#pragma once

// Private test seams for production constexpr, call arity, expression typing, binding, aggregate-tag, definition-index,
// label-provenance gate, validation-candidate and type-compatibility walk queries.
// Tests own their storage and observe production behavior, not a duplicate
// implementation. No declarations enter production builds.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CDiagnostic c_test_check_named_call_arities(Arena* arena, CAnalysisResult* analysis, CPreprocessResult preprocess,
                                                        u32 start, u32 end);
BUSTER_F_DECL bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                       CDeclaration* declaration);
BUSTER_F_DECL u32 c_test_parse_binding_bind(CParseResult* result, CScopeId scope, CEntityId entity, u32 symbol);
BUSTER_F_DECL void c_test_parse_binding_unwind(CParseResult* result, u32 mark);
BUSTER_F_DECL CTypeId c_test_aggregate_unique(CParseResult* result, CTypeKind kind, String8 tag, bool* decided);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_add(CParseResult* result, CType type);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_find(CParseResult* result, CTypeKind kind, String8 tag, CScopeId scope);
BUSTER_F_DECL void c_test_aggregate_lookup_rollback(CParseResult* result, CParseResult checkpoint);
// Promoted-member searches on this thread, and how many needed a per-type table.
BUSTER_F_DECL void c_test_member_search_counts(u64* searches, u64* tables);
BUSTER_F_DECL void c_test_definition_index_record(CParseResult* result, u32 definition_start, CTypeId type);
BUSTER_F_DECL u32 c_test_definition_scan_start(CParseResult const* result, u32 definition_start);
BUSTER_F_DECL bool c_test_parse_direct_expression_type(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                     u32 start, u32 end, CTypeId* type_out);
// Whether c_parse_validate_label_values would walk this function body's
// values; the analysis must already have built the scope index.
BUSTER_F_DECL bool c_test_parse_label_values_needed(CParseResult* result, CPreprocessResult preprocess, CDeclaration const* declaration);
// Disagreements of the validation-candidate sources with their scalar
// definitions: the call-shape scan over every [from, end) of shapes, and the
// two-population cursor over every [start, end) up to limit.
BUSTER_F_DECL u32 c_test_parse_call_shape_mismatches(CTokenShape const* shapes, u32 count);
BUSTER_F_DECL u32 c_test_parse_candidate_merge_mismatches(u32* first, u32 first_count, u32* second, u32 second_count, u32 limit);
// c_parse_types_compatible as production calls it, and the pair-stack walk
// it falls back to without the self-comparison chain in front: the oracle
// that chain must agree with. `reserve_types` makes room for hand-built rows.
BUSTER_F_DECL bool c_test_types_compatible(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right);
BUSTER_F_DECL bool c_test_types_compatible_walk(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CTypeId left, CTypeId right);
BUSTER_F_DECL bool c_test_parse_reserve_types(CParseResult* result, u32 additional);
// Tokens of [start, start + count) where the body scope map built under root
// disagrees with c_parse_scope_for_token's descent; UINT32_MAX without a
// children index.
BUSTER_F_DECL u32 c_test_parse_body_scope_mismatches(CParseResult* result, Arena* arena, CScopeId root, u32 start, u32 count);
#endif
