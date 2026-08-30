#!/usr/bin/env python3
"""Differentially test `ide cc` against clang on generated C programs.

Every program is generated from a seeded grammar, compiled four ways, executed,
and its observable behavior compared:

  clang -O0   the reference implementation
  clang -O2   the *control*: if the two clang builds disagree, the generated
              program is invalid (undefined or unspecified behavior) and the
              divergence is a harness/generator bug, never a compiler bug
  ide cc      the default pipeline (FAST register allocator)
  ide cc -fno-register-allocator
              the canonical stack emitter, a separate codegen path that has
              carried its own miscompiles (the W0 64-lane #UD family was
              canonical-path-only)

The observables are the process exit status and stdout.  Generated programs
print through the raw `write` syscall because printf is a silent no-op in
ide-built binaries, and every value a program computes is both written to
stdout (full width) and folded into an FNV-1a hash whose low bits become the
exit status — so a divergence is visible even when only one of the two
channels survives.

Programs are composed of independent *units* (one function each, reported as
`U<n> <value>` lines).  On a divergence the harness re-generates the same seed
as one-unit-per-program probes automatically, so the report names the exact
unit that diverges; that unit is usually a near-minimal fixture already.

Generator families, seeded from constructs this tree has historically
miscompiled (see docs/performance-audits/ and the fix commits referenced in
the family docstrings):

  enum_sizeof   sizeof inside enum-constant initializers (c86ed530) and its
                neighbours: enum constants as array bounds, case labels,
                static initializers, referencing earlier enumerators
  array_bound   sizeof-derived file-scope array bounds over struct tables
                (5b57105c), with canary globals so a mislaid neighbour is
                observable, plus function-scope and 2-D siblings
  sizeof_expr   sizeof over expression operands: promotions, shifts,
                ternaries, assignments, calls (which must stay unevaluated),
                nested sizeof (caad25e7 and the 162-shape probe campaign)
  lazy_operand  conditional/&&/|| operands in call-argument and
                compound-literal position with side effects in the condition
                and the arms, parenthesized and not (d27d790c; the
                parenthesized-condition double lowering is a known-open bug)
  union_vector  vector_size vectors in unions and locals, 64-bit lanes at 512
                bits (the W0-#UD family, a8383082), vector arguments and
                returns, over-aligned locals beside canaries

Families added 2026-08-30, seeded from the 2026-08 frontend campaigns
(_Atomic, _Complex, packed/aligned, bit-fields, VLA parameters, statement
expressions, noreturn, x87; the fix series PRs 536..778):

  atomic_qual   _Atomic scalars in all four spellings (qualifier, qualified
                typedef, _Atomic(T), atomic typedef), compound RMW and
                increments, atomic aggregates at 8 bytes or below with
                by-value pass/return and the power-of-two padding observable
  complex_arith _Complex float/double/long double: imaginary literals, union
                part reads, __real__/__imag__, exact arithmetic chains
                (values are multiples of 0.5, division only by powers of
                two), by-value crossings, structs containing complex
  packed_aligned packed/aligned on the struct, the member declarator, the
                typedef, plus _Alignas; member declarator lists with a
                trailing attribute on one declarator; offsetof/sizeof/
                _Alignof and address-modulo observables beside canaries
  bit_field     widths over every base type, anonymous and :0 fields,
                packed access units, attributes after the width, designated
                initializers over shared storage units, and a zeroed union
                pun that observes bit allocation directly
  param_decl    VLA and [static N] parameters (qualifier spellings included),
                2-D VLAs and runtime sizeof, expression bounds, [*]
                forward declarations, and local VLAs of scalars and structs
  stmt_expr     GNU statement expressions in initializer, call-argument,
                condition, loop-bound, subscript and nested positions,
                struct-valued results, shadowing declarations, comma tails
  decl_list     declarator lists distributing specifiers and attributes:
                noreturn on one declarator of a list or carried only by a
                typedef (the live-code-deletion observable), object lists
                with attributes mid-list at file and block scope
  x87_ld        long double: static-initializer folding, exact chains,
                conversions in both directions, halfword probes of the
                80-bit pattern (sign of -0.0L, folded 1e5000L), and x87
                aggregate/union ABI crossings by value
  mixed_abi     aggregates mixing the member kinds above (complex, long
                double, bit-fields, packed and aligned members, atomic
                members), positional and designated initializers,
                whole-struct copies and by-value ABI crossings

Constructs ide cc rejects today are still generated, at low probability, so
the rejects stay visible without drowning behavior divergences: compound
assignment and increments on atomic floats, __builtin_complex, complex ~
conjugation, aligned attributes on bit-fields, [*] forward declarations.

Usage (from the repository root, after `./build.sh build --config Release -t ide`):
    tools/differential_c_harness.py --count 50                  # smoke
    tools/differential_c_harness.py --count 2000 --jobs 16      # a real run
    tools/differential_c_harness.py --families lazy_operand --count 200
    tools/differential_c_harness.py --isolate lazy_operand:1234 # per-unit probe
    tools/differential_c_harness.py --self-test

Results land under build/differential-c/: every divergent program's source and
a report naming the family, seed, unit, and the four raw observations.
Categories are kept apart because they mean different things: `behavior` is a
wrong-code bug, `rejects` is valid code ide cc refuses, `ide-crash` is a
compiler crash, and `generator` is the harness's own bug (clang disagreed with
itself).
"""

import argparse
import concurrent.futures
import os
import random
import shutil
import subprocess
import sys
import zlib
from dataclasses import dataclass, field

REPOSITORY_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_IDE = os.path.join("build", "Release", "ide")
OUTPUT_ROOT = os.path.join("build", "differential-c")
RUN_TIMEOUT_SECONDS = 10
COMPILE_TIMEOUT_SECONDS = 60

# ---------------------------------------------------------------------------
# Program skeleton
# ---------------------------------------------------------------------------

PRELUDE = """\
// Generated by tools/differential_c_harness.py family={family} seed={seed}
// Compile with clang and `ide cc`; the program prints every observed value
// with the raw write syscall (printf is a silent no-op under ide-built
// binaries) and exits with the low bits of an FNV-1a hash over them.
typedef unsigned long long u64;
long write(int, const void*, unsigned long);

static u64 hash_state = 1469598103934665603ull;

static void sink(u64 value)
{{
    hash_state = (hash_state ^ value) * 1099511628211ull;
}}

static void emit_text(const char* text, unsigned long length)
{{
    write(1, text, length);
}}

static void emit_u64(u64 value)
{{
    char buffer[24];
    int index = 24;
    do
    {{
        index -= 1;
        buffer[index] = (char)('0' + (value % 10u));
        value /= 10u;
    }} while (value);
    emit_text(buffer + index, (unsigned long)(24 - index));
}}

static void unit_report(u64 unit, u64 value)
{{
    emit_text("U", 1);
    emit_u64(unit);
    emit_text(" ", 1);
    emit_u64(value);
    emit_text("\\n", 1);
    sink(unit);
    sink(value);
}}
"""

MAIN_TEMPLATE = """\
int main(void)
{{
{calls}\
    emit_text("H ", 2);
    emit_u64(hash_state);
    emit_text("\\n", 1);
    return (int)(hash_state & 63u);
}}
"""


@dataclass
class Unit:
    """One independent check: file-scope support text plus a body computing a u64."""

    globals_text: str
    body_text: str  # statements; must end by assigning `result`


@dataclass
class Program:
    family: str
    seed: int
    units: list

    def render(self, selected=None):
        indices = range(len(self.units)) if selected is None else selected
        pieces = [PRELUDE.format(family=self.family, seed=self.seed)]
        calls = []
        for index in indices:
            unit = self.units[index]
            if unit.globals_text:
                pieces.append(unit.globals_text)
            pieces.append(
                "static u64 unit_%d(void)\n{\n    u64 result = 0;\n%s    return result;\n}\n"
                % (index, unit.body_text)
            )
            calls.append("    unit_report(%du, unit_%d());\n" % (index, index))
        pieces.append(MAIN_TEMPLATE.format(calls="".join(calls)))
        return "\n".join(pieces)


# ---------------------------------------------------------------------------
# Shared generator vocabulary
# ---------------------------------------------------------------------------

SCALAR_TYPES = [
    # (spelling, size on LP64, is_float)
    ("unsigned char", 1, False),
    ("unsigned short", 2, False),
    ("unsigned int", 4, False),
    ("unsigned long long", 8, False),
    ("signed char", 1, False),
    ("short", 2, False),
    ("int", 4, False),
    ("long long", 8, False),
    ("float", 4, True),
    ("double", 8, True),
]


def pick_scalar(rng):
    return rng.choice(SCALAR_TYPES)


def struct_definition(rng, tag):
    """A struct with 1..4 members; returns (text, python-known lower size bound)."""
    member_count = rng.randint(1, 4)
    members = []
    for member_index in range(member_count):
        spelling, _, _ = pick_scalar(rng)
        if rng.random() < 0.3:
            members.append("    %s m%d[%d];" % (spelling, member_index, rng.randint(2, 5)))
        else:
            members.append("    %s m%d;" % (spelling, member_index))
    text = "typedef struct %s\n{\n%s\n} %s;\n" % (tag, "\n".join(members), tag)
    return text


# ---------------------------------------------------------------------------
# Family: enum_sizeof
# ---------------------------------------------------------------------------

def generate_enum_sizeof_unit(rng, unit_index):
    prefix = "eu%d" % unit_index
    globals_parts = []
    scalar_spelling, scalar_size, _ = pick_scalar(rng)
    while scalar_spelling in ("float", "double"):
        scalar_spelling, scalar_size, _ = pick_scalar(rng)
    typedef_name = "%s_T" % prefix
    globals_parts.append("typedef %s %s;\n" % (scalar_spelling, typedef_name))
    struct_tag = "%s_S" % prefix
    globals_parts.append(struct_definition(rng, struct_tag))

    # Enumerator initializers.  Shift amounts stay provably below 31 by
    # construction so the int-typed constants never overflow.
    enumerators = []
    shapes = []
    name0 = "%s_A" % prefix.upper()
    multiplier = rng.choice([1, 2, 3])
    if scalar_size * 8 * multiplier - 2 <= 30 and rng.random() < 0.7:
        shift = "sizeof(%s) * %du - 2u" % (typedef_name, 8 * multiplier)
        enumerators.append("    %s = 1 << (%s)," % (name0, shift))
        shapes.append("shift")
    else:
        enumerators.append(
            "    %s = (int)(sizeof(%s) * %du + sizeof(%s))," % (name0, typedef_name, rng.randint(1, 9), struct_tag)
        )
        shapes.append("arith")
    name1 = "%s_B" % prefix.upper()
    second = rng.choice(
        [
            "    %s = %s + (int)sizeof(%s)," % (name1, name0, typedef_name),
            "    %s = sizeof(%s) == %du ? %d : %d," % (name1, typedef_name, scalar_size, rng.randint(3, 40), rng.randint(41, 90)),
            "    %s = (int)(sizeof(%s) / sizeof(%s))," % (name1, struct_tag, typedef_name),
            "    %s = %s * 2 - (int)sizeof(char[sizeof(%s)])," % (name1, name0, typedef_name),
        ]
    )
    enumerators.append(second)
    enum_tag = "%s_E" % prefix
    globals_parts.append("enum %s\n{\n%s\n};\n" % (enum_tag, "\n".join(enumerators)))

    body = []
    body.append("    sink((u64)%s);\n" % name0)
    body.append("    sink((u64)%s);\n" % name1)
    body.append("    sink((u64)sizeof(enum %s));\n" % enum_tag)
    use = rng.randint(0, 3)
    if use == 0:
        # Clamped: an enumerator can reach 1<<30, and gigabyte pads only
        # exercise the linker's large-data layout (an already-recorded open
        # finding), drowning the enum-value signal this family is for.
        globals_parts.append("static char %s_pad[%s > 0 && %s < 65536 ? %s : 1];\n" % (prefix, name1, name1, name1))
        body.append("    sink((u64)sizeof(%s_pad));\n" % prefix)
    elif use == 1:
        globals_parts.append("static const int %s_table[] = { %s, %s, %s + %s };\n" % (prefix, name0, name1, name0, name1))
        body.append("    sink((u64)%s_table[0] + (u64)%s_table[1] * 3u + (u64)%s_table[2] * 7u);\n" % (prefix, prefix, prefix))
    elif use == 2:
        globals_parts.append("static volatile int %s_probe = %d;\n" % (prefix, rng.randint(0, 1)))
        body.append("    int probe = %s_probe ? (int)%s : (int)%s;\n" % (prefix, name0, name1))
        # Only one enumerator becomes a case label: two labels could fold to
        # the same value, which is a compile error in both compilers and
        # would only add generator noise.
        body.append("    switch (probe)\n    {\n")
        body.append("    case %s: sink(11u); break;\n" % name0)
        body.append("    default: sink(33u); break;\n    }\n")
    else:
        # The historically broken context beside the two that were correct:
        # the same expression as an enumerator, a static const, and a runtime
        # value must agree.
        globals_parts.append("static const int %s_stat = 1 << (sizeof(%s) * 8u - 2u);\n" % (prefix, "unsigned short"))
        globals_parts.append("enum %s_ctx { %s_CTX = 1 << (sizeof(unsigned short) * 8u - 2u) };\n" % (prefix, prefix.upper()))
        body.append("    unsigned short probe = 0;\n")
        body.append("    sink((u64)%s_stat);\n" % prefix)
        body.append("    sink((u64)%s_CTX);\n" % prefix.upper())
        body.append("    sink((u64)(1u << (sizeof(probe) * 8u - 2u)));\n")
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: array_bound
# ---------------------------------------------------------------------------

