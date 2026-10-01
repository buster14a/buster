/* Fixed zen5-calibration-v1 stage contract (#426), shared verbatim by the
 * three consumers so their bytes cannot drift apart:
 *   zen5_recipe.c        the trusted recipe driver asks for each stage by
 *                        name and, in its self-test, runs the same argv;
 *   systemd_broker.c     constructs the typed transient unit that runs it;
 *   credential_gate.c    checks the payload program and first argument.
 * The broker and gate deliberately have no Buster dependency, so this header
 * uses only the C library (snprintf) and string literals.
 *
 * Stage n (0..12) of the recipe is broker/gate stage BQ_ZEN5_STAGE_FIRST_NUMBER
 * + n. Stages 0..9 are the five serial trusted builds (generate, build) of
 * BQ_ZEN5_STAGE_BUILD_ROOTS, run as the service account with the configured
 * root as their only writable path; the trusted driver creates each root
 * before its generate. Stages 10..12 (oracle, pmu, captures) execute the
 * frozen binaries as the candidate account with one staging directory
 * writable. Every stage runs in ATTEMPT/base/source, with the broker's fixed
 * PATH=/usr/bin:/bin and LC_ALL=C (the gate clears everything else).
 *
 * PMU grant: the pmu stage's seccomp allow-list alone gains perf_event_open.
 * No capability is granted (the gate refuses any nonempty capability set, and
 * CAP_PERFMON would widen the unit to system-wide monitoring) and no host
 * setting changes: the host's perf_event_paranoid decides, and a refused or
 * unsupported counter is recorded as `invalid`, never as a zero count. The
 * PMU tool runs from ATTEMPT/zen5/pmu-tool, the driver-owned read-only copy
 * of the four pinned files, with -B -E -s so no snapshot file or environment
 * variable can shadow a module.
 *
 * Stop: a stage that exits normally has returned from `systemd-run --wait`
 * only after its unit is inactive, and KillMode=control-group has killed every
 * process in it, so nothing rewrites staging while the driver reads it. A
 * stage past the recipe deadline is different: the driver kills its broker
 * client and asks the broker to KILL the unit, then fails the attempt without
 * reading staging; the worker's cleanup proves every stage unit gone.
 *
 * Trust: every stage unit is denied the service and broker control sockets
 * (/run/buster-bench, /run/buster-bench-systemd-broker). The build stages
 * still run the revision's CMake as the service account with write access to
 * their root, so a zen5 revision must be as trusted as a smoke base: the
 * candidate account of the oracle, PMU and captures does not protect trusted
 * outputs from the revision itself.
 *
 * Map: BQ_ZEN5_STAGE_NAMES, BQ_ZEN5_STAGE_FIRST_NUMBER, BQ_ZEN5_STAGE_OUTER_VERB,
 * BQ_ZEN5_STAGE_BUILD_ROOTS, BqZen5StageCommand, bq_zen5_stage_command.
 */
#ifndef BUSTER_BENCH_ZEN5_STAGE_H
#define BUSTER_BENCH_ZEN5_STAGE_H
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BQ_ZEN5_STAGE_RECIPE "zen5-calibration-v1"
/* Broker CLI verb that starts the outer unit of a zen5 job. */
#define BQ_ZEN5_STAGE_OUTER_VERB "start-zen5-outer"
#define BQ_ZEN5_STAGE_COUNT 13u
/* Smoke stages are 0..5 and #1020 retirement stages 6..9; 10..15 stay free. */
#define BQ_ZEN5_STAGE_FIRST_NUMBER 16u
#define BQ_ZEN5_STAGE_BUILDS 5u
#define BQ_ZEN5_STAGE_ORACLE 10u
#define BQ_ZEN5_STAGE_PMU 11u
#define BQ_ZEN5_STAGE_CAPTURES 12u
/* Typed broker stage names, in stage order. */
#define BQ_ZEN5_STAGE_NAMES "zen5-immutable-generate", "zen5-immutable-build", "zen5-same-root-A-generate", \
    "zen5-same-root-A-build", "zen5-same-root-B-generate", "zen5-same-root-B-build", "zen5-cross-root-A-generate", \
    "zen5-cross-root-A-build", "zen5-cross-root-B-generate", "zen5-cross-root-B-build", "zen5-oracle", "zen5-pmu", \
    "zen5-captures"
/* Configured roots of builds 0..4, relative to the attempt workspace. */
#define BQ_ZEN5_STAGE_BUILD_ROOTS "zen5/builds/immutable", "zen5/builds/same-root", "zen5/builds/same-root", \
    "zen5/builds/cross-root-a", "zen5/builds/cross-root-b"
