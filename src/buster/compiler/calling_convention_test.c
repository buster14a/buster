// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The calling convention's tests, for the test-calling-convention command:
//
//   build/compiler test-calling-convention [--seed N] [--count N] [--cross-check] [--signatures N] [--clang PATH]
//                                          [--gcc PATH] [--keep]
//
// main.c includes this file after calling_convention.c, c_types_test.c (whose tables, random numbers, worlds of types
// and C spellings it uses), link_program.c (its instruction builders) and object_writer.c, and calls
// run_calling_convention_tests. The checks, in this order:
//   - Merge: clang's merge and post-merge, written again here as clang writes them, against the module's on every pair
//     of classes and both kinds of size.
//   - Fixtures: types whose classes, passing, slots and results clang's IR and LLVM's fixed stack objects show
//     (abi_test_fixtures says which; abi_test_build_atomics the LARGER, GROWN and refused _Atomic types and what holds
//     them; abi_test_build_split_vectors the vectors LLVM breaks up or packs that aren't _Atomic, and records of them),
//     at each target vector width, by the vector path and by the twin; calls whose locations clang and LLVM give (an
//     UNCOUNTED argument's shift, the LARGER copies' slots, a SMALLER one's and a DEMOTED result among them); the
//     levels across calls (abi_test_ordering); the walk without scratch memory for its stack (abi_test_short_scratch);
//     the walk over records that 2^64 paths lead to, under an alarm (abi_test_shared_records); and plain char's
//     extension, and an enum's of a fixed underlying char, with each signedness of char (abi_test_char_signedness).
//   - Worlds: random worlds of types (abi_test_world_build: scalars, vectors, complex types, _BitInts, _Atomic and
//     aligned types, _Atomics of structs of no size, and records and unions of them, small ones mostly, with
//     bit-fields, arrays, nesting, packing and alignment, and zero-length arrays of large records), classified by the
//     vector path in one table and by the twin in another, which must end up identical, and each type at each offset it
//     can be at against the oracle: clang's classify transcribed as clang has it, its recursion over members as a stack
//     of frames, from the type table alone (no rows). Then the twin classifies every type again at once, which must
//     leave its table as it was: the paths share the levels, and in the rootless world (AbiTestWorld.rootless), whose
//     levels only the waits order, this shows a type the levels took before what it reads.
//   - Assignment: random calls over a world's types, named and variadic, with results in memory and in registers,
//     assigned by the vector path and by the twin, which must agree on every location and total; calls around the
//     vector path's most arguments (abi_test_long_calls); and the vector path with its argument and location columns
//     ending at a PROT_NONE page.
//   - With --cross-check, against clang and GCC (abi_test_cross_check): for each compiler and target vector width (16,
//     32 and 64 bytes, the C built with -march=x86-64, x86-64-v3 and x86-64-v4), a world and --signatures signatures
//     (400) over its passable types, random but for clang's first few, which pass runs of bit-fields where its count of
//     registers runs ahead of LLVM's (abi_test_world_directed), written as C (the types, every argument's and result's
//     bytes, a C callee and a C caller of ours) and as our side (a caller of the C callee and a callee: X86Instruction
//     records through x86_encode, link_add_code_fixups and the object writer), linked by the compiler's driver and run:
//     each side checks every argument, result, hidden pointer, al and va_arg value against the bytes. GCC's worlds
//     leave out what GCC lays out or passes otherwise (each named where it is left out, GCC's own bugs among them), and
//     clang's a type in registers that holds an array of elements of no size, which crashes it now and then
//     (abi_test_empty_arrays), one with an SSE low eightbyte and bit-fields in an INTEGER high one, whose access units
//     the module doesn't work out yet (abi_test_bit_field_bytes), an _Atomic vector of bools, whose atomic loads crash
//     it, a record SSE, SSEUP of a size other than 16, 32 or 64 and one SSE, SSE that holds a __float128, which the
//     module doesn't model yet either, and a union of a vector LLVM packs into fewer bits than its bytes, whose copies
//     lose the other members' bits (abi_test_holds); an argument clang would pass as an i0 becomes a long, as does an
//     unnamed one clang's va_arg reads elsewhere than its call puts it (abi_test_va_disagreement); bytes clang doesn't
//     pass aren't checked (abi_test_mask_unpassed); and the C side copies a SMALLER type's LLVM bytes, which are all
//     clang's objects of it hold (abi_test_c_size). Then the extension runs, with plain char signed and unsigned
//     (abi_test_cross_extension): the types clang extends, passed and returned between clang's code and the records'
//     and this compiler's, both ways, at values with bits set past the type's.
// The exit status is 1 if the arguments are invalid or a check fails.
// With --cross-check, bounded child processes also check capture ownership with closed standard descriptors and
// descriptor exhaustion, before running the ordinary six configurations.
//
// Layout, in this order: the tally and helpers; merge; the oracle; fixtures; worlds; assignment; the cross-check;
// run_calling_convention_tests.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

// OS-dependent includes
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/memfd.h>

#define BUSTER_ABI_TEST_REPORTED 12
#define BUSTER_ABI_TEST_DEFAULT_COUNT 2000
#define BUSTER_ABI_TEST_DEFAULT_SIGNATURES 400
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

// What clang's getTypeAlign gives a field's canonical type: ALIGNED looked through, arrays' elements too, and an _Atomic
// type's from its canonical value, without the typedefs (getTypeInfo's AtomicType case): the value's alignment if it has
// no size or more than 16 bytes, and otherwise its size rounded up to a power of two. An aligned(16) typedef of an empty
// struct makes an _Atomic aligned to 16 in C, and to 1 canonically.
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
    uint64_t result = 1ull << table->types.alignment_log2[index];
    if (table->types.kind[index] == BUSTER_TYPE_KIND_ATOMIC)
    {
        uint32_t value = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        for (uint32_t i = 0; i < 64 && table->types.kind[value] == BUSTER_TYPE_KIND_ALIGNED; i += 1)
        {
            value = table->types.base[value] >> BUSTER_TYPE_INDEX_SHIFT;
        }
        const uint64_t size = table->types.size[value];
        uint64_t width = 1;
        while (width < size)
        {
            width *= 2;
        }
        result = size == 0 || size > 16 ? 1ull << table->types.alignment_log2[value] : width;
    }
    return result;
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
#define BUSTER_ABI_TEST_FIXTURES 256
// A result kind the fixture doesn't check: an UNSUPPORTED type's, which the code generator refuses.
#define BUSTER_ABI_TEST_RESULT_ANY 0xff

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
    // With detail, the result's flags, the bytes each of its registers holds and its third register must be these too.
    bool detail;
    uint8_t result_flags;
    uint8_t part_bytes;
    uint8_t third;
    // The stack slot's bytes it must have, or 0 for any.
    uint32_t stack_bytes;
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
            .detail = false,
            .result_flags = 0,
            .part_bytes = 0,
            .third = BUSTER_ABI_TEST_NONE,
            .stack_bytes = 0,
        };
        builder->count += 1;
    }
}

// The last expectation's stack slot.
static void abi_test_slot(AbiTestBuilder* builder, uint32_t stack_bytes)
{
    if (builder->count != 0 && builder->count <= BUSTER_ABI_TEST_FIXTURES)
    {
        builder->expectations[builder->count - 1].stack_bytes = stack_bytes;
    }
}

// The last expectation's result in detail: its flags, the bytes each register holds and its third register.
static void abi_test_result_detail(AbiTestBuilder* builder, uint8_t flags, uint8_t part_bytes, uint8_t third)
{
    if (builder->count != 0 && builder->count <= BUSTER_ABI_TEST_FIXTURES)
    {
        AbiTestExpectation* expectation = &builder->expectations[builder->count - 1];
        expectation->detail = true;
        expectation->result_flags = flags;
        expectation->part_bytes = part_bytes;
        expectation->third = third;
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

// A vector in memory as an argument: a byval copy of `slot` bytes (SMALLER when that is less than the C type's),
// aligned to 2^alignment_log2, MEMORY's result or the registers given, and not refused.
static void abi_test_vector_copy(AbiTestBuilder* builder, const char* name, TypeId type, uint32_t slot, uint32_t alignment_log2, uint8_t first, uint8_t second)
{
    const uint32_t alignment = alignment_log2 << BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
    const uint32_t smaller = slot < type_size(builder->table, type) ? BUSTER_ABI_PASS_SMALLER : 0;
    const uint8_t kind = first == BUSTER_ABI_TEST_NONE ? BUSTER_ABI_RESULT_MEMORY : BUSTER_ABI_RESULT_REGISTERS;
    abi_test_expectation(builder, name, type, BUSTER_ABI_IN_MEMORY, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE | alignment | smaller,
                         BUSTER_ABI_PASS_UNSUPPORTED | BUSTER_ABI_PASS_UNCOUNTED | (BUSTER_ABI_PASS_ALIGNMENT & ~alignment) | (BUSTER_ABI_PASS_SMALLER & ~smaller), kind,
                         first, second);
    abi_test_slot(builder, slot);
}

// A vector clang passes as its LLVM type and LLVM breaks up, refused: in one vector register by its classes, a wide one
// past 16 bytes.
static void abi_test_vector_refused(AbiTestBuilder* builder, const char* name, TypeId type, uint8_t result_kind, uint8_t first, uint8_t second)
{
    const uint32_t wide = type_size(builder->table, type) > BUSTER_ABI_REGISTER_BYTES ? BUSTER_ABI_PASS_WIDE : 0;
    abi_test_expectation(builder, name, type, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_UNSUPPORTED | wide, BUSTER_ABI_PASS_MEMORY | (BUSTER_ABI_PASS_WIDE & ~wide),
                         result_kind, first, second);
}

// Vectors LLVM breaks up that aren't _Atomic ones (abi_vector_llvm), as clang 23's IR and LLVM's registers and fixed stack
// objects give them at -march=x86-64, x86-64-v3 and x86-64-v4. Bools are an integer of their bits in memory: an i8 leaf
// for 8 of them, so that {__bf16, __bf16, _Alignas(8) bool ext8} widens its low part into rdi, which ext9's i9 doesn't, and
// an i128 for 128, which makes a union of it and a long one PAIR; from 16 bytes up LLVM passes each element as a value of
// its own (refused, and so is a record of one such member, but not a union of it and another vector), copies 48 bytes
// of 257 and 80 of 513, and returns them through a hidden pointer. _BitInt(4)s are packed in memory and a byte each in a
// register; long doubles are packed in memory and f80s on the stack (st0, and st1 10 bytes on, as results); __float128s an
// xmm register each; _BitInt(256)s i64s; and a vector in memory whose length isn't a power of two is returned an element
// at a time: three longs in rax, rdx and rcx, three doubles in xmm0, xmm1 and st0, five floats or doubles through a
// hidden pointer.
static void abi_test_build_split_vectors(AbiTestBuilder* builder)
{
    TypeTable* table = builder->table;
    const uint32_t wide = builder->vector_bytes;
    const uint8_t rax = BUSTER_ABI_RAX;
    const uint8_t rdx = BUSTER_ABI_RDX;
    const uint8_t none = BUSTER_ABI_TEST_NONE;
    const uint8_t any = BUSTER_ABI_TEST_RESULT_ANY;
    const uint8_t hidden = BUSTER_ABI_RESULT_MEMORY;
    const uint8_t demoted = BUSTER_ABI_FLAG_DEMOTED;
    const uint8_t st0 = BUSTER_ABI_TEST_ST(0);
    const uint8_t st1 = BUSTER_ABI_TEST_ST(1);
    const uint8_t xmm0 = BUSTER_ABI_TEST_XMM(0);
    const uint8_t xmm1 = BUSTER_ABI_TEST_XMM(1);
    const uint32_t refused = BUSTER_ABI_PASS_UNSUPPORTED;
    // Bools.
    const TypeId bits8 = type_vector(table, BUSTER_TYPE_BOOL, 8, true);
    const TypeId bits9 = type_vector(table, BUSTER_TYPE_BOOL, 9, true);
    const TypeId bits65 = type_vector(table, BUSTER_TYPE_BOOL, 65, true);
    const TypeId bits100 = type_vector(table, BUSTER_TYPE_BOOL, 100, true);
    const TypeId bits128 = type_vector(table, BUSTER_TYPE_BOOL, 128, true);
    const TypeId bits256 = type_vector(table, BUSTER_TYPE_BOOL, 256, true);
    abi_test_in_registers(builder, "bool ext8", bits8, BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_LOW_INTEGER, refused | BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO, rax, none);
    abi_test_in_registers(builder, "bool ext64", type_vector(table, BUSTER_TYPE_BOOL, 64, true), BUSTER_ABI_TEST_SSE_ONLY, BUSTER_ABI_PASS_LOW_SSE, refused, xmm0, none);
    abi_test_vector_refused(builder, "bool ext65", bits65, hidden, none, none);
    abi_test_result_detail(builder, demoted, 0, none);
    const TypeId bits129 = type_vector(table, BUSTER_TYPE_BOOL, 129, true);
    const TypeId bits257 = type_vector(table, BUSTER_TYPE_BOOL, 257, true);
    if (wide >= 32)
    {
        abi_test_vector_refused(builder, "bool ext129", bits129, hidden, none, none);
    }
    else
    {
        abi_test_vector_copy(builder, "bool ext129", bits129, 32, 5, none, none);
    }
    abi_test_result_detail(builder, demoted, 0, none);
    if (wide == 64)
    {
        abi_test_vector_refused(builder, "bool ext257", bits257, hidden, none, none);
    }
    else
    {
        abi_test_vector_copy(builder, "bool ext257", bits257, 48, 6, none, none);
    }
    abi_test_result_detail(builder, demoted, 0, none);
    abi_test_vector_copy(builder, "bool ext513", type_vector(table, BUSTER_TYPE_BOOL, 513, true), 80, 7, none, none);
    abi_test_result_detail(builder, demoted, 0, none);
    TypeMember m[3];
    m[0] = abi_test_member(builder, bits128);
    const TypeId bits128_struct = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    abi_test_vector_refused(builder, "struct {bool ext128}", bits128_struct, hidden, none, none);
    abi_test_result_detail(builder, demoted, 0, none);
    abi_test_vector_refused(builder, "union {bool ext128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 1, false, 0), hidden, none, none);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_INT, 0));
    abi_test_vector_refused(builder, "struct {bool ext128, int[0]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), hidden, none, none);
    m[1] = abi_test_member(builder, abi_test_vector(builder, BUSTER_TYPE_FLOAT, 16));
    abi_test_in_registers(builder, "union {bool ext128, float x4}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_VECTOR,
                          BUSTER_ABI_PASS_VECTOR, refused, xmm0, none);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    abi_test_in_registers(builder, "union {bool ext128, long}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_SSE), BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_HIGH_INTEGER,
                          refused | BUSTER_ABI_PASS_HIGH_SSE, rax, rdx);
    m[0] = abi_test_member(builder, bits100);
    abi_test_in_registers(builder, "union {bool ext100, long}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_SSE), BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_HIGH_SSE, refused | BUSTER_ABI_PASS_PAIR,
                          rax, xmm0);
    m[0] = abi_test_member(builder, type_array(table, bits65, 1));
    abi_test_vector_refused(builder, "struct {bool ext65[1]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), hidden, none, none);
    m[0] = abi_test_member(builder, bits128_struct);
    abi_test_vector_refused(builder, "struct {struct {bool ext128}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), hidden, none, none);
    abi_test_result_detail(builder, demoted, 0, none);
    m[0] = abi_test_member(builder, BUSTER_TYPE_BF16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_BF16);
    m[2] = abi_test_member(builder, bits8);
    m[2].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, __bf16, _Alignas(8) bool ext8}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_LOW_GENERAL | BUSTER_ABI_PASS_HIGH_INTEGER, refused, rax, rdx);
    m[2] = abi_test_member(builder, bits9);
    m[2].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, __bf16, _Alignas(8) bool ext9}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_LOW_SSE | BUSTER_ABI_PASS_HIGH_INTEGER, refused | BUSTER_ABI_PASS_LOW_GENERAL,
                          xmm0, rax);
    m[0] = abi_test_member(builder, bits256);
    const TypeId bits256_struct = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 20));
    const TypeId bits256_union = abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    if (wide >= 32)
    {
        abi_test_vector_refused(builder, "struct {bool ext256}", bits256_struct, any, none, none);
        // The array of 20 chars has no class, but it isn't empty: clang passes the union as <4 x double>.
        abi_test_in_registers(builder, "union {bool ext256, char[20]}", bits256_union, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, refused,
                              xmm0, none);
    }
    else
    {
        abi_test_in_memory(builder, "struct {bool ext256}", bits256_struct);
        abi_test_in_memory(builder, "union {bool ext256, char[20]}", bits256_union);
    }
    // An enum backed by bool retains vector storage, unlike an actual builtin-bool vector.
    builder->next_name += 1;
    const TypeId bool_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, builder->next_name);
    type_enum_complete(table, bool_enum, BUSTER_TYPE_BOOL, 0);
    const TypeId enum1 = type_vector(table, bool_enum, 1, true);
    m[0] = abi_test_member(builder, BUSTER_TYPE_BF16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_BF16);
    m[2] = abi_test_member(builder, enum1);
    m[2].alignment_log2 = 4;
    abi_test_in_registers(builder, "struct {__bf16, __bf16, _Alignas(8) enum : bool ext1}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_LOW_SSE | BUSTER_ABI_PASS_HIGH_INTEGER,
                          refused | BUSTER_ABI_PASS_LOW_GENERAL, xmm0, rax);
    abi_test_in_registers(builder, "enum : bool ext8", type_vector(table, bool_enum, 8, true), BUSTER_ABI_TEST_SSE_ONLY,
                          BUSTER_ABI_PASS_LOW_SSE | BUSTER_ABI_PASS_SMALLER, refused, xmm0, none);
    abi_test_vector_refused(builder, "enum : bool ext16", type_vector(table, bool_enum, 16, true), any, none, none);
    abi_test_vector_copy(builder, "enum : bool ext65", type_vector(table, bool_enum, 65, true), 16, 7, none, none);
    abi_test_result_detail(builder, demoted, 0, none);
    // Packed vector alignments past 2^15 do not fit the storage column. Preserve INEXACT rather than wrapping into I128.
    const TypeId wide_packed[] = {
        type_vector(table, BUSTER_TYPE_LONG_DOUBLE, 4096, true),
        type_vector(table, type_bit_int(table, 4, true), 131072, true),
    };
    const char* wide_names[] = {
        "_Atomic {long double ext4096[0]}",
        "_Atomic {unsigned _BitInt(4) ext131072[0]}",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(wide_packed); i += 1)
    {
        m[0] = abi_test_member(builder, type_array(table, wide_packed[i], 0));
        const TypeId atomic = type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
        abi_test_expectation(builder, wide_names[i], atomic, BUSTER_ABI_IN_MEMORY,
                             BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE | BUSTER_ABI_PASS_LARGER | refused, 0, any, none, none);
    }
    // Long doubles.
    const TypeId long_double1 = abi_test_vector(builder, BUSTER_TYPE_LONG_DOUBLE, 16);
    const TypeId long_double2 = abi_test_vector(builder, BUSTER_TYPE_LONG_DOUBLE, 32);
    const TypeId long_double3 = type_vector(table, BUSTER_TYPE_LONG_DOUBLE, 3, true);
    const TypeId long_double4 = abi_test_vector(builder, BUSTER_TYPE_LONG_DOUBLE, 64);
    abi_test_vector_refused(builder, "long double x1", long_double1, BUSTER_ABI_RESULT_REGISTERS, st0, none);
    abi_test_result_detail(builder, 0, 16, none);
    if (wide >= 32)
    {
        abi_test_vector_refused(builder, "long double x2", long_double2, BUSTER_ABI_RESULT_REGISTERS, st0, st1);
    }
    else
    {
        abi_test_vector_copy(builder, "long double x2", long_double2, 32, 5, st0, st1);
    }
    abi_test_result_detail(builder, 0, 10, none);
    if (wide == 64)
    {
        abi_test_vector_refused(builder, "long double ext3", long_double3, hidden, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_refused(builder, "long double x4", long_double4, hidden, none, none);
    }
    else
    {
        abi_test_vector_copy(builder, "long double ext3", long_double3, 32, 6, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_copy(builder, "long double x4", long_double4, 64, 6, none, none);
    }
    abi_test_result_detail(builder, demoted, 0, none);
    m[0] = abi_test_member(builder, long_double1);
    abi_test_vector_refused(builder, "struct {long double x1}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_RESULT_REGISTERS, st0, none);
    abi_test_result_detail(builder, 0, 16, none);
    m[1] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    abi_test_in_registers(builder, "union {long double x1, double}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_VECTOR,
                          BUSTER_ABI_PASS_VECTOR, refused, xmm0, none);
    // __float128s.
    const TypeId quad2 = abi_test_vector(builder, BUSTER_TYPE_FLOAT128, 32);
    const TypeId quad4 = abi_test_vector(builder, BUSTER_TYPE_FLOAT128, 64);
    abi_test_in_registers(builder, "__float128 x1", abi_test_vector(builder, BUSTER_TYPE_FLOAT128, 16), BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR, refused, xmm0, none);
    abi_test_result_detail(builder, 0, 16, none);
    if (wide >= 32)
    {
        abi_test_vector_refused(builder, "__float128 x2", quad2, BUSTER_ABI_RESULT_REGISTERS, xmm0, xmm1);
    }
    else
    {
        abi_test_vector_copy(builder, "__float128 x2", quad2, 32, 5, xmm0, xmm1);
    }
    abi_test_result_detail(builder, 0, 16, none);
    if (wide == 64)
    {
        abi_test_vector_refused(builder, "__float128 x4", quad4, hidden, none, none);
    }
    else
    {
        abi_test_vector_copy(builder, "__float128 x4", quad4, 64, 6, none, none);
    }
    abi_test_result_detail(builder, demoted, 0, none);
    // _BitInts.
    const TypeId wide1 = type_vector(table, type_bit_int(table, 256, false), 1, true);
    const TypeId bit_int128_2 = type_vector(table, type_bit_int(table, 128, false), 2, true);
    const TypeId bit_int128_4 = type_vector(table, type_bit_int(table, 128, false), 4, true);
    const TypeId narrow32 = type_vector(table, type_bit_int(table, 4, true), 32, true);
    // _BitInt(128)s in a vector register (as <2N x i64>), refused for the <N x i128> clang passes once it has no vector
    // register left, whose i64s LLVM puts in general registers; a struct of one is a copy then, and passes.
    const TypeId bit_int128_1 = type_vector(table, type_bit_int(table, 128, false), 1, true);
    abi_test_vector_refused(builder, "_BitInt(128) ext1", bit_int128_1, BUSTER_ABI_RESULT_REGISTERS, xmm0, none);
    abi_test_result_detail(builder, 0, 16, none);
    m[0] = abi_test_member(builder, bit_int128_1);
    abi_test_in_registers(builder, "struct {_BitInt(128) ext1}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_VECTOR,
                          BUSTER_ABI_PASS_VECTOR, refused, xmm0, none);
    if (wide >= 32)
    {
        abi_test_vector_refused(builder, "_BitInt(256) ext1", wide1, hidden, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_refused(builder, "_BitInt(128) ext2", bit_int128_2, BUSTER_ABI_RESULT_REGISTERS, xmm0, none);
        abi_test_vector_refused(builder, "unsigned _BitInt(4) ext32", narrow32, any, none, none);
    }
    else
    {
        abi_test_vector_copy(builder, "_BitInt(256) ext1", wide1, 32, 5, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_copy(builder, "_BitInt(128) ext2", bit_int128_2, 32, 5, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_copy(builder, "unsigned _BitInt(4) ext32", narrow32, 16, 5, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
    }
    if (wide == 64)
    {
        abi_test_vector_refused(builder, "_BitInt(128) ext4", bit_int128_4, BUSTER_ABI_RESULT_REGISTERS, xmm0, none);
    }
    else
    {
        abi_test_vector_copy(builder, "_BitInt(128) ext4", bit_int128_4, 64, 6, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
    }
    // clang's object of it is 4 bytes, of 8 in C.
    abi_test_in_registers(builder, "unsigned _BitInt(4) ext8", type_vector(table, type_bit_int(table, 4, true), 8, true), BUSTER_ABI_TEST_SSE_ONLY,
                          BUSTER_ABI_PASS_LOW_SSE | BUSTER_ABI_PASS_SMALLER, refused, xmm0, none);
    abi_test_vector_refused(builder, "unsigned _BitInt(4) ext16", type_vector(table, type_bit_int(table, 4, true), 16, true), any, none, none);
    // Lengths that aren't a power of two, in memory.
    const TypeId float5 = type_vector(table, BUSTER_TYPE_FLOAT, 5, true);
    const TypeId long3 = type_vector(table, BUSTER_TYPE_LONG, 3, true);
    const TypeId double3 = type_vector(table, BUSTER_TYPE_DOUBLE, 3, true);
    const TypeId double5 = type_vector(table, BUSTER_TYPE_DOUBLE, 5, true);
    if (wide >= 32)
    {
        abi_test_in_registers(builder, "float ext5", float5, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, refused, xmm0, none);
        abi_test_in_registers(builder, "long ext3", long3, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, refused, xmm0, none);
        abi_test_in_registers(builder, "double ext3", double3, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, refused, xmm0, none);
    }
    else
    {
        abi_test_vector_copy(builder, "float ext5", float5, 32, 5, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
        abi_test_vector_copy(builder, "long ext3", long3, 32, 5, rax, rdx);
        abi_test_result_detail(builder, 0, 8, BUSTER_ABI_RCX);
        abi_test_vector_copy(builder, "double ext3", double3, 32, 5, xmm0, xmm1);
        abi_test_result_detail(builder, 0, 8, st0);
    }
    if (wide == 64)
    {
        abi_test_in_registers(builder, "double ext5", double5, BUSTER_ABI_TEST_VECTOR, BUSTER_ABI_PASS_VECTOR | BUSTER_ABI_PASS_WIDE, refused, xmm0, none);
    }
    else
    {
        abi_test_vector_copy(builder, "double ext5", double5, 64, 6, none, none);
        abi_test_result_detail(builder, demoted, 0, none);
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
    // clang stores a _BitInt of 65 to 128 bits in a record as an i128 (alone, it passes as two i64s), and a union as its
    // member LLVM aligns most: an i128 (16) over _Complex long long and an aligned(16) struct of longs (8 to LLVM). A
    // union's bit-field is an integer of its width (i70: two i64s); a struct's wider than an eightbyte, an i128.
    const TypeId bits100 = type_bit_int(table, 100, false);
    m[0] = abi_test_member(builder, bits100);
    abi_test_in_registers(builder, "{_BitInt(100)}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[0] = abi_test_bits(builder, bits100, 70, true);
    abi_test_in_registers(builder, "{_BitInt(100) : 70}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_COMPLEX(BUSTER_TYPE_CODE_LLONG));
    m[1] = abi_test_member(builder, bits100);
    abi_test_in_registers(builder, "union {_Complex long long, _BitInt(100)}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[1] = abi_test_bits(builder, bits100, 70, true);
    abi_test_in_registers(builder, "union {_Complex long long, _BitInt(100) : 70}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 5));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {aligned(16) {long, long}, __int128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    // clang keeps a zero-length array in its type of a record ([0 x %struct.big]), aligned as its element however large:
    // {long, long, {__int128, long}[0]} is aligned to 16 there, ties with an __int128, and the first of the two is the
    // union's storage (two i64s, or one i128).
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    const TypeId big = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, big, 0));
    const TypeId tail = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0);
    m[0] = abi_test_member(builder, tail);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, {__int128, long}[0]}, __int128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[1] = abi_test_member(builder, tail);
    abi_test_in_registers(builder, "union {__int128, {long, long, {__int128, long}[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    // The same with a record larger than 64 bytes in the array, {__int128, char[64]}, whose row doesn't depend on its
    // members: its alignment in LLVM's type comes from them all the same (abi_wide_alignment).
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 64));
    const TypeId big80 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, big80, 0));
    const TypeId tail80 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0);
    m[0] = abi_test_member(builder, tail80);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, {__int128, char[64]}[0]}, __int128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[1] = abi_test_member(builder, tail80);
    abi_test_in_registers(builder, "union {__int128, {long, long, {__int128, char[64]}[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    // Each of abi_wide_alignment's rules alone, a record larger than 16 bytes that LLVM aligns to 1, so that the __int128 is
    // the union's storage (one i128, as clang 23 passes each): a member off its own alignment (packed {char, long double,
    // char[15]}, 32 bytes), a record off its own (packed {char, {__int128, long}, char[15]}, 48 bytes), a size its
    // alignment doesn't divide (packed {long double, char}, 17 bytes), and a union's members (union {__int128 x : 8,
    // char[24]}, whose bit-field is an i8).
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[2] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 15));
    const TypeId off_member = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, true, 0);
    m[1] = abi_test_member(builder, big);
    const TypeId off_record = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, true, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    const TypeId off_size = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_INT128, 8, true);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 24));
    const TypeId bits_union = abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    const TypeId walked[4] = {
        off_member,
        off_record,
        off_size,
        bits_union,
    };
    const char* const walked_names[4] = {
        "union {{long, long, packed {char, long double, char[15]}[0]}, __int128}",
        "union {{long, long, packed {char, {__int128, long}, char[15]}[0]}, __int128}",
        "union {{long, long, packed {long double, char}[0]}, __int128}",
        "union {{long, long, union {__int128 x : 8, char[24]}[0]}, __int128}",
    };
    for (uint32_t w = 0; w < 4; w += 1)
    {
        m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
        m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
        m[2] = abi_test_member(builder, type_array(table, walked[w], 0));
        m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0));
        m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
        abi_test_in_registers(builder, walked_names[w], abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS,
                              BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    }
    // The same with the large record inside another ({char, {__int128, long}}, aligned to 16 too), and with a packed one
    // in the array, aligned to 1 in LLVM's type (a long double off its own): the __int128 is then the most aligned.
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, big);
    const TypeId nested = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, nested, 0));
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, {char, {__int128, long}}[0]}, __int128}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG_DOUBLE);
    m[2] = abi_test_member(builder, BUSTER_TYPE_LONG);
    const TypeId packed_big = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, true, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, packed_big, 0));
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, packed {char, long double, long}[0]}, __int128}",
                          abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    // A run of bit-fields in the large record is its unit there (abi_bit_field_run): an array of bytes, aligned to 1,
    // where its 16 bytes don't fit before a char, which leaves {unsigned __int128 : 65, char, long} aligned to 8 in
    // LLVM's type and the __int128 the most aligned; an i128 where they fit, a tie the struct wins.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 65, true);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[2] = abi_test_member(builder, BUSTER_TYPE_LONG);
    const TypeId run_big = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, run_big, 0));
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, {unsigned __int128 : 65, char, long}[0]}, __int128}",
                          abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 65, true);
    m[1] = abi_test_member(builder, longs);
    const TypeId fitting_big = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[2] = abi_test_member(builder, type_array(table, fitting_big, 0));
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    abi_test_in_registers(builder, "union {{long, long, {unsigned __int128 : 65, long[2]}[0]}, __int128}",
                          abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    // A union stores a bit-field of 121 to 128 bits as an i128 (getIntNType rounds to bytes), unnamed though it is: one
    // i128 with a high eightbyte NO_CLASS, which clang counts as two registers; 120 bits are an i120, an i64 to pass.
    m[0] = abi_test_member(builder, type_bit_int(table, 32, true));
    m[1] = abi_test_bits(builder, type_bit_int(table, 128, true), 127, false);
    abi_test_in_registers(builder, "union {unsigned _BitInt(32), unsigned _BitInt(128) : 127}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGER_ONLY, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[1] = abi_test_bits(builder, type_bit_int(table, 128, true), 120, false);
    abi_test_in_registers(builder, "union {unsigned _BitInt(32), unsigned _BitInt(128) : 120}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGER_ONLY, 0, BUSTER_ABI_PASS_PAIR, rax, BUSTER_ABI_TEST_NONE);
    // The i128 an INTEGER low eightbyte reads is the whole value whatever the high eightbyte is, SSE too: a union of a
    // struct of an unnamed bit-field (no class, an i128 to LLVM) and {long, double}, or a vector and a packed bit-field,
    // stored as the struct (lowerUnion's first of the most aligned; with the vector first, {i64, double}). An INTEGER high
    // eightbyte reads it from offset 8: {i64, i128} after an SSE low one, and an i128 alone (refused). Two SSE eightbytes
    // are two doubles.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 125, false);
    const TypeId unnamed_i128 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    const TypeId bytes16 = abi_test_vector(builder, BUSTER_TYPE_UCHAR, 16);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_CHAR, 6, true);
    const TypeId packed_bits = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    const AbiClasses integer_sse = BUSTER_ABI_CLASSES(BUSTER_ABI_INTEGER, BUSTER_ABI_SSE);
    const uint32_t i128_pair = BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_HIGH_INTEGER;
    const uint32_t not_pair = BUSTER_ABI_PASS_HIGH_SSE | BUSTER_ABI_PASS_UNSUPPORTED;
    m[0] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    m[1] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    m[0] = abi_test_member(builder, unnamed_i128);
    abi_test_in_registers(builder, "union {struct {unsigned __int128 : 125}, {long, double}}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          integer_sse, i128_pair, not_pair, rax, rdx);
    m[0] = abi_test_member(builder, unnamed_i128);
    m[1] = abi_test_member(builder, bytes16);
    m[2] = abi_test_member(builder, packed_bits);
    abi_test_in_registers(builder, "union {struct {unsigned __int128 : 125}, char vector(16), packed {char : 6}}",
                          abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 3, false, 0), integer_sse, i128_pair, not_pair, rax, rdx);
    m[0] = abi_test_member(builder, bytes16);
    m[1] = abi_test_member(builder, unnamed_i128);
    abi_test_in_registers(builder, "union {char vector(16), struct {unsigned __int128 : 125}, packed {char : 6}}",
                          abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 3, false, 0), integer_sse, BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_HIGH_SSE,
                          BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_HIGH_INTEGER | BUSTER_ABI_PASS_UNSUPPORTED, rax, BUSTER_ABI_TEST_XMM(0));
    m[0] = abi_test_member(builder, BUSTER_TYPE_DOUBLE);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    m[0] = abi_test_member(builder, unnamed_i128);
    abi_test_expectation(builder, "union {struct {unsigned __int128 : 125}, {double, long}}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                         BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_UNSUPPORTED, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_PAIR,
                         BUSTER_ABI_TEST_RESULT_ANY, BUSTER_ABI_TEST_NONE, BUSTER_ABI_TEST_NONE);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_LONG, 64, false);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    m[0] = abi_test_member(builder, unnamed_i128);
    abi_test_expectation(builder, "union {struct {unsigned __int128 : 125}, {long : 64, long}}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                         BUSTER_ABI_CLASSES(BUSTER_ABI_NO_CLASS, BUSTER_ABI_INTEGER), BUSTER_ABI_PASS_UNSUPPORTED, BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_PAIR,
                         BUSTER_ABI_TEST_RESULT_ANY, BUSTER_ABI_TEST_NONE, BUSTER_ABI_TEST_NONE);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_DOUBLE, 2));
    abi_test_in_registers(builder, "union {struct {unsigned __int128 : 125}, double[2]}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0),
                          BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_SSE), BUSTER_ABI_PASS_LOW_SSE | BUSTER_ABI_PASS_HIGH_SSE,
                          BUSTER_ABI_PASS_PAIR | BUSTER_ABI_PASS_UNSUPPORTED, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    // A struct's run of bit-fields past an eightbyte is an i128 only where 16 bytes fit before the next member with
    // storage, or the struct's end (accumulateBitFields): packed, or followed by a char or an array of no elements, it
    // is an array of bytes (an i64 and an i8, or two i64s); an empty struct after it takes no room, and a bit-field that
    // starts inside a byte joins the run, one that starts at a byte doesn't.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 65, true);
    abi_test_in_registers(builder, "{unsigned __int128 : 65}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    abi_test_in_registers(builder, "packed {unsigned __int128 : 65}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0), BUSTER_ABI_TEST_INTEGERS, 0,
                          BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_in_registers(builder, "{unsigned __int128 : 65, char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, 0,
                          BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[1] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0));
    abi_test_in_registers(builder, "{unsigned __int128 : 65, struct {}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS,
                          BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_INT, 0));
    abi_test_in_registers(builder, "{unsigned __int128 : 65, int[0]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_INTEGERS, 0,
                          BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 60, true);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 10, true);
    abi_test_in_registers(builder, "{unsigned __int128 : 60, unsigned __int128 : 10}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, BUSTER_ABI_PASS_PAIR, 0, rax, rdx);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 72, true);
    m[1] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 8, true);
    abi_test_in_registers(builder, "{unsigned __int128 : 72, unsigned __int128 : 8}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGERS, 0, BUSTER_ABI_PASS_PAIR, rax, rdx);
    m[0] = abi_test_bits(builder, type_bit_int(table, 128, false), 65, true);
    abi_test_in_registers(builder, "packed {_BitInt(128) : 65}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0), BUSTER_ABI_TEST_INTEGERS, 0,
                          BUSTER_ABI_PASS_PAIR, rax, rdx);
    // clang widens a narrow low part (a bfloat, <2 x half> or <2 x bfloat>) to an i64 when its high part isn't aligned
    // to 8 (GetX86_64ByValArgumentPair), a general register for an eightbyte it counts as SSE; a half, to a double.
    const TypeId bf16 = BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BF16);
    const AbiClasses sse_integer = BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_INTEGER);
    const uint32_t low_general = BUSTER_ABI_PASS_LOW_GENERAL;
    m[0] = abi_test_member(builder, bf16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, _Alignas(8) char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), sse_integer, low_general, 0, rax, rdx);
    m[0] = abi_test_member(builder, BUSTER_TYPE_FLOAT16);
    abi_test_in_registers(builder, "{_Float16, _Alignas(8) char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), sse_integer, 0, low_general,
                          BUSTER_ABI_TEST_XMM(0), rax);
    m[1] = abi_test_member(builder, BUSTER_TYPE_FLOAT16);
    m[2] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[2].alignment_log2 = 4;
    abi_test_in_registers(builder, "{_Float16, _Float16, _Alignas(8) char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), sse_integer, low_general,
                          0, rax, rdx);
    m[0] = abi_test_member(builder, bf16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_FLOAT);
    m[1].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, _Alignas(8) float}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_TEST_SSES, low_general, 0,
                          rax, BUSTER_ABI_TEST_XMM(0));
    m[2] = abi_test_member(builder, BUSTER_TYPE_FLOAT);
    abi_test_in_registers(builder, "{__bf16, _Alignas(8) float, float}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), BUSTER_ABI_TEST_SSES, 0,
                          low_general, BUSTER_ABI_TEST_XMM(0), BUSTER_ABI_TEST_XMM(1));
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, _Alignas(8) long}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), sse_integer, 0, low_general,
                          BUSTER_ABI_TEST_XMM(0), rax);
    m[1] = abi_test_member(builder, bf16);
    m[2] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[2].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, __bf16, _Alignas(8) int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), sse_integer, low_general, 0,
                          rax, rdx);
    m[2] = abi_test_member(builder, bf16);
    m[3] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[3].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16, __bf16, __bf16, _Alignas(8) char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 4, false, 0), sse_integer, 0,
                          low_general, BUSTER_ABI_TEST_XMM(0), rax);
    // An array of an aligned typedef lays out as one of its base (the type table keeps no size for the typedef): a
    // __bf16 in it is the narrow low part clang widens, as in an array of plain __bf16.
    m[0] = abi_test_member(builder, type_array(table, bf16, 1));
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1].alignment_log2 = 4;
    abi_test_in_registers(builder, "{__bf16[1], _Alignas(8) int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), sse_integer, low_general, 0, rax,
                          rdx);
    m[0] = abi_test_member(builder, type_array(table, type_aligned(table, bf16, 0), 1));
    abi_test_in_registers(builder, "{aligned(1) __bf16[1], _Alignas(8) int}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), sse_integer, low_general,
                          0, rax, rdx);
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

