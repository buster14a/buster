// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// __builtin_clear_padding's stores (#148, #1842), planned before the function lanes run, as clang's PaddingClearer
// (clang/lib/CodeGen/CGBuiltin.cpp) finds them: the bits no scalar, bit-field or vector of the object holds (an unnamed
// bit-field's among them, a union's bits that none of its members holds, an x87 long double's last 48, a _BitInt's past
// its width, an atomic type's past its value's) are zeros. An array's elements, through arrays of arrays, are cleared in
// a loop by their element's stores, and a variable length array is left as it is, as clang's code leaves it.
//
// The walk gives each held piece of the element as an interval of bits, in an explicit stack's order (no recursion). An
// inner array outside any union is held whole, and its elements' padding is a task of its own, walked after, whose
// entries the array's dimensions repeat (#1842): the plan grows with the type's description, not its layout, so an
// element of any size and any number of runs has one. A union's array, whose members' bits overlap, has its first element
// walked and its intervals copied for the others, or merged into one where the element has no padding. The intervals
// sorted and merged, each byte no interval touches is a zero, in runs, and each that one touches in part an AND of its
// held bits. A plan of few stores is the call's own recipe's: its repeats unrolled where their instances are few, else
// each shared repeat a nest of loops of its stores. A longer one is a table of its entries in a read-only pool of the
// unit's (CheckLowerInitializers.items), which the recipe's loop reads.
//
// This is a cold, per-call plan of a type's layout, made once a call before the lanes run; it stays scalar.

// The walk's first room for the nesting of a type's arrays, records, complex and atomic types, which grows as it needs
// (#1842); the cost, in typed entries, of the stores a call's recipe makes itself, within a builtin's
// BUSTER_CBUILTIN_LOWER_ENTRIES; and the words of an entry (CBuiltinMiscLowerCall.padding's).
#define BUSTER_CHECK_LOWER_PADDING_DEPTH 64u
#define BUSTER_CHECK_LOWER_PADDING_INLINE 460u
#define BUSTER_CHECK_LOWER_PADDING_WORDS BUSTER_CBUILTIN_PADDING_WORDS

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

// The declarator token of member `member` of record tag `tag` in its body, 0 where it isn't found.
static uint32_t check_lower_padding_member_token(const CheckProgram* program, uint32_t tag, uint32_t member)
{
    const TypeTable* types = program->types;
    uint32_t open = BUSTER_CHECK_NONE;
    uint32_t close = BUSTER_CHECK_NONE;
    uint32_t token = 0;
    if (types->members.name[member] != 0 && check_lower_dump_body(program, tag, &open, &close))
    {
        // Its declarator: the body's earlier members' own first, as they come in order.
        uint32_t cursor = open + 1;
        for (uint32_t m = types->tags.member_begin[tag]; m <= member; m += 1)
        {
            token = types->members.name[m] != 0 ? check_lower_dump_member(program, &cursor, close, types->members.name[m]) : token;
        }
    }
    return token;
}

// The sugar depth of member `member` of record `tag` (check_lower_padding_opaque of its declarator), UINT32_MAX for none or
// where its declarator isn't found: only an array member's is looked for.
static uint32_t check_lower_padding_member(const CheckProgram* program, uint32_t tag, uint32_t member)
{
    const TypeTable* types = program->types;
    const bool array = type_kind_is_array(type_kind(types, type_strip_aligned(types, types->members.type[member])));
    const uint32_t token = array ? check_lower_padding_member_token(program, tag, member) : 0;
    return token != 0 ? check_lower_padding_opaque(program, token, 0) : UINT32_MAX;
}

