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

A CUDA-accelerated Aftereffects plugin for physically-based lens flare simulation. FlareSim traces rays through a real lens prescription to produce ghost reflections that respond correctly to source position, wavelength, and aperture shape — without look-up textures or artist-painted elements.

> [!IMPORTANT]
> This is mostly for personal exploration and NOT production tested or stable.

## Supported versions: 

AfterEffects 25.5 Windows + CUDA first.

## Plugin Settings

The UI is grouped to match the render pipeline: camera setup, aperture shaping, flare/source extraction, then reconstruction and post work.
Defaults below match the current plugin build.

### Lens

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Manufacturer | Bundled manufacturer popup | default bundle selection | Chooses a bundled preset family. |
| Lens | Manufacturer-specific popup | `double-gauss` | Chooses the actual lens prescription used for ray tracing. |

Notes:
- lens options come from the bundled `space55` and `flaresim_nuke` preset manifests
- the hidden `Legacy Lens` popup still exists for old saved comps; new work should use `Manufacturer` + `Lens`

### Camera

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Use Sensor Size | `Off`, `On` | `Off` | `Off`: use FOV controls. `On`: derive FOV from sensor size + focal length. |
| Sensor Preset | `Custom`, `Full Frame`, `Super 35`, `APS-C Canon`, `APS-C Nikon/Sony`, `Micro Four Thirds` | `Custom` | Preset sizes override width/height when not `Custom`. |
| FOV H (deg) | slider `1`-`180`; manual higher precision | `40` | Horizontal field of view when `Use Sensor Size` is off. |
| Auto FOV V | `Off`, `On` | `On` | When on, vertical FOV follows comp aspect ratio. |
| FOV V (deg) | slider `1`-`180` | `24` | Used only when `Auto FOV V` is off. |
| Sensor Width (mm) | slider `1`-`100` | `36` | Used when `Use Sensor Size` is on and preset is `Custom`. |
| Sensor Height (mm) | slider `1`-`100` | `24` | Used when `Use Sensor Size` is on and preset is `Custom`. |
| Focal Length (mm) | slider `1`-`200` | `50` | Used with sensor-size camera mode. |

### Aperture

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Aperture Blades | `0`-`16` | `0` | `0` keeps the lens/aperture path effectively round. Higher values force polygonal aperture shaping. |
| Aperture Rotation | slider `-180` to `180` deg | `0` | Rotates polygonal aperture and starburst orientation. |

### Flare Settings

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Adaptive Sampling | `Disabled`, `Enabled` | `Disabled` | Enables projected-cell reconstruction. Faster than brute-force grid increases, but more stylized/experimental. |
| Flare Gain | slider `0`-`5000`; manual values allowed above slider max | `8000` | Global flare brightness multiplier. |
| Sky Brightness | slider `0`-`4`; manual higher values allowed | `1` | Scales sub-threshold scene values before source extraction. |
| Threshold | slider `0`-`64` | `8` | Bright-pixel cutoff for source detection. Lower = more sources/noise. |
| Ray Grid | `1`-`2048` | `128` | Base ray density for ghost tracing. Main quality/perf lever. |
| Downsample | `1`-`12` | `2` | Source extraction block size. Higher = faster, fewer detected highlights. |
| Max Sources | `0`-`2048` | `256` | `0` means unlimited. Otherwise keeps only the brightest detected sources. |

### Post-processing

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Ghost Blur | slider `0`-`0.05` | `0.003` | Blur radius as a fraction of image diagonal. |
| Ghost Blur Passes | `0`-`8` | `3` | Extra blur passes for the ghost layer. |
| Haze Gain | slider `0`-`10` | `0` | Enables soft glow from extracted sources. `0` disables haze. |
| Haze Radius | slider `0`-`0.5` | `0.15` | Haze blur radius as a fraction of image diagonal. |
| Haze Blur Passes | `0`-`8` | `3` | Additional haze blur passes. |
| Starburst Gain | slider `0`-`10` | `0` | Enables diffraction-style starburst. `0` disables starburst. |
| Starburst Scale | slider `0`-`0.5` | `0.15` | Starburst size. Shape follows aperture blades/rotation. |
| Spectral Samples | `3`, `5`, `7`, `9`, `11` | `3` | Wavelength samples per ghost trace. Higher = better dispersion/color, slower render. |
| Ghost Cleanup | `Legacy Blur`, `Sharp Adaptive`, `Sharp + Blur` | `Sharp Adaptive` | Reconstruction mode for ghost artifacts. `Legacy Blur`: old soft cleanup. `Sharp Adaptive`: sharper footprint-based reconstruction. `Sharp + Blur`: sharp reconstruction plus post blur. |

Notes:
- `Ghost Blur` is only applied in `Legacy Blur` and `Sharp + Blur`
- haze and starburst are generated from extracted sources, not from the traced ghost buffer

### Advanced Ghosts

These only matter when chasing ghost artifacts/perf tradeoffs. Most shots should stay near defaults.

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Adaptive Strength | slider `0`-`2` | `0` | Strength of adaptive pair-grid scaling. Higher spends more rays on large/high-warp pairs. |
| Footprint Bias | slider `0.25`-`2` | `1` | Multiplier for adaptive splat footprint radius. Lower = sharper/riskier. Higher = smoother/softer. |
| Footprint Clamp | slider `0.5`-`4` | `1.15` | Caps adaptive footprint expansion. Prevents extremely wide splats. |
| Max Pair Grid | `0`-`512` | `0` | `0` keeps automatic limits. Non-zero hard-caps adaptive per-pair ray grids. |
| Cell Coverage | slider `0.5`-`2.5` | `1` | Expands or contracts projected-cell coverage. Higher can fill missing lobes; too high can bloat shapes. |
| Cell Edge Inset | slider `0`-`0.45` | `0.1` | Pulls risky cell corners inward near aperture boundaries. Higher can stabilize edge cells but may introduce gaps. |

Guidance:
- grid/lattice artifacts first: try `Ghost Cleanup = Sharp Adaptive`, then raise `Ray Grid`
- over-smooth ghosts: lower `Footprint Bias` a bit before touching `Ghost Blur`
- missing projected-cell coverage: raise `Cell Coverage` slightly, then adjust `Cell Edge Inset`
- perf spikes in hard shots: keep `Adaptive Sampling` disabled, or cap `Max Pair Grid`

### View + AE-only controls

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| View | `Composite`, `Flare Only`, `Bloom Only`, `Sources`, `Diagnostics` | `Composite` | Debug/output selection inside After Effects. |
| Mask Layer | AE layer selector | none | Limits source detection to the chosen layer's visible content in comp space while flare/haze/starburst still render across the full frame. |

Mask Layer behavior:
- only affects source detection
- does not crop or matte the generated flare, haze, or starburst
- uses the selected layer's visible pixels as the detection mask: `max(R,G,B) * alpha`
- evaluated in full comp space so the chosen layer can be smaller than frame size
- practical use: isolate which tracked highlights emit flares without cutting off the flare itself


## License

Unless noted otherwise, original project material in this repository is licensed under the
custom `Flaresim AE Licence`, a modified
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-nc-sa/4.0/)
licence with additional terms. See [LICENSE](LICENSE).

Bundled third-party material and imported upstream lens data keep their respective upstream notices/licenses and are not relicensed by this top-level notice.
