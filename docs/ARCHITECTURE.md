# Architecture

OpenMagic consists of two KMDF filter drivers. Neither replaces a Windows driver: each attaches above
and below a standard driver of the device and only adds the behavior Windows lacks.

## Keyboard: `OpenMagicKbd.sys`

### The problem

The Magic Keyboard reports Fn as a vendor-defined bit inside the keyboard's own input report
(Report ID 1). Over Bluetooth the descriptor declares it as button usage `0x03` on pages `0xFF00`
and `0xFF01`; other models and transports use page `0x00FF`. Windows' HID keyboard driver, `kbdhid`,
has no scancode for those usages and discards them, and user-mode programs cannot read a keyboard
collection, so Fn never reaches Windows.

### The stack

The same service is installed as both `LowerFilters` and `UpperFilters` of the HID keyboard
collection (`HID\...&Col01`). `AddDevice` runs twice; the instance added after `kbdhid` has attached
takes the upper role.

```
kbdclass
OpenMagicKbd (upper)   remaps scancodes using the Fn state
kbdhid
OpenMagicKbd (lower)   reads and strips the Fn bit from each HID report
HidBth / HidUsb
```

- **Lower instance.** It fetches the collection's preparsed data (`IOCTL_HID_GET_COLLECTION_DESCRIPTOR`)
  and, in the completion of each `IRP_MJ_READ` that `kbdhid` sends down, decodes the report with
  `HidP_GetUsages` / `HidP_GetUsageValue`. It records the Fn state and then clears the Fn usages from the
  report. Without that last step, `kbdhid` dropped key releases that arrived in the same report as an
  Fn change, leaving keys stuck down.
- **Upper instance.** It hooks `IOCTL_INTERNAL_KEYBOARD_CONNECT`, as the WDK `kbfiltr` sample does, so
  `kbdhid` delivers `KEYBOARD_INPUT_DATA` to it before `kbdclass`. Each make code is looked up in
  `FnMap` (while Fn is held) and then in `Map`. The mapping chosen when a key goes down is remembered
  per key, and the same mapping produces its repeats and its release. Releasing Fn before the key
  therefore never leaves a translated key or a combination held.

### Key maps

`HKLM\SYSTEM\CurrentControlSet\Services\OpenMagicKbd\Parameters`, `REG_MULTI_SZ` values `Map` and
`FnMap`, read when the keyboard starts. Each entry is `SRC=TGT[+TGT...]` in hexadecimal set-1
scancodes:

| Form | Meaning |
| --- | --- |
| `42=E022` | F8 becomes Play/Pause (E0-prefixed keys are written `E0xx`) |
| `3D=E05B+0F` | F3 becomes Win+Tab (up to three keys, pressed in order, released in reverse) |
| `66=E11D` | F15 becomes Pause (`E11D` = the E1 1D 45 sequence) |
| `68=` | F17 is disabled |

`tools\OpenMagic-Keymap.ps1` edits these values with key names.

## Mouse: `OpenMagicMouse.sys`

### The problem

The Magic Mouse 2 descriptor declares only an 8-byte input report `0x12` with buttons and X/Y motion.
The mouse sends finger positions only after a feature report `0xF1 = { 0x02, 0x01 }` (documented by
the Linux `hid-magicmouse` driver), which is not declared, so Windows' HID stack refuses to send it.
Once multitouch is on, report `0x12` grows to `14 + 8 × fingers` bytes, longer than declared.

### The stack

```
mouclass
OpenMagicMouse (mouse)       records mouclass' service callback, injects wheel events
mouhid
HidBth
OpenMagicMouse (bluetooth)   turns multitouch on, decodes fingers, trims report 0x12
BthEnum
```

The two roles live on different device nodes: the Bluetooth HID device (`BTHENUM\...PID&0269`, class
HIDClass) and its mouse collection (`HID\...PID&0269&Col01`, class Mouse). They therefore come from two
INF files that install the same service, so one driver image serves both, and the roles find each
other through the device's container ID.

### Bluetooth role

The filter sits between `HidBth` and the Bluetooth stack and sees every
`IOCTL_INTERNAL_BTH_SUBMIT_BRB`. Only `BRB_L2CA_ACL_TRANSFER` (L2CAP data) is intercepted.

- **Channels.** HID over Bluetooth uses a control channel and an interrupt channel. They are identified
  from their traffic rather than from how `HidBth` opens them. HANDSHAKE, HID_CONTROL, GET/SET_REPORT,
  GET/SET_PROTOCOL and GET/SET_IDLE transactions only appear on the control channel, and report `0x12`
  only arrives on the interrupt channel. A new interrupt channel means the mouse reconnected.
- **Whole packets.** `HidBth` reads the interrupt channel with a 9-byte buffer, the declared report
  size, and the Bluetooth stack would split a multitouch packet across several reads. The filter gives
  those reads its own 512-byte buffer, processes the complete packet and copies the plain 8-byte report
  (header + buttons + motion) back into `HidBth`'s buffer. Pointer motion and clicks keep using the inbox
  driver.
- **Multitouch.** Once reads use whole-packet buffers, the filter sends
  `SET_REPORT(Feature) 0xF1 02 01` (`53 F1 02 01`) on the control channel. It does this again after every
  reconnection, at most five attempts per connection. The device's HANDSHAKE reply goes to `HidBth`.

### Report `0x12` in multitouch mode

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 1 | Report ID `0x12` |
| 1 | 1 | Buttons (bit 0 left, bit 1 right) |
| 2 | 2 | X motion, signed |
| 4 | 2 | Y motion, signed |
| 6 | 8 | Header (timestamp and status) |
| 14 | 8 × n | One record per finger |

Finger record:

| Bits | Content |
| --- | --- |
| byte 0, byte 1 bits 0–3 | X position, 12-bit signed, grows to the right |
| byte 1 bits 4–7, byte 2 | Y position, 12-bit signed, grows towards the front |
| byte 3 / byte 4 | Touch major / minor axis |
| byte 5 bits 0–5 | Size |
| byte 5 bits 6–7, byte 6 bits 0–1 | Touch ID |
| byte 7 bits 4–7 | State (0 = none; non-zero = touching, `0x40` = dragging) |

With no finger on the surface the mouse sends the plain 8-byte report, which ends the gesture.

### Scrolling

Scrolling follows one finger. More than one finger, a click, or pointer motion before scrolling starts
cancel the gesture until the finger lifts. After a dead zone of 24 device units the gesture locks to the
dominant axis. Movement is converted into wheel units with a gain that grows with speed (0.4 to 2.4 wheel
units per device unit, scaled by `ScrollSpeed`). The remainder carries over between reports. Wheel
events (`MOUSE_WHEEL` / `MOUSE_HWHEEL`) are delivered by calling `mouclass`' service callback, the same
way `mouhid` delivers motion.

## Diagnostics

Both drivers keep an optional trace, enabled with the `Debug` (`REG_DWORD` = 1) value in their
`Parameters` key (`scripts\install-drivers.ps1 -Debug`) and written to `Parameters\DebugLog`
(`REG_BINARY`):

- **Keyboard:** the last 64 entries, 16 bytes each. Kind 1 = HID report (first 10 bytes), 2 = scancode
  from `kbdhid`, 3 = scancode to `kbdclass`. Each entry holds the Fn state, make code and flags.
- **Mouse:** counters (plain and multitouch reports, wheel events, multitouch attempts and last status,
  the last multitouch report) followed by the last 64 events: control/interrupt channel learned, report
  form changes, other inbound data, multitouch requests and scroll gestures.

The layouts are defined in `OpenMagicKbd.h` and `OpenMagicMouse.h`.
