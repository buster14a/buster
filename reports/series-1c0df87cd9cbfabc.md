# Preflight regression tests

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-08T17:36:47Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/merge-conflict-preflight-regression.yml |
| job&#95;key | regression |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | Preflight regression tests |
| requested&#95;labels | ubuntu-latest |
| event | pull&#95;request |
| head&#95;branch | codex/2758-ci-machine-specifications |
| workflow&#95;blob | 3ec9525f3e4d11950d443cfc271419aa672a3a5c |
| cpu&#95;normalized | Intel&#40;R&#41; Xeon&#40;R&#41; 6973P-C |
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

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=1; median 25.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** [job 113443781809](https://github.com/buster14a/buster/actions/runs/37815737465/job/113443781809) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37815737465/attempts/1) / [tested 62562876b840](https://github.com/buster14a/buster/commit/62562876b84022ab3ee4b7de4b6acfb9334b2874) at 2026-10-08T17:20:05Z.

### Cumulative change from the fixed initial anchor

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=1; median 25.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** unavailable.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-08T17:20:05Z | success / executed / physical-execution | 25 | [job 113443781809](https://github.com/buster14a/buster/actions/runs/37815737465/job/113443781809) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37815737465/attempts/1) / [tested 62562876b840](https://github.com/buster14a/buster/commit/62562876b84022ab3ee4b7de4b6acfb9334b2874) |
