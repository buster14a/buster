#pragma once

// Private test seam for production member lookup. The builder remains owned by
// c_gen.c; tests supply distinct persistent and rewindable arenas so an escaped
// search pointer or a type-universe-sized scratch request is observable.
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL IrValueId c_test_ir_member_place(Arena* arena, Arena* temporary_arena, IrProgram* program,
                                               IrFunction* function, IrValueId operand, String8 member, CPunctuator access,
                                               String8* failure_message);
#endif
