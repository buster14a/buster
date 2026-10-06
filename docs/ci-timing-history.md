# Hosted CI execution history (#2823 / #2824)

This is the build-test project's advisory Actions history. It consumes finalized
job/step API timestamps and the existing #2758 machine-specifications producer.
It does not change required checks, workloads, timeouts, merge admission, or
qualified compiler performance validation on the Ryzen 7 9700X.

The native owner is [tools/ci_metrics.c](../tools/ci_metrics.c). Its JSON/model,
collection, history, report and test headers separate transport, identities,
untrusted-data validation, storage and statistical policy. The legacy
[github_ci_time.py](../tools/github_ci_time.py) still owns require-jobs,
complete-workflow qualification, interruption evidence and queue definitions.
Those entry points and their admission behavior are unchanged.

## Commands and coverage

Compile on a GitHub-hosted Linux worker using its preinstalled compiler:

~~~sh
clang -std=c11 -Isrc -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -fwrapv -fno-strict-aliasing -funsigned-char tools/ci_metrics.c -lm -o ci-metrics
./ci-metrics --self-test
./ci-metrics collect --out /tmp/new-ci-history-bundle --days 2 --max-runs 100
./ci-metrics collect --out /tmp/new-backfill-bundle --run 37459678394
./ci-metrics report --input observations.jsonl --out /tmp/new-selected-report \
  --event push --branch main --job-key native --os Windows --cpu "AMD EPYC 7763 64-Core Processor" \
  --revision 5e46e4f552a8caf9cbecaa408e5089436e831a8a \
  --since 2026-10-01T00:00:00Z --until 2026-10-07T00:00:00Z
~~~

An output directory must be new. GITHUB_SHA supplies the exact collector revision;
GH_TOKEN is needed for authenticated reads. Optional --job selects one numeric API
job after validating the run inventory. --expect-machine is a read-only live-test
assertion; it is not a production hardware/merge gate. --days accepts 1–7 and
--max-runs accepts 1–1000. An explicit --run backfills existing work; it does not
dispatch or rerun a workload.

The collector inventories all terminal workflows returned by the repository API,
rather than maintaining a build-only or green-only job list. Attempt-specific
job inventories paginate at 100; changing counts, duplicates, wrong run/head/
attempt bindings and partial pages produce visible gaps. Current coverage
includes lightweight/reporting, Linux x86-64/AArch64, macOS AArch64, Windows
x86-64/AArch64, mobile-host, analyzer, release and reusable-workflow executions
whenever their existing predicates actually allocate work. Retired platforms are
not scheduled. The definition-level hardware inventory is owned by #2758/#2766;
the consumer adds four executing jobs in its own workflow.

The default cadence is a bounded half-hourly batch from trusted main. Manual
dispatch can select a terminal run for recovery. There is no completion-event self-trigger loop; earlier terminal executions of
this observational workflow are inventoried by later batches. A missed batch is
recoverable without rerunning jobs while API evidence remains available.

The data branch also retains a bounded 4,096-run attempt receipt cache, at most
64 pending run IDs, and a reverse-created-date sweep cursor/page. Pending runs
resume late finalization/API failures; a newer attempt invalidates its prior
complete receipt. Routine batches revisit recent runs and advance one reverse
page through older runs, retaining pending work before moving the cursor. This
provides automatic missed-event/old-rerun discovery beyond the recent window;
its lag depends on repository volume and remaining API/time budget. The report
shows the sweep cursor and pending counts rather than claiming instantaneous
all-history completeness. Large same-second inventories beyond GitHub's filtered
search ceiling remain visible gaps and require explicit run-ID recovery. Manual
backfill can recover an old rerun immediately while API evidence survives.

## Physical execution and metric identities

Schema buster-hosted-ci-execution-v1 retains the repository, numeric run/job IDs,
association attempt, original physical job/attempt, event/head identity, exact
first checkout identity/tree when proven, collector revision/observation time,
job display name, stable workflow job key, workflow path/revision/blob and matrix
identity. A source SHA labels a point and is not the cohort key.

Restricted include-only static matrices retain the complete selected literal
configuration as JSON, bound to the first action's exact strategy.job-index
input and immutable workflow blob. No display-name matching chooses among
cells. Axis products, include/exclude combinations, escaped YAML scalars,
dynamic expansion and reusable caller mappings remain explicitly unavailable
until they have an independently verifiable complete context receipt. The API
still retains their timing rows. The collector does not guess a reusable job
from an ambiguous name or assign its reported CPU to another numeric job.

The machine join reads the exact numeric job's bounded log, locates one v1 JSON
record in the API startup-step interval, checks original run/attempt bindings,
fetches the reported immutable workflow revision and verifies its first step
uses the reviewed machine-specifications action at
a36422384d0334a53d4be73bc306b97ccdba4768. Suspicious loader/compiler environments,
containers, defaults, ambiguous definitions and source actions without the exact
pin fail closed. The source step must independently use the pinned source mode.
This is producer/execution provenance, not authentication by a self-reported
digest. Raw bounded machine/source field status/reason records are retained;
CPU normalization collapses whitespace only and preserves the raw brand.

