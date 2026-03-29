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
    Keyword_First,
    Keyword_Return,
    Keyword_If,
    Keyword_Else,
    Keyword_Function,
    Keyword_Let,
    Keyword_For,
    Keyword_While,
    Keyword_Last,
    Nonsense);

STRUCT(Token)
{
    TokenId id;
    u8 length;
};

STRUCT(TopLevelDeclaration)
{
    u32 parser_token_start;
    u32 parser_token_end;
};

STRUCT(ParserResult)
{
    u32* restrict parser_token_indices;
    TopLevelDeclaration* restrict top_level_declarations;
    Token* restrict lexer_tokens;
    const char8* restrict source;
    u64 error_count;
    u32 parser_token_count;
    u32 top_level_declaration_count;
    u32 lexer_token_count;
    u32 reserved;
};

STRUCT(TokenizerResult)
{
    Slice<Token> tokens;
    u64 error_count;
};

BUSTER_F_DECL void parser_experiments();
BUSTER_F_DECL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length);
BUSTER_F_IMPL ParserResult parse(const char8* restrict source, TokenizerResult tokenizer);
