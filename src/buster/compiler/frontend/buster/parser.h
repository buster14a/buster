#pragma once
#include <buster/os.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
BUSTER_F_DECL UnitTestResult parser_tokenizer_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_expression_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_result_tests(UnitTestArguments* arguments);
BUSTER_F_DECL UnitTestResult parser_file_tests(UnitTestArguments* arguments);
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
    TOKEN_EQUAL_EQUAL,
    TOKEN_BANG,
    TOKEN_BANG_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_LEFT_EQUAL,
    TOKEN_SHIFT_RIGHT,
    TOKEN_SHIFT_RIGHT_EQUAL,
    TOKEN_PLUS,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS,
    TOKEN_MINUS_EQUAL,
    TOKEN_ASTERISK,
    TOKEN_ASTERISK_EQUAL,
    TOKEN_SLASH,
    TOKEN_SLASH_EQUAL,
    TOKEN_PERCENTAGE,
    TOKEN_PERCENTAGE_EQUAL,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_DOUBLE_DOT,
    TOKEN_TRIPLE_DOT,
    TOKEN_AMPERSAND,
    TOKEN_AMPERSAND_EQUAL,
    TOKEN_BAR,
    TOKEN_BAR_EQUAL,
    TOKEN_CARET,
    TOKEN_CARET_EQUAL,
    TOKEN_TILDE,
    TOKEN_AT,
    TOKEN_KEYWORD_RETURN,
    TOKEN_KEYWORD_IF,
    TOKEN_KEYWORD_ELSE,
    TOKEN_KEYWORD_FUNCTION,
    TOKEN_KEYWORD_FOR,
    TOKEN_KEYWORD_WHILE,
    TOKEN_KEYWORD_LOOP,
    TOKEN_KEYWORD_CODE,
    TOKEN_KEYWORD_DATA,
    TOKEN_KEYWORD_TYPE,
    TOKEN_KEYWORD_STRUCT,
    TOKEN_KEYWORD_UNION,
    TOKEN_KEYWORD_ENUM,
    TOKEN_KEYWORD_AND,
    TOKEN_KEYWORD_OR,
    TOKEN_KEYWORD_AND_SHORT_CIRCUIT,
    TOKEN_KEYWORD_OR_SHORT_CIRCUIT,
    TOKEN_KEYWORD_UNDEFINED,
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

typedef struct ParserSourceRange ParserSourceRange;
struct ParserSourceRange
{
    u32 offset;
    u32 length;
    u32 line;
    u32 column;
};

typedef enum AstNodeId
{
    AST_NODE_CONSTANT_INTEGER,
    AST_NODE_CONSTANT_FLOAT,
    AST_NODE_IDENTIFIER,
    AST_NODE_UNDEFINED,
    AST_NODE_ARRAY_LITERAL,
    AST_NODE_ARRAY_INDEX,
    AST_NODE_ARRAY_SLICE,
    AST_NODE_AGGREGATE_LITERAL,
    AST_NODE_MEMBER_ACCESS,
    AST_NODE_ENUM_LITERAL,
    AST_NODE_CALL,
    AST_NODE_INTRINSIC_CALL,

    // Unary operations
    AST_NODE_UNARY_MINUS,
    AST_NODE_UNARY_PLUS,
    AST_NODE_UNARY_LOGICAL_NOT,
    AST_NODE_UNARY_BITWISE_NOT,
    AST_NODE_ADDRESS_OF,
    AST_NODE_DEREFERENCE,

    // Binary operations
    AST_NODE_BINARY_PLUS,
    AST_NODE_BINARY_MINUS,
    AST_NODE_BINARY_ASTERISK,
    AST_NODE_BINARY_SLASH,
    AST_NODE_BINARY_PERCENT,
    AST_NODE_BINARY_SHIFT_LEFT,
    AST_NODE_BINARY_SHIFT_RIGHT,
    AST_NODE_BINARY_EQUAL,
    AST_NODE_BINARY_NOT_EQUAL,
    AST_NODE_BINARY_LESS,
    AST_NODE_BINARY_LESS_EQUAL,
    AST_NODE_BINARY_GREATER,
    AST_NODE_BINARY_GREATER_EQUAL,
    AST_NODE_BINARY_AMPERSAND,
    AST_NODE_BINARY_BAR,
    AST_NODE_BINARY_CARET,
    AST_NODE_BINARY_BOOLEAN_AND,
    AST_NODE_BINARY_BOOLEAN_OR,
    AST_NODE_BINARY_BOOLEAN_AND_SHORT_CIRCUIT,
    AST_NODE_BINARY_BOOLEAN_OR_SHORT_CIRCUIT,
    AST_NODE_BINARY_RANGE,

    AST_NODE_COUNT,
} AstNodeId;

