// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone contract tests for c_dependency.c; no compiler build is required.
// --oracle additionally compares short -MT/-MQ rules with installed gcc/clang.
// These tests do not imply that the compiler CLI is wired to this module.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct String8 String8;
struct String8
{
    char* pointer;
    uint64_t length;
};

#include "../../src/buster/compiler/c_dependency.c"

static uint32_t checks;
static uint32_t failures;

static void dependency_test_check(bool ok, const char* label)
{
    checks += 1;
    if (!ok)
    {
        fprintf(stderr, "FAIL: %s\n", label);
        failures += 1;
    }
}

static String8 dependency_test_text(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

static void dependency_test_rule(DependencyRule rule, const char* expected)
{
    const size_t expected_length = strlen(expected);
    char output[4096];
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        rule.scalar = scalar != 0;
        memset(output, 0x5a, sizeof(output));
        uint64_t needed = UINT64_MAX;
        dependency_test_check(dependency_rule(&rule, NULL, 0, &needed) == DEPENDENCY_OK && needed == expected_length, "size query");
        dependency_test_check(dependency_rule(&rule, output, expected_length, &needed) == DEPENDENCY_OK &&
                              needed == expected_length && memcmp(output, expected, expected_length) == 0, "exact rule");
        dependency_test_check(output[expected_length] == 0x5a, "no NUL or capacity overrun");
        for (uint64_t capacity = 0; capacity < expected_length; capacity += 1)
        {
            memset(output, 0x5a, sizeof(output));
            dependency_test_check(dependency_rule(&rule, output, capacity, &needed) == DEPENDENCY_CAPACITY && needed == expected_length, "capacity rejection");
            bool unchanged = true;
            for (size_t i = 0; i < sizeof(output); i += 1)
            {
                unchanged = unchanged && output[i] == 0x5a;
            }
            dependency_test_check(unchanged, "capacity rejection preserves destination");
        }
    }
}

static void dependency_test_invalid(DependencyRule rule)
{
    char output[1024];
    memset(output, 0x5a, sizeof(output));
    uint64_t needed = UINT64_MAX;
    dependency_test_check(dependency_rule(&rule, output, sizeof(output), &needed) == DEPENDENCY_INVALID_INPUT && needed == 0, "malformed input rejected");
    bool unchanged = true;
    for (size_t i = 0; i < sizeof(output); i += 1)
    {
        unchanged = unchanged && output[i] == 0x5a;
    }
    dependency_test_check(unchanged, "malformed input preserves destination");
}

// Deliberately different reference: group runs of backslashes, then inspect the
// next byte. No classifier masks, chunks, sink or implementation helper.
static size_t dependency_test_quote(char* output, const char* input, size_t length)
{
    size_t count = 0;
    for (size_t i = 0; i < length; i += 1)
    {
        if (input[i] == '\\')
        {
            size_t end = i;
            while (end < length && input[end] == '\\')
            {
                end += 1;
            }
            const size_t n = end - i;
            const size_t copies = end < length && (input[end] == ' ' || input[end] == '\t') ? n * 2 : n;
            for (size_t j = 0; j < copies; j += 1)
            {
                output[count++] = '\\';
            }
            i = end - 1;
        }
        else
        {
            if (input[i] == ' ' || input[i] == '\t' || input[i] == '#')
            {
                output[count++] = '\\';
            }
            else if (input[i] == '$')
            {
                output[count++] = '$';
            }
            output[count++] = input[i];
        }
    }
    return count;
}