// The pass bits of an UNCOUNTED value of a kind, and the VALUE bits that must be clear for it.
static uint32_t abi_test_value(uint32_t kind)
{
    return BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED | kind << BUSTER_ABI_PASS_VALUE_SHIFT;
}

static uint32_t abi_test_other_values(uint32_t kind)
{
    return BUSTER_ABI_PASS_VALUE & ~(kind << BUSTER_ABI_PASS_VALUE_SHIFT);
}

// clang's LLVM types for _Atomic types and what holds them, which the byval slots after five and six longs show (-mllvm
// -stop-after=finalize-isel's fixed stack objects). An _Atomic of a type of no size is {T, [1 x i8]}, as large as T is
// aligned in LLVM (LARGER): 16 bytes for _Atomic(struct { __int128 x[0]; }), of 1, and for one of a packed T, aligned
// to 1, while clang has a general register free, an i8's 8 once it has none (GROWN); 32 KB for one aligned by a vector of
// 32 KB, and one by a vector of 64 KB, past what the storage holds, refused, as is one aligned by a run of bit-fields of
// an eightbyte or less, whose unit the module doesn't work out; and 1 byte for one of a _BitInt(129), an array of its
// bytes in memory. A packed struct ending in one is
// those bytes and an i8 (packed {that _Atomic} 17, a slot of 24); one with a member after the overrun is laid out
// otherwise than C, and refused; one with room for it is the C layout, and so is a union. An _Atomic vector LLVM splits
// into its elements (<5 x i8> into five i8s, <2 x i128> into four i64s) is refused too; one of bools is the integer of
// its bits clang stores it as: a general register, two halves past 64 and an i128 at 128.
static void abi_test_build_atomics(AbiTestBuilder* builder)
{
    TypeTable* table = builder->table;
    const uint8_t rax = BUSTER_ABI_RAX;
    const uint8_t rdx = BUSTER_ABI_RDX;
    const uint8_t none = BUSTER_ABI_TEST_NONE;
    const uint32_t copy = BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_AGGREGATE;
    const uint32_t larger = BUSTER_ABI_PASS_LARGER;
    const uint32_t grown = BUSTER_ABI_PASS_GROWN;
    const uint32_t refused = BUSTER_ABI_PASS_UNSUPPORTED;
    const uint32_t value = BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_UNCOUNTED;
    const uint8_t any = BUSTER_ABI_TEST_RESULT_ANY;
    const uint8_t hidden = BUSTER_ABI_RESULT_MEMORY;
    TypeMember m[3];
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_INT128, 0));
    const TypeId z16 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    const TypeId z16_packed = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_LONG, 0));
    const TypeId z8 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_INT, 0));
    const TypeId z4 = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    const TypeId a16 = type_atomic(table, z16);
    const TypeId a8 = type_atomic(table, z8);
    abi_test_expectation(builder, "_Atomic {__int128[0]}", a16, BUSTER_ABI_IN_MEMORY, copy | larger | 4u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT, grown | refused, hidden, none,
                         none);
    abi_test_slot(builder, 16);
    abi_test_expectation(builder, "_Atomic packed {__int128[0]}", type_atomic(table, z16_packed), BUSTER_ABI_IN_MEMORY, copy | larger | grown, refused, hidden, none,
                         none);
    abi_test_slot(builder, 16);
    abi_test_expectation(builder, "_Atomic {long[0]}", a8, BUSTER_ABI_IN_MEMORY, copy | larger, grown | refused, hidden, none, none);
    abi_test_slot(builder, 8);
    // A flexible array member's element aligns its struct of no size in LLVM: an _Atomic of one of unsigned _BitInt(127)
    // (an i128 to LLVM, aligned to 8 in C) is 16 bytes, and GROWN.
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0));
    m[1] = abi_test_member(builder, type_incomplete_array(table, type_bit_int(table, 127, true)));
    abi_test_expectation(builder, "_Atomic {struct {}, unsigned _BitInt(127)[]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 16);
    // clang's canonical _Atomic type has no typedef in its value: an aligned(64) one counts neither for one of no size,
    // nor for one of more than 16 bytes, whose byval slots are aligned to 8 (the first an i8 once clang has no general
    // register).
    const uint32_t eight = 3u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
    const uint32_t not_eight = BUSTER_ABI_PASS_ALIGNMENT & ~eight;
    abi_test_expectation(builder, "_Atomic {} aligned(64)", type_atomic(table, type_aligned(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0), 6)),
                         BUSTER_ABI_IN_MEMORY, copy | eight, larger | grown | refused | not_eight, hidden, none, none);
    abi_test_slot(builder, 8);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_LONG, 4));
    abi_test_expectation(builder, "_Atomic {long[4]} aligned(64)", type_atomic(table, type_aligned(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), 6)),
                         BUSTER_ABI_IN_MEMORY, copy | eight, larger | grown | refused | not_eight, hidden, none, none);
    abi_test_slot(builder, 32);
    // Packed structs ending in one, which runs past their end.
    m[0] = abi_test_member(builder, a16);
    const TypeId ending = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    abi_test_expectation(builder, "packed {_Atomic {__int128[0]}}", ending, BUSTER_ABI_IN_MEMORY, copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 24);
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, a16);
    abi_test_expectation(builder, "packed {char, _Atomic {__int128[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 24);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 9));
    abi_test_expectation(builder, "packed {char[9], _Atomic {__int128[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger, grown | refused, hidden, none, none);
    abi_test_slot(builder, 32);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 100));
    abi_test_expectation(builder, "packed {char[100], _Atomic {__int128[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger, grown | refused, hidden, none, none);
    abi_test_slot(builder, 120);
    m[0] = abi_test_member(builder, a8);
    abi_test_expectation(builder, "packed {_Atomic {long[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 16);
    m[0] = abi_test_member(builder, type_atomic(table, z4));
    abi_test_expectation(builder, "packed {_Atomic {int[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0), BUSTER_ABI_IN_MEMORY, copy | larger,
                         grown | refused, hidden, none, none);
    abi_test_slot(builder, 8);
    // Room for it: the C layout.
    m[0] = abi_test_member(builder, a16);
    abi_test_expectation(builder, "{_Atomic {__int128[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), BUSTER_ABI_IN_MEMORY, copy,
                         larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 16);
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, a16);
    abi_test_expectation(builder, "{char, _Atomic {__int128[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_IN_MEMORY, copy,
                         larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 32);
    m[0] = abi_test_member(builder, a16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[2] = abi_test_member(builder, type_incomplete_array(table, BUSTER_TYPE_LONG));
    abi_test_expectation(builder, "{_Atomic {__int128[0]}, __int128, long[]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), BUSTER_ABI_IN_MEMORY,
                         copy, larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 32);
    m[0] = abi_test_member(builder, ending);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[1].alignment_log2 = 6;
    abi_test_expectation(builder, "{packed {_Atomic {__int128[0]}}, _Alignas(32) long}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                         BUSTER_ABI_IN_MEMORY, copy, larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 64);
    m[0] = abi_test_member(builder, a16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_expectation(builder, "union {_Atomic {__int128[0]}, char}", abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), BUSTER_ABI_IN_MEMORY, copy,
                         larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 16);
    // A member after an overrun, which clang moves (64 bytes for the first, 18 for the second, 32 for the next two), and
    // records holding a type whose LLVM size the module doesn't keep.
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    m[1] = abi_test_member(builder, a16);
    m[2] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_expectation(builder, "{char, _Atomic {__int128[0]}, char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 3, false, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | refused, 0, any, none, none);
    m[0] = abi_test_member(builder, a16);
    m[1] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_expectation(builder, "packed {_Atomic {__int128[0]}, char}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | refused, 0, any, none, none);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 0));
    abi_test_expectation(builder, "{_Atomic {__int128[0]}, char[0]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | refused, 0, any, none, none);
    m[1] = abi_test_member(builder, type_incomplete_array(table, BUSTER_TYPE_INT));
    abi_test_expectation(builder, "{_Atomic {__int128[0]}, int[]}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | refused, 0, any, none, none);
    m[0] = abi_test_member(builder, ending);
    abi_test_expectation(builder, "packed {packed {_Atomic {__int128[0]}}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0), BUSTER_ABI_IN_MEMORY,
                         copy | larger | refused, 0, any, none, none);
    // An _Atomic of a struct of no size aligned by a record larger than 64 bytes, an array of none of them, whose LLVM
    // alignment abi_wide_alignment walks: 16 bytes, as {__int128[0]}'s.
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT128);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 100));
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {struct {__int128, char[100]}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | 4u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT, grown | refused, hidden, none, none);
    abi_test_slot(builder, 16);
    // And by a record with a flexible array member and a size, whose storage the module doesn't keep (INEXACT): an
    // array of none of them in a struct and in a union, and in a record larger than 16 bytes, which the walk takes.
    m[0] = abi_test_member(builder, BUSTER_TYPE_INT);
    m[1] = abi_test_member(builder, type_incomplete_array(table, BUSTER_TYPE_LONG));
    const TypeId open_sized = abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(builder, type_array(table, open_sized, 0));
    abi_test_expectation(builder, "_Atomic {struct {int, long[]}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    abi_test_expectation(builder, "_Atomic union {struct {int, long[]}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    m[0] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 20));
    m[1] = abi_test_member(builder, open_sized);
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {struct {char[20], struct {int, long[]}}[0]}",
                         type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)), BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any,
                         none, none);
    // An _Atomic's canonical type drops its value's typedefs: of an aligned(16) typedef of an empty struct, it is aligned
    // to 16 in C and to 1 canonically, by which clang checks a member's offset. A union of an empty struct and an array of
    // none of them (through an aligned(1) typedef) at offset 1 passes as an i8.
    const TypeId empty16 = type_aligned(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0), 4);
    m[0] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0));
    m[1] = abi_test_member(builder, type_array(table, type_aligned(table, type_atomic(table, empty16), 0), 0));
    m[1] = abi_test_member(builder, abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0));
    m[0] = abi_test_member(builder, BUSTER_TYPE_CHAR);
    abi_test_in_registers(builder, "{char, union {struct {}, _Atomic {} aligned(16) aligned(1)[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0),
                          BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0, rax, none);
    abi_test_expectation(builder, "_Atomic packed {_Atomic {__int128[0]}}", type_atomic(table, ending), BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none,
                         none);
    // An _Atomic of a struct of no size aligned by a vector of 32 KB, the most the storage holds (LLVM aligns a vector
    // as large as itself): 32 KB to LLVM, and 32 KB and a byte for a packed struct ending in it. One of 64 KB, which the
    // storage doesn't hold (INEXACT), is refused, and so is the packed struct ending in it.
    const uint32_t aligned_32k = 15u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT;
    m[0] = abi_test_member(builder, type_array(table, abi_test_vector(builder, BUSTER_TYPE_FLOAT, 32768), 0));
    const TypeId a32k = type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    m[0] = abi_test_member(builder, type_array(table, abi_test_vector(builder, BUSTER_TYPE_FLOAT, 65536), 0));
    const TypeId a64k = type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    abi_test_expectation(builder, "_Atomic {float vector(32768)[0]}", a32k, BUSTER_ABI_IN_MEMORY, copy | larger | aligned_32k,
                         grown | refused | (BUSTER_ABI_PASS_ALIGNMENT & ~aligned_32k), hidden, none, none);
    abi_test_slot(builder, 32768);
    abi_test_expectation(builder, "_Atomic {float vector(65536)[0]}", a64k, BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    m[0] = abi_test_member(builder, a32k);
    abi_test_expectation(builder, "packed {_Atomic {float vector(32768)[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0),
                         BUSTER_ABI_IN_MEMORY, copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 32776);
    m[0] = abi_test_member(builder, a64k);
    abi_test_expectation(builder, "packed {_Atomic {float vector(65536)[0]}}", abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    // The same through a record larger than 16 bytes that holds the vector, whose alignment abi_wide_alignment walks
    // (byval({ %struct.s, [1 x i8] }) align 32768, and the 64 KB one INEXACT from its member).
    m[0] = abi_test_member(builder, abi_test_vector(builder, BUSTER_TYPE_FLOAT, 32768));
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {{float vector(32768)}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | aligned_32k, grown | refused | (BUSTER_ABI_PASS_ALIGNMENT & ~aligned_32k), hidden, none, none);
    abi_test_slot(builder, 32768);
    m[0] = abi_test_member(builder, abi_test_vector(builder, BUSTER_TYPE_FLOAT, 65536));
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {{float vector(65536)}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    // clang's unit for a run of bit-fields of an eightbyte or less, which the module doesn't work out (INEXACT): an
    // _Atomic of a struct of none of {unsigned __int128 : 8, char[20]}, which LLVM stores as {i8, [20 x i8], [11 x i8]},
    // aligned to 1 rather than 16, is refused. A _BitInt wider than 128 bits is an integer of its size in memory where
    // that is a multiple of 16 bytes, and otherwise an array of its bytes: _Atomics of structs of none of _BitInt(129),
    // of {_BitInt(129), char[4]} and of union {_BitInt(129), long} are 1, 1 and 8 bytes to LLVM, and of _BitInt(256) 16.
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 8, true);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 20));
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {{unsigned __int128 : 8, char[20]}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    m[0] = abi_test_bits(builder, BUSTER_TYPE_UINT128, 1, true);
    m[1] = abi_test_member(builder, BUSTER_TYPE_UCHAR);
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {{unsigned __int128 : 1, unsigned char}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | refused, 0, any, none, none);
    const TypeId bit_int_129 = type_bit_int(table, 129, false);
    m[0] = abi_test_member(builder, type_array(table, bit_int_129, 0));
    abi_test_expectation(builder, "_Atomic {_BitInt(129)[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy, larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 8);
    m[0] = abi_test_member(builder, bit_int_129);
    m[1] = abi_test_member(builder, type_array(table, BUSTER_TYPE_CHAR, 4));
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {{_BitInt(129), char[4]}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy, larger | grown | refused, hidden, none, none);
    abi_test_slot(builder, 8);
    m[0] = abi_test_member(builder, bit_int_129);
    m[1] = abi_test_member(builder, BUSTER_TYPE_LONG);
    m[0] = abi_test_member(builder, type_array(table, abi_test_record(builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0), 0));
    abi_test_expectation(builder, "_Atomic {union {_BitInt(129), long}[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger, grown | refused, hidden, none, none);
    abi_test_slot(builder, 8);
    m[0] = abi_test_member(builder, type_array(table, type_bit_int(table, 256, false), 0));
    abi_test_expectation(builder, "_Atomic {_BitInt(256)[0]}", type_atomic(table, abi_test_record(builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0)),
                         BUSTER_ABI_IN_MEMORY, copy | larger | grown, refused, hidden, none, none);
    abi_test_slot(builder, 16);
    // _Atomic vectors: in one vector register, as LLVM widens them to one it has (16 bytes for <9 x i8>, v8f16 for
    // <3 x half>), or split into their elements as no class says; a <1 x _BitInt(128)> in two halves.
    const uint32_t vector = abi_test_value(BUSTER_ABI_VALUE_VECTOR);
    const uint32_t not_vector = refused | abi_test_other_values(BUSTER_ABI_VALUE_VECTOR);
    abi_test_expectation(builder, "_Atomic char ext8", type_atomic(table, type_vector(table, BUSTER_TYPE_CHAR, 8, true)), BUSTER_ABI_IN_MEMORY, vector, not_vector,
                         BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), none);
    abi_test_expectation(builder, "_Atomic char ext9", type_atomic(table, type_vector(table, BUSTER_TYPE_CHAR, 9, true)), BUSTER_ABI_IN_MEMORY, vector, not_vector,
                         BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), none);
    abi_test_expectation(builder, "_Atomic _Float16 ext3", type_atomic(table, type_vector(table, BUSTER_TYPE_FLOAT16, 3, true)), BUSTER_ABI_IN_MEMORY, vector,
                         not_vector, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), none);
    abi_test_expectation(builder, "_Atomic char ext3", type_atomic(table, type_vector(table, BUSTER_TYPE_CHAR, 3, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0, any,
                         none, none);
    abi_test_expectation(builder, "_Atomic char ext5", type_atomic(table, type_vector(table, BUSTER_TYPE_CHAR, 5, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0, any,
                         none, none);
    abi_test_expectation(builder, "_Atomic short ext3", type_atomic(table, type_vector(table, BUSTER_TYPE_SHORT, 3, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0, any,
                         none, none);
    abi_test_expectation(builder, "_Atomic __int128 x2", type_atomic(table, abi_test_vector(builder, BUSTER_TYPE_INT128, 32)), BUSTER_ABI_IN_MEMORY, value | refused, 0,
                         any, none, none);
    abi_test_expectation(builder, "_Atomic long double ext2", type_atomic(table, type_vector(table, BUSTER_TYPE_LONG_DOUBLE, 2, true)), BUSTER_ABI_IN_MEMORY,
                         BUSTER_ABI_PASS_MEMORY | refused, 0, any, none, none);
    abi_test_expectation(builder, "_Atomic __float128 ext2", type_atomic(table, type_vector(table, BUSTER_TYPE_FLOAT128, 2, true)), BUSTER_ABI_IN_MEMORY, value | refused,
                         0, any, none, none);
    abi_test_expectation(builder, "_Atomic _BitInt(256) ext1", type_atomic(table, type_vector(table, type_bit_int(table, 256, false), 1, true)), BUSTER_ABI_IN_MEMORY,
                         value | refused, 0, any, none, none);
    abi_test_expectation(builder, "_Atomic _BitInt(128) ext1", type_atomic(table, type_vector(table, type_bit_int(table, 128, false), 1, true)), BUSTER_ABI_IN_MEMORY,
                         abi_test_value(BUSTER_ABI_VALUE_HALVES), refused | abi_test_other_values(BUSTER_ABI_VALUE_HALVES), BUSTER_ABI_RESULT_REGISTERS, rax, rdx);
    // _BitInt(4)s, packed in memory: <8 x i4> is promoted to 16-bit elements in xmm0, and <1 x i4> is an integer.
    abi_test_expectation(builder, "_Atomic unsigned _BitInt(4) ext8", type_atomic(table, type_vector(table, type_bit_int(table, 4, true), 8, true)),
                         BUSTER_ABI_IN_MEMORY, value | refused, 0, any, none, none);
    abi_test_expectation(builder, "_Atomic unsigned _BitInt(4) ext1", type_atomic(table, type_vector(table, type_bit_int(table, 4, true), 1, true)),
                         BUSTER_ABI_IN_MEMORY, abi_test_value(BUSTER_ABI_VALUE_GENERAL), refused | abi_test_other_values(BUSTER_ABI_VALUE_GENERAL),
                         BUSTER_ABI_RESULT_REGISTERS, rax, none);
    const TypeId char17 = type_atomic(table, type_vector(table, BUSTER_TYPE_CHAR, 17, true));
    if (builder->vector_bytes >= 32)
    {
        abi_test_expectation(builder, "_Atomic char ext17", char17, BUSTER_ABI_IN_MEMORY, vector, not_vector, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), none);
    }
    else
    {
        abi_test_expectation(builder, "_Atomic char ext17", char17, BUSTER_ABI_IN_MEMORY, value | refused, 0, any, none, none);
    }
    // _Atomic vectors of bools: i10 in a general register (ax as a result), i65 in two halves, i128 one PAIR, past that
    // more parts.
    const uint32_t general = abi_test_value(BUSTER_ABI_VALUE_GENERAL);
    const uint32_t not_general = refused | abi_test_other_values(BUSTER_ABI_VALUE_GENERAL);
    abi_test_expectation(builder, "_Atomic bool ext10", type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 10, true)), BUSTER_ABI_IN_MEMORY, general,
                         not_general | BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO, BUSTER_ABI_RESULT_REGISTERS, rax, none);
    abi_test_slot(builder, 8);
    abi_test_expectation(builder, "_Atomic bool ext64", type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 64, true)), BUSTER_ABI_IN_MEMORY, general, not_general,
                         BUSTER_ABI_RESULT_REGISTERS, rax, none);
    abi_test_expectation(builder, "_Atomic bool ext65", type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 65, true)), BUSTER_ABI_IN_MEMORY,
                         abi_test_value(BUSTER_ABI_VALUE_HALVES), refused | abi_test_other_values(BUSTER_ABI_VALUE_HALVES), BUSTER_ABI_RESULT_REGISTERS, rax, rdx);
    abi_test_expectation(builder, "_Atomic bool ext128", type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 128, true)), BUSTER_ABI_IN_MEMORY,
                         abi_test_value(BUSTER_ABI_VALUE_PAIR) | 4u << BUSTER_ABI_PASS_ALIGNMENT_SHIFT, refused | abi_test_other_values(BUSTER_ABI_VALUE_PAIR),
                         BUSTER_ABI_RESULT_REGISTERS, rax, rdx);
    abi_test_expectation(builder, "_Atomic bool ext129", type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 129, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0,
                         any, none, none);
    // _Atomic vectors of an enum of bool, <N x i1> rather than an integer (abi_is_bool_vector): 8 in an xmm register, 32
    // in a ymm one with 32-byte registers, and 10 and 64, which X86 breaks into their elements or into two halves,
    // refused.
    builder->next_name += 1;
    const TypeId bool_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, builder->next_name);
    abi_test_expectation(builder, "enum : bool", type_enum_complete(table, bool_enum, BUSTER_TYPE_BOOL, 0) ? bool_enum : 0, BUSTER_ABI_TEST_INTEGER_ONLY, 0, 0,
                         BUSTER_ABI_RESULT_REGISTERS, rax, none);
    abi_test_expectation(builder, "_Atomic enum : bool ext8", type_atomic(table, type_vector(table, bool_enum, 8, true)), BUSTER_ABI_IN_MEMORY, vector, not_vector,
                         BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0), none);
    abi_test_expectation(builder, "_Atomic enum : bool ext10", type_atomic(table, type_vector(table, bool_enum, 10, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0,
                         any, none, none);
    const TypeId bools32 = type_atomic(table, type_vector(table, bool_enum, 32, true));
    if (builder->vector_bytes >= 32)
    {
        abi_test_expectation(builder, "_Atomic enum : bool ext32", bools32, BUSTER_ABI_IN_MEMORY, vector, not_vector, BUSTER_ABI_RESULT_REGISTERS, BUSTER_ABI_TEST_XMM(0),
                             none);
    }
    else
    {
        abi_test_expectation(builder, "_Atomic enum : bool ext32", bools32, BUSTER_ABI_IN_MEMORY, value | refused, 0, any, none, none);
    }
    abi_test_expectation(builder, "_Atomic enum : bool ext64", type_atomic(table, type_vector(table, bool_enum, 64, true)), BUSTER_ABI_IN_MEMORY, value | refused, 0,
                         any, none, none);
}

// The levels across calls (abi_dependencies_classified): a record larger than 64 bytes waits for its members only
// where the table can hold a LARGER type, which a call knows from a LARGER type classified before (abi->larger) or an
// _Atomic of a type of no size among its own; and a flexible record of no size always does, since an _Atomic of it in a
// later call takes its alignment as its size. Four calls by each path: a struct of an empty struct and a flexible
// array of a struct two levels deep, aligned to 16, and one whose flexible array's element is a struct of 128 bytes
// holding that struct, which doesn't wait for it, and a struct of a zero-length array of that one, while each of no size
// waits for what abi_wide_alignment reads of it; an _Atomic {__int128[0]}; packed {packed {that _Atomic}} and a struct of
// 101 bytes ending in it, which needs it classified first to be refused; and _Atomics of the structs of no size, a slot
// of 16 for their alignment.
static void abi_test_ordering(AbiTestTally* tally)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        AbiTestTables tables;
        if (abi_test_expect(tally, abi_test_tables_open(&tables, 4096), "fixtures", "the tables can't be made"))
        {
            TypeTable* table = &tables.types.table;
            AbiTestBuilder builder = {
                .table = table,
                .expectations = 0,
                .count = 0,
                .next_name = 20000,
                .vector_bytes = 16,
            };
            const bool started = abi_test_expect(tally, abi_test_tables_start(&tables, 16), "fixtures", "the calling convention's table can't be made");
            TypeMember m[2];
            m[0] = abi_test_member(&builder, BUSTER_TYPE_INT128);
            m[0] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
            const TypeId deep = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
            m[0] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0));
            m[1] = abi_test_member(&builder, type_incomplete_array(table, deep));
            const TypeId open = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
            m[0] = abi_test_member(&builder, deep);
            m[1] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 100));
            const TypeId wide = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
            m[0] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 0, false, 0));
            m[1] = abi_test_member(&builder, type_incomplete_array(table, wide));
            const TypeId open_wide = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
            m[0] = abi_test_member(&builder, type_array(table, wide, 0));
            const TypeId zero_wide = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
            TypeId atomic = 0;
            for (uint32_t call = 0; started && call < 4; call += 1)
            {
                TypeId large = 0;
                TypeId atomic_open = 0;
                TypeId atomic_wide = 0;
                TypeId atomic_zero = 0;
                if (call == 1)
                {
                    m[0] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_INT128, 0));
                    atomic = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
                }
                else if (call == 2)
                {
                    m[0] = abi_test_member(&builder, atomic);
                    m[0] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0));
                    m[1] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0));
                    m[0] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 100));
                    large = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0);
                }
                else if (call == 3)
                {
                    atomic_open = type_atomic(table, open);
                    atomic_wide = type_atomic(table, open_wide);
                    atomic_zero = type_atomic(table, zero_wide);
                }
                if (path == 0)
                {
                    abi_classify_all(&tables.abi, table);
                }
                else
                {
                    abi_classify_all_scalar(&tables.abi, table);
                }
                const uint32_t refused = BUSTER_ABI_PASS_LARGER | BUSTER_ABI_PASS_UNSUPPORTED;
                const uint32_t large_pass = tables.abi.pass[large >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t open_pass = tables.abi.pass[atomic_open >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t open_bytes = tables.abi.stack_bytes[atomic_open >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t wide_pass = tables.abi.pass[atomic_wide >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t wide_bytes = tables.abi.stack_bytes[atomic_wide >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t zero_pass = tables.abi.pass[atomic_zero >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t zero_bytes = tables.abi.stack_bytes[atomic_zero >> BUSTER_TYPE_INDEX_SHIFT];
                char detail[160];
                snprintf(detail, sizeof(detail), "ordering, call %u (%s): pass %08x, %08x, %08x, %08x, stack %u, %u, %u", call, path == 0 ? "vector" : "scalar",
                         large_pass, open_pass, wide_pass, zero_pass, open_bytes, wide_bytes, zero_bytes);
                const bool large_right = large == 0 || (large_pass & refused) == refused;
                const bool open_right = atomic_open == 0 || ((open_pass & refused) == BUSTER_ABI_PASS_LARGER && open_bytes == 16);
                const bool wide_right = atomic_wide == 0 || ((wide_pass & refused) == BUSTER_ABI_PASS_LARGER && wide_bytes == 16);
                const bool zero_right = atomic_zero == 0 || ((zero_pass & refused) == BUSTER_ABI_PASS_LARGER && zero_bytes == 16);
                const bool classified = abi_is_classified(&tables.abi, open) && abi_is_classified(&tables.abi, open_wide) && abi_is_classified(&tables.abi, zero_wide);
                abi_test_expect(tally, large_right && open_right && wide_right && zero_right && classified, "fixtures", detail);
            }
            abi_test_tables_close(&tables);
        }
    }
}

// The walk without scratch memory for its stack (abi_wide_alignment): with the thread's first scratch arena replaced by
// one of `room` bytes, from 64 up, 512 more at a time, until both paths' walks have room, every classifiable type must be
// classified all the same (none waiting forever), and an _Atomic of a struct of no size that an array of none of a
// record larger than 64 bytes aligns must have its 16 bytes or be refused (UNSUPPORTED), never a slot of another size;
// and some room must give each.
static void abi_test_short_scratch(AbiTestTally* tally)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        bool walked = false;
        bool refused = false;
        for (uint64_t room = 64; !walked && room <= (1u << 20); room += 512)
        {
            AbiTestTables tables;
            if (abi_test_expect(tally, abi_test_tables_open(&tables, 4096), "fixtures", "the tables can't be made"))
            {
                TypeTable* table = &tables.types.table;
                AbiTestBuilder builder = {
                    .table = table,
                    .expectations = 0,
                    .count = 0,
                    .next_name = 30000,
                    .vector_bytes = 16,
                };
                const bool started = abi_test_tables_start(&tables, 16);
                TypeMember m[2];
                m[0] = abi_test_member(&builder, BUSTER_TYPE_INT128);
                m[1] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 100));
                m[0] = abi_test_member(&builder, type_array(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
                const TypeId atomic = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
                const Arena saved = thread_context.scratch[0];
                thread_context.scratch[0] = arena_reserve(room);
                if (path == 0)
                {
                    abi_classify_all(&tables.abi, table);
                }
                else
                {
                    abi_classify_all_scalar(&tables.abi, table);
                }
                arena_release(&thread_context.scratch[0]);
                thread_context.scratch[0] = saved;
                bool every = true;
                for (uint32_t index = 1; index < table->types.count; index += 1)
                {
                    every = every && abi_classifiable(table, index) == ((tables.abi.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0);
                }
                const uint32_t pass = tables.abi.pass[atomic >> BUSTER_TYPE_INDEX_SHIFT];
                const uint32_t bytes = tables.abi.stack_bytes[atomic >> BUSTER_TYPE_INDEX_SHIFT];
                const bool exact = (pass & (BUSTER_ABI_PASS_LARGER | BUSTER_ABI_PASS_UNSUPPORTED)) == BUSTER_ABI_PASS_LARGER && bytes == 16;
                const bool unsupported = (pass & BUSTER_ABI_PASS_UNSUPPORTED) != 0;
                walked = exact;
                refused = refused || unsupported;
                char detail[160];
                snprintf(detail, sizeof(detail), "short scratch, %" PRIu64 " bytes (%s): every type classified %d, pass %08x, stack %u", room,
                         path == 0 ? "vector" : "scalar", every, pass, bytes);
                abi_test_expect(tally, started && every && (exact || unsupported), "fixtures", detail);
                abi_test_tables_close(&tables);
            }
        }
        abi_test_expect(tally, walked && refused, "fixtures", path == 0 ? "short scratch (vector): no room gave both" : "short scratch (scalar): no room gave both");
    }
}

// The unions over each other in abi_test_shared_records.
#define BUSTER_ABI_TEST_SHARED_LEVELS 64

// What would hang if abi_wide_alignment regressed (abi_test_shared_records) runs under an alarm, whose watchdog ends the
// test with status 1.
static void abi_test_watchdog(int signal_number)
{
    (void)signal_number;
    static const char message[] = "test-calling-convention: the shared records' check didn't finish within 60 s (a walk per path)\n";
    const ssize_t written = write(1, message, sizeof(message) - 1);
    (void)written;
    _exit(1);
}

// Records that many paths lead to (abi_wide_alignment's set of finished records): W0 = {__int128, char[16]}, 32 bytes
// aligned to 16, and Wn = union {Wn-1 a, b;}, 64 of them, which a walk per path would take 2^64 frames for, under the
// watchdog's alarm. By each path every type must be classified, and an _Atomic of a struct of no size that an array of
// none of the last aligns must be a LARGER copy of 16 bytes, as _Atomic {struct {__int128, char[100]}[0]} is.
static void abi_test_shared_records(AbiTestTally* tally)
{
    signal(SIGALRM, abi_test_watchdog);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        AbiTestTables tables;
        if (abi_test_expect(tally, abi_test_tables_open(&tables, 4096), "fixtures", "the tables can't be made"))
        {
            TypeTable* table = &tables.types.table;
            AbiTestBuilder builder = {
                .table = table,
                .expectations = 0,
                .count = 0,
                .next_name = 31000,
                .vector_bytes = 16,
            };
            const bool started = abi_test_tables_start(&tables, 16);
            TypeMember m[2];
            m[0] = abi_test_member(&builder, BUSTER_TYPE_INT128);
            m[1] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 16));
            TypeId shared = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
            for (uint32_t level = 0; level < BUSTER_ABI_TEST_SHARED_LEVELS; level += 1)
            {
                m[0] = abi_test_member(&builder, shared);
                m[1] = abi_test_member(&builder, shared);
                shared = abi_test_record(&builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
            }
            m[0] = abi_test_member(&builder, type_array(table, shared, 0));
            const TypeId atomic = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
            alarm(60);
            if (path == 0)
            {
                abi_classify_all(&tables.abi, table);
            }
            else
            {
                abi_classify_all_scalar(&tables.abi, table);
            }
            alarm(0);
            bool every = true;
            for (uint32_t index = 1; index < table->types.count; index += 1)
            {
                every = every && abi_classifiable(table, index) == ((tables.abi.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0);
            }
            const uint32_t pass = tables.abi.pass[atomic >> BUSTER_TYPE_INDEX_SHIFT];
            const uint32_t bytes = tables.abi.stack_bytes[atomic >> BUSTER_TYPE_INDEX_SHIFT];
            const bool larger = (pass & (BUSTER_ABI_PASS_LARGER | BUSTER_ABI_PASS_UNSUPPORTED)) == BUSTER_ABI_PASS_LARGER && bytes == 16;
            char detail[160];
            snprintf(detail, sizeof(detail), "shared records (%s): every type classified %d, pass %08x, stack %u", path == 0 ? "vector" : "scalar", every, pass,
                     bytes);
            abi_test_expect(tally, started && every && larger, "fixtures", detail);
            abi_test_tables_close(&tables);
        }
    }
    signal(SIGALRM, SIG_DFL);
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
// f(long x5, __int128, long) puts the __int128 on the stack and the long in r9; a result in memory shifts the general
// registers by one; a struct of a flexible array member alone takes an 8-byte slot while a general register is free,
// and none after (clang's i0); {__bf16, _Alignas(8) char} takes two general registers (LOW_GENERAL); a union stored
// as an i128, which clang counts as two registers, goes on the stack after five longs, the long after it in r9; after
// an uncounted _Atomic int and four longs, packed {unsigned __int128 : 65} (an i64 and an i8) splits between r9 and
// the stack, while {unsigned __int128 : 60, unsigned __int128 : 10} (one i128) goes on the stack, the long in r9. And
// the LARGER copies (16 bytes for _Atomic(struct { __int128 x[0]; }), 17 and 26 for packed structs ending in one, a
// GROWN one's 8 once clang has no general register), and _Atomic bool vectors (an i10 in r9, uncounted; an i65's
// halves split; an i128 whole on the stack); and a union stored as an i128 with an SSE high eightbyte, which clang
// counts as two general registers too: on the stack after five longs, and in rdi and rsi before a double in xmm0. A
// vector of 4,096 bools, which isn't _Atomic, returns through the hidden pointer LLVM demotes it to, in rdi (one of
// 1,024 too); _Atomic {float vector(32768)[0]} is a copy of 32 KB, aligned to 32 KB, and a packed
// struct of it one of 32 KB and a byte, or an i8 once clang has no general register. And a result of two __int128s in a
// vector, which LLVM demotes to a hidden pointer clang doesn't count (DEMOTED), so that after it and five longs a
// struct aligned to 16, which clang counts in a register, is an i64 at stack 0 and the long after it at 8. And a vector
// of 513 bools, which LLVM copies in 80 bytes (an i513, of 128 in C), the long after it at 80, and returns through a
// hidden pointer that clang doesn't count, the sixth long on the stack.
// The inherited atomic _BitInt copies and enum-vector calls are checked alongside these vectors.
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
    m[0] = abi_test_member(&builder, type_incomplete_array(table, BUSTER_TYPE_LONG));
    const TypeId flexible = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    m[0] = abi_test_member(&builder, BUSTER_TYPE_BUILTIN(BUSTER_TYPE_CODE_BF16));
    m[1] = abi_test_member(&builder, BUSTER_TYPE_CHAR);
    m[1].alignment_log2 = 4;
    const TypeId widened = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    m[0] = abi_test_member(&builder, type_bit_int(table, 32, true));
    m[1] = abi_test_bits(&builder, type_bit_int(table, 128, true), 127, false);
    const TypeId stored = abi_test_record(&builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    m[0] = abi_test_bits(&builder, BUSTER_TYPE_UINT128, 65, true);
    const TypeId packed_wide = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    m[0] = abi_test_bits(&builder, BUSTER_TYPE_UINT128, 60, true);
    m[1] = abi_test_bits(&builder, BUSTER_TYPE_UINT128, 10, true);
    const TypeId run = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0);
    const TypeId atomic_int = type_atomic(table, BUSTER_TYPE_INT);
    const TypeId aligned_long = type_aligned(table, BUSTER_TYPE_LONG, 5);
    m[0] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_INT128, 0));
    const TypeId atomic_zero = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    const TypeId atomic_packed_zero = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0));
    m[0] = abi_test_member(&builder, atomic_zero);
    const TypeId ending = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    m[0] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 9));
    m[1] = abi_test_member(&builder, atomic_zero);
    const TypeId ending_late = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, true, 0);
    const TypeId bits10 = type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 10, true));
    const TypeId bits65 = type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 65, true));
    const TypeId bits128 = type_atomic(table, type_vector(table, BUSTER_TYPE_BOOL, 128, true));
    m[0] = abi_test_bits(&builder, BUSTER_TYPE_UINT128, 125, false);
    const TypeId unnamed_i128 = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0);
    m[0] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    m[1] = abi_test_member(&builder, BUSTER_TYPE_DOUBLE);
    m[1] = abi_test_member(&builder, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0));
    m[0] = abi_test_member(&builder, unnamed_i128);
    const TypeId sse_pair = abi_test_record(&builder, BUSTER_TYPE_KIND_UNION, m, 2, false, 0);
    const TypeId bools4096 = type_vector(table, BUSTER_TYPE_BOOL, 4096, true);
    const TypeId bools1024 = type_vector(table, BUSTER_TYPE_BOOL, 1024, true);
    m[0] = abi_test_member(&builder, type_array(table, abi_test_vector(&builder, BUSTER_TYPE_FLOAT, 32768), 0));
    const TypeId atomic_32k = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    m[0] = abi_test_member(&builder, atomic_32k);
    const TypeId ending_32k = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, true, 0);
    const TypeId int128_pair = abi_test_vector(&builder, BUSTER_TYPE_INT128, 32);
    m[0] = abi_test_member(&builder, BUSTER_TYPE_LONG);
    const TypeId aligned16 = abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 5);
    const TypeId bits513 = type_vector(table, BUSTER_TYPE_BOOL, 513, true);
    m[0] = abi_test_member(&builder, type_bit_int(table, 129, false));
    m[1] = abi_test_member(&builder, type_array(table, BUSTER_TYPE_CHAR, 4));
    m[0] = abi_test_member(&builder, type_array(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 2, false, 0), 0));
    const TypeId atomic_bytes = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
    builder.next_name += 1;
    const TypeId bool_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, builder.next_name);
    const TypeId enum_bits = type_enum_complete(table, bool_enum, BUSTER_TYPE_BOOL, 0) ? type_atomic(table, type_vector(table, bool_enum, 8, true)) : 0;
    m[0] = abi_test_member(&builder, type_array(table, type_bit_int(table, 129, true), 0));
    const TypeId atomic_unsigned_bytes = type_atomic(table, abi_test_record(&builder, BUSTER_TYPE_KIND_STRUCT, m, 1, false, 0));
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
        L,
        L,
        L,
        L,
        BUSTER_TYPE_NULLPTR,
        longs,
        L,
        // 7: long x5, __int128, long
        L,
        L,
        L,
        L,
        L,
        BUSTER_TYPE_INT128,
        L,
        // 14: long x5, nullptr_t, the high eightbyte alone, long
        L,
        L,
        L,
        L,
        L,
        BUSTER_TYPE_NULLPTR,
        high,
        L,
        // 22: double x7, _Atomic double, {long, double}, long
        D,
        D,
        D,
        D,
        D,
        D,
        D,
        atomic_double,
        mixed,
        L,
        // 32: a result in memory; long x6
        L,
        L,
        L,
        L,
        L,
        L,
        // 38: the flexible array member's struct, long
        flexible,
        L,
        // 40: long x6, the flexible array member's struct, long
        L,
        L,
        L,
        L,
        L,
        L,
        flexible,
        L,
        // 48: {__bf16, _Alignas(8) char}, long, double
        widened,
        L,
        D,
        // 51: long x5, union {unsigned _BitInt(32), unsigned _BitInt(128) : 127}, long
        L,
        L,
        L,
        L,
        L,
        stored,
        L,
        // 58: _Atomic int, long x4, packed {unsigned __int128 : 65}, long
        atomic_int,
        L,
        L,
        L,
        L,
        packed_wide,
        L,
        // 65: _Atomic int, long x4, {unsigned __int128 : 60, unsigned __int128 : 10}, long
        atomic_int,
        L,
        L,
        L,
        L,
        run,
        L,
        // 72: long x6, _Atomic {__int128[0]}, long
        L,
        L,
        L,
        L,
        L,
        L,
        atomic_zero,
        L,
        // 80: long x5, _Atomic packed {__int128[0]}, long, long
        L,
        L,
        L,
        L,
        L,
        atomic_packed_zero,
        L,
        L,
        // 88: long x6, _Atomic packed {__int128[0]}, long
        L,
        L,
        L,
        L,
        L,
        L,
        atomic_packed_zero,
        L,
        // 96: long x5, packed {_Atomic {__int128[0]}}, long, long
        L,
        L,
        L,
        L,
        L,
        ending,
        L,
        L,
        // 104: long x6, packed {_Atomic {__int128[0]}}, long
        L,
        L,
        L,
        L,
        L,
        L,
        ending,
        L,
        // 112: long x6, packed {char[9], _Atomic {__int128[0]}}, long
        L,
        L,
        L,
        L,
        L,
        L,
        ending_late,
        L,
        // 120: long x5, _Atomic bool ext10, long
        L,
        L,
        L,
        L,
        L,
        bits10,
        L,
        // 127: long x5, _Atomic bool ext65, long
        L,
        L,
        L,
        L,
        L,
        bits65,
        L,
        // 134: long x5, _Atomic bool ext128, long
        L,
        L,
        L,
        L,
        L,
        bits128,
        L,
        // 141: long x5, union {struct {unsigned __int128 : 125}, {long, double}}, long
        L,
        L,
        L,
        L,
        L,
        sse_pair,
        L,
        // 148: the union, double, long
        sse_pair,
        D,
        L,
        // 151: long, a vector of 4,096 bools as the result
        L,
        // 152: long x6, _Atomic {float vector(32768)[0]}, long
        L,
        L,
        L,
        L,
        L,
        L,
        atomic_32k,
        L,
        // 160: long x5, packed {_Atomic {float vector(32768)[0]}}, long, long
        L,
        L,
        L,
        L,
        L,
        ending_32k,
        L,
        L,
        // 168: long x6, packed {_Atomic {float vector(32768)[0]}}, long
        L,
        L,
        L,
        L,
        L,
        L,
        ending_32k,
        L,
        // 176: _Atomic {{_BitInt(129), char[4]}[0]}, long x7
        atomic_bytes,
        L,
        L,
        L,
        L,
        L,
        L,
        L,
        // 184: _Atomic of a vector of 8 of an enum of bool, long
        enum_bits,
        L,
        // 186: long x5, _Atomic {unsigned _BitInt(129)[0]}, long, long
        L,
        L,
        L,
        L,
        L,
        atomic_unsigned_bytes,
        L,
        L,
        // 194: a DEMOTED result; long x5, struct aligned(16) {long}, long
        L,
        L,
        L,
        L,
        L,
        aligned16,
        L,
        // 201: long, a vector of 1,024 bools as the result
        L,
        // 202: long x6, bool ext513, long
        L,
        L,
        L,
        L,
        L,
        L,
        bits513,
        L,
        // 210: a result of bool ext513; long x6
        L,
        L,
        L,
        L,
        L,
        L,
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
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_IGNORE, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, BUSTER_ABI_RSI, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(0), none, 0, 0 },
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
        { BUSTER_ABI_LOCATION_SPLIT, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        // 72: a 16-byte slot for the _Atomic of a byte, aligned to 16.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 16 },
        // 80: GROWN with a general register free in clang's count: a byval of 16, r9 left for the long after.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 16 },
        // 88: and with none, an i8 in 8 bytes.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 96: a packed struct ending in one, 17 bytes to LLVM: a slot of 24.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 24 },
        // 104: an i8 once clang has no general register.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 112: 26 bytes to LLVM, a type of 10, always a byval: a slot of 32.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 32 },
        // 120: an i10 in r9, uncounted, so the long clang meant for a register goes to the stack.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        // 127: an i65's halves in r9 and on the stack.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_SPLIT, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 134: an i128 on the stack whole, the long after it in r9.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        // 141: an i128 with an SSE high eightbyte, which clang counts as two general registers: a copy on the stack
        // after five longs, the long after it in r9.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        // 148: in rdi and rsi and no vector register: the double in xmm0, the long in rdx.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, BUSTER_ABI_RSI, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(0), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        // 151: the hidden pointer LLVM demotes the result to in rdi, the long in rsi.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        // 152: a copy of 32 KB aligned to 32 KB, the long after it at 32 KB.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 32768 },
        // 160: 32 KB and a byte, a slot of 32,776, r9 left for the long after.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 32776 },
        // 168: and with none, an i8 in 8 bytes.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 176: 1 byte to LLVM (the _BitInt an array of its bytes, aligned to 1), an 8-byte slot, the last long at 8.
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 184: <8 x i1>, no integer: in xmm0, the long in rdi.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_TEST_XMM(0), none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        // 186: 1 byte to LLVM, an 8-byte slot, r9 left for the long after it, the last at 8.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 194: the hidden result in rdi; the struct an i64 at stack 0 and the following long at 8.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 8 },
        // 201: as 151.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        // 184: an 80-byte copy, aligned to 128, the long after it at 80.
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RSI, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RDX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_RCX, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R8, none, 0, 0 },
        { BUSTER_ABI_LOCATION_REGISTERS, BUSTER_ABI_R9, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 0 },
        { BUSTER_ABI_LOCATION_STACK, none, none, 0, 80 },
        // 192: the hidden pointer in rdi, which clang doesn't count: the sixth long it meant for r9 on the stack.
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
        2,
        8,
        3,
        7,
        7,
        7,
        8,
        8,
        8,
        8,
        8,
        8,
        7,
        7,
        7,
        7,
        3,
        1,
        8,
        8,
        8,
        8,
        2,
        8,
        7,
        1,
        8,
        6,
    };
    const uint32_t named[] = {
        7,
        7,
        8,
        10,
        6,
        2,
        8,
        3,
        7,
        7,
        7,
        8,
        8,
        8,
        8,
        8,
        8,
        7,
        7,
        7,
        7,
        3,
        1,
        8,
        8,
        8,
        8,
        2,
        8,
        7,
        1,
        8,
        6,
    };
    const TypeId results[] = {
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        big,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        bools4096,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_LONG,
        int128_pair,
        bools1024,
        BUSTER_TYPE_VOID,
        bits513,
    };
    const uint8_t variadic[] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
    const AbiSignatures signatures = {
        .results = results,
        .counts = counts,
        .named = named,
        .variadic = variadic,
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
    const uint32_t checked = BUSTER_ARRAY_LENGTH(arguments);
    for (uint32_t i = 0; i < checked; i += 1)
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
    abi_test_expect(tally, calls[5].stack_bytes == 8 && calls[5].integer_registers == 1, "fixtures", "call 5's totals");
    abi_test_expect(tally, calls[6].stack_bytes == 8 && calls[6].integer_registers == 6, "fixtures", "call 6's totals");
    abi_test_expect(tally, calls[7].stack_bytes == 0 && calls[7].integer_registers == 3 && calls[7].vector_registers == 1, "fixtures", "call 7's totals");
    abi_test_expect(tally, calls[8].stack_bytes == 16 && calls[8].integer_registers == 6, "fixtures", "call 8's totals");
    abi_test_expect(tally, calls[9].stack_bytes == 16 && calls[9].integer_registers == 6, "fixtures", "call 9's totals");
    abi_test_expect(tally, calls[10].stack_bytes == 16 && calls[10].stack_alignment_log2 == 4 && calls[10].integer_registers == 6, "fixtures", "call 10's totals");
    abi_test_expect(tally, calls[11].stack_bytes == 24 && calls[11].stack_alignment_log2 == 4 && calls[11].integer_registers == 6, "fixtures", "call 11's totals");
    abi_test_expect(tally, calls[12].stack_bytes == 24 && calls[12].integer_registers == 6, "fixtures", "call 12's totals");
    abi_test_expect(tally, calls[13].stack_bytes == 16 && calls[13].integer_registers == 6, "fixtures", "call 13's totals");
    abi_test_expect(tally, calls[14].stack_bytes == 32 && calls[14].integer_registers == 6, "fixtures", "call 14's totals");
    abi_test_expect(tally, calls[15].stack_bytes == 16 && calls[15].integer_registers == 6, "fixtures", "call 15's totals");
    abi_test_expect(tally, calls[16].stack_bytes == 40 && calls[16].integer_registers == 6, "fixtures", "call 16's totals");
    abi_test_expect(tally, calls[17].stack_bytes == 8 && calls[17].integer_registers == 6, "fixtures", "call 17's totals");
    abi_test_expect(tally, calls[18].stack_bytes == 16 && calls[18].integer_registers == 6, "fixtures", "call 18's totals");
    abi_test_expect(tally, calls[19].stack_bytes == 16 && calls[19].stack_alignment_log2 == 4 && calls[19].integer_registers == 6, "fixtures", "call 19's totals");
    abi_test_expect(tally, calls[20].stack_bytes == 16 && calls[20].integer_registers == 6 && calls[20].vector_registers == 0, "fixtures", "call 20's totals");
    abi_test_expect(tally, calls[21].stack_bytes == 0 && calls[21].integer_registers == 3 && calls[21].vector_registers == 1, "fixtures", "call 21's totals");
    const bool demoted = calls[22].result.kind == BUSTER_ABI_RESULT_MEMORY && (calls[22].result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
    abi_test_expect(tally, demoted && calls[22].integer_registers == 2 && calls[22].stack_bytes == 0, "fixtures", "call 22's demoted result");
    abi_test_expect(tally, calls[23].stack_bytes == 32776 && calls[23].stack_alignment_log2 == 15 && calls[23].integer_registers == 6, "fixtures", "call 23's totals");
    abi_test_expect(tally, calls[24].stack_bytes == 32784 && calls[24].integer_registers == 6, "fixtures", "call 24's totals");
    abi_test_expect(tally, calls[25].stack_bytes == 16 && calls[25].integer_registers == 6, "fixtures", "call 25's totals");
    abi_test_expect(tally, calls[26].stack_bytes == 16 && calls[26].integer_registers == 6, "fixtures", "call 26's totals");
    abi_test_expect(tally, calls[27].stack_bytes == 0 && calls[27].integer_registers == 1 && calls[27].vector_registers == 1, "fixtures", "call 27's totals");
    abi_test_expect(tally, calls[28].stack_bytes == 16 && calls[28].integer_registers == 6, "fixtures", "call 28's totals");
    const bool demoted_pair = calls[29].result.kind == BUSTER_ABI_RESULT_MEMORY && (calls[29].result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
    abi_test_expect(tally, demoted_pair && calls[29].stack_bytes == 16 && calls[29].integer_registers == 6, "fixtures", "call 29's totals");
    const bool demoted_1024 = calls[30].result.kind == BUSTER_ABI_RESULT_MEMORY && (calls[30].result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
    abi_test_expect(tally, demoted_1024 && calls[30].integer_registers == 2, "fixtures", "call 30's demoted result");
    abi_test_expect(tally, calls[31].stack_bytes == 88 && calls[31].stack_alignment_log2 == 7 && calls[31].integer_registers == 6, "fixtures", "call 31's totals");
    abi_test_expect(tally,
                    calls[32].result.kind == BUSTER_ABI_RESULT_MEMORY && (calls[32].result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0 && calls[32].integer_registers == 6 &&
                        calls[32].stack_bytes == 8,
                    "fixtures", "call 32's hidden pointer");
    // va_arg aligns the overflow area as the canonical type is aligned (EmitVAArg): long aligned(32) reads from 8.
    const AbiVaArg plan = abi_va_arg(&tables->abi, table, aligned_long);
    abi_test_expect(tally, plan.kind == BUSTER_ABI_LOCATION_REGISTERS && plan.overflow_alignment_log2 == 3 && plan.stack_bytes == 8, "fixtures",
                    "va_arg of long aligned(32)");
    // And the union of calls 20 and 21 from two general registers' slots, as its calls pass it.
    const AbiVaArg pair_plan = abi_va_arg(&tables->abi, table, sse_pair);
    abi_test_expect(tally, pair_plan.kind == BUSTER_ABI_LOCATION_REGISTERS && pair_plan.integer_registers == 2 && pair_plan.vector_registers == 0, "fixtures",
                    "va_arg of an i128 with an SSE high eightbyte");
}

// Short atomic vectors use 128-bit legalized register types, including a single half. Once eight vector registers
// are occupied, two take 16-byte slots and the next integer starts at 32. Single ordinary elements and builtin bool
// vectors use scalar 8-byte slots: controls against widening the memory representation instead of the register type.
static void abi_test_atomic_vector_overflow(AbiTestTally* tally, AbiTestTables* tables, uint32_t vector_bytes, bool vector)
{
    TypeTable* table = &tables->types.table;
    const TypeId bool_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 30000);
    abi_test_expect(tally, type_enum_complete(table, bool_enum, BUSTER_TYPE_BOOL, 0), "fixtures", "overflow enum completion");
    const TypeId elements[] = {
        BUSTER_TYPE_FLOAT16,
        BUSTER_TYPE_BF16,
        bool_enum,
        BUSTER_TYPE_CHAR,
        BUSTER_TYPE_SHORT,
        BUSTER_TYPE_FLOAT,
        BUSTER_TYPE_BOOL,
    };
    const uint32_t lengths[][4] = {
        {
            1,
            2,
            3,
            4,
        },
        {
            1,
            2,
            3,
            4,
        },
        {
            1,
            2,
            4,
            8,
        },
        {
            1,
            2,
            4,
            8,
        },
        {
            1,
            2,
            4,
            8,
        },
        {
            1,
            2,
            3,
            4,
        },
        {
            1,
            2,
            4,
            8,
        },
    };
    TypeId atomics[BUSTER_ARRAY_LENGTH(elements)][BUSTER_ARRAY_LENGTH(lengths[0])];
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(elements); e += 1)
    {
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(lengths[e]); n += 1)
        {
            atomics[e][n] = type_atomic(table, type_vector(table, elements[e], lengths[e][n], true));
        }
    }
    if (vector)
    {
        abi_classify_all(&tables->abi, table);
    }
    else
    {
        abi_classify_all_scalar(&tables->abi, table);
    }
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(elements); e += 1)
    {
        for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(lengths[e]); n += 1)
        {
            TypeId arguments[17];
            for (uint32_t a = 0; a < 14; a += 1)
            {
                arguments[a] = a < 8 ? BUSTER_TYPE_DOUBLE : BUSTER_TYPE_LONG;
            }
            arguments[14] = atomics[e][n];
            arguments[15] = atomics[e][n];
            arguments[16] = BUSTER_TYPE_LONG;
            const uint32_t counts[] = {
                BUSTER_ARRAY_LENGTH(arguments),
            };
            const TypeId results[] = {
                BUSTER_TYPE_VOID,
            };
            const uint8_t variadic[] = {
                0,
            };
            const AbiSignatures signatures = {
                .results = results,
                .counts = counts,
                .named = counts,
                .variadic = variadic,
                .count = 1,
                .arguments = arguments,
            };
            AbiLocation locations[BUSTER_ARRAY_LENGTH(arguments)];
            AbiCall calls[1];
            if (vector)
            {
                abi_assign(&tables->abi, signatures, locations, calls);
            }
            else
            {
                abi_assign_scalar(&tables->abi, signatures, locations, calls);
            }
            const uint32_t index = atomics[e][n] >> BUSTER_TYPE_INDEX_SHIFT;
            const uint32_t pass = tables->abi.pass[index];
            const uint32_t slot = e < 2 || (e != 6 && lengths[e][n] > 1) ? 16 : 8;
            const uint32_t alignment = slot == 16 ? 4 : 3;
            const bool supported = (pass & BUSTER_ABI_PASS_UNSUPPORTED) == 0 && tables->abi.stack_bytes[index] == slot &&
                                   ((pass & BUSTER_ABI_PASS_ALIGNMENT) >> BUSTER_ABI_PASS_ALIGNMENT_SHIFT) == alignment;
            const bool slots = locations[14].kind == BUSTER_ABI_LOCATION_STACK && locations[14].stack_offset == 0 &&
                               locations[15].kind == BUSTER_ABI_LOCATION_STACK && locations[15].stack_offset == slot &&
                               locations[16].kind == BUSTER_ABI_LOCATION_STACK && locations[16].stack_offset == slot * 2;
            const bool totals = calls[0].stack_bytes == slot * 2 + 8 && calls[0].stack_alignment_log2 == BUSTER_ABI_STACK_ALIGNMENT_LOG2 && calls[0].integer_registers == 6 && calls[0].vector_registers == 8;
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "atomic vector overflow: element %u, length %u, %u-byte vectors, %s: slot %u, alignment %u; offsets %u/%u/%u; call %u bytes, alignment %u, registers %u/%u",
                     e, lengths[e][n], vector_bytes, vector ? "vector" : "scalar", tables->abi.stack_bytes[index],
                     (pass & BUSTER_ABI_PASS_ALIGNMENT) >> BUSTER_ABI_PASS_ALIGNMENT_SHIFT, locations[14].stack_offset, locations[15].stack_offset,
                     locations[16].stack_offset, calls[0].stack_bytes, calls[0].stack_alignment_log2, calls[0].integer_registers, calls[0].vector_registers);
            abi_test_expect(tally, supported && slots && totals, "fixtures", detail);
        }
    }
}

