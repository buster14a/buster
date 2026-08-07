#include <buster/tests/compiler/frontend/buster/parser_test.h>
#if BUSTER_INCLUDE_TESTS

// Number of operand subtrees a node consumes. Drives the implicit-tree walk:
// leaves push, unary pops 1, binary pops 2.
BUSTER_GLOBAL_LOCAL u32 ast_node_arity(AstNode* node)
{
    switch (node->id)
    {
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ENUM_LITERAL:
    {
        return 0;
    }
    case AST_NODE_ARRAY_LITERAL:
    {
        return node->array_literal.element_count;
    }
    case AST_NODE_AGGREGATE_LITERAL:
    {
        return node->aggregate_literal.field_count;
    }
    case AST_NODE_ARRAY_SLICE:
    {
        return 1 + (u32)node->array_slice.has_start + (u32)node->array_slice.has_end;
    }
    case AST_NODE_INTRINSIC_CALL:
    {
        return node->intrinsic_call.argument_count;
    }
    case AST_NODE_CALL:
    {
        return 1 + node->call.argument_count;
    }
    case AST_NODE_UNARY_MINUS:
    case AST_NODE_UNARY_PLUS:
    case AST_NODE_UNARY_LOGICAL_NOT:
    case AST_NODE_UNARY_BITWISE_NOT:
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_MEMBER_ACCESS:
    {
        return 1;
    }
    case AST_NODE_BINARY_PLUS:
    case AST_NODE_BINARY_MINUS:
    case AST_NODE_BINARY_ASTERISK:
    case AST_NODE_BINARY_SLASH:
    case AST_NODE_BINARY_PERCENT:
    case AST_NODE_BINARY_SHIFT_LEFT:
    case AST_NODE_BINARY_SHIFT_RIGHT:
    case AST_NODE_BINARY_EQUAL:
    case AST_NODE_BINARY_NOT_EQUAL:
    case AST_NODE_BINARY_LESS:
    case AST_NODE_BINARY_LESS_EQUAL:
    case AST_NODE_BINARY_GREATER:
    case AST_NODE_BINARY_GREATER_EQUAL:
    case AST_NODE_BINARY_AMPERSAND:
    case AST_NODE_BINARY_BAR:
    case AST_NODE_BINARY_CARET:
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    case AST_NODE_BINARY_RANGE:
    case AST_NODE_ARRAY_INDEX:
    {
        return 2;
    }
    case AST_NODE_COUNT:
    {
    }
    }

    BUSTER_UNREACHABLE();
}


BUSTER_GLOBAL_LOCAL String8 ast_node_symbol(AstNodeId id)
{
    switch (id)
    {
    case AST_NODE_UNARY_MINUS:
        return S8("neg");
    case AST_NODE_UNARY_PLUS:
        return S8("pos");
    case AST_NODE_UNARY_LOGICAL_NOT:
        return S8("not");
    case AST_NODE_UNARY_BITWISE_NOT:
        return S8("bit_not");
    case AST_NODE_ADDRESS_OF:
        return S8("&");
    case AST_NODE_DEREFERENCE:
        return S8("deref");
    case AST_NODE_BINARY_PLUS:
        return S8("+");
    case AST_NODE_BINARY_MINUS:
        return S8("-");
    case AST_NODE_BINARY_ASTERISK:
        return S8("*");
    case AST_NODE_BINARY_SLASH:
        return S8("/");
    case AST_NODE_BINARY_PERCENT:
        return S8("%");
    case AST_NODE_BINARY_SHIFT_LEFT:
        return S8("<<");
    case AST_NODE_BINARY_SHIFT_RIGHT:
        return S8(">>");
    case AST_NODE_BINARY_EQUAL:
        return S8("==");
    case AST_NODE_BINARY_NOT_EQUAL:
        return S8("!=");
    case AST_NODE_BINARY_LESS:
        return S8("<");
    case AST_NODE_BINARY_LESS_EQUAL:
        return S8("<=");
    case AST_NODE_BINARY_GREATER:
        return S8(">");
    case AST_NODE_BINARY_GREATER_EQUAL:
        return S8(">=");
    case AST_NODE_BINARY_AMPERSAND:
        return S8("&");
    case AST_NODE_BINARY_BAR:
        return S8("|");
    case AST_NODE_BINARY_CARET:
        return S8("^");
    case AST_NODE_BINARY_BOOLEAN_AND:
        return S8("and");
    case AST_NODE_BINARY_BOOLEAN_OR:
        return S8("or");
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
        return S8("and?");
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
        return S8("or?");
    case AST_NODE_BINARY_RANGE:
        return S8("range");
    case AST_NODE_ARRAY_INDEX:
        return S8("index");

    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ARRAY_LITERAL:
    case AST_NODE_ARRAY_SLICE:
    case AST_NODE_AGGREGATE_LITERAL:
    case AST_NODE_MEMBER_ACCESS:
    case AST_NODE_ENUM_LITERAL:
    case AST_NODE_CALL:
    case AST_NODE_INTRINSIC_CALL:
    case AST_NODE_COUNT:
    {
    }
    }

    BUSTER_UNREACHABLE();
}


// Reconstruct an S-expression from the implicit tree with a single forward pass
// over the postorder stream. This is the exact shape an analysis/typecheck pass
// takes: stream the contiguous array, push leaves and reduce operators against a
// small operand stack. No recursion, no pointer chasing.
String8 ast_expression_to_string(Arena* arena, AstExpression expression)
{
    if (expression.count == 0)
    {
        return S8("");
    }

    String8* stack = arena_allocate(arena, String8, expression.count);
    u32 top = 0;

    for (u32 i = 0; i < expression.count; i += 1)
    {
        AstNode* node = &expression.nodes[i];
        String8 formatted;

        u32 arity = ast_node_arity(node);
        if (node->id == AST_NODE_INTRINSIC_CALL)
        {
            BUSTER_CHECK(top >= arity);
            u32 first = top - arity;
            formatted = string_format(arena, S8("(@{S8}"), node->intrinsic_call.name.text);
            for (u32 argument_i = first; argument_i < top; argument_i += 1)
            {
                formatted = string_format(arena, S8("{S8} {S8}"), formatted, stack[argument_i]);
            }
            formatted = string_format(arena, S8("{S8})"), formatted);
            top -= arity;
        }
        else if (node->id == AST_NODE_CALL)
        {
            BUSTER_CHECK(top >= arity);
            u32 first = top - arity;
            formatted = string_format(arena, S8("(call {S8}"), stack[first]);
            for (u32 argument_i = first + 1; argument_i < top; argument_i += 1)
            {
                formatted = string_format(arena, S8("{S8} {S8}"), formatted, stack[argument_i]);
            }
            formatted = string_format(arena, S8("{S8})"), formatted);
            top -= arity;
        }
        else if (node->id == AST_NODE_AGGREGATE_LITERAL)
        {
            BUSTER_CHECK(top >= arity);
            u32 first = top - arity;
            formatted = S8("{");
            AstAggregateLiteralField* field = node->aggregate_literal.first_field;
            for (u32 field_i = 0; field_i < arity; field_i += 1)
            {
                BUSTER_CHECK(field);
                String8 separator = field_i ? S8(", ") : S8("");
                formatted = string_format(arena, S8("{S8}{S8}.{S8} = {S8}"), formatted, separator, field->name.text, stack[first + field_i]);
                field = field->next;
            }
            BUSTER_CHECK(!field);
            formatted = string_format(arena, S8("{S8}{S8}"), formatted, S8("}"));
            top -= arity;
        }
        else if (node->id == AST_NODE_MEMBER_ACCESS)
        {
            BUSTER_CHECK(top >= 1);
            String8 operand = stack[top - 1];
            top -= 1;
            formatted = string_format(arena, S8("{S8}.{S8}"), operand, node->member_access.member.text);
        }
        else if (node->id == AST_NODE_DEREFERENCE)
        {
            BUSTER_CHECK(top >= 1);
            String8 operand = stack[top - 1];
            top -= 1;
            formatted = string_format(arena, S8("{S8}.&"), operand);
        }
        else if (node->id == AST_NODE_ARRAY_LITERAL)
        {
            BUSTER_CHECK(top >= arity);
            u32 first = top - arity;
            formatted = S8("[");
            for (u32 element_i = first; element_i < top; element_i += 1)
            {
                String8 separator = element_i == first ? S8("") : S8(", ");
                formatted = string_format(arena, S8("{S8}{S8}{S8}"), formatted, separator, stack[element_i]);
            }
            formatted = string_format(arena, S8("{S8}]"), formatted);
            top -= arity;
        }
        else if (node->id == AST_NODE_ARRAY_SLICE)
        {
            BUSTER_CHECK(top >= arity);
            u32 first = top - arity;
            u32 bound = first + 1;
            formatted = string_format(arena, S8("{S8}["), stack[first]);
            if (node->array_slice.has_start)
            {
                formatted = string_format(arena, S8("{S8}{S8}"), formatted, stack[bound]);
                bound += 1;
            }
            formatted = string_format(arena, S8("{S8}.."), formatted);
            if (node->array_slice.has_end)
            {
                formatted = string_format(arena, S8("{S8}{S8}"), formatted, stack[bound]);
            }
            formatted = string_format(arena, S8("{S8}]"), formatted);
            top -= arity;
        }
        else
            switch (arity)
            {
                break;
            case 0:
            {
                switch (node->id)
                {
                    break;
                case AST_NODE_CONSTANT_INTEGER:
                {
                    formatted = node->integer.fits_u64 ? string_format(arena, S8("{u64}"), node->integer.value) : node->integer.spelling;
                }
                break;
                case AST_NODE_CONSTANT_FLOAT:
                {
                    formatted = node->floating.spelling;
                }
                break;
                case AST_NODE_CONSTANT_CHARACTER:
                {
                    formatted = node->character.spelling;
                }
                break;
                case AST_NODE_CONSTANT_STRING:
                {
                    formatted = node->string.spelling;
                }
                break;
                case AST_NODE_IDENTIFIER:
                {
                    formatted = node->identifier.text;
                }
                break;
                case AST_NODE_UNDEFINED:
                {
                    formatted = S8("undefined");
                }
                break;
                case AST_NODE_ENUM_LITERAL:
                {
                    formatted = string_format(arena, S8(".{S8}"), node->enum_literal.member.text);
                }
                break;
                default:
                    BUSTER_UNREACHABLE();
                }
            }
            break;
            case 1:
            {
                BUSTER_CHECK(top >= 1);
                String8 operand = stack[top - 1];
                top -= 1;
                formatted = string_format(arena, S8("({S8} {S8})"), ast_node_symbol(node->id), operand);
            }
            break;
            default:
            {
                BUSTER_CHECK(top >= 2);
                String8 right = stack[top - 1];
                String8 left = stack[top - 2];
                top -= 2;
                formatted = string_format(arena, S8("({S8} {S8} {S8})"), ast_node_symbol(node->id), left, right);
            }
            }

        stack[top] = formatted;
        top += 1;
    }

    BUSTER_CHECK(top == 1);
    return stack[0];
}

typedef struct AssignmentOperatorTestCase AssignmentOperatorTestCase;
struct AssignmentOperatorTestCase
{
    String8 spelling;
    TokenId token;
    AstAssignmentOperator operator;
};

BUSTER_GLOBAL_LOCAL AssignmentOperatorTestCase assignment_operator_test_cases[] = {
    {S8_INITIALIZER("="), TOKEN_EQUAL, AST_ASSIGNMENT_EQUAL},
    {S8_INITIALIZER("+="), TOKEN_PLUS_EQUAL, AST_ASSIGNMENT_PLUS_EQUAL},
    {S8_INITIALIZER("-="), TOKEN_MINUS_EQUAL, AST_ASSIGNMENT_MINUS_EQUAL},
    {S8_INITIALIZER("*="), TOKEN_ASTERISK_EQUAL, AST_ASSIGNMENT_MULTIPLY_EQUAL},
    {S8_INITIALIZER("/="), TOKEN_SLASH_EQUAL, AST_ASSIGNMENT_DIVIDE_EQUAL},
    {S8_INITIALIZER("%="), TOKEN_PERCENTAGE_EQUAL, AST_ASSIGNMENT_MODULO_EQUAL},
    {S8_INITIALIZER("<<="), TOKEN_SHIFT_LEFT_EQUAL, AST_ASSIGNMENT_SHIFT_LEFT_EQUAL},
    {S8_INITIALIZER(">>="), TOKEN_SHIFT_RIGHT_EQUAL, AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL},
    {S8_INITIALIZER("&="), TOKEN_AMPERSAND_EQUAL, AST_ASSIGNMENT_BITWISE_AND_EQUAL},
    {S8_INITIALIZER("|="), TOKEN_BAR_EQUAL, AST_ASSIGNMENT_BITWISE_OR_EQUAL},
    {S8_INITIALIZER("^="), TOKEN_CARET_EQUAL, AST_ASSIGNMENT_BITWISE_XOR_EQUAL},
};

