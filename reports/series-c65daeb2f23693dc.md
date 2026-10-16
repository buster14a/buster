# Windows x86-64 native

[Trusted main](index.md) · [All series](all.md) · [JSON](recent.jsonl) · [CSV](recent.csv)

Policy hosted-ci-cohort-v1; refresh 2026-10-16T09:19:27Z. Cohort fields and missing values:

| Field | Value |
|---|---|
| workflow&#95;path | .github/workflows/ci.yml |
| job&#95;key | native |
| matrix&#95;identity | {"name":"Windows x86-64 native","platform":"windows","runner":"windows-2025","os":"windows","arch":"x86&#95;64","vs&#95;arch":"amd64","vs&#95;component":"Microsoft.VisualStudio.Component.VC.Tools.x86.x64","clang&#95;arch":"x86&#95;64"} |
| invocation&#95;path | direct |
| display&#95;name | Windows x86-64 native |
| requested&#95;labels | windows-2025 |
| event | pull&#95;request |
| head&#95;branch | codex/4019-recover-inconsistent-inventories |
| workflow&#95;blob | 484c226869d8de7cb82eabb14a981f91151011d8 |
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
| runner&#95;image&#95;version | 20261007.264.1 |
| execution&#95;context | unavailable |
| toolchain&#95;identity | compiler='clang version 23.1.3 &#40;https://github.com/llvm/llvm-project 0d261d1ca552c95a8f007e061c787ac7132fbcbc&#41;' cmake='cmake version 4.4.4' ninja=1.13.2 |
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
