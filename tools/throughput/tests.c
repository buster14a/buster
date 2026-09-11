/* Native regression tests for the harness itself. Synthetic timings below are
 * fixtures, never compiler measurements. Child modes exercise the OS boundary.
 * Build: clang -std=c11 -O2 -Wall -Wextra -Werror tests.c -lm -o throughput-tests
 */
#define main throughput_cli_main
#include "throughput.c"
#undef main

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

static int test_child(int argc, char** argv)
{
    int result = 0;
    if (argc < 3) result = 2;
    else if (!strcmp(argv[2], "fail")) result = 7;
    else if (!strcmp(argv[2], "throughput")) result = throughput_cli_main(argc - 2, argv + 2);
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
    else if (!strcmp(argv[2], "memory"))
    {
        size_t bytes = (size_t)96 * 1024 * 1024;
        volatile unsigned char* memory = (volatile unsigned char*)malloc(bytes);
        if (!memory) result = 3;
        else
        {
            for (size_t i = 0; i < bytes; i += 4096) memory[i] = (unsigned char)(i >> 12);
            test_delay(10);
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

static void test_processes(char const* executable, char const* root)
{
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
    CHECK(large.exit_code == 0 && large.peak_rss_bytes >= 80.0 * 1048576.0);
    TpProcess small = tp_process(fail, NULL, log, 2, -1, 0);
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

static void test_inputs(char const* root)
{
    char a[TP_PATH_CAP], b[TP_PATH_CAP], hash_a[65], hash_b[65];
    CHECK(tp_path(a, root, "inputs-a") && tp_path(b, root, "inputs-b") && tp_mkdirs(a) && tp_mkdirs(b));
    char* options[] = {"throughput", "generate", "--profile", "smoke", NULL};
    TpConfig config;
    CHECK(tp_options(4, options, &config));
    TpWorkload first[TP_CASES], second[TP_CASES];
    CHECK(tp_generate(&config, a, first) && tp_generate(&config, b, second));
    for (unsigned i = 0; i < TP_CASES; ++i)
        CHECK(!strcmp(first[i].hash, second[i].hash) && first[i].bytes && first[i].lines && first[i].functions);
    /* Pin the specified seed's corpus across host compilers, including which
     * successive random draw belongs to each backend initializer operand. */
    CHECK(!strcmp(first[5].hash, "e1148b330b3d11a02937cbb0c474e203dd7a988608595f300d36b3965a5be9ee"));
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

#include "qualification_test.h"

int main(int argc, char** argv)
{
    int result = 2;
    if (argc >= 2 && !strcmp(argv[1], "child")) result = test_child(argc, argv);
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
        test_host_qualification(executable, root);
        test_sample_paths(executable, root);
        test_inputs(root);
        test_processes(executable, root);
        printf("THROUGHPUT_RECORD_BYTES process=%zu row=%zu\n", sizeof(TpProcess), sizeof(TpRow));
        fprintf(stdout, "THROUGHPUT_INTEGRATION_TEST assertions=%u failures=%u\n", test_assertions, test_failures);
        result = test_failures ? 1 : 0;
    }
    else fprintf(stderr, "usage: throughput-tests OUTPUT_DIRECTORY\n");
    return result;
}