static void dependency_test_generated(void)
{
    static const char alphabet[] = "abcXYZ012._/- \t$#\\\\";
    char input[257];
    char expected[2048];
    char scalar_output[2048];
    char vector_output[2048];
    uint32_t random = 0x805;
    for (uint32_t iteration = 0; iteration < 8192; iteration += 1)
    {
        const uint32_t length = iteration % 256 + 1;
        for (uint32_t i = 0; i < length; i += 1)
        {
            random = random * 1664525u + 1013904223u;
            input[i] = alphabet[(random >> 16) % (sizeof(alphabet) - 1)];
        }
        // Force a backslash run over the first 64-byte boundary, followed by whitespace.
        if (length > 65 && iteration % 4 == 0)
        {
            memset(input + 60, '\\', 5);
            input[65] = iteration % 8 == 0 ? ' ' : '\t';
        }
        size_t expected_count = 0;
        memcpy(expected, "out.o: ", 7);
        expected_count += 7;
        expected_count += dependency_test_quote(expected + expected_count, input, length);
        expected[expected_count++] = '\n';
        DependencyRule rule = {
            .source = { .pointer = input, .length = length },
            .object = dependency_test_text("out.o"),
            .scalar = true,
        };
        uint64_t scalar_count = 0;
        uint64_t vector_count = 0;
        const DependencyStatus scalar_status = dependency_rule(&rule, scalar_output, sizeof(scalar_output), &scalar_count);
        rule.scalar = false;
        const DependencyStatus vector_status = dependency_rule(&rule, vector_output, sizeof(vector_output), &vector_count);
        dependency_test_check(scalar_status == DEPENDENCY_OK && vector_status == DEPENDENCY_OK &&
                              scalar_count == expected_count && vector_count == expected_count &&
                              memcmp(scalar_output, expected, expected_count) == 0 &&
                              memcmp(vector_output, expected, expected_count) == 0, "generated independent quote oracle");
    }
}

static void dependency_test_guarded(void)
{
    const long page = sysconf(_SC_PAGESIZE);
    dependency_test_check(page > 0, "page size");
    if (page > 0)
    {
        char* memory = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        dependency_test_check(memory != MAP_FAILED, "guarded mapping");
        if (memory != MAP_FAILED)
        {
            const bool guarded = mprotect(memory + page, (size_t)page, PROT_NONE) == 0;
            dependency_test_check(guarded, "guard page protection");
            if (guarded)
            {
                for (uint32_t length = 0; length <= 64; length += 1)
                {
                    for (uint32_t byte = 0; byte < 256; byte += 1)
                    {
                        char* start = memory + page - length;
                        memset(start, (int)byte, length);
                        const uint64_t expected = dependency_special_scalar(start, length);
                        dependency_test_check(dependency_special(start, length, false) == expected, "all bytes and guarded classifier tails");
                    }
                }
                for (uint32_t length = 1; length <= 256; length += 1)
                {
                    char* start = memory + page - length;
                    memset(start, 'x', length);
                    start[length - 1] = '$';
                    DependencyRule rule = {
                        .source = { .pointer = start, .length = length },
                        .object = dependency_test_text("x.o"),
                    };
                    char output[1024];
                    uint64_t written = 0;
                    dependency_test_check(dependency_rule(&rule, output, sizeof(output), &written) == DEPENDENCY_OK &&
                                          written == length + 7, "guarded full rule");
                }
            }
            munmap(memory, (size_t)page * 2);
        }
    }
}

