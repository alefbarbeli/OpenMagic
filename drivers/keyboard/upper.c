#include "OpenMagicKbd.h"

#define OM_BATCH 32

static VOID OmUpperServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed);

NTSTATUS
OmUpperHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode)
{
    POM_DEVICE_CONTEXT ctx = OmGetContext(Device);
    PCONNECT_DATA connectData;
    size_t length;
    NTSTATUS status;

    switch (IoControlCode) {
    case IOCTL_INTERNAL_KEYBOARD_CONNECT:
        // Insert ourselves between kbdhid and kbdclass, as the WDK kbfiltr sample does.
        if (ctx->ClassConnect.ClassService != NULL) {
            return STATUS_SHARING_VIOLATION;
        }
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), (PVOID *)&connectData, &length);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        ctx->ClassConnect = *connectData;
        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(Device);
        connectData->ClassService = (PVOID)(ULONG_PTR)OmUpperServiceCallback;
        return STATUS_SUCCESS;

    case IOCTL_INTERNAL_KEYBOARD_DISCONNECT:
        return STATUS_NOT_IMPLEMENTED;

    default:
        return STATUS_SUCCESS;
    }
}

static BOOLEAN
OmFindMapping(_In_ const OM_KEYMAP *Map, _In_ USHORT Key, _Out_ PULONG Index)
{
    ULONG i;

    for (i = 0; i < Map->Count; i++) {
        if (Map->Entries[i].Source == Key) {
            *Index = i;
            return TRUE;
        }
    }
    *Index = 0;
    return FALSE;
}

static const OM_MAPPING *
OmActiveMapping(_In_ POM_DEVICE_CONTEXT Ctx, _In_ USHORT Active)
{
    ULONG index = Active - 1u;

    return index < OM_MAX_MAPPINGS ? &Ctx->Map.Entries[index] : &Ctx->FnMap.Entries[index - OM_MAX_MAPPINGS];
}

static VOID
OmEmitKey(_In_ USHORT Key, _In_ const KEYBOARD_INPUT_DATA *Template, _In_ BOOLEAN Release,
          _Out_writes_(OM_BATCH) PKEYBOARD_INPUT_DATA Out, _Inout_ PULONG Count)
{
    KEYBOARD_INPUT_DATA data = *Template;
    USHORT baseFlags = (USHORT)(Template->Flags & ~(KEY_BREAK | KEY_E0 | KEY_E1));
    USHORT releaseFlag = Release ? KEY_BREAK : KEY_MAKE;

    if (Key == OM_KEY_PAUSE) {
        // Pause has no single scancode: it is reported as E1 1D followed by 45.
        data.MakeCode = 0x1D;
        data.Flags = (USHORT)(baseFlags | KEY_E1 | releaseFlag);
        Out[(*Count)++] = data;
        data.MakeCode = 0x45;
        data.Flags = (USHORT)(baseFlags | releaseFlag);
    } else {
        data.MakeCode = (USHORT)(Key & 0xFF);
        data.Flags = (USHORT)(baseFlags | ((Key & 0xFF00) == 0xE000 ? KEY_E0 : 0) | releaseFlag);
    }
    Out[(*Count)++] = data;
}

// Translates one record. The mapping is chosen when the key goes down and the
// same mapping is used for its repeats and release, so letting go of Fn before
// the key cannot leave a media key or chord stuck down.
static VOID
OmTranslate(_In_ POM_DEVICE_CONTEXT Ctx, _In_ const KEYBOARD_INPUT_DATA *In,
            _Out_writes_(OM_BATCH) PKEYBOARD_INPUT_DATA Out, _Inout_ PULONG Count)
{
    BOOLEAN release = (In->Flags & KEY_BREAK) != 0;
    BOOLEAN e0 = (In->Flags & KEY_E0) != 0;
    USHORT key = (USHORT)((In->MakeCode & 0xFF) | (e0 ? 0xE000 : 0));
    ULONG id = (In->MakeCode & 0xFF) | (e0 ? 0x100 : 0);
    USHORT active = Ctx->Active[id];
    const OM_MAPPING *mapping = NULL;
    const OM_TARGET *target;
    ULONG index, i;

    if (In->Flags & KEY_E1) {
        Out[(*Count)++] = *In;
        return;
    }

    if (active != 0) {
        mapping = OmActiveMapping(Ctx, active);
    } else if (!release) {
        if (Ctx->Shared->FnDown && OmFindMapping(&Ctx->FnMap, key, &index)) {
            mapping = &Ctx->FnMap.Entries[index];
            Ctx->Active[id] = (USHORT)(1 + OM_MAX_MAPPINGS + index);
        } else if (OmFindMapping(&Ctx->Map, key, &index)) {
            mapping = &Ctx->Map.Entries[index];
            Ctx->Active[id] = (USHORT)(1 + index);
        }
    }

    if (mapping == NULL) {
        Out[(*Count)++] = *In;
        return;
    }

    target = &mapping->Target;
    if (release) {
        Ctx->Active[id] = 0;
        for (i = target->Count; i-- > 0;) {
            OmEmitKey(target->Keys[i], In, TRUE, Out, Count);
        }
    } else if (active != 0) {
        // Typematic repeat: repeat only the last key of the chord.
        if (target->Count != 0) {
            OmEmitKey(target->Keys[target->Count - 1], In, FALSE, Out, Count);
        }
    } else {
        for (i = 0; i < target->Count; i++) {
            OmEmitKey(target->Keys[i], In, FALSE, Out, Count);
        }
    }
}

static VOID
OmDeliver(_In_ POM_DEVICE_CONTEXT Ctx, _In_reads_(*Count) PKEYBOARD_INPUT_DATA Data, _Inout_ PULONG Count)
{
    ULONG consumed = 0;
    ULONG i;

    for (i = 0; i < *Count; i++) {
        OmDebugAdd(Ctx->Shared, OM_DEBUG_OUT, Data[i].MakeCode, Data[i].Flags, NULL, 0);
    }
    if (*Count != 0) {
        (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)Ctx->ClassConnect.ClassService)(
            Ctx->ClassConnect.ClassDeviceObject, Data, Data + *Count, &consumed);
        *Count = 0;
    }
}

// Called by kbdhid (at up to DISPATCH_LEVEL) with the scancodes of each report.
static VOID
OmUpperServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PKEYBOARD_INPUT_DATA InputDataStart,
    _In_ PKEYBOARD_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed)
{
    POM_DEVICE_CONTEXT ctx = OmGetContext(WdfWdmDeviceGetWdfDeviceHandle(DeviceObject));
    KEYBOARD_INPUT_DATA out[OM_BATCH];
    PKEYBOARD_INPUT_DATA in;
    ULONG count = 0;

    for (in = InputDataStart; in < InputDataEnd; in++) {
        OmDebugAdd(ctx->Shared, OM_DEBUG_IN, in->MakeCode, in->Flags, NULL, 0);
        if (count + OM_MAX_EMIT > OM_BATCH) {
            OmDeliver(ctx, out, &count);
        }
        OmTranslate(ctx, in, out, &count);
    }
    OmDeliver(ctx, out, &count);
    OmDebugFlush(ctx);

    *InputDataConsumed = (ULONG)(InputDataEnd - InputDataStart);
}
