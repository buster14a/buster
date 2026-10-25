// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The type table's tests, for the test-types command:
//
//   build/compiler test-types [--seed N] [--count N] [--clang PATH] [--keep]
//
// main.c includes this file after c_types.c and calls run_type_tests. The checks, in this order:
//   - Builtins: every builtin type's kind, code, size and alignment, from a table of clang 23's values of its own.
//   - Validation: requests clang refuses, each with the TypeError it gets, and the edge cases it accepts.
//   - Normalization: the spellings of one type that must get one TypeId (a qualified array and an array of qualified
//     elements, qualified parameters, a pointer to a qualified function, ALIGNED of ALIGNED, ALIGNED of a type's own
//     alignment, a vector of an ALIGNED element) and the ones that must not.
//   - Hash-consing: random constructions through every constructor, compared pair by pair with a structural
//     comparison of the test's own (an explicit stack over its terms, with the normalizations written again from
//     C's rules): equal structures get equal TypeIds, and different ones never do.
//   - Batches: random requests (some invalid) given to type_derive one at a time and to type_derive_batch in random
//     slices, in two tables that must end up identical, results, columns and index alike; and the index grown well
//     past its first size. Then ALIGNED requests of a tag completed since its wrapper was made, through both.
//   - Layout: fixtures (clang's corner cases), then random records of every member type, bit-fields of every width
//     next to every type, packed, #pragma pack, aligned, nesting, anonymous members and flexible array members, laid
//     out by the scalar layout in one table and by type_layout_records in another: identical offsets, sizes,
//     alignments and completion order. Member lookup finds the first member of a fresh table, directly and through an
//     anonymous member.
//   - Conversions: every pair of codes through both vector kernels and the scalar twin, at every length to 200 and at a
//     PROT_NONE page; type_usual_arithmetic_types against type_usual_arithmetic on every pair of a set of arithmetic
//     TypeIds (enums, _BitInts, ALIGNED and atomic types among them); and fixtures of clang's results.
//   - Compatibility and composite types: fixtures, the C standard's examples among them; types that share their
//     insides 64 levels deep, whose trees have 2^64 leaves, which each walk takes pair by distinct pair; composites of
//     chains of pointers 65,536 deep, whose canonical comparisons share one memo; and the walks with every amount of
//     room to 16 KB left in either scratch arena, which must answer right or say they couldn't.
//   - Printing: fixtures of clang's spellings.
//   - Clang, as the oracle (type_test_clang): a world of random types, records and enums,
//     written as C (a typedef per type, built from the earlier ones, so that the C spells each type independently of
//     the printer), with _Static_asserts of every size, alignment and member offset, of compatibility and of enums'
//     underlying types, which clang -fsyntax-only must accept; the same file compiled and run to check every
//     bit-field's placement (offsetof can't name one), and dumped (clang -Xclang -ast-dump) to compare clang's canonical
//     spelling of every type, every usual arithmetic conversion, promotion, bit-field promotion and composite type
//     with type_print's.
// Files go in a new directory under $TMPDIR (or /tmp), removed unless --keep. The exit status is 1 if the arguments
// are invalid or a check fails.
//
// Layout, in this order: the tally, the random numbers and the table helpers; the fixtures; hash-consing; batches;
// layout; conversions; compatibility; printing; run_type_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <spawn.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUSTER_TYPE_TEST_REPORTED 12
#define BUSTER_TYPE_TEST_ARENA_BYTES (1ull << 30)
#define BUSTER_TYPE_TEST_DEFAULT_COUNT 3000

typedef struct TypeTestTally TypeTestTally;
struct TypeTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool type_test_expect(TypeTestTally* tally, bool condition, const char* check, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_TYPE_TEST_REPORTED)
        {
            fprintf(stderr, "test-types: %s: %s\n", check, detail);
        }
        tally->failures += 1;
    }
    return condition;
}

static bool type_test_expect_number(TypeTestTally* tally, bool condition, const char* check, const char* what, uint64_t a, uint64_t b)
{
    char detail[256];
    snprintf(detail, sizeof(detail), "%s: %" PRIu64 " against %" PRIu64, what, a, b);
    return type_test_expect(tally, condition, check, detail);
}

static void type_test_report(const char* check, TypeTestTally tally)
{
    printf("%-34s %10" PRIu64 " checks, %" PRIu64 " failed\n", check, tally.checks, tally.failures);
}

// splitmix64 (THIRD_PARTY_NOTICES.md has its reference), from the seed times its increment.
static uint64_t type_test_random(uint64_t* state)
{
    *state += 0x9E3779B97F4A7C15ull;
    uint64_t result = *state;
    result = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9ull;
    result = (result ^ (result >> 27)) * 0x94D049BB133111EBull;
    return result ^ (result >> 31);
}

static uint64_t type_test_below(uint64_t* state, uint64_t bound)
{
    return bound == 0 ? 0 : type_test_random(state) % bound;
}

// A table with room for `types` types and as many parameters, tags and members, in arenas of its own.
typedef struct TypeTestTable TypeTestTable;
struct TypeTestTable
{
    TypeTable table;
    Arena arena;
    Arena index_arena;
};

static bool type_test_table_open(TypeTestTable* test, uint32_t types)
{
    const TypeCapacity capacity = {
        .types = types,
        .parameters = types * 4,
        .tags = types,
        .members = types * 8,
    };
    test->arena = arena_reserve(type_capacity_bytes(capacity));
    test->index_arena = arena_reserve(type_index_capacity_bytes(capacity));
    return type_initialize(&test->table, &test->arena, &test->index_arena, capacity);
}

static void type_test_table_close(TypeTestTable* test)
{
    arena_release(&test->arena);
    arena_release(&test->index_arena);
}

// A record's members from a short list, for the fixtures: name 0 is unnamed, width BUSTER_TYPE_NOT_BIT_FIELD isn't a
// bit-field.
static TypeMember type_test_member(uint32_t name, TypeId type, uint32_t width)
{
    TypeMember result = {
        .name = name,
        .type = type,
        .bit_width = width,
        .alignment_log2 = 0,
        .packed = false,
    };
    return result;
}

static TypeId type_test_record(TypeTable* table, uint8_t kind, const TypeMember* members, uint32_t count, TypeRecordAttributes attributes)
{
    const TypeId record = type_tag(table, kind, 1);
    return type_complete_record(table, record, members, count, attributes) ? record : 0;
}

// A member's offset in bits, by its position in its record.
static uint64_t type_test_offset(const TypeTable* table, TypeId record, uint32_t member)
{
    return table->members.offset[table->tags.member_begin[table->types.index[type_index(record)]] + member];
}

// type_print into scratch memory, NUL-terminated, for comparisons.
static bool type_test_prints(const TypeTable* table, TypeId type, uint32_t options, const char* expected, Arena* arena)
{
    const uint64_t length = arena->length;
    const String8 printed = type_print(table, arena, type, (String8){ 0 }, 0, options);
    const bool result = printed.length == strlen(expected) && memcmp(printed.pointer, expected, printed.length) == 0;
    if (!result)
    {
        fprintf(stderr, "test-types: printing: \"%.*s\" against \"%s\"\n", (int)printed.length, printed.pointer, expected);
    }
    arena_pop_to(arena, length);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fixtures

typedef struct TypeTestBuiltin TypeTestBuiltin;
struct TypeTestBuiltin
{
    TypeId type;
    uint8_t kind;
    uint64_t size;
    uint64_t alignment;
};

// clang 23's sizes and alignments on x86-64 Linux, measured with sizeof and _Alignof.
static const TypeTestBuiltin type_test_builtins[] = {
    { BUSTER_TYPE_BOOL, BUSTER_TYPE_KIND_INTEGER, 1, 1 },
    { BUSTER_TYPE_CHAR, BUSTER_TYPE_KIND_INTEGER, 1, 1 },
    { BUSTER_TYPE_SCHAR, BUSTER_TYPE_KIND_INTEGER, 1, 1 },
    { BUSTER_TYPE_UCHAR, BUSTER_TYPE_KIND_INTEGER, 1, 1 },
    { BUSTER_TYPE_SHORT, BUSTER_TYPE_KIND_INTEGER, 2, 2 },
    { BUSTER_TYPE_USHORT, BUSTER_TYPE_KIND_INTEGER, 2, 2 },
    { BUSTER_TYPE_INT, BUSTER_TYPE_KIND_INTEGER, 4, 4 },
    { BUSTER_TYPE_UINT, BUSTER_TYPE_KIND_INTEGER, 4, 4 },
    { BUSTER_TYPE_LONG, BUSTER_TYPE_KIND_INTEGER, 8, 8 },
    { BUSTER_TYPE_ULONG, BUSTER_TYPE_KIND_INTEGER, 8, 8 },
    { BUSTER_TYPE_LLONG, BUSTER_TYPE_KIND_INTEGER, 8, 8 },
    { BUSTER_TYPE_ULLONG, BUSTER_TYPE_KIND_INTEGER, 8, 8 },
    { BUSTER_TYPE_INT128, BUSTER_TYPE_KIND_INTEGER, 16, 16 },
    { BUSTER_TYPE_UINT128, BUSTER_TYPE_KIND_INTEGER, 16, 16 },
    { BUSTER_TYPE_BF16, BUSTER_TYPE_KIND_FLOATING, 2, 2 },
    { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_KIND_FLOATING, 2, 2 },
    { BUSTER_TYPE_HALF, BUSTER_TYPE_KIND_FLOATING, 2, 2 },
    { BUSTER_TYPE_FLOAT, BUSTER_TYPE_KIND_FLOATING, 4, 4 },
    { BUSTER_TYPE_DOUBLE, BUSTER_TYPE_KIND_FLOATING, 8, 8 },
    { BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_KIND_FLOATING, 16, 16 },
    { BUSTER_TYPE_FLOAT128, BUSTER_TYPE_KIND_FLOATING, 16, 16 },
    { BUSTER_TYPE_NULLPTR, BUSTER_TYPE_KIND_NULLPTR, 8, 8 },
    { BUSTER_TYPE_VOID, BUSTER_TYPE_KIND_VOID, 1, 1 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_CHAR), BUSTER_TYPE_KIND_COMPLEX, 2, 1 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_SHORT), BUSTER_TYPE_KIND_COMPLEX, 4, 2 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT), BUSTER_TYPE_KIND_COMPLEX, 8, 4 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_LONG), BUSTER_TYPE_KIND_COMPLEX, 16, 8 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT128), BUSTER_TYPE_KIND_COMPLEX, 32, 16 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_BF16), BUSTER_TYPE_KIND_COMPLEX, 4, 2 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT16), BUSTER_TYPE_KIND_COMPLEX, 4, 2 },
    { BUSTER_TYPE_COMPLEX_FLOAT, BUSTER_TYPE_KIND_COMPLEX, 8, 4 },
    { BUSTER_TYPE_COMPLEX_DOUBLE, BUSTER_TYPE_KIND_COMPLEX, 16, 8 },
    { BUSTER_TYPE_COMPLEX_LONG_DOUBLE, BUSTER_TYPE_KIND_COMPLEX, 32, 16 },
    { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT128), BUSTER_TYPE_KIND_COMPLEX, 32, 16 },
};

static bool type_test_builtin_types(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 64), "builtins", "the table can't be made"))
    {
        const TypeTable* table = &test.table;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(type_test_builtins); i += 1)
        {
            const TypeTestBuiltin* builtin = &type_test_builtins[i];
            char what[64];
            snprintf(what, sizeof(what), "builtin %u", type_index(builtin->type));
            type_test_expect(&tally, type_is_valid(table, builtin->type) && type_kind(table, builtin->type) == builtin->kind, "builtins", what);
            type_test_expect_number(&tally, type_size(table, builtin->type) == builtin->size, "builtins size", what, type_size(table, builtin->type), builtin->size);
            type_test_expect_number(&tally, type_alignment(table, builtin->type) == builtin->alignment, "builtins alignment", what, type_alignment(table, builtin->type),
                                    builtin->alignment);
            type_test_expect(&tally, type_arithmetic_code(table, builtin->type) == type_index(builtin->type), "builtins code", what);
            type_test_expect(&tally, type_is_complete(table, builtin->type) == (builtin->kind != BUSTER_TYPE_KIND_VOID), "builtins completeness", what);
        }
        // The indices without a type: complex bool and __fp16, and the gaps.
        const uint32_t empty[] = {
            0,
            BUSTER_TYPE_CODE_BIT_INT,
            BUSTER_TYPE_CODE_UNSIGNED_BIT_INT,
            BUSTER_TYPE_CODE_DEFERRED,
            BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_BOOL,
            BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_HALF,
            BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_NULLPTR,
            63,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(empty); i += 1)
        {
            type_test_expect(&tally, !type_is_valid(table, BUSTER_TYPE_BUILTIN(empty[i])), "builtins", "an index without a type is valid");
        }
        type_test_table_close(&test);
    }
    type_test_report("builtins", tally);
    return tally.failures == 0;
}

// A request and the error it must get (TYPE_ERROR_NONE: it must succeed).
static void type_test_error(TypeTestTally* tally, TypeTable* table, TypeId result, TypeError expected, const char* what)
{
    char detail[128];
    snprintf(detail, sizeof(detail), "%s: result %u, error %d against %d", what, result, (int)table->last_error, (int)expected);
    type_test_expect(tally, expected == TYPE_ERROR_NONE ? result != 0 : result == 0 && table->last_error == expected, "validation", detail);
    table->last_error = TYPE_ERROR_NONE;
}

static bool type_test_validation(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 1000), "validation", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId incomplete = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        const TypeId function = type_function(table, BUSTER_TYPE_INT, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId int_array = type_array(table, BUSTER_TYPE_INT, 3);
        const TypeId open_array = type_incomplete_array(table, BUSTER_TYPE_INT);
        const TypeId atomic = type_atomic(table, BUSTER_TYPE_INT);
        const TypeId bit_int = type_bit_int(table, 7, false);
        const TypeId aligned8 = type_aligned(table, BUSTER_TYPE_INT, 3);
        type_test_error(&tally, table, type_array(table, BUSTER_TYPE_VOID, 1), TYPE_ERROR_ELEMENT, "array of void");
        type_test_error(&tally, table, type_array(table, function, 1), TYPE_ERROR_ELEMENT, "array of a function");
        type_test_error(&tally, table, type_array(table, incomplete, 1), TYPE_ERROR_ELEMENT, "array of an incomplete struct");
        type_test_error(&tally, table, type_array(table, open_array, 2), TYPE_ERROR_ELEMENT, "array of an array of unknown size");
        type_test_error(&tally, table, type_array(table, enumeration, 2), TYPE_ERROR_ELEMENT, "array of an incomplete enum");
        type_test_error(&tally, table, type_array(table, aligned8, 2), TYPE_ERROR_ELEMENT_ALIGNMENT, "array of an int aligned to 8");
        type_test_error(&tally, table, type_array(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_MAX_OBJECT_BYTES), TYPE_ERROR_NONE, "array of 2^61 - 1 chars");
        type_test_error(&tally, table, type_array(table, BUSTER_TYPE_CHAR, BUSTER_TYPE_MAX_OBJECT_BYTES + 1), TYPE_ERROR_TOO_LARGE, "array of 2^61 chars");
        type_test_error(&tally, table, type_array(table, BUSTER_TYPE_INT, 1ull << 62), TYPE_ERROR_TOO_LARGE, "array whose size overflows");
        type_test_error(&tally, table, type_array(table, BUSTER_TYPE_INT, 0), TYPE_ERROR_NONE, "array of length 0");
        type_test_error(&tally, table, type_incomplete_array(table, function), TYPE_ERROR_ELEMENT, "incomplete array of a function");
        type_test_error(&tally, table, type_variable_array(table, int_array, 0), TYPE_ERROR_NONE, "[*] of an array");
        type_test_error(&tally, table, type_function(table, int_array, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_RETURN, "function returning an array");
        type_test_error(&tally, table, type_function(table, function, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_RETURN, "function returning a function");
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_HALF, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_RETURN, "function returning __fp16");
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_VOID | BUSTER_TYPE_CONST, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_NONE,
                        "function returning const void");
        const TypeId bad_parameters[] = {
            BUSTER_TYPE_VOID,
            int_array,
            function,
            BUSTER_TYPE_HALF,
        };
        const char* bad_parameter_names[] = {
            "void parameter",
            "array parameter",
            "function parameter",
            "__fp16 parameter",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad_parameters); i += 1)
        {
            type_test_error(&tally, table, type_function(table, BUSTER_TYPE_INT, &bad_parameters[i], 1, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_PARAMETER,
                            bad_parameter_names[i]);
        }
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_INT, &bad_parameters[0], 0, BUSTER_TYPE_FLAG_VARIADIC), TYPE_ERROR_OPERAND,
                        "variadic without a prototype");
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, 0), TYPE_ERROR_OPERAND,
                        "parameters without a prototype");
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_INT, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE | BUSTER_TYPE_FLAG_VARIADIC), TYPE_ERROR_NONE,
                        "(...) alone, as C23 has it");
        type_test_error(&tally, table, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ incomplete }, 1, BUSTER_TYPE_FLAG_PROTOTYPE), TYPE_ERROR_NONE,
                        "incomplete struct parameter, as a declaration has it");
        type_test_error(&tally, table, type_atomic(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST), TYPE_ERROR_ATOMIC, "_Atomic(const int)");
        type_test_error(&tally, table, type_atomic(table, int_array), TYPE_ERROR_ATOMIC, "_Atomic of an array");
        type_test_error(&tally, table, type_atomic(table, function), TYPE_ERROR_ATOMIC, "_Atomic of a function");
        type_test_error(&tally, table, type_atomic(table, atomic), TYPE_ERROR_ATOMIC, "_Atomic of _Atomic");
        type_test_error(&tally, table, type_atomic(table, bit_int), TYPE_ERROR_ATOMIC, "_Atomic(_BitInt(7)), refused by clang 23");
        type_test_error(&tally, table, type_atomic(table, incomplete), TYPE_ERROR_ATOMIC, "_Atomic of an incomplete struct");
        type_test_error(&tally, table, type_atomic(table, BUSTER_TYPE_VOID), TYPE_ERROR_ATOMIC, "_Atomic(void)");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_BOOL, 8, false), TYPE_ERROR_VECTOR, "vector_size of bool");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_BOOL, 8, true), TYPE_ERROR_NONE, "ext_vector_type of bool");
        type_test_error(&tally, table, type_vector(table, type_pointer(table, BUSTER_TYPE_INT), 2, false), TYPE_ERROR_VECTOR, "vector of pointers");
        type_test_error(&tally, table, type_vector(table, bit_int, 2, true), TYPE_ERROR_VECTOR, "vector of _BitInt(7)");
        type_test_error(&tally, table, type_vector(table, type_bit_int(table, 4, false), 2, true), TYPE_ERROR_NONE, "vector of _BitInt(4)");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_COMPLEX_FLOAT, 2, false), TYPE_ERROR_VECTOR, "vector of _Complex float");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_INT, 0, false), TYPE_ERROR_VECTOR, "vector of no elements");
        type_test_error(&tally, table, type_vector(table, atomic, 4, false), TYPE_ERROR_VECTOR, "vector of _Atomic int");
        type_test_error(&tally, table, type_vector(table, enumeration, 4, true), TYPE_ERROR_VECTOR, "ext vector of an incomplete enum");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_CHAR, 1u << 28, false), TYPE_ERROR_NONE, "vector of 2^28 chars");
        type_test_error(&tally, table, type_vector(table, BUSTER_TYPE_CHAR, (1u << 28) + 1, false), TYPE_ERROR_TOO_LARGE, "vector past 2^31 bits");
        type_test_error(&tally, table, type_vector_size(table, BUSTER_TYPE_INT, 6), TYPE_ERROR_VECTOR, "vector_size(6) of int");
        type_test_error(&tally, table, type_vector_size(table, BUSTER_TYPE_INT, 12), TYPE_ERROR_NONE, "vector_size(12) of int");
        type_test_error(&tally, table, type_bit_int(table, 1, false), TYPE_ERROR_BIT_INT, "_BitInt(1)");
        type_test_error(&tally, table, type_bit_int(table, 1, true), TYPE_ERROR_NONE, "unsigned _BitInt(1)");
        type_test_error(&tally, table, type_bit_int(table, 0, true), TYPE_ERROR_BIT_INT, "unsigned _BitInt(0)");
        type_test_error(&tally, table, type_bit_int(table, BUSTER_TYPE_MAX_BIT_INT_WIDTH, false), TYPE_ERROR_NONE, "_BitInt(8388608)");
        type_test_error(&tally, table, type_bit_int(table, BUSTER_TYPE_MAX_BIT_INT_WIDTH + 1, false), TYPE_ERROR_BIT_INT, "_BitInt(8388609)");
        type_test_error(&tally, table, type_complex(table, BUSTER_TYPE_BOOL), TYPE_ERROR_COMPLEX, "_Complex bool");
        type_test_error(&tally, table, type_complex(table, BUSTER_TYPE_HALF), TYPE_ERROR_COMPLEX, "_Complex __fp16");
        type_test_error(&tally, table, type_complex(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST), TYPE_ERROR_COMPLEX, "_Complex of const int");
        type_test_error(&tally, table, type_complex(table, enumeration), TYPE_ERROR_COMPLEX, "_Complex of an enum");
        type_test_error(&tally, table, type_complex(table, type_pointer(table, BUSTER_TYPE_INT)), TYPE_ERROR_COMPLEX, "_Complex of a pointer");
        type_test_error(&tally, table, type_complex(table, BUSTER_TYPE_INT128), TYPE_ERROR_NONE, "_Complex __int128, which the conversions make");
        type_test_error(&tally, table, type_complex(table, bit_int), TYPE_ERROR_NONE, "_Complex _BitInt(7), which the conversions make");
        type_test_error(&tally, table, type_aligned(table, function, 4), TYPE_ERROR_ALIGNMENT, "aligned function");
        type_test_error(&tally, table, type_aligned(table, BUSTER_TYPE_INT, BUSTER_TYPE_MAX_ALIGNMENT_LOG2 + 1), TYPE_ERROR_ALIGNMENT, "aligned past 2^32");
        type_test_error(&tally, table, type_aligned(table, BUSTER_TYPE_INT, 0), TYPE_ERROR_NONE, "int aligned to 1");
        type_test_error(&tally, table, type_pointer(table, 0), TYPE_ERROR_OPERAND, "pointer to none");
        type_test_error(&tally, table, type_pointer(table, (TypeId)(table->types.count + 5) << BUSTER_TYPE_INDEX_SHIFT), TYPE_ERROR_OPERAND, "pointer to a type past the table");
        type_test_error(&tally, table, type_pointer(table, BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BIT_INT)), TYPE_ERROR_OPERAND, "pointer to an index without a type");
        type_test_error(&tally, table, type_tag(table, BUSTER_TYPE_KIND_POINTER, 3), TYPE_ERROR_OPERAND, "a tag of kind POINTER");
        // Members.
        const TypeRecordAttributes plain = { 0 };
        TypeMember members[3];
        members[0] = type_test_member(1, incomplete, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_MEMBER, "member of an incomplete type");
        members[0] = type_test_member(1, function, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_MEMBER, "member of a function type");
        members[0] = type_test_member(1, BUSTER_TYPE_VOID, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_MEMBER, "void member");
        members[0] = type_test_member(1, type_variable_array(table, BUSTER_TYPE_INT, 9), BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_MEMBER, "variable length array member");
        members[0] = type_test_member(1, open_array, BUSTER_TYPE_NOT_BIT_FIELD);
        members[1] = type_test_member(2, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 2, plain), TYPE_ERROR_MEMBER, "flexible array member not last");
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_NONE, "flexible array member alone");
        members[0] = type_test_member(1, BUSTER_TYPE_FLOAT, 3);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_BIT_FIELD, "float bit-field");
        members[0] = type_test_member(1, atomic, 3);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_BIT_FIELD, "_Atomic int bit-field");
        members[0] = type_test_member(1, BUSTER_TYPE_BOOL, 2);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_BIT_FIELD, "bool : 2");
        members[0] = type_test_member(1, bit_int, 8);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_BIT_FIELD, "_BitInt(7) : 8");
        members[0] = type_test_member(1, BUSTER_TYPE_INT, 0);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_BIT_FIELD, "named int : 0");
        members[0] = type_test_member(0, BUSTER_TYPE_INT, 0);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_NONE, "int : 0");
        members[0] = type_test_member(1, BUSTER_TYPE_INT128, 100);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_NONE, "__int128 : 100");
        members[0] = type_test_member(1, enumeration, 3);
        type_test_error(&tally, table, type_test_record(table, BUSTER_TYPE_KIND_STRUCT, members, 1, plain), TYPE_ERROR_MEMBER, "bit-field of an incomplete enum");
        const TypeId defined = type_tag(table, BUSTER_TYPE_KIND_UNION, 4);
        members[0] = type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_error(&tally, table, type_record_define(table, defined, members, 1, plain) ? defined : 0, TYPE_ERROR_NONE, "a union defined");
        type_test_error(&tally, table, type_record_define(table, defined, members, 1, plain) ? defined : 0, TYPE_ERROR_TAG, "a union defined twice");
        type_test_error(&tally, table, type_record_layout(table, defined) ? defined : 0, TYPE_ERROR_NONE, "a union laid out");
        type_test_error(&tally, table, type_record_layout(table, defined) ? defined : 0, TYPE_ERROR_TAG, "a union laid out twice");
        type_test_error(&tally, table, type_record_layout(table, enumeration) ? enumeration : 0, TYPE_ERROR_TAG, "an enum laid out");
        type_test_error(&tally, table, type_enum_complete(table, enumeration, type_bit_int(table, 8, false), 0) ? enumeration : 0, TYPE_ERROR_UNDERLYING,
                        "an enum of _BitInt(8)");
        type_test_error(&tally, table, type_enum_complete(table, enumeration, BUSTER_TYPE_FLOAT, 0) ? enumeration : 0, TYPE_ERROR_UNDERLYING, "an enum of float");
        type_test_error(&tally, table, type_enum_complete(table, enumeration, BUSTER_TYPE_BOOL | BUSTER_TYPE_CONST, 0) ? enumeration : 0, TYPE_ERROR_NONE,
                        "an enum of const bool, without its const");
        type_test_expect(&tally, table->types.base[type_index(enumeration)] == BUSTER_TYPE_BOOL, "validation", "the enum's underlying type kept its const");
        type_test_error(&tally, table, type_enum_complete(table, enumeration, BUSTER_TYPE_INT, 0) ? enumeration : 0, TYPE_ERROR_TAG, "an enum completed twice");
        type_test_table_close(&test);
    }
    {
        // Capacities: a table with room for two types and one tag fails past them, and an arena too small for the
        // columns leaves both arenas as they were.
        const TypeCapacity small = {
            .types = 2,
            .parameters = 1,
            .tags = 1,
            .members = 1,
        };
        Arena arena = arena_reserve(type_capacity_bytes(small));
        Arena index_arena = arena_reserve(type_index_capacity_bytes(small));
        TypeTable table;
        type_test_expect(&tally, type_initialize(&table, &arena, &index_arena, small), "capacity", "a small table can't be made");
        const TypeId pointer = type_pointer(&table, BUSTER_TYPE_INT);
        const TypeId record = type_tag(&table, BUSTER_TYPE_KIND_STRUCT, 1);
        type_test_expect(&tally, pointer != 0 && record != 0, "capacity", "two types don't fit");
        // Room for one member besides record 0, the table's own.
        const TypeMember two[2] = {
            type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(2, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_test_expect(&tally, !type_record_define(&table, record, two, 2, (TypeRecordAttributes){ 0 }) && table.last_error == TYPE_ERROR_FULL, "capacity",
                         "two members fit in room for one");
        type_test_expect(&tally, type_record_define(&table, record, two, 1, (TypeRecordAttributes){ 0 }), "capacity", "one member doesn't fit in room for one");
        type_test_expect(&tally, type_pointer(&table, BUSTER_TYPE_LONG) == 0 && table.last_error == TYPE_ERROR_FULL, "capacity", "a third type fits");
        type_test_expect(&tally, type_pointer(&table, BUSTER_TYPE_INT) != 0, "capacity", "an existing type isn't found when the table is full");
        type_test_expect(&tally, type_function(&table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT, BUSTER_TYPE_INT }, 2, BUSTER_TYPE_FLAG_PROTOTYPE) == 0 &&
                                     table.last_error == TYPE_ERROR_FULL,
                         "capacity", "two parameters fit in room for one");
        arena_release(&arena);
        arena_release(&index_arena);
        Arena tiny = arena_reserve(64);
        Arena tiny_index = arena_reserve(64);
        const uint64_t lengths[2] = {
            tiny.length,
            tiny_index.length,
        };
        type_test_expect(&tally, !type_initialize(&table, &tiny, &tiny_index, small) && table.last_error == TYPE_ERROR_FULL, "capacity", "a table fits in 64 bytes");
        type_test_expect(&tally, tiny.length == lengths[0] && tiny_index.length == lengths[1], "capacity", "a failed initialization left pushes behind");
        arena_release(&tiny);
        arena_release(&tiny_index);
    }
    type_test_report("validation", tally);
    return tally.failures == 0;
}

