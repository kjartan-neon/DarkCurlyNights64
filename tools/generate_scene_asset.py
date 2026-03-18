#!/usr/bin/env python3
"""Generate C64 hires bitmap asset from `gfx/images/scene01.png` with embedded C header."""

from pathlib import Path
from PIL import Image

OUTPUT_WIDTH = 320
OUTPUT_HEIGHT = 200
BYTES_PER_ROW = OUTPUT_WIDTH // 8
BITMAP_BYTES = OUTPUT_HEIGHT * BYTES_PER_ROW

PRG_LOAD_ADDR = 0xE000

INPUT_IMAGE = Path("gfx/images/scene01.png")
OUTPUT_FILE_ROOT = Path("SCENE01.BMP")
OUTPUT_FILE_BUILD = Path("build/SCENE01.BMP")
OUTPUT_HEADER = Path("src/scene01_bitmap.h")


def convert_to_bitmap_bytes(image: Image.Image, invert: bool = True) -> bytes:
    """Convert image to C64 bitmap bytes.
    
    PIL's "1" mode uses: 0=white, 255=black
    C64 bitmap uses: 1=white (pixel on), 0=black (pixel off)
    So we need to invert the PIL output.
    """
    grayscale = image.convert("L")
    resized = grayscale.resize((OUTPUT_WIDTH, OUTPUT_HEIGHT), Image.Resampling.LANCZOS)
    dithered = resized.convert("1", dither=Image.Dither.FLOYDSTEINBERG)

    pixels = dithered.load()
    bitmap = bytearray()

    for char_row in range(25):
        for char_col in range(40):
            for pixel_row in range(8):
                byte_value = 0
                y_pos = char_row * 8 + pixel_row
                for bit in range(8):
                    x_pos = char_col * 8 + bit
                    # PIL "1" mode: 0=white, 255=black (non-zero)
                    # We want: 1=white (set bit), 0=black (clear bit)
                    pil_pixel = pixels[x_pos, y_pos]
                    if pil_pixel == 0:  # PIL white → C64 white (set bit)
                        byte_value |= 1 << (7 - bit)
                bitmap.append(byte_value)

    if len(bitmap) != BITMAP_BYTES:
        raise ValueError(f"Unexpected bitmap size: {len(bitmap)} bytes")

    return bytes(bitmap)


def write_prg_file(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(PRG_LOAD_ADDR.to_bytes(2, byteorder="little"))
        handle.write(payload)


def write_c_header(path: Path, payload: bytes) -> None:
    """Write bitmap data as a C header file with embedded array."""
    path.parent.mkdir(parents=True, exist_ok=True)
    
    with path.open("w") as handle:
        handle.write("/* Auto-generated bitmap header. DO NOT EDIT. */\n")
        handle.write("#ifndef SCENE01_BITMAP_H\n")
        handle.write("#define SCENE01_BITMAP_H\n\n")
        handle.write("#include <stdint.h>\n\n")
        handle.write(f"#define SCENE01_BITMAP_SIZE {len(payload)}\n\n")
        handle.write("static const uint8_t SCENE01_BITMAP_DATA[SCENE01_BITMAP_SIZE] = {\n")
        
        # Write 16 bytes per line
        for i in range(0, len(payload), 16):
            chunk = payload[i:i+16]
            hex_values = ", ".join(f"0x{byte:02X}" for byte in chunk)
            handle.write(f"    {hex_values},\n")
        
        handle.write("};\n\n")
        handle.write("#endif  /* SCENE01_BITMAP_H */\n")


def main() -> None:
    workspace_root = Path(__file__).resolve().parent.parent
    input_path = workspace_root / INPUT_IMAGE
    output_root = workspace_root / OUTPUT_FILE_ROOT
    output_build = workspace_root / OUTPUT_FILE_BUILD
    output_header = workspace_root / OUTPUT_HEADER

    if not input_path.exists():
        raise FileNotFoundError(f"Input image not found: {input_path}")

    image = Image.open(input_path)
    bitmap_bytes = convert_to_bitmap_bytes(image)

    write_prg_file(output_root, bitmap_bytes)
    write_prg_file(output_build, bitmap_bytes)
    write_c_header(output_header, bitmap_bytes)

    print(f"Input image: {input_path}")
    print(f"Output asset: {output_root}")
    print(f"Output asset: {output_build}")
    print(f"Output header: {output_header}")
    print(f"Bitmap size: {OUTPUT_WIDTH}x{OUTPUT_HEIGHT} pixels")
    print(f"Bitmap payload bytes: {len(bitmap_bytes)}")


if __name__ == "__main__":
    main()
