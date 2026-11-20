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
// - #2676's selects of comparisons (a maximum, a clamp, a loop's conditional sum, a comparison of a load with an
//   immediate and with a register, a doubleword's comparison choosing quadwords, an unsigned one): a cmov of the
//   comparison's flags for each, no setcc;
// - #2791's selects of floating comparisons (<, a negated >, a loop's conditional count by >=, and a sign's nested `?:`,
//   whose outer test stays a branch): ucomis and a cmov or jump of its flags for each, no setcc (== and != keep their
//   truth values, whose selects the oracle checks on unordered operands too);
// - #2745's objects a vector's value initializes whole, as the intrinsics' wrappers are (a struct's, a union's, a
//   compound literal's): no store of zeros before the vector's, while a volatile object and a union whose designator
//   moves to another member after the vector's (whose rest clang leaves zeros) keep their eight;
// - #2784's str_gsub loop, whose body precedes its header: no value stored back where the instruction before loaded
//   it from, as a range that waits in its slot (#2768) and is evicted after its reload did;
// - and #2870's pager, SQLite's pagerWriteLargeSector cut down: a loop's bound reloaded from its slot at the loop's head
//   and evicted again two blocks later, in that run of single-predecessor blocks, whose store the emitter leaves out as
//   the slot already holds the value; the code's certificate must prove the slot from the run's entries (checked by
//   --verify, and natively against the oracle).

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

// #2784: Lua's str_gsub, a loop that replaces a match through a call or copies a plain character and advances the
// source. Its body precedes its header in layout. The pattern waits in its slot until the call reads it, and its
// eviction after the reload stored it back into the same slot. #2834: the latch that joins n and lastmatch is laid out
// between the body and the header, and the body's ranges of the two took registers crossed against the header's, which
// each iteration swapped through r11; they now take their registers where the loop is entered.
struct patterns_buffer
{
    char* data;
    unsigned long size;
    unsigned long capacity;
};
const char* patterns_match(void* state, const char* s, const char* p);
void patterns_add_value(void* state, struct patterns_buffer* b, const char* s, const char* e, int tr);
void patterns_grow(struct patterns_buffer* b);

__attribute__((noinline)) long patterns_gsub(void* state, struct patterns_buffer* b, const char* src, unsigned long length, const char* p, long max_s, int tr)
{
    const char* end = src + length;
    const char* lastmatch = 0;
    const int anchor = *p == '^';
    long n = 0;
    if (anchor)
    {
        p += 1;
    }
    while (n < max_s)
    {
        const char* e = patterns_match(state, src, p);
        if (e != 0 && e != lastmatch)
        {
            n += 1;
            patterns_add_value(state, b, src, e, tr);
            src = lastmatch = e;
        }
        else if (src < end)
        {
            if (b->size >= b->capacity)
            {
                patterns_grow(b);
            }
            b->data[b->size++] = *src++;
        }
        else
        {
            break;
        }
        if (anchor)
        {
            break;
        }
    }
    while (src < end)
    {
        if (b->size >= b->capacity)
        {
            patterns_grow(b);
        }
        b->data[b->size++] = *src++;
    }
    return n;
}

struct patterns_pager
{
    int sector;
    long long page;
    unsigned pages;
    void* journal;
};
struct patterns_page
{
    struct patterns_pager* pager;
    unsigned number;
    unsigned short flags;
};
int patterns_journaled(void* journal, unsigned number);
int patterns_fetch_page(struct patterns_pager* pager, unsigned number, struct patterns_page** page);
struct patterns_page* patterns_find(struct patterns_pager* pager, unsigned number);

