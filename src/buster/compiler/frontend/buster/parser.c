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
        break; default: BUSTER_TODO();
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
                    break; BUSTER_SWITCH_ALPHA_UPPER: BUSTER_SWITCH_ALPHA_LOWER:
                    case '_':
                    {
                        while (true)
                        {
                            const char8* restrict it_start = it;
                            switch (*it_start)
                            {
                                break; BUSTER_SWITCH_ALPHA_UPPER: BUSTER_SWITCH_ALPHA_LOWER: BUSTER_SWITCH_DECIMAL_DIGIT:
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
                    break; BUSTER_SWITCH_DECIMAL_DIGIT:
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

                        const char8* restrict digits_start = it;

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
                                        BUSTER_SWITCH_DECIMAL_DIGIT:
                                        BUSTER_SWITCH_HEX_ALPHA:
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
                                        BUSTER_SWITCH_DECIMAL_DIGIT:
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

                        // A prefixed literal ("0x"/"0o"/"0b") with no digits after the prefix is malformed.
                        is_valid = is_valid && (format == INTEGER_FORMAT_DECIMAL || it != digits_start);

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
                                            break; BUSTER_SWITCH_DECIMAL_DIGIT: BUSTER_SWITCH_HEX_ALPHA: case '_': increment = true;
                                            break; default: increment = false;
                                        }
                                    }
                                    break; case INTEGER_FORMAT_DECIMAL:
                                    {
                                        switch (ch)
                                        {
                                            break; BUSTER_SWITCH_DECIMAL_DIGIT: BUSTER_SWITCH_HEX_ALPHA: case '_': increment = true;
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
                                            break; BUSTER_SWITCH_DECIMAL_DIGIT: increment = true;
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
                            BUSTER_TODO();
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
                    break; case '<':
                    {
                        if (it + 1 < top && it[1] == '<')
                        {
                            id = TOKEN_SHIFT_LEFT;
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
                    break; case '=': { id = TOKEN_EQUAL; it += 1; }
                    break; case ':': { id = TOKEN_COLON; it += 1; }
                    break; case ';': { id = TOKEN_SEMICOLON; it += 1; }
                    break; case ',': { id = TOKEN_COMMA; it += 1; }
                    break; case '&': { id = TOKEN_AMPERSAND; it += 1; }
                    break; case '%': { id = TOKEN_PERCENTAGE; it += 1; }
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
                    break; default: BUSTER_UNREACHABLE();
                }

                const char8* restrict end = it;
                u64 length = (u64)(end - start);
                if (length > (((u64)1 << 24) - 1))
                {
                    BUSTER_TODO(); // TODO: error
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
            BUSTER_TODO();
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
typedef struct AstExpressionNode AstExpressionNode;

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

// Depth of the per-expression operator and unary shunting-yard stacks. Left-
// associative binary operators only ever stack strictly-increasing binding
// powers, so this is bounded by the number of precedence levels; the unary
// stack is bounded by the length of a prefix run (`- - -x`). Both are checked.
enum { EXPRESSION_STACK_CAPACITY = 16 };

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
            ExpressionState state;
            TokenId end_token;
            // Postorder (RPN) output stream this expression emits into. The tree
            // is implicit in this ordering plus each node's arity, so no child
            // links are stored. `output_base` is the first emitted node.
            AstExpressionNode* output_base;
            u32 output_count;
            // Shunting-yard stacks holding operator/unary kinds (AstNodeId cast to
            // u8) not yet emitted. Operators are held until precedence resolves;
            // unaries until their operand is emitted.
            u16 operator_count;
            u16 unary_count;
            u8 operator_stack[EXPRESSION_STACK_CAPACITY];
            u8 unary_stack[EXPRESSION_STACK_CAPACITY];
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
    // Contiguous backing store for the postorder expression streams.
    Arena* restrict expression_arena;
    // Most recently finished expression (e.g. a return value).
    AstExpressionNode* expression_nodes;
    u32 expression_count;
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
    AST_NODE_UNARY_MINUS,
    AST_NODE_UNARY_PLUS,
    AST_NODE_BINARY_PLUS,
    AST_NODE_BINARY_MINUS,
    AST_NODE_BINARY_ASTERISK,
    AST_NODE_BINARY_SLASH,
    AST_NODE_BINARY_PERCENT,
    AST_NODE_BINARY_SHIFT_LEFT,
    AST_NODE_BINARY_SHIFT_RIGHT,
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

typedef union AstBinary AstBinary;
union AstBinary
{
    struct
    {
        AstNode* left;
        AstNode* right;
    };
    AstNode* operands[2];
};

typedef struct AstUnary AstUnary;
struct AstUnary
{
    AstNode* operand;
};

typedef struct AstNode AstNode;
struct AstNode
{
    union
    {
        AstCode code;
        AstBlock block;
        u64 constant_integer;
        AstBinary binary;
        AstUnary unary;
    };

    AstNodeId id;
};

// A node in the flattened, postorder expression stream. The tree is *implicit*:
// a node's operands are the subtrees emitted immediately before it, and its
// arity is fixed by its kind (see ast_node_arity), so no child links are stored.
// Because the whole expression is one contiguous array in evaluation order,
// analysis and typechecking stream through it front-to-back with a small operand
// stack — sequential access, branch-predictable, no pointer chasing.
struct AstExpressionNode
{
    AstNodeId id;
    u32 reserved;
    u64 constant_integer; // leaf payload; unused by operator nodes
};

typedef struct AstExpression AstExpression;
struct AstExpression
{
    AstExpressionNode* nodes; // postorder
    u32 count;
    u32 reserved;
};

BUSTER_GLOBAL_LOCAL AstNode* allocate_nodes(Parser* restrict parser, u64 count)
{
    AstNode* result = arena_allocate(parser->node_arena, AstNode, count);
    memset(result, 0, sizeof(*result) * count);
    return result;
}

BUSTER_GLOBAL_LOCAL AstNode* allocate_node(Parser* restrict parser)
{
    return allocate_nodes(parser, 1);
}

// Left binding power of a binary operator: higher binds tighter. Mirrors C/Zig
// precedence for these operators (multiplicative > additive > shift).
BUSTER_GLOBAL_LOCAL u8 binary_binding_power(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
            return 1;
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
            return 2;
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
            return 3;
        default:
            BUSTER_UNREACHABLE();
    }
}

// Number of operand subtrees a node consumes. Drives the implicit-tree walk:
// leaves push, unary pops 1, binary pops 2.
BUSTER_GLOBAL_LOCAL u32 ast_node_arity(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_CONSTANT_INTEGER:
            return 0;
        case AST_NODE_UNARY_MINUS:
        case AST_NODE_UNARY_PLUS:
            return 1;
        case AST_NODE_BINARY_PLUS:
        case AST_NODE_BINARY_MINUS:
        case AST_NODE_BINARY_ASTERISK:
        case AST_NODE_BINARY_SLASH:
        case AST_NODE_BINARY_PERCENT:
        case AST_NODE_BINARY_SHIFT_LEFT:
        case AST_NODE_BINARY_SHIFT_RIGHT:
            return 2;
        default:
            BUSTER_UNREACHABLE();
    }
}

BUSTER_GLOBAL_LOCAL String8 ast_node_symbol(AstNodeId id)
{
    switch (id)
    {
        case AST_NODE_UNARY_MINUS: return S8("neg");
        case AST_NODE_UNARY_PLUS: return S8("pos");
        case AST_NODE_BINARY_PLUS: return S8("+");
        case AST_NODE_BINARY_MINUS: return S8("-");
        case AST_NODE_BINARY_ASTERISK: return S8("*");
        case AST_NODE_BINARY_SLASH: return S8("/");
        case AST_NODE_BINARY_PERCENT: return S8("%");
        case AST_NODE_BINARY_SHIFT_LEFT: return S8("<<");
        case AST_NODE_BINARY_SHIFT_RIGHT: return S8(">>");
        default: BUSTER_UNREACHABLE();
    }
}

// Append a node to the current expression's postorder output stream.
BUSTER_GLOBAL_LOCAL AstExpressionNode* expression_emit(Parser* restrict parser, ParserState* restrict st, AstNodeId id)
{
    AstExpressionNode* node = arena_allocate(parser->expression_arena, AstExpressionNode, 1);
    node->id = id;
    node->reserved = 0;
    node->constant_integer = 0;
    if (st->expression.output_count == 0)
    {
        st->expression.output_base = node;
    }
    st->expression.output_count += 1;
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
    ParserState* expression_state = state(parser);
    BUSTER_CHECK(expression_state->id == PARSER_DECLARATION_EXPRESSION);
    parser->expression_nodes = expression_state->expression.output_base;
    parser->expression_count = expression_state->expression.output_count;

    state_pop(&parser->state);

    ParserState* resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case PARSER_DECLARATION_RETURN_STATEMENT: {}
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
        break; case TOKEN_GREATER: return S8("Greater");
        break; case TOKEN_LESS: return S8("Less");
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

BUSTER_GLOBAL_LOCAL AstExpression parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {0};
    parser.iterator.tokens = tokenizer.tokens;
    parser.iterator.source = source;
    parser.state.arena = arena_create((ArenaCreation){0});
    parser.node_arena = arena_create((ArenaCreation){0});
    parser.expression_arena = arena_create((ArenaCreation){0});

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
                    break; default: BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
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
                                BUSTER_TODO();
                            }
                            break; case TOKEN_LEFT_BRACKET:
                            {
                                ParserState* attribute_list_state = state_push(&parser.state);
                                attribute_list_state->id = PARSER_DECLARATION_ATTRIBUTE_LIST;
                                attribute_list_state->attribute_list.kind = ATTRIBUTE_LIST_SYMBOL;
                                attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_ITEM_OR_CLOSE;
                                attribute_list_state->attribute_list.symbol = &code_state->code.node->code.symbol_attributes;
                            }
                            break; default: BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case CODE_STATE_AFTER_EQUAL:
                    {
                        if (token.id != TOKEN_LEFT_BRACE)
                        {
                            BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_COUNT:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            BUSTER_TODO();
                        }

                        consume(&parser.iterator);
                        type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
                    }
                    break; case TYPE_STATE_AFTER_ARRAY_INFER_MARKER:
                    {
                        if (token.id != TOKEN_RIGHT_BRACKET)
                        {
                            BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_ARGUMENT_AFTER_NAME_SEGMENT:
                    {
                        if (token.id != TOKEN_COLON)
                        {
                            BUSTER_TODO();
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
                            BUSTER_TODO();
                        }

                        type_state->type.current_state = TYPE_STATE_FUNCTION_ARGUMENT_TYPE;

                        ParserState* child_type_state = state_push(&parser.state);
                        child_type_state->id = PARSER_DECLARATION_TYPE_REFERENCE;
                        child_type_state->type.current_state = TYPE_STATE_PREFIX_OR_BASE;
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
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case TYPE_STATE_FUNCTION_RETURN_TYPE:
                    {
                        if (!token_begins_type(token.id))
                        {
                            BUSTER_TODO();
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
                                        BUSTER_TODO();
                                    }
                                    break; case ATTRIBUTE_LIST_DATA:
                                    {
                                        BUSTER_TODO();
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
                                            break; case IR_SYMBOL_ATTRIBUTE_COUNT: BUSTER_TODO();
                                          break;
                                        }
                                    }
                                    break; case ATTRIBUTE_LIST_FUNCTION:
                                    {
                                        if (!string_equal(attribute_name, function_attribute_names[IR_FUNCTION_ATTRIBUTE_CALLING_CONVENTION]))
                                        {
                                            BUSTER_TODO();
                                        }

                                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN;
                                    }
                                    break; case ATTRIBUTE_LIST_COUNT: BUSTER_UNREACHABLE();
                                }
                            }
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_OPEN:
                    {
                        if (token.id != TOKEN_LEFT_PARENTHESIS)
                        {
                            BUSTER_TODO();
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_NAME:
                    {
                        if (!token_matches_any(&parser, token, calling_convention_names, BUSTER_ARRAY_LENGTH(calling_convention_names)))
                        {
                            BUSTER_TODO();
                        }

                        attribute_list_state->attribute_list.current_state = ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE;
                    }
                    break; case ATTRIBUTE_LIST_STATE_CALLING_CONVENTION_CLOSE:
                    {
                        if (token.id != TOKEN_RIGHT_PARENTHESIS)
                        {
                            BUSTER_TODO();
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
                            break; default: BUSTER_TODO();
                        }
                    }
                    break; case STATEMENT_STATE_END:
                    {
                        bool has_end_of_statement = statement_state->statement.end_token != TOKEN_ERROR;

                        if (has_end_of_statement)
                        {
                            if (token.id != statement_state->statement.end_token)
                            {
                                BUSTER_TODO();
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
                            BUSTER_TODO();
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
                                        number_parsing = string8_parse_u64_octal(number_string.pointer + 2);
                                    }
                                    break; case TOKEN_BINARY_INTEGER_LITERAL:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'b');
                                        number_parsing = string8_parse_u64_binary(number_string.pointer + 2);
                                    }
                                    break; default: BUSTER_UNREACHABLE();
                                }

                                AstExpressionNode* leaf = expression_emit(&parser, st, AST_NODE_CONSTANT_INTEGER);
                                leaf->constant_integer = number_parsing.value;

                                // Prefix unary operators bind tighter than any binary operator, so
                                // they are emitted right after their operand, innermost first.
                                while (st->expression.unary_count)
                                {
                                    st->expression.unary_count -= 1;
                                    expression_emit(&parser, st, (AstNodeId)st->expression.unary_stack[st->expression.unary_count]);
                                }

                                st->expression.state = EXPRESSION_STATE_TAIL;
                            }
                            break;
                            case TOKEN_MINUS:
                            case TOKEN_PLUS:
                            {
                                consume(&parser.iterator);
                                AstNodeId unary_id = token.id == TOKEN_MINUS ? AST_NODE_UNARY_MINUS : AST_NODE_UNARY_PLUS;
                                BUSTER_CHECK(st->expression.unary_count < EXPRESSION_STACK_CAPACITY);
                                st->expression.unary_stack[st->expression.unary_count] = (u8)unary_id;
                                st->expression.unary_count += 1;
                            }
                            break; default: BUSTER_TODO();
                        }
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
                                        break; default: BUSTER_TODO_TOKEN(token.id);
                                    }

                                    // Shunting yard: before pushing this operator, emit every stacked
                                    // operator that binds at least as tightly. `>=` makes equal
                                    // precedence left-associative; a strictly-tighter incoming
                                    // operator stays pending so it captures the next operand instead.
                                    u8 binding_power = binary_binding_power(binary_node_id);
                                    while (st->expression.operator_count &&
                                           binary_binding_power((AstNodeId)st->expression.operator_stack[st->expression.operator_count - 1]) >= binding_power)
                                    {
                                        st->expression.operator_count -= 1;
                                        expression_emit(&parser, st, (AstNodeId)st->expression.operator_stack[st->expression.operator_count]);
                                    }

                                    BUSTER_CHECK(st->expression.operator_count < EXPRESSION_STACK_CAPACITY);
                                    st->expression.operator_stack[st->expression.operator_count] = (u8)binary_node_id;
                                    st->expression.operator_count += 1;
                                    st->expression.state = EXPRESSION_STATE_PREFIX;
                                }
                                break; default: BUSTER_TODO_TOKEN(token.id);
                            }
                        }
                    }
                    break; case EXPRESSION_STATE_COUNT: BUSTER_UNREACHABLE();
                }
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

    AstExpression result = {
        .nodes = parser.expression_nodes,
        .count = parser.expression_count,
    };
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
                formatted = string_format(arena, S8("{u64}"), node->constant_integer);
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

