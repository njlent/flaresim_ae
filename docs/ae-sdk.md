# AE SDK Integration Notes

## Current state
- buildable AE-facing adapter code exists in `src/ae/`
- SDK-gated `.aex` scaffold target now exists in `src/ae/CMakeLists.txt`
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

## Remaining SDK tasks
- real AE parameter registration
- Smart PreRender / Smart Render checkout flow
- output-view compositing back into AE buffers
- file/import UI for external lenses
- PiPL/resource wiring
- GPU render path wiring on Windows
