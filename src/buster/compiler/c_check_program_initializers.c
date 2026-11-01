// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Immutable byte images for automatic initializers. Prepared before function lanes run; each initializer has a
// contiguous range of pools, either one constant prefix or its large literal parts. Small writes, volatile objects,
// runtime expressions and address relocations keep their ordered lowering. memcpy consumes the exact checked bytes.

#define BUSTER_CHECK_LOWER_INIT_UNROLLED 128u

typedef struct CheckLowerInitPool CheckLowerInitPool;
struct CheckLowerInitPool
{
    const uint8_t* bytes;
    uint32_t size;
    // BUSTER_CHECK_NONE denotes a complete constant prefix; otherwise an index in CheckInitializer.parts.
    uint32_t part;
};

typedef struct CheckLowerInitializers CheckLowerInitializers;
struct CheckLowerInitializers
{
    CheckLowerInitPool* items;
    uint32_t* first;
    uint32_t count;
    SymbolId symbol_base;
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
    if (!wanted && declaration < input->token_count && input->compound_objects[declaration] && !input->static_objects[declaration] &&
        program->compound_initializers != NULL && program->compound_initializers[plan] != 0)
    {
        const uint32_t opening = input->declaration_bodies[declaration];
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
    return wanted && declaration < input->token_count && !input->static_objects[declaration] &&
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
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const CheckInitializer* plan = program->initializers->plans + i;
        selected[i] = plan->object_size > BUSTER_CHECK_LOWER_INIT_UNROLLED && check_lower_init_selected(program, i, selected);
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
    arena_pop_to(scratch, mark);
    return valid;
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
