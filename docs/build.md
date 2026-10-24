# Build

## Local core build

Current local build verifies the shared core and host-agnostic runtime only.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

Targets covered:
- `flaresim_core`
- `flaresim_runtime`
- `flaresim_core_smoke`

Notes:
- OpenMP is optional.
- smoke tests load bundled lenses from `assets/lenses/space55/` and `assets/lenses/flaresim_nuke/`.
- if a CUDA toolkit is detected at configure time, the shared runtime builds `src/cuda/ghost_cuda.cu` automatically.
- if CUDA is missing or unavailable at runtime, the ghost renderer falls back to the CPU/OpenMP path automatically.
- `build/Debug/flaresim_core_smoke.exe` prints the selected ghost backend (`CUDA` or `CPU`) during render smoke runs.
- with the default Visual Studio generator on Windows, `ctest` needs `-C Debug`

## AE plugin configure/build

With the Adobe SDK extracted locally:

```bash
cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON
cmake --build build-ae --config Debug
cmake --build build-ae --config Release
```

`AE_SDK_ROOT` auto-detects `E:/projects/ae/SDK/AfterEffectsSDK_25.6_61_win` and resolves the inner Adobe SDK folder automatically when needed.
Set `-DAE_SDK_ROOT=...` if you want a different SDK root.

Current output:
- `build-ae/src/ae/Debug/FlareSimAE.aex`
- `build-ae/src/ae/Release/FlareSimAE.aex`

Current AE target coverage:
- PiPL/resource generation
- AE effect registration metadata
- Smart Render + legacy render bridge into shared runtime
- AE GPU Smart Render F32 selector path for CUDA hosts (`PF_Cmd_GPU_DEVICE_SETUP`, `PF_Cmd_SMART_RENDER_GPU`, `PF_PixelFormat_GPU_BGRA128`)
- device-resident CUDA flare/bloom/haze/starburst/composite path for AE GPU worlds
- Camera / Aperture / Flare Settings / Post-processing AE sections
- output `View` popup for Composite / Flare Only / Bloom Only / Sources / Diagnostics

GPU Smart Render note:
- current AE GPU path keeps full-frame image buffers on device end-to-end
- bright-source extraction, clustering, ghosting, haze, starburst, bloom, and composite now stay on device during AE GPU renders

Current host-validation status:
- After Effects 2025 (`25.5x4`) is installed locally
- the plugin is installed in:
  - `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects\FlareSimAE.aex`
- on March 30, 2026, host-side JSX checks verified:
  - effect discovery/add by match name
  - `View` popup readback through the installed effect
  - rendered outputs differ across Composite / Flare Only / Bloom Only / Sources / Diagnostics
- installed `.aex` matches `build-ae/src/ae/Debug/FlareSimAE.aex` by SHA-256

## Photoshop plugin configure/build

With the Photoshop C++ SDK extracted locally (Adobe Developer Console > Downloads > Photoshop > "Photoshop Plug-In and Connection SDK"):

```bash
cmake -S . -B build-ps -DFLARESIM_PS_ENABLE_PS_PLUGIN=ON
cmake --build build-ps --config Release --target flaresim_ps_plugin
```

`PS_SDK_ROOT` auto-detects `E:/projects/ae/SDK/adobe_photoshop_sdk_2026_win_v2`; pass `-DPS_SDK_ROOT=...` for another folder that contains `pluginsdk/` (or a parent holding `adobe_photoshop_sdk_*`).

Output:
- `build-ps/src/ps/Release/FlareSimPS.8bf`

Install (asks for elevation; restart Photoshop afterwards):

```powershell
scripts/install_ps_plugin.ps1
```

Layout:
- `src/ps/ps_settings.*`, `src/ps/ps_image.*`: SDK-free settings table/serialization, Photoshop pixel layouts (8/16/32-bit interleaved, premultiplied transparency), preview proxy. Built as `flaresim_ps_adapter` in every configuration.
- `src/ps/ps_dialog_win.cpp`: Win32 dialog with background preview renders. SDK-free.
- `src/ps/ps_plugin_main.cpp`, `PiPLs.json`, `FlareSimPS.rc`: SDK glue. The JSON PiPL is embedded as a `JSON_PIPL` resource; Photoshop 2026 needs no Cnvtpipl step.

Tests:
- `flaresim_ps_smoke` (ctest): settings round trip, pixel conversion at every depth, proxy scaling, 32-bit render.
- `flaresim_ps_dialog_harness --capture <prefix>`: opens the dialog outside Photoshop on a synthetic HDR image, captures it, clicks the preview and presses OK.
- `python tests/ps_host/run_host_test.py`: drives the installed plug-in in Photoshop over COM (via `cscript`; PowerShell's COM binding fails on Photoshop's type library) and compares 8/16/32-bit, selection, multi-band, Smart Filter, transparent-layer and preset (by name, with overrides, missing) results against `flaresim_ps_reference`. It writes a temporary `__flaresim_host_test__` preset to `%APPDATA%\FlareSim\presets` and removes it afterwards.
