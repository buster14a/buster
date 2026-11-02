# foreign-gc

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_gc.c` returns stable live atom/symbol/fixup
  maps, and `foreign_gc_link.c` composes COMDAT and FDE/CIE reachability into
  a fresh foreign link. Use `foreign_link_build_for_gc` to defer undefined
  references until collection; ordinary conversion remains eager. The
  adapter remaps external identities and retains explicit, init/fini,
  dynamic-export and GNU-retain roots. `test-foreign --cross-check` includes
  independent closure models, guarded tails, rollback, unwind-aware output
  and GNU ld/lld runtime checks. `benchmark-foreign-gc [--counts LIST]
  [--runs N]` measures fresh links with GC on/off and exact/vector paths,
  including cleanup, against independent maps/CFI and complete output bytes.
  Its original static leaf fixtures include mixed-dead and all-live controls;
  these are not corpus or multi-lane evidence. Run tests under the shared
  9700X lock and pin each benchmark process to CPU 2. The linker guide's
  "Foreign section garbage collection" documents borrowed lifetimes,
  terminal-pooling order and complete measurement boundaries.
