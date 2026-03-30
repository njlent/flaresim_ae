#!/usr/bin/env python3

from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SPACE55_DIR = REPO_ROOT / "assets" / "lenses" / "space55"
NUKE_SOURCE_DIR = Path(r"E:\projects\flaresim_nuke_lens_import\lenses\lens_files")
NUKE_DEST_DIR = REPO_ROOT / "assets" / "lenses" / "flaresim_nuke"
GENERATED_INC = REPO_ROOT / "src" / "ae" / "builtin_lenses_generated.inc"
MANIFEST_PATH = NUKE_DEST_DIR / "manifest.json"


SPACE55_PRESETS = [
    {
        "id": "arri-zeiss-master-prime-t1.3-50mm",
        "label": "ARRI/Zeiss Master Prime T1.3 50mm",
        "path": "arri-zeiss-master-prime-t1.3-50mm.lens",
    },
    {
        "id": "canon-ef-200-400-f4",
        "label": "Canon EF 200-400mm f/4",
        "path": "canon-ef-200-400-f4.lens",
    },
    {
        "id": "cooke-triplet",
        "label": "Cooke Triplet",
        "path": "cooketriplet.lens",
    },
    {
        "id": "double-gauss",
        "label": "Double Gauss",
        "path": "doublegauss.lens",
    },
    {
        "id": "test-lens",
        "label": "Test Lens",
        "path": "test.lens",
    },
]


NIKON_PREFIXES = (
    "AF-",
    "AF_",
    "AI_",
    "Medical-Nikkor",
    "Nikkor",
    "Pikaichi",
    "W-Nikkor",
    "Wide-conversion",
    "Zoom_Nikkor",
)


@dataclass(frozen=True)
class LensRecord:
    id: str
    label: str
    relative_path: str
    manufacturer: str
    manufacturer_id: str


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return slug or "lens"


def c_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
    )


def derive_manufacturer(stem: str) -> str:
    head = stem.split("_", 1)[0]
    if any(ch.isdigit() for ch in head):
        match = re.search(r"\(([^)]+)", stem)
        if match:
            nested_stem = match.group(1)
            if nested_stem and nested_stem != stem:
                return derive_manufacturer(nested_stem)

    if stem.startswith("Konica_Minolta_") or stem.startswith("Konica_") or stem.startswith("Minolta_"):
        return "Konica Minolta"
    if stem.startswith("Konina_"):
        return "Konica Minolta"
    if stem.startswith("Cosina_Voigtlander_") or stem.startswith("Voigtlander_"):
        return "Voigtlander"
    if stem.startswith("TH_Swiss_"):
        return "TH Swiss"
    if stem.startswith("Zhong_Yi_"):
        return "Zhong Yi"
    if stem.startswith("Arri_"):
        return "ARRI"
    if stem.startswith("Fujinon_"):
        return "Fujifilm"
    if stem.startswith("Fijifilm_") or stem.startswith("Fujifim_"):
        return "Fujifilm"
    if stem.startswith(NIKON_PREFIXES):
        return "Nikon"

    if head == "7Artisans":
        return "7Artisans"
    if head == "Cosina":
        return "Cosina"
    if head == "GOI":
        return "GOI"
    if head == "Ricoh":
        return "Ricoh"
    if head == "Rodenstein":
        return "Rodenstock"
    if head == "Sasmung":
        return "Samsung"
    if head == "Simga":
        return "Sigma"
    if head == "Venux":
        return "Venus"
    return head.replace("-", " ")


def load_space55_records() -> list[LensRecord]:
    records: list[LensRecord] = []
    for preset in SPACE55_PRESETS:
        records.append(
            LensRecord(
                id=preset["id"],
                label=preset["label"],
                relative_path=f"assets/lenses/space55/{preset['path']}",
                manufacturer="Space55",
                manufacturer_id="space55",
            )
        )
    return records


