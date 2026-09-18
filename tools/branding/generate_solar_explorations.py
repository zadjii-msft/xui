"""Build the Solar shape-by-proportion study without changing earlier artwork."""

import argparse
import json
import xml.etree.ElementTree as ET

import generate_solar as solar


OUTPUT = solar.ROOT / "docs/specs/branding/solar-explorations"
MANES = [
    {
        "id": "crescent", "letter": "A", "name": "Crescent",
        "note": "A broad, smooth horseshoe. Almost a continuous arc, divided into six soft sectors.",
        "paths": [
            "M16 137C15 155 32 174 54 179C73 180 83 175 100 168L94 158Q90 151 90 142Z",
            "M16 128C12 104 27 77 51 63C65 61 72 74 83 87L102 111Q94 121 91 134Z",
            "M60 55C65 30 94 12 122 16L124 52Q117 74 124 103Q115 103 109 107L88 81Z",
        ],
    },
    {
        "id": "tower", "letter": "B", "name": "Tower",
        "note": "A narrow, upright mane with tall crown locks and tucked-in shoulders.",
        "paths": [
            "M39 137C29 156 43 175 68 177Q84 177 101 168L96 151L90 139Z",
            "M37 128C24 101 38 66 60 59Q72 55 78 75L102 111Q94 119 91 131Z",
            "M72 53C66 26 85 9 97 14Q112 6 123 15L124 104L110 107L92 81Z",
        ],
    },
    {
        "id": "wings", "letter": "C", "name": "Wings",
        "note": "Wide and low, with horizontal cheek fans. Much less height above the ears.",
        "paths": [
            "M13 134C14 156 37 166 62 161Q78 162 99 170L98 155L90 139Z",
            "M13 123C9 105 12 78 28 77C52 79 67 86 81 91L102 112L90 132Z",
            "M39 69C60 43 87 42 123 48L124 105L109 107L85 83Q66 80 39 69Z",
        ],
    },
    {
        "id": "fan", "letter": "D", "name": "Fan",
        "note": "Six long, tapered fan blades. A graphic silhouette with straight stretches and rounded tips.",
        "paths": [
            "M16 120Q11 125 18 135L47 164Q66 181 98 170L94 152L88 141Z",
            "M22 94Q13 79 27 65L48 45Q56 37 66 53L104 111L91 130Z",
            "M70 37Q69 21 84 19L112 14Q124 11 124 27L124 104L110 105Z",
        ],
    },
    {
        "id": "cloudbank", "letter": "E", "name": "Cloud bank",
        "note": "Oversized, pillowy lobes. The small face becomes a tiny character inside a cloud of mane.",
        "paths": [
            "M22 135C6 147 15 166 32 166C42 185 67 182 78 172Q90 176 103 167L96 147Z",
            "M22 125C7 109 14 89 29 86C20 64 38 49 56 57C68 53 77 68 78 79L102 111L91 134Z",
            "M66 48C60 31 76 17 92 23C105 9 124 17 124 33L124 104L109 107Q95 82 85 73Z",
        ],
    },
    {
        "id": "willow", "letter": "F", "name": "Willow",
        "note": "Long leaf-like locks with narrow roots and curling tips. An airy, organic outline.",
        "paths": [
            "M16 112C44 114 67 123 88 142L103 167C67 181 35 156 37 139Q23 133 16 112Z",
            "M32 45C65 47 92 75 102 112L91 132C67 119 44 94 48 73Q34 65 32 45Z",
            "M80 14C112 30 123 54 124 103L111 104C93 82 76 59 86 43Q76 27 80 14Z",
        ],
    },
    {
        "id": "scallop", "letter": "G", "name": "Scalloped arch",
        "note": "A continuous-looking arch with repeated shallow waves, rather than six separate petals.",
        "paths": [
            "M20 134Q8 145 19 154Q16 169 33 169Q40 185 57 175Q74 181 99 169L96 154L90 140Z",
            "M19 124Q7 111 21 101Q13 83 31 78Q25 60 45 57Q50 45 62 53L103 111L90 132Z",
            "M69 47Q60 33 77 28Q80 11 95 20Q111 8 123 18L124 104L109 106L88 79Z",
        ],
    },
    {
        "id": "blocks", "letter": "H", "name": "Soft blocks",
        "note": "Chunky stepped locks with flat shoulders and rounded corners. A much more architectural head.",
        "paths": [
            "M17 133H86L100 166Q101 177 88 177H42Q32 177 32 164V155H24Q17 155 17 147Z",
            "M18 120V85Q18 76 28 76H40V60Q40 51 50 51H61L103 111L88 127Z",
            "M71 42V25Q71 16 81 16H114Q124 16 124 26V104H110L89 80V57H80Q71 57 71 42Z",
        ],
    },
    {
        "id": "flame", "letter": "I", "name": "Flame sweep",
        "note": "Six sweeping flame tongues. Pointed ends and deep concave curves replace the rounded flower outline.",
        "paths": [
            "M15 108C46 135 51 116 73 132Q90 143 104 167C79 184 50 167 48 151C32 150 21 129 15 108Z",
            "M31 39C49 67 70 51 80 73Q92 91 102 111L89 130C68 119 57 103 57 86C39 78 36 56 31 39Z",
            "M87 12C92 37 116 24 118 55Q127 75 124 103L110 105C100 86 86 74 91 52C78 38 78 23 87 12Z",
        ],
    },
    {
        "id": "ribbons", "letter": "J", "name": "Open ribbons",
        "note": "Broad folded ribbons with open centers and curled side tips. The lightest, most open construction.",
        "paths": [
            "M20 137C13 159 32 176 59 174Q85 176 100 166L92 154Q69 165 50 158Q34 152 37 140L51 143Q49 153 61 150L71 143Q50 126 20 137Z",
            "M21 123C11 92 36 64 61 62L98 110L88 124L57 83Q37 87 37 108L50 111L54 101L66 112L62 130Z",
            "M70 53C59 29 86 9 121 20L124 103L110 104L108 38Q87 33 87 49L99 61L102 81Z",
        ],
    },
    {
        "id": "droplets", "letter": "K", "name": "Droplets",
        "note": "Six detached teardrops, with generous space between them. A spare, almost symbolic lion.",
        "paths": [
            "M21 133C15 150 21 169 43 171C62 176 83 169 101 162C71 139 49 120 21 133Z",
            "M29 65C10 81 18 107 38 114Q69 124 97 115C76 86 62 55 29 65Z",
            "M87 17C66 17 67 43 79 62Q96 88 119 104C123 64 125 18 87 17Z",
        ],
    },
    {
        "id": "crown", "letter": "L", "name": "Crown crest",
        "note": "A broad, raised crown with little mane at the jaw. Almost all the visual weight sits above the face.",
        "paths": [
            "M20 116Q10 140 34 151L58 151Q78 158 97 166L94 146L86 128Z",
            "M24 100C16 74 30 42 49 39Q56 45 54 61Q75 70 102 110L88 123Z",
            "M63 38C74 14 97 22 103 13Q116 26 123 16L124 104L110 104L91 68Q90 47 63 38Z",
        ],
    },
]

