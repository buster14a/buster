// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1371: original sparse-switch regressions. Buster compiles SUBJECT, Clang compiles the independent ORACLE, and
// the default process supervisor links them. Both host paths must compile with --tier1-strict --verify, execute
// every oracle probe, and produce identical objects. A native CFG oracle bounds the largest comparison count on
// any path through sparse_signed32; a linear chain cannot pass merely by computing the right results.

#define BUSTER_SPARSE_16(M, B) \
    M((B) + 0) M((B) + 1) M((B) + 2) M((B) + 3) \
    M((B) + 4) M((B) + 5) M((B) + 6) M((B) + 7) \
    M((B) + 8) M((B) + 9) M((B) + 10) M((B) + 11) \
    M((B) + 12) M((B) + 13) M((B) + 14) M((B) + 15)

#define BUSTER_SPARSE_256(M) \
    BUSTER_SPARSE_16(M, 0) BUSTER_SPARSE_16(M, 16) BUSTER_SPARSE_16(M, 32) BUSTER_SPARSE_16(M, 48) \
    BUSTER_SPARSE_16(M, 64) BUSTER_SPARSE_16(M, 80) BUSTER_SPARSE_16(M, 96) BUSTER_SPARSE_16(M, 112) \
    BUSTER_SPARSE_16(M, 128) BUSTER_SPARSE_16(M, 144) BUSTER_SPARSE_16(M, 160) BUSTER_SPARSE_16(M, 176) \
    BUSTER_SPARSE_16(M, 192) BUSTER_SPARSE_16(M, 208) BUSTER_SPARSE_16(M, 224) BUSTER_SPARSE_16(M, 240)

// The case order is a permutation, not numeric order. The oracle derives its answers without this permutation.
#define BUSTER_SPARSE_KEY(I) (((I) * 73U) & 255U)
#define BUSTER_SPARSE_RESULT(I) ((int)(BUSTER_SPARSE_KEY(I) * 17U + 3U))

#if defined(BUSTER_TIER1_SPARSE_SUBJECT)

__attribute__((noinline)) int sparse_signed32(int x)
{
    int result = -1;
    switch (x)
    {
#define BUSTER_SPARSE_CASE(I) case (int)BUSTER_SPARSE_KEY(I) * 257 - 32768: result = BUSTER_SPARSE_RESULT(I); break;
        BUSTER_SPARSE_256(BUSTER_SPARSE_CASE)
#undef BUSTER_SPARSE_CASE
    case (-2147483647 - 1): result = -11; break;
    case 2147483647: result = -13; break;
    }
    return result;
}

__attribute__((noinline)) int sparse_unsigned32(unsigned x)
{
    int result;
    switch (x)
    {
#define BUSTER_SPARSE_CASE(I) case BUSTER_SPARSE_KEY(I) * 100003U + ((BUSTER_SPARSE_KEY(I) & 1U) << 31): result = BUSTER_SPARSE_RESULT(I); break;
        BUSTER_SPARSE_256(BUSTER_SPARSE_CASE)
#undef BUSTER_SPARSE_CASE
    case 0x80000000U: result = -17; break;
    case 0xffffffffU: result = -19; break;
    default: result = -1; break;
    }
    return result;
}

__attribute__((noinline)) int sparse_signed64(long x)
{
    int result = -1;
    switch (x)
    {
#define BUSTER_SPARSE_CASE(I) case ((long)BUSTER_SPARSE_KEY(I) - 128L) * 100000000003L: result = BUSTER_SPARSE_RESULT(I); break;
        BUSTER_SPARSE_256(BUSTER_SPARSE_CASE)
#undef BUSTER_SPARSE_CASE
    case (-9223372036854775807L - 1): result = -23; break;
    case 9223372036854775807L: result = -29; break;
    }
    return result;
}

