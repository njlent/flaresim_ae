# Original Flaresim Audit

Purpose:
- record what came from `space55/blackhole-rt/flaresim`
- define extraction order into `flaresim_ae`

## Canonical baseline

Source:
- upstream: `space55/blackhole-rt/flaresim`
- role: original optics/render implementation

Already extracted into `src/core/`:
- `vec3.h`
- `fresnel.h`
- `lens.h/.cpp`
- `trace.h/.cpp`
- `ghost.h/.cpp`

These form the minimum host-agnostic core:
- lens parsing
- optical surface geometry
- ray tracing through the lens
- ghost pair enumeration
- CPU ghost rendering baseline

## Still living in original `main.cpp`

Not extracted yet:
- config parser
- EXR/TGA IO
- source extraction
- bloom
- ghost blur helpers
- sky brightness logic
- CLI wiring

Extraction priority:
1. source extraction
2. bloom + blur helpers
3. render config/result structs
4. host adapters for AE

## AE adaptation notes

Keep from original:
- optics math
- ghost normalization behavior
- bundled lens format + presets

Do not port literally:
- EXR/TGA output model
- config-file UX
- CLI entrypoint

Borrow later from `flaresim_nuke`:
- CUDA path
- preview/final quality split
- starburst
- haze
- pair UI ideas

## Verification baseline

During bring-up:
- original CPU renderer is the correctness oracle
- AE wrapper should first match the extracted CPU core
- CUDA path should then match the CPU core closely on shared scenes
