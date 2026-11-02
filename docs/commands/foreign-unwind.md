# foreign-unwind

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_unwind.c` and `foreign_unwind_link.c` prune
  losing COMDAT FDEs and unused CIEs before canonical foreign symbols hide
  their original ownership. They retain matching CFI/personality/LSDA
  records and map symbols into compact unwind bytes; compatible live-CIE
  deduplication is separate. `test-foreign --cross-check` includes their
  malformed/rollback tests and original C++ throw/catch/cleanup fixture.
  `benchmark-foreign-unwind [--runs N] [--fdes N] [--profile mixed|all|none]`
  measures full pruning with exact/vector parity and independent expected
  bytes/maps. Run each measurement under the shared 9700X lock on CPU 2;
  these synthetic phase results are not whole-link acceptance. The linker
  guide's "Foreign COMDAT unwind pruning" documents the supported encodings,
  incoming-reference restrictions and benchmark memory/timing boundaries.
