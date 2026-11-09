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
  compiler, in six scenarios that run at once, each in a process of its own
  (each changes the directory and environment, and its results' shared mapping
  is its own): each waits out one-second timeouts, so in turn they took about 7 s
  and at once take about 2.
  `tests/c-testsuite/subset.txt` declares the programs of c-testsuite's commit
  `5c72756` that must pass, all 220 of them; the 9700X job fetches that commit
  and runs both, and the subset again with `--tier1`, which compiles them by the
  driver's `--tier1` and runs both paths' programs. [The c-testsuite guide](../../docs/c-testsuite.md) has the details.

## SQLite's tests (#146)

`src/buster/compiler/sqlite_test.c` builds SQLite's programs from the frozen
corpus's SQLite 3.53.2 (`$BUSTER_CORPUS/sqlite`) and runs SQLite's own tests
by them. `tools/ci/fetch-sqlite.sh DIR` fetches the
SQLite tree at 3.53.2's commit (`ccc132c`), whose amalgamation and shell are
the corpus's, as the c-testsuite fetch does; CI puts it in `build/sqlite`.
The owner chose all of SQLite's tests as #146's criterion: speedtest1
(`measure-tier1-programs`), the shell's own checks (below) and the Tcl suite
through `testfixture` (to come).

- `test-sqlite --clang PATH --sqlite DIR --tests DIR --scripts DIR
  [--directory DIR] [--keep]` builds the `sqlite3` shell (`shell.c` and
  `sqlite3.c`, with configure's shell options and math functions) by clang -O0,
  Tier 0 and `--tier1`. Each build runs SQLite's CLI tests (the nine `.sql`
  files `test/shellB.test` runs, in `--tests`, each as `sqlite3 :memory:
  ".read FILE"`), which must end "N tests run with 0 errors", and the scripts
  in `--scripts` (`tests/sqlite`: the SQL engine, the bundled extensions, the
  output modes and dot-commands, on-disk databases with `.selftest`), each in
  an empty directory. Every run's output and exit status must be clang's
  shell's, byte for byte. The hosted rows and native correctness run it
  (`correctness.tests`); it takes a few seconds.
