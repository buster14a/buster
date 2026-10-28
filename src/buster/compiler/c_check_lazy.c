// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Optional system-header body checking (#155). Include after c_environment.c and c_parser_bindings.c, before
// c_check_input.c. Syntax is always parsed. This adapter uses the parser's resolved declaration identities to
// prove which system static/inline bodies are unreachable from ordinary bodies and file-scope uses. It never
// substitutes its conservative checking-demand graph for the checked semantic references used for emission.
// The shared #129 solver supplies independent scalar/vector traversals; INLINE_ONLY is deliberately absent here:
// referencing an inline body demands semantic checking even when that body has no emitted definition.
//
// All token intervals retain their original identities and order. The selected reader visits the unit once in
// that order, so later redeclarations, completions and pragmas cannot authorize an earlier selected body. It
// checks every declaration/header, and skips only complete body intervals marked by deferred_tokens. A function
// typedef that cannot be classified before type checking is retained conservatively. Assembly and attributes
// with opaque symbol references also retain bodies; this adapter does not guess references from string spelling.
//
// Output rows and the count+1 byte mask belong to output, with no padding contract. Input/parser/PP storage is
// borrowed only during planning. The selected reader copies the mask. Failure clears the result and restores
// both arena marks; success also releases all temporary bindings/graph storage. Empty units have a zero sentinel.

#ifndef BUSTER_CHECK_LAZY_INCLUDED
#define BUSTER_CHECK_LAZY_INCLUDED 1

#include "buster/compiler/c_lowering.c"

#define BUSTER_CHECK_LAZY_SYSTEM UINT32_C(1)
#define BUSTER_CHECK_LAZY_STATIC UINT32_C(2)
#define BUSTER_CHECK_LAZY_INLINE UINT32_C(4)
#define BUSTER_CHECK_LAZY_EXTERN UINT32_C(8)
#define BUSTER_CHECK_LAZY_RETAIN UINT32_C(16)
#define BUSTER_CHECK_LAZY_OPAQUE UINT32_C(32)
#define BUSTER_CHECK_LAZY_TYPEDEF UINT32_C(64)

typedef enum CheckLazyStatus
{
    CHECK_LAZY_INVALID,
    CHECK_LAZY_OK,
    CHECK_LAZY_RESOURCE,
} CheckLazyStatus;

typedef struct CheckLazyBody CheckLazyBody;
struct CheckLazyBody
{
    uint32_t begin;
    uint32_t end;
    uint32_t declaration;
    uint32_t flags;
    bool selected;
};

typedef struct CheckLazy CheckLazy;
struct CheckLazy
{
    CheckLazyBody* bodies;
    uint8_t* deferred_tokens;
    uint32_t token_count;
    uint32_t body_count;
    uint32_t selected_count;
    uint32_t deferred_count;
    uint32_t reference_count;
    uint32_t binding_event_count;
    uint64_t retained_bytes;
    CBindingStatistics bindings;
    CLoweringStatistics reachability;
    CheckLazyStatus status;
};

// This checks shape, not eligibility. The producer owns system-header/root policy; the source consumer may use
// this before copying a caller-supplied mask. Any nonzero run must include both braces of a complete function.
static bool check_lazy_mask_valid(const Parser* parser, const uint8_t* mask)
{
    bool valid = parser != 0;
    if (valid && mask != 0)
    {
        const uint32_t count = parser->tokens.count;
        valid = mask[count] == 0 && parser->tree.roles != 0 && parser->tree.parents != 0;
        for (uint32_t token = 0; valid && token < count; token += 1)
        {
            if (mask[token] != 0)
            {
                const uint32_t close = parser->tree.parents[token];
                valid = mask[token] == 1 && parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY && close > token && close < count &&
                        parser->tree.roles[close] == PARSE_ROLE_CLOSE_BODY;
                for (uint32_t inside = token; valid && inside <= close; inside += 1)
                {
                    valid = mask[inside] == 1;
                }
                token = valid ? close : token;
            }
        }
    }
    return valid;
}

// Expansion location, including argument forwarding and generated tokens, determines where a definition was
// written. A macro defined in a system header but invoked in ordinary source must not make that source lazy.
// `origin` is the output token's (pp_history_at's). Returns 0 when its history doesn't lead to a location.
static uint32_t check_lazy_expansion_location(const Preprocessor* pp, uint32_t token, uint32_t origin)
{
    return pp_expansion_location(pp, token, origin);
}

