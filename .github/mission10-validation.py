"""Temporary hosted-only Mission 10 driver; never included in the source PR."""
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zlib

BASE = "bb96b5d08e570aff3cbd473441d71ab0dc691d67"
BASE_TREE = "8247940875e8265e4e5f2e764b992a3c8e25ae80"
PATCH_HASH = "648c845cd1979acaf432a6d3d7c9516dadb4a165a3f83928ba7f73870b3702bb"
FILES = ["src/buster/lib/compiler/frontend/c/c_gen.c", "src/buster/lib/compiler/frontend/c/c_parse.c", "src/buster/tests/compiler/driver/driver_test.c", "docs/agents/frontend.md"]
E = Path(os.environ["RUNNER_TEMP"]) / "mission10-validation"
E.mkdir(exist_ok=True)
PRIOR = Path(os.environ["RUNNER_TEMP"]) / "mission10-prior"
DRIVER = str(Path(os.environ["RUNNER_TEMP"]) / "mission10-driver")

def run(name, args, expected=None, timeout=1800):
    args = [str(x) for x in args]
    (E / (name + ".command.json")).write_text(json.dumps(args) + "\n")
    with (E / (name + ".log")).open("wb") as log:
        try:
            code = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=timeout, check=False).returncode
        except subprocess.TimeoutExpired:
            code = 124
    with (E / "status.tsv").open("a") as status:
        status.write(f"{name}\t{code}\n")
    print(f"{name}: {code}", flush=True)
    if expected is not None and code != expected:
        print((E / (name + ".log")).read_text(errors="replace")[-12000:], flush=True)
        raise RuntimeError(f"{name}: expected {expected}, observed {code}")
    return code

def identity(label):
    run(label + "-identity", ["git", "rev-parse", "HEAD", "HEAD^{tree}"], 0)
    if Path("build/Release/ide").exists():
        (E / (label + "-compiler.sha256")).write_text(hashlib.sha256(Path("build/Release/ide").read_bytes()).hexdigest() + "\n")

def native_probes(label, require_success):
    for mode in ("none", "mir-stack", "fast", "quality"):
        for form in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            flags = ["-std=gnu11", f"-fregister-allocator={mode}", "-fverify-codegen", form]
            if mode != "none":
                flags.append("-fno-machine-fallback")
            for runtime in (0, 1):
                name = f"{label}-{mode}-{form}-runtime{runtime}"
                binary = E / name
                code = run(name + "-compile", ["build/Release/ide", "cc", *flags, f"-DRUNTIME={runtime}", E / "packet.c", "-o", binary], 0 if require_success else None)
                if code == 0:
                    run(name + "-execute", [binary], 0 if require_success else None, timeout=30)
            name = f"{label}-{mode}-{form}-result-type"
            binary = E / name
            code = run(name + "-compile", ["build/Release/ide", "cc", *flags, E / "result-type.c", "-o", binary], 0 if require_success else None)
            if code == 0:
                run(name + "-execute", [binary], 0 if require_success else None, timeout=30)
    for case in ("invalid-offset", "invalid-alignment"):
        for form in ("-ffrontend-ssa", "-fno-frontend-ssa"):
            for action in ("object", "syntax"):
                flags = ["-c", "-o", E / f"{label}-{case}-{form}.o"] if action == "object" else ["-fsyntax-only"]
                run(f"{label}-{case}-{form}-{action}", ["build/Release/ide", "cc", form, *flags, E / (case + ".c")], 1 if require_success else None)

phase = sys.argv[1]
if phase == "prepare":
    encoded = Path(sys.argv[2]).read_text()
    # Correct transcription damage in the temporary transport only. The
    # decoded patch must still match the separately fixed source SHA-256.
    encoded = encoded.replace("nhOn6gTxV78", "nhOn6gQxV78").replace("AJATATYui", "AJATYui").replace("qQf75Jhyw", "qQf75hyw")
    patch = zlib.decompress(base64.b64decode(encoded, validate=False))
    if hashlib.sha256(patch).hexdigest() != PATCH_HASH:
        raise RuntimeError("prototype transport checksum mismatch")
    (E / "prototype-1.patch").write_bytes(patch)
    for name in ("packet.c", "result-type.c", "invalid-offset.c", "invalid-alignment.c"):
        shutil.copyfile(PRIOR / name, E / name)
    (E / "inputs.sha256.json").write_text(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in E.glob("*.c")}, indent=2) + "\n")
    if subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() != BASE:
        raise RuntimeError("unexpected baseline head")
    if subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], text=True).strip() != BASE_TREE:
        raise RuntimeError("unexpected baseline tree")
    run("patch-preflight", ["git", "apply", "--check", E / "prototype-1.patch"], 0)
    for cc in ("clang", "gcc"):
        run(cc + "-version", [cc, "--version"], 0)
        for opt in (0, 2):
            for runtime in (0, 1):
                name = f"{cc}-O{opt}-runtime{runtime}"
                binary = E / name
                run(name + "-compile", [cc, "-std=gnu11", f"-O{opt}", f"-DRUNTIME={runtime}", E / "packet.c", "-o", binary], 0)
                run(name + "-execute", [binary], 0, timeout=30)
            name = f"{cc}-O{opt}-result-type"
            binary = E / name
            run(name + "-compile", [cc, "-std=gnu11", f"-O{opt}", E / "result-type.c", "-o", binary], 0)
            run(name + "-execute", [binary], 0, timeout=30)
        run(cc + "-invalid-offset", [cc, "-std=gnu11", "-Wall", "-Werror", "-c", E / "invalid-offset.c", "-o", E / f"{cc}-invalid-offset.o"], 1)
        run(cc + "-invalid-alignment", [cc, "-std=gnu11", "-Wall", "-Werror", "-c", E / "invalid-alignment.c", "-o", E / f"{cc}-invalid-alignment.o"], 1 if cc == "clang" else 0)
