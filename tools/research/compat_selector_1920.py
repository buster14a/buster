#!/usr/bin/env python3
"""#1920 diagnostic only: run original CLI/selectors with inert path sentinels."""
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

BASE = "7afde682de1beb595c779192c9519b7524afb412"
BUILD_BLOB = "5a86d337a46666dec017ece3e8e02bed37c4831d"
COMMANDS = {
    "cjson": ("Cjson", "cjson"), "stb": ("Stb", "cjson"),
    "zlib": ("Zlib", "zlib"), "lua": ("Lua", "zlib"),
    "yyjson": ("Yyjson", "yyjson"), "lz4": ("Lz4", "compat"),
    "sqlite": ("Sqlite", "compat"), "sbase": ("Sbase", "compat"),
    "cpython": ("Cpython", "compat"), "doom": ("Doom", "doom"),
    "quickjs": ("Quickjs", "quickjs"), "musl": ("Musl", "musl"),
}
repo = Path.cwd()
out = repo / "selector-1920-evidence"
out.mkdir()
original = (repo / "build.c").read_bytes()
blob = hashlib.sha1(b"blob " + str(len(original)).encode() + b"\0" + original).hexdigest()
assert blob == BUILD_BLOB, (blob, BUILD_BLOB)
text = original.decode()
instrumented = text
for command, (typename, helper) in COMMANDS.items():
    prefix = "    ProcessResult action_result = PROCESS_RESULT_FAILED;\n" if command == "stb" else ""
    anchor = (f"BUSTER_GLOBAL_LOCAL ProcessResult test_{command}_action(Arena* arena, void* data)\n"
              "{\n" + prefix + f"    Test{typename}Options options = *(Test{typename}Options*)data;")
    assert instrumented.count(anchor) == 1, command
    insert = f"""
    /* Research-only early observation: no external input/compiler is run. */
    if (getenv("BUSTER_RESEARCH_SELECTOR_ONLY"))
    {{
        String8 selected = {helper}_ide_path(arena, options.config);
        string_print(S8("RESEARCH_SELECTOR_ONLY command={command} requested={{S8}} selected={{S8}}\\n"), options.config, selected);
        return PROCESS_RESULT_SUCCESS;
    }}"""
    instrumented = instrumented.replace(anchor, anchor + insert, 1)

def function_body(source, name):
    pattern = re.compile(r"^BUSTER_GLOBAL_LOCAL [^\n]*\b" + re.escape(name) + r"\([^\n]*\)\n\{", re.M)
    match = pattern.search(source)
    assert match, name
    # Selected functions contain no brace-bearing strings or comments.
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]

unchanged = sorted({helper + "_ide_path" for _, helper in COMMANDS.values()} | {"path_exists", "path_join"})
for name in unchanged:
    assert function_body(text, name) == function_body(instrumented, name), name
instrument_path = repo / "research_selector_driver_1920.c"
instrument_path.write_text(instrumented)
driver = out / "driver"
compile_argv = ["clang", "-Isrc", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                "-fwrapv", "-fno-strict-aliasing", "-funsigned-char", str(instrument_path), "-o", str(driver)]
version = subprocess.run(["clang", "--version"], capture_output=True, text=True, check=True).stdout
compile_run = subprocess.run(compile_argv, capture_output=True, text=True, timeout=180)
(out / "compile.log").write_text(compile_run.stdout + compile_run.stderr)
assert compile_run.returncode == 0, compile_run.stderr[-8000:]
rows = []
marker = re.compile(r"^RESEARCH_SELECTOR_ONLY command=(\w+) requested=(.*?) selected=(.*?)$", re.M)
# Explicitly tabulated independent expected baseline behavior: mask bits R,D,flat.
expected = {
    0: ("", "", ""),
    1: ("build/Release/ide", "build/Release/ide", "build/Release/ide"),
    2: ("build/Debug/ide", "build/Debug/ide", "build/Debug/ide"),
    3: ("build/Release/ide", "build/Debug/ide", "build/Release/ide"),
    4: ("build/ide", "build/ide", "build/ide"),
    5: ("build/Release/ide", "build/Release/ide", "build/Release/ide"),
    6: ("build/Debug/ide", "build/Debug/ide", "build/Debug/ide"),
    7: ("build/Release/ide", "build/Debug/ide", "build/Release/ide"),
}
paths = ["build/Release/ide", "build/Debug/ide", "build/ide"]
configs = ["", "Debug", "Release"]
raw = (out / "raw.jsonl").open("w")

