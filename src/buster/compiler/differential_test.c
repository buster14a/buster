// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Self-tests for differential.c. The only external dependency is clang, which builds our original fixture tool.
// All fake compiler/generator runs are marked as fixtures and count as zero source-to-machine campaign passes.
// These tests exercise the protocol, process supervision and retained evidence; they cannot qualify Buster codegen.

typedef struct DifferentialTests DifferentialTests;
struct DifferentialTests
{
    uint64_t checks;
    uint64_t failures;
};

static void differential_test_check(DifferentialTests* tests, bool condition, const char* name)
{
    ++tests->checks;
    if (!condition)
    {
        ++tests->failures;
        fprintf(stderr, "differential harness: FAIL %s\n", name);
    }
}

static DifferentialOptions differential_test_options(const char* output, const char* tool, const char* scenario, unsigned generator)
{
    DifferentialOptions options = differential_defaults();
    options.output = output;
    options.buster = tool;
    options.clang = tool;
    options.csmith = tool;
    options.yarpgen = tool;
    options.fixture = true;
    options.timeout_ms = 200;
    options.output_limit = 4096;
    options.source_limit = 4096;
    options.generator_mask = 1u << generator;
    options.revision = "original-protocol-fixture";
    options.generator_revision = "original-protocol-fixture";
    differential_add(&options.buster_args, "--fixture-buster");
    differential_add(&options.clang_args, "--fixture-clang");
    differential_add(&options.tier0_args, "--fixture-tier0");
    differential_add(&options.tier1_args, "--fixture-tier1");
    differential_add(&options.csmith_args, "--fixture-csmith");
    differential_add(&options.yarpgen_args, "--fixture-yarpgen");
    DifferentialArguments* arguments = generator == 0 ? &options.csmith_args : &options.yarpgen_args;
    differential_add(arguments, "--scenario");
    differential_add(arguments, scenario);
    // This is one argv entry. A shell implementation would expand or split it; the manifest must retain it literally.
    differential_add(arguments, "literal space ' quote \" dollar $() ; *");
    return options;
}

static void differential_test_processes(DifferentialTests* tests, const char* directory, const char* fixture)
{
    const char* const switches[] = {"--tree", "--hang-tree", "--flood", "--interrupt"};
    const char* const labels[] = {"process-tree", "process-timeout", "process-flood", "process-interrupt"};
    const uint32_t kinds[] = {0, 3, 4, 6};
    for (unsigned i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i)
    {
        DifferentialArguments arguments = {0};
        differential_add(&arguments, fixture);
        differential_add(&arguments, switches[i]);
        const DifferentialProcess result = differential_process(directory, labels[i], &arguments, 100, 256, 256, 4096);
        differential_test_check(tests, result.kind == kinds[i], labels[i]);
        differential_test_check(tests, result.cleanup_complete, "subreaper reaped complete process tree");
        differential_test_check(tests, result.elapsed_ms < 2000, "process deadline bounded");
        if (i < 2)
        {
            char path[PATH_MAX];
            char name[64];
            snprintf(name, sizeof(name), "%s.stderr", labels[i]);
            const bool joined = differential_path(path, sizeof(path), directory, name);
            FILE* file = joined ? fopen(path, "r") : 0;
            long child = 0;
            long grandchild = 0;
            const bool parsed = file && fscanf(file, "%ld %ld", &child, &grandchild) == 2 && child > 0 && grandchild > 0;
            if (file)
            {
                fclose(file);
            }
            differential_test_check(tests, parsed, "detached descendants were created");
            differential_test_check(tests, parsed && kill((pid_t)child, 0) < 0 && errno == ESRCH, "setsid child gone");
            differential_test_check(tests, parsed && kill((pid_t)grandchild, 0) < 0 && errno == ESRCH, "setsid grandchild gone");
        }
        if (i == 2)
        {
            char path[PATH_MAX];
            struct stat metadata;
            const bool joined = differential_path(path, sizeof(path), directory, "process-flood.stdout");
            differential_test_check(tests, result.truncated[0] && result.output_bytes[0] > 256, "output truncation explicit");
            differential_test_check(tests, joined && stat(path, &metadata) == 0 && metadata.st_size == 256, "retained output is exactly bounded");
        }
    }
    DifferentialArguments invalid = {0};
    differential_add(&invalid, "/no/such/buster-differential-test-tool");
    const DifferentialProcess missing = differential_process(directory, "process-exec-error", &invalid, 100, 256, 256, 4096);
    differential_test_check(tests, missing.kind == 5 && missing.error_number == ENOENT && missing.cleanup_complete, "exec failure distinguished from program exit");
    DifferentialArguments diagnostic = {0};
    differential_add(&diagnostic, fixture);
    differential_add(&diagnostic, "--stderr-flood");
    const DifferentialProcess noisy = differential_process(directory, "process-stderr-flood", &diagnostic, 100, 4096, 128, 4096);
    differential_test_check(tests, noisy.kind == 4 && noisy.truncated[1] && !noisy.truncated[0] && noisy.output_bytes[0] == 0 && noisy.output_bytes[1] > 128, "separate stderr limit");
    char path[PATH_MAX];
    struct stat metadata;
    const bool joined = differential_path(path, sizeof(path), directory, "process-stderr-flood.stderr");
    differential_test_check(tests, joined && stat(path, &metadata) == 0 && metadata.st_size == 128, "stderr is exactly bounded");
    // Exercise descriptor normalization without closing the test runner's own streams.
    int report[2] = {-1, -1};
    const bool piped = differential_pipe(report, O_CLOEXEC);
    const pid_t child = piped ? fork() : -1;
    if (child == 0)
    {
        close(report[0]);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        DifferentialArguments arguments = {0};
        differential_add(&arguments, fixture);
        differential_add(&arguments, "--descriptors");
        const DifferentialProcess checked = differential_process(directory, "process-closed-stdio", &arguments, 100, 256, 256, 4096);
        const bool sent = differential_write(report[1], &checked, sizeof(checked));
        _exit(sent ? 0 : 1);
    }
    if (report[1] >= 0)
    {
        close(report[1]);
    }
    DifferentialProcess checked = {.kind = 5};
    FILE* received = report[0] >= 0 ? fdopen(report[0], "r") : 0;
    bool received_all = received && fread(&checked, 1, sizeof(checked), received) == sizeof(checked);
    if (received)
    {
        received_all = fclose(received) == 0 && received_all;
    }
    else if (report[0] >= 0)
    {
        close(report[0]);
    }
    int status = 0;
    const bool reaped = child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    differential_test_check(tests, reaped && received_all && checked.kind == 0 && checked.output_bytes[0] == 3 && checked.cleanup_complete, "closed stdio restored and other descriptors closed");
}

