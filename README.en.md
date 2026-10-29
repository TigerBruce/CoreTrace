<div align="center">

# CoreTrace

**Per-core CPU history, temperature and memory usage in a corner of your desktop.**

Native Windows · C++ / Win32 · Portable

[![Windows build](https://github.com/TigerBruce/CoreTrace/actions/workflows/windows.yml/badge.svg)](https://github.com/TigerBruce/CoreTrace/actions/workflows/windows.yml)
[![MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

[中文](README.md) · [Build guide](BUILDING.md) · [Contributing](CONTRIBUTING.md)

</div>

CoreTrace is a resizable desktop monitor for CPU load history, per-core temperature and frequency, system memory, and the five largest process-name groups by private working set. Pin the layout to hide its controls; normal application windows can cover it. A small pencil button opens the editor again.

The application UI is currently in Chinese. This English overview is for users and contributors browsing the project.

| Dark | Light |
| :---: | :---: |
| ![Dark theme](guide/images/dark.png) | ![Light theme](guide/images/light.png) |

These previews use the actual renderer with synthetic demonstration data, not performance measurements.

## Features

- CPU and memory history; physical-core charts grouped by P/E class when Windows supplies a suitable topology.
- Blue CPU area with temperature lines: green below 65°C, orange from 65 to below 80°C, red at 80°C and above.
- Per-core frequency, highest reported core frequency, and firmware-reported memory transfer rate.
- Top-five memory bars with a shared scale, grouped by process name.
- Drag-to-resize layout, independently resizable ranking, linked CPU/memory summary heights.
- Dark/light themes, separate background/card transparency, global font size and font selection.
- 20/40/60-second history, pause, tray controls, and two CPU metrics.
- Native Win32/GDI rendering; no Electron, WebView or .NET dependency. No network access during normal operation.

## Run or build

Download the `CoreTrace-windows-x64` artifact from a successful **main** run in [Windows build](https://github.com/TigerBruce/CoreTrace/actions/workflows/windows.yml). GitHub may require signing in. Extract the artifact, then the portable ZIP inside it, and run `CoreTrace.exe` from a writable folder. These are unsigned development builds, subject to artifact retention limits.

To build on Windows 10/11 x64:

```powershell
git clone https://github.com/TigerBruce/CoreTrace.git
cd CoreTrace
powershell -NoProfile -File ./setup-tools.ps1
./build.cmd
./native-portable/CoreTrace.exe --adjust
```

The setup script downloads a fixed llvm-mingw toolchain and validates its SHA-256. Subsequent builds can run offline. See [BUILDING.md](BUILDING.md) for packaging and source layout.

## Hardware support

CPU load and memory statistics use Windows interfaces without an extra driver. Temperature and hardware frequency require a separately installed official [PawnIO](https://github.com/namazso/PawnIO) driver and the bundled `IntelMSR.bin` beside the executable. The application never installs the driver automatically.

Sensor support currently covers selected Intel Alder/Raptor Lake models and has been verified on an i5-14600K. AMD sensors and ARM64/32-bit builds are not implemented. Other hardware needs additional testing. Missing sensor values are not zero-temperature readings.

## CPU metrics

**Utility (default)** reads Windows `% Processor Utility`, using the direct `_Total` counter for overall usage. **Time** reads `% Processor Time`; its overall value averages logical processors. Each physical-core chart shows the maximum value among its logical threads in either mode. Display values are capped at 100%.

Utility follows the metric used by Task Manager on the development machine; independent sampling and Windows versions can still cause differences. Neither metric measures every physical execution resource or predicts spare simulation throughput. See [metric details](guide/METRICS.md) (Chinese).

## Resource use and data

A bounded 70-frame history is kept in memory. Sampling waits about one second between passes and runs below normal thread priority. Rendering is driven by updates and UI events, without continuous animation. Window dimensions and transparency affect bitmap memory; no universal CPU/memory guarantee or long-run stability claim is made.

Settings live beside the executable in `settings.ini`. Normal use does not persist telemetry logs. Explicit diagnostic modes in the source can produce local files.

## Contributing and license

Bug reports, hardware compatibility reports, documentation improvements and focused pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and include your Windows version, CPU model and selected CPU metric when reporting counter differences.

Original code and documentation are [MIT licensed](LICENSE). Third-party components and the existing user-supplied icon artwork are separate; see [NOTICE.md](NOTICE.md) for scope and provenance.
