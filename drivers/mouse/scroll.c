#include "OpenMagicMouse.h"

#define OMM_TOUCH_STATE_NONE    0x00

#define OMM_DEADZONE            24      // device units the finger must travel before scrolling
#define OMM_MOTION_BLOCK        6       // pointer counts per report that mark the mouse as moving
#define OMM_MAX_WHEEL           1200    // clamp per report (10 notches)

typedef struct _OMM_TOUCH {
    UCHAR Id;
    UCHAR State;
    LONG X;
    LONG Y;
} OMM_TOUCH;

static LONG
OmmAbs(_In_ LONG Value)
{
    return Value < 0 ? -Value : Value;
}

// Each finger record packs two 12-bit signed coordinates into bytes 0-2, the
// touch id across bytes 5-6 and the touch state in the high nibble of byte 7.
static VOID
OmmDecodeTouch(_In_reads_(OMM_TOUCH_LENGTH) const UCHAR *Record, _Out_ OMM_TOUCH *Touch)
{
    LONG x = ((Record[1] & 0x0F) << 8) | Record[0];
    LONG y = (Record[2] << 4) | (Record[1] >> 4);

    Touch->X = (x & 0x800) ? x - 0x1000 : x;
    Touch->Y = (y & 0x800) ? y - 0x1000 : y;    // grows towards the front of the mouse
    Touch->Id = (UCHAR)(((Record[6] << 2) | (Record[5] >> 6)) & 0x0F);
    Touch->State = Record[7] & 0xF0;
}

// Converts finger travel into wheel units. The gain grows with speed so slow
// drags are precise and flicks cover long pages. Returns whole units (or whole
// 120-unit notches when smooth scrolling is off) and keeps the remainder.
static LONG
OmmWheelStep(_Inout_ PLONG Accumulator, _In_ LONG Delta, _In_ const OMM_SETTINGS *Settings)
{
    LONG gain = 400 + 20 * min(OmmAbs(Delta), 100);       // 0.4 .. 2.4 units per device unit, x1000
    LONG quantum = Settings->Smooth ? 1000 : 120 * 1000;
    LONG step;

    *Accumulator += Delta * gain / 100 * Settings->ScrollSpeed;
    step = *Accumulator / quantum * (quantum / 1000);
    *Accumulator -= step * 1000;

    step = Settings->Natural ? -step : step;
    return max(-OMM_MAX_WHEEL, min(OMM_MAX_WHEEL, step));
}

static VOID
OmmUpdateGesture(
    _Inout_ OMM_GESTURE *Gesture,
    _In_ const OMM_SETTINGS *Settings,
    _In_ ULONG FingersDown,
    _In_ const OMM_TOUCH *Touch,
    _In_ BOOLEAN Buttons,
    _In_ BOOLEAN Moving,
    _Out_ PLONG WheelX,
    _Out_ PLONG WheelY)
{
    LONG dx, dy, travelX, travelY;

    *WheelX = 0;
    *WheelY = 0;

    if (FingersDown != 1) {
        RtlZeroMemory(Gesture, sizeof(*Gesture));
        return;
    }
    if (!Gesture->Tracking || Gesture->Id != Touch->Id) {
        RtlZeroMemory(Gesture, sizeof(*Gesture));
        Gesture->Tracking = TRUE;
        Gesture->Id = Touch->Id;
        Gesture->StartX = Gesture->LastX = Touch->X;
        Gesture->StartY = Gesture->LastY = Touch->Y;
        Gesture->Blocked = Buttons;
        return;
    }

    dx = Touch->X - Gesture->LastX;
    dy = Touch->Y - Gesture->LastY;
    Gesture->LastX = Touch->X;
    Gesture->LastY = Touch->Y;

    if (Buttons || (Moving && !Gesture->Scrolling)) {
        Gesture->Blocked = TRUE;
    }
    if (Gesture->Blocked) {
        return;
    }

    if (!Gesture->Scrolling) {
        travelX = Touch->X - Gesture->StartX;
        travelY = Touch->Y - Gesture->StartY;
        if (OmmAbs(travelX) < OMM_DEADZONE && OmmAbs(travelY) < OMM_DEADZONE) {
            return;
        }
        Gesture->Scrolling = TRUE;
        Gesture->Horizontal = Settings->Horizontal && OmmAbs(travelX) > 2 * OmmAbs(travelY);
    }

    if (Gesture->Horizontal) {
        *WheelX = OmmWheelStep(&Gesture->AccumX, dx, Settings);
    } else {
        *WheelY = OmmWheelStep(&Gesture->AccumY, dy, Settings);
    }
}

VOID
OmmEndGesture(_In_ POMM_DEVICE_CONTEXT Ctx)
{
    KIRQL irql;

    KeAcquireSpinLock(&Ctx->GestureLock, &irql);
    RtlZeroMemory(&Ctx->Gesture, sizeof(Ctx->Gesture));
    KeReleaseSpinLock(&Ctx->GestureLock, irql);
}

// Called for every multitouch report 0x12 (Report[0] is the report ID).
VOID
OmmProcessReport(_In_ POMM_DEVICE_CONTEXT Ctx, _In_reads_(Length) const UCHAR *Report, _In_ ULONG Length)
{
    ULONG count = (Length - OMM_REPORT_TOUCH_OFFSET) / OMM_TOUCH_LENGTH;
    BOOLEAN buttons = (Report[1] & 0x03) != 0;
    SHORT motionX = (SHORT)(Report[2] | (Report[3] << 8));
    SHORT motionY = (SHORT)(Report[4] | (Report[5] << 8));
    BOOLEAN moving = OmmAbs(motionX) + OmmAbs(motionY) > OMM_MOTION_BLOCK;
    OMM_TOUCH touch, active = { 0 };
    ULONG i, fingersDown = 0;
    LONG wheelX, wheelY;
    BOOLEAN wasScrolling, startedScrolling, horizontal;
    KIRQL irql;

    // Any non-zero state is a finger on the surface (as Linux treats it); the
    // Magic Mouse 2 also reports states other than start/drag while touching.
    for (i = 0; i < count; i++) {
        OmmDecodeTouch(Report + OMM_REPORT_TOUCH_OFFSET + i * OMM_TOUCH_LENGTH, &touch);
        if (touch.State != OMM_TOUCH_STATE_NONE) {
            fingersDown++;
            active = touch;
        }
    }

    KeAcquireSpinLock(&Ctx->GestureLock, &irql);
    wasScrolling = Ctx->Gesture.Scrolling;
    OmmUpdateGesture(&Ctx->Gesture, &Ctx->Settings, fingersDown, &active, buttons, moving, &wheelX, &wheelY);
    startedScrolling = !wasScrolling && Ctx->Gesture.Scrolling;
    horizontal = Ctx->Gesture.Horizontal;
    KeReleaseSpinLock(&Ctx->GestureLock, irql);

    if (startedScrolling) {
        OmmDebugEvent(Ctx, OMM_EVENT_SCROLL, horizontal, NULL, 0);
    }

    // Device Y grows towards the front and X to the right, matching the
    // positive directions of WHEEL (up) and HWHEEL (right).
    if (wheelY != 0) {
        OmmInjectWheel(Ctx->Link, MOUSE_WHEEL, wheelY);
        OmmDebugWheel(Ctx, wheelY);
    }
    if (wheelX != 0) {
        OmmInjectWheel(Ctx->Link, MOUSE_HWHEEL, wheelX);
        OmmDebugWheel(Ctx, wheelX);
    }
}
