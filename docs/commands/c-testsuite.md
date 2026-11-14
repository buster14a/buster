# c-testsuite

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/c_testsuite.c` is #145's harness: `test-c-testsuite
  [--subset FILE] [--jobs N] [--timeout SECONDS] [--compiler PATH]
  [--clang PATH] [--id-order N] [--tier1] [--cross-check] [--code-size]
  [--check-code-size] [--keep] DIR` compiles each of
  c-testsuite's single-exec programs by the direct path, by both paths, the
  scalar one with its identifiers' ids in another order (#153's `--id-order`),
  into the same bytes, and runs it against its expected output;
  `--cross-check` also links its `-c` object with `cc`, and compares the
  functions and objects it defines with `clang -O0 -c`'s (#129,
  `c_lowering_symbols.c`). `--code-size` requires `--tier1`, implies
  `--cross-check`, and compares the corpus's sum of all emitted Tier 1 function
  bytes with the matching Clang `-O0` functions' sum. `--check-code-size`
  enables that measurement and requires the first sum not to exceed the
  second. A Tier 1 body without a Clang counterpart stays in the first sum
  and contributes no reference bytes, making the bound stricter. It reads the object
  compile's explicit `--tier1-function-sizes` report and the same two objects
  used by the symbol comparison. Unpaired counts and bytes show the portion
  already included in the Tier 1 total; fallback and whole executable-section
  bytes are separate. Both modes reject empty comparisons, missing or invalid
  evidence, and inconsistent matched/unmatched counts. `--code-size` reports
  the bound's `gate=pass|fail` with `enforced=0`, so an otherwise valid size
  excess does not fail the command. `--check-code-size` records `enforced=1`
  and makes that excess fail. The independent symbol cross-check still applies.
  `--self-test` checks the harness with a stand-in
  compiler, in seven scenarios that run at once, each in a process of its own
  (each changes the directory and environment, and its results' shared mapping
  is its own), including a working object whose requested size report is
  absent, which fails even in report-only mode. It also checks the size reader,
  the enforced and report-only aggregate thresholds, conservative unpaired-body
  accounting, invalid and empty evidence, disabled measurement, and the
  command's invalid size-option combinations.
  `tests/c-testsuite/subset.txt` declares the programs of c-testsuite's commit
  `5c72756` that must pass, all 220 of them; the 9700X job fetches that commit
  and runs both, and the subset again with `--tier1`, which compiles them by the
  driver's `--tier1` and runs both paths' programs. Hosted and native
  correctness use `--code-size` on that Tier 1 run: its current size excess
  remains visible as `gate=fail` with `enforced=0`. #1116's enforced corpus is
  the original 34-file Lua corpus (`measure-tier1-programs
  --check-code-size=lua`); the strict c-testsuite flag remains available.
  [The c-testsuite guide](../../docs/c-testsuite.md) has the details.

## SQLite's tests (#146)

`src/buster/compiler/sqlite_test.c` builds SQLite's programs from the frozen
corpus's SQLite 3.53.2 (`$BUSTER_CORPUS/sqlite`) and runs SQLite's own tests
by them. `tools/ci/fetch-sqlite.sh DIR` fetches the
SQLite tree at 3.53.2's commit (`ccc132c`), whose amalgamation and shell are
the corpus's, as the c-testsuite fetch does; CI puts it in `build/sqlite`.
The owner chose all of SQLite's tests as #146's criterion: speedtest1
(`measure-tier1-programs`), the shell's own checks (below) and the Tcl suite
through `testfixture` (below).

- `test-sqlite --clang PATH --sqlite DIR --tests DIR --scripts DIR
  [--directory DIR] [--keep]` builds the `sqlite3` shell (`shell.c` and
  `sqlite3.c`, with configure's shell options and math functions) by clang -O0,
  Tier 0 and `--tier1`. Each build runs SQLite's CLI tests (the nine `.sql`
  files `test/shellB.test` runs, in `--tests`, each as `sqlite3 :memory:
  ".read FILE"`), which must end "N tests run with 0 errors", and the scripts
  in `--scripts` (`tests/sqlite`: the SQL engine, the bundled extensions, the
  output modes and dot-commands, on-disk databases with `.selftest`), each in
  an empty directory, with 120 s (a build 600 s). Every script must be a
  readable regular file, checked before any build; each run reads a copy of
  it that ends with a `.print` of a sentinel line. Every run's output and exit
  status must be clang's shell's, byte for byte, and clang's shell must have
  read the script to its end (status 0 or 1, and the sentinel's line). The hosted rows and native correctness run it
  (`correctness.tests`); it takes a few seconds.
- `test-sqlite-testfixture --clang PATH --tree DIR --tcl DIR
  [--amalgamation FILE] [--subset FILE] [--jobs N] [--timeout SECONDS]
  [--directory DIR] [--keep]` (`sqlite_testfixture.c`) builds `testfixture`
  (Tcl, `tclsqlite.c`, SQLite's test sources and the amalgamation) three
  times by SQLite's own `configure --with-tcl=DIR` and `make testfixture`,
  each in a directory of its own: by clang -O0, Tier 0 and `--tier1`.
  `--tree` is `fetch-sqlite.sh`'s tree and `--tcl` the directory of Tcl 8.6's
  `tclConfig.sh` (`tools/ci/tcl-config.sh` finds it). The Makefile's
  `CFLAGS.core=` drops `-fPIC`, which the driver refuses (#2101); its
  `-rpath DIR` is accepted since #1854. With `--amalgamation`, each build's
  generated `sqlite3.c` must be that file (the frozen corpus's is). Each
  build then runs `testfixture test/testrunner.tcl --jobs N full` in an empty
  directory: SQLite's `full` permutation is every Tcl script of `test/` and
  `ext/` (1,451 of them; only the 13 `test/*.test` that run other scripts,
  such as `all.test`, `veryquick.test` and `permutations.test`, are left
  out), or with `--subset` the scripts its file names. clang's testfixture
  reads each run's `testrunner.db`; clang's run must have run every script
  once and passed them all, and each of the others' scripts must have clang's
  state, error count and test count (`crash.test`'s count is random and
  isn't compared). Run it as a user other than root: as root, eleven of
  SQLite's scripts that test read-only files and directories fail for every
  build.
  - **Every row (pull requests and main pushes too):** `correctness.tests`
    runs it with `tests/sqlite/testfixture-subset.txt`, the 1,215 scripts
    that took under 100 ms each in Tier 0's full run, about a minute in all.
  - **Nightly and dispatched runs:** `hosted-correctness.yml`'s `sqlite-full`
    job runs the whole permutation by the plain portable row's compiler,
    about 9 minutes a build on a 4-CPU runner. Main's pushes skip it (#2334):
    it took 27 to 41 runner-minutes of each push run.
  - **Tcl's pin:** the distribution's package. Hosted: Ubuntu 26.04's
    `tcl8.6-dev` 8.6.17+dfsg-1build1 (`setup-hosted.sh` installs it;
    `tcl-config.sh` fails a hosted run on another version, to be pinned
    again). The 9700X: Arch's `tcl` 8.6.16-1 (`/usr/lib64/tclConfig.sh`),
    installed by the owner on 2026-11-11, so native correctness runs the
    subset too. Elsewhere `tcl-config.sh` prints the version it found, and a
    host without the package skips the heading.
  - **Results (2026-11-11, main `d43c37e` with #2100, a 4-CPU cloud
    container, as a non-root user):** the full permutation, 1,451 scripts and
    1,031,345 tests, passed by all three builds, every script with clang's
    state, errors and count; the per-PR subset took 47 s. SQLite's own `SQLITE_DEBUG` isn't
    in testfixture's flags, so #1360's slow `-c` doesn't apply.