def invoke(root, command, args, expect, config, case, env_on=True, bad=False):
    positional = ["unused external source", "unused external tests"] if command in ("lua", "sqlite", "doom") else ["unused external source"]
    argv = [str(driver), "test_" + command, *args, *positional]
    env = dict(os.environ)
    env.pop("BUSTER_RESEARCH_SELECTOR_ONLY", None)
    if env_on:
        env["BUSTER_RESEARCH_SELECTOR_ONLY"] = "1"
    run = subprocess.run(argv, cwd=root, env=env, capture_output=True, text=True, timeout=15)
    matches = marker.findall(run.stdout)
    record = dict(case=case, command=command, argv=argv, requested=config, expected=expect,
                  exit=run.returncode, stdout=run.stdout, stderr=run.stderr, markers=matches)
    raw.write(json.dumps(record) + "\n")
    raw.flush()
    if bad:
        assert run.returncode != 0 and not matches, record
    else:
        assert run.returncode == 0 and matches == [(command, config, expect)], record
    rows.append(dict(case=case, command=command, requested=config, selected=expect, negative_control=bad))

with tempfile.TemporaryDirectory(prefix="buster selector 1920 ") as tmp:
    temp = Path(tmp)
    for mask, config_index in itertools.product(range(8), range(3)):
        root = temp / f"mask-{mask}-config-{config_index}"
        root.mkdir()
        for bit, path in enumerate(paths):
            if mask & (1 << bit):
                p = root / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text("INERT NONEXECUTABLE PATH SENTINEL\n")
                p.chmod(0o600)
        config = configs[config_index]
        for command in COMMANDS:
            invoke(root, command, ["--config", config] if config else [],
                   expected[mask][config_index], config, f"mask-{mask}")

    for present_default in (False, True):
        root = temp / f"alternate-default-{present_default}"
        root.mkdir()
        for conf in ("Debug", "Release"):
            p = root / "alternate tree" / conf / "ide"
            p.parent.mkdir(parents=True)
            p.write_text("ALTERNATE TREE SENTINEL\n")
            p.chmod(0o600)
            if present_default:
                q = root / "build" / conf / "ide"
                q.parent.mkdir(parents=True)
                q.write_text("DEFAULT TREE SENTINEL\n")
                q.chmod(0o600)
        for conf, spelling in itertools.product(("Debug", "Release"),
                                                ("--build-dir", "--build-directory")):
            for command in COMMANDS:
                invoke(root, command, [spelling, "alternate tree", "--config", conf],
                       f"build/{conf}/ide" if present_default else "", conf,
                       f"alternate-{present_default}-{spelling}")

    root = temp / "negative"
    root.mkdir()
    for command in COMMANDS:
        invoke(root, command, ["--config", "DefinitelyInvalid"], "", "",
               "invalid-config", bad=True)
        # No positional operand: exercise missing option value, not directory existence.
        argv = [str(driver), "test_" + command, "--build-dir"]
        run = subprocess.run(argv, cwd=root, env={**os.environ, "BUSTER_RESEARCH_SELECTOR_ONLY": "1"},
                             capture_output=True, text=True, timeout=15)
        record = dict(case="missing-build-dir", command=command, argv=argv,
                      exit=run.returncode, stdout=run.stdout, stderr=run.stderr)
        raw.write(json.dumps(record) + "\n")
        assert run.returncode != 0 and not marker.findall(run.stdout), record
        rows.append(dict(case="missing-build-dir", command=command, negative_control=True))
    invoke(root, "cjson", [], "", "", "instrumentation-disabled", env_on=False, bad=True)
raw.close()
cross_config = [r for r in rows if r.get("requested") and r.get("selected") in
                ("build/Debug/ide", "build/Release/ide") and
                r["selected"] != "build/" + r["requested"] + "/ide"]
flat_unverified = [r for r in rows if r.get("requested") and r.get("selected") == "build/ide"]
assert len(rows) == 409 and len(cross_config) == 48 and len(flat_unverified) == 24
summary = dict(source_commit=BASE, source_blob=blob, source_sha256=hashlib.sha256(original).hexdigest(),
               instrumentation_count=12, unchanged_functions=unchanged, compile_argv=compile_argv,
               compiler_version=version, driver_sha256=hashlib.sha256(driver.read_bytes()).hexdigest(),
               observation_count=len(rows), matrix_cells=288, alternate_root_cells=96,
               cross_config_substitutions=len(cross_config), explicit_flat_unknown=len(flat_unverified),
               evidence_class="instrumented real CLI and unchanged selectors; inert files; no compiler or project execution",
               rows=rows)
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
(out / "instrumented-build.c").write_text(instrumented)
print(json.dumps({k:v for k,v in summary.items() if k != "rows"}, indent=2))
