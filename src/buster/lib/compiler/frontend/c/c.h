#pragma once

#include <buster/lib/arena.h>
#include <buster/lib/target.h>

typedef enum CTokenKind
{
    C_TOKEN_INVALID,
    C_TOKEN_END_OF_FILE,
    C_TOKEN_IDENTIFIER,
    C_TOKEN_PREPROCESSING_NUMBER,
    C_TOKEN_CHARACTER_LITERAL,
    C_TOKEN_STRING_LITERAL,
    C_TOKEN_PUNCTUATOR,
    C_TOKEN_NEWLINE,
    // Internal preprocessing marker produced by _Pragma/__pragma.  It is
    // consumed before the public preprocessing token stream is published.
    C_TOKEN_PRAGMA,
    C_TOKEN_KIND_COUNT,
} CTokenKind;

typedef struct CSourceLocation CSourceLocation;
struct CSourceLocation
{
    u64 offset;
    u32 line;
    u32 column;
    u32 file;
    u32 reserved;
};

typedef struct CToken CToken;
struct CToken
{
    String8 spelling;
    CSourceLocation location;
    CTokenKind kind;
    u32 pack_alignment;
};

typedef enum CDiagnosticKind
{
    C_DIAGNOSTIC_INVALID_CHARACTER,
    C_DIAGNOSTIC_UNTERMINATED_BLOCK_COMMENT,
    C_DIAGNOSTIC_UNTERMINATED_CHARACTER_LITERAL,
    C_DIAGNOSTIC_UNTERMINATED_STRING_LITERAL,
    C_DIAGNOSTIC_EXPECTED_DIRECTIVE,
    C_DIAGNOSTIC_EXPECTED_MACRO_NAME,
    C_DIAGNOSTIC_UNSUPPORTED_DIRECTIVE,
    C_DIAGNOSTIC_INVALID_MACRO_DEFINITION,
    C_DIAGNOSTIC_INVALID_MACRO_INVOCATION,
    C_DIAGNOSTIC_INVALID_TOKEN_PASTE,
    C_DIAGNOSTIC_MACRO_EXPANSION_LIMIT,
    C_DIAGNOSTIC_INVALID_CONDITIONAL,
    C_DIAGNOSTIC_UNMATCHED_CONDITIONAL,
    C_DIAGNOSTIC_INVALID_INCLUDE,
    C_DIAGNOSTIC_INCLUDE_NOT_FOUND,
    C_DIAGNOSTIC_INCLUDE_DEPTH,
    C_DIAGNOSTIC_INVALID_LINE,
    C_DIAGNOSTIC_INVALID_ALIGNMENT,
    C_DIAGNOSTIC_INVALID_ATOMIC_TYPE,
    C_DIAGNOSTIC_INVALID_FLEXIBLE_ARRAY_MEMBER,
    C_DIAGNOSTIC_EXPECTED_DECLARATION,
    C_DIAGNOSTIC_UNMATCHED_DELIMITER,
    C_DIAGNOSTIC_CONFLICTING_DECLARATION,
    C_DIAGNOSTIC_REDEFINITION,
    C_DIAGNOSTIC_UNDECLARED_IDENTIFIER,
    C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
    C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
    C_DIAGNOSTIC_INVALID_CONSTEXPR,
    C_DIAGNOSTIC_INVALID_CLEANUP_ATTRIBUTE,
    C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
    C_DIAGNOSTIC_PREPROCESSOR_ERROR,
    C_DIAGNOSTIC_PREPROCESSOR_WARNING,
    C_DIAGNOSTIC_KIND_COUNT,
} CDiagnosticKind;

typedef enum CDiagnosticSeverity
{
    C_DIAGNOSTIC_ERROR,
    C_DIAGNOSTIC_WARNING,
} CDiagnosticSeverity;

typedef struct CDiagnostic CDiagnostic;
struct CDiagnostic
{
    String8 message;
    CSourceLocation location;
    CDiagnosticKind kind;
    CDiagnosticSeverity severity;
    u32 reserved;
};

typedef struct CLexResult CLexResult;
struct CLexResult
{
    String8 translated_source;
    CToken* tokens;
    CDiagnostic* diagnostics;
    u64 token_count;
    u64 diagnostic_count;
};

typedef struct CPreprocessorDefinition CPreprocessorDefinition;
struct CPreprocessorDefinition
{
    String8 name;
    String8 value;
};

typedef enum CPreprocessDialect
{
    C_PREPROCESS_DIALECT_GNU17,
    C_PREPROCESS_DIALECT_GNU11,
    C_PREPROCESS_DIALECT_GNU23,
    C_PREPROCESS_DIALECT_C11,
    C_PREPROCESS_DIALECT_C17,
    C_PREPROCESS_DIALECT_C23,
    C_PREPROCESS_DIALECT_COUNT,
} CPreprocessDialect;

