#!/usr/bin/env python3
"""Delete remote branches that a branch-cleanup inventory marks for deletion.

The inventory (`docs/audits/<date>-branch-cleanup/inventory.tsv`) records, for
every remote branch at capture time, its tip SHA and an action. A `delete` row
is only deleted when all of these still hold:

- its tip commit is reachable from the archive anchor branch, so no commit
  becomes unreachable when the branch goes away;
- the live remote branch still points at the recorded SHA (a moved branch is
  held, never deleted under the old decision); and
- the push itself carries a `--force-with-lease` on that SHA, so a branch that
  moves between the check and the push is refused by the remote.

The default is a dry run that prints the plan. `--execute` pushes deletions in
batches and reports every branch it held. Nothing else in the repository is
changed. `--self-test` runs the checks against throwaway local repositories.

Usage:
    tools/delete_archived_branches.py docs/audits/2026-09-30-branch-cleanup/inventory.tsv
    tools/delete_archived_branches.py docs/audits/2026-09-30-branch-cleanup/inventory.tsv --execute
    tools/delete_archived_branches.py --self-test
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

ARCHIVE_BRANCH = "archive/branch-snapshot-20260930"
BATCH_SIZE = 50
HEADER = ["branch", "sha", "action", "reason", "date", "prs", "subject"]


def git(repo: pathlib.Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", "-C", str(repo), *args], text=True, capture_output=True, check=check)


def read_inventory(path: pathlib.Path) -> list[dict[str, str]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if lines[0].split("\t") != HEADER:
        raise SystemExit(f"{path}: unexpected header {lines[0]!r}")
    return [dict(zip(HEADER, line.split("\t"))) for line in lines[1:] if line]


def remote_heads(repo: pathlib.Path, remote: str) -> dict[str, str]:
    heads = {}
    for line in git(repo, "ls-remote", "--heads", remote).stdout.splitlines():
        sha, ref = line.split("\t")
        heads[ref.removeprefix("refs/heads/")] = sha
    return heads


def plan(repo: pathlib.Path, remote: str, rows: list[dict[str, str]], archive: str) -> tuple[list[dict[str, str]], list[tuple[str, str]]]:
    git(repo, "fetch", "--quiet", remote, f"+refs/heads/{archive}:refs/remotes/{remote}/{archive}")
    anchor = f"refs/remotes/{remote}/{archive}"
    heads = remote_heads(repo, remote)
    ready = []
    held = []
    for row in rows:
        if row["action"] != "delete":
            continue
        name = row["branch"]
        live = heads.get(name)
        if live is None:
            held.append((name, "already gone"))
        elif live != row["sha"]:
            held.append((name, f"moved to {live[:12]}; re-inventory before deleting"))
        elif git(repo, "cat-file", "-e", f"{row['sha']}^{{commit}}", check=False).returncode != 0:
            held.append((name, "tip commit missing locally; fetch full history first"))
        elif git(repo, "merge-base", "--is-ancestor", row["sha"], anchor, check=False).returncode != 0:
            held.append((name, f"tip not reachable from {archive}"))
        else:
            ready.append(row)
    return ready, held


def delete(repo: pathlib.Path, remote: str, ready: list[dict[str, str]]) -> list[tuple[str, str]]:
    failed = []
    for start in range(0, len(ready), BATCH_SIZE):
        batch = ready[start:start + BATCH_SIZE]
        leases = [f"--force-with-lease=refs/heads/{row['branch']}:{row['sha']}" for row in batch]
        refspecs = [f":refs/heads/{row['branch']}" for row in batch]
        result = git(repo, "push", "--porcelain", *leases, remote, *refspecs, check=False)
        for row in batch:
            ref = f"refs/heads/{row['branch']}"
            status = [line for line in result.stdout.splitlines() if f":{ref}\t" in line]
            if not status or not status[0].startswith("-"):
                failed.append((row["branch"], (status[0] if status else result.stderr.strip()) or "push failed"))
    return failed


def run(inventory: pathlib.Path, repo: pathlib.Path, remote: str, archive: str, execute: bool) -> int:
    rows = read_inventory(inventory)
    ready, held = plan(repo, remote, rows, archive)
    for name, why in held:
        print(f"hold\t{name}\t{why}")
    for row in ready:
        print(f"{'delete' if execute else 'would delete'}\t{row['branch']}\t{row['sha'][:12]}")
    failed = delete(repo, remote, ready) if execute else []
    for name, why in failed:
        print(f"failed\t{name}\t{why}")
    print(f"{len(ready) - len(failed)} {'deleted' if execute else 'deletable'}, {len(held)} held, {len(failed)} failed", file=sys.stderr)
    return 1 if failed else 0


def self_test() -> int:
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        origin = root / "origin.git"
        work = root / "work"
        subprocess.run(["git", "init", "--quiet", "--bare", str(origin)], check=True)
        subprocess.run(["git", "init", "--quiet", "-b", "main", str(work)], check=True)
        git(work, "config", "user.email", "t@example.com")
        git(work, "config", "user.name", "t")
        git(work, "remote", "add", "origin", str(origin))
        git(work, "commit", "--quiet", "--allow-empty", "-m", "base")
        shas = {}
        for name in ["gone", "keep", "moved", "orphan", "safe"]:
            git(work, "checkout", "--quiet", "-B", name, "main")
            git(work, "commit", "--quiet", "--allow-empty", "-m", name)
            shas[name] = git(work, "rev-parse", "HEAD").stdout.strip()
        tree = git(work, "rev-parse", "main^{tree}").stdout.strip()
        parents = [arg for name in ["gone", "keep", "moved", "safe"] for arg in ("-p", shas[name])]
        anchor = git(work, "commit-tree", tree, "-p", "main", *parents, "-m", "anchor").stdout.strip()
        git(work, "push", "--quiet", "origin", "main", "keep", "moved", "orphan", "safe", f"{anchor}:refs/heads/{ARCHIVE_BRANCH}")
        git(work, "checkout", "--quiet", "moved")
        git(work, "commit", "--quiet", "--allow-empty", "-m", "moved on")
        git(work, "push", "--quiet", "origin", "moved")
        inventory = root / "inventory.tsv"
        lines = ["\t".join(HEADER)] + [f"{name}\t{shas[name]}\t{'keep' if name == 'keep' else 'delete'}\tr\t-\t-\ts" for name in shas]
        inventory.write_text("\n".join(lines) + "\n", encoding="utf-8")
        status = run(inventory, work, "origin", ARCHIVE_BRANCH, True)
        left = set(remote_heads(work, "origin"))
        expected = {"main", "keep", "moved", "orphan", ARCHIVE_BRANCH}
        ok = status == 0 and left == expected
        print("self-test passed" if ok else f"self-test FAILED: status={status} left={sorted(left)}")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("inventory", nargs="?", type=pathlib.Path)
    parser.add_argument("--remote", default="origin")
    parser.add_argument("--archive", default=ARCHIVE_BRANCH)
    parser.add_argument("--execute", action="store_true", help="push deletions instead of printing the plan")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.inventory is None:
        parser.error("inventory path required")
    return run(args.inventory, pathlib.Path.cwd(), args.remote, args.archive, args.execute)


if __name__ == "__main__":
    sys.exit(main())
