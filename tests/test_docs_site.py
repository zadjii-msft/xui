"""Regression checks for the selected-page Retype adapter."""

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import docs_site


class DocumentationSiteTests(unittest.TestCase):
    def setUp(self):
        self.fixture_root = docs_site.ROOT / "build"
        self.fixture_root.mkdir(exist_ok=True)
        self.pages, self.folders = docs_site.navigation()
        self.source = docs_site.ROOT / "docs/specs/README.md"
        self.destination = self.pages[self.source][0]

    def rewrite(self, text):
        return docs_site.rewrite_links(
            text, self.source, self.destination, self.pages, "test-revision"
        )

    def test_navigation_selects_only_public_pages(self):
        self.assertEqual(self.destination, Path("index.md"))
        self.assertIn(docs_site.SUMMARY, self.pages)
        self.assertIn(docs_site.ROOT / "CONTRIBUTING.md", self.pages)
        self.assertIn(
            docs_site.ROOT / "bindings/dotnet/Minesweeper/README.md", self.pages
        )
        self.assertFalse(any("llm" in source.parts for source in self.pages))
        self.assertEqual(len(self.pages), len({entry[0] for entry in self.pages.values()}))
        tutorial = docs_site.ROOT / "docs/specs/tutorials/01-first-window.md"
        self.assertEqual(
            self.pages[tutorial][0], Path("learn/tutorials/01-first-window.md")
        )

    def test_selected_links_remain_local(self):
        self.assertEqual(
            self.rewrite("[Start](tutorials/README.md#learn)"),
            "[Start](learn/tutorials/index.md#learn)",
        )
        self.assertEqual(self.rewrite("[Here](#learn)"), "[Here](#learn)")
        self.assertIn(
            "contribute-and-explore-designs/contributing.md#gitbook-documentation",
            self.rewrite("[Build](../../CONTRIBUTING.md#gitbook-documentation)"),
        )

    def test_other_sources_link_to_exact_github_revision(self):
        result = self.rewrite("[Source](../../include/xui/application.hpp)")
        self.assertIn("/blob/test-revision/include/xui/application.hpp", result)
        result = self.rewrite("[Samples](../../bindings/dotnet)")
        self.assertIn("/tree/test-revision/bindings/dotnet", result)
        result = self.rewrite("[History](../llm/README.md)")
        self.assertIn("/blob/test-revision/docs/llm/README.md", result)

    def test_selected_branding_links_remain_local(self):
        source = docs_site.ROOT / "docs/specs/branding/zoey.md"
        destination = self.pages[source][0]
        for asset, original in docs_site.BRANDING.items():
            with self.subTest(asset=asset):
                href = "../../../assets/branding/generated/" + original.name
                result = docs_site.rewrite_links(
                    f"[Asset]({href}?download=1#preview)",
                    source, destination, self.pages, "test-revision",
                )
                self.assertEqual(
                    result, f"[Asset](../{asset.as_posix()}?download=1#preview)"
                )
        for href in (
            "../../../assets/branding/zoey.svg",
            "../../../assets/branding/generated/zoey-active-256.png",
            "../../../assets/branding/generated/index.html",
        ):
            with self.subTest(href=href):
                result = docs_site.rewrite_links(
                    f"[Other]({href})", source, destination, self.pages, "test-revision"
                )
                self.assertIn("/blob/test-revision/assets/branding/", result)

    def test_external_links_and_fenced_examples_stay_unchanged(self):
        text = (
            "[External](https://example.com/page#anchor)\n"
            "```md\n[Example](does-not-exist.md)\n```\n"
            "~~~md\n[Example](does-not-exist.md)\n~~~\n"
        )
        self.assertEqual(self.rewrite(text), text)

    def test_missing_links_fail(self):
        with self.assertRaisesRegex(ValueError, "invalid local link"):
            self.rewrite("[Missing](missing.md)")

    def test_prepare_is_stable_and_removes_obsolete_staged_pages(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            stage = Path(directory)
            with patch.object(docs_site, "INPUT", stage):
                docs_site.prepare("test-revision")
                page = stage / "index.md"
                first = page.stat().st_mtime_ns
                assets = {
                    path: ((stage / path).read_bytes(), (stage / path).stat().st_mtime_ns)
                    for path in docs_site.BRANDING
                }
                for path, source in docs_site.BRANDING.items():
                    self.assertEqual(assets[path][0], source.read_bytes())
                obsolete = stage / "obsolete.md"
                obsolete.write_text("old page", encoding="utf-8")
                private_asset = stage / "assets/branding/study.svg"
                private_asset.write_text("<svg/>", encoding="utf-8")
                docs_site.prepare("test-revision")
                self.assertEqual(page.stat().st_mtime_ns, first)
                self.assertFalse(obsolete.exists())
                self.assertFalse(private_asset.exists())
                for path, (content, modified) in assets.items():
                    self.assertEqual((stage / path).read_bytes(), content)
                    self.assertEqual((stage / path).stat().st_mtime_ns, modified)
                self.assertEqual(len(list(stage.rglob("*.md"))), len(self.pages))
                self.assertIn("visibility: hidden", (stage / "contents.md").read_text())
                self.assertIn(
                    'href="/xui/assets/branding/site.webmanifest"',
                    (stage / "_includes/head.html").read_text(),
                )

    def test_branding_allowlist_is_exact(self):
        self.assertEqual(
            {path.name for path in docs_site.BRANDING},
            {
                "zoey.svg", "zoey.ico", "zoey-16.png", "zoey-32.png",
                "zoey-180.png", "zoey-192.png", "zoey-256.png",
                "zoey-512.png", "site.webmanifest",
                "zoey-idle.svg", "zoey-idle.ico", "zoey-active.svg", "zoey-active.ico",
                "zoey-success.svg", "zoey-success.ico", "zoey-warning.svg", "zoey-warning.ico",
                "zoey-error.svg", "zoey-error.ico", "zoey-paused.svg", "zoey-paused.ico",
            },
        )
        for destination, source in docs_site.BRANDING.items():
            self.assertEqual(destination.parent, Path("assets/branding"))
            self.assertEqual(source.parent, docs_site.ROOT / "assets/branding/generated")

    def test_prepare_preserves_binary_bytes_and_updates_changed_assets(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            root = Path(directory)
            source = root / "fixture.ico"
            destination = Path("assets/branding/fixture.ico")
            source.write_bytes(b"\x00\xff\r\n\x80")
            with (
                patch.object(docs_site, "INPUT", root / "stage"),
                patch.object(docs_site, "BRANDING", {destination: source}),
            ):
                docs_site.prepare("test-revision")
                self.assertEqual((docs_site.INPUT / destination).read_bytes(), source.read_bytes())
                source.write_bytes(b"\x00\xfe\r\n\x81")
                docs_site.prepare("test-revision")
                self.assertEqual((docs_site.INPUT / destination).read_bytes(), source.read_bytes())

    def check_fixture(self, content, extra=False):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            output = Path(directory)
            (output / "index.html").write_text(content, encoding="utf-8")
            if extra:
                (output / "private.html").write_text("private", encoding="utf-8")
            pages = {self.source: (Path("index.md"), "Home", 1)}
            with contextlib.redirect_stdout(io.StringIO()):
                docs_site.check_output(pages, output)

    def test_output_accepts_base_path_and_existing_anchor(self):
        self.check_fixture('<h1 id="home">Home</h1><a href="/xui/#home">Home</a>')

    def test_output_rejects_wrong_base_path(self):
        with self.assertRaisesRegex(ValueError, "misses /xui/ prefix"):
            self.check_fixture('<a href="/index.html">Home</a>')

    def test_output_rejects_missing_anchor_or_page(self):
        with self.assertRaisesRegex(ValueError, "missing rendered anchor"):
            self.check_fixture('<a href="#absent">Missing</a>')
        with self.assertRaisesRegex(ValueError, "missing rendered target"):
            self.check_fixture('<a href="/xui/absent/">Missing</a>')

    def test_output_rejects_unlisted_pages(self):
        with self.assertRaisesRegex(ValueError, "Rendered page coverage differs"):
            self.check_fixture("<h1>Home</h1>", extra=True)

    def test_output_rejects_application_source(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            output = Path(directory)
            (output / "index.html").write_text("<h1>Home</h1>", encoding="utf-8")
            (output / "Program.cs").write_text("private source", encoding="utf-8")
            pages = {self.source: (Path("index.md"), "Home", 1)}
            with self.assertRaisesRegex(ValueError, "Unexpected published file"):
                docs_site.check_output(pages, output)

    def test_output_rejects_unlisted_branding_assets(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            output = Path(directory)
            (output / "index.html").write_text("<h1>Home</h1>", encoding="utf-8")
            extra = output / "assets/branding/study.svg"
            extra.parent.mkdir(parents=True)
            extra.write_text("<svg/>", encoding="utf-8")
            pages = {self.source: (Path("index.md"), "Home", 1)}
            with self.assertRaisesRegex(ValueError, "Unexpected published file"):
                docs_site.check_output(pages, output)

    def test_published_branding_bytes_and_base_path_links(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            output = Path(directory)
            for destination, source in docs_site.BRANDING.items():
                target = output / destination
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())
            html = (
                '<img src="/xui/assets/branding/zoey.svg" alt="XUI">'
                '<link rel="icon" href="/xui/assets/branding/zoey.ico">'
                '<link rel="apple-touch-icon" href="/xui/assets/branding/zoey-180.png">'
                '<link rel="manifest" href="/xui/assets/branding/site.webmanifest">'
            )
            page = output / "index.html"
            page.write_text(html, encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                docs_site.check_branding(output)
                docs_site.check_output({self.source: (Path("index.md"), "Home", 1)}, output)
            page.write_text(html.replace("/xui/assets/", "assets/"), encoding="utf-8")
            nested = output / "learn/examples/index.html"
            nested.parent.mkdir(parents=True)
            nested.write_text(html.replace("/xui/assets/", "../../assets/"), encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                docs_site.check_branding(output)
            for name in ("zoey.svg", "zoey.ico", "zoey-180.png", "site.webmanifest"):
                with self.subTest(name=name):
                    page.write_text(
                        html.replace("/xui/assets/branding/" + name, "/assets/branding/" + name),
                        encoding="utf-8",
                    )
                    with self.assertRaisesRegex(ValueError, "Missing rendered"):
                        docs_site.check_branding(output)
            page.write_text(html, encoding="utf-8")
            (output / "assets/branding/zoey.ico").write_bytes(b"not the canonical icon")
            with self.assertRaisesRegex(ValueError, "differs from canonical asset"):
                docs_site.check_branding(output)

    def test_code_and_navigation_match_the_original(self):
        with tempfile.TemporaryDirectory(dir=self.fixture_root) as directory:
            output = Path(directory)
            source = output / "source.md"
            source.write_text('```cpp\nitems({{1, "First"}});\n```\n', encoding="utf-8")
            config = output / "resources/js/config.js"
            config.parent.mkdir(parents=True)
            config.write_text(
                "var __DOCS_CONFIG__ = " + json.dumps({
                    "base": "/xui/", "sidebar": [{"n": "/", "l": "Home"}],
                }) + ";\n", encoding="utf-8",
            )
            page = output / "index.html"
            page.write_text(
                '<pre><code>items({{1, &quot;First&quot;}});</code></pre>', encoding="utf-8"
            )
            pages = {source: (Path("index.md"), "Home", 1)}
            with contextlib.redirect_stdout(io.StringIO()):
                docs_site.check_navigation_and_code(pages, output)
            page.write_text("<pre><code>items();</code></pre>", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Rendered code differs"):
                docs_site.check_navigation_and_code(pages, output)
            config.write_text(
                'var __DOCS_CONFIG__ = {"base":"/xui/","sidebar":[]};\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "Rendered sidebar differs"):
                docs_site.check_navigation_and_code(pages, output)

    def test_platform_binary_selection(self):
        cases = {
            "win32 arm64": "retypeapp-win-x64/bin/retype.exe",
            "win32 x64": "retypeapp-win-x64/bin/retype.exe",
            "linux x64": "retypeapp-linux-x64/bin/retype",
            "linux arm64": "retypeapp-linux-arm64/bin/retype",
            "darwin arm64": "retypeapp-darwin-arm64/bin/retype",
        }
        for platform, suffix in cases.items():
            with self.subTest(platform=platform):
                with patch.object(docs_site.subprocess, "check_output", return_value=platform):
                    with patch.object(Path, "is_file", return_value=True):
                        self.assertEqual(
                            docs_site.retype_command(),
                            [str(docs_site.ROOT / "node_modules" / suffix)],
                        )

    def tab_fixture(self, titles=None):
        titles = docs_site.LANGUAGE_TABS if titles is None else titles
        return "{% tabs %}\n" + "".join(
            '{% tab title="' + title + '" %}\nExample text.\n{% endtab %}\n'
            for title in titles
        ) + "{% endtabs %}\n"

    def test_gitbook_tabs_convert_in_requested_order(self):
        converted, groups = docs_site.convert_tabs(self.tab_fixture(), True)
        self.assertEqual(groups, [docs_site.LANGUAGE_TABS])
        self.assertEqual(
            converted,
            "+++ .xui\nExample text.\n+++ C#\nExample text.\n"
            "+++ Rust\nExample text.\n+++ C++\nExample text.\n+++\n",
        )

    def test_tab_directives_in_code_remain_literal(self):
        text = "````markdown\n" + self.tab_fixture() + "````\n"
        self.assertEqual(docs_site.convert_tabs(text), (text, []))

    def test_tab_validation_rejects_wrong_order_and_missing_languages(self):
        for titles in (["C++", ".xui", "C#", "Rust"], [".xui", "C#"]):
            with self.subTest(titles=titles):
                with self.assertRaisesRegex(ValueError, "expected tab order"):
                    docs_site.convert_tabs(self.tab_fixture(titles), True)

    def test_tab_validation_rejects_malformed_groups(self):
        examples = [
            "{% tabs %}\n{% tabs %}\n",
            '{% tab title=".xui" %}\n',
            "{% endtab %}\n",
            "{% endtabs %}\n",
            '{% tabs %}\n{% tab title=".xui" %}\n{% endtab %}\n',
            "{% tabs %}\n",
            self.tab_fixture([".xui", ".xui"]),
        ]
        for text in examples:
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    docs_site.convert_tabs(text)

    def test_control_cpp_examples_cannot_escape_tabs(self):
        with self.assertRaisesRegex(ValueError, "C\\+\\+ control example needs language tabs"):
            docs_site.convert_tabs("```cpp\nint value = 1;\n```\n", True)
        with self.assertRaisesRegex(ValueError, "must contain language tabs"):
            docs_site.convert_tabs("Only prose.", True)

    def test_rendered_tab_titles_preserve_language_order(self):
        html = "<doc-tabs>" + "".join(
            f'<doc-tab><template #title>{title}</template>'
            '<pre><code>literal {{ code }}</code></pre></doc-tab>'
            for title in docs_site.LANGUAGE_TABS
        ) + "</doc-tabs>"
        page = docs_site.HtmlPage(html)
        self.assertEqual(page.tab_groups, [docs_site.LANGUAGE_TABS])
        self.assertEqual(page.code_blocks, ["literal {{ code }}"] * 4)


if __name__ == "__main__":
    unittest.main()
