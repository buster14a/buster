// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's baseline hint identities and memory recipes. Include after c_builtin_lower.c. The source producer checks
// arity/types and evaluates every runtime argument once into the supplied existing local; this stage evaluates no
// source expression. EXPECT's two locals are I64 (target long), pointers and sizes are U64, and memset's value is I32.
// Optional hint integers retain their checked integer machine class. Alignment promises remain producer metadata.
// PREFETCH is a valid hint no-op; x86-64 extract/frob return-address representations and LAUNDER are the pointer identity.
//
// Runtime memcpy uses baseline SSE's 16-byte COPY; moves, fills and comparisons use 8-byte integer words, with exact byte
// tails. Small constant sizes have bounded straight-line recipes. No runtime call or optional output ISA is needed.
// Zero length performs no memory access; memmove selects its backward loop only for an overlapping higher destination.
// memcmp compares equal words then resolves a differing word in byte order. strlen scans aligned 8-byte words after
// eight bounded byte probes: an aligned word cannot cross an x86 page, including the word containing the terminator. It never
// reads before the supplied pointer, and requires an ordinary, nonvolatile terminated string in readable memory.
// All loops preserve the enclosing value stack, use absolute enclosing-stream branch destinations and initialize
// their U64 temporaries before use. Return values have their declared machine classes. No helper allocation occurs.
//
// The bit builtins (cbuiltin_bits) are c_builtin_bits_lower.c's recipes, of their arguments' and result's descriptors,
// and so are the alignment builtins' (cbuiltin_align, cbuiltin_misc_align), a pointer's descriptor its 64 bits.
//
// ALLOCA is Tier 0's ALLOCA of its U64 size, and FRAME_ADDRESS and RETURN_ADDRESS take no arguments: they are level 0's
// (the producer refuses any other level and evaluates none), Tier 0's FRAME_ADDRESS and the word 8 bytes above it, and
// DWARF_CFA takes none, the FRAME_ADDRESS plus 16. Each gives its function a frame pointer.
//
// Publication has the same disjoint-column and transactional capacity contract as cbuiltin_lower, with exact scalar
// and masked vector copy twins. TRAP and UNREACHABLE are explicitly refused: this module cannot invent a terminating
// typed operation.
#ifndef BUSTER_C_BUILTIN_MISC_LOWER_INCLUDED
#define BUSTER_C_BUILTIN_MISC_LOWER_INCLUDED

#define BUSTER_CBUILTIN_MISC_SMALL_BYTES 32u

typedef struct CBuiltinMiscLowerCall CBuiltinMiscLowerCall;
struct CBuiltinMiscLowerCall
{
    CBuiltinId id;
    uint32_t arguments[4];
    uint32_t argument_count;
    uint32_t constant_size;
    bool constant_size_known;
    // A bit builtin's arguments' descriptors and its result's (width | BUSTER_CBUILTIN_SIGNED, BUSTER_CBUILTIN_LOWER_WIDE
    // for 128 bits), a carry's pointer's 0, and a result past 64 bits' frame object.
    uint32_t argument_types[4];
    uint32_t result_type;
    uint32_t result_object;
};

#include "c_builtin_bits_lower.c"

