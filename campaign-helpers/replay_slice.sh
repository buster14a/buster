#!/bin/bash
# usage: replay_slice.sh <artifact-dir> <subject-commit> <subject-tree> <build-blob>
# Independent offline replay of a #1162 exact-systemd slice artifact (the
# `issue1162-exact-systemd-slice` upload, fetched through the readback branch).
# Run from a checkout whose .github/scripts and tools/bench_service match the
# subject. Builds the service tools, then re-runs the three verdicts and
# compares them byte-for-byte with the artifact's own outputs.
set -uo pipefail
A=$(cd "$1" && pwd); SUBJECT=$2; TREE=$3; BLOB=$4
ROOT=$(git rev-parse --show-toplevel)
OUT=$(mktemp -d)
cd "$ROOT" && mkdir -p build &&
  clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing \
        -funsigned-char -g build.c -o build/buster-bench-build &&
  build/buster-bench-build bench_service capabilities >/dev/null 2>&1
SERVICE=$ROOT/build/bench-service-tools/service
sha256sum "$A/result.bqexport"

# 1. Broker entry consumer, 11/11 activations, identical to the artifact's reconciliation.
python3 - "$A" "$OUT" <<'EOF'
import json, sys
a, out = sys.argv[1], sys.argv[2]
e = json.load(open(f"{a}/broker-entry-expected.json"))
e["readbacks"] = [f"{a}/entry-readback-before.json", f"{a}/entry-readback-after.json"]
json.dump(e, open(f"{out}/entry-local.json", "w"))
EOF
cmp "$A/entry-readback-before.json" "$A/entry-readback-after.json" && echo readbacks-identical
python3 .github/scripts/issue1162_broker_entry_evidence.py --journal-jsonl "$A/broker-journal.jsonl" \
  --observer-dir "$A/broker-observer-artifacts" --expected-json "$A/broker-expected.json" \
  --entry-expected-json "$OUT/entry-local.json" --overlap-jsonl "$A/broker-overlap.jsonl" > "$OUT/entry.json"
echo entry_exit=$?
python3 -c "import json,sys; d=json.load(open('$OUT/entry.json')); h=json.load(open('$A/broker-entry-reconciliation.json')); print('entry_complete', d['entry_complete'], 'activations', len(d['activations']), 'identical', d==h)"

# 2. Authenticated export unpack, identical to the artifact's replay tree.
R=$(sed -nE 's/^export-receipt-sha256=([a-f0-9]{64})$/\1/p' "$A/export-stderr.txt"); echo receipt=$R
mkdir -m 0700 "$OUT/replay" && "$SERVICE" unpack-export "$A/result.bqexport" "$OUT/replay/result" "$R"
echo unpack_exit=$?
diff -r "$OUT/replay/result" "$A/replay/result" && echo replay-tree-identical

# 3. Frozen-tree reconciliation, identical to the artifact's verdict.
python3 .github/scripts/issue1162_frozen_tree_evidence.py verify --observer-dir "$A/stage-observer-artifacts" \
  --base-receipt "$OUT/replay/result/validate-buster-v1.base-build.inventory" \
  --candidate-receipt "$OUT/replay/result/validate-buster-v1.candidate-build.inventory" \
  --source-commit "$SUBJECT" --source-tree "$TREE" --build-blob "$BLOB" --output "$OUT/ft.json" | tail -1
diff <(python3 -m json.tool "$OUT/ft.json") <(python3 -m json.tool "$A/frozen-tree-reconciliation.json") && echo frozen-identical
echo "scratch: $OUT"
