#!/usr/bin/env python3
"""Mint a performance audit: its id, its file, and its index line.

An audit id used to be a date plus a sequence letter, `2026-08-08k`.  That id
is chosen by counting the day's existing entries, which is a value two
concurrent sessions compute identically — three of the four audit PRs open when
the history was split into one file per audit had independently claimed
`2026-08-22a`.  The id is now the UTC timestamp at which the audit is recorded,
`2026-08-22T140351Z`: ISO 8601 with the colons dropped, because Windows forbids
them in filenames.  Two sessions cannot mint the same one without writing their
entries in the same second.

Minting here rather than by hand is the point of the script: it stamps the id,
writes `docs/performance-audits/<id>.md`, and inserts the matching line at the
top of the index in `PERFORMANCE_AUDITS.md`, so the file and its index entry
cannot disagree.

Usage:
    tools/new_audit.py --platform "Linux x86_64, Zen 4 7940HS" "the headline"
    tools/new_audit.py --check
    tools/new_audit.py --self-test
"""

import argparse
import datetime
import os
import re
import sys
import textwrap

INDEX_NAME = "PERFORMANCE_AUDITS.md"
AUDIT_DIRECTORY = os.path.join("docs", "performance-audits")
INDEX_HEADING = "## Audits, newest first"
ID_FORMAT = "%Y-%m-%dT%H%M%SZ"
WRAP_COLUMNS = 78

TIMESTAMP_ID = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{6}Z$")
# Entries recorded before 2026-08-22.  Kept as written: the corpus
# cross-references them by name.
LEGACY_ID = re.compile(r"^\d{4}-\d{2}-\d{2}[a-z]*$")
INDEX_LINE = re.compile(r"^- \[`([^`]+)`\]\(docs/performance-audits/([^)]+)\.md\) — ")
ENTRY_HEAD = re.compile(r"^`([^`]+)` \(")


def repository_root(start):
    """The directory holding the audit index, found by walking up from `start`."""
    directory = os.path.abspath(start)
    while not os.path.exists(os.path.join(directory, INDEX_NAME)):
        parent = os.path.dirname(directory)
        if parent == directory:
            raise SystemExit(f"{INDEX_NAME} not found above {start}")
        directory = parent
    return directory


def mint_id(now):
    return now.strftime(ID_FORMAT)


def index_line(audit_id, headline):
    return f"- [`{audit_id}`](docs/performance-audits/{audit_id}.md) — {headline}"


def entry_text(audit_id, platform, headline):
    """The opening paragraph every entry starts with, wrapped like the corpus."""
    opening = f"`{audit_id}` ({platform}; **{headline}**)."
    return "\n".join(textwrap.wrap(opening, width=WRAP_COLUMNS)) + "\n"


def insert_index_line(index_text, line):
    lines = index_text.split("\n")
    heading = lines.index(INDEX_HEADING)
    first_entry = next(
        position
        for position in range(heading + 1, len(lines))
        if lines[position].startswith("- [")
    )
    return "\n".join(lines[:first_entry] + [line] + lines[first_entry:])


def create(root, audit_id, platform, headline):
    path = os.path.join(root, AUDIT_DIRECTORY, audit_id + ".md")
    if os.path.exists(path):
        raise SystemExit(f"{path} already exists — mint again, a second later")
    index_path = os.path.join(root, INDEX_NAME)
    with open(index_path, encoding="utf-8") as handle:
        index_text = handle.read()
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(entry_text(audit_id, platform, headline))
    with open(index_path, "w", encoding="utf-8") as handle:
        handle.write(insert_index_line(index_text, index_line(audit_id, headline)))
    return path


def check(root):
    """Every audit file has one index line, every index line has its file, and
    each entry's own opening id matches the name of the file it is in."""
    problems = []
    directory = os.path.join(root, AUDIT_DIRECTORY)
    on_disk = sorted(
        name[: -len(".md")] for name in os.listdir(directory) if name.endswith(".md")
    )
    with open(os.path.join(root, INDEX_NAME), encoding="utf-8") as handle:
        index_text = handle.read()

    indexed = []
    for line in index_text.split("\n"):
        match = INDEX_LINE.match(line)
        if match:
            indexed.append(match.group(1))
            if match.group(1) != match.group(2):
                problems.append(f"index line for `{match.group(1)}` links to {match.group(2)}.md")

    for audit_id in sorted(set(indexed) - set(on_disk)):
        problems.append(f"`{audit_id}` is indexed but has no file")
    for audit_id in sorted(set(on_disk) - set(indexed)):
        problems.append(f"`{audit_id}` has a file but no index line")
    for audit_id in sorted(audit_id for audit_id in set(indexed) if indexed.count(audit_id) > 1):
        problems.append(f"`{audit_id}` is indexed more than once")

    for audit_id in on_disk:
        if not TIMESTAMP_ID.match(audit_id) and not LEGACY_ID.match(audit_id):
            problems.append(f"`{audit_id}` is neither a timestamp id nor a legacy id")
        with open(os.path.join(directory, audit_id + ".md"), encoding="utf-8") as handle:
            head = ENTRY_HEAD.match(handle.readline())
        if not head:
            problems.append(f"`{audit_id}` does not open with its id and a parenthetical")
        elif head.group(1) != audit_id:
            problems.append(f"`{audit_id}` opens with the id `{head.group(1)}`")
    return problems


def self_test():
    now = datetime.datetime(2026, 8, 22, 14, 3, 51, tzinfo=datetime.timezone.utc)
    assert mint_id(now) == "2026-08-22T140351Z"
    assert TIMESTAMP_ID.match(mint_id(now))
    # Timestamp ids sort chronologically as plain text, which the letter ids
    # stopped doing at `z`.
    assert sorted(["2026-08-22T140351Z", "2026-08-22T090000Z"])[0].endswith("090000Z")
    assert sorted(["2026-08-09aa", "2026-08-09b"])[0] == "2026-08-09aa"

    text = "\n".join([INDEX_HEADING, "", "- [`old`](docs/performance-audits/old.md) — x", ""])
    inserted = insert_index_line(text, "- [`new`](docs/performance-audits/new.md) — y")
    assert inserted.split("\n")[2].startswith("- [`new`]"), inserted
    assert inserted.split("\n")[3].startswith("- [`old`]"), inserted

    line = index_line("2026-08-22T140351Z", "a headline")
    assert INDEX_LINE.match(line), line
    assert INDEX_LINE.match(line).group(1) == INDEX_LINE.match(line).group(2)

    entry = entry_text("2026-08-22T140351Z", "Linux x86_64", "a headline that is long " * 4)
    assert ENTRY_HEAD.match(entry).group(1) == "2026-08-22T140351Z"
    assert max(len(l) for l in entry.split("\n")) <= WRAP_COLUMNS
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("headline", nargs="?", help="what the audit did, one clause")
    parser.add_argument("--platform", help='e.g. "Linux x86_64, Zen 4 7940HS"')
    parser.add_argument("--check", action="store_true",
                        help="verify the index and the audit directory agree")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    root = repository_root(os.path.dirname(os.path.abspath(__file__)))

    if arguments.self_test:
        return self_test()
    if arguments.check:
        problems = check(root)
        for problem in problems:
            print(problem)
        print(f"{len(problems)} problems")
        return 1 if problems else 0
    if not arguments.headline or not arguments.platform:
        parser.error("a headline and --platform are required to mint an audit")

    path = create(root, mint_id(datetime.datetime.now(datetime.timezone.utc)),
                  arguments.platform, arguments.headline)
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
