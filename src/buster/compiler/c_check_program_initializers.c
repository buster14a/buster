// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Immutable byte images for automatic initializers. Prepared before function lanes run; each initializer has a
// contiguous range of pools, either one constant prefix or its large literal parts. Small writes, volatile objects,
// runtime expressions and address relocations keep their ordered lowering. memcpy consumes the exact checked bytes.

#define BUSTER_CHECK_LOWER_INIT_UNROLLED 128u
// Zeros past this many bytes are cleared by a call of memset (check_lower_fill), store by store up to it.
#define BUSTER_CHECK_LOWER_FILL_UNROLLED 128u

typedef struct CheckLowerInitPool CheckLowerInitPool;
struct CheckLowerInitPool
{
    const uint8_t* bytes;
    uint32_t size;
    // BUSTER_CHECK_NONE denotes a complete constant prefix; otherwise an index in CheckInitializer.parts.
    uint32_t part;
    // Zero for byte images; machine literals retain their required placement alignment.
    uint8_t alignment_log2;
};

// __builtin_dump_struct's printing calls (#148, c_check_program_dump.c): an operand of a call's after the rest of the
// builtin's arguments and the format, one of
// - STRING, a string of the call's image at `value`;
// - SIZE, the size_t `value` (a bit-field's width);
// - VALUE, the value of the field of `type` at bit `value` of the record (a bit-field of `width` bits, else
//   BUSTER_TYPE_NOT_BIT_FIELD), converted to `promoted`, as a variadic argument is promoted;
// - ADDRESS, the address of the field at bit `value`.
typedef enum CheckLowerDumpKind
{
    CHECK_LOWER_DUMP_STRING,
    CHECK_LOWER_DUMP_SIZE,
    CHECK_LOWER_DUMP_VALUE,
    CHECK_LOWER_DUMP_ADDRESS,
} CheckLowerDumpKind;

typedef struct CheckLowerDumpOperand CheckLowerDumpOperand;
struct CheckLowerDumpOperand
{
    uint64_t value;
    TypeId type;
    TypeId promoted;
    uint32_t width;
    uint8_t kind;
};

// A call: its format's offset in the image and its operands, `count` of them from `first`.
typedef struct CheckLowerDumpLine CheckLowerDumpLine;
struct CheckLowerDumpLine
{
    uint32_t format;
    uint32_t first;
    uint32_t count;
};

// A call of __builtin_dump_struct: its node, the pool of its strings' image (CheckLowerInitializers.items) and its calls,
// or the reason its code is refused.
typedef struct CheckLowerDump CheckLowerDump;
struct CheckLowerDump
{
    uint32_t node;
    uint32_t pool;
    const CheckLowerDumpLine* lines;
    uint32_t line_count;
    const CheckLowerDumpOperand* operands;
    const char* refusal;
};

// A call of __builtin_clear_padding (c_check_program_padding.c): its node, its object's elements, each of `stride` bytes
// (none for a variable length array's), and the stores of one, `count` entries (CBuiltinMiscLowerCall.padding's), which
// the recipe makes in place, or, where `pool` isn't BUSTER_CHECK_NONE, a loop reads from that pool; or its refusal.
typedef struct CheckLowerPadding CheckLowerPadding;
struct CheckLowerPadding
{
    uint32_t node;
    uint32_t pool;
    const uint64_t* entries;
    uint32_t count;
    uint64_t elements;
    uint64_t stride;
    const char* refusal;
};

typedef struct CheckLowerInitializers CheckLowerInitializers;
struct CheckLowerInitializers
{
    CheckLowerInitPool* items;
    uint32_t* first;
    uint32_t count;
    SymbolId symbol_base;
    // Exact F80 keys (significand, sign/exponent) in ascending key order. Their 16-byte images are items at x87_first.
    const uint64_t* x87_keys;
    uint32_t x87_first;
    uint32_t x87_count;
    // The unit's __builtin_dump_struct calls, whose images are pools after the initializers' (first[count]
    // on), and the room the lowering's writers take besides the per-node bounds: for those calls, which evaluate the
    // printer and the rest of the arguments once a call, and for __builtin_os_log_format's data, evaluated once an item.
    CheckLowerDump* dumps;
    uint32_t dump_count;
    // The unit's __builtin_clear_padding calls, whose tables are pools after the dumps'.
    CheckLowerPadding* paddings;
    uint32_t padding_count;
    uint64_t extra_entries;
    uint64_t extra_calls;
    uint64_t extra_arguments;
    uint64_t extra_locals;
    // The atomic plans (CheckLowerWriter.atomics) of the nodes a dump's calls and os_log's items evaluate again, an _Atomic
    // object's read among them, past the one each node has (#1842).
    uint64_t extra_atomics;
};

// Pooling and aggregate COPY share the frozen unit's by-value containment facts. Top-level qualifiers remain in
// TypeId (including qualified arrays/aligned types); a lookup does no allocation or type-sized walk per initializer.
static bool check_lower_init_volatile(TypeId type, const uint8_t* facts, uint32_t count)
{
    const uint32_t index = type_index(type);
    return (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) != 0 || index >= count || facts == NULL ||
           (facts[index] & BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY) != 0;
}

