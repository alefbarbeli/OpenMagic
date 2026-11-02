#include "OpenMagicMouse.h"

#define OMM_DEBUG_OTHER_LIMIT 16

static EVT_WDF_WORKITEM OmmDebugWork;

NTSTATUS
OmmDebugInitialize(_In_ WDFDEVICE Device)
{
    POMM_DEVICE_CONTEXT ctx = OmmGetContext(Device);
    WDF_WORKITEM_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    KeInitializeSpinLock(&ctx->DebugLock);
    if (!ctx->Settings.Debug) {
        return STATUS_SUCCESS;
    }
    WDF_WORKITEM_CONFIG_INIT(&config, OmmDebugWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    return WdfWorkItemCreate(&config, &attributes, &ctx->DebugWorkItem);
}

static VOID
OmmDebugDirty(_In_ POMM_DEVICE_CONTEXT Ctx)
{
    if (Ctx->DebugWorkItem != NULL && InterlockedCompareExchange(&Ctx->DebugQueued, 1, 0) == 0) {
        WdfWorkItemEnqueue(Ctx->DebugWorkItem);
    }
}

VOID
OmmDebugEvent(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ UCHAR Kind, _In_ LONG Value,
              _In_reads_opt_(Length) const UCHAR *Data, _In_ ULONG Length)
{
    OMM_DEBUG_EVENT *event;
    KIRQL irql;

    if (!Ctx->Settings.Debug) {
        return;
    }
    KeAcquireSpinLock(&Ctx->DebugLock, &irql);
    if (Kind == OMM_EVENT_OTHER && ++Ctx->Stats.OtherInbound > OMM_DEBUG_OTHER_LIMIT) {
        KeReleaseSpinLock(&Ctx->DebugLock, irql);
        OmmDebugDirty(Ctx);
        return;
    }
    event = &Ctx->Events[Ctx->EventCount++ % OMM_DEBUG_EVENTS];
    RtlZeroMemory(event, sizeof(*event));
    event->Kind = Kind;
    event->Length = (USHORT)min(Length, 0xFFFF);
    event->Value = Value;
    if (Data != NULL) {
        RtlCopyMemory(event->Data, Data, min(Length, sizeof(event->Data)));
    }
    KeReleaseSpinLock(&Ctx->DebugLock, irql);
    OmmDebugDirty(Ctx);
}

// Counts every report 0x12 and records an event whenever it switches between
// the plain 8-byte form and the multitouch form.
VOID
OmmDebugReport(_In_ POMM_DEVICE_CONTEXT Ctx, _In_reads_(Length) const UCHAR *Report, _In_ ULONG Length)
{
    BOOLEAN touch = Length >= OMM_REPORT_TOUCH_OFFSET;
    BOOLEAN changed;
    KIRQL irql;

    if (!Ctx->Settings.Debug) {
        return;
    }
    KeAcquireSpinLock(&Ctx->DebugLock, &irql);
    if (touch) {
        Ctx->Stats.TouchReports++;
        Ctx->Stats.LastLength = Length;
        RtlCopyMemory(Ctx->Stats.LastReport, Report, min(Length, sizeof(Ctx->Stats.LastReport)));
    } else {
        Ctx->Stats.ShortReports++;
    }
    changed = touch != Ctx->LastWasTouch || Ctx->Stats.TouchReports + Ctx->Stats.ShortReports == 1;
    Ctx->LastWasTouch = touch;
    KeReleaseSpinLock(&Ctx->DebugLock, irql);

    if (changed) {
        OmmDebugEvent(Ctx, OMM_EVENT_REPORT, 0, Report, Length);
    } else {
        OmmDebugDirty(Ctx);
    }
}

VOID
OmmDebugWheel(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ LONG Amount)
{
    KIRQL irql;

    if (!Ctx->Settings.Debug) {
        return;
    }
    KeAcquireSpinLock(&Ctx->DebugLock, &irql);
    Ctx->Stats.WheelEvents++;
    Ctx->Stats.LastWheel = Amount;
    KeReleaseSpinLock(&Ctx->DebugLock, irql);
    OmmDebugDirty(Ctx);
}

VOID
OmmDebugEnable(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ NTSTATUS Status)
{
    KIRQL irql;

    if (!Ctx->Settings.Debug) {
        return;
    }
    KeAcquireSpinLock(&Ctx->DebugLock, &irql);
    Ctx->Stats.EnableAttempts++;
    Ctx->Stats.LastEnableStatus = Status;
    KeReleaseSpinLock(&Ctx->DebugLock, irql);
    OmmDebugEvent(Ctx, OMM_EVENT_ENABLE, Status, NULL, 0);
}

// Waits briefly so a burst of reports results in one write, then stores the
// stats and events in Parameters\DebugLog. Anything logged after the snapshot
// queues the work item again, so the final state is always written.
static VOID
OmmDebugWork(_In_ WDFWORKITEM WorkItem)
{
    DECLARE_CONST_UNICODE_STRING(valueName, L"DebugLog");
    POMM_DEVICE_CONTEXT ctx = OmmGetContext((WDFDEVICE)WdfWorkItemGetParentObject(WorkItem));
    struct {
        OMM_DEBUG_STATS Stats;
        OMM_DEBUG_EVENT Events[OMM_DEBUG_EVENTS];
    } snapshot;
    LARGE_INTEGER delay;
    ULONG count, first, i;
    WDFKEY key;
    KIRQL irql;

    delay.QuadPart = -5 * 1000 * 1000;     // 500 ms
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    InterlockedExchange(&ctx->DebugQueued, 0);

    RtlZeroMemory(&snapshot, sizeof(snapshot));
    KeAcquireSpinLock(&ctx->DebugLock, &irql);
    snapshot.Stats = ctx->Stats;
    count = min(ctx->EventCount, OMM_DEBUG_EVENTS);
    first = ctx->EventCount - count;
    for (i = 0; i < count; i++) {
        snapshot.Events[i] = ctx->Events[(first + i) % OMM_DEBUG_EVENTS];
    }
    KeReleaseSpinLock(&ctx->DebugLock, irql);

    if (NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        (VOID)WdfRegistryAssignValue(key, &valueName, REG_BINARY,
                                     sizeof(OMM_DEBUG_STATS) + count * sizeof(OMM_DEBUG_EVENT), &snapshot);
        WdfRegistryClose(key);
    }
}