static bool type_test_normalization(Arena* arena)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 1000), "normalization", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId const_int = BUSTER_TYPE_INT | BUSTER_TYPE_CONST;
        const TypeId array_of_const = type_array(table, const_int, 3);
        const TypeId const_array = type_qualify(table, type_array(table, BUSTER_TYPE_INT, 3), BUSTER_TYPE_CONST);
        type_test_expect(&tally, array_of_const == const_array && type_qualifiers(array_of_const) == BUSTER_TYPE_CONST, "normalization",
                         "an array of const int and a const int[3]");
        type_test_expect(&tally, type_element(table, array_of_const) == const_int, "normalization", "a const array's element isn't const");
        const TypeId nested = type_array(table, array_of_const, 2);
        type_test_expect(&tally, nested == type_qualify(table, type_array(table, type_array(table, BUSTER_TYPE_INT, 3), 2), BUSTER_TYPE_CONST), "normalization",
                         "a const int[2][3] spelled two ways");
        type_test_expect(&tally, type_pointer(table, array_of_const) != type_pointer(table, type_array(table, BUSTER_TYPE_INT, 3)), "normalization",
                         "pointers to const and plain arrays are one");
        const TypeId with_const = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ const_int }, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        const TypeId without = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        type_test_expect(&tally, with_const == without, "normalization", "int (const int) and int (int)");
        const TypeId const_return = type_function(table, const_int, (TypeId[]){ BUSTER_TYPE_INT }, 1, BUSTER_TYPE_FLAG_PROTOTYPE);
        type_test_expect(&tally, const_return != without, "normalization", "const int (int) and int (int) are one");
        type_test_expect(&tally, type_qualify(table, without, BUSTER_TYPE_CONST) == without, "normalization", "a function kept its const");
        type_test_expect(&tally, type_pointer(table, without | BUSTER_TYPE_VOLATILE) == type_pointer(table, without), "normalization",
                         "a pointer to a volatile function");
        const TypeId aligned1 = type_aligned(table, BUSTER_TYPE_INT, 0);
        type_test_expect(&tally, type_aligned(table, aligned1, 3) == type_aligned(table, BUSTER_TYPE_INT, 3), "normalization", "aligned of aligned");
        type_test_expect(&tally, type_aligned(table, BUSTER_TYPE_INT, 2) == BUSTER_TYPE_INT, "normalization", "int aligned to its own alignment");
        type_test_expect(&tally, type_aligned(table, const_int, 0) == (aligned1 | BUSTER_TYPE_CONST), "normalization", "aligned's qualifiers");
        type_test_expect(&tally, type_vector(table, aligned1, 4, false) == type_vector(table, BUSTER_TYPE_INT, 4, false), "normalization",
                         "a vector of an aligned int");
        type_test_expect(&tally, type_vector(table, const_int, 4, false) != type_vector(table, BUSTER_TYPE_INT, 4, false), "normalization",
                         "vectors of const int and int are one");
        type_test_expect(&tally, type_vector(table, BUSTER_TYPE_INT, 4, true) != type_vector(table, BUSTER_TYPE_INT, 4, false), "normalization",
                         "GNU and clang vectors are one");
        type_test_expect(&tally, type_complex(table, BUSTER_TYPE_FLOAT) == BUSTER_TYPE_COMPLEX_FLOAT, "normalization", "_Complex float isn't the builtin");
        const TypeId bit_int = type_bit_int(table, 13, true);
        type_test_expect(&tally, type_complex(table, bit_int) == type_complex(table, bit_int) && type_kind(table, type_complex(table, bit_int)) == BUSTER_TYPE_KIND_COMPLEX,
                         "normalization", "a complex _BitInt");
        type_test_expect(&tally, type_bit_int(table, 13, true) == bit_int && type_bit_int(table, 13, false) != bit_int, "normalization", "_BitInt(13)");
        type_test_expect(&tally, type_adjust_parameter(table, array_of_const) == type_pointer(table, const_int), "normalization", "a const int[3] parameter");
        type_test_expect(&tally, type_adjust_parameter(table, without) == type_pointer(table, without), "normalization", "a function parameter");
        type_test_expect(&tally, type_adjust_parameter(table, const_int) == BUSTER_TYPE_INT, "normalization", "a const int parameter");
        type_test_expect(&tally, type_incomplete_array(table, const_int) == type_qualify(table, type_incomplete_array(table, BUSTER_TYPE_INT), BUSTER_TYPE_CONST),
                         "normalization", "const int[]");
        type_test_expect(&tally, type_variable_array(table, BUSTER_TYPE_INT, 5) != type_variable_array(table, BUSTER_TYPE_INT, 6), "normalization",
                         "VLAs of different expressions are one");
        // Sizes of what was made, as clang has them.
        type_test_expect_number(&tally, type_size(table, nested) == 24 && type_alignment(table, nested) == 4, "normalization sizes", "const int[2][3]",
                                type_size(table, nested), 24);
        type_test_expect_number(&tally, type_size(table, aligned1) == 4 && type_alignment(table, aligned1) == 1, "normalization sizes", "int aligned to 1",
                                type_alignment(table, aligned1), 1);
        type_test_expect_number(&tally, type_size(table, type_array(table, aligned1, 3)) == 12 && type_alignment(table, type_array(table, aligned1, 3)) == 1,
                                "normalization sizes", "an array of 3 ints aligned to 1", type_size(table, type_array(table, aligned1, 3)), 12);
        const TypeId m128i_u = type_aligned(table, type_vector_size(table, BUSTER_TYPE_LLONG, 16), 0);
        type_test_expect_number(&tally, type_size(table, m128i_u) == 16 && type_alignment(table, m128i_u) == 1, "normalization sizes", "__m128i_u",
                                type_alignment(table, m128i_u), 1);
        type_test_expect(&tally, type_test_prints(table, m128i_u, 0, "__attribute__((__vector_size__(2 * sizeof(long long)))) long long", arena), "normalization",
                         "__m128i_u prints as its canonical type");
        // An aligned typedef of a struct completed later reads the struct's size once it is complete.
        const TypeId later = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 7);
        const TypeId later_aligned = type_aligned(table, later, 5);
        TypeMember member = type_test_member(1, BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_NOT_BIT_FIELD);
        type_test_expect(&tally, !type_is_complete(table, later_aligned), "normalization", "an aligned incomplete struct is complete");
        type_test_expect(&tally, type_complete_record(table, later, &member, 1, (TypeRecordAttributes){ 0 }), "normalization", "a struct can't be completed");
        type_test_expect(&tally, type_is_complete(table, later_aligned) && type_size(table, later_aligned) == 16 && type_alignment(table, later_aligned) == 32,
                         "normalization", "an aligned struct completed later");
        // An atomic type of at most 16 bytes rounds its size up to a power of two and takes it as its alignment.
        const TypeId three = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 8);
        const TypeId char3 = type_array(table, BUSTER_TYPE_CHAR, 3);
        member = type_test_member(1, char3, BUSTER_TYPE_NOT_BIT_FIELD);
        type_complete_record(table, three, &member, 1, (TypeRecordAttributes){ 0 });
        type_test_expect_number(&tally, type_size(table, type_atomic(table, three)) == 4 && type_alignment(table, type_atomic(table, three)) == 4, "normalization sizes",
                                "_Atomic(struct { char a[3]; })", type_size(table, type_atomic(table, three)), 4);
        type_test_expect_number(&tally, type_size(table, type_atomic(table, BUSTER_TYPE_COMPLEX_LONG_DOUBLE)) == 32 &&
                                    type_alignment(table, type_atomic(table, BUSTER_TYPE_COMPLEX_LONG_DOUBLE)) == 16,
                                "normalization sizes", "_Atomic(_Complex long double)", type_size(table, type_atomic(table, BUSTER_TYPE_COMPLEX_LONG_DOUBLE)), 32);
        const TypeId char16 = type_aligned(table, BUSTER_TYPE_CHAR, 4);
        type_test_expect_number(&tally, type_alignment(table, type_atomic(table, char16)) == 1, "normalization sizes", "_Atomic of a char aligned to 16",
                                type_alignment(table, type_atomic(table, char16)), 1);
        const uint64_t bit_int_sizes[][3] = {
            { 1, 1, 1 },
            { 7, 1, 1 },
            { 9, 2, 2 },
            { 17, 4, 4 },
            { 33, 8, 8 },
            { 65, 16, 8 },
            { 128, 16, 8 },
            { 129, 24, 8 },
            { 1000, 128, 8 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bit_int_sizes); i += 1)
        {
            const TypeId type = type_bit_int(table, (uint32_t)bit_int_sizes[i][0], true);
            type_test_expect_number(&tally, type_size(table, type) == bit_int_sizes[i][1] && type_alignment(table, type) == bit_int_sizes[i][2], "normalization sizes",
                                    "_BitInt's size", bit_int_sizes[i][0], type_size(table, type));
        }
        const uint64_t vector_sizes[][4] = {
            { BUSTER_TYPE_INT, 3, 16, 16 },
            { BUSTER_TYPE_INT, 5, 32, 32 },
            { BUSTER_TYPE_CHAR, 3, 4, 4 },
            { BUSTER_TYPE_DOUBLE, 100, 1024, 1024 },
            { BUSTER_TYPE_BOOL, 3, 1, 1 },
            { BUSTER_TYPE_BOOL, 100, 16, 16 },
            { BUSTER_TYPE_INT128, 3, 64, 64 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(vector_sizes); i += 1)
        {
            const TypeId type = type_vector(table, (TypeId)vector_sizes[i][0], (uint32_t)vector_sizes[i][1], true);
            type_test_expect_number(&tally, type_size(table, type) == vector_sizes[i][2] && type_alignment(table, type) == vector_sizes[i][3], "normalization sizes",
                                    "ext vector's size", vector_sizes[i][1], type_size(table, type));
        }
        type_test_table_close(&test);
    }
    type_test_report("normalization", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hash-consing

// The test's own record of each construction: a term, in the form the request had. A reference is a term and the
// qualifiers on it; a leaf is a builtin or tag type the test didn't build.
#define BUSTER_TYPE_TEST_LEAF 0

typedef struct TypeTestTerm TypeTestTerm;
struct TypeTestTerm
{
    uint8_t kind;
    uint8_t flags;
    uint32_t base;
    uint32_t base_qualifiers;
    uint64_t extent;
    uint32_t parameter_begin;
    uint32_t parameter_count;
    TypeId id;
};

typedef struct TypeTestRef TypeTestRef;
struct TypeTestRef
{
    uint32_t term;
    uint32_t qualifiers;
};

typedef struct TypeTestTerms TypeTestTerms;
struct TypeTestTerms
{
    TypeTestTerm* terms;
    uint32_t count;
    uint32_t* parameters;
    uint32_t parameter_count;
};

// What C's rules make of a reference, written again from C rather than from the table: an ALIGNED's base qualifiers
// belong to the reference, an ALIGNED of an ALIGNED is one of the innermost base with the outer alignment, an ALIGNED at
// its complete base's own alignment is its base (the one fact taken from the table), and a function's qualifiers are
// dropped. A parameter's reference (qualifiers BUSTER_TYPE_TEST_PARAMETER) loses every qualifier, hoisted ones too.
#define BUSTER_TYPE_TEST_PARAMETER UINT32_MAX

static TypeTestRef type_test_normal(const TypeTestTerms* terms, const TypeTable* table, TypeTestRef ref)
{
    const bool parameter = ref.qualifiers == BUSTER_TYPE_TEST_PARAMETER;
    TypeTestRef result = {
        .term = ref.term,
        .qualifiers = parameter ? 0 : ref.qualifiers,
    };
    const TypeTestTerm* term = &terms->terms[result.term];
    if (term->kind == BUSTER_TYPE_KIND_ALIGNED)
    {
        uint32_t base = term->base;
        uint32_t qualifiers = result.qualifiers | term->base_qualifiers;
        while (terms->terms[base].kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            qualifiers |= terms->terms[base].base_qualifiers;
            base = terms->terms[base].base;
        }
        const TypeId base_id = type_unqualified(terms->terms[base].id);
        if (type_is_complete(table, base_id) && type_alignment_log2(table, base_id) == term->extent)
        {
            result = (TypeTestRef){
                .term = base,
                .qualifiers = qualifiers,
            };
        }
    }
    if (terms->terms[result.term].kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        result.qualifiers = 0;
    }
    if (parameter)
    {
        result.qualifiers = BUSTER_TYPE_TEST_PARAMETER;
    }
    return result;
}

// The view the comparison uses: a kind, the qualifiers at this level, and the operands. ALIGNED terms over ALIGNED
// terms are followed to the innermost base, with the outer alignment.
typedef struct TypeTestView TypeTestView;
struct TypeTestView
{
    uint8_t kind;
    uint8_t flags;
    uint32_t qualifiers;
    TypeId leaf;
    uint64_t extent;
    // The base reference (an array's with the array's qualifiers carried down to it).
    TypeTestRef base;
    uint32_t parameter_begin;
    uint32_t parameter_count;
};

static TypeTestView type_test_view(const TypeTestTerms* terms, const TypeTable* table, TypeTestRef ref)
{
    const TypeTestRef normal = type_test_normal(terms, table, ref);
    const bool parameter = normal.qualifiers == BUSTER_TYPE_TEST_PARAMETER;
    const TypeTestTerm* term = &terms->terms[normal.term];
    TypeTestView result = {
        .kind = term->kind,
        .flags = term->flags,
        .qualifiers = parameter ? 0 : normal.qualifiers,
        .leaf = term->kind == BUSTER_TYPE_TEST_LEAF ? term->id : 0,
        .extent = term->extent,
        .base = {
            .term = term->base,
            .qualifiers = term->base_qualifiers,
        },
        .parameter_begin = term->parameter_begin,
        .parameter_count = term->parameter_count,
    };
    if (type_kind_is_array(term->kind))
    {
        result.base.qualifiers |= result.qualifiers;
        result.qualifiers = 0;
    }
    else if (term->kind == BUSTER_TYPE_KIND_ALIGNED)
    {
        // The base's qualifiers belong to the reference, and an ALIGNED base is looked through to its own base. Over
        // an array, the qualifiers are its elements' (C qualifies an array's elements), so they go down to the base.
        result.qualifiers |= term->base_qualifiers;
        result.base.qualifiers = 0;
        while (terms->terms[result.base.term].kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            result.qualifiers |= terms->terms[result.base.term].base_qualifiers;
            result.base.term = terms->terms[result.base.term].base;
        }
        if (type_kind_is_array(terms->terms[result.base.term].kind))
        {
            result.base.qualifiers = result.qualifiers;
            result.qualifiers = 0;
        }
    }
    else if (term->kind == BUSTER_TYPE_KIND_VECTOR || term->kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        // A vector's element loses its ALIGNED, keeping its qualifiers.
        while (terms->terms[result.base.term].kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            result.base.qualifiers |= terms->terms[result.base.term].base_qualifiers;
            result.base.term = terms->terms[result.base.term].base;
        }
    }
    else if (term->kind == BUSTER_TYPE_KIND_COMPLEX && terms->terms[term->base].kind == BUSTER_TYPE_TEST_LEAF)
    {
        result.kind = BUSTER_TYPE_TEST_LEAF;
        result.leaf = BUSTER_TYPE_COMPLEX(type_index(terms->terms[term->base].id));
    }
    if (parameter)
    {
        result.qualifiers = 0;
    }
    return result;
}

// Structural equality of two references, with an explicit stack of the pairs still to compare.
static bool type_test_same(const TypeTestTerms* terms, const TypeTable* table, TypeTestRef left, TypeTestRef right, TypeTestRef* stack, uint64_t capacity)
{
    uint64_t depth = 0;
    bool result = true;
    stack[depth] = left;
    stack[depth + 1] = right;
    depth = 2;
    while (result && depth != 0)
    {
        depth -= 2;
        const TypeTestView a = type_test_view(terms, table, stack[depth]);
        const TypeTestView b = type_test_view(terms, table, stack[depth + 1]);
        result = a.kind == b.kind && a.qualifiers == b.qualifiers && a.flags == b.flags && a.extent == b.extent;
        if (result && a.kind == BUSTER_TYPE_TEST_LEAF)
        {
            result = a.leaf == b.leaf;
        }
        else if (result && a.kind != BUSTER_TYPE_KIND_BIT_INT)
        {
            result = depth + 2 + 2ull * a.parameter_count <= capacity;
            if (result)
            {
                stack[depth] = a.base;
                stack[depth + 1] = b.base;
                depth += 2;
            }
            if (result && a.kind == BUSTER_TYPE_KIND_FUNCTION)
            {
                result = a.parameter_count == b.parameter_count;
                for (uint32_t i = 0; result && i < a.parameter_count; i += 1)
                {
                    stack[depth] = (TypeTestRef){
                        .term = terms->parameters[a.parameter_begin + i],
                        .qualifiers = BUSTER_TYPE_TEST_PARAMETER,
                    };
                    stack[depth + 1] = (TypeTestRef){
                        .term = terms->parameters[b.parameter_begin + i],
                        .qualifiers = BUSTER_TYPE_TEST_PARAMETER,
                    };
                    depth += 2;
                }
            }
        }
    }
    return result;
}

// The TypeId the table gives a reference.
static TypeId type_test_ref_id(const TypeTestTerms* terms, const TypeTable* table, TypeTestRef ref)
{
    return type_qualify(table, terms->terms[ref.term].id, ref.qualifiers);
}

// A random term the request may use as `kind`'s operand, or UINT32_MAX after a few misses.
static uint32_t type_test_pick(const TypeTestTerms* terms, const TypeTable* table, uint64_t* random, uint8_t kind)
{
    uint32_t result = UINT32_MAX;
    for (uint32_t attempt = 0; result == UINT32_MAX && attempt < 16; attempt += 1)
    {
        const uint32_t candidate = (uint32_t)type_test_below(random, terms->count);
        const TypeId id = type_strip_aligned(table, terms->terms[candidate].id);
        const uint8_t candidate_kind = type_kind(table, id);
        const bool complete = type_is_complete(table, id) && candidate_kind != BUSTER_TYPE_KIND_VOID && candidate_kind != BUSTER_TYPE_KIND_FUNCTION;
        bool fits = true;
        if (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_ATOMIC)
        {
            fits = complete && (kind != BUSTER_TYPE_KIND_ATOMIC || (!type_kind_is_array(candidate_kind) && candidate_kind != BUSTER_TYPE_KIND_ATOMIC && candidate_kind != BUSTER_TYPE_KIND_BIT_INT));
        }
        else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
        {
            fits = type_vector_element_is_valid(table, type_unqualified(id), kind == BUSTER_TYPE_KIND_EXT_VECTOR);
        }
        else if (kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            fits = !type_kind_is_array(candidate_kind) && candidate_kind != BUSTER_TYPE_KIND_FUNCTION && type_unqualified(id) != BUSTER_TYPE_HALF;
        }
        else if (kind == BUSTER_TYPE_KIND_COMPLEX)
        {
            fits = candidate_kind == BUSTER_TYPE_KIND_BIT_INT ||
                   (terms->terms[candidate].kind == BUSTER_TYPE_TEST_LEAF && type_index(id) < BUSTER_TYPE_BUILTIN_COUNT && (BUSTER_TYPE_COMPLEX_ELEMENTS >> type_index(id) & 1) != 0);
        }
        else if (kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            fits = candidate_kind != BUSTER_TYPE_KIND_FUNCTION;
        }
        if (fits)
        {
            result = candidate;
        }
    }
    return result;
}

// Builds `count` random terms through the constructors, over a small alphabet so that many are equal.
static void type_test_build_terms(TypeTable* table, TypeTestTerms* terms, uint64_t seed, uint32_t count)
{
    uint64_t random = seed;
    const TypeId leaves[] = {
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_BOOL,
        BUSTER_TYPE_CHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_LONG_DOUBLE,
        BUSTER_TYPE_HALF,
        BUSTER_TYPE_FLOAT16,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(leaves); i += 1)
    {
        terms->terms[terms->count] = (TypeTestTerm){
            .kind = BUSTER_TYPE_TEST_LEAF,
            .id = leaves[i],
        };
        terms->count += 1;
    }
    const TypeId tag = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
    TypeMember member = type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
    type_complete_record(table, tag, &member, 1, (TypeRecordAttributes){ 0 });
    terms->terms[terms->count] = (TypeTestTerm){
        .kind = BUSTER_TYPE_TEST_LEAF,
        .id = tag,
    };
    terms->count += 1;
    terms->terms[terms->count] = (TypeTestTerm){
        .kind = BUSTER_TYPE_TEST_LEAF,
        .id = type_tag(table, BUSTER_TYPE_KIND_UNION, 2),
    };
    terms->count += 1;
    const uint8_t kinds[] = {
        BUSTER_TYPE_KIND_POINTER,
        BUSTER_TYPE_KIND_POINTER,
        BUSTER_TYPE_KIND_ARRAY,
        BUSTER_TYPE_KIND_ARRAY,
        BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
        BUSTER_TYPE_KIND_VARIABLE_ARRAY,
        BUSTER_TYPE_KIND_FUNCTION,
        BUSTER_TYPE_KIND_FUNCTION,
        BUSTER_TYPE_KIND_VECTOR,
        BUSTER_TYPE_KIND_EXT_VECTOR,
        BUSTER_TYPE_KIND_ATOMIC,
        BUSTER_TYPE_KIND_ALIGNED,
        BUSTER_TYPE_KIND_BIT_INT,
        BUSTER_TYPE_KIND_COMPLEX,
    };
    while (terms->count < count)
    {
        const uint8_t kind = kinds[type_test_below(&random, BUSTER_ARRAY_LENGTH(kinds))];
        TypeTestTerm term = {
            .kind = kind,
        };
        TypeId id = 0;
        uint32_t base = kind == BUSTER_TYPE_KIND_BIT_INT ? 0 : type_test_pick(terms, table, &random, kind);
        if (base != UINT32_MAX)
        {
            term.base = base;
            const uint32_t qualifiers = (uint32_t)type_test_below(&random, 8);
            term.base_qualifiers = kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_COMPLEX || type_test_below(&random, 3) == 0 ? 0 : qualifiers;
            const TypeId base_id = type_qualify(table, terms->terms[base].id, term.base_qualifiers);
            if (kind == BUSTER_TYPE_KIND_POINTER)
            {
                id = type_pointer(table, base_id);
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY)
            {
                term.extent = type_test_below(&random, 4);
                id = type_array(table, base_id, term.extent);
            }
            else if (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                id = type_incomplete_array(table, base_id);
            }
            else if (kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY)
            {
                term.extent = type_test_below(&random, 3);
                id = type_variable_array(table, base_id, (uint32_t)term.extent);
            }
            else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
            {
                term.extent = 1 + type_test_below(&random, 4);
                id = type_vector(table, base_id, (uint32_t)term.extent, kind == BUSTER_TYPE_KIND_EXT_VECTOR);
            }
            else if (kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                id = type_atomic(table, base_id);
            }
            else if (kind == BUSTER_TYPE_KIND_ALIGNED)
            {
                term.extent = type_test_below(&random, 6);
                id = type_aligned(table, base_id, (uint32_t)term.extent);
            }
            else if (kind == BUSTER_TYPE_KIND_BIT_INT)
            {
                term.flags = (uint8_t)type_test_below(&random, 2);
                term.extent = 2 + type_test_below(&random, 6);
                term.base = 0;
                term.base_qualifiers = 0;
                id = type_bit_int(table, (uint32_t)term.extent, term.flags != 0);
            }
            else if (kind == BUSTER_TYPE_KIND_COMPLEX)
            {
                id = type_complex(table, base_id);
            }
            else
            {
                const uint32_t parameter_count = (uint32_t)type_test_below(&random, 4);
                term.flags = parameter_count != 0 || type_test_below(&random, 3) != 0 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0;
                term.flags |= term.flags != 0 && type_test_below(&random, 3) == 0 ? BUSTER_TYPE_FLAG_VARIADIC : 0;
                term.parameter_begin = terms->parameter_count;
                TypeId parameters[4];
                bool valid = true;
                for (uint32_t i = 0; valid && i < parameter_count; i += 1)
                {
                    const uint32_t parameter = type_test_pick(terms, table, &random, BUSTER_TYPE_KIND_FUNCTION);
                    const TypeId stripped = parameter == UINT32_MAX ? 0 : type_strip_aligned(table, terms->terms[parameter].id);
                    valid = parameter != UINT32_MAX && type_kind(table, stripped) != BUSTER_TYPE_KIND_VOID;
                    if (valid)
                    {
                        parameters[i] = terms->terms[parameter].id | (TypeId)type_test_below(&random, 2);
                        terms->parameters[terms->parameter_count + i] = parameter;
                    }
                }
                term.parameter_count = parameter_count;
                id = valid ? type_function(table, base_id, parameters, parameter_count, term.flags) : 0;
                if (id != 0)
                {
                    terms->parameter_count += parameter_count;
                }
            }
        }
        if (id != 0)
        {
            // An array's or ALIGNED's TypeId carries its base's hoisted qualifiers: the term's reference without
            // qualifiers is that TypeId as it came.
            term.id = id;
            terms->terms[terms->count] = term;
            terms->count += 1;
        }
        else
        {
            table->last_error = TYPE_ERROR_NONE;
        }
    }
}

static bool type_test_hash_consing(uint64_t seed, uint32_t count)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    TypeTestTerms terms = { 0 };
    Scratch scratch = scratch_begin(0);
    terms.terms = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeTestTerm, count);
    terms.parameters = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, 4ull * count);
    const uint64_t stack_capacity = 1 << 16;
    TypeTestRef* stack = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeTestRef, stack_capacity);
    if (type_test_expect(&tally, terms.terms != 0 && terms.parameters != 0 && stack != 0 && type_test_table_open(&test, count * 2), "hash-consing", "no memory"))
    {
        TypeTable* table = &test.table;
        type_test_build_terms(table, &terms, seed, count);
        uint64_t equal = 0;
        for (uint32_t i = 0; i < terms.count; i += 1)
        {
            for (uint32_t j = i; j < terms.count; j += 1)
            {
                const TypeTestRef a = {
                    .term = i,
                    .qualifiers = 0,
                };
                const TypeTestRef b = {
                    .term = j,
                    .qualifiers = 0,
                };
                const bool same = type_test_same(&terms, table, a, b, stack, stack_capacity);
                const bool same_id = type_test_ref_id(&terms, table, a) == type_test_ref_id(&terms, table, b);
                equal += same ? 1 : 0;
                if (same != same_id)
                {
                    char detail[160];
                    snprintf(detail, sizeof(detail), "terms %u and %u (kinds %u and %u, ids %u and %u): structurally %s", i, j, terms.terms[i].kind, terms.terms[j].kind,
                             terms.terms[i].id, terms.terms[j].id, same ? "equal" : "different");
                    type_test_expect(&tally, false, "hash-consing", detail);
                }
                else
                {
                    tally.checks += 1;
                }
            }
        }
        // Qualified references: equal structures under qualifiers get equal TypeIds too.
        uint64_t random = seed ^ 0x51ull;
        for (uint32_t k = 0; k < 4 * count; k += 1)
        {
            const TypeTestRef a = {
                .term = (uint32_t)type_test_below(&random, terms.count),
                .qualifiers = (uint32_t)type_test_below(&random, 8),
            };
            const TypeTestRef b = {
                .term = (uint32_t)type_test_below(&random, terms.count),
                .qualifiers = type_test_below(&random, 2) == 0 ? a.qualifiers : (uint32_t)type_test_below(&random, 8),
            };
            const bool same = type_test_same(&terms, table, a, b, stack, stack_capacity);
            type_test_expect(&tally, same == (type_test_ref_id(&terms, table, a) == type_test_ref_id(&terms, table, b)), "hash-consing", "qualified references");
        }
        // Asking again gives the same TypeId, and no new type.
        const uint32_t types = table->types.count;
        for (uint32_t i = 0; i < terms.count; i += 1)
        {
            const TypeTestTerm* term = &terms.terms[i];
            const TypeId base = type_qualify(table, terms.terms[term->base].id, term->base_qualifiers);
            TypeId again = term->id;
            if (term->kind == BUSTER_TYPE_KIND_POINTER)
            {
                again = type_pointer(table, base);
            }
            else if (term->kind == BUSTER_TYPE_KIND_ARRAY)
            {
                again = type_array(table, base, term->extent);
            }
            else if (term->kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                again = type_atomic(table, base);
            }
            type_test_expect(&tally, again == term->id, "hash-consing", "asking again gave another type");
        }
        type_test_expect(&tally, table->types.count == types, "hash-consing", "asking again made types");
        printf("%-34s %10u terms, %" PRIu64 " equal pairs\n", "hash-consing terms", terms.count, equal);
        type_test_table_close(&test);
    }
    scratch_end(scratch);
    type_test_report("hash-consing", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Batches

// The requests' kinds: every kind the batch hashes, and some it must refuse as type_derive does.
static const uint8_t type_test_batch_kinds[] = {
    BUSTER_TYPE_KIND_POINTER,
    BUSTER_TYPE_KIND_POINTER,
    BUSTER_TYPE_KIND_POINTER,
    BUSTER_TYPE_KIND_ARRAY,
    BUSTER_TYPE_KIND_ARRAY,
    BUSTER_TYPE_KIND_INCOMPLETE_ARRAY,
    BUSTER_TYPE_KIND_VARIABLE_ARRAY,
    BUSTER_TYPE_KIND_VECTOR,
    BUSTER_TYPE_KIND_EXT_VECTOR,
    BUSTER_TYPE_KIND_ATOMIC,
    BUSTER_TYPE_KIND_ALIGNED,
    BUSTER_TYPE_KIND_BIT_INT,
    BUSTER_TYPE_KIND_COMPLEX,
    BUSTER_TYPE_KIND_FUNCTION,
    BUSTER_TYPE_KIND_STRUCT,
    BUSTER_TYPE_KIND_INTEGER,
    77,
    200,
};

// Whether two tables hold the same types, column by column, and the same index.
static bool type_test_same_tables(const TypeTable* a, const TypeTable* b)
{
    const TypeColumns* x = &a->types;
    const TypeColumns* y = &b->types;
    bool result = x->count == y->count && a->parameters.count == b->parameters.count && a->index.bucket_count == b->index.bucket_count &&
                  a->index.count == b->index.count;
    if (result)
    {
        const uint64_t n = x->count;
        result = memcmp(x->kind, y->kind, n) == 0 && memcmp(x->code, y->code, n) == 0 && memcmp(x->flags, y->flags, n) == 0 &&
                 memcmp(x->alignment_log2, y->alignment_log2, n) == 0 && memcmp(x->base, y->base, n * sizeof(TypeId)) == 0 &&
                 memcmp(x->index, y->index, n * sizeof(uint32_t)) == 0 && memcmp(x->extent, y->extent, n * sizeof(uint64_t)) == 0 &&
                 memcmp(x->size, y->size, n * sizeof(uint64_t)) == 0 && memcmp(x->hash, y->hash, n * sizeof(uint64_t)) == 0;
        result = result && memcmp(a->parameters.type, b->parameters.type, a->parameters.count * sizeof(TypeId)) == 0;
        result = result && memcmp(a->index.slots, b->index.slots, (uint64_t)a->index.bucket_count * BUSTER_TYPE_BUCKET_BYTES) == 0;
    }
    return result;
}

static bool type_test_batches(uint64_t seed, uint32_t count)
{
    TypeTestTally tally = { 0 };
    TypeTestTable one;
    TypeTestTable many;
    Scratch scratch = scratch_begin(0);
    uint8_t* kinds = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, count);
    uint8_t* flags = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint8_t, count);
    TypeId* bases = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count);
    uint64_t* extents = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, count);
    TypeId* expected = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count);
    TypeId* results = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, count);
    const bool memory = kinds != 0 && flags != 0 && bases != 0 && extents != 0 && expected != 0 && results != 0;
    if (type_test_expect(&tally, memory && type_test_table_open(&one, count + 64) && type_test_table_open(&many, count + 64), "batches", "no memory"))
    {
        uint64_t random = seed;
        uint32_t made = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            kinds[i] = type_test_batch_kinds[type_test_below(&random, BUSTER_ARRAY_LENGTH(type_test_batch_kinds))];
            flags[i] = type_test_below(&random, 16) == 0 ? (uint8_t)type_test_below(&random, 256) : 0;
            if (kinds[i] == BUSTER_TYPE_KIND_BIT_INT)
            {
                flags[i] = (uint8_t)type_test_below(&random, 2);
            }
            // Bases: earlier results most often (so that requests repeat), builtins, and now and then none or junk.
            const uint64_t pick = type_test_below(&random, 20);
            TypeId base = 0;
            if (pick < 12 && made != 0)
            {
                base = expected[type_test_below(&random, i)];
            }
            else if (pick < 18)
            {
                base = BUSTER_TYPE_BUILTIN(1 + type_test_below(&random, BUSTER_TYPE_CODE_NULLPTR));
            }
            else if (pick == 18)
            {
                base = (TypeId)type_test_random(&random);
            }
            base |= (TypeId)(type_test_below(&random, 3) == 0 ? type_test_below(&random, 8) : 0);
            bases[i] = kinds[i] == BUSTER_TYPE_KIND_BIT_INT && pick != 19 ? 0 : base;
            extents[i] = type_test_below(&random, 5);
            if (kinds[i] == BUSTER_TYPE_KIND_ARRAY && type_test_below(&random, 50) == 0)
            {
                extents[i] = (1ull << 32) + type_test_below(&random, 3);
            }
            else if (kinds[i] == BUSTER_TYPE_KIND_BIT_INT)
            {
                extents[i] = type_test_below(&random, 140);
            }
            else if (kinds[i] == BUSTER_TYPE_KIND_VECTOR || kinds[i] == BUSTER_TYPE_KIND_EXT_VECTOR)
            {
                extents[i] = type_test_below(&random, 6);
            }
            expected[i] = type_derive(&one.table, kinds[i], flags[i], bases[i], extents[i]);
            made += expected[i] != 0 ? 1 : 0;
        }
        const TypeKeys keys = {
            .kinds = kinds,
            .flags = flags,
            .bases = bases,
            .extents = extents,
        };
        for (uint32_t first = 0; first < count;)
        {
            const uint32_t slice = (uint32_t)(1 + type_test_below(&random, 40));
            const uint32_t length = count - first < slice ? count - first : slice;
            const TypeKeys part = {
                .kinds = keys.kinds + first,
                .flags = keys.flags + first,
                .bases = keys.bases + first,
                .extents = keys.extents + first,
            };
            type_derive_batch(&many.table, part, length, results + first);
            first += length;
        }
        uint32_t different = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (expected[i] != results[i] && different < BUSTER_TYPE_TEST_REPORTED)
            {
                fprintf(stderr, "test-types: batches: request %u (kind %u, flags %u, base %u, extent %" PRIu64 ") got %u, one at a time %u\n", i, kinds[i], flags[i],
                        bases[i], extents[i], results[i], expected[i]);
            }
            different += expected[i] != results[i] ? 1 : 0;
            tally.checks += 1;
        }
        tally.failures += different;
        type_test_expect(&tally, type_test_same_tables(&one.table, &many.table), "batches", "the tables differ");
        type_test_expect(&tally, one.table.index.bucket_count > BUSTER_TYPE_INITIAL_BUCKETS, "batches", "the index never grew");
        // After growing, every type is found again.
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            if (expected[i] != 0)
            {
                const TypeId again = type_derive(&one.table, kinds[i], flags[i], bases[i], extents[i]);
                found += again == expected[i] ? 1 : 0;
                type_test_expect(&tally, again == expected[i], "batches", "a type wasn't found again after the index grew");
            }
        }
        printf("%-34s %10u requests, %u types, %u buckets, %u found again\n", "batches", count, one.table.types.count - BUSTER_TYPE_BUILTIN_COUNT,
               one.table.index.bucket_count, found);
        type_test_table_close(&one);
        type_test_table_close(&many);
    }
    scratch_end(scratch);
    type_test_report("batches", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

// A fixture: a record's members and the offsets, size and alignment clang 23 gives it (each measured with offsetof, or
// for a bit-field by setting it in a zeroed object and finding its first bit).
#define BUSTER_TYPE_TEST_FIXTURE_MEMBERS 4

typedef struct TypeTestLayoutFixture TypeTestLayoutFixture;
struct TypeTestLayoutFixture
{
    const char* name;
    uint8_t kind;
    TypeRecordAttributes attributes;
    uint32_t count;
    TypeMember members[BUSTER_TYPE_TEST_FIXTURE_MEMBERS];
    uint64_t offsets[BUSTER_TYPE_TEST_FIXTURE_MEMBERS];
    uint64_t size;
    uint64_t alignment;
};

// Member types the fixtures name beyond the builtins, resolved when the table exists.
#define BUSTER_TYPE_TEST_INT_ARRAY0 1u
#define BUSTER_TYPE_TEST_DOUBLE_OPEN 2u
#define BUSTER_TYPE_TEST_ALIGNED16 3u

#define BUSTER_TYPE_TEST_PLAIN(name_, type_) { .name = (name_), .type = (type_), .bit_width = BUSTER_TYPE_NOT_BIT_FIELD }
#define BUSTER_TYPE_TEST_BITS(name_, type_, width_) { .name = (name_), .type = (type_), .bit_width = (width_) }

static const TypeTestLayoutFixture type_test_layout_fixtures[] = {
    { "packed { char a : 7; char b : 7; char c; }", BUSTER_TYPE_KIND_STRUCT, { .packed = true }, 3,
      { BUSTER_TYPE_TEST_BITS(1, BUSTER_TYPE_CHAR, 7), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_CHAR, 7), BUSTER_TYPE_TEST_PLAIN(3, BUSTER_TYPE_CHAR) }, { 0, 7, 16 }, 3, 1 },
    { "{ char a; int : 0; char b; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(0, BUSTER_TYPE_INT, 0), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_CHAR) }, { 0, 32, 32 }, 5, 1 },
    { "{ char c; long : 0; char d; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(0, BUSTER_TYPE_LONG, 0), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_CHAR) }, { 0, 64, 64 }, 9, 1 },
    { "union { char c; int : 20; char : 0; }", BUSTER_TYPE_KIND_UNION, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(0, BUSTER_TYPE_INT, 20), BUSTER_TYPE_TEST_BITS(0, BUSTER_TYPE_CHAR, 0) }, { 0, 0, 0 }, 3, 1 },
    { "{ char c; int a[0]; char d; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_TEST_INT_ARRAY0), BUSTER_TYPE_TEST_PLAIN(3, BUSTER_TYPE_CHAR) }, { 0, 32, 32 }, 8, 4 },
    { "{ int a : 3; int b : 30; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2, { BUSTER_TYPE_TEST_BITS(1, BUSTER_TYPE_INT, 3), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_INT, 30) },
      { 0, 32 }, 8, 4 },
    { "{ char a; int b : 4; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2, { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_INT, 4) },
      { 0, 8 }, 4, 4 },
    { "packed { char a; int b; }", BUSTER_TYPE_KIND_STRUCT, { .packed = true }, 2, { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_INT) },
      { 0, 8 }, 5, 1 },
    { "pack(2) { char a; int b; }", BUSTER_TYPE_KIND_STRUCT, { .pack_log2 = 2 }, 2, { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_INT) },
      { 0, 16 }, 6, 2 },
    { "{ char a; int b aligned(8); }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), { .name = 2, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD, .alignment_log2 = 4 } }, { 0, 64 }, 16, 8 },
    { "{ int n; double d[]; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2, { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_INT), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_TEST_DOUBLE_OPEN) },
      { 0, 64 }, 8, 8 },
    { "pack(1) { char a; int b : 4; int c : 30; }", BUSTER_TYPE_KIND_STRUCT, { .pack_log2 = 1 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_INT, 4), BUSTER_TYPE_TEST_BITS(3, BUSTER_TYPE_INT, 30) }, { 0, 8, 12 }, 6, 1 },
    { "{ char a; int b : 4 aligned(4); }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), { .name = 2, .type = BUSTER_TYPE_INT, .bit_width = 4, .alignment_log2 = 3 } }, { 0, 32 }, 8, 4 },
    { "{ char c; struct aligned(16) { char c; } s; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_TEST_ALIGNED16) }, { 0, 128 }, 32, 16 },
    { "packed { char c; struct aligned(16) { char c; } s; }", BUSTER_TYPE_KIND_STRUCT, { .packed = true }, 2,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_TEST_ALIGNED16) }, { 0, 8 }, 17, 1 },
    { "union { char c; double d; int b : 5; }", BUSTER_TYPE_KIND_UNION, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_DOUBLE), BUSTER_TYPE_TEST_BITS(3, BUSTER_TYPE_INT, 5) }, { 0, 0, 0 }, 8, 8 },
    { "{ bool a : 1; bool b : 1; char c; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 3,
      { BUSTER_TYPE_TEST_BITS(1, BUSTER_TYPE_BOOL, 1), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_BOOL, 1), BUSTER_TYPE_TEST_PLAIN(3, BUSTER_TYPE_CHAR) }, { 0, 1, 8 }, 2, 1 },
    { "{ long long a : 40; long long b : 30; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2,
      { BUSTER_TYPE_TEST_BITS(1, BUSTER_TYPE_LLONG, 40), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_LLONG, 30) }, { 0, 64 }, 16, 8 },
    { "{ char a; __int128 b : 2; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 2, { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(2, BUSTER_TYPE_INT128, 2) },
      { 0, 8 }, 16, 16 },
    { "{ char c; int : 3; char d; }", BUSTER_TYPE_KIND_STRUCT, { 0 }, 3,
      { BUSTER_TYPE_TEST_PLAIN(1, BUSTER_TYPE_CHAR), BUSTER_TYPE_TEST_BITS(0, BUSTER_TYPE_INT, 3), BUSTER_TYPE_TEST_PLAIN(2, BUSTER_TYPE_CHAR) }, { 0, 8, 16 }, 3, 1 },
};

