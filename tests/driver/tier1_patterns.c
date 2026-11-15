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
// - #2592's fetch, `i = *pc++` at each handler's end: the load before the increment, which adds in pc's register, so
//   that no move precedes the handlers' indirect jumps (the function's first fetch, of its parameter, may still move);
// - #2768's handler that calls in one arm, as Lua's OP_GETTABUP does: the loop's values it spills around the call
//   wait in their slots for their next use, so that the join after the call reloads none of them into a register the
//   loop keeps another value in, and every handler's indirect jump follows its table's load at once;
// - and #2676's selects of comparisons (a maximum, a clamp, a loop's conditional sum, a comparison of a load with an
//   immediate and with a register, a doubleword's comparison choosing quadwords, an unsigned one): a cmov of the
//   comparison's flags for each, no setcc.

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

// #2768: Lua's OP_GETTABUP, a lookup that calls in one arm and a miss that calls in the other, and two handlers that use
// the loop's values more than the fetch uses pc. More values live across the calls than there are callee-saved
// registers.
struct patterns_state
{
    long* base;
    const long* constants;
    long** upvalues;
    long trap;
};
long* patterns_lookup(long* table, long key);
long patterns_miss(struct patterns_state* s, long* table, long key, long* ra);
long patterns_hook(struct patterns_state* s, const unsigned int* pc);

#define BUSTER_PATTERNS_A(i) (((i) >> 2) & 7)
#define BUSTER_PATTERNS_B(i) (((i) >> 5) & 7)
#define BUSTER_PATTERNS_K(i) (((i) >> 8) & 7)

__attribute__((noinline)) long patterns_spilled(struct patterns_state* s, const unsigned int* pc)
{
    static void* const table[] = { &&spilled_get, &&spilled_add, &&spilled_halt, &&spilled_push };
    long* base = s->base;
    const long* constants = s->constants;
    long** upvalues = s->upvalues;
    long trap = s->trap;
    unsigned int i = *pc++;
    goto* table[i & 3];
spilled_get:
    {
        long* ra = base + BUSTER_PATTERNS_A(i);
        long* upvalue = upvalues[BUSTER_PATTERNS_B(i) & 3];
        long key = constants[BUSTER_PATTERNS_K(i)];
        long* slot = 0;
        if (upvalue[0] == 0x45)
        {
            slot = patterns_lookup(upvalue, key);
        }
        if (slot != 0)
        {
            *ra = *slot;
        }
        else
        {
            trap = patterns_miss(s, upvalue, key, ra);
        }
        if (trap != 0)
        {
            trap = patterns_hook(s, pc);
            base = s->base;
        }
        i = *pc++;
        goto* table[i & 3];
    }
spilled_add:
    base[BUSTER_PATTERNS_A(i)] += base[BUSTER_PATTERNS_B(i)] + constants[0] + upvalues[0][1] + constants[6] * upvalues[2][1] + (constants[7] ^ upvalues[3][1]) + trap;
    i = *pc++;
    goto* table[i & 3];
spilled_push:
    base[BUSTER_PATTERNS_A(i)] = constants[BUSTER_PATTERNS_B(i)] + constants[2] + upvalues[1][1] + constants[5] * upvalues[2][2] + (constants[4] ^ upvalues[3][2]) + trap;
    i = *pc++;
    goto* table[i & 3];
spilled_halt:
    return base[BUSTER_PATTERNS_A(i)];
}

__attribute__((noinline)) long patterns_max(long a, long b)
{
    return a > b ? a : b;
}

__attribute__((noinline)) int patterns_clamp(int x)
{
    return x < 0 ? 0 : x > 255 ? 255 : x;
}

__attribute__((noinline)) long patterns_sum(const int* a, int n)
{
    long s = 0;
    for (int i = 0; i < n; i += 1)
    {
        if (a[i] > 128)
        {
            s += a[i];
        }
    }
    return s;
}

__attribute__((noinline)) int patterns_loaded(const int* p, int x, int y)
{
    return *p == 7 ? x : y;
}

__attribute__((noinline)) int patterns_loaded_other(const int* p, int q, int x, int y)
{
    return *p < q ? x : y;
}

__attribute__((noinline)) long patterns_mixed(int a, int b, long x, long y)
{
    return a < b ? x : y;
}

