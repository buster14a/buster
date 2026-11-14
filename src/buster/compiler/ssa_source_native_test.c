// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #134's native comparison for test-ssa-source, included by ssa_source_test.c. The driver compiles each C program
// strictly through Tier 1, by both paths, with a complete function-count report. Each executable then runs with the
// interpreter's main arguments and empty environment. Compilation keeps the caller's working directory, so relative
// source, include and predefines paths retain their meaning; execution gets a fresh directory per program and path.
// This is test supervision outside the compiler's hot paths, with no SIMD kernel. The two compiled paths exercise
// the production scalar/vector kernels. ssa_source_native_execute distinguishes a real exit(127) from failed exec.

typedef enum SsaSourceNativeStatus
{
    SSA_SOURCE_NATIVE_SETUP,
    SSA_SOURCE_NATIVE_BUILD,
    SSA_SOURCE_NATIVE_COVERAGE,
    SSA_SOURCE_NATIVE_EXECUTION,
    SSA_SOURCE_NATIVE_MISMATCH,
    SSA_SOURCE_NATIVE_OK,
} SsaSourceNativeStatus;

typedef struct SsaSourceNativeRun SsaSourceNativeRun;
struct SsaSourceNativeRun
{
    char* output;
    uint64_t length;
    int status;
    bool launched;
    bool timed_out;
};

typedef struct SsaSourceNativeResult SsaSourceNativeResult;
struct SsaSourceNativeResult
{
    SsaSourceNativeStatus status;
    uint32_t functions;
    int build_status;
    int run_status;
    bool timed_out;
    bool launched;
};

// A report is accepted only as one complete line, with bounded decimal fields, all expected functions in Tier 1 and
// no fallback. Missing, partial, overflowing and repeated reports cannot establish native acceptance.
static bool ssa_source_native_report(const char* text, uint32_t expected, uint32_t* functions)
{
    static const char* const names[] = { "tier1\tfunctions=", "\ttier1=", "\tfallbacks=" };
    const char* line = text != NULL ? strstr(text, names[0]) : NULL;
    const char* end = line != NULL ? strchr(line, '\n') : NULL;
    bool valid = expected != 0 && line != NULL && end != NULL && (line == text || line[-1] == '\n') && strstr(end, names[0]) == NULL;
    uint32_t values[3] = { 0 };
    for (uint32_t field = 0; valid && field < BUSTER_ARRAY_LENGTH(names); field += 1)
    {
        const char* found = strstr(line, names[field]);
        const char* number = found != NULL ? found + strlen(names[field]) : NULL;
        const char* repeated = number != NULL ? strstr(number, field == 0 ? "\tfunctions=" : names[field]) : NULL;
        valid = number != NULL && number < end && *number >= '0' && *number <= '9' && (repeated == NULL || repeated >= end);
        uint64_t value = 0;
        const char* at = number;
        while (valid && at < end && *at >= '0' && *at <= '9')
        {
            const uint32_t digit = (uint32_t)(*at - '0');
            valid = value <= (UINT32_MAX - digit) / 10;
            value = valid ? value * 10 + digit : value;
            at += 1;
        }
        valid = valid && (at == end || *at == '\t');
        values[field] = valid ? (uint32_t)value : 0;
    }
    *functions = valid ? values[0] : 0;
    return valid && values[0] == expected && values[1] == expected && values[2] == 0;
}

