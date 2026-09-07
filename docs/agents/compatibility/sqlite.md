# SQLite compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in SQLite compatibility harness takes the official SQLite 3.53.4
downloads -- the amalgamation and the source distribution -- and neither is
copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_sqlite --config Release /path/to/sqlite-amalgamation-3530400 /path/to/sqlite-src-3530400
```

The first path is the extracted `sqlite-amalgamation-3530400.zip`, the second
the extracted `sqlite-src-3530400.zip`; both are pinned by content, the
amalgamation with a per-file byte count and Buster hash and the source
distribution through its `VERSION` and its Fossil `manifest.uuid`
(`bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc`) plus the
per-file pins of the nine upstream files it reaches into. The harness does not
drive upstream's configure script or its makefiles -- upstream build-system
detection is a later driver milestone -- and it does not build `testfixture`:
the TCL test suite needs the source-generation toolchain (tclsh, lemon,
`mkkeywordhash`) that the milestone explicitly defers, so the upstream tests it
runs are the ones that build against the amalgamation.

It compiles in named stages and fails at the first one that breaks: `library`
(`sqlite3.c`, one 9,5 MB translation unit of 295.692 lines), `shell`
(`shell.c`), `cli-link` (the `sqlite3` executable, linked through `ide cc`),
and `upstream-programs` (`test/speedtest1.c`, `test/wordcount.c`,
`test/kvtest.c` and `mptest/mptest.c`, each linked against the same library
object). Everything is built with one flag set --
`-DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_ENABLE_COLUMN_METADATA` and the
configuration's `-DSQLITE_THREADSAFE` -- because objects that disagree would
not link.

The cross-check is what the milestone rests on. Both compilers build the same
CLI, run the same deterministic SQL script, and must produce identical output
*and* byte-identical database files: SQLite's file format is fully specified,
so `main.db`, the `.backup` of it and the `.restore` of that backup are three
independent byte-for-byte comparisons per configuration and allocator, and
`speedtest1.db` and `wordcount.db` are two more over far larger schemas. The
script covers what the milestone asks for -- schema changes, transactions,
savepoints and rollback, indexes, joins, triggers, the eponymous virtual
tables the configuration has (`generate_series`, `pragma_table_info`), ANALYZE,
VACUUM, backup and restore, and `PRAGMA integrity_check`, `quick_check` and
`foreign_key_check` -- and it avoids every source of nondeterminism, so
`randomblob()` and `datetime()` are deliberately absent. `kvtest` fills its
blobs from the platform's random source, so it is run but not compared.

The upstream test suite runs in the staged subsets the milestone asks for:
`mptest` executes its own `multiwrite01.test`, `config01.test`, `config02.test`
and `crash01.test` scripts, which fork five client processes against one
database with different journal modes and memory-mapped I/O settings. That is
the part of the suite that reaches POSIX advisory locking, WAL-less recovery
and `mremap`, and it is where the harness found the loads that the address of
an indexed pointer must not perform.

Both configurations of the threading axis run: `threadsafe` first, which is
upstream's default build and the one that carries the upstream programs, then
`single-thread`. Every configuration runs under FAST, NONE, MIR_STACK and
QUALITY. `SQLITE_METRIC` lines carry per-unit compiler wall time with the
`-fsource-metrics=` source metrics, `SQLITE_LINK` the link time and image size,
`SQLITE_WORKLOAD` and `SQLITE_CODEGEN` the workload's wall time and
instructions retired, and `SQLITE_DATABASE` each database's size, hash and
whether it matched Clang. For scale, a full matrix takes about
four minutes, and one recorded run compiled the 295.692-line `sqlite3.c` in
351 ms under FAST and 368 ms under QUALITY, linked the CLI in 46 ms, and put
the SQL workload at 615,6 M instructions under FAST, 615,8 M under QUALITY,
1.088,9 M under MIR_STACK and 1.148,5 M under NONE -- the allocator ordering
the names promise. Generated objects, metrics, databases and logs remain under
`build/sqlite-3.53.4-<pid>/`, which is about 260 MB for a full matrix and is
not cleaned up on the way out, so delete the directories of runs you are done
with.

Two upstream constructs stay outside the harness for reasons worth keeping.
A `SQLITE_DEBUG` build is not part of the matrix: `assert()` expands to a GNU
statement expression, and `if( p->apCsr ) for(i=0; i<p->nCursor; i++) assert(
p->apCsr[i]==0 );` was measured as ending at the assert's closing brace rather
than at its semicolon until the statement-extent walk in `c_gen.c` (today
`c_ir_statement_end`) learned to find the body past the header. That shape
now compiles, but the debug build
still fails an internal assertion of its own, so it is a documented gap rather
than a passing configuration. And the harness never builds `testfixture`, as
above.
