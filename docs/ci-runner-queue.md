# GitHub runner queue and macOS capacity

Ownership: runner scheduling latency and hosted-runner demand for every
GitHub workflow in this repository (#1805). Timing of successful `ci.yml`
cohorts stays in [ci-github-actions.md](ci-github-actions.md); merge-queue
policy stays in `tools/merge_queue_admission.py::QUEUE` and
`.github/main-merge-queue.ruleset.json`.

## Method

`tools/github_ci_time.py` is the only collector; it runs from an operator
machine or container, never as a runner-held polling job.

```sh
python3 tools/github_ci_time.py queue-collect --since 2026-09-29T03:00:00Z \
    --until 2026-09-29T09:07:00Z --output queue.json
python3 tools/github_ci_time.py queue-summarize queue.json --output queue-summary.json
```

`queue_collect` enumerates every run created in the window (splitting it until
each `created=` query fits GitHub's 1000-result search limit) plus every run
currently `queued`, `in_progress`, `waiting`, `pending` or `requested`. It
fetches all job attempts (`filter=all`) and retains run/attempt/job/SHA,
runner identity and the observation time. Historical listings must reconcile
exactly with `total_count`; live status listings change while paged, so each
records the reported total next to the number obtained.

`queue_summarize` groups jobs by exact runner labels and event:

- **dependency wait**: attempt `run_started_at` to job `created_at` (`needs`);
- **assignment wait**: job `created_at` to `started_at`, only when the job has a
  `runner_id` or `runner_name`. An unassigned job's `started_at` is a placeholder
  and is never counted as execution;
- **execution** and summed runner seconds of completed assigned jobs;
- jobs active and still queued at observation, and the oldest ready job;
- per runner family (macOS, Windows, Ubuntu), time-weighted occupancy of this
  repository's assigned jobs while any job of that family waited. The first
  90 minutes (`QUEUE_OCCUPANCY_WARMUP_SECONDS`) are excluded because jobs of runs
  created before the window are not collected;
- runner seconds by workflow, event and run outcome, and successful runs per hour
  with their created-to-updated latency.

Organization-wide allowance, other repositories' demand and GitHub provisioning
state are not visible to a repository token and are reported as unknown.

## Baseline: 2026-09-29

Window 03:00–09:07 UTC (activity from about 07:00), collected at 09:10 UTC on
`main` at `35845849de61f4c7e2177562eccc3d46b294c3f6`: 1547 runs, 4892 jobs,
about 2.5 minutes and 1600 REST requests.

| Runner label, event | Jobs | Assignment wait p50 / p90 / max | Execution p50 | Queued at observation |
| --- | ---: | --- | ---: | ---: |
| `macos-26`, merge_group | 270 | 241 s / 1280 s / 2188 s | 323 s | 64 |
| `macos-26`, pull_request | 275 | 290 s / 1278 s / 1937 s | 373 s | 25 |
| `macos-26-intel`, merge_group | 180 | 380 s / 1407 s / 2049 s | 462 s | 45 |
| `macos-26-intel`, pull_request | 189 | 387 s / 1384 s / 1955 s | 636 s | 22 |
| `ubuntu-26.04`, merge_group | 572 | 4 s / 64 s / 201 s | 146 s | 2 |
| `windows-2025`, pull_request | 220 | 3 s / 5 s / 103 s | 243 s | 0 |

Only macOS jobs wait for runners; Linux and Windows assignment is seconds.
From 04:30 UTC, macOS jobs waited during 5453 s. This repository alone held
48–50 macOS runners for 91.5% of that time and exactly 50 for 46.3%; it never
held more than 50 while a job waited, and 173 macOS jobs waited at the peak.
Ubuntu reached 236 and Windows 113 concurrent jobs.

**Binding constraint.** An effective ceiling of 50 concurrent macOS jobs,
saturated by this repository's own demand. It is consistent with a hosted
macOS concurrency allowance of 50; the account plan, whether other
repositories share it, and fleet provisioning remain unknown. Nothing here
indicates a provider outage.

**Demand.** macOS runners executed 4323 minutes. `ci.yml` is 98% of it:

| `ci.yml` event, run outcome | macOS minutes | Share |
| --- | ---: | ---: |
| merge_group, success | 1045 | 24.2% |
| pull_request, success | 896 | 20.7% |
| merge_group, cancelled | 697 | 16.1% |
| pull_request, in progress at observation | 339 | 7.8% |
| pull_request, failure | 286 | 6.6% |
| push, success | 264 | 6.1% |
| merge_group, queued at observation | 217 | 5.0% |
| pull_request, cancelled | 202 | 4.7% |
| merge_group, failure | 112 | 2.6% |

Each `ci.yml` run needs eight macOS jobs, so the ceiling runs about six CI
runs at once. Between 6 and 16 merge-group `ci.yml` runs were active
simultaneously (`max_entries_to_build: 20`); 16 ended cancelled, 1 failed and
12 succeeded. All five `main` push runs revalidated a SHA whose merge-group
`ci.yml` run had already succeeded. Successful `ci.yml` runs created in the
window: 12 merge-group (median 2175 s created-to-updated), 10 pull-request and
3 push.

## Candidate corrections (not applied)

The completion criteria require comparable before/after windows, so no queue
setting or workflow trigger changes with this baseline. Each candidate needs a
reviewed rollout and a second window measured with the commands above.

1. **Bound speculative merge-group builds to macOS capacity.** Lower
   `max_entries_to_build` to about `50 / 8`, i.e. 6. Deeper entries cannot hold
   macOS runners, compete with pull requests, and are the first cancelled when
   an earlier entry fails. Change the live ruleset,
   `.github/main-merge-queue.ruleset.json`, `QUEUE` and its tests together;
   compare completed merges per hour and end-to-end latency, not queue depth.
2. **Avoid revalidating merge-group heads on `main` pushes** (about 8% of macOS
   minutes here). Push runs may prime `main`-scoped caches and feed other
   workflows; verify those consumers before proposing any change, and never
   skip a push without exact-SHA success evidence.
3. **Raise the macOS allowance.** An account/billing decision with unknown
   cost; out of repository scope.
