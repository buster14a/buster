#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/arguments.h>
#include <buster/compiler/ir/ir.h>

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)TokenId::Keyword_Last - (u64)TokenId::Keyword_First - 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [-1 + (u64)TokenId::Keyword_Function - (u64)TokenId::Keyword_First] = S8("fn"),
    [-1 + (u64)TokenId::Keyword_If - (u64)TokenId::Keyword_First] = S8("if"),
    [-1 + (u64)TokenId::Keyword_Else - (u64)TokenId::Keyword_First] = S8("else"),
    [-1 + (u64)TokenId::Keyword_Return - (u64)TokenId::Keyword_First] = S8("return"),
    [-1 + (u64)TokenId::Keyword_Let - (u64)TokenId::Keyword_First] = S8("let"),
    [-1 + (u64)TokenId::Keyword_For - (u64)TokenId::Keyword_First] = S8("for"),
    [-1 + (u64)TokenId::Keyword_While - (u64)TokenId::Keyword_First] = S8("while"),
};
static_assert(BUSTER_ARRAY_LENGTH(keyword_names) == keyword_count);

static_assert(sizeof(Token) == 2);

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

BUSTER_F_IMPL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
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

        u32 line_count = 0;

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            let start = it;
            let ch = *start;

            if (ch == '\n')
            {
                let line_offset = (u32)(it - file_pointer) + 1;
                let line_index = line_count++;

                let line_offset_token_count = ((u32)(line_offset > UINT8_MAX) << 1) + 1;
                let line_index_token_count = ((u32)(line_offset > UINT8_MAX) << 1) + 1;

                let line_offset_token_index = (u32)token_count;
                let line_index_token_index = (u32)(line_offset_token_index + line_offset_token_count);

                token_count = line_offset_token_index + line_offset_token_count + line_index_token_count;

                u32 values[] = { line_offset, line_index };
                u32 indices[] = { line_offset_token_index, line_index_token_index };
                TokenId ids[] = { TokenId::LineOffset, TokenId::LineIndex };

                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
                {
                    let token = &tokens[indices[i]];
                    let value = values[i];
                    *token = {
                        .id = ids[i],
                        .length = value > UINT8_MAX ? (u8)0 : (u8)value,
                    };

                    // We write here any way because why not
                    *(u32*)(token + 1) = value;
                }
            }

            {
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
                    break; case '+':
                    {
                        if (it + 1 < top && it[1] == '=')
                        {
                            id = TokenId::PlusEqual;
                            it += 2;
                        }
                        else
                        {
                            id = TokenId::Plus;
                            it += 1;
                        }
                    }
                    break; case '-': { id = TokenId::Minus; it += 1; }
                    break; case '*': { id = TokenId::Asterisk; it += 1; }
                    break; case '/': { id = TokenId::Slash; it += 1; }
                    break; case '=': { id = TokenId::Equal; it += 1; }
                    break; case ':': { id = TokenId::Colon; it += 1; }
                    break; case ';': { id = TokenId::Semicolon; it += 1; }
                    break; case ',': { id = TokenId::Comma; it += 1; }
                    break; case '&': { id = TokenId::Ampersand; it += 1; }
                    break; case '.':
                    {
                        if (it + 2 < top && it[1] == '.' && it[2] == '.')
                        {
                            id = TokenId::TripleDot;
                        }
                        else if (it + 1 < top && it[1] == '.')
                        {
                            id = TokenId::DoubleDot;
                        }
                        else
                        {
                            id = TokenId::Dot;
                        }

                        it += (u64)id - (u64)TokenId::Dot + 1;
                    }
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

ENUM_T(ParserDeclaration, u8,
        None,
        Function,
        Type);

ENUM(FunctionState, u8,
    FunctionAttributeListParse,
    NameParse,
    SymbolAttributeListParse,
    ArgumentListParse,
    ReturnTypeParse);
STRUCT(ParserState)
{
    struct
    {
        FunctionState current_state;
        FLAG_ARRAY_U64(flags, FunctionState);
    } function;
    ParserDeclaration declaration_id;
};

STRUCT(ExtendedToken)
{
    u32 column:24;
    TokenId id;
    u32 length;
    u32 line;
    u32 offset;
};

static_assert(sizeof(ExtendedToken) == 16);

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token,
        // These are pre-read
        TokenId token_id, u8 token_length)
{
    bool extended_length = token_length == 0;
    bool has_fake_length = token_id == TokenId::LineFeed || token_id == TokenId::SOF || token_id == TokenId::EOF;
    u32 length = has_fake_length ? 0 : (extended_length ? *(u32*)(token + 1) : token_length);
    return length;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token)
{
    return get_token_length(token, token->id, token->length);
}

