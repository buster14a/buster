// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #134's random C programs, for test-ssa-source (ssa_source_test.c, which includes this file): a program is a few
// functions whose control flow is structured and unstructured at once, and a main that calls each of them on several
// inputs and prints what they return and the globals after, so that clang's build of the program and each of the IR's
// forms that the interpreter runs can be compared by their output.
//
// A function's statements: assignments to its locals and to memory, calls of the functions before it, if and else,
// while, do and for loops nested in each other, break and continue at any depth, switches whose cases fall through and
// whose case labels may sit inside the loops and blocks of their bodies (as Duff's device's do), gotos forward and
// backward to labels anywhere in the function, into loops and switches and out of them, and early returns. Its locals:
// those promotion takes (integers of every width and signedness, _Bool, unsigned __int128, float, double, long double, a
// pointer) and those it leaves in memory (an address-taken integer, a volatile one, an array and a struct).
//
// Everything is defined behavior in C: arithmetic is on unsigned values, and a signed or narrow local takes its value by
// a conversion (implementation-defined, alike in both compilers); divisors are made odd, and shift counts and array
// indices masked; floating locals stay nonnegative and small, so that their conversions to unsigned are defined; every
// local is set where it is declared, at the top of its function, so that a goto past a declaration skips no
// initialization; expressions have no side effects, so that the order their operands are evaluated in can't matter;
// and the program ends. Each loop's body starts with a decrement of the function's fuel, and so does each label, so that
// every cycle of the control flow decrements it, and the function returns when it reaches zero; and a call, which goes
// to an earlier function only, takes one of a global budget that main sets before each of its calls.
//
// Nothing here recurses: statements are generated with a stack of open constructs, and expressions by combining a list
// of operands two or three at a time.

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define BUSTER_SSA_GENERATE_FUNCTIONS 5
#define BUSTER_SSA_GENERATE_LABELS 5
#define BUSTER_SSA_GENERATE_DEPTH 7
#define BUSTER_SSA_GENERATE_INPUTS 5
#define BUSTER_SSA_GENERATE_TERMS 5
#define BUSTER_SSA_GENERATE_PIECE 1024

// The open constructs: a function's body, a block, an if's two branches, the loops and a switch.
#define BUSTER_SSA_GENERATE_BODY 0
#define BUSTER_SSA_GENERATE_BLOCK 1
#define BUSTER_SSA_GENERATE_IF 2
#define BUSTER_SSA_GENERATE_ELSE 3
#define BUSTER_SSA_GENERATE_WHILE 4
#define BUSTER_SSA_GENERATE_DO 5
#define BUSTER_SSA_GENERATE_FOR 6
#define BUSTER_SSA_GENERATE_SWITCH 7

typedef struct SsaGenerateConstruct SsaGenerateConstruct;
struct SsaGenerateConstruct
{
    // The statements still to generate in it, its kind, a loop's counter (i0 to i2) and a do loop's bound, and a switch's
    // case values taken (a bit each of 0 to 7) and whether it has its default.
    uint32_t left;
    uint8_t kind;
    uint8_t counter;
    uint8_t bound;
    uint8_t cases;
    bool defaulted;
};

typedef struct SsaGenerator SsaGenerator;
struct SsaGenerator
{
    char* text;
    uint64_t length;
    uint64_t capacity;
    uint64_t random;
    // Whether the text or an expression ran out of room: the program is then incomplete.
    bool overflow;
    // The function being generated, which may call those before it, and its labels placed, a bit each.
    uint32_t function;
    uint32_t placed;
    char pieces[BUSTER_SSA_GENERATE_TERMS][BUSTER_SSA_GENERATE_PIECE];
    char combined[BUSTER_SSA_GENERATE_PIECE];
    char expressions[3][BUSTER_SSA_GENERATE_PIECE];
};

