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

// The walk's first room for the nesting of a type's arrays, records, complex and atomic types, which grows as it needs
// (#1842); the entries an element's plan may have; the cost, in typed entries, of the stores a call's recipe makes
// itself, within a builtin's BUSTER_CBUILTIN_LOWER_ENTRIES.
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

// Whether level `depth` of declaration `name`'s type (0 the type, each pointer or array level one more) is an array whose
// type carries sugar (#1842's 1b), which clang's PaddingClearer, testing the sugared type for a ConstantArrayType, treats
// as one scalar: a declarator group's ParenType around it (a PAREN node, c_types_sugar.c, the reader's in a unit that
// clears padding), or a typedef's or a typeof's specifier that spells it (check_input_specifier_spelling), whose own
// declaration answers for its levels below.
static bool check_lower_padding_sugared(const CheckProgram* program, uint32_t name, uint32_t depth)
{
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    bool result = false;
    for (uint32_t hops = 0; !result && hops < 16 && name < input->token_count && input->declared_types != NULL; hops += 1)
    {
        // The declaration's own groups.
        uint32_t level = 0;
        for (uint32_t node = check_input_declared_sugar(input, name), steps = 0; !result && node != 0 && node < input->sugar.count && steps < 64; steps += 1)
        {
            const uint8_t kind = input->sugar.kind[node];
            result = kind == BUSTER_TYPE_SUGAR_PAREN && level == depth && type_kind_is_array(type_kind(types, type_strip_aligned(types, input->sugar.canonical[node])));
            level += kind == BUSTER_TYPE_SUGAR_POINTER || kind == BUSTER_TYPE_SUGAR_ARRAY ? 1 : 0;
            node = input->sugar.inner[node];
        }
        // Its specifier's: the level whose type the typedef or typeof writes.
        const uint32_t spelling = !result ? check_input_specifier_spelling(input, program->parser, name) : 0;
        const TypeId written = spelling != 0 && spelling < input->token_count ? type_unqualified(type_strip_aligned(types, input->declared_types[spelling])) : 0;
        TypeId at = type_strip_aligned(types, input->declared_types[name]);
        uint32_t specifier = 0;
        for (; written != 0 && specifier < 64 && type_is_valid(types, at) && type_unqualified(at) != written; specifier += 1)
        {
            const uint8_t kind = type_kind(types, at);
            at = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? type_strip_aligned(types, type_element(types, at)) : 0;
        }
        const bool found = written != 0 && type_is_valid(types, at) && type_unqualified(at) == written && specifier <= depth;
        result = result || (found && specifier == depth && type_kind_is_array(type_kind(types, written)));
        // Below it, a typedef's own declaration's levels (a typeof's expression keeps its own, which this doesn't follow).
        const bool deeper = !result && found && specifier < depth && input->declaration_kinds[spelling] == C_BINDING_TYPEDEF;
        depth -= deeper ? specifier : 0;
        name = deeper ? spelling : BUSTER_CHECK_NONE;
    }
    return result;
}

// The first array level from `depth` of declaration `name`'s type that carries sugar (check_lower_padding_sugared),
// counted from `depth`, through its arrays; UINT32_MAX for none.
static uint32_t check_lower_padding_opaque(const CheckProgram* program, uint32_t name, uint32_t depth)
{
    const TypeTable* types = program->types;
    const CheckInput* input = program->input;
    TypeId level = name < input->token_count && input->declared_types != NULL ? type_strip_aligned(types, input->declared_types[name]) : 0;
    for (uint32_t k = 0; k < depth && level != 0 && type_is_valid(types, level); k += 1)
    {
        const uint8_t kind = type_kind(types, level);
        level = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? type_strip_aligned(types, type_element(types, level)) : 0;
    }
    uint32_t result = UINT32_MAX;
    for (uint32_t k = 0; result == UINT32_MAX && k < 64 && level != 0 && type_is_valid(types, level) && type_kind_is_array(type_kind(types, level)); k += 1)
    {
        result = check_lower_padding_sugared(program, name, depth + k) ? k : UINT32_MAX;
        level = type_strip_aligned(types, type_element(types, level));
    }
    return result;
}

