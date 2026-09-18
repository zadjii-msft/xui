"""Check the targeted C03 eye and outer-border updates."""

from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "branding"))
import generate_high_crown_finish as finish


def signature(node):
    return node.tag, tuple(sorted(node.attrib.items())), tuple(signature(child) for child in node)


class HighCrownFinishTests(unittest.TestCase):
    def test_current_exports(self):
        files = finish.outputs()
        self.assertEqual(set(files), {p.name for p in finish.OUTPUT.glob("*.svg")})
        for name, content in files.items():
            self.assertEqual((finish.OUTPUT / name).read_text(encoding="utf-8"), content)

    def test_only_eyes_change_in_plain_version(self):
        source = ET.parse(finish.SOURCE).getroot()
        happy = ET.fromstring(finish.render(False))
        source.find(f".//*[@d='{finish.SLEEPING_EYES}']").set("d", finish.HAPPY_EYES)
        for identity in ("face", "mane", "mane-outline"):
            self.assertEqual(signature(source.find(f".//*[@id='{identity}']")), signature(happy.find(f".//*[@id='{identity}']")))
        self.assertIn("Q100 134 109 148", finish.HAPPY_EYES)
        self.assertIn("Q156 134 165 148", finish.HAPPY_EYES)

    def test_outline_only_follows_perimeter(self):
        plain = ET.fromstring(finish.render(False))
        outlined = ET.fromstring(finish.render(True))
        for identity in ("face", "segments", "mane-backing", "mane-outline"):
            self.assertEqual(signature(plain.find(f".//*[@id='{identity}']")), signature(outlined.find(f".//*[@id='{identity}']")))
        border = outlined.find(".//*[@id='outer-mane-border']")
        self.assertEqual(border.get("mask"), "url(#mane-border-band)")
        band = outlined.find(".//*[@id='mane-border-band']")
        self.assertEqual(len(band), 1)
        stroke = band[0]
        self.assertEqual(stroke.get("d"), plain.find(".//*[@id='mane-backing']").get("d"))
        self.assertTrue(stroke.get("d").endswith("Z"))
        self.assertEqual(stroke.get("fill"), "none")
        self.assertEqual(stroke.get("stroke"), "white")
        self.assertEqual(stroke.get("stroke-width"), "8")
        self.assertEqual(stroke.get("stroke-linejoin"), "round")
        regions = outlined.find(".//*[@id='border-colors']")
        self.assertEqual(len(regions), 6)
        self.assertEqual([node.get("d") for node in regions], [node.get("d") for node in plain.find(".//*[@id='segments']")])
        self.assertEqual([node.get("fill") for node in regions], [finish.darker(c) for c in finish.cloud.enamel_colors()])
        self.assertTrue(all(node.get("stroke") == node.get("fill") and node.get("stroke-width") == "0.8" for node in regions))
        ids = [node.get("id") for node in outlined.iter() if node.get("id")]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(outlined.find(".//*[@id='mane']").get("clip-path"), "url(#mane-outline)")
        face_border = outlined.find(".//*[@id='face-border']")
        self.assertEqual(len(face_border), 3)
        self.assertEqual(face_border.get("stroke"), "#73503C")
        self.assertTrue(all(node.get("fill") == "none" for node in face_border))


if __name__ == "__main__":
    unittest.main()
