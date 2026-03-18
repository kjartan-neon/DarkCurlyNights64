#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

echo "[1/2] Cropping images from gfx/originals -> gfx/images"
python3 tools/crop_images.py

echo "[2/2] Generating C64 temp images + scene assets"
python3 tools/generate_scene_assets.py

echo "Done: image pipeline rebuilt successfully."