BUSTER_GLOBAL_LOCAL bool tokenizer_stream_covers_source(TokenizerResult tokenizer, u64 source_length)
{
    bool result = tokenizer.token_count >= 1;
    u64 consumed_length = 0;
    u32 error_count = 0;

    for (u32 i = 0; i < tokenizer.token_count; i += 1)
    {
        Token* token = &tokenizer.tokens[i];
        u32 token_length = token_length_get(token);
        bool is_last = i + 1 == tokenizer.token_count;

        result = result && token->id < TOKEN_COUNT;
        if (is_last)
        {
            result = result && token->id == TOKEN_EOF && token_length == 0;
        }
        else
        {
            result = result && token->id != TOKEN_EOF && token_length > 0;
            consumed_length += token_length;
            error_count += token->id == TOKEN_ERROR;
        }
    }

    result = result && consumed_length == source_length;
    result = result && error_count == tokenizer.error_count;
    return result;
}

UnitTestResult parser_tokenizer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    u64 position = arena->position;

    {
        TokenizerResult tokenizer = tokenize(arena, S8("").pointer, 0);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, 0));
        BUSTER_TEST(arguments, tokenizer.token_count == 1);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_EOF);
        arena->position = position;
    }

    {
        TokenizerResult tokenizer = tokenize(arena, S8("").pointer, (u64)UINT32_MAX + 1);
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        BUSTER_TEST(arguments, tokenizer.token_count == 1);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_EOF);
        arena->position = position;
    }

    // Regression: a single lexeme longer than the 24-bit token length limit
    // must be reported as an error instead of being silently split into
    // multiple normal tokens.
    {
        u64 source_length = (u64)TOKEN_MAX_LENGTH + 1;
        char8* source = arena_allocate(arena, char8, source_length);
        memset(source, 'a', source_length);
        TokenizerResult tokenizer = tokenize(arena, source, source_length);
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        BUSTER_TEST(arguments, tokenizer.token_count == 3);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == TOKEN_MAX_LENGTH);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[1]) == 1);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_EOF);
        arena->position = position;
    }

    // A string literal over the token length limit keeps its original kind
    // for the representable prefix and reports the overflow remainder as an
    // error token.
    {
        u64 string_length = (u64)TOKEN_MAX_LENGTH + 2;
        char8* source = arena_allocate(arena, char8, string_length);
        source[0] = '"';
        memset(source + 1, 'a', string_length - 2);
        source[string_length - 1] = '"';
        TokenizerResult tokenizer = tokenize(arena, source, string_length);
        ParserResult parsed = parser_parse(arena, expression_arena, (String8){source, string_length}, tokenizer);
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        BUSTER_TEST(arguments, tokenizer.token_count == 3);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_STRING_LITERAL);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == TOKEN_MAX_LENGTH);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_EOF);
        BUSTER_TEST(arguments, parsed.diagnostic_count >= 1);
        arena->position = position;
    }

    {
        char8 source[] = {'1', '2', '3'};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        String8 spellings[] = {
            S8("0.0"),
            S8("1.0e+3"),
            S8("0x1.f"),
            S8("0x1.fp-2"),
        };
        TokenId tokens[] = {
            TOKEN_DECIMAL_FLOAT_LITERAL,
            TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT,
            TOKEN_HEXADECIMAL_FLOAT_LITERAL,
            TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT,
        };
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(spellings) == BUSTER_ARRAY_LENGTH(tokens));
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, spellings[i].pointer, spellings[i].length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, spellings[i].length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);
            BUSTER_TEST(arguments, tokenizer.token_count == 2);
            BUSTER_TEST(arguments, tokenizer.tokens[0].id == tokens[i]);
            BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == spellings[i].length);
            arena->position = position;
        }
    }

    {
        String8 spellings[] = {
            S8("'a'"), S8("'\\n'"), S8("'\\''"), S8("'ab'"), S8("''"),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, spellings[i].pointer, spellings[i].length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, spellings[i].length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);
            BUSTER_TEST(arguments, tokenizer.token_count == 2);
            BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_CHARACTER_LITERAL);
            BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == spellings[i].length);
            arena->position = position;
        }
    }

    {
        char8 source[] = {'\'', (char8)0xC3u, (char8)0xA9u, '\''};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_CHARACTER_LITERAL);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        String8 spellings[] = {
            S8("\"hello\""), S8("\"line\\n\""), S8("\"quote: \\\"\""), S8("\"hex: \\x41\""), S8("\"unicode: \\u{1F600}\""),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, spellings[i].pointer, spellings[i].length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, spellings[i].length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);
            BUSTER_TEST(arguments, tokenizer.token_count == 2);
            BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_STRING_LITERAL);
            BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == spellings[i].length);
            arena->position = position;
        }
    }

    {
        char8 source[] = {'"', (char8)0xC3u, (char8)0xA9u, '"'};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_STRING_LITERAL);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = {'1', '+', '2'};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 4);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_PLUS);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        arena->position = position;
    }

    {
        String8 source = S8("@reverse loop");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 5);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_AT);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_SPACE);
        BUSTER_TEST(arguments, tokenizer.tokens[3].id == TOKEN_KEYWORD_LOOP);
        arena->position = position;
    }

    {
        String8 spellings[] = {S8("break"), S8("continue")};
        TokenId tokens[] = {TOKEN_KEYWORD_BREAK, TOKEN_KEYWORD_CONTINUE};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(spellings) == BUSTER_ARRAY_LENGTH(tokens));
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, spellings[i].pointer, spellings[i].length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, spellings[i].length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);
            BUSTER_TEST(arguments, tokenizer.token_count == 2);
            BUSTER_TEST(arguments, tokenizer.tokens[0].id == tokens[i]);
            BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == spellings[i].length);
            arena->position = position;
        }
    }

    {
        String8 source = S8("breakfast continuer");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_IDENTIFIER);
        arena->position = position;
    }

    {
        String8 source = S8("switch => switcher");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 6);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_KEYWORD_SWITCH);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_FAT_ARROW);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[2]) == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[4].id == TOKEN_IDENTIFIER);
        arena->position = position;
    }

    {
        String8 spellings[] = {S8("and"), S8("or"), S8("and?"), S8("or?")};
        TokenId tokens[] = {
            TOKEN_KEYWORD_AND,
            TOKEN_KEYWORD_OR,
            TOKEN_KEYWORD_AND_SHORT_CIRCUIT,
            TOKEN_KEYWORD_OR_SHORT_CIRCUIT,
        };
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(spellings) == BUSTER_ARRAY_LENGTH(tokens));
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(spellings); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, spellings[i].pointer, spellings[i].length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, spellings[i].length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);
            BUSTER_TEST(arguments, tokenizer.token_count == 2);
            BUSTER_TEST(arguments, tokenizer.tokens[0].id == tokens[i]);
            BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == spellings[i].length);
            arena->position = position;
        }
    }

    {
        String8 source = S8("android orderly candy oracle");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 8);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, tokenizer.tokens[4].id == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, tokenizer.tokens[6].id == TOKEN_IDENTIFIER);
        arena->position = position;
    }

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(assignment_operator_test_cases); i += 1)
    {
        AssignmentOperatorTestCase test_case = assignment_operator_test_cases[i];
        TokenizerResult tokenizer = tokenize(arena, test_case.spelling.pointer, test_case.spelling.length);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, test_case.spelling.length));
        BUSTER_TEST(arguments, tokenizer.error_count == 0);
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == test_case.token);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == test_case.spelling.length);
        arena->position = position;
    }

    {
        char8 source[] = {'\t', '\t'};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_TAB);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = {'0', 'x'};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        arena->position = position;
    }

    {
        char8 source[] = {(char8)0xC3u, (char8)0xA9u};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        arena->position = position;
    }

    {
        char8 source[] = {(char8)0xF0u, (char8)0x80u, (char8)0x80u, (char8)0x80u};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == BUSTER_ARRAY_LENGTH(source) + 1);
        BUSTER_TEST(arguments, tokenizer.error_count == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = {'1', '+', '2', '\t', (char8)0xE2u, (char8)0x82u, (char8)0xACu, 0};
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 7);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_PLUS);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, tokenizer.tokens[3].id == TOKEN_TAB);
        BUSTER_TEST(arguments, tokenizer.tokens[4].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[4]) == 3);
        BUSTER_TEST(arguments, tokenizer.tokens[5].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, tokenizer.error_count == 2);
        arena->position = position;
    }

    {
        bool all_single_byte_inputs_pass = true;
        for (u32 byte = 0; byte <= 0xFFu; byte += 1)
        {
            char8 source[] = {(char8)byte};
            TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
            all_single_byte_inputs_pass = all_single_byte_inputs_pass && tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source));
            arena->position = position;
        }
        BUSTER_TEST(arguments, all_single_byte_inputs_pass);
    }

    {
        bool all_byte_pairs_pass = true;
        for (u32 first = 0; first <= 0xFFu && all_byte_pairs_pass; first += 1)
        {
            for (u32 second = 0; second <= 0xFFu; second += 1)
            {
                char8 source[] = {(char8)first, (char8)second};
                TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
                all_byte_pairs_pass = all_byte_pairs_pass && tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source));
                arena->position = position;
            }
        }
        BUSTER_TEST(arguments, all_byte_pairs_pass);
    }

    // Regression: an empty source must parse as a valid empty program rather
    // than being treated as a load failure by the bench corpus path.
    {
        String8 source = {0};
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, tokenizer.token_count == 1);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_EOF);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 0);
        BUSTER_TEST(arguments, parsed.type_declaration_count == 0);
        BUSTER_TEST(arguments, parsed.import_count == 0);
        arena->position = position;
    }

    bool expression_arena_destroyed = arena_destroy(expression_arena, 1);
    BUSTER_CHECK(expression_arena_destroyed);
    return result;
}

// Parse a bare expression by wrapping it in a minimal program and returning the
// postorder stream of its return value.
BUSTER_GLOBAL_LOCAL AstExpression parse_expression_snippet(Arena* arena, Arena* expression_arena, String8 expression)
{
    // Built by concatenation rather than string_format: the program braces would
    // otherwise be mistaken for format directives.
    String8 prefix = S8("code main[export] : fn[cc(c)] (argument:count: s32, argv: &&u8, envp: &&u8) s32\n{\n    return ");
    String8 suffix = S8(";\n}\n");

    u64 length = prefix.length + expression.length + suffix.length;
    char8* buffer = arena_allocate(arena, char8, length);
    memcpy(buffer, prefix.pointer, prefix.length);
    memcpy(buffer + prefix.length, expression.pointer, expression.length);
    memcpy(buffer + prefix.length + expression.length, suffix.pointer, suffix.length);

    TokenizerResult tokenizer = tokenize(arena, buffer, length);
    ParserResult parsed = parser_parse(arena, expression_arena, (String8){buffer, length}, tokenizer);
    BUSTER_CHECK(parsed.diagnostic_count == 0);
    BUSTER_CHECK(parsed.first_code);
    BUSTER_CHECK(parsed.first_code->body.first_statement);
    return parsed.first_code->body.first_statement->return_statement.expression;
}

