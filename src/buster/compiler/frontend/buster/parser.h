#pragma once
#include <buster/base.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL BatchTestResult parser_tests(UnitTestArguments* arguments);
#endif

ENUM_T(TokenId, u8,
    Error,
    Space,
    LineOffset,
    LineNumber,
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

STRUCT(LexerTokenIndex)
{
    u32 v;
};

STRUCT(ParserTokenIndex)
{
    u32 v;
};

STRUCT(TopLevelDeclarationRange)
{
    ParserTokenIndex start;
    ParserTokenIndex end;
    ParserTokenIndex first_body_index;
};

STRUCT(ParserResult)
{
    LexerTokenIndex* restrict parser_to_lexer_indices;
    TopLevelDeclarationRange* restrict top_level_declarations;
    Token* restrict lexer_tokens;
    const char8* restrict source;
    u64 error_count;
    u32 parser_token_count;
    u32 top_level_declaration_count;
    u32 lexer_token_count;
    u32 reserved;

    BUSTER_INLINE Token* get_token(ParserTokenIndex index) const
    {
        let i = parser_to_lexer_indices[index.v];
        let token = &lexer_tokens[i.v];
        return token;
    }
};

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
BUSTER_F_DECL ParserResult parse(const char8* restrict source, TokenizerResult tokenizer);
BUSTER_F_IMPL String8 get_token_content(const char8* source, Token* restrict tokens, LexerTokenIndex lexer_token_index);