BUSTER_GLOBAL_LOCAL void parse_experiment(Arena* arena, String8 path)
{
    u64 position = arena->position;

    // The tokenizer peeks a few bytes past the last consumed character (e.g. exponent sign/digit
    // lookahead on numeric literals); end_padding guarantees those reads land on zeroed memory
    // instead of past the arena allocation.
    String8 source = BYTE_SLICE_TO_STRING(8, file_read(arena, path, (FileReadOptions){ .end_padding = 8 }));

    string_print(S8("Tokenizing \"{S8}\"\n"), path);

    if (!source.pointer || !source.length)
    {
        // Missing/empty input (e.g. CWD has no tests/ directory): nothing to parse.
        arena->position = position;
        return;
    }

    TokenizerResult tokenizer = tokenize(arena, source.pointer, source.length);
    print_tokenizer_result(tokenizer, source.pointer);
    string_print(S8("Parsing \"{S8}\"\n"), path);
    AstExpression expression = parse(source.pointer, tokenizer);

    if (expression.count)
    {
        String8 dump = ast_expression_to_string(arena, expression);
        string_print(S8("=== Expression (postorder, {u32} nodes) ===\n{S8}\n"), expression.count, dump);
    }

    // string8_print(S8("=== Input ===\n{S8}\n"), source);
    // string8_print(S8("=== Error Count ===\n{u64}\n"), result.error_count);
    // string8_print(S8("=== Prefix Output (token reordering) ===\n"));
    // print_prefix(result, tokenizer.tokens.pointer, source.pointer);
    
    arena->position = position;
}

