/* Self-test for tools/bench_service/zen5_recipe.c (held zen5-calibration-v1).
 *
 * bench_service_zen5_recipe_self_test builds a disposable materialized
 * attempt: the five "builds" are fake-driver copies of /bin/sh, and the
 * workload compiler is a `cc` script that the copied shell runs from the
 * source directory. It then runs the real recipe phases, the real capture
 * runner (this executable) and the real PMU tool with perf/taskset stand-ins,
 * and replays the result with the landed readers (zen5_aa_noise.py,
 * zen5_build_control.py, zen5_host_qualification.py) and the #1053 handoff
 * consumer (zen5_calibration_handoff.py), using capture digests the test
 * computes itself rather than reads from the bundle.
 *
 * Cases (bench_service_zen5_test_case): success and replay, the plan frozen
 * before the first timed child, with snapshot modules that would shadow the
 * PMU tool's standard library; a changed plan refused before any child; a
 * pinned-tool mismatch; a missing and an unlisted tree identity; base !=
 * candidate; a budget that cannot cover the timing reserve; a frozen binary
 * changed before the oracle, before the captures stage (only the capture
 * runner's spec digests can notice) and after it; and a stage that
 * runs past the recipe deadline. With KEEP_DIR only the success case runs and
 * its result tree is kept for the tests.c export-content bridge. Fixtures
 * honor TMPDIR. Eligibility is exact (bench_service_zen5_test_ineligible_reason:
 * Linux x86-64, an x86 /proc/cpuinfo and python3): an eligible host must run
 * and pass every case, and zero cases there fail; an ineligible host prints
 * result=skipped-ineligible-host reason=... and exits 4, neither success nor
 * failure (bench_service_zen5_test_verdict).
 */
#if BUSTER_LINUX
#define BENCH_SERVICE_ZEN5_TEST_REVISION "1111111111111111111111111111111111111111"
#define BENCH_SERVICE_ZEN5_TEST_TREE "2222222222222222222222222222222222222222"

