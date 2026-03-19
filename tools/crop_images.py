#!/usr/bin/env python3
"""Crop scene source images for the C64 pipeline.

Workflow stage 1:
- Input: `gfx/originals/sceneNN.png`
- Output: `gfx/images/sceneNN.png` (center-cropped 345x212)
"""

import sys
from pathlib import Path
from PIL import Image


TARGET_WIDTH = 345
TARGET_HEIGHT = 212
TARGET_RATIO = TARGET_WIDTH / TARGET_HEIGHT
INSET_FRACTION = 0.10


def resize_and_center_crop(image_path: str, output_path: str) -> None:
    """
    Resize an image to fill the target area, then center-crop to 345x212.
    
    Args:
        image_path: Path to input image in gfx/originals/
        output_path: Path to save cropped image in gfx/images/
    """
    img = Image.open(image_path).convert("RGB")
    width, height = img.size

    inset_x = int(round(width * INSET_FRACTION))
    inset_y = int(round(height * INSET_FRACTION))
    zoom_left = max(0, inset_x)
    zoom_top = max(0, inset_y)
    zoom_right = min(width, width - inset_x)
    zoom_bottom = min(height, height - inset_y)

    if zoom_right <= zoom_left or zoom_bottom <= zoom_top:
        zoom_left, zoom_top, zoom_right, zoom_bottom = 0, 0, width, height

    zoomed = img.crop((zoom_left, zoom_top, zoom_right, zoom_bottom))
    zoomed_width, zoomed_height = zoomed.size
    current_ratio = zoomed_width / zoomed_height
    
    print(f"Processing: {Path(image_path).name}")
    print(f"  Original size: {width}x{height}")
    print(f"  Zoom crop: x={zoom_left}:{zoom_right}, y={zoom_top}:{zoom_bottom} ({zoomed_width}x{zoomed_height})")
    print(f"  Zoomed ratio: {current_ratio:.2f}")
    print(f"  Target size: {TARGET_WIDTH}x{TARGET_HEIGHT} (ratio: {TARGET_RATIO:.2f})")

    if current_ratio < TARGET_RATIO:
        resized_width = TARGET_WIDTH
        resized_height = int(round(TARGET_WIDTH / current_ratio))
    else:
        resized_height = TARGET_HEIGHT
        resized_width = int(round(TARGET_HEIGHT * current_ratio))

    resized = zoomed.resize((resized_width, resized_height), Image.Resampling.LANCZOS)

    left = max(0, (resized_width - TARGET_WIDTH) // 2)
    top = max(0, (resized_height - TARGET_HEIGHT) // 2)
    crop_box = (left, top, left + TARGET_WIDTH, top + TARGET_HEIGHT)

    cropped = resized.crop(crop_box)
    print(f"  Resized to: {resized_width}x{resized_height}")
    print(f"  Output size: {cropped.size[0]}x{cropped.size[1]}")

    cropped.save(output_path)
    print(f"  Saved to: {output_path}\n")


def main() -> None:
    """Process scene images in `gfx/originals/` into `gfx/images/`."""
    workspace_root = Path(__file__).parent.parent
    originals_dir = workspace_root / "gfx" / "originals"
    images_dir = workspace_root / "gfx" / "images"
    
    # Ensure output directory exists
    images_dir.mkdir(parents=True, exist_ok=True)
    
    if not originals_dir.exists():
        print(f"Error: {originals_dir} does not exist", file=sys.stderr)
        sys.exit(1)
    
    image_files = sorted(originals_dir.glob("scene*.png"))
    
    if not image_files:
        print(f"No scene images found in {originals_dir} (expected: scene01.png, scene02.png, ...)")
        sys.exit(0)
    
    print(f"Found {len(image_files)} scene image(s) to process\n")
    
    for image_file in sorted(image_files):
        try:
            output_file = images_dir / image_file.name
            resize_and_center_crop(str(image_file), str(output_file))
        except Exception as e:
            print(f"Error processing {image_file.name}: {e}\n", file=sys.stderr)
    
    print(f"Done! Cropped images saved to {images_dir}")


if __name__ == "__main__":
    main()
