// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2554: compile this original fixture's SUBJECT by Buster's Tier 1, its ORACLE by Clang, link and run them. The default
// mode is the process supervisor. Both Tier 1 host paths must compile strictly, verify and pass the oracle's checks,
// and GNU objdump's disassembly of each named function must have the shape the issue's four patterns leave:
// - truth values across blocks (`&&`, `||`, `!` and a loop's found flag): no setcc, the comparisons are the branches;
// - constants stored: each store an immediate's, no constant made in a register first;
// - shifts by zero and multiplications by one: none;
// - a computed goto's dispatch: an indirect jump at each handler's end, no jump to the shared one;
// - and #2592's fetch, `i = *pc++` at each handler's end: the load before the increment, which adds in pc's register, so
//   that no move precedes the handlers' indirect jumps (the function's first fetch, of its parameter, may still move).

#if defined(BUSTER_TIER1_PATTERNS_SUBJECT)

// The observable arms keep the truth fixtures branches under #1382's if-conversion.
volatile unsigned long patterns_taken;

__attribute__((noinline)) long patterns_and(long a, long b, long c)
{
    long result = c + 11;
    if (a < b && b < c)
    {
        result = a + 7;
        patterns_taken += 1;
    }
    return result;
}

__attribute__((noinline)) long patterns_or(long a, long b, long c)
{
    long result = c + 11;
    if (a < b || b == c)
    {
        result = a + 7;
        patterns_taken += 1;
    }
    return result;
}

__attribute__((noinline)) long patterns_not_and(const long* p, long n)
{
    long result = -1;
    if (!(p != 0 && n > 0))
    {
        patterns_taken += 1;
    }
    else
    {
        result = p[n - 1];
    }
    return result;
}

__attribute__((noinline)) long patterns_chain(long a, long b, long c, long d)
{
    long result = d + 11;
    if (a < b && b < c && c < d)
    {
        result = a + 7;
        patterns_taken += 1;
    }
    return result;
}

__attribute__((noinline)) long patterns_flag(const long* p, long n, long k)
{
    int found = 0;
    for (long i = 0; i < n; i += 1)
    {
        if (p[i] == k)
        {
            found = 1;
            break;
        }
    }
    long result = 3;
    if (found)
    {
        result = 5;
        patterns_taken += 1;
    }
    return result;
}

struct patterns_record
{
    signed char byte;
    short word;
    int dword;
    long qword;
    long negative;
    void* pointer;
};

__attribute__((noinline)) void patterns_store(struct patterns_record* r)
{
    r->byte = -7;
    r->word = 300;
    r->dword = 0x12345678;
    r->qword = 0x7fffffff;
    r->negative = -1;
    r->pointer = 0;
}

typedef unsigned int patterns_instruction;
#define BUSTER_PATTERNS_MASK1(n, p) ((~((~(patterns_instruction)0) << (n))) << (p))
#define BUSTER_PATTERNS_OPCODE(i) ((int)(((i) >> 0) & BUSTER_PATTERNS_MASK1(7, 0)))

__attribute__((noinline)) int patterns_field(const patterns_instruction* code, const int* table)
{
    return table[BUSTER_PATTERNS_OPCODE(code[0])] + BUSTER_PATTERNS_OPCODE(code[1]);
}

__attribute__((noinline)) char* patterns_scale(char* p, unsigned long n)
{
    return p + n * sizeof(char) + n / sizeof(char);
}

__attribute__((noinline)) long patterns_dispatch(const unsigned char* code, long* stack)
{
    static void* const table[] = { &&operation_add, &&operation_sub, &&operation_halt, &&operation_push };
    long sp = 0;
    long pc = 0;
    goto* table[code[pc]];
operation_add:
    stack[sp - 2] += stack[sp - 1];
    sp -= 1;
    pc += 1;
    goto* table[code[pc]];
operation_sub:
    stack[sp - 2] -= stack[sp - 1];
    sp -= 1;
    pc += 1;
    goto* table[code[pc]];
operation_push:
    stack[sp] = code[pc + 1];
    sp += 1;
    pc += 2;
    goto* table[code[pc]];
operation_halt:
    return stack[sp - 1];
}

// #2592: Lua's vmfetch, `i = *(pc++)` and a dispatch on its low bits; a push's operand is the rest of its word.
__attribute__((noinline)) long patterns_fetch(const unsigned int* pc, long* stack)
{
    static void* const table[] = { &&fetch_add, &&fetch_push, &&fetch_halt, &&fetch_dup };
    long sp = 0;
    unsigned int i = *pc++;
    goto* table[i & 3];
fetch_add:
    stack[sp - 2] += stack[sp - 1];
    sp -= 1;
    i = *pc++;
    goto* table[i & 3];
fetch_push:
    stack[sp] = (long)(i >> 2);
    sp += 1;
    i = *pc++;
    goto* table[i & 3];
fetch_dup:
    stack[sp] = stack[sp - 1];
    sp += 1;
    i = *pc++;
    goto* table[i & 3];
fetch_halt:
    return stack[sp - 1];
}