typedef struct BenchServiceZen5TestFixture BenchServiceZen5TestFixture;
struct BenchServiceZen5TestFixture
{
    char root[BENCH_SERVICE_RECIPE_PATH_CAP];
    char workspaces[BENCH_SERVICE_RECIPE_PATH_CAP];
    char attempt[BENCH_SERVICE_RECIPE_PATH_CAP];
    char source[BENCH_SERVICE_RECIPE_PATH_CAP];
    char candidate_source[BENCH_SERVICE_RECIPE_PATH_CAP];
    char result[BENCH_SERVICE_RECIPE_PATH_CAP];
};

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_copy(Arena* arena, char const* from, char const* to, mode_t mode)
{
    String8 bytes = {0};
    bool ok = bench_service_zen5_read(arena, from, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &bytes) &&
              bench_service_zen5_write_new(to, bytes, mode);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_directory(char* output, char const* parent, char const* name, mode_t mode)
{
    bool ok = bench_service_zen5_path(output, parent, name) && bench_service_recipe_test_mkdir(output, mode);
    return ok;
}

/* A private directory under TMPDIR (default /tmp) named from `prefix`. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_mkdtemp(char output[BENCH_SERVICE_RECIPE_PATH_CAP], char const* prefix)
{
    char const* temporary = getenv("TMPDIR");
    int length = snprintf(output, BENCH_SERVICE_RECIPE_PATH_CAP, "%s/%s-XXXXXX",
                          temporary && temporary[0] == '/' ? temporary : "/tmp", prefix);
    bool ok = length > 0 && (u32)length < BENCH_SERVICE_RECIPE_PATH_CAP && mkdtemp(output) != NULL;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_fixture(Arena* arena, BenchServiceZen5TestFixture* fixture,
                                                         char const* keep, bool tree, bool tree_listed,
                                                         char const* tampered_tool)
{
    *fixture = (BenchServiceZen5TestFixture){0};
    char root_template[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool ok = keep ? snprintf(fixture->root, sizeof(fixture->root), "%s", keep) > 0 :
                     bench_service_zen5_test_mkdtemp(root_template, "buster-bench-zen5");
    if (ok && !keep) snprintf(fixture->root, sizeof(fixture->root), "%s", root_template);
    char base[BENCH_SERVICE_RECIPE_PATH_CAP], candidate[BENCH_SERVICE_RECIPE_PATH_CAP], results[BENCH_SERVICE_RECIPE_PATH_CAP];
    char tools[BENCH_SERVICE_RECIPE_PATH_CAP], tests[BENCH_SERVICE_RECIPE_PATH_CAP], scratch[BENCH_SERVICE_RECIPE_PATH_CAP];
    ok = ok && bench_service_zen5_test_directory(fixture->workspaces, fixture->root, "workspaces", 02710) &&
         bench_service_zen5_test_directory(fixture->attempt, fixture->workspaces, "job-1-attempt-2", 02710) &&
         bench_service_zen5_test_directory(base, fixture->attempt, "base", 02710) &&
         bench_service_zen5_test_directory(candidate, fixture->attempt, "candidate", 02710) &&
         bench_service_zen5_test_directory(fixture->source, base, "source", 0700) &&
         bench_service_zen5_test_directory(fixture->candidate_source, candidate, "source", 0700) &&
         bench_service_zen5_test_directory(scratch, base, "build", 0700) &&
         bench_service_zen5_test_directory(scratch, candidate, "build", 0700) &&
         bench_service_zen5_test_directory(results, fixture->workspaces, "results", 0700) &&
         bench_service_zen5_test_directory(fixture->result, results, "job-1-attempt-2", 0700) &&
         bench_service_zen5_test_directory(tools, fixture->source, "tools", 0700) &&
         bench_service_zen5_test_directory(tests, fixture->source, "tests", 0700);
    char const* copied[] = {"tools/zen5_host_qualification.py", "tools/zen5_qualification_common.py",
                            "tools/zen5_qualification_replay.py", "tools/zen5_pmu_events_v1.json", "tests/c_abi_cfuncs.c"};
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(copied); index += 1)
    {
        char destination[BENCH_SERVICE_RECIPE_PATH_CAP];
        ok = bench_service_zen5_path(destination, fixture->source, copied[index]) &&
             bench_service_zen5_test_copy(arena, copied[index], destination, 0644);
        if (ok && tampered_tool && !strcmp(copied[index], tampered_tool))
        {
            int descriptor = open(destination, O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
            ok = bench_service_zen5_write_all(descriptor, (u8 const*)"\n", 1);
            if (descriptor >= 0) close(descriptor);
        }
    }
    /* Snapshot files named after standard modules: the PMU stage must import
     * only its pinned copies, so these would fail the success case. */
    char const* shadows[] = {"tools/decimal.py", "tools/json.py", "tools/hashlib.py"};
    char path[BENCH_SERVICE_RECIPE_PATH_CAP];
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(shadows); index += 1)
        ok = bench_service_zen5_path(path, fixture->source, shadows[index]) &&
             bench_service_recipe_test_write(path, "raise SystemExit(97)\n", 0444);
    /* The installed source manifest lists the tree identity file. */
    char tree_digest[SHA256_HEX_CAPACITY] = {0};
    String8 tree_bytes = S8(BENCH_SERVICE_ZEN5_TEST_TREE "\n");
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, tree_bytes.pointer, tree_bytes.length);
    sha256_finish_hex(&hash, (char8*)tree_digest);
    char manifest[512];
    int manifest_length = snprintf(manifest, sizeof(manifest),
                                   "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=" BENCH_SERVICE_ZEN5_TEST_REVISION "\n"
                                   "%s .bq-source-tree\n"
                                   "0000000000000000000000000000000000000000000000000000000000000000 cc\n",
                                   tree_listed ? tree_digest : "1111111111111111111111111111111111111111111111111111111111111111");
    ok = ok && manifest_length > 0 && (u32)manifest_length < sizeof(manifest) &&
         bench_service_zen5_path(path, fixture->source, ".source-manifest") &&
         bench_service_recipe_test_write(path, manifest, 0444);
    ok = ok && bench_service_zen5_path(path, fixture->source, "cc") &&
         bench_service_recipe_test_write(path,
             "#!/bin/sh\n"
             "in=\"\"\n"
             "out=\"\"\n"
             "while [ $# -gt 0 ]; do\n"
             "  case \"$1\" in -c) in=\"$2\"; shift 2;; -o) out=\"$2\"; shift 2;; *) shift;; esac\n"
             "done\n"
             "[ -n \"$in\" ] && [ -n \"$out\" ] || exit 3\n"
             "cat \"$in\" > \"$out\"\n", 0555);
    if (ok && tree)
        ok = bench_service_zen5_path(path, fixture->source, ".bq-source-tree") &&
             bench_service_recipe_test_write(path, BENCH_SERVICE_ZEN5_TEST_TREE "\n", 0444);
    ok = ok && chmod(fixture->source, 0550) == 0 && chmod(fixture->candidate_source, 0550) == 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bench_service_zen5_test_cleanup(Arena* arena, BenchServiceZen5TestFixture const* fixture, bool keep)
{
    chmod(fixture->source, 0700);
    chmod(fixture->candidate_source, 0700);
    remove_path_recursive(arena, string_from_pointer(keep ? fixture->attempt : fixture->root));
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_scripts(Arena* arena, char scripts[BENCH_SERVICE_RECIPE_PATH_CAP])
{
    char root_template[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool ok = bench_service_zen5_test_mkdtemp(root_template, "buster-bench-zen5-scripts");
    if (ok) snprintf(scripts, BENCH_SERVICE_RECIPE_PATH_CAP, "%s", root_template);
    char const* names[] = {"driver", "ninja", "perf", "taskset"};
    char const* bodies[] = {
        "#!/bin/sh\n"
        "set -eu\n"
        "mode=\"${1:-}\"\n"
        "build=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = \"--build-directory\" ]; then build=\"$2\"; shift 2; else shift; fi\n"
        "done\n"
        "[ -n \"$build\" ] || exit 40\n"
        "case \"$mode\" in\n"
        "generate) rm -rf \"$build\"; mkdir -p \"$build/Release\"\n"
        "  printf '[{\"directory\":\"%s\",\"command\":\"clang -c unity.c\"}]\\n' \"$build\" > \"$build/compile_commands.json\";;\n"
        "build) [ -z \"${BENCH_SERVICE_ZEN5_TEST_SLOW:-}\" ] || sleep 60\n"
        "  cp \"$(readlink -f /bin/sh)\" \"$build/Release/ide\"; chmod 0755 \"$build/Release/ide\"; echo \"built $build\";;\n"
        "*) exit 41;;\n"
        "esac\n",
        "#!/bin/sh\n"
        "root=\"\"\n"
        "while [ $# -gt 0 ]; do\n"
        "  if [ \"$1\" = -C ]; then root=\"$2\"; shift 2; else shift; fi\n"
        "done\n"
        "printf 'clang -O2 -I%s/generated -c /src/unity.c -o %s/unity.o\\nclang %s/unity.o -o %s/Release/ide\\n' "
        "\"$root\" \"$root\" \"$root\" \"$root\"\n",
        "#!/bin/sh\n"
        "if [ \"${1:-}\" = --version ]; then echo 'perf version 0.0-zen5-self-test'; exit 0; fi\n"
        "while [ $# -gt 0 ] && [ \"$1\" != -- ]; do shift; done\n"
        "shift\n"
        "exec \"$@\"\n",
        "#!/bin/sh\n"
        "shift 2\n"
        "exec \"$@\"\n",
    };
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        char path[BENCH_SERVICE_RECIPE_PATH_CAP];
        ok = bench_service_zen5_path(path, scripts, names[index]) &&
             bench_service_recipe_test_write(path, bodies[index], 0700);
    }
    char driver[BENCH_SERVICE_RECIPE_PATH_CAP], ninja[BENCH_SERVICE_RECIPE_PATH_CAP];
    char* self = realpath("/proc/self/exe", NULL);
    char const* python = access("/usr/bin/python3", X_OK) == 0 ? "/usr/bin/python3" :
                         access("/usr/local/bin/python3", X_OK) == 0 ? "/usr/local/bin/python3" : NULL;
    char const* path = getenv("PATH");
    String8 search = string_format(arena, S8("{S8}:{S8}"), string_from_pointer(scripts),
                                   string_from_pointer(path ? path : "/usr/bin:/bin"));
    ok = ok && self && python && bench_service_zen5_path(driver, scripts, "driver") &&
         bench_service_zen5_path(ninja, scripts, "ninja") &&
         setenv("PATH", (char const*)string_duplicate_arena(arena, search, true).pointer, 1) == 0;
    if (ok)
    {
        bench_service_recipe_driver_override = string_duplicate_arena(arena, string_from_pointer(driver), true);
        bench_service_zen5_ninja_override = string_duplicate_arena(arena, string_from_pointer(ninja), true);
        bench_service_zen5_python_override = string_duplicate_arena(arena, string_from_pointer(python), true);
        bench_service_zen5_self_override = string_duplicate_arena(arena, string_from_pointer(self), true);
        bench_service_zen5_direct = true;
        bench_service_zen5_test_gap_ns = 1000000u;
        bench_service_zen5_test_cpu_set = true;
        bench_service_zen5_test_cpu = 0;
    }
    free(self);
    return ok;
}

BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_test_run(Arena* arena, BenchServiceZen5TestFixture const* fixture,
                                                              char const* candidate)
{
    String8 arguments[] = {S8("1"), S8("2"), string_from_pointer(fixture->workspaces), S8(BENCH_SERVICE_ZEN5_TEST_REVISION),
                           string_from_pointer(candidate), string_from_pointer(fixture->result)};
    ProcessResult result = bench_service_zen5_recipe_add(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments));
    return result;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_exists(char const* parent, char const* relative)
{
    char path[BENCH_SERVICE_RECIPE_PATH_CAP];
    bool exists = bench_service_zen5_path(path, parent, relative) && access(path, F_OK) == 0;
    return exists;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_manifest(char const* result, char const* name, char const* needle)
{
    char path[BENCH_SERVICE_RECIPE_PATH_CAP];
    bool found = bench_service_zen5_path(path, result, name) && bench_service_recipe_test_contains(path, needle);
    return found;
}

/* Run one landed Python reader from the repository root; zero means valid. */
BUSTER_GLOBAL_LOCAL int bench_service_zen5_test_python(char const* root, char* const* argv)
{
    char log[BENCH_SERVICE_RECIPE_PATH_CAP];
    int descriptor = bench_service_zen5_path(log, root, "python.log") ?
                     open(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    BenchServiceZen5Child child = descriptor >= 0 ?
        bench_service_zen5_child(argv, NULL, descriptor, bench_service_zen5_now() + 300ull * 1000000000ull) :
        (BenchServiceZen5Child){.status = -1};
    if (descriptor >= 0) close(descriptor);
    return child.launched && !child.timed_out ? child.status : -1;
}

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_u64_after(Arena* arena, char const* path, char const* needle, u64* value)
{
    String8 text = {0};
    bool ok = bench_service_zen5_read(arena, path, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, &text);
    char const* found = ok ? strstr((char const*)text.pointer, needle) : NULL;
    IntegerParsingU64 parsed = {0};
    if (found)
    {
        String8 tail = string_from_pointer(found + strlen(needle));
        parsed = string8_parse_u64_decimal(tail);
    }
    ok = found && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length > 0;
    *value = ok ? parsed.value : 0;
    return ok;
}

/* Independent replay of the success result with the landed readers. */
BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_replay(Arena* arena, BenchServiceZen5TestFixture const* fixture)
{
    char python[BENCH_SERVICE_RECIPE_PATH_CAP], plan[BENCH_SERVICE_RECIPE_PATH_CAP], pmu[BENCH_SERVICE_RECIPE_PATH_CAP];
    char captures[BENCH_SERVICE_ZEN5_CAPTURES][BENCH_SERVICE_RECIPE_PATH_CAP];
    char digests[BENCH_SERVICE_ZEN5_CAPTURES][SHA256_HEX_CAPACITY], plan_digest[SHA256_HEX_CAPACITY];
    char trusted[BENCH_SERVICE_RECIPE_PATH_CAP], report[BENCH_SERVICE_RECIPE_PATH_CAP];
    bool ok = bench_service_zen5_setting(bench_service_zen5_python_override, BENCH_SERVICE_ZEN5_PYTHON, python) &&
              bench_service_zen5_path(plan, fixture->result, "zen5/plan.json") &&
              bench_service_zen5_path(pmu, fixture->result, "zen5/pmu/zen5-pmu-v1.json") &&
              bench_service_zen5_path(trusted, fixture->root, "trusted-captures.json") &&
              bench_service_zen5_path(report, fixture->root, "qualification-inputs.json") &&
              bench_service_zen5_file_digest(plan, BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, plan_digest, NULL, NULL);
    for (u32 capture = 0; ok && capture < BENCH_SERVICE_ZEN5_CAPTURES; capture += 1)
    {
        char relative[96];
        snprintf(relative, sizeof(relative), "zen5/captures/%s.json", bench_service_zen5_capture_names[capture]);
        ok = bench_service_zen5_path(captures[capture], fixture->result, relative) &&
             bench_service_zen5_file_digest(captures[capture], BENCH_SERVICE_RECIPE_BUNDLE_FILE_CAP, digests[capture],
                                            NULL, NULL);
    }
    char* aa[] = {python, "-B", "tools/zen5_aa_noise.py", "validate", captures[0], NULL};
    char* same[] = {python, "-B", "tools/zen5_build_control.py", "validate", captures[1], NULL};
    char* cross[] = {python, "-B", "tools/zen5_build_control.py", "validate", captures[2], NULL};
    char* host[] = {python, "-B", "tools/zen5_host_qualification.py", "validate", pmu, NULL};
    ok = ok && bench_service_zen5_test_python(fixture->root, aa) == 0 &&
         bench_service_zen5_test_python(fixture->root, same) == 0 &&
         bench_service_zen5_test_python(fixture->root, cross) == 0 &&
         bench_service_zen5_test_python(fixture->root, host) == 0;
    char trusted_digests[320] = {0};
    int trusted_length = ok ? snprintf(trusted_digests, sizeof(trusted_digests),
                                       "{\"cross-root\":\"%s\",\"immutable\":\"%s\",\"same-root-rebuild\":\"%s\"}\n",
                                       digests[2], digests[0], digests[1]) : -1;
    ok = ok && trusted_length > 0 && (u32)trusted_length < sizeof(trusted_digests) &&
         bench_service_zen5_write_new(trusted, string_from_pointer(trusted_digests), 0444);
    char* handoff[] = {python, "-B", "tools/zen5_calibration_handoff.py", "replay", plan, "--trusted-plan-sha256", plan_digest,
                       "--trusted-captures", trusted, "--immutable", captures[0], "--same-root-rebuild", captures[1],
                       "--cross-root", captures[2], "--output", report, NULL};
    ok = ok && bench_service_zen5_test_python(fixture->root, handoff) == 0 &&
         bench_service_recipe_test_contains(report, "\"status\":\"descriptive-complete\"") &&
         bench_service_recipe_test_contains(report, "\"ab_authorized\":false");
    /* The plan was durable before the runner verified it, and verified before
     * the first timed child of every capture. */
    u64 frozen = 0;
    char plan_manifest[BENCH_SERVICE_RECIPE_PATH_CAP];
    ok = ok && bench_service_zen5_path(plan_manifest, fixture->result, BENCH_SERVICE_ZEN5_NAME ".plan.manifest") &&
         bench_service_zen5_test_u64_after(arena, plan_manifest, "plan-frozen-monotonic-ns=", &frozen) && frozen > 0 &&
         bench_service_recipe_test_contains(plan_manifest, "timed-children-started=0");
    for (u32 capture = 0; ok && capture < BENCH_SERVICE_ZEN5_CAPTURES; capture += 1)
    {
        u64 verified = 0, started = 0;
        ok = bench_service_zen5_test_u64_after(arena, captures[capture], "\"plan_verified_monotonic_ns\":", &verified) &&
             bench_service_zen5_test_u64_after(arena, captures[capture], "\"started_monotonic_ns\":", &started) &&
             frozen <= verified && verified <= started;
    }
    return ok;
}

typedef enum BenchServiceZen5TestCase BenchServiceZen5TestCase;
enum BenchServiceZen5TestCase
{
    BENCH_SERVICE_ZEN5_TEST_SUCCESS,
    BENCH_SERVICE_ZEN5_TEST_PLAN_TAMPER,
    BENCH_SERVICE_ZEN5_TEST_PIN_MISMATCH,
    BENCH_SERVICE_ZEN5_TEST_MISSING_TREE,
    BENCH_SERVICE_ZEN5_TEST_TWO_SOURCES,
    BENCH_SERVICE_ZEN5_TEST_TIMING_RESERVE,
    BENCH_SERVICE_ZEN5_TEST_UNLISTED_TREE,
    BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_ORACLE,
    BENCH_SERVICE_ZEN5_TEST_FROZEN_AFTER_CAPTURES,
    BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_CAPTURES,
    BENCH_SERVICE_ZEN5_TEST_STAGE_OVERRUN,
    BENCH_SERVICE_ZEN5_TEST_CASE_COUNT,
};

BUSTER_GLOBAL_LOCAL bool bench_service_zen5_test_case(Arena* arena, BenchServiceZen5TestCase test_case, char const* keep)
{
    BenchServiceZen5TestFixture fixture;
    char const* tampered = test_case == BENCH_SERVICE_ZEN5_TEST_PIN_MISMATCH ? "tools/zen5_qualification_replay.py" : NULL;
    bool ok = bench_service_zen5_test_fixture(arena, &fixture, keep, test_case != BENCH_SERVICE_ZEN5_TEST_MISSING_TREE,
                                              test_case != BENCH_SERVICE_ZEN5_TEST_UNLISTED_TREE, tampered);
    bench_service_zen5_test_tamper_plan = test_case == BENCH_SERVICE_ZEN5_TEST_PLAN_TAMPER;
    bench_service_zen5_test_reserve_seconds = test_case == BENCH_SERVICE_ZEN5_TEST_TIMING_RESERVE ? 100000u : 0;
    bench_service_zen5_test_tamper_frozen = test_case == BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_ORACLE ? 1u :
                                            test_case == BENCH_SERVICE_ZEN5_TEST_FROZEN_AFTER_CAPTURES ? 2u :
                                            test_case == BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_CAPTURES ? 3u : 0u;
    /* Overrun: a 4-second budget and a first build that sleeps past it. */
    bool overrun = test_case == BENCH_SERVICE_ZEN5_TEST_STAGE_OVERRUN;
    bench_service_zen5_test_budget_seconds = overrun ? 4u : 0u;
    if (overrun) ok = ok && setenv("BENCH_SERVICE_ZEN5_TEST_SLOW", "1", 1) == 0;
    char const* candidate = test_case == BENCH_SERVICE_ZEN5_TEST_TWO_SOURCES ? "3333333333333333333333333333333333333333" :
                            BENCH_SERVICE_ZEN5_TEST_REVISION;
    ProcessResult result = ok ? bench_service_zen5_test_run(arena, &fixture, candidate) : PROCESS_RESULT_FAILED;
    char const* manifest = BENCH_SERVICE_ZEN5_NAME ".manifest";
    switch (test_case)
    {
    case BENCH_SERVICE_ZEN5_TEST_SUCCESS:
        ok = ok && result == PROCESS_RESULT_SUCCESS &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=succeeded\nstage=complete\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "ab-authorized=false\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "oracle-consistent=true\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "pmu-status=invalid\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "immutable-capture-status=complete\n"
                                              "same-root-rebuild-capture-status=complete\ncross-root-capture-status=complete\n") &&
             bench_service_zen5_test_exists(fixture.result, "zen5/pmu/runtime.identity") &&
             bench_service_zen5_test_exists(fixture.attempt, "zen5/pmu-tool/zen5_host_qualification.py") &&
             bench_service_zen5_test_replay(arena, &fixture);
        break;
    case BENCH_SERVICE_ZEN5_TEST_PLAN_TAMPER:
        /* Refused before any timed child: no capture and no child output. */
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=captures\n") &&
             bench_service_zen5_test_exists(fixture.result, BENCH_SERVICE_ZEN5_NAME ".plan.manifest") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/captures/immutable.json") &&
             !bench_service_zen5_test_exists(fixture.attempt, "zen5/staging/captures/immutable.json") &&
             !bench_service_zen5_test_exists(fixture.attempt, "zen5/staging/captures/immutable/output.o") &&
             bench_service_zen5_test_exists(fixture.result, BENCH_SERVICE_ZEN5_NAME ".bundle");
        break;
    case BENCH_SERVICE_ZEN5_TEST_PIN_MISMATCH:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=source\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "does not match the profile") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/logs/zen5-immutable-generate.log");
        break;
    case BENCH_SERVICE_ZEN5_TEST_MISSING_TREE:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=source\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "lacks a valid .bq-source-tree");
        break;
    case BENCH_SERVICE_ZEN5_TEST_TWO_SOURCES:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             !bench_service_zen5_test_exists(fixture.result, BENCH_SERVICE_ZEN5_NAME ".prepare.manifest");
        break;
    case BENCH_SERVICE_ZEN5_TEST_TIMING_RESERVE:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=plan\n") &&
             bench_service_zen5_test_exists(fixture.result, "zen5/builds/cross-root-B.json") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/plan.json") &&
             !bench_service_zen5_test_exists(fixture.attempt, "zen5/staging/captures");
        break;
    case BENCH_SERVICE_ZEN5_TEST_UNLISTED_TREE:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=source\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "does not list .bq-source-tree") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/logs/zen5-immutable-generate.log");
        break;
    case BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_ORACLE:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=oracle\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "frozen binaries changed before the oracle") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/oracle.record") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/logs/zen5-oracle.log");
        break;
    case BENCH_SERVICE_ZEN5_TEST_FROZEN_AFTER_CAPTURES:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=captures\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "frozen binaries changed after capture immutable") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "immutable-capture-status=missing\n") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/captures/immutable.json");
        break;
    case BENCH_SERVICE_ZEN5_TEST_FROZEN_BEFORE_CAPTURES:
        /* The runner compares against the spec's digests before any child. */
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=captures\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "captures were refused or failed") &&
             !bench_service_zen5_test_exists(fixture.attempt, "zen5/staging/captures/immutable.json") &&
             !bench_service_zen5_test_exists(fixture.attempt, "zen5/staging/captures/immutable/output.o");
        break;
    default:
        ok = ok && result == PROCESS_RESULT_FAILED &&
             bench_service_zen5_test_manifest(fixture.result, manifest, "status=failed\nstage=builds\n") &&
             bench_service_zen5_test_manifest(fixture.result, manifest,
                                              "stage zen5-immutable-build exceeded the recipe budget") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/builds/immutable.json") &&
             !bench_service_zen5_test_exists(fixture.result, "zen5/logs/zen5-same-root-A-generate.log") &&
             !bench_service_zen5_test_exists(fixture.result, BENCH_SERVICE_ZEN5_NAME ".plan.manifest");
        break;
    }
    bench_service_zen5_test_tamper_plan = false;
    bench_service_zen5_test_reserve_seconds = 0;
    bench_service_zen5_test_tamper_frozen = 0;
    bench_service_zen5_test_budget_seconds = 0;
    if (overrun) unsetenv("BENCH_SERVICE_ZEN5_TEST_SLOW");
    if (!ok) string_print(S8("BENCH_SERVICE_ZEN5_RECIPE_SELF_TEST_FAILURE case={u32} root={S8}\n"), (u32)test_case,
                          string_from_pointer(fixture.root));
    if (fixture.root[0]) bench_service_zen5_test_cleanup(arena, &fixture, keep != NULL);
    return ok;
}

