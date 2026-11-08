// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1218: compile this original fixture's SUBJECT by Buster, its ORACLE by Clang, and link them. The default mode is
// the process supervisor. Both Tier 1 host paths must compile strictly, verify, and execute against the independent
// oracle. GNU objdump must find every branch-only function, exactly one cmp/test in each, and no materialized setcc.
// Shared values, truncation, a nonzero comparison, observable accesses and unordered floating comparisons have
// semantic checks only: these are not all legal targets for the integer peephole.

#define BUSTER_TRUTH_CASES(X) \
    X(eq, long, ==) \
    X(ne, long, !=) \
    X(lt, long, <) \
    X(le, long, <=) \
    X(gt, long, >) \
    X(ge, long, >=) \
    X(ult, unsigned long, <) \
    X(ule, unsigned long, <=) \
    X(ugt, unsigned long, >) \
    X(uge, unsigned long, >=)

#if defined(BUSTER_TIER1_TRUTH_SUBJECT)

// The signed long conversion deliberately reproduces zext i1 -> i32, sext i32 -> i64, compare with zero.
// The observable arm keeps these branch-only probes as control flow under #1382's if-conversion. Its independent
// count oracle also checks the taken edge; pure conditional values have their own cmov shape tests.
volatile unsigned long truth_taken;

#define BUSTER_TRUTH_DEFINE(name, type, comparison) \
    __attribute__((noinline)) unsigned long truth_branch_##name(type x, type y) \
    { \
        unsigned long result = (unsigned long)y + 11; \
        if ((long)(x comparison y) != 0) \
        { \
            result = (unsigned long)x + 7; \
            truth_taken += 1; \
        } \
        return result; \
    } \
    __attribute__((noinline)) unsigned long truth_branch_not_##name(type x, type y) \
    { \
        unsigned long result = (unsigned long)y + 11; \
        if (0 == (long)(x comparison y)) \
        { \
            result = (unsigned long)x + 7; \
            truth_taken += 1; \
        } \
        return result; \
    }
BUSTER_TRUTH_CASES(BUSTER_TRUTH_DEFINE)
#undef BUSTER_TRUTH_DEFINE

__attribute__((noinline)) unsigned long truth_nested(long x, long y)
{
    unsigned long result = (unsigned long)y + 11;
    if ((long)((long)(x < y) == 0) != 0)
    {
        result = (unsigned long)x + 7;
        truth_taken += 1;
    }
    return result;
}

__attribute__((noinline)) unsigned long truth_shared(long x, long y)
{
    const int comparison = x < y;
    unsigned long result = (unsigned long)comparison;
    if ((long)comparison != 0)
    {
        result += (unsigned long)x;
    }
    else
    {
        result += (unsigned long)y;
    }
    return result;
}

__attribute__((noinline)) unsigned long truth_trunc(long x, long y)
{
    unsigned long result = 11;
    if ((unsigned char)((unsigned long)(x < y) << 8) != 0)
    {
        result = 7;
    }
    return result;
}

__attribute__((noinline)) unsigned long truth_nonzero(long x, long y)
{
    unsigned long result = 11;
    if ((long)(x < y) == 2)
    {
        result = 7;
    }
    return result;
}

__attribute__((noinline)) unsigned long truth_volatile(volatile long* x, long y)
{
    const long before = (*x)++;
    unsigned long result = 11;
    if ((long)(before < y) != 0)
    {
        result = 7;
    }
    return result;
}

__attribute__((noinline)) unsigned long truth_float(double x, double y)
{
    unsigned long result = 11;
    if ((long)(x < y) == 0)
    {
        result = 7;
    }
    return result;
}

#elif defined(BUSTER_TIER1_TRUTH_ORACLE)

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#define BUSTER_TRUTH_DECLARE(name, type, comparison) \
    unsigned long truth_branch_##name(type x, type y); \
    unsigned long truth_branch_not_##name(type x, type y);
BUSTER_TRUTH_CASES(BUSTER_TRUTH_DECLARE)
#undef BUSTER_TRUTH_DECLARE
unsigned long truth_nested(long x, long y);
extern volatile unsigned long truth_taken;
unsigned long truth_shared(long x, long y);
unsigned long truth_trunc(long x, long y);
unsigned long truth_nonzero(long x, long y);
unsigned long truth_volatile(volatile long* x, long y);
unsigned long truth_float(double x, double y);