UnitTestResult parser_expression_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    struct
    {
        String8 source;
        String8 expected;
    } cases[] = {
        {S8("'a'"), S8("'a'")},
        {S8("'\\n'"), S8("'\\n'")},
        {S8("'a' == 'b'"), S8("(== 'a' 'b')")},
        {S8("\"\""), S8("\"\"")},
        {S8("\"hello\""), S8("\"hello\"")},
        {S8("\"a\\n\""), S8("\"a\\n\"")},
        {S8("\"abc\"[1]"), S8("(index \"abc\" 1)")},
        {S8("0.0"), S8("0.0")},
        {S8("1.5 + 2.25"), S8("(+ 1.5 2.25)")},
        {S8("1.0e-3"), S8("1.0e-3")},
        {S8("0x1.fp+3"), S8("0x1.fp+3")},
        {S8("-0.5"), S8("(neg 0.5)")},
        {S8("1 + 2 * 3"), S8("(+ 1 (* 2 3))")},
        {S8("1+2"), S8("(+ 1 2)")},
        {S8("1 * 2 + 3"), S8("(+ (* 1 2) 3)")},
        {S8("1 + 2 + 3"), S8("(+ (+ 1 2) 3)")},
        {S8("1 - 2 - 3"), S8("(- (- 1 2) 3)")},
        {S8("2 * 3 + 4 * 5"), S8("(+ (* 2 3) (* 4 5))")},
        {S8("2 * -3"), S8("(* 2 (neg 3))")},
        {S8("- - 5"), S8("(neg (neg 5))")},
        {S8("!!1"), S8("(not (not 1))")},
        {S8("~!1"), S8("(bit_not (not 1))")},
        {S8("1 << 2 + 3 * 4"), S8("(<< 1 (+ 2 (* 3 4)))")},
        {S8("1 + 2 * 3 << 4 - 5"), S8("(<< (+ 1 (* 2 3)) (- 4 5))")},
        {S8("1 + 2 < 3 << 4"), S8("(< (+ 1 2) (<< 3 4))")},
        {S8("1 <= 2"), S8("(<= 1 2)")},
        {S8("1 > 2"), S8("(> 1 2)")},
        {S8("1 >= 2"), S8("(>= 1 2)")},
        {S8("1 == 2"), S8("(== 1 2)")},
        {S8("1 != 2"), S8("(!= 1 2)")},
        {S8("1 < 2 == 3"), S8("(== (< 1 2) 3)")},
        {S8("1 & 2 == 3"), S8("(& 1 (== 2 3))")},
        {S8("value"), S8("value")},
        {S8("value + 2 * count"), S8("(+ value (* 2 count))")},
        {S8("left * 3 + right / 4"), S8("(+ (* left 3) (/ right 4))")},
        {S8("value - 1 - offset"), S8("(- (- value 1) offset)")},
        {S8("value << 1 + offset"), S8("(<< value (+ 1 offset))")},
        {S8("value + 1 < limit << 2"), S8("(< (+ value 1) (<< limit 2))")},
        {S8("flags & mask == 3"), S8("(& flags (== mask 3))")},
        {S8("a | b ^ 1"), S8("(^ (| a b) 1)")},
        {S8("a and b"), S8("(and a b)")},
        {S8("a or b"), S8("(or a b)")},
        {S8("a and? b"), S8("(and? a b)")},
        {S8("a or? b"), S8("(or? a b)")},
        {S8("a or b and c"), S8("(or a (and b c))")},
        {S8("a or? b and? c"), S8("(or? a (and? b c))")},
        {S8("a and b & c == d"), S8("(and a (& b (== c d)))")},
        {S8("a and b or? c or d and? e"), S8("(or (or? (and a b) c) (and? d e))")},
        {S8("a .. b or c"), S8("(range a (or b c))")},
        {S8("(value + 2) * count"), S8("(* (+ value 2) count)")},
        {S8("value * (count + 3)"), S8("(* value (+ count 3))")},
        {S8("(a + b) * (c - 2)"), S8("(* (+ a b) (- c 2))")},
        {S8("((a + 1) * (b - 2)) << shift"), S8("(<< (* (+ a 1) (- b 2)) shift)")},
        {S8("-(value + 2) * count"), S8("(* (neg (+ value 2)) count)")},
        {S8("!(value == 0)"), S8("(not (== value 0))")},
        {S8("~(mask | 3) & flags"), S8("(& (bit_not (| mask 3)) flags)")},
        {S8("undefined"), S8("undefined")},
        {S8("[]"), S8("[]")},
        {S8("[1]"), S8("[1]")},
        {S8("[1, 2 + 3, value]"), S8("[1, (+ 2 3), value]")},
        {S8("[[1, 2], [3]]"), S8("[[1, 2], [3]]")},
        {S8("[1,]"), S8("[1]")},
        {S8("values[0]"), S8("(index values 0)")},
        {S8("values[1 + 2]"), S8("(index values (+ 1 2))")},
        {S8("values[0][1]"), S8("(index (index values 0) 1)")},
        {S8("-values[0]"), S8("(neg (index values 0))")},
        {S8("[1, 2][0]"), S8("(index [1, 2] 0)")},
        {S8("values[..]"), S8("values[..]")},
        {S8("values[1..]"), S8("values[1..]")},
        {S8("values[..3]"), S8("values[..3]")},
        {S8("values[1 + 1..4 - 1]"), S8("values[(+ 1 1)..(- 4 1)]")},
        {S8("values[1..3][0]"), S8("(index values[1..3] 0)")},
        {S8("-values[..]"), S8("(neg values[..])")},
        {S8("[1, 2][..]"), S8("[1, 2][..]")},
        {S8("0 .. n"), S8("(range 0 n)")},
        {S8("1 + 2 .. n * 2"), S8("(range (+ 1 2) (* n 2))")},
        {S8("@reverse(0 .. n)"), S8("(@reverse (range 0 n))")},
        {S8("@intrinsic()"), S8("(@intrinsic)")},
        {S8("@intrinsic(1, 2 + 3)"), S8("(@intrinsic 1 (+ 2 3))")},
        {S8("calculate()"), S8("(call calculate)")},
        {S8("calculate(1, 2 + 3,)"), S8("(call calculate 1 (+ 2 3))")},
        {S8("outer(inner(1), 2)"), S8("(call outer (call inner 1) 2)")},
        {S8("factory()(value)"), S8("(call (call factory) value)")},
        {S8("(select + fallback)(value)"), S8("(call (+ select fallback) value)")},
        {S8("get_values()[0]"), S8("(index (call get_values) 0)")},
        {S8("values[0](value)"), S8("(call (index values 0) value)")},
        {S8("-factory()(value)[0]"), S8("(neg (index (call (call factory) value) 0))")},
        {S8("{}"), S8("{}")},
        {S8("{ .a = 1, .b = 2 + 3, }"), S8("{.a = 1, .b = (+ 2 3)}")},
        {S8("value.field"), S8("value.field")},
        {S8("value.first.second"), S8("value.first.second")},
        {S8("factory().result"), S8("(call factory).result")},
        {S8("values[0].field"), S8("(index values 0).field")},
        {S8("object.method(1)"), S8("(call object.method 1)")},
        {S8("{ .value = 1 }.value"), S8("{.value = 1}.value")},
        {S8("&value"), S8("(& value)")},
        {S8("pointer.&"), S8("pointer.&")},
        {S8("pointer.&.&"), S8("pointer.&.&")},
        {S8("&pointer.&"), S8("(& pointer.&)")},
        {S8("pointer.&.field"), S8("pointer.&.field")},
        {S8("pointer.&[0]"), S8("(index pointer.& 0)")},
        {S8("pointer.&(value)"), S8("(call pointer.& value)")},
        {S8("&value & mask"), S8("(& (& value) mask)")},
        {S8(".ready"), S8(".ready")},
        {S8("@cast(.ready)"), S8("(@cast .ready)")},
        {S8(".ready.field"), S8(".ready.field")},
    };

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        u64 position = arena->position;
        AstExpression expression = parse_expression_snippet(arena, expression_arena, cases[i].source);
        String8 actual = ast_expression_to_string(arena, expression);
        BUSTER_STRING_TEST(arguments, actual, cases[i].expected);
        arena->position = position;
    }

    {
        char8 source_bytes[] = {'\'', (char8)0xC3u, (char8)0xA9u, '\''};
        String8 source = {source_bytes, BUSTER_ARRAY_LENGTH(source_bytes)};
        u64 position = arena->position;
        AstExpression expression = parse_expression_snippet(arena, expression_arena, source);
        BUSTER_TEST(arguments, expression.count == 1);
        BUSTER_TEST(arguments, expression.nodes[0].id == AST_NODE_CONSTANT_CHARACTER);
        if (expression.count == 1 && expression.nodes[0].id == AST_NODE_CONSTANT_CHARACTER)
        {
            AstCharacterLiteral character = expression.nodes[0].character;
            BUSTER_TEST(arguments, character.valid);
            BUSTER_TEST(arguments, character.code_point == 0xE9u);
            BUSTER_TEST(arguments, character.utf8_length == 2);
            BUSTER_TEST(arguments, character.utf8[0] == 0xC3u && character.utf8[1] == 0xA9u);
        }
        arena->position = position;
    }

    {
        char8 source_bytes[] = {'"', (char8)0xC3u, (char8)0xA9u, '"'};
        String8 source = {source_bytes, BUSTER_ARRAY_LENGTH(source_bytes)};
        u64 position = arena->position;
        AstExpression expression = parse_expression_snippet(arena, expression_arena, source);
        BUSTER_TEST(arguments, expression.count == 1);
        BUSTER_TEST(arguments, expression.nodes[0].id == AST_NODE_CONSTANT_STRING);
        if (expression.count == 1 && expression.nodes[0].id == AST_NODE_CONSTANT_STRING)
        {
            AstStringLiteral string = expression.nodes[0].string;
            BUSTER_TEST(arguments, string.valid);
            BUSTER_TEST(arguments, string.value.length == 2);
            BUSTER_TEST(arguments, (u8)string.value.pointer[0] == 0xC3u && (u8)string.value.pointer[1] == 0xA9u && string.value.pointer[2] == 0);
        }
        arena->position = position;
    }

    bool expression_arena_destroyed = arena_destroy(expression_arena, 1);
    BUSTER_CHECK(expression_arena_destroyed);
    return result;
}

