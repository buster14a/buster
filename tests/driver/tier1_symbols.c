// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2573: Tier 1's memory operands fold a direct symbol's address, rip-relative. Compile this original fixture's SUBJECT
// by Buster, its ORACLE by Clang, and link them, so that the system linker resolves every PC32 reference, its addend
// covering the immediate after the displacement field (cmp byte ptr [rip + x], 3's). The default mode is the process
// supervisor. Both Tier 1 host paths must compile strictly, verify, and execute against the oracle. GNU objdump must
// find every direct access rip-relative, without a lea of its symbol, each branch's comparison of its symbol's memory
// fused with its jump, and the GOT's and the thread-locals' accesses keeping their sequences: no PC32 names a symbol
// the unit doesn't define, nor a thread-local.

// Each width's fixtures, by name and type, and the constant its comparisons take: one that needs the width's immediate
// (an imm8 for a byte, an imm16 for a word, an imm32 for a doubleword and a quadword). Each width also compares with -7,
// which a sign-extended imm8 holds at every width (#2600).
#define BUSTER_SYMBOLS_WIDTHS(X) \
    X(u8, unsigned char, 0xc8) \
    X(i8, signed char, -3) \
    X(u16, unsigned short, 0x1234) \
    X(i16, short, -1000) \
    X(u32, unsigned int, 0x89abcdefu) \
    X(i32, int, -100000) \
    X(u64, unsigned long, 0x7edcba98ul) \
    X(i64, long, -0x12345678l)

// The operations a memory operand takes from its symbol (TIER1_RULE_LOAD_OPERAND): an addition prefers a lea of two
// registers, so these are the others.
#define BUSTER_SYMBOLS_OPERATIONS(X) \
    X(sub, -) \
    X(and, &) \
    X(or, |) \
    X(xor, ^)

#if defined(BUSTER_TIER1_SYMBOLS_SUBJECT)

// Every width at its own offset, past a byte that moves the others off zero.
struct SymbolsRecord
{
    unsigned char pad;
#define BUSTER_SYMBOLS_MEMBER(name, type, constant) type name;
    BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_MEMBER)
#undef BUSTER_SYMBOLS_MEMBER
    float f32;
    double f64;
};

#define BUSTER_SYMBOLS_GLOBAL(name, type, constant) type symbols_g_##name;
BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_GLOBAL)
#undef BUSTER_SYMBOLS_GLOBAL
struct SymbolsRecord symbols_record;
long symbols_table[8];
float symbols_g_f32;
double symbols_g_f64;
static long symbols_hidden;
_Thread_local long symbols_local_tls = 5;
// Each branch's observable arm keeps it a branch, a comparison fused with its jump, under #1382's if-conversion.
volatile long symbols_taken;

// The oracle's: a symbol this unit doesn't define, through its GOT slot, and a thread-local of another unit, through its
// offset's GOT slot (initial-exec).
extern long symbols_external;
extern struct SymbolsRecord symbols_external_record;
extern _Thread_local long symbols_oracle_tls;

#define BUSTER_SYMBOLS_DEFINE(name, type, constant) \
    __attribute__((noinline)) long symbols_load_##name(void) \
    { \
        return (long)symbols_g_##name; \
    } \
    __attribute__((noinline)) long symbols_member_##name(void) \
    { \
        return (long)symbols_record.name; \
    } \
    __attribute__((noinline)) void symbols_store_##name(long value) \
    { \
        symbols_g_##name = (type)value; \
        symbols_record.name = (type)(value + 1); \
    } \
    __attribute__((noinline)) int symbols_equal_##name(void) \
    { \
        return symbols_record.name == (type)(constant); \
    } \
    __attribute__((noinline)) long symbols_branch_##name(long x) \
    { \
        long result = x + 11; \
        if (symbols_g_##name < (type)(constant)) \
        { \
            result = x + 7; \
            symbols_taken += 1; \
        } \
        return result; \
    } \
    __attribute__((noinline)) int symbols_less_##name(type x) \
    { \
        return symbols_record.name < x; \
    } \
    __attribute__((noinline)) int symbols_small_##name(void) \
    { \
        return symbols_g_##name == (type)-7; \
    }
BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_DEFINE)
#undef BUSTER_SYMBOLS_DEFINE

#define BUSTER_SYMBOLS_OPERATE(name, operator) \
    __attribute__((noinline)) int symbols_##name##_i32(int x) \
    { \
        return x operator symbols_record.i32; \
    } \
    __attribute__((noinline)) long symbols_##name##_i64(long x) \
    { \
        return x operator symbols_g_i64; \
    }
