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
BUSTER_GLOBAL_LOCAL constexpr let block_end_of_statement_token = TokenId::Semicolon;

BUSTER_GLOBAL_LOCAL constexpr u64 keyword_count = (u64)last_keyword - (u64)first_keyword + 1;

BUSTER_GLOBAL_LOCAL String8 keyword_names[] = {
    [(u64)TokenId::Keyword_Function - (u64)first_keyword] = S8("fn"),
    [(u64)TokenId::Keyword_If - (u64)first_keyword] = S8("if"),
    [(u64)TokenId::Keyword_Else - (u64)first_keyword] = S8("else"),
    [(u64)TokenId::Keyword_Return - (u64)first_keyword] = S8("return"),
    [(u64)TokenId::Keyword_For - (u64)first_keyword] = S8("for"),
    [(u64)TokenId::Keyword_While - (u64)first_keyword] = S8("while"),
    [(u64)TokenId::Keyword_Code - (u64)first_keyword] = S8("code"),
    [(u64)TokenId::Keyword_Data - (u64)first_keyword] = S8("data"),
    [(u64)TokenId::Keyword_Type - (u64)first_keyword] = S8("type"),
    [(u64)TokenId::Keyword_Struct - (u64)first_keyword] = S8("struct"),
    [(u64)TokenId::Keyword_Union - (u64)first_keyword] = S8("union"),
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

ENUM(Format,
        Hexadecimal,
        Decimal,
        Octal,
        Binary
    );

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
}

