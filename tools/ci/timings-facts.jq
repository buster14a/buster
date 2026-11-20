# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# JSON/API normalization only. Phase classification and statistics belong to C.
# All text is one raw TSV field: controls, backslashes and Markdown table pipes
# are retained as ordinary data except controls, which become spaces.
def text: if . == null or . == "" then "-" else tostring | gsub("[\u0000-\u001f\u007f]"; " ") end;
def epoch: if type == "string" then (try (sub("\\.[0-9]+Z$"; "Z") | fromdateiso8601) catch null) else null end;
def row: map(text) | join("\t");
def group($workflow):
    if $workflow == "nightly-backfill.yml" or $workflow == "ci-timings.yml" or $workflow == "rerun.yml" then "reporting"
    elif $workflow == "publish-hosted-inputs.yml" or $workflow == "prepare-hosted-tools.yml" then "preparation"
    elif .event == "pull_request" or .event == "pull_request_target" then "pr"
    elif .event == "merge_group" and ((.head_branch // "") | startswith("gh-readonly-queue/main/")) then "queue"
    elif .event == "merge_group" then "other"
    elif .event == "push" and .head_branch == "main" then "main"
    elif .event == "workflow_dispatch" and ((.display_title // "") | startswith("Nightly backlog ")) then "targeted"
    elif .event == "workflow_dispatch" or .event == "schedule" then "full"
    else "other" end;
def runner:
    if .runner_id == 0 or .conclusion == "skipped"
        or (has("runner_id") and .runner_id == null and has("runner_name") and .runner_name == null
            and has("runner_group_id") and .runner_group_id == null and (.steps | type) == "array" and (.steps | length) == 0) then "none"
    elif (.labels // [] | index("self-hosted")) != null then "self-hosted"
    elif (.runner_id // 0) > 0 then "hosted"
    else "unknown" end;

$run[0] as $r |
(["run", $r.id, $attempt, $workflow, ($r | group($workflow)),
    (if $split == null then "all" elif ($r.created_at | epoch) == null then "all"
     elif ($r.created_at | epoch) < $split then "before" else "after" end),
    $r.head_sha, $r.head_branch, $r.status, $r.conclusion,
    ($r.created_at | epoch), ($r.run_started_at | epoch), ($r.updated_at | epoch), $fate, $complete] | row),
($jobs[] | .[] | . as $j |
    ([$evidence[] | select(.id == $j.id)] | .[0] // {}) as $e |
    (["job", $j.id, $j.name, ($j | runner), (($j.labels // []) | join(",")), $e.cpu,
        $j.status, $j.conclusion, ($j.created_at | epoch), ($j.started_at | epoch), ($j.completed_at | epoch),
        (if ($j.steps | type) == "array" then 1 else 0 end)] | row),
    (($j.steps // []) | to_entries[] |
        (["step", (.value.number // (.key + 1)), .value.name, .value.status, .value.conclusion,
            (.value.started_at | epoch), (.value.completed_at | epoch)] | row)),
    (if $e.lock_start != null or $e.lock_acquired != null or $e.lock_released != null then
        ["lock", $e.lock_start, $e.lock_acquired, $e.lock_released] | row else empty end)
)
