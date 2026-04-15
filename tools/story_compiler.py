#!/usr/bin/env python3
import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path


COND_NONE = "none"
COND_HAS_MULTITOOL = "has_multitool"
COND_HAS_COFFEE = "has_coffee"


@dataclass
class Option:
    letter: str
    text: str
    raw: str
    target_scene: int | None = None
    alt_target_scene: int | None = None
    condition: str = COND_NONE
    target_ref_letter: str | None = None


@dataclass
class Scene:
    id: int
    title: str
    description: str
    options: list[Option]


SCENE_RE = re.compile(r"^SCENE\s+(\d+):\s*(.+)$", re.IGNORECASE)
OPTION_RE = re.compile(r"^\s*\*\s*Option\s+([A-Z]):\s*(.+)$", re.IGNORECASE)


def normalize_spaces(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def escape_c_string(text: str) -> str:
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", " ")
    )


def parse_option(raw: str) -> tuple[str, int | None, int | None, str, str | None]:
    text = raw.strip()
    display = text
    if "(" in display:
        display = display.split("(", 1)[0].rstrip(" .")

    lower = text.lower()
    scene_refs = [int(n) for n in re.findall(r"SCENE\s+(\d+)", text, re.IGNORECASE)]

    condition = COND_NONE
    target_scene = scene_refs[0] if scene_refs else None
    alt_target_scene = scene_refs[1] if len(scene_refs) > 1 else None
    target_ref_letter = None

    if re.search(r"\b(go\s+back\s+to\s+a|go\s+to\s+a|return\s+to\s+a)\b", lower):
        target_ref_letter = "A"

    cond_match = re.search(
        r"Go\s+to\s+SCENE\s+(\d+).*Otherwise.*SCENE\s+(\d+)",
        text,
        re.IGNORECASE,
    )
    if cond_match:
        target_scene = int(cond_match.group(1))
        alt_target_scene = int(cond_match.group(2))

    if "multi-tool" in lower or "multitool" in lower:
        condition = COND_HAS_MULTITOOL
    elif "coffee" in lower:
        condition = COND_HAS_COFFEE

    if target_scene is None:
        from_match = re.search(r"from\s+(?:SCENE\s+)?(\d+)", text, re.IGNORECASE)
        if from_match:
            target_scene = int(from_match.group(1))

    return normalize_spaces(display), target_scene, alt_target_scene, condition, target_ref_letter


def parse_story(markdown: str) -> list[Scene]:
    lines = markdown.splitlines()
    scenes: list[Scene] = []
    idx = 0

    while idx < len(lines):
        line = lines[idx].strip()
        m_scene = SCENE_RE.match(line)
        if not m_scene:
            idx += 1
            continue

        scene_id = int(m_scene.group(1))
        scene_title = normalize_spaces(m_scene.group(2))
        idx += 1

        while idx < len(lines) and lines[idx].strip() == "":
            idx += 1

        if idx < len(lines) and lines[idx].strip().lower().startswith("description:"):
            idx += 1

        desc_lines: list[str] = []
        options: list[Option] = []

        while idx < len(lines):
            probe = lines[idx].strip()
            if SCENE_RE.match(probe):
                break

            m_opt = OPTION_RE.match(lines[idx])
            if m_opt:
                letter = m_opt.group(1).upper()
                raw_opt = normalize_spaces(m_opt.group(2))
                display, target, alt_target, cond, ref = parse_option(raw_opt)
                options.append(
                    Option(
                        letter=letter,
                        text=display,
                        raw=raw_opt,
                        target_scene=target,
                        alt_target_scene=alt_target,
                        condition=cond,
                        target_ref_letter=ref,
                    )
                )
            else:
                if probe:
                    desc_lines.append(probe)
            idx += 1

        description = normalize_spaces(" ".join(desc_lines))
        scenes.append(Scene(scene_id, scene_title, description, options))

    scene_by_id = {s.id: s for s in scenes}
    for scene in scenes:
        option_by_letter = {opt.letter: opt for opt in scene.options}
        for opt in scene.options:
            if opt.target_ref_letter:
                ref_opt = option_by_letter.get(opt.target_ref_letter)
                if ref_opt and ref_opt.target_scene:
                    opt.target_scene = ref_opt.target_scene
                else:
                    opt.target_scene = scene.id
            if opt.target_scene is None:
                opt.target_scene = scene.id
            if opt.alt_target_scene is not None and opt.alt_target_scene not in scene_by_id:
                opt.alt_target_scene = None
            if opt.target_scene not in scene_by_id:
                opt.target_scene = scene.id

    return scenes