// A parent-enforced deadline also stops a program that calls alarm(0). The close-on-exec pipe reports setup/exec
// failure separately from the program's exit status; EOF is successful exec, so a genuine return 127 remains valid.
// Both streams are captured together, as in the interpreter. The parent always reaps the child and kills its group.
static SsaSourceNativeRun ssa_source_native_execute(const char* executable, const char* directory, const char* output, uint32_t seconds)
{
    SsaSourceNativeRun result = { .status = -1 };
    int launch[2] = { -1, -1 };
    const bool piped = syscall(SYS_pipe2, launch, O_CLOEXEC) == 0;
    const bool ready = piped && launch[0] > STDERR_FILENO && launch[1] > STDERR_FILENO;
    fflush(stdout);
    fflush(stderr);
    const pid_t child = ready ? fork() : -1;
    if (child == 0)
    {
        close(launch[0]);
        const int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int capture = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        const bool prepared = setpgid(0, 0) == 0 && input > STDERR_FILENO && capture > STDERR_FILENO && chdir(directory) == 0 &&
                              dup2(input, STDIN_FILENO) == STDIN_FILENO && dup2(capture, STDOUT_FILENO) == STDOUT_FILENO && dup2(capture, STDERR_FILENO) == STDERR_FILENO;
        if (prepared)
        {
            char* const arguments[] = { "program", NULL };
            char* const environment[] = { NULL };
            execve(executable, arguments, environment);
        }
        const uint8_t failed = 1;
        ssize_t sent;
        do
        {
            sent = write(launch[1], &failed, sizeof(failed));
        }
        while (sent < 0 && errno == EINTR);
        if (sent != (ssize_t)sizeof(failed))
        {
            ssa_source_child_failure();
        }
        _exit(127);
    }
    if (launch[1] >= 0)
    {
        close(launch[1]);
    }
    if (child > 0)
    {
        const uint64_t deadline = c_testsuite_now() + (uint64_t)seconds * 1000000000;
        bool ended = false;
        bool wait_failed = false;
        while (!ended && !wait_failed && !result.timed_out)
        {
            siginfo_t information = { 0 };
            const int observed = waitid(P_PID, (id_t)child, &information, WEXITED | WNOHANG | WNOWAIT);
            ended = observed == 0 && information.si_pid == child;
            wait_failed = observed < 0 && errno != EINTR;
            result.timed_out = !ended && !wait_failed && c_testsuite_now() >= deadline;
            if (!ended && !wait_failed && !result.timed_out)
            {
                const struct timespec pause = { .tv_nsec = 1000000 };
                nanosleep(&pause, NULL);
            }
        }
        // Keep the leader unreaped until its group's cleanup, so its pid cannot be reused for another process group.
        kill(-child, SIGKILL);
        if (!ended)
        {
            kill(child, SIGKILL);
        }
        pid_t waited = 0;
        while (waited == 0)
        {
            waited = waitpid(child, &result.status, 0);
            waited = waited < 0 && errno == EINTR ? 0 : waited;
        }
        result.status = waited == child && !wait_failed ? result.status : -1;
        uint8_t failed = 0;
        ssize_t received;
        do
        {
            received = read(launch[0], &failed, sizeof(failed));
        }
        while (received < 0 && errno == EINTR);
        result.launched = waited == child && received == 0;
        result.output = c_testsuite_read(output, &result.length);
    }
    if (launch[0] >= 0)
    {
        close(launch[0]);
    }
    return result;
}

static bool ssa_source_native_finished(const SsaSourceNativeRun* run)
{
    return run->launched && !run->timed_out && run->status >= 0 && WIFEXITED(run->status) && run->output != NULL;
}

static bool ssa_source_native_matches(const SsaSourceNativeRun* run, const SsaSourceExpected* expected)
{
    return ssa_source_native_finished(run) && run->status == expected->status && run->length == expected->length &&
           memcmp(run->output, expected->output, run->length) == 0;
}