__attribute__((noinline)) unsigned patterns_unsigned(unsigned a, unsigned b, unsigned x)
{
    return a >= b ? x : 0;
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
long patterns_max(long a, long b);
int patterns_clamp(int x);
long patterns_sum(const int* a, int n);
int patterns_loaded(const int* p, int x, int y);
int patterns_loaded_other(const int* p, int q, int x, int y);
long patterns_mixed(int a, int b, long x, long y);
unsigned patterns_unsigned(unsigned a, unsigned b, unsigned x);
struct patterns_state
{
    long* base;
    const long* constants;
    long** upvalues;
    long trap;
};
long patterns_spilled(struct patterns_state* s, const unsigned int* pc);

#define BUSTER_PATTERNS_A(i) (((i) >> 2) & 7)
#define BUSTER_PATTERNS_B(i) (((i) >> 5) & 7)
#define BUSTER_PATTERNS_K(i) (((i) >> 8) & 7)
#define BUSTER_PATTERNS_WORD(operation, a, b, k) ((unsigned int)(operation) | (unsigned int)(a) << 2 | (unsigned int)(b) << 5 | (unsigned int)(k) << 8)

// patterns_spilled's callees: a hit for a key that isn't negative, a miss that stores its own result and asks for the
// hook where the key is odd, and a hook that counts its calls and the instructions they come after.
static unsigned long spilled_calls;
static long spilled_positions;
static const unsigned int* spilled_program;

long* patterns_lookup(long* table, long key)
{
    spilled_calls += 1;
    return key >= 0 ? &table[1 + (key & 1)] : 0;
}

long patterns_miss(struct patterns_state* s, long* table, long key, long* ra)
{
    spilled_calls += 1;
    *ra = key * 3 + table[0] + (s->base == ra);
    return key & 1;
}

long patterns_hook(struct patterns_state* s, const unsigned int* pc)
{
    spilled_calls += 1;
    spilled_positions += pc - spilled_program;
    return s->trap;
}

// patterns_spilled's semantics by a switch, for Clang to compile.
static long spilled_reference(struct patterns_state* s, const unsigned int* pc)
{
    long* base = s->base;
    const long* constants = s->constants;
    long** upvalues = s->upvalues;
    long trap = s->trap;
    long result = 0;
    bool running = true;
    while (running)
    {
        const unsigned int i = *pc++;
        switch (i & 3)
        {
        case 0:
        {
            long* ra = base + BUSTER_PATTERNS_A(i);
            long* upvalue = upvalues[BUSTER_PATTERNS_B(i) & 3];
            const long key = constants[BUSTER_PATTERNS_K(i)];
            long* slot = upvalue[0] == 0x45 ? patterns_lookup(upvalue, key) : 0;
            if (slot != 0)
            {
                *ra = *slot;
            }
            else
            {
                trap = patterns_miss(s, upvalue, key, ra);
            }
            if (trap != 0)
            {
                trap = patterns_hook(s, pc);
                base = s->base;
            }
            break;
        }
        case 1:
            base[BUSTER_PATTERNS_A(i)] += base[BUSTER_PATTERNS_B(i)] + constants[0] + upvalues[0][1] + constants[6] * upvalues[2][1] + (constants[7] ^ upvalues[3][1]) + trap;
            break;
        case 3:
            base[BUSTER_PATTERNS_A(i)] = constants[BUSTER_PATTERNS_B(i)] + constants[2] + upvalues[1][1] + constants[5] * upvalues[2][2] + (constants[4] ^ upvalues[3][2]) + trap;
            break;
        default:
            result = base[BUSTER_PATTERNS_A(i)];
            running = false;
            break;
        }
    }
    return result;
}

// One run of patterns_spilled or its reference over fresh state: the result, the registers' final values and the calls.
static bool spilled_run(bool reference, long* result, long* registers, unsigned long* calls, long* positions)
{
    static const unsigned int program[] = {
        BUSTER_PATTERNS_WORD(0, 1, 0, 2),
        BUSTER_PATTERNS_WORD(0, 2, 1, 3),
        BUSTER_PATTERNS_WORD(3, 3, 4, 0),
        BUSTER_PATTERNS_WORD(1, 1, 3, 0),
        BUSTER_PATTERNS_WORD(0, 4, 2, 1),
        BUSTER_PATTERNS_WORD(1, 4, 1, 0),
        BUSTER_PATTERNS_WORD(0, 5, 3, 3),
        BUSTER_PATTERNS_WORD(1, 5, 4, 0),
        BUSTER_PATTERNS_WORD(2, 4, 0, 0),
    };
    static const long constants[8] = { 2, -7, 5, 8, 11, -3, 4, 6 };
    long tables[4][4] = {
        { 0x45, 10, 20, 30 },
        { 3, 40, 50, 60 },
        { 0x45, 70, 80, 90 },
        { 0x45, 100, 110, 120 },
    };
    long* upvalues[4] = { tables[0], tables[1], tables[2], tables[3] };
    for (unsigned r = 0; r < 8; r += 1)
    {
        registers[r] = (long)r * 1000 + 1;
    }
    struct patterns_state state = {
        .base = registers,
        .constants = constants,
        .upvalues = upvalues,
        .trap = 0,
    };
    spilled_program = program;
    spilled_calls = 0;
    spilled_positions = 0;
    *result = reference ? spilled_reference(&state, program) : patterns_spilled(&state, program);
    *calls = spilled_calls;
    *positions = spilled_positions;
    return state.base == registers;
}

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
                // #2676's selects, on the inputs and their truncations.
                const int x = (int)a;
                const int y = (int)b;
                const int z = (int)c;
                valid = patterns_mixed(x, y, b, c) == (x < y ? b : c) && valid;
                valid = patterns_unsigned((unsigned)x, (unsigned)y, (unsigned)z) == ((unsigned)x >= (unsigned)y ? (unsigned)z : 0u) && valid;
                valid = patterns_loaded_other(&x, y, z, (int)c ^ 5) == (x < y ? z : ((int)c ^ 5)) && valid;
                valid = patterns_max(a, b) == (a > b ? a : b) && valid;
            }
        }
    }
    for (int x = -300; x <= 300; x += 1)
    {
        valid = patterns_clamp(x) == (x < 0 ? 0 : x > 255 ? 255 : x) && valid;
        valid = patterns_loaded(&x, x * 3, x - 1) == (x == 7 ? x * 3 : x - 1) && valid;
    }
    const int summed[] = { 0, 128, 129, 255, -129, 300, 7, 129 };
    long sum = 0;
    for (int n = 0; n <= 8; n += 1)
    {
        valid = patterns_sum(summed, n) == sum && valid;
        sum += n < 8 && summed[n] > 128 ? summed[n] : 0;
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
    long spilled_results[2];
    long spilled_registers[2][8];
    unsigned long spilled_counts[2];
    long spilled_hooks[2];
    for (unsigned reference = 0; reference < 2; reference += 1)
    {
        valid = spilled_run(reference != 0, &spilled_results[reference], spilled_registers[reference], &spilled_counts[reference], &spilled_hooks[reference]) && valid;
    }
    // Three lookups, two of which hit, and two misses, one of which asks for the hook: six calls.
    valid = spilled_results[0] == spilled_results[1] && memcmp(spilled_registers[0], spilled_registers[1], sizeof(spilled_registers[0])) == 0 && spilled_counts[0] == spilled_counts[1] && spilled_counts[0] == 6 && spilled_hooks[0] == spilled_hooks[1] && spilled_hooks[0] != 0 && valid;
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
    PATTERNS_SPILLED,
    PATTERNS_SELECT,
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
    { "patterns_spilled", PATTERNS_SPILLED },
    { "patterns_max", PATTERNS_SELECT },
    { "patterns_clamp", PATTERNS_SELECT },
    { "patterns_sum", PATTERNS_SELECT },
    { "patterns_loaded", PATTERNS_SELECT },
    { "patterns_loaded_other", PATTERNS_SELECT },
    { "patterns_mixed", PATTERNS_SELECT },
    { "patterns_unsigned", PATTERNS_SELECT },
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
    unsigned moves = 0;
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
    // Indirect jumps right after the load of their target, with nothing between them (#2768).
    unsigned loaded = 0;
    for (unsigned i = 0; i < body->count; i += 1)
    {
        const PatternsInstruction* instruction = &body->instructions[i];
        const char* operation = instruction->operation;
        const char* operands = instruction->operands;
        const char* comma = strrchr(operands, ',');
        const char* destination = comma != NULL ? comma + 1 : operands;
        sets += strncmp(operation, "set", 3) == 0;
        moves += strncmp(operation, "cmov", 4) == 0;
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
        const char* loaded_comma = before != NULL ? strrchr(before->operands, ',') : NULL;
        loaded += jump && operands[0] == '*' && loaded_comma != NULL && strcmp(before->operation, "mov") == 0 && patterns_memory(before->operands) && strcmp(loaded_comma + 1, operands + 1) == 0;
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
    case PATTERNS_SPILLED:
        valid = valid && indirect >= 4 && loaded + 1 >= indirect;
        break;
    case PATTERNS_SELECT:
        valid = valid && sets == 0 && moves >= 1;
        break;
    }
    if (!valid)
    {
        fprintf(stderr, "patterns shape %s: definitions=%u setcc=%u cmov=%u shifts-by-zero=%u immediate-stores=%u register-stores=%u register-constants=%u indirect-jumps=%u jumps-to-them=%u clean-jumps=%u increments=%u loaded-jumps=%u\n",
                function->name, body->definitions, sets, moves, shifts, immediates, registers, constants, indirect, to_indirect, clean, increments, loaded);
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
