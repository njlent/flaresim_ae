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

Verification:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

## Next slices
- AE SDK/CMake integration
- real SDK-backed `.aex` target
- Nuke-derived CUDA path