static bool type_test_layout_fixtures_check(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 1000), "layout fixtures", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId aligned16 = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 9);
        TypeMember member = type_test_member(1, BUSTER_TYPE_CHAR, BUSTER_TYPE_NOT_BIT_FIELD);
        const TypeRecordAttributes aligned_attribute = {
            .alignment_log2 = 5,
        };
        type_complete_record(table, aligned16, &member, 1, aligned_attribute);
        const TypeId resolved[] = {
            0,
            type_array(table, BUSTER_TYPE_INT, 0),
            type_incomplete_array(table, BUSTER_TYPE_DOUBLE),
            aligned16,
        };
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            // Each fixture by the scalar layout, then all of them by type_layout_records.
            TypeId records[BUSTER_ARRAY_LENGTH(type_test_layout_fixtures)];
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(type_test_layout_fixtures); i += 1)
            {
                const TypeTestLayoutFixture* fixture = &type_test_layout_fixtures[i];
                TypeMember members[BUSTER_TYPE_TEST_FIXTURE_MEMBERS];
                for (uint32_t m = 0; m < fixture->count; m += 1)
                {
                    members[m] = fixture->members[m];
                    members[m].type = members[m].type < BUSTER_ARRAY_LENGTH(resolved) ? resolved[members[m].type] : members[m].type;
                }
                records[i] = type_tag(table, fixture->kind, 1);
                type_test_expect(&tally, type_record_define(table, records[i], members, fixture->count, fixture->attributes), "layout fixtures", fixture->name);
                if (pass == 0)
                {
                    type_test_expect(&tally, type_record_layout(table, records[i]), "layout fixtures", fixture->name);
                }
            }
            if (pass == 1)
            {
                type_test_expect(&tally, type_layout_records(table, records, BUSTER_ARRAY_LENGTH(type_test_layout_fixtures)), "layout fixtures", "the batch");
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(type_test_layout_fixtures); i += 1)
            {
                const TypeTestLayoutFixture* fixture = &type_test_layout_fixtures[i];
                char what[160];
                snprintf(what, sizeof(what), "%s (%s)", fixture->name, pass == 0 ? "scalar" : "vector");
                type_test_expect_number(&tally, type_size(table, records[i]) == fixture->size, "layout fixtures size", what, type_size(table, records[i]), fixture->size);
                type_test_expect_number(&tally, type_alignment(table, records[i]) == fixture->alignment, "layout fixtures alignment", what, type_alignment(table, records[i]),
                                        fixture->alignment);
                for (uint32_t m = 0; m < fixture->count; m += 1)
                {
                    type_test_expect_number(&tally, type_test_offset(table, records[i], m) == fixture->offsets[m], "layout fixtures offset", what,
                                            type_test_offset(table, records[i], m), fixture->offsets[m]);
                }
            }
        }
        // __builtin_va_list: struct __va_list_tag[1], 24 bytes aligned to 8.
        const uint32_t names[5] = {
            1,
            2,
            3,
            4,
            5,
        };
        const TypeId va_list = type_builtin_va_list(table, names);
        type_test_expect(&tally, va_list != 0 && type_size(table, va_list) == 24 && type_alignment(table, va_list) == 8, "layout fixtures", "__builtin_va_list");
        uint64_t offset = 0;
        const TypeId tag = type_element(table, va_list);
        type_test_expect(&tally, type_find_member(table, tag, 5, &offset) != 0 && offset == 128, "layout fixtures", "reg_save_area's offset");
        // Lookup through anonymous members: struct { int a; union { char b; struct { short c; long d; }; }; }.
        const TypeId inner = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 0);
        const TypeMember inner_members[2] = {
            type_test_member(3, BUSTER_TYPE_SHORT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(4, BUSTER_TYPE_LONG, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_complete_record(table, inner, inner_members, 2, (TypeRecordAttributes){ 0 });
        const TypeId middle = type_tag(table, BUSTER_TYPE_KIND_UNION, 0);
        const TypeMember middle_members[2] = {
            type_test_member(2, BUSTER_TYPE_CHAR, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(0, inner, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_complete_record(table, middle, middle_members, 2, (TypeRecordAttributes){ 0 });
        const TypeId outer = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 6);
        const TypeMember outer_members[2] = {
            type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(0, middle, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_complete_record(table, outer, outer_members, 2, (TypeRecordAttributes){ 0 });
        type_test_expect(&tally, type_find_member(table, outer, 4, &offset) != 0 && offset == 128, "layout fixtures", "d through two anonymous members");
        type_test_expect(&tally, type_find_member(table, outer, 2, &offset) != 0 && offset == 64, "layout fixtures", "b through an anonymous union");
        type_test_expect(&tally, type_find_member(table, outer, 9, &offset) == 0, "layout fixtures", "a member that doesn't exist");
        type_test_table_close(&test);
    }
    type_test_report("layout fixtures", tally);
    return tally.failures == 0;
}

// The member types the random records draw from: every builtin object type, pointers, arrays (of length 0 too),
// vectors, ALIGNED types that raise and lower alignment, atomics, _BitInts, complex types and enums of every
// underlying type, and records of the earlier levels.
typedef struct TypeTestPool TypeTestPool;
struct TypeTestPool
{
    TypeId* types;
    uint32_t count;
    uint32_t capacity;
};

static void type_test_pool_add(TypeTestPool* pool, TypeId type)
{
    if (type != 0 && pool->count < pool->capacity)
    {
        pool->types[pool->count] = type;
        pool->count += 1;
    }
}

static void type_test_fill_pool(TypeTable* table, TypeTestPool* pool, uint64_t* random)
{
    for (uint32_t code = BUSTER_TYPE_CODE_BOOL; code < BUSTER_TYPE_BUILTIN_COUNT; code += 1)
    {
        if (type_is_valid(table, BUSTER_TYPE_BUILTIN(code)))
        {
            type_test_pool_add(pool, BUSTER_TYPE_BUILTIN(code));
        }
    }
    const uint32_t widths[] = {
        1,
        2,
        7,
        8,
        9,
        15,
        16,
        17,
        31,
        32,
        33,
        63,
        64,
        65,
        127,
        128,
        129,
        200,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        type_test_pool_add(pool, type_bit_int(table, widths[i], true));
        type_test_pool_add(pool, widths[i] > 1 ? type_bit_int(table, widths[i], false) : 0);
    }
    const TypeId underlying[] = {
        BUSTER_TYPE_BOOL,
        BUSTER_TYPE_SCHAR,
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_USHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_UINT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_ULONG,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(underlying); i += 1)
    {
        const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
        const uint8_t alignment = type_test_below(random, 4) == 0 ? (uint8_t)(1 + type_test_below(random, 5)) : 0;
        type_enum_complete(table, enumeration, underlying[i], alignment);
        type_test_pool_add(pool, enumeration);
    }
    const uint32_t base_count = pool->count;
    for (uint32_t i = 0; i < 80; i += 1)
    {
        const TypeId base = pool->types[type_test_below(random, base_count)];
        const uint64_t pick = type_test_below(random, 8);
        if (pick == 0)
        {
            type_test_pool_add(pool, type_pointer(table, base));
        }
        else if (pick == 1)
        {
            type_test_pool_add(pool, type_array(table, base, type_test_below(random, 4)));
        }
        else if (pick == 2)
        {
            type_test_pool_add(pool, type_vector(table, base, 1u << type_test_below(random, 4), false));
        }
        else if (pick == 3)
        {
            type_test_pool_add(pool, type_vector(table, base, 1 + (uint32_t)type_test_below(random, 5), true));
        }
        else if (pick == 4)
        {
            type_test_pool_add(pool, type_aligned(table, base, (uint32_t)type_test_below(random, 7)));
        }
        else if (pick == 5)
        {
            type_test_pool_add(pool, type_atomic(table, base));
        }
        else if (pick == 6)
        {
            type_test_pool_add(pool, type_complex(table, base));
        }
        else
        {
            type_test_pool_add(pool, base | (TypeId)(1 + type_test_below(random, 2)));
        }
        table->last_error = TYPE_ERROR_NONE;
    }
}

// Whether a type can be a bit-field's.
static bool type_test_bit_field_type(const TypeTable* table, TypeId type)
{
    return type_integer_width(table, type) != 0;
}

// Random members for a record: count of them, each a pool type, a bit-field of a random width (0 unnamed) where its
// type allows one, packed or aligned now and then, the last a flexible array member now and then.
static uint32_t type_test_random_members(TypeTable* table, const TypeTestPool* pool, uint64_t* random, TypeMember* members, uint32_t count, bool flexible, uint32_t* name)
{
    for (uint32_t m = 0; m < count; m += 1)
    {
        TypeId type = pool->types[type_test_below(random, pool->count)];
        uint32_t width = BUSTER_TYPE_NOT_BIT_FIELD;
        if (type_test_bit_field_type(table, type) && type_test_below(random, 100) < 45)
        {
            const uint64_t limit = type_integer_width(table, type);
            width = (uint32_t)type_test_below(random, limit + 1);
            type = type_unqualified(type) | (type_qualifiers(type) & BUSTER_TYPE_VOLATILE);
        }
        const bool unnamed = width == 0 || (width != BUSTER_TYPE_NOT_BIT_FIELD && type_test_below(random, 8) == 0);
        *name += 1;
        members[m] = (TypeMember){
            .name = unnamed ? 0 : *name,
            .type = type,
            .bit_width = width,
            .alignment_log2 = type_test_below(random, 12) == 0 ? (uint8_t)(1 + type_test_below(random, 7)) : 0,
            .packed = type_test_below(random, 10) == 0,
        };
    }
    if (flexible && count != 0)
    {
        TypeId element = 0;
        while (element == 0 || !type_is_complete(table, element))
        {
            element = pool->types[type_test_below(random, pool->count)];
        }
        const TypeId open = type_incomplete_array(table, element);
        if (open != 0)
        {
            members[count - 1].type = open;
            members[count - 1].bit_width = BUSTER_TYPE_NOT_BIT_FIELD;
            members[count - 1].name = *name;
        }
        table->last_error = TYPE_ERROR_NONE;
    }
    return count;
}

// Random records, level by level (each level's members may be records of the levels before), laid out one by one
// with the scalar layout or a level at a time with type_layout_records. The same seed makes the same records either
// way. records gets every record, in order; returns how many.
#define BUSTER_TYPE_TEST_MEMBERS 128

static uint32_t type_test_build_records(TypeTable* table, uint64_t seed, uint32_t per_level, uint32_t levels, bool vector, TypeId* records, TypeTestPool* pool)
{
    uint64_t random = seed;
    uint32_t count = 0;
    uint32_t name = 0;
    type_test_fill_pool(table, pool, &random);
    const TypeId huge = type_array(table, BUSTER_TYPE_CHAR, 1ull << 29);
    // Just under the vector layout's 2^30 bits per member: two of them come within 16 bits of 2^31.
    const TypeId near = type_array(table, BUSTER_TYPE_CHAR, (1ull << 27) - 1);
    // An alignment of 2^28 bytes, past the vector layout's.
    const TypeId over_aligned = type_aligned(table, BUSTER_TYPE_INT, 28);
    for (uint32_t level = 0; level < levels; level += 1)
    {
        const uint32_t first = count;
        for (uint32_t r = 0; r < per_level; r += 1)
        {
            const bool is_union = type_test_below(&random, 4) == 0;
            const TypeId record = type_tag(table, is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, 1);
            // Mostly small records; one in five of 15 to 127 members, around and past the vector layout's blocks of 16.
            uint32_t member_count = (uint32_t)type_test_below(&random, 15);
            if (type_test_below(&random, 5) == 0)
            {
                member_count = 15 + (uint32_t)type_test_below(&random, BUSTER_TYPE_TEST_MEMBERS - 15);
            }
            TypeMember members[BUSTER_TYPE_TEST_MEMBERS];
            type_test_random_members(table, pool, &random, members, member_count, !is_union && type_test_below(&random, 10) == 0, &name);
            if (member_count != 0 && type_test_below(&random, 60) == 0)
            {
                // Past the vector layout's 2^31 bits: the batch must give it to the scalar layout.
                members[0] = type_test_member(1, huge, BUSTER_TYPE_NOT_BIT_FIELD);
            }
            if (member_count >= 2 && type_test_below(&random, 40) == 0)
            {
                // Within 16 bits of 2^31 after two members, which the vector layout takes, and past it with any more
                // that have a size, in a step of their own.
                const uint32_t at = (uint32_t)type_test_below(&random, member_count - 1);
                name += 2;
                members[at] = type_test_member(name - 1, near, BUSTER_TYPE_NOT_BIT_FIELD);
                members[at + 1] = type_test_member(name, near, BUSTER_TYPE_NOT_BIT_FIELD);
            }
            const uint64_t aligned_past = member_count != 0 && type_test_below(&random, 80) == 0 ? 1 + type_test_below(&random, 3) : 0;
            if (aligned_past == 1)
            {
                name += 1;
                members[type_test_below(&random, member_count)] = type_test_member(name, over_aligned, BUSTER_TYPE_NOT_BIT_FIELD);
            }
            if (aligned_past == 2)
            {
                members[type_test_below(&random, member_count)].alignment_log2 = 29;
            }
            const TypeRecordAttributes attributes = {
                .packed = type_test_below(&random, 7) == 0,
                .pack_log2 = type_test_below(&random, 7) == 0 ? (uint8_t)(1 + type_test_below(&random, 5)) : 0,
                .alignment_log2 = aligned_past == 3 ? 30 : type_test_below(&random, 10) == 0 ? (uint8_t)(1 + type_test_below(&random, 7)) : 0,
            };
            if (type_record_define(table, record, members, member_count, attributes))
            {
                records[count] = record;
                count += 1;
                if (!vector)
                {
                    type_record_layout(table, record);
                }
            }
            table->last_error = TYPE_ERROR_NONE;
        }
        if (vector)
        {
            type_layout_records(table, records + first, count - first);
        }
        for (uint32_t r = first; r < count; r += 1)
        {
            if (type_is_complete(table, records[r]) && (table->types.flags[type_index(records[r])] & BUSTER_TYPE_FLAG_FLEXIBLE) == 0 &&
                type_size(table, records[r]) < (1u << 24))
            {
                type_test_pool_add(pool, records[r]);
            }
        }
    }
    return count;
}

static bool type_test_layout_twins(uint64_t seed, uint32_t per_level)
{
    TypeTestTally tally = { 0 };
    TypeTestTable scalar;
    TypeTestTable vector;
    Scratch scratch = scratch_begin(0);
    const uint32_t levels = 4;
    TypeId* scalar_records = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, per_level * levels);
    TypeId* vector_records = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, per_level * levels);
    TypeTestPool scalar_pool = {
        .types = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, 4096),
        .capacity = 4096,
    };
    TypeTestPool vector_pool = {
        .types = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, 4096),
        .capacity = 4096,
    };
    const bool memory = scalar_records != 0 && vector_records != 0 && scalar_pool.types != 0 && vector_pool.types != 0;
    if (type_test_expect(&tally, memory && type_test_table_open(&scalar, per_level * levels * 8) && type_test_table_open(&vector, per_level * levels * 8), "layout twins",
                         "no memory"))
    {
        const uint32_t count = type_test_build_records(&scalar.table, seed, per_level, levels, false, scalar_records, &scalar_pool);
        const uint32_t vector_count = type_test_build_records(&vector.table, seed, per_level, levels, true, vector_records, &vector_pool);
        type_test_expect(&tally, count == vector_count && memcmp(scalar_records, vector_records, count * sizeof(TypeId)) == 0, "layout twins", "different records");
        uint32_t members = 0;
        uint32_t bit_fields = 0;
        uint32_t large = 0;
        for (uint32_t r = 0; r < count && r < vector_count; r += 1)
        {
            const TypeId record = scalar_records[r];
            const uint32_t index = type_index(record);
            const uint32_t tag = scalar.table.types.index[index];
            char what[96];
            snprintf(what, sizeof(what), "record %u (%u members)", r, scalar.table.tags.member_count[tag]);
            type_test_expect_number(&tally, scalar.table.types.size[index] == vector.table.types.size[index], "layout twins size", what, scalar.table.types.size[index],
                                    vector.table.types.size[index]);
            type_test_expect_number(&tally, scalar.table.types.alignment_log2[index] == vector.table.types.alignment_log2[index], "layout twins alignment", what,
                                    scalar.table.types.alignment_log2[index], vector.table.types.alignment_log2[index]);
            type_test_expect_number(&tally, scalar.table.tags.completion[tag] == vector.table.tags.completion[tag], "layout twins completion", what,
                                    scalar.table.tags.completion[tag], vector.table.tags.completion[tag]);
            large += scalar.table.types.size[index] * 8 >= (1ull << 31) ? 1 : 0;
            for (uint32_t m = 0; m < scalar.table.tags.member_count[tag]; m += 1)
            {
                const uint32_t member = scalar.table.tags.member_begin[tag] + m;
                type_test_expect_number(&tally, scalar.table.members.offset[member] == vector.table.members.offset[member], "layout twins offset", what,
                                        scalar.table.members.offset[member], vector.table.members.offset[member]);
                members += 1;
                bit_fields += (scalar.table.members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 ? 1 : 0;
            }
        }
        printf("%-34s %10u records, %u members, %u bit-fields, %u past the vector layout's limit\n", "layout twins", count, members, bit_fields, large);
        type_test_table_close(&scalar);
        type_test_table_close(&vector);
    }
    scratch_end(scratch);
    type_test_report("layout twins", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Conversions

// Both kernels and the scalar twin on `count` pairs of codes, which may end at a PROT_NONE page.
static bool type_test_kernels_agree(const uint8_t* left, const uint8_t* right, uint64_t count, uint8_t* scalar, uint8_t* vector, uint8_t* pairs)
{
    type_usual_arithmetic_codes_scalar(left, right, count, scalar);
    type_usual_arithmetic_codes(left, right, count, vector);
    type_usual_arithmetic_codes_pairs(left, right, count, pairs);
    return count == 0 || (memcmp(scalar, vector, count) == 0 && memcmp(scalar, pairs, count) == 0);
}

static bool type_test_conversion_kernels(uint64_t seed)
{
    TypeTestTally tally = { 0 };
    uint8_t left[BUSTER_TYPE_CODE_COUNT * BUSTER_TYPE_CODE_COUNT];
    uint8_t right[BUSTER_TYPE_CODE_COUNT * BUSTER_TYPE_CODE_COUNT];
    uint8_t scalar[BUSTER_TYPE_CODE_COUNT * BUSTER_TYPE_CODE_COUNT];
    uint8_t vector[BUSTER_TYPE_CODE_COUNT * BUSTER_TYPE_CODE_COUNT];
    uint8_t pairs[BUSTER_TYPE_CODE_COUNT * BUSTER_TYPE_CODE_COUNT];
    for (uint32_t a = 0; a < BUSTER_TYPE_CODE_COUNT; a += 1)
    {
        for (uint32_t b = 0; b < BUSTER_TYPE_CODE_COUNT; b += 1)
        {
            left[a * BUSTER_TYPE_CODE_COUNT + b] = (uint8_t)a;
            right[a * BUSTER_TYPE_CODE_COUNT + b] = (uint8_t)b;
        }
    }
    type_test_kernels_agree(left, right, sizeof(left), scalar, vector, pairs);
    for (uint32_t i = 0; i < sizeof(left); i += 1)
    {
        char detail[96];
        snprintf(detail, sizeof(detail), "codes %u and %u: scalar %u, vpermb %u, vpermi2b %u", left[i], right[i], scalar[i], vector[i], pairs[i]);
        type_test_expect(&tally, scalar[i] == vector[i] && scalar[i] == pairs[i], "conversion kernels", detail);
    }
    // The integer table against the scalar twin's integer conversion, entry by entry.
    for (uint32_t a = BUSTER_TYPE_CODE_INT; a <= BUSTER_TYPE_CODE_UINT128; a += 1)
    {
        for (uint32_t b = BUSTER_TYPE_CODE_INT; b <= BUSTER_TYPE_CODE_UINT128; b += 1)
        {
            type_test_expect(&tally, type_integer_codes[(a & 7) << 3 | (b & 7)] == type_integer_conversion_code(a, b), "conversion kernels", "type_integer_codes");
        }
    }
    uint8_t promoted[BUSTER_TYPE_CODE_COUNT];
    uint8_t promoted_scalar[BUSTER_TYPE_CODE_COUNT];
    type_promote_codes(left + 0, BUSTER_TYPE_CODE_COUNT, promoted);
    for (uint32_t a = 0; a < BUSTER_TYPE_CODE_COUNT; a += 1)
    {
        promoted_scalar[a] = type_promote_code(left[a * BUSTER_TYPE_CODE_COUNT]);
        type_promote_codes(&left[a * BUSTER_TYPE_CODE_COUNT], 1, &promoted[a]);
    }
    type_test_expect(&tally, memcmp(promoted, promoted_scalar, sizeof(promoted)) == 0, "conversion kernels", "the promotions");
    // Every length to 200, with bytes past the end that must stay as they were.
    uint64_t random = seed;
    for (uint64_t count = 0; count <= 200; count += 1)
    {
        uint8_t l[256];
        uint8_t r[256];
        uint8_t s[256];
        uint8_t v[256];
        uint8_t p[256];
        for (uint64_t i = 0; i < 256; i += 1)
        {
            l[i] = (uint8_t)type_test_below(&random, BUSTER_TYPE_CODE_COUNT);
            r[i] = (uint8_t)type_test_below(&random, BUSTER_TYPE_CODE_COUNT);
        }
        memset(s, 0xa5, sizeof(s));
        memset(v, 0xa5, sizeof(v));
        memset(p, 0xa5, sizeof(p));
        type_test_expect(&tally, type_test_kernels_agree(l, r, count, s, v, p), "conversion kernels", "a length");
        type_test_expect(&tally, v[count] == 0xa5 && p[count] == 0xa5 && memcmp(v + count, s + count, sizeof(v) - count) == 0, "conversion kernels",
                         "a kernel wrote past its count");
    }
    // At a PROT_NONE page: every operand and result ends where the readable page does.
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* pages = mmap(0, (size_t)page * 6, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (type_test_expect(&tally, pages != MAP_FAILED, "conversion kernels", "mmap"))
    {
        mprotect(pages + page, (size_t)page, PROT_NONE);
        mprotect(pages + 3 * page, (size_t)page, PROT_NONE);
        mprotect(pages + 5 * page, (size_t)page, PROT_NONE);
        for (uint64_t count = 0; count <= 130; count += 1)
        {
            uint8_t* l = pages + page - count;
            uint8_t* r = pages + 3 * page - count;
            uint8_t* s = pages + 5 * page - count;
            uint8_t v[256];
            uint8_t p[256];
            for (uint64_t i = 0; i < count; i += 1)
            {
                l[i] = (uint8_t)type_test_below(&random, BUSTER_TYPE_CODE_COUNT);
                r[i] = (uint8_t)type_test_below(&random, BUSTER_TYPE_CODE_COUNT);
            }
            type_usual_arithmetic_codes(l, r, count, v);
            type_usual_arithmetic_codes_pairs(l, r, count, p);
            type_usual_arithmetic_codes(l, r, count, s);
            type_promote_codes(l, count, s);
            type_usual_arithmetic_codes(l, r, count, s);
            type_test_expect(&tally, count == 0 || (memcmp(s, v, count) == 0 && memcmp(s, p, count) == 0), "conversion kernels", "at a page's end");
        }
        munmap(pages, (size_t)page * 6);
    }
    type_test_report("conversion kernels", tally);
    return tally.failures == 0;
}

typedef struct TypeTestConversion TypeTestConversion;
struct TypeTestConversion
{
    TypeId left;
    TypeId right;
    TypeId result;
};

// The usual arithmetic conversions on TypeIds: the batch against the scalar function on every pair of a set of
// arithmetic types and a few others, the codes' scalar twin against the scalar function, and clang's results.
static bool type_test_conversion_types(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    Scratch scratch = scratch_begin(0);
    if (type_test_expect(&tally, type_test_table_open(&test, 4000), "conversions", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        TypeId set[256];
        uint32_t count = 0;
        for (uint32_t code = 0; code < BUSTER_TYPE_BUILTIN_COUNT; code += 1)
        {
            if (type_is_valid(table, BUSTER_TYPE_BUILTIN(code)))
            {
                set[count] = BUSTER_TYPE_BUILTIN(code);
                count += 1;
            }
        }
        const uint32_t widths[] = {
            1,
            2,
            8,
            9,
            31,
            32,
            33,
            64,
            65,
            128,
            129,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
        {
            set[count] = type_bit_int(table, widths[i], true);
            count += 1;
            if (widths[i] > 1)
            {
                set[count] = type_bit_int(table, widths[i], false);
                count += 1;
            }
        }
        const TypeId underlying[] = {
            BUSTER_TYPE_BOOL,
            BUSTER_TYPE_SCHAR,
            BUSTER_TYPE_UCHAR,
            BUSTER_TYPE_SHORT,
            BUSTER_TYPE_UINT,
            BUSTER_TYPE_LONG,
            BUSTER_TYPE_ULLONG,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(underlying); i += 1)
        {
            set[count] = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
            type_enum_complete(table, set[count], underlying[i], 0);
            count += 1;
        }
        set[count] = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        count += 1;
        set[count] = type_aligned(table, BUSTER_TYPE_INT, 0);
        count += 1;
        set[count] = type_aligned(table, BUSTER_TYPE_UCHAR, 4) | BUSTER_TYPE_CONST;
        count += 1;
        set[count] = type_atomic(table, BUSTER_TYPE_USHORT);
        count += 1;
        set[count] = type_atomic(table, BUSTER_TYPE_DOUBLE) | BUSTER_TYPE_VOLATILE;
        count += 1;
        set[count] = type_complex(table, type_bit_int(table, 40, false));
        count += 1;
        set[count] = type_pointer(table, BUSTER_TYPE_INT);
        count += 1;
        set[count] = BUSTER_TYPE_LONG | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE;
        count += 1;
        const uint64_t pair_count = (uint64_t)count * count;
        TypeId* left = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, pair_count);
        TypeId* right = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, pair_count);
        TypeId* expected = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, pair_count);
        TypeId* results = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, TypeId, pair_count);
        if (type_test_expect(&tally, left != 0 && right != 0 && expected != 0 && results != 0, "conversions", "no memory"))
        {
            for (uint64_t i = 0; i < pair_count; i += 1)
            {
                left[i] = set[i / count];
                right[i] = set[i % count];
                expected[i] = type_usual_arithmetic(table, left[i], right[i]);
            }
            type_usual_arithmetic_types(table, left, right, pair_count, results);
            for (uint64_t i = 0; i < pair_count; i += 1)
            {
                char detail[96];
                snprintf(detail, sizeof(detail), "%u and %u: batch %u, scalar %u", left[i], right[i], results[i], expected[i]);
                type_test_expect(&tally, results[i] == expected[i], "conversions batch", detail);
                // The codes' scalar twin agrees with the scalar function wherever it decides.
                const uint8_t code = type_usual_arithmetic_code(type_arithmetic_code(table, left[i]), type_arithmetic_code(table, right[i]));
                if (code != BUSTER_TYPE_CODE_DEFERRED)
                {
                    snprintf(detail, sizeof(detail), "%u and %u: code %u, scalar %u", left[i], right[i], code, expected[i]);
                    type_test_expect(&tally, BUSTER_TYPE_BUILTIN(code) == expected[i], "conversions codes", detail);
                }
            }
        }
        // clang 23's results, from __typeof__ of each sum compared with __builtin_types_compatible_p.
        const TypeId b2 = type_bit_int(table, 2, false);
        const TypeId b8 = type_bit_int(table, 8, false);
        const TypeId b9 = type_bit_int(table, 9, false);
        const TypeId b32 = type_bit_int(table, 32, false);
        const TypeId b33 = type_bit_int(table, 33, false);
        const TypeId b40 = type_bit_int(table, 40, false);
        const TypeId b64 = type_bit_int(table, 64, false);
        const TypeId b65 = type_bit_int(table, 65, false);
        const TypeId u1 = type_bit_int(table, 1, true);
        const TypeId u32 = type_bit_int(table, 32, true);
        const TypeId u64 = type_bit_int(table, 64, true);
        const TypeId complex_char = BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_CHAR);
        const TypeId complex_uchar = BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_UCHAR);
        const TypeId complex_short = BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_SHORT);
        const TypeId complex_int = BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT);
        const TypeTestConversion conversions[] = {
            { BUSTER_TYPE_BF16, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_FLOAT16 },
            { BUSTER_TYPE_HALF, BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT },
            { BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT16, BUSTER_TYPE_FLOAT },
            { BUSTER_TYPE_BF16, BUSTER_TYPE_BF16, BUSTER_TYPE_BF16 },
            { BUSTER_TYPE_BF16, BUSTER_TYPE_HALF, BUSTER_TYPE_FLOAT },
            { BUSTER_TYPE_FLOAT16, BUSTER_TYPE_INT, BUSTER_TYPE_FLOAT16 },
            { BUSTER_TYPE_LONG_DOUBLE, BUSTER_TYPE_FLOAT128, BUSTER_TYPE_FLOAT128 },
            { complex_char, complex_char, complex_char },
            { complex_char, complex_uchar, complex_uchar },
            { complex_uchar, complex_char, complex_uchar },
            { complex_char, BUSTER_TYPE_CHAR, complex_int },
            { complex_short, complex_char, complex_short },
            { complex_int, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_COMPLEX_DOUBLE },
            { BUSTER_TYPE_COMPLEX_FLOAT, BUSTER_TYPE_LLONG, BUSTER_TYPE_COMPLEX_FLOAT },
            { BUSTER_TYPE_COMPLEX_FLOAT, complex_int, BUSTER_TYPE_COMPLEX_FLOAT },
            { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT16), BUSTER_TYPE_BF16, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT16) },
            { complex_int, BUSTER_TYPE_BF16, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_BF16) },
            { complex_int, BUSTER_TYPE_INT128, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT128) },
            { complex_char, BUSTER_TYPE_UINT128, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_UINT128) },
            { complex_short, BUSTER_TYPE_BOOL, complex_int },
            { complex_char, BUSTER_TYPE_HALF, BUSTER_TYPE_COMPLEX_FLOAT },
            { complex_uchar, b8, complex_uchar },
            { complex_int, u32, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_UINT) },
            { complex_int, b40, type_complex(table, b40) },
            { BUSTER_TYPE_LLONG, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULLONG },
            { BUSTER_TYPE_LONG, BUSTER_TYPE_UINT, BUSTER_TYPE_LONG },
            { b33, BUSTER_TYPE_INT, b33 },
            { b32, BUSTER_TYPE_INT, BUSTER_TYPE_INT },
            { u32, BUSTER_TYPE_INT, BUSTER_TYPE_UINT },
            { b64, BUSTER_TYPE_UINT, b64 },
            { b2, b2, b2 },
            { u1, BUSTER_TYPE_BOOL, BUSTER_TYPE_INT },
            { b8, BUSTER_TYPE_CHAR, BUSTER_TYPE_INT },
            { b9, BUSTER_TYPE_SHORT, BUSTER_TYPE_INT },
            { b65, BUSTER_TYPE_ULLONG, b65 },
            { u64, BUSTER_TYPE_LONG, BUSTER_TYPE_ULONG },
            { b64, BUSTER_TYPE_ULONG, BUSTER_TYPE_ULONG },
            { BUSTER_TYPE_VOID, BUSTER_TYPE_INT, 0 },
            { BUSTER_TYPE_NULLPTR, BUSTER_TYPE_INT, 0 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(conversions); i += 1)
        {
            const TypeId result = type_usual_arithmetic(table, conversions[i].left, conversions[i].right);
            type_test_expect_number(&tally, result == conversions[i].result, "conversions clang", "usual arithmetic conversion", result, conversions[i].result);
        }
        TypeId packed_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 3);
        type_enum_complete(table, packed_enum, BUSTER_TYPE_UCHAR, 0);
        TypeId unsigned_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 4);
        type_enum_complete(table, unsigned_enum, BUSTER_TYPE_UINT, 0);
        const TypeTestConversion promotions[] = {
            { BUSTER_TYPE_BOOL, 0, BUSTER_TYPE_INT },
            { BUSTER_TYPE_UCHAR, 0, BUSTER_TYPE_INT },
            { BUSTER_TYPE_USHORT | BUSTER_TYPE_CONST, 0, BUSTER_TYPE_INT },
            { BUSTER_TYPE_HALF, 0, BUSTER_TYPE_FLOAT },
            { BUSTER_TYPE_FLOAT16, 0, BUSTER_TYPE_FLOAT16 },
            { b2, 0, b2 },
            { unsigned_enum, 0, BUSTER_TYPE_UINT },
            { packed_enum, 0, BUSTER_TYPE_INT },
            { complex_char, 0, complex_char },
            { type_atomic(table, BUSTER_TYPE_SHORT), 0, BUSTER_TYPE_INT },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(promotions); i += 1)
        {
            type_test_expect_number(&tally, type_promote(table, promotions[i].left) == promotions[i].result, "conversions clang", "promotion",
                                    type_promote(table, promotions[i].left), promotions[i].result);
        }
        const TypeTestConversion bit_fields[] = {
            { BUSTER_TYPE_LONG, 3, BUSTER_TYPE_INT },
            { BUSTER_TYPE_UINT, 32, BUSTER_TYPE_UINT },
            { BUSTER_TYPE_UINT, 31, BUSTER_TYPE_INT },
            { BUSTER_TYPE_INT, 32, BUSTER_TYPE_INT },
            { BUSTER_TYPE_LONG, 40, BUSTER_TYPE_LONG },
            { BUSTER_TYPE_ULLONG, 63, BUSTER_TYPE_ULLONG },
            { b40, 5, b40 },
            { unsigned_enum, 32, BUSTER_TYPE_UINT },
            { unsigned_enum, 31, BUSTER_TYPE_INT },
            { BUSTER_TYPE_BOOL, 1, BUSTER_TYPE_INT },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bit_fields); i += 1)
        {
            const TypeId result = type_promote_bit_field(table, bit_fields[i].left, bit_fields[i].right);
            type_test_expect_number(&tally, result == bit_fields[i].result, "conversions clang", "bit-field promotion", result, bit_fields[i].result);
        }
        const TypeTestConversion arguments[] = {
            { BUSTER_TYPE_FLOAT, 0, BUSTER_TYPE_DOUBLE },
            { BUSTER_TYPE_FLOAT | BUSTER_TYPE_CONST, 0, BUSTER_TYPE_DOUBLE },
            { BUSTER_TYPE_HALF, 0, BUSTER_TYPE_DOUBLE },
            { BUSTER_TYPE_FLOAT16, 0, BUSTER_TYPE_FLOAT16 },
            { BUSTER_TYPE_BF16, 0, BUSTER_TYPE_BF16 },
            { BUSTER_TYPE_CHAR, 0, BUSTER_TYPE_INT },
            { BUSTER_TYPE_NULLPTR, 0, BUSTER_TYPE_NULLPTR },
            { BUSTER_TYPE_DOUBLE, 0, BUSTER_TYPE_DOUBLE },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arguments); i += 1)
        {
            const TypeId result = type_default_argument_promote(table, arguments[i].left);
            type_test_expect_number(&tally, result == arguments[i].result, "conversions clang", "default argument promotion", result, arguments[i].result);
        }
        // Enumerations' underlying types, as clang chooses them from their values.
        typedef struct TypeTestEnumCase TypeTestEnumCase;
        struct TypeTestEnumCase
        {
            bool packed;
            int64_t value;
            bool is_unsigned;
            TypeId expected;
        };
        const TypeTestEnumCase enum_cases[] = {
            { true, 200, false, BUSTER_TYPE_UCHAR },
            { true, -1, false, BUSTER_TYPE_SCHAR },
            { false, 1, false, BUSTER_TYPE_UINT },
            { false, 0, false, BUSTER_TYPE_UINT },
            { false, -1, false, BUSTER_TYPE_INT },
            { false, 0x100000000ll, false, BUSTER_TYPE_ULONG },
            { false, -0x80000001ll, false, BUSTER_TYPE_LONG },
            { true, -129, false, BUSTER_TYPE_SHORT },
            { true, 0x10000, false, BUSTER_TYPE_UINT },
            { false, -1, true, BUSTER_TYPE_ULONG },
            { false, INT64_MIN, false, BUSTER_TYPE_LONG },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(enum_cases); i += 1)
        {
            uint32_t negative = 0;
            uint32_t positive = 0;
            type_enum_value_bits(enum_cases[i].value, enum_cases[i].is_unsigned, &negative, &positive);
            const TypeId result = type_enum_underlying(enum_cases[i].packed, negative, positive);
            type_test_expect_number(&tally, result == enum_cases[i].expected, "conversions clang", "an enum's underlying type", result, enum_cases[i].expected);
        }
        type_test_expect(&tally, type_enum_underlying(false, 0, 0) == BUSTER_TYPE_UINT, "conversions clang", "an enum without enumerators");
        type_test_table_close(&test);
    }
    scratch_end(scratch);
    type_test_report("conversions", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Compatibility and composite types

// Two types' compatibility, and their composite: expected (0 if they're incompatible).
static void type_test_merge(TypeTestTally* tally, TypeTable* table, TypeId left, TypeId right, TypeId expected, const char* what)
{
    const TypeAnswer answer = type_compatible(table, left, right);
    const bool compatible = answer == TYPE_ANSWER_YES;
    const TypeId composite = type_composite(table, left, right);
    char detail[160];
    snprintf(detail, sizeof(detail), "%s: compatible %d, composite %u against %u", what, (int)answer, composite, expected);
    type_test_expect(tally, answer != TYPE_ANSWER_FULL && compatible == (expected != 0) && composite == expected, "compatibility", detail);
    type_test_expect(tally, type_compatible(table, right, left) == answer, "compatibility", "compatibility isn't symmetric");
}

static bool type_test_compatibility(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 4000), "compatibility", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const uint8_t prototype = BUSTER_TYPE_FLAG_PROTOTYPE;
        const TypeId int_pointer = type_pointer(table, BUSTER_TYPE_INT);
        const TypeId const_int_pointer = type_pointer(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST);
        type_test_merge(&tally, table, int_pointer, const_int_pointer, 0, "int * and const int *");
        type_test_merge(&tally, table, const_int_pointer, const_int_pointer, const_int_pointer, "const int * and itself");
        type_test_merge(&tally, table, BUSTER_TYPE_INT, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 0, "int and const int");
        const TypeId int3 = type_array(table, BUSTER_TYPE_INT, 3);
        const TypeId int_open = type_incomplete_array(table, BUSTER_TYPE_INT);
        type_test_merge(&tally, table, int_open, int3, int3, "int[] and int[3]");
        type_test_merge(&tally, table, int3, int_open, int3, "int[3] and int[]");
        type_test_merge(&tally, table, type_array(table, BUSTER_TYPE_INT, 2), int3, 0, "int[2] and int[3]");
        type_test_merge(&tally, table, type_pointer(table, int_open), type_pointer(table, int3), type_pointer(table, int3), "int (*)[] and int (*)[3]");
        type_test_merge(&tally, table, int_pointer, int_open, 0, "int * and int[], C 6.7.6.2's extern example");
        const TypeId no_prototype = type_function(table, BUSTER_TYPE_INT, 0, 0, 0);
        const TypeId of_int = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype);
        type_test_merge(&tally, table, no_prototype, of_int, of_int, "int () and int (int)");
        type_test_merge(&tally, table, of_int, no_prototype, of_int, "int (int) and int ()");
        type_test_merge(&tally, table, no_prototype, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_SHORT }, 1, prototype), 0, "int () and int (short)");
        type_test_merge(&tally, table, no_prototype, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_FLOAT }, 1, prototype), 0, "int () and int (float)");
        const TypeId of_double = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_DOUBLE }, 1, prototype);
        type_test_merge(&tally, table, no_prototype, of_double, of_double, "int () and int (double)");
        type_test_merge(&tally, table, no_prototype, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype | BUSTER_TYPE_FLAG_VARIADIC), 0,
                        "int () and int (int, ...)");
        const TypeId of_float16 = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_FLOAT16 }, 1, prototype);
        type_test_merge(&tally, table, no_prototype, of_float16, of_float16, "int () and int (_Float16)");
        type_test_merge(&tally, table, of_int, of_double, 0, "int (int) and int (double)");
        type_test_merge(&tally, table, of_int, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT, BUSTER_TYPE_INT }, 2, prototype), 0,
                        "int (int) and int (int, int)");
        type_test_merge(&tally, table, of_int, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype | BUSTER_TYPE_FLAG_VARIADIC), 0,
                        "int (int) and int (int, ...)");
        type_test_merge(&tally, table, type_function(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 0, 0, prototype), type_function(table, BUSTER_TYPE_INT, 0, 0, prototype), 0,
                        "const int (void) and int (void)");
        // An enum is compatible with its underlying type (and only unqualified, as clang compares them), not with another
        // integer type or another enum.
        const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
        type_enum_complete(table, enumeration, BUSTER_TYPE_UINT, 0);
        const TypeId other = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        type_enum_complete(table, other, BUSTER_TYPE_UINT, 0);
        type_test_merge(&tally, table, enumeration, BUSTER_TYPE_UINT, BUSTER_TYPE_UINT, "enum E and unsigned int");
        type_test_merge(&tally, table, BUSTER_TYPE_UINT, enumeration, BUSTER_TYPE_UINT, "unsigned int and enum E");
        type_test_merge(&tally, table, enumeration, BUSTER_TYPE_INT, 0, "enum E and int");
        type_test_merge(&tally, table, enumeration, other, 0, "enum E and enum F");
        type_test_merge(&tally, table, type_pointer(table, enumeration | BUSTER_TYPE_CONST), type_pointer(table, BUSTER_TYPE_UINT | BUSTER_TYPE_CONST), 0,
                        "const enum E * and const unsigned int *, as clang has it");
        type_test_merge(&tally, table, type_pointer(table, enumeration), type_pointer(table, BUSTER_TYPE_UINT), type_pointer(table, BUSTER_TYPE_UINT),
                        "enum E * and unsigned int *");
        const TypeId of_enum = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ enumeration }, 1, prototype);
        type_test_merge(&tally, table, no_prototype, of_enum, of_enum, "int () and int (enum E of unsigned int)");
        const TypeId small = type_tag(table, BUSTER_TYPE_KIND_ENUM, 3);
        type_enum_complete(table, small, BUSTER_TYPE_UCHAR, 0);
        type_test_merge(&tally, table, no_prototype, type_function(table, BUSTER_TYPE_INT, (TypeId[]){ small }, 1, prototype), 0,
                        "int () and int (enum of unsigned char)");
        // ALIGNED is transparent: the composite is the left type, as clang's mergeTypes returns its left operand.
        const TypeId aligned1 = type_aligned(table, BUSTER_TYPE_INT, 0);
        type_test_merge(&tally, table, type_pointer(table, aligned1), int_pointer, type_pointer(table, aligned1), "int aligned(1) * and int *");
        type_test_merge(&tally, table, int_pointer, type_pointer(table, aligned1), int_pointer, "int * and int aligned(1) *");
        type_test_merge(&tally, table, type_pointer(table, type_incomplete_array(table, aligned1)), type_pointer(table, int3), type_pointer(table, int3),
                        "int aligned(1) (*)[] and int (*)[3]");
        const TypeId gnu = type_vector(table, BUSTER_TYPE_INT, 4, false);
        const TypeId ext = type_vector(table, BUSTER_TYPE_INT, 4, true);
        type_test_merge(&tally, table, gnu, ext, gnu, "GNU and clang vectors of 4 int");
        type_test_merge(&tally, table, gnu, type_vector(table, BUSTER_TYPE_UINT, 4, false), 0, "vectors of int and unsigned int");
        type_test_merge(&tally, table, type_bit_int(table, 5, false), type_bit_int(table, 5, true), 0, "_BitInt(5) and unsigned _BitInt(5)");
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 4);
        type_test_merge(&tally, table, record, type_tag(table, BUSTER_TYPE_KIND_STRUCT, 4), 0, "two struct S declarations, in C17");
        type_test_merge(&tally, table, type_atomic(table, BUSTER_TYPE_INT), type_atomic(table, BUSTER_TYPE_INT), type_atomic(table, BUSTER_TYPE_INT), "_Atomic int");
        // C17 6.2.7p5: int f(int (*)(), double (*)[3]); and int f(int (*)(char *), double (*)[]); make the composite
        // int f(int (*)(char *), double (*)[3]);
        const TypeId char_pointer = type_pointer(table, BUSTER_TYPE_CHAR);
        const TypeId of_char_pointer = type_function(table, BUSTER_TYPE_INT, &char_pointer, 1, prototype);
        const TypeId double3 = type_pointer(table, type_array(table, BUSTER_TYPE_DOUBLE, 3));
        const TypeId double_open = type_pointer(table, type_incomplete_array(table, BUSTER_TYPE_DOUBLE));
        const TypeId f1 = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ type_pointer(table, no_prototype), double3 }, 2, prototype);
        const TypeId f2 = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ type_pointer(table, of_char_pointer), double_open }, 2, prototype);
        const TypeId f3 = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ type_pointer(table, of_char_pointer), double3 }, 2, prototype);
        type_test_merge(&tally, table, f1, f2, f3, "C 6.2.7's example");
        // C17 6.7.6.3p21: double maximum(int n, int m, double a[n][m]); and its forms with [*] and [] are compatible
        // (the parameter is adjusted to a pointer to a variable length array).
        const TypeId of_m = type_pointer(table, type_variable_array(table, BUSTER_TYPE_DOUBLE, 2));
        const TypeId of_star = type_pointer(table, type_variable_array(table, BUSTER_TYPE_DOUBLE, 0));
        const TypeId maximum_m = type_function(table, BUSTER_TYPE_DOUBLE, (TypeId[]){ BUSTER_TYPE_INT, BUSTER_TYPE_INT, of_m }, 3, prototype);
        const TypeId maximum_star = type_function(table, BUSTER_TYPE_DOUBLE, (TypeId[]){ BUSTER_TYPE_INT, BUSTER_TYPE_INT, of_star }, 3, prototype);
        type_test_merge(&tally, table, maximum_m, maximum_star, maximum_m, "C 6.7.6.3's maximum, a[n][m] and a[*][*]");
        // C17 6.7.6.2p6: int (*p)[4][n+1] and int a[n][6][m] aren't compatible (4 and 6); int (*r)[n][n][n+1] and
        // int c[n][n][6][m] are.
        const TypeId inner4 = type_array(table, type_variable_array(table, BUSTER_TYPE_INT, 3), 4);
        const TypeId inner6 = type_array(table, type_variable_array(table, BUSTER_TYPE_INT, 4), 6);
        type_test_merge(&tally, table, type_pointer(table, inner4), type_pointer(table, inner6), 0, "C 6.7.6.2's p and a");
        const TypeId r = type_pointer(table, type_variable_array(table, type_variable_array(table, type_variable_array(table, BUSTER_TYPE_INT, 3), 5), 5));
        const TypeId c = type_pointer(table, type_variable_array(table, type_array(table, type_variable_array(table, BUSTER_TYPE_INT, 4), 6), 5));
        type_test_expect(&tally, type_compatible(table, r, c) == TYPE_ANSWER_YES, "compatibility", "C 6.7.6.2's r and c");
        type_test_merge(&tally, table, type_pointer(table, type_variable_array(table, BUSTER_TYPE_INT, 9)), type_pointer(table, int3), type_pointer(table, int3),
                        "int (*)[n] and int (*)[3]");
        type_test_table_close(&test);
    }
    type_test_report("compatibility", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Printing

static bool type_test_printing(Arena* arena)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 4000), "printing", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const uint8_t prototype = BUSTER_TYPE_FLAG_PROTOTYPE;
        const TypeId of_int = type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype);
        const TypeId no_prototype = type_function(table, BUSTER_TYPE_INT, 0, 0, 0);
        const TypeId int_pointer = type_pointer(table, BUSTER_TYPE_INT);
        const uint8_t names[] = "Svalue";
        const uint8_t* name_bytes[2] = {
            names,
            names,
        };
        const uint32_t name_lengths[2] = {
            0,
            1,
        };
        const TypeNames tag_names = {
            .bytes = name_bytes,
            .lengths = name_lengths,
            .count = 2,
        };
        typedef struct TypeTestPrinted TypeTestPrinted;
        struct TypeTestPrinted
        {
            TypeId type;
            const char* text;
        };
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeTestPrinted cases[] = {
            { BUSTER_TYPE_INT, "int" },
            { BUSTER_TYPE_UINT128, "unsigned __int128" },
            { BUSTER_TYPE_BOOL, "_Bool" },
            { BUSTER_TYPE_HALF, "__fp16" },
            { BUSTER_TYPE_NULLPTR, "nullptr_t" },
            { BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT), "_Complex int" },
            { BUSTER_TYPE_COMPLEX_LONG_DOUBLE | BUSTER_TYPE_CONST, "const _Complex long double" },
            { type_pointer(table, type_array(table, type_pointer(table, of_int), 3)), "int (*(*)[3])(int)" },
            { type_pointer(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST) | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE, "const int *const volatile" },
            { type_pointer(table, int_pointer | BUSTER_TYPE_RESTRICT) | BUSTER_TYPE_CONST, "int *restrict *const" },
            { type_array(table, BUSTER_TYPE_CHAR, 4), "char[4]" },
            { type_incomplete_array(table, type_array(table, BUSTER_TYPE_INT, 3)), "int[][3]" },
            { type_array(table, type_pointer(table, no_prototype), 2), "int (*[2])()" },
            { no_prototype, "int ()" },
            { type_function(table, BUSTER_TYPE_INT, 0, 0, prototype), "int (void)" },
            { type_function(table, BUSTER_TYPE_INT, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype | BUSTER_TYPE_FLAG_VARIADIC), "int (int, ...)" },
            { type_function(table, BUSTER_TYPE_INT, 0, 0, prototype | BUSTER_TYPE_FLAG_VARIADIC), "int (...)" },
            { type_function(table, type_pointer(table, type_function(table, BUSTER_TYPE_VOID, (TypeId[]){ BUSTER_TYPE_INT }, 1, prototype)), (TypeId[]){ BUSTER_TYPE_INT }, 1,
                            prototype),
              "void (*(int))(int)" },
            { type_atomic(table, int_pointer), "_Atomic(int *)" },
            { type_atomic(table, int_pointer) | BUSTER_TYPE_CONST, "const _Atomic(int *)" },
            { type_pointer(table, type_atomic(table, BUSTER_TYPE_INT)), "_Atomic(int) *" },
            { type_bit_int(table, 7, true), "unsigned _BitInt(7)" },
            { type_complex(table, type_bit_int(table, 40, false)), "_Complex _BitInt(40)" },
            { type_array(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 3), "const int[3]" },
            { type_pointer(table, type_array(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 3)), "const int (*)[3]" },
            { type_vector(table, BUSTER_TYPE_INT, 4, false), "__attribute__((__vector_size__(4 * sizeof(int)))) int" },
            { type_vector(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 4, false), "__attribute__((__vector_size__(4 * sizeof(const int)))) const int" },
            { type_vector(table, BUSTER_TYPE_INT, 3, true), "int __attribute__((ext_vector_type(3)))" },
            { type_pointer(table, type_vector(table, BUSTER_TYPE_FLOAT, 4, true)), "float * __attribute__((ext_vector_type(4)))" },
            { record, "struct S" },
            { type_pointer(table, record | BUSTER_TYPE_VOLATILE), "volatile struct S *" },
            { type_tag(table, BUSTER_TYPE_KIND_UNION, 0), "union (unnamed)" },
            { type_variable_array(table, BUSTER_TYPE_INT, 0), "int[*]" },
            { type_aligned(table, int_pointer, 0) | BUSTER_TYPE_CONST, "int *const" },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            const uint64_t length = arena->length;
            const String8 printed = type_print(table, arena, cases[i].type, (String8){ 0 }, &tag_names, 0);
            const bool same = printed.length == strlen(cases[i].text) && memcmp(printed.pointer, cases[i].text, printed.length) == 0;
            char detail[256];
            snprintf(detail, sizeof(detail), "\"%.*s\" against \"%s\"", (int)printed.length, printed.pointer, cases[i].text);
            type_test_expect(&tally, same, "printing", detail);
            arena_pop_to(arena, length);
        }
        type_test_expect(&tally, type_test_prints(table, BUSTER_TYPE_BOOL, BUSTER_TYPE_PRINT_C23, "bool", arena), "printing", "bool in C23");
        // A name goes where the declarator's name goes.
        const uint64_t length = arena->length;
        const String8 named = type_print(table, arena, type_pointer(table, type_array(table, BUSTER_TYPE_INT, 3)), BUSTER_S8("p"), 0, 0);
        type_test_expect(&tally, named.length == 11 && memcmp(named.pointer, "int (*p)[3]", 11) == 0, "printing", "a named pointer to an array");
        arena_pop_to(arena, length);
        type_test_table_close(&test);
    }
    type_test_report("printing", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The clang oracle
//
// The world: random types, records and enums made through the table, then written as one C file. Every type gets a
// typedef, Ty_N, spelled in full from C's declarator syntax (the test's own spelling, built a type at a time from the
// spellings of the types it is made of, so that it owes nothing to type_print), and clang -Xclang -ast-dump prints each
// typedef's type as written. A spelling with no typedef name in it has no sugar, so clang prints it as it prints the
// canonical type, which type_print must match; the types that need a typedef name to be spelled (an ALIGNED type, a
// complex _BitInt and the builtin complex types C can't spell) are compared only where that name is the whole type.
// The same file holds _Static_asserts of every complete type's size and alignment, every member's offset, every enum's
// underlying type and compatibility of pairs of types, and a main() that sets each bit-field in a zeroed object and
// prints any whose bits aren't the ones the layout gave it.

#define BUSTER_TYPE_TEST_SPELLING_LIMIT 4096
#define BUSTER_TYPE_TEST_RUN_SECONDS 300
#define BUSTER_TYPE_TEST_PATH_BYTES 4096
// Spellings' qualification: where a qualifier goes (before a specifier, after a pointer's star), or nowhere (a
// function type's are dropped).
#define BUSTER_TYPE_TEST_PREPEND 0
#define BUSTER_TYPE_TEST_APPEND 1
#define BUSTER_TYPE_TEST_DROP 2

extern char** environ;

// A string in an arena of its own, pushed a piece at a time so that it stays contiguous.
typedef struct TypeTestText TypeTestText;
struct TypeTestText
{
    Arena* arena;
    char* bytes;
    uint64_t length;
    bool valid;
};

static void type_test_append(TypeTestText* text, const char* bytes, uint64_t length)
{
    char* copy = length != 0 && text->valid ? arena_push(text->arena, length, 1) : 0;
    if (copy != 0)
    {
        memcpy(copy, bytes, length);
        text->bytes = text->bytes == 0 ? copy : text->bytes;
        text->length += length;
    }
    else if (length != 0)
    {
        text->valid = false;
    }
}

static __attribute__((format(printf, 2, 3))) void type_test_format(TypeTestText* text, const char* format, ...)
{
    char buffer[BUSTER_TYPE_TEST_SPELLING_LIMIT * 3];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length >= 0 && (uint64_t)length < sizeof(buffer))
    {
        type_test_append(text, buffer, (uint64_t)length);
    }
    else
    {
        text->valid = false;
    }
}

