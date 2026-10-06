/* Native tests for cpuset.h and the scale experiment (scaling.h). Topology
 * comes from fixture trees, so no assertion depends on the CI host's layout;
 * only placement uses the real permitted mask. TP_TEST_SCALE_FAULT injects a
 * defect into the fake multi-input compiler (test_scale_compiler).
 */
#ifndef BUSTER_THROUGHPUT_SCALING_TEST_H
#define BUSTER_THROUGHPUT_SCALING_TEST_H

static int test_scale_compiler(int argc, char** argv)
{
    char const* output = NULL;
    char const* metrics = NULL;
    char const* fault = getenv("TP_TEST_SCALE_FAULT");
    unsigned jobs = 0, inputs = 0, workers = 1;
    int result = 0;
    char const* sources[256];
    if (!fault) fault = "";
    for (int i = 2; i < argc && !result; ++i)
    {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
        else if (!strncmp(argv[i], "-fmetrics-out=", 14)) metrics = argv[i] + 14;
        else if (!strncmp(argv[i], "-fcompile-jobs=", 15)) result = tp_number(argv[i] + 15, &jobs) && jobs ? 0 : 9;
        else if (strstr(argv[i], ".c"))
        {
            if (inputs < 256) sources[inputs] = argv[i];
            inputs += 1;
        }
    }
    if (!output || !metrics || !inputs || inputs > 256) result = 9;
    if (jobs > 1) workers = jobs < inputs ? jobs : inputs;
#ifdef __linux__
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    unsigned pinned = sched_getaffinity(0, sizeof(affinity), &affinity) == 0 ? (unsigned)CPU_COUNT(&affinity) : 0;
    /* A placement never gives a worker less than one whole CPU. */
    if (!result && (pinned < workers || (getenv("TP_TEST_SCALE_PINNED") && pinned != (jobs > 1 ? jobs : 1)))) result = 6;
#endif
    if (!strcmp(fault, "wrong-workers") && jobs > 1) workers = 1;
    if (!result && !strcmp(fault, "sleep")) test_delay(5000);
    if (!result && !strcmp(fault, "memory"))
    {
        size_t bytes = (size_t)96 * 1024 * 1024;
        volatile unsigned char* memory = (volatile unsigned char*)malloc(bytes);
        if (memory)
        {
            for (size_t i = 0; i < bytes; i += 4096) memory[i] = (unsigned char)(i >> 12);
            free((void*)memory);
        }
    }
    unsigned broken[256], broken_count = 0;
    for (unsigned i = 0; i < inputs && !result; ++i)
    {
        char text[1 << 16];
        FILE* file = fopen(sources[i], "rb");
        size_t count = file ? fread(text, 1, sizeof(text) - 1, file) : 0;
        text[count] = 0;
        if (!file) result = 8;
        else fclose(file);
        if (strstr(text, "tp_scale_undeclared_")) broken[broken_count++] = i;
    }
    if (!result && broken_count)
    {
        int reverse = !strcmp(fault, "diagnostic-order") && jobs > 1;
        for (unsigned i = 0; i < broken_count; ++i)
            fprintf(stderr, "%s: error: use of undeclared identifier\n", sources[broken[reverse ? broken_count - 1 - i : i]]);
        result = 1;
    }
    if (!result)
    {
        FILE* report = fopen(metrics, "wb");
        int ok = report && fprintf(report, "CC_METRICS version=1 schema=buster-cc-metrics inputs=%u records=%u ok=%u rejected=0 failed=0 "
                                   "not_run=0 prebuilt=0 error=- exit_status=0 action=link target=- allocator=- compile_jobs=%u "
                                   "compilation_workers=%u intervals=%s keep_going=0\n",
                                   inputs, inputs, inputs, jobs ? jobs : 1, workers, workers > 1 ? "concurrent" : "serial") > 0;
        if (report && fclose(report) != 0) ok = 0;
        FILE* artifact = ok ? fopen(output, "wb") : NULL;
        ok = artifact && fprintf(artifact, "linked inputs=%u", inputs) > 0;
        if (ok && !strcmp(fault, "nondeterministic")) ok = fprintf(artifact, " jobs=%u", jobs) > 0;
        if (artifact && fclose(artifact) != 0) ok = 0;
        if (!ok) result = 8;
    }
    return result;
}