def generate_array_bound_unit(rng, unit_index):
    prefix = "ab%d" % unit_index
    globals_parts = []
    struct_tag = "%s_Item" % prefix
    globals_parts.append(struct_definition(rng, struct_tag))
    table_length = rng.randint(2, 6)
    items = ", ".join("{0}" for _ in range(table_length))
    globals_parts.append("static %s %s_table[] = { %s };\n" % (struct_tag, prefix, items))
    extra = rng.randint(0, 3)
    element_spelling, _, element_is_float = pick_scalar(rng)
    bound = "sizeof(%s_table) / sizeof(%s_table[0]) + %d" % (prefix, prefix, extra)
    globals_parts.append("static %s %s_dep[%s];\n" % (element_spelling, prefix, bound))
    globals_parts.append("static unsigned int %s_canary = 0x%08Xu;\n" % (prefix, rng.randrange(1, 1 << 32)))
    second_form = rng.randint(0, 2)
    if second_form == 0:
        globals_parts.append("static unsigned char %s_grid[sizeof(%s_table) / sizeof(%s_table[0])][%d];\n" % (prefix, prefix, prefix, rng.randint(2, 4)))
    elif second_form == 1:
        globals_parts.append("static unsigned short %s_second[sizeof(%s_dep) / sizeof(%s_dep[0]) + 1];\n" % (prefix, prefix, prefix))
    else:
        globals_parts.append("enum { %s_LEN = sizeof(%s_table) / sizeof(%s_table[0]) };\n" % (prefix.upper(), prefix, prefix))
        globals_parts.append("static unsigned char %s_enum_sized[%s_LEN * 2 + 1];\n" % (prefix, prefix.upper()))

    body = []
    body.append("    sink((u64)sizeof(%s_table));\n" % prefix)
    body.append("    sink((u64)sizeof(%s_dep));\n" % prefix)
    body.append("    sink((u64)(sizeof(%s_dep) / sizeof(%s_dep[0])));\n" % (prefix, prefix))
    tag_value = rng.randint(1, 100)
    cast = "(%s)" % element_spelling
    body.append("    %s_dep[0] = %s%d;\n" % (prefix, cast, tag_value))
    body.append("    %s_dep[sizeof(%s_dep) / sizeof(%s_dep[0]) - 1] = %s%d;\n" % (prefix, prefix, prefix, cast, tag_value + 1))
    if element_is_float:
        body.append("    sink((u64)%s_dep[0] + (u64)%s_dep[sizeof(%s_dep) / sizeof(%s_dep[0]) - 1] * 5u);\n" % (prefix, prefix, prefix, prefix))
    else:
        body.append("    sink((u64)%s_dep[0] + (u64)%s_dep[sizeof(%s_dep) / sizeof(%s_dep[0]) - 1] * 5u);\n" % (prefix, prefix, prefix, prefix))
    body.append("    sink((u64)%s_canary);\n" % prefix)
    if second_form == 0:
        body.append("    %s_grid[0][0] = 7; %s_grid[sizeof(%s_grid) / sizeof(%s_grid[0]) - 1][0] = 9;\n" % (prefix, prefix, prefix, prefix))
        body.append("    sink((u64)sizeof(%s_grid) + %s_grid[0][0] * 100u + %s_grid[sizeof(%s_grid) / sizeof(%s_grid[0]) - 1][0]);\n" % (prefix, prefix, prefix, prefix, prefix))
    elif second_form == 1:
        body.append("    sink((u64)sizeof(%s_second));\n" % prefix)
    else:
        body.append("    sink((u64)%s_LEN);\n" % prefix.upper())
        body.append("    sink((u64)sizeof(%s_enum_sized));\n" % prefix)
    # A function-scope sibling with the same shape.
    body.append("    static %s local_table[%d];\n" % (struct_tag, table_length))
    body.append("    static unsigned char local_dep[sizeof(local_table) / sizeof(local_table[0]) + %d];\n" % extra)
    body.append("    local_dep[0] = 3; local_dep[sizeof(local_dep) - 1] = 5;\n")
    body.append("    sink((u64)sizeof(local_dep) + local_dep[0] + local_dep[sizeof(local_dep) - 1]);\n")
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: sizeof_expr
# ---------------------------------------------------------------------------

SIZEOF_GLOBALS_TEMPLATE = """\
static char {p}_gc = 'a';
static signed char {p}_gsc = -3;
static unsigned char {p}_guc = 200;
static _Bool {p}_gb = 1;
static short {p}_gs = -7;
static unsigned short {p}_gus = 60000;
static int {p}_gi = 11;
static unsigned int {p}_gu = 12u;
static long {p}_gl = 13;
static long long {p}_gll = 14;
static unsigned long long {p}_gull = 15u;
static float {p}_gf = 2.0f;
static double {p}_gd = 3.0;
static int {p}_garr[7];
static int* {p}_gp = {p}_garr;
static char {p}_gstr[] = "sizeof";
enum {p}_Color {{ {p}_RED = 1, {p}_GREEN = 2 }};
static enum {p}_Color {p}_ge = {p}_GREEN;
static int {p}_calls;
static short {p}_scall(void)
{{
    {p}_calls += 1;
    return 1;
}}
"""

def sizeof_operand_pool(prefix):
    p = prefix
    return [
        # Promotions: narrow operands under unary and binary operators.
        "-%s_gc" % p, "+%s_gsc" % p, "~%s_guc" % p, "!%s_gb" % p,
        "-%s_gs" % p, "~%s_gus" % p, "+%s_gb" % p, "~%s_ge" % p,
        "%s_gc + %s_gc" % (p, p), "%s_gs + %s_gs" % (p, p),
        "%s_gb + %s_gb" % (p, p), "%s_guc * %s_gsc" % (p, p),
        "%s_gc + %s_gl" % (p, p), "%s_gs + %s_gd" % (p, p),
        "%s_gu + %s_gll" % (p, p), "%s_gf + %s_gc" % (p, p),
        # Shifts: the result type is the promoted left operand alone.
        "%s_gc << 1" % p, "%s_gs << %s_gll" % (p, p), "%s_gull >> %s_gc" % (p, p),
        "%s_gus << 2" % p,
        # Assignments: the result type is the (unpromoted) left operand.
        "%s_gc = 100" % p, "%s_gs += %s_gll" % (p, p), "%s_guc *= 2" % p,
        "%s_gb = 0" % p,
        # Ternaries over same and mixed arms.
        "%s_gb ? %s_gc : %s_gc" % (p, p, p),
        "%s_gb ? %s_gs : %s_gi" % (p, p, p),
        "%s_gb ? %s_gf : %s_gd" % (p, p, p),
        "%s_gi ? %s_guc : %s_gsc" % (p, p, p),
        # Comma: the right operand decides.
        "%s_gi, %s_gc" % (p, p), "%s_gd, %s_gb" % (p, p),
        # Nested sizeof and mixed arithmetic over it.
        "sizeof(%s_gc) + %s_gi" % (p, p), "sizeof %s_gd" % p,
        "%s_gi + (int)sizeof(%s_garr)" % (p, p),
        # Arrays, pointers, strings, casts, compound literals.
        "%s_garr" % p, "*%s_gp" % p, "%s_garr[2]" % p, "&%s_gi" % p,
        "\"literal\"", "(char)%s_gi" % p, "(unsigned short)(%s_gll + 1)" % p,
        "(int[3]){1, 2, 3}", "(struct { char a; long long b; }){0}",
        # Calls in unevaluated context: the call must not run.
        "%s_scall()" % p, "%s_scall() + %s_gc" % (p, p),
        "%s_gb ? %s_scall() : %s_scall()" % (p, p, p),
    ]


def generate_sizeof_expr_unit(rng, unit_index):
    prefix = "sz%d" % unit_index
    globals_text = SIZEOF_GLOBALS_TEMPLATE.format(p=prefix)
    pool = sizeof_operand_pool(prefix)
    rng.shuffle(pool)
    picked = pool[: rng.randint(6, 12)]
    body = []
    body.append("    %s_calls = 0;\n" % prefix)
    for operand in picked:
        body.append("    sink((u64)sizeof(%s));\n" % operand)
    # The side-effect observation: no sizeof operand above may have executed.
    body.append("    sink((u64)%s_calls * 1000u + 500u);\n" % prefix)
    # And the same call evaluated for real exactly once, as the control.
    body.append("    sink((u64)%s_scall());\n" % prefix)
    body.append("    sink((u64)%s_calls);\n" % prefix)
    body.append("    result = hash_state;\n")
    return Unit(globals_text, "".join(body))


# ---------------------------------------------------------------------------
# Family: lazy_operand
# ---------------------------------------------------------------------------

LAZY_GLOBALS_TEMPLATE = """\
static int {p}_calls;
static int {p}_conds;
static int {p}_make(int payload)
{{
    {p}_calls += 1;
    return payload;
}}
static int {p}_cond(int value)
{{
    {p}_conds += 1;
    return value;
}}
static int {p}_take(int value)
{{
    return value;
}}
static int {p}_take_two(int first, int second)
{{
    return first * 100 + second;
}}
typedef struct {p}_Pair
{{
    int a;
    int b;
}} {p}_Pair;
"""


def lazy_control_expression(rng, prefix, mask_expression):
    """A control expression with recorded side effects; returns (text, kind)."""
    p = prefix
    condition_kind = rng.randint(0, 3)
    if condition_kind == 0:
        condition = mask_expression
    elif condition_kind == 1:
        condition = "%s_conds++ ? %s : %s" % (p, mask_expression, mask_expression)
        condition = "(%s)" % condition
    elif condition_kind == 2:
        condition = "%s_cond(%s)" % (p, mask_expression)
    else:
        # The known-open shape has the increment directly in the condition of
        # a (possibly parenthesized) control expression.
        condition = "%s_conds++ + %s" % (p, mask_expression)
    operator_kind = rng.randint(0, 2)
    if operator_kind == 0:
        text = "%s ? %s_make(3) : 0" % (condition, p)
    elif operator_kind == 1:
        text = "%s && %s_make(1)" % (condition, p)
    else:
        text = "%s || %s_make(1)" % (condition, p)
    parenthesization = rng.randint(0, 2)
    for _ in range(parenthesization):
        text = "(%s)" % text
    return text, condition_kind, operator_kind, parenthesization


