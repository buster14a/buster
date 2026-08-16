#pragma once

/*
 * Private implementation contract for the GNU C frontend.
 *
 * c.h is the public front door.  This header is intentionally private to the
 * three implementation translation units; it carries the shared runtime
 * includes and the linkage policy used when those units are built separately.
 * The unity aggregator includes the same header so the three files retain one
 * optimized translation unit in Release.
 */
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/simd.h>
#include <buster/lib/string.h>

#if BUSTER_SIMD_512 && !defined(__BUSTER__)
#define BUSTER_C_LEX_COMPACT 1
#else
#define BUSTER_C_LEX_COMPACT 0
#endif

#if BUSTER_CPU_ARCH_X86_64 && defined(__AVX512F__) && defined(__AVX512BW__) && !defined(__BUSTER__) && !BUSTER_COMPILER_MSVC
#define BUSTER_C_TRANSLATE_AVX512 1
#else
#define BUSTER_C_TRANSLATE_AVX512 0
#endif

#if BUSTER_C_LEX_COMPACT || BUSTER_C_TRANSLATE_AVX512
#include <immintrin.h>
#endif

#if BUSTER_UNITY_BUILD
#define BUSTER_C_INTERNAL BUSTER_GLOBAL_LOCAL
#define BUSTER_C_SHARED BUSTER_GLOBAL_LOCAL
#define BUSTER_C_INLINE BUSTER_GLOBAL_LOCAL
#define BUSTER_C_EXTERN BUSTER_GLOBAL_LOCAL
#define BUSTER_C_DATA BUSTER_GLOBAL_LOCAL
#else
#define BUSTER_C_INTERNAL static
#define BUSTER_C_SHARED
#define BUSTER_C_INLINE static
#define BUSTER_C_EXTERN extern
#define BUSTER_C_DATA static
#endif

typedef struct CTypeParseMachine CTypeParseMachine;
typedef struct CIrDecodedString CIrDecodedString;
#define C_DECLARATION_KEYWORD_SLOT_COUNT 256

/* Source/preprocessor tables consumed by the parser's keyword classifier. */
BUSTER_C_EXTERN String8 const c_declaration_keyword_spellings[67];
BUSTER_C_EXTERN u8 c_declaration_keyword_slots[C_DECLARATION_KEYWORD_SLOT_COUNT];
BUSTER_C_EXTERN bool c_declaration_keyword_slots_built;
BUSTER_C_EXTERN void c_declaration_keyword_slots_build(void);
BUSTER_C_EXTERN u64 c_macro_name_hash(String8 name);
BUSTER_C_EXTERN bool c_preprocess_dialect_is_c23(CPreprocessDialect dialect);
BUSTER_C_EXTERN bool c_preprocess_dialect_is_gnu(CPreprocessDialect dialect);
BUSTER_C_EXTERN bool c_conditional_number(String8 spelling, u64* value);
BUSTER_C_EXTERN bool c_parse_auto_type_word(String8 spelling);
BUSTER_C_EXTERN bool c_parse_type_word_for_dialect(String8 spelling, CPreprocessDialect dialect);
BUSTER_C_EXTERN bool c_parse_alignof_word(String8 spelling);
BUSTER_C_EXTERN u8 c_parse_token_class_compute(String8 spelling);
BUSTER_C_EXTERN bool c_ir_decode_character_value(Arena* arena, char8 const* spelling_base, CToken token, Target target,
                                                   u64* value_out, CTypeKind* kind_out);
BUSTER_C_EXTERN bool c_ir_tokens_are_string_literals(CPreprocessResult preprocess, u32 start, u32 end);
BUSTER_C_EXTERN bool c_ir_named_label_at(CPreprocessResult const* preprocess, u32 body_start, u32 index, u32 body_end);
BUSTER_C_EXTERN bool c_ir_decode_string_literal_range_for_target(Arena* arena, CPreprocessResult preprocess, Target target,
                                                                  u32 start, u32 end, CIrDecodedString* decoded_out);
