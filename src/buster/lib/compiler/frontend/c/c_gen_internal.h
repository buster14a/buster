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
#include <buster/lib/compiler/ir/ir.h>

#define C_IR_EXT80_BIG_LIMBS 1024
typedef struct CIrExt80Big CIrExt80Big;
struct CIrExt80Big
{
    u32 limbs[C_IR_EXT80_BIG_LIMBS];
    u32 count;
};

#if BUSTER_INCLUDE_TESTS
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
