# AE SDK Integration Notes

## Current state
- buildable AE-facing adapter code exists in `src/ae/`
- SDK-gated `.aex` target now builds in `src/ae/CMakeLists.txt`
- PiPL/resource generation is wired through the AE SDK PiPL tool
- `PluginDataEntryFunction2` now registers effect metadata for host discovery
- Smart Render and legacy render now drive the shared frame bridge
- AE param set now includes top-level Camera, Aperture, Flare Settings, and Post-processing sections
- output `View` popup is wired through the shared runtime/compositor
- shared runtime/core are buildable and tested locally without the Adobe SDK

## Intended wrapper shape
- SDK entry point translates AE params -> `AeParameterState`
- `AeParameterState` -> `FrameRenderSettings`
- built-in lens popup uses `src/ae/builtin_lenses.*`
- wrapper resolves selected `.lens` path, loads `LensSystem`, then calls `render_frame()`

## Expected AE setup
- SmartFX
- `PF_OutFlag_DEEP_COLOR_AWARE`
- `PF_OutFlag2_SUPPORTS_SMART_RENDER`
- `PF_OutFlag2_FLOAT_COLOR_AWARE`
- 8/16/32-bpc support through one float runtime
- PiPL effect metadata + stable match name

Bit-depth policy in code:
- 8-bpc pack/unpack helpers clamp only at final 8-bit conversion
- 16-bpc pack/unpack helpers clamp only at final 16-bit conversion
- 32-bpc helpers preserve values above `1.0`
- `src/ae/frame_bridge.*` now runs one float render/composite path for all three host depths
- bridge preserves input alpha and only changes RGB payload
- bundled lens resolution no longer has to start from the repo root; `src/ae/asset_root.*` can discover the asset root by walking upward from an anchor path
- param ids, popup ordering, and popup-index -> runtime-state mapping now live in `src/ae/param_schema.*`
- plugin target also gets `FLARESIM_REPO_ROOT` as a fallback asset-root anchor during local builds

## Build result
- local SDK build currently emits `build-ae/src/ae/Debug/FlareSimAE.aex`
- binary now includes `.rsrc` data from generated PiPL wiring
- export table still exposes `EffectMain`, as expected for an AE effect

## Host-test status
- After Effects 2025 is installed locally
- JSX smoke scripts can be launched from `AfterFX.exe`
- the installed plug-in at `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects\FlareSimAE.aex` matches the local build output
- on March 30, 2026, host-side JSX validation confirmed:
  - effect add by match name
  - property enumeration including the `View` popup
  - `View` popup value readback for values `1..5`
  - rendered outputs differ between Composite / Flare Only / Bloom Only / Sources / Diagnostics in a clean AE project

## Remaining SDK tasks
- tighten Smart PreRender state handling beyond current minimal checkout/result-rect flow
- file/import UI for external lenses
- GPU render path wiring on Windows