static void differential_test_campaigns(DifferentialTests* tests, const char* directory, const char* fixture)
{
    const char* const scenarios[] =
    {
        "pass",
        "mismatch",
        "compile-error",
        "compile-crash",
        "compile-timeout",
        "compile-no-output",
        "run-error",
        "run-crash",
        "run-timeout",
        "run-flood",
        "empty-output",
        "generator-error",
        "generator-crash",
        "generator-timeout",
        "generator-flood",
    };
    const char* const signatures[] =
    {
        "",
        "buster-tier1:mismatch",
        "buster-tier1-compile:exit",
        "buster-tier1-compile:signal",
        "buster-tier1-compile:timeout",
        "buster-tier1-compile:no-executable",
        "buster-tier1-run:exit",
        "buster-tier1-run:signal",
        "buster-tier1-run:timeout",
        "buster-tier1-run:output-limit",
        "buster-tier1-run:empty-output",
        "generate:exit",
        "generate:signal",
        "generate:timeout",
        "generate:output-limit",
    };
    char first_directory[PATH_MAX] = {0};
    char passing_case[PATH_MAX] = {0};
    char failing_case[PATH_MAX] = {0};
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i)
    {
        DifferentialOptions options = differential_test_options(directory, fixture, scenarios[i], 0);
        DifferentialSummary summary = {0};
        const int result = differential_campaign(&options, &summary);
        differential_test_check(tests, result == (i == 0 ? 0 : 1), scenarios[i]);
        differential_test_check(tests, summary.passed == (i == 0) && summary.failed == (i != 0), "campaign accounting");
        differential_test_check(tests, strcmp(summary.signature, signatures[i]) == 0, "failure class retained");
        differential_test_check(tests, differential_contains(summary.directory, "summary.json", "\"campaign_passed\":0"), "fixtures cannot become compiler evidence");
        differential_test_check(tests, differential_contains(summary.last_case, "generate.json", "literal space ' quote \\\" dollar $() ; *"), "argv quoting preserved");
        differential_test_check(tests, differential_contains(summary.last_case, "result.json", "\"fixture\":true"), "case artifact labelled as fixture");
        if (i == 0)
        {
            snprintf(first_directory, sizeof(first_directory), "%s", summary.directory);
            snprintf(passing_case, sizeof(passing_case), "%s", summary.last_case);
        }
        if (i == 1)
        {
            snprintf(failing_case, sizeof(failing_case), "%s", summary.last_case);
        }
        if (i == 0 || i == 1)
        {
            char* storage = malloc(BUSTER_DIFFERENTIAL_CONFIG_BYTES);
            DifferentialOptions replay = differential_defaults();
            const bool loaded = storage && differential_load_config(summary.last_case, storage, BUSTER_DIFFERENTIAL_CONFIG_BYTES, &replay);
            differential_test_check(tests, loaded, "replay configuration round trip");
            if (loaded)
            {
                replay.fixture = true;
                replay.output = directory;
                replay.replay = summary.last_case;
                replay.source_directory = summary.last_case;
                replay.interesting = i == 1;
                DifferentialSummary replayed = {0};
                const int replay_result = differential_campaign(&replay, &replayed);
                differential_test_check(tests, replay_result == result && strcmp(replayed.signature, summary.signature) == 0, "replay preserves pass/failure without generation");
                differential_test_check(tests, replayed.generated == 1, "replay retained source input");
            }
            free(storage);
        }
    }
    DifferentialOptions options = differential_test_options(directory, fixture, "pass", 1);
    DifferentialSummary multi = {0};
    differential_test_check(tests, differential_campaign(&options, &multi) == 0 && multi.passed == 1, "YARPGen multifile protocol");
    differential_test_check(tests, multi.generated_by_generator[0] == 0 && multi.generated_by_generator[1] == 1 && multi.passed_by_generator[1] == 1, "per-generator counts");
    differential_test_check(tests, differential_contains(multi.last_case, "clang-o0-compile.json", "\"driver.c\",\"func.c\""), "all generated translation units compiled");
    differential_test_check(tests, differential_contains(multi.last_case, "input.json", "/init.h"), "generated header preserved");
    differential_test_check(tests, strcmp(first_directory, multi.directory) != 0, "independent campaign directory");
    options = differential_test_options(directory, fixture, "pass", 0);
    DifferentialSummary collision = {0};
    differential_test_check(tests, differential_campaign(&options, &collision) == 0 && strcmp(first_directory, collision.directory) != 0, "same seed does not overwrite earlier artifacts");
    differential_test_check(tests, differential_contains(first_directory, "summary.json", "\"passed\":1"), "earlier campaign evidence remains");
    options = differential_test_options(directory, fixture, "missing-source", 1);
    DifferentialSummary missing = {0};
    differential_test_check(tests, differential_campaign(&options, &missing) == 1 && missing.passed == 0 && strcmp(missing.signature, "generate:invalid-sources") == 0, "missing YARPGen file fails");
    options = differential_test_options(directory, fixture, "pass", 0);
    options.tier1 = false;
    DifferentialSummary tier0 = {0};
    differential_test_check(tests, differential_campaign(&options, &tier0) == 0 && tier0.passed == 1, "explicit Tier0-only campaign");
    differential_test_check(tests, differential_contains(tier0.directory, "summary.json", "\"tier1_campaign_passed\":0"), "Tier0 not reported as Tier1");
    options.fixture = false;
    DifferentialSummary substituted = {0};
    differential_test_check(tests, differential_campaign(&options, &substituted) == 2 && substituted.passed == 0, "same executable cannot masquerade as Buster and Clang");
    options.fixture = true;
    options.buster = "/no/such/buster";
    DifferentialSummary absent = {0};
    differential_test_check(tests, differential_campaign(&options, &absent) == 2 && absent.passed == 0, "missing Buster unavailable");
    differential_test_check(tests, differential_contains(absent.directory, "request.json", "/no/such/buster"), "unavailable request retained");
    options = differential_test_options(directory, fixture, "pass", 0);
    options.generate_only = true;
    DifferentialSummary generation = {0};
    differential_test_check(tests, differential_campaign(&options, &generation) == 2 && generation.generated == 1 && generation.passed == 0, "generation-only is not a passing compiler campaign");
    options = differential_test_options(directory, fixture, "pass", 0);
    options.tier1_args.count = 0;
    DifferentialSummary unspecified = {0};
    differential_test_check(tests, differential_campaign(&options, &unspecified) == 2 && unspecified.passed == 0, "missing tier selection unavailable");
    // A reducer candidate must preserve the original failure, not merely execute the original unreduced sources.
    char* storage = malloc(BUSTER_DIFFERENTIAL_CONFIG_BYTES);
    DifferentialOptions candidate = differential_defaults();
    const bool loaded = storage && differential_load_config(failing_case, storage, BUSTER_DIFFERENTIAL_CONFIG_BYTES, &candidate);
    differential_test_check(tests, loaded, "reduction loads original configuration");
    if (loaded)
    {
        candidate.fixture = true;
        candidate.interesting = true;
        candidate.replay = failing_case;
        candidate.source_directory = passing_case;
        candidate.output = directory;
        DifferentialSummary reduced = {0};
        differential_test_check(tests, differential_campaign(&candidate, &reduced) == 0 && reduced.signature[0] == 0, "reduction reads candidate sources from supplied directory");
    }
    free(storage);
}

