// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Private self-checks included by their support root, at its original definition position.

static void test_tool_check(bool condition, const char* detail, uint32_t* failures, uint32_t* checks)
{
    if (!condition)
    {
        printf("test tools: %s\n", detail);
    }
    *checks += 1;
    *failures += !condition;
}

// The skip line `tool` and `section` must produce, compared whole.
static void test_tool_check_skip(const TestTool* tool, const char* section, const char* expected, uint32_t* failures, uint32_t* checks)
{
    char* text = 0;
    size_t length = 0;
    FILE* stream = open_memstream(&text, &length);
    bool same = stream != 0;
    if (stream != 0)
    {
        test_tool_skip_to(stream, tool, section);
        same = fclose(stream) == 0 && text != 0 && strcmp(text, expected) == 0;
    }
    if (!same)
    {
        printf("test tools: \"%s\" printed \"%s\", not \"%s\"\n", section, text != 0 ? text : "", expected);
    }
    *checks += 1;
    *failures += !same;
    free(text);
}

// A usage error for the test-tool command's unexpected arguments. `errors` is where it goes (stderr for the command),
// so that the helper's test can compare it; it doesn't call the helper's checks, which keeps the command's call graph
// acyclic for the repository's check.
static int test_tool_refuse(FILE* errors, int count, char** arguments)
{
    if (count != 0 && errors != 0)
    {
        fprintf(errors, "test-tool: unexpected argument '%s'\nusage: test-tool\n", arguments[0]);
    }
    return 1;
}

// The test-tool command's arguments: the helper's checks with none, and a usage error for any other. `errors` is where
// the error goes (stderr for the command), so that the helper's test can compare it.
static int test_tool_command_to(FILE* errors, int count, char** arguments)
{
    return count == 0 ? (test_tool_test() == 0 ? 0 : 1) : test_tool_refuse(errors, count, arguments);
}

static int test_tool_command(int count, char** arguments)
{
    return test_tool_command_to(stderr, count, arguments);
}

