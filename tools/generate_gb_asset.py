#!/usr/bin/env python3
"""Generate Game Boy (GBDK-2020) bitmap tile assets from scene PNG images.

The Game Boy has a 160x144 pixel screen made of 8x8 tiles.
Graphics are stored as 2bpp (2 bits per pixel), giving 4 shades of grey.

Workflow:
- Input:  `gfx/originals/sceneNN.png` (high-res originals)
- Process: resize to 160x144, convert to 4-shade greyscale (2bpp), pack into 8x8 tiles
- Output: `src/gb/sceneNN_bitmap_gb.h` + `src/gb/sceneNN_bitmap_gb.c`

Also handles `gfx/intro.png` → `src/gb/intro_bitmap_gb.h` + `src/gb/intro_bitmap_gb.c`

Naming: all Game Boy specific files use the `_gb` suffix.
"""

from pathlib import Path
from PIL import Image

# Game Boy screen dimensions
GB_WIDTH = 160
GB_HEIGHT = 144
GB_IMAGE_HEIGHT = 72

# Tile dimensions
TILE_SIZE = 8
TILES_X = GB_WIDTH // TILE_SIZE    # 20 tiles wide
TILES_Y = GB_HEIGHT // TILE_SIZE   # 18 tiles tall
TOTAL_TILES = TILES_X * TILES_Y   # 360 tiles

# 2bpp: 2 bytes per tile row × 8 rows = 16 bytes per tile
BYTES_PER_TILE = 16
TOTAL_BYTES = TOTAL_TILES * BYTES_PER_TILE  # 5760 bytes

# GB greyscale palette: 4 shades (0=lightest/white … 3=darkest/black)
# We map 8-bit greyscale (0=black … 255=white) to GB palette index (0=white … 3=black)
PALETTE_SHADES = 4


def discover_scenes(originals_dir: Path) -> list[dict]:
    """Discover scene files in `gfx/originals/` by `sceneNN.png` naming."""
    scenes = []
    for image_path in sorted(originals_dir.glob("scene*.png")):
        stem = image_path.stem
        suffix = stem.removeprefix("scene")
        if not suffix.isdigit():
            continue
        scenes.append({"id": int(suffix), "input_path": image_path})
    return scenes