// An alignment builtin (cbuiltin_align) as clang's code computes it, of its descriptors (CBuiltinMiscLowerCall's
// argument_types and result_type, a pointer's 64 bits): the source's bits zero-extended from its width, the alignment's
// zero-extended or truncated to that width, and the mask the alignment less 1 in it, all ones for an alignment that
// truncates to 0. align_up is (x + mask) & ~mask, align_down x & ~mask, each its result's value, and is_aligned
// (x & mask) == 0, a bool's 0 or 1. A source of 65 to 128 bits goes limb by limb, the mask's borrow and the sum's carry
// crossing between them, its result written to its frame object (CBuiltinMiscLowerCall.result_object) and read back.
static void cbuiltin_misc_align(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call)
{
    const uint32_t source = call->argument_types[0];
    const uint32_t alignment = call->argument_types[1];
    const uint32_t width = cbuiltin_bits_width(source);
    const uint32_t alignment_width = cbuiltin_bits_width(alignment);
    const bool aligned = call->id == CBUILTIN_IS_ALIGNED;
    const bool up = call->id == CBUILTIN_ALIGN_UP;
    uint32_t a_low = 0;
    uint32_t a_high = 0;
    if (alignment_width <= 64)
    {
        a_low = cbuiltin_bits_load(recipe, call->arguments[1], alignment);
        a_high = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_set(recipe, a_high);
    }
    else
    {
        cbuiltin_bits_load_wide(recipe, call->arguments[1], alignment_width, false, &a_low, &a_high);
    }
    if (width <= 64)
    {
        const uint32_t x = cbuiltin_bits_load(recipe, call->arguments[0], source);
        const uint32_t mask = a_low;
        cbuiltin_lower_local(recipe, mask);
        cbuiltin_bits_mask(recipe, width);
        cbuiltin_lower_constant(recipe, 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        cbuiltin_lower_set(recipe, mask);
        cbuiltin_lower_local(recipe, x);
        cbuiltin_lower_local(recipe, mask);
        if (aligned)
        {
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            cbuiltin_lower_constant(recipe, 0);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, cbuiltin_lower_type_class(call->result_type), 0);
        }
        else
        {
            if (up)
            {
                cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
                cbuiltin_lower_local(recipe, mask);
            }
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_NOT);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
            cbuiltin_lower_set(recipe, x);
            cbuiltin_bits_result(recipe, x, call->result_type);
        }
    }
    else
    {
        // The alignment truncated to the source's width, then the mask's limbs: the low one less 1, the high one less the
        // low one's borrow.
        uint32_t x_low = 0;
        uint32_t x_high = 0;
        cbuiltin_bits_load_wide(recipe, call->arguments[0], width, false, &x_low, &x_high);
        const uint32_t borrow = cbuiltin_lower_temporary(recipe);
        cbuiltin_lower_local(recipe, a_high);
        cbuiltin_bits_mask(recipe, width - 64);
        cbuiltin_lower_set(recipe, a_high);
        cbuiltin_lower_local(recipe, a_low);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_lower_set(recipe, borrow);
        cbuiltin_lower_local(recipe, a_low);
        cbuiltin_lower_constant(recipe, 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        cbuiltin_lower_set(recipe, a_low);
        cbuiltin_lower_binary(recipe, a_high, BUSTER_TYPED_SUB, a_high, borrow);
        if (aligned)
        {
            cbuiltin_lower_binary(recipe, x_low, BUSTER_TYPED_AND, x_low, a_low);
            cbuiltin_lower_binary(recipe, x_high, BUSTER_TYPED_AND, x_high, a_high);
            cbuiltin_lower_local(recipe, x_low);
            cbuiltin_lower_local(recipe, x_high);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_OR);
            cbuiltin_lower_constant(recipe, 0);
            cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, cbuiltin_lower_type_class(call->result_type), 0);
        }
        else
        {
            if (up)
            {
                // The sum's low limb, and its carry into the high one: the wrapped sum below an addend.
                const uint32_t carry = cbuiltin_lower_temporary(recipe);
                cbuiltin_lower_binary(recipe, x_low, BUSTER_TYPED_ADD, x_low, a_low);
                cbuiltin_lower_binary(recipe, carry, BUSTER_TYPED_LT, x_low, a_low);
                cbuiltin_lower_binary(recipe, x_high, BUSTER_TYPED_ADD, x_high, a_high);
                cbuiltin_lower_binary(recipe, x_high, BUSTER_TYPED_ADD, x_high, carry);
            }
            cbuiltin_bits_invert(recipe, a_low, 64);
            cbuiltin_bits_invert(recipe, a_high, 64);
            cbuiltin_lower_binary(recipe, x_low, BUSTER_TYPED_AND, x_low, a_low);
            cbuiltin_lower_binary(recipe, x_high, BUSTER_TYPED_AND, x_high, a_high);
            cbuiltin_bits_store_wide(recipe, call->result_object, x_low, x_high, call->result_type);
        }
    }
}

static bool cbuiltin_misc_lower_local_class(const CBuiltinLowerBuffer* buffer, uint32_t local, uint32_t class)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] == class;
    return result;
}

// A 128-bit value's frame object (c_check_program_lower.c's check_lower_temporary of I128 or U128): an address-taken
// local without a class of its own, as cbuiltin_lower_slot_valid takes a wide overflow operand's.
static bool cbuiltin_misc_lower_object(const CBuiltinLowerBuffer* buffer, uint32_t local)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] == BUSTER_TYPED_CLASS_VOID &&
                        (buffer->local_flags[local] & BUSTER_TYPED_LOCAL_ADDRESS_TAKEN) != 0;
    return result;
}