// The sugar of the arrays a __builtin_clear_padding argument points to (#1842's 1b), as check_lower_padding_opaque counts
// it, UINT32_MAX for none: through implicit conversions (an array's decay keeps the level), `&` (one less), `*` and
// subscripts (one more), to
// - a declared name, or a member (`.` or `->`) of a record, whose declarator's sugar it reads at its level;
// - a cast's type name, or a call's result, whose array that is a pointer's pointee is always written with sugar (a
//   declarator's parentheses, a typedef or typeof), and an element of an array not (an array of a typedef's arrays is
//   the exception this doesn't see).
static uint32_t check_lower_padding_argument(const CheckProgram* program, uint32_t node)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckInput* input = program->input;
    const TypeTable* types = program->types;
    // The operands from the argument down, then their levels back up.
    int32_t level = 1;
    bool walking = true;
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
        walking = (convert && (decay || same)) || op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT;
        level += !walking ? 0 : op == CHECK_OP_ADDRESS ? -1 : op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT ? 1 : 0;
        node = walking ? operand : node;
    }
    const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
    const uint32_t depth = level >= 0 ? (uint32_t)level : UINT32_MAX;
    uint32_t result = UINT32_MAX;
    if (depth != UINT32_MAX && op == CHECK_OP_VALUE && graph->tokens[node] < input->token_count && input->declarations != NULL &&
        input->declarations[graph->tokens[node]] < input->token_count)
    {
        result = check_lower_padding_opaque(program, input->declarations[graph->tokens[node]], depth);
    }
    else if (depth != UINT32_MAX && (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW) && graph->members != NULL && program->stream != NULL)
    {
        // The member's record, and the record's tag that declares it (an anonymous member's own).
        const uint32_t base = graph->operand0[node];
        const TypeId base_type = base < program->stream->count ? type_unqualified(type_strip_aligned(types, check_stream_type(program->stream, base))) : 0;
        const TypeId record = op == CHECK_OP_ARROW && base_type != 0 && type_kind(types, base_type) == BUSTER_TYPE_KIND_POINTER ?
                              type_unqualified(type_strip_aligned(types, type_element(types, base_type))) : op == CHECK_OP_MEMBER ? base_type : 0;
        const uint8_t kind = record != 0 ? type_kind(types, record) : BUSTER_TYPE_KIND_NONE;
        uint64_t offset = 0;
        const uint32_t member = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ? type_find_member(types, record, graph->members[node], &offset) : 0;
        uint32_t tag = types->tags.count;
        for (uint32_t t = 0; member != 0 && tag == types->tags.count && t < types->tags.count; t += 1)
        {
            tag = member >= types->tags.member_begin[t] && member - types->tags.member_begin[t] < types->tags.member_count[t] ? t : tag;
        }
        const uint32_t token = tag < types->tags.count ? check_lower_padding_member_token(program, tag, member) : 0;
        result = token != 0 ? check_lower_padding_opaque(program, token, depth) : UINT32_MAX;
    }
    else if (depth != UINT32_MAX && depth != 0 && (op == CHECK_OP_CAST || op == CHECK_OP_CALL) && program->stream != NULL && node < program->stream->count)
    {
        // The type's level `depth`, and whether a pointer, not an array, has it: the array there is written with sugar.
        TypeId at = type_strip_aligned(types, check_stream_type(program->stream, node));
        bool pointed = false;
        for (uint32_t k = 0; k < depth && at != 0 && type_is_valid(types, at); k += 1)
        {
            const uint8_t kind = type_kind(types, at);
            pointed = kind == BUSTER_TYPE_KIND_POINTER;
            at = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? type_strip_aligned(types, type_element(types, at)) : 0;
        }
        result = at != 0 && pointed && type_kind_is_array(type_kind(types, at)) ? 0 : UINT32_MAX;
    }
    return result;
}

// The plan of a call's stores on an element of `type`, the size of `size` bytes: its table (CBuiltinMiscLowerCall.padding's:
// the header, the entries, then the pairs of their repeats) in `scratch`'s top. Returns the entries' count, with
// `*entry_words` their words and `*result_words` the table's, or UINT32_MAX with `*refusal` set where the table can't be a
// pool, or the arena is full. The walk's stack grows in `scratch` with the type's nesting, which a type table of 32-bit
// ids bounds.
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
    // Whether a union holds it, whose members' bits overlap: an array there is walked whole, its elements' intervals
    // copied, and none is a task of its own.
    bool in_union;
};

