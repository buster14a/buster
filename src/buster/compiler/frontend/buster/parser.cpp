#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/assertion.h>

ENUM_T(TokenId, u8,
    Error,
    Space,
    LineFeed,
    CarriageReturn,
    SOF,
    EOF,
    Identifier,
    Number,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    LeftParenthesis,
    RightParenthesis,
    ListStart,
    ListEnd,
    Equal,
    Greater,
    Less,
    Plus,
    Minus,
    Asterisk,
    Slash,
    Percentage,
    Colon,
    Semicolon,
    Comma,
    Ampersand,
    Keyword_First,
    Keyword_Return,
    Keyword_If,
    Keyword_Else,
    Keyword_Function,
    Keyword_Let,
    Keyword_Last,
    Nonsense);

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)TokenId::Keyword_Last - (u64)TokenId::Keyword_First - 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [-1 + (u64)TokenId::Keyword_Function - (u64)TokenId::Keyword_First] = S8("fn"),
    [-1 + (u64)TokenId::Keyword_If - (u64)TokenId::Keyword_First] = S8("if"),
    [-1 + (u64)TokenId::Keyword_Else - (u64)TokenId::Keyword_First] = S8("else"),
    [-1 + (u64)TokenId::Keyword_Return - (u64)TokenId::Keyword_First] = S8("return"),
    [-1 + (u64)TokenId::Keyword_Let - (u64)TokenId::Keyword_First] = S8("let"),
};
static_assert(BUSTER_ARRAY_LENGTH(keyword_names) == keyword_count);

STRUCT(Token)
{
    TokenId id;
    u8 length;
};

static_assert(sizeof(Token) == 2);

STRUCT(TokenizerResult)
{
    Slice<Token> tokens;
    u64 error_count;
};

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

#define SWITCH_DIGIT \
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

