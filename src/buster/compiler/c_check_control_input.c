// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Labels use function scope and a separate namespace. Build one bounded table keyed by body and identifier,
// then resolve forward gotos. Case ownership follows parser parents; the typed program pass converts case values
// to the promoted selector type and checks overlapping cases/defaults after expression checking.

static void check_input_region_prepare(CheckInputReader* reader, uint32_t opening)
{
    const Parser* parser = reader->parser;
    const uint32_t closing = parser->tree.parents[opening];
    const uint32_t block = parser->tree.parents[opening + 1];
    bool valid = reader->function_body < parser->tokens.count && closing < parser->tokens.count &&
                 block + 1 == closing && parser->tree.roles[block] == PARSE_ROLE_CLOSE_BLOCK;
    uint32_t final = BUSTER_CHECK_NONE;
    if (valid)
    {
        for (uint32_t child = reader->first_child[block]; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            if (parser->tree.roles[child] != PARSE_ROLE_OPEN_BLOCK)
            {
                final = child;
            }
        }
        bool expression = final < parser->tokens.count && parser->tree.roles[final] == PARSE_ROLE_TERMINATOR &&
                          reader->first_child[final] != BUSTER_CHECK_NONE;
        for (uint32_t child = expression ? reader->first_child[final] : BUSTER_CHECK_NONE; child != BUSTER_CHECK_NONE; child = reader->next_child[child])
        {
            const uint8_t role = parser->tree.roles[child];
            const uint8_t kind = parser->tokens.kinds[child];
            expression &= role != PARSE_ROLE_SPECIFIER && role != PARSE_ROLE_RETURN && kind != PARSE_KIND_GOTO &&
                          kind != PARSE_KIND_STRUCT && kind != PARSE_KIND_ENUM && kind != PARSE_KIND_STATIC_ASSERT &&
                          !check_input_attribute_starts(parser, child, final);
        }
        final = expression ? final : BUSTER_CHECK_NONE;
        const uint32_t index = reader->output->region_count++;
        reader->output->regions[index] = (CheckInputRegion)
        {
            .opening = opening,
            .closing = closing,
            .block = block,
            .body = reader->function_body,
            .result = BUSTER_CHECK_NONE,
            .final_statement = final,
        };
        reader->output->expression_regions[opening] = index + 1;
        reader->output->expression_regions[closing] = index + 1;
        reader->output->expression_regions[block] = index + 1;
    }
    else
    {
        check_input_report(reader, opening, CHECK_INPUT_DECLARATION);
    }
}

static bool check_input_region_transfer(const CheckInputReader* reader, uint32_t source, uint32_t target)
{
    const Parser* parser = reader->parser;
    bool valid = true;
    for (uint32_t ancestor = target; valid && ancestor < parser->tokens.count; ancestor = parser->tree.parents[ancestor])
    {
        const uint32_t selected = reader->output->expression_regions[ancestor];
        if (selected != 0)
        {
            const CheckInputRegion* region = &reader->output->regions[selected - 1];
            valid = source > region->opening && source < region->closing;
        }
    }
    return valid;
}

static void check_input_statement_attributes(CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t after = check_input_skip_attributes(parser, first, parser->tokens.count);
    if (after < parser->tokens.count && parser->tokens.kinds[after] == PARSE_KIND_SEMI)
    {
        uint32_t cursor = first;
        CheckInputAttributes* attributes = 0;
        bool valid = check_input_read_attributes(reader, &cursor, parser->tokens.count, &attributes) && cursor == after &&
                     attributes != 0 && reader->function_body < parser->tokens.count;
        for (uint32_t i = 0; valid && i < attributes->count; i += 1)
        {
            valid = attributes->records[i].kind == CHECK_ATTRIBUTE_FALLTHROUGH;
        }
        if (valid)
        {
            reader->output->attributes[after] = attributes->count != 0 ? attributes : 0;
        }
        else
        {
            check_input_report(reader, first, CHECK_INPUT_DECLARATION);
        }
    }
}