// The declaration a __builtin_clear_padding argument points into and the level of its type it points to (#1842's 1b):
// through implicit conversions (an array's decay keeps the level), `&` (one less), `*` and subscripts (one more), to a
// declared name. False for any other operand (a cast's type name, a member, a call), whose sugar this doesn't follow.
static bool check_lower_padding_argument(const CheckProgram* program, uint32_t node, uint32_t* name, uint32_t* depth)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    // The operands from the argument down, then their levels back up.
    int32_t level = 1;
    bool walking = true;
    bool found = false;
    for (uint32_t steps = 0; walking && node < graph->count && steps < 32; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t operand = graph->operand0[node];
        const bool convert = op == CHECK_OP_CONVERT && operand < graph->count;
        // An implicit conversion keeps the pointee where it is an array's decay or keeps the pointer's own pointee.
        const TypeId to = convert ? type_strip_aligned(types, program->stream != NULL && node < program->stream->count ? check_stream_type(program->stream, node) : 0) : 0;
        const TypeId from = convert && operand < program->stream->count ? type_strip_aligned(types, check_stream_type(program->stream, operand)) : 0;
        const bool decay = convert && type_kind_is_array(type_kind(types, from)) && type_kind(types, to) == BUSTER_TYPE_KIND_POINTER;
        const bool same = convert && type_kind(types, from) == BUSTER_TYPE_KIND_POINTER && type_kind(types, to) == BUSTER_TYPE_KIND_POINTER &&
                          type_unqualified(type_strip_aligned(types, type_element(types, from))) == type_unqualified(type_strip_aligned(types, type_element(types, to)));
        level += op == CHECK_OP_ADDRESS ? -1 : op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT ? 1 : 0;
        found = op == CHECK_OP_VALUE && graph->tokens[node] < input->token_count && input->declarations != NULL;
        *name = found ? input->declarations[graph->tokens[node]] : BUSTER_CHECK_NONE;
        walking = (convert && (decay || same)) || op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT;
        node = walking ? operand : node;
    }
    *depth = level >= 0 ? (uint32_t)level : 0;
    return found && level >= 0 && *name < input->token_count;
}

// The sugar depth of member `member` of record `tag` (check_lower_padding_opaque of its declarator), UINT32_MAX for none or
// where its declarator isn't found: only an array member's is looked for.
static uint32_t check_lower_padding_member(const CheckProgram* program, uint32_t tag, uint32_t member)
{
    const TypeTable* types = program->types;
    uint32_t result = UINT32_MAX;
    uint32_t open = BUSTER_CHECK_NONE;
    uint32_t close = BUSTER_CHECK_NONE;
    const uint32_t name = types->members.name[member];
    if (name != 0 && type_kind_is_array(type_kind(types, type_strip_aligned(types, types->members.type[member]))) && check_lower_dump_body(program, tag, &open, &close))
    {
        // Its declarator: the body's earlier members' own first, as they come in order.
        uint32_t cursor = open + 1;
        uint32_t token = 0;
        for (uint32_t m = types->tags.member_begin[tag]; m <= member; m += 1)
        {
            token = types->members.name[m] != 0 ? check_lower_dump_member(program, &cursor, close, types->members.name[m]) : token;
        }
        result = token != 0 ? check_lower_padding_opaque(program, token, 0) : UINT32_MAX;
    }
    return result;
}

