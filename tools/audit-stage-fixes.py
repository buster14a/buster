"""One-use transport for locally validated patches; never included in a fix PR."""
import hashlib
import json
import pathlib
import subprocess

BASE = "ebfdd11705bc470564e726857e498f1c82aa746c"
WORK = pathlib.Path("work")
PATCHES = [
    (184, "audit-i128.patch", "4622066fc9c578eefbe2134c69b2dccd20e1da3092d9dced65a1749a5756c200", "fix: preserve signed i128 division masks on x86-64", ["src/buster/lib/compiler/codegen/codegen.c", "src/buster/tests/compiler/driver/driver_test.c", "tests/basic_c_int128.c", "tests/basic_c_i128_division.c"]),
    (222, "audit-llvm-integer.patch", "f54c6c334657d153056624a6ceb8d6b3a66bbdd411746fac4bdd6baaece473f0", "fix: encode narrow LLVM sign-bit constants without the i64 sentinel", ["src/buster/lib/compiler/llvm/bitcode.c", "src/buster/lib/compiler/llvm/bitcode_internal.h", "src/buster/tests/compiler/llvm/bitcode_test.c", "src/buster/tests/compiler/driver/driver_test.c", "tests/basic_c_llvm_integer_constants.c", "tests/basic_c_llvm_integer_constants_caller.c"]),
]

def git(*args, data=None):
    return subprocess.run(["git", "-C", str(WORK), *args], input=data, stdout=subprocess.PIPE, check=True).stdout.decode().strip()

assert git("rev-parse", "HEAD") == BASE
assert git("status", "--porcelain") == ""
git("config", "user.name", "github-actions[bot]")
git("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
results = {}
for issue, filename, checksum, message, paths in PATCHES:
    branch = f"audit/staged-{issue}-20260907"
    assert not git("ls-remote", "--heads", "origin", "refs/heads/" + branch), "Ref already exists; refusing to replace it"
    git("checkout", "--detach", BASE)
    patch = (pathlib.Path("control/tools") / filename).read_bytes()
    assert hashlib.sha256(patch).hexdigest() == checksum
    git("apply", "--3way", "--index", "-", data=patch)
    assert sorted(git("diff", "--cached", "--name-only").splitlines()) == sorted(paths)
    git("diff", "--cached", "--check")
    git("commit", "-m", message)
    commit = git("rev-parse", "HEAD")
    assert git("rev-parse", "HEAD^") == BASE
    git("push", "origin", commit + ":refs/heads/" + branch)
    results[str(issue)] = {"commit": commit, "tree": git("rev-parse", "HEAD^{tree}"), "base": BASE, "branch": branch, "patch_sha256": checksum}
pathlib.Path("staged-fixes.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
