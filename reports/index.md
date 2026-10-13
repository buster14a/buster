# Hosted CI timing history

Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.

Collected at 2026-10-13T10:55:24Z. Policy hosted-ci-cohort-v1. 177 active observations, 1626 unknown hardware, 1 API/ingestion gaps, 0 incomplete runs, 70 aliases, 1108 unresolved cost intervals.

Observed hosted runner-seconds: 205819; observed unsuccessful-job runner-seconds: 10439 (72 jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. Dependency/deployment waits and required-CI critical-path attribution remain unavailable.

Reporting bounds: 30 observation-date shards / 32768 rows / 96 rendered series; 2390 rows outside the rendered series bound remain in exports. Raw history is retained separately from these bounded derived reports.

Bounded view omitted 0 older shards and 2426 older loaded rows; their raw records remain on the data branch.

[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/buster14a/buster/blob/main/docs/ci-timing-history.md)

Default view: trusted main push observations only. [All events, PRs and queues](all.md). The coverage totals and exports above include all selected events; the selector below filters main. Native report filters select job, OS, CPU, branch, revision and UTC date range.

| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |
|---|---|---|---:|---:|---|
| macOS AArch64 inventory &#40;qualification pending&#41; / non-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 2 | 2 | [series](series-cc1106da043825cf.md) |
| Linux x86-64 external projects / non-matrix | Ubuntu 26.04.1 LTS / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 0 | 2 | [series](series-f3f90495a24c9103.md) |
| Windows x86-64 inventory &#40;qualification pending&#41; / non-matrix | Windows Server 2025 Datacenter / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-bc4c14f60a362e5e.md) |
| Linux x86-64 bootstrap evidence / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-45419f71ad96fa6a.md) |
| Reconcile trusted retirement requests / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-659dd9bdc8513655.md) |
| Record the compiler benchmark request / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V45 96-Core Processor | push / main | 2 | 2 | [series](series-14061ff422275937.md) |
| Show the queued compiler benchmark check / non-matrix | Ubuntu 24.04.5 LTS / Intel&#40;R&#41; Xeon&#40;R&#41; 6973P-C | push / main | 1 | 1 | [series](series-cae1070a29eec6c4.md) |
| Open the native-retirement catch-up request / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-33e938f1f4be6999.md) |

## Largest observed main time changes

Ordered by absolute median seconds per physical execution; candidate frequency is shown separately. These descriptive differences include improvements and slowdowns. Missing context is not a qualified signal, and no required-CI critical-path impact is inferred.

| Series | Median change s | Change % | Candidate executions | State |
|---|---:|---:|---:|---|

Last selected execution completed: 2026-10-13T10:53:56Z; collection lag 88 s. Pending runs: 24; reverse-sweep cursor 2026-10-08T17:36:47Z. The cursor measures discovery progress, not evidence of complete historical coverage.
