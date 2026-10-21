//! Runs the FlareSim EffectCraft plug-in the way EffectCraft 0.13 does: wasmi 2 with fuel
//! metering, a 1 GiB memory cap, a 50M-instruction budget for loading and
//! 50M + 20k per pixel for each frame (crates/plugin/src/wasm.rs). Reports the fuel each
//! step used so budget overruns show up here before they do in the app.
//!
//! usage: flaresim-ec-host <plugin.wasm> [--size WxH] [--time S] [--frames N]
//!                         [--set id=value]... [--out frame.png] [--manifest out.json]
//!                         [--headroom K]
//!
//! Frames run on EffectCraft's exact budget; --headroom K allows K times as much, to measure
//! what an over-budget frame would need.
//!
//! The input frame is black with three small HDR highlights (values 40, 20, 12), opaque,
//! premultiplied.
//! A popup value may be its 0-based index or its option label.

use std::time::Instant;

use wasmi::{Config, Engine, Linker, Module, Store, StoreLimits, StoreLimitsBuilder, TypedFunc};

const FUEL_BASE: u64 = 50_000_000;
const FUEL_PER_PIXEL: u64 = 20_000;
const MAX_MEMORY: usize = 1 << 30;

type RenderFn = TypedFunc<(i32, i32, i32, i32, i32, f64, f64), i32>;

struct Args {
    wasm: String,
    width: u32,
    height: u32,
    time: f64,
    frames: u32,
    sets: Vec<(String, String)>,
    out: Option<String>,
    manifest_out: Option<String>,
    headroom: u64,
}

fn parse_args() -> Result<Args, String> {
    let mut it = std::env::args().skip(1);
    let mut args = Args { wasm: String::new(), width: 640, height: 360, time: 0.0, frames: 1, sets: vec![], out: None, manifest_out: None, headroom: 1 };
    while let Some(a) = it.next() {
        let mut value = |name: &str| it.next().ok_or(format!("{name} needs a value"));
        match a.as_str() {
            "--size" => {
                let v = value("--size")?;
                let (w, h) = v.split_once('x').ok_or("--size WxH")?;
                args.width = w.parse().map_err(|_| "bad width")?;
                args.height = h.parse().map_err(|_| "bad height")?;
            }
            "--time" => args.time = value("--time")?.parse().map_err(|_| "bad time")?,
            "--frames" => args.frames = value("--frames")?.parse().map_err(|_| "bad frames")?,
            "--set" => {
                let v = value("--set")?;
                let (k, val) = v.split_once('=').ok_or("--set id=value")?;
                args.sets.push((k.to_string(), val.to_string()));
            }
            "--out" => args.out = Some(value("--out")?),
            "--manifest" => args.manifest_out = Some(value("--manifest")?),
            "--headroom" => args.headroom = value("--headroom")?.parse().map_err(|_| "bad headroom")?,
            _ if args.wasm.is_empty() => args.wasm = a,
            _ => return Err(format!("unexpected argument `{a}`")),
        }
    }
    if args.wasm.is_empty() {
        return Err("usage: flaresim-ec-host <plugin.wasm> [--size WxH] [--time S] [--frames N] [--set id=value]... [--out f.png] [--manifest f.json] [--headroom K]".into());
    }
    Ok(args)
}

fn fuel_used(store: &Store<StoreLimits>, budget: u64) -> u64 {
    budget - store.get_fuel().unwrap_or(0)
}

fn flat_defaults(manifest: &serde_json::Value, sets: &[(String, String)]) -> Result<Vec<f64>, String> {
    let params = manifest["params"].as_array().ok_or("manifest has no params")?;
    for (id, _) in sets {
        if !params.iter().any(|p| p["id"] == id.as_str()) {
            return Err(format!("no parameter `{id}`"));
        }
    }
    let mut flat = vec![];
    for p in params {
        let id = p["id"].as_str().unwrap_or_default();
        let set = sets.iter().rev().find(|(k, _)| k == id).map(|(_, v)| v.as_str());
        match p["type"].as_str().unwrap_or_default() {
            "slider" | "angle" => {
                let d = p["default"].as_f64().unwrap_or(0.0);
                flat.push(match set {
                    Some(v) => v.parse().map_err(|_| format!("`{id}` needs a number"))?,
                    None => d,
                });
            }
            "checkbox" => {
                let d = p["default"].as_bool().unwrap_or(false);
                let on = match set {
                    Some(v) => matches!(v, "1" | "true" | "on"),
                    None => d,
                };
                flat.push(if on { 1.0 } else { 0.0 });
            }
            "popup" => {
                let options = p["options"].as_array().cloned().unwrap_or_default();
                let index = match set {
                    Some(v) => match v.parse::<usize>() {
                        Ok(i) => i,
                        Err(_) => options.iter().position(|o| o.as_str() == Some(v)).ok_or(format!("`{id}` has no option `{v}`"))?,
                    },
                    None => p["default"].as_u64().unwrap_or(0) as usize,
                };
                flat.push(index as f64);
            }
            "point" => {
                let d = &p["default"];
                flat.push(d[0].as_f64().unwrap_or(0.5));
                flat.push(d[1].as_f64().unwrap_or(0.5));
            }
            "color" => {
                for c in 0..4 {
                    flat.push(p["default"][c].as_f64().unwrap_or(1.0));
                }
            }
            other => return Err(format!("unknown parameter type `{other}`")),
        }
    }
    Ok(flat)
}

