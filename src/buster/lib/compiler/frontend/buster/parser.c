#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/integer.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/time.h>

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
    bool result = tokenizer_is_ascii_decimal_digit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
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
        break;
    case INTEGER_FORMAT_HEXADECIMAL:
    {
        result = tokenizer_is_ascii_hex_digit(ch) || ch == '_';
    }
    break;
    case INTEGER_FORMAT_DECIMAL:
    {
        result = tokenizer_is_ascii_decimal_digit(ch) || ch == '_';
    }
    break;
    case INTEGER_FORMAT_OCTAL:
    {
        result = (ch >= '0' && ch <= '7') || ch == '_';
    }
    break;
    case INTEGER_FORMAT_BINARY:
    {
        result = ch == '0' || ch == '1' || ch == '_';
    }
    break;
    case INTEGER_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool tokenizer_is_real_digit(IntegerFormat format, u8 ch)
{
    bool result = false;
    switch (format)
    {
        break;
    case INTEGER_FORMAT_HEXADECIMAL:
    {
        result = tokenizer_is_ascii_hex_digit(ch) || ch == '_';
    }
    break;
    case INTEGER_FORMAT_DECIMAL:
    {
        result = tokenizer_is_ascii_decimal_digit(ch) || ch == '_';
    }
    break;
    case INTEGER_FORMAT_OCTAL:
        break;
    case INTEGER_FORMAT_BINARY:
        break;
    case INTEGER_FORMAT_COUNT:
        BUSTER_UNREACHABLE();
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

BUSTER_GLOBAL_LOCAL void tokenizer_emit_token(Arena* arena, TokenizerResult* restrict result, Token** restrict token_start, u64* restrict token_count,
                                              TokenId id, u64 length)
{
    do
    {
        u32 chunk_length = (u32)BUSTER_MIN(length, (u64)TOKEN_MAX_LENGTH);
        Token* token = arena_allocate(arena, Token, 1);
        if (!*token_start)
        {
            *token_start = token;
        }
        *token = (Token){.id = id};
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

    if (file_length > UINT32_MAX)
    {
        tokenizer_emit_token(arena, &result, &token_start, &token_count, TOKEN_EOF, 0);
        result.tokens = token_start;
        result.token_count = 1;
        result.error_count = 1;
        return result;
    }

    const char8* restrict it = file_pointer;
    const char8* top = file_pointer + file_length;

    while (it < top)
    {
        const char8* restrict start = it;
        u8 start_ch = (u8)*start;
        TokenId id = TOKEN_ERROR;

        switch (start_ch)
        {
            break;
        BUSTER_SWITCH_ALPHA_UPPER:
        BUSTER_SWITCH_ALPHA_LOWER:
        case '_':
        {
            while (it < top && tokenizer_is_identifier_continue((u8)*it))
            {
                it += 1;
            }

            String8 identifier = string_from_pointer_length(start, (u64)(it - start));
            if (it < top && *it == '?' && (string_equal(identifier, S8("and")) || string_equal(identifier, S8("or"))))
            {
                it += 1;
                identifier.length += 1;
            }

            BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
                [TOKEN_KEYWORD_FUNCTION - first_keyword] = S8_INITIALIZER("fn"),
                [TOKEN_KEYWORD_IF - first_keyword] = S8_INITIALIZER("if"),
                [TOKEN_KEYWORD_ELSE - first_keyword] = S8_INITIALIZER("else"),
                [TOKEN_KEYWORD_SWITCH - first_keyword] = S8_INITIALIZER("switch"),
                [TOKEN_KEYWORD_RETURN - first_keyword] = S8_INITIALIZER("return"),
                [TOKEN_KEYWORD_BREAK - first_keyword] = S8_INITIALIZER("break"),
                [TOKEN_KEYWORD_CONTINUE - first_keyword] = S8_INITIALIZER("continue"),
                [TOKEN_KEYWORD_FOR - first_keyword] = S8_INITIALIZER("for"),
                [TOKEN_KEYWORD_WHILE - first_keyword] = S8_INITIALIZER("while"),
                [TOKEN_KEYWORD_LOOP - first_keyword] = S8_INITIALIZER("loop"),
                [TOKEN_KEYWORD_CODE - first_keyword] = S8_INITIALIZER("code"),
                [TOKEN_KEYWORD_DATA - first_keyword] = S8_INITIALIZER("data"),
                [TOKEN_KEYWORD_TYPE - first_keyword] = S8_INITIALIZER("type"),
                [TOKEN_KEYWORD_IMPORT - first_keyword] = S8_INITIALIZER("import"),
                [TOKEN_KEYWORD_STRUCT - first_keyword] = S8_INITIALIZER("struct"),
                [TOKEN_KEYWORD_UNION - first_keyword] = S8_INITIALIZER("union"),
                [TOKEN_KEYWORD_ENUM - first_keyword] = S8_INITIALIZER("enum"),
                [TOKEN_KEYWORD_VECTOR - first_keyword] = S8_INITIALIZER("vector"),
                [TOKEN_KEYWORD_AND - first_keyword] = S8_INITIALIZER("and"),
                [TOKEN_KEYWORD_OR - first_keyword] = S8_INITIALIZER("or"),
                [TOKEN_KEYWORD_AND_SHORT_CIRCUIT - first_keyword] = S8_INITIALIZER("and?"),
                [TOKEN_KEYWORD_OR_SHORT_CIRCUIT - first_keyword] = S8_INITIALIZER("or?"),
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
        break;
        case ' ':
        {
            id = TOKEN_SPACE;

            while (it < top && *it == ' ')
            {
                it += 1;
            }
        }
        break;
        case '\t':
        {
            id = TOKEN_TAB;

            while (it < top && *it == '\t')
            {
                it += 1;
            }
        }
        break;
        BUSTER_SWITCH_DECIMAL_DIGIT:
        {
            bool is_valid = true;
            bool has_integer_digit = false;
            IntegerFormat format = INTEGER_FORMAT_DECIMAL;

            if (start_ch == '0' && it + 1 < top)
            {
                u8 second_ch = (u8)it[1];
                switch (second_ch)
                {
                    break;
                case 'x':
                    it += 2;
                    format = INTEGER_FORMAT_HEXADECIMAL;
                    break;
                case 'o':
                    it += 2;
                    format = INTEGER_FORMAT_OCTAL;
                    break;
                case 'b':
                    it += 2;
                    format = INTEGER_FORMAT_BINARY;
                    break;
                default:
                {
                }
                }
            }

            while (it < top && tokenizer_is_integer_digit(format, (u8)*it))
            {
                has_integer_digit = has_integer_digit || *it != '_';
                it += 1;
            }

            is_valid = is_valid && (format == INTEGER_FORMAT_DECIMAL || has_integer_digit);

            bool is_float =
                (format == INTEGER_FORMAT_DECIMAL || format == INTEGER_FORMAT_HEXADECIMAL) && it < top && *it == '.' && !(it + 1 < top && it[1] == '.');

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
                        break;
                    case INTEGER_FORMAT_HEXADECIMAL:
                    {
                        id = TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT;
                        is_valid = is_valid && (exponent_ch == 'P' || exponent_ch == 'p');
                    }
                    break;
                    case INTEGER_FORMAT_DECIMAL:
                    {
                        id = TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT;
                        is_valid = is_valid && (exponent_ch == 'E' || exponent_ch == 'e');
                    }
                    break;
                    default:
                        BUSTER_UNREACHABLE();
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
                break;
                default:
                {
                    switch (format)
                    {
                        break;
                    case INTEGER_FORMAT_HEXADECIMAL:
                        id = TOKEN_HEXADECIMAL_FLOAT_LITERAL;
                        break;
                    case INTEGER_FORMAT_DECIMAL:
                        id = TOKEN_DECIMAL_FLOAT_LITERAL;
                        break;
                    default:
                        BUSTER_UNREACHABLE();
                    }
                }
                }
            }
            else
            {
                switch (format)
                {
                    break;
                case INTEGER_FORMAT_HEXADECIMAL:
                    id = TOKEN_HEXADECIMAL_INTEGER_LITERAL;
                    break;
                case INTEGER_FORMAT_DECIMAL:
                    id = TOKEN_DECIMAL_INTEGER_LITERAL;
                    break;
                case INTEGER_FORMAT_OCTAL:
                    id = TOKEN_OCTAL_INTEGER_LITERAL;
                    break;
                case INTEGER_FORMAT_BINARY:
                    id = TOKEN_BINARY_INTEGER_LITERAL;
                    break;
                case INTEGER_FORMAT_COUNT:
                    BUSTER_UNREACHABLE();
                }
            }

            id = is_valid ? id : TOKEN_ERROR;
        }
        break;
        case '\n':
        {
            id = TOKEN_LINE_FEED;
            it += 1;
        }
        break;
        case '\r':
        {
            id = TOKEN_CARRIAGE_RETURN;
            it += 1;
        }
        break;
        case '\'':
        {
            id = TOKEN_CHARACTER_LITERAL;
            it += 1;
            if (it < top && *it == '\'')
            {
                it += 1;
            }
            else
            {
                if (it < top && *it == '\\')
                {
                    it += 1;
                }
                if (it < top && *it != '\n' && *it != '\r')
                {
                    it += tokenizer_utf8_sequence_length(it, top);
                }

                while (it < top && *it != '\n' && *it != '\r')
                {
                    if (*it == '\'')
                    {
                        it += 1;
                        break;
                    }
                    if (*it == ';' || *it == ',' || *it == ')' || *it == ']' || *it == '}' || *it == ' ' || *it == '\t')
                    {
                        break;
                    }
                    it += tokenizer_utf8_sequence_length(it, top);
                }
            }
        }
        break;
        case '"':
        {
            id = TOKEN_STRING_LITERAL;
            const char8* recovery = 0;
            bool terminated = false;
            it += 1;
            while (it < top && *it != '\n' && *it != '\r')
            {
                if (*it == '"')
                {
                    it += 1;
                    terminated = true;
                    break;
                }
                if (*it == '\\')
                {
                    it += 1;
                    if (it == top || *it == '\n' || *it == '\r')
                    {
                        break;
                    }
                }
                else if (*it == ';' && !recovery)
                {
                    recovery = it;
                }
                it += tokenizer_utf8_sequence_length(it, top);
            }
            if (!terminated && recovery)
            {
                it = recovery;
            }
        }
        break;
        case '[':
        {
            id = TOKEN_LEFT_BRACKET;
            it += 1;
        }
        break;
        case ']':
        {
            id = TOKEN_RIGHT_BRACKET;
            it += 1;
        }
        break;
        case '{':
        {
            id = TOKEN_LEFT_BRACE;
            it += 1;
        }
        break;
        case '}':
        {
            id = TOKEN_RIGHT_BRACE;
            it += 1;
        }
        break;
        case '(':
        {
            id = TOKEN_LEFT_PARENTHESIS;
            it += 1;
        }
        break;
        case ')':
        {
            id = TOKEN_RIGHT_PARENTHESIS;
            it += 1;
        }
        break;
        case '<':
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
        break;
        case '>':
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
        break;
        case '+':
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
        break;
        case '-':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_MINUS_EQUAL : TOKEN_MINUS;
            it += has_equal ? 2 : 1;
        }
        break;
        case '*':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_ASTERISK_EQUAL : TOKEN_ASTERISK;
            it += has_equal ? 2 : 1;
        }
        break;
        case '=':
        {
            if (it + 1 < top && it[1] == '>')
            {
                id = TOKEN_FAT_ARROW;
                it += 2;
            }
            else if (it + 1 < top && it[1] == '=')
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
        break;
        case '!':
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
        break;
        case ':':
        {
            id = TOKEN_COLON;
            it += 1;
        }
        break;
        case ';':
        {
            id = TOKEN_SEMICOLON;
            it += 1;
        }
        break;
        case ',':
        {
            id = TOKEN_COMMA;
            it += 1;
        }
        break;
        case '&':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_AMPERSAND_EQUAL : TOKEN_AMPERSAND;
            it += has_equal ? 2 : 1;
        }
        break;
        case '%':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_PERCENTAGE_EQUAL : TOKEN_PERCENTAGE;
            it += has_equal ? 2 : 1;
        }
        break;
        case '|':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_BAR_EQUAL : TOKEN_BAR;
            it += has_equal ? 2 : 1;
        }
        break;
        case '^':
        {
            bool has_equal = it + 1 < top && it[1] == '=';
            id = has_equal ? TOKEN_CARET_EQUAL : TOKEN_CARET;
            it += has_equal ? 2 : 1;
        }
        break;
        case '~':
        {
            id = TOKEN_TILDE;
            it += 1;
        }
        break;
        case '@':
        {
            id = TOKEN_AT;
            it += 1;
        }
        break;
        case '$':
        {
            id = TOKEN_DOLLAR;
            it += 1;
        }
        break;
        case '/':
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
        break;
        case '.':
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
        break;
        default:
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

    BUSTER_CHECK(token_count <= UINT32_MAX);
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
    PARSER_STATE_IMPORT,
    PARSER_STATE_DATA_DECLARATION,
    PARSER_STATE_CODE,
    PARSER_STATE_TYPE_REFERENCE,
    PARSER_STATE_ATTRIBUTE_LIST,
    PARSER_STATE_BLOCK,
    PARSER_STATE_STATEMENT,
    PARSER_STATE_RETURN_STATEMENT,
    PARSER_STATE_DATA_STATEMENT,
    PARSER_STATE_ASSIGNMENT_STATEMENT,
    PARSER_STATE_IF_STATEMENT,
    PARSER_STATE_SWITCH_STATEMENT,
    PARSER_STATE_FOR_STATEMENT,
    PARSER_STATE_LOOP_STATEMENT,
    PARSER_STATE_TYPE_STATEMENT,
    PARSER_STATE_ARRAY_LITERAL,
    PARSER_STATE_AGGREGATE_LITERAL,
    PARSER_STATE_ARRAY_SUBSCRIPT,
    PARSER_STATE_MEMBER_ACCESS,
    PARSER_STATE_ENUM_LITERAL,
    PARSER_STATE_CALL,
    PARSER_STATE_INTRINSIC_CALL,
    PARSER_STATE_EXPRESSION,
    PARSER_STATE_UNARY_PREFIX,
    PARSER_STATE_COUNT,
} ParserStateId;

typedef enum ImportState
{
    IMPORT_STATE_NAMESPACE,
    IMPORT_STATE_EQUAL,
    IMPORT_STATE_PATH,
    IMPORT_STATE_SEMICOLON,
    IMPORT_STATE_COUNT,
} ImportState;

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
    TYPE_STATE_QUALIFIED_NAME,
    TYPE_STATE_AFTER_ARRAY_SLICE_START,
    TYPE_STATE_AFTER_ARRAY_COUNT,
    TYPE_STATE_AFTER_ARRAY_INFER_MARKER,
    TYPE_STATE_AFTER_VECTOR_KEYWORD,
    TYPE_STATE_AFTER_VECTOR_OPEN,
    TYPE_STATE_AFTER_VECTOR_COUNT,
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
    IF_STATEMENT_STATE_ELSE_IF,
    IF_STATEMENT_STATE_COUNT,
} IfStatementStateId;

typedef struct IfStatementState IfStatementState;
struct IfStatementState
{
    IfStatementStateId id;
};

typedef enum SwitchStatementStateId
{
    SWITCH_STATEMENT_STATE_OPEN_EXPRESSION,
    SWITCH_STATEMENT_STATE_EXPRESSION,
    SWITCH_STATEMENT_STATE_CLOSE_EXPRESSION,
    SWITCH_STATEMENT_STATE_CASE_OR_END,
    SWITCH_STATEMENT_STATE_CASE_EXPRESSION,
    SWITCH_STATEMENT_STATE_CASE_ARROW,
    SWITCH_STATEMENT_STATE_CASE_BODY,
    SWITCH_STATEMENT_STATE_CASE_DELIMITER,
    SWITCH_STATEMENT_STATE_COUNT,
} SwitchStatementStateId;

typedef struct SwitchStatementState SwitchStatementState;
struct SwitchStatementState
{
    SwitchStatementStateId id;
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

typedef enum TypeStatementStateId
{
    TYPE_STATEMENT_STATE_NAME,
    TYPE_STATEMENT_STATE_EQUAL,
    TYPE_STATEMENT_STATE_KIND,
    TYPE_STATEMENT_STATE_ALIAS_TYPE,
    TYPE_STATEMENT_STATE_OPEN,
    TYPE_STATEMENT_STATE_FIELD_OR_CLOSE,
    TYPE_STATEMENT_STATE_FIELD_COLON,
    TYPE_STATEMENT_STATE_FIELD_TYPE,
    TYPE_STATEMENT_STATE_FIELD_DELIMITER,
    TYPE_STATEMENT_STATE_ENUM_MEMBER_EQUAL_OR_DELIMITER,
    TYPE_STATEMENT_STATE_ENUM_MEMBER_VALUE,
    TYPE_STATEMENT_STATE_ENUM_MEMBER_DELIMITER,
    TYPE_STATEMENT_STATE_COUNT,
} TypeStatementStateId;

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

typedef enum AggregateLiteralStateId
{
    AGGREGATE_LITERAL_STATE_FIELD_OR_END,
    AGGREGATE_LITERAL_STATE_FIELD_NAME,
    AGGREGATE_LITERAL_STATE_EQUAL,
    AGGREGATE_LITERAL_STATE_VALUE,
    AGGREGATE_LITERAL_STATE_DELIMITER,
    AGGREGATE_LITERAL_STATE_COUNT,
} AggregateLiteralStateId;

typedef enum IntrinsicCallStateId
{
    INTRINSIC_CALL_STATE_NAME,
    INTRINSIC_CALL_STATE_OPEN,
    INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE,
    INTRINSIC_CALL_STATE_DELIMITER,
    INTRINSIC_CALL_STATE_COUNT,
} IntrinsicCallStateId;

typedef enum CallStateId
{
    CALL_STATE_ARGUMENT_OR_CLOSE,
    CALL_STATE_DELIMITER,
    CALL_STATE_COUNT,
} CallStateId;

typedef enum ExpressionState
{
    EXPRESSION_STATE_PREFIX,
    EXPRESSION_STATE_TAIL,
    EXPRESSION_STATE_COUNT,
} ExpressionState;

typedef enum ExpressionArgumentKind
{
    EXPRESSION_ARGUMENT_NONE,
    EXPRESSION_ARGUMENT_CALL,
    EXPRESSION_ARGUMENT_INTRINSIC,
    EXPRESSION_ARGUMENT_COUNT,
} ExpressionArgumentKind;

typedef enum BindingPower
{
    BINDING_POWER_RANGE,
    BINDING_POWER_BOOLEAN_OR,
    BINDING_POWER_BOOLEAN_AND,
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
            AstImport* import;
            ImportState state;
        } import;

