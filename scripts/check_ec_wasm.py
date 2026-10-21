#!/usr/bin/env python3
"""Check a built EffectCraft plug-in module against the plug-in API v1 rules.

EffectCraft refuses modules that import anything, and needs these exports: memory,
ec_api_version, ec_manifest_ptr, ec_manifest_len, ec_alloc, ec_render.

Usage: check_ec_wasm.py <flaresim_ec.wasm>
"""

from __future__ import annotations

import sys
from pathlib import Path

REQUIRED_EXPORTS = {"memory", "ec_api_version", "ec_manifest_ptr", "ec_manifest_len", "ec_alloc", "ec_render"}
KIND_NAMES = {0: "func", 1: "table", 2: "memory", 3: "global", 4: "tag"}


def read_leb(data: bytes, pos: int) -> tuple[int, int]:
    result = shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return result, pos


def read_name(data: bytes, pos: int) -> tuple[str, int]:
    length, pos = read_leb(data, pos)
    return data[pos : pos + length].decode("utf-8"), pos + length


def parse_sections(data: bytes) -> tuple[list[str], dict[str, str]]:
    if data[:4] != b"\0asm":
        raise ValueError("not a WebAssembly binary")
    imports: list[str] = []
    exports: dict[str, str] = {}
    pos = 8
    while pos < len(data):
        section_id = data[pos]
        size, pos = read_leb(data, pos + 1)
        body, end = pos, pos + size
        if section_id == 2:
            count, body = read_leb(data, body)
            for _ in range(count):
                module, body = read_name(data, body)
                name, body = read_name(data, body)
                imports.append(f"{module}.{name}")
                kind = data[body]
                body += 1
                if kind == 0:
                    _, body = read_leb(data, body)
                elif kind == 1:
                    body += 1
                    flags, body = read_leb(data, body)
                    _, body = read_leb(data, body)
                    if flags & 1:
                        _, body = read_leb(data, body)
                elif kind == 2:
                    flags, body = read_leb(data, body)
                    _, body = read_leb(data, body)
                    if flags & 1:
                        _, body = read_leb(data, body)
                elif kind == 3:
                    body += 2
                else:
                    break
        elif section_id == 7:
            count, body = read_leb(data, body)
            for _ in range(count):
                name, body = read_name(data, body)
                kind = data[body]
                _, body = read_leb(data, body + 1)
                exports[name] = KIND_NAMES.get(kind, str(kind))
        pos = end
    return imports, exports


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    data = Path(sys.argv[1]).read_bytes()
    imports, exports = parse_sections(data)
    ok = True
    if imports:
        ok = False
        print("imports (EffectCraft refuses these):")
        for name in imports:
            print(f"  {name}")
    missing = sorted(REQUIRED_EXPORTS - exports.keys())
    if missing:
        ok = False
        print("missing exports: " + ", ".join(missing))
    print(f"{len(data)} bytes, {len(imports)} imports, exports: {', '.join(sorted(exports))}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