// A part of the element the plan walks on its own (#1842): an array outside any union, its first instance at `offset`
// bytes, its elements' padding entries of its own, repeated by its dimensions after the `dims_count` pairs from
// `dims_first` that repeat it already, where its parent holds it whole. The element itself is the first, of none.
typedef struct CheckLowerPaddingTask CheckLowerPaddingTask;
struct CheckLowerPaddingTask
{
    TypeId type;
    uint64_t offset;
    uint32_t dims_first;
    uint32_t dims_count;
    uint32_t opaque;
};

// Room for `needed` words in `*words` of `*capacity`, which grows to the next power of 2 that holds them, `count` copied.
// False where the arena is full.
static bool check_lower_padding_words(Arena* scratch, uint64_t** words, uint64_t* capacity, uint64_t count, uint64_t needed)
{
    uint64_t room = *capacity;
    while (room < needed)
    {
        room *= 2;
    }
    uint64_t* grown = room != *capacity ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, room) : *words;
    const bool valid = grown != NULL;
    if (valid && grown != *words)
    {
        memcpy(grown, *words, count * sizeof(*grown));
        *words = grown;
        *capacity = room;
    }
    return valid;
}

// The longest run of zeros an entry holds, its bytes' field's.
#define BUSTER_CHECK_LOWER_PADDING_RUN ((UINT64_C(1) << 56) - 1)

