#!/usr/bin/env bash
set -euo pipefail

repo="${1:-buster14a/buster}"
reviewer_type="${2:-}"
reviewer_id="${3:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
ruleset="$root/.github/rulesets/benchmark-main.json"
api_version=2026-03-10

if [[ "$reviewer_type" != User && "$reviewer_type" != Team ]]; then
  printf 'usage: %s OWNER/REPO User|Team NUMERIC_REVIEWER_ID\n' "$0" >&2
  exit 2
fi
if [[ ! "$reviewer_id" =~ ^[1-9][0-9]*$ ]]; then
  printf 'reviewer ID must be a positive integer\n' >&2
  exit 2
fi
if [[ ! "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
  printf 'repository must be OWNER/REPO\n' >&2
  exit 2
fi

gh auth status >/dev/null

# Fail closed before any policy mutation. Reconfiguration may be rerun while
# admission is enabled; a later API failure must not leave dispatch enabled
# under a partially applied ruleset or environment.
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body false --repo "$repo"

ruleset_id="$(gh api -H "X-GitHub-Api-Version: $api_version" "repos/$repo/rulesets" \
  --jq '.[] | select(.name == "Benchmark dispatch main protection") | .id' | head -n 1)"
if [[ -n "$ruleset_id" ]]; then
  gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets/$ruleset_id" --input "$ruleset" >/dev/null
else
  ruleset_id="$(gh api --method POST -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets" --input "$ruleset" --jq .id)"
fi

environment_payload="$(mktemp)"
trap 'rm -f "$environment_payload"' EXIT
python3 - "$reviewer_type" "$reviewer_id" >"$environment_payload" <<'PY'
import json
import sys

reviewer_type, reviewer_id = sys.argv[1], int(sys.argv[2])
json.dump(
    {
        "wait_timer": 0,
        "prevent_self_review": True,
        "reviewers": [{"type": reviewer_type, "id": reviewer_id}],
        "deployment_branch_policy": {
            "protected_branches": True,
            "custom_branch_policies": False,
        },
    },
    sys.stdout,
)
PY

gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" --input "$environment_payload" >/dev/null

gh api -H "X-GitHub-Api-Version: $api_version" "repos/$repo/rulesets/$ruleset_id" \
  --jq '{id, name, enforcement, bypass_actors, conditions, rules}'
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" \
  --jq '{name, protection_rules, deployment_branch_policy}'
printf 'BENCH_SERVICE_DISPATCH_ENABLED remains false\n'
