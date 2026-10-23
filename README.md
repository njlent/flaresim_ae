<div align="center">

 <h1> <img src="docs/icon.png" width="100px">
 <br/>
 Flaresim AE</h1>

 <img src="https://img.shields.io/badge/AfterEffects Addon-050059"/> 
 <img src="https://img.shields.io/badge/Photoshop Filter-001E36"/> 
 <br>
 <img src="https://custom-icon-badges.demolab.com/badge/AE 2025.5-0078D6?logo=windows11&logoColor=white"/> 
 <img src="https://custom-icon-badges.demolab.com/badge/PS 2026-0078D6?logo=windows11&logoColor=white"/> 
 <img src="https://img.shields.io/badge/CUDA-76B900?logo=nvidia&logoColor=fff"/> 


</div>
<br/>



FlareSim ported to After Effects and Photoshop.

FlareSim simulates lens flares physically. It traces rays through a real lens prescription, so the ghost reflections move with the light source, split by wavelength and take the shape of the aperture. There are no lookup textures and no painted flare elements.

Both plugins share one CUDA renderer, with a CPU/OpenMP fallback when no CUDA device is available. The same settings give the same render in either host.

> [!IMPORTANT]
> This is mostly for personal exploration and NOT production tested or stable.

## Supported versions

| Host | Plugin | Tested with | Bit depths |
| --- | --- | --- | --- |
| After Effects | `FlareSimAE.aex` effect | AE 2025 (25.5), Windows | 8, 16, 32 bpc |
| Photoshop | `FlareSimPS.8bf` filter | PS 2026 (27.6), Windows | 8, 16, 32 bpc RGB |

Windows only for now. CUDA runs on NVIDIA GPUs; other machines use the CPU path, which is a lot slower.

## Building and installing

Build steps for both plugins are in [docs/build.md](docs/build.md). Each needs its Adobe SDK, which isn't in this repo. Download the After Effects SDK and the Photoshop Plug-In and Connection SDK from the [Adobe Developer Console](https://developer.adobe.com/console) (Downloads).

