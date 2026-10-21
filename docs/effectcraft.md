# FlareSim for EffectCraft

[EffectCraft](https://github.com/storytold/effectcraft) can't load After Effects `.aex` plug-ins.
Its third-party effects are WebAssembly modules (plug-in API v1). The `flaresim_ec` build compiles
the same C++ core (lens tracing, source extraction, ghosts, haze, starburst, compositing) into one
such module: `flaresim_ec.wasm`.

Tested with EffectCraft 0.13.0 on Windows (desktop app and `effectcraft-cli`).

## Install

Either:

- copy `flaresim_ec.wasm` into `%APPDATA%\EffectCraft\Plug-ins\` (macOS:
  `~/Library/Application Support/EffectCraft/Plug-ins/`, Linux: `~/.config/effectcraft/Plug-ins/`)
  and restart EffectCraft, or
- use **Effect ▸ Load Effect Plug-in…** and pick the file.

The effect is listed in Effects & Presets as **FlareSim EC** in the **FlareSim** category
(id `net.njlent.flaresim.ec`). Headless:

```sh
effectcraft-cli run effect.plugins.load '{"path": "flaresim_ec.wasm"}' \
    effect.apply '{"effect": "net.njlent.flaresim.ec"}' ...
```

The CLI doesn't read the `Plug-ins` folder, so load the plug-in in the same `run` as the work that
uses it. The web build of EffectCraft can't load WebAssembly plug-ins.

Typical use is the same as in After Effects: put FlareSim on the footage layer, or on an adjustment
layer above it, in a 32 bpc project so highlights can go above 1.0. The default Threshold is 8, so
8 bpc footage won't produce sources unless Threshold is lowered or Manual Source is used.

## Build

Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) (tested with wasi-sdk 34),
CMake 3.20+, Ninja and Python 3.

```sh
cmake -S . -B build-ec -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWASI_SDK_PREFIX=<wasi-sdk> \
    -DCMAKE_TOOLCHAIN_FILE=<wasi-sdk>/share/cmake/wasi-sdk-p1.cmake
cmake --build build-ec
python scripts/check_ec_wasm.py build-ec/src/ec/flaresim_ec.wasm
```

With the WASI toolchain the top-level `CMakeLists.txt` builds only `src/ec` (no CUDA, OpenMP or
AE SDK). `scripts/check_ec_wasm.py` fails if the module imports anything, which EffectCraft refuses.

What the build does differently from the AE plug-in:

- **No imports.** wasi-libc reaches the host through WASI calls (stdio, clock); `src/ec/wasi_stubs.cpp`
  defines them inside the module, so logging is discarded and the clock reads zero.
- **Lenses compiled in.** Plug-ins can't read files, so `scripts/generate_ec_lens_bank.py` embeds every
  bundled `.lens` prescription (comments stripped, ~1.4 MB) at build time, together with the Lens
  popup's labels as ready-made JSON.
- **Single-threaded.** EffectCraft runs one module instance per render thread; each frame is traced
  on one thread inside the wasmi interpreter.

## Differences from the After Effects plug-in

| | After Effects | EffectCraft |
| --- | --- | --- |
| Ghost tracing | CUDA, CPU fallback | single-threaded interpreter |
| Lens selection | Manufacturer + per-manufacturer Lens popups | one **Lens** popup, `Manufacturer / Lens` |
| Mask Layer | yes | no (plug-ins can't read other layers) |
| Aperture Rotation | slider | angle control |
| Fit Render Budget | — | on by default, see below |
| Auto Seed (Stratified pupil jitter) | frame number | layer time in milliseconds |

Every other parameter has the AE plug-in's id, range and default. Parameter ids are saved in
EffectCraft projects, so they must not be renamed. Like the AE plug-in, the Lens popup stores an
index into the bundled lens table, so adding lenses in the middle of that table changes which lens
old projects get.

## Render budget

EffectCraft meters plug-ins: a frame may run at most 50M + 20k × pixels WebAssembly instructions
(about 41.5 billion at 1080p), and a frame that runs out is **left unchanged**: the effect silently
does nothing. The AE defaults (Ray Grid 128, Sharp Adaptive, up to 256 sources) need 10–100× that
at 1080p.

With **Fit Render Budget** on (the default), the plug-in estimates each frame's cost before tracing
it and lowers quality until the estimate is under 60% of the budget:

1. Sharp Adaptive / Sharp + Blur → Legacy Blur, unless every source fits at Ray Grid 16. Sharp
   splatting costs about the area of every ghost for every source, so at HD it almost never fits.
2. Cluster Radius, doubling up to width / 96 (20 px at 1080p): a highlight breaks into many source
   blocks, and merging them keeps its energy while tracing it once.
3. Ray Grid, down to 16.
4. Cluster Radius, up to width / 8.
5. Max Sources (the brightest are kept).
6. Ray Grid, down to 8.

When it ends on Legacy Blur it also turns Pupil Jitter from Off to Halton (deterministic, so no
flicker) and raises Ghost Blur to about half the ray spacing of the larger ghosts, so they read as
soft grain instead of a dot lattice. It never raises quality or lowers the user's own blur.

The estimate is deterministic and errs high (measured up to 1.07× low on Double Gauss, 2–3× high on
lenses whose rays vignette early), so every render of a frame makes the same choice. Turn the
checkbox off to render exactly the settings you set; frames over budget then come out unchanged.

To get more quality inside the budget, spend it where it shows: set Cluster Radius and Max Sources
so each real highlight is one source, and the governor can afford a higher Ray Grid.

Measured with tests/ec_host on the development machine (three small highlights, Double Gauss,
defaults):

| Frame | Time | Governor result |
| --- | --- | --- |
| 640×360 | 1.3 s | 3 sources, Ray Grid 28, Legacy Blur |
| 1920×1080 | 12 s | 3 sources, Ray Grid 91, Legacy Blur |
| 3840×2160 | 30 s | 3 sources, Ray Grid 128, Legacy Blur (705 MiB) |

Previews at lower resolution in EffectCraft render faster.

### Memory

EffectCraft caps a plug-in instance at 1 GiB. Up to about 2.2 megapixels the plug-in keeps a render
cache (repeat renders with only post settings changed skip tracing); above that it renders without
one; a 4K frame peaks at about 705 MiB.

## Testing

`tests/ec_host` is a small Rust program that runs the module the way EffectCraft 0.13 does (wasmi 2,
same fuel and memory limits) on a synthetic HDR frame, and prints the fuel each step used and what
the budget governor chose:

```sh
cd tests/ec_host
cargo run --release -- ../../build-ec/src/ec/flaresim_ec.wasm --size 1920x1080 --out frame.png
cargo run --release -- ../../build-ec/src/ec/flaresim_ec.wasm --set fit_budget=0 --set ray_grid=32 --headroom 8
```

`--set id=value` sets a parameter (popups take an index or an option label); `--headroom K` allows K
times the budget, to measure what an over-budget frame would need. The module exports
`flaresim_ec_budget_report` for this; it is not part of the plug-in API.
