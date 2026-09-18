"""Build 30 deliberately different Cloud Crown concepts, not a parameter grid."""

import argparse
import json
import xml.etree.ElementTree as ET

import generate_cloud_crown as cloud
import generate_softling as softling


OUTPUT = softling.ROOT / "docs/specs/branding/cloud-crown-lab"


def study(slug, name, family, note, lower, middle, upper, *, rounding=0.85, face=(1, 1, 0, 0), right=None):
    return dict(id=slug, name=name, family=family, note=note, left=[lower, middle, upper],
                rounding=rounding, face=face, right=right)


STUDIES = [
    study("sky-puff", "Sky puff", "Skyward", "A tall stack of clouds over a smaller face.",
          [(57,185),(29,180),(22,153),(30,124)], [(22,98),(38,84),(43,60)],
          [(57,49),(55,28),(78,20),(96,30),(110,12),(128,18)], face=(.83,.88,10,0)),
    study("triple-summit", "Triple summit", "Skyward", "Three rounded peaks with deep valleys between them.",
          [(55,188),(25,184),(19,154),(23,122)], [(19,94),(34,77),(47,65)],
          [(50,21),(67,15),(85,58),(108,23),(128,12)]),
    study("crown-prongs", "Crown prongs", "Skyward", "Long angular prongs turn the cloud into an improbable crown.",
          [(58,184),(24,175),(25,122)], [(17,76),(54,96),(43,28),(77,60)],
          [(91,14),(111,55),(128,16)], rounding=0, face=(.9,.94,5,0)),
    study("mushroom-canopy", "Mushroom canopy", "Skyward", "A huge soft cap balances narrow lower locks.",
          [(65,183),(46,173),(40,128)], [(17,113),(16,71),(44,46)],
          [(57,19),(87,13),(108,20),(128,14)], face=(.83,.9,11,0)),
    study("rocket-cloud", "Rocket cloud", "Skyward", "A pointed crown and swept side fins stretch the silhouette upward.",
          [(57,184),(28,193),(39,150),(27,119)], [(42,96),(47,62),(78,48)],
          [(91,31),(111,27),(128,12)], rounding=.25, face=(.85,1.02,0,0)),
    study("wide-horizon", "Wide horizon", "Wide & low", "A low, broad bank of cloud around the same face.",
          [(43,192),(18,176),(15,146),(18,119)], [(14,96),(28,73),(51,71)],
          [(66,52),(92,49),(111,57),(128,50)]),
    study("butterfly", "Butterfly cloud", "Wide & low", "Large wing-like lobes extend far beyond the ears.",
          [(50,193),(24,184),(30,159),(17,131)], [(15,91),(19,43),(56,57)],
          [(80,71),(88,41),(107,31),(128,43)], face=(.91,.92,7,0)),
    study("rainbow-cap", "Rainbow cap", "Wide & low", "A broad, almost rectangular cap with a rounded underside.",
          [(49,189),(21,179),(17,142),(17,116)], [(17,73),(26,42),(52,38)],
          [(84,38),(109,38),(128,38)], rounding=.45),
    study("side-cushions", "Side cushions", "Wide & low", "Oversized cheek cushions and only a shallow crown.",
          [(45,199),(18,192),(16,158),(23,135)], [(13,112),(25,88),(49,85)],
          [(76,68),(98,69),(115,78),(128,72)], face=(.94,.91,8,0)),
    study("bat-cloud", "Bat cloud", "Wide & low", "Pointed wing tips and scalloped shoulders challenge the soft-cloud idea.",
          [(52,190),(19,175),(37,151),(16,117)], [(16,60),(48,82),(72,50)],
          [(96,67),(111,44),(128,32)], rounding=0),
    study("tulip", "Tulip crown", "Botanical", "Big petal tips replace the small cloud scallops.",
          [(56,188),(24,174),(20,136),(30,118)], [(22,63),(66,86),(65,36)],
          [(91,61),(111,32),(128,15)], rounding=.55, face=(.9,.94,5,0)),
    study("leaf-fan", "Leaf fan", "Botanical", "A serrated leaf-like edge with six broad touching color regions.",
          [(55,189),(25,177),(34,158),(17,140),(30,119)],
          [(17,97),(40,92),(29,64),(60,70),(54,41)],
          [(83,51),(86,21),(109,37),(128,13)], rounding=.05),
    study("clover", "Clover cloud", "Botanical", "A few giant round lobes give the crown a clover silhouette.",
          [(54,193),(23,190),(15,166),(23,143),(31,123)],
          [(15,93),(18,62),(44,43),(69,52)],
          [(69,26),(90,13),(117,18),(128,30)], rounding=1),
    study("sunflower", "Sunflower cloud", "Botanical", "Many narrow rounded points make a flower-like outline.",
          [(52,193),(30,195),(33,175),(16,168),(28,146),(16,126)],
          [(31,111),(18,89),(40,83),(31,60),(58,59)],
          [(57,33),(81,41),(91,17),(111,31),(128,14)], rounding=.5, face=(.92,.95,5,0)),
    study("coral", "Coral crown", "Botanical", "Lumpy branching shapes make the crown feel more creature-like.",
          [(57,188),(30,190),(20,171),(35,155),(20,132)],
          [(33,114),(23,84),(45,90),(40,52),(64,68)],
          [(62,27),(84,19),(98,48),(114,20),(128,26)], rounding=.65),
    study("cut-stone", "Cut stone", "Sculptural", "A faceted crown with hard shoulders and no rounded cloud lobes.",
          [(51,195),(17,168),(22,122)], [(17,88),(47,42),(64,55)],
          [(84,18),(109,28),(128,14)], rounding=0),
    study("lightning", "Lightning cut", "Sculptural", "Deep zigzag bites and a high crest replace the billows.",
          [(53,191),(22,180),(40,154),(18,132)], [(47,109),(23,85),(65,74),(48,43)],
          [(89,49),(99,18),(112,35),(128,14)], rounding=0),
    study("cloud-pixels", "Cloud pixels", "Sculptural", "A stair-stepped pixel cloud around the smooth Softling face.",
          [(55,191),(31,191),(31,177),(17,177),(17,129)],
          [(17,97),(31,97),(31,65),(49,65),(49,49),(65,49)],
          [(65,33),(97,33),(97,17),(113,17),(113,33),(128,33)], rounding=0),
    study("portal", "Cloud portal", "Sculptural", "A tall block-like arch makes the smaller face feel tucked inside.",
          [(48,197),(20,187),(20,125)], [(20,47),(44,24),(66,24)],
          [(91,24),(110,24),(128,24)], rounding=.15, face=(.78,.83,14,0)),
    study("folded-kite", "Folded kite", "Sculptural", "A paper-like angular frame with a gently tilted face.",
          [(61,190),(17,166),(39,127)], [(20,77),(66,85),(74,31)],
          [(101,49),(113,18),(128,30)], rounding=0, face=(.9,.97,5,-7)),
    study("windblown", "Windblown", "Offbeat", "A compact left side and an oversized right side break the symmetry.",
          [(63,185),(39,177),(32,140),(37,118)], [(31,91),(48,74),(65,65)],
          [(73,40),(96,34),(113,40),(128,28)],
          right=[[(44,196),(17,189),(16,151),(23,127)],[(14,94),(18,48),(45,44)],[(56,22),(88,14),(110,34),(128,28)]]),
    study("half-garden", "Half garden", "Offbeat", "Leaves on one side, cloud billows on the other.",
          [(53,191),(22,178),(36,157),(17,135)], [(33,116),(22,84),(51,87),(44,48),(71,61)],
          [(77,25),(102,42),(128,19)], rounding=.25,
          right=[[(51,196),(29,200),(16,182),(22,165),(15,124)],[(13,101),(30,86),(28,62),(56,52)],[(61,29),(89,26),(106,15),(128,19)]]),
    study("wonky-crown", "Wonky crown", "Offbeat", "Unequal peaks lean around a slightly tilted face.",
          [(56,190),(25,187),(18,153),(25,122)], [(17,99),(32,73),(52,68)],
          [(55,44),(78,40),(91,57),(111,33),(128,41)], face=(.95,.98,4,6),
          right=[[(53,190),(23,181),(20,151),(28,124)],[(18,85),(41,67),(45,38)],[(65,16),(88,20),(101,47),(128,41)]]),
    study("comet", "Cloud comet", "Offbeat", "One stretched angular wing meets a rounded cloud on the other side.",
          [(54,190),(17,177),(43,145),(16,103)], [(18,56),(49,83),(75,60)],
          [(89,31),(112,25),(128,32)], rounding=.1,
          right=[[(62,187),(39,187),(25,166),(31,126)],[(25,101),(41,80),(60,68)],[(62,45),(93,34),(110,41),(128,32)]]),
    study("jelly-crown", "Jelly crown", "Offbeat", "A low wobbly cloud sprouts one tall, soft horn.",
          [(50,193),(25,193),(16,172),(28,152),(19,127)], [(16,96),(40,80),(59,82)],
          [(68,58),(91,59),(112,74),(128,59)],
          right=[[(50,193),(22,187),(26,167),(16,140),(24,123)],[(17,97),(40,79),(60,83)],[(65,41),(81,16),(97,24),(99,62),(128,59)]]),
    study("big-cub", "Big cub", "Face & frame", "An oversized face fills a shallow, broad cloud collar.",
          [(44,185),(18,174),(16,142),(23,119)], [(15,91),(31,70),(49,65)],
          [(63,45),(93,40),(112,48),(128,38)], face=(1.18,1.06,0,0)),
    study("tiny-cub", "Tiny cub", "Face & frame", "A tiny face sits low inside an enormous cloud crown.",
          [(52,195),(25,197),(16,176),(24,151),(17,124)], [(16,97),(34,78),(31,53),(61,44)],
          [(71,20),(100,17),(114,30),(128,19)], face=(.62,.65,23,0)),
    study("long-face", "Long face", "Face & frame", "A narrow, tall face under a forked crown changes the mascot proportions.",
          [(58,189),(29,181),(25,148),(29,119)], [(21,88),(46,67),(56,47)],
          [(78,25),(100,43),(115,21),(128,35)], face=(.76,1.15,-3,0)),
    study("squish", "Squish", "Face & frame", "A wide, short face beneath a broad, playful cloud.",
          [(45,194),(20,181),(17,150),(24,127)], [(17,104),(28,83),(46,79)],
          [(55,54),(87,49),(112,62),(128,47)], face=(1.18,.7,24,0)),
    study("tilted-toy", "Tilted toy", "Face & frame", "A small tilted face contrasts with a large scalloped mane.",
          [(50,191),(25,193),(16,174),(27,154),(18,130)], [(25,105),(17,83),(40,68),(53,75)],
          [(54,49),(80,34),(99,45),(111,22),(128,30)], face=(.77,.8,15,11)),
]