- After Effects: copy `FlareSimAE.aex` into `<After Effects>\Support Files\Plug-ins\Effects\`.
- Photoshop: run `scripts/install_ps_plugin.ps1`. It asks for admin rights and copies the filter, `cudart64_12.dll` and `assets/lenses` into `<Photoshop>\Plug-ins\FlareSim\`. Restart Photoshop afterwards.

## Plugin settings

Both hosts show the same controls, grouped in render order: camera setup, aperture shaping, flare/source extraction, then reconstruction and post work.
The defaults below match the current build. A few controls only exist in one host; see [View and host-specific controls](#view-and-host-specific-controls).

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
| Anamorphic Squeeze | slider `1`-`4`; manual values allowed above slider max | `1` | Horizontally desqueezes ghost projection. `2` approximates a 2x anamorphic look; this does not add proprietary cylindrical lens prescriptions. |

### Aperture

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Aperture Blades | `0`-`16` | `0` | `0` keeps the lens/aperture path effectively round. Higher values force polygonal aperture shaping. |
| Aperture Rotation | slider `-180` to `180` deg | `0` | Rotates polygonal aperture and starburst orientation. |

### Flare Settings

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Adaptive Sampling | `Disabled`, `Enabled` | `Disabled` | Enables projected-cell reconstruction. Faster than brute-force grid increases, but more stylized/experimental. |
| Adaptive Quality | slider `0.25`-`2` | `1` | Scales adaptive per-pair ray grids. Lower values bound complex-lens render spikes; `1` keeps the `Ray Grid` baseline. |
| Flare Gain | slider `0`-`5000`; manual values allowed above slider max | `4000` | Global flare brightness multiplier. |
| Sky Brightness | slider `0`-`4`; manual higher values allowed | `1` | Scales sub-threshold scene values before source extraction. |
| Threshold | slider `0`-`64` | `8` | Bright-pixel cutoff for source detection. Lower = more sources/noise. |
| Source Cap | slider `0`-`64`; manual higher values allowed | `0` | Clamps individual source luminance before thresholding and flare contribution. `0` disables the cap. |
| Manual Source | `Off`, `On` | `Off` | Adds one artist-placed source in addition to detected HDR sources. |
| Source X / Source Y | slider `-1`-`2`; manual values allowed outside slider range | `0.5`, `0.5` | Manual source position in normalized frame space. `0..1` is on-frame; outside values create offscreen sources. |
| Source Intensity | slider `0`-`256`; manual higher values allowed | `16` | Manual source brightness. |
| Source Red / Green / Blue | slider `0`-`4`; manual higher values allowed | `1`, `1`, `1` | Manual source color multipliers. |
| Ray Grid | `1`-`2048` | `128` | Base ray density for ghost tracing. Main quality/perf lever. |
| Downsample | `1`-`12` | `2` | Source extraction block size. Higher = faster, fewer detected highlights. |
| Max Sources | `0`-`2048` | `256` | `0` means unlimited. Otherwise keeps only the brightest detected sources. |
| Cluster Radius | `0`-`256` | `0` | Merges nearby detected sources before ghost tracing. `0` disables clustering. |
| Preview Mode | `Off`, `On` | `Off` | Overrides quality controls below for faster interactive previews. |
| Preview Ray Grid | `1`-`128` | `16` | Ray grid used while Preview Mode is on. |
| Preview Max Sources | `0`-`512` | `100` | Source cap used while Preview Mode is on. |
| Preview Downsample | `1`-`24` | `8` | Source extraction stride used while Preview Mode is on. |
| Preview Spectral Samples | `3`, `5`, `7`, `9`, `11`, `15`, `21`, `31` | `3` | Spectral sample count used while Preview Mode is on. |

### Post-processing

| Setting | Values / range | Default | Notes |
| --- | --- | --- | --- |
| Ghost Blur | slider `0`-`0.05` | `0.0005` | Blur radius as a fraction of image diagonal. |
| Ghost Blur Passes | `0`-`8` | `3` | Extra blur passes for the ghost layer. |
| Haze Gain | slider `0`-`10` | `0` | Enables soft glow from extracted sources. `0` disables haze. |
| Haze Radius | slider `0`-`0.5` | `0.15` | Haze blur radius as a fraction of image diagonal. |
| Haze Blur Passes | `0`-`8` | `3` | Additional haze blur passes. |
| Starburst Gain | slider `0`-`10` | `0` | Enables diffraction-style starburst. `0` disables starburst. |
| Starburst Scale | slider `0`-`0.5` | `0.15` | Starburst size. Shape follows aperture blades/rotation. |
| Spectral Samples | `3`, `5`, `7`, `9`, `11`, `15`, `21`, `31` | `3` | Wavelength samples per ghost trace. Higher = better dispersion/color, slower render. |
| Spectral Jitter | `Off`, `Stratified`, `Halton` | `Off` | Jitters expanded-spectrum wavelength bins to reduce banding in high-dispersion renders. Applies when spectral samples are above 3. |
| Spectral Seed | `0`-`1000000` | `0` | Seed for spectral jitter. |
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
| Pair Start | `0`-`512` | `0` | Skips this many active ghost pairs after physical filtering. |
| Pair Count | `0`-`512` | `0` | Limits active ghost pairs after `Pair Start`. `0` keeps all remaining pairs. |
| Surface Start | `0`-`512` | `0` | First lens surface receiving the Surface Gain art override. |
| Surface Count | `0`-`512` | `0` | Number of surfaces to affect. `0` affects all surfaces from Surface Start onward. |
| Surface Gain | slider `0`-`4`; manual higher values allowed | `1` | Multiplies selected surface reflectance before ghost tracing. `0` suppresses selected reflective surfaces; values above `1` exaggerate them. |
| Pupil Jitter | `Off`, `Stratified`, `Halton` | `Off` | Entrance-pupil sampling pattern. Stratified and Halton break up regular grid dots. |
| Jitter Seed | `0`-`1000000` | `0` | Fixed seed for Stratified jitter when `Auto Seed` is off. |
| Auto Seed | `Off`, `On` | `On` | Uses the current frame as the Stratified jitter seed for animated noise variation. |
| Cell Coverage | slider `0.5`-`2.5` | `1` | Expands or contracts projected-cell coverage. Higher can fill missing lobes; too high can bloat shapes. |
| Cell Edge Inset | slider `0`-`0.45` | `0.1` | Pulls risky cell corners inward near aperture boundaries. Higher can stabilize edge cells but may introduce gaps. |

Guidance:
- grid/lattice artifacts first: try `Ghost Cleanup = Sharp Adaptive`, then raise `Ray Grid`
- complex-lens stalls: lower `Adaptive Quality` before lowering `Ray Grid`
- interactive setup: enable `Preview Mode`; brightness is compensated for preview downsample changes, not for a lower preview source cap
- over-smooth ghosts: lower `Footprint Bias` a bit before touching `Ghost Blur`
- missing projected-cell coverage: raise `Cell Coverage` slightly, then adjust `Cell Edge Inset`
- perf spikes in hard shots: keep `Adaptive Sampling` disabled, or cap `Max Pair Grid`
- pair isolation: raise `Pair Start`, then set `Pair Count = 1` to inspect one active pair at a time

### View and host-specific controls

| Setting | Values / range | Default | Hosts | Notes |
| --- | --- | --- | --- | --- |
| View | `Composite`, `Flare Only`, `Bloom Only`, `Sources`, `Diagnostics` | `Composite` | AE, PS | Output selection. In Photoshop, `Flare Only` on a duplicate layer gives you the flare on its own layer. |
| Mask Layer | AE layer selector | none | AE | Limits source detection to the chosen layer's visible content in comp space. Flare, haze and starburst still render across the full frame. |
| Auto Seed | `Off`, `On` | `On` | AE | Seeds Stratified jitter from the frame number. Photoshop has no timeline, so it always uses `Jitter Seed`. |

Mask Layer behavior:
- only affects source detection
- does not crop or matte the generated flare, haze, or starburst
- uses the selected layer's visible pixels as the detection mask: `max(R,G,B) * alpha`
- evaluated in full comp space so the chosen layer can be smaller than frame size
- practical use: isolate which tracked highlights emit flares without cutting off the flare itself


## Photoshop filter

The filter is at `Filter > FlareSim > FlareSim...`. It works on RGB documents in 8, 16 and 32 bits per channel.

Use 32-bit if you can. A 32-bit document is linear HDR, so source detection sees real scene values and the result keeps values above 1.0. In 8- and 16-bit documents every pixel tops out at 1.0, and the default `Threshold` of 8 finds nothing. Set `Threshold` just below 1 (around `0.9`-`0.98`) and raise `Flare Gain` a lot (around `40000`) to get a visible flare there. The status line under the preview reports how many image sources the current settings detect. If it finds none, it shows the brightest pixel next to the threshold.

How it behaves:
- The whole document is the camera frame. A selection limits where the flare is written. It doesn't change where sources are detected or where ghosts land.
- It works as a Smart Filter. Double-click the filter entry to reopen the dialog with its stored settings.
- On a layer with transparency, the filter premultiplies colour for the render and writes straight alpha back. With `Manual Source` on, it can draw a flare onto an empty layer.
- The preview renders at fit, half or full resolution in the background. Click or drag in it to place the manual source. Positions outside the image give off-frame sources.
- The dialog remembers the last settings you confirmed with OK. Scripted runs and actions don't change them.
- The render can't be cancelled once it starts, and the plugin holds the whole document in RAM as 32-bit float.

Scripting and actions: the filter's event is `stringIDToTypeID("flareSimPS")` with one text key `FsSt` holding `flaresim-ps 1` followed by `key=value` lines. The keys (`lens`, `flare_gain`, `ray_grid`, `view`, ...) are listed in `src/ps/ps_settings.cpp`; popups take their label text. Missing keys use defaults, never the last-used settings, so a script renders the same every time.

```js
var desc = new ActionDescriptor();
desc.putString(charIDToTypeID("FsSt"), "flaresim-ps 1\nlens=double-gauss\nflare_gain=2000\nview=Flare Only\n");
executeAction(stringIDToTypeID("flareSimPS"), desc, DialogModes.NO);
```

## License

Unless noted otherwise, original project material in this repository is licensed under the
custom `Flaresim AE Licence`, a modified
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-nc-sa/4.0/)
licence with additional terms. See [LICENSE](LICENSE).

Bundled third-party material and imported upstream lens data keep their respective upstream notices/licenses and are not relicensed by this top-level notice.
