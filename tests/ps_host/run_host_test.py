"""End-to-end test of FlareSimPS.8bf inside a real Photoshop.

Writes synthetic 8/16/32-bit TIFFs, drives Photoshop over COM to apply the
filter through its scripting event, and compares Photoshop's output with the
shared runtime rendered outside Photoshop (flaresim_ps_reference).

usage: python tests/ps_host/run_host_test.py [--build-dir build-ps] [--keep-open]
Requires the plug-in to be installed (scripts/install_ps_plugin.ps1), numpy and tifffile.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import tifffile

REPO = Path(__file__).resolve().parents[2]
WIDTH, HEIGHT = 960, 540
# Large enough that a 32-bit RGB read needs several advanceState bands.
LARGE_WIDTH, LARGE_HEIGHT = 4000, 2250
SELECTION = (120, 90, 700, 410)  # left, top, right, bottom


def settings_text(**overrides):
    values = {
        "ray_grid": "64",
        "downsample": "2",
        "max_sources": "64",
        "haze_gain": "0.5",
        "starburst_gain": "0.5",
    }
    values.update({k: str(v) for k, v in overrides.items()})
    return "flaresim-ps 1\n" + "".join(f"{k}={v}\n" for k, v in values.items())


def make_scene(hdr, width=WIDTH, height=HEIGHT):
    y = np.arange(height, dtype=np.float32)[:, None] / height
    img = np.zeros((height, width, 3), np.float32)
    img[..., 0] = 0.05 + 0.20 * (1 - y)
    img[..., 1] = 0.07 + 0.25 * (1 - y)
    img[..., 2] = 0.12 + 0.45 * (1 - y)
    yy, xx = np.mgrid[0:height, 0:width]
    sx, sy, r = width * 650 // WIDTH, height * 150 // HEIGHT, 3 * width // WIDTH
    sun = (xx - sx) ** 2 + (yy - sy) ** 2 <= r * r
    img[sun] = [300.0, 280.0, 240.0] if hdr else [1.0, 1.0, 1.0]
    return img


def reference(build_dir, work, name, image, settings):
    height, width = image.shape[:2]
    src = work / f"{name}_ref_in.f32"
    dst = work / f"{name}_ref_out.f32"
    cfg = work / f"{name}_settings.txt"
    np.ascontiguousarray(image, np.float32).tofile(src)
    cfg.write_text(settings, encoding="utf-8")
    exe = build_dir / "Release" / "flaresim_ps_reference.exe"
    subprocess.run([str(exe), str(src), str(dst), str(width), str(height), str(cfg)],
                   check=True, stdout=subprocess.DEVNULL)
    return np.fromfile(dst, np.float32).reshape(height, width, 3)


def run_photoshop(jsx_path, keep_open):
    # Windows Script Host binds late through IDispatch. PowerShell's COM
    # wrapper needs Photoshop's type library, which often fails to load.
    driver = Path(jsx_path).with_name("drive_photoshop.js")
    lines = [
        'var app = new ActiveXObject("Photoshop.Application");',
        # DoJavaScriptFile rejects some temp paths; evalFile through DoJavaScript does not.
        "var result = app.DoJavaScript(" + json.dumps(
            f"try {{ $.evalFile(new File({json.dumps(Path(jsx_path).as_posix())})); }} "
            "catch (e) { 'SCRIPT ERROR ' + e + ' line ' + e.line; }") + ");",
        "WScript.Echo(result);",
    ]
    if not keep_open:
        lines.append("if (app.Documents.Count == 0) { app.Quit(); }")
    driver.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # cscript writes in the console OEM codepage.
    proc = subprocess.run(["cscript", "//nologo", "//E:jscript", str(driver)],
                          capture_output=True, text=True, encoding="oem", errors="replace", timeout=900)
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout + proc.stderr)
    return proc.stdout


def report(label, ok, detail):
    print(f"[{'PASS' if ok else 'FAIL'}] {label}: {detail}")
    return ok


def compare(label, got, want, tol):
    diff = np.abs(got.astype(np.float64) - want.astype(np.float64)) / (1.0 + np.abs(want.astype(np.float64)))
    worst = float(diff.max())
    return report(label, worst <= tol,
                  f"max rel diff {worst:.2e} (tol {tol:.0e}), changed px {int((np.abs(got - want) > tol).any(-1).sum())}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=str(REPO / "build-ps"))
    parser.add_argument("--keep-open", action="store_true")
    args = parser.parse_args()
    build_dir = Path(args.build_dir).resolve()

    work = Path(tempfile.mkdtemp(prefix="flaresim_ps_host_"))
    hdr = make_scene(True)
    ldr = make_scene(False)
    tifffile.imwrite(work / "in32.tif", hdr, photometric="rgb")
    hdr_large = make_scene(True, LARGE_WIDTH, LARGE_HEIGHT)
    tifffile.imwrite(work / "in32_large.tif", hdr_large, photometric="rgb")
    tifffile.imwrite(work / "in16.tif", np.round(ldr * 65535).astype(np.uint16), photometric="rgb")
    tifffile.imwrite(work / "in8.tif", np.round(ldr * 255).astype(np.uint8), photometric="rgb")

    s32 = settings_text()
    sldr = settings_text(threshold=0.9, flare_gain=40000)
    slayer = settings_text(manual_source="on", source_x=0.3, source_y=0.4, source_intensity=200, threshold=1000)

    config = {
        "dir": work.as_posix(),
        "settings32": s32,
        "settingsLdr": sldr,
        "settingsLayer": slayer,
        "width": WIDTH,
        "height": HEIGHT,
        "baseDocuments": 0,
        "selection": list(SELECTION),
    }
    jsx = work / "host_test.jsx"
    jsx.write_text("var CONFIG = " + json.dumps(config) + ";\n" +
                   (Path(__file__).parent / "host_test.jsx").read_text(encoding="utf-8"), encoding="utf-8")

    # Scripted runs must never replace the user's interactive last-used settings.
    last_used = Path(os.environ["APPDATA"]) / "FlareSim" / "photoshop-last-used.txt"
    last_used_before = last_used.read_bytes() if last_used.exists() else None

    print(f"work dir: {work}")
    print(run_photoshop(jsx.as_posix(), args.keep_open).strip())

    ok = True

    # 32-bit: must match the runtime and keep over-range values.
    out32 = tifffile.imread(work / "out32.tif").astype(np.float32)[..., :3]
    ref32 = reference(build_dir, work, "flat32", hdr, s32)
    ok &= compare("32-bit flat vs reference", out32, ref32, 2e-3)
    added = out32 - hdr
    ok &= report("32-bit flare added", float(added.sum()) > 0, f"sum {float(added.sum()):.4g}")
    ok &= report("32-bit over-range kept", float(out32.max()) > 1.0,
                 f"max {float(out32.max()):.4g}, px>1 outside source {int(((out32 > 1).any(-1) & (hdr < 1).all(-1)).sum())}")

    recorded = (work / "recorded32.txt").read_text(encoding="utf-8") if (work / "recorded32.txt").exists() else ""
    ok &= report("descriptor recorded", recorded.startswith("flaresim-ps 1") and "ray_grid=64" in recorded,
                 f"{len(recorded)} chars")

    # Selection: the frame is still the whole document; only the selection changes.
    out_sel = tifffile.imread(work / "out32_selection.tif").astype(np.float32)[..., :3]
    left, top, right, bottom = SELECTION
    inside = np.zeros((HEIGHT, WIDTH), bool)
    inside[top + 1:bottom - 1, left + 1:right - 1] = True
    ok &= compare("32-bit selection interior vs full-frame reference", out_sel[inside], ref32[inside], 2e-3)
    outside = np.ones((HEIGHT, WIDTH), bool)
    outside[top - 1:bottom + 1, left - 1:right + 1] = False
    ok &= compare("32-bit selection exterior untouched", out_sel[outside], hdr[outside], 1e-6)

    out_large = tifffile.imread(work / "out32_large.tif").astype(np.float32)[..., :3]
    ref_large = reference(build_dir, work, "large32", hdr_large, s32)
    ok &= compare("32-bit 4000x2250 (multi-band) vs reference", out_large, ref_large, 2e-3)

    out_smart = tifffile.imread(work / "out32_smart.tif").astype(np.float32)[..., :3]
    ok &= compare("32-bit smart filter vs flat", out_smart, out32, 2e-3)

    # 16/8-bit: quantised, clamped to 0..1.
    ref_ldr = np.clip(reference(build_dir, work, "ldr", ldr, sldr), 0, 1)
    out16 = tifffile.imread(work / "out16.tif").astype(np.float32)[..., :3] / 65535.0
    ok &= compare("16-bit flat vs reference", out16, ref_ldr, 4e-4)
    ok &= report("16-bit flare added", float((out16 - ldr).sum()) > 0, f"sum {float((out16 - ldr).sum()):.4g}")
    out8 = tifffile.imread(work / "out8.tif").astype(np.float32)[..., :3] / 255.0
    ok &= compare("8-bit flat vs reference", out8, ref_ldr, 3.0 / 255.0)

    # Transparent layer + manual source: flare must create alpha.
    layer = tifffile.imread(work / "out32_layer.tif").astype(np.float32)
    has_alpha = layer.shape[-1] >= 4
    ok &= report("32-bit transparent layer has alpha", has_alpha, f"shape {layer.shape}")
    if has_alpha:
        alpha = layer[..., 3]
        ok &= report("manual source flare on empty layer", float(alpha.max()) > 0.1 and float(alpha.min()) < 0.5,
                     f"alpha min {float(alpha.min()):.3g} max {float(alpha.max()):.3g}, "
                     f"coverage {float((alpha > 0.01).mean()) * 100:.1f}%")

    last_used_after = last_used.read_bytes() if last_used.exists() else None
    ok &= report("last-used settings untouched by scripted runs", last_used_after == last_used_before,
                 "unchanged" if last_used_after == last_used_before else f"{last_used} was modified")

    print("RESULT:", "PASS" if ok else "FAIL")
    if ok:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print(f"kept {work} for inspection")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
