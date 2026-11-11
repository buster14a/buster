// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __builtin_clear_padding's stores (#148, #1842), planned before the function lanes run, as clang's PaddingClearer
// (clang/lib/CodeGen/CGBuiltin.cpp) finds them: the bits no scalar, bit-field or vector of the object holds (an unnamed
// bit-field's among them, a union's bits that none of its members holds, an x87 long double's last 48, a _BitInt's past
// its width, an atomic type's past its value's) are zeros. An array's elements, through arrays of arrays, are cleared in
// a loop by their element's stores, and a variable length array is left as it is, as clang's code leaves it.
//
// The walk gives each held piece of the element as an interval of bits, in an explicit stack's order (no recursion): an
// inner array's first element is walked and its intervals copied for the others, or merged into one where the element
// has no padding. The intervals sorted and merged, each byte no interval touches is a zero, in runs, and each that one
// touches in part an AND of its held bits. A plan of few stores is the call's own recipe's; a longer one is a table of
// its entries in a read-only pool of the unit's (CheckLowerInitializers.items), which the recipe's loop reads.
//
// This is a cold, per-call plan of a type's layout, made once a call before the lanes run; it stays scalar.

// The nesting of a type's arrays, records, complex and atomic types; the entries an element's plan may have; the cost, in
// typed entries, of the stores a call's recipe makes itself, within a builtin's BUSTER_CBUILTIN_LOWER_ENTRIES.
#define BUSTER_CHECK_LOWER_PADDING_DEPTH 64u
#define BUSTER_CHECK_LOWER_PADDING_RUNS (1u << 20)
#define BUSTER_CHECK_LOWER_PADDING_INLINE 460u

typedef struct CheckLowerPaddingBits CheckLowerPaddingBits;
struct CheckLowerPaddingBits
{
    uint64_t first;
    uint64_t last;
};

// The bits a scalar of `type` holds, as clang's PaddingClearer counts them (getScalarOccupiedSizeInBits): an x87 long
// double's 80, a _BitInt's width, any other's size.
static uint64_t check_lower_occupied_bits(const TypeTable* types, TypeId type)
{
    const TypeId bare = type_unqualified(type_strip_aligned(types, type));
    const uint8_t kind = type_kind(types, bare);
    return bare == BUSTER_TYPE_LONG_DOUBLE ? 80 : kind == BUSTER_TYPE_KIND_BIT_INT ? types->types.extent[type_index(bare)] : type_size(types, bare) * 8;
}

// Sorts the intervals from `first` to `count` by their first bits, bottom-up through `spare` (as many), where they aren't
// already in order, then merges those that overlap or touch. Returns the new count.
static uint32_t check_lower_padding_normalize(CheckLowerPaddingBits* bits, CheckLowerPaddingBits* spare, uint32_t first, uint32_t count)
{
    bool sorted = true;
    for (uint32_t i = first + 1; sorted && i < count; i += 1)
    {
        sorted = bits[i - 1].first <= bits[i].first;
    }
    const uint32_t n = count - first;
    CheckLowerPaddingBits* from = bits + first;
    CheckLowerPaddingBits* to = spare;
    for (uint32_t width = 1; !sorted && width < n; width *= 2)
    {
        for (uint32_t left = 0; left < n; left += 2 * width)
        {
            const uint32_t middle = left + width < n ? left + width : n;
            const uint32_t end = middle + width < n ? middle + width : n;
            uint32_t a = left;
            uint32_t b = middle;
            for (uint32_t k = left; k < end; k += 1)
            {
                const bool take_a = a < middle && (b >= end || from[a].first <= from[b].first);
                to[k] = take_a ? from[a] : from[b];
                a += take_a;
                b += !take_a;
            }
        }
        CheckLowerPaddingBits* swap = from;
        from = to;
        to = swap;
    }
    if (from != bits + first)
    {
        memcpy(bits + first, from, (uint64_t)n * sizeof(*bits));
    }
    uint32_t kept = first;
    for (uint32_t i = first; i < count; i += 1)
    {
        const bool merged = kept != first && bits[i].first <= bits[kept - 1].last;
        if (merged)
        {
            bits[kept - 1].last = bits[i].last > bits[kept - 1].last ? bits[i].last : bits[kept - 1].last;
        }
        else
        {
            bits[kept++] = bits[i];
        }
    }
    return kept;
}