// `program`, where not NULL, gives the sugar of the record members' arrays, and `opaque` the element's own
// (CheckLowerPaddingFrame.opaque): an array its sugar makes one scalar is all occupied, as clang's walk has it.
static uint32_t check_lower_padding_entries(const CheckProgram* program, const TypeTable* types, TypeId type, uint64_t size, uint32_t opaque, Arena* scratch,
                                            uint64_t** result, uint64_t* entry_words, uint64_t* result_words, const char** refusal)
{
    typedef CheckLowerPaddingFrame Frame;
    typedef CheckLowerPaddingTask Task;
    uint64_t room = BUSTER_CHECK_LOWER_PADDING_DEPTH;
    Frame* stack = BUSTER_ARENA_PUSH_ARRAY(scratch, Frame, room);
    uint64_t capacity = 64;
    CheckLowerPaddingBits* bits = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    CheckLowerPaddingBits* spare = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPaddingBits, capacity);
    uint64_t task_capacity = 16;
    Task* tasks = BUSTER_ARENA_PUSH_ARRAY(scratch, Task, task_capacity);
    uint32_t task_count = 0;
    // The tasks' repeats' pairs, and the entries' words.
    uint64_t dims_capacity = 64;
    uint64_t* dims = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, dims_capacity);
    uint64_t dims_words = 0;
    uint64_t words_capacity = 256;
    uint64_t* words = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words_capacity);
    uint64_t word_count = 0;
    uint64_t entry_count = 0;
    bool valid = stack != NULL && bits != NULL && spare != NULL && tasks != NULL && dims != NULL && words != NULL && size <= UINT64_MAX / 8;
    *refusal = valid || size <= UINT64_MAX / 8 ? NULL : "__builtin_clear_padding of an element past 2^61 bytes";
    if (valid)
    {
        tasks[task_count++] = (Task){ .type = type, .offset = 0, .opaque = opaque };
    }
    for (uint32_t t = 0; valid && t < task_count; t += 1)
    {
        const Task task = tasks[t];
        // Its repeats: its parent's, then each of its own array levels, outermost first, down to the first that isn't one or
        // whose sugar makes it one scalar.
        const uint64_t first_dim = dims_words / 2;
        valid = check_lower_padding_words(scratch, &dims, &dims_capacity, dims_words, dims_words + 2 * (uint64_t)task.dims_count);
        if (valid && task.dims_count != 0)
        {
            memcpy(dims + dims_words, dims + 2 * (uint64_t)task.dims_first, 2 * (uint64_t)task.dims_count * sizeof(*dims));
            dims_words += 2 * (uint64_t)task.dims_count;
        }
        TypeId element = task.type;
        uint32_t level_opaque = task.opaque;
        bool empty = false;
        while (valid && level_opaque != 0 && type_kind(types, type_strip_aligned(types, element)) == BUSTER_TYPE_KIND_ARRAY)
        {
            const TypeId bare = type_strip_aligned(types, element);
            const TypeId inner = types->types.base[type_index(bare)];
            valid = check_lower_padding_words(scratch, &dims, &dims_capacity, dims_words, dims_words + 2);
            if (valid)
            {
                dims[dims_words] = types->types.extent[type_index(bare)];
                dims[dims_words + 1] = type_size(types, inner);
                empty |= dims[dims_words] == 0 || dims[dims_words + 1] == 0;
                dims_words += 2;
            }
            element = inner;
            level_opaque = level_opaque != UINT32_MAX ? level_opaque - 1 : UINT32_MAX;
        }
        const uint32_t dim_count = (uint32_t)(dims_words / 2 - first_dim);
        const uint64_t element_size = t == 0 ? size : type_size(types, element);
        // The element's held bits, walked by an explicit stack.
        uint32_t count = 0;
        uint32_t depth = 1;
        stack[0] = (Frame){ .offset = 0, .type = element, .index = 0, .mark = 0, .opaque = level_opaque };
        while (valid && !empty && depth != 0)
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
            else if (kind == BUSTER_TYPE_KIND_ARRAY && !top->in_union && depth > 1)
            {
                // An array outside any union is held whole here, its elements' padding a task's (#1842), repeated by its
                // dimensions: the element's entries keep the type's size, not its layout's.
                first = top->offset;
                last = top->offset + type_size(types, bare) * 8;
                if (task_count == task_capacity)
                {
                    Task* grown = BUSTER_ARENA_PUSH_ARRAY(scratch, Task, 2 * task_capacity);
                    valid = grown != NULL;
                    if (valid)
                    {
                        memcpy(grown, tasks, task_capacity * sizeof(*tasks));
                        tasks = grown;
                        task_capacity *= 2;
                    }
                }
                if (valid)
                {
                    tasks[task_count++] = (Task){
                        .type = top->type,
                        .offset = task.offset + top->offset / 8,
                        .dims_first = (uint32_t)first_dim,
                        .dims_count = dim_count,
                        .opaque = top->opaque,
                    };
                }
                depth -= 1;
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_COMPLEX)
            {
                // A union's array, or a complex type: its first element is walked; then its intervals are its elements',
                // each element's own copy, or, where they hold all of it, the array's one.
                const TypeId inner = types->types.base[index];
                const uint64_t elements = kind == BUSTER_TYPE_KIND_COMPLEX ? 2 : types->types.extent[index];
                const uint64_t stride = type_size(types, inner) * 8;
                if (top->index == 0 && elements != 0)
                {
                    top->mark = count;
                    pushed = inner;
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
                    const uint64_t total = whole ? (uint64_t)count : (uint64_t)count + (uint64_t)each * (elements - 1);
                    valid = valid && total <= UINT32_MAX / 2;
                    *refusal = valid ? NULL : "__builtin_clear_padding of a union whose arrays hold more than 2147483647 runs of padding";
                    if (valid && whole)
                    {
                        bits[mark].last = top->offset + stride * elements;
                    }
                    else if (valid && each != 0)
                    {
                        valid = check_lower_padding_room(scratch, &bits, &spare, &capacity, count, total);
                        *refusal = valid ? NULL : "__builtin_clear_padding of a union whose arrays' runs of padding fill the arena";
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
                const TypeId inner = vector ? types->types.base[index] : bare;
                const uint64_t elements = vector ? types->types.extent[index] : 1;
                const uint64_t each = vector && type_unqualified(type_strip_aligned(types, inner)) == BUSTER_TYPE_BOOL && kind == BUSTER_TYPE_KIND_EXT_VECTOR ? 1 :
                                      kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? 0 : check_lower_occupied_bits(types, inner);
                first = top->offset;
                last = top->offset + each * elements;
                depth -= 1;
            }
            const bool laid = last <= element_size * 8 && kind != BUSTER_TYPE_KIND_VARIABLE_ARRAY;
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
                    top = stack + depth - 1;
                    room *= 2;
                }
            }
            if (valid && pushed != 0)
            {
                stack[depth] = (Frame){ .offset = at, .type = pushed, .index = 0, .mark = 0, .opaque = pushed_opaque,
                                        .in_union = top->in_union || kind == BUSTER_TYPE_KIND_UNION };
                depth += 1;
            }
        }
        count = valid && !empty ? check_lower_padding_normalize(bits, spare, 0, count) : 0;
        // Its entries: at most a zero run and two bytes' ANDs an interval and a run before the end, each repeated, where the
        // task has dimensions, by them: the innermost's count and stride, the others' combinations, and where their pairs are.
        const uint64_t inner_count = dim_count != 0 ? dims[dims_words - 2] : 1;
        const uint64_t inner_stride = dim_count != 0 ? dims[dims_words - 1] : 0;
        uint64_t outer_total = 1;
        for (uint32_t d = 0; d + 1 < dim_count; d += 1)
        {
            outer_total *= dims[2 * (first_dim + d)];
        }
        const uint64_t repeat[6] = { inner_count, inner_stride, outer_total, first_dim, dim_count != 0 ? dim_count - 1 : 0, 0 };
        // The byte whose bits an interval holds in part, until no later one can touch it, and the next byte none has touched.
        uint64_t pending = UINT64_MAX;
        uint32_t mask = 0;
        uint64_t next = 0;
        for (uint32_t i = 0; valid && !empty && i <= count; i += 1)
        {
            const uint64_t a = i < count ? bits[i].first : element_size * 8;
            const uint64_t b = i < count ? bits[i].last : element_size * 8;
            const uint64_t byte = a / 8;
            // An entry's byte and its bytes and mask word, made below at most three times an interval.
            uint64_t made[3][2];
            uint32_t made_count = 0;
            if (pending != UINT64_MAX && pending < byte)
            {
                if (mask != 0xff)
                {
                    made[made_count][0] = pending;
                    made[made_count][1] = (uint64_t)1 << 8 | mask;
                    made_count += 1;
                }
                next = pending + 1;
                pending = UINT64_MAX;
            }
            if (pending == UINT64_MAX && next < byte)
            {
                made[made_count][0] = next;
                made[made_count][1] = (byte - next) << 8;
                made_count += 1;
                next = byte;
            }
            if (i < count)
            {
                // The interval's first byte, then its last, whose later bits a later interval may hold.
                const uint64_t final = (b - 1) / 8;
                const uint32_t head = byte == final ? (uint32_t)(((1u << ((b - 1) % 8 + 1)) - 1) & ~((1u << (a % 8)) - 1)) : (uint32_t)(0xffu & ~((1u << (a % 8)) - 1));
                const uint32_t merged = (pending == byte ? mask : 0) | head;
                if (byte != final && merged != 0xff)
                {
                    made[made_count][0] = byte;
                    made[made_count][1] = (uint64_t)1 << 8 | merged;
                    made_count += 1;
                }
                pending = final;
                mask = byte == final ? merged : (uint32_t)((1u << ((b - 1) % 8 + 1)) - 1);
                next = final + 1;
            }
            for (uint32_t m = 0; valid && m < made_count; m += 1)
            {
                // A run of zeros past the field's longest is pieces of it.
                const bool zeros = (made[m][1] & 0xff) == 0;
                uint64_t left = zeros ? made[m][1] >> 8 : 1;
                uint64_t from = made[m][0];
                while (valid && left != 0)
                {
                    const uint64_t piece = zeros && left > BUSTER_CHECK_LOWER_PADDING_RUN ? BUSTER_CHECK_LOWER_PADDING_RUN : left;
                    const uint64_t length = dim_count != 0 ? BUSTER_CHECK_LOWER_PADDING_WORDS : BUSTER_CBUILTIN_PADDING_PLAIN;
                    valid = check_lower_padding_words(scratch, &words, &words_capacity, word_count, word_count + length);
                    if (valid)
                    {
                        words[word_count] = (task.offset + from) | (dim_count != 0 ? BUSTER_CBUILTIN_PADDING_REPEATED : 0);
                        words[word_count + 1] = zeros ? piece << 8 : made[m][1];
                        memcpy(words + word_count + 2, repeat, (length - 2) * sizeof(*words));
                        word_count += length;
                        entry_count += 1;
                    }
                    from += piece;
                    left -= piece;
                }
            }
        }
    }
    // The table: the header, the entries, then the repeats' pairs, which a repeated entry's sixth word then indexes as the
    // table's words.
    const uint64_t header = BUSTER_CBUILTIN_PADDING_HEADER;
    const uint64_t total = header + word_count + dims_words;
    const bool fits = total <= UINT32_MAX / 8 && entry_count < UINT32_MAX;
    *refusal = !valid || fits ? *refusal : "__builtin_clear_padding of an element whose table of stores passes 4 GiB";
    valid = valid && fits;
    uint64_t* table = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, total) : NULL;
    valid = valid && table != NULL;
    if (valid)
    {
        const uint64_t nothing[BUSTER_CBUILTIN_PADDING_HEADER] = { 1, 0, 1, 0, 0, 0 };
        memcpy(table, nothing, sizeof(nothing));
        memcpy(table + header, words, word_count * sizeof(*table));
        memcpy(table + header + word_count, dims, dims_words * sizeof(*table));
        for (uint64_t at = header; at < header + word_count;)
        {
            uint64_t* entry = table + at;
            const bool repeated = (entry[0] & BUSTER_CBUILTIN_PADDING_REPEATED) != 0;
            if (repeated)
            {
                entry[5] = entry[6] != 0 ? header + word_count + 2 * entry[5] : 0;
            }
            at += repeated ? BUSTER_CHECK_LOWER_PADDING_WORDS : BUSTER_CBUILTIN_PADDING_PLAIN;
        }
    }
    *result = table;
    *entry_words = valid ? word_count : 0;
    *result_words = valid ? total : 0;
    return valid ? (uint32_t)entry_count : UINT32_MAX;
}

