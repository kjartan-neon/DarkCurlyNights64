# DarkCurlyNights64 C64 v0.1.0

First public C64 release of `DarkCurlyNights64`.

Wake into freezing gel, cracked glass, and an alarm-lit stasis pod at the bottom of a dead world. This release packages the Commodore 64 edition of the interactive story as a ready-to-run disk image and standalone PRG.

## Included files

- `DarkCurlyNights64-c64-v0.1.0.d64` — bootable C64 disk image
- `DarkCurlyNights64-c64-v0.1.0.prg` — standalone C64 program file

## Highlights

- Ready-to-run C64 disk build
- Disk label set to `64`
- Program entry stored as `dark64`
- Scene loading from per-scene disk files (`01`..`30`)
- Release packaging automation for future C64 builds

## How to run

On macOS with VICE installed:

```zsh
/Applications/vice-arm64-gtk3-3.10/bin/x64sc -autostart DarkCurlyNights64-c64-v0.1.0.d64
```

## Controls

- `1`..`4` — choose option
- `R` — restart after ending scene
- `Q` — quit

## Notes

This tag corresponds to the first automated C64 release bundle under `releases/c64/`.