#elif defined(BUSTER_TIER1_PATTERNS_ORACLE)

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern volatile unsigned long patterns_taken;
long patterns_and(long a, long b, long c);
long patterns_or(long a, long b, long c);
long patterns_not_and(const long* p, long n);
long patterns_chain(long a, long b, long c, long d);
long patterns_flag(const long* p, long n, long k);
struct patterns_record
{
    signed char byte;
    short word;
    int dword;
    long qword;
    long negative;
    void* pointer;
};
void patterns_store(struct patterns_record* r);
int patterns_field(const unsigned int* code, const int* table);
char* patterns_scale(char* p, unsigned long n);
long patterns_dispatch(const unsigned char* code, long* stack);
long patterns_fetch(const unsigned int* pc, long* stack);

int main(void)
{
    const long inputs[] = { LONG_MIN, -2, -1, 0, 1, 2, 3, LONG_MAX };
    const unsigned count = sizeof(inputs) / sizeof(inputs[0]);
    bool valid = true;
    for (unsigned i = 0; i < count; i += 1)
    {
        for (unsigned j = 0; j < count; j += 1)
        {
            for (unsigned k = 0; k < count; k += 1)
            {
                const long a = inputs[i];
                const long b = inputs[j];
                const long c = inputs[k];
                const long yes = (long)((unsigned long)a + 7);
                const long no = (long)((unsigned long)c + 11);
                unsigned long taken = patterns_taken;
                bool condition = a < b && b < c;
                valid = patterns_and(a, b, c) == (condition ? yes : no) && patterns_taken == taken + condition && valid;
                taken = patterns_taken;
                condition = a < b || b == c;
                valid = patterns_or(a, b, c) == (condition ? yes : no) && patterns_taken == taken + condition && valid;
                for (unsigned m = 0; m < count; m += 1)
                {
                    const long d = inputs[m];
                    taken = patterns_taken;
                    condition = a < b && b < c && c < d;
                    valid = patterns_chain(a, b, c, d) == (condition ? yes : (long)((unsigned long)d + 11)) && patterns_taken == taken + condition && valid;
                }
            }
        }
    }
    const long values[] = { 4, 9, -3, 9, 12 };
    for (long n = -1; n <= 5; n += 1)
    {
        unsigned long taken = patterns_taken;
        const bool missing = n <= 0;
        valid = patterns_not_and(values, n) == (missing ? -1 : values[n - 1]) && patterns_taken == taken + missing && valid;
        taken = patterns_taken;
        valid = patterns_not_and(0, n) == -1 && patterns_taken == taken + 1 && valid;
        for (long k = -4; k <= 13; k += 1)
        {
            bool found = false;
            for (long i = 0; i < n; i += 1)
            {
                found = found || values[i] == k;
            }
            taken = patterns_taken;
            valid = patterns_flag(values, n, k) == (found ? 5 : 3) && patterns_taken == taken + found && valid;
        }
    }
    struct patterns_record record;
    memset(&record, 0x5a, sizeof(record));
    patterns_store(&record);
    valid = record.byte == -7 && record.word == 300 && record.dword == 0x12345678 && record.qword == 0x7fffffff && record.negative == -1 && record.pointer == 0 && valid;
    int table[128];
    for (int t = 0; t < 128; t += 1)
    {
        table[t] = t * 3 + 1;
    }
    const unsigned int code[] = { 0xffffff85u, 0x00000107u };
    valid = patterns_field(code, table) == table[0x05] + 0x07 && valid;
    char bytes[64];
    valid = patterns_scale(bytes, 5) == bytes + 10 && valid;
    // push 6, push 4, sub, push 9, add, halt: 6 - 4 + 9.
    const unsigned char program[] = { 3, 6, 3, 4, 1, 3, 9, 0, 2 };
    long stack[8] = { 0 };
    valid = patterns_dispatch(program, stack) == 11 && valid;
    // push 6, push 4, dup, add, add, halt: 6 + 4 + 4.
    const unsigned int words[] = { 6 << 2 | 1, 4 << 2 | 1, 3, 0, 0, 2 };
    long fetched[8] = { 0 };
    valid = patterns_fetch(words, fetched) == 14 && valid;
    puts(valid ? "Tier 1 patterns native oracle: pass" : "Tier 1 patterns native oracle: FAIL");
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

static bool patterns_run(char* const arguments[], const char* output)
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
                perror("patterns output");
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
        fprintf(stderr, "patterns command failed: %s\n", arguments[0]);
    }
    return valid;
}

