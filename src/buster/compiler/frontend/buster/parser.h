#pragma once
#include <buster/base.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL BatchTestResult parser_tests(UnitTestArguments* arguments);
#endif

ENUM_T(TokenId, u8,
    Error,
    Space,
    Tab,
    LineFeed,
    CarriageReturn,
    Comment,
    EOF,
    Identifier,
    HexadecimalIntegerLiteral,
    DecimalIntegerLiteral,
    OctalIntegerLiteral,
    BinaryIntegerLiteral,
    DecimalFloatLiteral,
    DecimalFloatLiteralExponent,
    HexadecimalFloatLiteral,
    HexadecimalFloatLiteralExponent,
    FloatLiteral,
    Underscore,
    LeftBracket,
    RightBracket,
    LeftBrace,
    RightBrace,
    LeftParenthesis,
    RightParenthesis,
    Equal,
    Greater,
    Less,
    Plus,
    PlusEqual,
    Minus,
    Asterisk,
    Slash,
    Percentage,
    Colon,
    Semicolon,
    Comma,
    Dot,
    DoubleDot,
    TripleDot,
    Ampersand,
    Keyword_Return,
    Keyword_If,
    Keyword_Else,
    Keyword_Function,
    Keyword_For,
    Keyword_While,
    Keyword_Code,
    Keyword_Data,
    Keyword_Type,
    Keyword_Struct,
    Keyword_Union);

BUSTER_GLOBAL_LOCAL constexpr TokenId first_keyword = TokenId::Keyword_Return;
BUSTER_GLOBAL_LOCAL constexpr TokenId last_keyword = TokenId::Keyword_Union;

STRUCT(Token)
{
    u32 length:24;
    TokenId id;
};

static_assert(sizeof(Token) == sizeof(u32));

STRUCT(TokenizerResult)
{
    Slice<Token> tokens;
    u64 error_count;
};

STRUCT(LineAndColumn)
{
    u32 line;
    u32 column;
};

BUSTER_F_DECL void parser_experiments();
BUSTER_F_DECL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length);
BUSTER_F_IMPL String8 get_token_content(const char8* source, Token* restrict tokens, u32 lexer_token_index);