// Plain char extends as the table's char is signed (TypeTable.char_unsigned, -funsigned-char), as clang's does, and so
// does an enum of a fixed underlying char; the other character types, short and bool keep theirs. A call's locations
// and result carry each one's: a char argument, and a char result, with every other type an argument after it.
static void abi_test_char_signedness(AbiTestTally* tally)
{
    for (uint32_t unsigned_char = 0; unsigned_char < 2; unsigned_char += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            AbiTestTables tables;
            if (abi_test_expect(tally, abi_test_tables_open(&tables, 64), "fixtures", "the tables can't be made"))
            {
                TypeTable* table = &tables.types.table;
                table->char_unsigned = unsigned_char != 0;
                const TypeId char_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
                const TypeId schar_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
                const bool complete = char_enum != 0 && schar_enum != 0 && type_enum_complete(table, char_enum, BUSTER_TYPE_CHAR, 0) &&
                                      type_enum_complete(table, schar_enum, BUSTER_TYPE_SCHAR, 0);
                const uint32_t plain = unsigned_char != 0 ? BUSTER_ABI_PASS_ZERO : BUSTER_ABI_PASS_SIGN;
                const TypeId types[] = {
                    BUSTER_TYPE_CHAR,
                    char_enum,
                    BUSTER_TYPE_SCHAR,
                    schar_enum,
                    BUSTER_TYPE_UCHAR,
                    BUSTER_TYPE_SHORT,
                    BUSTER_TYPE_USHORT,
                    BUSTER_TYPE_BOOL,
                };
                const uint32_t extensions[] = {
                    plain,
                    plain,
                    BUSTER_ABI_PASS_SIGN,
                    BUSTER_ABI_PASS_SIGN,
                    BUSTER_ABI_PASS_ZERO,
                    BUSTER_ABI_PASS_SIGN,
                    BUSTER_ABI_PASS_ZERO,
                    BUSTER_ABI_PASS_ZERO,
                };
                const char* names[] = {
                    "char",
                    "enum : char",
                    "signed char",
                    "enum : signed char",
                    "unsigned char",
                    "short",
                    "unsigned short",
                    "bool",
                };
                const bool started = abi_test_expect(tally, complete && abi_test_tables_start(&tables, 16), "fixtures", "the char signedness tables can't be made");
                if (started && path == 0)
                {
                    abi_classify_all(&tables.abi, table);
                }
                else if (started)
                {
                    abi_classify_all_scalar(&tables.abi, table);
                }
                const uint32_t counts[] = {
                    BUSTER_ARRAY_LENGTH(types),
                };
                const TypeId results[] = {
                    BUSTER_TYPE_CHAR,
                };
                const uint8_t variadic[] = {
                    0,
                };
                const AbiSignatures signatures = {
                    .results = results,
                    .counts = counts,
                    .named = counts,
                    .variadic = variadic,
                    .count = 1,
                    .arguments = types,
                };
                AbiLocation locations[BUSTER_ARRAY_LENGTH(types)];
                AbiCall calls[1];
                if (started && path == 0)
                {
                    abi_assign(&tables.abi, signatures, locations, calls);
                }
                else if (started)
                {
                    abi_assign_scalar(&tables.abi, signatures, locations, calls);
                }
                for (uint32_t i = 0; started && i < BUSTER_ARRAY_LENGTH(types); i += 1)
                {
                    const uint32_t index = types[i] >> BUSTER_TYPE_INDEX_SHIFT;
                    const uint32_t pass = tables.abi.pass[index];
                    const uint8_t flag = extensions[i] == BUSTER_ABI_PASS_SIGN ? BUSTER_ABI_FLAG_SIGN : BUSTER_ABI_FLAG_ZERO;
                    const uint8_t extension = BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO;
                    // Six in the general registers, then two in stack slots, with the same flags.
                    const uint8_t kind = i < 6 ? BUSTER_ABI_LOCATION_REGISTERS : BUSTER_ABI_LOCATION_STACK;
                    const bool same = (pass & (BUSTER_ABI_PASS_SIGN | BUSTER_ABI_PASS_ZERO)) == extensions[i] && (pass & BUSTER_ABI_PASS_MEMORY) == 0 &&
                                      (tables.abi.results[index].flags & extension) == flag && locations[i].kind == kind &&
                                      (locations[i].flags & extension) == flag;
                    char detail[192];
                    snprintf(detail, sizeof(detail), "%s with %s char (%s): pass %08x, result flags %02x, location kind %u flags %02x", names[i],
                             unsigned_char != 0 ? "unsigned" : "signed", path == 0 ? "vector" : "scalar", pass, tables.abi.results[index].flags,
                             locations[i].kind, locations[i].flags);
                    abi_test_expect(tally, same, "fixtures", detail);
                }
                if (started)
                {
                    const uint8_t flag = unsigned_char != 0 ? BUSTER_ABI_FLAG_ZERO : BUSTER_ABI_FLAG_SIGN;
                    abi_test_expect(tally, calls[0].result.kind == BUSTER_ABI_RESULT_REGISTERS && calls[0].result.registers[0] == BUSTER_ABI_RAX &&
                                    (calls[0].result.flags & (BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO)) == flag, "fixtures",
                                    unsigned_char != 0 ? "a call's char result with unsigned char" : "a call's char result with signed char");
                }
                abi_test_tables_close(&tables);
            }
        }
    }
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
                abi_test_build_split_vectors(&builder);
                abi_test_build_records(&builder);
                abi_test_build_atomics(&builder);
                abi_test_expect(&tally, builder.count < BUSTER_ABI_TEST_FIXTURES, "fixtures", "more fixtures than BUSTER_ABI_TEST_FIXTURES holds");
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
                    const bool same_result = e.result_kind == BUSTER_ABI_TEST_RESULT_ANY ||
                                             (result.kind == e.result_kind && (e.result_kind != BUSTER_ABI_RESULT_REGISTERS ||
                                                                               (result.registers[0] == e.result[0] && result.registers[1] == e.result[1])));
                    const bool same_detail = !e.detail || (result.flags == e.result_flags && result.part_bytes == e.part_bytes && result.registers[2] == e.third);
                    const bool same = classified && abi_classes(&tables.abi, e.type, true) == e.named && abi_classes(&tables.abi, e.type, false) == e.unnamed &&
                                      (pass & e.set) == e.set && (pass & e.clear) == 0 && same_result && same_detail &&
                                      (e.stack_bytes == 0 || tables.abi.stack_bytes[index] == e.stack_bytes);
                    snprintf(detail, sizeof(detail), "%s (%u bytes, %s): classes %02x/%02x, pass %08x, stack %u, result %u (%02x %02x %02x, flags %02x, part %u)",
                             e.name, widths[w], path == 0 ? "vector" : "scalar", abi_classes(&tables.abi, e.type, true), abi_classes(&tables.abi, e.type, false), pass,
                             classified ? tables.abi.stack_bytes[index] : 0, result.kind, result.registers[0], result.registers[1], result.registers[2], result.flags,
                             result.part_bytes);
                    abi_test_expect(&tally, same, "fixtures", detail);
                }
                abi_test_calls(&tally, &tables, widths[w], path == 0);
                abi_test_atomic_vector_overflow(&tally, &tables, widths[w], path == 0);
                abi_test_tables_close(&tables);
            }
        }
    }
    abi_test_ordering(&tally);
    abi_test_short_scratch(&tally);
    abi_test_shared_records(&tally);
    abi_test_char_signedness(&tally);
    abi_test_report("fixtures", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Worlds: random types for the calling convention, in a TypeTestWorld (c_types_test.c) so that the cross-check can
// write them as C. Small aggregates mostly, which is where the classes are decided member by member. `gcc` leaves out
// what only clang spells (clang's ext_vector_type, __fp16, complex _BitInts) and what GCC passes otherwise: _Atomic
// types, flexible array members, vectors of 4 bytes or less of floating elements, a vector of one __int128 (one
// eightbyte to GCC in an aggregate), unnamed bit-fields of a nonzero width (GCC classifies them, clang skips them),
// members of no size (GCC ignores them where clang makes a wide record or offset 16 MEMORY), misaligned members (GCC
// checks a member's scalars against their sizes, and no _BitInt, where clang checks each member's alignment), so no
// packing and no alignment a typedef lowers; and what GCC lays out otherwise: arrays whose element's size isn't a
// multiple of its alignment (GCC refuses them), bit-fields of aligned typedefs, _BitInt(128) bit-fields of all 128 bits
// (GCC aligns them to 16 at an offset 16 divides), abi_test_gcc_enum_differs's enums, and aligned typedefs whose
// declaration spells a vector: GCC applies a declarator's attributes before the vector_size of its specifiers, so it
// drops the aligned of `typedef long __attribute__((vector_size(8))) *T __attribute__((aligned(32)))`.

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
    // A clang world without _Atomics of types of no size, the roots of every LARGER type, so that the levels don't make
    // every record wait for its members (abi_classify_levels' `all`) and only their own waits keep them right.
    bool rootless;
    // A world of what this compiler's C passes (#137): integers of up to 64 bits, enums, pointers, float, double, and
    // the records and aligned typedefs made of them; our side of its cross-check is C that this compiler compiles.
    bool compiled;
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

// Whether GCC gives an enum another type than clang: one with an aligned attribute, which GCC ignores, or one whose
// values no 64-bit type holds (a negative one and one past INT64_MAX), to which GCC gives __int128 and clang long.
static bool abi_test_gcc_enum_differs(const AbiTestWorld* world, TypeId type)
{
    const TypeTable* table = &world->types.test.table;
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    bool result = false;
    if (table->types.kind[index] == BUSTER_TYPE_KIND_ENUM)
    {
        const uint32_t tag = table->types.index[index];
        const uint32_t begin = world->types.enum_begin[tag];
        const uint32_t end = begin + world->types.enum_count[tag];
        bool negative = false;
        bool huge = false;
        for (uint32_t i = begin; i < end; i += 1)
        {
            negative = negative || (world->types.enum_unsigned[i] == 0 && world->types.enum_values[i] < 0);
            huge = huge || world->types.enum_unsigned[i] != 0;
        }
        result = table->tags.alignment_log2[tag] != 0 || (negative && huge);
    }
    return result;
}

// Whether a type's spelling writes a vector_size attribute out: a vector's, or a pointer's or an array's that leads to
// one (an ALIGNED type, which is spelled by its name, ends the walk).
static bool abi_test_spells_vector(const TypeTable* table, TypeId type)
{
    TypeId walk = type_strip_aligned(table, type);
    while (type_kind(table, walk) == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(type_kind(table, walk)))
    {
        walk = type_element(table, walk);
    }
    return type_kind(table, walk) == BUSTER_TYPE_KIND_VECTOR;
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
        // GCC places a bit-field of an aligned typedef by the typedef's alignment, and clang by its type's.
        const bool aligned = type_kind(table, type) == BUSTER_TYPE_KIND_ALIGNED;
        // GCC classifies a union's bit-field by its type, not its width (a zero-width one too): none in its worlds.
        if (pick < 3 && type_test_bit_field_type(table, type) && (type_qualifiers(type) & BUSTER_TYPE_CONST) == 0 && !(world->gcc && (aligned || is_union)))
        {
            // GCC gives a _BitInt(128) bit-field of all 128 bits at an offset 16 divides TImode's alignment, 16, which
            // raises its record's (clang keeps the type's, 8): none in its worlds.
            const uint64_t bits = type_integer_width(table, type);
            const bool full_bit_int = world->gcc && type_kind(table, type) == BUSTER_TYPE_KIND_BIT_INT && bits == 128;
            width = (uint32_t)type_test_below(random, full_bit_int ? bits : bits + 1);
        }
        else if (pick < 5)
        {
            // Now and then an array of no elements of any type, a large record among them, which clang keeps in its LLVM
            // type aligned as the record (abi_wide_alignment, which the levels wait for). GCC refuses an array of length
            // 0 before a struct's last member.
            const bool any = !world->gcc && type_test_below(random, 4) == 0;
            const TypeId element = abi_test_world_pick(world, random, !any);
            const uint64_t element_size = type_size(table, element);
            const uint64_t lengths = element_size == 0 ? 3 : element_size <= 4 ? 5 : 3;
            const uint64_t length = any ? 0 : world->gcc ? 1 + type_test_below(random, lengths - 1) : type_test_below(random, lengths);
            // GCC refuses an array whose element's size isn't a multiple of its alignment.
            const bool divisible = element_size % type_alignment(table, element) == 0;
            const TypeId array = !world->gcc || divisible ? type_array(table, type_unqualified(element), length) : 0;
            type = array != 0 ? array : type;
            table->last_error = TYPE_ERROR_NONE;
        }
        type = world->gcc && type_size(table, type) == 0 ? BUSTER_TYPE_CHAR : type;
        const bool unnamed = width == 0 || (width != BUSTER_TYPE_NOT_BIT_FIELD && type_test_below(random, 4) == 0 && !world->gcc);
        members[m] = (TypeMember){
            .name = unnamed ? 0 : type_test_new_name(&world->types),
            .type = type,
            .bit_width = width,
            .alignment_log2 = !unnamed && type_test_below(random, 16) == 0 ? (uint8_t)(1 + type_test_below(random, world->compiled ? 4 : 5)) : 0,
            .packed = !unnamed && type_test_below(random, 16) == 0 && !world->gcc,
        };
    }
    uint32_t total = count;
    if (!world->gcc && !is_union && count > 0 && type_test_below(random, 16) == 0)
    {
        const TypeId element = abi_test_world_pick(world, random, true);
        const TypeId open = type_incomplete_array(table, type_unqualified(element));
        members[count - 1] = open != 0 ? type_test_member(type_test_new_name(&world->types), open, BUSTER_TYPE_NOT_BIT_FIELD) : members[count - 1];
        table->last_error = TYPE_ERROR_NONE;
    }
    const TypeRecordAttributes attributes = {
        .packed = type_test_below(random, 8) == 0 && !world->gcc,
        .pack_log2 = type_test_below(random, 10) == 0 && !world->gcc ? (uint8_t)(1 + type_test_below(random, 4)) : 0,
        .alignment_log2 = type_test_below(random, 10) == 0 ? (uint8_t)(1 + type_test_below(random, world->compiled ? 4 : 6)) : 0,
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

// The records clang's directed signatures pass (abi_test_cross_signatures): runs of bit-fields past an eightbyte that
// are one i128 or two parts, packed and not (see abi_bit_field_run). Returns how many it made into `directed`.
#define BUSTER_ABI_TEST_DIRECTED 5

static uint32_t abi_test_world_directed(AbiTestWorld* world, TypeId* directed)
{
    TypeTable* table = &world->types.test.table;
    const TypeId bit_int = type_bit_int(table, 128, false);
    const uint32_t widths[BUSTER_ABI_TEST_DIRECTED][2] = {
        { 65, 0 },
        { 65, 0 },
        { 65, 0 },
        { 60, 10 },
        { 65, 0 },
    };
    uint32_t count = 0;
    for (uint32_t d = 0; d < BUSTER_ABI_TEST_DIRECTED; d += 1)
    {
        // {unsigned __int128 : 65} and packed; followed by a char; two bit-fields in one run; packed _BitInt(128) : 65.
        const TypeId type = d == 4 ? bit_int : BUSTER_TYPE_UINT128;
        TypeMember members[2] = {
            type_test_member(type_test_new_name(&world->types), type, widths[d][0]),
            widths[d][1] != 0 ? type_test_member(type_test_new_name(&world->types), type, widths[d][1])
                              : type_test_member(type_test_new_name(&world->types), BUSTER_TYPE_CHAR, BUSTER_TYPE_NOT_BIT_FIELD),
        };
        const uint32_t member_count = d == 2 || d == 3 ? 2 : 1;
        const TypeRecordAttributes attributes = {
            .packed = d == 1 || d == 4,
            .pack_log2 = 0,
            .alignment_log2 = 0,
        };
        const TypeId record = type_tag(table, BUSTER_TYPE_KIND_STRUCT, type_test_new_name(&world->types));
        if (record != 0 && type_complete_record(table, record, members, member_count, attributes))
        {
            type_test_world_completed(&world->types, record);
            abi_test_world_add(world, record);
            directed[count] = record;
            count += 1;
        }
        table->last_error = TYPE_ERROR_NONE;
    }
    return count;
}

static void abi_test_world_build(AbiTestWorld* world, uint64_t seed, uint32_t count)
{
    uint64_t random = seed;
    TypeTable* table = &world->types.test.table;
    for (uint32_t code = BUSTER_TYPE_CODE_BOOL; code < BUSTER_TYPE_BUILTIN_COUNT; code += 1)
    {
        const bool complex = code >= BUSTER_TYPE_CODE_COMPLEX;
        const bool spellable = !complex || (BUSTER_TYPE_COMPLEX_SPELLABLE >> (code - BUSTER_TYPE_CODE_COMPLEX) & 1) != 0;
        const bool clang_only = code == BUSTER_TYPE_CODE_HALF || code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_FLOAT128 || !spellable;
        // GCC classifies a _Complex _Float16 at any offset but an eightbyte's start as two classes, the second the next
        // eightbyte's (classify_argument's HCmode): no member is made of one (an argument of one is at 0).
        const bool gcc_member = code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_FLOAT16;
        const TypeId builtin = BUSTER_TYPE_BUILTIN(code);
        const bool valid = type_is_valid(table, builtin);
        const bool complex_floating = code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_DOUBLE ||
                                      code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_LONG_DOUBLE;
        const bool compiled = !world->compiled || (valid && ((type_kind(table, builtin) == BUSTER_TYPE_KIND_INTEGER && type_size(table, builtin) <= 16) ||
                                                             builtin == BUSTER_TYPE_FLOAT || builtin == BUSTER_TYPE_DOUBLE || builtin == BUSTER_TYPE_LONG_DOUBLE ||
                                                             complex_floating));
        if (valid && code != BUSTER_TYPE_CODE_NULLPTR && (!world->gcc || (!clang_only && !gcc_member)) && compiled)
        {
            abi_test_world_add(world, builtin);
        }
    }
    for (uint32_t i = 0; i < 12; i += 1)
    {
        type_test_world_enum(&world->types, &random);
        const TypeId enumeration = world->types.pool.types[world->types.pool.count - 1];
        abi_test_world_add(world, world->gcc && abi_test_gcc_enum_differs(world, enumeration) ? 0 : enumeration);
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
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths) && !world->compiled; i += 1)
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
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(elements) && !world->compiled; e += 1)
    {
        const bool floating = type_kind(table, elements[e]) == BUSTER_TYPE_KIND_FLOATING;
        for (uint64_t bytes = 2; bytes <= 64; bytes *= 2)
        {
            const bool gcc_divergent = (floating && bytes <= 4) || (elements[e] == BUSTER_TYPE_INT128 && bytes == 16);
            if (bytes >= type_size(table, elements[e]) && !(world->gcc && gcc_divergent))
            {
                abi_test_world_add(world, type_vector_size(table, elements[e], bytes));
                table->last_error = TYPE_ERROR_NONE;
            }
        }
    }
    if (!world->gcc && !world->compiled)
    {
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_FLOAT, 3, true));
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_INT, 3, true));
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_CHAR, 5, true));
        abi_test_world_add(world, type_complex(table, type_bit_int(table, 13, false)));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_INT));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_DOUBLE));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_INT128));
        abi_test_world_add(world, type_atomic(table, BUSTER_TYPE_LONG_DOUBLE));
        // Vectors LLVM breaks up or lays out otherwise than C (abi_vector_llvm): of bools, narrow _BitInts and long
        // doubles, which clang's LLVM type packs in memory, of __float128s and of _BitInts of 128 bits and more, and of
        // lengths that aren't a power of two past 16 bytes. The signatures pass none of those it refuses, but records hold
        // them (the _Atomic ones of bools, which clang passes as integers, crash its atomic loads at most widths: the
        // fixtures check those).
        const uint32_t bool_lengths[] = {
            8,
            9,
            16,
            33,
            64,
            65,
            100,
            128,
            129,
            200,
            257,
            384,
            513,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bool_lengths); i += 1)
        {
            abi_test_world_add(world, type_vector(table, BUSTER_TYPE_BOOL, bool_lengths[i], true));
        }
        for (uint64_t bytes = 16; bytes <= 64; bytes *= 2)
        {
            abi_test_world_add(world, type_vector_size(table, BUSTER_TYPE_LONG_DOUBLE, bytes));
            abi_test_world_add(world, type_vector_size(table, BUSTER_TYPE_FLOAT128, bytes));
        }
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_LONG_DOUBLE, 3, true));
        abi_test_world_add(world, type_vector(table, BUSTER_TYPE_FLOAT128, 3, true));
        const uint32_t bit_int_vectors[][2] = {
            { 128, 1 },
            { 128, 2 },
            { 128, 4 },
            { 256, 1 },
            { 256, 2 },
            { 512, 1 },
            { 4, 8 },
            { 4, 16 },
            { 4, 32 },
            { 4, 64 },
            { 2, 64 },
            { 1, 128 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bit_int_vectors); i += 1)
        {
            abi_test_world_add(world, type_vector(table, type_bit_int(table, bit_int_vectors[i][0], true), bit_int_vectors[i][1], true));
        }
        const uint32_t odd_vectors[][2] = {
            { BUSTER_TYPE_FLOAT, 5 },
            { BUSTER_TYPE_FLOAT, 6 },
            { BUSTER_TYPE_FLOAT, 9 },
            { BUSTER_TYPE_DOUBLE, 3 },
            { BUSTER_TYPE_DOUBLE, 5 },
            { BUSTER_TYPE_LONG, 3 },
            { BUSTER_TYPE_LONG, 5 },
            { BUSTER_TYPE_INT, 5 },
            { BUSTER_TYPE_SHORT, 9 },
            { BUSTER_TYPE_CHAR, 17 },
            { BUSTER_TYPE_FLOAT16, 9 },
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(odd_vectors); i += 1)
        {
            abi_test_world_add(world, type_vector(table, odd_vectors[i][0], odd_vectors[i][1], true));
        }
        // Structs of an array of no size, and _Atomics of them, which LLVM makes as large as it aligns them (LARGER):
        // the records made later hold them, and a packed one that ends in one is larger to LLVM too.
        const TypeId zero_elements[3] = {
            BUSTER_TYPE_INT128,
            BUSTER_TYPE_LONG,
            BUSTER_TYPE_INT128,
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(zero_elements); i += 1)
        {
            TypeMember member = type_test_member(type_test_new_name(&world->types), type_array(table, zero_elements[i], 0), BUSTER_TYPE_NOT_BIT_FIELD);
            const TypeRecordAttributes attributes = {
                .packed = i == 2,
                .pack_log2 = 0,
                .alignment_log2 = 0,
            };
            const TypeId zero = type_tag(table, BUSTER_TYPE_KIND_STRUCT, type_test_new_name(&world->types));
            const bool completed = zero != 0 && type_complete_record(table, zero, &member, 1, attributes);
            if (completed)
            {
                type_test_world_completed(&world->types, zero);
                abi_test_world_add(world, zero);
                abi_test_world_add(world, world->rootless ? 0 : type_atomic(table, zero));
            }
            table->last_error = TYPE_ERROR_NONE;
        }
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
            // (Aligned to 16 at most in a compiled world, which this compiler's frame objects are.)
            const TypeId base = abi_test_world_pick(world, &random, true);
            const uint32_t alignment_log2 = (uint32_t)type_test_below(&random, world->compiled ? 5 : 7);
            const bool lowered = alignment_log2 <= type_alignment_log2(table, type_strip_aligned(table, base));
            const bool gcc_divergent = lowered || abi_test_spells_vector(table, base);
            abi_test_world_add(world, world->gcc && gcc_divergent ? 0 : type_aligned(table, type_unqualified(base), alignment_log2));
            table->last_error = TYPE_ERROR_NONE;
        }
        else if (pick < 13 && !world->gcc && !world->compiled)
        {
            // Of a type of no size too, whose LLVM type is larger (LARGER: _Atomic(struct { __int128 x[0]; }) is 16 bytes
            // to LLVM), and of an ext_vector_type, which LLVM may split into its elements (UNSUPPORTED: an _Atomic <5 x
            // i8> is five i8s, a general register each).
            const TypeId base = abi_test_world_pick(world, &random, true);
            const bool root = table->types.size[abi_resolved_index(table, base)] == 0;
            abi_test_world_add(world, world->rootless && root ? 0 : type_atomic(table, type_unqualified(base)));
            table->last_error = TYPE_ERROR_NONE;
        }
        else if (pick < 14 || world->compiled)
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