// A type's spelling: a declaration of it is left, the name, then right. sugared says a typedef name is in it, and
// spellable that it can be written at all (not a variable length array, nullptr_t in gnu17 or an anonymous tag).
typedef struct TypeTestSpelling TypeTestSpelling;
struct TypeTestSpelling
{
    char* left;
    char* right;
    uint8_t mode;
    bool sugared;
    bool spellable;
    // The oracle's number for the type's typedef, Ty_<number>.
    uint32_t number;
};

typedef struct TypeTestWorld TypeTestWorld;
struct TypeTestWorld
{
    TypeTestTable test;
    TypeTestPool pool;
    Arena arena;
    // By tag: written inline in its parent (an anonymous member's record), types.count when completed, and an enum's
    // enumerators (values from enum_values) and whether its underlying type is fixed.
    uint8_t* anonymous;
    uint32_t* completed_at;
    uint32_t* enum_begin;
    uint32_t* enum_count;
    uint8_t* enum_fixed;
    int64_t* enum_values;
    uint8_t* enum_unsigned;
    uint32_t enum_value_count;
    // Tags in completion order.
    uint32_t* completions;
    uint32_t completion_count;
    TypeTestSpelling* spellings;
    // Identifier ids' spellings, x<id>, for the printer.
    const uint8_t** name_bytes;
    uint32_t* name_lengths;
    TypeNames names;
    uint32_t next_name;
};

