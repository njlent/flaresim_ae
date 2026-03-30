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
```

`AE_SDK_ROOT` auto-detects `E:/projects/ae/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK`.
Set `-DAE_SDK_ROOT=...` if you want a different SDK root.

Current output:
- `build-ae/src/ae/Debug/FlareSimAE.aex`

Current AE target coverage:
- PiPL/resource generation
- AE effect registration metadata
- Smart Render + legacy render bridge into shared runtime
- internal CUDA ghost rendering with CPU output back to AE worlds

Current host-validation gap:
- this repo can build the `.aex`
- this session could not copy into `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects` because of Windows permissions
- final in-host effect discovery/load still needs an elevated or manual install step
