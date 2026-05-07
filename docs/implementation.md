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

### Slice 3
- added buildable `src/ae/` adapter code
- added built-in lens descriptors and AE parameter translation
- documented current SDK gap in `docs/ae-sdk.md`

### Slice 4
- added built-in/external lens resolution + loading in the AE-facing layer
- preset chooser path can now resolve bundled lens IDs into actual `.lens` files

### Slice 5
- added output-view compositing in `src/ae/output_view.*`
- AE-facing layer can now produce composite / flare-only / bloom-only / source / diagnostics buffers from runtime output
- smoke coverage now exercises output-view composition

### Slice 6
- added `src/ae/pixel_convert.*` for AE-style 8/16/32 pixel pack/unpack helpers
- 8/16-bit helpers clamp only at final integer pack
- 32-bit float helpers preserve HDR values above `1.0`
- smoke coverage now checks 8/16 clamp behavior and 32-bit HDR retention

### Slice 7
- added `src/ae/frame_bridge.*` as the host-agnostic AE render path
- bridge unpacks 8/16/32 AE-style pixels into one float image buffer
- bridge resolves lens preset, runs shared renderer, composes selected output view, then packs back to host depth
- bridge keeps source alpha intact while writing rendered RGB

### Slice 8
- added `src/ae/asset_root.*` to discover the bundled lens asset root from an anchor path
- built-in lens loading now depends on an asset root, not a checkout-specific repo root assumption
- smoke coverage now verifies asset-root discovery before built-in lens loads and frame-bridge renders

### Slice 9
- added `src/ae/param_schema.*` for shared AE param ids and popup schemas
- lens preset popup and output-view popup strings now come from one tested source
- popup-index -> `AeParameterState` translation now lives outside the Adobe SDK layer

### Slice 10
- replaced the PARAM_SETUP placeholder with real AE popup/slider/layer registration code in `src/ae/plugin_params.cpp`
- plugin param defaults now come from the same shared UI schema used by tests
- local smoke build still stays SDK-free; Adobe-side compilation remains pending an installed SDK

### Slice 11
- added PiPL/resource generation for the AE plugin target in `src/ae/CMakeLists.txt`
- added shared AE effect metadata in `src/ae/plugin_version.h`
- added `PluginDataEntryFunction2` registration export in `src/ae/plugin_entry.cpp`
- local SDK builds now emit a metadata-carrying `FlareSimAE.aex`

### Slice 12
- replaced Smart Render / legacy render no-op scaffolding with real world checkout + frame-bridge calls in `src/ae/plugin_smart_render.cpp`
- added repo-root/module-path asset-root fallback for local plugin runs
- added `tests/ae_smoke_test.jsx` as a host-side smoke script for AE effect discovery/add checks

### Slice 13
- imported the `flaresim_nuke` lens catalog into generated bundled-lens manifests/descriptors
- expanded the AE lens chooser to a collapsible outer lens group, manufacturer popup, and collapsible per-manufacturer lens groups
- grouped flare gain / threshold / ray grid / downsample under a collapsible Flare Settings section
- kept the legacy built-in lens popup hidden for old-project compatibility
- smoke coverage now checks grouped manufacturer selection and legacy popup fallback

### Slice 14
- added `src/cuda/ghost_cuda.*` as a CUDA ghost renderer derived from the `flaresim_nuke` kernel structure
- CMake now auto-detects CUDA and builds a `flaresim_cuda` target with a stub fallback when no toolkit is present
- `src/core/ghost.cpp` now prefilters pairs once, tries CUDA first, and falls back to the CPU/OpenMP renderer on failure
- `src/runtime/render_frame.*` now records which ghost backend ran so smoke tests can verify the selection path
- local smoke renders now report `Ghost renderer backend: CUDA` on this RTX 3090 workstation

### Slice 15
- added AE Camera, Aperture, Flare Settings, and Post-processing controls derived from the Nuke plugin feature set
- wired camera FOV/sensor controls, aperture blades/rotation, ghost blur, haze, starburst, and spectral samples into the shared runtime
- added a visible `Max Sources` AE control in Flare Settings
- improved 32-bpc source extraction so near-white values below `1.0` can still trigger flares
- `Sources` view now follows the actually applied source list after `Max Sources` filtering
- host-side AE verification on March 30, 2026 confirmed that installed output views switch correctly in After Effects 2025 (`25.5x4`)

### Slice 16
- added shared `Ghost Cleanup` mode selection with `Legacy Blur`, `Sharp Adaptive`, and `Sharp + Blur`
- CUDA ghost rendering now supports per-pair adaptive tent splats so sparse ghost samples can fill in without a full-image blur pass
- shared runtime now only applies post ghost blur when the selected cleanup mode includes it
- smoke coverage now verifies popup/schema mapping and runtime state propagation for the new cleanup mode

### Slice 17
- added an AE `Sky Brightness` control derived from the original `flaresim` fallback path
- shared runtime now scales sub-threshold scene pixels before source extraction, bloom, and composite/diagnostic output when `Sky Brightness != 1`
- exact upstream `sky.R/G/B` layer handling remains out of scope for AE because the current host path only receives one beauty image
- smoke coverage now verifies runtime scaling, parameter plumbing, and frame-bridge output for the new control

### Slice 18
- split shared runtime work into scene, source extraction, ghosts, bloom, haze, and starburst stages with per-stage reuse keyed by actual inputs/settings
- AE render bridge now keeps a thread-local stage cache so post controls like ghost blur can reuse prior source/ghost/bloom work during iteration
- output-view planning now skips unrelated systems entirely; `Sources` and `Diagnostics` no longer run flare/bloom/haze/starburst work
- smoke coverage now verifies cache reuse on blur-only edits and verifies that `Sources` view leaves other stage buffers untouched