int main(void)
{
    const long inputs[] = {
        LONG_MIN,
        LONG_MIN + 1,
        -65536,
        -257,
        -256,
        -129,
        -128,
        -1,
        0,
        1,
        2,
        127,
        128,
        255,
        256,
        257,
        65535,
        LONG_MAX - 1,
        LONG_MAX,
    };
    bool valid = true;
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i += 1)
    {
        for (unsigned j = 0; j < sizeof(inputs) / sizeof(inputs[0]); j += 1)
        {
            const long x = inputs[i];
            const long y = inputs[j];
            const unsigned long yes = (unsigned long)x + 7;
            const unsigned long no = (unsigned long)y + 11;
#define BUSTER_TRUTH_CHECK(name, type, comparison) \
            do \
            { \
                const bool condition = (type)x comparison (type)y; \
                const unsigned long taken = truth_taken; \
                const unsigned long direct = truth_branch_##name((type)x, (type)y); \
                const unsigned long inverse = truth_branch_not_##name((type)x, (type)y); \
                valid = direct == (condition ? yes : no) && inverse == (condition ? no : yes) && truth_taken == taken + 1 && valid; \
            } while (0);
            BUSTER_TRUTH_CASES(BUSTER_TRUTH_CHECK)
#undef BUSTER_TRUTH_CHECK
            const unsigned long taken = truth_taken;
            valid = truth_nested(x, y) == (x >= y ? yes : no) && truth_taken == taken + (unsigned long)(x >= y) && valid;
            const unsigned long shared = (unsigned long)(x < y) + (unsigned long)(x < y ? x : y);
            valid = truth_shared(x, y) == shared && valid;
            valid = truth_trunc(x, y) == 11 && valid;
            valid = truth_nonzero(x, y) == 11 && valid;
            if (x != LONG_MAX)
            {
                volatile long value = x;
                const unsigned long observed = truth_volatile(&value, y);
                valid = observed == (x < y ? 7ul : 11ul) && value == x + 1 && valid;
            }
        }
    }
    const double floating[] = {
        -__builtin_inf(),
        -1.0,
        -0.0,
        0.0,
        1.0,
        __builtin_inf(),
        __builtin_nan(""),
    };
    for (unsigned i = 0; i < sizeof(floating) / sizeof(floating[0]); i += 1)
    {
        for (unsigned j = 0; j < sizeof(floating) / sizeof(floating[0]); j += 1)
        {
            const unsigned long expected = floating[i] < floating[j] ? 11 : 7;
            valid = truth_float(floating[i], floating[j]) == expected && valid;
        }
    }
    puts(valid ? "Tier 1 truth native oracle: pass" : "Tier 1 truth native oracle: FAIL");
    return valid ? 0 : 1;
}

