#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

echo "[1/3] Cropping images from gfx/originals -> gfx/images"
python3 tools/crop_images.py

echo "[2/3] Generating C64 temp images + scene assets"
python3 tools/generate_scene_assets.py

echo "[3/3] Packing raw top-half scenes from gfx/bmp into build/scenes_pack.bin"
python3 tools/pack_scenes_raw_tophalf.py --input-dir gfx/bmp --output-file build/scenes_pack.bin

echo "Done: image pipeline rebuilt successfully."
