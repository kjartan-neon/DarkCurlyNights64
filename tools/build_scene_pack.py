#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

MAGIC = b"SCN1"
SCENE_FILE_RE = re.compile(r"^SCENE(\d{2})\.BMP$")


def discover_scene_files(input_dir: Path) -> list[tuple[int, Path]]:
    scene_files: list[tuple[int, Path]] = []
    for path in sorted(input_dir.glob("SCENE??.BMP")):
        match = SCENE_FILE_RE.match(path.name)
        if not match:
            continue
        scene_id = int(match.group(1))
        if scene_id < 1 or scene_id > 99:
            continue
        scene_files.append((scene_id, path))
    return scene_files


def build_pack(scene_files: list[tuple[int, Path]]) -> bytes:
    entries: list[tuple[int, int, int, bytes]] = []
    offset = 0

    for scene_id, path in scene_files:
        payload = path.read_bytes()
        size = len(payload)
        if size == 0:
            raise ValueError(f"Empty scene payload: {path}")
        if size > 65535:
            raise ValueError(f"Scene payload too large for 16-bit size field: {path} ({size})")
        if offset > 0xFFFFFF:
            raise ValueError("Pack data offset exceeds 24-bit limit")

        entries.append((scene_id, offset, size, payload))
        offset += size

    if len(entries) > 255:
        raise ValueError("Too many scenes for 8-bit scene count")

    header = bytearray()
    header.extend(MAGIC)
    header.append(len(entries))

    table = bytearray()
    data = bytearray()

    for scene_id, scene_offset, scene_size, payload in entries:
        table.append(scene_id)
        table.append(scene_offset & 0xFF)
        table.append((scene_offset >> 8) & 0xFF)
        table.append((scene_offset >> 16) & 0xFF)
        table.append(scene_size & 0xFF)
        table.append((scene_size >> 8) & 0xFF)
        data.extend(payload)

    return bytes(header + table + data)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build SCENES.BIN from SCENENN.BMP files")
    parser.add_argument("--input-dir", type=Path, default=Path("."), help="Directory with SCENENN.BMP files")
    parser.add_argument("--output", type=Path, default=Path("SCENES.BIN"), help="Output packed file")
    parser.add_argument("--copy-to-build", action="store_true", help="Also copy output file into build/SCENES.BIN")
    args = parser.parse_args()

    input_dir = args.input_dir.resolve()
    output_path = args.output.resolve()

    scene_files = discover_scene_files(input_dir)
    if not scene_files:
        raise SystemExit(f"No SCENENN.BMP files found in {input_dir}")

    packed_bytes = build_pack(scene_files)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(packed_bytes)

    print(f"Packed {len(scene_files)} scene(s) into {output_path} ({len(packed_bytes)} bytes)")

    if args.copy_to_build:
        build_output = output_path.parent / "build" / output_path.name
        build_output.parent.mkdir(parents=True, exist_ok=True)
        build_output.write_bytes(packed_bytes)
        print(f"Copied pack to {build_output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
