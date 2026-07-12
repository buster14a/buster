#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/time.h>

#define first_keyword TOKEN_KEYWORD_RETURN
#define last_keyword TOKEN_KEYWORD_UNION

#define pointer_token TOKEN_AMPERSAND
#define array_slice_token_start (TOKEN_LEFT_BRACKET)
#define array_slice_token_end (array_slice_token_start + 1)
BUSTER_GLOBAL_LOCAL TokenId block_end_of_statement_token = TOKEN_SEMICOLON;

#define KEYWORD_COUNT ((u64)last_keyword - (u64)first_keyword + 1)

BUSTER_GLOBAL_LOCAL bool tokenizer_is_ascii_alpha(u8 ch)
{
    bool result = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_ascii_decimal_digit(u8 ch)
{
    bool result = ch >= '0' && ch <= '9';
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_ascii_hex_digit(u8 ch)
{
    bool result =
        tokenizer_is_ascii_decimal_digit(ch) ||
        (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f');
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_identifier_start(u8 ch)
{
    bool result = tokenizer_is_ascii_alpha(ch) || ch == '_';
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_identifier_continue(u8 ch)
{
    bool result = tokenizer_is_identifier_start(ch) || tokenizer_is_ascii_decimal_digit(ch);
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_integer_digit(IntegerFormat format, u8 ch)
{
    bool result = false;
    switch (format)
    {
        break; case INTEGER_FORMAT_HEXADECIMAL:
        {
            result = tokenizer_is_ascii_hex_digit(ch) || ch == '_';
        }
        break; case INTEGER_FORMAT_DECIMAL:
        {
            result = tokenizer_is_ascii_decimal_digit(ch) || ch == '_';
        }
        break; case INTEGER_FORMAT_OCTAL:
        {
            result = (ch >= '0' && ch <= '7') || ch == '_';
        }
        break; case INTEGER_FORMAT_BINARY:
        {
            result = ch == '0' || ch == '1' || ch == '_';
        }
        break; case INTEGER_FORMAT_COUNT: BUSTER_UNREACHABLE();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_real_digit(IntegerFormat format, u8 ch)
{
    bool result = false;
    switch (format)
    {
        break; case INTEGER_FORMAT_HEXADECIMAL:
        {
            result = tokenizer_is_ascii_hex_digit(ch) || ch == '_';
        }
        break; case INTEGER_FORMAT_DECIMAL:
        {
            result = tokenizer_is_ascii_decimal_digit(ch) || ch == '_';
        }
        break; case INTEGER_FORMAT_OCTAL:
        break; case INTEGER_FORMAT_BINARY:
        break; case INTEGER_FORMAT_COUNT: BUSTER_UNREACHABLE();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_utf8_continuation(u8 ch)
{
    bool result = (ch & 0xC0u) == 0x80u;
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_utf8_second_in_range(u8 ch, u8 min, u8 max)
{
    bool result = ch >= min && ch <= max;
    return result;
}

BUSTER_GLOBAL_LOCAL u64 tokenizer_utf8_sequence_length(const char8* restrict it, const char8* restrict top)
{
    u64 result = 1;
    u64 available = (u64)(top - it);
    u8 first = (u8)it[0];

    if (first >= 0xC2u && first <= 0xDFu && available >= 2)
    {
        u8 second = (u8)it[1];
        if (tokenizer_is_utf8_continuation(second))
        {
            result = 2;
        }
    }
    else if (first == 0xE0u && available >= 3)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        if (tokenizer_is_utf8_second_in_range(second, 0xA0u, 0xBFu) && tokenizer_is_utf8_continuation(third))
        {
            result = 3;
        }
    }
    else if (first >= 0xE1u && first <= 0xECu && available >= 3)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        if (tokenizer_is_utf8_continuation(second) && tokenizer_is_utf8_continuation(third))
        {
            result = 3;
        }
    }
    else if (first == 0xEDu && available >= 3)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        if (tokenizer_is_utf8_second_in_range(second, 0x80u, 0x9Fu) && tokenizer_is_utf8_continuation(third))
        {
            result = 3;
        }
    }
    else if (first >= 0xEEu && first <= 0xEFu && available >= 3)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        if (tokenizer_is_utf8_continuation(second) && tokenizer_is_utf8_continuation(third))
        {
            result = 3;
        }
    }
    else if (first == 0xF0u && available >= 4)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        u8 fourth = (u8)it[3];
        if (tokenizer_is_utf8_second_in_range(second, 0x90u, 0xBFu) && tokenizer_is_utf8_continuation(third) && tokenizer_is_utf8_continuation(fourth))
        {
            result = 4;
        }
    }
    else if (first >= 0xF1u && first <= 0xF3u && available >= 4)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        u8 fourth = (u8)it[3];
        if (tokenizer_is_utf8_continuation(second) && tokenizer_is_utf8_continuation(third) && tokenizer_is_utf8_continuation(fourth))
        {
            result = 4;
        }
    }
    else if (first == 0xF4u && available >= 4)
    {
        u8 second = (u8)it[1];
        u8 third = (u8)it[2];
        u8 fourth = (u8)it[3];
        if (tokenizer_is_utf8_second_in_range(second, 0x80u, 0x8Fu) && tokenizer_is_utf8_continuation(third) && tokenizer_is_utf8_continuation(fourth))
        {
            result = 4;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void tokenizer_emit_token(Arena* arena, TokenizerResult* restrict result, Token** restrict token_start, u64* restrict token_count, TokenId id, u64 length)
{
    do
    {
        u32 chunk_length = (u32)BUSTER_MIN(length, (u64)TOKEN_MAX_LENGTH);
        Token* token = arena_allocate(arena, Token, 1);
        if (!*token_start)
        {
            *token_start = token;
        }
        *token = (Token){ .id = id };
        token_length_set(token, chunk_length);
        *token_count += 1;
        length -= chunk_length;

        if (id == TOKEN_ERROR)
        {
            result->error_count += 1;
        }
    } while (length);
}

TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
{
    TokenizerResult result = {0};
    Token* token_start = 0;
    u64 token_count = 0;

    const char8* restrict it = file_pointer;
    const char8* top = file_pointer + file_length;

    while (it < top)
    {
        const char8* restrict start = it;
        u8 start_ch = (u8)*start;
        TokenId id = TOKEN_ERROR;

        switch (start_ch)
        {
            break; BUSTER_SWITCH_ALPHA_UPPER: BUSTER_SWITCH_ALPHA_LOWER:
            case '_':
            {
                while (it < top && tokenizer_is_identifier_continue((u8)*it))
                {
                    it += 1;
                }

                String8 identifier = string_from_pointer_length(start, (u64)(it - start));

                BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
                    [TOKEN_KEYWORD_FUNCTION - first_keyword] = S8_INITIALIZER("fn"),
                    [TOKEN_KEYWORD_IF - first_keyword] = S8_INITIALIZER("if"),
                    [TOKEN_KEYWORD_ELSE - first_keyword] = S8_INITIALIZER("else"),
                    [TOKEN_KEYWORD_RETURN - first_keyword] = S8_INITIALIZER("return"),
                    [TOKEN_KEYWORD_FOR - first_keyword] = S8_INITIALIZER("for"),
                    [TOKEN_KEYWORD_WHILE - first_keyword] = S8_INITIALIZER("while"),
                    [TOKEN_KEYWORD_CODE - first_keyword] = S8_INITIALIZER("code"),
                    [TOKEN_KEYWORD_DATA - first_keyword] = S8_INITIALIZER("data"),
                    [TOKEN_KEYWORD_TYPE - first_keyword] = S8_INITIALIZER("type"),
                    [TOKEN_KEYWORD_STRUCT - first_keyword] = S8_INITIALIZER("struct"),
                    [TOKEN_KEYWORD_UNION - first_keyword] = S8_INITIALIZER("union"),
                };

                BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(keyword_names) == KEYWORD_COUNT);

                u64 i;
                for (i = 0; i < KEYWORD_COUNT; i += 1)
                {
                    if (string_equal(identifier, keyword_names[i]))
                    {
                        break;
                    }
                }

                if (i == KEYWORD_COUNT)
                {
                    id = identifier.length == 1 && identifier.pointer[0] == '_' ? TOKEN_UNDERSCORE : TOKEN_IDENTIFIER;
                }
                else
                {
                    id = (TokenId)(i + (u64)first_keyword);
                }
            }
            break; case ' ':
            {
                id = TOKEN_SPACE;

                while (it < top && *it == ' ')
                {
                    it += 1;
                }
            }
            break; case '\t':
            {
                id = TOKEN_TAB;

                while (it < top && *it == '\t')
                {
                    it += 1;
                }
            }
            break; BUSTER_SWITCH_DECIMAL_DIGIT:
            {
                bool is_valid = true;
                bool has_integer_digit = false;
                IntegerFormat format = INTEGER_FORMAT_DECIMAL;

                if (start_ch == '0' && it + 1 < top)
                {
                    u8 second_ch = (u8)it[1];
                    switch (second_ch)
                    {
                        break; case 'x': it += 2; format = INTEGER_FORMAT_HEXADECIMAL;
                        break; case 'o': it += 2; format = INTEGER_FORMAT_OCTAL;
                        break; case 'b': it += 2; format = INTEGER_FORMAT_BINARY;
                        break; default: {}
                    }
                }

                while (it < top && tokenizer_is_integer_digit(format, (u8)*it))
                {
                    has_integer_digit = has_integer_digit || *it != '_';
                    it += 1;
                }

                is_valid = is_valid && (format == INTEGER_FORMAT_DECIMAL || has_integer_digit);

                bool is_float = (format == INTEGER_FORMAT_DECIMAL || format == INTEGER_FORMAT_HEXADECIMAL) && it < top && *it == '.' && !(it + 1 < top && it[1] == '.');

                if (is_float)
                {
                    it += 1;

                    while (it < top && tokenizer_is_real_digit(format, (u8)*it))
                    {
                        it += 1;
                    }

                    u8 exponent_ch = it < top ? (u8)*it : 0;

                    switch (exponent_ch)
                    {
                        break;
                        case 'E':
                        case 'e':
                        case 'P':
                        case 'p':
                        {
                            switch (format)
                            {
                                break; case INTEGER_FORMAT_HEXADECIMAL:
                                {
                                    id = TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT;
                                    is_valid = is_valid && (exponent_ch == 'P' || exponent_ch == 'p');
                                }
                                break; case INTEGER_FORMAT_DECIMAL:
                                {
                                    id = TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT;
                                    is_valid = is_valid && (exponent_ch == 'E' || exponent_ch == 'e');
                                }
                                break; default: BUSTER_UNREACHABLE();
                            }

                            it += 1;

                            u8 exponent_sign = it < top ? (u8)*it : 0;
                            bool has_exponent_sign = exponent_sign == '+' || exponent_sign == '-';
                            is_valid = is_valid && has_exponent_sign;
                            it += has_exponent_sign;

                            bool has_exponent_digit = false;
                            while (it < top && (tokenizer_is_ascii_decimal_digit((u8)*it) || *it == '_'))
                            {
                                has_exponent_digit = has_exponent_digit || *it != '_';
                                it += 1;
                            }
                            is_valid = is_valid && has_exponent_digit;
                        }
                        break; default:
                        {
                            switch (format)
                            {
                                break; case INTEGER_FORMAT_HEXADECIMAL: id = TOKEN_HEXADECIMAL_FLOAT_LITERAL;
                                break; case INTEGER_FORMAT_DECIMAL: id = TOKEN_DECIMAL_FLOAT_LITERAL;
                                break; default: BUSTER_UNREACHABLE();
                            }
                        }
                    }
                }
                else
                {
                    switch (format)
                    {
                        break; case INTEGER_FORMAT_HEXADECIMAL: id = TOKEN_HEXADECIMAL_INTEGER_LITERAL;
                        break; case INTEGER_FORMAT_DECIMAL: id = TOKEN_DECIMAL_INTEGER_LITERAL;
                        break; case INTEGER_FORMAT_OCTAL: id = TOKEN_OCTAL_INTEGER_LITERAL;
                        break; case INTEGER_FORMAT_BINARY: id = TOKEN_BINARY_INTEGER_LITERAL;
                        break; case INTEGER_FORMAT_COUNT: BUSTER_UNREACHABLE();
                    }
                }

                id = is_valid ? id : TOKEN_ERROR;
            }
            break; case '\n': { id = TOKEN_LINE_FEED; it += 1; }
            break; case '\r': { id = TOKEN_CARRIAGE_RETURN; it += 1; }
            break; case '[': { id = TOKEN_LEFT_BRACKET; it += 1; }
            break; case ']': { id = TOKEN_RIGHT_BRACKET; it += 1; }
            break; case '{': { id = TOKEN_LEFT_BRACE; it += 1; }
            break; case '}': { id = TOKEN_RIGHT_BRACE; it += 1; }
            break; case '(': { id = TOKEN_LEFT_PARENTHESIS; it += 1; }
            break; case ')': { id = TOKEN_RIGHT_PARENTHESIS; it += 1; }
            break; case '<':
            {
                if (it + 1 < top && it[1] == '<')
                {
                    id = TOKEN_SHIFT_LEFT;
                    it += 2;
                }
                else if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_LESS_EQUAL;
                    it += 2;
                }
                else
                {
                    id = TOKEN_LESS;
                    it += 1;
                }
            }
            break; case '>':
            {
                if (it + 1 < top && it[1] == '>')
                {
                    id = TOKEN_SHIFT_RIGHT;
                    it += 2;
                }
                else if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_GREATER_EQUAL;
                    it += 2;
                }
                else
                {
                    id = TOKEN_GREATER;
                    it += 1;
                }
            }
            break; case '+':
            {
                if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_PLUS_EQUAL;
                    it += 2;
                }
                else
                {
                    id = TOKEN_PLUS;
                    it += 1;
                }
            }
            break; case '-': { id = TOKEN_MINUS; it += 1; }
            break; case '*': { id = TOKEN_ASTERISK; it += 1; }
            break; case '=':
            {
                if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_EQUAL_EQUAL;
                    it += 2;
                }
                else
                {
                    id = TOKEN_EQUAL;
                    it += 1;
                }
            }
            break; case '!':
            {
                if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_BANG_EQUAL;
                    it += 2;
                }
                else
                {
                    id = TOKEN_BANG;
                    it += 1;
                }
            }
            break; case ':': { id = TOKEN_COLON; it += 1; }
            break; case ';': { id = TOKEN_SEMICOLON; it += 1; }
            break; case ',': { id = TOKEN_COMMA; it += 1; }
            break; case '&': { id = TOKEN_AMPERSAND; it += 1; }
            break; case '%': { id = TOKEN_PERCENTAGE; it += 1; }
            break; case '|': { id = TOKEN_BAR; it += 1; }
            break; case '^': { id = TOKEN_CARET; it += 1; }
            break; case '~': { id = TOKEN_TILDE; it += 1; }
            break; case '/':
            {
                if (it + 1 < top && it[1] == '/')
                {
                    id = TOKEN_COMMENT;
                    it += 2;
                    while (it < top && *it != '\n' && *it != '\r')
                    {
                        it += 1;
                    }
                }
                else
                {
                    id = TOKEN_SLASH;
                    it += 1;
                }
            }
            break; case '.':
            {
                if (it + 2 < top && it[1] == '.' && it[2] == '.')
                {
                    id = TOKEN_TRIPLE_DOT;
                }
                else if (it + 1 < top && it[1] == '.')
                {
                    id = TOKEN_DOUBLE_DOT;
                }
                else
                {
                    id = TOKEN_DOT;
                }

                it += (u64)id - (u64)TOKEN_DOT + 1;
            }
            break; default:
            {
                it += tokenizer_utf8_sequence_length(it, top);
            }
        }

        const char8* restrict end = it;
        u64 length = (u64)(end - start);
        tokenizer_emit_token(arena, &result, &token_start, &token_count, id, length);
    }

    tokenizer_emit_token(arena, &result, &token_start, &token_count, TOKEN_EOF, 0);

    result.tokens = token_start;

    if (token_count > UINT32_MAX)
    {
        BUSTER_TODO();
    }

    result.token_count = (u32)token_count;
    return result;
}

String8 get_token_content(const char8* source, Token* restrict tokens, u32 lexer_token_index)
{
    u64 offset = 0;
    for (u32 i = 0; i < lexer_token_index; i += 1)
    {
        offset += token_length_get(&tokens[i]);
    }
    return (String8){
        .pointer = (char8*)source + offset,
        .length = token_length_get(&tokens[lexer_token_index]),
    };
}

typedef enum ParserDeclaration
{
    PARSER_DECLARATION_ROOT,
    PARSER_DECLARATION_CODE,
    PARSER_DECLARATION_TYPE_REFERENCE,
    PARSER_DECLARATION_ATTRIBUTE_LIST,
    PARSER_DECLARATION_BLOCK,
    PARSER_DECLARATION_STATEMENT,
    PARSER_DECLARATION_RETURN_STATEMENT,
    PARSER_DECLARATION_TYPE_DECLARATION,
    PARSER_DECLARATION_DATA_DECLARATION,
    PARSER_DECLARATION_EXPRESSION,
    PARSER_DECLARATION_UNARY_PREFIX,
    PARSER_DECLARATION_COUNT,
}ParserDeclaration;

typedef enum CodeState
{
    CODE_STATE_BEFORE_NAME,
    CODE_STATE_AFTER_NAME,
    CODE_STATE_TYPE,
    CODE_STATE_AFTER_TYPE,
    CODE_STATE_AFTER_EQUAL,
    CODE_STATE_BODY,
    CODE_STATE_COUNT,
} CodeState;

typedef enum TypeState
{
    TYPE_STATE_PREFIX_OR_BASE,
    TYPE_STATE_AFTER_ARRAY_SLICE_START,
    TYPE_STATE_AFTER_ARRAY_COUNT,
    TYPE_STATE_AFTER_ARRAY_INFER_MARKER,
    TYPE_STATE_AFTER_FUNCTION_KEYWORD,
    TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE,
    TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT,
    TYPE_STATE_FUNCTION_ARGUMENT_AFTER_COLON,
    TYPE_STATE_FUNCTION_ARGUMENT_TYPE,
    TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE,
    TYPE_STATE_FUNCTION_RETURN_TYPE,
    TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE,
    TYPE_STATE_COUNT,
} TypeState;

typedef enum AttributeListKind
{
    ATTRIBUTE_LIST_CODE,
    ATTRIBUTE_LIST_DATA,
    ATTRIBUTE_LIST_SYMBOL,
    ATTRIBUTE_LIST_FUNCTION,
    ATTRIBUTE_LIST_COUNT,
} AttributeListKind;
// TODO add more

typedef enum AttributeListState
{
    ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE,
    ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN,
    ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME,
    ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE,
    ATTRIBUTE_LIST_STATE_COUNT,
} AttributeListState;

typedef enum StatementStateId
{
        STATEMENT_STATE_START,
        STATEMENT_STATE_END,
        STATEMENT_STATE_COUNT,
} StatementStateId;

typedef enum ReturnStatementState
{
    RETURN_STATEMENT_STATE_VALUE_OR_END,
    RETURN_STATEMENT_STATE_END,
    RETURN_STATEMENT_STATE_COUNT,
} ReturnStatementState;

typedef enum ExpressionState
{
    EXPRESSION_STATE_PREFIX,
    EXPRESSION_STATE_TAIL,
    EXPRESSION_STATE_COUNT,
} ExpressionState;

typedef enum BindingPower
{
    BINDING_POWER_BITWISE,
    BINDING_POWER_EQUALITY,
    BINDING_POWER_RELATIONAL,
    BINDING_POWER_SHIFT,
    BINDING_POWER_ADD,
    BINDING_POWER_MULTIPLY,
    BINDING_POWER_COUNT,
} BindingPower;

typedef struct ParserState ParserState;
struct ParserState
{
    ParserDeclaration id;
    union
    {
        struct
        {
            CodeState current_state;
            AstCode* code;
        } code;

        struct
        {
            TypeState current_state;
            AstCode* code;
            AstType** destination;
            AstType* root;
            AstType* type;
            AstTypeArgument* argument;
            ParserSourceRange name_range;
            ParserSourceRange prefix_range;
        } type;

        struct
        {
            AstCode* code;
            AstType* type;
            AttributeListKind kind;
            AttributeListState current_state;
        } attribute_list;

        struct
        {
            AstBlock* block;
            u32 brace_depth;
        } block;

        struct
        {
            StatementStateId state;
            AstStatement* statement;
            TokenId end_token;
        } statement;

        struct
        {
            ReturnStatementState state;
            AstStatement* statement;
            TokenId end_token;
        } return_statement;

        struct
        {
            ExpressionState state;
            TokenId end_token;
            // Postorder (RPN) output stream this expression emits into. The tree
            // is implicit in this ordering plus each node's arity, so no child
            // links are stored. `output_base` is the first emitted node.
            AstExpressionNode* output_base;
            u32 output_count;
            // Shunting-yard stack holding binary operator kinds (AstNodeId cast
            // to u8) not yet emitted; operators are held until precedence
            // resolves.
            u16 operator_count;
            // Depth of the per-expression binary-operator shunting-yard stack. Left-
            // associative binary operators only ever stack strictly-increasing binding
            // powers, so this is bounded by the number of precedence levels (one push per
            // level, at most) and checked. Prefix unary operators are not stacked here:
            // each pending unary operator is its own arena-backed ParserState frame, so
            // prefix runs are unbounded.
            u8 operator_stack[BINDING_POWER_COUNT];
        } expression;

        // One pending prefix unary operator (`-`, `+`, `!`, `~`). A run of
        // prefix operators is a run of these frames on the state stack, popped
        // innermost-first once the operand has been emitted. The id is an
        // AstNodeId stored as u8 because that enum is declared later.
        struct
        {
            u8 id;
        } unary_prefix;
    };
};

typedef struct ExtendedToken ExtendedToken;
struct ExtendedToken
{
    TokenId id;
    u8 reserved[3];
    u32 column;
    u32 length;
    u32 line;
    u32 offset;
};

BUSTER_CT_CHECK(sizeof(ExtendedToken) == 20);

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token)
{
    bool has_fake_length = token->id == TOKEN_EOF;
    u32 length = has_fake_length ? 0 : token_length_get(token);
    return length;
}

