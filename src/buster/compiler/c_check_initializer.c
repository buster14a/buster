// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source initializer lists to ordered, checked subobject writes. Include after c_checker.c and c_check_literals.c.
// Token spans are half open. check_init_walk uses explicit stacks and retains expression nodes for automatic
// storage; check_initializer_infer_array shares the same subobject cursor before expression checking. Final
// reference liveness lives in check_initializer_live, shared by symbol preparation and data emission.
// Output owns the parts. The two arenas must be disjoint; new allocation suffixes must not overlap input arrays.
// Live input prefixes in either stable-address arena are permitted. Failed calls publish no partial parts.
// The source entry point retains warning positions and omits excess-list tails; the direct entry point is strict.
#ifndef BUSTER_C_CHECK_INITIALIZER_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_INCLUDED

typedef struct CheckInitString CheckInitString;
struct CheckInitString
{
    // An ordinary literal's object is token + 1; string_ids maps every adjacent part to this record's index.
    uint32_t object;
    uint32_t token;
    TypeId type;
    const uint8_t* bytes;
    uint64_t byte_count;
    // Predefined function-name arrays have static bytes, but C does not treat them as string-literal syntax.
    bool predefined;
};

typedef enum CheckInitKind
{
    CHECK_INIT_VALUE,
    CHECK_INIT_BYTES,
    CHECK_INIT_CLEAR,
} CheckInitKind;

typedef enum CheckInitStatus
{
    CHECK_INIT_OK,
    CHECK_INIT_INVALID,
    CHECK_INIT_CHECK_FAILED,
    CHECK_INIT_NON_CONSTANT,
    CHECK_INIT_UNSUPPORTED,
    CHECK_INIT_RESOURCE,
} CheckInitStatus;

typedef struct CheckInitPart CheckInitPart;
struct CheckInitPart
{
    CheckInitKind kind;
    TypeId type;
    uint32_t node;
    uint32_t token;
    // One-based selection in stream->graph->overload_candidates; zero for an ordinary expression.
    uint32_t overload_candidate;
    uint64_t bit_offset;
    uint32_t bit_width;
    const uint8_t* bytes;
    uint64_t byte_count;
    CheckValue value;
    CheckConversion conversion;
};

typedef struct CheckInitializer CheckInitializer;
struct CheckInitializer
{
    TypeId type;
    // Instance storage can extend a GNU flexible-array member without changing the declared type's sizeof.
    uint64_t object_size;
    CheckInitPart* parts;
    uint32_t count;
    CheckInitStatus status;
    uint32_t failed_token;
    uint32_t warning_count;
    uint32_t warning_token;
};

static __attribute__((unused)) bool check_initializer_run(TypeTable* types, const Parser* parser, const uint32_t* token_nodes,
                                  const CheckStream* stream, const CheckInitString* strings, uint32_t string_count, const uint32_t* string_ids,
                                  TypeId type, uint32_t begin, uint32_t end, bool static_storage,
                                  Arena* output, Arena* scratch, CheckInitializer* result);

static __attribute__((unused)) bool check_initializer_infer_array(TypeTable* types, const Parser* parser, const InternTable* identifiers, TypeId type,
                                          uint32_t begin, uint32_t end, const uint64_t* designator_indices, const uint8_t* designator_valid,
                                          const TypeId* expression_types, const TypeId* cast_types, Arena* scratch, TypeId* completed,
                                          uint32_t* failed_token, CheckInitStatus* status);

static CheckInitStatus check_initializer_part_bits(const TypeTable* types, const CheckInitPart* part, uint64_t* bits);
static CheckInitStatus check_initializer_live_writes(const TypeTable* types, const CheckInitializer* initialized, Arena* scratch, uint8_t** selected);

typedef struct CheckInitCopy CheckInitCopy;
typedef struct CheckInitFrame CheckInitFrame;
struct CheckInitFrame
{
    TypeId type;
    uint64_t offset;
    uint64_t next;
    uint32_t width;
    uint32_t resume;
    CheckInitCopy* copy;
    bool braced;
    bool wrapper;
};

typedef struct CheckInitSelection CheckInitSelection;
struct CheckInitSelection
{
    TypeId type;
    uint64_t offset;
    uint32_t width;
};

typedef struct CheckInitUnion CheckInitUnion;
struct CheckInitUnion
{
    TypeId type;
    uint64_t offset;
    uint64_t member;
};

struct CheckInitCopy
{
    uint64_t source;
    uint64_t destination;
    uint64_t bits;
    uint32_t first;
    CheckInitUnion* unions;
    uint32_t union_count;
    uint32_t union_capacity;
};

typedef struct CheckInitRange CheckInitRange;
struct CheckInitRange
{
    CheckInitRange* next;
    uint64_t first;
    uint64_t last;
    uint64_t current;
    uint32_t token;
};

typedef struct CheckInitRepeat CheckInitRepeat;
struct CheckInitRepeat
{
    CheckInitRepeat* previous;
    CheckInitRange* first;
    CheckInitRange* last;
    uint32_t token;
    uint32_t stop;
    uint32_t depth;
    uint32_t target_depth;
};

typedef struct CheckInitWalker CheckInitWalker;
typedef struct CheckInitExpression CheckInitExpression;
struct CheckInitExpression
{
    uint32_t first;
    uint32_t end;
    uint32_t node;
};
typedef struct CheckInitConstants CheckInitConstants;
typedef struct CheckInitObjects CheckInitObjects;
typedef struct CheckInitWarningSink CheckInitWarningSink;
struct CheckInitWarningSink
{
    uint32_t* tokens;
    uint32_t capacity;
    uint32_t count;
};

typedef struct CheckInitWarningCollector CheckInitWarningCollector;
struct CheckInitWarningCollector
{
    CheckInitWarningSink* sink;
    uint8_t* seen;
    uint32_t token_count;
};

struct CheckInitWalker
{
    TypeTable* types;
    const Parser* parser;
    const uint32_t* token_nodes;
    uint32_t token_nodes_first;
    uint32_t token_nodes_count;
    const CheckInitExpression* expressions;
    uint32_t expression_count;
    const CheckStream* stream;
    const CheckGraph* graph;
    const CheckInitConstants* constants;
    const CheckInitObjects* objects;
    const CheckConstValues* const_values;
    uint32_t source_position;
    CheckInitWarningCollector* warnings;
    const CheckInitString* strings;
    uint32_t string_count;
    const uint32_t* string_ids;
    const uint64_t* indices;
    const uint8_t* index_valid;
    const TypeId* expression_types;
    // An inferring walk's casts' types, at their `)`s (the reader's query types), or null; and the identifiers' table,
    // which spells an extension vector's swizzles.
    const TypeId* cast_types;
    const InternTable* identifiers;
    CheckInitFrame* frames;
    uint32_t depth;
    uint32_t capacity;
    CheckInitUnion* unions;
    uint32_t union_count;
    uint32_t union_capacity;
    CheckInitRepeat* repeat;
    uint64_t extent;
    TypeId incomplete;
    TypeId flexible;
    uint64_t flexible_offset;
    uint64_t flexible_extent;
    bool infer;
    bool static_storage;
    bool defer_constants;
    bool flexible_storage;
    bool ignore_excess;
    Arena* output;
    Arena* scratch;
    CheckInitializer* result;
};

static bool check_init_aggregate(const TypeTable* types, TypeId type)
{
    const uint8_t kind = type_kind(types, type_strip_aligned(types, type));
    return kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ||
           kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION ||
           kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
}

static void check_init_fail(CheckInitWalker* walker, CheckInitStatus status, uint32_t token)
{
    if (walker->result->status == CHECK_INIT_OK)
    {
        walker->result->status = status;
        walker->result->failed_token = token;
    }
}

static void check_init_warning(CheckInitWalker* walker, uint32_t token)
{
    if (walker->result->warning_count == 0)
    {
        walker->result->warning_token = token;
    }
    walker->result->warning_count += 1;
    CheckInitWarningCollector* warnings = walker->warnings;
    if (warnings != NULL && token < warnings->token_count && !warnings->seen[token])
    {
        CheckInitWarningSink* sink = warnings->sink;
        if (sink->count == sink->capacity)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, token);
        }
        else
        {
            sink->tokens[sink->count++] = token;
            warnings->seen[token] = 1;
        }
    }
}

// The extensions of an initializer at `token`: the graph's, and a system header's leniency where the token is in one;
// without a graph, check_input_expression_extensions' (c_check_constant.c, included after this file).
static uint32_t check_init_extensions(const CheckInitWalker* walker, uint32_t token)
{
    const uint32_t system = walker->graph != NULL && check_graph_system_token(walker->graph, token) ? BUSTER_CHECK_EXTENSION_SYSTEM_HEADER : 0;
    return walker->ignore_excess ? (walker->graph != NULL ? walker->graph->extensions | system :
           BUSTER_CHECK_EXTENSION_GNU_CONSTANT | (walker->parser->language.gnu ? BUSTER_CHECK_EXTENSION_GNU : 0)) : 0;
}

static void check_init_append(CheckInitWalker* walker, const CheckInitPart* part)
{
    if (!walker->infer && walker->result->status == CHECK_INIT_OK)
    {
        CheckInitPart* slot = BUSTER_ARENA_PUSH_ARRAY(walker->output, CheckInitPart, 1);
        if (slot == 0 || walker->result->count == UINT32_MAX)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, part->token);
        }
        else
        {
            if (walker->result->count == 0)
            {
                walker->result->parts = slot;
            }
            *slot = *part;
            walker->result->count += 1;
        }
    }
}

static void check_init_clear(CheckInitWalker* walker, TypeId type, uint64_t offset, uint32_t token)
{
    if (type_is_complete(walker->types, type))
    {
        const CheckInitPart part = {
            .kind = CHECK_INIT_CLEAR,
            .type = type,
            .node = BUSTER_CHECK_NONE,
            .token = token,
            .bit_offset = offset,
            .byte_count = type_size(walker->types, type),
        };
        check_init_append(walker, &part);
    }
}

static void check_init_flexible_reset(CheckInitWalker* walker, uint32_t token)
{
    uint32_t written = 0;
    for (uint32_t i = 0; !walker->infer && walker->result->status == CHECK_INIT_OK && i < walker->result->count; i += 1)
    {
        CheckInitPart part = walker->result->parts[i];
        uint64_t bits = 0;
        const CheckInitStatus status = check_initializer_part_bits(walker->types, &part, &bits);
        if (status != CHECK_INIT_OK)
        {
            check_init_fail(walker, status, token);
        }
        else if (part.bit_offset < walker->flexible_offset)
        {
            if (part.bit_offset + bits > walker->flexible_offset)
            {
                if (part.kind == CHECK_INIT_CLEAR || part.kind == CHECK_INIT_BYTES)
                {
                    part.byte_count = (walker->flexible_offset - part.bit_offset) / 8;
                    part.bit_width = 0;
                }
                else
                {
                    check_init_fail(walker, CHECK_INIT_INVALID, token);
                }
            }
            walker->result->parts[written++] = part;
        }
    }
    if (!walker->infer && walker->result->parts != 0)
    {
        walker->result->count = written;
        arena_pop_to(walker->output, (uint64_t)((uint8_t*)(walker->result->parts + written) - walker->output->base));
    }
    walker->flexible_extent = 0;
}

static bool check_init_select(CheckInitWalker* walker, CheckInitFrame* frame, CheckInitSelection* selected)
{
    const TypeTable* types = walker->types;
    const TypeId base = type_strip_aligned(types, frame->type);
    const uint8_t kind = type_kind(types, base);
    bool valid = false;
    *selected = (CheckInitSelection){
        .type = frame->type,
        .offset = frame->offset,
        .width = frame->width,
    };
    if (frame->wrapper || !check_init_aggregate(types, base))
    {
        valid = frame->next == 0;
    }
    else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ||
             kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        selected->type = type_element(types, base);
        const bool packed = kind == BUSTER_TYPE_KIND_EXT_VECTOR && type_unqualified(selected->type) == BUSTER_TYPE_BOOL;
        const uint64_t size = type_size(types, selected->type);
        const uint64_t bits = packed ? 1 : size * 8;
        valid = (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY || frame->next < types->types.extent[type_index(base)]) &&
                size != 0 && size <= UINT64_MAX / 8 && frame->next <= (UINT64_MAX - frame->offset) / bits;
        if (valid)
        {
            selected->offset += frame->next * bits;
            selected->width = packed ? 1 : 0;
        }
    }
    else
    {
        const uint32_t tag = types->types.index[type_index(base)];
        const uint32_t begin = types->tags.member_begin[tag];
        const uint32_t count = types->tags.member_count[tag];
        while (frame->next < count)
        {
            const uint32_t member = begin + (uint32_t)frame->next;
            const bool bit_field = (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
            const bool flexible = type_kind(types, type_strip_aligned(types, types->members.type[member])) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
            const bool flexible_root = flexible && walker->flexible_storage && frame->copy == 0 &&
                                       kind == BUSTER_TYPE_KIND_STRUCT && frame == walker->frames + 1 && frame->braced &&
                                       frame->offset == 0 && frame->next + 1 == count;
            if (!bit_field || types->members.name[member] != 0)
            {
                selected->type = types->members.type[member];
                selected->width = bit_field ? types->members.bit_width[member] : 0;
                valid = types->members.offset[member] <= UINT64_MAX - frame->offset;
                selected->offset += valid ? types->members.offset[member] : 0;
                if (valid && flexible_root)
                {
                    walker->flexible = type_strip_aligned(types, selected->type);
                    walker->flexible_offset = selected->offset;
                }
                break;
            }
            frame->next += 1;
        }
    }
    return valid;
}