BUSTER_C_EXTERN String8 c_ir_unsupported_gnu_construct(CPreprocessResult preprocess, u32 start, u32 end, u32* token_index_out);
BUSTER_C_EXTERN CTypeKind c_ir_primitive_type_kind(CPreprocessResult preprocess, u32 start, u32 end, u32* declarator_start);
BUSTER_C_EXTERN u32 c_symbol_intern(CSymbolTable* table, String8 name);
BUSTER_C_EXTERN bool c_type_parse_buffer_size_add(u64* size, u64 count, u64 element_size, u64 alignment);
BUSTER_C_EXTERN void c_parse_declaration_type(CTypeParseMachine* machine, CParseResult* result, CPreprocessResult preprocess,
                                                CDeclaration* declaration, CTypeId inherited_base);
BUSTER_C_EXTERN bool c_parse_validate_constexpr_declaration(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                            CPreprocessResult preprocess, CDeclaration* declaration);
BUSTER_C_EXTERN CTypeId c_parse_add_type(CParseResult* result, CType type);
BUSTER_C_EXTERN bool c_parse_validate_constexpr_initializer(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                            CPreprocessResult preprocess, CScopeId scope, CEntityId entity_id,
                                                            u32 initializer_start, u32 initializer_end);
BUSTER_C_EXTERN void c_parse_infer_file_array_bounds(CTypeParseMachine* machine, Arena* arena,
                                                       CPreprocessResult preprocess, CParseResult* result);
BUSTER_C_EXTERN void c_parse_static_assert_check(CTypeParseMachine* machine, Arena* arena,
                                                  CPreprocessResult preprocess, CParseResult* result,
                                                  CDeclaration declaration, CScopeId scope);
BUSTER_C_EXTERN void c_parse_bind_function_body(CTypeParseMachine* machine, Arena* result_arena,
                                                 CParseResult* result, CPreprocessResult preprocess,
                                                 u32 declaration_index);
BUSTER_C_EXTERN void c_parse_validate_unattached_cleanup_attributes(CParseResult* result,
                                                                      CPreprocessResult preprocess);
BUSTER_C_EXTERN u64 c_parse_name_hash(u32 symbol, String8 name);
BUSTER_C_EXTERN u32 c_parse_name_symbol(CParseResult* result, String8 name);
BUSTER_C_EXTERN bool c_parse_types_compatible(Arena* result_arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right);
BUSTER_C_EXTERN void c_parse_scope_add_entity(CParseResult* result, CScopeId scope, CEntityId entity);
BUSTER_C_EXTERN CTypeId c_parse_pointer_chain(CParseResult* result, CPreprocessResult preprocess, CTypeId base, u32* index, u32 end);
BUSTER_C_EXTERN u32 c_parse_skip_attributes(CPreprocessResult preprocess, u32 index, u32 end);
BUSTER_C_EXTERN CTypeId c_parse_array_suffixes(CParseResult* result, CPreprocessResult preprocess, CTypeId element_type, u32* index, u32 end);
BUSTER_C_EXTERN CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name);
BUSTER_C_EXTERN CEntityId c_parse_lookup_typedef_name(CParseResult* result, String8 name, bool oldest);
BUSTER_C_EXTERN u32 c_parse_identifier_use_index(CParseResult* result, u32 token_index);
BUSTER_C_EXTERN CEntity* c_parse_first_constant_entity(CParseResult* result, String8 name);
BUSTER_C_EXTERN void c_parse_index_scope_children(CParseResult* result, Arena* arena);
BUSTER_C_EXTERN CEntityId c_parse_lookup_entity_at(CParseResult* result, CPreprocessResult preprocess, CScopeId scope,
                                                    String8 name, u32 token_index);
BUSTER_C_EXTERN bool c_parse_result_reserve_types(CParseResult* result, u32 additional);
BUSTER_C_EXTERN void c_type_parse_rollback(CTypeParseMachine* machine, CParseResult* result,
                                             CParseResult checkpoint, u32 mutation_mark);
BUSTER_C_EXTERN bool c_initializer_consume_separator(CToken* tokens, u32 limit, u32* cursor, u64 next_index);
BUSTER_C_EXTERN bool c_initializer_has_top_level_comma(CToken* tokens, u32 start, u32 end);
BUSTER_C_EXTERN CIRLowerResult c_lower_to_ir(Arena* arena, String8 source_path, CPreprocessResult preprocess,
                                              CAnalysisResult parse, Target target);
