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
// Calling-thread requested association-list bytes; excludes other type-machine
// scratch and never includes the small local list.
BUSTER_F_DECL u64 c_test_generic_association_scratch_bytes(void);
BUSTER_F_DECL CTypeId c_test_aggregate_unique(CParseResult* result, CTypeKind kind, String8 tag, bool* decided);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_add(CParseResult* result, CType type);
BUSTER_F_DECL CTypeId c_test_aggregate_lookup_find(CParseResult* result, CTypeKind kind, String8 tag, CScopeId scope);
BUSTER_F_DECL void c_test_aggregate_lookup_rollback(CParseResult* result, CParseResult checkpoint);

// Type-task steps, operator-token visits, and expression-end table/walk rows.
BUSTER_F_DECL u64 c_test_assignment_chain_work(void);

// Caps, on the calling thread, the scratch bytes each function body's guarded
// validation allocations (c_parse_body_scratch_allocate) may claim past the
// body's starting position, so a small fixture exhausts the same allocation
// sites a multi-million-token body exhausts on the real arena. Zero restores
// the arena's real remaining capacity.
BUSTER_F_DECL void c_test_body_validation_scratch_limit(u64 bytes);
// The same cap for the private model arena of each type-constant query a body
// makes, measured from the model arena's position when the query opens it.
BUSTER_F_DECL void c_test_body_validation_model_limit(u64 bytes);
// Records, on the calling thread, every request the guard checks into
// `entries` until `capacity` are held (zero capacity stops recording). Each
// body opens with a separator row (element_size 0); a request row holds its
// element size and count and the byte offset its end reaches past the body's
// starting position (past the query's model position for a `model` row), or
// UINT64_MAX for the request the guard refused.
typedef struct CTestBodyScratchRequest CTestBodyScratchRequest;
struct CTestBodyScratchRequest
{
    u64 element_size;
    u64 count;
    u64 end;
    bool model;
};
BUSTER_F_DECL void c_test_body_validation_scratch_trace(CTestBodyScratchRequest* entries, u32 capacity);
BUSTER_F_DECL u32 c_test_body_validation_scratch_trace_count(void);

// The tree expression typer (c_ast_types.c). c_test_ast_type_probe answers one
// range of an analyzed function body on a private machine. `status` is a
// C_TEST_AST_TYPE_PROBE_* value: the function has no body, the body was not
// typed, or the typer's INACTIVE, MISS, DECLINE or ANSWER (in that order, from
// C_TEST_AST_TYPE_PROBE_INACTIVE).
enum
{
    C_TEST_AST_TYPE_PROBE_NO_BODY = 100,
    C_TEST_AST_TYPE_PROBE_UNTYPED = 101,
    C_TEST_AST_TYPE_PROBE_INACTIVE = 0,
    C_TEST_AST_TYPE_PROBE_MISS = 1,
    C_TEST_AST_TYPE_PROBE_DECLINE = 2,
    C_TEST_AST_TYPE_PROBE_ANSWER = 3,
};
typedef struct CTestAstTypeProbe CTestAstTypeProbe;
struct CTestAstTypeProbe
{
    CTypeId type;
    CTypeKind kind;
    u64 nodes_typed;
    u64 nodes_accepted;
    u32 status;
    // The CAstKind of the node the range mapped to.
    u32 node_kind;
    bool nonplace_projection;
    // The answer replays a string-literal token: a checked cast's operand,
    // or with replay_answer the queried literal (C_AST_TYPE_STRING), whose
    // row is the answer.
    bool replay;
    bool replay_answer;
};
BUSTER_F_DECL CTestAstTypeProbe c_test_ast_type_probe(Arena* scratch, CPreprocessResult preprocess, CParseResult* result, CAst const* ast, String8 function,
                                                      u32 start, u32 end, bool checked);

// Flexible-array promoted-member count replay checks and bounded traversal control.
BUSTER_F_DECL bool c_test_ast_type_replay_effective_member_count(void);
BUSTER_F_DECL bool c_test_flexible_array_postorder_work(Arena* arena);
BUSTER_F_DECL bool c_test_parse_type_replay_effective_member_count(void);

