# watch-merge-group

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-09T09:39:53Z. Cohort fields and missing values:

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
| 2026-10-08T17:25:36Z | success / executed / physical-execution | 10 | [job 113446205099](https://github.com/buster14a/buster/actions/runs/37816452670/job/113446205099) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37816452670/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:28Z | success / executed / physical-execution | 12 | [job 113448364945](https://github.com/buster14a/buster/actions/runs/37817088035/job/113448364945) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817088035/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:32Z | success / executed / physical-execution | 13 | [job 113448379761](https://github.com/buster14a/buster/actions/runs/37817092675/job/113448379761) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817092675/attempts/1) / tested source unavailable |
| 2026-10-08T17:30:50Z | success / executed / physical-execution | 56 | [job 113448529186](https://github.com/buster14a/buster/actions/runs/37817137630/job/113448529186) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817137630/attempts/1) / tested source unavailable |
| 2026-10-08T17:31:18Z | success / executed / physical-execution | 9 | [job 113448740730](https://github.com/buster14a/buster/actions/runs/37817198143/job/113448740730) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817198143/attempts/1) / tested source unavailable |
| 2026-10-08T17:32:06Z | success / executed / physical-execution | 11 | [job 113449090271](https://github.com/buster14a/buster/actions/runs/37817302937/job/113449090271) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817302937/attempts/1) / tested source unavailable |
| 2026-10-08T17:32:15Z | success / executed / physical-execution | 10 | [job 113449156547](https://github.com/buster14a/buster/actions/runs/37817321545/job/113449156547) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37817321545/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:35Z | success / executed / physical-execution | 7 | [job 113573018358](https://github.com/buster14a/buster/actions/runs/37853798419/job/113573018358) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853798419/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:46Z | success / executed / physical-execution | 11 | [job 113573084595](https://github.com/buster14a/buster/actions/runs/37853818276/job/113573084595) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853818276/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:48Z | success / executed / physical-execution | 11 | [job 113573095077](https://github.com/buster14a/buster/actions/runs/37853821406/job/113573095077) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853821406/attempts/1) / tested source unavailable |
| 2026-10-08T22:29:49Z | success / executed / physical-execution | 9 | [job 113573100514](https://github.com/buster14a/buster/actions/runs/37853822597/job/113573100514) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853822597/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:02Z | success / executed / physical-execution | 11 | [job 113573176270](https://github.com/buster14a/buster/actions/runs/37853845938/job/113573176270) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853845938/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:02Z | success / executed / physical-execution | 9 | [job 113573180104](https://github.com/buster14a/buster/actions/runs/37853847023/job/113573180104) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853847023/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:04Z | success / executed / physical-execution | 9 | [job 113573184590](https://github.com/buster14a/buster/actions/runs/37853848495/job/113573184590) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853848495/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:30Z | success / executed / physical-execution | 6 | [job 113573341584](https://github.com/buster14a/buster/actions/runs/37853899704/job/113573341584) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853899704/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:41Z | success / executed / physical-execution | 11 | [job 113573365368](https://github.com/buster14a/buster/actions/runs/37853907664/job/113573365368) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853907664/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:41Z | success / executed / physical-execution | 10 | [job 113573398089](https://github.com/buster14a/buster/actions/runs/37853918223/job/113573398089) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853918223/attempts/1) / tested source unavailable |
| 2026-10-08T22:30:49Z | success / executed / physical-execution | 6 | [job 113573455763](https://github.com/buster14a/buster/actions/runs/37853936224/job/113573455763) / [attempt 1](https://github.com/buster14a/buster/actions/runs/37853936224/attempts/1) / tested source unavailable |
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
