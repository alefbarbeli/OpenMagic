#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntddmou.h>
#include <kbdmou.h>
#include <bthdef.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <sdpnode.h>
#include <bthddi.h>

#define OMM_POOL_TAG        'uMmO'
#define OMM_CONTAINER_CHARS 64

// Magic Mouse 2 input report 0x12. The descriptor declares 8 bytes (ID,
// buttons, X, Y); with multitouch on the device appends a 6-byte header and
// one 8-byte record per finger.
#define OMM_REPORT_MOUSE        0x12
#define OMM_REPORT_BASE_LENGTH  8
#define OMM_REPORT_TOUCH_OFFSET 14
#define OMM_TOUCH_LENGTH        8

// HidBth reads the interrupt channel with a buffer sized for the declared
// 8-byte report, so a multitouch packet would arrive split across several
// reads. Those reads get this buffer instead; the packet arrives whole.
#define OMM_PACKET_SIZE         512

typedef struct _OMM_REQUEST_CONTEXT {
    BOOLEAN Swapped;
    PVOID OriginalBuffer;
    PMDL OriginalMdl;
    ULONG OriginalSize;
    UCHAR Packet[OMM_PACKET_SIZE];
} OMM_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OMM_REQUEST_CONTEXT, OmmGetRequestContext)

// One service, two places in the device tree of the same mouse:
//   Bluetooth role: lower filter of the BTHENUM HID device (below HidBth). Sees
//     raw L2CAP traffic, turns multitouch on, decodes touches into scroll.
//   Mouse role: upper filter of the HID mouse collection (above mouhid). Only
//     learns mouclass' service callback so scroll events can be injected.
typedef enum _OMM_ROLE {
    OmmRoleBluetooth,
    OmmRoleMouse
} OMM_ROLE;

// Joins the two roles of one physical mouse (matched by container ID).
typedef struct _OMM_LINK {
    LIST_ENTRY Link;
    ULONG RefCount;
    KSPIN_LOCK Lock;
    CONNECT_DATA ClassConnect;              // guarded by Lock
    WCHAR ContainerId[OMM_CONTAINER_CHARS];
} OMM_LINK, *POMM_LINK;

typedef struct _OMM_SETTINGS {
    LONG ScrollSpeed;       // percent
    BOOLEAN Natural;        // content follows the finger (macOS default)
    BOOLEAN Horizontal;     // allow horizontal scrolling
    BOOLEAN Smooth;         // high-resolution wheel deltas instead of 120-unit notches
    BOOLEAN Debug;          // write Parameters\DebugLog
} OMM_SETTINGS;

// Optional diagnostic trace (Parameters\Debug = 1), written to
// Parameters\DebugLog as REG_BINARY: OMM_DEBUG_STATS followed by the last
// OMM_DEBUG_EVENTS events, oldest first.
#define OMM_DEBUG_EVENTS        64
#define OMM_EVENT_CONTROL       1   // HID control channel learned; Data = message that revealed it
#define OMM_EVENT_INTERRUPT     2   // HID interrupt channel learned (new connection)
#define OMM_EVENT_REPORT        3   // report 0x12 changed form; Length, Data = first bytes
#define OMM_EVENT_OTHER         4   // other inbound data; Length, Data = first bytes
#define OMM_EVENT_ENABLE        5   // Value = NTSTATUS of the SET_REPORT
#define OMM_EVENT_ENABLE_SKIP   6   // Value = control channel known, Data[0] = interrupt channel known
#define OMM_EVENT_SCROLL        7   // gesture started scrolling; Value = 1 horizontal, 0 vertical

typedef struct _OMM_DEBUG_EVENT {
    UCHAR Kind;
    UCHAR Reserved;
    USHORT Length;
    LONG Value;
    UCHAR Data[8];
} OMM_DEBUG_EVENT;