// Verify mode (c_test_ast_type_verify_set): every tree answer is also computed
// by the type machine and compared; the tree answer is still what the caller
// gets. The mode and its counts are per thread. A mismatch is a tree answer the
// machine would not have given: reasons are C_TEST_AST_TYPE_MISMATCH_* bits,
// and the first few are kept in full.
enum
{
    C_TEST_AST_TYPE_MISMATCH_VALIDITY = 1u << 0,
    C_TEST_AST_TYPE_MISMATCH_TYPE = 1u << 1,
    C_TEST_AST_TYPE_MISMATCH_CONSTRAINT = 1u << 2,
    C_TEST_AST_TYPE_MISMATCH_NONPLACE = 1u << 3,
    C_TEST_AST_TYPE_MISMATCH_DIAGNOSTIC = 1u << 4,
    C_TEST_AST_TYPE_MISMATCH_TABLES = 1u << 5,
    C_TEST_AST_TYPE_MISMATCH_REASONS = 6,
    C_TEST_AST_TYPE_MISMATCH_KEPT = 8,
};
typedef struct CTestAstTypeMismatch CTestAstTypeMismatch;
struct CTestAstTypeMismatch
{
    u32 start;
    u32 end;
    u32 node_kind;
    u32 reasons;
    // CTypeKind of each answer, C_TYPE_COUNT when there is none.
    u32 tree_type_kind;
    u32 machine_type_kind;
    bool machine_valid;
    bool checked;
};
typedef struct CTestAstTypeVerify CTestAstTypeVerify;
struct CTestAstTypeVerify
{
    u64 compared;
    u64 mismatches;
    // Designator probes the const-assignment walk skipped, which the machine
    // answered too, and those it did not fail cleanly.
    u64 probes;
    u64 probe_mismatches;
    // Answers to a member query whose row is a shared qualified copy
    // (c_parse_intern_qualified_type).
    u64 shared_members;
    u64 reason_counts[C_TEST_AST_TYPE_MISMATCH_REASONS];
    CTestAstTypeMismatch first[C_TEST_AST_TYPE_MISMATCH_KEPT];
    u32 first_count;
};
BUSTER_F_DECL void c_test_ast_type_verify_set(bool enabled);
// A negative control for the differential: while set, every alignment record
// a replay inherited is held with its range start moved, so the comparison
// with the machine's record must report a table mismatch.
BUSTER_F_DECL void c_test_ast_type_verify_perturb_alignments(bool enabled);
// The counts since the last take, which resets them.
BUSTER_F_DECL CTestAstTypeVerify c_test_ast_type_verify_take(void);
// Turns the declaration specifier read from the tree (c_parse.c) off and back
// on, per thread, so a test can analyse the same unit with and without it.
BUSTER_F_DECL void c_test_ast_specifiers_set(bool enabled);
// Verify mode for the type-name runs skipped as no type name
// (c_parse_type_name_specifiers under -fc-ast-pilot): the machine's attempt
// also runs on every skipped run, and the counts record the runs it checked
// and those where it answered, kept a type row or reported a diagnostic. Per
// thread; the take resets the counts.
typedef struct CTestAstTypeNameVerify CTestAstTypeNameVerify;
struct CTestAstTypeNameVerify
{
    u64 checked;
    u64 answered;
    u64 appended;
    u64 diagnosed;
};
BUSTER_F_DECL void c_test_ast_type_names_verify_set(bool enabled);
BUSTER_F_DECL CTestAstTypeNameVerify c_test_ast_type_names_verify_take(void);
// Verify mode for the member specifier runs read from the tree
// (c_parse_member_specifiers_type under -fc-ast-pilot): before each read the
// machine's scalar frame also reads the run, and is rolled back. The counts
// record the reads compared and those whose type row, its fields or the
// declarator start differed from the machine's, or that the machine refused.
// Per thread; the take resets the counts.
typedef struct CTestAstMemberVerify CTestAstMemberVerify;
struct CTestAstMemberVerify
{
    u64 compared;
    u64 mismatches;
};
BUSTER_F_DECL void c_test_ast_members_verify_set(bool enabled);
BUSTER_F_DECL CTestAstMemberVerify c_test_ast_members_verify_take(void);

BUSTER_F_DECL CDiagnostic c_test_check_named_call_arities(Arena* arena, CAnalysisResult* analysis, CPreprocessResult preprocess,
                                                        u32 start, u32 end);

// Compare the production endpoint table with its fallback at every subrange.
BUSTER_F_DECL bool c_test_constraint_expression_ends_agree(Arena* arena, CPreprocessResult preprocess, CParseResult* result, u32 start, u32 count);

