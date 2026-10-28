// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retained source-history handoff from the sequential checker to #113. Include after c_environment.c. A successful
// check_input publishes this snapshot before releasing its reader scratch. It contains real declaration/use/restore
// events, declaration-specific types, tag completion positions and parser-derived body slots. It does not defer the
// resolver or make the shared TypeTable writable by body workers. Body-local types and linkage effects still need
// private construction and ordered reconciliation before an actual deferred checker can consume this handoff.
// valid certifies a coherent retained source history. check_graph_run still determines expression acceptance;
// publication alone does not authorize accepting a translation unit or launching a deferred checker.
//
// The snapshot owns exactly count events (no padding), without mutable heads, an undo stack or a previous column.
// Only c_bindings_scalar/batch and read-only environment APIs may consume that CBindings view; never append, reset
// or call c_bindings_lookup on it. Every pointer survives reader scratch reset. declaration_types borrows the
// CheckInput output column, and TypeIds borrow its TypeTable. Keep those arenas and table unchanged until readers
// join. CheckEnvironment itself may move: its index points to an arena-owned CBindings, not an embedded struct.
// Restore events define only operation/declaration/point; their other column entries remain unspecified, as in the
// producer. Consumers classify operation before reading those fields rather than depending on zeroed arena pages.

typedef struct CheckEnvironment CheckEnvironment;
struct CheckEnvironment
{
    const CBindings* bindings;
    CEnvironment file;
    const TypeId* declaration_types;
    const uint32_t* tag_completions;
    CBodyRange* bodies;
    uint32_t body_count;
    uint32_t tag_count;
    uint32_t token_count;
    uint64_t retained_bytes;
    bool valid;
};

// Count the parser's body roles before allocating their exact range column. The two parser paths exercise the
// scalar/vector twins. Bounded masked loads include partial tails; no token bytes are reparsed here.
static uint32_t check_environment_body_count(const Parser* parser)
{
    uint32_t count = 0;
    if (BUSTER_SCALAR_PATH(parser->options.scalar))
    {
        for (uint32_t token = 0; token < parser->tokens.count; token += 1)
        {
            count += parser->tree.roles[token] == PARSE_ROLE_CLOSE_BODY;
        }
    }
    else
    {
        for (uint64_t block = 0; block < parser->tokens.count; block += 64)
        {
            const uint32_t live = parser->tokens.count - block < 64 ? (uint32_t)(parser->tokens.count - block) : 64;
            const __mmask64 mask = live == 64 ? ~(__mmask64)0 : (((__mmask64)1 << live) - 1);
            const __m512i roles = _mm512_maskz_loadu_epi8(mask, parser->tree.roles + block);
            count += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(mask, roles, _mm512_set1_epi8(PARSE_ROLE_CLOSE_BODY)));
        }
    }
    return count;
}

// Inputs are a finalized successful reader's event stream and type/completion columns. Completions are token
// positions after closing braces, with UINT32_MAX for missing/incomplete entries. The arenas must be distinct;
// preexisting inputs may share either arena below its entry mark. Failure empties the handoff and rolls back only
// these allocations. The caller discards its unit on failure; existing sequential outputs remain diagnostic data.
static bool check_environment_publish(CheckEnvironment* environment, const Parser* parser, const CBindings* source,
                                      const TypeId* declaration_types, const TypeTable* types, const uint32_t* completions, Arena* arena, Arena* scratch)
{
    *environment = (CheckEnvironment){ 0 };
    const uint64_t mark = arena->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = source->count;
    bool valid = arena != scratch && source->valid && count <= source->capacity && count != UINT32_MAX &&
                 parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE && declaration_types != 0;
    CBindings* bindings = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, CBindings, 1) : 0;
    valid &= bindings != 0;
    if (valid)
    {
        *bindings = (CBindings)
        {
            .name_capacity = source->name_capacity,
            .capacity = count,
            .count = count,
            .valid = true,
        };
    }
#define BUSTER_CHECK_ENVIRONMENT_COPY(field, type) \
    if (valid && count != 0) \
    { \
        bindings->field = arena_push_array(arena, count, sizeof(type), 64); \
        valid = bindings->field != 0; \
        if (valid) \
        { \
            memcpy(bindings->field, source->field, (uint64_t)count * sizeof(type)); \
        } \
    }
    BUSTER_CHECK_ENVIRONMENT_COPY(key, uint32_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(declaration, uint32_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(point, uint32_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(end, uint32_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(depth, uint32_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(operation, uint8_t);
    BUSTER_CHECK_ENVIRONMENT_COPY(kind, uint8_t);
#undef BUSTER_CHECK_ENVIRONMENT_COPY
    const uint32_t tags = types->tags.count;
    uint32_t* retained_completions = valid && tags != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, tags) : 0;
    valid &= tags == 0 || retained_completions != 0;
    if (valid && tags != 0)
    {
        memcpy(retained_completions, completions, (uint64_t)tags * sizeof(uint32_t));
    }
    const uint32_t capacity = valid ? check_environment_body_count(parser) : 0;
    CBodyRange* bodies = valid && capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, CBodyRange, capacity) : 0;
    valid &= capacity == 0 || bodies != 0;
    uint32_t body_count = 0;
    valid = valid && c_environment_bodies(&parser->tree, parser->tokens.count, bodies, capacity, &body_count);
    valid = valid && c_environment_build(&environment->file, bindings, arena, scratch);
    if (valid)
    {
        environment->bindings = bindings;
        environment->declaration_types = declaration_types;
        environment->tag_completions = retained_completions;
        environment->bodies = bodies;
        environment->body_count = body_count;
        environment->tag_count = tags;
        environment->token_count = parser->tokens.count;
        environment->retained_bytes = arena->length - mark;
        environment->valid = true;
    }
    else
    {
        arena_pop_to(arena, mark);
        *environment = (CheckEnvironment){ 0 };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