// A record's line and its members' lines as the table has them, for a failure's report (abi_test_dump).
static void abi_test_dump_record(const TypeTable* table, const AbiTable* abi, uint32_t index, uint32_t depth)
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
    }
}

// A record's members as the table has them, a level deep: the record, then each record among its members.
static void abi_test_dump(const TypeTable* table, const AbiTable* abi, uint32_t index)
{
    abi_test_dump_record(table, abi, index, 0);
    const uint32_t tag = table->types.index[index];
    const uint32_t begin = table->tags.member_begin[tag];
    const uint32_t end = begin + table->tags.member_count[tag];
    for (uint32_t member = begin; member < end; member += 1)
    {
        const uint32_t resolved = abi_resolved_index(table, table->members.type[member]);
        if (table->types.kind[resolved] == BUSTER_TYPE_KIND_STRUCT || table->types.kind[resolved] == BUSTER_TYPE_KIND_UNION)
        {
            abi_test_dump_record(table, abi, resolved, 1);
        }
    }
}

// Every classifiable type, by the vector path and by the twin, at each vector width: identical tables, and every type at
// every offset it can be at the oracle's classes. Three clang worlds, at 16, 32 and 64 bytes, a GCC one at 16, and a
// rootless clang one at 32 (AbiTestWorld.rootless), whose levels only the waits keep right.
static bool abi_test_worlds(uint64_t seed, uint32_t count)
{
    AbiTestTally tally = { 0 };
    uint64_t compared = 0;
    AbiTestFrame* frames = malloc(sizeof(AbiTestFrame) * BUSTER_ABI_TEST_FRAMES);
    char detail[256];
    for (uint32_t round = 0; frames != 0 && round < 5; round += 1)
    {
        AbiTestWorld world;
        if (abi_test_expect(&tally, abi_test_world_open(&world, count * 8 + 4096), "worlds", "the world can't be made"))
        {
            world.gcc = round == 3;
            world.rootless = round == 4;
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
                                      vector.alignment_log2[index] == scalar.alignment_log2[index] && vector.storage[index] == scalar.storage[index] &&
                                      vector.layout[index] == scalar.layout[index] && memcmp(&vector.results[index], &scalar.results[index], sizeof(AbiReturn)) == 0;
                    snprintf(detail, sizeof(detail),
                             "type %u (kind %u): the vector path and the twin differ: pass %08x/%08x, stack %u/%u, alignment %u/%u, storage %02x/%02x, layout %012" PRIx64
                             "/%012" PRIx64 ", row[0] %02x/%02x",
                             index, table->types.kind[index], vector.pass[index], scalar.pass[index], vector.stack_bytes[index], scalar.stack_bytes[index],
                             vector.alignment_log2[index], scalar.alignment_log2[index], vector.storage[index], scalar.storage[index], vector.layout[index],
                             scalar.layout[index], vector.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES], scalar.rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES]);
                    if (classified && !same && tally.failures < BUSTER_ABI_TEST_REPORTED)
                    {
                        abi_test_dump(table, &vector, index);
                        abi_test_dump(table, &scalar, index);
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
                                    abi_test_dump(table, &vector, index);
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
                // The twin's table classified again, every type at once with all it reads classified, must not change: the
                // levels take a type only when what it reads is classified (a zero-length array of a large record waits
                // for every type abi_wide_alignment reads), and the paths share them. Only the rootless world shows a
                // missing wait: in the others every record waits for its members anyway.
                TypeId* again = malloc(sizeof(TypeId) * table->types.count);
                uint32_t again_count = 0;
                for (uint32_t index = 1; again != 0 && index < table->types.count; index += 1)
                {
                    again[again_count] = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
                    again_count += (scalar.pass[index] & BUSTER_ABI_PASS_CLASSIFIED) != 0 ? 1 : 0;
                }
                abi_classify_scalar(&scalar, table, again, again_count);
                abi_test_expect(&tally, again != 0, "worlds", "no memory to classify again");
                for (uint32_t index = 1; again != 0 && index < table->types.count; index += 1)
                {
                    const bool same = memcmp(vector.rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES, scalar.rows + (uint64_t)index * BUSTER_ABI_ROW_BYTES,
                                             BUSTER_ABI_ROW_BYTES) == 0 &&
                                      vector.pass[index] == scalar.pass[index] && vector.stack_bytes[index] == scalar.stack_bytes[index] &&
                                      vector.alignment_log2[index] == scalar.alignment_log2[index] && vector.storage[index] == scalar.storage[index] &&
                                      vector.layout[index] == scalar.layout[index] && memcmp(&vector.results[index], &scalar.results[index], sizeof(AbiReturn)) == 0;
                    snprintf(detail, sizeof(detail), "type %u (kind %u) classified again: pass %08x/%08x, storage %02x/%02x, layout %012" PRIx64 "/%012" PRIx64,
                             index, table->types.kind[index], vector.pass[index], scalar.pass[index], vector.storage[index], scalar.storage[index], vector.layout[index],
                             scalar.layout[index]);
                    abi_test_expect(&tally, same, "worlds", detail);
                }
                free(again);
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

// A type an argument or a result may have: not an array, a function or __fp16, not UNSUPPORTED, nor too large to pass.
static bool abi_test_passable(const AbiTable* abi, const TypeTable* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t resolved = abi_resolved_index(table, type);
    const uint8_t kind = table->types.kind[resolved];
    const bool half = kind == BUSTER_TYPE_KIND_FLOATING && table->types.code[resolved] == BUSTER_TYPE_CODE_HALF;
    return (abi->pass[index] & (BUSTER_ABI_PASS_CLASSIFIED | BUSTER_ABI_PASS_UNSUPPORTED)) == BUSTER_ABI_PASS_CLASSIFIED && abi->stack_bytes[index] <= (1u << 16) &&
           kind != BUSTER_TYPE_KIND_VOID && !type_kind_is_array(kind) && kind != BUSTER_TYPE_KIND_FUNCTION && !half;
}

// Whether this compiler's C passes a type as an argument and a result (a compiled world's cross-check): an integer of up
// to 64 bits, an enum, a pointer, float and double, aligned to 8 at most, and a long double, a 128-bit integer, a complex
// float, double or long double, a struct or a union aligned to 16 at most.
static bool abi_test_compiled_passes(const TypeTable* table, TypeId type)
{
    const uint32_t resolved = abi_resolved_index(table, type);
    const uint8_t kind = table->types.kind[resolved];
    const uint8_t code = table->types.code[resolved];
    const bool scalar = (kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[resolved] <= 8) || kind == BUSTER_TYPE_KIND_ENUM || kind == BUSTER_TYPE_KIND_POINTER ||
                        (kind == BUSTER_TYPE_KIND_FLOATING && (code == BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_DOUBLE));
    const bool complex = kind == BUSTER_TYPE_KIND_COMPLEX && (code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_FLOAT || code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_DOUBLE ||
                                                              code == BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_LONG_DOUBLE);
    const bool wide = (kind == BUSTER_TYPE_KIND_FLOATING && code == BUSTER_TYPE_CODE_LONG_DOUBLE) || (kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[resolved] == 16) ||
                      complex || kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION;
    return (scalar && type_alignment_log2(table, type) <= 3) || (wide && type_alignment_log2(table, type) <= 4);
}

// Random calls: their results (void now and then), counts (mostly small, a few long ones past the registers), named
// counts (a fifth variadic) and arguments, over `types`.
typedef struct AbiTestCalls AbiTestCalls;
struct AbiTestCalls
{
    TypeId* results;
    uint32_t* counts;
    uint32_t* named;
    uint8_t* variadic;
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
        calls->variadic[c] = calls->named[c] != arguments || type_test_below(random, 8) == 0 ? 1 : 0;
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
        .variadic = calls->variadic,
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

// Calls longer than the vector path's 16-bit register sums count (BUSTER_ABI_COUNTED_ARGUMENTS), which it leaves to
// the twin, and as long as they hold: 65,542 longs, whose general registers' sum would carry into the vector ones' and
// give the last six rdi to r9 again; 32,767 __int128s after a hidden pointer (the most a sum holds: 65,535 registers)
// and 32,768 without one; the same counts of _Complex double, for the vector registers' sum; then a short call. Both
// paths must agree, and the longs' call must have its stack: 65,536 slots, no vector register.
static void abi_test_long_calls(AbiTestTally* tally, const AbiTable* abi, const TypeId* passable, uint32_t passable_count)
{
    TypeId hidden = BUSTER_TYPE_VOID;
    for (uint32_t i = 0; i < passable_count && hidden == BUSTER_TYPE_VOID; i += 2)
    {
        const AbiReturn result = abi->results[passable[i] >> BUSTER_TYPE_INDEX_SHIFT];
        hidden = result.kind == BUSTER_ABI_RESULT_MEMORY && (result.flags & BUSTER_ABI_FLAG_DEMOTED) == 0 ? passable[i] : hidden;
    }
    const uint32_t counts[6] = {
        65542,
        BUSTER_ABI_COUNTED_ARGUMENTS,
        BUSTER_ABI_COUNTED_ARGUMENTS + 1,
        BUSTER_ABI_COUNTED_ARGUMENTS,
        BUSTER_ABI_COUNTED_ARGUMENTS + 1,
        3,
    };
    const TypeId types[6] = {
        BUSTER_TYPE_LONG,
        BUSTER_TYPE_INT128,
        BUSTER_TYPE_INT128,
        BUSTER_TYPE_COMPLEX_DOUBLE,
        BUSTER_TYPE_COMPLEX_DOUBLE,
        BUSTER_TYPE_LONG,
    };
    const TypeId results[6] = {
        BUSTER_TYPE_VOID,
        hidden,
        BUSTER_TYPE_VOID,
        hidden,
        BUSTER_TYPE_VOID,
        BUSTER_TYPE_VOID,
    };
    const uint8_t variadic[6] = { 0 };
    uint64_t total = 0;
    for (uint32_t c = 0; c < 6; c += 1)
    {
        total += counts[c];
    }
    TypeId* arguments = malloc(sizeof(TypeId) * total);
    AbiLocation* vector_locations = malloc(sizeof(AbiLocation) * total);
    AbiLocation* scalar_locations = malloc(sizeof(AbiLocation) * total);
    if (abi_test_expect(tally, arguments != 0 && vector_locations != 0 && scalar_locations != 0, "assignment", "no memory for the long calls"))
    {
        uint64_t first = 0;
        for (uint32_t c = 0; c < 6; c += 1)
        {
            for (uint32_t a = 0; a < counts[c]; a += 1)
            {
                arguments[first + a] = types[c];
            }
            first += counts[c];
        }
        const AbiSignatures signatures = {
            .results = results,
            .counts = counts,
            .named = counts,
            .variadic = variadic,
            .count = 6,
            .arguments = arguments,
        };
        AbiCall vector_calls[6];
        AbiCall scalar_calls[6];
        abi_test_compare_assignment(tally, abi, signatures, total, vector_locations, scalar_locations, vector_calls, scalar_calls, "long calls");
        abi_test_expect(tally, vector_calls[0].stack_bytes == (65542 - 6) * 8 && vector_calls[0].vector_registers == 0 && vector_calls[0].integer_registers == 6 &&
                                   vector_locations[65536].kind == BUSTER_ABI_LOCATION_STACK && vector_locations[65536].stack_offset == (65536 - 6) * 8,
                        "assignment", "the long call of longs");
    }
    free(arguments);
    free(vector_locations);
    free(scalar_locations);
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
        .variadic = malloc(count),
        .arguments = malloc(sizeof(TypeId) * capacity),
    };
    AbiLocation* vector_locations = malloc(sizeof(AbiLocation) * capacity);
    AbiLocation* scalar_locations = malloc(sizeof(AbiLocation) * capacity);
    AbiCall* vector_calls = malloc(sizeof(AbiCall) * count);
    AbiCall* scalar_calls = malloc(sizeof(AbiCall) * count);
    TypeId* passable = 0;
    uint64_t arguments = 0;
    const bool memory = calls.results != 0 && calls.counts != 0 && calls.named != 0 && calls.variadic != 0 && calls.arguments != 0 && vector_locations != 0 && scalar_locations != 0 &&
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
                if (round == 0)
                {
                    abi_test_long_calls(&tally, &abi, passable, passable_count);
                }
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
                        const uint8_t variadic = 0;
                        const AbiSignatures one = {
                            .results = &result,
                            .counts = &n,
                            .named = &named,
                            .variadic = &variadic,
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
    free(calls.variadic);
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
// The cross-check (--cross-check, contract 4 on #135): random signatures over a world of types, each one's callee and
// caller written as C and compiled by clang or GCC, and generated as encoder records from the module's locations
// (abi_assign's): our caller calls the C callee, and the C caller calls our callee. Both sides take their values from
// the same bytes, which the C file defines (each argument's in a 64-byte slot of its signature's array), and check
// what arrives: the C callee its parameters and va_arg's, the C caller what our callee stored of its arguments and
// what it returned, and main what our caller stored of the C callee's result (and the hidden pointer it got back).
// Each value's bytes are a valid representation of its type (bool 0 or 1, _BitInts extended, long doubles normal),
// and a mask says which of its bits mean something (not padding, nor a _BitInt's bits past its width). The records
// pass through x86_encode, link_add_code_fixups and the object writer, and the C compiler's driver links the two
// objects. It runs at each target vector width (-march=x86-64, x86-64-v3 and x86-64-v4), with clang (every type) and with
// GCC (the types GCC passes as clang does), the compiled worlds' runs and the extension runs (abi_test_cross_extension)
// beside them, at once, a child process each (abi_test_cross_check).

#define BUSTER_ABI_TEST_SLOT 64
#define BUSTER_ABI_TEST_FILL_ITEMS 4096
#define BUSTER_ABI_TEST_PATH_BYTES 4096
#define BUSTER_ABI_TEST_MAX_ARGUMENTS 32
#define BUSTER_ABI_TEST_MAX_INSTRUCTIONS 1024
// The runs: clang's, then GCC's, each at 16-, 32- and 64-byte vectors.
#define BUSTER_ABI_TEST_CROSS_WIDTHS 3
// clang's and GCC's runs at each width, then a compiled world's with each, at 16 bytes, then the extension runs.
#define BUSTER_ABI_TEST_CROSS_RUNS (2 * BUSTER_ABI_TEST_CROSS_WIDTHS + 4)
// Tier 0's integer and floating depths (BUSTER_TIER0_INTEGER_DEPTHS and BUSTER_TIER0_FLOATING_DEPTHS; tier0_test.c,
// which main.c includes after tier0.c and this file, checks them), which a compiled world's calls keep within.
#define BUSTER_ABI_TEST_TIER0_INTEGER_DEPTHS 15
#define BUSTER_ABI_TEST_TIER0_FLOATING_DEPTHS 16

typedef struct AbiTestFillItem AbiTestFillItem;
struct AbiTestFillItem
{
    TypeId type;
    uint64_t offset;
};

static void abi_test_random_bits(uint8_t* bytes, uint8_t* mask, uint64_t first, uint64_t count, uint64_t* random)
{
    for (uint64_t bit = first; bit < first + count; bit += 1)
    {
        const uint8_t value = (uint8_t)(type_test_random(random) & 1);
        bytes[bit / 8] = (uint8_t)((bytes[bit / 8] & ~(1u << (bit % 8))) | value << (bit % 8));
        mask[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
}

// A value of `type` at `bytes`: random contents of a valid representation, padding random too, and in `mask` its
// meaningful bits. A union holds one named member's value, picked at random.
// Keeps a floating value, or a vector's floating elements, finite: the top bit of each one's exponent, the second
// highest of its last byte, clear (half, bfloat, float, double, __float128), so that no copy of it through a register
// quiets a NaN's payload.
static void abi_test_finite(const TypeTable* table, uint32_t index, uint8_t* bytes, uint64_t size)
{
    const uint8_t kind = table->types.kind[index];
    const bool vector = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    const uint32_t element = vector ? table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT : index;
    const uint64_t element_size = table->types.size[element];
    if (table->types.kind[element] == BUSTER_TYPE_KIND_FLOATING && table->types.code[element] != BUSTER_TYPE_CODE_LONG_DOUBLE && element_size != 0)
    {
        for (uint64_t offset = element_size - 1; offset < size; offset += element_size)
        {
            bytes[offset] &= 0xbf;
        }
    }
}

// The bits clang's LLVM type gives each element of a vector whose elements it packs in memory (a bool's 1, a _BitInt
// narrower than 8 bits its width, a long double's x86_fp80 80, 10 bytes apart where C has 16), or 0 for none.
static uint64_t abi_test_packed_bits(const TypeTable* table, uint32_t vector)
{
    const uint32_t element = abi_resolved_index(table, table->types.base[vector]);
    const uint8_t kind = table->types.kind[element];
    const uint8_t code = table->types.code[element];
    uint64_t result = 0;
    if (kind == BUSTER_TYPE_KIND_INTEGER && code == BUSTER_TYPE_CODE_BOOL)
    {
        result = 1;
    }
    else if (kind == BUSTER_TYPE_KIND_BIT_INT && table->types.extent[element] < 8)
    {
        result = table->types.extent[element];
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING && code == BUSTER_TYPE_CODE_LONG_DOUBLE)
    {
        result = 80;
    }
    return result;
}

// The bytes clang's objects of a type take, its LLVM type's: fewer than sizeof says for a vector of packed elements (an
// integer of a vector of bools' bits, at least 8, aligned as LLVM aligns integers, to 16 past 64 bits; the elements of
// the others packed, their bytes rounded up to a power of two), the C size otherwise.
static uint64_t abi_test_llvm_bytes(const TypeTable* table, TypeId type)
{
    const uint32_t index = abi_resolved_index(table, type);
    const uint8_t kind = table->types.kind[index];
    const uint64_t count = table->types.extent[index];
    const uint64_t bits = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ? abi_test_packed_bits(table, index) : 0;
    uint64_t result = table->types.size[index];
    if (bits == 1)
    {
        const uint64_t width = count < 8 ? 8 : count;
        const uint64_t alignment = width <= 8 ? 1 : width <= 16 ? 2 : width <= 32 ? 4 : width <= 64 ? 8 : 16;
        result = ((width + 7) / 8 + alignment - 1) / alignment * alignment;
    }
    else if (bits != 0)
    {
        const uint64_t bytes = (count * bits + 7) / 8;
        result = 1;
        while (result < bytes)
        {
            result *= 2;
        }
    }
    return result;
}

// Per type, what it holds, in itself or in its members, elements, base or value, that clang 23 passes or copies
// otherwise than the module says (bits):
//   - HOLDS_QUAD: a __float128. A record SSE, SSE whose LLVM type starts with one, 16 bytes, gets it from
//     GetSSETypeAtOffset as its low part, which GetX86_64ByValArgumentPair widens to an i64 (docs/calling-convention.md,
//     "Next steps"). A vector of them is no fp128 to getFPTypeAtOffset, and holds none.
//   - HOLDS_PACKS: a vector whose LLVM type packs it into fewer bits than its bytes (of bools other than a multiple of 8
//     that fills its bytes, of narrow _BitInts or of long doubles: abi_test_packed_bits).
//   - HOLDS_UNION_PACKS: a union that holds such a vector. As the union's LLVM type, which takes its most aligned member
//     (lowerUnion), the vector makes clang's copies of the union lose the other members' bits past its own.
// Worked out in passes over the table until one changes nothing; 0 without the memory.
#define BUSTER_ABI_TEST_HOLDS_QUAD 1u
#define BUSTER_ABI_TEST_HOLDS_PACKS 2u
#define BUSTER_ABI_TEST_HOLDS_UNION_PACKS 4u

static uint8_t* abi_test_holds(const TypeTable* table)
{
    uint8_t* holds = calloc(table->types.count, 1);
    bool changed = holds != 0;
    while (changed)
    {
        changed = false;
        for (uint32_t index = 1; index < table->types.count; index += 1)
        {
            const uint8_t kind = table->types.kind[index];
            uint8_t value = holds[index];
            if (kind == BUSTER_TYPE_KIND_FLOATING && table->types.code[index] == BUSTER_TYPE_CODE_FLOAT128)
            {
                value |= BUSTER_ABI_TEST_HOLDS_QUAD;
            }
            else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
            {
                const uint64_t bits = abi_test_packed_bits(table, index);
                const uint64_t count = table->types.extent[index];
                const uint64_t llvm_bits = bits == 1 ? (count < 8 ? 8 : count) : count * bits;
                value |= bits != 0 && llvm_bits < table->types.size[index] * 8 ? BUSTER_ABI_TEST_HOLDS_PACKS : 0;
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                value |= holds[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT];
            }
            else if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_is_complete(table, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT))
            {
                const uint32_t tag = table->types.index[index];
                const uint32_t begin = table->tags.member_begin[tag];
                const uint32_t end = begin + table->tags.member_count[tag];
                for (uint32_t member = begin; member < end; member += 1)
                {
                    const uint8_t member_holds = holds[table->members.type[member] >> BUSTER_TYPE_INDEX_SHIFT];
                    value |= member_holds;
                    value |= kind == BUSTER_TYPE_KIND_UNION && (member_holds & BUSTER_ABI_TEST_HOLDS_PACKS) != 0 ? BUSTER_ABI_TEST_HOLDS_UNION_PACKS : 0;
                }
            }
            changed = changed || value != holds[index];
            holds[index] = value;
        }
    }
    return holds;
}

// A normal x87 value at `bytes`: the explicit integer bit set, the exponent neither 0 nor all ones, so that loads and
// stores through x87's registers keep it.
static void abi_test_x87(uint8_t* bytes, uint8_t* mask, uint64_t* random)
{
    const uint64_t mantissa = type_test_random(random) | 1ull << 63;
    const uint16_t exponent = (uint16_t)(1 + type_test_below(random, 0x7ffe)) | (uint16_t)((type_test_random(random) & 1) << 15);
    memcpy(bytes, &mantissa, 8);
    memcpy(bytes + 8, &exponent, 2);
    memset(mask, 0xff, 10);
}

static void abi_test_fill(const TypeTable* table, TypeId type, uint8_t* bytes, uint8_t* mask, uint64_t* random, AbiTestFillItem* stack)
{
    const uint64_t size = table->types.size[abi_resolved_index(table, type)];
    for (uint64_t i = 0; i < size; i += 1)
    {
        bytes[i] = (uint8_t)type_test_random(random);
        mask[i] = 0;
    }
    stack[0] = (AbiTestFillItem){
        .type = type,
        .offset = 0,
    };
    uint32_t depth = 1;
    while (depth != 0)
    {
        depth -= 1;
        const AbiTestFillItem item = stack[depth];
        uint32_t index = abi_resolved_index(table, item.type);
        if (table->types.kind[index] == BUSTER_TYPE_KIND_ENUM)
        {
            index = table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT;
        }
        const uint8_t kind = table->types.kind[index];
        const uint8_t code = table->types.code[index];
        const uint64_t item_size = table->types.size[index];
        uint8_t* b = bytes + item.offset;
        uint8_t* m = mask + item.offset;
        if (kind == BUSTER_TYPE_KIND_INTEGER && code == BUSTER_TYPE_CODE_BOOL)
        {
            b[0] = (uint8_t)(type_test_random(random) & 1);
            m[0] = 0xff;
        }
        else if (kind == BUSTER_TYPE_KIND_FLOATING && code == BUSTER_TYPE_CODE_LONG_DOUBLE)
        {
            abi_test_x87(b, m, random);
        }
        else if ((kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && abi_test_packed_bits(table, index) == 80)
        {
            for (uint64_t i = 0; i < table->types.extent[index]; i += 1)
            {
                abi_test_x87(b + 10 * i, m + 10 * i, random);
            }
        }
        else if ((kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR) && abi_test_packed_bits(table, index) != 0)
        {
            abi_test_random_bits(b, m, 0, table->types.extent[index] * abi_test_packed_bits(table, index), random);
        }
        else if (kind == BUSTER_TYPE_KIND_NULLPTR)
        {
            memset(b, 0, 8);
        }
        else if (kind == BUSTER_TYPE_KIND_BIT_INT)
        {
            const uint64_t width = table->types.extent[index];
            const bool is_unsigned = (table->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) != 0;
            abi_test_random_bits(b, m, 0, width, random);
            const uint8_t top = (uint8_t)(b[(width - 1) / 8] >> ((width - 1) % 8) & 1);
            for (uint64_t bit = width; bit < item_size * 8; bit += 1)
            {
                const uint8_t value = is_unsigned ? 0 : top;
                b[bit / 8] = (uint8_t)((b[bit / 8] & ~(1u << (bit % 8))) | value << (bit % 8));
            }
        }
        else if (kind == BUSTER_TYPE_KIND_EXT_VECTOR)
        {
            const uint64_t element_size = table->types.size[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT];
            memset(m, 0xff, table->types.extent[index] * element_size);
            abi_test_finite(table, index, b, table->types.extent[index] * element_size);
        }
        else if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_VECTOR)
        {
            memset(m, 0xff, item_size);
            abi_test_finite(table, index, b, item_size);
        }
        else if (kind == BUSTER_TYPE_KIND_COMPLEX && depth + 2 <= BUSTER_ABI_TEST_FILL_ITEMS)
        {
            const TypeId element = table->types.base[index];
            const uint64_t element_size = table->types.size[element >> BUSTER_TYPE_INDEX_SHIFT];
            stack[depth] = (AbiTestFillItem){
                .type = element,
                .offset = item.offset,
            };
            stack[depth + 1] = (AbiTestFillItem){
                .type = element,
                .offset = item.offset + element_size,
            };
            depth += 2;
        }
        else if (kind == BUSTER_TYPE_KIND_ATOMIC && depth < BUSTER_ABI_TEST_FILL_ITEMS)
        {
            stack[depth] = (AbiTestFillItem){
                .type = table->types.base[index],
                .offset = item.offset,
            };
            depth += 1;
        }
        else if (kind == BUSTER_TYPE_KIND_ARRAY)
        {
            const TypeId element = table->types.base[index];
            const uint64_t element_size = table->types.size[abi_resolved_index(table, element)];
            for (uint64_t i = 0; i < table->types.extent[index] && depth < BUSTER_ABI_TEST_FILL_ITEMS; i += 1)
            {
                stack[depth] = (AbiTestFillItem){
                    .type = element,
                    .offset = item.offset + i * element_size,
                };
                depth += 1;
            }
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = table->types.index[index];
            const uint32_t begin = table->tags.member_begin[tag];
            const uint32_t end = begin + table->tags.member_count[tag];
            uint32_t chosen = UINT32_MAX;
            if (kind == BUSTER_TYPE_KIND_UNION)
            {
                uint32_t named = 0;
                for (uint32_t member = begin; member < end; member += 1)
                {
                    named += table->members.name[member] != 0 ? 1 : 0;
                }
                uint32_t pick = (uint32_t)type_test_below(random, named);
                for (uint32_t member = begin; member < end && chosen == UINT32_MAX; member += 1)
                {
                    if (table->members.name[member] != 0 && pick == 0)
                    {
                        chosen = member;
                    }
                    pick -= table->members.name[member] != 0 ? 1 : 0;
                }
            }
            for (uint32_t member = begin; member < end; member += 1)
            {
                const TypeId member_type = table->members.type[member];
                const uint8_t member_kind = table->types.kind[abi_resolved_index(table, member_type)];
                const bool taken = kind == BUSTER_TYPE_KIND_STRUCT ? table->members.name[member] != 0 : member == chosen;
                if (!taken || member_kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
                {
                }
                else if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0)
                {
                    abi_test_random_bits(bytes, mask, item.offset * 8 + table->members.offset[member], table->members.bit_width[member], random);
                }
                else if (depth < BUSTER_ABI_TEST_FILL_ITEMS)
                {
                    stack[depth] = (AbiTestFillItem){
                        .type = member_type,
                        .offset = item.offset + table->members.offset[member] / 8,
                    };
                    depth += 1;
                }
            }
        }
    }
}

// A signature of the cross-check: its result, its arguments (from `first` in the arguments' column, the first `named` of
// them named), each argument's offset in the signature's data, and the data's and the result's sizes.
typedef struct AbiTestSignature AbiTestSignature;
struct AbiTestSignature
{
    TypeId result;
    uint32_t first;
    uint32_t count;
    uint32_t named;
    bool variadic;
    uint32_t data_bytes;
    uint32_t result_bytes;
};

typedef struct AbiTestCross AbiTestCross;
struct AbiTestCross
{
    AbiTestWorld* world;
    AbiTable* abi;
    AbiTestSignature* signatures;
    uint32_t signature_count;
    TypeId* arguments;
    uint32_t* offsets;
    uint32_t argument_count;
    AbiLocation* locations;
    AbiCall* calls;
    // The bytes: every signature's data and masks one after another, and its result's and mask.
    uint8_t* data;
    uint8_t* data_mask;
    uint64_t* data_begin;
    uint8_t* results;
    uint8_t* result_mask;
    uint64_t* result_begin;
    bool gcc;
    // Our side as C compiled by this compiler (AbiTestWorld.compiled), which can't see the al and the hidden pointer it
    // gets, as our records can.
    bool compiled;
};

// Whether a type may be an unnamed (variadic) argument here: unchanged by the default argument promotions, and one
// clang passes and reads alike (not nullptr_t, whose call passes it in a register and whose va_arg reads memory, nor a
// WIDE union of no vector).
static bool abi_test_unnamed_ok(const AbiTable* abi, const TypeTable* table, TypeId type, AbiTestFrame* frames)
{
    const uint32_t index = abi_resolved_index(table, type);
    const uint8_t kind = table->types.kind[index];
    const uint8_t code = table->types.code[index];
    const bool promoted = (kind == BUSTER_TYPE_KIND_INTEGER && table->types.size[index] < 4) || kind == BUSTER_TYPE_KIND_ENUM ||
                          (kind == BUSTER_TYPE_KIND_FLOATING && code != BUSTER_TYPE_CODE_DOUBLE && code != BUSTER_TYPE_CODE_LONG_DOUBLE &&
                           code != BUSTER_TYPE_CODE_FLOAT128);
    const bool scalar_memory = kind == BUSTER_TYPE_KIND_NULLPTR || kind == BUSTER_TYPE_KIND_ATOMIC;
    const bool wide_without_vector = abi_test_classify(table, abi->vector_bytes, false, type, 0, frames) == BUSTER_ABI_TEST_VECTOR &&
                                     table->types.size[index] > BUSTER_ABI_REGISTER_BYTES;
    // clang's call passes a value of no size in an 8-byte slot, and its va_arg skips none; and a LOW_GENERAL one's low
    // eightbyte in a general register, which its va_arg reads from the save area's vector part.
    const bool empty = table->types.size[index] == 0;
    const bool low_general = (abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_LOW_GENERAL) != 0;
    // And a WIDE vector of _BitInt(128)s as its value, i64s in general registers, which its va_arg reads from the stack;
    // and a SMALLER one's LLVM bytes, where its va_arg skips the C size.
    const bool vector = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
    const uint32_t element = vector ? abi_resolved_index(table, table->types.base[index]) : 0;
    const bool wide_values = vector && (abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_WIDE) != 0 &&
                             table->types.kind[element] == BUSTER_TYPE_KIND_BIT_INT && table->types.size[element] == BUSTER_ABI_REGISTER_BYTES;
    const bool smaller = (abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_SMALLER) != 0;
    return !promoted && !scalar_memory && !wide_without_vector && !empty && !low_general && !wide_values && !smaller;
}

// Whether GCC passes a type otherwise than clang as a result: a vector that is MEMORY by its size, which clang returns
// in registers (xmm0 and on for one wider than the target's, xmm0 for <1 x double>) and GCC through a hidden pointer,
// and a DEMOTED one, whose hidden pointer GCC counts and clang doesn't (it splits the arguments after it otherwise).
static bool abi_test_gcc_result_differs(const AbiTable* abi, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const AbiReturn result = abi->results[index];
    const bool demoted = (result.flags & BUSTER_ABI_FLAG_DEMOTED) != 0;
    return (abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES] == BUSTER_ABI_IN_MEMORY && result.kind == BUSTER_ABI_RESULT_REGISTERS) || demoted;
}

// Whether GCC passes a named argument of a variadic call otherwise than clang: a WIDE one, which LLVM puts on the stack
// and GCC, as the psABI says, in a YMM or ZMM register.
static bool abi_test_gcc_variadic_differs(const AbiTable* abi, TypeId type)
{
    return (abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_WIDE) != 0;
}

// Whether a type is a vector wrapped in records of one member its size (struct { V v; }, and so on), which GCC passes
// as clang does when it is WIDE: clang classifies an array wider than 16 bytes in a union as nothing (its size check
// returns before it is classified), so `union { __m128 v; char c[24]; }` is SSE, SSEUP to clang and MEMORY to GCC.
static bool abi_test_single_vector(const TypeTable* table, TypeId type)
{
    uint32_t index = abi_resolved_index(table, type);
    bool walking = true;
    while (walking)
    {
        const uint8_t kind = table->types.kind[index];
        walking = false;
        if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = table->types.index[index];
            const uint32_t member = table->tags.member_begin[tag];
            const uint32_t inner = table->tags.member_count[tag] == 1 ? abi_resolved_index(table, table->members.type[member]) : index;
            walking = inner != index && table->types.size[inner] == table->types.size[index] && table->members.bit_width[member] == BUSTER_TYPE_NOT_BIT_FIELD;
            index = walking ? inner : index;
        }
    }
    return table->types.kind[index] == BUSTER_TYPE_KIND_VECTOR;
}

// Whether GCC passes an unnamed argument otherwise than clang: a WIDE aggregate, as a union of vectors as wide as a
// vector register, to which GCC gives an integer mode: its caller passes it in a register and its va_arg reads 32 or 64
// bytes from the register save area, past the 16 a register has there (GCC 16 crashes calling itself so). And an
// aligned typedef that raises its type's alignment to 16 or more: both place it by its type's, but GCC 16's va_arg
// copies it from the overflow area with an aligned load (movdqa), which faults at an offset 16 doesn't divide.
static bool abi_test_gcc_unnamed_differs(const AbiTable* abi, const TypeTable* table, TypeId type)
{
    const uint32_t pass = abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT];
    const uint32_t alignment_log2 = type_alignment_log2(table, type);
    const bool raised = alignment_log2 >= 4 && alignment_log2 > type_alignment_log2(table, type_strip_aligned(table, type));
    return ((pass & BUSTER_ABI_PASS_WIDE) != 0 && (pass & BUSTER_ABI_PASS_AGGREGATE) != 0) || raised;
}

// Per type, whether it holds, in itself or in its members, elements, base or value, an array of elements of no size:
// clang 23's getFPTypeAtOffset and GetINTEGERTypeAtOffset divide an offset by such an array's element's LLVM size, 0,
// where they look into it for a type passed in registers, and crash (SIGFPE) now and then, as GetSSETypeAtOffset does
// after the float in `struct { long l; _Float16 h; struct {} e[1]; }`; so clang's signatures don't pass one in
// registers. Worked out in passes over the table until one changes nothing; 0 without the memory.
static uint8_t* abi_test_empty_arrays(const TypeTable* table)
{
    uint8_t* holds = calloc(table->types.count, 1);
    bool changed = holds != 0;
    while (changed)
    {
        changed = false;
        for (uint32_t index = 1; index < table->types.count; index += 1)
        {
            const uint8_t kind = table->types.kind[index];
            uint8_t value = holds[index];
            if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                const bool empty = table->types.size[abi_resolved_index(table, table->types.base[index])] == 0;
                value |= holds[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT] | (type_kind_is_array(kind) && empty ? 1 : 0);
            }
            else if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_is_complete(table, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT))
            {
                const uint32_t tag = table->types.index[index];
                const uint32_t begin = table->tags.member_begin[tag];
                const uint32_t end = begin + table->tags.member_count[tag];
                for (uint32_t member = begin; member < end; member += 1)
                {
                    value |= holds[table->members.type[member] >> BUSTER_TYPE_INDEX_SHIFT];
                }
            }
            changed = changed || value != holds[index];
            holds[index] = value;
        }
    }
    return holds;
}