static uint32_t type_test_new_name(TypeTestWorld* world)
{
    world->next_name += 1;
    return world->next_name;
}

// Completes a record and remembers when.
static void type_test_world_completed(TypeTestWorld* world, TypeId tag_type)
{
    const uint32_t tag = world->test.table.types.index[type_index(tag_type)];
    world->completed_at[tag] = world->test.table.types.count;
    world->completions[world->completion_count] = tag;
    world->completion_count += 1;
}

// A random pool type that may be a member's (a complete object type), or a bit-field's when bit_field.
static TypeId type_test_world_member_type(TypeTestWorld* world, uint64_t* random, bool bit_field)
{
    const TypeTable* table = &world->test.table;
    TypeId result = 0;
    for (uint32_t attempt = 0; result == 0 && attempt < 64; attempt += 1)
    {
        const TypeId candidate = world->pool.types[type_test_below(random, world->pool.count)];
        const uint8_t kind = type_kind(table, type_strip_aligned(table, candidate));
        const bool object = type_is_complete(table, candidate) && kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION;
        if (object && (!bit_field || (type_test_bit_field_type(table, candidate) && (type_qualifiers(candidate) & BUSTER_TYPE_CONST) == 0)))
        {
            result = candidate;
        }
    }
    return result;
}

// One random derived type over the pool (and the pool's qualified versions), added to the pool.
static void type_test_world_derive(TypeTestWorld* world, uint64_t* random)
{
    TypeTable* table = &world->test.table;
    const TypeId base = world->pool.types[type_test_below(random, world->pool.count)];
    const uint64_t pick = type_test_below(random, 12);
    TypeId result = 0;
    if (pick <= 2)
    {
        result = type_pointer(table, type_test_below(random, 10) == 0 ? BUSTER_TYPE_VOID : base);
    }
    else if (pick == 3)
    {
        result = type_array(table, base, type_test_below(random, 5));
    }
    else if (pick == 4)
    {
        result = type_incomplete_array(table, base);
    }
    else if (pick == 5)
    {
        TypeId parameters[4];
        const uint32_t count = (uint32_t)type_test_below(random, 4);
        bool valid = true;
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            const TypeId parameter = world->pool.types[type_test_below(random, world->pool.count)];
            valid = type_kind(table, type_strip_aligned(table, parameter)) != BUSTER_TYPE_KIND_VOID;
            parameters[i] = valid ? type_adjust_parameter(table, parameter) : 0;
            valid = valid && parameters[i] != 0;
        }
        uint8_t flags = count != 0 || type_test_below(random, 3) != 0 ? BUSTER_TYPE_FLAG_PROTOTYPE : 0;
        // gnu17 needs a parameter before ..., which C23 doesn't.
        flags |= count != 0 && type_test_below(random, 4) == 0 ? BUSTER_TYPE_FLAG_VARIADIC : 0;
        // A function without a prototype may only return what a function may, which type_function checks.
        result = valid ? type_function(table, base, parameters, count, flags) : 0;
    }
    else if (pick == 6 || pick == 7)
    {
        const bool ext = pick == 7;
        const uint32_t count = ext ? 1 + (uint32_t)type_test_below(random, 6) : 1u << type_test_below(random, 4);
        result = type_vector(table, type_unqualified(base), count, ext);
    }
    else if (pick == 8)
    {
        result = type_atomic(table, type_unqualified(base));
    }
    else if (pick == 9)
    {
        result = type_aligned(table, type_unqualified(base), (uint32_t)type_test_below(random, 7));
    }
    else if (pick == 10)
    {
        result = type_complex(table, type_bit_int(table, 9 + (uint32_t)type_test_below(random, 120), type_test_below(random, 2) == 0));
    }
    else
    {
        // A qualified version: restrict only on pointers to objects, which C requires.
        uint32_t qualifiers = 1 + (uint32_t)type_test_below(random, 3);
        const TypeId stripped = type_strip_aligned(table, base);
        if (type_kind(table, stripped) == BUSTER_TYPE_KIND_POINTER && type_kind(table, type_strip_aligned(table, table->types.base[type_index(stripped)])) != BUSTER_TYPE_KIND_FUNCTION &&
            type_test_below(random, 2) == 0)
        {
            qualifiers |= BUSTER_TYPE_RESTRICT;
        }
        result = type_kind(table, base) == BUSTER_TYPE_KIND_FUNCTION ? 0 : base | qualifiers;
    }
    // Arrays of functions and the like fail: only what the table made goes in the pool.
    table->last_error = TYPE_ERROR_NONE;
    if (result != 0 && type_kind(table, type_strip_aligned(table, result)) != BUSTER_TYPE_KIND_VARIABLE_ARRAY)
    {
        type_test_pool_add(&world->pool, result);
    }
}

// An enum: a fixed underlying type, or random values (and packed, now and then), and an aligned attribute now and then.
static void type_test_world_enum(TypeTestWorld* world, uint64_t* random)
{
    TypeTable* table = &world->test.table;
    const TypeId enumeration = type_tag(table, BUSTER_TYPE_KIND_ENUM, type_test_new_name(world));
    const uint32_t tag = table->types.index[type_index(enumeration)];
    const uint8_t alignment = type_test_below(random, 5) == 0 ? (uint8_t)(1 + type_test_below(random, 6)) : 0;
    world->enum_begin[tag] = world->enum_value_count;
    TypeId underlying = 0;
    if (type_test_below(random, 3) == 0)
    {
        const TypeId fixed[] = {
            BUSTER_TYPE_BOOL,
            BUSTER_TYPE_CHAR,
            BUSTER_TYPE_SCHAR,
            BUSTER_TYPE_UCHAR,
            BUSTER_TYPE_SHORT,
            BUSTER_TYPE_USHORT,
            BUSTER_TYPE_INT,
            BUSTER_TYPE_UINT,
            BUSTER_TYPE_LONG,
            BUSTER_TYPE_ULONG,
            BUSTER_TYPE_LLONG,
            BUSTER_TYPE_ULLONG,
        };
        underlying = fixed[type_test_below(random, BUSTER_ARRAY_LENGTH(fixed))];
        world->enum_fixed[tag] = 1;
        world->enum_values[world->enum_value_count] = 0;
        world->enum_unsigned[world->enum_value_count] = 0;
        world->enum_value_count += 1;
    }
    else
    {
        const bool packed = type_test_below(random, 3) == 0;
        const int64_t edges[] = {
            0,
            1,
            -1,
            127,
            128,
            255,
            256,
            -128,
            -129,
            32767,
            32768,
            65535,
            65536,
            -32768,
            -32769,
            INT32_MAX,
            (int64_t)INT32_MAX + 1,
            UINT32_MAX,
            (int64_t)UINT32_MAX + 1,
            INT32_MIN,
            (int64_t)INT32_MIN - 1,
            INT64_MAX,
            INT64_MIN,
        };
        uint32_t negative = 0;
        uint32_t positive = 0;
        const uint32_t count = 1 + (uint32_t)type_test_below(random, 3);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const bool huge = type_test_below(random, 12) == 0;
            const int64_t value = huge ? -1 : edges[type_test_below(random, BUSTER_ARRAY_LENGTH(edges))];
            world->enum_values[world->enum_value_count] = value;
            world->enum_unsigned[world->enum_value_count] = huge ? 1 : 0;
            world->enum_value_count += 1;
            type_enum_value_bits(value, huge, &negative, &positive);
        }
        underlying = type_enum_underlying(packed, negative, positive);
        world->enum_fixed[tag] = packed ? 2 : 0;
    }
    world->enum_count[tag] = world->enum_value_count - world->enum_begin[tag];
    type_enum_complete(table, enumeration, underlying, alignment);
    type_test_world_completed(world, enumeration);
    type_test_pool_add(&world->pool, enumeration);
}

// A random member: a pool type, a bit-field of it (a third of the time, where its type allows one) of a random width (0
// unnamed), and now and then an explicit alignment or packed.
static TypeMember type_test_world_member(TypeTestWorld* world, uint64_t* random)
{
    const TypeTable* table = &world->test.table;
    const bool bit_field = type_test_below(random, 3) == 0;
    TypeId type = type_test_world_member_type(world, random, bit_field);
    uint32_t width = BUSTER_TYPE_NOT_BIT_FIELD;
    if (bit_field && type != 0)
    {
        width = (uint32_t)type_test_below(random, type_integer_width(table, type) + 1);
    }
    else if (type == 0)
    {
        type = BUSTER_TYPE_INT;
    }
    const bool unnamed = width == 0 || (width != BUSTER_TYPE_NOT_BIT_FIELD && type_test_below(random, 6) == 0);
    const TypeMember result = {
        .name = unnamed ? 0 : type_test_new_name(world),
        .type = type,
        .bit_width = width,
        .alignment_log2 = !unnamed && type_test_below(random, 10) == 0 ? (uint8_t)(1 + type_test_below(random, 6)) : 0,
        .packed = !unnamed && type_test_below(random, 10) == 0,
    };
    return result;
}

static TypeRecordAttributes type_test_world_attributes(uint64_t* random)
{
    const TypeRecordAttributes result = {
        .packed = type_test_below(random, 7) == 0,
        .pack_log2 = type_test_below(random, 7) == 0 ? (uint8_t)(1 + type_test_below(random, 5)) : 0,
        .alignment_log2 = type_test_below(random, 10) == 0 ? (uint8_t)(1 + type_test_below(random, 6)) : 0,
    };
    return result;
}

// An anonymous member's struct or union: a few random members, laid out before its parent, with its parent's #pragma
// pack (the pragma applies to it too, since it is defined inside the parent).
static TypeId type_test_world_anonymous(TypeTestWorld* world, uint64_t* random, uint8_t pack)
{
    TypeTable* table = &world->test.table;
    const TypeId record = type_tag(table, type_test_below(random, 3) == 0 ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, 0);
    TypeRecordAttributes attributes = type_test_world_attributes(random);
    attributes.pack_log2 = pack;
    TypeMember members[8];
    const uint32_t count = 1 + (uint32_t)type_test_below(random, 4);
    for (uint32_t m = 0; m < count; m += 1)
    {
        members[m] = type_test_world_member(world, random);
    }
    TypeId result = 0;
    if (record != 0)
    {
        world->anonymous[table->types.index[type_index(record)]] = 1;
        if (type_complete_record(table, record, members, count, attributes))
        {
            type_test_world_completed(world, record);
            result = record;
        }
    }
    table->last_error = TYPE_ERROR_NONE;
    return result;
}

