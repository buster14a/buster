# gnu

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_gnu.c` retains bounded GNU attribute groups/arguments
  and declarator asm labels when `ParseOptions.extension_metadata` is enabled.
  `test-gnu --clang` checks exact records, scalar/vector agreement and guarded
  capacities; `benchmark-gnu [-E's options] file.c` measures collection,
  allocation and the parser handoff. `test-types` also checks GNU machine-mode
  type actions. [The GNU extension guide](../../docs/c-gnu-extensions.md) records
  the source-checker/lowering work still required before #147 can close.
