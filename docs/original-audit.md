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

Keep out initially:
- OpenEXR I/O
- TGA output
- config-file parsing
- CLI-only diagnostics

Minimal verification:
- load bundled `.lens` preset
- enumerate expected ghost-pair count
- extract at least one bright source from synthetic image
- bloom produces non-zero spread from a hot pixel
