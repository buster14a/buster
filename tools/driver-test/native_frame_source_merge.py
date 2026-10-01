#!/usr/bin/env python3
"""Read-only Git three-way merge evidence; never updates any Git ref."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(os.environ["GITHUB_WORKSPACE"])
OUT = Path(os.environ["RUNNER_TEMP"]) / "native-frame-source-merge"
OUT.mkdir(exist_ok=True)
BASELINE = os.environ["BUSTER_FRAME_BASELINE"]
FEATURE = os.environ["BUSTER_FRAME_FEATURE"]
DRIVER = "src/buster/tests/compiler/driver/driver_test.c"
EXPECTED = {
    ".github/workflows/driver-test-attribution.yml",
    "docs/driver-test-timing.md",
    DRIVER,
    "tools/driver-test/host_native_frame_batch.c",
    "tools/driver-test/host_native_frame_batch_control.c",
}
def git(*arguments):
    return subprocess.check_output(["git", *arguments], cwd=ROOT).decode("utf-8")
def record(value):
    (OUT / "merge.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    print("NATIVE_FRAME_SOURCE_MERGE_V1 " + json.dumps(value, ensure_ascii=True), flush=True)

result = {"baseline": BASELINE, "feature": FEATURE, "success": False}
try:
    if not all(re.fullmatch(r"[0-9a-f]{40}", value) for value in (BASELINE, FEATURE)):
        raise RuntimeError("Only complete immutable commit IDs are accepted")
    for revision in (BASELINE, FEATURE):
        git("cat-file", "-e", revision + "^{commit}")
    result["merge_base"] = git("merge-base", BASELINE, FEATURE).strip()
    process = subprocess.run(["git", "merge-tree", "--write-tree", BASELINE, FEATURE], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (OUT / "merge-tree.stdout").write_bytes(process.stdout)
    (OUT / "merge-tree.stderr").write_bytes(process.stderr)
    if process.returncode != 0:
        raise RuntimeError("Actual Git three-way merge reported a conflict; no conflict preference applied")
    tree = process.stdout.decode("ascii").splitlines()[0]
    if not re.fullmatch(r"[0-9a-f]{40}", tree):
        raise RuntimeError("Merge did not emit a complete tree ID")
    result["merged_tree"] = tree
    changed = git("diff", "--name-only", BASELINE, tree).splitlines()
    if set(changed) != EXPECTED:
        raise RuntimeError("Combined tree changes unexpected paths: " + repr(changed))
    if git("diff", "--name-only", BASELINE, tree, "--", "src").splitlines() != [DRIVER]:
        raise RuntimeError("Combined src tree changes anything beyond the feature driver")
    git("diff", "--check", BASELINE, tree)
    result["baseline_tree"] = git("rev-parse", BASELINE + "^{tree}").strip()
    result["baseline_src_tree"] = git("rev-parse", BASELINE + ":src").strip()
    result["merged_src_tree"] = git("rev-parse", tree + ":src").strip()
    result["changed_entries"] = []
    for path in sorted(changed):
        row = git("ls-tree", tree, "--", path).strip()
        mode_type_sha, actual_path = row.split("\t", 1)
        mode, kind, sha = mode_type_sha.split()
        if kind != "blob" or actual_path != path:
            raise RuntimeError("Unexpected merge tree entry")
        result["changed_entries"].append({"path": path, "mode": mode, "type": kind, "sha": sha})
    result["driver_blobs"] = {label: git("rev-parse", revision + ":" + DRIVER).strip() for label, revision in (("ancestor", result["merge_base"]), ("baseline", BASELINE), ("feature", FEATURE), ("merged", tree))}
    raw = subprocess.check_output(["git", "show", tree + ":" + DRIVER], cwd=ROOT)
    (OUT / "merged-driver.txt").write_bytes(raw)
    result["driver_bytes"] = len(raw)
    result["driver_sha256"] = hashlib.sha256(raw).hexdigest()
    result["driver_git_blob"] = hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw).hexdigest()
    if result["driver_git_blob"] != result["driver_blobs"]["merged"]:
        raise RuntimeError("Merged driver content identity mismatch")
    diff = subprocess.check_output(["git", "diff", BASELINE, tree], cwd=ROOT)
    (OUT / "feature.diff").write_bytes(diff)
    result["success"] = True
    record(result)
    # Public source only, bounded JSON lines. Connector can recreate exactly this
    # audited Git blob without granting the hosted helper repository write access.
    source = raw.decode("utf-8")
    size = 16384
    chunks = [source[start:start+size] for start in range(0, len(source), size)]
    print("NATIVE_FRAME_SOURCE_DRIVER_CHUNKS_V1 " + json.dumps({"count": len(chunks)}, ensure_ascii=True), flush=True)
    for index, value in enumerate(chunks):
        print("NATIVE_FRAME_SOURCE_DRIVER_CHUNK_V1 " + json.dumps({"index": index, "value": value}, ensure_ascii=True), flush=True)
except BaseException as error:
    result["error"] = repr(error)
    record(result)
    raise