typedef struct ParserStateState ParserStateState;
struct ParserStateState
{
    Arena* arena;
    u64 minimum_position;
};


BUSTER_GLOBAL_LOCAL bool state_is_empty(ParserStateState* stack)
{
    return stack->arena->position < stack->minimum_position + sizeof(ParserState);
}

BUSTER_GLOBAL_LOCAL ParserState* state_top(ParserStateState* stack)
{
    return arena_get_pointer_at_position(stack->arena, ParserState, stack->arena->position - sizeof(ParserState));
}

BUSTER_GLOBAL_LOCAL ParserState* state_previous(ParserStateState* stack)
{
    BUSTER_CHECK(stack->arena->position >= stack->minimum_position + 2 * sizeof(ParserState));
    return arena_get_pointer_at_position(stack->arena, ParserState, stack->arena->position - 2 * sizeof(ParserState));
}

BUSTER_GLOBAL_LOCAL ParserState state_pop(ParserStateState* stack)
{
    BUSTER_CHECK(!state_is_empty(stack));
    ParserState old_top = *state_top(stack);
    stack->arena->position -= sizeof(ParserState);
    return old_top;
}

BUSTER_GLOBAL_LOCAL ParserState* state_push(ParserStateState* stack)
{
    ParserState* state = arena_allocate(stack->arena, ParserState, 1);
    *state = (ParserState){0};
    return state;
}

typedef struct TokenIterator TokenIterator;
struct TokenIterator
{
    Token* tokens;
    u32 token_count;
    const char8* source;
    u32 token_index;
    u32 line_index;
    u32 line_offset;
    u32 column_index;
};