The retained source record describes the actual primary checkout. It is not a
claim that every later subprocess, secondary checkout or guest executed that
tree. Host/process architecture, guest/translation context, observed resource
limits and compilation target are distinct. CPU or resource values are never
inferred from an OS label. Unknown/self-hosted classes do not enter the default
known-hosted latency or cost populations.

API-observed elapsed seconds are completed_at minus started_at, with one-second
timestamp resolution. Missing, inverted and interrupted intervals are null,
never zero. Steps retain their individual API spans, outcomes and endpoints for
drill-down; overlapping spans are not summed into process CPU time. Existing NATIVE_PHASE_RECORD JSON and NATIVE_TOOLCHAIN lines in the exact
numeric job log are retained as reported context when their schemas/clock/units,
bounds, uniqueness and API time interval validate. Native phase elapsed_ns,
observer overhead and monotonic clock scope remain in raw exports, separate from
API job time. These receipts do not complete missing cache/workload/worker
context or authenticate every selected tool. No phase total or complete
unattributed-time decomposition is fabricated.

Job start minus workflow creation is not labeled runner-queue delay. Dependency,
deployment-approval and scheduling wait components, required-CI completion
latency and critical-path attribution remain unavailable here; #1805 and the
existing admission inventory own that evidence. Runner-seconds are observed
executed-job wall time, not billed or OS-weighted minutes.

## Reruns, reuse, outcomes and replay

The #2052 carried-forward contract compares exact run, original display name,
terminal status/conclusion, start/end and all step name/status/conclusion/
timestamp fields across attempts. One uniquely matching earlier physical
execution is retained once with later aliases. Multiple matching originals are
unresolved; they do not add a sample or cost. A genuinely repeated execution with
different timestamps/steps retains its own ID and runner cost.

Skipped-before-allocation rows have neither duration nor CPU. Executed draft
deferrals are a separate workload. Controller metadata without a runner is
separate. A skipped main-push job creates no new measurement or cost; exact
queue-to-main association still needs the existing reuse receipt and is not
inferred from similar names. Failed/cancelled observations remain in raw exports
and unsuccessful-job cost views. A successful sibling in a failed workflow is
eligible for its own latency population.

Raw observations are append-only. Reimport/reconciliation suppresses identical
physical observations; later metadata enrichment appends a new observation and
retains the original bytes/time/provenance. Conflicting immutable execution
fields reject. Derived views select the latest consistent observation. Candidate
and baseline cannot contain the same physical job. Cost is attributed only to
the original known-hosted execution; aliases, skipped work and unknown intervals
do not silently acquire runner-seconds.

## Storage and publication

The separate ci-timing-history Git branch is the durable store:

| Path | Meaning |
|---|---|
| history/YYYY-MM-DD/N.jsonl | Immutable observation records in append-only shards, each at most 16 MiB |
| history/YYYY-MM-DD/index.json | Versioned bounded shard count, at most 64 |
| history/progress.json | Bounded completed-attempt receipts, pending IDs and reverse sweep cursor |
| history/anchors/KEY.jsonl | First at most twenty physical samples for a fixed cohort/policy anchor |
| manifest.json | Latest collection watermark, collector/policy revisions and gap/overhead counters |
| reports/index.md | Default trusted-main selector, largest absolute median shifts and freshness |
| reports/all.md | Independently selectable PR/queue/manual/main job/matrix/OS/CPU histories |
| reports/series-KEY.md | Raw execution links, baseline/candidate/anchor membership and comparisons |
| reports/recent.jsonl / recent.csv | Reproducible allowlisted exports |

Each batch stages at most 8 MiB of new raw records and reserves API budget for
reporting/publication. The thirty-day reader visits newer shards first and
limits loaded history to half its row/string capacity, reserving the remainder
for new observations. Derived exports retain the newest rows within a separate
byte budget; older omitted shards/rows are counted and remain accessible in raw
history. Output files together are bounded to 128 MiB. Bounds do not erase data
or let an older bundle overwrite a newer one.

Raw shards have no automatic expiry; old Git revisions also retain previous
indexes/reports. Routine refresh reads thirty observation-date days, at most
32,768 rows in a 128 MiB string arena, and renders at most 96 series. Counts and
omissions are visible; exported selected rows remain available when a series
exceeds the rendered bound. The anchor never slides as the rolling window ages.
Git blob/tree/commit identities and a pinned data-head read protect the store;
append publication independently verifies existing raw shard bytes remain an
unchanged prefix. Bounded derived reports do not require an all-history rescan.

The publisher is a distinct job with contents:write; the collector has only
contents/actions read. Both execute the current immutable main workflow checkout.
PR/push tests and previews have no data-write job. Candidate code/reports are
never executed with the publisher's credentials. Only this trusted workflow
run's generated artifact is downloaded; plan schema/revision and producer run/attempt, strict allowlisted paths,
regular files, row bounds, raw append prefixes, collector watermark and data-head
lease are validated before publication. Allowed paths are history/, reports/,
manifest.json and README.md on the data branch. No main ref is written.

