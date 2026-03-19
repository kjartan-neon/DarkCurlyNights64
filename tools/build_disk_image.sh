#!/usr/bin/env zsh
# build_disk_image.sh — Assembles a C64 disk image for DarkCurlyNights64.
#
# D64 capacity: 664 free blocks after format.
# Scene images are stored as per-scene raw top-half files (01..30).
# Each file contains load address + 4000 bytes bitmap payload.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
C1541_BIN="${C1541_BIN:-/Applications/vice-arm64-gtk3-3.10/bin/c1541}"

IMAGE_TYPE="d64"
IMAGE_PATH=""
PRG_PATH="${BUILD_DIR}/DarkCurlyNights64.prg"

# Free blocks after format (as reported by c1541/BASIC)
D64_CAPACITY=664
D81_CAPACITY=3160

# ── Argument parsing ──────────────────────────────────────────────────────────
usage() {
  cat <<'EOF'
Usage: tools/build_disk_image.sh [options]

  --type   d64|d81    Disk image format   (default: d64)
  --output /path      Output image file   (default: build/DarkCurlyNights64.<type>)
  --prg    /path      Main PRG file       (default: build/DarkCurlyNights64.prg)
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --type)   IMAGE_TYPE="$2"; shift 2 ;;
    --output) IMAGE_PATH="$2"; shift 2 ;;
    --prg)    PRG_PATH="$2";   shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
  esac
done

# ── Validate ──────────────────────────────────────────────────────────────────
if [[ "$IMAGE_TYPE" != "d64" && "$IMAGE_TYPE" != "d81" ]]; then
  echo "Unsupported image type: ${IMAGE_TYPE}" >&2; exit 1
fi
[[ -z "$IMAGE_PATH" ]] && IMAGE_PATH="${BUILD_DIR}/DarkCurlyNights64.${IMAGE_TYPE}"
if [[ ! -x "$C1541_BIN" ]]; then
  echo "c1541 not found: ${C1541_BIN}" >&2; exit 1
fi
if [[ ! -f "$PRG_PATH" ]]; then
  echo "PRG not found: ${PRG_PATH}  (run: ninja -C build)" >&2; exit 1
fi

# ── Collect source scene files ────────────────────────────────────────────────
all_scenes=("${REPO_ROOT}"/gfx/bmp/SCENE??.BMP(N))
total_scenes=${#all_scenes[@]}

if (( total_scenes == 0 )); then
  echo "No SCENE??.BMP files found in ${REPO_ROOT}/gfx/bmp" >&2; exit 1
fi

# ── Build per-scene top-half files ────────────────────────────────────────────
TMPDIR_SCENES="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_SCENES"' EXIT

for scene_path in "${all_scenes[@]}"; do
  scene_name="${scene_path:t}"
  scene_id="${scene_name#SCENE}"
  scene_id="${scene_id%.BMP}"
  out_name="${scene_id}"

  python3 - "$scene_path" "${TMPDIR_SCENES}/${out_name}" <<'PY'
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])
payload = src.read_bytes()
if len(payload) < 4002:
    raise SystemExit(f"Scene file too small: {src} ({len(payload)} bytes)")

dst.write_bytes(payload[:4002])
PY
done

# ceil(n / 254)
ceil_blocks() { echo $(( ($1 + 253) / 254 )); }

prg_bytes=$(stat -f "%z" "$PRG_PATH")
prg_blocks=$(ceil_blocks "$prg_bytes")

if [[ "$IMAGE_TYPE" == "d64" ]]; then
  capacity=$D64_CAPACITY
else
  capacity=$D81_CAPACITY
fi

remaining=$(( capacity - prg_blocks ))
scene_total_bytes=0
scene_total_blocks=0

for scene_file in "${TMPDIR_SCENES}"/[0-9][0-9](N); do
  scene_bytes=$(stat -f "%z" "$scene_file")
  scene_blocks=$(ceil_blocks "$scene_bytes")
  scene_total_bytes=$(( scene_total_bytes + scene_bytes ))
  scene_total_blocks=$(( scene_total_blocks + scene_blocks ))
done

if (( scene_total_blocks > remaining )); then
  echo "Scene files do not fit on ${IMAGE_TYPE}: ${scene_total_blocks} blocks needed, ${remaining} available." >&2
  exit 1
fi

image_type_upper=$(echo "$IMAGE_TYPE" | tr '[:lower:]' '[:upper:]')
echo "Disk layout (${image_type_upper}):"
printf "  Capacity  : %d blocks\n" "$capacity"
printf "  PRG       : %d blocks (%d bytes)\n" "$prg_blocks" "$prg_bytes"
printf "  Scenes    : %d raw top-half files\n" "$total_scenes"
printf "  Scene data: %d blocks (%d bytes)\n" "$scene_total_blocks" "$scene_total_bytes"
printf "  Used      : %d / %d blocks\n" "$(( prg_blocks + scene_total_blocks ))" "$capacity"

# ── Create image ──────────────────────────────────────────────────────────────
mkdir -p "$(dirname "$IMAGE_PATH")"
rm -f "$IMAGE_PATH"

"$C1541_BIN" -format "dark64,64" "${IMAGE_TYPE}" "$IMAGE_PATH" >/dev/null

# Main game executable
"$C1541_BIN" "$IMAGE_PATH" -write "$PRG_PATH" DARK64 >/dev/null
echo ""
printf "  %-8s  %d bytes\n" "DARK64" "$prg_bytes"

# ── Write scene files (01..NN) ────────────────────────────────────────────────
for scene_file in "${TMPDIR_SCENES}"/[0-9][0-9](N); do
  scene_name="${scene_file:t}"
  scene_bytes=$(stat -f "%z" "$scene_file")
  "$C1541_BIN" "$IMAGE_PATH" -write "$scene_file" "$scene_name" >/dev/null
  printf "  %-8s  %d bytes\n" "$scene_name" "$scene_bytes"
done

echo ""
echo "Built: ${IMAGE_PATH}  (${total_scenes} scenes as raw top-half files)"
