"""Build fuller Softling manes with shared boundaries and an unchanged face."""

import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs/specs/branding/solar-d2-styles/06-softling.svg"
OUTPUT = ROOT / "docs/specs/branding/softling"
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
CENTER = (128, 148)
GAP_WIDTH = 3
LINE_WIDTH = 1.75
WAVE_AMOUNT = 24

CONTOURS = [
    {
        "id": "plush", "letter": "A", "name": "Plush fan",
        "note": "The closest to Softling: fuller rounded locks merge into a soft fan.",
        "points": [(53, 190), (16, 123), (56, 55), (128, 20)],
        "arcs": [
            [((22, 190), (9, 156), (16, 123))],
            [((13, 105), (26, 84), (35, 79)), ((29, 57), (40, 44), (56, 55))],
            [((66, 32), (73, 18), (94, 25)), ((110, 13), (119, 14), (128, 20))],
        ],
    },
    {
        "id": "halo", "letter": "B", "name": "Round halo",
        "note": "A broad, nearly circular mane with full cheeks and a smooth continuous crown.",
        "points": [(54, 198), (11, 125), (49, 43), (128, 10)],
        "arcs": [
            [((24, 188), (8, 155), (11, 125))],
            [((12, 93), (25, 62), (49, 43))],
            [((70, 19), (99, 8), (128, 10))],
        ],
    },
    {
        "id": "cloud", "letter": "C", "name": "Cloud crown",
        "note": "Large billowing lobes add volume above the ears and around the sides.",
        "points": [(51, 197), (17, 124), (56, 53), (128, 15)],
        "arcs": [
            [((29, 204), (13, 184), (20, 166)), ((7, 159), (7, 133), (17, 124))],
            [((7, 110), (14, 91), (30, 87)), ((15, 62), (37, 43), (56, 53))],
            [((48, 28), (75, 15), (90, 24)), ((102, 7), (123, 8), (128, 15))],
        ],
    },
    {
        "id": "wave", "letter": "D", "name": "Rolling wave",
        "note": "Deeper ripples and curling edges make the mane feel loose, flowing, and fluffy.",
        "points": [(48, 194), (15, 123), (53, 53), (128, 18)],
        "arcs": [
            [((26, 199), (13, 182), (22, 168)), ((28, 157), (8, 153), (11, 141)), ((12, 132), (20, 133), (15, 123))],
            [((7, 109), (27, 102), (22, 91)), ((12, 77), (22, 60), (38, 65)), ((47, 68), (42, 55), (53, 53))],
            [((60, 48), (61, 32), (78, 35)), ((85, 37), (79, 18), (98, 18)), ((111, 20), (116, 7), (128, 18))],
        ],
    },
]
JOINS = [
    {"id": "narrow", "column": 1, "name": "Narrow gaps", "note": "True transparent 3px channels between segments."},
    {"id": "joined", "column": 2, "name": "Touching colors", "note": "Continuous mane with no gaps or divider lines."},
    {"id": "lined", "column": 3, "name": "Fine dividers", "note": "Continuous mane separated by thin brown lines."},
    {"id": "wavy", "column": 4, "name": "Wavy joins", "note": "Continuous mane with flowing S-shaped color boundaries."},
]


def mirror(point):
    return (256 - point[0], point[1])


def reverse(curve):
    start, first, second, end = curve
    return (end, second, first, start)


def point_text(point):
    return " ".join(f"{value:.3f}".rstrip("0").rstrip(".") if value else "0" for value in point)


def curve_text(curve):
    return "C" + " ".join(point_text(point) for point in curve[1:])


def curve_path(curve):
    return "M" + point_text(curve[0]) + curve_text(curve)


def geometry(contour, join):
    points = contour["points"] + [mirror(point) for point in reversed(contour["points"][:-1])]
    arcs = []
    for start, commands in zip(contour["points"][:-1], contour["arcs"], strict=True):
        curves = []
        for first, second, end in commands:
            curves.append((start, first, second, end))
            start = end
        arcs.append(curves)
    arcs += [
        [tuple(mirror(point) for point in reverse(curve)) for curve in reversed(curves)]
        for curves in reversed(arcs)
    ]
    boundaries = []
    for index, point in enumerate(points):
        if index == 0:
            boundaries.append((CENTER, (118, 163), (point[0] + 26, point[1] + 11), point))
        elif index == 6:
            boundaries.append(tuple(mirror(value) for value in boundaries[0]))
        else:
            dx, dy = point[0] - CENTER[0], point[1] - CENTER[1]
            amount = WAVE_AMOUNT if join["id"] == "wavy" else 0
            if index > 3:
                amount *= -1
            length = math.hypot(dx, dy)
            normal = (-dy / length * amount, dx / length * amount)
            first = (CENTER[0] + dx / 3 + normal[0], CENTER[1] + dy / 3 + normal[1])
            second = (CENTER[0] + dx * 2 / 3 - normal[0], CENTER[1] + dy * 2 / 3 - normal[1])
            boundaries.append((CENTER, first, second, point))
    segments = [
        curve_path(boundaries[index]) +
        "".join(curve_text(curve) for curve in arcs[index]) +
        curve_text(reverse(boundaries[index + 1])) + "Z"
        for index in range(6)
    ]
    outline = (
        curve_path(boundaries[0]) +
        "".join(curve_text(curve) for curves in arcs for curve in curves) +
        curve_text(reverse(boundaries[6])) + "Z"
    )
    return segments, outline, boundaries


