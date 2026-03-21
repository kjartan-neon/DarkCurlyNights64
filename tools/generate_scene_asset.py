#!/usr/bin/env python3
"""Generate C64 hires bitmap assets from cropped scene PNG images.

The C64 has non-square pixels (taller than wide, roughly 1:2 aspect ratio).
To display an image at the correct aspect ratio on screen, we render at half height.
So for a 320x200 bitmap, we render a full-width 320x100 image, which displays correctly.

Workflow stage 2:
- Input: `gfx/images/sceneNN.png` (from `tools/crop_images.py`)
- Temp: `gfx/c64/sceneNN_c64.png` (dithered 1-bit C64 working image)
- Output: `gfx/bmp/SCENENN.BMP`, `build/SCENENN.BMP`, `src/sceneNN_bitmap.h`
"""

from pathlib import Path
from PIL import Image

OUTPUT_WIDTH = 320
OUTPUT_HEIGHT = 200  # Full bitmap height (25 chars × 8 pixels)
RENDER_HEIGHT = 100  # But we only render half-height to compensate for C64 pixel aspect ratio
RENDER_WIDTH = 320   # Use the full visible width; storage size stays the same
BYTES_PER_ROW = OUTPUT_WIDTH // 8
BITMAP_BYTES = OUTPUT_HEIGHT * BYTES_PER_ROW

PRG_LOAD_ADDR = 0xE000

def discover_scenes(images_dir: Path) -> list[dict]:
    """Discover scene files in `gfx/images/` by `sceneNN.png` naming."""
    scenes = []
    for image_path in sorted(images_dir.glob("scene*.png")):
        stem = image_path.stem
        suffix = stem.removeprefix("scene")
        if not suffix.isdigit():
            continue
        scenes.append({"id": int(suffix), "input_path": image_path})
    return scenes


def prepare_c64_image(image: Image.Image) -> Image.Image:
    """Create a full-width 1-bit C64 working image at 320x100 pixels.

    This version keeps the full input width and only crops top/bottom to match
    the C64 display aspect (320x200 = 1.6), then renders at half height to
    compensate for C64 pixel shape.
    """
    # Step 1: Convert to grayscale for predictable thresholding/dithering.
    grayscale = image.convert("L")

    # Step 2: Read source dimensions.
    source_width, source_height = grayscale.size

    # Step 3: Compute the target display aspect ratio for C64 fullscreen.
    # We target 320x200 display space first; half-height rendering is done later.
    target_display_aspect = OUTPUT_WIDTH / OUTPUT_HEIGHT

    # Step 4: Keep full width; compute height needed to match target aspect.
    target_crop_height = int(round(source_width / target_display_aspect))

    # Step 5: Crop only top and bottom (centered vertically).
    # If the source is already shorter than target_crop_height, we cannot crop;
    # in that case we keep full height and continue.
    if source_height > target_crop_height:
        crop_top = (source_height - target_crop_height) // 2
        crop_bottom = crop_top + target_crop_height
        aspect_crop = grayscale.crop((0, crop_top, source_width, crop_bottom))
    else:
        aspect_crop = grayscale

    # Step 6: Resize to C64 working render size (320x100).
    # 320x100 is intentional: C64 pixels are non-square, so this displays as ~320x200.
    resized = aspect_crop.resize((RENDER_WIDTH, RENDER_HEIGHT), Image.Resampling.LANCZOS)

    # Step 7: Convert to 1-bit with dithering for bitmap export.
    return resized.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def convert_to_bitmap_bytes(c64_image: Image.Image) -> bytes:
    """Convert image to C64 bitmap bytes.
    
    The C64 has non-square pixels with ~2:1 height:width ratio.
    We compensate by rendering at half the intended display height.
    
    PIL's "1" mode uses: 0=white, 255=black
    C64 bitmap uses: 1=white (pixel on), 0=black (pixel off)
    """
    pixels = c64_image.load()
    bitmap = bytearray()

    # Fill the full 25-row bitmap, but only populate the top half with actual image data
    for char_row in range(25):
        for char_col in range(40):
            for pixel_row in range(8):
                byte_value = 0
                y_pos = char_row * 8 + pixel_row
                
                # Only use image data for the top half (RENDER_HEIGHT = 100 pixels = 12.5 char rows)
                if y_pos < RENDER_HEIGHT:
                    for bit in range(8):
                        x_pos = char_col * 8 + bit
                        # PIL "1" mode: 0=white, 255=black (non-zero)
                        # We want: 1=white (set bit), 0=black (clear bit)
                        pil_pixel = pixels[x_pos, y_pos]
                        if pil_pixel == 0:  # PIL white → C64 white (set bit)
                            byte_value |= 1 << (7 - bit)
                # Else: leave byte_value at 0 (black) for the bottom half
                
                bitmap.append(byte_value)

    if len(bitmap) != BITMAP_BYTES:
        raise ValueError(f"Unexpected bitmap size: {len(bitmap)} bytes")

    return bytes(bitmap)