__attribute__((noinline)) int sparse_unsigned64(unsigned long x)
{
    int result = -1;
    // The outer dispatch is planned before its selector's inner switch is emitted. The low two bits are retained
    // by each XOR, so the oracle can invert this transformation when probing every outer case's boundaries.
    switch (({
        unsigned long selected = x;
        switch (x & 3)
        {
        case 0: selected ^= 4; break;
        case 1: selected ^= 8; break;
        case 2: selected ^= 12; break;
        default: selected ^= 16; break;
        }
        selected;
    }))
    {
#define BUSTER_SPARSE_CASE(I) case BUSTER_SPARSE_KEY(I) * 0x100000003UL + ((unsigned long)(BUSTER_SPARSE_KEY(I) & 1U) << 63): result = BUSTER_SPARSE_RESULT(I); break;
        BUSTER_SPARSE_256(BUSTER_SPARSE_CASE)
#undef BUSTER_SPARSE_CASE
    case 0x8000000000000000UL: result = -31; break;
    case 0xffffffffffffffffUL: result = -37; break;
    }
    return result;
}

unsigned long sparse_cleanup;

__attribute__((noinline)) static void sparse_note(unsigned long* value)
{
    sparse_cleanup += *value;
}

__attribute__((noinline)) unsigned long sparse_ranges(volatile long* selector, unsigned long seed)
{
    // A value is live underneath the statement expression while every generated dispatch edge is lowered.
    const unsigned long answer = 7 + ({
        unsigned long result = seed;
        if (seed & 0x10000UL)
        {
            goto inside;
        }
        switch ((*selector)++)
        {
#define BUSTER_SPARSE_CASE(I) case ((long)BUSTER_SPARSE_KEY(I) - 128) * 100003L ... ((long)BUSTER_SPARSE_KEY(I) - 128) * 100003L + 2: result ^= (unsigned)BUSTER_SPARSE_RESULT(I); break;
            BUSTER_SPARSE_256(BUSTER_SPARSE_CASE)
#undef BUSTER_SPARSE_CASE
        case -2000000000L:
            result += 11;
            __attribute__((fallthrough));
        default:
            result += 29;
            __attribute__((fallthrough));
        case 2000000000L:
            result ^= 0x55;
            break;
        case 3000000000L:
        case 4000000000L:
        inside:
            result += 7;
            break;
        case 5000000000L:
            switch (seed & 3)
            {
            case 0: result += 101; break;
            case 1: result += 103; break;
            default: result += 107; break;
            }
            break;
        case 6000000000L:
        {
            unsigned long value __attribute__((cleanup(sparse_note))) = seed + 31;
            result += 13;
            break;
        }
        case 7000000000L:
        {
            unsigned long value __attribute__((cleanup(sparse_note))) = seed + 37;
            result += 17;
            goto finished;
        }
        case 9000000000L ... 8999999999L:
            result = 999;
            break;
        }
    finished:
        result;
    });
    return answer;
}

__attribute__((noinline)) int sparse_small(int x)
{
    int result = 41;
    switch (x)
    {
    case -200003: result = 3; break;
    case -1: result = 5; break;
    case 100003: result = 7; break;
    case 300009: result = 11; break;
    }
    return result;
}

#elif defined(BUSTER_TIER1_SPARSE_ORACLE)

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

int sparse_signed32(int x);
int sparse_unsigned32(unsigned x);
int sparse_signed64(long x);
int sparse_unsigned64(unsigned long x);
unsigned long sparse_ranges(volatile long* selector, unsigned long seed);
int sparse_small(int x);
extern unsigned long sparse_cleanup;

static int sparse_reference32(int x)
{
    int result = -1;
    if (x >= -32768 && x <= 32767 && (x + 32768) % 257 == 0)
    {
        result = (x + 32768) / 257 * 17 + 3;
    }
    if (x == INT_MIN)
    {
        result = -11;
    }
    if (x == INT_MAX)
    {
        result = -13;
    }
    return result;
}

static int sparse_reference_u32(unsigned x)
{
    int result = -1;
    for (unsigned k = 0; k < 256; k += 1)
    {
        const unsigned value = k * 100003U + (k % 2 != 0 ? 0x80000000U : 0);
        if (x == value)
        {
            result = (int)(k * 17 + 3);
        }
    }
    if (x == 0x80000000U)
    {
        result = -17;
    }
    if (x == UINT_MAX)
    {
        result = -19;
    }
    return result;
}