def geometry(item):
    left = item["left"]
    right = item["right"] or left
    left_points = sum(left, [])
    right_points = sum(right, [])
    if left_points[-1] != right_points[-1] or left_points[-1][0] != 128:
        raise ValueError(f"{item['id']}: both halves must meet at the crown center.")
    points = left_points + [softling.mirror(point) for point in reversed(right_points[:-1])]
    ends = [0, len(left[0]) - 1, len(left[0]) + len(left[1]) - 1, len(left_points) - 1]
    ends += [ends[-1] + len(right[2]), ends[-1] + len(right[2]) + len(right[1]), len(points) - 1]
    curves = []
    tension = item["rounding"] / 6
    for index, (a, b) in enumerate(zip(points, points[1:])):
        prev = points[max(0, index - 1)]
        after = points[min(len(points) - 1, index + 2)]
        c1 = tuple(a[c] + (b[c] - prev[c]) * tension for c in (0, 1))
        c2 = tuple(b[c] - (after[c] - a[c]) * tension for c in (0, 1))
        curves.append((a, c1, c2, b))
    boundaries = []
    for index, end in enumerate(ends):
        point = points[end]
        if index in (0, 6):
            direction = 1 if index == 0 else -1
            boundaries.append((softling.CENTER, (128 - 10 * direction, 163),
                               (point[0] + 26 * direction, point[1] + 11), point))
        else:
            first = tuple(softling.CENTER[c] + (point[c] - softling.CENTER[c]) / 3 for c in (0, 1))
            second = tuple(softling.CENTER[c] + (point[c] - softling.CENTER[c]) * 2 / 3 for c in (0, 1))
            boundaries.append((softling.CENTER, first, second, point))
    pieces = [
        softling.curve_path(boundaries[i]) +
        "".join(softling.curve_text(curve) for curve in curves[ends[i]:ends[i + 1]]) +
        softling.curve_text(softling.reverse(boundaries[i + 1])) + "Z"
        for i in range(6)
    ]
    outline = (softling.curve_path(boundaries[0]) +
               "".join(softling.curve_text(curve) for curve in curves) +
               softling.curve_text(softling.reverse(boundaries[6])) + "Z")
    return pieces, outline, boundaries