// The cost, in a builtin's typed entries, of an element's stores made in place (cbuiltin_misc_lower_padding_stores): a
// run's stores of 8, 4, 2 and 1 bytes 6 entries each, a byte's AND 12, and each nest of loops of a shared repeat
// BUSTER_CBUILTIN_PADDING_LOOP a level (#1842); UINT64_MAX where a repeat has more levels than the nest's room, which only
// the table's loop makes.
static uint64_t check_lower_padding_cost(const uint64_t* table, uint32_t count)
{
    uint64_t cost = 0;
    const uint64_t* entry = table + BUSTER_CBUILTIN_PADDING_HEADER;
    const uint64_t* group = NULL;
    for (uint32_t i = 0; cost != UINT64_MAX && i < count; i += 1)
    {
        const uint64_t run = entry[1] >> 8;
        const bool zeros = (entry[1] & 0xff) == 0;
        const bool repeated = (entry[0] & BUSTER_CBUILTIN_PADDING_REPEATED) != 0;
        const bool opens = repeated && (group == NULL || !cbuiltin_misc_lower_padding_shared(group, entry));
        const bool deep = repeated && entry[6] >= BUSTER_CBUILTIN_PADDING_LEVELS;
        const uint64_t loops = opens ? (entry[6] + 1) * BUSTER_CBUILTIN_PADDING_LOOP : 0;
        const uint64_t own = (zeros ? (run / 8 + (uint64_t)__builtin_popcountll(run & 7)) * 6 : 12) + loops;
        cost = deep || own > UINT64_MAX / 2 - cost ? UINT64_MAX : cost + own;
        group = repeated ? (opens ? entry : group) : NULL;
        entry += repeated ? BUSTER_CBUILTIN_PADDING_WORDS : BUSTER_CBUILTIN_PADDING_PLAIN;
    }
    return cost;
}