// A named record with random members, an anonymous member now and then, a flexible array member now and then, and
// random attributes.
static void type_test_world_record(TypeTestWorld* world, uint64_t* random)
{
    TypeTable* table = &world->test.table;
    const bool is_union = type_test_below(random, 4) == 0;
    const TypeRecordAttributes attributes = type_test_world_attributes(random);
    TypeMember members[BUSTER_TYPE_TEST_MEMBERS];
    const uint32_t count = (uint32_t)type_test_below(random, 13);
    for (uint32_t m = 0; m < count; m += 1)
    {
        const TypeId anonymous = type_test_below(random, 12) == 0 ? type_test_world_anonymous(world, random, attributes.pack_log2) : 0;
        members[m] = anonymous != 0 ? type_test_member(0, anonymous, BUSTER_TYPE_NOT_BIT_FIELD) : type_test_world_member(world, random);
    }
    if (!is_union && count != 0 && type_test_below(random, 8) == 0)
    {
        const TypeId element = type_test_world_member_type(world, random, false);
        const TypeId open = element != 0 ? type_incomplete_array(table, type_unqualified(element)) : 0;
        if (open != 0)
        {
            members[count - 1] = type_test_member(type_test_new_name(world), open, BUSTER_TYPE_NOT_BIT_FIELD);
        }
        table->last_error = TYPE_ERROR_NONE;
    }
    const TypeId record = type_tag(table, is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, type_test_new_name(world));
    if (record != 0 && type_complete_record(table, record, members, count, attributes))
    {
        type_test_world_completed(world, record);
        if ((table->types.flags[type_index(record)] & BUSTER_TYPE_FLAG_FLEXIBLE) == 0 && type_size(table, record) < (1u << 20))
        {
            type_test_pool_add(&world->pool, record);
        }
    }
    table->last_error = TYPE_ERROR_NONE;
}

static bool type_test_world_build(TypeTestWorld* world, uint64_t seed, uint32_t count)
{
    uint64_t random = seed;
    TypeTable* table = &world->test.table;
    for (uint32_t code = BUSTER_TYPE_CODE_BOOL; code < BUSTER_TYPE_BUILTIN_COUNT; code += 1)
    {
        if (type_is_valid(table, BUSTER_TYPE_BUILTIN(code)) && code != BUSTER_TYPE_CODE_NULLPTR)
        {
            type_test_pool_add(&world->pool, BUSTER_TYPE_BUILTIN(code));
        }
    }
    for (uint32_t i = 0; i < 24; i += 1)
    {
        type_test_world_enum(world, &random);
    }
    const uint32_t widths[] = {
        1,
        2,
        7,
        8,
        9,
        16,
        24,
        31,
        32,
        33,
        64,
        65,
        100,
        128,
        129,
        256,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        type_test_pool_add(&world->pool, type_bit_int(table, widths[i], true));
        type_test_pool_add(&world->pool, widths[i] > 1 ? type_bit_int(table, widths[i], false) : 0);
    }
    for (uint32_t level = 0; level < 4; level += 1)
    {
        for (uint32_t i = 0; i < count / 8; i += 1)
        {
            type_test_world_derive(world, &random);
        }
        for (uint32_t i = 0; i < count / 32; i += 1)
        {
            type_test_world_record(world, &random);
        }
    }
    return table->types.count < table->types.capacity;
}

// Joins strings into the world's arena: a NUL-terminated copy.
static char* type_test_join(TypeTestWorld* world, const char* a, const char* b, const char* c)
{
    const uint64_t la = strlen(a);
    const uint64_t lb = strlen(b);
    const uint64_t lc = strlen(c);
    char* result = arena_push(&world->arena, la + lb + lc + 1, 1);
    if (result != 0)
    {
        memcpy(result, a, la);
        memcpy(result + la, b, lb);
        memcpy(result + la + lb, c, lc);
        result[la + lb + lc] = 0;
    }
    return result;
}

// A type's spelling with qualifiers: before a specifier, after a pointer's star, and none on a function.
static TypeTestSpelling type_test_qualified(TypeTestWorld* world, TypeId type)
{
    TypeTestSpelling result = world->spellings[type_index(type)];
    const uint32_t qualifiers = type_qualifiers(type);
    if (qualifiers != 0 && result.mode != BUSTER_TYPE_TEST_DROP && result.left != 0)
    {
        char words[32] = "";
        const char* names[3] = {
            "const ",
            "volatile ",
            "restrict ",
        };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if ((qualifiers >> i & 1) != 0)
            {
                strcat(words, names[i]);
            }
        }
        result.left = result.mode == BUSTER_TYPE_TEST_PREPEND ? type_test_join(world, words, result.left, "") : type_test_join(world, result.left, words, "");
    }
    return result;
}

// The spelling of a type name (an abstract declarator), "left right".
static char* type_test_type_name(TypeTestWorld* world, TypeId type)
{
    const TypeTestSpelling spelling = type_test_qualified(world, type);
    return spelling.left != 0 ? type_test_join(world, spelling.left, spelling.right, "") : 0;
}

// Spells every type, in index order, from the spellings of what it is made of; see TypeTestSpelling.
static void type_test_spell_world(TypeTestWorld* world)
{
    const TypeTable* table = &world->test.table;
    char buffer[BUSTER_TYPE_TEST_SPELLING_LIMIT];
    for (uint32_t index = 1; index < table->types.count; index += 1)
    {
        const uint8_t kind = table->types.kind[index];
        const TypeId base = table->types.base[index];
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        TypeTestSpelling spelling = {
            .left = 0,
            .right = "",
            .mode = BUSTER_TYPE_TEST_PREPEND,
            .sugared = false,
            .spellable = true,
        };
        const TypeTestSpelling inner = kind != BUSTER_TYPE_KIND_NONE && base != 0 ? type_test_qualified(world, base) : (TypeTestSpelling){ 0 };
        if (index < BUSTER_TYPE_BUILTIN_COUNT && kind != BUSTER_TYPE_KIND_NONE)
        {
            if (kind == BUSTER_TYPE_KIND_COMPLEX && (BUSTER_TYPE_COMPLEX_SPELLABLE >> (index - BUSTER_TYPE_CODE_COMPLEX) & 1) == 0)
            {
                snprintf(buffer, sizeof(buffer), "Tx_b%u ", index);
                spelling.sugared = true;
            }
            else if (kind == BUSTER_TYPE_KIND_COMPLEX)
            {
                snprintf(buffer, sizeof(buffer), "_Complex %s ", type_builtins[index - BUSTER_TYPE_CODE_COMPLEX].name);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%s ", type_builtins[index].name);
            }
            spelling.left = type_test_join(world, buffer, "", "");
            spelling.spellable = index != BUSTER_TYPE_CODE_NULLPTR;
        }
        else if (kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            snprintf(buffer, sizeof(buffer), "%s_BitInt(%" PRIu64 ") ", (table->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 ? "unsigned " : "", table->types.extent[index]);
            spelling.left = type_test_join(world, buffer, "", "");
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
        {
            const uint32_t tag = table->types.index[index];
            snprintf(buffer, sizeof(buffer), "%s x%u ", kind == BUSTER_TYPE_KIND_STRUCT ? "struct" : kind == BUSTER_TYPE_KIND_UNION ? "union" : "enum", table->tags.name[tag]);
            spelling.left = type_test_join(world, buffer, "", "");
            spelling.spellable = world->anonymous[tag] == 0;
        }
        else if (kind == BUSTER_TYPE_KIND_POINTER && inner.left != 0)
        {
            const uint8_t pointee = type_kind(table, type_strip_aligned(table, base));
            const bool wrap = type_kind_is_array(pointee) || pointee == BUSTER_TYPE_KIND_FUNCTION;
            spelling.left = type_test_join(world, inner.left, wrap ? "(*" : "*", "");
            spelling.right = wrap ? type_test_join(world, ")", inner.right, "") : inner.right;
            spelling.mode = BUSTER_TYPE_TEST_APPEND;
            spelling.sugared = inner.sugared;
            spelling.spellable = inner.spellable;
        }
        else if ((kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) && inner.left != 0)
        {
            snprintf(buffer, sizeof(buffer), "[%" PRIu64 "]", table->types.extent[index]);
            spelling.left = inner.left;
            spelling.right = type_test_join(world, kind == BUSTER_TYPE_KIND_ARRAY ? buffer : "[]", inner.right, "");
            spelling.mode = world->spellings[type_index(base)].mode;
            spelling.sugared = inner.sugared;
            spelling.spellable = inner.spellable;
        }
        else if (kind == BUSTER_TYPE_KIND_FUNCTION && inner.left != 0)
        {
            uint32_t count = 0;
            const TypeId* parameters = type_parameters(table, type, &count);
            char* list = type_test_join(world, "(", "", "");
            bool valid = list != 0;
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                const char* name = type_test_type_name(world, parameters[i]);
                valid = name != 0 && world->spellings[type_index(parameters[i])].spellable;
                spelling.sugared = spelling.sugared || world->spellings[type_index(parameters[i])].sugared;
                list = valid ? type_test_join(world, list, i != 0 ? ", " : "", name) : 0;
            }
            if (valid && (table->types.flags[index] & BUSTER_TYPE_FLAG_VARIADIC) != 0)
            {
                list = type_test_join(world, list, count != 0 ? ", ..." : "...", "");
            }
            else if (valid && count == 0 && (table->types.flags[index] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0)
            {
                list = type_test_join(world, list, "void", "");
            }
            spelling.left = valid ? inner.left : 0;
            spelling.right = valid ? type_test_join(world, list, ")", inner.right) : "";
            spelling.mode = BUSTER_TYPE_TEST_DROP;
            spelling.sugared = spelling.sugared || inner.sugared;
            spelling.spellable = valid && inner.spellable;
        }
        else if ((kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && inner.left != 0)
        {
            // The attribute goes among the declaration specifiers, where it makes the vector of the unqualified
            // element and the qualifiers before it qualify the vector: a vector of qualified elements needs its own
            // typedef, as does any vector of a qualified element.
            const uint64_t bytes = table->types.extent[index] * type_size(table, base);
            if (kind == BUSTER_TYPE_KIND_VECTOR)
            {
                snprintf(buffer, sizeof(buffer), "__attribute__((vector_size(%" PRIu64 "))) ", bytes);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "__attribute__((ext_vector_type(%" PRIu64 "))) ", table->types.extent[index]);
            }
            if (type_qualifiers(base) != 0)
            {
                snprintf(buffer, sizeof(buffer), "Tx_%u ", index);
                spelling.left = type_test_join(world, buffer, "", "");
                spelling.sugared = true;
            }
            else
            {
                spelling.left = type_test_join(world, inner.left, buffer, "");
                spelling.sugared = inner.sugared;
            }
            spelling.spellable = inner.spellable;
        }
        else if (kind == BUSTER_TYPE_KIND_ATOMIC && inner.left != 0)
        {
            const char* name = type_test_type_name(world, base);
            spelling.left = name != 0 ? type_test_join(world, "_Atomic(", name, ") ") : 0;
            spelling.sugared = inner.sugared;
            spelling.spellable = inner.spellable;
        }
        else if ((kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_COMPLEX) && inner.left != 0)
        {
            // Only a typedef spells these: its own, Tx_<index>.
            snprintf(buffer, sizeof(buffer), "Tx_%u ", index);
            spelling.left = type_test_join(world, buffer, "", "");
            spelling.mode = kind == BUSTER_TYPE_KIND_ALIGNED ? world->spellings[type_index(base)].mode : BUSTER_TYPE_TEST_PREPEND;
            spelling.sugared = true;
            spelling.spellable = inner.spellable;
        }
        else
        {
            spelling.spellable = false;
        }
        if (spelling.left != 0 && strlen(spelling.left) + strlen(spelling.right) > BUSTER_TYPE_TEST_SPELLING_LIMIT / 2)
        {
            spelling.spellable = false;
        }
        spelling.spellable = spelling.spellable && spelling.left != 0;
        world->spellings[index] = spelling;
    }
}

// The expectations: Ty_<number> must print as expected[number]. kinds[number] says what it checks, for reports.
typedef struct TypeTestOracle TypeTestOracle;
struct TypeTestOracle
{
    TypeTestText* text;
    char** expected;
    const char** what;
    uint32_t count;
    uint32_t capacity;
    uint64_t asserts;
};

// A typedef the oracle compares: "typedef <left> Ty_<n> <right> <suffix>;", printed by clang as type_print prints
// `type`.
static void type_test_oracle_typedef(TypeTestWorld* world, TypeTestOracle* oracle, const char* left, const char* right, const char* suffix, TypeId type, const char* what)
{
    if (oracle->count < oracle->capacity)
    {
        const uint32_t number = oracle->count;
        const uint64_t length = world->arena.length;
        const String8 printed = type_print(&world->test.table, &world->arena, type, (String8){ 0 }, &world->names, 0);
        char* expected = arena_push(&world->arena, printed.length + 1, 1);
        if (expected != 0)
        {
            memcpy(expected, printed.pointer, printed.length);
            expected[printed.length] = 0;
            oracle->expected[number] = expected;
            oracle->what[number] = what;
            oracle->count += 1;
            type_test_format(oracle->text, "typedef %sTy_%u%s%s;\n", left, number, right, suffix);
        }
        else
        {
            arena_pop_to(&world->arena, length);
        }
    }
}

// A struct's, union's or enum's definition, at its completion; an anonymous record's members are written inside its
// parent's. The anonymous records' definitions are inline, one level deep.
// A member's packed and aligned attributes, each between `before` and `after`.
static void type_test_write_member_attributes(const TypeTable* table, TypeTestText* text, uint32_t member, const char* before, const char* after)
{
    if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_PACKED) != 0)
    {
        type_test_format(text, "%s__attribute__((packed))%s", before, after);
    }
    if (table->members.alignment_log2[member] != 0)
    {
        type_test_format(text, "%s__attribute__((aligned(%u)))%s", before, 1u << (table->members.alignment_log2[member] - 1), after);
    }
}

// A member's declaration after `indent`, its attributes after its declarator (a bit-field's after its width).
static void type_test_write_member(TypeTestWorld* world, TypeTestText* text, uint32_t member, const char* indent)
{
    const TypeTable* table = &world->test.table;
    const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
    const TypeTestSpelling spelling = type_test_qualified(world, table->members.type[member]);
    type_test_format(text, "%s", indent);
    type_test_format(text, "%s", spelling.left);
    if (table->members.name[member] != 0)
    {
        type_test_format(text, "x%u", table->members.name[member]);
    }
    type_test_format(text, "%s", spelling.right);
    if (bit_field)
    {
        type_test_format(text, " : %u", table->members.bit_width[member]);
    }
    type_test_write_member_attributes(table, text, member, " ", "");
    type_test_format(text, ";\n");
}

