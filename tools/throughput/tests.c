/* Native regression tests for the harness itself. Synthetic timings below are
 * fixtures, never compiler measurements. Child modes exercise the OS boundary.
 * Build through ./build.sh bench_throughput self-test; shared.c owns linkage.
 */
#define TP_WORKLOAD_TEST_ALLOCATIONS 1
#define main throughput_cli_main
#include "throughput.c"
#undef main
#undef TP_WORKLOAD_TEST_ALLOCATIONS
#include "retirement_stats.h"

static unsigned test_assertions, test_failures;
#define CHECK(c) do { ++test_assertions; if (!(c)) { ++test_failures; fprintf(stderr, "TEST failure %d: %s\n", __LINE__, #c); } } while (0)

static int test_text(char const* root, char const* name, char const* text)
{
    char path[TP_PATH_CAP];
    int ok = tp_path(path, root, name);
    FILE* file = ok ? fopen(path, "wb") : NULL;
    ok = file && fputs(text, file) >= 0;
    if (file && fclose(file) != 0) ok = 0;
    return ok;
}

static int test_bundle(char const* root, unsigned scenario)
{
    int ok = tp_mkdirs(root) && test_text(root, "metadata.json", "{\"synthetic_test_fixture\":true}\n") &&
             test_text(root, "jobs.tsv", "0\tfixture/fast\n") && test_text(root, "commands.jsonl", "") &&
             test_text(root, "capabilities.jsonl", "") && test_text(root, "telemetry.csv", TP_RAW_HEADER);
    char path[TP_PATH_CAP];
    FILE* file = ok && tp_path(path, root, "samples.csv") ? fopen(path, "wb") : NULL;
    ok = ok && file != NULL;
    if (file)
    {
        fputs(TP_RAW_HEADER, file);
        for (unsigned round = 0; round < TP_ROUNDS && ok; ++round)
        {
            for (unsigned pair = 0; pair < 20 && ok; ++pair)
            {
                for (unsigned variant = 0; variant < 2 && ok; ++variant)
                {
                    TpRow row = {0};
                    row.present = 1;
                    row.process.wall_seconds = 1.0;
                    row.process.user_seconds = 0.8;
                    row.process.system_seconds = 0.1;
                    row.process.peak_rss_bytes = 64.0 * 1048576.0;
                    if (scenario == 10 || scenario == 11)
                    {
                        row.process.diagnostics_available = (1u << TP_DIAGNOSTICS) - 1u;
                        for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i)
                            row.process.diagnostics[i] = scenario == 10 ? (variant ? UINT64_C(1000000000000) : 1) : 0;
                        if (scenario == 11 && variant && pair == 3)
                            row.process.diagnostics_available &= ~(1u << TP_MINOR_FAULTS);
                    }
                    row.arena_calls = row.arena_bytes = NAN;
                    for (unsigned i = 0; i < TP_COUNTERS; ++i) row.process.counters[i] = NAN;
                    row.output_bytes = 4; row.source_bytes = 16; row.source_lines = 2; row.source_functions = 1;
                    memset(row.output_hash, '0', 64); row.output_hash[64] = 0;
                    if (variant && (scenario == 1 || (scenario == 2 && round == 0))) row.process.wall_seconds = 1.3;
                    if (scenario == 3) row.process.wall_seconds = variant ? 0.0013 : 0.001;
                    if (variant && scenario == 4) row.process.wall_seconds = pair & 1 ? 1.5 : 0.8;
                    if (variant && scenario == 5) row.process.peak_rss_bytes *= 1.5;
                    if (variant && scenario == 7 && pair == 3) row.output_hash[0] = '1';
                    if (variant && scenario == 8) row.source_bytes += 1;
                    unsigned order = variant ^ (pair & 1);
                    if (scenario == 6) order = 0;
                    if (!(scenario == 9 && round == 1 && pair == 19 && variant == 1))
                        ok = tp_sample_csv(file, round, pair, order, variant, 0, &row);
                }
            }
        }
        if (fclose(file) != 0) ok = 0;
    }
    if (ok) ok = tp_completion(root, 1, 20, 1, 1);
    return ok;
}

static void test_delay(unsigned milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) { }
#endif
}

static int test_admission_transcript(void)
{
    static char const text[] = "admitted\n";
    int result = 3;
#ifdef _WIN32
    DWORD written = 0;
    if (WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)(sizeof(text) - 1), &written, NULL) &&
        written == (DWORD)(sizeof(text) - 1)) result = 0;
#else
    if (fwrite(text, 1, sizeof(text) - 1, stdout) == sizeof(text) - 1 && fflush(stdout) == 0) result = 0;
#endif
    return result;
}

static int test_child(int argc, char** argv)
{
    int result = 0;
    if (argc < 3) result = 2;
    else if (!strcmp(argv[2], "fail")) result = 7;
    else if (!strcmp(argv[2], "throughput")) result = throughput_cli_main(argc - 2, argv + 2);
    else if (!strcmp(argv[2], "throughput-admission-oom"))
    {
        tp_workload_test_fail_state_allocation = 1;
        result = throughput_cli_main(argc - 2, argv + 2);
    }
#ifndef _WIN32
    else if (!strcmp(argv[2], "throughput-low-stack"))
    {
        struct rlimit limit = {(rlim_t)1024 * 1024, (rlim_t)1024 * 1024};
        result = setrlimit(RLIMIT_STACK, &limit) == 0 ? throughput_cli_main(argc - 2, argv + 2) : 2;
    }
#endif
#ifdef __linux__
    else if (argc == 4 && !strcmp(argv[2], "host-lock-probe"))
    {
        TpHostLock lock = {-1};
        int error = tp_host_lock_acquire(argv[3], &lock);
        result = error == EAGAIN || error == EWOULDBLOCK ? 0 : 1;
        tp_host_lock_release(&lock);
    }
#endif
    else if (!strcmp(argv[2], "sleep")) test_delay(5000);
    else if (!strcmp(argv[2], "admission-transcript")) result = test_admission_transcript();
    else if (argc == 4 && !strcmp(argv[2], "descendant-marker"))
    {
        test_delay(2000);
        result = file_write(string_from_pointer(argv[3]), (ByteSlice){(u8*)"escaped", 7}) ? 0 : 3;
    }
    else if (argc == 4 && !strcmp(argv[2], "descendant"))
    {
        String8 child_args[] = {string_from_pointer(argv[0]), S8("child"), S8("descendant-marker"), string_from_pointer(argv[3])};
        ProcessSpawnResult child = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_args), (SliceString8){0}, (SliceString8){0}, (ProcessSpawnOptions){0});
        result = child.handle ? 0 : 3;
        if (child.handle)
        {
            test_delay(5000);
            TemporalArena temp = scratch_begin(0, 0);
            os_process_wait_deadline(temp.arena, child, 1000000);
            scratch_end(temp);
        }
    }
    else if (!strcmp(argv[2], "memory"))
    {
        size_t bytes = (size_t)96 * 1024 * 1024;
        volatile unsigned char* memory = (volatile unsigned char*)malloc(bytes);
        if (!memory) result = 3;
        else
        {
            for (size_t i = 0; i < bytes; i += 4096) memory[i] = (unsigned char)(i >> 12);
#ifdef __APPLE__
            /* Revisit every touched page before wait4 reaps the child. The
             * one-pass, 10ms case intermittently missed the 80 MiB macOS CI floor. */
            for (unsigned pass = 0; pass < 8; ++pass)
            {
                for (size_t i = 0; i < bytes; i += 4096)
                    memory[i] ^= (unsigned char)(pass + 1);
            }
            test_delay(100);
#else
            test_delay(10);
#endif
            free((void*)memory);
        }
    }
    else if (!strcmp(argv[2], "compare") && argc == 4) result = tp_compare(argv[3]);
    else if (!strcmp(argv[2], "echo"))
    {
        for (int i = 3; i < argc; ++i) printf("%s\n", argv[i]);
    }
#ifndef _WIN32
    else if (!strcmp(argv[2], "summary-write-failure") && argc == 4)
    {
        /* Restrict only this child so buffered report writes fail at close. */
        struct rlimit limit;
        int ok = getrlimit(RLIMIT_FSIZE, &limit) == 0;
        if (ok)
        {
            limit.rlim_cur = 1;
            ok = signal(SIGXFSZ, SIG_IGN) != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0;
        }
        result = ok && tp_compare(argv[3]) == 2 ? 0 : 1;
    }
#endif
    else result = 2;
    return result;
}

static int test_compiler_child(int argc, char** argv)
{
    char const* source = NULL;
    char const* output = NULL;
    char const* metrics = NULL;
    int compile_only = 0, compiler_fail = 0, compiler_missing = 0, mutate_prior = 0, result = 0;
    for (int i = 2; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strncmp(argv[i], "-fsource-metrics=", 17)) metrics = argv[i] + 17;
        else if (!strcmp(argv[i], "-c")) compile_only = 1;
        else if (!strcmp(argv[i], "-DTP_TEST_COMPILER_FAIL")) compiler_fail = 1;
        else if (!strcmp(argv[i], "-DTP_TEST_COMPILER_MISSING")) compiler_missing = 1;
        else if (!strcmp(argv[i], "-DTP_TEST_MUTATE_PRIOR")) mutate_prior = 1;
        else if (strstr(argv[i], ".c"))
        {
            source = argv[i];
            compiler_fail |= strstr(argv[i], "compiler-fail") != NULL;
            compiler_missing |= strstr(argv[i], "compiler-missing") != NULL;
        }
    }
    if (!source || !output || !metrics) result = 9;
    else if (compiler_fail) result = 7;
    else if (!compiler_missing)
    {
        FILE* report = fopen(metrics, "wb");
        int ok = report && fputs("lexed.translated_bytes=16\nlexed.translated_lines=2\n", report) >= 0;
        if (report && fclose(report) != 0) ok = 0;
        if (compile_only)
        {
            FILE* artifact = ok ? fopen(output, "wb") : NULL;
            ok = artifact && fputs("object", artifact) >= 0;
            if (artifact && fclose(artifact) != 0) ok = 0;
        }
        else
        {
            FILE* input = ok ? fopen(argv[0], "rb") : NULL;
            FILE* artifact = input ? fopen(output, "wb") : NULL;
            unsigned char buffer[16384];
            ok = input && artifact;
            while (ok)
            {
                size_t count = fread(buffer, 1, sizeof(buffer), input);
                if (count && fwrite(buffer, 1, count, artifact) != count) ok = 0;
                if (count < sizeof(buffer))
                {
                    if (ferror(input)) ok = 0;
                    break;
                }
            }
            if (input && fclose(input) != 0) ok = 0;
            if (artifact && fclose(artifact) != 0) ok = 0;
#ifndef _WIN32
            if (ok && chmod(output, 0700) != 0) ok = 0;
#endif
            if (ok && mutate_prior)
            {
                char prior[TP_PATH_CAP];
                size_t directory_length = (size_t)(strrchr(output, '/') - output);
                ok = directory_length + strlen("/object-000.o") < sizeof(prior);
                if (ok)
                {
                    memcpy(prior, output, directory_length);
                    strcpy(prior + directory_length, "/object-000.o");
                    FILE* changed = fopen(prior, "wb");
                    ok = changed && fputs("mutated", changed) >= 0;
                    if (changed && fclose(changed) != 0) ok = 0;
                }
            }
        }
        if (!ok) result = 8;
    }
    return result;
}

static int test_identity_manifest(char const* root, char const* name, char const* kind, char const* operation,
                                  char const* bindings, char const* closure_path, char const* closure_hash, uint64_t closure_bytes)
{
    char text[TP_PATH_CAP + 1024];
    int length = snprintf(text, sizeof(text),
        "schema=buster-throughput-identity-v1\nkind=%s\nidentity=test:%s\noperation=%s\n%sfile=%s\t%s\t%" PRIu64 "\n",
        kind, kind, operation, bindings, closure_path, closure_hash, closure_bytes);
    return length > 0 && (size_t)length < sizeof(text) && test_text(root, name, text);
}