static void test_cpu_set_parse(void)
{
    TpCpuSet set;
    char text[TP_CPU_SET_TEXT_CAP];
    CHECK(tp_cpu_set_parse("0", &set) && set.count == 1 && tp_cpu_set_has(&set, 0));
    CHECK(tp_cpu_set_parse("0-3,8", &set) && set.count == 5 && tp_cpu_set_format(&set, text, sizeof(text)) && !strcmp(text, "0-3,8"));
    CHECK(tp_cpu_set_parse("6,2,4-5\n", &set) && set.count == 4 && tp_cpu_set_format(&set, text, sizeof(text)) && !strcmp(text, "2,4-6"));
    CHECK(tp_cpu_set_parse("1023", &set) && set.count == 1);
    static char const* const rejected[] = {"", "\n", "3-1", "1,1", "0-2,2", "1,", ",1", "a", "1-", "-1", "1024", "0-1024",
                                           "1 ", "1\n2", "1,,2", "99999999999"};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
    {
        int parsed = tp_cpu_set_parse(rejected[i], &set);
        CHECK(!parsed && set.count == 0);
        if (parsed) fprintf(stderr, "TEST note: accepted CPU list \"%s\"\n", rejected[i]);
    }
    CHECK(tp_cpu_set_parse("0-1023", &set) && set.count == TP_MAX_CPUS && tp_cpu_set_format(&set, text, sizeof(text)) && !strcmp(text, "0-1023"));
    CHECK(!tp_cpu_set_format(&set, text, 4));
    TpCpuSet workers;
    CHECK(tp_scale_workers_parse("1,2,4,7", &workers) && workers.count == 4);
    CHECK(tp_scale_workers_parse("64", &workers) && !tp_scale_workers_parse("65", &workers));
    CHECK(!tp_scale_workers_parse("0,1", &workers) && !tp_scale_workers_parse("2,2", &workers));
}

static int test_topology_cpu(char const* root, unsigned cpu, char const* siblings, char const* package, char const* core)
{
    char directory[TP_PATH_CAP];
    snprintf(directory, sizeof(directory), "%s/cpu%u/topology", root, cpu);
    int ok = tp_mkdirs(directory);
    if (ok && siblings) ok = test_text(directory, "thread_siblings_list", siblings);
    if (ok && package) ok = test_text(directory, "physical_package_id", package);
    if (ok && core) ok = test_text(directory, "core_id", core);
    return ok;
}