/* Host eligibility, exactly: a Linux x86-64 build of this driver, a readable
 * /proc/cpuinfo whose first page has the x86 kernel's "cpu family" field, and
 * an executable python3 at /usr/bin or /usr/local/bin (the landed readers and
 * the real PMU tool run under it). Returns NULL when eligible, otherwise the
 * reason printed in the skip line. tests.c's zen5 bridge applies the same
 * definition, so an eligible host can never report a skip as green. */
BUSTER_GLOBAL_LOCAL char const* bench_service_zen5_test_ineligible_reason(void)
{
    char const* reason = NULL;
#if defined(__x86_64__)
    /* procfs reports a zero size, so read the first page directly. */
    char cpuinfo[4096] = {0};
    int descriptor = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
    ssize_t count = descriptor >= 0 ? read(descriptor, cpuinfo, sizeof(cpuinfo) - 1) : -1;
    if (descriptor >= 0) close(descriptor);
    if (count <= 0 || strstr(cpuinfo, "cpu family") == NULL) reason = "no-x86-proc-cpuinfo";
    else if (access("/usr/bin/python3", X_OK) != 0 && access("/usr/local/bin/python3", X_OK) != 0) reason = "no-python3";
#else
    reason = "not-linux-x86-64";
#endif
    return reason;
}

