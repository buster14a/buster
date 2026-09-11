#pragma once
#include <buster/lib/compiler/ir/ir.h>

#if BUSTER_INCLUDE_TESTS
// The unchanged classifier, bypassing every cache, is the side-table oracle.
BUSTER_F_DECL IrAbiValue ir_test_abi_reference(IrProgram* program, IrTypeId type, IrAbiConvention convention, IrAbiUse use);
BUSTER_F_DECL IrFastStatistics ir_test_fast_function(IrProgram* program, IrFunction* function);
#endif
