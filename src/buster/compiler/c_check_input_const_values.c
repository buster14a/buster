// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// A completed const initializer is evaluated against the bindings captured while its scope was active.
// The shared subobject cursor converts its writes; sparse receipts never allocate an object's full extent.

static uint32_t check_input_const_expressions(CheckInputReader* reader, uint32_t begin, uint32_t end, CheckInitExpression* expressions)
{
    const Parser* parser = reader->parser;
    uint32_t count = 0;
    for (uint32_t token = begin; token < end;)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const uint8_t role = parser->tree.roles[token];
        if (role == PARSE_ROLE_OPEN_INITIALIZER || role == PARSE_ROLE_CLOSE_INITIALIZER || kind == PARSE_KIND_COMMA || kind == PARSE_KIND_EQUAL ||
            (token == begin && role == PARSE_ROLE_OPEN_COMPOUND_BODY) || (token + 1 == end && role == PARSE_ROLE_CLOSE_COMPOUND_BODY))
        {
            token += 1;
        }
        else if (kind == PARSE_KIND_PERIOD)
        {
            token += token + 1 < end ? 2 : 1;
        }
        else if (kind == PARSE_KIND_L_SQUARE && reader->designator_valid[token] != 0)
        {
            const uint32_t close = parser->tree.parents[token];
            token = close > token && close < end ? close + 1 : end;
        }
        else
        {
            const uint32_t stop = check_init_expression_end(parser, token, end);
            if (stop == token)
            {
                token += 1;
            }
            else
            {
                if (expressions != NULL)
                {
                    expressions[count] = (CheckInitExpression){ .first = token, .end = stop, .node = count };
                }
                count += 1;
                token = stop;
            }
        }
    }
    return count;
}


