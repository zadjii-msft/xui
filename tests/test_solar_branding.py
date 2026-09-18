"""Check the generated Solar studies without browser or image dependencies."""

from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "branding"))
import generate_solar as solar
import generate_solar_explorations as explorations


class SolarBrandingTests(unittest.TestCase):
    def test_exports_are_current(self):
        expected = solar.outputs()
        self.assertEqual(len(expected), 43)
        self.assertEqual(
            {path.name for path in solar.OUTPUT.glob("*.svg")},
            {name for name in expected if name.endswith(".svg")},
        )
        for name, content in expected.items():
            with self.subTest(file=name):
                self.assertEqual((solar.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_six_distinct_manes_and_seven_palettes(self):
        self.assertEqual(len(solar.MANES), 6)
        self.assertEqual(len({tuple(mane["paths"]) for mane in solar.MANES}), 6)
        self.assertEqual(len({mane["id"] for mane in solar.MANES}), 6)
        self.assertEqual(len(solar.PALETTES), 7)
        self.assertEqual(len({palette["id"] for palette in solar.PALETTES}), 7)

    def test_face_geometry_and_rainbow_colors_match_original(self):
        original = solar.original_face()
        for mane in solar.MANES:
            for palette in solar.PALETTES:
                with self.subTest(shape=mane["id"], palette=palette["id"]):
                    root = ET.fromstring(solar.render(mane, palette, original))
                    face = root.find(".//*[@id='face']")
                    self.assertEqual(len(face), 7)
                    for source, output in zip(original, face, strict=True):
                        if palette["id"] == "rainbow":
                            self.assertEqual(source.attrib, output.attrib)
                        else:
                            exclude_colors = lambda attrs: {
                                key: value for key, value in attrs.items()
                                if key not in ("fill", "stroke")
                            }
                            self.assertEqual(exclude_colors(source.attrib), exclude_colors(output.attrib))
                            for key in ("fill", "stroke"):
                                if output.get(key) and output.get(key) != "none":
                                    self.assertIn(output.get(key), palette["face"])

    def test_six_separate_segments_with_stable_status_geometry(self):
        for mane in solar.MANES:
            geometry = None
            for palette in solar.PALETTES:
                with self.subTest(shape=mane["id"], palette=palette["id"]):
                    root = ET.fromstring(solar.render(mane, palette, solar.original_face()))
                    segments = root.find(".//*[@id='mane']")
                    self.assertEqual(len(segments), 6)
                    current = [(path.get("d"), path.get("transform")) for path in segments]
                    if geometry is None:
                        geometry = current
                    self.assertEqual(current, geometry)
                    fills = {path.get("fill") for path in segments}
                    self.assertEqual(len(fills), 6 if palette["id"] == "rainbow" else 1)
                    self.assertEqual(
                        [path.get("id") for path in segments],
                        [f"segment-{index}" for index in range(1, 7)],
                    )

    def test_accessible_standalone_vector_exports(self):
        namespace = f"{{{solar.NS}}}"
        allowed = {"svg", "title", "desc", "g", "path"}
        for name, content in solar.outputs().items():
            if not name.endswith(".svg"):
                continue
            with self.subTest(file=name):
                root = ET.fromstring(content)
                self.assertEqual(root.get("viewBox"), "0 0 256 256")
                self.assertEqual(root.get("role"), "img")
                self.assertEqual(root.get("aria-labelledby"), "title desc")
                self.assertTrue(root.find(namespace + "title").text)
                self.assertTrue(root.find(namespace + "desc").text)
                for element in root.iter():
                    self.assertIn(element.tag.removeprefix(namespace), allowed)
                    self.assertFalse(any(key.startswith("on") or key.endswith("href") for key in element.attrib))


class SolarExplorationTests(unittest.TestCase):
    def test_complete_cross_product_and_current_exports(self):
        entries = explorations.variants()
        self.assertEqual(len(entries), 48)
        self.assertEqual(len({entry["id"] for entry in entries}), 48)
        self.assertEqual(
            {(entry["mane"], entry["face"]) for entry in entries},
            {(mane["id"], face["id"]) for mane in explorations.MANES for face in explorations.FACES},
        )
        expected = explorations.outputs()
        self.assertEqual(len(expected), 337)
        self.assertEqual(
            {path.name for path in explorations.OUTPUT.glob("*.svg")},
            {name for name in expected if name.endswith(".svg")},
        )
        for name, content in expected.items():
            with self.subTest(file=name):
                self.assertEqual((explorations.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_substantial_proportion_changes_and_distinct_manes(self):
        self.assertEqual(len({tuple(mane["paths"]) for mane in explorations.MANES}), 12)
        faces = {face["id"]: face for face in explorations.FACES}
        self.assertGreater(faces["large"]["scaleX"] / faces["small"]["scaleX"], 1.7)
        self.assertGreater(faces["large"]["scaleY"] / faces["small"]["scaleY"], 1.7)
        self.assertGreater(faces["wide"]["scaleX"] / faces["wide"]["scaleY"], 1.4)
        self.assertLess(faces["small"]["scaleX"], faces["balanced"]["scaleX"])
        self.assertLess(faces["balanced"]["scaleX"], faces["large"]["scaleX"])

    def test_source_expression_is_preserved_and_only_group_is_resized(self):
        source = solar.original_face()
        for mane in explorations.MANES:
            for proportion in explorations.FACES:
                with self.subTest(mane=mane["id"], face=proportion["id"]):
                    root = ET.fromstring(explorations.render(mane, proportion, solar.PALETTES[0], source))
                    face = root.find(".//*[@id='face']")
                    self.assertEqual([path.attrib for path in face], [path.attrib for path in source])
                    self.assertEqual(
                        face.get("transform"),
                        f"translate(128 144) scale({proportion['scaleX']:g} {proportion['scaleY']:g}) translate(-128 -132)",
                    )
                    segments = root.find(".//*[@id='mane']")
                    self.assertEqual(len(segments), 6)
                    self.assertEqual(len({path.get("fill") for path in segments}), 6)

    def test_palette_changes_do_not_change_geometry(self):
        for mane in explorations.MANES:
            for face in explorations.FACES:
                expected = None
                for palette in solar.PALETTES:
                    with self.subTest(mane=mane["id"], face=face["id"], palette=palette["id"]):
                        root = ET.fromstring(explorations.render(mane, face, palette, solar.original_face()))
                        geometry = [
                            (node.tag, {key: value for key, value in node.attrib.items() if key not in ("fill", "stroke")})
                            for node in root.iter() if node.tag.rsplit("}", 1)[-1] not in ("title", "desc")
                        ]
                        if expected is None:
                            expected = geometry
                        self.assertEqual(geometry, expected)
                        self.assertEqual(root.get("role"), "img")
                        self.assertEqual(root.get("viewBox"), "0 0 256 256")
                        for node in root.iter():
                            self.assertIn(node.tag.rsplit("}", 1)[-1], {"svg", "title", "desc", "g", "path"})
                        if palette["id"] != "rainbow":
                            self.assertEqual(len({node.get("fill") for node in root.find(".//*[@id='mane']")}), 1)


if __name__ == "__main__":
    unittest.main()
