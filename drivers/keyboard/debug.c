#include "OpenMagicKbd.h"

static EVT_WDF_WORKITEM OmDebugWork;

VOID
OmDebugAdd(_In_ POM_SHARED Shared, _In_ UCHAR Kind, _In_ USHORT MakeCode, _In_ USHORT Flags,
           _In_reads_opt_(Length) const UCHAR *Data, _In_ ULONG Length)
{
    OM_DEBUG_ENTRY *entry;
    KIRQL irql;

    if (!Shared->Debug) {
        return;
    }
    KeAcquireSpinLock(&Shared->DebugLock, &irql);
    entry = &Shared->DebugLog[Shared->DebugCount++ % OM_DEBUG_ENTRIES];
    RtlZeroMemory(entry, sizeof(*entry));
    entry->Kind = Kind;
    entry->FnDown = (UCHAR)Shared->FnDown;
    entry->MakeCode = MakeCode;
    entry->Flags = Flags;
    if (Data != NULL) {
        RtlCopyMemory(entry->Data, Data, min(Length, sizeof(entry->Data)));
    }
    KeReleaseSpinLock(&Shared->DebugLock, irql);
}

NTSTATUS
OmDebugInitialize(_In_ WDFDEVICE Device)
{
    WDF_WORKITEM_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_WORKITEM_CONFIG_INIT(&config, OmDebugWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    return WdfWorkItemCreate(&config, &attributes, &OmGetContext(Device)->DebugWorkItem);
}

// Called by the upper instance after each batch of scancodes.
VOID
OmDebugFlush(_In_ POM_DEVICE_CONTEXT Ctx)
{
    if (Ctx->Shared->Debug && Ctx->DebugWorkItem != NULL &&
        InterlockedCompareExchange(&Ctx->DebugQueued, 1, 0) == 0) {
        WdfWorkItemEnqueue(Ctx->DebugWorkItem);
    }
}

// Writes the trace, oldest entry first, to Parameters\DebugLog.
static VOID
OmDebugWork(_In_ WDFWORKITEM WorkItem)
{
    DECLARE_CONST_UNICODE_STRING(valueName, L"DebugLog");
    POM_DEVICE_CONTEXT ctx = OmGetContext((WDFDEVICE)WdfWorkItemGetParentObject(WorkItem));
    POM_SHARED shared = ctx->Shared;
    OM_DEBUG_ENTRY snapshot[OM_DEBUG_ENTRIES];
    ULONG count, first, i;
    WDFKEY key;
    KIRQL irql;

    InterlockedExchange(&ctx->DebugQueued, 0);

    KeAcquireSpinLock(&shared->DebugLock, &irql);
    count = min(shared->DebugCount, OM_DEBUG_ENTRIES);
    first = shared->DebugCount - count;
    for (i = 0; i < count; i++) {
        snapshot[i] = shared->DebugLog[(first + i) % OM_DEBUG_ENTRIES];
    }
    KeReleaseSpinLock(&shared->DebugLock, irql);

    if (count != 0 &&
        NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)WdfRegistryAssignValue(key, &valueName, REG_BINARY, count * sizeof(OM_DEBUG_ENTRY), snapshot);
        WdfRegistryClose(key);
    }
}