static void check_input_initializer_const(CheckInputReader* reader, uint32_t declaration, uint32_t begin, uint32_t end, TypeId type)
{
    const uint64_t mark = reader->scratch->length;
    const uint32_t count = check_input_const_expressions(reader, begin, end, NULL);
    CheckInitExpression* expressions = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckInitExpression, count) : NULL;
    CheckValue* values = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckValue, count) : NULL;
    CheckDiagnostic* diagnostics = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckDiagnostic, count) : NULL;
    bool valid = count == 0 || (expressions != NULL && values != NULL && diagnostics != NULL);
    reader->output->resource |= !valid;
    if (valid)
    {
        check_input_const_expressions(reader, begin, end, expressions);
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        values[i] = (CheckValue){ 0 };
        diagnostics[i] = CHECK_DIAG_NONE;
        valid = check_input_evaluate_cached(reader, expressions[i].first, expressions[i].end, values + i);
    }
    uint32_t direct = valid && count == 1 ? expressions[0].first : end;
    uint32_t direct_end = valid && count == 1 ? expressions[0].end : end;
    while (direct < direct_end && reader->parser->tokens.kinds[direct] == PARSE_KIND_L_PAREN && reader->parser->tree.parents[direct] == direct_end - 1)
    {
        direct += 1;
        direct_end -= 1;
    }
    const uint32_t assignment = reader->output->compound_objects[declaration] ? declaration : begin - 1;
    const bool static_read = valid && count == 1 && reader->static_initializers[assignment] && direct + 1 == direct_end &&
                             reader->parser->tokens.kinds[direct] == PARSE_KIND_IDENTIFIER && values[0].category == CHECK_LVALUE &&
                             !check_init_aggregate(reader->types, values[0].type);
    CheckInitializer initialized = { .type = type, .failed_token = BUSTER_CHECK_NONE, .warning_token = BUSTER_CHECK_NONE };
    CheckStream stream = { .values = values, .diagnostics = diagnostics, .count = count };
    // Evaluate/query before this point. Until the walk ends, its output arena contains only contiguous part
    // appends; receipt publication and diagnostic growth happen afterward, while side tables use scratch.
    CheckInitWalker walker = {
        .types = reader->types,
        .parser = reader->parser,
        .expressions = expressions,
        .expression_count = count,
        .stream = &stream,
        .const_values = &reader->output->const_values,
        .source_position = end,
        .strings = reader->output->strings,
        .string_count = reader->output->string_count,
        .string_ids = reader->string_ids,
        .indices = reader->designator_indices,
        .index_valid = reader->designator_valid,
        .ignore_excess = true,
        .output = reader->arena,
        .scratch = reader->scratch,
        .result = &initialized,
    };
    valid = valid && check_init_walk(&walker, type, begin, end);
    reader->output->resource |= initialized.status == CHECK_INIT_RESOURCE;
    uint8_t* live = NULL;
    if (valid)
    {
        const CheckInitStatus status = check_initializer_live_writes(reader->types, &initialized, reader->scratch, &live);
        reader->output->resource |= status == CHECK_INIT_RESOURCE;
        valid = status == CHECK_INIT_OK;
    }
    const uint32_t zero = initialized.object_size != 0;
    const uint32_t part_count = initialized.count < UINT32_MAX ? initialized.count + zero : 0;
    valid = valid && initialized.object_size <= UINT64_MAX / 8 && (initialized.count == 0 || part_count != 0);
    CheckConstPart* parts = valid && part_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConstPart, part_count) : NULL;
    CheckConstUnion* unions = valid && walker.union_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckConstUnion, walker.union_count) : NULL;
    if (valid && ((part_count != 0 && parts == NULL) || (walker.union_count != 0 && unions == NULL)))
    {
        reader->output->resource = true;
        valid = false;
    }
    if (valid && zero != 0)
    {
        parts[0] = (CheckConstPart){ .kind = CHECK_CONST_CLEAR, .type = initialized.type, .bit_count = initialized.object_size * 8 };
    }
    uint32_t kept = zero;
    for (uint32_t i = 0; valid && i < initialized.count; i += 1)
    {
        if (!live[i])
        {
            continue;
        }
        const CheckInitPart* part = initialized.parts + i;
        uint64_t bits = 0;
        valid = check_initializer_part_bits(reader->types, part, &bits) == CHECK_INIT_OK;
        if (part->kind == CHECK_INIT_VALUE)
        {
            valid = valid && part->value.constant_kind != CHECK_CONSTANT_NONE && part->value.fault == CHECK_FAULT_NONE &&
                    (part->value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_INACTIVE_UNION_READ)) == 0 &&
                    (part->value.constant_kind != CHECK_CONSTANT_ADDRESS || part->value.object == 0 ||
                     (part->value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0);
        }
        if (valid && bits != 0)
        {
            parts[kept++] = (CheckConstPart){
                .kind = part->kind == CHECK_INIT_VALUE ? CHECK_CONST_VALUE : part->kind == CHECK_INIT_BYTES ? CHECK_CONST_BYTES : CHECK_CONST_CLEAR,
                .type = part->type,
                .bit_offset = part->bit_offset,
                .bit_count = bits,
                .value = part->value,
                .bytes = part->bytes,
                .byte_count = part->byte_count,
            };
        }
    }
    uint32_t union_count = 0;
    for (uint32_t i = 0; valid && i < walker.union_capacity; i += 1)
    {
        const CheckInitUnion* selected = walker.unions + i;
        if (selected->type != 0)
        {
            unions[union_count++] = (CheckConstUnion){ .type = selected->type, .bit_offset = selected->offset, .member = (uint32_t)selected->member + 1 };
        }
    }
    if (valid)
    {
        const CheckConstObject object = {
            .object = reader->objects[declaration],
            .declaration = declaration,
            .completed = end,
            .type = initialized.type,
            .byte_size = initialized.object_size,
        };
        valid = check_const_append(&reader->const_values, object, parts, kept, unions, union_count);
        reader->output->resource |= !valid;
        reader->output->const_values = check_const_view(&reader->const_values);
    }
    // A bare value read without an earlier initializer is already known to be nonconstant. Diagnose at its
    // own declaration, before a later enum can obscure that error; other unsupported proofs remain deferred.
    if (!valid && static_read && initialized.status == CHECK_INIT_OK && !reader->output->resource)
    {
        check_input_report(reader, direct, CHECK_INPUT_CONSTANT_INITIALIZER);
    }
    arena_pop_to(reader->scratch, mark);
}
