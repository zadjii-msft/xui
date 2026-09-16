"""Generate shared style identifiers and exact native schema snapshots for bindings."""
import argparse
import ctypes
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent.parent
header = (root / "include" / "xui" / "control_styling.hpp").read_text()
catalog_path = root / "bindings" / "control_style_catalog.json"


def names(kind):
    body = re.search(r"enum class " + kind + r"[^{]*\{(.*?)\};", header, re.S).group(1)
    return [re.match(r"\s*(\w+)", field).group(1) for field in body.split(",") if field.strip()]


def pascal(name):
    return "".join(word[:1].upper() + word[1:] for word in name.split("_"))


def camel(name):
    value = pascal(name)
    return value[:1].lower() + value[1:]


targets, parts = names("StyleTarget"), names("StylePart")
properties = dict((name, 1 << int(bit)) for name, bit in re.findall(r"(\w+)\s*=\s*1u\s*<<\s*(\d+)", header))
states = dict((name, 1 << int(bit)) for name, bit in re.findall(r"(\w+)\s*=\s*1ull\s*<<\s*(\d+)", header))
args = argparse.ArgumentParser()
native = args.add_mutually_exclusive_group()
native.add_argument("--native-library", type=Path)
native.add_argument("--native-executable", type=Path)
args.add_argument("--check", action="store_true", help="Compare catalogs and generated files without writing.")
options = args.parse_args()
defaults = dict(state_properties=None, maximum_font_size=32768, maximum_font_family_utf16=1024,
                font_styles=7, horizontal_alignments=15, vertical_alignments=15)


def normalize(rows):
    result = []
    keys = set()
    for row in rows:
        row = dict(row)
        key = (row["target"], row["part"])
        if key in keys or not (0 <= key[0] < len(targets) and 0 <= key[1] < len(parts)):
            raise ValueError(f"Invalid or duplicate style schema key: {key}")
        keys.add(key)
        for name, value in defaults.items():
            row.setdefault(name, row["properties"] if name == "state_properties" else value)
        expected = ("target", "part", "properties", "states", *defaults)
        if set(row) != set(expected):
            raise ValueError(f"Unexpected style schema fields: {key}")
        row["maximum_font_size"] = ctypes.c_float(row["maximum_font_size"]).value
        row = {name: row[name] for name in expected}
        if row["properties"] & ~sum(properties.values()) or row["states"] & ~sum(states.values()):
            raise ValueError(f"Unknown property or state bits: {key}")
        if row["state_properties"] & ~row["properties"]:
            raise ValueError(f"State properties exceed the part schema: {key}")
        if not (0 < row["maximum_font_size"] <= 32768 and 0 < row["maximum_font_family_utf16"] <= 1024):
            raise ValueError(f"Invalid font limits: {key}")
        for name, mask in (("font_styles", 7), ("horizontal_alignments", 15), ("vertical_alignments", 15)):
            if not row[name] or row[name] & ~mask:
                raise ValueError(f"Invalid {name}: {key}")
        result.append(row)
    return sorted(result, key=lambda row: (row["target"], row["part"]))


def emit(path, content):
    content = content.replace("\r\n", "\n")
    if options.check:
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            raise RuntimeError(f"Generated style catalog is stale: {path.relative_to(root)}")
    else:
        path.write_text(content, encoding="utf-8", newline="\n")