static bool check_lazy_system(const Preprocessor* pp, uint32_t token)
{
    bool system = false;
    const uint32_t location = check_lazy_expansion_location(pp, token, pp_history_at(pp, token));
    const uint32_t range = location != 0 ? pp_find_range(pp, location) : BUSTER_PP_NONE;
    if (range != BUSTER_PP_NONE && range < pp->range_count)
    {
        const PPRange* source = pp->ranges + range;
        uint8_t classification = source->system;
        uint32_t note = source->first_line_directive;
        for (uint32_t steps = 0; note != BUSTER_PP_NONE && note < pp->line_directive_count && steps < pp->line_directive_count; steps += 1)
        {
            const PPLineDirective* directive = pp->line_directives + note;
            if (directive->location <= location)
            {
                classification = directive->system;
            }
            note = directive->next_in_range;
        }
        system = classification == BUSTER_PP_SYSTEM_FILE || classification == BUSTER_PP_EXTERN_C_FILE;
    }
    return system;
}

static bool check_lazy_name(const InternTable* names, IdentId id, const char* expected)
{
    bool equal = id != 0 && id < names->count;
    if (equal)
    {
        const InternSpan span = names->spans[id];
        const uint8_t* bytes = (const uint8_t*)names->records + span.offset;
        uint32_t length = span.length;
        if (length >= 4 && bytes[0] == '_' && bytes[1] == '_' && bytes[length - 2] == '_' && bytes[length - 1] == '_')
        {
            bytes += 2;
            length -= 4;
        }
        equal = length == strlen(expected) && memcmp(bytes, expected, length) == 0;
    }
    return equal;
}

static uint32_t check_lazy_header(const Parser* parser, const Preprocessor* pp, uint32_t first, uint32_t end, const ParseBindings* bindings)
{
    uint32_t flags = 0;
    for (uint32_t token = first; token < end; token += 1)
    {
        const uint8_t role = parser->tree.roles[token];
        const uint32_t name = parser->tokens.names[token];
        if (role == PARSE_ROLE_SPECIFIER)
        {
            flags |= name == C_NAME_STATIC ? BUSTER_CHECK_LAZY_STATIC : 0;
            flags |= name == C_NAME_EXTERN ? BUSTER_CHECK_LAZY_EXTERN : 0;
            flags |= name == C_NAME_INLINE || name == C_NAME_GNU_INLINE || name == C_NAME_GNU_INLINE2 ? BUSTER_CHECK_LAZY_INLINE : 0;
            flags |= bindings->kinds[token] == C_BINDING_TYPEDEF ? BUSTER_CHECK_LAZY_TYPEDEF : 0;
        }
        if (gnu_syntax_attribute_role(role))
        {
            const uint32_t close = parser->tree.parents[token];
            for (uint32_t attribute = token + 1; attribute < close && attribute < end; attribute += 1)
            {
                const uint32_t id = parser->tokens.names[attribute];
                const bool retained = check_lazy_name(pp->table, id, "used") || check_lazy_name(pp->table, id, "retain") ||
                                      check_lazy_name(pp->table, id, "constructor") || check_lazy_name(pp->table, id, "destructor") ||
                                      check_lazy_name(pp->table, id, "export_name") || check_lazy_name(pp->table, id, "gnu_inline");
                flags |= retained ? BUSTER_CHECK_LAZY_RETAIN : 0;
            }
        }
        const uint32_t close = parser->tree.parents[token];
        if (parse_closer_roles[role] != PARSE_ROLE_NONE && close > token && close < end)
        {
            token = close;
        }
    }
    return flags;
}

