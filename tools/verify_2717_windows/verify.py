"""Temporary #2717 Windows debugger verification (hosted session only).

Compiles dbg5/dbg6/dbg7 with Buster `ide cc -g` in three modes plus clang-cl
and MSVC references, then reads locals at fixed source lines with cdb and lldb.
Writes results.md / results.json under the output directory. Not production.
"""
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
U = "UNAVAILABLE"

# Ordered breakpoint hits per fixture: (line, {variable: expected}).
EXPECT = {
    "dbg5": [(11, {"a": "6", "b": "3", "c": "103", "d": "5", "e": "106", "w": "3298534883328"})],
    "dbg6": [(6, {"once": "6", "twice": "9"}), (8, {"once": "6", "twice": "10"})],
    "dbg7": [
        (16, {"both": "4", "one": U}),
        (21, {"both": "4", "i": "0", "t": "1", "total": "0"}),
        (21, {"both": "4", "i": "1", "t": "8", "total": "1"}),
        (21, {"both": "4", "i": "2", "t": "15", "total": "9"}),
        (24, {"both": "4", "total": "24"}),
    ],
}
BUSTER_MODES = [("default", []), ("nossa", ["-fno-frontend-ssa"]), ("quality", ["-fregister-allocator=quality"])]


def run(cmd, log, stdin=None, timeout=300):
    with open(log, "a", encoding="utf-8") as f:
        f.write("\n$ " + " ".join(cmd) + "\n")
        try:
            p = subprocess.run(cmd, input=stdin, capture_output=True, text=True, timeout=timeout, errors="replace")
            out = p.stdout + p.stderr
            code = p.returncode
        except subprocess.TimeoutExpired as e:
            out = (e.stdout or "") if isinstance(e.stdout, str) else ""
            out += "\n<TIMEOUT>\n"
            code = -999
        except OSError as e:
            out = "<OSERROR %s>" % e
            code = -998
        f.write(out + "\n[exit %d]\n" % code)
    return code, out


def norm(value):
    v = value.strip()
    if "unavailable" in v.lower() or "not available" in v.lower() or "optimized" in v.lower():
        return U
    m = re.match(r"^(?:0n)?(-?\d+)\b", v)
    if m:
        return m.group(1)
    m = re.match(r"^0x([0-9a-fA-F`]+)\b", v)
    if m:
        return str(int(m.group(1).replace("`", ""), 16))
    return v


def parse_cdb(out):
    hits = []
    current = None
    for line in out.splitlines():
        m = re.search(r"HIT2717 (\d+)", line)
        if m and ".echo" not in line:
            current = {"line": int(m.group(1)), "vars": {}, "raw": []}
            hits.append(current)
            continue
        if current is None:
            continue
        m = re.match(r"^\s*(?:prv\s+\w+\s+)?(\S+)\s+(.+?)\s+(\w+) = (.*)$", line)
        if m:
            current["vars"][m.group(3)] = norm(m.group(4))
            current["raw"].append(line.strip())
    return hits


def parse_lldb(out):
    hits = []
    current = None
    for line in out.splitlines():
        m = re.search(r"frame #0:.*?\.c:(\d+)", line)
        if m:
            current = {"line": int(m.group(1)), "vars": {}, "raw": []}
            hits.append(current)
            continue
        if current is None:
            continue
        m = re.match(r"^\s*\((.+?)\) (\w+) = (.*)$", line)
        if m:
            current["vars"][m.group(2)] = norm(m.group(3))
            current["raw"].append(line.strip())
        m = re.match(r"^\s*\((.+?)\) (\w+) = $", line)
    return hits


def find_cdb():
    arch = "arm64" if os.environ.get("VERIFY_ARCH") == "aarch64" else "x64"
    roots = [os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), os.environ.get("ProgramFiles", r"C:\Program Files")]
    for root in roots:
        p = os.path.join(root, "Windows Kits", "10", "Debuggers", arch, "cdb.exe")
        if os.path.exists(p):
            return p
    return shutil.which("cdb")


