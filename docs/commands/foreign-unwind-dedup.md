# foreign-unwind-dedup

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_unwind_dedup.c` plans bounded terminal CIE
  pooling, and `foreign_unwind_dedup_link.c` transactionally copies/remaps
  the foreign link. Run it after COMDAT pruning and optional GC; its result
  goes only to terminal pooling or executable writing, never ET_REL output
  or another GC/original-import pass. `test-foreign --cross-check` covers
  exact/vector plans, rollback, record/endpoint maps, mixed alignments and
  COMDAT-to-GC-to-pooling C++ throw/catch/destructor behavior.
  `benchmark-foreign-unwind-dedup [--counts LIST] [--runs N]` measures fresh
  whole links through cleanup with pooling on/off and duplicate/unique
  original CFI fixtures. The linker guide's "Foreign terminal CIE pooling"
  defines the one-CIE pool cap, source-only local-reference allowance,
  borrowed lifetimes, alignment and pending performance evidence. Use the
  shared 9700X lock and CPU 2 for each measurement process.
