import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bindings"))
from contracts import load, validate, validate_native, unique_object, compiler_catalog, native_header


class FeatureContracts(unittest.TestCase):
    def setUp(self):
        self.manifest, self.ids = load()

    def reject(self, change, message):
        manifest = copy.deepcopy(self.manifest)
        change(manifest)
        with self.assertRaisesRegex(ValueError, message):
            validate(manifest, self.ids)

    def test_native_dispatch_parity(self):
        validate_native(self.manifest, self.ids)

    def test_unknown_and_duplicate_control_ids(self):
        self.reject(lambda m: m["controls"].append(["Unknown", "XUI_UNKNOWN", "control"]), "Unknown/duplicate")
        self.reject(lambda m: m["controls"].append(m["controls"][0]), "Unknown/duplicate")
        ids = dict(self.ids, XUI_F_VALUE=self.ids["XUI_F_RANGE"])
        with self.assertRaisesRegex(ValueError, "Duplicate numeric ID"):
            validate(self.manifest, ids)

    def test_duplicate_json_keys(self):
        with self.assertRaisesRegex(ValueError, "Duplicate contract key"):
            json.loads('{"properties": {}, "properties": {}}', object_pairs_hook=unique_object)

    def test_unknown_property_and_shape(self):
        self.reject(lambda m: m["members"][0].__setitem__(2, "XUI_F_UNKNOWN"), "Unknown property")
        self.reject(lambda m: m["members"][0].__setitem__(3, "reflected"), "Unknown property/shape")
        self.reject(lambda m: m["members"][0].__setitem__(3, "double"), "Wrong carrier shape")
        self.reject(lambda m: m["properties"]["XUI_F_RANGE"].__setitem__("fields", 128), "Invalid carrier")

    def test_wrong_property_control_and_operation(self):
        self.reject(lambda m: m["members"][0][0].append("InfoBadge"), "Unsupported setter")
        self.reject(lambda m: m["members"].append([["RangeInput"], "Focused", "XUI_F_FOCUSED", "bool", "get"]),
                    "Wrong carrier shape|Unsupported setter")
        self.reject(lambda m: m["members"][10].__setitem__(4, "get"), "Unsupported getter")

    def test_wrong_action(self):
        self.reject(lambda m: m["actions"][0].__setitem__(2, "XUI_A_UNKNOWN"), "Unknown action")
        self.reject(lambda m: m["actions"][0][0].append("InfoBadge"), "Unsupported action")

    def test_wrong_event(self):
        self.reject(lambda m: m["markup"]["Button"]["arguments"]["click"].__setitem__("native_event", "XUI_CHANGE"),
                    "Unsupported event")
        self.reject(lambda m: m["markup"]["Button"]["arguments"]["click"].__setitem__("type", "double"),
                    "Unsupported event/shape")

    def test_unsupported_language(self):
        self.reject(lambda m: m["coverage"]["NumericInput"].__setitem__("markup", True), "Unsupported markup")
        self.reject(lambda m: m["coverage"]["RangeInput"].__setitem__("rust", False), "Unsupported generated")

    def test_cardinality_and_mutability(self):
        self.reject(lambda m: m["markup"]["Popup"].__setitem__("children", -2), "Invalid child")
        self.reject(lambda m: m["markup"]["RangeInput"]["arguments"]["range"].__setitem__("mode", "mutable"),
                    "Incomplete mutable")
        self.reject(lambda m: m["markup"]["Button"]["arguments"]["value"].__setitem__("setter", "Ignore()"),
                    "Missing mutable value")
        self.reject(lambda m: m["markup"]["Button"]["binding_order"].append("value"), "Invalid binding order")

    def test_native_wrong_control_and_shape(self):
        path = Path(__file__).resolve().parents[1] / "src" / "c_api_features.inc"
        original = path.read_text()
        for source in (
            original.replace("SetTarget<XUI_F_REVERSED>", "SetTarget<XUI_F_VALUE>"),
            original.replace("fields<XUI_F_REVERSED>", "fields<XUI_F_VALUE>"),
            original.replace("case XUI_F_REVERSED:", "case XUI_F_UNKNOWN:"),
            original.replace("feature<xui::InlineStatus>(n).show()", "feature<xui::InfoBadge>(n).show()"),
        ):
            with self.assertRaises(ValueError):
                validate_native(self.manifest, self.ids, source)

    def test_deterministic_generation(self):
        self.assertEqual(compiler_catalog(self.manifest), compiler_catalog(copy.deepcopy(self.manifest)))
        self.assertEqual(native_header(self.manifest, self.ids), native_header(copy.deepcopy(self.manifest), self.ids))


if __name__ == "__main__":
    unittest.main()