typedef enum BenchServiceZen5TestVerdict BenchServiceZen5TestVerdict;
enum BenchServiceZen5TestVerdict
{
    BENCH_SERVICE_ZEN5_TEST_PASS,
    BENCH_SERVICE_ZEN5_TEST_FAIL,
    BENCH_SERVICE_ZEN5_TEST_SKIP,
};

/* An eligible host passes only when every case ran and passed; zero cases on
 * an eligible host is a failure, never a skip. Only an ineligible host skips. */
BUSTER_GLOBAL_LOCAL BenchServiceZen5TestVerdict bench_service_zen5_test_verdict(bool eligible, bool ok, u32 cases,
                                                                                u32 expected)
{
    BenchServiceZen5TestVerdict verdict = !eligible ? BENCH_SERVICE_ZEN5_TEST_SKIP :
                                          ok && expected > 0 && cases == expected ? BENCH_SERVICE_ZEN5_TEST_PASS :
                                          BENCH_SERVICE_ZEN5_TEST_FAIL;
    return verdict;
}

/* Exit status: 0 pass, 1 fail, and PROCESS_RESULT_NOT_EXISTENT (4) for a
 * recorded skip on an ineligible host, which is neither success nor failure. */
BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_recipe_self_test(Arena* arena, SliceString8 arguments)
{
    char scripts[BENCH_SERVICE_RECIPE_PATH_CAP] = {0}, keep[BENCH_SERVICE_RECIPE_PATH_CAP] = {0};
    bool keep_mode = arguments.length == 1;
    bool ok = arguments.length <= 1 && (!keep_mode || (bench_service_recipe_path(arguments.pointer[0]) &&
                                                       snprintf(keep, sizeof(keep), "%.*s", (int)arguments.pointer[0].length,
                                                                arguments.pointer[0].pointer) > 0));
    /* The verdict rule itself: an eligible host with zero cases fails. */
    ok = ok && bench_service_zen5_test_verdict(true, true, 0, 11) == BENCH_SERVICE_ZEN5_TEST_FAIL &&
         bench_service_zen5_test_verdict(true, false, 11, 11) == BENCH_SERVICE_ZEN5_TEST_FAIL &&
         bench_service_zen5_test_verdict(true, true, 10, 11) == BENCH_SERVICE_ZEN5_TEST_FAIL &&
         bench_service_zen5_test_verdict(true, true, 11, 11) == BENCH_SERVICE_ZEN5_TEST_PASS &&
         bench_service_zen5_test_verdict(false, true, 0, 11) == BENCH_SERVICE_ZEN5_TEST_SKIP;
    char const* ineligible = bench_service_zen5_test_ineligible_reason();
    u32 expected = keep_mode ? 1u : (u32)BENCH_SERVICE_ZEN5_TEST_CASE_COUNT;
    u32 cases = 0;
    if (ok && !ineligible)
    {
        ok = bench_service_zen5_test_scripts(arena, scripts);
        for (u32 test_case = 0; ok && test_case < expected; test_case += 1)
        {
            ok = bench_service_zen5_test_case(arena, (BenchServiceZen5TestCase)test_case, keep_mode ? keep : NULL);
            cases += ok ? 1u : 0u;
        }
    }
    bench_service_recipe_driver_override = (String8){0};
    bench_service_zen5_ninja_override = (String8){0};
    bench_service_zen5_python_override = (String8){0};
    bench_service_zen5_self_override = (String8){0};
    bench_service_zen5_direct = false;
    bench_service_zen5_test_gap_ns = 0;
    bench_service_zen5_test_cpu_set = false;
    if (scripts[0]) remove_path_recursive(arena, string_from_pointer(scripts));
    /* A malformed invocation or a broken verdict rule fails on any host. */
    BenchServiceZen5TestVerdict verdict = !ok && ineligible ? BENCH_SERVICE_ZEN5_TEST_FAIL :
                                          bench_service_zen5_test_verdict(!ineligible, ok, cases, expected);
    if (verdict == BENCH_SERVICE_ZEN5_TEST_SKIP)
        string_print(S8("BENCH_SERVICE_ZEN5_RECIPE_SELF_TEST cases=0 result=skipped-ineligible-host reason={S8}\n"),
                     string_from_pointer(ineligible));
    else
        string_print(S8("BENCH_SERVICE_ZEN5_RECIPE_SELF_TEST cases={u32} result={S8}\n"), cases,
                     string_from_pointer(verdict == BENCH_SERVICE_ZEN5_TEST_PASS ? "pass" : "fail"));
    ProcessResult result = verdict == BENCH_SERVICE_ZEN5_TEST_PASS ? PROCESS_RESULT_SUCCESS :
                           verdict == BENCH_SERVICE_ZEN5_TEST_SKIP ? PROCESS_RESULT_NOT_EXISTENT : PROCESS_RESULT_FAILED;
    return result;
}
#else
BUSTER_GLOBAL_LOCAL ProcessResult bench_service_zen5_recipe_self_test(Arena* arena, SliceString8 arguments)
{
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("BENCH_SERVICE_ZEN5_RECIPE_SELF_TEST cases=0 result=skipped-ineligible-host reason=not-linux-x86-64\n"));
    return PROCESS_RESULT_NOT_EXISTENT;
}
#endif
