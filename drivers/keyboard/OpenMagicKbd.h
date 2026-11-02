#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntddkbd.h>
#include <kbdmou.h>
#include <hidclass.h>
#include <hidusage.h>
#include <hidpi.h>

#define OM_POOL_TAG     'bKmO'
#define OM_MAX_MAPPINGS 64
#define OM_MAX_CHORD    3
#define OM_KEY_PAUSE    0xE11D

// Pause expands to two records, so one chord can emit up to twice its length.
#define OM_MAX_EMIT     (2 * OM_MAX_CHORD)

// The same service is installed as both LowerFilters and UpperFilters of the
// HID keyboard collection: the lower instance sees raw HID reports (where the
// Fn bit lives), the upper instance sees scancodes on their way to kbdclass.
typedef enum _OM_ROLE {
    OmRoleLower,
    OmRoleUpper
} OM_ROLE;

// Optional diagnostic trace (Parameters\Debug = 1): the last reports seen by
// the lower instance and the scancodes in/out of the upper instance, written
// to Parameters\DebugLog as REG_BINARY.
#define OM_DEBUG_ENTRIES    64
#define OM_DEBUG_REPORT     1
#define OM_DEBUG_IN         2
#define OM_DEBUG_OUT        3

typedef struct _OM_DEBUG_ENTRY {
    UCHAR Kind;
    UCHAR FnDown;
    USHORT MakeCode;
    USHORT Flags;
    UCHAR Data[10];     // first report bytes (reports only)
} OM_DEBUG_ENTRY;

// State shared by the lower and upper instances attached to the same PDO.
typedef struct _OM_SHARED {
    LIST_ENTRY Link;
    PDEVICE_OBJECT Pdo;
    ULONG RefCount;
    volatile LONG FnDown;

    BOOLEAN Debug;
    KSPIN_LOCK DebugLock;
    ULONG DebugCount;
    OM_DEBUG_ENTRY DebugLog[OM_DEBUG_ENTRIES];
} OM_SHARED, *POM_SHARED;

// Keys are set-1 scancodes; E0-prefixed keys are encoded as 0xE0xx.
typedef struct _OM_TARGET {
    UCHAR Count;                    // 0 suppresses the key
    USHORT Keys[OM_MAX_CHORD];      // pressed in order, released in reverse
} OM_TARGET;

typedef struct _OM_MAPPING {
    USHORT Source;
    OM_TARGET Target;
} OM_MAPPING;

typedef struct _OM_KEYMAP {
    ULONG Count;
    OM_MAPPING Entries[OM_MAX_MAPPINGS];
} OM_KEYMAP;

typedef struct _OM_DEVICE_CONTEXT {
    OM_ROLE Role;
    POM_SHARED Shared;

    // Lower role
    PHIDP_PREPARSED_DATA PreparsedData;
    USHORT InputReportLength;

    // Upper role
    WDFWORKITEM DebugWorkItem;
    volatile LONG DebugQueued;
    CONNECT_DATA ClassConnect;
    OM_KEYMAP Map;          // always applied
    OM_KEYMAP FnMap;        // applied while Fn is held, takes precedence over Map
    USHORT Active[0x200];   // per source key: mapping chosen at make time (0 = none)
} OM_DEVICE_CONTEXT, *POM_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OM_DEVICE_CONTEXT, OmGetContext)

// driver.c
POM_SHARED OmSharedAcquire(_In_ PDEVICE_OBJECT Pdo);
VOID OmSharedRelease(_In_ POM_SHARED Shared);
VOID OmForwardRequest(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

// debug.c
VOID OmDebugAdd(_In_ POM_SHARED Shared, _In_ UCHAR Kind, _In_ USHORT MakeCode, _In_ USHORT Flags,
                _In_reads_opt_(Length) const UCHAR *Data, _In_ ULONG Length);
NTSTATUS OmDebugInitialize(_In_ WDFDEVICE Device);
VOID OmDebugFlush(_In_ POM_DEVICE_CONTEXT Ctx);

// lower.c
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT OmLowerSelfManagedIoInit;
VOID OmLowerHandleRead(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request);
VOID OmLowerCleanup(_In_ POM_DEVICE_CONTEXT Ctx);

// upper.c
NTSTATUS OmUpperHandleInternalIoctl(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request, _In_ ULONG IoControlCode);

// config.c
VOID OmLoadKeymaps(_In_ WDFDRIVER Driver, _Inout_ POM_DEVICE_CONTEXT Ctx);