def find_lldb(log):
    candidates = [os.path.join(os.environ.get("BUSTER_CI_LLVM_BIN", ""), "lldb.exe"), r"C:\Program Files\LLVM\bin\lldb.exe"]
    found = None
    for lldb in candidates:
        if found or not os.path.exists(lldb):
            continue
        # A release lldb links a specific python3XY.dll; put a matching hosted Python on PATH.
        code, out = run(["dumpbin", "/dependents", os.path.join(os.path.dirname(lldb), "liblldb.dll")], log)
        m = re.search(r"python3(\d+)\.dll", out, re.I)
        root = r"C:\hostedtoolcache\windows\Python"
        if m and os.path.isdir(root):
            minor = m.group(1)
            for entry in sorted(os.listdir(root)):
                for arch in ("arm64", "x64"):
                    d = os.path.join(root, entry, arch)
                    if entry.startswith("3.%s." % minor) and os.path.exists(os.path.join(d, "python3%s.dll" % minor)):
                        os.environ["PATH"] = d + os.pathsep + os.environ["PATH"]
                        os.environ["PYTHONHOME"] = d
        code, _ = run([lldb, "--version"], log)
        found = lldb if code == 0 else None
    return found


def debug_cdb(cdb, exe, fixture, out_dir, log):
    module = os.path.splitext(os.path.basename(exe))[0]
    lines = sorted({line for line, _ in EXPECT[fixture]})
    script = [".lines -e", ".symopt+0x40", "!sym noisy", ".reload /f /v %s.exe" % module, "ld %s" % module, "lm v m %s" % module, "!lmi %s" % module, "!sym quiet", "x %s!f" % module]
    probe_script = script + ["bp `%s!%s.c:%d`" % (module, fixture, line) for line in lines] + ["q"]
    code, out = run([cdb, "-lines", "-y", out_dir, "-srcpath", HERE, exe], log, stdin="\n".join(probe_script) + "\n", timeout=180)
    # Several C13 rows for one line make a source breakpoint ambiguous (#1440);
    # break at the lowest matched address of each line instead.
    targets = {}
    pending = []
    for text in out.splitlines():
        for m in re.finditer(r"Matched: \S+ \(([0-9a-f`]+)\)", text):
            pending.append(int(m.group(1).replace("`", ""), 16))
        m = re.search(r"Ambiguous symbol error at '`[^`]*:(\d+)`", text)
        if m and pending:
            targets[int(m.group(1))] = min(pending)
            pending = []
    for line in lines:
        target = ("0x%x" % targets[line]) if line in targets else "`%s!%s.c:%d`" % (module, fixture, line)
        script.append('bp %s ".echo HIT2717 %d; dv /t /v; g"' % (target, line))
    script += ["bl", "g", "q"]
    env = dict(os.environ)
    env["_NT_SYMBOL_PATH"] = out_dir
    cmd = [cdb, "-lines", "-G", "-y", out_dir, "-srcpath", HERE, exe]
    with open(log, "a", encoding="utf-8") as f:
        f.write("\n# cdb script\n" + "\n".join(script) + "\n")
    code, out = run(cmd, log, stdin="\n".join(script) + "\n", timeout=180)
    return parse_cdb(out)


def debug_lldb(lldb, exe, fixture, log):
    hits = EXPECT[fixture]
    cmd = [lldb, "-b", "-o", "settings set target.load-cwd-lldbinit false"]
    for line in sorted({line for line, _ in hits}):
        cmd += ["-o", "breakpoint set -f %s.c -l %d" % (fixture, line)]
    cmd += ["-o", "run"]
    for _ in hits:
        cmd += ["-o", "frame info", "-o", "frame variable", "-o", "continue"]
    cmd += [exe]
    code, out = run(cmd, log, timeout=180)
    return parse_lldb(out)


