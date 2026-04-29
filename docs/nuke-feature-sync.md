# Nuke Feature Sync

Source checked: `E:/projects/ae/flaresim_nuke` at `0e28ead` (`LocalStarlight/flaresim_nuke`).

User link checked first: NVIDIA AI Decoded ray reconstruction article. That article is conceptual DLSS-RR background, not a Nuke source drop. The actionable upstream for this AE port is the Nuke FlareSim repo above.

## Gap Tracker

| Feature | Nuke upstream | AE status | Notes |
| --- | --- | --- | --- |
| Source Cap | `source_cap` knob clamps per-source luminance before flare contribution. | Ported | Added AE param, CPU extraction, CUDA extraction, docs, regression coverage. |
| Preview Mode | Overrides ray grid, max sources, downsample, spectral samples; compensates flare gain for downsample. | Ported | Added AE params and applies preview overrides before render-state handoff. |
| Auto Seed | Stratified jitter seed can follow frame number. | Ported | Added AE checkbox; smart/legacy render resolve seed from current frame for Stratified jitter. |
| Per-pair controls | Nuke dynamic Pairs tab with refresh/select/deselect. | Partial | AE now exposes `Pair Start` / `Pair Count` to isolate active pair ranges. Exact per-lens dynamic checkboxes remain a custom-UI task. |
| Source preview/output | Nuke `source.rgb` and Sources Only mode. | Already covered | AE `View = Sources` / `Diagnostics`. |
| Offscreen source support | Flares OFX/marketing docs advertise automatic positioning; no open Nuke offscreen knob found. | Ported | Added one manual source with normalized frame coordinates that can sit outside `0..1`, plus intensity/color controls. |
| Max Sources `0` | `0` means unlimited. | Ported | CPU already matched; GPU path now compacts detected candidates and honors unlimited mode. |
| Mask input | Nuke second input alpha mask. | Already covered | AE `Mask Layer`; full-comp detection mask. |
| Pupil jitter | Off, Stratified, Halton. | Already covered | AE has `Pupil Jitter` and `Jitter Seed`. |
| Expanded Spectrum | Flares OFX docs describe `Expanded Spectrum` for stronger dispersion; open Nuke exposes 3/5/7/9/11. | Ported | AE popup now exposes 3/5/7/9/11/15/21/31. |
| Spectral jitter | Flares OFX docs call out dispersion artifacts under low wavelength counts; no open Nuke knob found. | Ported | AE now has `Spectral Jitter` / `Spectral Seed`; CPU and CUDA share jittered wavelength tables. |
| Haze/starburst | Haze + FFT aperture starburst. | Already covered | AE has haze/starburst controls. |

## Implementation Log

- 2026-04-29: Ported Source Cap from Nuke. CPU path clamps source luminance during extraction; CUDA path clamps source candidates before threshold rejection. Added docs and smoke-test assertions.
- 2026-04-29: Ported Auto Seed. AE now has the Nuke-style checkbox and derives Stratified jitter seed from the current frame.
- 2026-04-29: Ported Preview Mode. AE preview controls override render quality and apply the Nuke downsample brightness compensation.
- 2026-04-29: Fixed AE GPU source handling so `Max Sources = 0` means unlimited instead of zero sources.
- 2026-04-29: Added AE pair-window controls (`Pair Start`, `Pair Count`) as an AE-compatible substitute for Nuke's dynamic per-pair checkbox tab.
- 2026-04-29: Added Expanded Spectrum options by extending AE spectral sample popups to 15/21/31.
- 2026-04-29: Added Spectral Jitter. CPU fallback now honors expanded spectral sample counts instead of tracing only RGB, and CUDA hashes/uploads jittered spectral tables.
- 2026-04-29: Added manual/offscreen source support. AE now appends a manual source after detected-source limiting so artists can place flares outside the comp frame.