def source_art():
    source = ET.parse(SOURCE).getroot()
    face = source.find(".//*[@id='face']")
    mane = source.find(".//*[@id='mane']")
    if face is None or mane is None or len(mane) != 6:
        raise ValueError("Softling source must contain a face and six mane groups.")
    colors = [group.find(f"{{{NS}}}path").attrib["fill"] for group in mane]
    return face, colors


def variants():
    return [
        {
            "id": f"m{index * 4 + offset:02d}",
            "label": f"M{index * 4 + offset:02d}",
            "contour": contour["id"], "join": join["id"],
            "file": f"m{index * 4 + offset:02d}-{contour['id']}-{join['id']}.svg",
            "name": f"{contour['name']} / {join['name']}",
        }
        for index, contour in enumerate(CONTOURS) for offset, join in enumerate(JOINS, 1)
    ]


def render(contour, join, face, colors, *, shape=None):
    segments, outline, boundaries = geometry(contour, join) if shape is None else shape
    svg = ET.Element(f"{{{NS}}}svg", {"viewBox": "0 0 256 256", "role": "img", "aria-labelledby": "title desc"})
    ET.SubElement(svg, f"{{{NS}}}title", {"id": "title"}).text = f"Softling - {contour['name']} / {join['name']}"
    ET.SubElement(svg, f"{{{NS}}}desc", {"id": "desc"}).text = (
        f"The original front-facing Softling face and pastel rainbow palette, with a fuller {contour['name'].lower()} mane. "
        f"{join['note']} Six colored segments surround the upper face. The chin stays clear."
    )
    defs = ET.SubElement(svg, f"{{{NS}}}defs")
    clip = ET.SubElement(defs, f"{{{NS}}}clipPath", {"id": "mane-outline"})
    ET.SubElement(clip, f"{{{NS}}}path", {"d": outline})
    if join["id"] == "narrow":
        mask = ET.SubElement(defs, f"{{{NS}}}mask", {
            "id": "segment-gaps", "maskUnits": "userSpaceOnUse", "x": "0", "y": "0",
            "width": "256", "height": "256", "mask-type": "luminance",
        })
        ET.SubElement(mask, f"{{{NS}}}rect", {"width": "256", "height": "256", "fill": "white"})
        for boundary in boundaries[1:-1]:
            ET.SubElement(mask, f"{{{NS}}}path", {
                "d": curve_path(boundary), "fill": "none", "stroke": "black",
                "stroke-width": str(GAP_WIDTH), "stroke-linecap": "round",
            })
    attributes = {"id": "mane", "clip-path": "url(#mane-outline)"}
    if join["id"] == "narrow":
        attributes["mask"] = "url(#segment-gaps)"
    mane = ET.SubElement(svg, f"{{{NS}}}g", attributes)
    # The backing and clipped overpaint prevent transparent antialiasing cracks at shared edges.
    ET.SubElement(mane, f"{{{NS}}}path", {"id": "mane-backing", "d": outline, "fill": colors[0]})
    pieces = ET.SubElement(mane, f"{{{NS}}}g", {"id": "segments"})
    for index, (segment, color) in enumerate(zip(segments, colors, strict=True), 1):
        ET.SubElement(pieces, f"{{{NS}}}path", {
            "id": f"segment-{index}", "d": segment, "fill": color,
            "stroke": color, "stroke-width": "0.8", "stroke-linejoin": "round",
        })
    if join["id"] == "lined":
        dividers = ET.SubElement(mane, f"{{{NS}}}g", {
            "id": "dividers", "fill": "none", "stroke": "#73503C",
            "stroke-width": str(LINE_WIDTH), "stroke-opacity": "0.68", "stroke-linecap": "round",
        })
        for boundary in boundaries[1:-1]:
            ET.SubElement(dividers, f"{{{NS}}}path", {"d": curve_path(boundary)})
    svg.append(deepcopy(face))
    ET.indent(svg, space="  ")
    return ET.tostring(svg, encoding="unicode") + "\n"


def outputs():
    face, colors = source_art()
    files = {}
    entries = variants()
    for entry in entries:
        contour = next(item for item in CONTOURS if item["id"] == entry["contour"])
        join = next(item for item in JOINS if item["id"] == entry["join"])
        files[entry["file"]] = render(contour, join, face, colors)
    metadata = {
        "contours": [{key: contour[key] for key in ("id", "letter", "name", "note")} for contour in CONTOURS],
        "joins": JOINS, "variants": entries,
    }
    files["studies.js"] = "window.softlingStudies = " + json.dumps(metadata, indent=2) + ";\n"
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
                print(f"Missing or stale Softling asset: {name}")
            for name in unexpected:
                print(f"Unexpected Softling asset: {name}")
            return 1
        print("Softling assets are current: four contours, four join treatments, 16 SVGs.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote 16 Softling mane studies and gallery metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
