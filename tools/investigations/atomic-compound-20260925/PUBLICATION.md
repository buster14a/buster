# Publication record

New confirmed root cause: [#1186 — narrow atomic compound assignments and prefix increments return unconverted promoted values](https://github.com/buster14a/buster/issues/1186).

Existing-root follow-up, not a duplicate issue: [executed mixed-type atomic evidence on #1137](https://github.com/buster14a/buster/issues/1137#issuecomment-5833727983).

Only one new issue was filed. The proposed repair directions are not implemented or proven. No production source, generated binding, existing production workflow, protection, production branch, PR state or existing issue state was changed.

## Latest executed revision versus publication-time main

The last executed 160-cell replay is source **8ab1e194026dbb7c5391b25e68292743f4b96efb**, tree **a98734a3c1de9448e563afd588a1996cf8bb8b69**, with the exact commands/artifact hashes in README.md. The phrase "final current-main replay" there refers to that pinned source, not a claim that a moving branch cannot advance afterwards.

The final publication-time main read was **850433a06c309195e86b0a4827f11d3e1f16febc**, tree **a634dffdfad43a8e81da76c9b097f53961acc20f**. This last snapshot was **source-reconciled, not runtime-tested**.

The [net delta from the executed source](https://github.com/buster14a/buster/compare/8ab1e194026dbb7c5391b25e68292743f4b96efb...850433a06c309195e86b0a4827f11d3e1f16febc) is #1101: 21 additions/14 deletions in c_gen.c, plus 180 test lines. The complete patch was inspected. It changes control/call preparation for `__builtin_choose_expr`, including the selected-arm deferral, while retaining the existing `_Generic` behavior. The reduced static atomic inputs use neither builtin. The atomic arithmetic, RMW, result normalization and prefix/postfix helper bodies are not modified. This supports continued source applicability, but no execution on 850433a is claimed.

## Evidence location and scope

[README.md](README.md), [reduced-results.tsv](reduced-results.tsv), three exact executed C files, and the linked Python generators/workflow form the durable repository packet. The complete raw Actions ZIPs, including generated binaries, expire 2026-10-25. Their digests and the distinction between valid executions and failed setups are retained in README.md.

The accompanying downloadable text/source evidence bundle provided with the investigation includes raw command/outcome records, stdout/stderr, source snapshots, per-artifact manifests, build configuration, reductions and failed-attempt records. It intentionally excludes compiled binaries/compiler executables; its own BUNDLE_MANIFEST.json describes the included files. The original producer SHA256SUMS files also mention binaries available only in the original Actions ZIPs. Offline source/hash/disassembly processing is not represented as execution of the compiler in the assistant container.
