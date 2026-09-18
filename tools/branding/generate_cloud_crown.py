"""Build six touching-color Cloud Crown studies with the Enamel mane palette."""

import argparse
from copy import deepcopy
import json
import re
import xml.etree.ElementTree as ET

import generate_softling as softling


OUTPUT = softling.ROOT / "docs/specs/branding/cloud-crown"
PALETTE_SOURCE = softling.ROOT / "docs/specs/branding/solar-d2-styles/03-enamel.svg"
JOIN = next(join for join in softling.JOINS if join["id"] == "joined")
ANCHOR = deepcopy(next(contour for contour in softling.CONTOURS if contour["id"] == "cloud"))
ANCHOR.update(
    id="cloud-crown", name="Cloud Crown / Enamel",
    note="The exact M10 silhouette with Enamel mane colors. Use this as the comparison anchor.",
)
CONTOURS = [
    ANCHOR,
    {
        "id": "rounder-billows", "name": "Rounder billows",
        "note": "Larger, smoother billows and fewer side scallops make a softer, simpler cloud.",
        "points": [(52, 196), (15, 124), (54, 53), (128, 18)],
        "arcs": [
            [((24, 199), (9, 162), (15, 124))],
            [((9, 104), (17, 88), (32, 82)), ((25, 58), (39, 44), (54, 53))],
            [((57, 26), (84, 15), (100, 23)), ((111, 12), (122, 12), (128, 18))],
        ],
    },
    {
        "id": "high-crown", "name": "High crown",
        "note": "Raised shoulders and shorter lower locks concentrate the mane above the ears.",
        "points": [(58, 187), (23, 118), (53, 46), (128, 14)],
        "arcs": [
            [((34, 195), (18, 173), (25, 158)), ((13, 150), (13, 129), (23, 118))],
            [((11, 103), (17, 83), (32, 79)), ((23, 55), (36, 35), (53, 46))],
            [((51, 23), (77, 12), (94, 23)), ((110, 8), (123, 8), (128, 14))],
        ],
    },
    {
        "id": "broad-cloud", "name": "Broad cloud",
        "note": "Wider shoulders and fuller side locks give the same face a broader cloud frame.",
        "points": [(42, 197), (10, 127), (46, 57), (128, 23)],
        "arcs": [
            [((22, 202), (10, 180), (18, 166)), ((10, 157), (9, 138), (10, 127))],
            [((9, 107), (14, 91), (27, 87)), ((14, 66), (27, 47), (46, 57))],
            [((44, 30), (75, 21), (91, 32)), ((106, 15), (123, 16), (128, 23))],
        ],
    },
    {
        "id": "compact-cloud", "name": "Compact cloud",
        "note": "A closer cloud frame reduces the mane footprint without changing the face size.",
        "points": [(58, 192), (24, 122), (62, 61), (128, 28)],
        "arcs": [
            [((38, 199), (23, 179), (29, 164)), ((16, 155), (17, 134), (24, 122))],
            [((16, 108), (24, 92), (38, 90)), ((26, 71), (45, 53), (62, 61))],
            [((58, 41), (81, 28), (94, 37)), ((108, 23), (123, 22), (128, 28))],
        ],
    },
    {
        "id": "extra-scallops", "name": "Extra scallops",
        "note": "More small, rounded scallops make the outline fluffier. The color joins still touch.",
        "points": [(50, 196), (17, 124), (57, 53), (128, 20)],
        "arcs": [
            [((33, 201), (21, 187), (26, 175)), ((11, 179), (9, 157), (18, 151)), ((7, 145), (10, 130), (17, 124))],
            [((7, 111), (12, 98), (25, 96)), ((17, 86), (22, 68), (36, 70)), ((32, 54), (47, 44), (57, 53))],
            [((48, 38), (59, 23), (76, 30)), ((73, 17), (90, 11), (102, 23)), ((111, 11), (124, 12), (128, 20))],
        ],
    },
]


def enamel_colors():
    source = ET.parse(PALETTE_SOURCE).getroot()
    mane = source.find(".//*[@id='mane']")
    if mane is None or len(mane) != 6:
        raise ValueError("Enamel source must contain six mane segments.")
    colors = []
    for group in mane:
        fills = [path.get("fill") for path in group.findall(f"{{{softling.NS}}}path") if path.get("fill") != "none"]
        if len(fills) != 1 or not re.fullmatch(r"#[0-9A-Fa-f]{6}", fills[0] or ""):
            raise ValueError("Each Enamel segment must contain one solid six-digit color.")
        colors.append(fills[0])
    return colors


def variants():
    return [
        {
            "id": f"c{index:02d}", "label": f"C{index:02d}", "name": contour["name"],
            "note": contour["note"], "file": f"c{index:02d}-{contour['id']}.svg",
        }
        for index, contour in enumerate(CONTOURS, 1)
    ]


def render(contour, face, colors):
    svg = ET.fromstring(softling.render(contour, JOIN, face, colors))
    svg.find(f"{{{softling.NS}}}title").text = f"Softling - {contour['name']} - Enamel mane colors"
    svg.find(f"{{{softling.NS}}}desc").text = (
        f"The unchanged front-facing Softling face inside a six-color cloud mane. {contour['note']} "
        "The mane uses the six solid Enamel colors, without its outlines or highlights. "
        "All segments touch, without gaps or divider lines. The chin stays clear."
    )
    return ET.tostring(svg, encoding="unicode") + "\n"


def outputs():
    face, _ = softling.source_art()
    colors = enamel_colors()
    entries = variants()
    files = {
        entry["file"]: render(contour, face, colors)
        for contour, entry in zip(CONTOURS, entries, strict=True)
    }
    files["studies.js"] = "window.cloudCrownStudies = " + json.dumps({"variants": entries}, indent=2) + ";\n"
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
        for name in stale:
            print(f"Missing or stale Cloud Crown asset: {name}")
        for name in unexpected:
            print(f"Unexpected Cloud Crown asset: {name}")
        if stale or unexpected:
            return 1
        print("Cloud Crown assets are current: six continuous manes in Enamel colors.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote six Cloud Crown studies and gallery metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
