#pragma once

// Private differential and capacity controls for direct parameter-edge layout.
#include <buster/lib/compiler/codegen/machine.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL MachineSelectResult machine_test_select_parameter_edge_layout(Arena* arena, IrProgram* program, IrFunction* function,
                                                                           Target target, bool direct_layout, bool preserve_debug_values);
BUSTER_F_DECL MachinePoint machine_test_builder_definition_point(MachineFunctionBuilder* builder, u32 instruction_index);
#endif
