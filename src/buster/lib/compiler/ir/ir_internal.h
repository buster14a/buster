#pragma once
#include <buster/lib/compiler/ir/ir.h>

// The old blocks/rows/values an inline batch may rewrite, and the original
// parameter-bearing blocks whose rebuilt incoming lists must be rechecked.
// Every ID at or beyond a base count is new. Only the inliner produces this
// trusted write set; the strict validator remains the independent fallback.
typedef struct IrValidationSplice IrValidationSplice;
struct IrValidationSplice
{
    u32* blocks;
    u32* rows;
    u32* values;
    u32* parameter_blocks;
    u32 block_count;
    u32 row_count;
    u32 value_count;
    u32 parameter_block_count;
    u32 base_blocks;
    u32 base_instructions;
    u32 base_values;
};

// A defect ir_inline_module plants in each spliced caller before its output
// check, so tests can show the splice proof refuses what it covers and that
// checked builds still catch a write outside it. Production passes NONE.
typedef enum IrInlineSpliceFault
{
    IR_INLINE_SPLICE_FAULT_NONE,
    // A new row reads an out-of-range operand.
    IR_INLINE_SPLICE_FAULT_OPERAND,
    // A new block drops its first row from its chain.
    IR_INLINE_SPLICE_FAULT_ORPHAN,
    // A new row also defines a value a row outside the region defines.
    IR_INLINE_SPLICE_FAULT_SECOND_DEFINITION,
    // A new block parameter claims one more incoming value than it has.
    IR_INLINE_SPLICE_FAULT_INCOMING,
    // A row outside the region reads an out-of-range operand.
    IR_INLINE_SPLICE_FAULT_OUTSIDE,
    IR_INLINE_SPLICE_FAULT_COUNT,
} IrInlineSpliceFault;

#if BUSTER_INCLUDE_TESTS
// The unchanged classifier, bypassing every cache, is the side-table oracle.
BUSTER_F_DECL IrAbiValue ir_test_abi_reference(IrProgram* program, IrTypeId type, IrAbiConvention convention, IrAbiUse use);
BUSTER_F_DECL IrFastStatistics ir_test_fast_function(IrProgram* program, IrFunction* function);
// Runs ir_fast_function with a fresh CSE table whose next block generation
// follows `generation`; UINT32_MAX exercises the clearing rollover.
BUSTER_F_DECL IrFastStatistics ir_test_fast_cse_function(IrProgram* program, IrFunction* function, u32 generation);
BUSTER_F_DECL IrValidationError ir_test_validate_va_instruction_operation(IrProgram* program, IrFunction* function, IrType* signature,
                                                                          IrInstruction* instruction);
// The historical three-pass canonical validator (module ownership proof, then
// every value, then blocks and rows), retained as the traversal oracle for the
// fused single-walk ir_validate_canonical_function. Same leaf predicates.
BUSTER_F_DECL IrValidationResult ir_test_validate_canonical_module_reference(IrProgram* program, IrModule* module);
BUSTER_F_DECL IrValidationResult ir_test_validate_canonical_splice(IrProgram* program, IrFunction* function, IrValidationSplice const* splice);
// ir_inline_module with `fault` planted in every spliced caller.
BUSTER_F_DECL IrValidationResult ir_test_inline_module(IrProgram* program, IrModule* module, IrInlineSpliceFault fault);
// One-shot required pricing and unchanged optional pricing for planned counts.
typedef struct IrInlineTableCounts IrInlineTableCounts;
struct IrInlineTableCounts
{
    u64 blocks;
    u64 instructions;
    u64 values;
    u64 extras;
};
typedef struct IrInlineTableCharge IrInlineTableCharge;
struct IrInlineTableCharge
{
    u64 required_bytes;
    u64 required_work;
    u64 optional_bytes;
    u64 optional_work;
};
BUSTER_F_DECL IrInlineTableCharge ir_test_inline_table_charge(IrFunction const* caller, IrInlineTableCounts before,
                                                            IrInlineTableCounts after);
BUSTER_F_DECL void ir_test_inline_reserve_tables(Arena* arena, IrFunction* caller, IrInlineTableCounts counts);
#endif
