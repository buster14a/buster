// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The calling convention's tests, for the test-calling-convention command:
//
//   build/compiler test-calling-convention [--seed N] [--count N] [--cross-check] [--clang PATH] [--gcc PATH] [--keep]
//
// main.c includes this file after calling_convention.c, c_types_test.c (whose tables, random numbers, worlds of types
// and C spellings it uses), link_program.c (its instruction builders) and object_writer.c, and calls
// run_calling_convention_tests. The checks, in this order:
//   - Merge: clang's merge and post-merge, written again here as clang writes them, against the module's on every pair
//     of classes and both kinds of size.
//   - Fixtures: types whose classes, passing and results clang's IR shows (abi_test_fixtures says which), at each target
//     vector width, by the vector path and by the twin; and calls whose locations clang and LLVM give (an UNCOUNTED
//     argument's shift among them).
//   - Worlds: random worlds of types (abi_test_world_build: scalars, vectors, complex types, _BitInts, _Atomic and
//     aligned types, and records and unions of them, small ones mostly, with bit-fields, arrays, nesting, packing and
//     alignment), classified by the vector path in one table and by the twin in another, which must end up identical,
//     and each type at each offset it can be at against the oracle: clang's classify transcribed as clang has it, its
//     recursion over members as a stack of frames, from the type table alone (no rows).
//   - Assignment: random calls over a world's types, named and variadic, with results in memory and in registers,
//     assigned by the vector path and by the twin, which must agree on every location and total; and the vector path
//     with its argument and location columns ending at a PROT_NONE page.
// The exit status is 1 if the arguments are invalid or a check fails.
//
// Layout, in this order: the tally and helpers; merge; the oracle; fixtures; worlds; assignment; run_calling_convention_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

// OS-dependent includes
#include <unistd.h>
#include <sys/mman.h>

#define BUSTER_ABI_TEST_REPORTED 12
#define BUSTER_ABI_TEST_DEFAULT_COUNT 2000
#define BUSTER_ABI_TEST_ARENA_BYTES (1ull << 30)
#define BUSTER_ABI_TEST_FRAMES 4096

typedef struct AbiTestTally AbiTestTally;
struct AbiTestTally
{
    uint64_t checks;
    uint64_t failures;
};

static bool abi_test_expect(AbiTestTally* tally, bool condition, const char* check, const char* detail)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < BUSTER_ABI_TEST_REPORTED)
        {
            fprintf(stderr, "test-calling-convention: %s: %s\n", check, detail);
        }
        tally->failures += 1;
    }
    return condition;
}

static void abi_test_report(const char* check, AbiTestTally tally)
{
    printf("%-34s %10" PRIu64 " checks, %" PRIu64 " failed\n", check, tally.checks, tally.failures);
}

// A type table with room for `types` types, and a calling convention table over it, for `vector_bytes`.
typedef struct AbiTestTables AbiTestTables;
struct AbiTestTables
{
    TypeTestTable types;
    AbiTable abi;
    Arena arena;
};

static bool abi_test_tables_open(AbiTestTables* tables, uint32_t types)
{
    tables->arena = (Arena){ 0 };
    const bool opened = type_test_table_open(&tables->types, types);
    tables->arena = opened ? arena_reserve(abi_capacity_bytes(tables->types.table.types.capacity)) : (Arena){ 0 };
    return opened && tables->arena.base != 0;
}

static bool abi_test_tables_start(AbiTestTables* tables, uint32_t vector_bytes)
{
    arena_pop_to(&tables->arena, 0);
    return abi_initialize(&tables->abi, &tables->arena, &tables->types.table, vector_bytes);
}

static void abi_test_tables_close(AbiTestTables* tables)
{
    type_test_table_close(&tables->types);
    arena_release(&tables->arena);
}

// ---------------------------------------------------------------------------------------------------------------------
// Merge: clang's merge and postMerge as clang writes them (X86_64ABIInfo::merge and ::postMerge), against the module's.

static uint8_t abi_test_clang_merge(uint8_t accumulated, uint8_t field)
{
    uint8_t result = BUSTER_ABI_SSE;
    if (accumulated == field || field == BUSTER_ABI_NO_CLASS)
    {
        result = accumulated;
    }
    else if (field == BUSTER_ABI_MEMORY)
    {
        result = BUSTER_ABI_MEMORY;
    }
    else if (accumulated == BUSTER_ABI_NO_CLASS)
    {
        result = field;
    }
    else if (accumulated == BUSTER_ABI_INTEGER || field == BUSTER_ABI_INTEGER)
    {
        result = BUSTER_ABI_INTEGER;
    }
    else if (field == BUSTER_ABI_X87 || field == BUSTER_ABI_X87UP || field == BUSTER_ABI_COMPLEX_X87 || accumulated == BUSTER_ABI_X87 ||
             accumulated == BUSTER_ABI_X87UP)
    {
        result = BUSTER_ABI_MEMORY;
    }
    return result;
}

// clang's postMerge: the classes it leaves, not canonical.
static void abi_test_clang_post_merge(uint64_t size_bits, uint8_t* low, uint8_t* high)
{
    if (*high == BUSTER_ABI_MEMORY)
    {
        *low = BUSTER_ABI_MEMORY;
    }
    if (*high == BUSTER_ABI_X87UP && *low != BUSTER_ABI_X87)
    {
        *low = BUSTER_ABI_MEMORY;
    }
    if (size_bits > 128 && (*low != BUSTER_ABI_SSE || *high != BUSTER_ABI_SSEUP))
    {
        *low = BUSTER_ABI_MEMORY;
    }
    if (*high == BUSTER_ABI_SSEUP && *low != BUSTER_ABI_SSE)
    {
        *high = BUSTER_ABI_SSE;
    }
}

