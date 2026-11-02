#include "OpenMagicKbd.h"

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD OmEvtDeviceAdd;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP OmEvtDeviceCleanup;
static EVT_WDF_IO_QUEUE_IO_READ OmEvtIoRead;
static EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OmEvtIoInternalDeviceControl;

static LIST_ENTRY g_SharedList;
static KSPIN_LOCK g_SharedLock;

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    InitializeListHead(&g_SharedList);
    KeInitializeSpinLock(&g_SharedLock);

    WDF_DRIVER_CONFIG_INIT(&config, OmEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

// AddDevice runs twice for the same PDO; the upper instance is the one added
// after kbdhid has attached its function device object.
static OM_ROLE
OmDetectRole(_In_ PDEVICE_OBJECT Pdo)
{
    static const UNICODE_STRING kbdhid = RTL_CONSTANT_STRING(L"\\Driver\\kbdhid");
    PDEVICE_OBJECT top = IoGetAttachedDeviceReference(Pdo);
    OM_ROLE role = RtlEqualUnicodeString(&top->DriverObject->DriverName, &kbdhid, TRUE) ? OmRoleUpper : OmRoleLower;

    ObDereferenceObject(top);
    return role;
}

static NTSTATUS
OmEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    PDEVICE_OBJECT pdo = WdfFdoInitWdmGetPhysicalDevice(DeviceInit);
    OM_ROLE role = OmDetectRole(pdo);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    POM_DEVICE_CONTEXT ctx;
    WDFDEVICE device;
    NTSTATUS status;

    WdfFdoInitSetFilter(DeviceInit);

    if (role == OmRoleUpper) {
        WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_KEYBOARD);
    } else {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpPower;
        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPower);
        pnpPower.EvtDeviceSelfManagedIoInit = OmLowerSelfManagedIoInit;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPower);
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OM_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = OmEvtDeviceCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ctx = OmGetContext(device);
    ctx->Role = role;
    ctx->Shared = OmSharedAcquire(pdo);
    if (ctx->Shared == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (role == OmRoleUpper) {
        OmLoadKeymaps(Driver, ctx);
        status = OmDebugInitialize(device);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    // Request types without a callback are forwarded by the framework.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = OmEvtIoRead;
    queueConfig.EvtIoInternalDeviceControl = OmEvtIoInternalDeviceControl;
    return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

static VOID
OmEvtDeviceCleanup(_In_ WDFOBJECT Object)
{
    POM_DEVICE_CONTEXT ctx = OmGetContext((WDFDEVICE)Object);

    OmLowerCleanup(ctx);
    if (ctx->Shared != NULL) {
        OmSharedRelease(ctx->Shared);
        ctx->Shared = NULL;
    }
}

static VOID
OmEvtIoRead(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t Length)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);

    UNREFERENCED_PARAMETER(Length);

    if (OmGetContext(device)->Role == OmRoleLower) {
        OmLowerHandleRead(device, Request);
    } else {
        OmForwardRequest(Request, WdfDeviceGetIoTarget(device));
    }
}

static VOID
OmEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (OmGetContext(device)->Role == OmRoleUpper) {
        status = OmUpperHandleInternalIoctl(device, Request, IoControlCode);
    }
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    OmForwardRequest(Request, WdfDeviceGetIoTarget(device));
}

VOID
OmForwardRequest(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
{
    WDF_REQUEST_SEND_OPTIONS options;

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (!WdfRequestSend(Request, Target, &options)) {
        WdfRequestComplete(Request, WdfRequestGetStatus(Request));
    }
}

POM_SHARED
OmSharedAcquire(_In_ PDEVICE_OBJECT Pdo)
{
    POM_SHARED fresh = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(OM_SHARED), OM_POOL_TAG);
    POM_SHARED found = NULL;
    PLIST_ENTRY entry;
    KIRQL irql;

    KeAcquireSpinLock(&g_SharedLock, &irql);
    for (entry = g_SharedList.Flink; entry != &g_SharedList; entry = entry->Flink) {
        POM_SHARED shared = CONTAINING_RECORD(entry, OM_SHARED, Link);
        if (shared->Pdo == Pdo) {
            shared->RefCount++;
            found = shared;
            break;
        }
    }
    if (found == NULL && fresh != NULL) {
        fresh->Pdo = Pdo;
        fresh->RefCount = 1;
        KeInitializeSpinLock(&fresh->DebugLock);
        InsertTailList(&g_SharedList, &fresh->Link);
        found = fresh;
        fresh = NULL;
    }
    KeReleaseSpinLock(&g_SharedLock, irql);

    if (fresh != NULL) {
        ExFreePoolWithTag(fresh, OM_POOL_TAG);
    }
    return found;
}

VOID
OmSharedRelease(_In_ POM_SHARED Shared)
{
    BOOLEAN last;
    KIRQL irql;

    KeAcquireSpinLock(&g_SharedLock, &irql);
    last = (--Shared->RefCount == 0);
    if (last) {
        RemoveEntryList(&Shared->Link);
    }
    KeReleaseSpinLock(&g_SharedLock, irql);

    if (last) {
        ExFreePoolWithTag(Shared, OM_POOL_TAG);
    }
}