typedef struct CPreprocessOptions CPreprocessOptions;
struct CPreprocessOptions
{
    CPreprocessorDefinition* definitions;
    String8* undefinitions;
    String8* include_paths;
    String8* system_include_paths;
    String8 source_path;
    Target target;
    TargetDataLayout data_layout;
    u32 definition_count;
    u32 undefinition_count;
    u32 include_path_count;
    u32 system_include_path_count;
    u32 expansion_limit;
    u32 include_depth_limit;
    CPreprocessDialect dialect;
    bool disable_external_includes;
    u8 reserved[3];
};

typedef struct CPreprocessResult CPreprocessResult;
struct CPreprocessResult
{
    CToken* tokens;
    CDiagnostic* diagnostics;
    String8* files;
    Target target;
    TargetDataLayout data_layout;
    u64 token_count;
    u64 diagnostic_count;
    u64 error_count;
    u64 warning_count;
    u64 diagnostic_capacity;
    u32 file_count;
    CPreprocessDialect dialect;
};

typedef u32 CIdUnderlying;

typedef struct CNodeId CNodeId;
struct CNodeId
{
    CIdUnderlying value;
};

typedef struct CTypeId CTypeId;
struct CTypeId
{
    CIdUnderlying value;
};

typedef struct CEntityId CEntityId;
struct CEntityId
{
    CIdUnderlying value;
};

typedef struct CScopeId CScopeId;
struct CScopeId
{
    CIdUnderlying value;
};

#define C_ID_UNDERLYING_INVALID UINT32_MAX
#define C_NODE_ID_INVALID ((CNodeId){.value = C_ID_UNDERLYING_INVALID})
#define C_TYPE_ID_INVALID ((CTypeId){.value = C_ID_UNDERLYING_INVALID})
#define C_ENTITY_ID_INVALID ((CEntityId){.value = C_ID_UNDERLYING_INVALID})
#define C_SCOPE_ID_INVALID ((CScopeId){.value = C_ID_UNDERLYING_INVALID})
#define C_ARRAY_BOUND_INVALID UINT32_MAX

BUSTER_CT_CHECK(sizeof(CNodeId) == sizeof(CIdUnderlying));
BUSTER_CT_CHECK(sizeof(CTypeId) == sizeof(CIdUnderlying));
BUSTER_CT_CHECK(sizeof(CEntityId) == sizeof(CIdUnderlying));
BUSTER_CT_CHECK(sizeof(CScopeId) == sizeof(CIdUnderlying));

typedef enum CTypeKind
{
    C_TYPE_INVALID,
    C_TYPE_VOID,
    C_TYPE_BOOL,
    C_TYPE_CHAR,
    C_TYPE_SIGNED_CHAR,
    C_TYPE_UNSIGNED_CHAR,
    C_TYPE_SHORT,
    C_TYPE_UNSIGNED_SHORT,
    C_TYPE_INT,
    C_TYPE_UNSIGNED_INT,
    C_TYPE_LONG,
    C_TYPE_UNSIGNED_LONG,
    C_TYPE_LONG_LONG,
    C_TYPE_UNSIGNED_LONG_LONG,
    C_TYPE_INT128,
    C_TYPE_UNSIGNED_INT128,
    C_TYPE_FLOAT,
    C_TYPE_DOUBLE,
    C_TYPE_LONG_DOUBLE,
    C_TYPE_VA_LIST,
    C_TYPE_NULLPTR,
    C_TYPE_POINTER,
    C_TYPE_ARRAY,
    C_TYPE_VECTOR,
    C_TYPE_FUNCTION,
    C_TYPE_STRUCT,
    C_TYPE_UNION,
    C_TYPE_ENUM,
    C_TYPE_COUNT,
} CTypeKind;

typedef struct CArrayBound CArrayBound;
struct CArrayBound
{
    u64 inferred_count;
    u32 token_start;
    u32 token_count;
    bool is_static;
    bool is_star;
    bool has_inferred_count;
    u8 reserved;
};

typedef struct CType CType;
struct CType
{
    String8 tag;
    CTypeId element_type;
    CTypeId return_type;
    CTypeId unqualified_type;
    u32 array_bound;
    u32 parameter_start;
    u32 parameter_count;
    u32 member_start;
    u32 member_count;
    u32 enum_member_start;
    u32 enum_member_count;
    u32 definition_start;
    u32 definition_token_count;
    u32 vector_byte_size;
    CTypeKind kind;
    bool is_const;
    bool is_volatile;
    bool is_restrict;
    bool is_atomic;
    bool is_variadic;
    bool is_complete;
    bool has_unqualified_type;
    u8 reserved;
};