schemas = normalize(json.loads(catalog_path.read_text(encoding="utf-8"))["schemas"])
exported = None
if options.native_library:
    library = ctypes.CDLL(str(options.native_library.resolve()))
    lookup = library.xui_control_style_get_schema
    lookup.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64),
                       ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint64)]
    lookup.restype = ctypes.c_int32
    lookup_limits = library.xui_control_style_get_limits
    lookup_limits.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_float),
                             ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
                             ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    lookup_limits.restype = ctypes.c_int32
    exported = []
    for target in range(len(targets)):
        for part in range(len(parts)):
            allowed, active, state_properties = ctypes.c_uint64(), ctypes.c_uint64(), ctypes.c_uint64()
            status = lookup(target, part, ctypes.byref(allowed), ctypes.byref(active), ctypes.byref(state_properties))
            if status == 0:
                font_size, font_length, font_styles = ctypes.c_float(), ctypes.c_uint32(), ctypes.c_uint32()
                horizontal, vertical = ctypes.c_uint32(), ctypes.c_uint32()
                if lookup_limits(target, part, ctypes.byref(font_size), ctypes.byref(font_length), ctypes.byref(font_styles),
                                 ctypes.byref(horizontal), ctypes.byref(vertical)):
                    raise RuntimeError(f"Style limits lookup failed: target={target}, part={part}")
                exported.append(dict(target=target, part=part, properties=allowed.value,
                                    states=active.value, state_properties=state_properties.value,
                                    maximum_font_size=font_size.value, maximum_font_family_utf16=font_length.value,
                                    font_styles=font_styles.value, horizontal_alignments=horizontal.value,
                                    vertical_alignments=vertical.value))
            elif status != 1:
                raise RuntimeError(f"Schema lookup failed: target={target}, part={part}, status={status}")
elif options.native_executable:
    output = subprocess.run([str(options.native_executable.resolve()), str(len(targets)), str(len(parts))],
                            check=True, capture_output=True, text=True, encoding="utf-8")
    exported = json.loads(output.stdout)["schemas"]
if exported is not None:
    exported = normalize(exported)
    if options.check:
        if exported != schemas:
            raise RuntimeError("Native style schemas do not match bindings/control_style_catalog.json")
    else:
        schemas = exported
        emit(catalog_path, json.dumps({"schemas": schemas}, indent=2) + "\n")
implemented = sorted(set(s["target"] for s in schemas))
banner = "// Generated by bindings/generate_control_styles.py. Do not edit.\n"
cs = [banner, "namespace Xui;\n"]
for kind, values, backing in [
    ("StyleTarget", dict((name, index) for index, name in enumerate(targets)), "uint"),
    ("StylePart", dict((name, index) for index, name in enumerate(parts)), "uint"),
    ("StyleState", states, "ulong"),
    ("StyleProperty", properties, "ulong"),
]:
    if kind in ("StyleState", "StyleProperty"):
        cs.append("[Flags]\n")
    cs.append(f"public enum {kind} : {backing} {{\n")
    cs.extend(f"    {pascal(name)} = {value}UL,\n" if backing == "ulong" else f"    {pascal(name)} = {value},\n" for name, value in values.items())
    cs.append("}\n")
cs.append("internal static class StyleSchemaCatalog {\n")
cs.append("    internal static bool HasTarget(StyleTarget target) => (uint)target is " + " or ".join(str(target) for target in implemented) + ";\n")
cs.append("    internal static (ulong Properties, ulong States, ulong StateProperties)? Find(StyleTarget target, StylePart part) => ((uint)target, (uint)part) switch {\n")
cs.extend(f"        ({s['target']}, {s['part']}) => ({s['properties']}UL, {s['states']}UL, {s['state_properties']}UL),\n" for s in schemas)
cs.append("        _ => null\n    };\n")
cs.append("    internal static (float FontSize, uint FontFamilyUtf16, uint FontStyles, uint HorizontalAlignments, uint VerticalAlignments) Limits(StyleTarget target, StylePart part) => ((uint)target, (uint)part) switch {\n")
cs.extend(f"        ({s['target']}, {s['part']}) => ({s['maximum_font_size']}f, {s['maximum_font_family_utf16']}u, {s['font_styles']}u, {s['horizontal_alignments']}u, {s['vertical_alignments']}u),\n" for s in schemas)
cs.append("        _ => (32768f, 1024u, 7u, 15u, 15u)\n    };\n}\n")
emit(root / "bindings" / "dotnet" / "Xui" / "ControlStyleCatalog.g.cs", "".join(cs))

rs = [banner]
for kind, values, backing in [
    ("StyleTarget", dict((name, index) for index, name in enumerate(targets)), "u32"),
    ("StylePart", dict((name, index) for index, name in enumerate(parts)), "u32"),
    ("StyleState", states, "u64"),
]:
    rs.append(f"#[derive(Clone, Copy, Debug, PartialEq, Eq)]\n#[repr({backing})]\npub enum {kind} {{\n")
    rs.extend(f"    {pascal(name)} = {value},\n" for name, value in values.items())
    rs.append("}\n")
