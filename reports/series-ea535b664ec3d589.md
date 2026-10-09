# Exact merge-tree preflight

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-09T16:37:57Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/merge-conflict-preflight.yml |
| job&#95;key | unavailable |
| matrix&#95;identity | unavailable |
| invocation&#95;path | unavailable |
| display&#95;name | Exact merge-tree preflight |
| requested&#95;labels | ubuntu-latest |
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
| 2026-10-08T22:30:55Z | success / executed / physical-execution | 36 | [job 113573490861](https://github.com/buster14a/buster/actions/runs/37853946932/job/113573490861) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853946932/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:59Z | success / executed / physical-execution | 33 | [job 113573514183](https://github.com/buster14a/buster/actions/runs/37853954543/job/113573514183) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853954543/attempts/1) / tested source unavailable |
| 2026-10-08T22:31:03Z | success / executed / physical-execution | 36 | [job 113573536621](https://github.com/buster14a/buster/actions/runs/37853961095/job/113573536621) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853961095/attempts/1) / tested source unavailable |
| 2026-10-09T02:19:37Z | success / executed / physical-execution | 31 | [job 113638345750](https://github.com/buster14a/buster/actions/runs/37874031393/job/113638345750) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874031393/attempts/1) / tested source unavailable |
| 2026-10-09T02:20:55Z | success / executed / physical-execution | 35 | [job 113638687484](https://github.com/buster14a/buster/actions/runs/37874139773/job/113638687484) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874139773/attempts/1) / tested source unavailable |
| 2026-10-09T02:22:01Z | success / executed / physical-execution | 34 | [job 113638973846](https://github.com/buster14a/buster/actions/runs/37874227128/job/113638973846) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874227128/attempts/1) / tested source unavailable |
| 2026-10-09T02:25:44Z | success / executed / physical-execution | 35 | [job 113639927951](https://github.com/buster14a/buster/actions/runs/37874524832/job/113639927951) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874524832/attempts/1) / tested source unavailable |
| 2026-10-09T02:26:23Z | success / executed / physical-execution | 33 | [job 113640090126](https://github.com/buster14a/buster/actions/runs/37874575401/job/113640090126) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874575401/attempts/1) / tested source unavailable |
| 2026-10-09T02:27:15Z | success / executed / physical-execution | 38 | [job 113640294425](https://github.com/buster14a/buster/actions/runs/37874639848/job/113640294425) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874639848/attempts/1) / tested source unavailable |
| 2026-10-09T02:30:21Z | success / executed / physical-execution | 37 | [job 113641071678](https://github.com/buster14a/buster/actions/runs/37874882760/job/113641071678) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874882760/attempts/1) / tested source unavailable |
| 2026-10-09T02:30:25Z | success / executed / physical-execution | 37 | [job 113641092254](https://github.com/buster14a/buster/actions/runs/37874889583/job/113641092254) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874889583/attempts/1) / tested source unavailable |
| 2026-10-09T09:34:00Z | success / executed / physical-execution | 32 | [job 113759402688](https://github.com/buster14a/buster/actions/runs/37912087985/job/113759402688) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912087985/attempts/1) / tested source unavailable |
| 2026-10-09T09:34:06Z | success / executed / physical-execution | 33 | [job 113759433374](https://github.com/buster14a/buster/actions/runs/37912097694/job/113759433374) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912097694/attempts/1) / tested source unavailable |
| 2026-10-09T09:34:11Z | success / executed / physical-execution | 25 | [job 113759463494](https://github.com/buster14a/buster/actions/runs/37912106688/job/113759463494) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912106688/attempts/1) / tested source unavailable |
| 2026-10-09T09:36:27Z | success / executed / physical-execution | 38 | [job 113760256379](https://github.com/buster14a/buster/actions/runs/37912351628/job/113760256379) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912351628/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:30Z | success / executed / physical-execution | 34 | [job 113760979997](https://github.com/buster14a/buster/actions/runs/37912573612/job/113760979997) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912573612/attempts/1) / tested source unavailable |
| 2026-10-09T09:39:11Z | success / executed / physical-execution | 32 | [job 113761216256](https://github.com/buster14a/buster/actions/runs/37912645452/job/113761216256) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912645452/attempts/1) / tested source unavailable |
