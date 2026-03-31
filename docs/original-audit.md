# Original Flaresim Audit

Canonical source:
- `space55/blackhole-rt/flaresim`

Best first extraction targets:
- `fresnel.h`
- `vec3.h`
- `lens.{h,cpp}`
- `trace.{h,cpp}`
- `ghost.{h,cpp}`

Worth extracting from original `main.cpp` immediately:
- bright-source extraction
- bloom

Already extracted:
- `src/core/vec3.h`
- `src/core/fresnel.h`
- `src/core/lens.{h,cpp}`
- `src/core/trace.{h,cpp}`
- `src/core/ghost.{h,cpp}`
- `src/core/image.h`
- `src/core/source_extract.{h,cpp}`
- `src/core/bloom.{h,cpp}`
- `src/runtime/render_frame.{h,cpp}`

Keep out initially:
- OpenEXR I/O
- TGA output
- config-file parsing
- CLI-only diagnostics
- exact `sky.R/G/B` sky brightness logic
- ghost blur helpers

Adapted later:
- threshold-based `sky_brightness` fallback for AE/plugin inputs without separate sky layers

Minimal verification:
- load bundled `.lens` preset
- enumerate expected ghost-pair count
- extract at least one bright source from synthetic image
- bloom produces non-zero spread from a hot pixel

Next extraction priorities:
1. ghost blur helpers
2. richer render config/result structs
3. AE host adapter layer