void parser_experiments(void)
{
    Arena* arena = arena_create((ArenaCreation){0});
    String8 experiments[] = {
        S8("tests/basic_minimal.bbb"),
        S8("tests/basic_comment.bbb"),
        S8("tests/basic_hexadecimal_literal.bbb"),
        S8("tests/basic_octal_literal.bbb"),
        S8("tests/basic_binary_literal.bbb"),
        S8("tests/basic_unary_minus.bbb"),
        S8("tests/basic_unary_plus.bbb"),
        S8("tests/basic_integer_literal_add.bbb"),
        S8("tests/basic_integer_literal_sub.bbb"),
        S8("tests/basic_integer_literal_multiply.bbb"),
        S8("tests/basic_integer_literal_divide.bbb"),
        S8("tests/basic_integer_literal_mod.bbb"),
        S8("tests/basic_integer_literal_shift_left.bbb"),
        S8("tests/basic_integer_literal_shift_right.bbb"),
        S8("tests/basic_integer_literal_precedence.bbb"),
        // S8("tests/basic_if_else.bbb"),
        // S8("tests/array_slices.bbb"),
    };

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(experiments); i +=1)
    {
        String8 experiment = experiments[i];
        parse_experiment(arena, experiment);
    }
}

#if BUSTER_INCLUDE_TESTS
// Parse a bare expression by wrapping it in a minimal program and returning the
// postorder stream of its return value.
BUSTER_GLOBAL_LOCAL AstExpression parse_expression_snippet(Arena* arena, String8 expression)
{
    // Built by concatenation rather than string_format: the program braces would
    // otherwise be mistaken for format directives.
    String8 prefix = S8("code main[export] : fn[cc(c)] (argument:count: s32, argv: &&u8, envp: &&u8) s32\n{\n    return ");
    String8 suffix = S8(";\n}\n");

    // The tokenizer peeks a few bytes past the end; give it zeroed padding.
    u64 padding = 8;
    u64 length = prefix.length + expression.length + suffix.length;
    char8* buffer = arena_allocate(arena, char8, length + padding);
    memcpy(buffer, prefix.pointer, prefix.length);
    memcpy(buffer + prefix.length, expression.pointer, expression.length);
    memcpy(buffer + prefix.length + expression.length, suffix.pointer, suffix.length);
    memset(buffer + length, 0, padding);

    TokenizerResult tokenizer = tokenize(arena, buffer, length);
    return parse(buffer, tokenizer);
}

UnitTestResult parser_expression_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;

    struct { String8 source; String8 expected; } cases[] = {
        { S8("1 + 2 * 3"),           S8("(+ 1 (* 2 3))") },
        { S8("1 * 2 + 3"),           S8("(+ (* 1 2) 3)") },
        { S8("1 + 2 + 3"),           S8("(+ (+ 1 2) 3)") },
        { S8("1 - 2 - 3"),           S8("(- (- 1 2) 3)") },
        { S8("2 * 3 + 4 * 5"),       S8("(+ (* 2 3) (* 4 5))") },
        { S8("2 * -3"),              S8("(* 2 (neg 3))") },
        { S8("- - 5"),               S8("(neg (neg 5))") },
        { S8("1 << 2 + 3 * 4"),      S8("(<< 1 (+ 2 (* 3 4)))") },
        { S8("1 + 2 * 3 << 4 - 5"),  S8("(<< (+ 1 (* 2 3)) (- 4 5))") },
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

BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    BatchTestResult result = {0};
    parser_experiments();
    return result;
}
#endif