BUSTER_SYMBOLS_OPERATIONS(BUSTER_SYMBOLS_OPERATE)
#undef BUSTER_SYMBOLS_OPERATE

__attribute__((noinline)) double symbols_float(double x)
{
    return x + symbols_g_f64 + symbols_record.f64 + symbols_g_f32 + symbols_record.f32;
}

__attribute__((noinline)) void symbols_float_store(double x)
{
    symbols_g_f64 = x;
    symbols_record.f64 = x + 1.0;
    symbols_g_f32 = (float)x;
    symbols_record.f32 = (float)(x + 2.0);
}

// Constant indices, the largest past the record, and a table's element by an index, whose base stays a register.
__attribute__((noinline)) long symbols_table_sum(long i)
{
    symbols_table[7] = symbols_table[0] + symbols_table[6];
    return symbols_table[i & 7] + symbols_table[3];
}

// An address of the symbol plus an offset: one lea from rip.
__attribute__((noinline)) long* symbols_address(void)
{
    return &symbols_table[5];
}

// A sequentially consistent store: xchg with the symbol's memory.
__attribute__((noinline)) void symbols_atomic(long value)
{
    __atomic_store_n(&symbols_g_i64, value, __ATOMIC_SEQ_CST);
}

// The symbol's address shared by a loop's blocks, and a static one, whose references are its section's.
__attribute__((noinline)) long symbols_loop(long n)
{
    long total = 0;
    for (long i = 0; i < n; i += 1)
    {
        symbols_hidden += i;
        total += symbols_g_i32 + symbols_record.i64;
    }
    return total + symbols_hidden;
}

// Two members deep, a value and a function to call: the typed stream's conversions around each member's address make
// the walk's chain to the symbol its bound's length.
struct SymbolsInner
{
    long pad;
    long value;
    int (*call)(int);
};

struct SymbolsOuter
{
    int a;
    struct SymbolsInner inner;
};

struct SymbolsOuter symbols_outer;

__attribute__((noinline)) long symbols_nested(long x)
{
    symbols_outer.inner.pad = x;
    return symbols_outer.inner.value;
}

__attribute__((noinline)) int symbols_nested_call(int x)
{
    return symbols_outer.inner.call(x);
}

// Through the GOT and the thread pointer: never a rip-relative access of the symbol itself.
__attribute__((noinline)) long symbols_got(long value)
{
    const long old = symbols_external;
    symbols_external = value;
    symbols_external_record.i16 = (short)value;
    return old + symbols_external_record.i32 + (symbols_external_record.u8 == 0xc8);
}

__attribute__((noinline)) long symbols_tls(long value)
{
    const long old = symbols_local_tls + symbols_oracle_tls;
    symbols_local_tls = value;
    symbols_oracle_tls = value + 1;
    return old + (symbols_local_tls == 3);
}

#elif defined(BUSTER_TIER1_SYMBOLS_ORACLE)

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct SymbolsRecord
{
    unsigned char pad;
#define BUSTER_SYMBOLS_MEMBER(name, type, constant) type name;
    BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_MEMBER)
#undef BUSTER_SYMBOLS_MEMBER
    float f32;
    double f64;
};

#define BUSTER_SYMBOLS_DECLARE(name, type, constant) \
    extern type symbols_g_##name; \
    long symbols_load_##name(void); \
    long symbols_member_##name(void); \
    void symbols_store_##name(long value); \
    int symbols_equal_##name(void); \
    long symbols_branch_##name(long x); \
    int symbols_less_##name(type x); \
    int symbols_small_##name(void);
BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_DECLARE)
#undef BUSTER_SYMBOLS_DECLARE
#define BUSTER_SYMBOLS_DECLARE_OPERATION(name, operator) \
    int symbols_##name##_i32(int x); \
    long symbols_##name##_i64(long x);
BUSTER_SYMBOLS_OPERATIONS(BUSTER_SYMBOLS_DECLARE_OPERATION)
#undef BUSTER_SYMBOLS_DECLARE_OPERATION
extern struct SymbolsRecord symbols_record;
extern long symbols_table[8];
extern float symbols_g_f32;
extern double symbols_g_f64;
extern _Thread_local long symbols_local_tls;
extern volatile long symbols_taken;
double symbols_float(double x);
void symbols_float_store(double x);
long symbols_table_sum(long i);
long* symbols_address(void);
void symbols_atomic(long value);
long symbols_loop(long n);
long symbols_got(long value);
long symbols_tls(long value);
long symbols_nested(long x);
int symbols_nested_call(int x);

