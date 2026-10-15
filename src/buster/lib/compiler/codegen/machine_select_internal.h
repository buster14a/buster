#pragma once

// Private differential controls for selector work reduction. Each context is
// owned by its calling fixture; production and function lanes share no flags.
#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
enum
{
    MACHINE_TEST_SELECT_LEGACY_ALIAS_FACTS = 1u << 0,
    MACHINE_TEST_SELECT_LEGACY_REGISTERS = 1u << 1,
    MACHINE_TEST_SELECT_LEGACY_SIGNATURE = 1u << 2,
};
#define MACHINE_SELECT_TEST_LEGACY(module, flag) ((module) && ((module)->test_work_legacy & (flag)))
BUSTER_F_DECL MachineSelectResult machine_test_select_work(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                          MachineSelectionModule* module, u32 legacy, bool preserve_debug_values);
BUSTER_F_DECL bool machine_x64_test_prepare_signature(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                     MachineSelectionModule* module);
BUSTER_F_DECL u32* machine_test_canonical_layout_full(Arena* arena, IrFunction const* function);
BUSTER_F_DECL void machine_test_classify_instruction_mutability(MachineFunctionBuilder* builder, MachineInstruction const* instruction);
#else
#define MACHINE_SELECT_TEST_LEGACY(module, flag) false
#endif