        struct
        {
            AstDataDeclaration* declaration;
            DataStatementState state;
        } data_declaration;

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
            AstType* qualified;
            AstTypeArgument* argument;
            ParserSourceRange name_range;
            ParserSourceRange prefix_range;
            bool compile_time_prefix;
            bool argument_is_compile_time;
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
                SwitchStatementState switch_state;
                ForStatementState for_state;
                LoopStatementState loop_state;
            };
            AstStatement* pointer;
            TokenId end_token;
        } statement;

        struct
        {
            TypeStatementStateId state;
            AstTypeDeclaration* declaration;
            AstTypeField* field;
            AstEnumMember* enum_member;
        } type_statement;

        struct
        {
            ExpressionState state;
            TokenId end_token;
            bool is_group;
            bool ends_at_assignment;
            bool is_array_element;
            bool ends_at_array_delimiter;
            bool is_aggregate_field;
            bool ends_at_aggregate_delimiter;
            bool is_enum_value;
            bool ends_at_enum_delimiter;
            bool is_array_subscript_bound;
            bool is_array_subscript_end;
            bool ends_at_slice_operator;
            u8 argument_kind;
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
            AggregateLiteralStateId state;
            AstAggregateLiteralField* first_field;
            AstAggregateLiteralField* last_field;
            u32 field_count;
        } aggregate_literal;

        struct
        {
            ParserSourceRange range;
            ArraySubscriptStateId state;
            bool has_start;
            bool has_end;
        } array_subscript;

        struct
        {
            ParserSourceRange range;
        } member_access;

        struct
        {
            ParserSourceRange range;
        } enum_literal;

        struct
        {
            ParserSourceRange range;
            CallStateId state;
            u32 argument_count;
        } call;

        struct
        {
            AstIdentifier name;
            ParserSourceRange range;
            AstType** type_argument_destination;
            IntrinsicCallStateId state;
            u32 argument_count;
        } intrinsic_call;

        // One pending prefix unary operator (`-`, `+`, `!`, `~`, `&`). A run of
        // prefix operators is a run of these frames on the state stack, popped
        // innermost-first once the operand has been emitted. The id is an
        // AstNodeId stored as u8 because that enum is declared later.
        struct
        {
            u8 id;
            ParserSourceRange range;
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
    PARSER_RECOVERY_EXPRESSION_DELIMITER,
    PARSER_RECOVERY_TYPE_DELIMITER,
    PARSER_RECOVERY_SWITCH_CASE,
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
BUSTER_GLOBAL_LOCAL String8 get_string(const char8* source, ExtendedToken token);

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

BUSTER_GLOBAL_LOCAL AstImport* parser_import_push(Parser* parser, ExtendedToken token)
{
    AstImport* import = arena_allocate(parser->result_arena, AstImport, 1);
    *import = (AstImport){.range = source_range_from_token(token)};
    if (parser->result->last_import)
    {
        parser->result->last_import->next = import;
    }
    else
    {
        parser->result->first_import = import;
    }
    parser->result->last_import = import;
    parser->result->import_count += 1;
    return import;
}

BUSTER_GLOBAL_LOCAL AstDataDeclaration* parser_data_declaration_push(Parser* parser, ExtendedToken token)
{
    AstDataDeclaration* declaration = arena_allocate(parser->result_arena, AstDataDeclaration, 1);
    *declaration = (AstDataDeclaration){
        .range = source_range_from_token(token),
    };
    if (parser->result->last_data_declaration)
    {
        parser->result->last_data_declaration->next = declaration;
    }
    else
    {
        parser->result->first_data_declaration = declaration;
    }
    parser->result->last_data_declaration = declaration;
    parser->result->data_declaration_count += 1;
    return declaration;
}

BUSTER_GLOBAL_LOCAL AstTypeDeclaration* parser_type_declaration_push(Parser* parser, ExtendedToken token)
{
    AstTypeDeclaration* declaration = arena_allocate(parser->result_arena, AstTypeDeclaration, 1);
    *declaration = (AstTypeDeclaration){.range = source_range_from_token(token)};
    if (parser->result->last_type_declaration)
    {
        parser->result->last_type_declaration->next = declaration;
    }
    else
    {
        parser->result->first_type_declaration = declaration;
    }
    parser->result->last_type_declaration = declaration;
    parser->result->type_declaration_count += 1;
    return declaration;
}

BUSTER_GLOBAL_LOCAL AstTypeField* parser_type_field_push(Parser* parser, AstTypeDeclaration* declaration, ExtendedToken token)
{
    AstTypeField* field = arena_allocate(parser->result_arena, AstTypeField, 1);
    *field = (AstTypeField){
        .name =
            {
                .text = get_string(parser->iterator.source, token),
                .range = source_range_from_token(token),
            },
        .range = source_range_from_token(token),
    };
    if (declaration->last_field)
    {
        declaration->last_field->next = field;
    }
    else
    {
        declaration->first_field = field;
    }
    declaration->last_field = field;
    declaration->field_count += 1;
    return field;
}

BUSTER_GLOBAL_LOCAL AstEnumMember* parser_enum_member_push(Parser* parser, AstTypeDeclaration* declaration, ExtendedToken token)
{
    AstEnumMember* member = arena_allocate(parser->result_arena, AstEnumMember, 1);
    *member = (AstEnumMember){
        .name =
            {
                .text = get_string(parser->iterator.source, token),
                .range = source_range_from_token(token),
            },
        .range = source_range_from_token(token),
    };
    if (declaration->last_enum_member)
    {
        declaration->last_enum_member->next = member;
    }
    else
    {
        declaration->first_enum_member = member;
    }
    declaration->last_enum_member = member;
    declaration->enum_member_count += 1;
    return member;
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
    type->is_compile_time = type_state->type.compile_time_prefix;
    type_state->type.compile_time_prefix = false;
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
    *argument = (AstTypeArgument){.range = source_range_from_token(token)};
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

BUSTER_GLOBAL_LOCAL AstStatement* parser_statement_create(Parser* parser, AstStatementId id, ExtendedToken token)
{
    AstStatement* statement = arena_allocate(parser->result_arena, AstStatement, 1);
    *statement = (AstStatement){
        .range = source_range_from_token(token),
        .id = id,
    };
    return statement;
}

BUSTER_GLOBAL_LOCAL AstStatement* parser_statement_push(Parser* parser, AstBlock* block, AstStatementId id, ExtendedToken token)
{
    AstStatement* statement = parser_statement_create(parser, id, token);
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

BUSTER_GLOBAL_LOCAL AstSwitchCase* parser_switch_case_push(Parser* parser, AstSwitchStatement* switch_statement, ExtendedToken token)
{
    AstSwitchCase* switch_case = arena_allocate(parser->result_arena, AstSwitchCase, 1);
    *switch_case = (AstSwitchCase){.range = source_range_from_token(token)};
    if (switch_statement->last_case)
    {
        switch_statement->last_case->next = switch_case;
    }
    else
    {
        switch_statement->first_case = switch_case;
    }
    switch_statement->last_case = switch_case;
    switch_statement->case_count += 1;
    return switch_case;
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

BUSTER_GLOBAL_LOCAL ParserState* parser_expression_container(Parser* parser)
{
    ParserState* bottom = arena_get_pointer_at_position(parser->state.arena, ParserState, parser->state.minimum_position);
    ParserState* frame = state(parser);
    for (;;)
    {
        switch (frame->id)
        {
        case PARSER_STATE_ARRAY_LITERAL:
        case PARSER_STATE_ARRAY_SUBSCRIPT:
        case PARSER_STATE_AGGREGATE_LITERAL:
        case PARSER_STATE_CALL:
        {
            return frame;
        }
        case PARSER_STATE_INTRINSIC_CALL:
        {
            if (frame->intrinsic_call.state >= INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE)
            {
                return frame;
            }
        }
        break;
        default:
        {
        }
        }
        if (frame == bottom)
        {
            break;
        }
        frame -= 1;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL ParserState* parser_type_declaration_container(Parser* parser)
{
    ParserState* bottom = arena_get_pointer_at_position(parser->state.arena, ParserState, parser->state.minimum_position);
    ParserState* frame = state(parser);
    for (;;)
    {
        if (frame->id == PARSER_STATE_TYPE_STATEMENT && frame->type_statement.state >= TYPE_STATEMENT_STATE_FIELD_OR_CLOSE)
        {
            return frame;
        }
        if (frame == bottom)
        {
            break;
        }
        frame -= 1;
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL void parser_unexpected(Parser* parser, ExtendedToken token, TokenId expected)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN, token, expected, S8("unexpected token"));
    ParserStateId state_id = state(parser)->id;
    switch (state_id)
    {
    case PARSER_STATE_STATEMENT:
    case PARSER_STATE_RETURN_STATEMENT:
    case PARSER_STATE_DATA_STATEMENT:
    case PARSER_STATE_ASSIGNMENT_STATEMENT:
    case PARSER_STATE_IF_STATEMENT:
    case PARSER_STATE_SWITCH_STATEMENT:
    case PARSER_STATE_FOR_STATEMENT:
    case PARSER_STATE_LOOP_STATEMENT:
    case PARSER_STATE_MEMBER_ACCESS:
    case PARSER_STATE_ENUM_LITERAL:
    {
        parser->recovery = PARSER_RECOVERY_STATEMENT;
    }
    break;
    case PARSER_STATE_ARRAY_LITERAL:
    case PARSER_STATE_AGGREGATE_LITERAL:
    case PARSER_STATE_ARRAY_SUBSCRIPT:
    case PARSER_STATE_CALL:
    {
        parser->recovery = PARSER_RECOVERY_EXPRESSION_DELIMITER;
    }
    break;
    case PARSER_STATE_INTRINSIC_CALL:
    {
        parser->recovery =
            state(parser)->intrinsic_call.state >= INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE ? PARSER_RECOVERY_EXPRESSION_DELIMITER : PARSER_RECOVERY_STATEMENT;
    }
    break;
    case PARSER_STATE_TYPE_STATEMENT:
    case PARSER_STATE_DATA_DECLARATION:
    case PARSER_STATE_TYPE_REFERENCE:
    {
        parser->recovery = parser_type_declaration_container(parser) ? PARSER_RECOVERY_TYPE_DELIMITER : PARSER_RECOVERY_DECLARATION;
    }
    break;
    case PARSER_STATE_EXPRESSION:
    case PARSER_STATE_UNARY_PREFIX:
    {
        ParserState* expression_state = state(parser);
        while (expression_state->id == PARSER_STATE_UNARY_PREFIX)
        {
            expression_state -= 1;
        }
        BUSTER_CHECK(expression_state->id == PARSER_STATE_EXPRESSION);
        if (expression_state->expression.end_token == TOKEN_FAT_ARROW)
        {
            parser->recovery = PARSER_RECOVERY_SWITCH_CASE;
        }
        else if (parser_type_declaration_container(parser))
        {
            parser->recovery = PARSER_RECOVERY_TYPE_DELIMITER;
        }
        else
        {
            parser->recovery = parser_expression_container(parser) ? PARSER_RECOVERY_EXPRESSION_DELIMITER : PARSER_RECOVERY_STATEMENT;
        }
    }
    break;
    default:
    {
        parser->recovery = PARSER_RECOVERY_DECLARATION;
    }
    }
}

BUSTER_GLOBAL_LOCAL void parser_expected_assignment_operator(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_ASSIGNMENT_OPERATOR, token, TOKEN_ERROR, S8("expected assignment operator"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_expected_array_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER, token, TOKEN_ERROR, S8("expected ',' or ']' after array element"));
    parser->recovery = PARSER_RECOVERY_EXPRESSION_DELIMITER;
}

BUSTER_GLOBAL_LOCAL void parser_expected_call_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER, token, TOKEN_ERROR, S8("expected ',' or ')' after call argument"));
    parser->recovery = PARSER_RECOVERY_EXPRESSION_DELIMITER;
}

BUSTER_GLOBAL_LOCAL void parser_expected_type_field_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_TYPE_FIELD_DELIMITER, token, TOKEN_ERROR, S8("expected ',' or '}' after type field"));
    parser->recovery = PARSER_RECOVERY_TYPE_DELIMITER;
}

BUSTER_GLOBAL_LOCAL void parser_expected_aggregate_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_AGGREGATE_DELIMITER, token, TOKEN_ERROR, S8("expected ',' or '}' after aggregate field"));
    parser->recovery = PARSER_RECOVERY_EXPRESSION_DELIMITER;
}

BUSTER_GLOBAL_LOCAL void parser_expected_postfix_access(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_POSTFIX_ACCESS, token, TOKEN_ERROR, S8("expected member name or '&' after '.'"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_expected_else_body(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_ELSE_BODY, token, TOKEN_ERROR, S8("expected 'if' or '{' after 'else'"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_expected_switch_case_delimiter(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_SWITCH_CASE_DELIMITER, token, TOKEN_ERROR, S8("expected ',' or '}' after switch case"));
    parser->recovery = PARSER_RECOVERY_SWITCH_CASE;
}

BUSTER_GLOBAL_LOCAL void parser_duplicate_switch_else(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_DUPLICATE_SWITCH_ELSE, token, TOKEN_ERROR, S8("switch may only have one 'else' case"));
    parser->recovery = PARSER_RECOVERY_SWITCH_CASE;
}

BUSTER_GLOBAL_LOCAL void parser_expected_enum_delimiter(Parser* parser, ExtendedToken token, bool has_explicit_value)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_ENUM_DELIMITER, token, TOKEN_ERROR,
                           has_explicit_value ? S8("expected ',' or '}' after enum value") : S8("expected '=', ',' or '}' after enum member"));
    parser->recovery = PARSER_RECOVERY_TYPE_DELIMITER;
}

BUSTER_GLOBAL_LOCAL void parser_expected_type_declaration_kind(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_TYPE_DECLARATION_KIND, token, TOKEN_ERROR,
                           S8("expected type, 'struct', 'union', or 'enum' after '='"));
    parser->recovery = PARSER_RECOVERY_DECLARATION;
}

BUSTER_GLOBAL_LOCAL void parser_chained_range(Parser* parser, ExtendedToken token)
{
    parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_CHAINED_RANGE, token, TOKEN_ERROR, S8("range operator '..' is not associative"));
    parser->recovery = PARSER_RECOVERY_STATEMENT;
}

BUSTER_GLOBAL_LOCAL void parser_aggregate_trim_incomplete_field(ParserState* aggregate)
{
    BUSTER_CHECK(aggregate->id == PARSER_STATE_AGGREGATE_LITERAL);
    AstAggregateLiteralField* field = aggregate->aggregate_literal.first_field;
    u32 completed_count = aggregate->aggregate_literal.field_count;
    if (completed_count == 0)
    {
        aggregate->aggregate_literal.first_field = 0;
        aggregate->aggregate_literal.last_field = 0;
        return;
    }

    for (u32 i = 1; i < completed_count; i += 1)
    {
        BUSTER_CHECK(field);
        field = field->next;
    }
    BUSTER_CHECK(field);
    field->next = 0;
    aggregate->aggregate_literal.last_field = field;
}

BUSTER_GLOBAL_LOCAL void parser_type_remove_incomplete_item(ParserState* type_statement)
{
    BUSTER_CHECK(type_statement->id == PARSER_STATE_TYPE_STATEMENT);
    AstTypeDeclaration* declaration = type_statement->type_statement.declaration;
    TypeStatementStateId state_id = type_statement->type_statement.state;
    if (state_id == TYPE_STATEMENT_STATE_FIELD_COLON || state_id == TYPE_STATEMENT_STATE_FIELD_TYPE)
    {
        BUSTER_CHECK(declaration->field_count);
        AstTypeField* previous = 0;
        AstTypeField* field = declaration->first_field;
        while (field && field != declaration->last_field)
        {
            previous = field;
            field = field->next;
        }
        BUSTER_CHECK(field == declaration->last_field);
        if (previous)
        {
            previous->next = 0;
        }
        else
        {
            declaration->first_field = 0;
        }
        declaration->last_field = previous;
        declaration->field_count -= 1;
        type_statement->type_statement.field = 0;
    }
    else if (state_id == TYPE_STATEMENT_STATE_ENUM_MEMBER_VALUE)
    {
        BUSTER_CHECK(declaration->enum_member_count);
        AstEnumMember* previous = 0;
        AstEnumMember* member = declaration->first_enum_member;
        while (member && member != declaration->last_enum_member)
        {
            previous = member;
            member = member->next;
        }
        BUSTER_CHECK(member == declaration->last_enum_member);
        if (previous)
        {
            previous->next = 0;
        }
        else
        {
            declaration->first_enum_member = 0;
        }
        declaration->last_enum_member = previous;
        declaration->enum_member_count -= 1;
        type_statement->type_statement.enum_member = 0;
    }
}

