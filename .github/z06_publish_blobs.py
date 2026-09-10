"""Transport five reviewed source blobs; never update refs or execute a build."""
import base64
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request
import zlib

BASE = "ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae"
PATCH_SHA256 = "a5875df4e474dfebb78ccf546acd7a3df54ca3c15628df049d9324007ab49067"
PATHS = ["docs/agents/frontend/foundations.md", "src/buster/lib/compiler/frontend/c/c_source.c", "src/buster/tests/compiler/driver/driver_test.c", "src/buster/tests/compiler/frontend/c/c_test.c", "tests/basic_c_macro_task_batches.c"]
root = Path.cwd()
out = Path(os.environ["RUNNER_TEMP"]) / "z06-source-objects"
out.mkdir(exist_ok=False)
work = Path(os.environ["RUNNER_TEMP"]) / "z06-pinned-worktree"

def git(*args):
    return subprocess.check_output(["git", "-C", str(work), *args], text=True).strip()

encoded = "".join((root / ".github/z06-patch" / ("part" + str(index))).read_text().strip() for index in range(4))
patch = zlib.decompress(base64.b64decode(encoded, validate=True))
if hashlib.sha256(patch).hexdigest() != PATCH_SHA256:
    raise RuntimeError("Patch digest mismatch")
(out / "candidate.patch").write_bytes(patch)
subprocess.run(["git", "worktree", "add", "--detach", str(work), BASE], check=True)
if git("rev-parse", "HEAD") != BASE or git("rev-parse", "origin/main") != BASE:
    raise RuntimeError("Main moved; explicit reconciliation required")
subprocess.run(["git", "-C", str(work), "apply", "--check", "--whitespace=error-all", str(out / "candidate.patch")], check=True)
subprocess.run(["git", "-C", str(work), "apply", "--whitespace=error-all", str(out / "candidate.patch")], check=True)
subprocess.run(["git", "-C", str(work), "add", "--", *PATHS], check=True)
subprocess.run(["git", "-C", str(work), "diff", "--cached", "--check"], check=True)
if git("diff", "--cached", "--name-only").splitlines() != PATHS:
    raise RuntimeError("Unexpected changed file set")
manifest = {"base": BASE, "patch_sha256": PATCH_SHA256, "expected_tree": git("write-tree"), "files": []}
for path in PATHS:
    data = (work / path).read_bytes()
    expected_blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    payload = json.dumps({"content": base64.b64encode(data).decode(), "encoding": "base64"}).encode()
    request = urllib.request.Request("https://api.github.com/repos/buster14a/buster/git/blobs", data=payload, method="POST", headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"], "Accept": "application/vnd.github+json", "Content-Type": "application/json", "User-Agent": "buster-z06-source-objects"})
    with urllib.request.urlopen(request, timeout=60) as response:
        blob = json.load(response)
    if blob["sha"] != expected_blob:
        raise RuntimeError("Blob digest mismatch for " + path)
    manifest["files"].append({"path": path, "mode": "100644", "type": "blob", "sha": expected_blob, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps(manifest, indent=2))
with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
    summary.write("Uploaded five unreferenced Git source blobs for the Z06 candidate. No branch, PR, issue, repository setting, compiler build, test or performance result is changed/claimed by this transport.\n\n")
    summary.write("Patch SHA-256: `" + PATCH_SHA256 + "`. Expected implementation tree: `" + manifest["expected_tree"] + "`.\n")