def generate_lazy_operand_unit(rng, unit_index):
    prefix = "lz%d" % unit_index
    globals_text = LAZY_GLOBALS_TEMPLATE.format(p=prefix)
    p = prefix
    body = []
    body.append("    %s_calls = 0;\n" % p)
    body.append("    %s_conds = 0;\n" % p)
    check_count = rng.randint(3, 6)
    for check_index in range(check_count):
        mask = rng.randint(0, 1)
        expression, _, _, _ = lazy_control_expression(rng, prefix, str(mask))
        position = rng.randint(0, 7)
        if position == 0:
            statement = "int v%d = %s_take(%s);" % (check_index, p, expression)
        elif position == 1:
            statement = "int v%d = %s_take_two(%s, 7);" % (check_index, p, expression)
        elif position == 2:
            statement = "int v%d = %s_take_two(7, %s);" % (check_index, p, expression)
        elif position == 3:
            statement = "int v%d = %s_take(%s_take(%s));" % (check_index, p, p, expression)
        elif position == 4:
            statement = "%s_Pair pr%d = (%s_Pair){.a = 1, .b = %s}; int v%d = pr%d.b;" % (p, check_index, p, expression, check_index, check_index)
        elif position == 5:
            statement = "%s_Pair pr%d = (%s_Pair){1, %s}; int v%d = pr%d.b;" % (p, check_index, p, expression, check_index, check_index)
        elif position == 6:
            statement = "int* items%d = (int[2]){1, %s}; int v%d = items%d[1];" % (check_index, expression, check_index, check_index)
        else:
            statement = "int v%d = 1 + (%s);" % (check_index, expression)
        body.append("    %s\n" % statement)
        body.append("    sink((u64)(unsigned)v%d);\n" % check_index)
        body.append("    sink((u64)%s_calls * 8u + (u64)%s_conds);\n" % (p, p))
    body.append("    result = hash_state;\n")
    return Unit(globals_text, "".join(body))


# ---------------------------------------------------------------------------
# Family: union_vector
# ---------------------------------------------------------------------------

VECTOR_ELEMENTS = [
    ("signed char", 1, False),
    ("unsigned char", 1, False),
    ("short", 2, False),
    ("unsigned short", 2, False),
    ("int", 4, False),
    ("unsigned int", 4, False),
    ("long long", 8, False),
    ("unsigned long long", 8, False),
    ("float", 4, True),
    ("double", 8, True),
]

# 1/2/4-byte vectors ride general-purpose registers on SysV (the GCC
# deviation clang follows); 8 bytes up rides XMM/YMM/ZMM. Keep both regimes
# in the draw so the classification boundary stays covered.
VECTOR_BYTES = [1, 2, 4, 8, 16, 32, 64]


def generate_union_vector_unit(rng, unit_index):
    prefix = "uv%d" % unit_index
    element_spelling, element_size, element_is_float = rng.choice(VECTOR_ELEMENTS)
    vector_bytes = rng.choice([size for size in VECTOR_BYTES if size >= element_size])
    lane_count = vector_bytes // element_size
    vector_type = "%s_V" % prefix
    storage_type = "%s_Storage" % prefix
    globals_parts = []
    globals_parts.append("typedef %s %s __attribute__((vector_size(%d)));\n" % (element_spelling, vector_type, vector_bytes))
    globals_parts.append(
        "typedef union %s\n{\n    %s vector;\n    %s lanes[%d];\n} %s;\n"
        % (storage_type, vector_type, element_spelling, lane_count, storage_type)
    )
    by_value = rng.random() < 0.5
    if by_value:
        globals_parts.append(
            "static %s %s_combine(%s a, %s b)\n{\n    return a + b;\n}\n"
            % (vector_type, prefix, vector_type, vector_type)
        )

    body = []
    # Frame-layout canaries around the vector storage: a union-vector local
    # mis-sized or mis-aligned in the frame historically clobbered neighbours.
    body.append("    unsigned int before_canary = 0x%08Xu;\n" % rng.randrange(1, 1 << 32))
    body.append("    %s a;\n" % storage_type)
    body.append("    unsigned int middle_canary = 0x%08Xu;\n" % rng.randrange(1, 1 << 32))
    body.append("    %s b;\n" % storage_type)
    body.append("    unsigned int after_canary = 0x%08Xu;\n" % rng.randrange(1, 1 << 32))
    values_a = [rng.randint(1, 9) for _ in range(lane_count)]
    values_b = [rng.randint(1, 9) for _ in range(lane_count)]
    body.append("    for (int lane = 0; lane < %d; lane += 1)\n    {\n" % lane_count)
    body.append("        a.lanes[lane] = (%s)((lane * 7 + %d) %% 9 + 1);\n" % (element_spelling, values_a[0]))
    body.append("        b.lanes[lane] = (%s)((lane * 5 + %d) %% 9 + 1);\n" % (element_spelling, values_b[0]))
    body.append("    }\n")
    operations = ["+", "-"]
    if not element_is_float:
        operations += ["&", "|", "^"]
    operations.append("*")
    operator = rng.choice(operations)
    body.append("    %s c;\n" % storage_type)
    if by_value:
        body.append("    c.vector = %s_combine(a.vector %s b.vector, b.vector);\n" % (prefix, operator))
    else:
        body.append("    c.vector = a.vector %s b.vector;\n" % operator)
    if rng.random() < 0.5:
        scalar = rng.randint(1, 4)
        second_operator = rng.choice(["+", "-"])
        body.append("    c.vector = c.vector %s %d;\n" % (second_operator, scalar))
    if rng.random() < 0.4:
        body.append("    c.vector = -c.vector;\n")
    body.append("    u64 folded = 0;\n")
    body.append("    for (int lane = 0; lane < %d; lane += 1)\n    {\n" % lane_count)
    if element_is_float:
        body.append("        folded = folded * 31u + (u64)(long long)c.lanes[lane];\n")
    else:
        body.append("        folded = folded * 31u + (u64)(unsigned long long)c.lanes[lane];\n")
    body.append("    }\n")
    body.append("    sink(folded);\n")
    if rng.random() < 0.5:
        # Through (long long) first: a negative float converted straight to an
        # unsigned type is undefined, and lanes go negative after - and unary -.
        body.append("    sink((u64)(long long)c.vector[%d]);\n" % rng.randrange(lane_count))
    body.append("    sink((u64)before_canary + (u64)middle_canary * 3u + (u64)after_canary * 7u);\n")
    body.append("    sink((u64)sizeof(%s) * 100u + (u64)sizeof(%s));\n" % (vector_type, storage_type))
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: atomic_qual
# ---------------------------------------------------------------------------

ATOMIC_SCALARS = [
    ("char", 1, False),
    ("unsigned char", 1, False),
    ("short", 2, False),
    ("unsigned short", 2, False),
    ("int", 4, False),
    ("unsigned int", 4, False),
    ("long long", 8, False),
    ("unsigned long long", 8, False),
    ("float", 4, True),
    ("double", 8, True),
]

# Aggregate layouts stay at 8 bytes or below: past 8 clang lowers atomic
# loads and stores to libatomic calls it does not link, and the harness
# would only be measuring link failures.  (spelling, name, element_count)
ATOMIC_AGGREGATE_LAYOUTS = [
    [("char", "first", 1), ("char", "second", 1)],
    [("char", "first", 1), ("char", "second", 1), ("char", "third", 1)],
    [("char", "first", 1), ("short", "second", 1)],
    [("short", "first", 1), ("char", "second", 1)],
    [("char", "first", 5)],
    [("char", "first", 7)],
    [("int", "first", 1), ("char", "second", 1)],
    [("int", "first", 1), ("short", "second", 1)],
    [("unsigned char", "first", 3), ("unsigned char", "second", 1)],
]


def sink_value(body, expression, is_float):
    # Negative floats must go through (long long): a negative float converted
    # straight to an unsigned type is undefined.
    if is_float:
        body.append("    sink((u64)(long long)(%s));\n" % expression)
    else:
        body.append("    sink((u64)(%s));\n" % expression)


def generate_atomic_qual_unit(rng, unit_index):
    prefix = "aq%d" % unit_index
    globals_parts = []
    body = []

    # The four spellings of an atomic scalar: the qualifier on the base type,
    # the qualifier over a typedef, the _Atomic(T) specifier form, and a
    # typedef that is itself atomic.
    scalar_spelling, _, scalar_is_float = rng.choice(ATOMIC_SCALARS)
    spelling_form = rng.randint(0, 3)
    if spelling_form == 0:
        atomic_spelling = "_Atomic %s" % scalar_spelling
    elif spelling_form == 1:
        globals_parts.append("typedef %s %s_Base;\n" % (scalar_spelling, prefix))
        atomic_spelling = "_Atomic %s_Base" % prefix
    elif spelling_form == 2:
        atomic_spelling = "_Atomic(%s)" % scalar_spelling
    else:
        globals_parts.append("typedef _Atomic %s %s_AT;\n" % (scalar_spelling, prefix))
        atomic_spelling = "%s_AT" % prefix

    globals_parts.append("static %s %s_shared = %d;\n" % (atomic_spelling, prefix, rng.randint(1, 30)))
    body.append("    %s local_value = %d;\n" % (atomic_spelling, rng.randint(1, 30)))

    for target in ("%s_shared" % prefix, "local_value"):
        if scalar_is_float:
            operations = [
                "%s = %s + %d;" % (target, target, rng.randint(1, 5)),
                "%s = %s * 2;" % (target, target),
            ]
            if rng.random() < 0.08:
                # Rejected today (compound assignment on an atomic float);
                # kept rare so the reject stays covered without drowning runs.
                operations.append("%s += %d;" % (target, rng.randint(1, 3)))
            if rng.random() < 0.05:
                operations.append("%s++;" % target)
        else:
            pool = [
                "%s += %d;" % (target, rng.randint(1, 9)),
                "%s -= %d;" % (target, rng.randint(1, 5)),
                "%s ^= %d;" % (target, rng.randint(1, 15)),
                "%s |= %d;" % (target, rng.randint(1, 15)),
                "%s &= %d;" % (target, rng.randint(16, 63)),
                "%s++;" % target,
                "--%s;" % target,
            ]
            rng.shuffle(pool)
            operations = pool[: rng.randint(2, 4)]
            if rng.random() < 0.4:
                # The value of an assignment to an atomic object is the value
                # stored.
                operations.append("sink((u64)(%s = %d));" % (target, rng.randint(1, 40)))
        for operation in operations:
            body.append("    %s\n" % operation)
        sink_value(body, target, scalar_is_float)

    # An atomic aggregate: store a plain seed (atomic store), load it back
    # (atomic load), and cross it by value when the layout drew a helper.
    layout = rng.choice(ATOMIC_AGGREGATE_LAYOUTS)
    plain_tag = "%s_P" % prefix
    member_lines = []
    for member_spelling, member_name, element_count in layout:
        if element_count == 1:
            member_lines.append("    %s %s;" % (member_spelling, member_name))
        else:
            member_lines.append("    %s %s[%d];" % (member_spelling, member_name, element_count))
    globals_parts.append("typedef struct %s\n{\n%s\n} %s;\n" % (plain_tag, "\n".join(member_lines), plain_tag))
    aggregate_spelling = rng.choice(["_Atomic %s" % plain_tag, "_Atomic(%s)" % plain_tag])
    by_value = rng.random() < 0.7
    if by_value:
        first_spelling, first_name, first_count = layout[0]
        first_access = "%s[0]" % first_name if first_count > 1 else first_name
        globals_parts.append(
            "static %s %s_bump(%s value, int delta)\n{\n"
            "    %s plain = value;\n"
            "    plain.%s = (%s)(plain.%s + delta);\n"
            "    return plain;\n"
            "}\n"
            % (aggregate_spelling, prefix, aggregate_spelling, plain_tag,
               first_access, first_spelling, first_access)
        )

    body.append("    %s box;\n" % aggregate_spelling)
    body.append("    %s seed = {0};\n" % plain_tag)
    for member_spelling, member_name, element_count in layout:
        if element_count == 1:
            body.append("    seed.%s = (%s)%d;\n" % (member_name, member_spelling, rng.randint(1, 60)))
        else:
            for element_index in range(element_count):
                body.append("    seed.%s[%d] = (%s)%d;\n"
                            % (member_name, element_index, member_spelling, rng.randint(1, 60)))
    body.append("    box = seed;\n")
    body.append("    %s out = box;\n" % plain_tag)

    def sink_members(source):
        for member_spelling, member_name, element_count in layout:
            if element_count == 1:
                body.append("    sink((u64)%s.%s);\n" % (source, member_name))
            else:
                for element_index in range(element_count):
                    body.append("    sink((u64)%s.%s[%d]);\n" % (source, member_name, element_index))

    sink_members("out")
    if by_value:
        body.append("    %s boxed_result;\n" % aggregate_spelling)
        body.append("    boxed_result = %s_bump(box, %d);\n" % (prefix, rng.randint(1, 9)))
        body.append("    %s out_bumped = boxed_result;\n" % plain_tag)
        sink_members("out_bumped")
    # The padding-promotion observable: an atomic aggregate is padded to a
    # power of two, the plain one is not.
    body.append("    sink((u64)sizeof(%s) * 100u + (u64)_Alignof(%s));\n" % (plain_tag, plain_tag))
    body.append("    sink((u64)sizeof(%s) * 100u + (u64)_Alignof(%s));\n"
                % (aggregate_spelling, aggregate_spelling))

    if rng.random() < 0.3:
        # Atomic pointers: plain load/add/store only. Compound arithmetic on
        # an atomic pointer is rejected by clang 22 and stays out.
        body.append("    static int slot_array[6];\n")
        body.append("    _Atomic(int*) cursor = slot_array;\n")
        body.append("    cursor = cursor + %d;\n" % rng.randint(1, 4))
        body.append("    int* raw_cursor = cursor;\n")
        body.append("    sink((u64)(raw_cursor - slot_array));\n")

    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: complex_arith