elif phase == "baseline":
    run("build-driver", ["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", "build.c", "-o", DRIVER], 0)
    run("configure", [DRIVER, "generate", "--cc", "clang", "--config", "Release", "--linker", "DEFAULT", "--", "-DBUSTER_DEBUG_INFO=OFF", "-DBUSTER_UNITY_BUILD=OFF"], 0)
    run("baseline-build", [DRIVER, "build", "--config", "Release", "-t", "ide"], 0)
    identity("baseline")
    native_probes("baseline", False)
    run("baseline-self-host", [DRIVER, "test_self_host", "--config", "Release"], 0)
elif phase == "apply":
    run("apply", ["git", "apply", E / "prototype-1.patch"], 0)
    run("diff-check", ["git", "diff", "--check"], 0)
    names = subprocess.check_output(["git", "diff", "--name-only"], text=True).splitlines()
    if set(names) != set(FILES):
        raise RuntimeError("source scope changed")
    run("source-diff", ["git", "diff", "--binary", BASE], 0)
    run("stage-source", ["git", "add", "--", *FILES], 0)
    tree = subprocess.check_output(["git", "write-tree"], text=True).strip()
    (E / "candidate-tree.txt").write_text(tree + "\n")
    (E / "candidate-blobs.json").write_text(json.dumps({p: {"git": subprocess.check_output(["git", "hash-object", p], text=True).strip(), "sha256": hashlib.sha256(Path(p).read_bytes()).hexdigest()} for p in FILES}, indent=2) + "\n")
elif phase == "candidate":
    run("candidate-build", [DRIVER, "build", "--config", "Release", "-t", "ide"], 0)
    identity("candidate-base-with-staged-patch")
    native_probes("candidate", True)
elif phase == "suite":
    run("candidate-test-all", [DRIVER, "build", "--config", "Release", "-t", "test_all"], 0, timeout=3600)
elif phase == "self-host":
    run("candidate-self-host", [DRIVER, "test_self_host", "--config", "Release"], 0)
elif phase == "publish-objects":
    if not (E / "candidate-tree.txt").exists():
        raise RuntimeError("no candidate source tree")
    # Publish source objects only. No branch/ref or PR is written by this job.
    # The connected integration separately reviews and publishes the exact head.
    token = os.environ["GH_TOKEN"]
    endpoint = "https://api.github.com/repos/buster14a/buster/git/"
    def post(route, body):
        request = urllib.request.Request(endpoint + route, data=json.dumps(body).encode(), method="POST", headers={"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json", "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    entries = []
    for path in FILES:
        blob = post("blobs", {"content": base64.b64encode(Path(path).read_bytes()).decode(), "encoding": "base64"})
        expected = subprocess.check_output(["git", "hash-object", path], text=True).strip()
        if blob["sha"] != expected:
            raise RuntimeError("source blob identity mismatch")
        entries.append({"path": path, "mode": "100644", "type": "blob", "sha": blob["sha"]})
    tree = post("trees", {"base_tree": BASE_TREE, "tree": entries})
    if tree["sha"] != (E / "candidate-tree.txt").read_text().strip():
        raise RuntimeError("source tree identity mismatch")
    commit = post("commits", {"message": "c: evaluate assume-aligned offsets and preserve void-pointer semantics", "tree": tree["sha"], "parents": [BASE]})
    (E / "publication.json").write_text(json.dumps(commit, indent=2) + "\n")
    print("SOURCE_COMMIT=" + commit["sha"], flush=True)
    print("SOURCE_TREE=" + tree["sha"], flush=True)
else:
    raise RuntimeError("unknown phase")