BUSTER_GLOBAL_LOCAL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
{
    TokenizerResult result = {};

    if (file_length)
    {
        let token_start = arena_allocate(arena, Token, file_length + 2); // SOF, EOF
        let tokens = token_start;
        u64 token_count = 0;

        let it = file_pointer;
        let top = file_pointer + file_length;

        tokens[token_count++] = {
            .id = TokenId::SOF,
            .length = 1,
        };

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            let start = it;
            let ch = *start;

            TokenId id;

            switch (ch)
            {
                break;
                SWITCH_ALPHA_UPPER:
                SWITCH_ALPHA_LOWER:
                case '_':
                {
                    while (true)
                    {
                        let it_start = it;
                        switch (*it_start)
                        {
                            break;
                            SWITCH_ALPHA_UPPER:
                            SWITCH_ALPHA_LOWER:
                            SWITCH_DIGIT:
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

                    let identifier = string8_from_pointer_length(start, (u64)(it - start));

                    u64 i;
                    for (i = 0; i < keyword_count; i += 1)
                    {
                        if (string8_equal(identifier, keyword_names[i]))
                        {
                            break;
                        }
                    }

                    if (i == keyword_count)
                    {
                        id = TokenId::Identifier;
                    }
                    else
                    {
                        id = (TokenId)(i + 1 + (u64)TokenId::Keyword_First);
                    }
                }
                break; case ' ':
                {
                    id = TokenId::Space;

                    while (*it == ' ')
                    {
                        it += 1;
                    }
                }
                break; SWITCH_DIGIT:
                {
                    id = TokenId::Number;

                    while (true)
                    {
                        let it_start = it;

                        switch (*it_start)
                        {
                            break; SWITCH_DIGIT: it += 1;
                            break; default: break;
                        }

                        if (it - it_start == 0)
                        {
                            break;
                        }
                    }
                }
                break; case '\n': { id = TokenId::LineFeed; it += 1; }
                break; case '[': { id = TokenId::LeftBracket; it += 1; }
                break; case ']': { id = TokenId::RightBracket; it += 1; }
                break; case '{': { id = TokenId::LeftBrace; it += 1; }
                break; case '}': { id = TokenId::RightBrace; it += 1; }
                break; case '(': { id = TokenId::LeftParenthesis; it += 1; }
                break; case ')': { id = TokenId::RightParenthesis; it += 1; }
                break; case '<': { id = TokenId::Less; it += 1; }
                break; case '>': { id = TokenId::Greater; it += 1; }
                break; case '+': { id = TokenId::Plus; it += 1; }
                break; case '-': { id = TokenId::Minus; it += 1; }
                break; case '*': { id = TokenId::Asterisk; it += 1; }
                break; case '/': { id = TokenId::Slash; it += 1; }
                break; case '=': { id = TokenId::Equal; it += 1; }
                break; case ':': { id = TokenId::Colon; it += 1; }
                break; case ';': { id = TokenId::Semicolon; it += 1; }
                break; case ',': { id = TokenId::Comma; it += 1; }
                break; case '&': { id = TokenId::Ampersand; it += 1; }
                break; default: BUSTER_UNREACHABLE();
            }

            let end = it;
            let length = (u32)(end - start);

            bool big_length = length > UINT8_MAX;

            let token_index = token_count;
            tokens[token_index] = { 
                .id = id,
                .length = BUSTER_SELECT(big_length, (u8)0, (u8)length),
            };

            static_assert(sizeof(Token) * 2 == sizeof(u32));

            *(u32*)(&tokens[token_index + 1]) = length;
            token_count = token_index + 1 + ((u32)big_length << 1);
        }

        tokens[token_count++] = {
            .id = TokenId::EOF,
            .length = 1,
        };

        result.tokens.pointer = token_start;
        result.tokens.length = token_count;
    }

    return result;
}

STRUCT(ParserResult)
{
    Token* restrict pointer;
    u32 count;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL void print_token(TokenId id)
{
    bool a = true;
    String8 result = {};
    switch (id)
    {
        break;
        case TokenId::SOF:
        case TokenId::EOF:
        {}
        break; case TokenId::Semicolon: result = S8("; ");
        break; case TokenId::ListStart: result = S8("[ ");
        break; case TokenId::ListEnd: result = S8("] ");
        break; case TokenId::Identifier: result = S8("Identifier ");
        break; case TokenId::LeftParenthesis: result = S8("( ");
        break; case TokenId::RightParenthesis: result = S8(") ");
        break; case TokenId::LeftBrace: result = S8("{{ ");
        break; case TokenId::RightBrace: result = S8("}} ");
        break; case TokenId::Colon: result = S8(": ");
        break; case TokenId::Comma: result = S8(", ");
        break; case TokenId::Ampersand: result = S8("& ");
        break; case TokenId::Keyword_Return: result = S8("return ");
        break; case TokenId::Number: result = S8("Number ");
        break; default: if (a) BUSTER_UNREACHABLE();
    }

    if (result.pointer)
    {
        string8_print(result);
    }
}

STRUCT(TokenGet)
{
    u32 index;
    Token token;
    u8 reserved[2];
};

STRUCT(TokenArray)
{
    Arena* token_arena;
    Arena* index_arena;

    BUSTER_INLINE void append(TokenGet get)
    {
        print_token(get.token.id);
        *arena_allocate(token_arena, Token, 1) = get.token;
        *arena_allocate(token_arena, u32, 1) = get.index;
    }
};

STRUCT(TokenPlusIndex)
{
    Token token;
    u16 index_low;
    u16 index_high;
};

STRUCT(TokenStack)
{
    Arena* arena;

    BUSTER_INLINE void push(Token token, u32 index)
    {
        let allocation = arena_allocate(arena, TokenPlusIndex, 1);
        *allocation = {
            .token = token,
            .index_low = (u16)(index >> 0),
            .index_high = (u16)(index >> 16),
        };
    }

    BUSTER_INLINE bool is_empty()
    {
        return arena->position == arena_minimum_position;
    }

    BUSTER_INLINE Token pop()
    {
        let position = arena->position - sizeof(TokenPlusIndex);
        let token = arena_get_pointer(arena, TokenPlusIndex, position);
        BUSTER_CHECK(position >= arena_minimum_position);
        arena->position = position;
        return token->token;
    }

    BUSTER_INLINE TokenPlusIndex top()
    {
        let token = arena_get_pointer(arena, TokenPlusIndex, arena->position - sizeof(TokenPlusIndex));
        return *token;
    }
};

STRUCT(StateFlags)
{
    bool is_function;
    bool is_declaration;
};

STRUCT(StateStack)
{
    Arena* arena;

    BUSTER_INLINE void push(StateFlags state)
    {
        let allocation = arena_allocate(arena, StateFlags, 1);
        *allocation = state;
    }

    BUSTER_INLINE void pop()
    {
        let position = arena->position - sizeof(StateFlags);
        // let token = arena_get_pointer(arena, Token, position);
        arena->position = position;
    }

    BUSTER_INLINE StateFlags top()
    {
        StateFlags result = {};
        let position = arena->position - sizeof(StateFlags);
        if (position >= arena_minimum_position)
        {
            result = *arena_get_pointer(arena, StateFlags, position);
        }
        return result;
    }
};

STRUCT(State)
{
    TokenStack operators;
    TokenStack operands;
    StateStack states;
};

// TODO: transform
// BUSTER_GLOBAL_LOCAL u32 token_hierarchy(TokenId id)
// {
//     u32 result;
//
//     switch (id)
//     {
//         break;
//         case TokenId::ListStart:
//         case TokenId::LeftParenthesis:
//         {
//             return 0;
//         }
//         break; case TokenId::Colon:
//         {
//             return 1;
//         }
//         break; case TokenId::Keyword_Function:
//         {
//             return 2;
//         }
//         break;
//         case TokenId::RightParenthesis:
//         case TokenId::RightBracket:
//         case TokenId::ListEnd:
//         {
//             return UINT32_MAX;
//         }
//         break; default: BUSTER_UNREACHABLE();
//     }
//
//     return result;
// }

// BUSTER_GLOBAL_LOCAL ParserResult parse(TokenizerResult tokenizer)
// {
//     ParserResult result = {};
//     // TokenArray output = {
//     //     .token_arena = arena_create((ArenaCreation){}),
//     //     .index_arena = arena_create((ArenaCreation){}),
//     // };
//     State state = {
//         .operators = { .arena = arena_create({}) },
//         .operands = { .arena = arena_create({}) },
//         .states = { .arena = arena_create({}) },
//     };
//
//     Token* restrict tokens = tokenizer.tokens.pointer;
//     let token_count = tokenizer.tokens.length;
//     BUSTER_UNUSED(tokens);
//     BUSTER_UNUSED(token_count);
//     u32 token_index = 0;
//     BUSTER_UNUSED(token_index);
//
//     while (token_index < token_count)
//     {
//
//         Token token = tokens[token_index];
//
//         bool is_operand;
//         switch (token.id)
//         {
//             break;
//             case TokenId::Keyword_Function:
//             case TokenId::LeftBracket:
//             case TokenId::ListStart:
//             case TokenId::ListEnd:
//             case TokenId::LeftParenthesis:
//             case TokenId::RightParenthesis:
//             case TokenId::Colon:
//             {
//                 is_operand = false;
//             }
//             break; case TokenId::Identifier:
//             {
//                 is_operand = true;
//             }
//             break; default: BUSTER_UNREACHABLE();
//         }
//
//         if (is_operand)
//         {
//             state.operands.push(token, token_index);
//         }
//         else
//         {
//             if (token.id == TokenId::LeftParenthesis || state.operators.is_empty() || token_hierarchy(token.id) < token_hierarchy(state.operators.top().token.id))
//             {
//                 state.operators.push(token, token_index);
//             }
//             else if (token.id == TokenId::RightParenthesis || token.id == TokenId::ListEnd)
//             {
//                 TokenId matching;
//                 switch (token.id)
//                 {
//                     break; case TokenId::RightParenthesis: matching = TokenId::LeftParenthesis;
//                     break; case TokenId::ListEnd: matching = TokenId::ListStart;
//                     break; default: BUSTER_UNREACHABLE();
//                 }
//
//                 while (!state.operators.is_empty() && state.operators.top().token.id != matching)
//                 {
//                     BUSTER_TRAP();
//                 }
//
//                 state.operators.pop();
//             }
//             else if (token_hierarchy(token.id) >= token_hierarchy(state.operators.top().token.id))
//             {
//                 while (!state.operators.is_empty() && token_hierarchy(token.id) >= token_hierarchy(state.operators.top().token.id))
//                 {
//                     let op = state.operators.pop();
//                     let operand1 = state.operands.pop();
//                     let operand2 = state.operands.pop();
//
//                     BUSTER_UNUSED(op);
//                     BUSTER_UNUSED(operand1);
//                     BUSTER_UNUSED(operand2);
//                     BUSTER_TRAP();
//                 }
//                 BUSTER_TRAP();
//             }
//         }
//
//         token_index += 1;
//     }
//
//     bool v = true;
//     if (v) BUSTER_TRAP();
//
//     return result;
// }

STRUCT(ParserInputIterator)
{
    Token* restrict pointer;
    u32 count;
    u32 index;
    StateStack state;
};

STRUCT(Parser)
{
    ParserInputIterator tokens;
    TokenArray output;

    TokenGet get()
    {
        bool is_ready = false;
        while (!is_ready)
        {
            Token* restrict token = &tokens.pointer[tokens.index];
            switch (token->id)
            {
                break;
                case TokenId::Keyword_Function:
                case TokenId::Keyword_Let:
                case TokenId::Keyword_Return:
                case TokenId::ListStart:
                case TokenId::ListEnd:
                case TokenId::Identifier:
                case TokenId::LeftParenthesis:
                case TokenId::RightParenthesis:
                case TokenId::Colon:
                case TokenId::Semicolon:
                case TokenId::Comma:
                case TokenId::Ampersand:
                case TokenId::LeftBrace:
                case TokenId::RightBrace:
                case TokenId::Equal:
                case TokenId::Number:
                case TokenId::SOF:
                case TokenId::EOF:
                {
                    is_ready = true;
                }
                break; case TokenId::LeftBracket:
                {
                    bool is_function_attribute = tokens.state.top().is_function;
                    bool is_declaration_attribute = tokens.state.top().is_declaration;

                    if (is_function_attribute || is_declaration_attribute)
                    {
                        token->id = TokenId::ListStart;
                    }
                }
                break; case TokenId::RightBracket:
                {
                    // TODO: do it properly
                    token->id = TokenId::ListEnd;
                }
                break;
                case TokenId::Space:
                case TokenId::LineFeed:
                {
                    tokens.index += 1;
                }
                break; default: BUSTER_UNREACHABLE(); 
            }
        }

        TokenGet result = {
            .token = tokens.pointer[tokens.index],
            .index = tokens.index,
        };

        return result;
    }

    TokenGet consume_if(TokenId id)
    {
        TokenGet result = {};
        let t = get();
        if (t.token.id == id)
        {
            tokens.index += 1;
            result = t;
        }
        else
        {
            BUSTER_TRAP();
        }

        return result;
    }

    void consume_and_append_if(TokenId id)
    {
        let t = get();
        if (t.token.id == id)
        {
            output.append(t);
            tokens.index += 1;
        }
        else
        {
            BUSTER_TRAP();
        }
    }
};

// TODO: more correct
BUSTER_GLOBAL_LOCAL void parse_attribute_list(Parser* restrict parser)
{
    if (parser->get().token.id == TokenId::ListStart)
    {
        while (true)
        {
            let t = parser->get();
            bool a = true;
            parser->tokens.index += 1;
            if (a) BUSTER_TRAP();

            if (t.token.id == TokenId::ListEnd)
            {
                break;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void parse_argument_declaration_list(Parser* restrict parser)
{
    parser->consume_and_append_if(TokenId::LeftParenthesis);

    while (true)
    {
        let t = parser->get();
        bool a = true;
        if (a) BUSTER_TRAP();

        if (t.token.id == TokenId::RightParenthesis)
        {
            break;
        }
    }
}

// TODO:
BUSTER_GLOBAL_LOCAL void parse_type(Parser* restrict parser)
{
    BUSTER_UNUSED(parser);
    bool a = true;
    if (a) BUSTER_TRAP();
    // parser->expect(TokenId::Identifier);
}

ENUM_T(Precedence, u8,
      None,
      Assignment,
      BooleanOr,
      BooleanAnd,
      Comparison,
      Bitwise,
      Shifting,
      Add,
      Div,
      Prefix,
      AggregateInitialization,
      Postfix);

STRUCT(ExpressionParser)
{
    TokenGet token;
    Precedence precedence;
    u8 reserved[3];
};

// BUSTER_GLOBAL_LOCAL void parse_left(Parser* restrict parser)
// {
// }

BUSTER_GLOBAL_LOCAL void parse_expression(Parser* restrict parser, ExpressionParser expression_parser)
{
    bool a = true;

    BUSTER_UNUSED(expression_parser);

    let t = parser->get();
    switch (t.token.id)
    {
        break; case TokenId::Number:
        {
            if (a) BUSTER_TRAP();
            // parser->expect(t.token.id);
        }
        break; default: if (a) BUSTER_UNREACHABLE();
    }
}

BUSTER_GLOBAL_LOCAL void parse_variable_declaration(Parser* restrict parser)
{
    BUSTER_UNUSED(parser);
    let let_t = parser->consume_if(TokenId::Keyword_Let);
    let identifier_t = parser->consume_if(TokenId::Identifier);
    let colon_t = parser->consume_if(TokenId::Colon);
    BUSTER_UNUSED(let_t);
    BUSTER_UNUSED(identifier_t);
    BUSTER_UNUSED(colon_t);

    bool parse_t = parser->get().token.id != TokenId::Equal;
    if (parse_t)
    {
        parse_type(parser);
    }

    let equal_t = parser->consume_if(TokenId::Equal);
    BUSTER_UNUSED(equal_t);

    parse_expression(parser, {});
}

BUSTER_GLOBAL_LOCAL void parse_return(Parser* restrict parser)
{
    parser->consume_and_append_if(TokenId::Keyword_Return);

    parse_expression(parser, {});
}

BUSTER_GLOBAL_LOCAL void parse_block(Parser* restrict parser, TokenId start_token, TokenId end_token);

BUSTER_GLOBAL_LOCAL void parse_function(Parser* restrict parser)
{
    parser->tokens.index += 1;

    let state = parser->tokens.state.top();
    state.is_function = true;
    parser->tokens.state.push(state);
    defer { parser->tokens.state.pop(); };

    // Function attribute list
    parse_attribute_list(parser);

    // Function name
    let identifier_t = parser->consume_if(TokenId::Identifier);

    // Symbol attribute list
    parse_attribute_list(parser);

    parse_argument_declaration_list(parser);

    // Return type
    parse_type(parser);

    parse_block(parser, TokenId::LeftBrace, TokenId::RightBrace);
}

BUSTER_GLOBAL_LOCAL void parse_block(Parser* restrict parser, TokenId start_token, TokenId end_token)
{
    parser->expect(start_token);

    while (parser->get().token.id != end_token)
    {
        parser->expect(TokenId::Semicolon);

        let t = parser->get();

        switch (t.token.id)
        {
            break; case TokenId::Keyword_Let: parse_variable_declaration(parser);
            break; case TokenId::Keyword_Return: parse_return(parser);
            break; case TokenId::Keyword_Function: parse_function(parser);
            break; default: BUSTER_UNREACHABLE();
        }
    }

    parser->expect(end_token);
}

BUSTER_GLOBAL_LOCAL void parse_statements(Parser* restrict parser)
{
    while (parser->get().token.id != TokenId::EOF)
    {
        parser->expect(TokenId::Semicolon);

        let t = parser->get();

        bool a = true;
        switch (t.token.id)
        {
            break; case TokenId::Keyword_Function: parse_function(parser);
            break; default: if (a) BUSTER_UNREACHABLE();
        }

        if (a) BUSTER_TRAP();
        break;
    }
}

BUSTER_GLOBAL_LOCAL ParserResult parser_consume(Parser* restrict parser)
{
    ParserResult result = {};
    let tokens = arena_get_slice(parser->output.token_arena, Token, arena_minimum_position, parser->output.token_arena->position);
    result.pointer = tokens.pointer;
    result.count = (u32)tokens.length;
    return result;
}

BUSTER_GLOBAL_LOCAL ParserResult parse(TokenizerResult tokenizer)
{
    Parser parser = {
        .tokens = {
            .pointer = tokenizer.tokens.pointer,
            .count = (u32)tokenizer.tokens.length,
            .state = {
                .arena = arena_create({}),
            },
        },
        .output = {
            .token_arena = arena_create({}),
            .index_arena = arena_create({}),
        },
    };

    parse_block(&parser, TokenId::SOF, TokenId::EOF);

    let result = parser_consume(&parser);

    return result;
}

BUSTER_F_DECL void parser_experiments()
{
    let arena = arena_create((ArenaCreation){});
    String8 source = 
#if 0
        S8(
            "; fn[cc(c)] main [export] (argument_count: u32, argv: &&u8, envp: &&u8) s32\n"
            "{\n"
            "    ; let a: s32 = 0\n"
            "    ; let b: s32 = 0\n"
            "    ; if (a > 0)\n"
            "    {\n"
            "        ; a = b\n"
            "    }\n"
            "    else\n"
            "    {\n"
            "        ; b = a\n"
            "    }\n"
            "    ; return a + b\n"
            "    ; return a;\n"
            "}\n"
            );
#else
        S8(
            "; fn[cc(c)] main [export] (argument_count: u32, argv: &&u8, envp: &&u8) s32\n"
            "{\n"
            "    ; return 0\n"
            "}\n"
            );
        // postfix attribute
        // fn
        //
#endif
    // [ .. ] fn  [ .. ] main 
    let tokenizer = tokenize(arena, source.pointer, source.length);
    parse(tokenizer);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    BUSTER_UNUSED(arguments);
    return result;
}
#endif
