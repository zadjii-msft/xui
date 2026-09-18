"""Check the 30-concept lab and preserve the earlier artwork contracts."""

from collections import Counter
from pathlib import Path
import re
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "branding"))
import generate_cloud_crown_lab as lab
import generate_cloud_crown as cloud
import generate_softling as softling


def signature(node):
    return node.tag, tuple(sorted(node.attrib.items())), tuple(signature(child) for child in node)


class CloudCrownLabTests(unittest.TestCase):
    def test_thirty_current_exports_and_six_families(self):
        entries = lab.variants()
        self.assertEqual([entry["label"] for entry in entries], [f"B{i:02d}" for i in range(1, 31)])
        self.assertEqual(Counter(entry["family"] for entry in entries), {
            "Skyward": 5, "Wide & low": 5, "Botanical": 5, "Sculptural": 5, "Offbeat": 5, "Face & frame": 5,
        })
        files = lab.outputs()
        self.assertEqual(len(files), 31)
        self.assertEqual({p.name for p in lab.OUTPUT.glob("*.svg")}, {name for name in files if name.endswith(".svg")})
        for name, content in files.items():
            with self.subTest(file=name):
                self.assertEqual((lab.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_unique_geometry_and_intentional_proportion_changes(self):
        self.assertEqual(len({lab.geometry(item)[1] for item in lab.STUDIES}), 30)
        self.assertGreaterEqual(len({item["face"] for item in lab.STUDIES}), 12)
        self.assertEqual(sum(item["right"] is not None for item in lab.STUDIES), 5)
        for item in lab.STUDIES:
            with self.subTest(concept=item["id"]):
                pieces, outline, boundaries = lab.geometry(item)
                self.assertEqual(len(pieces), 6)
                self.assertEqual(len(boundaries), 7)
                self.assertTrue(outline.endswith("Z"))
                for index, piece in enumerate(pieces):
                    self.assertTrue(piece.startswith(softling.curve_path(boundaries[index])))
                    self.assertTrue(piece.endswith(softling.curve_text(softling.reverse(boundaries[index + 1])) + "Z"))

    def test_enamel_palette_and_original_facial_features(self):
        face, _ = softling.source_art()
        colors = cloud.enamel_colors()
        for item in lab.STUDIES:
            with self.subTest(concept=item["id"]):
                svg = ET.fromstring(lab.render(item, face, colors))
                self.assertEqual(signature(svg.find(".//*[@id='face']")), signature(face))
                self.assertIsNotNone(svg.find(".//*[@id='face-frame']").get("transform"))
                pieces = svg.find(".//*[@id='segments']")
                self.assertEqual([piece.get("fill") for piece in pieces], colors)
                self.assertEqual([piece.get("stroke") for piece in pieces], colors)
                self.assertIsNone(svg.find(".//*[@id='dividers']"))
                self.assertFalse(any(node.get("mask") for node in svg.iter()))

    def test_accessible_standalone_vectors(self):
        ns = f"{{{softling.NS}}}"
        for name, markup in lab.outputs().items():
            if not name.endswith(".svg"):
                continue
            with self.subTest(file=name):
                svg = ET.fromstring(markup)
                self.assertEqual(svg.get("viewBox"), "0 0 256 256")
                self.assertEqual(svg.get("role"), "img")
                self.assertEqual(svg.get("aria-labelledby"), "title desc")
                self.assertTrue(svg.find(ns + "title").text)
                self.assertTrue(svg.find(ns + "desc").text)
                ids = [node.get("id") for node in svg.iter() if node.get("id")]
                self.assertEqual(len(ids), len(set(ids)))
                for node in svg.iter():
                    self.assertIn(node.tag.removeprefix(ns), {"svg", "title", "desc", "defs", "clipPath", "g", "path", "ellipse", "circle", "rect"})
                    self.assertFalse(any(key.lower().startswith("on") or key.endswith("href") for key in node.attrib))
                    for value in node.attrib.values():
                        for reference in re.findall(r"url\(([^)]+)\)", value):
                            self.assertTrue(reference.startswith("#"))
                            self.assertIn(reference[1:], ids)


if __name__ == "__main__":
    unittest.main()
