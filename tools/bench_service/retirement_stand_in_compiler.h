/* The unit-oracle fixture's stand-in compilers (#881 PR 2 fixture).
 *
 * The matched-build fixture driver (retirement_matched_build_fixture.c)
 * freezes these bytes as a census-fixture subject's compiler, and the
 * preparation runner hands the same bytes to the census emitter
 * (retirement_validator_eligibility_test.py --emit --compilers), so the
 * census manifest names them and the projection's compiler join holds. They
 * are dash scripts in lane B's campaign argv shape (retirement_worker_unit_tests.h
 * pins the row plan): `compile SOURCE FIXTURE OUTPUT METRICS --label=N`
 * copies SOURCE/FIXTURE.out and .metrics out of A's candidate root (`program`,
 * the runtime rows' compile, copies SOURCE/FIXTURE.program, an executable);
 * `batch GROUP SOURCE @LIST METRICS --label=N` writes r<row>.o (the row from
 * the metrics leaf, b<row>.metrics timed or u<row>.metrics untimed) from
 * SOURCE/tests/unit.c.o and the one-input metrics record
 * SOURCE/tests/batch.metrics (every fixture object group has one member and
 * the fixture's untimed contract names the timed batch target). They prove the
 * campaign's mechanics, never a compiler. Jobs select behaviours by the work
 * directory's path: job 85 fails every launch; jobs 86 and 87 sleep a second
 * in every A/A second-label launch, and job 86's also leave
 * BQ_RETIREMENT_STAND_IN_AA_MARKER in the work directory, so the fixture's
 * SIGTERM lands in the first of them instead of after a fixed delay; job 88's
 * second-label launches leave a detached (`setsid`) sleeper behind, its pid
 * in escaped.pid in the work directory. Every launch costs one dash exec
 * and one `cat` per file it writes (plus `chmod` for an executable): the
 * path test reads $PWD, which dash sets from getcwd at start-up, instead of
 * forking for $(pwd). The two sides differ only in their comment, so their
 * digests differ.
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_STAND_IN_COMPILER_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_STAND_IN_COMPILER_H

#define BQ_RETIREMENT_STAND_IN_AA_MARKER "aa.started"

#define BQ_RETIREMENT_STAND_IN_COMPILER(side) \
    "#!/bin/sh\n# stand-in compiler " side "\n" \
    "case \"$PWD\" in\n" \
    "  */job-85-attempt-*) exit 9 ;;\n" \
    "  */job-86-attempt-*) case \"$*\" in *--label=2*) : > " BQ_RETIREMENT_STAND_IN_AA_MARKER "; sleep 1 ;; esac ;;\n" \
    "  */job-87-attempt-*) case \"$*\" in *--label=2*) sleep 1 ;; esac ;;\n" \
    "  */job-88-attempt-*) case \"$*\" in *--label=2*)\n" \
    "    setsid sh -c 'exec sleep 60' </dev/null >/dev/null 2>&1 & echo $! > escaped.pid ;; esac ;;\n" \
    "esac\n" \
    "if [ \"$1\" = compile ]; then\n" \
    "  cat \"$2/$3.out\" > \"$4\" && chmod 0700 \"$4\" && cat \"$2/$3.metrics\" > \"$5\" && exit 0\n" \
    "  exit 1\nfi\n" \
    "if [ \"$1\" = program ]; then\n" \
    "  cat \"$2/$3.program\" > \"$4\" && chmod 0700 \"$4\" && cat \"$2/$3.metrics\" > \"$5\" && exit 0\n" \
    "  exit 1\nfi\n" \
    "if [ \"$1\" = batch ]; then\n" \
    "  m=$5; r=${m#?}; r=${r%.metrics}\n" \
    "  cat \"$3/tests/unit.c.o\" > \"r$r.o\" && cat \"$3/tests/batch.metrics\" > \"$m\" && exit 0\n" \
    "  exit 1\nfi\nexit 2\n"
#define BQ_RETIREMENT_STAND_IN_BASE BQ_RETIREMENT_STAND_IN_COMPILER("base")
#define BQ_RETIREMENT_STAND_IN_CANDIDATE BQ_RETIREMENT_STAND_IN_COMPILER("candidate")

#endif
