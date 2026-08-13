#line 16465 "src/buster/lib/compiler/frontend/c/c.c"
BUSTER_GLOBAL_LOCAL String8 c_ir_unsupported_gnu_construct(CPreprocessResult preprocess, u32 start, u32 end, u32* token_index_out);

BUSTER_GLOBAL_LOCAL CAnalysisResult c_analyze_semantics(Arena* arena, CPreprocessResult preprocess, CParserResult syntax)
{
    CParseResult result = {
        .arena = arena,
        .symbols = preprocess.symbols,
    };
    if (syntax.diagnostic_count)
    {
        result.diagnostics = syntax.diagnostics;
        result.diagnostic_count = syntax.diagnostic_count;
        return result;
    }
    if (!arena || !preprocess.tokens || !preprocess.token_count)
    {
        return result;
    }
    if (preprocess.token_count > (UINT32_MAX - 1) / 2)
    {
        return result;
    }
    u32 token_count = (u32)preprocess.token_count;
    u32 identifier_count = 0;
    u32 semicolon_count = 0;
    u32 comma_count = 0;
    u32 open_parenthesis_count = 0;
    u32 open_bracket_count = 0;
    u32 open_brace_count = 0;
    u32 for_count = 0;
    u32 type_delimiter_depth = 0;
    u32 maximum_delimiter_depth = 0;
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        CToken token = preprocess.tokens[token_index];
        if (token.kind == C_TOKEN_IDENTIFIER)
        {
            identifier_count += 1;
            for_count += string_equal(c_token_spelling(preprocess.spelling_base, token), S8("for"));
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON))
        {
            semicolon_count += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
        {
            comma_count += 1;
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
        {
            open_parenthesis_count += 1;
            type_delimiter_depth += 1;
            maximum_delimiter_depth = BUSTER_MAX(maximum_delimiter_depth, type_delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET))
        {
            open_bracket_count += 1;
            type_delimiter_depth += 1;
            maximum_delimiter_depth = BUSTER_MAX(maximum_delimiter_depth, type_delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
        {
            open_brace_count += 1;
            type_delimiter_depth += 1;
            maximum_delimiter_depth = BUSTER_MAX(maximum_delimiter_depth, type_delimiter_depth);
        }
        else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                 c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
        {
            type_delimiter_depth -= type_delimiter_depth != 0;
        }
    }
    if (maximum_delimiter_depth > (UINT32_MAX - 64) / 8 || token_count == UINT32_MAX)
    {
        return result;
    }
    u32 type_frame_capacity = maximum_delimiter_depth * 4 + 32;
    u32 type_mutation_capacity = maximum_delimiter_depth * 8 + 64;
    u32 expression_task_capacity = token_count + 1;
    u64 promoted_member_capacity_u64 = (u64)token_count * 2 + 1;
    if (promoted_member_capacity_u64 > UINT32_MAX)
    {
        return result;
    }
    u32 promoted_member_capacity = (u32)promoted_member_capacity_u64;
    u32 incomplete_array_chain_capacity = (u32)promoted_member_capacity_u64;
    u64 machine_buffer_size = arena_minimum_position;
    if (!c_type_parse_buffer_size_add(&machine_buffer_size, type_frame_capacity, sizeof(CTypeParseFrame), BUSTER_ALIGN_OF(CTypeParseFrame)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, type_mutation_capacity, sizeof(CTypeMutation), BUSTER_ALIGN_OF(CTypeMutation)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, expression_task_capacity, sizeof(CParseExpressionTypeTask),
                                      BUSTER_ALIGN_OF(CParseExpressionTypeTask)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, promoted_member_capacity, sizeof(CParsePromotedMemberWork),
                                      BUSTER_ALIGN_OF(CParsePromotedMemberWork)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, promoted_member_capacity, sizeof(u32), BUSTER_ALIGN_OF(u32)) ||
        !c_type_parse_buffer_size_add(&machine_buffer_size, incomplete_array_chain_capacity, sizeof(CTypeId), BUSTER_ALIGN_OF(CTypeId)) ||
        machine_buffer_size > UINT64_MAX - (BUSTER_KB(64) - 1))
    {
        return result;
    }
    machine_buffer_size = (machine_buffer_size + BUSTER_KB(64) - 1) & ~(BUSTER_KB(64) - 1);
    Arena* machine_buffer_arena = arena_create((ArenaCreation){
        .reserved_size = machine_buffer_size,
        .granularity = BUSTER_KB(64),
        .initial_size = BUSTER_MIN(machine_buffer_size, BUSTER_KB(256)),
    });
    if (!machine_buffer_arena)
    {
        return result;
    }
    Arena* machine_conflicts[] = {
        arena,
    };
    TemporalArena machine_temporary = scratch_begin(machine_conflicts, BUSTER_ARRAY_LENGTH(machine_conflicts));
    CTypeParseMachine machine = {
        .frames = arena_allocate(machine_buffer_arena, CTypeParseFrame, type_frame_capacity),
        .mutations = arena_allocate(machine_buffer_arena, CTypeMutation, type_mutation_capacity),
        .expression_tasks = arena_allocate(machine_buffer_arena, CParseExpressionTypeTask, expression_task_capacity),
        .incomplete_array_chain = arena_allocate(machine_buffer_arena, CTypeId, incomplete_array_chain_capacity),
        .incomplete_array_chain_capacity = incomplete_array_chain_capacity,
        .scratch_arena = machine_temporary.arena,
        .layout_cache = {.tokens = preprocess.tokens},
        .promoted_member_work = arena_allocate(machine_buffer_arena, CParsePromotedMemberWork, promoted_member_capacity),
        .promoted_member_visited = arena_allocate(machine_buffer_arena, u32, promoted_member_capacity),
        .promoted_member_capacity = promoted_member_capacity,
        .frame_capacity = type_frame_capacity,
        .mutation_capacity = type_mutation_capacity,
        .expression_task_capacity = expression_task_capacity,
    };
    result.declaration_capacity = semicolon_count + open_brace_count + 1;
    result.type_capacity = token_count * 2 + 1;
    result.parameter_capacity = comma_count + open_parenthesis_count + 1;
    result.member_capacity = identifier_count + semicolon_count + 1;
    result.enum_member_capacity = identifier_count + 1;
    result.array_bound_capacity = open_bracket_count + 1;
    result.alignment_capacity = token_count + 1;
    result.entity_capacity = identifier_count + 1;
    result.scope_capacity = open_brace_count + open_parenthesis_count + for_count + 1;
    result.identifier_use_capacity = identifier_count + 1;
    result.identifier_use_by_token_capacity = token_count + 1;
    result.deferred_static_assert_capacity = token_count + 1;
    result.diagnostic_capacity = token_count + 1;
    result.declarations = arena_allocate(arena, CDeclaration, result.declaration_capacity);
    result.types = arena_allocate(arena, CType, result.type_capacity);
    result.parameters = arena_allocate(arena, CParameter, result.parameter_capacity);
    result.members = arena_allocate(arena, CMember, result.member_capacity);
    result.enum_members = arena_allocate(arena, CEnumMember, result.enum_member_capacity);
    result.array_bounds = arena_allocate(arena, CArrayBound, result.array_bound_capacity);
    result.alignments = arena_allocate(arena, CAlignmentSpecifier, result.alignment_capacity);
    result.entities = arena_allocate(arena, CEntity, result.entity_capacity);
    result.scopes = arena_allocate(arena, CScope, result.scope_capacity);
    result.deferred_static_asserts = arena_allocate(arena, CDeferredStaticAssert, result.deferred_static_assert_capacity);
    u64 desired_lookup_bucket_count = (u64)result.entity_capacity * 2;
    result.entity_lookup_bucket_count = 1;
    while ((u64)result.entity_lookup_bucket_count < desired_lookup_bucket_count && result.entity_lookup_bucket_count <= UINT32_MAX / 2)
    {
        result.entity_lookup_bucket_count *= 2;
    }
    result.entity_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.entity_lookup_buckets, 0xff, sizeof(*result.entity_lookup_buckets) * result.entity_lookup_bucket_count);
    result.typedef_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.typedef_lookup_buckets, 0xff, sizeof(*result.typedef_lookup_buckets) * result.entity_lookup_bucket_count);
    result.name_lookup_buckets = arena_allocate(arena, CEntityId, result.entity_lookup_bucket_count);
    memset(result.name_lookup_buckets, 0xff, sizeof(*result.name_lookup_buckets) * result.entity_lookup_bucket_count);
    {
        u32 aggregate_slot_count = 16384;
        result.aggregate_lookup = arena_allocate(arena, CAggregateLookup, 1);
        *result.aggregate_lookup = (CAggregateLookup){
            .slots = arena_allocate(arena, CAggregateLookupSlot, aggregate_slot_count),
            .slot_count = aggregate_slot_count,
        };
        memset(result.aggregate_lookup->slots, 0, sizeof(*result.aggregate_lookup->slots) * aggregate_slot_count);
    }
    result.position_index = arena_allocate(arena, CTokenPositionIndex, 1);
    *result.position_index = (CTokenPositionIndex){0};
    result.identifier_uses = arena_allocate(arena, CIdentifierUse, result.identifier_use_capacity);
    result.identifier_use_by_token = arena_allocate(arena, u32, result.identifier_use_by_token_capacity);
    memset(result.identifier_use_by_token, 0xff, sizeof(*result.identifier_use_by_token) * result.identifier_use_by_token_capacity);
    result.token_classes = arena_allocate(arena, u8, result.identifier_use_by_token_capacity);
    memset(result.token_classes, 0, sizeof(*result.token_classes) * result.identifier_use_by_token_capacity);
    result.diagnostics = arena_allocate(arena, CDiagnostic, result.diagnostic_capacity);
    BUSTER_CHECK(result.scope_count < result.scope_capacity);
    result.scopes[result.scope_count++] = (CScope){
        .parent = C_SCOPE_ID_INVALID,
        .first_entity = C_ENTITY_ID_INVALID,
        .last_entity = C_ENTITY_ID_INVALID,
        .token_start = 0,
        .token_end = UINT32_MAX,
    };
    for (CParserDeclaration* syntax_declaration = syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        u32 start = syntax_declaration->token_start;
        u32 index = start + syntax_declaration->token_count;
        u32 body_start = syntax_declaration->body_start;
        u32 body_count = syntax_declaration->body_token_count;
        bool is_typedef = syntax_declaration->is_typedef;
        bool is_constexpr = syntax_declaration->is_constexpr;
        bool variadic = syntax_declaration->is_variadic;
        String8 function_name = syntax_declaration->function_name_token < preprocess.token_count
                                    ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[syntax_declaration->function_name_token])
                                    : (String8){0};
        CSourceLocation function_location = syntax_declaration->function_name_token < preprocess.token_count
                                                ? c_preprocess_token_location(&preprocess, preprocess.tokens[syntax_declaration->function_name_token])
                                                : (CSourceLocation){0};
        String8 object_name = syntax_declaration->name_token < preprocess.token_count ? c_token_spelling(preprocess.spelling_base, preprocess.tokens[syntax_declaration->name_token]) : (String8){0};
        CSourceLocation object_location = syntax_declaration->name_token < preprocess.token_count
                                              ? c_preprocess_token_location(&preprocess, preprocess.tokens[syntax_declaration->name_token])
                                              : (CSourceLocation){0};
        bool static_assertion = syntax_declaration->kind == C_PARSER_DECLARATION_STATIC_ASSERT;
        bool global_assembly = syntax_declaration->kind == C_PARSER_DECLARATION_ASSEMBLY;
        bool type_only = static_assertion || syntax_declaration->kind == C_PARSER_DECLARATION_TYPE;
        CDeclarationKind kind = global_assembly        ? C_DECLARATION_ASSEMBLY
                                : type_only            ? C_DECLARATION_TYPE
                                : is_typedef           ? C_DECLARATION_TYPEDEF
                                : function_name.length ? C_DECLARATION_FUNCTION
                                                       : C_DECLARATION_OBJECT;
        String8 name = kind == C_DECLARATION_FUNCTION ? function_name : object_name;
        CSourceLocation location = kind == C_DECLARATION_ASSEMBLY   ? c_preprocess_token_location(&preprocess, preprocess.tokens[start])
                                   : kind == C_DECLARATION_FUNCTION ? function_location
                                                                    : object_location;
        BUSTER_CHECK(result.declaration_count < result.declaration_capacity);
        CDeclaration* declaration = &result.declarations[result.declaration_count++];
        *declaration = (CDeclaration){
            .name = name,
            .location = location,
            .token_start = start,
            .token_count = index - start,
            .body_start = body_start,
            .body_token_count = body_count,
            .type = C_TYPE_ID_INVALID,
            .entity = C_ENTITY_ID_INVALID,
            .scope = C_SCOPE_ID_INVALID,
            .syntax_declaration = syntax_declaration,
            .syntax_body = syntax_declaration->first_statement,
            .kind = kind,
            .is_definition = syntax_declaration->is_definition,
            .is_variadic = variadic,
            .is_constexpr = is_constexpr,
        };
        if (!static_assertion && !global_assembly)
        {
            c_parse_declaration_type(&machine, &result, preprocess, declaration);
            if (declaration->kind == C_DECLARATION_OBJECT && declaration->type.value < result.type_count)
            {
                CTypeId object_type = C_TYPE_ID_INVALID;
                if (!c_parse_clone_incomplete_array_declarator(&machine, &result, declaration->type, &object_type))
                {
                    c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                                       S8("compiler resource limit while materializing incomplete array declaration"));
                }
                else
                {
                    declaration->type = object_type;
                }
            }
            if (declaration->is_constexpr && declaration->kind == C_DECLARATION_OBJECT && declaration->type.value < result.type_count)
            {
                declaration->type = c_parse_add_qualified_type(&result, declaration->type,
                                                               (CType){
                                                                   .is_const = true,
                                                               });
            }
            c_parse_validate_constexpr_declaration(&machine, arena, &result, preprocess, declaration);
            if (kind == C_DECLARATION_TYPEDEF && (string_equal(declaration->name, S8("va_list")) || string_equal(declaration->name, S8("__gnuc_va_list")) ||
                                                  string_equal(declaration->name, S8("__builtin_va_list"))))
            {
                declaration->type = c_parse_add_type(&result, (CType){
                                                                  .element_type = C_TYPE_ID_INVALID,
                                                                  .return_type = C_TYPE_ID_INVALID,
                                                                  .array_bound = C_ARRAY_BOUND_INVALID,
                                                                  .kind = C_TYPE_VA_LIST,
                                                                  .is_complete = true,
                                                              });
            }
        }
        if (!static_assertion && !declaration->name.length && c_preprocess_dialect_is_c23(preprocess.dialect))
        {
            for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
            {
                String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]);
                if (!string_equal(spelling, S8("true")) && !string_equal(spelling, S8("false")) && !string_equal(spelling, S8("nullptr")) &&
                    !string_equal(spelling, S8("constexpr")) && !string_equal(spelling, S8("typeof_unqual")))
                {
                    continue;
                }
                c_parse_diagnostic(&result, c_preprocess_token_location(&preprocess, preprocess.tokens[token_index]), C_DIAGNOSTIC_EXPECTED_DECLARATION,
                                   string_format(arena, S8("C23 keyword '{S8}' cannot be used as an identifier"), spelling));
                break;
            }
        }
        if (!declaration->name.length || declaration->type.value == C_ID_UNDERLYING_INVALID || kind == C_DECLARATION_TYPE || kind == C_DECLARATION_ASSEMBLY)
        {
            continue;
        }
        CEntity* existing = 0;
        u32 existing_index = C_ID_UNDERLYING_INVALID;
        CEntity* conflicting = 0;
        bool overloadable = false;
        for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
        {
            overloadable |=
                preprocess.tokens[token_index].kind == C_TOKEN_IDENTIFIER && string_equal(c_token_spelling(preprocess.spelling_base, preprocess.tokens[token_index]), S8("__overloadable__"));
        }
        CEntityKind entity_kind = kind == C_DECLARATION_FUNCTION ? C_ENTITY_FUNCTION : kind == C_DECLARATION_TYPEDEF ? C_ENTITY_TYPEDEF : C_ENTITY_OBJECT;
        // The name chain lists same-named entities newest first; the
        // redeclaration logic below needs ascending entity order, so gather
        // the file-scope candidates and walk them in reverse.
        u32 candidate_ids[64];
        u32 candidate_count = 0;
        bool candidates_overflowed = false;
        u32 name_bucket = (u32)c_parse_name_hash(c_parse_name_symbol(&result, declaration->name), declaration->name) & (result.entity_lookup_bucket_count - 1);
        for (CEntityId chain = result.name_lookup_buckets[name_bucket]; chain.value != C_ID_UNDERLYING_INVALID;
             chain = result.entities[chain.value].next_by_name)
        {
            CEntity* candidate = &result.entities[chain.value];
            if (candidate->scope.value != 0 || !string_equal(candidate->name, declaration->name))
            {
                continue;
            }
            if (candidate_count == 64)
            {
                candidates_overflowed = true;
                break;
            }
            candidate_ids[candidate_count++] = chain.value;
        }
        if (candidates_overflowed)
        {
            for (u32 entity_index = 0; entity_index < result.entity_count; entity_index += 1)
            {
                CEntity* candidate = &result.entities[entity_index];
                if (candidate->scope.value != 0 || !string_equal(candidate->name, declaration->name))
                {
                    continue;
                }
                if (candidate->kind == entity_kind && c_parse_types_compatible(arena, &result, preprocess, candidate->type, declaration->type))
                {
                    existing = candidate;
                    existing_index = entity_index;
                    break;
                }
                if (!conflicting)
                {
                    conflicting = candidate;
                }
            }
        }
        else
        {
            for (u32 position = candidate_count; position-- > 0;)
            {
                u32 entity_index = candidate_ids[position];
                CEntity* candidate = &result.entities[entity_index];
                if (candidate->kind == entity_kind && c_parse_types_compatible(arena, &result, preprocess, candidate->type, declaration->type))
                {
                    existing = candidate;
                    existing_index = entity_index;
                    break;
                }
                if (!conflicting)
                {
                    conflicting = candidate;
                }
            }
        }
        if (existing)
        {
            declaration->entity = (CEntityId){
                .value = existing_index,
            };
            if (existing->is_definition && declaration->is_definition)
            {
                c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_REDEFINITION, S8("redefinition"));
            }
            else
            {
                existing->is_definition |= declaration->is_definition;
            }
            continue;
        }
        if (conflicting && !(kind == C_DECLARATION_FUNCTION && conflicting->kind == C_ENTITY_FUNCTION && overloadable))
        {
            declaration->entity = (CEntityId){
                .value = (u32)(conflicting - result.entities),
            };
            c_parse_diagnostic(&result, declaration->location, C_DIAGNOSTIC_CONFLICTING_DECLARATION,
                               string_format(arena, S8("conflicting declaration of '{S8}' (previous type {u32}, new type {u32})"), declaration->name,
                                             conflicting->type.value, declaration->type.value));
            continue;
        }
        CEntityId entity = {
            .value = result.entity_count,
        };
        u32 declaration_name_token = kind == C_DECLARATION_FUNCTION ? syntax_declaration->function_name_token : syntax_declaration->name_token;
        bool is_thread_local = false;
        if (kind == C_DECLARATION_OBJECT)
        {
            for (u32 token_index = declaration->token_start; token_index < declaration->token_start + declaration->token_count; token_index += 1)
            {
                CToken token = preprocess.tokens[token_index];
                is_thread_local |= token.kind == C_TOKEN_IDENTIFIER &&
                                   (string_equal(c_token_spelling(preprocess.spelling_base, token), S8("_Thread_local")) || string_equal(c_token_spelling(preprocess.spelling_base, token), S8("__thread")) ||
                                    string_equal(c_token_spelling(preprocess.spelling_base, token), S8("thread_local")));
            }
        }
        declaration->entity = entity;
        BUSTER_CHECK(result.entity_count < result.entity_capacity);
        result.entities[result.entity_count++] = (CEntity){
            .name = declaration->name,
            .location = declaration->location,
            .type = declaration->type,
            .scope =
                {
                    .value = 0,
                },
            .next_in_scope = C_ENTITY_ID_INVALID,
            .declaration_index = result.declaration_count - 1,
            .declaration_token_plus_one = declaration_name_token < token_count ? declaration_name_token + 1 : 0,
            .alignment_start = declaration->alignment_start,
            .alignment_count = declaration->alignment_count,
            .kind = kind == C_DECLARATION_FUNCTION  ? C_ENTITY_FUNCTION
                    : kind == C_DECLARATION_TYPEDEF ? C_ENTITY_TYPEDEF
                                                    : C_ENTITY_OBJECT,
            .is_definition = declaration->is_definition,
            .is_thread_local = is_thread_local,
            .is_constexpr = declaration->is_constexpr,
        };
        c_parse_scope_add_entity(&result,
                                 (CScopeId){
                                     .value = 0,
                                 },
                                 entity);
        if (kind == C_DECLARATION_TYPEDEF)
        {
            u32 declaration_end = declaration->token_start + declaration->token_count;
            u32 segment_start = declaration_end;
            u32 delimiter_depth = 0;
            for (u32 token_index = declaration->token_start; token_index < declaration_end; token_index += 1)
            {
                CToken token = preprocess.tokens[token_index];
                if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                    c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
                {
                    delimiter_depth += 1;
                }
                else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                         c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
                {
                    delimiter_depth -= delimiter_depth != 0;
                }
                else if (!delimiter_depth && c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA))
                {
                    segment_start = token_index + 1;
                    break;
                }
            }
            CTypeId alias_base = declaration->base_type;
            while (segment_start < declaration_end)
            {
                u32 segment_end = segment_start;
                delimiter_depth = 0;
                while (segment_end < declaration_end)
                {
                    CToken token = preprocess.tokens[segment_end];
                    if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                        c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
                    {
                        delimiter_depth += 1;
                    }
                    else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                             c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
                    {
                        delimiter_depth -= delimiter_depth != 0;
                    }
                    else if (!delimiter_depth && (c_token_is_punctuator(&token, C_PUNCTUATOR_COMMA) || c_token_is_punctuator(&token, C_PUNCTUATOR_SEMICOLON)))
                    {
                        break;
                    }
                    segment_end += 1;
                }
                u32 name_index = segment_start;
                while (name_index < segment_end && preprocess.tokens[name_index].kind != C_TOKEN_IDENTIFIER)
                {
                    name_index += 1;
                }
                u32 declarator_index = segment_start;
                CTypeId alias_type = c_parse_pointer_chain(&result, preprocess, alias_base, &declarator_index, name_index);
                if (name_index < segment_end && declarator_index == name_index)
                {
                    u32 suffix_index = c_parse_skip_attributes(preprocess, name_index + 1, segment_end);
                    alias_type = c_parse_array_suffixes(&result, preprocess, alias_type, &suffix_index, segment_end);
                    suffix_index = c_parse_skip_attributes(preprocess, suffix_index, segment_end);
                    String8 alias_name = c_token_spelling(preprocess.spelling_base, preprocess.tokens[name_index]);
                    if (suffix_index == segment_end && alias_type.value != C_ID_UNDERLYING_INVALID &&
                        c_parse_lookup_entity(&result, (CScopeId){.value = 0}, alias_name).value == C_ID_UNDERLYING_INVALID)
                    {
                        CEntityId alias_entity = {.value = result.entity_count};
                        BUSTER_CHECK(result.entity_count < result.entity_capacity);
                        result.entities[result.entity_count++] = (CEntity){
                            .name = alias_name,
                            .location = c_preprocess_token_location(&preprocess, preprocess.tokens[name_index]),
                            .type = alias_type,
                            .scope = {.value = 0},
                            .next_in_scope = C_ENTITY_ID_INVALID,
                            .declaration_index = result.declaration_count - 1,
                            .kind = C_ENTITY_TYPEDEF,
                        };
                        c_parse_scope_add_entity(&result, (CScopeId){.value = 0}, alias_entity);
                    }
                }
                segment_start = segment_end + 1;
            }
        }
    }
    if (result.enum_member_count)
    {
        CTypeId enum_integer_type = c_parse_add_type(&result, (CType){
                                                                  .element_type = C_TYPE_ID_INVALID,
                                                                  .return_type = C_TYPE_ID_INVALID,
                                                                  .array_bound = C_ARRAY_BOUND_INVALID,
                                                                  .kind = C_TYPE_INT,
                                                                  .is_complete = true,
                                                              });
        for (u32 member_index = 0; member_index < result.enum_member_count; member_index += 1)
        {
            CEnumMember* member = &result.enum_members[member_index];
            if (c_parse_lookup_entity(&result,
                                      (CScopeId){
                                          .value = 0,
                                      },
                                      member->name)
                    .value != C_ID_UNDERLYING_INVALID)
            {
                c_parse_diagnostic(&result, member->location, C_DIAGNOSTIC_REDEFINITION, S8("redefinition of enumerator"));
                continue;
            }
            CEntityId entity = {
                .value = result.entity_count,
            };
            BUSTER_CHECK(result.entity_count < result.entity_capacity);
            result.entities[result.entity_count++] = (CEntity){
                .name = member->name,
                .location = member->location,
                .type = enum_integer_type,
                .scope =
                    {
                        .value = 0,
                    },
                .next_in_scope = C_ENTITY_ID_INVALID,
                .declaration_index = C_ID_UNDERLYING_INVALID,
                .kind = C_ENTITY_ENUMERATOR,
                .is_definition = true,
                .constant_is_negative = member->is_negative,
                .constant_value = member->value,
            };
            c_parse_scope_add_entity(&result,
                                     (CScopeId){
                                         .value = 0,
                                     },
                                     entity);
        }
    }
    for (u32 declaration_index = 0; declaration_index < result.declaration_count; declaration_index += 1)
    {
        CDeclaration* declaration = &result.declarations[declaration_index];
        if (!declaration->is_constexpr || declaration->kind != C_DECLARATION_OBJECT || declaration->entity.value >= result.entity_count)
        {
            continue;
        }
        u32 end = declaration->token_start + declaration->token_count;
        u32 initializer_start = end;
        u32 depth = 0;
        for (u32 token_index = declaration->token_start; token_index < end; token_index += 1)
        {
            CToken token = preprocess.tokens[token_index];
            if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACKET) ||
                c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_BRACE))
            {
                depth += 1;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS) || c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACKET) ||
                     c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_BRACE))
            {
                if (depth)
                {
                    depth -= 1;
                }
            }
            else if (!depth && c_token_is_punctuator(&token, C_PUNCTUATOR_ASSIGN))
            {
                initializer_start = token_index + 1;
                break;
            }
        }
        u32 initializer_end = end;
        if (initializer_end > initializer_start && c_token_is_punctuator(&preprocess.tokens[initializer_end - 1], C_PUNCTUATOR_SEMICOLON))
        {
            initializer_end -= 1;
        }
        if (initializer_start < initializer_end)
        {
            c_parse_validate_constexpr_initializer(&machine, arena, &result, preprocess,
                                                   (CScopeId){
                                                       .value = 0,
                                                   },
                                                   declaration->entity, initializer_start, initializer_end);
        }
    }
    c_parse_infer_file_array_bounds(&machine, arena, preprocess, &result);
    for (CParserDeclaration* syntax_declaration = syntax.first_declaration; syntax_declaration; syntax_declaration = syntax_declaration->next)
    {
        if (syntax_declaration->kind != C_PARSER_DECLARATION_STATIC_ASSERT)
        {
            continue;
        }
        c_parse_static_assert_check(&machine, arena, preprocess, &result,
                                    (CDeclaration){
                                        .token_start = syntax_declaration->token_start,
                                        .token_count = syntax_declaration->token_count,
                                    },
                                    (CScopeId){
                                        .value = 0,
                                    });
    }
    for (u32 declaration_index = 0; declaration_index < result.declaration_count; declaration_index += 1)
    {
        CDeclaration* declaration = &result.declarations[declaration_index];
        if (declaration->kind != C_DECLARATION_FUNCTION)
        {
            continue;
        }
        CScopeId scope = {
            .value = result.scope_count,
        };
        declaration->scope = scope;
        BUSTER_CHECK(result.scope_count < result.scope_capacity);
        CScope* function_scope = &result.scopes[result.scope_count++];
        *function_scope = (CScope){
            .parent =
                {
                    .value = 0,
                },
            .first_entity = C_ENTITY_ID_INVALID,
            .last_entity = C_ENTITY_ID_INVALID,
            .token_start = declaration->body_start,
            .token_end = declaration->body_start + declaration->body_token_count,
        };
        for (u32 parameter_index = 0; parameter_index < declaration->parameter_count; parameter_index += 1)
        {
            CParameter* parameter = &result.parameters[declaration->parameter_start + parameter_index];
            if (!parameter->name.length)
            {
                continue;
            }
            CEntityId entity = {
                .value = result.entity_count,
            };
            parameter->entity = entity;
            BUSTER_CHECK(result.entity_count < result.entity_capacity);
            result.entities[result.entity_count++] = (CEntity){
                .name = parameter->name,
                .location = parameter->location,
                .type = parameter->type,
                .scope = scope,
                .next_in_scope = C_ENTITY_ID_INVALID,
                .declaration_index = declaration_index,
                .kind = C_ENTITY_PARAMETER,
                .is_definition = true,
            };
            c_parse_scope_add_entity(&result, scope, entity);
        }
        u32 unsupported_token_index = UINT32_MAX;
        String8 unsupported_construct = c_ir_unsupported_gnu_construct(
            preprocess, declaration->body_start, declaration->body_start + declaration->body_token_count, &unsupported_token_index);
        if (unsupported_construct.length)
        {
            c_parse_diagnostic(&result, unsupported_token_index < preprocess.token_count ? c_preprocess_token_location(&preprocess, preprocess.tokens[unsupported_token_index]) : declaration->location,
                               C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
                               string_format(arena, S8("in function '{S8}': {S8}"), declaration->name, unsupported_construct));
            continue;
        }
        c_parse_bind_function_body(&machine, arena, &result, preprocess, declaration_index);
    }
    c_parse_validate_unattached_cleanup_attributes(&result, preprocess);
    scratch_end(machine_temporary);
    BUSTER_CHECK(arena_destroy(machine_buffer_arena, 1));
    return result;
}
CParseResult c_parse(Arena* arena, CPreprocessResult preprocess)
{
    CParserResult syntax = c_parse_ast(arena, preprocess);
    return c_analyze_semantics(arena, preprocess, syntax);
}