BUSTER_F_DECL void c_test_definition_index_record(CParseResult* result, u32 definition_start, CTypeId type);
BUSTER_F_DECL u32 c_test_definition_scan_start(CParseResult const* result, u32 definition_start);

// Existing binding probes must not intern absent names or mutate model rows.
BUSTER_F_DECL bool c_test_existing_entity_lookup_read_only(CParseResult const* result, String8 name, CEntityKind kind);

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
// Force fresh completion queries to exercise row-free capture and rollback.
BUSTER_F_DECL void c_test_ast_fact_uncached_set(bool enabled);
BUSTER_F_DECL void c_test_ast_required_fact_intervals(CAstFacts* facts, CAst const* ast, u8* required);
BUSTER_F_DECL void c_test_enum_scope_dirty_set(bool enabled);
// Keeps the interning window shut (CTypeInterning), so one source can be
// compiled with and without interned rows and the objects compared.
BUSTER_F_DECL void c_test_set_type_interning_off(bool off);
// Two const copies of `base` through c_parse_intern_qualified_type with the
// window open, and the lookup after them: the same row and one append when
// the copy may be shared, two rows (and two alignment records for an aligned
// base) when it may not.
typedef struct CTestQualifiedShare CTestQualifiedShare;
struct CTestQualifiedShare
{
    CTypeId first;
    CTypeId second;
    CTypeId found;
    u32 types_added;
    u32 alignments_added;
};
BUSTER_F_DECL CTestQualifiedShare c_test_qualified_share(CParseResult* result, CTypeId base);

// Cast-proof token visits and logical-position probes on the calling thread.
BUSTER_F_DECL u64 c_test_logical_label_type_work(void);

// Poison classification fields only after completed analysis, preserving
// source bytes and the original stream identity. Disarming returns how many
// source records were poisoned, so object controls prove the seam ran.
BUSTER_F_DECL u64 c_test_lowering_token_poison(bool armed);

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

// Member rows c_parse_member_type examined on this thread, and the name indexes
// it built for wide aggregates.
BUSTER_F_DECL void c_test_member_lookup_counts(u64* visits, u64* builds);

// Work performed by promoted offsetof searches on the calling thread.
BUSTER_F_DECL bool c_test_member_offset(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId aggregate, String8 name, u64* offset);
BUSTER_F_DECL void c_test_member_offset_counts(u64* types, u64* members, u64* scratch_bytes);

// Promoted-member searches on this thread, and how many needed a per-type table.
BUSTER_F_DECL void c_test_member_search_counts(u64* searches, u64* tables);

// Member rows the initializer slot helpers have read on this thread, by their
// reference walks and by the per-walk slot tables.
BUSTER_F_DECL void c_test_initializer_slot_member_visits(u64* visits);

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

// Directional reads of the immutable pair table, including clipped ranges.
BUSTER_F_DECL u32 c_test_parse_delimiter_match(CParseResult* result, CPreprocessResult preprocess, u32 token, u32 start, bool reverse);

BUSTER_F_DECL bool c_test_parse_direct_expression_type(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                     u32 start, u32 end, CTypeId* type_out);

BUSTER_F_DECL bool c_parse_label_address_prefix_with_typedef(CParseResult* result, CPreprocessResult const* preprocess,
                                                             CScopeId scope, u32 body_start, u32 index);
BUSTER_F_DECL bool c_test_parse_label_address_prefix_paths_agree(CParseResult* result, CPreprocessResult const* preprocess,
                                                                 CScopeId scope, u32 body_start, u32 index);

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
    C_TEST_PARSE_NESTING_SCOPE_LEVELS,
    C_TEST_PARSE_NESTING_STATEMENT_END_STEPS,
    C_TEST_PARSE_NESTING_LOOKUP_SCOPE_STEPS,
    C_TEST_PARSE_NESTING_LABEL_VALUE_TOKENS,
    C_TEST_PARSE_NESTING_LABEL_ROOT_TOKENS,
    C_TEST_PARSE_NESTING_LABEL_SPLIT_TOKENS,
    C_TEST_PARSE_NESTING_LABEL_INDEX_TOKENS,
    C_TEST_PARSE_NESTING_LABEL_ROOT_REUSES,
    C_TEST_PARSE_NESTING_STATEMENT_EXPRESSION_TAILS,
    C_TEST_PARSE_NESTING_SLOTS,
};
BUSTER_F_DECL u64 c_test_parse_nesting_count(u32 slot);

