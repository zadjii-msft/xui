"""Build the Solar mane studies from the original face and six authored silhouettes."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs/specs/branding/solar"
ORIGINAL = ROOT / "docs/specs/branding/01-solar.svg"
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)

MANES = [
    {
        "id": "a-soft-arc",
        "name": "Soft arc",
        "letter": "A",
        "note": "A balanced horseshoe of six soft waves. Short side locks stop above the chin.",
        "paths": [
            "M30 119C17 134 24 151 36 158C33 178 47 191 64 187L80 158C70 149 65 138 64 130Z",
            "M29 109C22 95 30 80 41 76C37 58 51 44 66 49L80 85C70 95 65 109 65 121Z",
            "M76 56C68 39 78 25 94 26C105 12 124 17 125 32L125 67C111 65 98 71 87 82Z",
        ],
    },
    {
        "id": "b-high-crown",
        "name": "High crown",
        "letter": "B",
        "note": "More lift above the forehead, with the shortest lower locks. The most open jawline.",
        "paths": [
            "M32 113C19 126 23 140 35 147C35 164 50 176 68 168L79 149C70 141 67 133 65 124Z",
            "M31 103C19 85 29 67 43 63C41 44 55 32 71 40L82 79C72 91 67 105 65 115Z",
            "M80 46C73 28 85 14 101 17C113 5 126 12 125 27L125 66C111 64 99 69 89 77Z",
        ],
    },
    {
        "id": "c-round-petals",
        "name": "Round petals",
        "letter": "C",
        "note": "Plumper, rounder segments with a gentle wave at each tip. A softer mascot silhouette.",
        "paths": [
            "M30 120C12 134 18 151 32 159C24 181 47 199 65 183L82 158C70 148 67 138 65 129Z",
            "M26 108C11 90 23 70 40 70C31 48 51 31 68 47L82 81C71 93 67 106 65 121Z",
            "M76 55C62 36 78 17 95 24C108 7 126 18 125 33L125 67C109 66 98 71 86 83Z",
        ],
    },
    {
        "id": "d-tidal-wave",
        "name": "Tidal wave",
        "letter": "D",
        "note": "A more visible ripple along every segment. Still six pieces, not a ring of small spikes.",
        "paths": [
            "M29 120C15 127 26 139 21 146C15 159 33 160 33 169C32 184 51 193 65 181L82 157C72 149 66 137 64 130Z",
            "M27 108C14 99 27 87 29 80C26 69 43 67 41 59C39 42 58 38 68 49L82 84C71 94 66 108 65 121Z",
            "M77 55C65 46 78 36 78 29C79 16 95 26 101 18C113 7 127 21 125 33L125 67C111 66 99 71 88 81Z",
        ],
    },
    {
        "id": "e-close-crop",
        "name": "Close crop",
        "letter": "E",
        "note": "A narrower mane with broad, simple segments. Less volume around the cheeks.",
        "paths": [
            "M42 120C29 131 35 144 43 150C38 167 52 181 67 172L81 155C72 145 68 138 65 129Z",
            "M40 110C29 95 38 81 49 77C43 60 56 48 70 54L83 86C74 98 69 110 67 121Z",
            "M79 59C72 43 83 31 97 31C108 19 125 25 125 39L125 67C112 65 101 71 89 81Z",
        ],
    },
    {
        "id": "f-swept-locks",
        "name": "Swept locks",
        "letter": "F",
        "note": "Longer curved shoulders sweep upward into the crown. More movement, without a beard of mane.",
        "paths": [
            "M29 118C12 140 25 157 40 157C39 177 53 189 68 178C54 169 68 164 81 157C71 146 66 137 64 129Z",
            "M27 107C16 82 32 69 48 69C39 49 54 33 70 43C62 56 76 63 83 81C73 93 68 105 65 121Z",
            "M77 53C68 28 91 14 105 23C109 10 123 11 125 25L125 67C111 65 98 71 87 81C87 64 79 68 77 53Z",
        ],
    },
]

PALETTES = [
    {
        "id": "rainbow", "name": "Rainbow", "label": "Canonical",
        "mane": ["#E56868", "#ED9652", "#EACB56", "#64B986", "#60A4D8", "#9A80CF"],
        "face": ["#EAA83B", "#F8CC70", "#7A3E20", "#452B24", "#FFE7A9"],
    },
    {
        "id": "idle", "name": "Slate", "label": "Idle",
        "mane": ["#64748B"] * 6,
        "face": ["#94A3B8", "#D4DCE5", "#475569", "#243246", "#F1F5F9"],
    },
    {
        "id": "active", "name": "Blue", "label": "Active",
        "mane": ["#3784D8"] * 6,
        "face": ["#78B2E9", "#C6E2FA", "#24609B", "#133D6B", "#E9F5FF"],
    },
    {
        "id": "success", "name": "Green", "label": "Success",
        "mane": ["#309A72"] * 6,
        "face": ["#79BF9D", "#CBE9D5", "#257556", "#154D39", "#EFFAF1"],
    },
    {
        "id": "warning", "name": "Amber", "label": "Warning",
        "mane": ["#D49A23"] * 6,
        "face": ["#E3B958", "#F4DDA3", "#936715", "#614514", "#FFF5D8"],
    },
    {
        "id": "error", "name": "Red", "label": "Error",
        "mane": ["#D75C66"] * 6,
        "face": ["#EC9BA0", "#F6CED1", "#AB424B", "#732D36", "#FFF0F1"],
    },
    {
        "id": "paused", "name": "Violet", "label": "Paused",
        "mane": ["#8B69C9"] * 6,
        "face": ["#B49BDE", "#E0D5F3", "#6D4CA5", "#48336B", "#F5EFFF"],
    },
]
FACE_COLORS = PALETTES[0]["face"]


def original_face():
    root = ET.parse(ORIGINAL).getroot()
    paths = root.findall(f"{{{NS}}}path")
    if len(paths) != 8:
        raise ValueError("Solar source changed: expected one mane path and seven face paths.")
    return paths[1:]


def render(mane, palette, face):
    svg = ET.Element(f"{{{NS}}}svg", {
        "viewBox": "0 0 256 256", "role": "img", "aria-labelledby": "title desc",
    })
    ET.SubElement(svg, f"{{{NS}}}title", {"id": "title"}).text = (
        f"Solar {mane['name']} - {palette['label']}"
    )
    ET.SubElement(svg, f"{{{NS}}}desc", {"id": "desc"}).text = (
        f"The original front-facing Solar lion face with six gently wavy mane segments "
        f"above and beside the head, leaving the chin clear. {palette['name']} palette."
    )
    composition = ET.SubElement(svg, f"{{{NS}}}g", {"transform": "translate(0 12)"})
    segments = ET.SubElement(composition, f"{{{NS}}}g", {"id": "mane"})
    paths = mane["paths"] + list(reversed(mane["paths"]))
    for index, (geometry, color) in enumerate(zip(paths, palette["mane"], strict=True)):
        attributes = {"d": geometry, "fill": color, "id": f"segment-{index + 1}"}
        if index >= 3:
            attributes["transform"] = "translate(256 0) scale(-1 1)"
        ET.SubElement(segments, f"{{{NS}}}path", attributes)
    face_group = ET.SubElement(composition, f"{{{NS}}}g", {"id": "face"})
    colors = dict(zip(FACE_COLORS, palette["face"], strict=True))
    for source in face:
        element = deepcopy(source)
        for attribute in ("fill", "stroke"):
            color = element.get(attribute)
            if color and color != "none":
                element.set(attribute, colors[color])
        face_group.append(element)
    ET.indent(svg, space="  ")
    return ET.tostring(svg, encoding="unicode") + "\n"


def outputs():
    face = original_face()
    files = {
        f"{mane['id']}-{palette['id']}.svg": render(mane, palette, face)
        for mane in MANES for palette in PALETTES
    }
    metadata = {
        "shapes": [
            {key: value for key, value in mane.items() if key != "paths"}
            for mane in MANES
        ],
        "palettes": [
            {key: palette[key] for key in ("id", "name", "label")}
            for palette in PALETTES
        ],
    }
    files["studies.js"] = "window.solarStudies = " + json.dumps(metadata, indent=2) + ";\n"
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report stale assets without changing files.")
    args = parser.parse_args()
    files = outputs()
    if args.check:
        stale = [
            name for name, content in files.items()
            if not (OUTPUT / name).exists() or (OUTPUT / name).read_text(encoding="utf-8") != content
        ]
        unexpected = sorted(path.name for path in OUTPUT.glob("*.svg") if path.name not in files)
        if stale or unexpected:
            for name in stale:
                print(f"Missing or stale Solar asset: {name}")
            for name in unexpected:
                print(f"Unexpected Solar asset: {name}")
            return 1
        print("Solar assets are current: six silhouettes, seven palettes, 42 SVGs.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote 42 Solar SVGs and gallery metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