static void type_test_write_members(TypeTestWorld* world, TypeTestText* text, uint32_t tag)
{
    const TypeTable* table = &world->test.table;
    for (uint32_t m = 0; m < table->tags.member_count[tag]; m += 1)
    {
        const uint32_t member = table->tags.member_begin[tag] + m;
        const TypeId type = table->members.type[member];
        const uint32_t name = table->members.name[member];
        const uint32_t inner = type_kind(table, type) == BUSTER_TYPE_KIND_STRUCT || type_kind(table, type) == BUSTER_TYPE_KIND_UNION ? table->types.index[type_index(type)] : 0;
        if (name == 0 && inner != 0 && world->anonymous[inner] != 0 && (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0)
        {
            type_test_format(text, "    %s", type_kind(table, type) == BUSTER_TYPE_KIND_STRUCT ? "struct" : "union");
            type_test_format(text, "%s", table->tags.packed[inner] != 0 ? " __attribute__((packed))" : "");
            if (table->tags.alignment_log2[inner] != 0)
            {
                type_test_format(text, " __attribute__((aligned(%u)))", 1u << (table->tags.alignment_log2[inner] - 1));
            }
            type_test_format(text, " {\n");
            for (uint32_t i = 0; i < table->tags.member_count[inner]; i += 1)
            {
                type_test_write_member(world, text, table->tags.member_begin[inner] + i, "        ");
            }
            type_test_format(text, "    };\n");
        }
        else
        {
            type_test_write_member(world, text, member, "    ");
        }
    }
}

static void type_test_write_tag(TypeTestWorld* world, TypeTestText* text, uint32_t tag)
{
    const TypeTable* table = &world->test.table;
    const TypeId type = table->tags.type[tag];
    const uint8_t kind = type_kind(table, type);
    if (kind == BUSTER_TYPE_KIND_ENUM)
    {
        type_test_format(text, "enum%s", world->enum_fixed[tag] == 2 ? " __attribute__((packed))" : "");
        if (table->tags.alignment_log2[tag] != 0)
        {
            type_test_format(text, " __attribute__((aligned(%u)))", 1u << (table->tags.alignment_log2[tag] - 1));
        }
        type_test_format(text, " x%u", table->tags.name[tag]);
        if (world->enum_fixed[tag] == 1)
        {
            type_test_format(text, " : %s", type_test_type_name(world, table->types.base[type_index(type)]));
        }
        type_test_format(text, " {");
        for (uint32_t i = 0; i < world->enum_count[tag]; i += 1)
        {
            const int64_t value = world->enum_values[world->enum_begin[tag] + i];
            type_test_format(text, "%s x%u_%u = ", i != 0 ? "," : "", table->tags.name[tag], i);
            if (world->enum_unsigned[world->enum_begin[tag] + i] != 0)
            {
                type_test_format(text, "%" PRIu64 "ull", (uint64_t)value);
            }
            else if (value == INT64_MIN)
            {
                type_test_format(text, "(-9223372036854775807ll - 1)");
            }
            else
            {
                type_test_format(text, "%" PRId64 "ll", value);
            }
        }
        type_test_format(text, " };\n");
    }
    else if (world->anonymous[tag] == 0)
    {
        const uint8_t pack = table->tags.pack_log2[tag];
        if (pack != 0)
        {
            type_test_format(text, "#pragma pack(push, %u)\n", 1u << (pack - 1));
        }
        type_test_format(text, "%s%s", kind == BUSTER_TYPE_KIND_STRUCT ? "struct" : "union", table->tags.packed[tag] != 0 ? " __attribute__((packed))" : "");
        if (table->tags.alignment_log2[tag] != 0)
        {
            type_test_format(text, " __attribute__((aligned(%u)))", 1u << (table->tags.alignment_log2[tag] - 1));
        }
        type_test_format(text, " x%u {\n", table->tags.name[tag]);
        type_test_write_members(world, text, tag);
        type_test_format(text, "};\n");
        if (pack != 0)
        {
            type_test_format(text, "#pragma pack(pop)\n");
        }
    }
}

// Writes the world's types: their typedefs in creation order, each record's definition where it was completed.
static void type_test_write_types(TypeTestWorld* world, TypeTestOracle* oracle)
{
    const TypeTable* table = &world->test.table;
    TypeTestText* text = oracle->text;
    // The builtin complex types C can't spell, from the conversions that make them.
    type_test_format(text, "typedef __typeof__((_Complex int)0 + (__int128)0) Tx_b%u;\n", BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_INT128);
    type_test_format(text, "typedef __typeof__((_Complex int)0 + (unsigned __int128)0) Tx_b%u;\n", BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_UINT128);
    type_test_format(text, "typedef __typeof__((_Complex int)0 + (__bf16)0) Tx_b%u;\n", BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_BF16);
    uint32_t completed = 0;
    for (uint32_t index = 1; index < table->types.count; index += 1)
    {
        while (completed < world->completion_count && world->completed_at[world->completions[completed]] <= index)
        {
            type_test_write_tag(world, text, world->completions[completed]);
            completed += 1;
        }
        TypeTestSpelling* spelling = &world->spellings[index];
        const uint8_t kind = table->types.kind[index];
        const TypeId base = table->types.base[index];
        if (spelling->spellable && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
        {
            type_test_format(text, "%s x%u;\n", kind == BUSTER_TYPE_KIND_STRUCT ? "struct" : "union", table->tags.name[table->types.index[index]]);
        }
        if (!spelling->spellable || kind == BUSTER_TYPE_KIND_ENUM)
        {
            // An enum's typedef follows its definition (C has no forward declaration of an enum of a fixed type).
            spelling->number = UINT32_MAX;
        }
        else if (kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            const TypeTestSpelling inner = world->spellings[type_index(base)];
            char suffix[64];
            snprintf(suffix, sizeof(suffix), " __attribute__((aligned(%u)))", 1u << table->types.extent[index]);
            spelling->number = oracle->count;
            // Tx_<index> is the name other spellings use; the oracle's own typedef checks its type.
            type_test_format(text, "typedef %sTx_%u%s%s;\n", inner.left, index, inner.right, suffix);
            type_test_oracle_typedef(world, oracle, inner.left, inner.right, suffix, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, inner.sugared ? 0 : "an aligned typedef");
        }
        else if (kind == BUSTER_TYPE_KIND_COMPLEX && index >= BUSTER_TYPE_BUILTIN_COUNT)
        {
            const char* element = type_test_type_name(world, base);
            spelling->number = oracle->count;
            type_test_format(text, "typedef __typeof__((_Complex char)0 + (%s)0) Tx_%u;\n", element, index);
            char left[512];
            snprintf(left, sizeof(left), "__typeof__((_Complex char)0 + (%s)0) ", element);
            type_test_oracle_typedef(world, oracle, left, "", "", (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, "a complex _BitInt");
        }
        else if ((kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && spelling->sugared && type_qualifiers(base) != 0)
        {
            // A vector of qualified elements: the attribute after the declarator applies to the qualified element.
            const char* element = type_test_type_name(world, base);
            char suffix[96];
            if (kind == BUSTER_TYPE_KIND_VECTOR)
            {
                snprintf(suffix, sizeof(suffix), " __attribute__((vector_size(%" PRIu64 ")))", table->types.extent[index] * type_size(table, base));
            }
            else
            {
                snprintf(suffix, sizeof(suffix), " __attribute__((ext_vector_type(%" PRIu64 ")))", table->types.extent[index]);
            }
            spelling->number = oracle->count;
            char left[512];
            snprintf(left, sizeof(left), "%s ", element);
            type_test_format(text, "typedef %sTx_%u%s;\n", left, index, suffix);
            type_test_oracle_typedef(world, oracle, left, "", suffix, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, "a vector of qualified elements");
        }
        else
        {
            spelling->number = oracle->count;
            type_test_oracle_typedef(world, oracle, spelling->left, spelling->right, "", (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, spelling->sugared ? 0 : "a type");
        }
    }
    while (completed < world->completion_count)
    {
        type_test_write_tag(world, text, world->completions[completed]);
        completed += 1;
    }
    // The enums' typedefs, after their definitions.
    for (uint32_t index = BUSTER_TYPE_BUILTIN_COUNT; index < table->types.count; index += 1)
    {
        if (table->types.kind[index] == BUSTER_TYPE_KIND_ENUM)
        {
            world->spellings[index].number = oracle->count;
            type_test_oracle_typedef(world, oracle, world->spellings[index].left, "", "", (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, "an enum");
        }
    }
}

// The typedef name of a type that has one, qualified as the TypeId is (a type without one is spelled out).
static char* type_test_name_of(TypeTestWorld* world, TypeId type)
{
    const TypeTestSpelling* spelling = &world->spellings[type_index(type)];
    char* result = 0;
    if (spelling->number != UINT32_MAX && type_index(type) >= BUSTER_TYPE_BUILTIN_COUNT)
    {
        char name[32];
        snprintf(name, sizeof(name), "Ty_%u ", spelling->number);
        TypeTestSpelling named = *spelling;
        named.left = name;
        named.right = "";
        named.mode = spelling->mode == BUSTER_TYPE_TEST_DROP ? BUSTER_TYPE_TEST_DROP : BUSTER_TYPE_TEST_PREPEND;
        const uint32_t qualifiers = type_qualifiers(type);
        char words[32] = "";
        if (named.mode != BUSTER_TYPE_TEST_DROP)
        {
            strcat(words, (qualifiers & BUSTER_TYPE_CONST) != 0 ? "const " : "");
            strcat(words, (qualifiers & BUSTER_TYPE_VOLATILE) != 0 ? "volatile " : "");
            strcat(words, (qualifiers & BUSTER_TYPE_RESTRICT) != 0 ? "restrict " : "");
        }
        result = type_test_join(world, words, name, "");
    }
    else
    {
        result = type_test_type_name(world, type);
    }
    return result;
}

// The static checks: every complete type's size and alignment, every member's offset, every enum's underlying type,
// compatibility of random pairs and variants, and the conversions, promotions and composite types as typedefs.
static void type_test_write_checks(TypeTestWorld* world, TypeTestOracle* oracle, uint64_t seed)
{
    TypeTable* table = &world->test.table;
    TypeTestText* text = oracle->text;
    for (uint32_t index = 1; index < table->types.count; index += 1)
    {
        const TypeTestSpelling* spelling = &world->spellings[index];
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t kind = table->types.kind[index];
        const bool sized = type_is_complete(table, type) || kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_FUNCTION;
        if (spelling->spellable && sized && spelling->number != UINT32_MAX)
        {
            type_test_format(text, "_Static_assert(sizeof(Ty_%u) == %" PRIu64 "ull, \"size of Ty_%u\");\n", spelling->number, type_size(table, type), spelling->number);
            type_test_format(text, "_Static_assert(_Alignof(Ty_%u) == %" PRIu64 "ull, \"alignment of Ty_%u\");\n", spelling->number, type_alignment(table, type), spelling->number);
            oracle->asserts += 2;
        }
        if (spelling->spellable && type_is_complete(table, type) && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
        {
            const uint32_t tag = table->types.index[index];
            for (uint32_t m = 0; m < table->tags.member_count[tag]; m += 1)
            {
                const uint32_t member = table->tags.member_begin[tag] + m;
                const uint32_t name = table->members.name[member];
                const TypeId member_type = table->members.type[member];
                const bool inner = name == 0 && (type_kind(table, member_type) == BUSTER_TYPE_KIND_STRUCT || type_kind(table, member_type) == BUSTER_TYPE_KIND_UNION);
                const uint32_t inner_tag = inner ? table->types.index[type_index(member_type)] : 0;
                const uint32_t names = inner ? table->tags.member_count[inner_tag] : 1;
                for (uint32_t k = 0; k < names; k += 1)
                {
                    const uint32_t target = inner ? table->tags.member_begin[inner_tag] + k : member;
                    const uint32_t target_name = table->members.name[target];
                    uint64_t offset = 0;
                    if (target_name != 0 && (table->members.flags[target] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 && type_find_member(table, type, target_name, &offset) == target)
                    {
                        type_test_format(text, "_Static_assert(__builtin_offsetof(%sx%u) == %" PRIu64 "ull, \"offset of x%u\");\n",
                                         type_test_join(world, spelling->left, ", ", ""), target_name, offset / 8, target_name);
                        oracle->asserts += 1;
                    }
                }
            }
        }
        if (kind == BUSTER_TYPE_KIND_ENUM && spelling->number != UINT32_MAX)
        {
            type_test_format(text, "_Static_assert(__builtin_types_compatible_p(Ty_%u, %s), \"underlying type of Ty_%u\");\n", spelling->number,
                             type_test_type_name(world, table->types.base[index]), spelling->number);
            oracle->asserts += 1;
        }
    }
    // Compatibility: random pairs of pool types, and of each with a variant made from its parts: an array of unknown
    // size for one of known size, an enum's underlying type for it, an ALIGNED's base, a function without a prototype
    // for one with, or another array length.
    uint64_t random = seed ^ 0xc0ffeeull;
    for (uint32_t i = 0; i < world->pool.count * 2; i += 1)
    {
        const TypeId a = world->pool.types[type_test_below(&random, world->pool.count)];
        TypeId b = world->pool.types[type_test_below(&random, world->pool.count)];
        const uint8_t kind = type_kind(table, a);
        const uint64_t pick = type_test_below(&random, 4);
        if (pick == 0 && kind == BUSTER_TYPE_KIND_ARRAY)
        {
            b = type_incomplete_array(table, type_element(table, a));
        }
        else if (pick == 0 && kind == BUSTER_TYPE_KIND_ENUM)
        {
            b = table->types.base[type_index(a)];
        }
        else if (pick == 0 && kind == BUSTER_TYPE_KIND_ALIGNED)
        {
            b = type_strip_aligned(table, a);
        }
        else if (pick == 0 && kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            b = type_function(table, type_return(table, a), 0, 0, 0);
        }
        else if (pick == 1 && kind == BUSTER_TYPE_KIND_ARRAY)
        {
            b = type_array(table, type_element(table, a), table->types.extent[type_index(a)] + 1);
        }
        else if (pick == 1 && kind == BUSTER_TYPE_KIND_POINTER)
        {
            const TypeId pointee = type_return(table, a);
            b = type_kind(table, pointee) == BUSTER_TYPE_KIND_ARRAY ? type_pointer(table, type_incomplete_array(table, type_element(table, pointee))) : b;
        }
        table->last_error = TYPE_ERROR_NONE;
        const char* left = b != 0 ? type_test_name_of(world, a) : 0;
        const char* right = b != 0 ? type_test_name_of(world, b) : 0;
        const bool spellable = b != 0 && world->spellings[type_index(a)].spellable && world->spellings[type_index(b)].spellable && left != 0 && right != 0;
        if (spellable)
        {
            const bool compatible = type_compatible(table, a, b) == TYPE_ANSWER_YES;
            type_test_format(text, "_Static_assert(__builtin_types_compatible_p(%s *, %s *) == %d, \"compatibility %u\");\n", left, right, compatible ? 1 : 0, i);
            oracle->asserts += 1;
            const bool plain = !world->spellings[type_index(a)].sugared && !world->spellings[type_index(b)].sugared && type_qualifiers(a) == 0 && type_qualifiers(b) == 0;
            if (compatible && plain && type_kind(table, a) != BUSTER_TYPE_KIND_VOID)
            {
                const TypeId composite = type_composite(table, a, b);
                char typed[BUSTER_TYPE_TEST_SPELLING_LIMIT * 2];
                snprintf(typed, sizeof(typed), "__typeof__(*(1 ? (%s *)0 : (%s *)0)) ", left, right);
                if (composite != 0)
                {
                    type_test_oracle_typedef(world, oracle, typed, "", "", composite, "a composite type");
                }
            }
        }
    }
    // The usual arithmetic conversions and the promotions, on every pair of a set of arithmetic types spelled out.
    TypeId arithmetic[96];
    uint32_t count = 0;
    for (uint32_t code = BUSTER_TYPE_CODE_BOOL; code < BUSTER_TYPE_BUILTIN_COUNT && count < 64; code += 1)
    {
        if (type_is_valid(table, BUSTER_TYPE_BUILTIN(code)) && code != BUSTER_TYPE_CODE_NULLPTR && world->spellings[code].spellable && !world->spellings[code].sugared)
        {
            arithmetic[count] = BUSTER_TYPE_BUILTIN(code);
            count += 1;
        }
    }
    for (uint32_t i = 0; i < world->pool.count && count < BUSTER_ARRAY_LENGTH(arithmetic); i += 1)
    {
        const TypeId type = world->pool.types[i];
        const uint8_t kind = type_kind(table, type);
        if ((kind == BUSTER_TYPE_KIND_BIT_INT || kind == BUSTER_TYPE_KIND_ENUM) && type_qualifiers(type) == 0 && type_test_below(&random, 2) == 0)
        {
            arithmetic[count] = type;
            count += 1;
        }
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        const char* a = type_test_type_name(world, arithmetic[i]);
        char typed[BUSTER_TYPE_TEST_SPELLING_LIMIT];
        snprintf(typed, sizeof(typed), "__typeof__(+(%s)0) ", a);
        type_test_oracle_typedef(world, oracle, typed, "", "", type_promote(table, arithmetic[i]), "a promotion");
        for (uint32_t j = 0; j < count; j += 1)
        {
            const TypeId result = type_usual_arithmetic(table, arithmetic[i], arithmetic[j]);
            if (result != 0)
            {
                snprintf(typed, sizeof(typed), "__typeof__((%s)0 + (%s)0) ", a, type_test_type_name(world, arithmetic[j]));
                type_test_oracle_typedef(world, oracle, typed, "", "", result, "a usual arithmetic conversion");
            }
        }
    }
    // Bit-field promotions: a bit-field of each integer type of the set at several widths.
    const uint32_t widths[] = {
        1,
        7,
        8,
        15,
        16,
        31,
        32,
        33,
        63,
        64,
        100,
    };
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t width = type_integer_width(table, arithmetic[i]);
        const char* name = type_test_type_name(world, arithmetic[i]);
        if (width != 0)
        {
            type_test_format(text, "struct bit_field_%u {", i);
            for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths); w += 1)
            {
                if (widths[w] <= width)
                {
                    type_test_format(text, " %sm%u : %u;", name, w, widths[w]);
                }
            }
            type_test_format(text, " };\n");
            for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths); w += 1)
            {
                if (widths[w] <= width)
                {
                    char typed[BUSTER_TYPE_TEST_SPELLING_LIMIT];
                    snprintf(typed, sizeof(typed), "__typeof__(+((struct bit_field_%u *)0)->m%u) ", i, w);
                    type_test_oracle_typedef(world, oracle, typed, "", "", type_promote_bit_field(table, arithmetic[i], widths[w]), "a bit-field promotion");
                }
            }
        }
    }
    // Qualified pool types, spelled out with their qualifiers.
    for (uint32_t i = 0; i < world->pool.count; i += 1)
    {
        const TypeId type = world->pool.types[i];
        const TypeTestSpelling spelling = type_test_qualified(world, type);
        if (type_qualifiers(type) != 0 && world->spellings[type_index(type)].spellable && !world->spellings[type_index(type)].sugared)
        {
            type_test_oracle_typedef(world, oracle, spelling.left, spelling.right, "", type, "a qualified type");
        }
    }
}

// main(): each bit-field of each record set to all ones in a zeroed object, then its bits compared with the layout's.
static void type_test_write_bit_field_checks(TypeTestWorld* world, TypeTestText* text, uint64_t* bit_fields)
{
    const TypeTable* table = &world->test.table;
    type_test_format(text, "int printf(const char*, ...);\nvoid* memset(void*, int, unsigned long);\n");
    type_test_format(text, "static int check(const void* object, unsigned long size, unsigned long offset, unsigned long width, int id)\n{\n");
    type_test_format(text, "    const unsigned char* bytes = object;\n    int failures = 0;\n");
    type_test_format(text, "    for (unsigned long i = 0; i < size * 8; i += 1)\n    {\n");
    type_test_format(text, "        const int set = bytes[i / 8] >> (i %% 8) & 1;\n        const int expected = i >= offset && i < offset + width;\n");
    type_test_format(text, "        failures += set != expected;\n    }\n");
    type_test_format(text, "    if (failures != 0)\n    {\n        printf(\"bit-field %%d: %%d bits differ\\n\", id, failures);\n    }\n");
    type_test_format(text, "    return failures != 0;\n}\n");
    type_test_format(text, "int main(void)\n{\n    int failures = 0;\n");
    for (uint32_t index = BUSTER_TYPE_BUILTIN_COUNT; index < table->types.count; index += 1)
    {
        const uint8_t kind = table->types.kind[index];
        const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
        const TypeTestSpelling* spelling = &world->spellings[index];
        if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && spelling->spellable && type_is_complete(table, type) && type_size(table, type) < (1u << 20))
        {
            const uint32_t tag = table->types.index[index];
            for (uint32_t m = 0; m < table->tags.member_count[tag]; m += 1)
            {
                const uint32_t member = table->tags.member_begin[tag] + m;
                const TypeId member_type = table->members.type[member];
                const bool inner = table->members.name[member] == 0 && (type_kind(table, member_type) == BUSTER_TYPE_KIND_STRUCT || type_kind(table, member_type) == BUSTER_TYPE_KIND_UNION);
                const uint32_t inner_tag = inner ? table->types.index[type_index(member_type)] : 0;
                const uint32_t names = inner ? table->tags.member_count[inner_tag] : 1;
                for (uint32_t k = 0; k < names; k += 1)
                {
                    const uint32_t target = inner ? table->tags.member_begin[inner_tag] + k : member;
                    const uint32_t name = table->members.name[target];
                    uint64_t offset = 0;
                    if (name != 0 && (table->members.flags[target] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 && table->members.bit_width[target] != 0 &&
                        type_find_member(table, type, name, &offset) == target)
                    {
                        type_test_format(text, "    {\n        static %sobject;\n        memset(&object, 0, sizeof object);\n", spelling->left);
                        type_test_format(text, "        object.x%u = -1;\n", name);
                        type_test_format(text, "        failures += check(&object, sizeof object, %" PRIu64 "ul, %uul, %u);\n    }\n", offset, table->members.bit_width[target], target);
                        *bit_fields += 1;
                    }
                }
            }
        }
    }
    type_test_format(text, "    return failures != 0;\n}\n");
}

// Runs `arguments` with stdout and stderr in files, for up to BUSTER_TYPE_TEST_RUN_SECONDS: its exit status, or -1 if
// it couldn't start, was killed or ran out of time.
static int type_test_run(char* const* arguments, const char* output_path, const char* error_path)
{
    int result = -1;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) == 0)
    {
        pid_t child = 0;
        const bool ready = posix_spawn_file_actions_addopen(&actions, 1, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0 &&
                           posix_spawn_file_actions_addopen(&actions, 2, error_path, O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0;
        if (ready && posix_spawnp(&child, arguments[0], &actions, 0, arguments, environ) == 0)
        {
            int status = 0;
            pid_t waited = 0;
            const struct timespec pause = {
                .tv_sec = 0,
                .tv_nsec = 1000000,
            };
            for (uint64_t milliseconds = 0; waited == 0 && milliseconds < 1000ull * BUSTER_TYPE_TEST_RUN_SECONDS; milliseconds += 1)
            {
                waited = waitpid(child, &status, WNOHANG);
                if (waited == 0)
                {
                    nanosleep(&pause, 0);
                }
            }
            if (waited == 0)
            {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            else if (waited == child && WIFEXITED(status))
            {
                result = WEXITSTATUS(status);
            }
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    return result;
}

// A file's bytes, NUL-terminated, in `arena`; empty if it can't be read.
static String8 type_test_read_file(Arena* arena, const char* path)
{
    String8 result = { 0 };
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat stats;
    if (fd != -1 && fstat(fd, &stats) == 0)
    {
        char* bytes = arena_push(arena, (uint64_t)stats.st_size + 1, 1);
        uint64_t length = 0;
        bool reading = bytes != 0;
        while (reading && length < (uint64_t)stats.st_size)
        {
            const ssize_t got = read(fd, bytes + length, (uint64_t)stats.st_size - length);
            reading = got > 0;
            length += got > 0 ? (uint64_t)got : 0;
        }
        if (bytes != 0)
        {
            bytes[length] = 0;
            result = (String8){
                .pointer = bytes,
                .length = length,
            };
        }
    }
    if (fd != -1)
    {
        close(fd);
    }
    return result;
}

static bool type_test_write_file(const char* path, const TypeTestText* text)
{
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    bool result = fd != -1 && text->valid;
    uint64_t written = 0;
    while (result && written < text->length)
    {
        const ssize_t put = write(fd, text->bytes + written, text->length - written);
        result = put > 0;
        written += put > 0 ? (uint64_t)put : 0;
    }
    if (fd != -1)
    {
        close(fd);
    }
    return result;
}

// Whether `needle` occurs in the `length` bytes at `line`.
static bool type_test_contains(const char* line, uint64_t length, const char* needle)
{
    const uint64_t size = strlen(needle);
    bool result = false;
    for (uint64_t i = 0; !result && i + size <= length; i += 1)
    {
        result = memcmp(line + i, needle, size) == 0;
    }
    return result;
}

// Where `needle` first occurs in the `length` bytes at `line`, or 0.
static const char* type_test_find(const char* line, uint64_t length, const char* needle)
{
    const uint64_t size = strlen(needle);
    const char* result = 0;
    for (uint64_t i = 0; result == 0 && i + size <= length; i += 1)
    {
        result = memcmp(line + i, needle, size) == 0 ? line + i : 0;
    }
    return result;
}

// Reports clang's diagnostics: every error (a failed _Static_assert names its check) is a failure.
static void type_test_clang_errors(TypeTestTally* tally, String8 errors, const char* check)
{
    const char* line = errors.pointer;
    while (line != 0 && *line != 0)
    {
        const char* end = strchr(line, '\n');
        const uint64_t length = end != 0 ? (uint64_t)(end - line) : strlen(line);
        if (type_test_contains(line, length, ": error: "))
        {
            char detail[512];
            snprintf(detail, sizeof(detail), "%.*s", (int)(length < 500 ? length : 500), line);
            type_test_expect(tally, false, check, detail);
        }
        line = end != 0 ? end + 1 : 0;
    }
}

// Compares the dump's TypedefDecl lines of the oracle's typedefs with their expected spellings: the last quoted type
// on each, which is the desugared one when clang shows both.
static void type_test_compare_dump(TypeTestTally* tally, const TypeTestOracle* oracle, String8 dump, uint64_t* compared)
{
    const char* line = dump.pointer;
    while (line != 0 && *line != 0)
    {
        const char* end = strchr(line, '\n');
        const uint64_t length = end != 0 ? (uint64_t)(end - line) : strlen(line);
        const char* name = type_test_find(line, length, " Ty_");
        if (type_test_contains(line, length, "TypedefDecl") && name != 0 && name[4] >= '0' && name[4] <= '9')
        {
            const uint32_t number = (uint32_t)strtoul(name + 4, 0, 10);
            const char* last_quote = 0;
            const char* before = 0;
            for (const char* c = line; c < line + length; c += 1)
            {
                if (*c == '\'')
                {
                    before = last_quote;
                    last_quote = c;
                }
            }
            if (number < oracle->count && before != 0 && oracle->what[number] != 0)
            {
                const uint64_t printed_length = (uint64_t)(last_quote - before - 1);
                const bool same = printed_length == strlen(oracle->expected[number]) && memcmp(before + 1, oracle->expected[number], printed_length) == 0;
                char detail[BUSTER_TYPE_TEST_SPELLING_LIMIT];
                snprintf(detail, sizeof(detail), "Ty_%u (%s): clang \"%.*s\", type_print \"%s\"", number, oracle->what[number], (int)printed_length, before + 1,
                         oracle->expected[number]);
                type_test_expect(tally, same, "clang printing", detail);
                *compared += 1;
            }
        }
        line = end != 0 ? end + 1 : 0;
    }
}

static bool type_test_clang(uint64_t seed, uint32_t count, const char* clang, bool keep)
{
    TypeTestTally tally = { 0 };
    char directory[BUSTER_TYPE_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-types-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    TypeTestWorld world = { 0 };
    world.arena = arena_reserve(BUSTER_TYPE_TEST_ARENA_BYTES);
    Arena text_arena = arena_reserve(BUSTER_TYPE_TEST_ARENA_BYTES);
    const uint32_t capacity = count * 4 + 4096;
    world.pool = (TypeTestPool){
        .types = BUSTER_ARENA_PUSH_ARRAY(&world.arena, TypeId, capacity),
        .capacity = capacity,
    };
    world.anonymous = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint8_t, capacity);
    world.completed_at = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint32_t, capacity);
    world.enum_begin = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint32_t, capacity);
    world.enum_count = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint32_t, capacity);
    world.enum_fixed = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint8_t, capacity);
    world.enum_values = BUSTER_ARENA_PUSH_ARRAY(&world.arena, int64_t, capacity);
    world.enum_unsigned = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint8_t, capacity);
    world.completions = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint32_t, capacity);
    world.spellings = BUSTER_ARENA_PUSH_ARRAY(&world.arena, TypeTestSpelling, capacity);
    world.name_bytes = BUSTER_ARENA_PUSH_ARRAY(&world.arena, const uint8_t*, capacity);
    world.name_lengths = BUSTER_ARENA_PUSH_ARRAY(&world.arena, uint32_t, capacity);
    TypeTestOracle oracle = {
        .expected = BUSTER_ARENA_PUSH_ARRAY(&world.arena, char*, capacity * 4),
        .what = BUSTER_ARENA_PUSH_ARRAY(&world.arena, const char*, capacity * 4),
        .capacity = capacity * 4,
    };
    TypeTestText text = {
        .arena = &text_arena,
        .valid = true,
    };
    oracle.text = &text;
    const bool memory = world.pool.types != 0 && world.anonymous != 0 && world.completed_at != 0 && world.enum_begin != 0 && world.enum_count != 0 && world.enum_fixed != 0 &&
                        world.enum_values != 0 && world.enum_unsigned != 0 && world.completions != 0 && world.spellings != 0 && world.name_bytes != 0 &&
                        world.name_lengths != 0 && oracle.expected != 0 && oracle.what != 0;
    if (!type_test_expect(&tally, memory && type_test_table_open(&world.test, capacity - 64), "clang", "no memory"))
    {
    }
    else if (!type_test_expect(&tally, mkdtemp(directory) != 0, "clang", "can't make the directory"))
    {
    }
    else
    {
        memset(world.anonymous, 0, capacity);
        memset(world.enum_fixed, 0, capacity);
        memset(world.spellings, 0, capacity * sizeof(TypeTestSpelling));
        for (uint32_t i = 0; i < capacity; i += 1)
        {
            world.completed_at[i] = UINT32_MAX;
            char name[16];
            const int length = snprintf(name, sizeof(name), "x%u", i);
            world.name_bytes[i] = (const uint8_t*)type_test_join(&world, name, "", "");
            world.name_lengths[i] = (uint32_t)length;
        }
        world.names = (TypeNames){
            .bytes = world.name_bytes,
            .lengths = world.name_lengths,
            .count = capacity,
        };
        type_test_expect(&tally, type_test_world_build(&world, seed, count), "clang", "the world didn't fit in its table");
        type_test_spell_world(&world);
        type_test_write_types(&world, &oracle);
        type_test_write_checks(&world, &oracle, seed);
        uint64_t bit_fields = 0;
        type_test_write_bit_field_checks(&world, &text, &bit_fields);
        char source[BUSTER_TYPE_TEST_PATH_BYTES];
        char program[BUSTER_TYPE_TEST_PATH_BYTES];
        char output[BUSTER_TYPE_TEST_PATH_BYTES];
        char errors[BUSTER_TYPE_TEST_PATH_BYTES];
        char dump[BUSTER_TYPE_TEST_PATH_BYTES];
        snprintf(source, sizeof(source), "%s/types.c", directory);
        snprintf(program, sizeof(program), "%s/types", directory);
        snprintf(output, sizeof(output), "%s/output.txt", directory);
        snprintf(errors, sizeof(errors), "%s/errors.txt", directory);
        snprintf(dump, sizeof(dump), "%s/dump.txt", directory);
        if (type_test_expect(&tally, type_test_write_file(source, &text), "clang", "can't write the C file"))
        {
            char* compile[] = {
                (char*)clang,
                "-std=gnu17",
                "-Wno-everything",
                "-ferror-limit=0",
                "-o",
                program,
                source,
                0,
            };
            const int compiled = type_test_run(compile, output, errors);
            type_test_clang_errors(&tally, type_test_read_file(&world.arena, errors), "clang _Static_asserts");
            type_test_expect(&tally, compiled == 0, "clang", "clang didn't compile the C file (is clang 23 on PATH, or --clang given?)");
            if (compiled == 0)
            {
                char* run[] = {
                    program,
                    0,
                };
                const int ran = type_test_run(run, output, errors);
                const String8 printed = type_test_read_file(&world.arena, output);
                type_test_expect(&tally, ran == 0, "clang bit-fields", printed.length != 0 ? printed.pointer : "the program failed");
            }
            char* dumping[] = {
                (char*)clang,
                "-std=gnu17",
                "-Wno-everything",
                "-fsyntax-only",
                "-Xclang",
                "-ast-dump",
                "-Xclang",
                "-ast-dump-filter=Ty_",
                source,
                0,
            };
            const int dumped = type_test_run(dumping, dump, errors);
            type_test_expect(&tally, dumped == 0, "clang", "clang -ast-dump failed");
            uint64_t compared = 0;
            type_test_compare_dump(&tally, &oracle, type_test_read_file(&world.arena, dump), &compared);
            uint32_t printed = 0;
            for (uint32_t i = 0; i < oracle.count; i += 1)
            {
                printed += oracle.what[i] != 0 ? 1 : 0;
            }
            type_test_expect(&tally, compared == printed, "clang printing", "a typedef is missing from the dump");
            printf("%-34s %10u types, %" PRIu64 " _Static_asserts, %" PRIu64 " bit-fields, %" PRIu64 " spellings compared\n", "clang", world.test.table.types.count,
                   oracle.asserts, bit_fields, compared);
        }
        if (keep)
        {
            printf("the files are in %s\n", directory);
        }
        else
        {
            unlink(source);
            unlink(program);
            unlink(output);
            unlink(errors);
            unlink(dump);
            rmdir(directory);
        }
        type_test_table_close(&world.test);
    }
    arena_release(&world.arena);
    arena_release(&text_arena);
    type_test_report("clang", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

typedef struct TypeTestMachineMode TypeTestMachineMode;
struct TypeTestMachineMode
{
    TypeId base;
    const char* mode;
    TypeId result;
    TypeError error;
    const char* base_text;
    const char* result_text;
    // Clang accepts a few mode types the table deliberately does not represent yet.
    bool clang_rejects;
};

// Original mode fixtures. Every accepted request also becomes C with pointer compatibility checks (which retain
// qualifiers), size/alignment checks, and a surrounding record's size/alignment/member offset. Rejected source
// cases run separately so that one expected error cannot hide another case's accidental acceptance.
static bool type_test_machine_modes(const char* clang, bool keep)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test = { 0 };
    Arena text_arena = arena_reserve(1ull << 20);
    Arena read_arena = arena_reserve(1ull << 20);
    char directory[BUSTER_TYPE_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-modes-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const bool ready = type_test_table_open(&test, 256) && text_arena.base != 0 && read_arena.base != 0;
    if (type_test_expect(&tally, ready && mkdtemp(directory) != 0, "machine modes", "can't initialize the test"))
    {
        TypeTable* table = &test.table;
        const TypeId signed_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
        const TypeId unsigned_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        const TypeId incomplete_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 3);
        type_enum_complete(table, signed_enum, BUSTER_TYPE_INT, 0);
        type_enum_complete(table, unsigned_enum, BUSTER_TYPE_UINT, 0);
        const TypeId int_vector = type_vector(table, BUSTER_TYPE_INT, 4, false);
        const TypeId char_vector = type_vector(table, BUSTER_TYPE_CHAR, 4, false);
        const TypeId short_vector = type_vector(table, BUSTER_TYPE_SHORT, 8, false);
        const TypeId float_vector = type_vector(table, BUSTER_TYPE_FLOAT, 4, false);
        const TypeId int_ext = type_vector(table, BUSTER_TYPE_INT, 3, true);
        const TypeId long_vector = type_vector(table, BUSTER_TYPE_LONG, 1, false);
        const TypeId bool_ext = type_vector(table, BUSTER_TYPE_BOOL, 16, true);
        const TypeId ushort_vector = type_vector(table, BUSTER_TYPE_USHORT, 8, false);
        const TypeTestMachineMode fixtures[] =
        {
#define BUSTER_TYPE_MODE_TEST(base_, mode_, result_, error_, base_text_, result_text_, rejects_) \
            { \
                .base = (base_), \
                .mode = (mode_), \
                .result = (result_), \
                .error = (error_), \
                .base_text = (base_text_), \
                .result_text = (result_text_), \
                .clang_rejects = (rejects_), \
            }
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "QI", BUSTER_TYPE_SCHAR, TYPE_ERROR_NONE, "int", "signed char", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_UINT, "__QI__", BUSTER_TYPE_UCHAR, TYPE_ERROR_NONE, "unsigned int", "unsigned char", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_CHAR, "HI", BUSTER_TYPE_SHORT, TYPE_ERROR_NONE, "char", "short", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_UCHAR, "HI", BUSTER_TYPE_USHORT, TYPE_ERROR_NONE, "unsigned char", "unsigned short", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_LONG, "SI", BUSTER_TYPE_INT, TYPE_ERROR_NONE, "long", "int", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_ULONG, "SI", BUSTER_TYPE_UINT, TYPE_ERROR_NONE, "unsigned long", "unsigned int", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_LLONG, "DI", BUSTER_TYPE_LONG, TYPE_ERROR_NONE, "long long", "long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_ULLONG, "DI", BUSTER_TYPE_ULONG, TYPE_ERROR_NONE, "unsigned long long", "unsigned long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "TI", BUSTER_TYPE_INT128, TYPE_ERROR_NONE, "int", "__int128", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_UINT, "TI", BUSTER_TYPE_UINT128, TYPE_ERROR_NONE, "unsigned int", "unsigned __int128", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_BOOL, "word", BUSTER_TYPE_ULONG, TYPE_ERROR_NONE, "_Bool", "unsigned long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "__byte__", BUSTER_TYPE_SCHAR, TYPE_ERROR_NONE, "int", "signed char", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "__word__", BUSTER_TYPE_LONG, TYPE_ERROR_NONE, "int", "long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_UINT, "pointer", BUSTER_TYPE_ULONG, TYPE_ERROR_NONE, "unsigned int", "unsigned long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "unwind_word", BUSTER_TYPE_LONG, TYPE_ERROR_NONE, "int", "long", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "HF", BUSTER_TYPE_HALF, TYPE_ERROR_NONE, "float", "__fp16", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_DOUBLE, "SF", BUSTER_TYPE_FLOAT, TYPE_ERROR_NONE, "double", "float", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "DF", BUSTER_TYPE_DOUBLE, TYPE_ERROR_NONE, "float", "double", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "XF", BUSTER_TYPE_LONG_DOUBLE, TYPE_ERROR_NONE, "float", "long double", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "TF", BUSTER_TYPE_FLOAT128, TYPE_ERROR_NONE, "float", "__float128", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "__KF__", BUSTER_TYPE_FLOAT128, TYPE_ERROR_NONE, "float", "__float128", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_DOUBLE, "SC", BUSTER_TYPE_COMPLEX_FLOAT, TYPE_ERROR_NONE, "_Complex double", "_Complex float", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "DC", BUSTER_TYPE_COMPLEX_DOUBLE, TYPE_ERROR_NONE, "_Complex float", "_Complex double", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "XC", BUSTER_TYPE_COMPLEX_LONG_DOUBLE, TYPE_ERROR_NONE, "_Complex float", "_Complex long double", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "TC", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT128), TYPE_ERROR_NONE, "_Complex float", "__typeof__((__float128)0 + (_Complex double)0)", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "KC", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT128), TYPE_ERROR_NONE, "_Complex float", "__typeof__((__float128)0 + (_Complex double)0)", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "DF", BUSTER_TYPE_DOUBLE, TYPE_ERROR_NONE, "_Complex float", "double", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE, "QI", BUSTER_TYPE_SCHAR, TYPE_ERROR_NONE, "const volatile int", "signed char", false),
            BUSTER_TYPE_MODE_TEST(type_aligned(table, BUSTER_TYPE_INT | BUSTER_TYPE_CONST, 5), "HI", BUSTER_TYPE_SHORT, TYPE_ERROR_NONE, "const int __attribute__((aligned(32)))", "short", false),
            BUSTER_TYPE_MODE_TEST(signed_enum, "QI", BUSTER_TYPE_SCHAR, TYPE_ERROR_NONE, "enum ModeSigned", "signed char", false),
            BUSTER_TYPE_MODE_TEST(unsigned_enum, "QI", BUSTER_TYPE_UCHAR, TYPE_ERROR_NONE, "enum ModeUnsigned", "unsigned char", false),
            BUSTER_TYPE_MODE_TEST(incomplete_enum, "QI", BUSTER_TYPE_UCHAR, TYPE_ERROR_NONE, "enum ModeIncomplete", "unsigned char", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "V4SI", int_vector, TYPE_ERROR_NONE, "int", "int __attribute__((vector_size(16)))", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "__V4SF__", float_vector, TYPE_ERROR_NONE, "float", "float __attribute__((vector_size(16)))", false),
            BUSTER_TYPE_MODE_TEST(int_vector, "HI", short_vector, TYPE_ERROR_NONE, "int __attribute__((vector_size(16)))", "short __attribute__((vector_size(16)))", false),
            BUSTER_TYPE_MODE_TEST(int_ext, "DI", long_vector, TYPE_ERROR_NONE, "int __attribute__((ext_vector_type(3)))", "long __attribute__((vector_size(8)))", false),
            BUSTER_TYPE_MODE_TEST(bool_ext, "HI", ushort_vector, TYPE_ERROR_NONE, "_Bool __attribute__((ext_vector_type(16)))", "unsigned short __attribute__((vector_size(16)))", false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "BF", 0, TYPE_ERROR_MODE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "__libgcc_cmp_return__", 0, TYPE_ERROR_MODE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "__QI___", 0, TYPE_ERROR_MODE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "V0SI", 0, TYPE_ERROR_MODE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "V3SI", 0, TYPE_ERROR_MODE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "V18446744073709551616SI", 0, TYPE_ERROR_TOO_LARGE, "int", 0, false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "XI", 0, TYPE_ERROR_MODE_UNSUPPORTED, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "QF", 0, TYPE_ERROR_MODE_UNSUPPORTED, "float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "IF", 0, TYPE_ERROR_MODE_UNSUPPORTED, "float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "IC", 0, TYPE_ERROR_MODE_UNSUPPORTED, "_Complex float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "QI", 0, TYPE_ERROR_MODE_BASE, "float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "DF", 0, TYPE_ERROR_MODE_BASE, "int", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_FLOAT, "DC", 0, TYPE_ERROR_MODE_BASE, "float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "SI", 0, TYPE_ERROR_MODE_BASE, "_Complex float", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT), "SC", 0, TYPE_ERROR_MODE_BASE, "_Complex int", 0, true),
            BUSTER_TYPE_MODE_TEST(type_pointer(table, BUSTER_TYPE_INT), "pointer", 0, TYPE_ERROR_MODE_BASE, "int *", 0, true),
            BUSTER_TYPE_MODE_TEST(type_atomic(table, BUSTER_TYPE_INT), "QI", 0, TYPE_ERROR_MODE_BASE, "_Atomic(int)", 0, true),
            BUSTER_TYPE_MODE_TEST(type_bit_int(table, 25, false), "HI", 0, TYPE_ERROR_MODE_BASE, "_BitInt(25)", 0, true),
            BUSTER_TYPE_MODE_TEST(unsigned_enum, "V4SI", 0, TYPE_ERROR_MODE_BASE, "enum ModeUnsigned", 0, true),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "HC", 0, TYPE_ERROR_MODE_UNSUPPORTED, "_Complex float", 0, false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_COMPLEX_FLOAT, "V2SC", 0, TYPE_ERROR_MODE_UNSUPPORTED, "_Complex float", 0, false),
            BUSTER_TYPE_MODE_TEST(char_vector, "DI", 0, TYPE_ERROR_MODE_UNSUPPORTED, "char __attribute__((vector_size(4)))", 0, false),
            BUSTER_TYPE_MODE_TEST(BUSTER_TYPE_INT, "V4294967296SI", 0, TYPE_ERROR_TOO_LARGE, "int", 0, false),
            BUSTER_TYPE_MODE_TEST(0, "QI", 0, TYPE_ERROR_OPERAND, 0, 0, false),
#undef BUSTER_TYPE_MODE_TEST
        };
        TypeTestText text = {
            .arena = &text_arena,
            .valid = true,
        };
        char* expected[BUSTER_ARRAY_LENGTH(fixtures)] = { 0 };
        const char* what[BUSTER_ARRAY_LENGTH(fixtures)] = { 0 };
        TypeTestOracle oracle = {
            .expected = expected,
            .what = what,
            .count = BUSTER_ARRAY_LENGTH(fixtures),
        };
        uint32_t accepted = 0;
        const char* declarations = "enum ModeSigned { ModeNegative = -1 };\nenum ModeUnsigned { ModePositive = 1 };\nenum ModeIncomplete;\n";
        type_test_format(&text, "%s", declarations);
        const long page = sysconf(_SC_PAGESIZE);
        uint8_t* guarded = page > 0 ? mmap(0, (uint64_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
        const bool guard_ready = guarded != MAP_FAILED && mprotect(guarded + page, (uint64_t)page, PROT_NONE) == 0;
        type_test_expect(&tally, guard_ready, "machine modes", "can't create the guarded name buffer");
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            const TypeTestMachineMode* fixture = &fixtures[i];
            const uint64_t length = strlen(fixture->mode);
            String8 name = {
                .pointer = (char*)fixture->mode,
                .length = length,
            };
            if (guard_ready)
            {
                name.pointer = (char*)guarded + page - length;
                memcpy(name.pointer, fixture->mode, length);
            }
            const TypeId actual = type_machine_mode(table, fixture->base, name);
            char detail[256];
            snprintf(detail, sizeof(detail), "case %u, %s mode(%s): type %u against %u, error %u against %u", i,
                     fixture->base_text != 0 ? fixture->base_text : "invalid", fixture->mode, actual, fixture->result, table->last_error, fixture->error);
            type_test_expect(&tally, actual == fixture->result && table->last_error == fixture->error, "machine modes", detail);
            if (actual != 0 && fixture->result != 0)
            {
                const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 0);
                const TypeMember members[3] = {
                    type_test_member(1, BUSTER_TYPE_CHAR, BUSTER_TYPE_NOT_BIT_FIELD),
                    type_test_member(2, actual, BUSTER_TYPE_NOT_BIT_FIELD),
                    type_test_member(3, BUSTER_TYPE_CHAR, BUSTER_TYPE_NOT_BIT_FIELD),
                };
                const bool laid_out = type_complete_record(table, record, members, 3, (TypeRecordAttributes){ 0 });
                uint64_t offset = 0;
                const bool found = laid_out && type_find_member(table, record, 2, &offset) != 0;
                type_test_expect(&tally, found, "machine mode layout", fixture->mode);
                type_test_format(&text, "typedef %s Input_%u;\ntypedef Input_%u Output_%u __attribute__((mode(%s)));\ntypedef %s Expected_%u;\n",
                                 fixture->base_text, i, i, i, fixture->mode, fixture->result_text, i);
                type_test_format(&text, "typedef Output_%u Ty_%u;\n", i, i);
                const String8 spelling = type_print(table, &read_arena, actual, (String8){ 0 }, 0, 0);
                expected[i] = arena_push(&read_arena, spelling.length + 1, 1);
                if (type_test_expect(&tally, expected[i] != 0 && spelling.length != 0, "machine modes", "can't retain the canonical type spelling"))
                {
                    memcpy(expected[i], spelling.pointer, spelling.length);
                    expected[i][spelling.length] = 0;
                    what[i] = fixture->mode;
                    accepted += 1;
                }
                type_test_format(&text, "_Static_assert(__builtin_types_compatible_p(Output_%u *, Expected_%u *), \"case %u type\");\n", i, i, i);
                type_test_format(&text, "_Static_assert(sizeof(Output_%u) == %" PRIu64 " && _Alignof(Output_%u) == %" PRIu64 ", \"case %u size and alignment\");\n",
                                 i, type_size(table, actual), i, type_alignment(table, actual), i);
                type_test_format(&text, "struct Record_%u { char a; Output_%u b; char c; };\n", i, i);
                type_test_format(&text, "_Static_assert(sizeof(struct Record_%u) == %" PRIu64 " && _Alignof(struct Record_%u) == %" PRIu64 ", \"case %u record layout\");\n",
                                 i, type_size(table, record), i, type_alignment(table, record), i);
                type_test_format(&text, "_Static_assert(__builtin_offsetof(struct Record_%u, b) == %" PRIu64 ", \"case %u member offset\");\n", i, offset / 8, i);
            }
        }
        type_test_expect(&tally, type_size(table, signed_enum) == 4 && type_size(table, unsigned_enum) == 4 && !type_is_complete(table, incomplete_enum),
                         "machine modes", "an enum typedef's mode changed its tag");
        type_test_expect(&tally, type_machine_mode(table, BUSTER_TYPE_INT, (String8){ 0 }) == 0 && table->last_error == TYPE_ERROR_MODE,
                         "machine modes", "an empty name was accepted");
        type_test_expect(&tally, type_machine_mode(table, BUSTER_TYPE_INT, (String8){ .pointer = 0, .length = UINT64_MAX }) == 0 && table->last_error == TYPE_ERROR_MODE,
                         "machine modes", "a null name was read");
        if (guard_ready)
        {
            type_test_expect(&tally, type_machine_mode(table, BUSTER_TYPE_INT, (String8){ .pointer = (char*)guarded + page, .length = 0 }) == 0,
                             "machine modes", "a zero-length name at a guard page was read");
        }
        if (guarded != MAP_FAILED)
        {
            munmap(guarded, (uint64_t)page * 2);
        }
        TypeTestTable full = { 0 };
        if (type_test_expect(&tally, type_test_table_open(&full, 0), "machine modes", "can't make a table without derived-type capacity"))
        {
            type_test_expect(&tally, type_machine_mode(&full.table, BUSTER_TYPE_INT, BUSTER_S8("V4SI")) == 0 && full.table.last_error == TYPE_ERROR_FULL &&
                             full.table.types.count == BUSTER_TYPE_BUILTIN_COUNT, "machine modes", "a vector mode did not preserve capacity failure");
            type_test_expect(&tally, type_machine_mode(&full.table, BUSTER_TYPE_INT, BUSTER_S8("DI")) == BUSTER_TYPE_LONG && full.table.last_error == TYPE_ERROR_NONE,
                             "machine modes", "a scalar mode allocated a derived type or retained a stale error");
        }
        type_test_table_close(&full);
        char source[BUSTER_TYPE_TEST_PATH_BYTES];
        char output[BUSTER_TYPE_TEST_PATH_BYTES];
        char errors[BUSTER_TYPE_TEST_PATH_BYTES];
        snprintf(source, sizeof(source), "%s/accepted.c", directory);
        snprintf(output, sizeof(output), "%s/output.txt", directory);
        snprintf(errors, sizeof(errors), "%s/errors.txt", directory);
        char* arguments[] = {
            (char*)clang,
            "-std=gnu17",
            "-fsyntax-only",
            "-Wno-everything",
            "-ferror-limit=0",
            source,
            0,
        };
        if (type_test_expect(&tally, type_test_write_file(source, &text), "machine modes clang", "can't write accepted.c"))
        {
            const int status = type_test_run(arguments, output, errors);
            type_test_clang_errors(&tally, type_test_read_file(&read_arena, errors), "machine modes clang");
            type_test_expect(&tally, status == 0, "machine modes clang", "the accepted cases or their layouts differ");
            char* dumping[] = {
                (char*)clang,
                "-std=gnu17",
                "-fsyntax-only",
                "-Wno-everything",
                "-Xclang",
                "-ast-dump",
                "-Xclang",
                "-ast-dump-filter=Ty_",
                source,
                0,
            };
            const int dumped = type_test_run(dumping, output, errors);
            type_test_expect(&tally, dumped == 0, "machine modes clang", "clang's canonical type dump failed");
            uint64_t compared = 0;
            type_test_compare_dump(&tally, &oracle, type_test_read_file(&read_arena, output), &compared);
            type_test_expect(&tally, compared == accepted, "machine modes clang", "the canonical type dump omitted a fixture");
        }
        if (!keep)
        {
            unlink(source);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
        {
            const TypeTestMachineMode* fixture = &fixtures[i];
            if (fixture->clang_rejects)
            {
                arena_pop_to(&text_arena, 0);
                text = (TypeTestText){ .arena = &text_arena, .valid = true };
                type_test_format(&text, "%stypedef %s Input;\ntypedef Input Output __attribute__((mode(%s)));\n", declarations, fixture->base_text, fixture->mode);
                snprintf(source, sizeof(source), "%s/rejected-%u.c", directory, i);
                if (type_test_expect(&tally, type_test_write_file(source, &text), "machine modes clang", "can't write a rejected case"))
                {
                    const int status = type_test_run(arguments, output, errors);
                    const String8 diagnostic = type_test_read_file(&read_arena, errors);
                    type_test_expect(&tally, status > 0 && type_test_contains(diagnostic.pointer, diagnostic.length, ": error:"), "machine modes clang", fixture->mode);
                }
                if (!keep)
                {
                    unlink(source);
                }
            }
        }
        if (keep)
        {
            printf("machine mode files are in %s\n", directory);
        }
        else
        {
            unlink(output);
            unlink(errors);
            rmdir(directory);
        }
    }
    type_test_table_close(&test);
    arena_release(&text_arena);
    arena_release(&read_arena);
    type_test_report("machine modes", tally);
    return tally.failures == 0;
}

static bool type_test_parse_u64(const char* text, uint64_t* value)
{
    char* end = 0;
    errno = 0;
    const unsigned long long parsed = strtoull(text, &end, 0);
    const bool result = text[0] >= '0' && text[0] <= '9' && end != text && *end == 0 && errno == 0;
    if (result)
    {
        *value = (uint64_t)parsed;
    }
    return result;
}

// type_find_member's answer for no member is 0, which no member is: the first member of the first record in a fresh
// table, directly and through an anonymous member (a regression: member 0 was the first record's first).
static bool type_test_member_lookup(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 64), "member lookup", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeMember members[2] = {
            type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(2, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_complete_record(table, record, members, 2, (TypeRecordAttributes){ 0 });
        uint64_t offset = UINT64_MAX;
        const uint32_t first = type_find_member(table, record, 1, &offset);
        type_test_expect(&tally, first != 0 && table->members.name[first] == 1 && offset == 0, "member lookup", "the first member of the first record");
        offset = UINT64_MAX;
        const uint32_t second = type_find_member(table, record, 2, &offset);
        type_test_expect(&tally, first != 0 && second == first + 1 && offset == 32, "member lookup", "the second member");
        type_test_expect(&tally, type_find_member(table, record, 3, &offset) == 0, "member lookup", "a member that doesn't exist");
        type_test_table_close(&test);
    }
    if (type_test_expect(&tally, type_test_table_open(&test, 64), "member lookup", "the table can't be made"))
    {
        // struct { struct { int x; }; int y; }, whose x is the table's first member.
        TypeTable* table = &test.table;
        const TypeId inner = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 0);
        const TypeMember inner_member = type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        type_complete_record(table, inner, &inner_member, 1, (TypeRecordAttributes){ 0 });
        const TypeId outer = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 3);
        const TypeMember outer_members[2] = {
            type_test_member(0, inner, BUSTER_TYPE_NOT_BIT_FIELD),
            type_test_member(2, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        type_complete_record(table, outer, outer_members, 2, (TypeRecordAttributes){ 0 });
        uint64_t offset = UINT64_MAX;
        const uint32_t x = type_find_member(table, outer, 1, &offset);
        type_test_expect(&tally, x != 0 && table->members.name[x] == 1 && offset == 0, "member lookup", "x, the table's first member, through an anonymous member");
        offset = UINT64_MAX;
        type_test_expect(&tally, type_find_member(table, outer, 2, &offset) != 0 && offset == 32, "member lookup", "y, after the anonymous member");
        type_test_table_close(&test);
    }
    type_test_report("member lookup", tally);
    return tally.failures == 0;
}

// Types that share their insides: level n + 1 is a pointer to int (T, T) of level n's T, so a type of n levels is
// n pairs of records but a tree of 2^n leaves. Compatibility, the canonical comparison and the composite walk each
// distinct pair once, so 64 levels take a moment (a regression: they expanded every occurrence of a pair).
#define BUSTER_TYPE_TEST_SHARED_LEVELS 64

static bool type_test_shared_pairs(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 1024), "shared pairs", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        // Level 0: int (*)[] and int (*)[3], compatible; a pointer to an 8-byte aligned int and to an int, which are
        // one canonical type.
        TypeId levels[4] = {
            type_pointer(table, type_incomplete_array(table, BUSTER_TYPE_INT)),
            type_pointer(table, type_array(table, BUSTER_TYPE_INT, 3)),
            type_pointer(table, type_aligned(table, BUSTER_TYPE_INT, 3)),
            type_pointer(table, BUSTER_TYPE_INT),
        };
        for (uint32_t level = 0; level < BUSTER_TYPE_TEST_SHARED_LEVELS; level += 1)
        {
            for (uint32_t chain = 0; chain < 4; chain += 1)
            {
                const TypeId parameters[2] = {
                    levels[chain],
                    levels[chain],
                };
                levels[chain] = type_pointer(table, type_function(table, BUSTER_TYPE_INT, parameters, 2, BUSTER_TYPE_FLAG_PROTOTYPE));
            }
        }
        type_test_expect(&tally, levels[0] != 0 && levels[1] != 0 && levels[2] != 0 && levels[3] != 0, "shared pairs", "the types can't be made");
        type_test_expect(&tally, type_compatible(table, levels[0], levels[1]) == TYPE_ANSWER_YES, "shared pairs", "int (*)[] and int (*)[3] at the bottom aren't compatible");
        type_test_expect(&tally, type_composite(table, levels[0], levels[1]) == levels[1], "shared pairs", "the composite isn't the one with int (*)[3]");
        type_test_expect(&tally, type_same_canonical(table, levels[2], levels[3]) == TYPE_ANSWER_YES, "shared pairs", "an aligned int at the bottom isn't looked through");
        type_test_expect(&tally, type_compatible(table, levels[2], levels[3]) == TYPE_ANSWER_YES, "shared pairs", "an aligned int at the bottom isn't compatible with int");
        type_test_expect(&tally, type_compatible(table, levels[0], levels[3]) == TYPE_ANSWER_NO, "shared pairs", "int (*)[] at the bottom is compatible with int *");
        type_test_expect(&tally, type_composite(table, levels[0], levels[3]) == 0, "shared pairs", "int (*)[] and int * at the bottom have a composite");
        type_test_table_close(&test);
    }
    type_test_report("shared pairs", tally);
    return tally.failures == 0;
}