typedef struct AstIdentifier AstIdentifier;
struct AstIdentifier
{
    String8 text;
    ParserSourceRange range;
};

typedef struct AstIntegerLiteral AstIntegerLiteral;
struct AstIntegerLiteral
{
    String8 spelling;
    u64 value;
    u8 base;
    bool fits_u64;
    u8 reserved[6];
};

typedef struct AstFloatLiteral AstFloatLiteral;
struct AstFloatLiteral
{
    String8 spelling;
    u8 base;
    bool has_exponent;
    u8 reserved[6];
};

typedef struct AstArrayLiteral AstArrayLiteral;
struct AstArrayLiteral
{
    ParserSourceRange range;
    u32 element_count;
};

typedef struct AstArrayIndex AstArrayIndex;
struct AstArrayIndex
{
    ParserSourceRange range;
};

typedef struct AstArraySlice AstArraySlice;
struct AstArraySlice
{
    ParserSourceRange range;
    bool has_start;
    bool has_end;
    u8 reserved[2];
};

typedef struct AstIntrinsicCall AstIntrinsicCall;
typedef struct AstAggregateLiteralField AstAggregateLiteralField;
struct AstAggregateLiteralField
{
    AstAggregateLiteralField* next;
    AstIdentifier name;
};

typedef struct AstAggregateLiteral AstAggregateLiteral;
struct AstAggregateLiteral
{
    AstAggregateLiteralField* first_field;
    AstAggregateLiteralField* last_field;
    ParserSourceRange range;
    u32 field_count;
};

typedef struct AstMemberAccess AstMemberAccess;
struct AstMemberAccess
{
    AstIdentifier member;
    ParserSourceRange range;
};

typedef struct AstPointerOperator AstPointerOperator;
struct AstPointerOperator
{
    ParserSourceRange range;
};

typedef struct AstEnumLiteral AstEnumLiteral;
struct AstEnumLiteral
{
    AstIdentifier member;
    ParserSourceRange range;
};

typedef struct AstCall AstCall;
struct AstCall
{
    ParserSourceRange range;
    u32 argument_count;
};

struct AstIntrinsicCall
{
    AstIdentifier name;
    ParserSourceRange range;
    u32 argument_count;
};

typedef struct AstType AstType;
typedef struct AstNode AstNode;

struct AstNode
{
    AstNodeId id;
    union
    {
        AstIntegerLiteral integer;
        AstFloatLiteral floating;
        AstIdentifier identifier;
        AstArrayLiteral array_literal;
        AstArrayIndex array_index;
        AstArraySlice array_slice;
        AstAggregateLiteral aggregate_literal;
        AstMemberAccess member_access;
        AstPointerOperator pointer_operator;
        AstEnumLiteral enum_literal;
        AstCall call;
        AstIntrinsicCall intrinsic_call;
    };
};

typedef struct AstExpression AstExpression;
struct AstExpression
{
    AstNode* nodes;
    u32 count;
    u32 reserved;
};

typedef struct AstStatement AstStatement;
typedef struct AstBlock AstBlock;
struct AstBlock
{
    AstStatement* first_statement;
    AstStatement* last_statement;
    ParserSourceRange range;
    u32 statement_count;
    u32 reserved;
};

typedef enum AstStatementId
{
    AST_STATEMENT_RETURN,
    AST_STATEMENT_DATA,
    AST_STATEMENT_EXPRESSION,
    AST_STATEMENT_ASSIGNMENT,
    AST_STATEMENT_IF,
    AST_STATEMENT_FOR,
    AST_STATEMENT_LOOP,
    AST_STATEMENT_COUNT,
} AstStatementId;

typedef struct AstReturnStatement AstReturnStatement;
struct AstReturnStatement
{
    AstExpression expression;
};

typedef struct AstDataStatement AstDataStatement;
struct AstDataStatement
{
    AstIdentifier name;
    AstType* type;
    AstExpression initializer;
};

