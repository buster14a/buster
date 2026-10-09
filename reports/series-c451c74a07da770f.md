# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-09T16:37:57Z. Cohort fields and missing values:

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
| 2026-10-08T22:31:00Z | success / executed / physical-execution | 9 | [job 113573516335](https://github.com/buster14a/buster/actions/runs/37853954954/job/113573516335) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853954954/attempts/1) / tested source unavailable |
| 2026-10-09T02:19:24Z | success / executed / physical-execution | 9 | [job 113638294555](https://github.com/buster14a/buster/actions/runs/37874014634/job/113638294555) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874014634/attempts/1) / tested source unavailable |
| 2026-10-09T02:20:57Z | success / executed / physical-execution | 8 | [job 113638692160](https://github.com/buster14a/buster/actions/runs/37874141309/job/113638692160) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874141309/attempts/1) / tested source unavailable |
| 2026-10-09T02:23:27Z | success / executed / physical-execution | 8 | [job 113639339221](https://github.com/buster14a/buster/actions/runs/37874339817/job/113639339221) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874339817/attempts/1) / tested source unavailable |
| 2026-10-09T02:23:39Z | success / executed / physical-execution | 8 | [job 113639390949](https://github.com/buster14a/buster/actions/runs/37874356448/job/113639390949) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37874356448/attempts/1) / tested source unavailable |
| 2026-10-09T09:34:19Z | success / executed / physical-execution | 8 | [job 113759506889](https://github.com/buster14a/buster/actions/runs/37912119514/job/113759506889) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912119514/attempts/1) / tested source unavailable |
| 2026-10-09T09:34:33Z | success / executed / physical-execution | 12 | [job 113759584315](https://github.com/buster14a/buster/actions/runs/37912144642/job/113759584315) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912144642/attempts/1) / tested source unavailable |
| 2026-10-09T09:37:02Z | success / executed / physical-execution | 9 | [job 113760452075](https://github.com/buster14a/buster/actions/runs/37912411308/job/113760452075) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912411308/attempts/1) / tested source unavailable |
| 2026-10-09T09:37:02Z | success / executed / physical-execution | 9 | [job 113760460005](https://github.com/buster14a/buster/actions/runs/37912413895/job/113760460005) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912413895/attempts/1) / tested source unavailable |
| 2026-10-09T09:37:15Z | success / executed / physical-execution | 10 | [job 113760535311](https://github.com/buster14a/buster/actions/runs/37912437161/job/113760535311) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912437161/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:11Z | success / executed / physical-execution | 13 | [job 113760869424](https://github.com/buster14a/buster/actions/runs/37912540313/job/113760869424) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912540313/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:20Z | success / executed / physical-execution | 9 | [job 113760922286](https://github.com/buster14a/buster/actions/runs/37912555963/job/113760922286) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912555963/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:24Z | success / executed / physical-execution | 10 | [job 113760939268](https://github.com/buster14a/buster/actions/runs/37912561159/job/113760939268) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912561159/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:28Z | success / executed / physical-execution | 11 | [job 113760958797](https://github.com/buster14a/buster/actions/runs/37912554576/job/113760958797) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912554576/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:28Z | success / executed / physical-execution | 11 | [job 113760962552](https://github.com/buster14a/buster/actions/runs/37912568488/job/113760962552) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912568488/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:32Z | success / executed / physical-execution | 10 | [job 113760991354](https://github.com/buster14a/buster/actions/runs/37912575031/job/113760991354) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912575031/attempts/1) / tested source unavailable |
| 2026-10-09T09:38:40Z | success / executed / physical-execution | 10 | [job 113761032422](https://github.com/buster14a/buster/actions/runs/37912589579/job/113761032422) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37912589579/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:24Z | success / executed / physical-execution | 8 | [job 113921204975](https://github.com/buster14a/buster/actions/runs/37960318930/job/113921204975) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960318930/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:25Z | success / executed / physical-execution | 10 | [job 113921214122](https://github.com/buster14a/buster/actions/runs/37960321525/job/113921214122) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960321525/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:27Z | success / executed / physical-execution | 10 | [job 113921217236](https://github.com/buster14a/buster/actions/runs/37960322472/job/113921217236) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960322472/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:27Z | success / executed / physical-execution | 7 | [job 113921221779](https://github.com/buster14a/buster/actions/runs/37960324104/job/113921221779) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960324104/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:27Z | success / executed / physical-execution | 9 | [job 113921228994](https://github.com/buster14a/buster/actions/runs/37960325855/job/113921228994) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960325855/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:35Z | success / executed / physical-execution | 10 | [job 113921279729](https://github.com/buster14a/buster/actions/runs/37960341140/job/113921279729) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960341140/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:37Z | success / executed / physical-execution | 7 | [job 113921289244](https://github.com/buster14a/buster/actions/runs/37960343398/job/113921289244) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960343398/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:40Z | success / executed / physical-execution | 10 | [job 113921317440](https://github.com/buster14a/buster/actions/runs/37960351765/job/113921317440) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960351765/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:48Z | success / executed / physical-execution | 7 | [job 113921360500](https://github.com/buster14a/buster/actions/runs/37960364301/job/113921360500) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960364301/attempts/1) / tested source unavailable |
| 2026-10-09T16:36:56Z | success / executed / physical-execution | 9 | [job 113921420354](https://github.com/buster14a/buster/actions/runs/37960381277/job/113921420354) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960381277/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:03Z | success / executed / physical-execution | 8 | [job 113921473491](https://github.com/buster14a/buster/actions/runs/37960396827/job/113921473491) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960396827/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:09Z | success / executed / physical-execution | 9 | [job 113921505000](https://github.com/buster14a/buster/actions/runs/37960406666/job/113921505000) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960406666/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:25Z | success / executed / physical-execution | 7 | [job 113921611118](https://github.com/buster14a/buster/actions/runs/37960438578/job/113921611118) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960438578/attempts/1) / tested source unavailable |
| 2026-10-09T16:37:30Z | success / executed / physical-execution | 11 | [job 113921648496](https://github.com/buster14a/buster/actions/runs/37960449444/job/113921648496) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37960449444/attempts/1) / tested source unavailable |
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