// Builds and runs one path, retaining all files for the caller to remove only after every form passed. The compile
// timeout is independent of the program's limit: a one-second timeout control must still finish a sanitized build.
static bool ssa_source_native_one(const char* source, char** options, uint32_t option_count, const char* directory, bool scalar, const SsaSourceExpected* expected,
                                  uint32_t functions, const SsaSourceLimits* limits, SsaSourceNativeResult* result)
{
    *result = (SsaSourceNativeResult){ .status = SSA_SOURCE_NATIVE_SETUP, .build_status = -1, .run_status = -1 };
    char executable[PATH_MAX];
    char log[PATH_MAX];
    char output[PATH_MAX];
    const int executable_length = snprintf(executable, sizeof(executable), "%s/program", directory);
    const int log_length = snprintf(log, sizeof(log), "%s/compile.log", directory);
    const int output_length = snprintf(output, sizeof(output), "%s/output", directory);
    char** arguments = calloc((size_t)option_count + 16, sizeof(char*));
    bool ready = arguments != NULL && limits->compiler != NULL && limits->directory != NULL && executable_length >= 0 &&
                 (size_t)executable_length < sizeof(executable) && log_length >= 0 && (size_t)log_length < sizeof(log) && output_length >= 0 &&
                 (size_t)output_length < sizeof(output) && mkdir(directory, 0755) == 0;
    uint32_t count = 0;
    if (ready)
    {
        arguments[count++] = (char*)limits->compiler;
        for (uint32_t i = 0; i < option_count; i += 1)
        {
            // A caller's --scalar may select preparation's oracle, but these builds must still exercise both paths.
            if (strcmp(options[i], "--scalar") != 0)
            {
                arguments[count++] = options[i];
                if (pp_option_takes_value(options[i]) && i + 1 < option_count)
                {
                    arguments[count++] = options[++i];
                }
            }
        }
        arguments[count++] = "--tier1";
        arguments[count++] = "--tier1-strict";
        arguments[count++] = "--tier1-report";
        arguments[count++] = "--verify";
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count++] = (char*)source;
        arguments[count++] = "-o";
        arguments[count++] = executable;
        arguments[count++] = "-lm";
        arguments[count] = NULL;
        result->status = SSA_SOURCE_NATIVE_BUILD;
        result->build_status = c_testsuite_run(arguments, limits->directory, log, BUSTER_C_TESTSUITE_COMPILE_SECONDS, &result->timed_out);
        ready = !result->timed_out && result->build_status >= 0 && WIFEXITED(result->build_status) && WEXITSTATUS(result->build_status) == 0;
    }
    uint64_t log_bytes = 0;
    char* text = result->status == SSA_SOURCE_NATIVE_BUILD ? c_testsuite_read(log, &log_bytes) : NULL;
    if (ready)
    {
        result->status = SSA_SOURCE_NATIVE_COVERAGE;
        ready = ssa_source_native_report(text, functions, &result->functions);
    }
    if (!ready && text != NULL)
    {
        // Hosted jobs retain their output even when the temporary directory disappears. Keep a bounded diagnostic
        // beside the failing source/path instead of reporting only the compiler's nonzero status.
        const uint64_t shown = log_bytes < 16384 ? log_bytes : 16384;
        fprintf(stderr, "test-ssa-source: native=%s compiler log (%s):\n", scalar ? "scalar" : "vector", log);
        fwrite(text, 1, shown, stderr);
        if (shown != 0 && text[shown - 1] != '\n')
        {
            fputc('\n', stderr);
        }
        if (shown < log_bytes)
        {
            fprintf(stderr, "test-ssa-source: compiler log truncated after %" PRIu64 " of %" PRIu64 " bytes\n", shown, log_bytes);
        }
    }
    free(text);
    SsaSourceNativeRun run = { .status = -1 };
    if (ready)
    {
        result->status = SSA_SOURCE_NATIVE_EXECUTION;
        run = ssa_source_native_execute(executable, directory, output, limits->timeout);
        result->run_status = run.status;
        result->timed_out = run.timed_out;
        result->launched = run.launched;
        ready = ssa_source_native_finished(&run);
    }
    if (ready)
    {
        result->status = SSA_SOURCE_NATIVE_MISMATCH;
        ready = ssa_source_native_matches(&run, expected);
    }
    result->status = ready ? SSA_SOURCE_NATIVE_OK : result->status;
    static const char* const stages[] = { "setup", "compile", "coverage", "execution", "output/status", "passed" };
    printf("test-ssa-source: file=%s native=%s %s functions=%u/%u build_status=%d run_status=%d launched=%u timed_out=%u (%s)\n", source,
           scalar ? "scalar" : "vector", stages[result->status], result->functions, functions, result->build_status, result->run_status,
           (uint32_t)result->launched, (uint32_t)result->timed_out, directory);
    free(run.output);
    free(arguments);
    return ready;
}