def roundtrip_cell(cdb, fixture, producer, exe, out_dir, log):
    """Rewrite the PDB container with llvm-pdbutil (same CodeView records) and read it with cdb."""
    cell = {"fixture": fixture, "producer": producer + "+llvm-pdb-container", "compile_exit": 0, "run_exit": 0}
    pdb = os.path.splitext(exe)[0] + ".pdb"
    rt_dir = os.path.join(out_dir, "rt")
    os.makedirs(rt_dir, exist_ok=True)
    name = os.path.basename(pdb)
    yaml = os.path.join(rt_dir, name + ".yaml")
    code, out = run(["llvm-pdbutil", "pdb2yaml", "-all", pdb], log)
    open(yaml, "w", encoding="utf-8").write(out if code == 0 else "")
    new_pdb = os.path.join(rt_dir, name)
    code2, _ = run(["llvm-pdbutil", "yaml2pdb", "-pdb", new_pdb, yaml], log)
    if code or code2:
        cell["compile_exit"] = code or code2
        return cell
    hidden = pdb + ".hidden"
    os.replace(pdb, hidden)
    try:
        hits = debug_cdb(cdb, exe, fixture, rt_dir, log)
    finally:
        os.replace(hidden, pdb)
    cell["cdb"] = {"hits": hits, "rows": compare(producer, fixture, hits)}
    cell["lldb"] = "not run"
    return cell


def probe(cdb, out_dir, log, results):
    """Why does dbghelp reject Buster PDBs? Tools plus timestamp/signature variants."""
    exe = os.path.join(out_dir, "dbg6_buster_default.exe")
    pdb = os.path.join(out_dir, "dbg6_buster_default.pdb")
    if not cdb or not os.path.exists(exe):
        return
    debuggers = os.path.dirname(cdb)
    probe_log = os.path.join(out_dir, "probe.log")
    run([os.path.join(debuggers, "symchk.exe"), "/v", exe, "/s", out_dir], probe_log)
    run([os.path.join(debuggers, "symchk.exe"), "/v", pdb], probe_log)
    run([os.path.join(debuggers, "dbh.exe"), "-v", pdb, "info"], probe_log)
    run([os.path.join(debuggers, "dbh.exe"), "-v", pdb, "enum", "*"], probe_log)
    run([os.path.join(debuggers, "dbh.exe"), "-v", exe, "info"], probe_log)
    run(["dumpbin", "/headers", "/section:.rdata", exe], probe_log)
    run(["llvm-pdbutil", "dump", "-summary", "-streams", "-modules", pdb], probe_log)
    code, out = run([sys.executable, os.path.join(HERE, "pdb_variants.py"), pdb, os.path.join(out_dir, "variants"), os.path.join(out_dir, "rt", os.path.basename(pdb))], probe_log)
    results["variants"] = {}
    for variant_pdb in [line.strip() for line in out.splitlines() if line.strip().endswith(".pdb")]:
        tag = os.path.basename(os.path.dirname(variant_pdb))
        code, text = run([os.path.join(debuggers, "dbh.exe"), "-v", variant_pdb, "info"], probe_log)
        results["variants"][tag] = "dbh-fault" if "0xc0000005" in text else "dbh-ok"
    code, out = run([sys.executable, os.path.join(HERE, "patch_probe.py"), exe, os.path.join(out_dir, "probe")], probe_log)
    variants = [line.strip() for line in out.splitlines() if line.strip().endswith(".exe")]
    # Hide the original so dbghelp resolves the variant's PDB through -y only.
    hidden = pdb + ".hidden"
    os.replace(pdb, hidden)
    results["probe"] = {}
    for variant in variants:
        hits = debug_cdb(cdb, variant, "dbg6", os.path.dirname(variant), probe_log)
        results["probe"][os.path.basename(os.path.dirname(variant))] = hits
    os.replace(hidden, pdb)


def compare(producer, fixture, hits):
    rows = []
    expected = EXPECT[fixture]
    for index, (line, want) in enumerate(expected):
        got = hits[index] if index < len(hits) else None
        for var, value in want.items():
            if got is None:
                obs, verdict = "NO HIT", "FAIL"
            elif got["line"] != line:
                obs, verdict = "hit line %d" % got["line"], "FAIL"
            else:
                obs = got["vars"].get(var, "MISSING")
                if obs == value:
                    verdict = "ok" if value != U else "ok (intended unavailable)"
                elif value == U:
                    verdict = "note (readable; Buster design expects unavailable)" if producer.startswith("buster") else "ref-readable"
                else:
                    verdict = "FAIL"
            rows.append({"line": line, "hit": index, "var": var, "expected": value, "observed": obs, "verdict": verdict})
    return rows