BUSTER_C_EXTERN CEntityId c_parse_lookup_entity_token(CParseResult* result, char8 const* spelling_base,
                                                       CScopeId scope, CToken const* token);
BUSTER_C_EXTERN CScopeId c_parse_scope_for_token(CParseResult* result, CScopeId root, u32 token_index);
BUSTER_C_EXTERN bool c_parse_clone_incomplete_array_declarator(CTypeParseMachine* machine, CParseResult* result, CTypeId type, CTypeId* type_out);
BUSTER_C_EXTERN void c_parse_diagnostic(CParseResult* result, CSourceLocation location, CDiagnosticKind kind, String8 message);
BUSTER_C_EXTERN bool c_parse_builtin_type_layout(Target target, CTypeKind kind, u64* size_out, u32* alignment_out);
BUSTER_C_EXTERN CTypeId c_parse_add_qualified_type(CParseResult* result, CTypeId base, CType qualifiers);
BUSTER_C_EXTERN bool c_parse_type_qualifier_word(String8 spelling, CType* type);
BUSTER_C_EXTERN u32 c_preprocess_token_source(CPreprocessResult const* preprocess, CToken token, IrSourceMapCursor* cursor);
BUSTER_C_EXTERN CSourceLocation c_preprocess_token_location_cursor(CPreprocessResult const* preprocess, CToken token,
                                                                     IrSourceMapCursor* cursor);
BUSTER_C_EXTERN bool c_parse_label_address_prefix_with_typedef(CParseResult* result, CPreprocessResult const* preprocess,
                                                                 CScopeId scope, u32 expression_start, u32 index);
BUSTER_C_EXTERN bool c_parse_label_address_prefix(CPreprocessResult const* preprocess, u32 expression_start, u32 index);
BUSTER_C_EXTERN bool c_parse_asm_goto_qualifier(CPreprocessResult preprocess, u32 start, u32 end);
BUSTER_C_EXTERN bool c_parse_type_start(CParseResult* result, CScopeId scope, String8 spelling, CPreprocessDialect dialect);
typedef struct CSpellingSpace CSpellingSpace;
struct CSpellingSpace
{
    char8* base;
    u64 used;
    u64 capacity;
    Arena* arena;
};
BUSTER_C_EXTERN CSpellingSpace c_space_local(Arena* arena, u64 capacity);
BUSTER_C_EXTERN bool c_token_is_punctuator(const CToken* token, CPunctuator punctuator);
BUSTER_C_EXTERN String8 c_token_spelling(char8 const* spelling_base, CToken token);
BUSTER_C_EXTERN u16 c_token_length_field(u64 length);
BUSTER_C_EXTERN CToken c_space_token(CSpellingSpace* space, String8 text, CTokenKind kind, CPunctuator punctuator);
BUSTER_C_EXTERN CToken c_space_retoken(CSpellingSpace* space, char8 const* from_base, CToken token);
BUSTER_C_EXTERN bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                    CPreprocessResult* result, u64* value_out);

typedef struct CSymbolSlot CSymbolSlot;
typedef enum CSymbolBuiltin
{
    C_SYMBOL_BUILTIN_NONE,
    C_SYMBOL_BUILTIN_EXPECT,
    C_SYMBOL_BUILTIN_CONSTANT_P,
    C_SYMBOL_BUILTIN_CHOOSE_EXPR,
    C_SYMBOL_BUILTIN_TYPES_COMPATIBLE_P,
    C_SYMBOL_BUILTIN_OBJECT_SIZE,
    C_SYMBOL_BUILTIN_ASSUME_ALIGNED,
    C_SYMBOL_BUILTIN_DEBUGTRAP,
    C_SYMBOL_BUILTIN_UNREACHABLE,
    C_SYMBOL_BUILTIN_STRLEN,
    C_SYMBOL_BUILTIN_CLEAR_CACHE,
    C_SYMBOL_BUILTIN_PREFETCH,
    C_SYMBOL_BUILTIN_VA_ARG,
    C_SYMBOL_BUILTIN_VA_START,
    C_SYMBOL_BUILTIN_VA_START_C23,
    C_SYMBOL_BUILTIN_VA_COPY,
    C_SYMBOL_BUILTIN_VA_END,
    C_SYMBOL_BUILTIN_GENERIC,
    C_SYMBOL_BUILTIN_ATOMIC,
    C_SYMBOL_BUILTIN_MATH,
    C_SYMBOL_BUILTIN_COUNT_LEADING_ZEROS,
    C_SYMBOL_BUILTIN_COUNT_TRAILING_ZEROS,
    C_SYMBOL_BUILTIN_POPULATION_COUNT,
    C_SYMBOL_BUILTIN_SIMD,
    C_SYMBOL_BUILTIN_COUNT,
} CSymbolBuiltin;
BUSTER_C_EXTERN CSymbolBuiltin c_symbol_builtin_from_spelling(String8 spelling);

