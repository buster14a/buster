# Exact merge-tree preflight

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-08T22:30:31Z. Cohort fields and missing values:

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
| 2026-10-08T17:20:03Z | success / executed / physical-execution | 46 | [job 113443758930](https://github.com/buster14a/buster/actions/runs/37815730460/job/113443758930) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37815730460/attempts/1) / tested source unavailable |
| 2026-10-08T17:20:37Z | success / executed / physical-execution | 42 | [job 113444017422](https://github.com/buster14a/buster/actions/runs/37815809332/job/113444017422) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37815809332/attempts/1) / tested source unavailable |
| 2026-10-08T17:24:56Z | success / executed / physical-execution | 29 | [job 113445914234](https://github.com/buster14a/buster/actions/runs/37816365757/job/113445914234) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816365757/attempts/1) / tested source unavailable |
| 2026-10-08T17:26:32Z | success / executed / physical-execution | 33 | [job 113446633875](https://github.com/buster14a/buster/actions/runs/37816577415/job/113446633875) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816577415/attempts/1) / tested source unavailable |
| 2026-10-08T17:27:17Z | success / executed / physical-execution | 38 | [job 113446968364](https://github.com/buster14a/buster/actions/runs/37816675646/job/113446968364) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816675646/attempts/1) / tested source unavailable |
| 2026-10-08T17:29:43Z | success / executed / physical-execution | 37 | [job 113448023479](https://github.com/buster14a/buster/actions/runs/37816986461/job/113448023479) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816986461/attempts/1) / tested source unavailable |
| 2026-10-08T17:29:51Z | success / executed / physical-execution | 35 | [job 113448086272](https://github.com/buster14a/buster/actions/runs/37817003997/job/113448086272) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817003997/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:29Z | success / executed / physical-execution | 44 | [job 113448366557](https://github.com/buster14a/buster/actions/runs/37817088588/job/113448366557) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817088588/attempts/1) / tested source unavailable |
| 2026-10-08T17:32:02Z | success / executed / physical-execution | 29 | [job 113449064800](https://github.com/buster14a/buster/actions/runs/37817295013/job/113449064800) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817295013/attempts/1) / tested source unavailable |
| 2026-10-08T17:36:11Z | success / executed / physical-execution | 35 | [job 113450901421](https://github.com/buster14a/buster/actions/runs/37817833585/job/113450901421) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817833585/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:32Z | success / executed / physical-execution | 39 | [job 113573006523](https://github.com/buster14a/buster/actions/runs/37853794776/job/113573006523) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853794776/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:42Z | success / executed / physical-execution | 34 | [job 113573065018](https://github.com/buster14a/buster/actions/runs/37853812582/job/113573065018) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853812582/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:58Z | success / executed / physical-execution | 35 | [job 113573154362](https://github.com/buster14a/buster/actions/runs/37853839793/job/113573154362) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853839793/attempts/1) / tested source unavailable |
