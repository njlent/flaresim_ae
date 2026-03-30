# Build

## Local core build

Current local build verifies the shared core and host-agnostic runtime only.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Targets covered:
- `flaresim_core`
- `flaresim_runtime`
- `flaresim_core_smoke`

Not covered yet:
- After Effects plugin target
- Adobe SDK wiring
- CUDA path

Notes:
- OpenMP is optional.
- smoke tests load bundled lenses from `assets/lenses/space55/`.

## AE plugin configure/build

With the Adobe SDK extracted locally:

```bash
cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON
cmake --build build-ae
```

`AE_SDK_ROOT` auto-detects `E:/projects/ae/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK`.
Set `-DAE_SDK_ROOT=...` if you want a different SDK root.