// Room for `needed` intervals: where `*capacity` has less, both arrays (`spare` sorting's) grow to the next power of 2 that
// holds them, the `count` intervals copied. False where the arena is full.
static bool check_lower_padding_room(Arena* scratch, CheckLowerPaddingBits** bits, CheckLowerPaddingBits** spare, uint64_t* capacity, uint32_t count,
                                     uint64_t needed)
{
    uint64_t room = *capacity;
    while (room < needed)
    {
        room *= 2;
    }
    CheckLowerPaddingBits* grown = room != *capacity ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, room) : *bits;
    CheckLowerPaddingBits* grown_spare = room != *capacity ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, room) : *spare;
    const bool valid = grown != NULL && grown_spare != NULL;
    if (valid && grown != *bits)
    {
        memcpy(grown, *bits, (uint64_t)count * sizeof(*grown));
        *bits = grown;
        *spare = grown_spare;
        *capacity = room;
    }
    return valid;
}

// The plan of a call's stores on an element of `type`, the size of `size` bytes: its entries, each the byte offset << 32,
// the run's bytes << 8 and the mask kept (0 for a run of zeros), in `scratch`'s top. Returns their count, or UINT32_MAX
// with `*refusal` set where the type nests past the depth above, an offset passes 4 GiB or its runs pass their limit, or
// the arena is full.
static uint32_t check_lower_padding_entries(const TypeTable* types, TypeId type, uint64_t size, Arena* scratch, uint64_t** result, const char** refusal)
{
    typedef struct
    {
        uint64_t offset;
        TypeId type;
        uint64_t index;
        uint32_t mark;
    } Frame;
    Frame stack[BUSTER_CHECK_LOWER_PADDING_DEPTH];
    uint64_t capacity = 64;
    CheckLowerPaddingBits* bits = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    CheckLowerPaddingBits* spare = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    uint32_t count = 0;
    uint32_t depth = 1;
    bool valid = bits != NULL && spare != NULL && size <= UINT32_MAX;
    *refusal = valid ? NULL : size > UINT32_MAX ? "__builtin_clear_padding of an element past 4 GiB" : NULL;
    stack[0] = (Frame){ .offset = 0, .type = type, .index = 0, .mark = 0 };
    while (valid && depth != 0)
    {
        Frame* top = stack + depth - 1;
        const TypeId bare = type_unqualified(type_strip_aligned(types, top->type));
        const uint8_t kind = type_kind(types, bare);
        const uint32_t index = type_index(bare);
        uint64_t first = 0;
        uint64_t last = 0;
        TypeId pushed = 0;
        uint64_t at = 0;
        valid = check_lower_padding_room(scratch, &bits, &spare, &capacity, count, (uint64_t)count + 1);
        if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_COMPLEX)
        {
            // Its first element is walked; then its intervals are its elements', each element's own copy, or, where they
            // hold all of it, the array's one.
            const TypeId element = types->types.base[index];
            const uint64_t elements = kind == BUSTER_TYPE_KIND_COMPLEX ? 2 : types->types.extent[index];
            const uint64_t stride = type_size(types, element) * 8;
            if (top->index == 0 && elements != 0)
            {
                top->mark = count;
                pushed = element;
                at = top->offset;
                top->index = 1;
            }
            else if (elements > 1)
            {
                count = check_lower_padding_normalize(bits, spare, top->mark, count);
                const uint32_t mark = top->mark;
                const uint32_t each = count - mark;
                const bool whole = each == 1 && bits[mark].first == top->offset && bits[mark].last == top->offset + stride;
                const uint64_t total = whole ? (uint64_t)count : (uint64_t)count + each * (elements - 1);
                valid = valid && total <= BUSTER_CHECK_LOWER_PADDING_RUNS;
                *refusal = valid || total <= BUSTER_CHECK_LOWER_PADDING_RUNS ? NULL : "__builtin_clear_padding of an element of more than 1048576 runs of padding";
                if (valid && whole)
                {
                    bits[mark].last = top->offset + stride * elements;
                }
                else if (valid && each != 0)
                {
                    valid = check_lower_padding_room(scratch, &bits, &spare, &capacity, count, total);
                    for (uint64_t e = 1; valid && e < elements; e += 1)
                    {
                        for (uint32_t i = 0; i < each; i += 1)
                        {
                            bits[count + i] = (CheckLowerPaddingBits){ .first = bits[mark + i].first + e * stride, .last = bits[mark + i].last + e * stride };
                        }
                        count += each;
                    }
                }
                depth -= 1;
            }
            else
            {
                depth -= 1;
            }
        }
        else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = types->types.index[index];
            const uint32_t member = types->tags.member_begin[tag] + (uint32_t)top->index;
            const bool more = top->index < types->tags.member_count[tag];
            const bool field = more && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            // An unnamed bit-field is padding.
            first = field && types->members.name[member] != 0 ? top->offset + types->members.offset[member] : 0;
            last = field && types->members.name[member] != 0 ? first + types->members.bit_width[member] : 0;
            pushed = more && !field ? types->members.type[member] : 0;
            at = more ? top->offset + types->members.offset[member] : 0;
            top->index += 1;
            depth -= more ? 0 : 1;
        }
        else if (kind == BUSTER_TYPE_KIND_ATOMIC)
        {
            top->type = types->types.base[index];
        }
        else
        {
            // A vector holds its elements' bits from its start (a vector of bools one bit each); anything else is a
            // scalar's. An array of no size (a flexible member) holds none.
            const bool vector = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
            const TypeId element = vector ? types->types.base[index] : bare;
            const uint64_t elements = vector ? types->types.extent[index] : 1;
            const uint64_t each = vector && type_unqualified(type_strip_aligned(types, element)) == BUSTER_TYPE_BOOL && kind == BUSTER_TYPE_KIND_EXT_VECTOR ? 1 :
                                  kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? 0 : check_lower_occupied_bits(types, element);
            first = top->offset;
            last = top->offset + each * elements;
            depth -= 1;
        }
        const bool laid = last <= size * 8 && kind != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
        *refusal = !valid || laid ? *refusal : "__builtin_clear_padding of a type whose layout this compiler doesn't walk";
        valid = valid && laid;
        if (valid && last > first)
        {
            bits[count++] = (CheckLowerPaddingBits){ .first = first, .last = last };
        }
        if (valid && pushed != 0)
        {
            valid = depth < BUSTER_CHECK_LOWER_PADDING_DEPTH;
            *refusal = valid ? NULL : "__builtin_clear_padding of a type nested more than 64 levels";
            stack[depth] = (Frame){ .offset = at, .type = pushed, .index = 0, .mark = 0 };
            depth += valid;
        }
    }
    count = valid ? check_lower_padding_normalize(bits, spare, 0, count) : 0;
    // The entries: at most a zero run and two bytes' ANDs an interval, a run before the end, and long runs' pieces.
    uint64_t* entries = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, 3 * (uint64_t)count + 2 + size / 0xffffff) : NULL;
    uint32_t entry_count = 0;
    valid = valid && entries != NULL;
    // The byte whose bits an interval holds in part, until no later one can touch it, and the next byte none has touched.
    uint64_t pending = UINT64_MAX;
    uint32_t mask = 0;
    uint64_t next = 0;
    for (uint32_t i = 0; valid && i <= count; i += 1)
    {
        const uint64_t a = i < count ? bits[i].first : size * 8;
        const uint64_t b = i < count ? bits[i].last : size * 8;
        const uint64_t byte = a / 8;
        if (pending != UINT64_MAX && pending < byte)
        {
            if (mask != 0xff)
            {
                entries[entry_count++] = pending << 32 | (uint64_t)1 << 8 | mask;
            }
            next = pending + 1;
            pending = UINT64_MAX;
        }
        for (uint64_t zero = next; pending == UINT64_MAX && zero < byte;)
        {
            const uint64_t run = byte - zero < 0xffffff ? byte - zero : 0xffffff;
            entries[entry_count++] = zero << 32 | run << 8;
            zero += run;
            next = zero;
        }
        if (i < count)
        {
            // The interval's first byte, then its last, whose later bits a later interval may hold.
            const uint64_t final = (b - 1) / 8;
            const uint32_t head = byte == final ? (uint32_t)(((1u << ((b - 1) % 8 + 1)) - 1) & ~((1u << (a % 8)) - 1)) : (uint32_t)(0xffu & ~((1u << (a % 8)) - 1));
            const uint32_t merged = (pending == byte ? mask : 0) | head;
            if (byte != final && merged != 0xff)
            {
                entries[entry_count++] = byte << 32 | (uint64_t)1 << 8 | merged;
            }
            pending = final;
            mask = byte == final ? merged : (uint32_t)((1u << ((b - 1) % 8 + 1)) - 1);
            next = final + 1;
        }
    }
    valid = valid && entry_count <= BUSTER_CHECK_LOWER_PADDING_RUNS;
    *refusal = valid || *refusal != NULL ? *refusal : entries != NULL ? "__builtin_clear_padding of an element of more than 1048576 runs of padding" : NULL;
    *result = entries;
    return valid ? entry_count : UINT32_MAX;
}