static bool check_init_storage(const CheckInitWalker* walker, const CheckInitSelection* selected)
{
    const TypeId type = type_strip_aligned(walker->types, selected->type);
    return type_kind(walker->types, type) != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ||
           (type == walker->incomplete && selected->offset == 0) ||
           (type == walker->flexible && selected->offset == walker->flexible_offset);
}

static uint32_t check_init_union_slot(const CheckInitUnion* entries, uint32_t capacity, TypeId type, uint64_t offset)
{
    uint64_t hash = (offset >> 3) ^ ((uint64_t)type * UINT64_C(0x9e3779b97f4a7c15));
    hash ^= hash >> 32;
    uint32_t slot = (uint32_t)hash & (capacity - 1);
    while (entries[slot].type != 0 && (entries[slot].type != type || entries[slot].offset != offset))
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static void check_init_used(CheckInitWalker* walker, const CheckInitFrame* frame, uint32_t token, bool clear_union)
{
    const TypeId type = type_strip_aligned(walker->types, frame->type);
    if (!frame->wrapper && type == walker->incomplete && frame->offset == 0 && frame->next >= walker->extent)
    {
        if (frame->next == UINT64_MAX)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, token);
        }
        else
        {
            walker->extent = frame->next + 1;
        }
    }
    if (!frame->wrapper && type == walker->flexible && frame->offset == walker->flexible_offset && frame->next >= walker->flexible_extent)
    {
        if (frame->next == UINT64_MAX)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, token);
        }
        else
        {
            walker->flexible_extent = frame->next + 1;
        }
    }
    if (!frame->wrapper && type_kind(walker->types, type) == BUSTER_TYPE_KIND_UNION)
    {
        if (walker->union_count == walker->union_capacity / 2)
        {
            const uint32_t capacity = walker->union_capacity == 0 ? 16 : walker->union_capacity <= UINT32_MAX / 2 ? walker->union_capacity * 2 : 0;
            CheckInitUnion* entries = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitUnion, capacity) : 0;
            if (entries == 0)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, token);
            }
            else
            {
                memset(entries, 0, capacity * sizeof(*entries));
                for (uint32_t i = 0; i < walker->union_capacity; i += 1)
                {
                    const CheckInitUnion* previous = walker->unions + i;
                    if (previous->type != 0)
                    {
                        const uint32_t slot = check_init_union_slot(entries, capacity, previous->type, previous->offset);
                        entries[slot] = *previous;
                    }
                }
                walker->unions = entries;
                walker->union_capacity = capacity;
            }
        }
        if (walker->result->status == CHECK_INIT_OK)
        {
            const uint32_t slot = check_init_union_slot(walker->unions, walker->union_capacity, type, frame->offset);
            CheckInitUnion* state = walker->unions + slot;
            if (state->type == 0)
            {
                *state = (CheckInitUnion){
                    .type = type,
                    .offset = frame->offset,
                    .member = frame->next,
                };
                walker->union_count += 1;
            }
            else if (state->member != frame->next)
            {
                if (clear_union)
                {
                    check_init_clear(walker, type, frame->offset, token);
                }
                state->member = frame->next;
            }
        }
    }
}

static void check_init_advance(CheckInitWalker* walker)
{
    bool finished = false;
    while (!finished && walker->depth != 0)
    {
        CheckInitFrame* frame = &walker->frames[walker->depth - 1];
        const TypeId base = type_strip_aligned(walker->types, frame->type);
        if (!frame->wrapper && type_kind(walker->types, base) == BUSTER_TYPE_KIND_UNION)
        {
            const uint32_t tag = walker->types->types.index[type_index(base)];
            frame->next = walker->types->tags.member_count[tag];
        }
        else
        {
            frame->next += 1;
        }
        CheckInitSelection selected = { 0 };
        finished = frame->braced || frame->wrapper || check_init_select(walker, frame, &selected);
        if (!finished)
        {
            walker->depth -= 1;
        }
    }
}

static bool check_init_push(CheckInitWalker* walker, const CheckInitSelection* selected, bool braced, uint32_t token)
{
    if (walker->depth == walker->capacity && walker->capacity <= UINT32_MAX / 2)
    {
        const uint32_t capacity = walker->capacity * 2;
        CheckInitFrame* frames = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitFrame, capacity);
        if (frames != 0)
        {
            memcpy(frames, walker->frames, walker->depth * sizeof(*frames));
            walker->frames = frames;
            walker->capacity = capacity;
        }
    }
    const bool atomic_braces = braced && type_kind(walker->types, type_strip_aligned(walker->types, selected->type)) == BUSTER_TYPE_KIND_ATOMIC;
    const bool valid = walker->depth < walker->capacity && !atomic_braces;
    if (!valid)
    {
        check_init_fail(walker, atomic_braces ? CHECK_INIT_INVALID : CHECK_INIT_RESOURCE, token);
    }
    else
    {
        walker->frames[walker->depth] = (CheckInitFrame){
            .type = type_kind(walker->types, type_strip_aligned(walker->types, selected->type)) == BUSTER_TYPE_KIND_ATOMIC ?
                    type_conversion_operand(walker->types, selected->type) : selected->type,
            .offset = selected->offset,
            .width = selected->width,
            .braced = braced,
        };
        walker->depth += 1;
    }
    return valid;
}

// Initializer separators live outside every expression delimiter. This also keeps commas in compound literals,
// parenthesized comma expressions, subscripts and calls inside their checked expression span.
static uint32_t check_init_expression_end(const Parser* parser, uint32_t begin, uint32_t end)
{
    uint32_t depth = 0;
    uint32_t token = begin;
    bool done = false;
    while (token < end && !done)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (depth == 0 && (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_R_BRACE))
        {
            done = true;
        }
        else
        {
            if (kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE)
            {
                depth += 1;
            }
            else if (kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE)
            {
                depth -= depth != 0;
            }
            token += 1;
        }
    }
    return token;
}

// Clang ignores the entire remaining braced list after an ordinary excess element. Nested lists end
// independently, so an excess inner row must not consume the outer array's following rows.
static uint32_t check_init_excess_end(const Parser* parser, uint32_t begin, uint32_t end)
{
    uint32_t depth = 0;
    uint32_t token = begin;
    bool done = false;
    while (token < end && !done)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        if (kind == PARSE_KIND_R_BRACE && depth == 0)
        {
            done = true;
        }
        else
        {
            depth += kind == PARSE_KIND_L_BRACE;
            depth -= kind == PARSE_KIND_R_BRACE && depth != 0;
            token += 1;
        }
    }
    return token;
}

static uint32_t check_init_node(CheckInitWalker* walker, uint32_t begin, uint32_t end)
{
    uint32_t result = BUSTER_CHECK_NONE;
    if (walker->expressions != NULL)
    {
        uint32_t first = 0;
        uint32_t last = walker->expression_count;
        while (first < last)
        {
            const uint32_t middle = first + (last - first) / 2;
            if (walker->expressions[middle].first < end)
            {
                first = middle + 1;
            }
            else
            {
                last = middle;
            }
        }
        if (first != 0 && walker->expressions[first - 1].end > begin)
        {
            result = walker->expressions[first - 1].node;
        }
    }
    else if (walker->token_nodes != 0 && walker->stream != 0)
    {
        for (uint32_t token = begin; token < end; token += 1)
        {
            const bool bounded = walker->token_nodes_count != 0;
            const bool present = !bounded || (token >= walker->token_nodes_first && token - walker->token_nodes_first < walker->token_nodes_count);
            const uint32_t node = present ? walker->token_nodes[token - walker->token_nodes_first] : BUSTER_CHECK_NONE;
            if (node < walker->stream->count && (result == BUSTER_CHECK_NONE || node > result))
            {
                result = node;
            }
        }
    }
    return result;
}

static bool check_init_index(CheckInitWalker* walker, uint32_t begin, uint32_t end, uint64_t* index)
{
    bool valid = false;
    if (walker->infer || (walker->const_values != NULL && walker->indices != NULL))
    {
        valid = walker->indices != 0 && walker->index_valid != 0 && walker->index_valid[begin] != 0;
        if (valid)
        {
            *index = walker->indices[begin];
        }
    }
    else
    {
        // The source adapter may fold an early array-bound/designator expression onto its closing bracket.
        const uint32_t node = check_init_node(walker, begin + 1, end + 1);
        if (node == BUSTER_CHECK_NONE || walker->stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            check_init_fail(walker, CHECK_INIT_CHECK_FAILED, begin);
        }
        else
        {
            const CheckValue* value = &walker->stream->values[node];
            uint32_t width = 0;
            bool is_signed = false;
            valid = value->constant_kind == CHECK_CONSTANT_INTEGER && (value->flags & BUSTER_CHECK_ICE) != 0 &&
                    value->fault == CHECK_FAULT_NONE && (value->flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                    check_action_integer_type(walker->types, value->type, &width, &is_signed) &&
                    (!is_signed || ((value->integer >> (width - 1)) & 1) == 0) && value->integer <= UINT64_MAX;
            if (valid)
            {
                *index = (uint64_t)value->integer;
            }
        }
    }
    return valid;
}

// Revisit range targets through the ordinary subobject cursor, rather than copying a template of writes:
// earlier union members can differ at each destination. Repeated VALUE parts retain the same checked node,
// so a runtime consumer evaluates a range's expression once and uses that value at every destination.
static void check_init_repeat(CheckInitWalker* walker, uint32_t* position, bool* after_value)
{
    bool active = true;
    while (active && *after_value && walker->repeat != 0)
    {
        CheckInitRepeat* repeat = walker->repeat;
        bool complete = walker->depth <= repeat->target_depth;
        if (!complete)
        {
            bool implicit = true;
            for (uint32_t i = repeat->target_depth; implicit && i < walker->depth; i += 1)
            {
                implicit = !walker->frames[i].braced;
            }
            const uint8_t kind = walker->parser->tokens.kinds[*position];
            const uint8_t next = *position + 1 < walker->parser->tokens.count ? walker->parser->tokens.kinds[*position + 1] : PARSE_KIND_COMMA;
            if (implicit && (kind == PARSE_KIND_R_BRACE ||
                (kind == PARSE_KIND_COMMA && (next == PARSE_KIND_L_SQUARE || next == PARSE_KIND_PERIOD))))
            {
                walker->depth = repeat->target_depth;
                check_init_advance(walker);
                complete = true;
            }
        }
        if (!complete)
        {
            active = false;
        }
        else
        {
            if (repeat->stop == BUSTER_CHECK_NONE)
            {
                repeat->stop = *position;
            }
            if (*position != repeat->stop)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, *position);
                active = false;
            }
        }
        CheckInitRange* advance = 0;
        for (CheckInitRange* range = active ? repeat->first : 0; range != 0; range = range->next)
        {
            if (range->current < range->last)
            {
                advance = range;
            }
        }
        if (advance != 0)
        {
            advance->current += 1;
            for (CheckInitRange* range = advance->next; range != 0; range = range->next)
            {
                range->current = range->first;
            }
            walker->depth = repeat->depth;
            *position = repeat->token;
            *after_value = false;
        }
        else if (active)
        {
            walker->repeat = repeat->previous;
        }
    }
}

