# Reconcile exact-group admission

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-14T12:01:13Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/merge-queue-reconcile.yml |
| job&#95;key | reconcile |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | Reconcile exact-group admission |
| requested&#95;labels | ubuntu-latest |
| event | workflow&#95;run |
| head&#95;branch | main |
| workflow&#95;blob | 56f3e3eb3eef433fbad5fc244f2e6d4951f9c19c |
| cpu&#95;normalized | AMD EPYC 9V45 96-Core Processor |
| os | Ubuntu 24.04.5 LTS |
| os&#95;version | 24.04 |
| kernel | 6.17.0-1022-azure |
| machine&#95;arch | x86&#95;64 |
| process&#95;arch | x86&#95;64 |
| effective&#95;cpu | 4 |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | ubuntu24 |
| runner&#95;image&#95;version | 20261004.327.1 |
| execution&#95;context | VM indicated by CPU hypervisor feature; underlying physical host unknown |
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

**insufficient data**. Baseline: n=5; median 52.0 s; MAD 6.0 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 18.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 115013073413](https://github.com/buster14a/buster/actions/runs/38300278812/job/115013073413) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38300278812/attempts/1) / [tested 7bc92b2d8a83](https://github.com/buster14a/buster/commit/7bc92b2d8a83274c016d2b27ad73a8bac0eae853) at 2026-10-12T21:20:19Z; [job 115296703373](https://github.com/buster14a/buster/actions/runs/38385544949/job/115296703373) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38385544949/attempts/1) / [tested 7d6236e08d16](https://github.com/buster14a/buster/commit/7d6236e08d1601e47b67b4f4e6b4af7cb1d1e54b) at 2026-10-13T12:29:00Z; [job 115407205637](https://github.com/buster14a/buster/actions/runs/38414778909/job/115407205637) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38414778909/attempts/1) / [tested c02c5a53148d](https://github.com/buster14a/buster/commit/c02c5a53148d363e8ccd05a295c37025513cf6d2) at 2026-10-13T16:18:12Z; [job 115468093439](https://github.com/buster14a/buster/actions/runs/38435673626/job/115468093439) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38435673626/attempts/1) / [tested 6d4694883990](https://github.com/buster14a/buster/commit/6d4694883990f8043d22794ff38cf070ddbfc134) at 2026-10-13T18:28:45Z; [job 115592115313](https://github.com/buster14a/buster/actions/runs/38472668453/job/115592115313) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38472668453/attempts/1) / [tested 42a66b13235e](https://github.com/buster14a/buster/commit/42a66b13235ebe4fa2564175f48b354e2a268303) at 2026-10-13T23:20:57Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
