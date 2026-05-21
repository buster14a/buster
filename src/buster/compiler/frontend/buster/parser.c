#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/compiler/ir/ir.h>

#define first_keyword TOKEN_KEYWORD_RETURN
#define last_keyword TOKEN_KEYWORD_UNION

#define pointer_token TOKEN_AMPERSAND
#define array_slice_token_start (TOKEN_LEFT_BRACKET)
#define array_slice_token_end (array_slice_token_start + 1)
BUSTER_GLOBAL_LOCAL TokenId block_end_of_statement_token = TOKEN_SEMICOLON;

#define KEYWORD_COUNT ((u64)last_keyword - (u64)first_keyword + 1)

#define SWITCH_ALPHA_UPPER \
                case 'A':\
                case 'B':\
                case 'C':\
                case 'D':\
                case 'E':\
                case 'F':\
                case 'G':\
                case 'H':\
                case 'I':\
                case 'J':\
                case 'K':\
                case 'L':\
                case 'M':\
                case 'N':\
                case 'O':\
                case 'P':\
                case 'Q':\
                case 'R':\
                case 'S':\
                case 'T':\
                case 'U':\
                case 'V':\
                case 'X':\
                case 'Y':\
                case 'Z'

#define SWITCH_ALPHA_LOWER \
                case 'a':\
                case 'b':\
                case 'c':\
                case 'd':\
                case 'e':\
                case 'f':\
                case 'g':\
                case 'h':\
                case 'i':\
                case 'j':\
                case 'k':\
                case 'l':\
                case 'm':\
                case 'n':\
                case 'o':\
                case 'p':\
                case 'q':\
                case 'r':\
                case 's':\
                case 't':\
                case 'u':\
                case 'v':\
                case 'x':\
                case 'y':\
                case 'z'

#define DECIMAL_DIGIT \
    case '0':\
    case '1':\
    case '2':\
    case '3':\
    case '4':\
    case '5':\
    case '6':\
    case '7':\
    case '8':\
    case '9'

#define HEX_ALPHA_LOWER \
                case 'a':\
                case 'b':\
                case 'c':\
                case 'd':\
                case 'e':\
                case 'f'

#define HEX_ALPHA_UPPER \
                case 'A':\
                case 'B':\
                case 'C':\
                case 'D':\
                case 'E':\
                case 'F'

#define HEX_ALPHA HEX_ALPHA_UPPER: HEX_ALPHA_LOWER

// STRUCT(LexInteger)
// {
//     IntegerParsingU64 parsing;
//     const char8* restrict it;
//     Format format;
// };
// BUSTER_GLOBAL_LOCAL LexInteger lex_integer(const char8* restrict it)
// {
// }

BUSTER_GLOBAL_LOCAL bool is_valid_character_after_digit(char8 ch)
{
    switch (ch)
    {
        break;
        case ' ':
        case ';':
        {
            return true;
        }
        break; default: BUSTER_TRAP();
    }

    return false;
}

TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
{
    TokenizerResult result = {0};

    if (file_length)
    {
        Token* restrict token_start = arena_allocate(arena, Token, file_length + 1);
        Token* restrict tokens = token_start;
        u64 token_count = 0;

        const char8* restrict it = file_pointer;
        const char8* top = file_pointer + file_length;

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            const char8* restrict start = it;
            char8 start_ch = *start;

            {
                TokenId id;

                switch (start_ch)
                {
                    break; SWITCH_ALPHA_UPPER: SWITCH_ALPHA_LOWER:
                    case '_':
                    {
                        while (true)
                        {
                            const char8* restrict it_start = it;
                            switch (*it_start)
                            {
                                break; SWITCH_ALPHA_UPPER: SWITCH_ALPHA_LOWER: DECIMAL_DIGIT:
                                case '_':
                                {
                                    it += 1;
                                }
                                break; default: break;
                            }

                            if (it - it_start == 0)
                            {
                                break;
                            }
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

                        while (*it == ' ')
                        {
                            it += 1;
                        }
                    }
                    break; DECIMAL_DIGIT:
                    {
                        bool is_valid = true;

                        IntegerFormat format = INTEGER_FORMAT_DECIMAL;

                        if (start_ch == '0')
                        {
                            char8 second_ch = *(it + 1);
                            switch (second_ch)
                            {
                                break; case 'x': it += 2; format = INTEGER_FORMAT_HEXADECIMAL;
                                break; case 'o': it += 2; format = INTEGER_FORMAT_OCTAL;
                                break; case 'b': it += 2; format = INTEGER_FORMAT_BINARY;
                                break; default: {}
                            }
                        }

                        bool increment;

                        do
                        {
                            const char8* restrict it_start = it;

                            char8 ch = *it_start;

                            switch (format)
                            {
                                break; case INTEGER_FORMAT_HEXADECIMAL:
                                {
                                    switch (ch)
                                    {
                                        break; 
                                        DECIMAL_DIGIT:
                                        HEX_ALPHA:
                                        case '_':
                                        {
                                            increment = true;
                                        }
                                        break; default: increment = false;
                                    }
                                }
                                break; case INTEGER_FORMAT_DECIMAL:
                                {
                                    switch (ch)
                                    {
                                        break; 
                                        DECIMAL_DIGIT:
                                        case '_':
                                        {
                                            increment = true;
                                        }
                                        break; default: increment = false;
                                    }
                                }
                                break; case INTEGER_FORMAT_OCTAL:
                                {
                                    switch (ch)
                                    {
                                        break; 
                                        case '0':
                                        case '1':
                                        case '2':
                                        case '3':
                                        case '4':
                                        case '5':
                                        case '6':
                                        case '7':
                                        case '_':
                                        {
                                            increment = true;
                                        }
                                        break; default: increment = false;
                                    }
                                }
                                break; case INTEGER_FORMAT_BINARY:
                                {
                                    switch (ch)
                                    {
                                        break; 
                                        case '0':
                                        case '1':
                                        case '_':
                                        {
                                            increment = true;
                                        }
                                        break; default: increment = false;
                                    }
                                }
                                break; case INTEGER_FORMAT_COUNT: BUSTER_UNREACHABLE();
                            }

                            it += increment;
                        } while (increment);

                        char8 maybe_float_separator = *it;
                        bool is_float = maybe_float_separator == '.';
                        is_valid = is_valid && is_valid_character_after_digit(maybe_float_separator);

                        if (is_float)
                        {
                            is_valid = is_valid && (format == INTEGER_FORMAT_DECIMAL || format == INTEGER_FORMAT_HEXADECIMAL);

                            it += 1;

                            do
                            {
                                char8 ch = *it;

                                switch (format)
                                {
                                    break; case INTEGER_FORMAT_HEXADECIMAL:
                                    {
                                        switch (ch)
                                        {
                                            break; DECIMAL_DIGIT: HEX_ALPHA: case '_': increment = true;
                                            break; default: increment = false;
                                        }
                                    }
                                    break; case INTEGER_FORMAT_DECIMAL:
                                    {
                                        switch (ch)
                                        {
                                            break; DECIMAL_DIGIT: HEX_ALPHA: case '_': increment = true;
                                            break; default: increment = false;
                                        }
                                    }
                                    break; default: BUSTER_UNREACHABLE();
                                }

                                it += increment;
                            } while (increment);

                            char8 exponent_ch = *it;

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

                                    char8 exponent_sign = *it; // '+' or '-'
                                    is_valid = is_valid && (exponent_sign == '+' || exponent_sign == '-');
                                    it += 1;

                                    do
                                    {
                                        const char8* restrict it_start = it;

                                        switch (*it_start)
                                        {
                                            break; DECIMAL_DIGIT: increment = true;
                                            break; default: increment = false;
                                        }

                                        it += increment;
                                    }
                                    while (increment);
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

                        is_valid = is_valid && is_valid_character_after_digit(*it);

                        if (!is_valid)
                        {
                            BUSTER_TRAP();
                        }
                    }
                    break; case '\n': { id = TOKEN_LINE_FEED; it += 1; }
                    break; case '\r': { id = TOKEN_CARRIAGE_RETURN; it += 1; }
                    break; case '[': { id = TOKEN_LEFT_BRACKET; it += 1; }
                    break; case ']': { id = TOKEN_RIGHT_BRACKET; it += 1; }
                    break; case '{': { id = TOKEN_LEFT_BRACE; it += 1; }
                    break; case '}': { id = TOKEN_RIGHT_BRACE; it += 1; }
                    break; case '(': { id = TOKEN_LEFT_PARENTHESIS; it += 1; }
                    break; case ')': { id = TOKEN_RIGHT_PARENTHESIS; it += 1; }
                    break; case '<': { id = TOKEN_LESS; it += 1; }
                    break; case '>': { id = TOKEN_GREATER; it += 1; }
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
                    break; case '/': { id = TOKEN_SLASH; it += 1; }
                    break; case '=': { id = TOKEN_EQUAL; it += 1; }
                    break; case ':': { id = TOKEN_COLON; it += 1; }
                    break; case ';': { id = TOKEN_SEMICOLON; it += 1; }
                    break; case ',': { id = TOKEN_COMMA; it += 1; }
                    break; case '&': { id = TOKEN_AMPERSAND; it += 1; }
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
                    break; default: BUSTER_UNREACHABLE();
                }

                const char8* restrict end = it;
                u64 length = (u64)(end - start);
                if (length > (((u64)1 << 24) - 1))
                {
                    BUSTER_TRAP(); // TODO: error
                }

                u64 token_index = token_count;

                tokens[token_index] = (Token){ .id = id };
                token_length_set(&tokens[token_index], (u32)length);

                token_count = token_index + 1;
            }
        }

        tokens[token_count++] = (Token){ .id = TOKEN_EOF };
        token_length_set(&tokens[token_count - 1], 0);

        result.tokens = token_start;

        if (result.token_count > UINT32_MAX)
        {
            BUSTER_TRAP();
        }

        result.token_count = (u32)token_count;
    }

    return result;
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

typedef struct AstNode AstNode;

typedef struct CodeAttributes CodeAttributes;
struct CodeAttributes
{
    u8 foo;
};

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

typedef enum StatementId
{
      STATEMENT_RETURN,
      STATEMENT_COUNT,
} StatementId;

typedef enum ExpressionState
{
    EXPRESSION_STATE_PREFIX,
    EXPRESSION_STATE_TAIL,
    EXPRESSION_STATE_COUNT,
} ExpressionState;

typedef struct ParserState ParserState;
struct ParserState
{
    ParserDeclaration id;
    union
    {
        struct
        {
            CodeState current_state;
            AstNode* node;
        } code;

        struct
        {
            TypeState current_state;
        } type;

        struct
        {
            union
            {
                IrSymbolAttributes* symbol;
                IrFunctionAttributes* function;
                CodeAttributes* code;
            };
            AttributeListKind kind;
            AttributeListState current_state;
        } attribute_list;

        struct
        {
            AstNode* node;
            u32 brace_depth;
        } block;

        struct
        {
            StatementStateId state;
            StatementId id;
            TokenId end_token;
        } statement;

        struct
        {
            ReturnStatementState state;
        } return_statement;

        struct
        {
            u8 minimum_binding_power;
            ExpressionState state;
            AstNode* current;
            TokenId end_token;
        } expression;
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
};


BUSTER_GLOBAL_LOCAL bool state_is_empty(ParserStateState* stack)
{
    return stack->arena->position < (arena_minimum_position + sizeof(ParserState));
}

BUSTER_GLOBAL_LOCAL ParserState* state_top(ParserStateState* stack)
{
    return arena_get_pointer_at_position(stack->arena, ParserState, stack->arena->position - sizeof(ParserState));
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
    Token* restrict token = &iterator->tokens[iterator->token_index];
    ExtendedToken result = to_extended_token(iterator, *token);
    return result;
}

typedef struct Parser Parser;
struct Parser
{
    TokenIterator iterator;
    ParserStateState state;
    Arena* restrict node_arena;
};

BUSTER_GLOBAL_LOCAL void consume_token(TokenIterator* restrict iterator, Token* restrict token)
{
    bool is_line_feed = token->id == TOKEN_LINE_FEED;
    iterator->line_index += is_line_feed;
    iterator->line_offset = is_line_feed ? iterator->line_offset + iterator->column_index + 1 : iterator->line_offset;
    iterator->column_index = is_line_feed ? 0 : get_token_length(token) + iterator->column_index;
    iterator->token_index += 1;
}

BUSTER_GLOBAL_LOCAL void consume(TokenIterator* restrict iterator)
{
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
        BUSTER_TRAP();
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

BUSTER_GLOBAL_LOCAL void finish_type_reference(Parser* parser)
{
    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_DECLARATION_CODE:
        {
            if (resume_state->code.current_state != CODE_STATE_TYPE)
            {
                BUSTER_TRAP();
            }

            resume_state->code.current_state = CODE_STATE_AFTER_TYPE;
        }
        break; case PARSER_DECLARATION_TYPE_REFERENCE:
        {
            switch (resume_state->type.current_state)
            {
                break; case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
                {
                    resume_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_DELIMITER_OR_CLOSE;
                }
                break; case TYPE_STATE_FUNCTION_RETURN_TYPE:
                {
                    resume_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE;
                }
                break; default: BUSTER_TRAP();
            }
        }
        break; default:
        {
        }
    }
}

typedef enum IrFunctionAttribute 
{
    IR_FUNCTION_ATTRIBUTE_CALLING_CONVENTION,
    IR_FUNCTION_ATTRIBUTE_COUNT,
}IrFunctionAttribute;

BUSTER_GLOBAL_LOCAL String8 function_attribute_names[] = {
    [IR_FUNCTION_ATTRIBUTE_CALLING_CONVENTION] = S8_INITIALIZER("cc"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(function_attribute_names) == IR_FUNCTION_ATTRIBUTE_COUNT);

BUSTER_GLOBAL_LOCAL String8 calling_convention_names[] = {
    [IR_CALLING_CONVENTION_C] = S8_INITIALIZER("c"),
    [IR_CALLING_CONVENTION_SYSTEMV] = S8_INITIALIZER("systemv"),
    [IR_CALLING_CONVENTION_WIN64] = S8_INITIALIZER("win64"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(calling_convention_names) == IR_CALLING_CONVENTION_COUNT);

typedef enum IrSymbolAttribute
{
    IR_SYMBOL_ATTRIBUTE_EXPORT,
    IR_SYMBOL_ATTRIBUTE_COUNT,
} IrSymbolAttribute;

BUSTER_GLOBAL_LOCAL String8 symbol_attribute_names[] = {
    [IR_SYMBOL_ATTRIBUTE_EXPORT] = S8_INITIALIZER("export"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(symbol_attribute_names) == IR_SYMBOL_ATTRIBUTE_COUNT);

typedef struct AstFunctionDeclaration AstFunctionDeclaration;
struct AstFunctionDeclaration
{
    u8 foo;
};

typedef struct AstFunctionDefinition AstFunctionDefinition;
struct AstFunctionDefinition
{
    u8 foo;
};

typedef struct AstBlock AstBlock;
struct AstBlock
{
    u8 foo;
};

typedef enum AstNodeId
{
    AST_NODE_FUNCTION_DEFINITION,
    AST_NODE_FUNCTION_DECLARATION,
    AST_NODE_BLOCK,
    AST_NODE_CONSTANT_INTEGER,
    AST_NODE_COUNT,
} AstNodeId;

typedef enum CodeAttributeId
{
    CODE_ATTRIBUTE_INLINE,
    CODE_ATTRIBUTE_COUNT,
} CodeAttributeId;

BUSTER_GLOBAL_LOCAL String8 code_attributes_names[] = {
    [CODE_ATTRIBUTE_INLINE] = S8_INITIALIZER("inline"),
};

BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(code_attributes_names) == CODE_ATTRIBUTE_COUNT);

typedef struct AstCode AstCode;
struct AstCode
{
    CodeAttributes code_attributes;
    IrSymbolAttributes symbol_attributes;
    String8 name;
};

typedef struct AstNode AstNode;
struct AstNode
{
    union
    {
        AstCode code;
        AstBlock block;
        u64 constant_integer;
    };

    AstNodeId id;
};

BUSTER_GLOBAL_LOCAL AstNode* allocate_node(Parser* restrict parser)
{
    AstNode* node = arena_allocate(parser->node_arena, AstNode, 1);
    *node = (AstNode){0};
    return node;
}

BUSTER_GLOBAL_LOCAL void parse_block(Parser* restrict parser)
{
    ParserState* block_state = state_push(&parser->state);
    block_state->id = PARSER_DECLARATION_BLOCK;
    block_state->block.brace_depth = 1;
    AstNode* node = allocate_node(parser);
    node->id = AST_NODE_BLOCK;
    block_state->block.node = node;
}

BUSTER_GLOBAL_LOCAL void parse_expression(Parser* restrict parser, TokenId end_of_statement_token)
{
    ParserState* state = state_push(&parser->state);
    state->id = PARSER_DECLARATION_EXPRESSION;
    state->expression.state = EXPRESSION_STATE_PREFIX;
    state->expression.end_token = end_of_statement_token;
}

BUSTER_GLOBAL_LOCAL void finish_expression(Parser* restrict parser)
{
    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_DECLARATION_RETURN_STATEMENT: {}
        break; default: BUSTER_TRAP();
    }
}

BUSTER_GLOBAL_LOCAL void parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {0};
    parser.iterator.tokens = tokenizer.tokens;
    parser.iterator.source = source;
    parser.state.arena = arena_create((ArenaCreation){0});
    parser.node_arena = arena_create((ArenaCreation){0});

    // Push a dummy state so the stack is never empty
    state_push(&parser.state);

    bool is_running = true;

    while (is_running)
    {
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
                        function_state->code.node = allocate_node(&parser);
                    }
                    break; case TOKEN_EOF:
                    {
                        is_running = false;
                    }
                    break; default: BUSTER_TRAP();
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
                                attribute_list_state->attribute_list.code = &code_state->code.node->code.code_attributes;
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                code_state->code.node->code.name = get_string(parser.iterator.source, token);
                                code_state->code.current_state = CODE_STATE_AFTER_NAME;
                            }
                            break; default: BUSTER_TRAP();
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
                            }
                            break; case TOKEN_EQUAL:
                            {
                                BUSTER_TRAP();
                            }
                            break; case TOKEN_LEFT_BRACKET:
                            {
                                ParserState* attribute_list_state = state_push(&parser.state);
                                attribute_list_state->id = PARSER_DECLARATION_ATTRIBUTE_LIST;
                                attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_SYMBOL;
                                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                                attribute_list_state->attribute_list.symbol = &code_state->code.node->code.symbol_attributes;
                            }
                            break; default: BUSTER_TRAP();
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

                                parse_block(&parser);
                            }
                            break; case TOKEN_SEMICOLON:
                            {
                                consume(&parser.iterator);
                                state_pop(&parser.state);
                            }
                            break; case TOKEN_EQUAL:
                            {
                                consume(&parser.iterator);
                                code_state->code.current_state = CODE_STATE_AFTER_EQUAL;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case CODE_STATE_AFTER_EQUAL:
                    {
                        if (token.id != TOKEN_LEFT_BRACE)
                        {
                            BUSTER_TRAP();
                        }

                        consume(&parser.iterator);

                        code_state->code.current_state = CODE_STATE_BODY;

                        parse_block(&parser);
                    }
                    break; case CODE_STATE_BODY:
                    {
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
                            }
                            break; case array_slice_token_start:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_SLICE_START;
                            }
                            break; case TOKEN_IDENTIFIER:
                            {
                                consume(&parser.iterator);
                                finish_type_reference(&parser);
                            }
                            break; case TOKEN_KEYWORD_FUNCTION:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_AFTER_FUNCTION_KEYWORD;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_SLICE_START:
                    {
                        switch (token.id)
                        {
                            break; case array_slice_token_end:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                            }
                            // break; case TOKEN_Number:
                            // {
                            //     consume(&parser.iterator)                            //     type_state->type.current_state = TYPE_STATE_AfterArrayCount;
                            // }
                            break; case TOKEN_UNDERSCORE:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_AFTER_ARRAY_INFER_MARKER;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_COUNT:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            BUSTER_TRAP();
                        }

                        consume(&parser.iterator);
                        type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_INFER_MARKER:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            BUSTER_TRAP();
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
                            }
                            break; case TOKEN_LEFT_PARENTHESIS:
                            {
                                consume(&parser.iterator);
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_NAME_OR_CLOSE;
                            }
                            break; default: BUSTER_TRAP();
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
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT:
                    {
                        if (token.id != TOKEN_COLON)
                        {
                            BUSTER_TRAP();
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
                                type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT;
                                break;
                            }
                        }

                        if (!token_begins_type(token.id))
                        {
                            BUSTER_TRAP();
                        }

                        type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_TYPE;

                        ParserState* child_type_state = state_push(&parser.state);
                        child_type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                        child_type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_TYPE:
                    {
                        BUSTER_TRAP();
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
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_RETURN_TYPE:
                    {
                        if (!token_begins_type(token.id))
                        {
                            BUSTER_TRAP();
                        }

                        ParserState* child_type_state = state_push(&parser.state);
                        child_type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                        child_type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_AFTER_FUNCTION_RETURN_TYPE:
                    {
                        finish_type_reference(&parser);
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
                                        BUSTER_TRAP();
                                    }
                                    break; case ATTRIBUTE_LIST_DATA:
                                    {
                                        BUSTER_TRAP();
                                    }
                                    break; case ATTRIBUTE_LIST_SYMBOL:
                                    {
                                        IrSymbolAttribute attribute = IR_SYMBOL_ATTRIBUTE_COUNT;

                                        for (EACH_ARRAY_INDEX(i, symbol_attribute_names))
                                        {
                                            if (string_equal(attribute_name, symbol_attribute_names[i]))
                                            {
                                                attribute = (IrSymbolAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (attribute)
                                        {
                                            break; case IR_SYMBOL_ATTRIBUTE_EXPORT:
                                            {
                                                attribute_list_state->attribute_list.symbol->exported = true;
                                                attribute_list_state->attribute_list.symbol->linkage = IR_LINKAGE_EXTERNAL;
                                            }
                                            break; case IR_SYMBOL_ATTRIBUTE_COUNT: BUSTER_TRAP();
                                          break;
                                        }
                                    }
                                    break; case ATTRIBUTE_LIST_FUNCTION:
                                    {
                                        if (!string_equal(attribute_name, function_attribute_names[IR_FUNCTION_ATTRIBUTE_CALLING_CONVENTION]))
                                        {
                                            BUSTER_TRAP();
                                        }

                                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN;
                                    }
                                    break; case ATTRIBUTE_LIST_COUNT: BUSTER_UNREACHABLE();
                                }
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            BUSTER_TRAP();
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME:
                    {
                        if (!token_matches_any(&parser, token, calling_convention_names, BUSTER_ARRAY_LENGTH(calling_convention_names)))
                        {
                            BUSTER_TRAP();
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            BUSTER_TRAP();
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
                            BUSTER_TRAP();
                        }

                        block_state->block.brace_depth -= 1;
                        if (block_state->block.brace_depth == 0)
                        {
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
                                statement_state->statement.id = STATEMENT_RETURN;

                                ParserState* state = state_push(&parser.state);
                                state->id = PARSER_DECLARATION_RETURN_STATEMENT;
                                state->return_statement.state = RETURN_STATEMENT_STATE_VALUE_OR_END;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case STATEMENT_STATE_END:
                    {
                        bool has_end_of_statement = statement_state->statement.end_token != TOKEN_ERROR;

                        if (has_end_of_statement)
                        {
                            if (token.id != statement_state->statement.end_token)
                            {
                                BUSTER_TRAP();
                            }

                            consume(&parser.iterator);
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

                ParserState* previous_state = return_statement_state - 1;
                BUSTER_CHECK(previous_state->id == PARSER_DECLARATION_STATEMENT);
                TokenId end_of_statement_token = previous_state->statement.end_token;

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
                            BUSTER_TRAP();
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
                        switch (token.id)
                        {
                            break;
                            case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
                            case TOKEN_DECIMAL_INTEGER_LITERAL:
                            case TOKEN_OCTAL_INTEGER_LITERAL:
                            case TOKEN_BINARY_INTEGER_LITERAL:
                            {
                                consume(&parser.iterator);
                                AstNode* number_node = allocate_node(&parser);
                                st->expression.current = number_node;

                                String8 number_string = get_string(parser.iterator.source, token);

                                IntegerParsingU64 number_parsing;
                                switch (token.id)
                                {
                                    break; case TOKEN_HEXADECIMAL_INTEGER_LITERAL:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'x');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; case TOKEN_DECIMAL_INTEGER_LITERAL:
                                    {
                                        number_parsing = string8_parse_u64_decimal(number_string.pointer);
                                    }
                                    break; case TOKEN_OCTAL_INTEGER_LITERAL:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'o');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; case TOKEN_BINARY_INTEGER_LITERAL:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'b');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; default: BUSTER_UNREACHABLE();
                                }

                                *number_node = (AstNode){
                                    .id = AST_NODE_CONSTANT_INTEGER,
                                    .constant_integer = number_parsing.value,
                                };

                                st->expression.state = EXPRESSION_STATE_TAIL;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case EXPRESSION_STATE_TAIL:
                    {
                        if (token.id == st->expression.end_token)
                        {
                            finish_expression(&parser);
                        }
                        else
                        {
                            switch (token.id)
                            {
                                break; case TOKEN_PLUS:
                                {
                                    BUSTER_TRAP();
                                }
                                break; default: BUSTER_TRAP();
                            }
                        }
                    }
                    break; case EXPRESSION_STATE_COUNT: BUSTER_UNREACHABLE();
                }
            }
            break; case PARSER_DECLARATION_TYPE_DECLARATION:
            {
                BUSTER_TRAP();
            }
            break; case PARSER_DECLARATION_DATA_DECLARATION:
            {
                BUSTER_TRAP();
            }
        }
    }

    // return result;
}

BUSTER_GLOBAL_LOCAL void print_tokenizer_result(TokenizerResult tokenizer, const char8* restrict source)
{
    TokenIterator iterator = token_initialize(tokenizer.tokens, tokenizer.token_count, source);
    for (u32 i = 0; i < tokenizer.token_count; i += 1)
    {
        ExtendedToken token = token_get(&iterator);
        String8 string = get_string(source, token);

        String8 token_id;

        switch ((TokenIdEnum)token.id)
        {
            break; case TOKEN_ERROR: token_id = S8("Error");
            break; case TOKEN_SPACE: token_id = S8("Space");
            break; case TOKEN_TAB: token_id = S8("Tab");
            break; case TOKEN_LINE_FEED: token_id = S8("LineFeed");
            break; case TOKEN_CARRIAGE_RETURN: token_id = S8("CarriageReturn");
            break; case TOKEN_COMMENT: token_id = S8("Comment");
            break; case TOKEN_EOF: token_id = S8("EOF");
            break; case TOKEN_IDENTIFIER: token_id = S8("Identifier");
            break; case TOKEN_HEXADECIMAL_INTEGER_LITERAL: token_id = S8("HexadecimalIntegerLiteral");
            break; case TOKEN_DECIMAL_INTEGER_LITERAL: token_id = S8("DecimalIntegerLiteral");
            break; case TOKEN_OCTAL_INTEGER_LITERAL: token_id = S8("OctalIntegerLiteral");
            break; case TOKEN_BINARY_INTEGER_LITERAL: token_id = S8("BinaryIntegerLiteral");
            break; case TOKEN_DECIMAL_FLOAT_LITERAL: token_id = S8("DecimalFloatLiteral");
            break; case TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT: token_id = S8("DecimalFloatLiteralExponent");
            break; case TOKEN_HEXADECIMAL_FLOAT_LITERAL: token_id = S8("HexadecimalFloatLiteral");
            break; case TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT: token_id = S8("HexadecimalFloatLiteralExponent");
            break; case TOKEN_FLOAT_LITERAL: token_id = S8("FloatLiteral");
            break; case TOKEN_UNDERSCORE: token_id = S8("Underscore");
            break; case TOKEN_LEFT_BRACKET: token_id = S8("LeftBracket");
            break; case TOKEN_RIGHT_BRACKET: token_id = S8("RightBracket");
            break; case TOKEN_LEFT_BRACE: token_id = S8("LeftBrace");
            break; case TOKEN_RIGHT_BRACE: token_id = S8("RightBrace");
            break; case TOKEN_LEFT_PARENTHESIS: token_id = S8("LeftParenthesis");
            break; case TOKEN_RIGHT_PARENTHESIS: token_id = S8("RightParenthesis");
            break; case TOKEN_EQUAL: token_id = S8("Equal");
            break; case TOKEN_GREATER: token_id = S8("Greater");
            break; case TOKEN_LESS: token_id = S8("Less");
            break; case TOKEN_PLUS: token_id = S8("Plus");
            break; case TOKEN_PLUS_EQUAL: token_id = S8("PlusEqual");
            break; case TOKEN_MINUS: token_id = S8("Minus");
            break; case TOKEN_ASTERISK: token_id = S8("Asterisk");
            break; case TOKEN_SLASH: token_id = S8("Slash");
            break; case TOKEN_PERCENTAGE: token_id = S8("Percentage");
            break; case TOKEN_COLON: token_id = S8("Colon");
            break; case TOKEN_SEMICOLON: token_id = S8("Semicolon");
            break; case TOKEN_COMMA: token_id = S8("Comma");
            break; case TOKEN_DOT: token_id = S8("Dot");
            break; case TOKEN_DOUBLE_DOT: token_id = S8("DoubleDot");
            break; case TOKEN_TRIPLE_DOT: token_id = S8("TripleDot");
            break; case TOKEN_AMPERSAND: token_id = S8("Ampersand");
            break; case TOKEN_KEYWORD_RETURN: token_id = S8("Keyword_Return");
            break; case TOKEN_KEYWORD_IF: token_id = S8("Keyword_If");
            break; case TOKEN_KEYWORD_ELSE: token_id = S8("Keyword_Else");
            break; case TOKEN_KEYWORD_FUNCTION: token_id = S8("Keyword_Function");
            break; case TOKEN_KEYWORD_FOR: token_id = S8("Keyword_For");
            break; case TOKEN_KEYWORD_WHILE: token_id = S8("Keyword_While");
            break; case TOKEN_KEYWORD_CODE: token_id = S8("Keyword_Code");
            break; case TOKEN_KEYWORD_DATA: token_id = S8("Keyword_Data");
            break; case TOKEN_KEYWORD_TYPE: token_id = S8("Keyword_Type");
            break; case TOKEN_KEYWORD_STRUCT: token_id = S8("Keyword_Struct");
            break; case TOKEN_KEYWORD_UNION: token_id = S8("Keyword_Union");
            break; case TOKEN_COUNT: token_id = S8("Token_Count(Error)");
            break; default: BUSTER_TRAP();
        }

        string_print(S8("[{u64}] {u32}:{u32} at {u32} {S8} \"{S8}\"\n"), i, token.line, token.column, token.offset, token_id, string.pointer[0] >= ' ' ? string : S8(""));

        consume(&iterator);
    }
}

BUSTER_GLOBAL_LOCAL void parse_experiment(Arena* arena, String8 path)
{
    u64 position = arena->position;

    String8 source = BYTE_SLICE_TO_STRING(8, file_read(arena, path, (FileReadOptions){0}));

    TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
    print_tokenizer_result(tokenizer, source.pointer);
    parse(source.pointer, tokenizer);

    // string8_print(S8("=== Input ===\n{S8}\n"), source);
    // string8_print(S8("=== Error Count ===\n{u64}\n"), result.error_count);
    // string8_print(S8("=== Prefix Output (token reordering) ===\n"));
    // print_prefix(result, tokenizer.tokens.pointer, source.pointer);
    
    arena->position = position;
}

void parser_experiments(void)
{
    Arena* arena = arena_create((ArenaCreation){0});
    parse_experiment(arena, S8("tests/basic.bbb"));
    parse_experiment(arena, S8("tests/basic_hexadecimal.bbb"));
    parse_experiment(arena, S8("tests/basic_octal.bbb"));
    parse_experiment(arena, S8("tests/basic_binary.bbb"));
    // parse_experiment(arena, S8("tests/basic_sum.bbb"));
    // parse_experiment(arena, S8("tests/if_else.bbb"));
    // parse_experiment(arena, S8("tests/array_slices.bbb"));
}

#if BUSTER_INCLUDE_TESTS
BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    BatchTestResult result = {0};
    parser_experiments();
    return result;
}
#endif
