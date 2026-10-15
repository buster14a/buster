# Reconcile trusted retirement requests

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-15T17:47:58Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/native-retirement-automation.yml |
| job&#95;key | reconcile |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | Reconcile trusted retirement requests |
| requested&#95;labels | ubuntu-26.04 |
| event | workflow&#95;run |
| head&#95;branch | main |
| workflow&#95;blob | 35f0397b6fe481faaace4d97599ca48166992e81 |
| cpu&#95;normalized | AMD EPYC 7763 64-Core Processor |
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

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=1; median 61.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** [job 116431065923](https://github.com/buster14a/buster/actions/runs/38737687240/job/116431065923) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38737687240/attempts/1) / [tested 02a6414e020f](https://github.com/buster14a/buster/commit/02a6414e020f5d549122197c3a7c8407df5bed76) at 2026-10-15T12:50:10Z.

### Cumulative change from the fixed initial anchor

**noncomparable/workload-or-environment changed**. Baseline: n=2; median 69.5 s; MAD 3.5 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=1; median 61.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change: -8.5 s (-12.2%). Observed practical floor 10.5 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 116127005849](https://github.com/buster14a/buster/actions/runs/38644228085/job/116127005849) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38644228085/attempts/1) / [tested 614353c082fe](https://github.com/buster14a/buster/commit/614353c082fe8fbc5fdaaa57e67d3fd5b106b4dd) at 2026-10-14T21:57:57Z; [job 116255291965](https://github.com/buster14a/buster/actions/runs/38684221743/job/116255291965) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38684221743/attempts/1) / [tested 614353c082fe](https://github.com/buster14a/buster/commit/614353c082fe8fbc5fdaaa57e67d3fd5b106b4dd) at 2026-10-15T04:57:50Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-15T12:50:10Z | success / executed / physical-execution | 61 | [job 116431065923](https://github.com/buster14a/buster/actions/runs/38737687240/job/116431065923) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38737687240/attempts/1) / [tested 02a6414e020f](https://github.com/buster14a/buster/commit/02a6414e020f5d549122197c3a7c8407df5bed76) |