// Token visits and delimiter/ancestor operations in nested-tag scans.
// The position-index token population is charged once for its actual build.
enum
{
    C_TEST_PARSE_TAG_SCAN_INDEX_TOKENS,
    C_TEST_PARSE_TAG_SCAN_DELIMITER_STEPS,
    C_TEST_PARSE_TAG_SCAN_LAYOUT_TOKENS,
    C_TEST_PARSE_TAG_SCAN_ALIGNMENT_TOKENS,
    C_TEST_PARSE_TAG_SCAN_MEMBER_TOKENS,
    C_TEST_PARSE_TAG_SCAN_ANCESTOR_STEPS,
    C_TEST_PARSE_TAG_SCAN_SLOTS,
};
BUSTER_F_DECL u64 c_test_parse_tag_scan_count(u32 slot);

// Observable answers of the production specifier and size/alignment token queries,
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
    C_TEST_WORD_CLASS_ALIGNOF = 1 << 10,
    C_TEST_WORD_CLASS_SIZEOF_ALIGNOF = 1 << 11,
};
BUSTER_F_DECL u32 c_test_parse_word_classes(CPreprocessResult preprocess, CToken token);

// A real member search around a failed private workspace replacement:
// retain the old owner on failure, recover on retry, and release via teardown.
typedef struct CTestPromotedMemberWorkspace CTestPromotedMemberWorkspace;
struct CTestPromotedMemberWorkspace
{
    CTypeId member_type;
    bool initial_search;
    bool failed_growth;
    bool fault_consumed;
    bool workspace_preserved;
    bool structured_error;
    bool recovered;
    bool replacement;
    bool released;
};
BUSTER_F_DECL CTestPromotedMemberWorkspace c_test_promoted_member_workspace(CParseResult* result, CTypeId root, String8 name);

// The default-off comparison of semantic and lowering answers for
// scalar-integer `sizeof` expression operands (GitHub #2531, slice 2). While
// set on the calling thread, semantic analysis keeps a CSizeofOperandType row
// per answered operand (CParseResult.sizeof_operand_types) and lowering
// compares every `sizeof` size it settles against the rows for the same
// keyword: its size always, and its signedness where lowering's answer
// carries an integer type. Rows of different producers for one keyword are
// compared with each other too. Output is unchanged: nothing reads the rows
// but the comparison. The checked constant-node resolver records its settled
// sizeof answer; the historical prediction-path row is retained and stays zero.
enum
{
    C_TEST_SIZEOF_HANDOFF_PATH_RESOLVER,
    C_TEST_SIZEOF_HANDOFF_PATH_PREDICTION,
    C_TEST_SIZEOF_HANDOFF_PATHS,
    C_TEST_SIZEOF_HANDOFF_AGREE = 0,
    C_TEST_SIZEOF_HANDOFF_SIZE_MISMATCH,
    // The sizes agree but lowering's integer type has the other signedness.
    C_TEST_SIZEOF_HANDOFF_SIGNEDNESS_MISMATCH,
    // The node resolver gave no answer for an operand with a row.
    C_TEST_SIZEOF_HANDOFF_DECLINED,
    // A lowering answer for a `sizeof` keyword with no row in its scope.
    C_TEST_SIZEOF_HANDOFF_UNMATCHED,
    C_TEST_SIZEOF_HANDOFF_OUTCOMES,
    C_TEST_SIZEOF_HANDOFF_MISMATCH_KEPT = 16,
};
typedef struct CTestSizeofHandoffMismatch CTestSizeofHandoffMismatch;
struct CTestSizeofHandoffMismatch
{
    u32 keyword;
    u32 operand_end;
    u64 lowering_size;
    u8 semantic_size;
    u8 kind;
    u8 producer;
    u8 path;
    bool semantic_signed;
    bool lowering_signed;
};
typedef struct CTestSizeofHandoff CTestSizeofHandoff;
struct CTestSizeofHandoff
{
    // Rows kept per CSizeofOperandProducer, and their bytes.
    u64 rows[C_SIZEOF_OPERAND_PRODUCER_COUNT];
    u64 row_bytes;
    // An answer for a key its producer already kept: the same answer, or a
    // different one. A row whose kind or size differs from another
    // producer's row for the same keyword counts once in conflicts too.
    u64 repeats;
    u64 conflicts;
    // Operand type queries the validation producer made for this comparison
    // alone, and typed operands outside the scalar-integer population.
    u64 extra_queries;
    u64 outside_population;
    // Compared rows whose producer's scope is not the keyword's innermost
    // scope (counted per row compared, whatever the outcome).
    u64 scope_differs;
    // Agreeing sizes whose signedness was also compared, because lowering's
    // answer carried an integer type.
    u64 signedness_compared;
    // Lowering answers compared, per path and outcome; one per matching row.
    u64 outcomes[C_TEST_SIZEOF_HANDOFF_PATHS][C_TEST_SIZEOF_HANDOFF_OUTCOMES];
    CTestSizeofHandoffMismatch first[C_TEST_SIZEOF_HANDOFF_MISMATCH_KEPT];
    u32 first_count;
};
BUSTER_F_DECL void c_test_sizeof_handoff_set(bool enabled);
// A negative control for the comparison: while set, every row a producer in
// `producer_mask` (bits of CSizeofOperandProducer) keeps has its integer
// kind's signedness flipped (int becomes unsigned int, unsigned long becomes
// long), so the sizes still agree and only the kind is wrong.
BUSTER_F_DECL void c_test_sizeof_handoff_perturb_set(u32 producer_mask);
// The counts since the last take, which resets them.
BUSTER_F_DECL CTestSizeofHandoff c_test_sizeof_handoff_take(void);