typedef struct _OMM_DEBUG_STATS {
    ULONG ShortReports;     // report 0x12 without touch data
    ULONG TouchReports;     // report 0x12 with touch data
    ULONG OtherInbound;
    ULONG WheelEvents;
    ULONG EnableAttempts;
    LONG LastEnableStatus;
    LONG LastWheel;
    ULONG LastLength;
    UCHAR LastReport[32];   // last multitouch report 0x12 (from the report ID)
} OMM_DEBUG_STATS;

// Single-finger scroll gesture.
typedef struct _OMM_GESTURE {
    BOOLEAN Tracking;
    BOOLEAN Scrolling;
    BOOLEAN Horizontal;     // axis locked when scrolling starts
    BOOLEAN Blocked;        // click or mouse motion: ignore until the finger lifts
    UCHAR Id;
    LONG StartX, StartY;
    LONG LastX, LastY;
    LONG AccumX, AccumY;    // pending scroll, in 1/1000 wheel units
} OMM_GESTURE;

typedef struct _OMM_DEVICE_CONTEXT {
    OMM_ROLE Role;
    POMM_LINK Link;

    // Bluetooth role
    BTH_PROFILE_DRIVER_INTERFACE Bth;
    BOOLEAN HaveBth;
    KSPIN_LOCK ChannelLock;
    L2CAP_CHANNEL_HANDLE Control;       // HID control channel (learned from its traffic)
    L2CAP_CHANNEL_HANDLE Interrupt;     // HID interrupt channel (where report 0x12 arrives)
    BTH_ADDR Address;
    volatile LONG MultitouchOn;
    volatile LONG EnableQueued;
    ULONG EnableAttempts;
    ULONGLONG LastEnableTime;
    WDFWORKITEM EnableWorkItem;
    OMM_SETTINGS Settings;
    KSPIN_LOCK GestureLock;
    OMM_GESTURE Gesture;

    KSPIN_LOCK DebugLock;
    OMM_DEBUG_STATS Stats;
    OMM_DEBUG_EVENT Events[OMM_DEBUG_EVENTS];
    ULONG EventCount;
    BOOLEAN LastWasTouch;
    WDFWORKITEM DebugWorkItem;
    volatile LONG DebugQueued;
    ULONGLONG LastDebugWrite;
} OMM_DEVICE_CONTEXT, *POMM_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OMM_DEVICE_CONTEXT, OmmGetContext)

// driver.c
VOID OmmForwardRequest(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

// bluetooth.c
NTSTATUS OmmBtInitialize(_In_ WDFDEVICE Device);
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT OmmBtSelfManagedIoInit;
VOID OmmBtHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode);
VOID OmmBtCleanup(_In_ POMM_DEVICE_CONTEXT Ctx);

// scroll.c
VOID OmmProcessReport(_In_ POMM_DEVICE_CONTEXT Ctx, _In_reads_(Length) const UCHAR *Report, _In_ ULONG Length);
VOID OmmEndGesture(_In_ POMM_DEVICE_CONTEXT Ctx);

// debug.c
NTSTATUS OmmDebugInitialize(_In_ WDFDEVICE Device);
VOID OmmDebugEvent(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ UCHAR Kind, _In_ LONG Value,
                   _In_reads_opt_(Length) const UCHAR *Data, _In_ ULONG Length);
VOID OmmDebugReport(_In_ POMM_DEVICE_CONTEXT Ctx, _In_reads_(Length) const UCHAR *Report, _In_ ULONG Length);
VOID OmmDebugWheel(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ LONG Amount);
VOID OmmDebugEnable(_In_ POMM_DEVICE_CONTEXT Ctx, _In_ NTSTATUS Status);

// mouse.c
NTSTATUS OmmMouseHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode);
VOID OmmMouseCleanup(_In_ POMM_DEVICE_CONTEXT Ctx);
VOID OmmInjectWheel(_In_opt_ POMM_LINK Link, _In_ USHORT ButtonFlag, _In_ LONG Amount);
