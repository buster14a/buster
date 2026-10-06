#!/usr/bin/env python3
"""Offline static-site and literal deployment-contract regressions; no site generator.

Run from any directory. Repository-link checks require a complete source checkout.
These checks do not prove Pages activation, live deployment, or browser accessibility.
"""

import importlib.util
from html.parser import HTMLParser
import pathlib
import tempfile
import unittest
from urllib.parse import unquote, urljoin, urlsplit

ROOT = pathlib.Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
WORKFLOW = ROOT / ".github/workflows/pages.yml"
UPLOAD = "actions/upload-pages-artifact@fc324d3547104276b827a68afc52ff2a11cc49c9"
DEPLOY = "actions/deploy-pages@368f82528645a54fb793d4d04e342629a3f51346"
GUARD = "github.repository == 'buster14a/buster' && github.ref == 'refs/heads/main' && (github.event_name == 'push' || github.event_name == 'workflow_dispatch')"


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.tags = []
        self.doctype = False
        self.feed(text)
        self.close()

    def handle_decl(self, declaration):
        self.doctype = declaration.lower() == "doctype html"

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if len(attrs) != len(attributes):
            raise ValueError("duplicate HTML attribute")
        if tag in {"script", "iframe", "object", "embed", "base", "form", "style"}:
            raise ValueError("site must remain static, without embedded code or forms")
        if any(key.startswith("on") or key in {"style", "srcdoc", "srcset"} for key in attrs):
            raise ValueError("unsupported active or inline asset attribute")
        if "id" in attrs:
            if not attrs["id"] or attrs["id"] in self.ids:
                raise ValueError("empty or duplicate HTML id")
            self.ids.add(attrs["id"])
        self.tags.append((tag, attrs))
        for attribute in ("href", "src"):
            if attribute in attrs:
                self.links.append((tag, attrs[attribute]))


def local_target(reference, prefix):
    parsed = urlsplit(reference)
    path = unquote(parsed.path)
    if parsed.scheme or parsed.netloc or path.startswith("/") or "\\" in path or ".." in pathlib.PurePosixPath(path).parts:
        raise ValueError("local links must be relative and remain in the site")
    resolved = urlsplit(urljoin("https://example.invalid" + prefix + "index.html", reference))
    if not resolved.path.startswith(prefix):
        raise ValueError("link escaped the project prefix")
    relative = unquote(resolved.path[len(prefix):])
    return (relative + "index.html" if relative.endswith("/") or not relative else relative)


def site_files(site):
    if site.is_symlink() or not site.is_dir():
        raise ValueError("site must be a real directory")
    paths = sorted(site.iterdir())
    if {path.name for path in paths} != {"index.html", "styles.css"}:
        raise ValueError("unexpected site contents; review the published-file allowlist")
    for path in paths:
        if path.is_symlink() or not path.is_file() or path.stat().st_nlink != 1:
            raise ValueError("linked or non-regular published file")
        if path.stat().st_size == 0 or path.stat().st_size > 128 * 1024:
            raise ValueError("empty or unexpectedly large published file")
    return paths


