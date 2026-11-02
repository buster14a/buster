# foreign-debug

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/foreign_debug.c` builds `link-foreign`'s DWARF from the
  selected objects' debug sections (#395): concatenated by name in input order
  (strings not merged), references between debug sections resolved, those to
  code and data left to the writer, lld's tombstones for discarded code. Its
  relocation passes take 8 relocations at a time with the exact pass as their
  twin; `inflate.c` decompresses zlib-compressed sections (RFCs 1950 and 1951;
  serial within a stream, the streams spread over lanes, the Adler-32 64 bytes
  at a time). `test-inflate` checks the decompressor against gzip's streams,
  hand-made errors and every cut and changed byte at `PROT_NONE` pages;
  `test-foreign --cross-check` (`foreign_debug_test.c`) links gcc's and clang's
  `-g` fixtures by both paths and on lanes, against lld with
  `llvm-dwarfdump --verify` and gdb, and checks the relocation kernels' twins.
  The driver's contract ("Debug information") has the details.