static void check_init_designate(CheckInitWalker* walker, uint32_t* position, uint32_t end)
{
    const Parser* parser = walker->parser;
    uint32_t token = *position;
    while (walker->depth > 1 && !walker->frames[walker->depth - 1].braced)
    {
        walker->depth -= 1;
    }
    if (!walker->frames[walker->depth - 1].braced)
    {
        check_init_fail(walker, CHECK_INIT_INVALID, token);
    }
    const uint32_t base_depth = walker->depth;
    CheckInitRepeat* repeat = walker->repeat != 0 && walker->repeat->token == token ? walker->repeat : 0;
    const bool replay = repeat != 0;
    CheckInitRange* range = replay ? repeat->first : 0;
    bool more = true;
    while (more && token < end && walker->result->status == CHECK_INIT_OK)
    {
        CheckInitFrame* frame = &walker->frames[walker->depth - 1];
        const TypeId type = type_strip_aligned(walker->types, frame->type);
        const uint8_t kind = type_kind(walker->types, type);
        if (parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE)
        {
            uint32_t close = token + 1;
            uint32_t depth = 1;
            uint32_t ellipsis = BUSTER_CHECK_NONE;
            while (close < end && depth != 0)
            {
                const uint8_t inner = parser->tokens.kinds[close];
                if (depth == 1 && inner == PARSE_KIND_ELLIPSIS)
                {
                    ellipsis = close;
                }
                depth += inner == PARSE_KIND_L_SQUARE;
                depth -= inner == PARSE_KIND_R_SQUARE;
                close += depth != 0;
            }
            uint64_t index = 0;
            uint64_t last = 0;
            if ((kind != BUSTER_TYPE_KIND_ARRAY && kind != BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) || close == end ||
                !check_init_index(walker, token, ellipsis != BUSTER_CHECK_NONE ? ellipsis : close, &index) ||
                (ellipsis != BUSTER_CHECK_NONE && (!check_init_index(walker, ellipsis, close, &last) || last < index)))
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                last = ellipsis == BUSTER_CHECK_NONE ? index : last;
                frame->next = last;
                CheckInitSelection final = { 0 };
                if (!check_init_select(walker, frame, &final))
                {
                    check_init_fail(walker, CHECK_INIT_INVALID, token);
                }
                else if (walker->infer)
                {
                    // Only the highest target can enlarge the inferred outer extent.
                    index = last;
                }
                else if (last > index)
                {
                    if (!replay)
                    {
                        if (repeat == 0)
                        {
                            repeat = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitRepeat, 1);
                            if (repeat != 0)
                            {
                                *repeat = (CheckInitRepeat){
                                    .previous = walker->repeat,
                                    .token = *position,
                                    .stop = BUSTER_CHECK_NONE,
                                    .depth = base_depth,
                                };
                                walker->repeat = repeat;
                            }
                        }
                        range = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitRange, 1);
                        if (repeat != 0 && range != 0)
                        {
                            *range = (CheckInitRange){
                                .first = index,
                                .last = last,
                                .current = index,
                                .token = token,
                            };
                            if (repeat->last == 0)
                            {
                                repeat->first = range;
                            }
                            else
                            {
                                repeat->last->next = range;
                            }
                            repeat->last = range;
                        }
                    }
                    if (repeat == 0 || range == 0)
                    {
                        check_init_fail(walker, CHECK_INIT_RESOURCE, token);
                    }
                    else if (range->token != token)
                    {
                        check_init_fail(walker, CHECK_INIT_INVALID, token);
                    }
                    else
                    {
                        index = range->current;
                        range = range->next;
                    }
                }
                frame->next = index;
                token = close + 1;
            }
        }
        else if (parser->tokens.kinds[token] == PARSE_KIND_PERIOD)
        {
            if ((kind != BUSTER_TYPE_KIND_STRUCT && kind != BUSTER_TYPE_KIND_UNION) || token + 1 >= end ||
                parser->tokens.kinds[token + 1] != PARSE_KIND_IDENTIFIER)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                const uint32_t name = parser->tokens.names[token + 1];
                bool found = false;
                while (!found && walker->result->status == CHECK_INIT_OK)
                {
                    frame = &walker->frames[walker->depth - 1];
                    const TypeId record = type_strip_aligned(walker->types, frame->type);
                    const uint32_t tag = walker->types->types.index[type_index(record)];
                    const uint32_t begin = walker->types->tags.member_begin[tag];
                    const uint32_t count = walker->types->tags.member_count[tag];
                    uint32_t member = 0;
                    for (uint32_t i = 0; i < count && member == 0; i += 1)
                    {
                        if (walker->types->members.name[begin + i] == name)
                        {
                            member = begin + i;
                            found = true;
                        }
                    }
                    for (uint32_t i = 0; i < count && member == 0; i += 1)
                    {
                        uint64_t ignored = 0;
                        if (walker->types->members.name[begin + i] == 0 &&
                            (walker->types->members.flags[begin + i] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0 &&
                            type_find_member(walker->types, walker->types->members.type[begin + i], name, &ignored) != 0)
                        {
                            member = begin + i;
                        }
                    }
                    if (member == 0)
                    {
                        check_init_fail(walker, CHECK_INIT_INVALID, token);
                    }
                    else
                    {
                        frame->next = member - begin;
                        if (!found)
                        {
                            CheckInitSelection selected = { 0 };
                            if (check_init_select(walker, frame, &selected))
                            {
                                check_init_used(walker, frame, token, true);
                                check_init_push(walker, &selected, false, token);
                            }
                            else
                            {
                                check_init_fail(walker, CHECK_INIT_INVALID, token);
                            }
                        }
                    }
                }
                token += 2;
            }
        }
        else
        {
            check_init_fail(walker, CHECK_INIT_INVALID, token);
        }
        if (walker->result->status == CHECK_INIT_OK)
        {
            CheckInitSelection selected = { 0 };
            frame = &walker->frames[walker->depth - 1];
            if (!check_init_select(walker, frame, &selected))
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                check_init_used(walker, frame, token, true);
                more = token < end && (parser->tokens.kinds[token] == PARSE_KIND_PERIOD || parser->tokens.kinds[token] == PARSE_KIND_L_SQUARE);
                if (more)
                {
                    if (!check_init_aggregate(walker->types, selected.type) ||
                        type_kind(walker->types, type_strip_aligned(walker->types, selected.type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
                    {
                        check_init_fail(walker, CHECK_INIT_INVALID, token);
                    }
                    else
                    {
                        check_init_push(walker, &selected, false, token);
                    }
                }
            }
        }
    }
    if (walker->result->status == CHECK_INIT_OK)
    {
        if (token >= end || parser->tokens.kinds[token] != PARSE_KIND_EQUAL)
        {
            check_init_fail(walker, CHECK_INIT_INVALID, token);
        }
        else
        {
            token += 1;
            if (repeat != 0 && !replay)
            {
                repeat->target_depth = walker->depth;
            }
        }
    }
    *position = token;
}

// Both identities are already indexed by the reader. In particular, an ordinary nonstring initializer never
// searches the unit's literal pool, and an array-valued parenthesized string resolves through its original object.
static const CheckInitString* check_init_string(CheckInitWalker* walker, uint32_t begin, uint32_t end, uint32_t node)
{
    const CheckInitString* result = 0;
    uint32_t object = 0;
    if (node != BUSTER_CHECK_NONE)
    {
        const CheckValue* value = &walker->stream->values[node];
        if (type_kind(walker->types, type_strip_aligned(walker->types, value->type)) == BUSTER_TYPE_KIND_ARRAY && value->offset == 0)
        {
            object = value->object;
        }
    }
    if (object != 0 && object <= walker->parser->tokens.count && walker->string_ids != 0)
    {
        const uint32_t index = walker->string_ids[object - 1];
        if (index < walker->string_count && !walker->strings[index].predefined && walker->strings[index].object == object)
        {
            result = walker->strings + index;
        }
    }
    if (result == 0 && begin < end && walker->string_ids != 0)
    {
        const uint32_t index = walker->string_ids[begin];
        bool literal_only = index < walker->string_count && !walker->strings[index].predefined &&
                            walker->strings[index].token >= begin && walker->strings[index].token < end;
        for (uint32_t token = begin; literal_only && token < end; token += 1)
        {
            literal_only = walker->parser->tokens.kinds[token] == PARSE_KIND_STRING;
        }
        if (literal_only)
        {
            result = walker->strings + index;
        }
    }
    return result;
}

static bool check_init_character(TypeId type)
{
    return type == BUSTER_TYPE_CHAR || type == BUSTER_TYPE_SCHAR || type == BUSTER_TYPE_UCHAR;
}

static uint32_t check_init_compound(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t begin, uint32_t end,
                                    uint32_t node, TypeId* root_type, uint64_t* source_offset)
{
    const TypeId destination = selected->type;
    CheckInitSelection value_selection = *selected;
    if (type_kind(walker->types, type_strip_aligned(walker->types, selected->type)) == BUSTER_TYPE_KIND_ATOMIC)
    {
        value_selection.type = type_conversion_operand(walker->types, selected->type);
        selected = &value_selection;
    }
    uint32_t opener = BUSTER_CHECK_NONE;
    const uint8_t kind = type_kind(walker->types, type_strip_aligned(walker->types, selected->type));
    const bool record = kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || check_vector_type(walker->types, selected->type);
    if (!walker->infer && node < walker->stream->count && check_vector_type(walker->types, selected->type))
    {
        const CheckAction action = {
            .op = CHECK_OP_INITIALIZE,
            .operands = { walker->stream->values[node] },
            .type = destination,
            .node = node,
            .extensions = check_init_extensions(walker, begin),
        };
        CheckActionResult checked = { 0 };
        check_action_scalar(walker->types, &action, &checked);
        if (checked.diagnostic != CHECK_DIAG_NONE)
        {
            check_init_fail(walker, checked.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_INVALID, begin);
        }
    }
    // A compound literal of block scope too, an automatic object, whose value clang folds into a static object's
    // initializer as GNU C does: the walk copies its body's parts, each of which the static destination requires to be
    // constant (`static const S g[] = { (S){ "a", 1 } };` in a function, but not `(S){ n }`).
    if (walker->result->status == CHECK_INIT_OK && !walker->infer && walker->constants == NULL && walker->static_storage && record && node != BUSTER_CHECK_NONE &&
        walker->stream->values[node].category == CHECK_LVALUE)
    {
        for (uint32_t token = begin; token < end && opener == BUSTER_CHECK_NONE; token += 1)
        {
            if (walker->parser->tree.roles[token] == PARSE_ROLE_OPEN_COMPOUND_BODY)
            {
                const uint32_t close = walker->parser->tree.parents[token];
                const uint32_t root = close > token && close < end ? check_init_node(walker, close, close + 1) : BUSTER_CHECK_NONE;
                const CheckValue* value = walker->stream->values + node;
                bool member_path = root != BUSTER_CHECK_NONE;
                for (uint32_t tail = close + 1; member_path && tail < end;)
                {
                    if (walker->parser->tokens.kinds[tail] == PARSE_KIND_R_PAREN)
                    {
                        tail += 1;
                    }
                    else if (walker->parser->tokens.kinds[tail] == PARSE_KIND_PERIOD && tail + 1 < end &&
                             walker->parser->tokens.kinds[tail + 1] == PARSE_KIND_IDENTIFIER)
                    {
                        tail += 2;
                    }
                    else
                    {
                        member_path = false;
                    }
                }
                if (member_path && walker->stream->values[root].object == value->object && value->offset >= 0)
                {
                    const TypeId type = walker->stream->values[root].type;
                    const uint64_t size = type_size(walker->types, type);
                    const uint64_t selected_size = type_size(walker->types, selected->type);
                    if ((uint64_t)value->offset <= size && selected_size <= size - (uint64_t)value->offset && size <= UINT64_MAX / 8)
                    {
                        opener = token;
                        *root_type = type;
                        *source_offset = (uint64_t)value->offset * 8;
                    }
                }
            }
        }
    }
    return opener;
}

// A constant record expression can select a nested subobject of a compound literal. Plan its complete source
// body in the same iterative walk, then retain only writes that intersect that subobject and translate their
// offsets. Source union state stays separate until the selected subobject's active members are transferred.
static void check_init_finish_copy(CheckInitWalker* walker, const CheckInitCopy* copy, uint32_t token)
{
    CheckInitUnion* source_unions = walker->unions;
    const uint32_t source_capacity = walker->union_capacity;
    walker->unions = copy->unions;
    walker->union_count = copy->union_count;
    walker->union_capacity = copy->union_capacity;
    const uint64_t limit = copy->source + copy->bits;
    const uint32_t count = walker->result->count;
    uint32_t written = copy->first;
    for (uint32_t i = copy->first; walker->result->status == CHECK_INIT_OK && i < count; i += 1)
    {
        CheckInitPart part = walker->result->parts[i];
        uint64_t bits = 0;
        const CheckInitStatus status = check_initializer_part_bits(walker->types, &part, &bits);
        if (status != CHECK_INIT_OK)
        {
            check_init_fail(walker, status, part.token);
        }
        else if (part.bit_offset < limit && part.bit_offset + bits > copy->source)
        {
            const uint64_t begin = part.bit_offset > copy->source ? part.bit_offset : copy->source;
            const uint64_t end = part.bit_offset + bits < limit ? part.bit_offset + bits : limit;
            if (begin != part.bit_offset || end != part.bit_offset + bits)
            {
                if (part.kind == CHECK_INIT_CLEAR)
                {
                    part.bit_width = (begin % 8 == 0 && end % 8 == 0) ? 0 : (uint32_t)(end - begin);
                    part.byte_count = part.bit_width == 0 ? (end - begin) / 8 : 0;
                }
                else if (part.kind == CHECK_INIT_BYTES && begin % 8 == 0 && end % 8 == 0)
                {
                    part.bytes += (begin - part.bit_offset) / 8;
                    part.byte_count = (end - begin) / 8;
                }
                else
                {
                    // Reinterpreting a fraction of a scalar representation needs an explicit target bitcast.
                    check_init_fail(walker, CHECK_INIT_UNSUPPORTED, token);
                }
            }
            if (walker->result->status == CHECK_INIT_OK)
            {
                part.bit_offset = copy->destination + begin - copy->source;
                walker->result->parts[written++] = part;
            }
        }
    }
    if (walker->result->status == CHECK_INIT_OK)
    {
        walker->result->count = written;
        arena_pop_to(walker->output, (uint64_t)((uint8_t*)(walker->result->parts + written) - walker->output->base));
        for (uint32_t i = 0; walker->result->status == CHECK_INIT_OK && i < source_capacity; i += 1)
        {
            const CheckInitUnion* source = source_unions + i;
            if (source->type != 0 && source->offset >= copy->source && source->offset < limit &&
                type_size(walker->types, source->type) <= (limit - source->offset) / 8)
            {
                const CheckInitFrame frame = {
                    .type = source->type,
                    .offset = copy->destination + source->offset - copy->source,
                    .next = source->member,
                };
                check_init_used(walker, &frame, token, false);
            }
        }
    }
}