static TpProcess test_admit_workload_mode(char const* executable, char const* mode, char const* descriptor, char const* source_root,
                                          char const* evidence, char const* output, char* manifests[6], char const* log)
{
    char* command[] = {(char*)executable, "child", (char*)mode, "admit-workload", (char*)descriptor,
        "--source-root", (char*)source_root, "--compiler", (char*)executable, "--evidence", (char*)evidence,
        "--evidence-outcome", "pass", "--output", (char*)output, "--qualification-id", "test:qualification",
        "--dependency-manifest", manifests[0], "--resource-manifest", manifests[1],
        "--sysroot-manifest", manifests[2], "--sdk-manifest", manifests[3],
        "--environment-manifest", manifests[4], "--runtime-manifest", manifests[5], NULL};
    return tp_process(command, NULL, log, 10, -1, 0);
}

static TpProcess test_admit_workload(char const* executable, char const* descriptor, char const* source_root,
                                     char const* evidence, char const* output, char* manifests[6], char const* log)
{
    return test_admit_workload_mode(executable, "throughput", descriptor, source_root, evidence, output, manifests, log);
}

static int test_admission_descriptor(char const* directory, char const* tree_hash,
                                     char const* source_hash, uint64_t source_bytes,
                                     char const* generated_hash, uint64_t generated_bytes,
                                     char const* compiler_control, char const* transcript_sha256)
{
    char text[16384];
    char object_control[128] = {0}, link_control[128] = {0};
    int controls_ok = !compiler_control[0] ||
        (snprintf(object_control, sizeof(object_control), "object_argv=%s\n", compiler_control) > 0 &&
         snprintf(link_control, sizeof(link_control), "compile_link_argv=%s\n", compiler_control) > 0);
    int length = snprintf(text, sizeof(text),
        "schema=%s\nname=admission-fixture\nfamily=fixture\nsource_identity=test:source\n"
        "dependency_identity=test:dependency\ngenerated_identity=test:generated\nresource_identity=test:resource\n"
        "sysroot_identity=test:sysroot\nsdk_identity=test:sdk\nenvironment_identity=test:environment\n"
        "runtime_identity=test:runtime\n"
        "target=x86_64-unknown-linux-gnu\nabi=sysv-amd64\ncpu=baseline\ncpu_features=baseline\n"
        "c_lowerings=local-backed-canonical,direct-ssa\npic_modes=off,on\nallocator_modes=none,mir-stack,fast,quality\n"
        "operations=source-to-object,source-to-linked-executable,runtime\nartifacts=object,executable,runtime-transcript\n"
        "oracle=test:oracle\noracle_success=status=pass\nhistorical_outcome=failed\n"
        "historical_evidence=test:historical\nadmission=fresh-required\nadmission_frontend=direct-ssa\n"
        "admission_pic=off\nadmission_allocator=fast\nruntime_transcript_sha256=%s\ncwd=.\n"
        "requested_translation_unit_bytes=%" PRIu64 "\ninput_tree_sha256=%s\n"
        "input=source\tsource.c\t%s\t%" PRIu64 "\ninput=generated\tgenerated.c\t%s\t%" PRIu64 "\n"
        "link_input=source.c\nlink_input=generated.c\n"
        "object_argv=$COMPILER\nobject_argv=cc\nobject_argv=-I$ROOT\nobject_argv=$TARGET\nobject_argv=$CPU\n"
        "object_argv=$FRONTEND\nobject_argv=$PIC\nobject_argv=-fregister-allocator=$MODE\n"
        "object_argv=-fsource-metrics=$METRICS\n%sobject_argv=-c\nobject_argv=-o\nobject_argv=$OUTPUT\nobject_argv=$SOURCE\n"
        "compile_link_argv=$COMPILER\ncompile_link_argv=cc\ncompile_link_argv=-I$ROOT\ncompile_link_argv=$TARGET\n"
        "compile_link_argv=$CPU\ncompile_link_argv=$FRONTEND\ncompile_link_argv=$PIC\n"
        "compile_link_argv=-fregister-allocator=$MODE\ncompile_link_argv=-fsource-metrics=$METRICS\n%s"
        "compile_link_argv=$LINK_SOURCES\ncompile_link_argv=-lm\ncompile_link_argv=-o\ncompile_link_argv=$OUTPUT\n"
        "runtime_argv=$EXECUTABLE\nruntime_argv=child\nruntime_argv=admission-transcript\n",
        TP_WORKLOAD_DESCRIPTOR_SCHEMA, transcript_sha256, source_bytes + generated_bytes, tree_hash,
        source_hash, source_bytes, generated_hash, generated_bytes, object_control, link_control);
    return controls_ok && length > 0 && (size_t)length < sizeof(text) && test_text(directory, "admission.workload", text);
}

static void test_processes(char const* executable, char const* root)
{
    // Foundation-only tools have no application entry-point clock prewarm.
    u64 clock_before = os_now_microseconds();
    test_delay(1);
    CHECK(os_now_microseconds() > clock_before);
    char log[TP_PATH_CAP];
    CHECK(tp_path(log, root, "child.log"));
    char* fail[] = {(char*)executable, "child", "fail", NULL};
    TpProcess failed = tp_process(fail, NULL, log, 2, -1, 0);
    CHECK(failed.exit_code == 7 && !failed.timed_out);
    char* missing[] = {"/definitely/missing/buster-throughput-compiler", NULL};
    TpProcess absent = tp_process(missing, NULL, log, 2, -1, 0);
    CHECK(absent.exit_code != 0 || absent.launch_error);
    char* sleep[] = {(char*)executable, "child", "sleep", NULL};
    TpProcess timeout = tp_process(sleep, NULL, log, 1, -1, 0);
    CHECK(timeout.timed_out && timeout.wall_seconds < 4.0);
    char marker[TP_PATH_CAP];
    CHECK(tp_path(marker, root, "descendant-marker"));
    CHECK(os_file_delete(string_from_pointer(marker)));
    char* descendant[] = {(char*)executable, "child", "descendant", marker, NULL};
    TpProcess tree = tp_process(descendant, NULL, log, 1, -1, 0);
    CHECK(tree.timed_out && tree.wall_seconds < 4.0);
    test_delay(1500);
    struct stat marker_status;
    CHECK(stat(marker, &marker_status) != 0 && errno == ENOENT);
    char* memory[] = {(char*)executable, "child", "memory", NULL};
#ifdef __linux__
    struct rusage before = {0}, after = {0};
    CHECK(getrusage(RUSAGE_CHILDREN, &before) == 0);
#endif
    TpProcess large = tp_process(memory, NULL, log, 3, -1, 0);
#ifdef __linux__
    CHECK(getrusage(RUSAGE_CHILDREN, &after) == 0);
    /* No other child runs between these snapshots. The independent cumulative
     * OS delta must match this invocation's wait4 result, not lifetime totals. */
    CHECK(large.diagnostics_available == (1u << TP_DIAGNOSTICS) - 1u);
    CHECK(large.diagnostics[TP_MINOR_FAULTS] == (uint64_t)(after.ru_minflt - before.ru_minflt));
    CHECK(large.diagnostics[TP_MAJOR_FAULTS] == (uint64_t)(after.ru_majflt - before.ru_majflt));
    CHECK(large.diagnostics[TP_VOLUNTARY_SWITCHES] == (uint64_t)(after.ru_nvcsw - before.ru_nvcsw));
    CHECK(large.diagnostics[TP_INVOLUNTARY_SWITCHES] == (uint64_t)(after.ru_nivcsw - before.ru_nivcsw));
    CHECK(large.diagnostics[TP_MINOR_FAULTS] > 0 && large.diagnostics[TP_VOLUNTARY_SWITCHES] > 0);
    printf("PROCESS_DIAGNOSTICS minor=%" PRIu64 " major=%" PRIu64 " voluntary=%" PRIu64 " involuntary=%" PRIu64 "\n",
           large.diagnostics[TP_MINOR_FAULTS], large.diagnostics[TP_MAJOR_FAULTS],
           large.diagnostics[TP_VOLUNTARY_SWITCHES], large.diagnostics[TP_INVOLUNTARY_SWITCHES]);
#else
    CHECK(large.diagnostics_available == 0 && timeout.diagnostics_available == 0 && failed.diagnostics_available == 0);
#endif
    TpProcess rejected = tp_process(fail, NULL, root, 2, -1, 0);
    CHECK(rejected.launch_error && rejected.diagnostics_available == 0);
    TpProcess small = tp_process(fail, NULL, log, 2, -1, 0);
    printf("PROCESS_RSS large_bytes=%.0f small_bytes=%.0f\n", large.peak_rss_bytes, small.peak_rss_bytes);
    CHECK(large.exit_code == 0 && large.peak_rss_bytes >= 80.0 * 1048576.0);
    CHECK(small.exit_code == 7 && small.peak_rss_bytes < large.peak_rss_bytes * 0.8);
    CHECK(large.user_seconds >= 0 && large.system_seconds >= 0 && large.wall_seconds > 0);
    char* echo[] = {(char*)executable, "child", "echo", "space in argument", "quote\"inside", "tail\\", "", NULL};
    TpProcess quoted = tp_process(echo, NULL, log, 2, -1, 1);
    CHECK(quoted.exit_code == 0);
    FILE* file = fopen(log, "rb");
    char text[256] = {0};
    CHECK(file != NULL);
    if (file)
    {
        size_t count = fread(text, 1, sizeof(text) - 1, file);
        CHECK(count < sizeof(text) - 1 && !ferror(file));
        CHECK(fclose(file) == 0);
    }
#ifdef _WIN32
    CHECK(!strcmp(text, "space in argument\r\nquote\"inside\r\ntail\\\r\n\r\n"));
#else
    CHECK(!strcmp(text, "space in argument\nquote\"inside\ntail\\\n\n"));
#endif
    for (unsigned i = 0; i < TP_COUNTERS; ++i)
        CHECK(isfinite(quoted.counters[i]) ? quoted.running_fraction[i] >= 0.90 : quoted.counter_errors[i] != 0);
}

/* Exact integer serialization is independent of floating-point summary
 * medians. Zero, UINT64_MAX and unavailable must survive a complete replay. */