// The cost, in a builtin's typed entries, of an element's stores made in place (cbuiltin_misc_lower_padding_stores): a
// run's stores of 8, 4, 2 and 1 bytes 6 entries each, a byte's AND 12.
static uint64_t check_lower_padding_cost(const uint64_t* entries, uint32_t count)
{
    uint64_t cost = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint64_t run = (entries[i] >> 8) & 0xffffff;
        const bool zeros = (entries[i] & 0xff) == 0;
        cost += zeros ? (run / 8 + (uint64_t)__builtin_popcountll(run & 7)) * 6 : 12;
    }
    return cost;
}

// The plan of the __builtin_clear_padding call at `node`: its object's elements, through arrays of arrays, each of a
// stride's bytes (a variable length array's none), and the element's entries, in place where they are few, else a pool's.
static bool check_lower_padding_plan(CheckLowerInitializers* pools, CheckLowerPadding* plan, const CheckProgram* program, uint32_t node, Arena* output,
                                     Arena* scratch)
{
    const TypeTable* types = program->types;
    const CheckGraph* graph = &program->input->graph;
    const uint32_t first = graph->argument_offsets[node];
    const TypeId pointer = graph->argument_counts[node] != 0 ? type_strip_aligned(types, program->stream->argument_conversions[first].type) : 0;
    TypeId element = pointer != 0 && type_kind(types, pointer) == BUSTER_TYPE_KIND_POINTER ? type_element(types, pointer) : 0;
    uint64_t elements = 1;
    uint8_t kind = element != 0 ? type_kind(types, type_strip_aligned(types, element)) : BUSTER_TYPE_KIND_NONE;
    while (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        const TypeId bare = type_strip_aligned(types, element);
        elements *= types->types.extent[type_index(bare)];
        element = types->types.base[type_index(bare)];
        kind = type_kind(types, type_strip_aligned(types, element));
    }
    const bool none = element == 0 || kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY || elements == 0;
    *plan = (CheckLowerPadding){ .node = node, .pool = BUSTER_CHECK_NONE, .elements = none ? 0 : elements, .stride = none ? 0 : type_size(types, element) };
    plan->refusal = element == 0 ? "__builtin_clear_padding of no object type" : NULL;
    bool valid = true;
    if (!none)
    {
        const uint64_t mark = scratch->length;
        uint64_t* entries = NULL;
        const char* refusal = NULL;
        const uint32_t count = check_lower_padding_entries(types, element, plan->stride, scratch, &entries, &refusal);
        valid = count != UINT32_MAX || refusal != NULL;
        plan->refusal = refusal;
        uint64_t* kept = count != UINT32_MAX && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, count) : NULL;
        valid = valid && (count == UINT32_MAX || count == 0 || kept != NULL);
        if (valid && kept != NULL)
        {
            memcpy(kept, entries, (uint64_t)count * sizeof(*kept));
            plan->entries = kept;
            plan->count = count;
            // A table's pool: the entries' bytes, read by the recipe's loop.
            plan->pool = check_lower_padding_cost(kept, count) > BUSTER_CHECK_LOWER_PADDING_INLINE ? pools->count : BUSTER_CHECK_NONE;
            if (plan->pool != BUSTER_CHECK_NONE)
            {
                pools->items[pools->count++] = (CheckLowerInitPool){ .bytes = (const uint8_t*)kept, .size = count * 8, .part = BUSTER_CHECK_NONE };
            }
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// The plans of the unit's __builtin_clear_padding calls. False where the arenas are full.
static bool check_lower_padding_prepare(CheckLowerInitializers* pools, const CheckProgram* program, Arena* output, Arena* scratch)
{
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = program->input != NULL ? &program->input->graph : NULL;
    uint32_t clears = 0;
    bool valid = true;
    for (uint32_t i = 0; stream != NULL && graph != NULL && i < stream->misc_count; i += 1)
    {
        const CheckBuiltinMiscEffect* effect = stream->misc + i;
        clears += effect->node < graph->count && graph->ops[effect->node] == CHECK_OP_BUILTIN_MISC && effect->runtime_builtin == CBUILTIN_CLEAR_PADDING;
    }
    if (clears != 0)
    {
        CheckLowerInitPool* items = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerInitPool, (uint64_t)pools->count + clears);
        pools->paddings = BUSTER_ARENA_PUSH_ARRAY(output, CheckLowerPadding, clears);
        valid = items != NULL && pools->paddings != NULL;
        if (valid && pools->count != 0)
        {
            memcpy(items, pools->items, (uint64_t)pools->count * sizeof(*items));
        }
        pools->items = valid ? items : pools->items;
        for (uint32_t i = 0; valid && i < stream->misc_count; i += 1)
        {
            const CheckBuiltinMiscEffect* effect = stream->misc + i;
            if (effect->node < graph->count && graph->ops[effect->node] == CHECK_OP_BUILTIN_MISC && effect->runtime_builtin == CBUILTIN_CLEAR_PADDING)
            {
                valid = check_lower_padding_plan(pools, pools->paddings + pools->padding_count, program, effect->node, output, scratch);
                pools->padding_count += valid;
            }
        }
    }
    return valid;
}

// The plan of the __builtin_clear_padding call at `node`, or NULL: a unit has few of them.
static const CheckLowerPadding* check_lower_padding_find(const CheckLowerInitializers* pools, uint32_t node)
{
    const CheckLowerPadding* result = NULL;
    for (uint32_t p = 0; result == NULL && p < pools->padding_count; p += 1)
    {
        result = pools->paddings[p].node == node ? pools->paddings + p : NULL;
    }
    return result;
}