static bool cbuiltin_misc_lower_integer(const CBuiltinLowerBuffer* buffer, uint32_t local)
{
    const bool result = local < buffer->local_count && buffer->local_classes[local] >= BUSTER_TYPED_CLASS_I8 && buffer->local_classes[local] <= BUSTER_TYPED_CLASS_U64;
    return result;
}

static void cbuiltin_misc_lower_patch(CBuiltinLowerRecipe* recipe, uint32_t branch, uint32_t first)
{
    if (branch < BUSTER_CBUILTIN_LOWER_ENTRIES)
    {
        recipe->values[branch] = (uint64_t)first + recipe->count;
    }
}

static uint32_t cbuiltin_misc_lower_branch(CBuiltinLowerRecipe* recipe, uint32_t kind)
{
    const uint32_t result = recipe->count;
    cbuiltin_lower_emit(recipe, kind, kind == BUSTER_TYPED_JUMP ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_I32, 0);
    return result;
}

static void cbuiltin_misc_lower_address(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t index)
{
    cbuiltin_lower_local(recipe, pointer);
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
}

static void cbuiltin_misc_lower_byte(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t index, uint32_t output)
{
    cbuiltin_misc_lower_address(recipe, pointer, index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, output);
}

static void cbuiltin_misc_lower_copy_byte(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t index)
{
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    if (call->id == CBUILTIN_MEMSET)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, call->arguments[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else
    {
        cbuiltin_misc_lower_address(recipe, call->arguments[1], index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U8, 0);
}

static void cbuiltin_misc_lower_pattern(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t pattern)
{
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, call->arguments[1]);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_constant(recipe, UINT64_C(0x0101010101010101));
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_MUL);
    cbuiltin_lower_set(recipe, pattern);
}

static void cbuiltin_misc_lower_copy_word(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t index, uint32_t pattern)
{
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    if (call->id == CBUILTIN_MEMSET)
    {
        cbuiltin_lower_local(recipe, pattern);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
    else if (call->id == CBUILTIN_MEMCPY)
    {
        cbuiltin_misc_lower_address(recipe, call->arguments[1], index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, 16);
    }
    else
    {
        // COPY's shared-IR contract requires equal or disjoint ranges. A machine word's LOAD before SET also
        // handles close overlap, without depending on COPY's present instruction selection for one chunk.
        cbuiltin_misc_lower_address(recipe, call->arguments[1], index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_U64, 0);
    }
}

static void cbuiltin_misc_lower_copy_single(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t class, uint32_t pattern)
{
    cbuiltin_lower_local(recipe, call->arguments[0]);
    if (call->id == CBUILTIN_MEMSET && class == BUSTER_TYPED_CLASS_U8)
    {
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_I32, call->arguments[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8, 0);
    }
    else if (call->id == CBUILTIN_MEMSET)
    {
        cbuiltin_lower_local(recipe, pattern);
    }
    else
    {
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, class, 0);
    }
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, class, 0);
}

static void cbuiltin_misc_lower_offset(CBuiltinLowerRecipe* recipe, uint32_t pointer, uint32_t offset)
{
    cbuiltin_lower_local(recipe, pointer);
    if (offset != 0)
    {
        cbuiltin_lower_constant(recipe, offset);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_ADD);
    }
}