struct SymbolsInner
{
    long pad;
    long value;
    int (*call)(int);
};

struct SymbolsOuter
{
    int a;
    struct SymbolsInner inner;
};

extern struct SymbolsOuter symbols_outer;

static int symbols_twice(int x)
{
    return 2 * x;
}

long symbols_external = 40;
struct SymbolsRecord symbols_external_record = { .i32 = 9, .u8 = 0xc8 };
_Thread_local long symbols_oracle_tls = 6;

// Each width's sample values: the constant and its neighbors, the extremes and a few others, as the width keeps them.
static const long symbols_samples[] = { 0, 1, -1, 2, -3, 127, 128, -128, -129, 255, 256, 999, -1000, -1001, 0x1234, 0x7fff, 0x8000, 0xffff, 0x89abcdef, 0x7edcba98, -0x12345678, -100000, 0x7fffffff, -0x7fffffffl - 1, 0x123456789abcdefl };

int main(void)
{
    bool valid = true;
    unsigned char bytes[sizeof(struct SymbolsRecord)];
    for (unsigned s = 0; s < sizeof(symbols_samples) / sizeof(symbols_samples[0]); s += 1)
    {
        const long value = symbols_samples[s];
#define BUSTER_SYMBOLS_CHECK(name, type, constant) \
        do \
        { \
            memset(&symbols_record, 0x5a, sizeof(symbols_record)); \
            symbols_store_##name(value); \
            const type stored = (type)value; \
            const type member = (type)(value + 1); \
            struct SymbolsRecord wanted; \
            memset(&wanted, 0x5a, sizeof(wanted)); \
            wanted.name = member; \
            memcpy(bytes, &symbols_record, sizeof(bytes)); \
            valid = symbols_g_##name == stored && memcmp(bytes, &wanted, sizeof(bytes)) == 0 && valid; \
            valid = symbols_load_##name() == (long)stored && symbols_member_##name() == (long)member && valid; \
            const long taken = symbols_taken; \
            valid = symbols_branch_##name(value) == (stored < (type)(constant) ? value + 7 : value + 11) && valid; \
            valid = symbols_taken == taken + (stored < (type)(constant)) && valid; \
            symbols_record.name = (type)(constant); \
            valid = symbols_equal_##name() == 1 && valid; \
            symbols_record.name = stored; \
            valid = symbols_equal_##name() == (stored == (type)(constant)) && valid; \
            valid = symbols_less_##name((type)(constant)) == (stored < (type)(constant)) && symbols_less_##name(member) == (stored < member) && valid; \
            valid = symbols_small_##name() == (stored == (type)-7) && valid; \
            symbols_g_##name = (type)-7; \
            valid = symbols_small_##name() == 1 && valid; \
            symbols_g_##name = stored; \
        } while (0);
        BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_CHECK)
#undef BUSTER_SYMBOLS_CHECK
        symbols_g_i64 = value;
        symbols_record.i32 = (int)(value >> 3);
#define BUSTER_SYMBOLS_CHECK_OPERATION(name, operator) \
        valid = symbols_##name##_i32((int)value) == ((int)value operator (int)(value >> 3)) && symbols_##name##_i64(value * 3) == ((value * 3) operator value) && valid;
        BUSTER_SYMBOLS_OPERATIONS(BUSTER_SYMBOLS_CHECK_OPERATION)
#undef BUSTER_SYMBOLS_CHECK_OPERATION
        const double x = (double)value / 8.0;
        symbols_float_store(x);
        valid = symbols_g_f64 == x && symbols_record.f64 == x + 1.0 && symbols_g_f32 == (float)x && symbols_record.f32 == (float)(x + 2.0) && valid;
        valid = symbols_float(0.5) == 0.5 + x + (x + 1.0) + (double)(float)x + (double)(float)(x + 2.0) && valid;
        for (unsigned k = 0; k < 8; k += 1)
        {
            symbols_table[k] = value * (long)(k + 1);
        }
        const long seven = symbols_table[0] + symbols_table[6];
        valid = symbols_table_sum((long)s) == (((long)s & 7) == 7 ? seven : symbols_table[s & 7]) + symbols_table[3] && symbols_table[7] == seven && valid;
        valid = symbols_address() == &symbols_table[5] && valid;
        symbols_atomic(value ^ 0x55);
        valid = symbols_g_i64 == (value ^ 0x55) && valid;
        symbols_g_i32 = (int)value;
        symbols_record.i64 = value >> 1;
        const long before = symbols_loop(0);
        long total = 0;
        long hidden = before;
        for (long i = 0; i < 5; i += 1)
        {
            hidden += i;
            total += (int)value + (value >> 1);
        }
        valid = symbols_loop(5) == total + hidden && valid;
        const long old = symbols_external;
        valid = symbols_got(value) == old + symbols_external_record.i32 + 1 && symbols_external == value && symbols_external_record.i16 == (short)value && valid;
        symbols_outer.inner.value = value ^ 7;
        symbols_outer.inner.call = symbols_twice;
        valid = symbols_nested(value) == (value ^ 7) && symbols_outer.inner.pad == value && symbols_nested_call((int)value) == 2 * (int)value && valid;
        const long tls = symbols_local_tls + symbols_oracle_tls;
        valid = symbols_tls(value) == tls + (value == 3) && symbols_local_tls == value && symbols_oracle_tls == value + 1 && valid;
    }
    puts(valid ? "Tier 1 symbols native oracle: pass" : "Tier 1 symbols native oracle: FAIL");
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