// Whether the two paths' executables, each `program` in its directory, are the same bytes (#1395, #2606): the same
// source and options give the same Tier 1 program on any compiler host. A difference prints its first offset.
static bool ssa_source_native_identical(const char* source, const char* vector_directory, const char* scalar_directory)
{
    char paths[2][PATH_MAX];
    const int vector_length = snprintf(paths[0], sizeof(paths[0]), "%s/program", vector_directory);
    const int scalar_length = snprintf(paths[1], sizeof(paths[1]), "%s/program", scalar_directory);
    const bool named = vector_length >= 0 && (size_t)vector_length < sizeof(paths[0]) && scalar_length >= 0 && (size_t)scalar_length < sizeof(paths[1]);
    uint64_t lengths[2] = { 0, 0 };
    char* bytes[2] = { named ? c_testsuite_read(paths[0], &lengths[0]) : NULL, named ? c_testsuite_read(paths[1], &lengths[1]) : NULL };
    uint64_t at = 0;
    while (bytes[0] != NULL && bytes[1] != NULL && at < lengths[0] && at < lengths[1] && bytes[0][at] == bytes[1][at])
    {
        at += 1;
    }
    const bool same = bytes[0] != NULL && bytes[1] != NULL && lengths[0] == lengths[1] && at == lengths[0];
    if (!same)
    {
        printf("test-ssa-source: file=%s native executables differ at byte %" PRIu64 " (sizes %" PRIu64 "/%" PRIu64 ", vector %s, scalar %s)\n", source, at, lengths[0], lengths[1],
               bytes[0] != NULL ? "read" : "unreadable", bytes[1] != NULL ? "read" : "unreadable");
    }
    free(bytes[0]);
    free(bytes[1]);
    return same;
}

static bool ssa_source_native_write(const char* path, const char* text)
{
    FILE* file = fopen(path, "w");
    const size_t length = strlen(text);
    bool written = file != NULL && fwrite(text, 1, length, file) == length;
    written = file != NULL && fclose(file) == 0 && written;
    return written;
}

