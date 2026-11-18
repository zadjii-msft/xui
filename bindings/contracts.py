"""Validated feature and markup contracts shared by the binding generators."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def require(condition, message):
    if not condition:
        raise ValueError(message)


def enum_values(header):
    header = re.sub(r"/\*.*?\*/|//[^\n]*", "", header, flags=re.S)
    result = {}
    for body in re.findall(r"enum\s*\{(.*?)\};", header, re.S):
        value = -1
        for field in body.split(","):
            field = field.strip()
            if not field:
                continue
            match = re.fullmatch(r"(XUI_\w+)(?:\s*=\s*(0x[0-9a-fA-F]+|\d+))?", field)
            if not match:
                continue
            name, number = match.groups()
            value = int(number, 0) if number else value + 1
            require(name not in result, f"Duplicate native ID: {name}")
            result[name] = value
    return result


def load():
    manifest = json.loads((ROOT / "bindings/features.json").read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    for kind, node in manifest["markup"].items():
        for name, reference in node["arguments"].items():
            require(isinstance(reference, str) and reference in manifest["shared_arguments"],
                    f"Unknown argument contract: {kind}.{name}")
            node["arguments"][name] = dict(manifest["shared_arguments"][reference])
    header = (ROOT / "include/xui/xui.h").read_text(encoding="utf-8") + (ROOT / "include/xui/xui_features.h").read_text(encoding="utf-8")
    ids = enum_values(header)
    validate(manifest, ids)
    return manifest, ids


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate contract key: {key}")
        result[key] = value
    return result


SHAPES = {
    "double": 1, "bool": 16, "ulong": 16, "uint": 16, "string": 64,
    "range": 15, "selection": 48, "color": 16, "Axis": 16, "ButtonIcon": 16,
    "CheckState": 16, "ProgressState": 16, "ItemsPresentation": 16,
    "CompactNavigation": 16, "AdaptivePresentation": 16, "NavigationSide": 16,
}
CATEGORIES = {"control", "combo", "content", "panel", "two", "popup", "target",
              "document", "secret", "date", "contentPopup", "twoControl"}
MODES = {"mutable", "construction", "event", "reference", "placement", "special"}


def validate(manifest, ids):
    require(set(manifest) == {"controls", "members", "actions", "native_types", "properties", "markup",
                             "coverage", "action_targets", "creation_modes", "events", "shared_arguments"},
            "Unknown or missing contract section")
    for prefix in ("XUI_F_", "XUI_A_"):
        values = [value for symbol, value in ids.items() if symbol.startswith(prefix)]
        require(len(values) == len(set(values)), f"Duplicate numeric ID: {prefix}")
    controls, numbers = {}, set()
    for name, symbol, category in manifest["controls"]:
        require(name not in controls and symbol in ids and ids[symbol] not in numbers,
                f"Unknown/duplicate control ID: {name}/{symbol}")
        require(category in CATEGORIES, f"Unknown constructor category: {category}")
        controls[name] = symbol
        numbers.add(ids[symbol])
    properties = manifest["properties"]
    require(set(properties) == {s for s in ids if s.startswith("XUI_F_")},
            "Property contract must cover every native feature property exactly once")
    for symbol, row in properties.items():
        require(set(row) == {"fields", "set", "get", "native_set", "native_get"}, f"Invalid property schema: {symbol}")
        require(type(row["fields"]) is int and 0 <= row["fields"] <= 127,
                f"Invalid carrier shape: {symbol}")
        require(row["set"] or row["get"], f"Property has no operation: {symbol}")
        require(bool(row["fields"]) == bool(row["set"]), f"Read-only property has setter fields: {symbol}")
        for operation in ("set", "get"):
            require(len(row[operation]) == len(set(row[operation])), f"Duplicate target: {symbol}")
            target = row["native_" + operation]
            require(target is None or re.fullmatch(r"[A-Z]\w*", target), f"Invalid native target type: {symbol}")
            if target:
                require(set(manifest["native_types"].get(target, [target])) <= set(row[operation]),
                        f"Native type exceeds supported targets: {symbol}")
    seen = set()
    for targets, member, symbol, shape, getter in manifest["members"]:
        require(symbol in properties and shape in SHAPES, f"Unknown property/shape: {symbol}/{shape}")
        require(SHAPES[shape] == properties[symbol]["fields"], f"Wrong carrier shape: {symbol}/{shape}")
        require(getter in ("", "get"), f"Invalid accessor: {symbol}")
        for target in targets:
            require(target in controls and (target, member) not in seen, f"Unknown/duplicate member: {target}.{member}")
            require(target in properties[symbol]["set"], f"Unsupported setter: {target}/{symbol}")
            require(not getter or target in properties[symbol]["get"], f"Unsupported getter: {target}/{symbol}")
            seen.add((target, member))
    seen = set()
    for targets, method, symbol in manifest["actions"]:
        require(symbol in manifest["action_targets"], f"Unknown action ID: {symbol}")
        for target in targets:
            require(target in controls and (target, method) not in seen, f"Unknown/duplicate action: {target}.{method}")
            require(target in manifest["action_targets"][symbol], f"Unsupported action: {target}/{symbol}")
            seen.add((target, method))
    for kind, node in manifest["markup"].items():
        require(set(node) == {"type", "children", "stack", "control", "arguments", "binding_order"}, f"Invalid markup control: {kind}")
        require(type(node["children"]) is int and node["children"] >= -1, f"Invalid child cardinality: {kind}")
        require(type(node["stack"]) is bool and type(node["control"]) is bool, f"Invalid markup flags: {kind}")
        for name, argument in node["arguments"].items():
            require(argument["mode"] in MODES, f"Unknown argument mode: {kind}.{name}")
            if argument["mode"] == "mutable":
                require(set(argument) == {"mode", "type", "setter", "default", "always"},
                        f"Incomplete mutable adapter: {kind}.{name}")
                require("{0}" in argument["setter"], f"Missing mutable value: {kind}.{name}")
            elif argument["mode"] == "event":
                require(set(argument) == {"mode", "type", "subscribe", "identifier", "native_event"}, f"Incomplete event: {kind}.{name}")
                require("{0}" in argument["subscribe"], f"Missing event adapter: {kind}.{name}")
                require(type(argument["identifier"]) is bool, f"Invalid handler shape: {kind}.{name}")
                require(argument["native_event"] in ids and
                        manifest["events"].get(kind, {}).get(argument["native_event"]) == argument["type"],
                        f"Unsupported event/shape: {kind}.{name}")
            else:
                require(set(argument) == {"mode"}, f"Unexpected argument metadata: {kind}.{name}")
        require(node["stack"] or "value" in node["arguments"], f"Missing constructor argument: {kind}")
        mutable = {k for k, a in node["arguments"].items() if a["mode"] == "mutable"}
        require(set(node["binding_order"]) == mutable and len(node["binding_order"]) == len(mutable),
                f"Invalid binding order: {kind}")
    for name, row in manifest["coverage"].items():
        require(set(row) == {"c", "csharp", "rust", "markup"}, f"Invalid language coverage: {name}")
        require(all(type(v) is bool for v in row.values()), f"Invalid coverage flag: {name}")
        require(row["markup"] == (name in manifest["markup"]), f"Unsupported markup surface: {name}")
        if name in controls:
            require(row["c"] and row["csharp"] and row["rust"], f"Unsupported generated adapter: {name}")
    require(set(controls) | set(manifest["markup"]) == set(manifest["coverage"]), "Incomplete language coverage")
    require(set(manifest["action_targets"]) == {s for s in ids if s.startswith("XUI_A_")}, "Incomplete action coverage")
    require(set(manifest["events"]) <= set(manifest["markup"]), "Unknown event target")
    require(all(name in controls and type(mode) is int and 0 < mode <= 2
                for name, mode in manifest["creation_modes"].items()), "Invalid constructor mode")


def native_header(manifest, ids):
    types = sorted({row[key] for row in manifest["properties"].values()
                    for key in ("native_set", "native_get") if row[key]})
    lines = ["// Generated by bindings/generate_features.py. Do not edit.",
             "#pragma once", "namespace xui { " + " ".join(f"class {name};" for name in types) + " }",
             "namespace xui::feature_contract {",
             "template<unsigned Property> inline constexpr unsigned fields = 0;"]
    for symbol, row in manifest["properties"].items():
        lines.append(f"template<> inline constexpr unsigned fields<{symbol}> = {row['fields']};")
    for operation in ("set", "get"):
        name = operation.title()
        lines += [f"template<unsigned Property> struct {name}Property;",
                  f"template<unsigned Property> using {name}Target = typename {name}Property<Property>::Target;"]
        for symbol, row in manifest["properties"].items():
            if row["native_" + operation]:
                lines.append(f"template<> struct {name}Property<{symbol}> {{ using Target = xui::{row['native_' + operation]}; }};")
    lines += ["#ifdef XUI_FEATURE_CATALOG", "constexpr unsigned maximum_mode(unsigned kind) {", "    switch (kind) {"]
    symbols = {name: symbol for name, symbol, _ in manifest["controls"]}
    lines += [f"    case {symbols[name]}: return {mode};" for name, mode in manifest["creation_modes"].items()]
    lines += ["    default: return 0;", "    }", "}"]
    lines += ["struct Property { unsigned id, fields; const char* setters; const char* getters; };",
              "inline constexpr Property properties[] = {"]
    for symbol, row in manifest["properties"].items():
        lines.append(f'    {{{symbol}, fields<{symbol}>, "{",".join(row["set"])}", "{",".join(row["get"])}"}},')
    lines += ["};", "struct Control { unsigned id, children, mode, languages; };", "inline constexpr Control controls[] = {"]
    for name, symbol, category in manifest["controls"]:
        count = 2 if category in ("two", "twoControl") else 1 if category in ("content", "contentPopup", "target") else 0
        coverage = manifest["coverage"][name]
        languages = sum(1 << i for i, lang in enumerate(("c", "csharp", "rust", "markup")) if coverage[lang])
        lines.append(f"    {{{symbol}, {count}, maximum_mode({symbol}), {languages}}},")
    lines += ["};", "#endif"]
    lines += ["}", ""]
    return "\n".join(lines)


def creation_validation(manifest):
    def kinds(categories):
        return " || ".join(f"kind == {symbol}" for _, symbol, category in manifest["controls"] if category in categories)
    symbols = {name: symbol for name, symbol, _ in manifest["controls"]}
    modes = []
    for mode in sorted(set(manifest["creation_modes"].values())):
        targets = " || ".join(f"kind == {symbols[name]}" for name, maximum in manifest["creation_modes"].items() if maximum == mode)
        modes.append(f"(({targets}) && o->mode <= {mode})")
    return "\n".join([
        "// Generated by bindings/generate_features.py. Do not edit.",
        f"const bool one = {kinds({'content', 'contentPopup'})};",
        f"const bool two = {kinds({'two', 'twoControl'})};",
        f'require((one || two || {kinds({"target"})}) == bool(o->content) && two == bool(o->second), XUI_INVALID_ARGUMENT, "Invalid composition children.");',
        'require(' + " || ".join(modes) + ' || !o->mode, XUI_INVALID_ARGUMENT, "Invalid creation mode.");',
        "",
    ])


def validate_native(manifest, ids, source=None):
    """Keep handwritten dispatch targets and operations in the declared contract."""
    source = source if source is not None else (ROOT / "src/c_api_features.inc").read_text(encoding="utf-8")
    hierarchy = manifest["native_types"]
    for operation, end in (("set", "get"), ("get", "action")):
        section = source.split(f"xui_status XUI_CALL xui_feature_{operation}(")[1].split(
            f"xui_status XUI_CALL xui_feature_{end}(")[0]
        found = {}
        for symbol, body in re.findall(r"case (XUI_F_\w+):(.*?)(?=case XUI_F_|default:)", section, re.S):
            require(symbol not in found, f"Duplicate native dispatch: {symbol}")
            types = re.findall(r"as<xui::(\w+)>\(n\)", body)
            require(not re.search(r"feature<xui::\w+>\(n\)", body),
                    f"Native setter/getter bypasses its declared target: {symbol}")
            selectors = re.findall(r"feature<xui::feature_contract::(\w+)Target<(XUI_F_\w+)>>\(n\)", body)
            expected_type = manifest["properties"].get(symbol, {}).get("native_" + operation)
            require(all(selector == (operation.title(), symbol) for selector in selectors) and
                    bool(selectors) == bool(expected_type), f"Wrong typed native dispatch: {symbol}")
            if expected_type:
                types.append(expected_type)
            if "control(n)" in body:
                types.append("Control")
            found[symbol] = sorted({t for name in types for t in hierarchy.get(name, [name])})
            if operation == "set":
                require(re.findall(r"fields\(v, xui::feature_contract::fields<(XUI_F_\w+)>\)", body) == [symbol],
                        f"Native setter bypasses its declared carrier shape: {symbol}")
        expected = {symbol: row[operation] for symbol, row in manifest["properties"].items() if row[operation]}
        require(found == expected, f"Native {operation} targets differ from the feature contract")
    section = source.split("xui_status XUI_CALL xui_feature_create(")[1].split("xui_status XUI_CALL xui_feature_set(")[0]
    require(set(re.findall(r"case (XUI_\w+):", section)) == {c[1] for c in manifest["controls"]},
            "Native feature constructors differ from the feature contract")
    section = source.split("xui_status XUI_CALL xui_feature_action(")[1].split("\nxui_status ")[0]
    found = {}
    for symbol, body in re.findall(r"case (XUI_A_\w+):(.*?)(?=case XUI_A_|default:)", section, re.S):
        types = re.findall(r"(?:feature|as|resource)<xui::(\w+)>\(n(?:,|\))", body)
        require(symbol not in found, f"Duplicate native action: {symbol}")
        found[symbol] = sorted({t for name in types for t in hierarchy.get(name, [name])})
    require(found == manifest["action_targets"], "Native actions differ from the feature contract")


def native_snapshot(manifest, ids):
    properties = [[ids[s], r["fields"], ",".join(r["set"]), ",".join(r["get"])] for s, r in manifest["properties"].items()]
    controls = []
    for name, symbol, category in manifest["controls"]:
        count = 2 if category in ("two", "twoControl") else 1 if category in ("content", "contentPopup", "target") else 0
        coverage = manifest["coverage"][name]
        controls.append([ids[symbol], count, manifest["creation_modes"].get(name, 0),
                         sum(1 << i for i, lang in enumerate(("c", "csharp", "rust", "markup")) if coverage[lang])])
    return {"properties": properties, "controls": controls}


def compiler_catalog(manifest):
    quote = lambda value: json.dumps(value, ensure_ascii=True)
    lines = ["// Generated by bindings/generate_features.py. Do not edit.",
             "#nullable enable", "namespace Xui.Generator;",
             "internal readonly record struct ContractBinding(string Name, string Type, string Setter, string Default, bool Always);",
             "internal readonly record struct ContractEvent(string Name, string Type, string Subscribe, bool Identifier);",
             "internal static class ControlContract", "{"]
    nodes = manifest["markup"]
    for method, result, values, fallback in (
        ("Arguments", "string[]?", lambda n: "[" + ", ".join(quote(k) for k in n["arguments"]) + "]", "null"),
        ("Type", "string", lambda n: quote(n["type"]), "throw new System.ArgumentException(\"Unknown markup control\", nameof(kind))"),
        ("Children", "int", lambda n: str(n["children"]), "throw new System.ArgumentException(\"Unknown markup control\", nameof(kind))"),
        ("Stack", "bool", lambda n: str(n["stack"]).lower(), "false"),
        ("Structural", "string[]", lambda n: "[" + ", ".join(quote(k) for k, a in n["arguments"].items()
            if a["mode"] in ("construction", "reference", "placement") or k in ("id", "searchId")) + "]", "[]"),
        ("Construction", "string[]", lambda n: "[" + ", ".join(quote(k) for k, a in n["arguments"].items()
            if a["mode"] == "construction") + "]", "[]"),
    ):
        array = result.startswith("string[]")
        if array:
            lines += [f"    private static readonly string[] {kind}{method} = {values(node)};"
                      for kind, node in nodes.items()]
        lines += [f"    internal static {result} {method}(string kind) => kind switch", "    {"]
        lines += [f"        {quote(kind)} => {kind + method if array else values(node)}," for kind, node in nodes.items()]
        lines += [f"        _ => {fallback}", "    };"]
    for method, record, mode in (("Bindings", "ContractBinding", "mutable"), ("Events", "ContractEvent", "event")):
        cases = []
        for kind, node in nodes.items():
            rows = []
            arguments = node["arguments"]
            names = node["binding_order"] if mode == "mutable" else arguments
            for name in names:
                arg = arguments[name]
                if arg["mode"] != mode:
                    continue
                fields = [quote(name), quote(arg["type"])]
                if mode == "mutable":
                    fields += [quote(arg["setter"]), quote(arg["default"]), str(arg["always"]).lower()]
                else:
                    fields += [quote(arg["subscribe"]), str(arg["identifier"]).lower()]
                rows.append("new(" + ", ".join(fields) + ")")
            lines.append(f"    private static readonly {record}[] {kind}{method} = [{', '.join(rows)}];")
            cases.append(f"        {quote(kind)} => {kind}{method},")
        lines += [f"    internal static {record}[] {method}(string kind) => kind switch", "    {",
                  *cases, "        _ => []", "    };"]
    lines += ["}", ""]
    return "\n".join(lines)
