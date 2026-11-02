# Contributing to OpenMagic

Thanks for your interest. Bug reports, device reports and pull requests are welcome.

## Reporting a problem

Open an [issue](https://github.com/alefbarbeli/OpenMagic/issues) with:

- your Windows version and build (`winver`);
- the device, its model number (on the bottom of the device) and the connection (Bluetooth or USB);
- what you expected and what happened.

For key or scrolling problems, a diagnostic trace helps a lot. Install the drivers with
`scripts\install-drivers.ps1 -Debug`, reproduce the problem and attach the output of:

```powershell
Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\OpenMagicKbd\Parameters' DebugLog    # keyboard
Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Services\OpenMagicMouse\Parameters' DebugLog  # mouse
```

The trace formats are described in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#diagnostics).

## Supporting another device

Other Magic Keyboard and Magic Mouse models need their hardware IDs in the INF files and, for the mouse,
a check of their multitouch report format. If you own one, open an issue with the output of
`Get-PnpDevice -PresentOnly | Where-Object InstanceId -match '004C|05AC'` and its HID descriptors.

## Building

1. `scripts\setup-dev.ps1` (elevated) installs Visual Studio 2026, the Windows SDK and the WDK.
   Add `-EnableTestSigning` on a test machine.
2. `scripts\new-signing-certificate.ps1` creates your own test-signing certificate.
3. `scripts\build.ps1` builds, signs and verifies both drivers and produces the installer in `out\`.
4. `scripts\install-drivers.ps1` (elevated) installs the built packages directly;
   `scripts\uninstall-drivers.ps1` removes them.

Kernel-mode bugs crash Windows, so test on a machine where that is acceptable, and keep a restore point
or another input device at hand.

## Pull requests

- Keep changes focused, one topic per pull request.
- Follow the style of the surrounding code: C with WDK types, four-space indentation, braces on every
  block, `Om`/`Omm` prefixes, short comments that explain why.
- Code must build without warnings at `/W4` and pass `infverif`.
- Describe how you tested the change and on which device.

This project is an independent implementation. Do not contribute code or data derived from proprietary
drivers or software.