# ---------------------------------------------------------------------------

COMPLEX_TYPES = [
    # (real spelling, real literal suffix, imaginary literal suffix)
    ("float", "f", "fi"),
    ("double", "", "i"),
    ("long double", "L", "Li"),
]

# Multiples of 0.5 with small magnitude: every sum, difference, and product
# the chains below can build is exact in float already, so clang -O0, clang
# -O2, and ide cc must agree bit for bit.
COMPLEX_REAL_VALUES = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 0.5, 1.5, 2.5, -2.0, -0.5, 12.0]


def real_literal(value, suffix):
    return "%r%s" % (float(value), suffix)


def generate_complex_arith_unit(rng, unit_index):
    prefix = "cx%d" % unit_index
    real_spelling, real_suffix, imaginary_suffix = rng.choice(COMPLEX_TYPES)
    complex_spelling = "%s _Complex" % real_spelling
    globals_parts = []
    body = []
    union_tag = "%s_U" % prefix
    globals_parts.append(
        "typedef union %s\n{\n    %s z;\n    %s xy[2];\n} %s;\n"
        % (union_tag, complex_spelling, real_spelling, union_tag)
    )

    def lit(value):
        return real_literal(value, real_suffix)

    def imag_lit(value):
        return real_literal(value, imaginary_suffix)

    def value():
        return rng.choice(COMPLEX_REAL_VALUES)

    mix_helper = rng.random() < 0.7
    if mix_helper:
        mix_operator = rng.choice(["+", "-"])
        globals_parts.append(
            "static %s %s_mix(%s a, %s b, %s scale)\n{\n"
            "    return a * scale %s b;\n"
            "}\n"
            % (complex_spelling, prefix, complex_spelling, complex_spelling, real_spelling, mix_operator)
        )
    carry_helper = rng.random() < 0.5
    if carry_helper:
        globals_parts.append("typedef struct %s_C\n{\n    %s z;\n    int tag;\n} %s_C;\n"
                             % (prefix, complex_spelling, prefix))
        globals_parts.append(
            "static %s_C %s_carry(%s_C v, %s delta)\n{\n"
            "    v.z += delta;\n"
            "    v.tag += 3;\n"
            "    return v;\n"
            "}\n"
            % (prefix, prefix, prefix, real_spelling)
        )

    # Two seeds through randomly drawn construction forms.
    for slot in ("za", "zb"):
        form = rng.randint(0, 3)
        real_value, imaginary_value = value(), value()
        if form == 0:
            body.append("    %s %s = %s + %s;\n"
                        % (complex_spelling, slot, lit(real_value), imag_lit(imaginary_value)))
        elif form == 1:
            body.append("    %s_U %s_box = { .xy = { %s, %s } };\n"
                        % (prefix, slot, lit(real_value), lit(imaginary_value)))
            body.append("    %s %s = %s_box.z;\n" % (complex_spelling, slot, slot))
        elif form == 2:
            body.append("    %s %s = %s;\n" % (complex_spelling, slot, lit(real_value)))
            body.append("    %s += %s;\n" % (slot, imag_lit(imaginary_value)))
        else:
            body.append("    %s %s = %s * %s + %s;\n"
                        % (complex_spelling, slot, imag_lit(imaginary_value), lit(1.0), lit(real_value)))

    body.append("    %s zc = za;\n" % complex_spelling)
    operation_pool = [
        "zc = za + zb;",
        "zc = za - zb;",
        "zc = za * zb;",
        "zc = za / %s;" % lit(rng.choice([2.0, 4.0, 0.5])),
        "zc = za * %s + zb;" % lit(value()),
        "zc = -za;",
        "zc += zb;",
        "zc = (%s)((double _Complex)zc);" % complex_spelling,
        "zc = zc - %s;" % lit(value()),
    ]
    rng.shuffle(operation_pool)
    for operation in operation_pool[: rng.randint(2, 4)]:
        body.append("    %s\n" % operation)
    if rng.random() < 0.01:
        # Rejected today ("this unary operator has no complex form").
        body.append("    zc = ~zc;\n")
    if rng.random() < 0.01:
        # Rejected today (unbound identifier __builtin_complex).
        body.append("    zc = zc + __builtin_complex(%s, %s);\n" % (lit(1.0), lit(2.0)))
    if mix_helper:
        body.append("    zc = %s_mix(za, zb, %s);\n" % (prefix, lit(value())))

    body.append("    sink(za == zb ? 7u : 9u);\n")
    body.append("    sink(zc != za ? 3u : 4u);\n")
    body.append("    %s_U probe;\n" % prefix)
    for variable in ("za", "zb", "zc"):
        body.append("    probe.z = %s;\n" % variable)
        body.append("    sink((u64)(long long)probe.xy[0]);\n")
        body.append("    sink((u64)(long long)probe.xy[1]);\n")
        # The real part again through the conversion path: complex to real
        # drops the imaginary part.
        body.append("    sink((u64)(long long)(%s)%s);\n" % (real_spelling, variable))
    if rng.random() < 0.35:
        body.append("    sink((u64)(long long)__real__ zc);\n")
        body.append("    sink((u64)(long long)__imag__ zc);\n")
    if carry_helper:
        body.append("    %s_C boxed = { %s + %s, %d };\n"
                    % (prefix, lit(value()), imag_lit(value()), rng.randint(1, 9)))
        body.append("    boxed = %s_carry(boxed, %s);\n" % (prefix, lit(value())))
        body.append("    probe.z = boxed.z;\n")
        body.append("    sink((u64)(long long)probe.xy[0]);\n")
        body.append("    sink((u64)(long long)probe.xy[1]);\n")
        body.append("    sink((u64)boxed.tag);\n")
    body.append("    sink((u64)sizeof(%s));\n" % complex_spelling)
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: packed_aligned
# ---------------------------------------------------------------------------

PACKED_SCALARS = [
    ("unsigned char", 1),
    ("unsigned short", 2),
    ("unsigned int", 4),
    ("unsigned long long", 8),
    ("signed char", 1),
    ("short", 2),
    ("int", 4),
    ("long long", 8),
    ("float", 4),
    ("double", 8),
]


def generate_packed_aligned_unit(rng, unit_index):
    prefix = "pk%d" % unit_index
    globals_parts = []
    body = []
    struct_tag = "%s_S" % prefix

    aligned_typedef = rng.random() < 0.25
    if aligned_typedef:
        globals_parts.append("typedef int %s_AI __attribute__((aligned(16)));\n" % prefix)

    member_entries = []  # (names, spelling, is_float)
    member_lines = []
    member_count = rng.randint(2, 4)
    for member_index in range(member_count):
        spelling, natural = rng.choice(PACKED_SCALARS)
        is_float = spelling in ("float", "double")
        name = "m%d" % member_index
        draw = rng.random()
        if draw < 0.40:
            member_lines.append("    %s %s;" % (spelling, name))
            names = [name]
        elif draw < 0.55:
            alignment = rng.choice([1, 2, 4, 8, 16, 32])
            member_lines.append("    %s %s __attribute__((aligned(%d)));" % (spelling, name, alignment))
            names = [name]
        elif draw < 0.67:
            # _Alignas below the natural alignment is a constraint violation;
            # only stricter-or-equal values are generated.
            alignment = rng.choice([a for a in (1, 2, 4, 8, 16, 32) if a >= natural])
            specifier = rng.choice(["_Alignas(%d)" % alignment, "_Alignas(long long)"])
            member_lines.append("    %s %s %s;" % (specifier, spelling, name))
            names = [name]
        elif draw < 0.79:
            member_lines.append("    %s %s __attribute__((packed));" % (spelling, name))
            names = [name]
        else:
            # A member declarator list with a trailing attribute on the middle
            # declarator: per-declarator application is the historical bug.
            alignment = rng.choice([2, 4, 8, 16])
            names = [name, "%s_b" % name, "%s_c" % name]
            member_lines.append("    %s %s, %s __attribute__((aligned(%d))), %s;"
                                % (spelling, names[0], names[1], alignment, names[2]))
        member_entries.append((names, spelling, is_float))
    if aligned_typedef and rng.random() < 0.6:
        member_lines.append("    %s_AI aliased;" % prefix)
        member_entries.append((["aliased"], "int", False))

    attribute_draw = rng.random()
    if attribute_draw < 0.30:
        attribute_text = "__attribute__((packed))"
    elif attribute_draw < 0.45:
        attribute_text = "__attribute__((aligned(%d)))" % rng.choice([2, 4, 8, 16, 32])
    elif attribute_draw < 0.55:
        attribute_text = "__attribute__((packed, aligned(%d)))" % rng.choice([2, 4, 8])
    else:
        attribute_text = ""
    if attribute_text and rng.random() < 0.5:
        header = "typedef struct %s %s\n{" % (attribute_text, struct_tag)
        trailer = "} %s;\n" % struct_tag
    else:
        header = "typedef struct %s\n{" % struct_tag
        trailer = "} %s %s;\n" % (attribute_text, struct_tag) if attribute_text else "} %s;\n" % struct_tag
    globals_parts.append("%s\n%s\n%s" % (header, "\n".join(member_lines), trailer))

    instance_type = struct_tag
    if rng.random() < 0.3:
        # The whole struct re-aligned through a typedef.
        globals_parts.append("typedef %s %s_A __attribute__((aligned(%d)));\n"
                             % (struct_tag, prefix, rng.choice([16, 32])))
        instance_type = "%s_A" % prefix
        body.append("    sink((u64)_Alignof(%s_A));\n" % prefix)

    globals_parts.append("static %s %s_g;\n" % (instance_type, prefix))

    body.append("    sink((u64)sizeof(%s) * 100u + (u64)_Alignof(%s));\n" % (struct_tag, struct_tag))
    for names, _, _ in member_entries:
        for name in names:
            body.append("    sink((u64)__builtin_offsetof(%s, %s));\n" % (struct_tag, name))
    body.append("    sink((u64)((unsigned long long)&%s_g %% _Alignof(%s)));\n" % (prefix, instance_type))

    body.append("    unsigned int before_canary = 0x%08Xu;\n" % rng.randrange(1, 1 << 32))
    body.append("    %s local_instance;\n" % instance_type)
    body.append("    unsigned int after_canary = 0x%08Xu;\n" % rng.randrange(1, 1 << 32))
    stored = 1
    for names, spelling, _ in member_entries:
        for name in names:
            body.append("    local_instance.%s = (%s)%d;\n" % (name, spelling, stored))
            stored += 3
    body.append("    %s copied = local_instance;\n" % instance_type)
    for names, _, is_float in member_entries:
        for name in names:
            sink_value(body, "copied.%s" % name, is_float)
    body.append("    sink((u64)((unsigned long long)&local_instance %% _Alignof(%s)));\n" % instance_type)
    body.append("    sink((u64)before_canary * 3u + (u64)after_canary);\n")

    # Arrays use the plain struct type: an aligned typedef whose alignment
    # does not divide the size cannot be an array element (clang refuses).
    body.append("    %s pair_array[2];\n" % struct_tag)
    first_member = member_entries[0][0][0]
    body.append("    pair_array[1].%s = (%s)%d;\n" % (first_member, member_entries[0][1], rng.randint(1, 50)))
    sink_value(body, "pair_array[1].%s" % first_member, member_entries[0][2])
    body.append("    sink((u64)sizeof(pair_array));\n")
    body.append("    sink((u64)((char*)&pair_array[1] - (char*)&pair_array[0]));\n")

    if aligned_typedef:
        body.append("    %s_AI aligned_local = %d;\n" % (prefix, rng.randint(1, 40)))
        body.append("    const %s_AI qualified_local = %d;\n" % (prefix, rng.randint(1, 40)))
        body.append("    sink((u64)_Alignof(%s_AI));\n" % prefix)
        body.append("    sink((u64)((unsigned long long)&aligned_local % 16u));\n")
        body.append("    sink((u64)((unsigned long long)&qualified_local % 16u));\n")
        body.append("    sink((u64)(aligned_local + qualified_local));\n")

    if rng.random() < 0.4:
        body.append("    int list_a = %d, list_b __attribute__((aligned(16))) = %d, list_c = %d;\n"
                    % (rng.randint(1, 9), rng.randint(1, 9), rng.randint(1, 9)))
        body.append("    sink((u64)(unsigned)(list_a + list_b * 3 + list_c * 7));\n")
        body.append("    sink((u64)((unsigned long long)&list_b % 16u));\n")

    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: bit_field
