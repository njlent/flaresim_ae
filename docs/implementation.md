# Implementation Log

## Goal
Ship an AE implementation in staged slices, with frequent commits.

## Current slices
1. Extract host-agnostic core from original `blackhole-rt/flaresim`.
2. Add a host-agnostic full-frame render runtime around that core.
3. Add AE plugin scaffolding around the runtime.
4. Add verification harness + docs for build/SDK setup.

## Working rules
- original `blackhole-rt/flaresim` = source of truth for core optics/render behavior
- `flaresim_nuke` = reference for later GPU/UI features
- keep new notes in `docs/` as implementation advances

## Slice 1 scope
- root `CMakeLists.txt`
- shared `flaresim_core` library
- extracted source extraction + bloom modules
- smoke-test executable via `ctest`

## Next slices
- AE SDK/CMake integration
- Nuke-derived CUDA path