static void differential_test_parsing(DifferentialTests* tests)
{
    const char* const numbers[] = {"0", "1", "18446744073709551615", "18446744073709551616", "-1", "", "12x", "+1"};
    const bool accepted[] = {true, true, true, false, false, false, false, false};
    for (unsigned i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i)
    {
        uint64_t value = 0;
        differential_test_check(tests, differential_unsigned(numbers[i], UINT64_MAX, &value) == accepted[i], "bounded unsigned option parser");
    }
    char* seed_zero[] = {"--seed", "0"};
    char* overflow[] = {"--seed", "18446744073709551615", "--count", "2"};
    char* unknown[] = {"--unknown", "x"};
    char* missing[] = {"--count"};
    char* large[] = {"--output-limit", "67108865"};
    char* good[] = {"--seed", "18446744073709551615", "--count", "1", "--tiers", "0"};
    char** const cases[] = {seed_zero, overflow, unknown, missing, large, good};
    const int counts[] = {2, 4, 2, 1, 2, 6};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        DifferentialOptions options = differential_defaults();
        differential_test_check(tests, differential_options(counts[i], cases[i], &options) == (i == 5), "campaign option validation");
    }
    DifferentialArguments arguments = {0};
    bool valid = true;
    for (unsigned i = 0; i < BUSTER_DIFFERENTIAL_ARGS - 1; ++i)
    {
        valid = differential_add(&arguments, "argument") && valid;
    }
    differential_test_check(tests, valid && !differential_add(&arguments, "overflow") && arguments.values[arguments.count] == 0, "argv capacity and terminator");
}