# ---------------------------------------------------------------------------

BITFIELD_BASES = [
    ("unsigned int", 32, False),
    ("int", 32, True),
    ("unsigned char", 8, False),
    ("signed char", 8, True),
    ("unsigned short", 16, False),
    ("short", 16, True),
    ("unsigned long long", 64, False),
    ("long long", 64, True),
    ("_Bool", 1, False),
]


def bit_field_literal(value, base_bits, is_signed):
    if is_signed:
        return "%d" % value if base_bits < 64 else "%dll" % value
    return "%du" % value if base_bits < 64 else "%dull" % value


def generate_bit_field_unit(rng, unit_index):
    prefix = "bt%d" % unit_index
    globals_parts = []
    body = []
    struct_tag = "%s_S" % prefix

    field_lines = []
    named_fields = []  # (name, width, is_signed, base_bits, initial_value)
    field_count = rng.randint(2, 6)
    for field_index in range(field_count):
        draw = rng.random()
        if draw < 0.10 and field_lines:
            field_lines.append("    unsigned : 0;")
            continue
        if draw < 0.20:
            field_lines.append("    unsigned : %d;" % rng.randint(1, 7))
            continue
        if draw < 0.32:
            spelling, _ = rng.choice(PACKED_SCALARS[:8])
            name = "plain%d" % field_index
            field_lines.append("    %s %s;" % (spelling, name))
            named_fields.append((name, None, False, None, rng.randint(1, 90)))
            continue
        base_spelling, base_bits, base_signed = rng.choice(BITFIELD_BASES)
        width = 1 if base_bits == 1 else rng.randint(1, base_bits)
        suffix = ""
        attribute_draw = rng.random()
        if attribute_draw < 0.10:
            suffix = " __attribute__((packed))"
        elif attribute_draw < 0.112:
            # Rejected today ("alignment specifier cannot be applied to a
            # bit-field", which clang accepts); kept rare.
            suffix = " __attribute__((aligned(%d)))" % rng.choice([2, 4, 8])
        name = "f%d" % field_index
        field_lines.append("    %s %s : %d%s;" % (base_spelling, name, width, suffix))
        if base_bits == 1:
            initial_value = rng.randint(0, 1)
        elif base_signed:
            initial_value = rng.randint(-(2 ** (width - 1)), 2 ** (width - 1) - 1)
        else:
            initial_value = rng.randrange(0, 2 ** min(width, 63))
        named_fields.append((name, width, base_signed, base_bits, initial_value))
    if not named_fields:
        field_lines.append("    unsigned f0 : 5;")
        named_fields.append(("f0", 5, False, 32, rng.randint(0, 31)))

    attribute_draw = rng.random()
    if attribute_draw < 0.35:
        attribute_text = "__attribute__((packed))"
    elif attribute_draw < 0.45:
        attribute_text = "__attribute__((aligned(%d)))" % rng.choice([2, 4, 8])
    elif attribute_draw < 0.53:
        attribute_text = "__attribute__((packed, aligned(%d)))" % rng.choice([2, 4])
    else:
        attribute_text = ""
    if attribute_text and rng.random() < 0.5:
        header = "typedef struct %s %s\n{" % (attribute_text, struct_tag)
        trailer = "} %s;\n" % struct_tag
    else:
        header = "typedef struct %s\n{" % struct_tag
        trailer = "} %s %s;\n" % (attribute_text, struct_tag) if attribute_text else "} %s;\n" % struct_tag
    globals_parts.append("%s\n%s\n%s" % (header, "\n".join(field_lines), trailer))

    def field_literal(field):
        name, width, is_signed, base_bits, initial_value = field
        if width is None:
            return "%d" % initial_value
        return bit_field_literal(initial_value, base_bits or 32, is_signed)

    init_form = rng.randint(0, 2)
    if init_form == 0:
        body.append("    %s x = {0};\n" % struct_tag)
        for field in named_fields:
            body.append("    x.%s = %s;\n" % (field[0], field_literal(field)))
        current = {field[0]: field[4] for field in named_fields}
    elif init_form == 1:
        initializers = ", ".join(field_literal(field) for field in named_fields)
        body.append("    %s x = { %s };\n" % (struct_tag, initializers))
        current = {field[0]: field[4] for field in named_fields}
    else:
        # A designated subset: every unnamed field must read back zero, even
        # one sharing a storage unit with a named one.
        chosen = [field for field in named_fields if rng.random() < 0.6]
        if not chosen:
            chosen = [named_fields[0]]
        initializers = ", ".join(".%s = %s" % (field[0], field_literal(field)) for field in chosen)
        body.append("    %s x = { %s };\n" % (struct_tag, initializers))
        chosen_names = {field[0] for field in chosen}
        current = {field[0]: (field[4] if field[0] in chosen_names else 0) for field in named_fields}

    # Compound steps that provably stay inside the field's range.
    for field in named_fields:
        name, width, is_signed, base_bits, _ = field
        if width is None or rng.random() < 0.5:
            continue
        if base_bits == 1:
            continue
        if is_signed:
            headroom = (2 ** (width - 1) - 1) - current[name]
            if headroom > 0:
                delta = rng.randint(1, min(3, headroom))
                body.append("    x.%s += %d;\n" % (name, delta))
                current[name] += delta
        else:
            delta = rng.randint(1, 7)
            body.append("    x.%s += %du;\n" % (name, delta))
            current[name] = (current[name] + delta) % (2 ** width)

    body.append("    %s y = x;\n" % struct_tag)
    for field in named_fields:
        name, width, is_signed, _, _ = field
        signed_read = is_signed or (width is None)
        sink_expression = "(u64)(long long)y.%s" % name if signed_read else "(u64)y.%s" % name
        body.append("    sink(%s);\n" % sink_expression)
    body.append("    sink((u64)sizeof(%s) * 100u + (u64)_Alignof(%s));\n" % (struct_tag, struct_tag))

    if rng.random() < 0.3:
        # Bit allocation order and unit offsets observed through the bytes:
        # the union starts fully zeroed, so every bit is deterministic.
        globals_parts.append("union %s_W\n{\n    unsigned long long word;\n    %s bits;\n};\n"
                             % (prefix, struct_tag))
        body.append("    union %s_W pun = { .word = 0 };\n" % prefix)
        for field in named_fields:
            body.append("    pun.bits.%s = %s;\n" % (field[0], field_literal(field)))
        body.append("    sink(pun.word);\n")

    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: param_decl
# ---------------------------------------------------------------------------

VLA_ELEMENT_TYPES = [
    ("int", False),
    ("unsigned char", False),
    ("short", False),
    ("double", True),
    ("long long", False),
]


def generate_param_decl_unit(rng, unit_index):
    prefix = "vp%d" % unit_index
    globals_parts = []
    body = []

    shape_pool = ["one_d", "static_const", "two_d", "expression_bound", "local_vla"]
    shapes = rng.sample(shape_pool, rng.randint(2, 3))
    if rng.random() < 0.04:
        # Rejected today (a [*] forward declaration conflicts with its own
        # definition); kept rare.
        shapes.append("star_forward")

    for shape_index, shape in enumerate(shapes):
        element_spelling, element_is_float = rng.choice(VLA_ELEMENT_TYPES)
        cast = "(%s)" % element_spelling
        read = "(u64)(long long)" if element_is_float else "(u64)"
        if shape == "one_d":
            bound = rng.choice(["count", "static count", "const count", "restrict static count",
                                "static const count", "const restrict static count"])
            globals_parts.append(
                "static u64 %s_sum%d(int count, %s values[%s])\n{\n"
                "    u64 total = (u64)sizeof(values);\n"
                "    for (int index = 0; index < count; index += 1)\n    {\n"
                "        total = total * 3u + %svalues[index];\n"
                "    }\n"
                "    return total;\n"
                "}\n"
                % (prefix, shape_index, element_spelling, bound, read)
            )
            length = rng.randint(3, 6)
            body.append("    %s data%d[%d];\n" % (element_spelling, shape_index, length))
            body.append("    for (int index = 0; index < %d; index += 1)\n    {\n"
                        "        data%d[index] = %s(index * 2 + %d);\n    }\n"
                        % (length, shape_index, cast, rng.randint(1, 9)))
            passed = length if "static" in bound or rng.random() < 0.5 else length - 1
            body.append("    sink(%s_sum%d(%d, data%d));\n" % (prefix, shape_index, passed, shape_index))
        elif shape == "static_const":
            bound_length = rng.randint(2, 5)
            parenthesized = rng.random() < 0.2
            declarator = "(values)[static %d]" % bound_length if parenthesized else "values[static %d]" % bound_length
            globals_parts.append(
                "static u64 %s_fix%d(%s %s)\n{\n"
                "    u64 total = 0;\n"
                "    for (int index = 0; index < %d; index += 1)\n    {\n"
                "        total = total * 5u + %svalues[index];\n"
                "    }\n"
                "    return total;\n"
                "}\n"
                % (prefix, shape_index, element_spelling, declarator, bound_length, read)
            )
            length = bound_length + rng.randint(0, 2)
            body.append("    %s fixed%d[%d];\n" % (element_spelling, shape_index, length))
            body.append("    for (int index = 0; index < %d; index += 1)\n    {\n"
                        "        fixed%d[index] = %s(index + %d);\n    }\n"
                        % (length, shape_index, cast, rng.randint(1, 9)))
            body.append("    sink(%s_fix%d(fixed%d));\n" % (prefix, shape_index, shape_index))
        elif shape == "two_d":
            rows = rng.randint(2, 4)
            columns = rng.randint(2, 5)
            globals_parts.append(
                "static u64 %s_grid%d(int rows, int cols, %s cells[rows][cols])\n{\n"
                "    u64 total = (u64)sizeof(cells[0]) + (u64)sizeof(cells);\n"
                "    for (int row = 0; row < rows; row += 1)\n    {\n"
                "        for (int col = 0; col < cols; col += 1)\n        {\n"
                "            total = total * 5u + %scells[row][col];\n        }\n"
                "    }\n"
                "    return total;\n"
                "}\n"
                % (prefix, shape_index, element_spelling, read)
            )
            body.append("    %s grid%d[%d][%d];\n" % (element_spelling, shape_index, rows, columns))
            body.append("    for (int row = 0; row < %d; row += 1)\n    {\n"
                        "        for (int col = 0; col < %d; col += 1)\n        {\n"
                        "            grid%d[row][col] = %s(row * 7 + col + %d);\n        }\n"
                        "    }\n"
                        % (rows, columns, shape_index, cast, rng.randint(1, 9)))
            body.append("    sink(%s_grid%d(%d, %d, grid%d));\n"
                        % (prefix, shape_index, rows, columns, shape_index))
        elif shape == "expression_bound":
            half = rng.randint(2, 4)
            globals_parts.append(
                "static u64 %s_flat%d(int half, %s values[half * 2])\n{\n"
                "    u64 total = (u64)sizeof(values);\n"
                "    for (int index = 0; index < half * 2; index += 1)\n    {\n"
                "        total = total * 7u + %svalues[index];\n"
                "    }\n"
                "    return total;\n"
                "}\n"
                % (prefix, shape_index, element_spelling, read)
            )
            body.append("    %s flat%d[%d];\n" % (element_spelling, shape_index, half * 2))
            body.append("    for (int index = 0; index < %d; index += 1)\n    {\n"
                        "        flat%d[index] = %s(index * 3 + %d);\n    }\n"
                        % (half * 2, shape_index, cast, rng.randint(1, 9)))
            body.append("    sink(%s_flat%d(%d, flat%d));\n" % (prefix, shape_index, half, shape_index))
        elif shape == "star_forward":
            globals_parts.append("static u64 %s_star%d(int count, %s values[*]);\n"
                                 % (prefix, shape_index, element_spelling))
            globals_parts.append(
                "static u64 %s_star%d(int count, %s values[count])\n{\n"
                "    u64 total = 0;\n"
                "    for (int index = 0; index < count; index += 1)\n    {\n"
                "        total = total * 3u + %svalues[index];\n"
                "    }\n"
                "    return total;\n"
                "}\n"
                % (prefix, shape_index, element_spelling, read)
            )
            length = rng.randint(2, 5)
            body.append("    %s starred%d[%d];\n" % (element_spelling, shape_index, length))
            body.append("    for (int index = 0; index < %d; index += 1)\n    {\n"
                        "        starred%d[index] = %s(index + %d);\n    }\n"
                        % (length, shape_index, cast, rng.randint(1, 9)))
            body.append("    sink(%s_star%d(%d, starred%d));\n" % (prefix, shape_index, length, shape_index))
        else:  # local_vla
            length = rng.randint(2, 6)
            body.append("    int length%d = %d;\n" % (shape_index, length))
            variant = rng.randint(0, 2)
            if variant == 0:
                body.append("    %s buffer%d[length%d];\n" % (element_spelling, shape_index, shape_index))
                body.append("    for (int index = 0; index < length%d; index += 1)\n    {\n"
                            "        buffer%d[index] = %s(index * 2 + %d);\n    }\n"
                            % (shape_index, shape_index, cast, rng.randint(1, 9)))
                body.append("    u64 local_total%d = (u64)sizeof(buffer%d);\n" % (shape_index, shape_index))
                body.append("    for (int index = 0; index < length%d; index += 1)\n    {\n"
                            "        local_total%d = local_total%d * 3u + %sbuffer%d[index];\n    }\n"
                            % (shape_index, shape_index, shape_index, read, shape_index))
                body.append("    sink(local_total%d);\n" % shape_index)
            elif variant == 1:
                globals_parts.append("struct %s_Cell%d\n{\n    int x;\n    char y;\n};\n" % (prefix, shape_index))
                body.append("    struct %s_Cell%d cells%d[length%d];\n"
                            % (prefix, shape_index, shape_index, shape_index))
                body.append("    for (int index = 0; index < length%d; index += 1)\n    {\n"
                            "        cells%d[index].x = index * 3 + %d;\n"
                            "        cells%d[index].y = (char)(index + 1);\n    }\n"
                            % (shape_index, shape_index, rng.randint(1, 9), shape_index))
                body.append("    u64 cell_total%d = (u64)sizeof(cells%d[0]);\n" % (shape_index, shape_index))
                body.append("    for (int index = 0; index < length%d; index += 1)\n    {\n"
                            "        cell_total%d = cell_total%d * 3u + (u64)cells%d[index].x + (u64)cells%d[index].y;\n    }\n"
                            % (shape_index, shape_index, shape_index, shape_index, shape_index))
                body.append("    sink(cell_total%d);\n" % shape_index)
            else:
                inner = rng.randint(2, 4)
                body.append("    %s matrix%d[length%d][%d];\n" % (element_spelling, shape_index, shape_index, inner))
                body.append("    for (int row = 0; row < length%d; row += 1)\n    {\n"
                            "        for (int col = 0; col < %d; col += 1)\n        {\n"
                            "            matrix%d[row][col] = %s(row * 5 + col);\n        }\n"
                            "    }\n"
                            % (shape_index, inner, shape_index, cast))
                body.append("    sink((u64)sizeof(matrix%d) + (u64)sizeof(matrix%d[0]) + %smatrix%d[length%d - 1][%d]);\n"
                            % (shape_index, shape_index, read, shape_index, shape_index, inner - 1))

    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: stmt_expr