static void test_process_fields(char const* root)
{
    char path[TP_PATH_CAP];
    int path_ok = tp_path(path, root, "process-fields.csv");
    CHECK(path_ok);
    TpRow row = {0};
    row.process.wall_seconds = 1.0;
    row.process.user_seconds = 0.8;
    row.process.system_seconds = 0.1;
    row.process.peak_rss_bytes = 67108864.0;
    row.process.diagnostics_available = (1u << TP_MINOR_FAULTS) | (1u << TP_MAJOR_FAULTS) | (1u << TP_VOLUNTARY_SWITCHES);
    row.process.diagnostics[TP_MAJOR_FAULTS] = UINT64_MAX;
    row.process.diagnostics[TP_VOLUNTARY_SWITCHES] = 17;
    for (unsigned i = 0; i < TP_COUNTERS; ++i) row.process.counters[i] = NAN;
    row.arena_calls = row.arena_bytes = NAN;
    row.output_bytes = 4; row.source_bytes = 16; row.source_lines = 2; row.source_functions = 1;
    memset(row.output_hash, '0', 64); row.output_hash[64] = 0;
    FILE* file = path_ok ? fopen(path, "wb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        fputs(TP_RAW_HEADER, file);
        for (unsigned round = 0; round < TP_ROUNDS; ++round)
            for (unsigned variant = 0; variant < 2; ++variant)
                CHECK(tp_sample_csv(file, round, 0, variant, variant, 0, &row));
        CHECK(fclose(file) == 0);
        TpRow loaded[TP_ROUNDS * 2] = {0};
        CHECK(tp_load_samples(root, "process-fields.csv", 1, 1, 1, loaded));
        for (unsigned i = 0; i < TP_ROUNDS * 2; ++i)
        {
            CHECK(loaded[i].process.diagnostics_available == row.process.diagnostics_available);
            CHECK(loaded[i].process.diagnostics[TP_MINOR_FAULTS] == 0);
            CHECK(loaded[i].process.diagnostics[TP_MAJOR_FAULTS] == UINT64_MAX);
            CHECK(loaded[i].process.diagnostics[TP_VOLUNTARY_SWITCHES] == 17);
            CHECK(isnan(tp_metric(&loaded[i], TP_STANDARD_METRICS + TP_INVOLUNTARY_SWITCHES)));
        }
    }
    static char const* const tokens[] = {"NA", "0", "18446744073709551615", "-1", "1.5", "nan", "inf", "", "18446744073709551616", "1e3", "0x1"};
    for (unsigned token = 0; token < sizeof(tokens) / sizeof(tokens[0]); ++token)
    {
        file = path_ok ? fopen(path, "wb") : NULL;
        CHECK(file != NULL);
        if (file)
        {
            fputs(TP_RAW_HEADER, file);
            for (unsigned round = 0; round < TP_ROUNDS; ++round)
            {
                for (unsigned variant = 0; variant < 2; ++variant)
                    fprintf(file, "%u,0,%u,%u,0,1,0.8,0.1,67108864,NA,NA,NA,NA,NA,NA,NA,NA,4,16,2,1,%s,%s,0,0,0\n",
                            round, variant, variant, row.output_hash, tokens[token]);
            }
            CHECK(fclose(file) == 0);
            TpRow loaded[TP_ROUNDS * 2] = {0};
            CHECK(tp_load_samples(root, "process-fields.csv", 1, 1, 1, loaded) == (token < 3));
        }
    }
}

static void test_diagnostic_probes(char const* root)
{
    char directory[TP_PATH_CAP], path[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "diagnostic-probes") && test_bundle(directory, 0) &&
                   tp_path(path, directory, "telemetry.csv");
    CHECK(paths_ok);
    FILE* file = paths_ok ? fopen(path, "wb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        fputs(TP_RAW_HEADER, file);
        for (unsigned kind = 0; kind < 2; ++kind)
        {
            for (unsigned repeat = 0; repeat < 3; ++repeat)
            {
                for (unsigned variant = 0; variant < 2; ++variant)
                {
                    TpRow row = {0};
                    row.process.wall_seconds = 1.0;
                    row.process.peak_rss_bytes = 67108864.0;
                    for (unsigned i = 0; i < TP_COUNTERS; ++i) row.process.counters[i] = NAN;
                    row.arena_calls = kind ? 3.0 : NAN;
                    row.arena_bytes = kind ? 15.0 : NAN;
                    row.output_bytes = 4; row.source_bytes = 16; row.source_lines = 2; row.source_functions = 1;
                    memset(row.output_hash, '0', 64); row.output_hash[64] = 0;
                    if (!kind)
                    {
                        row.process.diagnostics_available = (1u << TP_DIAGNOSTICS) - 1u;
                        for (unsigned i = 0; i < TP_DIAGNOSTICS; ++i) row.process.diagnostics[i] = repeat + variant;
                        if (variant && repeat == 2) row.process.diagnostics_available &= ~(1u << TP_MINOR_FAULTS);
                    }
                    CHECK(tp_sample_csv(file, kind, repeat, variant, variant, 0, &row));
                }
            }
        }
        CHECK(fclose(file) == 0);
        CHECK(tp_completion(directory, 1, 20, 1, 1));
        CHECK(tp_compare(directory) == 0);
        paths_ok = tp_path(path, directory, "summary.json");
        CHECK(paths_ok);
        file = paths_ok ? fopen(path, "rb") : NULL;
        CHECK(file != NULL);
        if (file)
        {
            char text[16384] = {0};
            size_t size = fread(text, 1, sizeof(text) - 1, file);
            CHECK(size < sizeof(text) - 1 && !ferror(file));
            CHECK(strstr(text, "\"minor_faults\":[null,null]") != NULL);
            CHECK(strstr(text, "\"available_samples\":2,\"median\":null") != NULL);
            CHECK(strstr(text, "\"available_samples\":0,\"median\":null") != NULL);
            CHECK(fclose(file) == 0);
        }
    }
}

static void test_legacy_schema(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "legacy-schema") && tp_mkdirs(directory) &&
                   test_text(directory, "complete.txt", "schema=1 jobs=1 pairs=20 rounds=2 guard=1\n") &&
                   tp_path(log, directory, "rejection.log");
    CHECK(paths_ok);
    if (paths_ok)
    {
        char* args[] = {(char*)executable, "child", "compare", directory, NULL};
        TpProcess result = tp_process(args, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2 && !result.launch_error && !result.timed_out);
    }
    FILE* file = paths_ok ? fopen(log, "rb") : NULL;
    CHECK(file != NULL);
    if (file)
    {
        char text[2048] = {0};
        size_t count = fread(text, 1, sizeof(text) - 1, file);
        CHECK(count < sizeof(text) - 1 && !ferror(file) && strstr(text, "unsupported result schema 1 (expected 2)") != NULL);
        CHECK(fclose(file) == 0);
    }
}

static void test_compile_options(void)
{
    char* options[] = {"throughput", "generate", "--flag", "-O3", "--flag", "-O0", NULL};
    TpConfig config;
    CHECK(tp_options(6, options, &config));
    CHECK(!config.assembly);
    char* artifact_options[] = {"throughput", "generate", "--flag", "-O3", "--flag", "-O0", "--artifact", "object", NULL};
    for (unsigned assembly = 0; assembly < 2; ++assembly)
    {
        artifact_options[7] = assembly ? "assembly" : "object";
        CHECK(tp_options(8, artifact_options, &config));
        CHECK(config.assembly == (int)assembly);
        config.self_host_generated = "generated";
        for (unsigned stage = 0; stage < 4; ++stage)
        {
            for (unsigned mode = 0; mode < 4; ++mode)
            {
                TpJob job = {0};
                job.mode = mode;
                job.stage = stage;
                job.assembly = config.assembly;
                strcpy(job.workload.path, "input.c");
                TpArguments args;
                tp_compile_arguments(&config, &job, "ide", "output", "metrics", &args);
                unsigned selected = 0, last_optimization = 0, selections = 0;
                unsigned objects = 0, assemblies = 0, preprocesses = 0;
                for (unsigned i = 0; i < args.count; ++i)
                {
                    if (!strncmp(args.values[i], "-O", 2)) last_optimization = i;
                    if (!strncmp(args.values[i], "-fregister-allocator=", 21)) { selected = i; ++selections; }
                    objects += !strcmp(args.values[i], "-c");
                    assemblies += !strcmp(args.values[i], "-S");
                    preprocesses += !strcmp(args.values[i], "-E");
                }
                CHECK(selections == 1 && selected > last_optimization &&
                      !strcmp(args.values[selected] + 21, tp_modes[mode]) && args.values[args.count] == NULL);
                CHECK(objects == (unsigned)(!stage && !assembly));
                CHECK(assemblies == (unsigned)(!stage && assembly));
                CHECK(!preprocesses);
                CHECK(!strcmp(tp_artifact_extension(&job), stage ? ".exe" : assembly ? ".s" : ".o"));
                CHECK(!strcmp(args.values[0], "ide") && !strcmp(args.values[1], "cc"));
                CHECK(!strcmp(args.values[2], stage ? "-g" : "-g0"));
                CHECK(args.count >= 3);
                if (args.count >= 3)
                {
                    CHECK(!strcmp(args.values[args.count - 3], "input.c") &&
                          !strcmp(args.values[args.count - 2], "-o") &&
                          !strcmp(args.values[args.count - 1], "output"));
                }
            }
        }
    }
    char* forbidden[] = {"throughput", "generate", "--flag", "-fno-register-allocator", NULL};
    CHECK(!tp_options(4, forbidden, &config));
    forbidden[3] = "-S";
    CHECK(!tp_options(4, forbidden, &config));
    forbidden[3] = "-c";
    CHECK(!tp_options(4, forbidden, &config));
    forbidden[3] = "-E";
    CHECK(!tp_options(4, forbidden, &config));
    char* invalid_artifact[] = {"throughput", "generate", "--artifact", "executable", NULL};
    CHECK(!tp_options(4, invalid_artifact, &config));
    char* missing_artifact[] = {"throughput", "generate", "--artifact", NULL};
    CHECK(!tp_options(3, missing_artifact, &config));
}

static void test_workload_selection(void)
{
    TpConfig config;
    char* defaults[] = {"throughput", "run", NULL};
    CHECK(tp_options(2, defaults, &config));
    CHECK(config.workload_mask == TP_DEFAULT_WORKLOAD_MASK);
    unsigned count;
    CHECK(tp_job_count(&config, TP_MAX_JOBS, &count) && count == 24);
    char* selected[] = {"throughput", "run", "--no-guard", "--workload", "aggregate-abi",
                        "--workload", "macros", "--workload", "macros", NULL};
    CHECK(tp_options(9, selected, &config));
    CHECK(config.workload_mask == (TP_ALL_WORKLOAD_MASK ^ TP_DEFAULT_WORKLOAD_MASK));
    CHECK(tp_job_count(&config, TP_MAX_JOBS, &count) && count == 8);
    selected[4] = "default";
    CHECK(tp_options(9, selected, &config));
    CHECK(config.workload_mask == (TP_DEFAULT_WORKLOAD_MASK | (1u << 6)));
    selected[4] = "all";
    CHECK(tp_options(9, selected, &config));
    CHECK(config.workload_mask == TP_ALL_WORKLOAD_MASK);
    char* invalid[] = {"throughput", "run", "--workload", "macros", NULL};
    CHECK(!tp_options(4, invalid, &config)); /* Custom corpus cannot alter the CI guard. */
    invalid[1] = "generate";
    invalid[3] = "";
    CHECK(!tp_options(4, invalid, &config));
    invalid[3] = "macro";
    CHECK(!tp_options(4, invalid, &config));
    CHECK(!tp_options(3, invalid, &config));
}

