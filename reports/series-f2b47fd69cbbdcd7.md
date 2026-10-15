# Windows x86-64 inventory &#40;qualification pending&#41;

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-15T12:52:46Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/external-projects.yml |
| job&#95;key | windows |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | Windows x86-64 inventory &#40;qualification pending&#41; |
| requested&#95;labels | windows-2025 |
| event | push |
| head&#95;branch | main |
| workflow&#95;blob | 783e739fc33823168b4a97b5f3f2ffdc980c9c6d |
| cpu&#95;normalized | AMD EPYC 7763 64-Core Processor |
| os | Windows Server 2025 Datacenter |
| os&#95;version | 24H2 |
| kernel | 26100 |
| machine&#95;arch | x86&#95;64 |
| process&#95;arch | x86&#95;64 |
| effective&#95;cpu | 4 |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | win25-vs2026 |
| runner&#95;image&#95;version | 20260925.250.1 |
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

**insufficient data**. Baseline: n=3; median 40.0 s; MAD 2.0 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 6.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 115600777708](https://github.com/buster14a/buster/actions/runs/38486332667/job/115600777708) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38486332667/attempts/1) / [tested eb4991b9eb14](https://github.com/buster14a/buster/commit/eb4991b9eb14ea18e586128af9b1aae89894126c) at 2026-10-13T23:47:16Z; [job 116094121546](https://github.com/buster14a/buster/actions/runs/38634508919/job/116094121546) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38634508919/attempts/1) / [tested ad8c8373c319](https://github.com/buster14a/buster/commit/ad8c8373c319eeca3e3f233e1c7cd7b22b6fbf9d) at 2026-10-14T20:43:48Z; [job 116115078925](https://github.com/buster14a/buster/actions/runs/38640675623/job/116115078925) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38640675623/attempts/1) / [tested 614353c082fe](https://github.com/buster14a/buster/commit/614353c082fe8fbc5fdaaa57e67d3fd5b106b4dd) at 2026-10-14T21:29:39Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
