#!/usr/bin/env zsh
# build_disk_image.sh — Assembles a C64 disk image for DarkCurlyNights64.
#
# D64 capacity: 664 free blocks after format.
# Scene images are compressed into one indexed pack file ("00").
# This avoids per-file block slack and allows all 30 scenes on one D64.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
C1541_BIN="${C1541_BIN:-/Applications/vice-arm64-gtk3-3.10/bin/c1541}"

IMAGE_TYPE="d64"
IMAGE_PATH=""
PRG_PATH="${BUILD_DIR}/DarkCurlyNights64.prg"
COMPRESSOR="${REPO_ROOT}/tools/compress_scenes_rle.py"

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
if [[ ! -f "$COMPRESSOR" ]]; then
  echo "Compressor script not found: ${COMPRESSOR}" >&2; exit 1
fi

# ── Collect source scene files ────────────────────────────────────────────────
all_scenes=("${REPO_ROOT}"/SCENE??.BMP(N))
total_scenes=${#all_scenes[@]}

if (( total_scenes == 0 )); then
  echo "No SCENE??.BMP files found in ${REPO_ROOT}" >&2; exit 1
fi

# ── Build compressed scene pack ───────────────────────────────────────────────
TMPDIR_SCENES="$(mktemp -d)"
SCENE_PACK_FILE="${TMPDIR_SCENES}/00"
trap 'rm -rf "$TMPDIR_SCENES"' EXIT

python3 "$COMPRESSOR" --input-dir "$REPO_ROOT" --output-file "$SCENE_PACK_FILE" >/dev/null
if [[ ! -f "$SCENE_PACK_FILE" ]]; then
  echo "Compression failed: pack file not generated." >&2; exit 1
fi

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
pack_bytes=$(stat -f "%z" "$SCENE_PACK_FILE")
pack_blocks=$(ceil_blocks "$pack_bytes")

if (( pack_blocks > remaining )); then
  echo "Compressed pack does not fit on ${IMAGE_TYPE}: ${pack_blocks} blocks needed, ${remaining} available." >&2
  exit 1
fi

image_type_upper=$(echo "$IMAGE_TYPE" | tr '[:lower:]' '[:upper:]')
echo "Disk layout (${image_type_upper}):"
printf "  Capacity  : %d blocks\n" "$capacity"
printf "  PRG       : %d blocks (%d bytes)\n" "$prg_blocks" "$prg_bytes"
printf "  Scenes    : %d compressed in pack\n" "$total_scenes"
printf "  Pack (00) : %d blocks (%d bytes)\n" "$pack_blocks" "$pack_bytes"
printf "  Used      : %d / %d blocks\n" "$(( prg_blocks + pack_blocks ))" "$capacity"

# ── Create image ──────────────────────────────────────────────────────────────
mkdir -p "$(dirname "$IMAGE_PATH")"
rm -f "$IMAGE_PATH"

"$C1541_BIN" -format "dark64,64" "${IMAGE_TYPE}" "$IMAGE_PATH" >/dev/null

# Main game executable
"$C1541_BIN" "$IMAGE_PATH" -write "$PRG_PATH" DARK64 >/dev/null
echo ""
printf "  %-8s  %d bytes\n" "DARK64" "$prg_bytes"

# ── Write compressed scene pack ───────────────────────────────────────────────
"$C1541_BIN" "$IMAGE_PATH" -write "$SCENE_PACK_FILE" 00 >/dev/null
printf "  %-8s  %d bytes\n" "00" "$pack_bytes"

echo ""
echo "Built: ${IMAGE_PATH}  (${total_scenes} scenes in compressed pack)"
