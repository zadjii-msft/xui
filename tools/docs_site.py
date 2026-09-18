"""Build a Retype site from the explicit GitBook page allowlist."""

import argparse
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import posixpath
import re
import subprocess
import sys
import time
from urllib.parse import quote, unquote, urljoin, urlsplit


ROOT = Path(__file__).resolve().parents[1]
SUMMARY = ROOT / "docs/specs/SUMMARY.md"
INPUT = ROOT / "build/retype-input"
OUTPUT = ROOT / "build/retype-site"
SITE = "https://zadjii-msft.github.io/xui/"
REPOSITORY = "https://github.com/zadjii-msft/xui"
# Public, domain-restricted license published at https://retype.com/community/.
COMMUNITY_KEY = (
    "dEVPBhEAT01MR0NBQ0xBT0VHR0NDR0dGT09PRUFDTEZARE9PT09FRERET15aEx0AHAEWWh0bT0Q-"
    "pId4N7uwS11nfzalpH2MG9Wql5Jtoc9vFnQMJJeh0WfKhv7CHrYPeg"
)
ENTRY = re.compile(r"^( *)\* \[([^\]]+)\]\(([^)]+)\)$")
LINK = re.compile(r"(!?\[[^\]\n]*\]\()([^)\s]+)([^)]*\))")
FENCE = re.compile(r"^\s*(`{3,}|~{3,})")
LANGUAGE_TABS = [".xui", "C#", "Rust", "C++"]
TAB = re.compile(r'^\{% tab title="([^"]+)" %\}$')
BRANDING = {
    Path("assets/branding") / name: ROOT / "assets/branding/generated" / name
    for name in (
        "zoey.svg", "zoey.ico", "zoey-16.png", "zoey-32.png",
        "zoey-180.png", "zoey-192.png", "zoey-256.png", "zoey-512.png",
        "zoey-idle.svg", "zoey-idle.ico", "zoey-active.svg", "zoey-active.ico",
        "zoey-success.svg", "zoey-success.ico", "zoey-warning.svg", "zoey-warning.ico",
        "zoey-error.svg", "zoey-error.ico", "zoey-paused.svg", "zoey-paused.ico",
        "site.webmanifest",
    )
}


def branding_url(name):
    return urlsplit(SITE).path + "assets/branding/" + name


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def source_path(page, href):
    parsed = urlsplit(href)
    target = (page.parent / unquote(parsed.path)).resolve() if parsed.path else page
    if not target.is_relative_to(ROOT) or not target.exists():
        raise ValueError(f"{page.relative_to(ROOT)}: invalid local link: {href}")
    return target


def navigation():
    pages = {}
    folders = {}
    section = Path()
    parent = None
    for line in SUMMARY.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            label = line[3:]
            section = Path(slug(label))
            folders[section] = (label, 1000 - len(pages))
            parent = None
            continue
        match = ENTRY.fullmatch(line)
        if not match:
            continue
        indent, label, href = match.groups()
        source = source_path(SUMMARY, href)
        if source in pages:
            raise ValueError(f"Duplicate navigation page: {href}")
        if not pages:
            destination = Path("index.md")
        elif indent:
            if parent is None:
                raise ValueError(f"Navigation child has no parent: {href}")
            destination = parent / source.name
        elif source.name.lower() == "readme.md" and source.parent.name in (
            "tutorials", "languages", "controls"
        ):
            parent = section / source.parent.name
            folders[parent] = (label, 1000 - len(pages))
            destination = parent / "index.md"
        else:
            parent = None
            name = source.parent.name if source.name.lower() == "readme.md" else source.stem
            destination = section / f"{name.lower()}.md"
        pages[source] = (destination, label, 1000 - len(pages))
    if not pages or next(iter(pages)) != ROOT / "docs/specs/README.md":
        raise ValueError("The first navigation page must be the handbook landing page")
    destinations = [entry[0] for entry in pages.values()]
    if len(set(destinations)) != len(destinations):
        raise ValueError("Navigation produces duplicate site paths")
    pages[SUMMARY] = (Path("contents.md"), "Book contents", -1)
    return pages, folders


