// Copyright 2026 Buster contributors.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Immutable lexical overload sets and their translation-unit linkage identities. Ordinary bindings still
// identify the most recent declaration; each source use retains its own set head before later declarations.
#ifndef BUSTER_C_CHECK_OVERLOAD_INPUT_INCLUDED
#define BUSTER_C_CHECK_OVERLOAD_INPUT_INCLUDED

static uint32_t check_input_overload_flags(const CheckInputAttributes* attributes)
{
    uint32_t flags = 0;
    for (uint32_t i = 0; attributes != 0 && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (!attribute->ignored)
        {
            flags |= attribute->kind == CHECK_ATTRIBUTE_OVERLOADABLE ? BUSTER_CHECK_OVERLOADABLE : 0;
            flags |= attribute->kind == CHECK_ATTRIBUTE_UNAVAILABLE ? BUSTER_CHECK_OVERLOAD_UNAVAILABLE : 0;
        }
    }
    return flags;
}

static bool check_input_overload_initialize(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t plain = intern_find(reader->pp->table, (const uint8_t*)"overloadable", 12);
    const uint32_t underscored = intern_find(reader->pp->table, (const uint8_t*)"__overloadable__", 16);
    bool present = false;
    uint32_t capacity = 0;
    uint32_t calls = 0;
    for (uint32_t token = 0; token < parser->tokens.count; token += 1)
    {
        const uint32_t name = parser->tokens.names[token];
        present |= name != 0 && (name == plain || name == underscored);
        capacity += parser->tree.roles[token] == PARSE_ROLE_OPERAND;
        calls += parser->tree.roles[token] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[token] == PARSE_ROLE_CLOSE_FUNCTION;
    }
    bool valid = true;
    if (present)
    {
        CheckInput* input = reader->output;
        reader->overload_capacity = capacity;
        reader->overload_candidates = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCandidate, (uint64_t)capacity + 1);
        input->overload_heads = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, (uint64_t)parser->tokens.count + 1);
        reader->overload_linked_heads = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, reader->pp->table->count);
        reader->overload_linked_next = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)capacity + 1);
        input->graph.overload_calls = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCall, (uint64_t)calls + 1);
        input->graph.overload_capacity = calls;
        input->graph.overload_candidates = reader->overload_candidates;
        valid = reader->overload_candidates != 0 && input->overload_heads != 0 && reader->overload_linked_heads != 0 &&
                reader->overload_linked_next != 0 && input->graph.overload_calls != 0;
        if (valid)
        {
            memset(input->overload_heads, 0, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
            memset(reader->overload_linked_heads, 0, (uint64_t)reader->pp->table->count * sizeof(uint32_t));
        }
    }
    reader->output->resource |= !valid;
    return valid;
}

static bool check_input_overload_active(const CheckInputReader* reader, uint32_t token)
{
    bool active = false;
    if (reader->overload_candidates != 0 && reader->declaration_kinds[token] == C_BINDING_FUNCTION)
    {
        active = (check_input_overload_flags(reader->output->attributes[token]) & BUSTER_CHECK_OVERLOADABLE) != 0;
        const uint32_t name = reader->parser->tokens.names[token];
        for (uint32_t linked = reader->overload_linked_heads[name]; !active && linked != 0; linked = reader->overload_linked_next[linked - 1])
        {
            active = (reader->overload_candidates[linked - 1].flags & BUSTER_CHECK_OVERLOADABLE) != 0;
        }
    }
    return active;
}

