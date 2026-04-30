# EFZ DirectDraw Wrapper

A small 32-bit `ddraw.dll` compatibility wrapper for older **EFZ** releases. It replaces the legacy DirectDraw path with a software 8-bit palette surface and presents frames through GDI, allowing the game to run in a modern windowed or borderless fullscreen setup without D3D or DXGI.

## Tested Versions

- EFZ 1.11
- EFZ BSE 2.13
- EFZ BME 3.03 Beta
- EFZ Memorial 4.00

## Features

- Windowed mode by default, centered at the game resolution
- Borderless fullscreen toggle with **F11**
- Software 8-bit palette surface emulation
- GDI `StretchDIBits` presentation
- DirectInput focus-loss guard to prevent stale key states
- Optional frame pacing, defaulting to 60 FPS in this source
- Per-frame diagnostics written to `ddraw_wrapper.log`

## Building

This wrapper must be built as a **32-bit DLL**, because the supported game executables are PE32/i386.

```bat
build.bat
```

Or with CMake:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The output DLL should be named:

```text
ddraw.dll
```

## Installation

1. Build `ddraw.dll`.
2. Copy it next to the target EFZ executable.
3. Launch the game normally.
4. Press **F11** to toggle between windowed and borderless fullscreen mode.

## Logs

Runtime logs are written to:

```text
ddraw_wrapper.log
```

The log is created in the game directory and includes DirectDraw calls, window-mode changes, focus events, rendering diagnostics, and shutdown messages.

## Notes

- This is a focused DirectDraw 1.0 compatibility shim, not a full DirectDraw implementation.
- Unsupported API calls generally return `DDERR_UNSUPPORTED` or a safe no-op result.
- The wrapper uses CPU-side surfaces and GDI presentation, so it does not require Direct3D, DXGI, or modern GPU features.