rs.append("fn style_schema(target: StyleTarget, part: StylePart) -> Option<(u64, u64, u64)> {\n    match (target as u32, part as u32) {\n")
rs.extend(f"        ({s['target']}, {s['part']}) => Some(({s['properties']}, {s['states']}, {s['state_properties']})),\n" for s in schemas)
rs.append("        _ => None,\n    }\n}\n")
rs.append("fn style_target_supported(target: StyleTarget) -> bool { matches!(target as u32, " + " | ".join(str(target) for target in implemented) + ") }\n")
rs.append("fn style_limits(target: StyleTarget, part: StylePart) -> (f32, usize, u32, u32, u32) {\n    match (target as u32, part as u32) {\n")
rs.extend(f"        ({s['target']}, {s['part']}) => ({s['maximum_font_size']}f32, {s['maximum_font_family_utf16']}, {s['font_styles']}, {s['horizontal_alignments']}, {s['vertical_alignments']}),\n" for s in schemas)
rs.append("        _ => (32768.0, 1024, 7, 15, 15),\n    }\n}\n")
emit(root / "bindings" / "rust" / "xui" / "src" / "control_style_catalog.g.rs", "".join(rs))

compiler = [banner, "namespace Xui.Generator;\ninternal static class StyleCatalog {\n"]
compiler.append("    internal static bool TargetExists(string target) => target switch {\n")
compiler.extend(f'        "{pascal(targets[target])}" => true,\n' for target in implemented)
compiler.append("        _ => false\n    };\n")
for method, field, vocabulary in [("Properties", "properties", properties), ("States", "states", states),
                                  ("StateProperties", "state_properties", properties)]:
    compiler.append(f"    internal static string[] {method}(string target, string part) => (target, part) switch {{\n")
    for s in schemas:
        values = ", ".join(json.dumps(camel(name)) for name, bit in vocabulary.items() if s[field] & bit)
        compiler.append(f'        ("{pascal(targets[s["target"]])}", "{camel(parts[s["part"]])}") => [{values}],\n')
    compiler.append("        _ => []\n    };\n")
compiler.append("    internal static (float FontSize, uint FontFamilyUtf16, uint FontStyles, uint HorizontalAlignments, uint VerticalAlignments) Limits(string target, string part) => (target, part) switch {\n")
compiler.extend(f'        ("{pascal(targets[s["target"]])}", "{camel(parts[s["part"]])}") => ({s["maximum_font_size"]}f, {s["maximum_font_family_utf16"]}u, {s["font_styles"]}u, {s["horizontal_alignments"]}u, {s["vertical_alignments"]}u),\n' for s in schemas)
compiler.append("        _ => (32768f, 1024u, 7u, 15u, 15u)\n    };\n}\n")
emit(root / "bindings" / "dotnet" / "Xui.Generator" / "StyleCatalog.g.cs", "".join(compiler))

checks = [banner, '#pragma once\n#include "xui/control_styling.hpp"\n#include "xui/xui.h"\n']
for name in targets:
    checks.append(f"static_assert(static_cast<uint32_t>(xui::StyleTarget::{name}) == static_cast<uint32_t>(XUI_STYLE_TARGET_{name.upper()}));\n")
for name in parts:
    constant = name.upper() + "_PART" if name in ("text", "error", "empty") else name.upper()
    checks.append(f"static_assert(static_cast<uint32_t>(xui::StylePart::{name}) == static_cast<uint32_t>(XUI_STYLE_{constant}));\n")
for name in properties:
    checks.append(f"static_assert(xui::style_property(xui::StyleProperty::{name}) == static_cast<uint64_t>(XUI_STYLE_{name.upper()}));\n")
for name in states:
    prefix = "XUI_STYLE_" if states[name] <= 16 else "XUI_STYLE_STATE_"
    checks.append(f"static_assert(xui::style_states::{name} == static_cast<uint64_t>({prefix}{name.upper()}));\n")
emit(root / "bindings" / "native" / "control_style_catalog_checks.g.hpp", "".join(checks))