def emit_json(scenes: list[Scene], out_path: Path) -> None:
    payload = {
        "scenes": [
            {
                "id": s.id,
                "title": s.title,
                "description": s.description,
                "options": [
                    {
                        "letter": o.letter,
                        "text": o.text,
                        "target_scene": o.target_scene,
                        "alt_target_scene": o.alt_target_scene,
                        "condition": o.condition,
                        "raw": o.raw,
                    }
                    for o in s.options
                ],
            }
            for s in scenes
        ]
    }
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def cond_to_c(cond: str) -> str:
    if cond == COND_HAS_MULTITOOL:
        return "STORY_COND_HAS_MULTITOOL"
    if cond == COND_HAS_COFFEE:
        return "STORY_COND_HAS_COFFEE"
    return "STORY_COND_NONE"


def emit_header(scenes: list[Scene], out_path: Path) -> None:
    option_items: list[tuple[int, Option]] = []
    for s in scenes:
        for o in s.options:
            option_items.append((s.id, o))

    lines: list[str] = []
    lines.append("#ifndef GENERATED_STORY_H")
    lines.append("#define GENERATED_STORY_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define STORY_COND_NONE 0")
    lines.append("#define STORY_COND_HAS_MULTITOOL 1")
    lines.append("#define STORY_COND_HAS_COFFEE 2")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char* text;")
    lines.append("    uint8_t target_scene;")
    lines.append("    uint8_t alt_target_scene;")
    lines.append("    uint8_t condition;")
    lines.append("} StoryOption;")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    uint8_t id;")
    lines.append("    const char* title;")
    lines.append("    const char* description;")
    lines.append("    uint8_t first_option;")
    lines.append("    uint8_t option_count;")
    lines.append("} StoryScene;")
    lines.append("")
    lines.append(f"static const uint8_t STORY_SCENE_COUNT = {len(scenes)};")
    lines.append(f"static const uint8_t STORY_OPTION_COUNT = {len(option_items)};")
    lines.append("")
    lines.append("static const StoryOption STORY_OPTIONS[] = {")
    for _, o in option_items:
        alt = o.alt_target_scene if o.alt_target_scene is not None else 255
        lines.append(
            f'    {{"{escape_c_string(o.text)}", {o.target_scene}, {alt}, {cond_to_c(o.condition)}}},'
        )
    lines.append("};")
    lines.append("")
    lines.append("static const StoryScene STORY_SCENES[] = {")
    first_idx = 0
    for s in scenes:
        lines.append(
            f'    {{{s.id}, "{escape_c_string(s.title)}", "{escape_c_string(s.description)}", {first_idx}, {len(s.options)}}},'
        )
        first_idx += len(s.options)
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Compile story markdown into JSON and C header")
    parser.add_argument("--input", default="story.md")
    parser.add_argument("--json-out", default="story_compiled.json")
    parser.add_argument("--header-out", default="src/generated_story.h")
    args = parser.parse_args()

    input_path = Path(args.input)
    markdown = input_path.read_text(encoding="utf-8")
    scenes = parse_story(markdown)

    emit_json(scenes, Path(args.json_out))
    emit_header(scenes, Path(args.header_out))

    print(f"Compiled {len(scenes)} scenes to {args.json_out} and {args.header_out}")


if __name__ == "__main__":
    main()
