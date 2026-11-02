#include "OpenMagicMouse.h"

// The mouse role never changes the data mouhid sends; it only records
// mouclass' service callback from the connect request so the Bluetooth role
// can deliver extra wheel events through the same path.
NTSTATUS
OmmMouseHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode)
{
    POMM_LINK link = OmmGetContext(Device)->Link;
    PCONNECT_DATA connectData;
    size_t length;
    NTSTATUS status;
    KIRQL irql;

    switch (IoControlCode) {
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), (PVOID *)&connectData, &length);
        if (!NT_SUCCESS(status)) {
            return status;
        }
        KeAcquireSpinLock(&link->Lock, &irql);
        link->ClassConnect = *connectData;
        KeReleaseSpinLock(&link->Lock, irql);
        return STATUS_SUCCESS;

    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        KeAcquireSpinLock(&link->Lock, &irql);
        RtlZeroMemory(&link->ClassConnect, sizeof(link->ClassConnect));
        KeReleaseSpinLock(&link->Lock, irql);
        return STATUS_SUCCESS;

    default:
        return STATUS_SUCCESS;
    }
}

VOID
OmmMouseCleanup(_In_ POMM_DEVICE_CONTEXT Ctx)
{
    KIRQL irql;

    if (Ctx->Link != NULL) {
        KeAcquireSpinLock(&Ctx->Link->Lock, &irql);
        RtlZeroMemory(&Ctx->Link->ClassConnect, sizeof(Ctx->Link->ClassConnect));
        KeReleaseSpinLock(&Ctx->Link->Lock, irql);
    }
}

// mouclass' callback expects DISPATCH_LEVEL, which holding the spin lock
// guarantees; the lock also keeps the mouse role from going away meanwhile.
VOID
OmmInjectWheel(_In_opt_ POMM_LINK Link, _In_ USHORT ButtonFlag, _In_ LONG Amount)
{
    MOUSE_INPUT_DATA data;
    ULONG consumed = 0;
    KIRQL irql;

    if (Link == NULL) {
        return;
    }

    RtlZeroMemory(&data, sizeof(data));
    data.Flags = MOUSE_MOVE_RELATIVE;
    data.ButtonFlags = ButtonFlag;
    data.ButtonData = (USHORT)(SHORT)Amount;

    KeAcquireSpinLock(&Link->Lock, &irql);
    if (Link->ClassConnect.ClassService != NULL) {
        (*(PSERVICE_CALLBACK_ROUTINE)(ULONG_PTR)Link->ClassConnect.ClassService)(
            Link->ClassConnect.ClassDeviceObject, &data, &data + 1, &consumed);
    }
    KeReleaseSpinLock(&Link->Lock, irql);
}