static void test_job_capacity(void)
{
    TpConfig config = {0};
    Arena* arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    TpWorkload* workloads = arena_allocate_zeroed(arena, TpWorkload, TP_CASES);
    TpJob* jobs = arena_allocate_zeroed(arena, TpJob, TP_MAX_JOBS + 1);
    CHECK(workloads && jobs);
    if (workloads && jobs)
    {
        for (unsigned kind = 0; kind < TP_CASES; ++kind)
            strcpy(workloads[kind].name, tp_case_names[kind]);
        /* Every nonempty subset, mode subset and optional stage pair. This
         * exercises counts above the former 32-job limit with real writes. */
        for (unsigned mask = 1; mask <= TP_ALL_WORKLOAD_MASK; ++mask)
        {
            config.workload_mask = mask;
            for (unsigned modes = 1; modes <= TP_ALL_MODE_MASK; ++modes)
            {
                config.mode_mask = modes;
                for (unsigned self = 0; self < 2; ++self)
                {
                    config.self_host_root = self ? "frozen" : NULL;
                    config.assembly = (int)(mask & 1u);
                    unsigned count = 0;
                    CHECK(tp_job_count(&config, TP_MAX_JOBS, &count));
                    unsigned expected = 0;
                    for (unsigned kind = 0; kind < TP_CASES; ++kind)
                        for (unsigned mode = 0; mode < TP_MODES; ++mode)
                            expected += !!(mask & (1u << kind)) && !!(modes & (1u << mode));
                    expected += self * 2;
                    CHECK(count == expected);
                    unsigned rejected = 99;
                    /* Null storage proves rejection precedes even the first write. */
                    CHECK(!tp_prepare_jobs(&config, NULL, NULL, count - 1, &rejected) && rejected == 0);
                    jobs[count].stage = 99;
                    CHECK(tp_prepare_jobs(&config, workloads, jobs, count, &count));
                    CHECK(count == expected && jobs[count].stage == 99);
                    unsigned next = 0;
                    for (unsigned kind = 0; kind < TP_CASES; ++kind)
                    {
                        for (unsigned mode = 0; mode < TP_MODES; ++mode)
                        {
                            if ((mask & (1u << kind)) && (modes & (1u << mode)))
                            {
                                CHECK(!strcmp(jobs[next].workload.name, tp_case_names[kind]) &&
                                      jobs[next].mode == mode && !jobs[next].stage && jobs[next].assembly == config.assembly);
                                ++next;
                            }
                        }
                    }
                    for (unsigned stage = 1; self && stage <= 2; ++stage)
                    {
                        CHECK(jobs[next].stage == stage && jobs[next].mode == 2 && !jobs[next].assembly &&
                              !strcmp(jobs[next].workload.path, "frozen/src/buster/apps/ide/ide.c"));
                        ++next;
                    }
                    CHECK(next == count);
                }
            }
        }
        unsigned count;
        config.workload_mask = 0;
        CHECK(!tp_prepare_jobs(&config, NULL, NULL, TP_MAX_JOBS, &count) && !count);
        config.workload_mask = TP_ALL_WORKLOAD_MASK + 1;
        CHECK(!tp_prepare_jobs(&config, NULL, NULL, TP_MAX_JOBS, &count) && !count);
        config.workload_mask = TP_ALL_WORKLOAD_MASK;
        config.mode_mask = 0;
        CHECK(!tp_prepare_jobs(&config, NULL, NULL, TP_MAX_JOBS, &count) && !count);
        config.mode_mask = TP_ALL_MODE_MASK + 1;
        CHECK(!tp_prepare_jobs(&config, NULL, NULL, TP_MAX_JOBS, &count) && !count);
    }
    arena_destroy(arena, 1);
}

static void test_optional_inputs(char const* root)
{
    static char const* const profiles[] = {"smoke", "ci", "full"};
    /* Independently reconstructed from the documented transport recipes plus
     * the input-schema comment and unsigned arithmetic adaptation. */
    static char const* const hashes[3][2] = {
        {"995792800d47ecf3edc45291dafc0600e9675404fc7a7b9787ae05c8d308c7d4", "487e98d97fd5d16bac9852df20083baa2d2ff22e41b5aa385bc2ca3521b927af"},
        {"de53fc669bce964500dea92194c9bf4a0edae1513de22a60c121e998528c9fb1", "31820c423f224ea2c5edd5b779ccf1ea59ba2c3519f4b3997cb5ee9351355054"},
        {"a99ada929f75c22fb6a4d11d927e60f181f14d6abc4bcd73ab1a53bae5919c4d", "3fc9c9514492a0142d40d2402003782a85c62be7cc0a9b5a9fe090712f41a4d2"}};
    static uint64_t const bytes[3][2] = {{2721, 13874}, {21686, 114343}, {185768, 953001}};
    static uint64_t const functions[3][2] = {{256, 128}, {2048, 1024}, {16384, 8192}};
    char a[TP_PATH_CAP], b[TP_PATH_CAP];
    CHECK(tp_path(a, root, "optional-a") && tp_path(b, root, "optional-b") && tp_mkdirs(a) && tp_mkdirs(b));
    char* options[] = {"throughput", "generate", "--workload", "aggregate-abi", "--workload", "macros", NULL};
    TpConfig config;
    CHECK(tp_options(6, options, &config));
    TpWorkload first[TP_CASES] = {0}, second[TP_CASES] = {0};
    for (unsigned profile = 0; profile < 3; ++profile)
    {
        config.profile = profiles[profile];
        config.scale = 1;
        CHECK(tp_generate(&config, a, first));
        for (unsigned recipe = 0; recipe < 2; ++recipe)
        {
            unsigned kind = TP_DEFAULT_CASES + recipe;
            CHECK(!strcmp(first[kind].hash, hashes[profile][recipe]));
            CHECK(first[kind].bytes == bytes[profile][recipe] && first[kind].functions == functions[profile][recipe]);
            CHECK(first[kind].lines == functions[profile][recipe] + (recipe ? 1 : 6));
            TpConfig single = config;
            single.workload_mask = 1u << kind;
            CHECK(tp_generate(&single, b, second));
            CHECK(!strcmp(first[kind].hash, second[kind].hash));
        }
        config.scale = 2;
        CHECK(tp_generate(&config, b, second));
        for (unsigned kind = TP_DEFAULT_CASES; kind < TP_CASES; ++kind)
            CHECK(second[kind].functions == 2 * first[kind].functions && strcmp(second[kind].hash, first[kind].hash));
    }
    for (unsigned kind = 0; kind < TP_DEFAULT_CASES; ++kind)
    {
        char path[TP_PATH_CAP], leaf[128];
        snprintf(leaf, sizeof(leaf), "%s.c", tp_case_names[kind]);
        CHECK(tp_path(path, a, leaf));
        struct stat info;
        CHECK(stat(path, &info) != 0 && errno == ENOENT);
    }
}

static void test_sample_paths(char const* executable, char const* root)
{
    char commands_path[TP_PATH_CAP], capabilities_path[TP_PATH_CAP];
    int paths_ok = tp_path(commands_path, root, "rejected-sample-commands.jsonl") &&
                   tp_path(capabilities_path, root, "rejected-sample-capabilities.jsonl");
    CHECK(paths_ok);
    FILE* commands = paths_ok ? fopen(commands_path, "wb") : NULL;
    FILE* capabilities = paths_ok ? fopen(capabilities_path, "wb") : NULL;
    CHECK(commands && capabilities);
    if (commands && capabilities)
    {
        TpJob job = {0};
        strcpy(job.workload.name, "case");
        char output_root[TP_PATH_CAP];
        /* Reject both an oversized artifact path and a metrics path after
         * the artifact path exactly fits. A null config verifies that path
         * rejection needs no compiler options and cannot reach argv setup. */
        size_t lengths[] = {TP_PATH_CAP - 1, TP_PATH_CAP - sizeof("/case-none-0.o")};
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
        {
            memset(output_root, 'x', lengths[i]);
            output_root[lengths[i]] = 0;
            TpRow row;
            CHECK(!tp_measure(NULL, &job, executable, root, output_root, 0,
                              "sample_identifier", 0, &row, commands, capabilities));
            CHECK(!row.present);
            CHECK(ftell(commands) == 0 && ftell(capabilities) == 0);
        }
    }
    if (commands) CHECK(fclose(commands) == 0);
    if (capabilities) CHECK(fclose(capabilities) == 0);
}

static void test_compiler_failures(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], commands_path[TP_PATH_CAP], capabilities_path[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "compiler-controls") && tp_mkdirs(directory) &&
                   tp_path(commands_path, directory, "commands.jsonl") &&
                   tp_path(capabilities_path, directory, "capabilities.jsonl");
    CHECK(paths_ok);
    FILE* commands = paths_ok ? fopen(commands_path, "wb") : NULL;
    FILE* capabilities = paths_ok ? fopen(capabilities_path, "wb") : NULL;
    CHECK(commands && capabilities);
    if (commands && capabilities)
    {
        TpConfig config = {.timeout = 2, .cpu = -1};
        TpJob job = {0};
        job.mode = 0;
        strcpy(job.workload.name, "case");
        char stale[TP_PATH_CAP];
        CHECK(tp_path(stale, directory, "case-none-0.o") && test_text(directory, "case-none-0.o", "stale"));
        CHECK(tp_path(job.workload.path, directory, "compiler-fail.c") && test_text(directory, "compiler-fail.c", "int value;\n"));
        TpRow row;
        CHECK(!tp_measure(&config, &job, executable, directory, directory, 0, "failed-compiler", 0, &row, commands, capabilities));
        struct stat info;
        CHECK(stat(stale, &info) != 0 && errno == ENOENT && !row.present);
        CHECK(tp_path(job.workload.path, directory, "compiler-missing.c") && test_text(directory, "compiler-missing.c", "int value;\n"));
        CHECK(!tp_measure(&config, &job, executable, directory, directory, 0, "missing-artifact", 0, &row, commands, capabilities));
        CHECK(stat(stale, &info) != 0 && errno == ENOENT && !row.present);
    }
    if (commands) CHECK(fclose(commands) == 0);
    if (capabilities) CHECK(fclose(capabilities) == 0);
}

