#pragma once

// Public API of the C frontend. The pipeline is three stage functions, each
// consuming the previous stage's result: c_preprocess (tokens), c_parse_ast
// plus c_analyze_semantics (declarations, types, entities, scopes; c_parse
// runs both), and c_lower_to_ir via c_analyze (canonical IR). Results
// reference earlier-stage storage — callers keep the translation-unit arena
// alive until every downstream consumer is done. Invalid input reports
// CDiagnostic rows; assertions are reserved for internal invariants.

#include <buster/lib/arena.h>
#include <buster/lib/compiler/ir/model.h>
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

// A recovered position: `offset` is the byte index into the file, and
// `map_offset` is the spelling-space offset it was recovered from — the key
// a later lookup takes, and what an IrSourceRange stores in place of the
// line and column recovering them would have cost.
typedef struct CSourceLocation CSourceLocation;
struct CSourceLocation
{
    u32 offset;
    u32 line;
    u32 column;
    u32 file;
    u32 map_offset;
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
    // For C_TOKEN_IDENTIFIER tokens, the symbol id the preprocessor's intern
    // pass assigned (0 = uninterned; consumers must keep a spelling-based
    // fallback for synthesized and test-built tokens). The symbol travels
    // with the spelling: every site that respells a token must re-intern.
    u32 symbol;
    // Spelling byte count, or C_TOKEN_LENGTH_OVERSIZED when the spelling is
    // 0xFFFF bytes or longer. The sentinel is only ever stored for
    // terminated string and character literals — the one token shape that
    // can legitimately grow that large — whose exact length c_token_length
    // re-derives by walking the spelling to its closing delimiter, which is
    // the spelling's own last byte, so the scan never reads past it and is
    // safe on raw source text and packed synthesized copies alike. Every
    // other oversized shape is diagnosed and clamped at creation. Read the
    // field through c_token_length (or c_token_spelling): summing or
    // indexing the raw field would treat the sentinel as 65535.
    u16 length;
    // A CTokenKind.
    u8 kind;
    // A CPunctuator, narrowed to a byte.  It is C_PUNCTUATOR_NONE on every
    // token whose kind is not C_TOKEN_PUNCTUATOR, which is what lets
    // c_token_is_punctuator be one compare and skip the kind test.
    u8 punctuator;
};

#define C_TOKEN_LENGTH_OVERSIZED UINT16_MAX

BUSTER_CT_CHECK(sizeof(CToken) == 12);
BUSTER_CT_CHECK(C_PUNCTUATOR_COUNT <= UINT8_MAX);
BUSTER_CT_CHECK(C_TOKEN_KIND_COUNT <= UINT8_MAX);

// The parser asks about a token's shape far more often than it reads its
// spelling, offset, or symbol.  Keep that projection in one byte beside the
// final token stream: non-punctuators use their CTokenKind directly, while a
// punctuator carries a high-bit tag and its CPunctuator id in the low bits.
// The tag deliberately leaves the CToken ABI untouched; rows remain the
// 12-byte public record consumed by preprocessing and lowering callers.
typedef u8 CTokenShape;
enum
{
    C_TOKEN_SHAPE_PUNCTUATOR = UINT8_C(0x80),
    C_TOKEN_SHAPE_PUNCTUATOR_MASK = UINT8_C(0x7f),
};