static uint32_t test_tool_test(void)
{
    uint32_t failures = 0;
    uint32_t checks = 0;
    uint32_t version[BUSTER_TEST_TOOL_COMPONENTS] = { 0 };
    uint32_t count = 0;
    bool parsed = test_tool_parse_version("gcc (GCC) 16.2.1 20260101", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 16 && version[1] == 2 && version[2] == 1, "gcc 16.2.1 parses", &failures, &checks);
    parsed = test_tool_parse_version("GNU assembler (GNU Binutils for Ubuntu) 2.42", version, &count);
    test_tool_check(parsed && count == 2 && version[0] == 2 && version[1] == 42, "as 2.42 parses", &failures, &checks);
    parsed = test_tool_parse_version("perf version 6.8.12-1-generic", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 6 && version[1] == 8 && version[2] == 12, "perf 6.8.12 parses", &failures, &checks);
    parsed = test_tool_parse_version("x86_64-linux-gnu-gcc (GCC) 16.2.1", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 16 && version[1] == 2 && version[2] == 1, "a target-prefixed gcc parses its version, not the target's digits", &failures, &checks);
    parsed = test_tool_parse_version("x86_64-conda-linux-gnu-gcc (conda-forge gcc 9.4.0-1) 9.4.0", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 9 && version[1] == 4 && version[2] == 0, "a digit-bearing package name doesn't hide the version", &failures, &checks);
    parsed = test_tool_parse_version("gcc (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0", version, &count);
    test_tool_check(parsed && count == 3 && version[0] == 13 && version[1] == 3 && version[2] == 0, "a distribution's package doesn't hide the version", &failures, &checks);
    parsed = test_tool_parse_version("no version here", version, &count);
    test_tool_check(!parsed, "text without a version doesn't parse", &failures, &checks);
    const uint32_t sixteen[] = { 16 };
    const uint32_t sixteen_zeros[] = { 16, 0, 0 };
    const uint32_t newer[] = { 16, 2, 1 };
    const uint32_t older[] = { 13, 3, 0 };
    const uint32_t as_old[] = { 2, 42 };
    const uint32_t as_minimum[] = { 2, 47 };
    test_tool_check(test_tool_compare(sixteen_zeros, 3, sixteen, 1) == 0, "16.0.0 equals 16", &failures, &checks);
    test_tool_check(test_tool_compare(newer, 3, sixteen, 1) > 0, "16.2.1 is newer than 16", &failures, &checks);
    test_tool_check(test_tool_compare(older, 3, sixteen, 1) < 0, "13.3.0 is older than 16", &failures, &checks);
    test_tool_check(test_tool_compare(as_old, 2, as_minimum, 2) < 0, "2.42 is older than 2.47", &failures, &checks);
    const char* temporary = oracle_temporary_directory();
    char path[4200];
    snprintf(path, sizeof(path), "%s/buster-test-tool-XXXXXX", temporary);
    const int descriptor = mkstemp(path);
    const char script[] = "#!/bin/sh\nprintf 'fake gcc 16.2.1\\n'\n";
    bool written = descriptor >= 0 && write(descriptor, script, sizeof(script) - 1) == (ssize_t)(sizeof(script) - 1) && fchmod(descriptor, 0755) == 0;
    if (descriptor >= 0)
    {
        written = close(descriptor) == 0 && written;
    }
    test_tool_check(written, "the fake tool is written", &failures, &checks);
    char unreadable_path[4200];
    snprintf(unreadable_path, sizeof(unreadable_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int unreadable_descriptor = mkstemp(unreadable_path);
    const char quiet_script[] = "#!/bin/sh\nexit 0\n";
    bool quiet_written = unreadable_descriptor >= 0 && write(unreadable_descriptor, quiet_script, sizeof(quiet_script) - 1) == (ssize_t)(sizeof(quiet_script) - 1) &&
                         fchmod(unreadable_descriptor, 0755) == 0;
    if (unreadable_descriptor >= 0)
    {
        quiet_written = close(unreadable_descriptor) == 0 && quiet_written;
    }
    test_tool_check(quiet_written, "the quiet tool is written", &failures, &checks);
    char hanging_path[4200];
    snprintf(hanging_path, sizeof(hanging_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int hanging_descriptor = mkstemp(hanging_path);
    const char hanging_script[] = "#!/bin/sh\nexec /bin/sleep 5\n";
    bool hanging_written = hanging_descriptor >= 0 && write(hanging_descriptor, hanging_script, sizeof(hanging_script) - 1) == (ssize_t)(sizeof(hanging_script) - 1) &&
                           fchmod(hanging_descriptor, 0755) == 0;
    if (hanging_descriptor >= 0)
    {
        hanging_written = close(hanging_descriptor) == 0 && hanging_written;
    }
    test_tool_check(hanging_written, "the hanging tool is written", &failures, &checks);
    // A tool that waits on a subprocess of its own, which the deadline's group kill must take with it. The subprocess's
    // pid goes to a file the test reads, so it can check the kill reached the whole group rather than the tool alone.
    char wrapper_path[4200];
    snprintf(wrapper_path, sizeof(wrapper_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int wrapper_descriptor = mkstemp(wrapper_path);
    char pid_path[4200];
    snprintf(pid_path, sizeof(pid_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int pid_descriptor = mkstemp(pid_path);
    if (pid_descriptor >= 0)
    {
        close(pid_descriptor);
        unlink(pid_path);
    }
    char wrapper_script[4600];
    const int wrapper_length = snprintf(wrapper_script, sizeof(wrapper_script), "#!/bin/sh\n/bin/sleep 30 &\nprintf '%%s\\n' \"$!\" > \"%s\"\nwait\n", pid_path);
    bool wrapper_written = wrapper_length > 0 && (size_t)wrapper_length < sizeof(wrapper_script) && wrapper_descriptor >= 0 &&
                           write(wrapper_descriptor, wrapper_script, (size_t)wrapper_length) == (ssize_t)wrapper_length && fchmod(wrapper_descriptor, 0755) == 0;
    if (wrapper_descriptor >= 0)
    {
        wrapper_written = close(wrapper_descriptor) == 0 && wrapper_written;
    }
    test_tool_check(wrapper_written && pid_descriptor >= 0, "the wrapping tool is written", &failures, &checks);
    // A target-prefixed tool whose own version is old: the gate must read the version, not the target's digits.
    char target_path[4200];
    snprintf(target_path, sizeof(target_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int target_descriptor = mkstemp(target_path);
    const char target_script[] = "#!/bin/sh\nprintf 'x86_64-conda-linux-gnu-gcc (conda-forge gcc 9.4.0-1) 9.4.0\\n'\n";
    bool target_written = target_descriptor >= 0 && write(target_descriptor, target_script, sizeof(target_script) - 1) == (ssize_t)(sizeof(target_script) - 1) &&
                          fchmod(target_descriptor, 0755) == 0;
    if (target_descriptor >= 0)
    {
        target_written = close(target_descriptor) == 0 && target_written;
    }
    test_tool_check(target_written, "the target-prefixed tool is written", &failures, &checks);
    // A tool that starts but whose --version fails, as a perf wrapper does on a kernel without its perf: unavailable
    // even where no minimum is asked, since its consumers would fail opaquely.
    char failing_path[4200];
    snprintf(failing_path, sizeof(failing_path), "%s/buster-test-tool-XXXXXX", temporary);
    const int failing_descriptor = mkstemp(failing_path);
    const char failing_script[] = "#!/bin/sh\nprintf 'WARNING: perf not found for kernel 6.18.44\\n'\nexit 2\n";
    bool failing_written = failing_descriptor >= 0 && write(failing_descriptor, failing_script, sizeof(failing_script) - 1) == (ssize_t)(sizeof(failing_script) - 1) &&
                           fchmod(failing_descriptor, 0755) == 0;
    if (failing_descriptor >= 0)
    {
        failing_written = close(failing_descriptor) == 0 && failing_written;
    }
    test_tool_check(failing_written, "the failing tool is written", &failures, &checks);
    const uint32_t minimum_fifteen[] = { 15 };
    const uint32_t minimum_sixteen[] = { 16 };
    const uint32_t minimum_seventeen[] = { 17 };
    TestTool modern = { 0 };
    TestTool old = { 0 };
    if (written)
    {
        modern = test_tool_probe("gcc", path, minimum_sixteen, 1);
        old = test_tool_probe("gcc", path, minimum_seventeen, 1);
    }
    test_tool_check(modern.found && modern.version_parsed && test_tool_available(&modern) && strcmp(modern.version_text, "16.2.1") == 0, "the fake gcc 16.2.1 meets 16", &failures, &checks);
    test_tool_check(old.found && old.version_parsed && !test_tool_available(&old) && strcmp(old.version_text, "16.2.1") == 0, "the fake gcc 16.2.1 doesn't meet 17", &failures, &checks);
    const TestTool missing = test_tool_probe("absent-tool", "buster-test-tool-that-does-not-exist-1129", minimum_sixteen, 1);
    test_tool_check(!missing.found && !missing.available && strcmp(missing.unavailable, "not found on PATH") == 0, "a missing tool isn't found", &failures, &checks);
    const TestTool unreadable = quiet_written ? test_tool_probe("quiet", unreadable_path, minimum_sixteen, 1) : missing;
    test_tool_check(quiet_written && unreadable.found && !unreadable.version_parsed && !unreadable.available && strcmp(unreadable.unavailable, "version unreadable") == 0,
                    "a tool without a version can't meet one", &failures, &checks);
    TestTool failing = { 0 };
    TestTool failing_minimum = { 0 };
    if (failing_written)
    {
        failing = test_tool_probe("perf", failing_path, 0, 0);
        failing_minimum = test_tool_probe("perf", failing_path, minimum_sixteen, 1);
    }
    test_tool_check(failing_written && failing.found && failing.failed && !failing.version_parsed && !test_tool_available(&failing) &&
                        strcmp(failing.unavailable, "--version exited with status 2") == 0,
                    "a tool whose --version fails is unavailable without a minimum", &failures, &checks);
    test_tool_check(failing_written && failing_minimum.found && failing_minimum.failed && !test_tool_available(&failing_minimum),
                    "a tool whose --version fails is unavailable with a minimum, though its output holds digits", &failures, &checks);
    TestTool hanging = { 0 };
    if (hanging_written)
    {
        const uint32_t saved_seconds = test_tool_run_seconds;
        test_tool_run_seconds = 1;
        hanging = test_tool_probe("gcc", hanging_path, minimum_sixteen, 1);
        test_tool_run_seconds = saved_seconds;
    }
    test_tool_check(hanging_written && hanging.found && hanging.timed_out && !hanging.version_parsed && !test_tool_available(&hanging),
                    "a hanging tool is killed and unavailable", &failures, &checks);
    TestTool wrapper = { 0 };
    long grandchild = 0;
    if (wrapper_written)
    {
        const uint32_t saved_seconds = test_tool_run_seconds;
        test_tool_run_seconds = 1;
        wrapper = test_tool_probe("gcc", wrapper_path, minimum_sixteen, 1);
        test_tool_run_seconds = saved_seconds;
        FILE* pid_file = fopen(pid_path, "r");
        if (pid_file != 0)
        {
            if (fscanf(pid_file, "%ld", &grandchild) != 1)
            {
                grandchild = 0;
            }
            fclose(pid_file);
        }
    }
    // The recorded subprocess must have died with the probe's group: gone entirely, or a zombie that a reaping init
    // collects. A live one means the kill missed the group; it is killed here so a regression doesn't leak it.
    bool subprocess_gone = false;
    for (uint32_t tries = 0; grandchild > 0 && !subprocess_gone && tries < 200; tries += 1)
    {
        char process_path[64];
        snprintf(process_path, sizeof(process_path), "/proc/%ld/stat", grandchild);
        FILE* process_file = fopen(process_path, "r");
        if (process_file != 0)
        {
            char status[256] = { 0 };
            const size_t got = fread(status, 1, sizeof(status) - 1, process_file);
            fclose(process_file);
            const char* after = got > 0 ? strrchr(status, ')') : 0;
            const char state = after != 0 && after[1] == ' ' ? after[2] : 0;
            subprocess_gone = state == 'Z' || state == 'X' || state == 'x';
        }
        else
        {
            subprocess_gone = kill(grandchild, 0) != 0 && errno == ESRCH;
        }
        if (!subprocess_gone)
        {
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 10000000,
            };
            nanosleep(&pause, 0);
        }
    }
    if (grandchild > 0 && !subprocess_gone)
    {
        kill(grandchild, SIGKILL);
    }
    test_tool_check(wrapper.found && wrapper.timed_out && !test_tool_available(&wrapper) && subprocess_gone,
                    "a tool's whole process group is killed at the deadline", &failures, &checks);
    TestTool target = { 0 };
    if (target_written)
    {
        target = test_tool_probe("gcc", target_path, minimum_fifteen, 1);
    }
    test_tool_check(target.found && target.version_parsed && !test_tool_available(&target) && strcmp(target.version_text, "9.4.0") == 0,
                    "a target-prefixed gcc 9.4.0 doesn't meet 15", &failures, &checks);
    if (target_written)
    {
        test_tool_check_skip(&target, "test target", "test target: skipped: needs gcc >= 15, found 9.4.0\n", &failures, &checks);
    }
    if (written)
    {
        test_tool_check_skip(&old, "test old", "test old: skipped: needs gcc >= 17, found 16.2.1\n", &failures, &checks);
    }
    test_tool_check_skip(&missing, "test missing", "test missing: skipped: needs absent-tool >= 16, not found on PATH\n", &failures, &checks);
    if (quiet_written)
    {
        test_tool_check_skip(&unreadable, "test unreadable", "test unreadable: skipped: needs quiet >= 16, found version unreadable\n", &failures, &checks);
    }
    if (failing_written)
    {
        test_tool_check_skip(&failing, "test failing", "test failing: skipped: needs perf, --version exited with status 2\n", &failures, &checks);
    }
    if (hanging_written)
    {
        test_tool_check_skip(&hanging, "test hanging", "test hanging: skipped: needs gcc >= 16, didn't end within 1 s\n", &failures, &checks);
    }
    const TestTool missing_perf = test_tool_probe("perf", "buster-test-tool-that-does-not-exist-1129", 0, 0);
    test_tool_check_skip(&missing_perf, "test perf", "test perf: skipped: needs perf, not found on PATH\n", &failures, &checks);
    // The stdout path the cross-checks call, shown once as a reader sees it.
    test_tool_skip(&missing, "test tools example");
    // test-tool itself accepts no arguments: the command's usage error, as main.c dispatches it.
    char* usage = 0;
    size_t usage_length = 0;
    FILE* usage_stream = open_memstream(&usage, &usage_length);
    char* extra[] = { "extra" };
    bool refused = usage_stream != 0 && test_tool_refuse(usage_stream, 1, extra) == 1;
    if (usage_stream != 0)
    {
        refused = fclose(usage_stream) == 0 && refused && usage != 0 && strcmp(usage, "test-tool: unexpected argument 'extra'\nusage: test-tool\n") == 0;
    }
    test_tool_check(refused, "test-tool refuses extra arguments", &failures, &checks);
    free(usage);
    if (descriptor >= 0)
    {
        unlink(path);
    }
    if (unreadable_descriptor >= 0)
    {
        unlink(unreadable_path);
    }
    if (hanging_descriptor >= 0)
    {
        unlink(hanging_path);
    }
    if (wrapper_descriptor >= 0)
    {
        unlink(wrapper_path);
    }
    if (target_descriptor >= 0)
    {
        unlink(target_path);
    }
    if (failing_descriptor >= 0)
    {
        unlink(failing_path);
    }
    unlink(pid_path);
    printf("test tools: %u checks, %u failed\n", checks, failures);
    return failures;
}
