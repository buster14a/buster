# Hosted CI timing history

Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.

Collected at 2026-10-16T14:19:41Z. Policy hosted-ci-cohort-v1. 156 active observations, 2455 unknown hardware, 1 API/ingestion gaps, 0 incomplete runs, 70 aliases, 1423 unresolved cost intervals.

Observed hosted runner-seconds: 610446; observed unsuccessful-job runner-seconds: 57329 (168 jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. Dependency/deployment waits and required-CI critical-path attribution remain unavailable.

Reporting bounds: 30 observation-date shards / 32768 rows / 96 rendered series; 4684 rows outside the rendered series bound remain in exports. Raw history is retained separately from these bounded derived reports.

Bounded view omitted 0 older shards and 4961 older loaded rows; their raw records remain on the data branch.

[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/buster14a/buster/blob/main/docs/ci-timing-history.md)

Default view: trusted main push observations only. [All events, PRs and queues](all.md). The coverage totals and exports above include all selected events; the selector below filters main. Native report filters select job, OS, CPU, branch, revision and UTC date range.

| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |
|---|---|---|---:|---:|---|
| API migration policy / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-3c35a6bac3be55cb.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 14 | [series](series-4494fc1d80963d60.md) |
| Record the compiler benchmark request / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 2 | 2 | [series](series-c896ad4742c2d6cf.md) |
| Show the queued compiler benchmark check / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-5d5a0b88bc232db5.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 13 | [series](series-e179d4e27c9210c7.md) |
| Benchmark service workflow policy / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-6f1fc262416a9e2c.md) |
| Open the native-retirement catch-up request / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-6e498da088dd02a8.md) |
| Main integration admission / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-e44a78de2bf3ce0b.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 14 | [series](series-1df5c2f22286ee08.md) |
| Reconcile exact-group admission / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-dc64e35cc80eb48b.md) |
| Reconcile trusted retirement requests / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-b8b216bfefc124ef.md) |
| Pages validation and artifact /  | unknown OS / unknown CPU | push / main | 0 | 10 | [series](series-fe39ffa1f1875ff1.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 10 | [series](series-3e4ea91afe05e1e2.md) |

## Largest observed main time changes

Ordered by absolute median seconds per physical execution; candidate frequency is shown separately. These descriptive differences include improvements and slowdowns. Missing context is not a qualified signal, and no required-CI critical-path impact is inferred.

| Series | Median change s | Change % | Candidate executions | State |
|---|---:|---:|---:|---|

Last selected execution completed: 2026-10-16T13:59:05Z; collection lag 1236 s. Pending runs: 1; reverse-sweep cursor 2026-10-08T17:36:47Z. The cursor measures discovery progress, not evidence of complete historical coverage.