static void check_input_controls(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint64_t mark = reader->scratch->length;
    uint64_t capacity = 1;
    while (capacity < (uint64_t)count * 2 + 1)
    {
        capacity *= 2;
    }
    uint32_t* labels = arena_push_array(reader->scratch, capacity, sizeof(uint32_t), 4);
    reader->output->resource |= labels == 0;
    if (labels != 0)
    {
        memset(labels, 0, capacity * sizeof(uint32_t));
        uint32_t body = BUSTER_CHECK_NONE;
        uint32_t close = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            const uint8_t role = parser->tree.roles[token];
            const uint8_t kind = parser->tokens.kinds[token];
            if (token > close)
            {
                body = BUSTER_CHECK_NONE;
            }
            if (role == PARSE_ROLE_OPEN_BODY)
            {
                body = token;
                close = parser->tree.parents[token];
            }
            if (role == PARSE_ROLE_LABEL)
            {
                const uint32_t name_token = reader->first_child[token];
                if (body == BUSTER_CHECK_NONE || name_token >= count || parser->tokens.kinds[name_token] != PARSE_KIND_IDENTIFIER)
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
                else
                {
                    reader->output->declaration_bodies[token] = body;
                    const uint32_t name = parser->tokens.names[name_token];
                    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + body) & (capacity - 1);
                    while (labels[slot] != 0)
                    {
                        const uint32_t old = labels[slot] - 1;
                        const uint32_t old_name = reader->first_child[old];
                        if (reader->output->declaration_bodies[old] == body && parser->tokens.names[old_name] == name)
                        {
                            check_input_report(reader, token, CHECK_INPUT_REDECLARATION);
                            break;
                        }
                        slot = (slot + 1) & (capacity - 1);
                    }
                    labels[slot] = token + 1;
                }
            }
            else if (kind == PARSE_KIND_GOTO)
            {
                reader->output->declaration_bodies[token] = body;
            }
            else if (role == PARSE_ROLE_OPEN_CASE)
            {
                const uint32_t end = parser->tree.parents[token];
                uint32_t owner = end < count ? parser->tree.parents[end] : count;
                while (owner < count)
                {
                    const uint32_t keyword = reader->first_child[owner];
                    if (parser->tree.roles[owner] == PARSE_ROLE_CLOSE_CONDITION && keyword < count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH)
                    {
                        break;
                    }
                    owner = parser->tree.parents[owner];
                }
                if (owner >= count || end >= count || !check_input_region_transfer(reader, owner, end))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
                else
                {
                    reader->output->control_owners[token] = owner;
                    reader->output->control_owners[end] = owner;
                }
            }
        }
        for (uint32_t token = 0; token < count; token += 1)
        {
            if (parser->tokens.kinds[token] == PARSE_KIND_GOTO)
            {
                const uint32_t body = reader->output->declaration_bodies[token];
                const uint32_t operand = reader->first_child[token];
                bool found = false;
                if (body != BUSTER_CHECK_NONE && operand < count && parser->tokens.kinds[operand] == PARSE_KIND_IDENTIFIER)
                {
                    const uint32_t name = parser->tokens.names[operand];
                    uint64_t slot = ((uint64_t)name * UINT64_C(11400714819323198485) + body) & (capacity - 1);
                    while (!found && labels[slot] != 0)
                    {
                        const uint32_t target = labels[slot] - 1;
                        const uint32_t target_name = reader->first_child[target];
                        found = reader->output->declaration_bodies[target] == body && parser->tokens.names[target_name] == name;
                        if (found)
                        {
                            reader->output->label_targets[token] = target;
                            if (!check_input_region_transfer(reader, token, target))
                            {
                                check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                            }
                        }
                        slot = (slot + 1) & (capacity - 1);
                    }
                }
                const bool unfinished = body < count && parser->tree.roles[body] == PARSE_ROLE_OPEN_BODY &&
                                        parser->tree.parents[body] >= reader->fragment_end;
                if (!found && !unfinished)
                {
                    check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
                }
            }
            else if (parser->tokens.kinds[token] == PARSE_KIND_SEMI && reader->output->attributes[token] != 0)
            {
                bool enclosed = false;
                for (uint32_t ancestor = parser->tree.parents[token]; ancestor < count && !enclosed; ancestor = parser->tree.parents[ancestor])
                {
                    const uint32_t keyword = reader->first_child[ancestor];
                    enclosed = parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_CONDITION && keyword < count &&
                               parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
                }
                if (!enclosed)
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
}
