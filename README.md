<p align="center">
  <img src="assets/openmagic.png" width="112" alt="OpenMagic">
</p>

<h1 align="center">OpenMagic</h1>

<p align="center">
  Open-source Windows drivers for the Apple Magic Keyboard and Magic Mouse 2.
  <br>
  Fn media keys and configurable keys for the keyboard, smooth scrolling for the mouse.
</p>

<p align="center">
  <a href="https://github.com/alefbarbeli/OpenMagic/releases/latest"><img src="https://img.shields.io/github/v/release/alefbarbeli/OpenMagic?label=release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-0078D4" alt="Windows 10 and 11, x64">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT license"></a>
</p>

<p align="center"><a href="README.pt-BR.md">Leia em português</a></p>

---

## Features

**Magic Keyboard**

- **Fn + F7…F12** control media playback and volume: previous, play/pause, next, mute, volume down, volume up.
- **Fn shortcuts**: Fn+F3 opens Task View, Fn+F4 opens Search, Fn+Backspace is Delete, Fn+arrows are Page Up, Page Down, Home and End.
- **F13–F15** work as Print Screen, Scroll Lock and Pause. F16–F19 are free for your own shortcuts.
- **Every key is remappable**, with or without Fn, including key combinations.
- Works over **Bluetooth and USB**.

**Magic Mouse 2**

- **One-finger scrolling**, vertical and horizontal, with high-resolution (smooth) wheel events.
- **Natural or traditional** direction and adjustable speed.
- Pointer movement and clicks keep using the standard Windows mouse driver.

## Supported devices

| Device | Model | Connection | Status |
| --- | --- | --- | --- |
| Magic Keyboard with Numeric Keypad | A1843 (`05AC:026C`) | Bluetooth, USB | Tested |
| Magic Mouse 2 | A1657 (`004C:0269`) | Bluetooth | Tested |

Other Magic Keyboard and Magic Mouse models are not supported yet. Reports and contributions are welcome.

## Installation

1. Download `OpenMagic-Setup-<version>.exe` from the [latest release](https://github.com/alefbarbeli/OpenMagic/releases/latest).
2. Run it and choose the drivers to install.
3. Restart when Setup asks.

> [!IMPORTANT]
> OpenMagic's drivers are signed with the project's own test certificate, not by Microsoft, because
> production kernel-driver signing requires an EV certificate and Microsoft attestation. Windows only
> loads test-signed drivers in **test-signing mode**, so Setup:
>
> - turns on test-signing mode (a "Test Mode" watermark appears on the desktop),
> - adds the OpenMagic test certificate to the PC's trusted certificates,
> - installs the drivers right away, or after the restart if test-signing mode was off.
>
> Test-signing mode requires **Secure Boot to be off**, and some games with anti-cheat software
> refuse to run while it is on. If BitLocker is on, it is suspended for one restart so the boot
> configuration change doesn't trigger recovery. Uninstalling OpenMagic offers to turn test-signing off again.

Silent installation: `OpenMagic-Setup-<version>.exe /VERYSILENT /ENABLETESTMODE=1`

If another Magic Keyboard or Magic Mouse driver is installed (Magic Utilities, Apple Boot Camp drivers),
uninstall it first.

## Configuration

The settings tools are installed in `C:\Program Files\OpenMagic\tools` and change settings from an
elevated PowerShell. The device restarts to apply the change.

### Keyboard key maps

There are two key maps: **Map** is always active, **FnMap** applies while Fn is held and takes precedence.

| Key | Default with Fn | Default without Fn |
| --- | --- | --- |
| F3 | Win+Tab (Task View) | F3 |
| F4 | Win+S (Search) | F4 |
| F7 / F8 / F9 | Previous / Play-Pause / Next | F7 / F8 / F9 |
| F10 / F11 / F12 | Mute / Volume down / Volume up | F10 / F11 / F12 |
| Backspace | Delete | Backspace |
| ↑ ↓ ← → | Page Up / Page Down / Home / End | arrows |
| F13 / F14 / F15 | F13 / F14 / F15 | Print Screen / Scroll Lock / Pause |

```powershell
cd 'C:\Program Files\OpenMagic\tools'
.\OpenMagic-Keymap.ps1                        # show both key maps
.\OpenMagic-Keymap.ps1 set F16 MediaStop      # F16 -> Stop
.\OpenMagic-Keymap.ps1 set F1 F21 -Fn         # Fn+F1 -> F21, for your own shortcut
.\OpenMagic-Keymap.ps1 set F17 Win+LShift+S   # up to 3 keys (screenshot)
.\OpenMagic-Keymap.ps1 set F18 None           # disable a key
.\OpenMagic-Keymap.ps1 remove F16             # back to the default key
.\OpenMagic-Keymap.ps1 keys                   # list key names
```

`Watch-Keys.ps1` shows the keys Windows receives, which helps when building a key map.

### Mouse

| Setting | Values | Default |
| --- | --- | --- |
| `ScrollSpeed` | 10–400 (%) | 100 |
| `NaturalScrolling` | 1 = content follows the finger, 0 = traditional | 1 |
| `HorizontalScrolling` | 1 = sideways swipes scroll horizontally | 1 |
| `SmoothScrolling` | 1 = high-resolution steps, 0 = classic wheel notches | 1 |

```powershell
.\OpenMagic-Mouse.ps1                              # show the settings
.\OpenMagic-Mouse.ps1 -ScrollSpeed 150
.\OpenMagic-Mouse.ps1 -NaturalScrolling 0 -SmoothScrolling 0
```

## Uninstallation

Open **Settings > Apps > Installed apps**, select **OpenMagic** and choose **Uninstall**. The keyboard
and mouse go back to the standard Windows drivers, the certificate is removed, and Setup asks whether to
turn test-signing mode off. Afterwards, turn the Magic Mouse off and on again: it stays in multitouch
mode, which the standard driver can't read, until it restarts.

## How it works

Each driver is a small KMDF filter installed around the standard Windows drivers:

- **Keyboard**: one instance below `kbdhid` reads the Fn bit, which Windows otherwise discards, from
  every HID report; another instance above `kbdhid` remaps scancodes on their way to `kbdclass`.
- **Mouse**: one instance below the Bluetooth HID driver turns on the mouse's multitouch reporting and
  turns finger movement into scrolling; another instance above `mouhid` delivers the scroll events.

The details, including the HID reports involved, are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Building from source

Requirements: Windows 10/11 x64, Visual Studio 2026 with the Windows Driver Kit, and Inno Setup 6 for
the installer.

```powershell
.\scripts\setup-dev.ps1                 # Visual Studio, SDK and WDK (elevated)
.\scripts\new-signing-certificate.ps1   # your own test-signing certificate
.\scripts\build.ps1                     # drivers + out\OpenMagic-Setup-<version>.exe
```

`scripts\install-drivers.ps1` and `scripts\uninstall-drivers.ps1` install the freshly built packages
without the installer, and `install-drivers.ps1 -Debug` turns on the drivers' diagnostic trace.
See [CONTRIBUTING.md](CONTRIBUTING.md).

## Disclaimer

OpenMagic is an independent project and is not affiliated with, endorsed by or sponsored by Apple Inc.
Apple, Magic Keyboard and Magic Mouse are trademarks of Apple Inc. The drivers were written from the
devices' own HID descriptors and from publicly documented protocol details, such as the Linux kernel's
`hid-apple` and `hid-magicmouse` drivers.

The drivers run in the Windows kernel. They are provided as is, without warranty; see the [license](LICENSE).

## License

[MIT](LICENSE) © 2026 Alef Barbeli