def rewrite_links(text, source, destination, pages, revision):
    assets = {source: destination for destination, source in BRANDING.items()}

    def replace(match):
        href = match[2]
        parsed = urlsplit(href)
        if parsed.scheme or parsed.netloc:
            return match[0]
        target = source_path(source, href)
        suffix = ("?" + parsed.query if parsed.query else "") + (
            "#" + parsed.fragment if parsed.fragment else ""
        )
        if not parsed.path:
            replacement = suffix
        elif target in pages:
            replacement = posixpath.relpath(
                pages[target][0].as_posix(), destination.parent.as_posix()
            ) + suffix
        elif target in assets:
            replacement = posixpath.relpath(
                assets[target].as_posix(), destination.parent.as_posix()
            ) + suffix
        else:
            kind = "tree" if target.is_dir() else "blob"
            path = quote(target.relative_to(ROOT).as_posix())
            replacement = f"{REPOSITORY}/{kind}/{revision}/{path}{suffix}"
        return match[1] + replacement + match[3]

    lines = []
    fence = None
    for line in text.splitlines(keepends=True):
        marker = FENCE.match(line)
        if marker:
            if fence is None:
                fence = marker[1]
            elif marker[1][0] == fence[0] and len(marker[1]) >= len(fence):
                fence = None
            lines.append(line)
        else:
            lines.append(LINK.sub(replace, line) if fence is None else line)
    return "".join(lines)


def convert_tabs(text, require_language_tabs=False):
    lines = []
    groups = []
    titles = None
    active = None
    has_content = False
    fence = None
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        stripped = line.strip()
        marker = FENCE.match(line)
        if marker:
            if fence is None:
                language = line[marker.end():].strip().split(" ", 1)[0]
                if require_language_tabs and language == "cpp" and active != "C++":
                    raise ValueError(f"Line {number}: C++ control example needs language tabs")
                fence = marker[1]
            elif marker[1][0] == fence[0] and len(marker[1]) >= len(fence):
                fence = None
            lines.append(line)
            has_content = True
            continue
        if fence is not None:
            lines.append(line)
            continue
        if stripped == "{% tabs %}":
            if titles is not None:
                raise ValueError(f"Line {number}: nested tab groups are not supported")
            titles = []
        elif match := TAB.fullmatch(stripped):
            if titles is None or active is not None:
                raise ValueError(f"Line {number}: tab is outside a group or overlaps another tab")
            active = match[1]
            if active in titles:
                raise ValueError(f"Line {number}: duplicate tab title: {active}")
            titles.append(active)
            has_content = False
            lines.append(f"+++ {active}\n")
        elif stripped == "{% endtab %}":
            if active is None or not has_content:
                raise ValueError(f"Line {number}: unmatched or empty tab")
            active = None
        elif stripped == "{% endtabs %}":
            if titles is None or active is not None or not titles:
                raise ValueError(f"Line {number}: unmatched or incomplete tab group")
            if require_language_tabs and titles != LANGUAGE_TABS:
                raise ValueError(f"Line {number}: expected tab order {LANGUAGE_TABS}, got {titles}")
            groups.append(titles)
            titles = None
            lines.append("+++\n")
        elif stripped.startswith("{%") and ("tab" in stripped):
            raise ValueError(f"Line {number}: unsupported tab directive: {stripped}")
        else:
            if titles is not None and active is None and stripped:
                raise ValueError(f"Line {number}: content between tabs must be outside the group")
            has_content |= bool(stripped)
            lines.append(line)
    if titles is not None:
        raise ValueError("Unclosed tab group")
    if require_language_tabs and not groups:
        raise ValueError("Control guides must contain language tabs")
    return "".join(lines), groups


def prepare(revision):
    pages, folders = navigation()
    files = {}
    for source, (destination, label, order) in pages.items():
        text = source.read_text(encoding="utf-8")
        if text.startswith("---\n"):
            raise ValueError(f"Page frontmatter needs explicit adapter support: {source}")
        metadata = f"---\nlabel: {json.dumps(label)}\norder: {order}\n"
        if source == SUMMARY:
            metadata += "visibility: hidden\n"
        converted, _ = convert_tabs(
            text, source.parent == ROOT / "docs/specs/controls"
        )
        files[destination] = (metadata + "---\n" + rewrite_links(
            converted, source, destination, pages, revision
        )).encode("utf-8")
    for directory, (label, order) in folders.items():
        if directory / "index.md" in files:
            continue
        files[directory / "index.yml"] = (
            f"label: {json.dumps(label)}\norder: {order}\n"
        ).encode("utf-8")
    for destination, source in BRANDING.items():
        files[destination] = source.read_bytes()
    files[Path("_includes/head.html")] = (
        f'<link rel="apple-touch-icon" sizes="180x180" href="{branding_url("zoey-180.png")}">\n'
        f'<link rel="manifest" href="{branding_url("site.webmanifest")}">\n'
    ).encode("utf-8")
    INPUT.mkdir(parents=True, exist_ok=True)
    for existing in INPUT.rglob("*"):
        if existing.is_file() and existing.relative_to(INPUT) not in files:
            existing.unlink()
    for path, content in files.items():
        target = INPUT / path
        if not target.exists() or target.read_bytes() != content:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
    return pages