BUSTER_GLOBAL_LOCAL void parser_recover(Parser* parser)
{
    if (parser->recovery == PARSER_RECOVERY_EXPRESSION_DELIMITER)
    {
        ParserState* container = parser_expression_container(parser);
        if (container)
        {
            while (state(parser) != container)
            {
                state_pop(&parser->state);
            }

            ParserState* owner = container - 1;
            while (owner->id == PARSER_STATE_UNARY_PREFIX)
            {
                owner -= 1;
            }
            BUSTER_CHECK(owner->id == PARSER_STATE_EXPRESSION);
            u64 expression_position = parser->expression_arena_minimum_position;
            if (owner->expression.output_count)
            {
                expression_position = (u64)((u8*)(owner->expression.output_base + owner->expression.output_count) - (u8*)parser->expression_arena);
            }
            arena_set_position(parser->expression_arena, expression_position);

            if (container->id == PARSER_STATE_AGGREGATE_LITERAL && container->aggregate_literal.state != AGGREGATE_LITERAL_STATE_DELIMITER)
            {
                parser_aggregate_trim_incomplete_field(container);
            }

            TokenId close_token = TOKEN_ERROR;
            bool accepts_comma = true;
            switch (container->id)
            {
                break;
            case PARSER_STATE_ARRAY_LITERAL:
                close_token = TOKEN_RIGHT_BRACKET;
                break;
            case PARSER_STATE_ARRAY_SUBSCRIPT:
            {
                close_token = TOKEN_RIGHT_BRACKET;
                accepts_comma = false;
            }
            break;
            case PARSER_STATE_AGGREGATE_LITERAL:
                close_token = TOKEN_RIGHT_BRACE;
                break;
            case PARSER_STATE_CALL:
            case PARSER_STATE_INTRINSIC_CALL:
                close_token = TOKEN_RIGHT_PARENTHESIS;
                break;
            default:
                BUSTER_UNREACHABLE();
            }

            u32 parenthesis_depth = 0;
            u32 bracket_depth = 0;
            u32 brace_depth = 0;
            ExtendedToken token = peek(parser);
            while (token.id != TOKEN_EOF)
            {
                bool at_container_depth = parenthesis_depth == 0 && bracket_depth == 0 && brace_depth == 0;
                if (at_container_depth && token.id == close_token)
                {
                    if (container->id == PARSER_STATE_ARRAY_SUBSCRIPT)
                    {
                        consume(&parser->iterator);
                        state_pop(&parser->state);
                        owner->expression.state = EXPRESSION_STATE_TAIL;
                    }
                    else if (container->id == PARSER_STATE_ARRAY_LITERAL)
                    {
                        container->array_literal.state = ARRAY_LITERAL_STATE_DELIMITER;
                    }
                    else if (container->id == PARSER_STATE_AGGREGATE_LITERAL)
                    {
                        container->aggregate_literal.state = AGGREGATE_LITERAL_STATE_DELIMITER;
                    }
                    else if (container->id == PARSER_STATE_CALL)
                    {
                        container->call.state = CALL_STATE_DELIMITER;
                    }
                    else
                    {
                        container->intrinsic_call.state = INTRINSIC_CALL_STATE_DELIMITER;
                    }
                    parser->recovery = PARSER_RECOVERY_NONE;
                    return;
                }
                if (at_container_depth && accepts_comma && token.id == TOKEN_COMMA)
                {
                    consume(&parser->iterator);
                    if (container->id == PARSER_STATE_ARRAY_LITERAL)
                    {
                        container->array_literal.state = ARRAY_LITERAL_STATE_ELEMENT_OR_END;
                    }
                    else if (container->id == PARSER_STATE_AGGREGATE_LITERAL)
                    {
                        container->aggregate_literal.state = AGGREGATE_LITERAL_STATE_FIELD_OR_END;
                    }
                    else if (container->id == PARSER_STATE_CALL)
                    {
                        container->call.state = CALL_STATE_ARGUMENT_OR_CLOSE;
                    }
                    else
                    {
                        container->intrinsic_call.state = INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE;
                    }
                    parser->recovery = PARSER_RECOVERY_NONE;
                    return;
                }
                if (at_container_depth && token.id == TOKEN_SEMICOLON)
                {
                    break;
                }

                switch (token.id)
                {
                    break;
                case TOKEN_LEFT_PARENTHESIS:
                    parenthesis_depth += 1;
                    break;
                case TOKEN_LEFT_BRACKET:
                    bracket_depth += 1;
                    break;
                case TOKEN_LEFT_BRACE:
                    brace_depth += 1;
                    break;
                case TOKEN_RIGHT_PARENTHESIS:
                {
                    if (parenthesis_depth)
                    {
                        parenthesis_depth -= 1;
                    }
                    else
                    {
                        parser->recovery = PARSER_RECOVERY_STATEMENT;
                    }
                }
                break;
                case TOKEN_RIGHT_BRACKET:
                {
                    if (bracket_depth)
                    {
                        bracket_depth -= 1;
                    }
                    else
                    {
                        parser->recovery = PARSER_RECOVERY_STATEMENT;
                    }
                }
                break;
                case TOKEN_RIGHT_BRACE:
                {
                    if (brace_depth)
                    {
                        brace_depth -= 1;
                    }
                    else
                    {
                        parser->recovery = PARSER_RECOVERY_STATEMENT;
                    }
                }
                break;
                default:
                {
                }
                }
                if (parser->recovery == PARSER_RECOVERY_STATEMENT)
                {
                    break;
                }
                consume(&parser->iterator);
                token = peek(parser);
            }
        }
        parser->recovery = PARSER_RECOVERY_STATEMENT;
    }

    if (parser->recovery == PARSER_RECOVERY_TYPE_DELIMITER)
    {
        ParserState* type_statement = parser_type_declaration_container(parser);
        if (type_statement)
        {
            while (state(parser) != type_statement)
            {
                state_pop(&parser->state);
            }
            parser_type_remove_incomplete_item(type_statement);

            u32 parenthesis_depth = 0;
            u32 bracket_depth = 0;
            u32 brace_depth = 0;
            ExtendedToken token = peek(parser);
            while (token.id != TOKEN_EOF)
            {
                bool at_declaration_depth = parenthesis_depth == 0 && bracket_depth == 0 && brace_depth == 0;
                if (at_declaration_depth && token.id == TOKEN_COMMA)
                {
                    consume(&parser->iterator);
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
                    parser->recovery = PARSER_RECOVERY_NONE;
                    return;
                }
                if (at_declaration_depth && token.id == TOKEN_RIGHT_BRACE)
                {
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
                    parser->recovery = PARSER_RECOVERY_NONE;
                    return;
                }
                if (at_declaration_depth &&
                    (token.id == TOKEN_KEYWORD_CODE || token.id == TOKEN_KEYWORD_TYPE || token.id == TOKEN_KEYWORD_DATA || token.id == TOKEN_KEYWORD_IMPORT))
                {
                    break;
                }

                switch (token.id)
                {
                    break;
                case TOKEN_LEFT_PARENTHESIS:
                    parenthesis_depth += 1;
                    break;
                case TOKEN_LEFT_BRACKET:
                    bracket_depth += 1;
                    break;
                case TOKEN_LEFT_BRACE:
                    brace_depth += 1;
                    break;
                case TOKEN_RIGHT_PARENTHESIS:
                {
                    if (parenthesis_depth)
                    {
                        parenthesis_depth -= 1;
                    }
                }
                break;
                case TOKEN_RIGHT_BRACKET:
                {
                    if (bracket_depth)
                    {
                        bracket_depth -= 1;
                    }
                }
                break;
                case TOKEN_RIGHT_BRACE:
                {
                    if (brace_depth)
                    {
                        brace_depth -= 1;
                    }
                }
                break;
                default:
                {
                }
                }
                consume(&parser->iterator);
                token = peek(parser);
            }
        }
        parser->recovery = PARSER_RECOVERY_DECLARATION;
    }

    if (parser->recovery == PARSER_RECOVERY_SWITCH_CASE)
    {
        while (state(parser)->id != PARSER_STATE_SWITCH_STATEMENT && state(parser)->id != PARSER_STATE_ROOT)
        {
            state_pop(&parser->state);
        }

        if (state(parser)->id == PARSER_STATE_SWITCH_STATEMENT)
        {
            ParserState* switch_state = state(parser);
            AstStatement* statement = switch_state->statement.pointer;
            u32 brace_depth = 0;
            ExtendedToken token = peek(parser);
            while (token.id != TOKEN_EOF)
            {
                if (token.id == TOKEN_COMMA && brace_depth == 0)
                {
                    consume(&parser->iterator);
                    switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_OR_END;
                    parser->recovery = PARSER_RECOVERY_NONE;
                    return;
                }
                if (token.id == TOKEN_RIGHT_BRACE)
                {
                    if (brace_depth == 0)
                    {
                        consume(&parser->iterator);
                        statement->range.length = token.offset + token.length - statement->range.offset;
                        state_pop(&parser->state);
                        parser->recovery = PARSER_RECOVERY_NONE;
                        return;
                    }
                    brace_depth -= 1;
                }
                else if (token.id == TOKEN_LEFT_BRACE)
                {
                    brace_depth += 1;
                }
                consume(&parser->iterator);
                token = peek(parser);
            }
        }
        parser->recovery = PARSER_RECOVERY_STATEMENT;
    }

    if (parser->recovery == PARSER_RECOVERY_STATEMENT)
    {
        AstStatement* statement = 0;
        u32 aggregate_brace_depth = 0;
        while (state(parser)->id != PARSER_STATE_BLOCK && state(parser)->id != PARSER_STATE_ROOT)
        {
            ParserState popped = state_pop(&parser->state);
            if (popped.id == PARSER_STATE_STATEMENT)
            {
                statement = popped.statement.pointer;
            }
            else if (popped.id == PARSER_STATE_AGGREGATE_LITERAL)
            {
                aggregate_brace_depth += 1;
            }
            else if (popped.id == PARSER_STATE_RETURN_STATEMENT || popped.id == PARSER_STATE_DATA_STATEMENT || popped.id == PARSER_STATE_ASSIGNMENT_STATEMENT ||
                     popped.id == PARSER_STATE_IF_STATEMENT || popped.id == PARSER_STATE_FOR_STATEMENT || popped.id == PARSER_STATE_LOOP_STATEMENT ||
                     popped.id == PARSER_STATE_SWITCH_STATEMENT)
            {
                statement = popped.statement.pointer;
            }
        }

        ExtendedToken token = peek(parser);
        while (token.id != TOKEN_EOF)
        {
            if (aggregate_brace_depth == 0 &&
                (token.id == TOKEN_KEYWORD_CODE || token.id == TOKEN_KEYWORD_TYPE || token.id == TOKEN_KEYWORD_DATA || token.id == TOKEN_KEYWORD_IMPORT))
            {
                break;
            }
            if (token.id == TOKEN_SEMICOLON && aggregate_brace_depth == 0)
            {
                break;
            }
            if (token.id == TOKEN_RIGHT_BRACE)
            {
                if (aggregate_brace_depth == 0)
                {
                    break;
                }
                aggregate_brace_depth -= 1;
            }
            else if (token.id == TOKEN_LEFT_BRACE && aggregate_brace_depth)
            {
                aggregate_brace_depth += 1;
            }
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
        else if (token.id == TOKEN_KEYWORD_CODE || token.id == TOKEN_KEYWORD_TYPE || token.id == TOKEN_KEYWORD_DATA || token.id == TOKEN_KEYWORD_IMPORT ||
                 token.id == TOKEN_EOF)
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
        while (token.id != TOKEN_KEYWORD_CODE && token.id != TOKEN_KEYWORD_TYPE && token.id != TOKEN_KEYWORD_DATA && token.id != TOKEN_KEYWORD_IMPORT &&
               token.id != TOKEN_EOF)
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
    IntegerLiteralParsing result = {.valid = true, .fits_u64 = true};
    u32 base = 10;
    u64 index = 0;
    if (id == TOKEN_HEXADECIMAL_INTEGER_LITERAL)
    {
        base = 16;
        index = 2;
    }
    else if (id == TOKEN_OCTAL_INTEGER_LITERAL)
    {
        base = 8;
        index = 2;
    }
    else if (id == TOKEN_BINARY_INTEGER_LITERAL)
    {
        base = 2;
        index = 2;
    }
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
        if (ch >= '0' && ch <= '9')
        {
            digit = (u32)(ch - '0');
        }
        else if (ch >= 'A' && ch <= 'F')
        {
            digit = (u32)(ch - 'A') + 10;
        }
        else if (ch >= 'a' && ch <= 'f')
        {
            digit = (u32)(ch - 'a') + 10;
        }
        else
        {
            result.valid = false;
            break;
        }

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

typedef struct CharacterLiteralParsing CharacterLiteralParsing;
struct CharacterLiteralParsing
{
    u32 code_point;
    u8 utf8[4];
    u8 utf8_length;
    bool escaped;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CharacterLiteralParsing parse_character_literal(String8 spelling)
{
    CharacterLiteralParsing result = {0};
    if (spelling.length < 3 || spelling.pointer[0] != '\'' || spelling.pointer[spelling.length - 1] != '\'')
    {
        return result;
    }

    String8 content = {
        .pointer = spelling.pointer + 1,
        .length = spelling.length - 2,
    };
    if (content.length == 2 && content.pointer[0] == '\\')
    {
        u8 value;
        switch (content.pointer[1])
        {
            break;
        case '0':
            value = 0;
            break;
        case 'n':
            value = '\n';
            break;
        case 'r':
            value = '\r';
            break;
        case 't':
            value = '\t';
            break;
        case '\\':
            value = '\\';
            break;
        case '\'':
            value = '\'';
            break;
        case '"':
            value = '"';
            break;
        default:
            return result;
        }
        result.code_point = value;
        result.utf8[0] = value;
        result.utf8_length = 1;
        result.escaped = true;
        result.valid = true;
        return result;
    }

    if (content.length < 1 || content.length > 4)
    {
        return result;
    }

    u8 first = (u8)content.pointer[0];
    u64 utf8_length = tokenizer_utf8_sequence_length(content.pointer, content.pointer + content.length);
    if (utf8_length != content.length || (first >= 0x80u && utf8_length == 1))
    {
        return result;
    }

    for (u64 i = 0; i < utf8_length; i += 1)
    {
        result.utf8[i] = (u8)content.pointer[i];
    }
    result.utf8_length = (u8)utf8_length;

    switch (utf8_length)
    {
        break;
    case 1:
    {
        result.code_point = first;
    }
    break;
    case 2:
    {
        result.code_point = ((u32)(first & 0x1Fu) << 6) | (u32)(result.utf8[1] & 0x3Fu);
    }
    break;
    case 3:
    {
        result.code_point = ((u32)(first & 0x0Fu) << 12) | ((u32)(result.utf8[1] & 0x3Fu) << 6) | (u32)(result.utf8[2] & 0x3Fu);
    }
    break;
    case 4:
    {
        result.code_point =
            ((u32)(first & 0x07u) << 18) | ((u32)(result.utf8[1] & 0x3Fu) << 12) | ((u32)(result.utf8[2] & 0x3Fu) << 6) | (u32)(result.utf8[3] & 0x3Fu);
    }
    break;
    default:
        BUSTER_UNREACHABLE();
    }

    result.valid = true;
    return result;
}

BUSTER_GLOBAL_LOCAL bool parser_hexadecimal_digit(u8 ch, u32* value)
{
    bool valid = true;
    if (ch >= '0' && ch <= '9')
    {
        *value = (u32)(ch - '0');
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        *value = (u32)(ch - 'A') + 10;
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        *value = (u32)(ch - 'a') + 10;
    }
    else
    {
        valid = false;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL u8 parser_utf8_encode(u32 code_point, u8 output[4])
{
    u8 length;
    if (code_point <= 0x7Fu)
    {
        output[0] = (u8)code_point;
        length = 1;
    }
    else if (code_point <= 0x7FFu)
    {
        output[0] = (u8)(0xC0u | (code_point >> 6));
        output[1] = (u8)(0x80u | (code_point & 0x3Fu));
        length = 2;
    }
    else if (code_point <= 0xFFFFu)
    {
        output[0] = (u8)(0xE0u | (code_point >> 12));
        output[1] = (u8)(0x80u | ((code_point >> 6) & 0x3Fu));
        output[2] = (u8)(0x80u | (code_point & 0x3Fu));
        length = 3;
    }
    else
    {
        output[0] = (u8)(0xF0u | (code_point >> 18));
        output[1] = (u8)(0x80u | ((code_point >> 12) & 0x3Fu));
        output[2] = (u8)(0x80u | ((code_point >> 6) & 0x3Fu));
        output[3] = (u8)(0x80u | (code_point & 0x3Fu));
        length = 4;
    }
    return length;
}

BUSTER_TEST_F_DECL StringLiteralParsing parse_string_literal(Arena* arena, String8 spelling)
{
    char8* decoded = arena_allocate(arena, char8, spelling.length + 1);
    u64 write = 0;
    bool valid = spelling.length >= 2 && spelling.pointer[0] == '"' && spelling.pointer[spelling.length - 1] == '"';
    u64 end = valid ? spelling.length - 1 : spelling.length;

    for (u64 read = valid ? 1 : 0; valid && read < end;)
    {
        u8 ch = (u8)spelling.pointer[read];
        if (ch != '\\')
        {
            u64 length = tokenizer_utf8_sequence_length(spelling.pointer + read, spelling.pointer + end);
            if ((ch >= 0x80u && length == 1) || read + length > end)
            {
                valid = false;
                break;
            }
            for (u64 i = 0; i < length; i += 1)
            {
                decoded[write] = spelling.pointer[read + i];
                write += 1;
            }
            read += length;
            continue;
        }

        read += 1;
        if (read >= end)
        {
            valid = false;
            break;
        }

        u8 escaped = (u8)spelling.pointer[read];
        switch (escaped)
        {
            break;
        case 'n':
            decoded[write] = '\n';
            write += 1;
            read += 1;
            break;
        case 'r':
            decoded[write] = '\r';
            write += 1;
            read += 1;
            break;
        case 't':
            decoded[write] = '\t';
            write += 1;
            read += 1;
            break;
        case '\\':
            decoded[write] = '\\';
            write += 1;
            read += 1;
            break;
        case '\'':
            decoded[write] = '\'';
            write += 1;
            read += 1;
            break;
        case '"':
            decoded[write] = '"';
            write += 1;
            read += 1;
            break;
        case 'x':
        {
            if (read + 2 >= end)
            {
                valid = false;
                break;
            }
            u32 high = 0;
            u32 low = 0;
            valid = parser_hexadecimal_digit((u8)spelling.pointer[read + 1], &high) && parser_hexadecimal_digit((u8)spelling.pointer[read + 2], &low);
            if (valid)
            {
                decoded[write] = (char8)((high << 4) | low);
                write += 1;
                read += 3;
            }
        }
        break;
        case 'u':
        {
            if (read + 1 >= end || spelling.pointer[read + 1] != '{')
            {
                valid = false;
                break;
            }

            u64 digit = read + 2;
            u32 code_point = 0;
            u32 digit_count = 0;
            while (digit < end && spelling.pointer[digit] != '}')
            {
                u32 value = 0;
                if (digit_count == 6 || !parser_hexadecimal_digit((u8)spelling.pointer[digit], &value))
                {
                    valid = false;
                    break;
                }
                code_point = (code_point << 4) | value;
                digit_count += 1;
                digit += 1;
            }
            valid = valid && digit_count > 0 && digit < end && spelling.pointer[digit] == '}' && code_point <= 0x10FFFFu &&
                    !(code_point >= 0xD800u && code_point <= 0xDFFFu);
            if (valid)
            {
                u8 encoded[4];
                u8 encoded_length = parser_utf8_encode(code_point, encoded);
                for (u8 i = 0; i < encoded_length; i += 1)
                {
                    decoded[write] = (char8)encoded[i];
                    write += 1;
                }
                read = digit + 1;
            }
        }
        break;
        default:
        {
            valid = false;
        }
        }
    }

    decoded[write] = 0;
    return (StringLiteralParsing){
        .value = {decoded, write},
        .valid = valid,
    };
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
        is_noise = id == TOKEN_LINE_FEED || id == TOKEN_TAB || id == TOKEN_SPACE || id == TOKEN_COMMENT || id == TOKEN_CARRIAGE_RETURN;

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
    String8 result = {.pointer = (char8*)&source[token.offset], .length = token.length};
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

BUSTER_GLOBAL_LOCAL bool token_begins_type(TokenId id)
{
    bool result = id == TOKEN_IDENTIFIER || id == TOKEN_DOLLAR || id == pointer_token || id == array_slice_token_start || id == TOKEN_KEYWORD_VECTOR ||
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
            break;
        case AST_TYPE_POINTER:
        case AST_TYPE_SLICE:
        case AST_TYPE_INFERRED_ARRAY:
        {
            type = type->element_type;
        }
        break;
        case AST_TYPE_ARRAY:
        {
            type = type->array.element_type;
        }
        break;
        case AST_TYPE_VECTOR:
        {
            type = type->vector.element_type;
        }
        break;
        default:
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
        break;
    case PARSER_STATE_CODE:
    {
        BUSTER_CHECK(resume_state->code.current_state == CODE_STATE_TYPE);
        resume_state->code.current_state = CODE_STATE_AFTER_TYPE;
    }
    break;
    case PARSER_STATE_TYPE_REFERENCE:
    {
        switch (resume_state->type.current_state)
        {
            break;
        case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
        {
            BUSTER_CHECK(resume_state->type.argument);
            parser_source_range_set_end(&resume_state->type.argument->range, end_offset);
            resume_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE;
        }
        break;
        case TYPE_STATE_FUNCTION_RETURN_TYPE:
        {
            resume_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE;
        }
        break;
        default:
            BUSTER_UNREACHABLE();
        }
    }
    break;
    case PARSER_STATE_INTRINSIC_CALL:
    {
        BUSTER_CHECK(resume_state->intrinsic_call.type_argument_destination);
        resume_state->intrinsic_call.state = INTRINSIC_CALL_STATE_DELIMITER;
    }
    break;
    case PARSER_STATE_FOR_STATEMENT:
    {
        BUSTER_CHECK(resume_state->statement.for_state.id == FOR_STATEMENT_STATE_TYPE);
        resume_state->statement.for_state.id = FOR_STATEMENT_STATE_EQUAL;
    }
    break;
    case PARSER_STATE_DATA_DECLARATION:
    {
        BUSTER_CHECK(resume_state->data_declaration.state.id == DATA_STATEMENT_STATE_AFTER_TYPE);
    }
    break;
    case PARSER_STATE_TYPE_STATEMENT:
    {
        if (resume_state->type_statement.state == TYPE_STATEMENT_STATE_ALIAS_TYPE)
        {
            parser_source_range_set_end(&resume_state->type_statement.declaration->range, end_offset);
            state_pop(&parser->state);
        }
        else
        {
            BUSTER_CHECK(resume_state->type_statement.state == TYPE_STATEMENT_STATE_FIELD_TYPE);
            BUSTER_CHECK(resume_state->type_statement.field);
            parser_source_range_set_end(&resume_state->type_statement.field->range, end_offset);
            resume_state->type_statement.state = TYPE_STATEMENT_STATE_FIELD_DELIMITER;
        }
    }
    break;
    default:
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
// arity is encoded by its kind and payload, so no child links are stored.
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
    case AST_NODE_BINARY_BOOLEAN_OR:
    case AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT:
    {
        return BINDING_POWER_BOOLEAN_OR;
    }
    case AST_NODE_BINARY_BOOLEAN_AND:
    case AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT:
    {
        return BINDING_POWER_BOOLEAN_AND;
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
    case AST_NODE_ADDRESS_OF:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_CONSTANT_INTEGER:
    case AST_NODE_CONSTANT_FLOAT:
    case AST_NODE_CONSTANT_CHARACTER:
    case AST_NODE_CONSTANT_STRING:
    case AST_NODE_IDENTIFIER:
    case AST_NODE_UNDEFINED:
    case AST_NODE_ARRAY_LITERAL:
    case AST_NODE_ARRAY_INDEX:
    case AST_NODE_ARRAY_SLICE:
    case AST_NODE_AGGREGATE_LITERAL:
    case AST_NODE_MEMBER_ACCESS:
    case AST_NODE_ENUM_LITERAL:
    case AST_NODE_CALL:
    case AST_NODE_INTRINSIC_CALL:
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
    *block = (AstBlock){.range = source_range_from_token(opening_brace)};
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
    case TOKEN_EQUAL:
        return AST_ASSIGNMENT_EQUAL;
    case TOKEN_PLUS_EQUAL:
        return AST_ASSIGNMENT_PLUS_EQUAL;
    case TOKEN_MINUS_EQUAL:
        return AST_ASSIGNMENT_MINUS_EQUAL;
    case TOKEN_ASTERISK_EQUAL:
        return AST_ASSIGNMENT_MULTIPLY_EQUAL;
    case TOKEN_SLASH_EQUAL:
        return AST_ASSIGNMENT_DIVIDE_EQUAL;
    case TOKEN_PERCENTAGE_EQUAL:
        return AST_ASSIGNMENT_MODULO_EQUAL;
    case TOKEN_SHIFT_LEFT_EQUAL:
        return AST_ASSIGNMENT_SHIFT_LEFT_EQUAL;
    case TOKEN_SHIFT_RIGHT_EQUAL:
        return AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL;
    case TOKEN_AMPERSAND_EQUAL:
        return AST_ASSIGNMENT_BITWISE_AND_EQUAL;
    case TOKEN_BAR_EQUAL:
        return AST_ASSIGNMENT_BITWISE_OR_EQUAL;
    case TOKEN_CARET_EQUAL:
        return AST_ASSIGNMENT_BITWISE_XOR_EQUAL;
    default:
        BUSTER_UNREACHABLE();
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
    state->expression.is_aggregate_field = false;
    state->expression.ends_at_aggregate_delimiter = false;
    state->expression.is_array_subscript_bound = false;
    state->expression.is_array_subscript_end = false;
    state->expression.ends_at_slice_operator = false;
    state->expression.argument_kind = EXPRESSION_ARGUMENT_NONE;
}

BUSTER_GLOBAL_LOCAL void parse_assignment_target(Parser* restrict parser)
{
    // A statement that starts with an expression is ambiguous until either an
    // assignment operator or the statement terminator appears. Calls may end
    // at the terminator and become expression statements; other roots retain
    // the more useful "expected assignment operator" diagnostic.
    TokenId end_token = state(parser)->statement.end_token;
    parse_expression(parser, end_token);
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
    element->expression.is_aggregate_field = false;
    element->expression.ends_at_aggregate_delimiter = false;
    element->expression.is_array_subscript_bound = false;
    element->expression.is_array_subscript_end = false;
    element->expression.ends_at_slice_operator = false;
    element->expression.argument_kind = EXPRESSION_ARGUMENT_NONE;
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

BUSTER_GLOBAL_LOCAL void parse_aggregate_literal(Parser* restrict parser, ExtendedToken opening_brace)
{
    ParserState* aggregate = state_push(&parser->state);
    aggregate->id = PARSER_STATE_AGGREGATE_LITERAL;
    aggregate->aggregate_literal.range = source_range_from_token(opening_brace);
    aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_FIELD_OR_END;
}

BUSTER_GLOBAL_LOCAL void parse_aggregate_field_value(Parser* restrict parser)
{
    ParserState* value = state_push(&parser->state);
    value->id = PARSER_STATE_EXPRESSION;
    value->expression.state = EXPRESSION_STATE_PREFIX;
    value->expression.end_token = TOKEN_ERROR;
    value->expression.is_aggregate_field = true;
    value->expression.ends_at_aggregate_delimiter = true;
}

BUSTER_GLOBAL_LOCAL void parse_enum_member_value(Parser* restrict parser)
{
    ParserState* value = state_push(&parser->state);
    value->id = PARSER_STATE_EXPRESSION;
    value->expression.state = EXPRESSION_STATE_PREFIX;
    value->expression.end_token = TOKEN_ERROR;
    value->expression.is_enum_value = true;
    value->expression.ends_at_enum_delimiter = true;
}

BUSTER_GLOBAL_LOCAL void parse_member_access(Parser* restrict parser, ExtendedToken dot)
{
    ParserState* member = state_push(&parser->state);
    member->id = PARSER_STATE_MEMBER_ACCESS;
    member->member_access.range = source_range_from_token(dot);
}

BUSTER_GLOBAL_LOCAL void parse_enum_literal(Parser* restrict parser, ExtendedToken dot)
{
    ParserState* literal = state_push(&parser->state);
    literal->id = PARSER_STATE_ENUM_LITERAL;
    literal->enum_literal.range = source_range_from_token(dot);
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
    bound->expression.is_aggregate_field = false;
    bound->expression.ends_at_aggregate_delimiter = false;
    bound->expression.is_array_subscript_bound = true;
    bound->expression.is_array_subscript_end = is_end;
    bound->expression.ends_at_slice_operator = !is_end;
    bound->expression.argument_kind = EXPRESSION_ARGUMENT_NONE;
}

BUSTER_GLOBAL_LOCAL void parse_call(Parser* restrict parser, ExtendedToken opening_parenthesis)
{
    ParserState* call = state_push(&parser->state);
    call->id = PARSER_STATE_CALL;
    call->call.range = source_range_from_token(opening_parenthesis);
    call->call.state = CALL_STATE_ARGUMENT_OR_CLOSE;
}

BUSTER_GLOBAL_LOCAL void parse_call_argument(Parser* restrict parser)
{
    ParserState* argument = state_push(&parser->state);
    argument->id = PARSER_STATE_EXPRESSION;
    argument->expression.state = EXPRESSION_STATE_PREFIX;
    argument->expression.end_token = TOKEN_ERROR;
    argument->expression.argument_kind = EXPRESSION_ARGUMENT_CALL;
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
    argument->expression.is_aggregate_field = false;
    argument->expression.ends_at_aggregate_delimiter = false;
    argument->expression.is_array_subscript_bound = false;
    argument->expression.is_array_subscript_end = false;
    argument->expression.ends_at_slice_operator = false;
    argument->expression.argument_kind = EXPRESSION_ARGUMENT_INTRINSIC;
}

BUSTER_GLOBAL_LOCAL ParserState* expression_owner(Parser* restrict parser);
BUSTER_GLOBAL_LOCAL void expression_finish_operand(Parser* restrict parser, ParserState* restrict owner);

BUSTER_GLOBAL_LOCAL void finish_expression(Parser* restrict parser)
{
    ParserState* expression_state = state(parser);
    BUSTER_CHECK(expression_state->id == PARSER_STATE_EXPRESSION);
    u32 output_count = expression_state->expression.output_count;
    BUSTER_CHECK(output_count);

    if (expression_state->expression.is_enum_value)
    {
        AstNode* output = arena_allocate(parser->result_arena, AstNode, output_count);
        memcpy(output, expression_state->expression.output_base, sizeof(*output) * output_count);
        AstExpression expression = {.nodes = output, .count = output_count};
        state_pop(&parser->state);

        ParserState* type_statement = state(parser);
        BUSTER_CHECK(type_statement->id == PARSER_STATE_TYPE_STATEMENT);
        BUSTER_CHECK(type_statement->type_statement.state == TYPE_STATEMENT_STATE_ENUM_MEMBER_VALUE);
        BUSTER_CHECK(type_statement->type_statement.enum_member);
        type_statement->type_statement.enum_member->value = expression;
        type_statement->type_statement.enum_member->has_explicit_value = true;
        parser_source_range_set_end(&type_statement->type_statement.enum_member->range, peek(parser).offset);
        type_statement->type_statement.state = TYPE_STATEMENT_STATE_ENUM_MEMBER_DELIMITER;
        return;
    }

    if (expression_state->expression.argument_kind != EXPRESSION_ARGUMENT_NONE)
    {
        ExpressionArgumentKind argument_kind = (ExpressionArgumentKind)expression_state->expression.argument_kind;
        AstNode* output_base = expression_state->expression.output_base;
        state_pop(&parser->state);

        ParserState* argument_list = state(parser);
        ParserState* owner = argument_list - 1;
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
        if (argument_kind == EXPRESSION_ARGUMENT_CALL)
        {
            BUSTER_CHECK(argument_list->id == PARSER_STATE_CALL);
            argument_list->call.argument_count += 1;
            argument_list->call.state = CALL_STATE_DELIMITER;
        }
        else
        {
            BUSTER_CHECK(argument_kind == EXPRESSION_ARGUMENT_INTRINSIC);
            BUSTER_CHECK(argument_list->id == PARSER_STATE_INTRINSIC_CALL);
            argument_list->intrinsic_call.argument_count += 1;
            argument_list->intrinsic_call.state = INTRINSIC_CALL_STATE_DELIMITER;
        }
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

    if (expression_state->expression.is_aggregate_field)
    {
        AstNode* output_base = expression_state->expression.output_base;
        state_pop(&parser->state);

        ParserState* aggregate = state(parser);
        BUSTER_CHECK(aggregate->id == PARSER_STATE_AGGREGATE_LITERAL);
        ParserState* owner = aggregate - 1;
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
        aggregate->aggregate_literal.field_count += 1;
        aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_DELIMITER;
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
    AstExpression expression = {.nodes = output, .count = output_count};

    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break;
    case PARSER_STATE_RETURN_STATEMENT:
    {
        resume_state->statement.pointer->return_statement.expression = expression;
        resume_state->statement.return_state.id = RETURN_STATEMENT_STATE_END;
    }
    break;
    case PARSER_STATE_DATA_STATEMENT:
    {
        resume_state->statement.pointer->data_statement.initializer = expression;
        resume_state->statement.data_state.id = DATA_STATEMENT_STATE_END;
    }
    break;
    case PARSER_STATE_DATA_DECLARATION:
    {
        resume_state->data_declaration.declaration->initializer = expression;
        resume_state->data_declaration.state.id = DATA_STATEMENT_STATE_END;
    }
    break;
    case PARSER_STATE_ASSIGNMENT_STATEMENT:
    {
        switch (resume_state->statement.assignment_state.id)
        {
            break;
        case ASSIGNMENT_STATEMENT_STATE_TARGET:
        {
            ExtendedToken token = peek(parser);
            if (token_is_assignment_operator(token.id))
            {
                resume_state->statement.pointer->assignment_statement.target = expression;
                resume_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_OPERATOR;
            }
            else
            {
                AstNodeId root_id = expression.nodes[expression.count - 1].id;
                if (root_id != AST_NODE_CALL && root_id != AST_NODE_INTRINSIC_CALL)
                {
                    parser_expected_assignment_operator(parser, token);
                    return;
                }

                resume_state->statement.pointer->id = AST_STATEMENT_EXPRESSION;
                resume_state->statement.pointer->expression_statement.expression = expression;
                resume_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_END;
            }
        }
        break;
        case ASSIGNMENT_STATEMENT_STATE_VALUE:
        {
            resume_state->statement.pointer->assignment_statement.value = expression;
            resume_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_END;
        }
        break;
        default:
            BUSTER_UNREACHABLE();
        }
    }
    break;
    case PARSER_STATE_IF_STATEMENT:
    {
        BUSTER_CHECK(resume_state->statement.if_state.id == IF_STATEMENT_STATE_CONDITION);
        resume_state->statement.pointer->if_statement.condition = expression;
        resume_state->statement.if_state.id = IF_STATEMENT_STATE_CLOSE_CONDITION;
    }
    break;
    case PARSER_STATE_SWITCH_STATEMENT:
    {
        AstSwitchStatement* switch_statement = &resume_state->statement.pointer->switch_statement;
        if (resume_state->statement.switch_state.id == SWITCH_STATEMENT_STATE_EXPRESSION)
        {
            switch_statement->expression = expression;
            resume_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CLOSE_EXPRESSION;
        }
        else
        {
            BUSTER_CHECK(resume_state->statement.switch_state.id == SWITCH_STATEMENT_STATE_CASE_EXPRESSION);
            BUSTER_CHECK(switch_statement->last_case);
            switch_statement->last_case->expression = expression;
            resume_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_ARROW;
        }
    }
    break;
    case PARSER_STATE_FOR_STATEMENT:
    {
        BUSTER_CHECK(resume_state->statement.for_state.id == FOR_STATEMENT_STATE_ITERABLE);
        resume_state->statement.pointer->for_statement.iterable = expression;
        resume_state->statement.for_state.id = FOR_STATEMENT_STATE_CLOSE;
    }
    break;
    case PARSER_STATE_LOOP_STATEMENT:
    {
        BUSTER_CHECK(resume_state->statement.loop_state.id == LOOP_STATEMENT_STATE_CONDITION);
        resume_state->statement.pointer->loop_statement.condition = expression;
        resume_state->statement.pointer->loop_statement.has_condition = true;
        resume_state->statement.loop_state.id = LOOP_STATEMENT_STATE_CLOSE_CONDITION;
    }
    break;
    default:
        BUSTER_UNREACHABLE();
    }
}

BUSTER_TEST_F_DECL String8 string_from_token_id(TokenIdEnum id)
{
    switch (id)
    {
        break;
    case TOKEN_ERROR:
        return S8("Error");
        break;
    case TOKEN_SPACE:
        return S8("Space");
        break;
    case TOKEN_TAB:
        return S8("Tab");
        break;
    case TOKEN_LINE_FEED:
        return S8("LineFeed");
        break;
    case TOKEN_CARRIAGE_RETURN:
        return S8("CarriageReturn");
        break;
    case TOKEN_COMMENT:
        return S8("Comment");
        break;
    case TOKEN_EOF:
        return S8("EOF");
        break;
    case TOKEN_IDENTIFIER:
        return S8("Identifier");
        break;
    case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
        return S8("HexadecimalIntegerLiteral");
        break;
    case TOKEN_DECIMAL_INTEGER_LITERAL:
        return S8("DecimalIntegerLiteral");
        break;
    case TOKEN_OCTAL_INTEGER_LITERAL:
        return S8("OctalIntegerLiteral");
        break;
    case TOKEN_BINARY_INTEGER_LITERAL:
        return S8("BinaryIntegerLiteral");
        break;
    case TOKEN_DECIMAL_FLOAT_LITERAL:
        return S8("DecimalFloatLiteral");
        break;
    case TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT:
        return S8("DecimalFloatLiteralExponent");
        break;
    case TOKEN_HEXADECIMAL_FLOAT_LITERAL:
        return S8("HexadecimalFloatLiteral");
        break;
    case TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT:
        return S8("HexadecimalFloatLiteralExponent");
        break;
    case TOKEN_FLOAT_LITERAL:
        return S8("FloatLiteral");
        break;
    case TOKEN_CHARACTER_LITERAL:
        return S8("CharacterLiteral");
        break;
    case TOKEN_STRING_LITERAL:
        return S8("StringLiteral");
        break;
    case TOKEN_UNDERSCORE:
        return S8("Underscore");
        break;
    case TOKEN_LEFT_BRACKET:
        return S8("LeftBracket");
        break;
    case TOKEN_RIGHT_BRACKET:
        return S8("RightBracket");
        break;
    case TOKEN_LEFT_BRACE:
        return S8("LeftBrace");
        break;
    case TOKEN_RIGHT_BRACE:
        return S8("RightBrace");
        break;
    case TOKEN_LEFT_PARENTHESIS:
        return S8("LeftParenthesis");
        break;
    case TOKEN_RIGHT_PARENTHESIS:
        return S8("RightParenthesis");
        break;
    case TOKEN_EQUAL:
        return S8("Equal");
        break;
    case TOKEN_FAT_ARROW:
        return S8("FatArrow");
        break;
    case TOKEN_EQUAL_EQUAL:
        return S8("EqualEqual");
        break;
    case TOKEN_BANG:
        return S8("Bang");
        break;
    case TOKEN_BANG_EQUAL:
        return S8("BangEqual");
        break;
    case TOKEN_GREATER:
        return S8("Greater");
        break;
    case TOKEN_GREATER_EQUAL:
        return S8("GreaterEqual");
        break;
    case TOKEN_LESS:
        return S8("Less");
        break;
    case TOKEN_LESS_EQUAL:
        return S8("LessEqual");
        break;
    case TOKEN_SHIFT_LEFT:
        return S8("ShiftLeft");
        break;
    case TOKEN_SHIFT_LEFT_EQUAL:
        return S8("ShiftLeftEqual");
        break;
    case TOKEN_SHIFT_RIGHT:
        return S8("ShiftRight");
        break;
    case TOKEN_SHIFT_RIGHT_EQUAL:
        return S8("ShiftRightEqual");
        break;
    case TOKEN_PLUS:
        return S8("Plus");
        break;
    case TOKEN_PLUS_EQUAL:
        return S8("PlusEqual");
        break;
    case TOKEN_MINUS:
        return S8("Minus");
        break;
    case TOKEN_MINUS_EQUAL:
        return S8("MinusEqual");
        break;
    case TOKEN_ASTERISK:
        return S8("Asterisk");
        break;
    case TOKEN_ASTERISK_EQUAL:
        return S8("AsteriskEqual");
        break;
    case TOKEN_SLASH:
        return S8("Slash");
        break;
    case TOKEN_SLASH_EQUAL:
        return S8("SlashEqual");
        break;
    case TOKEN_COLON:
        return S8("Colon");
        break;
    case TOKEN_SEMICOLON:
        return S8("Semicolon");
        break;
    case TOKEN_COMMA:
        return S8("Comma");
        break;
    case TOKEN_DOT:
        return S8("Dot");
        break;
    case TOKEN_DOUBLE_DOT:
        return S8("DoubleDot");
        break;
    case TOKEN_TRIPLE_DOT:
        return S8("TripleDot");
        break;
    case TOKEN_AMPERSAND:
        return S8("Ampersand");
        break;
    case TOKEN_AMPERSAND_EQUAL:
        return S8("AmpersandEqual");
        break;
    case TOKEN_PERCENTAGE:
        return S8("Percent");
        break;
    case TOKEN_PERCENTAGE_EQUAL:
        return S8("PercentEqual");
        break;
    case TOKEN_BAR:
        return S8("Bar");
        break;
    case TOKEN_BAR_EQUAL:
        return S8("BarEqual");
        break;
    case TOKEN_CARET:
        return S8("Caret");
        break;
    case TOKEN_CARET_EQUAL:
        return S8("CaretEqual");
        break;
    case TOKEN_TILDE:
        return S8("Tilde");
        break;
    case TOKEN_AT:
        return S8("At");
        break;
    case TOKEN_DOLLAR:
        return S8("Dollar");
        break;
    case TOKEN_KEYWORD_RETURN:
        return S8("Keyword_Return");
        break;
    case TOKEN_KEYWORD_BREAK:
        return S8("Keyword_Break");
        break;
    case TOKEN_KEYWORD_CONTINUE:
        return S8("Keyword_Continue");
        break;
    case TOKEN_KEYWORD_IF:
        return S8("Keyword_If");
        break;
    case TOKEN_KEYWORD_ELSE:
        return S8("Keyword_Else");
        break;
    case TOKEN_KEYWORD_SWITCH:
        return S8("Keyword_Switch");
        break;
    case TOKEN_KEYWORD_FUNCTION:
        return S8("Keyword_Function");
        break;
    case TOKEN_KEYWORD_FOR:
        return S8("Keyword_For");
        break;
    case TOKEN_KEYWORD_WHILE:
        return S8("Keyword_While");
        break;
    case TOKEN_KEYWORD_LOOP:
        return S8("Keyword_Loop");
        break;
    case TOKEN_KEYWORD_CODE:
        return S8("Keyword_Code");
        break;
    case TOKEN_KEYWORD_DATA:
        return S8("Keyword_Data");
        break;
    case TOKEN_KEYWORD_TYPE:
        return S8("Keyword_Type");
        break;
    case TOKEN_KEYWORD_IMPORT:
        return S8("Keyword_Import");
        break;
    case TOKEN_KEYWORD_STRUCT:
        return S8("Keyword_Struct");
        break;
    case TOKEN_KEYWORD_UNION:
        return S8("Keyword_Union");
        break;
    case TOKEN_KEYWORD_ENUM:
        return S8("Keyword_Enum");
        break;
    case TOKEN_KEYWORD_AND:
        return S8("Keyword_And");
        break;
    case TOKEN_KEYWORD_OR:
        return S8("Keyword_Or");
        break;
    case TOKEN_KEYWORD_AND_SHORT_CIRCUIT:
        return S8("Keyword_AndShortCircuit");
        break;
    case TOKEN_KEYWORD_OR_SHORT_CIRCUIT:
        return S8("Keyword_OrShortCircuit");
        break;
    case TOKEN_KEYWORD_UNDEFINED:
        return S8("Keyword_Undefined");
        break;
    case TOKEN_KEYWORD_VECTOR:
        return S8("Keyword_Vector");
        break;
    case TOKEN_COUNT:
        return S8("Token_Count(Error)");
    }

    BUSTER_UNREACHABLE();
}

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
        AstNodeId id = (AstNodeId)unary_frame.unary_prefix.id;
        AstNode* node = expression_emit(parser, owner, id);
        if (id == AST_NODE_ADDRESS_OF)
        {
            node->pointer_operator.range = unary_frame.unary_prefix.range;
        }
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
        break;
    case TOKEN_AT:
    {
        consume(&parser->iterator);
        ParserState* intrinsic = state_push(&parser->state);
        intrinsic->id = PARSER_STATE_INTRINSIC_CALL;
        intrinsic->intrinsic_call.range = source_range_from_token(token);
        intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_NAME;
        intrinsic->intrinsic_call.argument_count = 0;
    }
    break;
    case TOKEN_DOT:
    {
        consume(&parser->iterator);
        parse_enum_literal(parser, token);
    }
    break;
    case TOKEN_LEFT_BRACKET:
    {
        consume(&parser->iterator);

        ParserState* array_state = state_push(&parser->state);
        array_state->id = PARSER_STATE_ARRAY_LITERAL;
        array_state->array_literal.range = source_range_from_token(token);
        array_state->array_literal.state = ARRAY_LITERAL_STATE_ELEMENT_OR_END;
    }
    break;
    case TOKEN_LEFT_BRACE:
    {
        consume(&parser->iterator);
        parse_aggregate_literal(parser, token);
    }
    break;
    case TOKEN_LEFT_PARENTHESIS:
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
        group_state->expression.is_aggregate_field = false;
        group_state->expression.ends_at_aggregate_delimiter = false;
        group_state->expression.is_array_subscript_bound = false;
        group_state->expression.is_array_subscript_end = false;
        group_state->expression.ends_at_slice_operator = false;
        group_state->expression.argument_kind = EXPRESSION_ARGUMENT_NONE;
    }
    break;
    case TOKEN_IDENTIFIER:
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
    break;
    case TOKEN_KEYWORD_UNDEFINED:
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
    case TOKEN_CHARACTER_LITERAL:
    {
        consume(&parser->iterator);

        String8 spelling = get_string(parser->iterator.source, token);
        CharacterLiteralParsing character = parse_character_literal(spelling);
        if (!character.valid)
        {
            parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_INVALID_CHARACTER, token, TOKEN_ERROR, S8("invalid character literal"));
        }

        ParserState* owner = expression_owner(parser);
        AstNode* leaf = expression_emit(parser, owner, AST_NODE_CONSTANT_CHARACTER);
        leaf->character = (AstCharacterLiteral){
            .spelling = spelling,
            .code_point = character.code_point,
            .utf8 =
                {
                    character.utf8[0],
                    character.utf8[1],
                    character.utf8[2],
                    character.utf8[3],
                },
            .utf8_length = character.utf8_length,
            .escaped = character.escaped,
            .valid = character.valid,
        };

        expression_finish_operand(parser, owner);
    }
    break;
    case TOKEN_STRING_LITERAL:
    {
        consume(&parser->iterator);

        String8 spelling = get_string(parser->iterator.source, token);
        StringLiteralParsing string = parse_string_literal(parser->result_arena, spelling);
        if (!string.valid)
        {
            parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_INVALID_STRING, token, TOKEN_ERROR, S8("invalid string literal"));
        }

        ParserState* owner = expression_owner(parser);
        AstNode* leaf = expression_emit(parser, owner, AST_NODE_CONSTANT_STRING);
        leaf->string = (AstStringLiteral){
            .spelling = spelling,
            .value = string.value,
            .valid = string.valid,
        };

        expression_finish_operand(parser, owner);
    }
    break;
    case TOKEN_DECIMAL_FLOAT_LITERAL:
    case TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT:
    case TOKEN_HEXADECIMAL_FLOAT_LITERAL:
    case TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT:
    {
        consume(&parser->iterator);

        ParserState* owner = expression_owner(parser);
        AstNode* leaf = expression_emit(parser, owner, AST_NODE_CONSTANT_FLOAT);
        leaf->floating = (AstFloatLiteral){
            .spelling = get_string(parser->iterator.source, token),
            .base = (token.id == TOKEN_HEXADECIMAL_FLOAT_LITERAL || token.id == TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT) ? 16 : 10,
            .has_exponent = token.id == TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT || token.id == TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT,
        };

        expression_finish_operand(parser, owner);
    }
    break;
    case TOKEN_MINUS:
    case TOKEN_PLUS:
    case TOKEN_BANG:
    case TOKEN_TILDE:
    case TOKEN_AMPERSAND:
    {
        consume(&parser->iterator);
        AstNodeId unary_id;
        switch (token.id)
        {
            break;
        case TOKEN_MINUS:
            unary_id = AST_NODE_UNARY_MINUS;
            break;
        case TOKEN_PLUS:
            unary_id = AST_NODE_UNARY_PLUS;
            break;
        case TOKEN_BANG:
            unary_id = AST_NODE_UNARY_LOGICAL_NOT;
            break;
        case TOKEN_TILDE:
            unary_id = AST_NODE_UNARY_BITWISE_NOT;
            break;
        case TOKEN_AMPERSAND:
            unary_id = AST_NODE_ADDRESS_OF;
            break;
        default:
            BUSTER_UNREACHABLE();
        }
        ParserState* unary_state = state_push(&parser->state);
        unary_state->id = PARSER_STATE_UNARY_PREFIX;
        unary_state->unary_prefix.id = (u8)unary_id;
        unary_state->unary_prefix.range = source_range_from_token(token);
    }
    break;
    default:
    {
        ParserState* owner = expression_owner(parser);
        parser_diagnostic_push(parser, PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION, token, TOKEN_ERROR, S8("expected expression"));
        if (owner->expression.is_enum_value || parser_type_declaration_container(parser))
        {
            parser->recovery = PARSER_RECOVERY_TYPE_DELIMITER;
        }
        else
        {
            parser->recovery = parser_expression_container(parser) ? PARSER_RECOVERY_EXPRESSION_DELIMITER : PARSER_RECOVERY_STATEMENT;
        }
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

BUSTER_GLOBAL_LOCAL void finish_aggregate_literal(Parser* restrict parser, ExtendedToken closing_brace)
{
    ParserState aggregate = state_pop(&parser->state);
    BUSTER_CHECK(aggregate.id == PARSER_STATE_AGGREGATE_LITERAL);

    ParserSourceRange range = aggregate.aggregate_literal.range;
    parser_source_range_set_end(&range, closing_brace.offset + closing_brace.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_AGGREGATE_LITERAL);
    node->aggregate_literal = (AstAggregateLiteral){
        .first_field = aggregate.aggregate_literal.first_field,
        .last_field = aggregate.aggregate_literal.last_field,
        .range = range,
        .field_count = aggregate.aggregate_literal.field_count,
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

BUSTER_GLOBAL_LOCAL void finish_member_access(Parser* restrict parser, ExtendedToken name)
{
    ParserState member = state_pop(&parser->state);
    BUSTER_CHECK(member.id == PARSER_STATE_MEMBER_ACCESS);

    ParserSourceRange range = member.member_access.range;
    parser_source_range_set_end(&range, name.offset + name.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_MEMBER_ACCESS);
    node->member_access = (AstMemberAccess){
        .member =
            {
                .text = get_string(parser->iterator.source, name),
                .range = source_range_from_token(name),
            },
        .range = range,
    };
    expression_finish_operand(parser, owner);
}

BUSTER_GLOBAL_LOCAL void finish_pointer_dereference(Parser* restrict parser, ExtendedToken ampersand)
{
    ParserState member = state_pop(&parser->state);
    BUSTER_CHECK(member.id == PARSER_STATE_MEMBER_ACCESS);

    ParserSourceRange range = member.member_access.range;
    parser_source_range_set_end(&range, ampersand.offset + ampersand.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_DEREFERENCE);
    node->pointer_operator.range = range;
    expression_finish_operand(parser, owner);
}

BUSTER_GLOBAL_LOCAL void finish_enum_literal(Parser* restrict parser, ExtendedToken name)
{
    ParserState literal = state_pop(&parser->state);
    BUSTER_CHECK(literal.id == PARSER_STATE_ENUM_LITERAL);

    ParserSourceRange range = literal.enum_literal.range;
    parser_source_range_set_end(&range, name.offset + name.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_ENUM_LITERAL);
    node->enum_literal = (AstEnumLiteral){
        .member =
            {
                .text = get_string(parser->iterator.source, name),
                .range = source_range_from_token(name),
            },
        .range = range,
    };
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
        .type_argument = intrinsic.intrinsic_call.type_argument_destination ? *intrinsic.intrinsic_call.type_argument_destination : 0,
        .argument_count = intrinsic.intrinsic_call.argument_count,
    };
    expression_finish_operand(parser, owner);
}

BUSTER_GLOBAL_LOCAL void finish_call(Parser* restrict parser, ExtendedToken closing_parenthesis)
{
    ParserState call = state_pop(&parser->state);
    BUSTER_CHECK(call.id == PARSER_STATE_CALL);

    ParserSourceRange range = call.call.range;
    parser_source_range_set_end(&range, closing_parenthesis.offset + closing_parenthesis.length);

    ParserState* owner = expression_owner(parser);
    AstNode* node = expression_emit(parser, owner, AST_NODE_CALL);
    node->call = (AstCall){
        .range = range,
        .argument_count = call.call.argument_count,
    };
    expression_finish_operand(parser, owner);
}

ParserResult parser_parse(Arena* result_arena, Arena* expression_arena, String8 source, TokenizerResult tokenizer)
{
    BUSTER_CHECK(result_arena);
    BUSTER_CHECK(expression_arena);
    BUSTER_CHECK(result_arena != expression_arena);

    // Expression nodes are copied into result_arena when an expression is
    // finished. Rewind this caller-owned staging arena for every parse so one
    // reservation can be reused across an entire test or benchmark run.
    arena_reset_to_start(expression_arena);

    ParserResult result = {.source = source};
    Arena* scratch_conflicts[] = {result_arena, expression_arena};
    TemporalArena scratch = scratch_begin(scratch_conflicts, BUSTER_ARRAY_LENGTH(scratch_conflicts));
    Parser parser = {0};
    parser.iterator.tokens = tokenizer.tokens;
    parser.iterator.token_count = tokenizer.token_count;
    parser.iterator.source = source.pointer;
    parser.state.arena = scratch.arena;
    parser.state.minimum_position = scratch.arena->position;
    parser.result_arena = result_arena;
    parser.expression_arena = expression_arena;
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
            break;
        case PARSER_STATE_COUNT:
            BUSTER_UNREACHABLE();
            break;
        case PARSER_STATE_ROOT:
        {
            ExtendedToken token = peek_and_consume(&parser);

            switch (token.id)
            {
                break;
            case TOKEN_KEYWORD_CODE:
            {
                ParserState* function_state = state_push(&parser.state);
                function_state->id = PARSER_STATE_CODE;
                function_state->code.current_state = CODE_STATE_BEFORE_NAME;
                function_state->code.code = parser_code_push(&parser, token);
            }
            break;
            case TOKEN_KEYWORD_TYPE:
            {
                ParserState* type_statement = state_push(&parser.state);
                type_statement->id = PARSER_STATE_TYPE_STATEMENT;
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_NAME;
                type_statement->type_statement.declaration = parser_type_declaration_push(&parser, token);
            }
            break;
            case TOKEN_KEYWORD_IMPORT:
            {
                ParserState* import_state = state_push(&parser.state);
                import_state->id = PARSER_STATE_IMPORT;
                import_state->import.state = IMPORT_STATE_NAMESPACE;
                import_state->import.import = parser_import_push(&parser, token);
            }
            break;
            case TOKEN_KEYWORD_DATA:
            {
                ParserState* data_state = state_push(&parser.state);
                data_state->id = PARSER_STATE_DATA_DECLARATION;
                data_state->data_declaration.state.id = DATA_STATEMENT_STATE_NAME;
                data_state->data_declaration.declaration = parser_data_declaration_push(&parser, token);
            }
            break;
            case TOKEN_EOF:
            {
                is_running = false;
            }
            break;
            default:
                parser_unexpected(&parser, token, TOKEN_KEYWORD_CODE);
            }
        }
        break;
        case PARSER_STATE_DATA_DECLARATION:
        {
            ParserState* data_state = state(&parser);
            AstDataDeclaration* data = data_state->data_declaration.declaration;
            ExtendedToken token = peek(&parser);
            switch (data_state->data_declaration.state.id)
            {
                break;
            case DATA_STATEMENT_STATE_NAME:
            {
                if (token.id == TOKEN_DOLLAR)
                {
                    consume(&parser.iterator);
                    data->is_compile_time = true;
                }
                else if (token.id == TOKEN_IDENTIFIER)
                {
                    if (!data->is_compile_time)
                    {
                        parser_unexpected(&parser, token, TOKEN_DOLLAR);
                        break;
                    }
                    consume(&parser.iterator);
                    data->name = (AstIdentifier){
                        .text = get_string(parser.iterator.source, token),
                        .range = source_range_from_token(token),
                    };
                    data_state->data_declaration.state.id = DATA_STATEMENT_STATE_AFTER_NAME;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case DATA_STATEMENT_STATE_AFTER_NAME:
            {
                if (token.id == TOKEN_COLON)
                {
                    consume(&parser.iterator);
                    ParserState* type_state = state_push(&parser.state);
                    type_state->id = PARSER_STATE_TYPE_REFERENCE;
                    type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    type_state->type.destination = &data->type;
                }
                data_state->data_declaration.state.id = DATA_STATEMENT_STATE_AFTER_TYPE;
            }
            break;
            case DATA_STATEMENT_STATE_AFTER_TYPE:
            {
                if (token.id == TOKEN_EQUAL)
                {
                    consume(&parser.iterator);
                    data_state->data_declaration.state.id = DATA_STATEMENT_STATE_INITIALIZER;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                }
            }
            break;
            case DATA_STATEMENT_STATE_INITIALIZER:
            {
                parse_expression(&parser, TOKEN_SEMICOLON);
            }
            break;
            case DATA_STATEMENT_STATE_END:
            {
                if (token.id == TOKEN_SEMICOLON)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&data->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_SEMICOLON);
                }
            }
            break;
            case DATA_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_IMPORT:
        {
            ParserState* import_state = state(&parser);
            AstImport* import = import_state->import.import;
            ExtendedToken token = peek(&parser);

            switch (import_state->import.state)
            {
                break;
            case IMPORT_STATE_NAMESPACE:
            {
                if (token.id == TOKEN_IDENTIFIER)
                {
                    consume(&parser.iterator);
                    import->name_space = (AstIdentifier){
                        .text = get_string(parser.iterator.source, token),
                        .range = source_range_from_token(token),
                    };
                    import_state->import.state = IMPORT_STATE_EQUAL;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case IMPORT_STATE_EQUAL:
            {
                if (token.id == TOKEN_EQUAL)
                {
                    consume(&parser.iterator);
                    import_state->import.state = IMPORT_STATE_PATH;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                }
            }
            break;
            case IMPORT_STATE_PATH:
            {
                if (token.id == TOKEN_STRING_LITERAL)
                {
                    consume(&parser.iterator);
                    String8 spelling = get_string(parser.iterator.source, token);
                    StringLiteralParsing string = parse_string_literal(parser.result_arena, spelling);
                    if (!string.valid)
                    {
                        parser_diagnostic_push(&parser, PARSER_DIAGNOSTIC_INVALID_STRING, token, TOKEN_ERROR, S8("invalid string literal"));
                    }
                    import->path = string.value;
                    import->path_range = source_range_from_token(token);
                    import_state->import.state = IMPORT_STATE_SEMICOLON;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_STRING_LITERAL);
                }
            }
            break;
            case IMPORT_STATE_SEMICOLON:
            {
                if (token.id == TOKEN_SEMICOLON)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&import->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_SEMICOLON);
                }
            }
            break;
            case IMPORT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_CODE:
        {
            ParserState* code_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (code_state->code.current_state)
            {
                break;
            case CODE_STATE_BEFORE_NAME:
            {
                consume(&parser.iterator);

                switch (token.id)
                {
                    break;
                case TOKEN_LEFT_BRACKET:
                {
                    ParserState* attribute_list_state = state_push(&parser.state);
                    attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
                    attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_CODE;
                    attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                    attribute_list_state->attribute_list.code = code_state->code.code;
                }
                break;
                case TOKEN_IDENTIFIER:
                {
                    code_state->code.code->name = get_string(parser.iterator.source, token);
                    code_state->code.current_state = CODE_STATE_AFTER_NAME;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case CODE_STATE_AFTER_NAME:
            {
                consume(&parser.iterator);

                switch (token.id)
                {
                    break;
                case TOKEN_COLON:
                {
                    code_state->code.current_state = CODE_STATE_TYPE;

                    ParserState* type_state = state_push(&parser.state);
                    type_state->id = PARSER_STATE_TYPE_REFERENCE;
                    type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    type_state->type.code = code_state->code.code;
                    type_state->type.destination = &code_state->code.code->type;
                }
                break;
                case TOKEN_EQUAL:
                {
                    parser_unexpected(&parser, token, TOKEN_COLON);
                }
                break;
                case TOKEN_LEFT_BRACKET:
                {
                    ParserState* attribute_list_state = state_push(&parser.state);
                    attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
                    attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_SYMBOL;
                    attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                    attribute_list_state->attribute_list.code = code_state->code.code;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_COLON);
                }
            }
            break;
            case CODE_STATE_TYPE:
            {
                BUSTER_UNREACHABLE();
            }
            break;
            case CODE_STATE_AFTER_TYPE:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_LEFT_BRACE:
                {
                    consume(&parser.iterator);

                    code_state->code.current_state = CODE_STATE_BODY;
                    code_state->code.code->has_body = true;
                    parse_block(&parser, &code_state->code.code->body, token);
                }
                break;
                case TOKEN_SEMICOLON:
                {
                    consume(&parser.iterator);
                    code_state->code.code->range.length = token.offset + token.length - code_state->code.code->range.offset;
                    state_pop(&parser.state);
                }
                break;
                case TOKEN_EQUAL:
                {
                    consume(&parser.iterator);
                    code_state->code.current_state = CODE_STATE_AFTER_EQUAL;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_LEFT_BRACE);
                }
            }
            break;
            case CODE_STATE_AFTER_EQUAL:
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
            break;
            case CODE_STATE_BODY:
            {
                code_state->code.code->range.length =
                    code_state->code.code->body.range.offset + code_state->code.code->body.range.length - code_state->code.code->range.offset;
                state_pop(&parser.state);
            }
            break;
            case CODE_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_TYPE_REFERENCE:
        {
            ParserState* type_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (type_state->type.current_state)
            {
                break;
            case TYPE_STATE_PREFIX_OR_BASE:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_DOLLAR:
                {
                    consume(&parser.iterator);
                    if (type_state->type.compile_time_prefix)
                    {
                        parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    }
                    else
                    {
                        type_state->type.compile_time_prefix = true;
                    }
                }
                break;
                case pointer_token:
                {
                    consume(&parser.iterator);
                    AstType* pointer = parser_type_attach(&parser, type_state, AST_TYPE_POINTER, token);
                    type_state->type.destination = &pointer->element_type;
                }
                break;
                case array_slice_token_start:
                {
                    consume(&parser.iterator);
                    type_state->type.prefix_range = source_range_from_token(token);
                    type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_SLICE_START;
                }
                break;
                case TOKEN_KEYWORD_VECTOR:
                {
                    consume(&parser.iterator);
                    type_state->type.prefix_range = source_range_from_token(token);
                    type_state->type.current_state = TYPE_STATE_AFTER_VECTOR_KEYWORD;
                }
                break;
                case TOKEN_IDENTIFIER:
                {
                    consume(&parser.iterator);
                    ExtendedToken next = peek(&parser);
                    if (next.id == TOKEN_DOT)
                    {
                        consume(&parser.iterator);
                        AstType* qualified = parser_type_attach(&parser, type_state, AST_TYPE_QUALIFIED_NAMED, token);
                        qualified->qualified.name_space = (AstIdentifier){
                            .text = get_string(parser.iterator.source, token),
                            .range = source_range_from_token(token),
                        };
                        type_state->type.qualified = qualified;
                        type_state->type.current_state = TYPE_STATE_QUALIFIED_NAME;
                    }
                    else
                    {
                        AstType* named = parser_type_attach(&parser, type_state, AST_TYPE_NAMED, token);
                        named->name = get_string(parser.iterator.source, token);
                        finish_type_reference(&parser, token.offset + token.length);
                    }
                }
                break;
                case TOKEN_KEYWORD_FUNCTION:
                {
                    consume(&parser.iterator);
                    type_state->type.type = parser_type_attach(&parser, type_state, AST_TYPE_FUNCTION, token);
                    type_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_KEYWORD;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case TYPE_STATE_QUALIFIED_NAME:
            {
                if (token.id == TOKEN_IDENTIFIER)
                {
                    consume(&parser.iterator);
                    AstType* qualified = type_state->type.qualified;
                    BUSTER_CHECK(qualified->id == AST_TYPE_QUALIFIED_NAMED);
                    qualified->qualified.name = (AstIdentifier){
                        .text = get_string(parser.iterator.source, token),
                        .range = source_range_from_token(token),
                    };
                    finish_type_reference(&parser, token.offset + token.length);
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case TYPE_STATE_AFTER_ARRAY_SLICE_START:
            {
                switch (token.id)
                {
                    break;
                case array_slice_token_end:
                {
                    consume(&parser.iterator);
                    ParserSourceRange range = type_state->type.prefix_range;
                    AstType* slice = parser_type_attach(&parser, type_state, AST_TYPE_SLICE,
                                                        (ExtendedToken){
                                                            .offset = range.offset,
                                                            .length = range.length,
                                                            .line = range.line,
                                                            .column = range.column,
                                                        });
                    type_state->type.destination = &slice->element_type;
                    type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                }
                break;
                case TOKEN_UNDERSCORE:
                {
                    consume(&parser.iterator);
                    ParserSourceRange range = type_state->type.prefix_range;
                    AstType* inferred_array = parser_type_attach(&parser, type_state, AST_TYPE_INFERRED_ARRAY,
                                                                 (ExtendedToken){
                                                                     .offset = range.offset,
                                                                     .length = range.length,
                                                                     .line = range.line,
                                                                     .column = range.column,
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
                    AstType* array = parser_type_attach(&parser, type_state, AST_TYPE_ARRAY,
                                                        (ExtendedToken){
                                                            .offset = range.offset,
                                                            .length = range.length,
                                                            .line = range.line,
                                                            .column = range.column,
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
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                }
            }
            break;
            case TYPE_STATE_AFTER_ARRAY_COUNT:
            {
                if (token.id != TOKEN_RIGHT_BRACKET)
                {
                    parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                    continue;
                }

                consume(&parser.iterator);
                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
            }
            break;
            case TYPE_STATE_AFTER_ARRAY_INFER_MARKER:
            {
                if (token.id != TOKEN_RIGHT_BRACKET)
                {
                    parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                    continue;
                }

                consume(&parser.iterator);
                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
            }
            break;
            case TYPE_STATE_AFTER_VECTOR_KEYWORD:
            {
                if (token.id != TOKEN_LEFT_BRACKET)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_BRACKET);
                    continue;
                }

                consume(&parser.iterator);
                type_state->type.current_state = TYPE_STATE_AFTER_VECTOR_OPEN;
            }
            break;
            case TYPE_STATE_AFTER_VECTOR_OPEN:
            {
                switch (token.id)
                {
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
                        parser_diagnostic_push(&parser, PARSER_DIAGNOSTIC_INVALID_INTEGER, token, TOKEN_ERROR, S8("invalid vector lane count"));
                    }

                    ParserSourceRange range = type_state->type.prefix_range;
                    AstType* vector = parser_type_attach(&parser, type_state, AST_TYPE_VECTOR,
                                                         (ExtendedToken){
                                                             .offset = range.offset,
                                                             .length = range.length,
                                                             .line = range.line,
                                                             .column = range.column,
                                                         });
                    vector->vector.count = (AstIntegerLiteral){
                        .spelling = count_string,
                        .value = count.value,
                        .base = count.base,
                        .fits_u64 = count.fits_u64,
                    };
                    type_state->type.destination = &vector->vector.element_type;
                    type_state->type.current_state = TYPE_STATE_AFTER_VECTOR_COUNT;
                }
                break;
                default:
                {
                    parser_unexpected(&parser, token, TOKEN_DECIMAL_INTEGER_LITERAL);
                }
                }
            }
            break;
            case TYPE_STATE_AFTER_VECTOR_COUNT:
            {
                if (token.id != TOKEN_RIGHT_BRACKET)
                {
                    parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                    continue;
                }

                consume(&parser.iterator);
                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
            }
            break;
            case TYPE_STATE_AFTER_FUNCTION_KEYWORD:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_LEFT_BRACKET:
                {
                    consume(&parser.iterator);

                    ParserState* attribute_list_state = state_push(&parser.state);
                    attribute_list_state->id = PARSER_STATE_ATTRIBUTE_LIST;
                    attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_FUNCTION;
                    attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                    attribute_list_state->attribute_list.code = type_state->type.code;
                    attribute_list_state->attribute_list.type = type_state->type.type;
                }
                break;
                case TOKEN_LEFT_PARENTHESIS:
                {
                    consume(&parser.iterator);
                    type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                }
            }
            break;
            case TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_RIGHT_PARENTHESIS:
                {
                    consume(&parser.iterator);
                    type_state->type.current_state = TYPE_STATE_FUNCTION_RETURN_TYPE;
                }
                break;
                case TOKEN_DOLLAR:
                {
                    consume(&parser.iterator);
                    if (type_state->type.argument_is_compile_time)
                    {
                        parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    }
                    else
                    {
                        type_state->type.argument_is_compile_time = true;
                    }
                }
                break;
                case TOKEN_TRIPLE_DOT:
                {
                    consume(&parser.iterator);
                    type_state->type.type->function.is_variadic = true;
                    type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE;
                }
                break;
                case TOKEN_IDENTIFIER:
                {
                    consume(&parser.iterator);
                    type_state->type.argument = parser_type_argument_push(&parser, type_state->type.type, token);
                    type_state->type.argument->is_compile_time = type_state->type.argument_is_compile_time;
                    type_state->type.argument_is_compile_time = false;
                    type_state->type.name_range = source_range_from_token(token);
                    type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT:
            {
                if (token.id != TOKEN_COLON)
                {
                    parser_unexpected(&parser, token, TOKEN_COLON);
                    continue;
                }

                consume(&parser.iterator);
                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_COLON;
            }
            break;
            case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_COLON:
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
            break;
            case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
            {
                BUSTER_UNREACHABLE();
            }
            break;
            case TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_COMMA:
                {
                    if (type_state->type.type->function.is_variadic)
                    {
                        parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                        continue;
                    }
                    consume(&parser.iterator);
                    type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE;
                }
                break;
                case TOKEN_RIGHT_PARENTHESIS:
                {
                    consume(&parser.iterator);
                    type_state->type.current_state = TYPE_STATE_FUNCTION_RETURN_TYPE;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                }
            }
            break;
            case TYPE_STATE_FUNCTION_RETURN_TYPE:
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
            break;
            case TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE:
            {
                AstType* return_type = type_state->type.type->function.return_type;
                finish_type_reference(&parser, return_type->range.offset + return_type->range.length);
            }
            break;
            case TYPE_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_ATTRIBUTE_LIST:
        {
            ParserState* attribute_list_state = state(&parser);
            ExtendedToken token = peek_and_consume(&parser);

            switch (attribute_list_state->attribute_list.current_state)
            {
                break;
            case ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE:
            {
                switch (token.id)
                {
                    break;
                case TOKEN_RIGHT_BRACKET:
                {
                    state_pop(&parser.state);
                }
                break;
                case TOKEN_IDENTIFIER:
                {
                    String8 attribute_name = get_string(parser.iterator.source, token);

                    switch (attribute_list_state->attribute_list.kind)
                    {
                        break;
                    case ATTRIBUTE_LIST_CODE:
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
                    break;
                    case ATTRIBUTE_LIST_DATA:
                    {
                        parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    }
                    break;
                    case ATTRIBUTE_LIST_SYMBOL:
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
                            break;
                        case SYMBOL_ATTRIBUTE_EXPORT:
                        {
                            attribute_list_state->attribute_list.code->exported = true;
                        }
                        break;
                        case SYMBOL_ATTRIBUTE_COUNT:
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                            break;
                        }
                    }
                    break;
                    case ATTRIBUTE_LIST_FUNCTION:
                    {
                        if (!string_equal(attribute_name, function_attribute_names[FUNCTION_ATTRIBUTE_CALLING_CONVENTION]))
                        {
                            parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN;
                    }
                    break;
                    case ATTRIBUTE_LIST_COUNT:
                        BUSTER_UNREACHABLE();
                    }
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_RIGHT_BRACKET);
                }
            }
            break;
            case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN:
            {
                if (token.id != TOKEN_LEFT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                    continue;
                }

                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME;
            }
            break;
            case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME:
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
            break;
            case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE:
            {
                if (token.id != TOKEN_RIGHT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_RIGHT_PARENTHESIS);
                    continue;
                }

                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
            }
            break;
            case ATTRIBUTE_LIST_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_BLOCK:
        {
            ParserState* block_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (token.id)
            {
                break;
            case TOKEN_RIGHT_BRACE:
            {
                consume(&parser.iterator);

                if (block_state->block.brace_depth == 0)
                {
                    BUSTER_UNREACHABLE();
                }

                block_state->block.brace_depth -= 1;
                if (block_state->block.brace_depth == 0)
                {
                    block_state->block.block->range.length = token.offset + token.length - block_state->block.block->range.offset;
                    state_pop(&parser.state);
                }
            }
            break;
            default:
            {
                ParserState* statement_state = state_push(&parser.state);
                statement_state->id = PARSER_STATE_STATEMENT;
                statement_state->statement.statement_state = STATEMENT_STATE_START;
            }
            }
        }
        break;
        case PARSER_STATE_STATEMENT:
        {
            ParserState* statement_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (statement_state->statement.statement_state)
            {
                break;
            case STATEMENT_STATE_START:
            {
                statement_state->statement.statement_state = STATEMENT_STATE_END;
                statement_state->statement.end_token = block_end_of_statement_token;

                ParserState* block_state = state_previous(&parser.state);
                BUSTER_CHECK(block_state->id == PARSER_STATE_BLOCK);

                switch (token.id)
                {
                    break;
                case TOKEN_KEYWORD_RETURN:
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
                break;
                case TOKEN_KEYWORD_DATA:
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
                break;
                case TOKEN_KEYWORD_BREAK:
                {
                    consume(&parser.iterator);
                    AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_BREAK, token);
                    statement_state->statement.pointer = statement;
                }
                break;
                case TOKEN_KEYWORD_CONTINUE:
                {
                    consume(&parser.iterator);
                    AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_CONTINUE, token);
                    statement_state->statement.pointer = statement;
                }
                break;
                case TOKEN_KEYWORD_IF:
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
                break;
                case TOKEN_KEYWORD_SWITCH:
                {
                    consume(&parser.iterator);
                    AstStatement* statement = parser_statement_push(&parser, block_state->block.block, AST_STATEMENT_SWITCH, token);
                    statement_state->statement.pointer = statement;
                    statement_state->statement.end_token = TOKEN_ERROR;

                    ParserState* state = state_push(&parser.state);
                    state->id = PARSER_STATE_SWITCH_STATEMENT;
                    state->statement.switch_state.id = SWITCH_STATEMENT_STATE_OPEN_EXPRESSION;
                    state->statement.pointer = statement;
                    state->statement.end_token = TOKEN_ERROR;
                }
                break;
                case TOKEN_KEYWORD_FOR:
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
                break;
                case TOKEN_KEYWORD_LOOP:
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
                case TOKEN_AT:
                case TOKEN_LEFT_BRACKET:
                case TOKEN_LEFT_BRACE:
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
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_KEYWORD_RETURN);
                }
            }
            break;
            case STATEMENT_STATE_END:
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
            break;
            case STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_RETURN_STATEMENT:
        {
            ParserState* return_statement_state = state(&parser);
            ExtendedToken token = peek(&parser);

            TokenId end_of_statement_token = return_statement_state->statement.end_token;

            switch (return_statement_state->statement.return_state.id)
            {
                break;
            case RETURN_STATEMENT_STATE_VALUE_OR_END:
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
            break;
            case RETURN_STATEMENT_STATE_END:
            {
                if (token.id != end_of_statement_token)
                {
                    parser_unexpected(&parser, token, end_of_statement_token);
                    continue;
                }

                state_pop(&parser.state);
            }
            break;
            case RETURN_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_DATA_STATEMENT:
        {
            ParserState* data_statement_state = state(&parser);
            ExtendedToken token = peek(&parser);

            TokenIdEnum start_token_id = (TokenIdEnum)token.id;
            TokenId end_of_statement_token = data_statement_state->statement.end_token;

            BUSTER_UNUSED(end_of_statement_token);

            switch (data_statement_state->statement.data_state.id)
            {
                break;
            case DATA_STATEMENT_STATE_NAME:
            {
                switch (start_token_id)
                {
                    break;
                case TOKEN_DOLLAR:
                {
                    consume(&parser.iterator);
                    AstDataStatement* data = &data_statement_state->statement.pointer->data_statement;
                    if (data->is_compile_time)
                    {
                        parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    }
                    else
                    {
                        data->is_compile_time = true;
                    }
                }
                break;
                case TOKEN_IDENTIFIER:
                {
                    consume(&parser.iterator);
                    String8 name = get_string(parser.iterator.source, token);
                    ParserSourceRange range = source_range_from_token(token);
                    data_statement_state->statement.pointer->data_statement.name = (AstIdentifier){
                        .range = range,
                        .text = name,
                    };
                    data_statement_state->statement.data_state.id = DATA_STATEMENT_STATE_AFTER_NAME;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case DATA_STATEMENT_STATE_AFTER_NAME:
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
            break;
            case DATA_STATEMENT_STATE_AFTER_TYPE:
            {
                switch (start_token_id)
                {
                    break;
                case TOKEN_EQUAL:
                {
                    consume(&parser.iterator);
                    data_statement_state->statement.data_state.id = DATA_STATEMENT_STATE_INITIALIZER;
                }
                break;
                default:
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                }
            }
            break;
            case DATA_STATEMENT_STATE_INITIALIZER:
            {
                parse_expression(&parser, data_statement_state->statement.end_token);
            }
            break;
            case DATA_STATEMENT_STATE_END:
            {
                if (token.id != end_of_statement_token)
                {
                    parser_unexpected(&parser, token, end_of_statement_token);
                    continue;
                }

                state_pop(&parser.state);
            }
            break;
            case DATA_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_ASSIGNMENT_STATEMENT:
        {
            ParserState* assignment_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (assignment_state->statement.assignment_state.id)
            {
                break;
            case ASSIGNMENT_STATEMENT_STATE_TARGET:
            {
                parse_assignment_target(&parser);
            }
            break;
            case ASSIGNMENT_STATEMENT_STATE_OPERATOR:
            {
                if (!token_is_assignment_operator(token.id))
                {
                    parser_expected_assignment_operator(&parser, token);
                    continue;
                }

                assignment_state->statement.pointer->assignment_statement.operator= assignment_operator_from_token(token.id);
                consume(&parser.iterator);
                assignment_state->statement.assignment_state.id = ASSIGNMENT_STATEMENT_STATE_VALUE;
            }
            break;
            case ASSIGNMENT_STATEMENT_STATE_VALUE:
            {
                parse_expression(&parser, assignment_state->statement.end_token);
            }
            break;
            case ASSIGNMENT_STATEMENT_STATE_END:
            {
                if (token.id != assignment_state->statement.end_token)
                {
                    parser_unexpected(&parser, token, assignment_state->statement.end_token);
                    continue;
                }

                state_pop(&parser.state);
            }
            break;
            case ASSIGNMENT_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_IF_STATEMENT:
        {
            ParserState* if_state = state(&parser);
            ExtendedToken token = peek(&parser);
            AstStatement* statement = if_state->statement.pointer;

            switch (if_state->statement.if_state.id)
            {
                break;
            case IF_STATEMENT_STATE_OPEN_CONDITION:
            {
                if (token.id != TOKEN_LEFT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                    continue;
                }

                consume(&parser.iterator);
                if_state->statement.if_state.id = IF_STATEMENT_STATE_CONDITION;
            }
            break;
            case IF_STATEMENT_STATE_CONDITION:
            {
                parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
            }
            break;
            case IF_STATEMENT_STATE_CLOSE_CONDITION:
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
            break;
            case IF_STATEMENT_STATE_THEN_BLOCK:
            {
                if_state->statement.if_state.id = IF_STATEMENT_STATE_ELSE_OR_END;
            }
            break;
            case IF_STATEMENT_STATE_ELSE_OR_END:
            {
                if (token.id == TOKEN_KEYWORD_ELSE)
                {
                    consume(&parser.iterator);

                    ExtendedToken alternative = peek(&parser);
                    if (alternative.id == TOKEN_KEYWORD_IF)
                    {
                        consume(&parser.iterator);

                        AstStatement* nested_if = parser_statement_create(&parser, AST_STATEMENT_IF, alternative);
                        statement->if_statement.alternative = AST_IF_ALTERNATIVE_IF;
                        statement->if_statement.else_if = nested_if;
                        if_state->statement.if_state.id = IF_STATEMENT_STATE_ELSE_IF;

                        ParserState* nested_state = state_push(&parser.state);
                        nested_state->id = PARSER_STATE_IF_STATEMENT;
                        nested_state->statement.if_state.id = IF_STATEMENT_STATE_OPEN_CONDITION;
                        nested_state->statement.pointer = nested_if;
                        nested_state->statement.end_token = TOKEN_ERROR;
                    }
                    else if (alternative.id == TOKEN_LEFT_BRACE)
                    {
                        consume(&parser.iterator);

                        statement->if_statement.alternative = AST_IF_ALTERNATIVE_BLOCK;
                        if_state->statement.if_state.id = IF_STATEMENT_STATE_ELSE_BLOCK;
                        parse_block(&parser, &statement->if_statement.else_block, alternative);
                    }
                    else
                    {
                        parser_expected_else_body(&parser, alternative);
                        continue;
                    }
                }
                else
                {
                    AstBlock* then_block = &statement->if_statement.then_block;
                    statement->range.length = then_block->range.offset + then_block->range.length - statement->range.offset;
                    state_pop(&parser.state);
                }
            }
            break;
            case IF_STATEMENT_STATE_ELSE_BLOCK:
            {
                AstBlock* else_block = &statement->if_statement.else_block;
                statement->range.length = else_block->range.offset + else_block->range.length - statement->range.offset;
                state_pop(&parser.state);
            }
            break;
            case IF_STATEMENT_STATE_ELSE_IF:
            {
                AstStatement* nested_if = statement->if_statement.else_if;
                BUSTER_CHECK(nested_if && nested_if->id == AST_STATEMENT_IF);
                statement->range.length = nested_if->range.offset + nested_if->range.length - statement->range.offset;
                state_pop(&parser.state);
            }
            break;
            case IF_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_SWITCH_STATEMENT:
        {
            ParserState* switch_state = state(&parser);
            ExtendedToken token = peek(&parser);
            AstStatement* statement = switch_state->statement.pointer;
            AstSwitchStatement* switch_statement = &statement->switch_statement;

            switch (switch_state->statement.switch_state.id)
            {
                break;
            case SWITCH_STATEMENT_STATE_OPEN_EXPRESSION:
            {
                if (token.id != TOKEN_LEFT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                    continue;
                }
                consume(&parser.iterator);
                switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_EXPRESSION;
            }
            break;
            case SWITCH_STATEMENT_STATE_EXPRESSION:
            {
                parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
            }
            break;
            case SWITCH_STATEMENT_STATE_CLOSE_EXPRESSION:
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
                switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_OR_END;
            }
            break;
            case SWITCH_STATEMENT_STATE_CASE_OR_END:
            {
                if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    statement->range.length = token.offset + token.length - statement->range.offset;
                    state_pop(&parser.state);
                }
                else if (token.id == TOKEN_KEYWORD_ELSE)
                {
                    if (switch_statement->else_case)
                    {
                        parser_duplicate_switch_else(&parser, token);
                        continue;
                    }

                    consume(&parser.iterator);
                    AstSwitchCase* switch_case = parser_switch_case_push(&parser, switch_statement, token);
                    switch_case->is_else = true;
                    switch_statement->else_case = switch_case;
                    switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_ARROW;
                }
                else
                {
                    parser_switch_case_push(&parser, switch_statement, token);
                    switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_EXPRESSION;
                }
            }
            break;
            case SWITCH_STATEMENT_STATE_CASE_EXPRESSION:
            {
                parse_expression(&parser, TOKEN_FAT_ARROW);
            }
            break;
            case SWITCH_STATEMENT_STATE_CASE_ARROW:
            {
                if (token.id != TOKEN_FAT_ARROW)
                {
                    parser_unexpected(&parser, token, TOKEN_FAT_ARROW);
                    parser.recovery = PARSER_RECOVERY_SWITCH_CASE;
                    continue;
                }
                consume(&parser.iterator);

                ExtendedToken opening_brace = peek(&parser);
                if (opening_brace.id != TOKEN_LEFT_BRACE)
                {
                    parser_unexpected(&parser, opening_brace, TOKEN_LEFT_BRACE);
                    parser.recovery = PARSER_RECOVERY_SWITCH_CASE;
                    continue;
                }
                consume(&parser.iterator);
                switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_BODY;
                BUSTER_CHECK(switch_statement->last_case);
                parse_block(&parser, &switch_statement->last_case->body, opening_brace);
            }
            break;
            case SWITCH_STATEMENT_STATE_CASE_BODY:
            {
                AstSwitchCase* switch_case = switch_statement->last_case;
                BUSTER_CHECK(switch_case);
                switch_case->range.length = switch_case->body.range.offset + switch_case->body.range.length - switch_case->range.offset;
                switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_DELIMITER;
            }
            break;
            case SWITCH_STATEMENT_STATE_CASE_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    switch_state->statement.switch_state.id = SWITCH_STATEMENT_STATE_CASE_OR_END;
                }
                else if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    statement->range.length = token.offset + token.length - statement->range.offset;
                    state_pop(&parser.state);
                }
                else
                {
                    parser_expected_switch_case_delimiter(&parser, token);
                    continue;
                }
            }
            break;
            case SWITCH_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_FOR_STATEMENT:
        {
            ParserState* for_state = state(&parser);
            ExtendedToken token = peek(&parser);
            AstStatement* statement = for_state->statement.pointer;

            switch (for_state->statement.for_state.id)
            {
                break;
            case FOR_STATEMENT_STATE_OPEN:
            {
                if (token.id != TOKEN_LEFT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                    continue;
                }
                consume(&parser.iterator);
                for_state->statement.for_state.id = FOR_STATEMENT_STATE_DATA;
            }
            break;
            case FOR_STATEMENT_STATE_DATA:
            {
                if (token.id != TOKEN_KEYWORD_DATA)
                {
                    parser_unexpected(&parser, token, TOKEN_KEYWORD_DATA);
                    continue;
                }
                consume(&parser.iterator);
                for_state->statement.for_state.id = FOR_STATEMENT_STATE_NAME;
            }
            break;
            case FOR_STATEMENT_STATE_NAME:
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
            break;
            case FOR_STATEMENT_STATE_TYPE_OR_EQUAL:
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
            break;
            case FOR_STATEMENT_STATE_TYPE:
            {
                BUSTER_UNREACHABLE();
            }
            break;
            case FOR_STATEMENT_STATE_EQUAL:
            {
                if (token.id != TOKEN_EQUAL)
                {
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                    continue;
                }
                consume(&parser.iterator);
                for_state->statement.for_state.id = FOR_STATEMENT_STATE_ITERABLE;
            }
            break;
            case FOR_STATEMENT_STATE_ITERABLE:
            {
                parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
            }
            break;
            case FOR_STATEMENT_STATE_CLOSE:
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
            break;
            case FOR_STATEMENT_STATE_BODY:
            {
                AstBlock* body = &statement->for_statement.body;
                statement->range.length = body->range.offset + body->range.length - statement->range.offset;
                state_pop(&parser.state);
            }
            break;
            case FOR_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_LOOP_STATEMENT:
        {
            ParserState* loop_state = state(&parser);
            ExtendedToken token = peek(&parser);
            AstStatement* statement = loop_state->statement.pointer;

            switch (loop_state->statement.loop_state.id)
            {
                break;
            case LOOP_STATEMENT_STATE_CONDITION_OR_BODY:
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
            break;
            case LOOP_STATEMENT_STATE_CONDITION:
            {
                parse_expression(&parser, TOKEN_RIGHT_PARENTHESIS);
            }
            break;
            case LOOP_STATEMENT_STATE_CLOSE_CONDITION:
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
            break;
            case LOOP_STATEMENT_STATE_BODY:
            {
                AstBlock* body = &statement->loop_statement.body;
                statement->range.length = body->range.offset + body->range.length - statement->range.offset;
                state_pop(&parser.state);
            }
            break;
            case LOOP_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_ARRAY_LITERAL:
        {
            ParserState* array_state = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (array_state->array_literal.state)
            {
                break;
            case ARRAY_LITERAL_STATE_ELEMENT_OR_END:
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
            break;
            case ARRAY_LITERAL_STATE_DELIMITER:
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
            break;
            case ARRAY_LITERAL_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_ARRAY_SUBSCRIPT:
        {
            ParserState* subscript = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (subscript->array_subscript.state)
            {
                break;
            case ARRAY_SUBSCRIPT_STATE_START_OR_RANGE:
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
            break;
            case ARRAY_SUBSCRIPT_STATE_AFTER_START:
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
            break;
            case ARRAY_SUBSCRIPT_STATE_END_OR_CLOSE:
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
            break;
            case ARRAY_SUBSCRIPT_STATE_CLOSE:
            {
                BUSTER_CHECK(token.id == TOKEN_RIGHT_BRACKET);
                consume(&parser.iterator);
                finish_array_subscript(&parser, token, true);
            }
            break;
            case ARRAY_SUBSCRIPT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_AGGREGATE_LITERAL:
        {
            ParserState* aggregate = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (aggregate->aggregate_literal.state)
            {
                break;
            case AGGREGATE_LITERAL_STATE_FIELD_OR_END:
            {
                if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    finish_aggregate_literal(&parser, token);
                }
                else if (token.id == TOKEN_DOT)
                {
                    consume(&parser.iterator);
                    aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_FIELD_NAME;
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_DOT);
                }
            }
            break;
            case AGGREGATE_LITERAL_STATE_FIELD_NAME:
            {
                if (token.id != TOKEN_IDENTIFIER)
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    continue;
                }
                consume(&parser.iterator);

                AstAggregateLiteralField* field = arena_allocate(parser.result_arena, AstAggregateLiteralField, 1);
                *field = (AstAggregateLiteralField){
                    .name =
                        {
                            .text = get_string(parser.iterator.source, token),
                            .range = source_range_from_token(token),
                        },
                };
                if (aggregate->aggregate_literal.last_field)
                {
                    aggregate->aggregate_literal.last_field->next = field;
                }
                else
                {
                    aggregate->aggregate_literal.first_field = field;
                }
                aggregate->aggregate_literal.last_field = field;
                aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_EQUAL;
            }
            break;
            case AGGREGATE_LITERAL_STATE_EQUAL:
            {
                if (token.id != TOKEN_EQUAL)
                {
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                    continue;
                }
                consume(&parser.iterator);
                aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_VALUE;
            }
            break;
            case AGGREGATE_LITERAL_STATE_VALUE:
            {
                parse_aggregate_field_value(&parser);
            }
            break;
            case AGGREGATE_LITERAL_STATE_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    aggregate->aggregate_literal.state = AGGREGATE_LITERAL_STATE_FIELD_OR_END;
                }
                else if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    finish_aggregate_literal(&parser, token);
                }
                else
                {
                    parser_expected_aggregate_delimiter(&parser, token);
                }
            }
            break;
            case AGGREGATE_LITERAL_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_MEMBER_ACCESS:
        {
            ExtendedToken token = peek(&parser);
            if (token.id == TOKEN_AMPERSAND)
            {
                consume(&parser.iterator);
                finish_pointer_dereference(&parser, token);
            }
            else if (token.id == TOKEN_IDENTIFIER)
            {
                consume(&parser.iterator);
                finish_member_access(&parser, token);
            }
            else
            {
                parser_expected_postfix_access(&parser, token);
            }
        }
        break;
        case PARSER_STATE_ENUM_LITERAL:
        {
            ExtendedToken token = peek(&parser);
            if (token.id != TOKEN_IDENTIFIER)
            {
                parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                continue;
            }
            consume(&parser.iterator);
            finish_enum_literal(&parser, token);
        }
        break;
        case PARSER_STATE_CALL:
        {
            ParserState* call = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (call->call.state)
            {
                break;
            case CALL_STATE_ARGUMENT_OR_CLOSE:
            {
                if (token.id == TOKEN_RIGHT_PARENTHESIS)
                {
                    consume(&parser.iterator);
                    finish_call(&parser, token);
                }
                else
                {
                    parse_call_argument(&parser);
                }
            }
            break;
            case CALL_STATE_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    call->call.state = CALL_STATE_ARGUMENT_OR_CLOSE;
                }
                else if (token.id == TOKEN_RIGHT_PARENTHESIS)
                {
                    consume(&parser.iterator);
                    finish_call(&parser, token);
                }
                else
                {
                    parser_expected_call_delimiter(&parser, token);
                }
            }
            break;
            case CALL_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_INTRINSIC_CALL:
        {
            ParserState* intrinsic = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (intrinsic->intrinsic_call.state)
            {
                break;
            case INTRINSIC_CALL_STATE_NAME:
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
            break;
            case INTRINSIC_CALL_STATE_OPEN:
            {
                if (token.id != TOKEN_LEFT_PARENTHESIS)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_PARENTHESIS);
                    continue;
                }
                consume(&parser.iterator);
                intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE;
            }
            break;
            case INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE:
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
            break;
            case INTRINSIC_CALL_STATE_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    if (string_equal(intrinsic->intrinsic_call.name.text, S8("va_arg")) && intrinsic->intrinsic_call.argument_count == 1 &&
                        !intrinsic->intrinsic_call.type_argument_destination)
                    {
                        AstType** destination = arena_allocate(parser.result_arena, AstType*, 1);
                        *destination = 0;
                        intrinsic->intrinsic_call.type_argument_destination = destination;
                        ParserState* type_state = state_push(&parser.state);
                        type_state->id = PARSER_STATE_TYPE_REFERENCE;
                        type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                        type_state->type.destination = destination;
                    }
                    else
                    {
                        intrinsic->intrinsic_call.state = INTRINSIC_CALL_STATE_ARGUMENT_OR_CLOSE;
                    }
                }
                else if (token.id == TOKEN_RIGHT_PARENTHESIS)
                {
                    consume(&parser.iterator);
                    finish_intrinsic_call(&parser, token);
                }
                else
                {
                    parser_expected_call_delimiter(&parser, token);
                }
            }
            break;
            case INTRINSIC_CALL_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_EXPRESSION:
        {
            ParserState* st = state(&parser);
            ExtendedToken token = peek(&parser);

            switch (st->expression.state)
            {
                break;
            case EXPRESSION_STATE_PREFIX:
            {
                expression_parse_prefix(&parser);
            }
            break;
            case EXPRESSION_STATE_TAIL:
            {
                bool at_end = token.id == st->expression.end_token || (st->expression.ends_at_assignment && token_is_assignment_operator(token.id)) ||
                              (st->expression.ends_at_array_delimiter && (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_BRACKET)) ||
                              (st->expression.ends_at_aggregate_delimiter && (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_BRACE)) ||
                              (st->expression.ends_at_enum_delimiter && (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_BRACE)) ||
                              (st->expression.ends_at_slice_operator && token.id == TOKEN_DOUBLE_DOT) ||
                              (st->expression.argument_kind != EXPRESSION_ARGUMENT_NONE && (token.id == TOKEN_COMMA || token.id == TOKEN_RIGHT_PARENTHESIS));
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
                    case TOKEN_KEYWORD_AND:
                    case TOKEN_KEYWORD_OR:
                    case TOKEN_KEYWORD_AND_SHORT_CIRCUIT:
                    case TOKEN_KEYWORD_OR_SHORT_CIRCUIT:
                    case TOKEN_DOUBLE_DOT:
                    {
                        if (token.id == TOKEN_DOUBLE_DOT && st->expression.is_array_subscript_bound && st->expression.is_array_subscript_end)
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
                            break;
                        case TOKEN_PLUS:
                            binary_node_id = AST_NODE_BINARY_PLUS;
                            break;
                        case TOKEN_MINUS:
                            binary_node_id = AST_NODE_BINARY_MINUS;
                            break;
                        case TOKEN_ASTERISK:
                            binary_node_id = AST_NODE_BINARY_ASTERISK;
                            break;
                        case TOKEN_SLASH:
                            binary_node_id = AST_NODE_BINARY_SLASH;
                            break;
                        case TOKEN_PERCENTAGE:
                            binary_node_id = AST_NODE_BINARY_PERCENT;
                            break;
                        case TOKEN_SHIFT_LEFT:
                            binary_node_id = AST_NODE_BINARY_SHIFT_LEFT;
                            break;
                        case TOKEN_SHIFT_RIGHT:
                            binary_node_id = AST_NODE_BINARY_SHIFT_RIGHT;
                            break;
                        case TOKEN_EQUAL_EQUAL:
                            binary_node_id = AST_NODE_BINARY_EQUAL;
                            break;
                        case TOKEN_BANG_EQUAL:
                            binary_node_id = AST_NODE_BINARY_NOT_EQUAL;
                            break;
                        case TOKEN_LESS:
                            binary_node_id = AST_NODE_BINARY_LESS;
                            break;
                        case TOKEN_LESS_EQUAL:
                            binary_node_id = AST_NODE_BINARY_LESS_EQUAL;
                            break;
                        case TOKEN_GREATER:
                            binary_node_id = AST_NODE_BINARY_GREATER;
                            break;
                        case TOKEN_GREATER_EQUAL:
                            binary_node_id = AST_NODE_BINARY_GREATER_EQUAL;
                            break;
                        case TOKEN_AMPERSAND:
                            binary_node_id = AST_NODE_BINARY_AMPERSAND;
                            break;
                        case TOKEN_BAR:
                            binary_node_id = AST_NODE_BINARY_BAR;
                            break;
                        case TOKEN_CARET:
                            binary_node_id = AST_NODE_BINARY_CARET;
                            break;
                        case TOKEN_KEYWORD_AND:
                            binary_node_id = AST_NODE_BINARY_BOOLEAN_AND;
                            break;
                        case TOKEN_KEYWORD_OR:
                            binary_node_id = AST_NODE_BINARY_BOOLEAN_OR;
                            break;
                        case TOKEN_KEYWORD_AND_SHORT_CIRCUIT:
                            binary_node_id = AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT;
                            break;
                        case TOKEN_KEYWORD_OR_SHORT_CIRCUIT:
                            binary_node_id = AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT;
                            break;
                        case TOKEN_DOUBLE_DOT:
                            binary_node_id = AST_NODE_BINARY_RANGE;
                            break;
                        default:
                            BUSTER_UNREACHABLE();
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
                    break;
                    case TOKEN_LEFT_BRACKET:
                    {
                        consume(&parser.iterator);
                        parse_array_subscript(&parser, token);
                    }
                    break;
                    case TOKEN_LEFT_PARENTHESIS:
                    {
                        consume(&parser.iterator);
                        parse_call(&parser, token);
                    }
                    break;
                    case TOKEN_DOT:
                    {
                        consume(&parser.iterator);
                        parse_member_access(&parser, token);
                    }
                    break;
                    default:
                    {
                        if (st->expression.ends_at_assignment)
                        {
                            parser_expected_assignment_operator(&parser, token);
                        }
                        else if (st->expression.ends_at_array_delimiter)
                        {
                            parser_expected_array_delimiter(&parser, token);
                        }
                        else if (st->expression.ends_at_aggregate_delimiter)
                        {
                            parser_expected_aggregate_delimiter(&parser, token);
                        }
                        else if (st->expression.ends_at_enum_delimiter)
                        {
                            parser_expected_enum_delimiter(&parser, token, true);
                        }
                        else if (st->expression.argument_kind != EXPRESSION_ARGUMENT_NONE)
                        {
                            parser_expected_call_delimiter(&parser, token);
                        }
                        else
                        {
                            parser_unexpected(&parser, token, st->expression.end_token);
                        }
                    }
                    }
                }
            }
            break;
            case EXPRESSION_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        break;
        case PARSER_STATE_UNARY_PREFIX:
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
                else if (token.id == TOKEN_LEFT_PARENTHESIS)
                {
                    consume(&parser.iterator);
                    parse_call(&parser, token);
                }
                else if (token.id == TOKEN_DOT)
                {
                    consume(&parser.iterator);
                    parse_member_access(&parser, token);
                }
                else
                {
                    expression_finish_prefix_unaries(&parser, owner);
                }
            }
        }
        break;
        case PARSER_STATE_TYPE_STATEMENT:
        {
            ParserState* type_statement = state(&parser);
            ExtendedToken token = peek(&parser);
            AstTypeDeclaration* declaration = type_statement->type_statement.declaration;

            switch (type_statement->type_statement.state)
            {
                break;
            case TYPE_STATEMENT_STATE_NAME:
            {
                if (token.id != TOKEN_IDENTIFIER)
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    continue;
                }
                consume(&parser.iterator);
                declaration->name = (AstIdentifier){
                    .text = get_string(parser.iterator.source, token),
                    .range = source_range_from_token(token),
                };
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_EQUAL;
            }
            break;
            case TYPE_STATEMENT_STATE_EQUAL:
            {
                if (token.id != TOKEN_EQUAL)
                {
                    parser_unexpected(&parser, token, TOKEN_EQUAL);
                    continue;
                }
                consume(&parser.iterator);
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_KIND;
            }
            break;
            case TYPE_STATEMENT_STATE_KIND:
            {
                if (token.id == TOKEN_KEYWORD_STRUCT)
                {
                    declaration->kind = AST_TYPE_DECLARATION_STRUCT;
                }
                else if (token.id == TOKEN_KEYWORD_UNION)
                {
                    declaration->kind = AST_TYPE_DECLARATION_UNION;
                }
                else if (token.id == TOKEN_KEYWORD_ENUM)
                {
                    declaration->kind = AST_TYPE_DECLARATION_ENUM;
                }
                else if (token_begins_type(token.id))
                {
                    declaration->kind = AST_TYPE_DECLARATION_ALIAS;
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_ALIAS_TYPE;

                    ParserState* alias_type = state_push(&parser.state);
                    alias_type->id = PARSER_STATE_TYPE_REFERENCE;
                    alias_type->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    alias_type->type.destination = &declaration->alias_type;
                    break;
                }
                else
                {
                    parser_expected_type_declaration_kind(&parser, token);
                    continue;
                }
                consume(&parser.iterator);
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_OPEN;
            }
            break;
            case TYPE_STATEMENT_STATE_ALIAS_TYPE:
            {
                BUSTER_UNREACHABLE();
            }
            break;
            case TYPE_STATEMENT_STATE_OPEN:
            {
                if (token.id != TOKEN_LEFT_BRACE)
                {
                    parser_unexpected(&parser, token, TOKEN_LEFT_BRACE);
                    continue;
                }
                consume(&parser.iterator);
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
            }
            break;
            case TYPE_STATEMENT_STATE_FIELD_OR_CLOSE:
            {
                if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&declaration->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else if (token.id == TOKEN_IDENTIFIER)
                {
                    consume(&parser.iterator);
                    if (declaration->kind == AST_TYPE_DECLARATION_ENUM)
                    {
                        type_statement->type_statement.enum_member = parser_enum_member_push(&parser, declaration, token);
                        type_statement->type_statement.state = TYPE_STATEMENT_STATE_ENUM_MEMBER_EQUAL_OR_DELIMITER;
                    }
                    else
                    {
                        type_statement->type_statement.field = parser_type_field_push(&parser, declaration, token);
                        type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_COLON;
                    }
                }
                else
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                }
            }
            break;
            case TYPE_STATEMENT_STATE_FIELD_COLON:
            {
                if (token.id != TOKEN_COLON)
                {
                    parser_unexpected(&parser, token, TOKEN_COLON);
                    continue;
                }
                consume(&parser.iterator);
                type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_TYPE;
            }
            break;
            case TYPE_STATEMENT_STATE_FIELD_TYPE:
            {
                if (!token_begins_type(token.id))
                {
                    parser_unexpected(&parser, token, TOKEN_IDENTIFIER);
                    continue;
                }

                ParserState* child_type = state_push(&parser.state);
                child_type->id = PARSER_STATE_TYPE_REFERENCE;
                child_type->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                child_type->type.destination = &type_statement->type_statement.field->type;
            }
            break;
            case TYPE_STATEMENT_STATE_FIELD_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
                }
                else if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&declaration->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else
                {
                    parser_expected_type_field_delimiter(&parser, token);
                }
            }
            break;
            case TYPE_STATEMENT_STATE_ENUM_MEMBER_EQUAL_OR_DELIMITER:
            {
                if (token.id == TOKEN_EQUAL)
                {
                    consume(&parser.iterator);
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_ENUM_MEMBER_VALUE;
                }
                else if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
                }
                else if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&declaration->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else
                {
                    parser_expected_enum_delimiter(&parser, token, false);
                }
            }
            break;
            case TYPE_STATEMENT_STATE_ENUM_MEMBER_VALUE:
            {
                parse_enum_member_value(&parser);
            }
            break;
            case TYPE_STATEMENT_STATE_ENUM_MEMBER_DELIMITER:
            {
                if (token.id == TOKEN_COMMA)
                {
                    consume(&parser.iterator);
                    type_statement->type_statement.state = TYPE_STATEMENT_STATE_FIELD_OR_CLOSE;
                }
                else if (token.id == TOKEN_RIGHT_BRACE)
                {
                    consume(&parser.iterator);
                    parser_source_range_set_end(&declaration->range, token.offset + token.length);
                    state_pop(&parser.state);
                }
                else
                {
                    parser_expected_enum_delimiter(&parser, token, true);
                }
            }
            break;
            case TYPE_STATEMENT_STATE_COUNT:
                BUSTER_UNREACHABLE();
            }
        }
        }
    }

    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic missing_array_delimiter_diagnostics[] = {
    {
        .message = S8_INITIALIZER("expected ',' or ']' after array element"),
        .kind = PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER,
        .found = TOKEN_IDENTIFIER,
        .expected = TOKEN_ERROR,
        .line = 3,
        .column = 22,
        .length = 7,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic unexpected_top_level_diagnostics[] = {
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_AT,
        .expected = TOKEN_KEYWORD_CODE,
        .line = 1,
        .column = 1,
        .length = 1,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic malformed_import_diagnostics[] = {
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_EQUAL,
        .expected = TOKEN_IDENTIFIER,
        .line = 1,
        .column = 8,
        .length = 1,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic missing_call_delimiter_diagnostics[] = {
    {
        .message = S8_INITIALIZER("expected ',' or ')' after call argument"),
        .kind = PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER,
        .found = TOKEN_IDENTIFIER,
        .expected = TOKEN_ERROR,
        .line = 3,
        .column = 15,
        .length = 7,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic malformed_type_items_diagnostics[] = {
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_AT,
        .expected = TOKEN_IDENTIFIER,
        .line = 3,
        .column = 5,
        .length = 1,
    },
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_COMMA,
        .expected = TOKEN_IDENTIFIER,
        .line = 5,
        .column = 10,
        .length = 1,
    },
    {
        .message = S8_INITIALIZER("expected expression"),
        .kind = PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION,
        .found = TOKEN_COMMA,
        .expected = TOKEN_ERROR,
        .line = 12,
        .column = 11,
        .length = 1,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic missing_alias_type_diagnostics[] = {
    {
        .message = S8_INITIALIZER("expected type, 'struct', 'union', or 'enum' after '='"),
        .kind = PARSER_DIAGNOSTIC_EXPECTED_TYPE_DECLARATION_KIND,
        .found = TOKEN_KEYWORD_CODE,
        .expected = TOKEN_ERROR,
        .line = 2,
        .column = 1,
        .length = 4,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic malformed_function_alias_diagnostics[] = {
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_AMPERSAND,
        .expected = TOKEN_COLON,
        .line = 1,
        .column = 29,
        .length = 1,
    },
};

BUSTER_GLOBAL_LOCAL ParserFileExpectedDiagnostic missing_compile_time_marker_diagnostics[] = {
    {
        .message = S8_INITIALIZER("unexpected token"),
        .kind = PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
        .found = TOKEN_IDENTIFIER,
        .expected = TOKEN_DOLLAR,
        .line = 1,
        .column = 6,
        .length = 5,
    },
};

BUSTER_TEST_F_DECL ParserFileTestCase parser_file_test_cases[] = {
    {
        .path = S8_INITIALIZER("tests/basic_vector.bbb"),
        .expected_expression = S8_INITIALIZER("(@cast (index negated 0))"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 2,
        .expected_type_declaration_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_vector_error.bbb"),
        .expected_code_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_code_non_function_type_error.bbb"),
        .expected_code_count = 2,
        .expected_type_declaration_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_return_without_value_error.bbb"),
        .expected_code_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_variadic.bbb"),
        .expected_expression = S8_INITIALIZER(
            "(- (+ (+ (+ (call first_two 1 small 100) (@cast (call take_float floating))) (@cast (call take_pair pair))) (@cast (call take_large large))) 81)"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 5,
        .expected_type_declaration_count = 2,
    },
    {
        .path = S8_INITIALIZER("tests/basic_variadic_error.bbb"),
    },
    {.path = S8_INITIALIZER("tests/basic_minimal.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {
        .path = S8_INITIALIZER("tests/basic_import.bbb"),
        .expected_expression = S8_INITIALIZER("0"),
        .expected_import_count = 2,
    },
    {.path = S8_INITIALIZER("tests/basic_comment.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {
        .path = S8_INITIALIZER("tests/basic_compile_time.bbb"),
        .expected_expression = S8_INITIALIZER("(+ (+ (+ first reused) distinct_value) (@cast (+ distinct_type local)))"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 2,
    },
    {
        .path = S8_INITIALIZER("tests/compile_time_argument_error.bbb"),
        .expected_expression = S8_INITIALIZER("(call choose runtime_value)"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 2,
    },
    {.path = S8_INITIALIZER("tests/modules/core/math.bbb"), .expected_expression = S8_INITIALIZER("(+ (+ a b) bias)")},
    {.path = S8_INITIALIZER("tests/modules/system/platform.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {.path = S8_INITIALIZER("tests/basic_hexadecimal_literal.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {.path = S8_INITIALIZER("tests/basic_octal_literal.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {.path = S8_INITIALIZER("tests/basic_binary_literal.bbb"), .expected_expression = S8_INITIALIZER("0")},
    {.path = S8_INITIALIZER("tests/basic_float.bbb"), .expected_expression = S8_INITIALIZER("(@cast f)")},
    {.path = S8_INITIALIZER("tests/basic_character_literal.bbb"), .expected_expression = S8_INITIALIZER("(@cast character)")},
    {.path = S8_INITIALIZER("tests/basic_string_literal.bbb"), .expected_expression = S8_INITIALIZER("(@cast (- (index greeting 0) 'h'))")},
    {.path = S8_INITIALIZER("tests/basic_unary_minus.bbb"), .expected_expression = S8_INITIALIZER("(neg 0)")},
    {.path = S8_INITIALIZER("tests/basic_unary_plus.bbb"), .expected_expression = S8_INITIALIZER("(pos 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_add.bbb"), .expected_expression = S8_INITIALIZER("(+ 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_sub.bbb"), .expected_expression = S8_INITIALIZER("(- 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_multiply.bbb"), .expected_expression = S8_INITIALIZER("(* 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_divide.bbb"), .expected_expression = S8_INITIALIZER("(/ 0 1)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_mod.bbb"), .expected_expression = S8_INITIALIZER("(% 0 1)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_shift_left.bbb"), .expected_expression = S8_INITIALIZER("(<< 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_shift_right.bbb"), .expected_expression = S8_INITIALIZER("(>> 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_and.bbb"), .expected_expression = S8_INITIALIZER("(& 0 1)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_or.bbb"), .expected_expression = S8_INITIALIZER("(| 0 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_xor.bbb"), .expected_expression = S8_INITIALIZER("(^ 1 1)")},
    {.path = S8_INITIALIZER("tests/basic_boolean_operators.bbb"),
     .expected_expression = S8_INITIALIZER("(or? (or (and (== 0 0) (< 1 2)) (and? (== 2 2) (!= 3 4))) (<= 4 5))")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_compare.bbb"), .expected_expression = S8_INITIALIZER("(!= (== (< 1 2) 3) (> (>= (<= 4 5) 6) 7))")},
    {.path = S8_INITIALIZER("tests/basic_logical_not.bbb"), .expected_expression = S8_INITIALIZER("(not (not 0))")},
    {.path = S8_INITIALIZER("tests/basic_bitwise_not.bbb"), .expected_expression = S8_INITIALIZER("(bit_not 0)")},
    {.path = S8_INITIALIZER("tests/basic_integer_literal_precedence.bbb"), .expected_expression = S8_INITIALIZER("(<< (+ 1 (* 2 3)) (- 4 5))")},
    {.path = S8_INITIALIZER("tests/basic_variable.bbb"), .expected_expression = S8_INITIALIZER("result")},
    {.path = S8_INITIALIZER("tests/basic_array_literal.bbb"), .expected_expression = S8_INITIALIZER("(index result 0)")},
    {.path = S8_INITIALIZER("tests/basic_assignment.bbb"), .expected_expression = S8_INITIALIZER("result")},
    {.path = S8_INITIALIZER("tests/basic_pointer.bbb"), .expected_expression = S8_INITIALIZER("p.&")},
    {.path = S8_INITIALIZER("tests/basic_if_else.bbb"), .expected_expression = S8_INITIALIZER("(+ a b)")},
    {.path = S8_INITIALIZER("tests/basic_else_if.bbb"), .expected_expression = S8_INITIALIZER("value")},
    {
        .path = S8_INITIALIZER("tests/basic_switch.bbb"),
        .expected_expression = S8_INITIALIZER("e"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 1,
        .expected_type_declaration_count = 1,
    },
    {.path = S8_INITIALIZER("tests/basic_for.bbb"), .expected_expression = S8_INITIALIZER("total")},
    {.path = S8_INITIALIZER("tests/basic_continue.bbb"), .expected_expression = S8_INITIALIZER("total")},
    {.path = S8_INITIALIZER("tests/basic_loop.bbb"), .expected_expression = S8_INITIALIZER("value")},
    {.path = S8_INITIALIZER("tests/basic_break.bbb"), .expected_expression = S8_INITIALIZER("value")},
    {
        .path = S8_INITIALIZER("tests/basic_function_call.bbb"),
        .expected_expression = S8_INITIALIZER("(call final_result value)"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 4,
    },
    {
        .path = S8_INITIALIZER("tests/basic_struct.bbb"),
        .expected_expression = S8_INITIALIZER("(- s.a s.b)"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 1,
        .expected_type_declaration_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_union.bbb"),
        .expected_expression = S8_INITIALIZER("number.signed_value"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 1,
        .expected_type_declaration_count = 1,
    },
    {
        .path = S8_INITIALIZER("tests/basic_enum.bbb"),
        .expected_expression = S8_INITIALIZER("result"),
        .expression_code_name = S8_INITIALIZER("main"),
        .expected_code_count = 1,
        .expected_type_declaration_count = 2,
    },
    {
        .path = S8_INITIALIZER("tests/basic_type_alias.bbb"),
        .expected_expression = S8_INITIALIZER("0"),
        .expected_code_count = 1,
        .expected_type_declaration_count = 5,
    },
    {.path = S8_INITIALIZER("tests/array_slices.bbb"), .expected_expression = S8_INITIALIZER("(- total_a total_b)")},
    {
        .path = S8_INITIALIZER("tests/errors/missing_array_delimiter.bbb"),
        .expected_expression = S8_INITIALIZER("11"),
        .expected_diagnostics = missing_array_delimiter_diagnostics,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(missing_array_delimiter_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/malformed_import.bbb"),
        .expected_expression = S8_INITIALIZER("31"),
        .expected_diagnostics = malformed_import_diagnostics,
        .expected_import_count = 1,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(malformed_import_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/missing_call_delimiter.bbb"),
        .expected_expression = S8_INITIALIZER("12"),
        .expected_diagnostics = missing_call_delimiter_diagnostics,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(missing_call_delimiter_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/malformed_type_items.bbb"),
        .expected_expression = S8_INITIALIZER("13"),
        .expected_diagnostics = malformed_type_items_diagnostics,
        .expected_code_count = 1,
        .expected_type_declaration_count = 2,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(malformed_type_items_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/unexpected_top_level.bbb"),
        .expected_diagnostics = unexpected_top_level_diagnostics,
        .expected_code_count = 0,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(unexpected_top_level_diagnostics),
        .expected_code_count_is_set = true,
    },
    {
        .path = S8_INITIALIZER("tests/errors/missing_alias_type.bbb"),
        .expected_expression = S8_INITIALIZER("21"),
        .expected_diagnostics = missing_alias_type_diagnostics,
        .expected_code_count = 1,
        .expected_type_declaration_count = 1,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(missing_alias_type_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/malformed_function_alias.bbb"),
        .expected_expression = S8_INITIALIZER("22"),
        .expected_diagnostics = malformed_function_alias_diagnostics,
        .expected_code_count = 1,
        .expected_type_declaration_count = 1,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(malformed_function_alias_diagnostics),
    },
    {
        .path = S8_INITIALIZER("tests/errors/missing_compile_time_marker.bbb"),
        .expected_expression = S8_INITIALIZER("0"),
        .expected_diagnostics = missing_compile_time_marker_diagnostics,
        .expected_code_count = 1,
        .expected_diagnostic_count = BUSTER_ARRAY_LENGTH(missing_compile_time_marker_diagnostics),
    },
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(parser_file_test_cases) == PARSER_FILE_TEST_CASE_COUNT);

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

typedef struct ParserBenchMappedFile ParserBenchMappedFile;
struct ParserBenchMappedFile
{
    String8 source;
    FileMapRead mapped_file;
};

BUSTER_GLOBAL_LOCAL void parser_bench_populate_file(Arena* arena, ParserBenchMappedFile* mapped_file, String8 path)
{
    *mapped_file = (ParserBenchMappedFile){0};
    mapped_file->mapped_file = file_map_read(arena, path, (FileReadOptions){0});
    mapped_file->source = BYTE_SLICE_TO_STRING(8, mapped_file->mapped_file.bytes);
}

BUSTER_GLOBAL_LOCAL void parser_bench_unmap_files(ParserBenchMappedFile* mapped_files, u64 file_count)
{
    for (u64 i = 0; i < file_count; i += 1)
    {
        ParserBenchMappedFile file = mapped_files[i];
        file_map_unmap(file.mapped_file);
    }
}

ParserBenchResult parser_parse_bench(Arena* arena, u64 iterations)
{
    ParserBenchResult result = {0};
    result.iterations = iterations;
    result.file_count = BUSTER_ARRAY_LENGTH(parser_file_test_cases);

    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

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
            FileMapRead source_file = file_map_read(scratch.arena, test_case.path, (FileReadOptions){0});
            String8 source = BYTE_SLICE_TO_STRING(8, source_file.bytes);

            if (source.pointer && source.length)
            {
#if BUSTER_INSTRUMENT
                TimeDataType file_start = timestamp_take();
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                TimeDataType tokenize_end = timestamp_take();
                parser_parse(scratch.arena, expression_arena, source, tokenizer);
                TimeDataType parse_end = timestamp_take();

                tokenize_ns_sum += timestamp_ns_between(file_start, tokenize_end);
                parse_ns_sum += timestamp_ns_between(tokenize_end, parse_end);
                file_durations_ns[iteration * result.file_count + i] = timestamp_ns_between(file_start, parse_end);
#else
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                parser_parse(scratch.arena, expression_arena, source, tokenizer);
#endif
            }
            file_map_unmap(source_file);
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

    bool expression_arena_destroyed = arena_destroy(expression_arena, 1);
    BUSTER_CHECK(expression_arena_destroyed);

    return result;
}

ParserBenchResult parser_parse_bench_mmap(Arena* arena, u64 iterations)
{
    ParserBenchResult result = {0};
    result.iterations = iterations;
    result.file_count = BUSTER_ARRAY_LENGTH(parser_file_test_cases);

    Arena* expression_arena = arena_create((ArenaCreation){0});
    BUSTER_CHECK(expression_arena);

    ParserBenchMappedFile* mapped_files = arena_allocate(arena, ParserBenchMappedFile, result.file_count);

    for (u64 i = 0; i < result.file_count; i += 1)
    {
        mapped_files[i] = (ParserBenchMappedFile){0};
        parser_bench_populate_file(arena, &mapped_files[i], parser_file_test_cases[i].path);
    }

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
            String8 source = mapped_files[i].source;

            if (source.pointer && source.length)
            {
#if BUSTER_INSTRUMENT
                TimeDataType file_start = timestamp_take();
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                TimeDataType tokenize_end = timestamp_take();
                parser_parse(scratch.arena, expression_arena, source, tokenizer);
                TimeDataType parse_end = timestamp_take();

                tokenize_ns_sum += timestamp_ns_between(file_start, tokenize_end);
                parse_ns_sum += timestamp_ns_between(tokenize_end, parse_end);
                file_durations_ns[iteration * result.file_count + i] = timestamp_ns_between(file_start, parse_end);
#else
                TokenizerResult tokenizer = tokenize(scratch.arena, source.pointer, source.length);
                parser_parse(scratch.arena, expression_arena, source, tokenizer);
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

    parser_bench_unmap_files(mapped_files, result.file_count);
    return result;
}