static void test_workload_descriptors(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], source_root[TP_PATH_CAP], descriptor_path[TP_PATH_CAP], evidence[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "workload-descriptors") && tp_mkdirs(directory) &&
                   tp_path(source_root, directory, "inputs") && tp_mkdirs(source_root) &&
                   tp_path(descriptor_path, directory, "fixture.workload") &&
                   tp_path(evidence, directory, "oracle.log") && tp_path(log, directory, "preflight.log");
    CHECK(paths_ok);
    char source_path[TP_PATH_CAP], header_path[TP_PATH_CAP], source_hash[65], header_hash[65], tree_hash[65];
    uint64_t source_bytes = 0, header_bytes = 0, lines = 0;
    CHECK(paths_ok && test_text(source_root, "source.c", "#include \"header.h\"\nint value(void) { return ANSWER; }\n") &&
          test_text(source_root, "header.h", "#define ANSWER 42\n") && test_text(directory, "oracle.log", "status=failed\n") &&
          tp_path(source_path, source_root, "source.c") && tp_path(header_path, source_root, "header.h") &&
          tp_hash_file(source_path, source_hash, &source_bytes, &lines) &&
          tp_hash_file(header_path, header_hash, &header_bytes, &lines) && tp_hash_tree(source_root, tree_hash));
    char descriptor[8192];
    int length = snprintf(descriptor, sizeof(descriptor),
        "schema=%s\nname=fixture\nfamily=fixture\nsource_identity=test:fixture\n"
        "dependency_identity=none\ngenerated_identity=none\nresource_identity=runtime-required\n"
        "sysroot_identity=runtime-required\nsdk_identity=none\nenvironment_identity=runtime-required\n"
        "target=x86_64-unknown-linux-gnu\nabi=sysv-amd64\n"
        "cpu_features=baseline\nc_lowerings=local-backed-canonical,direct-ssa\npic_modes=off,on\n"
        "allocator_modes=none,mir-stack,fast,quality\noperations=source-to-object,source-to-linked-executable\n"
        "artifacts=object,executable\noracle=test:fixture\nhistorical_outcome=failed\n"
        "historical_evidence=test:failed-compiler\nadmission=fresh-required\ncwd=.\n"
        "requested_translation_unit_bytes=%" PRIu64 "\ninput_tree_sha256=%s\n"
        "input=source\tsource.c\t%s\t%" PRIu64 "\ninput=header\theader.h\t%s\t%" PRIu64 "\n"
        "compile_argv=$COMPILER\ncompile_argv=cc\ncompile_argv=-I$ROOT\ncompile_argv=$FRONTEND\ncompile_argv=$PIC\n"
        "compile_argv=-fregister-allocator=$MODE\ncompile_argv=-fsource-metrics=$METRICS\n"
        "compile_argv=-c\ncompile_argv=-o\ncompile_argv=$OUTPUT\ncompile_argv=$SOURCE\n"
        "link_argv=$COMPILER\nlink_argv=cc\nlink_argv=$OBJECTS\nlink_argv=-o\nlink_argv=$OUTPUT\n",
        TP_WORKLOAD_DESCRIPTOR_SCHEMA_V1, source_bytes, tree_hash, source_hash, source_bytes, header_hash, header_bytes);
    CHECK(length > 0 && (size_t)length < sizeof(descriptor));
    CHECK(test_text(directory, "fixture.workload", descriptor));
    TpWorkloadDescriptor parsed;
    CHECK(tp_workload_descriptor_parse(descriptor_path, &parsed));
    CHECK(parsed.input_count == 2 && parsed.requested_translation_unit_bytes == source_bytes &&
          !strcmp(parsed.historical_outcome, "failed"));
    CHECK(parsed.legacy_compile_argument_count < TP_WORKLOAD_MAX_ARGUMENTS);
    strcpy(parsed.legacy_compile_arguments[parsed.legacy_compile_argument_count++], "$UNRECOGNIZED");
    CHECK(!tp_workload_arguments_valid(parsed.legacy_compile_arguments, parsed.legacy_compile_argument_count, 0, 1));
    --parsed.legacy_compile_argument_count;
    unsigned mode_argument = parsed.legacy_compile_argument_count, root_argument = parsed.legacy_compile_argument_count;
    unsigned output_argument = parsed.legacy_compile_argument_count, output_option = parsed.legacy_compile_argument_count;
    for (unsigned i = 0; i < parsed.legacy_compile_argument_count; ++i)
    {
        if (!strcmp(parsed.legacy_compile_arguments[i], "-fregister-allocator=$MODE")) mode_argument = i;
        if (!strcmp(parsed.legacy_compile_arguments[i], "-I$ROOT")) root_argument = i;
        if (!strcmp(parsed.legacy_compile_arguments[i], "$OUTPUT")) output_argument = i;
        if (!strcmp(parsed.legacy_compile_arguments[i], "-o")) output_option = i;
    }
    CHECK(mode_argument < parsed.legacy_compile_argument_count && root_argument < parsed.legacy_compile_argument_count);
    if (mode_argument < parsed.legacy_compile_argument_count && root_argument < parsed.legacy_compile_argument_count)
    {
        strcpy(parsed.legacy_compile_arguments[mode_argument], "-fregister-allocator=$MODEjunk");
        CHECK(!tp_workload_arguments_valid(parsed.legacy_compile_arguments, parsed.legacy_compile_argument_count, 0, 1));
        strcpy(parsed.legacy_compile_arguments[mode_argument], "-fregister-allocator=$MODE");
        strcpy(parsed.legacy_compile_arguments[root_argument], "-I.");
        CHECK(!tp_workload_arguments_valid(parsed.legacy_compile_arguments, parsed.legacy_compile_argument_count, 0, 1));
        strcpy(parsed.legacy_compile_arguments[root_argument], "-I$ROOT");
        strcpy(parsed.legacy_compile_arguments[0], "-g0");
        CHECK(!tp_workload_arguments_valid(parsed.legacy_compile_arguments, parsed.legacy_compile_argument_count, 0, 1));
        strcpy(parsed.legacy_compile_arguments[0], "$COMPILER");
    }
    CHECK(output_argument < parsed.legacy_compile_argument_count && output_option < parsed.legacy_compile_argument_count);
    if (output_argument < parsed.legacy_compile_argument_count && output_option < parsed.legacy_compile_argument_count)
    {
        strcpy(parsed.legacy_compile_arguments[output_option], "$OUTPUT");
        strcpy(parsed.legacy_compile_arguments[output_argument], "-o");
        CHECK(!tp_workload_arguments_valid(parsed.legacy_compile_arguments, parsed.legacy_compile_argument_count, 0, 1));
    }
    if (paths_ok)
    {
        char* command[] = {(char*)executable, "child", "throughput", "check-workload", descriptor_path,
            "--source-root", source_root, "--compiler", (char*)executable, "--evidence", evidence,
            "--evidence-outcome", "failed", NULL};
        TpProcess result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 0 && !result.launch_error && !result.timed_out);
        FILE* file = fopen(log, "rb");
        char report[8192] = {0};
        CHECK(file != NULL);
        if (file)
        {
            size_t size = fread(report, 1, sizeof(report) - 1, file);
            CHECK(size < sizeof(report) - 1 && !ferror(file) && fclose(file) == 0);
            CHECK(strstr(report, "\"admitted\":false") && strstr(report, "\"performed_work\":null") &&
                  strstr(report, "\"oracle_evidence_outcome\":\"failed\""));
        }
        CHECK(remove(header_path) == 0);
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        CHECK(test_text(source_root, "header.h", "#define ANSWER 42\n"));
        CHECK(test_text(source_root, "source.c", "#include \"header.h\"\nint value(void) { return 7; }\n"));
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        CHECK(test_text(source_root, "source.c", "#include \"header.h\"\nint value(void) { return ANSWER; }\n"));
        CHECK(test_text(source_root, "extra.h", "undeclared\n"));
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        char extra[TP_PATH_CAP];
        CHECK(tp_path(extra, source_root, "extra.h") && remove(extra) == 0);
        command[12] = "accepted";
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        command[12] = "failed";
        command[10] = extra;
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        command[10] = evidence;
        char malformed[8192];
        length = snprintf(malformed, sizeof(malformed), "%sschema=duplicate\n", descriptor);
        CHECK(length > 0 && (size_t)length < sizeof(malformed) && test_text(directory, "fixture.workload", malformed));
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        size_t descriptor_length = strlen(descriptor) + 1;
        memcpy(malformed, descriptor, descriptor_length);
        char* unsafe = strstr(malformed, "input=source\tsource.c");
        CHECK(unsafe != NULL);
        if (unsafe) memcpy(unsafe + strlen("input=source\t"), "../bad.c", strlen("../bad.c"));
        CHECK(test_text(directory, "fixture.workload", malformed));
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
        memcpy(malformed, descriptor, descriptor_length);
        char* frontend = strstr(malformed, "compile_argv=$FRONTEND");
        CHECK(frontend != NULL);
        if (frontend) memcpy(frontend + strlen("compile_argv="), "-DMISSING", strlen("-DMISSING"));
        CHECK(test_text(directory, "fixture.workload", malformed));
        result = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(result.exit_code == 2);
    }
}

static void test_workload_admission(char const* executable, char const* root)
{
    char* missing_output[] = {"throughput", "admit-workload", "descriptor", "--source-root", "root", "--compiler", "compiler",
        "--evidence", "evidence", "--evidence-outcome", "pass", "--qualification-id", "id",
        "--dependency-manifest", "dependency", "--resource-manifest", "resource", "--sysroot-manifest", "sysroot",
        "--sdk-manifest", "sdk", "--environment-manifest", "environment", "--runtime-manifest", "runtime", NULL};
    TpConfig missing_output_config;
    CHECK(!tp_options((int)BUSTER_ARRAY_LENGTH(missing_output) - 1, missing_output, &missing_output_config));
    char directory[TP_PATH_CAP], source_root[TP_PATH_CAP], descriptor[TP_PATH_CAP], evidence[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "workload-admission") && tp_mkdirs(directory) &&
                   tp_path(source_root, directory, "inputs") && tp_mkdirs(source_root) &&
                   tp_path(descriptor, directory, "admission.workload") &&
                   tp_path(evidence, directory, "oracle.log") && tp_path(log, directory, "admission.log");
    CHECK(paths_ok);
    char source[TP_PATH_CAP], generated[TP_PATH_CAP], closure[TP_PATH_CAP];
    char source_hash[65], generated_hash[65], closure_hash[65], tree_hash[65];
    uint64_t source_bytes = 0, generated_bytes = 0, closure_bytes = 0, lines = 0;
    CHECK(paths_ok && test_text(source_root, "source.c", "int source(void) { return 1; }\n") &&
          test_text(source_root, "generated.c", "int generated(void) { return 2; }\n") &&
          test_text(directory, "closure.txt", "immutable closure\n") && test_text(directory, "oracle.log", "status=pass\n") &&
          tp_path(source, source_root, "source.c") && tp_path(generated, source_root, "generated.c") &&
          tp_path(closure, directory, "closure.txt") && tp_hash_file(source, source_hash, &source_bytes, &lines) &&
          tp_hash_file(generated, generated_hash, &generated_bytes, &lines) &&
          tp_hash_file(closure, closure_hash, &closure_bytes, &lines) && tp_hash_tree(source_root, tree_hash));
    CHECK(test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes, "",
                                    "e7635c5d652fd35a6f2c259b32694b78d0aaefc90d611609e6401a76fcf31265"));
    static char const* const names[6] = {"dependency.identity", "resource.identity", "sysroot.identity",
                                         "sdk.identity", "environment.identity", "runtime.identity"};
    static char const* const kinds[6] = {"dependency", "resource", "sysroot", "sdk", "environment", "runtime"};
    static char const* const operations[6] = {
        "source-to-linked-executable",
        "source-to-object,source-to-linked-executable",
        "source-to-object,source-to-linked-executable",
        "source-to-object,source-to-linked-executable",
        "source-to-object,source-to-linked-executable,runtime",
        "runtime"};
    static char const* const bindings[6] = {
        "argument=source-to-linked-executable|-lm\n",
        "argument=source-to-object|-ffrontend-ssa\nargument=source-to-linked-executable|-ffrontend-ssa\n",
        "argument=source-to-object|--target=x86_64-unknown-linux-gnu\nargument=source-to-linked-executable|--target=x86_64-unknown-linux-gnu\n",
        "argument=source-to-object|-mcpu=baseline\nargument=source-to-linked-executable|-mcpu=baseline\n",
        "argument=source-to-object|-fno-pic\nargument=source-to-linked-executable|-fno-pic\nargument=runtime|$EXECUTABLE\n",
        "argument=runtime|$EXECUTABLE\n"};
    char manifest_paths[6][TP_PATH_CAP];
    char* manifests[6];
    for (unsigned i = 0; i < 6; ++i)
    {
        CHECK(test_identity_manifest(directory, names[i], kinds[i], operations[i], bindings[i], closure, closure_hash, closure_bytes));
        CHECK(tp_path(manifest_paths[i], directory, names[i]));
        manifests[i] = manifest_paths[i];
    }
    char output[TP_PATH_CAP];
    CHECK(tp_path(output, directory, "success"));
#if defined(_WIN32)
    TpProcess result = test_admit_workload(executable, descriptor, source_root, evidence, output, manifests, log);
#elif defined(BUSTER_SANITIZE)
    puts("THROUGHPUT_ADMISSION_STACK status=unsupported reason=sanitizer-instrumented");
    TpProcess result = test_admit_workload(executable, descriptor, source_root, evidence, output, manifests, log);
#else
    TpProcess result = test_admit_workload_mode(executable, "throughput-low-stack", descriptor, source_root,
                                                evidence, output, manifests, log);
