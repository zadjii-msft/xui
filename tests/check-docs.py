"""Check the GitBook navigation, local links, and public control coverage."""

from collections import Counter
import json
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\[[^\]\n]+\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^(`{3,}|~{3,})")


def prose(text):
    lines = []
    fence = None
    for line in text.splitlines():
        match = FENCE.match(line)
        if match:
            marker = match[1]
            if fence is None:
                fence = marker
            elif marker[0] == fence[0] and len(marker) >= len(fence):
                fence = None
            lines.append("")
        else:
            lines.append(line if fence is None else "")
    return "\n".join(lines)


def anchors(text):
    text = prose(text)
    result = set(re.findall(r'<a\s+(?:id|name)=["\']([^"\']+)', text))
    counts = Counter()
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", text, re.MULTILINE):
        heading = re.sub(r"<[^>]+>", "", heading)
        heading = LINK.sub(lambda match: match[0].split("]")[0][1:], heading)
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        count = counts[slug]
        counts[slug] += 1
        result.add(slug if count == 0 else f"{slug}-{count}")
    return result


def local_link(page, href):
    parsed = urlsplit(href)
    if parsed.scheme or parsed.netloc:
        return None
    path = (page.parent / unquote(parsed.path)).resolve() if parsed.path else page
    return path, unquote(parsed.fragment)


def main():
    errors = []
    config = (ROOT / ".gitbook.yaml").read_text(encoding="utf-8")
    # This book deliberately uses only these three Git Sync settings.
    expected = {
        "root": "./",
        "readme": "docs/specs/README.md",
        "summary": "docs/specs/SUMMARY.md",
    }
    for key, value in expected.items():
        match = re.search(rf"^\s*{key}:\s*(\S+)\s*$", config, re.MULTILINE)
        if match is None or match[1] != value:
            errors.append(f".gitbook.yaml: expected {key}: {value}")

    summary = ROOT / expected["summary"]
    if not summary.is_file():
        errors.append("Missing GitBook summary")
        pages = []
    else:
        entries = []
        for href in LINK.findall(prose(summary.read_text(encoding="utf-8"))):
            target = local_link(summary, href)
            if target is None:
                errors.append(f"SUMMARY.md: navigation must use local pages: {href}")
                continue
            path, fragment = target
            if fragment or path.suffix != ".md":
                errors.append(f"SUMMARY.md: expected a whole Markdown page: {href}")
            entries.append(path)
        for path, count in Counter(entries).items():
            if count != 1:
                errors.append(f"SUMMARY.md: duplicate page {path.relative_to(ROOT)}")
        if not entries or entries[0] != ROOT / expected["readme"]:
            errors.append("SUMMARY.md: first page must match structure.readme")
        pages = sorted(set(entries + [summary, ROOT / "README.md"]))
        for directory in ("controls", "languages", "tutorials"):
            for page in (ROOT / "docs" / "specs" / directory).rglob("*.md"):
                if "bin" in page.parts or "obj" in page.parts:
                    continue
                if page not in entries:
                    errors.append(f"SUMMARY.md: missing page {page.relative_to(ROOT)}")

    checked_links = 0
    for page in pages:
        if not page.is_file():
            errors.append(f"Missing page: {page.relative_to(ROOT)}")
            continue
        for href in LINK.findall(prose(page.read_text(encoding="utf-8"))):
            target = local_link(page, href)
            if target is None:
                continue
            checked_links += 1
            path, fragment = target
            if not path.is_relative_to(ROOT):
                errors.append(f"{page.relative_to(ROOT)}: link leaves repository: {href}")
            elif not path.exists():
                errors.append(f"{page.relative_to(ROOT)}: missing link target: {href}")
            elif fragment and path.is_file() and path.suffix == ".md":
                if fragment not in anchors(path.read_text(encoding="utf-8")):
                    errors.append(f"{page.relative_to(ROOT)}: missing anchor: {href}")

    catalog_page = ROOT / "docs" / "specs" / "controls" / "README.md"
    if not catalog_page.is_file():
        errors.append("Missing control catalog")
    else:
        catalog = catalog_page.read_text(encoding="utf-8")
        source = (ROOT / "bindings" / "dotnet" / "Xui" / "ControlStyleCatalog.g.cs").read_text(encoding="utf-8")
        enum = re.search(r"public enum StyleTarget : uint \{(.*?)\}", source, re.DOTALL)[1]
        names = {int(value): name for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", enum)}
        schemas = json.loads((ROOT / "bindings" / "control_style_catalog.json").read_text(encoding="utf-8"))["schemas"]
        required = {names[schema["target"]] for schema in schemas}
        # These facades have no independent style schema but are public authoring surfaces.
        required.update(("ContentDialog", "CommandSurface", "LocationPicker", "ViewPicker", "CustomShellMenu", "Window"))
        for name in sorted(required):
            if re.search(rf"\b{re.escape(name)}\b", catalog) is None:
                errors.append(f"Control catalog does not mention {name}")
        # Also cover concrete public Elements that might not have a style adapter yet.
        excluded = {"Control", "VirtualCollection", "DocumentText", "RuntimeHost", "NativeEditBridge"}
        for header in (ROOT / "include" / "xui").glob("*.hpp"):
            declarations = re.findall(
                r"class\s+(\w+)\s*(?:final\s*)?:\s*public\s+"
                r"(?:Element|Control|Stack|VirtualCollection|DocumentText|RuntimeHost|VectorCanvas)\s*\{",
                header.read_text(encoding="utf-8"),
            )
            for name in declarations:
                if name not in excluded and re.search(rf"\b{re.escape(name)}\b", catalog) is None:
                    errors.append(f"Control catalog does not mention public {name} from {header.name}")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Documentation checks passed: {len(pages)} pages, {checked_links} local links, control catalog coverage.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