static bool check_lazy_plan(CheckLazy* result, const Parser* parser, const Preprocessor* pp, Arena* output, Arena* scratch, bool scalar)
{
    (void)scalar;

    const uint64_t mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    CheckLazy candidate = { .status = CHECK_LAZY_INVALID };
    bool valid = output != scratch && parser != 0 && pp != 0 && parser->fatal == PARSE_MESSAGE_NONE && parser->errors == 0 &&
                 parser->tree.roles != 0 && parser->tree.parents != 0 && parser->tree.depths != 0 && parser->tokens.sources != 0 && pp->table != 0;
    const uint32_t count = valid ? parser->tokens.count : 0;
    const uint64_t slots = (uint64_t)count + 1;
    const uint32_t names = valid ? parser->meanings.capacity : 0;
    valid &= names != 0;
    uint32_t body_count = 0;
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        body_count += parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY;
    }
    candidate.status = valid ? CHECK_LAZY_RESOURCE : CHECK_LAZY_INVALID;
    candidate.bodies = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckLazyBody, body_count) : 0;
    candidate.deferred_tokens = valid && (body_count == 0 || candidate.bodies != 0) ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, slots) : 0;
    valid &= candidate.deferred_tokens != 0;
    const uint64_t temporary = output->length;
    CBodyRange* ranges = valid && body_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CBodyRange, body_count) : 0;
    valid &= body_count == 0 || ranges != 0;
    uint32_t found = 0;
    valid = valid && c_environment_bodies(&parser->tree, count, ranges, body_count, &found) && found == body_count;
    ParseBindings bindings = { 0 };
    Parser replay = valid ? *parser : (Parser){ 0 };
    replay.scratch = *scratch;
    valid = valid && parse_bindings_build(&bindings, &replay, output) && parse_bindings_resolve(&bindings, BUSTER_SCALAR_PATH(scalar), scratch);
    uint32_t* owners = valid ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    uint32_t* firsts = owners != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    uint32_t* targets = firsts != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, names) : 0;
    uint32_t* flags = targets != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)body_count + 1) : 0;
    uint32_t* sources = flags != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    uint32_t* destinations = sources != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    uint32_t* kinds = destinations != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, slots) : 0;
    valid &= kinds != 0;
    uint32_t references = 0;
    if (valid)
    {
        memset(candidate.deferred_tokens, 0, slots);
        memset(owners, 0, slots * sizeof(uint32_t));
        memset(targets, 0, (uint64_t)names * sizeof(uint32_t));
        memset(flags, 0, ((uint64_t)body_count + 1) * sizeof(uint32_t));
        uint32_t body = 0;
        uint32_t active = 0;
        uint32_t first = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            firsts[token] = first;
            if (body < body_count && token == ranges[body].begin)
            {
                candidate.bodies[body] = (CheckLazyBody)
                {
                    .begin = ranges[body].begin,
                    .end = ranges[body].end,
                    .declaration = BUSTER_PARSE_NONE,
                };
                active = body + 1;
            }
            owners[token] = active;
            if (active != 0 && token + 1 == ranges[body].end)
            {
                active = 0;
                body += 1;
                first = token + 1;
            }
            else if ((active != 0 && (parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY || parser->tree.roles[token] == PARSE_ROLE_OPEN_BLOCK ||
                                     parser->tree.roles[token] == PARSE_ROLE_CLOSE_BLOCK || parser->tree.roles[token] == PARSE_ROLE_TERMINATOR)) ||
                     (active == 0 && parser->tree.parents[token] == BUSTER_PARSE_ROOT))
            {
                first = token + 1;
            }
        }
        for (uint32_t event = 0; event < bindings.events.count; event += 1)
        {
            const CBindings* events = &bindings.events;
            if (events->operation[event] == C_BINDING_DECLARE && events->kind[event] == C_BINDING_FUNCTION && events->depth[event] == 0 &&
                events->key[event] < names && events->point[event] < count && owners[events->point[event]] != 0)
            {
                const uint32_t slot = owners[events->point[event]] - 1;
                CheckLazyBody* row = candidate.bodies + slot;
                if (events->point[event] == row->begin)
                {
                    row->declaration = events->declaration[event];
                    row->flags = check_lazy_header(parser, pp, firsts[row->declaration], row->begin, &bindings);
                    const bool system = check_lazy_system(pp, parser->tokens.sources[row->declaration]) && check_lazy_system(pp, parser->tokens.sources[row->begin]);
                    row->flags |= system ? BUSTER_CHECK_LAZY_SYSTEM : 0;
                    if (targets[events->key[event]] != 0)
                    {
                        candidate.bodies[targets[events->key[event]] - 1].flags |= BUSTER_CHECK_LAZY_RETAIN;
                        row->flags |= BUSTER_CHECK_LAZY_RETAIN;
                    }
                    targets[events->key[event]] = slot + 1;
                }
            }
        }
        bool opaque = pp->options.no_history;
        for (uint32_t token = 0; token < count; token += 1)
        {
            opaque |= parser->tokens.kinds[token] == PARSE_KIND_ASM;
            const uint32_t name = parser->tokens.names[token];
            opaque |= check_lazy_name(pp->table, name, "alias") || check_lazy_name(pp->table, name, "ifunc") || check_lazy_name(pp->table, name, "cleanup");
            if (owners[token] != 0 && (check_lazy_name(pp->table, name, "used") || check_lazy_name(pp->table, name, "retain")))
            {
                candidate.bodies[owners[token] - 1].flags |= BUSTER_CHECK_LAZY_RETAIN;
            }
            const uint32_t event = bindings.token_events[token];
            if (event == 0 || bindings.namespaces[token] != C_BINDING_ORDINARY || name >= names || targets[name] == 0)
            {
                continue;
            }
            CheckLazyBody* row = candidate.bodies + targets[name] - 1;
            if (bindings.events.operation[event - 1] == C_BINDING_DECLARE && bindings.events.depth[event - 1] == 0)
            {
                const uint32_t declaration_flags = check_lazy_header(parser, pp, firsts[token], bindings.events.point[event - 1], &bindings);
                row->flags |= declaration_flags & BUSTER_CHECK_LAZY_RETAIN;
                if ((row->flags & BUSTER_CHECK_LAZY_STATIC) == 0 && ((declaration_flags & BUSTER_CHECK_LAZY_INLINE) == 0 ||
                    (declaration_flags & BUSTER_CHECK_LAZY_EXTERN) != 0))
                {
                    row->flags |= BUSTER_CHECK_LAZY_RETAIN;
                }
            }
            if (bindings.events.operation[event - 1] == C_BINDING_USE && bindings.declarations[token] != BUSTER_PARSE_NONE)
            {
                const uint32_t declaration = bindings.declarations[token];
                const uint32_t declared_event = bindings.token_events[declaration];
                bool function = bindings.kinds[token] == C_BINDING_FUNCTION;
                if (!function && bindings.kinds[token] == C_BINDING_OBJECT && declared_event != 0)
                {
                    // A typedef can hide a function type. Retain that possible dependency; a plain block object
                    // or enumerator with the same spelling is independently resolved and creates no edge.
                    const uint32_t declaration_flags = check_lazy_header(parser, pp, firsts[declaration], declaration, &bindings);
                    function = bindings.events.depth[declared_event - 1] == 0 || (declaration_flags & BUSTER_CHECK_LAZY_TYPEDEF) != 0;
                }
                if (function && owners[token] == 0)
                {
                    row->flags |= BUSTER_CHECK_LAZY_RETAIN;
                }
                else if (function)
                {
                    sources[references] = owners[token] - 1;
                    destinations[references] = targets[name] - 1;
                    kinds[references++] = C_LOWERING_REFERENCE_EVALUATED;
                }
            }
        }
        for (uint32_t body = 0; body < body_count; body += 1)
        {
            CheckLazyBody* row = candidate.bodies + body;
            valid &= row->declaration != BUSTER_PARSE_NONE;
            row->flags |= opaque ? BUSTER_CHECK_LAZY_OPAQUE : 0;
            const bool eligible = (row->flags & BUSTER_CHECK_LAZY_SYSTEM) != 0 && (row->flags & (BUSTER_CHECK_LAZY_STATIC | BUSTER_CHECK_LAZY_INLINE)) != 0 &&
                                  (row->flags & (BUSTER_CHECK_LAZY_RETAIN | BUSTER_CHECK_LAZY_OPAQUE)) == 0;
            flags[body] = BUSTER_C_LOWERING_FUNCTION | (eligible ? 0 : BUSTER_C_LOWERING_USED);
        }
        CLowering plan = { 0 };
        const CLoweringInput graph =
        {
            .definition_count = body_count,
            .reference_count = references,
            .definition_flags = flags,
            .reference_source = sources,
            .reference_target = destinations,
            .reference_kind = kinds,
        };
        valid = valid && c_lowering_prepare(scratch, graph, &plan) == C_LOWERING_OK;
        valid = valid && (BUSTER_SCALAR_PATH(scalar) ? c_lowering_scalar(&plan, C_LOWERING_REACHABLE) : c_lowering_vector(&plan, C_LOWERING_REACHABLE)) == C_LOWERING_OK;
        if (valid)
        {
            for (uint32_t body = 0; body < body_count; body += 1)
            {
                CheckLazyBody* row = candidate.bodies + body;
                row->selected = (plan.reached[body / 64] & (UINT64_C(1) << (body % 64))) != 0;
                if (!row->selected)
                {
                    memset(candidate.deferred_tokens + row->begin, 1, row->end - row->begin);
                }
            }
            candidate.token_count = count;
            candidate.body_count = body_count;
            candidate.selected_count = plan.selected_count;
            candidate.deferred_count = body_count - plan.selected_count;
            candidate.reference_count = references;
            candidate.binding_event_count = bindings.events.count;
            candidate.bindings = bindings.statistics;
            candidate.reachability = plan.statistics;
            candidate.retained_bytes = temporary - mark;
            candidate.status = CHECK_LAZY_OK;
            valid = check_lazy_mask_valid(parser, candidate.deferred_tokens);
        }
    }
    arena_pop_to(output, valid ? temporary : mark);
    arena_pop_to(scratch, scratch_mark);
    *result = valid ? candidate : (CheckLazy){ .status = candidate.status == CHECK_LAZY_OK ? CHECK_LAZY_INVALID : candidate.status };
    return valid;
}

#endif
