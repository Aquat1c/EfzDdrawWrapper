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
- Optional overlay-callback export (`EFZDDRAW_RegisterOverlayCallback`) for companion mods

## How It Works

The DLL is placed next to the game EXE and implements only the DirectDraw 1.0 surface path the EFZ games actually use (`ddraw.c`):

1. **COM shims.** `DirectDrawCreate` returns lightweight C structs (`MyDDraw`, `MyDDSurface`, `MyDDPalette`) whose vtables (`g_DDVtbl`, `g_DDSurfVtbl`) list every method in exact SDK order. `MY_DDSURFACEDESC` mirrors the 108-byte SDK `DDSURFACEDESC` layout on 32-bit.
2. **CPU surfaces.** `DD_CreateSurface` and `surf_create` allocate system-memory 8-bit indexed buffers with a 4-byte-aligned pitch (`alloc_pixels`); `DDSurf_Lock`, `DDSurf_Blt`, and `DDSurf_BltFast` operate on those bytes directly, including color-keyed copies.
3. **GDI presentation.** `DDSurf_Flip` calls `present_frame`, which wraps the back buffer in a top-down 8-bit palette DIB and stretches it to the client area with `StretchDIBits`. Borderless fullscreen letterboxes via `compute_letterbox` and fills only the bar regions to avoid full-screen flashing.
4. **Window handling.** `DD_SetDisplayMode` applies the windowed or borderless window styles; `WrapperWndProc` subclasses the game window (re-presenting on `WM_PAINT`, suppressing `WM_ERASEBKGND` background erases), and the `LLKeyboardProc` low-level hook catches F11 even when the game swallows keys.
5. **Input guard.** `hook_dinput_iat` redirects the game's DirectInput imports through proxy devices; while the window is inactive, `DIDev_GetDeviceState` and `DIDev_Poll` return zeroed state instead of stale or failing reads.
6. **Frame pacing.** `DD_WaitForVerticalBlank` and `DDSurf_Flip` share `fps_throttle` (60 FPS by default) without double-waiting per frame.
7. **Overlay callback.** The exported `EFZDDRAW_RegisterOverlayCallback` (ordinal `@8` in `ddraw.def`) lets a companion DLL observe every presented frame: `present_frame` fills an `EfzDdrawOverlayFrame` (pixels, width, height, pitch, palette, window, `presentSerial`) and invokes the callback before the GDI blit. A faulting callback is caught and disables itself.

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

The log is created in the game directory and includes DirectDraw calls, window-mode changes, focus events, rendering diagnostics, and shutdown messages. Logging is enabled in Debug builds and compiled out of Release builds.

## Notes

- This is a focused DirectDraw 1.0 compatibility shim, not a full DirectDraw implementation.
- Unsupported API calls generally return `DDERR_UNSUPPORTED` or a safe no-op result.
- The wrapper uses CPU-side surfaces and GDI presentation, so it does not require Direct3D, DXGI, or modern GPU features.
