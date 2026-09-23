#!/usr/bin/env bash
set -euo pipefail

repo="${1:-buster14a/buster}"
reviewer_type="${2:-}"
reviewer_id="${3:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
ruleset="$root/.github/rulesets/benchmark-main.json"
main_ruleset="$root/.github/main-merge-queue.ruleset.json"
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

# Keep this identity aligned with tools/merge_queue_admission.py::RULESET_ID.
main_ruleset_id=22537199
listed_main_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets?includes_parents=false" \
  --jq '.[] | select(.name == "main") | .id')"
if [[ "$listed_main_ids" != "$main_ruleset_id" ]]; then
  printf 'exact main merge-queue ruleset is missing or replaced\n' >&2
  exit 1
fi
live_main_payload="$(mktemp)"
environment_payload="$(mktemp)"
trap 'rm -f "$live_main_payload" "$environment_payload"' EXIT
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets/$main_ruleset_id" >"$live_main_payload"
python3 "$root/tools/bench_service/deploy/verify_github_queue.py" \
  "$main_ruleset" "$live_main_payload" "$repo" "$main_ruleset_id"

ruleset_id="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets?includes_parents=false" \
  --jq '.[] | select(.name == "Benchmark dispatch main protection") | .id')"
if [[ "$ruleset_id" == *$'\n'* ]]; then
  printf 'multiple benchmark admission rulesets exist\n' >&2
  exit 1
fi
if [[ -n "$ruleset_id" ]]; then
  gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets/$ruleset_id" --input "$ruleset" >/dev/null
else
  ruleset_id="$(gh api --method POST -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets" --input "$ruleset" --jq .id)"
fi

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
            "protected_branches": False,
            "custom_branch_policies": True,
        },
    },
    sys.stdout,
)
PY

gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" --input "$environment_payload" >/dev/null

branch_policies="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x/deployment-branch-policies?per_page=100" \
  --jq 'if .total_count == 0 then "create" elif .total_count == 1 and .branch_policies[0].name == "main" and .branch_policies[0].type == "branch" then "present" else "invalid" end')"
if [[ "$branch_policies" == create ]]; then
  gh api --method POST -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/environments/benchmark-9700x/deployment-branch-policies" \
    -f name=main -f type=branch >/dev/null
elif [[ "$branch_policies" != present ]]; then
  printf 'benchmark environment has unexpected deployment branch policies\n' >&2
  exit 1
fi

gh api -H "X-GitHub-Api-Version: $api_version" "repos/$repo/rulesets/$ruleset_id" \
  --jq '{id, name, enforcement, bypass_actors, conditions, rules}'
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" \
  --jq '{name, protection_rules, deployment_branch_policy}'
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x/deployment-branch-policies" \
  --jq '{total_count, branch_policies}'
printf 'BENCH_SERVICE_DISPATCH_ENABLED remains false\n'