// The plan of a call's stores on an element of `type`, the size of `size` bytes: its entries, each the byte offset << 32,
// the run's bytes << 8 and the mask kept (0 for a run of zeros), in `scratch`'s top. Returns their count, or UINT32_MAX
// with `*refusal` set where an offset passes 4 GiB or its runs pass their limit, or the arena is full. The walk's stack
// grows in `scratch` with the type's nesting, which a type table of 32-bit ids bounds.
typedef struct CheckLowerPaddingFrame CheckLowerPaddingFrame;
struct CheckLowerPaddingFrame
{
    uint64_t offset;
    TypeId type;
    uint64_t index;
    uint32_t mark;
    // The array levels below this frame's type to the first whose sugar makes it one scalar (#1842's 1b), 0 for its own,
    // UINT32_MAX for none.
    uint32_t opaque;
};

// `program`, where not NULL, gives the sugar of the record members' arrays, and `opaque` the element's own
// (CheckLowerPaddingFrame.opaque): an array its sugar makes one scalar is all occupied, as clang's walk has it.
static uint32_t check_lower_padding_entries(const CheckProgram* program, const TypeTable* types, TypeId type, uint64_t size, uint32_t opaque, Arena* scratch,
                                            uint64_t** result, const char** refusal)
{
    typedef CheckLowerPaddingFrame Frame;
    uint64_t room = BUSTER_CHECK_LOWER_PADDING_DEPTH;
    Frame* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, Frame, room);
    uint64_t capacity = 64;
    CheckLowerPaddingBits* bits = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    CheckLowerPaddingBits* spare = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    uint32_t count = 0;
    uint32_t depth = 1;
    bool valid = stack != NULL && bits != NULL && spare != NULL && size <= UINT32_MAX;
    *refusal = valid ? NULL : size > UINT32_MAX ? "__builtin_clear_padding of an element past 4 GiB" : NULL;
    if (valid)
    {
        stack[0] = (Frame){ .offset = 0, .type = type, .index = 0, .mark = 0, .opaque = opaque };
    }
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
        uint32_t pushed_opaque = UINT32_MAX;
        valid = check_lower_padding_room(scratch, &bits, &spare, &capacity, count, (uint64_t)count + 1);
        if (top->opaque == 0)
        {
            // An array whose sugar makes it one scalar: all of it occupied.
            first = top->offset;
            last = top->offset + type_size(types, bare) * 8;
            depth -= 1;
        }
        else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_COMPLEX)
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
                pushed_opaque = top->opaque != UINT32_MAX && kind == BUSTER_TYPE_KIND_ARRAY ? top->opaque - 1 : UINT32_MAX;
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
            pushed_opaque = pushed != 0 && program != NULL ? check_lower_padding_member(program, tag, member) : UINT32_MAX;
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
        if (valid && pushed != 0 && depth == room)
        {
            // Twice the room, the frames copied: the arena's pushes are its own, so the old ones stay as garbage until the
            // call's plan pops the scratch.
            Frame* grown = BUSTER_ARENA_PUSH_ARRAY(scratch, Frame, 2 * room);
            valid = grown != NULL;
            if (valid)
            {
                memcpy(grown, stack, room * sizeof(*stack));
                stack = grown;
                room *= 2;
            }
        }
        if (valid && pushed != 0)
        {
            stack[depth] = (Frame){ .offset = at, .type = pushed, .index = 0, .mark = 0, .opaque = pushed_opaque };
            depth += 1;
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
    // The sugar of the arrays it points to (#1842's 1b): where one of them carries some, its elements are all occupied.
    uint32_t declaration = BUSTER_CHECK_NONE;
    uint32_t level = 0;
    const uint32_t opaque = graph->argument_counts[node] != 0 && check_lower_padding_argument(program, graph->argument_nodes[first], &declaration, &level) ?
                            check_lower_padding_opaque(program, declaration, level) : UINT32_MAX;
    uint32_t stripped = 0;
    while (kind == BUSTER_TYPE_KIND_ARRAY)
    {
        stripped += 1;
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
        const uint32_t count = check_lower_padding_entries(program, types, element, plan->stride, opaque != UINT32_MAX && opaque <= stripped ? 0 : UINT32_MAX, scratch,
                                                           &entries, &refusal);
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