// At most four full words, or two overlapping pieces loaded before either store for memmove. The latter keeps
// close overlap safe in either direction, even when the two pieces of one source range overlap each other.
static void cbuiltin_misc_lower_copy_small(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call)
{
    const uint32_t size = call->constant_size;
    if (size != 0 && call->id == CBUILTIN_MEMCPY)
    {
        cbuiltin_lower_local(recipe, call->arguments[0]);
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_COPY, BUSTER_TYPED_CLASS_VOID, size);
    }
    else if (size != 0 && call->id == CBUILTIN_MEMMOVE)
    {
        const uint32_t piece = size >= 8 ? 8 : size >= 4 ? 4 : size >= 2 ? 2 : 1;
        const uint32_t class = piece == 8 ? BUSTER_TYPED_CLASS_U64 : piece == 4 ? BUSTER_TYPED_CLASS_U32 : piece == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
        const uint32_t left = cbuiltin_lower_temporary(recipe);
        const uint32_t right = cbuiltin_lower_temporary(recipe);
        for (uint32_t p = 0; p < 1u + (size > piece); p += 1)
        {
            cbuiltin_misc_lower_offset(recipe, call->arguments[1], p == 0 ? 0 : size - piece);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, class, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_set(recipe, p == 0 ? left : right);
        }
        for (uint32_t p = 0; p < 1u + (size > piece); p += 1)
        {
            cbuiltin_misc_lower_offset(recipe, call->arguments[0], p == 0 ? 0 : size - piece);
            cbuiltin_lower_local(recipe, p == 0 ? left : right);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, class, 0);
            cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, class, 0);
        }
    }
    else if (size != 0)
    {
        const uint32_t pattern = cbuiltin_lower_temporary(recipe);
        cbuiltin_misc_lower_pattern(recipe, call, pattern);
        uint32_t offset = 0;
        for (uint32_t piece = 8; piece != 0; piece /= 2)
        {
            const uint32_t class = piece == 8 ? BUSTER_TYPED_CLASS_U64 : piece == 4 ? BUSTER_TYPED_CLASS_U32 : piece == 2 ? BUSTER_TYPED_CLASS_U16 : BUSTER_TYPED_CLASS_U8;
            while (size - offset >= piece)
            {
                cbuiltin_misc_lower_offset(recipe, call->arguments[0], offset);
                cbuiltin_lower_local(recipe, pattern);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, class, 0);
                cbuiltin_lower_emit(recipe, BUSTER_TYPED_SET, class, 0);
                offset += piece;
            }
        }
    }
    cbuiltin_lower_local(recipe, call->arguments[0]);
}

static void cbuiltin_misc_lower_copy(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    const uint32_t end = cbuiltin_lower_temporary(recipe);
    const uint32_t pattern = call->id == CBUILTIN_MEMSET ? cbuiltin_lower_temporary(recipe) : 0;
    const uint32_t bytes = call->id == CBUILTIN_MEMCPY ? 16 : 8;
    uint32_t empty = UINT32_MAX;
    uint32_t one_end = UINT32_MAX;
    uint32_t eight_end = UINT32_MAX;
    if (!call->constant_size_known)
    {
        // Measured short dynamic operations need neither loop setup nor overlap classification. Each nonzero
        // fast case is a single exact load/store, which is safe for a move in either direction too.
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
        empty = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_constant(recipe, 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
        const uint32_t not_one = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_misc_lower_copy_single(recipe, call, BUSTER_TYPED_CLASS_U8, pattern);
        one_end = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
        cbuiltin_misc_lower_patch(recipe, not_one, first);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_constant(recipe, 8);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
        const uint32_t not_eight = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        if (call->id == CBUILTIN_MEMSET)
        {
            cbuiltin_misc_lower_pattern(recipe, call, pattern);
        }
        cbuiltin_misc_lower_copy_single(recipe, call, BUSTER_TYPED_CLASS_U64, pattern);
        eight_end = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
        cbuiltin_misc_lower_patch(recipe, not_eight, first);
    }
    if (call->id == CBUILTIN_MEMSET)
    {
        cbuiltin_misc_lower_pattern(recipe, call, pattern);
    }
    uint32_t backward_end = UINT32_MAX;
    if (call->id == CBUILTIN_MEMMOVE)
    {
        cbuiltin_lower_local(recipe, call->arguments[0]);
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_GT);
        const uint32_t lower_destination = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_local(recipe, call->arguments[0]);
        cbuiltin_lower_local(recipe, call->arguments[1]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
        const uint32_t disjoint = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_local(recipe, call->arguments[2]);
        cbuiltin_lower_set(recipe, index);
        const uint32_t wide_head = first + recipe->count;
        cbuiltin_lower_local(recipe, index);
        cbuiltin_lower_constant(recipe, bytes);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_GE);
        const uint32_t tail = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_SUB, index, bytes);
        cbuiltin_misc_lower_copy_word(recipe, call, index, pattern);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, wide_head);
        cbuiltin_misc_lower_patch(recipe, tail, first);
        const uint32_t head = first + recipe->count;
        cbuiltin_lower_local(recipe, index);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
        const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
        cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_SUB, index, 1);
        cbuiltin_misc_lower_copy_byte(recipe, call, index);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
        cbuiltin_misc_lower_patch(recipe, zero, first);
        backward_end = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
        cbuiltin_misc_lower_patch(recipe, lower_destination, first);
        cbuiltin_misc_lower_patch(recipe, disjoint, first);
    }
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, index);
    cbuiltin_lower_immediate(recipe, end, BUSTER_TYPED_AND, call->arguments[2], ~(uint64_t)(bytes - 1));
    const uint32_t wide_head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, end);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t tail = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_copy_word(recipe, call, index, pattern);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, bytes);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, wide_head);
    cbuiltin_misc_lower_patch(recipe, tail, first);
    const uint32_t head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, call->arguments[2]);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_copy_byte(recipe, call, index);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 1);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
    cbuiltin_misc_lower_patch(recipe, zero, first);
    cbuiltin_misc_lower_patch(recipe, backward_end, first);
    cbuiltin_misc_lower_patch(recipe, empty, first);
    cbuiltin_misc_lower_patch(recipe, one_end, first);
    cbuiltin_misc_lower_patch(recipe, eight_end, first);
    cbuiltin_lower_local(recipe, call->arguments[0]);
}