#endif
    CHECK(result.exit_code == 0 && !result.launch_error && !result.timed_out);
    FILE* file = fopen(log, "rb");
    char report[65536] = {0};
    CHECK(file != NULL);
    if (file)
    {
        size_t size = fread(report, 1, sizeof(report) - 1, file);
        CHECK(size < sizeof(report) - 1 && !ferror(file) && fclose(file) == 0);
        CHECK(strstr(report, "\"admitted\":true") && strstr(report, "\"performed_cells\":[") &&
              strstr(report, "\"object\":{\"command_count\":2") &&
              strstr(report, "\"translated_bytes\":32") && strstr(report, "\"runtime-transcript\"") &&
              strstr(report, "\"argv_bound\":true") && strstr(report, "}},\"argv_identity\"") &&
              !strstr(report, "}}},\"argv_identity\""));
    }

    char failed_output[TP_PATH_CAP], failure_log[TP_PATH_CAP];
    CHECK(tp_path(failure_log, directory, "admission-failure.log"));
    CHECK(tp_path(failed_output, directory, "admission-state-oom"));
    result = test_admit_workload_mode(executable, "throughput-admission-oom", descriptor, source_root,
                                      evidence, failed_output, manifests, failure_log);
    struct stat failed_status;
    CHECK(result.exit_code == 2 && stat(failed_output, &failed_status) != 0 && errno == ENOENT);
    CHECK(tp_path(failed_output, directory, "preexisting") && tp_mkdirs(failed_output));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);

    char* saved_dependency = manifests[0];
    char missing_manifest[TP_PATH_CAP];
    CHECK(tp_path(missing_manifest, directory, "missing.identity") && tp_path(failed_output, directory, "missing-dependency"));
    manifests[0] = missing_manifest;
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
    manifests[0] = saved_dependency;

    char mismatch[TP_PATH_CAP + 1024];
    int mismatch_length = snprintf(mismatch, sizeof(mismatch),
        "schema=buster-throughput-identity-v1\nkind=dependency\nidentity=test:wrong-dependency\n"
        "operation=source-to-linked-executable\nargument=source-to-linked-executable|-lm\nfile=%s\t%s\t%" PRIu64 "\n",
        closure, closure_hash, closure_bytes);
    CHECK(mismatch_length > 0 && (size_t)mismatch_length < sizeof(mismatch) &&
          test_text(directory, names[0], mismatch) && tp_path(failed_output, directory, "identity-mismatch"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
    CHECK(test_identity_manifest(directory, names[0], kinds[0], operations[0], bindings[0], closure, closure_hash, closure_bytes));

    CHECK(test_text(directory, "closure.txt", "drifted closure\n") && tp_path(failed_output, directory, "closure-drift"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
    CHECK(test_text(directory, "closure.txt", "immutable closure\n"));

    char* saved_runtime = manifests[5];
    manifests[5] = missing_manifest;
    CHECK(tp_path(failed_output, directory, "missing-runtime"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
    manifests[5] = saved_runtime;

    CHECK(test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes,
                                    "-DTP_TEST_COMPILER_FAIL",
                                    "e7635c5d652fd35a6f2c259b32694b78d0aaefc90d611609e6401a76fcf31265") &&
          tp_path(failed_output, directory, "compiler-failure"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
    char commands[TP_PATH_CAP];
    CHECK(tp_path(commands, failed_output, "commands.jsonl"));
    file = fopen(commands, "rb");
    memset(report, 0, sizeof(report));
    CHECK(file != NULL);
    if (file)
    {
        size_t size = fread(report, 1, sizeof(report) - 1, file);
        CHECK(size < sizeof(report) - 1 && !ferror(file) && fclose(file) == 0);
        CHECK(strstr(report, "\"operation_result\":\"source-to-object\"") && strstr(report, "\"exit_code\":7"));
    }

    CHECK(test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes,
                                    "-DTP_TEST_COMPILER_MISSING",
                                    "e7635c5d652fd35a6f2c259b32694b78d0aaefc90d611609e6401a76fcf31265") &&
          tp_path(failed_output, directory, "missing-artifact"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);

    CHECK(test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes,
                                    "-DTP_TEST_MUTATE_PRIOR",
                                    "e7635c5d652fd35a6f2c259b32694b78d0aaefc90d611609e6401a76fcf31265") &&
          tp_path(failed_output, directory, "artifact-drift"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);

    CHECK(test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes, "",
                                    "0000000000000000000000000000000000000000000000000000000000000000") &&
          tp_path(failed_output, directory, "runtime-mismatch"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);

    CHECK(test_text(directory, "oracle.log", "prefix status=pass suffix\n") &&
          test_admission_descriptor(directory, tree_hash, source_hash, source_bytes, generated_hash, generated_bytes, "",
                                    "e7635c5d652fd35a6f2c259b32694b78d0aaefc90d611609e6401a76fcf31265") &&
          tp_path(failed_output, directory, "oracle-substring"));
    result = test_admit_workload(executable, descriptor, source_root, evidence, failed_output, manifests, failure_log);
    CHECK(result.exit_code == 2);
}

static void test_checked_in_workload_descriptors(void)
{
    CHECK(tp_workload_absolute_path_syntax("/tmp/buster/closure", 0));
    CHECK(!tp_workload_absolute_path_syntax("tmp/buster/closure", 0));
    CHECK(tp_workload_absolute_path_syntax("D:/a/buster/closure", 1));
    CHECK(tp_workload_absolute_path_syntax("d:\\a\\buster\\closure", 1));
    CHECK(tp_workload_absolute_path_syntax("//server/share/closure", 1));
    CHECK(tp_workload_absolute_path_syntax("\\\\server\\share\\closure", 1));
    CHECK(!tp_workload_absolute_path_syntax("D:relative", 1));
    CHECK(!tp_workload_absolute_path_syntax("/drive-relative", 1));
    CHECK(!tp_workload_absolute_path_syntax("//server", 1));
    static char const* const paths[] = {
        "tools/throughput/workloads/cjson-1.7.19.workload",
        "tools/throughput/workloads/lua-5.4.8.workload",
        "tools/throughput/workloads/sqlite-3.53.4.workload"};
    static char const* const families[] = {"cjson", "lua", "sqlite"};
    static unsigned const object_counts[] = {3, 34, 2};
    static unsigned const link_counts[] = {3, 33, 2};
    for (unsigned i = 0; i < BUSTER_ARRAY_LENGTH(paths); ++i)
    {
        TpWorkloadDescriptor descriptor;
        CHECK(tp_workload_descriptor_parse(paths[i], &descriptor));
        CHECK(!strcmp(descriptor.family, families[i]) && descriptor.link_input_count == link_counts[i]);
        unsigned object_count = 0;
        uint64_t largest_source = 0;
        for (unsigned j = 0; j < descriptor.input_count; ++j)
        {
            int source = tp_workload_source_input(descriptor.inputs + j);
            object_count += source;
            if (source && descriptor.inputs[j].bytes > largest_source) largest_source = descriptor.inputs[j].bytes;
        }
        CHECK(object_count == object_counts[i]);
        if (!strcmp(descriptor.family, "sqlite")) CHECK(largest_source > UINT64_C(9500000));
    }
}

static void test_inputs(char const* root)
{
    char a[TP_PATH_CAP], b[TP_PATH_CAP], hash_a[65], hash_b[65];
    CHECK(tp_path(a, root, "inputs-a") && tp_path(b, root, "inputs-b") && tp_mkdirs(a) && tp_mkdirs(b));
    char* options[] = {"throughput", "generate", "--profile", "smoke", NULL};
    TpConfig config;
    CHECK(tp_options(4, options, &config));
    TpWorkload first[TP_CASES], second[TP_CASES];
    CHECK(tp_generate(&config, a, first) && tp_generate(&config, b, second));
    for (unsigned i = 0; i < TP_DEFAULT_CASES; ++i)
        CHECK(!strcmp(first[i].hash, second[i].hash) && first[i].bytes && first[i].lines && first[i].functions);
    /* Pin all default inputs: opt-in coverage must not expand the CI corpus
     * or change which random draw belongs to each backend operand. */
    static char const* const default_hashes[TP_DEFAULT_CASES] = {
        "ba2d69c491960aeb405d9379ee9e938f8703367d61577d8e9ada16187d284c40",
        "6b2c0a8094e7c1eb64c4ccb8b86d8d982d25141bcabe54e029011a7bd310bd0e",
        "1fdddb874b78b52c47898df53873b50af0de52a7669b62781a26c4fbd53b2ecf",
        "d10004c51b1b88f386843fa629139a06965fcc77729e14210bf038063d536f79",
        "cd5f16811fdb7c542dcc26c3454d1e3fe7762104a19b8968fdd2e46a25c13a2e",
        "e1148b330b3d11a02937cbb0c474e203dd7a988608595f300d36b3965a5be9ee"};
    for (unsigned kind = 0; kind < TP_DEFAULT_CASES; ++kind) CHECK(!strcmp(first[kind].hash, default_hashes[kind]));
    CHECK(tp_hash_tree(a, hash_a) && tp_hash_tree(b, hash_b) && !strcmp(hash_a, hash_b));
    config.seed++;
    CHECK(tp_generate(&config, b, second));
    CHECK(tp_hash_tree(b, hash_b) && strcmp(hash_a, hash_b));
#ifndef _WIN32
    char link[TP_PATH_CAP];
    CHECK(tp_path(link, b, "forbidden-link"));
    (void)remove(link);
    CHECK(symlink(first[0].path, link) == 0);
    CHECK(!tp_hash_tree(b, hash_b));
    CHECK(remove(link) == 0);
#endif
    --config.seed;
    config.workload_mask = TP_ALL_WORKLOAD_MASK;
    CHECK(tp_generate(&config, b, second));
    for (unsigned kind = 0; kind < TP_DEFAULT_CASES; ++kind) CHECK(!strcmp(first[kind].hash, second[kind].hash));
    char metrics[TP_PATH_CAP];
    CHECK(tp_path(metrics, root, "test.metrics"));
    TpRow row = {0};
    CHECK(test_text(root, "test.metrics", "version=1\nlexed.translated_bytes=42\nlexed.translated_lines=5\nfuture.key=abc\n"));
    CHECK(tp_read_metrics(metrics, &row) && row.source_bytes == 42 && row.source_lines == 5 && isnan(row.arena_calls));
    CHECK(test_text(root, "test.metrics", "lexed.translated_bytes=42\nlexed.translated_lines=5\nallocation.arena_calls=3\nallocation.arena_bytes=15\n"));
    CHECK(tp_read_metrics(metrics, &row) && row.arena_calls == 3 && row.arena_bytes == 15);
    CHECK(test_text(root, "test.metrics", "lexed.translated_bytes=42\nlexed.translated_lines=5\nlexed.translated_lines=6\n"));
    CHECK(!tp_read_metrics(metrics, &row));
    CHECK(test_text(root, "test.metrics", "lexed.translated_bytes=-1\nlexed.translated_lines=5\n"));
    CHECK(!tp_read_metrics(metrics, &row));
    CHECK(test_text(root, "test.metrics", "lexed.translated_bytes=42\n"));
    CHECK(!tp_read_metrics(metrics, &row));
}

static void test_maximum_jobs(char const* root)
{
    char directory[TP_PATH_CAP], path[TP_PATH_CAP];
    int ok = tp_path(directory, root, "maximum-jobs") && test_bundle(directory, 0);
    CHECK(ok);
    Arena* arena = arena_create((ArenaCreation){.flags = {.no_pool = 1}});
    TpRow* rows = arena_allocate_zeroed(arena, TpRow, TP_ROUNDS * 2 * 20);
    CHECK(rows != NULL);
    ok = ok && rows && tp_load_samples(directory, "samples.csv", 1, 20, 0, rows);
    CHECK(ok);
    FILE* manifest = ok && tp_path(path, directory, "jobs.tsv") ? fopen(path, "wb") : NULL;
    FILE* samples = ok && tp_path(path, directory, "samples.csv") ? fopen(path, "wb") : NULL;
    CHECK(manifest && samples);
    ok = ok && manifest && samples;
    if (ok)
    {
        fputs(TP_RAW_HEADER, samples);
        for (unsigned job = 0; job < TP_MAX_JOBS; ++job)
        {
            fprintf(manifest, "%u\tfixture-%u/fast\n", job, job);
            for (unsigned round = 0; round < TP_ROUNDS; ++round)
                for (unsigned pair = 0; pair < 20; ++pair)
                    for (unsigned variant = 0; variant < 2; ++variant)
                        CHECK(tp_sample_csv(samples, round, pair, variant ^ (pair & 1), variant, job,
                                            rows + tp_row_index(0, round, variant, pair, 20)));
        }
    }
    if (manifest) CHECK(fclose(manifest) == 0);
    if (samples) CHECK(fclose(samples) == 0);
    if (ok)
    {
        CHECK(tp_completion(directory, TP_MAX_JOBS, 20, 0, 1));
        CHECK(tp_compare(directory) == 0);
    }
    arena_destroy(arena, 1);
}

static void test_summaries(char const* root, int expected)
{
    for (unsigned i = 0; i < sizeof(tp_summary_names) / sizeof(tp_summary_names[0]); ++i)
    {
        char path[TP_PATH_CAP];
        int path_ok = tp_path(path, root, tp_summary_names[i]);
        CHECK(path_ok);
        if (path_ok)
        {
            struct stat info;
            int status = stat(path, &info);
            CHECK(expected ? status == 0 : status != 0 && errno == ENOENT);
        }
    }
}

static void test_summary_cleanup(char const* root)
{
    char directory[TP_PATH_CAP], blocked[TP_PATH_CAP], path[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "blocked-summary") && tp_mkdirs(directory) &&
                   tp_path(blocked, directory, tp_summary_names[0]) && tp_mkdirs(blocked);
    CHECK(paths_ok);
    if (paths_ok)
    {
        /* A nonempty directory fails removal on every supported host. The
         * other report must still be removed, and unrelated contents retained. */
        CHECK(test_text(blocked, "keep.txt", "not a derived report\n"));
        CHECK(test_text(directory, tp_summary_names[1], "stale verdict\n"));
        CHECK(!tp_remove_summaries(directory));
        struct stat info;
        CHECK(tp_path(path, directory, tp_summary_names[1]));
        CHECK(stat(path, &info) != 0 && errno == ENOENT);
        CHECK(tp_path(path, blocked, "keep.txt"));
        CHECK(stat(path, &info) == 0);
        CHECK(tp_compare(directory) == 2);
    }
}

#ifndef _WIN32
static void test_summary_write_failure(char const* executable, char const* root)
{
    char directory[TP_PATH_CAP], log[TP_PATH_CAP];
    int paths_ok = tp_path(directory, root, "summary-write-failure") &&
                   tp_path(log, root, "summary-write-failure.log");
    CHECK(paths_ok);
    if (paths_ok)
    {
        CHECK(test_bundle(directory, 0));
        char* command[] = {(char*)executable, "child", "summary-write-failure", directory, NULL};
        TpProcess child = tp_process(command, NULL, log, 3, -1, 0);
        CHECK(child.exit_code == 0 && !child.timed_out && !child.launch_error);
        test_summaries(directory, 0);
        CHECK(tp_completion(directory, 1, 20, 1, 0));
    }
}
#endif

static void test_retirement_statistics(void)
{
    enum { PAIRS = TP_RETIREMENT_MIN_PAIRS_PER_ROUND };
    double ratios[2 * TP_RETIREMENT_ROUNDS * TP_RETIREMENT_MAX_PAIRS_PER_ROUND];
    double* workspace = (double*)malloc((size_t)TP_RETIREMENT_MIN_RESAMPLES * sizeof(*workspace));
    TpRetirementPlan plan = {
        .seed = UINT64_C(0x0123456789abcdef),
        .version = TP_RETIREMENT_STATISTICS_VERSION,
        .bootstrap_members_per_scope = 1,
        .cell_members_per_scope = 1,
        .pairs_per_round = PAIRS,
        .resamples = TP_RETIREMENT_MIN_RESAMPLES,
        .frozen_before_samples = 1,
    };
    TpRetirementSeries series = {
        .ratios = ratios,
        .ratio_count = TP_RETIREMENT_ROUNDS * PAIRS,
        .cell_count = 1,
        .observed_pairs = {PAIRS, PAIRS},
        .member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER,
        .family_index = 0,
        .metric_index = TP_RETIREMENT_WALL_TIME,
        .limit = 1.02,
    };
    CHECK(workspace != NULL);
    if (workspace)
    {
        for (unsigned i = 0; i < TP_RETIREMENT_ROUNDS * PAIRS; ++i) ratios[i] = 1.0;
        TpRetirementResult unchanged = tp_retirement_assess(&plan, &series, NULL, 0);
        CHECK(unchanged.valid && unchanged.outcome == TP_RETIREMENT_PASS && !unchanged.resampled && !unchanged.resamples);
        CHECK(unchanged.round[0].estimate == 1.0 && unchanged.round[0].lower == 1.0 && unchanged.round[0].upper == 1.0);
        CHECK(unchanged.round[1].estimate == 1.0 && unchanged.pooled.estimate == 1.0);
        CHECK(unchanged.tail_alpha == TP_RETIREMENT_FAMILY_ALPHA / 12.0);

        for (unsigned i = 0; i < TP_RETIREMENT_ROUNDS * PAIRS; ++i) ratios[i] = 1.08;
        series.limit = 1.05;
        TpRetirementResult regression = tp_retirement_assess(&plan, &series, NULL, 0);
        CHECK(regression.valid && regression.outcome == TP_RETIREMENT_REGRESSION);
        CHECK(regression.round[0].lower > series.limit && regression.round[1].lower > series.limit &&
              regression.pooled.lower > series.limit);

        for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
            for (unsigned pair = 0; pair < PAIRS; ++pair)
                ratios[round * PAIRS + pair] = pair < PAIRS / 2 ? 0.92 : 1.20;
        series.limit = 1.05;
        TpRetirementResult broad = tp_retirement_assess(&plan, &series, NULL, 0);
        CHECK(broad.valid && broad.outcome == TP_RETIREMENT_INCONCLUSIVE);
        CHECK(broad.round[0].estimate > series.limit);
        CHECK(broad.round[0].lower <= series.limit && broad.round[0].upper > series.limit);

        for (unsigned pair = 0; pair < PAIRS; ++pair)
        {
            ratios[pair] = 1.0;
            ratios[PAIRS + pair] = 1.10;
        }
        TpRetirementResult disagreement = tp_retirement_assess(&plan, &series, NULL, 0);
        CHECK(disagreement.valid && disagreement.outcome == TP_RETIREMENT_INCONCLUSIVE);
        CHECK(disagreement.round[0].upper <= series.limit && disagreement.round[1].lower > series.limit);
        CHECK(disagreement.pooled.lower <= disagreement.round[0].lower &&
              disagreement.pooled.upper >= disagreement.round[1].upper);

        for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
            for (unsigned block = 0; block < PAIRS / 2; ++block)
                for (unsigned within = 0; within < 2; ++within)
                    ratios[round * PAIRS + block * 2 + within] = 1.0 + (double)block * 0.01;
        plan.cell_members_per_scope = 1;
        series.limit = 2.0;
        TpRetirementResult cell_family_one = tp_retirement_assess(&plan, &series, NULL, 0);
        plan.cell_members_per_scope = TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE;
        TpRetirementResult cell_family_many = tp_retirement_assess(&plan, &series, NULL, 0);
        CHECK(cell_family_one.valid && cell_family_many.valid &&
              cell_family_one.tail_alpha == TP_RETIREMENT_FAMILY_ALPHA / 12.0);
        CHECK(cell_family_many.tail_alpha == TP_RETIREMENT_FAMILY_ALPHA /
              (12.0 * TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE));
        CHECK(cell_family_many.round[0].lower < cell_family_one.round[0].lower &&
              cell_family_many.round[0].upper > cell_family_one.round[0].upper);

        plan.cell_members_per_scope = 1;
        for (unsigned cell = 0; cell < 2; ++cell)
            for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
                for (unsigned block = 0; block < PAIRS / 2; ++block)
                    for (unsigned within = 0; within < 2; ++within)
                        ratios[((cell * TP_RETIREMENT_ROUNDS + round) * PAIRS) +
                               block * 2 + within] = 1.0 + (double)block * 0.01;
        series.cell_count = 2;
        series.ratio_count = 2 * TP_RETIREMENT_ROUNDS * PAIRS;
        series.member_kind = TP_RETIREMENT_BOOTSTRAP_MEMBER;
        series.limit = 2.0;
        TpRetirementResult bootstrap_family_one =
            tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        plan.bootstrap_members_per_scope = TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE;
        TpRetirementResult bootstrap_family_many =
            tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        CHECK(bootstrap_family_one.valid && bootstrap_family_many.valid &&
              bootstrap_family_one.tail_alpha == TP_RETIREMENT_FAMILY_ALPHA / 12.0);
        CHECK(bootstrap_family_many.tail_alpha == TP_RETIREMENT_FAMILY_ALPHA /
              (12.0 * TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE));
        CHECK(bootstrap_family_many.round[0].lower < bootstrap_family_one.round[0].lower &&
              bootstrap_family_many.round[0].upper > bootstrap_family_one.round[0].upper);

        plan.bootstrap_members_per_scope = 1;
        for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
            for (unsigned pair = 0; pair < PAIRS; ++pair)
            {
                ratios[round * PAIRS + pair] = 1.0;
                ratios[(TP_RETIREMENT_ROUNDS + round) * PAIRS + pair] = 1.21;
            }
        series.cell_count = 2;
        series.ratio_count = 2 * TP_RETIREMENT_ROUNDS * PAIRS;
        series.member_kind = TP_RETIREMENT_BOOTSTRAP_MEMBER;
        series.limit = 1.11;
        TpRetirementResult aggregate = tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        CHECK(aggregate.valid && aggregate.outcome == TP_RETIREMENT_PASS && aggregate.resampled &&
              aggregate.resamples == plan.resamples);
        CHECK(fabs(aggregate.pooled.estimate - 1.10) < 1e-12);

        for (unsigned cell = 0; cell < 2; ++cell)
            for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
                for (unsigned block = 0; block < PAIRS / 2; ++block)
                    for (unsigned within = 0; within < 2; ++within)
                    {
                        int low = cell == 0 ? block < PAIRS / 4 : block >= PAIRS / 4;
                        ratios[((cell * TP_RETIREMENT_ROUNDS + round) * PAIRS) +
                               block * 2 + within] = low ? 1.0 : 9.0;
                    }
        series.limit = 10.0;
        TpRetirementResult crossed = tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        double geometric_mean_of_cell_medians = sqrt(5.0 * 5.0);
        CHECK(crossed.valid && fabs(crossed.round[0].estimate - 3.0) < 1e-12 &&
              fabs(crossed.round[1].estimate - 3.0) < 1e-12 &&
              fabs(crossed.pooled.estimate - 3.0) < 1e-12);
        CHECK(geometric_mean_of_cell_medians == 5.0 &&
              crossed.pooled.estimate != geometric_mean_of_cell_medians);

        static unsigned char const round_two_is_one[PAIRS / 2] = {
            0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0,
            0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0,
        };
        for (unsigned round = 0; round < TP_RETIREMENT_ROUNDS; ++round)
            for (unsigned block = 0; block < PAIRS / 2; ++block)
            {
                double value = round == 0 ? (block < 10 ? 1.0 : (block < 20 ? 2.0 : 4.0)) :
                                               (round_two_is_one[block] ? 1.0 : 4.0);
                for (unsigned cell = 0; cell < 2; ++cell)
                    for (unsigned within = 0; within < 2; ++within)
                        ratios[((cell * TP_RETIREMENT_ROUNDS + round) * PAIRS) + block * 2 + within] = value;
            }
        series.limit = 4.0;
        TpRetirementResult known = tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        CHECK(known.valid && known.outcome == TP_RETIREMENT_PASS && known.resampled);
        CHECK(fabs(known.round[0].estimate - 2.0) < 1e-12 && fabs(known.round[1].estimate - 2.5) < 1e-12 &&
              fabs(known.pooled.estimate - 2.0) < 1e-12);
        CHECK(fabs(known.round[0].lower - 1.0) < 1e-12 && fabs(known.round[0].upper - 4.0) < 1e-12 &&
              fabs(known.round[1].lower - 1.0) < 1e-12 && fabs(known.round[1].upper - 4.0) < 1e-12 &&
              fabs(known.pooled.lower - 1.0) < 1e-12 && fabs(known.pooled.upper - 4.0) < 1e-12);
        TpRetirementResult replay = tp_retirement_assess(&plan, &series, workspace, plan.resamples);
        CHECK(known.outcome == replay.outcome && known.tail_alpha == replay.tail_alpha &&
              known.round[0].lower == replay.round[0].lower && known.round[0].upper == replay.round[0].upper &&
              known.round[1].lower == replay.round[1].lower && known.round[1].upper == replay.round[1].upper &&
              known.pooled.lower == replay.pooled.lower && known.pooled.upper == replay.pooled.upper);
        CHECK(tp_retirement_quantile_rank(TP_RETIREMENT_MIN_RESAMPLES,
                                          TP_RETIREMENT_FAMILY_ALPHA / 12.0) == 416);
        CHECK(tp_retirement_quantile_rank(TP_RETIREMENT_MIN_RESAMPLES,
                                          1.0 - TP_RETIREMENT_FAMILY_ALPHA / 12.0) == 99583);
        /* Generated independently from the documented SplitMix64 protocol,
         * without calling any retirement-statistics implementation helper.
         */
        static unsigned const expected_draws[] = {23, 0, 19, 3, 28, 20, 12, 12, 0, 21, 18, 18};
        TpRetirementRandom bootstrap_random = {
            tp_retirement_seed(plan.seed, TP_RETIREMENT_SEED_DOMAIN_BOOTSTRAP,
                               TP_RETIREMENT_WALL_TIME, 0, TP_RETIREMENT_ROUNDS)};
        for (unsigned i = 0; i < sizeof(expected_draws) / sizeof(expected_draws[0]); ++i)
            CHECK(tp_retirement_random_bounded(&bootstrap_random, PAIRS / 2) == expected_draws[i]);
        double known_blocks[PAIRS] = {0}, known_sample[PAIRS] = {0};
        for (unsigned block = 0; block < PAIRS / 2; ++block)
        {
            known_blocks[block] = block < 10 ? 1.0 : (block < 20 ? 2.0 : 4.0);
            known_blocks[PAIRS / 2 + block] = round_two_is_one[block] ? 1.0 : 4.0;
        }
        bootstrap_random.state = tp_retirement_seed(plan.seed, TP_RETIREMENT_SEED_DOMAIN_BOOTSTRAP,
                                                     TP_RETIREMENT_WALL_TIME, 0,
                                                     TP_RETIREMENT_ROUNDS);
        CHECK(tp_retirement_bootstrap_sample(&bootstrap_random, known_blocks, PAIRS / 2,
                                             TP_RETIREMENT_ROUNDS, known_sample) == 4.0);

        unsigned orientations[7], first_cells[7], second_cells[7], replay_orientations[7],
                 replay_first[7], replay_second[7], first_seen = 0, second_seen = 0;
        CHECK(tp_retirement_block_schedule(plan.seed, 1, 9, 7, orientations, first_cells, second_cells, 7));
        CHECK(tp_retirement_block_schedule(plan.seed, 1, 9, 7, replay_orientations, replay_first, replay_second, 7));
        CHECK(!memcmp(orientations, replay_orientations, sizeof(orientations)) &&
              !memcmp(first_cells, replay_first, sizeof(first_cells)) &&
              !memcmp(second_cells, replay_second, sizeof(second_cells)));
        CHECK(orientations[0] == TP_RETIREMENT_AB && orientations[1] == TP_RETIREMENT_BA &&
              orientations[2] == TP_RETIREMENT_AB && orientations[3] == TP_RETIREMENT_BA &&
              orientations[4] == TP_RETIREMENT_AB && orientations[5] == TP_RETIREMENT_AB &&
              orientations[6] == TP_RETIREMENT_BA);
        CHECK(first_cells[0] == 5 && first_cells[1] == 3 && first_cells[2] == 6 && first_cells[3] == 2 &&
              first_cells[4] == 1 && first_cells[5] == 4 && first_cells[6] == 0);
        CHECK(second_cells[0] == 6 && second_cells[1] == 3 && second_cells[2] == 5 && second_cells[3] == 1 &&
              second_cells[4] == 4 && second_cells[5] == 2 && second_cells[6] == 0);
        CHECK(tp_retirement_seed(plan.seed, TP_RETIREMENT_SEED_DOMAIN_BOOTSTRAP,
                                 TP_RETIREMENT_WALL_TIME, 0, 0) ==
              UINT64_C(0x7cf91efeea31f8fd));
        for (unsigned i = 0; i < 7; ++i)
        {
            CHECK(orientations[i] <= TP_RETIREMENT_BA);
            if (first_cells[i] < 7) first_seen |= 1u << first_cells[i];
            if (second_cells[i] < 7) second_seen |= 1u << second_cells[i];
        }
        CHECK(first_seen == 0x7f && second_seen == 0x7f);
        CHECK(!tp_retirement_block_schedule(plan.seed, 2, 9, 7, orientations, first_cells, second_cells, 7));

        series.member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER;
        series.cell_count = 1;
        series.ratio_count = TP_RETIREMENT_ROUNDS * PAIRS;
        series.limit = 1.05;
        ratios[0] = NAN;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        ratios[0] = INFINITY;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        ratios[0] = 0.0;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        ratios[0] = 1.0;
        series.ratios = NULL;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        series.ratios = ratios;
        --series.ratio_count;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        ++series.ratio_count;
        --series.observed_pairs[1];
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        ++series.observed_pairs[1];
        plan.frozen_before_samples = 0;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        plan.frozen_before_samples = 1;
        series.family_index = 1;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        series.family_index = 0;
        series.metric_index = TP_RETIREMENT_VARIABLE_METRICS;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        series.metric_index = 0;
        plan.resamples = TP_RETIREMENT_MIN_RESAMPLES - 1;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        plan.resamples = TP_RETIREMENT_MIN_RESAMPLES + 1;
        series.member_kind = TP_RETIREMENT_BOOTSTRAP_MEMBER;
        series.cell_count = 2;
        series.ratio_count = 2 * TP_RETIREMENT_ROUNDS * PAIRS;
        CHECK(tp_retirement_assess(&plan, &series, workspace, TP_RETIREMENT_MIN_RESAMPLES).outcome == TP_RETIREMENT_INVALID);
        plan.resamples = TP_RETIREMENT_MIN_RESAMPLES;
        plan.bootstrap_members_per_scope = TP_RETIREMENT_MAX_BOOTSTRAP_MEMBERS_PER_SCOPE;
        CHECK(tp_retirement_validate(&plan, &series, workspace, plan.resamples));
        ++plan.bootstrap_members_per_scope;
        CHECK(!tp_retirement_validate(&plan, &series, workspace, plan.resamples));
        plan.bootstrap_members_per_scope = 1;
        plan.resamples = TP_RETIREMENT_MAX_RESAMPLES + 1;
        CHECK(tp_retirement_assess(&plan, &series, workspace, plan.resamples).outcome == TP_RETIREMENT_INVALID);
        plan.resamples = TP_RETIREMENT_MIN_RESAMPLES;
        series.member_kind = TP_RETIREMENT_EXACT_CELL_MEMBER;
        series.cell_count = 1;
        series.ratio_count = TP_RETIREMENT_ROUNDS * PAIRS;
        CHECK(TP_RETIREMENT_MAX_PAIRS_PER_ROUND == TP_MAX_PAIRS);
        series.cell_count = TP_RETIREMENT_MAX_CELLS + 1;
        CHECK(tp_retirement_assess(&plan, &series, NULL, 0).outcome == TP_RETIREMENT_INVALID);
        series.cell_count = 1;
        plan.cell_members_per_scope = TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE;
        series.family_index = TP_RETIREMENT_MAX_CELL_MEMBERS_PER_SCOPE - 1;
        CHECK(tp_retirement_validate(&plan, &series, NULL, 0));
        ++plan.cell_members_per_scope;
        CHECK(!tp_retirement_validate(&plan, &series, NULL, 0));
        plan.cell_members_per_scope = 1;
        series.family_index = 0;
        plan.pairs_per_round = TP_RETIREMENT_MAX_PAIRS_PER_ROUND;
        series.ratio_count = (size_t)TP_RETIREMENT_ROUNDS * TP_RETIREMENT_MAX_PAIRS_PER_ROUND;
        series.observed_pairs[0] = series.observed_pairs[1] = TP_RETIREMENT_MAX_PAIRS_PER_ROUND;
        for (unsigned i = 0; i < TP_RETIREMENT_ROUNDS * TP_RETIREMENT_MAX_PAIRS_PER_ROUND; ++i) ratios[i] = 1.0;
        CHECK(tp_retirement_validate(&plan, &series, NULL, 0));
        plan.pairs_per_round = TP_RETIREMENT_MAX_PAIRS_PER_ROUND + 2;
        CHECK(!tp_retirement_validate(&plan, &series, NULL, 0));
        free(workspace);
    }
}

#include "qualification_test.h"

int main(int argc, char** argv)
{
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    int result = 2;
    if (argc >= 2 && !strcmp(argv[1], "cc")) result = test_compiler_child(argc, argv);
    else if (argc >= 2 && !strcmp(argv[1], "child")) result = test_child(argc, argv);
    else if (argc == 2)
    {
        char root[TP_PATH_CAP], executable[TP_PATH_CAP];
        CHECK(tp_absolute(argv[1], root) && tp_mkdirs(root) && tp_absolute(argv[0], executable));
        CHECK(tp_self_test() == 0);
        static int const expected[] = {0, 1, 0, 0, 0, 1, 2, 2, 2, 2, 0, 0};
        for (unsigned scenario = 0; scenario < sizeof(expected) / sizeof(expected[0]); ++scenario)
        {
            char path[TP_PATH_CAP], leaf[128];
            snprintf(leaf, sizeof(leaf), "scenario-%u", scenario);
            CHECK(tp_path(path, root, leaf) && test_bundle(path, scenario));
            CHECK(test_text(path, tp_summary_names[0], "stale verdict\n"));
            CHECK(test_text(path, tp_summary_names[1], "stale verdict\n"));
            CHECK(tp_compare(path) == expected[scenario]);
            test_summaries(path, expected[scenario] != 2);
            /* Comparison/cleanup must preserve all six sealed evidence files. */
            CHECK(tp_completion(path, 1, 20, 1, 0));
        }
        char tampered[TP_PATH_CAP];
        CHECK(tp_path(tampered, root, "scenario-0"));
        test_summaries(tampered, 1);
        CHECK(test_text(tampered, "metadata.json", "changed\n"));
        CHECK(tp_compare(tampered) == 2);
        test_summaries(tampered, 0);
        CHECK(test_bundle(tampered, 0));
        CHECK(tp_compare(tampered) == 0);
        char completion[TP_PATH_CAP];
        CHECK(tp_path(completion, tampered, "complete.txt"));
        CHECK(remove(completion) == 0);
        CHECK(tp_compare(tampered) == 2);
        test_summaries(tampered, 0);
        test_summary_cleanup(root);
#ifndef _WIN32
        test_summary_write_failure(executable, root);
#endif
        test_process_fields(root);
        test_diagnostic_probes(root);
        test_legacy_schema(executable, root);
        test_compile_options();
        test_workload_selection();
        test_job_capacity();
        test_optional_inputs(root);
        test_host_qualification(executable, root);
        test_sample_paths(executable, root);
        test_compiler_failures(executable, root);
        test_workload_descriptors(executable, root);
        test_workload_admission(executable, root);
        test_checked_in_workload_descriptors();
        test_inputs(root);
        test_maximum_jobs(root);
        test_processes(executable, root);
        test_retirement_statistics();
        printf("THROUGHPUT_RECORD_BYTES process=%zu row=%zu job=%zu max_jobs=%u run_heap=%zu replay_heap=%zu\n",
               sizeof(TpProcess), sizeof(TpRow), sizeof(TpJob), (unsigned)TP_MAX_JOBS,
               TP_MAX_JOBS * (sizeof(TpJob) + 2 * sizeof(TpRow)),
               (size_t)TP_MAX_JOBS * TP_ROUNDS * 2 * (TP_MAX_PAIRS + 3) * sizeof(TpRow));
        fprintf(stdout, "THROUGHPUT_INTEGRATION_TEST assertions=%u failures=%u\n", test_assertions, test_failures);
        result = test_failures ? 1 : 0;
    }
    else fprintf(stderr, "usage: throughput-tests OUTPUT_DIRECTORY\n");
    thread_context_release(context);
    arena_pool_release_thread();
    return result;
}
