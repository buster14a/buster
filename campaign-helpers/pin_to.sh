#!/bin/bash
# usage: pin_to.sh <main-or-queue-commit M>
# Re-pins the #1162 exact-systemd slice to M in a detached worktree at $WEXEC.
# Afterwards: set INTEGRATED_REVIEW if build.c changed (see README), run the
# frozen-tree self-test, commit, and push to codex/1162-exact-systemd-slice-20260925.
set -euo pipefail
M=$1; W=${WEXEC:-$HOME/wexec}; REPO=${REPO:-$(git rev-parse --show-toplevel)}
cd "$REPO" && git fetch -q origin main
git worktree remove --force $W 2>/dev/null || true
git worktree add -q --detach $W $M
cd $W
T=$(git rev-parse $M^{tree}); B=$(git rev-parse $M:build.c)
tmp=$(mktemp -d); git archive $M -- src cmake CMakeLists.txt | tar -x -C $tmp
read NF NB SHA < <(REV=$M ROOT=$tmp python3 - <<'PY'
import hashlib, os
from pathlib import Path
root = Path(os.environ["ROOT"]); rev = os.environ["REV"]
files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix().encode())
lines = ["BQ-SOURCE-V1", "repository=buster14a/buster", "revision=" + rev]
for path in files:
    lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + " " + path.relative_to(root).as_posix())
m = ("\n".join(lines) + "\n").encode()
print(len(files), len(m), hashlib.sha256(m).hexdigest())
PY
)
rm -rf $tmp
python3 - "$M" "$T" "$B" "$NF" "$NB" "$SHA" <<'PY'
import re, sys
M,T,B,NF,NB,SHA = sys.argv[1:]
p='.github/scripts/issue1162_exact_systemd_slice.sh'; s=open(p).read()
s=re.sub(r'^subject=[0-9a-f]{40}$', f'subject={M}', s, count=1, flags=re.M)
s=re.sub(r'^subject_tree=[0-9a-f]{40}$', f'subject_tree={T}', s, count=1, flags=re.M)
s=re.sub(r'^subject_build_blob=[0-9a-f]{40}$', f'subject_build_blob={B}', s, count=1, flags=re.M)
lines=s.split('\n'); out=[]
for l in lines:
    if re.match(r'    "[0-9a-f]{40}": \(\d+, \d+, "[0-9a-f]{64}"\),', l) and not l.startswith('    "ade6ac4b'):
        l=f'    "{M}": ({NF}, {NB}, "{SHA}"),'
    out.append(l)
open(p,'w').write('\n'.join(out))
p='.github/scripts/issue1162_frozen_tree_evidence.py'; s=open(p).read()
s=re.sub(r'INTEGRATED_HEAD = "[0-9a-f]{40}"', f'INTEGRATED_HEAD = "{M}"', s)
s=re.sub(r'INTEGRATED_TREE = "[0-9a-f]{40}"', f'INTEGRATED_TREE = "{T}"', s)
s=re.sub(r'INTEGRATED_BUILD_BLOB = "[0-9a-f]{40}"', f'INTEGRATED_BUILD_BLOB = "{B}"', s)
open(p,'w').write(s)
PY
git diff --stat
echo "M=$M T=$T B=$B manifest=$NF/$NB/$SHA"