static void cbuiltin_misc_lower_compare(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    const uint32_t left = cbuiltin_lower_temporary(recipe);
    const uint32_t right = cbuiltin_lower_temporary(recipe);
    const uint32_t result = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, index);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_set(recipe, result);
    const uint32_t end = cbuiltin_lower_temporary(recipe);
    cbuiltin_lower_immediate(recipe, end, BUSTER_TYPED_AND, call->arguments[2], ~UINT64_C(7));
    const uint32_t wide_head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, end);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t tail = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_misc_lower_address(recipe, call->arguments[1], index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    const uint32_t mismatch = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 8);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, wide_head);
    cbuiltin_misc_lower_patch(recipe, tail, first);
    cbuiltin_misc_lower_patch(recipe, mismatch, first);
    const uint32_t head = first + recipe->count;
    cbuiltin_lower_local(recipe, index);
    cbuiltin_lower_local(recipe, call->arguments[2]);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_LT);
    const uint32_t zero = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_FALSE);
    cbuiltin_misc_lower_byte(recipe, call->arguments[0], index, left);
    cbuiltin_misc_lower_byte(recipe, call->arguments[1], index, right);
    cbuiltin_lower_local(recipe, left);
    cbuiltin_lower_local(recipe, right);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
    const uint32_t same = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
    cbuiltin_lower_binary(recipe, result, BUSTER_TYPED_SUB, left, right);
    const uint32_t different = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_JUMP);
    cbuiltin_misc_lower_patch(recipe, same, first);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 1);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, head);
    cbuiltin_misc_lower_patch(recipe, zero, first);
    cbuiltin_misc_lower_patch(recipe, different, first);
    cbuiltin_lower_local(recipe, result);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
}

