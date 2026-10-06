#pragma once

// Private test seams over C parser and analysis queries. Tests own their
// storage and observe production behavior, not a duplicate implementation.
// No declarations enter production builds.
//
// Map: each seam group (its comment, types and declarations) is one
// blank-line-separated block, ordered by the byte order of the group's first
// c_test_ function name. Add a new group at its sorted position and do not
// list groups here, so independent additions land at different anchors; see
// docs/agents/testing.md.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CTypeId c_test_aggregate_unique(CParseResult* result, CTypeKind kind, String8 tag, bool* decided);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_add(CParseResult* result, CType type);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_find(CParseResult* result, CTypeKind kind, String8 tag, CScopeId scope);
BUSTER_F_DECL void c_test_aggregate_lookup_rollback(CParseResult* result, CParseResult checkpoint);

BUSTER_F_DECL CDiagnostic c_test_check_named_call_arities(Arena* arena, CAnalysisResult* analysis, CPreprocessResult preprocess,
                                                        u32 start, u32 end);

BUSTER_F_DECL void c_test_definition_index_record(CParseResult* result, u32 definition_start, CTypeId type);
BUSTER_F_DECL u32 c_test_definition_scan_start(CParseResult const* result, u32 definition_start);

// A single file-scope expression-type query, reported field by field; see
// c_test_expression_type_query in c_parse.c. `machine_only` forces the type
// machine even for a lone literal token, as the differential oracle.
typedef struct CTestExpressionQuery CTestExpressionQuery;
struct CTestExpressionQuery
{
    CTypeId type;
    CTypeId published_type;
    CTypeId result_type;
    CTypeKind kind;
    u64 scratch_delta;
    u32 type_count_delta;
    u32 diagnostic_delta;
    u32 result_index;
    u32 frame_count;
    u32 mutation_count;
    u32 mutation_type_limit;
    u32 expression_task_count;
    u32 published_end;
    u32 published_flags;
    bool valid;
    bool result_valid;
    bool failed;
};
BUSTER_F_DECL CTestExpressionQuery c_test_expression_type_query(Arena* scratch, CPreprocessResult preprocess, CParseResult* result, u32 start,
                                                                u32 end, bool checked, bool nested, CTypeId cached, bool scalars,
                                                                bool machine_only);
BUSTER_F_DECL void c_test_set_literal_query_machine_only(bool machine_only);

// Direct member alignment queries must leave the published model, including
// spare rows and the type-map input counts/pointers, byte-for-byte unchanged.
typedef struct CTestMemberAlignmentQuery CTestMemberAlignmentQuery;
struct CTestMemberAlignmentQuery
{
    u32 alignment;
    bool valid;
    bool model_unchanged;
};
BUSTER_F_DECL CTestMemberAlignmentQuery c_test_member_alignment_query(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                              CScopeId scope, u32 start, u32 end);

// Promoted-member searches on this thread, and how many needed a per-type table.
BUSTER_F_DECL void c_test_member_search_counts(u64* searches, u64* tables);

BUSTER_F_DECL u32 c_test_parse_binding_bind(CParseResult* result, CScopeId scope, CEntityId entity, u32 symbol);
BUSTER_F_DECL void c_test_parse_binding_unwind(CParseResult* result, u32 mark);

// Tokens of [start, start + count) where the body scope map built under root
// disagrees with c_parse_scope_for_token's descent; UINT32_MAX without a
// children index.
BUSTER_F_DECL u32 c_test_parse_body_scope_mismatches(CParseResult* result, Arena* arena, CScopeId root, u32 start, u32 count);

// Disagreements of the validation-candidate sources with their scalar
// definitions: the call-shape scan over every [from, end) of shapes, and the
// two-population cursor over every [start, end) up to limit.
BUSTER_F_DECL u32 c_test_parse_call_shape_mismatches(CTokenShape const* shapes, u32 count);
BUSTER_F_DECL u32 c_test_parse_candidate_merge_mismatches(u32* first, u32 first_count, u32* second, u32 second_count, u32 limit);

