# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-15T21:26:47Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/ci-merge-group-watch.yml |
| job&#95;key | watch-merge-group |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | watch-merge-group |
| requested&#95;labels | ubuntu-26.04 |
| event | workflow&#95;run |
| head&#95;branch | main |
| workflow&#95;blob | 079df8228d8ce6d829f095027a3303fb98cfe406 |
| cpu&#95;normalized | INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C |
| os | Ubuntu 26.04.1 LTS |
| os&#95;version | 26.04 |
| kernel | 7.0.0-1017-azure |
| machine&#95;arch | x86&#95;64 |
| process&#95;arch | x86&#95;64 |
| effective&#95;cpu | 4 |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | ubuntu26 |
| runner&#95;image&#95;version | 20261012.163.1 |
| execution&#95;context | VM indicated by CPU hypervisor feature; underlying physical host unknown |
| toolchain&#95;identity | unavailable |
| cache&#95;state | unavailable |
| workload&#95;identity | unavailable |
| worker&#95;budget | unavailable |
| execution&#95;kind | executed |

Context qualification: incomplete-producer-context. Unknown critical context prevents change signals; raw points remain selectable. Workflow/blob, CPU/resources, image, cache, toolchain and workload changes create distinct boundaries. The tested SHA labels a point, rather than defining a separate cohort.

### Trailing comparison

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=3; median 28.0 s; MAD 3.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 9.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** [job 116612997422](https://github.com/buster14a/buster/actions/runs/38790353954/job/116612997422) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38790353954/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) at 2026-10-15T19:00:44Z; [job 116616213277](https://github.com/buster14a/buster/actions/runs/38791292614/job/116616213277) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38791292614/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) at 2026-10-15T19:07:28Z; [job 116621804045](https://github.com/buster14a/buster/actions/runs/38792916673/job/116621804045) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38792916673/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) at 2026-10-15T19:19:17Z.

### Cumulative change from the fixed initial anchor

**noncomparable/workload-or-environment changed**. Baseline: n=1; median 19.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Candidate: n=3; median 28.0 s; MAD 3.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change: +9.0 s (+47.4%). Observed practical floor 2.0 s; candidate variability allowance 9.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 116106651449](https://github.com/buster14a/buster/actions/runs/38638190753/job/116106651449) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38638190753/attempts/1) / [tested ad8c8373c319](https://github.com/buster14a/buster/commit/ad8c8373c319eeca3e3f233e1c7cd7b22b6fbf9d) at 2026-10-14T21:10:42Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-15T19:00:44Z | success / executed / physical-execution | 28 | [job 116612997422](https://github.com/buster14a/buster/actions/runs/38790353954/job/116612997422) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38790353954/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) |
| 2026-10-15T19:07:28Z | success / executed / physical-execution | 25 | [job 116616213277](https://github.com/buster14a/buster/actions/runs/38791292614/job/116616213277) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38791292614/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) |
| 2026-10-15T19:19:17Z | success / executed / physical-execution | 33 | [job 116621804045](https://github.com/buster14a/buster/actions/runs/38792916673/job/116621804045) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38792916673/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) |
