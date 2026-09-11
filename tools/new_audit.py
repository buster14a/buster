#!/usr/bin/env python3
"""Mint a performance audit: its id and its file, and nothing shared.

An audit id used to be a date plus a sequence letter, `2026-08-08k`.  That id
is chosen by counting the day's existing entries, which is a value two
concurrent sessions compute identically — three of the four audit PRs open when
the history was split into one file per audit had independently claimed
`2026-08-22a`.  The id is now the UTC timestamp at which the audit is recorded,
`2026-08-22T140351Z`: ISO 8601 with the colons dropped, because Windows forbids
them in filenames.  Two sessions cannot mint the same one without writing their
entries in the same second.

Every audit also used to insert a line at the top of the index in
`PERFORMANCE_AUDITS.md`, so any two open audit branches conflicted as soon as
one landed.  `.gitattributes` marked the index `merge=union`, but GitHub's
mergeability check does not apply it, and the local union merges that cleared
the conflict kept both sides' lines without knowing which was newer.  The index
is now closed at the id in its heading: a new audit writes its own file and
nothing else, and since timestamp ids sort chronologically, the directory is
the index past that id.  `--list` merges the two into one history.

Usage:
    tools/new_audit.py --platform "Linux x86_64, Zen 4 7940HS" "the headline"
    tools/new_audit.py --newest
    tools/new_audit.py --list
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
ID_FORMAT = "%Y-%m-%dT%H%M%SZ"
WRAP_COLUMNS = 78

TIMESTAMP_ID = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{6}Z$")
# Entries recorded before 2026-08-22.  Kept as written: the corpus
# cross-references them by name.
LEGACY_ID = re.compile(r"^\d{4}-\d{2}-\d{2}[a-z]*$")
# The closed index names, in its heading, the newest audit it lists.
INDEX_HEADING = re.compile(r"^## Audits through `(\d{4}-\d{2}-\d{2}T\d{6}Z)`, newest first$")
INDEX_LINE = re.compile(r"^- \[`([^`]+)`\]\(docs/performance-audits/([^)]+)\.md\) — (.*)$")
ENTRY_HEAD = re.compile(r"^`([^`]+)` \(")
HEADLINE = re.compile(r"\*\*(.+?)\*\*", re.S)


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


def entry_text(audit_id, platform, headline):
    """The opening paragraph every entry starts with, wrapped like the corpus.
    Breaking only at spaces lets `file_headline` rejoin the headline exactly."""
    opening = f"`{audit_id}` ({platform}; **{headline}**)."
    lines = textwrap.wrap(opening, width=WRAP_COLUMNS, break_on_hyphens=False, break_long_words=False)
    return "\n".join(lines) + "\n"


def file_headline(opening):
    """The headline an audit carries in the bold span of its opening paragraph,
    or the whole paragraph when it has none."""
    bold = HEADLINE.search(opening)
    return " ".join((bold.group(1) if bold else opening).split())


def parse_index(index_text):
    """The id the index is closed at (None without the heading), and its lines
    in order as (id, linked file name, headline)."""
    closed_through = None
    entries = []
    for line in index_text.split("\n"):
        heading = INDEX_HEADING.match(line)
        if heading:
            closed_through = heading.group(1)
        entry = INDEX_LINE.match(line)
        if entry:
            entries.append(entry.groups())
    return closed_through, entries


def read_index(root):
    with open(os.path.join(root, INDEX_NAME), encoding="utf-8") as handle:
        return parse_index(handle.read())


def audit_openings(root):
    """Every audit on disk: id -> the opening paragraph of its file."""
    directory = os.path.join(root, AUDIT_DIRECTORY)
    openings = {}
    for name in os.listdir(directory):
        if name.endswith(".md"):
            with open(os.path.join(directory, name), encoding="utf-8") as handle:
                openings[name[: -len(".md")]] = handle.read().split("\n\n", 1)[0]
    return openings


def listing(entries, openings):
    """Every audit, newest first: the timestamp ids in descending order, then
    the letter ids in the order the index records, since their names do not
    sort.  An indexed audit keeps its index headline; a later one supplies its
    own."""
    indexed = {audit_id: headline for audit_id, _, headline in entries}
    stamped = sorted((audit_id for audit_id in openings if TIMESTAMP_ID.match(audit_id)), reverse=True)
    rows = [(audit_id, indexed.get(audit_id) or file_headline(openings[audit_id])) for audit_id in stamped]
    rows += [(audit_id, headline) for audit_id, _, headline in entries if not TIMESTAMP_ID.match(audit_id)]
    return rows


def newest(openings):
    return max(audit_id for audit_id in openings if TIMESTAMP_ID.match(audit_id))


def create(root, audit_id, platform, headline):
    path = os.path.join(root, AUDIT_DIRECTORY, audit_id + ".md")
    if os.path.exists(path):
        raise SystemExit(f"{path} already exists — mint again, a second later")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(entry_text(audit_id, platform, headline))
    return path


def check(root):
    """The closed index and the audit directory agree: every index line links
    its own file and names an audit no newer than the id the index is closed
    at, every letter-id audit is indexed, and every audit the index does not
    list opens with its own id, which is where `--list` finds its headline."""
    problems = []
    closed_through, entries = read_index(root)
    openings = audit_openings(root)
    indexed = [audit_id for audit_id, _, _ in entries]
    if closed_through is None:
        problems.append(f"{INDEX_NAME} has no heading `## Audits through `<id>`, newest first`")

    for audit_id, linked, _ in entries:
        if audit_id != linked:
            problems.append(f"index line for `{audit_id}` links to {linked}.md")
        if audit_id not in openings:
            problems.append(f"`{audit_id}` is indexed but has no file")
        if closed_through and TIMESTAMP_ID.match(audit_id) and audit_id > closed_through:
            problems.append(f"`{audit_id}` is indexed, but the index is closed at `{closed_through}`: "
                            "a new audit is its file alone")
    for audit_id in sorted(audit_id for audit_id in set(indexed) if indexed.count(audit_id) > 1):
        problems.append(f"`{audit_id}` is indexed more than once")

    for audit_id in sorted(openings):
        if not TIMESTAMP_ID.match(audit_id) and not LEGACY_ID.match(audit_id):
            problems.append(f"`{audit_id}` is neither a timestamp id nor a legacy id")
        elif LEGACY_ID.match(audit_id) and audit_id not in indexed:
            problems.append(f"`{audit_id}` has a file but no index line")
        head = ENTRY_HEAD.match(openings[audit_id])
        if head and head.group(1) != audit_id:
            problems.append(f"`{audit_id}` opens with the id `{head.group(1)}`")
        elif not head and audit_id not in indexed:
            problems.append(f"`{audit_id}` does not open with its id and a parenthetical")
    return problems


def self_test():
    now = datetime.datetime(2026, 8, 22, 14, 3, 51, tzinfo=datetime.timezone.utc)
    assert mint_id(now) == "2026-08-22T140351Z"
    assert TIMESTAMP_ID.match(mint_id(now))
    # Timestamp ids sort chronologically as plain text, which the letter ids
    # stopped doing at `z`.
    assert sorted(["2026-08-22T140351Z", "2026-08-22T090000Z"])[0].endswith("090000Z")
    assert sorted(["2026-08-09aa", "2026-08-09b"])[0] == "2026-08-09aa"

    headline = " ".join(["a byte-identical headline long enough to wrap"] * 4)
    entry = entry_text("2026-08-22T140351Z", "Linux x86_64", headline)
    assert ENTRY_HEAD.match(entry).group(1) == "2026-08-22T140351Z"
    assert max(len(l) for l in entry.split("\n")) <= WRAP_COLUMNS
    assert file_headline(entry) == headline, entry

    closed_through, entries = parse_index("\n".join([
        "## Audits through `2026-08-22T140351Z`, newest first",
        "",
        "- [`2026-08-22T140351Z`](docs/performance-audits/2026-08-22T140351Z.md) — indexed",
        "- [`2026-08-22b`](docs/performance-audits/2026-08-22b.md) — letter b",
        "- [`2026-08-22a`](docs/performance-audits/2026-08-22a.md) — letter a",
        "",
    ]))
    assert closed_through == "2026-08-22T140351Z"
    openings = {
        "2026-08-22T140351Z": "`2026-08-22T140351Z` (x; **not the index headline**).",
        "2026-08-22T090000Z": "`2026-08-22T090000Z` (x; **recovered after the index closed**).",
        "2026-08-23T000000Z": "`2026-08-23T000000Z` (x; **the newest**).",
        "2026-08-22b": "b",
        "2026-08-22a": "a",
    }
    rows = listing(entries, openings)
    assert rows == [
        ("2026-08-23T000000Z", "the newest"),
        ("2026-08-22T140351Z", "indexed"),
        ("2026-08-22T090000Z", "recovered after the index closed"),
        ("2026-08-22b", "letter b"),
        ("2026-08-22a", "letter a"),
    ], rows
    assert newest(openings) == "2026-08-23T000000Z"
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("headline", nargs="?", help="what the audit did, one clause")
    parser.add_argument("--platform", help='e.g. "Linux x86_64, Zen 4 7940HS"')
    parser.add_argument("--newest", action="store_true", help="print the newest audit's path")
    parser.add_argument("--list", action="store_true", help="print every audit, newest first")
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
    if arguments.newest:
        print(os.path.join(AUDIT_DIRECTORY, newest(audit_openings(root)) + ".md"))
        return 0
    if arguments.list:
        _, entries = read_index(root)
        for audit_id, headline in listing(entries, audit_openings(root)):
            print(f"{audit_id}  {headline}")
        return 0
    if not arguments.headline or not arguments.platform:
        parser.error("a headline and --platform are required to mint an audit")

    path = create(root, mint_id(datetime.datetime.now(datetime.timezone.utc)),
                  arguments.platform, arguments.headline)
    print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
