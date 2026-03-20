#!/usr/bin/env python3
"""Prepare scene source images for the C64 pipeline.

Workflow stage 1:
- Input: `gfx/originals/sceneNN.png`
- Output: `gfx/images/sceneNN.png` (345x212)

Design goal:
- Keep the full horizontal content (no left/right crop).
- Only adjust vertically (crop or pad top/bottom) after width-fit.
"""

import sys
from pathlib import Path
from PIL import Image


TARGET_WIDTH = 345
TARGET_HEIGHT = 145
TARGET_RATIO = TARGET_WIDTH / TARGET_HEIGHT


def prepare_scene_image(image_path: str, output_path: str) -> None:
    """
    Convert one source scene image to the fixed working size (345x212).

    Steps:
    1) Load source image.
    2) Resize to target width while preserving original aspect ratio.
    3) If needed, crop only top/bottom to target height.
    4) If needed, pad only top/bottom to target height.
    5) Save to `gfx/images`.
    
    Args:
        image_path: Path to input image in gfx/originals/
        output_path: Path to save prepared image in gfx/images/
    """
    # Step 1: Load image and normalize to RGB mode.
    img = Image.open(image_path).convert("RGB")
    width, height = img.size

    # Step 2: Resize to the target width while preserving aspect ratio.
    # This guarantees we keep the full left/right content from the source.
    resized_height = int(round(height * (TARGET_WIDTH / width)))
    width_fitted = img.resize((TARGET_WIDTH, resized_height), Image.Resampling.LANCZOS)

    # Step 3: Adjust only vertical framing to reach target height.
    # If too tall, crop top/bottom (centered). If too short, pad top/bottom.
    if resized_height > TARGET_HEIGHT:
        top = (resized_height - TARGET_HEIGHT) // 2
        bottom = top + TARGET_HEIGHT
        final_image = width_fitted.crop((0, top, TARGET_WIDTH, bottom))
        vertical_action = f"crop y={top}:{bottom}"
    elif resized_height < TARGET_HEIGHT:
        top_padding = (TARGET_HEIGHT - resized_height) // 2
        final_image = Image.new("RGB", (TARGET_WIDTH, TARGET_HEIGHT), (0, 0, 0))
        final_image.paste(width_fitted, (0, top_padding))
        vertical_action = f"pad top={top_padding}, bottom={TARGET_HEIGHT - resized_height - top_padding}"
    else:
        final_image = width_fitted
        vertical_action = "none"

    # Step 4: Logging for quick tuning and debugging.
    current_ratio = width / height
    
    print(f"Processing: {Path(image_path).name}")
    print(f"  Original size: {width}x{height}")
    print(f"  Original ratio: {current_ratio:.3f}")
    print(f"  Target size: {TARGET_WIDTH}x{TARGET_HEIGHT} (ratio: {TARGET_RATIO:.2f})")
    print(f"  Width-fit size: {TARGET_WIDTH}x{resized_height}")
    print(f"  Vertical adjust: {vertical_action}")
    print(f"  Output size: {final_image.size[0]}x{final_image.size[1]}")

    # Step 5: Save prepared image.
    final_image.save(output_path)
    print(f"  Saved to: {output_path}\n")


def main() -> None:
    """Process scene images in `gfx/originals/` into `gfx/images/`."""
    workspace_root = Path(__file__).parent.parent
    originals_dir = workspace_root / "gfx" / "originals"
    images_dir = workspace_root / "gfx" / "images"
    
    # Ensure output directory exists before writing files.
    images_dir.mkdir(parents=True, exist_ok=True)
    
    if not originals_dir.exists():
        print(f"Error: {originals_dir} does not exist", file=sys.stderr)
        sys.exit(1)
    
    # Process all files named sceneNN.png.
    image_files = sorted(originals_dir.glob("scene*.png"))
    
    if not image_files:
        print(f"No scene images found in {originals_dir} (expected: scene01.png, scene02.png, ...)")
        sys.exit(0)
    
    print(f"Found {len(image_files)} scene image(s) to process\n")
    
    for image_file in sorted(image_files):
        try:
            output_file = images_dir / image_file.name
            prepare_scene_image(str(image_file), str(output_file))
        except Exception as e:
            print(f"Error processing {image_file.name}: {e}\n", file=sys.stderr)
    
    print(f"Done! Cropped images saved to {images_dir}")


if __name__ == "__main__":
    main()