typedef struct CMember CMember;
struct CMember
{
    String8 name;
    CSourceLocation location;
    CTypeId type;
    u32 alignment_start;
    u32 alignment_count;
    u32 bit_width;
    u32 bit_width_token_start;
    u32 bit_width_token_count;
    bool is_bit_field;
    u8 reserved[3];
};

typedef struct CAlignmentSpecifier CAlignmentSpecifier;
struct CAlignmentSpecifier
{
    CTypeId type;
    u32 token_start;
    u32 token_count;
};

typedef struct CEnumMember CEnumMember;
struct CEnumMember
{
    String8 name;
    CSourceLocation location;
    u64 value;
    bool is_negative;
    u8 reserved[7];
};

typedef struct CParameter CParameter;
struct CParameter
{
    String8 name;
    CSourceLocation location;
    CTypeId type;
    CEntityId entity;
};

typedef struct CParserStatement CParserStatement;
typedef struct CParserDeclaration CParserDeclaration;

typedef enum CEntityKind
{
    C_ENTITY_OBJECT,
    C_ENTITY_FUNCTION,
    C_ENTITY_TYPEDEF,
    C_ENTITY_PARAMETER,
    C_ENTITY_LOCAL,
    C_ENTITY_ENUMERATOR,
    C_ENTITY_COUNT,
} CEntityKind;

typedef struct CEntity CEntity;
struct CEntity
{
    String8 name;
    CSourceLocation location;
    CTypeId type;
    CScopeId scope;
    CEntityId next_in_scope;
    CEntityId next_in_lookup;
    CEntityId next_typedef_in_lookup;
    u32 declaration_index;
    u32 declaration_token_plus_one;
    u32 declaration_token_start;
    u32 declaration_token_count;
    u32 alignment_start;
    u32 alignment_count;
    CEntityKind kind;
    bool is_definition;
    bool is_static_storage;
    bool is_thread_local;
    bool is_constexpr;
    bool has_constant_value;
    bool constant_is_negative;
    bool has_cleanup;
    bool cleanup_attribute_checked;
    u8 reserved[2];
    CEntityId cleanup_function;
    u32 cleanup_attribute_token;
    u32 cleanup_attribute_end;
    u64 constant_value;
};

typedef struct CScope CScope;
struct CScope
{
    CScopeId parent;
    CEntityId first_entity;
    CEntityId last_entity;
    u32 token_start;
    u32 token_end;
    u32 entity_count;
};

typedef struct CIdentifierUse CIdentifierUse;
struct CIdentifierUse
{
    u32 token_index;
    CEntityId entity;
    CScopeId scope;
};

typedef enum CDeclarationKind
{
    C_DECLARATION_OBJECT,
    C_DECLARATION_FUNCTION,
    C_DECLARATION_TYPEDEF,
    C_DECLARATION_TYPE,
    C_DECLARATION_ASSEMBLY,
    C_DECLARATION_COUNT,
} CDeclarationKind;

typedef struct CDeclaration CDeclaration;
struct CDeclaration
{
    String8 name;
    CSourceLocation location;
    u32 token_start;
    u32 token_count;
    u32 body_start;
    u32 body_token_count;
    u32 parameter_start;
    u32 parameter_count;
    u32 alignment_start;
    u32 alignment_count;
    CTypeId type;
    CTypeId base_type;
    CEntityId entity;
    CScopeId scope;
    CParserDeclaration* syntax_declaration;
    CParserStatement* syntax_body;
    CDeclarationKind kind;
    bool is_definition;
    bool is_variadic;
    bool is_constexpr;
    u8 reserved;
};

typedef struct CDeferredStaticAssert CDeferredStaticAssert;
struct CDeferredStaticAssert
{
    u32 token_start;
    u32 token_count;
    CScopeId scope;
    CSourceLocation location;
};

typedef enum CParserDeclarationKind
{
    C_PARSER_DECLARATION_OBJECT,
    C_PARSER_DECLARATION_FUNCTION,
    C_PARSER_DECLARATION_TYPEDEF,
    C_PARSER_DECLARATION_TYPE,
    C_PARSER_DECLARATION_STATIC_ASSERT,
    C_PARSER_DECLARATION_ASSEMBLY,
    C_PARSER_DECLARATION_UNKNOWN,
    C_PARSER_DECLARATION_COUNT,
} CParserDeclarationKind;

