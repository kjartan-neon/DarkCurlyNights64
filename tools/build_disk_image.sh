#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
C1541_BIN="/Applications/vice-arm64-gtk3-3.10/bin/c1541"

IMAGE_TYPE="d81"
IMAGE_PATH=""
PRG_PATH="$BUILD_DIR/DarkCurlyNights64.prg"
PACK_PATH="$REPO_ROOT/SCENES.BIN"

D64_MAX_PAYLOAD=168000
D81_MAX_PAYLOAD=800000

usage() {
  cat <<'EOF'
Usage: tools/build_disk_image.sh [--type d81|d64] [--output /abs/path/image] [--prg /abs/path/prg] [--pack /abs/path/SCENES.BIN]

Defaults:
  --type   d81
  --output <repo>/build/DarkCurlyNights64.<type>
  --prg    <repo>/build/DarkCurlyNights64.prg
  --pack   <repo>/SCENES.BIN
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --type)
      IMAGE_TYPE="$2"
      shift 2
      ;;
    --output)
      IMAGE_PATH="$2"
      shift 2
      ;;
    --prg)
      PRG_PATH="$2"
      shift 2
      ;;
    --pack)
      PACK_PATH="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$IMAGE_TYPE" != "d64" && "$IMAGE_TYPE" != "d81" ]]; then
  echo "Unsupported image type: $IMAGE_TYPE (expected d64 or d81)" >&2
  exit 1
fi

if [[ -z "$IMAGE_PATH" ]]; then
  IMAGE_PATH="$BUILD_DIR/DarkCurlyNights64.$IMAGE_TYPE"
fi

if [[ ! -x "$C1541_BIN" ]]; then
  echo "c1541 not found at: $C1541_BIN" >&2
  exit 1
fi

if [[ ! -f "$PRG_PATH" ]]; then
  echo "PRG not found: $PRG_PATH" >&2
  echo "Run: ninja -C $BUILD_DIR" >&2
  exit 1
fi

if [[ ! -f "$PACK_PATH" ]]; then
  echo "Pack file not found: $PACK_PATH" >&2
  echo "Run: python3 $REPO_ROOT/tools/build_scene_pack.py --input-dir $REPO_ROOT --output $REPO_ROOT/SCENES.BIN --copy-to-build" >&2
  exit 1
fi

PRG_SIZE=$(stat -f "%z" "$PRG_PATH")
PACK_SIZE=$(stat -f "%z" "$PACK_PATH")
TOTAL_PAYLOAD=$((PRG_SIZE + PACK_SIZE))

if [[ "$IMAGE_TYPE" == "d64" && $TOTAL_PAYLOAD -gt $D64_MAX_PAYLOAD ]]; then
  echo "Payload too large for D64: ${TOTAL_PAYLOAD} bytes > ${D64_MAX_PAYLOAD} bytes" >&2
  echo "Use --type d81 for full game assets." >&2
  exit 1
fi

if [[ "$IMAGE_TYPE" == "d81" && $TOTAL_PAYLOAD -gt $D81_MAX_PAYLOAD ]]; then
  echo "Payload too large for D81: ${TOTAL_PAYLOAD} bytes > ${D81_MAX_PAYLOAD} bytes" >&2
  exit 1
fi

mkdir -p "$(dirname "$IMAGE_PATH")"
rm -f "$IMAGE_PATH"

DISK_LABEL="DARKNIGHTS,64"
"$C1541_BIN" -format "$DISK_LABEL" "$IMAGE_TYPE" "$IMAGE_PATH" >/dev/null

"$C1541_BIN" "$IMAGE_PATH" -write "$PRG_PATH" DARK64 >/dev/null
"$C1541_BIN" "$IMAGE_PATH" -write "$PACK_PATH" SCENES.BIN >/dev/null

echo "Built disk image: $IMAGE_PATH"
echo "  type: $IMAGE_TYPE"
echo "  prg:  $PRG_PATH ($PRG_SIZE bytes)"
echo "  pack: $PACK_PATH ($PACK_SIZE bytes)"
echo "  total payload: $TOTAL_PAYLOAD bytes"