struct CSymbolTable
{
    Arena* arena;
    String8* names;
    CSymbolSlot* slots;
    u8* builtin_kinds;
    u8* class_bits;
    u32 predefined_limit;
    u32 slot_capacity;
    u32 name_capacity;
    u32 count;
};

typedef enum CConditionalOperator
{
    C_CONDITIONAL_OPEN,
    C_CONDITIONAL_INDEX_OPEN,
    C_CONDITIONAL_UNARY_PLUS,
    C_CONDITIONAL_UNARY_MINUS,
    C_CONDITIONAL_LOGICAL_NOT,
    C_CONDITIONAL_BITWISE_NOT,
    C_CONDITIONAL_ADDRESS_OF,
    C_CONDITIONAL_DEREFERENCE,
    C_CONDITIONAL_CAST,
    C_CONDITIONAL_MULTIPLY,
    C_CONDITIONAL_DIVIDE,
    C_CONDITIONAL_REMAINDER,
    C_CONDITIONAL_ADD,
    C_CONDITIONAL_SUBTRACT,
    C_CONDITIONAL_SHIFT_LEFT,
    C_CONDITIONAL_SHIFT_RIGHT,
    C_CONDITIONAL_LESS,
    C_CONDITIONAL_LESS_EQUAL,
    C_CONDITIONAL_GREATER,
    C_CONDITIONAL_GREATER_EQUAL,
    C_CONDITIONAL_EQUAL,
    C_CONDITIONAL_NOT_EQUAL,
    C_CONDITIONAL_BITWISE_AND,
    C_CONDITIONAL_BITWISE_XOR,
    C_CONDITIONAL_BITWISE_OR,
    C_CONDITIONAL_LOGICAL_AND,
    C_CONDITIONAL_LOGICAL_OR,
    C_CONDITIONAL_QUESTION,
    C_CONDITIONAL_SELECT,
    C_CONDITIONAL_COMMA,
    C_CONDITIONAL_OPERATOR_COUNT,
} CConditionalOperator;
BUSTER_C_EXTERN u32 c_conditional_precedence(CConditionalOperator operation);
BUSTER_C_EXTERN bool c_conditional_is_unary(CConditionalOperator operation);
BUSTER_C_EXTERN bool c_conditional_operator(CToken token, bool unary, CConditionalOperator* operation);
typedef enum CIrStringEncoding
{
    C_IR_STRING_ENCODING_ORDINARY,
    C_IR_STRING_ENCODING_UTF8,
    C_IR_STRING_ENCODING_UTF16,
    C_IR_STRING_ENCODING_UTF32,
    C_IR_STRING_ENCODING_WIDE,
} CIrStringEncoding;

struct CIrDecodedString
{
    ByteSlice bytes;
    u64 element_count;
    u32 element_width;
    CTypeKind element_kind;
    CIrStringEncoding encoding;
};

typedef struct CTypeParseFrame CTypeParseFrame;
typedef struct CTypeMutation CTypeMutation;
typedef struct CParseExpressionTypeTask CParseExpressionTypeTask;
typedef struct CParsePromotedMemberWork CParsePromotedMemberWork;

typedef enum CParseExpressionTypeOperation
{
    C_PARSE_EXPRESSION_TYPE_NONE,
    C_PARSE_EXPRESSION_TYPE_COMMA,
    C_PARSE_EXPRESSION_TYPE_ASSIGN,
    C_PARSE_EXPRESSION_TYPE_ARITHMETIC,
    C_PARSE_EXPRESSION_TYPE_SHIFT,
    C_PARSE_EXPRESSION_TYPE_COMPARE,
    C_PARSE_EXPRESSION_TYPE_CONDITIONAL,
    C_PARSE_EXPRESSION_TYPE_UNARY,
    C_PARSE_EXPRESSION_TYPE_LOGICAL_NOT,
} CParseExpressionTypeOperation;

