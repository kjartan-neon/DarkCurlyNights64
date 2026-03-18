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

## Image pipeline

Use this workflow when adding scene images:

1. Put source images in `gfx/originals/` named `scene01.png`, `scene02.png`, etc.
2. Crop/fit them into `gfx/images/`.
3. Generate C64 temporary images in `gfx/c64/` and final assets/headers.

Run commands:

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
python3 tools/crop_images.py
python3 tools/generate_scene_asset.py
```

Or run the full image pipeline in one command:

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
./tools/rebuild_images.sh
```

VS Code task: `Images: Rebuild Pipeline`

Generated files:

- `gfx/images/sceneNN.png` (cropped 600x212)
- `gfx/c64/sceneNN_c64.png` (temporary 1-bit 320x100 C64 image)
- `SCENENN.BMP` and `build/SCENENN.BMP`
- `src/sceneNN_bitmap.h`

## Build

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
ninja -C build
```

## Config backup templates

Known-good VS Code and build configuration snapshots are stored under:

- `examples/vscode/`
- `examples/build/`

These files are examples/snapshots to recover quickly if local config is broken.

### Restore working VS Code config files

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
cp examples/vscode/settings.example.json .vscode/settings.json
cp examples/vscode/tasks.example.json .vscode/tasks.json
cp examples/vscode/launch.example.json .vscode/launch.json
cp examples/vscode/c_cpp_properties.example.json .vscode/c_cpp_properties.json
```

### Restore build metadata files (same machine / same path)

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
mkdir -p build
cp examples/build/build.ninja.example build/build.ninja
cp examples/build/compile_commands.example.json build/compile_commands.json
```

### Starting on another computer

`examples/build/*` may contain absolute paths from this machine. On a different machine:

1. Restore `.vscode` files from `examples/vscode/*`
2. Update emulator / llvm-mos paths in `.vscode/settings.json`
3. Regenerate build files by opening the folder in VS Code with VS64 installed and running the default build task

For more details, see `examples/README.md`.

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