BUSTER_GLOBAL_LOCAL TokenIterator token_initialize(Token* tokens, u32 token_count, const char8* source)
{
    TokenIterator result = {
        .tokens = tokens,
        .token_count = token_count,
        .source = source,
    };

    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken to_extended_token(TokenIterator* restrict iterator, Token token)
{
    u32 token_length = token_length_get(&token);
    ExtendedToken result = {
        .id = token.id,
        .column = iterator->column_index,
        .length = token_length,
        .line = iterator->line_index,
        .offset = iterator->line_offset + iterator->column_index,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken token_get(TokenIterator* restrict iterator)
{
    BUSTER_CHECK(iterator->token_index < iterator->token_count);
    Token* restrict token = &iterator->tokens[iterator->token_index];
    ExtendedToken result = to_extended_token(iterator, *token);
    return result;
}

typedef struct Parser Parser;
typedef enum ParserRecoveryKind
{
    PARSER_RECOVERY_NONE,
    PARSER_RECOVERY_STATEMENT,
    PARSER_RECOVERY_DECLARATION,
} ParserRecoveryKind;

struct Parser
{
    TokenIterator iterator;
    ParserStateState state;
    Arena* restrict result_arena;
    Arena* restrict expression_arena;
    u64 expression_arena_minimum_position;
    ParserResult* result;
    ParserRecoveryKind recovery;
};

BUSTER_GLOBAL_LOCAL void consume(TokenIterator* restrict iterator);
BUSTER_GLOBAL_LOCAL ExtendedToken peek(Parser* restrict parser);
BUSTER_GLOBAL_LOCAL ParserState* state(Parser* parser);

BUSTER_GLOBAL_LOCAL ParserSourceRange source_range_from_token(ExtendedToken token)
{
    return (ParserSourceRange){
        .offset = token.offset,
        .length = token.length,
        .line = token.line,
        .column = token.column,
    };
}

BUSTER_GLOBAL_LOCAL AstCode* parser_code_push(Parser* parser, ExtendedToken token)
{
    AstCode* code = arena_allocate(parser->result_arena, AstCode, 1);
    *code = (AstCode){
        .range = source_range_from_token(token),
    };
    if (parser->result->last_code)
    {
        parser->result->last_code->next = code;
    }
    else
    {
        parser->result->first_code = code;
    }
    parser->result->last_code = code;
    parser->result->code_count += 1;
    return code;
}

BUSTER_GLOBAL_LOCAL AstType* parser_type_push(Parser* parser, AstTypeId id, ExtendedToken token)
{
    AstType* type = arena_allocate(parser->result_arena, AstType, 1);
    *type = (AstType){
        .range = source_range_from_token(token),
        .id = id,
    };
    if (id == AST_TYPE_FUNCTION)
    {
        type->function.calling_convention = AST_CALLING_CONVENTION_C;
    }
    return type;
}

BUSTER_GLOBAL_LOCAL AstType* parser_type_attach(Parser* parser, ParserState* type_state, AstTypeId id, ExtendedToken token)
{
    BUSTER_CHECK(type_state->id == PARSER_DECLARATION_TYPE_REFERENCE);
    BUSTER_CHECK(type_state->type.destination);
    AstType* type = parser_type_push(parser, id, token);
    *type_state->type.destination = type;
    if (!type_state->type.root)
    {
        type_state->type.root = type;
    }
    return type;
}

BUSTER_GLOBAL_LOCAL AstTypeArgument* parser_type_argument_push(Parser* parser, AstType* function_type, ExtendedToken token)
{
    BUSTER_CHECK(function_type && function_type->id == AST_TYPE_FUNCTION);
    AstTypeArgument* argument = arena_allocate(parser->result_arena, AstTypeArgument, 1);
    *argument = (AstTypeArgument){ .range = source_range_from_token(token) };
    if (function_type->function.last_argument)
    {
        function_type->function.last_argument->next = argument;
    }
    else
    {
        function_type->function.first_argument = argument;
    }
    function_type->function.last_argument = argument;
    function_type->function.argument_count += 1;
    return argument;
}

BUSTER_GLOBAL_LOCAL AstStatement* parser_statement_push(Parser* parser, AstBlock* block, AstStatementId id, ExtendedToken token)
{
    AstStatement* statement = arena_allocate(parser->result_arena, AstStatement, 1);
    *statement = (AstStatement){
        .range = source_range_from_token(token),
        .id = id,
    };
    if (block->last_statement)
    {
        block->last_statement->next = statement;
    }
    else
    {
        block->first_statement = statement;
    }
    block->last_statement = statement;
    block->statement_count += 1;
    return statement;
}

BUSTER_GLOBAL_LOCAL ParserDiagnostic* parser_diagnostic_push(Parser* parser, ParserDiagnosticKind kind, ExtendedToken token, TokenId expected, String8 message)
{
    ParserDiagnostic* diagnostic = arena_allocate(parser->result_arena, ParserDiagnostic, 1);
    *diagnostic = (ParserDiagnostic){
        .message = message,
        .range = source_range_from_token(token),
        .kind = kind,
        .found = token.id,
        .expected = expected,
    };
    if (parser->result->last_diagnostic)
    {
        parser->result->last_diagnostic->next = diagnostic;
    }
    else
    {
        parser->result->first_diagnostic = diagnostic;
    }
    parser->result->last_diagnostic = diagnostic;
    parser->result->diagnostic_count += 1;
    return diagnostic;
}

BUSTER_GLOBAL_LOCAL void parser_unexpected(Parser* parser, ExtendedToken token, TokenId expected)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN, token, expected, S8("unexpected token"));
    ParserDeclaration declaration = state(parser)->id;
    if (declaration == PARSER_DECLARATION_STATEMENT || declaration == PARSER_DECLARATION_RETURN_STATEMENT ||
        declaration == PARSER_DECLARATION_EXPRESSION || declaration == PARSER_DECLARATION_UNARY_PREFIX)
    {
        parser->recovery = PARSER_RECOVERY_STATEMENT;
    }
    else
    {
        parser->recovery = PARSER_RECOVERY_DECLARATION;
    }
}

BUSTER_GLOBAL_LOCAL void parser_recover(Parser* parser)
{
    if (parser->recovery == PARSER_RECOVERY_STATEMENT)
    {
        AstStatement* statement = 0;
        while (state(parser)->id != PARSER_DECLARATION_BLOCK && state(parser)->id != PARSER_DECLARATION_ROOT)
        {
            ParserState popped = state_pop(&parser->state);
            if (popped.id == PARSER_DECLARATION_STATEMENT) { statement = popped.statement.statement; }
            else if (popped.id == PARSER_DECLARATION_RETURN_STATEMENT) { statement = popped.return_statement.statement; }
        }

        ExtendedToken token = peek(parser);
        while (token.id != TOKEN_SEMICOLON && token.id != TOKEN_RIGHT_BRACE && token.id != TOKEN_KEYWORD_CODE && token.id != TOKEN_EOF)
        {
            consume(&parser->iterator);
            token = peek(parser);
        }
        if (token.id == TOKEN_SEMICOLON)
        {
            consume(&parser->iterator);
            if (statement)
            {
                statement->range.length = token.offset + token.length - statement->range.offset;
            }
        }
        else if (token.id == TOKEN_KEYWORD_CODE || token.id == TOKEN_EOF)
        {
            parser->recovery = PARSER_RECOVERY_DECLARATION;
        }
        else
        {
            parser->recovery = PARSER_RECOVERY_NONE;
            return;
        }
    }

    if (parser->recovery == PARSER_RECOVERY_DECLARATION)
    {
        arena_set_position(parser->state.arena, parser->state.minimum_position);
        state_push(&parser->state);

        ExtendedToken token = peek(parser);
        while (token.id != TOKEN_KEYWORD_CODE && token.id != TOKEN_EOF)
        {
            consume(&parser->iterator);
            token = peek(parser);
        }
    }
    parser->recovery = PARSER_RECOVERY_NONE;
}

typedef struct IntegerLiteralParsing IntegerLiteralParsing;
struct IntegerLiteralParsing
{
    u64 value;
    u8 base;
    bool valid;
    bool fits_u64;
};

BUSTER_GLOBAL_LOCAL IntegerLiteralParsing parse_integer_literal(String8 string, TokenId id)
{
    IntegerLiteralParsing result = { .valid = true, .fits_u64 = true };
    u32 base = 10;
    u64 index = 0;
    if (id == TOKEN_HEXADECIMAL_INTEGER_LITERAL) { base = 16; index = 2; }
    else if (id == TOKEN_OCTAL_INTEGER_LITERAL) { base = 8; index = 2; }
    else if (id == TOKEN_BINARY_INTEGER_LITERAL) { base = 2; index = 2; }
    result.base = (u8)base;

    bool previous_was_digit = false;
    bool has_digit = false;
    for (; index < string.length; index += 1)
    {
        u8 ch = (u8)string.pointer[index];
        if (ch == '_')
        {
            if (!previous_was_digit || index + 1 == string.length)
            {
                result.valid = false;
            }
            previous_was_digit = false;
            continue;
        }

        u32 digit;
        if (ch >= '0' && ch <= '9') { digit = (u32)(ch - '0'); }
        else if (ch >= 'A' && ch <= 'F') { digit = (u32)(ch - 'A') + 10; }
        else if (ch >= 'a' && ch <= 'f') { digit = (u32)(ch - 'a') + 10; }
        else { result.valid = false; break; }

        if (digit >= base)
        {
            result.valid = false;
            break;
        }
        if (result.value > (UINT64_MAX - digit) / base)
        {
            result.fits_u64 = false;
            result.value = 0;
        }
        else if (result.fits_u64)
        {
            result.value = result.value * base + digit;
        }
        previous_was_digit = true;
        has_digit = true;
    }
    result.valid = result.valid && has_digit;
    return result;
}

BUSTER_GLOBAL_LOCAL void consume_token(TokenIterator* restrict iterator, Token* restrict token)
{
    if (token->id == TOKEN_EOF)
    {
        return;
    }

    bool is_line_feed = token->id == TOKEN_LINE_FEED;
    iterator->line_index += is_line_feed;
    iterator->line_offset = is_line_feed ? iterator->line_offset + iterator->column_index + 1 : iterator->line_offset;
    iterator->column_index = is_line_feed ? 0 : get_token_length(token) + iterator->column_index;
    iterator->token_index += 1;
}

BUSTER_GLOBAL_LOCAL void consume(TokenIterator* restrict iterator)
{
    BUSTER_CHECK(iterator->token_index < iterator->token_count);
    consume_token(iterator, &iterator->tokens[iterator->token_index]);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_extended(Parser* restrict parser, bool consume_result)
{
    ExtendedToken result;
    bool is_noise = true;

    do
    {
        result = token_get(&parser->iterator);
        TokenId id = result.id;
        is_noise =
            id == TOKEN_LINE_FEED ||
            id == TOKEN_TAB ||
            id == TOKEN_SPACE ||
            id == TOKEN_COMMENT ||
            id == TOKEN_CARRIAGE_RETURN;

        if (is_noise || consume_result)
        {
            consume(&parser->iterator);
        }
    } while (is_noise);

    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek(Parser* restrict parser)
{
    return peek_extended(parser, false);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_and_consume(Parser* restrict parser)
{
    return peek_extended(parser, true);
}

BUSTER_GLOBAL_LOCAL String8 get_string(const char8* source, ExtendedToken token)
{
    String8 result = { .pointer = (char8*)&source[token.offset], .length = token.length };
    return result;
}

// BUSTER_GLOBAL_LOCAL String8 get_string(Parser& parser, ExtendedToken token)
// {
//     String8 result = {};
//     if (token.length)
//     {
//         result = { .pointer = (char8*)&parser.source[token.offset], .length = token.length };
//     }
//     return result;
// }

BUSTER_GLOBAL_LOCAL ExtendedToken expect(Parser* parser, TokenId id)
{
    ExtendedToken token = peek_and_consume(parser);

    if (token.id != id)
    {
        String8 string = get_string(parser->iterator.source, token);
        BUSTER_UNUSED(string);
        BUSTER_TODO();
    }

    return token;
}

BUSTER_GLOBAL_LOCAL ParserState* state(Parser* parser)
{
    return state_top(&parser->state);
}

BUSTER_GLOBAL_LOCAL bool token_matches(Parser* parser, ExtendedToken token, String8 expected)
{
    bool result = false;
    if (token.id == TOKEN_IDENTIFIER)
    {
        result = string_equal(get_string(parser->iterator.source, token), expected);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool token_matches_any(Parser* parser, ExtendedToken token, String8* names, u64 name_count)
{
    bool result = false;
    if (token.id == TOKEN_IDENTIFIER)
    {
        String8 candidate = get_string(parser->iterator.source, token);
        for (u64 i = 0; i < name_count; i += 1)
        {
            if (string_equal(candidate, names[i]))
            {
                result = true;
                break;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool token_begins_type(TokenId id)
{
    bool result =
        id == TOKEN_IDENTIFIER ||
        id == pointer_token ||
        id == array_slice_token_start ||
        id == TOKEN_KEYWORD_FUNCTION;
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_ahead(Parser* parser, u32 count)
{
    Parser copy = *parser;
    ExtendedToken result = {0};

    for (u32 i = 0; i <= count; i += 1)
    {
        result = peek_and_consume(&copy);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void parser_source_range_set_end(ParserSourceRange* range, u32 end_offset)
{
    BUSTER_CHECK(end_offset >= range->offset);
    range->length = end_offset - range->offset;
}

BUSTER_GLOBAL_LOCAL void finish_type_ranges(AstType* type, u32 end_offset)
{
    while (type)
    {
        parser_source_range_set_end(&type->range, end_offset);
        switch (type->id)
        {
            break; case AST_TYPE_POINTER:
            case AST_TYPE_SLICE:
            case AST_TYPE_INFERRED_ARRAY:
            {
                type = type->element_type;
            }
            break; default:
            {
                type = 0;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void finish_type_reference(Parser* parser, u32 end_offset)
{
    ParserState completed_state = state_pop(&parser->state);
    if (completed_state.type.root)
    {
        finish_type_ranges(completed_state.type.root, end_offset);
    }

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_DECLARATION_CODE:
        {
            if (resume_state->code.current_state != CODE_STATE_TYPE)
            {
                BUSTER_TODO();
            }

            resume_state->code.current_state = CODE_STATE_AFTER_TYPE;
        }
        break; case PARSER_DECLARATION_TYPE_REFERENCE:
        {
            switch (resume_state->type.current_state)
            {
                break; case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
                {
                    BUSTER_CHECK(resume_state->type.argument);
                    parser_source_range_set_end(&resume_state->type.argument->range, end_offset);
                    resume_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE;
                }
                break; case TYPE_STATE_FUNCTION_RETURN_TYPE:
                {
                    resume_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE;
                }
                break; default: BUSTER_TODO();
            }
        }
        break; default:
        {
        }
    }
}

typedef enum FunctionAttribute
{
    FUNCTION_ATTRIBUTE_CALLING_CONVENTION,
    FUNCTION_ATTRIBUTE_COUNT,
} FunctionAttribute;

BUSTER_GLOBAL_LOCAL String8 function_attribute_names[] = {
    [FUNCTION_ATTRIBUTE_CALLING_CONVENTION] = S8_INITIALIZER("cc"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(function_attribute_names) == FUNCTION_ATTRIBUTE_COUNT);

BUSTER_GLOBAL_LOCAL String8 calling_convention_names[] = {
    [AST_CALLING_CONVENTION_C] = S8_INITIALIZER("c"),
    [AST_CALLING_CONVENTION_SYSTEMV] = S8_INITIALIZER("systemv"),
    [AST_CALLING_CONVENTION_WIN64] = S8_INITIALIZER("win64"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(calling_convention_names) == AST_CALLING_CONVENTION_COUNT);

typedef enum SymbolAttribute
{
    SYMBOL_ATTRIBUTE_EXPORT,
    SYMBOL_ATTRIBUTE_COUNT,
} SymbolAttribute;

BUSTER_GLOBAL_LOCAL String8 symbol_attribute_names[] = {
    [SYMBOL_ATTRIBUTE_EXPORT] = S8_INITIALIZER("export"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(symbol_attribute_names) == SYMBOL_ATTRIBUTE_COUNT);

BUSTER_GLOBAL_LOCAL String8 string_from_node_id(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_CONSTANT_INTEGER: return S8("ConstantInteger");
        case AST_NODE_UNARY_MINUS: return S8("UnaryMinus");
        case AST_NODE_UNARY_PLUS: return S8("UnaryPlus");
        case AST_NODE_UNARY_LOGICAL_NOT: return S8("UnaryLogicalNot");
        case AST_NODE_UNARY_BITWISE_NOT: return S8("UnaryBitwiseNot");
        case AST_NODE_BINARY_PLUS: return S8("BinaryPlus");
        case AST_NODE_BINARY_MINUS: return S8("BinaryMinus");
        case AST_NODE_BINARY_ASTERISK: return S8("BinaryAsterisk");
        case AST_NODE_BINARY_SLASH: return S8("BinarySlash");
        case AST_NODE_BINARY_PERCENT: return S8("BinaryPercent");
        case AST_NODE_BINARY_SHIFT_LEFT: return S8("BinaryShiftLeft");
        case AST_NODE_BINARY_SHIFT_RIGHT: return S8("BinaryShiftRight");
        case AST_NODE_BINARY_EQUAL: return S8("BinaryEqual");
        case AST_NODE_BINARY_NOT_EQUAL: return S8("BinaryNotEqual");
        case AST_NODE_BINARY_LESS: return S8("BinaryLess");
        case AST_NODE_BINARY_LESS_EQUAL: return S8("BinaryLessEqual");
        case AST_NODE_BINARY_GREATER: return S8("BinaryGreater");
        case AST_NODE_BINARY_GREATER_EQUAL: return S8("BinaryGreaterEqual");
        case AST_NODE_BINARY_AMPERSAND: return S8("BinaryAmpersand");
        case AST_NODE_BINARY_BAR: return S8("BinaryBar");
        case AST_NODE_BINARY_CARET: return S8("BinaryCaret");
        case AST_NODE_COUNT:
        {
        }
    }

    BUSTER_UNREACHABLE();
}

typedef enum CodeAttributeId
{
    CODE_ATTRIBUTE_INLINE,
    CODE_ATTRIBUTE_COUNT,
} CodeAttributeId;

BUSTER_GLOBAL_LOCAL String8 code_attributes_names[] = {
    [CODE_ATTRIBUTE_INLINE] = S8_INITIALIZER("inline"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(code_attributes_names) == CODE_ATTRIBUTE_COUNT);

// A node in the flattened, postorder expression stream. The tree is *implicit*:
// a node's operands are the subtrees emitted immediately before it, and its
// arity is fixed by its kind (see ast_node_arity), so no child links are stored.
// Because the whole expression is one contiguous array in evaluation order,
// analysis and typechecking stream through it front-to-back with a small operand
// stack — sequential access, branch-predictable, no pointer chasing.
// Left binding power of a binary operator: higher binds tighter.
BUSTER_GLOBAL_LOCAL BindingPower binary_binding_power(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_BINARY_AMPERSAND:
        case AST_NODE_BINARY_BAR:
        case AST_NODE_BINARY_CARET:
        {
            return BINDING_POWER_BITWISE;
        }
        case AST_NODE_BINARY_EQUAL:
        case AST_NODE_BINARY_NOT_EQUAL:
        {
            return BINDING_POWER_EQUALITY;
        }
        case AST_NODE_BINARY_LESS:
        case AST_NODE_BINARY_LESS_EQUAL:
        case AST_NODE_BINARY_GREATER:
        case AST_NODE_BINARY_GREATER_EQUAL:
        {
            return BINDING_POWER_RELATIONAL;
        }
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
        {
            return BINDING_POWER_SHIFT;
        }
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
        {
            return BINDING_POWER_ADD;
        }
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        {
            return BINDING_POWER_MULTIPLY;
        }
        case AST_NODE_COUNT:
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
        case AST_NODE_CONSTANT_INTEGER:
        {
        }
    }

    BUSTER_UNREACHABLE();
}

// Number of operand subtrees a node consumes. Drives the implicit-tree walk:
// leaves push, unary pops 1, binary pops 2.
BUSTER_GLOBAL_LOCAL u32 ast_node_arity(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_CONSTANT_INTEGER:
        {
            return 0;
        }
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
        case AST_NODE_UNARY_LOGICAL_NOT:
        case AST_NODE_UNARY_BITWISE_NOT:
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
        case AST_NODE_UNARY_MINUS: return S8("neg");
        case AST_NODE_UNARY_PLUS: return S8("pos");
        case AST_NODE_UNARY_LOGICAL_NOT: return S8("not");
        case AST_NODE_UNARY_BITWISE_NOT: return S8("bit_not");
        case AST_NODE_BINARY_PLUS: return S8("+");
        case AST_NODE_BINARY_MINUS: return S8("-");
        case AST_NODE_BINARY_ASTERISK: return S8("*");
        case AST_NODE_BINARY_SLASH: return S8("/");
        case AST_NODE_BINARY_PERCENT: return S8("%");
        case AST_NODE_BINARY_SHIFT_LEFT: return S8("<<");
        case AST_NODE_BINARY_SHIFT_RIGHT: return S8(">>");
        case AST_NODE_BINARY_EQUAL: return S8("==");
        case AST_NODE_BINARY_NOT_EQUAL: return S8("!=");
        case AST_NODE_BINARY_LESS: return S8("<");
        case AST_NODE_BINARY_LESS_EQUAL: return S8("<=");
        case AST_NODE_BINARY_GREATER: return S8(">");
        case AST_NODE_BINARY_GREATER_EQUAL: return S8(">=");
        case AST_NODE_BINARY_AMPERSAND: return S8("&");
        case AST_NODE_BINARY_BAR: return S8("|");
        case AST_NODE_BINARY_CARET: return S8("^");

        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_COUNT:
        {
        }
    }

    BUSTER_UNREACHABLE();
}

// Append a node to the current expression's postorder output stream.
BUSTER_GLOBAL_LOCAL AstExpressionNode* expression_emit(Parser* restrict parser, ParserState* restrict st, AstNodeId id)
{
    AstExpressionNode* node = arena_allocate(parser->expression_arena, AstExpressionNode, 1);
    node->id = id;
    node->reserved = 0;
    node->integer = (AstIntegerLiteral){0};
    if (st->expression.output_count == 0)
    {
        st->expression.output_base = node;
    }
    st->expression.output_count += 1;
    return node;
}

BUSTER_GLOBAL_LOCAL void parse_block(Parser* restrict parser, AstBlock* block, ExtendedToken opening_brace)
{
    *block = (AstBlock){ .range = source_range_from_token(opening_brace) };
    ParserState* block_state = state_push(&parser->state);
    block_state->id = PARSER_DECLARATION_BLOCK;
    block_state->block.brace_depth = 1;
    block_state->block.block = block;
}

BUSTER_GLOBAL_LOCAL void parse_expression(Parser* restrict parser, TokenId end_of_statement_token)
{
    arena_set_position(parser->expression_arena, parser->expression_arena_minimum_position);
    ParserState* state = state_push(&parser->state);
    state->id = PARSER_DECLARATION_EXPRESSION;
    state->expression.state = EXPRESSION_STATE_PREFIX;
    state->expression.end_token = end_of_statement_token;
}

BUSTER_GLOBAL_LOCAL void finish_expression(Parser* restrict parser)
{
    ParserState* expression_state = state(parser);
    BUSTER_CHECK(expression_state->id == PARSER_DECLARATION_EXPRESSION);
    u32 output_count = expression_state->expression.output_count;
    BUSTER_CHECK(output_count);
    AstExpressionNode* output = arena_allocate(parser->result_arena, AstExpressionNode, output_count);
    memcpy(output, expression_state->expression.output_base, sizeof(*output) * output_count);
    AstExpression expression = { .nodes = output, .count = output_count };

    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_DECLARATION_RETURN_STATEMENT:
        {
            resume_state->return_statement.statement->expression = expression;
            resume_state->return_statement.state = RETURN_STATEMENT_STATE_END;
        }
        break; default: BUSTER_TODO();
    }
}

BUSTER_GLOBAL_LOCAL String8 string_from_token_id(TokenIdEnum id)
{
    switch (id)
    {
        break; case TOKEN_ERROR: return S8("Error");
        break; case TOKEN_SPACE: return S8("Space");
        break; case TOKEN_TAB: return S8("Tab");
        break; case TOKEN_LINE_FEED: return S8("LineFeed");
        break; case TOKEN_CARRIAGE_RETURN: return S8("CarriageReturn");
        break; case TOKEN_COMMENT: return S8("Comment");
        break; case TOKEN_EOF: return S8("EOF");
        break; case TOKEN_IDENTIFIER: return S8("Identifier");
        break; case TOKEN_HEXADECIMAL_INTEGER_LITERAL: return S8("HexadecimalIntegerLiteral");
        break; case TOKEN_DECIMAL_INTEGER_LITERAL: return S8("DecimalIntegerLiteral");
        break; case TOKEN_OCTAL_INTEGER_LITERAL: return S8("OctalIntegerLiteral");
        break; case TOKEN_BINARY_INTEGER_LITERAL: return S8("BinaryIntegerLiteral");
        break; case TOKEN_DECIMAL_FLOAT_LITERAL: return S8("DecimalFloatLiteral");
        break; case TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT: return S8("DecimalFloatLiteralExponent");
        break; case TOKEN_HEXADECIMAL_FLOAT_LITERAL: return S8("HexadecimalFloatLiteral");
        break; case TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT: return S8("HexadecimalFloatLiteralExponent");
        break; case TOKEN_FLOAT_LITERAL: return S8("FloatLiteral");
        break; case TOKEN_UNDERSCORE: return S8("Underscore");
        break; case TOKEN_LEFT_BRACKET: return S8("LeftBracket");
        break; case TOKEN_RIGHT_BRACKET: return S8("RightBracket");
        break; case TOKEN_LEFT_BRACE: return S8("LeftBrace");
        break; case TOKEN_RIGHT_BRACE: return S8("RightBrace");
        break; case TOKEN_LEFT_PARENTHESIS: return S8("LeftParenthesis");
        break; case TOKEN_RIGHT_PARENTHESIS: return S8("RightParenthesis");
        break; case TOKEN_EQUAL: return S8("Equal");
        break; case TOKEN_EQUAL_EQUAL: return S8("EqualEqual");
        break; case TOKEN_BANG: return S8("Bang");
        break; case TOKEN_BANG_EQUAL: return S8("BangEqual");
        break; case TOKEN_GREATER: return S8("Greater");
        break; case TOKEN_GREATER_EQUAL: return S8("GreaterEqual");
        break; case TOKEN_LESS: return S8("Less");
        break; case TOKEN_LESS_EQUAL: return S8("LessEqual");
        break; case TOKEN_SHIFT_LEFT: return S8("ShiftLeft");
        break; case TOKEN_SHIFT_RIGHT: return S8("ShiftRight");
        break; case TOKEN_PLUS: return S8("Plus");
        break; case TOKEN_PLUS_EQUAL: return S8("PlusEqual");
        break; case TOKEN_MINUS: return S8("Minus");
        break; case TOKEN_ASTERISK: return S8("Asterisk");
        break; case TOKEN_SLASH: return S8("Slash");
        break; case TOKEN_COLON: return S8("Colon");
        break; case TOKEN_SEMICOLON: return S8("Semicolon");
        break; case TOKEN_COMMA: return S8("Comma");
        break; case TOKEN_DOT: return S8("Dot");
        break; case TOKEN_DOUBLE_DOT: return S8("DoubleDot");
        break; case TOKEN_TRIPLE_DOT: return S8("TripleDot");
        break; case TOKEN_AMPERSAND: return S8("Ampersand");
        break; case TOKEN_PERCENTAGE: return S8("Percent");
        break; case TOKEN_BAR: return S8("Bar");
        break; case TOKEN_CARET: return S8("Caret");
        break; case TOKEN_TILDE: return S8("Tilde");
        break; case TOKEN_KEYWORD_RETURN: return S8("Keyword_Return");
        break; case TOKEN_KEYWORD_IF: return S8("Keyword_If");
        break; case TOKEN_KEYWORD_ELSE: return S8("Keyword_Else");
        break; case TOKEN_KEYWORD_FUNCTION: return S8("Keyword_Function");
        break; case TOKEN_KEYWORD_FOR: return S8("Keyword_For");
        break; case TOKEN_KEYWORD_WHILE: return S8("Keyword_While");
        break; case TOKEN_KEYWORD_CODE: return S8("Keyword_Code");
        break; case TOKEN_KEYWORD_DATA: return S8("Keyword_Data");
        break; case TOKEN_KEYWORD_TYPE: return S8("Keyword_Type");
        break; case TOKEN_KEYWORD_STRUCT: return S8("Keyword_Struct");
        break; case TOKEN_KEYWORD_UNION: return S8("Keyword_Union");
        break; case TOKEN_COUNT: return S8("Token_Count(Error)");
    }

    BUSTER_UNREACHABLE();
}

#define BUSTER_TODO_TOKEN(id) BUSTER_TODO_MESSAGE(S8("TODO {S8}"), string_from_token_id((TokenIdEnum)id))
#define BUSTER_TODO_NODE(id) BUSTER_TODO_MESSAGE(S8("TODO {S8}"), string_from_node_id((AstNodeId)id))

// The expression frame that owns the (possibly empty) run of pending
// unary-prefix frames currently on top of the state stack. Frames are
// contiguous in the state arena, so walking down is plain pointer arithmetic.
BUSTER_GLOBAL_LOCAL ParserState* expression_owner(Parser* restrict parser)
{
    ParserState* frame = state(parser);
    while (frame->id == PARSER_DECLARATION_UNARY_PREFIX)
    {
        frame -= 1;
    }
    BUSTER_CHECK(frame->id == PARSER_DECLARATION_EXPRESSION);
    return frame;
}

// Prefix position of an expression: either an operand or another prefix unary
// operator. Shared by the expression frame (PREFIX state) and the unary-prefix
// frames stacked on top of it.
BUSTER_GLOBAL_LOCAL void expression_parse_prefix(Parser* restrict parser)
{
    ExtendedToken token = peek(parser);

    switch (token.id)
    {
        break;
        case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
        case TOKEN_DECIMAL_INTEGER_LITERAL:
        case TOKEN_OCTAL_INTEGER_LITERAL:
        case TOKEN_BINARY_INTEGER_LITERAL:
        {
            consume(&parser->iterator);

            String8 number_string = get_string(parser->iterator.source, token);
            IntegerLiteralParsing number_parsing = parse_integer_literal(number_string, token.id);
            if (!number_parsing.valid)
            {
                parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_INVALID_INTEGER, token, TOKEN_ERROR, S8("invalid integer literal"));
                number_parsing.value = 0;
                number_parsing.fits_u64 = false;
            }

            ParserState* owner = expression_owner(parser);
            AstExpressionNode* leaf = expression_emit(parser, owner, AST_NODE_CONSTANT_INTEGER);
            leaf->integer = (AstIntegerLiteral){
                .spelling = number_string,
                .value = number_parsing.value,
                .base = number_parsing.base,
                .fits_u64 = number_parsing.fits_u64,
            };

            // Prefix unary operators bind tighter than any binary operator, so
            // they are emitted right after their operand, innermost first, by
            // popping their frames.
            while (state(parser)->id == PARSER_DECLARATION_UNARY_PREFIX)
            {
                ParserState unary_frame = state_pop(&parser->state);
                expression_emit(parser, owner, (AstNodeId)unary_frame.unary_prefix.id);
            }

            BUSTER_CHECK(state(parser) == owner);
            owner->expression.state = EXPRESSION_STATE_TAIL;
        }
        break;
        case TOKEN_MINUS:
        case TOKEN_PLUS:
        case TOKEN_BANG:
        case TOKEN_TILDE:
        {
            consume(&parser->iterator);
            AstNodeId unary_id;
            switch (token.id)
            {
                break; case TOKEN_MINUS: unary_id = AST_NODE_UNARY_MINUS;
                break; case TOKEN_PLUS: unary_id = AST_NODE_UNARY_PLUS;
                break; case TOKEN_BANG: unary_id = AST_NODE_UNARY_LOGICAL_NOT;
                break; case TOKEN_TILDE: unary_id = AST_NODE_UNARY_BITWISE_NOT;
                break; default: BUSTER_TODO_TOKEN(token.id);
            }
            ParserState* unary_state = state_push(&parser->state);
            unary_state->id = PARSER_DECLARATION_UNARY_PREFIX;
            unary_state->unary_prefix.id = (u8)unary_id;
        }
        break; default: parser_unexpected(parser, token, TOKEN_DECIMAL_INTEGER_LITERAL);
    }
}

ParserResult parser_parse(Arena* result_arena, String8 source, TokenizerResult tokenizer)
{
    ParserResult result = { .source = source };
    TemporalArena scratch = scratch_begin(&result_arena, 1);
    Parser parser = {0};
    parser.iterator.tokens = tokenizer.tokens;
    parser.iterator.token_count = tokenizer.token_count;
    parser.iterator.source = source.pointer;
    parser.state.arena = scratch.arena;
    parser.state.minimum_position = scratch.arena->position;
    parser.result_arena = result_arena;
    parser.expression_arena = arena_create((ArenaCreation){0});
    parser.expression_arena_minimum_position = parser.expression_arena->position;
    parser.result = &result;

    // Push a dummy state so the stack is never empty
    state_push(&parser.state);

    bool is_running = true;

    while (is_running)
    {
        if (parser.recovery != PARSER_RECOVERY_NONE)
        {
            parser_recover(&parser);
            continue;
        }
        switch (state(&parser)->id)
        {
            break; case PARSER_DECLARATION_COUNT: BUSTER_UNREACHABLE();
            break; case PARSER_DECLARATION_ROOT:
            {
                ExtendedToken token = peek_and_consume(&parser);

                switch (token.id)
                {
                    break; case TOKEN_KEYWORD_CODE:
                    {
                        ParserState* function_state = state_push(&parser.state);
                        function_state->id = PARSER_DECLARATION_CODE;
                        function_state->code.current_state = CODE_STATE_BEFORE_NAME;
                        function_state->code.code = parser_code_push(&parser, token);
                    }
                    break; case TOKEN_EOF:
                    {
                        is_running = false;
                    }
                    break; default: parser_unexpected(&parser, token, TOKEN_KEYWORD_CODE);
                }
            }
            break; case PARSER_DECLARATION_CODE:
            {
                ParserState* code_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (code_state->code.current_state)
                {
                    break; case CODE_STATE_BEFORE_NAME:
                    {
                        consume(&parser.iterator);

                        switch (token.id)
                        {
                            break; case TOKEN_LEFT_BRACKET:
                            {
                                ParserState* attribute_list_state = state_push(&parser.state);
                                attribute_list_state->id = PARSER_DECLARATION_ATTRIBUTE_LIST;
                                attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_CODE;
                                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                                attribute_list_state->attribute_list.code = code_state->code.code;
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                code_state->code.code->name = get_string(parser.iterator.source, token);
                                code_state->code.current_state = CODE_STATE_AFTER_NAME;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                        }
                    }
                    break; case CODE_STATE_AFTER_NAME:
                    {
                        consume(&parser.iterator);

                        switch (token.id)
                        {
                            break; case TOKEN_COLON:
                            {
                                code_state->code.current_state = CODE_STATE_TYPE;

                                ParserState* type_state = state_push(&parser.state);
                                type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                                type_state->type.code = code_state->code.code;
                                type_state->type.destination = &code_state->code.code->type;
                            }
                            break; case TOKEN_EQUAL:
                            {
                                parser_unexpected(&parser, token, TOKEN_COLON);
                            }
                            break; case TOKEN_LEFT_BRACKET:
                            {
                                ParserState* attribute_list_state = state_push(&parser.state);
                                attribute_list_state->id = PARSER_DECLARATION_ATTRIBUTE_LIST;
                                attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_SYMBOL;
                                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                                attribute_list_state->attribute_list.code = code_state->code.code;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_COLON);
                        }
                    }
                    break; case CODE_STATE_TYPE:
                    {
                        BUSTER_UNREACHABLE();
                    }
                    break; case CODE_STATE_AFTER_TYPE:
                    {
                        switch (token.id)
                        {
                            break; case TOKEN_LEFT_BRACE:
                            {
                                consume(&parser.iterator);

                                code_state->code.current_state = CODE_STATE_BODY;
                                code_state->code.code->has_body = true;
                                parse_block(&parser, &code_state->code.code->body, token);
                            }
                            break; case TOKEN_SEMICOLON:
                            {
                                consume(&parser.iterator);
                                code_state->code.code->range.length = token.offset + token.length - code_state->code.code->range.offset;
                                state_pop(&parser.state);
                            }
                            break; case TOKEN_EQUAL:
                            {
                                consume(&parser.iterator);
                                code_state->code.current_state = CODE_STATE_AFTER_EQUAL;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_LEFT_BRACE);
                        }
                    }
                    break; case CODE_STATE_AFTER_EQUAL:
                    {
                        if (token.id != TOKEN_LEFT_BRACE)
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_BRACE);
                            continue;
                        }

                        consume(&parser.iterator);

                        code_state->code.current_state = CODE_STATE_BODY;
                        code_state->code.code->has_body = true;
                        parse_block(&parser, &code_state->code.code->body, token);
                    }
                    break; case CODE_STATE_BODY:
                    {
                        code_state->code.code->range.length = code_state->code.code->body.range.offset + code_state->code.code->body.range.length - code_state->code.code->range.offset;
                        state_pop(&parser.state);
                    }
                    break; case CODE_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_TYPE_REFERENCE:
            {
                ParserState* type_state = state(&parser);
                ExtendedToken token = peek(&parser);
                
                switch (type_state->type.current_state)
                {
                    break; case TYPE_STATE_PREFIX_OR_BASE:
                    {
                        switch (token.id)
                        {
                            break; case pointer_token:
                            {
                                consume(&parser.iterator);
                                AstType* pointer = parser_type_attach(&parser, type_state, AST_TYPE_POINTER, token);
                                type_state->type.destination = &pointer->element_type;
                            }
                            break; case array_slice_token_start:
                            {
                                consume(&parser.iterator);
                                type_state->type.prefix_range = source_range_from_token(token);
                                type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_SLICE_START;
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                consume(&parser.iterator);
                                AstType* named = parser_type_attach(&parser, type_state, AST_TYPE_NAMED, token);
                                named->name = get_string(parser.iterator.source, token);
                                finish_type_reference(&parser, token.offset + token.length);
                            }
                            break; case TOKEN_KEYWORD_FUNCTION:
                            {
                                consume(&parser.iterator);
                                type_state->type.type = parser_type_attach(&parser, type_state, AST_TYPE_FUNCTION, token);
                                type_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_KEYWORD;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                        }
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_SLICE_START:
                    {
                        switch (token.id)
                        {
                            break; case array_slice_token_end:
                            {
                                consume(&parser.iterator);
                                ParserSourceRange range = type_state->type.prefix_range;
                                AstType* slice = parser_type_attach(&parser, type_state, AST_TYPE_SLICE, (ExtendedToken){
                                    .offset = range.offset, .length = range.length, .line = range.line, .column = range.column,
                                });
                                type_state->type.destination = &slice->element_type;
                                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                            }
                            // break; case TOKEN_Number:
                            // {
                            //     consume(&parser.iterator)                            //     type_state->type.current_state = TYPE_STATE_AfterArrayCount;
                            // }
                            break; case TOKEN_UNDERSCORE:
                            {
                                consume(&parser.iterator);
                                ParserSourceRange range = type_state->type.prefix_range;
                                AstType* inferred_array = parser_type_attach(&parser, type_state, AST_TYPE_INFERRED_ARRAY, (ExtendedToken){
                                    .offset = range.offset, .length = range.length, .line = range.line, .column = range.column,
                                });
                                type_state->type.destination = &inferred_array->element_type;
                                type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_INFER_MARKER;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                        }
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_COUNT:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                            continue;
                        }

                        consume(&parser.iterator);
                        type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_INFER_MARKER:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                            continue;
                        }

                        consume(&parser.iterator);
                        type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_AFTER_FUNCTION_KEYWORD:
                    {
                        switch (token.id)
                        {
                            break; case TOKEN_LEFT_BRACKET:
                            {
                                consume(&parser.iterator);

                                ParserState* attribute_list_state = state_push(&parser.state);
                                attribute_list_state->id = PARSER_DECLARATION_ATTRIBUTE_LIST;
                                attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_FUNCTION;
                                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                                attribute_list_state->attribute_list.code = type_state->type.code;
                                attribute_list_state->attribute_list.type = type_state->type.type;
                            }
                            break; case TOKEN_LEFT_PARENTHESIS:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE:
                    {
                        switch (token.id)
                        {
                            break; case TOKEN_RIGHT_PARENTHESIS:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_RETURN_TYPE;
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                consume(&parser.iterator);
                                type_state->type.argument = parser_type_argument_push(&parser, type_state->type.type, token);
                                type_state->type.name_range = source_range_from_token(token);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT:
                    {
                        if (token.id != TOKEN_COLON)
                        {
                            parser_unexpected(&parser, token, TOKEN_COLON);
                            continue;
                        }

                        consume(&parser.iterator);
                        type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_COLON;
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_COLON:
                    {
                        if (token.id == TOKEN_IDENTIFIER)
                        {
                            ExtendedToken next = peek_ahead(&parser, 1);
                            if (next.id == TOKEN_COLON)
                            {
                                consume(&parser.iterator);
                                type_state->type.name_range.length = token.offset + token.length - type_state->type.name_range.offset;
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT;
                                break;
                            }
                        }

                        if (!token_begins_type(token.id))
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            continue;
                        }

                        type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_TYPE;
                        type_state->type.argument->name = (String8){
                            .pointer = (char8*)parser.iterator.source + type_state->type.name_range.offset,
                            .length = type_state->type.name_range.length,
                        };

                        ParserState* child_type_state = state_push(&parser.state);
                        child_type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                        child_type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                        child_type_state->type.code = type_state->type.code;
                        child_type_state->type.destination = &type_state->type.argument->type;
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
                    {
                        BUSTER_TODO();
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE:
                    {
                        switch (token.id)
                        {
                            break; case TOKEN_COMMA:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE;
                            }
                            break; case TOKEN_RIGHT_PARENTHESIS:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_RETURN_TYPE;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_RETURN_TYPE:
                    {
                        if (!token_begins_type(token.id))
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            continue;
                        }

                        ParserState* child_type_state = state_push(&parser.state);
                        child_type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                        child_type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                        child_type_state->type.code = type_state->type.code;
                        child_type_state->type.destination = &type_state->type.type->function.return_type;
                    }
                    break; case TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE:
                    {
                        AstType* return_type = type_state->type.type->function.return_type;
                        finish_type_reference(&parser, return_type->range.offset + return_type->range.length);
                    }
                    break; case TYPE_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_ATTRIBUTE_LIST:
            {
                ParserState* attribute_list_state = state(&parser);
                ExtendedToken token = peek_and_consume(&parser);

                switch (attribute_list_state->attribute_list.current_state)
                {
                    break; case ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE:
                    {
                        switch (token.id)
                        {
                            break; case TOKEN_RIGHT_BRACKET:
                            {
                                state_pop(&parser.state);
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                String8 attribute_name = get_string(parser.iterator.source, token);

                                switch (attribute_list_state->attribute_list.kind)
                                {
                                    break; case ATTRIBUTE_LIST_CODE:
                                    {
                                        if (string_equal(attribute_name, code_attributes_names[CODE_ATTRIBUTE_INLINE]))
                                        {
                                            attribute_list_state->attribute_list.code->inline_hint = true;
                                        }
                                        else
                                        {
                                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                                        }
                                    }
                                    break; case ATTRIBUTE_LIST_DATA:
                                    {
                                        parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                                    }
                                    break; case ATTRIBUTE_LIST_SYMBOL:
                                    {
                                        SymbolAttribute attribute = SYMBOL_ATTRIBUTE_COUNT;

                                        for (EACH_ARRAY_INDEX(i, symbol_attribute_names))
                                        {
                                            if (string_equal(attribute_name, symbol_attribute_names[i]))
                                            {
                                                attribute = (SymbolAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (attribute)
                                        {
                                            break; case SYMBOL_ATTRIBUTE_EXPORT:
                                            {
                                                attribute_list_state->attribute_list.code->exported = true;
                                            }
                                            break; case SYMBOL_ATTRIBUTE_COUNT: parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                                          break;
                                        }
                                    }
                                    break; case ATTRIBUTE_LIST_FUNCTION:
                                    {
                                        if (!string_equal(attribute_name, function_attribute_names[FUNCTION_ATTRIBUTE_CALLING_CONVENTION]))
                                        {
                                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                                        }

                                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN;
                                    }
                                    break; case ATTRIBUTE_LIST_COUNT: BUSTER_UNREACHABLE();
                                }
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                        }
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                            continue;
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME:
                    {
                        AstCallingConvention calling_convention = AST_CALLING_CONVENTION_COUNT;
                        for (EACH_ARRAY_INDEX(i, calling_convention_names))
                        {
                            if (token_matches(&parser, token, calling_convention_names[i]))
                            {
                                calling_convention = (AstCallingConvention)i;
                                break;
                            }
                        }
                        if (calling_convention == AST_CALLING_CONVENTION_COUNT)
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            continue;
                        }

                        BUSTER_CHECK(attribute_list_state->attribute_list.type);
                        attribute_list_state->attribute_list.type->function.calling_convention = calling_convention;
                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                            continue;
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                    }
                    break; case ATTRIBUTE_LIST_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_BLOCK:
            {
                ParserState* block_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (token.id)
                {
                    break; case TOKEN_RIGHT_BRACE:
                    {
                        consume(&parser.iterator);

                        if (block_state->block.brace_depth == 0)
                        {
                            BUSTER_TODO();
                        }

                        block_state->block.brace_depth -= 1;
                        if (block_state->block.brace_depth == 0)
                        {
                            block_state->block.block->range.length = token.offset + token.length - block_state->block.block->range.offset;
                            state_pop(&parser.state);
                        }
                    }
                    break; default:
                    {
                        ParserState* statement_state = state_push(&parser.state);
                        statement_state->id = PARSER_DECLARATION_STATEMENT;
                        statement_state->statement.state = STATEMENT_STATE_START;
                    }
                }
            }
            break; case PARSER_DECLARATION_STATEMENT:
            {
                ParserState* statement_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (statement_state->statement.state)
                {
                    break; case STATEMENT_STATE_START:
                    {
                        statement_state->statement.state = STATEMENT_STATE_END;
                        statement_state->statement.end_token = block_end_of_statement_token;

                        consume(&parser.iterator);

                        switch (token.id)
                        {
                            break; case TOKEN_KEYWORD_RETURN:
                            {
                                ParserState* block_state = state_previous(&parser.state);
                                BUSTER_CHECK(block_state->id == PARSER_DECLARATION_BLOCK);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_RETURN, token);
                                statement_state->statement.statement = statement;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_DECLARATION_RETURN_STATEMENT;
                                state->return_statement.state = RETURN_STATEMENT_STATE_VALUE_OR_END;
                                state->return_statement.statement = statement;
                                state->return_statement.end_token = statement_state->statement.end_token;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_KEYWORD_RETURN);
                        }
                    }
                    break; case STATEMENT_STATE_END:
                    {
                        bool has_end_of_statement = statement_state->statement.end_token != TOKEN_ERROR;

                        if (has_end_of_statement)
                        {
                            if (token.id != statement_state->statement.end_token)
                            {
                                parser_unexpected(&parser, token, statement_state->statement.end_token);
                                continue;
                            }

                            consume(&parser.iterator);
                            statement_state->statement.statement->range.length = token.offset + token.length - statement_state->statement.statement->range.offset;
                        }

                        state_pop(&parser.state);
                    }
                    break; case STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_RETURN_STATEMENT:
            {
                ParserState* return_statement_state = state(&parser);
                ExtendedToken token = peek(&parser);

                TokenId end_of_statement_token = return_statement_state->return_statement.end_token;

                switch (return_statement_state->return_statement.state)
                {
                    break; case RETURN_STATEMENT_STATE_VALUE_OR_END:
                    {
                        if (token.id == end_of_statement_token)
                        {
                            return_statement_state->return_statement.state = RETURN_STATEMENT_STATE_END;
                        }
                        else
                        {
                            parse_expression(&parser, end_of_statement_token);
                        }
                    }
                    break; case RETURN_STATEMENT_STATE_END:
                    {
                        if (token.id != end_of_statement_token)
                        {
                            parser_unexpected(&parser, token, end_of_statement_token);
                            continue;
                        }

                        state_pop(&parser.state);
                    }
                    break; case RETURN_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_EXPRESSION:
            {
                ParserState* st = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (st->expression.state)
                {
                    break; case EXPRESSION_STATE_PREFIX:
                    {
                        expression_parse_prefix(&parser);
                    }
                    break; case EXPRESSION_STATE_TAIL:
                    {
                        if (token.id == st->expression.end_token)
                        {
                            // Emit any operators still held on the stack (tightest-binding
                            // first) to complete the postorder stream.
                            while (st->expression.operator_count)
                            {
                                st->expression.operator_count -= 1;
                                expression_emit(&parser, st, (AstNodeId)st->expression.operator_stack[st->expression.operator_count]);
                            }
                            finish_expression(&parser);
                        }
                        else
                        {
                            switch (token.id)
                            {
                                break;
                                case TOKEN_PLUS:
                                case TOKEN_MINUS:
                                case TOKEN_ASTERISK:
                                case TOKEN_SLASH:
                                case TOKEN_PERCENTAGE:
                                case TOKEN_SHIFT_LEFT:
                                case TOKEN_SHIFT_RIGHT:
                                case TOKEN_EQUAL_EQUAL:
                                case TOKEN_BANG_EQUAL:
                                case TOKEN_LESS:
                                case TOKEN_LESS_EQUAL:
                                case TOKEN_GREATER:
                                case TOKEN_GREATER_EQUAL:
                                case TOKEN_BAR:
                                case TOKEN_AMPERSAND:
                                case TOKEN_CARET:
                                {
                                    consume(&parser.iterator);

                                    AstNodeId binary_node_id;

                                    switch (token.id)
                                    {
                                        break; case TOKEN_PLUS: binary_node_id = AST_NODE_BINARY_PLUS;
                                        break; case TOKEN_MINUS: binary_node_id = AST_NODE_BINARY_MINUS;
                                        break; case TOKEN_ASTERISK: binary_node_id = AST_NODE_BINARY_ASTERISK;
                                        break; case TOKEN_SLASH: binary_node_id = AST_NODE_BINARY_SLASH;
                                        break; case TOKEN_PERCENTAGE: binary_node_id = AST_NODE_BINARY_PERCENT;
                                        break; case TOKEN_SHIFT_LEFT: binary_node_id = AST_NODE_BINARY_SHIFT_LEFT;
                                        break; case TOKEN_SHIFT_RIGHT: binary_node_id = AST_NODE_BINARY_SHIFT_RIGHT;
                                        break; case TOKEN_EQUAL_EQUAL: binary_node_id = AST_NODE_BINARY_EQUAL;
                                        break; case TOKEN_BANG_EQUAL: binary_node_id = AST_NODE_BINARY_NOT_EQUAL;
                                        break; case TOKEN_LESS: binary_node_id = AST_NODE_BINARY_LESS;
                                        break; case TOKEN_LESS_EQUAL: binary_node_id = AST_NODE_BINARY_LESS_EQUAL;
                                        break; case TOKEN_GREATER: binary_node_id = AST_NODE_BINARY_GREATER;
                                        break; case TOKEN_GREATER_EQUAL: binary_node_id = AST_NODE_BINARY_GREATER_EQUAL;
                                        break; case TOKEN_AMPERSAND: binary_node_id = AST_NODE_BINARY_AMPERSAND;
                                        break; case TOKEN_BAR: binary_node_id = AST_NODE_BINARY_BAR;
                                        break; case TOKEN_CARET: binary_node_id = AST_NODE_BINARY_CARET;
                                        break; default: BUSTER_TODO_TOKEN(token.id);
                                    }

                                    // Shunting yard: before pushing this operator, emit every stacked
                                    // operator that binds at least as tightly. `>=` makes equal
                                    // precedence left-associative; a strictly-tighter incoming
                                    // operator stays pending so it captures the next operand instead.
                                    BindingPower binding_power = binary_binding_power(binary_node_id);
                                    while (st->expression.operator_count &&
                                           binary_binding_power((AstNodeId)st->expression.operator_stack[st->expression.operator_count - 1]) >= binding_power)
                                    {
                                        st->expression.operator_count -= 1;
                                        expression_emit(&parser, st, (AstNodeId)st->expression.operator_stack[st->expression.operator_count]);
                                    }

                                    BUSTER_CHECK(st->expression.operator_count < BINDING_POWER_COUNT);
                                    st->expression.operator_stack[st->expression.operator_count] = (u8)binary_node_id;
                                    st->expression.operator_count += 1;
                                    st->expression.state = EXPRESSION_STATE_PREFIX;
                                }
                                break; default: parser_unexpected(&parser, token, st->expression.end_token);
                            }
                        }
                    }
                    break; case EXPRESSION_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_UNARY_PREFIX:
            {
                expression_parse_prefix(&parser);
            }
            break; case PARSER_DECLARATION_TYPE_DECLARATION:
            {
                BUSTER_TODO();
            }
            break; case PARSER_DECLARATION_DATA_DECLARATION:
            {
                BUSTER_TODO();
            }
        }
    }

    arena_destroy(parser.expression_arena, 1);
    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL void print_tokenizer_result(TokenizerResult tokenizer, const char8* restrict source)
{
    TokenIterator iterator = token_initialize(tokenizer.tokens, tokenizer.token_count, source);
    for (u32 i = 0; i < tokenizer.token_count; i += 1)
    {
        ExtendedToken token = token_get(&iterator);
        String8 string = get_string(source, token);

        String8 token_id = string_from_token_id(token.id);

        String8 display_string = string.pointer && string.length > 0 && string.pointer[0] >= ' ' ? string : S8("");
        string_print(S8("[{u64}] {u32}:{u32} at {u32} {S8} \"{S8}\"\n"), i, token.line, token.column, token.offset, token_id, display_string);

        consume(&iterator);
    }
}

// Reconstruct an S-expression from the implicit tree with a single forward pass
// over the postorder stream. This is the exact shape an analysis/typecheck pass
// takes: stream the contiguous array, push leaves and reduce operators against a
// small operand stack. No recursion, no pointer chasing.
BUSTER_GLOBAL_LOCAL String8 ast_expression_to_string(Arena* arena, AstExpression expression)
{
    if (expression.count == 0)
    {
        return S8("");
    }

    String8* stack = arena_allocate(arena, String8, expression.count);
    u32 top = 0;

    for (u32 i = 0; i < expression.count; i += 1)
    {
        AstExpressionNode* node = &expression.nodes[i];
        String8 formatted;

        switch (ast_node_arity(node->id))
        {
            break; case 0:
            {
                formatted = node->integer.fits_u64 ? string_format(arena, S8("{u64}"), node->integer.value) : node->integer.spelling;
            }
            break; case 1:
            {
                BUSTER_CHECK(top >= 1);
                String8 operand = stack[top - 1];
                top -= 1;
                formatted = string_format(arena, S8("({S8} {S8})"), ast_node_symbol(node->id), operand);
            }
            break; default:
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

typedef struct ParserFileTestCase ParserFileTestCase;
struct ParserFileTestCase
{
    String8 path;
    String8 expected_expression;
};

BUSTER_GLOBAL_LOCAL ParserFileTestCase parser_file_test_cases[] = {
    { S8_INITIALIZER("tests/basic_minimal.bbb"), S8_INITIALIZER("0") },
    { S8_INITIALIZER("tests/basic_comment.bbb"), S8_INITIALIZER("0") },
    { S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"), S8_INITIALIZER("0") },
    { S8_INITIALIZER("tests/basic_octal_literal.bbb"), S8_INITIALIZER("0") },
    { S8_INITIALIZER("tests/basic_binary_literal.bbb"), S8_INITIALIZER("0") },
    { S8_INITIALIZER("tests/basic_unary_minus.bbb"), S8_INITIALIZER("(neg 0)") },
    { S8_INITIALIZER("tests/basic_unary_plus.bbb"), S8_INITIALIZER("(pos 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_add.bbb"), S8_INITIALIZER("(+ 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"), S8_INITIALIZER("(- 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"), S8_INITIALIZER("(* 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"), S8_INITIALIZER("(/ 0 1)") },
    { S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"), S8_INITIALIZER("(% 0 1)") },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"), S8_INITIALIZER("(<< 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"), S8_INITIALIZER("(>> 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_and.bbb"), S8_INITIALIZER("(& 0 1)") },
    { S8_INITIALIZER("tests/basic_integer_literal_or.bbb"), S8_INITIALIZER("(| 0 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"), S8_INITIALIZER("(^ 1 1)") },
    { S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"), S8_INITIALIZER("(!= (== (< 1 2) 3) (> (>= (<= 4 5) 6) 7))") },
    { S8_INITIALIZER("tests/basic_logical_not.bbb"), S8_INITIALIZER("(not (not 0))") },
    { S8_INITIALIZER("tests/basic_bitwise_not.bbb"), S8_INITIALIZER("(bit_not 0)") },
    { S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"), S8_INITIALIZER("(<< (+ 1 (* 2 3)) (- 4 5))") },
    // { S8_INITIALIZER("tests/basic_if_else.bbb"), S8_INITIALIZER("") },
    // { S8_INITIALIZER("tests/array_slices.bbb"), S8_INITIALIZER("") },
};

BUSTER_GLOBAL_LOCAL int parser_bench_u64_compare(const void* a, const void* b)
{
    u64 left = *(const u64*)a;
    u64 right = *(const u64*)b;
    return (left > right) - (left < right);
}

#if BUSTER_INSTRUMENT
BUSTER_GLOBAL_LOCAL int parser_bench_file_result_compare_desc(const void* a, const void* b)
{
    const ParserBenchFileResult* left = (const ParserBenchFileResult*)a;
    const ParserBenchFileResult* right = (const ParserBenchFileResult*)b;
    return (left->median_ns < right->median_ns) - (left->median_ns > right->median_ns);
}
#endif

ParserBenchResult parser_parse_bench(Arena* arena, u64 iterations)
{
    ParserBenchResult result = {0};
    result.iterations = iterations;
    result.file_count = BUSTER_ARRAY_LENGTH(parser_file_test_cases);

    u64* durations_ns = arena_allocate(arena, u64, iterations);
#if BUSTER_INSTRUMENT
    u64* tokenize_durations_ns = arena_allocate(arena, u64, iterations);
    u64* parse_durations_ns = arena_allocate(arena, u64, iterations);
    u64* file_durations_ns = arena_allocate(arena, u64, iterations * result.file_count);
#endif

    for (u64 iteration = 0; iteration < iterations; iteration += 1)
    {
        TemporalArena scratch = scratch_begin(&arena, 1);

#if BUSTER_INSTRUMENT
        u64 tokenize_ns_sum = 0;
        u64 parse_ns_sum = 0;
#endif
        TimeDataType start = timestamp_take();
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(parser_file_test_cases); i += 1)
        {
            ParserFileTestCase test_case = parser_file_test_cases[i];
            String8 source = BYTE_SLICE_TO_STRING(8, file_read(scratch.arena, test_case.path, (FileReadOptions){0}));

            if (source.pointer && source.length)
            {
#if BUSTER_INSTRUMENT
                TimeDataType file_start = timestamp_take();
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                TimeDataType tokenize_end = timestamp_take();
                parser_parse(scratch.arena, source, tokenizer);
                TimeDataType parse_end = timestamp_take();

                tokenize_ns_sum += timestamp_ns_between(file_start, tokenize_end);
                parse_ns_sum += timestamp_ns_between(tokenize_end, parse_end);
                file_durations_ns[iteration * result.file_count + i] = timestamp_ns_between(file_start, parse_end);
#else
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                parser_parse(scratch.arena, source, tokenizer);
#endif
            }
        }
        TimeDataType end = timestamp_take();
        durations_ns[iteration] = timestamp_ns_between(start, end);
#if BUSTER_INSTRUMENT
        tokenize_durations_ns[iteration] = tokenize_ns_sum;
        parse_durations_ns[iteration] = parse_ns_sum;
#endif

        scratch_end(scratch);
    }

    qsort(durations_ns, iterations, sizeof(u64), parser_bench_u64_compare);
    result.min_ns = iterations ? durations_ns[0] : 0;
    result.median_ns = iterations ? durations_ns[iterations / 2] : 0;

#if BUSTER_INSTRUMENT
    qsort(tokenize_durations_ns, iterations, sizeof(u64), parser_bench_u64_compare);
    qsort(parse_durations_ns, iterations, sizeof(u64), parser_bench_u64_compare);
    result.tokenize_min_ns = iterations ? tokenize_durations_ns[0] : 0;
    result.tokenize_median_ns = iterations ? tokenize_durations_ns[iterations / 2] : 0;
    result.parse_min_ns = iterations ? parse_durations_ns[0] : 0;
    result.parse_median_ns = iterations ? parse_durations_ns[iterations / 2] : 0;

    result.files = arena_allocate(arena, ParserBenchFileResult, result.file_count);
    u64* per_file_ns = arena_allocate(arena, u64, iterations);
    for (u64 file_i = 0; file_i < result.file_count; file_i += 1)
    {
        for (u64 iteration = 0; iteration < iterations; iteration += 1)
        {
            per_file_ns[iteration] = file_durations_ns[iteration * result.file_count + file_i];
        }
        qsort(per_file_ns, iterations, sizeof(u64), parser_bench_u64_compare);

        result.files[file_i] = (ParserBenchFileResult){
            .path = parser_file_test_cases[file_i].path,
            .min_ns = iterations ? per_file_ns[0] : 0,
            .median_ns = iterations ? per_file_ns[iterations / 2] : 0,
        };
    }
    qsort(result.files, result.file_count, sizeof(ParserBenchFileResult), parser_bench_file_result_compare_desc);
#endif

    return result;
}

#if BUSTER_INCLUDE_TESTS
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
    u64 position = arena->position;

    {
        TokenizerResult tokenizer = tokenize(arena, S8("").pointer, 0);
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, 0));
        BUSTER_TEST(arguments, tokenizer.token_count == 1);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_EOF);
        arena->position = position;
    }

    {
        char8 source[] = { '1', '2', '3' };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = { '1', '+', '2' };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 4);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, tokenizer.tokens[1].id == TOKEN_PLUS);
        BUSTER_TEST(arguments, tokenizer.tokens[2].id == TOKEN_DECIMAL_INTEGER_LITERAL);
        arena->position = position;
    }

    {
        char8 source[] = { '\t', '\t' };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_TAB);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = { '0', 'x' };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        arena->position = position;
    }

    {
        char8 source[] = { (char8)0xC3u, (char8)0xA9u };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == 2);
        BUSTER_TEST(arguments, tokenizer.tokens[0].id == TOKEN_ERROR);
        BUSTER_TEST(arguments, token_length_get(&tokenizer.tokens[0]) == BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer.error_count == 1);
        arena->position = position;
    }

    {
        char8 source[] = { (char8)0xF0u, (char8)0x80u, (char8)0x80u, (char8)0x80u };
        TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
        BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source)));
        BUSTER_TEST(arguments, tokenizer.token_count == BUSTER_ARRAY_LENGTH(source) + 1);
        BUSTER_TEST(arguments, tokenizer.error_count == BUSTER_ARRAY_LENGTH(source));
        arena->position = position;
    }

    {
        char8 source[] = { '1', '+', '2', '\t', (char8)0xE2u, (char8)0x82u, (char8)0xACu, 0 };
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
            char8 source[] = { (char8)byte };
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
                char8 source[] = { (char8)first, (char8)second };
                TokenizerResult tokenizer = tokenize(arena, source, BUSTER_ARRAY_LENGTH(source));
                all_byte_pairs_pass = all_byte_pairs_pass && tokenizer_stream_covers_source(tokenizer, BUSTER_ARRAY_LENGTH(source));
                arena->position = position;
            }
        }
        BUSTER_TEST(arguments, all_byte_pairs_pass);
    }

    return result;
}

// Parse a bare expression by wrapping it in a minimal program and returning the
// postorder stream of its return value.
BUSTER_GLOBAL_LOCAL AstExpression parse_expression_snippet(Arena* arena, String8 expression)
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
    ParserResult parsed = parser_parse(arena, (String8){ buffer, length }, tokenizer);
    BUSTER_CHECK(parsed.diagnostic_count == 0);
    BUSTER_CHECK(parsed.first_code);
    BUSTER_CHECK(parsed.first_code->body.first_statement);
    return parsed.first_code->body.first_statement->expression;
}

UnitTestResult parser_expression_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    struct { String8 source; String8 expected; } cases[] = {
        { S8("1 + 2 * 3"),           S8("(+ 1 (* 2 3))") },
        { S8("1+2"),                 S8("(+ 1 2)") },
        { S8("1 * 2 + 3"),           S8("(+ (* 1 2) 3)") },
        { S8("1 + 2 + 3"),           S8("(+ (+ 1 2) 3)") },
        { S8("1 - 2 - 3"),           S8("(- (- 1 2) 3)") },
        { S8("2 * 3 + 4 * 5"),       S8("(+ (* 2 3) (* 4 5))") },
        { S8("2 * -3"),              S8("(* 2 (neg 3))") },
        { S8("- - 5"),               S8("(neg (neg 5))") },
        { S8("!!1"),                 S8("(not (not 1))") },
        { S8("~!1"),                 S8("(bit_not (not 1))") },
        { S8("1 << 2 + 3 * 4"),      S8("(<< 1 (+ 2 (* 3 4)))") },
        { S8("1 + 2 * 3 << 4 - 5"),  S8("(<< (+ 1 (* 2 3)) (- 4 5))") },
        { S8("1 + 2 < 3 << 4"),      S8("(< (+ 1 2) (<< 3 4))") },
        { S8("1 <= 2"),              S8("(<= 1 2)") },
        { S8("1 > 2"),               S8("(> 1 2)") },
        { S8("1 >= 2"),              S8("(>= 1 2)") },
        { S8("1 == 2"),              S8("(== 1 2)") },
        { S8("1 != 2"),              S8("(!= 1 2)") },
        { S8("1 < 2 == 3"),          S8("(== (< 1 2) 3)") },
        { S8("1 & 2 == 3"),          S8("(& 1 (== 2 3))") },
    };

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        u64 position = arena->position;
        AstExpression expression = parse_expression_snippet(arena, cases[i].source);
        String8 actual = ast_expression_to_string(arena, expression);
        BUSTER_STRING_TEST(arguments, actual, cases[i].expected);
        arena->position = position;
    }

    return result;
}

UnitTestResult parser_result_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u64 position = arena->position;

    {
        String8 source = S8(
            "code first[export] : fn[cc(systemv)] () s32 { return 1; }\n"
            "code second : fn[cc(win64)] () s32 { return 2; }\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.last_code != 0 && parsed.first_code != parsed.last_code);
        if (parsed.first_code && parsed.last_code)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_code->name, S8("first"));
            BUSTER_STRING_TEST(arguments, parsed.last_code->name, S8("second"));
            BUSTER_TEST(arguments, parsed.first_code->exported);
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
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->expression.count == 1);
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->expression.nodes[0].integer.value == 1);
                BUSTER_TEST(arguments, parsed.last_code->body.first_statement->expression.nodes[0].integer.value == 2);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code broken : fn[cc(c)] () s32 { return 1 + ; return 3; } code recovered : fn[cc(c)] () s32 { return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.code_count == 2);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstExpression recovered_expression = parsed.first_code->body.last_statement->expression;
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
            ParserResult parsed = parser_parse(arena, source, tokenizer);
            BUSTER_TEST(arguments, parsed.diagnostic_count > 0);
            arena->position = position;
        }
    }

    {
        String8 source = S8("code main : fn () s32 { return 1 + 1__0; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        AstStatement* statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, statement != 0 && statement->expression.count == 3);
        if (statement && statement->expression.count == 3)
        {
            AstExpressionNode* nodes = statement->expression.nodes;
            BUSTER_TEST(arguments, nodes[0].id == AST_NODE_CONSTANT_INTEGER && nodes[0].integer.value == 1);
            BUSTER_TEST(arguments, nodes[1].id == AST_NODE_CONSTANT_INTEGER && nodes[1].integer.value == 0);
            BUSTER_TEST(arguments, nodes[2].id == AST_NODE_BINARY_PLUS);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code typed : fn (argument:count: s32, argv: &&u8, bytes: []u8, inferred: [_]u8) s32;");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstType* function = parsed.first_code ? parsed.first_code->type : 0;
        BUSTER_TEST(arguments, function != 0 && function->id == AST_TYPE_FUNCTION);
        if (function && function->id == AST_TYPE_FUNCTION)
        {
            BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + function->range.offset, function->range.length }), S8("fn (argument:count: s32, argv: &&u8, bytes: []u8, inferred: [_]u8) s32"));
            BUSTER_TEST(arguments, function->function.argument_count == 4);
            BUSTER_TEST(arguments, function->function.return_type != 0);
            BUSTER_TEST(arguments, function->function.return_type && function->function.return_type->id == AST_TYPE_NAMED);
            AstTypeArgument* argument = function->function.first_argument;
            BUSTER_TEST(arguments, argument != 0);
            if (argument)
            {
                BUSTER_STRING_TEST(arguments, argument->name, S8("argument:count"));
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->range.offset, argument->range.length }), S8("argument:count: s32"));
                BUSTER_TEST(arguments, argument->type && argument->type->id == AST_TYPE_NAMED);
                argument = argument->next;
            }
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_POINTER);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->range.offset, argument->range.length }), S8("argv: &&u8"));
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->type->range.offset, argument->type->range.length }), S8("&&u8"));
                AstType* second_pointer = argument->type->element_type;
                BUSTER_TEST(arguments, second_pointer && second_pointer->id == AST_TYPE_POINTER);
                if (second_pointer)
                {
                    BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + second_pointer->range.offset, second_pointer->range.length }), S8("&u8"));
                }
            }
            argument = argument ? argument->next : 0;
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_SLICE);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->range.offset, argument->range.length }), S8("bytes: []u8"));
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->type->range.offset, argument->type->range.length }), S8("[]u8"));
            }
            argument = argument ? argument->next : 0;
            BUSTER_TEST(arguments, argument && argument->type && argument->type->id == AST_TYPE_INFERRED_ARRAY);
            if (argument && argument->type)
            {
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->range.offset, argument->range.length }), S8("inferred: [_]u8"));
                BUSTER_STRING_TEST(arguments, ((String8){ source.pointer + argument->type->range.offset, argument->type->range.length }), S8("[_]u8"));
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
        { S8_INITIALIZER("1_000"), 1000, true, PARSER_DIAGNOSTIC_COUNT, 0 },
        { S8_INITIALIZER("0xF_F"), 255, true, PARSER_DIAGNOSTIC_COUNT, 0 },
        { S8_INITIALIZER("1__000"), 0, false, PARSER_DIAGNOSTIC_INVALID_INTEGER, 1 },
        { S8_INITIALIZER("18446744073709551616"), 0, false, PARSER_DIAGNOSTIC_COUNT, 0 },
        { S8_INITIALIZER("340282366920938463463374607431768211456"), 0, false, PARSER_DIAGNOSTIC_COUNT, 0 },
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
        String8 source = { source_pointer, source_length };
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == integer_cases[i].diagnostic_count);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->kind == integer_cases[i].expected_diagnostic);
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.first_statement != 0);
        if (parsed.first_code && parsed.first_code->body.first_statement)
        {
            AstExpression expression = parsed.first_code->body.first_statement->expression;
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

        enum { deep_unary_count = 40 };
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

        AstExpression expression = parse_expression_snippet(arena, source);
        String8 actual = ast_expression_to_string(arena, expression);
        BUSTER_STRING_TEST(arguments, actual, expected);

        arena->position = deep_unary_position;
    }

    // Regression: parser_parse() must not crash when the caller hands it one
    // of the thread's own scratch arenas as result_arena. The parser used to
    // carve its expression arena out of the same scratch pool, so a caller-
    // supplied scratch arena could exhaust both slots and hand back null.
    {
        TemporalArena caller_scratch = scratch_begin(0, 0);
        String8 source = S8("code main : fn[cc(c)] () s32 { return 1 + 2 * 3; }");
        TokenizerResult tokenizer = tokenize(caller_scratch.arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(caller_scratch.arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.code_count == 1);
        scratch_end(caller_scratch);
    }

    return result;
}

UnitTestResult parser_file_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(parser_file_test_cases); i += 1)
    {
        u64 position = arena->position;
        ParserFileTestCase test_case = parser_file_test_cases[i];
        String8 source = BYTE_SLICE_TO_STRING(8, file_read(arena, test_case.path, (FileReadOptions){0}));

        BUSTER_TEST(arguments, source.pointer != 0);
        BUSTER_TEST(arguments, source.length > 0);

        if (source.pointer && source.length)
        {
            TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);

            ParserResult parsed = parser_parse(arena, source, tokenizer);
            BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
            BUSTER_TEST(arguments, parsed.code_count == 1);
            AstExpression expression = {0};
            if (parsed.first_code && parsed.first_code->body.first_statement)
            {
                expression = parsed.first_code->body.first_statement->expression;
            }
            String8 actual = ast_expression_to_string(arena, expression);
            BUSTER_STRING_TEST(arguments, actual, test_case.expected_expression);
        }

        arena->position = position;
    }

    return result;
}
#endif
