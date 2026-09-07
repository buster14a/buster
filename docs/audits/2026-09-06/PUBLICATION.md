# Public audit record — September 6, 2026

The four issue reports, three code-fix PRs, and complete audit evidence are now public in `buster14a/buster`.

**The original report, README and manifest are preserved as delivered and retain their historical `not published` status. This file supersedes that status. The earlier claim that the GitHub connector exposed no write actions was incorrect; authenticated GitHub writes subsequently succeeded.**

## Public issues and pull requests

| Public PR | Contents | Related issues | Status at publication |
|---|---|---|---|
| [#169](https://github.com/buster14a/buster/pull/169) | eBPF comparison/logical-not offsets and signed bitwise-not normalization | [#165](https://github.com/buster14a/buster/issues/165), [#166](https://github.com/buster14a/buster/issues/166) | Open draft |
| [#170](https://github.com/buster14a/buster/pull/170) | Wasm scalar memory-alignment hints | [#167](https://github.com/buster14a/buster/issues/167) | Open draft |
| [#171](https://github.com/buster14a/buster/pull/171) | Scratch-output lifetime and null-empty string copies | #144; partial #100; retained #101 regression | Open draft |
| [#172](https://github.com/buster14a/buster/pull/172) | This complete audit package and publication evidence | All audit findings, including [#168](https://github.com/buster14a/buster/issues/168) | Open |

The eBPF symbol-lookup scaling issue #168 includes its probe and raw measurement in this package. No speculative performance patch was prepared for it.

## Exact code revisions and fresh validation

The code branches were created independently from main snapshot `31cff20271188e6429d7d40a5c5fe89dd8fb540a`. No fix was merged, no force-push was used, and main was not updated by this publication.

| Patch | Public branch | Commit | Focused baseline / patched exit |
|---|---|---|---|
| eBPF | [audit/2026-09-06-ebpf-scalar](https://github.com/buster14a/buster/tree/audit/2026-09-06-ebpf-scalar) | `b57c8a5f382d22f58d96c2e46832a315991c8c18` | 1 / 0 |
| Wasm | [audit/2026-09-06-wasm-alignment](https://github.com/buster14a/buster/tree/audit/2026-09-06-wasm-alignment) | `91f54819a19d027d2e150a59b544171ecf328c72` | 1 / 0 |
| Runtime | [audit/2026-09-06-runtime-boundaries](https://github.com/buster14a/buster/tree/audit/2026-09-06-runtime-boundaries) | `06a95cea89ec36d12b6f679a15b40a1ecdf99c09` | 1 / 0 |

The [successful publication workflow](https://github.com/buster14a/buster/actions/runs/34038019940) ran all three before/after regressions on clean worktrees of that actual base with Clang `-O2`, AddressSanitizer and UndefinedBehaviorSanitizer. Patched results are 474/474 eBPF cases, 112/112 Wasm module validations, and all runtime scenarios. Leak checking was disabled for process-lifetime arenas; address and undefined checks were enabled.

Exact outcomes, base/blob comparisons and branch SHAs are in [publication-results.json](publication-results.json). Fresh logs are under `evidence/publication-*-baseline.log` and `evidence/publication-*-patched.log`.

The #101 relative-file-mapping fix already exists in the base: `file.c` exactly matches the original patch's postimage. The runtime branch therefore changes only `os.c` and `string.c`, while retaining all three original runtime regression scenarios. The newer Unicode conversion fix in `string.c` is preserved.

## Package and limits

This directory publishes all 53 files of the delivered audit package, plus six fresh logs and these two publication records. Read [AUDIT_REPORT.md](AUDIT_REPORT.md) for the findings and original validation, [evidence/PROVENANCE.md](evidence/PROVENANCE.md) for provenance, and `patches/` for the original exact standalone patchsets. The original `SHA256SUMS.txt` applies to the delivered package files, not these additional publication records.

The original report describes tests in an older recovered checkout. The new publication evidence above uses the actual publication-base checkout; it still establishes only focused regression results, not a full project build.

The regression scripts remain opt-in. Full `test_all`, Release self-host, native Windows/macOS and relevant emitted-program integration gates remain outstanding. The eBPF interpreter tests the emitted instruction subset, not kernel loading; Wasm coverage validates modules, not full C-program execution. The focused eBPF/Wasm fixtures retain an unused-IR-path compiler warning documented in their PRs. The three code PRs are therefore public drafts, not assertions of merge readiness.
