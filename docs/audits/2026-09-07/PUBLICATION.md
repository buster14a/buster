# Public issue record — September 7, 2026

The 26 findings of this bundle are now on GitHub in `buster14a/buster`.

**`README.md` and `manifest.json` are preserved as delivered and retain their `not published` status. This file supersedes that status.** `publication-results.json` is the machine-readable ledger and records all 26 keys, including the three that were routed to existing issues rather than filed.

Filed with `publish.py` through the `gh` CLI, so the issues are authored by the repository owner. Preflight was clean: **0 issues showed file drift** against `main` at `44a936a8b19901bee0ed9f9062d310eede0066af` — the audited revision is current `main` — and **0 matched an existing issue** by marker or exact title.

## 23 issues filed

| Issue | Priority | Key |
|---|---|---|
| [#183](https://github.com/buster14a/buster/issues/183) | P1 | `c-enum-underlying-type` |
| [#184](https://github.com/buster14a/buster/issues/184) | P1 | `x64-i128-signed-divide` |
| [#185](https://github.com/buster14a/buster/issues/185) | P1 | `c-float-to-integer-constant-fold` |
| [#186](https://github.com/buster14a/buster/issues/186) | P1 | `c-bool-aggregate-initializer` |
| [#187](https://github.com/buster14a/buster/issues/187) | P1 | `c-unsigned-to-float-constant-fold` |
| [#188](https://github.com/buster14a/buster/issues/188) | P1 | `c-nested-initializer-string` |
| [#189](https://github.com/buster14a/buster/issues/189) | P1 | `c-i128-aggregate-initializer` |
| [#190](https://github.com/buster14a/buster/issues/190) | P1 | `x86-apx-evex-prefix-fields` |
| [#191](https://github.com/buster14a/buster/issues/191) | P1 | `x86-evex-broadcast-disp8-scale` |
| [#192](https://github.com/buster14a/buster/issues/192) | P1 | `x86-rex2-memory-width-w` |
| [#193](https://github.com/buster14a/buster/issues/193) | P1 | `x86-att-s-suffix-alias` |
| [#194](https://github.com/buster14a/buster/issues/194) | P2 | `x64-i128-float-cast-unsupported` |
| [#195](https://github.com/buster14a/buster/issues/195) | P2 | `x64-atomic-16-byte-aggregate` |
| [#196](https://github.com/buster14a/buster/issues/196) | P2 | `release-check-macro-is-unreachable` |
| [#197](https://github.com/buster14a/buster/issues/197) | P1 | `c-parse-initializer-infinite-loop` |
| [#198](https://github.com/buster14a/buster/issues/198) | P1 | `c-parse-identifier-use-overflow` |
| [#199](https://github.com/buster14a/buster/issues/199) | P1 | `arena-commit-failure-and-granularity` |
| [#200](https://github.com/buster14a/buster/issues/200) | P1 | `file-read-unsized-source` |
| [#201](https://github.com/buster14a/buster/issues/201) | P1 | `dwarf-ranges-double-count-base` |
| [#202](https://github.com/buster14a/buster/issues/202) | P1 | `codeview-fieldlist-length-overflow` |
| [#203](https://github.com/buster14a/buster/issues/203) | P1 | `codeview-gproc32-pend-offsets` |
| [#204](https://github.com/buster14a/buster/issues/204) | P2 | `codeview-scope-stack-quadratic` |
| [#205](https://github.com/buster14a/buster/issues/205) | P1 | `driver-default-object-path` |

`publish.py --link-references` then rewrote seven of those bodies so that backticked references to sibling keys carry the issue number.

## 3 findings routed to already-open issues

`publish.py` matches on its audit marker or an exact title, which it documents as not being semantic duplicate detection — the README instructs the operator to read the open issues first. Three findings turned out to be the same defect as an issue that is already open, described in the same functions with the same required fix. Filing them again would have split the work, so their evidence was posted as a comment on the existing issue instead. Each is recorded in `publication-results.json` with the comment URL, which also stops a later `--publish` run from filing them.

| Finding | Existing issue | Why |
|---|---|---|
| `c-import-once-path-unchecked` | [#81](https://github.com/buster14a/buster/issues/81) | #81 already owns the root-length `once_paths` sizing, the unchecked append, and the checking `#pragma once` sibling. The [comment](https://github.com/buster14a/buster/issues/81#issuecomment-5570719960) adds the measured overrun — 19988 writes past the end, about 320 KB — and line numbers re-derived at `44a936a8`. |
| `os-write-error-counted-as-success` | [#82](https://github.com/buster14a/buster/issues/82) | #82 already owns the file-write error contract and notes that failures reach `BUSTER_CHECK` instead of returning. The [comment](https://github.com/buster14a/buster/issues/82#issuecomment-5570720165) adds the Release mechanism and its two demonstrated shapes: 195904 bytes silently dropped, and a 256-byte buffer written as 257 bytes with byte 99 duplicated. |
| `os-read-error-indistinguishable-from-eof` | [#109](https://github.com/buster14a/buster/issues/109) | #109 already owns the read contract and sketches the `OsReadStatus` shape this finding asks for. The [comment](https://github.com/buster14a/buster/issues/109#issuecomment-5570720365) adds the reproduction and states plainly that it comes from a subsystem pass and was not re-executed at `44a936a8`. |

## Lineage of the constant-initializer findings

Four of the S1 findings are downstream of the [#149](https://github.com/buster14a/buster/issues/149)–[#154](https://github.com/buster14a/buster/issues/154) batch, which is closed. The audit did not cross-check the tracker, so a comment was added to each naming its predecessor: #185 is an uncovered arm of the same `c_ir_constant_cast` those fixes touched, while #186, #187 and #189 are the aggregate-initializer writers repeating defects the cast path has already had corrected. The pattern is itself worth acting on — the two paths should share one conversion helper rather than being fixed in parallel a third time.

## Limits carried over

Everything `README.md` says about the evidence still holds and is not weakened by publication. 17 of the 26 findings were reproduced first-hand; the other 9 came from subsystem passes and say so in their own **Evidence** line. Release self-host was not run (it needs TCC), and Windows, macOS and mobile targets were not exercised — the `codeview-*` findings come from cross-compiled containers, not from running them. No patches were prepared for any of these; they are reports.