// The type of the expression from `begin` to `end` that an inferring walk (check_initializer_infer_array, before the
// expressions are checked) reads from the tree, at the expression's root, the one token whose parent is outside it: an
// identifier's declared type, a compound literal's (at its opening parenthesis), a ?:'s from its second and third
// operands' (check_init_conditional_type: `n ? v : 0` is a vector, #555's third review) and its condition's
// (check_init_condition_type: `mask ? 1 : 2` of an extension vector is one), a parenthesized expression's, a call's
// result where it calls a function or an intrinsic by its name (an intrinsic's prototype the reader's, at the call's
// `)`), and a cast's (the reader's, at its `)`); an int, for which one stands for every scalar (the callers ask only
// whether the value is a record, a vector or neither), for a constant, a `sizeof` or `_Alignof`; a `!`'s, a `&&`'s and
// a `||`'s from their operands', an int or, of an extension vector, a vector (#555's sixth review); a subscript's, a
// member's (`.` and `->`, an extension vector's swizzle among them) and an indirection's of such an expression (`x[1]`,
// `p->a.b`, `v.x`, `*p`), from its base's type;
// a `-`, `+` or `~`'s, its
// operand's; and a binary operator's (multiplicative to bitwise or), from both operands' as check_init_binary_type
// combines them: a vector where either is one, a pointer for pointer arithmetic, and a scalar for the rest (`ha +
// (short)n` is a vector, `*(p + 1)` a struct, `(p - q) * h` a vector, `*(p + (p - (S*)q))` a struct). 0 for another
// expression (a call through a pointer's expression), which the caller leaves to the last typed token in it, and which
// check_init_binary_type takes for neither a scalar nor a pointer (#555's second review). A compound literal's own tokens (a
// member's value) don't count: `(struct L){ .target = n }` is a struct L, not n's type. One pass over the expression
// gives each of its nodes its first and last children and its opening bracket, and the walk follows them down from the
// root, so that it reads each token once however deeply the expression nests (#480's review: a rescan of the range at
// each level was quadratic). It keeps the subscripts, members and indirections it passes (in the scratch arena, at most
// one a token, so that a chain of any length keeps them all: #495's review), then applies them to the base's type from
// the innermost out, any it can't (a member of what isn't a record) making it 0. A subscript's base is either operand,
// as C has it (`p[1]` and `1[p]`): the walk takes the first, and where that one's type is no pointer or array (or it has
// none), it goes back to the subscript and takes the index instead, once (#495's review). Each such retry drops the
// steps under the subscript, whose subtree it leaves for the index's, so that each token is still read once.
// A binary operator's step and a ?:'s (CheckInitWalker's walk keeps a token's kind for the others), past every token
// kind.
#define BUSTER_CHECK_INIT_ROOT_BINARY PARSE_KIND_COUNT
#define BUSTER_CHECK_INIT_ROOT_CONDITIONAL (PARSE_KIND_COUNT + 1)
#define BUSTER_CHECK_INIT_ROOT_CONDITION (PARSE_KIND_COUNT + 2)
#define BUSTER_CHECK_INIT_ROOT_NOT (PARSE_KIND_COUNT + 3)

// A binary operator's value's type from its operands' (0 for one the walk has no type for), as far as the walk needs it,
// and 0 where they leave it open, so that an operand of no type is taken for no scalar (#555's second review):
// - a vector's where either operand is one (GNU's operators, a compare's mask among them);
// - for `+` with one pointer or array operand, that one's (in valid C the other is an integer, whatever the walk has for
//   it);
// - for `-` with a pointer or an array on the left, its type where the right one is a scalar, a scalar's where it is a
//   pointer too (a ptrdiff_t), and 0 where the walk has no type for it; a scalar's where only the right one is a pointer
//   (in valid C, the left one is one too);
// - else, where the walk has both operands' types, a scalar's (an int: a compare's, of pointers too); 0 otherwise.
static TypeId check_init_binary_type(const TypeTable* types, uint8_t operator_kind, TypeId left, TypeId right)
{
    const TypeId left_base = left != 0 ? type_strip_aligned(types, left) : 0;
    const TypeId right_base = right != 0 ? type_strip_aligned(types, right) : 0;
    const uint8_t left_kind = left_base != 0 ? type_kind(types, left_base) : 0;
    const uint8_t right_kind = right_base != 0 ? type_kind(types, right_base) : 0;
    const bool left_pointer = left_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(left_kind);
    const bool right_pointer = right_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(right_kind);
    TypeId result = 0;
    if (left_base != 0 && check_vector_type(types, left_base))
    {
        result = left;
    }
    else if (right_base != 0 && check_vector_type(types, right_base))
    {
        result = right;
    }
    else if (operator_kind == PARSE_KIND_PLUS && (left_pointer || right_pointer))
    {
        result = left_pointer && right_pointer ? 0 : left_pointer ? left : right;
    }
    else if (operator_kind == PARSE_KIND_MINUS && left_pointer)
    {
        result = right_pointer ? BUSTER_TYPE_INT : right != 0 ? left : 0;
    }
    else if (right_pointer && operator_kind == PARSE_KIND_MINUS)
    {
        result = BUSTER_TYPE_INT;
    }
    else if (left != 0 && right != 0)
    {
        result = BUSTER_TYPE_INT;
    }
    return result;
}
_Static_assert(PARSE_KIND_COUNT + 3 <= UINT8_MAX, "a step's kind fits a byte");

// A ?:'s value's type from its second and third operands' (0 for one the walk has no type for): a vector's where either
// is one (GNU C converts a scalar arm to the other's vector type); a record's, a pointer's or an array's where either is
// one (in valid C the other is a compatible record, or a pointer or a null pointer constant); a scalar's (an int) where
// both are scalars; 0 otherwise, where an arm of no type leaves it open.
static TypeId check_init_conditional_type(const TypeTable* types, TypeId second, TypeId third)
{
    const TypeId second_base = second != 0 ? type_strip_aligned(types, second) : 0;
    const TypeId third_base = third != 0 ? type_strip_aligned(types, third) : 0;
    const uint8_t second_kind = second_base != 0 ? type_kind(types, second_base) : 0;
    const uint8_t third_kind = third_base != 0 ? type_kind(types, third_base) : 0;
    const bool second_object = second_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(second_kind) || second_kind == BUSTER_TYPE_KIND_STRUCT ||
                               second_kind == BUSTER_TYPE_KIND_UNION;
    const bool third_object = third_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(third_kind) || third_kind == BUSTER_TYPE_KIND_STRUCT ||
                              third_kind == BUSTER_TYPE_KIND_UNION;
    TypeId result = 0;
    if (second_base != 0 && check_vector_type(types, second_base))
    {
        result = second;
    }
    else if (third_base != 0 && check_vector_type(types, third_base))
    {
        result = third;
    }
    else if (second_object || third_object)
    {
        result = second_object ? second : third;
    }
    else if (second != 0 && third != 0)
    {
        result = BUSTER_TYPE_INT;
    }
    return result;
}

// A ?:'s value's type from its arms' (check_init_conditional_type's) and its condition's (#555's fifth review): a vector
// where the arms are one; a vector condition's where they aren't, since clang's extension vectors choose element by
// element (`mask ? 1 : 2` is a vector); the arms' where the condition is a known scalar, or where they are a pointer, an
// array or a record (a vector condition takes none); and 0 where a condition of no type could still be a vector.
static TypeId check_init_condition_type(const TypeTable* types, TypeId arms, TypeId condition)
{
    const TypeId arms_base = arms != 0 ? type_strip_aligned(types, arms) : 0;
    const TypeId condition_base = condition != 0 ? type_strip_aligned(types, condition) : 0;
    const uint8_t arms_kind = arms_base != 0 ? type_kind(types, arms_base) : 0;
    const bool arms_object = arms_kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(arms_kind) || arms_kind == BUSTER_TYPE_KIND_STRUCT ||
                             arms_kind == BUSTER_TYPE_KIND_UNION;
    TypeId result = 0;
    if (arms_base != 0 && check_vector_type(types, arms_base))
    {
        result = arms;
    }
    else if (condition_base != 0 && check_vector_type(types, condition_base))
    {
        result = condition;
    }
    else if (condition != 0 || arms_object)
    {
        result = arms;
    }
    return result;
}

