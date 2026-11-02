#include "OpenMagicMouse.h"

// Bluetooth HID transaction headers (HID profile spec): the high nibble is the
// transaction type, the low nibble its parameter.
#define OMM_HIDP_DATA_INPUT          0xA1
#define OMM_HIDP_SET_REPORT_FEATURE  0x53

#define OMM_ENABLE_RETRY_INTERVAL    (10 * 1000 * 1000)    // 1 s, in 100 ns units
#define OMM_ENABLE_MAX_ATTEMPTS      5

// Feature report 0xF1 = { 0x02, 0x01 } switches the Magic Mouse 2 into
// multitouch reporting (documented by the Linux hid-magicmouse driver).
static UCHAR g_EnableMultitouch[] = { OMM_HIDP_SET_REPORT_FEATURE, 0xF1, 0x02, 0x01 };

static EVT_WDF_REQUEST_COMPLETION_ROUTINE OmmBrbComplete;
static EVT_WDF_WORKITEM OmmEnableWork;

NTSTATUS
OmmBtInitialize(_In_ WDFDEVICE Device)
{
    POMM_DEVICE_CONTEXT ctx = OmmGetContext(Device);
    WDF_WORKITEM_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;

    KeInitializeSpinLock(&ctx->ChannelLock);
    KeInitializeSpinLock(&ctx->GestureLock);

    WDF_WORKITEM_CONFIG_INIT(&config, OmmEnableWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    status = WdfWorkItemCreate(&config, &attributes, &ctx->EnableWorkItem);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    return OmmDebugInitialize(Device);
}

NTSTATUS
OmmBtSelfManagedIoInit(_In_ WDFDEVICE Device)
{
    POMM_DEVICE_CONTEXT ctx = OmmGetContext(Device);
    NTSTATUS status;

    status = WdfFdoQueryForInterface(Device, &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE, (PINTERFACE)&ctx->Bth,
                                     sizeof(ctx->Bth), BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI, NULL);
    ctx->HaveBth = NT_SUCCESS(status);
    return STATUS_SUCCESS;
}

VOID
OmmBtCleanup(_In_ POMM_DEVICE_CONTEXT Ctx)
{
    if (Ctx->HaveBth) {
        Ctx->Bth.Interface.InterfaceDereference(Ctx->Bth.Interface.Context);
        Ctx->HaveBth = FALSE;
    }
}

static PBRB
OmmRequestBrb(_In_ WDFREQUEST Request)
{
    return (PBRB)IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(Request))->Parameters.Others.Argument1;
}

// Only data transfers matter; everything else HidBth sends passes straight down.
// Reads on the interrupt channel are redirected into the request's own packet
// buffer and copied back on completion.
VOID
OmmBtHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode)
{
    POMM_DEVICE_CONTEXT ctx = OmmGetContext(Device);
    PBRB brb;

    if (IoControlCode == IOCTL_INTERNAL_BTH_SUBMIT_BRB &&
        (brb = OmmRequestBrb(Request))->BrbHeader.Type == BRB_L2CA_ACL_TRANSFER) {
        struct _BRB_L2CA_ACL_TRANSFER *acl = &brb->BrbL2caAclTransfer;
        OMM_REQUEST_CONTEXT *rc = OmmGetRequestContext(Request);

        rc->Swapped = FALSE;
        if ((acl->TransferFlags & ACL_TRANSFER_DIRECTION_IN) && ctx->Interrupt != NULL &&
            acl->ChannelHandle == ctx->Interrupt) {
            rc->OriginalBuffer = acl->Buffer;
            rc->OriginalMdl = acl->BufferMDL;
            rc->OriginalSize = acl->BufferSize;
            acl->Buffer = rc->Packet;
            acl->BufferMDL = NULL;
            acl->BufferSize = sizeof(rc->Packet);
            rc->Swapped = TRUE;
        }

        WdfRequestFormatRequestUsingCurrentType(Request);
        WdfRequestSetCompletionRoutine(Request, OmmBrbComplete, ctx);
        if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(Device), WDF_NO_SEND_OPTIONS)) {
            if (rc->Swapped) {
                acl->Buffer = rc->OriginalBuffer;
                acl->BufferMDL = rc->OriginalMdl;
                acl->BufferSize = rc->OriginalSize;
            }
            WdfRequestComplete(Request, WdfRequestGetStatus(Request));
        }
        return;
    }
    OmmForwardRequest(Request, WdfDeviceGetIoTarget(Device));
}

