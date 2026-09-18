"""Check Cloud Crown geometry, source preservation, and the Enamel palette."""

from pathlib import Path
import re
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "branding"))
import generate_cloud_crown as cloud
import generate_softling as softling


def signature(node, omit=()):
    return (
        node.tag, tuple(sorted((key, value) for key, value in node.attrib.items() if key not in omit)),
        tuple(signature(child, omit) for child in node),
    )


class CloudCrownTests(unittest.TestCase):
    def test_complete_current_exports(self):
        entries = cloud.variants()
        self.assertEqual([entry["label"] for entry in entries], [f"C{i:02d}" for i in range(1, 7)])
        files = cloud.outputs()
        self.assertEqual(len(files), 7)
        self.assertEqual({p.name for p in cloud.OUTPUT.glob("*.svg")}, {name for name in files if name.endswith(".svg")})
        for name, content in files.items():
            with self.subTest(file=name):
                self.assertEqual((cloud.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_exact_enamel_fills_and_softling_face(self):
        colors = cloud.enamel_colors()
        self.assertEqual(colors, ["#ED6B79", "#F1A355", "#F0D260", "#75BA94", "#75AED7", "#AB8AC9"])
        face, _ = softling.source_art()
        for contour in cloud.CONTOURS:
            with self.subTest(contour=contour["id"]):
                svg = ET.fromstring(cloud.render(contour, face, colors))
                self.assertEqual(signature(svg.find(".//*[@id='face']")), signature(face))
                pieces = svg.find(".//*[@id='segments']")
                self.assertEqual([piece.get("fill") for piece in pieces], colors)
                self.assertEqual([piece.get("stroke") for piece in pieces], colors)
                self.assertEqual(len(pieces), 6)
                self.assertIsNone(svg.find(".//*[@id='dividers']"))
                self.assertFalse(any(node.get("mask") for node in svg.iter()))
                self.assertIsNone(svg.find(f".//{{{softling.NS}}}mask"))

    def test_anchor_is_exactly_m10_recolored(self):
        m10 = ET.parse(softling.OUTPUT / "m10-cloud-joined.svg").getroot()
        c01 = ET.fromstring(cloud.outputs()["c01-cloud-crown.svg"])
        for identity in ("mane", "mane-outline", "face"):
            self.assertEqual(
                signature(c01.find(f".//*[@id='{identity}']"), ("fill", "stroke")),
                signature(m10.find(f".//*[@id='{identity}']"), ("fill", "stroke")),
            )

    def test_six_distinct_closed_contours_with_shared_joins(self):
        outlines = set()
        for contour in cloud.CONTOURS:
            for arcs, endpoint in zip(contour["arcs"], contour["points"][1:], strict=True):
                self.assertEqual(arcs[-1][-1], endpoint)
            pieces, outline, boundaries = softling.geometry(contour, cloud.JOIN)
            outlines.add(outline)
            for index, piece in enumerate(pieces):
                self.assertTrue(piece.startswith(softling.curve_path(boundaries[index])))
                self.assertTrue(piece.endswith(softling.curve_text(softling.reverse(boundaries[index + 1])) + "Z"))
        self.assertEqual(len(outlines), 6)

    def test_standalone_accessible_vectors(self):
        ns = f"{{{softling.NS}}}"
        for filename, content in cloud.outputs().items():
            if not filename.endswith(".svg"):
                continue
            with self.subTest(file=filename):
                svg = ET.fromstring(content)
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