static void cbuiltin_misc_lower_length(CBuiltinLowerRecipe* recipe, const CBuiltinMiscLowerCall* call, uint32_t first)
{
    const uint32_t index = cbuiltin_lower_temporary(recipe);
    const uint32_t word = cbuiltin_lower_temporary(recipe);
    uint32_t short_ends[8];
    // Tiny strings take one branch per character, with no loop or alignment arithmetic. Each next load is reached
    // only after the preceding character proved nonzero, including at a guard page. The index local joins the long
    // path's result without leaving operands across a condition in the typed stream.
    for (uint32_t i = 0; i < 8; i += 1)
    {
        cbuiltin_lower_constant(recipe, i);
        cbuiltin_lower_set(recipe, index);
        cbuiltin_misc_lower_offset(recipe, call->arguments[0], i);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
        cbuiltin_lower_emit(recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_EQ);
        short_ends[i] = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
    }
    // The first eight bytes are nonzero. Start at the next aligned word, rechecking at most seven of those bytes.
    // This removes a per-character alignment loop without reading before the supplied pointer.
    cbuiltin_lower_constant(recipe, 8);
    cbuiltin_lower_local(recipe, call->arguments[0]);
    cbuiltin_lower_constant(recipe, 7);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_set(recipe, index);
    const uint32_t wide_head = first + recipe->count;
    cbuiltin_misc_lower_address(recipe, call->arguments[0], index);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
    cbuiltin_lower_set(recipe, word);
    // has_zero(x) = (x - ones) & ~x & high_bits. Only its nonzeroness is used; borrow can mark later bytes too.
    cbuiltin_lower_local(recipe, word);
    cbuiltin_lower_constant(recipe, UINT64_C(0x0101010101010101));
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_SUB);
    cbuiltin_lower_local(recipe, word);
    cbuiltin_lower_constant(recipe, UINT64_MAX);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_XOR);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_constant(recipe, UINT64_C(0x8080808080808080));
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_CLASS_U64, word);
    cbuiltin_lower_constant(recipe, 0);
    cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
    const uint32_t tail = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
    cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, 8);
    cbuiltin_lower_emit(recipe, BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID, wide_head);
    cbuiltin_misc_lower_patch(recipe, tail, first);
    // Borrow may mark bytes after the first zero, never before it. Locate that first marked byte in three steps,
    // without rereading up to eight bytes or assuming a target BMI/POPCNT instruction.
    for (uint32_t bytes = 4; bytes != 0; bytes /= 2)
    {
        cbuiltin_lower_local(recipe, word);
        cbuiltin_lower_constant(recipe, (UINT64_C(1) << (bytes * 8)) - 1);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_AND);
        cbuiltin_lower_constant(recipe, 0);
        cbuiltin_lower_operation(recipe, BUSTER_TYPED_NE);
        const uint32_t lower = cbuiltin_misc_lower_branch(recipe, BUSTER_TYPED_BRANCH_TRUE);
        cbuiltin_lower_immediate(recipe, index, BUSTER_TYPED_ADD, index, bytes);
        cbuiltin_lower_immediate(recipe, word, BUSTER_TYPED_SHR, word, bytes * 8);
        cbuiltin_misc_lower_patch(recipe, lower, first);
    }
    for (uint32_t i = 0; i < 8; i += 1)
    {
        cbuiltin_misc_lower_patch(recipe, short_ends[i], first);
    }
    cbuiltin_lower_local(recipe, index);
}

