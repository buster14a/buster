#pragma once
#include <buster/os.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL BatchTestResult parser_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_expression_tests(UnitTestArguments* arguments);
#endif

typedef enum TokenIdEnum
{
    TOKEN_ERROR,
    TOKEN_SPACE,
    TOKEN_TAB,
    TOKEN_LINE_FEED,
    TOKEN_CARRIAGE_RETURN,
    TOKEN_COMMENT,
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_HEXADECIMAL_INTEGER_LITERAL,
    TOKEN_DECIMAL_INTEGER_LITERAL,
    TOKEN_OCTAL_INTEGER_LITERAL,
    TOKEN_BINARY_INTEGER_LITERAL,
    TOKEN_DECIMAL_FLOAT_LITERAL,
    TOKEN_DECIMAL_FLOAT_LITERAL_EXPONENT,
    TOKEN_HEXADECIMAL_FLOAT_LITERAL,
    TOKEN_HEXADECIMAL_FLOAT_LITERAL_EXPONENT,
    TOKEN_FLOAT_LITERAL,
    TOKEN_UNDERSCORE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_PARENTHESIS,
    TOKEN_RIGHT_PARENTHESIS,
    TOKEN_EQUAL,
    TOKEN_GREATER,
    TOKEN_LESS,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_PLUS,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS,
    TOKEN_ASTERISK,
    TOKEN_SLASH,
    TOKEN_PERCENTAGE,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_DOUBLE_DOT,
    TOKEN_TRIPLE_DOT,
    TOKEN_AMPERSAND,
    TOKEN_BAR,
    TOKEN_CARET,
    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_ELSE,
    TOKEN_KEYWORD_FUNCTION,
    TOKEN_KEYWORD_FOR,
    TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_CODE,
    TOKEN_KEYWORD_DATA,
    TOKEN_KEYWORD_TYPE,
    TOKEN_KEYWORD_STRUCT,
    TOKEN_KEYWORD_UNION,
    TOKEN_COUNT,
} TokenIdEnum;

typedef u8 TokenId;

typedef struct Token Token;
struct Token
{
    u8 length_bytes[3];
    TokenId id;
};

BUSTER_CT_CHECK(sizeof(Token) == sizeof(u32));

enum
{
    TOKEN_MAX_LENGTH = ((u32)1 << 24) - 1,
};

BUSTER_GLOBAL_LOCAL BUSTER_INLINE u32 token_length_get(Token* restrict token)
{
    u32 result =
        ((u32)token->length_bytes[0] << 0) |
        ((u32)token->length_bytes[1] << 8) |
        ((u32)token->length_bytes[2] << 16);
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_INLINE void token_length_set(Token* restrict token, u32 length)
{
    BUSTER_CHECK(length <= TOKEN_MAX_LENGTH);
    token->length_bytes[0] = (u8)(length >> 0);
    token->length_bytes[1] = (u8)(length >> 8);
    token->length_bytes[2] = (u8)(length >> 16);
}

typedef struct TokenizerResult TokenizerResult;
struct TokenizerResult
{
    Token* tokens;
    u32 token_count;
    u32 error_count;
};

typedef struct LineAndColumn LineAndColumn;
struct LineAndColumn
{
    u32 line;
    u32 column;
};

BUSTER_F_DECL void parser_experiments(void);
BUSTER_F_DECL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length);
BUSTER_F_DECL String8 get_token_content(const char8* source, Token* restrict tokens, u32 lexer_token_index);
