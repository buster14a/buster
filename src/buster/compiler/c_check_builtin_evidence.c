// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Builtin witnesses describe syntax which the producer has actually seen. A pointer's type or a value's default
// object-zero fields never prove its origin. Unknown paths remain unknown; the typed query action supplies the
// specified conservative result and never reads source-program memory. An atomic's address also takes, from here, the
// alignment of the declaration its lvalue path starts at (base_token), as clang gives a declaration's.

static bool check_input_complex_builtin(CBuiltinId builtin)
{
    return builtin >= CBUILTIN_CREAL && builtin <= CBUILTIN_CIMAGL;
}

static TypeId check_input_complex_format(CBuiltinId builtin)
{
    const uint32_t format = ((uint32_t)builtin - CBUILTIN_CREAL) % 3;
    return format == 1 ? BUSTER_TYPE_FLOAT : format == 2 ? BUSTER_TYPE_LONG_DOUBLE : BUSTER_TYPE_DOUBLE;
}

// The graph is appended in postorder, so every syntactic effect summary is computed once. Unlike ordinary
// constant folding, conditional and logical nodes retain effects from every source arm. Typed volatile reads
// are added by the checker after conversions; this producer witness describes explicit source effects only.
static bool check_input_builtin_syntax_effects(CheckInputReader* reader, uint32_t root)
{
    const CheckGraph* graph = &reader->output->graph;
    if (root < graph->count && reader->builtin_syntax_effects == 0)
    {
        reader->builtin_syntax_effects = arena_push_array(reader->scratch, graph->capacity, sizeof(uint8_t), 1);
        reader->output->resource |= reader->builtin_syntax_effects == 0;
    }
    while (!reader->output->resource && root < graph->count && reader->builtin_syntax_count <= root)
    {
        const uint32_t node = reader->builtin_syntax_count++;
        const CheckOp op = (CheckOp)graph->ops[node];
        const CBuiltinId builtin = (CBuiltinId)graph->members[node];
        bool effects = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_ASSIGN || op == CHECK_OP_BUILTIN_OVERFLOW || op == CHECK_OP_VARARGS ||
                       op == CHECK_OP_BUILTIN_ATOMIC || (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN);
        effects |= op == CHECK_OP_VALUE && (check_graph_leaf_of(graph, node)->flags & BUSTER_CHECK_SIDE_EFFECT) != 0;
        effects |= op == CHECK_OP_STATEMENT_VALUE && (graph->members[node] & BUSTER_CHECK_SIDE_EFFECT) != 0;
        effects |= op == CHECK_OP_BUILTIN_MISC && (builtin == CBUILTIN_UNREACHABLE || builtin == CBUILTIN_TRAP ||
                   builtin == CBUILTIN_PREFETCH || builtin == CBUILTIN_MEMCPY || builtin == CBUILTIN_MEMSET ||
                   builtin == CBUILTIN_MEMMOVE || builtin == CBUILTIN_ALLOCA);
        effects |= op == CHECK_OP_BUILTIN_VECTOR && (builtin == CBUILTIN_NONTEMPORAL_STORE || builtin == CBUILTIN_MASKED_STORE ||
                                                     builtin == CBUILTIN_MASKED_COMPRESS_STORE || builtin == CBUILTIN_MASKED_SCATTER);
        const bool unevaluated = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_BUILTIN_QUERY ||
                                 (op == CHECK_OP_BUILTIN_FLOAT && builtin >= CBUILTIN_NAN && builtin <= CBUILTIN_NANL) ||
                                 (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_NONDETERMINISTIC_VALUE);
        if (!unevaluated)
        {
            const uint32_t operands[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t operand = 0; operand < BUSTER_ARRAY_LENGTH(operands); operand += 1)
            {
                const uint32_t child = operands[operand];
                effects |= child < node && reader->builtin_syntax_effects[child] != 0;
            }
            if (check_graph_has_arguments(op))
            {
                uint32_t count = graph->argument_counts[node];
                if (op == CHECK_OP_BUILTIN_VECTOR && builtin == CBUILTIN_SHUFFLEVECTOR && count > 2)
                {
                    count = 2;
                }
                for (uint32_t argument = 0; argument < count; argument += 1)
                {
                    const uint32_t child = graph->argument_nodes[graph->argument_offsets[node] + argument];
                    effects |= child < node && reader->builtin_syntax_effects[child] != 0;
                }
            }
        }
        reader->builtin_syntax_effects[node] = effects;
    }
    return !reader->output->resource && root < reader->builtin_syntax_count && reader->builtin_syntax_effects[root] != 0;
}

// clang's alignment of an object's declaration, its log2 (ASTContext::getDeclAlign): an aligned attribute's alone, which
// may be less than the type's, or else the type's, at least 16 for an array of 16 bytes or more (x86-64's large arrays).
static uint32_t check_input_declaration_alignment(const CheckInputReader* reader, uint32_t declaration, TypeId type)
{
    const CheckInputAttributes* attributes = reader->output->attributes[declaration];
    const TypeId plain = type_strip_aligned(reader->types, type);
    uint32_t result = type_alignment_log2(reader->types, type);
    if (attributes != 0 && attributes->alignment_log2 != 0)
    {
        result = (uint32_t)attributes->alignment_log2 - 1;
    }
    else if (type_kind_is_array(type_kind(reader->types, plain)) && type_size(reader->types, type) >= 16 && result < 4)
    {
        result = 4;
    }
    return result;
}

