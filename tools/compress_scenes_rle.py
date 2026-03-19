#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

MAGIC = b"RPK1"
OUTPUT_LOAD_ADDR = b"\x00\x20"


def packbits_encode(data: bytes) -> bytes:
    out = bytearray()
    index = 0
    size = len(data)

    while index < size:
        run_len = 1
        while index + run_len < size and run_len < 128 and data[index + run_len] == data[index]:
            run_len += 1

        if run_len >= 3:
            out.append(127 + run_len)
            out.append(data[index])
            index += run_len
            continue

        literal_start = index
        index += 1
        while index < size:
            run_len = 1
            while index + run_len < size and run_len < 128 and data[index + run_len] == data[index]:
                run_len += 1
            if run_len >= 3 or (index - literal_start) >= 128:
                break
            index += 1

        literal_len = index - literal_start
        out.append(literal_len - 1)
        out.extend(data[literal_start:index])

    return bytes(out)


def normalize_scene_payload(raw: bytes) -> bytes:
    if len(raw) == 8002 and raw[0] == 0x00 and raw[1] == 0xE0:
        return raw[2:]
    return raw


def compress_scene(input_path: Path) -> tuple[bytes, int]:
    raw = input_path.read_bytes()
    payload = normalize_scene_payload(raw)
    encoded = packbits_encode(payload)
    return encoded, len(payload)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compress SCENE??.BMP files into one indexed RLE pack for C64 disk images.")
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
    total_in = 0
    total_comp = 0

    for index, scene_path in enumerate(scene_files, start=1):
        encoded, raw_size = compress_scene(scene_path)
        offset = len(data_blob)
        comp_size = len(encoded)
        total_in += raw_size
        total_comp += comp_size

        index_entries.append((offset, comp_size, raw_size))
        data_blob.extend(encoded)
        print(f"{index:02d} <- {scene_path.name}  {raw_size} -> {comp_size} bytes")

    header = bytearray()
    header.extend(MAGIC)
    header.append(len(scene_files) & 0xFF)
    for offset, comp_size, raw_size in index_entries:
        header.extend(offset.to_bytes(4, "little"))
        header.extend(comp_size.to_bytes(2, "little"))
        header.extend(raw_size.to_bytes(2, "little"))

    pack_body = bytes(header) + bytes(data_blob)
    output_file.write_bytes(OUTPUT_LOAD_ADDR + pack_body)

    total_out = len(OUTPUT_LOAD_ADDR) + len(pack_body)
    ratio = (100.0 * total_comp / total_in) if total_in else 0.0
    print(f"TOTAL: {len(scene_files)} scenes, raw={total_in}, compressed={total_comp} ({ratio:.1f}%), pack={total_out} bytes")
    print(f"Wrote pack: {output_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
