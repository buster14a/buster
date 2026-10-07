"""One selection contract for direct 9700X workloads (#2924).

Shared by the hosted authorization (changed-file rows from GitHub's API) and
the executor (`git diff`), so both reduce a pull request to the same workload
set or the same refusal. Imports nothing local.

Contract. Only files directly inside `benchmarks/9700x/` ending in `.c` or
`.data` are workload files; a workload is a `.c` named by WORKLOAD_NAME and its
optional same-stem `.data` input.
- Added or modified source or input selects that workload.
- A removed or renamed-away input selects the surviving source (it now has no
  input); a renamed-to input selects its new workload.
- A removed or renamed-away source is never compiled. It cancels its workload
  even when its input changed in the same pull request.
- A renamed source selects its new name only.
- A present `.c`/`.data` file whose name is outside WORKLOAD_NAME is refused,
  as are an unknown change status and more than WORKLOAD_LIMIT workloads.
  Removed files with unsupported names are ignored.

Changes are `(status, old, new)` with status added, modified, removed or
renamed; `old` is only meaningful for a rename, and a removal names its path
in `new`. Map: classify, select, git_changes, api_changes.
"""

from __future__ import annotations

import re

WORKLOAD_DIRECTORY = "benchmarks/9700x"
WORKLOAD_NAME = re.compile(r"benchmarks/9700x/[a-z0-9][a-z0-9_-]{0,47}\.c")
WORKLOAD_LIMIT = 4
PRESENT_STATUSES = ("added", "modified", "copied", "changed", "unchanged")


def classify(path: str) -> str:
    """'source', 'data' or '' for a path outside the workload files."""
    result = ""
    if path.startswith(WORKLOAD_DIRECTORY + "/") and "/" not in path[len(WORKLOAD_DIRECTORY) + 1:]:
        result = "source" if path.endswith(".c") else "data" if path.endswith(".data") else ""
    return result


def select(changes: list[tuple[str, str, str]]) -> tuple[list[str], list[str]]:
    """(sorted workload sources, refusal reasons) for normalized changes."""
    selected: set[str] = set()
    gone: set[str] = set()
    problems: list[str] = []
    for status, old, new in changes:
        if status == "removed":
            touched = [(new, False)]
        elif status == "renamed":
            touched = [(old, False), (new, True)]
        elif status in PRESENT_STATUSES:
            touched = [(new, True)]
        else:
            touched = []
            if classify(new) or classify(old):
                problems.append(f"{new}: unsupported change status {status!r}")
        for path, present in touched:
            kind = classify(path)
            stem = path.rsplit(".", 1)[0] + ".c"
            valid = bool(WORKLOAD_NAME.fullmatch(stem))
            if not kind or (not present and not valid):
                continue
            if not valid:
                problems.append(f"{path}: unsupported workload filename (needs {WORKLOAD_NAME.pattern}"
                                " or the same stem with .data)")
            elif kind == "source" and not present:
                gone.add(stem)
            else:
                selected.add(stem)
    result = sorted(selected - gone)
    if len(result) > WORKLOAD_LIMIT:
        problems.append(f"more than {WORKLOAD_LIMIT} workloads changed: {len(result)}")
    return result, problems


def git_changes(listing: str) -> list[tuple[str, str, str]]:
    """Normalize `git diff --no-renames --name-status -z` output."""
    tokens = [token for token in listing.split("\0") if token]
    kinds = {"A": "added", "M": "modified", "T": "modified", "D": "removed"}
    return [(kinds.get(tokens[index], tokens[index]), "", tokens[index + 1])
            for index in range(0, len(tokens) - 1, 2)]


def api_changes(files: list[dict]) -> list[tuple[str, str, str]]:
    """Normalize validated GitHub changed-file rows."""
    return [(row["status"], row.get("previous_filename") or "", row["filename"]) for row in files]
