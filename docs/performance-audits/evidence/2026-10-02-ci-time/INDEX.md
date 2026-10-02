# Published CI time audit: evidence and issue index

**Published 2 October 2026.** This is an archive and finding handoff, not a production optimization or performance-acceptance change. Preserve this archive branch; it is intentionally not a PR into main.

## Start here

- [Full original report](report.md), including both complete per-lane appendices.
- [Complete evidence bundle](evidence.zip): 53 members, including all 50 original delivered files and all 27 unchanged original artifact ZIPs.
- [Full-precision derived CSV/JSON ledgers](derived/): phase totals, every tree/lane, module timings, launch records, partition records and verified artifact inventory.
- [Publication receipt](publication.json), [GitHub artifact metadata](publication-artifact-metadata.json), [original member hashes](expected-original.json), and [reproduction/correction notes](PUBLICATION.md).
- [Successful archive-only hosted execution](https://github.com/buster14a/buster/actions/runs/37005935404), attempt 1, job 110834097465.

Immutable data commit: **`999f03babd32d1e97758ea6f0c3a30dae63a1551`**.
Archive branch: `codex/709-ci-time-evidence`.
Inspected main baseline: `d4ca72fb4e97ae0a410b0d03258b00495fa45e13`.
Measurement source: `e424b387fcb51b00c1d19e87e2d369ae8a212792`, tree `b2ab099b499f7d5c8e91de92312f270c475c0a8f`.

The detailed measured samples are A3 run **36991068435** (combined checks) and C4 run **36996168725** (split checks), both first attempts. Their complete desktop data contains 26 archives, 44 configured trees, 26 captured runtime invocations and 1420 module timing records; the 27th archive is C4's analyzer. The report also cites the existing nine-run qualification ledger. This bundle is not presented as a preservation of all 108 desktop archives in that separate campaign.

## Verified publication

| Object | SHA-256 / identity |
|---|---|
| Original delivered outer ZIP | `422a4cebdd04c3d27d80cb7a23c144b3ade681017caafe7023ba74c334a9d5b0` |
| Published normalized `evidence.zip`, 5854551 bytes | `c5a3078a691750a37569dbce3b04d895895f432da4c7585c33e108ab51305889` |
| Committed evidence ZIP Git blob | `1fb26002e4977999da8175f4e39cb02859f003ea` |
| Original report | `8b6b5fe354e599a712ca88609e32f3bbf46c7ec9c6dd5b34b1f9e92a331315ca` |
| Backup Actions artifact 11225896785, outer ZIP | `4b36206fa6278333e4631195e2a84930b74d159bde13a047315b38596d934209` |

Completed checks: the archive job downloaded all 27 original ZIPs and verified their exact GitHub-published hashes and source identities; data replay matched all 50 original members. After publication, a separate download of artifact 11225896785 matched its published digest, the contained evidence ZIP and every original member byte-for-byte. Its Git blob hash also matches the actual committed file's metadata. The normalized outer ZIP differs from the original container intentionally; no original member was modified. The Git copy remains available beyond the backup Actions artifact's retention window.

Only the archive/replay job ran: 42 seconds of observed job execution and 81 seconds of queue delay. These are publication costs, not new measurements of Buster compilation or tests. No compiler, test suite, analyzer, benchmark, laptop or 9700X execution was performed for publication.

## Findings and existing issue ownership

| Finding / result | Existing owner and publication update | Concrete next step or disposition |
|---|---|---|
| Generation/build/test breakdown and whole-CI reporting | [#709](https://github.com/buster14a/buster/issues/709), [original audit summary](https://github.com/buster14a/buster/issues/709#issuecomment-5951900471) | Use the existing report/observer infrastructure. Distinguish overlapping task-minutes, workflow latency, runner occupancy and CPU time; full-validation and reused-main cohorts remain separate. Native/mobile/setup/queue work is outside the desktop three-way sum, not zero. |
| Sanitized Debug driver tail, fixed-budget group imbalance and high-launch fixtures | [#1826: new evidence](https://github.com/buster14a/buster/issues/1826#issuecomment-5952268686) | Refresh existing fixture/operation attribution before selecting one bounded repair or partition adjustment. Driver spans 98–99% of the C4 Debug payload; group tails are not guaranteed savings. |
| Split layout timing and full evidence qualification | [#2120: archive and owner handoff](https://github.com/buster14a/buster/issues/2120#issuecomment-5952278145), [#2119](https://github.com/buster14a/buster/issues/2119) | Keep the historical 26.12% lower median latency as a conditional observation. Preserve the failed attempt/retry and hardware/resource/condition limitations; do not change defaults on this publication. The Windows all-builds barrier missed its target. |
| Byte-safe unit evidence ingestion | [Open PR #2264](https://github.com/buster14a/buster/pull/2264), head `8c3d49e707813bee323bd8358e32c86bd29df1b5` when checked | Existing owner is integrating the reader repair; this archive does not create another fix. Raw diagnostic bytes and proof/hash checks remain intact. |
| Host-dependent assertion counts and missing effective feature witnesses | [#2120 owner checkpoint](https://github.com/buster14a/buster/issues/2120#issuecomment-5951676609), branch `codex/2120-native-host-census` | Source accounting explains deliberate hardware-dependent counts. Independent same-binary feature evidence remains necessary; do not infer identical workloads from CPU model names or normalize counts retrospectively. |
| Scoped resource receipts versus missing complete descendant/nested-operation accounting | Same [#2120 checkpoint](https://github.com/buster14a/buster/issues/2120#issuecomment-5951676609), branch `codex/2120-native-resource-witnesses`; broader rollup #709 | Preserve reader -> host -> resource integration order. POSIX waited-descendant, Windows leader-only, complete process-tree totals and different memory peaks must remain distinct. A complete mixed-phase operation ledger is a future extension, not a reason to duplicate or expand the active branch silently. |
| Analyzer workload and the rejected four-worker default | [#2033: new C4 evidence and correction](https://github.com/buster14a/buster/issues/2033#issuecomment-5952262718), merged [#2046](https://github.com/buster14a/buster/pull/2046) | Keep two workers. The existing four-worker experiment was completed and rejected under its declared envelope; it was not missing. C4 fixed-cost two-worker imbalance headroom is only about 11.017 seconds / 2.35%, so shard-size extremes alone do not establish a worthwhile rebalancing project. |
| Optional frame/vector batching | [#1885's deferred disposition](https://github.com/buster14a/buster/issues/1885#issuecomment-5948290674), closed unmerged PR #1892; current evidence on #1826 | Keep deferred, with no duplicate issue or automatic reopening. Existing observer/link caches and in-process Buster compilation must be acknowledged. Logged process counts do not establish launch overhead dominance. |
| Exact-source main CI reuse | Delivered [#1808](https://github.com/buster14a/buster/issues/1808), [live observation](https://github.com/buster14a/buster/issues/709#issuecomment-5948407998) | Already delivered; do not propose it as missing or mix reuse runs with complete validation in usage percentages. |
| Incomplete reproduction instructions in the delivered package | [PUBLICATION.md](PUBLICATION.md), added [replay.py](replay.py) | Fixed for this archive: phase totals and three analyzer extracts now regenerate alongside all original ledgers. Historical files remain unchanged. |

No new duplicate issue is needed for these findings. This publication does not take over the implementation branches above. No successor is being claimed for an unassigned future experiment.

## Corrections and current status take precedence over the frozen report

The original report is retained byte-for-byte as historical evidence. Its recommendations and blocker wording must be read with this index:

1. #2033 is **closed/rejected**, with results delivered in merged #2046. Four workers improved isolated wall time but exceeded that experiment's CPU-cost allowance. The report's optional worker-budget suggestion is not a newly unperformed experiment, nor authorization to change the default.
2. C4's shard durations sum to 914.461155s. At two workers, the fixed-duration work lower bound is 457.2305775s versus observed 468.247562s. At most 11.0169845s (2.35%) lies above that lower bound under the fixed-cost assumption. Balancing is not supported as a large saving by shard extremes alone.
3. The original Windows log blocker has a concrete repair in PR #2264, and intentional assertion-count differences have source accounting. That progress does not retroactively supply effective host-feature/resource witnesses or full historical campaign qualification.
4. The two historical helper scripts alone omitted phase-total reconstruction and analyzer extraction. `replay.py` completes those steps and checks every original member. The initial broad claim that the original two scripts reproduced every output was too strong; the publication corrects it without rewriting evidence.

## Reproduce offline

Download `evidence.zip`, extract it, and run:

```sh
python3 -B replay.py
```

Use Python 3 without `-O`/`PYTHONOPTIMIZE`. This reads ZIP/log/JSON data and writes regenerated diagnostic ledgers. It runs no captured binary, Buster compiler, tests or benchmarks. It is not a replacement for the repository's independent phase/assertion qualification validators.

**License/provenance:** Buster first-party terms remain unselected/unresolved at the inspected source, per [LICENSES/README.md](https://github.com/buster14a/buster/blob/e424b387fcb51b00c1d19e87e2d369ae8a212792/LICENSES/README.md) / #621. No third-party implementation, new dependency or asset was imported. The original report's external technical references remain references, not a claim of importing their code.
