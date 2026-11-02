#include <initguid.h>
#include "OpenMagicMouse.h"

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD OmmEvtDeviceAdd;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP OmmEvtDeviceCleanup;
static EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL OmmEvtIoInternalDeviceControl;

static LIST_ENTRY g_Links;
static KSPIN_LOCK g_LinksLock;

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;

    InitializeListHead(&g_Links);
    KeInitializeSpinLock(&g_LinksLock);

    WDF_DRIVER_CONFIG_INIT(&config, OmmEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

// The mouse role is the one added on top of mouhid; on the Bluetooth device
// node nothing but the BthEnum PDO is attached yet.
static OMM_ROLE
OmmDetectRole(_In_ PDEVICE_OBJECT Pdo)
{
    static const UNICODE_STRING mouhid = RTL_CONSTANT_STRING(L"\\Driver\\mouhid");
    PDEVICE_OBJECT top = IoGetAttachedDeviceReference(Pdo);
    OMM_ROLE role = RtlEqualUnicodeString(&top->DriverObject->DriverName, &mouhid, TRUE) ? OmmRoleMouse : OmmRoleBluetooth;

    ObDereferenceObject(top);
    return role;
}

// Both roles of one mouse share its container ID. If it can't be read the key
// stays empty, which still pairs the roles correctly with a single mouse.
static POMM_LINK
OmmLinkAcquire(_In_ PDEVICE_OBJECT Pdo)
{
    WCHAR containerId[OMM_CONTAINER_CHARS] = { 0 };
    POMM_LINK fresh, found = NULL;
    PLIST_ENTRY entry;
    ULONG length;
    KIRQL irql;

    (VOID)IoGetDeviceProperty(Pdo, DevicePropertyContainerID, sizeof(containerId) - sizeof(WCHAR), containerId, &length);

    fresh = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(OMM_LINK), OMM_POOL_TAG);
    KeAcquireSpinLock(&g_LinksLock, &irql);
    for (entry = g_Links.Flink; entry != &g_Links; entry = entry->Flink) {
        POMM_LINK link = CONTAINING_RECORD(entry, OMM_LINK, Link);
        if (RtlEqualMemory(link->ContainerId, containerId, sizeof(containerId))) {
            link->RefCount++;
            found = link;
            break;
        }
    }
    if (found == NULL && fresh != NULL) {
        fresh->RefCount = 1;
        KeInitializeSpinLock(&fresh->Lock);
        RtlCopyMemory(fresh->ContainerId, containerId, sizeof(containerId));
        InsertTailList(&g_Links, &fresh->Link);
        found = fresh;
        fresh = NULL;
    }
    KeReleaseSpinLock(&g_LinksLock, irql);

    if (fresh != NULL) {
        ExFreePoolWithTag(fresh, OMM_POOL_TAG);
    }
    return found;
}

static VOID
OmmLinkRelease(_In_ POMM_LINK Link)
{
    BOOLEAN last;
    KIRQL irql;

    KeAcquireSpinLock(&g_LinksLock, &irql);
    last = (--Link->RefCount == 0);
    if (last) {
        RemoveEntryList(&Link->Link);
    }
    KeReleaseSpinLock(&g_LinksLock, irql);

    if (last) {
        ExFreePoolWithTag(Link, OMM_POOL_TAG);
    }
}

// Settings live in HKLM\SYSTEM\CurrentControlSet\Services\OpenMagicMouse\Parameters.
static VOID
OmmLoadSettings(_In_ WDFDRIVER Driver, _Out_ OMM_SETTINGS *Settings)
{
    DECLARE_CONST_UNICODE_STRING(speedName, L"ScrollSpeed");
    DECLARE_CONST_UNICODE_STRING(naturalName, L"NaturalScrolling");
    DECLARE_CONST_UNICODE_STRING(horizontalName, L"HorizontalScrolling");
    DECLARE_CONST_UNICODE_STRING(smoothName, L"SmoothScrolling");
    DECLARE_CONST_UNICODE_STRING(debugName, L"Debug");
    ULONG value;
    WDFKEY key;

    Settings->ScrollSpeed = 100;
    Settings->Natural = TRUE;
    Settings->Horizontal = TRUE;
    Settings->Smooth = TRUE;
    Settings->Debug = FALSE;

    if (!NT_SUCCESS(WdfDriverOpenParametersRegistryKey(Driver, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        return;
    }
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &speedName, &value)) && value >= 10 && value <= 400) {
        Settings->ScrollSpeed = (LONG)value;
    }
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &naturalName, &value))) {
        Settings->Natural = value != 0;
    }
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &horizontalName, &value))) {
        Settings->Horizontal = value != 0;
    }
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &smoothName, &value))) {
        Settings->Smooth = value != 0;
    }
    if (NT_SUCCESS(WdfRegistryQueryULong(key, &debugName, &value))) {
        Settings->Debug = value != 0;
    }
    WdfRegistryClose(key);
}

static NTSTATUS
OmmEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    PDEVICE_OBJECT pdo = WdfFdoInitWdmGetPhysicalDevice(DeviceInit);
    OMM_ROLE role = OmmDetectRole(pdo);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    POMM_DEVICE_CONTEXT ctx;
    WDFDEVICE device;
    NTSTATUS status;

    WdfFdoInitSetFilter(DeviceInit);

    if (role == OmmRoleMouse) {
        WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);
    } else {
        WDF_PNPPOWER_EVENT_CALLBACKS pnpPower;
        WDF_OBJECT_ATTRIBUTES requestAttributes;

        WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPower);
        pnpPower.EvtDeviceSelfManagedIoInit = OmmBtSelfManagedIoInit;
        WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPower);

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&requestAttributes, OMM_REQUEST_CONTEXT);
        WdfDeviceInitSetRequestAttributes(DeviceInit, &requestAttributes);
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OMM_DEVICE_CONTEXT);
    attributes.EvtCleanupCallback = OmmEvtDeviceCleanup;
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ctx = OmmGetContext(device);
    ctx->Role = role;
    ctx->Link = OmmLinkAcquire(pdo);
    if (ctx->Link == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (role == OmmRoleBluetooth) {
        OmmLoadSettings(Driver, &ctx->Settings);
        status = OmmBtInitialize(device);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }

    // Request types without a callback are forwarded by the framework.
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = OmmEvtIoInternalDeviceControl;
    return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

static VOID
OmmEvtDeviceCleanup(_In_ WDFOBJECT Object)
{
    POMM_DEVICE_CONTEXT ctx = OmmGetContext((WDFDEVICE)Object);

    if (ctx->Role == OmmRoleMouse) {
        OmmMouseCleanup(ctx);
    } else {
        OmmBtCleanup(ctx);
    }
    if (ctx->Link != NULL) {
        OmmLinkRelease(ctx->Link);
        ctx->Link = NULL;
    }
}

static VOID
OmmEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    NTSTATUS status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (OmmGetContext(device)->Role == OmmRoleBluetooth) {
        OmmBtHandleInternalIoctl(device, Request, IoControlCode);
        return;
    }

    status = OmmMouseHandleInternalIoctl(device, Request, IoControlCode);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }
    OmmForwardRequest(Request, WdfDeviceGetIoTarget(device));
}

VOID
OmmForwardRequest(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
{
    WDF_REQUEST_SEND_OPTIONS options;

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if (!WdfRequestSend(Request, Target, &options)) {
        WdfRequestComplete(Request, WdfRequestGetStatus(Request));
    }
}