// A composite of two chains of pointers 65,536 deep that differ only at the bottom, over arrays of an aligned int: its
// canonical comparisons, of every frame and of every finish, share one memo, so each pair is compared once (a
// regression: each started a walk of its own, (n + 1)^2 pairs in all). Then two chains that are one canonical type
// all the way down, which the composite takes as they are.
#define BUSTER_TYPE_TEST_CHAIN_LEVELS 65536

static bool type_test_composite_chains(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 4 * BUSTER_TYPE_TEST_CHAIN_LEVELS + 64), "composite chains", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId element = type_aligned(table, BUSTER_TYPE_INT, 0);
        TypeId chains[4] = {
            type_incomplete_array(table, element),
            type_array(table, element, 3),
            type_array(table, BUSTER_TYPE_INT, 3),
            type_array(table, element, 3),
        };
        for (uint32_t level = 0; level < BUSTER_TYPE_TEST_CHAIN_LEVELS; level += 1)
        {
            for (uint32_t chain = 0; chain < 4; chain += 1)
            {
                chains[chain] = type_pointer(table, chains[chain]);
            }
        }
        type_test_expect(&tally, chains[0] != 0 && chains[1] != 0 && chains[2] != 0 && chains[3] != 0, "composite chains", "the types can't be made");
        type_test_expect(&tally, type_compatible(table, chains[0], chains[1]) == TYPE_ANSWER_YES, "composite chains", "int[] and int[3] at the bottom aren't compatible");
        type_test_expect(&tally, type_same_canonical(table, chains[0], chains[1]) == TYPE_ANSWER_NO, "composite chains", "int[] and int[3] at the bottom are one canonical type");
        type_test_expect(&tally, type_composite(table, chains[0], chains[1]) == chains[1], "composite chains", "the composite isn't the chain over int[3]");
        type_test_expect(&tally, type_same_canonical(table, chains[2], chains[3]) == TYPE_ANSWER_YES, "composite chains", "an aligned int at the bottom isn't looked through");
        type_test_expect(&tally, type_composite(table, chains[2], chains[3]) == chains[2], "composite chains", "the composite of one canonical type isn't its left type");
        type_test_table_close(&test);
    }
    type_test_report("composite chains", tally);
    return tally.failures == 0;
}

// The walks with their scratch memory nearly full: with every amount of room from none to 16 KB left in either
// scratch arena, a composite must give its type or 0 with last_error TYPE_ERROR_FULL, and compatibility and the
// canonical comparison their answer or TYPE_ANSWER_FULL, never another (a regression: a canonical comparison that
// couldn't push its stacks answered no, and the composite of int[] and int[3] of an int aligned to 1 became an int[3]
// aligned to 4).
#define BUSTER_TYPE_TEST_SCRATCH_ROOM 16384

static bool type_test_scratch_exhaustion(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 1024), "scratch exhaustion", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        // That pair, then both under 100 pointers, so that the walks' stacks grow; and two types that are one canonical
        // type, pointers to int[3] of the aligned int and of int.
        const TypeId aligned = type_aligned(table, BUSTER_TYPE_INT, 0);
        TypeId lefts[2] = {
            type_incomplete_array(table, BUSTER_TYPE_INT),
            0,
        };
        TypeId rights[2] = {
            type_array(table, aligned, 3),
            0,
        };
        lefts[1] = lefts[0];
        rights[1] = rights[0];
        for (uint32_t level = 0; level < 100; level += 1)
        {
            lefts[1] = type_pointer(table, lefts[1]);
            rights[1] = type_pointer(table, rights[1]);
        }
        const TypeId same_left = type_pointer(table, type_array(table, aligned, 3));
        const TypeId same_right = type_pointer(table, type_array(table, BUSTER_TYPE_INT, 3));
        for (uint32_t pair = 0; pair < 2; pair += 1)
        {
            type_test_expect(&tally, type_composite(table, lefts[pair], rights[pair]) == rights[pair], "scratch exhaustion", "the composite with room isn't int[3] of the aligned int");
        }
        Scratch first = scratch_begin(0);
        Scratch second = scratch_begin(first.arena);
        Arena* arenas[2] = {
            first.arena,
            second.arena,
        };
        uint32_t answered = 0;
        uint32_t refused = 0;
        for (uint32_t which = 0; which < 2; which += 1)
        {
            Arena* arena = arenas[which];
            for (uint64_t room = 0; room <= BUSTER_TYPE_TEST_SCRATCH_ROOM; room += 16)
            {
                const uint64_t length = arena->length;
                const uint64_t left_bytes = arena->capacity - arena->length;
                arena_push_array(arena, left_bytes > room ? left_bytes - room : 0, 1, 1);
                char what[96];
                snprintf(what, sizeof(what), "%" PRIu64 " bytes left in scratch arena %u", room, which);
                for (uint32_t pair = 0; pair < 2; pair += 1)
                {
                    table->last_error = TYPE_ERROR_NONE;
                    const TypeId composite = type_composite(table, lefts[pair], rights[pair]);
                    type_test_expect(&tally, composite == rights[pair] || (composite == 0 && table->last_error == TYPE_ERROR_FULL), "scratch exhaustion composite", what);
                    answered += composite != 0 ? 1 : 0;
                    refused += composite == 0 ? 1 : 0;
                    const TypeAnswer compatible = type_compatible(table, lefts[pair], rights[pair]);
                    type_test_expect(&tally, compatible == TYPE_ANSWER_YES || compatible == TYPE_ANSWER_FULL, "scratch exhaustion compatibility", what);
                }
                const TypeAnswer same = type_same_canonical(table, same_left, same_right);
                const TypeAnswer different = type_same_canonical(table, lefts[1], rights[1]);
                type_test_expect(&tally, same == TYPE_ANSWER_YES || same == TYPE_ANSWER_FULL, "scratch exhaustion canonical", what);
                type_test_expect(&tally, different == TYPE_ANSWER_NO || different == TYPE_ANSWER_FULL, "scratch exhaustion canonical", what);
                arena_pop_to(arena, length);
            }
        }
        // The room must have reached both: composites that failed and ones that answered, in each arena's loop.
        type_test_expect(&tally, answered != 0 && refused != 0, "scratch exhaustion", "the room never made a composite fail, or never let one through");
        printf("%-34s %10u composites answered, %u refused for want of room\n", "scratch exhaustion", answered, refused);
        scratch_end(second);
        scratch_end(first);
        type_test_table_close(&test);
    }
    type_test_report("scratch exhaustion", tally);
    return tally.failures == 0;
}

// An ALIGNED request of a tag completed since its wrapper was made: the batch must give what type_aligned gives (the
// tag, when the alignment is its own), not the wrapper the index holds (a regression).
static bool type_test_batch_after_completion(void)
{
    TypeTestTally tally = { 0 };
    TypeTestTable test;
    if (type_test_expect(&tally, type_test_table_open(&test, 64), "completion", "the table can't be made"))
    {
        TypeTable* table = &test.table;
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, 1);
        const TypeId wrapper = type_aligned(table, record, 2);
        const TypeMember member = type_test_member(1, BUSTER_TYPE_INT, BUSTER_TYPE_NOT_BIT_FIELD);
        type_complete_record(table, record, &member, 1, (TypeRecordAttributes){ 0 });
        const uint8_t kinds[2] = {
            BUSTER_TYPE_KIND_ALIGNED,
            BUSTER_TYPE_KIND_ALIGNED,
        };
        const uint8_t flags[2] = {
            0,
            0,
        };
        const TypeId bases[2] = {
            record,
            record,
        };
        const uint64_t extents[2] = {
            2,
            3,
        };
        const TypeKeys keys = {
            .kinds = kinds,
            .flags = flags,
            .bases = bases,
            .extents = extents,
        };
        TypeId batched[2] = { 0 };
        type_derive_batch(table, keys, 2, batched);
        type_test_expect(&tally, wrapper != 0 && wrapper != record, "completion", "no wrapper while the tag was incomplete");
        type_test_expect(&tally, type_aligned(table, record, 2) == record, "completion", "the tag's own alignment isn't the tag");
        type_test_expect(&tally, batched[0] == record, "completion", "the batch gave the wrapper for the tag's own alignment");
        type_test_expect(&tally, batched[1] != 0 && batched[1] == type_aligned(table, record, 3), "completion", "the batch and type_aligned differ on a larger alignment");
        type_test_table_close(&test);
    }
    type_test_report("completion", tally);
    return tally.failures == 0;
}

static int run_type_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t count = BUSTER_TYPE_TEST_DEFAULT_COUNT;
    const char* clang = "clang";
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            valid = valid && type_test_parse_u64(argv[i + 1], &seed);
            i += 1;
        }
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
        {
            valid = valid && type_test_parse_u64(argv[i + 1], &count) && count >= 64 && count <= (1u << 20);
            i += 1;
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--keep") == 0)
        {
            keep = true;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    Arena arena = arena_reserve(BUSTER_TYPE_TEST_ARENA_BYTES);
    if (!valid)
    {
        fprintf(stderr, "usage: test-types [--seed N] [--count N] [--clang PATH] [--keep]\n");
    }
    else
    {
        bool passed = type_test_builtin_types();
        passed = type_test_validation() && passed;
        passed = type_test_machine_modes(clang, keep) && passed;
        passed = type_test_normalization(&arena) && passed;
        passed = type_test_hash_consing(seed, (uint32_t)count) && passed;
        passed = type_test_batches(seed, (uint32_t)count * 8) && passed;
        passed = type_test_batch_after_completion() && passed;
        passed = type_test_layout_fixtures_check() && passed;
        passed = type_test_member_lookup() && passed;
        passed = type_test_layout_twins(seed, (uint32_t)count / 4) && passed;
        passed = type_test_conversion_kernels(seed) && passed;
        passed = type_test_conversion_types() && passed;
        passed = type_test_compatibility() && passed;
        passed = type_test_shared_pairs() && passed;
        passed = type_test_composite_chains() && passed;
        passed = type_test_scratch_exhaustion() && passed;
        passed = type_test_printing(&arena) && passed;
        passed = type_test_clang(seed, (uint32_t)count, clang, keep) && passed;
        result = passed ? 0 : 1;
    }
    arena_release(&arena);
    return result;
}
