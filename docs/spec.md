# FlareSim AE Spec

## Goal
Build an Adobe After Effects version of FlareSim.

Implementation basis:
- primary source of truth: [space55/blackhole-rt `flaresim/`](https://github.com/space55/blackhole-rt/tree/main/flaresim)
- secondary reference: [LocalStarlight/flaresim_nuke](https://github.com/LocalStarlight/flaresim_nuke)

Primary target:
- Windows 11
- After Effects 25.4+
- NVIDIA GPU
- CUDA-first acceleration

Core promise:
- physically-based lens ghosts from real `.lens` prescriptions
- interactive preview on modern RTX hardware
- AE-native workflow, not a Nuke-shaped port

Product target:
- match the spirit and core behavior of the Nuke plugin where it improves the artist experience
- but treat the original `blackhole-rt/flaresim` code as the canonical optics/render baseline

## Why This Is Not A Straight Port
`flaresim_nuke` assumes host behavior that AE does not share.

Key deltas:
- original `blackhole-rt/flaresim` is a CLI/offline EXR renderer, not a host plugin
- Nuke plugin does lazy full-frame compute on first scanline request; AE wants SmartFX pre-render/render.
- Nuke writes extra named channels (`flare.rgb`, `source.rgb`, `haze.rgb`, `starburst.rgb`); AE effects output one image only.
- Nuke exposes a filesystem file picker for `.lens`; AE has no normal arbitrary file-path effect param.
- Nuke UI can cheaply emit hundreds of pair toggles; AE needs arbitrary-data + custom UI or a less literal UX.
- AE Multi-Frame Rendering means render/setup may overlap across threads; mutable globals/sequence state are risky.

Implication:
- port optics/render core
- redesign host glue + UI
- treat feature parity as phased, not milestone-1

## Source Of Truth
`space55/blackhole-rt/flaresim` should be treated as the canonical basis for:
- `.lens` format + bundled lenses
- `lens.*`: lens parsing + surface geometry
- `trace.*`, `fresnel.h`, `vec3.h`: ray/optics math
- `ghost.*`: ghost-pair enumeration + CPU render baseline
- bright-source extraction logic
- ghost normalization / area boost behavior
- original bloom behavior

Why:
- it is the original code the Nuke plugin was based on
- less host baggage
- cleaner core extraction path
- better correctness oracle for regression tests

## Feature Lineage
### Original `blackhole-rt/flaresim`
Baseline features from the original code:
- CPU/OpenMP ghost renderer
- EXR/TGA-oriented source extraction + output flow
- bloom
- ghost blur
- ghost normalization
- config-driven parameter model
- bundled `.lens` library

### Later `flaresim_nuke`
Useful derivative features to borrow selectively:
- CUDA acceleration
- Nuke host integration patterns
- preview/final quality split
- starburst diffraction pass
- haze / veiling glare
- richer camera/aperture controls
- pair-selection UI
- source clustering

Rule:
- start core implementation from the original codebase
- port Nuke additions one by one as explicit enhancements

## AE v1 Scope
Must have:
- single AE effect plugin (`.aex`)
- 8-bpc support
- 16-bpc support
- 32-bpc SmartFX render path
- HDR-safe float processing; no clamp at 1.0 in 32-bpc mode
- Windows CUDA acceleration for ghost pass
- `.lens` support
- optional mask layer input
- preview/final quality controls
- ghost-only / composite / debug views
- stable behavior under AE MFR

Should have:
- bloom, because it exists in the original codebase
- haze
- starburst
- bundled starter lens library, including the `space55/blackhole-rt/flaresim/lenses` set
- custom lens import on Windows
- project-safe serialization of lens selection + pair state

Can slip:
- macOS build
- Premiere support
- zero-copy AE GPU output path
- full 1:1 pair-checkbox parity with Nuke UI

## Non-Goals For First Release
- Linux support
- non-NVIDIA GPU support
- exact Nuke channel model
- exact Nuke Python lens browser UX
- cross-host shared UI layer
- CPU performance parity with CUDA path

## Host Translation Plan
### Render model
Use SmartFX, not legacy render selectors.

Plan:
- `PF_OutFlag_DEEP_COLOR_AWARE`
- `PF_OutFlag2_SUPPORTS_SMART_RENDER`
- `PF_OutFlag2_FLOAT_COLOR_AWARE`
- 32-bpc first-class
- `PF_Cmd_SMART_PRE_RENDER` requests full source frame and optional mask layer
- `PF_Cmd_SMART_RENDER` does CPU-output render for v1
- preserve scene-linear values above 1.0 end-to-end in 32-bpc comps

Reason:
- upstream algorithm is full-frame/global anyway
- source detection scans whole frame
- haze/starburst are whole-frame post passes
- CPU output avoids early lock-in to AE GPU surface interop

Important nuance:
- v1 still uses CUDA for heavy rendering
- "CPU output" means AE receives a CPU frame
- it does not mean the renderer is CPU-only
- output writeback must not normalize or clip HDR values in float mode

### Bit-depth policy
Support all AE render depths the host asks for:
- 8-bpc
- 16-bpc
- 32-bpc float

Implementation rule:
- convert input pixels to one internal float pipeline
- run optics/render in float regardless of host depth
- convert back only at final writeback

Writeback rule:
- 32-bpc: preserve values above `1.0`
- 8/16-bpc: honor host pixel-format limits only at final pack step; no earlier clamp/normalize

### GPU strategy
Phase split:
1. v0/v1 dev basis: keep the original CPU renderer available as correctness reference and fallback path during bring-up
2. v1 ship path: internal CUDA compute + copy result into AE CPU output buffer
3. v2: evaluate `PF_Cmd_GPU_SMART_RENDER_GPU` / host GPU-frame path only if copy cost is material

Why:
- fastest route to a working Windows/CUDA product
- preserves the original renderer as a validation oracle
- lower host-interop risk
- keeps CUDA code close to upstream
- AE docs recommend CUDA Driver API for forward compatibility; adopt that at design time

### Output model
AE cannot emit Nuke-style auxiliary channel groups from a normal effect. Replace channel outputs with a `View` popup:
- Composite
- Flare Only
- Bloom Only
- Sources
- Haze Only
- Starburst Only
- Diagnostics

### Mask input
AE effect parameter model:
- `param[0]`: source layer
- extra `PF_Param_LAYER`: optional mask layer

Mask behavior:
- sample chosen mask layer in comp space from the AE layer selector
- derive detection weight from visible content, not alpha alone
- use `max(R,G,B) * alpha` only during source extraction
- keep ghost/haze/starburst rendering full-frame from the surviving sources
- mask choice must not crop final flare output

## User-Facing Feature Set
### MVP controls
- Lens
- Flare Gain
- Sky Brightness (approximate threshold-based background scale; exact upstream sky-layer mode needs separate `sky.*` inputs)
- Ray Grid
- Threshold
- Source Cap
- Manual/offscreen source position + intensity/color
- Max Sources
- Downsample
- Cluster Radius
- Preview Mode + preview overrides
- Bloom strength/radius/passes or equivalent original-bloom controls
- Camera mode: direct FOV or sensor size + focal length
- Aperture blades + rotation
- Spectral samples
- Spectral jitter + seed
- Ghost blur + passes
- Haze gain/radius/passes
- Starburst gain/scale
- Show Sources
- Output/View mode
- Mask Layer

### Pair control plan
Do not start with 500 static checkbox params.

Phase 1:
- auto-filter pairs only
- optional `Pair Mode`: All / Bright Only / Top N / Manual
- ship without manual per-pair UI if needed

Phase 2:
- store pair enables as arbitrary-data bitset
- custom ECW list using Drawbot
- filter/search/sort active pairs
- multi-select enable/disable

## Lens Handling
### Format
Keep upstream `.lens` format unchanged.

### Selection UX
Because AE lacks a normal filesystem path param for arbitrary files:
- ship bundled lens preset library
- add custom `Import Lens...` button on Windows
- persist imported path + display name in arbitrary data / flattened state

Built-in presets for v1 should include the `space55/blackhole-rt/flaresim/lenses` set:
- `arri-zeiss-master-prime-t1.3-50mm.lens`
- `canon-ef-200-400-f4.lens`
- `cooketriplet.lens`
- `doublegauss.lens`
- `test.lens`

Plugin UX requirement:
- these bundled presets must be directly choosable in-plugin from a preset popup/browser, not hidden as repo-only assets

Stored lens selection should include:
- source kind: bundled vs external
- preset ID or absolute path
- display name
- last-known file timestamp/hash

Behavior:
- if external file missing, effect bypasses with warning state
- no hard crash, no black-frame surprise without UI feedback

## Architecture
### Module split
Recommended tree:

```text
src/
  core/
    fresnel.h
    vec3.h
    lens.{h,cpp}
    trace.{h,cpp}
    ghost.{h,cpp}
    bloom.{h,cpp}
    starburst.{h,cpp}
    source_extract.{h,cpp}
    render_config.h
    render_result.h
  cuda/
    cuda_device.h
    cuda_device_win.cpp
    ghost_cuda_driver.cu
    ghost_cuda_driver.h
  ae/
    entry.cpp
    global_setup.cpp
    params.cpp
    smart_prerender.cpp
    smart_render.cpp
    lens_ui.cpp
    pair_ui.cpp
    compute_cache.cpp
    sequence_data.h
  tests/
    ...
```

### Core render flow
Per frame:
1. Checkout source layer + optional mask layer.
2. Resolve params.
3. Load lens selection from cache.
4. Compute FOV/sensor mapping.
5. Extract bright sources from frame.
6. Cluster sources.
7. Filter active ghost pairs.
8. Run CUDA ghost render.
9. Run bloom, then haze/starburst if enabled.
10. Composite output according to selected view mode.

### Cache plan
Use AE Compute Cache for expensive, deterministic objects:
- parsed lens system
- filtered ghost pair list
- pair spread/boost data
- bloom kernels / intermediate plans if worth caching
- starburst PSF per aperture shape

Do not lean on mutable sequence data for heavy render-time state. Use sequence data only for small immutable-per-instance config, if needed.

Compute-cache keys should include:
- effect params affecting result
- lens identity/hash
- source layer state hash
- mask layer state hash
- frame/time where relevant

Device-side persistent allocations should live in per-device plugin-owned GPU data, not in global mutable singletons.

## Threading / MFR
Target:
- safe under `PF_OutFlag2_SUPPORTS_THREADED_RENDERING`

Rules:
- no unsynchronized mutable globals
- no render-time writes into shared sequence data
- per-device CUDA state isolated
- compute cache handles cross-thread reuse
- host wrapper stays re-entrant

If MFR safety slips, do not fake safety. Ship without threaded-render flag until verified.

## Rendering Path Decision
### v1 recommended path
Use:
- SmartFX CPU output
- internal CUDA Driver API compute
- copy final float RGB into AE output world
- keep float values above 1.0 intact in the AE output buffer
- add depth adapters for `PF_Pixel8`, `PF_Pixel16`, and `PF_PixelFloat`
- keep a CPU baseline path from the original code during development and validation

Why this is the best first build:
- closest to the original optics pipeline while still landing on the requested Windows/CUDA product
- lowest AE GPU-interop risk
- easiest to debug
- still satisfies Windows + CUDA-first goal

### v2 optional path
Investigate:
- `PF_Cmd_GPU_DEVICE_SETUP`
- `PF_Cmd_GPU_SMART_RENDER_GPU`
- host GPU-frame output

Only pursue if profiling shows CPU copy is a real bottleneck.

## UI Plan
### Standard controls
Use normal AE params for:
- sliders
- checkboxes
- popups
- mask layer param

Use parameter supervision for:
- preview mode enable/disable
- FOV mode switching
- bundled vs imported lens mode

### Custom controls
Use custom ECW UI only where AE standard params are a bad fit:
- lens picker/import status
- pair editor
- maybe diagnostics summary

Keep custom UI small. No giant bespoke panel for the whole effect.

## Performance Targets
Set targets relative to upstream Nuke behavior on same GPU.

Acceptance goals:
- AE overhead vs upstream render core: under 15% for same scene/settings
- CUDA output should track the original CPU renderer closely on matched settings
- preview preset: interactive at 1080p on current midrange RTX hardware
- final preset: stable 4K renders without GPU memory churn/frame-to-frame leaks

Hard requirements:
- no per-frame `cudaMalloc/cudaFree` churn in steady state
- lens parse not repeated every frame unless source changes
- starburst PSF reused until aperture shape changes
- no clamp/normalize of values above 1.0 in 32-bpc mode
- 8/16-bpc paths use the same core renderer, not a degraded separate effect

## Quality / Correctness Targets
The AE port should match upstream for:
- ghost position
- ghost ordering / pair contribution
- chromatic separation
- aperture footprint
- original bloom response where bloom is enabled
- haze/starburst general look
- HDR energy retention above 1.0

Allowed initial differences:
- UI/UX
- debug presentation
- minor numeric drift from CUDA runtime -> driver migration

## Validation Plan
### Unit / low-level
- lens parser fixtures
- surface intersection tests
- CPU trace tests
- ghost pair enumeration/filter tests
- bloom tests
- starburst PSF symmetry/normalization tests
- pixel unpack/pack tests for 8/16/32-bpc adapters

### Golden renders
Build canonical scenes and compare AE output first against the original `blackhole-rt/flaresim` renderer, then against the Nuke plugin for derivative features:
- single bright point, on-axis
- single bright point, edge-of-frame
- bright source well above 1.0
- multiple highlights
- original bloom-enabled scene
- polygon aperture
- high spectral sample count
- haze + starburst enabled

Comparison metrics:
- per-pixel RMSE / max error
- centroid of main ghosts
- relative energy by pair
- explicit check that >1.0 input/output values remain >1.0 in 32-bpc mode

Validation hierarchy:
- original `blackhole-rt/flaresim` = baseline for core ghost physics + bloom
- `flaresim_nuke` = reference for CUDA-era additions like haze/starburst/preview UX

### In-host integration
- automated `aerender` project renders on Windows
- run the same scenes in 8/16/32-bpc comps
- save goldens
- compare against known-good PNG/EXR outputs

### Stress
- MFR enabled
- parameter scrubbing
- lens swap during timeline playback
- large source counts
- repeated comp open/close

## Packaging
First release:
- Windows `.aex`
- bundled lens library in plugin resources or sidecar data dir
- installer or zip drop-in for AE MediaCore path

CUDA packaging:
- prefer CUDA Driver API
- avoid runtime-DLL dependency if possible
- if runtime dependency remains, package it intentionally and version-pin it

## Milestones
### M0: feasibility spike
- trivial AE SmartFX effect builds on Windows
- original CPU flare render callable from extracted core
- CUDA Driver API init works inside AE host
- render one CUDA-generated frame into AE output
- decide if CPU-output path is sufficient for v1

Exit criteria:
- confirmed toolchain
- confirmed host stability
- confirmed plugin install/debug loop

### M1: shared core extraction
- extract reusable optics/render code from `blackhole-rt/flaresim` first
- keep the original CPU path building in a standalone test harness
- layer in Nuke-derived additions only after the core baseline is stable
- add standalone test harness
- preserve original upstream golden behavior

Exit criteria:
- standalone tests green
- core library independent from Nuke headers

### M2: AE MVP render
- SmartFX effect
- source + mask checkout
- lens load
- source detection
- CUDA ghost render
- composite / flare-only / source debug output

Exit criteria:
- usable first effect inside AE
- stable on Windows/NVIDIA

### M3: parity pass
- haze
- starburst
- preview mode
- camera/aperture/spectral controls
- compute cache integration

Exit criteria:
- feature-complete enough for internal use

### M4: UX pass
- lens import UI
- diagnostics state
- pair-mode improvements
- optional manual pair editor

Exit criteria:
- no obviously "ported from another host" rough edges

### M5: ship pass
- docs
- installer/package
- benchmark table
- regression suite
- crash/perf soak

Exit criteria:
- repeatable install
- green render tests
- acceptable perf/stability

## Main Risks
### 1. AE GPU path complexity
Risk: host GPU-frame interop adds large schedule risk.
Mitigation: ship v1 with CPU output + internal CUDA.

### 2. Pair editor UX
Risk: literal Nuke parity produces unusable AE UI.
Mitigation: phase manual pair UI after MVP; start with auto-filter + top-N controls.

### 3. Lens file UX
Risk: AE lacks normal arbitrary file-picker param.
Mitigation: bundled presets first; Windows import button + custom ECW status.

### 4. MFR/thread safety
Risk: subtle crashes/data races under AE render concurrency.
Mitigation: compute cache, no mutable shared render state, explicit MFR stress tests before enabling flag.

### 5. Windows/CUDA compatibility drift
Risk: CUDA runtime/version mismatch with AE or driver.
Mitigation: use CUDA Driver API if possible; lock first release to validated AE/CUDA toolchain.

## Open Questions
- Is v1 allowed to ship without manual per-pair toggles?
- Do we want external `.lens` import in v1, or can it land shortly after the bundled presets?
- Is CPU-output + internal CUDA acceptable for first release, or must AE show full GPU-badged render support immediately?
- Should mask input support alpha only, or selectable RGBA/luma channels?

## Recommendation
Build this in two tracks:
1. establish the original `blackhole-rt/flaresim` core inside a standalone/shared library and keep its CPU path as a correctness oracle
2. ship a Windows-only, CUDA-accelerated AE SmartFX effect with CPU output using that core
3. pursue zero-copy GPU render path and full manual pair UI only after the render core is stable

That path gets the real product into AE fastest, preserves upstream optics, and keeps the risky AE-host-specific work isolated.

## References
- Original source: [space55/blackhole-rt `flaresim/`](https://github.com/space55/blackhole-rt/tree/main/flaresim)
- Upstream repo: [LocalStarlight/flaresim_nuke](https://github.com/LocalStarlight/flaresim_nuke)
- Adobe AE SDK: [Building GPU Effects](https://ae-plugins.docsforadobe.dev/intro/gpu-build-instructions/)
- Adobe AE SDK: [Command Selectors](https://ae-plugins.docsforadobe.dev/effect-basics/command-selectors/)
- Adobe AE SDK: [SmartFX](https://ae-plugins.docsforadobe.dev/smartfx/smartfx/)
- Adobe AE SDK: [Multi-Frame Rendering in AE](https://ae-plugins.docsforadobe.dev/effect-details/multi-frame-rendering-in-ae/)
- Adobe AE SDK: [Compute Cache API](https://ae-plugins.docsforadobe.dev/effect-details/compute-cache-api/)
- Adobe AE SDK: [Parameter Supervision](https://ae-plugins.docsforadobe.dev/effect-details/parameter-supervision/)
- Adobe AE SDK: [Arbitrary Data Parameters](https://ae-plugins.docsforadobe.dev/effect-details/arbitrary-data-parameters/)
- Adobe AE SDK: [Custom UI & Drawbot](https://ae-plugins.docsforadobe.dev/effect-ui-events/custom-ui-and-drawbot/)
