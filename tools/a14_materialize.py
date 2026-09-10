#!/usr/bin/env python3
"""One-session A14 source materialization and CI evidence; not a product file."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import textwrap
import unittest
import urllib.request

BASE = "ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae"
ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "source"
OUT = ROOT / "evidence"
OUT.mkdir(exist_ok=True)
API = "https://api.github.com/repos/buster14a/buster/"
EXPECTED = {
    ".github/workflows/ios-monitor-tests.yml": "922bde08c7dbc42d401f900d254eb8019a38ba7c",
    "CMakeLists.txt": "f5ebc5e09ce3c001f2e3a82d72fb719211716f19",
    "cmake/AndroidApk.cmake": "6f2f931f571f421038504022ffba95d161031a7d",
    "docs/agents/build.md": "2b154a3b1052edb9a86ebb950967c9cf67b22677",
    "tests/android_apk_assets_test.py": "a885558aa4852220381eb1b9e3e972e18285c9c3",
    "tests/mobile_ci_scripts_test.sh": "2a9b96d8043c63ac82fa38395b0d3584bc3444d4",
}


def api(path, data=None):
    payload = None if data is None else json.dumps(data).encode()
    request = urllib.request.Request(API + path, data=payload, headers={
        "Authorization": "Bearer " + os.environ["GH_TOKEN"],
        "Accept": "application/vnd.github+json", "Content-Type": "application/json",
        "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "buster-a14-ci",
    })
    with urllib.request.urlopen(request, timeout=60) as response:
        value = json.load(response)
    return value


def pages(path):
    result = []
    page = 1
    while True:
        batch = api(path + ("&" if "?" in path else "?") + f"per_page=100&page={page}")
        assert isinstance(batch, list)
        if not batch:
            break
        result.extend(batch)
        page += 1
    return result


def git(*args):
    return subprocess.check_output(["git", "-C", str(WORK), *args], text=True).strip()


assert git("rev-parse", "HEAD") == BASE
assert api("branches/main")["commit"]["sha"] == BASE, "main moved: reconcile before publishing"
issues = pages("issues?state=open")
pulls = pages("pulls?state=open")
files = {str(p["number"]): pages(f"pulls/{p['number']}/files") for p in pulls}
(OUT / "inventory.json").write_text(json.dumps({"issues": issues, "pulls": pulls, "files": files}, indent=2))
print("Open issues:", sum("pull_request" not in issue for issue in issues), flush=True)
print("Open PRs:", [p["number"] for p in pulls], flush=True)
for number, entries in files.items():
    shared = [entry["filename"] for entry in entries if entry["filename"] in EXPECTED]
    if shared:
        print("Shared files:", number, shared, flush=True)

path = WORK / "CMakeLists.txt"
text = path.read_text()
start = text.index('    add_custom_command(\n        OUTPUT "${BUSTER_APK}"')
marker = '    add_custom_target(apk DEPENDS "${BUSTER_APK}")'
end = text.index(marker, start) + len(marker)
(OUT / "baseline-apk.cmake").write_text(textwrap.dedent(text[start:end]) + "\n")
path.write_text(text[:start] + "    include(cmake/AndroidApk.cmake)" + text[end:])
for name in ("cmake/AndroidApk.cmake", "tests/android_apk_assets_test.py"):
    (WORK / name).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / name, WORK / name)
path = WORK / ".github/workflows/ios-monitor-tests.yml"
text = path.read_text()
assert text.count("      - 'android/**'") == 2
path.write_text(text.replace("      - 'android/**'", "      - 'android/**'\n      - 'CMakeLists.txt'\n      - 'cmake/AndroidApk.cmake'"))
path = WORK / "tests/mobile_ci_scripts_test.sh"
text = path.read_text()
marker = 'fake_tool="$repo_root/tests/mobile_ci_fake_tool.sh"'
assert text.count(marker) == 1
path.write_text(text.replace(marker, '# Exercise the real APK dependency graph before introducing fake CMake/Ninja.\npython3 "$repo_root/tests/android_apk_assets_test.py"\n\n' + marker))
path = WORK / "docs/agents/build.md"
path.write_text(path.read_text() + '''
## Incremental Android test assets

The Android `apk` graph in `cmake/AndroidApk.cmake` treats the active files under
`tests/` as package inputs, not compiler sources. After configuring the normal
Android tree, `cmake --build build/android-ci-x86_64 --config Debug --target apk`
repackages fixture edits, additions, renames and deletions without requiring a
C source rebuild or another destructive `generate`. Release uses the same
contract. An unchanged build does not repackage. A content-stable inventory
handles removals and newly added files whose timestamps predate the APK;
`CONFIGURE_DEPENDS` refreshes that inventory with the supported Ninja generators.
The preserved `.bbb` corpus is excluded before Android asset packaging.

`python3 tests/android_apk_assets_test.py` exercises that same production graph
through real CMake and Ninja Multi-Config, in Debug and Release, with isolated
fixtures and controlled packaging-tool stand-ins. It checks package contents,
fixture-only invalidation, stale-file removal, no-op behavior, and the retained
native-library/manifest/sign-script dependencies. It requires the existing
CMake, Ninja, Python and Bash prerequisites, not an Android SDK or a compiler.
The existing `tests/mobile_ci_scripts_test.sh` suite and mobile lifecycle CI run
it on Linux and macOS. This is build-graph evidence, not Android compilation,
signing or device execution; those remain the regular Android mobile CI gates.
Do not run two configurations' packaging concurrently in one build directory:
the existing APK and staging paths are shared.
''')
for name, expected in EXPECTED.items():
    assert git("hash-object", name) == expected, name
subprocess.run(["git", "-C", str(WORK), "add", *EXPECTED], check=True)
subprocess.run(["git", "-C", str(WORK), "diff", "--cached", "--check"], check=True)
assert git("write-tree") == "91effa8c7f40681d914dc9a13b37a89b5d8aa727"
(OUT / "product.patch").write_text(git("diff", "--cached") + "\n")

os.environ["BUSTER_ANDROID_APK_GRAPH"] = str(OUT / "baseline-apk.cmake")
spec = importlib.util.spec_from_file_location("apk_baseline", WORK / "tests/android_apk_assets_test.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with (OUT / "baseline.log").open("w") as stream:
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(module))
expected_failures = {f"apk_baseline.{configuration}ApkTests.{name}" for configuration in ("Debug", "Release") for name in (
    "test_initial_contents_and_noop", "test_fixture_edit", "test_old_timestamp_addition", "test_removal",
    "test_rename", "test_dormant_edit_is_not_a_package_input", "test_empty_active_inventory")}
actual_failures = {case.id() for case, trace in result.failures}
print("Baseline cases:", result.testsRun, "failures:", sorted(actual_failures), "errors:", len(result.errors), flush=True)
assert result.testsRun == 16 and not result.errors and actual_failures == expected_failures, "unexpected baseline result; inspect baseline.log"
os.environ.pop("BUSTER_ANDROID_APK_GRAPH")
with (OUT / "candidate.log").open("w") as stream:
    subprocess.run([sys.executable, "tests/android_apk_assets_test.py"], cwd=WORK, stdout=stream,
                   stderr=subprocess.STDOUT, check=True, timeout=180)
print((OUT / "candidate.log").read_text(), flush=True)
assert api("branches/main")["commit"]["sha"] == BASE, "main moved: reconcile before publishing"
entries = []
for name, expected in EXPECTED.items():
    blob = api("git/blobs", {"content": (WORK / name).read_text(), "encoding": "utf-8"})
    assert blob["sha"] == expected
    entries.append({"path": name, "mode": "100755" if name.endswith(".sh") else "100644", "type": "blob", "sha": expected})
(OUT / "blobs.json").write_text(json.dumps({"base": BASE, "tree": git("write-tree"), "entries": entries}, indent=2))
print("Materialized product tree:", git("write-tree"), "(no product branch or PR written by this workflow)", flush=True)
