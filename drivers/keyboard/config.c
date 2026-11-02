#include "OpenMagicKbd.h"

// Keymaps live in HKLM\SYSTEM\CurrentControlSet\Services\OpenMagicKbd\Parameters
// as REG_MULTI_SZ values "Map" and "FnMap". Each string is "SRC=TGT[+TGT...]":
// hex set-1 scancodes, E0-prefixed keys written as E0xx, Pause as E11D, and an
// empty target to disable a key. Example: "42=E022" (F8 -> Play/Pause),
// "3D=E05B+0F" (F3 -> Win+Tab). They are read when the device starts.

static BOOLEAN
OmParseKey(_Inout_ PCWCH *Cursor, _In_ PCWCH End, _Out_ PUSHORT Key)
{
    ULONG value = 0, digits = 0;

    *Key = 0;
    while (*Cursor < End) {
        WCHAR c = **Cursor;
        ULONG digit;

        if (c >= L'0' && c <= L'9') {
            digit = c - L'0';
        } else if (c >= L'a' && c <= L'f') {
            digit = c - L'a' + 10;
        } else if (c >= L'A' && c <= L'F') {
            digit = c - L'A' + 10;
        } else {
            break;
        }
        if (++digits > 4) {
            return FALSE;
        }
        value = (value << 4) | digit;
        (*Cursor)++;
    }

    if (digits == 0 || (value > 0xFF && (value & 0xFF00) != 0xE000 && value != OM_KEY_PAUSE)) {
        return FALSE;
    }
    *Key = (USHORT)value;
    return TRUE;
}

static BOOLEAN
OmParseMapping(_In_ PCUNICODE_STRING Text, _Out_ OM_MAPPING *Mapping)
{
    PCWCH cursor = Text->Buffer;
    PCWCH end = Text->Buffer + Text->Length / sizeof(WCHAR);

    RtlZeroMemory(Mapping, sizeof(*Mapping));
    if (!OmParseKey(&cursor, end, &Mapping->Source) || Mapping->Source == OM_KEY_PAUSE) {
        return FALSE;
    }
    if (cursor >= end || *cursor != L'=') {
        return FALSE;
    }
    cursor++;

    while (cursor < end) {
        if (Mapping->Target.Count == OM_MAX_CHORD ||
            !OmParseKey(&cursor, end, &Mapping->Target.Keys[Mapping->Target.Count])) {
            return FALSE;
        }
        Mapping->Target.Count++;
        if (cursor < end) {
            if (*cursor != L'+' || ++cursor >= end) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static VOID
OmLoadKeymap(_In_ WDFKEY Key, _In_ PCWSTR ValueName, _Out_ OM_KEYMAP *Map)
{
    WDF_OBJECT_ATTRIBUTES stringAttributes;
    UNICODE_STRING valueName;
    WDFCOLLECTION strings;
    ULONG i, count;

    Map->Count = 0;
    RtlInitUnicodeString(&valueName, ValueName);
    if (!NT_SUCCESS(WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &strings))) {
        return;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&stringAttributes);
    stringAttributes.ParentObject = strings;
    if (NT_SUCCESS(WdfRegistryQueryMultiString(Key, &valueName, &stringAttributes, strings))) {
        count = WdfCollectionGetCount(strings);
        for (i = 0; i < count && Map->Count < OM_MAX_MAPPINGS; i++) {
            UNICODE_STRING text;
            OM_MAPPING mapping;

            WdfStringGetUnicodeString((WDFSTRING)WdfCollectionGetItem(strings, i), &text);
            if (OmParseMapping(&text, &mapping)) {
                Map->Entries[Map->Count++] = mapping;
            }
        }
    }
    WdfObjectDelete(strings);
}

VOID
OmLoadKeymaps(_In_ WDFDRIVER Driver, _Inout_ POM_DEVICE_CONTEXT Ctx)
{
    DECLARE_CONST_UNICODE_STRING(debugName, L"Debug");
    ULONG debug = 0;
    WDFKEY key;

    Ctx->Map.Count = 0;
    Ctx->FnMap.Count = 0;
    if (!NT_SUCCESS(WdfDriverOpenParametersRegistryKey(Driver, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &key))) {
        return;
    }
    OmLoadKeymap(key, L"Map", &Ctx->Map);
    OmLoadKeymap(key, L"FnMap", &Ctx->FnMap);
    (VOID)WdfRegistryQueryULong(key, &debugName, &debug);
    Ctx->Shared->Debug = debug != 0;
    WdfRegistryClose(key);
}
