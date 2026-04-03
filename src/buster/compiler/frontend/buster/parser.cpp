#pragma once
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/string.h>
#include <buster/file.h>
#include <buster/arguments.h>
#include <buster/compiler/ir/ir.h>

BUSTER_GLOBAL_LOCAL constexpr let pointer_token = TokenId::Ampersand;
BUSTER_GLOBAL_LOCAL constexpr let array_slice_token_start = TokenId::LeftBracket;
BUSTER_GLOBAL_LOCAL constexpr let array_slice_token_end = (TokenId)((u64)array_slice_token_start + 1);

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)TokenId::Keyword_Last - (u64)TokenId::Keyword_First - 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [-1 + (u64)TokenId::Keyword_Function - (u64)TokenId::Keyword_First] = S8("fn"),
    [-1 + (u64)TokenId::Keyword_If - (u64)TokenId::Keyword_First] = S8("if"),
    [-1 + (u64)TokenId::Keyword_Else - (u64)TokenId::Keyword_First] = S8("else"),
    [-1 + (u64)TokenId::Keyword_Return - (u64)TokenId::Keyword_First] = S8("return"),
    [-1 + (u64)TokenId::Keyword_For - (u64)TokenId::Keyword_First] = S8("for"),
    [-1 + (u64)TokenId::Keyword_While - (u64)TokenId::Keyword_First] = S8("while"),
    [-1 + (u64)TokenId::Keyword_Code - (u64)TokenId::Keyword_First] = S8("code"),
    [-1 + (u64)TokenId::Keyword_Data - (u64)TokenId::Keyword_First] = S8("data"),
    [-1 + (u64)TokenId::Keyword_Type - (u64)TokenId::Keyword_First] = S8("type"),
    [-1 + (u64)TokenId::Keyword_Struct - (u64)TokenId::Keyword_First] = S8("struct"),
    [-1 + (u64)TokenId::Keyword_Union - (u64)TokenId::Keyword_First] = S8("union"),
};

static_assert(BUSTER_ARRAY_LENGTH(keyword_names) == keyword_count);

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
        let token_start = arena_allocate(arena, Token, file_length + 1);
        let tokens = token_start;
        u64 token_count = 0;

        let it = file_pointer;
        let top = file_pointer + file_length;

        u32 line_count = 0;

        while (true)
        {
            if (it >= top)
            {
                break;
            }

            let start = it;
            let ch = *start;

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
                            id = identifier.length == 1 && identifier[0] == '_' ? TokenId::Underscore : TokenId::Identifier;
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
                let length = (u64)(end - start);
                if (length > (((u64)1 << 24) - 1))
                {
                    BUSTER_TRAP(); // TODO: error
                }


                let token_index = token_count;

                tokens[token_index] = { 
                    .length = (u32)length,
                    .id = id,
                };

                token_count = token_index + 1;
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
        Code,
        TypeReference,
        AttributeList,
        Block,
        TypeDeclaration,
        DataDeclaration);

ENUM_T(CodeState, u8,
    BeforeName,
    AfterName,
    Type,
    AfterType,
    AfterEqual,
    Body);

ENUM_T(TypeState, u8,
    PrefixOrBase,
    AfterArraySliceStart,
    AfterArrayCount,
    AfterArrayInferMarker,
    AfterFunctionKeyword,
    FunctionArgumentNameOrClose,
    FunctionArgumentAfterNameSegment,
    FunctionArgumentAfterColon,
    FunctionArgumentType,
    FunctionArgumentDelimiterOrClose,
    FunctionReturnType,
    AfterFunctionReturnType);

ENUM_T(AttributeListKind, u8,
    Code,
    Data,
    Symbol,
    Function);
// TODO add more

ENUM_T(AttributeListState, u8,
    ItemOrClose,
    CallingConventionOpen,
    CallingConventionName,
    CallingConventionClose);

