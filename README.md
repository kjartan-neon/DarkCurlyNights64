# DarkCurlyNights64

C64 interactive story prototype using VS64 + llvm-mos.

## Story pipeline

`story.md` is the authoring source.

Generate machine-readable data + C data header:

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
python3 tools/story_compiler.py
```

Outputs:

- `story_compiled.json` (machine-readable story graph)
- `src/generated_story.h` (generated C data used by the game)

## Build

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
ninja -C build
```

## Run in VICE

```zsh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64/build/DarkCurlyNights64.prg
```

## Controls

- `1`..`4`: choose option
- `R`: restart after ending scene
- `Q`: quit

## Screen layout

- Top ~75% (rows 0-17): color-bar raster-style graphics area (demo bars)
- Bottom ~25% (rows 18-24): scene title, description, and player options