class HtmlPage(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.images = []
        self.link_elements = []
        self.code_blocks = []
        self.in_pre = False
        self.tab_groups = []
        self.tab_title_pending = False
        self.in_tab_title = False
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        if tag == "pre":
            self.in_pre = True
            self.code_blocks.append("")
        attrs = dict(attrs)
        if tag == "img":
            self.images.append(attrs)
        elif tag == "link":
            self.link_elements.append(attrs)
        if tag == "doc-tabs":
            self.tab_groups.append([])
        elif tag == "doc-tab":
            self.tab_groups[-1].append("")
            self.tab_title_pending = True
        elif tag == "template" and "#title" in attrs and self.tab_title_pending:
            self.in_tab_title = True
            self.tab_title_pending = False
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if tag == "a" and "name" in attrs:
            self.ids.add(attrs["name"])
        for attribute in ("href", "src"):
            if attribute in attrs:
                self.links.append(attrs[attribute])

    def handle_endtag(self, tag):
        if tag == "pre":
            self.in_pre = False
        elif tag == "template":
            self.in_tab_title = False
        elif tag == "doc-tab":
            self.tab_title_pending = False

    def handle_data(self, data):
        if self.in_pre:
            self.code_blocks[-1] += data
        if self.in_tab_title:
            self.tab_groups[-1][-1] += data


def fenced_code(text):
    blocks = []
    fence = None
    for line in text.splitlines():
        marker = FENCE.match(line)
        if marker:
            if fence is None:
                fence = marker[1]
                blocks.append([])
            elif marker[1][0] == fence[0] and len(marker[1]) >= len(fence):
                fence = None
            else:
                blocks[-1].append(line)
        elif fence is not None:
            blocks[-1].append(line)
    return ["\n".join(lines).strip() for lines in blocks]


def html_path(markdown):
    return (
        markdown.with_suffix(".html")
        if markdown.name == "index.md"
        else markdown.with_suffix("") / "index.html"
    )


def check_output(pages, output=OUTPUT):
    expected = {html_path(entry[0]) for entry in pages.values()}
    actual = {path.relative_to(output) for path in output.rglob("*.html")}
    missing = expected - actual
    extra = actual - expected - {Path("404.html")}
    if missing or extra:
        raise ValueError(f"Rendered page coverage differs: missing={missing}, extra={extra}")
    allowed = expected | {entry[0] for entry in pages.values()} | {
        Path("404.html"), Path(".nojekyll"), Path("robots.txt"),
        Path("sitemap.xml"), Path("llms.txt"),
    } | set(BRANDING)
    for path in output.rglob("*"):
        relative = path.relative_to(output)
        resource = relative.parts[0] == "resources" and path.suffix in {
            ".js", ".css", ".woff2", ".txt", ".json",
        }
        if path.is_symlink() or (path.is_file() and relative not in allowed and not resource):
            raise ValueError(f"Unexpected published file: {relative}")
    html = {
        path: HtmlPage((output / path).read_text(encoding="utf-8"))
        for path in actual
    }
    failures = []
    count = 0
    for page, document in html.items():
        for href in document.links:
            parsed = urlsplit(href)
            if parsed.netloc and parsed.netloc != urlsplit(SITE).netloc:
                continue
            if parsed.scheme and parsed.scheme not in ("http", "https"):
                continue
            path = unquote(parsed.path)
            if path.startswith("/"):
                if not path.startswith("/xui/"):
                    failures.append(f"{page}: link misses /xui/ prefix: {href}")
                    continue
                target = output / path[len("/xui/"):]
            elif path:
                target = output / page.parent / path
            else:
                target = output / page
            target = target.resolve()
            if target.is_dir():
                target /= "index.html"
            count += 1
            if not target.is_relative_to(output.resolve()) or not target.is_file():
                failures.append(f"{page}: missing rendered target: {href}")
            elif parsed.fragment and target.suffix == ".html":
                target_page = html[target.relative_to(output.resolve())]
                if unquote(parsed.fragment) not in target_page.ids:
                    failures.append(f"{page}: missing rendered anchor: {href}")
    if failures:
        raise ValueError("\n".join(failures))
    print(f"Retype output checks passed: {len(expected)} pages, {count} links/assets.")


def check_branding(output=OUTPUT):
    for destination, source in BRANDING.items():
        target = output / destination
        if not target.is_file() or target.read_bytes() != source.read_bytes():
            raise ValueError(f"Published branding differs from canonical asset: {destination}")
    for path in output.rglob("*.html"):
        page = HtmlPage(path.read_text(encoding="utf-8"))
        page_url = urljoin(SITE, path.relative_to(output).as_posix())

        def matches(href, name):
            return urljoin(page_url, href or "") == urljoin(SITE, branding_url(name))

        if not any(matches(image.get("src"), "zoey.svg") for image in page.images):
            raise ValueError(f"Missing rendered Zoey logo: {path}")
        for relation, name in (
            ("icon", "zoey.ico"), ("apple-touch-icon", "zoey-180.png"),
            ("manifest", "site.webmanifest"),
        ):
            if not any(
                relation in link.get("rel", "").split()
                and matches(link.get("href"), name)
                for link in page.link_elements
            ):
                raise ValueError(f"Missing rendered {relation}: {path}")
    print("Retype branding checks passed: canonical assets and /xui/ logo/icon links.")


def check_navigation_and_code(pages, output=OUTPUT):
    text = (output / "resources/js/config.js").read_text(encoding="utf-8")
    config = json.loads(text.removeprefix("var __DOCS_CONFIG__ = ").rstrip(";\n"))
    if config["base"] != "/xui/":
        raise ValueError("Retype navigation has the wrong project-site prefix")
    navigation_pages = []

    def walk(nodes, prefix=Path()):
        for node in nodes:
            path = Path() if node["n"] == "/" else prefix / node["n"]
            if node.get("c", True):
                navigation_pages.append(path / "index.html")
            walk(node.get("i", []), path)

    walk(config["sidebar"])
    expected = [html_path(entry[0]) for source, entry in pages.items() if source != SUMMARY]
    if navigation_pages != expected:
        raise ValueError("Rendered sidebar differs from SUMMARY.md page order")
    blocks = 0
    tab_groups = 0
    for source, (destination, _, _) in pages.items():
        rendered = HtmlPage((output / html_path(destination)).read_text(encoding="utf-8"))
        text = source.read_text(encoding="utf-8")
        original = fenced_code(text)
        if original != [block.strip() for block in rendered.code_blocks]:
            raise ValueError(f"Rendered code differs from its source: {source}")
        _, groups = convert_tabs(text)
        if groups != rendered.tab_groups:
            raise ValueError(f"Rendered language tabs differ from their source: {source}")
        blocks += len(original)
        tab_groups += len(groups)
    print(
        f"Retype navigation/code checks passed: {len(expected)} entries, "
        f"{blocks} code blocks, {tab_groups} tab groups."
    )


def retype_command():
    platform, architecture = subprocess.check_output(
        ["node", "-p", "process.platform + ' ' + process.arch"], text=True
    ).strip().split()
    if platform == "win32":
        # Retype has no Windows ARM64 binary. Windows 11 runs its x64 build.
        architecture = "x86" if architecture == "ia32" else "x64"
        platform = "win"
    binary = ROOT / f"node_modules/retypeapp-{platform}-{architecture}/bin/retype"
    if platform == "win":
        binary = binary.with_suffix(".exe")
    if not binary.is_file():
        instruction = "npm ci --cpu=x64" if platform == "win" else "npm ci"
        raise ValueError(f"Retype is missing. Run {instruction} from the repository root.")
    return [str(binary)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "build", "start", "check"))
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    pages = prepare(revision)
    if args.command == "prepare":
        print(f"Prepared {len(pages)} handbook pages in {INPUT}")
        return 0
    if args.command == "check":
        check_output(pages)
        check_navigation_and_code(pages)
        check_branding()
        return 0
    command = retype_command()
    environment = dict(os.environ)
    environment.setdefault("RETYPE_KEY", COMMUNITY_KEY)
    if args.command == "build":
        subprocess.run(command + ["clean"], cwd=ROOT, env=environment, check=True)
        subprocess.run(command + ["build", "--strict"], cwd=ROOT, env=environment, check=True)
        check_output(pages)
        check_navigation_and_code(pages)
        check_branding()
        return 0
    command += ["start", "--host", "127.0.0.1", "--port", str(args.port)]
    if args.no_open:
        command.append("--no-open")
    process = subprocess.Popen(command, cwd=ROOT, env=environment)
    try:
        while process.poll() is None:
            time.sleep(1)
            prepare(revision)
        return process.returncode
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Documentation site failed: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