def write_prg_file(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(PRG_LOAD_ADDR.to_bytes(2, byteorder="little"))
        handle.write(payload)


def write_temp_c64_image(path: Path, image: Image.Image) -> None:
    """Write temporary C64 working image to `gfx/c64/`."""
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def write_c_header(path: Path, payload: bytes, scene_id: int) -> None:
    """Write bitmap data as a C header file with embedded array."""
    path.parent.mkdir(parents=True, exist_ok=True)
    
    guard = f"SCENE{scene_id:02d}_BITMAP_H"
    array_name = f"SCENE{scene_id:02d}_BITMAP_DATA"
    size_name = f"SCENE{scene_id:02d}_BITMAP_SIZE"
    
    with path.open("w") as handle:
        handle.write("/* Auto-generated bitmap header. DO NOT EDIT. */\n")
        handle.write(f"#ifndef {guard}\n")
        handle.write(f"#define {guard}\n\n")
        handle.write("#include <stdint.h>\n\n")
        handle.write(f"#define {size_name} {len(payload)}\n\n")
        handle.write(f"static const uint8_t {array_name}[{size_name}] = {{\n")
        
        # Write 16 bytes per line
        for i in range(0, len(payload), 16):
            chunk = payload[i:i+16]
            hex_values = ", ".join(f"0x{byte:02X}" for byte in chunk)
            handle.write(f"    {hex_values},\n")
        
        handle.write("};\n\n")
        handle.write(f"#endif  /* {guard} */\n")


def write_named_c_header(path: Path, payload: bytes, guard: str, array_name: str, size_name: str) -> None:
    """Write bitmap payload as a C header with custom symbol names."""
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w") as handle:
        handle.write("/* Auto-generated bitmap header. DO NOT EDIT. */\n")
        handle.write(f"#ifndef {guard}\n")
        handle.write(f"#define {guard}\n\n")
        handle.write("#include <stdint.h>\n\n")
        handle.write(f"#define {size_name} {len(payload)}\n\n")
        handle.write(f"static const uint8_t {array_name}[{size_name}] = {{\n")

        for i in range(0, len(payload), 16):
            chunk = payload[i:i+16]
            hex_values = ", ".join(f"0x{byte:02X}" for byte in chunk)
            handle.write(f"    {hex_values},\n")

        handle.write("};\n\n")
        handle.write(f"#endif  /* {guard} */\n")


def process_intro_asset(workspace_root: Path, temp_c64_dir: Path, bmp_dir: Path) -> None:
    """Optionally convert gfx/intro.png into embedded intro bitmap assets."""
    input_path = workspace_root / "gfx" / "intro.png"

    if not input_path.exists():
        print(f"⚠ Intro image not found (skipping): {input_path}")
        return

    output_root = bmp_dir / "INTRO.BMP"
    output_build = workspace_root / "build" / "INTRO.BMP"
    output_header = workspace_root / "src" / "intro_bitmap.h"
    output_temp_c64 = temp_c64_dir / "intro_c64.png"

    print("Processing Intro image...")
    image = Image.open(input_path)
    c64_image = prepare_c64_image(image)
    write_temp_c64_image(output_temp_c64, c64_image)
    bitmap_bytes = convert_to_bitmap_bytes(c64_image)

    write_prg_file(output_root, bitmap_bytes)
    write_prg_file(output_build, bitmap_bytes)
    write_named_c_header(output_header, bitmap_bytes, "INTRO_BITMAP_H", "INTRO_BITMAP_DATA", "INTRO_BITMAP_SIZE")

    print(f"  Input image: {input_path}")
    print(f"  Temp C64 image: {output_temp_c64}")
    print(f"  Output asset: {output_root}")
    print(f"  Output header: {output_header}")
    print(f"  Bitmap display size: {RENDER_WIDTH}x{RENDER_HEIGHT} pixels filling 320x100")
    print(f"  Bitmap storage size: 320x200 pixels ({len(bitmap_bytes)} bytes)")


def main() -> None:
    workspace_root = Path(__file__).resolve().parent.parent
    images_dir = workspace_root / "gfx" / "images"
    temp_c64_dir = workspace_root / "gfx" / "c64"
    bmp_dir = workspace_root / "gfx" / "bmp"
    scenes = discover_scenes(images_dir)

    if not images_dir.exists():
        print(f"⚠ Input folder not found: {images_dir}")
        print("  Run tools/crop_images.py first.")
        return

    if not scenes:
        print(f"⚠ No scene images found in {images_dir} (expected: scene01.png, scene02.png, ...)")
        print("  Run tools/crop_images.py first.")
        return
    
    for scene in scenes:
        scene_id = scene["id"]
        input_path = scene["input_path"]
        output_root = bmp_dir / f"SCENE{scene_id:02d}.BMP"
        output_build = workspace_root / f"build/SCENE{scene_id:02d}.BMP"
        output_header = workspace_root / f"src/scene{scene_id:02d}_bitmap.h"
        output_temp_c64 = temp_c64_dir / f"scene{scene_id:02d}_c64.png"
        
        print(f"Processing Scene {scene_id}...")
        image = Image.open(input_path)
        c64_image = prepare_c64_image(image)
        write_temp_c64_image(output_temp_c64, c64_image)
        bitmap_bytes = convert_to_bitmap_bytes(c64_image)
        
        write_prg_file(output_root, bitmap_bytes)
        write_prg_file(output_build, bitmap_bytes)
        write_c_header(output_header, bitmap_bytes, scene_id)
        
        print(f"  Input image: {input_path}")
        print(f"  Temp C64 image: {output_temp_c64}")
        print(f"  Output asset: {output_root}")
        print(f"  Output header: {output_header}")
        print(f"  Bitmap display size: {RENDER_WIDTH}x{RENDER_HEIGHT} pixels filling 320x100")
        print(f"  Bitmap storage size: 320x200 pixels ({len(bitmap_bytes)} bytes)")

    process_intro_asset(workspace_root, temp_c64_dir, bmp_dir)


if __name__ == "__main__":
    main()