static int sparse_reference64(long x)
{
    int result = -1;
    for (unsigned k = 0; k < 256; k += 1)
    {
        if (x == ((long)k - 128) * 100000000003L)
        {
            result = (int)(k * 17 + 3);
        }
    }
    if (x == LONG_MIN)
    {
        result = -23;
    }
    if (x == LONG_MAX)
    {
        result = -29;
    }
    return result;
}

static unsigned long sparse_selector64(unsigned long x)
{
    return x ^ ((x % 4 + 1) * 4);
}

static int sparse_reference_u64(unsigned long x)
{
    const unsigned long selected = sparse_selector64(x);
    int result = -1;
    for (unsigned k = 0; k < 256; k += 1)
    {
        const unsigned long value = k * 0x100000003UL + (k % 2 != 0 ? 0x8000000000000000UL : 0);
        if (selected == value)
        {
            result = (int)(k * 17 + 3);
        }
    }
    if (selected == 0x8000000000000000UL)
    {
        result = -31;
    }
    if (selected == ULONG_MAX)
    {
        result = -37;
    }
    return result;
}

static bool sparse_check_ranges(long x, unsigned long seed)
{
    volatile long selector = x;
    unsigned long expected = seed;
    unsigned long cleanup = 0;
    bool matched = false;
    if (seed & 0x10000UL)
    {
        expected += 7;
    }
    else
    {
        for (unsigned k = 0; k < 256; k += 1)
        {
            const long low = ((long)k - 128) * 100003L;
            if (x >= low && x <= low + 2)
            {
                expected ^= k * 17 + 3;
                matched = true;
            }
        }
        if (!matched)
        {
            if (x == 3000000000L || x == 4000000000L)
            {
                expected += 7;
            }
            else if (x == 5000000000L)
            {
                expected += seed % 4 == 0 ? 101 : seed % 4 == 1 ? 103 : 107;
            }
            else if (x == 6000000000L || x == 7000000000L)
            {
                expected += x == 6000000000L ? 13 : 17;
                cleanup = seed + (x == 6000000000L ? 31 : 37);
            }
            else
            {
                expected += x == -2000000000L ? 11 : 0;
                expected += x == 2000000000L ? 0 : 29;
                expected ^= 0x55;
            }
        }
    }
    sparse_cleanup = 0;
    const unsigned long actual = sparse_ranges(&selector, seed);
    const bool valid = actual == expected + 7 && selector == x + ((seed & 0x10000UL) == 0) && sparse_cleanup == cleanup;
    if (!valid)
    {
        fprintf(stderr, "sparse ranges: x=%ld seed=%lu actual=%lu expected=%lu selector=%ld cleanup=%lu expected-cleanup=%lu\n",
                x, seed, actual, expected + 7, selector, sparse_cleanup, cleanup);
    }
    return valid;
}

static bool sparse_check_values(int a, unsigned b, long c, unsigned long d)
{
    const bool valid = sparse_signed32(a) == sparse_reference32(a) && sparse_unsigned32(b) == sparse_reference_u32(b) &&
                       sparse_signed64(c) == sparse_reference64(c) && sparse_unsigned64(d) == sparse_reference_u64(d);
    if (!valid)
    {
        fprintf(stderr, "sparse integer probe: signed32=%d unsigned32=%u signed64=%ld unsigned64=%lu\n", a, b, c, d);
    }
    return valid;
}

