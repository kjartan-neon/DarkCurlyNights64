# DarkCurlyNights64 — Agent Handoff (C64 + GB)

This file summarizes project context, workflow, and troubleshooting knowledge so a new coding agent (or developer) can be productive quickly on a new machine.

---

## 1) Project overview

- **Project**: `DarkCurlyNights64`
- **Primary targets**:
  - **Commodore 64** build + `.d64` disk image
  - **Game Boy (DMG)** port via GBDK-2020
- **Shared narrative source**:
  - `story.md` → compiled into:
    - `story_compiled.json`
    - `src/generated_story.h`

## 1.1) First 5 commands (bootstrap)

Run these from a fresh terminal on a new machine:

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
python3 tools/story_compiler.py
ninja -C build && ./tools/build_disk_image.sh --type d64
make -f Makefile_gb assets
make -f Makefile_gb
```

This gets both targets rebuilt quickly (C64 + GB) after syncing the repo.

---

## 2) Key repository paths

- `src/main.c` — C64 runtime
- `src/gb/main_gb.c` — GB runtime
- `src/generated_story.h` — generated scene/option data used by both ports
- `tools/story_compiler.py` — story markdown compiler
- `tools/rebuild_images.sh` — C64 image pipeline
- `tools/build_disk_image.sh` — C64 disk image builder
- `tools/generate_gb_asset.py` — GB bitmap asset generation
- `Makefile_gb` — GB build entry
- `build/DarkCurlyNights64.d64` — C64 output
- `build/gb/DarkCurlyNights_gb.gb` — GB output

---

## 3) Environment setup (new machine)

## 3.1 C64 side

Required:
- `ninja`
- llvm-mos / VS64 toolchain expected by existing `build/` config
- VICE (`x64sc`) for emulator testing

Primary C64 runtime file:
- `src/main.c`

What `src/main.c` handles:
- configuring VIC-II hires bitmap mode
- loading scene bitmaps from disk into bitmap RAM
- rendering story title/description/options in lower rows
- handling keyboard-driven scene transitions

Mac path used in this project:
- `/Applications/vice-arm64-gtk3-3.10/bin/x64sc`

## 3.2 GB side

Required:
- **GBDK-2020** installed under path expected by `Makefile_gb`:
  - `/usr/local/local/lib/gbdk`
- Python venv for asset script (used by `make -f Makefile_gb assets`)

If GBDK is in a different location, update `Makefile_gb`:
- `GBDK := <your-path>`

---

## 4) Standard workflows

## 4.1 Story compile

```zsh
python3 tools/story_compiler.py
```

Outputs:
- `story_compiled.json`
- `src/generated_story.h`

Notes:
- The story pipeline was simplified: no scene-grant flags are emitted anymore.
- `StoryScene` now has: `id, title, description, first_option, option_count`.

## 4.2 C64 build + disk

```zsh
ninja -C build
./tools/build_disk_image.sh --type d64
```

Output:
- `build/DarkCurlyNights64.d64`

## 4.3 C64 image pipeline

```zsh
./tools/rebuild_images.sh
```

Generates/updates C64 image assets and `build/scenes_pack.bin`.

## 4.4 GB assets + build

```zsh
make -f Makefile_gb assets
make -f Makefile_gb
```

Output:
- `build/gb/DarkCurlyNights_gb.gb`

---

## 5) Emulator run/test commands

## 5.1 C64 in VICE

```zsh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -autostart build/DarkCurlyNights64.d64
```

## 5.2 C64 headless-ish smoke log

```zsh
pkill -f x64sc || true
rm -f /tmp/darkcurlynights64-vice-verbose.log
(/Applications/vice-arm64-gtk3-3.10/bin/x64sc --verbose -autostart "build/DarkCurlyNights64.d64" > /tmp/darkcurlynights64-vice-verbose.log 2>&1 &) \
  && sleep 15 \
  && pkill -f x64sc || true
