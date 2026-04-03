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
        // Newlines can expand into line metadata plus the newline token itself.
        let token_start = arena_allocate(arena, Token, (file_length * 7) + 2); // SOF, EOF
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

                let line_offset_token_count = 3u;
                let line_index_token_count = 3u;

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
                        .length = 0,
                    };

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

                if (big_length)
                {
                    static_assert(sizeof(Token) * 2 == sizeof(u32));
                    *(u32*)(&tokens[token_index + 1]) = length;
                }

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
        Type,
        AttributeList,
        Block);

ENUM_T(FunctionState, u8,
    BeforeName,
    AfterName,
    ArgumentNameOrClose,
    ArgumentColon,
    ArgumentType,
    ArgumentDelimiterOrClose,
    ReturnType,
    Body);

ENUM_T(TypeState, u8,
    PrefixOrBase,
    AfterLeftBracket,
    AfterArrayCount,
    AfterArrayInferMarker);

ENUM_T(AttributeListKind, u8,
    Function,
    Symbol);

ENUM_T(AttributeListState, u8,
    ItemOrClose,
    CallingConventionOpen,
    CallingConventionName,
    CallingConventionClose);

STRUCT(ParserState)
{
    ParserDeclaration declaration_id;
    union
    {
        struct
        {
            FunctionState current_state;
        } function;

        struct
        {
            TypeState current_state;
        } type;

        struct
        {
            AttributeListKind kind;
            AttributeListState current_state;
        } attribute_list;

        struct
        {
            u32 brace_depth;
        } block;
    };
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
    parser.column_index = token->id == TokenId::LineFeed ? 0 : (parser.column_index + ((token->id == TokenId::LineIndex || token->id == TokenId::LineOffset) ? 0 : length));
    parser.token_index += 1 + ((u32)(token->length == 0) << 1);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek(Parser& restrict parser, bool consume_result = false)
{
    ExtendedToken result = {};
    bool is_noise = true;

    do
    {
        let restrict token = &parser.tokens[parser.token_index];
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

BUSTER_GLOBAL_LOCAL bool token_matches(Parser& parser, ExtendedToken token, String8 expected)
{
    bool result = false;
    if (token.id == TokenId::Identifier)
    {
        result = string8_equal(get_string(parser, token), expected);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool token_matches_any(Parser& parser, ExtendedToken token, String8* names, u64 name_count)
{
    bool result = false;
    if (token.id == TokenId::Identifier)
    {
        let candidate = get_string(parser, token);
        for (u64 i = 0; i < name_count; i += 1)
        {
            if (string8_equal(candidate, names[i]))
            {
                result = true;
                break;
            }
        }
    }
    return result;
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

    bool is_running = true;
    while (is_running)
    {
        switch (state(parser)->declaration_id)
        {
            break; case ParserDeclaration::None:
            {
                let token = peek_and_consume(parser);

                switch (token.id)
                {
                    break; case TokenId::Keyword_Function:
                    {
                        let function_state = parser.state_stack.push();
                        function_state->declaration_id = ParserDeclaration::Function;
                        function_state->function.current_state = FunctionState::BeforeName;
                    }
                    break; case TokenId::EOF:
                    {
                        is_running = false;
                    }
                    break; default: BUSTER_TRAP();
                }
            }
            break; case ParserDeclaration::Function:
            {
                let function_state = state(parser);
                let token = peek_and_consume(parser);

                switch (function_state->function.current_state)
                {
                    break; case FunctionState::BeforeName:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->declaration_id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Function;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; case TokenId::Identifier:
                            {
                                function_state->function.current_state = FunctionState::AfterName;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case FunctionState::AfterName:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->declaration_id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Symbol;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; case TokenId::LeftParenthesis:
                            {
                                function_state->function.current_state = FunctionState::ArgumentNameOrClose;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case FunctionState::ArgumentNameOrClose:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::RightParenthesis:
                            {
                                function_state->function.current_state = FunctionState::ReturnType;

                                let type_state = parser.state_stack.push();
                                type_state->declaration_id = ParserDeclaration::Type;
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            break; case TokenId::Identifier:
                            {
                                function_state->function.current_state = FunctionState::ArgumentColon;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case FunctionState::ArgumentColon:
                    {
                        if (token.id != TokenId::Colon)
                        {
                            BUSTER_TRAP();
                        }

                        function_state->function.current_state = FunctionState::ArgumentType;

                        let type_state = parser.state_stack.push();
                        type_state->declaration_id = ParserDeclaration::Type;
                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case FunctionState::ArgumentType:
                    {
                        BUSTER_TRAP();
                    }
                    break; case FunctionState::ArgumentDelimiterOrClose:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::Comma:
                            {
                                function_state->function.current_state = FunctionState::ArgumentNameOrClose;
                            }
                            break; case TokenId::RightParenthesis:
                            {
                                function_state->function.current_state = FunctionState::ReturnType;

                                let type_state = parser.state_stack.push();
                                type_state->declaration_id = ParserDeclaration::Type;
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case FunctionState::ReturnType:
                    {
                        BUSTER_TRAP();
                    }
                    break; case FunctionState::Body:
                    {
                        if (token.id != TokenId::LeftBrace)
                        {
                            BUSTER_TRAP();
                        }

                        // The block frame owns brace balancing for the whole body.
                        parser.state_stack.pop();

                        let block_state = parser.state_stack.push();
                        block_state->declaration_id = ParserDeclaration::Block;
                        block_state->block.brace_depth = 1;
                    }
                    break; case FunctionState::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::Type:
            {
                let type_state = state(parser);
                let token = peek_and_consume(parser);

                switch (type_state->type.current_state)
                {
                    break; case TypeState::PrefixOrBase:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::Ampersand:
                            {
                            }
                            break; case TokenId::LeftBracket:
                            {
                                type_state->type.current_state = TypeState::AfterLeftBracket;
                            }
                            break; case TokenId::Identifier:
                            {
                                parser.state_stack.pop();

                                let resume_state = state(parser);
                                if (resume_state->declaration_id == ParserDeclaration::Function)
                                {
                                    switch (resume_state->function.current_state)
                                    {
                                        break; case FunctionState::ArgumentType:
                                        {
                                            resume_state->function.current_state = FunctionState::ArgumentDelimiterOrClose;
                                        }
                                        break; case FunctionState::ReturnType:
                                        {
                                            resume_state->function.current_state = FunctionState::Body;
                                        }
                                        break; default:
                                        {
                                        }
                                    }
                                }
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::AfterLeftBracket:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::RightBracket:
                            {
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            break; case TokenId::Number:
                            {
                                type_state->type.current_state = TypeState::AfterArrayCount;
                            }
                            break; case TokenId::Identifier:
                            {
                                if (!token_matches(parser, token, S8("_")))
                                {
                                    BUSTER_TRAP();
                                }

                                type_state->type.current_state = TypeState::AfterArrayInferMarker;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::AfterArrayCount:
                    {
                        if (token.id != TokenId::RightBracket)
                        {
                            BUSTER_TRAP();
                        }

                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterArrayInferMarker:
                    {
                        if (token.id != TokenId::RightBracket)
                        {
                            BUSTER_TRAP();
                        }

                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::AttributeList:
            {
                let attribute_list_state = state(parser);
                let token = peek_and_consume(parser);

                switch (attribute_list_state->attribute_list.current_state)
                {
                    break; case AttributeListState::ItemOrClose:
                    {
                        if (token.id == TokenId::RightBracket)
                        {
                            parser.state_stack.pop();
                        }
                        else if (attribute_list_state->attribute_list.kind == AttributeListKind::Function)
                        {
                            if (!token_matches(parser, token, function_attribute_names[(u64)IrFunctionAttribute::CallingConvention]))
                            {
                                BUSTER_TRAP();
                            }

                            attribute_list_state->attribute_list.current_state = AttributeListState::CallingConventionOpen;
                        }
                        else if (attribute_list_state->attribute_list.kind == AttributeListKind::Symbol)
                        {
                            if (!token_matches_any(parser, token, symbol_attribute_names, BUSTER_ARRAY_LENGTH(symbol_attribute_names)))
                            {
                                BUSTER_TRAP();
                            }
                        }
                        else
                        {
                            BUSTER_UNREACHABLE();
                        }
                    }
                    break; case AttributeListState::CallingConventionOpen:
                    {
                        if (token.id != TokenId::LeftParenthesis)
                        {
                            BUSTER_TRAP();
                        }

                        attribute_list_state->attribute_list.current_state = AttributeListState::CallingConventionName;
                    }
                    break; case AttributeListState::CallingConventionName:
                    {
                        if (!token_matches_any(parser, token, calling_convention_names, BUSTER_ARRAY_LENGTH(calling_convention_names)))
                        {
                            BUSTER_TRAP();
                        }

                        attribute_list_state->attribute_list.current_state = AttributeListState::CallingConventionClose;
                    }
                    break; case AttributeListState::CallingConventionClose:
                    {
                        if (token.id != TokenId::RightParenthesis)
                        {
                            BUSTER_TRAP();
                        }

                        attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                    }
                    break; case AttributeListState::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::Block:
            {
                let block_state = state(parser);
                let token = peek_and_consume(parser);

                switch (token.id)
                {
                    break; case TokenId::LeftBrace:
                    {
                        block_state->block.brace_depth += 1;
                    }
                    break; case TokenId::RightBrace:
                    {
                        if (block_state->block.brace_depth == 0)
                        {
                            BUSTER_TRAP();
                        }

                        block_state->block.brace_depth -= 1;
                        if (block_state->block.brace_depth == 0)
                        {
                            parser.state_stack.pop();
                        }
                    }
                    break; case TokenId::EOF:
                    {
                        BUSTER_TRAP();
                    }
                    break; default:
                    {
                    }
                }
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
