#!/usr/bin/env bash
set -euo pipefail

repo="${1:-buster14a/buster}"
root="$(cd "$(dirname "$0")/../../.." && pwd)"
ruleset="$root/.github/rulesets/benchmark-main.json"
policy="$root/.github/actions-policies/benchmark-dispatch.json"
main_ruleset="$root/.github/main-merge-queue.ruleset.json"
api_version=2026-03-10

if [[ ! "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
  printf 'usage: %s OWNER/REPO\n' "$0" >&2
  exit 2
fi
owner="${repo%%/*}"
gh auth status >/dev/null

# Reconfiguration must fail closed, including on reruns after activation.
gh variable set BENCH_SERVICE_DISPATCH_ENABLED --body false --repo "$repo"

main_ruleset_id=22537199
listed_main_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets?includes_parents=false" \
  --jq '.[] | select(.name == "main") | .id')"
if [[ "$listed_main_ids" != "$main_ruleset_id" ]]; then
  printf 'exact main merge-queue ruleset is missing or replaced\n' >&2
  exit 1
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets/$main_ruleset_id" >"$tmp/main.json"
python3 "$root/tools/bench_service/deploy/verify_github_queue.py" \
  "$main_ruleset" "$tmp/main.json" "$repo" "$main_ruleset_id"

# A repository-scoped runner cannot be protected by an organization runner group.
group_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "orgs/$owner/actions/runner-groups?per_page=100" \
  --jq '.runner_groups[] | select(.name == "buster-9700x-service-dispatch") | .id')"
if [[ ! "$group_ids" =~ ^[1-9][0-9]*$ ]]; then
  printf 'exactly one restricted organization runner group is required\n' >&2
  exit 1
fi
gh api -H "X-GitHub-Api-Version: $api_version" \
  "orgs/$owner/actions/runner-groups/$group_ids" >"$tmp/group.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "orgs/$owner/actions/runner-groups/$group_ids/repositories?per_page=100" >"$tmp/repositories.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "orgs/$owner/actions/runner-groups/$group_ids/runners?per_page=100" >"$tmp/runners.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/runners?per_page=100" >"$tmp/repo-runners.json"
repo_id="$(gh api -H "X-GitHub-Api-Version: $api_version" "repos/$repo" --jq .id)"
python3 - "$tmp" "$repo" "$repo_id" <<'PY'
import json
import pathlib
import sys

directory, repo, repo_id = pathlib.Path(sys.argv[1]), sys.argv[2], int(sys.argv[3])
read = lambda name: json.loads((directory / name).read_text())
group = read("group.json")
expected_workflow = f"{repo}/.github/workflows/9700x-service-dispatch.yml@refs/heads/main"
if not (
    group.get("visibility") == "selected"
    and group.get("allows_public_repositories") is True
    and group.get("restricted_to_workflows") is True
    and group.get("selected_workflows") == [expected_workflow]
):
    sys.exit("runner group must be limited to the selected public repository and main workflow")
repositories = read("repositories.json")
if repositories.get("total_count") != 1 or [r["id"] for r in repositories["repositories"]] != [repo_id]:
    sys.exit("runner group repository access is not exclusive")
runners = read("runners.json")
if runners.get("total_count") != 1 or [r["name"] for r in runners["runners"]] != ["buster-zen5-9700x"]:
    sys.exit("expected organization runner is not in the restricted group")
required = {"self-hosted", "Linux", "X64", "buster-zen5", "ryzen-9700x"}
if not required <= {label["name"] for label in runners["runners"][0]["labels"]}:
    sys.exit("organization runner labels do not match the workflow")
repository_runners = read("repo-runners.json")
if any(r["name"] == "buster-zen5-9700x" or
       {"buster-zen5", "ryzen-9700x"} & {label["name"] for label in r["labels"]}
       for r in repository_runners["runners"]):
    sys.exit("benchmark runner is still registered at repository scope")
PY

# Preserve the exact main branch deployment restriction before changing review.
branch_policies="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x/deployment-branch-policies?per_page=100" \
  --jq 'if .total_count == 1 and .branch_policies[0].name == "main" and .branch_policies[0].type == "branch" then "present" else "invalid" end')"
if [[ "$branch_policies" != present ]]; then
  printf 'benchmark environment must allow exactly the main branch\n' >&2
  exit 1
fi

ruleset_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets?includes_parents=false" \
  --jq '.[] | select(.name == "Benchmark dispatch main protection") | .id')"
if [[ "$ruleset_ids" == *$'\n'* ]]; then
  printf 'multiple benchmark branch rulesets exist\n' >&2
  exit 1
fi
if [[ -n "$ruleset_ids" ]]; then
  gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets/$ruleset_ids" --input "$ruleset" >"$tmp/benchmark.json"
else
  gh api --method POST -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/rulesets" --input "$ruleset" >"$tmp/benchmark.json"
fi

policy_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/policies?has_parents=false&per_page=100" \
  --jq '.policies[] | select(.name == "9700X benchmark dispatch administrators") | .id')"
if [[ "$policy_ids" == *$'\n'* ]]; then
  printf 'multiple benchmark Actions policies exist\n' >&2
  exit 1
fi
if [[ -n "$policy_ids" ]]; then
  gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/actions/policies/$policy_ids" --input "$policy" >/dev/null
else
  policy_ids="$(gh api --method POST -H "X-GitHub-Api-Version: $api_version" \
    "repos/$repo/actions/policies" --input "$policy" --jq .id)"
fi
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/policies/$policy_ids" >"$tmp/policy.json"
python3 - "$ruleset" "$tmp/benchmark.json" "$policy" "$tmp/policy.json" <<'PY'
import json
import sys

expected_ruleset, live_ruleset, expected_policy, live_policy = map(
    lambda path: json.load(open(path)), sys.argv[1:]
)
for key in ("name", "target", "enforcement", "bypass_actors", "conditions", "rules"):
    if live_ruleset.get(key) != expected_ruleset[key]:
        sys.exit(f"benchmark branch ruleset mismatch: {key}")
for key in ("name", "enforcement", "conditions", "rules"):
    if live_policy.get(key) != expected_policy[key]:
        sys.exit(f"benchmark Actions policy mismatch: {key}")
if live_policy.get("source_type") != "Repository":
    sys.exit("benchmark Actions policy must belong to this repository")
PY

# Only after the actor restriction, branch protection, and runner restriction
# are read back may a dispatch run without an extra environment approval.
python3 - >"$tmp/environment.json" <<'PY'
import json
import sys

json.dump(
    {
        "wait_timer": 0,
        "prevent_self_review": True,
        "reviewers": [],
        "deployment_branch_policy": {
            "protected_branches": False,
            "custom_branch_policies": True,
        },
    },
    sys.stdout,
)
PY
gh api --method PUT -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" --input "$tmp/environment.json" >"$tmp/live-environment.json"
python3 - "$tmp/live-environment.json" <<'PY'
import json
import sys

environment = json.load(open(sys.argv[1]))
rules = environment.get("protection_rules", [])
if any(rule.get("type") == "required_reviewers" for rule in rules):
    sys.exit("environment still requires a reviewer")
if environment.get("deployment_branch_policy") != {
    "protected_branches": False, "custom_branch_policies": True
}:
    sys.exit("environment deployment branch policy changed")
PY
printf 'Admin-only Actions policy and restricted runner group verified; BENCH_SERVICE_DISPATCH_ENABLED remains false\n'