STRUCT(ParserState)
{
    ParserDeclaration id;
    union
    {
        struct
        {
            CodeState current_state;
            String8 name;
        } code;

        struct
        {
            TypeState current_state;
        } type;

        struct
        {
            union
            {
                IrSymbolAttributes symbol;
                IrFunctionAttributes function;
            };
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

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 get_token_length(Token* restrict token)
{
    bool has_fake_length = token->id == TokenId::EOF;
    u32 length = has_fake_length ? 0 : token->length;
    return length;
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
    let length = get_token_length(token);
    parser.line_index += token->id == TokenId::LineFeed;
    parser.line_offset = token->id == TokenId::LineFeed ? parser.column_index : parser.line_offset;
    parser.column_index = token->id == TokenId::LineFeed ? 0 : length + parser.column_index;
    parser.token_index += 1;
}

BUSTER_GLOBAL_LOCAL void consume(Parser& restrict parser)
{
    consume(parser, &parser.tokens[parser.token_index]);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_extended(Parser& restrict parser, bool consume_result)
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

BUSTER_GLOBAL_LOCAL ExtendedToken peek(Parser& restrict parser)
{
    return peek_extended(parser, false);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_and_consume(Parser& restrict parser)
{
    return peek_extended(parser, true);
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

BUSTER_GLOBAL_LOCAL bool token_begins_type(TokenId id)
{
    bool result =
        id == TokenId::Identifier ||
        id == pointer_token ||
        id == array_slice_token_start ||
        id == TokenId::Keyword_Function;
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_ahead(Parser& parser, u32 count)
{
    Parser copy = parser;
    ExtendedToken result = {};

    for (u32 i = 0; i <= count; i += 1)
    {
        result = peek_and_consume(copy);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void finish_type_reference(Parser& parser)
{
    parser.state_stack.pop();

    let resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case ParserDeclaration::Code:
        {
            if (resume_state->code.current_state != CodeState::Type)
            {
                BUSTER_TRAP();
            }

            resume_state->code.current_state = CodeState::AfterType;
        }
        break; case ParserDeclaration::TypeReference:
        {
            switch (resume_state->type.current_state)
            {
                break; case TypeState::FunctionArgumentType:
                {
                    resume_state->type.current_state = TypeState::FunctionArgumentDelimiterOrClose;
                }
                break; case TypeState::FunctionReturnType:
                {
                    resume_state->type.current_state = TypeState::AfterFunctionReturnType;
                }
                break; default: BUSTER_TRAP();
            }
        }
        break; default:
        {
        }
    }
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

STRUCT(AstFunctionDeclaration)
{
};

STRUCT(AstFunctionDefinition)
{
};

STRUCT(AstBlock)
{
};

ENUM_T(AstNodeId, u8,
    FunctionDefinition,
    FunctionDeclaration,
    Block);

STRUCT(AstNode)
{
    union
    {
        AstFunctionDefinition function_definition;
        AstFunctionDeclaration function_declaration;
        AstBlock block;
    };

    AstNodeId id;
};

BUSTER_GLOBAL_LOCAL void parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {};
    parser.tokens = tokenizer.tokens;
    parser.source = source;
    parser.state_stack.arena = arena_create({});

    // Push a dummy state so the stack is never empty
    parser.state_stack.push();

    bool is_running = true;

    while (is_running)
    {
        switch (state(parser)->id)
        {
            break; case ParserDeclaration::Count: BUSTER_UNREACHABLE();
            break; case ParserDeclaration::None:
            {
                let token = peek_and_consume(parser);

                switch (token.id)
                {
                    break; case TokenId::Keyword_Code:
                    {
                        let function_state = parser.state_stack.push();
                        function_state->id = ParserDeclaration::Code;
                        function_state->code.current_state = CodeState::BeforeName;
                    }
                    break; case TokenId::EOF:
                    {
                        is_running = false;
                    }
                    break; default: BUSTER_TRAP();
                }
            }
            break; case ParserDeclaration::Code:
            {
                let code_state = state(parser);
                let token = peek(parser);

                switch (code_state->code.current_state)
                {
                    break; case CodeState::BeforeName:
                    {
                        consume(parser);

                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Code;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; case TokenId::Identifier:
                            {
                                code_state->code.name = get_string(parser, token);
                                code_state->code.current_state = CodeState::AfterName;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case CodeState::AfterName:
                    {
                        consume(parser);

                        switch (token.id)
                        {
                            break; case TokenId::Colon:
                            {
                                code_state->code.current_state = CodeState::Type;

                                let type_state = parser.state_stack.push();
                                type_state->id = ParserDeclaration::TypeReference;
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            break; case TokenId::Equal:
                            {
                                BUSTER_TRAP();
                            }
                            break; case TokenId::LeftBracket:
                            {
                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Symbol;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case CodeState::Type:
                    {
                        BUSTER_UNREACHABLE();
                    }
                    break; case CodeState::AfterType:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::LeftBrace:
                            {
                                consume(parser);

                                code_state->code.current_state = CodeState::Body;

                                let block_state = parser.state_stack.push();
                                block_state->id = ParserDeclaration::Block;
                                block_state->block.brace_depth = 1;
                            }
                            break; case TokenId::Semicolon:
                            {
                                consume(parser);
                                parser.state_stack.pop();
                            }
                            break; case TokenId::Equal:
                            {
                                consume(parser);
                                code_state->code.current_state = CodeState::AfterEqual;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case CodeState::AfterEqual:
                    {
                        if (token.id != TokenId::LeftBrace)
                        {
                            BUSTER_TRAP();
                        }

                        consume(parser);

                        code_state->code.current_state = CodeState::Body;

                        let block_state = parser.state_stack.push();
                        block_state->id = ParserDeclaration::Block;
                        block_state->block.brace_depth = 1;
                    }
                    break; case CodeState::Body:
                    {
                        parser.state_stack.pop();
                    }
                    break; case CodeState::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::TypeReference:
            {
                let type_state = state(parser);
                let token = peek(parser);
                
                switch (type_state->type.current_state)
                {
                    break; case TypeState::PrefixOrBase:
                    {
                        switch (token.id)
                        {
                            break; case pointer_token:
                            {
                                consume(parser);
                            }
                            break; case array_slice_token_start:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::AfterArraySliceStart;
                            }
                            break; case TokenId::Identifier:
                            {
                                consume(parser);
                                finish_type_reference(parser);
                            }
                            break; case TokenId::Keyword_Function:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::AfterFunctionKeyword;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::AfterArraySliceStart:
                    {
                        switch (token.id)
                        {
                            break; case array_slice_token_end:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            break; case TokenId::Number:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::AfterArrayCount;
                            }
                            break; case TokenId::Underscore:
                            {
                                consume(parser);
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

                        consume(parser);
                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterArrayInferMarker:
                    {
                        if (token.id != TokenId::RightBracket)
                        {
                            BUSTER_TRAP();
                        }

                        consume(parser);
                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterFunctionKeyword:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                consume(parser);

                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Function;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; case TokenId::LeftParenthesis:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionArgumentNameOrClose;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::FunctionArgumentNameOrClose:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::RightParenthesis:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionReturnType;
                            }
                            break; case TokenId::Identifier:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionArgumentAfterNameSegment;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::FunctionArgumentAfterNameSegment:
                    {
                        if (token.id != TokenId::Colon)
                        {
                            BUSTER_TRAP();
                        }

                        consume(parser);
                        type_state->type.current_state = TypeState::FunctionArgumentAfterColon;
                    }
                    break; case TypeState::FunctionArgumentAfterColon:
                    {
                        if (token.id == TokenId::Identifier)
                        {
                            let next = peek_ahead(parser, 1);
                            if (next.id == TokenId::Colon)
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionArgumentAfterNameSegment;
                                break;
                            }
                        }

                        if (!token_begins_type(token.id))
                        {
                            BUSTER_TRAP();
                        }

                        type_state->type.current_state = TypeState::FunctionArgumentType;

                        let child_type_state = parser.state_stack.push();
                        child_type_state->id = ParserDeclaration::TypeReference;
                        child_type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::FunctionArgumentType:
                    {
                        BUSTER_TRAP();
                    }
                    break; case TypeState::FunctionArgumentDelimiterOrClose:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::Comma:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionArgumentNameOrClose;
                            }
                            break; case TokenId::RightParenthesis:
                            {
                                consume(parser);
                                type_state->type.current_state = TypeState::FunctionReturnType;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case TypeState::FunctionReturnType:
                    {
                        if (!token_begins_type(token.id))
                        {
                            BUSTER_TRAP();
                        }

                        let child_type_state = parser.state_stack.push();
                        child_type_state->id = ParserDeclaration::TypeReference;
                        child_type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterFunctionReturnType:
                    {
                        finish_type_reference(parser);
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
                        switch (token.id)
                        {
                            break; case TokenId::RightBracket:
                            {
                                parser.state_stack.pop();
                            }
                            break; case TokenId::Identifier:
                            {
                                let attribute_name = get_string(parser, token);

                                switch (attribute_list_state->attribute_list.kind)
                                {
                                    break; case AttributeListKind::Code:
                                    {
                                        BUSTER_TRAP();
                                    }
                                    break; case AttributeListKind::Data:
                                    {
                                        BUSTER_TRAP();
                                    }
                                    break; case AttributeListKind::Symbol:
                                    {
                                        let attribute = IrSymbolAttribute::Count;

                                        for (EACH_ARRAY_INDEX(i, symbol_attribute_names))
                                        {
                                            if (string8_equal(attribute_name, symbol_attribute_names[i]))
                                            {
                                                attribute = (IrSymbolAttribute)i;
                                                break;
                                            }
                                        }

                                        switch (attribute)
                                        {
                                            break; case IrSymbolAttribute::Export:
                                            {
                                                attribute_list_state->attribute_list.symbol.exported = true;
                                                attribute_list_state->attribute_list.symbol.linkage = IrLinkage::External;
                                            }
                                            break; case IrSymbolAttribute::Count: BUSTER_TRAP();
                                          break;
                                        }
                                    }
                                    break; case AttributeListKind::Function:
                                    {
                                        if (!string8_equal(attribute_name, function_attribute_names[(u64)IrFunctionAttribute::CallingConvention]))
                                        {
                                            BUSTER_TRAP();
                                        }

                                        attribute_list_state->attribute_list.current_state = AttributeListState::CallingConventionOpen;
                                    }
                                    break; case AttributeListKind::Count: BUSTER_UNREACHABLE();
                                }
                            }
                            break; default: BUSTER_TRAP();
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
            break; case ParserDeclaration::TypeDeclaration:
            {
                BUSTER_TRAP();
            }
            break; case ParserDeclaration::DataDeclaration:
            {
                BUSTER_TRAP();
            }
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
    parser_experiments();
    return result;
}
#endif