static void check_input_overload_declare(CheckInputReader* reader, uint32_t token, uint32_t point, uint32_t old, bool active)
{
    CheckInput* input = reader->output;
    if (reader->overload_candidates != 0 && reader->declaration_kinds[token] == C_BINDING_FUNCTION && input->report_count == 0 && !input->resource)
    {
        const uint32_t name = reader->parser->tokens.names[token];
        const uint32_t scope = reader->scope_count != 0 ? reader->scope_openings[reader->scope_count - 1] + 1 : 0;
        uint32_t head = old != 0 ? input->overload_heads[reader->bindings.declaration[old - 1]] : 0;
        head = head != 0 && reader->overload_candidates[head - 1].scope == scope ? head - 1 : BUSTER_CHECK_NONE;
        CheckOverloadCandidate candidate = {
            .type = reader->declared_types[token],
            .declaration = token,
            .canonical = token,
            .object = reader->objects[token],
            .previous = head,
            .scope = scope,
            .position = point,
            .flags = check_input_overload_flags(input->attributes[token]) | (reader->defined[token] ? BUSTER_CHECK_OVERLOAD_DEFINED : 0),
        };
        const CheckOverloadContext context = {
            .tag_completions = reader->tag_completions,
            .transparent_unions = reader->transparent_unions,
            .tag_completion_count = reader->types->tags.count,
            .source_position = point,
            .extensions = check_input_expression_extensions(reader->parser),
        };
        CheckOverloadResult declared = { .status = CHECK_OVERLOAD_OK };
        if (active)
        {
            const uint32_t linked = reader->linked_declarations[name];
            const uint32_t previous = old != 0 ? reader->bindings.declaration[old - 1] : BUSTER_CHECK_NONE;
            if ((old != 0 && reader->bindings.depth[old - 1] == reader->scope_count && reader->bindings.kind[old - 1] != C_BINDING_FUNCTION) ||
                (linked != 0 && reader->declaration_kinds[linked - 1] != C_BINDING_FUNCTION) ||
                (old != 0 && reader->linkage[previous] != 0 && reader->linkage[previous] != reader->linkage[token]))
            {
                declared.status = CHECK_OVERLOAD_CONFLICT;
            }
            else
            {
                declared = check_overload_declare(reader->types, reader->overload_candidates, input->graph.overload_candidate_count, head, &candidate, &context);
                candidate.canonical = declared.canonical;
                candidate.object = declared.object;
                candidate.flags = declared.flags;
            }
            for (uint32_t linked = reader->overload_linked_heads[name]; declared.status == CHECK_OVERLOAD_OK && linked != 0; linked = reader->overload_linked_next[linked - 1])
            {
                const CheckOverloadCandidate* prior = reader->overload_candidates + linked - 1;
                const TypeAnswer signature = check_overload_signature(reader->types, prior->type, candidate.type);
                if (signature == TYPE_ANSWER_FULL)
                {
                    declared.status = CHECK_OVERLOAD_FULL;
                }
                else if (signature == TYPE_ANSWER_YES)
                {
                    const TypeAnswer compatible = check_overload_compatible(reader->types, prior->type, candidate.type, &context);
                    if (compatible != TYPE_ANSWER_YES || reader->linkage[prior->declaration] != reader->linkage[token] ||
                        ((prior->flags ^ candidate.flags) & BUSTER_CHECK_OVERLOADABLE) != 0)
                    {
                        declared.status = compatible == TYPE_ANSWER_FULL ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_CONFLICT;
                    }
                    else if (reader->defined[token] && (prior->flags & BUSTER_CHECK_OVERLOAD_DEFINED) != 0)
                    {
                        declared.status = CHECK_OVERLOAD_REDEFINITION;
                    }
                    else
                    {
                        candidate.canonical = prior->canonical;
                        candidate.object = prior->object;
                        candidate.flags |= prior->flags;
                    }
                }
            }
        }
        else
        {
            // The ordinary declaration path already checked compatibility and chose a stable object identity.
            candidate.canonical = candidate.object != 0 ? candidate.object - 1 : token;
        }
        input->resource |= declared.status == CHECK_OVERLOAD_FULL || input->graph.overload_candidate_count == reader->overload_capacity;
        if (declared.status != CHECK_OVERLOAD_OK || input->resource)
        {
            check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
        }
        else
        {
            const uint32_t index = input->graph.overload_candidate_count++;
            reader->overload_candidates[index] = candidate;
            input->overload_heads[token] = index + 1;
            reader->objects[token] = candidate.object;
            reader->definition_seen[token] |= (candidate.flags & BUSTER_CHECK_OVERLOAD_DEFINED) != 0;
            reader->overload_linked_next[index] = reader->overload_linked_heads[name];
            reader->overload_linked_heads[name] = index + 1;
            if (active)
            {
                reader->linked_declarations[name] = token + 1;
                reader->linked_types[name] = candidate.type;
            }
        }
    }
}

#endif