int main(void)
{
    bool valid = true;
    // Exhaustive 16-bit input domain, with 256 sparse cases at both signed endpoints and all of their holes.
    for (int x = -32768; valid && x <= 32767; x += 1)
    {
        valid = sparse_signed32(x) == sparse_reference32(x);
        if (!valid)
        {
            fprintf(stderr, "sparse exhaustive signed16 input: %d\n", x);
        }
    }
    // Every wider case and its adjacent misses, including unsigned wrapping at zero.
    for (unsigned k = 0; valid && k < 256; k += 1)
    {
        for (int delta = -1; valid && delta <= 1; delta += 1)
        {
            const int a = (int)k * 257 - 32768 + delta;
            const unsigned b = k * 100003U + (k % 2 != 0 ? 0x80000000U : 0) + (unsigned)delta;
            const long c = ((long)k - 128) * 100000000003L + delta;
            const unsigned long selected = k * 0x100000003UL + (k % 2 != 0 ? 0x8000000000000000UL : 0) + (unsigned long)delta;
            valid = sparse_check_values(a, b, c, sparse_selector64(selected));
        }
        for (int delta = -1; valid && delta <= 3; delta += 1)
        {
            valid = sparse_check_ranges(((long)k - 128) * 100003L + delta, k % 4);
        }
    }
    static const long edges[] = { LONG_MIN, LONG_MIN + 1, -1, 0, 1, LONG_MAX - 1, LONG_MAX };
    for (unsigned i = 0; valid && i < sizeof(edges) / sizeof(edges[0]); i += 1)
    {
        const long value = edges[i];
        valid = sparse_check_values((int)value, (unsigned)value, value, sparse_selector64((unsigned long)value));
    }
    valid = valid && sparse_check_values(INT_MIN, 0x80000000U, LONG_MIN, sparse_selector64(0x8000000000000000UL));
    valid = valid && sparse_check_values(INT_MAX, UINT_MAX, LONG_MAX, sparse_selector64(ULONG_MAX));
    unsigned long random = 1371;
    for (unsigned i = 0; valid && i < 4096; i += 1)
    {
        random = random * 6364136223846793005UL + 1442695040888963407UL;
        valid = sparse_check_values((int)random, (unsigned)(random >> 19), (long)random, random);
    }
    static const long controls[] = { -2000000001L, -2000000000L, 2000000000L, 3000000000L, 4000000000L,
                                    5000000000L, 6000000000L, 7000000000L, 8999999999L, 9000000000L };
    for (unsigned i = 0; valid && i < sizeof(controls) / sizeof(controls[0]); i += 1)
    {
        for (unsigned seed = 0; valid && seed < 4; seed += 1)
        {
            valid = sparse_check_ranges(controls[i], seed) && sparse_check_ranges(controls[i], seed + 0x10000UL);
        }
    }
    valid = valid && sparse_small(-200003) == 3 && sparse_small(-1) == 5 && sparse_small(100003) == 7 && sparse_small(300009) == 11 &&
            sparse_small(-200004) == 41 && sparse_small(0) == 41 && sparse_small(300010) == 41;
    puts(valid ? "sparse switch native oracle: pass" : "sparse switch native oracle: FAIL");
    return valid ? 0 : 1;
}

#else

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool sparse_run(char* const arguments[], const char* output)
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
                perror("sparse output");
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
        fprintf(stderr, "sparse command failed: %s\n", arguments[0]);
    }
    return valid;
}

static bool sparse_same(const char* first, const char* second)
{
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    bool valid = a != NULL && b != NULL;
    bool ended = false;
    while (valid && !ended)
    {
        unsigned char left[4096];
        unsigned char right[4096];
        const size_t left_count = fread(left, 1, sizeof(left), a);
        const size_t right_count = fread(right, 1, sizeof(right), b);
        valid = left_count == right_count && memcmp(left, right, left_count) == 0 && !ferror(a) && !ferror(b);
        ended = left_count < sizeof(left);
    }
    if (a != NULL)
    {
        valid = fclose(a) == 0 && valid;
    }
    if (b != NULL)
    {
        valid = fclose(b) == 0 && valid;
    }
    return valid;
}

// The fixture has a bounded, acyclic graph. Decode objdump's address and mnemonic fields only; register names and
// exact instruction selection do not affect the longest number of cmp/test instructions on a path to a return.
#define BUSTER_SPARSE_INSTRUCTIONS 16384
#define BUSTER_SPARSE_PATH_COMPARISONS 32

