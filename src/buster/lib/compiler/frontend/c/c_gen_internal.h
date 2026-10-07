#pragma once

// Private test seams into c_gen.c.
//
// Member lookup: the builder remains owned by c_gen.c; tests supply distinct
// persistent and rewindable arenas so an escaped search pointer or a
// type-universe-sized scratch request is observable.
//
// The exact-literal bignum (c_ir_ext80_big_* in c_gen.c): only the limbs below
// `count` are defined.  Tests fill every other limb with a sentinel to show a
// helper neither reads nor writes past its live limbs, so the value layout is
// declared here instead of inside c_gen.c.
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir_append.h>

// C lowering owns fresh, unpublished functions and maintains their instruction
// storage invariants, so its append sites may use the construction-only path.
#define ir_function_add_instruction(...) ir_instruction_append_trusted(__VA_ARGS__)

// Validation/planning boundary for one translation unit. These counts are
// derived only from finalized preprocess and parse rows; no arena state or IR
// table is changed until the caller accepts the complete plan.
typedef struct CIrLowerCapacityPlan CIrLowerCapacityPlan;
struct CIrLowerCapacityPlan
{
    u32 token_capacity;
    u32 type_capacity;
    u32 symbol_capacity;
    u32 function_capacity;
    u32 query_frame_capacity;
};
BUSTER_F_DECL bool c_ir_lower_capacity_plan(CPreprocessResult preprocess, CAnalysisResult parse, CIrLowerCapacityPlan* plan_out);

#define C_IR_EXT80_BIG_LIMBS 1024
typedef struct CIrExt80Big CIrExt80Big;
struct CIrExt80Big
{
    u32 limbs[C_IR_EXT80_BIG_LIMBS];
    u32 count;
};

#if BUSTER_INCLUDE_TESTS
// Smaller logical budgets exercise the production preflights without
// generating megabyte source fixtures or changing public lowering options.
BUSTER_F_DECL CIRLowerResult c_test_lower_to_ir_with_scratch_limits(Arena* arena, String8 source_path, CPreprocessResult preprocess,
                                                                    CAnalysisResult parse, Target target, CIRLowerOptions options,
                                                                    u64 query_reservation_limit, u64 function_reservation_limit);
// Rows and slots c_ir_label_find has visited since process start.
BUSTER_F_DECL u64 c_test_ir_label_find_probes(void);
BUSTER_F_DECL bool c_test_ir_scratch_reservation_size(u64 required_size, u64 current_size, u64* size_out);
BUSTER_F_DECL bool c_test_ir_function_scratch_size(u32 body_capacity, u32 lowering_capacity, bool direct_ssa, u64* size_out);
BUSTER_F_DECL bool c_test_ir_arena_reservation_advance(u64 reserved_size, u64* position, u64 element_size, u64 count, u64 alignment);
typedef enum CTestIrScratchProbeKind
{
    C_TEST_IR_SCRATCH_EVENTS,
    C_TEST_IR_SCRATCH_READS,
    C_TEST_IR_SCRATCH_BODY_TASKS,
    C_TEST_IR_SCRATCH_SSA_SLOTS,
    C_TEST_IR_SCRATCH_SSA_PARAMETERS,
    C_TEST_IR_SCRATCH_SSA_FINISH_CFG,
    C_TEST_IR_SCRATCH_SSA_FINISH_PUBLISH,
} CTestIrScratchProbeKind;
BUSTER_F_DECL bool c_test_ir_dynamic_scratch_rejection(CPreprocessResult preprocess, CDeclaration declaration, CTestIrScratchProbeKind kind);

// Execute the production inference/materialization machines over a synthetic
// flat integer token stream, independently of the TU query-stack reservation.
BUSTER_F_DECL bool c_test_initializer_flat_bytes(Arena* arena, Arena* task_arena, u32 element_count, u8* bytes,
                                                  u64* inferred_count, u64* scratch_bytes, String8* failure_message);
BUSTER_F_DECL IrValueId c_test_ir_member_place(Arena* arena, Arena* temporary_arena, IrProgram* program,
                                               IrFunction* function, IrValueId operand, String8 member, CPunctuator access,
                                               String8* failure_message);
// Fields a member lookup visited, and name indexes built, on this thread.
BUSTER_F_DECL void c_test_ir_member_lookup_counts(u64* visits, u64* builds);
BUSTER_F_DECL bool c_test_ext80_big_shift_left(CIrExt80Big* value, u32 shift);
BUSTER_F_DECL s32 c_test_ext80_big_compare_shifted(CIrExt80Big const* left, CIrExt80Big const* right, s32 shift);
BUSTER_F_DECL bool c_test_ext80_big_subtract_shifted(CIrExt80Big* left, CIrExt80Big const* right, u32 shift);
BUSTER_F_DECL bool c_test_ext80_big_add(CIrExt80Big* left, CIrExt80Big const* right);
BUSTER_F_DECL bool c_test_ext80_big_multiply(CIrExt80Big const* left, CIrExt80Big const* right, CIrExt80Big* product_out);
BUSTER_F_DECL bool c_test_ext80_parse_rational_literal(String8 spelling, CIrExt80Big* numerator_out, CIrExt80Big* denominator_out,
                                                       s32* binary_exponent_out);

// Constant-initializer relocations (#1450): one operation is an append of a
// record at `offset`, or a designated clear of [offset, offset + size).
typedef struct CTestInitializerRelocationOperation CTestInitializerRelocationOperation;
struct CTestInitializerRelocationOperation
{
    u64 offset;
    u64 size;
    u32 symbol;
    bool clear;
    u8 reserved[3];
};

// Both final arrays and where each run stopped (the operation count when it
// did not): `indexed` went through a designator-machine context and its
// relocation index, `reference` through the whole-array compaction alone.
typedef struct CTestInitializerRelocationReplay CTestInitializerRelocationReplay;
struct CTestInitializerRelocationReplay
{
    IrGlobalRelocation* indexed;
    IrGlobalRelocation* reference;
    u32 indexed_count;
    u32 reference_count;
    u32 indexed_stop;
    u32 reference_stop;
    u32 indexed_appends;
    bool index_built;
    u8 reserved[3];
    u64 index_bucket_visits;
    u64 index_group_visits;
    u64 index_removed_rows;
    u64 index_compaction_rows;
    u64 reference_compaction_rows;
};

BUSTER_F_DECL void c_test_initializer_relocation_replay(Arena* arena, u32 pointer_size, u64 byte_count, u32 capacity,
                                                        CTestInitializerRelocationOperation const* operations, u32 operation_count,
                                                        CTestInitializerRelocationReplay* replay);
#endif
