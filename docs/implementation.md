# Implementation Log

## Goal
Ship an AE implementation in staged slices, with frequent commits.

## Current slices
1. Extract host-agnostic core from original `blackhole-rt/flaresim`.
2. Add a host-agnostic full-frame render runtime around that core.
3. Add buildable AE-facing adapter code around the runtime.
4. Add SDK-backed plugin entry/render glue.
5. Add verification harness + docs for build/SDK setup.

## Working rules
- original `blackhole-rt/flaresim` = source of truth for core optics/render behavior
- `flaresim_nuke` = reference for later GPU/UI features
- keep new notes in `docs/` as implementation advances

## Slice 1 scope
- root `CMakeLists.txt`
- shared `flaresim_core` library
- extracted source extraction + bloom modules
- smoke-test executable via `ctest`

## Completed

### Slice 1
- extracted original optics core into `src/core/`
- shared `flaresim_core` target
- smoke test target via `ctest`

### Slice 2
- extracted `source_extract` from original `main.cpp`
- extracted original bloom into `src/core/`
- added `src/runtime/render_frame.*` as a host-agnostic frame wrapper
- local build now covers `flaresim_core`, `flaresim_runtime`, and `flaresim_core_smoke`

### Slice 3
- added buildable `src/ae/` adapter code
- added built-in lens descriptors and AE parameter translation
- documented current SDK gap in `docs/ae-sdk.md`

### Slice 4
- added built-in/external lens resolution + loading in the AE-facing layer
- preset chooser path can now resolve bundled lens IDs into actual `.lens` files

### Slice 5
- added output-view compositing in `src/ae/output_view.*`
- AE-facing layer can now produce composite / flare-only / bloom-only / source / diagnostics buffers from runtime output
- smoke coverage now exercises output-view composition

### Slice 6
- added `src/ae/pixel_convert.*` for AE-style 8/16/32 pixel pack/unpack helpers
- 8/16-bit helpers clamp only at final integer pack
- 32-bit float helpers preserve HDR values above `1.0`
- smoke coverage now checks 8/16 clamp behavior and 32-bit HDR retention

Verification:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

## Next slices
- AE SDK/CMake integration
- real SDK-backed `.aex` target
- Nuke-derived CUDA path
