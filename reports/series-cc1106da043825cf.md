# macOS AArch64 inventory &#40;qualification pending&#41;

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-13T17:09:19Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/external-projects.yml |
| job&#95;key | macos |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | macOS AArch64 inventory &#40;qualification pending&#41; |
| requested&#95;labels | macos-26 |
| event | push |
| head&#95;branch | main |
| workflow&#95;blob | 06261f3123c913dc5ffb0ffe156e63549eb8338b |
| cpu&#95;normalized | Apple M1 &#40;Virtual&#41; |
| os | macOS |
| os&#95;version | 26.6.2 |
| kernel | 25.6.0 |
| machine&#95;arch | arm64 |
| process&#95;arch | aarch64 |
| effective&#95;cpu | unavailable |
| cpu&#95;quota | unavailable |
| memory&#95;limit | unavailable |
| runner&#95;image | macos26 |
| runner&#95;image&#95;version | 20260907.0351.1 |
| execution&#95;context | unavailable |
| toolchain&#95;identity | unavailable |
| cache&#95;state | unavailable |
| workload&#95;identity | unavailable |
| worker&#95;budget | unavailable |
| execution&#95;kind | executed |

Context qualification: incomplete-producer-context. Unknown critical context prevents change signals; raw points remain selectable. Workflow/blob, CPU/resources, image, cache, toolchain and workload changes create distinct boundaries. The tested SHA labels a point, rather than defining a separate cohort.

### Trailing comparison

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=1; median 28.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** [job 115407737326](https://github.com/buster14a/buster/actions/runs/38429525930/job/115407737326) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38429525930/attempts/1) / [tested 2d610db56206](https://github.com/buster14a/buster/commit/2d610db56206ed811439e0c01962950cdd82f227) at 2026-10-13T16:19:29Z.

### Cumulative change from the fixed initial anchor

**noncomparable/workload-or-environment changed**. Baseline: n=4; median 22.5 s; MAD 1.5 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=1; median 28.0 s; MAD 0.0 s; 1 UTC date buckets; p95 unavailable (requires 20). Change: +5.5 s (+24.4%). Observed practical floor 4.5 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 114981459601](https://github.com/buster14a/buster/actions/runs/38300341847/job/114981459601) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38300341847/attempts/1) / [tested 44bf166edd33](https://github.com/buster14a/buster/commit/44bf166edd33f2321cb9ab6a202a0098d99e591e) at 2026-10-12T19:51:02Z; [job 115256548904](https://github.com/buster14a/buster/actions/runs/38385619066/job/115256548904) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38385619066/attempts/1) / [tested 5899677b83d5](https://github.com/buster14a/buster/commit/5899677b83d5c2123891cfd3ceb091e0c749697b) at 2026-10-13T10:46:19Z; [job 115273241435](https://github.com/buster14a/buster/actions/runs/38390622167/job/115273241435) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38390622167/attempts/1) / [tested 9a9d1f56588e](https://github.com/buster14a/buster/commit/9a9d1f56588ee278c831e55b64dc2c5aabb8ac20) at 2026-10-13T11:30:12Z; [job 115369441359](https://github.com/buster14a/buster/actions/runs/38418566430/job/115369441359) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38418566430/attempts/1) / [tested 23de83a8c802](https://github.com/buster14a/buster/commit/23de83a8c802e7f3052ee88bc311a97c0bd631c2) at 2026-10-13T15:02:23Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-13T16:19:29Z | success / executed / physical-execution | 28 | [job 115407737326](https://github.com/buster14a/buster/actions/runs/38429525930/job/115407737326) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38429525930/attempts/1) / [tested 2d610db56206](https://github.com/buster14a/buster/commit/2d610db56206ed811439e0c01962950cdd82f227) |