def variants():
    return [
        dict(id=f"b{i:02d}", label=f"B{i:02d}", name=item["name"], family=item["family"],
             note=item["note"], file=f"b{i:02d}-{item['id']}.svg")
        for i, item in enumerate(STUDIES, 1)
    ]


def render(item, face, colors):
    svg = ET.fromstring(softling.render(item, cloud.JOIN, face, colors, shape=geometry(item)))
    svg.find(f"{{{softling.NS}}}title").text = f"Cloud Crown lab - {item['name']}"
    svg.find(f"{{{softling.NS}}}desc").text = (
        f"{item['note']} Six touching Enamel-colored mane segments surround a front-facing Softling lion. "
        "The original facial features remain, with deliberate scale or tilt changes in some studies."
    )
    sx, sy, dy, angle = item["face"]
    original = svg.find(".//*[@id='face']")
    svg.remove(original)
    frame = ET.SubElement(svg, f"{{{softling.NS}}}g", {
        "id": "face-frame",
        "transform": f"translate(128 {160 + dy}) rotate({angle}) scale({sx} {sy}) translate(-128 -160)",
    })
    frame.append(original)
    ET.indent(svg, space="  ")
    return ET.tostring(svg, encoding="unicode") + "\n"


def outputs():
    face, _ = softling.source_art()
    colors = cloud.enamel_colors()
    entries = variants()
    files = {entry["file"]: render(item, face, colors) for item, entry in zip(STUDIES, entries, strict=True)}
    files["studies.js"] = "window.cloudCrownLab = " + json.dumps({"variants": entries}, indent=2) + ";\n"
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Report stale assets without changing files.")
    args = parser.parse_args()
    files = outputs()
    if args.check:
        stale = [name for name, content in files.items() if not (OUTPUT / name).exists()
                 or (OUTPUT / name).read_text(encoding="utf-8") != content]
        unexpected = sorted(p.name for p in OUTPUT.glob("*.svg") if p.name not in files)
        for name in stale:
            print(f"Missing or stale Cloud Crown lab asset: {name}")
        for name in unexpected:
            print(f"Unexpected Cloud Crown lab asset: {name}")
        if stale or unexpected:
            return 1
        print("Cloud Crown lab assets are current: 30 distinct concepts.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote 30 Cloud Crown lab SVGs and gallery metadata.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
