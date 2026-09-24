#!/usr/bin/env bash
set -euo pipefail

repo="${1:-buster14a/buster}"
root="$(cd "$(dirname "$0")/../../.." && pwd)"
ruleset="$root/.github/rulesets/benchmark-main.json"
main_ruleset="$root/.github/main-merge-queue.ruleset.json"
actor_policy="$root/.github/benchmark-actions-policy.json"
api_version=2026-03-10

if [[ ! "$repo" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]]; then
  printf 'usage: %s OWNER/REPO\n' "$0" >&2
  exit 2
fi
owner="${repo%%/*}"
gh auth status >/dev/null

# Installation is a staging operation. Never reset an active dispatch window
# as a side effect of a policy read-back or rerun. Missing and unreadable
# variables also stop before any policy mutation.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/variables/BENCH_SERVICE_DISPATCH_ENABLED" >"$tmp/staged-variable.json"; then
  printf 'readable BENCH_SERVICE_DISPATCH_ENABLED=false is required before installation\n' >&2
  exit 1
fi
python3 - "$tmp/staged-variable.json" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        variable = json.load(source)
    if variable.get("name") != "BENCH_SERVICE_DISPATCH_ENABLED" or variable.get("value") != "false":
        raise ValueError("dispatch is not staged disabled")
except (OSError, ValueError, AttributeError) as error:
    print(f"BENCH_ADMISSION_FAIL {error}; leave the variable unchanged", file=sys.stderr)
    sys.exit(1)
PY

main_ruleset_id=22537199
listed_main_ids="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets?includes_parents=false" \
  --jq '.[] | select(.name == "main") | .id')"
if [[ "$listed_main_ids" != "$main_ruleset_id" ]]; then
  printf 'exact main merge-queue ruleset is missing or replaced\n' >&2
  exit 1
fi
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

# The requester policy is a pre-existing administrator control. Apps can start
# a run, but only an administrator may approve its protected-environment job.
# Never create or change the requester policy as a side effect of installation.
policy_id="$(python3 - "$actor_policy" <<'PY'
import json
import sys
print(json.load(open(sys.argv[1], encoding="utf-8"))["id"])
PY
)"
policy_matches="$(gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/policies?has_parents=false&per_page=100" \
  --jq ".policies[] | select(.source_type == \"Repository\" and .id == $policy_id) | .id")"
if [[ "$policy_matches" != "$policy_id" ]]; then
  printf 'expected the one reviewed repository Actions policy for benchmark requests\n' >&2
  exit 1
fi
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/policies/$policy_id" >"$tmp/actor-policy.json"
python3 "$root/tools/bench_service/deploy/verify_github_actor_policy.py" \
  "$actor_policy" "$tmp/actor-policy.json" "$repo"

gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/collaborators/davidgmbb/permission" >"$tmp/reviewer-permission.json"
python3 - "$tmp/reviewer-permission.json" <<'PY'
import json
import sys

permission = json.load(open(sys.argv[1], encoding="utf-8"))
user = permission.get("user") or {}
if permission.get("permission") != "admin" or user.get("login") != "davidgmbb" or user.get("id") != 39247043:
    sys.exit("required benchmark reviewer must still be the reviewed repository administrator")
PY

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

python3 - "$ruleset" "$tmp/benchmark.json" <<'PY'
import json
import sys

expected_ruleset, live_ruleset = map(
    lambda path: json.load(open(path)), sys.argv[1:]
)
for key in ("name", "target", "enforcement", "bypass_actors", "conditions", "rules"):
    if live_ruleset.get(key) != expected_ruleset[key]:
        sys.exit(f"benchmark branch ruleset mismatch: {key}")
PY

# Connector requests must remain pending until the reviewed administrator
# approves the environment job. Preserve the exact main branch restriction.
python3 - >"$tmp/environment.json" <<'PY'
import json
import sys

json.dump(
    {
        "wait_timer": 0,
        "prevent_self_review": True,
        "reviewers": [{"type": "User", "id": 39247043}],
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

# Verify fresh GET responses after installation, including the exact branch
# restriction and disabled variable. A successful PUT response is not a receipt.
benchmark_id="$(python3 - "$tmp/benchmark.json" <<'PY'
import json
import sys

value = json.load(open(sys.argv[1])).get("id")
if type(value) is not int or value <= 0:
    sys.exit("installed benchmark ruleset ID missing")
print(value)
PY
)"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/rulesets/$benchmark_id" >"$tmp/installed-benchmark.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x" >"$tmp/installed-environment.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/environments/benchmark-9700x/deployment-branch-policies?per_page=100" >"$tmp/installed-branches.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/variables/BENCH_SERVICE_DISPATCH_ENABLED" >"$tmp/installed-variable.json"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/actions/policies/$policy_id" >"$tmp/installed-actor-policy.json"
python3 "$root/tools/bench_service/deploy/verify_github_actor_policy.py" \
  "$actor_policy" "$tmp/installed-actor-policy.json" "$repo"
gh api -H "X-GitHub-Api-Version: $api_version" \
  "repos/$repo/collaborators/davidgmbb/permission" >"$tmp/reviewer-permission.json"
python3 - "$tmp/reviewer-permission.json" <<'PY'
import json
import sys

permission = json.load(open(sys.argv[1], encoding="utf-8"))
user = permission.get("user") or {}
if permission.get("permission") != "admin" or user.get("login") != "davidgmbb" or user.get("id") != 39247043:
    sys.exit("required benchmark reviewer lost repository administrator permission")
PY
python3 "$root/tools/bench_service/deploy/verify_github_admission.py" \
  "$ruleset" "$tmp/installed-benchmark.json" "$tmp/installed-environment.json" \
  "$tmp/installed-branches.json" "$tmp/installed-variable.json"
printf 'Connector requester policy, administrator reviewer and restricted runner group verified; BENCH_SERVICE_DISPATCH_ENABLED read back false\n'