#define BQ_ZEN5_STAGE_BUILD_IDS "immutable", "same-root-A", "same-root-B", "cross-root-A", "cross-root-B"

#define BQ_ZEN5_STAGE_DRIVER "/usr/local/libexec/buster-bench-build"
#define BQ_ZEN5_STAGE_PYTHON "/usr/bin/python3"
#define BQ_ZEN5_STAGE_CAPTURE_VERB "bench_service_zen5_capture"
#define BQ_ZEN5_STAGE_PERF_SYSCALL "perf_event_open"
/* Equal to the installed profile's workload= and cpu= (zen5_profile_test.py). */
#define BQ_ZEN5_STAGE_WORKLOAD "tests/c_abi_cfuncs.c"
#define BQ_ZEN5_STAGE_CPU "2"
#define BQ_ZEN5_STAGE_CPU_NUMBER 2u
/* The profile's pmu-capture pin, and the driver-owned directory holding the
 * verified copies of the four pinned PMU files (by basename). */
#define BQ_ZEN5_STAGE_PMU_TOOL "tools/zen5_host_qualification.py"
#define BQ_ZEN5_STAGE_PMU_TOOL_DIRECTORY "zen5/pmu-tool"
#define BQ_ZEN5_STAGE_PMU_TOOL_NAME "zen5_host_qualification.py"

/* Attempt-relative paths the trusted driver writes and every stage reads. */
#define BQ_ZEN5_STAGE_SOURCE "base/source"
#define BQ_ZEN5_STAGE_IMMUTABLE_BINARY "zen5/frozen/immutable/ide"
#define BQ_ZEN5_STAGE_ORACLE_SPEC "zen5/oracle.spec"
#define BQ_ZEN5_STAGE_ORACLE_STAGING "zen5/staging/oracle"
#define BQ_ZEN5_STAGE_PMU_STAGING "zen5/staging/pmu"
#define BQ_ZEN5_STAGE_CAPTURE_STAGING "zen5/staging/captures"
#define BQ_ZEN5_STAGE_REPOSITORY "zen5/repository.json"
#define BQ_ZEN5_STAGE_PROFILE "zen5/recipe.profile"

#define BQ_ZEN5_STAGE_MAX_ARGS 32u
#define BQ_ZEN5_STAGE_TEXT 512u

typedef struct BqZen5StageCommand
{
    char const* argv[BQ_ZEN5_STAGE_MAX_ARGS + 1];
    char text[BQ_ZEN5_STAGE_MAX_ARGS][BQ_ZEN5_STAGE_TEXT];
    char directory[BQ_ZEN5_STAGE_TEXT];
    char writable[BQ_ZEN5_STAGE_TEXT];
    unsigned count;
    bool service;
    bool perf;
    bool valid;
} BqZen5StageCommand;

static inline void bq_zen5_stage_literal(BqZen5StageCommand* command, char const* literal)
{
    if (command->valid && command->count < BQ_ZEN5_STAGE_MAX_ARGS)
        command->argv[command->count++] = literal;
    else
        command->valid = false;
}

/* ATTEMPT/relative (or ATTEMPT/relative/leaf) as one argument. */
static inline void bq_zen5_stage_path(BqZen5StageCommand* command, char const* attempt, char const* relative,
                                      char const* leaf)
{
    if (command->valid && command->count < BQ_ZEN5_STAGE_MAX_ARGS)
    {
        int length = snprintf(command->text[command->count], BQ_ZEN5_STAGE_TEXT, "%s/%s%s%s", attempt, relative,
                              leaf ? "/" : "", leaf ? leaf : "");
        command->valid = length > 0 && (unsigned)length < BQ_ZEN5_STAGE_TEXT;
        if (command->valid)
        {
            command->argv[command->count] = command->text[command->count];
            command->count += 1;
        }
    }
    else
        command->valid = false;
}

/* The exact argv, working directory, writable path, account and syscall
 * allowance of recipe stage `index` for the attempt directory `attempt`. */
