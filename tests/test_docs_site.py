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
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            with patch.object(docs_site, "INPUT", stage):
                docs_site.prepare("test-revision")
                page = stage / "index.md"
                first = page.stat().st_mtime_ns
                obsolete = stage / "obsolete.md"
                obsolete.write_text("old page", encoding="utf-8")
                docs_site.prepare("test-revision")
                self.assertEqual(page.stat().st_mtime_ns, first)
                self.assertFalse(obsolete.exists())
                self.assertEqual(len(list(stage.rglob("*.md"))), len(self.pages))
                self.assertIn("visibility: hidden", (stage / "contents.md").read_text())

    def check_fixture(self, content, extra=False):
        with tempfile.TemporaryDirectory() as directory:
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
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "index.html").write_text("<h1>Home</h1>", encoding="utf-8")
            (output / "Program.cs").write_text("private source", encoding="utf-8")
            pages = {self.source: (Path("index.md"), "Home", 1)}
            with self.assertRaisesRegex(ValueError, "Unexpected published file"):
                docs_site.check_output(pages, output)

    def test_code_and_navigation_match_the_original(self):
        with tempfile.TemporaryDirectory() as directory:
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