static void dependency_test_fixtures(void)
{
    DependencyFile files[] = {
        { .path = dependency_test_text("src/main.c"), .used = true },
        { .path = dependency_test_text("inc/user.h"), .used = true },
        { .path = dependency_test_text("sys/system.h"), .used = true, .system = true },
        { .path = dependency_test_text("sys/transitive.h"), .used = true, .system = true },
        { .path = dependency_test_text("not-consumed.h"), .used = false },
        { .path = dependency_test_text("inc/from-command-line.h"), .used = true },
        { .path = { .pointer = NULL, .length = 10 }, .used = false },
    };
    DependencyRule rule = {
        .source = dependency_test_text("src/main.c"),
        .object = dependency_test_text("obj/main.o"),
        .files = files,
        .file_count = sizeof(files) / sizeof(files[0]),
    };
    dependency_test_rule(rule, "obj/main.o: src/main.c inc/user.h inc/from-command-line.h\n");
    rule.include_system = true;
    dependency_test_rule(rule, "obj/main.o: src/main.c inc/user.h sys/system.h sys/transitive.h inc/from-command-line.h\n");
    rule.include_system = false;
    rule.phony = true;
    dependency_test_rule(rule, "obj/main.o: src/main.c inc/user.h inc/from-command-line.h\n\ninc/user.h:\n\ninc/from-command-line.h:\n");
    // Reused from a user context: inclusion is not determined just by its directory.
    files[3].system = false;
    dependency_test_rule(rule, "obj/main.o: src/main.c inc/user.h sys/transitive.h inc/from-command-line.h\n\ninc/user.h:\n\nsys/transitive.h:\n\ninc/from-command-line.h:\n");
    DependencyTarget targets[] = {
        { .text = dependency_test_text("$(objdir)/raw.o"), .quote = false },
        { .text = dependency_test_text("obj $ #\\ name.o"), .quote = true },
    };
    rule = (DependencyRule){
        .source = dependency_test_text("src $ #\\ file.c"),
        .targets = targets,
        .target_count = 2,
    };
    dependency_test_rule(rule, "$(objdir)/raw.o obj\\ $$\\ \\#\\\\\\ name.o: src\\ $$\\ \\#\\\\\\ file.c\n");
    targets[1].text = dependency_test_text("bad\ntarget.o");
    dependency_test_invalid(rule);
    targets[1].text = dependency_test_text("bad\rtarget.o");
    targets[1].quote = false;
    dependency_test_invalid(rule);
    rule.targets = NULL;
    dependency_test_invalid(rule);
    rule.target_count = 0;
    rule.object = dependency_test_text("a.o");
    rule.source = dependency_test_text("");
    dependency_test_invalid(rule);
    rule.source = dependency_test_text("a\nb.c");
    dependency_test_invalid(rule);
    rule.source = dependency_test_text("a\rb.c");
    dependency_test_invalid(rule);
    char nul_name[] = { 'a', 0, 'b' };
    rule.source = (String8){ .pointer = nul_name, .length = sizeof(nul_name) };
    dependency_test_invalid(rule);
    rule.source = dependency_test_text("a.c");
    rule.files = NULL;
    rule.file_count = 1;
    dependency_test_invalid(rule);
    rule.files = files;
    files[0].path = (String8){ .pointer = NULL, .length = 4 };
    dependency_test_invalid(rule);
    files[0].path = dependency_test_text("first.h");
    files[1].path = dependency_test_text("late\ninvalid.h");
    rule.file_count = 2;
    dependency_test_invalid(rule);
    files[1].path = (String8){ .pointer = NULL, .length = 4 };
    files[1].system = true;
    rule.phony = true;
    dependency_test_rule(rule, "a.o: a.c first.h\n\nfirst.h:\n");
    rule.include_system = true;
    dependency_test_invalid(rule);
    rule.file_count = 0;
    uint64_t needed = 99;
    dependency_test_check(dependency_rule(NULL, NULL, 0, &needed) == DEPENDENCY_INVALID_INPUT && needed == 0, "null rule");
    dependency_test_check(dependency_rule(&rule, NULL, 1, &needed) == DEPENDENCY_INVALID_INPUT, "invalid query capacity");
    dependency_test_check(dependency_rule(&rule, NULL, 0, NULL) == DEPENDENCY_INVALID_INPUT, "null size destination");
    DependencySink sink = { .count = UINT64_MAX - 1, .status = DEPENDENCY_OK };
    dependency_append(&sink, "xx", 2);
    dependency_test_check(sink.status == DEPENDENCY_OVERFLOW && sink.count == UINT64_MAX - 1, "append count overflow");
    sink = (DependencySink){ .count = UINT64_MAX, .status = DEPENDENCY_OK };
    dependency_backslashes(&sink, 1);
    dependency_test_check(sink.status == DEPENDENCY_OVERFLOW && sink.count == UINT64_MAX, "escape count overflow");
}