# ---------------------------------------------------------------------------

def generate_stmt_expr_unit(rng, unit_index):
    prefix = "se%d" % unit_index
    globals_parts = []
    globals_parts.append("static int %s_counter;\n" % prefix)
    globals_parts.append(
        "static int %s_bump(void)\n{\n    %s_counter += 1;\n    return %s_counter;\n}\n"
        % (prefix, prefix, prefix)
    )
    globals_parts.append("static int %s_take(int value)\n{\n    return value * 2 + 1;\n}\n" % prefix)
    globals_parts.append("typedef struct %s_Pair\n{\n    int a;\n    int b;\n} %s_Pair;\n" % (prefix, prefix))

    body = []
    body.append("    %s_counter = 0;\n" % prefix)
    shape_pool = ["init", "call_arg", "loop_bound", "nested",
                  "inner_loop", "struct_value", "shadow", "subscript", "comma_tail"]
    shapes = rng.sample(shape_pool, rng.randint(3, 5))
    if rng.random() < 0.06:
        # Rejected today: a statement expression as the operand of a lazily
        # lowered control expression (if/while/ternary/&&) does not lower;
        # kept rare so the reject stays covered.
        shapes.append("condition")
    for shape_index, shape in enumerate(shapes):
        if shape == "init":
            body.append("    int init%d = ({ int t = %d; t * 3 + 1; });\n" % (shape_index, rng.randint(1, 20)))
            body.append("    sink((u64)init%d);\n" % shape_index)
        elif shape == "call_arg":
            body.append("    sink((u64)%s_take(({ int t = %s_bump(); t + %d; })));\n"
                        % (prefix, prefix, rng.randint(1, 9)))
            body.append("    sink((u64)%s_counter);\n" % prefix)
        elif shape == "condition":
            body.append("    if (({ int c = %s_bump(); c %% 2 == 1; }))\n    {\n        sink(21u);\n    }\n"
                        "    else\n    {\n        sink(22u);\n    }\n" % prefix)
            body.append("    sink((u64)%s_counter);\n" % prefix)
        elif shape == "loop_bound":
            body.append("    for (int index = 0; index < ({ %d; }); index += 1)\n    {\n"
                        "        sink((u64)(unsigned)(index * 3 + 1));\n    }\n" % rng.randint(2, 4))
        elif shape == "nested":
            body.append("    int nested%d = ({ int outer = ({ %d; }) + 2; outer * 3; });\n"
                        % (shape_index, rng.randint(1, 9)))
            body.append("    sink((u64)nested%d);\n" % shape_index)
        elif shape == "inner_loop":
            body.append("    int loop_value%d = ({ int accumulated = 0;"
                        " for (int j = 0; j < %d; j += 1) { accumulated += j * %d + 1; } accumulated; });\n"
                        % (shape_index, rng.randint(2, 5), rng.randint(1, 9)))
            body.append("    sink((u64)loop_value%d);\n" % shape_index)
        elif shape == "struct_value":
            body.append("    %s_Pair pair%d = ({ (%s_Pair){ %d, %d }; });\n"
                        % (prefix, shape_index, prefix, rng.randint(1, 30), rng.randint(1, 30)))
            body.append("    sink((u64)(pair%d.a * 10 + pair%d.b));\n" % (shape_index, shape_index))
        elif shape == "shadow":
            body.append("    int shade%d = %d;\n" % (shape_index, rng.randint(1, 9)))
            body.append("    int shaded%d = ({ int shade%d = %d; shade%d + 5; });\n"
                        % (shape_index, shape_index, rng.randint(20, 40), shape_index))
            body.append("    sink((u64)shade%d);\n    sink((u64)shaded%d);\n" % (shape_index, shape_index))
        elif shape == "subscript":
            body.append("    int items%d[4] = { 1, %d, %d, 4 };\n"
                        % (shape_index, rng.randint(2, 9), rng.randint(2, 9)))
            body.append("    int picked%d = items%d[({ %d; })];\n"
                        % (shape_index, shape_index, rng.randint(0, 3)))
            body.append("    sink((u64)picked%d);\n" % shape_index)
        else:  # comma_tail
            body.append("    int tail%d = ({ int t = %d; t += 2, t * 5; });\n" % (shape_index, rng.randint(1, 9)))
            body.append("    sink((u64)tail%d);\n" % shape_index)
    body.append("    sink((u64)%s_counter * 100u);\n" % prefix)
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: decl_list
# ---------------------------------------------------------------------------

def generate_decl_list_unit(rng, unit_index):
    prefix = "dl%d" % unit_index
    globals_parts = []
    body = []
    globals_parts.append("static volatile int %s_after;\n" % prefix)
    body.append("    %s_after = 0;\n" % prefix)

    shapes = rng.sample(["trailing_noreturn", "specifier_noreturn", "noreturn_typedef",
                         "object_list", "local_list"], rng.randint(2, 3))
    for shape_index, shape in enumerate(shapes):
        if shape == "trailing_noreturn":
            # The attribute belongs to one declarator of the list.  If it
            # leaks onto the sibling, the code after the call is dead to the
            # compiler and the counter update disappears.
            returned = rng.randint(10, 60)
            gone = "%s_gone%d" % (prefix, shape_index)
            keep = "%s_keep%d" % (prefix, shape_index)
            if rng.random() < 0.5:
                globals_parts.append("static int %s(void) __attribute__((noreturn)), %s(void);\n" % (gone, keep))
            else:
                globals_parts.append("static int %s(void), %s(void) __attribute__((noreturn));\n" % (keep, gone))
            globals_parts.append("static int %s(void)\n{\n    %s_after += 1;\n    return %d;\n}\n"
                                 % (keep, prefix, returned))
            body.append("    int kept%d = %s();\n" % (shape_index, keep))
            body.append("    %s_after += 100;\n" % prefix)
            body.append("    sink((u64)kept%d);\n" % shape_index)
            body.append("    sink((u64)%s_after);\n" % prefix)
        elif shape == "specifier_noreturn":
            returned = rng.randint(10, 60)
            globals_parts.append("static _Noreturn void %s_never%d(void);\n" % (prefix, shape_index))
            globals_parts.append("static int %s_ctrl%d(void)\n{\n    %s_after += 2;\n    return %d;\n}\n"
                                 % (prefix, shape_index, prefix, returned))
            body.append("    sink((u64)%s_ctrl%d());\n" % (prefix, shape_index))
            body.append("    %s_after += 10;\n" % prefix)
            body.append("    sink((u64)%s_after);\n" % prefix)
        elif shape == "noreturn_typedef":
            # noreturn carried only by a type must not delete code after
            # calls to unrelated functions declared alongside it.
            globals_parts.append("typedef void %s_nrty%d(void) __attribute__((noreturn));\n"
                                 % (prefix, shape_index))
            globals_parts.append("static %s_nrty%d %s_neverdef%d;\n" % (prefix, shape_index, prefix, shape_index))
            globals_parts.append("static %s_nrty%d* %s_slot%d;\n" % (prefix, shape_index, prefix, shape_index))
            body.append("    sink((u64)sizeof(%s_slot%d));\n" % (prefix, shape_index))
        elif shape == "object_list":
            attributed = rng.random() < 0.5
            array_text = "%s_arr%d[4] __attribute__((aligned(32)))" % (prefix, shape_index) if attributed \
                else "%s_arr%d[4]" % (prefix, shape_index)
            globals_parts.append("static int %s_x%d = %d, %s, *%s_ptr%d = &%s_x%d;\n"
                                 % (prefix, shape_index, rng.randint(1, 40), array_text,
                                    prefix, shape_index, prefix, shape_index))
            body.append("    %s_arr%d[0] = %d;\n" % (prefix, shape_index, rng.randint(1, 40)))
            body.append("    sink((u64)(*%s_ptr%d + %s_arr%d[0]));\n"
                        % (prefix, shape_index, prefix, shape_index))
            if attributed:
                body.append("    sink((u64)((unsigned long long)&%s_arr%d %% 32u));\n" % (prefix, shape_index))
        else:  # local_list
            body.append("    int narrow%d = %d, wide%d __attribute__((aligned(16))) = %d, last%d = %d;\n"
                        % (shape_index, rng.randint(1, 9), shape_index, rng.randint(1, 9),
                           shape_index, rng.randint(1, 9)))
            body.append("    sink((u64)(unsigned)(narrow%d + wide%d * 3 + last%d * 7));\n"
                        % (shape_index, shape_index, shape_index))
            body.append("    sink((u64)((unsigned long long)&wide%d %% 16u));\n" % shape_index)

    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: x87_ld
# ---------------------------------------------------------------------------

LONG_DOUBLE_VALUES = ["0.0L", "1.0L", "2.0L", "0.5L", "1.5L", "3.25L", "10.0L",
                      "1048576.0L", "0.0078125L", "123456789.0L", "-4.5L", "-24.0L"]


