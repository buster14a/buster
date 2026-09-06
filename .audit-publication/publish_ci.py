#!/usr/bin/env python3
"""One-shot, branch-only publication of the user's September 6 audit package.

Runs only on the isolated publication branch. Never updates main, force-pushes,
merges, edits repository settings, or uses a personal credential. The workflow
supplies a contents-write GitHub Actions token; PR creation is done separately.
"""
import hashlib
import json
import lzma
import os
from pathlib import Path, PurePosixPath
import signal
import subprocess
import tempfile

BASE = "31cff20271188e6429d7d40a5c5fe89dd8fb540a"
PACKAGE_SHA256 = "7fd937f5158b1f97f3121961fcbad249123ba5da449768fe4c28da8a4595cc6e"
REPO = "buster14a/buster"
PUBLICATION_BRANCH = "audit/2026-09-06-publication"
RECORD_BRANCH = "audit/2026-09-06-public-record"
DOC_ROOT = Path("docs/audits/2026-09-06")
ISSUES = {"ebpf-boolean": 165, "ebpf-signed-not": 166,
          "wasm-alignment": 167, "ebpf-symbol-growth": 168}
ALLOWED_BRANCHES = {"audit/2026-09-06-ebpf-scalar",
                    "audit/2026-09-06-wasm-alignment",
                    "audit/2026-09-06-runtime-boundaries", RECORD_BRANCH}
ROOT = Path.cwd()