static inline bool bq_zen5_stage_command(unsigned index, char const* attempt, BqZen5StageCommand* command)
{
    static char const* const roots[BQ_ZEN5_STAGE_BUILDS] = {BQ_ZEN5_STAGE_BUILD_ROOTS};
    static char const* const generate_options[] = {"--cc", "clang", "--no-include-tests", "--no-developer-targets",
        "--no-check-optional-warnings", "--no-fuzz", "--no-sanitize", "--no-time-trace", "--no-instrument", "--no-lto"};
    static char const* const build_options[] = {"-t", "ide", "--", "-j1"};
    static char const* const captures[] = {"immutable", "same-root-rebuild", "cross-root"};
    memset(command, 0, sizeof(*command));
    command->valid = attempt && attempt[0] == '/' && index < BQ_ZEN5_STAGE_COUNT;
    int directory = command->valid ? snprintf(command->directory, sizeof(command->directory), "%s/%s", attempt,
                                              BQ_ZEN5_STAGE_SOURCE) : -1;
    command->valid = command->valid && directory > 0 && (unsigned)directory < sizeof(command->directory);
    char const* writable = index < BQ_ZEN5_STAGE_ORACLE ? roots[index / 2u] : index == BQ_ZEN5_STAGE_ORACLE ?
                           BQ_ZEN5_STAGE_ORACLE_STAGING : index == BQ_ZEN5_STAGE_PMU ? BQ_ZEN5_STAGE_PMU_STAGING :
                           BQ_ZEN5_STAGE_CAPTURE_STAGING;
    int written = command->valid ? snprintf(command->writable, sizeof(command->writable), "%s/%s", attempt,
                                            writable) : -1;
    command->valid = command->valid && written > 0 && (unsigned)written < sizeof(command->writable);
    command->service = index < BQ_ZEN5_STAGE_ORACLE;
    command->perf = index == BQ_ZEN5_STAGE_PMU;
    if (command->valid && index < BQ_ZEN5_STAGE_ORACLE)
    {
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_DRIVER);
        bq_zen5_stage_literal(command, index % 2u == 0 ? "generate" : "build");
        bq_zen5_stage_literal(command, "--build-directory");
        bq_zen5_stage_path(command, attempt, roots[index / 2u], NULL);
        bq_zen5_stage_literal(command, "--config");
        bq_zen5_stage_literal(command, "Release");
        unsigned count = index % 2u == 0 ? (unsigned)(sizeof(generate_options) / sizeof(generate_options[0])) :
                                           (unsigned)(sizeof(build_options) / sizeof(build_options[0]));
        for (unsigned option = 0; option < count; option += 1)
            bq_zen5_stage_literal(command, index % 2u == 0 ? generate_options[option] : build_options[option]);
    }
    else if (command->valid && index == BQ_ZEN5_STAGE_ORACLE)
    {
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_DRIVER);
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_CAPTURE_VERB);
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_ORACLE_SPEC, NULL);
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_ORACLE_STAGING, "oracle.record");
    }
    else if (command->valid && index == BQ_ZEN5_STAGE_PMU)
    {
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_PYTHON);
        bq_zen5_stage_literal(command, "-B");
        bq_zen5_stage_literal(command, "-E");
        bq_zen5_stage_literal(command, "-s");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_PMU_TOOL_DIRECTORY, BQ_ZEN5_STAGE_PMU_TOOL_NAME);
        bq_zen5_stage_literal(command, "capture");
        bq_zen5_stage_literal(command, "--output");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_PMU_STAGING, "zen5-pmu-v1.json");
        bq_zen5_stage_literal(command, "--cpu");
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_CPU);
        bq_zen5_stage_literal(command, "--repository-root");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_SOURCE, NULL);
        bq_zen5_stage_literal(command, "--repository-identity");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_REPOSITORY, NULL);
        bq_zen5_stage_literal(command, "--working-directory");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_SOURCE, NULL);
        bq_zen5_stage_literal(command, "--input");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_SOURCE, BQ_ZEN5_STAGE_WORKLOAD);
        bq_zen5_stage_literal(command, "--output-artifact");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_PMU_STAGING, "output.o");
        bq_zen5_stage_literal(command, "--environment-input");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_PROFILE, NULL);
        bq_zen5_stage_literal(command, "--");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_IMMUTABLE_BINARY, NULL);
        bq_zen5_stage_literal(command, "cc");
        bq_zen5_stage_literal(command, "-g0");
        bq_zen5_stage_literal(command, "-O0");
        bq_zen5_stage_literal(command, "-c");
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_WORKLOAD);
        bq_zen5_stage_literal(command, "-o");
        bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_PMU_STAGING, "output.o");
    }
    else if (command->valid)
    {
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_DRIVER);
        bq_zen5_stage_literal(command, BQ_ZEN5_STAGE_CAPTURE_VERB);
        for (unsigned capture = 0; capture < sizeof(captures) / sizeof(captures[0]); capture += 1)
        {
            char spec[64], output[64];
            snprintf(spec, sizeof(spec), "zen5/%s.spec", captures[capture]);
            snprintf(output, sizeof(output), "%s.json", captures[capture]);
            bq_zen5_stage_path(command, attempt, spec, NULL);
            bq_zen5_stage_path(command, attempt, BQ_ZEN5_STAGE_CAPTURE_STAGING, output);
        }
    }
    if (command->valid) command->argv[command->count] = NULL;
    return command->valid;
}
#endif
