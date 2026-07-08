#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_VERSION_STATE = {
    "latest": "0.0.0",
    "tag_prefix": "c64-v",
    "release_prefix": "DarkCurlyNights64-c64",
}


@dataclass(frozen=True)
class SemVer:
    major: int
    minor: int
    patch: int

    @classmethod
    def parse(cls, value: str) -> "SemVer":
        parts = value.split(".")
        if len(parts) != 3:
            raise ValueError(f"Invalid semantic version: {value}")
        return cls(*(int(part) for part in parts))

    def bump_minor(self) -> "SemVer":
        return SemVer(self.major, self.minor + 1, 0)

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


def run_git(repo_root: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=check,
        text=True,
        capture_output=True,
    )


def get_repo_head(repo_root: Path) -> str:
    return run_git(repo_root, "rev-parse", "HEAD").stdout.strip()


def get_repo_dirty(repo_root: Path) -> bool:
    return bool(run_git(repo_root, "status", "--porcelain", check=True).stdout.strip())


def tag_exists(repo_root: Path, tag_name: str) -> bool:
    result = run_git(repo_root, "tag", "-l", tag_name)
    return bool(result.stdout.strip())


def create_git_tag(repo_root: Path, tag_name: str, message: str) -> str:
    run_git(repo_root, "tag", "-a", tag_name, "-m", message)
    return f"Created git tag {tag_name}"


def load_version_state(version_file: Path) -> dict[str, str]:
    if not version_file.exists():
        return dict(DEFAULT_VERSION_STATE)

    data = json.loads(version_file.read_text())
    merged = dict(DEFAULT_VERSION_STATE)
    merged.update(data)
    return merged


def save_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def copy_artifact(source: Path, destination: Path) -> int:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return destination.stat().st_size


def build_release(args: argparse.Namespace) -> int:
    repo_root = args.repo_root.resolve()
    disk_image = args.disk_image.resolve()
    prg_image = args.prg.resolve()
    releases_root = (repo_root / args.release_dir).resolve()
    version_file = releases_root / "version.json"

    if not disk_image.exists():
        raise FileNotFoundError(f"Disk image not found: {disk_image}")
    if not prg_image.exists():
        raise FileNotFoundError(f"PRG image not found: {prg_image}")

    version_state = load_version_state(version_file)
    current_version = SemVer.parse(version_state["latest"])
    next_version = current_version.bump_minor()
    version_text = str(next_version)
    tag_name = f"{version_state['tag_prefix']}{version_text}"
    release_prefix = version_state["release_prefix"]

    release_dir = releases_root / f"v{version_text}"
    release_dir.mkdir(parents=True, exist_ok=True)

    disk_destination = release_dir / f"{release_prefix}-v{version_text}.d64"
    prg_destination = release_dir / f"{release_prefix}-v{version_text}.prg"

    disk_size = copy_artifact(disk_image, disk_destination)
    prg_size = copy_artifact(prg_image, prg_destination)

    git_head = get_repo_head(repo_root)
    repo_dirty = get_repo_dirty(repo_root)
    tag_status = "skipped"
    tag_message = "Skipped git tag creation: repository has uncommitted changes."

    git_tag_mode = args.git_tag_mode
    if tag_exists(repo_root, tag_name):
        tag_status = "exists"
        tag_message = f"Git tag already exists: {tag_name}"
    elif git_tag_mode == "never":
        tag_message = f"Skipped git tag creation for {tag_name} (git tagging disabled)."
    elif repo_dirty and git_tag_mode != "always":
        tag_message = "Skipped git tag creation: repository has uncommitted changes."
    else:
        tag_status = "created"
        tag_message = create_git_tag(repo_root, tag_name, f"C64 release v{version_text}")

    release_manifest = {
        "platform": "c64",
        "version": version_text,
        "tag": tag_name,
        "git_head": git_head,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "artifacts": [
            {
                "path": str(disk_destination.relative_to(repo_root)),
                "bytes": disk_size,
            },
            {
                "path": str(prg_destination.relative_to(repo_root)),
                "bytes": prg_size,
            },
        ],
        "git_tag": {
            "mode": git_tag_mode,
            "status": tag_status,
            "message": tag_message,
        },
    }

    save_json(release_dir / "release.json", release_manifest)

    version_state["latest"] = version_text
    save_json(version_file, version_state)

    print(f"Prepared C64 release v{version_text}")
    print(f"  Release folder: {release_dir.relative_to(repo_root)}")
    print(f"  Disk image    : {disk_destination.relative_to(repo_root)}")
    print(f"  PRG           : {prg_destination.relative_to(repo_root)}")
    print(f"  Git tag       : {tag_name}")
    print(f"  Tag status    : {tag_message}")

    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prepare a versioned C64 release artifact set.")
    parser.add_argument("--repo-root", type=Path, required=True, help="Repository root")
    parser.add_argument("--disk-image", type=Path, required=True, help="Built .d64 image")
    parser.add_argument("--prg", type=Path, required=True, help="Built .prg executable")
    parser.add_argument(
        "--release-dir",
        default="releases/c64",
        help="Release output directory relative to repo root",
    )
    parser.add_argument(
        "--git-tag-mode",
        choices=("auto", "always", "never"),
        default="auto",
        help="Create git tag automatically, force it, or skip it",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        return build_release(args)
    except Exception as exc:
        print(f"release_c64.py: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))