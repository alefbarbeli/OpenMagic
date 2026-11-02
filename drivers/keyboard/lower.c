#include "OpenMagicKbd.h"

static EVT_WDF_REQUEST_COMPLETION_ROUTINE OmLowerReadComplete;

// Fetches the collection's preparsed data so reports can be decoded with
// HidP_* regardless of transport (USB and Bluetooth lay reports out differently).
// Failures are not fatal: the keyboard keeps working, Fn just never registers.
NTSTATUS
OmLowerSelfManagedIoInit(_In_ WDFDEVICE Device)
{
    POM_DEVICE_CONTEXT ctx = OmGetContext(Device);
    WDFIOTARGET target = WdfDeviceGetIoTarget(Device);
    HID_COLLECTION_INFORMATION info;
    WDF_MEMORY_DESCRIPTOR output;
    PHIDP_PREPARSED_DATA ppd;
    HIDP_CAPS caps;
    NTSTATUS status;

    RtlZeroMemory(&info, sizeof(info));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&output, &info, sizeof(info));
    status = WdfIoTargetSendIoctlSynchronously(target, NULL, IOCTL_HID_GET_COLLECTION_INFORMATION,
                                               NULL, &output, NULL, NULL);
    if (!NT_SUCCESS(status) || info.DescriptorSize == 0) {
        return STATUS_SUCCESS;
    }

    ppd = ExAllocatePool2(POOL_FLAG_NON_PAGED, info.DescriptorSize, OM_POOL_TAG);
    if (ppd == NULL) {
        return STATUS_SUCCESS;
    }
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&output, ppd, info.DescriptorSize);
    status = WdfIoTargetSendIoctlSynchronously(target, NULL, IOCTL_HID_GET_COLLECTION_DESCRIPTOR,
                                               NULL, &output, NULL, NULL);
    if (!NT_SUCCESS(status) || HidP_GetCaps(ppd, &caps) != HIDP_STATUS_SUCCESS || caps.InputReportByteLength == 0) {
        ExFreePoolWithTag(ppd, OM_POOL_TAG);
        return STATUS_SUCCESS;
    }

    ctx->InputReportLength = caps.InputReportByteLength;
    InterlockedExchangePointer((PVOID volatile *)&ctx->PreparsedData, ppd);
    return STATUS_SUCCESS;
}

VOID
OmLowerCleanup(_In_ POM_DEVICE_CONTEXT Ctx)
{
    PHIDP_PREPARSED_DATA ppd = InterlockedExchangePointer((PVOID volatile *)&Ctx->PreparsedData, NULL);

    if (ppd != NULL) {
        ExFreePoolWithTag(ppd, OM_POOL_TAG);
    }
}

VOID
OmLowerHandleRead(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request)
{
    WdfRequestFormatRequestUsingCurrentType(Request);
    WdfRequestSetCompletionRoutine(Request, OmLowerReadComplete, OmGetContext(Device));
    if (!WdfRequestSend(Request, WdfDeviceGetIoTarget(Device), WDF_NO_SEND_OPTIONS)) {
        WdfRequestComplete(Request, WdfRequestGetStatus(Request));
    }
}

// Apple keyboards report Fn as usage 0x03 on one of these vendor pages (the
// set Linux hid-apple accepts), either as a button or as a value.
static const USAGE g_FnPages[] = { 0xFF00, 0xFF01, 0x00FF };

// Returns TRUE when the report carries the Fn field.
static BOOLEAN
OmReportFnState(_In_ POM_DEVICE_CONTEXT Ctx, _In_ PHIDP_PREPARSED_DATA Ppd, _In_ PCHAR Report, _Out_ PLONG FnDown)
{
    const USAGE *pages = g_FnPages;
    BOOLEAN present = FALSE;
    ULONG i, j;

    *FnDown = 0;
    for (i = 0; i < ARRAYSIZE(g_FnPages); i++) {
        USAGE usages[8];
        ULONG count = ARRAYSIZE(usages);
        ULONG value;

        if (HidP_GetUsages(HidP_Input, pages[i], 0, usages, &count, Ppd, Report, Ctx->InputReportLength) == HIDP_STATUS_SUCCESS) {
            present = TRUE;
            for (j = 0; j < count; j++) {
                if (usages[j] == 0x03) {
                    *FnDown = 1;
                }
            }
        }
        if (HidP_GetUsageValue(HidP_Input, pages[i], 0, 0x03, &value, Ppd, Report, Ctx->InputReportLength) == HIDP_STATUS_SUCCESS) {
            present = TRUE;
            if (value != 0) {
                *FnDown = 1;
            }
        }
    }
    return present;
}

// Clears the Fn field before kbdhid parses the report. kbdhid has no scancode
// for these vendor usages, and a key released in the same report as an Fn
// change never reached kbdclass, leaving it stuck down.
static VOID
OmStripFn(_In_ POM_DEVICE_CONTEXT Ctx, _In_ PHIDP_PREPARSED_DATA Ppd, _Inout_ PCHAR Report)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(g_FnPages); i++) {
        USAGE usage = 0x03;
        ULONG count = 1;

        (VOID)HidP_UnsetUsages(HidP_Input, g_FnPages[i], 0, &usage, &count, Ppd, Report, Ctx->InputReportLength);
        (VOID)HidP_SetUsageValue(HidP_Input, g_FnPages[i], 0, 0x03, 0, Ppd, Report, Ctx->InputReportLength);
    }
}

// kbdhid reads with its own nonpaged buffer; depending on the HID stack's I/O
// type it arrives as an MDL, a system buffer or a kernel-mode user buffer.
static PVOID
OmLowerReadBuffer(_In_ WDFREQUEST Request)
{
    PIRP irp = WdfRequestWdmGetIrp(Request);

    if (irp->MdlAddress != NULL) {
        return MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
    }
    if (irp->AssociatedIrp.SystemBuffer != NULL) {
        return irp->AssociatedIrp.SystemBuffer;
    }
    return irp->RequestorMode == KernelMode ? irp->UserBuffer : NULL;
}

// Runs before kbdhid's own completion, so the Fn state is current by the time
// kbdhid turns this report into scancodes for the upper instance.
static VOID
OmLowerReadComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context)
{
    POM_DEVICE_CONTEXT ctx = (POM_DEVICE_CONTEXT)Context;
    PHIDP_PREPARSED_DATA ppd = ctx->PreparsedData;
    NTSTATUS status = Params->IoStatus.Status;
    ULONG_PTR information = Params->IoStatus.Information;
    PVOID buffer;
    size_t offset;

    UNREFERENCED_PARAMETER(Target);

    if (NT_SUCCESS(status) && ppd != NULL && (buffer = OmLowerReadBuffer(Request)) != NULL) {
        for (offset = 0; offset + ctx->InputReportLength <= information; offset += ctx->InputReportLength) {
            PCHAR report = (PCHAR)buffer + offset;
            LONG fnDown;

            BOOLEAN hasFn = OmReportFnState(ctx, ppd, report, &fnDown);

            if (hasFn) {
                InterlockedExchange(&ctx->Shared->FnDown, fnDown);
            }
            OmDebugAdd(ctx->Shared, OM_DEBUG_REPORT, 0, 0, (const UCHAR *)report, ctx->InputReportLength);
            if (hasFn) {
                OmStripFn(ctx, ppd, report);
            }
        }
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}
