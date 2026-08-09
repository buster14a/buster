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

// Every punctuator the lexer can produce, so that recognizing one is a scalar
// compare instead of a string compare.  The declaration order is the lexer's
// maximal-munch scan order: a spelling must precede every spelling it starts
// with.  Digraphs stay distinct from the punctuators they spell, because
// callers ask about a spelling and never about a meaning.
typedef enum CPunctuator
{
    C_PUNCTUATOR_NONE,
    C_PUNCTUATOR_HASH_HASH_DIGRAPH,
    C_PUNCTUATOR_SHIFT_LEFT_ASSIGN,
    C_PUNCTUATOR_SHIFT_RIGHT_ASSIGN,
    C_PUNCTUATOR_ELLIPSIS,
    C_PUNCTUATOR_ARROW,
    C_PUNCTUATOR_PLUS_PLUS,
    C_PUNCTUATOR_MINUS_MINUS,
    C_PUNCTUATOR_SHIFT_LEFT,
    C_PUNCTUATOR_SHIFT_RIGHT,
    C_PUNCTUATOR_LESS_EQUAL,
    C_PUNCTUATOR_GREATER_EQUAL,
    C_PUNCTUATOR_EQUAL,
    C_PUNCTUATOR_NOT_EQUAL,
    C_PUNCTUATOR_AMPERSAND_AMPERSAND,
    C_PUNCTUATOR_PIPE_PIPE,
    C_PUNCTUATOR_STAR_ASSIGN,
    C_PUNCTUATOR_SLASH_ASSIGN,
    C_PUNCTUATOR_PERCENT_ASSIGN,
    C_PUNCTUATOR_PLUS_ASSIGN,
    C_PUNCTUATOR_MINUS_ASSIGN,
    C_PUNCTUATOR_AMPERSAND_ASSIGN,
    C_PUNCTUATOR_CARET_ASSIGN,
    C_PUNCTUATOR_PIPE_ASSIGN,
    C_PUNCTUATOR_HASH_HASH,
    C_PUNCTUATOR_LEFT_BRACKET_DIGRAPH,
    C_PUNCTUATOR_RIGHT_BRACKET_DIGRAPH,
    C_PUNCTUATOR_LEFT_BRACE_DIGRAPH,
    C_PUNCTUATOR_RIGHT_BRACE_DIGRAPH,
    C_PUNCTUATOR_HASH_DIGRAPH,
    C_PUNCTUATOR_LEFT_BRACKET,
    C_PUNCTUATOR_RIGHT_BRACKET,
    C_PUNCTUATOR_LEFT_PARENTHESIS,
    C_PUNCTUATOR_RIGHT_PARENTHESIS,
    C_PUNCTUATOR_LEFT_BRACE,
    C_PUNCTUATOR_RIGHT_BRACE,
    C_PUNCTUATOR_DOT,
    C_PUNCTUATOR_AMPERSAND,
    C_PUNCTUATOR_STAR,
    C_PUNCTUATOR_PLUS,
    C_PUNCTUATOR_MINUS,
    C_PUNCTUATOR_TILDE,
    C_PUNCTUATOR_EXCLAMATION,
    C_PUNCTUATOR_SLASH,
    C_PUNCTUATOR_PERCENT,
    C_PUNCTUATOR_LESS,
    C_PUNCTUATOR_GREATER,
    C_PUNCTUATOR_CARET,
    C_PUNCTUATOR_PIPE,
    C_PUNCTUATOR_QUESTION,
    C_PUNCTUATOR_COLON,
    C_PUNCTUATOR_SEMICOLON,
    C_PUNCTUATOR_ASSIGN,
    C_PUNCTUATOR_COMMA,
    C_PUNCTUATOR_HASH,
    C_PUNCTUATOR_AT,
    C_PUNCTUATOR_BACKSLASH,
    C_PUNCTUATOR_COUNT,
} CPunctuator;

typedef struct CSourceLocation CSourceLocation;
struct CSourceLocation
{
    u64 offset;
    u32 line;
    u32 column;
    u32 file;
};

// A token no longer stores its spelling pointer or an eager source location.
// The spelling is `spelling_base + offset` for `length` bytes, where the base
// is the owning CLexResult's or CPreprocessResult's spelling space; the
// line/column/file location is recovered on demand from the same offset (see
// c_lex_token_location and c_preprocess_token_location).
typedef struct CToken CToken;
struct CToken
{
    u32 offset;
    u32 length;
    // For C_TOKEN_IDENTIFIER tokens, the symbol id the preprocessor's intern
    // pass assigned (0 = uninterned; consumers must keep a spelling-based
    // fallback for synthesized and test-built tokens). The symbol travels
    // with the spelling: every site that respells a token must re-intern.
    u32 symbol;
    // The #pragma pack alignment in effect where the token was emitted.
    u16 pack_alignment;
    // A CTokenKind.
    u8 kind;
    // A CPunctuator, narrowed to a byte.  It is C_PUNCTUATOR_NONE on every
    // token whose kind is not C_TOKEN_PUNCTUATOR, which is what lets
    // c_token_is_punctuator be one compare and skip the kind test.
    u8 punctuator;
};