static void test_topology(char const* root)
{
    char flat[TP_PATH_CAP], smt[TP_PATH_CAP], broken[TP_PATH_CAP];
    CHECK(tp_path(flat, root, "topology-flat") && tp_path(smt, root, "topology-smt") && tp_path(broken, root, "topology-broken"));
    for (unsigned cpu = 0; cpu < 4; ++cpu)
    {
        char siblings[32], core[32];
        snprintf(siblings, sizeof(siblings), "%u\n", cpu);
        snprintf(core, sizeof(core), "%u\n", cpu);
        CHECK(test_topology_cpu(flat, cpu, siblings, "0\n", core));
    }
    /* Linux numbering on an 8C/16T-style part: CPU N and N+4 share a core. */
    for (unsigned cpu = 0; cpu < 8; ++cpu)
    {
        char siblings[32], core[32];
        snprintf(siblings, sizeof(siblings), "%u,%u\n", cpu % 4, cpu % 4 + 4);
        snprintf(core, sizeof(core), "%u\n", cpu % 4);
        CHECK(test_topology_cpu(smt, cpu, siblings, "0\n", core));
    }
    CHECK(test_topology_cpu(broken, 0, "0\n", "0\n", "0\n"));
    CHECK(test_topology_cpu(broken, 1, "1\n", "0\n", NULL));
    CHECK(test_topology_cpu(broken, 2, "x\n", "0\n", "2\n"));
    CHECK(test_topology_cpu(broken, 3, "0\n", "0\n", "3\n"));
    CHECK(test_topology_cpu(broken, 4, "4\n", "zero\n", "4\n"));

    TpTopology* topology = (TpTopology*)calloc(1, sizeof(TpTopology));
    TpCpuSet set, selected;
    CHECK(topology != NULL);
    if (topology)
    {
        CHECK(tp_cpu_set_parse("0-3", &set) && tp_topology_read(flat, &set, topology) == 0 &&
              topology->count == 4 && topology->physical_cores == 4);
        CHECK(tp_topology_physical_prefix(topology, 2, &selected) && selected.count == 2 &&
              tp_cpu_set_has(&selected, 0) && tp_cpu_set_has(&selected, 1));
        CHECK(!tp_topology_physical_prefix(topology, 5, &selected) && !tp_topology_physical_prefix(topology, 0, &selected));
        CHECK(tp_cpu_set_parse("0-7", &set) && tp_topology_read(smt, &set, topology) == 0 &&
              topology->count == 8 && topology->physical_cores == 4 && topology->cpus[5].core == 1 && topology->cpus[5].siblings == 2);
        CHECK(tp_topology_physical_prefix(topology, 4, &selected) && selected.count == 4 && tp_cpu_set_has(&selected, 3) &&
              !tp_cpu_set_has(&selected, 4));
        CHECK(!tp_topology_physical_prefix(topology, 5, &selected));
        /* Two siblings of one core are one physical core, never two workers. */
        CHECK(tp_cpu_set_parse("1,5", &set) && tp_topology_read(smt, &set, topology) == 0 &&
              topology->count == 2 && topology->physical_cores == 1 && !tp_topology_physical_prefix(topology, 2, &selected));
        CHECK(tp_cpu_set_parse("0", &set) && tp_topology_read(broken, &set, topology) == 0);
        CHECK(tp_cpu_set_parse("0-1", &set) && tp_topology_read(broken, &set, topology) == ENOENT && topology->count == 0);
        CHECK(tp_cpu_set_parse("2", &set) && tp_topology_read(broken, &set, topology) == EILSEQ);
        CHECK(tp_cpu_set_parse("3", &set) && tp_topology_read(broken, &set, topology) == EILSEQ);
        CHECK(tp_cpu_set_parse("4", &set) && tp_topology_read(broken, &set, topology) == EILSEQ);
        CHECK(tp_cpu_set_parse("9", &set) && tp_topology_read(broken, &set, topology) == ENOENT);
    }
    free(topology);
}

static void test_cpu_set_placement(char const* executable, char const* root)
{
    TpCpuSet allowed, set;
    int error = tp_cpu_set_allowed(&allowed);
    char log[TP_PATH_CAP];
    CHECK(tp_path(log, root, "affinity.log"));
    char* command[] = {(char*)executable, "child", "affinity-count", NULL};
#ifdef __linux__
    CHECK(error == 0 && allowed.count > 0 && tp_cpu_set_permitted(&allowed) == 0);
    unsigned first = TP_MAX_CPUS, outside = TP_MAX_CPUS;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS; ++cpu)
    {
        if (tp_cpu_set_has(&allowed, cpu) && first == TP_MAX_CPUS) first = cpu;
        if (!tp_cpu_set_has(&allowed, cpu)) outside = cpu;
    }
    memset(&set, 0, sizeof(set));
    if (outside < TP_MAX_CPUS)
    {
        tp_cpu_set_add(&set, first);
        tp_cpu_set_add(&set, outside);
        CHECK(tp_cpu_set_permitted(&set) == EINVAL);
    }
    memset(&set, 0, sizeof(set));
    tp_cpu_set_add(&set, first);
    TpProcess pinned = tp_process_cpus(command, NULL, log, 5, &set, 0);
    CHECK_CHILD_EXIT(pinned, 1, log);
    TpProcess inherited = tp_process_cpus(command, NULL, log, 5, NULL, 0);
    CHECK_CHILD_EXIT(inherited, (int)allowed.count, log);
    if (allowed.count >= 2)
    {
        unsigned second = first + 1;
        while (!tp_cpu_set_has(&allowed, second)) ++second;
        tp_cpu_set_add(&set, second);
        TpProcess pair = tp_process_cpus(command, NULL, log, 5, &set, 0);
        CHECK_CHILD_EXIT(pair, 2, log);
    }
    TpCpuSet empty = {0};
    TpProcess rejected = tp_process_cpus(command, NULL, log, 5, &empty, 0);
    CHECK(rejected.launch_stage == TP_LAUNCH_CPU && rejected.launch_error == EINVAL);
    /* A narrowed process refuses a request it can no longer satisfy. */
    if (allowed.count >= 2)
    {
        cpu_set_t original, narrowed;
        CHECK(sched_getaffinity(0, sizeof(original), &original) == 0);
        CPU_ZERO(&narrowed);
        CPU_SET(first, &narrowed);
        if (sched_setaffinity(0, sizeof(narrowed), &narrowed) == 0)
        {
            CHECK(tp_cpu_set_permitted(&allowed) == EINVAL);
            CHECK(sched_setaffinity(0, sizeof(original), &original) == 0);
        }
    }
