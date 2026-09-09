BUSTER_C_INTERNAL bool c_integer_expression_evaluate_with_features(Arena* arena, CSpellingSpace* space, CSymbolTable* symbols, CMacro* first_macro,
                                                                     CPpStampTable* stamps, CPpToken* tokens, u32 token_count, u32 expansion_limit,
                                                                     CPreprocessResult* result,
                                                                     CPreprocessOptions* options, String8 including_path,
                                                                     CIncludeSearchOrigin including_origin, u64* value_out)
{
    bool valid = true;
    char8 const* base = space->base;
    CPpToken* transformed = arena_allocate(arena, CPpToken, token_count);
    u32 transformed_count = 0;
    for (u32 token_index = 0; valid && token_index < token_count; token_index += 1)
    {
        CPpToken token = tokens[token_index];
        if (token.token.kind == C_TOKEN_IDENTIFIER && c_token_spelling_equal(base, token.token, S8("defined")))
        {
            u32 name_index = token_index + 1;
            bool parenthesized = name_index < token_count && c_token_is_punctuator(&tokens[name_index].token, C_PUNCTUATOR_LEFT_PARENTHESIS);
            name_index += parenthesized;
            if (name_index >= token_count || tokens[name_index].token.kind != C_TOKEN_IDENTIFIER)
            {
                valid = false;
            }
            else
            {
                CMacro* macro = c_macro_find_token(first_macro, symbols, base, &tokens[name_index].token);
                CPpToken replacement = token;
                replacement.token.kind = C_TOKEN_PREPROCESSING_NUMBER;
                replacement.token.punctuator = C_PUNCTUATOR_NONE;
                replacement.token.offset = macro && macro->definition.defined ? C_SPELLING_ONE : C_SPELLING_ZERO;
                replacement.token.length = 1;
                replacement.token.symbol = 0;
                transformed[transformed_count++] = replacement;
                token_index = name_index;
                if (parenthesized)
                {
                    valid = token_index + 1 < token_count && c_token_is_punctuator(&tokens[token_index + 1].token, C_PUNCTUATOR_RIGHT_PARENTHESIS);
                    token_index += 1;
                }
            }
        }
        else
        {
            transformed[transformed_count++] = token;
        }
    }
    CPreprocessTokenNode* first_expanded = 0;
    CPreprocessTokenNode* last_expanded = 0;
    u64 expanded_count = 0;
    if (valid)
    {
        valid = c_preprocess_expand(arena, space, symbols, first_macro, 0, 0, stamps, transformed, transformed_count, &first_expanded,
                                    &last_expanded, &expanded_count, expansion_limit, result);
    }
    if (valid)
    {
        valid = c_conditional_feature_operators(arena, space, symbols, first_macro, first_expanded, options, including_path, including_origin);
    }
    if (valid)
    {
        u64* values = arena_allocate(arena, u64, expanded_count + 1);
        u8* value_flags = arena_allocate(arena, u8, expanded_count + 1);
        CConditionalOperator* operations = arena_allocate(arena, CConditionalOperator, expanded_count + 1);
        u32 value_count = 0;
        u32 operation_count = 0;
        bool expect_operand = true;
        for (CPreprocessTokenNode* node = first_expanded; valid && node; node = node->next)
        {
            CToken token = node->token.token;
            if (token.kind == C_TOKEN_PREPROCESSING_NUMBER)
            {
                u64 value = 0;
                String8 number_spelling = c_token_spelling(base, token);
                valid = expect_operand && c_conditional_number(number_spelling, &value);
                if (valid)
                {
                    // The literal is uintmax_t when it says so or when its
                    // value does not fit intmax_t. Characters and keywords
                    // below remain signed regardless of their bit patterns.
                    bool literal_unsigned = value > (u64)INT64_MAX;
                    for (u64 suffix_index = 0; suffix_index < number_spelling.length; suffix_index += 1)
                    {
                        literal_unsigned |= number_spelling.pointer[suffix_index] == 'u' || number_spelling.pointer[suffix_index] == 'U';
                    }
                    value_flags[value_count] = literal_unsigned ? C_CONDITIONAL_VALUE_UNSIGNED : 0;
                    values[value_count++] = value;
                    expect_operand = false;
                }
            }
            else if (token.kind == C_TOKEN_CHARACTER_LITERAL)
            {
                u64 character = 0;
                CTypeKind character_kind = C_TYPE_INVALID;
                valid = expect_operand && c_ir_decode_character_value(arena, base, token, result->target, &character, &character_kind);
                if (valid)
                {
                    value_flags[value_count] = 0;
                    values[value_count++] = character;
                    expect_operand = false;
                }
            }
            else if (token.kind == C_TOKEN_IDENTIFIER)
            {
                valid = expect_operand;
                if (valid)
                {
                    value_flags[value_count] = 0;
                    values[value_count++] = c_preprocess_dialect_is_c23(result->dialect) && c_token_spelling_equal(base, token, S8("true"));
                    expect_operand = false;
                }
            }
            else if (token.kind != C_TOKEN_PUNCTUATOR)
            {
                valid = false;
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_LEFT_PARENTHESIS))
            {
                valid = expect_operand;
                if (valid)
                {
                    operations[operation_count++] = C_CONDITIONAL_OPEN;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_RIGHT_PARENTHESIS))
            {
                valid = !expect_operand;
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
                {
                    valid = c_conditional_apply(operations[--operation_count], values, value_flags, &value_count);
                }
                valid = valid && operation_count != 0;
                if (valid)
                {
                    operation_count -= 1;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_QUESTION))
            {
                valid = !expect_operand;
                u32 precedence = c_conditional_precedence(C_CONDITIONAL_QUESTION);
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN &&
                       c_conditional_precedence(operations[operation_count - 1]) > precedence)
                {
                    valid = c_conditional_apply(operations[--operation_count], values, value_flags, &value_count);
                }
                if (valid)
                {
                    operations[operation_count++] = C_CONDITIONAL_QUESTION;
                    expect_operand = true;
                }
            }
            else if (c_token_is_punctuator(&token, C_PUNCTUATOR_COLON))
            {
                valid = !expect_operand;
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_QUESTION)
                {
                    CConditionalOperator previous = operations[--operation_count];
                    valid = previous != C_CONDITIONAL_OPEN && c_conditional_apply(previous, values, value_flags, &value_count);
                }
                valid = valid && operation_count != 0;
                if (valid)
                {
                    operations[operation_count - 1] = C_CONDITIONAL_SELECT;
                    expect_operand = true;
                }
            }
            else
            {
                CConditionalOperator operation = C_CONDITIONAL_OPERATOR_COUNT;
                valid = c_conditional_operator(token, expect_operand, &operation);
                bool unary = c_conditional_is_unary(operation);
                valid = valid && expect_operand == unary;
                u32 precedence = c_conditional_precedence(operation);
                while (valid && operation_count && operations[operation_count - 1] != C_CONDITIONAL_OPEN)
                {
                    CConditionalOperator previous = operations[operation_count - 1];
                    u32 previous_precedence = c_conditional_precedence(previous);
                    if (previous_precedence < precedence || (unary && previous_precedence == precedence))
                    {
                        break;
                    }
                    operation_count -= 1;
                    valid = c_conditional_apply(previous, values, value_flags, &value_count);
                }
                if (valid)
                {
                    operations[operation_count++] = operation;
                    expect_operand = true;
                }
            }
        }
        valid = valid && !expect_operand;
        while (valid && operation_count)
        {
            CConditionalOperator operation = operations[--operation_count];
            valid = operation != C_CONDITIONAL_OPEN && operation != C_CONDITIONAL_QUESTION &&
                    c_conditional_apply(operation, values, value_flags, &value_count);
        }
        valid = valid && value_count == 1 && !(value_flags[0] & C_CONDITIONAL_VALUE_FAULT);
        if (valid)
        {
            *value_out = values[0];
        }
    }
    return valid;
}