def prepare_gb_image(image: Image.Image) -> Image.Image:
    """Center-crop and quantise image into a 160x72 top-half GB buffer.

    Steps:
    1. Convert to greyscale (L mode, 0=black … 255=white).
    2. Center-crop to 160:72 aspect ratio (20:9), using more vertical content
       than the C64 pipeline while still fitting half-screen GB image area.
    3. Resize to 160x72 pixels.
    4. Quantise to 4 GB shades (0=white … 3=black).
    5. Place result at top of a 160x144 buffer; lower half is filled black.
    """
    # Step 1: greyscale
    grey = image.convert("L")

    source_w, source_h = grey.size
    target_aspect = GB_WIDTH / GB_IMAGE_HEIGHT  # 160/72 ≈ 2.222

    # Step 2: centre-crop to target aspect ratio
    source_aspect = source_w / source_h
    if source_aspect > target_aspect:
        # Source is wider than target: crop left/right
        new_w = int(round(source_h * target_aspect))
        crop_x = (source_w - new_w) // 2
        grey = grey.crop((crop_x, 0, crop_x + new_w, source_h))
    elif source_aspect < target_aspect:
        # Source is taller than target: crop top/bottom
        new_h = int(round(source_w / target_aspect))
        crop_y = (source_h - new_h) // 2
        grey = grey.crop((0, crop_y, source_w, crop_y + new_h))

    # Step 3: resize to 160x72
    resized = grey.resize((GB_WIDTH, GB_IMAGE_HEIGHT), Image.Resampling.LANCZOS)

    # Step 4: quantise to 4 shades and invert for GB palette indexing.
    # PIL greyscale: 0=black ... 255=white
    # GB color index: 0=white ... 3=black
    # So invert bucket mapping: 0-63 -> 3, 64-127 -> 2, 128-191 -> 1, 192-255 -> 0
    quantised = resized.point(lambda p: 3 - min(p // 64, 3))

    # Step 5: place into top half of full GB buffer (160x144)
    full_frame = Image.new("L", (GB_WIDTH, GB_HEIGHT), color=3)
    full_frame.paste(quantised, (0, 0))

    return full_frame


def image_to_2bpp_tiles(image: Image.Image) -> bytes:
    """Convert a 4-shade 160x144 greyscale image to Game Boy 2bpp tile data.

    Game Boy 2bpp format (per tile row):
    - byte 0 (low plane): bit 7 of each pixel from left to right
    - byte 1 (high plane): bit 6 of each pixel from left to right
    
    Pixel colour index (0=white … 3=black):
      index bit 0 → low plane bit for that pixel
      index bit 1 → high plane bit for that pixel

    Tiles are stored left-to-right, top-to-bottom.
    """
    pixels = image.load()
    result = bytearray()

    for tile_y in range(TILES_Y):
        for tile_x in range(TILES_X):
            # Each tile: 8 rows × 2 bytes
            for row in range(TILE_SIZE):
                low_byte = 0
                high_byte = 0
                y = tile_y * TILE_SIZE + row
                for col in range(TILE_SIZE):
                    x = tile_x * TILE_SIZE + col
                    # Pixel value 0-3 (0=white, 3=black on GB)
                    shade = int(pixels[x, y])
                    bit_pos = 7 - col  # MSB is leftmost pixel
                    low_byte  |= (shade & 0x01) << bit_pos
                    high_byte |= ((shade >> 1) & 0x01) << bit_pos
                result.append(low_byte)
                result.append(high_byte)

    assert len(result) == TOTAL_BYTES, f"Expected {TOTAL_BYTES} bytes, got {len(result)}"
    return bytes(result)


def write_gb_header(
    path: Path,
    payload_size: int,
    guard: str,
    array_name: str,
    size_name: str,
    tiles_x: int,
    tiles_y: int,
) -> None:
    """Write banked declaration header for GBDK-2020 asset data."""
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w") as f:
        f.write("/* Auto-generated Game Boy bitmap header. DO NOT EDIT. */\n")
        f.write(f"/* Tile layout: {tiles_x} × {tiles_y} tiles ({tiles_x * tiles_y} total), 2bpp */\n")
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <gb/gb.h>\n\n")
        f.write(f"#define {size_name} {payload_size}\n")
        f.write(f"#define {size_name}_TILES_X {tiles_x}\n")
        f.write(f"#define {size_name}_TILES_Y {tiles_y}\n")
        f.write(f"#define {size_name}_TILES_TOTAL {tiles_x * tiles_y}\n\n")
        f.write(f"BANKREF_EXTERN({array_name})\n")
        f.write(f"extern const uint8_t {array_name}[{size_name}];\n\n")
        f.write(f"#endif  /* {guard} */\n")


def write_gb_source(
    path: Path,
    payload: bytes,
    include_header: str,
    array_name: str,
) -> None:
    """Write banked definition source for GBDK-2020 asset data."""
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w") as f:
        f.write("/* Auto-generated Game Boy bitmap source. DO NOT EDIT. */\n")
        f.write("#pragma bank 255\n")
        f.write("#include <gb/gb.h>\n")
        f.write(f"#include \"{include_header}\"\n\n")
        f.write(f"BANKREF({array_name})\n")
        f.write(f"const uint8_t {array_name}[] = {{\n")

        for i in range(0, len(payload), 16):
            chunk = payload[i : i + 16]
            hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
            f.write(f"    {hex_vals},\n")

        f.write("};\n")


def process_scene(scene_id: int, input_path: Path, output_header: Path, output_source: Path) -> None:
    """Convert one scene PNG → GB banked header + source."""
    print(f"Processing Scene {scene_id:02d} → {output_header.name}, {output_source.name} …")
    image = Image.open(input_path)
    gb_image = prepare_gb_image(image)
    tile_data = image_to_2bpp_tiles(gb_image)

    guard       = f"SCENE{scene_id:02d}_BITMAP_GB_H"
    array_name  = f"SCENE{scene_id:02d}_BITMAP_GB_DATA"
    size_name   = f"SCENE{scene_id:02d}_BITMAP_GB_SIZE"

    write_gb_header(output_header, len(tile_data), guard, array_name, size_name, TILES_X, TILES_Y)
    write_gb_source(output_source, tile_data, output_header.name, array_name)
    print(f"  Input:  {input_path}")
    print(f"  Output: {output_header}")
    print(f"          {output_source}  ({len(tile_data)} bytes, {TOTAL_TILES} tiles)")


def process_intro(input_path: Path, output_header: Path, output_source: Path) -> None:
    """Convert intro.png → GB banked header + source."""
    if not input_path.exists():
        print(f"⚠ Intro image not found (skipping): {input_path}")
        return

    print(f"Processing Intro image → {output_header.name}, {output_source.name} …")
    image = Image.open(input_path)
    gb_image = prepare_gb_image(image)
    tile_data = image_to_2bpp_tiles(gb_image)

    write_gb_header(
        output_header, len(tile_data),
        guard      = "INTRO_BITMAP_GB_H",
        array_name = "INTRO_BITMAP_GB_DATA",
        size_name  = "INTRO_BITMAP_GB_SIZE",
        tiles_x    = TILES_X,
        tiles_y    = TILES_Y,
    )
    write_gb_source(output_source, tile_data, output_header.name, "INTRO_BITMAP_GB_DATA")
    print(f"  Input:  {input_path}")
    print(f"  Output: {output_header}")
    print(f"          {output_source}  ({len(tile_data)} bytes, {TOTAL_TILES} tiles)")


def main() -> None:
    workspace_root = Path(__file__).resolve().parent.parent
    originals_dir  = workspace_root / "gfx" / "originals"
    gb_src_dir     = workspace_root / "src" / "gb"

    gb_src_dir.mkdir(parents=True, exist_ok=True)

    # --- Scene images ---
    scenes = discover_scenes(originals_dir)
    if not scenes:
        print(f"⚠ No scene images found in {originals_dir}")
        return

    for scene in scenes:
        scene_id    = scene["id"]
        input_path  = scene["input_path"]
        output_header = gb_src_dir / f"scene{scene_id:02d}_bitmap_gb.h"
        output_source = gb_src_dir / f"scene{scene_id:02d}_bitmap_gb.c"
        process_scene(scene_id, input_path, output_header, output_source)

    # --- Intro image ---
    intro_input  = workspace_root / "gfx" / "intro.png"
    intro_output = gb_src_dir / "intro_bitmap_gb.h"
    intro_source = gb_src_dir / "intro_bitmap_gb.c"
    process_intro(intro_input, intro_output, intro_source)

    print(f"\n✓ All GB assets written to {gb_src_dir}")


if __name__ == "__main__":
    main()
