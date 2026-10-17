# Reconcile trusted retirement requests

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-17T08:50:42Z. Cohort fields and missing values:

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

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=5; median 69.0 s; MAD 5.0 s; 3 UTC date buckets; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 15.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** [job 116624442458](https://github.com/buster14a/buster/actions/runs/38793682494/job/116624442458) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38793682494/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) at 2026-10-15T19:24:48Z; [job 116691704829](https://github.com/buster14a/buster/actions/runs/38813323410/job/116691704829) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38813323410/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) at 2026-10-15T21:54:13Z; [job 116797917129](https://github.com/buster14a/buster/actions/runs/38846435287/job/116797917129) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38846435287/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) at 2026-10-16T03:37:24Z; [job 116888683773](https://github.com/buster14a/buster/actions/runs/38875269101/job/116888683773) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38875269101/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) at 2026-10-16T08:40:38Z; [job 117266034752](https://github.com/buster14a/buster/actions/runs/38988709216/job/117266034752) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38988709216/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) at 2026-10-17T00:57:11Z.

### Cumulative change from the fixed initial anchor

**noncomparable/workload-or-environment changed**. Baseline: n=4; median 69.5 s; MAD 4.5 s; 2 UTC date buckets; p95 unavailable (requires 20). Candidate: n=5; median 69.0 s; MAD 5.0 s; 3 UTC date buckets; p95 unavailable (requires 20). Change: -0.5 s (-0.7%). Observed practical floor 13.5 s; candidate variability allowance 15.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 116127005849](https://github.com/buster14a/buster/actions/runs/38644228085/job/116127005849) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38644228085/attempts/1) / [tested 614353c082fe](https://github.com/buster14a/buster/commit/614353c082fe8fbc5fdaaa57e67d3fd5b106b4dd) at 2026-10-14T21:57:57Z; [job 116255291965](https://github.com/buster14a/buster/actions/runs/38684221743/job/116255291965) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38684221743/attempts/1) / [tested 614353c082fe](https://github.com/buster14a/buster/commit/614353c082fe8fbc5fdaaa57e67d3fd5b106b4dd) at 2026-10-15T04:57:50Z; [job 116431065923](https://github.com/buster14a/buster/actions/runs/38737687240/job/116431065923) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38737687240/attempts/1) / [tested 02a6414e020f](https://github.com/buster14a/buster/commit/02a6414e020f5d549122197c3a7c8407df5bed76) at 2026-10-15T12:50:10Z; [job 116621803005](https://github.com/buster14a/buster/actions/runs/38792916491/job/116621803005) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38792916491/attempts/1) / [tested 266ef083badd](https://github.com/buster14a/buster/commit/266ef083baddd86e3947b4d9c156d1d14a8215d9) at 2026-10-15T19:19:16Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
| 2026-10-15T19:24:48Z | success / executed / physical-execution | 64 | [job 116624442458](https://github.com/buster14a/buster/actions/runs/38793682494/job/116624442458) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38793682494/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) |
| 2026-10-15T21:54:13Z | success / executed / physical-execution | 83 | [job 116691704829](https://github.com/buster14a/buster/actions/runs/38813323410/job/116691704829) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38813323410/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) |
| 2026-10-16T03:37:24Z | success / executed / physical-execution | 72 | [job 116797917129](https://github.com/buster14a/buster/actions/runs/38846435287/job/116797917129) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38846435287/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) |
| 2026-10-16T08:40:38Z | success / executed / physical-execution | 69 | [job 116888683773](https://github.com/buster14a/buster/actions/runs/38875269101/job/116888683773) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38875269101/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) |
| 2026-10-17T00:57:11Z | success / executed / physical-execution | 64 | [job 117266034752](https://github.com/buster14a/buster/actions/runs/38988709216/job/117266034752) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38988709216/attempts/1) / [tested dbc869835b0c](https://github.com/buster14a/buster/commit/dbc869835b0c4d66b591e5b420f7ee4d16cb23ed) |
