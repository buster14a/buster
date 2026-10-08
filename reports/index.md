# Hosted CI timing history

Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.

Collected at 2026-10-08T17:36:47Z. Policy hosted-ci-cohort-v1. 111 active observations, 110 unknown hardware, 3 API/ingestion gaps, 2 incomplete runs, 0 aliases, 93 unresolved cost intervals.

Observed hosted runner-seconds: 25; observed unsuccessful-job runner-seconds: 0 (0 jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. Dependency/deployment waits and required-CI critical-path attribution remain unavailable.

Reporting bounds: 30 observation-date shards / 32768 rows / 96 rendered series; 0 rows outside the rendered series bound remain in exports. Raw history is retained separately from these bounded derived reports.

Bounded view omitted 0 older shards and 0 older loaded rows; their raw records remain on the data branch.

[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/buster14a/buster/blob/main/docs/ci-timing-history.md)

Default view: trusted main push observations only. [All events, PRs and queues](all.md). The coverage totals and exports above include all selected events; the selector below filters main. Native report filters select job, OS, CPU, branch, revision and UTC date range.

| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |
|---|---|---|---:|---:|---|

## Largest observed main time changes

Ordered by absolute median seconds per physical execution; candidate frequency is shown separately. These descriptive differences include improvements and slowdowns. Missing context is not a qualified signal, and no required-CI critical-path impact is inferred.

| Series | Median change s | Change % | Candidate executions | State |
|---|---:|---:|---:|---|

Last selected execution completed: 2026-10-08T17:36:46Z; collection lag 1 s. Pending runs: 10; reverse-sweep cursor not started. The cursor measures discovery progress, not evidence of complete historical coverage.
