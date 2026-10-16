# Reconcile exact-group admission

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-16T04:36:23Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/merge-queue-reconcile.yml |
| job&#95;key | reconcile |
| matrix&#95;identity | non-matrix |
| invocation&#95;path | direct |
| display&#95;name | Reconcile exact-group admission |
| requested&#95;labels | ubuntu-latest |
| event | workflow&#95;run |
| head&#95;branch | main |
| workflow&#95;blob | 56f3e3eb3eef433fbad5fc244f2e6d4951f9c19c |
| cpu&#95;normalized | AMD EPYC 9V74 80-Core Processor |
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

**insufficient data**. Baseline: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 2.0 s; candidate variability allowance 2.0 s.

**Trailing baseline observations:** unavailable.

**Candidate observations:** unavailable.

### Cumulative change from the fixed initial anchor

**insufficient data**. Baseline: n=13; median 55.0 s; MAD 4.0 s; 4 UTC date buckets; p95 unavailable (requires 20). Candidate: n=0; median/dispersion unavailable; p95 unavailable (requires 20). Change unavailable (percent unavailable below two-second baseline). Observed practical floor 12.0 s; candidate variability allowance 2.0 s.

Anchor reason: initial observations of this exact policy/cohort. It fills at most twenty points and never slides. Candidate overlap is excluded. Changing an anchor requires a new explicit report/policy and recorded reason; old rows remain.

**Fixed anchor observations:** [job 114353256636](https://github.com/buster14a/buster/actions/runs/38099822831/job/114353256636) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38099822831/attempts/1) / [tested 9516f631b6a3](https://github.com/buster14a/buster/commit/9516f631b6a30943b90a77816708407a0a04b904) at 2026-10-11T00:51:08Z; [job 114353431331](https://github.com/buster14a/buster/actions/runs/38099881507/job/114353431331) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38099881507/attempts/1) / [tested 9516f631b6a3](https://github.com/buster14a/buster/commit/9516f631b6a30943b90a77816708407a0a04b904) at 2026-10-11T00:52:11Z; [job 114354384205](https://github.com/buster14a/buster/actions/runs/38100200265/job/114354384205) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38100200265/attempts/1) / [tested 9516f631b6a3](https://github.com/buster14a/buster/commit/9516f631b6a30943b90a77816708407a0a04b904) at 2026-10-11T00:57:53Z; [job 114395351122](https://github.com/buster14a/buster/actions/runs/38114037032/job/114395351122) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38114037032/attempts/1) / [tested a67057643a40](https://github.com/buster14a/buster/commit/a67057643a40c0ab99931849eb30053a2b870fcc) at 2026-10-11T05:13:03Z; [job 114642601473](https://github.com/buster14a/buster/actions/runs/38195457581/job/114642601473) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38195457581/attempts/1) / [tested 15ea8a709b3c](https://github.com/buster14a/buster/commit/15ea8a709b3c47e7287580fd1c46bfb66eddf18c) at 2026-10-12T02:48:56Z; [job 114754806373](https://github.com/buster14a/buster/actions/runs/38232126774/job/114754806373) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38232126774/attempts/1) / [tested c6201a26f013](https://github.com/buster14a/buster/commit/c6201a26f013074b977640232ea039a4e89f34d0) at 2026-10-12T09:49:53Z; [job 115012006612](https://github.com/buster14a/buster/actions/runs/38300224309/job/115012006612) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38300224309/attempts/1) / [tested 2e122d873725](https://github.com/buster14a/buster/commit/2e122d873725ee8db3e2216eb4d6fd4df281ce18) at 2026-10-12T21:17:12Z; [job 115011000443](https://github.com/buster14a/buster/actions/runs/38300000070/job/115011000443) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38300000070/attempts/1) / [tested b7354cef7164](https://github.com/buster14a/buster/commit/b7354cef71648dd842c7c25d2f436d97ac4ccd32) at 2026-10-12T21:14:17Z; [job 115316432404](https://github.com/buster14a/buster/actions/runs/38390349445/job/115316432404) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38390349445/attempts/1) / [tested 3ce70f8a85fd](https://github.com/buster14a/buster/commit/3ce70f8a85fd4294e6b332c2cc485a3f3dcbd39b) at 2026-10-13T13:14:24Z; [job 115468466884](https://github.com/buster14a/buster/actions/runs/38436013078/job/115468466884) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38436013078/attempts/1) / [tested 6d4694883990](https://github.com/buster14a/buster/commit/6d4694883990f8043d22794ff38cf070ddbfc134) at 2026-10-13T18:29:33Z; [job 115592633496](https://github.com/buster14a/buster/actions/runs/38472855130/job/115592633496) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38472855130/attempts/1) / [tested 42a66b13235e](https://github.com/buster14a/buster/commit/42a66b13235ebe4fa2564175f48b354e2a268303) at 2026-10-13T23:22:32Z; [job 115848792387](https://github.com/buster14a/buster/actions/runs/38559841264/job/115848792387) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38559841264/attempts/1) / [tested 69f594c81a52](https://github.com/buster14a/buster/commit/69f594c81a52eb3457496d6c12761fda24ffce55) at 2026-10-14T12:20:42Z; [job 116110589378](https://github.com/buster14a/buster/actions/runs/38639345482/job/116110589378) / [attempt 1](https://github.com/buster14a/buster/actions/runs/38639345482/attempts/1) / [tested ad8c8373c319](https://github.com/buster14a/buster/commit/ad8c8373c319eeca3e3f233e1c7cd7b22b6fbf9d) at 2026-10-14T21:19:26Z.

Existing native phase receipts, where supplied, are retained in the JSON/CSV exports as native_phase_records_json. Their native monotonic clock scope, elapsed_ns and observer overhead remain separate from API job wall time; phases are not summed into CPU time. Toolchain log receipts are reported context rather than complete producer authentication.

These are descriptive CI signals, not independent significance tests or paired experiments. At least eight successful physical executions and three UTC date buckets per side are required. No detectable change does not establish equivalence or causality. Outliers remain in the raw rows.

| Started | Outcome / population | Elapsed s | Source evidence |
|---|---|---:|---|