static int run_differential_tests(int argc, char** argv)
{
    const char* clang = "clang";
    const char* source = "tests/differential/tool.c";
    bool valid = true;
    for (int i = 0; valid && i < argc; ++i)
    {
        if (i + 1 < argc && strcmp(argv[i], "--clang") == 0)
        {
            clang = argv[++i];
        }
        else if (i + 1 < argc && strcmp(argv[i], "--fixture-source") == 0)
        {
            source = argv[++i];
        }
        else
        {
            valid = false;
        }
    }
    char directory[] = "/tmp/buster differential '$;-XXXXXX";
    char compiler[PATH_MAX];
    char fixture_source[PATH_MAX];
    char fixture[PATH_MAX];
    valid = valid && differential_resolve(clang, compiler) && realpath(source, fixture_source) != 0 && mkdtemp(directory) != 0 && differential_path(fixture, sizeof(fixture), directory, "fixture tool '$;.exe");
    DifferentialTests tests = {0};
    if (valid)
    {
        DifferentialArguments arguments = {0};
        differential_add(&arguments, compiler);
        differential_add(&arguments, "-std=c11");
        differential_add(&arguments, "-D_GNU_SOURCE");
        differential_add(&arguments, "-O1");
        differential_add(&arguments, "-Wall");
        differential_add(&arguments, "-Wextra");
        differential_add(&arguments, "-Werror");
        differential_add(&arguments, fixture_source);
        differential_add(&arguments, "-o");
        differential_add(&arguments, fixture);
        const DifferentialProcess built = differential_process(directory, "fixture-build", &arguments, 30000, 65536, 65536, BUSTER_DIFFERENTIAL_MAX_BYTES);
        valid = built.kind == 0;
        differential_test_check(&tests, valid, "build original C fixture");
    }
    if (valid)
    {
        differential_test_parsing(&tests);
        differential_test_processes(&tests, directory, fixture);
        differential_test_campaigns(&tests, directory, fixture);
    }
    else
    {
        fprintf(stderr, "test-differential-harness requires clang and tests/differential/tool.c; [--clang PATH] [--fixture-source PATH]\n");
    }
    fprintf(stderr, "differential harness: %llu checks, %llu failures; compiler campaigns=0; artifacts=%s\n", (unsigned long long)tests.checks, (unsigned long long)tests.failures, directory);
    const int result = valid ? tests.failures ? 1 : 0 : 2;
    return result;
}