typedef enum PatternsKind
{
    PATTERNS_TRUTH,
    PATTERNS_STORE,
    PATTERNS_SHIFT,
    PATTERNS_DISPATCH,
    PATTERNS_FETCH,
} PatternsKind;

typedef struct PatternsFunction PatternsFunction;
struct PatternsFunction
{
    const char* name;
    PatternsKind kind;
};

static const PatternsFunction patterns_functions[] = {
    { "patterns_and", PATTERNS_TRUTH },
    { "patterns_or", PATTERNS_TRUTH },
    { "patterns_not_and", PATTERNS_TRUTH },
    { "patterns_chain", PATTERNS_TRUTH },
    { "patterns_flag", PATTERNS_TRUTH },
    { "patterns_store", PATTERNS_STORE },
    { "patterns_field", PATTERNS_SHIFT },
    { "patterns_scale", PATTERNS_SHIFT },
    { "patterns_dispatch", PATTERNS_DISPATCH },
    { "patterns_fetch", PATTERNS_FETCH },
};

#define BUSTER_PATTERNS_COUNT (sizeof(patterns_functions) / sizeof(patterns_functions[0]))
// A function's instructions that the disassembly keeps, at most.
#define BUSTER_PATTERNS_INSTRUCTIONS 512

typedef struct PatternsInstruction PatternsInstruction;
struct PatternsInstruction
{
    unsigned long long address;
    char operation[32];
    char operands[96];
};

typedef struct PatternsBody PatternsBody;
struct PatternsBody
{
    PatternsInstruction instructions[BUSTER_PATTERNS_INSTRUCTIONS];
    unsigned count;
    unsigned definitions;
};

// Whether the operands are `$IMMEDIATE,MEMORY` (a store of an immediate), `REGISTER,MEMORY` (a store of a register) or
// `$IMMEDIATE,REGISTER`/`%r,%r` with an xor (a constant made in a register).
static bool patterns_memory(const char* operand)
{
    return strchr(operand, '(') != NULL;
}

static bool patterns_shape(const PatternsFunction* function, const PatternsBody* body)
{
    unsigned sets = 0;
    unsigned shifts = 0;
    unsigned immediates = 0;
    unsigned registers = 0;
    unsigned constants = 0;
    unsigned indirect = 0;
    unsigned to_indirect = 0;
    // Indirect jumps with no register-to-register move right before them, where an edge's moves go, and increments of 4
    // in place (an add, where a lea into another register would leave the edge a move back).
    unsigned clean = 0;
    unsigned increments = 0;
    for (unsigned i = 0; i < body->count; i += 1)
    {
        const PatternsInstruction* instruction = &body->instructions[i];
        const char* operation = instruction->operation;
        const char* operands = instruction->operands;
        const char* comma = strrchr(operands, ',');
        const char* destination = comma != NULL ? comma + 1 : operands;
        sets += strncmp(operation, "set", 3) == 0;
        shifts += (strncmp(operation, "shl", 3) == 0 || strncmp(operation, "shr", 3) == 0 || strncmp(operation, "sar", 3) == 0) && strncmp(operands, "$0x0,", 5) == 0;
        const bool move = strncmp(operation, "mov", 3) == 0 && strncmp(operation, "movz", 4) != 0 && strncmp(operation, "movs", 4) != 0;
        immediates += move && operands[0] == '$' && patterns_memory(destination);
        registers += move && operands[0] == '%' && patterns_memory(destination);
        constants += (move && operands[0] == '$' && !patterns_memory(destination)) || (strncmp(operation, "xor", 3) == 0 && comma != NULL && strncmp(operands, destination, (size_t)(comma - operands)) == 0);
        const bool jump = strcmp(operation, "jmp") == 0;
        indirect += jump && operands[0] == '*';
        increments += strncmp(operation, "add", 3) == 0 && strncmp(operands, "$0x4,%", 6) == 0;
        const PatternsInstruction* before = i != 0 ? &body->instructions[i - 1] : NULL;
        const bool moved = before != NULL && strcmp(before->operation, "mov") == 0 && before->operands[0] == '%' && !patterns_memory(before->operands);
        clean += jump && operands[0] == '*' && !moved;
        if (jump && operands[0] != '*')
        {
            const unsigned long long target = strtoull(operands, NULL, 16);
            for (unsigned k = 0; k < body->count; k += 1)
            {
                to_indirect += body->instructions[k].address == target && strcmp(body->instructions[k].operation, "jmp") == 0 && body->instructions[k].operands[0] == '*';
            }
        }
    }
    bool valid = body->definitions == 1;
    switch (function->kind)
    {
    case PATTERNS_TRUTH:
        valid = valid && sets == 0;
        break;
    case PATTERNS_STORE:
        valid = valid && immediates == 6 && registers == 0 && constants == 0;
        break;
    case PATTERNS_SHIFT:
        valid = valid && shifts == 0;
        break;
    case PATTERNS_DISPATCH:
        valid = valid && indirect >= 4 && to_indirect == 0;
        break;
    case PATTERNS_FETCH:
        valid = valid && indirect >= 4 && clean >= 3 && increments >= 3;
        break;
    }
    if (!valid)
    {
        fprintf(stderr, "patterns shape %s: definitions=%u setcc=%u shifts-by-zero=%u immediate-stores=%u register-stores=%u register-constants=%u indirect-jumps=%u jumps-to-them=%u clean-jumps=%u increments=%u\n",
                function->name, body->definitions, sets, shifts, immediates, registers, constants, indirect, to_indirect, clean, increments);
    }
    return valid;
}