static void dependency_test_oracle(const char* compiler, const char* directory, const char* target, bool quote)
{
    int descriptors[2];
    const bool piped = pipe(descriptors) == 0;
    dependency_test_check(piped, "oracle pipe");
    if (piped)
    {
        const pid_t child = fork();
        dependency_test_check(child >= 0, "oracle fork");
        if (child == 0)
        {
            close(descriptors[0]);
            if (dup2(descriptors[1], STDOUT_FILENO) < 0 || chdir(directory) != 0)
            {
                _exit(125);
            }
            close(descriptors[1]);
            execlp(compiler, compiler, "-MM", quote ? "-MQ" : "-MT", target, "tiny.c", (char*)NULL);
            _exit(127);
        }
        close(descriptors[1]);
        char actual[4096];
        size_t count = 0;
        bool read_ok = true;
        if (child > 0)
        {
            ssize_t n;
            do
            {
                n = read(descriptors[0], actual + count, sizeof(actual) - count);
                if (n > 0)
                {
                    count += (size_t)n;
                }
                else if (n < 0 && errno != EINTR)
                {
                    read_ok = false;
                }
            } while ((n > 0 || (n < 0 && errno == EINTR)) && count < sizeof(actual));
        }
        close(descriptors[0]);
        int status = 0;
        pid_t waited = -1;
        if (child > 0)
        {
            do
            {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
        }
        DependencyTarget one = {
            .text = dependency_test_text(target),
            .quote = quote,
        };
        DependencyRule rule = {
            .source = dependency_test_text("tiny.c"),
            .targets = &one,
            .target_count = 1,
        };
        char expected[4096];
        uint64_t expected_count = 0;
        const DependencyStatus rendered = dependency_rule(&rule, expected, sizeof(expected), &expected_count);
        dependency_test_check(read_ok && waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
                              rendered == DEPENDENCY_OK && expected_count == count && memcmp(expected, actual, count) == 0, compiler);
    }
}

static void dependency_test_oracles(void)
{
    char directory[] = "/tmp/buster-dependency-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    dependency_test_check(created, "oracle directory");
    if (created)
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/tiny.c", directory);
        FILE* file = fopen(path, "wb");
        bool ready = file != NULL;
        if (file != NULL)
        {
            ready = fputs("int x;\n", file) >= 0;
            ready = fclose(file) == 0 && ready;
        }
        dependency_test_check(ready, "oracle source");
        if (ready)
        {
            static const char* const targets[] = {
                "out.o",
                "out name.o",
                "out#name.o",
                "$(objdir)/out.o",
                "out\\ name.o",
                "out\\\\ name.o",
                "out\tname.o",
                "out\\#name.o",
            };
            static const char* const compilers[] = { "gcc", "clang" };
            for (uint32_t c = 0; c < sizeof(compilers) / sizeof(compilers[0]); c += 1)
            {
                for (uint32_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t += 1)
                {
                    for (uint32_t quote = 0; quote < 2; quote += 1)
                    {
                        dependency_test_oracle(compilers[c], directory, targets[t], quote != 0);
                    }
                }
            }
        }
        unlink(path);
        rmdir(directory);
    }
}

int main(int argc, char** argv)
{
    dependency_test_fixtures();
    dependency_test_generated();
    dependency_test_guarded();
    if (argc == 2 && strcmp(argv[1], "--oracle") == 0)
    {
        dependency_test_oracles();
    }
    else if (argc != 1)
    {
        dependency_test_check(false, "usage: dependency-rule-test [--oracle]");
    }
    printf("dependency-rule: %u checks, %u failures; AVX-512BW compiled: %d\n", checks, failures, BUSTER_DEPENDENCY_VECTOR);
    return failures == 0 ? 0 : 1;
}