class PagesTests(unittest.TestCase):
    def test_static_file_boundary(self):
        self.assertEqual(len(site_files(SITE)), 2)
        css = (SITE / "styles.css").read_text(encoding="utf-8")
        self.assertNotRegex(css.lower(), r"@import|url\s*\(|expression\s*\(")
        self.assertIn("prefers-color-scheme", css)
        self.assertIn(":focus-visible", css)

    def test_semantic_entry_point(self):
        page = Page((SITE / "index.html").read_text(encoding="utf-8"))
        self.assertTrue(page.doctype)
        self.assertEqual(sum(tag == "h1" for tag, _ in page.tags), 1)
        self.assertEqual(sum(tag == "main" for tag, _ in page.tags), 1)
        self.assertIn(("a", "#main"), page.links)
        self.assertTrue(any(tag == "html" and attrs.get("lang") == "en" for tag, attrs in page.tags))
        self.assertTrue(any(tag == "meta" and attrs.get("name") == "viewport" for tag, attrs in page.tags))
        for tag, attrs in page.tags:
            if tag == "nav":
                self.assertIn("aria-label", attrs)

    def test_links_at_root_and_project_prefix(self):
        page = Page((SITE / "index.html").read_text(encoding="utf-8"))
        for prefix in ("/", "/buster/", "/renamed-project/"):
            for tag, reference in page.links:
                with self.subTest(prefix=prefix, reference=reference):
                    parsed = urlsplit(reference)
                    if not parsed.scheme:
                        target = SITE / local_target(reference, prefix)
                        self.assertTrue(target.is_file(), reference)
                        if parsed.fragment:
                            self.assertIn(unquote(parsed.fragment), Page(target.read_text(encoding="utf-8")).ids)
                    else:
                        self.assertEqual(tag, "a", "external assets are not allowed")
                        self.assertEqual(parsed.scheme, "https")
                        self.assertEqual(parsed.netloc, "github.com")
                        self.assertTrue(parsed.path.startswith("/buster14a/buster"))

    def test_repository_document_links_exist(self):
        page = Page((SITE / "index.html").read_text(encoding="utf-8"))
        prefix = "/buster14a/buster/blob/main/"
        for _, reference in page.links:
            path = urlsplit(reference).path
            if path.startswith(prefix):
                with self.subTest(reference=reference):
                    self.assertTrue((ROOT / path[len(prefix):]).is_file(), reference)

    def test_reject_root_absolute_and_escaping_links(self):
        for reference in ("/styles.css", "../secret", "%2e%2e/secret", "//elsewhere.invalid/x", "a\\b", "https://elsewhere.invalid/x"):
            with self.subTest(reference=reference), self.assertRaises(ValueError):
                local_target(reference, "/buster/")

    def test_reject_active_html_and_duplicate_ids(self):
        for text in ('<script src="x">', '<a onclick="x">', '<base href="/">', '<p id="x"><p id="x">', '<p id="x" id="y">'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                Page(text)

    def test_reject_unexpected_and_linked_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            site = root / "site"
            site.mkdir()
            (site / "index.html").write_text("ok", encoding="utf-8")
            (site / "styles.css").write_text("ok", encoding="utf-8")
            self.assertEqual(len(site_files(site)), 2)
            (site / "secret.txt").write_text("not for publication", encoding="utf-8")
            with self.assertRaises(ValueError):
                site_files(site)
            (site / "secret.txt").unlink()
            outside = root / "outside"
            outside.write_text("not for publication", encoding="utf-8")
            (site / "index.html").unlink()
            (site / "index.html").symlink_to(outside)
            with self.assertRaises(ValueError):
                site_files(site)
            (site / "index.html").unlink()
            (site / "index.html").hardlink_to(outside)
            with self.assertRaises(ValueError):
                site_files(site)

    def test_literal_workflow_privilege_boundary(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        build, deploy = text.split("\n  deploy:\n")
        self.assertIn("\npermissions:\n  contents: read\n", build)
        self.assertNotIn("permissions:", build.split("\njobs:\n")[1])
        self.assertNotIn("write", build)
        self.assertIn("persist-credentials: false", build)
        self.assertIn("run: python3 tools/pages_test.py -v", build)
        self.assertIn("uses: " + UPLOAD, build)
        self.assertIn("path: site\n          retention-days: 1", build)
        self.assertNotIn("pull_request_target", text)
        self.assertNotIn("secrets.", text)
        self.assertIn("branches: [main]", text)
        self.assertIn("\n  merge_group:\n", text)
        self.assertIn("\n  workflow_dispatch:\n", text)
        self.assertIn("group: pages-${{ github.event_name == 'pull_request' && github.event.pull_request.number || github.ref }}", text)
        self.assertIn("cancel-in-progress: false", text)
        # An exact literal contract, not a YAML parser or a live authorization proof.
        expected = f"""    name: Deploy GitHub Pages
    if: {GUARD}
    needs: build
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    permissions:
      pages: write
      id-token: write
    environment:
      name: github-pages
      url: ${{{{ steps.deployment.outputs.page_url }}}}
    steps:
      - name: Machine specifications
        uses: buster14a/buster/.github/actions/machine-specifications@6f2ab3357f1e0f359fbf9f40621906171c026283
        with:
          requested-runner: >-
            ubuntu-24.04
      - name: Deploy validated artifact
        id: deployment
        uses: {DEPLOY} # v5.0.1
"""
        self.assertEqual(deploy, expected)

    def test_pages_action_pins_remain_restricted(self):
        spec = importlib.util.spec_from_file_location("action_pins", ROOT / "tools/check_action_pins.py")
        pins = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pins)
        self.assertEqual(pins.check_text(WORKFLOW.read_text(encoding="utf-8"), WORKFLOW), [])
        for pin in (UPLOAD, DEPLOY):
            self.assertEqual(pins.check_text("uses: " + pin, "fixture.yml"), [])
            for invalid in (pin.split("@")[0] + "@main", pin.split("@")[0] + "@" + "0" * 40):
                self.assertTrue(pins.check_text("uses: " + invalid, "fixture.yml"))


if __name__ == "__main__":
    unittest.main()