FACES = [
    {"id": "small", "name": "Small", "note": "Small face, dominant mane.", "scaleX": 0.62, "scaleY": 0.62},
    {"id": "balanced", "name": "Balanced", "note": "Middle ground between face and mane.", "scaleX": 0.84, "scaleY": 0.84},
    {"id": "wide", "name": "Wide + short", "note": "Broad cheeks and a shorter muzzle.", "scaleX": 1.10, "scaleY": 0.76},
    {"id": "large", "name": "Large", "note": "An oversized face with the mane pushed to the edges.", "scaleX": 1.08, "scaleY": 1.08},
]


def variants():
    return [
        {
            "id": f"{mane['letter'].lower()}{index}",
            "label": f"{mane['letter']}{index}",
            "mane": mane["id"], "face": face["id"],
            "stem": f"{mane['letter'].lower()}{index}-{mane['id']}-{face['id']}",
            "name": f"{mane['name']} / {face['name']}",
        }
        for mane in MANES for index, face in enumerate(FACES, 1)
    ]


def render(mane, proportion, palette, source_face):
    root = ET.fromstring(solar.render(mane, palette, source_face))
    namespace = f"{{{solar.NS}}}"
    root.find(namespace + "title").text = f"Solar {mane['name']} / {proportion['name']} - {palette['label']}"
    root.find(namespace + "desc").text = (
        f"A front-facing lion with the Solar expression, a {proportion['name'].lower()} face, "
        f"and six {mane['name'].lower()} mane segments above and beside the face. "
        f"The chin remains clear. {palette['name']} palette."
    )
    root.find(namespace + "g").attrib.clear()
    root.find(".//*[@id='face']").set(
        "transform",
        f"translate(128 144) scale({proportion['scaleX']:g} {proportion['scaleY']:g}) translate(-128 -132)",
    )
    ET.indent(root, space="  ")
    return ET.tostring(root, encoding="unicode") + "\n"


def outputs():
    files = {}
    source_face = solar.original_face()
    entries = variants()
    for entry in entries:
        mane = next(item for item in MANES if item["id"] == entry["mane"])
        face = next(item for item in FACES if item["id"] == entry["face"])
        for palette in solar.PALETTES:
            files[f"{entry['stem']}-{palette['id']}.svg"] = render(mane, face, palette, source_face)
    metadata = {
        "manes": [{key: value for key, value in mane.items() if key != "paths"} for mane in MANES],
        "faces": FACES,
        "palettes": [{key: palette[key] for key in ("id", "name", "label")} for palette in solar.PALETTES],
        "variants": entries,
    }
    files["studies.js"] = "window.solarExplorations = " + json.dumps(metadata, indent=2) + ";\n"
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
                print(f"Missing or stale Solar exploration: {name}")
            for name in unexpected:
                print(f"Unexpected Solar exploration: {name}")
            return 1
        print("Solar explorations are current: 48 geometry variants, seven palettes, 336 SVGs.")
        return 0
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        (OUTPUT / name).write_text(content, encoding="utf-8", newline="\n")
    print("Wrote 336 SVGs and metadata for 48 Solar geometry variants.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
