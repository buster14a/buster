/* Fixed #1020 retirement build-stage contract, shared verbatim by three
 * consumers so their bytes cannot drift apart:
 *   retirement_matched_build.c  builds each stage's command and binds its
 *                               digest (argv, cwd, environment, umask);
 *   systemd_broker.c            constructs the typed stage unit that runs it;
 *   credential_gate.c           checks the payload and fixes its environment.
 * The broker and gate deliberately have no Buster dependency, so this header
 * uses only the preprocessor and string literals.
 *
 * Stage n (0..3) is baseline generate, baseline build, candidate generate,
 * candidate build. Its argv is exactly
 *   DRIVER <generate|build> --build-directory <attempt>/<PARENT>/<LEAF>
 *          --config CONFIG <GENERATE_OPTIONS | BUILD_OPTIONS>
 * with cwd <attempt>/<base|candidate>/source, environment exactly
 * PATH=<TOOLCHAIN_ROOT>/bin LC_ALL=C TZ=UTC HOME=/nonexistent, and umask 0077
 * (baseline) or 0007 (candidate). The service creates each configured root
 * fresh, service-owned, immediately before its generate; the broker names
 * exactly that root as the stage's only ReadWritePaths entry, so the stage
 * can fill it but cannot replace it or write anywhere else.
 *
 * Map: BQ_RETIREMENT_STAGE_NAMES, _DRIVER, _TOOLCHAIN_ROOT, _*_VALUE,
 * _BASE_PARENT, _CANDIDATE_PARENT, _BUILD_LEAF, _GENERATE_OPTIONS,
 * _BUILD_OPTIONS, _FIRST_NUMBER.
 */
#ifndef BUSTER_BENCH_RETIREMENT_STAGE_H
#define BUSTER_BENCH_RETIREMENT_STAGE_H

#define BQ_RETIREMENT_STAGE_COUNT 4u
/* Typed broker stage names, in stage order. */
#define BQ_RETIREMENT_STAGE_NAMES "retirement-base-generate", "retirement-base-build", \
    "retirement-candidate-generate", "retirement-candidate-build"
/* The broker's and gate's numeric stage for retirement stage 0; the smoke
 * stages keep 0..5. */
#define BQ_RETIREMENT_STAGE_FIRST_NUMBER 6u

#define BQ_RETIREMENT_STAGE_DRIVER "/usr/local/libexec/buster-bench-build"
#define BQ_RETIREMENT_STAGE_TOOLCHAIN_ROOT "/opt/buster-bench/installed/toolchain/native-retirement-performance-v1"

/* Environment values; each consumer spells NAME=VALUE from these. */
#define BQ_RETIREMENT_STAGE_PATH_VALUE BQ_RETIREMENT_STAGE_TOOLCHAIN_ROOT "/bin"
#define BQ_RETIREMENT_STAGE_LC_ALL_VALUE "C"
#define BQ_RETIREMENT_STAGE_TZ_VALUE "UTC"
#define BQ_RETIREMENT_STAGE_HOME_VALUE "/nonexistent"

/* Configured roots, relative to the attempt workspace. base/build is the
 * materializer's service-private 02700 directory; candidate/ is its 02710
 * subject directory, which the candidate group may only traverse. */
#define BQ_RETIREMENT_STAGE_BASE_PARENT "base/build"
#define BQ_RETIREMENT_STAGE_CANDIDATE_PARENT "candidate"
#define BQ_RETIREMENT_STAGE_BUILD_LEAF "matched-build"

#define BQ_RETIREMENT_STAGE_CONFIG "Release"
#define BQ_RETIREMENT_STAGE_GENERATE_OPTIONS "--cc", "clang", "--no-include-tests", "--no-developer-targets", \
    "--no-check-optional-warnings", "--no-fuzz", "--no-sanitize", "--no-time-trace", "--no-instrument", "--no-lto"
#define BQ_RETIREMENT_STAGE_BUILD_OPTIONS "-t", "ide", "--", "-j1"

#endif