// Real driver and process controls use the same path as a source comparison. Interpreter-only step and record
// controls remain in ssa_source_execution_tests; native code has neither an interpreter step count nor its record.
static bool ssa_source_native_tests(const char* work, const SsaSourceLimits* limits)
{
    typedef struct SsaSourceNativeControl SsaSourceNativeControl;
    struct SsaSourceNativeControl
    {
        const char* name;
        const char* source;
        const char* output;
        uint32_t functions;
        int exit_status;
        SsaSourceNativeStatus status;
        bool timed_out;
        bool refused;
    };
    static const SsaSourceNativeControl controls[] = {
        {
            .name = "relative-paths-and-streams",
            .source = "#include <value.h>\nextern long write(int, const void *, unsigned long);\n"
                      "int main(void) { volatile int n = 6; int sum = 0; for (int i = 0; i < n; ++i) sum += (i & 1) ? i : i + 1;\n"
                      "write(1, \"native\\n\", 7); write(2, \"error\\n\", 6); return sum == 18 && SSA_SOURCE_VALUE + SSA_SOURCE_FORCED == 22 ? 7 : 8; }\n",
            .output = "native\nerror\n",
            .functions = 1,
            .exit_status = 7,
            .status = SSA_SOURCE_NATIVE_OK,
        },
        {
            .name = "return-127",
            .source = "int main(void) { return 127; }\n",
            .output = "",
            .functions = 1,
            .exit_status = 127,
            .status = SSA_SOURCE_NATIVE_OK,
        },
        {
            .name = "exit-255",
            .source = "extern void exit(int); int main(void) { exit(255); return 0; }\n",
            .output = "",
            .functions = 1,
            .exit_status = 255,
            .status = SSA_SOURCE_NATIVE_OK,
        },
        {
            .name = "parent-timeout",
            .source = "extern unsigned alarm(unsigned); int main(void) { alarm(0); volatile unsigned x = 0; for (;;) ++x; }\n",
            .output = "",
            .functions = 1,
            .status = SSA_SOURCE_NATIVE_EXECUTION,
            .timed_out = true,
        },
        {
            .name = "compile-failure",
            .source = "int main(void) { return missing; }\n",
            .output = "",
            .functions = 1,
            .status = SSA_SOURCE_NATIVE_BUILD,
        },
        {
            .name = "strict-refusal",
            .source = "long next(long x) { return x + 1; } long tail(long x) { __attribute__((musttail)) return next(x); } int main(void) { return tail(1) != 2; }\n",
            .output = "",
            .functions = 3,
            .status = SSA_SOURCE_NATIVE_BUILD,
            .refused = true,
        },
        {
            .name = "output-mismatch",
            .source = "extern long write(int, const void *, unsigned long); int main(void) { write(1, \"actual\\n\", 7); return 0; }\n",
            .output = "wanted\n",
            .functions = 1,
            .status = SSA_SOURCE_NATIVE_MISMATCH,
        },
        {
            .name = "status-mismatch",
            .source = "int main(void) { return 1; }\n",
            .output = "",
            .functions = 1,
            .status = SSA_SOURCE_NATIVE_MISMATCH,
        },
        {
            .name = "coverage-shortfall",
            .source = "int main(void) { return 0; }\n",
            .output = "",
            .functions = 2,
            .status = SSA_SOURCE_NATIVE_COVERAGE,
        },
    };
    bool passed = true;
    for (uint32_t control = 0; control < BUSTER_ARRAY_LENGTH(controls); control += 1)
    {
        const SsaSourceNativeControl* test = &controls[control];
        char directory[PATH_MAX];
        char source[PATH_MAX];
        const int directory_length = snprintf(directory, sizeof(directory), "%s/native control %u", work, control);
        const int source_length = snprintf(source, sizeof(source), "%s/program with space.c", directory);
        bool made = directory_length >= 0 && (size_t)directory_length < sizeof(directory) && source_length >= 0 && (size_t)source_length < sizeof(source) &&
                    mkdir(directory, 0755) == 0 && ssa_source_native_write(source, test->source);
        char* options[] = { "-I", "include space", "-include", "forced.h" };
        if (made && control == 0)
        {
            char include[PATH_MAX];
            char header[PATH_MAX];
            char forced[PATH_MAX];
            const int include_length = snprintf(include, sizeof(include), "%s/include space", directory);
            const int header_length = snprintf(header, sizeof(header), "%s/value.h", include);
            const int forced_length = snprintf(forced, sizeof(forced), "%s/forced.h", directory);
            made = include_length >= 0 && (size_t)include_length < sizeof(include) && header_length >= 0 && (size_t)header_length < sizeof(header) &&
                   forced_length >= 0 && (size_t)forced_length < sizeof(forced) && mkdir(include, 0755) == 0 &&
                   ssa_source_native_write(header, "#define SSA_SOURCE_VALUE 17\n") && ssa_source_native_write(forced, "#define SSA_SOURCE_FORCED 5\n");
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char run[PATH_MAX];
            const int run_length = snprintf(run, sizeof(run), "%s/%s", directory, path == 0 ? "vector" : "scalar");
            SsaSourceLimits own = *limits;
            own.directory = directory;
            own.timeout = test->timed_out ? 1 : 10;
            const SsaSourceExpected expected = { .output = test->output, .length = strlen(test->output), .status = test->exit_status << 8 };
            SsaSourceNativeResult result = { .status = SSA_SOURCE_NATIVE_SETUP };
            const bool ready = made && run_length >= 0 && (size_t)run_length < sizeof(run);
            const bool matched = ready && ssa_source_native_one("program with space.c", control == 0 ? options : NULL,
                                                               control == 0 ? BUSTER_ARRAY_LENGTH(options) : 0, run, path != 0, &expected, test->functions, &own, &result);
            bool checked = ready && matched == (test->status == SSA_SOURCE_NATIVE_OK) && result.status == test->status && result.timed_out == test->timed_out;
            if (test->timed_out)
            {
                checked = checked && result.launched && result.run_status >= 0 && WIFSIGNALED(result.run_status) && WTERMSIG(result.run_status) == SIGKILL;
            }
            if (test->status == SSA_SOURCE_NATIVE_BUILD)
            {
                checked = checked && result.build_status >= 0 && WIFEXITED(result.build_status) && WEXITSTATUS(result.build_status) != 0 && result.run_status == -1;
            }
            if (test->refused && ready)
            {
                char log[PATH_MAX];
                const int log_length = snprintf(log, sizeof(log), "%s/compile.log", run);
                uint64_t length = 0;
                char* text = log_length >= 0 && (size_t)log_length < sizeof(log) ? c_testsuite_read(log, &length) : NULL;
                checked = checked && text != NULL && strstr(text, "strict Tier 1 qualification failed:") != NULL && strstr(text, "Tier 0 fallback forbidden") != NULL;
                free(text);
            }
            printf("test-ssa-source: native-self-test=%s path=%s %s\n", test->name, path == 0 ? "vector" : "scalar", checked ? "passed" : "FAILED");
            passed = checked && passed;
        }
    }
    char missing[PATH_MAX];
    char output[PATH_MAX];
    const int missing_length = snprintf(missing, sizeof(missing), "%s/no-such-executable", work);
    const int output_length = snprintf(output, sizeof(output), "%s/failed-launch.out", work);
    const bool named = missing_length >= 0 && (size_t)missing_length < sizeof(missing) && output_length >= 0 && (size_t)output_length < sizeof(output);
    SsaSourceNativeRun absent = named ? ssa_source_native_execute(missing, work, output, 10) : (SsaSourceNativeRun){ .status = -1 };
    const SsaSourceExpected exit127 = { .output = "", .status = 127 << 8 };
    const bool rejected = named && !absent.launched && !absent.timed_out && absent.status >= 0 && WIFEXITED(absent.status) && WEXITSTATUS(absent.status) == 127 &&
                          !ssa_source_native_matches(&absent, &exit127);
    printf("test-ssa-source: native-self-test=failed-exec-is-not-exit-127 %s\n", rejected ? "passed" : "FAILED");
    passed = rejected && passed;
    free(absent.output);
    static const char* const reports[] = {
        "tier1\tfunctions=2\ttier1=2\tfallbacks=0\n",
        "",
        "tier1\tfunctions=2\ttier1=2\n",
        "tier1\tfunctions=2\ttier1=1\tfallbacks=1\n",
        "tier1\tfunctions=2\ttier1=2\tfallbacks=0",
        "tier1\tfunctions=2\ttier1=2\tfallbacks=0\ntier1\tfunctions=2\ttier1=2\tfallbacks=0\n",
        "tier1\tfunctions=2\ttier1=2\tfallbacks=0\tfallbacks=1\n",
        "tier1\tfunctions=2\tfunctions=1\ttier1=2\tfallbacks=0\n",
        "tier1\tfunctions=4294967298\ttier1=2\tfallbacks=0\n",
        "tier1\tfunctions=-2\ttier1=2\tfallbacks=0\n",
        "tier1\tfunctions=0\ttier1=0\tfallbacks=0\n",
    };
    for (uint32_t report = 0; report < BUSTER_ARRAY_LENGTH(reports); report += 1)
    {
        uint32_t functions = 0;
        const bool accepted = ssa_source_native_report(reports[report], 2, &functions);
        const bool checked = accepted == (report == 0);
        printf("test-ssa-source: native-self-test=report-%u %s\n", report, checked ? "passed" : "FAILED");
        passed = checked && passed;
    }
    return passed;
}