#else

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool truth_run(char* const arguments[], const char* output)
{
    const pid_t child = fork();
    bool valid = child >= 0;
    if (child == 0)
    {
        if (output != NULL)
        {
            const int descriptor = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (descriptor < 0 || dup2(descriptor, STDOUT_FILENO) < 0)
            {
                perror("truth output");
                _exit(126);
            }
            close(descriptor);
        }
        alarm(120);
        execvp(arguments[0], arguments);
        perror(arguments[0]);
        _exit(127);
    }
    if (child > 0)
    {
        int status = 0;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        valid = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (!valid)
    {
        fprintf(stderr, "truth command failed: %s\n", arguments[0]);
    }
    return valid;
}

static const char* const truth_names[] = {
#define BUSTER_TRUTH_NAME(name, type, comparison) "truth_branch_" #name, "truth_branch_not_" #name,
    BUSTER_TRUTH_CASES(BUSTER_TRUTH_NAME)
#undef BUSTER_TRUTH_NAME
    "truth_nested",
};

static bool truth_shape(const char* path)
{
    const unsigned count = sizeof(truth_names) / sizeof(truth_names[0]);
    unsigned comparisons[sizeof(truth_names) / sizeof(truth_names[0])] = { 0 };
    unsigned sets[sizeof(truth_names) / sizeof(truth_names[0])] = { 0 };
    unsigned seen[sizeof(truth_names) / sizeof(truth_names[0])] = { 0 };
    unsigned current = count;
    FILE* stream = fopen(path, "r");
    bool valid = stream != NULL;
    char line[4096];
    while (valid && fgets(line, sizeof(line), stream) != NULL)
    {
        const char* open = strchr(line, '<');
        const char* close = open != NULL ? strstr(open, ">:") : NULL;
        if (close != NULL)
        {
            current = count;
            const size_t length = (size_t)(close - open - 1);
            for (unsigned i = 0; i < count; i += 1)
            {
                if (strlen(truth_names[i]) == length && memcmp(open + 1, truth_names[i], length) == 0)
                {
                    current = i;
                    seen[i] += 1;
                }
            }
        }
        else if (current < count)
        {
            unsigned long long address;
            char operation[32];
            if (sscanf(line, " %llx: %31s", &address, operation) == 2)
            {
                comparisons[current] += strncmp(operation, "cmp", 3) == 0 || strncmp(operation, "test", 4) == 0;
                sets[current] += strncmp(operation, "set", 3) == 0;
            }
        }
    }
    if (stream != NULL)
    {
        valid = !ferror(stream) && valid;
        valid = fclose(stream) == 0 && valid;
    }
    for (unsigned i = 0; i < count; i += 1)
    {
        if (seen[i] != 1 || comparisons[i] != 1 || sets[i] != 0)
        {
            fprintf(stderr, "truth shape %s: definitions=%u cmp/test=%u setcc=%u\n", truth_names[i], seen[i], comparisons[i], sets[i]);
            valid = false;
        }
    }
    return valid;
}

int main(int argc, char** argv)
{
    bool valid = argc == 3;
    if (!valid)
    {
        fprintf(stderr, "usage: %s BUSTER_COMPILER CLANG (from the repository root)\n", argv[0]);
    }
    char directory[] = "/tmp/buster-truth-XXXXXX";
    const bool created = valid && mkdtemp(directory) != NULL;
    valid = valid && created;
    char subject[128];
    char oracle[128];
    char program[128];
    char assembly[128];
    // All paths have a fixed, bounded prefix and fixed suffixes, smaller than their buffers.
    snprintf(subject, sizeof(subject), "%s/subject.o", directory);
    snprintf(oracle, sizeof(oracle), "%s/oracle.o", directory);
    snprintf(program, sizeof(program), "%s/program", directory);
    snprintf(assembly, sizeof(assembly), "%s/assembly.txt", directory);
    char source[] = "tests/driver/tier1_truth.c";
    if (valid)
    {
        char* arguments[] = { argv[2], "-std=c17", "-O2", "-DBUSTER_TIER1_TRUTH_ORACLE", "-c", source, "-o", oracle, NULL };
        valid = truth_run(arguments, NULL);
    }
    for (unsigned scalar = 0; valid && scalar < 2; scalar += 1)
    {
        // Fresh outputs: an unsuccessful command must never reuse the preceding path's files.
        unlink(subject);
        unlink(program);
        unlink(assembly);
        char* compile[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", "--tier1-report", "-std=c17", "-DBUSTER_TIER1_TRUTH_SUBJECT", "-c", source, "-o", subject, scalar ? "--scalar" : NULL, NULL };
        char* link[] = { argv[2], oracle, subject, "-o", program, NULL };
        char* execute[] = { program, NULL };
        char* disassemble[] = { "objdump", "-d", "--no-show-raw-insn", subject, NULL };
        valid = truth_run(compile, NULL) && truth_run(link, NULL) && truth_run(execute, NULL) && truth_run(disassemble, assembly) && truth_shape(assembly);
        printf("Tier 1 truth %s: %s\n", scalar ? "scalar" : "default", valid ? "pass" : "FAIL");
    }
    if (valid)
    {
        unlink(subject);
        unlink(oracle);
        unlink(program);
        unlink(assembly);
        valid = rmdir(directory) == 0;
    }
    if (!valid && created)
    {
        fprintf(stderr, "truth regression artifacts retained in %s\n", directory);
    }
    return valid ? 0 : 1;
}

#endif