// The object an identifier names, of a complete type that isn't variably modified, or BUSTER_CHECK_NONE: its declaration.
static uint32_t check_input_evidence_object(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    uint32_t result = BUSTER_CHECK_NONE;
    if (token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = reader->output->declarations[token];
        const TypeId type = reader->declared_types[token];
        const uint8_t kind = type != 0 ? type_kind(reader->types, type_strip_aligned(reader->types, type)) : 0;
        if (declaration < parser->tokens.count && type != 0 && kind != BUSTER_TYPE_KIND_FUNCTION && type_is_complete(reader->types, type) &&
            (type_inherited_flags(reader->types, type) & BUSTER_TYPE_FLAG_VARIABLY_MODIFIED) == 0)
        {
            result = declaration;
        }
    }
    return result;
}

static void check_input_builtin_evidence(CheckInputReader* reader, uint32_t first, uint32_t end, CheckBuiltinCall* evidence)
{
    const Parser* parser = reader->parser;
    // The leftmost identifier past prefix operators, casts and parentheses, whose declaration starts an lvalue path there.
    uint32_t base = first;
    bool leading = true;
    while (leading && base < end)
    {
        const uint8_t role = parser->tree.roles[base];
        const uint8_t kind = parser->tokens.kinds[base];
        const uint32_t close = parser->tree.parents[base];
        if (kind == PARSE_KIND_L_PAREN && role == PARSE_ROLE_OPEN_GROUP)
        {
            base += 1;
        }
        else if (role == PARSE_ROLE_OPEN_CAST && close > base && close + 1 < end)
        {
            base = close + 1;
        }
        else if ((kind == PARSE_KIND_AMP || kind == PARSE_KIND_STAR) && role == PARSE_ROLE_PREFIX)
        {
            base += 1;
        }
        else
        {
            leading = false;
        }
    }
    const uint32_t object = base < end ? check_input_evidence_object(reader, base) : BUSTER_CHECK_NONE;
    if (object != BUSTER_CHECK_NONE)
    {
        evidence->base_token = base;
        evidence->base_alignment = (uint8_t)(check_input_declaration_alignment(reader, object, reader->declared_types[base]) + 1);
    }
    bool address = false;
    while (first < end)
    {
        const uint8_t role = parser->tree.roles[first];
        const uint8_t kind = parser->tokens.kinds[first];
        const uint32_t close = parser->tree.parents[first];
        if (kind == PARSE_KIND_L_PAREN && close == end - 1 && role == PARSE_ROLE_OPEN_GROUP)
        {
            first += 1;
            end -= 1;
        }
        else if (role == PARSE_ROLE_OPEN_CAST && close > first && close + 1 < end)
        {
            first = close + 1;
        }
        else if (kind == PARSE_KIND_AMP && role == PARSE_ROLE_PREFIX && !address)
        {
            address = true;
            first += 1;
        }
        else
        {
            break;
        }
    }
    uint64_t size = UINT64_MAX;
    bool literal = false;
    if (first < end && (parser->tokens.kinds[first] == PARSE_KIND_STRING || check_input_function_name_token(parser, first)))
    {
        uint32_t after = first + (parser->tokens.kinds[first] != PARSE_KIND_STRING);
        while (after < end && parser->tokens.kinds[after] == PARSE_KIND_STRING)
        {
            after += 1;
        }
        const CheckInitString* string = after == end ? check_input_string(reader, first) : 0;
        if (string != 0)
        {
            literal = parser->tokens.kinds[first] == PARSE_KIND_STRING;
            const TypeId element = type_element(reader->types, string->type);
            evidence->string_literal_start = literal && !address;
            evidence->nan_payload_known = literal && !address && element == BUSTER_TYPE_CHAR && string->byte_count != 0 && string->byte_count - 1 <= UINT32_MAX;
            evidence->nan_payload = evidence->nan_payload_known ? string->bytes : 0;
            evidence->nan_payload_length = evidence->nan_payload_known ? (uint32_t)string->byte_count - 1 : 0;
            evidence->minimum_alignment = UINT64_C(1) << type_alignment_log2(reader->types, element);
            evidence->alignment_known = true;
            size = string->byte_count;
        }
    }
    else if (first + 1 == end && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER)
    {
        const uint32_t declaration = check_input_evidence_object(reader, first);
        const TypeId type = reader->declared_types[first];
        if (declaration != BUSTER_CHECK_NONE && (address || type_kind_is_array(type_kind(reader->types, type_strip_aligned(reader->types, type)))))
        {
            evidence->minimum_alignment = UINT64_C(1) << check_input_declaration_alignment(reader, declaration, type);
            evidence->alignment_known = true;
            size = type_size(reader->types, type);
        }
    }
    if (size != UINT64_MAX)
    {
        // These paths point at a complete object itself: whole-object and nearest-subobject bounds coincide.
        // Member and conditional paths need their own distinct four-mode proof and remain unknown here.
        for (uint32_t mode = 0; mode < 4; mode += 1)
        {
            evidence->object_size_known[mode] = true;
            evidence->object_size_source_constant[mode] = !literal || (mode & 1) != 0;
            evidence->object_sizes[mode] = size;
        }
    }
}
