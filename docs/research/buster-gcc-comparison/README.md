# Buster versus GCC — preserved comparison evidence

Frozen report for [#2274](https://github.com/buster14a/buster/issues/2274), linked from comparison umbrella [#2334](https://github.com/buster14a/buster/issues/2334). This directory is an archival publication, not a live implementation backlog.

Read the [complete Markdown report](report.md), [PDF](buster-vs-gcc-comparison.pdf), or [HTML source](buster-vs-gcc-report.html). The report contains 45 comparison dimensions, 11 recommendations, 8 license rows and 49 primary source links. It explains scoped winners, measured differences, GCC mechanisms, proposed Buster remedies and unresolved evidence.

## Measurements and provenance

- Buster source: d4ca72fb4e97ae0a410b0d03258b00495fa45e13; production tree e3a17ff788cbfc5f4038f3c260a121315ddf561c.
- Exact diagnostic harness/workflow: e19b531e03db62afd8f688ecd69d7f9f111283bf, archived in [provenance](provenance). No production-source diff.
- Timed compiler: Ubuntu GCC 15.2.0-16ubuntu1. GCC 16.2.0 capability source: 78d4ac73dd391005b895a6148cd9831e28e1208b. Do not transfer timing results between versions.
- Original timing host: GitHub-hosted Ubuntu 26.04.1, AMD EPYC 7763; Buster producer Clang 21.1.8. No laptop or 9700X execution.
- Six common C inputs; 18 cells; 216 timed object compilations, 144 runtime executions, 18 preflights and 12 wrapper controls: 390 observations. 56 untimed probes.
- Buster FAST/O0 and GCC O0/O2 were measured separately with common semantic flags, baseline ISA and g0. This is not a full debug-information comparison.
- Median paired ratios, not ratios of displayed medians; descriptive win counts, not confidence intervals. GNU-time CPU is coarse and RSS is maximum child peak. Stock GCC hardening was retained; no matching ablation.

| Run | Disposition | Durable archive |
|---|---|---|
| [37004946749](https://github.com/buster14a/buster/actions/runs/37004946749) | Failed before building because checkout history was shallow. No measurements. | [Setup failure ZIP](evidence/run-37004946749-setup-failure.zip) |
| [37005042712](https://github.com/buster14a/buster/actions/runs/37005042712) | All wall-time inference rejected due polling quantization. Behavior/probe evidence retained. | [Rejected timing ZIP](evidence/run-37005042712-rejected-wall-timing.zip) |
| [37005475203](https://github.com/buster14a/buster/actions/runs/37005475203) | Current bounded diagnostics with blocking wait and independent watchdog. | [Current evidence ZIP](evidence/run-37005475203-current-diagnostic.zip) |

Each exact ZIP preserves every original stdout/stderr/time record, source, object, executable, oracle, metadata and uploaded build-provenance file. [Browsable raw tables and inputs](data), [current samples CSV](data/current-samples.csv), [paired comparisons](data/paired-comparisons.json), [assembly inspection](assembly) and all three [job logs](provenance) are also retained.

## Verification

[verification.json](verification.json) records archive SHA256/CRC checks, all 18 source/object hash checks and independent summary-statistic replay. [manifest.json](manifest.json) provides per-file byte count/SHA256/Git-blob identity and explicit limitations. From this directory run `sha256sum --check SHA256SUMS`. Archives are evidence: do not run retained executables merely to check integrity. [Publication script](tooling/publish-evidence.py) performs artifact/statistic replay without rebuilding Buster or executing generated programs.

## Findings and ownership

Measured headline: Buster compiles the 256-function unit 3.17x faster than GCC O0 and 9.00x than O2; GCC O2 runs all four programs 1.43–6.00x faster and emits smaller text on all six inputs. Larger-fixture wins are workload-specific. GCC's broader production-readiness assessment is distinct from compilation throughput.

Existing owners retain implementation scope: [#1568 negative array bounds](https://github.com/buster14a/buster/issues/1568), [#1574 warnings](https://github.com/buster14a/buster/issues/1574), [#1927 FP residency](https://github.com/buster14a/buster/issues/1927), [#1944 vector census](https://github.com/buster14a/buster/issues/1944), [#1295 startup](https://github.com/buster14a/buster/issues/1295), [#47 optimization policies](https://github.com/buster14a/buster/issues/47), [#1931 comparator adapter](https://github.com/buster14a/buster/issues/1931), and new [#2302 depfiles](https://github.com/buster14a/buster/issues/2302). Exact posted evidence is copied in provenance; follow the issues for current status. No competing implementation or ownership transfer is implied.

## Limits and licensing

The measured compiler executable was never retained; its SHA256/size/build recipe are recorded. Qualified Zen 5/retirement acceptance, GCC 16 timing, broad pristine applications and other-target execution remain unrun. Historical SQLite evidence in the report keeps its original source and evidence class.

The workspace went offline during publication. Original working-note files, frozen report-rendering JSON/scripts and original layout QA files could not be copied byte-for-byte. Their substantive findings are in the full report. The retained PDF/HTML are recovered delivered reports; verification records whether those bytes match the earlier workspace hashes. [Publication context](provenance/publication-context.json) lists these limits explicitly.

Buster has no selected project-wide first-party license at the pinned revision; upstream notices do not license Buster's own code ([#621](https://github.com/buster14a/buster/issues/621)). GCC compiler: GPL-3.0-or-later; runtime-library exceptions and component-specific terms are detailed and sourced in the report. This publication grants no new license and imports no external compiler source.