static TypeId check_init_root_type(CheckInitWalker* walker, uint32_t begin, uint32_t end)
{
    const Parser* parser = walker->parser;
    const TypeId* types = walker->expression_types;
    TypeId result = 0;
    const bool valid = types != 0 && begin < end && end <= parser->tokens.count;
    const uint32_t count = valid ? end - begin : 0;
    const uint64_t mark = walker->scratch->length;
    // The links (each node's first and last child, its opener and its last child before it: a ?:'s second operand, or
    // its `?` for GNU's `a ?: b`), then the steps: a step's member token for a `.` or a `->` (a binary operator's or a
    // ?:'s own token; BUSTER_CHECK_NONE for a subscript or a `*`), its operator's kind, a subscript's, a binary
    // operator's or a ?:'s other operand, its root, while the walk hasn't taken it (BUSTER_CHECK_NONE otherwise), and a
    // binary operator's left operand's type (a ?:'s second), kept while the walk goes down the other one.
    uint32_t* links = valid ? arena_push_array(walker->scratch, (uint64_t)count * 7, sizeof(uint32_t), 4) : 0;
    uint8_t* operators = links != 0 ? arena_push_array(walker->scratch, count, sizeof(uint8_t), 1) : 0;
    uint32_t* first = links;
    uint32_t* last = links != 0 ? links + count : 0;
    uint32_t* opener = links != 0 ? links + 2 * (uint64_t)count : 0;
    uint32_t* previous = links != 0 ? links + 3 * (uint64_t)count : 0;
    uint32_t* members = links != 0 ? links + 4 * (uint64_t)count : 0;
    uint32_t* alternates = links != 0 ? links + 5 * (uint64_t)count : 0;
    TypeId* lefts = links != 0 ? links + 6 * (uint64_t)count : 0;
    uint32_t steps = 0;
    uint32_t root = BUSTER_CHECK_NONE;
    uint32_t roots = 0;
    if (valid && (links == 0 || operators == 0))
    {
        check_init_fail(walker, CHECK_INIT_RESOURCE, begin);
    }
    else if (links != 0)
    {
        memset(links, 0xff, (uint64_t)count * 4 * sizeof(uint32_t));
        for (uint32_t token = begin; token < end; token += 1)
        {
            const uint32_t parent = parser->tree.parents[token];
            const uint8_t role = parser->tree.roles[token];
            if (parent < begin || parent >= end)
            {
                root = token;
                roots += 1;
            }
            else
            {
                const uint32_t slot = parent - begin;
                first[slot] = first[slot] == BUSTER_CHECK_NONE ? token : first[slot];
                last[slot] = token;
                previous[slot] = token < parent ? token : previous[slot];
                opener[slot] = role == PARSE_ROLE_OPEN_GROUP || role == PARSE_ROLE_OPEN_CALL || role == PARSE_ROLE_OPEN_COMPOUND ||
                               role == PARSE_ROLE_OPEN_SUBSCRIPT ? token : opener[slot];
            }
        }
    }
    bool walking = roots == 1;
    while (walking)
    {
        // Down from `root` to the primary expression, keeping the steps.
        result = 0;
        bool descending = true;
        while (descending)
        {
            const uint8_t role = parser->tree.roles[root];
            const uint32_t slot = root - begin;
            const uint32_t open = opener[slot];
            descending = false;
            if (role == PARSE_ROLE_OPERAND && parser->tokens.kinds[root] == PARSE_KIND_IDENTIFIER)
            {
                result = types[root];
            }
            else if (role == PARSE_ROLE_CLOSE_CAST)
            {
                result = walker->cast_types != 0 ? walker->cast_types[root] : 0;
            }
            else if ((role == PARSE_ROLE_OPERAND && (parser->tokens.kinds[root] == PARSE_KIND_NUMBER || parser->tokens.kinds[root] == PARSE_KIND_CHARACTER)) ||
                     (role == PARSE_ROLE_PREFIX && parser->tokens.kinds[root] == PARSE_KIND_SIZEOF))
            {
                result = BUSTER_TYPE_INT;
            }
            else if (steps < count && role == PARSE_ROLE_PREFIX && parser->tokens.kinds[root] == PARSE_KIND_EXCLAIM && last[slot] != BUSTER_CHECK_NONE &&
                     last[slot] > root)
            {
                // Its operand's type, from which the up-walk makes an int or, of an extension vector, its vector.
                members[steps] = BUSTER_CHECK_NONE;
                operators[steps] = BUSTER_CHECK_INIT_ROOT_NOT;
                alternates[steps] = BUSTER_CHECK_NONE;
                steps += 1;
                root = last[slot];
                descending = true;
            }
            else if (role == PARSE_ROLE_CLOSE_COMPOUND_BODY && first[slot] != BUSTER_CHECK_NONE)
            {
                // Its `(`, whose `)` is the body's first child.
                const uint32_t literal = opener[first[slot] - begin];
                result = literal != BUSTER_CHECK_NONE && parser->tree.roles[literal] == PARSE_ROLE_OPEN_COMPOUND ? types[literal] : 0;
            }
            else if (steps + 1 < count && role == PARSE_ROLE_CLOSE_TERNARY && previous[slot] != BUSTER_CHECK_NONE && last[slot] != BUSTER_CHECK_NONE &&
                     last[slot] > root)
            {
                // The second operand, the last child before the `:`, then the third, the last child, the conditional
                // step's alternate, and then the condition, the first child, the condition step's, outside it. GNU's
                // `a ?: b` has its condition for its second operand, which takes no step of its own.
                const bool omitted = parser->tree.roles[previous[slot]] == PARSE_ROLE_OPEN_TERNARY;
                if (!omitted)
                {
                    members[steps] = root;
                    operators[steps] = BUSTER_CHECK_INIT_ROOT_CONDITION;
                    alternates[steps] = first[slot];
                    steps += 1;
                }
                members[steps] = root;
                operators[steps] = BUSTER_CHECK_INIT_ROOT_CONDITIONAL;
                alternates[steps] = last[slot];
                steps += 1;
                root = omitted ? first[slot] : previous[slot];
                descending = true;
            }
            else if (role == PARSE_ROLE_CLOSE_GROUP && open != BUSTER_CHECK_NONE && parser->tree.roles[open] == PARSE_ROLE_OPEN_GROUP &&
                     last[slot] != BUSTER_CHECK_NONE && last[slot] > open)
            {
                root = last[slot];
                descending = true;
            }
            else if (role == PARSE_ROLE_CLOSE_CALL && open != BUSTER_CHECK_NONE && parser->tree.roles[open] == PARSE_ROLE_OPEN_CALL &&
                     first[slot] + 1 == open && parser->tree.roles[first[slot]] == PARSE_ROLE_OPERAND && parser->tokens.kinds[first[slot]] == PARSE_KIND_IDENTIFIER)
            {
                const uint32_t name = first[slot];
                // An intrinsic has no declaration: the reader keeps its prototype at the call's `)` (its query type).
                const TypeId declared = types[name] != 0 ? types[name] : walker->cast_types != 0 ? walker->cast_types[root] : 0;
                TypeId callee = declared != 0 ? type_strip_aligned(walker->types, declared) : 0;
                callee = callee != 0 && type_kind(walker->types, callee) == BUSTER_TYPE_KIND_POINTER ? type_strip_aligned(walker->types, type_element(walker->types, callee)) : callee;
                result = callee != 0 && type_kind(walker->types, callee) == BUSTER_TYPE_KIND_FUNCTION ? type_return(walker->types, callee) : 0;
            }
            else if (steps < count && role == PARSE_ROLE_CLOSE_SUBSCRIPT && open != BUSTER_CHECK_NONE && parser->tree.roles[open] == PARSE_ROLE_OPEN_SUBSCRIPT &&
                     first[slot] != BUSTER_CHECK_NONE && first[slot] < open)
            {
                // The base, the first child, before the `[` whose `]` the root is; the index, the last child, after it.
                members[steps] = BUSTER_CHECK_NONE;
                operators[steps] = PARSE_KIND_L_SQUARE;
                alternates[steps] = last[slot] != BUSTER_CHECK_NONE && last[slot] > open ? last[slot] : BUSTER_CHECK_NONE;
                steps += 1;
                root = first[slot];
                descending = true;
            }
            else if (steps < count && role == PARSE_ROLE_MEMBER_OPERATOR && root + 1 < end && parser->tree.roles[root + 1] == PARSE_ROLE_MEMBER &&
                     first[slot] != BUSTER_CHECK_NONE && first[slot] < root)
            {
                members[steps] = root + 1;
                operators[steps] = parser->tokens.kinds[root];
                alternates[steps] = BUSTER_CHECK_NONE;
                steps += 1;
                root = first[slot];
                descending = true;
            }
            else if (steps < count && role == PARSE_ROLE_PREFIX && parser->tokens.kinds[root] == PARSE_KIND_STAR && last[slot] != BUSTER_CHECK_NONE &&
                     last[slot] > root)
            {
                members[steps] = BUSTER_CHECK_NONE;
                operators[steps] = PARSE_KIND_STAR;
                alternates[steps] = BUSTER_CHECK_NONE;
                steps += 1;
                root = last[slot];
                descending = true;
            }
            else if (role == PARSE_ROLE_PREFIX && (parser->tokens.kinds[root] == PARSE_KIND_MINUS || parser->tokens.kinds[root] == PARSE_KIND_PLUS ||
                                                   parser->tokens.kinds[root] == PARSE_KIND_TILDE) && last[slot] != BUSTER_CHECK_NONE && last[slot] > root)
            {
                root = last[slot];
                descending = true;
            }
            else if (steps < count && ((role >= PARSE_ROLE_MULTIPLICATIVE && role <= PARSE_ROLE_BITWISE_OR) || role == PARSE_ROLE_LOGICAL_AND ||
                                       role == PARSE_ROLE_LOGICAL_OR) && first[slot] != BUSTER_CHECK_NONE &&
                     first[slot] < root && last[slot] != BUSTER_CHECK_NONE && last[slot] > root)
            {
                // The left operand, the first child, then the right one, the last, the step's alternate.
                members[steps] = root;
                operators[steps] = BUSTER_CHECK_INIT_ROOT_BINARY;
                alternates[steps] = last[slot];
                steps += 1;
                root = first[slot];
                descending = true;
            }
        }
        // Up from the primary: the steps from the innermost out. A subscript reads a pointer's or an array's element, or
        // a vector's lane (`v[0]` is its element type's, #555's fourth review); one whose operand is none of them (an
        // integer: `1[p]`'s 1), or has no type here, takes its index instead, once: the walk goes down it again, with the
        // steps under the subscript dropped.
        walking = false;
        uint32_t step = steps;
        while (step > 0 && !walking)
        {
            step -= 1;
            TypeId base = result != 0 ? type_strip_aligned(walker->types, result) : 0;
            uint8_t kind = base != 0 ? type_kind(walker->types, base) : 0;
            const bool indirect = operators[step] != PARSE_KIND_PERIOD;
            const bool element = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ||
                                 (operators[step] == PARSE_KIND_L_SQUARE && base != 0 && check_vector_type(walker->types, base));
            const bool conditional = operators[step] == BUSTER_CHECK_INIT_ROOT_CONDITIONAL;
            const bool condition = operators[step] == BUSTER_CHECK_INIT_ROOT_CONDITION;
            const bool binary = operators[step] == BUSTER_CHECK_INIT_ROOT_BINARY || conditional || condition;
            if (binary && alternates[step] != BUSTER_CHECK_NONE)
            {
                // Both operands, whatever the left one's type: `p - q` is no pointer, and `(p - q) * h` a vector; a ?:'s
                // two arms, and then its arms' type with its condition.
                lefts[step] = result;
                root = alternates[step];
                alternates[step] = BUSTER_CHECK_NONE;
                steps = step + 1;
                walking = true;
            }
            else if (operators[step] == BUSTER_CHECK_INIT_ROOT_NOT)
            {
                // An extension vector's `!` is element by element, a vector; any other operand's, an int.
                result = base != 0 && check_vector_type(walker->types, base) ? result : result != 0 ? BUSTER_TYPE_INT : 0;
            }
            else if (binary)
            {
                result = conditional ? check_init_conditional_type(walker->types, lefts[step], result) :
                         condition   ? check_init_condition_type(walker->types, lefts[step], result) :
                                       check_init_binary_type(walker->types, parser->tokens.kinds[members[step]], lefts[step], result);
            }
            else if (operators[step] == PARSE_KIND_L_SQUARE && !element && alternates[step] != BUSTER_CHECK_NONE)
            {
                root = alternates[step];
                alternates[step] = BUSTER_CHECK_NONE;
                steps = step + 1;
                walking = true;
            }
            else
            {
                if (indirect)
                {
                    // A subscript, a `*` and a `->` read what a pointer points at, or an array's element (or a subscript a
                    // vector's lane).
                    base = element ? type_strip_aligned(walker->types, type_element(walker->types, base)) : 0;
                    kind = base != 0 ? type_kind(walker->types, base) : 0;
                    result = base;
                }
                if (members[step] != BUSTER_CHECK_NONE && kind == BUSTER_TYPE_KIND_EXT_VECTOR && walker->identifiers != 0)
                {
                    // An extension vector's swizzle (#555's sixth review): one lane its element type's scalar, more a
                    // vector of those lanes, made as check_vector_member makes it, so that a swizzle of it counts them
                    // (`v.xy.lo` is a lane: #555's eighth review); 0 where the table has no room for it.
                    const IdentId name = parser->tokens.names[members[step]];
                    uint8_t lanes[16];
                    CheckVectorMember swizzle;
                    const bool decoded = check_vector_member_decode(intern_bytes(walker->identifiers, name), intern_length(walker->identifiers, name),
                                                                    lanes, sizeof(lanes), &swizzle);
                    const uint32_t vector_lanes = check_vector_count(walker->types, base);
                    // .lo, .hi, .even and .odd take half the lanes, rounded up as check_vector_member has it: a three-lane
                    // vector's are two lanes, a vector (#555's seventh review).
                    const uint32_t width = !decoded ? 0 : swizzle.kind == CHECK_VECTOR_MEMBER_LANES ? swizzle.count :
                                           swizzle.kind == CHECK_VECTOR_MEMBER_INVALID ? 0 : (vector_lanes + 1) / 2;
                    const TypeId element = check_vector_element(walker->types, base);
                    result = width == 0 ? 0 : width == 1 ? element : type_vector(walker->types, element, width, true);
                }
                else if (members[step] != BUSTER_CHECK_NONE)
                {
                    uint64_t offset = 0;
                    const uint32_t member = base != 0 && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) ?
                                            type_find_member(walker->types, base, parser->tokens.names[members[step]], &offset) : 0;
                    result = member != 0 ? walker->types->members.type[member] : 0;
                }
            }
        }
    }
    arena_pop_to(walker->scratch, mark);
    return result;
}

static bool check_init_whole(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t begin, uint32_t end,
                             uint32_t node, const CheckInitString* string)
{
    const TypeId type = type_conversion_operand(walker->types, selected->type);
    const uint8_t kind = type_kind(walker->types, type);
    bool whole = !check_init_aggregate(walker->types, type);
    if (!whole && string != 0 && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY))
    {
        whole = true;
    }
    if (!whole && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION))
    {
        TypeId source = node != BUSTER_CHECK_NONE ? walker->stream->values[node].type : 0;
        const TypeId root = walker->infer ? check_init_root_type(walker, begin, end) : 0;
        source = root != 0 ? root : source;
        if (walker->infer && walker->expression_types != 0 && root == 0)
        {
            for (uint32_t token = begin; token < end; token += 1)
            {
                source = walker->expression_types[token] != 0 ? walker->expression_types[token] : source;
            }
        }
        if (type_is_valid(walker->types, source))
        {
            if (walker->stream != NULL && node < walker->stream->count && walker->stream->values[node].category == CHECK_LVALUE &&
                type_kind(walker->types, type_strip_aligned(walker->types, source)) == BUSTER_TYPE_KIND_ATOMIC)
            {
                source = type_conversion_operand(walker->types, source);
            }
            const TypeId target = type_kind(walker->types, type_strip_aligned(walker->types, source)) == BUSTER_TYPE_KIND_ATOMIC
                                      ? type_unqualified(type_strip_aligned(walker->types, selected->type))
                                      : type;
            const TypeAnswer compatible = type_compatible(walker->types, target, type_unqualified(type_strip_aligned(walker->types, source)));
            whole = compatible == TYPE_ANSWER_YES;
            if (compatible == TYPE_ANSWER_FULL)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, begin);
            }
        }
    }
    if (!whole && check_vector_type(walker->types, type))
    {
        TypeId source = node != BUSTER_CHECK_NONE ? walker->stream->values[node].type : 0;
        const TypeId root = walker->infer ? check_init_root_type(walker, begin, end) : 0;
        source = root != 0 ? root : source;
        if (walker->infer && walker->expression_types != NULL && root == 0)
        {
            for (uint32_t token = begin; token < end; token += 1)
            {
                source = walker->expression_types[token] != 0 ? walker->expression_types[token] : source;
            }
        }
        whole = check_vector_type(walker->types, type_conversion_operand(walker->types, source)) ||
                (kind == BUSTER_TYPE_KIND_EXT_VECTOR && (walker->infer || check_vector_real(walker->types, source)));
    }
    if (!whole && walker->infer && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) &&
        walker->parser->tokens.kinds[begin] == PARSE_KIND_STRING)
    {
        whole = true;
    }
    return whole;
}

#include "c_check_initializer_vector.c"