fn test_frame(width: u32, height: u32) -> Vec<[f32; 4]> {
    // (x, y, radius as a fraction of the shorter side, value)
    let spots = [(0.30, 0.35, 0.008, 40.0), (0.72, 0.62, 0.005, 20.0), (0.55, 0.25, 0.003, 12.0)];
    let mut px = vec![[0.0, 0.0, 0.0, 1.0]; (width * height) as usize];
    let r_scale = width.min(height) as f64;
    for y in 0..height {
        for x in 0..width {
            let p = &mut px[(y * width + x) as usize];
            for &(sx, sy, sr, v) in &spots {
                let dx = x as f64 - sx * width as f64;
                let dy = y as f64 - sy * height as f64;
                if (dx * dx + dy * dy).sqrt() < sr * r_scale + 1.0 {
                    *p = [v, v * 0.95, v * 0.85, 1.0];
                }
            }
        }
    }
    px
}

fn write_png(path: &str, width: u32, height: u32, pixels: &[[f32; 4]]) -> Result<(), String> {
    let file = std::fs::File::create(path).map_err(|e| e.to_string())?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(file), width, height);
    enc.set_color(png::ColorType::Rgb);
    enc.set_depth(png::BitDepth::Eight);
    let mut writer = enc.write_header().map_err(|e| e.to_string())?;
    let data: Vec<u8> = pixels
        .iter()
        .flat_map(|p| [p[0], p[1], p[2]])
        .map(|v| (v.clamp(0.0, 1.0).powf(1.0 / 2.2) * 255.0 + 0.5) as u8)
        .collect();
    writer.write_image_data(&data).map_err(|e| e.to_string())
}

