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

// The unit's tables of overloads, where `present`, a token named overloadable or __overloadable__, says it may have
// some: room for `capacity` candidates, its operands, and `calls` calls, its calls' and function declarators' closing
// parentheses. check_input_run counts them in its pass over the tokens (check_input_count_tokens).
static bool check_input_overload_initialize(CheckInputReader* reader, bool present, uint32_t capacity, uint32_t calls)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    if (present)
    {
        CheckInput* input = reader->output;
        reader->overload_capacity = capacity;
        reader->overload_candidates = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCandidate, (uint64_t)capacity + 1);
        input->overload_heads = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, (uint64_t)parser->tokens.count + 1);
        valid = check_overload_index_initialize(&reader->overload_index, reader->scratch, capacity, reader->pp->table->count);
        input->graph.overload_calls = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCall, (uint64_t)calls + 1);
        input->graph.overload_capacity = calls;
        input->graph.overload_candidates = reader->overload_candidates;
        valid = valid && reader->overload_candidates != 0 && input->overload_heads != 0 && input->graph.overload_calls != 0;
        if (valid)
        {
            memset(input->overload_heads, 0, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
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
        const uint32_t name = reader->parser->tokens.names[token];
        active = reader->overload_index.active[name] || (check_input_overload_flags(reader->output->attributes[token]) & BUSTER_CHECK_OVERLOADABLE) != 0;
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
        uint32_t definition_origin = reader->definition_origins[token];
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
        CheckOverloadIndex* index = &reader->overload_index;
        uint64_t signature_hash = 0;
        const bool hashed = check_overload_signature_hash(index, reader->types, candidate.type, reader->scratch, &signature_hash);
        const uint64_t lexical_key = check_overload_index_key(signature_hash, name, scope);
        const uint64_t linked_key = check_overload_index_key(signature_hash, name, UINT32_MAX);
        const uint32_t lexical_slot = hashed ? check_overload_index_find(index, lexical_key, name, scope, reader->parser->options.scalar) : UINT32_MAX;
        const uint32_t linked_slot = hashed ? check_overload_index_find(index, linked_key, name, UINT32_MAX, reader->parser->options.scalar) : UINT32_MAX;
        const uint32_t lexical_head = check_overload_index_head(index, lexical_slot);
        const uint32_t linked_head = check_overload_index_head(index, linked_slot);
        const uint32_t unmarked = head != BUSTER_CHECK_NONE ? index->unmarked[head] : BUSTER_CHECK_NONE;
        input->resource |= !hashed || lexical_slot == UINT32_MAX || linked_slot == UINT32_MAX;
        CheckOverloadResult declared = { .status = CHECK_OVERLOAD_OK };
        if (active && !input->resource)
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
                declared = check_overload_declare_chain(reader->types, reader->overload_candidates, input->graph.overload_candidate_count,
                                                       lexical_head, &candidate, &context, index->lexical_next, index->checked, unmarked);
                candidate.canonical = declared.canonical;
                candidate.object = declared.object;
                candidate.flags = declared.flags;
            }
            bool complete = false;
            for (uint32_t linked = linked_head; !complete && declared.status == CHECK_OVERLOAD_OK && linked != UINT32_MAX; linked = index->linked_next[linked])
            {
                const CheckOverloadCandidate* prior = reader->overload_candidates + linked;
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
                        if (definition_origin == 0 && (prior->flags & BUSTER_CHECK_OVERLOAD_DEFINED) != 0)
                        {
                            definition_origin = reader->definition_origins[prior->declaration];
                        }
                        const TypeAnswer same = index->checked[linked] ? type_same_canonical(reader->types, prior->type, candidate.type) : TYPE_ANSWER_NO;
                        declared.status = same == TYPE_ANSWER_FULL ? CHECK_OVERLOAD_FULL : CHECK_OVERLOAD_OK;
                        complete = same == TYPE_ANSWER_YES;
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
            const uint32_t current = input->graph.overload_candidate_count++;
            reader->overload_candidates[current] = candidate;
            input->overload_heads[token] = current + 1;
            reader->objects[token] = candidate.object;
            reader->definition_origins[token] = definition_origin;
            index->lexical_next[current] = active ? check_overload_index_previous(reader->overload_candidates, candidate.type, lexical_head, index->lexical_next) : lexical_head;
            index->linked_next[current] = active ? check_overload_index_previous(reader->overload_candidates, candidate.type, linked_head, index->linked_next) : linked_head;
            index->checked[current] = active;
            index->unmarked[current] = unmarked != BUSTER_CHECK_NONE ? unmarked :
                                       (candidate.flags & BUSTER_CHECK_OVERLOADABLE) == 0 ? candidate.canonical : BUSTER_CHECK_NONE;
            index->active[name] |= (candidate.flags & BUSTER_CHECK_OVERLOADABLE) != 0;
            check_overload_index_publish(index, lexical_slot, lexical_key, name, scope, current);
            // Both lookups can choose the same empty slot before either key is published.
            const uint32_t publish_linked = check_overload_index_find(index, linked_key, name, UINT32_MAX, reader->parser->options.scalar);
            check_overload_index_publish(index, publish_linked, linked_key, name, UINT32_MAX, current);
            if (active)
            {
                reader->linked_declarations[name] = token + 1;
                reader->linked_types[name] = candidate.type;
            }
        }
    }
}

#endif