UnitTestResult parser_result_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);
    u64 position = arena->position;

    {
        String8 source = S8("import math = \"core/math\";\n"
                            "import platform = \"system/platform\";\n"
                            "code identity : fn (value: math.Vector) math.Vector;\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        AstImport* math = parsed.first_import;
        AstImport* platform = math ? math->next : 0;

        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.import_count == 2);
        BUSTER_TEST(arguments, parsed.code_count == 1);
        BUSTER_TEST(arguments, math != 0 && platform != 0 && platform->next == 0);
        if (math && platform)
        {
            BUSTER_STRING_TEST(arguments, math->name_space.text, S8("math"));
            BUSTER_STRING_TEST(arguments, math->path, S8("core/math"));
            BUSTER_TEST(arguments, math->name_space.range.line == 0);
            BUSTER_TEST(arguments, math->name_space.range.column == 7);
            BUSTER_TEST(arguments, math->path_range.line == 0);
            BUSTER_TEST(arguments, math->path_range.column == 14);
            BUSTER_TEST(arguments, math->range.offset == 0);
            BUSTER_TEST(arguments, math->range.length == 26);
            BUSTER_STRING_TEST(arguments, platform->name_space.text, S8("platform"));
            BUSTER_STRING_TEST(arguments, platform->path, S8("system/platform"));
            BUSTER_TEST(arguments, platform->range.line == 1);
        }
        AstType* function = parsed.first_code ? parsed.first_code->type : 0;
        AstTypeArgument* argument = function && function->id == AST_TYPE_FUNCTION ? function->function.first_argument : 0;
        BUSTER_TEST(arguments, argument != 0 && argument->type != 0 && argument->type->id == AST_TYPE_QUALIFIED_NAMED);
        BUSTER_TEST(arguments, function != 0 && function->function.return_type != 0 && function->function.return_type->id == AST_TYPE_QUALIFIED_NAMED);
        if (argument && argument->type && argument->type->id == AST_TYPE_QUALIFIED_NAMED)
        {
            BUSTER_STRING_TEST(arguments, argument->type->qualified.name_space.text, S8("math"));
            BUSTER_STRING_TEST(arguments, argument->type->qualified.name.text, S8("Vector"));
        }
        arena->position = position;
    }

    {
        String8 source = S8("import math = \"core/math\";\n"
                            "code broken : fn (value: math.) void;\n"
                            "code recovered : fn () s32 { return 37; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_RIGHT_PARENTHESIS);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_IDENTIFIER);
        BUSTER_TEST(arguments, parsed.last_code != 0 && string_equal(parsed.last_code->name, S8("recovered")));
        if (parsed.last_code && parsed.last_code->body.last_statement)
        {
            AstExpression recovered = parsed.last_code->body.last_statement->return_statement.expression;
            BUSTER_TEST(arguments, recovered.count == 1);
            BUSTER_TEST(arguments, recovered.nodes[0].id == AST_NODE_CONSTANT_INTEGER);
            BUSTER_TEST(arguments, recovered.nodes[0].integer.value == 37);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data plain = \"hello\";\n"
                            "    data escaped = \"\\x00\\n\\r\\t\\\\\\'\\\"A\\u{1F600}\";\n"
                            "    return 0;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 3);

        AstStatement* plain = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* escaped = plain ? plain->next : 0;
        BUSTER_TEST(arguments, plain != 0 && plain->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, escaped != 0 && escaped->id == AST_STATEMENT_DATA);
        if (plain && escaped)
        {
            AstExpression plain_value = plain->data_statement.initializer;
            AstExpression escaped_value = escaped->data_statement.initializer;
            BUSTER_TEST(arguments, plain_value.count == 1 && plain_value.nodes[0].id == AST_NODE_CONSTANT_STRING);
            BUSTER_TEST(arguments, escaped_value.count == 1 && escaped_value.nodes[0].id == AST_NODE_CONSTANT_STRING);
            if (plain_value.count == 1 && escaped_value.count == 1)
            {
                AstStringLiteral plain_string = plain_value.nodes[0].string;
                AstStringLiteral escaped_string = escaped_value.nodes[0].string;
                char8 escaped_bytes[] = {
                    0, '\n', '\r', '\t', '\\', '\'', '"', 'A', (char8)0xF0u, (char8)0x9Fu, (char8)0x98u, (char8)0x80u,
                };
                BUSTER_TEST(arguments, plain_string.valid);
                BUSTER_STRING_TEST(arguments, plain_string.spelling, S8("\"hello\""));
                BUSTER_STRING_TEST(arguments, plain_string.value, S8("hello"));
                BUSTER_TEST(arguments, plain_string.value.pointer[plain_string.value.length] == 0);
                BUSTER_TEST(arguments, escaped_string.valid);
                BUSTER_STRING_TEST(arguments, escaped_string.value, ((String8){escaped_bytes, BUSTER_ARRAY_LENGTH(escaped_bytes)}));
                BUSTER_TEST(arguments, escaped_string.value.pointer[escaped_string.value.length] == 0);
                BUSTER_TEST(arguments, AST_STRING_LITERAL_DEFAULT_ELEMENT_BIT_WIDTH == 8);
            }
        }
        arena->position = position;
    }

    {
        String8 invalid_spellings[] = {
            S8("\"\\0\""), S8("\"\\x0\""), S8("\"\\xGG\""), S8("\"\\u{}\""), S8("\"\\u{D800}\""), S8("\"\\u{110000}\""), S8("\"\\u{1234567}\""),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid_spellings); i += 1)
        {
            StringLiteralParsing string = parse_string_literal(arena, invalid_spellings[i]);
            BUSTER_TEST(arguments, !string.valid);
            BUSTER_TEST(arguments, string.value.pointer[string.value.length] == 0);
            arena->position = position;
        }
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data invalid = \"bad\\q\";\n"
                            "    return 3;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_INVALID_STRING);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("invalid string literal"));
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_STRING_LITERAL);
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data invalid = \"missing quote;\n"
                            "    return 4;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_INVALID_STRING);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_STRING_LITERAL);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 4);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data ascii = 'a';\n"
                            "    data newline = '\\n';\n"
                            "    return @cast(ascii);\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 3);

        AstStatement* ascii = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* newline = ascii ? ascii->next : 0;
        BUSTER_TEST(arguments, ascii != 0 && ascii->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, newline != 0 && newline->id == AST_STATEMENT_DATA);
        if (ascii && newline)
        {
            AstExpression ascii_value = ascii->data_statement.initializer;
            AstExpression newline_value = newline->data_statement.initializer;
            BUSTER_TEST(arguments, ascii_value.count == 1 && ascii_value.nodes[0].id == AST_NODE_CONSTANT_CHARACTER);
            BUSTER_TEST(arguments, newline_value.count == 1 && newline_value.nodes[0].id == AST_NODE_CONSTANT_CHARACTER);
            if (ascii_value.count == 1 && newline_value.count == 1)
            {
                AstCharacterLiteral ascii_character = ascii_value.nodes[0].character;
                AstCharacterLiteral newline_character = newline_value.nodes[0].character;
                BUSTER_TEST(arguments, ascii_character.valid && !ascii_character.escaped);
                BUSTER_TEST(arguments, ascii_character.code_point == (u32)'a');
                BUSTER_TEST(arguments, ascii_character.utf8_length == 1 && ascii_character.utf8[0] == (u8)'a');
                BUSTER_TEST(arguments, newline_character.valid && newline_character.escaped);
                BUSTER_TEST(arguments, newline_character.code_point == (u32)'\n');
                BUSTER_TEST(arguments, newline_character.utf8_length == 1 && newline_character.utf8[0] == (u8)'\n');
                BUSTER_TEST(arguments, AST_CHARACTER_LITERAL_DEFAULT_BIT_WIDTH == 8);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data invalid = 'ab';\n"
                            "    return 3;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_INVALID_CHARACTER);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("invalid character literal"));
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_CHARACTER_LITERAL);
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data invalid = 'a;\n"
                            "    return 4;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_INVALID_CHARACTER);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_CHARACTER_LITERAL);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 4);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data decimal = 1.25;\n"
                            "    data scientific = 1.0e-3;\n"
                            "    data hexadecimal = 0x1.fp+2;\n"
                            "    return decimal;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 4);

        AstStatement* decimal = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* scientific = decimal ? decimal->next : 0;
        AstStatement* hexadecimal = scientific ? scientific->next : 0;
        BUSTER_TEST(arguments, decimal != 0 && decimal->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, scientific != 0 && scientific->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, hexadecimal != 0 && hexadecimal->id == AST_STATEMENT_DATA);
        if (decimal && scientific && hexadecimal)
        {
            AstExpression decimal_value = decimal->data_statement.initializer;
            AstExpression scientific_value = scientific->data_statement.initializer;
            AstExpression hexadecimal_value = hexadecimal->data_statement.initializer;
            BUSTER_TEST(arguments, decimal_value.count == 1 && decimal_value.nodes[0].id == AST_NODE_CONSTANT_FLOAT);
            BUSTER_TEST(arguments, scientific_value.count == 1 && scientific_value.nodes[0].id == AST_NODE_CONSTANT_FLOAT);
            BUSTER_TEST(arguments, hexadecimal_value.count == 1 && hexadecimal_value.nodes[0].id == AST_NODE_CONSTANT_FLOAT);
            if (decimal_value.count == 1 && scientific_value.count == 1 && hexadecimal_value.count == 1)
            {
                AstFloatLiteral decimal_literal = decimal_value.nodes[0].floating;
                AstFloatLiteral scientific_literal = scientific_value.nodes[0].floating;
                AstFloatLiteral hexadecimal_literal = hexadecimal_value.nodes[0].floating;
                BUSTER_STRING_TEST(arguments, decimal_literal.spelling, S8("1.25"));
                BUSTER_STRING_TEST(arguments, scientific_literal.spelling, S8("1.0e-3"));
                BUSTER_STRING_TEST(arguments, hexadecimal_literal.spelling, S8("0x1.fp+2"));
                BUSTER_TEST(arguments, decimal_literal.base == 10 && !decimal_literal.has_exponent);
                BUSTER_TEST(arguments, scientific_literal.base == 10 && scientific_literal.has_exponent);
                BUSTER_TEST(arguments, hexadecimal_literal.base == 16 && hexadecimal_literal.has_exponent);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data invalid = left and?;\n"
                            "    return 3;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_SEMICOLON);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data number: s32 = 1;\n"
                            "    data p: &s32 = &number;\n"
                            "    p.& = 0;\n"
                            "    return p.&;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 4);

        AstStatement* number = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* pointer = number ? number->next : 0;
        AstStatement* assignment = pointer ? pointer->next : 0;
        AstStatement* return_statement = assignment ? assignment->next : 0;
        BUSTER_TEST(arguments, number != 0 && number->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, pointer != 0 && pointer->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, assignment != 0 && assignment->id == AST_STATEMENT_ASSIGNMENT);
        BUSTER_TEST(arguments, return_statement != 0 && return_statement->id == AST_STATEMENT_RETURN);

        if (pointer && pointer->id == AST_STATEMENT_DATA)
        {
            AstType* pointer_type = pointer->data_statement.type;
            BUSTER_TEST(arguments, pointer_type != 0 && pointer_type->id == AST_TYPE_POINTER);
            if (pointer_type && pointer_type->id == AST_TYPE_POINTER)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + pointer_type->range.offset, pointer_type->range.length}), S8("&s32"));
                BUSTER_TEST(arguments, pointer_type->element_type != 0 && pointer_type->element_type->id == AST_TYPE_NAMED);
            }

            AstExpression initializer = pointer->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 2);
            if (initializer.count == 2)
            {
                BUSTER_TEST(arguments, initializer.nodes[0].id == AST_NODE_IDENTIFIER);
                BUSTER_TEST(arguments, initializer.nodes[1].id == AST_NODE_ADDRESS_OF);
                BUSTER_STRING_TEST(arguments, initializer.nodes[0].identifier.text, S8("number"));
                BUSTER_STRING_TEST(
                    arguments,
                    ((String8){source.pointer + initializer.nodes[1].pointer_operator.range.offset, initializer.nodes[1].pointer_operator.range.length}),
                    S8("&"));
            }
        }

        if (assignment && assignment->id == AST_STATEMENT_ASSIGNMENT)
        {
            AstExpression target = assignment->assignment_statement.target;
            BUSTER_TEST(arguments, target.count == 2);
            if (target.count == 2)
            {
                BUSTER_TEST(arguments, target.nodes[0].id == AST_NODE_IDENTIFIER);
                BUSTER_TEST(arguments, target.nodes[1].id == AST_NODE_DEREFERENCE);
                BUSTER_STRING_TEST(arguments,
                                   ((String8){source.pointer + target.nodes[1].pointer_operator.range.offset, target.nodes[1].pointer_operator.range.length}),
                                   S8(".&"));
            }
        }

        if (return_statement && return_statement->id == AST_STATEMENT_RETURN)
        {
            AstExpression expression = return_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 2);
            if (expression.count == 2)
            {
                BUSTER_TEST(arguments, expression.nodes[0].id == AST_NODE_IDENTIFIER);
                BUSTER_TEST(arguments, expression.nodes[1].id == AST_NODE_DEREFERENCE);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data value = pointer.; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_POSTFIX_ACCESS);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_SEMICOLON);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected member name or '&' after '.'"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Implicit = enum { a, b, c, }\n"
                            "type Explicit = enum { a = 0, b = 1 + 2, c = 4, }\n"
                            "code main : fn () s32 { data value: Explicit = .b; return value; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.type_declaration_count == 2);

        AstTypeDeclaration* implicit = parsed.first_type_declaration;
        AstTypeDeclaration* explicit = implicit ? implicit->next : 0;
        BUSTER_TEST(arguments, implicit != 0 && implicit->kind == AST_TYPE_DECLARATION_ENUM);
        BUSTER_TEST(arguments, explicit != 0 && explicit->kind == AST_TYPE_DECLARATION_ENUM);
        if (implicit && explicit)
        {
            BUSTER_STRING_TEST(arguments, implicit->name.text, S8("Implicit"));
            BUSTER_TEST(arguments, implicit->enum_member_count == 3);
            AstEnumMember* implicit_a = implicit->first_enum_member;
            AstEnumMember* implicit_b = implicit_a ? implicit_a->next : 0;
            AstEnumMember* implicit_c = implicit_b ? implicit_b->next : 0;
            BUSTER_TEST(arguments, implicit_a != 0 && implicit_b != 0 && implicit_c != 0 && implicit_c == implicit->last_enum_member);
            if (implicit_a && implicit_b && implicit_c)
            {
                BUSTER_STRING_TEST(arguments, implicit_a->name.text, S8("a"));
                BUSTER_STRING_TEST(arguments, implicit_b->name.text, S8("b"));
                BUSTER_STRING_TEST(arguments, implicit_c->name.text, S8("c"));
                BUSTER_TEST(arguments, !implicit_a->has_explicit_value);
                BUSTER_TEST(arguments, !implicit_b->has_explicit_value);
                BUSTER_TEST(arguments, !implicit_c->has_explicit_value);
            }

            BUSTER_STRING_TEST(arguments, explicit->name.text, S8("Explicit"));
            BUSTER_TEST(arguments, explicit->enum_member_count == 3);
            AstEnumMember* explicit_a = explicit->first_enum_member;
            AstEnumMember* explicit_b = explicit_a ? explicit_a->next : 0;
            AstEnumMember* explicit_c = explicit_b ? explicit_b->next : 0;
            BUSTER_TEST(arguments, explicit_a != 0 && explicit_b != 0 && explicit_c != 0 && explicit_c == explicit->last_enum_member);
            if (explicit_a && explicit_b && explicit_c)
            {
                BUSTER_TEST(arguments, explicit_a->has_explicit_value);
                BUSTER_TEST(arguments, explicit_b->has_explicit_value);
                BUSTER_TEST(arguments, explicit_c->has_explicit_value);
                BUSTER_TEST(arguments, explicit_a->value.count == 1 && explicit_a->value.nodes[0].integer.value == 0);
                BUSTER_TEST(arguments, explicit_b->value.count == 3 && explicit_b->value.nodes[2].id == AST_NODE_BINARY_PLUS);
                BUSTER_TEST(arguments, explicit_c->value.count == 1 && explicit_c->value.nodes[0].integer.value == 4);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + explicit_a->range.offset, explicit_a->range.length}), S8("a = 0"));
            }
            BUSTER_STRING_TEST(arguments, ((String8){source.pointer + implicit->range.offset, implicit->range.length}),
                               S8("type Implicit = enum { a, b, c, }"));
        }

        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 1 && initializer.nodes[0].id == AST_NODE_ENUM_LITERAL);
            if (initializer.count == 1 && initializer.nodes[0].id == AST_NODE_ENUM_LITERAL)
            {
                AstEnumLiteral literal = initializer.nodes[0].enum_literal;
                BUSTER_STRING_TEST(arguments, literal.member.text, S8("b"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + literal.range.offset, literal.range.length}), S8(".b"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Broken = enum { a = 0 b = 1, }\n"
                            "code main : fn () s32 { return 7; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ENUM_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_IDENTIFIER);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or '}' after enum value"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstExpression expression = parsed.first_code->body.last_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 1 && expression.nodes[0].integer.value == 7);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code[inline] first[export] : fn[cc(systemv)] () s32 { return 1; }\n"
                            "code second : fn[cc(win64)] () s32 { return 2; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.last_code != 0 && parsed.first_code != parsed.last_code);
        if (parsed.first_code && parsed.last_code)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_code->name, S8("first"));
            BUSTER_STRING_TEST(arguments, parsed.last_code->name, S8("second"));
            BUSTER_TEST(arguments, parsed.first_code->exported);
            BUSTER_TEST(arguments, parsed.first_code->inline_hint);
            BUSTER_TEST(arguments, parsed.first_code->type != 0 && parsed.first_code->type->id == AST_TYPE_FUNCTION);
            BUSTER_TEST(arguments, parsed.last_code->type != 0 && parsed.last_code->type->id == AST_TYPE_FUNCTION);
            if (parsed.first_code->type && parsed.last_code->type)
            {
                BUSTER_TEST(arguments, parsed.first_code->type->function.calling_convention == AST_CALLING_CONVENTION_SYSTEMV);
                BUSTER_TEST(arguments, parsed.last_code->type->function.calling_convention == AST_CALLING_CONVENTION_WIN64);
            }
            BUSTER_TEST(arguments, parsed.first_code->body.statement_count == 1);
            BUSTER_TEST(arguments, parsed.last_code->body.statement_count == 1);
            if (parsed.first_code->body.first_statement && parsed.last_code->body.first_statement)
            {
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->return_statement.expression.count == 1);
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->return_statement.expression.nodes[0].integer.value == 1);
                BUSTER_TEST(arguments, parsed.last_code->body.first_statement->return_statement.expression.nodes[0].integer.value == 2);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Index = u32\n"
                            "type Bytes = []u8\n"
                            "type Callback = fn (value: s32) s32\n"
                            "type Pointer = &s32\n"
                            "type Handler = &fn[cc(systemv)] (input: []u8, next: &fn (value: s32) bool) &fn (result: s32) u32\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 0);
        BUSTER_TEST(arguments, parsed.type_declaration_count == 5);

        AstTypeDeclaration* index = parsed.first_type_declaration;
        AstTypeDeclaration* bytes = index ? index->next : 0;
        AstTypeDeclaration* callback = bytes ? bytes->next : 0;
        AstTypeDeclaration* pointer = callback ? callback->next : 0;
        AstTypeDeclaration* handler = pointer ? pointer->next : 0;
        BUSTER_TEST(arguments, index != 0 && index->kind == AST_TYPE_DECLARATION_ALIAS);
        BUSTER_TEST(arguments, bytes != 0 && bytes->kind == AST_TYPE_DECLARATION_ALIAS);
        BUSTER_TEST(arguments, callback != 0 && callback->kind == AST_TYPE_DECLARATION_ALIAS);
        BUSTER_TEST(arguments, pointer != 0 && pointer->kind == AST_TYPE_DECLARATION_ALIAS);
        BUSTER_TEST(arguments, handler != 0 && handler->kind == AST_TYPE_DECLARATION_ALIAS);
        BUSTER_TEST(arguments, handler == parsed.last_type_declaration);

        if (index && bytes && callback && pointer && handler)
        {
            BUSTER_TEST(arguments, index->alias_type != 0 && index->alias_type->id == AST_TYPE_NAMED);
            if (index->alias_type)
            {
                BUSTER_STRING_TEST(arguments, index->alias_type->name, S8("u32"));
            }

            BUSTER_TEST(arguments, bytes->alias_type != 0 && bytes->alias_type->id == AST_TYPE_SLICE);
            AstType* bytes_element = bytes->alias_type ? bytes->alias_type->element_type : 0;
            BUSTER_TEST(arguments, bytes_element != 0 && bytes_element->id == AST_TYPE_NAMED);
            if (bytes_element)
            {
                BUSTER_STRING_TEST(arguments, bytes_element->name, S8("u8"));
            }

            AstType* callback_function = callback->alias_type;
            BUSTER_TEST(arguments, callback_function != 0 && callback_function->id == AST_TYPE_FUNCTION);
            if (callback_function && callback_function->id == AST_TYPE_FUNCTION)
            {
                BUSTER_TEST(arguments, callback_function->function.argument_count == 1);
                AstTypeArgument* value = callback_function->function.first_argument;
                BUSTER_TEST(arguments, value != 0);
                if (value)
                {
                    BUSTER_STRING_TEST(arguments, value->name, S8("value"));
                    BUSTER_TEST(arguments, value->type != 0 && value->type->id == AST_TYPE_NAMED);
                }
                BUSTER_TEST(arguments, callback_function->function.return_type != 0 && callback_function->function.return_type->id == AST_TYPE_NAMED);
            }

            BUSTER_TEST(arguments, pointer->alias_type != 0 && pointer->alias_type->id == AST_TYPE_POINTER);
            BUSTER_TEST(arguments,
                        pointer->alias_type != 0 && pointer->alias_type->element_type != 0 && pointer->alias_type->element_type->id == AST_TYPE_NAMED);

            AstType* handler_pointer = handler->alias_type;
            AstType* handler_function = handler_pointer ? handler_pointer->element_type : 0;
            BUSTER_TEST(arguments, handler_pointer != 0 && handler_pointer->id == AST_TYPE_POINTER);
            BUSTER_TEST(arguments, handler_function != 0 && handler_function->id == AST_TYPE_FUNCTION);
            if (handler_function && handler_function->id == AST_TYPE_FUNCTION)
            {
                BUSTER_TEST(arguments, handler_function->function.calling_convention == AST_CALLING_CONVENTION_SYSTEMV);
                BUSTER_TEST(arguments, handler_function->function.argument_count == 2);
                AstTypeArgument* input = handler_function->function.first_argument;
                AstTypeArgument* next = input ? input->next : 0;
                BUSTER_TEST(arguments, input != 0 && input->type != 0 && input->type->id == AST_TYPE_SLICE);
                BUSTER_TEST(arguments, next != 0 && next->type != 0 && next->type->id == AST_TYPE_POINTER);
                AstType* next_function = next && next->type ? next->type->element_type : 0;
                BUSTER_TEST(arguments, next_function != 0 && next_function->id == AST_TYPE_FUNCTION);
                if (next_function && next_function->id == AST_TYPE_FUNCTION)
                {
                    BUSTER_TEST(arguments, next_function->function.argument_count == 1);
                    BUSTER_TEST(arguments, next_function->function.return_type != 0 && next_function->function.return_type->id == AST_TYPE_NAMED);
                }

                AstType* return_pointer = handler_function->function.return_type;
                AstType* return_function = return_pointer ? return_pointer->element_type : 0;
                BUSTER_TEST(arguments, return_pointer != 0 && return_pointer->id == AST_TYPE_POINTER);
                BUSTER_TEST(arguments, return_function != 0 && return_function->id == AST_TYPE_FUNCTION);
                if (return_function && return_function->id == AST_TYPE_FUNCTION)
                {
                    BUSTER_TEST(arguments, return_function->function.argument_count == 1);
                    BUSTER_TEST(arguments, return_function->function.return_type != 0 && return_function->function.return_type->id == AST_TYPE_NAMED);
                }
            }

            BUSTER_STRING_TEST(arguments, ((String8){source.pointer + handler->range.offset, handler->range.length}),
                               S8("type Handler = &fn[cc(systemv)] (input: []u8, next: &fn (value: s32) bool) &fn (result: s32) u32"));
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Pair = struct { left: s32, right: []u8, }\n"
                            "type Number = union { signed_value: s32, unsigned_value: u32, }\n"
                            "code main : fn () s32 {\n"
                            "    data pair: Pair = { .left = 1, .right = 2, };\n"
                            "    return pair.left;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.type_declaration_count == 2);

        AstTypeDeclaration* structure = parsed.first_type_declaration;
        AstTypeDeclaration* union_type = structure ? structure->next : 0;
        BUSTER_TEST(arguments, structure != 0 && structure->kind == AST_TYPE_DECLARATION_STRUCT);
        BUSTER_TEST(arguments, union_type != 0 && union_type->kind == AST_TYPE_DECLARATION_UNION);
        if (structure && union_type)
        {
            BUSTER_STRING_TEST(arguments, structure->name.text, S8("Pair"));
            BUSTER_TEST(arguments, structure->field_count == 2);
            AstTypeField* left = structure->first_field;
            AstTypeField* right = left ? left->next : 0;
            BUSTER_TEST(arguments, left != 0 && right != 0 && right == structure->last_field);
            if (left && right)
            {
                BUSTER_STRING_TEST(arguments, left->name.text, S8("left"));
                BUSTER_TEST(arguments, left->type != 0 && left->type->id == AST_TYPE_NAMED);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + left->range.offset, left->range.length}), S8("left: s32"));
                BUSTER_STRING_TEST(arguments, right->name.text, S8("right"));
                BUSTER_TEST(arguments, right->type != 0 && right->type->id == AST_TYPE_SLICE);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + right->range.offset, right->range.length}), S8("right: []u8"));
            }

            BUSTER_STRING_TEST(arguments, union_type->name.text, S8("Number"));
            BUSTER_TEST(arguments, union_type->field_count == 2);
            BUSTER_STRING_TEST(arguments, ((String8){source.pointer + structure->range.offset, structure->range.length}),
                               S8("type Pair = struct { left: s32, right: []u8, }"));
        }

        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* return_statement = data ? data->next : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, return_statement != 0 && return_statement->id == AST_STATEMENT_RETURN);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 3);
            if (initializer.count == 3)
            {
                AstNode* aggregate = &initializer.nodes[2];
                BUSTER_TEST(arguments, aggregate->id == AST_NODE_AGGREGATE_LITERAL);
                BUSTER_TEST(arguments, aggregate->aggregate_literal.field_count == 2);
                AstAggregateLiteralField* first = aggregate->aggregate_literal.first_field;
                BUSTER_TEST(arguments, first != 0 && first->next != 0);
                if (first && first->next)
                {
                    BUSTER_STRING_TEST(arguments, first->name.text, S8("left"));
                    BUSTER_STRING_TEST(arguments, first->next->name.text, S8("right"));
                }
                BUSTER_STRING_TEST(arguments,
                                   ((String8){source.pointer + aggregate->aggregate_literal.range.offset, aggregate->aggregate_literal.range.length}),
                                   S8("{ .left = 1, .right = 2, }"));
            }
        }
        if (return_statement && return_statement->id == AST_STATEMENT_RETURN)
        {
            AstExpression expression = return_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 2);
            if (expression.count == 2)
            {
                AstNode* member = &expression.nodes[1];
                BUSTER_TEST(arguments, member->id == AST_NODE_MEMBER_ACCESS);
                BUSTER_STRING_TEST(arguments, member->member_access.member.text, S8("left"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + member->member_access.range.offset, member->member_access.range.length}),
                                   S8(".left"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Broken = struct { first: s32 second: s32, }\n"
                            "code main : fn () s32 { return 7; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_TYPE_FIELD_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_IDENTIFIER);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or '}' after type field"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstExpression expression = parsed.first_code->body.last_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 1 && expression.nodes[0].integer.value == 7);
        }
        arena->position = position;
    }

    {
        String8 source = S8("type S = struct { a: s32, b: s32, }\n"
                            "code main : fn () s32 { data s: S = { .a = 1 .b = 2 }; return 8; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_AGGREGATE_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_EQUAL);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or '}' after aggregate field"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstExpression expression = parsed.first_code->body.last_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 1 && expression.nodes[0].integer.value == 8);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data result: s32 = undefined;\n"
                            "    result + offset = source + 1;\n"
                            "    return result;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 3);
        AstStatement* data_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* assignment_statement = data_statement ? data_statement->next : 0;
        BUSTER_TEST(arguments, data_statement != 0 && data_statement->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, assignment_statement != 0 && assignment_statement->id == AST_STATEMENT_ASSIGNMENT);
        if (data_statement && data_statement->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data_statement->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 1 && initializer.nodes[0].id == AST_NODE_UNDEFINED);
        }
        if (assignment_statement && assignment_statement->id == AST_STATEMENT_ASSIGNMENT)
        {
            AstExpression target = assignment_statement->assignment_statement.target;
            BUSTER_TEST(arguments, target.count == 3);
            if (target.count == 3)
            {
                BUSTER_TEST(arguments, target.nodes[0].id == AST_NODE_IDENTIFIER);
                BUSTER_STRING_TEST(arguments, target.nodes[0].identifier.text, S8("result"));
                BUSTER_TEST(arguments, target.nodes[1].id == AST_NODE_IDENTIFIER);
                BUSTER_STRING_TEST(arguments, target.nodes[1].identifier.text, S8("offset"));
                BUSTER_TEST(arguments, target.nodes[2].id == AST_NODE_BINARY_PLUS);
            }
            BUSTER_TEST(arguments, assignment_statement->assignment_statement.value.count == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { consume(value); return 0; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, statement != 0 && statement->id == AST_STATEMENT_EXPRESSION);
        if (statement && statement->id == AST_STATEMENT_EXPRESSION)
        {
            AstExpression expression = statement->expression_statement.expression;
            BUSTER_TEST(arguments, expression.count == 3);
            if (expression.count == 3)
            {
                BUSTER_TEST(arguments, expression.nodes[2].id == AST_NODE_CALL);
                BUSTER_TEST(arguments, expression.nodes[2].call.argument_count == 1);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + expression.nodes[2].call.range.offset, expression.nodes[2].call.range.length}),
                                   S8("(value)"));
            }
            BUSTER_STRING_TEST(arguments, ((String8){source.pointer + statement->range.offset, statement->range.length}), S8("consume(value);"));
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { consume(1 2); return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_DECIMAL_INTEGER_LITERAL);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or ')' after call argument"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { consume(1; return 4; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_SEMICOLON);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or ')' after call argument"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 4);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data values = [1 missing(2, 3), 4];\n"
                            "    return 5;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER);
        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* return_statement = data ? data->next : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, return_statement != 0 && return_statement->id == AST_STATEMENT_RETURN);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 2);
            if (initializer.count == 2)
            {
                BUSTER_TEST(arguments, initializer.nodes[0].integer.value == 4);
                BUSTER_TEST(arguments, initializer.nodes[1].id == AST_NODE_ARRAY_LITERAL);
                BUSTER_TEST(arguments, initializer.nodes[1].array_literal.element_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    consume(1 missing(2, 3), 4);\n"
                            "    return 6;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER);
        AstStatement* expression_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* return_statement = expression_statement ? expression_statement->next : 0;
        BUSTER_TEST(arguments, expression_statement != 0 && expression_statement->id == AST_STATEMENT_EXPRESSION);
        BUSTER_TEST(arguments, return_statement != 0 && return_statement->id == AST_STATEMENT_RETURN);
        if (expression_statement && expression_statement->id == AST_STATEMENT_EXPRESSION)
        {
            AstExpression expression = expression_statement->expression_statement.expression;
            BUSTER_TEST(arguments, expression.count == 3);
            if (expression.count == 3)
            {
                BUSTER_TEST(arguments, expression.nodes[1].integer.value == 4);
                BUSTER_TEST(arguments, expression.nodes[2].id == AST_NODE_CALL);
                BUSTER_TEST(arguments, expression.nodes[2].call.argument_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data value = { .first = 1 missing(2, 3), .last = 4 };\n"
                            "    return 7;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_AGGREGATE_DELIMITER);
        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        AstStatement* return_statement = data ? data->next : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        BUSTER_TEST(arguments, return_statement != 0 && return_statement->id == AST_STATEMENT_RETURN);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 2);
            if (initializer.count == 2)
            {
                BUSTER_TEST(arguments, initializer.nodes[0].integer.value == 4);
                BUSTER_TEST(arguments, initializer.nodes[1].id == AST_NODE_AGGREGATE_LITERAL);
                BUSTER_TEST(arguments, initializer.nodes[1].aggregate_literal.field_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("type Recovered = struct { @ ignored, first: s32, bad: , last: u32, }\n"
                            "type Choice = enum { First = 1, Bad = , Last = 3, }\n"
                            "code main : fn () s32 { return 8; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 3);
        BUSTER_TEST(arguments, parsed.type_declaration_count == 2);
        BUSTER_TEST(arguments, parsed.code_count == 1);
        AstTypeDeclaration* structure = parsed.first_type_declaration;
        AstTypeDeclaration* enumeration = structure ? structure->next : 0;
        BUSTER_TEST(arguments, structure != 0 && structure->field_count == 2);
        BUSTER_TEST(arguments, enumeration != 0 && enumeration->enum_member_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.first_statement != 0);
        if (parsed.first_code && parsed.first_code->body.first_statement)
        {
            AstExpression expression = parsed.first_code->body.first_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 1 && expression.nodes[0].integer.value == 8);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data values: [3]s32 = [1, 2 + 3, 4];\n"
                            "    return values;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstType* array_type = data->data_statement.type;
            BUSTER_TEST(arguments, array_type != 0 && array_type->id == AST_TYPE_ARRAY);
            if (array_type && array_type->id == AST_TYPE_ARRAY)
            {
                BUSTER_TEST(arguments, array_type->array.count.fits_u64);
                BUSTER_TEST(arguments, array_type->array.count.value == 3);
                BUSTER_TEST(arguments, array_type->array.element_type != 0 && array_type->array.element_type->id == AST_TYPE_NAMED);
            }

            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 6);
            if (initializer.count == 6)
            {
                AstNode* literal = &initializer.nodes[5];
                BUSTER_TEST(arguments, literal->id == AST_NODE_ARRAY_LITERAL);
                BUSTER_TEST(arguments, literal->array_literal.element_count == 3);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + literal->array_literal.range.offset, literal->array_literal.range.length}),
                                   S8("[1, 2 + 3, 4]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data values = [1 2]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or ']' after array element"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data values: [1]s32 = [1];\n"
                            "    values[0] -= 1;\n"
                            "    return values[0];\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstStatement* assignment = parsed.first_code && parsed.first_code->body.first_statement ? parsed.first_code->body.first_statement->next : 0;
        BUSTER_TEST(arguments, assignment != 0 && assignment->id == AST_STATEMENT_ASSIGNMENT);
        if (assignment && assignment->id == AST_STATEMENT_ASSIGNMENT)
        {
            AstExpression target = assignment->assignment_statement.target;
            BUSTER_TEST(arguments, assignment->assignment_statement.operator== AST_ASSIGNMENT_MINUS_EQUAL);
            BUSTER_TEST(arguments, target.count == 3 && target.nodes[2].id == AST_NODE_ARRAY_INDEX);
            if (target.count == 3)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + target.nodes[2].array_index.range.offset, target.nodes[2].array_index.range.length}),
                                   S8("[0]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data value = values[]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_RIGHT_BRACKET);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            BUSTER_TEST(arguments, parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    data sliced = values[1 + 2..4];\n"
                            "    return sliced;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstStatement* data = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, data != 0 && data->id == AST_STATEMENT_DATA);
        if (data && data->id == AST_STATEMENT_DATA)
        {
            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 6);
            if (initializer.count == 6)
            {
                AstNode* slice = &initializer.nodes[5];
                BUSTER_TEST(arguments, slice->id == AST_NODE_ARRAY_SLICE);
                BUSTER_TEST(arguments, slice->array_slice.has_start);
                BUSTER_TEST(arguments, slice->array_slice.has_end);
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + slice->array_slice.range.offset, slice->array_slice.range.length}), S8("[1 + 2..4]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data value = values[1..2..3]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_DOUBLE_DOT);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_RIGHT_BRACKET);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            BUSTER_TEST(arguments, parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        }
        arena->position = position;
    }

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(assignment_operator_test_cases); i += 1)
    {
        u64 case_position = arena->position;
        AssignmentOperatorTestCase test_case = assignment_operator_test_cases[i];
        String8 prefix = S8("code main : fn () s32 { target ");
        String8 suffix = S8(" value + 1; return 0; }");
        u64 source_length = prefix.length + test_case.spelling.length + suffix.length;
        char8* source_pointer = arena_allocate(arena, char8, source_length);
        memcpy(source_pointer, prefix.pointer, prefix.length);
        memcpy(source_pointer + prefix.length, test_case.spelling.pointer, test_case.spelling.length);
        memcpy(source_pointer + prefix.length + test_case.spelling.length, suffix.pointer, suffix.length);
        String8 source = {source_pointer, source_length};

        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* assignment = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, assignment != 0 && assignment->id == AST_STATEMENT_ASSIGNMENT);
        if (assignment && assignment->id == AST_STATEMENT_ASSIGNMENT)
        {
            BUSTER_TEST(arguments, assignment->assignment_statement.operator== test_case.operator);
            BUSTER_TEST(arguments, assignment->assignment_statement.target.count == 1);
            BUSTER_TEST(arguments, assignment->assignment_statement.value.count == 3);
        }
        arena->position = case_position;
    }

    {
        String8 source = S8("code main : fn () s32 { target += ; return 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 1);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { target; return 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ASSIGNMENT_OPERATOR);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected assignment operator"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 1);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    if (a > 0) {\n"
                            "        if (b) { a = b; }\n"
                            "    } else {\n"
                            "        b = a;\n"
                            "    }\n"
                            "    return a + b;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* if_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, if_statement != 0 && if_statement->id == AST_STATEMENT_IF);
        if (if_statement && if_statement->id == AST_STATEMENT_IF)
        {
            AstIfStatement* if_data = &if_statement->if_statement;
            BUSTER_TEST(arguments, if_data->condition.count == 3);
            BUSTER_TEST(arguments, if_data->condition.count == 3 && if_data->condition.nodes[2].id == AST_NODE_BINARY_GREATER);
            BUSTER_TEST(arguments, if_data->alternative == AST_IF_ALTERNATIVE_BLOCK);
            BUSTER_TEST(arguments, if_data->then_block.statement_count == 1);
            BUSTER_TEST(arguments, if_data->else_block.statement_count == 1);

            AstStatement* nested_if = if_data->then_block.first_statement;
            BUSTER_TEST(arguments, nested_if != 0 && nested_if->id == AST_STATEMENT_IF);
            if (nested_if && nested_if->id == AST_STATEMENT_IF)
            {
                BUSTER_TEST(arguments, nested_if->if_statement.condition.count == 1);
                BUSTER_TEST(arguments, nested_if->if_statement.alternative == AST_IF_ALTERNATIVE_NONE);
                BUSTER_TEST(arguments, nested_if->if_statement.then_block.statement_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    if (a) { a = 1; }\n"
                            "    else if (b) { a = 2; }\n"
                            "    else if (c) { a = 3; }\n"
                            "    else { a = 4; }\n"
                            "    return a;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);

        AstStatement* outer = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, outer != 0 && outer->id == AST_STATEMENT_IF);
        if (outer && outer->id == AST_STATEMENT_IF)
        {
            BUSTER_TEST(arguments, outer->if_statement.condition.count == 1);
            BUSTER_TEST(arguments, outer->if_statement.then_block.statement_count == 1);
            BUSTER_TEST(arguments, outer->if_statement.alternative == AST_IF_ALTERNATIVE_IF);

            AstStatement* first_else_if = outer->if_statement.else_if;
            BUSTER_TEST(arguments, first_else_if != 0 && first_else_if->id == AST_STATEMENT_IF);
            if (first_else_if && first_else_if->id == AST_STATEMENT_IF)
            {
                BUSTER_TEST(arguments, first_else_if->next == 0);
                BUSTER_TEST(arguments, first_else_if->if_statement.condition.count == 1);
                BUSTER_TEST(arguments, first_else_if->if_statement.alternative == AST_IF_ALTERNATIVE_IF);

                AstStatement* second_else_if = first_else_if->if_statement.else_if;
                BUSTER_TEST(arguments, second_else_if != 0 && second_else_if->id == AST_STATEMENT_IF);
                if (second_else_if && second_else_if->id == AST_STATEMENT_IF)
                {
                    BUSTER_TEST(arguments, second_else_if->next == 0);
                    BUSTER_TEST(arguments, second_else_if->if_statement.condition.count == 1);
                    BUSTER_TEST(arguments, second_else_if->if_statement.alternative == AST_IF_ALTERNATIVE_BLOCK);
                    BUSTER_TEST(arguments, second_else_if->if_statement.else_block.statement_count == 1);
                    BUSTER_TEST(arguments, outer->range.offset + outer->range.length ==
                                               second_else_if->if_statement.else_block.range.offset + second_else_if->if_statement.else_block.range.length);
                }
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    if (value) { value = 1; } else value = 2;\n"
                            "    return 3;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ELSE_BODY);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected 'if' or '{' after 'else'"));
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_IDENTIFIER);
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { if (value) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { "
                            "switch (value + 1) { .a => { return 0; }, 2 => { return 2; }, "
                            "else => { return 3; } } return 4; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);

        AstStatement* statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, statement != 0 && statement->id == AST_STATEMENT_SWITCH);
        if (statement && statement->id == AST_STATEMENT_SWITCH)
        {
            AstSwitchStatement* switch_statement = &statement->switch_statement;
            BUSTER_TEST(arguments, switch_statement->expression.count == 3);
            BUSTER_TEST(arguments, switch_statement->expression.count == 3 && switch_statement->expression.nodes[2].id == AST_NODE_BINARY_PLUS);
            BUSTER_TEST(arguments, switch_statement->case_count == 3);

            AstSwitchCase* first = switch_statement->first_case;
            AstSwitchCase* second = first ? first->next : 0;
            AstSwitchCase* otherwise = second ? second->next : 0;
            BUSTER_TEST(arguments, first != 0 && !first->is_else);
            BUSTER_TEST(arguments, first != 0 && first->expression.count == 1 && first->expression.nodes[0].id == AST_NODE_ENUM_LITERAL);
            BUSTER_TEST(arguments, first != 0 && first->body.statement_count == 1);
            BUSTER_TEST(arguments, second != 0 && !second->is_else);
            BUSTER_TEST(arguments, second != 0 && second->expression.count == 1 && second->expression.nodes[0].id == AST_NODE_CONSTANT_INTEGER &&
                                       second->expression.nodes[0].integer.value == 2);
            BUSTER_TEST(arguments, otherwise != 0 && otherwise->is_else);
            BUSTER_TEST(arguments, otherwise != 0 && otherwise->expression.count == 0);
            BUSTER_TEST(arguments, otherwise != 0 && otherwise->next == 0);
            BUSTER_TEST(arguments, switch_statement->last_case == otherwise);
            BUSTER_TEST(arguments, switch_statement->else_case == otherwise);
            if (first && otherwise)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + first->range.offset, first->range.length}), S8(".a => { return 0; }"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + otherwise->range.offset, otherwise->range.length}), S8("else => { return 3; }"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { "
                            "switch (value) { 0 { return 1; }, else => { return 2; }, } "
                            "return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_FAT_ARROW);
        AstStatement* switch_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, switch_statement != 0 && switch_statement->id == AST_STATEMENT_SWITCH);
        BUSTER_TEST(arguments, switch_statement != 0 && switch_statement->switch_statement.else_case != 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0 &&
                                   parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { "
                            "switch (value) { 0 => { } 1 => { }, else => { }, } return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_SWITCH_CASE_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or '}' after switch case"));
        }
        AstStatement* switch_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, switch_statement != 0 && switch_statement->id == AST_STATEMENT_SWITCH);
        BUSTER_TEST(arguments, switch_statement != 0 && switch_statement->switch_statement.case_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0 &&
                                   parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { "
                            "switch (value) { else => { }, else => { }, } return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_DUPLICATE_SWITCH_ELSE);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("switch may only have one 'else' case"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0 &&
                                   parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    for (data outer = 0 .. count) {\n"
                            "        for (data inner: s32 = @reverse(0 .. count)) { total += inner; }\n"
                            "    }\n"
                            "    return total;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* outer = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, outer != 0 && outer->id == AST_STATEMENT_FOR);
        if (outer && outer->id == AST_STATEMENT_FOR)
        {
            BUSTER_STRING_TEST(arguments, outer->for_statement.name.text, S8("outer"));
            BUSTER_TEST(arguments, outer->for_statement.type == 0);
            BUSTER_TEST(arguments, outer->for_statement.iterable.count == 3);
            BUSTER_TEST(arguments, outer->for_statement.iterable.count == 3 && outer->for_statement.iterable.nodes[2].id == AST_NODE_BINARY_RANGE);
            BUSTER_TEST(arguments, outer->for_statement.body.statement_count == 1);

            AstStatement* inner = outer->for_statement.body.first_statement;
            BUSTER_TEST(arguments, inner != 0 && inner->id == AST_STATEMENT_FOR);
            if (inner && inner->id == AST_STATEMENT_FOR)
            {
                BUSTER_STRING_TEST(arguments, inner->for_statement.name.text, S8("inner"));
                AstType* type = inner->for_statement.type;
                BUSTER_TEST(arguments, type != 0 && type->id == AST_TYPE_NAMED);
                if (type && type->id == AST_TYPE_NAMED)
                {
                    BUSTER_STRING_TEST(arguments, type->name, S8("s32"));
                }
                BUSTER_TEST(arguments, inner->for_statement.iterable.count == 4);
                BUSTER_TEST(arguments, inner->for_statement.iterable.count == 4 && inner->for_statement.iterable.nodes[2].id == AST_NODE_BINARY_RANGE);
                BUSTER_TEST(arguments, inner->for_statement.iterable.count == 4 && inner->for_statement.iterable.nodes[3].id == AST_NODE_INTRINSIC_CALL);
                if (inner->for_statement.iterable.count == 4)
                {
                    AstIntrinsicCall* call = &inner->for_statement.iterable.nodes[3].intrinsic_call;
                    BUSTER_STRING_TEST(arguments, call->name.text, S8("reverse"));
                    BUSTER_TEST(arguments, call->argument_count == 1);
                }
                BUSTER_TEST(arguments, inner->for_statement.body.statement_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    loop (value < limit) {\n"
                            "        loop { value += 1; }\n"
                            "    }\n"
                            "    return value;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* conditional = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, conditional != 0 && conditional->id == AST_STATEMENT_LOOP);
        if (conditional && conditional->id == AST_STATEMENT_LOOP)
        {
            BUSTER_TEST(arguments, conditional->loop_statement.has_condition);
            BUSTER_TEST(arguments, conditional->loop_statement.condition.count == 3);
            BUSTER_TEST(arguments,
                        conditional->loop_statement.condition.count == 3 && conditional->loop_statement.condition.nodes[2].id == AST_NODE_BINARY_LESS);
            BUSTER_TEST(arguments, conditional->loop_statement.body.statement_count == 1);
            AstStatement* unconditional = conditional->loop_statement.body.first_statement;
            BUSTER_TEST(arguments, unconditional != 0 && unconditional->id == AST_STATEMENT_LOOP);
            if (unconditional && unconditional->id == AST_STATEMENT_LOOP)
            {
                BUSTER_TEST(arguments, !unconditional->loop_statement.has_condition);
                BUSTER_TEST(arguments, unconditional->loop_statement.condition.count == 0);
                BUSTER_TEST(arguments, unconditional->loop_statement.body.statement_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 {\n"
                            "    loop {\n"
                            "        break;\n"
                            "        continue;\n"
                            "    }\n"
                            "    return 0;\n"
                            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);

        AstStatement* loop = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, loop != 0 && loop->id == AST_STATEMENT_LOOP);
        if (loop && loop->id == AST_STATEMENT_LOOP)
        {
            AstBlock* body = &loop->loop_statement.body;
            BUSTER_TEST(arguments, body->statement_count == 2);
            AstStatement* break_statement = body->first_statement;
            AstStatement* continue_statement = break_statement ? break_statement->next : 0;
            BUSTER_TEST(arguments, break_statement != 0 && break_statement->id == AST_STATEMENT_BREAK);
            BUSTER_TEST(arguments, continue_statement != 0 && continue_statement->id == AST_STATEMENT_CONTINUE);
            BUSTER_TEST(arguments, continue_statement != 0 && continue_statement->next == 0);
            if (break_statement && continue_statement)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + break_statement->range.offset, break_statement->range.length}), S8("break;"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + continue_statement->range.offset, continue_statement->range.length}),
                                   S8("continue;"));
            }
        }
        arena->position = position;
    }

    {
        struct
        {
            String8 source;
            AstStatementId id;
        } cases[] = {
            {
                S8("code main : fn () s32 { loop { break value; } return 7; }"),
                AST_STATEMENT_BREAK,
            },
            {
                S8("code main : fn () s32 { loop { continue value; } return 7; }"),
                AST_STATEMENT_CONTINUE,
            },
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
        {
            TokenizerResult tokenizer = tokenize(arena, cases[i].source.pointer, cases[i].source.length);
            ParserResult parsed = parser_parse(arena, expression_arena, cases[i].source, tokenizer);
            BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
            BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN);
            BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_IDENTIFIER);
            BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_SEMICOLON);
            AstStatement* loop = parsed.first_code ? parsed.first_code->body.first_statement : 0;
            BUSTER_TEST(arguments, loop != 0 && loop->id == AST_STATEMENT_LOOP);
            if (loop && loop->id == AST_STATEMENT_LOOP)
            {
                BUSTER_TEST(arguments, loop->loop_statement.body.first_statement != 0 && loop->loop_statement.body.first_statement->id == cases[i].id);
            }
            BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
            if (parsed.first_code && parsed.first_code->body.last_statement)
            {
                AstStatement* recovered = parsed.first_code->body.last_statement;
                BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
                BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 7);
            }
            arena->position = position;
        }
    }

    {
        String8 source = S8("code main : fn () s32 { loop (value) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data range = 0 .. count .. 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_CHAINED_RANGE);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_DOUBLE_DOT);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("range operator '..' is not associative"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { for (data e = values[..]) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 && recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { return * 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected expression"));
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_ASTERISK);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code broken : fn[cc(c)] () s32 { return 1 + ; return 3; } code recovered : fn[cc(c)] () s32 { return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.code_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstExpression recovered_expression = parsed.first_code->body.last_statement->return_statement.expression;
            BUSTER_TEST(arguments, recovered_expression.count == 1 && recovered_expression.nodes[0].integer.value == 3);
        }
        BUSTER_TEST(arguments, parsed.last_code != 0);
        if (parsed.last_code)
        {
            BUSTER_STRING_TEST(arguments, parsed.last_code->name, S8("recovered"));
            BUSTER_TEST(arguments, parsed.last_code->body.first_statement != 0);
        }
        arena->position = position;
    }

    {
        String8 truncated_sources[] = {
            S8_INITIALIZER("code"),
            S8_INITIALIZER("code name"),
            S8_INITIALIZER("code main["),
            S8_INITIALIZER("code main : fn () s32 {"),
        };
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(truncated_sources); i += 1)
        {
            String8 source = truncated_sources[i];
            TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
            ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
            BUSTER_TEST(arguments, parsed.diagnostic_count > 0);
            arena->position = position;
        }
    }

    {
        String8 source = S8("code main : fn () s32 { return 1 + 1__0; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        AstStatement* statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, statement != 0 && statement->return_statement.expression.count == 3);
        if (statement && statement->return_statement.expression.count == 3)
        {
            AstNode* nodes = statement->return_statement.expression.nodes;
            BUSTER_TEST(arguments, nodes[0].id == AST_NODE_CONSTANT_INTEGER && nodes[0].integer.value == 1);
            BUSTER_TEST(arguments, nodes[1].id == AST_NODE_CONSTANT_INTEGER && nodes[1].integer.value == 0);
            BUSTER_TEST(arguments, nodes[2].id == AST_NODE_BINARY_PLUS);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code typed : fn (argument:count: s32, argv: &&u8, bytes: []u8, inferred: [_]u8) s32;");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstType* function = parsed.first_code ? parsed.first_code->type : 0;
        BUSTER_TEST(arguments, function != 0 && function->id == AST_TYPE_FUNCTION);
        if (function && function->id == AST_TYPE_FUNCTION)
        {
            BUSTER_STRING_TEST(arguments, ((String8){source.pointer + function->range.offset, function->range.length}),
                               S8("fn (argument:count: s32, argv: &&u8, bytes: []u8, inferred: [_]u8) s32"));
            BUSTER_TEST(arguments, function->function.argument_count == 4);
            BUSTER_TEST(arguments, function->function.return_type != 0);
            BUSTER_TEST(arguments, function->function.return_type && function->function.return_type->id == AST_TYPE_NAMED);
            AstTypeArgument* argument = function->function.first_argument;
            BUSTER_TEST(arguments, argument != 0);
            if (argument)
            {
                BUSTER_STRING_TEST(arguments, argument->name, S8("argument:count"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->range.offset, argument->range.length}), S8("argument:count: s32"));
                BUSTER_TEST(arguments, argument->type && argument->type->id == AST_TYPE_NAMED);
                argument = argument->next;
            }
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_POINTER);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->range.offset, argument->range.length}), S8("argv: &&u8"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->type->range.offset, argument->type->range.length}), S8("&&u8"));
                AstType* second_pointer = argument->type->element_type;
                BUSTER_TEST(arguments, second_pointer && second_pointer->id == AST_TYPE_POINTER);
                if (second_pointer)
                {
                    BUSTER_STRING_TEST(arguments, ((String8){source.pointer + second_pointer->range.offset, second_pointer->range.length}), S8("&u8"));
                }
            }
            argument = argument ? argument->next : 0;
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_SLICE);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->range.offset, argument->range.length}), S8("bytes: []u8"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->type->range.offset, argument->type->range.length}), S8("[]u8"));
            }
            argument = argument ? argument->next : 0;
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_INFERRED_ARRAY);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->range.offset, argument->range.length}), S8("inferred: [_]u8"));
                BUSTER_STRING_TEST(arguments, ((String8){source.pointer + argument->type->range.offset, argument->type->range.length}), S8("[_]u8"));
            }
        }
        arena->position = position;
    }

    struct
    {
        String8 literal;
        u64 expected_value;
        bool expected_fits_u64;
        ParserDiagnosticKind expected_diagnostic;
        u32 diagnostic_count;
    } integer_cases[] = {
        {S8_INITIALIZER("1_000"), 1000, true, PARSER_DIAGNOSTIC_COUNT, 0},
        {S8_INITIALIZER("0xF_F"), 255, true, PARSER_DIAGNOSTIC_COUNT, 0},
        {S8_INITIALIZER("1__000"), 0, false, PARSER_DIAGNOSTIC_INVALID_INTEGER, 1},
        {S8_INITIALIZER("18446744073709551616"), 0, false, PARSER_DIAGNOSTIC_COUNT, 0},
        {S8_INITIALIZER("340282366920938463463374607431768211456"), 0, false, PARSER_DIAGNOSTIC_COUNT, 0},
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(integer_cases); i += 1)
    {
        u64 case_position = arena->position;
        String8 prefix = S8("code main : fn[cc(c)] () s32 { return ");
        String8 suffix = S8("; }");
        u64 source_length = prefix.length + integer_cases[i].literal.length + suffix.length;
        char8* source_pointer = arena_allocate(arena, char8, source_length);
        memcpy(source_pointer, prefix.pointer, prefix.length);
        memcpy(source_pointer + prefix.length, integer_cases[i].literal.pointer, integer_cases[i].literal.length);
        memcpy(source_pointer + prefix.length + integer_cases[i].literal.length, suffix.pointer, suffix.length);
        String8 source = {source_pointer, source_length};
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == integer_cases[i].diagnostic_count);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->kind == integer_cases[i].expected_diagnostic);
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.first_statement != 0);
        if (parsed.first_code && parsed.first_code->body.first_statement)
        {
            AstExpression expression = parsed.first_code->body.first_statement->return_statement.expression;
            BUSTER_TEST(arguments, expression.count == 1);
            if (expression.count == 1)
            {
                BUSTER_TEST(arguments, expression.nodes[0].integer.value == integer_cases[i].expected_value);
                BUSTER_TEST(arguments, expression.nodes[0].integer.fits_u64 == integer_cases[i].expected_fits_u64);
                BUSTER_STRING_TEST(arguments, expression.nodes[0].integer.spelling, integer_cases[i].literal);
            }
        }
        arena->position = case_position;
    }

    // Regression: prefix runs used to abort past 16 operators because pending
    // unaries lived in a fixed-size array instead of state-stack frames.
    {
        u64 deep_unary_position = arena->position;

        enum
        {
            deep_unary_count = 40
        };
        u64 source_length = deep_unary_count * 2 + 1;
        char8* source_pointer = arena_allocate(arena, char8, source_length);
        for (u64 i = 0; i < deep_unary_count; i += 1)
        {
            source_pointer[i * 2] = '-';
            source_pointer[i * 2 + 1] = ' ';
        }
        source_pointer[source_length - 1] = '5';
        String8 source = string_from_pointer_length(source_pointer, source_length);

        String8 nesting_open = S8("(neg ");
        u64 expected_length = deep_unary_count * nesting_open.length + 1 + deep_unary_count;
        char8* expected_pointer = arena_allocate(arena, char8, expected_length);
        u64 expected_offset = 0;
        for (u64 i = 0; i < deep_unary_count; i += 1)
        {
            memcpy(expected_pointer + expected_offset, nesting_open.pointer, nesting_open.length);
            expected_offset += nesting_open.length;
        }
        expected_pointer[expected_offset] = '5';
        expected_offset += 1;
        for (u64 i = 0; i < deep_unary_count; i += 1)
        {
            expected_pointer[expected_offset] = ')';
            expected_offset += 1;
        }
        String8 expected = string_from_pointer_length(expected_pointer, expected_length);

        AstExpression expression = parse_expression_snippet(arena, expression_arena, source);
        String8 actual = ast_expression_to_string(arena, expression);
        BUSTER_STRING_TEST(arguments, actual, expected);

        arena->position = deep_unary_position;
    }

    // Exercise the explicit state stack with nesting far beyond typical source:
    // a long prefix run, deeply nested calls, and a long mixed postfix chain.
    {
        u64 stress_position = arena->position;
        enum
        {
            stress_prefix_count = 256,
            stress_call_count = 256,
            stress_postfix_count = 128,
        };
        String8 prefix = S8("& ");
        String8 call_open = S8("f(");
        String8 leaf = S8("value");
        String8 postfix = S8("[0].field()");
        u64 source_length = stress_prefix_count * prefix.length + stress_call_count * call_open.length + leaf.length + stress_call_count +
                            stress_postfix_count * postfix.length;
        char8* source_pointer = arena_allocate(arena, char8, source_length);
        u64 offset = 0;
        for (u32 i = 0; i < stress_prefix_count; i += 1)
        {
            memcpy(source_pointer + offset, prefix.pointer, prefix.length);
            offset += prefix.length;
        }
        for (u32 i = 0; i < stress_call_count; i += 1)
        {
            memcpy(source_pointer + offset, call_open.pointer, call_open.length);
            offset += call_open.length;
        }
        memcpy(source_pointer + offset, leaf.pointer, leaf.length);
        offset += leaf.length;
        for (u32 i = 0; i < stress_call_count; i += 1)
        {
            source_pointer[offset] = ')';
            offset += 1;
        }
        for (u32 i = 0; i < stress_postfix_count; i += 1)
        {
            memcpy(source_pointer + offset, postfix.pointer, postfix.length);
            offset += postfix.length;
        }
        BUSTER_CHECK(offset == source_length);

        AstExpression expression = parse_expression_snippet(arena, expression_arena, string_from_pointer_length(source_pointer, source_length));
        u32 expected_node_count = stress_prefix_count + stress_call_count * 2 + 1 + stress_postfix_count * 4;
        BUSTER_TEST(arguments, expression.count == expected_node_count);
        if (expression.count == expected_node_count)
        {
            BUSTER_TEST(arguments, expression.nodes[0].id == AST_NODE_IDENTIFIER);
            BUSTER_STRING_TEST(arguments, expression.nodes[0].identifier.text, S8("f"));
            BUSTER_TEST(arguments, expression.nodes[expression.count - 1].id == AST_NODE_ADDRESS_OF);
        }

        arena->position = stress_position;
    }

    // The caller-owned expression arena is staging only: parser_parse()
    // rewinds it for every run, while previously returned AST nodes remain
    // valid because completed expressions live in the result arena.
    {
        u64 reuse_position = arena->position;
        String8 first_source = S8("code first : fn () s32 { return 1 + 2; }");
        TokenizerResult first_tokenizer = tokenize(arena, first_source.pointer, first_source.length);
        ParserResult first = parser_parse(arena, expression_arena, first_source, first_tokenizer);
        BUSTER_TEST(arguments, first.diagnostic_count == 0);
        AstStatement* first_return = first.first_code ? first.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, first_return != 0 && first_return->id == AST_STATEMENT_RETURN);

        arena_allocate(expression_arena, AstNode, 128);
        u64 polluted_expression_position = expression_arena->position;

        String8 second_source = S8("code second : fn () s32 { return 7; }");
        TokenizerResult second_tokenizer = tokenize(arena, second_source.pointer, second_source.length);
        ParserResult second = parser_parse(arena, expression_arena, second_source, second_tokenizer);
        BUSTER_TEST(arguments, second.diagnostic_count == 0);
        BUSTER_TEST(arguments, expression_arena->position < polluted_expression_position);

        if (first_return && first_return->id == AST_STATEMENT_RETURN)
        {
            AstExpression first_expression = first_return->return_statement.expression;
            BUSTER_TEST(arguments, first_expression.count == 3);
            BUSTER_TEST(arguments, first_expression.count == 3 && first_expression.nodes[0].id == AST_NODE_CONSTANT_INTEGER &&
                                       first_expression.nodes[0].integer.value == 1 && first_expression.nodes[1].id == AST_NODE_CONSTANT_INTEGER &&
                                       first_expression.nodes[1].integer.value == 2 && first_expression.nodes[2].id == AST_NODE_BINARY_PLUS);
        }
        arena->position = reuse_position;
    }

    // Regression: parser_parse() must not crash when the caller hands it one
    // of the thread's own scratch arenas as result_arena. The parser used to
    // carve its expression arena out of the same scratch pool, so a caller-
    // supplied scratch arena could exhaust both slots and hand back null.
    {
        TemporalArena caller_scratch = scratch_begin(0, 0);
        String8 source = S8("code main : fn[cc(c)] () s32 { return 1 + 2 * 3; }");
        TokenizerResult tokenizer = tokenize(caller_scratch.arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(caller_scratch.arena, expression_arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 1);
        scratch_end(caller_scratch);
    }

    // The benchmark modes share the parser path but own their source storage
    // differently: IO reloads each file through the iteration scratch arena,
    // while parse preloads and releases the complete corpus separately.
    {
        ParserBenchResult io_result = parser_bench_run(arena, 1, PARSER_BENCH_MODE_IO);
        ParserBenchResult parse_result = parser_bench_run(arena, 1, PARSER_BENCH_MODE_PARSE);
        BUSTER_TEST(arguments, io_result.source_load_succeeded);
        BUSTER_TEST(arguments, parse_result.source_load_succeeded);
        BUSTER_TEST(arguments, io_result.file_count == PARSER_FILE_TEST_CASE_COUNT);
        BUSTER_TEST(arguments, parse_result.file_count == PARSER_FILE_TEST_CASE_COUNT);
        BUSTER_TEST(arguments, io_result.iterations == 1);
        BUSTER_TEST(arguments, parse_result.iterations == 1);
#if BUSTER_INSTRUMENT
        BUSTER_TEST(arguments, io_result.files != 0);
        BUSTER_TEST(arguments, parse_result.files != 0);
#endif

        ParserBenchResult invalid_result = parser_bench_run(arena, 1, PARSER_BENCH_MODE_COUNT);
        BUSTER_TEST(arguments, !invalid_result.source_load_succeeded);
    }

    bool expression_arena_destroyed = arena_destroy(expression_arena, 1);
    BUSTER_CHECK(expression_arena_destroyed);
    return result;
}

