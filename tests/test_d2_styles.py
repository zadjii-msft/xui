"""Check the individually authored D2 style studies."""

from pathlib import Path
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "docs/specs/branding/solar-d2-styles"
NS = "{http://www.w3.org/2000/svg}"
PAINT = {"fill", "stroke", "color", "opacity", "fill-opacity", "stroke-opacity", "stop-color", "id"}
ALLOWED = {
    "svg", "g", "path", "rect", "circle", "ellipse", "line", "polyline", "polygon", "title", "desc",
    "defs", "linearGradient", "radialGradient", "stop", "clipPath", "mask", "pattern", "use",
    "filter", "feGaussianBlur", "feOffset", "feMerge", "feMergeNode", "feComposite",
    "feColorMatrix", "feFlood", "feBlend", "feDropShadow",
}


class D2StyleTests(unittest.TestCase):
    def test_complete_set(self):
        files = list(ART.glob("*.svg"))
        self.assertEqual(len(files), 24)
        self.assertEqual(sorted(int(path.name.split("-")[0]) for path in files), list(range(1, 25)))

    def test_six_segments_and_redrawn_faces(self):
        fingerprints = set()
        original = ET.parse(ROOT / "docs/specs/branding/01-solar.svg").getroot()
        original_paths = {path.get("d") for path in original.findall(NS + "path")[1:]}
        for path in sorted(ART.glob("*.svg")):
            with self.subTest(file=path.name):
                svg = ET.parse(path).getroot()
                mane = svg.find(".//*[@id='mane']")
                face = svg.find(".//*[@id='face']")
                self.assertIsNotNone(mane)
                self.assertIsNotNone(face)
                self.assertEqual(len(mane), 6)
                self.assertEqual([node.tag for node in mane], [NS + "g"] * 6)
                self.assertEqual([node.get("id") for node in mane], [f"segment-{i}" for i in range(1, 7)])
                self.assertTrue(all(len(node) > 0 for node in mane))
                paths = {node.get("d") for node in face.iter() if node.get("d")}
                self.assertLessEqual(len(paths & original_paths), 2, "Face must be redrawn, not copied.")
                fingerprint = tuple(
                    (node.tag, tuple(sorted((key, value) for key, value in node.attrib.items() if key not in PAINT)))
                    for node in face.iter()
                )
                self.assertNotIn(fingerprint, fingerprints, "Two styles share the same face geometry.")
                fingerprints.add(fingerprint)

    def test_accessible_standalone_vectors(self):
        for path in sorted(ART.glob("*.svg")):
            with self.subTest(file=path.name):
                svg = ET.parse(path).getroot()
                self.assertEqual(svg.tag, NS + "svg")
                self.assertEqual(svg.get("viewBox"), "0 0 256 256")
                self.assertEqual(svg.get("role"), "img")
                ids = [node.get("id") for node in svg.iter() if node.get("id")]
                self.assertEqual(len(ids), len(set(ids)))
                labels = svg.get("aria-labelledby", "").split()
                self.assertGreaterEqual(len(labels), 1)
                self.assertTrue(set(labels) <= set(ids))
                self.assertTrue(svg.find(NS + "title").text)
                self.assertTrue(svg.find(NS + "desc").text)
                for node in svg.iter():
                    self.assertIn(node.tag.removeprefix(NS), ALLOWED)
                    for key, value in node.attrib.items():
                        self.assertFalse(key.lower().startswith("on"))
                        if key.endswith("href"):
                            self.assertTrue(value.startswith("#") and value[1:] in ids)


if __name__ == "__main__":
    unittest.main()