typedef struct SparseInstruction SparseInstruction;
struct SparseInstruction
{
    unsigned long long address;
    unsigned long long target;
    unsigned successors[2];
    unsigned successor_count;
    unsigned predecessors;
    unsigned comparisons;
    char operation[32];
    bool relative;
    bool reachable;
};

static bool sparse_shape(const char* path)
{
    SparseInstruction* instructions = calloc(BUSTER_SPARSE_INSTRUCTIONS, sizeof(*instructions));
    unsigned* queue = calloc(BUSTER_SPARSE_INSTRUCTIONS, sizeof(*queue));
    FILE* stream = fopen(path, "r");
    bool valid = stream != NULL && instructions != NULL && queue != NULL;
    unsigned count = 0;
    unsigned definitions = 0;
    bool body = false;
    char line[4096];
    while (valid && fgets(line, sizeof(line), stream) != NULL)
    {
        const char* open = strchr(line, '<');
        const char* close = open != NULL ? strstr(open, ">:") : NULL;
        if (close != NULL)
        {
            body = (size_t)(close - open) == strlen("<sparse_signed32") && memcmp(open, "<sparse_signed32", strlen("<sparse_signed32")) == 0;
            definitions += body;
        }
        else if (body)
        {
            SparseInstruction instruction = { 0 };
            char operand[96] = { 0 };
            const int fields = sscanf(line, " %llx: %31s %95s", &instruction.address, instruction.operation, operand);
            if (fields >= 2)
            {
                valid = count < BUSTER_SPARSE_INSTRUCTIONS;
                if (valid)
                {
                    if (instruction.operation[0] == 'j')
                    {
                        char* end = NULL;
                        instruction.target = strtoull(operand, &end, 16);
                        instruction.relative = end != operand && *end == 0;
                    }
                    instructions[count++] = instruction;
                }
            }
        }
    }
    if (stream != NULL)
    {
        valid = !ferror(stream) && valid;
        valid = fclose(stream) == 0 && valid;
    }
    valid = valid && definitions == 1 && count != 0;
    for (unsigned i = 0; valid && i < count; i += 1)
    {
        SparseInstruction* instruction = &instructions[i];
        const char* operation = instruction->operation;
        if (operation[0] == 'j')
        {
            unsigned target = count;
            for (unsigned j = 0; instruction->relative && j < count; j += 1)
            {
                if (instructions[j].address == instruction->target)
                {
                    target = j;
                }
            }
            valid = target < count;
            if (valid)
            {
                instruction->successors[instruction->successor_count++] = target;
            }
        }
        if (strcmp(operation, "jmp") != 0 && strncmp(operation, "ret", 3) != 0 && i + 1 < count)
        {
            instruction->successors[instruction->successor_count++] = i + 1;
        }
        valid = valid && strncmp(operation, "call", 4) != 0;
    }
    unsigned reached = 0;
    if (valid)
    {
        instructions[0].reachable = true;
        queue[reached++] = 0;
    }
    for (unsigned q = 0; valid && q < reached; q += 1)
    {
        const SparseInstruction* instruction = &instructions[queue[q]];
        valid = instruction->successor_count != 0 || strncmp(instruction->operation, "ret", 3) == 0;
        for (unsigned k = 0; valid && k < instruction->successor_count; k += 1)
        {
            const unsigned target = instruction->successors[k];
            if (!instructions[target].reachable)
            {
                instructions[target].reachable = true;
                queue[reached++] = target;
            }
            instructions[target].predecessors += 1;
        }
    }
    unsigned queued = 0;
    for (unsigned i = 0; valid && i < count; i += 1)
    {
        if (instructions[i].reachable && instructions[i].predecessors == 0)
        {
            queue[queued++] = i;
        }
    }
    unsigned maximum = 0;
    for (unsigned q = 0; valid && q < queued; q += 1)
    {
        SparseInstruction* instruction = &instructions[queue[q]];
        const unsigned comparisons = instruction->comparisons + (strncmp(instruction->operation, "cmp", 3) == 0 || strncmp(instruction->operation, "test", 4) == 0);
        if (comparisons > maximum)
        {
            maximum = comparisons;
        }
        for (unsigned k = 0; k < instruction->successor_count; k += 1)
        {
            SparseInstruction* successor = &instructions[instruction->successors[k]];
            if (comparisons > successor->comparisons)
            {
                successor->comparisons = comparisons;
            }
            successor->predecessors -= 1;
            if (successor->predecessors == 0)
            {
                queue[queued++] = instruction->successors[k];
            }
        }
    }
    valid = valid && queued == reached && reached != 0 && maximum <= BUSTER_SPARSE_PATH_COMPARISONS;
    printf("sparse native CFG: instructions=%u reachable=%u ordered=%u maximum-comparisons=%u bound=%u %s\n",
           count, reached, queued, maximum, BUSTER_SPARSE_PATH_COMPARISONS, valid ? "pass" : "FAIL");
    free(instructions);
    free(queue);
    return valid;
}

