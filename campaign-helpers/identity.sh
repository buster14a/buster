#!/bin/bash
# usage: identity.sh <rev>  (run inside a repo checkout)
set -euo pipefail
rev=$1
echo "commit $(git rev-parse $rev) tree $(git rev-parse $rev^{tree})"
# Installed / built service inputs and deploy references
git ls-tree -r $rev -- build.c tools/bench_service/deploy tools/bench_service/profiles \
  | awk '{print $3, $4}'
# Aggregate digests over the closures the installed binaries are built from
for set in "tools/bench_service" "tools/throughput" "src"; do
  printf '%s aggregate-ls-tree-sha256=%s files=%s\n' "$set" \
    "$(git ls-tree -r $rev -- $set | sha256sum | cut -d' ' -f1)" "$(git ls-tree -r $rev -- $set | wc -l)"
done