typedef struct AstAssignmentStatement AstAssignmentStatement;
typedef struct AstExpressionStatement AstExpressionStatement;
struct AstExpressionStatement
{
    AstExpression expression;
};

typedef enum AstAssignmentOperator
{
    AST_ASSIGNMENT_EQUAL,
    AST_ASSIGNMENT_PLUS_EQUAL,
    AST_ASSIGNMENT_MINUS_EQUAL,
    AST_ASSIGNMENT_MULTIPLY_EQUAL,
    AST_ASSIGNMENT_DIVIDE_EQUAL,
    AST_ASSIGNMENT_MODULO_EQUAL,
    AST_ASSIGNMENT_SHIFT_LEFT_EQUAL,
    AST_ASSIGNMENT_SHIFT_RIGHT_EQUAL,
    AST_ASSIGNMENT_BITWISE_AND_EQUAL,
    AST_ASSIGNMENT_BITWISE_OR_EQUAL,
    AST_ASSIGNMENT_BITWISE_XOR_EQUAL,
    AST_ASSIGNMENT_COUNT,
} AstAssignmentOperator;

struct AstAssignmentStatement
{
    AstExpression target;
    AstExpression value;
    AstAssignmentOperator operator;
};

typedef struct AstIfStatement AstIfStatement;
struct AstIfStatement
{
    AstExpression condition;
    AstBlock then_block;
    AstBlock else_block;
    bool has_else;
    u8 reserved[7];
};

typedef struct AstForStatement AstForStatement;
struct AstForStatement
{
    AstIdentifier name;
    AstType* type;
    AstExpression iterable;
    AstBlock body;
};

typedef struct AstLoopStatement AstLoopStatement;
struct AstLoopStatement
{
    AstExpression condition;
    AstBlock body;
    bool has_condition;
    u8 reserved[7];
};

struct AstStatement
{
    AstStatement* next;
    ParserSourceRange range;
    AstStatementId id;
    union
    {
        AstReturnStatement return_statement;
        AstDataStatement data_statement;
        AstExpressionStatement expression_statement;
        AstAssignmentStatement assignment_statement;
        AstIfStatement if_statement;
        AstForStatement for_statement;
        AstLoopStatement loop_statement;
    };
};

typedef enum AstCallingConvention
{
    AST_CALLING_CONVENTION_C,
    AST_CALLING_CONVENTION_SYSTEMV,
    AST_CALLING_CONVENTION_WIN64,
    AST_CALLING_CONVENTION_COUNT,
} AstCallingConvention;

typedef enum AstTypeId
{
    AST_TYPE_NAMED,
    AST_TYPE_POINTER,
    AST_TYPE_SLICE,
    AST_TYPE_INFERRED_ARRAY,
    AST_TYPE_ARRAY,
    AST_TYPE_FUNCTION,
    AST_TYPE_COUNT,
} AstTypeId;

typedef struct AstTypeArgument AstTypeArgument;

struct AstTypeArgument
{
    AstTypeArgument* next;
    String8 name;
    AstType* type;
    ParserSourceRange range;
};

struct AstType
{
    ParserSourceRange range;
    AstTypeId id;
    u32 reserved;
    union
    {
        String8 name;
        AstType* element_type;
        struct
        {
            AstType* element_type;
            AstIntegerLiteral count;
        } array;
        struct
        {
            AstTypeArgument* first_argument;
            AstTypeArgument* last_argument;
            AstType* return_type;
            AstCallingConvention calling_convention;
            u32 argument_count;
        } function;
    };
};

typedef enum AstTypeDeclarationKind
{
    AST_TYPE_DECLARATION_STRUCT,
    AST_TYPE_DECLARATION_UNION,
    AST_TYPE_DECLARATION_ENUM,
    AST_TYPE_DECLARATION_COUNT,
} AstTypeDeclarationKind;

typedef struct AstTypeField AstTypeField;
struct AstTypeField
{
    AstTypeField* next;
    AstIdentifier name;
    AstType* type;
    ParserSourceRange range;
};

typedef struct AstEnumMember AstEnumMember;
struct AstEnumMember
{
    AstEnumMember* next;
    AstIdentifier name;
    AstExpression value;
    ParserSourceRange range;
    bool has_explicit_value;
    u8 reserved[3];
};