typedef enum CParserStatementKind
{
    C_PARSER_STATEMENT_BLOCK,
    C_PARSER_STATEMENT_STATIC_ASSERT,
    C_PARSER_STATEMENT_DECLARATION,
    C_PARSER_STATEMENT_EXPRESSION,
    C_PARSER_STATEMENT_LABEL,
    C_PARSER_STATEMENT_UNKNOWN,
    C_PARSER_STATEMENT_COUNT,
} CParserStatementKind;

typedef struct CParserExpression CParserExpression;
struct CParserExpression
{
    u32 token_start;
    u32 token_count;
};

struct CParserStatement
{
    CParserStatement* next;
    CParserStatement* first_child;
    CParserStatement* last_child;
    CParserExpression expression;
    CSourceLocation location;
    u32 token_start;
    u32 token_count;
    u32 body_start;
    u32 body_token_count;
    CParserStatementKind kind;
    u8 reserved[4];
};

struct CParserDeclaration
{
    CParserDeclaration* next;
    CSourceLocation location;
    u32 token_start;
    u32 token_count;
    u32 body_start;
    u32 body_token_count;
    u32 name_token;
    u32 function_name_token;
    CParserStatement* first_statement;
    CParserStatement* last_statement;
    CParserExpression expression;
    CParserDeclarationKind kind;
    bool is_definition;
    bool is_typedef;
    bool is_constexpr;
    bool is_variadic;
    bool seen_equal;
    u8 reserved[3];
};

typedef struct CParserResult CParserResult;
struct CParserResult
{
    CParserDeclaration* first_declaration;
    CParserDeclaration* last_declaration;
    CDiagnostic* diagnostics;
    u32 declaration_count;
    u32 diagnostic_count;
    u32 declaration_capacity;
    u32 diagnostic_capacity;
};

typedef struct CParseResult CParseResult;
struct CParseResult
{
    // Borrowed owner of the result arrays.  It must outlive this result and is
    // never destroyed or rewound by the parser.
    Arena* arena;
    CDeclaration* declarations;
    CType* types;
    CParameter* parameters;
    CMember* members;
    CEnumMember* enum_members;
    CArrayBound* array_bounds;
    CAlignmentSpecifier* alignments;
    CEntity* entities;
    CScope* scopes;
    CEntityId* entity_lookup_buckets;
    CEntityId* typedef_lookup_buckets;
    CIdentifierUse* identifier_uses;
    u32* identifier_use_by_token;
    CDiagnostic* diagnostics;
    CDeferredStaticAssert* deferred_static_asserts;
    u32 declaration_count;
    u32 type_count;
    u32 parameter_count;
    u32 member_count;
    u32 enum_member_count;
    u32 array_bound_count;
    u32 alignment_count;
    u32 entity_count;
    u32 scope_count;
    u32 identifier_use_count;
    u32 diagnostic_count;
    u32 deferred_static_assert_count;
    u32 declaration_capacity;
    u32 type_capacity;
    u32 parameter_capacity;
    u32 member_capacity;
    u32 enum_member_capacity;
    u32 array_bound_capacity;
    u32 alignment_capacity;
    u32 entity_capacity;
    u32 scope_capacity;
    u32 entity_lookup_bucket_count;
    u32 identifier_use_capacity;
    u32 identifier_use_by_token_capacity;
    u32 diagnostic_capacity;
    u32 deferred_static_assert_capacity;
};

// CParseResult is the compatibility name for the semantic model.  New phase
// boundaries should use CAnalysisResult so syntax parsing cannot be confused
// with semantic analysis.
typedef CParseResult CAnalysisResult;

typedef struct IrProgram IrProgram;
typedef struct CIRLowerResult CIRLowerResult;
struct CIRLowerResult
{
    IrProgram* program;
    CDiagnostic* diagnostics;
    u32 diagnostic_count;
};

BUSTER_F_DECL CLexResult c_lex(Arena* arena, String8 source);
BUSTER_F_DECL CPreprocessResult c_preprocess(Arena* arena, String8 source, CPreprocessOptions options);
BUSTER_F_DECL CParserResult c_parse_ast(Arena* arena, CPreprocessResult preprocess);
BUSTER_F_DECL CIRLowerResult c_analyze(Arena* arena, String8 source_path, CPreprocessResult preprocess, CParserResult syntax, Target target);
BUSTER_F_DECL CParseResult c_parse(Arena* arena, CPreprocessResult preprocess);
// Compatibility entry point for tests and callers that already own an analyzed model.
BUSTER_F_DECL CIRLowerResult c_lower_to_ir(Arena* arena, String8 source_path, CPreprocessResult preprocess, CAnalysisResult analysis, Target target);
BUSTER_F_DECL String8 c_token_kind_name(CTokenKind kind);
