# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-08T17:36:47Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/ci-merge-group-watch.yml |
| job&#95;key | unavailable |
| matrix&#95;identity | unavailable |
| invocation&#95;path | unavailable |
| display&#95;name | watch-merge-group |
| requested&#95;labels | ubuntu-26.04 |
| event | workflow&#95;run |
| head&#95;branch | main |
| workflow&#95;blob | unavailable |
| cpu&#95;normalized | unavailable |
| os | unavailable |
| os&#95;version | unavailable |
| kernel | unavailable |
| machine&#95;arch | unavailable |
| process&#95;arch | unavailable |
| effective&#95;cpu | unavailable |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | unavailable |
| runner&#95;image&#95;version | unavailable |
| execution&#95;context | unavailable |
| toolchain&#95;identity | unavailable |
| cache&#95;state | unavailable |
| workload&#95;identity | unavailable |
| worker&#95;budget | unavailable |
| execution&#95;kind | executed |

Context qualification: incomplete-producer-context. Unknown critical context prevents change signals; raw points remain selectable. Workflow/blob, CPU/resources, image, cache, toolchain and workload changes create distinct boundaries. The tested SHA labels a point, rather than defining a separate cohort.

### Trailing comparison

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** unavailable.

### Cumulative change from the fixed initial anchor

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** unavailable.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-08T17:23:36Z | success / executed / physical-execution | 8 | [job 113445320697](https://github.com/buster14a/buster/actions/runs/37816189485/job/113445320697) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816189485/attempts/1) / tested source unavailable |
| 2026-10-08T17:23:42Z | success / executed / physical-execution | 9 | [job 113445358379](https://github.com/buster14a/buster/actions/runs/37816201078/job/113445358379) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816201078/attempts/1) / tested source unavailable |
| 2026-10-08T17:23:47Z | success / executed / physical-execution | 8 | [job 113445388184](https://github.com/buster14a/buster/actions/runs/37816209324/job/113445388184) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816209324/attempts/1) / tested source unavailable |
| 2026-10-08T17:24:22Z | success / executed / physical-execution | 8 | [job 113445656562](https://github.com/buster14a/buster/actions/runs/37816288669/job/113445656562) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816288669/attempts/1) / tested source unavailable |
| 2026-10-08T17:24:50Z | success / executed / physical-execution | 10 | [job 113445868253](https://github.com/buster14a/buster/actions/runs/37816351987/job/113445868253) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816351987/attempts/1) / tested source unavailable |
| 2026-10-08T17:25:36Z | success / executed / physical-execution | 10 | [job 113446205099](https://github.com/buster14a/buster/actions/runs/37816452670/job/113446205099) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816452670/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:28Z | success / executed / physical-execution | 12 | [job 113448364945](https://github.com/buster14a/buster/actions/runs/37817088035/job/113448364945) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817088035/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:32Z | success / executed / physical-execution | 13 | [job 113448379761](https://github.com/buster14a/buster/actions/runs/37817092675/job/113448379761) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817092675/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:50Z | success / executed / physical-execution | 56 | [job 113448529186](https://github.com/buster14a/buster/actions/runs/37817137630/job/113448529186) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817137630/attempts/1) / tested source unavailable |
| 2026-10-08T17:31:18Z | success / executed / physical-execution | 9 | [job 113448740730](https://github.com/buster14a/buster/actions/runs/37817198143/job/113448740730) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817198143/attempts/1) / tested source unavailable |
| 2026-10-08T17:32:06Z | success / executed / physical-execution | 11 | [job 113449090271](https://github.com/buster14a/buster/actions/runs/37817302937/job/113449090271) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817302937/attempts/1) / tested source unavailable |
| 2026-10-08T17:32:15Z | success / executed / physical-execution | 10 | [job 113449156547](https://github.com/buster14a/buster/actions/runs/37817321545/job/113449156547) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817321545/attempts/1) / tested source unavailable |