// Per type, which of its first 16 bytes hold bit-fields, in itself or in its members, elements, base or value: a bit per
// byte. clang makes access units of a run of bit-fields (CGRecordLowering::accumulateBitFields), which the module doesn't
// work out yet (it takes the run as its first bit-field's type: see "Next steps" in docs/calling-convention.md), and a
// unit in an INTEGER high eightbyte decides, by its alignment, whether a narrow SSE low eightbyte widens to an i64 in a
// general register (LOW_GENERAL): struct { _Complex _Float16 h; _Complex long z[0]; _BitInt(8) a : 3; unsigned char
// b : 8; } is (i64, i16) to clang, and the module passes its low part in xmm0. So clang's signatures don't pass a type
// with an SSE low eightbyte and bit-fields in an INTEGER high one. Worked out in passes over the table until one changes
// nothing; 0 without the memory.
static uint16_t* abi_test_bit_field_bytes(const TypeTable* table)
{
    uint16_t* bytes = calloc(table->types.count, sizeof(uint16_t));
    bool changed = bytes != 0;
    while (changed)
    {
        changed = false;
        for (uint32_t index = 1; index < table->types.count; index += 1)
        {
            const uint8_t kind = table->types.kind[index];
            uint16_t value = bytes[index];
            if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                value |= bytes[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT];
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY)
            {
                const uint32_t element = bytes[table->types.base[index] >> BUSTER_TYPE_INDEX_SHIFT];
                const uint64_t element_size = table->types.size[abi_resolved_index(table, table->types.base[index])];
                for (uint64_t i = 0; element != 0 && element_size != 0 && i < table->types.extent[index] && i * element_size < 16; i += 1)
                {
                    value |= (uint16_t)(element << (i * element_size));
                }
            }
            else if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && type_is_complete(table, (TypeId)index << BUSTER_TYPE_INDEX_SHIFT))
            {
                const uint32_t tag = table->types.index[index];
                const uint32_t begin = table->tags.member_begin[tag];
                const uint32_t end = begin + table->tags.member_count[tag];
                for (uint32_t member = begin; member < end; member += 1)
                {
                    const uint64_t offset = table->members.offset[member];
                    const uint64_t width = table->members.bit_width[member];
                    if ((table->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0)
                    {
                        const uint32_t member_bytes = bytes[table->members.type[member] >> BUSTER_TYPE_INDEX_SHIFT];
                        value |= (offset >> 3) < 16 ? (uint16_t)(member_bytes << (offset >> 3)) : 0;
                    }
                    for (uint64_t byte = offset >> 3; width != 0 && byte <= (offset + width - 1) >> 3 && byte < 16; byte += 1)
                    {
                        value |= (uint16_t)(1u << byte);
                    }
                }
            }
            changed = changed || value != bytes[index];
            bytes[index] = value;
        }
    }
    return bytes;
}

static uint64_t abi_test_round(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

// What clang passes of a value in registers: no byte of an eightbyte that is NO_CLASS, padding or the part of a scalar
// past the eightbyte it starts in, which clang classifies by its start alone (an 8-byte enum at offset 4 of a record
// passes its low half); and of an SSE eightbyte, only its type's bytes (GetSSETypeAtOffset over its LLVM type, as
// abi_sse_part reads the module's layout), but for a low one GetX86_64ByValArgumentPair widens to 8 bytes, as it does
// one before a high part that doesn't start at 8 by its alignment. `union { struct { char
// __attribute__((vector_size(16))) z[0]; _Complex float c; float f; } s; long __attribute__((vector_size(16))) w; }` is
// stored as its struct, which the array aligns to 16 in LLVM, and passes as (<2 x float>, float): none of the vector's
// bytes 12 to 15 (GCC passes the eightbyte whole: `clang` says whose). Clears those bytes' mask. Our side passes and
// stores every byte of its registers' eightbytes, so the mask never hides a register the module gives wrongly.
static void abi_test_mask_unpassed(const AbiTable* abi, const TypeTable* table, TypeId type, uint8_t* mask, bool clang)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint8_t classes = abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES];
    const uint8_t low = classes & 7;
    const uint8_t high = classes >> 4;
    const uint64_t size = table->types.size[abi_resolved_index(table, type)];
    const uint64_t low_end = size < 8 ? size : 8;
    const uint64_t high_end = size < 16 ? size : 16;
    if (low == BUSTER_ABI_NO_CLASS)
    {
        memset(mask, 0, low_end);
    }
    if (high == BUSTER_ABI_NO_CLASS && size > 8)
    {
        memset(mask + 8, 0, high_end - 8);
    }
    if (clang && (low == BUSTER_ABI_SSE || high == BUSTER_ABI_SSE) && size <= 16)
    {
        const uint64_t layout = abi->layout[index];
        bool narrow = false;
        const uint64_t low_bytes = low == BUSTER_ABI_SSE ? 1ull << abi_sse_part(layout, 0, size, &narrow) : 8;
        const uint32_t high_alignment_log2 = high == BUSTER_ABI_SSE ? abi_sse_part(layout, 8, size, &narrow) : high == BUSTER_ABI_INTEGER ? abi_integer_part(layout, size) : 3;
        const uint64_t high_alignment = 1ull << high_alignment_log2;
        const bool paired = size > 8 && (high == BUSTER_ABI_SSE || high == BUSTER_ABI_INTEGER) && low != BUSTER_ABI_NO_CLASS;
        const bool widened = paired && (low_bytes + high_alignment - 1) / high_alignment * high_alignment != 8;
        const uint64_t low_passed = widened ? 8 : low_bytes;
        if (low == BUSTER_ABI_SSE && low_passed < low_end)
        {
            memset(mask + low_passed, 0, low_end - low_passed);
        }
        if (high == BUSTER_ABI_SSE && 8 + high_alignment < high_end)
        {
            memset(mask + 8 + high_alignment, 0, high_end - 8 - high_alignment);
        }
    }
}

// The first unnamed argument of call `n` that clang's va_arg reads elsewhere than clang's call puts it, or UINT32_MAX.
// va_start takes the registers LLVM gave the named arguments and the hidden pointer, and the stack past them (the named
// arguments' own call, `named`); va_arg then reads each unnamed one from the save area in its eightbytes' order if all
// the registers its classes need remain, else from the stack (abi_va_arg). The two differ after an argument whose
// registers clang counts otherwise than LLVM assigns (SPLIT, UNCOUNTED, LOW_GENERAL, a PAIR on the stack, a DEMOTED
// result): clang disagrees with itself there, but for a scalar, which both place by LLVM's registers.
static uint32_t abi_test_va_disagreement(const AbiTestCross* cross, uint32_t n, AbiCall named)
{
    const AbiTestSignature* signature = &cross->signatures[n];
    const TypeTable* table = &cross->world->types.test.table;
    uint32_t general = named.integer_registers;
    uint32_t vector = named.vector_registers;
    uint64_t overflow = named.stack_bytes;
    uint32_t result = UINT32_MAX;
    for (uint32_t a = signature->named; a < signature->count && result == UINT32_MAX; a += 1)
    {
        const uint32_t i = signature->first + a;
        const AbiVaArg plan = abi_va_arg(cross->abi, table, cross->arguments[i]);
        const AbiLocation location = cross->locations[i];
        AbiLocation expected = {
            .stack_offset = 0,
            .registers = {
                BUSTER_ABI_REGISTER_NONE,
                BUSTER_ABI_REGISTER_NONE,
            },
            .kind = BUSTER_ABI_LOCATION_IGNORE,
            .flags = 0,
        };
        const bool fits = plan.kind == BUSTER_ABI_LOCATION_REGISTERS && general + plan.integer_registers <= BUSTER_ABI_INTEGER_ARGUMENTS &&
                          vector + plan.vector_registers <= BUSTER_ABI_VECTOR_ARGUMENTS;
        if (fits)
        {
            uint32_t part = 0;
            for (uint32_t e = 0; e < 2; e += 1)
            {
                const uint8_t next = plan.sources[e] == BUSTER_ABI_VA_GENERAL ? abi_integer_arguments[general] : (uint8_t)(BUSTER_ABI_REGISTER_VECTOR | vector);
                expected.registers[part] = plan.sources[e] != BUSTER_ABI_VA_NONE ? next : expected.registers[part];
                general += plan.sources[e] == BUSTER_ABI_VA_GENERAL ? 1 : 0;
                vector += plan.sources[e] == BUSTER_ABI_VA_VECTOR ? 1 : 0;
                part += plan.sources[e] != BUSTER_ABI_VA_NONE ? 1 : 0;
            }
            expected.kind = BUSTER_ABI_LOCATION_REGISTERS;
        }
        else if (plan.kind != BUSTER_ABI_LOCATION_IGNORE)
        {
            const uint64_t alignment = 1ull << plan.overflow_alignment_log2;
            overflow = (overflow + alignment - 1) & ~(alignment - 1);
            expected.kind = BUSTER_ABI_LOCATION_STACK;
            expected.stack_offset = (uint32_t)overflow;
            overflow += plan.stack_bytes;
        }
        const bool same = location.kind == expected.kind && location.registers[0] == expected.registers[0] && location.registers[1] == expected.registers[1] &&
                          (expected.kind != BUSTER_ABI_LOCATION_STACK || location.stack_offset == expected.stack_offset);
        result = same ? result : a;
    }
    return result;
}