def generate_x87_ld_unit(rng, unit_index):
    prefix = "xl%d" % unit_index
    globals_parts = []
    body = []

    qualifier = "const " if rng.random() < 0.3 else ""
    globals_parts.append("static %slong double %s_g0 = %s;\n"
                         % (qualifier, prefix, rng.choice(LONG_DOUBLE_VALUES)))
    globals_parts.append("static long double %s_tab[3] = { %s, %s, %s };\n"
                         % (prefix, rng.choice(LONG_DOUBLE_VALUES), rng.choice(LONG_DOUBLE_VALUES),
                            rng.choice(LONG_DOUBLE_VALUES)))
    globals_parts.append("typedef struct %s_B\n{\n    long double magnitude;\n    int tag;\n} %s_B;\n"
                         % (prefix, prefix))
    globals_parts.append("static %s_B %s_boxed = { %s, %d };\n"
                         % (prefix, prefix, rng.choice(LONG_DOUBLE_VALUES), rng.randint(1, 9)))
    globals_parts.append("union %s_Shape\n{\n    long double value;\n    unsigned short halves[5];\n};\n"
                         % prefix)

    carry_helper = rng.random() < 0.7
    if carry_helper:
        globals_parts.append(
            "static %s_B %s_carry(%s_B v, long double delta)\n{\n"
            "    v.magnitude += delta;\n    v.tag += 1;\n    return v;\n}\n"
            % (prefix, prefix, prefix)
        )
    fma_helper = rng.random() < 0.6
    if fma_helper:
        globals_parts.append(
            "static long double %s_fma(long double a, long double b, long double c)\n{\n"
            "    return a * b + c;\n}\n" % prefix
        )
    low_helper = rng.random() < 0.5
    if low_helper:
        # Only the ten value bytes of the 80-bit format are defined; the
        # helper reads defined halfwords alone, but the union still crosses
        # the call boundary by value (the x87/INTEGER merge shape).
        globals_parts.append(
            "static unsigned long long %s_low(union %s_Shape s)\n{\n"
            "    return (unsigned long long)s.halves[0] + ((unsigned long long)s.halves[4] << 16);\n}\n"
            % (prefix, prefix)
        )
    overflow_fold = rng.random() < 0.2
    if overflow_fold:
        globals_parts.append("static long double %s_big = 1e5000L;\n" % prefix)

    body.append("    long double acc = %s_g0;\n" % prefix)
    operation_pool = [
        "acc = acc * 2.0L + %s;" % rng.choice(LONG_DOUBLE_VALUES),
        "acc = acc - %s_tab[1];" % prefix,
        "acc = acc / 4.0L;",
        "acc = -acc;",
        "acc = acc + %s_tab[2] * 0.5L;" % prefix,
    ]
    if fma_helper:
        operation_pool.append("acc = %s_fma(acc, %s, %s);"
                              % (prefix, rng.choice(["2.0L", "0.5L", "3.0L"]),
                                 rng.choice(LONG_DOUBLE_VALUES)))
    rng.shuffle(operation_pool)
    for operation in operation_pool[: rng.randint(2, 4)]:
        body.append("    %s\n" % operation)

    body.append("    double narrowed = (double)acc;\n")
    body.append("    sink((u64)(long long)(narrowed * 4.0));\n")
    body.append("    float single = (float)acc;\n")
    body.append("    sink((u64)(long long)single);\n")
    body.append("    long double widened = (long double)%dll * 0.5L;\n" % rng.randrange(1, 1 << 48))
    body.append("    sink((u64)(long long)(widened * 2.0L));\n")
    body.append("    long double from_unsigned = (long double)%dull;\n" % rng.randrange(1, 1 << 52))
    body.append("    sink((u64)(unsigned long long)from_unsigned);\n")
    body.append("    sink(acc > %s ? 3u : 4u);\n" % rng.choice(LONG_DOUBLE_VALUES))
    body.append("    sink(acc == acc ? 1u : 0u);\n")

    body.append("    union %s_Shape probe;\n" % prefix)
    body.append("    probe.value = acc;\n")
    body.append("    u64 folded = 0;\n")
    body.append("    for (int half = 0; half < 5; half += 1)\n    {\n"
                "        folded = folded * 31u + probe.halves[half];\n    }\n")
    body.append("    sink(folded);\n")
    if rng.random() < 0.4:
        body.append("    probe.value = -0.0L;\n")
        body.append("    sink((u64)(probe.halves[4] >> 15));\n")
    if low_helper:
        body.append("    probe.value = %s;\n" % rng.choice(LONG_DOUBLE_VALUES))
        body.append("    sink(%s_low(probe));\n" % prefix)
    if carry_helper:
        body.append("    %s_B moved = %s_carry(%s_boxed, 0.75L);\n" % (prefix, prefix, prefix))
        body.append("    sink((u64)(long long)(moved.magnitude * 8.0L));\n")
        body.append("    sink((u64)moved.tag);\n")
    if overflow_fold:
        body.append("    sink(%s_big > 1e300L ? 8u : 9u);\n" % prefix)
        body.append("    probe.value = %s_big;\n" % prefix)
        body.append("    sink((u64)probe.halves[4] * 65536u + (u64)probe.halves[3]);\n")
    body.append("    sink((u64)sizeof(long double) * 100u + (u64)_Alignof(long double));\n")
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Family: mixed_abi
# ---------------------------------------------------------------------------

MIXED_MEMBER_KINDS = ["int_scalar", "double_scalar", "byte_scalar", "long_double",
                      "complex_double", "complex_float", "bit_pair", "packed_sub",
                      "aligned_int", "atomic_int"]


def generate_mixed_abi_unit(rng, unit_index):
    prefix = "mx%d" % unit_index
    globals_parts = []
    body = []
    struct_tag = "%s_M" % prefix

    kinds = rng.sample(MIXED_MEMBER_KINDS, rng.randint(2, 4))
    packed_struct = rng.random() < 0.18 and "atomic_int" not in kinds
    aligned_struct = rng.random() < 0.15

    if "complex_double" in kinds:
        globals_parts.append("typedef union %s_PD\n{\n    double _Complex z;\n    double xy[2];\n} %s_PD;\n"
                             % (prefix, prefix))
    if "complex_float" in kinds:
        globals_parts.append("typedef union %s_PF\n{\n    float _Complex z;\n    float xy[2];\n} %s_PF;\n"
                             % (prefix, prefix))
    if "packed_sub" in kinds:
        globals_parts.append("typedef struct %s_Sub\n{\n    char inner_a;\n    int inner_b;\n}"
                             " __attribute__((packed)) %s_Sub;\n" % (prefix, prefix))

    members = []  # dicts: name, kind, decl, init text, member value(s)
    for kind_index, kind in enumerate(kinds):
        name = "field%d" % kind_index
        member = {"kind": kind, "name": name}
        if kind == "int_scalar":
            member["decl"] = "    int %s;" % name
            member["init"] = "%d" % rng.randint(1, 90)
        elif kind == "double_scalar":
            member["decl"] = "    double %s;" % name
            member["init"] = "%r" % rng.choice([1.5, 2.0, 3.5, 8.0, -2.5])
        elif kind == "byte_scalar":
            member["decl"] = "    unsigned char %s;" % name
            member["init"] = "%d" % rng.randint(1, 200)
        elif kind == "long_double":
            member["decl"] = "    long double %s;" % name
            member["init"] = rng.choice(["1.5L", "3.25L", "10.0L", "-4.5L"])
        elif kind == "complex_double":
            member["decl"] = "    double _Complex %s;" % name
            member["init"] = "%r + %ri" % (rng.choice([1.0, 2.5, 4.0]), rng.choice([0.5, 3.0, 6.0]))
        elif kind == "complex_float":
            member["decl"] = "    float _Complex %s;" % name
            member["init"] = "%rf + %rfi" % (rng.choice([1.0, 2.5, 4.0]), rng.choice([0.5, 3.0, 6.0]))
        elif kind == "bit_pair":
            member["decl"] = "    unsigned %s_lo : 5;\n    unsigned %s_hi : 11;" % (name, name)
            member["init"] = ("%du" % rng.randint(0, 31), "%du" % rng.randint(0, 2047))
        elif kind == "packed_sub":
            member["decl"] = "    %s_Sub %s;" % (prefix, name)
            member["init"] = "{ %d, %d }" % (rng.randint(1, 90), rng.randint(1, 900))
        elif kind == "aligned_int":
            member["decl"] = "    int %s __attribute__((aligned(16)));" % name
            member["init"] = "%d" % rng.randint(1, 90)
        else:  # atomic_int
            member["decl"] = "    _Atomic int %s;" % name
            member["init"] = "%d" % rng.randint(1, 90)
        members.append(member)

    attribute_parts = []
    if packed_struct:
        attribute_parts.append("packed")
    if aligned_struct:
        attribute_parts.append("aligned(%d)" % rng.choice([16, 32]))
    attribute_text = "__attribute__((%s))" % ", ".join(attribute_parts) if attribute_parts else ""
    member_text = "\n".join(member["decl"] for member in members)
    globals_parts.append("typedef struct %s\n{\n%s\n} %s %s;\n"
                         % (struct_tag, member_text, attribute_text, struct_tag)
                         if attribute_text else
                         "typedef struct %s\n{\n%s\n} %s;\n" % (struct_tag, member_text, struct_tag))

    churn_helper = rng.random() < 0.75
    if churn_helper:
        churn_lines = []
        for member in members:
            name = member["name"]
            if member["kind"] == "bit_pair":
                churn_lines.append("    value.%s_lo = (unsigned)(value.%s_lo + 1u) & 31u;" % (name, name))
            elif member["kind"] == "packed_sub":
                churn_lines.append("    value.%s.inner_b += delta;" % name)
            elif member["kind"] in ("complex_double", "complex_float"):
                churn_lines.append("    value.%s += delta;" % name)
            elif member["kind"] == "byte_scalar":
                churn_lines.append("    value.%s = (unsigned char)(value.%s + delta);" % (name, name))
            else:
                churn_lines.append("    value.%s += delta;" % name)
        globals_parts.append(
            "static %s %s_churn(%s value, int delta)\n{\n%s\n    return value;\n}\n"
            % (struct_tag, prefix, struct_tag, "\n".join(churn_lines))
        )

    designated = rng.random() < 0.5
    initializer_parts = []
    for member in members:
        name = member["name"]
        if member["kind"] == "bit_pair":
            low_value, high_value = member["init"]
            if designated:
                initializer_parts.append(".%s_lo = %s" % (name, low_value))
                initializer_parts.append(".%s_hi = %s" % (name, high_value))
            else:
                initializer_parts.append(low_value)
                initializer_parts.append(high_value)
        else:
            if designated:
                initializer_parts.append(".%s = %s" % (name, member["init"]))
            else:
                initializer_parts.append(member["init"])
    body.append("    %s base = { %s };\n" % (struct_tag, ", ".join(initializer_parts)))
    body.append("    %s duplicate = base;\n" % struct_tag)

    def sink_members(source):
        for member in members:
            name = member["name"]
            if member["kind"] == "bit_pair":
                body.append("    sink((u64)%s.%s_lo * 10000u + (u64)%s.%s_hi);\n"
                            % (source, name, source, name))
            elif member["kind"] == "packed_sub":
                body.append("    sink((u64)%s.%s.inner_a * 10000u + (u64)%s.%s.inner_b);\n"
                            % (source, name, source, name))
            elif member["kind"] == "complex_double":
                body.append("    %s_PD probe_%s_%s = { %s.%s };\n" % (prefix, source, name, source, name))
                body.append("    sink((u64)(long long)probe_%s_%s.xy[0]);\n" % (source, name))
                body.append("    sink((u64)(long long)probe_%s_%s.xy[1]);\n" % (source, name))
            elif member["kind"] == "complex_float":
                body.append("    %s_PF probe_%s_%s = { %s.%s };\n" % (prefix, source, name, source, name))
                body.append("    sink((u64)(long long)probe_%s_%s.xy[0]);\n" % (source, name))
                body.append("    sink((u64)(long long)probe_%s_%s.xy[1]);\n" % (source, name))
            elif member["kind"] in ("double_scalar", "long_double"):
                body.append("    sink((u64)(long long)(%s.%s * 8));\n" % (source, name))
            else:
                body.append("    sink((u64)%s.%s);\n" % (source, name))

    sink_members("duplicate")
    if churn_helper:
        body.append("    %s churned = %s_churn(base, %d);\n" % (struct_tag, prefix, rng.randint(1, 9)))
        sink_members("churned")

    body.append("    sink((u64)sizeof(%s) * 100u + (u64)_Alignof(%s));\n" % (struct_tag, struct_tag))
    for member in members:
        if member["kind"] == "bit_pair":
            continue
        body.append("    sink((u64)__builtin_offsetof(%s, %s));\n" % (struct_tag, member["name"]))
    body.append("    result = hash_state;\n")
    return Unit("".join(globals_parts), "".join(body))


