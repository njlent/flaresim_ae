<div align="center">

 <h1> <img src="docs/icon.png" width="100px">
 <br/>
 Flaresim AE</h1>

 <img src="https://img.shields.io/badge/AfterEffects Addon-050059"/> 
 <br>
 <img src="https://custom-icon-badges.demolab.com/badge/AE 2025.5-0078D6?logo=windows11&logoColor=white"/> 
 <img src="https://img.shields.io/badge/CUDA-76B900?logo=nvidia&logoColor=fff"/> 


</div>
<br/>



After Effects port of FlareSim.

> [!IMPORTANT]
> This is mostly for personal exploration and NOT production tested or stable.

## Supported versions: 

AfterEffects 25.5 Windows + CUDA first.

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
- AE UI now includes:
  - grouped bundled lens presets under Lens
  - top-level Camera, Aperture, Flare Settings, and Post-processing sections
  - output `View` modes for Composite / Flare Only / Bloom Only / Sources / Diagnostics
  - visible `Max Sources` control in Flare Settings
  - `Ghost Cleanup` popup for Legacy Blur / Sharp Adaptive / Sharp + Blur
- low-threshold 32-bpc source detection now works for near-white values below `1.0`
- sharp adaptive ghost cleanup now reduces CUDA splat artifacts without forcing a full post blur
- local smoke tests green

Host validation:
- After Effects 2025 (`25.5x4`) is installed locally
- the built plugin has been verified in-host on March 30, 2026
- JSX host checks confirmed:
  - effect discovery/add by match name
  - live `View` popup values reaching the host effect
  - per-view renders differ in-host for Composite / Flare Only / Bloom Only / Sources / Diagnostics

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
- installed plugin path:
  - `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects\FlareSimAE.aex`
- current installed binary matches the local build output by SHA-256
- earlier cache/view-mode issues were narrowed down with JSX host tests and the current binary now switches output views in a clean AE project

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
- tighten Smart PreRender state / optional mask-layer flow
- add persistent per-instance CUDA/cache ownership instead of the current thread-local scratch cache
