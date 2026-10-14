# Hosted CI timing history

Operational Actions observations; advisory. Compiler performance acceptance and the qualified 9700X dashboard are separate.

Collected at 2026-10-14T17:14:57Z. Policy hosted-ci-cohort-v1. 166 active observations, 2117 unknown hardware, 2 API/ingestion gaps, 1 incomplete runs, 70 aliases, 1318 unresolved cost intervals.

Observed hosted runner-seconds: 439128; observed unsuccessful-job runner-seconds: 35638 (136 jobs). These exclude unknown-hosting and missing intervals; they are neither billed minutes nor process CPU time. Dependency/deployment waits and required-CI critical-path attribution remain unavailable.

Reporting bounds: 30 observation-date shards / 32768 rows / 96 rendered series; 3516 rows outside the rendered series bound remain in exports. Raw history is retained separately from these bounded derived reports.

Bounded view omitted 0 older shards and 3897 older loaded rows; their raw records remain on the data branch.

[JSON export](recent.jsonl) · [CSV export](recent.csv) · [Collector policy](https://github.com/buster14a/buster/blob/main/docs/ci-timing-history.md)

Default view: trusted main push observations only. [All events, PRs and queues](all.md). The coverage totals and exports above include all selected events; the selector below filters main. Native report filters select job, OS, CPU, branch, revision and UTC date range.

| Job / matrix | OS / actual CPU | Event / branch | Successful samples | Missing context | History |
|---|---|---|---:|---:|---|
| Reconcile trusted retirement requests / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V45 96-Core Processor | push / main | 1 | 1 | [series](series-10506922e39f8938.md) |
| Main CI reuse decision / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-ab3a018448ec8bc8.md) |
| Workflow lint / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-0c5282e5ed5c6ef3.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 8 | [series](series-834b60ad161ab002.md) |
| github.event&#95;name == 'merge&#95;group' &#38;&#38; 'Workflow lint' &#124;&#124; 'Queue lint preflight &#40;inactive&#41;' /  | unknown OS / unknown CPU | push / main | 0 | 9 | [series](series-a9b57d195a9e6398.md) |
| Clang analyzer shards / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-560244a0bd177708.md) |
| Linux x86-64 portability / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-5872b09bec3e7d5e.md) |
| Windows x86-64 release / unavailable-dynamic-or-reusable-matrix | Windows Server 2025 Datacenter / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-4227dcafce9400ef.md) |
| Linux x86-64 release / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-9222b671470304bb.md) |
| macOS AArch64 portability / unavailable-dynamic-or-reusable-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 1 | 1 | [series](series-988fca8854d65458.md) |
| macOS AArch64 release / unavailable-dynamic-or-reusable-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 1 | 1 | [series](series-88a61735ecee8a6e.md) |
| Windows AArch64 release / unavailable-dynamic-or-reusable-matrix | Windows 10 Enterprise / Cobalt 100 | push / main | 1 | 1 | [series](series-96de1b5d136dbd84.md) |
| Linux AArch64 sanitized-release / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / 0xd49 | push / main | 1 | 1 | [series](series-b1147ac104732eba.md) |
| Linux x86-64 sanitized-release / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-9c3999541903419b.md) |
| Windows AArch64 checks / unavailable-dynamic-or-reusable-matrix | Windows 10 Enterprise / Cobalt 100 | push / main | 1 | 1 | [series](series-06d96ae717fef8de.md) |
| Windows x86-64 sanitized-release / unavailable-dynamic-or-reusable-matrix | Windows Server 2025 Datacenter / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-4b24e3111c389ddf.md) |
| macOS AArch64 sanitized-release / unavailable-dynamic-or-reusable-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 1 | 1 | [series](series-07fe582d116bfada.md) |
| Windows x86-64 portability / unavailable-dynamic-or-reusable-matrix | Windows Server 2025 Datacenter / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 1 | 1 | [series](series-14a99a7f0f2239c3.md) |
| Linux AArch64 portability / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / 0xd49 | push / main | 1 | 1 | [series](series-3d0f1cef6f548dfc.md) |
| Linux AArch64 release / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / 0xd49 | push / main | 1 | 1 | [series](series-4f9873a1dacc5bba.md) |
| UEFI firmware boot /  | unknown OS / unknown CPU | push / main | 0 | 9 | [series](series-869cab7418f8fa44.md) |
| ${{ matrix.name }}${{ &#40;&#40;&#40;github.event&#95;name == 'pull&#95;request'&#41; &#38;&#38; github.event.pull&#95;request.draft &#38;&#38; &#40;github.run&#95;attempt == '1'&#41; &#38;&#38; startsWith&#40;matrix.runner, 'macos-'&#41; &#38;&#38; ' &#40;deferred for draft PR&#41;'&#41; &#124;&#124; ''&#41; }} /  | unknown OS / unknown CPU | push / main | 0 | 18 | [series](series-a2e01e6c19ba3f4d.md) |
| CI complete / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-29bc31c71c7644c3.md) |
| Hosted ordinary compiler bridge proof / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V45 96-Core Processor | push / main | 2 | 2 | [series](series-b1cc0ce81c796933.md) |
| Hosted compiler closure Utility proof / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 2 | 2 | [series](series-7d006970abfdd831.md) |
| Hosted TCC native proofs / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 3 | 3 | [series](series-684ab76cd9826139.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 11 | [series](series-58dd6628997072af.md) |
| Canonical TCC bootstrap / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 2 | 2 | [series](series-772699bac82a8140.md) |
| Pages validation and artifact /  | unknown OS / unknown CPU | push / main | 0 | 6 | [series](series-fe39ffa1f1875ff1.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 6 | [series](series-3e4ea91afe05e1e2.md) |
| Deploy GitHub Pages /  | unknown OS / unknown CPU | push / main | 0 | 6 | [series](series-793aebc7e64d3c33.md) |
| API migration policy / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V45 96-Core Processor | push / main | 3 | 3 | [series](series-cb32ae882dc3309a.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 10 | [series](series-4494fc1d80963d60.md) |
| Open the native-retirement catch-up request / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 5 | 5 | [series](series-33e938f1f4be6999.md) |
| Benchmark service workflow policy / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 3 | 3 | [series](series-ff827e3a1b9a9b4f.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 9 | [series](series-e179d4e27c9210c7.md) |
| Record the compiler benchmark request / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-61652c772d0438d4.md) |
| Show the queued compiler benchmark check / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 4 | 4 | [series](series-17bd61e85706124b.md) |
| Main integration admission / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 7763 64-Core Processor | push / main | 5 | 5 | [series](series-1b8f6d07d07f66c8.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 11 | [series](series-1df5c2f22286ee08.md) |
| Native retirement merge admission / non-matrix | Ubuntu 24.04.5 LTS / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 1 | 1 | [series](series-f83fd6ca430c87e3.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 10 | [series](series-70a4549d34f665cb.md) |
| Rebind unit &#40;ubuntu-26.04&#41; / unavailable-dynamic-or-reusable-matrix | Ubuntu 26.04.1 LTS / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 3 | 3 | [series](series-394dcfa8e3c641d7.md) |
| Rebind unit &#40;macos-26&#41; / unavailable-dynamic-or-reusable-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 7 | 7 | [series](series-567346415101811f.md) |
| Reconstruct candidate closure ephemerally / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 5 | 5 | [series](series-471f555b9fc27065.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 7 | [series](series-e6a5018658203188.md) |
| GPU Linux consumers / non-matrix | Ubuntu 26.04.1 LTS / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 1 | 1 | [series](series-95939e56e61aa76c.md) |
| GPU Metal consumer /  | unknown OS / unknown CPU | push / main | 0 | 12 | [series](series-5055dc4fc114973a.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 11 | [series](series-4ec16058bf5d4207.md) |
| embedding / non-matrix | Ubuntu 24.04.5 LTS / Intel&#40;R&#41; Xeon&#40;R&#41; 6973P-C | push / main | 1 | 1 | [series](series-38a8534cdab88a17.md) |
| Exact merge-tree preflight / non-matrix | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 2 | 2 | [series](series-b7b64658b76266ad.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 12 | [series](series-1ec309cd5d3078c1.md) |
| foundation / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 4 | 4 | [series](series-2144b421086ce73d.md) |
| No-code plan /  | unknown OS / unknown CPU | push / main | 0 | 7 | [series](series-f84e77c467cacc6a.md) |
| lifecycle &#40;Debug, OFF, --sanitize&#41; / {"config":"Debug","unity":"OFF","sanitize":"--sanitize"} | Ubuntu 24.04.5 LTS / INTEL&#40;R&#41; XEON&#40;R&#41; PLATINUM 8573C | push / main | 2 | 2 | [series](series-a00fea82f8997176.md) |
| lifecycle &#40;Release, ON, --no-sanitize&#41; / {"config":"Release","unity":"ON","sanitize":"--no-sanitize"} | Ubuntu 24.04.5 LTS / AMD EPYC 9V74 80-Core Processor | push / main | 1 | 1 | [series](series-1ffcfa757a4e01b7.md) |
| Linux x86-64 bootstrap evidence / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 6 | 6 | [series](series-45419f71ad96fa6a.md) |
| Windows x86-64 inventory &#40;qualification pending&#41; / non-matrix | Windows Server 2025 Datacenter / AMD EPYC 7763 64-Core Processor | push / main | 1 | 1 | [series](series-f2b47fd69cbbdcd7.md) |
| macOS AArch64 inventory &#40;qualification pending&#41; / non-matrix | macOS / Apple M1 &#40;Virtual&#41; | push / main | 1 | 1 | [series](series-90951964e10474ae.md) |
| Linux x86-64 external projects / non-matrix | Ubuntu 26.04.1 LTS / AMD EPYC 7763 64-Core Processor | push / main | 0 | 1 | [series](series-0816922646fd89b0.md) |

## Largest observed main time changes

Ordered by absolute median seconds per physical execution; candidate frequency is shown separately. These descriptive differences include improvements and slowdowns. Missing context is not a qualified signal, and no required-CI critical-path impact is inferred.

| Series | Median change s | Change % | Candidate executions | State |
|---|---:|---:|---:|---|

Last selected execution completed: 2026-10-14T12:27:18Z; collection lag 17259 s. Pending runs: 10; reverse-sweep cursor 2026-10-08T17:36:47Z. The cursor measures discovery progress, not evidence of complete historical coverage.
