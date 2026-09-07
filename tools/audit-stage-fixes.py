"""Stage the independently reproduced #263 fix, not active #249/#251 work."""
import hashlib
import json
from pathlib import Path
import subprocess

BASE = "ebfdd11705bc470564e726857e498f1c82aa746c"
BRANCH = "audit/staged-263-20260907"
TREE = "fbda3d1566ff03860a3c8c9ec6374175725c22c4"
CHECKSUM = "24c9a287ae8d07f5f749b638eab0139de3fe1031bb8f4b5af8268dbfb78723ee"
PATHS = ["src/buster/lib/compiler/llvm/bitcode.c", "src/buster/tests/compiler/driver/driver_test.c", "tests/basic_c_llvm_switch.c", "tests/basic_c_llvm_switch_caller.c"]

def git(*args, data=None):
    return subprocess.run(["git", "-C", "work", *args], input=data, stdout=subprocess.PIPE, check=True).stdout.decode().strip()

assert git("rev-parse", "HEAD") == BASE
assert git("status", "--porcelain") == ""
assert not git("ls-remote", "--heads", "origin", "refs/heads/" + BRANCH), "Ref already exists; refusing to replace it"
patch = Path("control/tools/audit-llvm-switch.patch").read_bytes()
assert hashlib.sha256(patch).hexdigest() == CHECKSUM
git("apply", "--check", "--index", "-", data=patch)
git("apply", "--index", "-", data=patch)
assert sorted(git("diff", "--cached", "--name-only").splitlines()) == sorted(PATHS)
git("diff", "--cached", "--check")
assert git("write-tree") == TREE, "Tree differs from the local validated candidate"
git("config", "user.name", "github-actions[bot]")
git("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
git("commit", "-m", "fix: preserve canonical switch target order in LLVM bitcode")
commit = git("rev-parse", "HEAD")
assert git("rev-parse", "HEAD^") == BASE
git("push", "origin", commit + ":refs/heads/" + BRANCH)
result = {"263": {"commit": commit, "tree": TREE, "base": BASE, "branch": BRANCH, "patch_sha256": CHECKSUM}}
Path("staged-fixes.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