static void check_init_vector_value(CheckInitWalker* walker, const CheckInitSelection* selected, TypeId destination, uint32_t token, uint32_t node)
{
    const TypeId target = type_unqualified(type_strip_aligned(walker->types, selected->type));
    TypeId splat_type = target;
    uint32_t source_node = node;
    bool bitcast = false;
    const CheckGraph* graph = walker->graph;
    while (graph != NULL && source_node < graph->count && graph->ops[source_node] == CHECK_OP_CAST && graph->operand0[source_node] < source_node &&
           check_vector_type(walker->types, walker->stream->values[source_node].type))
    {
        splat_type = walker->stream->values[source_node].type;
        source_node = graph->operand0[source_node];
        bitcast = check_action_kind(walker->types, splat_type) == BUSTER_TYPE_KIND_VECTOR;
    }
    const CheckValue source = walker->stream->values[source_node];
    const bool scalar = check_vector_real(walker->types, source.type);
    const uint8_t* constant = check_init_constant_bytes(walker, node);
    if (walker->static_storage && constant != NULL)
    {
        check_init_vector_parts(walker, selected, token, node, constant);
    }
    else if (scalar && (check_action_kind(walker->types, splat_type) == BUSTER_TYPE_KIND_EXT_VECTOR || bitcast))
    {
        const TypeId element = bitcast ? source.type : check_vector_element(walker->types, splat_type);
        const CheckAction action = {
            .op = CHECK_OP_INITIALIZE,
            .operands = { source },
            .type = element,
            .node = source_node,
            .extensions = check_init_extensions(walker, token),
        };
        CheckActionResult converted;
        check_action_scalar(walker->types, &action, &converted);
        if (converted.diagnostic != CHECK_DIAG_NONE)
        {
            check_init_fail(walker, converted.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_INVALID, token);
        }
        else if (walker->static_storage && (converted.value.constant_kind == CHECK_CONSTANT_NONE || converted.value.fault != CHECK_FAULT_NONE ||
                 (converted.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0))
        {
            check_init_fail(walker, CHECK_INIT_NON_CONSTANT, token);
        }
        else
        {
            const bool packed = !bitcast && element == BUSTER_TYPE_BOOL;
            const uint64_t bits = packed ? 1 : type_size(walker->types, element) * 8;
            const uint32_t count = bitcast ? 1 : check_vector_count(walker->types, splat_type);
            for (uint32_t i = 0; walker->result->status == CHECK_INIT_OK && i < count; i += 1)
            {
                const CheckInitPart part = {
                    .kind = CHECK_INIT_VALUE,
                    .type = element,
                    .node = source_node,
                    .token = token,
                    .bit_offset = selected->offset + bits * i,
                    .bit_width = packed ? 1 : 0,
                    .value = converted.value,
                    .conversion = converted.conversions[0],
                };
                check_init_append(walker, &part);
            }
        }
    }
    else
    {
        const CheckAction action = {
            .op = CHECK_OP_INITIALIZE,
            .operands = { walker->stream->values[node] },
            .type = destination,
            .node = node,
        };
        CheckActionResult converted;
        check_action_scalar(walker->types, &action, &converted);
        if (converted.diagnostic != CHECK_DIAG_NONE)
        {
            check_init_fail(walker, converted.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_INVALID, token);
        }
        else if (walker->static_storage)
        {
            check_init_fail(walker, CHECK_INIT_NON_CONSTANT, token);
        }
        else
        {
            const CheckInitPart part = {
                .kind = CHECK_INIT_VALUE,
                .type = target,
                .node = node,
                .token = token,
                .bit_offset = selected->offset,
                .value = converted.value,
                .conversion = converted.conversions[0],
            };
            check_init_append(walker, &part);
        }
    }
}

// Source graph checking leaves aggregate elements pending until the subobject cursor supplies their exact
// destination type. Resolve that edge without changing its shared leaf or admitting later declarations.
static CheckDiagnostic check_init_overload(CheckInitWalker* walker, CheckAction* action, uint32_t* candidate)
{
    CheckDiagnostic diagnostic = CHECK_DIAG_NONE;
    *candidate = 0;
    if (action->operands[0].overload_head != 0)
    {
        const CheckGraph* graph = walker->stream->graph;
        if (graph == NULL || action->node >= graph->count)
        {
            diagnostic = CHECK_DIAG_INCOMPATIBLE;
        }
        else
        {
            const CheckOverloadContext context = {
                .tag_completions = graph->tag_completions,
                .transparent_unions = graph->transparent_unions,
                .tag_completion_count = graph->tag_completion_count,
                .source_position = graph->source_positions != NULL ? graph->source_positions[action->node] : BUSTER_CHECK_NONE,
                .extensions = action->extensions,
            };
            CheckValue resolved = { 0 };
            const CheckOverloadResult selected = check_overload_argument(walker->types, action->operands, action->type,
                                                                         graph->overload_candidates, graph->overload_candidate_count,
                                                                         &context, walker->scratch, &resolved);
            if (selected.status != CHECK_OVERLOAD_OK)
            {
                diagnostic = selected.status == CHECK_OVERLOAD_FULL ? CHECK_DIAG_RESOURCE : CHECK_DIAG_INCOMPATIBLE;
            }
            else
            {
                action->operands[0] = resolved;
                *candidate = selected.candidate + 1;
            }
        }
    }
    return diagnostic;
}

// Aggregate copies retain typed writes and active union selections. Offsets alone cannot prove an inactive
// union read, and a sparse CLEAR run must stay sparse even when the object is a very large array.
static bool check_init_const_copy(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t token, uint32_t node)
{
    const CheckValue source = node < walker->stream->count ? walker->stream->values[node] : (CheckValue){ 0 };
    const CheckConstValues* owner = NULL;
    const CheckConstObject* object = walker->const_values != NULL ? check_const_find(walker->const_values, source.object, walker->source_position, &owner) : NULL;
    bool valid = object != NULL && owner != NULL && source.category == CHECK_LVALUE && source.constant_kind == CHECK_CONSTANT_ADDRESS &&
                 source.offset >= 0 && source.object_bit_offset == 0 && source.fault == CHECK_FAULT_NONE &&
                 (source.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                 (type_qualifiers(source.type) & BUSTER_TYPE_VOLATILE) == 0 && check_const_eligible(walker->types, object->type) &&
                 type_unqualified(type_strip_aligned(walker->types, source.type)) == type_unqualified(type_strip_aligned(walker->types, selected->type));
    const uint64_t size = type_size(walker->types, selected->type);
    valid = valid && size <= UINT64_MAX / 8 && (uint64_t)source.offset <= object->byte_size && size <= object->byte_size - (uint64_t)source.offset;
    const uint64_t first = valid ? (uint64_t)source.offset * 8 : 0;
    const uint64_t bits = valid ? size * 8 : 0;
    if (valid)
    {
        check_init_clear(walker, selected->type, selected->offset, token);
    }
    for (uint32_t p = 0; valid && p < object->part_count; p += 1)
    {
        const CheckConstPart* part = owner->parts + object->part_first + p;
        const bool overlap = part->bit_offset < first + bits && first < part->bit_offset + part->bit_count;
        if (overlap)
        {
            const uint64_t low = part->bit_offset > first ? part->bit_offset : first;
            const uint64_t high = part->bit_offset + part->bit_count < first + bits ? part->bit_offset + part->bit_count : first + bits;
            const bool exact = low == part->bit_offset && high - low == part->bit_count;
            valid = exact || part->kind == CHECK_CONST_CLEAR || (part->kind == CHECK_CONST_BYTES && low % 8 == 0 && high % 8 == 0);
            if (valid)
            {
                CheckInitPart copied = {
                    .kind = part->kind == CHECK_CONST_VALUE ? CHECK_INIT_VALUE : part->kind == CHECK_CONST_BYTES ? CHECK_INIT_BYTES : CHECK_INIT_CLEAR,
                    .type = part->type,
                    .node = node,
                    .token = token,
                    .bit_offset = selected->offset + low - first,
                    .value = part->value,
                    .bytes = part->bytes,
                    .byte_count = part->byte_count,
                };
                if (copied.kind == CHECK_INIT_BYTES)
                {
                    copied.bytes += (low - part->bit_offset) / 8;
                    copied.byte_count = (high - low) / 8;
                }
                else if (copied.kind == CHECK_INIT_CLEAR && (high - low) % 8 == 0)
                {
                    copied.byte_count = (high - low) / 8;
                }
                else if (high - low != type_size(walker->types, part->type) * 8)
                {
                    valid = high - low <= UINT32_MAX;
                    copied.bit_width = valid ? (uint32_t)(high - low) : 0;
                }
                if (valid)
                {
                    check_init_append(walker, &copied);
                }
            }
        }
    }
    for (uint32_t u = 0; valid && u < object->union_count; u += 1)
    {
        const CheckConstUnion* source_union = owner->unions + object->union_first + u;
        if (source_union->bit_offset >= first && source_union->bit_offset - first < bits)
        {
            if (walker->union_count == walker->union_capacity / 2)
            {
                const uint32_t capacity = walker->union_capacity == 0 ? 16 : walker->union_capacity <= UINT32_MAX / 2 ? walker->union_capacity * 2 : 0;
                CheckInitUnion* entries = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitUnion, capacity) : NULL;
                if (entries == NULL)
                {
                    check_init_fail(walker, CHECK_INIT_RESOURCE, token);
                    valid = false;
                }
                else
                {
                    memset(entries, 0, (uint64_t)capacity * sizeof(*entries));
                    for (uint32_t i = 0; i < walker->union_capacity; i += 1)
                    {
                        const CheckInitUnion* entry = walker->unions + i;
                        if (entry->type != 0)
                        {
                            entries[check_init_union_slot(entries, capacity, entry->type, entry->offset)] = *entry;
                        }
                    }
                    walker->unions = entries;
                    walker->union_capacity = capacity;
                }
            }
            if (valid)
            {
                const uint64_t offset = selected->offset + source_union->bit_offset - first;
                const uint32_t slot = check_init_union_slot(walker->unions, walker->union_capacity, source_union->type, offset);
                walker->union_count += walker->unions[slot].type == 0;
                walker->unions[slot] = (CheckInitUnion){ .type = source_union->type, .offset = offset, .member = source_union->member - 1 };
            }
        }
    }
    if (!valid)
    {
        check_init_fail(walker, CHECK_INIT_NON_CONSTANT, token);
    }
    return valid;
}

static void check_init_value(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t token, uint32_t end,
                             uint32_t node, const CheckInitString* string)
{
    const TypeId destination = selected->type;
    CheckInitSelection value_selection = *selected;
    if (type_kind(walker->types, type_strip_aligned(walker->types, selected->type)) == BUSTER_TYPE_KIND_ATOMIC)
    {
        value_selection.type = type_conversion_operand(walker->types, selected->type);
        selected = &value_selection;
    }
    const CheckInitConstants* previous_constants = walker->constants;
    CheckInitConstants constants = { 0 };
    if (!walker->infer && walker->static_storage && node < walker->stream->count && walker->graph != NULL && walker->constants == NULL &&
        (walker->stream->values[node].constant_kind == CHECK_CONSTANT_NONE ||
         (check_vector_type(walker->types, walker->stream->values[node].type) && check_init_constant_bytes(walker, node) == NULL)))
    {
        check_init_constants(walker, token, end, node, &constants);
        walker->constants = &constants;
    }
    const TypeId type = type_strip_aligned(walker->types, selected->type);
    const uint8_t kind = type_kind(walker->types, type);
    if (!walker->infer && string != 0 && (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY))
    {
        const TypeId element = type_unqualified(type_element(walker->types, type));
        const TypeId source = type_unqualified(type_element(walker->types, string->type));
        const uint64_t unit = type_size(walker->types, element);
        const bool compatible = element == source || (check_init_character(element) && check_init_character(source));
        uint64_t size = type_size(walker->types, type);
        if (!compatible || unit == 0 || string->byte_count < unit || string->byte_count % unit != 0 || string->bytes == 0)
        {
            check_init_fail(walker, CHECK_INIT_INVALID, token);
        }
        else
        {
            if (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && type == walker->incomplete && selected->offset == 0)
            {
                walker->extent = string->byte_count / unit;
                size = string->byte_count;
            }
            else if (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY && type == walker->flexible && selected->offset == walker->flexible_offset)
            {
                check_init_flexible_reset(walker, token);
                walker->flexible_extent = string->byte_count / unit;
                size = string->byte_count;
            }
            if (size < string->byte_count - unit && !walker->ignore_excess)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                if (size < string->byte_count - unit)
                {
                    check_init_warning(walker, token);
                }
                const CheckInitPart part = {
                    .kind = CHECK_INIT_BYTES,
                    .type = selected->type,
                    .node = node,
                    .token = token,
                    .bit_offset = selected->offset,
                    .bytes = string->bytes,
                    .byte_count = size < string->byte_count ? size : string->byte_count,
                };
                check_init_clear(walker, selected->type, selected->offset, token);
                check_init_append(walker, &part);
            }
        }
    }
    else if (!walker->infer)
    {
        if (node == BUSTER_CHECK_NONE || walker->stream->diagnostics[node] != CHECK_DIAG_NONE)
        {
            check_init_fail(walker, CHECK_INIT_CHECK_FAILED, token);
        }
        else if (walker->const_values != NULL && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) &&
                 walker->stream->values[node].category == CHECK_LVALUE && walker->stream->values[node].constant_kind == CHECK_CONSTANT_ADDRESS &&
                 walker->stream->values[node].fault == CHECK_FAULT_NONE &&
                 (walker->stream->values[node].flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_CONST_READ_RESTRICTED)) == 0 &&
                 (type_qualifiers(walker->stream->values[node].type) & BUSTER_TYPE_VOLATILE) == 0 &&
                 check_const_find(walker->const_values, walker->stream->values[node].object, walker->source_position, NULL) != NULL)
        {
            check_init_const_copy(walker, selected, token, node);
        }
        else if (check_vector_type(walker->types, type))
        {
            check_init_vector_value(walker, selected, destination, token, node);
        }
        else
        {
            CheckAction action = {
                .op = CHECK_OP_INITIALIZE,
                .type = destination,
                .node = node,
                .const_values = walker->const_values,
                .constant_evaluated = walker->const_values != NULL,
                .source_position = walker->source_position,
                .extensions = check_init_extensions(walker, token),
            };
            action.operands[0] = check_init_constant_value(walker, node);
            CheckActionResult converted = { 0 };
            uint32_t candidate = 0;
            converted.diagnostic = check_init_overload(walker, &action, &candidate);
            if (converted.diagnostic == CHECK_DIAG_NONE)
            {
                check_action_scalar(walker->types, &action, &converted);
            }
            if (converted.diagnostic != CHECK_DIAG_NONE)
            {
                check_init_fail(walker, converted.diagnostic == CHECK_DIAG_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_INVALID, token);
            }
            else if (walker->static_storage && !walker->defer_constants && (converted.value.constant_kind == CHECK_CONSTANT_NONE ||
                     converted.value.fault != CHECK_FAULT_NONE || (converted.value.flags & BUSTER_CHECK_SIDE_EFFECT) != 0 ||
                     (converted.value.constant_kind == CHECK_CONSTANT_ADDRESS && converted.value.object != 0 &&
                     ((converted.value.flags & BUSTER_CHECK_STATIC_OBJECT) == 0 || selected->width != 0))))
            {
                check_init_fail(walker, CHECK_INIT_NON_CONSTANT, token);
            }
            else
            {
                const CheckInitPart part = {
                    .kind = CHECK_INIT_VALUE,
                    .type = selected->type,
                    .node = node,
                    .token = token,
                    .overload_candidate = candidate,
                    .bit_offset = selected->offset,
                    .bit_width = selected->width,
                    .value = converted.value,
                    .conversion = converted.conversions[0],
                };
                check_init_append(walker, &part);
            }
        }
    }
    walker->constants = previous_constants;
}

