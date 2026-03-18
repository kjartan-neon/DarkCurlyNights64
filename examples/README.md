# Example config snapshots

This folder contains known-good snapshots of local development configuration.

## Contents

- `vscode/settings.example.json`
- `vscode/tasks.example.json`
- `vscode/launch.example.json`
- `vscode/c_cpp_properties.example.json`
- `build/build.ninja.example`
- `build/compile_commands.example.json`

## Quick recovery on this machine

Use this if `.vscode` or `build` config gets corrupted.

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
cp examples/vscode/settings.example.json .vscode/settings.json
cp examples/vscode/tasks.example.json .vscode/tasks.json
cp examples/vscode/launch.example.json .vscode/launch.json
cp examples/vscode/c_cpp_properties.example.json .vscode/c_cpp_properties.json
mkdir -p build
cp examples/build/build.ninja.example build/build.ninja
cp examples/build/compile_commands.example.json build/compile_commands.json
ninja -C build
```

## Starting on another machine

The build snapshot files can include absolute paths from the original machine.
Use this flow instead:

```zsh
cd /path/to/DarkCurlyNights64
cp examples/vscode/settings.example.json .vscode/settings.json
cp examples/vscode/tasks.example.json .vscode/tasks.json
cp examples/vscode/launch.example.json .vscode/launch.json
cp examples/vscode/c_cpp_properties.example.json .vscode/c_cpp_properties.json
```

Then in VS Code:

1. Install VS64 + llvm-mos toolchain + VICE
2. Update paths in `.vscode/settings.json`
3. Run the default build task (`Build: Debug`) to regenerate `build/build.ninja`
4. Verify with `ninja -C build`

## Keep snapshots fresh

After you confirm a known-good setup, refresh snapshots:

```zsh
cd /Users/kjartan.bjorndal.michalsen/Documents/GitHub/DarkCurlyNights64
cp .vscode/settings.json examples/vscode/settings.example.json
cp .vscode/tasks.json examples/vscode/tasks.example.json
cp .vscode/launch.json examples/vscode/launch.example.json
cp .vscode/c_cpp_properties.json examples/vscode/c_cpp_properties.example.json
cp build/build.ninja examples/build/build.ninja.example
cp build/compile_commands.json examples/build/compile_commands.example.json
```