// Signatures over `types`, their bytes, and their locations (abi_assign's): first one per `directed` record,
// (nullptr_t, long x4, the record, long), where clang's count of general registers runs ahead of LLVM's (the uncounted
// nullptr_t takes rdi) and the record finds two counted and one left (a split, or one i128 on the stack and the long
// in r9); then random ones.
static bool abi_test_cross_signatures(AbiTestCross* cross, const TypeId* types, uint32_t type_count, const TypeId* unnamed_types, uint32_t unnamed_count,
                                      const TypeId* directed, uint32_t directed_count, uint64_t* random, AbiTestFillItem* stack)
{
    const TypeTable* table = &cross->world->types.test.table;
    uint32_t argument = 0;
    uint64_t data = 0;
    uint64_t results = 0;
    for (uint32_t n = 0; n < cross->signature_count; n += 1)
    {
        AbiTestSignature* signature = &cross->signatures[n];
        const bool directed_one = n < directed_count;
        const uint64_t shape = type_test_below(random, 16);
        const uint32_t random_named = (uint32_t)(shape < 10 ? type_test_below(random, 6) : shape < 15 ? 4 + type_test_below(random, 10) : 12 + type_test_below(random, 10));
        const uint32_t named = directed_one ? 7 : random_named;
        const uint32_t unnamed = !directed_one && unnamed_count != 0 && type_test_below(random, 4) == 0 ? (uint32_t)type_test_below(random, 7) : 0;
        const bool variadic = !directed_one && (unnamed != 0 || (unnamed_count != 0 && type_test_below(random, 16) == 0));
        signature->result = directed_one || type_test_below(random, 5) == 0 ? BUSTER_TYPE_VOID : types[type_test_below(random, type_count)];
        for (uint32_t attempt = 0; cross->gcc && attempt < 64 && abi_test_gcc_result_differs(cross->abi, signature->result); attempt += 1)
        {
            signature->result = types[type_test_below(random, type_count)];
        }
        signature->result = cross->gcc && abi_test_gcc_result_differs(cross->abi, signature->result) ? BUSTER_TYPE_VOID : signature->result;
        signature->first = argument;
        signature->count = named + unnamed;
        signature->named = named;
        signature->variadic = variadic;
        uint32_t offset = 0;
        // Tier 0 holds a call's arguments at once, on its stacks of 15 integer and 16 floating values (a long double's
        // value on the integer one, as a 128-bit integer's), and copies its aggregates through each stack's first free
        // register (#136); past them it takes values to its frame's deep slots, but not a call's aggregates or a
        // function's long doubles and 128-bit integers. Our caller's `r = c_callee(...)` may hold r's address under them
        // when the result is a struct, a union, a long double or a 128-bit integer. So in a compiled world an argument
        // past 13 integer or 15 floating ones becomes one of the other stack's (21 named and 6 unnamed fit both).
        uint32_t depths[2] = { 0, 0 };
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            TypeId type = a < named ? types[type_test_below(random, type_count)] : unnamed_types[type_test_below(random, unnamed_count)];
            type = directed_one ? (a == 0 ? BUSTER_TYPE_NULLPTR : a == 5 ? directed[n] : BUSTER_TYPE_LONG) : type;
            for (uint32_t attempt = 0; cross->gcc && variadic && a < named && attempt < 64 && abi_test_gcc_variadic_differs(cross->abi, type); attempt += 1)
            {
                type = types[type_test_below(random, type_count)];
            }
            type = cross->gcc && variadic && a < named && abi_test_gcc_variadic_differs(cross->abi, type) ? BUSTER_TYPE_LONG : type;
            const TypeId stripped = type_strip_aligned(table, type);
            const bool floating = type_kind(table, stripped) == BUSTER_TYPE_KIND_FLOATING && type_unqualified(stripped) != BUSTER_TYPE_LONG_DOUBLE;
            const bool full = depths[floating] == (floating ? BUSTER_ABI_TEST_TIER0_FLOATING_DEPTHS - 1 : BUSTER_ABI_TEST_TIER0_INTEGER_DEPTHS - 2);
            type = cross->compiled && full ? (floating ? BUSTER_TYPE_LONG : BUSTER_TYPE_DOUBLE) : type;
            depths[floating != full] += 1;
            cross->arguments[argument + a] = type;
            cross->offsets[argument + a] = offset;
            // A slot of its own even for a type of no size, which the stack passes in 8 bytes.
            const uint64_t size = table->types.size[abi_resolved_index(table, type)];
            offset += (uint32_t)abi_test_round(size != 0 ? size : 1, BUSTER_ABI_TEST_SLOT);
        }
        signature->data_bytes = offset + BUSTER_ABI_TEST_SLOT;
        signature->result_bytes = (uint32_t)abi_test_round(table->types.size[abi_resolved_index(table, signature->result)], BUSTER_ABI_TEST_SLOT) + BUSTER_ABI_TEST_SLOT;
        cross->data_begin[n] = data;
        cross->result_begin[n] = results;
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            const uint64_t at = data + cross->offsets[argument + a];
            abi_test_fill(table, cross->arguments[argument + a], cross->data + at, cross->data_mask + at, random, stack);
        }
        for (uint64_t i = data + offset; i < data + signature->data_bytes; i += 1)
        {
            cross->data[i] = (uint8_t)type_test_random(random);
            cross->data_mask[i] = 0;
        }
        memset(cross->results + results, 0, signature->result_bytes);
        memset(cross->result_mask + results, 0, signature->result_bytes);
        if (signature->result != BUSTER_TYPE_VOID)
        {
            abi_test_fill(table, signature->result, cross->results + results, cross->result_mask + results, random, stack);
        }
        argument += signature->count;
        data += signature->data_bytes;
        results += signature->result_bytes;
    }
    cross->argument_count = argument;
    TypeId* results_types = malloc(sizeof(TypeId) * cross->signature_count);
    uint32_t* counts = malloc(sizeof(uint32_t) * cross->signature_count);
    uint32_t* named = malloc(sizeof(uint32_t) * cross->signature_count);
    uint8_t* variadic = malloc(cross->signature_count);
    const bool result = results_types != 0 && counts != 0 && named != 0 && variadic != 0;
    if (result)
    {
        for (uint32_t n = 0; n < cross->signature_count; n += 1)
        {
            results_types[n] = cross->signatures[n].result;
            counts[n] = cross->signatures[n].count;
            named[n] = cross->signatures[n].named;
            variadic[n] = cross->signatures[n].variadic ? 1 : 0;
        }
        const AbiSignatures signatures = {
            .results = results_types,
            .counts = counts,
            .named = named,
            .variadic = variadic,
            .count = cross->signature_count,
            .arguments = cross->arguments,
        };
        abi_assign(cross->abi, signatures, cross->locations, cross->calls);
        // clang passes a type of no size in memory as an i0 once no general register is free (see EMPTY), which its own
        // passes crash on now and then (LowerArguments, ADCE): the fixtures check that case, and here such an argument
        // becomes a long, which leaves the other arguments where they were.
        bool replaced = false;
        for (uint32_t n = 0; n < cross->signature_count; n += 1)
        {
            const AbiTestSignature* signature = &cross->signatures[n];
            for (uint32_t a = 0; a < signature->count; a += 1)
            {
                const uint32_t i = signature->first + a;
                const uint32_t pass = cross->abi->pass[cross->arguments[i] >> BUSTER_TYPE_INDEX_SHIFT];
                if ((pass & (BUSTER_ABI_PASS_EMPTY | BUSTER_ABI_PASS_IGNORE)) == BUSTER_ABI_PASS_EMPTY && cross->locations[i].kind == BUSTER_ABI_LOCATION_IGNORE)
                {
                    const uint64_t at = cross->data_begin[n] + cross->offsets[i];
                    cross->arguments[i] = BUSTER_TYPE_LONG;
                    abi_test_fill(table, BUSTER_TYPE_LONG, cross->data + at, cross->data_mask + at, random, stack);
                    replaced = true;
                }
            }
        }
        if (replaced)
        {
            abi_assign(cross->abi, signatures, cross->locations, cross->calls);
        }
        // An unnamed argument clang's va_arg reads elsewhere than its call puts it (abi_test_va_disagreement) becomes a
        // long, a round at a time, as one changes where the arguments after it go; the named arguments' own calls give
        // va_start's registers and stack.
        TypeId* named_arguments = malloc(sizeof(TypeId) * (argument + 1));
        AbiLocation* named_locations = malloc(sizeof(AbiLocation) * (argument + 1));
        AbiCall* named_calls = malloc(sizeof(AbiCall) * cross->signature_count);
        bool disagreeing = named_arguments != 0 && named_locations != 0 && named_calls != 0;
        if (disagreeing)
        {
            uint32_t packed = 0;
            for (uint32_t n = 0; n < cross->signature_count; n += 1)
            {
                memcpy(named_arguments + packed, cross->arguments + cross->signatures[n].first, sizeof(TypeId) * cross->signatures[n].named);
                packed += cross->signatures[n].named;
            }
            const AbiSignatures named_signatures = {
                .results = results_types,
                .counts = named,
                .named = named,
                .variadic = variadic,
                .count = cross->signature_count,
                .arguments = named_arguments,
            };
            abi_assign(cross->abi, named_signatures, named_locations, named_calls);
        }
        for (uint32_t round = 0; disagreeing && round < 8; round += 1)
        {
            disagreeing = false;
            for (uint32_t n = 0; n < cross->signature_count; n += 1)
            {
                const AbiTestSignature* signature = &cross->signatures[n];
                const uint32_t a = abi_test_va_disagreement(cross, n, named_calls[n]);
                // A long read elsewhere follows an argument va_arg steps over otherwise than the call (a 16-byte one
                // whose high eightbyte is NO_CLASS takes 8 bytes of the stack, and va_arg skips 16): all become longs.
                const bool all = a != UINT32_MAX && cross->arguments[signature->first + a] == BUSTER_TYPE_LONG;
                for (uint32_t u = signature->named; a != UINT32_MAX && u < signature->count; u += 1)
                {
                    const uint32_t i = signature->first + u;
                    const uint64_t at = cross->data_begin[n] + cross->offsets[i];
                    const bool replace = (u == a || all) && cross->arguments[i] != BUSTER_TYPE_LONG;
                    cross->arguments[i] = replace ? BUSTER_TYPE_LONG : cross->arguments[i];
                    if (replace)
                    {
                        abi_test_fill(table, BUSTER_TYPE_LONG, cross->data + at, cross->data_mask + at, random, stack);
                    }
                }
                disagreeing = disagreeing || a != UINT32_MAX;
            }
            if (disagreeing)
            {
                abi_assign(cross->abi, signatures, cross->locations, cross->calls);
            }
        }
        free(named_arguments);
        free(named_locations);
        free(named_calls);
        for (uint32_t n = 0; n < cross->signature_count; n += 1)
        {
            const AbiTestSignature* signature = &cross->signatures[n];
            for (uint32_t a = 0; a < signature->count; a += 1)
            {
                const uint32_t i = signature->first + a;
                const uint8_t kind = cross->locations[i].kind;
                if (kind == BUSTER_ABI_LOCATION_REGISTERS || kind == BUSTER_ABI_LOCATION_SPLIT)
                {
                    abi_test_mask_unpassed(cross->abi, table, cross->arguments[i], cross->data_mask + cross->data_begin[n] + cross->offsets[i], !cross->gcc);
                }
            }
            if (cross->calls[n].result.kind == BUSTER_ABI_RESULT_REGISTERS)
            {
                abi_test_mask_unpassed(cross->abi, table, signature->result, cross->result_mask + cross->result_begin[n], !cross->gcc);
            }
        }
    }
    free(results_types);
    free(counts);
    free(named);
    free(variadic);
    return result;
}

// Appends bytes as C initializers.
static void abi_test_hex(TypeTestText* text, const uint8_t* bytes, uint64_t count)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[1200];
    uint32_t used = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        buffer[used] = '0';
        buffer[used + 1] = 'x';
        buffer[used + 2] = digits[bytes[i] >> 4];
        buffer[used + 3] = digits[bytes[i] & 15];
        buffer[used + 4] = ',';
        used += 5;
        if (used > sizeof(buffer) - 8 || i + 1 == count)
        {
            type_test_append(text, buffer, used);
            used = 0;
        }
    }
}

// A type's name in the C file: its typedef, or its spelling (nullptr_t's, which the world doesn't spell, typeof's).
static const char* abi_test_c_name(AbiTestWorld* world, TypeId type)
{
    const TypeTable* table = &world->types.test.table;
    const char* result = "typeof(nullptr) ";
    if (table->types.kind[type >> BUSTER_TYPE_INDEX_SHIFT] != BUSTER_TYPE_KIND_NULLPTR)
    {
        result = type_test_name_of(&world->types, type_unqualified(type));
    }
    return result;
}

// The parameter list of signature n: "(T0 a0, T1 a1, ...)", or the types alone, unnamed arguments past the named ones
// left to "...".
static void abi_test_parameters(AbiTestCross* cross, TypeTestText* text, uint32_t n, bool names)
{
    const AbiTestSignature* signature = &cross->signatures[n];
    type_test_format(text, "(");
    for (uint32_t a = 0; a < signature->named; a += 1)
    {
        type_test_format(text, "%s%s", a != 0 ? ", " : "", abi_test_c_name(cross->world, cross->arguments[signature->first + a]));
        if (names)
        {
            type_test_format(text, "a%u", a);
        }
    }
    if (signature->variadic)
    {
        type_test_format(text, "%s...", signature->named != 0 ? ", " : "");
    }
    else if (signature->count == 0)
    {
        type_test_format(text, "%s", "void");
    }
    type_test_format(text, ")");
}

static bool abi_test_variadic(const AbiTestSignature* signature)
{
    return signature->variadic;
}

// The C file: the world's types, every signature's bytes, its C callee (which our caller calls) and C caller (which
// calls our callee), and main, which runs them all and prints how many checks failed.
// The bytes the C side copies and checks of its variable of a type: sizeof's, but for a SMALLER type clang's object's,
// which a copy of sizeof bytes would run past.
static void abi_test_c_size(const AbiTestCross* cross, TypeId type, const char* variable, char* buffer, uint64_t capacity)
{
    if ((cross->abi->pass[type >> BUSTER_TYPE_INDEX_SHIFT] & BUSTER_ABI_PASS_SMALLER) != 0)
    {
        snprintf(buffer, capacity, "%" PRIu64, abi_test_llvm_bytes(&cross->world->types.test.table, type));
    }
    else
    {
        snprintf(buffer, capacity, "sizeof(%s)", variable);
    }
}

static void abi_test_write_c(AbiTestCross* cross, TypeTestText* text, bool* variadic_empty)
{
    char size[64];
    char variable[32];
    AbiTestWorld* world = cross->world;
    const TypeTable* table = &world->types.test.table;
    type_test_format(text, "#include <stdarg.h>\n#include <stdio.h>\n#include <string.h>\n");
    type_test_format(text, "static int abi_checks;\nstatic int abi_failures;\n");
    type_test_format(text, "static void abi_check(const void* got, const void* want, const void* mask, unsigned long size, int signature, int argument, const char* what)\n{\n");
    type_test_format(text, "    const unsigned char* g = got;\n    const unsigned char* w = want;\n    const unsigned char* m = mask;\n    int bad = 0;\n");
    type_test_format(text, "    for (unsigned long i = 0; i < size; i += 1)\n    {\n        bad |= (g[i] ^ w[i]) & m[i];\n    }\n    abi_checks += 1;\n");
    type_test_format(text, "    if (bad != 0 && abi_failures < 40)\n    {\n        printf(\"FAIL %%s signature %%d argument %%d:\", what, signature, argument);\n");
    type_test_format(text, "        for (unsigned long i = 0; i < size; i += 1)\n        {\n            printf(\" %%02x/%%02x/%%02x\", g[i], w[i], m[i]);\n        }\n        printf(\"\\n\");\n    }\n");
    type_test_format(text, "    abi_failures += bad != 0 ? 1 : 0;\n}\n");
    // The types: their typedefs, which the oracle of c_types_test.c also prints (its expectations unused here).
    TypeTestOracle oracle = {
        .text = text,
        .expected = BUSTER_ARENA_PUSH_ARRAY(&world->types.arena, char*, table->types.count * 4),
        .what = BUSTER_ARENA_PUSH_ARRAY(&world->types.arena, const char*, table->types.count * 4),
        .capacity = table->types.count * 4,
    };
    type_test_write_types(&world->types, &oracle);
    for (uint32_t n = 0; n < cross->signature_count; n += 1)
    {
        const AbiTestSignature* signature = &cross->signatures[n];
        const char* result = abi_test_c_name(world, signature->result);
        const bool has_result = signature->result != BUSTER_TYPE_VOID;
        const uint64_t data = cross->data_begin[n];
        const uint64_t results = cross->result_begin[n];
        type_test_format(text, "\n// signature %u: result classes %02x, %s; al %u, stack %u bytes aligned to %u\n", n,
                         cross->abi->rows[(uint64_t)(signature->result >> BUSTER_TYPE_INDEX_SHIFT) * BUSTER_ABI_ROW_BYTES],
                         cross->calls[n].result.kind == BUSTER_ABI_RESULT_MEMORY ? "in memory" : "in registers", cross->calls[n].vector_registers, cross->calls[n].stack_bytes,
                         1u << cross->calls[n].stack_alignment_log2);
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            const TypeId type = cross->arguments[signature->first + a];
            const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
            const AbiLocation location = cross->locations[signature->first + a];
            type_test_format(text, "//   a%u: %s(%" PRIu64 " bytes, classes %02x, pass %08x): kind %u, registers %02x %02x, flags %u, stack %u\n", a,
                             abi_test_c_name(world, type), table->types.size[abi_resolved_index(table, type)], cross->abi->rows[(uint64_t)index * BUSTER_ABI_ROW_BYTES],
                             cross->abi->pass[index], location.kind, location.registers[0], location.registers[1], location.flags, location.stack_offset);
        }
        type_test_format(text, "_Alignas(64) unsigned char abi_args_%u[%u] = {", n, signature->data_bytes);
        abi_test_hex(text, cross->data + data, signature->data_bytes);
        type_test_format(text, "};\nconst unsigned char abi_mask_args_%u[%u] = {", n, signature->data_bytes);
        abi_test_hex(text, cross->data_mask + data, signature->data_bytes);
        type_test_format(text, "};\n_Alignas(64) unsigned char abi_ret_%u[%u] = {", n, signature->result_bytes);
        abi_test_hex(text, cross->results + results, signature->result_bytes);
        type_test_format(text, "};\nconst unsigned char abi_mask_ret_%u[%u] = {", n, signature->result_bytes);
        abi_test_hex(text, cross->result_mask + results, signature->result_bytes);
        type_test_format(text, "};\n_Alignas(64) unsigned char abi_capture_%u[%u];\n_Alignas(64) unsigned char abi_result_%u[%u];\n", n, signature->data_bytes, n,
                         signature->result_bytes);
        type_test_format(text, "void* abi_result_pointer_%u;\nunsigned char abi_al_%u;\n", n, n);
        // The C callee, which our caller calls.
        type_test_format(text, "%sc_callee_%u", result, n);
        abi_test_parameters(cross, text, n, true);
        type_test_format(text, "\n{\n");
        // Each argument is checked through a copy: clang 23 crashes eliding the copy of an argument whose address is
        // taken when a byval argument of no size (a record of a flexible array member alone) is on the stack before it.
        for (uint32_t a = 0; a < signature->named; a += 1)
        {
            const uint32_t offset = cross->offsets[signature->first + a];
            abi_test_c_size(cross, cross->arguments[signature->first + a], "c", size, sizeof(size));
            type_test_format(text, "    {\n        %sc = a%u;\n", abi_test_c_name(world, cross->arguments[signature->first + a]), a);
            type_test_format(text, "        abi_check(&c, abi_args_%u + %u, abi_mask_args_%u + %u, %s, %u, %u, \"A\");\n    }\n", n, offset, n, offset, size, n, a);
        }
        if (abi_test_variadic(signature))
        {
            type_test_format(text, "    va_list ap;\n");
            if (signature->named != 0)
            {
                type_test_format(text, "    va_start(ap, a%u);\n", signature->named - 1);
            }
            else
            {
                type_test_format(text, "    va_start(ap);\n");
                *variadic_empty = true;
            }
            for (uint32_t a = signature->named; a < signature->count; a += 1)
            {
                const uint32_t offset = cross->offsets[signature->first + a];
                const char* type = abi_test_c_name(world, cross->arguments[signature->first + a]);
                abi_test_c_size(cross, cross->arguments[signature->first + a], "v", size, sizeof(size));
                type_test_format(text, "    {\n        %sv = va_arg(ap, %s);\n", type, type);
                type_test_format(text, "        abi_check(&v, abi_args_%u + %u, abi_mask_args_%u + %u, %s, %u, %u, \"A va_arg\");\n    }\n", n, offset, n, offset, size, n, a);
            }
            type_test_format(text, "    va_end(ap);\n");
        }
        if (has_result)
        {
            abi_test_c_size(cross, signature->result, "r", size, sizeof(size));
            type_test_format(text, "    %sr;\n    memcpy(&r, abi_ret_%u, %s);\n    return r;\n", result, n, size);
        }
        type_test_format(text, "}\n");
        // Our callee, and the C caller of it.
        type_test_format(text, "%sbuster_callee_%u", result, n);
        abi_test_parameters(cross, text, n, false);
        type_test_format(text, ";\nvoid buster_call_%u(void);\nvoid c_call_%u(void)\n{\n", n, n);
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            const uint32_t offset = cross->offsets[signature->first + a];
            const TypeId type = cross->arguments[signature->first + a];
            const char* name = abi_test_c_name(world, type);
            // A _BitInt as wide as a vector register, copied into place with memcpy, crashes GCC 16's bitintlower
            // before a call whose result is an aggregate; a load of the type doesn't (the slots are 64-byte aligned,
            // and the program is built without strict aliasing).
            if (type_kind(&world->types.test.table, type_strip_aligned(&world->types.test.table, type)) == BUSTER_TYPE_KIND_BIT_INT)
            {
                type_test_format(text, "    %sa%u = *(const %s*)(abi_args_%u + %u);\n", name, a, name, n, offset);
            }
            else
            {
                snprintf(variable, sizeof(variable), "a%u", a);
                abi_test_c_size(cross, type, variable, size, sizeof(size));
                type_test_format(text, "    %sa%u;\n    memcpy(&a%u, abi_args_%u + %u, %s);\n", name, a, a, n, offset, size);
            }
        }
        type_test_format(text, "    abi_al_%u = 0xff;\n    ", n);
        if (has_result)
        {
            type_test_format(text, "%sr = ", result);
        }
        type_test_format(text, "buster_callee_%u(", n);
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            type_test_format(text, "%sa%u", a != 0 ? ", " : "", a);
        }
        type_test_format(text, ");\n");
        if (has_result)
        {
            abi_test_c_size(cross, signature->result, "r", size, sizeof(size));
            type_test_format(text, "    abi_check(&r, abi_ret_%u, abi_mask_ret_%u, %s, %u, -1, \"B result\");\n", n, n, size, n);
        }
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            const uint32_t offset = cross->offsets[signature->first + a];
            type_test_format(text, "    abi_check(abi_capture_%u + %u, abi_args_%u + %u, abi_mask_args_%u + %u, sizeof(a%u), %u, %u, \"B\");\n", n, offset, n, offset, n,
                             offset, a, n, a);
        }
        if (abi_test_variadic(signature) && !cross->compiled)
        {
            const uint8_t expected = cross->calls[n].vector_registers;
            const uint8_t al = expected;
            type_test_format(text, "    {\n        const unsigned char al = %u;\n        abi_check(&abi_al_%u, &al, \"\\xff\", 1, %u, -2, \"B al\");\n    }\n", al, n, n);
        }
        type_test_format(text, "}\n");
    }
    type_test_format(text, "\nint main(void)\n{\n");
    for (uint32_t n = 0; n < cross->signature_count; n += 1)
    {
        const AbiTestSignature* signature = &cross->signatures[n];
        type_test_format(text, "    buster_call_%u();\n", n);
        if (signature->result != BUSTER_TYPE_VOID)
        {
            const uint64_t size = table->types.size[abi_resolved_index(table, signature->result)];
            type_test_format(text, "    abi_check(abi_result_%u, abi_ret_%u, abi_mask_ret_%u, %" PRIu64 ", %u, -1, \"A result\");\n", n, n, n, size, n);
            if (cross->calls[n].result.kind == BUSTER_ABI_RESULT_MEMORY && !cross->compiled)
            {
                type_test_format(text, "    {\n        void* expected = abi_result_%u;\n        abi_check(&abi_result_pointer_%u, &expected, \"\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\", 8, %u, -1, \"A hidden pointer\");\n    }\n", n, n, n);
            }
        }
        type_test_format(text, "    c_call_%u();\n", n);
    }
    type_test_format(text, "    printf(\"checks %%d failures %%d\\n\", abi_checks, abi_failures);\n    return abi_failures != 0;\n}\n");
}
// Our side as C, which this compiler compiles (a compiled world's): the world's types, the C side's data and callees
// declared, and per signature our callee, which captures each named argument's bytes and each unnamed one's through
// va_arg and returns the result's bytes, and our caller, which calls the C callee with the arguments' bytes and keeps
// its result's.
static void abi_test_write_ours(AbiTestCross* cross, TypeTestText* text)
{
    AbiTestWorld* world = cross->world;
    const TypeTable* table = &world->types.test.table;
    type_test_format(text, "#include <stdarg.h>\n#include <string.h>\n");
    TypeTestOracle oracle = {
        .text = text,
        .expected = BUSTER_ARENA_PUSH_ARRAY(&world->types.arena, char*, table->types.count * 4),
        .what = BUSTER_ARENA_PUSH_ARRAY(&world->types.arena, const char*, table->types.count * 4),
        .capacity = table->types.count * 4,
    };
    type_test_write_types(&world->types, &oracle);
    for (uint32_t n = 0; n < cross->signature_count; n += 1)
    {
        const AbiTestSignature* signature = &cross->signatures[n];
        const char* result = abi_test_c_name(world, signature->result);
        const bool has_result = signature->result != BUSTER_TYPE_VOID;
        type_test_format(text, "\nextern unsigned char abi_args_%u[];\nextern unsigned char abi_ret_%u[];\n", n, n);
        type_test_format(text, "extern unsigned char abi_capture_%u[];\nextern unsigned char abi_result_%u[];\n%sc_callee_%u", n, n, result, n);
        abi_test_parameters(cross, text, n, false);
        type_test_format(text, ";\n%sbuster_callee_%u", result, n);
        abi_test_parameters(cross, text, n, true);
        type_test_format(text, "\n{\n");
        for (uint32_t a = 0; a < signature->named; a += 1)
        {
            type_test_format(text, "    memcpy(abi_capture_%u + %u, &a%u, sizeof(a%u));\n", n, cross->offsets[signature->first + a], a, a);
        }
        if (abi_test_variadic(signature))
        {
            type_test_format(text, "    va_list ap;\n");
            if (signature->named != 0)
            {
                type_test_format(text, "    va_start(ap, a%u);\n", signature->named - 1);
            }
            else
            {
                type_test_format(text, "    va_start(ap);\n");
            }
            for (uint32_t a = signature->named; a < signature->count; a += 1)
            {
                const char* type = abi_test_c_name(world, cross->arguments[signature->first + a]);
                type_test_format(text, "    {\n        %sv = va_arg(ap, %s);\n", type, type);
                type_test_format(text, "        memcpy(abi_capture_%u + %u, &v, sizeof(v));\n    }\n", n, cross->offsets[signature->first + a]);
            }
            type_test_format(text, "    va_end(ap);\n");
        }
        if (has_result)
        {
            type_test_format(text, "    %sr;\n    memcpy(&r, abi_ret_%u, sizeof(r));\n    return r;\n", result, n);
        }
        type_test_format(text, "}\nvoid buster_call_%u(void)\n{\n", n);
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            const uint32_t offset = cross->offsets[signature->first + a];
            type_test_format(text, "    %sa%u;\n    memcpy(&a%u, abi_args_%u + %u, sizeof(a%u));\n", abi_test_c_name(world, cross->arguments[signature->first + a]), a, a, n,
                             offset, a);
        }
        type_test_format(text, "    ");
        if (has_result)
        {
            type_test_format(text, "%sr = ", result);
        }
        type_test_format(text, "c_callee_%u(", n);
        for (uint32_t a = 0; a < signature->count; a += 1)
        {
            type_test_format(text, "%sa%u", a != 0 ? ", " : "", a);
        }
        type_test_format(text, ");\n");
        if (has_result)
        {
            type_test_format(text, "    memcpy(abi_result_%u, &r, sizeof(r));\n", n);
        }
        type_test_format(text, "}\n");
    }
}

// A function of ours being built: its records and the references of their symbolic fields.
typedef struct AbiTestCode AbiTestCode;
struct AbiTestCode
{
    X86Instruction instructions[BUSTER_ABI_TEST_MAX_INSTRUCTIONS];
    LinkReference references[BUSTER_ABI_TEST_MAX_INSTRUCTIONS];
    uint32_t fixup_ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(BUSTER_ABI_TEST_MAX_INSTRUCTIONS)];
    uint32_t count;
    uint32_t reference_count;
    bool overflow;
};

static void abi_test_add(AbiTestCode* code, X86Instruction instruction)
{
    if (code->count < BUSTER_ABI_TEST_MAX_INSTRUCTIONS)
    {
        code->instructions[code->count] = instruction;
        code->count += 1;
    }
    else
    {
        code->overflow = true;
    }
}

// An instruction with a RIP-relative memory operand at symbol + addend.
static void abi_test_rip(AbiTestCode* code, uint32_t form, uint8_t reg, SymbolId symbol, int64_t addend)
{
    if (code->count < BUSTER_ABI_TEST_MAX_INSTRUCTIONS)
    {
        code->references[code->reference_count] = (LinkReference){
            .instruction = code->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_PC32,
            .target = symbol,
            .addend = addend,
        };
        code->reference_count += 1;
    }
    X86Instruction instruction = link_program_rip(form, reg);
    instruction.form |= BUSTER_X86_FORM_FIXUP;
    abi_test_add(code, instruction);
}

static void abi_test_call(AbiTestCode* code, SymbolId symbol)
{
    if (code->count < BUSTER_ABI_TEST_MAX_INSTRUCTIONS)
    {
        code->references[code->reference_count] = (LinkReference){
            .instruction = code->count,
            .field = X86_FIXUP_DISPLACEMENT,
            .kind = BUSTER_LINK_FIXUP_CALL,
            .target = symbol,
            .addend = 0,
        };
        code->reference_count += 1;
    }
    X86Instruction instruction = link_program_bare(X86_FORM_CALL_REL32);
    instruction.form |= BUSTER_X86_FORM_FIXUP;
    abi_test_add(code, instruction);
}

// A register's load of `bytes` bytes (a vector register's whole width past 8, or 8 with movq; a general register's 8,
// or the extending load of a scalar of 1, 2 or 4 bytes), and its store of what a part holds.
static uint32_t abi_test_load_form(uint8_t reg, uint32_t bytes, uint8_t extension)
{
    uint32_t result = X86_FORM_MOV_R64_RM64;
    if ((reg & BUSTER_ABI_REGISTER_VECTOR) != 0)
    {
        result = bytes <= 8 ? X86_FORM_MOVQ_X_XM64 : bytes == 16 ? X86_FORM_MOVDQU_X_XM : bytes == 32 ? X86_FORM_VMOVDQU_Y_YM256 : X86_FORM_VMOVDQU64_Z_ZM512;
    }
    else if (extension == BUSTER_ABI_FLAG_SIGN)
    {
        result = bytes == 1 ? X86_FORM_MOVSX_R64_RM8 : bytes == 2 ? X86_FORM_MOVSX_R64_RM16 : X86_FORM_MOVSXD_R64_RM32;
    }
    else if (extension == BUSTER_ABI_FLAG_ZERO)
    {
        result = bytes == 1 ? X86_FORM_MOVZX_R32_RM8 : bytes == 2 ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOV_R32_RM32;
    }
    return result;
}

static uint32_t abi_test_store_form(uint8_t reg, uint32_t bytes)
{
    uint32_t result = X86_FORM_MOV_RM64_R64;
    if ((reg & BUSTER_ABI_REGISTER_VECTOR) != 0)
    {
        result = bytes <= 8 ? X86_FORM_MOVQ_XM64_X : bytes == 16 ? X86_FORM_MOVDQU_XM_X : bytes == 32 ? X86_FORM_VMOVDQU_YM256_Y : X86_FORM_VMOVDQU64_ZM512_Z;
    }
    return result;
}

// The bytes a copy of a stack argument moves: its slot's, but a LARGER type's own, rounded up to 8: the rest of its slot,
// which clang's larger LLVM type takes, is padding, and a GROWN one passed as an integer has 8 bytes.
static uint32_t abi_test_copied_bytes(const AbiTable* abi, const TypeTable* table, TypeId type)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint64_t size = table->types.size[abi_resolved_index(table, type)];
    return (abi->pass[index] & BUSTER_ABI_PASS_LARGER) != 0 ? (uint32_t)((size + 7) & ~(uint64_t)7) : abi->stack_bytes[index];
}

// A value's parts in registers or 8-byte stack slots: which register (NONE for the slot of a SPLIT one), where in the
// value it starts, how many bytes a load of it reads, and whether the load extends.
typedef struct AbiTestPart AbiTestPart;
struct AbiTestPart
{
    uint8_t reg;
    uint8_t extension;
    uint32_t offset;
    uint32_t bytes;
};

static uint32_t abi_test_parts(const AbiTable* abi, const TypeTable* table, TypeId type, AbiLocation location, AbiTestPart* parts)
{
    const uint32_t index = type >> BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t pass = abi->pass[index];
    const uint64_t size = table->types.size[abi_resolved_index(table, type)];
    const uint32_t value = (pass & BUSTER_ABI_PASS_VALUE) >> BUSTER_ABI_PASS_VALUE_SHIFT;
    const uint8_t extension = location.flags & (BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO);
    uint32_t count = 0;
    if (location.kind != BUSTER_ABI_LOCATION_REGISTERS && location.kind != BUSTER_ABI_LOCATION_SPLIT)
    {
    }
    else if ((pass & BUSTER_ABI_PASS_VECTOR) != 0 || ((pass & BUSTER_ABI_PASS_UNCOUNTED) != 0 && value == BUSTER_ABI_VALUE_VECTOR))
    {
        parts[0] = (AbiTestPart){
            .reg = location.registers[0],
            .extension = 0,
            .offset = 0,
            .bytes = size <= 8 ? 8 : (uint32_t)size,
        };
        count = 1;
    }
    else if ((pass & BUSTER_ABI_PASS_UNCOUNTED) != 0)
    {
        // Two halves for an i128 or a <1 x i128> (the second on the stack when SPLIT), else one.
        const uint32_t halves = value == BUSTER_ABI_VALUE_PAIR || value == BUSTER_ABI_VALUE_HALVES ? 2u : 1u;
        for (uint32_t i = 0; i < halves; i += 1)
        {
            parts[i] = (AbiTestPart){
                .reg = location.registers[i],
                .extension = 0,
                .offset = 8 * i,
                .bytes = 8,
            };
        }
        count = halves;
    }
    else
    {
        const bool halves[2] = {
            (pass & (BUSTER_ABI_PASS_LOW_INTEGER | BUSTER_ABI_PASS_LOW_SSE)) != 0,
            (pass & (BUSTER_ABI_PASS_HIGH_INTEGER | BUSTER_ABI_PASS_HIGH_SSE)) != 0,
        };
        for (uint32_t half = 0; half < 2; half += 1)
        {
            if (halves[half])
            {
                // A scalar narrower than 8 bytes loads extended, as its flags ask.
                const bool extend = extension != 0 && size < 8;
                parts[count] = (AbiTestPart){
                    .reg = location.registers[count],
                    .extension = extend ? extension : 0,
                    .offset = 8 * half,
                    .bytes = extend ? (uint32_t)size : 8,
                };
                count += 1;
            }
        }
        // An aggregate of 8 bytes or less that clang passes as an integer once it has no general register free, MEMORY
        // or not, in the register LLVM has left.
        if (count == 0 && location.registers[0] != BUSTER_ABI_REGISTER_NONE)
        {
            parts[0] = (AbiTestPart){
                .reg = location.registers[0],
                .extension = 0,
                .offset = 0,
                .bytes = 8,
            };
            count = 1;
        }
    }
    return count;
}

// The symbols of a signature: the C file's data and callee, our caller and callee.
typedef struct AbiTestSymbols AbiTestSymbols;
struct AbiTestSymbols
{
    SymbolId args;
    SymbolId ret;
    SymbolId capture;
    SymbolId result;
    SymbolId result_pointer;
    SymbolId al;
    SymbolId c_callee;
    SymbolId caller;
    SymbolId callee;
};

static SymbolId abi_test_symbol(Link* link, const char* prefix, uint32_t n, uint8_t flags)
{
    char name[64];
    const int length = snprintf(name, sizeof(name), "%s%u", prefix, n);
    const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)length);
    return id != 0 ? link_add_symbol(link, id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) : 0;
}