def sync_nuke_lenses() -> list[LensRecord]:
    if not NUKE_SOURCE_DIR.is_dir():
        raise SystemExit(f"Missing source lens dir: {NUKE_SOURCE_DIR}")

    NUKE_DEST_DIR.mkdir(parents=True, exist_ok=True)

    existing = {path.name for path in NUKE_DEST_DIR.glob("*.lens")}
    source_names = {path.name for path in NUKE_SOURCE_DIR.glob("*.lens")}
    for stale in sorted(existing - source_names):
        (NUKE_DEST_DIR / stale).unlink()

    records: list[LensRecord] = []
    used_ids: set[str] = set()

    for source_path in sorted(NUKE_SOURCE_DIR.glob("*.lens"), key=lambda path: path.name.lower()):
        dest_path = NUKE_DEST_DIR / source_path.name
        shutil.copy2(source_path, dest_path)

        stem = source_path.stem
        manufacturer = derive_manufacturer(stem)
        manufacturer_id = slugify(manufacturer)
        base_id = slugify(stem)
        lens_id = base_id
        suffix = 2
        while lens_id in used_ids:
            lens_id = f"{base_id}-{suffix}"
            suffix += 1
        used_ids.add(lens_id)

        records.append(
            LensRecord(
                id=lens_id,
                label=stem.replace("_", " "),
                relative_path=f"assets/lenses/flaresim_nuke/{source_path.name}",
                manufacturer=manufacturer,
                manufacturer_id=manufacturer_id,
            )
        )

    return records


def write_manifest(records: list[LensRecord]) -> None:
    payload = {
        "source": {
            "name": "LocalStarlight/flaresim_nuke",
            "repo": "https://github.com/LocalStarlight/flaresim_nuke",
            "subpath": "lenses/lens_files",
        },
        "count": len(records),
        "presets": [
            {
                "id": record.id,
                "manufacturer": record.manufacturer,
                "label": record.label,
                "path": Path(record.relative_path).name,
            }
            for record in records
        ],
    }
    MANIFEST_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def write_generated_include(records: list[LensRecord]) -> None:
    ordered = sorted(
        records,
        key=lambda record: (
            record.manufacturer != "Space55",
            record.manufacturer.lower(),
            record.label.lower(),
        ),
    )

    manufacturers: list[dict[str, object]] = []
    manufacturer_index: dict[str, int] = {}
    grouped_records: list[LensRecord] = []

    for record in ordered:
        index = manufacturer_index.get(record.manufacturer)
        if index is None:
            index = len(manufacturers)
            manufacturer_index[record.manufacturer] = index
            manufacturers.append(
                {
                    "id": record.manufacturer_id,
                    "label": record.manufacturer,
                    "first_lens_index": len(grouped_records),
                    "lens_count": 0,
                }
            )
        manufacturers[index]["lens_count"] = int(manufacturers[index]["lens_count"]) + 1
        grouped_records.append(record)

    lines: list[str] = []
    lines.append("// Generated by scripts/generate_builtin_lenses.py")
    lines.append("")
    lines.append("constexpr BuiltinLensManufacturerDescriptor kBuiltinLensManufacturers[] = {")
    for manufacturer in manufacturers:
        lines.append(
            '    {"%s", "%s", %d, %d},'
            % (
                c_string(str(manufacturer["id"])),
                c_string(str(manufacturer["label"])),
                int(manufacturer["first_lens_index"]),
                int(manufacturer["lens_count"]),
            )
        )
    lines.append("};")
    lines.append("")
    lines.append("constexpr BuiltinLensDescriptor kBuiltinLenses[] = {")
    for record in grouped_records:
        lines.append(
            '    {"%s", "%s", "%s", %d},'
            % (
                c_string(record.id),
                c_string(record.label),
                c_string(record.relative_path),
                manufacturer_index[record.manufacturer],
            )
        )
    lines.append("};")
    lines.append("")

    GENERATED_INC.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    all_records = load_space55_records() + sync_nuke_lenses()
    write_manifest([record for record in all_records if "flaresim_nuke" in record.relative_path])
    write_generated_include(all_records)
    print(
        f"Generated {len(all_records)} bundled lenses across "
        f"{len({record.manufacturer for record in all_records})} manufacturers."
    )


if __name__ == "__main__":
    main()
