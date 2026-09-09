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
    else if (!strcmp(argv[2], "echo"))
    {
        for (int i = 3; i < argc; ++i) printf("%s\n", argv[i]);
    }
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
    TpProcess large = tp_process(memory, NULL, log, 3, -1, 0);
    CHECK(large.exit_code == 0 && large.peak_rss_bytes >= 80.0 * 1048576.0);
    TpProcess small = tp_process(fail, NULL, log, 2, -1, 0);
    CHECK(small.exit_code == 7 && small.peak_rss_bytes < large.peak_rss_bytes * 0.8);
    CHECK(large.user_seconds >= 0 && large.system_seconds >= 0 && large.wall_seconds > 0);
    char* echo[] = {(char*)executable, "child", "echo", "space in argument", "quote\"inside", "tail\\", "", NULL};
    TpProcess quoted = tp_process(echo, NULL, log, 2, -1, 1);
    CHECK(quoted.exit_code == 0);
    FILE* file = fopen(log, "rb");
    char text[256] = {0};
    if (file) { (void)fread(text, 1, sizeof(text) - 1, file); fclose(file); }
#ifdef _WIN32
    CHECK(!strcmp(text, "space in argument\r\nquote\"inside\r\ntail\\\r\n\r\n"));
#else
    CHECK(!strcmp(text, "space in argument\nquote\"inside\ntail\\\n\n"));
#endif
    for (unsigned i = 0; i < TP_COUNTERS; ++i)
        CHECK(isfinite(quoted.counters[i]) ? quoted.running_fraction[i] >= 0.90 : quoted.counter_errors[i] != 0);
}

static void test_compile_options(void)
{
    char* options[] = {"throughput", "generate", "--flag", "-O3", "--flag", "-O0", NULL};
    TpConfig config;
    CHECK(tp_options(6, options, &config));
    config.self_host_generated = "generated";
    for (unsigned stage = 0; stage < 3; ++stage)
    {
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            TpJob job = {0};
            job.mode = mode;
            job.stage = stage;
            strcpy(job.workload.path, "input.c");
            TpArguments args;
            tp_compile_arguments(&config, &job, "ide", "output", "metrics", &args);
            unsigned selected = 0, last_optimization = 0, selections = 0;
            for (unsigned i = 0; i < args.count; ++i)
            {
                if (!strncmp(args.values[i], "-O", 2)) last_optimization = i;
                if (!strncmp(args.values[i], "-fregister-allocator=", 21)) { selected = i; ++selections; }
            }
            CHECK(selections == 1 && selected > last_optimization &&
                  !strcmp(args.values[selected] + 21, tp_modes[mode]) && args.values[args.count] == NULL);
        }
    }
    char* forbidden[] = {"throughput", "generate", "--flag", "-fno-register-allocator", NULL};
    CHECK(!tp_options(4, forbidden, &config));
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

int main(int argc, char** argv)
{
    int result = 2;
    if (argc >= 2 && !strcmp(argv[1], "child")) result = test_child(argc, argv);
    else if (argc == 2)
    {
        char root[TP_PATH_CAP], executable[TP_PATH_CAP];
        CHECK(tp_absolute(argv[1], root) && tp_mkdirs(root) && tp_absolute(argv[0], executable));
        CHECK(tp_self_test() == 0);
        static int const expected[] = {0, 1, 0, 0, 0, 1, 2, 2, 2, 2};
        for (unsigned scenario = 0; scenario < sizeof(expected) / sizeof(expected[0]); ++scenario)
        {
            char path[TP_PATH_CAP], leaf[128];
            snprintf(leaf, sizeof(leaf), "scenario-%u", scenario);
            CHECK(tp_path(path, root, leaf) && test_bundle(path, scenario));
            CHECK(tp_compare(path) == expected[scenario]);
        }
        char tampered[TP_PATH_CAP];
        CHECK(tp_path(tampered, root, "scenario-0"));
        CHECK(test_text(tampered, "metadata.json", "changed\n"));
        CHECK(tp_compare(tampered) == 2);
        test_compile_options();
        test_sample_paths(executable, root);
        test_inputs(root);
        test_processes(executable, root);
        fprintf(stdout, "THROUGHPUT_INTEGRATION_TEST assertions=%u failures=%u\n", test_assertions, test_failures);
        result = test_failures ? 1 : 0;
    }
    else fprintf(stderr, "usage: throughput-tests OUTPUT_DIRECTORY\n");
    return result;
}
