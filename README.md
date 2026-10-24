<div align="center">

 <h1> <img src="docs/icon.png" width="100px">
 <br/>
 Flaresim AE + PS</h1>

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
| Cluster Radius | `0`-`256` | `0` | Renders bright areas with fewer sources while keeping their shape. Sources whose blocks are less than this many pixels apart link into one area. Each area is then split into cells about twice the radius across, and each cell becomes one source with that cell's summed brightness. The ghosts and starburst are softened by the radius to make up for the merge. Small lights stay one source each. On a lit facade, `8` rendered 12 times faster with about 2% flare difference. Larger radii are faster and softer. In the Sources view, merged blocks show dim around the sources they became. `0` disables clustering. |
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
| Color Space | `Auto`, `Linear Rec.709 / sRGB`, `ACEScg`, `ACES2065-1`, `Linear Rec.2020`, `Linear Display P3` | `Auto` | AE, PS | Scene-linear space of the image. The flare is rendered in linear Rec.709 internally and converted back, so it looks the same in every space; the manual source colour is read in this space. `Auto` uses the document's ICC profile for 32-bit Photoshop documents (8/16-bit documents stay Rec.709 / sRGB), and the OCIO working space in After Effects projects that use OCIO colour management (Rec.709 otherwise). |
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
- The plugin holds the whole document in RAM as 32-bit float.
- `Color Space` sits at the top of the dialog, under Lens. Its `Auto` entry names what it found, for example `Auto (ACEScg, from the document profile)`. For a 32-bit ACEScg document without a profile, pick `ACEScg` there; the choice is saved with the dialog's last-used settings and in presets, so it sticks. The preview converts from the chosen space to sRGB for display, and the status line names the space when it isn't Rec.709.

### Progress and cancelling

The preview shows its progress as a strip along the bottom of the preview and a percentage in the status line. Changing a setting stops the running preview and starts a new one. To stop previews while you change several settings, untick `Preview`. Tick it again to render once with the final settings.

When you press OK, Photoshop shows its progress bar for the full render. Press Esc to cancel. The render checks for a cancel between batches of flare sources (up to 16 per render), so with only a few sources it can take a moment to stop.

### Presets

The Preset row at the top of the dialog saves the current settings, including the lens, under a name:
- Save: type a name in the Preset box and press Save (or Enter). Saving under an existing name asks before replacing it.
- Load: pick a preset from the list.
- Delete: pick it, then press Delete.

Each preset is a text file, `%APPDATA%\FlareSim\presets\<name>.flaresim`, in the same `key=value` format the scripting key uses. Copy the files to share presets or move them to another machine.

### Actions

Record the filter in an action as usual. The step stores the full settings, so it plays back the same on a machine without your presets. When the settings equal a saved preset, the step also records the preset's name, so the Actions panel shows which preset it used. Turn on the step's dialog toggle to adjust the settings each time the action runs.

### Scripting

`scripts/install_ps_plugin.ps1` also installs `File > Scripts > FlareSim Preset...`, which applies a preset picked from a list, and `flaresim.jsxinc`, a helper library for your own scripts:

```js
//@include "flaresim.jsxinc"
flareSim("Golden hour");                                     // a saved preset
flareSim("Golden hour", {flare_gain: 2500});                 // the preset with one value changed
flareSim(null, {lens: "double-gauss", view: "Flare Only"});  // defaults plus these values
flareSim("Golden hour", null, true);                         // open the dialog with the preset
flareSimListPresets();                                       // ["Golden hour", ...]
```

Without the library, call the filter's event `stringIDToTypeID("flareSimPS")` directly. It takes two optional text keys:
- `FsPr`: a preset name. If no preset has that name and there's no `FsSt` either, the call throws an error.
- `FsSt`: settings as `flaresim-ps 1` followed by `key=value` lines. These go on top of the preset.

The keys (`lens`, `flare_gain`, `ray_grid`, `view`, ...) are the ones in a preset file and in `src/ps/ps_settings.cpp`. Popups take their label text, and switches take `on`/`off`. Keys you leave out come from the preset, or from the defaults when there is no preset, never from the last dialog session, so a script renders the same every time.

```js
var desc = new ActionDescriptor();
desc.putString(charIDToTypeID("FsPr"), "Golden hour");
desc.putString(charIDToTypeID("FsSt"), "flaresim-ps 1\nflare_gain=2000\nview=Flare Only\n");
executeAction(stringIDToTypeID("flareSimPS"), desc, DialogModes.NO);
```

## License

Unless noted otherwise, original project material in this repository is licensed under the
custom `Flaresim AE Licence`, a modified
[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-nc-sa/4.0/)
licence with additional terms. See [LICENSE](LICENSE).

Bundled third-party material and imported upstream lens data keep their respective upstream notices/licenses and are not relicensed by this top-level notice.
