"""Make happy-eye and outlined finishes of the selected C03 High Crown."""

import argparse
from copy import deepcopy
import xml.etree.ElementTree as ET

import generate_cloud_crown as cloud
import generate_softling as softling


SOURCE = cloud.OUTPUT / "c03-high-crown.svg"
OUTPUT = softling.ROOT / "docs/specs/branding/high-crown-finish"
SLEEPING_EYES = "M91 148Q100 155 109 148M147 148Q156 155 165 148"
HAPPY_EYES = "M91 148Q100 134 109 148M147 148Q156 134 165 148"
NS = f"{{{softling.NS}}}"


def darker(color):
    return "#" + "".join(f"{round(int(color[i:i + 2], 16) * 0.68):02X}" for i in (1, 3, 5))


def render(outlined):
    svg = ET.parse(SOURCE).getroot()
    face = svg.find(".//*[@id='face']")
    eyes = [node for node in face if node.get("d") == SLEEPING_EYES]
    if len(eyes) != 1:
        raise ValueError("C03 must contain exactly one expected sleeping-eye path.")
    eyes[0].set("d", HAPPY_EYES)
    title = "High Crown - Happy eyes" + (" and colored outlines" if outlined else "")
    svg.find(NS + "title").text = title
    svg.find(NS + "desc").text = (
        "The selected C03 High Crown lion with cheerful closed eyes that arch upward. "
        "Its shape, six touching Enamel mane colors, and other facial features stay unchanged. " +
        ("A brown outline surrounds the face and ears. Each mane region has a darker matching border "
         "only along the outer perimeter, not between colors." if outlined else "No new outlines.")
    )
    if outlined:
        mane = svg.find(".//*[@id='mane']")
        silhouette = mane.find(".//*[@id='mane-backing']").get("d")
        band = ET.SubElement(svg.find(NS + "defs"), NS + "mask", {
            "id": "mane-border-band", "maskUnits": "userSpaceOnUse",
            "x": "0", "y": "0", "width": "256", "height": "256",
        })
        # One closed stroke joins every corner; its colors come from the existing mane regions.
        ET.SubElement(band, NS + "path", {
            "d": silhouette, "fill": "none", "stroke": "white",
            "stroke-width": "8", "stroke-linejoin": "round",
        })
        perimeter = ET.SubElement(mane, NS + "g", {
            "id": "outer-mane-border", "mask": "url(#mane-border-band)",
        })
        colors = [darker(color) for color in cloud.enamel_colors()]
        ET.SubElement(perimeter, NS + "path", {
            "id": "border-backing", "d": silhouette, "fill": colors[0],
        })
        regions = ET.SubElement(perimeter, NS + "g", {"id": "border-colors"})
        for index, (segment, color) in enumerate(zip(mane.find(".//*[@id='segments']"), colors, strict=True), 1):
            region = deepcopy(segment)
            region.set("id", f"border-segment-{index}")
            region.set("fill", color)
            region.set("stroke", color)
            regions.append(region)
        # Draw the outlines behind the original face, so overlapping ears do not gain internal rings.
        outline = ET.Element(NS + "g", {
            "id": "face-border", "fill": "none", "stroke": "#73503C",
            "stroke-width": "4", "stroke-linejoin": "round",
        })
        for node in face:
            if (node.tag == NS + "circle" and node.get("r") == "23") or node.get("fill") == "#F5C875":
                copy = deepcopy(node)
                copy.set("fill", "none")
                outline.append(copy)
        svg.insert(list(svg).index(face), outline)
    ET.indent(svg, space="  ")
    return ET.tostring(svg, encoding="unicode") + "\n"


def outputs():
    return {"h01-happy.svg": render(False), "h02-happy-outlined.svg": render(True)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    files = outputs()
    if args.check:
        stale = [name for name, content in files.items()
                 if not (OUTPUT / name).exists() or (OUTPUT / name).read_text(encoding="utf-8") != content]
        unexpected = [p.name for p in OUTPUT.glob("*.svg") if p.name not in files]
        if stale or unexpected:
            for name in stale + unexpected:
                print(f"Missing, stale, or unexpected High Crown finish: {name}")
            return 1
        print("Both High Crown finishes are current.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote happy-eye and outlined High Crown SVGs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