BUSTER_F_IMPL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length)
{
    TokenizerResult result = {};

    if (file_length)
    {
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
            let start_ch = *start;

            {
                TokenId id;

                switch (start_ch)
                {
                    break; SWITCH_ALPHA_UPPER: SWITCH_ALPHA_LOWER:
                    case '_':
                    {
                        while (true)
                        {
                            let it_start = it;
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
                            id = (TokenId)(i + (u64)first_keyword);
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
                    break; DECIMAL_DIGIT:
                    {
                        bool is_valid = true;

                        let format = Format::Decimal;

                        if (start_ch == '0')
                        {
                            let second_ch = *(it + 1);
                            switch (second_ch)
                            {
                                break; case 'x': it += 2; format = Format::Hexadecimal;
                                break; case 'o': it += 2; format = Format::Octal;
                                break; case 'b': it += 2; format = Format::Binary;
                                break; default: {}
                            }
                        }

                        bool increment;

                        do
                        {
                            let it_start = it;

                            let ch = *it_start;

                            switch (format)
                            {
                                break; case Format::Hexadecimal:
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
                                break; case Format::Decimal:
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
                                break; case Format::Octal:
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
                                break; case Format::Binary:
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
                                break; case Format::Count: BUSTER_UNREACHABLE();
                            }

                            it += increment;
                        } while (increment);

                        let maybe_float_separator = *it;
                        bool is_float = maybe_float_separator == '.';
                        is_valid = is_valid && is_valid_character_after_digit(maybe_float_separator);

                        if (is_float)
                        {
                            is_valid = is_valid && (format == Format::Decimal || format == Format::Hexadecimal);

                            it += 1;

                            do
                            {
                                let ch = *it;

                                switch (format)
                                {
                                    break; case Format::Hexadecimal:
                                    {
                                        switch (ch)
                                        {
                                            break; DECIMAL_DIGIT: HEX_ALPHA: case '_': increment = true;
                                            break; default: increment = false;
                                        }
                                    }
                                    break; case Format::Decimal:
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

                            let exponent_ch = *it;

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
                                        break; case Format::Hexadecimal:
                                        {
                                            id = TokenId::HexadecimalFloatLiteralExponent;
                                            is_valid = is_valid && (exponent_ch == 'P' || exponent_ch == 'p');
                                        }
                                        break; case Format::Decimal:
                                        {
                                            id = TokenId::DecimalFloatLiteralExponent;
                                            is_valid = is_valid && (exponent_ch == 'E' || exponent_ch == 'e');
                                        }
                                        break; default: BUSTER_UNREACHABLE();
                                    }

                                    it += 1;

                                    let exponent_sign = *it; // '+' or '-'
                                    is_valid = is_valid && (exponent_sign == '+' || exponent_sign == '-');
                                    it += 1;

                                    do
                                    {
                                        let it_start = it;

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
                                        break; case Format::Hexadecimal: id = TokenId::HexadecimalFloatLiteral;
                                        break; case Format::Decimal: id = TokenId::DecimalFloatLiteral;
                                        break; default: BUSTER_UNREACHABLE();
                                    }
                                }
                            }
                        }
                        else
                        {
                            switch (format)
                            {
                                break; case Format::Hexadecimal: id = TokenId::HexadecimalIntegerLiteral;
                                break; case Format::Decimal: id = TokenId::DecimalIntegerLiteral;
                                break; case Format::Octal: id = TokenId::OctalIntegerLiteral;
                                break; case Format::Binary: id = TokenId::BinaryIntegerLiteral;
                                break; case Format::Count: BUSTER_UNREACHABLE();
                            }
                        }

                        is_valid = is_valid && is_valid_character_after_digit(*it);

                        if (!is_valid)
                        {
                            BUSTER_TRAP();
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
            .length = 0,
        };

        result.tokens.pointer = token_start;
        result.tokens.length = token_count;
    }

    return result;
}

ENUM_T(ParserDeclaration, u8,
        Root,
        Code,
        TypeReference,
        AttributeList,
        Block,
        Statement,
        ReturnStatement,
        TypeDeclaration,
        DataDeclaration,
        Expression);

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

struct AstNode;

STRUCT(CodeAttributes)
{
};

ENUM_T(StatementStateId, u8,
        Start,
        End);

ENUM_T(ReturnStatementState, u8,
    ValueOrEnd,
    End);

ENUM_T(StatementId, u8,
      Return);

ENUM_T(ExpressionState, u8,
    Prefix,
    Tail);

STRUCT(ParserState)
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

STRUCT(TokenIterator)
{
    Slice<Token> tokens;
    const char8* source;
    u32 token_index;
    u32 line_index;
    u32 line_offset;
    u32 column_index;
};

BUSTER_GLOBAL_LOCAL TokenIterator token_initialize(Slice<Token> tokens, const char8* source)
{
    TokenIterator result = {
        .tokens = tokens,
        .source = source,
    };

    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken to_extended(TokenIterator& restrict iterator, Token token)
{
    ExtendedToken result = {
        .id = token.id,
        .column = iterator.column_index,
        .length = token.length,
        .line = iterator.line_index,
        .offset = iterator.line_offset + iterator.column_index,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL ExtendedToken token_get(TokenIterator& restrict iterator)
{
    auto& restrict token = iterator.tokens[iterator.token_index];
    let result = to_extended(iterator, token);
    return result;
}

STRUCT(Parser)
{
    TokenIterator iterator;
    StateStack state_stack;
    Arena* restrict node_arena;
};

BUSTER_GLOBAL_LOCAL void consume(TokenIterator& restrict iterator, Token& restrict token)
{
    iterator.line_index += token.id == TokenId::LineFeed;
    iterator.line_offset = token.id == TokenId::LineFeed ? iterator.line_offset + iterator.column_index + 1 : iterator.line_offset;
    iterator.column_index = token.id == TokenId::LineFeed ? 0 : token.length + iterator.column_index;
    iterator.token_index += 1;
}

BUSTER_GLOBAL_LOCAL void consume(TokenIterator& restrict iterator)
{
    consume(iterator, iterator.tokens[iterator.token_index]);
}

BUSTER_GLOBAL_LOCAL ExtendedToken peek_extended(Parser& restrict parser, bool consume_result)
{
    ExtendedToken result;
    bool is_noise = true;

    do
    {
        result = token_get(parser.iterator);
        let id = result.id;
        is_noise =
            id == TokenId::LineFeed ||
            id == TokenId::Tab ||
            id == TokenId::Space ||
            id == TokenId::Comment ||
            id == TokenId::CarriageReturn;

        if (is_noise || consume_result)
        {
            consume(parser.iterator);
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

BUSTER_GLOBAL_LOCAL ExtendedToken expect(Parser& parser, TokenId id)
{
    let token = peek_and_consume(parser);

    if (token.id != id)
    {
        let string = get_string(parser.iterator.source, token);
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
        result = string8_equal(get_string(parser.iterator.source, token), expected);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool token_matches_any(Parser& parser, ExtendedToken token, String8* names, u64 name_count)
{
    bool result = false;
    if (token.id == TokenId::Identifier)
    {
        let candidate = get_string(parser.iterator.source, token);
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
    Block,
    ConstantInteger);

ENUM_T(CodeAttributeId, u8,
        Inline);

BUSTER_GLOBAL_LOCAL String8 code_attributes_names[] = {
    [(u64)CodeAttributeId::Inline] = S8("inline"),
};

STRUCT(AstCode)
{
    CodeAttributes code_attributes;
    IrSymbolAttributes symbol_attributes;
    String8 name;
};

STRUCT(AstNode)
{
    union
    {
        AstCode code;
        AstBlock block;
        u64 constant_integer;
    };

    AstNodeId id;
};

BUSTER_GLOBAL_LOCAL AstNode* allocate_node(Parser& restrict parser)
{
    let node = arena_allocate(parser.node_arena, AstNode, 1);
    *node = {};
    return node;
}

BUSTER_GLOBAL_LOCAL void parse_block(Parser& restrict parser)
{
    let block_state = parser.state_stack.push();
    block_state->id = ParserDeclaration::Block;
    block_state->block.brace_depth = 1;
    let node = allocate_node(parser);
    node->id = AstNodeId::Block;
    block_state->block.node = node;
}

BUSTER_GLOBAL_LOCAL void parse_expression(Parser& restrict parser, TokenId end_of_statement_token)
{
    let state = parser.state_stack.push();
    state->id = ParserDeclaration::Expression;
    state->expression.state = ExpressionState::Prefix;
    state->expression.end_token = end_of_statement_token;
}

BUSTER_GLOBAL_LOCAL void finish_expression(Parser& restrict parser)
{
    parser.state_stack.pop();

    let resume_state = state(parser);

    switch (resume_state->id)
    {
        break; case ParserDeclaration::ReturnStatement:
        {}
        break; default: BUSTER_TRAP();
    }
}

BUSTER_GLOBAL_LOCAL void parse(const char8* restrict source, TokenizerResult tokenizer)
{
    Parser parser = {};
    parser.iterator.tokens = tokenizer.tokens;
    parser.iterator.source = source;
    parser.state_stack.arena = arena_create({});
    parser.node_arena = arena_create({});

    // Push a dummy state so the stack is never empty
    parser.state_stack.push();

    bool is_running = true;

    while (is_running)
    {
        switch (state(parser)->id)
        {
            break; case ParserDeclaration::Count: BUSTER_UNREACHABLE();
            break; case ParserDeclaration::Root:
            {
                let token = peek_and_consume(parser);

                switch (token.id)
                {
                    break; case TokenId::Keyword_Code:
                    {
                        let function_state = parser.state_stack.push();
                        function_state->id = ParserDeclaration::Code;
                        function_state->code.current_state = CodeState::BeforeName;
                        function_state->code.node = allocate_node(parser);
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
                        consume(parser.iterator);

                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Code;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                                attribute_list_state->attribute_list.code = &code_state->code.node->code.code_attributes;
                            }
                            break; case TokenId::Identifier:
                            {
                                code_state->code.node->code.name = get_string(parser.iterator.source, token);
                                code_state->code.current_state = CodeState::AfterName;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case CodeState::AfterName:
                    {
                        consume(parser.iterator);

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
                                attribute_list_state->attribute_list.symbol = &code_state->code.node->code.symbol_attributes;
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
                                consume(parser.iterator);

                                code_state->code.current_state = CodeState::Body;

                                parse_block(parser);
                            }
                            break; case TokenId::Semicolon:
                            {
                                consume(parser.iterator);
                                parser.state_stack.pop();
                            }
                            break; case TokenId::Equal:
                            {
                                consume(parser.iterator);
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

                        consume(parser.iterator);

                        code_state->code.current_state = CodeState::Body;

                        parse_block(parser);
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
                                consume(parser.iterator);
                            }
                            break; case array_slice_token_start:
                            {
                                consume(parser.iterator);
                                type_state->type.current_state = TypeState::AfterArraySliceStart;
                            }
                            break; case TokenId::Identifier:
                            {
                                consume(parser.iterator);
                                finish_type_reference(parser);
                            }
                            break; case TokenId::Keyword_Function:
                            {
                                consume(parser.iterator);
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
                                consume(parser.iterator);
                                type_state->type.current_state = TypeState::PrefixOrBase;
                            }
                            // break; case TokenId::Number:
                            // {
                            //     consume(parser);
                            //     type_state->type.current_state = TypeState::AfterArrayCount;
                            // }
                            break; case TokenId::Underscore:
                            {
                                consume(parser.iterator);
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

                        consume(parser.iterator);
                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterArrayInferMarker:
                    {
                        if (token.id != TokenId::RightBracket)
                        {
                            BUSTER_TRAP();
                        }

                        consume(parser.iterator);
                        type_state->type.current_state = TypeState::PrefixOrBase;
                    }
                    break; case TypeState::AfterFunctionKeyword:
                    {
                        switch (token.id)
                        {
                            break; case TokenId::LeftBracket:
                            {
                                consume(parser.iterator);

                                let attribute_list_state = parser.state_stack.push();
                                attribute_list_state->id = ParserDeclaration::AttributeList;
                                attribute_list_state->attribute_list.kind = AttributeListKind::Function;
                                attribute_list_state->attribute_list.current_state = AttributeListState::ItemOrClose;
                            }
                            break; case TokenId::LeftParenthesis:
                            {
                                consume(parser.iterator);
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
                                consume(parser.iterator);
                                type_state->type.current_state = TypeState::FunctionReturnType;
                            }
                            break; case TokenId::Identifier:
                            {
                                consume(parser.iterator);
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

                        consume(parser.iterator);
                        type_state->type.current_state = TypeState::FunctionArgumentAfterColon;
                    }
                    break; case TypeState::FunctionArgumentAfterColon:
                    {
                        if (token.id == TokenId::Identifier)
                        {
                            let next = peek_ahead(parser, 1);
                            if (next.id == TokenId::Colon)
                            {
                                consume(parser.iterator);
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
                                consume(parser.iterator);
                                type_state->type.current_state = TypeState::FunctionArgumentNameOrClose;
                            }
                            break; case TokenId::RightParenthesis:
                            {
                                consume(parser.iterator);
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
                                let attribute_name = get_string(parser.iterator.source, token);

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
                                                attribute_list_state->attribute_list.symbol->exported = true;
                                                attribute_list_state->attribute_list.symbol->linkage = IrLinkage::External;
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
                let token = peek(parser);

                switch (token.id)
                {
                    break; case TokenId::RightBrace:
                    {
                        consume(parser.iterator);

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
                    break; default:
                    {
                        let statement_state = parser.state_stack.push();
                        statement_state->id = ParserDeclaration::Statement;
                        statement_state->statement.state = StatementStateId::Start;
                    }
                }
            }
            break; case ParserDeclaration::Statement:
            {
                let statement_state = state(parser);
                let token = peek(parser);

                switch (statement_state->statement.state)
                {
                    break; case StatementStateId::Start:
                    {
                        statement_state->statement.state = StatementStateId::End;
                        statement_state->statement.end_token = block_end_of_statement_token;

                        consume(parser.iterator);

                        switch (token.id)
                        {
                            break; case TokenId::Keyword_Return:
                            {
                                statement_state->statement.id = StatementId::Return;

                                let state = parser.state_stack.push();
                                state->id = ParserDeclaration::ReturnStatement;
                                state->return_statement.state = ReturnStatementState::ValueOrEnd;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case StatementStateId::End:
                    {
                        bool has_end_of_statement = statement_state->statement.end_token != TokenId::Error;

                        if (has_end_of_statement)
                        {
                            if (token.id != statement_state->statement.end_token)
                            {
                                BUSTER_TRAP();
                            }

                            consume(parser.iterator);
                        }

                        parser.state_stack.pop();
                    }
                    break; case StatementStateId::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::ReturnStatement:
            {
                let return_statement_state = state(parser);
                let token = peek(parser);

                let previous_state = return_statement_state - 1;
                BUSTER_CHECK(previous_state->id == ParserDeclaration::Statement);
                let end_of_statement_token = previous_state->statement.end_token;

                switch (return_statement_state->return_statement.state)
                {
                    break; case ReturnStatementState::ValueOrEnd:
                    {
                        if (token.id == end_of_statement_token)
                        {
                            return_statement_state->return_statement.state = ReturnStatementState::End;
                        }
                        else
                        {
                            parse_expression(parser, end_of_statement_token);
                        }
                    }
                    break; case ReturnStatementState::End:
                    {
                        if (token.id != end_of_statement_token)
                        {
                            BUSTER_TRAP();
                        }

                        parser.state_stack.pop();
                    }
                    break; case ReturnStatementState::Count: BUSTER_UNREACHABLE();
                }
            }
            break; case ParserDeclaration::Expression:
            {
                let st = state(parser);
                let token = peek(parser);

                switch (st->expression.state)
                {
                    break; case ExpressionState::Prefix:
                    {
                        switch (token.id)
                        {
                            break;
                            case TokenId::HexadecimalIntegerLiteral:
                            case TokenId::DecimalIntegerLiteral:
                            case TokenId::OctalIntegerLiteral:
                            case TokenId::BinaryIntegerLiteral:
                            {
                                consume(parser.iterator);
                                let number_node = allocate_node(parser);
                                st->expression.current = number_node;

                                let number_string = get_string(parser.iterator.source, token);

                                IntegerParsingU64 number_parsing;
                                switch (token.id)
                                {
                                    break; case TokenId::HexadecimalIntegerLiteral:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'x');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; case TokenId::DecimalIntegerLiteral:
                                    {
                                        number_parsing = string8_parse_u64_decimal(number_string.pointer);
                                    }
                                    break; case TokenId::OctalIntegerLiteral:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'o');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; case TokenId::BinaryIntegerLiteral:
                                    {
                                        BUSTER_CHECK(number_string.length >= 3);
                                        BUSTER_CHECK(number_string.pointer[0] == '0');
                                        BUSTER_CHECK(number_string.pointer[1] == 'b');
                                        number_parsing = string8_parse_u64_hexadecimal(number_string.pointer + 2);
                                    }
                                    break; default: BUSTER_UNREACHABLE();
                                }

                                *number_node = {
                                    .id = AstNodeId::ConstantInteger,
                                    .constant_integer = number_parsing.value,
                                };

                                st->expression.state = ExpressionState::Tail;
                            }
                            break; default: BUSTER_TRAP();
                        }
                    }
                    break; case ExpressionState::Tail:
                    {
                        if (token.id == st->expression.end_token)
                        {
                            finish_expression(parser);
                        }
                        else
                        {
                            switch (token.id)
                            {
                                break; case TokenId::Plus:
                                {
                                    BUSTER_TRAP();
                                }
                                break; default: BUSTER_TRAP();
                            }
                        }
                    }
                    break; case ExpressionState::Count: BUSTER_UNREACHABLE();
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

BUSTER_GLOBAL_LOCAL void print_tokenizer_result(TokenizerResult tokenizer, const char8* restrict source)
{
    let iterator = token_initialize(tokenizer.tokens, source);
    for (u64 i = 0; i < tokenizer.tokens.length; i += 1)
    {
        let token = token_get(iterator);
        let string = get_string(source, token);

        String8 token_id;

        switch (token.id)
        {
            break; case TokenId::Error: token_id = S8("Error");
            break; case TokenId::Space: token_id = S8("Space");
            break; case TokenId::Tab: token_id = S8("Tab");
            break; case TokenId::LineFeed: token_id = S8("LineFeed");
            break; case TokenId::CarriageReturn: token_id = S8("CarriageReturn");
            break; case TokenId::Comment: token_id = S8("Comment");
            break; case TokenId::EOF: token_id = S8("EOF");
            break; case TokenId::Identifier: token_id = S8("Identifier");
            break; case TokenId::HexadecimalIntegerLiteral: token_id = S8("HexadecimalIntegerLiteral");
            break; case TokenId::DecimalIntegerLiteral: token_id = S8("DecimalIntegerLiteral");
            break; case TokenId::OctalIntegerLiteral: token_id = S8("OctalIntegerLiteral");
            break; case TokenId::BinaryIntegerLiteral: token_id = S8("BinaryIntegerLiteral");
            break; case TokenId::DecimalFloatLiteral: token_id = S8("DecimalFloatLiteral");
            break; case TokenId::DecimalFloatLiteralExponent: token_id = S8("DecimalFloatLiteralExponent");
            break; case TokenId::HexadecimalFloatLiteral: token_id = S8("HexadecimalFloatLiteral");
            break; case TokenId::HexadecimalFloatLiteralExponent: token_id = S8("HexadecimalFloatLiteralExponent");
            break; case TokenId::FloatLiteral: token_id = S8("FloatLiteral");
            break; case TokenId::Underscore: token_id = S8("Underscore");
            break; case TokenId::LeftBracket: token_id = S8("LeftBracket");
            break; case TokenId::RightBracket: token_id = S8("RightBracket");
            break; case TokenId::LeftBrace: token_id = S8("LeftBrace");
            break; case TokenId::RightBrace: token_id = S8("RightBrace");
            break; case TokenId::LeftParenthesis: token_id = S8("LeftParenthesis");
            break; case TokenId::RightParenthesis: token_id = S8("RightParenthesis");
            break; case TokenId::Equal: token_id = S8("Equal");
            break; case TokenId::Greater: token_id = S8("Greater");
            break; case TokenId::Less: token_id = S8("Less");
            break; case TokenId::Plus: token_id = S8("Plus");
            break; case TokenId::PlusEqual: token_id = S8("PlusEqual");
            break; case TokenId::Minus: token_id = S8("Minus");
            break; case TokenId::Asterisk: token_id = S8("Asterisk");
            break; case TokenId::Slash: token_id = S8("Slash");
            break; case TokenId::Percentage: token_id = S8("Percentage");
            break; case TokenId::Colon: token_id = S8("Colon");
            break; case TokenId::Semicolon: token_id = S8("Semicolon");
            break; case TokenId::Comma: token_id = S8("Comma");
            break; case TokenId::Dot: token_id = S8("Dot");
            break; case TokenId::DoubleDot: token_id = S8("DoubleDot");
            break; case TokenId::TripleDot: token_id = S8("TripleDot");
            break; case TokenId::Ampersand: token_id = S8("Ampersand");
            break; case TokenId::Keyword_Return: token_id = S8("Keyword_Return");
            break; case TokenId::Keyword_If: token_id = S8("Keyword_If");
            break; case TokenId::Keyword_Else: token_id = S8("Keyword_Else");
            break; case TokenId::Keyword_Function: token_id = S8("Keyword_Function");
            break; case TokenId::Keyword_For: token_id = S8("Keyword_For");
            break; case TokenId::Keyword_While: token_id = S8("Keyword_While");
            break; case TokenId::Keyword_Code: token_id = S8("Keyword_Code");
            break; case TokenId::Keyword_Data: token_id = S8("Keyword_Data");
            break; case TokenId::Keyword_Type: token_id = S8("Keyword_Type");
            break; case TokenId::Keyword_Struct: token_id = S8("Keyword_Struct");
            break; case TokenId::Keyword_Union: token_id = S8("Keyword_Union");
            break; case TokenId::Count: BUSTER_UNREACHABLE();
        }

        string8_print(S8("[{u64}] {u32}:{u32} at {u32} {S8} \"{S8}\"\n"), i, token.line, token.column, token.offset, token_id, string.pointer[0] >= ' ' ? string : S8(""));

        consume(iterator);
    }
}

BUSTER_GLOBAL_LOCAL void parse_experiment(Arena* arena, StringOs path)
{
    let position = arena->position;
    defer { arena->position = position; };

    let source = BYTE_SLICE_TO_STRING(8, file_read(arena, path, {}));

    let tokenizer = tokenize(arena, source.pointer, source.length);
    print_tokenizer_result(tokenizer, source.pointer);
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
    parse_experiment(arena, SOs("tests/basic_hexadecimal.bbb"));
    parse_experiment(arena, SOs("tests/basic_octal.bbb"));
    parse_experiment(arena, SOs("tests/basic_binary.bbb"));
    parse_experiment(arena, SOs("tests/basic_sum.bbb"));
    // parse_experiment(arena, SOs("tests/if_else.bbb"));
    // parse_experiment(arena, SOs("tests/array_slices.bbb"));
}

#if BUSTER_INCLUDE_TESTS
BUSTER_F_IMPL BatchTestResult parser_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    parser_experiments();
    return result;
}
#endif