typedef enum CTypeParseFrameKind
{
    C_TYPE_PARSE_FRAME_SCALAR,
    C_TYPE_PARSE_FRAME_CORE,
    C_TYPE_PARSE_FRAME_SIZEOF,
    C_TYPE_PARSE_FRAME_EXPRESSION_LEAF,
    C_TYPE_PARSE_FRAME_ALIGNMENT,
    C_TYPE_PARSE_FRAME_AGGREGATE_SEGMENT,
    C_TYPE_PARSE_FRAME_AGGREGATE_RANGE,
    C_TYPE_PARSE_FRAME_PARENTHESIZED,
    C_TYPE_PARSE_FRAME_PARAMETER,
} CTypeParseFrameKind;

typedef enum CTypeParseFrameStage
{
    C_TYPE_PARSE_STAGE_BEGIN,
    C_TYPE_PARSE_STAGE_CHILD,
    C_TYPE_PARSE_STAGE_FALLBACK,
    C_TYPE_PARSE_STAGE_PARAMETERS,
    C_TYPE_PARSE_STAGE_PARAMETER_RESULT,
    C_TYPE_PARSE_STAGE_FINISH,
} CTypeParseFrameStage;

struct CTypeMutation
{
    CTypeId id;
    CType previous;
};

struct CParseExpressionTypeTask
{
    u32 start;
    u32 end;
    u32 split;
    u32 colon;
    CTypeId left_type;
    CParseExpressionTypeOperation operation;
    u8 state;
    u8 reserved[3];
};

struct CTypeParseFrame
{
    CParseResult* result;
    CParseResult checkpoint;
    CPreprocessResult preprocess;
    Arena* arena;
    CParseExpressionTypeTask* expression_tasks;
    CType qualifiers;
    CType original_type;
    CTypeId type;
    CTypeId base_type;
    CTypeId original_type_id;
    CToken name;
    CToken first;
    CScopeId scope;
    u32 start;
    u32 end;
    u32 index;
    u32 close;
    u32 specifier_index;
    u32 declarator_start;
    u32 declarator_end;
    u32 name_index;
    u32 close_index;
    u32 pointer_start;
    u32 parameter_start;
    u32 segment_start;
    u32 scan_index;
    u32 depth;
    u32 task_count;
    u32 task_capacity;
    u32 task_mark;
    u32 alignment_start;
    u32 alignment_count;
    u32 mutation_mark;
    u32 definition_type_start;
    u32 pending_index;
    u64 arena_mark;
    CTypeParseFrameKind kind;
    CTypeParseFrameStage stage;
    bool unqualified;
    bool has_name;
    bool variadic;
    bool has_function_suffix;
    bool original_type_valid;
    bool is_bit_field;
    bool auto_conditional;
    u8 reserved[1];
};

typedef struct CTypeLayoutCache CTypeLayoutCache;
struct CTypeLayoutCache
{
    u64* sizes;
    u32* alignments;
    u8* states;
    u32 capacity;
    CToken const* tokens;
};

struct CTypeParseMachine
{
    CTypeParseFrame* frames;
    CTypeMutation* mutations;
    CParseExpressionTypeTask* expression_tasks;
    CTypeId* incomplete_array_chain;
    u32 incomplete_array_chain_capacity;
    Arena* scratch_arena;
    CTypeLayoutCache layout_cache;
    CParsePromotedMemberWork* promoted_member_work;
    u32* promoted_member_visited;
    u32 promoted_member_capacity;
    u32 promoted_member_generation;
    CTypeId result_type;
    u32 result_index;
    u32 frame_count;
    u32 frame_capacity;
    u32 mutation_count;
    u32 mutation_capacity;
    u32 mutation_type_limit;
    u32 expression_task_count;
    u32 expression_task_capacity;
    bool result_valid;
    bool failed;
    u8 reserved[2];
};

struct CParsePromotedMemberWork
{
    CTypeId type;
    u32 root_field;
    u32 depth;
};