// The values an expression reads, each of type unsigned int: the function's parameters, its locals (through a conversion,
// and those in memory through their pointer, element or member) and the globals.
static const char* const ssa_generate_operands[] = {
    "p0",
    "p1",
    "p2",
    "u0",
    "u1",
    "u2",
    "u3",
    "i0",
    "i1",
    "i2",
    "fuel",
    "(unsigned)s0",
    "(unsigned)s1",
    "(unsigned)c0",
    "(unsigned)c1",
    "(unsigned)h0",
    "(unsigned)h1",
    "(unsigned)w0",
    "(unsigned)(w0 >> 29)",
    "(unsigned)l0",
    "(unsigned)b0",
    "(unsigned)x0",
    "(unsigned)(x0 >> 70)",
    "(unsigned)d0",
    "(unsigned)f0",
    "(unsigned)e0",
    "*q0",
    "*mp",
    "m0",
    "v0",
    "a0[u1 & 3u]",
    "a0[i0 & 3u]",
    "r0.x",
    "r0.y",
    "g0",
    "g1",
    "garr[u2 & 7u]",
    "garr[i1 & 7u]",
};

// Two operands' combinations, and one's.
static const char* const ssa_generate_binary[] = {
    "(%s + %s)",
    "(%s - %s)",
    "(%s * %s)",
    "(%s & %s)",
    "(%s | %s)",
    "(%s ^ %s)",
    "(%s << (%s & 31u))",
    "(%s >> (%s & 31u))",
    "(%s / (%s | 1u))",
    "(%s %% (%s | 1u))",
    "(unsigned)(%s < %s)",
    "(unsigned)(%s == %s)",
    "(unsigned)((int)%s < (int)%s)",
    "(unsigned)((int)%s >= (int)%s)",
    "(unsigned)(%s && %s)",
    "(unsigned)(%s || %s)",
    "(unsigned)((short)%s > (signed char)%s)",
};

static const char* const ssa_generate_unary[] = {
    "~%s",
    "-%s",
    "(unsigned)!%s",
    "(unsigned)(unsigned char)%s",
    "(unsigned)(short)%s",
    "(unsigned)(signed char)%s",
};

// The assignments, each of one or two expressions (its count of %s).
static const char* const ssa_generate_assignments[] = {
    "u%u = %s;",
    "u%u += %s;",
    "u%u ^= %s;",
    "u%u = u%u * 0x9e3779b1u + %s;",
    "i%u = %s & 15u;",
    "s0 = (int)%s;",
    "s1 = (int)((unsigned)s1 + %s);",
    "c0 = (unsigned char)%s;",
    "c1 = (signed char)%s;",
    "h0 = (short)%s;",
    "h1 = (unsigned short)(h1 + %s);",
    "w0 = w0 * 6364136223846793005ull + %s;",
    "w0 ^= (unsigned long long)%s << (%s & 63u);",
    "l0 = (long)((unsigned long)l0 + %s);",
    "b0 = %s != 0u;",
    "x0 = x0 * 0x100000001b3ull + %s;",
    "d0 = d0 * 0.5 + (double)(%s & 1023u);",
    "f0 = f0 * 0.5f + (float)(%s & 255u);",
    "e0 = e0 * 0.5L + (long double)(%s & 1023u);",
    "q0 = &garr[%s & 7u];",
    "*q0 = %s;",
    "*q0 += %s;",
    "*mp = %s;",
    "m0 ^= %s;",
    "v0 = %s;",
    "a0[%s & 3u] = %s;",
    "r0.x = %s;",
    "r0.y ^= %s;",
    "g0 = %s;",
    "g1 += %s;",
    "garr[%s & 7u] = %s;",
};