STRUCT(StateStack)
{
    Arena* arena;

    BUSTER_INLINE bool is_empty()
    {
        return arena->position < (arena_minimum_position + sizeof(ParserState));
    }

    BUSTER_INLINE ParserState* top()
    {
        return arena_get_pointer_at_position(arena, ParserState, arena->position - sizeof(ParserState));
    }

    BUSTER_INLINE ParserState pop()
    {
        BUSTER_CHECK(!is_empty());
        ParserState old_top = *top();
        arena->position -= sizeof(ParserState);
        return old_top;
    }

    BUSTER_INLINE ParserState* push()
    {
        let state = arena_allocate(arena, ParserState, 1);
        *state = {};
        return state;
    }

};

STRUCT(Parser)
{
    StateStack state_stack;
    Slice<Token> tokens;
    u32 token_index;
    const char8* source;
    u32 line_index;
    u32 line_offset;
    u32 column_index;
};

BUSTER_GLOBAL_LOCAL void consume(Parser& restrict parser, Token* restrict token)
{
    // Line number and line offset are always 32-bit length
    let length = get_token_length(token);
    parser.line_index = token->id == TokenId::LineIndex ? length : parser.line_index;
    parser.line_offset = token->id == TokenId::LineOffset ? length : parser.line_offset;
    parser.column_index = parser.column_index + ((token->id == TokenId::LineIndex || token->id == TokenId::LineOffset) ? 0 : length);
    parser.token_index += 1 + ((u32)(token->length == 0) << 1);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek(Parser& restrict parser, bool consume_result = false)
{
    ExtendedToken result = {};
    bool is_noise = true;

    do
    {
        auto* restrict token = &parser.tokens[parser.token_index];
        let id = token->id;
        let token_length = token->length;
        result = {
            .id = id,
            .column = parser.column_index,
            .length = get_token_length(token),
            .line = parser.line_index,
            .offset = parser.line_offset + parser.column_index,
        };

        is_noise =
            id == TokenId::LineFeed ||
            id == TokenId::LineOffset ||
            id == TokenId::LineIndex ||
            id == TokenId::Tab ||
            id == TokenId::Space ||
            id == TokenId::Comment ||
            id == TokenId::CarriageReturn;

        if (is_noise || consume_result)
        {
            consume(parser, token);
        }
    } while (is_noise);

    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_and_consume(Parser& restrict parser)
{
    return peek(parser, true);
}

BUSTER_GLOBAL_LOCAL String8 get_string(Parser& parser, ExtendedToken token)
{
    String8 result = {};
    if (token.length)
    {
        result = { .pointer = (char8*)&parser.source[token.offset], .length = token.length };
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken expect(Parser& parser, TokenId id)
{
    let token = peek_and_consume(parser);

    if (token.id != id)
    {
        let string = get_string(parser, token);
        BUSTER_TRAP();
    }

    return token;
}

BUSTER_GLOBAL_LOCAL ParserState* state(Parser& parser)
{
    return parser.state_stack.top();
}

ENUM(IrFunctionAttribute,
    CallingConvention);

BUSTER_GLOBAL_LOCAL String8 function_attribute_names[] = {
    [(u64)IrFunctionAttribute::CallingConvention] = S8("cc"),
};

static_assert(BUSTER_ARRAY_LENGTH(function_attribute_names) == (u64)IrFunctionAttribute::Count);

BUSTER_GLOBAL_LOCAL String8 calling_convention_names[] = {
    [(u64)IrCallingConvention::C] = S8("c"),
    [(u64)IrCallingConvention::SystemV] = S8("systemv"),
    [(u64)IrCallingConvention::Win64] = S8("win64"),
};

static_assert(BUSTER_ARRAY_LENGTH(calling_convention_names) == (u64)IrCallingConvention::Count);

ENUM_T(IrSymbolAttribute, u8,
      Export);

BUSTER_GLOBAL_LOCAL String8 symbol_attribute_names[] = {
    [(u64)IrSymbolAttribute::Export] = S8("export"),
};

static_assert(BUSTER_ARRAY_LENGTH(symbol_attribute_names) == (u64)IrSymbolAttribute::Count);

BUSTER_GLOBAL_LOCAL void parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {};
    parser.token_index += 1;
    parser.tokens = tokenizer.tokens;
    parser.source = source;
    parser.state_stack.arena = arena_create({});

    BUSTER_CHECK(parser.tokens[0].id == TokenId::SOF);

    // Push a dummy state so the stack is never empty
    parser.state_stack.push();

    while (true)
    {
        switch (state(parser)->declaration_id)
        {
            break; case ParserDeclaration::None:
            {
                auto token = peek_and_consume(parser);

                switch (token.id)
                {
                    break; case TokenId::Keyword_Function:
                    {
                        let function_state = parser.state_stack.push();
                        function_state->declaration_id = ParserDeclaration::Function;
                    }
                    break; default: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::Function:
            {
                auto token = peek_and_consume(parser);
                let initial_state = state(parser);

                let has_parsed_return_type = flag_get(initial_state->function.flags, FunctionState::ReturnTypeParse);
                let has_parsed_argument_list = flag_get(initial_state->function.flags, FunctionState::ArgumentListParse);
                let has_parsed_symbol_attributes = flag_get(initial_state->function.flags, FunctionState::SymbolAttributeListParse);
                let has_parsed_function_name = flag_get(initial_state->function.flags, FunctionState::NameParse);
                let has_parsed_function_attributes = flag_get(initial_state->function.flags, FunctionState::FunctionAttributeListParse);

                switch (token.id)
                {
                    break; case TokenId::LeftBracket:
                    {
                        bool error =
                            has_parsed_return_type ||
                            has_parsed_argument_list ||
                            has_parsed_symbol_attributes ||
                            (has_parsed_function_name && has_parsed_function_attributes && has_parsed_symbol_attributes);

                        if (error)
                        {
                            BUSTER_TRAP();
                        }

                        if (has_parsed_function_name)
                        {
                            if (has_parsed_symbol_attributes)
                            {
                                BUSTER_UNREACHABLE();
                            }

                            // parse symbol attributes

                            IrSymbolAttributes result = {};
                            state(parser)->function.current_state = FunctionState::SymbolAttributeListParse;

                            while (true)
                            {
                                let symbol_attribute_name_token = peek_and_consume(parser);
                                if (symbol_attribute_name_token.id == TokenId::RightBracket)
                                {
                                    break;
                                }

                                switch (symbol_attribute_name_token.id)
                                {
                                    break; case TokenId::Identifier:
                                    {
                                        let symbol_attribute_name = get_string(parser, symbol_attribute_name_token);

                                        let symbol_attribute = IrSymbolAttribute::Count;

                                        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(symbol_attribute_names); i += 1)
                                        {
                                            if (string8_equal(symbol_attribute_name, symbol_attribute_names[i]))
                                            {
                                                symbol_attribute = (IrSymbolAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (symbol_attribute)
                                        {
                                            break; case IrSymbolAttribute::Export:
                                            {
                                                result.linkage = IrLinkage::External;
                                                result.exported = true;
                                            }
                                            break; break; case IrSymbolAttribute::Count: BUSTER_TRAP();
                                        }
                                    }
                                    break; default: BUSTER_TRAP();
                                }
                            }
                        }
                        else
                        {
                            if (has_parsed_function_attributes)
                            {
                                BUSTER_UNREACHABLE();
                            }

                            // parse function attributes
                            IrFunctionAttributes result = {};
                            state(parser)->function.current_state = FunctionState::FunctionAttributeListParse;

                            while (true)
                            {
                                let attribute_name_token = peek_and_consume(parser);
                                if (attribute_name_token.id == TokenId::RightBracket)
                                {
                                    break;
                                }

                                switch (attribute_name_token.id)
                                {
                                    break; case TokenId::Identifier:
                                    {
                                        let attribute_name_candidate = get_string(parser, attribute_name_token);

                                        let attribute = IrFunctionAttribute::Count;

                                        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(function_attribute_names); i += 1)
                                        {
                                            if (string8_equal(attribute_name_candidate, function_attribute_names[i]))
                                            {
                                                attribute = (IrFunctionAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (attribute)
                                        {
                                            break; case IrFunctionAttribute::CallingConvention:
                                            {
                                                expect(parser, TokenId::LeftParenthesis);
                                                let calling_convention_name_candidate_token = expect(parser, TokenId::Identifier);
                                                expect(parser, TokenId::RightParenthesis);

                                                let calling_convention_name_candidate = get_string(parser, calling_convention_name_candidate_token);

                                                let calling_convention = IrCallingConvention::Count;

                                                for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(calling_convention_names); i += 1)
                                                {
                                                    if (string8_equal(calling_convention_name_candidate, calling_convention_names[i]))
                                                    {
                                                        calling_convention = (IrCallingConvention)i;
                                                        break;
                                                    }
                                                }

                                                if (calling_convention == IrCallingConvention::Count)
                                                {
                                                    BUSTER_TRAP();
                                                }

                                                result.calling_convention = calling_convention;
                                            }
                                            break; case IrFunctionAttribute::Count: BUSTER_TRAP();
                                        }
                                    }
                                    break; default: BUSTER_TRAP();
                                }
                            }
                        }
                    }
                    break; case TokenId::Identifier:
                    {
                        if (!has_parsed_return_type && !has_parsed_argument_list && !has_parsed_symbol_attributes && !has_parsed_function_name)
                        {
                            state(parser)->function.current_state = FunctionState::NameParse;
                            let function_name = get_string(parser, token);
                        }
                        else
                        {
                            BUSTER_TRAP();
                        }
                    }
                    break; case TokenId::LeftParenthesis:
                    {
                        if (!has_parsed_function_name || has_parsed_return_type || has_parsed_argument_list)
                        {
                            BUSTER_TRAP();
                        }

                        state(parser)->function.current_state = FunctionState::ArgumentListParse;

                        while (true)
                        {
                            let argument_name_token = peek_and_consume(parser);
                            if (argument_name_token.id == TokenId::RightParenthesis)
                            {
                                break;
                            }

                            if (argument_name_token.id == TokenId::Identifier)
                            {
                                let argument_name = get_string(parser, argument_name_token);
                                expect(parser, TokenId::Colon);

                                let type_state = parser.state_stack.push();
                                type_state->declaration_id = ParserDeclaration::Type;
                            }
                            else
                            {
                                BUSTER_TRAP();
                            }
                        }

                        BUSTER_TRAP();
                    }
                    break; default: BUSTER_UNREACHABLE();
                }

                if (initial_state == state(parser))
                {
                    flag_set(state(parser)->function.flags, state(parser)->function.current_state, true);
                }
            }
            break; case ParserDeclaration::Type:
            {
                BUSTER_TRAP();
            }
            break; case ParserDeclaration::Count: BUSTER_UNREACHABLE();
        }
    }

    // return result;
}

BUSTER_GLOBAL_LOCAL void parse_experiment(Arena* arena, StringOs path)
{
    let position = arena->position;
    defer { arena->position = position; };

    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, path, {}));

    let tokenizer = tokenize(arena, source.pointer, source.length);
    parse(source.pointer, tokenizer);

    // string8_print(S8("=== Input ===\n{S8}\n"), source);
    // string8_print(S8("=== Error Count ===\n{u64}\n"), result.error_count);
    // string8_print(S8("=== Prefix Output (token reordering) ===\n"));
    // print_prefix(result, tokenizer.tokens.pointer, source.pointer);
}

BUSTER_F_IMPL void parser_experiments()
{
    let arena = arena_create({});
    parse_experiment(arena, SOs("tests/basic.bbb"));
    parse_experiment(arena, SOs("tests/if_else.bbb"));
    parse_experiment(arena, SOs("tests/array_slices.bbb"));
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    consume_unit_tests(&result, parser_statement_semicolon_tests(arguments));
    consume_unit_tests(&result, parser_top_level_declaration_tests(arguments));
    consume_unit_tests(&result, parser_multiline_prefix_tests(arguments));
    return result;
}
#endif
