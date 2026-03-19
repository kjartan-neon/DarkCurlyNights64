#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

MAGIC = b"RAW1"
OUTPUT_LOAD_ADDR = b"\x00\x20"
TOP_HALF_BYTES = 4000


def normalize_scene_payload(raw: bytes) -> bytes:
    if len(raw) == 8002 and raw[0] == 0x00 and raw[1] == 0xE0:
        return raw[2:]
    return raw


def top_half_payload(scene_path: Path) -> bytes:
    raw = scene_path.read_bytes()
    payload = normalize_scene_payload(raw)
    if len(payload) != 8000:
        raise ValueError(f"Unexpected scene payload size for {scene_path}: {len(payload)}")
    return payload[:TOP_HALF_BYTES]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pack SCENE??.BMP files into RAW1 uncompressed top-half scene pack"
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing SCENE??.BMP files")
    parser.add_argument("--output-file", required=True, help="Output pack file path")
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    output_file = Path(args.output_file)
    output_file.parent.mkdir(parents=True, exist_ok=True)

    scene_files = sorted(input_dir.glob("SCENE??.BMP"))
    if not scene_files:
        raise SystemExit(f"No SCENE??.BMP files found in {input_dir}")

    index_entries: list[tuple[int, int, int]] = []
    data_blob = bytearray()

    for index, scene_path in enumerate(scene_files, start=1):
        payload = top_half_payload(scene_path)
        offset = len(data_blob)
        size = len(payload)

        index_entries.append((offset, size, size))
        data_blob.extend(payload)
        print(f"{index:02d} <- {scene_path.name}  top-half {size} bytes")

    header = bytearray()
    header.extend(MAGIC)
    header.append(len(scene_files) & 0xFF)
    for offset, packed_size, raw_size in index_entries:
        header.extend(offset.to_bytes(4, "little"))
        header.extend(packed_size.to_bytes(2, "little"))
        header.extend(raw_size.to_bytes(2, "little"))

    pack_body = bytes(header) + bytes(data_blob)
    output_file.write_bytes(OUTPUT_LOAD_ADDR + pack_body)

    total_out = len(OUTPUT_LOAD_ADDR) + len(pack_body)
    print(
        f"TOTAL: {len(scene_files)} scenes, top-half raw={TOP_HALF_BYTES * len(scene_files)}, pack={total_out} bytes"
    )
    print(f"Wrote pack: {output_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