// A table's repeats unrolled into plain entries (#1842), where all their instances' stores cost at most what a call's
// recipe makes in place: a small inner array's few stores, not loops. The instances' offsets are the table loop's: each
// combination's digits by its pairs' counts, innermost first, each times its stride, then the innermost repeat's
// instances. The entries come out sorted by offset, adjacent runs of zeros joined, as the walk makes a plan without
// repeats. True where it unrolled them into `*result` (`*result_count` entries after the header), in `scratch`; false,
// and `*result` untouched, otherwise.
static bool check_lower_padding_unroll(const uint64_t* table, uint32_t count, Arena* scratch, uint64_t** result, uint32_t* result_count)
{
    uint64_t cost = 0;
    uint64_t total = 0;
    bool repeats = false;
    uint64_t at = BUSTER_CBUILTIN_PADDING_HEADER;
    for (uint32_t i = 0; cost <= BUSTER_CHECK_LOWER_PADDING_INLINE && i < count; i += 1)
    {
        const uint64_t* entry = table + at;
        const bool repeated = (entry[0] & BUSTER_CBUILTIN_PADDING_REPEATED) != 0;
        const uint64_t run = entry[1] >> 8;
        const bool zeros = (entry[1] & 0xff) == 0;
        const uint64_t own = zeros ? (run / 8 + (uint64_t)__builtin_popcountll(run & 7)) * 6 : 12;
        const uint64_t inner = repeated ? entry[2] : 1;
        const uint64_t outer = repeated ? entry[4] : 1;
        const bool small = own <= BUSTER_CHECK_LOWER_PADDING_INLINE && inner <= BUSTER_CHECK_LOWER_PADDING_INLINE && outer <= BUSTER_CHECK_LOWER_PADDING_INLINE;
        cost = small ? cost + own * inner * outer : UINT64_MAX;
        total += small ? inner * outer : 0;
        repeats |= repeated;
        at += repeated ? BUSTER_CBUILTIN_PADDING_WORDS : BUSTER_CBUILTIN_PADDING_PLAIN;
    }
    const bool unrolled = repeats && cost <= BUSTER_CHECK_LOWER_PADDING_INLINE;
    uint64_t* entries = unrolled ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, BUSTER_CBUILTIN_PADDING_HEADER + total * BUSTER_CBUILTIN_PADDING_PLAIN) : NULL;
    if (entries != NULL)
    {
        memcpy(entries, table, BUSTER_CBUILTIN_PADDING_HEADER * sizeof(*entries));
        uint64_t* made = entries + BUSTER_CBUILTIN_PADDING_HEADER;
        uint64_t made_count = 0;
        at = BUSTER_CBUILTIN_PADDING_HEADER;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint64_t* entry = table + at;
            const bool repeated = (entry[0] & BUSTER_CBUILTIN_PADDING_REPEATED) != 0;
            const uint64_t inner = repeated ? entry[2] : 1;
            const uint64_t outer = repeated ? entry[4] : 1;
            const uint64_t pairs = repeated ? entry[6] : 0;
            for (uint64_t c = 0; c < outer; c += 1)
            {
                uint64_t offset = entry[0] & ~BUSTER_CBUILTIN_PADDING_REPEATED;
                uint64_t rest = c;
                for (uint64_t p = pairs; p != 0; p -= 1)
                {
                    const uint64_t* pair = table + entry[5] + 2 * (p - 1);
                    offset += rest % pair[0] * pair[1];
                    rest /= pair[0];
                }
                for (uint64_t k = 0; k < inner; k += 1)
                {
                    // Placed by its offset among those made (few: at most the inline cost's sixth).
                    const uint64_t word = offset + k * (repeated ? entry[3] : 0);
                    uint64_t j = made_count;
                    while (j != 0 && made[2 * (j - 1)] > word)
                    {
                        made[2 * j] = made[2 * (j - 1)];
                        made[2 * j + 1] = made[2 * (j - 1) + 1];
                        j -= 1;
                    }
                    made[2 * j] = word;
                    made[2 * j + 1] = entry[1];
                    made_count += 1;
                }
            }
            at += repeated ? BUSTER_CBUILTIN_PADDING_WORDS : BUSTER_CBUILTIN_PADDING_PLAIN;
        }
        // Adjacent runs of zeros, one (an inner array's last element's padding and its parent's after it), as the walk
        // makes them without repeats.
        uint64_t kept = 0;
        for (uint64_t j = 0; j < made_count; j += 1)
        {
            const bool zeros = (made[2 * j + 1] & 0xff) == 0;
            const uint64_t* previous = kept != 0 ? made + 2 * (kept - 1) : NULL;
            const bool joined = previous != NULL && zeros && (previous[1] & 0xff) == 0 && previous[0] + (previous[1] >> 8) == made[2 * j] &&
                                (previous[1] >> 8) + (made[2 * j + 1] >> 8) <= BUSTER_CHECK_LOWER_PADDING_RUN;
            if (joined)
            {
                made[2 * (kept - 1) + 1] += made[2 * j + 1];
            }
            else
            {
                made[2 * kept] = made[2 * j];
                made[2 * kept + 1] = made[2 * j + 1];
                kept += 1;
            }
        }
        *result = entries;
        *result_count = (uint32_t)kept;
    }
    return entries != NULL;
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
    const uint32_t opaque = graph->argument_counts[node] != 0 ? check_lower_padding_argument(program, graph->argument_nodes[first]) : UINT32_MAX;
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
        uint64_t entry_words = 0;
        uint64_t words = 0;
        const char* refusal = NULL;
        uint32_t count = check_lower_padding_entries(program, types, element, plan->stride, opaque != UINT32_MAX && opaque <= stripped ? 0 : UINT32_MAX, scratch,
                                                     &entries, &entry_words, &words, &refusal);
        valid = count != UINT32_MAX || refusal != NULL;
        plan->refusal = refusal;
        // A small inner array's repeats, its stores made in place.
        const bool unrolled = count != UINT32_MAX && count != 0 && check_lower_padding_unroll(entries, count, scratch, &entries, &count);
        entry_words = unrolled ? (uint64_t)count * BUSTER_CBUILTIN_PADDING_PLAIN : entry_words;
        words = unrolled ? BUSTER_CBUILTIN_PADDING_HEADER + entry_words : words;
        uint64_t* kept = count != UINT32_MAX && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, words) : NULL;
        valid = valid && (count == UINT32_MAX || count == 0 || kept != NULL);
        if (valid && kept != NULL)
        {
            memcpy(kept, entries, words * sizeof(*kept));
            plan->entries = kept;
            plan->count = count;
            plan->words = entry_words;
            // A table's pool: the entries' and their repeats' words, read by the recipe's loop.
            plan->pool = check_lower_padding_cost(kept, count) > BUSTER_CHECK_LOWER_PADDING_INLINE ? pools->count : BUSTER_CHECK_NONE;
            if (plan->pool != BUSTER_CHECK_NONE)
            {
                pools->items[pools->count++] = (CheckLowerInitPool){ .bytes = (const uint8_t*)kept, .size = (uint32_t)(words * 8), .part = BUSTER_CHECK_NONE };
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