# ---------------------------------------------------------------------------
# Driving
# ---------------------------------------------------------------------------

FAMILY_GENERATORS = {
    "enum_sizeof": generate_enum_sizeof_unit,
    "array_bound": generate_array_bound_unit,
    "sizeof_expr": generate_sizeof_expr_unit,
    "lazy_operand": generate_lazy_operand_unit,
    "union_vector": generate_union_vector_unit,
    "atomic_qual": generate_atomic_qual_unit,
    "complex_arith": generate_complex_arith_unit,
    "packed_aligned": generate_packed_aligned_unit,
    "bit_field": generate_bit_field_unit,
    "param_decl": generate_param_decl_unit,
    "stmt_expr": generate_stmt_expr_unit,
    "decl_list": generate_decl_list_unit,
    "x87_ld": generate_x87_ld_unit,
    "mixed_abi": generate_mixed_abi_unit,
}


def generate_program(family, seed, unit_count):
    # zlib.crc32 rather than hash(): the latter is salted per interpreter run,
    # and a seed must reproduce the same program forever.
    rng = random.Random(zlib.crc32(family.encode()) * 0x9E3779B9 + seed)
    generator = FAMILY_GENERATORS[family]
    units = [generator(rng, unit_index) for unit_index in range(unit_count)]
    return Program(family, seed, units)


@dataclass
class Observation:
    label: str
    compile_ok: bool
    compile_output: str
    compile_returncode: int
    run_returncode: int = None
    run_stdout: bytes = b""
    run_timeout: bool = False

    def behavior_key(self):
        return (self.run_returncode, self.run_stdout, self.run_timeout)


def clean_environment():
    environment = dict(os.environ)
    environment["LD_PRELOAD"] = ""
    return environment


def compile_and_run(label, compile_command, source_path, binary_path, work_directory):
    environment = clean_environment()
    command = compile_command + [source_path, "-o", binary_path]
    try:
        compile_process = subprocess.run(
            command, capture_output=True, timeout=COMPILE_TIMEOUT_SECONDS,
            cwd=work_directory, env=environment,
        )
    except subprocess.TimeoutExpired:
        return Observation(label, False, "compile timeout", -9999)
    output = (compile_process.stdout + compile_process.stderr).decode("utf-8", "replace")
    if compile_process.returncode != 0:
        return Observation(label, False, output, compile_process.returncode)
    observation = Observation(label, True, output, 0)
    try:
        run_process = subprocess.run(
            [binary_path], capture_output=True, timeout=RUN_TIMEOUT_SECONDS,
            cwd=work_directory, env=environment,
        )
        observation.run_returncode = run_process.returncode
        observation.run_stdout = run_process.stdout
    except subprocess.TimeoutExpired:
        observation.run_timeout = True
    return observation


@dataclass
class CaseResult:
    family: str
    seed: int
    category: str  # ok | behavior | rejects | ide-crash | run-crash | generator
    detail: str = ""
    observations: list = field(default_factory=list)


def modes(arguments):
    ide_path = os.path.abspath(arguments.ide)
    return [
        ("clang-O0", [arguments.cc, "-O0", "-w"]),
        ("clang-O2", [arguments.cc, "-O2", "-w"]),
        ("ide", [ide_path, "cc"]),
        ("ide-canon", [ide_path, "cc", "-fno-register-allocator"]),
    ]


def evaluate_case(arguments, family, seed, unit_count, work_directory, selected=None):
    program = generate_program(family, seed, unit_count)
    source_text = program.render(selected)
    tag = "%s_%d" % (family, seed)
    if selected is not None:
        tag += "_u" + "_".join(str(index) for index in selected)
    source_path = os.path.join(work_directory, tag + ".c")
    with open(source_path, "w") as source_file:
        source_file.write(source_text)
    observations = []
    for label, compile_command in modes(arguments):
        binary_path = os.path.join(work_directory, tag + "." + label)
        observations.append(compile_and_run(label, compile_command, source_path, binary_path, work_directory))
    result = classify(family, seed, observations)
    return result, source_path, source_text


def classify(family, seed, observations):
    reference, control, ide_fast, ide_canon = observations
    result = CaseResult(family, seed, "ok", observations=observations)
    if not reference.compile_ok or not control.compile_ok:
        result.category = "generator"
        result.detail = "clang rejected the generated program"
    elif reference.run_timeout or control.run_timeout:
        result.category = "generator"
        result.detail = "clang-built binary timed out"
    elif reference.behavior_key() != control.behavior_key():
        result.category = "generator"
        result.detail = "clang -O0 and -O2 disagree (undefined behavior in the generator)"
    else:
        for ide_observation in (ide_fast, ide_canon):
            if not ide_observation.compile_ok:
                if ide_observation.compile_returncode < 0:
                    result.category = "ide-crash"
                    result.detail = "%s: compiler terminated by signal %d" % (ide_observation.label, -ide_observation.compile_returncode)
                else:
                    result.category = "rejects"
                    result.detail = "%s: %s" % (ide_observation.label, ide_observation.compile_output.strip().splitlines()[-1] if ide_observation.compile_output.strip() else "no diagnostic")
            elif ide_observation.run_timeout:
                result.category = "behavior"
                result.detail = "%s: run timed out" % ide_observation.label
            elif ide_observation.behavior_key() != reference.behavior_key():
                if ide_observation.run_returncode is not None and ide_observation.run_returncode < 0:
                    result.category = "run-crash"
                    result.detail = "%s: binary terminated by signal %d" % (ide_observation.label, -ide_observation.run_returncode)
                else:
                    result.category = "behavior"
                    result.detail = "%s: exit/stdout differ" % ide_observation.label
            if result.category != "ok":
                break
    return result


def divergent_units(arguments, family, seed, unit_count, work_directory):
    """Re-run the same seed one unit at a time; returns the divergent indices."""
    indices = []
    for unit_index in range(unit_count):
        result, _, _ = evaluate_case(arguments, family, seed, unit_count, work_directory, selected=[unit_index])
        if result.category != "ok":
            indices.append((unit_index, result.category, result.detail))
    return indices


def write_report(divergence_directory, result, source_text, unit_findings):
    tag = "%s_%d" % (result.family, result.seed)
    os.makedirs(divergence_directory, exist_ok=True)
    with open(os.path.join(divergence_directory, tag + ".c"), "w") as source_file:
        source_file.write(source_text)
    with open(os.path.join(divergence_directory, tag + ".report.txt"), "w") as report_file:
        report_file.write("family=%s seed=%d category=%s\n" % (result.family, result.seed, result.category))
        report_file.write("detail: %s\n\n" % result.detail)
        for observation in result.observations:
            report_file.write("--- %s ---\n" % observation.label)
            report_file.write("compile_ok=%s returncode=%s\n" % (observation.compile_ok, observation.compile_returncode))
            if observation.compile_output.strip():
                report_file.write("compile output:\n%s\n" % observation.compile_output.strip())
            if observation.compile_ok:
                report_file.write("run returncode=%s timeout=%s\n" % (observation.run_returncode, observation.run_timeout))
                report_file.write("stdout:\n%s\n" % observation.run_stdout.decode("utf-8", "replace"))
            report_file.write("\n")
        if unit_findings:
            report_file.write("divergent units (isolated re-runs):\n")
            for unit_index, category, detail in unit_findings:
                report_file.write("  unit %d: %s (%s)\n" % (unit_index, category, detail))


def run_one(arguments, family, seed, work_root, divergence_directory):
    work_directory = os.path.join(work_root, "%s_%d" % (family, seed))
    os.makedirs(work_directory, exist_ok=True)
    result, _, source_text = evaluate_case(arguments, family, seed, arguments.units, work_directory)
    unit_findings = []
    if result.category != "ok":
        unit_findings = divergent_units(arguments, family, seed, arguments.units, work_directory)
        write_report(divergence_directory, result, source_text, unit_findings)
    shutil.rmtree(work_directory, ignore_errors=True)
    return result, unit_findings


def self_test():
    """The generators must be deterministic and must render compilable text."""
    failures = 0
    for family in FAMILY_GENERATORS:
        first = generate_program(family, 42, 4).render()
        second = generate_program(family, 42, 4).render()
        if first != second:
            print("self-test FAIL: %s is not deterministic" % family)
            failures += 1
        if "unit_3" not in first or "int main(void)" not in first:
            print("self-test FAIL: %s render is incomplete" % family)
            failures += 1
        single = generate_program(family, 42, 4).render(selected=[2])
        if "unit_2" not in single or "unit_1" in single:
            print("self-test FAIL: %s unit selection is wrong" % family)
            failures += 1
    print("self-test %s" % ("FAILED" if failures else "passed"))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--families", default=",".join(FAMILY_GENERATORS), help="comma-separated family list")
    parser.add_argument("--count", type=int, default=100, help="programs per family")
    parser.add_argument("--seed", type=int, default=1, help="first seed; program seeds are seed..seed+count-1")
    parser.add_argument("--units", type=int, default=6, help="units per program")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--ide", default=DEFAULT_IDE)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--isolate", default=None, help="family:seed — print the per-unit verdicts for one seed and keep its sources")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    exit_code = 0
    if arguments.self_test:
        exit_code = self_test()
    else:
        os.chdir(REPOSITORY_ROOT)
        if not os.path.exists(arguments.ide):
            print("missing %s — build it with ./build.sh build --config Release -t ide" % arguments.ide)
            exit_code = 1
        else:
            # Absolute: the compile and run subprocesses use the case's work
            # directory as their cwd, where a repository-relative path would
            # not resolve.
            output_root = os.path.abspath(OUTPUT_ROOT)
            work_root = os.path.join(output_root, "work")
            divergence_directory = os.path.join(output_root, "divergences")
            os.makedirs(work_root, exist_ok=True)
            if arguments.isolate:
                family, _, seed_text = arguments.isolate.partition(":")
                seed = int(seed_text)
                isolate_directory = os.path.join(output_root, "isolate_%s_%d" % (family, seed))
                os.makedirs(isolate_directory, exist_ok=True)
                result, source_path, _ = evaluate_case(arguments, family, seed, arguments.units, isolate_directory)
                print("whole program: %s (%s)" % (result.category, result.detail))
                for unit_index, category, detail in divergent_units(arguments, family, seed, arguments.units, isolate_directory):
                    print("unit %d: %s (%s)" % (unit_index, category, detail))
                print("sources kept under %s" % isolate_directory)
            else:
                families = [family.strip() for family in arguments.families.split(",") if family.strip()]
                unknown = [family for family in families if family not in FAMILY_GENERATORS]
                if unknown:
                    print("unknown families: %s" % ", ".join(unknown))
                    exit_code = 1
                else:
                    tasks = [(family, seed) for family in families
                             for seed in range(arguments.seed, arguments.seed + arguments.count)]
                    tallies = {}
                    divergence_count = 0
                    with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
                        futures = {pool.submit(run_one, arguments, family, seed, work_root, divergence_directory): (family, seed)
                                   for family, seed in tasks}
                        completed = 0
                        for future in concurrent.futures.as_completed(futures):
                            family, seed = futures[future]
                            result, unit_findings = future.result()
                            tallies.setdefault(family, {}).setdefault(result.category, 0)
                            tallies[family][result.category] += 1
                            completed += 1
                            if result.category != "ok":
                                divergence_count += 1
                                units_text = ",".join(str(unit_index) for unit_index, _, _ in unit_findings) or "-"
                                print("[%d/%d] %s seed=%d %s units=%s: %s"
                                      % (completed, len(tasks), family, seed, result.category, units_text, result.detail))
                            elif completed % 200 == 0:
                                print("[%d/%d] ..." % (completed, len(tasks)))
                    print("\nsummary (programs per verdict):")
                    for family in families:
                        family_tallies = tallies.get(family, {})
                        row = "  %-13s" % family
                        for category in ("ok", "behavior", "rejects", "ide-crash", "run-crash", "generator"):
                            row += " %s=%-5d" % (category, family_tallies.get(category, 0))
                        print(row)
                    if divergence_count:
                        print("\n%d divergent programs; sources and reports under %s" % (divergence_count, divergence_directory))
                        exit_code = 2
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
