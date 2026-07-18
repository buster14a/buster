#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/time.h>

#define first_keyword TOKEN_KEYWORD_RETURN
#define last_keyword (TOKEN_COUNT - 1)

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
                    [TOKEN_KEYWORD_LOOP - first_keyword] = S8_INITIALIZER("loop"),
                    [TOKEN_KEYWORD_CODE - first_keyword] = S8_INITIALIZER("code"),
                    [TOKEN_KEYWORD_DATA - first_keyword] = S8_INITIALIZER("data"),
                    [TOKEN_KEYWORD_TYPE - first_keyword] = S8_INITIALIZER("type"),
                    [TOKEN_KEYWORD_STRUCT - first_keyword] = S8_INITIALIZER("struct"),
                    [TOKEN_KEYWORD_UNION - first_keyword] = S8_INITIALIZER("union"),
                    [TOKEN_KEYWORD_UNDEFINED - first_keyword] = S8_INITIALIZER("undefined"),
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
                    bool has_equal = it + 2 < top && it[2] == '=';
                    id = has_equal ? TOKEN_SHIFT_LEFT_EQUAL : TOKEN_SHIFT_LEFT;
                    it += has_equal ? 3 : 2;
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
                    bool has_equal = it + 2 < top && it[2] == '=';
                    id = has_equal ? TOKEN_SHIFT_RIGHT_EQUAL : TOKEN_SHIFT_RIGHT;
                    it += has_equal ? 3 : 2;
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
            break; case '-':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_MINUS_EQUAL : TOKEN_MINUS;
                it += has_equal ? 2 : 1;
            }
            break; case '*':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_ASTERISK_EQUAL : TOKEN_ASTERISK;
                it += has_equal ? 2 : 1;
            }
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
            break; case '&':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_AMPERSAND_EQUAL : TOKEN_AMPERSAND;
                it += has_equal ? 2 : 1;
            }
            break; case '%':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_PERCENTAGE_EQUAL : TOKEN_PERCENTAGE;
                it += has_equal ? 2 : 1;
            }
            break; case '|':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_BAR_EQUAL : TOKEN_BAR;
                it += has_equal ? 2 : 1;
            }
            break; case '^':
            {
                bool has_equal = it + 1 < top && it[1] == '=';
                id = has_equal ? TOKEN_CARET_EQUAL : TOKEN_CARET;
                it += has_equal ? 2 : 1;
            }
            break; case '~': { id = TOKEN_TILDE; it += 1; }
            break; case '@': { id = TOKEN_AT; it += 1; }
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
                else if (it + 1 < top && it[1] == '=')
                {
                    id = TOKEN_SLASH_EQUAL;
                    it += 2;
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

typedef enum ParserStateId
{
    PARSER_STATE_ROOT,
    PARSER_STATE_CODE,
    PARSER_STATE_TYPE_REFERENCE,
    PARSER_STATE_ATTRIBUTE_LIST,
    PARSER_STATE_BLOCK,
    PARSER_STATE_STATEMENT,
    PARSER_STATE_RETURN_STATEMENT,
    PARSER_STATE_DATA_STATEMENT,
    PARSER_STATE_ASSIGNMENT_STATEMENT,
    PARSER_STATE_IF_STATEMENT,
    PARSER_STATE_FOR_STATEMENT,
    PARSER_STATE_LOOP_STATEMENT,
    PARSER_STATE_TYPE_STATEMENT,
    PARSER_STATE_ARRAY_LITERAL,
    PARSER_STATE_ARRAY_SUBSCRIPT,
    PARSER_STATE_INTRINSIC_CALL,
    PARSER_STATE_EXPRESSION,
    PARSER_STATE_UNARY_PREFIX,
    PARSER_STATE_COUNT,
}ParserStateId;

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

typedef enum ReturnStatementStateId
{
    RETURN_STATEMENT_STATE_VALUE_OR_END,
    RETURN_STATEMENT_STATE_END,
    RETURN_STATEMENT_STATE_COUNT,
} ReturnStatementStateId;

typedef struct ReturnStatementState ReturnStatementState;
struct ReturnStatementState
{
    ReturnStatementStateId id;
};

typedef enum DataStatementStateId
{
    DATA_STATEMENT_STATE_NAME,
    DATA_STATEMENT_STATE_AFTER_NAME,
    DATA_STATEMENT_STATE_AFTER_TYPE,
    DATA_STATEMENT_STATE_INITIALIZER,
    DATA_STATEMENT_STATE_END,
    DATA_STATEMENT_STATE_COUNT,
} DataStatementStateId;

typedef struct DataStatementState DataStatementState;
struct DataStatementState
{
    DataStatementStateId id;
};

typedef enum AssignmentStatementStateId
{
    ASSIGNMENT_STATEMENT_STATE_TARGET,
    ASSIGNMENT_STATEMENT_STATE_OPERATOR,
    ASSIGNMENT_STATEMENT_STATE_VALUE,
    ASSIGNMENT_STATEMENT_STATE_END,
    ASSIGNMENT_STATEMENT_STATE_COUNT,
} AssignmentStatementStateId;

typedef struct AssignmentStatementState AssignmentStatementState;
struct AssignmentStatementState
{
    AssignmentStatementStateId id;
};

typedef enum IfStatementStateId
{
    IF_STATEMENT_STATE_OPEN_CONDITION,
    IF_STATEMENT_STATE_CONDITION,
    IF_STATEMENT_STATE_CLOSE_CONDITION,
    IF_STATEMENT_STATE_THEN_BLOCK,
    IF_STATEMENT_STATE_ELSE_OR_END,
    IF_STATEMENT_STATE_ELSE_BLOCK,
    IF_STATEMENT_STATE_COUNT,
} IfStatementStateId;

typedef struct IfStatementState IfStatementState;
struct IfStatementState
{
    IfStatementStateId id;
};

typedef enum ForStatementStateId
{
    FOR_STATEMENT_STATE_OPEN,
    FOR_STATEMENT_STATE_DATA,
    FOR_STATEMENT_STATE_NAME,
    FOR_STATEMENT_STATE_TYPE_OR_EQUAL,
    FOR_STATEMENT_STATE_TYPE,
    FOR_STATEMENT_STATE_EQUAL,
    FOR_STATEMENT_STATE_ITERABLE,
    FOR_STATEMENT_STATE_CLOSE,
    FOR_STATEMENT_STATE_BODY,
    FOR_STATEMENT_STATE_COUNT,
} ForStatementStateId;

typedef struct ForStatementState ForStatementState;
struct ForStatementState
{
    ForStatementStateId id;
};

typedef enum LoopStatementStateId
{
    LOOP_STATEMENT_STATE_CONDITION_OR_BODY,
    LOOP_STATEMENT_STATE_CONDITION,
    LOOP_STATEMENT_STATE_CLOSE_CONDITION,
    LOOP_STATEMENT_STATE_BODY,
    LOOP_STATEMENT_STATE_COUNT,
} LoopStatementStateId;

typedef struct LoopStatementState LoopStatementState;
struct LoopStatementState
{
    LoopStatementStateId id;
};

typedef enum ArrayLiteralStateId
{
    ARRAY_LITERAL_STATE_ELEMENT_OR_END,
    ARRAY_LITERAL_STATE_DELIMITER,
    ARRAY_LITERAL_STATE_COUNT,
} ArrayLiteralStateId;

typedef enum ArraySubscriptStateId
{
    ARRAY_SUBSCRIPT_STATE_START_OR_RANGE,
    ARRAY_SUBSCRIPT_STATE_AFTER_START,
    ARRAY_SUBSCRIPT_STATE_END_OR_CLOSE,
    ARRAY_SUBSCRIPT_STATE_CLOSE,
    ARRAY_SUBSCRIPT_STATE_COUNT,
} ArraySubscriptStateId;

typedef enum IntrinsicCallStateId
{
    INTRINSIC_CALL_STATE_NAME,
    INTRINSIC_CALL_STATE_OPEN,
    INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE,
    INTRINSIC_CALL_STATE_DELIMITER,
    INTRINSIC_CALL_STATE_COUNT,
} IntrinsicCallStateId;

typedef enum ExpressionState
{
    EXPRESSION_STATE_PREFIX,
    EXPRESSION_STATE_TAIL,
    EXPRESSION_STATE_COUNT,
} ExpressionState;

typedef enum BindingPower
{
    BINDING_POWER_RANGE,
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
    ParserStateId id;
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
            union
            {
                StatementStateId statement_state;
                ReturnStatementState return_state;
                DataStatementState data_state;
                AssignmentStatementState assignment_state;
                IfStatementState if_state;
                ForStatementState for_state;
                LoopStatementState loop_state;
            };
            AstStatement* pointer;
            TokenId end_token;
        } statement;

        struct
        {
            ExpressionState state;
            TokenId end_token;
            bool is_group;
            bool ends_at_assignment;
            bool is_array_element;
            bool ends_at_array_delimiter;
            bool is_array_subscript_bound;
            bool is_array_subscript_end;
            bool ends_at_slice_operator;
            bool is_intrinsic_argument;
            bool ends_at_intrinsic_delimiter;
            // Postorder (RPN) output stream this expression emits into. The tree
            // is implicit in this ordering plus each node's arity, so no child
            // links are stored. `output_base` is the first emitted node.
            AstNode* output_base;
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

        struct
        {
            ParserSourceRange range;
            ArrayLiteralStateId state;
            u32 element_count;
        } array_literal;

        struct
        {
            ParserSourceRange range;
            ArraySubscriptStateId state;
            bool has_start;
            bool has_end;
        } array_subscript;

        struct
        {
            AstIdentifier name;
            ParserSourceRange range;
            IntrinsicCallStateId state;
            u32 argument_count;
        } intrinsic_call;

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
    BUSTER_CHECK(type_state->id == PARSER_STATE_TYPE_REFERENCE);
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
    ParserStateId state_id = state(parser)->id;
    if (state_id == PARSER_STATE_STATEMENT || state_id == PARSER_STATE_RETURN_STATEMENT ||
        state_id == PARSER_STATE_DATA_STATEMENT || state_id == PARSER_STATE_ASSIGNMENT_STATEMENT ||
        state_id == PARSER_STATE_IF_STATEMENT || state_id == PARSER_STATE_FOR_STATEMENT ||
        state_id == PARSER_STATE_LOOP_STATEMENT ||
        state_id == PARSER_STATE_ARRAY_LITERAL ||
        state_id == PARSER_STATE_ARRAY_SUBSCRIPT ||
        state_id == PARSER_STATE_INTRINSIC_CALL ||
        state_id == PARSER_STATE_EXPRESSION || state_id == PARSER_STATE_UNARY_PREFIX)
    {
        parser->recovery = PARSER_RECOVERY_STATEMENT;
    }
    else
    {
        parser->recovery = PARSER_RECOVERY_DECLARATION;
    }
}

BUSTER_GLOBAL_LOCAL void parser_expected_assignment_operator(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(
            parser,
            PARSER_DIAGNOSTIC_EXPECTED_ASSIGNMENT_OPERATOR,
            token,
            TOKEN_ERROR,
            S8("expected assignment operator"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_expected_array_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(
            parser,
            PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER,
            token,
            TOKEN_ERROR,
            S8("expected ',' or ']' after array element"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_chained_range(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(
            parser,
            PARSER_DIAGNOSTIC_CHAINED_RANGE,
            token,
            TOKEN_ERROR,
            S8("range operator '..' is not associative"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_recover(Parser* parser)
{
    if (parser->recovery == PARSER_RECOVERY_STATEMENT)
    {
        AstStatement* statement = 0;
        while (state(parser)->id != PARSER_STATE_BLOCK && state(parser)->id != PARSER_STATE_ROOT)
        {
            ParserState popped = state_pop(&parser->state);
            if (popped.id == PARSER_STATE_STATEMENT) { statement = popped.statement.pointer; }
            else if (popped.id == PARSER_STATE_RETURN_STATEMENT || popped.id == PARSER_STATE_DATA_STATEMENT ||
                     popped.id == PARSER_STATE_ASSIGNMENT_STATEMENT || popped.id == PARSER_STATE_IF_STATEMENT ||
                     popped.id == PARSER_STATE_FOR_STATEMENT || popped.id == PARSER_STATE_LOOP_STATEMENT)
            {
                statement = popped.statement.pointer;
            }
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
            break; case AST_TYPE_ARRAY:
            {
                type = type->array.element_type;
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
        break; case PARSER_STATE_CODE:
        {
            if (resume_state->code.current_state != CODE_STATE_TYPE)
            {
                BUSTER_TODO();
            }

            resume_state->code.current_state = CODE_STATE_AFTER_TYPE;
        }
        break; case PARSER_STATE_TYPE_REFERENCE:
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
        break; case PARSER_STATE_FOR_STATEMENT:
        {
            BUSTER_CHECK(resume_state->statement.for_state.id == FOR_STATEMENT_STATE_TYPE);
            resume_state->statement.for_state.id = FOR_STATEMENT_STATE_EQUAL;
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
        case AST_NODE_UNDEFINED: return S8("Undefined");
        case AST_NODE_ARRAY_LITERAL: return S8("ArrayLiteral");
        case AST_NODE_ARRAY_INDEX: return S8("ArrayIndex");
        case AST_NODE_ARRAY_SLICE: return S8("ArraySlice");
        case AST_NODE_INTRINSIC_CALL: return S8("IntrinsicCall");
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
        case AST_NODE_BINARY_RANGE: return S8("BinaryRange");
        case AST_NODE_IDENTIFIER: return S8("Identifier");
        case AST_NODE_COUNT: {}
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
        case AST_NODE_BINARY_RANGE:
        {
            return BINDING_POWER_RANGE;
        }
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
        case AST_NODE_IDENTIFIER:
        case AST_NODE_UNDEFINED:
        case AST_NODE_ARRAY_LITERAL:
        case AST_NODE_ARRAY_INDEX:
        case AST_NODE_ARRAY_SLICE:
        case AST_NODE_INTRINSIC_CALL:
        {
        }
    }

    BUSTER_UNREACHABLE();
}

// Number of operand subtrees a node consumes. Drives the implicit-tree walk:
// leaves push, unary pops 1, binary pops 2.
BUSTER_GLOBAL_LOCAL u32 ast_node_arity(AstNode* node)
{
    switch (node->id)
    {
        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_IDENTIFIER:
        case AST_NODE_UNDEFINED:
        {
            return 0;
        }
        case AST_NODE_ARRAY_LITERAL:
        {
            return node->array_literal.element_count;
        }
        case AST_NODE_ARRAY_SLICE:
        {
            return 1 + (u32)node->array_slice.has_start + (u32)node->array_slice.has_end;
        }
        case AST_NODE_INTRINSIC_CALL:
        {
            return node->intrinsic_call.argument_count;
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
        case AST_NODE_BINARY_RANGE: return S8("range");
        case AST_NODE_ARRAY_INDEX: return S8("index");

        case AST_NODE_CONSTANT_INTEGER:
        case AST_NODE_IDENTIFIER:
        case AST_NODE_UNDEFINED:
        case AST_NODE_ARRAY_LITERAL:
        case AST_NODE_ARRAY_SLICE:
        case AST_NODE_INTRINSIC_CALL:
        case AST_NODE_COUNT:
        {
        }
    }

    BUSTER_UNREACHABLE();
}

// Append a node to the current expression's postorder output stream.
BUSTER_GLOBAL_LOCAL AstNode* expression_emit(Parser* restrict parser, ParserState* restrict st, AstNodeId id)
{
    AstNode* node = arena_allocate(parser->expression_arena, AstNode, 1);
    node->id = id;
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
    block_state->id = PARSER_STATE_BLOCK;
    block_state->block.brace_depth = 1;
    block_state->block.block = block;
}

BUSTER_GLOBAL_LOCAL bool token_is_assignment_operator(TokenId id)
{
    switch (id)
    {
        case TOKEN_EQUAL:
        case TOKEN_PLUS_EQUAL:
        case TOKEN_MINUS_EQUAL:
        case TOKEN_ASTERISK_EQUAL:
        case TOKEN_SLASH_EQUAL:
        case TOKEN_PERCENTAGE_EQUAL:
        case TOKEN_SHIFT_LEFT_EQUAL:
        case TOKEN_SHIFT_RIGHT_EQUAL:
        case TOKEN_AMPERSAND_EQUAL:
        case TOKEN_BAR_EQUAL:
        case TOKEN_CARET_EQUAL:
        {
            return true;
        }
        default:
        {
            return false;
        }
    }
}

BUSTER_GLOBAL_LOCAL AstAssignmentOperator assignment_operator_from_token(TokenId id)
{
    switch (id)
    {
        case TOKEN_EQUAL: return AST_ASSIGNMENT_EQUAL;
        case TOKEN_PLUS_EQUAL: return AST_ASSIGNMENT_PLUS_EQUAL;
        case TOKEN_MINUS_EQUAL: return AST_ASSIGNMENT_MINUS_EQUAL;
        case TOKEN_ASTERISK_EQUAL: return AST_ASSIGNMENT_MULTIPLY_EQUAL;
        case TOKEN_SLASH_EQUAL: return AST_ASSIGNMENT_DIVIDE_EQUAL;
        case TOKEN_PERCENTAGE_EQUAL: return AST_ASSIGNMENT_MODULO_EQUAL;
        case TOKEN_SHIFT_LEFT_EQUAL: return AST_ASSIGNMENT_SHIFT_LEFT_EQUAL;
        case TOKEN_SHIFT_RIGHT_EQUAL: return AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL;
        case TOKEN_AMPERSAND_EQUAL: return AST_ASSIGNMENT_BITWISE_AND_EQUAL;
        case TOKEN_BAR_EQUAL: return AST_ASSIGNMENT_BITWISE_OR_EQUAL;
        case TOKEN_CARET_EQUAL: return AST_ASSIGNMENT_BITWISE_XOR_EQUAL;
        default: BUSTER_UNREACHABLE();
    }
}

BUSTER_GLOBAL_LOCAL void parse_expression(Parser* restrict parser, TokenId end_of_statement_token)
{
    arena_set_position(parser->expression_arena, parser->expression_arena_minimum_position);
    ParserState* state = state_push(&parser->state);
    state->id = PARSER_STATE_EXPRESSION;
    state->expression.state = EXPRESSION_STATE_PREFIX;
    state->expression.end_token = end_of_statement_token;
    state->expression.is_group = false;
    state->expression.ends_at_assignment = false;
    state->expression.is_array_element = false;
    state->expression.ends_at_array_delimiter = false;
    state->expression.is_array_subscript_bound = false;
    state->expression.is_array_subscript_end = false;
    state->expression.ends_at_slice_operator = false;
    state->expression.is_intrinsic_argument = false;
    state->expression.ends_at_intrinsic_delimiter = false;
}

BUSTER_GLOBAL_LOCAL void parse_assignment_target(Parser* restrict parser)
{
    parse_expression(parser, TOKEN_ERROR);
    state(parser)->expression.ends_at_assignment = true;
}

BUSTER_GLOBAL_LOCAL void parse_array_element(Parser* restrict parser)
{
    ParserState* element = state_push(&parser->state);
    element->id = PARSER_STATE_EXPRESSION;
    element->expression.state = EXPRESSION_STATE_PREFIX;
    element->expression.end_token = TOKEN_ERROR;
    element->expression.is_array_element = true;
    element->expression.ends_at_array_delimiter = true;
    element->expression.is_array_subscript_bound = false;
    element->expression.is_array_subscript_end = false;
    element->expression.ends_at_slice_operator = false;
    element->expression.is_intrinsic_argument = false;
    element->expression.ends_at_intrinsic_delimiter = false;
}

BUSTER_GLOBAL_LOCAL void parse_array_subscript(Parser* restrict parser, ExtendedToken opening_bracket)
{
    ParserState* subscript = state_push(&parser->state);
    subscript->id = PARSER_STATE_ARRAY_SUBSCRIPT;
    subscript->array_subscript.range = source_range_from_token(opening_bracket);
    subscript->array_subscript.state = ARRAY_SUBSCRIPT_STATE_START_OR_RANGE;
    subscript->array_subscript.has_start = false;
    subscript->array_subscript.has_end = false;
}

BUSTER_GLOBAL_LOCAL void parse_array_subscript_bound(Parser* restrict parser, bool is_end)
{
    ParserState* bound = state_push(&parser->state);
    bound->id = PARSER_STATE_EXPRESSION;
    bound->expression.state = EXPRESSION_STATE_PREFIX;
    bound->expression.end_token = TOKEN_RIGHT_BRACKET;
    bound->expression.is_group = false;
    bound->expression.ends_at_assignment = false;
    bound->expression.is_array_element = false;
    bound->expression.ends_at_array_delimiter = false;
    bound->expression.is_array_subscript_bound = true;
    bound->expression.is_array_subscript_end = is_end;
    bound->expression.ends_at_slice_operator = !is_end;
    bound->expression.is_intrinsic_argument = false;
    bound->expression.ends_at_intrinsic_delimiter = false;
}

BUSTER_GLOBAL_LOCAL void parse_intrinsic_argument(Parser* restrict parser)
{
    ParserState* argument = state_push(&parser->state);
    argument->id = PARSER_STATE_EXPRESSION;
    argument->expression.state = EXPRESSION_STATE_PREFIX;
    argument->expression.end_token = TOKEN_ERROR;
    argument->expression.is_group = false;
    argument->expression.ends_at_assignment = false;
    argument->expression.is_array_element = false;
    argument->expression.ends_at_array_delimiter = false;
    argument->expression.is_array_subscript_bound = false;
    argument->expression.is_array_subscript_end = false;
    argument->expression.ends_at_slice_operator = false;
    argument->expression.is_intrinsic_argument = true;
    argument->expression.ends_at_intrinsic_delimiter = true;
}

BUSTER_GLOBAL_LOCAL ParserState* expression_owner(Parser* restrict parser);
BUSTER_GLOBAL_LOCAL void expression_finish_operand(Parser* restrict parser, ParserState* restrict owner);

BUSTER_GLOBAL_LOCAL void finish_expression(Parser* restrict parser)
{
    ParserState* expression_state = state(parser);
    BUSTER_CHECK(expression_state->id == PARSER_STATE_EXPRESSION);
    u32 output_count = expression_state->expression.output_count;
    BUSTER_CHECK(output_count);

    if (expression_state->expression.is_intrinsic_argument)
    {
        AstNode* output_base = expression_state->expression.output_base;
        state_pop(&parser->state);

        ParserState* intrinsic = state(parser);
        BUSTER_CHECK(intrinsic->id == PARSER_STATE_INTRINSIC_CALL);
        ParserState* owner = intrinsic - 1;
        while (owner->id == PARSER_STATE_UNARY_PREFIX)
        {
            owner -= 1;
        }
        BUSTER_CHECK(owner->id == PARSER_STATE_EXPRESSION);
        if (owner->expression.output_count == 0)
        {
            owner->expression.output_base = output_base;
        }
        owner->expression.output_count += output_count;
        intrinsic->intrinsic_call.argument_count += 1;
        intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_DELIMITER;
        return;
    }

    if (expression_state->expression.is_array_subscript_bound)
    {
        AstNode* output_base = expression_state->expression.output_base;
        bool is_end = expression_state->expression.is_array_subscript_end;
        state_pop(&parser->state);

        ParserState* subscript = state(parser);
        BUSTER_CHECK(subscript->id == PARSER_STATE_ARRAY_SUBSCRIPT);
        ParserState* owner = subscript - 1;
        while (owner->id == PARSER_STATE_UNARY_PREFIX)
        {
            owner -= 1;
        }
        BUSTER_CHECK(owner->id == PARSER_STATE_EXPRESSION);
        if (owner->expression.output_count == 0)
        {
            owner->expression.output_base = output_base;
        }
        owner->expression.output_count += output_count;
        if (is_end)
        {
            subscript->array_subscript.has_end = true;
            subscript->array_subscript.state = ARRAY_SUBSCRIPT_STATE_CLOSE;
        }
        else
        {
            subscript->array_subscript.has_start = true;
            subscript->array_subscript.state = ARRAY_SUBSCRIPT_STATE_AFTER_START;
        }
        return;
    }

    if (expression_state->expression.is_array_element)
    {
        AstNode* output_base = expression_state->expression.output_base;
        state_pop(&parser->state);

        ParserState* array_state = state(parser);
        BUSTER_CHECK(array_state->id == PARSER_STATE_ARRAY_LITERAL);
        ParserState* owner = array_state - 1;
        while (owner->id == PARSER_STATE_UNARY_PREFIX)
        {
            owner -= 1;
        }
        BUSTER_CHECK(owner->id == PARSER_STATE_EXPRESSION);
        if (owner->expression.output_count == 0)
        {
            owner->expression.output_base = output_base;
        }
        owner->expression.output_count += output_count;
        array_state->array_literal.element_count += 1;
        array_state->array_literal.state = ARRAY_LITERAL_STATE_DELIMITER;
        return;
    }

    if (expression_state->expression.is_group)
    {
        AstNode* output_base = expression_state->expression.output_base;
        state_pop(&parser->state);

        ExtendedToken closing_parenthesis = peek(parser);
        BUSTER_CHECK(closing_parenthesis.id == TOKEN_RIGHT_PARENTHESIS);
        consume(&parser->iterator);

        ParserState* owner = expression_owner(parser);
        if (owner->expression.output_count == 0)
        {
            owner->expression.output_base = output_base;
        }
        owner->expression.output_count += output_count;
        expression_finish_operand(parser, owner);
        return;
    }

    AstNode* output = arena_allocate(parser->result_arena, AstNode, output_count);
    memcpy(output, expression_state->expression.output_base, sizeof(*output) * output_count);
    AstExpression expression = { .nodes = output, .count = output_count };

    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_STATE_RETURN_STATEMENT:
        {
            resume_state->statement.pointer->return_statement.expression = expression;
            resume_state->statement.return_state.id = RETURN_STATEMENT_STATE_END;
        }
        break; case PARSER_STATE_DATA_STATEMENT:
        {
            resume_state->statement.pointer->data_statement.initializer = expression;
            resume_state->statement.data_state.id = DATA_STATEMENT_STATE_END;
        }
        break; case PARSER_STATE_ASSIGNMENT_STATEMENT:
        {
            switch (resume_state->statement.assignment_state.id)
            {
                break; case ASSIGNMENT_STATEMENT_STATE_TARGET:
                {
                    resume_state->statement.pointer->assignment_statement.target = expression;
                    resume_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_OPERATOR;
                }
                break; case ASSIGNMENT_STATEMENT_STATE_VALUE:
                {
                    resume_state->statement.pointer->assignment_statement.value = expression;
                    resume_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_END;
                }
                break; default: BUSTER_UNREACHABLE();
            }
        }
        break; case PARSER_STATE_IF_STATEMENT:
        {
            BUSTER_CHECK(resume_state->statement.if_state.id == IF_STATEMENT_STATE_CONDITION);
            resume_state->statement.pointer->if_statement.condition = expression;
            resume_state->statement.if_state.id = IF_STATEMENT_STATE_CLOSE_CONDITION;
        }
        break; case PARSER_STATE_FOR_STATEMENT:
        {
            BUSTER_CHECK(resume_state->statement.for_state.id == FOR_STATEMENT_STATE_ITERABLE);
            resume_state->statement.pointer->for_statement.iterable = expression;
            resume_state->statement.for_state.id = FOR_STATEMENT_STATE_CLOSE;
        }
        break; case PARSER_STATE_LOOP_STATEMENT:
        {
            BUSTER_CHECK(resume_state->statement.loop_state.id == LOOP_STATEMENT_STATE_CONDITION);
            resume_state->statement.pointer->loop_statement.condition = expression;
            resume_state->statement.pointer->loop_statement.has_condition = true;
            resume_state->statement.loop_state.id = LOOP_STATEMENT_STATE_CLOSE_CONDITION;
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
        break; case TOKEN_SHIFT_LEFT_EQUAL: return S8("ShiftLeftEqual");
        break; case TOKEN_SHIFT_RIGHT: return S8("ShiftRight");
        break; case TOKEN_SHIFT_RIGHT_EQUAL: return S8("ShiftRightEqual");
        break; case TOKEN_PLUS: return S8("Plus");
        break; case TOKEN_PLUS_EQUAL: return S8("PlusEqual");
        break; case TOKEN_MINUS: return S8("Minus");
        break; case TOKEN_MINUS_EQUAL: return S8("MinusEqual");
        break; case TOKEN_ASTERISK: return S8("Asterisk");
        break; case TOKEN_ASTERISK_EQUAL: return S8("AsteriskEqual");
        break; case TOKEN_SLASH: return S8("Slash");
        break; case TOKEN_SLASH_EQUAL: return S8("SlashEqual");
        break; case TOKEN_COLON: return S8("Colon");
        break; case TOKEN_SEMICOLON: return S8("Semicolon");
        break; case TOKEN_COMMA: return S8("Comma");
        break; case TOKEN_DOT: return S8("Dot");
        break; case TOKEN_DOUBLE_DOT: return S8("DoubleDot");
        break; case TOKEN_TRIPLE_DOT: return S8("TripleDot");
        break; case TOKEN_AMPERSAND: return S8("Ampersand");
        break; case TOKEN_AMPERSAND_EQUAL: return S8("AmpersandEqual");
        break; case TOKEN_PERCENTAGE: return S8("Percent");
        break; case TOKEN_PERCENTAGE_EQUAL: return S8("PercentEqual");
        break; case TOKEN_BAR: return S8("Bar");
        break; case TOKEN_BAR_EQUAL: return S8("BarEqual");
        break; case TOKEN_CARET: return S8("Caret");
        break; case TOKEN_CARET_EQUAL: return S8("CaretEqual");
        break; case TOKEN_TILDE: return S8("Tilde");
        break; case TOKEN_AT: return S8("At");
        break; case TOKEN_KEYWORD_RETURN: return S8("Keyword_Return");
        break; case TOKEN_KEYWORD_IF: return S8("Keyword_If");
        break; case TOKEN_KEYWORD_ELSE: return S8("Keyword_Else");
        break; case TOKEN_KEYWORD_FUNCTION: return S8("Keyword_Function");
        break; case TOKEN_KEYWORD_FOR: return S8("Keyword_For");
        break; case TOKEN_KEYWORD_WHILE: return S8("Keyword_While");
        break; case TOKEN_KEYWORD_LOOP: return S8("Keyword_Loop");
        break; case TOKEN_KEYWORD_CODE: return S8("Keyword_Code");
        break; case TOKEN_KEYWORD_DATA: return S8("Keyword_Data");
        break; case TOKEN_KEYWORD_TYPE: return S8("Keyword_Type");
        break; case TOKEN_KEYWORD_STRUCT: return S8("Keyword_Struct");
        break; case TOKEN_KEYWORD_UNION: return S8("Keyword_Union");
        break; case TOKEN_KEYWORD_UNDEFINED: return S8("Keyword_Undefined");
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
    while (frame->id == PARSER_STATE_UNARY_PREFIX)
    {
        frame -= 1;
    }
    BUSTER_CHECK(frame->id == PARSER_STATE_EXPRESSION);
    return frame;
}

// Mark a prefix operand complete. Pending unary frames remain until the parser
// has seen the whole postfix chain, because indexing binds tighter than unary.
BUSTER_GLOBAL_LOCAL void expression_finish_operand(Parser* restrict parser, ParserState* restrict owner)
{
    BUSTER_CHECK(state(parser) == owner || state(parser)->id == PARSER_STATE_UNARY_PREFIX);
    owner->expression.state = EXPRESSION_STATE_TAIL;
}

BUSTER_GLOBAL_LOCAL void expression_finish_prefix_unaries(Parser* restrict parser, ParserState* restrict owner)
{
    while (state(parser)->id == PARSER_STATE_UNARY_PREFIX)
    {
        ParserState unary_frame = state_pop(&parser->state);
        expression_emit(parser, owner, (AstNodeId)unary_frame.unary_prefix.id);
    }
    BUSTER_CHECK(state(parser) == owner);
}

// Prefix position of an expression: either an operand or another prefix unary
// operator. Shared by the expression frame (PREFIX state) and the unary-prefix
// frames stacked on top of it.
BUSTER_GLOBAL_LOCAL void expression_parse_prefix(Parser* restrict parser)
{
    ExtendedToken token = peek(parser);

    switch (token.id)
    {
        break; case TOKEN_AT:
        {
            consume(&parser->iterator);
            ParserState* intrinsic = state_push(&parser->state);
            intrinsic->id = PARSER_STATE_INTRINSIC_CALL;
            intrinsic->intrinsic_call.range = source_range_from_token(token);
            intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_NAME;
            intrinsic->intrinsic_call.argument_count = 0;
        }
        break; case TOKEN_LEFT_BRACKET:
        {
            consume(&parser->iterator);

            ParserState* array_state = state_push(&parser->state);
            array_state->id = PARSER_STATE_ARRAY_LITERAL;
            array_state->array_literal.range = source_range_from_token(token);
            array_state->array_literal.state = ARRAY_LITERAL_STATE_ELEMENT_OR_END;
        }
        break; case TOKEN_LEFT_PARENTHESIS:
        {
            consume(&parser->iterator);

            ParserState* group_state = state_push(&parser->state);
            group_state->id = PARSER_STATE_EXPRESSION;
            group_state->expression.state = EXPRESSION_STATE_PREFIX;
            group_state->expression.end_token = TOKEN_RIGHT_PARENTHESIS;
            group_state->expression.is_group = true;
            group_state->expression.ends_at_assignment = false;
            group_state->expression.is_array_element = false;
            group_state->expression.ends_at_array_delimiter = false;
            group_state->expression.is_array_subscript_bound = false;
            group_state->expression.is_array_subscript_end = false;
            group_state->expression.ends_at_slice_operator = false;
            group_state->expression.is_intrinsic_argument = false;
            group_state->expression.ends_at_intrinsic_delimiter = false;
        }
        break; case TOKEN_IDENTIFIER:
        {
            consume(&parser->iterator);

            ParserState* owner = expression_owner(parser);
            AstNode* leaf = expression_emit(parser, owner, AST_NODE_IDENTIFIER);
            leaf->identifier = (AstIdentifier){
                .text = get_string(parser->iterator.source, token),
                .range = source_range_from_token(token),
            };

            expression_finish_operand(parser, owner);
        }
        break; case TOKEN_KEYWORD_UNDEFINED:
        {
            consume(&parser->iterator);

            ParserState* owner = expression_owner(parser);
            expression_emit(parser, owner, AST_NODE_UNDEFINED);
            expression_finish_operand(parser, owner);
        }
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
            AstNode* leaf = expression_emit(parser, owner, AST_NODE_CONSTANT_INTEGER);
            leaf->integer = (AstIntegerLiteral){
                .spelling = number_string,
                .value = number_parsing.value,
                .base = number_parsing.base,
                .fits_u64 = number_parsing.fits_u64,
            };

            expression_finish_operand(parser, owner);
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
            unary_state->id = PARSER_STATE_UNARY_PREFIX;
            unary_state->unary_prefix.id = (u8)unary_id;
        }
        break; default:
        {
            parser_diagnostic_push(
                    parser,
                    PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION,
                    token,
                    TOKEN_ERROR,
                    S8("expected expression"));
            parser->recovery = PARSER_RECOVERY_STATEMENT;
        }
    }
}

BUSTER_GLOBAL_LOCAL void finish_array_literal(Parser* restrict parser, ExtendedToken closing_bracket)
{
    ParserState array_state = state_pop(&parser->state);
    BUSTER_CHECK(array_state.id == PARSER_STATE_ARRAY_LITERAL);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_ARRAY_LITERAL);
    ParserSourceRange range = array_state.array_literal.range;
    parser_source_range_set_end(&range, closing_bracket.offset + closing_bracket.length);
    node->array_literal = (AstArrayLiteral){
        .range = range,
        .element_count = array_state.array_literal.element_count,
    };
    expression_finish_operand(parser, owner);
}

BUSTER_GLOBAL_LOCAL void finish_array_subscript(Parser* restrict parser, ExtendedToken closing_bracket, bool is_slice)
{
    ParserState subscript = state_pop(&parser->state);
    BUSTER_CHECK(subscript.id == PARSER_STATE_ARRAY_SUBSCRIPT);

    ParserSourceRange range = subscript.array_subscript.range;
    parser_source_range_set_end(&range, closing_bracket.offset + closing_bracket.length);

    ParserState* owner = expression_owner(parser);
    if (is_slice)
    {
        AstNode* node = expression_emit(parser, owner, AST_NODE_ARRAY_SLICE);
        node->array_slice = (AstArraySlice){
            .range = range,
            .has_start = subscript.array_subscript.has_start,
            .has_end = subscript.array_subscript.has_end,
        };
    }
    else
    {
        BUSTER_CHECK(subscript.array_subscript.has_start);
        AstNode* node = expression_emit(parser, owner, AST_NODE_ARRAY_INDEX);
        node->array_index.range = range;
    }
    expression_finish_operand(parser, owner);
}

BUSTER_GLOBAL_LOCAL void finish_intrinsic_call(Parser* restrict parser, ExtendedToken closing_parenthesis)
{
    ParserState intrinsic = state_pop(&parser->state);
    BUSTER_CHECK(intrinsic.id == PARSER_STATE_INTRINSIC_CALL);

    ParserSourceRange range = intrinsic.intrinsic_call.range;
    parser_source_range_set_end(&range, closing_parenthesis.offset + closing_parenthesis.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_INTRINSIC_CALL);
    node->intrinsic_call = (AstIntrinsicCall){
        .name = intrinsic.intrinsic_call.name,
        .range = range,
        .argument_count = intrinsic.intrinsic_call.argument_count,
    };
    expression_finish_operand(parser, owner);
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

        ParserState* original_state = state(&parser);
        switch (original_state->id)
        {
            break; case PARSER_STATE_COUNT: BUSTER_UNREACHABLE();
            break; case PARSER_STATE_ROOT:
            {
                ExtendedToken token = peek_and_consume(&parser);

                switch (token.id)
                {
                    break; case TOKEN_KEYWORD_CODE:
                    {
                        ParserState* function_state = state_push(&parser.state);
                        function_state->id = PARSER_STATE_CODE;
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
            break; case PARSER_STATE_CODE:
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
                                attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
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
                                type_state->id = PARSER_STATE_TYPE_REFERENCE;
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
                                attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
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
            break; case PARSER_STATE_TYPE_REFERENCE:
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
                            break;
                            case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
                            case TOKEN_DECIMAL_INTEGER_LITERAL:
                            case TOKEN_OCTAL_INTEGER_LITERAL:
                            case TOKEN_BINARY_INTEGER_LITERAL:
                            {
                                consume(&parser.iterator);
                                String8 count_string = get_string(parser.iterator.source, token);
                                IntegerLiteralParsing count = parse_integer_literal(count_string, token.id);
                                if (!count.valid)
                                {
                                    parser_diagnostic_push(&parser, PARSER_DIAGNOSTIC_INVALID_INTEGER, token, TOKEN_ERROR, S8("invalid array count"));
                                }

                                ParserSourceRange range = type_state->type.prefix_range;
                                AstType* array = parser_type_attach(&parser, type_state, AST_TYPE_ARRAY, (ExtendedToken){
                                    .offset = range.offset, .length = range.length, .line = range.line, .column = range.column,
                                });
                                array->array.count = (AstIntegerLiteral){
                                    .spelling = count_string,
                                    .value = count.value,
                                    .base = count.base,
                                    .fits_u64 = count.fits_u64,
                                };
                                type_state->type.destination = &array->array.element_type;
                                type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_COUNT;
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
                                attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
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
                        child_type_state->id = PARSER_STATE_TYPE_REFERENCE;
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
                        child_type_state->id = PARSER_STATE_TYPE_REFERENCE;
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
            break; case PARSER_STATE_ATTRIBUTE_LIST:
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
            break; case PARSER_STATE_BLOCK:
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
                        statement_state->id = PARSER_STATE_STATEMENT;
                        statement_state->statement.statement_state = STATEMENT_STATE_START;
                    }
                }
            }
            break; case PARSER_STATE_STATEMENT:
            {
                ParserState* statement_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (statement_state->statement.statement_state)
                {
                    break; case STATEMENT_STATE_START:
                    {
                        statement_state->statement.statement_state = STATEMENT_STATE_END;
                        statement_state->statement.end_token = block_end_of_statement_token;

                        ParserState* block_state = state_previous(&parser.state);
                        BUSTER_CHECK(block_state->id == PARSER_STATE_BLOCK);

                        switch (token.id)
                        {
                            break; case TOKEN_KEYWORD_RETURN:
                            {
                                consume(&parser.iterator);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_RETURN, token);
                                statement_state->statement.pointer = statement;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_RETURN_STATEMENT;
                                state->statement.return_state.id = RETURN_STATEMENT_STATE_VALUE_OR_END;
                                state->statement.pointer = statement;
                                state->statement.end_token = statement_state->statement.end_token;
                            }
                            break; case TOKEN_KEYWORD_DATA:
                            {
                                consume(&parser.iterator);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_DATA, token);
                                statement_state->statement.pointer = statement;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_DATA_STATEMENT;
                                state->statement.data_state.id = DATA_STATEMENT_STATE_NAME;
                                state->statement.pointer = statement;
                                state->statement.end_token = statement_state->statement.end_token;
                            }
                            break; case TOKEN_KEYWORD_IF:
                            {
                                consume(&parser.iterator);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_IF, token);
                                statement_state->statement.pointer = statement;
                                statement_state->statement.end_token = TOKEN_ERROR;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_IF_STATEMENT;
                                state->statement.if_state.id = IF_STATEMENT_STATE_OPEN_CONDITION;
                                state->statement.pointer = statement;
                                state->statement.end_token = TOKEN_ERROR;
                            }
                            break; case TOKEN_KEYWORD_FOR:
                            {
                                consume(&parser.iterator);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_FOR, token);
                                statement_state->statement.pointer = statement;
                                statement_state->statement.end_token = TOKEN_ERROR;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_FOR_STATEMENT;
                                state->statement.for_state.id = FOR_STATEMENT_STATE_OPEN;
                                state->statement.pointer = statement;
                                state->statement.end_token = TOKEN_ERROR;
                            }
                            break; case TOKEN_KEYWORD_LOOP:
                            {
                                consume(&parser.iterator);
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_LOOP, token);
                                statement_state->statement.pointer = statement;
                                statement_state->statement.end_token = TOKEN_ERROR;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_LOOP_STATEMENT;
                                state->statement.loop_state.id = LOOP_STATEMENT_STATE_CONDITION_OR_BODY;
                                state->statement.pointer = statement;
                                state->statement.end_token = TOKEN_ERROR;
                            }
                            break;
                            case TOKEN_IDENTIFIER:
                            case TOKEN_LEFT_PARENTHESIS:
                            case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
                            case TOKEN_DECIMAL_INTEGER_LITERAL:
                            case TOKEN_OCTAL_INTEGER_LITERAL:
                            case TOKEN_BINARY_INTEGER_LITERAL:
                            case TOKEN_KEYWORD_UNDEFINED:
                            case TOKEN_MINUS:
                            case TOKEN_PLUS:
                            case TOKEN_BANG:
                            case TOKEN_TILDE:
                            {
                                AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_ASSIGNMENT, token);
                                statement_state->statement.pointer = statement;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_STATE_ASSIGNMENT_STATEMENT;
                                state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_TARGET;
                                state->statement.pointer = statement;
                                state->statement.end_token = statement_state->statement.end_token;
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
                            statement_state->statement.pointer->range.length = token.offset + token.length - statement_state->statement.pointer->range.offset;
                        }

                        state_pop(&parser.state);
                    }
                    break; case STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_RETURN_STATEMENT:
            {
                ParserState* return_statement_state = state(&parser);
                ExtendedToken token = peek(&parser);

                TokenId end_of_statement_token = return_statement_state->statement.end_token;

                switch (return_statement_state->statement.return_state.id)
                {
                    break; case RETURN_STATEMENT_STATE_VALUE_OR_END:
                    {
                        if (token.id == end_of_statement_token)
                        {
                            return_statement_state->statement.return_state.id = RETURN_STATEMENT_STATE_END;
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
            break; case PARSER_STATE_DATA_STATEMENT:
            {
                ParserState* data_statement_state = state(&parser);
                ExtendedToken token = peek(&parser);

                TokenIdEnum start_token_id = (TokenIdEnum)token.id;
                TokenId end_of_statement_token = data_statement_state->statement.end_token;

                BUSTER_UNUSED(end_of_statement_token);

                switch (data_statement_state->statement.data_state.id)
                {
                    break; case DATA_STATEMENT_STATE_NAME:
                    {
                        switch (start_token_id)
                        {
                            break; case TOKEN_IDENTIFIER:
                            {
                                consume(&parser.iterator);
                                String8 name = get_string(parser.iterator.source, token);
                                ParserSourceRange range = source_range_from_token(token);
                                data_statement_state->statement.pointer->data_statement.name = (AstIdentifier) {
                                    .range = range,
                                    .text = name,
                                };
                                data_statement_state->statement.data_state.id = DATA_STATEMENT_STATE_AFTER_NAME;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                        }
                    }
                    break; case DATA_STATEMENT_STATE_AFTER_NAME:
                    {
                        if (start_token_id == TOKEN_COLON)
                        {
                            consume(&parser.iterator);

                            ParserState* type_state = state_push(&parser.state);
                            type_state->id = PARSER_STATE_TYPE_REFERENCE;
                            type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                            type_state->type.code = 0;
                            type_state->type.destination = &data_statement_state->statement.pointer->data_statement.type;
                        }

                        data_statement_state->statement.data_state.id = DATA_STATEMENT_STATE_AFTER_TYPE;
                    }
                    break; case DATA_STATEMENT_STATE_AFTER_TYPE:
                    {
                        switch (start_token_id)
                        {
                            break; case TOKEN_EQUAL:
                            {
                                consume(&parser.iterator);
                                data_statement_state->statement.data_state.id = DATA_STATEMENT_STATE_INITIALIZER;
                            }
                            break; default: parser_unexpected(&parser, token, TOKEN_EQUAL);
                        }
                    }
                    break; case DATA_STATEMENT_STATE_INITIALIZER:
                    {
                        parse_expression(&parser, data_statement_state->statement.end_token);
                    }
                    break; case DATA_STATEMENT_STATE_END:
                    {
                        if (token.id != end_of_statement_token)
                        {
                            parser_unexpected(&parser, token, end_of_statement_token);
                            continue;
                        }

                        state_pop(&parser.state);
                    }
                    break; case DATA_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_ASSIGNMENT_STATEMENT:
            {
                ParserState* assignment_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (assignment_state->statement.assignment_state.id)
                {
                    break; case ASSIGNMENT_STATEMENT_STATE_TARGET:
                    {
                        parse_assignment_target(&parser);
                    }
                    break; case ASSIGNMENT_STATEMENT_STATE_OPERATOR:
                    {
                        if (!token_is_assignment_operator(token.id))
                        {
                            parser_expected_assignment_operator(&parser, token);
                            continue;
                        }

                        assignment_state->statement.pointer->assignment_statement.operator = assignment_operator_from_token(token.id);
                        consume(&parser.iterator);
                        assignment_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_VALUE;
                    }
                    break; case ASSIGNMENT_STATEMENT_STATE_VALUE:
                    {
                        parse_expression(&parser, assignment_state->statement.end_token);
                    }
                    break; case ASSIGNMENT_STATEMENT_STATE_END:
                    {
                        if (token.id != assignment_state->statement.end_token)
                        {
                            parser_unexpected(&parser, token, assignment_state->statement.end_token);
                            continue;
                        }

                        state_pop(&parser.state);
                    }
                    break; case ASSIGNMENT_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_IF_STATEMENT:
            {
                ParserState* if_state = state(&parser);
                ExtendedToken token = peek(&parser);
                AstStatement* statement = if_state->statement.pointer;

                switch (if_state->statement.if_state.id)
                {
                    break; case IF_STATEMENT_STATE_OPEN_CONDITION:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                            continue;
                        }

                        consume(&parser.iterator);
                        if_state->statement.if_state.id = IF_STATEMENT_STATE_CONDITION;
                    }
                    break; case IF_STATEMENT_STATE_CONDITION:
                    {
                        parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
                    }
                    break; case IF_STATEMENT_STATE_CLOSE_CONDITION:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                            continue;
                        }
                        consume(&parser.iterator);

                        ExtendedToken opening_brace = peek(&parser);
                        if (opening_brace.id != TOKEN_LEFT_BRACE)
                        {
                            parser_unexpected(&parser, opening_brace, TOKEN_LEFT_BRACE);
                            continue;
                        }
                        consume(&parser.iterator);

                        if_state->statement.if_state.id = IF_STATEMENT_STATE_THEN_BLOCK;
                        parse_block(&parser, &statement->if_statement.then_block, opening_brace);
                    }
                    break; case IF_STATEMENT_STATE_THEN_BLOCK:
                    {
                        if_state->statement.if_state.id = IF_STATEMENT_STATE_ELSE_OR_END;
                    }
                    break; case IF_STATEMENT_STATE_ELSE_OR_END:
                    {
                        if (token.id == TOKEN_KEYWORD_ELSE)
                        {
                            consume(&parser.iterator);

                            ExtendedToken opening_brace = peek(&parser);
                            if (opening_brace.id != TOKEN_LEFT_BRACE)
                            {
                                parser_unexpected(&parser, opening_brace, TOKEN_LEFT_BRACE);
                                continue;
                            }
                            consume(&parser.iterator);

                            statement->if_statement.has_else = true;
                            if_state->statement.if_state.id = IF_STATEMENT_STATE_ELSE_BLOCK;
                            parse_block(&parser, &statement->if_statement.else_block, opening_brace);
                        }
                        else
                        {
                            AstBlock* then_block = &statement->if_statement.then_block;
                            statement->range.length = then_block->range.offset + then_block->range.length - statement->range.offset;
                            state_pop(&parser.state);
                        }
                    }
                    break; case IF_STATEMENT_STATE_ELSE_BLOCK:
                    {
                        AstBlock* else_block = &statement->if_statement.else_block;
                        statement->range.length = else_block->range.offset + else_block->range.length - statement->range.offset;
                        state_pop(&parser.state);
                    }
                    break; case IF_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_FOR_STATEMENT:
            {
                ParserState* for_state = state(&parser);
                ExtendedToken token = peek(&parser);
                AstStatement* statement = for_state->statement.pointer;

                switch (for_state->statement.for_state.id)
                {
                    break; case FOR_STATEMENT_STATE_OPEN:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                            continue;
                        }
                        consume(&parser.iterator);
                        for_state->statement.for_state.id = FOR_STATEMENT_STATE_DATA;
                    }
                    break; case FOR_STATEMENT_STATE_DATA:
                    {
                        if (token.id != TOKEN_KEYWORD_DATA)
                        {
                            parser_unexpected(&parser, token, TOKEN_KEYWORD_DATA);
                            continue;
                        }
                        consume(&parser.iterator);
                        for_state->statement.for_state.id = FOR_STATEMENT_STATE_NAME;
                    }
                    break; case FOR_STATEMENT_STATE_NAME:
                    {
                        if (token.id != TOKEN_IDENTIFIER)
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            continue;
                        }
                        consume(&parser.iterator);
                        statement->for_statement.name = (AstIdentifier){
                            .text = get_string(parser.iterator.source, token),
                            .range = source_range_from_token(token),
                        };
                        for_state->statement.for_state.id = FOR_STATEMENT_STATE_TYPE_OR_EQUAL;
                    }
                    break; case FOR_STATEMENT_STATE_TYPE_OR_EQUAL:
                    {
                        if (token.id == TOKEN_COLON)
                        {
                            consume(&parser.iterator);
                            for_state->statement.for_state.id = FOR_STATEMENT_STATE_TYPE;

                            ParserState* type_state = state_push(&parser.state);
                            type_state->id = PARSER_STATE_TYPE_REFERENCE;
                            type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                            type_state->type.code = 0;
                            type_state->type.destination = &statement->for_statement.type;
                        }
                        else
                        {
                            for_state->statement.for_state.id = FOR_STATEMENT_STATE_EQUAL;
                        }
                    }
                    break; case FOR_STATEMENT_STATE_TYPE:
                    {
                        BUSTER_UNREACHABLE();
                    }
                    break; case FOR_STATEMENT_STATE_EQUAL:
                    {
                        if (token.id != TOKEN_EQUAL)
                        {
                            parser_unexpected(&parser, token, TOKEN_EQUAL);
                            continue;
                        }
                        consume(&parser.iterator);
                        for_state->statement.for_state.id = FOR_STATEMENT_STATE_ITERABLE;
                    }
                    break; case FOR_STATEMENT_STATE_ITERABLE:
                    {
                        parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
                    }
                    break; case FOR_STATEMENT_STATE_CLOSE:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                            continue;
                        }
                        consume(&parser.iterator);

                        ExtendedToken opening_brace = peek(&parser);
                        if (opening_brace.id != TOKEN_LEFT_BRACE)
                        {
                            parser_unexpected(&parser, opening_brace, TOKEN_LEFT_BRACE);
                            continue;
                        }
                        consume(&parser.iterator);
                        for_state->statement.for_state.id = FOR_STATEMENT_STATE_BODY;
                        parse_block(&parser, &statement->for_statement.body, opening_brace);
                    }
                    break; case FOR_STATEMENT_STATE_BODY:
                    {
                        AstBlock* body = &statement->for_statement.body;
                        statement->range.length = body->range.offset + body->range.length - statement->range.offset;
                        state_pop(&parser.state);
                    }
                    break; case FOR_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_LOOP_STATEMENT:
            {
                ParserState* loop_state = state(&parser);
                ExtendedToken token = peek(&parser);
                AstStatement* statement = loop_state->statement.pointer;

                switch (loop_state->statement.loop_state.id)
                {
                    break; case LOOP_STATEMENT_STATE_CONDITION_OR_BODY:
                    {
                        if (token.id == TOKEN_LEFT_PARENTHESIS)
                        {
                            consume(&parser.iterator);
                            loop_state->statement.loop_state.id = LOOP_STATEMENT_STATE_CONDITION;
                        }
                        else if (token.id == TOKEN_LEFT_BRACE)
                        {
                            consume(&parser.iterator);
                            loop_state->statement.loop_state.id = LOOP_STATEMENT_STATE_BODY;
                            parse_block(&parser, &statement->loop_statement.body, token);
                        }
                        else
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_BRACE);
                        }
                    }
                    break; case LOOP_STATEMENT_STATE_CONDITION:
                    {
                        parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
                    }
                    break; case LOOP_STATEMENT_STATE_CLOSE_CONDITION:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                            continue;
                        }
                        consume(&parser.iterator);

                        ExtendedToken opening_brace = peek(&parser);
                        if (opening_brace.id != TOKEN_LEFT_BRACE)
                        {
                            parser_unexpected(&parser, opening_brace, TOKEN_LEFT_BRACE);
                            continue;
                        }
                        consume(&parser.iterator);
                        loop_state->statement.loop_state.id = LOOP_STATEMENT_STATE_BODY;
                        parse_block(&parser, &statement->loop_statement.body, opening_brace);
                    }
                    break; case LOOP_STATEMENT_STATE_BODY:
                    {
                        AstBlock* body = &statement->loop_statement.body;
                        statement->range.length = body->range.offset + body->range.length - statement->range.offset;
                        state_pop(&parser.state);
                    }
                    break; case LOOP_STATEMENT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_ARRAY_LITERAL:
            {
                ParserState* array_state = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (array_state->array_literal.state)
                {
                    break; case ARRAY_LITERAL_STATE_ELEMENT_OR_END:
                    {
                        if (token.id == TOKEN_RIGHT_BRACKET)
                        {
                            consume(&parser.iterator);
                            finish_array_literal(&parser, token);
                        }
                        else
                        {
                            parse_array_element(&parser);
                        }
                    }
                    break; case ARRAY_LITERAL_STATE_DELIMITER:
                    {
                        if (token.id == TOKEN_COMMA)
                        {
                            consume(&parser.iterator);
                            array_state->array_literal.state = ARRAY_LITERAL_STATE_ELEMENT_OR_END;
                        }
                        else if (token.id == TOKEN_RIGHT_BRACKET)
                        {
                            consume(&parser.iterator);
                            finish_array_literal(&parser, token);
                        }
                        else
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                        }
                    }
                    break; case ARRAY_LITERAL_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_ARRAY_SUBSCRIPT:
            {
                ParserState* subscript = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (subscript->array_subscript.state)
                {
                    break; case ARRAY_SUBSCRIPT_STATE_START_OR_RANGE:
                    {
                        if (token.id == TOKEN_DOUBLE_DOT)
                        {
                            consume(&parser.iterator);
                            subscript->array_subscript.state = ARRAY_SUBSCRIPT_STATE_END_OR_CLOSE;
                        }
                        else
                        {
                            parse_array_subscript_bound(&parser, false);
                        }
                    }
                    break; case ARRAY_SUBSCRIPT_STATE_AFTER_START:
                    {
                        if (token.id == TOKEN_RIGHT_BRACKET)
                        {
                            consume(&parser.iterator);
                            finish_array_subscript(&parser, token, false);
                        }
                        else
                        {
                            BUSTER_CHECK(token.id == TOKEN_DOUBLE_DOT);
                            consume(&parser.iterator);
                            subscript->array_subscript.state = ARRAY_SUBSCRIPT_STATE_END_OR_CLOSE;
                        }
                    }
                    break; case ARRAY_SUBSCRIPT_STATE_END_OR_CLOSE:
                    {
                        if (token.id == TOKEN_RIGHT_BRACKET)
                        {
                            consume(&parser.iterator);
                            finish_array_subscript(&parser, token, true);
                        }
                        else
                        {
                            parse_array_subscript_bound(&parser, true);
                        }
                    }
                    break; case ARRAY_SUBSCRIPT_STATE_CLOSE:
                    {
                        BUSTER_CHECK(token.id == TOKEN_RIGHT_BRACKET);
                        consume(&parser.iterator);
                        finish_array_subscript(&parser, token, true);
                    }
                    break; case ARRAY_SUBSCRIPT_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_INTRINSIC_CALL:
            {
                ParserState* intrinsic = state(&parser);
                ExtendedToken token = peek(&parser);

                switch (intrinsic->intrinsic_call.state)
                {
                    break; case INTRINSIC_CALL_STATE_NAME:
                    {
                        if (token.id != TOKEN_IDENTIFIER)
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            continue;
                        }
                        consume(&parser.iterator);
                        intrinsic->intrinsic_call.name = (AstIdentifier){
                            .text = get_string(parser.iterator.source, token),
                            .range = source_range_from_token(token),
                        };
                        intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_OPEN;
                    }
                    break; case INTRINSIC_CALL_STATE_OPEN:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                            continue;
                        }
                        consume(&parser.iterator);
                        intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE;
                    }
                    break; case INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE:
                    {
                        if (token.id == TOKEN_RIGHT_PARENTHESIS)
                        {
                            consume(&parser.iterator);
                            finish_intrinsic_call(&parser, token);
                        }
                        else
                        {
                            parse_intrinsic_argument(&parser);
                        }
                    }
                    break; case INTRINSIC_CALL_STATE_DELIMITER:
                    {
                        if (token.id == TOKEN_COMMA)
                        {
                            consume(&parser.iterator);
                            intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE;
                        }
                        else if (token.id == TOKEN_RIGHT_PARENTHESIS)
                        {
                            consume(&parser.iterator);
                            finish_intrinsic_call(&parser, token);
                        }
                        else
                        {
                            parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                        }
                    }
                    break; case INTRINSIC_CALL_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_EXPRESSION:
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
                        bool at_end = token.id == st->expression.end_token ||
                                      (st->expression.ends_at_assignment && token_is_assignment_operator(token.id)) ||
                                      (st->expression.ends_at_array_delimiter &&
                                       (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_BRACKET)) ||
                                      (st->expression.ends_at_slice_operator && token.id == TOKEN_DOUBLE_DOT) ||
                                      (st->expression.ends_at_intrinsic_delimiter &&
                                       (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_PARENTHESIS));
                        if (at_end)
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
                                case TOKEN_DOUBLE_DOT:
                                {
                                    if (token.id == TOKEN_DOUBLE_DOT &&
                                        st->expression.is_array_subscript_bound &&
                                        st->expression.is_array_subscript_end)
                                    {
                                        parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                                        continue;
                                    }
                                    if (token.id == TOKEN_DOUBLE_DOT)
                                    {
                                        for (u16 operator_i = 0; operator_i < st->expression.operator_count; operator_i += 1)
                                        {
                                            if ((AstNodeId)st->expression.operator_stack[operator_i] == AST_NODE_BINARY_RANGE)
                                            {
                                                parser_chained_range(&parser, token);
                                                break;
                                            }
                                        }
                                        if (parser.recovery != PARSER_RECOVERY_NONE)
                                        {
                                            continue;
                                        }
                                    }
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
                                        break; case TOKEN_DOUBLE_DOT: binary_node_id = AST_NODE_BINARY_RANGE;
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
                                break; case TOKEN_LEFT_BRACKET:
                                {
                                    consume(&parser.iterator);
                                    parse_array_subscript(&parser, token);
                                }
                                break; default:
                                {
                                    if (st->expression.ends_at_assignment)
                                    {
                                        parser_expected_assignment_operator(&parser, token);
                                    }
                                    else if (st->expression.ends_at_array_delimiter)
                                    {
                                        parser_expected_array_delimiter(&parser, token);
                                    }
                                    else
                                    {
                                        parser_unexpected(&parser, token, st->expression.end_token);
                                    }
                                }
                            }
                        }
                    }
                    break; case EXPRESSION_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_STATE_UNARY_PREFIX:
            {
                ParserState* owner = expression_owner(&parser);
                if (owner->expression.state == EXPRESSION_STATE_PREFIX)
                {
                    expression_parse_prefix(&parser);
                }
                else
                {
                    ExtendedToken token = peek(&parser);
                    if (token.id == TOKEN_LEFT_BRACKET)
                    {
                        consume(&parser.iterator);
                        parse_array_subscript(&parser, token);
                    }
                    else
                    {
                        expression_finish_prefix_unaries(&parser, owner);
                    }
                }
            }
            break; case PARSER_STATE_TYPE_STATEMENT:
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
        else switch (arity)
        {
            break; case 0:
            {
                switch (node->id)
                {
                    break; case AST_NODE_CONSTANT_INTEGER:
                    {
                        formatted = node->integer.fits_u64 ? string_format(arena, S8("{u64}"), node->integer.value) : node->integer.spelling;
                    }
                    break; case AST_NODE_IDENTIFIER:
                    {
                        formatted = node->identifier.text;
                    }
                    break; case AST_NODE_UNDEFINED:
                    {
                        formatted = S8("undefined");
                    }
                    break; default: BUSTER_TODO();
                }
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

BUSTER_GLOBAL_LOCAL ParserFileTestCase parser_file_test_cases[] =
{
    {
        .path = S8_INITIALIZER("tests/basic_minimal.bbb"),
        .expected_expression = S8_INITIALIZER("0")
    },
    {
        .path = S8_INITIALIZER("tests/basic_comment.bbb"),
        .expected_expression = S8_INITIALIZER("0")
    },
    {
        .path = S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"),
        .expected_expression = S8_INITIALIZER("0")
    },
    {
        .path = S8_INITIALIZER("tests/basic_octal_literal.bbb"),
        .expected_expression = S8_INITIALIZER("0")
    },
    {
        .path = S8_INITIALIZER("tests/basic_binary_literal.bbb"),
        .expected_expression = S8_INITIALIZER("0")
    },
    {
        .path = S8_INITIALIZER("tests/basic_unary_minus.bbb"),
        .expected_expression = S8_INITIALIZER("(neg 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_unary_plus.bbb"),
        .expected_expression = S8_INITIALIZER("(pos 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_add.bbb"),
        .expected_expression = S8_INITIALIZER("(+ 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"),
        .expected_expression = S8_INITIALIZER("(- 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"),
        .expected_expression = S8_INITIALIZER("(* 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"),
        .expected_expression = S8_INITIALIZER("(/ 0 1)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"),
        .expected_expression = S8_INITIALIZER("(% 0 1)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"),
        .expected_expression = S8_INITIALIZER("(<< 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"),
        .expected_expression = S8_INITIALIZER("(>> 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_and.bbb"),
        .expected_expression = S8_INITIALIZER("(& 0 1)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_or.bbb"),
        .expected_expression = S8_INITIALIZER("(| 0 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"),
        .expected_expression = S8_INITIALIZER("(^ 1 1)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"),
        .expected_expression = S8_INITIALIZER("(!= (== (< 1 2) 3) (> (>= (<= 4 5) 6) 7))")
    },
    {
        .path = S8_INITIALIZER("tests/basic_logical_not.bbb"),
        .expected_expression = S8_INITIALIZER("(not (not 0))")
    },
    {
        .path = S8_INITIALIZER("tests/basic_bitwise_not.bbb"),
        .expected_expression = S8_INITIALIZER("(bit_not 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"),
        .expected_expression = S8_INITIALIZER("(<< (+ 1 (* 2 3)) (- 4 5))")
    },
    {
        .path = S8_INITIALIZER("tests/basic_variable.bbb"),
        .expected_expression = S8_INITIALIZER("result")
    },
    {
        .path = S8_INITIALIZER("tests/basic_array_literal.bbb"),
        .expected_expression = S8_INITIALIZER("(index result 0)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_assignment.bbb"),
        .expected_expression = S8_INITIALIZER("result")
    },
    {
        .path = S8_INITIALIZER("tests/basic_if_else.bbb"),
        .expected_expression = S8_INITIALIZER("(+ a b)")
    },
    {
        .path = S8_INITIALIZER("tests/basic_for.bbb"),
        .expected_expression = S8_INITIALIZER("total")
    },
    {
        .path = S8_INITIALIZER("tests/basic_loop.bbb"),
        .expected_expression = S8_INITIALIZER("value")
    },
    {
        .path = S8_INITIALIZER("tests/array_slices.bbb"),
        .expected_expression = S8_INITIALIZER("(- total_a total_b)")
    },
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
typedef struct AssignmentOperatorTestCase AssignmentOperatorTestCase;
struct AssignmentOperatorTestCase
{
    String8 spelling;
    TokenId token;
    AstAssignmentOperator operator;
};

BUSTER_GLOBAL_LOCAL AssignmentOperatorTestCase assignment_operator_test_cases[] =
{
    { S8_INITIALIZER("="),   TOKEN_EQUAL,             AST_ASSIGNMENT_EQUAL },
    { S8_INITIALIZER("+="),  TOKEN_PLUS_EQUAL,        AST_ASSIGNMENT_PLUS_EQUAL },
    { S8_INITIALIZER("-="),  TOKEN_MINUS_EQUAL,       AST_ASSIGNMENT_MINUS_EQUAL },
    { S8_INITIALIZER("*="),  TOKEN_ASTERISK_EQUAL,    AST_ASSIGNMENT_MULTIPLY_EQUAL },
    { S8_INITIALIZER("/="),  TOKEN_SLASH_EQUAL,       AST_ASSIGNMENT_DIVIDE_EQUAL },
    { S8_INITIALIZER("%="),  TOKEN_PERCENTAGE_EQUAL,  AST_ASSIGNMENT_MODULO_EQUAL },
    { S8_INITIALIZER("<<="), TOKEN_SHIFT_LEFT_EQUAL,  AST_ASSIGNMENT_SHIFT_LEFT_EQUAL },
    { S8_INITIALIZER(">>="), TOKEN_SHIFT_RIGHT_EQUAL, AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL },
    { S8_INITIALIZER("&="),  TOKEN_AMPERSAND_EQUAL,   AST_ASSIGNMENT_BITWISE_AND_EQUAL },
    { S8_INITIALIZER("|="),  TOKEN_BAR_EQUAL,         AST_ASSIGNMENT_BITWISE_OR_EQUAL },
    { S8_INITIALIZER("^="),  TOKEN_CARET_EQUAL,       AST_ASSIGNMENT_BITWISE_XOR_EQUAL },
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
    return parsed.first_code->body.first_statement->return_statement.expression;
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
        { S8("value"),               S8("value") },
        { S8("value + 2 * count"),   S8("(+ value (* 2 count))") },
        { S8("left * 3 + right / 4"), S8("(+ (* left 3) (/ right 4))") },
        { S8("value - 1 - offset"),  S8("(- (- value 1) offset)") },
        { S8("value << 1 + offset"), S8("(<< value (+ 1 offset))") },
        { S8("value + 1 < limit << 2"), S8("(< (+ value 1) (<< limit 2))") },
        { S8("flags & mask == 3"),   S8("(& flags (== mask 3))") },
        { S8("a | b ^ 1"),           S8("(^ (| a b) 1)") },
        { S8("(value + 2) * count"), S8("(* (+ value 2) count)") },
        { S8("value * (count + 3)"), S8("(* value (+ count 3))") },
        { S8("(a + b) * (c - 2)"),   S8("(* (+ a b) (- c 2))") },
        { S8("((a + 1) * (b - 2)) << shift"), S8("(<< (* (+ a 1) (- b 2)) shift)") },
        { S8("-(value + 2) * count"), S8("(* (neg (+ value 2)) count)") },
        { S8("!(value == 0)"),       S8("(not (== value 0))") },
        { S8("~(mask | 3) & flags"), S8("(& (bit_not (| mask 3)) flags)") },
        { S8("undefined"),           S8("undefined") },
        { S8("[]"),                  S8("[]") },
        { S8("[1]"),                 S8("[1]") },
        { S8("[1, 2 + 3, value]"),   S8("[1, (+ 2 3), value]") },
        { S8("[[1, 2], [3]]"),       S8("[[1, 2], [3]]") },
        { S8("[1,]"),                S8("[1]") },
        { S8("values[0]"),           S8("(index values 0)") },
        { S8("values[1 + 2]"),       S8("(index values (+ 1 2))") },
        { S8("values[0][1]"),        S8("(index (index values 0) 1)") },
        { S8("-values[0]"),          S8("(neg (index values 0))") },
        { S8("[1, 2][0]"),           S8("(index [1, 2] 0)") },
        { S8("values[..]"),          S8("values[..]") },
        { S8("values[1..]"),         S8("values[1..]") },
        { S8("values[..3]"),         S8("values[..3]") },
        { S8("values[1 + 1..4 - 1]"), S8("values[(+ 1 1)..(- 4 1)]") },
        { S8("values[1..3][0]"),     S8("(index values[1..3] 0)") },
        { S8("-values[..]"),         S8("(neg values[..])") },
        { S8("[1, 2][..]"),          S8("[1, 2][..]") },
        { S8("0 .. n"),              S8("(range 0 n)") },
        { S8("1 + 2 .. n * 2"),      S8("(range (+ 1 2) (* n 2))") },
        { S8("@reverse(0 .. n)"),     S8("(@reverse (range 0 n))") },
        { S8("@intrinsic()"),         S8("(@intrinsic)") },
        { S8("@intrinsic(1, 2 + 3)"), S8("(@intrinsic 1 (+ 2 3))") },
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
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->return_statement.expression.count == 1);
                BUSTER_TEST(arguments, parsed.first_code->body.first_statement->return_statement.expression.nodes[0].integer.value == 1);
                BUSTER_TEST(arguments, parsed.last_code->body.first_statement->return_statement.expression.nodes[0].integer.value == 2);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    data result: s32 = undefined;\n"
            "    result + offset = source + 1;\n"
            "    return result;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
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
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    data values: [3]s32 = [1, 2 + 3, 4];\n"
            "    return values;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
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
                BUSTER_TEST(arguments, array_type->array.element_type != 0 &&
                        array_type->array.element_type->id == AST_TYPE_NAMED);
            }

            AstExpression initializer = data->data_statement.initializer;
            BUSTER_TEST(arguments, initializer.count == 6);
            if (initializer.count == 6)
            {
                AstNode* literal = &initializer.nodes[5];
                BUSTER_TEST(arguments, literal->id == AST_NODE_ARRAY_LITERAL);
                BUSTER_TEST(arguments, literal->array_literal.element_count == 3);
                BUSTER_STRING_TEST(arguments,
                        ((String8){ source.pointer + literal->array_literal.range.offset, literal->array_literal.range.length }),
                        S8("[1, 2 + 3, 4]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data values = [1 2]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->found == TOKEN_DECIMAL_INTEGER_LITERAL);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected ',' or ']' after array element"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 3);
        }
        arena->position = position;
    }

    {
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    data values: [1]s32 = [1];\n"
            "    values[0] -= 1;\n"
            "    return values[0];\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        AstStatement* assignment = parsed.first_code && parsed.first_code->body.first_statement ?
                parsed.first_code->body.first_statement->next : 0;
        BUSTER_TEST(arguments, assignment != 0 && assignment->id == AST_STATEMENT_ASSIGNMENT);
        if (assignment && assignment->id == AST_STATEMENT_ASSIGNMENT)
        {
            AstExpression target = assignment->assignment_statement.target;
            BUSTER_TEST(arguments, assignment->assignment_statement.operator == AST_ASSIGNMENT_MINUS_EQUAL);
            BUSTER_TEST(arguments, target.count == 3 && target.nodes[2].id == AST_NODE_ARRAY_INDEX);
            if (target.count == 3)
            {
                BUSTER_STRING_TEST(arguments,
                        ((String8){ source.pointer + target.nodes[2].array_index.range.offset,
                                   target.nodes[2].array_index.range.length }),
                        S8("[0]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data value = values[]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->found == TOKEN_RIGHT_BRACKET);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            BUSTER_TEST(arguments, parsed.first_code->body.last_statement->id == AST_STATEMENT_RETURN);
        }
        arena->position = position;
    }

    {
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    data sliced = values[1 + 2..4];\n"
            "    return sliced;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
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
                BUSTER_STRING_TEST(arguments,
                        ((String8){ source.pointer + slice->array_slice.range.offset,
                                   slice->array_slice.range.length }),
                        S8("[1 + 2..4]"));
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data value = values[1..2..3]; return 3; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->found == TOKEN_DOUBLE_DOT);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->expected == TOKEN_RIGHT_BRACKET);
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
        String8 source = { source_pointer, source_length };

        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* assignment = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, assignment != 0 && assignment->id == AST_STATEMENT_ASSIGNMENT);
        if (assignment && assignment->id == AST_STATEMENT_ASSIGNMENT)
        {
            BUSTER_TEST(arguments, assignment->assignment_statement.operator == test_case.operator);
            BUSTER_TEST(arguments, assignment->assignment_statement.target.count == 1);
            BUSTER_TEST(arguments, assignment->assignment_statement.value.count == 3);
        }
        arena->position = case_position;
    }

    {
        String8 source = S8("code main : fn () s32 { target += ; return 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 1);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { target; return 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_EXPECTED_ASSIGNMENT_OPERATOR);
        if (parsed.first_diagnostic)
        {
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message, S8("expected assignment operator"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 1);
        }
        arena->position = position;
    }

    {
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    if (a > 0) {\n"
            "        if (b) { a = b; }\n"
            "    } else {\n"
            "        b = a;\n"
            "    }\n"
            "    return a + b;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* if_statement = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, if_statement != 0 && if_statement->id == AST_STATEMENT_IF);
        if (if_statement && if_statement->id == AST_STATEMENT_IF)
        {
            AstIfStatement* if_data = &if_statement->if_statement;
            BUSTER_TEST(arguments, if_data->condition.count == 3);
            BUSTER_TEST(arguments, if_data->condition.count == 3 && if_data->condition.nodes[2].id == AST_NODE_BINARY_GREATER);
            BUSTER_TEST(arguments, if_data->has_else);
            BUSTER_TEST(arguments, if_data->then_block.statement_count == 1);
            BUSTER_TEST(arguments, if_data->else_block.statement_count == 1);

            AstStatement* nested_if = if_data->then_block.first_statement;
            BUSTER_TEST(arguments, nested_if != 0 && nested_if->id == AST_STATEMENT_IF);
            if (nested_if && nested_if->id == AST_STATEMENT_IF)
            {
                BUSTER_TEST(arguments, nested_if->if_statement.condition.count == 1);
                BUSTER_TEST(arguments, !nested_if->if_statement.has_else);
                BUSTER_TEST(arguments, nested_if->if_statement.then_block.statement_count == 1);
            }
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { if (value) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 && parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    for (data outer = 0 .. count) {\n"
            "        for (data inner: s32 = @reverse(0 .. count)) { total += inner; }\n"
            "    }\n"
            "    return total;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* outer = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, outer != 0 && outer->id == AST_STATEMENT_FOR);
        if (outer && outer->id == AST_STATEMENT_FOR)
        {
            BUSTER_STRING_TEST(arguments, outer->for_statement.name.text, S8("outer"));
            BUSTER_TEST(arguments, outer->for_statement.type == 0);
            BUSTER_TEST(arguments, outer->for_statement.iterable.count == 3);
            BUSTER_TEST(arguments, outer->for_statement.iterable.count == 3 &&
                    outer->for_statement.iterable.nodes[2].id == AST_NODE_BINARY_RANGE);
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
                BUSTER_TEST(arguments, inner->for_statement.iterable.count == 4 &&
                        inner->for_statement.iterable.nodes[2].id == AST_NODE_BINARY_RANGE);
                BUSTER_TEST(arguments, inner->for_statement.iterable.count == 4 &&
                        inner->for_statement.iterable.nodes[3].id == AST_NODE_INTRINSIC_CALL);
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
        String8 source = S8(
            "code main : fn () s32 {\n"
            "    loop (value < limit) {\n"
            "        loop { value += 1; }\n"
            "    }\n"
            "    return value;\n"
            "}\n");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.statement_count == 2);
        AstStatement* conditional = parsed.first_code ? parsed.first_code->body.first_statement : 0;
        BUSTER_TEST(arguments, conditional != 0 && conditional->id == AST_STATEMENT_LOOP);
        if (conditional && conditional->id == AST_STATEMENT_LOOP)
        {
            BUSTER_TEST(arguments, conditional->loop_statement.has_condition);
            BUSTER_TEST(arguments, conditional->loop_statement.condition.count == 3);
            BUSTER_TEST(arguments, conditional->loop_statement.condition.count == 3 &&
                    conditional->loop_statement.condition.nodes[2].id == AST_NODE_BINARY_LESS);
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
        String8 source = S8("code main : fn () s32 { loop (value) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { data range = 0 .. count .. 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->kind == PARSER_DIAGNOSTIC_CHAINED_RANGE);
        if (parsed.first_diagnostic)
        {
            BUSTER_TEST(arguments, parsed.first_diagnostic->found == TOKEN_DOUBLE_DOT);
            BUSTER_STRING_TEST(arguments, parsed.first_diagnostic->message,
                    S8("range operator '..' is not associative"));
        }
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { for (data e = values[..]) return 1; return 2; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
        BUSTER_TEST(arguments, parsed.diagnostic_count == 1);
        BUSTER_TEST(arguments, parsed.first_diagnostic != 0 &&
                parsed.first_diagnostic->expected == TOKEN_LEFT_BRACE);
        BUSTER_TEST(arguments, parsed.first_code != 0 && parsed.first_code->body.last_statement != 0);
        if (parsed.first_code && parsed.first_code->body.last_statement)
        {
            AstStatement* recovered = parsed.first_code->body.last_statement;
            BUSTER_TEST(arguments, recovered->id == AST_STATEMENT_RETURN);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1);
            BUSTER_TEST(arguments, recovered->return_statement.expression.count == 1 &&
                    recovered->return_statement.expression.nodes[0].integer.value == 2);
        }
        arena->position = position;
    }

    {
        String8 source = S8("code main : fn () s32 { return * 1; }");
        TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
        ParserResult parsed = parser_parse(arena, source, tokenizer);
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
        ParserResult parsed = parser_parse(arena, source, tokenizer);
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

        bool file_valid = source.pointer != 0 && source.length != 0;
        BUSTER_TEST(arguments, file_valid);

        if (source.pointer && source.length)
        {
            TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
            BUSTER_TEST(arguments, tokenizer_stream_covers_source(tokenizer, source.length));
            BUSTER_TEST(arguments, tokenizer.error_count == 0);

            ParserResult parsed = parser_parse(arena, source, tokenizer);

            if (parsed.diagnostic_count == 0)
            {
                BUSTER_TEST(arguments, parsed.code_count == 1);

                AstExpression expression = {0};

                if (parsed.first_code)
                {
                    AstStatement* last_statement = parsed.first_code->body.last_statement;
                    if (last_statement)
                    {
                        switch (last_statement->id)
                        {
                            break; case AST_STATEMENT_RETURN: expression = last_statement->return_statement.expression;
                            break; default: os_fail();
                        }
                    }
                }

                String8 actual = ast_expression_to_string(arena, expression);
                BUSTER_STRING_TEST(arguments, actual, test_case.expected_expression);
            }
            else
            {
                for (ParserDiagnostic* diagnostic = parsed.first_diagnostic; diagnostic; diagnostic = diagnostic->next)
                {
                    String8 found = string_from_token_id((TokenIdEnum)diagnostic->found);
                    u32 line = diagnostic->range.line + 1;
                    u32 column = diagnostic->range.column + 1;
                    if (diagnostic->expected == TOKEN_ERROR)
                    {
                        arguments->show(
                                arguments,
                                S8("{S8}:{u32}:{u32}: parser error: {S8}; found {S8}\n"),
                                test_case.path,
                                line,
                                column,
                                diagnostic->message,
                                found);
                    }
                    else
                    {
                        String8 expected = string_from_token_id((TokenIdEnum)diagnostic->expected);
                        arguments->show(
                                arguments,
                                S8("{S8}:{u32}:{u32}: parser error: {S8}; found {S8}, expected {S8}\n"),
                                test_case.path,
                                line,
                                column,
                                diagnostic->message,
                                found,
                                expected);
                    }
                }
                BUSTER_TEST(arguments, parsed.diagnostic_count == 0);
            }
        }

        arena->position = position;
    }

    return result;
}
#endif