int main(int argc, char** argv)
{
    bool valid = argc == 3;
    if (!valid)
    {
        fprintf(stderr, "usage: %s BUSTER_COMPILER CLANG (from the repository root)\n", argv[0]);
    }
    char directory[] = "/tmp/buster-sparse-XXXXXX";
    const bool created = valid && mkdtemp(directory) != NULL;
    valid = valid && created;
    char subject[2][128];
    char oracle[128];
    char program[128];
    char assembly[128];
    snprintf(subject[0], sizeof(subject[0]), "%s/vector.o", directory);
    snprintf(subject[1], sizeof(subject[1]), "%s/scalar.o", directory);
    snprintf(oracle, sizeof(oracle), "%s/oracle.o", directory);
    snprintf(program, sizeof(program), "%s/program", directory);
    snprintf(assembly, sizeof(assembly), "%s/assembly.txt", directory);
    char source[] = "tests/driver/tier1_sparse_switch.c";
    if (valid)
    {
        char* compile[] = { argv[2], "-std=gnu17", "-O2", "-DBUSTER_TIER1_SPARSE_ORACLE", "-c", source, "-o", oracle, NULL };
        char* reference[] = { argv[2], "-std=gnu17", "-O2", "-w", "-DBUSTER_TIER1_SPARSE_SUBJECT", "-c", source, "-o", subject[0], NULL };
        char* link[] = { argv[2], oracle, subject[0], "-o", program, NULL };
        char* execute[] = { program, NULL };
        valid = sparse_run(compile, NULL) && sparse_run(reference, NULL) && sparse_run(link, NULL) && sparse_run(execute, NULL);
    }
    for (unsigned scalar = 0; valid && scalar < 2; scalar += 1)
    {
        unlink(subject[scalar]);
        unlink(program);
        unlink(assembly);
        char* compile[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", "--tier1-report", "-std=gnu17", "-w", "-DBUSTER_TIER1_SPARSE_SUBJECT",
                            "-c", source, "-o", subject[scalar], scalar ? "--scalar" : NULL, NULL };
        char* link[] = { argv[2], oracle, subject[scalar], "-o", program, NULL };
        char* execute[] = { program, NULL };
        char* disassemble[] = { "objdump", "-d", "--no-show-raw-insn", "--disassemble=sparse_signed32", subject[scalar], NULL };
        valid = sparse_run(compile, NULL) && sparse_run(link, NULL) && sparse_run(execute, NULL) && sparse_run(disassemble, assembly) && sparse_shape(assembly);
        printf("sparse switch %s: %s\n", scalar ? "scalar" : "default", valid ? "pass" : "FAIL");
    }
    if (valid)
    {
        valid = sparse_same(subject[0], subject[1]);
        printf("sparse switch scalar/vector object identity: %s\n", valid ? "pass" : "FAIL");
    }
    if (valid)
    {
        unlink(subject[0]);
        unlink(subject[1]);
        unlink(oracle);
        unlink(program);
        unlink(assembly);
        valid = rmdir(directory) == 0;
    }
    if (!valid && created)
    {
        fprintf(stderr, "sparse switch regression artifacts retained in %s\n", directory);
    }
    return valid ? 0 : 1;
}

#endif