#include "c_check_initializer_embed.c"

static bool check_init_walk(CheckInitWalker* walker, TypeId type, uint32_t begin, uint32_t end)
{
    walker->flexible_storage = walker->static_storage && walker->parser->tree.roles[begin] != PARSE_ROLE_OPEN_COMPOUND_BODY;
    walker->capacity = 16;
    walker->frames = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitFrame, walker->capacity);
    if (walker->frames == 0 || walker->capacity == 0)
    {
        check_init_fail(walker, CHECK_INIT_RESOURCE, begin);
    }
    else
    {
        walker->frames[0] = (CheckInitFrame){
            .type = type,
            .wrapper = true,
        };
        walker->depth = 1;
        const TypeId base = type_strip_aligned(walker->types, type);
        walker->incomplete = type_kind(walker->types, base) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY ? base : 0;
    }
    uint32_t token = begin;
    bool after_value = false;
    while (token < end && walker->result->status == CHECK_INIT_OK)
    {
        check_init_repeat(walker, &token, &after_value);
        const uint8_t kind = walker->parser->tokens.kinds[token];
        if (kind == PARSE_KIND_R_BRACE)
        {
            while (walker->depth > 1 && !walker->frames[walker->depth - 1].braced)
            {
                walker->depth -= 1;
            }
            if (walker->depth == 1)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                const CheckInitFrame* closing = walker->frames + walker->depth - 1;
                const uint32_t resume = closing->resume;
                if (closing->copy != 0)
                {
                    check_init_finish_copy(walker, closing->copy, token);
                }
                walker->depth -= 1;
                check_init_advance(walker);
                token = resume != 0 ? resume : token + 1;
                after_value = true;
            }
        }
        else if (after_value)
        {
            if (kind != PARSE_KIND_COMMA || walker->depth == 1)
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            else
            {
                after_value = false;
                token += 1;
            }
        }
        else
        {
            const bool designated = kind == PARSE_KIND_PERIOD || kind == PARSE_KIND_L_SQUARE;
            if (designated)
            {
                check_init_designate(walker, &token, end);
            }
            CheckInitSelection selected = { 0 };
            CheckInitFrame* frame = &walker->frames[walker->depth - 1];
            bool excess = false;
            if (walker->result->status == CHECK_INIT_OK && (token >= end || !check_init_select(walker, frame, &selected)))
            {
                if (token < end && !designated && frame->braced && walker->ignore_excess)
                {
                    const uint32_t stop = check_init_excess_end(walker->parser, token, end);
                    if (stop == token || stop == end)
                    {
                        check_init_fail(walker, CHECK_INIT_INVALID, token);
                    }
                    else
                    {
                        // Expression checking already visited this span. It contributes no initialization,
                        // conversion, runtime evaluation or reference, including calls and string addresses.
                        check_init_warning(walker, token);
                        token = stop;
                        after_value = true;
                        excess = true;
                    }
                }
                else
                {
                    check_init_fail(walker, CHECK_INIT_INVALID, token);
                }
            }
            if (!excess && walker->result->status == CHECK_INIT_OK && !check_init_storage(walker, &selected) &&
                (token + 1 >= end || walker->parser->tokens.kinds[token] != PARSE_KIND_L_BRACE ||
                 walker->parser->tokens.kinds[token + 1] != PARSE_KIND_R_BRACE))
            {
                check_init_fail(walker, CHECK_INIT_INVALID, token);
            }
            if (!excess && walker->result->status == CHECK_INIT_OK)
            {
                check_init_used(walker, frame, token, true);
                if (walker->parser->tokens.kinds[token] == PARSE_KIND_L_BRACE)
                {
                    if (type_strip_aligned(walker->types, selected.type) == walker->flexible && selected.offset == walker->flexible_offset)
                    {
                        check_init_flexible_reset(walker, token);
                    }
                    if (!frame->wrapper)
                    {
                        if (selected.width != 0)
                        {
                            const CheckInitPart clear = {
                                .kind = CHECK_INIT_CLEAR,
                                .type = selected.type,
                                .node = BUSTER_CHECK_NONE,
                                .token = token,
                                .bit_offset = selected.offset,
                                .bit_width = selected.width,
                            };
                            check_init_append(walker, &clear);
                        }
                        else
                        {
                            check_init_clear(walker, selected.type, selected.offset, token);
                        }
                    }
                    check_init_push(walker, &selected, true, token);
                    token += 1;
                }
                else
                {
                    const uint32_t stop = check_init_expression_end(walker->parser, token, end);
                    const uint32_t node = check_init_node(walker, token, stop);
                    const CheckInitString* string = check_init_string(walker, token, stop, node);
                    uint32_t embed_length = 0;
                    const uint8_t* embed = parse_embed_bytes(walker->parser, token, &embed_length);
                    const TypeId element = type_unqualified(type_strip_aligned(walker->types, selected.type));
                    const TypeId string_element = string != 0 ? type_unqualified(type_element(walker->types, string->type)) : 0;
                    const bool string_elements = element == string_element || (check_init_character(element) && check_init_character(string_element));
                    if (stop == token)
                    {
                        check_init_fail(walker, CHECK_INIT_INVALID, token);
                    }
                    else if (embed != NULL && embed_length > 1)
                    {
                        if (stop != token + 1)
                        {
                            check_init_fail(walker, CHECK_INIT_UNSUPPORTED, token);
                        }
                        else
                        {
                            const bool excess_embed = check_init_embed(walker, embed, embed_length, token);
                            token = excess_embed ? check_init_excess_end(walker->parser, stop, end) : stop;
                            after_value = true;
                        }
                    }
                    else if (string != 0 && string_elements && !designated && frame->braced && frame->next == 0 &&
                             (type_kind(walker->types, type_strip_aligned(walker->types, frame->type)) == BUSTER_TYPE_KIND_ARRAY ||
                              type_kind(walker->types, type_strip_aligned(walker->types, frame->type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY) &&
                             !check_init_aggregate(walker->types, selected.type))
                    {
                        const CheckInitSelection array = {
                            .type = frame->type,
                            .offset = frame->offset,
                        };
                        check_init_value(walker, &array, token, stop, node, string);
                        const TypeId array_type = type_strip_aligned(walker->types, frame->type);
                        frame->next = array_type == walker->incomplete ? walker->extent : array_type == walker->flexible ? walker->flexible_extent :
                                      walker->types->types.extent[type_index(array_type)];
                        token = stop;
                        after_value = true;
                    }
                    else
                    {
                        if (!frame->wrapper && frame->next == 0 && check_vector_type(walker->types, frame->type) && node != BUSTER_CHECK_NONE &&
                            check_vector_type(walker->types, walker->stream->values[node].type))
                        {
                            selected = (CheckInitSelection){ .type = frame->type, .offset = frame->offset };
                            frame->next = check_vector_count(walker->types, frame->type) - 1;
                        }
                        if (check_init_whole(walker, &selected, token, stop, node, string))
                        {
                            TypeId root_type = 0;
                            uint64_t source_offset = 0;
                            const uint32_t compound = check_init_compound(walker, &selected, token, stop, node, &root_type, &source_offset);
                            if (compound != BUSTER_CHECK_NONE)
                            {
                                check_init_clear(walker, selected.type, selected.offset, token);
                                CheckInitCopy* copy = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitCopy, 1);
                                const CheckInitSelection root = { .type = root_type };
                                if (copy == 0)
                                {
                                    check_init_fail(walker, CHECK_INIT_RESOURCE, token);
                                }
                                else if (walker->result->status == CHECK_INIT_OK && check_init_push(walker, &root, true, token))
                                {
                                    *copy = (CheckInitCopy){
                                        .source = source_offset,
                                        .destination = selected.offset,
                                        .bits = type_size(walker->types, selected.type) * 8,
                                        .first = walker->result->count,
                                        .unions = walker->unions,
                                        .union_count = walker->union_count,
                                        .union_capacity = walker->union_capacity,
                                    };
                                    walker->frames[walker->depth - 1].resume = stop;
                                    walker->frames[walker->depth - 1].copy = copy;
                                    walker->unions = 0;
                                    walker->union_count = 0;
                                    walker->union_capacity = 0;
                                    token = compound + 1;
                                }
                            }
                            else
                            {
                                check_init_value(walker, &selected, token, stop, node, string);
                                check_init_advance(walker);
                                token = stop;
                                after_value = true;
                            }
                        }
                        else if (frame->wrapper)
                        {
                            check_init_fail(walker, CHECK_INIT_INVALID, token);
                        }
                        else
                        {
                            check_init_push(walker, &selected, false, token);
                        }
                    }
                }
            }
        }
    }
    if (walker->result->status == CHECK_INIT_OK && (walker->depth != 1 || walker->frames[0].next != 1))
    {
        check_init_fail(walker, CHECK_INIT_INVALID, end);
    }
    if (walker->result->status == CHECK_INIT_OK && walker->incomplete != 0)
    {
        TypeId completed = type_array(walker->types, type_element(walker->types, walker->incomplete), walker->extent);
        if (completed == 0)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, begin);
        }
        else
        {
            completed = type_qualify(walker->types, completed, type_qualifiers(type));
            walker->result->type = completed;
        }
    }
    if (walker->result->status == CHECK_INIT_OK)
    {
        walker->result->object_size = type_size(walker->types, walker->result->type);
        if (walker->flexible != 0)
        {
            const uint64_t offset = walker->flexible_offset / 8;
            const uint64_t unit = type_size(walker->types, type_element(walker->types, walker->flexible));
            if (unit == 0 || walker->flexible_extent > (UINT64_MAX - offset) / unit)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, end);
            }
            else
            {
                const uint64_t size = offset + walker->flexible_extent * unit;
                walker->result->object_size = size > walker->result->object_size ? size : walker->result->object_size;
            }
        }
    }
    return walker->result->status == CHECK_INIT_OK;
}

// The diagnostic caller has proved this complete root's dependency closure despite unrelated stream reports.
// Only that caller permits reports; it discards all parts unconditionally and cannot supply emission plans.
static bool check_initializer_plan_warnings(TypeTable* types, const Parser* parser, const uint32_t* token_nodes,
                                   const CheckStream* stream, const CheckInitString* strings, uint32_t string_count, const uint32_t* string_ids,
                                   TypeId type, uint32_t begin, uint32_t end, bool static_storage, bool diagnostic_only, bool ignore_excess,
                                   Arena* output, Arena* scratch, CheckInitializer* result, const CheckGraph* graph,
                                   CheckInitWarningCollector* warnings, const CheckInitObjects* objects)
{
    *result = (CheckInitializer){
        .type = type,
        .failed_token = BUSTER_CHECK_NONE,
        .warning_token = BUSTER_CHECK_NONE,
    };
    const uint64_t output_start = output != 0 ? output->length : 0;
    const uint64_t scratch_start = scratch != 0 ? scratch->length : 0;
    bool valid = types != 0 && parser != 0 && token_nodes != 0 && stream != 0 && output != 0 && scratch != 0 &&
                 output != scratch && type_is_valid(types, type) && begin < end && end <= parser->tokens.count &&
                 (string_count == 0 || (strings != 0 && string_ids != 0)) && (stream->count == 0 || (stream->values != 0 && stream->diagnostics != 0));
    valid = valid && (graph == NULL || (graph->count == stream->count && (graph->count == 0 ||
            (graph->ops != NULL && graph->tokens != NULL && graph->operand0 != NULL && graph->operand1 != NULL && graph->operand2 != NULL))));
    result->status = valid ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    if (valid && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_ATOMIC && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE)
    {
        valid = false;
        result->status = CHECK_INIT_INVALID;
        result->failed_token = begin;
    }
    if (valid && (stream->status != CHECK_STATUS_OK || (!diagnostic_only && stream->report_count != 0)))
    {
        valid = false;
        result->status = CHECK_INIT_CHECK_FAILED;
    }
    if (valid)
    {
        CheckInitWalker walker = {
            .types = types,
            .parser = parser,
            .token_nodes = token_nodes,
            .stream = stream,
            .graph = graph,
            .objects = objects,
            .const_values = static_storage && graph != NULL ? graph->const_values : NULL,
            .source_position = end,
            .warnings = warnings,
            .strings = strings,
            .string_count = string_count,
            .string_ids = string_ids,
            .static_storage = static_storage,
            .defer_constants = ignore_excess,
            .ignore_excess = ignore_excess,
            .output = output,
            .scratch = scratch,
            .result = result,
        };
        valid = check_init_walk(&walker, type, begin, end);
        if (valid && ignore_excess)
        {
            // Source override diagnostics belong here, with original checked writes, before emission discards
            // fully overwritten values and their expression edges. Direct ordered-write clients retain theirs.
            uint8_t* live = NULL;
            result->status = check_initializer_live_writes(types, result, scratch, &live);
            valid = result->status == CHECK_INIT_OK;
            uint32_t kept = 0;
            for (uint32_t p = 0; valid && p < result->count; p += 1)
            {
                if (live[p])
                {
                    result->parts[kept++] = result->parts[p];
                }
            }
            result->count = valid ? kept : result->count;
            for (uint32_t p = 0; valid && static_storage && p < result->count; p += 1)
            {
                const CheckInitPart* part = result->parts + p;
                const CheckValue* value = &part->value;
                if (part->kind == CHECK_INIT_VALUE && (value->constant_kind == CHECK_CONSTANT_NONE || value->fault != CHECK_FAULT_NONE ||
                    (value->flags & BUSTER_CHECK_SIDE_EFFECT) != 0 || (value->constant_kind == CHECK_CONSTANT_ADDRESS && value->object != 0 &&
                    ((value->flags & BUSTER_CHECK_STATIC_OBJECT) == 0 || part->bit_width != 0))))
                {
                    result->status = CHECK_INIT_NON_CONSTANT;
                    result->failed_token = part->token;
                    valid = false;
                }
            }
        }
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_start);
    }
    if (!valid)
    {
        if (output != 0)
        {
            arena_pop_to(output, output_start);
        }
        result->parts = 0;
        result->count = 0;
    }
    return valid;
}