BUSTER_CT_CHECK(sizeof(CToken) == 16);
BUSTER_CT_CHECK(C_PUNCTUATOR_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK(C_TOKEN_KIND_COUNT <= UINT8_MAX);

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

// What one lex consumed, measured in the units a human uses to describe
// source: bytes, lines, code, comments, whitespace. Every field falls out of
// a branch c_lex_space already takes, so measuring costs a counter update per
// whitespace byte, per comment and per line rather than a second pass.
//
// Two exact partitions hold per lex, and survive summation:
//   translated_bytes == code_bytes + comment_bytes + blank_bytes
//   translated_lines == code_lines + (comment_lines - mixed_lines) + blank_lines
// `code_bytes` is derived, not stored: translated_bytes - comment - blank.
//
// `bytes`/`lines` measure the file as written; `translated_bytes`/
// `translated_lines` measure what the lexer actually scanned, after
// translation phases 1-2 delete carriage returns and fold line splices.
// `spliced_lines` is the difference in lines, and the two byte counts differ
// by that plus the deleted carriage returns.
typedef struct CSourceMetrics CSourceMetrics;
struct CSourceMetrics
{
    // Lexed aggregates count one per inclusion, unique aggregates one per
    // distinct path; a single lex reports 1.
    u64 files;
    u64 bytes;
    u64 translated_bytes;
    u64 lines;
    u64 translated_lines;
    u64 spliced_lines;
    // sLOC: lines carrying at least one preprocessing token.
    u64 code_lines;
    u64 comment_lines;
    // Lines carrying both code and comment, counted in both totals above.
    u64 mixed_lines;
    u64 blank_lines;
    // Comment text including its delimiters, and the newlines inside a block
    // comment; those newlines are therefore not in blank_bytes.
    u64 comment_bytes;
    // Whitespace outside comments, newlines included.
    u64 blank_bytes;
    // String and character literal spellings; a subset of the derived
    // code_bytes.
    u64 literal_bytes;
    u64 comments;
    // Preprocessing tokens, excluding the newline markers and the end marker.
    u64 tokens;
};

// The other half of the frontend's work: what preprocessing produced, as
// against what it read. Macro expansion multiplies the source by whatever
// factor the macros ask for, so parsing and lowering scale with these numbers
// and not with the file sizes in CSourceMetrics.
typedef struct CPreprocessedMetrics CPreprocessedMetrics;
struct CPreprocessedMetrics
{
    // Tokens handed to the parser, the end marker excluded.
    u64 tokens;
    // Their spellings summed: the -E output with all layout removed.
    u64 bytes;
    // Every spelling byte the run retained — the translated files plus each
    // synthesized spelling (stringify, paste, builtins, expansion copies).
    // Its excess over the scanned bytes is what preprocessing invented.
    u64 spelling_bytes;
    // Macro replacements performed, and #define directives that defined them.
    u64 expansions;
    u64 definitions;
};

typedef struct CLexResult CLexResult;
struct CLexResult
{
    String8 translated_source;
    // Base pointer token offsets are relative to: translated_source.pointer
    // for a standalone c_lex, the preprocessor's shared spelling space when
    // the lex ran inside c_preprocess.
    char8 const* spelling_base;
    CToken* tokens;
    CDiagnostic* diagnostics;
    // Location checkpoints of the translated source, retained so a token's
    // line/column recover on demand from its offset: the original-source
    // location at each linearity break beside the translated offset of that
    // break (see c_translate_source in c.c).
    CSourceLocation* checkpoints;
    u32* checkpoint_offsets;
    u32 checkpoint_count;
    // Spelling-space offset of translated_source.pointer (0 standalone).
    u32 translated_offset;
    CSourceMetrics metrics;
    u64 token_count;
    u64 diagnostic_count;
    // Cursor into the checkpoints; location queries at non-decreasing
    // offsets amortize to one advance instead of a search.
    u32 location_cursor;
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

typedef struct CSymbolTable CSymbolTable;

// One region of the spelling space, keyed by its first offset; a region
// covers up to the next entry's start (entries are sorted by start, gaps
// belong to the preceding entry). FILE entries recover line/column through
// the owning lex result's checkpoints with the #line delta of the region;
// EXPANSION entries carry one location shared by every offset in the region
// (macro expansion output copies its spellings contiguously, so one entry
// covers one invocation's tokens).
typedef enum CSourceMapEntryKind
{
    C_SOURCE_MAP_FILE,
    C_SOURCE_MAP_EXPANSION,
} CSourceMapEntryKind;

typedef struct CSourceMapEntry CSourceMapEntry;
struct CSourceMapEntry
{
    u32 start;
    u32 file;
    CSourceLocation* checkpoints;
    u32* checkpoint_offsets;
    u32 checkpoint_count;
    u32 translated_offset;
    s64 line_delta;
    CSourceLocation location;
    CSourceMapEntryKind kind;
    u32 reserved;
};

// The spelling space of every preprocess result begins with this fixed
// prelude, so tokens synthesized after preprocessing (static-assert
// wrappers, lowering-internal constants, C23 respells) can reference
// well-known spellings at compile-time-known offsets.
#define C_SPELLING_PRELUDE_TEXT "_Static_assert();._Bool_Alignas_Alignof_Thread_local010.0f"
enum
{
    C_SPELLING_STATIC_ASSERT = 0,
    C_SPELLING_STATIC_ASSERT_LENGTH = 14,
    C_SPELLING_LEFT_PARENTHESIS = 14,
    C_SPELLING_RIGHT_PARENTHESIS = 15,
    C_SPELLING_SEMICOLON = 16,
    C_SPELLING_DOT = 17,
    C_SPELLING_BOOL = 18,
    C_SPELLING_BOOL_LENGTH = 5,
    C_SPELLING_ALIGNAS = 23,
    C_SPELLING_ALIGNAS_LENGTH = 8,
    C_SPELLING_ALIGNOF = 31,
    C_SPELLING_ALIGNOF_LENGTH = 8,
    C_SPELLING_THREAD_LOCAL = 39,
    C_SPELLING_THREAD_LOCAL_LENGTH = 13,
    C_SPELLING_ZERO = 52,
    C_SPELLING_ONE = 53,
    // "0.0f"; length 3 spells "0.0".
    C_SPELLING_FLOAT_ZERO = 54,
    C_SPELLING_PRELUDE_LENGTH = 58,
};

BUSTER_CT_CHECK(sizeof(C_SPELLING_PRELUDE_TEXT) - 1 == C_SPELLING_PRELUDE_LENGTH);

// Location-recovery state of one preprocess run: the sorted spelling-space
// regions, the page-bracket index (entry covering each 1 KB page's first
// byte) that turns lookups into one load and a short bounded search, and
// the commit-on-demand arena owning the spelling space. Behind one pointer
// because CPreprocessResult travels by value through the whole parse and
// lowering surface — growing that struct taxes every call.
typedef struct CSourceMapRecovery CSourceMapRecovery;
struct CSourceMapRecovery
{
    // Must outlive every consumer of the tokens; a caller compiling many
    // units may destroy it once the unit's compilation is complete.
    Arena* spelling_arena;
    CSourceMapEntry* entries;
    u32* pages;
    u32 count;
    u32 page_count;
    u32 reserved;
    u32 padding;
};

typedef struct CPreprocessResult CPreprocessResult;
struct CPreprocessResult
{
    CToken* tokens;
    // Base of the spelling space every token offset points into.
    char8 const* spelling_base;
    // Null for hand-built results; token locations then recover as zero.
    CSourceMapRecovery* recovery;
    // The identifier intern table the tokens' symbol ids point into; arena
    // resident so parse and lowering can intern on demand. Null for
    // hand-built results.
    CSymbolTable* symbols;
    CDiagnostic* diagnostics;
    String8* files;
    Target target;
    TargetDataLayout data_layout;
    // Every file the preprocessor lexed, summed twice: once per inclusion,
    // and once per distinct path. A header without #pragma once is re-read
    // and re-lexed at every #include, so their ratio is the translation
    // unit's include amplification.
    CSourceMetrics source_lexed;
    CSourceMetrics source_unique;
    CPreprocessedMetrics preprocessed;
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
    // Interned id of `name` (0 when the parse ran without a symbol table);
    // the scope-lookup buckets and chains key on it.
    u32 symbol;
    CSourceLocation location;
    CTypeId type;
    CScopeId scope;
    CEntityId next_in_scope;
    CEntityId next_in_lookup;
    CEntityId next_typedef_in_lookup;
    CEntityId next_by_name;
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

// (kind, tag) -> oldest matching aggregate type id. Slots can go stale when a
// speculative type parse rolls the result back, so lookups validate the
// recorded id against the live type table and fall back to the linear scan;
// staleness costs time, never a wrong answer. The header lives outside
// CParseResult because rollback restores that struct wholesale from a
// checkpoint copy while the slot storage keeps its contents; fill only ever
// grows, which is what guarantees probe termination under the half-full cap.
typedef struct CAggregateLookupSlot CAggregateLookupSlot;
struct CAggregateLookupSlot
{
    String8 tag;
    u32 kind;
    u32 type_index;
    bool used;
};

typedef struct CAggregateLookup CAggregateLookup;
struct CAggregateLookup
{
    CAggregateLookupSlot* slots;
    u32 slot_count;
    u32 fill;
    bool saturated;
};

// Sorted token positions of the rare spellings whose "does this range contain
// one" queries the type-parse machine re-asks per declaration scan. Built at
// most once per parse from the fixed token stream; the header lives outside
// the checkpointed CParseResult body via a stable pointer so speculative
// rollbacks can neither invalidate it nor force a rebuild.
typedef struct CTokenPositionIndex CTokenPositionIndex;
struct CTokenPositionIndex
{
    u32* vector_size_positions;
    u32* alignas_positions;
    // Per token: the position of the matching closer for every opening
    // (/[/{ whose whole group is properly nested across all three delimiter
    // kinds, else UINT32_MAX. A mismatched closer unmatches everything still
    // open, so scans over malformed regions keep their exact scalar walks.
    u32* matching_delimiters;
    u32 vector_size_count;
    u32 alignas_count;
    bool built;
};

typedef struct CParseResult CParseResult;
struct CParseResult
{
    // Borrowed owner of the result arrays.  It must outlive this result and is
    // never destroyed or rewound by the parser.
    Arena* arena;
    // Borrowed from the preprocess result (null for hand-built inputs); lets
    // the parse intern names on demand so entity lookups key on symbol ids.
    CSymbolTable* symbols;
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
    CEntityId* name_lookup_buckets;
    CAggregateLookup* aggregate_lookup;
    CTokenPositionIndex* position_index;
    CIdentifierUse* identifier_uses;
    u32* identifier_use_by_token;
    // Lazily computed per-token spelling-predicate bits, indexed like
    // identifier_use_by_token; see C_TOKEN_CLASS_* in c.c.
    u8* token_classes;
    // Children of each scope in ascending scope order, built by
    // c_parse_index_scope_children once scopes are final; zero when absent.
    // c_parse_scope_for_token descends this index instead of scanning every
    // scope when it is present.
    u32* scope_children_offsets;
    u32* scope_children;
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

// Fills the C frontend's remaining first-use tables on the calling thread.
// Call before lane_run; the tables are read without synchronization, so a
// gang that reaches one unwarmed reports through
// BUSTER_CHECK_SERIAL_INITIALIZATION instead of racing. compiler_prewarm()
// covers this along with the rest of the compiler.
BUSTER_F_DECL void c_prewarm(void);
BUSTER_F_DECL CLexResult c_lex(Arena* arena, String8 source);
// c_lex through the scalar reference loop, whatever the host supports. Only
// the differential gate in the tests calls it; production always goes through
// c_lex, which dispatches to the compaction emitter where it is available.
BUSTER_F_DECL CLexResult c_lex_reference(Arena* arena, String8 source);
BUSTER_F_DECL CPreprocessResult c_preprocess(Arena* arena, String8 source, CPreprocessOptions options);
BUSTER_F_DECL void c_source_metrics_add(CSourceMetrics* total, CSourceMetrics const* part);
// translated_bytes minus comments and whitespace: the bytes that became
// tokens, literal spellings included.
BUSTER_F_DECL u64 c_source_metrics_code_bytes(CSourceMetrics metrics);
// The spelling of a token relative to its owning result's spelling base.
BUSTER_F_DECL String8 c_token_spelling(char8 const* spelling_base, CToken token);
// On-demand line/column/file recovery. The lex variant serves standalone lex
// results (file always 0) and advances the result's amortization cursor; the
// preprocess variant binary-searches the source map and is safe on any token
// of the final stream.
BUSTER_F_DECL CSourceLocation c_lex_token_location(CLexResult* lex, CToken token);
BUSTER_F_DECL CSourceLocation c_preprocess_token_location(CPreprocessResult const* preprocess, CToken token);
BUSTER_F_DECL CParserResult c_parse_ast(Arena* arena, CPreprocessResult preprocess);
BUSTER_F_DECL CIRLowerResult c_analyze(Arena* arena, String8 source_path, CPreprocessResult preprocess, CParserResult syntax, Target target);
BUSTER_F_DECL CParseResult c_parse(Arena* arena, CPreprocessResult preprocess);
// Compatibility entry point for tests and callers that already own an analyzed model.
BUSTER_F_DECL CIRLowerResult c_lower_to_ir(Arena* arena, String8 source_path, CPreprocessResult preprocess, CAnalysisResult analysis, Target target);
BUSTER_F_DECL String8 c_token_kind_name(CTokenKind kind);
