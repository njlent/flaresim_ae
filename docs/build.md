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
