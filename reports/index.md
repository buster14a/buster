# Hosted CI timing history

Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.

Collected at 2026-10-15T17:47:58Z. Policy hosted-ci-cohort-v1. 144 active observations, 2288 unknown hardware, 1 API/ingestion gaps, 0 incomplete runs, 70 aliases, 1372 unresolved cost intervals.

Observed hosted runner-seconds: 541861; observed unsuccessful-job runner-seconds: 50731 (161 jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. Dependency/deployment waits and required-CI critical-path attribution remain unavailable.

Reporting bounds: 30 observation-date shards / 32768 rows / 96 rendered series; 4092 rows outside the rendered series bound remain in exports. Raw history is retained separately from these bounded derived reports.

Bounded view omitted 0 older shards and 4486 older loaded rows; their raw records remain on the data branch.

[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/buster14a/buster/blob/main/docs/ci-timing-history.md)

Default view: trusted main push observations only. [All events, PRs and queues](all.md). The coverage totals and exports above include all selected events; the selector below filters main. Native report filters select job, OS, CPU, branch, revision and UTC date range.

| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |
|---|---|---|---:|---:|---|
| Hosted ordinary compiler bridge proof / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-dcdd48ee362f256d.md) |
| Hosted TCC native proofs / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-63f2f448f64573ee.md) |
| Hosted compiler closure Utility proof / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-3e38183b6283bb86.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 13 | [series](series-58dd6628997072af.md) |
| Canonical TCC bootstrap / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-8abe79bc9c10c542.md) |
| Linux x86-64 bootstrap evidence / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 3 | 3 | [series](series-aaf47bc23866c195.md) |
| GPU Linux consumers / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V45 96-Core Processor | push / main | 4 | 4 | [series](series-2c5128a4405c11e4.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 13 | [series](series-4ec16058bf5d4207.md) |
| GPU Metal consumer /  | unknown OS / unknown CPU | push / main | 0 | 14 | [series](series-5055dc4fc114973a.md) |
| macOS AArch64 inventory &#40;qualification pending&#41; / non-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 3 | 3 | [series](series-90951964e10474ae.md) |
| Windows x86-64 inventory &#40;qualification pending&#41; / non-matrix | Windows Server 2025 Datacenter / AMD EPYC 7763 64-Core Processor | push / main | 3 | 3 | [series](series-f2b47fd69cbbdcd7.md) |
| Linux x86-64 external projects / non-matrix | Ubuntu 26.04.1 LTS / Intel&#40;R&#41; Xeon&#40;R&#41; Platinum 8370C CPU @ 2.80GHz | push / main | 0 | 1 | [series](series-deb73717b96dbe66.md) |

## Largest observed main time changes

Ordered by absolute median seconds per physical execution; candidate frequency is shown separately. These descriptive differences include improvements and slowdowns. Missing context is not a qualified signal, and no required-CI critical-path impact is inferred.

| Series | Median change s | Change % | Candidate executions | State |
|---|---:|---:|---:|---|

Last selected execution completed: 2026-10-15T17:44:08Z; collection lag 230 s. Pending runs: 1; reverse-sweep cursor 2026-10-08T17:36:47Z. The cursor measures discovery progress, not evidence of complete historical coverage.
