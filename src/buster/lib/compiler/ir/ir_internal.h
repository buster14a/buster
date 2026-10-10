#pragma once
#include <buster/lib/compiler/ir/ir.h>

#if BUSTER_INCLUDE_TESTS
// The unchanged classifier, bypassing every cache, is the side-table oracle.
BUSTER_F_DECL IrAbiValue ir_test_abi_reference(IrProgram* program, IrTypeId type, IrAbiConvention convention, IrAbiUse use);
BUSTER_F_DECL IrFastStatistics ir_test_fast_function(IrProgram* program, IrFunction* function);
BUSTER_F_DECL IrValidationError ir_test_validate_va_instruction_operation(IrProgram* program, IrFunction* function, IrType* signature,
                                                                          IrInstruction* instruction);
// The historical three-pass canonical validator (module ownership proof, then
// every value, then blocks and rows), retained as the traversal oracle for the
// fused single-walk ir_validate_canonical_function. Same leaf predicates.
BUSTER_F_DECL IrValidationResult ir_test_validate_canonical_module_reference(IrProgram* program, IrModule* module);
#endif