UnitTestResult parser_file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    for (u64 i = 0; i < PARSER_FILE_TEST_CASE_COUNT; i += 1)
    {
        u64 position = arena->position;
        ParserFileTestCase test_case = parser_file_test_cases[i];
        FileMapRead source_file = file_map_read(arena, test_case.path, (FileReadOptions){0});
        String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);

        bool file_valid = source.pointer != 0 && source.length != 0;
        BUSTER_TEST(arguments, file_valid);

        if (source.pointer && source.length)
        {
            TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);

            ParserResult parsed = parser_parse(arena, expression_arena, source, tokenizer);
            if (parsed.diagnostic_count != test_case.expected_diagnostic_count)
            {
                for (ParserDiagnostic* diagnostic = parsed.first_diagnostic; diagnostic; diagnostic = diagnostic->next)
                {
                    String8 found = string_from_token_id((TokenIdEnum)diagnostic->found);
                    u32 line = diagnostic->range.line + 1;
                    u32 column = diagnostic->range.column + 1;
                    if (diagnostic->expected == TOKEN_ERROR)
                    {
                        arguments->show(arguments, S8("{S8}:{u32}:{u32}: parser error: {S8}; found {S8}\n"), test_case.path, line, column, diagnostic->message,
                                        found);
                    }
                    else
                    {
                        String8 expected = string_from_token_id((TokenIdEnum)diagnostic->expected);
                        arguments->show(arguments, S8("{S8}:{u32}:{u32}: parser error: {S8}; found {S8}, expected {S8}\n"), test_case.path, line, column,
                                        diagnostic->message, found, expected);
                    }
                }
            }
            BUSTER_TEST(arguments, parsed.diagnostic_count == test_case.expected_diagnostic_count);

            ParserDiagnostic* diagnostic = parsed.first_diagnostic;
            for (u32 diagnostic_index = 0; diagnostic_index < test_case.expected_diagnostic_count; diagnostic_index += 1)
            {
                ParserFileExpectedDiagnostic expected = test_case.expected_diagnostics[diagnostic_index];
                BUSTER_TEST(arguments, diagnostic != 0);
                if (diagnostic)
                {
                    bool matches = diagnostic->kind == expected.kind && diagnostic->found == expected.found && diagnostic->expected == expected.expected &&
                                   string_equal(diagnostic->message, expected.message) && diagnostic->range.line + 1 == expected.line &&
                                   diagnostic->range.column + 1 == expected.column && diagnostic->range.length == expected.length;
                    if (!matches)
                    {
                        arguments->show(arguments, S8("{S8}: diagnostic {u32} mismatch: actual {u32}:{u32} length {u32}, expected {u32}:{u32} length {u32}\n"),
                                        test_case.path, diagnostic_index, diagnostic->range.line + 1, diagnostic->range.column + 1, diagnostic->range.length,
                                        expected.line, expected.column, expected.length);
                    }
                    BUSTER_TEST(arguments, diagnostic->kind == expected.kind);
                    BUSTER_TEST(arguments, diagnostic->found == expected.found);
                    BUSTER_TEST(arguments, diagnostic->expected == expected.expected);
                    BUSTER_STRING_TEST(arguments, diagnostic->message, expected.message);
                    BUSTER_TEST(arguments, diagnostic->range.line + 1 == expected.line);
                    BUSTER_TEST(arguments, diagnostic->range.column + 1 == expected.column);
                    BUSTER_TEST(arguments, diagnostic->range.length == expected.length);
                    diagnostic = diagnostic->next;
                }
            }

            u32 expected_code_count = test_case.expected_code_count || test_case.expected_code_count_is_set ? test_case.expected_code_count : 1;
            BUSTER_TEST(arguments, parsed.code_count == expected_code_count);
            BUSTER_TEST(arguments, parsed.type_declaration_count == test_case.expected_type_declaration_count);
            BUSTER_TEST(arguments, parsed.import_count == test_case.expected_import_count);

            if (test_case.expected_expression.length)
            {
                AstExpression expression = {0};
                AstCode* expression_code = parsed.first_code;
                if (test_case.expression_code_name.length)
                {
                    while (expression_code && !string_equal(expression_code->name, test_case.expression_code_name))
                    {
                        expression_code = expression_code->next;
                    }
                    BUSTER_TEST(arguments, expression_code != 0);
                }

                if (expression_code)
                {
                    AstStatement* last_statement = expression_code->body.last_statement;
                    if (last_statement)
                    {
                        switch (last_statement->id)
                        {
                            break;
                        case AST_STATEMENT_RETURN:
                        {
                            expression = last_statement->return_statement.expression;
                        }
                        break;
                        case AST_STATEMENT_SWITCH:
                        {
                            expression = last_statement->switch_statement.expression;
                        }
                        break;
                        default:
                            os_fail();
                        }
                    }
                }

                String8 actual = ast_expression_to_string(arena, expression);
                BUSTER_STRING_TEST(arguments, actual, test_case.expected_expression);
            }
            if (string_equal(test_case.path, S8("tests/basic_compile_time.bbb")))
            {
                BUSTER_TEST(arguments, parsed.data_declaration_count == 1);
                BUSTER_TEST(arguments, parsed.first_data_declaration && parsed.first_data_declaration->is_compile_time);
                AstCode* compile_time_identity = parsed.first_code;
                BUSTER_TEST(arguments, compile_time_identity && compile_time_identity->type->function.first_argument &&
                                           compile_time_identity->type->function.first_argument->is_compile_time);
                BUSTER_TEST(arguments, compile_time_identity && compile_time_identity->type->function.first_argument &&
                                           compile_time_identity->type->function.first_argument->type->is_compile_time);
            }
            else if (string_equal(test_case.path, S8("tests/basic_variadic.bbb")))
            {
                AstCode* variadic = parsed.first_code;
                BUSTER_TEST(arguments, variadic && variadic->type->function.is_variadic);
                BUSTER_TEST(arguments, variadic && variadic->type->function.argument_count == 1);
                AstStatement* statement = variadic ? variadic->body.first_statement : 0;
                while (statement && statement->id != AST_STATEMENT_DATA)
                {
                    statement = statement->next;
                }
                BUSTER_TEST(arguments, statement != 0);
            }
        }

        file_map_unmap(source_file);
        arena->position = position;
    }

    bool expression_arena_destroyed = arena_destroy(expression_arena, 1);
    BUSTER_CHECK(expression_arena_destroyed);
    return result;
}
#endif
