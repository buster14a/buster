#!/usr/bin/env python3
"""Fail when a tracked Markdown file links a repository path that does not exist.

Deleting a file used to leave every relative link to it dead while CI stayed
green: the only documentation step, `tools/new_audit.py --check`, compares the
audit index with its directory and resolves no links (#1638).

This check reads every tracked `*.md` file and resolves, against the set of
tracked paths, the destination of every inline link or image `[text](target)`
and every reference definition `[label]: target`.  It reports each target
whose path is not a tracked file or a directory holding one, as
`file:line: target`.  Resolving against `git ls-files` rather than the disk
keeps the result identical on a clean checkout, a dirty tree, and a
case-insensitive filesystem.

Out of scope, by design:
- Targets with a URL scheme (`https:`, `mailto:`): no network access.
- `#anchor` fragments, including the fragment after a path: heading slug rules
  differ between renderers, so only the path part is resolved.
- Text inside fenced code blocks and inline code spans, which is not a link.
- `<path>` angle brackets: CommonMark autolinks require a scheme, so a bare
  path in brackets is not a link.
- Indented code blocks: Markdown nests them inside list items, where telling
  them from continuation text needs a full parser.  No current file needs it.

Immutable audit records (#1637) get no exemption: a deletion that breaks a
link in one fails here like any other, so the change that deletes the target
also decides how the record handles it.

Entry points: `markdown_links` extracts destinations, `missing_targets` checks
one tree of texts, `check` reads the checkout.

Usage:
    tools/check_markdown_links.py
    tools/check_markdown_links.py --self-test
"""

import argparse
import os
import posixpath
import re
import subprocess
import sys
import urllib.parse

FENCE = re.compile(r"^\s*(`{3,}|~{3,})")
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
REFERENCE_DEFINITION = re.compile(r"^ {0,3}\[(?:[^\]\\]|\\.)+\]:[ \t]*(<[^>]*>|\S+)")
CODE_SPAN = re.compile(r"(`+)(?:.*?[^`])?\1(?!`)")


def strip_code_spans(line):
    """`line` with each inline code span blanked, so a link written as code is
    not read as a link."""
    return CODE_SPAN.sub(" ", line)


def inline_destinations(line):
    """Destinations of the `[text](target)` links and images in `line`.  The
    destination ends at whitespace or at the parenthesis that balances its
    opening one; `<...>` destinations may hold spaces and parentheses."""
    destinations = []
    start = line.find("](")
    while start >= 0:
        position = start + 2
        while position < len(line) and line[position] in " \t":
            position += 1
        if position < len(line) and line[position] == "<":
            close = line.find(">", position + 1)
            if close >= 0:
                destinations.append(line[position + 1:close])
            start = line.find("](", max(close, position) + 1)
        else:
            depth = 0
            end = position
            scanning = True
            while scanning and end < len(line):
                character = line[end]
                if character == "\\" and end + 1 < len(line):
                    end += 2
                elif character == "(":
                    depth += 1
                    end += 1
                elif character == ")" and depth > 0:
                    depth -= 1
                    end += 1
                elif character == ")" or character in " \t":
                    scanning = False
                else:
                    end += 1
            if end > position:
                destinations.append(line[position:end])
            start = line.find("](", end)
    return destinations


def markdown_links(text):
    """(line number, destination) for every inline link, image and reference
    definition outside fenced code blocks and code spans."""
    links = []
    fence = None
    for number, line in enumerate(text.split("\n"), 1):
        opening = FENCE.match(line)
        if fence is not None:
            if opening and opening.group(1)[0] == fence[0] and len(opening.group(1)) >= len(fence) \
                    and not line.strip()[len(opening.group(1)):].strip():
                fence = None
        elif opening:
            fence = opening.group(1)
        else:
            definition = REFERENCE_DEFINITION.match(line)
            if definition:
                destination = definition.group(1)
                if destination.startswith("<") and destination.endswith(">"):
                    destination = destination[1:-1]
                links.append((number, destination))
            for destination in inline_destinations(strip_code_spans(line)):
                links.append((number, destination))
    return links


def link_path(destination):
    """The repository path part of `destination`, or None when it names no
    path to check: a URL with a scheme, a pure `#anchor`, or nothing."""
    path = None
    if destination and not SCHEME.match(destination) and not destination.startswith("#"):
        path = re.split(r"[#?]", destination, maxsplit=1)[0]
        path = urllib.parse.unquote(re.sub(r"\\(.)", r"\1", path))
    return path or None


def missing_targets(texts, tracked):
    """(file, line, destination) for every link in `texts` whose path is not
    in the tracked tree.  `texts` maps a repository path to its Markdown and
    `tracked` lists every tracked file, both with `/` separators."""
    present = set()
    for path in tracked:
        present.add(path)
        parent = posixpath.dirname(path)
        while parent and parent not in present:
            present.add(parent)
            parent = posixpath.dirname(parent)
    findings = []
    for name in sorted(texts):
        for number, destination in markdown_links(texts[name]):
            path = link_path(destination)
            if path is not None:
                if path.startswith("/"):
                    resolved = posixpath.normpath(path.lstrip("/") or ".")
                else:
                    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), path))
                if resolved != "." and resolved not in present:
                    findings.append((name, number, destination))
    return findings


def repository_root():
    completed = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=os.path.dirname(os.path.abspath(__file__)),
                               stdout=subprocess.PIPE, check=True, text=True)
    return completed.stdout.strip()


def check(root):
    completed = subprocess.run(["git", "ls-files", "-z"], cwd=root, stdout=subprocess.PIPE, check=True)
    tracked = [path for path in completed.stdout.decode("utf-8").split("\0") if path]
    texts = {}
    for path in tracked:
        if path.endswith(".md"):
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as handle:
                texts[path] = handle.read()
    return len(texts), missing_targets(texts, tracked)


def self_test():
    tracked = ["README.md", "docs/guide.md", "docs/sub/page.md", "tools/tool.py", "a b/c.md"]
    texts = {
        "docs/guide.md": "\n".join([
            "[present](../tools/tool.py) [missing file](../tools/gone.py)",
            "[anchored](sub/page.md#some-heading) [anchor only](#heading)",
            "[missing anchored](sub/gone.md#heading)",
            "```sh",
            "[fenced](not/there.md)",
            "```",
            "[web](https://example.com/x) [mail](mailto:someone@example.com)",
            "`[code span](not/there.md)` ![image](missing.png)",
            "[directory](sub) [root](/README.md) [root missing](/nope.md)",
            "[spaced](<../a b/c.md>) [escaped](../a%20b/c.md) [titled](sub/page.md \"Title\")",
            "~~~~",
            "[tilde fenced](not/there.md)",
            "```",
            "still fenced: [x](not/there.md)",
            "~~~~",
            "[reference]: ../tools/gone-too.py",
            "[parenthesized](sub/page.md (title)) [outside](../../outside.md)",
        ]),
    }
    found = [(line, destination) for _, line, destination in missing_targets(texts, tracked)]
    expected = [
        (1, "../tools/gone.py"),
        (3, "sub/gone.md#heading"),
        (8, "missing.png"),
        (9, "/nope.md"),
        (16, "../tools/gone-too.py"),
        (17, "../../outside.md"),
    ]
    assert sorted(found) == expected, found
    assert link_path("#heading") is None and link_path("https://x/y") is None
    assert link_path("sub/page.md#heading") == "sub/page.md"
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()
    files, findings = check(repository_root())
    for name, line, destination in findings:
        print(f"{name}:{line}: unresolved link target {destination}")
    print(f"{len(findings)} unresolved link targets in {files} Markdown files")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