// Our caller of the C callee: the stack arguments copied into the argument area (rep movsb takes rsi, rdi and rcx, so
// first), then the register ones loaded, the hidden pointer and al set, the call, and the result stored.
static void abi_test_emit_caller(AbiTestCross* cross, uint32_t n, AbiTestSymbols symbols, AbiTestCode* code)
{
    const TypeTable* table = &cross->world->types.test.table;
    const AbiTestSignature* signature = &cross->signatures[n];
    const AbiCall call = cross->calls[n];
    const AbiLocation* locations = cross->locations + signature->first;
    const uint32_t* offsets = cross->offsets + signature->first;
    const uint64_t alignment = 1ull << call.stack_alignment_log2;
    abi_test_add(code, link_program_registers(X86_FORM_PUSH_R64, 0, X86_REGISTER_RBP));
    abi_test_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RBP, X86_REGISTER_RSP));
    abi_test_add(code, link_program_immediate(X86_FORM_SUB_RM64_IMM32, X86_REGISTER_RSP, (int32_t)abi_test_round(call.stack_bytes, 16)));
    if (alignment > 16)
    {
        abi_test_add(code, link_program_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_RSP, (int32_t)-(int64_t)alignment));
    }
    AbiTestPart parts[2];
    for (uint32_t a = 0; a < signature->count; a += 1)
    {
        const AbiLocation location = locations[a];
        const TypeId type = cross->arguments[signature->first + a];
        const uint32_t count = abi_test_parts(cross->abi, table, type, location, parts);
        if (location.kind == BUSTER_ABI_LOCATION_STACK && (location.flags & BUSTER_ABI_FLAG_HIGH) != 0)
        {
            abi_test_rip(code, X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, symbols.args, offsets[a] + 8);
            abi_test_add(code, link_program_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, (int32_t)location.stack_offset));
        }
        else if (location.kind == BUSTER_ABI_LOCATION_STACK)
        {
            abi_test_rip(code, X86_FORM_LEA_R64_M, X86_REGISTER_RSI, symbols.args, offsets[a]);
            abi_test_add(code, link_program_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RDI, X86_REGISTER_RSP, (int32_t)location.stack_offset));
            abi_test_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RCX, (int32_t)abi_test_copied_bytes(cross->abi, table, type)));
            abi_test_add(code, link_program_bare(X86_FORM_REP_MOVS_B));
        }
        for (uint32_t p = 0; p < count; p += 1)
        {
            if (parts[p].reg == BUSTER_ABI_REGISTER_NONE)
            {
                abi_test_rip(code, X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, symbols.args, offsets[a] + parts[p].offset);
                abi_test_add(code, link_program_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, X86_REGISTER_RSP, (int32_t)location.stack_offset));
            }
        }
    }
    for (uint32_t a = 0; a < signature->count; a += 1)
    {
        const TypeId type = cross->arguments[signature->first + a];
        const uint32_t count = abi_test_parts(cross->abi, table, type, locations[a], parts);
        for (uint32_t p = 0; p < count; p += 1)
        {
            if (parts[p].reg != BUSTER_ABI_REGISTER_NONE)
            {
                const uint32_t form = abi_test_load_form(parts[p].reg, parts[p].bytes, parts[p].extension);
                abi_test_rip(code, form, parts[p].reg & BUSTER_X86_REGISTER_NUMBER, symbols.args, offsets[a] + parts[p].offset);
            }
        }
    }
    if (call.result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        abi_test_rip(code, X86_FORM_LEA_R64_M, X86_REGISTER_RDI, symbols.result, 0);
    }
    abi_test_add(code, link_program_immediate(X86_FORM_MOV_R8_IMM8, X86_REGISTER_RAX, call.vector_registers));
    abi_test_call(code, symbols.c_callee);
    if (call.result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        abi_test_rip(code, X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, symbols.result_pointer, 0);
    }
    else if (call.result.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        const uint32_t high = (call.result.flags & BUSTER_ABI_FLAG_HIGH) != 0 ? 8 : 0;
        for (uint32_t p = 0; p < BUSTER_ABI_RESULT_PARTS && call.result.registers[p] != BUSTER_ABI_REGISTER_NONE; p += 1)
        {
            const uint8_t reg = call.result.registers[p];
            const uint32_t offset = high + p * call.result.part_bytes;
            if ((reg & BUSTER_ABI_REGISTER_X87) != 0)
            {
                // st0 each time: the store pops st1 into st0. An x87 register holds a double for a part of 8 bytes.
                abi_test_rip(code, call.result.part_bytes == 8 ? X86_FORM_FSTP_M64 : X86_FORM_FSTP_M80, 0, symbols.result, offset);
            }
            else
            {
                abi_test_rip(code, abi_test_store_form(reg, call.result.part_bytes), reg & BUSTER_X86_REGISTER_NUMBER, symbols.result, offset);
            }
        }
    }
    abi_test_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RSP, X86_REGISTER_RBP));
    abi_test_add(code, link_program_registers(X86_FORM_POP_R64, 0, X86_REGISTER_RBP));
    abi_test_add(code, link_program_bare(X86_FORM_RET));
}

// Our callee: the register arguments and al stored, then the stack ones copied from above the return address, then the
// result loaded (or copied through the hidden pointer, kept in r11).
static void abi_test_emit_callee(AbiTestCross* cross, uint32_t n, AbiTestSymbols symbols, AbiTestCode* code)
{
    const TypeTable* table = &cross->world->types.test.table;
    const AbiTestSignature* signature = &cross->signatures[n];
    const AbiCall call = cross->calls[n];
    const AbiLocation* locations = cross->locations + signature->first;
    const uint32_t* offsets = cross->offsets + signature->first;
    if (call.result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        abi_test_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_R11, X86_REGISTER_RDI));
    }
    AbiTestPart parts[2];
    for (uint32_t a = 0; a < signature->count; a += 1)
    {
        const TypeId type = cross->arguments[signature->first + a];
        const uint32_t count = abi_test_parts(cross->abi, table, type, locations[a], parts);
        for (uint32_t p = 0; p < count; p += 1)
        {
            if (parts[p].reg != BUSTER_ABI_REGISTER_NONE)
            {
                const uint32_t bytes = parts[p].bytes < 8 ? 8 : parts[p].bytes;
                abi_test_rip(code, abi_test_store_form(parts[p].reg, bytes), parts[p].reg & BUSTER_X86_REGISTER_NUMBER, symbols.capture, offsets[a] + parts[p].offset);
            }
        }
    }
    abi_test_rip(code, X86_FORM_MOV_RM8_R8, X86_REGISTER_RAX, symbols.al, 0);
    for (uint32_t a = 0; a < signature->count; a += 1)
    {
        const AbiLocation location = locations[a];
        const TypeId type = cross->arguments[signature->first + a];
        const uint32_t count = abi_test_parts(cross->abi, table, type, location, parts);
        const int32_t slot = (int32_t)location.stack_offset + 8;
        if (location.kind == BUSTER_ABI_LOCATION_STACK && (location.flags & BUSTER_ABI_FLAG_HIGH) != 0)
        {
            abi_test_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RSP, slot));
            abi_test_rip(code, X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, symbols.capture, offsets[a] + 8);
        }
        else if (location.kind == BUSTER_ABI_LOCATION_STACK)
        {
            abi_test_add(code, link_program_memory(X86_FORM_LEA_R64_M, X86_REGISTER_RSI, X86_REGISTER_RSP, slot));
            abi_test_rip(code, X86_FORM_LEA_R64_M, X86_REGISTER_RDI, symbols.capture, offsets[a]);
            abi_test_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RCX, (int32_t)abi_test_copied_bytes(cross->abi, table, type)));
            abi_test_add(code, link_program_bare(X86_FORM_REP_MOVS_B));
        }
        for (uint32_t p = 0; p < count; p += 1)
        {
            if (parts[p].reg == BUSTER_ABI_REGISTER_NONE)
            {
                abi_test_add(code, link_program_memory(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_RSP, slot));
                abi_test_rip(code, X86_FORM_MOV_RM64_R64, X86_REGISTER_RAX, symbols.capture, offsets[a] + parts[p].offset);
            }
        }
    }
    if (call.result.kind == BUSTER_ABI_RESULT_MEMORY)
    {
        // A SMALLER type's LLVM bytes: clang's object behind the hidden pointer is no larger.
        const uint32_t result_index = signature->result >> BUSTER_TYPE_INDEX_SHIFT;
        const bool smaller = (cross->abi->pass[result_index] & BUSTER_ABI_PASS_SMALLER) != 0;
        const uint64_t size = smaller ? cross->abi->stack_bytes[result_index] : table->types.size[abi_resolved_index(table, signature->result)];
        abi_test_rip(code, X86_FORM_LEA_R64_M, X86_REGISTER_RSI, symbols.ret, 0);
        abi_test_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RDI, X86_REGISTER_R11));
        abi_test_add(code, link_program_immediate(X86_FORM_MOV_R32_IMM32, X86_REGISTER_RCX, (int32_t)size));
        abi_test_add(code, link_program_bare(X86_FORM_REP_MOVS_B));
        abi_test_add(code, link_program_registers(X86_FORM_MOV_R64_RM64, X86_REGISTER_RAX, X86_REGISTER_R11));
    }
    else if (call.result.kind == BUSTER_ABI_RESULT_REGISTERS)
    {
        const uint64_t size = table->types.size[abi_resolved_index(table, signature->result)];
        const uint32_t high = (call.result.flags & BUSTER_ABI_FLAG_HIGH) != 0 ? 8 : 0;
        const uint8_t extension = call.result.flags & (BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO);
        uint32_t count = 0;
        while (count < BUSTER_ABI_RESULT_PARTS && call.result.registers[count] != BUSTER_ABI_REGISTER_NONE)
        {
            count += 1;
        }
        // x87's parts are pushed last first, so that st0 is the first.
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t p = (call.result.registers[0] & BUSTER_ABI_REGISTER_X87) != 0 ? count - 1 - i : i;
            const uint8_t reg = call.result.registers[p];
            if ((reg & BUSTER_ABI_REGISTER_X87) != 0)
            {
                abi_test_rip(code, call.result.part_bytes == 8 ? X86_FORM_FLD_M64 : X86_FORM_FLD_M80, 0, symbols.ret, high + p * call.result.part_bytes);
            }
            else
            {
                const bool extend = extension != 0 && size < 8;
                const uint32_t form = abi_test_load_form(reg, extend ? (uint32_t)size : call.result.part_bytes, extend ? extension : 0);
                abi_test_rip(code, form, reg & BUSTER_X86_REGISTER_NUMBER, symbols.ret, high + p * call.result.part_bytes);
            }
        }
    }
    abi_test_add(code, link_program_bare(X86_FORM_RET));
}

// Encodes a function into an atom of the link, with its fixups, and defines its symbol there.
static bool abi_test_finish(Link* link, Arena* bytes, AbiTestCode* code, SymbolId symbol)
{
    uint32_t marked = 0;
    for (uint32_t i = 0; i < code->count; i += 1)
    {
        marked += (code->instructions[i].form & BUSTER_X86_FORM_FIXUP) != 0 ? 1 : 0;
    }
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    uint8_t* output = arena_push(bytes, capacity, 16);
    const bool valid = !code->overflow && output != 0 && x86_validate(code->instructions, code->count) == code->count;
    const int64_t length = valid ? x86_encode(code->instructions, code->count, output, capacity, 0, code->fixup_ends) : -1;
    bool result = false;
    if (length >= 0)
    {
        const uint64_t padded = abi_test_round((uint64_t)length, 16);
        memset(output + length, 0xcc, padded - (uint64_t)length);
        arena_pop_to(bytes, (uint64_t)(output - bytes->base) + padded);
        const AtomId atom = link_add_atom(link, BUSTER_LINK_CLASS_TEXT, 4, padded, output, 0);
        result = atom != 0 && link_add_code_fixups(link, atom, code->instructions, code->count, code->fixup_ends, marked, code->references, code->reference_count) &&
                 link_define_symbol(link, symbol, atom, 0, (uint64_t)length);
    }
    return result;
}

// Our object: every signature's caller and callee in one unit.
static bool abi_test_write_object(AbiTestCross* cross, const char* path, AbiTestCode* code)
{
    const uint32_t count = cross->signature_count;
    const LinkCapacity capacity = {
        .names = 16 * count + 16,
        .atoms = 2 * count + 2,
        .symbols = 16 * count + 16,
        .fixups = 2 * count * (6 * BUSTER_ABI_TEST_MAX_ARGUMENTS + 32) + 16,
        .wide_addends = 2,
        .priorities = 2,
    };
    Arena tables = arena_reserve(link_capacity_bytes(capacity));
    Arena names = arena_reserve(64ull * capacity.names + 4096);
    Arena bytes = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    Arena objects = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    Link link;
    bool result = tables.base != 0 && names.base != 0 && bytes.base != 0 && objects.base != 0 && link_initialize(&link, &tables, &names, capacity);
    for (uint32_t n = 0; result && n < count; n += 1)
    {
        const uint8_t data = BUSTER_LINK_SYMBOL_NOTYPE | BUSTER_LINK_SYMBOL_GLOBAL;
        const uint8_t function = BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL;
        AbiTestSymbols symbols = {
            .args = abi_test_symbol(&link, "abi_args_", n, data),
            .ret = abi_test_symbol(&link, "abi_ret_", n, data),
            .capture = abi_test_symbol(&link, "abi_capture_", n, data),
            .result = abi_test_symbol(&link, "abi_result_", n, data),
            .result_pointer = abi_test_symbol(&link, "abi_result_pointer_", n, data),
            .al = abi_test_symbol(&link, "abi_al_", n, data),
            .c_callee = abi_test_symbol(&link, "c_callee_", n, data),
            .caller = 0,
            .callee = 0,
        };
        char name[64];
        int length = snprintf(name, sizeof(name), "buster_call_%u", n);
        symbols.caller = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, function, BUSTER_LINK_SOURCE_PROGRAM);
        length = snprintf(name, sizeof(name), "buster_callee_%u", n);
        symbols.callee = link_add_symbol(&link, link_add_name(&link, (const uint8_t*)name, (uint32_t)length), 0, 0, 0, function, BUSTER_LINK_SOURCE_PROGRAM);
        code->count = 0;
        code->reference_count = 0;
        code->overflow = false;
        abi_test_emit_caller(cross, n, symbols, code);
        result = symbols.callee != 0 && abi_test_finish(&link, &bytes, code, symbols.caller);
        code->count = 0;
        code->reference_count = 0;
        code->overflow = false;
        abi_test_emit_callee(cross, n, symbols, code);
        result = result && abi_test_finish(&link, &bytes, code, symbols.callee);
    }
    ObjectUnits units;
    ObjectSymbols object_symbols;
    result = result && link_check(&link) == LINK_ERROR_NONE && object_prepare(&units, &link, 1, &objects) && object_map_symbols(&object_symbols, &link, &objects);
    if (result)
    {
        const ObjectOptions options = {
            .function_sections = false,
        };
        const Object object = object_build(&link, &units, &object_symbols, 0, options, BUSTER_S8("calling_convention.c"), &objects);
        result = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
    }
    arena_release(&tables);
    arena_release(&names);
    arena_release(&bytes);
    arena_release(&objects);
    return result;
}

// A run of the cross-check, in a child process: its name in the report, its files' prefix, its C compiler and vector
// width, whether our side is C compiled by this compiler (a compiled world's), and whether it is an extension run, with
// plain char signed or unsigned. What the child writes to its standard output and errors goes to memfds of its own, for
// the parent to print.
typedef struct AbiTestCrossRun AbiTestCrossRun;
struct AbiTestCrossRun
{
    const char* name;
    const char* file;
    uint32_t vector_bytes;
    bool gcc;
    bool compiled;
    bool extension;
    bool char_unsigned;
    pid_t child;
    int output;
    int errors;
};

// Run `index` (clang's at 16-, 32- and 64-byte vectors, then GCC's, then a compiled world's against clang and against
// GCC, at 16 bytes, then the extension runs with signed and with unsigned char), not started.
static AbiTestCrossRun abi_test_cross_run(uint32_t index)
{
    const bool extension = index >= 2 * BUSTER_ABI_TEST_CROSS_WIDTHS + 2;
    const bool char_unsigned = index == 2 * BUSTER_ABI_TEST_CROSS_WIDTHS + 3;
    const bool compiled = index >= 2 * BUSTER_ABI_TEST_CROSS_WIDTHS;
    const bool gcc = !extension && (compiled ? index == 2 * BUSTER_ABI_TEST_CROSS_WIDTHS + 1 : index >= BUSTER_ABI_TEST_CROSS_WIDTHS);
    const AbiTestCrossRun run = {
        .name = extension ? (char_unsigned ? "extension, unsigned char" : "extension, signed char") :
                compiled ? (gcc ? "compiled gcc" : "compiled clang") : gcc ? "gcc" : "clang",
        .file = extension ? (char_unsigned ? "extension_unsigned" : "extension_signed") :
                compiled ? (gcc ? "compiled_gcc" : "compiled_clang") : gcc ? "gcc" : "clang",
        .vector_bytes = compiled ? 16u : 16u << (index % BUSTER_ABI_TEST_CROSS_WIDTHS),
        .gcc = gcc,
        .compiled = compiled,
        .extension = extension,
        .char_unsigned = char_unsigned,
        .child = -1,
        .output = -1,
        .errors = -1,
    };
    return run;
}

// One run of the cross-check: a world for the C compiler (GCC's without what GCC passes otherwise, a compiled one's of
// what this compiler's C passes), `count` signatures over its passable types, and the C file, compiled by `compiler`,
// linked by its driver with our side, records in an object or, in a compiled world, C compiled by `buster`, and run.
static bool abi_test_cross(AbiTestTally* tally, uint64_t seed, uint32_t count, const char* compiler, AbiTestCrossRun run, const char* buster, const char* directory, bool keep)
{
    char detail[512];
    AbiTestWorld world;
    bool result = false;
    const bool gcc = run.gcc;
    const bool compiled = run.compiled;
    const uint32_t vector_bytes = run.vector_bytes;
    if (abi_test_expect(tally, abi_test_world_open(&world, 8192), "cross-check", "the world can't be made"))
    {
        world.gcc = gcc;
        world.compiled = compiled;
        abi_test_world_build(&world, seed, 600);
        // GCC's worlds have neither nullptr_t nor packing, and compiled ones neither nullptr_t nor the directed records.
        TypeId directed[BUSTER_ABI_TEST_DIRECTED];
        const uint32_t directed_count = gcc || compiled ? 0 : abi_test_world_directed(&world, directed);
        type_test_spell_world(&world.types);
        // GCC has no __fp16 on x86-64, nor a spelling of _Complex __float128, and it passes a _Complex __bf16 in two
        // SSE registers but counts one, as neither its va_arg nor clang reads it (nothing in its world is made of these).
        world.types.spellings[BUSTER_TYPE_CODE_HALF].spellable = !gcc;
        world.types.spellings[BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_FLOAT128].spellable = !gcc;
        world.types.spellings[BUSTER_TYPE_CODE_COMPLEX + BUSTER_TYPE_CODE_BF16].spellable = !gcc;
        const TypeTable* table = &world.types.test.table;
        Arena arena = arena_reserve(abi_capacity_bytes(table->types.capacity));
        AbiTable abi;
        AbiTestFrame* frames = malloc(sizeof(AbiTestFrame) * BUSTER_ABI_TEST_FRAMES);
        AbiTestFillItem* stack = malloc(sizeof(AbiTestFillItem) * BUSTER_ABI_TEST_FILL_ITEMS);
        TypeId* types = malloc(sizeof(TypeId) * table->types.count);
        TypeId* unnamed_types = malloc(sizeof(TypeId) * table->types.count);
        const uint32_t arguments = count * BUSTER_ABI_TEST_MAX_ARGUMENTS;
        AbiTestCross cross = {
            .world = &world,
            .abi = &abi,
            .signatures = malloc(sizeof(AbiTestSignature) * count),
            .signature_count = count,
            .arguments = malloc(sizeof(TypeId) * arguments),
            .offsets = malloc(sizeof(uint32_t) * arguments),
            .locations = malloc(sizeof(AbiLocation) * arguments),
            .calls = malloc(sizeof(AbiCall) * count),
            .data = malloc((uint64_t)count * BUSTER_ABI_TEST_MAX_ARGUMENTS * 320 + (uint64_t)count * 64),
            .data_mask = malloc((uint64_t)count * BUSTER_ABI_TEST_MAX_ARGUMENTS * 320 + (uint64_t)count * 64),
            .data_begin = malloc(sizeof(uint64_t) * count),
            .results = malloc((uint64_t)count * 384),
            .result_mask = malloc((uint64_t)count * 384),
            .result_begin = malloc(sizeof(uint64_t) * count),
            .gcc = gcc,
            .compiled = compiled,
        };
        AbiTestCode* code = malloc(sizeof(AbiTestCode));
        const bool memory = frames != 0 && stack != 0 && types != 0 && unnamed_types != 0 && cross.signatures != 0 && cross.arguments != 0 && cross.offsets != 0 &&
                            cross.locations != 0 && cross.calls != 0 && cross.data != 0 && cross.data_mask != 0 && cross.data_begin != 0 && cross.results != 0 &&
                            cross.result_mask != 0 && cross.result_begin != 0 && code != 0;
        uint8_t* empty_arrays = memory ? abi_test_empty_arrays(table) : 0;
        uint16_t* bit_field_bytes = memory ? abi_test_bit_field_bytes(table) : 0;
        uint8_t* holds = memory ? abi_test_holds(table) : 0;
        if (abi_test_expect(tally, empty_arrays != 0 && bit_field_bytes != 0 && holds != 0 && abi_initialize(&abi, &arena, table, vector_bytes), "cross-check", "no memory"))
        {
            abi_classify_all(&abi, table);
            uint32_t type_count = 0;
            uint32_t unnamed_count = 0;
            // nullptr_t, which the world doesn't spell, as clang's C can.
            if (!gcc && !compiled)
            {
                types[type_count] = BUSTER_TYPE_NULLPTR;
                type_count += 1;
            }
            for (uint32_t index = 1; index < table->types.count; index += 1)
            {
                const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
                const bool spelled = world.types.spellings[index].spellable && world.types.spellings[index].number != UINT32_MAX;
                const bool small = table->types.size[abi_resolved_index(table, type)] <= 256;
                const bool atomic_result = table->types.kind[index] == BUSTER_TYPE_KIND_ATOMIC;
                const bool wide_aggregate = (abi.pass[index] & (BUSTER_ABI_PASS_WIDE | BUSTER_ABI_PASS_AGGREGATE)) == (BUSTER_ABI_PASS_WIDE | BUSTER_ABI_PASS_AGGREGATE);
                // GCC passes a LOW_GENERAL type's low eightbyte in a vector register, as the psABI says.
                const bool low_general = (abi.pass[index] & BUSTER_ABI_PASS_LOW_GENERAL) != 0;
                const bool gcc_differs = gcc && (abi_test_gcc_enum_differs(&world, type) || (wide_aggregate && !abi_test_single_vector(table, type)) || low_general);
                // clang crashes on some types passed in registers that hold an array of elements of no size.
                const bool in_registers = (abi.pass[index] & (BUSTER_ABI_PASS_MEMORY | BUSTER_ABI_PASS_IGNORE)) == 0;
                const bool clang_crashes = !gcc && in_registers && empty_arrays[index] != 0;
                // clang's access units of bit-fields, which the module doesn't work out, decide whether a narrow SSE low
                // eightbyte widens (abi_test_bit_field_bytes).
                const AbiClasses classes = abi_classes(&abi, type, true);
                const bool unit_decides = !gcc && (classes & 7) == BUSTER_ABI_SSE && (classes >> 4) == BUSTER_ABI_INTEGER && (bit_field_bytes[index] & 0xff00u) != 0;
                // A record SSE, SSEUP of a size that isn't a vector register's, which clang passes as <size / 8 x double>
                // (a union of a 16-byte vector and a longer array): not modeled yet (docs/calling-convention.md, "Next
                // steps").
                const uint64_t resolved_size = table->types.size[abi_resolved_index(table, type)];
                const uint8_t resolved_kind = table->types.kind[abi_resolved_index(table, type)];
                const bool odd_vector_record = (resolved_kind == BUSTER_TYPE_KIND_STRUCT || resolved_kind == BUSTER_TYPE_KIND_UNION) &&
                                               (abi.pass[index] & BUSTER_ABI_PASS_VECTOR) != 0 && resolved_size != 16 && resolved_size != 32 && resolved_size != 64;
                // clang's atomic loads of an _Atomic vector of bools crash its instruction selection (an aligned typedef
                // of one's too, which the check above lets through).
                const uint32_t atomic_value = resolved_kind == BUSTER_TYPE_KIND_ATOMIC ? abi_resolved_index(table, table->types.base[abi_resolved_index(table, type)]) : 0;
                const bool atomic_bits = atomic_value != 0 && table->types.kind[atomic_value] == BUSTER_TYPE_KIND_EXT_VECTOR &&
                                         abi_test_packed_bits(table, atomic_value) == 1;
                // A record SSE, SSE whose LLVM type may start with a __float128, and a union of a vector LLVM packs into
                // fewer bits than its bytes (abi_test_holds).
                const bool quad_pair = !gcc && (holds[index] & BUSTER_ABI_TEST_HOLDS_QUAD) != 0 && classes == BUSTER_ABI_CLASSES(BUSTER_ABI_SSE, BUSTER_ABI_SSE);
                const bool union_packs = !gcc && (holds[index] & BUSTER_ABI_TEST_HOLDS_UNION_PACKS) != 0;
                if ((spelled || table->types.kind[index] == BUSTER_TYPE_KIND_ENUM) && small && abi_test_passable(&abi, table, type) && !atomic_result && !gcc_differs &&
                    !clang_crashes && !unit_decides && !odd_vector_record && !atomic_bits && !quad_pair && !union_packs && (!compiled || abi_test_compiled_passes(table, type)))
                {
                    types[type_count] = type;
                    type_count += 1;
                    if (abi_test_unnamed_ok(&abi, table, type, frames) && !(gcc && abi_test_gcc_unnamed_differs(&abi, table, type)))
                    {
                        unnamed_types[unnamed_count] = type;
                        unnamed_count += 1;
                    }
                }
            }
            uint64_t random = seed * 0x9E3779B97F4A7C15ull + vector_bytes;
            result = abi_test_cross_signatures(&cross, types, type_count, unnamed_types, unnamed_count, directed, directed_count < count ? directed_count : count, &random,
                                               stack);
            Arena text_arena = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
            TypeTestText text = {
                .arena = &text_arena,
                .valid = true,
            };
            bool variadic_empty = false;
            abi_test_write_c(&cross, &text, &variadic_empty);
            char source[BUSTER_ABI_TEST_PATH_BYTES];
            char ours[BUSTER_ABI_TEST_PATH_BYTES];
            char object[BUSTER_ABI_TEST_PATH_BYTES];
            char scalar_object[BUSTER_ABI_TEST_PATH_BYTES];
            char program[BUSTER_ABI_TEST_PATH_BYTES];
            char output[BUSTER_ABI_TEST_PATH_BYTES];
            char errors[BUSTER_ABI_TEST_PATH_BYTES];
            const char* name = run.name;
            snprintf(source, sizeof(source), "%s/%s_%u.c", directory, run.file, vector_bytes);
            snprintf(ours, sizeof(ours), "%s/%s_%u_buster.c", directory, run.file, vector_bytes);
            snprintf(object, sizeof(object), "%s/%s_%u_buster.o", directory, run.file, vector_bytes);
            snprintf(scalar_object, sizeof(scalar_object), "%s/%s_%u_buster_scalar.o", directory, run.file, vector_bytes);
            snprintf(program, sizeof(program), "%s/%s_%u", directory, run.file, vector_bytes);
            snprintf(output, sizeof(output), "%s/%s_%u.out", directory, run.file, vector_bytes);
            snprintf(errors, sizeof(errors), "%s/%s_%u.err", directory, run.file, vector_bytes);
            result = result && text.valid && type_test_write_file(source, &text);
            abi_test_expect(tally, result, "cross-check", "the C file can't be written");
            bool written = false;
            if (result && compiled)
            {
                // Our side's C, compiled by this compiler into an object, the vector paths' records checked as they go.
                TypeTestText our_text = {
                    .arena = &text_arena,
                    .valid = true,
                };
                abi_test_write_ours(&cross, &our_text);
                written = our_text.valid && type_test_write_file(ours, &our_text);
                char* build[] = {
                    (char*)buster,
                    "--verify",
                    "-std=gnu23",
                    "-w",
                    "-c",
                    "-o",
                    object,
                    ours,
                    0,
                };
                const int built = written ? type_test_run(build, output, errors) : -1;
                const String8 complaints = type_test_read_file(&world.types.arena, errors);
                written = built == 0;
                snprintf(detail, sizeof(detail), "%s, %u-byte vectors: this compiler can't compile our side (status %d): %.300s", name, vector_bytes, built,
                         complaints.length != 0 ? complaints.pointer : "");
                abi_test_expect(tally, written, "cross-check", detail);
                if (written)
                {
                    // And by the scalar paths (the producer's and Tier 0's twins), into the same bytes.
                    char* scalar_build[] = {
                        (char*)buster,
                        "--verify",
                        "--scalar",
                        "-std=gnu23",
                        "-w",
                        "-c",
                        "-o",
                        scalar_object,
                        ours,
                        0,
                    };
                    const int scalar_built = type_test_run(scalar_build, output, errors);
                    const String8 vector_bytes_object = type_test_read_file(&world.types.arena, object);
                    const String8 scalar_bytes_object = type_test_read_file(&world.types.arena, scalar_object);
                    written = scalar_built == 0 && vector_bytes_object.length != 0 && vector_bytes_object.length == scalar_bytes_object.length &&
                              memcmp(vector_bytes_object.pointer, scalar_bytes_object.pointer, vector_bytes_object.length) == 0;
                    snprintf(detail, sizeof(detail), "%s, %u-byte vectors: the scalar paths' object (status %d) isn't the vector paths' bytes", name, vector_bytes,
                             scalar_built);
                    abi_test_expect(tally, written, "cross-check", detail);
                }
            }
            else if (result)
            {
                written = abi_test_write_object(&cross, object, code);
                snprintf(detail, sizeof(detail), "%s, %u-byte vectors: our object can't be built", name, vector_bytes);
                abi_test_expect(tally, written, "cross-check", detail);
            }
            if (written)
            {
                const char* march = vector_bytes == 16 ? "-march=x86-64" : vector_bytes == 32 ? "-march=x86-64-v3" : "-march=x86-64-v4";
                char* compile[] = {
                    (char*)compiler,
                    "-std=gnu23",
                    "-O1",
                    "-fno-strict-aliasing",
                    (char*)march,
                    "-w",
                    "-o",
                    program,
                    source,
                    object,
                    "-latomic",
                    0,
                };
                const int compiled = type_test_run(compile, output, errors);
                const String8 complaints = type_test_read_file(&world.types.arena, errors);
                snprintf(detail, sizeof(detail), "%s, %u-byte vectors: compiling and linking failed: %.300s", name, vector_bytes, complaints.length != 0 ? complaints.pointer : "");
                if (abi_test_expect(tally, compiled == 0, "cross-check", detail))
                {
                    char* run[] = {
                        program,
                        0,
                    };
                    const int ran = type_test_run(run, output, errors);
                    const String8 printed = type_test_read_file(&world.types.arena, output);
                    const char* summary = printed.length != 0 ? strstr(printed.pointer, "checks ") : 0;
                    snprintf(detail, sizeof(detail), "%s, %u-byte vectors: the program failed (status %d):\n%.4000s", name, vector_bytes, ran,
                             printed.length != 0 ? printed.pointer : "");
                    if (abi_test_expect(tally, ran == 0 && summary != 0, "cross-check", detail))
                    {
                        printf("%-34s %s, %u-byte vectors: %u signatures, %u arguments, %.*s", "cross-check", name, vector_bytes, count, cross.argument_count,
                               (int)(printed.pointer + printed.length - summary), summary);
                    }
                }
            }
            if (!keep && tally->failures == 0)
            {
                unlink(source);
                unlink(ours);
                unlink(object);
                unlink(scalar_object);
                unlink(program);
                unlink(output);
                unlink(errors);
            }
            arena_release(&text_arena);
        }
        free(frames);
        free(stack);
        free(types);
        free(unnamed_types);
        free(empty_arrays);
        free(bit_field_bytes);
        free(holds);
        free(cross.signatures);
        free(cross.arguments);
        free(cross.offsets);
        free(cross.locations);
        free(cross.calls);
        free(cross.data);
        free(cross.data_mask);
        free(cross.data_begin);
        free(cross.results);
        free(cross.result_mask);
        free(cross.result_begin);
        free(code);
        arena_release(&arena);
        abi_test_world_close(&world);
    }
    return result;
}

// The extension runs (#475's review): plain char with each signedness (TypeTable.char_unsigned, -funsigned-char), enums
// of a fixed underlying char and signed char, the other character types, short, unsigned short and bool, passed and
// returned between clang's code and ours, both ways, at values with bits set past the type's. Records check the
// module's flags against clang's code: a callee that returns the 32 bits its argument arrives in, which must be the
// extension the flags say clang's caller makes; a caller that extends as the flags say before calling clang's callee,
// which takes the 32 bits as they come (`int f(char c) { return c; }` is a move); and, the control, one that extends the
// other way, whose result must show it. C that this compiler compiles (the vector and the scalar paths into the same
// bytes) checks Tier 0's code the same ways. clang's callee returns its result unextended, though its IR says signext or
// zeroext (LLVM's x86-64 code ignores them on a result, and GCC's has no extension either): a caller extends what it
// gets back itself, which results with bits set past the type's check, and the run counts the results clang returned
// unextended.
typedef struct AbiTestExtension AbiTestExtension;
struct AbiTestExtension
{
    const char* name;
    const char* spelling;
    // The type's bits in its register: 8, 16, or 1 for bool.
    uint32_t bits;
};

static const AbiTestExtension abi_test_extensions[] = {
    { "char", "char", 8 },
    { "schar", "signed char", 8 },
    { "uchar", "unsigned char", 8 },
    { "char_enum", "enum abi_char_enum", 8 },
    { "schar_enum", "enum abi_schar_enum", 8 },
    { "short", "short", 16 },
    { "ushort", "unsigned short", 16 },
    { "bool", "bool", 1 },
};

#define BUSTER_ABI_TEST_EXTENSIONS BUSTER_ARRAY_LENGTH(abi_test_extensions)