// splitmix64.
static uint64_t ssa_generate_random(SsaGenerator* g)
{
    g->random += 0x9e3779b97f4a7c15ull;
    uint64_t z = g->random;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t ssa_generate_below(SsaGenerator* g, uint32_t bound)
{
    return bound == 0 ? 0 : (uint32_t)(ssa_generate_random(g) % bound);
}

static bool ssa_generate_chance(SsaGenerator* g, uint32_t percent)
{
    return ssa_generate_below(g, 100) < percent;
}

static void ssa_generate_text(SsaGenerator* g, const char* format, ...) __attribute__((format(printf, 2, 3)));
static void ssa_generate_text(SsaGenerator* g, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const uint64_t room = g->capacity - g->length;
    const int written = vsnprintf(g->text + g->length, room, format, arguments);
    va_end(arguments);
    if (written < 0 || (uint64_t)written >= room)
    {
        g->overflow = true;
    }
    else
    {
        g->length += (uint64_t)written;
    }
}

// Writes `format`'s one or two pieces into `out`, which must hold them, or marks the program incomplete.
static void ssa_generate_piece(SsaGenerator* g, char* out, const char* format, const char* first, const char* second, const char* third)
{
    const int written = snprintf(out, BUSTER_SSA_GENERATE_PIECE, format, first, second, third);
    if (written < 0 || written >= BUSTER_SSA_GENERATE_PIECE)
    {
        g->overflow = true;
        out[0] = 0;
    }
}

// An operand: one of the function's values, or a constant (small, one of the edges of unsigned's range, or any).
static void ssa_generate_operand(SsaGenerator* g, char* out)
{
    const uint32_t kind = ssa_generate_below(g, 10);
    if (kind < 7)
    {
        ssa_generate_piece(g, out, "%s", ssa_generate_operands[ssa_generate_below(g, BUSTER_ARRAY_LENGTH(ssa_generate_operands))], 0, 0);
    }
    else if (kind == 7)
    {
        snprintf(out, BUSTER_SSA_GENERATE_PIECE, "%uu", ssa_generate_below(g, 10));
    }
    else if (kind == 8)
    {
        static const char* const edges[] = { "0xffffffffu", "0x80000000u", "0x7fffffffu", "0x10000u" };
        ssa_generate_piece(g, out, "%s", edges[ssa_generate_below(g, BUSTER_ARRAY_LENGTH(edges))], 0, 0);
    }
    else
    {
        snprintf(out, BUSTER_SSA_GENERATE_PIECE, "0x%xu", (uint32_t)ssa_generate_random(g));
    }
}

// An unsigned expression without side effects, into expressions[slot]: up to `terms` operands, combined two at a time
// (three for ?:) by random operators until one is left, with a unary operator now and then.
static void ssa_generate_expression(SsaGenerator* g, uint32_t slot, uint32_t terms)
{
    uint32_t count = 1 + ssa_generate_below(g, terms);
    for (uint32_t t = 0; t < count; t += 1)
    {
        ssa_generate_operand(g, g->pieces[t]);
    }
    while (count > 1)
    {
        const uint32_t first = ssa_generate_below(g, count - 1);
        const bool ternary = count > 2 && first + 2 < count && ssa_generate_chance(g, 10);
        if (ternary)
        {
            ssa_generate_piece(g, g->combined, "(%s ? %s : %s)", g->pieces[first], g->pieces[first + 1], g->pieces[first + 2]);
        }
        else
        {
            const char* format = ssa_generate_binary[ssa_generate_below(g, BUSTER_ARRAY_LENGTH(ssa_generate_binary))];
            ssa_generate_piece(g, g->combined, format, g->pieces[first], g->pieces[first + 1], 0);
        }
        if (ssa_generate_chance(g, 15))
        {
            const char* format = ssa_generate_unary[ssa_generate_below(g, BUSTER_ARRAY_LENGTH(ssa_generate_unary))];
            ssa_generate_piece(g, g->pieces[first], format, g->combined, 0, 0);
        }
        else
        {
            memcpy(g->pieces[first], g->combined, BUSTER_SSA_GENERATE_PIECE);
        }
        const uint32_t taken = ternary ? 2 : 1;
        for (uint32_t t = first + 1; t + taken < count; t += 1)
        {
            memcpy(g->pieces[t], g->pieces[t + taken], BUSTER_SSA_GENERATE_PIECE);
        }
        count -= taken;
    }
    memcpy(g->expressions[slot], g->pieces[0], BUSTER_SSA_GENERATE_PIECE);
}

// The fuel's decrement, which every loop's body starts with and every label is: the function returns once it runs out.
static void ssa_generate_fuel(SsaGenerator* g)
{
    ssa_generate_text(g, " if (--fuel == 0u) goto done;\n");
}

// An assignment to one of the function's locals or to memory.
static void ssa_generate_assignment(SsaGenerator* g)
{
    const uint32_t which = ssa_generate_below(g, BUSTER_ARRAY_LENGTH(ssa_generate_assignments));
    const char* format = ssa_generate_assignments[which];
    ssa_generate_expression(g, 0, BUSTER_SSA_GENERATE_TERMS);
    ssa_generate_expression(g, 1, 2);
    const uint32_t local = ssa_generate_below(g, 4);
    if (which <= 2)
    {
        ssa_generate_text(g, format, local, g->expressions[0]);
    }
    else if (which == 3)
    {
        ssa_generate_text(g, format, local, local, g->expressions[0]);
    }
    else if (which == 4)
    {
        ssa_generate_text(g, format, local % 3, g->expressions[0]);
    }
    else if (strstr(format, "%s") != strrchr(format, '%'))
    {
        // Two expressions: an index or a shift count, then the value.
        ssa_generate_text(g, format, g->expressions[1], g->expressions[0]);
    }
    else
    {
        ssa_generate_text(g, format, g->expressions[0]);
    }
    ssa_generate_text(g, "\n");
}

// Whether a construct of `kind` is open on the stack below `depth` (a loop for continue, a loop or a switch for break),
// and the innermost switch's place, which case labels belong to (UINT32_MAX for none).
static uint32_t ssa_generate_switch(const SsaGenerateConstruct* stack, uint32_t depth)
{
    uint32_t found = UINT32_MAX;
    for (uint32_t d = depth; d != 0 && found == UINT32_MAX; d -= 1)
    {
        found = stack[d - 1].kind == BUSTER_SSA_GENERATE_SWITCH ? d - 1 : found;
    }
    return found;
}

static bool ssa_generate_in_loop(const SsaGenerateConstruct* stack, uint32_t depth)
{
    bool found = false;
    for (uint32_t d = 0; d < depth; d += 1)
    {
        const uint8_t kind = stack[d].kind;
        found |= kind == BUSTER_SSA_GENERATE_WHILE || kind == BUSTER_SSA_GENERATE_DO || kind == BUSTER_SSA_GENERATE_FOR;
    }
    return found;
}

// A statement that opens no construct: an assignment (most), a call of an earlier function, a label, a case label of the
// innermost switch, a break or a continue (conditional or not), a goto (conditional or not) or an early return.
static void ssa_generate_simple(SsaGenerator* g, SsaGenerateConstruct* stack, uint32_t depth)
{
    const uint32_t kind = ssa_generate_below(g, 100);
    const uint32_t switch_place = ssa_generate_switch(stack, depth);
    const bool loop = ssa_generate_in_loop(stack, depth);
    const bool breakable = loop || switch_place != UINT32_MAX;
    uint32_t label = UINT32_MAX;
    for (uint32_t k = 0; k < BUSTER_SSA_GENERATE_LABELS && label == UINT32_MAX; k += 1)
    {
        label = (g->placed & (1u << k)) == 0 ? k : label;
    }
    if (kind < 12 && g->function != 0)
    {
        ssa_generate_expression(g, 0, 3);
        ssa_generate_expression(g, 1, 3);
        ssa_generate_expression(g, 2, 3);
        ssa_generate_text(g, "if (budget != 0u) { budget -= 1u; u%u = fn%u(%s, %s, %s); }\n", ssa_generate_below(g, 4), ssa_generate_below(g, g->function), g->expressions[0],
                          g->expressions[1], g->expressions[2]);
    }
    else if (kind < 20 && label != UINT32_MAX)
    {
        g->placed |= 1u << label;
        ssa_generate_text(g, "L%u:", label);
        ssa_generate_fuel(g);
    }
    else if (kind < 30 && switch_place != UINT32_MAX)
    {
        SsaGenerateConstruct* owner = &stack[switch_place];
        const uint32_t value = ssa_generate_below(g, 8);
        if ((owner->cases & (1u << value)) == 0)
        {
            owner->cases |= (uint8_t)(1u << value);
            ssa_generate_text(g, "case %uu: ;\n", value);
        }
        else if (!owner->defaulted)
        {
            owner->defaulted = true;
            ssa_generate_text(g, "default: ;\n");
        }
        else
        {
            ssa_generate_text(g, "break;\n");
        }
    }
    else if (kind < 36 && breakable)
    {
        ssa_generate_expression(g, 0, 3);
        ssa_generate_text(g, ssa_generate_chance(g, 70) ? "if (%s) break;\n" : "break; (void)(%s);\n", g->expressions[0]);
    }
    else if (kind < 41 && loop)
    {
        ssa_generate_expression(g, 0, 3);
        ssa_generate_text(g, ssa_generate_chance(g, 70) ? "if (%s) continue;\n" : "continue; (void)(%s);\n", g->expressions[0]);
    }
    else if (kind < 47)
    {
        ssa_generate_expression(g, 0, 3);
        const uint32_t target = ssa_generate_below(g, BUSTER_SSA_GENERATE_LABELS + 1);
        char name[16];
        snprintf(name, sizeof(name), target == BUSTER_SSA_GENERATE_LABELS ? "done" : "L%u", target);
        if (ssa_generate_chance(g, 70))
        {
            ssa_generate_text(g, "if (%s) goto %s;\n", g->expressions[0], name);
        }
        else
        {
            ssa_generate_text(g, "goto %s;\n", name);
        }
    }
    else if (kind < 49)
    {
        ssa_generate_expression(g, 0, BUSTER_SSA_GENERATE_TERMS);
        ssa_generate_text(g, "if (%s) return %s;\n", ssa_generate_chance(g, 50) ? "fuel < 8u" : "u3 == 7u", g->expressions[0]);
    }
    else
    {
        ssa_generate_assignment(g);
    }
}

// Opens a construct of `kind` on the stack, its header and a loop's fuel written, with up to `statements` statements to
// generate in it.
static void ssa_generate_open(SsaGenerator* g, SsaGenerateConstruct* stack, uint32_t* depth, uint8_t kind, uint32_t statements)
{
    uint32_t loops = 0;
    for (uint32_t d = 0; d < *depth; d += 1)
    {
        loops += stack[d].kind == BUSTER_SSA_GENERATE_WHILE || stack[d].kind == BUSTER_SSA_GENERATE_DO || stack[d].kind == BUSTER_SSA_GENERATE_FOR;
    }
    SsaGenerateConstruct construct = {
        .left = 1 + ssa_generate_below(g, statements),
        .kind = kind,
        .counter = (uint8_t)(loops % 3),
        .bound = (uint8_t)(2 + ssa_generate_below(g, 6)),
    };
    ssa_generate_expression(g, 0, 3);
    if (kind == BUSTER_SSA_GENERATE_BLOCK)
    {
        ssa_generate_text(g, "{\n");
    }
    else if (kind == BUSTER_SSA_GENERATE_IF)
    {
        ssa_generate_text(g, "if (%s) {\n", g->expressions[0]);
    }
    else if (kind == BUSTER_SSA_GENERATE_WHILE)
    {
        // Counted, or until its condition, its break or its fuel.
        if (ssa_generate_chance(g, 75))
        {
            ssa_generate_text(g, "while (i%u++ < %uu && %s) {", construct.counter, construct.bound, g->expressions[0]);
        }
        else
        {
            ssa_generate_text(g, "while (%s) {", g->expressions[0]);
        }
        ssa_generate_fuel(g);
    }
    else if (kind == BUSTER_SSA_GENERATE_DO)
    {
        ssa_generate_text(g, "do {");
        ssa_generate_fuel(g);
    }
    else if (kind == BUSTER_SSA_GENERATE_FOR)
    {
        if (ssa_generate_chance(g, 85))
        {
            ssa_generate_text(g, "for (i%u = %uu; i%u < %uu; i%u += 1u) {", construct.counter, ssa_generate_below(g, 2), construct.counter, construct.bound,
                              construct.counter);
        }
        else
        {
            ssa_generate_text(g, "for (;;) {");
        }
        ssa_generate_fuel(g);
    }
    else
    {
        ssa_generate_text(g, "switch (%s & 7u) {\n", g->expressions[0]);
    }
    stack[*depth] = construct;
    *depth += 1;
}

// Closes the innermost construct: an if may go on to its else, a do loop takes its condition, and a function's body
// places the labels no statement placed and its end, `done`, which returns a value of every local.
static void ssa_generate_close(SsaGenerator* g, SsaGenerateConstruct* stack, uint32_t* depth)
{
    SsaGenerateConstruct* construct = &stack[*depth - 1];
    if (construct->kind == BUSTER_SSA_GENERATE_IF && ssa_generate_chance(g, 50))
    {
        ssa_generate_text(g, "} else {\n");
        construct->kind = BUSTER_SSA_GENERATE_ELSE;
        construct->left = 1 + ssa_generate_below(g, 4);
    }
    else if (construct->kind == BUSTER_SSA_GENERATE_DO)
    {
        ssa_generate_expression(g, 0, 3);
        ssa_generate_text(g, "} while (i%u++ < %uu && %s);\n", construct->counter, construct->bound, g->expressions[0]);
        *depth -= 1;
    }
    else if (construct->kind == BUSTER_SSA_GENERATE_BODY)
    {
        for (uint32_t k = 0; k < BUSTER_SSA_GENERATE_LABELS; k += 1)
        {
            if ((g->placed & (1u << k)) == 0)
            {
                ssa_generate_text(g, "L%u:", k);
                ssa_generate_fuel(g);
            }
        }
        ssa_generate_text(g, "done:\n    return u0 ^ u1 ^ u2 ^ u3 ^ i0 ^ i1 ^ i2 ^ fuel ^ (unsigned)s0 ^ (unsigned)s1 ^ (unsigned)c0 ^ (unsigned)c1 ^ (unsigned)h0 ^\n"
                             "           (unsigned)h1 ^ (unsigned)w0 ^ (unsigned)(w0 >> 32) ^ (unsigned)l0 ^ (unsigned)b0 ^ (unsigned)x0 ^ (unsigned)(x0 >> 96) ^\n"
                             "           (unsigned)d0 ^ (unsigned)f0 ^ (unsigned)e0 ^ *q0 ^ m0 ^ v0 ^ a0[0] ^ a0[1] ^ a0[2] ^ a0[3] ^ r0.x ^ r0.y;\n}\n\n");
        *depth -= 1;
    }
    else
    {
        ssa_generate_text(g, "}\n");
        *depth -= 1;
    }
}

// Function `f`'s definition: its locals, then its statements, generated from the stack of open constructs, each taking a
// statement of its parent's budget.
static void ssa_generate_function(SsaGenerator* g, uint32_t f)
{
    g->function = f;
    g->placed = 0;
    ssa_generate_text(g,
                      "static unsigned fn%u(unsigned p0, unsigned p1, unsigned p2)\n{\n"
                      "    unsigned fuel = 40u + (p0 & 15u);\n"
                      "    unsigned u0 = p0, u1 = p1 ^ 0x9e3779b9u, u2 = p2, u3 = 0u;\n"
                      "    unsigned i0 = 0u, i1 = 0u, i2 = 0u;\n"
                      "    int s0 = (int)p2, s1 = -7;\n"
                      "    unsigned char c0 = (unsigned char)p0;\n"
                      "    signed char c1 = (signed char)p1;\n"
                      "    short h0 = (short)p1;\n"
                      "    unsigned short h1 = (unsigned short)p2;\n"
                      "    unsigned long long w0 = ((unsigned long long)p0 << 32) | p1;\n"
                      "    long l0 = -1;\n"
                      "    _Bool b0 = p0 > p1;\n"
                      "    unsigned __int128 x0 = ((unsigned __int128)p2 << 64) | p0;\n"
                      "    double d0 = 0.5;\n"
                      "    float f0 = 2.0f;\n"
                      "    long double e0 = 0.25L;\n"
                      "    unsigned* q0 = &garr[p0 & 7u];\n"
                      "    unsigned m0 = p1;\n"
                      "    unsigned* mp = &m0;\n"
                      "    volatile unsigned v0 = p2;\n"
                      "    unsigned a0[4] = { p0, p1, p2, 3u };\n"
                      "    struct ssa_pair r0 = { p0, p2 };\n",
                      f);
    SsaGenerateConstruct stack[BUSTER_SSA_GENERATE_DEPTH + 1];
    uint32_t depth = 1;
    stack[0] = (SsaGenerateConstruct){
        .left = 20 + ssa_generate_below(g, 30),
        .kind = BUSTER_SSA_GENERATE_BODY,
    };
    while (depth != 0 && !g->overflow)
    {
        SsaGenerateConstruct* top = &stack[depth - 1];
        if (top->left == 0)
        {
            // A construct's closer at its header's indentation, a statement at its own.
            ssa_generate_text(g, "%*s", (int)(depth * 4 - 4), "");
            ssa_generate_close(g, stack, &depth);
        }
        else
        {
            top->left -= 1;
            ssa_generate_text(g, "%*s", (int)(depth * 4), "");
            const uint32_t open = depth < BUSTER_SSA_GENERATE_DEPTH ? 40 - depth * 5 : 0;
            if (ssa_generate_chance(g, open))
            {
                static const uint8_t kinds[] = {
                    BUSTER_SSA_GENERATE_BLOCK, BUSTER_SSA_GENERATE_IF,  BUSTER_SSA_GENERATE_IF,  BUSTER_SSA_GENERATE_WHILE,
                    BUSTER_SSA_GENERATE_DO,    BUSTER_SSA_GENERATE_FOR, BUSTER_SSA_GENERATE_FOR, BUSTER_SSA_GENERATE_SWITCH,
                };
                ssa_generate_open(g, stack, &depth, kinds[ssa_generate_below(g, BUSTER_ARRAY_LENGTH(kinds))], 6);
            }
            else
            {
                ssa_generate_simple(g, stack, depth);
            }
        }
    }
}

// The program of `seed` into `text` (`capacity` bytes, NUL-terminated): its globals, BUSTER_SSA_GENERATE_FUNCTIONS
// functions, and main, which calls each on every input, with a budget of calls for each call, and prints a hash of what
// they returned and the globals after each input. Its length, or 0 if it didn't fit.
static uint64_t ssa_generate_program(SsaGenerator* g, char* text, uint64_t capacity, uint64_t seed)
{
    g->text = text;
    g->length = 0;
    g->capacity = capacity;
    g->random = seed;
    g->overflow = capacity == 0;
    ssa_generate_text(g, "// test-ssa-source's program of seed %" PRIu64 " (ssa_source_generate.c).\n"
                         "int printf(const char*, ...);\n\nstruct ssa_pair { unsigned x; unsigned y; };\n\nstatic unsigned budget;\n",
                      seed);
    ssa_generate_text(g, "static unsigned g0 = 0x%xu, g1 = 0x%xu;\nstatic unsigned garr[8] = {", (uint32_t)ssa_generate_random(g), (uint32_t)ssa_generate_random(g));
    for (uint32_t k = 0; k < 8; k += 1)
    {
        ssa_generate_text(g, "%s0x%xu", k == 0 ? " " : ", ", (uint32_t)ssa_generate_random(g));
    }
    ssa_generate_text(g, " };\n\n");
    for (uint32_t f = 0; f < BUSTER_SSA_GENERATE_FUNCTIONS && !g->overflow; f += 1)
    {
        ssa_generate_function(g, f);
    }
    ssa_generate_text(g, "int main(void)\n{\n    static const unsigned inputs[%u][3] = {\n", BUSTER_SSA_GENERATE_INPUTS);
    for (uint32_t i = 0; i < BUSTER_SSA_GENERATE_INPUTS; i += 1)
    {
        const uint32_t small = ssa_generate_below(g, 16);
        ssa_generate_text(g, "        { %uu, 0x%xu, 0x%xu },\n", small, (uint32_t)ssa_generate_random(g), (uint32_t)ssa_generate_random(g));
    }
    ssa_generate_text(g, "    };\n    unsigned h = 2166136261u;\n    for (unsigned i = 0u; i < %uu; i += 1u)\n    {\n", BUSTER_SSA_GENERATE_INPUTS);
    for (uint32_t f = 0; f < BUSTER_SSA_GENERATE_FUNCTIONS; f += 1)
    {
        ssa_generate_text(g, "        budget = 24u;\n        h = (h ^ fn%u(inputs[i][0], inputs[i][1], inputs[i][2])) * 16777619u;\n", f);
    }
    ssa_generate_text(g, "        printf(\"%%u: %%u %%u %%u %%u %%u\\n\", i, h, g0, g1, garr[i & 7u], garr[(i + 3u) & 7u]);\n    }\n    return 0;\n}\n");
    return g->overflow ? 0 : g->length;
}