static bool abi_test_merge(void)
{
    AbiTestTally tally = { 0 };
    char detail[128];
    for (uint8_t accumulated = 0; accumulated < BUSTER_ABI_CLASS_COUNT; accumulated += 1)
    {
        for (uint8_t field = 0; field < BUSTER_ABI_CLASS_COUNT; field += 1)
        {
            // clang stops merging at MEMORY, which the module makes absorbing on either side.
            const uint8_t expected = accumulated == BUSTER_ABI_MEMORY ? BUSTER_ABI_MEMORY : abi_test_clang_merge(accumulated, field);
            snprintf(detail, sizeof(detail), "merge(%u, %u) is %u, not %u", accumulated, field, abi_merge_class(accumulated, field), expected);
            abi_test_expect(&tally, abi_merge_class(accumulated, field) == expected, "merge", detail);
            for (uint32_t big = 0; big < 2; big += 1)
            {
                uint8_t low = accumulated;
                uint8_t high = field;
                abi_test_clang_post_merge(big != 0 ? 256 : 128, &low, &high);
                const AbiClasses got = abi_post_merge(big != 0 ? 32 : 16, BUSTER_ABI_CLASSES(accumulated, field));
                snprintf(detail, sizeof(detail), "post-merge of (%u, %u), %s", accumulated, field, big != 0 ? "32 bytes" : "16 bytes");
                abi_test_expect(&tally, got == abi_canonical(low, high), "merge", detail);
            }
        }
    }
    abi_test_report("merge", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The oracle: clang's X86_64ABIInfo::classify transcribed, its recursion over members and elements a stack of frames.
// It reads the type table alone: the classes of a type at a bit offset, not canonical.

typedef struct AbiTestFrame AbiTestFrame;
struct AbiTestFrame
{
    uint32_t index;
    uint64_t offset;
    uint64_t next;
    uint8_t low;
    uint8_t high;
    bool started;
    bool finished;
    bool post;
};

static uint32_t abi_test_resolve(const TypeTable* table, uint32_t index)
{
    uint32_t result = index;
    for (uint32_t i = 0; i < 4 && (table->types.kind[result] == BUSTER_TYPE_KIND_ALIGNED || table->types.kind[result] == BUSTER_TYPE_KIND_ENUM); i += 1)
    {
        result = table->types.base[result] >> BUSTER_TYPE_INDEX_SHIFT;
    }
    return result;
}

// What clang's getTypeAlign gives a field's canonical type: ALIGNED looked through, arrays' elements too.
static uint64_t abi_test_canonical_alignment(const TypeTable* table, TypeId type)
{
    uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    for (uint32_t i = 0; i < 64; i += 1)
    {
        if (table->types.kind[index] == BUSTER_TYPE_KIND_ALIGNED || table->types.kind[index] == BUSTER_TYPE_KIND_ARRAY)
        {
            index = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        }
    }
    return 1ull << table->types.alignment_log2[index];
}

static bool abi_test_is_aggregate_kind(uint8_t kind)
{
    return kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ARRAY;
}

static uint8_t* abi_test_current(uint64_t offset, uint8_t* low, uint8_t* high)
{
    return offset < 64 ? low : high;
}

// A type that isn't a record or an array, at `offset` bits: the rules before clang's ConstantArrayType case.
static void abi_test_leaf(const TypeTable* table, uint32_t vector_bytes, bool named, uint32_t index, uint64_t offset, uint8_t* low, uint8_t* high)
{
    const uint32_t resolved = abi_test_resolve(table, index);
    const uint8_t kind = table->types.kind[resolved];
    const uint8_t code = table->types.code[resolved];
    const uint64_t size = table->types.size[resolved] * 8;
    *low = BUSTER_ABI_NO_CLASS;
    *high = BUSTER_ABI_NO_CLASS;
    uint8_t* current = abi_test_current(offset, low, high);
    *current = BUSTER_ABI_MEMORY;
    if (kind == BUSTER_TYPE_KIND_VOID)
    {
        *current = BUSTER_ABI_NO_CLASS;
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER && (code == BUSTER_TYPE_CODE_INT128 || code == BUSTER_TYPE_CODE_UINT128))
    {
        *low = BUSTER_ABI_INTEGER;
        *high = BUSTER_ABI_INTEGER;
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_POINTER)
    {
        *current = BUSTER_ABI_INTEGER;
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        if (code == BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_DOUBLE || code == BUSTER_TYPE_CODE_FLOAT16 || code == BUSTER_TYPE_CODE_BF16)
        {
            *current = BUSTER_ABI_SSE;
        }
        else if (code == BUSTER_TYPE_CODE_FLOAT128)
        {
            *low = BUSTER_ABI_SSE;
            *high = BUSTER_ABI_SSEUP;
        }
        else if (code == BUSTER_TYPE_CODE_LONG_DOUBLE)
        {
            *low = BUSTER_ABI_X87;
            *high = BUSTER_ABI_X87UP;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const uint32_t element = table->types.base[resolved] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t element_code = table->types.code[element];
        const bool element_kind_integer = table->types.kind[element] == BUSTER_TYPE_KIND_INTEGER;
        if (size == 1 || size == 8 || size == 16 || size == 32)
        {
            *current = BUSTER_ABI_INTEGER;
            if (offset / 64 != (offset + size - 1) / 64)
            {
                *high = *low;
            }
        }
        else if (size == 64)
        {
            if (table->types.kind[element] != BUSTER_TYPE_KIND_FLOATING || element_code != BUSTER_TYPE_CODE_DOUBLE)
            {
                *current = BUSTER_ABI_SSE;
                if (offset != 0 && offset != 64)
                {
                    *high = *low;
                }
            }
        }
        else if (size == 128 || (named && size <= (uint64_t)vector_bytes * 8))
        {
            const bool int128 = element_kind_integer && (element_code == BUSTER_TYPE_CODE_INT128 || element_code == BUSTER_TYPE_CODE_UINT128);
            if (size == 128 || !int128)
            {
                *low = BUSTER_ABI_SSE;
                *high = BUSTER_ABI_SSEUP;
            }
        }
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        const uint32_t element = table->types.base[resolved] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint8_t element_kind = table->types.kind[element];
        const uint8_t element_code = table->types.code[element];
        if (element_kind == BUSTER_TYPE_KIND_INTEGER || element_kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            if (size <= 64)
            {
                *current = BUSTER_ABI_INTEGER;
            }
            else if (size <= 128)
            {
                *low = BUSTER_ABI_INTEGER;
                *high = BUSTER_ABI_INTEGER;
            }
        }
        else if (element_code == BUSTER_TYPE_CODE_FLOAT16 || element_code == BUSTER_TYPE_CODE_FLOAT || element_code == BUSTER_TYPE_CODE_BF16)
        {
            *current = BUSTER_ABI_SSE;
        }
        else if (element_code == BUSTER_TYPE_CODE_DOUBLE)
        {
            *low = BUSTER_ABI_SSE;
            *high = BUSTER_ABI_SSE;
        }
        else if (element_code == BUSTER_TYPE_CODE_LONG_DOUBLE)
        {
            *current = BUSTER_ABI_COMPLEX_X87;
        }
        const uint64_t real = offset / 64;
        const uint64_t imaginary = (offset + table->types.size[element] * 8) / 64;
        if (*high == BUSTER_ABI_NO_CLASS && real != imaginary)
        {
            *high = *low;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT)
    {
        const uint64_t width = table->types.extent[resolved];
        if (width <= 64)
        {
            *current = BUSTER_ABI_INTEGER;
        }
        else if (width <= 128)
        {
            *low = BUSTER_ABI_INTEGER;
            *high = BUSTER_ABI_INTEGER;
        }
    }
    // NULLPTR, __fp16 (BuiltinTypes clang lists nowhere) and ATOMIC (no case at all): MEMORY.
}

// Starts a frame for a record or an array at `offset` bits: the checks before clang's loop, which may finish it.
static void abi_test_start(const TypeTable* table, uint32_t vector_bytes, AbiTestFrame* frame)
{
    const uint32_t index = frame->index;
    const uint8_t kind = table->types.kind[index];
    const uint64_t size = table->types.size[index] * 8;
    frame->started = true;
    frame->low = BUSTER_ABI_NO_CLASS;
    frame->high = BUSTER_ABI_NO_CLASS;
    uint8_t* current = abi_test_current(frame->offset, &frame->low, &frame->high);
    *current = BUSTER_ABI_MEMORY;
    if (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        const TypeId element = table->types.base[index];
        const uint64_t element_size = table->types.size[abi_test_resolve(table, element >> BUSTER_TYPE_INDEX_SHIFT)] * 8;
        if (size > 512 || frame->offset % (8ull << table->types.alignment_log2[element >> BUSTER_TYPE_INDEX_SHIFT]) != 0)
        {
            frame->finished = true;
        }
        else
        {
            *current = BUSTER_ABI_NO_CLASS;
            frame->finished = size > 128 && (size != element_size || size > (uint64_t)vector_bytes * 8);
            frame->next = 0;
        }
    }
    else
    {
        if (size > 512 || (table->types.flags[index] & BUSTER_TYPE_FLAG_FLEXIBLE) != 0)
        {
            frame->finished = true;
        }
        else
        {
            *current = BUSTER_ABI_NO_CLASS;
            frame->next = table->tags.member_begin[table->types.index[index]];
        }
    }
}

static bool abi_test_merge_into(AbiTestFrame* frame, uint8_t low, uint8_t high)
{
    frame->low = abi_test_clang_merge(frame->low, low);
    frame->high = abi_test_clang_merge(frame->high, high);
    return frame->low == BUSTER_ABI_MEMORY || frame->high == BUSTER_ABI_MEMORY;
}

// clang's classify of `type` at `offset` bits, as a named argument or not: canonical classes.
static AbiClasses abi_test_classify(const TypeTable* table, uint32_t vector_bytes, bool named, TypeId type, uint64_t offset, AbiTestFrame* stack)
{
    const uint32_t root = abi_test_resolve(table, type >> BUSTER_TYPE_INDEX_SHIFT);
    uint8_t low = BUSTER_ABI_NO_CLASS;
    uint8_t high = BUSTER_ABI_NO_CLASS;
    uint32_t depth = 0;
    if (abi_test_is_aggregate_kind(table->types.kind[root]))
    {
        stack[0] = (AbiTestFrame){
            .index = root,
            .offset = offset,
        };
        depth = 1;
    }
    else
    {
        abi_test_leaf(table, vector_bytes, named, root, offset, &low, &high);
    }
    while (depth != 0)
    {
        AbiTestFrame* frame = &stack[depth - 1];
        const uint32_t index = frame->index;
        const uint8_t kind = table->types.kind[index];
        const uint64_t size = table->types.size[index] * 8;
        if (!frame->started)
        {
            abi_test_start(table, vector_bytes, frame);
        }
        else if (!frame->finished && kind == BUSTER_TYPE_KIND_ARRAY)
        {
            const TypeId element = table->types.base[index];
            const uint32_t element_index = abi_test_resolve(table, element >> BUSTER_TYPE_INDEX_SHIFT);
            const uint64_t element_size = table->types.size[element_index] * 8;
            if (frame->next == table->types.extent[index])
            {
                frame->finished = true;
                frame->post = true;
            }
            else
            {
                const uint64_t element_offset = frame->offset + frame->next * element_size;
                frame->next += 1;
                if (abi_test_is_aggregate_kind(table->types.kind[element_index]) && depth < BUSTER_ABI_TEST_FRAMES)
                {
                    stack[depth] = (AbiTestFrame){
                        .index = element_index,
                        .offset = element_offset,
                    };
                    depth += 1;
                }
                else
                {
                    uint8_t field_low;
                    uint8_t field_high;
                    abi_test_leaf(table, vector_bytes, named, element_index, element_offset, &field_low, &field_high);
                    frame->finished = abi_test_merge_into(frame, field_low, field_high);
                    frame->post = true;
                }
            }
        }
        else if (!frame->finished)
        {
            const uint32_t tag = table->types.index[index];
            const uint32_t end = table->tags.member_begin[tag] + table->tags.member_count[tag];
            const bool is_union = kind == BUSTER_TYPE_KIND_UNION;
            if (frame->next == end)
            {
                frame->finished = true;
                frame->post = true;
            }
            else
            {
                const uint32_t member = (uint32_t)frame->next;
                frame->next += 1;
                const TypeId member_type = table->members.type[member];
                const uint32_t member_index = abi_test_resolve(table, member_type >> BUSTER_TYPE_INDEX_SHIFT);
                const bool bit_field = (table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                const uint64_t member_offset = frame->offset + table->members.offset[member];
                const uint64_t member_size = table->types.size[member_index] * 8;
                if (bit_field && table->members.name[member] == 0)
                {
                }
                else if (size > 128 && ((!is_union && size != member_size) || size > (uint64_t)vector_bytes * 8))
                {
                    frame->low = BUSTER_ABI_MEMORY;
                    frame->finished = true;
                    frame->post = true;
                }
                else if (!bit_field && member_offset % (8 * abi_test_canonical_alignment(table, member_type)) != 0)
                {
                    frame->low = BUSTER_ABI_MEMORY;
                    frame->finished = true;
                    frame->post = true;
                }
                else if (bit_field)
                {
                    const uint64_t width = table->members.bit_width[member];
                    const uint64_t first = member_offset / 64;
                    const uint64_t last = (member_offset + width - 1) / 64;
                    const uint8_t field_low = first != 0 ? BUSTER_ABI_NO_CLASS : BUSTER_ABI_INTEGER;
                    const uint8_t field_high = first != 0 || last != 0 ? BUSTER_ABI_INTEGER : BUSTER_ABI_NO_CLASS;
                    frame->finished = abi_test_merge_into(frame, field_low, field_high);
                    frame->post = true;
                }
                else if (abi_test_is_aggregate_kind(table->types.kind[member_index]) && depth < BUSTER_ABI_TEST_FRAMES)
                {
                    stack[depth] = (AbiTestFrame){
                        .index = member_index,
                        .offset = member_offset,
                    };
                    depth += 1;
                }
                else
                {
                    uint8_t field_low;
                    uint8_t field_high;
                    abi_test_leaf(table, vector_bytes, named, member_index, member_offset, &field_low, &field_high);
                    frame->finished = abi_test_merge_into(frame, field_low, field_high);
                    frame->post = true;
                }
            }
        }
        if (frame->finished)
        {
            low = frame->low;
            high = frame->high;
            if (frame->post)
            {
                abi_test_clang_post_merge(size, &low, &high);
            }
            depth -= 1;
            if (depth != 0)
            {
                AbiTestFrame* parent = &stack[depth - 1];
                parent->finished = abi_test_merge_into(parent, low, high);
                parent->post = true;
            }
        }
    }
    return abi_canonical(low, high);
}

// ---------------------------------------------------------------------------------------------------------------------
// Fixtures: types whose classes, passing and results clang 23's IR shows on x86-64 Linux (`clang -O1 -S -emit-llvm` of
// functions that take and return them, read for byval, sret, the coerced types and the attributes), at each target
// vector width. GCC 16 passes them alike but where calling_convention.c's orientation comment says otherwise.

#define BUSTER_ABI_TEST_INTEGER_ONLY BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_NO_CLASS)
#define BUSTER_ABI_TEST_SSE_ONLY BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_NO_CLASS)
#define BUSTER_ABI_TEST_INTEGERS BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_INTEGER)
#define BUSTER_ABI_TEST_SSES BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_SSE)
#define BUSTER_ABI_TEST_VECTOR BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_SSEUP)
#define BUSTER_ABI_TEST_XMM(n) (BUSTER_ABI_REGISTER_VECTOR | (n))
#define BUSTER_ABI_TEST_ST(n) (BUSTER_ABI_REGISTER_X87 | (n))
#define BUSTER_ABI_TEST_NONE BUSTER_ABI_REGISTER_NONE
#define BUSTER_ABI_TEST_FIXTURES 160

typedef struct AbiTestExpectation AbiTestExpectation;
struct AbiTestExpectation
{
    const char* name;
    TypeId type;
    AbiClasses named;
    AbiClasses unnamed;
    // Pass bits that must be set, and that must be clear.
    uint32_t set;
    uint32_t clear;
    uint8_t result_kind;
    uint8_t result[2];
};

typedef struct AbiTestBuilder AbiTestBuilder;
struct AbiTestBuilder
{
    TypeTable* table;
    AbiTestExpectation* expectations;
    uint32_t count;
    uint32_t next_name;
    uint32_t vector_bytes;
};

static void abi_test_expectation(AbiTestBuilder* builder, const char* name, TypeId type, AbiClasses named, uint32_t set, uint32_t clear, uint8_t result_kind, uint8_t first,
                                 uint8_t second)
{
    if (builder->count < BUSTER_ABI_TEST_FIXTURES)
    {
        builder->expectations[builder->count] = (AbiTestExpectation){
            .name = name,
            .type = type,
            .named = named,
            .unnamed = (set & BUSTER_ABI_PASS_WIDE) != 0 ? BUSTER_ABI_IN_MEMORY : named,
            .set = set,
            .clear = clear,
            .result_kind = result_kind,
            .result = {
                first,
                second,
            },
        };
        builder->count += 1;
    }
}

// The common shapes: a type passed in registers and returned in them.
static void abi_test_in_registers(AbiTestBuilder* builder, const char* name, TypeId type, AbiClasses named, uint32_t set, uint32_t clear, uint8_t first, uint8_t second)
{
    abi_test_expectation(builder, name, type, named, set, clear | BUSTER_ABI_PASS_MEMORY, BUSTER_ABI_RESULT_REGISTERS, first, second);
}

static void abi_test_in_memory(AbiTestBuilder* builder, const char* name, TypeId type)
{
    abi_test_expectation(builder, name, type, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, BUSTER_ABI_PASS_UNCOUNTED,
                         BUSTER_ABI_RESULT_MEMORY, BUSTER_ABI_TEST_NONE, BUSTER_ABI_TEST_NONE);
}

static TypeMember abi_test_member(AbiTestBuilder* builder, TypeId type)
{
    builder->next_name += 1;
    return type_test_member(builder->next_name, type, BUSTER_TYPE_NOT_BIT_FIELD);
}

static TypeMember abi_test_bits(AbiTestBuilder* builder, TypeId type, uint32_t width, bool named)
{
    builder->next_name += named ? 1 : 0;
    return type_test_member(named ? builder->next_name : 0, type, width);
}

static TypeId abi_test_record(AbiTestBuilder* builder, uint8_t kind, const TypeMember* members, uint32_t count, bool packed, uint8_t alignment_log2)
{
    builder->next_name += 1;
    const TypeId record = type_tag(builder->table, kind, builder->next_name);
    const TypeRecordAttributes attributes = {
        .packed = packed,
        .pack_log2 = 0,
        .alignment_log2 = alignment_log2,
    };
    return record != 0 && type_complete_record(builder->table, record, members, count, attributes) ? record : 0;
}

static TypeId abi_test_vector(AbiTestBuilder* builder, TypeId element, uint64_t bytes)
{
    return type_vector_size(builder->table, element, bytes);
}

static void abi_test_build_scalars(AbiTestBuilder* builder)
{
    TypeTable* table = builder->table;
    const uint8_t rax = BUSTER_ABI_RAX;
    const uint8_t rdx = BUSTER_ABI_RDX;
    abi_test_in_registers(builder, "int", BUSTER_TYPE_INT, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_LOW_INTEGER, BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO, rax,
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "char", BUSTER_TYPE_CHAR, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_SIGN, BUSTER_ABI_PASS_ZERO, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "unsigned char", BUSTER_TYPE_UCHAR, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_ZERO, BUSTER_ABI_PASS_SIGN, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "bool", BUSTER_TYPE_BOOL, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_ZERO, BUSTER_ABI_PASS_SIGN, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "short", BUSTER_TYPE_SHORT, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_SIGN, BUSTER_ABI_PASS_ZERO, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "unsigned long", BUSTER_TYPE_ULONG, BUSTER_ABI_TEST_INTEGER_ONLY, 0, BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO, rax,
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "float", BUSTER_TYPE_FLOAT, BUSTER_ABI_TEST_SSE_ONLY, BUSTER_ABI_PASS_LOW_SSE, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "double", BUSTER_TYPE_DOUBLE, BUSTER_ABI_TEST_SSE_ONLY, BUSTER_ABI_PASS_LOW_SSE, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_Float16", BUSTER_TYPE_FLOAT16, BUSTER_ABI_TEST_SSE_ONLY, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "__bf16", BUSTER_TYPE_BF16, BUSTER_ABI_TEST_SSE_ONLY, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "__float128", BUSTER_TYPE_FLOAT128, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR, BUSTER_ABI_PASS_WIDE, BUSTER_ABI_TEST_XMM(0),
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "__int128", BUSTER_TYPE_INT128, BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    abi_test_in_registers(builder, "int *", type_pointer(table, BUSTER_TYPE_INT), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    // long double: X87, X87UP, in memory as an argument (an x86_fp80 value LLVM puts on the stack), st0 as a result.
    abi_test_expectation(builder, "long double", BUSTER_TYPE_LONG_DOUBLE, BUSTER_ABI_CLASSES(BUSTER_ABI_X87, BUSTER_ABI_X87UP), BUSTER_ABI_PASS_MEMORY,
                         BUSTER_ABI_PASS_AGGREGATE | BUSTER_ABI_PASS_UNCOUNTED, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_ST(0), BUSTER_ABI_TEST_NONE);
    // __fp16: no case in clang's classify, so MEMORY (it is never an argument or a result, only a member).
    abi_test_expectation(builder, "__fp16", BUSTER_TYPE_HALF, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY, BUSTER_ABI_PASS_AGGREGATE, BUSTER_ABI_RESULT_REGISTERS,
                         BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    // nullptr_t: MEMORY, but passed as a pointer in a register clang doesn't count (f(long x4, nullptr_t, struct {long,
    // long}, long) passes the struct's halves in r9 and on the stack), and returned in rax.
    abi_test_expectation(builder, "nullptr_t", BUSTER_TYPE_NULLPTR, BUSTER_ABI_IN_MEMORY,
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED | BUSTER_ABI_VALUE_GENERAL << BUSTER_ABI_PASS_VALUE_SHIFT, BUSTER_ABI_PASS_AGGREGATE,
                         BUSTER_ABI_RESULT_REGISTERS, rax, BUSTER_ABI_TEST_NONE);
    abi_test_expectation(builder, "_Atomic int", type_atomic(table, BUSTER_TYPE_INT), BUSTER_ABI_IN_MEMORY,
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED | BUSTER_ABI_VALUE_GENERAL << BUSTER_ABI_PASS_VALUE_SHIFT,
                         BUSTER_ABI_PASS_AGGREGATE | BUSTER_ABI_PASS_SIGN, BUSTER_ABI_RESULT_REGISTERS, rax, BUSTER_ABI_TEST_NONE);
    abi_test_expectation(builder, "_Atomic __int128", type_atomic(table, BUSTER_TYPE_INT128), BUSTER_ABI_IN_MEMORY,
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED | BUSTER_ABI_VALUE_PAIR << BUSTER_ABI_PASS_VALUE_SHIFT, BUSTER_ABI_PASS_AGGREGATE,
                         BUSTER_ABI_RESULT_REGISTERS, rax, rdx);
    abi_test_expectation(builder, "_Atomic double", type_atomic(table, BUSTER_TYPE_DOUBLE), BUSTER_ABI_IN_MEMORY,
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED | BUSTER_ABI_VALUE_VECTOR << BUSTER_ABI_PASS_VALUE_SHIFT, BUSTER_ABI_PASS_AGGREGATE,
                         BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_expectation(builder, "_Atomic long double", type_atomic(table, BUSTER_TYPE_LONG_DOUBLE), BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY,
                         BUSTER_ABI_PASS_AGGREGATE | BUSTER_ABI_PASS_UNCOUNTED, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_ST(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_BitInt(7)", type_bit_int(table, 7, false), BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_SIGN, BUSTER_ABI_PASS_ZERO, rax,
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "unsigned _BitInt(17)", type_bit_int(table, 17, true), BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_ZERO, BUSTER_ABI_PASS_SIGN, rax,
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_BitInt(33)", type_bit_int(table, 33, false), BUSTER_ABI_TEST_INTEGER_ONLY, 0, BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO, rax,
                          BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_BitInt(65)", type_bit_int(table, 65, false), BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    abi_test_in_registers(builder, "_BitInt(100)", type_bit_int(table, 100, false), BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    abi_test_in_registers(builder, "unsigned _BitInt(128)", type_bit_int(table, 128, true), BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    abi_test_in_memory(builder, "_BitInt(129)", type_bit_int(table, 129, false));
    abi_test_in_registers(builder, "_Complex float", BUSTER_TYPE_COMPLEX_FLOAT, BUSTER_ABI_TEST_SSE_ONLY, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_Complex double", BUSTER_TYPE_COMPLEX_DOUBLE, BUSTER_ABI_TEST_SSES, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    abi_test_in_registers(builder, "_Complex char", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_CHAR), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_Complex int", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_INT), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "_Complex long", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_LONG), BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    abi_test_expectation(builder, "_Complex long double", BUSTER_TYPE_COMPLEX_LONG_DOUBLE, BUSTER_ABI_CLASSES(BUSTER_ABI_COMPLEX_X87, BUSTER_ABI_COMPLEX_X87),
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, 0, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_ST(0), BUSTER_ABI_TEST_ST(1));
    abi_test_in_memory(builder, "_Complex __float128", BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_FLOAT128));
}

static void abi_test_build_vectors(AbiTestBuilder* builder)
{
    const uint8_t rax = BUSTER_ABI_RAX;
    const uint32_t wide = builder->vector_bytes;
    abi_test_in_registers(builder, "char x4", abi_test_vector(builder, BUSTER_TYPE_CHAR, 4), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    // A vector of one float is INTEGER (gcc's rule once); GCC 16 passes it as SSE.
    abi_test_in_registers(builder, "float x1", abi_test_vector(builder, BUSTER_TYPE_FLOAT, 4), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "short x2", abi_test_vector(builder, BUSTER_TYPE_SHORT, 4), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "long x1", abi_test_vector(builder, BUSTER_TYPE_LONG, 8), BUSTER_ABI_TEST_SSE_ONLY, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "float x2", abi_test_vector(builder, BUSTER_TYPE_FLOAT, 8), BUSTER_ABI_TEST_SSE_ONLY, 0, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    // <1 x double>: MEMORY, byval as an argument, and returned as its value in xmm0.
    abi_test_expectation(builder, "double x1", abi_test_vector(builder, BUSTER_TYPE_DOUBLE, 8), BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE,
                         BUSTER_ABI_PASS_UNCOUNTED, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "float x4", abi_test_vector(builder, BUSTER_TYPE_FLOAT, 16), BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR, BUSTER_ABI_PASS_WIDE,
                          BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    abi_test_in_registers(builder, "__int128 x1", abi_test_vector(builder, BUSTER_TYPE_INT128, 16), BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR, 0,
                          BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    const TypeId float8 = abi_test_vector(builder, BUSTER_TYPE_FLOAT, 32);
    const TypeId float16 = abi_test_vector(builder, BUSTER_TYPE_FLOAT, 64);
    if (wide >= 32)
    {
        abi_test_in_registers(builder, "float x8", float8, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, 0, BUSTER_ABI_TEST_XMM(0),
                              BUSTER_ABI_TEST_NONE);
        abi_test_in_memory(builder, "__int128 x2", abi_test_vector(builder, BUSTER_TYPE_INT128, 32));
    }
    else
    {
        // Wider than the target's registers: byval as an argument, xmm0 and xmm1 as a result (LLVM splits it).
        abi_test_expectation(builder, "float x8", float8, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, BUSTER_ABI_PASS_WIDE,
                             BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    }
    if (wide >= 64)
    {
        abi_test_in_registers(builder, "float x16", float16, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, 0, BUSTER_ABI_TEST_XMM(0),
                              BUSTER_ABI_TEST_NONE);
    }
    else
    {
        // Two or four of the target's vector registers as a result (xmm0 to xmm3, or ymm0 and ymm1).
        abi_test_expectation(builder, "float x16", float16, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, BUSTER_ABI_PASS_UNSUPPORTED,
                             BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    }
    // Eight 16-byte registers would hold it, more than LLVM returns a value in: it returns it through a hidden pointer.
    const TypeId float32 = abi_test_vector(builder, BUSTER_TYPE_FLOAT, 128);
    if (wide == 16)
    {
        abi_test_in_memory(builder, "float x32", float32);
    }
    else
    {
        abi_test_expectation(builder, "float x32", float32, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, BUSTER_ABI_PASS_UNSUPPORTED,
                             BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    }
}

static void abi_test_build_records(AbiTestBuilder* builder)
{
    TypeTable* table = builder->table;
    const uint8_t rax = BUSTER_ABI_RAX;
    const uint8_t rdx = BUSTER_ABI_RDX;
    const TypeId flt = BUSTER_TYPE_FLOAT;
    TypeMember m[4];
    m[0] = abi_test_member(builder, flt);
    m[1] = abi_test_member(builder, flt);
    m[2] = abi_test_member(builder, flt);
    abi_test_in_registers(builder, "{float, float, float}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), BUSTER_ABI_TEST_SSES, 0, 0,
                          BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1] = abi_test_member(builder, flt);
    abi_test_in_registers(builder, "{int, float}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGER_ONLY, 0,
                          BUSTER_ABI_PASS_SIGN, rax, BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT);
    abi_test_in_registers(builder, "{long, int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR,
                          rax, rdx);
    m[0] = abi_test_member(builder, flt);
    m[1] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    abi_test_in_registers(builder, "{float, double}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_SSES, 0, 0,
                          BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    abi_test_in_registers(builder, "{long, double}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_SSE), 0, 0, rax, BUSTER_ABI_TEST_XMM(0));
    m[0] = abi_test_member(builder, type_array(table, flt, 3));
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_in_registers(builder, "{float[3], char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER), 0, 0, BUSTER_ABI_TEST_XMM(0), rax);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_INT, 3));
    abi_test_in_registers(builder, "{int[3]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_INTEGERS, 0, 0, rax, rdx);
    // Bit-fields: {int a : 3; int : 0; long b : 40} is INTEGER twice; a bit-field may straddle the eightbytes.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_INT, 3, true);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_INT, 0, false);
    m[2] = abi_test_bits(builder, BUSTER_TYPE_LONG, 40, true);
    abi_test_in_registers(builder, "{int:3, int:0, long:40}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), BUSTER_ABI_TEST_INTEGERS, 0, 0, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_LONG, 60, true);
    abi_test_in_registers(builder, "{char, long:60}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, 0, 0, rax, rdx);
    m[0] = abi_test_member(builder, flt);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_INT, 3, true);
    abi_test_in_registers(builder, "{float, int:3}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax,
                          BUSTER_ABI_TEST_NONE);
    // An unnamed bit-field is padding: {float, int : 3} is a float.
    m[0] = abi_test_member(builder, flt);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_INT, 3, false);
    abi_test_in_registers(builder, "{float, int:3 unnamed}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_SSE_ONLY, 0, 0,
                          BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    // Nothing to pass: empty, unnamed bit-fields alone, zero-length arrays.
    const TypeId empty = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0);
    abi_test_expectation(builder, "{}", empty, BUSTER_ABI_NOTHING, BUSTER_ABI_PASS_IGNORE, BUSTER_ABI_PASS_MEMORY, BUSTER_ABI_RESULT_IGNORE, BUSTER_ABI_TEST_NONE,
                         BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_INT, 0, false);
    abi_test_expectation(builder, "{int:0}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_NOTHING, BUSTER_ABI_PASS_IGNORE, 0,
                         BUSTER_ABI_RESULT_IGNORE, BUSTER_ABI_TEST_NONE, BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 0));
    abi_test_expectation(builder, "{char[0]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_NOTHING, BUSTER_ABI_PASS_IGNORE, 0,
                         BUSTER_ABI_RESULT_IGNORE, BUSTER_ABI_TEST_NONE, BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_member(builder, empty);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT);
    abi_test_in_registers(builder, "{struct {}, int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax,
                          BUSTER_ABI_TEST_NONE);
    // Packed: an int at offset 1 is misaligned (MEMORY); {int, char} packed is 5 bytes of INTEGER.
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT);
    abi_test_in_memory(builder, "packed {char, int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0));
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    const TypeId packed = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0);
    abi_test_in_registers(builder, "packed {int, char}", packed, BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, BUSTER_ABI_TEST_NONE);
    // ... which at offset 1 of another struct puts its int at bit 8: MEMORY, though its own alignment is 1.
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, packed);
    abi_test_in_memory(builder, "{char, packed {int, char}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    // The low eightbyte padding alone: the high one passes by itself.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_LONG, 64, false);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    abi_test_in_registers(builder, "{long:64 unnamed, long}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_HIGH_INTEGER, BUSTER_ABI_PASS_LOW_INTEGER, rax,
                          BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_LONG, 64, false);
    m[1] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    abi_test_in_registers(builder, "{long:64 unnamed, double}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, BUSTER_ABI_SSE), BUSTER_ABI_PASS_HIGH_SSE, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    // A flexible array member puts the struct in memory (clang; GCC classifies the rest).
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1] = abi_test_member(builder, type_incomplete_array(table, BUSTER_TYPE_INT));
    abi_test_in_memory(builder, "{int, int[]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    // An _Atomic or nullptr_t member puts it in memory (clang; GCC classifies them as their values).
    m[0] = abi_test_member(builder, type_atomic(table, BUSTER_TYPE_INT));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT);
    abi_test_in_memory(builder, "{_Atomic int, int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    m[0] = abi_test_member(builder, BUSTER_TYPE_NULLPTR);
    abi_test_in_memory(builder, "{nullptr_t}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    // An _Atomic struct is in memory too, and returned through the hidden pointer.
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[2] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_in_memory(builder, "_Atomic {char, char, char}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0)));
    // long double in a struct: X87, X87UP; in unions it merges with what shares its eightbytes, in member order.
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    abi_test_expectation(builder, "{long double}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_CLASSES(BUSTER_ABI_X87, BUSTER_ABI_X87UP),
                         BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE, 0, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_ST(0), BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    abi_test_in_memory(builder, "union {long double, long}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0));
    const TypeId longs = type_array(table, BUSTER_TYPE_LONG, 2);
    const TypeId doubles = type_array(table, BUSTER_TYPE_DOUBLE, 2);
    m[0] = abi_test_member(builder, longs);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[2] = abi_test_member(builder, doubles);
    abi_test_in_registers(builder, "union {long[2], long double, double[2]}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 3, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          0, 0, rax, rdx);
    m[0] = abi_test_member(builder, doubles);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[2] = abi_test_member(builder, longs);
    abi_test_in_memory(builder, "union {double[2], long double, long[2]}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 3, false, 0));
    // Aligned: 32 bytes for one int is MEMORY; 16 for one long is one INTEGER eightbyte, a 16-byte slot on the stack.
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    abi_test_in_memory(builder, "aligned(32) {int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 6));
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    abi_test_in_registers(builder, "aligned(16) {long}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 5), BUSTER_ABI_TEST_INTEGER_ONLY,
                          4u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT, 0, rax, BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "{__int128}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0,
                          rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {long, __int128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    // A struct of 32 bytes: MEMORY, unless it is one vector the target passes in a register.
    m[0] = abi_test_member(builder, type_vector_size(table, BUSTER_TYPE_FLOAT, 16));
    m[1] = abi_test_member(builder, type_vector_size(table, BUSTER_TYPE_FLOAT, 16));
    abi_test_in_memory(builder, "{float x4, float x4}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    const TypeId float8 = type_vector_size(table, BUSTER_TYPE_FLOAT, 32);
    m[0] = abi_test_member(builder, float8);
    const TypeId wrapped = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    m[0] = abi_test_member(builder, float8);
    m[1] = abi_test_member(builder, flt);
    const TypeId with_float = abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    m[0] = abi_test_member(builder, float8);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    const TypeId with_long = abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    m[0] = abi_test_member(builder, float8);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 20));
    const TypeId with_chars = abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    if (builder->vector_bytes >= 32)
    {
        const uint32_t set = BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE | BUSTER_ABI_PASS_AGGREGATE;
        abi_test_in_registers(builder, "{float x8}", wrapped, BUSTER_ABI_TEST_VECTOR, set, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
        abi_test_in_registers(builder, "union {float x8, float}", with_float, BUSTER_ABI_TEST_VECTOR, set, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
        // A char array past 16 bytes adds NO_CLASS (clang returns before merging), so this is one YMM register.
        abi_test_in_registers(builder, "union {float x8, char[20]}", with_chars, BUSTER_ABI_TEST_VECTOR, set, 0, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_NONE);
    }
    else
    {
        abi_test_in_memory(builder, "{float x8}", wrapped);
        abi_test_in_memory(builder, "union {float x8, float}", with_float);
        abi_test_in_memory(builder, "union {float x8, char[20]}", with_chars);
    }
    abi_test_in_memory(builder, "union {float x8, long}", with_long);
    // Nested at a nonzero offset: {int; struct {short, short}} is one INTEGER eightbyte; {int; struct {float, float}}
    // puts the inner struct's halves in different eightbytes.
    m[0] = abi_test_member(builder, BUSTER_TYPE_FLOAT);
    m[1] = abi_test_member(builder, BUSTER_TYPE_FLOAT);
    const TypeId pair = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1] = abi_test_member(builder, pair);
    abi_test_in_registers(builder, "{int, {float, float}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_SSE), 0, 0, rax, BUSTER_ABI_TEST_XMM(0));
    // An aligned typedef passes as its type: canonical types have no typedefs.
    abi_test_in_registers(builder, "int aligned(32)", type_aligned(table, BUSTER_TYPE_INT, 5), BUSTER_ABI_TEST_INTEGER_ONLY, 3u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT, 0, rax,
                          BUSTER_ABI_TEST_NONE);
}

// The location expected of an argument, in a call fixture.
typedef struct AbiTestLocation AbiTestLocation;
struct AbiTestLocation
{
    uint8_t kind;
    uint8_t first;
    uint8_t second;
    uint8_t flags;
    uint32_t stack_offset;
};

static bool abi_test_same_location(AbiLocation got, AbiTestLocation expected)
{
    return got.kind == expected.kind && got.registers[0] == expected.first && got.registers[1] == expected.second && got.flags == expected.flags &&
           got.stack_offset == expected.stack_offset;
}

// Calls whose locations clang and LLVM give: f(long x4, nullptr_t, struct {long, long}, long) splits the struct;
// f(long x5, __int128, long) puts the __int128 on the stack and the long in r9; and a result in memory shifts the
// general registers by one.
static void abi_test_calls(AbiTestTally* tally, AbiTestTables* tables, uint32_t vector_bytes, bool vector)
{
    TypeTable* table = &tables->types.table;
    AbiTestBuilder builder = {
        .table = table,
        .expectations = 0,
        .count = 0,
        .next_name = 10000,
        .vector_bytes = vector_bytes,
    };
    TypeMember m[2];
    m[0] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    const TypeId longs = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_bits(&builder, BUSTER_TYPE_LONG, 64, false);
    m[1] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    const TypeId high = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(&builder, BUSTER_TYPE_DOUBLE);
    const TypeId mixed = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    const TypeId big_members[1] = {
        type_array(table, BUSTER_TYPE_LONG, 4),
    };
    m[0] = abi_test_member(&builder, big_members[0]);
    const TypeId big = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    const TypeId atomic_double = type_atomic(table, BUSTER_TYPE_DOUBLE);
    if (vector)
    {
        abi_classify_all(&tables->abi, table);
    }
    else
    {
        abi_classify_all_scalar(&tables->abi, table);
    }
    const uint8_t none = BUSTER_ABI_REGISTER_NONE;
    const TypeId L = BUSTER_TYPE_LONG;
    const TypeId D = BUSTER_TYPE_DOUBLE;
    const TypeId arguments[] = {
        // 0: long x4, nullptr_t, {long, long}, long
        L, L, L, L, BUSTER_TYPE_NULLPTR, longs, L,
        // 7: long x5, __int128, long
        L, L, L, L, L, BUSTER_TYPE_INT128, L,
        // 14: long x5, nullptr_t, the high eightbyte alone, long
        L, L, L, L, L, BUSTER_TYPE_NULLPTR, high, L,
        // 22: double x7, _Atomic double, {long, double}, long
        D, D, D, D, D, D, D, atomic_double, mixed, L,
        // 32: a result in memory; long x6
        L, L, L, L, L, L,
    };
    const AbiTestLocation expected[] = {
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_SPLIT, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, BUSTER_ABI_FLAG_HIGH, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(0), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(1), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(2), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(3), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(4), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(5), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(6), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(7), none, 0, 0 },
        { BUSTER_ABI_LOCATION_SPLIT, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
    };
    const uint32_t counts[] = {
        7,
        7,
        8,
        10,
        6,
    };
    const uint32_t named[] = {
        7,
        7,
        8,
        10,
        6,
    };
    const TypeId results[] = {
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        big,
    };
    const AbiSignatures signatures = {
        .results = results,
        .counts = counts,
        .named = named,
        .count = BUSTER_ARRAY_LENGTH(counts),
        .arguments = arguments,
    };
    AbiLocation locations[BUSTER_ARRAY_LENGTH(arguments)];
    AbiCall calls[BUSTER_ARRAY_LENGTH(counts)];
    memset(locations, 0xa5, sizeof(locations));
    if (vector)
    {
        abi_assign(&tables->abi, signatures, locations, calls);
    }
    else
    {
        abi_assign_scalar(&tables->abi, signatures, locations, calls);
    }
    char detail[160];
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(arguments); i += 1)
    {
        snprintf(detail, sizeof(detail), "argument %u (%s, %u bytes): kind %u, registers %02x %02x, flags %u, offset %u", i, vector ? "vector" : "scalar", vector_bytes,
                 locations[i].kind, locations[i].registers[0], locations[i].registers[1], locations[i].flags, locations[i].stack_offset);
        abi_test_expect(tally, abi_test_same_location(locations[i], expected[i]), "fixtures", detail);
    }
    abi_test_expect(tally, calls[0].stack_bytes == 16 && calls[0].integer_registers == 6 && calls[0].vector_registers == 0, "fixtures", "call 0's totals");
    abi_test_expect(tally, calls[1].stack_bytes == 16 && calls[1].stack_alignment_log2 == 4 && calls[1].integer_registers == 6, "fixtures", "call 1's totals");
    abi_test_expect(tally, calls[3].stack_bytes == 8 && calls[3].vector_registers == 8 && calls[3].integer_registers == 2, "fixtures", "call 3's totals");
    abi_test_expect(tally, calls[4].result.kind == BUSTER_ABI_RESULT_MEMORY && calls[4].integer_registers == 6 && calls[4].stack_bytes == 8, "fixtures",
                    "call 4's hidden pointer");
}

static bool abi_test_fixtures(void)
{
    AbiTestTally tally = { 0 };
    const uint32_t widths[3] = {
        16,
        32,
        64,
    };
    AbiTestExpectation expectations[BUSTER_ABI_TEST_FIXTURES];
    char detail[256];
    for (uint32_t w = 0; w < 3; w += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            AbiTestTables tables;
            if (abi_test_expect(&tally, abi_test_tables_open(&tables, 4096), "fixtures", "the tables can't be made"))
            {
                AbiTestBuilder builder = {
                    .table = &tables.types.table,
                    .expectations = expectations,
                    .count = 0,
                    .next_name = 0,
                    .vector_bytes = widths[w],
                };
                abi_test_build_scalars(&builder);
                abi_test_build_vectors(&builder);
                abi_test_build_records(&builder);
                abi_test_expect(&tally, abi_test_tables_start(&tables, widths[w]), "fixtures", "the calling convention's table can't be made");
                if (path == 0)
                {
                    abi_classify_all(&tables.abi, &tables.types.table);
                }
                else
                {
                    abi_classify_all_scalar(&tables.abi, &tables.types.table);
                }
                for (uint32_t i = 0; i < builder.count; i += 1)
                {
                    const AbiTestExpectation e = expectations[i];
                    const uint32_t index = e.type >> BUSTER_TYPE_INDEX_SHIFT;
                    const uint32_t pass = tables.abi.pass[index];
                    const AbiReturn result = tables.abi.results[index];
                    const bool classified = e.type != 0 && (pass & BUSTER_ABI_PASS_CLASSIFIED) != 0;
                    const bool same = classified && abi_classes(&tables.abi, e.type, true) == e.named && abi_classes(&tables.abi, e.type, false) == e.unnamed &&
                                      (pass & e.set) == e.set && (pass & e.clear) == 0 && result.kind == e.result_kind &&
                                      (e.result_kind != BUSTER_ABI_RESULT_REGISTERS || (result.registers[0] == e.result[0] && result.registers[1] == e.result[1]));
                    snprintf(detail, sizeof(detail), "%s (%u bytes, %s): classes %02x/%02x, pass %08x, result %u (%02x %02x)", e.name, widths[w], path == 0 ? "vector" : "scalar",
                             abi_classes(&tables.abi, e.type, true), abi_classes(&tables.abi, e.type, false), pass, result.kind, result.registers[0], result.registers[1]);
                    abi_test_expect(&tally, same, "fixtures", detail);
                }
                abi_test_calls(&tally, &tables, widths[w], path == 0);
                abi_test_tables_close(&tables);
            }
        }
    }
    abi_test_report("fixtures", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Worlds: random types for the calling convention, in a TypeTestWorld (c_types_test.c) so that the cross-check can
// write them as C. Small aggregates mostly, which is where the classes are decided member by member; `gcc` leaves out
// what only clang spells (clang's ext_vector_type, __fp16, complex _BitInts) and what GCC passes otherwise (_Atomic
// types, flexible array members, vectors of 4 bytes or less of floating elements).

#define BUSTER_ABI_TEST_MEMBERS 8

typedef struct AbiTestWorld AbiTestWorld;
struct AbiTestWorld
{
    TypeTestWorld types;
    Arena text_arena;
    // The types a member or an argument may have: the pool's small ones and its others.
    TypeId* small;
    uint32_t small_count;
    TypeId* others;
    uint32_t other_count;
    uint32_t capacity;
    bool gcc;
};

static void abi_test_world_add(AbiTestWorld* world, TypeId type)
{
    const TypeTable* table = &world->types.test.table;
    if (type != 0 && type_is_complete(table, type))
    {
        const uint32_t index = abi_resolved_index(table, type);
        const uint8_t kind = table->types.kind[index];
        const bool object = kind != BUSTER_TYPE_KIND_VOID && kind != BUSTER_TYPE_KIND_FUNCTION && !type_kind_is_array(kind);
        if (object && table->types.size[index] <= 16 && world->small_count < world->capacity)
        {
            world->small[world->small_count] = type;
            world->small_count += 1;
        }
        else if (object && world->other_count < world->capacity)
        {
            world->others[world->other_count] = type;
            world->other_count += 1;
        }
        type_test_pool_add(&world->types.pool, type);
    }
}

static TypeId abi_test_world_pick(AbiTestWorld* world, uint64_t* random, bool small)
{
    const bool from_small = small || world->other_count == 0 || type_test_below(random, 5) != 0;
    return from_small ? world->small[type_test_below(random, world->small_count)] : world->others[type_test_below(random, world->other_count)];
}

static bool abi_test_world_open(AbiTestWorld* world, uint32_t capacity)
{
    TypeTestWorld* types = &world->types;
    *world = (AbiTestWorld){ 0 };
    types->arena = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    world->text_arena = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    types->pool = (TypeTestPool){
        .types = BUSTER_ARENA_PUSH_ARRAY(&types->arena, TypeId, capacity),
        .capacity = capacity,
    };
    types->anonymous = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint8_t, capacity);
    types->completed_at = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint32_t, capacity);
    types->enum_begin = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint32_t, capacity);
    types->enum_count = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint32_t, capacity);
    types->enum_fixed = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint8_t, capacity);
    types->enum_values = BUSTER_ARENA_PUSH_ARRAY(&types->arena, int64_t, capacity);
    types->enum_unsigned = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint8_t, capacity);
    types->completions = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint32_t, capacity);
    types->spellings = BUSTER_ARENA_PUSH_ARRAY(&types->arena, TypeTestSpelling, capacity);
    types->name_bytes = BUSTER_ARENA_PUSH_ARRAY(&types->arena, const uint8_t*, capacity);
    types->name_lengths = BUSTER_ARENA_PUSH_ARRAY(&types->arena, uint32_t, capacity);
    world->small = BUSTER_ARENA_PUSH_ARRAY(&types->arena, TypeId, capacity);
    world->others = BUSTER_ARENA_PUSH_ARRAY(&types->arena, TypeId, capacity);
    world->capacity = capacity;
    const bool memory = types->pool.types != 0 && types->anonymous != 0 && types->completed_at != 0 && types->enum_begin != 0 && types->enum_count != 0 &&
                        types->enum_fixed != 0 && types->enum_values != 0 && types->enum_unsigned != 0 && types->completions != 0 && types->spellings != 0 &&
                        types->name_bytes != 0 && types->name_lengths != 0 && world->small != 0 && world->others != 0 && world->text_arena.base != 0;
    const bool result = memory && type_test_table_open(&types->test, capacity - 64);
    if (result)
    {
        memset(types->anonymous, 0, capacity);
        memset(types->enum_fixed, 0, capacity);
        memset(types->spellings, 0, capacity * sizeof(TypeTestSpelling));
        for (uint32_t i = 0; i < capacity; i += 1)
        {
            types->completed_at[i] = UINT32_MAX;
            char name[16];
            const int length = snprintf(name, sizeof(name), "x%u", i);
            types->name_bytes[i] = (const uint8_t*)type_test_join(types, name, "", "");
            types->name_lengths[i] = (uint32_t)length;
        }
        types->names = (TypeNames){
            .bytes = types->name_bytes,
            .lengths = types->name_lengths,
            .count = capacity,
        };
    }
    return result;
}

static void abi_test_world_close(AbiTestWorld* world)
{
    type_test_table_close(&world->types.test);
    arena_release(&world->types.arena);
    arena_release(&world->text_arena);
}

// A record of random members, completed: small ones mostly, bit-fields, arrays of small types, earlier records, and
// now and then a flexible array member and attributes. Returns 0 if the table refused it.
static TypeId abi_test_world_record(AbiTestWorld* world, uint64_t* random, bool big)
{
    TypeTable* table = &world->types.test.table;
    TypeMember members[BUSTER_ABI_TEST_MEMBERS];
    const bool is_union = type_test_below(random, 4) == 0;
    const uint32_t count = (uint32_t)type_test_below(random, big ? BUSTER_ABI_TEST_MEMBERS + 1 : 5);
    for (uint32_t m = 0; m < count; m += 1)
    {
        const uint64_t pick = type_test_below(random, 16);
        TypeId type = abi_test_world_pick(world, random, !big);
        uint32_t width = BUSTER_TYPE_NOT_BIT_FIELD;
        if (pick < 3 && type_test_bit_field_type(table, type) && (type_qualifiers(type) & BUSTER_TYPE_CONST) == 0)
        {
            width = (uint32_t)type_test_below(random, type_integer_width(table, type) + 1);
        }
        else if (pick < 5)
        {
            const TypeId element = abi_test_world_pick(world, random, true);
            const uint64_t element_size = type_size(table, element);
            const uint64_t length = element_size == 0 ? type_test_below(random, 3) : type_test_below(random, element_size <= 4 ? 5 : 3);
            const TypeId array = type_array(table, type_unqualified(element), length);
            type = array != 0 ? array : type;
            table->last_error = TYPE_ERROR_NONE;
        }
        const bool unnamed = width == 0 || (width != BUSTER_TYPE_NOT_BIT_FIELD && type_test_below(random, 4) == 0);
        members[m] = (TypeMember){
            .name = unnamed ? 0 : type_test_new_name(&world->types),
            .type = type,
            .bit_width = width,
            .alignment_log2 = !unnamed && type_test_below(random, 16) == 0 ? (uint8_t)(1 + type_test_below(random, 5)) : 0,
            .packed = !unnamed && type_test_below(random, 16) == 0,
        };
    }
    uint32_t total = count;
    if (!world->gcc && !is_union && count != 0 && type_test_below(random, 16) == 0)
    {
        const TypeId element = abi_test_world_pick(world, random, true);
        const TypeId open = type_incomplete_array(table, type_unqualified(element));
        members[count - 1] = open != 0 ? type_test_member(type_test_new_name(&world->types), open, BUSTER_TYPE_NOT_BIT_FIELD) : members[count - 1];
        table->last_error = TYPE_ERROR_NONE;
    }
    const TypeRecordAttributes attributes = {
        .packed = type_test_below(random, 8) == 0,
        .pack_log2 = type_test_below(random, 10) == 0 ? (uint8_t)(1 + type_test_below(random, 4)) : 0,
        .alignment_log2 = type_test_below(random, 10) == 0 ? (uint8_t)(1 + type_test_below(random, 6)) : 0,
    };
    const TypeId record = type_tag(table, is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, type_test_new_name(&world->types));
    TypeId result = 0;
    if (record != 0 && type_complete_record(table, record, members, total, attributes))
    {
        type_test_world_completed(&world->types, record);
        result = record;
    }
    table->last_error = TYPE_ERROR_NONE;
    return result;
}

static void abi_test_world_build(AbiTestWorld* world, uint64_t seed, uint32_t count)
{
    uint64_t random = seed;
    TypeTable* table = &world->types.test.table;
    for (uint32_t code = BUSTER_TYPE_CODE_BOOL; code < BUSTER_TYPE_BUILTIN_COUNT; code += 1)
    {
        const bool complex = code >= BUSTER_TYPE_CODE_COMPLEX;
        const bool spellable = !complex || (BUSTER_TYPE_COMPLEX_SPELLABLE >> (code - BUSTER_TYPE_CODE_COMPLEX) & 1) != 0;
        const bool clang_only = code == BUSTER_TYPE_CODE_HALF || !spellable;
        if (type_is_valid(table, BUSTER_TYPE_BUILTIN(code)) && code != BUSTER_TYPE_CODE_NULLPTR && (!world->gcc || !clang_only))
        {
            abi_test_world_add(world, BUSTER_TYPE_BUILTIN(code));
        }
    }
    for (uint32_t i = 0; i < 12; i += 1)
    {
        type_test_world_enum(&world->types, &random);
        abi_test_world_add(world, world->types.pool.types[world->types.pool.count - 1]);
    }
    const uint32_t widths[] = {
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
        100,
        127,
        128,
        129,
        200,
        256,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        abi_test_world_add(world, type_bit_int(table, widths[i], false));
        abi_test_world_add(world, type_bit_int(table, widths[i], true));
    }
    const TypeId elements[] = {
        BUSTER_TYPE_CHAR,
        BUSTER_TYPE_UCHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_INT,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_DOUBLE,
        BUSTER_TYPE_FLOAT16,
        BUSTER_TYPE_INT128,
    };
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(elements); e += 1)
    {
        const bool floating = type_kind(table, elements[e]) == BUSTER_TYPE_KIND_FLOATING;
        for (uint64_t bytes = 2; bytes <= 64; bytes *= 2)
        {
            if (bytes >= type_size(table, elements[e]) && !(world->gcc && floating && bytes <= 4))
            {
                abi_test_world_add(world, type_vector_size(table, elements[e], bytes));
                table->last_error = TYPE_ERROR_NONE;
            }
        }
    }
    if (!world->gcc)
    {
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_FLOAT, 3, true));
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_INT, 3, true));
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_CHAR, 5, true));
        abi_test_world_add(world, type_complex(table, type_bit_int(table, 13, false)));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_INT));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_DOUBLE));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_INT128));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_LONG_DOUBLE));
    }
    abi_test_world_add(world, type_pointer(table, BUSTER_TYPE_VOID));
    abi_test_world_add(world, type_pointer(table, BUSTER_TYPE_DOUBLE));
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t pick = type_test_below(&random, 16);
        if (pick < 10)
        {
            abi_test_world_add(world, abi_test_world_record(world, &random, pick == 0));
        }
        else if (pick < 12)
        {
            const TypeId base = abi_test_world_pick(world, &random, true);
            abi_test_world_add(world, type_aligned(table, type_unqualified(base), (uint32_t)type_test_below(&random, 7)));
            table->last_error = TYPE_ERROR_NONE;
        }
        else if (pick < 13 && !world->gcc)
        {
            const TypeId base = abi_test_world_pick(world, &random, true);
            abi_test_world_add(world, type_atomic(table, type_unqualified(base)));
            table->last_error = TYPE_ERROR_NONE;
        }
        else if (pick < 14)
        {
            abi_test_world_add(world, type_pointer(table, abi_test_world_pick(world, &random, false)));
        }
        else
        {
            // A wide vector wrapped, alone or in a union with something smaller.
            const TypeId vector = type_vector_size(table, elements[type_test_below(&random, 7)], 16u << type_test_below(&random, 3));
            TypeMember members[2] = {
                type_test_member(type_test_new_name(&world->types), vector, BUSTER_TYPE_NOT_BIT_FIELD),
                type_test_member(type_test_new_name(&world->types), abi_test_world_pick(world, &random, true), BUSTER_TYPE_NOT_BIT_FIELD),
            };
            const bool is_union = type_test_below(&random, 2) == 0;
            const TypeId record = vector != 0 ? type_tag(table, is_union ? BUSTER_TYPE_KIND_UNION : BUSTER_TYPE_KIND_STRUCT, type_test_new_name(&world->types)) : 0;
            if (record != 0 && type_complete_record(table, record, members, is_union ? 2 : 1, (TypeRecordAttributes){ 0 }))
            {
                type_test_world_completed(&world->types, record);
                abi_test_world_add(world, record);
            }
            table->last_error = TYPE_ERROR_NONE;
        }
    }
}

// A record's members as the table has them, a level deep, for a failure's report.
static void abi_test_dump(const TypeTable* table, const AbiTable* abi, uint32_t index, uint32_t depth)
{
    const uint32_t tag = table->types.index[index];
    const uint32_t begin = table->tags.member_begin[tag];
    const uint32_t end = begin + table->tags.member_count[tag];
    fprintf(stderr, "%*stype %u: kind %u, size %" PRIu64 ", alignment %u, flags %02x, row", depth * 4, "", index, table->types.kind[index], table->types.size[index],
            1u << table->types.alignment_log2[index], table->types.flags[index]);
    for (uint32_t o = 0; o < BUSTER_ABI_ROW_BYTES; o += 1)
    {
        fprintf(stderr, " %02x", abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES + o]);
    }
    fprintf(stderr, "\n");
    for (uint32_t member = begin; member < end; member += 1)
    {
        const uint32_t type = table->members.type[member] >> BUSTER_TYPE_INDEX_SHIFT;
        const uint32_t resolved = abi_resolved_index(table, table->members.type[member]);
        fprintf(stderr, "%*smember at bit %" PRIu64 ": type %u (kind %u, resolved %u kind %u), size %" PRIu64 ", alignment %u, canonical %u, width %u, name %u, row", depth * 4 + 2, "",
                table->members.offset[member], type, table->types.kind[type], resolved, table->types.kind[resolved], table->types.size[resolved],
                1u << table->types.alignment_log2[type], 1u << abi->alignment_log2[type], table->members.bit_width[member], table->members.name[member]);
        for (uint32_t o = 0; o < BUSTER_ABI_ROW_BYTES; o += 1)
        {
            fprintf(stderr, " %02x", abi->rows[(uint64_t)type * BUSTER_ABI_ROW_BYTES + o]);
        }
        fprintf(stderr, "\n");
        if (table->types.kind[type] == BUSTER_TYPE_KIND_ARRAY)
        {
            const uint32_t element = table->types.base[type] >> BUSTER_TYPE_INDEX_SHIFT;
            fprintf(stderr, "%*selement: type %u (kind %u), count %" PRIu64 ", size %" PRIu64 ", alignment %u, canonical %u\n", depth * 4 + 4, "", element,
                    table->types.kind[element], table->types.extent[type], table->types.size[abi_resolved_index(table, table->types.base[type])],
                    1u << table->types.alignment_log2[element], 1u << abi->alignment_log2[element]);
        }
        if (depth == 0 && (table->types.kind[resolved] == BUSTER_TYPE_KIND_STRUCT || table->types.kind[resolved] == BUSTER_TYPE_KIND_UNION))
        {
            abi_test_dump(table, abi, resolved, depth + 1);
        }
    }
}

// Every classifiable type, by the vector path and by the twin, at each vector width: identical tables, and every type at
// every offset it can be at the oracle's classes.
static bool abi_test_worlds(uint64_t seed, uint32_t count)
{
    AbiTestTally tally = { 0 };
    uint64_t compared = 0;
    AbiTestFrame* frames = malloc(sizeof(AbiTestFrame) * BUSTER_ABI_TEST_FRAMES);
    char detail[256];
    for (uint32_t round = 0; frames != 0 && round < 4; round += 1)
    {
        AbiTestWorld world;
        if (abi_test_expect(&tally, abi_test_world_open(&world, count * 8 + 4096), "worlds", "the world can't be made"))
        {
            world.gcc = round == 3;
            abi_test_world_build(&world, seed + round, count);
            type_test_spell_world(&world.types);
            const TypeTable* table = &world.types.test.table;
            const uint32_t width = 16u << (round % 3);
            Arena vector_arena = arena_reserve(abi_capacity_bytes(table->types.capacity));
            Arena scalar_arena = arena_reserve(abi_capacity_bytes(table->types.capacity));
            AbiTable vector;
            AbiTable scalar;
            const bool made = abi_initialize(&vector, &vector_arena, table, width) && abi_initialize(&scalar, &scalar_arena, table, width);
            if (abi_test_expect(&tally, made, "worlds", "the tables can't be made"))
            {
                const uint32_t vector_count = abi_classify_all(&vector, table);
                const uint32_t scalar_count = abi_classify_all_scalar(&scalar, table);
                abi_test_expect(&tally, vector_count == scalar_count && vector_count != 0, "worlds", "the paths classified different counts");
                for (uint32_t index = 1; index < table->types.count; index += 1)
                {
                    const bool classified = (vector.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0;
                    const bool same = memcmp(vector.rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES, scalar.rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES,
                                             BUSTER_ABI_ROW_BYTES) == 0 &&
                                      vector.pass[index] == scalar.pass[index] && vector.stack_bytes[index] == scalar.stack_bytes[index] &&
                                      vector.alignment_log2[index] == scalar.alignment_log2[index] &&
                                      memcmp(&vector.results[index], &scalar.results[index], sizeof(AbiReturn)) == 0;
                    snprintf(detail, sizeof(detail), "type %u (kind %u): the vector path and the twin differ: pass %08x/%08x, stack %u/%u, alignment %u/%u, row[0] %02x/%02x",
                             index, table->types.kind[index], vector.pass[index], scalar.pass[index], vector.stack_bytes[index], scalar.stack_bytes[index],
                             vector.alignment_log2[index], scalar.alignment_log2[index], vector.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES],
                             scalar.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES]);
                    if (classified && !same && tally.failures < BUSTER_ABI_TEST_REPORTED)
                    {
                        abi_test_dump(table, &vector, index, 0);
                        abi_test_dump(table, &scalar, index, 0);
                    }
                    abi_test_expect(&tally, !classified || same, "worlds", detail);
                    abi_test_expect(&tally, classified == abi_classifiable(table, index), "worlds", "a classifiable type left out, or the reverse");
                    const uint64_t size = table->types.size[abi_resolved_index(table, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT)];
                    for (uint32_t offset = 0; classified && offset < BUSTER_ABI_ROW_BYTES; offset += 1)
                    {
                        if (offset == 0 || offset + size <= BUSTER_ABI_ROW_BYTES)
                        {
                            const AbiClasses expected = abi_test_classify(table, width, true, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, (uint64_t)offset * 8, frames);
                            const AbiClasses got = vector.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES + offset];
                            if (got != expected)
                            {
                                const uint64_t length = world.types.arena.length;
                                const String8 printed = type_print(table, &world.types.arena, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, (String8){ 0 }, &world.types.names, 0);
                                snprintf(detail, sizeof(detail), "%.*s (type %u, %" PRIu64 " bytes) at %u, %u-byte vectors: %02x, the oracle %02x", (int)printed.length,
                                         printed.pointer, index, size, offset, width, got, expected);
                                if (tally.failures < BUSTER_ABI_TEST_REPORTED && (table->types.kind[index] == BUSTER_TYPE_KIND_STRUCT || table->types.kind[index] == BUSTER_TYPE_KIND_UNION))
                                {
                                    TypeTestText text = {
                                        .arena = &world.text_arena,
                                        .valid = true,
                                    };
                                    type_test_write_tag(&world.types, &text, table->types.index[index]);
                                    fprintf(stderr, "%.*s", (int)text.length, text.bytes);
                                    arena_pop_to(&world.text_arena, 0);
                                    abi_test_dump(table, &vector, index, 0);
                                }
                                arena_pop_to(&world.types.arena, length);
                            }
                            abi_test_expect(&tally, got == expected, "worlds", detail);
                            compared += 1;
                        }
                    }
                    if (classified)
                    {
                        // A variadic argument of more than 16 bytes that is SSE, SSEUP without a vector deciding it (a union of
                        // a __float128 and a long array) clang classifies so, but its call puts it on the stack and its va_arg
                        // reads it from the save area: the module passes it in memory, as GCC does.
                        AbiClasses unnamed = abi_test_classify(table, width, false, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, 0, frames);
                        unnamed = unnamed == BUSTER_ABI_TEST_VECTOR && size > BUSTER_ABI_REGISTER_BYTES ? BUSTER_ABI_IN_MEMORY : unnamed;
                        snprintf(detail, sizeof(detail), "type %u unnamed: %02x, the oracle %02x", index, abi_classes(&vector, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, false),
                                 unnamed);
                        abi_test_expect(&tally, abi_classes(&vector, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT, false) == unnamed, "worlds", detail);
                    }
                }
            }
            arena_release(&vector_arena);
            arena_release(&scalar_arena);
            abi_test_world_close(&world);
        }
    }
    free(frames);
    printf("%-34s %10" PRIu64 " offsets compared with the oracle\n", "worlds", compared);
    abi_test_report("worlds", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Assignment: random calls over a world's types, by the vector path and by the twin.

// A type an argument or a result may have: not an array or a function, not UNSUPPORTED, nor too large to pass.
static bool abi_test_passable(const AbiTable* abi, const TypeTable* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint8_t kind = table->types.kind[abi_resolved_index(table, type)];
    return (abi->pass[index] & (BUSTER_ABI_PASS_CLASSIFIED | BUSTER_ABI_PASS_UNSUPPORTED)) == BUSTER_ABI_PASS_CLASSIFIED && abi->stack_bytes[index] <= (1u << 16) &&
           kind != BUSTER_TYPE_KIND_VOID && !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION;
}

// Random calls: their results (void now and then), counts (mostly small, a few long ones past the registers), named
// counts (a fifth variadic) and arguments, over `types`.
typedef struct AbiTestCalls AbiTestCalls;
struct AbiTestCalls
{
    TypeId* results;
    uint32_t* counts;
    uint32_t* named;
    TypeId* arguments;
    uint32_t count;
    uint64_t argument_count;
};

static void abi_test_random_calls(AbiTestCalls* calls, const TypeId* types, uint32_t type_count, uint32_t count, uint64_t capacity, uint64_t* random)
{
    calls->count = 0;
    calls->argument_count = 0;
    for (uint32_t c = 0; c < count; c += 1)
    {
        const uint64_t shape = type_test_below(random, 16);
        uint32_t arguments = (uint32_t)(shape < 12 ? type_test_below(random, 5) : shape < 15 ? 4 + type_test_below(random, 12) : 16 + type_test_below(random, 40));
        arguments = calls->argument_count + arguments <= capacity ? arguments : 0;
        calls->results[c] = type_test_below(random, 4) == 0 ? BUSTER_TYPE_VOID : types[type_test_below(random, type_count)];
        calls->counts[c] = arguments;
        calls->named[c] = type_test_below(random, 5) == 0 ? (uint32_t)type_test_below(random, arguments + 1) : arguments;
        for (uint32_t a = 0; a < arguments; a += 1)
        {
            calls->arguments[calls->argument_count + a] = types[type_test_below(random, type_count)];
        }
        calls->argument_count += arguments;
        calls->count += 1;
    }
}

static AbiSignatures abi_test_signatures(const AbiTestCalls* calls)
{
    const AbiSignatures result = {
        .results = calls->results,
        .counts = calls->counts,
        .named = calls->named,
        .count = calls->count,
        .arguments = calls->arguments,
    };
    return result;
}

// Assigns `calls` by both paths and compares every location and call.
static void abi_test_compare_assignment(AbiTestTally* tally, const AbiTable* abi, AbiSignatures signatures, uint64_t argument_count, AbiLocation* vector_locations,
                                        AbiLocation* scalar_locations, AbiCall* vector_calls, AbiCall* scalar_calls, const char* what)
{
    char detail[256];
    memset(vector_locations, 0x5a, argument_count * sizeof(AbiLocation));
    memset(scalar_locations, 0xa5, argument_count * sizeof(AbiLocation));
    abi_assign(abi, signatures, vector_locations, vector_calls);
    abi_assign_scalar(abi, signatures, scalar_locations, scalar_calls);
    uint64_t first = 0;
    for (uint32_t c = 0; c < signatures.count; c += 1)
    {
        const bool same_call = memcmp(&vector_calls[c], &scalar_calls[c], sizeof(AbiCall)) == 0;
        snprintf(detail, sizeof(detail), "%s: call %u's totals: stack %u/%u, alignment %u/%u, registers %u/%u and %u/%u", what, c, vector_calls[c].stack_bytes,
                 scalar_calls[c].stack_bytes, vector_calls[c].stack_alignment_log2, scalar_calls[c].stack_alignment_log2, vector_calls[c].integer_registers,
                 scalar_calls[c].integer_registers, vector_calls[c].vector_registers, scalar_calls[c].vector_registers);
        abi_test_expect(tally, same_call, "assignment", detail);
        for (uint32_t a = 0; a < signatures.counts[c]; a += 1)
        {
            const AbiLocation v = vector_locations[first + a];
            const AbiLocation s = scalar_locations[first + a];
            snprintf(detail, sizeof(detail), "%s: call %u argument %u: kind %u/%u, registers %02x %02x/%02x %02x, flags %u/%u, offset %u/%u", what, c, a, v.kind, s.kind,
                     v.registers[0], v.registers[1], s.registers[0], s.registers[1], v.flags, s.flags, v.stack_offset, s.stack_offset);
            abi_test_expect(tally, memcmp(&v, &s, sizeof(AbiLocation)) == 0, "assignment", detail);
        }
        first += signatures.counts[c];
    }
}

static bool abi_test_assignment(uint64_t seed, uint32_t count)
{
    AbiTestTally tally = { 0 };
    uint64_t random = seed * 0x9E3779B97F4A7C15ull + 1;
    const uint64_t capacity = (uint64_t)count * 64;
    AbiTestCalls calls = {
        .results = malloc(sizeof(TypeId) * count),
        .counts = malloc(sizeof(uint32_t) * count),
        .named = malloc(sizeof(uint32_t) * count),
        .arguments = malloc(sizeof(TypeId) * capacity),
    };
    AbiLocation* vector_locations = malloc(sizeof(AbiLocation) * capacity);
    AbiLocation* scalar_locations = malloc(sizeof(AbiLocation) * capacity);
    AbiCall* vector_calls = malloc(sizeof(AbiCall) * count);
    AbiCall* scalar_calls = malloc(sizeof(AbiCall) * count);
    TypeId* passable = 0;
    uint64_t arguments = 0;
    const bool memory = calls.results != 0 && calls.counts != 0 && calls.named != 0 && calls.arguments != 0 && vector_locations != 0 && scalar_locations != 0 &&
                        vector_calls != 0 && scalar_calls != 0;
    for (uint32_t round = 0; memory && round < 3; round += 1)
    {
        AbiTestWorld world;
        if (abi_test_expect(&tally, abi_test_world_open(&world, count * 4 + 4096), "assignment", "the world can't be made"))
        {
            abi_test_world_build(&world, seed + 16 + round, count / 2);
            const TypeTable* table = &world.types.test.table;
            Arena arena = arena_reserve(abi_capacity_bytes(table->types.capacity));
            AbiTable abi;
            passable = malloc(sizeof(TypeId) * table->types.count * 2);
            if (abi_test_expect(&tally, passable != 0 && abi_initialize(&abi, &arena, table, 16u << round), "assignment", "the table can't be made"))
            {
                abi_classify_all(&abi, table);
                uint32_t passable_count = 0;
                for (uint32_t index = 1; index < table->types.count; index += 1)
                {
                    const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
                    if (abi_test_passable(&abi, table, type))
                    {
                        passable[passable_count] = type;
                        passable[passable_count + 1] = type | (uint32_t)type_test_below(&random, 4);
                        passable_count += 2;
                    }
                }
                abi_test_random_calls(&calls, passable, passable_count, count, capacity, &random);
                abi_test_compare_assignment(&tally, &abi, abi_test_signatures(&calls), calls.argument_count, vector_locations, scalar_locations, vector_calls, scalar_calls,
                                            "random calls");
                arguments += calls.argument_count;
                // Every prefix of the arguments ending at a PROT_NONE page: one call of them, then the locations too.
                const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
                uint8_t* guarded = mmap(0, 4 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (abi_test_expect(&tally, guarded != MAP_FAILED && mprotect(guarded + page, page, PROT_NONE) == 0 && mprotect(guarded + 3 * page, page, PROT_NONE) == 0,
                                    "assignment", "the guard pages can't be made"))
                {
                    for (uint32_t n = 0; n <= 40 && n <= calls.argument_count; n += 1)
                    {
                        TypeId* edge_arguments = (TypeId*)(guarded + page) - n;
                        AbiLocation* edge_locations = (AbiLocation*)(guarded + 3 * page) - n;
                        memcpy(edge_arguments, calls.arguments, n * sizeof(TypeId));
                        const TypeId result = BUSTER_TYPE_VOID;
                        const uint32_t named = n;
                        const AbiSignatures one = {
                            .results = &result,
                            .counts = &n,
                            .named = &named,
                            .count = 1,
                            .arguments = edge_arguments,
                        };
                        AbiCall edge_call;
                        abi_assign(&abi, one, edge_locations, &edge_call);
                        abi_assign_scalar(&abi, one, scalar_locations, scalar_calls);
                        abi_test_expect(&tally, memcmp(edge_locations, scalar_locations, n * sizeof(AbiLocation)) == 0 && memcmp(&edge_call, scalar_calls, sizeof(AbiCall)) == 0,
                                        "assignment", "a call ending at a page edge");
                    }
                    munmap(guarded, 4 * page);
                }
            }
            free(passable);
            arena_release(&arena);
            abi_test_world_close(&world);
        }
    }
    free(calls.results);
    free(calls.counts);
    free(calls.named);
    free(calls.arguments);
    free(vector_locations);
    free(scalar_locations);
    free(vector_calls);
    free(scalar_calls);
    printf("%-34s %10" PRIu64 " arguments assigned by both paths\n", "assignment", arguments);
    abi_test_report("assignment", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

int run_calling_convention_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t count = BUSTER_ABI_TEST_DEFAULT_COUNT;
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
            valid = valid && type_test_parse_u64(argv[i + 1], &count) && count >= 16 && count <= (1u << 20);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: test-calling-convention [--seed N] [--count N]\n");
    }
    else
    {
        bool passed = abi_test_merge();
        passed = abi_test_fixtures() && passed;
        passed = abi_test_worlds(seed, (uint32_t)count) && passed;
        passed = abi_test_assignment(seed, (uint32_t)count) && passed;
        result = passed ? 0 : 1;
    }
    return result;
}