__attribute__((noinline)) long patterns_sector(struct patterns_page* page)
{
    struct patterns_pager* pager = page->pager;
    const unsigned per_sector = (unsigned)(pager->sector / pager->page);
    const unsigned first = ((page->number - 1) & ~(per_sector - 1)) + 1;
    int count = 0;
    int rc = 0;
    int sync = 0;
    if (page->number > pager->pages)
    {
        count = (int)(page->number - first) + 1;
    }
    for (int i = 0; i < count && rc == 0; i += 1)
    {
        const unsigned number = first + (unsigned)i;
        struct patterns_page* other;
        if (number == page->number || !patterns_journaled(pager->journal, number))
        {
            rc = patterns_fetch_page(pager, number, &other);
        }
        else if ((other = patterns_find(pager, number)) != 0)
        {
            if ((other->flags & 8) != 0)
            {
                sync = 1;
            }
        }
    }
    long touched = 0;
    if (sync)
    {
        for (int i = 0; i < count; i += 1)
        {
            touched += 1;
        }
    }
    return rc * 1000 + touched;
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

__attribute__((noinline)) int patterns_float_less(double a, double b, int x, int y)
{
    return a < b ? x : y;
}

__attribute__((noinline)) long patterns_float_not_greater(float a, float b, long x, long y)
{
    return !(a > b) ? x : y;
}

__attribute__((noinline)) long patterns_float_count(const double* p, int n, double t)
{
    long c = 0;
    for (int i = 0; i < n; i += 1)
    {
        if (p[i] >= t)
        {
            c += 3;
        }
        else
        {
            c -= 1;
        }
    }
    return c;
}

__attribute__((noinline)) int patterns_float_sign(double x)
{
    return x < 0 ? -1 : x > 0 ? 1 : 0;
}

__attribute__((noinline)) int patterns_float_equal(double a, double b, int x, int y)
{
    return a == b ? x : y;
}

__attribute__((noinline)) long patterns_float_unequal(float a, float b, long x)
{
    return a != b ? x : 0;
}

typedef long long patterns_vector __attribute__((vector_size(64)));
typedef struct
{
    patterns_vector v;
} patterns_wrapper;
typedef union
{
    patterns_vector v;
    unsigned long long q[8];
} patterns_lanes;

__attribute__((noinline)) void patterns_whole_literal(patterns_wrapper* out, const patterns_vector* p)
{
    *out = (patterns_wrapper){ .v = *p };
}

__attribute__((noinline)) void patterns_whole_declaration(patterns_wrapper* out, const patterns_vector* p)
{
    patterns_wrapper r = { *p };
    *out = r;
}

__attribute__((noinline)) void patterns_whole_union(patterns_lanes* out, const patterns_vector* p)
{
    patterns_lanes r = { .v = *p };
    *out = r;
}

__attribute__((noinline)) unsigned long long patterns_whole_member(const patterns_vector* p)
{
    patterns_lanes r = { .v = *p };
    return r.q[3] ^ r.q[6];
}

// The override is the point: clang's -Winitializer-overrides would say so.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
__attribute__((noinline)) void patterns_whole_override(patterns_lanes* out, const patterns_vector* p)
{
    patterns_lanes r = { .v = *p, .q[1] = 5 };
    *out = r;
}
#pragma clang diagnostic pop

__attribute__((noinline)) void patterns_whole_volatile(patterns_wrapper* out, const patterns_vector* p)
{
    volatile patterns_wrapper r = { *p };
    *out = r;
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
int patterns_float_less(double a, double b, int x, int y);
long patterns_float_not_greater(float a, float b, long x, long y);
long patterns_float_count(const double* p, int n, double t);
int patterns_float_sign(double x);
int patterns_float_equal(double a, double b, int x, int y);
long patterns_float_unequal(float a, float b, long x);
struct patterns_state
{
    long* base;
    const long* constants;
    long** upvalues;
    long trap;
};
long patterns_spilled(struct patterns_state* s, const unsigned int* pc);
struct patterns_buffer
{
    char* data;
    unsigned long size;
    unsigned long capacity;
};
long patterns_gsub(void* state, struct patterns_buffer* b, const char* src, unsigned long length, const char* p, long max_s, int tr);
struct patterns_pager
{
    int sector;
    long long page;
    unsigned pages;
    void* journal;
};
struct patterns_page
{
    struct patterns_pager* pager;
    unsigned number;
    unsigned short flags;
};
long patterns_sector(struct patterns_page* page);
typedef long long patterns_vector __attribute__((vector_size(64)));
typedef struct
{
    patterns_vector v;
} patterns_wrapper;
typedef union
{
    patterns_vector v;
    unsigned long long q[8];
} patterns_lanes;
void patterns_whole_literal(patterns_wrapper* out, const patterns_vector* p);
void patterns_whole_declaration(patterns_wrapper* out, const patterns_vector* p);
void patterns_whole_union(patterns_lanes* out, const patterns_vector* p);
unsigned long long patterns_whole_member(const patterns_vector* p);
void patterns_whole_override(patterns_lanes* out, const patterns_vector* p);
void patterns_whole_volatile(patterns_wrapper* out, const patterns_vector* p);

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

// patterns_gsub's callees: a match of a run of the pattern's character, a replacement that appends `<`, the run's length
// and the replacement's digit, `>`, and a buffer that grows in steps up to its storage. Each counts its calls, twice
// with a state.
#define BUSTER_PATTERNS_GSUB_STORAGE 256
static unsigned long gsub_calls;
void patterns_grow(struct patterns_buffer* b);

const char* patterns_match(void* state, const char* s, const char* p)
{
    gsub_calls += 1 + (state != 0);
    const char* e = s;
    while (*p != 0 && *e == *p)
    {
        e += 1;
    }
    return e != s ? e : 0;
}

void patterns_add_value(void* state, struct patterns_buffer* b, const char* s, const char* e, int tr)
{
    gsub_calls += 1 + (state != 0);
    const char text[] = { '<', (char)('0' + (e - s)), (char)('0' + tr), '>' };
    for (unsigned i = 0; i < sizeof(text); i += 1)
    {
        if (b->size >= b->capacity)
        {
            patterns_grow(b);
        }
        b->data[b->size++] = text[i];
    }
}

void patterns_grow(struct patterns_buffer* b)
{
    gsub_calls += 1;
    b->capacity = b->capacity * 2 < BUSTER_PATTERNS_GSUB_STORAGE ? b->capacity * 2 : BUSTER_PATTERNS_GSUB_STORAGE;
}

// patterns_gsub's semantics, for Clang to compile.
static long gsub_reference(void* state, struct patterns_buffer* b, const char* src, unsigned long length, const char* p, long max_s, int tr)
{
    const char* end = src + length;
    const char* lastmatch = 0;
    const bool anchor = *p == '^';
    p += anchor;
    long n = 0;
    bool running = true;
    while (running && n < max_s)
    {
        const char* e = patterns_match(state, src, p);
        if (e != 0 && e != lastmatch)
        {
            n += 1;
            patterns_add_value(state, b, src, e, tr);
            src = lastmatch = e;
        }
        else if (src < end)
        {
            if (b->size >= b->capacity)
            {
                patterns_grow(b);
            }
            b->data[b->size++] = *src++;
        }
        else
        {
            running = false;
        }
        running = running && !anchor;
    }
    while (src < end)
    {
        if (b->size >= b->capacity)
        {
            patterns_grow(b);
        }
        b->data[b->size++] = *src++;
    }
    return n;
}

// patterns_gsub against its reference on each source, pattern and limit: the same count, text, buffer and calls.
static bool gsub_runs(void)
{
    static const char* const sources[] = { "", "aab-ab--b", "bbbb", "a-b-a-b-aaa", "--ab" };
    static const char* const patterns[] = { "a", "^a", "b", "-", "^-", "z" };
    static const long limits[] = { 0, 1, 2, 100 };
    bool valid = true;
    for (unsigned i = 0; i < sizeof(sources) / sizeof(sources[0]); i += 1)
    {
        for (unsigned j = 0; j < sizeof(patterns) / sizeof(patterns[0]); j += 1)
        {
            for (unsigned k = 0; k < sizeof(limits) / sizeof(limits[0]); k += 1)
            {
                char storage[2][BUSTER_PATTERNS_GSUB_STORAGE];
                struct patterns_buffer buffers[2];
                long counts[2];
                unsigned long calls[2];
                for (unsigned reference = 0; reference < 2; reference += 1)
                {
                    memset(storage[reference], 0, sizeof(storage[reference]));
                    buffers[reference] = (struct patterns_buffer){ .data = storage[reference], .size = 0, .capacity = 4 };
                    gsub_calls = 0;
                    void* state = k == 2 ? &buffers[reference] : 0;
                    const unsigned long length = strlen(sources[i]);
                    const int tr = (int)(i + j) % 10;
                    counts[reference] = reference != 0 ? gsub_reference(state, &buffers[reference], sources[i], length, patterns[j], limits[k], tr) :
                                                         patterns_gsub(state, &buffers[reference], sources[i], length, patterns[j], limits[k], tr);
                    calls[reference] = gsub_calls;
                }
                valid = counts[0] == counts[1] && calls[0] == calls[1] && buffers[0].size == buffers[1].size && buffers[0].capacity == buffers[1].capacity &&
                        memcmp(storage[0], storage[1], sizeof(storage[0])) == 0 && valid;
            }
        }
    }
    return valid;
}

// patterns_sector's callees: a journal that holds every third page, a fetch that fails on the journal's own number,
// and a lookup that finds the odd pages, every fourth one needing a sync. Each counts its calls, and the fetch sums the
// numbers it took.
#define BUSTER_PATTERNS_SECTOR_PAGES 64
static struct patterns_page sector_pages[BUSTER_PATTERNS_SECTOR_PAGES];
static unsigned long sector_calls;
static unsigned long sector_fetched;

int patterns_journaled(void* journal, unsigned number)
{
    sector_calls += 1;
    return journal != 0 && number % 3 == 0;
}

int patterns_fetch_page(struct patterns_pager* pager, unsigned number, struct patterns_page** page)
{
    sector_calls += 1;
    sector_fetched += number;
    *page = &sector_pages[number % BUSTER_PATTERNS_SECTOR_PAGES];
    return pager->journal != 0 && number == *(const unsigned*)pager->journal ? 7 : 0;
}

struct patterns_page* patterns_find(struct patterns_pager* pager, unsigned number)
{
    sector_calls += 1 + (pager->journal != 0);
    struct patterns_page* page = &sector_pages[number % BUSTER_PATTERNS_SECTOR_PAGES];
    page->flags = (unsigned short)(number % 4 == 1 ? 8 : 0);
    return number % 2 != 0 ? page : 0;
}

// patterns_sector's semantics, for Clang to compile.
static long sector_reference(struct patterns_page* page)
{
    struct patterns_pager* pager = page->pager;
    const unsigned per_sector = (unsigned)(pager->sector / pager->page);
    const unsigned first = ((page->number - 1) & ~(per_sector - 1)) + 1;
    const int count = page->number > pager->pages ? (int)(page->number - first) + 1 : 0;
    int rc = 0;
    bool sync = false;
    for (int i = 0; i < count && rc == 0; i += 1)
    {
        const unsigned number = first + (unsigned)i;
        struct patterns_page* other = 0;
        if (number == page->number || !patterns_journaled(pager->journal, number))
        {
            rc = patterns_fetch_page(pager, number, &other);
        }
        else
        {
            other = patterns_find(pager, number);
            sync = sync || (other != 0 && (other->flags & 8) != 0);
        }
    }
    return rc * 1000 + (sync ? count : 0);
}

// patterns_sector against its reference on each sector size, page and journal: the same result, calls and fetches.
static bool sector_runs(void)
{
    static const int sectors[] = { 512, 4096, 8192 };
    static const unsigned numbers[] = { 1, 5, 9, 17, 23, 32 };
    static const unsigned failing[] = { 0, 3, 21, 31 };
    bool valid = true;
    for (unsigned i = 0; i < sizeof(sectors) / sizeof(sectors[0]); i += 1)
    {
        for (unsigned j = 0; j < sizeof(numbers) / sizeof(numbers[0]); j += 1)
        {
            for (unsigned k = 0; k <= sizeof(failing) / sizeof(failing[0]); k += 1)
            {
                long results[2];
                unsigned long calls[2];
                unsigned long fetched[2];
                for (unsigned reference = 0; reference < 2; reference += 1)
                {
                    struct patterns_pager pager = { .sector = sectors[i], .page = 512, .pages = j % 3 == 0 ? 0 : j % 3 == 1 ? numbers[j] - 1 : 1000, .journal = k == 0 ? 0 : (void*)&failing[k - 1] };
                    struct patterns_page page = { .pager = &pager, .number = numbers[j], .flags = 0 };
                    sector_calls = 0;
                    sector_fetched = 0;
                    results[reference] = reference != 0 ? sector_reference(&page) : patterns_sector(&page);
                    calls[reference] = sector_calls;
                    fetched[reference] = sector_fetched;
                }
                valid = results[0] == results[1] && calls[0] == calls[1] && fetched[0] == fetched[1] && valid;
            }
        }
    }
    return valid;
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
    // #2791's floating selects, on NaN, zeros of both signs and infinities among the operands.
    const double reals[] = { -__builtin_inf(), -2.5, -0.0, 0.0, 1.0, 2.5, __builtin_inf(), __builtin_nan(""), -__builtin_nan("") };
    const int real_count = (int)(sizeof(reals) / sizeof(reals[0]));
    for (int i = 0; i < real_count; i += 1)
    {
        for (int j = 0; j < real_count; j += 1)
        {
            const double a = reals[i];
            const double b = reals[j];
            const float f = (float)a;
            const float g = (float)b;
            valid = patterns_float_less(a, b, i, j + 100) == (a < b ? i : j + 100) && valid;
            valid = patterns_float_not_greater(f, g, (long)i << 40, -(long)j) == (!(f > g) ? (long)i << 40 : -(long)j) && valid;
            valid = patterns_float_equal(a, b, i + 7, j - 9) == (a == b ? i + 7 : j - 9) && valid;
            valid = patterns_float_unequal(f, g, (long)i * 3 + 1) == (f != g ? (long)i * 3 + 1 : 0) && valid;
        }
        valid = patterns_float_sign(reals[i]) == (reals[i] < 0 ? -1 : reals[i] > 0 ? 1 : 0) && valid;
        long counted = 0;
        for (int n = 0; n < real_count; n += 1)
        {
            valid = patterns_float_count(reals, n, reals[i]) == counted && valid;
            counted += reals[n] >= reals[i] ? 3 : -1;
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
    valid = gsub_runs() && valid;
    valid = sector_runs() && valid;
    patterns_vector vector;
    unsigned long long lanes[8];
    for (int l = 0; l < 8; l += 1)
    {
        lanes[l] = 0x0101010101010101ull * (unsigned long long)(l + 1) + (unsigned long long)l;
        vector[l] = (long long)lanes[l];
    }
    patterns_wrapper wrapper;
    memset(&wrapper, 0x5a, sizeof(wrapper));
    patterns_whole_literal(&wrapper, &vector);
    valid = memcmp(&wrapper, lanes, sizeof(lanes)) == 0 && valid;
    memset(&wrapper, 0x5a, sizeof(wrapper));
    patterns_whole_declaration(&wrapper, &vector);
    valid = memcmp(&wrapper, lanes, sizeof(lanes)) == 0 && valid;
    memset(&wrapper, 0x5a, sizeof(wrapper));
    patterns_whole_volatile(&wrapper, &vector);
    valid = memcmp(&wrapper, lanes, sizeof(lanes)) == 0 && valid;
    patterns_lanes united;
    memset(&united, 0x5a, sizeof(united));
    patterns_whole_union(&united, &vector);
    valid = memcmp(united.q, lanes, sizeof(lanes)) == 0 && valid;
    valid = patterns_whole_member(&vector) == (lanes[3] ^ lanes[6]) && valid;
    const unsigned long long overridden[8] = { 0, 5, 0, 0, 0, 0, 0, 0 };
    memset(&united, 0x5a, sizeof(united));
    patterns_whole_override(&united, &vector);
    valid = memcmp(united.q, overridden, sizeof(overridden)) == 0 && valid;
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
    PATTERNS_WHOLE,
    PATTERNS_CLEARED,
    PATTERNS_GSUB,
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
    { "patterns_float_less", PATTERNS_SELECT },
    { "patterns_float_not_greater", PATTERNS_SELECT },
    { "patterns_float_count", PATTERNS_SELECT },
    { "patterns_float_sign", PATTERNS_SELECT },
    { "patterns_whole_literal", PATTERNS_WHOLE },
    { "patterns_whole_declaration", PATTERNS_WHOLE },
    { "patterns_whole_union", PATTERNS_WHOLE },
    { "patterns_whole_member", PATTERNS_WHOLE },
    { "patterns_whole_override", PATTERNS_CLEARED },
    { "patterns_whole_volatile", PATTERNS_CLEARED },
    { "patterns_gsub", PATTERNS_GSUB },
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
    // Stores of a register into the memory the instruction before loaded it from (#2784).
    unsigned stored_back = 0;
    // Instructions that name r11, the scratch register through which edges' moves break their cycles (#2834).
    unsigned scratch = 0;
    // Stores of a zero immediate to memory: an object's clear's (#2745).
    unsigned zeros = 0;
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
        zeros += move && strncmp(operands, "$0x0,", 5) == 0 && patterns_memory(destination);
        registers += move && operands[0] == '%' && patterns_memory(destination);
        constants += (move && operands[0] == '$' && !patterns_memory(destination)) || (strncmp(operation, "xor", 3) == 0 && comma != NULL && strncmp(operands, destination, (size_t)(comma - operands)) == 0);
        const bool jump = strcmp(operation, "jmp") == 0;
        indirect += jump && operands[0] == '*';
        increments += strncmp(operation, "add", 3) == 0 && strncmp(operands, "$0x4,%", 6) == 0;
        scratch += strstr(operands, "%r11") != NULL;
        const PatternsInstruction* before = i != 0 ? &body->instructions[i - 1] : NULL;
        const bool moved = before != NULL && strcmp(before->operation, "mov") == 0 && before->operands[0] == '%' && !patterns_memory(before->operands);
        clean += jump && operands[0] == '*' && !moved;
        const char* loaded_comma = before != NULL ? strrchr(before->operands, ',') : NULL;
        loaded += jump && operands[0] == '*' && loaded_comma != NULL && strcmp(before->operation, "mov") == 0 && patterns_memory(before->operands) && strcmp(loaded_comma + 1, operands + 1) == 0;
        if (move && comma != NULL && loaded_comma != NULL && strcmp(before->operation, "mov") == 0 && loaded_comma[1] == '%')
        {
            // `mov MEMORY,%REGISTER` and then `mov %REGISTER,MEMORY`.
            char reversed[sizeof(before->operands) + 2];
            snprintf(reversed, sizeof(reversed), "%s,%.*s", loaded_comma + 1, (int)(loaded_comma - before->operands), before->operands);
            stored_back += patterns_memory(destination) && strcmp(operands, reversed) == 0;
        }
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
    case PATTERNS_WHOLE:
        valid = valid && zeros == 0;
        break;
    case PATTERNS_CLEARED:
        valid = valid && zeros == 8;
        break;
    case PATTERNS_GSUB:
        valid = valid && stored_back == 0 && scratch == 0;
        break;
    }
    if (!valid)
    {
        fprintf(stderr, "patterns shape %s: definitions=%u setcc=%u cmov=%u shifts-by-zero=%u immediate-stores=%u register-stores=%u register-constants=%u indirect-jumps=%u jumps-to-them=%u clean-jumps=%u increments=%u loaded-jumps=%u zero-stores=%u stored-back=%u r11=%u\n",
                function->name, body->definitions, sets, moves, shifts, immediates, registers, constants, indirect, to_indirect, clean, increments, loaded, zeros, stored_back, scratch);
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