BUSTER_F_DECL bool c_test_parse_direct_expression_type(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                     u32 start, u32 end, CTypeId* type_out);

// Whether c_parse_validate_label_values would walk this function body's
// values; the analysis must already have built the scope index.
BUSTER_F_DECL bool c_test_parse_label_values_needed(CParseResult* result, CPreprocessResult preprocess, CDeclaration const* declaration);

// Token work this thread's nested-control-statement paths have done so far
// (#2676). Tests take the difference across one parse and bound its growth
// with nesting depth.
enum
{
    C_TEST_PARSE_NESTING_STATEMENT_END_TOKENS,
    C_TEST_PARSE_NESTING_CALL_KEYWORDS_SKIPPED,
    C_TEST_PARSE_NESTING_CALL_LOOKUPS,
    C_TEST_PARSE_NESTING_BODY_SCOPE_STORES,
    C_TEST_PARSE_NESTING_SLOTS,
};
BUSTER_F_DECL u64 c_test_parse_nesting_count(u32 slot);

// Observable answers of the five production specifier-word token queries,
// including their qualifier writes. No counters or state enter production.
enum
{
    C_TEST_WORD_CLASS_TYPE = 1 << 0,
    C_TEST_WORD_CLASS_AUTO_TYPE = 1 << 1,
    C_TEST_WORD_CLASS_TYPE_NAME_START = 1 << 2,
    C_TEST_WORD_CLASS_QUALIFIER = 1 << 3,
    C_TEST_WORD_CLASS_ATOMIC_PREFIX = 1 << 4,
    C_TEST_WORD_CLASS_CONST = 1 << 5,
    C_TEST_WORD_CLASS_VOLATILE = 1 << 6,
    C_TEST_WORD_CLASS_RESTRICT = 1 << 7,
    C_TEST_WORD_CLASS_ATOMIC = 1 << 8,
    C_TEST_WORD_CLASS_QUALIFIERS_AGREE = 1 << 9,
};
BUSTER_F_DECL u32 c_test_parse_word_classes(CPreprocessResult preprocess, CToken token);

typedef struct CTestTypeConstantQuery CTestTypeConstantQuery;
struct CTestTypeConstantQuery
{
    CIntegerConstant constant;
    bool model_unchanged;
};
BUSTER_F_DECL CTestTypeConstantQuery c_test_type_integer_constant(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                CScopeId scope, u32 start, u32 end);
// Runs the same protected reader without full-capacity snapshots. The caller
// can supply large legitimate buffers and check its published rows/sentinels.
BUSTER_F_DECL CIntegerConstant c_test_type_integer_constant_read(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                CScopeId scope, u32 start, u32 end);

// One layout query that reaches the solve, as a machineless caller without a
// cache asks it: through the demand-driven agenda when `agenda_allowed` (which
// still falls back to the ordered passes exactly as production does), through
// the ordered passes otherwise. `statistics` receives this query's counts in
// place of the result's own record. `offset_member` is UINT32_MAX and
// `offset_out` null except for an offsetof query.
BUSTER_F_DECL bool c_test_type_layout(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId type, bool agenda_allowed,
                                      u32 offset_member, CTypeLayoutStatistics* statistics, u64* size_out, u32* alignment_out, u64* offset_out);

// Bytes one type-machine frame row copies on every push.
BUSTER_F_DECL u64 c_test_type_parse_frame_bytes(void);

// Whether nested frames' rollback snapshots stay independent; see c_parse.c.
BUSTER_F_DECL bool c_test_type_parse_snapshot_rows_restore(Arena* arena, u32 depth);

// c_parse_types_compatible as production calls it, and the pair-stack walk
// it falls back to without the self-comparison chain in front: the oracle
// that chain must agree with. `reserve_types` makes room for hand-built rows.
BUSTER_F_DECL bool c_test_types_compatible(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right);
BUSTER_F_DECL bool c_test_types_compatible_walk(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right);
BUSTER_F_DECL bool c_test_parse_reserve_types(CParseResult* result, u32 additional);

BUSTER_F_DECL bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                       CDeclaration* declaration);
#endif
