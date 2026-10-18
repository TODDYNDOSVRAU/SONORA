#!/usr/bin/env python3
"""Safely add Sonora to a copy of the Samsung S23 audio_effects_sec.xml.

The input is never overwritten; only validated additions are inserted.
"""

from __future__ import annotations

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

NAMESPACE = "http://schemas.android.com/audio/audio_effects_conf/v2_0"
ROOT_TAG = f"{{{NAMESPACE}}}audio_effects_conf"
LIBRARY = {"name": "sonora", "path": "libsonora_spatial.so"}
EFFECT = {
    "name": "sonora_spatial",
    "library": "sonora",
    "uuid": "238b4a9f-6e5c-704b-219f-534f4e4f5242",
}
LIBRARY_LINE = '<library name="sonora" path="libsonora_spatial.so"/>'
EFFECT_LINE = (
    '<effect name="sonora_spatial" library="sonora" '
    'uuid="238b4a9f-6e5c-704b-219f-534f4e4f5242"/>'
)
APPLY_LINE = '<apply effect="sonora_spatial"/>'


def read_text(path: Path) -> str:
    with path.open("r", encoding="utf-8", newline="") as stream:
        return stream.read()


def parse_sections(text: str):
    try:
        root = ET.fromstring(text)
    except ET.ParseError as error:
        raise ValueError(f"Invalid XML: {error}") from error
    if root.tag != ROOT_TAG or root.attrib.get("version") != "2.0":
        raise ValueError("Expected audio_effects_conf version 2.0")

    ns = f"{{{NAMESPACE}}}"
    libraries = root.find(f"{ns}libraries")
    effects = root.find(f"{ns}effects")
    postprocess = root.find(f"{ns}postprocess")
    if libraries is None or effects is None or postprocess is None:
        raise ValueError("Required libraries/effects/postprocess sections are missing")

    music_streams = [
        stream
        for stream in postprocess.findall(f"{ns}stream")
        if stream.attrib.get("type") == "music"
    ]
    if len(music_streams) != 1:
        raise ValueError("Expected exactly one postprocess stream with type=music")
    music_applies = music_streams[0].findall(f"{ns}apply")
    if not any(item.attrib.get("effect") == "sa3d" for item in music_applies):
        raise ValueError("Expected the Samsung sa3d effect in the music stream")
    return libraries, effects, music_applies


def add_before_close(text: str, tag: str, element_line: str) -> str:
    pattern = re.compile(
        rf"(?m)^(?P<indent>[ \t]*)</{re.escape(tag)}>(?P<eol>\r?\n|$)"
    )
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one closing </{tag}> tag")
    match = matches[0]
    eol = match.group("eol") or "\n"
    insertion = f"{match.group('indent')}    {element_line}{eol}"
    return text[: match.start()] + insertion + text[match.start() :]


def add_after_sa3d(text: str) -> str:
    stream_pattern = re.compile(
        r"<stream\b(?=[^>]*\btype\s*=\s*['\"]music['\"])[^>]*>.*?</stream>",
        re.DOTALL,
    )
    streams = list(stream_pattern.finditer(text))
    if len(streams) != 1:
        raise ValueError("Could not identify exactly one XML music stream block")
    stream = streams[0]
    block = stream.group(0)
    apply_pattern = re.compile(
        r"(?m)^(?P<indent>[ \t]*)<apply\b(?=[^>]*\beffect\s*=\s*['\"]sa3d['\"])[^>]*/>(?P<eol>\r?\n|$)"
    )
    applies = list(apply_pattern.finditer(block))
    if len(applies) != 1:
        raise ValueError("Could not identify exactly one sa3d apply line in music stream")
    apply = applies[0]
    eol = apply.group("eol") or "\n"
    insertion = f"{apply.group('indent')}{APPLY_LINE}{eol}"
    updated_block = block[: apply.end()] + insertion + block[apply.end() :]
    return text[: stream.start()] + updated_block + text[stream.end() :]


def merge(text: str) -> str:
    libraries, effects, music_applies = parse_sections(text)

    library_nodes = [node for node in libraries if node.attrib.get("name") == "sonora"]
    if library_nodes and (len(library_nodes) != 1 or library_nodes[0].attrib != LIBRARY):
        raise ValueError("A conflicting or duplicate library named sonora already exists")

    effect_nodes = [node for node in effects if node.attrib.get("name") == "sonora_spatial"]
    if effect_nodes and (len(effect_nodes) != 1 or effect_nodes[0].attrib != EFFECT):
        raise ValueError("A conflicting or duplicate effect named sonora_spatial already exists")

    apply_nodes = [item for item in music_applies if item.attrib.get("effect") == "sonora_spatial"]
    if len(apply_nodes) > 1:
        raise ValueError("Duplicate sonora_spatial apply entries exist in the music stream")

    updated = text
    if not library_nodes:
        updated = add_before_close(updated, "libraries", LIBRARY_LINE)
    if not effect_nodes:
        updated = add_before_close(updated, "effects", EFFECT_LINE)
    if not apply_nodes:
        updated = add_after_sa3d(updated)

    parse_sections(updated)
    return updated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Add the Sonora effect to a copy of Samsung audio_effects_sec.xml."
    )
    parser.add_argument("input_xml", type=Path, help="Original vendor XML; never overwritten")
    parser.add_argument("output_xml", type=Path, help="Destination for the merged XML copy")
    args = parser.parse_args()

    source = args.input_xml.resolve()
    destination = args.output_xml.resolve()
    if source == destination:
        parser.error("input and output must be different files")
    if not source.is_file():
        parser.error("input XML does not exist")
    if destination.exists():
        parser.error("output already exists; choose a new path to avoid overwriting files")

    try:
        updated = merge(read_text(source))
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("x", encoding="utf-8", newline="") as stream:
            stream.write(updated)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"Wrote merged copy: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
