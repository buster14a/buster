# c-testsuite

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_testsuite.c` is #145's harness: `test-c-testsuite
  [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH]
  [--clang PATH] [--id-order N] [--cross-check] [--keep] DIR` compiles each of
  c-testsuite's single-exec programs by the direct path, by both paths, the
  scalar one with its identifiers' ids in another order (#153's `--id-order`),
  into the same bytes, and runs it against its expected output;
  `--cross-check` also links its `-c` object with `cc`, and compares the
  functions and objects it defines with `clang -O0 -c`'s (#129,
  `c_lowering_symbols.c`). `--self-test` checks the harness with a stand-in
  compiler.
  `tests/c-testsuite/subset.txt` declares the programs of c-testsuite's commit
  `5c72756` that must pass, all 220 of them; the 9700X job fetches that commit
  and runs both, and the subset again with `--tier1`, which compiles them by the
  driver's `--tier1` and runs both paths' programs. [The c-testsuite guide](../../docs/c-testsuite.md) has the details.
