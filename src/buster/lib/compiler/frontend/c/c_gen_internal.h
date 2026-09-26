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

#define C_IR_EXT80_BIG_LIMBS 1024
typedef struct CIrExt80Big CIrExt80Big;
struct CIrExt80Big
{
    u32 limbs[C_IR_EXT80_BIG_LIMBS];
    u32 count;
};

// Work performed by direct-SSA trivial-parameter simplification. Parameter and
// incoming visits count the same evaluation events for the notification agenda
// and its full-sweep test reference. `removed` counts every unlinked parameter
// and `trivial` the subset whose removal changed a root. The remaining fields
// charge the agenda's own witness recording, survivor classification, watch,
// notification and dirty-set work.
typedef struct CIrSsaSimplifyWork CIrSsaSimplifyWork;
struct CIrSsaSimplifyWork
{
    u64 sweeps;
    u64 block_visits;
    u64 empty_block_visits;
    u64 parameter_visits;
    u64 incoming_visits;
    u64 removed;
    u64 trivial;
    u64 survivors;
    u64 classified;
    u64 watch_links;
    u64 notifications;
    u64 agenda_words;
};

#if BUSTER_INCLUDE_TESTS
// A synthetic simplification input. Parameter p lives in parameter_blocks[p],
// and each block's list keeps ascending parameter order. Its incoming values
// are incoming_values[incoming_offsets[p] .. incoming_offsets[p + 1]).
// `replacements` is the initial forest over value_count values (forwarded
// parameters point away from themselves); `memory_parameters`, when present,
// marks parameters whose owner stays in memory.
typedef struct CTestSsaSimplifyCase CTestSsaSimplifyCase;
struct CTestSsaSimplifyCase
{
    u32 const* parameter_blocks;
    u32 const* parameter_values;
    u32 const* incoming_offsets;
    u32 const* incoming_values;
    u32 const* replacements;
    u8 const* memory_parameters;
    u32 value_count;
    u32 block_count;
    u32 parameter_count;
};

// Production and reference results on independent copies of one case.
// `identical` compares every value's final root and every block's parameter
// list, first/last links and count. `retained` counts surviving parameters.
typedef struct CTestSsaSimplifyResult CTestSsaSimplifyResult;
struct CTestSsaSimplifyResult
{
    CIrSsaSimplifyWork agenda;
    CIrSsaSimplifyWork reference;
    u32 retained;
    bool valid;
    bool identical;
};

BUSTER_F_DECL CTestSsaSimplifyResult c_test_ssa_simplify_parameters(Arena* arena, CTestSsaSimplifyCase const* input);
BUSTER_F_DECL IrValueId c_test_ir_member_place(Arena* arena, Arena* temporary_arena, IrProgram* program,
                                               IrFunction* function, IrValueId operand, String8 member, CPunctuator access,
                                               String8* failure_message);
BUSTER_F_DECL bool c_test_ext80_big_shift_left(CIrExt80Big* value, u32 shift);
BUSTER_F_DECL s32 c_test_ext80_big_compare_shifted(CIrExt80Big const* left, CIrExt80Big const* right, s32 shift);
BUSTER_F_DECL bool c_test_ext80_big_subtract_shifted(CIrExt80Big* left, CIrExt80Big const* right, u32 shift);
BUSTER_F_DECL bool c_test_ext80_big_add(CIrExt80Big* left, CIrExt80Big const* right);
BUSTER_F_DECL bool c_test_ext80_big_multiply(CIrExt80Big const* left, CIrExt80Big const* right, CIrExt80Big* product_out);
BUSTER_F_DECL bool c_test_ext80_parse_rational_literal(String8 spelling, CIrExt80Big* numerator_out, CIrExt80Big* denominator_out,
                                                       s32* binary_exponent_out);
#endif