static bool symbols_run(char* const arguments[], const char* output)
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
                perror("symbols output");
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
        fprintf(stderr, "symbols command failed: %s\n", arguments[0]);
    }
    return valid;
}

// The symbols no direct access may name in a PC32: the oracle's and the thread-locals.
static const char* const symbols_indirect[] = {
    "symbols_external",
    "symbols_external_record",
    "symbols_oracle_tls",
    "symbols_local_tls",
};

// The relocation kinds each indirect function's sequences need, by their names in objdump's output.
static const char* const symbols_sequences[] = {
    "R_X86_64_REX_GOTPCRELX\tsymbols_external-",
    "R_X86_64_REX_GOTPCRELX\tsymbols_external_record-",
    "R_X86_64_TPOFF32\tsymbols_local_tls",
    "R_X86_64_GOTTPOFF\tsymbols_oracle_tls-",
};

// objdump -dr's text: every function but symbols_address, symbols_got and symbols_tls without a lea from rip, and
// symbols_address with one; every PC32 naming a symbol the unit defines and not a thread-local; each direct function
// with at least one PC32 on a memory operand; and the GOT's and thread-locals' sequences present.
static bool symbols_shape(const char* path)
{
    FILE* stream = fopen(path, "r");
    bool valid = stream != NULL;
    char line[4096];
    char function[256] = "";
    unsigned functions = 0;
    unsigned direct_references = 0;
    unsigned function_references = 0;
    unsigned function_leas = 0;
    unsigned function_jumps = 0;
    unsigned function_selects = 0;
    unsigned sequences[sizeof(symbols_sequences) / sizeof(symbols_sequences[0])] = { 0 };
    bool previous_lea = false;
    while (stream != NULL)
    {
        const bool more = fgets(line, sizeof(line), stream) != NULL;
        const char* open = more ? strchr(line, '<') : NULL;
        const char* close = open != NULL ? strstr(open, ">:") : NULL;
        // A function ends where the next starts, or at the end: check what it had.
        if ((close != NULL || !more) && function[0] != 0)
        {
            const bool address = strcmp(function, "symbols_address") == 0;
            const bool indirect = strcmp(function, "symbols_got") == 0 || strcmp(function, "symbols_tls") == 0;
            const bool table = strcmp(function, "symbols_table_sum") == 0;
            const bool branch = strncmp(function, "symbols_branch_", strlen("symbols_branch_")) == 0;
            const bool leas_valid = indirect || function_leas == (address || table ? 1u : 0u);
            const bool references_valid = indirect || address || function_references != 0;
            // A branch's comparison of its symbol's memory with the constant feeds a conditional jump: no setcc or cmov.
            const bool branch_valid = !branch || (function_jumps != 0 && function_selects == 0);
            if (!leas_valid || !references_valid || !branch_valid)
            {
                fprintf(stderr, "symbols shape %s: leas from rip=%u direct references=%u conditional jumps=%u setcc/cmov=%u\n", function, function_leas, function_references, function_jumps,
                        function_selects);
                valid = false;
            }
            functions += 1;
        }
        if (!more)
        {
            break;
        }
        if (close != NULL)
        {
            const size_t length = (size_t)(close - open - 1);
            valid = length < sizeof(function) && valid;
            snprintf(function, sizeof(function), "%.*s", (int)(length < sizeof(function) ? length : 0), open + 1);
            function_references = 0;
            function_leas = 0;
            function_jumps = 0;
            function_selects = 0;
            previous_lea = false;
        }
        else if (strstr(line, "R_X86_64_") != NULL)
        {
            for (unsigned k = 0; k < sizeof(symbols_sequences) / sizeof(symbols_sequences[0]); k += 1)
            {
                sequences[k] += strstr(line, symbols_sequences[k]) != NULL;
            }
            const char* pc32 = strstr(line, "R_X86_64_PC32\t");
            if (pc32 != NULL)
            {
                const char* target = pc32 + strlen("R_X86_64_PC32\t");
                for (unsigned k = 0; k < sizeof(symbols_indirect) / sizeof(symbols_indirect[0]); k += 1)
                {
                    const size_t length = strlen(symbols_indirect[k]);
                    if (strncmp(target, symbols_indirect[k], length) == 0 && (target[length] == '-' || target[length] == '+' || target[length] == '\n'))
                    {
                        fprintf(stderr, "symbols shape %s: a PC32 names %s, which the unit reaches indirectly\n", function, symbols_indirect[k]);
                        valid = false;
                    }
                }
                // A lea's own reference is its symbol's address, not a memory operand's.
                function_references += !previous_lea;
                direct_references += !previous_lea;
            }
        }
        else
        {
            unsigned long long at;
            char operation[32];
            char operands[256];
            const int fields = sscanf(line, " %llx: %31s %255s", &at, operation, operands);
            previous_lea = fields >= 2 && strcmp(operation, "lea") == 0 && fields == 3 && strstr(operands, "(%rip)") != NULL;
            function_leas += previous_lea;
            function_jumps += fields >= 2 && operation[0] == 'j' && strcmp(operation, "jmp") != 0;
            function_selects += fields >= 2 && (strncmp(operation, "set", 3) == 0 || strncmp(operation, "cmov", 4) == 0);
        }
    }
    if (stream != NULL)
    {
        valid = !ferror(stream) && valid;
        valid = fclose(stream) == 0 && valid;
    }
    for (unsigned k = 0; k < sizeof(symbols_sequences) / sizeof(symbols_sequences[0]); k += 1)
    {
        if (sequences[k] == 0)
        {
            fprintf(stderr, "symbols shape: no %s\n", symbols_sequences[k]);
            valid = false;
        }
    }
    // Seven functions of each width, two of each operation and the ten others, each seen once.
#define BUSTER_SYMBOLS_COUNT(...) +1
    const unsigned expected = 7 * (0 BUSTER_SYMBOLS_WIDTHS(BUSTER_SYMBOLS_COUNT)) + 2 * (0 BUSTER_SYMBOLS_OPERATIONS(BUSTER_SYMBOLS_COUNT)) + 10;
#undef BUSTER_SYMBOLS_COUNT
    if (functions != expected || direct_references < expected - 3)
    {
        fprintf(stderr, "symbols shape: %u functions of %u, %u direct references\n", functions, expected, direct_references);
        valid = false;
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
    char directory[] = "/tmp/buster-symbols-XXXXXX";
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
    char source[] = "tests/driver/tier1_symbols.c";
    if (valid)
    {
        char* arguments[] = { argv[2], "-std=c17", "-O2", "-DBUSTER_TIER1_SYMBOLS_ORACLE", "-c", source, "-o", oracle, NULL };
        valid = symbols_run(arguments, NULL);
    }
    for (unsigned scalar = 0; valid && scalar < 2; scalar += 1)
    {
        // Fresh outputs: an unsuccessful command must never reuse the preceding path's files.
        unlink(subject);
        unlink(program);
        unlink(assembly);
        char* compile[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", "--tier1-report", "-std=c17", "-DBUSTER_TIER1_SYMBOLS_SUBJECT", "-c", source, "-o", subject, scalar ? "--scalar" : NULL, NULL };
        char* link[] = { argv[2], oracle, subject, "-o", program, NULL };
        char* execute[] = { program, NULL };
        char* disassemble[] = { "objdump", "-dr", "--no-show-raw-insn", subject, NULL };
        valid = symbols_run(compile, NULL) && symbols_run(link, NULL) && symbols_run(execute, NULL) && symbols_run(disassemble, assembly) && symbols_shape(assembly);
        printf("Tier 1 symbols %s: %s\n", scalar ? "scalar" : "default", valid ? "pass" : "FAIL");
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
        fprintf(stderr, "symbols regression artifacts retained in %s\n", directory);
    }
    return valid ? 0 : 1;
}

#endif