typedef struct CTestTypeConstantQuery CTestTypeConstantQuery;
struct CTestTypeConstantQuery
{
    CIntegerConstant constant;
    bool model_unchanged;
};
typedef struct CTestTypeConstantCursorQuery CTestTypeConstantCursorQuery;
struct CTestTypeConstantCursorQuery
{
    CTestTypeConstantQuery query;
    bool prepared;
};
BUSTER_F_DECL void c_test_offsetof_completion_only_set(bool enabled);
BUSTER_F_DECL CTestTypeConstantQuery c_test_type_integer_constant(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                CScopeId scope, u32 start, u32 end);
BUSTER_F_DECL CTestTypeConstantCursorQuery c_test_type_integer_constant_cursor(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                             CScopeId prepared_scope, CScopeId scope, u32 start, u32 end);
// Runs the same protected reader without full-capacity snapshots. The caller
// can supply large legitimate buffers and check its published rows/sentinels.
typedef struct CTestSizeofArrayWork CTestSizeofArrayWork;
struct CTestSizeofArrayWork
{
    u64 dependency_tasks;
    u64 bound_requests;
    u64 numeric_receipts;
    u64 certified_reads;
    u64 cycle_refusals;
};
BUSTER_F_DECL CTestSizeofArrayWork c_test_sizeof_array_work_take(void);

BUSTER_F_DECL CIntegerConstant c_test_type_integer_constant_read(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                CScopeId scope, u32 start, u32 end);
BUSTER_F_DECL CTestTypeConstantQuery c_test_type_integer_constant_sparse(Arena* scratch, CPreprocessResult preprocess, CParseResult* result,
                                                                       CScopeId scope, u32 start, u32 end);

// One layout query that reaches the solve, as a machineless caller without a
// cache asks it: through the demand-driven agenda when `agenda_allowed` (which
// still falls back to the ordered passes exactly as production does), through
// the ordered passes otherwise. `statistics` receives this query's counts in
// place of the result's own record. `offset_member` is UINT32_MAX and
// `offset_out` null except for an offsetof query.
BUSTER_F_DECL bool c_test_type_layout(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId type, bool agenda_allowed,
                                      u32 offset_member, CTypeLayoutStatistics* statistics, u64* size_out, u32* alignment_out, u64* offset_out);

// Repeat member-offset queries with one idle machine and its committed cache.
BUSTER_F_DECL bool c_test_type_layout_offset_queries(Arena* arena, CPreprocessResult preprocess, CParseResult* result, CTypeId const* types, u32 count,
                                       u32 member_index, u64 expected_offset, u32 repeats, CTypeLayoutStatistics* statistics);

// Bytes one type-machine frame row copies on every push.
BUSTER_F_DECL u64 c_test_type_parse_frame_bytes(void);
// Override production's derived frame limit for a structured refusal/recovery test.
BUSTER_F_DECL void c_test_type_parse_frame_capacity_limit_set(u32 limit);

// Rollback restores a speculative tag even after geometric type-row growth.
BUSTER_F_DECL bool c_test_type_parse_rollback_after_growth(Arena* arena, bool* grew_out, bool* restored_pointer_out,
                                                           bool* old_tag_restored_out, bool* grown_tag_preserved_out);

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
