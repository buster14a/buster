#!/usr/bin/env python3
# Counters for one ide binary on the frozen unity input, every target and mode.
import json, os, re, subprocess, sys
S = os.path.dirname(os.path.abspath(__file__))
name, ide = sys.argv[1], sys.argv[2]
TARGETS = {"x86_64": ["-march=native"], "aarch64": ["-target", "aarch64-linux", "-isystem", "/usr/aarch64-linux-gnu/include"]}
out = {}
for t, flags in TARGETS.items():
    for m in ["fast", "quality"]:
        cmd = [ide, "cc", "-Isrc", "-Igenerated", "-DBUSTER_UNITY_BUILD=1", "-DBUSTER_INCLUDE_TESTS=0", "-g0"] + flags + ["-fregister-allocator=" + m, "-c", "src/buster/apps/ide/ide.c", "-o", os.devnull, "-v"]
        p = subprocess.run(cmd, cwd=os.path.join(S, "frozen"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert p.returncode == 0, p.stdout[-2000:]
        row = {}
        for line in p.stdout.splitlines():
            head = line.split(" ", 1)[0]
            if head in ("CODEGEN", "CODEGEN_ALLOCATOR"):
                for k, v in re.findall(r"(\w+)=(\d+)\b", line):
                    row[k] = int(v)
        out["%s/%s" % (t, m)] = {k: row[k] for k in ("values", "spills", "reloads", "boundary_spills", "boundary_reloads", "copies", "rematerializations", "code_bytes")}
os.makedirs(os.path.join(S, "attr"), exist_ok=True)
json.dump(out, open(os.path.join(S, "attr", name + ".json"), "w"), indent=1)
print(name, json.dumps(out["x86_64/fast"]))
