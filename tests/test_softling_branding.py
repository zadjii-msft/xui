"""Check Softling face preservation and the shared-boundary mane treatments."""

from pathlib import Path
import re
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "branding"))
import generate_softling as softling


def signature(node):
    return (node.tag, tuple(sorted(node.attrib.items())), tuple(signature(child) for child in node))


class SoftlingBrandingTests(unittest.TestCase):
    def test_complete_current_exports(self):
        variants = softling.variants()
        self.assertEqual(len(variants), 16)
        self.assertEqual([item["label"] for item in variants], [f"M{i:02d}" for i in range(1, 17)])
        self.assertEqual(
            {(item["contour"], item["join"]) for item in variants},
            {(contour["id"], join["id"]) for contour in softling.CONTOURS for join in softling.JOINS},
        )
        expected = softling.outputs()
        self.assertEqual(len(expected), 17)
        self.assertEqual(
            {path.name for path in softling.OUTPUT.glob("*.svg")},
            {name for name in expected if name.endswith(".svg")},
        )
        for name, content in expected.items():
            with self.subTest(file=name):
                self.assertEqual((softling.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_original_face_and_palette_are_unchanged(self):
        face, colors = softling.source_art()
        for contour in softling.CONTOURS:
            for join in softling.JOINS:
                with self.subTest(contour=contour["id"], join=join["id"]):
                    svg = ET.fromstring(softling.render(contour, join, face, colors))
                    self.assertEqual(signature(svg.find(".//*[@id='face']")), signature(face))
                    segments = svg.find(".//*[@id='segments']")
                    self.assertEqual(len(segments), 6)
                    self.assertEqual([path.get("fill") for path in segments], colors)
                    self.assertEqual([path.get("id") for path in segments], [f"segment-{i}" for i in range(1, 7)])
                    self.assertIsNone(svg.find(".//*[@id='face']").get("transform"))

    def test_contours_and_shared_boundaries(self):
        outlines = set()
        for contour in softling.CONTOURS:
            expected_outline = None
            straight_boundaries = None
            for join in softling.JOINS:
                with self.subTest(contour=contour["id"], join=join["id"]):
                    pieces, outline, boundaries = softling.geometry(contour, join)
                    self.assertEqual(len(pieces), 6)
                    self.assertEqual(len(boundaries), 7)
                    if expected_outline is None:
                        expected_outline = outline
                        straight_boundaries = boundaries
                    self.assertEqual(outline, expected_outline, "Join treatments must not alter the outer contour.")
                    for index, piece in enumerate(pieces):
                        self.assertTrue(piece.startswith(softling.curve_path(boundaries[index])))
                        self.assertTrue(piece.endswith(softling.curve_text(softling.reverse(boundaries[index + 1])) + "Z"))
                    if join["id"] == "wavy":
                        for straight, wave in zip(straight_boundaries[1:-1], boundaries[1:-1], strict=True):
                            self.assertEqual(straight[0], wave[0])
                            self.assertEqual(straight[-1], wave[-1])
                            self.assertNotEqual(straight[1:3], wave[1:3])
            outlines.add(expected_outline)
        self.assertEqual(len(outlines), 4)

    def test_transparent_gaps_and_opaque_dividers_are_separate_modes(self):
        ns = f"{{{softling.NS}}}"
        face, colors = softling.source_art()
        for join in softling.JOINS:
            svg = ET.fromstring(softling.render(softling.CONTOURS[0], join, face, colors))
            mane = svg.find(".//*[@id='mane']")
            mask = svg.find(".//*[@id='segment-gaps']")
            lines = svg.find(".//*[@id='dividers']")
            with self.subTest(join=join["id"]):
                self.assertIsNotNone(svg.find(".//*[@id='mane-backing']"))
                if join["id"] == "narrow":
                    self.assertEqual(mane.get("mask"), "url(#segment-gaps)")
                    self.assertEqual(mask.get("mask-type"), "luminance")
                    cuts = mask.findall(ns + "path")
                    self.assertEqual(len(cuts), 5)
                    self.assertTrue(all(path.get("stroke-width") == "3" for path in cuts))
                else:
                    self.assertIsNone(mask)
                    self.assertIsNone(mane.get("mask"))
                if join["id"] == "lined":
                    self.assertEqual(len(lines), 5)
                    self.assertEqual(lines.get("stroke-width"), str(softling.LINE_WIDTH))
                else:
                    self.assertIsNone(lines)

    def test_accessible_self_contained_vectors(self):
        ns = f"{{{softling.NS}}}"
        for name, content in softling.outputs().items():
            if not name.endswith(".svg"):
                continue
            with self.subTest(file=name):
                svg = ET.fromstring(content)
                self.assertEqual(svg.get("viewBox"), "0 0 256 256")
                self.assertEqual(svg.get("role"), "img")
                self.assertEqual(svg.get("aria-labelledby"), "title desc")
                self.assertTrue(svg.find(ns + "title").text)
                self.assertTrue(svg.find(ns + "desc").text)
                ids = [node.get("id") for node in svg.iter() if node.get("id")]
                self.assertEqual(len(ids), len(set(ids)))
                self.assertTrue(set(svg.get("aria-labelledby").split()).issubset(ids))
                for node in svg.iter():
                    self.assertIn(node.tag.removeprefix(ns), {"svg", "title", "desc", "g", "defs", "clipPath", "mask", "path", "rect", "circle", "ellipse"})
                    self.assertFalse(any(key.lower().startswith("on") or key.endswith("href") for key in node.attrib))
                    for value in node.attrib.values():
                        for reference in re.findall(r"url\(([^)]+)\)", value):
                            self.assertTrue(reference.startswith("#"))
                            self.assertIn(reference[1:], ids)


if __name__ == "__main__":
    unittest.main()