Publication creates one commit based on the observed data head and updates the
ref without force. A changed lease/stale watermark fails closed. An exact previously successful
bundle is idempotent only when its producer/revision/watermark and every target
file already match the pinned current data head; it creates no new Git objects. API POSTs are
not blindly retried after uncertain replies; reconciliation must inspect the
resulting data head. Collection failures remain failed observability results
even when valid partial observations can be retained/published. They are not
new required merge checks.

After integration, browse the data branch's reports/index.md. The Actions job
summary links it and exposes collection gaps/API requests/retries. Publication
cannot overwrite a newer report with an older bundle. Current source has not
been deployed merely because the feature PR exists.

Artifacts/logs may expire before backfill. Timing survives when hardware cannot
be joined; unavailable evidence remains explicit. Deletion/redaction requires
an authorized maintainer operation on retained Git history and any exported
copies; normal collection never silently deletes raw history.

## Comparison policy: hosted-ci-cohort-v1

Cohorts separate workflow/job/matrix/invocation, event/branch, workflow blob,
actual CPU, OS/kernel/architecture, effective resources, image, execution
context, toolchain, cache/workload/worker policy and execution kind. Renames,
splits and changed definitions remain visible boundaries. Unknown critical
context cannot create a qualified comparable cohort. Equal test/assertion counts
are not treated as equivalent work, and time/count normalization is not used.

The latest eight successful physical observations form the candidate window.
The preceding at most twenty form the trailing baseline. The fixed initial
anchor contains at most twenty samples and never includes a candidate's physical
job in its own baseline. It exposes cumulative drift even after a trailing
baseline adapts. Anchor replacement is an explicit policy/report operation with
a recorded reason; old raw observations remain.

Every side reports n, median and median absolute deviation (MAD). Nearest-rank
p95 requires twenty observations. The practical absolute floor is the larger of
two API timestamp ticks and three baseline MADs; the candidate variability
allowance is the larger of two ticks and three candidate MADs. This adapts to
observed hosted variability rather than importing retirement thresholds.
Percent change is unavailable below a two-second baseline.

At least eight samples and three UTC date buckets per side are needed for a
directional descriptive signal. A shift exceeding practical floor plus
candidate allowance is a regression/improvement signal; a smaller shift below
the practical floor with no greater candidate variability is no detectable
change. Other cases are inconclusive, insufficient data, or
noncomparable/workload-or-environment changed. These are descriptive operational
signals, not independent p-values, paired experiments, proof of equivalence or
causal attribution. The date-bucket requirement reduces treating correlated
same-run bursts as independent evidence; it does not remove all correlation.
No per-point notifications, best-of-N selection or automatic workload reruns
are performed. Thresholds/baselines are trusted publisher policy.

The v1 hardware producer alone does not supply complete actual toolchain,
cache, worker and workload receipts for every job. Such rows are retained and
their observed differences are shown, but comparison qualification remains
incomplete. The collector must not mark those unknown fields complete to obtain
a signal. This is a remaining delivery/acceptance boundary for #2824, rather
than a claim that the synthetic detector proves live cohort qualification.

## Validation and remaining acceptance

Native synthetic controls cover strict JSON/Unicode/depth/duplicate/finite-value
validation, exact replay/enrichment, malformed identities, escaping, aliased
versus genuine reruns, successful siblings, failed/cancelled exclusions,
resource/CPU/image/toolchain/cache/workload transitions, outliers, slowdown,
improvement, cumulative anchor drift, sparse/short histories and self-baseline
rejection. They never enter the production history store.

The read-only hosted preview collects five authentic existing desktop jobs
from run 37459678394: Linux x86-64, Linux AArch64, macOS AArch64, Windows
x86-64 and Windows AArch64, including their primary merge checkout and CPU
evidence. They are retained in independent read-only review bundles. It retains generated data/reports as review artifacts;
it does not publish candidate output or claim a synthetic regression occurred.
Cloud validation records the actual revision, commands and observed job spans
on the owning PR. Native/collector compilation and real join checks are ordinary
functional validation, not performance-validation tests or 9700X acceptance.

Full delivery still requires live trusted publication after #2766 integration,
complete reusable/dynamic matrix and comparison-context receipts, explicit
queue-to-main reuse association, broader reusable/reuse and recovery-sweep failure fixtures, plus measured
steady-state collection/publication overhead. Native fixtures already cover
paginated inventories, duplicate pages, transient GET recovery, late job
finalization, persisted pending/attempt state, wrong-run publication rejection,
exact replay, stale leases and symlink payloads. Collection wall time, native
peak RSS, API requests/retries and pending/sweep progress are retained in the
manifest; publication wall time/API requests are printed in its job log. The issues remain open until
their own acceptance criteria are met. Missing checks are not called green.

First-party license selection remains unselected per
[LICENSES/README.md](../LICENSES/README.md), tracked by #621. No upstream
implementation, new dependency, database service or SaaS is introduced.
