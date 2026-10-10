# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-10T01:18:19Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/ci-merge-group-watch.yml |
| job&#95;key | unavailable |
| matrix&#95;identity | unavailable |
| invocation&#95;path | unavailable |
| display&#95;name | watch-merge-group |
| requested&#95;labels | ubuntu-26.04 |
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
| 2026-10-09T16:37:40Z | success / executed / physical-execution | 9 | [job 113921717749](https://github.com/buster14a/buster/actions/runs/37960469068/job/113921717749) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960469068/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:47Z | success / executed / physical-execution | 10 | [job 113921759954](https://github.com/buster14a/buster/actions/runs/37960481973/job/113921759954) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960481973/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:47Z | success / executed / physical-execution | 8 | [job 113921760432](https://github.com/buster14a/buster/actions/runs/37960482038/job/113921760432) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960482038/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:51Z | success / executed / physical-execution | 7 | [job 113921790667](https://github.com/buster14a/buster/actions/runs/37960491042/job/113921790667) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960491042/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:54Z | success / executed / physical-execution | 9 | [job 113921806716](https://github.com/buster14a/buster/actions/runs/37960495616/job/113921806716) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960495616/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:58Z | success / executed / physical-execution | 9 | [job 113921841221](https://github.com/buster14a/buster/actions/runs/37960505638/job/113921841221) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960505638/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:59Z | success / executed / physical-execution | 8 | [job 113921844328](https://github.com/buster14a/buster/actions/runs/37960506421/job/113921844328) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960506421/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:59Z | success / executed / physical-execution | 11 | [job 113921845854](https://github.com/buster14a/buster/actions/runs/37960506850/job/113921845854) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960506850/attempts/1) / tested source unavailable |
| 2026-10-09T16:38:07Z | success / executed / physical-execution | 9 | [job 113921900823](https://github.com/buster14a/buster/actions/runs/37960523367/job/113921900823) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960523367/attempts/1) / tested source unavailable |
| 2026-10-09T16:38:09Z | success / executed / physical-execution | 10 | [job 113921913824](https://github.com/buster14a/buster/actions/runs/37960527048/job/113921913824) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960527048/attempts/1) / tested source unavailable |
| 2026-10-09T16:38:14Z | success / executed / physical-execution | 10 | [job 113921943781](https://github.com/buster14a/buster/actions/runs/37960536096/job/113921943781) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960536096/attempts/1) / tested source unavailable |
| 2026-10-09T16:38:28Z | success / executed / physical-execution | 9 | [job 113922044923](https://github.com/buster14a/buster/actions/runs/37960565850/job/113922044923) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960565850/attempts/1) / tested source unavailable |
| 2026-10-09T16:38:55Z | success / executed / physical-execution | 8 | [job 113922217570](https://github.com/buster14a/buster/actions/runs/37960616518/job/113922217570) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960616518/attempts/1) / tested source unavailable |
| 2026-10-09T21:09:36Z | success / executed / physical-execution | 10 | [job 114027244834](https://github.com/buster14a/buster/actions/runs/37991697993/job/114027244834) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991697993/attempts/1) / tested source unavailable |
| 2026-10-09T21:10:30Z | success / executed / physical-execution | 7 | [job 114027576847](https://github.com/buster14a/buster/actions/runs/37991796560/job/114027576847) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991796560/attempts/1) / tested source unavailable |
| 2026-10-09T21:10:32Z | success / executed / physical-execution | 7 | [job 114027587495](https://github.com/buster14a/buster/actions/runs/37991798878/job/114027587495) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991798878/attempts/1) / tested source unavailable |
| 2026-10-09T21:11:42Z | success / executed / physical-execution | 8 | [job 114028019509](https://github.com/buster14a/buster/actions/runs/37991923326/job/114028019509) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991923326/attempts/1) / tested source unavailable |
| 2026-10-09T21:12:15Z | success / executed / physical-execution | 8 | [job 114028218855](https://github.com/buster14a/buster/actions/runs/37991979423/job/114028218855) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991979423/attempts/1) / tested source unavailable |
| 2026-10-09T21:12:19Z | success / executed / physical-execution | 9 | [job 114028244887](https://github.com/buster14a/buster/actions/runs/37991986621/job/114028244887) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991986621/attempts/1) / tested source unavailable |
| 2026-10-09T21:12:21Z | success / executed / physical-execution | 10 | [job 114028254024](https://github.com/buster14a/buster/actions/runs/37991989064/job/114028254024) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37991989064/attempts/1) / tested source unavailable |
| 2026-10-09T21:12:32Z | success / executed / physical-execution | 9 | [job 114028314063](https://github.com/buster14a/buster/actions/runs/37992005936/job/114028314063) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37992005936/attempts/1) / tested source unavailable |
| 2026-10-09T21:12:35Z | success / executed / physical-execution | 9 | [job 114028337806](https://github.com/buster14a/buster/actions/runs/37992012883/job/114028337806) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37992012883/attempts/1) / tested source unavailable |
| 2026-10-09T21:20:04Z | success / executed / physical-execution | 9 | [job 114031046024](https://github.com/buster14a/buster/actions/runs/37992798033/job/114031046024) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37992798033/attempts/1) / tested source unavailable |
| 2026-10-09T21:20:16Z | success / executed / physical-execution | 10 | [job 114031126653](https://github.com/buster14a/buster/actions/runs/37992814413/job/114031126653) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37992814413/attempts/1) / tested source unavailable |