BUSTER_CT_CHECK((u32)C_PUNCTUATOR_COUNT <= (u32)C_TOKEN_SHAPE_PUNCTUATOR_MASK);
BUSTER_CT_CHECK((u32)C_TOKEN_KIND_COUNT < (u32)C_TOKEN_SHAPE_PUNCTUATOR);

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenShape c_token_shape_from_fields(CTokenKind kind, CPunctuator punctuator)
{
    CTokenShape result = kind == C_TOKEN_PUNCTUATOR ? (CTokenShape)(C_TOKEN_SHAPE_PUNCTUATOR | (u8)punctuator) : (CTokenShape)kind;
    return result;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenShape c_token_shape_from_token(CToken token)
{
    return c_token_shape_from_fields((CTokenKind)token.kind, (CPunctuator)token.punctuator);
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool c_token_shape_is_punctuator(CTokenShape shape)
{
    return (shape & C_TOKEN_SHAPE_PUNCTUATOR) != 0;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenKind c_token_shape_kind(CTokenShape shape)
{
    return c_token_shape_is_punctuator(shape) ? C_TOKEN_PUNCTUATOR : (CTokenKind)shape;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CPunctuator c_token_shape_punctuator(CTokenShape shape)
{
    return c_token_shape_is_punctuator(shape) ? (CPunctuator)(shape & C_TOKEN_SHAPE_PUNCTUATOR_MASK) : C_PUNCTUATOR_NONE;
}

// A set of punctuators as a 64-bit mask: bit `p` is a member test for
// CPunctuator `p`.  Every id is a valid bit position because the whole enum
// fits in 64 values, which the check below holds it to; adding a 65th
// punctuator fails the build rather than silently truncating a set.
// C_PUNCTUATOR_NONE is bit 0, so a set that leaves bit 0 clear answers "is
// this token one of these punctuators" with no kind test — the same
// invariant c_token_is_punctuator relies on.
#define C_PUNCTUATOR_BIT(punctuator) ((u64)1 << (punctuator))
BUSTER_CT_CHECK(C_PUNCTUATOR_COUNT <= 64);

// One shift and one mask answer what a chain of dependent compares answers.
// Callers that ask several punctuator questions about the same token pay one
// serial branch ladder per token otherwise; this is the classification they
// gate on instead.  Inline for the same reason c_token_length is: an
// out-of-line call in a per-token loop costs more than the body, and it
// perturbs the caller's register allocation in every copy.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool c_punctuator_in_set(u8 punctuator, u64 set)
{
    return ((set >> punctuator) & 1) != 0;
}

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
    C_DIAGNOSTIC_INVALID_BIT_FIELD_WIDTH,
    C_DIAGNOSTIC_EXPECTED_DECLARATION,
    C_DIAGNOSTIC_UNMATCHED_DELIMITER,
    C_DIAGNOSTIC_CONFLICTING_DECLARATION,
    C_DIAGNOSTIC_REDEFINITION,
    C_DIAGNOSTIC_UNDECLARED_IDENTIFIER,
    C_DIAGNOSTIC_STATIC_ASSERT_NOT_CONSTANT,
    C_DIAGNOSTIC_STATIC_ASSERT_FAILED,
    C_DIAGNOSTIC_INVALID_CONSTEXPR,
    C_DIAGNOSTIC_INVALID_CLEANUP_ATTRIBUTE,
    C_DIAGNOSTIC_INVALID_VOID_OBJECT,
    C_DIAGNOSTIC_UNSUPPORTED_SEMANTICS,
    C_DIAGNOSTIC_PREPROCESSOR_ERROR,
    C_DIAGNOSTIC_PREPROCESSOR_WARNING,
    // An identifier or preprocessing number of 0xFFFF bytes or more: the
    // token length field cannot represent it and only literals carry the
    // oversized escape, so the token is diagnosed and its length clamped.
    C_DIAGNOSTIC_TOKEN_TOO_LONG,
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

// One distinct path of a unit's include closure, with how many times its
// bytes were actually lexed and the scanned size of one lex. A `lex_count`
// above one attributes the unit's include amplification — nothing suppressed
// that file's re-inclusion — and `(lex_count - 1) * translated_bytes` of the
// lexed aggregate is what re-reading it cost. Suppressed re-inclusions
// (#pragma once, #import, a recognized include guard) do not count: the rows
// sum to the lexed aggregate, not to the #include directives reached. This
// is deliberately not the token-carrying file table next to `map_entry`
// (see c_source.c): that one is populated lazily so token-free headers never
// enter the program's source map, while attribution needs every lexed file.
typedef struct CSourceFileMetrics CSourceFileMetrics;
struct CSourceFileMetrics
{
    String8 path;
    u64 translated_bytes;
    u32 lex_count;
    u32 reserved;
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

// The facts a translation unit consults a handful of times, held out of the
// row every frontend call carries. CPreprocessResult is passed by value
// through the parser and the IR lowering — roughly a hundred and fifty
// by-value parameters, one copy per type-parse frame push, one per constant
// initializer — and these six members were 548 of its 712 bytes while the hot
// paths read only `tokens`, `spelling_base`, `target` and `dialect`. The data
// layout is read three times per compile and the measurement tables only by
// `-v` reporting and `-fsource-metrics=`, so they ride one pointer instead of
// every copy.
//
// Null for hand-built results (the token-space literals the integer-constant
// evaluator takes); `c_preprocess_detail` reads those as the all-zero block
// their inline members used to be.
typedef struct CPreprocessDetail CPreprocessDetail;
struct CPreprocessDetail
{
    TargetDataLayout data_layout;
    // Every file the preprocessor lexed, summed twice: once per inclusion,
    // and once per distinct path. A header with neither #pragma once nor a
    // recognized whole-file include guard is re-read and re-lexed at every
    // #include, so their ratio is the translation unit's include
    // amplification, and `lexed_files` attributes it per path.
    CSourceMetrics source_lexed;
    CSourceMetrics source_unique;
    CSourceFileMetrics* lexed_files;
    CPreprocessedMetrics preprocessed;
    u32 lexed_file_count;
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
    // One byte per token, in the same order as `tokens`: raw CTokenKind for
    // non-punctuators and a tagged CPunctuator for punctuators.  The
    // preprocessor copies this projection into its final private stream.
    CTokenShape* token_shapes;
    CDiagnostic* diagnostics;
    // Location checkpoints of the translated source, retained so a token's
    // line/column recover on demand from its offset: the original-source
    // location at each linearity break beside the translated offset of that
    // break (see c_translate_source in c.c).
    IrSourceCheckpoint* checkpoints;
    u32* checkpoint_offsets;
    // Page bracket over the checkpoints (see IrSourceRegion).
    u32* checkpoint_pages;
    u32 checkpoint_page_count;
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
    // The replacement list, taken verbatim: an empty value defines an
    // object-like macro that expands to nothing, the way `-DNAME=` does. A
    // caller that wants the `1` a valueless `-DNAME` means must spell it.
    String8 value;
};

typedef enum CPreprocessDialect
{
    C_PREPROCESS_DIALECT_GNU17,
    C_PREPROCESS_DIALECT_GNU99,
    C_PREPROCESS_DIALECT_GNU11,
    C_PREPROCESS_DIALECT_GNU23,
    C_PREPROCESS_DIALECT_C99,
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
    // GNU-as sources use `#` for line comments, so a `.S` run treats a `#`
    // line whose word is no known directive as a comment instead of an
    // error; conditionals, defines and includes keep their meaning.
    bool assembly_comment_lines;
    u8 reserved[2];
};

typedef struct CSymbolTable CSymbolTable;

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

// Location-recovery state of one preprocess run: the source map over the
// spelling space, and the commit-on-demand arena owning that space. Behind
// one pointer because CPreprocessResult travels by value through the whole
// parse and lowering surface — growing that struct taxes every call.
//
// The map partitions the space into IrSourceRegions: TEXT regions recover
// line/column through the owning lex result's checkpoints with the #line
// delta of the region, and STAMP regions carry one position shared by every
// offset in them (macro expansion output copies its spellings contiguously,
// so one region covers one invocation's tokens). The preprocessor and every
// IR consumer read it through the same `ir_source_map_position`, so a
// token's position and the position an IR range resolves to are one lookup
// over one structure — the program keeps a pointer to this very map.
typedef struct CSourceMapRecovery CSourceMapRecovery;
struct CSourceMapRecovery
{
    // Must outlive every consumer of the tokens; a caller compiling many
    // units may destroy it once the unit's compilation is complete.
    Arena* spelling_arena;
    // Owns the result's token array itself: the preprocessor streams output
    // tokens into a private arena so each lands in its final slot once.
    // Same lifetime contract as the spelling arena — destroy both together.
    Arena* token_arena;
    // Owns the one-byte kind|punctuator projection beside the token rows.
    // Keeping it in a separate private arena preserves the row stream's
    // contiguous layout while giving parser shape walks a linear byte scan.
    Arena* token_shape_arena;
    CTokenShape* token_shapes;
    IrSourceMap map;
    // Region-array capacity, kept across the respell pass that may append.
    u32 capacity;
    u32 reserved;
};

// One entry of the #pragma pack change list: `alignment` is in effect for
// every final-stream token from index `token_index` until the next entry.
// The preprocessor appends an entry whenever the pack state at output-append
// time differs from the last entry (a _Pragma can change it mid-expansion,
// so the state is sampled as tokens land in the final stream, not at lex
// time), which keeps the list sorted and deduplicated by construction;
// c_preprocess_pack_alignment binary-searches it.
typedef struct CPackAlignment CPackAlignment;
struct CPackAlignment
{
    u32 token_index;
    u32 alignment;
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
    // Sorted (token index, alignment) spans replacing a per-token field;
    // null with count 0 for hand-built results, which query as alignment 0.
    CPackAlignment* pack_changes;
    // The data layout and the measurement tables; see CPreprocessDetail for
    // why they are not members here. Null for hand-built results.
    CPreprocessDetail* detail;
    Target target;
    u64 token_count;
    u64 diagnostic_count;
    u64 error_count;
    u64 warning_count;
    u64 diagnostic_capacity;
    u32 file_count;
    CPreprocessDialect dialect;
    u32 pack_change_count;
};

// Hand-built results carry no detail block and read as an all-zero one, which
// is exactly what their inline members held before the split.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CPreprocessDetail const* c_preprocess_detail(CPreprocessResult preprocess)
{
    static CPreprocessDetail const c_preprocess_detail_empty = {0};
    return preprocess.detail ? preprocess.detail : &c_preprocess_detail_empty;
}

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
    // The three C99 complex types. Each is laid out as two contiguous
    // elements of its underlying real type -- real part first -- which is
    // both what the psABIs specify and what lets the IR model them as
    // two-field aggregates (see c_ir_scalar_type).
    C_TYPE_FLOAT_COMPLEX,
    C_TYPE_DOUBLE_COMPLEX,
    C_TYPE_LONG_DOUBLE_COMPLEX,
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

// The complex kinds and their underlying real kind, in both directions. Every
// complex question in the frontend goes through these three so the mapping
// lives in one place; `c_type_kind_complex_of` returns C_TYPE_INVALID for a
// real kind that has no complex counterpart (C only defines the three).
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE bool c_type_kind_is_complex(CTypeKind kind)
{
    return kind == C_TYPE_FLOAT_COMPLEX || kind == C_TYPE_DOUBLE_COMPLEX || kind == C_TYPE_LONG_DOUBLE_COMPLEX;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTypeKind c_type_kind_complex_element(CTypeKind kind)
{
    return kind == C_TYPE_FLOAT_COMPLEX         ? C_TYPE_FLOAT
           : kind == C_TYPE_DOUBLE_COMPLEX      ? C_TYPE_DOUBLE
           : kind == C_TYPE_LONG_DOUBLE_COMPLEX ? C_TYPE_LONG_DOUBLE
                                                : C_TYPE_INVALID;
}

BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTypeKind c_type_kind_complex_of(CTypeKind kind)
{
    return kind == C_TYPE_FLOAT         ? C_TYPE_FLOAT_COMPLEX
           : kind == C_TYPE_DOUBLE      ? C_TYPE_DOUBLE_COMPLEX
           : kind == C_TYPE_LONG_DOUBLE ? C_TYPE_LONG_DOUBLE_COMPLEX
                                        : C_TYPE_INVALID;
}

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
    // The scope a tagged aggregate was declared in, so two block-scope
    // definitions of one tag in sibling functions stay two types -- clang's
    // xmmintrin.h defines `struct __mm_storeh_pi_struct` inside two
    // intrinsics -- and a reference resolves to the innermost visible one.
    // Meaningful only for struct/union/enum types with a tag; everything
    // else leaves it zero, which is the file scope.
    CScopeId tag_scope;
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
    // GNU's transparent_union: a parameter of this union type accepts an
    // argument of any member's type, passed as that member's own bits.
    bool is_transparent_union;
    bool has_unqualified_type;
    // The function type was written `()`, with no parameter list at all --
    // not `(void)`, which declares zero parameters. C11 6.2.7p3 makes an
    // unprototyped declaration compatible with a non-variadic prototype, so
    // musl's `long __syscall_cp_asm();` and the eight-parameter prototype
    // beside it declare one function rather than two conflicting ones.
    bool is_unprototyped;
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
    // A GNU `packed` attribute on this member alone
    // (`struct { char c; int i __attribute__((packed)); }`): the member is
    // placed at byte alignment, which also stops it from raising the
    // aggregate's own alignment.
    bool is_packed;
    u8 reserved[2];
};

// One `_Alignas(...)` or GNU `aligned(...)` request, as either the type it
// names or the token range of its constant expression. Which of the two
// spellings wrote it is not stored: token_start is always two tokens past the
// keyword -- the keyword and its `(` -- so c_alignment_specifier_is_standard
// reads it back out of the token stream, and this table is sized at one record
// per token of the translation unit, where a flag word would commit four more
// bytes per token for a fact only a rejected request ever asks about.
typedef struct CAlignmentSpecifier CAlignmentSpecifier;
struct CAlignmentSpecifier
{
    CTypeId type;
    u32 token_start;
    u32 token_count;
};

// The GNU attributes a struct or union definition carries on the definition
// itself, either before the tag (`struct __attribute__((packed)) P { ... }`)
// or after the closing brace (`struct P { ... } __attribute__((packed));`).
// They live in a side table rather than in CType because the population is a
// handful of aggregates in a translation unit while the type table is tens of
// thousands of entries: a flag word per type would grow every one of them.
// Both layout engines -- the sizeof folding in c_parse.c and the IR layout in
// c_gen.c -- read this table, and they must agree or a folded sizeof
// contradicts the object it sizes.
typedef struct CAggregateAttributes CAggregateAttributes;
struct CAggregateAttributes
{
    u32 type_index;
    // Range into CParseResult.alignments, as CMember/CDeclaration name theirs.
    u32 alignment_start;
    u32 alignment_count;
    bool is_packed;
    u8 reserved[3];
};

// The alignment a declarator wrote on the *type* it declares rather than on an
// object it declares: `typedef int cache_line __attribute__((aligned(64)))`.
// GNU `aligned` there sets the alignment of the type, so unlike every other
// alignment record in this file it *replaces* the natural alignment instead of
// raising it, and it is the one place the attribute lowers one without
// `packed` -- `typedef int pair __attribute__((aligned(2)))` is two-byte
// aligned in both GCC and Clang. The record keys on the alias copy the
// declarator made, never on the type it aliases, so `int` keeps its own
// alignment -- and on every qualified copy of that alias, because a qualifier
// cannot take an alignment away and the qualified copy points past the alias
// at the type it strips to (#714). It lives in a side table for the reason
// CParseResult.noreturn_function_types does: the population is a handful of
// typedefs per translation unit against a type table of tens of thousands of
// entries, so a field on CType would cost every one of them.
typedef struct CTypeAlignment CTypeAlignment;
struct CTypeAlignment
{
    u32 type_index;
    // Range into CParseResult.alignments, as CMember/CDeclaration name theirs.
    u32 alignment_start;
    u32 alignment_count;
};

typedef struct CEnumMember CEnumMember;
struct CEnumMember
{
    String8 name;
    CSourceLocation location;
    // Interned id of `name`, carried from the declaring token so the entity
    // filed from this record is keyed without a second intern; 0 when the
    // parse ran without a symbol table.  It sits in the alignment hole
    // ahead of `value`, so the record's size is unchanged.
    u32 symbol;
    u64 value;
    bool is_negative;
    u8 reserved[7];
};
BUSTER_CT_CHECK(sizeof(CEnumMember) == 56);

typedef struct CParameter CParameter;
struct CParameter
{
    String8 name;
    CSourceLocation location;
    CTypeId type;
    CEntityId entity;
    // Interned id of `name`, as CEnumMember.symbol; fills the tail padding.
    u32 symbol;
};
BUSTER_CT_CHECK(sizeof(CParameter) == 48);

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
    // Register storage on a local. Only the local declarator path records it,
    // and only one reader needs it: an assembler label on a `register` local
    // binds a machine register instead of renaming a symbol.
    bool is_register;
    u8 reserved[1];
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
    // The one declarator this declaration owns out of a comma-separated list,
    // initializer included and the separating ',' or ';' excluded. Zero count
    // means the declaration was not split and owns its whole token range;
    // token_start/token_count always span the declaration specifiers, which
    // every declarator of the list shares.
    u32 declarator_start;
    u32 declarator_count;
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
    CDeclarationKind kind;
    bool is_definition;
    bool is_variadic;
    bool is_constexpr;
    // Set on the second and later declarators of a list: the specifiers were
    // already parsed for the first one, so base_type is supplied rather than
    // recomputed.
    bool is_declarator_continuation;
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

typedef struct CParserExpression CParserExpression;
struct CParserExpression
{
    u32 token_start;
    u32 token_count;
};

// One _Static_assert statement of a function body, at any block depth: the
// statement's token range, a leading C23 attribute sequence included, listed
// in the order the body reads.  It is the only fact the syntax pass keeps
// about a body statement -- the semantic pass rebinds bodies from their
// tokens, and the _Static_assert checks, which run against the block scopes
// that binding creates, were the one consumer of the per-statement tree the
// pass used to build.
typedef struct CParserStaticAssert CParserStaticAssert;
struct CParserStaticAssert
{
    CParserStaticAssert* next;
    u32 token_start;
    u32 token_count;
};

struct CParserDeclaration
{
    CParserDeclaration* next;
    CSourceLocation location;
    u32 token_start;
    u32 token_count;
    // See CDeclaration: one declarator of a comma-separated list, or a zero
    // count when the declaration was not split.
    u32 declarator_start;
    u32 declarator_count;
    u32 body_start;
    u32 body_token_count;
    u32 name_token;
    u32 function_name_token;
    // The body's _Static_assert statements in body order; null for the
    // body that has none, which is nearly every body.
    CParserStaticAssert* first_static_assert;
    CParserStaticAssert* last_static_assert;
    CParserExpression expression;
    CParserDeclarationKind kind;
    bool is_definition;
    bool is_typedef;
    bool is_constexpr;
    bool is_variadic;
    bool seen_equal;
    bool is_declarator_continuation;
    u8 reserved[2];
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
    // A second live type was recorded under this (kind, tag): the slot's one
    // id can no longer answer alone, so lookups fall to the linear scan and
    // resolve by scope.  Never cleared -- a rollback that removes the second
    // type leaves the flag costing a scan, which is the same time-not-answers
    // contract staleness already has.
    bool multiple;
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
    // Ascending positions of every identifier token directly followed by a
    // ':' punctuator — the necessary condition c_ir_named_label_at tests
    // before its out-of-line proof. Label-table sizing and dead-code label
    // scans enumerate these instead of re-asking the condition of every
    // body token; the population includes ternary and bit-field colons, so
    // consumers still run the proof per candidate.
    u32* label_candidate_positions;
    // Ascending positions of every __attribute__/__attribute identifier, so
    // the once-per-parse unattached-cleanup validation visits attributes
    // instead of re-classifying the whole stream.
    u32* attribute_positions;
    // Per token: the position of the matching closer for every opening
    // (/[/{ whose whole group is properly nested across all three delimiter
    // kinds, else UINT32_MAX. A mismatched closer unmatches everything still
    // open, so scans over malformed regions keep their exact scalar walks.
    u32* matching_delimiters;
    u32 vector_size_count;
    u32 alignas_count;
    u32 label_candidate_count;
    u32 attribute_count;
    // Delimiter scan verdicts that matching_delimiters alone cannot carry:
    // closers that matched nothing (mismatched or excess) plus openers still
    // unmatched at the end of the stream. Zero means the whole stream is
    // properly nested, which is what lets a consumer trust the array for any
    // sub-range without re-scanning it; non-zero sends malformed streams back
    // to their exact scalar walks.
    u32 delimiter_mismatch_count;
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
    CAggregateAttributes* aggregate_attributes;
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
    // The function types a declarator spelled `noreturn` on: the attribute
    // written on a function pointer or a typedef rather than on a function
    // declaration. It lives beside the type table instead of as a bit inside
    // CType because the set is empty in almost every translation unit, while
    // the type table is large and walked linearly -- a ninth bool would cost
    // every CType eight bytes to answer a question that is almost always no.
    // c_parse_type_is_noreturn is the only reader.
    CTypeId* noreturn_function_types;
    // The types a typedef declarator wrote `aligned(N)` on; see CTypeAlignment.
    // c_parse_type_alignment is the only reader.
    CTypeAlignment* type_alignments;
    u32 declaration_count;
    u32 type_count;
    u32 parameter_count;
    u32 member_count;
    u32 enum_member_count;
    u32 array_bound_count;
    u32 alignment_count;
    u32 aggregate_attribute_count;
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
    u32 aggregate_attribute_capacity;
    u32 entity_capacity;
    u32 scope_capacity;
    u32 entity_lookup_bucket_count;
    u32 identifier_use_capacity;
    u32 identifier_use_by_token_capacity;
    u32 diagnostic_capacity;
    u32 deferred_static_assert_capacity;
    u32 noreturn_function_type_count;
    u32 noreturn_function_type_capacity;
    u32 type_alignment_count;
    u32 type_alignment_capacity;
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
    // Published only after the C lowerer finishes every function and global
    // through its typed builders without a rejected row. The driver may trust
    // this private producer boundary; public/manual IR still uses the full
    // canonical validator.
    bool canonical_ir_certified;
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
// The cold half of c_token_length: the exact byte count of a spelling whose
// length field carries the sentinel, re-derived by the delimiter scan.
BUSTER_F_DECL u64 c_token_length_oversized(char8 const* spelling_base, CToken token);
// The spelling byte count: the field, or the oversized re-derivation. The
// guard runs on every hot spelling read, so the fast path is inlined here —
// left out of line, the call alone measured +4.5% of stage-1 instructions.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE u64 c_token_length(char8 const* spelling_base, CToken token)
{
    return token.length != C_TOKEN_LENGTH_OVERSIZED ? token.length : c_token_length_oversized(spelling_base, token);
}
// The spelling of a token relative to its owning result's spelling base.
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE String8 c_token_spelling(char8 const* spelling_base, CToken token)
{
    return (String8){
        .pointer = (char8*)spelling_base + token.offset,
        .length = c_token_length(spelling_base, token),
    };
}
// The #pragma pack alignment in effect at a final-stream token index: the
// greatest pack_changes entry at or before it, 0 (natural alignment) before
// the first entry or when the result carries no list.
BUSTER_F_DECL u32 c_preprocess_pack_alignment(CPreprocessResult const* preprocess, u64 token_index);
// On-demand line/column/file recovery. The lex variant serves standalone lex
// results (file always 0) and advances the result's amortization cursor; the
// preprocess variant binary-searches the source map and is safe on any token
// of the final stream.
BUSTER_F_DECL CSourceLocation c_lex_token_location(CLexResult* lex, CToken token);
BUSTER_F_DECL CSourceLocation c_preprocess_token_location(CPreprocessResult const* preprocess, CToken token);
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenShape const* c_preprocess_token_shapes(CPreprocessResult const* preprocess)
{
    CTokenShape const* result = preprocess && preprocess->recovery ? preprocess->recovery->token_shapes : 0;
    return result;
}
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenShape c_preprocess_token_shape(CPreprocessResult const* preprocess, u32 token_index)
{
    CTokenShape const* shapes = c_preprocess_token_shapes(preprocess);
    CTokenShape result = C_TOKEN_INVALID;
    if (preprocess)
    {
        result = shapes ? shapes[token_index] : preprocess->tokens ? c_token_shape_from_token(preprocess->tokens[token_index]) : C_TOKEN_INVALID;
    }
    return result;
}
BUSTER_GLOBAL_LOCAL BUSTER_UNUSED_DECL BUSTER_INLINE CTokenShape c_preprocess_token_shape_at(CTokenShape const* shapes,
                                                                                             CPreprocessResult const* preprocess,
                                                                                             u32 token_index)
{
    return shapes ? shapes[token_index] : preprocess && preprocess->tokens ? c_token_shape_from_token(preprocess->tokens[token_index]) : C_TOKEN_INVALID;
}
// The definition attributes recorded for an aggregate type, or an all-zero
// record when it carries none. The table holds one entry per attributed
// aggregate and is empty in almost every translation unit, so the scan is a
// count test in the common case.
BUSTER_F_DECL CAggregateAttributes c_parse_aggregate_attributes(CParseResult const* result, CTypeId type);
// The alignment run a typedef declarator asked for on this type, or a null
// pointer when it asked for none. The table is empty in almost every
// translation unit, so the scan is a count test in the common case.
BUSTER_F_DECL CTypeAlignment const* c_parse_type_alignment(CParseResult const* result, CTypeId type);
BUSTER_F_DECL CParserResult c_parse_ast(Arena* arena, CPreprocessResult preprocess);
BUSTER_F_DECL void c_parse_position_index_ensure(CParseResult* result, CPreprocessResult preprocess);
BUSTER_F_DECL CIRLowerResult c_analyze(Arena* arena, String8 source_path, CPreprocessResult preprocess, CParserResult syntax, Target target);
BUSTER_F_DECL CParseResult c_parse(Arena* arena, CPreprocessResult preprocess);
// Compatibility entry point for tests and callers that already own an analyzed model.
BUSTER_F_DECL CIRLowerResult c_lower_to_ir(Arena* arena, String8 source_path, CPreprocessResult preprocess, CAnalysisResult analysis, Target target);
BUSTER_F_DECL String8 c_token_kind_name(CTokenKind kind);