static bool check_initializer_plan(TypeTable* types, const Parser* parser, const uint32_t* token_nodes,
                                   const CheckStream* stream, const CheckInitString* strings, uint32_t string_count, const uint32_t* string_ids,
                                   TypeId type, uint32_t begin, uint32_t end, bool static_storage, bool diagnostic_only, bool ignore_excess,
                                   Arena* output, Arena* scratch, CheckInitializer* result, const CheckGraph* graph)
{
    return check_initializer_plan_warnings(types, parser, token_nodes, stream, strings, string_count, string_ids, type, begin, end,
                                           static_storage, diagnostic_only, ignore_excess, output, scratch, result, graph, NULL, NULL);
}

static bool check_initializer_run(TypeTable* types, const Parser* parser, const uint32_t* token_nodes,
                                  const CheckStream* stream, const CheckInitString* strings, uint32_t string_count, const uint32_t* string_ids,
                                  TypeId type, uint32_t begin, uint32_t end, bool static_storage,
                                  Arena* output, Arena* scratch, CheckInitializer* result)
{
    return check_initializer_plan(types, parser, token_nodes, stream, strings, string_count, string_ids, type, begin, end,
                                  static_storage, false, false, output, scratch, result, NULL);
}

// Clang's ordinary C language modes diagnose excess elements as warnings. Their expressions remain in the
// checked graph, but source-compatible plans omit them. Direct consumers can request the strict API above.
static __attribute__((unused)) bool check_initializer_source(TypeTable* types, const Parser* parser, const uint32_t* token_nodes,
                                     const CheckStream* stream, const CheckInitString* strings, uint32_t string_count, const uint32_t* string_ids,
                                     TypeId type, uint32_t begin, uint32_t end, bool static_storage,
                                     Arena* output, Arena* scratch, CheckInitializer* result)
{
    return check_initializer_plan(types, parser, token_nodes, stream, strings, string_count, string_ids, type, begin, end,
                                  static_storage, false, true, output, scratch, result, NULL);
}

static bool check_initializer_infer_array(TypeTable* types, const Parser* parser, const InternTable* identifiers, TypeId type, uint32_t begin,
                                          uint32_t end, const uint64_t* designator_indices, const uint8_t* designator_valid,
                                          const TypeId* expression_types, const TypeId* cast_types, Arena* scratch, TypeId* completed,
                                          uint32_t* failed_token, CheckInitStatus* status)
{
    CheckInitializer result = {
        .type = type,
        .failed_token = BUSTER_CHECK_NONE,
    };
    const uint64_t scratch_start = scratch != 0 ? scratch->length : 0;
    bool valid = types != 0 && parser != 0 && scratch != 0 && type_is_valid(types, type) && begin < end && end <= parser->tokens.count;
    result.status = valid ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    if (valid)
    {
        CheckInitWalker walker = {
            .types = types,
            .parser = parser,
            .indices = designator_indices,
            .index_valid = designator_valid,
            .expression_types = expression_types,
            .cast_types = cast_types,
            .identifiers = identifiers,
            .infer = true,
            .ignore_excess = true,
            .scratch = scratch,
            .result = &result,
        };
        valid = check_init_walk(&walker, type, begin, end);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_start);
    }
    *completed = valid ? result.type : 0;
    *failed_token = result.failed_token;
    if (status != 0)
    {
        *status = result.status;
    }
    return valid;
}

typedef struct CheckInitReference CheckInitReference;
struct CheckInitReference
{
    uint64_t offset;
    uint32_t part;
};

static uint32_t check_init_reference_position(const CheckInitReference* references, uint32_t count, uint64_t offset)
{
    uint32_t begin = 0;
    uint32_t end = count;
    while (begin < end)
    {
        const uint32_t middle = begin + (end - begin) / 2;
        if (references[middle].offset < offset)
        {
            begin = middle + 1;
        }
        else
        {
            end = middle;
        }
    }
    return begin;
}

static uint32_t check_init_uncovered(uint32_t* next, uint32_t index)
{
    uint32_t root = index;
    while (next[root] != root)
    {
        root = next[root];
    }
    while (next[index] != index)
    {
        const uint32_t previous = index;
        index = next[index];
        next[previous] = root;
    }
    return root;
}

static CheckInitStatus check_initializer_part_bits(const TypeTable* types, const CheckInitPart* part, uint64_t* bits)
{
    CheckInitStatus status = CHECK_INIT_OK;
    *bits = 0;
    if (!type_is_valid(types, part->type) || (uint32_t)part->kind > CHECK_INIT_CLEAR)
    {
        status = CHECK_INIT_INVALID;
    }
    else
    {
        const uint64_t bytes = part->kind == CHECK_INIT_VALUE ? type_size(types, part->type) : part->byte_count;
        if (part->bit_width != 0)
        {
            *bits = part->bit_width;
        }
        else if (bytes > UINT64_MAX / 8)
        {
            status = CHECK_INIT_INVALID;
        }
        else
        {
            *bits = bytes * 8;
        }
        if (*bits > UINT64_MAX - part->bit_offset)
        {
            status = CHECK_INIT_INVALID;
        }
    }
    return status;
}

#include "c_check_init_live.c"

// Each candidate pointer start is a point. A later bit-range write covers the points whose eight-byte fields it
// intersects. Walk writes backwards; a disjoint-set next-uncovered index visits every point at most once. Stable
// radix sorting and range searches avoid quadratic insertion/movement for reverse-ordered designated arrays.
static CheckInitStatus check_initializer_live(const TypeTable* types, const CheckInitializer* initializer, Arena* scratch,
                                               uint8_t** selected, uint32_t* count)
{
    *selected = 0;
    *count = 0;
    CheckInitStatus status = types != 0 && initializer != 0 && scratch != 0 && initializer->status == CHECK_INIT_OK &&
                             (initializer->count == 0 || initializer->parts != 0) ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    uint32_t candidates = 0;
    for (uint32_t i = 0; status == CHECK_INIT_OK && i < initializer->count; i += 1)
    {
        const CheckInitPart* part = initializer->parts + i;
        uint64_t bits = 0;
        status = check_initializer_part_bits(types, part, &bits);
        if (status == CHECK_INIT_OK && part->kind == CHECK_INIT_VALUE && part->value.constant_kind == CHECK_CONSTANT_ADDRESS && part->value.object != 0)
        {
            uint32_t width = 0;
            bool is_signed = false;
            const bool integer = check_action_integer_type(types, part->type, &width, &is_signed) && width == 64;
            if (bits != 64 || part->bit_width != 0 || (part->bit_offset % 8) != 0 ||
                (!integer && type_kind(types, type_strip_aligned(types, part->type)) != BUSTER_TYPE_KIND_POINTER))
            {
                status = CHECK_INIT_INVALID;
            }
            else
            {
                candidates += 1;
            }
        }
    }
    CheckInitReference* references = 0;
    CheckInitReference* temporary = 0;
    uint32_t* positions = 0;
    uint32_t* next = 0;
    uint8_t* live = 0;
    if (status == CHECK_INIT_OK && candidates != 0)
    {
        references = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitReference, candidates);
        temporary = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitReference, candidates);
        positions = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, initializer->count);
        next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)candidates + 1);
        live = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, initializer->count);
        if (references == 0 || temporary == 0 || positions == 0 || next == 0 || live == 0)
        {
            status = CHECK_INIT_RESOURCE;
        }
        else
        {
            memset(live, 0, initializer->count);
            uint32_t at = 0;
            for (uint32_t i = 0; i < initializer->count; i += 1)
            {
                const CheckInitPart* part = initializer->parts + i;
                positions[i] = UINT32_MAX;
                if (part->kind == CHECK_INIT_VALUE && part->value.constant_kind == CHECK_CONSTANT_ADDRESS && part->value.object != 0)
                {
                    references[at] = (CheckInitReference){
                        .offset = part->bit_offset,
                        .part = i,
                    };
                    at += 1;
                }
            }
        }
    }
    for (uint32_t shift = 0; status == CHECK_INIT_OK && candidates > 1 && shift < 64; shift += 8)
    {
        uint32_t counts[256] = { 0 };
        for (uint32_t i = 0; i < candidates; i += 1)
        {
            counts[(references[i].offset >> shift) & 255] += 1;
        }
        uint32_t total = 0;
        for (uint32_t bucket = 0; bucket < 256; bucket += 1)
        {
            const uint32_t size = counts[bucket];
            counts[bucket] = total;
            total += size;
        }
        for (uint32_t i = 0; i < candidates; i += 1)
        {
            const uint32_t bucket = (uint32_t)((references[i].offset >> shift) & 255);
            temporary[counts[bucket]] = references[i];
            counts[bucket] += 1;
        }
        CheckInitReference* swap = references;
        references = temporary;
        temporary = swap;
    }
    uint32_t unique = 0;
    for (uint32_t i = 0; status == CHECK_INIT_OK && i < candidates; i += 1)
    {
        const CheckInitReference reference = references[i];
        if (unique == 0 || references[unique - 1].offset != reference.offset)
        {
            references[unique] = reference;
            unique += 1;
        }
        positions[reference.part] = unique - 1;
    }
    if (status == CHECK_INIT_OK && candidates != 0)
    {
        for (uint32_t i = 0; i <= unique; i += 1)
        {
            next[i] = i;
        }
        for (uint32_t i = initializer->count; i != 0; i -= 1)
        {
            const uint32_t part_index = i - 1;
            const CheckInitPart* part = initializer->parts + part_index;
            uint64_t bits = 0;
            status = check_initializer_part_bits(types, part, &bits);
            assert(status == CHECK_INIT_OK);
            if (positions[part_index] != UINT32_MAX && check_init_uncovered(next, positions[part_index]) == positions[part_index])
            {
                live[part_index] = 1;
                *count += 1;
            }
            if (bits != 0)
            {
                const uint32_t begin = check_init_reference_position(references, unique, part->bit_offset >= 63 ? part->bit_offset - 63 : 0);
                const uint32_t end = check_init_reference_position(references, unique, part->bit_offset + bits);
                uint32_t at = check_init_uncovered(next, begin);
                while (at < end)
                {
                    next[at] = check_init_uncovered(next, at + 1);
                    at = next[at];
                }
            }
        }
        *selected = live;
    }
    return status;
}

// Public object-ID slices retain source order and duplicate IDs from distinct surviving fields. The emitter
// uses the same live-part classification, so discarded literals and foreign symbols need no shared symbol.
static __attribute__((unused)) CheckInitStatus check_initializer_references(const TypeTable* types, const CheckInitializer* initializer,
                                                                          Arena* output, Arena* scratch, uint32_t** objects, uint32_t* count)
{
    *objects = 0;
    *count = 0;
    const uint64_t output_mark = output != 0 ? output->length : 0;
    const uint64_t scratch_mark = scratch != 0 ? scratch->length : 0;
    uint8_t* selected = 0;
    uint32_t used = 0;
    CheckInitStatus status = output != 0 && scratch != 0 && output != scratch ?
                             check_initializer_live(types, initializer, scratch, &selected, &used) : CHECK_INIT_INVALID;
    uint32_t* result = 0;
    if (status == CHECK_INIT_OK && used != 0)
    {
        result = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, used);
        if (result == 0)
        {
            status = CHECK_INIT_RESOURCE;
        }
        else
        {
            uint32_t at = 0;
            for (uint32_t i = 0; i < initializer->count; i += 1)
            {
                if (selected[i])
                {
                    result[at] = initializer->parts[i].value.object;
                    at += 1;
                }
            }
            assert(at == used);
        }
    }
    if (status == CHECK_INIT_OK)
    {
        *objects = result;
        *count = used;
    }
    else if (output != 0)
    {
        arena_pop_to(output, output_mark);
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, scratch_mark);
    }
    return status;
}
#endif