// Whether the selected program can initialize this automatic object. Compound literals initialize at evaluation,
// rather than at a control entry, so also retain their selected enclosing function's plans.
static bool check_lower_init_selected(const CheckProgram* program, uint32_t plan, const uint8_t* selected)
{
    const CheckInput* input = program->input;
    const uint32_t declaration = program->initializers->declaration_tokens[plan];
    bool wanted = selected[plan] != 0;
    if (!wanted && declaration < input->token_count && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
        program->compound_initializers != NULL && program->compound_initializers[plan] != 0)
    {
        const uint32_t opening = check_sparse_get(input->declaration_bodies, declaration);
        uint32_t first = 0;
        uint32_t end = program->body_count;
        while (first < end)
        {
            const uint32_t middle = first + (end - first) / 2;
            if (program->bodies[middle].opening < opening)
            {
                first = middle + 1;
            }
            else
            {
                end = middle;
            }
        }
        wanted = first < program->body_count && program->bodies[first].opening == opening;
    }
    return wanted && declaration < input->token_count && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_STATIC) &&
           (type_qualifiers(type_strip_aligned(program->types, program->initializers->plans[plan].type)) & BUSTER_TYPE_VOLATILE) == 0;
}

// The constant prefix's size, or zero where any part needs runtime work. The checked converted values are serialized
// by the same target-byte routines as constant vector initialization. Padding and omitted subobjects start at zero.
// Do not manufacture a huge image for a sparse initializer whose existing few stores already have bounded code.
static uint32_t check_lower_init_prefix(const CheckProgram* program, const CheckInitializer* plan)
{
    uint64_t end = 0;
    uint64_t payload = 0;
    bool constant = plan->status == CHECK_INIT_OK && plan->object_size <= UINT32_MAX;
    for (uint32_t p = 0; constant && p < plan->count; p += 1)
    {
        const CheckInitPart* part = plan->parts + p;
        uint64_t bits = 0;
        uint8_t scalar[16];
        constant = part->overload_candidate == 0 && part->bit_offset % 8 == 0 && part->bit_width == 0 &&
                   (type_qualifiers(type_strip_aligned(program->types, part->type)) & BUSTER_TYPE_VOLATILE) == 0 &&
                   check_initializer_part_bits(program->types, part, &bits) == CHECK_INIT_OK && bits % 8 == 0 &&
                   part->bit_offset / 8 <= plan->object_size && bits / 8 <= plan->object_size - part->bit_offset / 8;
        if (constant && part->kind == CHECK_INIT_VALUE)
        {
            const uint32_t flags = part->value.flags | (part->node < program->stream->count ? check_stream_flags(program->stream, part->node) : 0);
            constant = (flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 &&
                       bits <= 128 && check_init_scalar_bytes(program->types, &part->value, scalar);
        }
        else if (constant && part->kind == CHECK_INIT_BYTES)
        {
            constant = bits == 0 || part->bytes != NULL;
        }
        if (constant && part->kind != CHECK_INIT_CLEAR)
        {
            const uint64_t stop = part->bit_offset / 8 + bits / 8;
            end = stop > end ? stop : end;
            payload += bits / 8;
        }
    }
    return constant && payload > BUSTER_CHECK_LOWER_INIT_UNROLLED && (end + 1) / 2 <= payload ? (uint32_t)end : 0;
}

static bool check_lower_init_image(const CheckProgram* program, const CheckInitializer* plan, uint8_t* bytes, uint32_t size)
{
    memset(bytes, 0, size);
    bool valid = true;
    for (uint32_t p = 0; valid && p < plan->count; p += 1)
    {
        const CheckInitPart* part = plan->parts + p;
        uint64_t bits = 0;
        uint8_t scalar[16];
        valid = check_initializer_part_bits(program->types, part, &bits) == CHECK_INIT_OK;
        const uint64_t first = part->bit_offset / 8;
        const uint64_t count = first < size ? (bits / 8 < size - first ? bits / 8 : size - first) : 0;
        if (valid && count != 0 && part->kind == CHECK_INIT_CLEAR)
        {
            memset(bytes + first, 0, count);
        }
        else if (valid && count != 0)
        {
            valid = part->kind == CHECK_INIT_BYTES || check_init_scalar_bytes(program->types, &part->value, scalar);
            if (valid)
            {
                memcpy(bytes + first, part->kind == CHECK_INIT_BYTES ? part->bytes : scalar, count);
            }
        }
    }
    return valid;
}

#include "c_check_program_dump.c"
#include "c_check_program_padding.c"

static bool check_lower_init_pools(CheckLowerInitializers* pools, const CheckProgram* program, const uint8_t* type_facts, uint32_t type_count,
                                   Arena* output, Arena* scratch)
{
    *pools = (CheckLowerInitializers){ 0 };
    const uint32_t count = program->initializers != NULL ? program->initializers->count : 0;
    const uint64_t mark = scratch->length;
    uint8_t* selected = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    uint32_t* prefixes = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
    bool valid = count == 0 || (selected != NULL && prefixes != NULL);
    if (valid && count != 0)
    {
        memset(selected, 0, count);
    }
    for (uint32_t c = 0; valid && count != 0 && c < program->control_count; c += 1)
    {
        const CheckControl* control = program->controls + c;
        if (control->kind == CHECK_CONTROL_INITIALIZE && control->target < count)
        {
            selected[control->target] = 1;
        }
    }
    uint64_t capacity = 0;
    uint64_t clears = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckInitializer* plan = program->initializers->plans + i;
        const bool large = plan->object_size > BUSTER_CHECK_LOWER_INIT_UNROLLED || plan->object_size > BUSTER_CHECK_LOWER_FILL_UNROLLED;
        const bool wanted = large && check_lower_init_selected(program, i, selected);
        selected[i] = wanted && plan->object_size > BUSTER_CHECK_LOWER_INIT_UNROLLED;
        // The memset calls check_lower_fill may make for the plan's zeros: its object's, whole or past a prefix, and its
        // parts'. A volatile object makes none (check_lower_init_selected leaves it out, and check_lower_init_library).
        const bool clearing = wanted && plan->object_size > BUSTER_CHECK_LOWER_FILL_UNROLLED;
        clears += clearing;
        for (uint32_t p = 0; clearing && p < plan->count; p += 1)
        {
            clears += plan->parts[p].kind == CHECK_INIT_CLEAR && plan->parts[p].bit_width == 0 && plan->parts[p].byte_count > BUSTER_CHECK_LOWER_FILL_UNROLLED;
        }
        prefixes[i] = selected[i] ? check_lower_init_prefix(program, plan) : 0;
        if (prefixes[i] != 0 && check_lower_init_volatile(plan->type, type_facts, type_count))
        {
            prefixes[i] = 0;
        }
        capacity += prefixes[i] != 0;
        for (uint32_t p = 0; selected[i] && prefixes[i] == 0 && p < plan->count; p += 1)
        {
            capacity += plan->parts[p].kind == CHECK_INIT_BYTES && plan->parts[p].byte_count > BUSTER_CHECK_LOWER_INIT_UNROLLED &&
                        (type_qualifiers(type_strip_aligned(program->types, plan->parts[p].type)) & BUSTER_TYPE_VOLATILE) == 0;
        }
    }
    valid = valid && capacity < UINT32_MAX;
    if (valid && capacity != 0)
    {
        pools->items = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerInitPool, capacity);
        pools->first = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1);
        valid = pools->items != NULL && pools->first != NULL;
    }
    for (uint32_t i = 0; valid && capacity != 0 && i < count; i += 1)
    {
        const CheckInitializer* plan = program->initializers->plans + i;
        pools->first[i] = pools->count;
        if (prefixes[i] != 0)
        {
            uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, prefixes[i]);
            valid = bytes != NULL && check_lower_init_image(program, plan, bytes, prefixes[i]);
            if (valid)
            {
                pools->items[pools->count++] = (CheckLowerInitPool){ .bytes = bytes, .size = prefixes[i], .part = BUSTER_CHECK_NONE };
            }
        }
        for (uint32_t p = 0; valid && selected[i] && prefixes[i] == 0 && p < plan->count; p += 1)
        {
            const CheckInitPart* part = plan->parts + p;
            if (part->kind == CHECK_INIT_BYTES && part->byte_count > BUSTER_CHECK_LOWER_INIT_UNROLLED &&
                (type_qualifiers(type_strip_aligned(program->types, part->type)) & BUSTER_TYPE_VOLATILE) == 0)
            {
                valid = part->bytes != NULL && part->byte_count <= UINT32_MAX && part->bit_offset % 8 == 0 && part->bit_width == 0;
                if (valid)
                {
                    pools->items[pools->count++] = (CheckLowerInitPool){ .bytes = part->bytes, .size = (uint32_t)part->byte_count, .part = p };
                }
            }
        }
    }
    if (valid && capacity != 0)
    {
        pools->first[count] = pools->count;
    }
    // Each clear's call and its three arguments, past the per-node bounds.
    pools->extra_calls += clears;
    pools->extra_arguments += clears * 3;
    arena_pop_to(scratch, mark);
    return valid && check_lower_dump_prepare(pools, program, output, scratch) && check_lower_padding_prepare(pools, program, output, scratch);
}

static uint32_t check_lower_init_pool(const CheckLowerInitializers* pools, uint32_t plan, uint32_t part)
{
    uint32_t first = pools->first != NULL ? pools->first[plan] : 0;
    uint32_t end = pools->first != NULL ? pools->first[plan + 1] : 0;
    const uint32_t limit = end;
    while (first < end)
    {
        const uint32_t middle = first + (end - first) / 2;
        if (pools->items[middle].part < part)
        {
            first = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return first < limit && pools->items[first].part == part ? first : BUSTER_CHECK_NONE;
}