// HANDSHAKE, HID_CONTROL, GET/SET_REPORT, GET/SET_PROTOCOL and GET/SET_IDLE
// only ever travel on the HID control channel.
static BOOLEAN
OmmIsControlMessage(_In_ UCHAR Header)
{
    switch (Header >> 4) {
    case 0x0: case 0x1: case 0x4: case 0x5: case 0x6: case 0x7: case 0x8: case 0x9:
        return TRUE;
    default:
        return FALSE;
    }
}

static VOID
OmmLearnControlChannel(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ L2CAP_CHANNEL_HANDLE Channel, _In_ const UCHAR *Message)
{
    BOOLEAN changed;
    KIRQL irql;

    KeAcquireSpinLock(&Ctx->ChannelLock, &irql);
    changed = Ctx->Control != Channel;
    Ctx->Control = Channel;
    KeReleaseSpinLock(&Ctx->ChannelLock, irql);

    if (changed) {
        OmmDebugEvent(Ctx, OMM_EVENT_CONTROL, 0, Message, 1);
    }
}

// A new interrupt channel means the mouse (re)connected in its default mode.
static VOID
OmmLearnInterruptChannel(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ L2CAP_CHANNEL_HANDLE Channel, _In_ BTH_ADDR Address)
{
    BOOLEAN changed;
    KIRQL irql;

    KeAcquireSpinLock(&Ctx->ChannelLock, &irql);
    changed = Ctx->Interrupt != Channel;
    if (changed) {
        Ctx->Interrupt = Channel;
        Ctx->Address = Address;
        Ctx->EnableAttempts = 0;
        InterlockedExchange(&Ctx->MultitouchOn, 0);
    }
    KeReleaseSpinLock(&Ctx->ChannelLock, irql);

    if (changed) {
        OmmDebugEvent(Ctx, OMM_EVENT_INTERRUPT, 0, NULL, 0);
    }
}

static VOID
OmmRequestMultitouch(_In_ POMM_DEVICE_CONTEXT Ctx)
{
    ULONGLONG now = KeQueryInterruptTime();

    if (Ctx->EnableAttempts >= OMM_ENABLE_MAX_ATTEMPTS || now - Ctx->LastEnableTime < OMM_ENABLE_RETRY_INTERVAL) {
        return;
    }
    if (InterlockedCompareExchange(&Ctx->EnableQueued, 1, 0) == 0) {
        Ctx->LastEnableTime = now;
        WdfWorkItemEnqueue(Ctx->EnableWorkItem);
    }
}

// Swapped is TRUE when the read used the whole-packet buffer.
static VOID
OmmAclCompleted(_In_ POMM_DEVICE_CONTEXT Ctx, _Inout_ struct _BRB_L2CA_ACL_TRANSFER *Acl, _In_ BOOLEAN Swapped)
{
    BOOLEAN inbound = (Acl->TransferFlags & ACL_TRANSFER_DIRECTION_IN) != 0;
    PUCHAR data = (PUCHAR)Acl->Buffer;
    ULONG length = Acl->BufferSize;

    if (data == NULL && Acl->BufferMDL != NULL) {
        data = (PUCHAR)MmGetSystemAddressForMdlSafe(Acl->BufferMDL, NormalPagePriority | MdlMappingNoExecute);
    }
    if (data == NULL || length == 0) {
        return;
    }

    if (OmmIsControlMessage(data[0]) && !(inbound && Acl->ChannelHandle == Ctx->Interrupt)) {
        OmmLearnControlChannel(Ctx, Acl->ChannelHandle, data);
        if (inbound) {
            OmmDebugEvent(Ctx, OMM_EVENT_OTHER, 0, data, length);
        }
        return;
    }
    if (!inbound) {
        return;
    }
    if (length < 2 || data[0] != OMM_HIDP_DATA_INPUT || data[1] != OMM_REPORT_MOUSE) {
        OmmDebugEvent(Ctx, OMM_EVENT_OTHER, 0, data, length);
        return;
    }

    // Report 0x12 only ever arrives on the interrupt channel.
    OmmLearnInterruptChannel(Ctx, Acl->ChannelHandle, Acl->BtAddress);
    OmmDebugReport(Ctx, data + 1, length - 1);

    if (length - 1 >= OMM_REPORT_TOUCH_OFFSET) {
        InterlockedExchange(&Ctx->MultitouchOn, 1);
        OmmProcessReport(Ctx, data + 1, length - 1);
        // Give HidBth the report its descriptor declares; it carries the
        // pointer motion and buttons, so the inbox mouse driver keeps working.
        Acl->BufferSize = 1 + OMM_REPORT_BASE_LENGTH;
    } else if (Ctx->MultitouchOn) {
        // In multitouch mode the plain report means no finger is on the surface.
        OmmEndGesture(Ctx);
    } else if (Swapped) {
        // Only once HidBth's reads use whole-packet buffers: a read still
        // pending with the small buffer would hand it a split packet.
        OmmRequestMultitouch(Ctx);
    }
}

