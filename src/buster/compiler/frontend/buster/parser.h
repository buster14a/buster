#pragma once
#include <buster/base.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL BatchTestResult parser_tests(UnitTestArguments* arguments);
#endif

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

STRUCT(Token)
{
    TokenId id;
    u8 length;
};

STRUCT(ParserResult)
{
    u32* restrict parser_token_indices;
    Token* restrict lexer_tokens;
    u32 parser_token_count;
    u32 lexer_token_count;
    const char8* restrict source;
};

STRUCT(TokenizerResult)
{
    Slice<Token> tokens;
    u64 error_count;
};

BUSTER_F_DECL void parser_experiments();
BUSTER_F_IMPL ParserResult parse(const char8* restrict source, TokenizerResult tokenizer);