fn run() -> Result<(), String> {
    let args = parse_args()?;
    let bytes = std::fs::read(&args.wasm).map_err(|e| format!("{}: {e}", args.wasm))?;

    let mut config = Config::default();
    config.consume_fuel(true);
    let engine = Engine::new(&config);
    let module = Module::new(&engine, &bytes[..]).map_err(|e| format!("not a valid module: {e}"))?;
    if let Some(i) = module.imports().next() {
        return Err(format!("module imports `{}.{}` (EffectCraft refuses imports)", i.module(), i.name()));
    }

    let mut store = Store::new(&engine, StoreLimitsBuilder::new().memory_size(MAX_MEMORY).instances(1).build());
    store.limiter(|l| l);
    // Loading runs on EffectCraft's fixed budget; measure with a large one so an overrun
    // reports how much it would need.
    let load_budget = u64::MAX / 4;
    store.set_fuel(load_budget).map_err(|e| e.to_string())?;
    let t0 = Instant::now();
    let instance = Linker::<StoreLimits>::new(&engine).instantiate_and_start(&mut store, &module).map_err(|e| format!("instantiation failed: {e}"))?;
    let memory = instance.get_memory(&store, "memory").ok_or("no `memory` export")?;
    let api = instance.get_typed_func::<(), i32>(&store, "ec_api_version").map_err(|e| e.to_string())?.call(&mut store, ()).map_err(|e| e.to_string())?;
    let ptr = instance.get_typed_func::<(), i32>(&store, "ec_manifest_ptr").map_err(|e| e.to_string())?.call(&mut store, ()).map_err(|e| e.to_string())?;
    let len = instance.get_typed_func::<(), i32>(&store, "ec_manifest_len").map_err(|e| e.to_string())?.call(&mut store, ()).map_err(|e| e.to_string())?;
    let load_fuel = fuel_used(&store, load_budget);
    let mut text = vec![0u8; len as usize];
    memory.read(&store, ptr as u32 as usize, &mut text).map_err(|e| e.to_string())?;
    let manifest: serde_json::Value = serde_json::from_slice(&text).map_err(|e| format!("bad manifest JSON: {e}"))?;
    println!(
        "load: api {api}, manifest {len} bytes, {} params, fuel {load_fuel} ({:.0}% of the {FUEL_BASE} budget), {:.2}s",
        manifest["params"].as_array().map_or(0, |p| p.len()),
        load_fuel as f64 * 100.0 / FUEL_BASE as f64,
        t0.elapsed().as_secs_f64()
    );
    if load_fuel > FUEL_BASE {
        println!("load: OVER BUDGET, EffectCraft would refuse this module");
    }
    if let Some(path) = &args.manifest_out {
        std::fs::write(path, &text).map_err(|e| e.to_string())?;
    }

    let flat = flat_defaults(&manifest, &args.sets)?;
    let alloc = instance.get_typed_func::<i32, i32>(&store, "ec_alloc").map_err(|e| e.to_string())?;
    let render: RenderFn = instance.get_typed_func(&store, "ec_render").map_err(|e| e.to_string())?;
    // FlareSim's own diagnostics export (not part of the plug-in API).
    let budget_report = instance.get_typed_func::<(), i32>(&store, "flaresim_ec_budget_report").ok();

    let (w, h) = (args.width, args.height);
    let n = (w * h) as usize;
    let input = test_frame(w, h);
    let budget = FUEL_BASE + FUEL_PER_PIXEL * n as u64;
    let mut output = input.clone();
    for frame in 0..args.frames {
        let time = args.time + frame as f64 / 24.0;
        let params_bytes = flat.len() * 8;
        let total = params_bytes + n * 16;
        let limit = budget.saturating_mul(args.headroom.max(1));
        store.set_fuel(limit).map_err(|e| e.to_string())?;
        let base = alloc.call(&mut store, total as i32).map_err(|e| e.to_string())? as u32 as usize;
        let mut buf = Vec::with_capacity(total);
        for v in &flat {
            buf.extend_from_slice(&v.to_le_bytes());
        }
        for p in &input {
            for c in p {
                buf.extend_from_slice(&c.to_le_bytes());
            }
        }
        memory.write(&mut store, base, &buf).map_err(|e| e.to_string())?;
        store.set_fuel(limit).map_err(|e| e.to_string())?;
        let t = Instant::now();
        let code = render
            .call(&mut store, ((base + params_bytes) as i32, w as i32, h as i32, base as i32, flat.len() as i32, time, 1.0))
            .map_err(|e| format!("ec_render trapped after {:.2}s: {e} (EffectCraft leaves the frame unchanged)", t.elapsed().as_secs_f64()))?;
        let used = fuel_used(&store, limit);
        println!(
            "frame {frame} (t={time:.3}s, {w}x{h}): code {code}, {:.2}s, fuel {used} ({:.0}% of the {budget} budget){}, memory {} MiB",
            t.elapsed().as_secs_f64(),
            used as f64 * 100.0 / budget as f64,
            if used > budget { " OVER BUDGET" } else { "" },
            memory.data_size(&store) >> 20
        );
        if code != 0 {
            return Err(format!("ec_render returned {code}"));
        }
        if let Some(report) = &budget_report {
            store.set_fuel(FUEL_BASE).map_err(|e| e.to_string())?;
            let ptr = report.call(&mut store, ()).map_err(|e| e.to_string())? as u32 as usize;
            let mut raw = [0u8; 14 * 8];
            memory.read(&store, ptr, &mut raw).map_err(|e| e.to_string())?;
            let r: Vec<f64> = raw.chunks_exact(8).map(|b| f64::from_le_bytes(b.try_into().unwrap_or([0; 8]))).collect();
            let cleanup = |v: f64| ["Legacy Blur", "Sharp Adaptive", "Sharp + Blur"].get(v as usize).copied().unwrap_or("?");
            println!(
                "  budget: {} pairs; sources {} -> {}, cluster {} -> {}, ray grid {} -> {}, {} -> {}{}; estimate {:.2e} -> {:.2e} ({:.0}% of budget), actual/estimate {:.2}",
                r[3], r[4], r[5], r[12], r[13], r[6], r[7], cleanup(r[8]), cleanup(r[9]),
                if r[10] != 0.0 { " (reduced)" } else { "" },
                r[1], r[2], r[2] * 100.0 / r[0], used as f64 / r[2].max(1.0)
            );
        }
        memory.read(&store, base + params_bytes, &mut buf[params_bytes..]).map_err(|e| e.to_string())?;
        for (p, b) in output.iter_mut().zip(buf[params_bytes..].chunks_exact(16)) {
            for (c, v) in p.iter_mut().zip(b.chunks_exact(4)) {
                *c = f32::from_le_bytes([v[0], v[1], v[2], v[3]]);
            }
        }
    }

    let changed = input.iter().zip(&output).filter(|(a, b)| a != b).count();
    let peak = output.iter().map(|p| p[0].max(p[1]).max(p[2])).fold(0.0f32, f32::max);
    println!("output: {changed} of {n} pixels changed, peak {peak:.3}");
    if let Some(path) = &args.out {
        write_png(path, w, h, &output)?;
        println!("wrote {path}");
    }
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("flaresim-ec-host: {e}");
        std::process::exit(1);
    }
}