static VOID
OmmBrbComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context)
{
    NTSTATUS status = Params->IoStatus.Status;
    PBRB brb = OmmRequestBrb(Request);
    struct _BRB_L2CA_ACL_TRANSFER *acl = &brb->BrbL2caAclTransfer;
    OMM_REQUEST_CONTEXT *rc = OmmGetRequestContext(Request);
    BOOLEAN succeeded = NT_SUCCESS(status) && NT_SUCCESS(brb->BrbHeader.Status);

    UNREFERENCED_PARAMETER(Target);

    if (succeeded) {
        OmmAclCompleted((POMM_DEVICE_CONTEXT)Context, acl, rc->Swapped);
    }

    // Hand HidBth its own buffer back, holding at most what it asked for
    // (for report 0x12 that is the plain 8-byte report).
    if (rc->Swapped) {
        ULONG deliver = rc->OriginalSize;

        if (succeeded) {
            PVOID target = rc->OriginalBuffer;

            if (target == NULL && rc->OriginalMdl != NULL) {
                target = MmGetSystemAddressForMdlSafe(rc->OriginalMdl, NormalPagePriority | MdlMappingNoExecute);
            }
            deliver = min(acl->BufferSize, rc->OriginalSize);
            if (target != NULL) {
                RtlCopyMemory(target, rc->Packet, deliver);
            } else {
                deliver = 0;
            }
        }
        acl->Buffer = rc->OriginalBuffer;
        acl->BufferMDL = rc->OriginalMdl;
        acl->BufferSize = deliver;
        rc->Swapped = FALSE;
    }

    WdfRequestCompleteWithInformation(Request, status, Params->IoStatus.Information);
}

// Sends SET_REPORT(Feature 0xF1) on the HID control channel. The device's
// HANDSHAKE reply is left to HidBth's pending read on that channel.
static VOID
OmmEnableWork(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    POMM_DEVICE_CONTEXT ctx = OmmGetContext(device);
    L2CAP_CHANNEL_HANDLE control, interrupt;
    struct _BRB_L2CA_ACL_TRANSFER *brb;
    WDF_MEMORY_DESCRIPTOR brbDescriptor;
    WDF_REQUEST_SEND_OPTIONS options;
    BTH_ADDR address;
    NTSTATUS status;
    KIRQL irql;

    KeAcquireSpinLock(&ctx->ChannelLock, &irql);
    control = ctx->Control;
    interrupt = ctx->Interrupt;
    address = ctx->Address;
    KeReleaseSpinLock(&ctx->ChannelLock, irql);

    if (control == NULL || control == interrupt || !ctx->HaveBth) {
        UCHAR interruptKnown = interrupt != NULL;
        OmmDebugEvent(ctx, OMM_EVENT_ENABLE_SKIP, control != NULL, &interruptKnown, sizeof(interruptKnown));
        InterlockedExchange(&ctx->EnableQueued, 0);
        return;
    }

    ctx->EnableAttempts++;
    brb = (struct _BRB_L2CA_ACL_TRANSFER *)ctx->Bth.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, OMM_POOL_TAG);
    if (brb != NULL) {
        brb->BtAddress = address;
        brb->ChannelHandle = control;
        brb->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
        brb->BufferSize = sizeof(g_EnableMultitouch);
        brb->Buffer = g_EnableMultitouch;

        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&brbDescriptor, brb, sizeof(*brb));
        WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
        WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));
        status = WdfIoTargetSendInternalIoctlOthersSynchronously(WdfDeviceGetIoTarget(device), NULL,
            IOCTL_INTERNAL_BTH_SUBMIT_BRB, &brbDescriptor, NULL, NULL, &options, NULL);
        if (NT_SUCCESS(status)) {
            status = brb->Hdr.Status;
        }
        ctx->Bth.BthFreeBrb((PBRB)brb);
        OmmDebugEnable(ctx, status);

        // A stale handle (e.g. after a reconnect) is relearned from new traffic.
        if (!NT_SUCCESS(status)) {
            KeAcquireSpinLock(&ctx->ChannelLock, &irql);
            if (ctx->Control == control) {
                ctx->Control = NULL;
            }
            KeReleaseSpinLock(&ctx->ChannelLock, irql);
        }
    }

    InterlockedExchange(&ctx->EnableQueued, 0);
}
