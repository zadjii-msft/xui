"""Check hand-authored illustration styles, not shape or palette variants."""

from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

from test_d2_styles import ALLOWED, NS, PAINT


ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "docs/specs/branding/cloud-crown-styles"
COLORS = ["#ED6B79", "#F1A355", "#F0D260", "#75BA94", "#75AED7", "#AB8AC9"]


def fingerprint(face):
    return tuple(
        (node.tag, tuple(sorted((key, value) for key, value in node.attrib.items()
                               if key not in PAINT and key != "transform")))
        for node in face.iter()
    )


class CloudCrownStyleTests(unittest.TestCase):
    def test_thirty_individual_drawings(self):
        files = sorted(ART.glob("*.svg"))
        self.assertEqual(len(files), 30)
        self.assertEqual([int(path.name.split("-")[0]) for path in files], list(range(1, 31)))

    def test_redrawn_faces_and_six_enamel_regions(self):
        softling = ET.parse(ROOT / "docs/specs/branding/solar-d2-styles/06-softling.svg").getroot()
        original_face = softling.find(".//*[@id='face']")
        original_paths = {node.get("d") for node in original_face.iter() if node.get("d")}
        faces = {fingerprint(original_face)}
        for path in sorted(ART.glob("*.svg")):
            with self.subTest(file=path.name):
                svg = ET.parse(path).getroot()
                mane = svg.find(".//*[@id='mane']")
                face = svg.find(".//*[@id='face']")
                self.assertIsNotNone(mane)
                self.assertIsNotNone(face)
                segments = mane.findall(NS + "g")
                self.assertEqual(len(segments), 6)
                self.assertEqual([node.get("id") for node in segments], [f"segment-{i}" for i in range(1, 7)])
                self.assertTrue(all(len(node) for node in segments))
                for segment, color in zip(segments, COLORS, strict=True):
                    paints = {value.upper() for node in segment.iter() for value in node.attrib.values()}
                    self.assertIn(color, paints, "Each segment must retain its primary Enamel color.")
                geometry = fingerprint(face)
                self.assertNotIn(geometry, faces, "The face must be newly drawn, not the same geometry repainted or transformed.")
                faces.add(geometry)
                paths = {node.get("d") for node in face.iter() if node.get("d")}
                self.assertLessEqual(len(paths & original_paths), 1, "Do not preserve the previous Softling face paths.")

    def test_accessible_standalone_vectors(self):
        for path in sorted(ART.glob("*.svg")):
            with self.subTest(file=path.name):
                svg = ET.parse(path).getroot()
                self.assertEqual(svg.tag, NS + "svg")
                self.assertEqual(svg.get("viewBox"), "0 0 256 256")
                self.assertEqual(svg.get("role"), "img")
                ids = [node.get("id") for node in svg.iter() if node.get("id")]
                self.assertEqual(len(ids), len(set(ids)))
                self.assertTrue(svg.find(NS + "title").text)
                self.assertTrue(svg.find(NS + "desc").text)
                labels = svg.get("aria-labelledby", "").split()
                self.assertGreaterEqual(len(labels), 1)
                self.assertTrue(set(labels).issubset(ids))
                for node in svg.iter():
                    self.assertIn(node.tag.removeprefix(NS), ALLOWED)
                    for key, value in node.attrib.items():
                        self.assertFalse(key.lower().startswith("on"))
                        if key.endswith("href"):
                            self.assertTrue(value.startswith("#") and value[1:] in ids)
                        for reference in re.findall(r"url\(([^)]+)\)", value):
                            reference = reference.strip("'\"")
                            self.assertTrue(reference.startswith("#"))
                            self.assertIn(reference[1:], ids)


if __name__ == "__main__":
    unittest.main()