def git(*args, cwd=ROOT, check=True):
    p = subprocess.run(["git", *args], cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and p.returncode:
        raise RuntimeError(f"git {args[0]} failed: {p.stderr}")
    return p


def probe(command, cwd, env):
    process = subprocess.Popen(command, cwd=cwd, env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True)
    try:
        text, _ = process.communicate(timeout=180)
        return {"command": command, "exit_code": process.returncode, "output": text}
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        text, _ = process.communicate()
        return {"command": command, "exit_code": 124, "output": text + "\nTIMEOUT: 180 seconds\n"}


def publish(worktree, branch, message):
    if branch not in ALLOWED_BRANCHES:
        raise RuntimeError("branch is not in the fixed publication allowlist")
    git("config", "user.name", "github-actions[bot]", cwd=worktree)
    git("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com", cwd=worktree)
    if branch == RECORD_BRANCH:
        # Exact patches/raw diagnostics intentionally retain format whitespace.
        git("diff", "--cached", "--check", "--", ".",
            ":(exclude)docs/audits/2026-09-06/patches/*.patch",
            ":(exclude)docs/audits/2026-09-06/evidence/*.log", cwd=worktree)
    else:
        git("diff", "--cached", "--check", cwd=worktree)
    tree = git("write-tree", cwd=worktree).stdout.strip()
    existing = git("ls-remote", "--exit-code", "origin", "refs/heads/" + branch, check=False)
    if existing.returncode == 0:
        sha = existing.stdout.split()[0]
        git("fetch", "--depth=1", "origin", sha)
        if git("rev-parse", sha + "^{tree}").stdout.strip() != tree:
            raise RuntimeError(f"refusing to alter existing different branch {branch}")
        return sha
    if existing.returncode != 2:
        raise RuntimeError("could not establish whether destination branch exists")
    git("commit", "-m", message, cwd=worktree)
    sha = git("rev-parse", "HEAD", cwd=worktree).stdout.strip()
    git("push", "origin", "HEAD:refs/heads/" + branch, cwd=worktree)
    remote = git("ls-remote", "origin", "refs/heads/" + branch).stdout.split()[0]
    if remote != sha:
        raise RuntimeError("published branch SHA verification failed")
    return sha


def main():
    if os.environ.get("GITHUB_REPOSITORY") != REPO or os.environ.get("GITHUB_REF") != "refs/heads/" + PUBLICATION_BRANCH:
        raise RuntimeError("this publisher is restricted to its one repository and branch")
    if os.environ.get("GITHUB_SERVER_URL") != "https://github.com":
        raise RuntimeError("this publisher runs only on github.com")
    compressed = b"".join((ROOT / ".audit-publication" / f"package.{i}").read_bytes() for i in range(5))
    if hashlib.sha256(compressed).hexdigest() != PACKAGE_SHA256:
        raise RuntimeError("audit package checksum mismatch")
    files = json.loads(lzma.decompress(compressed))
    if len(files) != 53:
        raise RuntimeError("unexpected audit package file count")
    for name, content in files.items():
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or not isinstance(content, str):
            raise RuntimeError("invalid audit package entry")
    manifest = json.loads(files["manifest.json"])
    if manifest["repository"] != REPO:
        raise RuntimeError("unexpected audit repository")
    git("fetch", "--depth=1", "origin", BASE)
    if git("rev-parse", BASE).stdout.strip() != BASE:
        raise RuntimeError("base SHA mismatch")
    run_url = f"https://github.com/{REPO}/actions/runs/{os.environ['GITHUB_RUN_ID']}"
    results = {"repository": REPO, "base_sha": BASE, "workflow_url": run_url,
               "package_sha256": PACKAGE_SHA256, "issues": ISSUES, "patches": []}
    evidence = {}
    # No token is inherited by compiler/test subprocesses. Git's temporary auth
    # header remains confined to the publisher's git repository configuration.
    env = {k: v for k, v in os.environ.items() if k not in {"GH_TOKEN", "GITHUB_TOKEN", "GH_ENTERPRISE_TOKEN"}}
    env.update({"CC": "clang", "AUDIT_OPT": "-O2", "AUDIT_SANITIZE": "1",
                "ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    # Prefer stable memory64 when supported; retain the original experimental flag otherwise.
    # Use a real memory64 module; a constructor can ignore unknown properties.
    stable = subprocess.run(["node", "-e", "new WebAssembly.Module(Uint8Array.from([0,97,115,109,1,0,0,0,5,3,1,4,1]))"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if stable.returncode == 0:
        env["AUDIT_NODE_FLAGS"] = ""
    with tempfile.TemporaryDirectory(prefix="buster-publication-") as temporary:
        temporary = Path(temporary)
        package = temporary / "package"
        package.mkdir()
        for name, content in files.items():
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        for item in manifest["pull_requests"]:
            key, branch = item["key"], item["branch"]
            patch = package / item["patch"]
            if hashlib.sha256(patch.read_bytes()).hexdigest() != item["patch_sha256"]:
                raise RuntimeError("patch SHA256 mismatch")
            worktree = temporary / key
            git("worktree", "add", "--detach", str(worktree), BASE)
            observed = {path: git("rev-parse", BASE + ":" + path).stdout.strip()
                        for path in item["production_preimages"]}
            # #101 already landed: the complete file equals this patch's
            # postimage. Exclude only that proven-identical upstream change.
            upstream = set()
            mapping_path = "src/buster/lib/file.c"
            if key == "runtime-boundaries" and observed.get(mapping_path) == "a036e23a7abf55df010a8aefcb892e015b10598b":
                upstream.add(mapping_path)
            exclusions = ["--exclude=" + path for path in sorted(upstream)]
            git("apply", "--check", "--whitespace=error", *exclusions, str(patch), cwd=worktree)
            git("apply", "--index", "--whitespace=error", *exclusions, str(patch), cwd=worktree)
            changed = git("diff", "--cached", "--name-only", cwd=worktree).stdout.splitlines()
            expected = (set(item["production_preimages"]) - upstream) | {item["test"], item["test"].replace(".py", ".c")}
            if set(changed) != expected:
                raise RuntimeError("unexpected patched file set")
            # Keep the committed index pristine while running before/after against
            # exactly the selected base and the exact archived regression sources.
            patched = {path: (worktree / path).read_bytes() for path in item["production_preimages"]}
            for path in patched:
                (worktree / path).write_bytes(subprocess.check_output(["git", "show", BASE + ":" + path], cwd=ROOT))
            before = probe(["python3", item["test"]], worktree, env)
            for path, content in patched.items():
                (worktree / path).write_bytes(content)
            after = probe(["python3", item["test"]], worktree, env)
            record = {"key": key, "branch": branch, "observed_preimages": observed,
                      "original_audit_preimages": item["production_preimages"],
                      "already_present_upstream": sorted(upstream),
                      "baseline_exit_code": before["exit_code"], "patched_exit_code": after["exit_code"],
                      "validation": "focused Clang -O2 ASan/UBSan; leak checking disabled for process-lifetime arenas"}
            for label, result in [("baseline", before), ("patched", after)]:
                evidence[f"publication-{key}-{label}.log"] = result["output"]
                print(f"{key} {label} exit={result['exit_code']}\n{result['output'][-2500:]}", flush=True)
            git("diff", "--exit-code", cwd=worktree)
            record["commit_sha"] = publish(worktree, branch, item["title"] + "\n\nPublish the September 6 audit patch and opt-in regression, preserving identical changes already on main.\nFull integration and platform gates remain outstanding.")
            results["patches"].append(record)
            print("PUBLISHED " + json.dumps(record), flush=True)
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
                summary.write(f"- [{key}](https://github.com/{REPO}/tree/{branch}): `{record['commit_sha']}`; focused before/after exit {before['exit_code']}/{after['exit_code']}\n")
        record_tree = temporary / "record"
        git("worktree", "add", "--detach", str(record_tree), BASE)
        destination = record_tree / DOC_ROOT
        destination.mkdir(parents=True)
        for name, content in files.items():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        for name, content in evidence.items():
            (destination / "evidence" / name).write_text(content)
        # Preserve the delivered package byte-for-byte; the new entry point corrects
        # its historical publication status and the erroneous read-only conclusion.
        note = ["# Public audit record — September 6, 2026\n",
                "This directory publishes all 53 files of the delivered audit package, plus fresh publication-run evidence.\n",
                "**The original report, README and manifest retain their historical `not published` status. That status is superseded by this file. The earlier claim that the connector exposed no write actions was incorrect; authenticated GitHub writes subsequently succeeded.**\n",
                "The issue reports are public: " + ", ".join(f"[#{n}](https://github.com/{REPO}/issues/{n})" for n in ISSUES.values()) + ".\n",
                f"The code branches below were created independently from current-main snapshot `{BASE}`. No change was merged and main was not updated by this publication.\n",
                "| Patch | Public branch | Commit | Focused before / after exit |\n|---|---|---|---|\n"]
        for record in results["patches"]:
            note.append(f"| {record['key']} | [{record['branch']}](https://github.com/{REPO}/tree/{record['branch']}) | `{record['commit_sha']}` | {record['baseline_exit_code']} / {record['patched_exit_code']} |\n")
        note.append("\nThe #101 relative-file-mapping fix already exists in the publication base (file.c is exactly the original patch's postimage). The runtime branch therefore changes only os.c and string.c, while keeping all three original runtime regression scenarios. The existing Unicode conversion fix in string.c is preserved.\n")
        note.append("\nPublic code PRs already opened: [#169](https://github.com/buster14a/buster/pull/169) for eBPF and [#170](https://github.com/buster14a/buster/pull/170) for Wasm. The runtime PR and this evidence PR are linked in their public discussions after branch verification.\n")
        note += [f"\n[Publication workflow and full logs]({run_url}). Exact outcomes and preimage comparisons are in [publication-results.json](publication-results.json).\n",
                 "\nThe regression scripts remain opt-in. A zero patched exit is not a full test_all, Release self-host, native Windows/macOS, or kernel-eBPF result. Baseline failures must be read alongside the logs to distinguish semantic failures from host build problems. Full integration/platform gates remain outstanding, so code PRs should be drafts.\n",
                 "\nRead [AUDIT_REPORT.md](AUDIT_REPORT.md) for findings and original validation, [evidence/PROVENANCE.md](evidence/PROVENANCE.md) for provenance, and patches/ for exact standalone patchsets. The original SHA256SUMS applies to the delivered package files, not the newly added publication records.\n"]
        (destination / "PUBLICATION.md").write_text("\n".join(note))
        (destination / "publication-results.json").write_text(json.dumps(results, indent=2) + "\n")
        git("add", str(DOC_ROOT), cwd=record_tree)
        results["record_branch"] = RECORD_BRANCH
        results["record_commit_sha"] = publish(record_tree, RECORD_BRANCH, "docs: publish the complete September 6 audit package and validation evidence")
        print("PUBLICATION_COMPLETE " + json.dumps(results), flush=True)
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(f"\n[Public audit record](https://github.com/{REPO}/blob/{RECORD_BRANCH}/{DOC_ROOT}/PUBLICATION.md)\n")


if __name__ == "__main__":
    main()
