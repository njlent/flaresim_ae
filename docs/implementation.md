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

Verification:
- `cmake -S . -B build`
- `cmake --build build`
- `ctest --test-dir build -C Debug --output-on-failure`
- `cmake -S . -B build-ae -DFLARESIM_AE_ENABLE_AE_PLUGIN=ON`
- `cmake --build build-ae --config Debug`

## Next slices
- per-instance/persistent CUDA allocation ownership in AE sequence or compute-cache state
- tighten Smart PreRender invalidation / host-cache behavior across parameter changes
