#!/usr/bin/env bash
# Usage: make_inputs.sh EVIDENCE_DIR WORK_DIR. Regenerates the inputs; check
# them against inputs.sha256 (bytes depend on the exact Clang build).
set -euo pipefail
evidence=$1; work=$2
cd "$work"
cp "$evidence/main.c" main.c
for n in 256 1024 4096 16384; do
    python3 -I "$evidence/gen_tls.py" "$n" "m$n.c"
    clang -O0 -fPIC -g0 -c "m$n.c" -o "m$n.o"
    clang -O0 -fPIC -fno-plt -g0 -c "m$n.c" -o "n$n.o"
    rm -f "m$n.a"; ar rcs "m$n.a" "m$n.o"
done
clang -O0 -g0 -c main.c -o main.o
python3 -I "$evidence/gen_plain.py" plain.c
clang -O0 -fPIC -g0 -c plain.c -o plain.o