### Slice 19
- added per-pair ghost sampling plans with local footprint probes in the sharp cleanup path
- `SharpAdaptive` now derives splat radius from traced pupil-to-sensor footprint estimates instead of relying only on pair-wide spacing heuristics
- CUDA ghost rendering now mirrors the same footprint-aware radius selection per sample
- smoke coverage now exercises footprint-radius selection alongside adaptive pair planning

### Slice 20
- added a collapsed AE `Advanced Ghosts` topic for adaptive sampling strength, footprint bias, footprint clamp, and max adaptive pair grid
- advanced ghost controls default to neutral auto-like behavior and only affect the sharp cleanup internals when adjusted
- shared runtime ghost cache keys now include the advanced ghost controls so AE invalidation stays correct across expert tweaks

### Slice 21
- added Jacobian-aware local density compensation on top of the footprint-aware splat path
- `SharpAdaptive` now scales per-sample ghost contribution against a per-pair reference footprint area instead of relying only on one pair-wide area boost
- CUDA ghost rendering now mirrors the same local density compensation term so preview/final paths stay aligned
- smoke coverage now exercises the density compensation helper and verifies planned reference footprint areas

### Slice 22
- added projected pupil-cell rasterization as an automatic path for large or highly warped sharp-cleanup ghost pairs
- the CPU renderer now rasterizes projected pupil quads instead of depositing only point splats for those pairs
- smoke coverage now exercises the cell-rasterization selection heuristic alongside the earlier pair-planning helpers

### Slice 23
- added CUDA projected pupil-cell rasterization so sharp-cleanup cell pairs stay on the GPU instead of forcing a CPU fallback
- CUDA ghost dispatch now splits each adaptive grid bucket into splat pairs and cell-rasterized pairs, launching the matching kernel for each subset
- the CPU renderer is now a real fallback path again; CUDA can cover both adaptive splats and projected pupil cells
- smoke coverage now forces a CUDA launch through a cell-rasterized pair when a CUDA device is available

### Slice 24
- added Advanced Ghost controls for `Cell Coverage` and `Cell Edge Inset` so truncated projected-cell ghosts can be widened or made more edge-safe from the AE UI
- projected pupil cells now trace inset corners instead of only exact cell corners, which keeps more edge cells alive near the aperture boundary
- CPU and CUDA cell rasterization now apply the same projected-quad coverage bias before rasterization so full-flare coverage can be tuned without falling back to blur

### Slice 25
- fixed projected-cell color loss by tracing channel/spectral-sample-specific quads instead of reusing the green quad geometry for all channels
- added an `Advanced Ghosts` `Projected Cells` mode selector so the projected-cell path can run in `Auto`, `Off`, or `Force`
- CPU and CUDA cell paths now preserve red/blue fringe placement while still supporting the coverage and edge-inset controls

### Slice 26
- replaced the ambiguous `Projected Cells` checkbox semantics with an explicit `Auto` / `Off` / `Force` mode end to end
- ghost pair planning now honors `Off` by disabling projected cells completely and honors `Force` by using projected cells for every sharp-cleanup pair
- runtime cache keys, AE parameter checkout, and smoke coverage now all track the mode enum so toggling the control changes both output and render cost

### Slice 27
- stopped shrinking every projected cell corner by default when `Cell Edge Inset` is non-zero
- projected-cell tracing now uses exact cell corners whenever they are valid, and only falls back to inset corners at aperture-risk boundaries
- this targets the warped grid/gap lattice directly while keeping the edge-safety behavior for boundary cells

### Slice 28
- replaced flat-shaded projected-cell deposition with corner-weighted interpolation on both CPU and CUDA
- projected cells now reuse the traced corner throughput as a smooth density field, which breaks up the visible cell lattice without forcing a much higher `ray_grid`
- total cell energy stays normalized to the interpolated corner average, so the fix targets the grid artifact rather than hiding it with blur

### Slice 29
- moved the projected-cell mode control to the top of `Flare Settings` and relabeled it to `Adaptive Sampling`
- simplified the projected-cell UI to `Disabled` / `Enabled`, with `Disabled` as the new default and old `Auto` removed from the exposed popup
- renamed the Advanced Ghosts scalar control to `Adaptive Strength` so the UI no longer has two unrelated `Adaptive Sampling` controls

### Slice 30
- wired the existing AE `Mask Layer` selector into the real SmartFX render path instead of leaving it UI-only
- source extraction now treats the selected AE layer as a comp-space detection mask using visible content (`max(R,G,B) * alpha`), not alpha alone
- flare/haze/starburst generation now stays full-frame after source selection; the selected mask only decides which sources survive
- AE bridge alpha now expands from rendered RGB energy so masked source isolation does not clip flare lobes outside the source matte
- SmartFX pre-render/render now carries the checked-out mask layer rect through to rendering so smaller selected layers align correctly in comp space

### Slice 31
- added a top-level `Adaptive Quality` control to scale adaptive per-pair ray grids without changing the base `Ray Grid`
- adaptive pair planning now uses quality-scaled min/base/max buckets so complex lenses can be bounded quickly while keeping the reconstruction path active
- shared frame render caches now own a persistent CUDA ghost buffer cache, with the previous thread-local CUDA cache retained only as a no-cache fallback path
- smoke coverage now verifies adaptive quality planning and AE parameter/runtime propagation

Verification:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build -C Debug --output-on-failure`
- `cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON`
- `cmake --build build-ae --config Debug`

## Next slices
- tighten Smart PreRender invalidation / host-cache behavior across parameter changes