static CBuiltinLowerResult cbuiltin_misc_append(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call, bool vector)
{
    (void)vector;

    CBuiltinLowerResult result = { 0 };
    CBuiltinLowerRecipe recipe = { 0 };
    uint32_t minimum = 0;
    uint32_t maximum = 0;
    uint32_t result_class = BUSTER_TYPED_CLASS_VOID;
    if (buffer == 0 || call == 0 || buffer->count > buffer->capacity || buffer->local_count > buffer->local_capacity ||
        buffer->capacity > BUSTER_TIER0_MAX_ENTRIES || buffer->local_capacity > BUSTER_TIER0_MAX_LOCALS || buffer->kinds == 0 ||
        buffer->classes == 0 || buffer->values == 0 || buffer->local_classes == 0 || buffer->local_flags == 0)
    {
        result.error = CBUILTIN_LOWER_ARGUMENT;
    }
    else
    {
        switch (call->id)
        {
        case CBUILTIN_EXPECT: minimum = maximum = 2; result_class = BUSTER_TYPED_CLASS_I64; break;
        case CBUILTIN_ASSUME_ALIGNED: minimum = 2; maximum = 3; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_PREFETCH: minimum = 1; maximum = 3; break;
        case CBUILTIN_EXTRACT_RETURN_ADDR:
        case CBUILTIN_FROB_RETURN_ADDR:
        case CBUILTIN_LAUNDER:
        case CBUILTIN_ALLOCA:
        case CBUILTIN_STRLEN: minimum = maximum = 1; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_MEMCPY:
        case CBUILTIN_MEMMOVE:
        case CBUILTIN_MEMSET: minimum = maximum = 3; result_class = BUSTER_TYPED_CLASS_U64; break;
        case CBUILTIN_MEMCMP: minimum = maximum = 3; result_class = BUSTER_TYPED_CLASS_I32; break;
        case CBUILTIN_FRAME_ADDRESS:
        case CBUILTIN_RETURN_ADDRESS:
        case CBUILTIN_DWARF_CFA: minimum = maximum = 0; result_class = BUSTER_TYPED_CLASS_U64; break;
        default:
            if (cbuiltin_bits(call->id) || cbuiltin_align(call->id))
            {
                // A result past 64 bits is its frame object's value (c_builtin_bits_lower.c, cbuiltin_misc_align).
                const bool carry = cbuiltin_carry(call->id);
                const bool wide = !carry && cbuiltin_bits_width(call->result_type) > 64;
                if (cbuiltin_align(call->id))
                {
                    minimum = maximum = 2;
                }
                else
                {
                    cbuiltin_bits_arity(call->id, &minimum, &maximum);
                }
                result_class = cbuiltin_bits_class(carry ? call->argument_types[0] : call->result_type);
                result.error = result_class == BUSTER_TYPED_CLASS_VOID || (wide && !cbuiltin_misc_lower_object(buffer, call->result_object)) ?
                               CBUILTIN_LOWER_ARGUMENT : CBUILTIN_LOWER_OK;
            }
            else
            {
                result.error = CBUILTIN_LOWER_UNSUPPORTED;
            }
            break;
        }
        if (result.error == CBUILTIN_LOWER_OK && (call->argument_count < minimum || call->argument_count > maximum))
        {
            result.error = CBUILTIN_LOWER_ARGUMENT;
        }
        for (uint32_t i = 0; result.error == CBUILTIN_LOWER_OK && i < call->argument_count; i += 1)
        {
            // A bit builtin's argument is of its descriptor's class, one past 64 bits a frame object, and a carry's pointer U64.
            const bool bits = (cbuiltin_bits(call->id) && !(cbuiltin_carry(call->id) && i == 3)) || cbuiltin_align(call->id);
            const bool wide = bits && cbuiltin_bits_width(call->argument_types[i]) > 64;
            const uint32_t class = bits ? cbuiltin_lower_type_class(call->argument_types[i]) : call->id == CBUILTIN_EXPECT ? BUSTER_TYPED_CLASS_I64 :
                                   call->id == CBUILTIN_MEMSET && i == 1 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
            const bool hint_integer = i > 0 && (call->id == CBUILTIN_PREFETCH || (call->id == CBUILTIN_ASSUME_ALIGNED && i == 1));
            const bool valid = hint_integer ? cbuiltin_misc_lower_integer(buffer, call->arguments[i]) :
                               wide ? cbuiltin_bits_width(call->argument_types[i]) <= 128 && cbuiltin_misc_lower_object(buffer, call->arguments[i]) :
                                      class != BUSTER_TYPED_CLASS_VOID && cbuiltin_misc_lower_local_class(buffer, call->arguments[i], class);
            if (!valid)
            {
                result.error = CBUILTIN_LOWER_ARGUMENT;
            }
        }
    }
    if (result.error == CBUILTIN_LOWER_OK)
    {
        recipe.temporary_first = buffer->local_count;
        if (call->id == CBUILTIN_MEMCPY || call->id == CBUILTIN_MEMMOVE || call->id == CBUILTIN_MEMSET)
        {
            if (call->constant_size_known && call->constant_size <= (call->id == CBUILTIN_MEMMOVE ? 16u : BUSTER_CBUILTIN_MISC_SMALL_BYTES))
            {
                cbuiltin_misc_lower_copy_small(&recipe, call);
            }
            else
            {
                cbuiltin_misc_lower_copy(&recipe, call, buffer->count);
            }
        }
        else if (call->id == CBUILTIN_MEMCMP)
        {
            if (call->constant_size_known && call->constant_size <= 1)
            {
                if (call->constant_size == 0)
                {
                    cbuiltin_lower_constant(&recipe, 0);
                }
                else
                {
                    for (uint32_t i = 0; i < 2; i += 1)
                    {
                        cbuiltin_lower_local(&recipe, call->arguments[i]);
                        cbuiltin_lower_emit(&recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U8, 0);
                        cbuiltin_lower_emit(&recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
                    }
                    cbuiltin_lower_operation(&recipe, BUSTER_TYPED_SUB);
                }
                cbuiltin_lower_emit(&recipe, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32, 0);
            }
            else
            {
                cbuiltin_misc_lower_compare(&recipe, call, buffer->count);
            }
        }
        else if (call->id == CBUILTIN_STRLEN)
        {
            cbuiltin_misc_lower_length(&recipe, call, buffer->count);
        }
        else if (call->id == CBUILTIN_ALLOCA)
        {
            // clang -O0's dynamic alloca: the size rounded up to 16 and taken from the stack, which the function's
            // return (or an enclosing variable-length array scope's restore, where clang's stackrestore is) releases.
            cbuiltin_lower_local(&recipe, call->arguments[0]);
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_CLASS_U64, 0);
        }
        else if (call->id == CBUILTIN_DWARF_CFA)
        {
            // clang -O0's canonical frame address: the frame pointer, past the caller's rbp and the return address.
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_lower_constant(&recipe, 16);
            cbuiltin_lower_operation(&recipe, BUSTER_TYPED_ADD);
        }
        else if (call->id == CBUILTIN_FRAME_ADDRESS || call->id == CBUILTIN_RETURN_ADDRESS)
        {
            // Level 0's, as clang -O0's frame has them: the frame pointer, and the return address above the caller's rbp
            // that it points at.
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            if (call->id == CBUILTIN_RETURN_ADDRESS)
            {
                cbuiltin_lower_constant(&recipe, 8);
                cbuiltin_lower_operation(&recipe, BUSTER_TYPED_ADD);
                cbuiltin_lower_emit(&recipe, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_U64, 0);
            }
        }
        else if (cbuiltin_bits(call->id))
        {
            cbuiltin_bits_recipe(&recipe, call->id, call->arguments, call->argument_types, call->argument_count, call->result_type, call->result_object);
        }
        else if (cbuiltin_align(call->id))
        {
            cbuiltin_misc_align(&recipe, call);
        }
        else if (call->id != CBUILTIN_PREFETCH)
        {
            cbuiltin_lower_emit(&recipe, BUSTER_TYPED_LOCAL, result_class, call->arguments[0]);
        }
        if (recipe.full || recipe.count > buffer->capacity - buffer->count || recipe.temporary_count > buffer->local_capacity - buffer->local_count)
        {
            result.error = CBUILTIN_LOWER_CAPACITY;
        }
        else
        {
            result.first = buffer->count;
            result.count = recipe.count;
            result.result_class = result_class;
            result.temporary_first = buffer->local_count;
            result.temporary_count = recipe.temporary_count;
            if (BUSTER_VECTOR_PATH(vector))
            {
                for (uint32_t i = 0; i < recipe.count; i += 64)
                {
                    const uint32_t remaining = recipe.count - i < 64 ? recipe.count - i : 64;
                    const __mmask64 mask = UINT64_MAX >> (64 - remaining);
                    _mm512_mask_storeu_epi8(buffer->kinds + buffer->count + i, mask, _mm512_maskz_loadu_epi8(mask, recipe.kinds + i));
                    _mm512_mask_storeu_epi8(buffer->classes + buffer->count + i, mask, _mm512_maskz_loadu_epi8(mask, recipe.classes + i));
                }
                for (uint32_t i = 0; i < recipe.count; i += 8)
                {
                    const uint32_t remaining = recipe.count - i < 8 ? recipe.count - i : 8;
                    const __mmask8 mask = (__mmask8)(UINT32_C(255) >> (8 - remaining));
                    _mm512_mask_storeu_epi64(buffer->values + buffer->count + i, mask, _mm512_maskz_loadu_epi64(mask, recipe.values + i));
                }
            }
            else
            {
                for (uint32_t i = 0; i < recipe.count; i += 1)
                {
                    buffer->kinds[buffer->count + i] = recipe.kinds[i];
                    buffer->classes[buffer->count + i] = recipe.classes[i];
                    buffer->values[buffer->count + i] = recipe.values[i];
                }
            }
            memset(buffer->local_classes + buffer->local_count, BUSTER_TYPED_CLASS_U64, recipe.temporary_count);
            memset(buffer->local_flags + buffer->local_count, 0, recipe.temporary_count);
            buffer->count += recipe.count;
            buffer->local_count += recipe.temporary_count;
        }
    }
    return result;
}

static CBuiltinLowerResult cbuiltin_misc_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call);

static CBuiltinLowerResult cbuiltin_misc_lower(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call)
{
#if !BUSTER_HOST_VECTOR
    return cbuiltin_misc_lower_scalar(buffer, call);
#else
    const CBuiltinLowerResult result = cbuiltin_misc_append(buffer, call, true);
    return result;
#endif
}

static CBuiltinLowerResult cbuiltin_misc_lower_scalar(CBuiltinLowerBuffer* buffer, const CBuiltinMiscLowerCall* call)
{
    const CBuiltinLowerResult result = cbuiltin_misc_append(buffer, call, false);
    return result;
}

#endif