grep -n "AUTOSTART:\|JAM\|BITMAP LOAD FAILED\|LOAD ERROR" /tmp/darkcurlynights64-vice-verbose.log | tail -80
```

Expected:
- `AUTOSTART` lines present
- `JAM_COUNT=0`
- `LOAD_ERROR_COUNT=0`

Note:
- Warnings like missing `2000/4000/CMDHD` ROMs were observed; they are not blocking normal 1541 autostart in this project.

---

## 6) Commodore 64 side: important implementation notes

File: `src/main.c`

## 6.1 Screen and memory layout

- Bitmap RAM is used at `0xE000` (`BITMAP_RAM`), with color/screen support from `0xC000` and `0xD800`.
- The UI is split logically:
  - image area in top rows
  - story text and options in lower rows
- Key layout constants to keep aligned with rendering behavior:
  - `DESC_ROW_START`, `DESC_ROWS`
  - `OPTION_ROW_START`, `OPTION_ROWS`

## 6.2 Scene bitmap loading strategy

- Scene IDs map to two-digit bitmap files (`01`..`30`) loaded via KERNAL APIs.
- Loader tries multiple IEC device candidates, using `KERNAL_LAST_DEVICE` first, then `8/9/10/11`.
- The runtime tracks `loaded_bitmap_scene_id` to avoid unnecessary reloads when re-rendering the same scene.
- If loading fails, `BITMAP LOAD FAILED` is shown in the top text area.

## 6.3 Input and navigation behavior

- `SPACE`: cycle description pages (when a scene text has multiple pages).
- `1`..`9`: pick a scene option (bounded by `scene->option_count` and `OPTION_ROWS`).
- `R`: restart when on end scenes (`option_count == 0`).
- `N`: debug skip to next scene.
- `Q`: quit.

## 6.4 Story branching and conditions

- Option routing uses `resolve_target(...)` with `StoryOption.condition`.
- Active condition checks on C64:
  - `STORY_COND_HAS_MULTITOOL`
  - `STORY_COND_HAS_COFFEE`
- Grants were removed from generated story output, so there is no `grants_flags` path anymore.

## 6.5 C64 asset and disk workflow notes

- `./tools/rebuild_images.sh` prepares C64 scene image assets and updates `build/scenes_pack.bin`.
- `ninja -C build` compiles the C64 program.
- `./tools/build_disk_image.sh --type d64` assembles the bootable `.d64` image.
- If scenes change visually, run image rebuild first, then build + disk steps.

---

## 7) GB port: important implementation notes

File: `src/gb/main_gb.c`

## 7.1 Font + tile budget decisions

- Goal was mixed-case UI **and** 9 rows of image.
- Using full `font_ibm` directly consumed too many tiles.
- Implemented compact curated UI font tile loading from `font_ibm_fixed_tiles`.
- UI character set is constrained and loaded into tile range `0..75`.
- Scene bitmap tiles use `76..255` (`180` tiles = `20*9` rows).

Important fix made:
- Corrected glyph source indexing in loader (`source_tile = ascii`, not `ascii - 32`).

## 7.2 Selection menu behavior

- Selection screen optimized so moving cursor updates only indicator area (numbers / `*`) rather than redrawing full screen each keypress.
- Menu supports up to **4 entries** so scenes with 3 options can still include `Read scene again` as item 4.

## 7.3 End-of-part screen

- After final scene (`option_count == 0`), GB now shows a dedicated screen:
  - intro image background
  - text lines:
    - `End of part 1`
    - (blank line)
    - `Elara waits for you`
    - `in part 2`

## 7.4 Stability fixes

- Bounds checks added around menu/option access to avoid invalid option dereferences.
- Cached active scene/menu state used to avoid wrong-scene option redraw behavior.

---

## 8) C64/GB shared story-model changes

Recent simplification in `tools/story_compiler.py` and generated header:
- Removed story flag emission from generated C header (`STORY_FLAG_*` removed)
- Removed `grants_flags` from `StoryScene`
- Runtime logic updated accordingly

`StoryOption.condition` still exists and uses:
- `STORY_COND_NONE`
- `STORY_COND_HAS_MULTITOOL`
- `STORY_COND_HAS_COFFEE`

---

## 9) Known gotchas

- If C64 scene images fail to appear, verify the `.d64` was rebuilt after image generation:
  - `./tools/rebuild_images.sh`
  - `ninja -C build`
  - `./tools/build_disk_image.sh --type d64`
- If VICE autostart opens but content is stale, close VICE and relaunch with the section 5.1 command so the fresh disk image is mounted.

- VS Code Problems may show include-path errors for GB headers (`gb/gb.h`) even when `make -f Makefile_gb` builds fine. Treat as editor config issue unless build fails.
- If `Read scene again` does not appear as item 4, verify:
  - `MAX_MENU_OPTIONS == 4`
  - `OPTION_SLOT_COUNT == 4`
- If GB text turns into symbols/noise:
  - verify font loader conversion and source index logic in `load_ui_font_tiles()`.

---

## 10) Quick recovery checklist

1. Compile story:
   - `python3 tools/story_compiler.py`
2. Rebuild C64 + disk:
   - `ninja -C build`
   - `./tools/build_disk_image.sh --type d64`
3. Rebuild GB:
   - `make -f Makefile_gb assets`
   - `make -f Makefile_gb`
4. Smoke test C64 in VICE:
   - run the log command in section 5.2
5. If GB menu behaves strangely:
   - inspect `src/gb/main_gb.c` menu cache + indicator update helpers

---

## 11) Suggested next improvements (optional)

- Add GB emulator command examples to `README.md` (SameBoy/BGB/Emulicious).
- Add a tiny GB UI-font self-test toggle to quickly validate glyph mapping after changes.
- Add CI script snippets for story compile + C64 build + GB build.
