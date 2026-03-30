# flaresim_ae

After Effects port of FlareSim.

Windows + CUDA first.
Implementation basis: original `space55/blackhole-rt/flaresim`, with later `flaresim_nuke` features layered on selectively.

Current status:
- shared optics core extracted from original `blackhole-rt/flaresim`
- host-agnostic full-frame runtime in place
- Windows CUDA ghost renderer now wired into the shared runtime:
  - internal CUDA compute for the ghost pass
  - automatic CPU fallback if CUDA is unavailable at runtime
  - backend selection exposed in smoke coverage
- AE-facing adapter layer in place for:
  - bundled/external lens resolution
  - output-view composition
  - 8/16/32-bit pixel conversion
  - one float render bridge across all supported bit depths
  - shared AE param schema + grouped popup mapping
- SDK-gated AE plugin target builds on Windows with the local Adobe SDK
- AE metadata/registration wiring now in place:
  - PiPL resource generation
  - `PluginDataEntryFunction2`
  - stable effect name/category/match name
- Smart Render + legacy render now call the shared frame bridge
- bundled `space55` + `flaresim_nuke` lens presets included by default
- AE lens UI now nests bundled presets under a collapsible Lens section, then by manufacturer, with a separate collapsible Flare Settings section
- local smoke tests green

Current blocker:
- final in-host validation still needs an elevated/manual copy into the Adobe plug-in folder on this machine

Implemented so far:
- `src/core/`: extracted lens/ghost/bloom/source code
- `src/runtime/`: host-agnostic frame renderer
- `src/ae/`: adapter/runtime bridge plus SDK-backed AE plugin glue
- `tests/core_smoke.cpp`: coverage for lens loading, rendering, output views, bit-depth conversion, asset-root discovery, and AE param schema mapping across grouped + legacy lens selectors
- `tests/ae_smoke_test.jsx`: host-side smoke script for AE effect discovery / add checks

Local verify:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

CUDA notes:
- if a CUDA toolkit is present at configure time, `src/cuda/ghost_cuda.cu` is built and linked automatically
- current Windows build uses the shared CUDA runtime and targets common Turing/Ampere/Ada architectures by default
- runtime ghost rendering prefers CUDA first and falls back to CPU if the driver/GPU path is unavailable

To configure the AE plugin target with the extracted SDK:

```bash
cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON
cmake --build build-ae --config Debug
```

`AE_SDK_ROOT` auto-detects `E:/projects/ae/AfterEffectsSDK_25.6_61_win/ae25.6_61.64bit.AfterEffectsSDK`.
Override it explicitly if your SDK lives elsewhere.

Built plugin output:
- `build-ae/src/ae/Debug/FlareSimAE.aex`

Current host-test note:
- this session could launch After Effects and run JSX smoke scripts
- this session could not write into `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects`
- final load verification therefore still requires an elevated/manual copy of `FlareSimAE.aex`

Start here:
- [docs/spec.md](docs/spec.md)
- [docs/implementation.md](docs/implementation.md)
- [docs/original-audit.md](docs/original-audit.md)
- [docs/build.md](docs/build.md)
- [docs/ae-sdk.md](docs/ae-sdk.md)

Bundled lens presets:
- [assets/lenses/space55/manifest.json](assets/lenses/space55/manifest.json)
- [assets/lenses/flaresim_nuke/manifest.json](assets/lenses/flaresim_nuke/manifest.json)

Next major steps:
- complete elevated/manual in-host validation against the installed AE plug-in folder
- tighten Smart PreRender state / optional mask-layer flow
- add persistent per-instance CUDA/cache ownership instead of the current thread-local scratch cache
