# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-16T04:36:23Z. Cohort fields and missing values:

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
| cpu&#95;normalized | AMD EPYC 9V74 80-Core Processor |
| os | Ubuntu 26.04.1 LTS |
| os&#95;version | 26.04 |
| kernel | 7.0.0-1012-azure |
| machine&#95;arch | x86&#95;64 |
| process&#95;arch | x86&#95;64 |
| effective&#95;cpu | 4 |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | ubuntu26 |
| runner&#95;image&#95;version | 20260927.149.1 |
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

**insufficient data**. Baseline: n=7; median 16.0 s; MAD 3.0 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 9.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 114353210798](https://github.com/buster14a/buster/actions/runs/38099807133/job/114353210798) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38099807133/attempts/1) / [tested 9516f631b6a3](https://github.com/buster14a/buster/commit/9516f631b6a30943b90a77816708407a0a04b904) at 2026-10-11T00:50:52Z; [job 114394857455](https://github.com/buster14a/buster/actions/runs/38113901267/job/114394857455) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38113901267/attempts/1) / [tested a67057643a40](https://github.com/buster14a/buster/commit/a67057643a40c0ab99931849eb30053a2b870fcc) at 2026-10-11T05:09:54Z; [job 114395248438](https://github.com/buster14a/buster/actions/runs/38114037035/job/114395248438) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38114037035/attempts/1) / [tested a67057643a40](https://github.com/buster14a/buster/commit/a67057643a40c0ab99931849eb30053a2b870fcc) at 2026-10-11T05:12:23Z; [job 114488885677](https://github.com/buster14a/buster/actions/runs/38145843161/job/114488885677) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38145843161/attempts/1) / [tested d0a760144efe](https://github.com/buster14a/buster/commit/d0a760144efefca8c105898dbe169f8e7483391a) at 2026-10-11T14:06:33Z; [job 114489207644](https://github.com/buster14a/buster/actions/runs/38145949755/job/114489207644) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38145949755/attempts/1) / [tested cd0ed6b2ca8e](https://github.com/buster14a/buster/commit/cd0ed6b2ca8e4104de2b9e762403299b8bc3eed6) at 2026-10-11T14:08:09Z; [job 116105168394](https://github.com/buster14a/buster/actions/runs/38637752943/job/116105168394) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38637752943/attempts/1) / [tested ad8c8373c319](https://github.com/buster14a/buster/commit/ad8c8373c319eeca3e3f233e1c7cd7b22b6fbf9d) at 2026-10-14T21:07:29Z; [job 116110589234](https://github.com/buster14a/buster/actions/runs/38639345525/job/116110589234) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38639345525/attempts/1) / [tested ad8c8373c319](https://github.com/buster14a/buster/commit/ad8c8373c319eeca3e3f233e1c7cd7b22b6fbf9d) at 2026-10-14T21:19:26Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