#elif defined(_WIN32)
    CHECK(error == 0 && allowed.count > 0 && tp_cpu_set_permitted(&allowed) == 0);
    (void)set;
    (void)command;
#else
    CHECK(error == ENOSYS);
    CHECK(tp_cpu_set_parse("0", &set) && tp_cpu_set_permitted(&set) == ENOSYS);
    TpProcess unsupported = tp_process_cpus(command, NULL, log, 5, &set, 0);
    CHECK(unsupported.launch_stage == TP_LAUNCH_CPU && unsupported.launch_error == ENOSYS);
#endif
}

static void test_scale_options(void)
{
    TpConfig config;
    char* valid[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "auto", "--workers", "1,2,4,7", NULL};
    CHECK(tp_options(10, valid, &config) && config.scale_workers.count == 4 && config.scale_shape_mask == 15u &&
          config.scale_repeats == TP_SCALE_DEFAULT_REPEATS);
    char* shapes[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", "--workers", "1",
                      "--shape", "tiny", "--shape", "count", "--allow-smt", "--max-rss-mib", "512", NULL};
    CHECK(tp_options(17, shapes, &config) && config.scale_shape_mask == ((1u << TP_SCALE_TINY) | (1u << TP_SCALE_COUNT)) &&
          config.scale_allow_smt && config.scale_max_rss_mib == 512);
    char* missing_set[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--workers", "1", NULL};
    CHECK(!tp_options(8, missing_set, &config));
    char* missing_workers[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", NULL};
    CHECK(!tp_options(8, missing_workers, &config));
    char* zero_worker[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", "--workers", "0", NULL};
    CHECK(!tp_options(10, zero_worker, &config));
    char* repeats[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", "--workers", "1",
                       "--repeats", "65", NULL};
    CHECK(!tp_options(12, repeats, &config));
    char* flag[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", "--workers", "1",
                    "--flag", "-g", NULL};
    CHECK(!tp_options(12, flag, &config));
    char* shape[] = {"throughput", "scale", "--compiler", "ide", "--output", "out", "--cpu-set", "0", "--workers", "1",
                     "--shape", "huge", NULL};
    CHECK(!tp_options(12, shape, &config));
    char* run[] = {"throughput", "run", "--cpu-set", "0", "--no-guard", NULL};
    CHECK(!tp_options(5, run, &config));
}

#ifdef __linux__
static int test_file_contains(char const* directory, char const* leaf, char const* needle)
{
    char path[TP_PATH_CAP], text[1 << 16];
    FILE* file = tp_path(path, directory, leaf) ? fopen(path, "rb") : NULL;
    size_t count = file ? fread(text, 1, sizeof(text) - 1, file) : 0;
    text[count] = 0;
    if (file) fclose(file);
    return file && strstr(text, needle) != NULL;
}

typedef struct TestScaleRun
{
    char const* executable;
    char const* fault;
    char const* cpus;
    char const* workers;
    char const* topology;
    char const* shape;
    char const* timeout;
    char const* budget;
    int allow_smt, pinned;
} TestScaleRun;

static TpProcess test_scale_run(TestScaleRun options, char const* output, char const* log)
{
    char* command[40];
    unsigned argc = 0;
    command[argc++] = (char*)options.executable;
    command[argc++] = "child";
    command[argc++] = "throughput";
    command[argc++] = "scale";
    command[argc++] = "--compiler"; command[argc++] = (char*)options.executable;
    command[argc++] = "--output"; command[argc++] = (char*)output;
    command[argc++] = "--cpu-set"; command[argc++] = (char*)options.cpus;
    command[argc++] = "--workers"; command[argc++] = (char*)options.workers;
    command[argc++] = "--topology-root"; command[argc++] = (char*)options.topology;
    command[argc++] = "--profile"; command[argc++] = "smoke";
    command[argc++] = "--repeats"; command[argc++] = "3";
    command[argc++] = "--warmups"; command[argc++] = "1";
    command[argc++] = "--timeout"; command[argc++] = (char*)(options.timeout ? options.timeout : "10");
    if (options.shape) { command[argc++] = "--shape"; command[argc++] = (char*)options.shape; }
    if (options.budget) { command[argc++] = "--max-rss-mib"; command[argc++] = (char*)options.budget; }
    if (options.allow_smt) command[argc++] = "--allow-smt";
    command[argc] = NULL;
    if (options.fault) setenv("TP_TEST_SCALE_FAULT", options.fault, 1);
    else unsetenv("TP_TEST_SCALE_FAULT");
    if (options.pinned) setenv("TP_TEST_SCALE_PINNED", "1", 1);
    else unsetenv("TP_TEST_SCALE_PINNED");
    TpProcess result = tp_process(command, NULL, log, 120, -1, 0);
    unsetenv("TP_TEST_SCALE_FAULT");
    unsetenv("TP_TEST_SCALE_PINNED");
    return result;
}

static void test_scale_fault(TestScaleRun options, char const* root, char const* leaf, char const* reason)
{
    char output[TP_PATH_CAP], log[TP_PATH_CAP];
    CHECK(tp_path(output, root, leaf) && tp_path(log, root, "scale-fault.log"));
    TpProcess result = test_scale_run(options, output, log);
    CHECK_CHILD_EXIT(result, 2, log);
    CHECK(test_file_contains(output, "scaling.json", "\"status\":\"invalid\"") &&
          test_file_contains(output, "scaling.json", reason));
    char markdown[TP_PATH_CAP];
    struct stat exists;
    CHECK(tp_path(markdown, output, "scaling.md") && stat(markdown, &exists) != 0);
}
#endif

static void test_scale(char const* executable, char const* root)
{
#ifdef __linux__
    TpCpuSet allowed;
    CHECK(tp_cpu_set_allowed(&allowed) == 0);
    unsigned cpus[2] = {TP_MAX_CPUS, TP_MAX_CPUS}, found = 0;
    for (unsigned cpu = 0; cpu < TP_MAX_CPUS && found < 2; ++cpu)
        if (tp_cpu_set_has(&allowed, cpu)) cpus[found++] = cpu;
    /* Fixture topology over the REAL permitted CPUs: placement is real, the
     * claimed core layout is the test's. Separate cores, then siblings. */
    char separate[TP_PATH_CAP], siblings[TP_PATH_CAP], cpu_list[64], pair[64];
    CHECK(tp_path(separate, root, "scale-topology-separate") && tp_path(siblings, root, "scale-topology-siblings"));
    snprintf(cpu_list, sizeof(cpu_list), found == 2 ? "%u,%u" : "%u", cpus[0], cpus[1]);
    snprintf(pair, sizeof(pair), "%u,%u\n", cpus[0], cpus[1]);
    for (unsigned i = 0; i < found; ++i)
    {
        char own[32], core[32];
        snprintf(own, sizeof(own), "%u\n", cpus[i]);
        snprintf(core, sizeof(core), "%u\n", i);
        CHECK(test_topology_cpu(separate, cpus[i], own, "0\n", core));
        CHECK(test_topology_cpu(siblings, cpus[i], found == 2 ? pair : own, "0\n", "0\n"));
    }
    char const* workers = found == 2 ? "1,2" : "1";
    char output[TP_PATH_CAP], log[TP_PATH_CAP];
    CHECK(tp_path(output, root, "scale-valid") && tp_path(log, root, "scale.log"));
    TestScaleRun valid = {executable, NULL, cpu_list, workers, separate, NULL, NULL, NULL, 0, 1};
    TpProcess result = test_scale_run(valid, output, log);
    CHECK_CHILD_EXIT(result, 0, log);
    CHECK(test_file_contains(output, "scaling.json", "\"status\":\"valid\"") &&
          test_file_contains(output, "scaling.json", "\"diagnostics_identical\":true") &&
          test_file_contains(output, "scaling.md", "| default | core | 1 |") &&
          test_file_contains(output, "scaling-metadata.json", "\"smt_siblings_in_set\":0") &&
          test_file_contains(output, "scaling-metadata.json", "\"name\":\"skewed\"") &&
          test_file_contains(output, "scaling-metadata.json", "\"name\":\"tiny\"") &&
          test_file_contains(output, "commands.jsonl", found == 2 ? "\"-fcompile-jobs=2\"" : "\"-fcompile-jobs=1\""));
    if (found == 2)
    {
        CHECK(test_file_contains(output, "scaling-metadata.json", "\"name\":\"count-w2-n1\"") &&
              test_file_contains(output, "scaling-metadata.json", "\"name\":\"count-w2-n3\"") &&
              test_file_contains(output, "scaling.csv", "equal,8,2,core,") &&
              test_file_contains(output, "scaling.csv", ",2,concurrent,"));
    }
    /* A result directory is never reused. */
    result = test_scale_run(valid, output, log);
    CHECK_CHILD_EXIT(result, 2, log);

    TestScaleRun fault = valid;
    fault.shape = "equal";
    fault.pinned = 0;
    if (found == 2)
    {
        fault.fault = "wrong-workers";
        test_scale_fault(fault, root, "scale-wrong-workers", "participating workers");
        fault.fault = "nondeterministic";
        test_scale_fault(fault, root, "scale-nondeterministic", "differs from");
        fault.fault = "diagnostic-order";
        test_scale_fault(fault, root, "scale-diagnostic-order", "diagnostics at 2 workers");
        /* Two SMT siblings of one core cannot host two workers. */
        TestScaleRun refused = valid;
        refused.topology = siblings;
        CHECK(tp_path(output, root, "scale-smt-refused"));
        result = test_scale_run(refused, output, log);
        CHECK_CHILD_EXIT(result, 2, log);
        refused.workers = "1";
        refused.allow_smt = 1;
        refused.shape = "equal";
        CHECK(tp_path(output, root, "scale-smt"));
        result = test_scale_run(refused, output, log);
        CHECK_CHILD_EXIT(result, 0, log);
        CHECK(test_file_contains(output, "scaling.csv", "equal,4,2,smt,"));
    }
    fault.fault = "sleep";
    fault.timeout = "1";
    test_scale_fault(fault, root, "scale-timeout", "timeout=1");
    fault.fault = "memory";
    fault.timeout = NULL;
    fault.budget = "32";
    test_scale_fault(fault, root, "scale-memory", "exceeds the 32 MiB budget");
    TestScaleRun outside = valid;
    outside.cpus = "1023";
    CHECK(tp_path(output, root, "scale-outside"));
    if (!tp_cpu_set_has(&allowed, 1023))
    {
        result = test_scale_run(outside, output, log);
        CHECK_CHILD_EXIT(result, 2, log);
    }
#else
    char output[TP_PATH_CAP], log[TP_PATH_CAP];
    CHECK(tp_path(output, root, "scale-unsupported") && tp_path(log, root, "scale.log"));
    char* command[] = {(char*)executable, "child", "throughput", "scale", "--compiler", (char*)executable, "--output", output,
                       "--cpu-set", "auto", "--workers", "1", "--profile", "smoke", "--repeats", "1", NULL};
    /* macOS has no per-process CPU set; Windows has a permitted mask but no
     * sysfs topology. Both refuse the experiment rather than guess placement. */
    TpProcess result = tp_process(command, NULL, log, 60, -1, 0);
    CHECK_CHILD_EXIT(result, 2, log);
#endif
}

static void test_scaling(char const* executable, char const* root)
{
    test_cpu_set_parse();
    test_topology(root);
    test_cpu_set_placement(executable, root);
    test_scale_options();
    test_scale(executable, root);
}

#endif
