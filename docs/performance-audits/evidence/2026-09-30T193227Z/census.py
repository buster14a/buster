#!/usr/bin/env python3
"""Read-only source census for #129; direct calls, not runtime hotness."""
import hashlib
import json
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[3]
pattern = re.compile(r"\b(simd512_[a-z_]+|_mm512_(?:test_epi32_mask|min_epu32|max_epu32|maskz_permutex2var_epi32|permutex2var_epi32|conflict_epi32|popcnt_epi64|add_epi32|sub_epi32))\s*\(")
files = []
matches = []
paths = subprocess.check_output(["git", "ls-files", "-z", "src/buster/lib/compiler"], cwd=root).decode().split("\0")
for path in sorted(filter(None, paths)):
    if pathlib.Path(path).suffix not in (".c", ".h") or "/generated/" in path:
        continue
    data = (root / path).read_bytes()
    blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    files.append({"path": path, "bytes": len(data), "git_blob": blob, "sha256": hashlib.sha256(data).hexdigest()})
    for number, line in enumerate(data.decode().splitlines(), 1):
        for match in pattern.finditer(line):
            matches.append({"path": path, "line": number, "name": match.group(1), "text": line.strip()})
print(json.dumps({"revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root).decode().strip(),
                  "scope": "all tracked non-generated compiler C/header files; textual direct-call census, not dynamic frequency",
                  "files": files, "matches": matches}, indent=2))
