# Public audit record — September 6, 2026

This directory publishes all 53 files of the delivered audit package, plus fresh publication-run evidence.

**The original report, README and manifest retain their historical `not published` status. That status is superseded by this file. The earlier claim that the connector exposed no write actions was incorrect; authenticated GitHub writes subsequently succeeded.**

The issue reports are public: [#165](https://github.com/buster14a/buster/issues/165), [#166](https://github.com/buster14a/buster/issues/166), [#167](https://github.com/buster14a/buster/issues/167), [#168](https://github.com/buster14a/buster/issues/168).

The code branches below were created independently from current-main snapshot `31cff20271188e6429d7d40a5c5fe89dd8fb540a`. No change was merged and main was not updated by this publication.

| Patch | Public branch | Commit | Focused before / after exit |
|---|---|---|---|

| ebpf-scalar | [audit/2026-09-06-ebpf-scalar](https://github.com/buster14a/buster/tree/audit/2026-09-06-ebpf-scalar) | `b57c8a5f382d22f58d96c2e46832a315991c8c18` | 1 / 0 |

| wasm-alignment | [audit/2026-09-06-wasm-alignment](https://github.com/buster14a/buster/tree/audit/2026-09-06-wasm-alignment) | `91f54819a19d027d2e150a59b544171ecf328c72` | 1 / 0 |

| runtime-boundaries | [audit/2026-09-06-runtime-boundaries](https://github.com/buster14a/buster/tree/audit/2026-09-06-runtime-boundaries) | `06a95cea89ec36d12b6f679a15b40a1ecdf99c09` | 1 / 0 |


The #101 relative-file-mapping fix already exists in the publication base (file.c is exactly the original patch's postimage). The runtime branch therefore changes only os.c and string.c, while keeping all three original runtime regression scenarios. The existing Unicode conversion fix in string.c is preserved.


Public code PRs already opened: [#169](https://github.com/buster14a/buster/pull/169) for eBPF and [#170](https://github.com/buster14a/buster/pull/170) for Wasm. The runtime PR and this evidence PR are linked in their public discussions after branch verification.


[Publication workflow and full logs](https://github.com/buster14a/buster/actions/runs/34038019940). Exact outcomes and preimage comparisons are in [publication-results.json](publication-results.json).


The regression scripts remain opt-in. A zero patched exit is not a full test_all, Release self-host, native Windows/macOS, or kernel-eBPF result. Baseline failures must be read alongside the logs to distinguish semantic failures from host build problems. Full integration/platform gates remain outstanding, so code PRs should be drafts.


Read [AUDIT_REPORT.md](AUDIT_REPORT.md) for findings and original validation, [evidence/PROVENANCE.md](evidence/PROVENANCE.md) for provenance, and patches/ for exact standalone patchsets. The original SHA256SUMS applies to the delivered package files, not the newly added publication records.