typedef struct AstTypeDeclaration AstTypeDeclaration;
struct AstTypeDeclaration
{
    AstTypeDeclaration* next;
    AstTypeField* first_field;
    AstTypeField* last_field;
    AstEnumMember* first_enum_member;
    AstEnumMember* last_enum_member;
    AstIdentifier name;
    ParserSourceRange range;
    AstTypeDeclarationKind kind;
    u32 field_count;
    u32 enum_member_count;
    u32 reserved;
};

typedef struct AstCode AstCode;
struct AstCode
{
    AstCode* next;
    String8 name;
    AstType* type;
    AstBlock body;
    ParserSourceRange range;
    bool exported;
    bool inline_hint;
    bool has_body;
    u8 reserved[1];
};

typedef enum ParserDiagnosticKind
{
    PARSER_DIAGNOSTIC_UNEXPECTED_TOKEN,
    PARSER_DIAGNOSTIC_EXPECTED_EXPRESSION,
    PARSER_DIAGNOSTIC_EXPECTED_ASSIGNMENT_OPERATOR,
    PARSER_DIAGNOSTIC_EXPECTED_ARRAY_DELIMITER,
    PARSER_DIAGNOSTIC_EXPECTED_CALL_DELIMITER,
    PARSER_DIAGNOSTIC_EXPECTED_TYPE_FIELD_DELIMITER,
    PARSER_DIAGNOSTIC_EXPECTED_AGGREGATE_DELIMITER,
    PARSER_DIAGNOSTIC_EXPECTED_POSTFIX_ACCESS,
    PARSER_DIAGNOSTIC_EXPECTED_ENUM_DELIMITER,
    PARSER_DIAGNOSTIC_EXPECTED_TYPE_DECLARATION_KIND,
    PARSER_DIAGNOSTIC_CHAINED_RANGE,
    PARSER_DIAGNOSTIC_INVALID_INTEGER,
    PARSER_DIAGNOSTIC_EXPRESSION_TOO_DEEP,
    PARSER_DIAGNOSTIC_COUNT,
} ParserDiagnosticKind;

typedef struct ParserDiagnostic ParserDiagnostic;
struct ParserDiagnostic
{
    ParserDiagnostic* next;
    String8 message;
    ParserSourceRange range;
    ParserDiagnosticKind kind;
    TokenId found;
    TokenId expected;
    u8 reserved[2];
};

typedef struct ParserResult ParserResult;
struct ParserResult
{
    String8 source;
    AstCode* first_code;
    AstCode* last_code;
    AstTypeDeclaration* first_type_declaration;
    AstTypeDeclaration* last_type_declaration;
    ParserDiagnostic* first_diagnostic;
    ParserDiagnostic* last_diagnostic;
    u32 code_count;
    u32 type_declaration_count;
    u32 diagnostic_count;
};

typedef struct LineAndColumn LineAndColumn;
struct LineAndColumn
{
    u32 line;
    u32 column;
};

BUSTER_F_DECL TokenizerResult tokenize(Arena* arena, const char8* restrict file_pointer, u64 file_length);
BUSTER_F_DECL ParserResult parser_parse(Arena* result_arena, Arena* expression_arena, String8 source, TokenizerResult tokenizer);
BUSTER_F_DECL String8 get_token_content(const char8* source, Token* restrict tokens, u32 lexer_token_index);

#if BUSTER_INSTRUMENT
typedef struct ParserBenchFileResult ParserBenchFileResult;
struct ParserBenchFileResult
{
    String8 path;
    u64 min_ns;
    u64 median_ns;
};
#endif

typedef struct ParserBenchResult ParserBenchResult;
struct ParserBenchResult
{
    u64 iterations;
    u64 file_count;
    u64 min_ns;
    u64 median_ns;
#if BUSTER_INSTRUMENT
    u64 tokenize_min_ns;
    u64 tokenize_median_ns;
    u64 parse_min_ns;
    u64 parse_median_ns;
    // Sorted slowest (by median_ns) first.
    ParserBenchFileResult* files;
#endif
};

// Parses every entry in the file-test corpus `iterations` times, timing each
// full pass, and returns min/median wall time. Not gated by
// BUSTER_INCLUDE_TESTS: this is a benchmark, not a test, and must stay
// buildable in Release for CI perf tracking.
BUSTER_F_DECL ParserBenchResult parser_parse_bench(Arena* arena, u64 iterations);