// The records' callee, callers and result reader for each type, against clang's c_take_ and c_give_.
static bool abi_test_extension_object(const char* path, const uint8_t* flags, AbiTestCode* code)
{
    const LinkCapacity capacity = {
        .names = 8 * BUSTER_ABI_TEST_EXTENSIONS + 16,
        .atoms = 4 * BUSTER_ABI_TEST_EXTENSIONS + 2,
        .symbols = 8 * BUSTER_ABI_TEST_EXTENSIONS + 16,
        .fixups = 4 * BUSTER_ABI_TEST_EXTENSIONS + 16,
        .wide_addends = 2,
        .priorities = 2,
    };
    Arena tables = arena_reserve(link_capacity_bytes(capacity));
    Arena names = arena_reserve(64ull * capacity.names + 4096);
    Arena bytes = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    Arena objects = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
    Link link;
    bool result = tables.base != 0 && names.base != 0 && bytes.base != 0 && objects.base != 0 && link_initialize(&link, &tables, &names, capacity);
    for (uint32_t t = 0; result && t < BUSTER_ABI_TEST_EXTENSIONS; t += 1)
    {
        const uint8_t external = BUSTER_LINK_SYMBOL_NOTYPE | BUSTER_LINK_SYMBOL_GLOBAL;
        const uint8_t function = BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL;
        const char* prefixes[6] = {
            "c_take_",
            "c_give_",
            "buster_raw_take_",
            "buster_raw_call_",
            "buster_raw_wrong_",
            "buster_raw_result_",
        };
        SymbolId symbols[6] = { 0 };
        for (uint32_t s = 0; s < 6; s += 1)
        {
            char name[64];
            const int length = snprintf(name, sizeof(name), "%s%s", prefixes[s], abi_test_extensions[t].name);
            const NameId id = link_add_name(&link, (const uint8_t*)name, (uint32_t)length);
            symbols[s] = id != 0 ? link_add_symbol(&link, id, 0, 0, 0, s < 2 ? external : function, s < 2 ? BUSTER_LINK_SOURCE_NONE : BUSTER_LINK_SOURCE_PROGRAM) : 0;
            result = result && symbols[s] != 0;
        }
        // The callee: the 32 bits its argument arrived in.
        code->count = 0;
        code->reference_count = 0;
        code->overflow = false;
        abi_test_add(code, link_program_registers(X86_FORM_MOV_R32_RM32, X86_REGISTER_RAX, X86_REGISTER_RDI));
        abi_test_add(code, link_program_bare(X86_FORM_RET));
        result = result && abi_test_finish(&link, &bytes, code, symbols[2]);
        // The callers: the value's bits extended as the flags say, and the other way, then clang's callee.
        for (uint32_t wrong = 0; wrong < 2; wrong += 1)
        {
            const bool sign = (flags[t] == BUSTER_ABI_FLAG_SIGN) != (wrong != 0);
            const bool word = abi_test_extensions[t].bits == 16;
            const uint32_t form = sign ? (word ? X86_FORM_MOVSX_R32_RM16 : X86_FORM_MOVSX_R32_RM8) : word ? X86_FORM_MOVZX_R32_RM16 : X86_FORM_MOVZX_R32_RM8;
            code->count = 0;
            code->reference_count = 0;
            code->overflow = false;
            abi_test_add(code, link_program_registers(form, X86_REGISTER_RDI, X86_REGISTER_RDI));
            abi_test_add(code, link_program_registers(X86_FORM_PUSH_R64, 0, X86_REGISTER_RAX));
            abi_test_call(code, symbols[0]);
            abi_test_add(code, link_program_registers(X86_FORM_POP_R64, 0, X86_REGISTER_RCX));
            abi_test_add(code, link_program_bare(X86_FORM_RET));
            result = result && abi_test_finish(&link, &bytes, code, symbols[3 + wrong]);
        }
        // The result reader: clang's result's 32 bits as they come back.
        code->count = 0;
        code->reference_count = 0;
        code->overflow = false;
        abi_test_add(code, link_program_registers(X86_FORM_PUSH_R64, 0, X86_REGISTER_RAX));
        abi_test_call(code, symbols[1]);
        abi_test_add(code, link_program_registers(X86_FORM_POP_R64, 0, X86_REGISTER_RCX));
        abi_test_add(code, link_program_bare(X86_FORM_RET));
        result = result && abi_test_finish(&link, &bytes, code, symbols[5]);
    }
    ObjectUnits units;
    ObjectSymbols object_symbols;
    result = result && link_check(&link) == LINK_ERROR_NONE && object_prepare(&units, &link, 1, &objects) && object_map_symbols(&object_symbols, &link, &objects);
    if (result)
    {
        const ObjectOptions options = {
            .function_sections = false,
        };
        const Object object = object_build(&link, &units, &object_symbols, 0, options, BUSTER_S8("calling_convention.c"), &objects);
        result = object.error == OBJECT_ERROR_NONE && object_write_file(path, object.bytes, object.size) == OBJECT_ERROR_NONE;
    }
    arena_release(&tables);
    arena_release(&names);
    arena_release(&bytes);
    arena_release(&objects);
    return result;
}

// The run's C files: clang's side (its callee and result of each type, the checks and main) and ours (Tier 0's callee,
// result and callers of clang's), and the C extension each type's flags give its bits, and the other one.
static void abi_test_extension_c(TypeTestText* text, TypeTestText* ours, const uint8_t* flags)
{
    const char* enums = "enum abi_char_enum : char { ABI_CHAR_ENUM };\nenum abi_schar_enum : signed char { ABI_SCHAR_ENUM };\n";
    type_test_format(text, "#include <stdio.h>\n%s", enums);
    type_test_format(ours, "%s", enums);
    type_test_format(text, "static int abi_checks;\nstatic int abi_failures;\n");
    type_test_format(text, "static void abi_extension_check(int good, const char* what, const char* type, long value, long got, long want)\n{\n");
    type_test_format(text, "    abi_checks += 1;\n    if (!good && abi_failures < 40)\n    {\n");
    type_test_format(text, "        printf(\"FAIL %%s, %%s, value %%#lx: got %%#lx, want %%#lx\\n\", what, type, value, got, want);\n    }\n");
    type_test_format(text, "    abi_failures += !good;\n}\n");
    for (uint32_t t = 0; t < BUSTER_ABI_TEST_EXTENSIONS; t += 1)
    {
        const char* n = abi_test_extensions[t].name;
        const char* s = abi_test_extensions[t].spelling;
        const char* bits = abi_test_extensions[t].bits == 16 ? "short" : "char";
        const bool sign = flags[t] == BUSTER_ABI_FLAG_SIGN;
        type_test_format(text, "\nint c_take_%s(%s x)\n{\n    return x;\n}\n%s c_give_%s(long v)\n{\n    return (%s)v;\n}\n", n, s, s, n, s);
        type_test_format(text, "unsigned buster_raw_take_%s(%s x);\nint buster_raw_call_%s(long v);\nint buster_raw_wrong_%s(long v);\n", n, s, n, n);
        type_test_format(text, "unsigned buster_raw_result_%s(long v);\nint buster_take_%s(%s x);\n%s buster_give_%s(long v);\n", n, n, s, s, n);
        type_test_format(text, "int buster_call_take_%s(long v);\nint buster_call_give_%s(long v);\n", n, n);
        type_test_format(text, "static unsigned abi_flags_%s(long v)\n{\n    return (unsigned)(int)(%s %s)(unsigned %s)v;\n}\n", n, sign ? "signed" : "unsigned", bits, bits);
        type_test_format(text, "static unsigned abi_other_%s(long v)\n{\n    return (unsigned)(int)(%s %s)(unsigned %s)v;\n}\n", n, sign ? "unsigned" : "signed", bits, bits);
        type_test_format(ours, "\nint c_take_%s(%s x);\n%s c_give_%s(long v);\n", n, s, s, n);
        type_test_format(ours, "int buster_take_%s(%s x)\n{\n    return x;\n}\n%s buster_give_%s(long v)\n{\n    return (%s)v;\n}\n", n, s, s, n, s);
        type_test_format(ours, "int buster_call_take_%s(long v)\n{\n    return c_take_%s((%s)v);\n}\n", n, n, s);
        type_test_format(ours, "int buster_call_give_%s(long v)\n{\n    return c_give_%s(v);\n}\n", n, n);
    }
    type_test_format(text, "\nstatic const long abi_values[] = { 0, 1, 0x7f, 0x80, 0xff, 0x100, 0x17f, 0x1ff, 0x7fff, 0x8000, 0xffff, 0x10000, 0x18000, -1, -0x80, -0x81, 0x123456789 };\n");
    type_test_format(text, "\nint main(void)\n{\n    unsigned long unextended = 0;\n");
    type_test_format(text, "    for (unsigned long i = 0; i < sizeof(abi_values) / sizeof(abi_values[0]); i += 1)\n    {\n        const long v = abi_values[i];\n");
    for (uint32_t t = 0; t < BUSTER_ABI_TEST_EXTENSIONS; t += 1)
    {
        const char* n = abi_test_extensions[t].name;
        const char* s = abi_test_extensions[t].spelling;
        type_test_format(text, "        {\n            const %s x = (%s)v;\n            const int want = x;\n            const long value = x;\n", s, s);
        type_test_format(text, "            const unsigned arrived = buster_raw_take_%s(x);\n", n);
        type_test_format(text, "            abi_extension_check(arrived == abi_flags_%s(value), \"clang's caller extends as the flags say\", \"%s\", v, arrived, abi_flags_%s(value));\n", n, s, n);
        type_test_format(text, "            const int taken = buster_raw_call_%s(value);\n", n);
        type_test_format(text, "            abi_extension_check(taken == want, \"clang's callee takes the flags' extension\", \"%s\", v, taken, want);\n", s);
        type_test_format(text, "            if (abi_flags_%s(value) != abi_other_%s(value))\n            {\n                const int wrong = buster_raw_wrong_%s(value);\n", n, n, n);
        type_test_format(text, "                abi_extension_check(wrong != want, \"the other extension shows in clang's callee\", \"%s\", v, wrong, want);\n            }\n", s);
        // A bool's result is its low byte alone (the psABI's bit 0, bits 1 to 7 zero), not its register's truth.
        const char* low = abi_test_extensions[t].bits == 1 ? "unsigned char" : s;
        type_test_format(text, "            const unsigned returned = buster_raw_result_%s(v);\n", n);
        type_test_format(text, "            abi_extension_check((%s)returned == (%s)x, \"clang's result's bits\", \"%s\", v, returned, (unsigned)want);\n", low, low, s);
        type_test_format(text, "            unextended += returned != abi_flags_%s(value);\n", n);
        type_test_format(text, "            const int ours = buster_take_%s(x);\n", n);
        type_test_format(text, "            abi_extension_check(ours == want, \"our callee\", \"%s\", v, ours, want);\n", s);
        type_test_format(text, "            const int given = buster_give_%s(v);\n", n);
        type_test_format(text, "            abi_extension_check(given == want, \"our result\", \"%s\", v, given, want);\n", s);
        type_test_format(text, "            const int called = buster_call_take_%s(v);\n", n);
        type_test_format(text, "            abi_extension_check(called == want, \"our caller extends an argument\", \"%s\", v, called, want);\n", s);
        type_test_format(text, "            const int extended = buster_call_give_%s(v);\n", n);
        type_test_format(text, "            abi_extension_check(extended == want, \"our caller extends a result\", \"%s\", v, extended, want);\n        }\n", s);
    }
    type_test_format(text, "    }\n    printf(\"checks %%d failures %%d, %%lu of clang's results unextended\\n\", abi_checks, abi_failures, unextended);\n");
    type_test_format(text, "    return abi_failures != 0;\n}\n");
}

// An extension run: the module's flags for each type at a table's signedness, the C files, our side's C compiled by
// `buster` (by both paths) and the records' object, linked by clang's driver with clang's side, and run.
static bool abi_test_cross_extension(AbiTestTally* tally, const char* compiler, AbiTestCrossRun run, const char* buster, const char* directory, bool keep)
{
    char detail[512];
    bool result = false;
    AbiTestTables tables;
    if (abi_test_expect(tally, abi_test_tables_open(&tables, 64), "cross-check", "the extension run's tables can't be made"))
    {
        TypeTable* table = &tables.types.table;
        table->char_unsigned = run.char_unsigned;
        const TypeId char_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 1);
        const TypeId schar_enum = type_tag(table, BUSTER_TYPE_KIND_ENUM, 2);
        const bool complete = char_enum != 0 && schar_enum != 0 && type_enum_complete(table, char_enum, BUSTER_TYPE_CHAR, 0) &&
                              type_enum_complete(table, schar_enum, BUSTER_TYPE_SCHAR, 0);
        const TypeId types[BUSTER_ABI_TEST_EXTENSIONS] = {
            BUSTER_TYPE_CHAR,
            BUSTER_TYPE_SCHAR,
            BUSTER_TYPE_UCHAR,
            char_enum,
            schar_enum,
            BUSTER_TYPE_SHORT,
            BUSTER_TYPE_USHORT,
            BUSTER_TYPE_BOOL,
        };
        uint8_t flags[BUSTER_ABI_TEST_EXTENSIONS] = { 0 };
        result = complete && abi_test_tables_start(&tables, 16);
        if (result)
        {
            // Each type the argument and the result of a call of its own: its location's flags, which a caller reads.
            abi_classify_all(&tables.abi, table);
            uint32_t counts[BUSTER_ABI_TEST_EXTENSIONS];
            uint8_t variadic[BUSTER_ABI_TEST_EXTENSIONS] = { 0 };
            for (uint32_t t = 0; t < BUSTER_ABI_TEST_EXTENSIONS; t += 1)
            {
                counts[t] = 1;
            }
            const AbiSignatures signatures = {
                .results = types,
                .counts = counts,
                .named = counts,
                .variadic = variadic,
                .count = BUSTER_ABI_TEST_EXTENSIONS,
                .arguments = types,
            };
            AbiLocation locations[BUSTER_ABI_TEST_EXTENSIONS];
            AbiCall calls[BUSTER_ABI_TEST_EXTENSIONS];
            abi_assign(&tables.abi, signatures, locations, calls);
            for (uint32_t t = 0; t < BUSTER_ABI_TEST_EXTENSIONS; t += 1)
            {
                flags[t] = locations[t].flags & (BUSTER_ABI_FLAG_SIGN | BUSTER_ABI_FLAG_ZERO);
                result = result && locations[t].kind == BUSTER_ABI_LOCATION_REGISTERS && locations[t].registers[0] == BUSTER_ABI_RDI &&
                         (flags[t] == BUSTER_ABI_FLAG_SIGN || flags[t] == BUSTER_ABI_FLAG_ZERO);
            }
        }
        snprintf(detail, sizeof(detail), "%s: every type must be an extended argument in rdi", run.name);
        abi_test_expect(tally, result, "cross-check", detail);
        // Each text grows in an arena of its own; the files' complaints and bytes are read into the first.
        Arena text_arena = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
        Arena our_arena = arena_reserve(BUSTER_ABI_TEST_ARENA_BYTES);
        TypeTestText text = {
            .arena = &text_arena,
            .valid = true,
        };
        TypeTestText our_text = {
            .arena = &our_arena,
            .valid = true,
        };
        char source[BUSTER_ABI_TEST_PATH_BYTES];
        char ours[BUSTER_ABI_TEST_PATH_BYTES];
        char object[BUSTER_ABI_TEST_PATH_BYTES];
        char scalar_object[BUSTER_ABI_TEST_PATH_BYTES];
        char records[BUSTER_ABI_TEST_PATH_BYTES];
        char program[BUSTER_ABI_TEST_PATH_BYTES];
        char output[BUSTER_ABI_TEST_PATH_BYTES];
        char errors[BUSTER_ABI_TEST_PATH_BYTES];
        snprintf(source, sizeof(source), "%s/%s_%u.c", directory, run.file, run.vector_bytes);
        snprintf(ours, sizeof(ours), "%s/%s_%u_buster.c", directory, run.file, run.vector_bytes);
        snprintf(object, sizeof(object), "%s/%s_%u_buster.o", directory, run.file, run.vector_bytes);
        snprintf(scalar_object, sizeof(scalar_object), "%s/%s_%u_buster_scalar.o", directory, run.file, run.vector_bytes);
        snprintf(records, sizeof(records), "%s/%s_%u_records.o", directory, run.file, run.vector_bytes);
        snprintf(program, sizeof(program), "%s/%s_%u", directory, run.file, run.vector_bytes);
        snprintf(output, sizeof(output), "%s/%s_%u.out", directory, run.file, run.vector_bytes);
        snprintf(errors, sizeof(errors), "%s/%s_%u.err", directory, run.file, run.vector_bytes);
        if (result)
        {
            abi_test_extension_c(&text, &our_text, flags);
            result = text.valid && our_text.valid && type_test_write_file(source, &text) && type_test_write_file(ours, &our_text);
            snprintf(detail, sizeof(detail), "%s: the C files can't be written", run.name);
            abi_test_expect(tally, result, "cross-check", detail);
        }
        char* signedness = run.char_unsigned ? "-funsigned-char" : "-fsigned-char";
        if (result)
        {
            // Our side's C, by the vector paths and by the scalar ones, into the same bytes.
            char* build[] = {
                (char*)buster,
                "--verify",
                "-std=gnu23",
                "-w",
                signedness,
                "-c",
                "-o",
                object,
                ours,
                0,
            };
            const int built = type_test_run(build, output, errors);
            const String8 complaints = type_test_read_file(&text_arena, errors);
            result = built == 0;
            snprintf(detail, sizeof(detail), "%s: this compiler can't compile our side (status %d): %.300s", run.name, built, complaints.length != 0 ? complaints.pointer : "");
            abi_test_expect(tally, result, "cross-check", detail);
        }
        if (result)
        {
            char* scalar_build[] = {
                (char*)buster,
                "--verify",
                "--scalar",
                "-std=gnu23",
                "-w",
                signedness,
                "-c",
                "-o",
                scalar_object,
                ours,
                0,
            };
            const int scalar_built = type_test_run(scalar_build, output, errors);
            const String8 vector_bytes_object = type_test_read_file(&text_arena, object);
            const String8 scalar_bytes_object = type_test_read_file(&text_arena, scalar_object);
            result = scalar_built == 0 && vector_bytes_object.length != 0 && vector_bytes_object.length == scalar_bytes_object.length &&
                     memcmp(vector_bytes_object.pointer, scalar_bytes_object.pointer, vector_bytes_object.length) == 0;
            snprintf(detail, sizeof(detail), "%s: the scalar paths' object (status %d) isn't the vector paths' bytes", run.name, scalar_built);
            abi_test_expect(tally, result, "cross-check", detail);
        }
        AbiTestCode* code = result ? malloc(sizeof(AbiTestCode)) : 0;
        if (result)
        {
            result = code != 0 && abi_test_extension_object(records, flags, code);
            snprintf(detail, sizeof(detail), "%s: the records' object can't be built", run.name);
            abi_test_expect(tally, result, "cross-check", detail);
        }
        free(code);
        if (result)
        {
            char* compile[] = {
                (char*)compiler,
                "-std=gnu23",
                "-O1",
                "-fno-strict-aliasing",
                "-march=x86-64",
                signedness,
                "-w",
                "-o",
                program,
                source,
                object,
                records,
                0,
            };
            const int linked = type_test_run(compile, output, errors);
            const String8 complaints = type_test_read_file(&text_arena, errors);
            result = linked == 0;
            snprintf(detail, sizeof(detail), "%s: compiling and linking failed: %.300s", run.name, complaints.length != 0 ? complaints.pointer : "");
            abi_test_expect(tally, result, "cross-check", detail);
        }
        if (result)
        {
            char* arguments[] = {
                program,
                0,
            };
            const int ran = type_test_run(arguments, output, errors);
            const String8 printed = type_test_read_file(&text_arena, output);
            const char* summary = printed.length != 0 ? strstr(printed.pointer, "checks ") : 0;
            result = ran == 0 && summary != 0;
            snprintf(detail, sizeof(detail), "%s: the program failed (status %d):\n%.4000s", run.name, ran, printed.length != 0 ? printed.pointer : "");
            if (abi_test_expect(tally, result, "cross-check", detail))
            {
                printf("%-34s %s: %u types, %.*s", "cross-check", run.name, (uint32_t)BUSTER_ABI_TEST_EXTENSIONS, (int)(printed.pointer + printed.length - summary), summary);
            }
        }
        if (!keep && tally->failures == 0)
        {
            unlink(source);
            unlink(ours);
            unlink(object);
            unlink(scalar_object);
            unlink(records);
            unlink(program);
            unlink(output);
            unlink(errors);
        }
        arena_release(&text_arena);
        arena_release(&our_arena);
        abi_test_tables_close(&tables);
    }
    return result;
}

// A capture must never occupy a standard descriptor: replay would otherwise write back into the capture when the
// caller closed stdout or stderr, and the child's dup2 calls could overwrite the other capture.
static int abi_test_cross_capture(const char* name)
{
    int descriptor = (int)syscall(SYS_memfd_create, name, MFD_CLOEXEC);
    if (descriptor >= 0 && descriptor <= STDERR_FILENO)
    {
        const int original = descriptor;
        descriptor = fcntl(original, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        close(original);
    }
    return descriptor;
}

// Starts run `index` (abi_test_cross_run's), our side compiled by `buster` in a compiled world, in a child process,
// whose tally goes to `tally`, in memory it shares with the parent. The parent must have flushed its stdout and stderr,
// which the child would otherwise print again.
static AbiTestCrossRun abi_test_cross_start(uint32_t index, AbiTestTally* tally, uint64_t seed, uint32_t count, const char* clang, const char* gcc, const char* buster,
                                            const char* directory, bool keep)
{
    AbiTestCrossRun run = abi_test_cross_run(index);
    run.output = abi_test_cross_capture("cross-check output");
    run.errors = abi_test_cross_capture("cross-check errors");
    if (run.output >= 0 && run.errors >= 0)
    {
        run.child = fork();
    }
    if (run.child == 0)
    {
        const bool redirected = dup2(run.output, STDOUT_FILENO) == STDOUT_FILENO && dup2(run.errors, STDERR_FILENO) == STDERR_FILENO;
        if (redirected && run.extension)
        {
            abi_test_cross_extension(tally, clang, run, buster, directory, keep);
        }
        else if (redirected)
        {
            abi_test_cross(tally, seed + run.vector_bytes + (run.compiled ? 7 : 0), count, run.gcc ? gcc : clang, run, buster, directory, keep);
        }
        fflush(stdout);
        fflush(stderr);
        _exit(redirected ? 0 : 1);
    }
    return run;
}

// Writes what `descriptor` holds, from its start, to `stream`.
static void abi_test_cross_print(int descriptor, FILE* stream)
{
    char buffer[4096];
    off_t offset = 0;
    ssize_t got = descriptor >= 0 ? pread(descriptor, buffer, sizeof(buffer), offset) : 0;
    while (got > 0)
    {
        fwrite(buffer, 1, (size_t)got, stream);
        offset += got;
        got = pread(descriptor, buffer, sizeof(buffer), offset);
    }
}

// Waits for a run's child, prints what it wrote (its errors on stderr, then its output on stdout), and adds the tally it
// left in `shared` to `tally`. A run whose child couldn't start or didn't end normally is one failed check.
static void abi_test_cross_finish(AbiTestTally* tally, AbiTestCrossRun run, const AbiTestTally* shared)
{
    int status = 0;
    const bool waited = run.child > 0 && waitpid(run.child, &status, 0) == run.child;
    abi_test_cross_print(run.errors, stderr);
    abi_test_cross_print(run.output, stdout);
    if (waited && WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        tally->checks += shared->checks;
        tally->failures += shared->failures;
    }
    else
    {
        char detail[128];
        if (!waited)
        {
            snprintf(detail, sizeof(detail), "%s, %u-byte vectors: the run's process can't start", run.name, run.vector_bytes);
        }
        else if (WIFSIGNALED(status))
        {
            snprintf(detail, sizeof(detail), "%s, %u-byte vectors: the run's process ended by signal %d", run.name, run.vector_bytes, WTERMSIG(status));
        }
        else
        {
            snprintf(detail, sizeof(detail), "%s, %u-byte vectors: the run's process exited with status %d", run.name, run.vector_bytes, WEXITSTATUS(status));
        }
        abi_test_expect(tally, false, "cross-check", detail);
    }
    if (run.output >= 0)
    {
        close(run.output);
    }
    if (run.errors >= 0)
    {
        close(run.errors);
    }
}

// The ten runs are independent, each with a world and files of its own, and each takes about its compiler's time, so
// they run at once, a child process each. The parent prints each one's output and adds its tally in the runs' order, so
// the lines and counts are those of the runs one after another; a run that fails keeps its files.
static bool abi_test_cross_check(uint64_t seed, uint32_t count, const char* clang, const char* gcc, const char* buster, bool keep)
{
    AbiTestTally tally = { 0 };
    char directory[BUSTER_ABI_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    snprintf(directory, sizeof(directory), "%s/buster-calling-convention-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    const uint64_t shared_bytes = sizeof(AbiTestTally) * BUSTER_ABI_TEST_CROSS_RUNS;
    AbiTestTally* shared = mmap(0, shared_bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (abi_test_expect(&tally, (x86_encoder_initialized || x86_encoder_initialize()) && shared != MAP_FAILED && mkdtemp(directory) != 0, "cross-check", "can't start"))
    {
        AbiTestCrossRun runs[BUSTER_ABI_TEST_CROSS_RUNS];
        fflush(stdout);
        fflush(stderr);
        for (uint32_t r = 0; r < BUSTER_ABI_TEST_CROSS_RUNS; r += 1)
        {
            runs[r] = abi_test_cross_start(r, &shared[r], seed, count, clang, gcc, buster, directory, keep);
        }
        for (uint32_t r = 0; r < BUSTER_ABI_TEST_CROSS_RUNS; r += 1)
        {
            abi_test_cross_finish(&tally, runs[r], &shared[r]);
        }
        if (keep || tally.failures != 0)
        {
            printf("the files are in %s\n", directory);
        }
        else
        {
            rmdir(directory);
        }
    }
    if (shared != MAP_FAILED)
    {
        munmap(shared, shared_bytes);
    }
    abi_test_report("cross-check", tally);
    return tally.failures == 0;
}

// The regression children start with known standard streams, then close the selected ones. Limit both time and file
// size: a replay that writes back into its capture must fail without hanging the tests or filling memory.
static bool abi_test_cross_descriptor_setup(uint32_t closed)
{
    const int null = open("/dev/null", O_RDWR);
    bool ready = null >= 0;
    for (int descriptor = STDIN_FILENO; ready && descriptor <= STDERR_FILENO; descriptor += 1)
    {
        ready = dup2(null, descriptor) == descriptor;
    }
    if (null > STDERR_FILENO)
    {
        close(null);
    }
    for (int descriptor = STDIN_FILENO; ready && descriptor <= STDERR_FILENO; descriptor += 1)
    {
        if ((closed & (1u << descriptor)) != 0)
        {
            ready = close(descriptor) == 0;
        }
    }
    clearerr(stdout);
    clearerr(stderr);
    struct rlimit limit;
    ready = ready && getrlimit(RLIMIT_FSIZE, &limit) == 0;
    if (ready)
    {
        limit.rlim_cur = limit.rlim_cur < (8u << 20) ? limit.rlim_cur : (8u << 20);
        ready = setrlimit(RLIMIT_FSIZE, &limit) == 0;
    }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGALRM);
    sigaddset(&signals, SIGXFSZ);
    ready = ready && signal(SIGALRM, SIG_DFL) != SIG_ERR && signal(SIGXFSZ, SIG_DFL) != SIG_ERR && sigprocmask(SIG_UNBLOCK, &signals, 0) == 0;
    alarm(10);
    return ready;
}

static bool abi_test_cross_standard_descriptors(uint32_t closed)
{
    bool same = true;
    for (int descriptor = STDIN_FILENO; descriptor <= STDERR_FILENO; descriptor += 1)
    {
        const int flags = fcntl(descriptor, F_GETFD);
        const bool expected_closed = (closed & (1u << descriptor)) != 0;
        same = (expected_closed ? flags == -1 && errno == EBADF : flags >= 0) && same;
    }
    return same;
}

// Verify that all ten runs reached the failing compiler, then remove only their known files in this test's private
// TMPDIR. A setup failure also exits 1, but cannot leave all 40 files. The ordinary cross-check keeps its failed runs.
static bool abi_test_cross_descriptor_files(const char* temporary)
{
    DIR* root = opendir(temporary);
    bool complete = root != 0;
    uint32_t directories = 0;
    struct dirent* entry;
    while (root != 0 && (entry = readdir(root)) != 0)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            char directory[BUSTER_ABI_TEST_PATH_BYTES + sizeof(entry->d_name)];
            snprintf(directory, sizeof(directory), "%s/%s", temporary, entry->d_name);
            directories += 1;
            for (uint32_t r = 0; r < BUSTER_ABI_TEST_CROSS_RUNS; r += 1)
            {
                // The C file, our side (C in a compiled world, an object otherwise), and the failing compiler's output.
                const AbiTestCrossRun run = abi_test_cross_run(r);
                const char* suffixes[] = {
                    ".c",
                    run.compiled ? "_buster.c" : "_buster.o",
                    ".out",
                    ".err",
                };
                for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(suffixes); s += 1)
                {
                    char path[sizeof(directory) + 64];
                    snprintf(path, sizeof(path), "%s/%s_%u%s", directory, run.file, run.vector_bytes, suffixes[s]);
                    complete = unlink(path) == 0 && complete;
                }
            }
            complete = rmdir(directory) == 0 && complete;
        }
    }
    if (root != 0)
    {
        complete = closedir(root) == 0 && complete;
    }
    return complete && directories == 1;
}

// Both capture duplications fail with a limit of 3 and closed output streams; with a limit of 4 and open standard
// streams, only the first memfd fits. In either case, finishing must release every capture and count one failed run.
static bool abi_test_cross_descriptor_failure(rlim_t descriptors, const char* directory)
{
    const uint32_t closed = descriptors == 3 ? 6 : 0;
    struct rlimit limit;
    bool ready = abi_test_cross_descriptor_setup(closed) && getrlimit(RLIMIT_NOFILE, &limit) == 0;
    close(STDERR_FILENO + 1);
    if (ready)
    {
        limit.rlim_cur = descriptors;
        ready = setrlimit(RLIMIT_NOFILE, &limit) == 0;
    }
    bool passed = false;
    if (ready)
    {
        AbiTestTally tally = { 0 };
        const AbiTestTally shared = { 0 };
        AbiTestCrossRun run = abi_test_cross_start(0, &tally, 1, 20, "/bin/false", "/bin/false", "/bin/false", directory, false);
        const bool failed = run.child == -1 && run.errors == -1 && (descriptors == 3 ? run.output == -1 : run.output == STDERR_FILENO + 1);
        abi_test_cross_finish(&tally, run, &shared);
        const int flags = fcntl(STDERR_FILENO + 1, F_GETFD);
        passed = failed && flags == -1 && errno == EBADF && tally.checks == 1 && tally.failures == 1 && abi_test_cross_standard_descriptors(closed);
    }
    return passed;
}

static bool abi_test_cross_descriptors(void)
{
    AbiTestTally tally = { 0 };
    char directory[BUSTER_ABI_TEST_PATH_BYTES];
    const char* temporary = getenv("TMPDIR");
    const int length = snprintf(directory, sizeof(directory), "%s/buster-abi-descriptors-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    if (abi_test_expect(&tally, length > 0 && (uint64_t)length < sizeof(directory) && mkdtemp(directory) != 0, "cross-check descriptors", "can't make the test directory"))
    {
        for (uint32_t closed = 0; closed < 8; closed += 1)
        {
            fflush(stdout);
            fflush(stderr);
            const pid_t child = fork();
            if (child == 0)
            {
                if (!abi_test_cross_descriptor_setup(closed) || setenv("TMPDIR", directory, 1) != 0)
                {
                    _exit(127);
                }
                const bool passed = abi_test_cross_check(1, 20, "/bin/false", "/bin/false", "/bin/false", false);
                _exit(abi_test_cross_standard_descriptors(closed) ? (passed ? 0 : 1) : 127);
            }
            int status = 0;
            pid_t waited;
            do
            {
                waited = child > 0 ? waitpid(child, &status, 0) : -1;
            } while (child > 0 && waited < 0 && errno == EINTR);
            char detail[128];
            snprintf(detail, sizeof(detail), "closed mask %u: the failing compiler must exit 1 and leave the standard descriptors unchanged", closed);
            abi_test_expect(&tally, waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 1, "cross-check descriptors", detail);
            abi_test_expect(&tally, abi_test_cross_descriptor_files(directory), "cross-check descriptors", "all ten failed runs must retain their files");
        }
        for (rlim_t descriptors = 3; descriptors <= 4; descriptors += 1)
        {
            fflush(stdout);
            fflush(stderr);
            const pid_t child = fork();
            if (child == 0)
            {
                _exit(abi_test_cross_descriptor_failure(descriptors, directory) ? 0 : 127);
            }
            int status = 0;
            pid_t waited;
            do
            {
                waited = child > 0 ? waitpid(child, &status, 0) : -1;
            } while (child > 0 && waited < 0 && errno == EINTR);
            abi_test_expect(&tally, waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "cross-check descriptors",
                            descriptors == 3 ? "failed duplication must close the original descriptors" : "failed second capture must close the first capture");
        }
        abi_test_expect(&tally, rmdir(directory) == 0, "cross-check descriptors", "the test directory must be empty");
    }
    abi_test_report("cross-check descriptors", tally);
    return tally.failures == 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command

static int run_calling_convention_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    uint64_t count = BUSTER_ABI_TEST_DEFAULT_COUNT;
    uint64_t signatures = BUSTER_ABI_TEST_DEFAULT_SIGNATURES;
    const char* clang = "clang";
    const char* gcc = "gcc";
    // The compiler of our side's C in the compiled runs: this executable, unless --buster names another.
    const char* buster = "/proc/self/exe";
    bool cross_check = false;
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
            valid = valid && type_test_parse_u64(argv[i + 1], &count) && count >= 16 && count <= (1u << 20);
            i += 1;
        }
        else if (strcmp(argv[i], "--signatures") == 0 && i + 1 < argc)
        {
            valid = valid && type_test_parse_u64(argv[i + 1], &signatures) && signatures >= 1 && signatures <= (1u << 16);
            i += 1;
        }
        else if (strcmp(argv[i], "--clang") == 0 && i + 1 < argc)
        {
            clang = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--gcc") == 0 && i + 1 < argc)
        {
            gcc = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--buster") == 0 && i + 1 < argc)
        {
            buster = argv[i + 1];
            i += 1;
        }
        else if (strcmp(argv[i], "--cross-check") == 0)
        {
            cross_check = true;
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
    if (!valid)
    {
        fprintf(stderr, "usage: test-calling-convention [--seed N] [--count N] [--cross-check] [--signatures N] [--clang PATH] [--gcc PATH] [--buster PATH] [--keep]\n");
    }
    else
    {
        bool passed = abi_test_merge();
        passed = abi_test_fixtures() && passed;
        passed = abi_test_worlds(seed, (uint32_t)count) && passed;
        passed = abi_test_assignment(seed, (uint32_t)count) && passed;
        passed = (!cross_check || abi_test_cross_descriptors()) && passed;
        passed = (!cross_check || abi_test_cross_check(seed, (uint32_t)signatures, clang, gcc, buster, keep)) && passed;
        result = passed ? 0 : 1;
    }
    return result;
}