def main():
    ide, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    log = os.path.join(out_dir, "session.log")
    cdb = find_cdb()
    lldb = find_lldb(log)
    results = {"cdb": cdb, "lldb": lldb, "cells": []}
    run([ide, "--version"], log)
    if cdb:
        run([cdb, "-version"], log, stdin="q\n")
    if lldb:
        run([lldb, "--version"], log)
    for fixture in EXPECT:
        src = os.path.join(HERE, fixture + ".c")
        producers = []
        for mode, flags in BUSTER_MODES:
            exe = os.path.join(out_dir, "%s_buster_%s.exe" % (fixture, mode))
            code, _ = run([ide, "cc", "-g"] + flags + [src, "-o", exe], log)
            producers.append(("buster-" + mode, exe, code))
        exe = os.path.join(out_dir, "%s_clangcl.exe" % fixture)
        code, _ = run(["clang-cl", "/nologo", "/Od", "/Zi", src, "/Fe" + exe, "/Fd" + os.path.join(out_dir, fixture + "_clangcl_obj.pdb"), "-fuse-ld=lld", "/link", "/DEBUG:FULL"], log)
        producers.append(("clang-cl", exe, code))
        exe = os.path.join(out_dir, "%s_msvc.exe" % fixture)
        code, _ = run(["cl", "/nologo", "/Od", "/Zi", src, "/Fe" + exe, "/Fd" + os.path.join(out_dir, fixture + "_msvc_obj.pdb"), "/Fo" + os.path.join(out_dir, fixture + "_msvc.obj"), "/link", "/DEBUG:FULL"], log)
        producers.append(("msvc", exe, code))
        for producer, exe, code in producers:
            cell = {"fixture": fixture, "producer": producer, "compile_exit": code}
            if code == 0:
                cell["run_exit"], _ = run([exe], log)
                for debugger, fn in (("cdb", cdb), ("lldb", lldb)):
                    if not fn:
                        cell[debugger] = "not available"
                        continue
                    hits = debug_cdb(fn, exe, fixture, out_dir, log) if debugger == "cdb" else debug_lldb(fn, exe, fixture, log)
                    cell[debugger] = {"hits": hits, "rows": compare(producer, fixture, hits)}
            results["cells"].append(cell)
            if producer.startswith("buster") and code == 0 and cdb:
                results["cells"].append(roundtrip_cell(cdb, fixture, producer, exe, out_dir, log))
    probe(cdb, out_dir, log, results)
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, indent=1)
    md = ["# #2717 Windows %s debugger readback" % os.environ.get("VERIFY_ARCH", "?"), "", "cdb: `%s`  lldb: `%s`" % (cdb, lldb), ""]
    md.append("| fixture | producer | debugger | line/hit | var | expected | observed | verdict |")
    md.append("|---|---|---|---|---|---|---|---|")
    failures = 0
    for cell in results["cells"]:
        if cell["compile_exit"] != 0:
            md.append("| %s | %s | - | - | - | - | compile exit %d | FAIL |" % (cell["fixture"], cell["producer"], cell["compile_exit"]))
            failures += 1
            continue
        for debugger in ("cdb", "lldb"):
            data = cell.get(debugger)
            if not isinstance(data, dict):
                md.append("| %s | %s | %s | - | - | - | %s | n/a |" % (cell["fixture"], cell["producer"], debugger, data))
                continue
            for row in data["rows"]:
                if row["verdict"] == "FAIL":
                    failures += 1
                md.append("| %s | %s | %s | %d/%d | %s | %s | %s | %s |" % (cell["fixture"], cell["producer"], debugger, row["line"], row["hit"], row["var"], row["expected"], row["observed"], row["verdict"]))
        md.append("| %s | %s | run | - | - | exit 0 | exit %s | %s |" % (cell["fixture"], cell["producer"], cell.get("run_exit"), "ok" if cell.get("run_exit") == 0 else "FAIL"))
    md.append("")
    for tag, verdict in results.get("variants", {}).items():
        md.append("variant %s: %s" % (tag, verdict))
    for tag, hits in results.get("probe", {}).items():
        md.append("probe %s: %s" % (tag, "; ".join("line %d %s" % (h["line"], h["vars"]) for h in hits) or "no hits"))
    md.append("")
    md.append("FAIL rows: %d" % failures)
    text = "\n".join(md) + "\n"
    with open(os.path.join(out_dir, "results.md"), "w", encoding="utf-8") as f:
        f.write(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(text)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
