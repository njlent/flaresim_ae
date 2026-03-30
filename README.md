# flaresim_ae

After Effects port of FlareSim.

Windows + CUDA first.
Implementation basis: original `space55/blackhole-rt/flaresim`, with later `flaresim_nuke` features layered on selectively.

Current status:
- shared optics core extracted from original `blackhole-rt/flaresim`
- host-agnostic full-frame runtime in place
- AE-facing adapter layer in place for:
  - bundled/external lens resolution
  - output-view composition
  - 8/16/32-bit pixel conversion
  - one float render bridge across all supported bit depths
  - shared AE param schema + popup mapping
- SDK-gated AE plugin scaffold in place
- real `PF_Cmd_PARAM_SETUP` registration code in place
- bundled `space55` lens presets included and selectable by schema
- smoke tests green locally

Current blocker:
- no local Adobe After Effects SDK in this environment, so the `.aex` target and SmartFX selector path are not compiled end-to-end yet

Implemented so far:
- `src/core/`: extracted lens/ghost/bloom/source code
- `src/runtime/`: host-agnostic frame renderer
- `src/ae/`: adapter/runtime bridge for AE-facing work that can be tested without the SDK
- `tests/core_smoke.cpp`: coverage for lens loading, rendering, output views, bit-depth conversion, asset-root discovery, and AE param schema mapping

Local verify:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Start here:
- [docs/spec.md](docs/spec.md)
- [docs/implementation.md](docs/implementation.md)
- [docs/original-audit.md](docs/original-audit.md)
- [docs/build.md](docs/build.md)
- [docs/ae-sdk.md](docs/ae-sdk.md)

Bundled lens presets:
- [assets/lenses/space55/manifest.json](assets/lenses/space55/manifest.json)

Next major steps:
- finish Smart PreRender / Smart Render world checkout against the Adobe SDK
- build the real `.aex` target on Windows with the AE SDK present
- add the Windows CUDA render path