static bool patterns_shapes(const char* path)
{
    static PatternsBody bodies[BUSTER_PATTERNS_COUNT];
    memset(bodies, 0, sizeof(bodies));
    unsigned current = BUSTER_PATTERNS_COUNT;
    FILE* stream = fopen(path, "r");
    bool valid = stream != NULL;
    char line[4096];
    while (valid && fgets(line, sizeof(line), stream) != NULL)
    {
        const char* open = strchr(line, '<');
        const char* close = open != NULL ? strstr(open, ">:") : NULL;
        if (close != NULL)
        {
            current = BUSTER_PATTERNS_COUNT;
            const size_t length = (size_t)(close - open - 1);
            for (unsigned i = 0; i < BUSTER_PATTERNS_COUNT; i += 1)
            {
                if (strlen(patterns_functions[i].name) == length && memcmp(open + 1, patterns_functions[i].name, length) == 0)
                {
                    current = i;
                    bodies[i].definitions += 1;
                }
            }
        }
        else if (current < BUSTER_PATTERNS_COUNT)
        {
            PatternsInstruction instruction = { 0 };
            const int fields = sscanf(line, " %llx: %31s %95s", &instruction.address, instruction.operation, instruction.operands);
            if (fields >= 2 && bodies[current].count < BUSTER_PATTERNS_INSTRUCTIONS)
            {
                bodies[current].instructions[bodies[current].count] = instruction;
                bodies[current].count += 1;
            }
        }
    }
    if (stream != NULL)
    {
        valid = !ferror(stream) && valid;
        valid = fclose(stream) == 0 && valid;
    }
    for (unsigned i = 0; i < BUSTER_PATTERNS_COUNT; i += 1)
    {
        valid = patterns_shape(&patterns_functions[i], &bodies[i]) && valid;
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
    char directory[] = "/tmp/buster-patterns-XXXXXX";
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
    char source[] = "tests/driver/tier1_patterns.c";
    if (valid)
    {
        char* arguments[] = { argv[2], "-std=gnu17", "-O2", "-DBUSTER_TIER1_PATTERNS_ORACLE", "-c", source, "-o", oracle, NULL };
        valid = patterns_run(arguments, NULL);
    }
    for (unsigned scalar = 0; valid && scalar < 2; scalar += 1)
    {
        // Fresh outputs: an unsuccessful command must never reuse the preceding path's files.
        unlink(subject);
        unlink(program);
        unlink(assembly);
        char* compile[] = { argv[1], "--tier1", "--tier1-strict", "--verify", "--lanes", "1", "--tier1-report", "-std=gnu17", "-DBUSTER_TIER1_PATTERNS_SUBJECT", "-c", source, "-o", subject, scalar ? "--scalar" : NULL, NULL };
        char* link[] = { argv[2], oracle, subject, "-o", program, NULL };
        char* execute[] = { program, NULL };
        char* disassemble[] = { "objdump", "-d", "--no-show-raw-insn", subject, NULL };
        valid = patterns_run(compile, NULL) && patterns_run(link, NULL) && patterns_run(execute, NULL) && patterns_run(disassemble, assembly) && patterns_shapes(assembly);
        printf("Tier 1 patterns %s: %s\n", scalar ? "scalar" : "default", valid ? "pass" : "FAIL");
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
        fprintf(stderr, "patterns regression artifacts retained in %s\n", directory);
    }
    return valid ? 0 : 1;
}

#endif
