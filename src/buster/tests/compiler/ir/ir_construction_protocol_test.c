// Included by ir_test.c. The block-row construction protocol (ir.h): the same
// function is built through ir_block_append_instruction and, independently,
// through raw ir_function_add_instruction rows with hand-written links, then
// both go through the canonical validator and CFG publication. Every refusal
// is shown to leave the function unchanged; retraction, truncation and
// insertion misuse is refused; publication rejects the family's invalid states
// that raw construction can still represent, with the validator's own error;
// and the C frontend's expression tails after a noreturn operand -- once rows
// committed behind UNREACHABLE on the certified path -- now validate.
#include <buster/lib/compiler/codegen/codegen.h>

typedef struct IrProtocolFixture IrProtocolFixture;
struct IrProtocolFixture
{
    IrProgram program;
    IrFunction* function;
    IrTypeId void_type;
    IrTypeId integer_type;
    IrTypeId bool_type;
    IrValueId argument;
    IrValueId zero;
    IrValueId predicate;
    IrValueId one;
    IrValueId sum;
};

// i32 f(i32 a) { if (a == 0) return a; return a + 1; } over blocks b0, b1, b2,
// with every value allocated up front the way the frontend allocates results
// before their rows.
BUSTER_GLOBAL_LOCAL IrProtocolFixture ir_protocol_fixture(Arena* arena)
{
    IrProtocolFixture fixture = {.program = ir_program_initialize(arena, 1, 4, 0, 0)};
    IrProgram* program = &fixture.program;
    fixture.void_type = ir_program_add_type(program, (IrType){.kind = IR_TYPE_VOID, .layout = {.resolved = true}});
    fixture.integer_type = ir_program_add_type(program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32, .is_signed = true,
                                                                 .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    fixture.bool_type = ir_program_add_type(program, (IrType){.kind = IR_TYPE_BOOLEAN, .bit_width = 1,
                                                              .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    IrTypeId* parameters = arena_allocate(arena, IrTypeId, 1);
    parameters[0] = fixture.integer_type;
    IrTypeId signature = ir_program_add_type(program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = fixture.integer_type, .parameter_types = parameters,
                                                               .parameter_count = 1, .calling_convention = IR_CALLING_CONVENTION_C,
                                                               .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    fixture.function = ir_module_add_function(arena, program->modules, (IrFunction){.canonical_type = signature, .entry = {.value = 0},
                                                                                     .state = IR_FUNCTION_LOWERED});
    for (u32 index = 0; index < 3; index += 1)
    {
        ir_function_add_block(arena, fixture.function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                 .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
    }
    IrValue integer = {.canonical_type = fixture.integer_type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE};
    fixture.argument = ir_function_add_value(arena, fixture.function, integer);
    fixture.zero = ir_function_add_value(arena, fixture.function, integer);
    fixture.predicate = ir_function_add_value(arena, fixture.function,
                                              (IrValue){.canonical_type = fixture.bool_type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
    fixture.one = ir_function_add_value(arena, fixture.function, integer);
    fixture.sum = ir_function_add_value(arena, fixture.function, integer);
    return fixture;
}

BUSTER_GLOBAL_LOCAL IrInstruction ir_protocol_row(IrOpcode opcode, IrTypeId type, IrValueId result)
{
    return (IrInstruction){.canonical_type = type, .symbol = IR_SYMBOL_ID_INVALID, .canonical_local = IR_LOCAL_ID_INVALID,
                           .next = IR_INSTRUCTION_ID_INVALID, .result = result, .opcode = (u8)opcode,
                           .conversion_operation = IR_CONVERSION_COUNT, .unary_operation = IR_UNARY_COUNT, .binary_operation = IR_BINARY_COUNT,
                           .memory_order = IR_MEMORY_ORDER_COUNT, .failure_memory_order = IR_MEMORY_ORDER_COUNT,
                           .atomic_operation = IR_ATOMIC_OPERATION_COUNT};
}

BUSTER_GLOBAL_LOCAL IrInstruction ir_protocol_constant(Arena* arena, IrTypeId type, IrValueId result, u64 value)
{
    IrInstruction row = ir_protocol_row(IR_OPCODE_CONSTANT_INTEGER, type, result);
    row.immediates = arena_allocate(arena, u64, 1);
    row.immediates[0] = value;
    row.immediate_count = 1;
    return row;
}

BUSTER_GLOBAL_LOCAL IrInstruction ir_protocol_binary(Arena* arena, IrTypeId type, IrValueId result, IrBinaryOperation operation, IrValueId left, IrValueId right)
{
    IrInstruction row = ir_protocol_row(IR_OPCODE_BINARY, type, result);
    row.operands = arena_allocate(arena, IrValueId, 2);
    row.operands[0] = left;
    row.operands[1] = right;
    row.operand_count = 2;
    row.binary_operation = (u8)operation;
    return row;
}

BUSTER_GLOBAL_LOCAL IrInstruction ir_protocol_return(Arena* arena, IrTypeId void_type, IrValueId value)
{
    IrInstruction row = ir_protocol_row(IR_OPCODE_RETURN, void_type, IR_VALUE_ID_INVALID);
    row.operands = arena_allocate(arena, IrValueId, 1);
    row.operands[0] = value;
    row.operand_count = 1;
    return row;
}

// The fixture's eight rows, in one interleaved commit order: b0 and b2 both
// receive rows before b0 closes, so no chain is a run of consecutive ids.
// `block_of` names the block each row belongs to.
BUSTER_GLOBAL_LOCAL u32 ir_protocol_rows(Arena* arena, IrProtocolFixture* fixture, IrInstruction* rows, u32* block_of)
{
    rows[0] = ir_protocol_row(IR_OPCODE_ARGUMENT, fixture->integer_type, fixture->argument);
    rows[0].immediates = arena_allocate(arena, u64, 1);
    rows[0].immediates[0] = 0;
    rows[0].immediate_count = 1;
    rows[1] = ir_protocol_constant(arena, fixture->integer_type, fixture->zero, 0);
    rows[2] = ir_protocol_constant(arena, fixture->integer_type, fixture->one, 1);
    rows[3] = ir_protocol_binary(arena, fixture->bool_type, fixture->predicate, IR_BINARY_INTEGER_EQUAL, fixture->argument, fixture->zero);
    rows[4] = ir_protocol_row(IR_OPCODE_BRANCH_IF, fixture->void_type, IR_VALUE_ID_INVALID);
    rows[4].operands = arena_allocate(arena, IrValueId, 1);
    rows[4].operands[0] = fixture->predicate;
    rows[4].operand_count = 1;
    rows[4].targets = arena_allocate(arena, IrBlockId, 2);
    rows[4].targets[0] = (IrBlockId){.value = 1};
    rows[4].targets[1] = (IrBlockId){.value = 2};
    rows[4].target_count = 2;
    rows[5] = ir_protocol_return(arena, fixture->void_type, fixture->argument);
    rows[6] = ir_protocol_binary(arena, fixture->integer_type, fixture->sum, IR_BINARY_INTEGER_ADD, fixture->argument, fixture->one);
    rows[7] = ir_protocol_return(arena, fixture->void_type, fixture->sum);
    u32 blocks[] = {0, 0, 2, 0, 0, 1, 2, 2};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(blocks); index += 1)
    {
        block_of[index] = blocks[index];
    }
    return BUSTER_ARRAY_LENGTH(blocks);
}

// The pre-protocol way every hand-built fixture writes IR: raw rows, then the
// chain links, tails, definitions and the terminated flag by hand.
BUSTER_GLOBAL_LOCAL void ir_protocol_build_raw(Arena* arena, IrProtocolFixture* fixture, IrInstruction* rows, u32* block_of, u32 row_count)
{
    IrFunction* function = fixture->function;
    for (u32 index = 0; index < row_count; index += 1)
    {
        IrInstructionId id = ir_function_add_instruction(arena, function, rows[index], (IrSourceRange){.offset = index});
        IrBlock* block = function->blocks + block_of[index];
        if (block->last_instruction.value == IR_ID_UNDERLYING_INVALID)
        {
            block->first_instruction = id;
        }
        else
        {
            function->instructions[block->last_instruction.value].next = id;
        }
        block->last_instruction = id;
        if (rows[index].result.value != IR_ID_UNDERLYING_INVALID)
        {
            function->values[rows[index].result.value].definition = id;
        }
        block->terminated = rows[index].opcode == IR_OPCODE_BRANCH_IF || rows[index].opcode == IR_OPCODE_RETURN;
    }
}

BUSTER_GLOBAL_LOCAL bool ir_protocol_build_committed(Arena* arena, IrProtocolFixture* fixture, IrInstruction* rows, u32* block_of, u32 row_count)
{
    bool accepted = true;
    for (u32 index = 0; index < row_count; index += 1)
    {
        IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
        IrInstructionId id = ir_block_append_instruction(arena, fixture->function, (IrBlockId){.value = block_of[index]}, rows[index],
                                                         (IrSourceRange){.offset = index}, &refusal);
        accepted &= refusal == IR_COMMIT_ACCEPTED && id.value == index;
    }
    return accepted;
}

// Everything a refused operation must leave alone: counts, every block record
// and every value row, bit for bit.
typedef struct IrProtocolSnapshot IrProtocolSnapshot;
struct IrProtocolSnapshot
{
    IrBlock* blocks;
    IrValue* values;
    IrInstruction* instructions;
    u32 block_count;
    u32 value_count;
    u32 instruction_count;
};

BUSTER_GLOBAL_LOCAL IrProtocolSnapshot ir_protocol_snapshot(Arena* arena, IrFunction* function)
{
    IrProtocolSnapshot snapshot = {.block_count = function->block_count, .value_count = function->value_count,
                                   .instruction_count = function->instruction_count};
    snapshot.blocks = arena_allocate(arena, IrBlock, function->block_count);
    snapshot.values = arena_allocate(arena, IrValue, function->value_count);
    snapshot.instructions = arena_allocate(arena, IrInstruction, function->instruction_count);
    memcpy(snapshot.blocks, function->blocks, sizeof(IrBlock) * function->block_count);
    memcpy(snapshot.values, function->values, sizeof(IrValue) * function->value_count);
    memcpy(snapshot.instructions, function->instructions, sizeof(IrInstruction) * function->instruction_count);
    return snapshot;
}

BUSTER_GLOBAL_LOCAL bool ir_protocol_unchanged(IrProtocolSnapshot snapshot, IrFunction* function)
{
    return snapshot.block_count == function->block_count && snapshot.value_count == function->value_count &&
           snapshot.instruction_count == function->instruction_count && !function->published_cfg &&
           !memcmp(snapshot.blocks, function->blocks, sizeof(IrBlock) * function->block_count) &&
           !memcmp(snapshot.values, function->values, sizeof(IrValue) * function->value_count) &&
           !memcmp(snapshot.instructions, function->instructions, sizeof(IrInstruction) * function->instruction_count);
}

BUSTER_GLOBAL_LOCAL IrCommitRefusal ir_protocol_commit(Arena* arena, IrFunction* function, u32 block, IrInstruction row, IrInstructionId* id_out)
{
    IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
    IrInstructionId id = ir_block_append_instruction(arena, function, (IrBlockId){.value = block}, row, (IrSourceRange){0}, &refusal);
    if (id_out)
    {
        *id_out = id;
    }
    return refusal;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_protocol_value(Arena* arena, IrFunction* function, IrTypeId type)
{
    return ir_function_add_value(arena, function, (IrValue){.canonical_type = type, .definition = IR_INSTRUCTION_ID_INVALID, .category = IR_VALUE_VALUE});
}

BUSTER_GLOBAL_LOCAL bool ir_protocol_same_validation(IrValidationResult left, IrValidationResult right)
{
    return left.error == right.error && left.function.value == right.function.value && left.block.value == right.block.value &&
           left.instruction.value == right.instruction.value;
}

// Differential: both constructions produce the same canonical rows, links,
// definitions and block states, the same validator verdict and the same
// published spans and edges.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_differential_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    IrProtocolFixture raw = ir_protocol_fixture(arena);
    IrProtocolFixture committed = ir_protocol_fixture(arena);
    IrInstruction rows[8];
    u32 block_of[8];
    u32 row_count = ir_protocol_rows(arena, &raw, rows, block_of);
    ir_protocol_build_raw(arena, &raw, rows, block_of, row_count);
    BUSTER_TEST(arguments, ir_protocol_build_committed(arena, &committed, rows, block_of, row_count));
    IrFunction* left = raw.function;
    IrFunction* right = committed.function;
    if (BUSTER_REQUIRE(arguments, left->instruction_count == row_count && right->instruction_count == row_count))
    {
        for (u32 index = 0; index < row_count; index += 1)
        {
            IrInstruction* a = left->instructions + index;
            IrInstruction* b = right->instructions + index;
            BUSTER_TEST(arguments, a->next.value == b->next.value && a->result.value == b->result.value && a->opcode == b->opcode &&
                                       a->operand_count == b->operand_count && a->target_count == b->target_count && a->operands == b->operands &&
                                       a->targets == b->targets && a->immediates == b->immediates);
            BUSTER_TEST(arguments, left->instruction_canonical_sources[index].offset == right->instruction_canonical_sources[index].offset);
        }
        for (u32 index = 0; index < left->block_count; index += 1)
        {
            IrBlock* a = left->blocks + index;
            IrBlock* b = right->blocks + index;
            BUSTER_TEST(arguments, a->first_instruction.value == b->first_instruction.value && a->last_instruction.value == b->last_instruction.value &&
                                       a->terminated == b->terminated && b->terminated);
        }
        BUSTER_TEST(arguments, !memcmp(left->values, right->values, sizeof(IrValue) * left->value_count));
        BUSTER_TEST(arguments, ir_function_first_open_block(right).value == IR_ID_UNDERLYING_INVALID);
        IrValidationResult raw_validation = ir_validate_canonical_module(&raw.program, raw.program.modules);
        IrValidationResult committed_validation = ir_validate_canonical_module(&committed.program, committed.program.modules);
        BUSTER_TEST(arguments, raw_validation.error == IR_VALIDATION_NONE && ir_protocol_same_validation(raw_validation, committed_validation));
        IrValidationResult raw_publication = ir_function_publish_cfg(arena, left);
        IrValidationResult committed_publication = ir_function_publish_cfg(arena, right);
        BUSTER_TEST(arguments, raw_publication.error == IR_VALIDATION_NONE && committed_publication.error == IR_VALIDATION_NONE);
        if (BUSTER_REQUIRE(arguments, left->published_cfg && right->published_cfg))
        {
            IrPublishedCfg const* a = left->published_cfg;
            IrPublishedCfg const* b = right->published_cfg;
            BUSTER_TEST(arguments, a->block_count == b->block_count && a->edge_count == b->edge_count && a->edge_count == 2 &&
                                       !memcmp(a->blocks, b->blocks, sizeof(*a->blocks) * a->block_count) &&
                                       !memcmp(a->edges, b->edges, sizeof(*a->edges) * a->edge_count));
            // Interleaved ids were permuted into dense spans the same way.
            BUSTER_TEST(arguments, a->instruction_remap && b->instruction_remap &&
                                       !memcmp(a->instruction_remap, b->instruction_remap, sizeof(*a->instruction_remap) * row_count));
        }
    }
    scratch_end(temporary);
    return result;
}

// Each refused commit names its reason and leaves the function bit-identical;
// the oracle still accepts the function afterwards.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_refusal_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    IrProtocolFixture fixture = ir_protocol_fixture(arena);
    IrInstruction rows[8];
    u32 block_of[8];
    u32 row_count = ir_protocol_rows(arena, &fixture, rows, block_of);
    BUSTER_TEST(arguments, ir_protocol_build_committed(arena, &fixture, rows, block_of, row_count));
    IrFunction* function = fixture.function;
    // One more block stays open for the misuse cases that need one.
    IrBlock* open = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                      .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
    u32 open_block = open ? open->id.value : 0;
    BUSTER_TEST(arguments, open && ir_function_first_open_block(function).value == open_block);
    IrValueId fresh = ir_protocol_value(arena, function, fixture.integer_type);
    IrProtocolSnapshot snapshot = ir_protocol_snapshot(arena, function);

    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, 0, ir_protocol_constant(arena, fixture.integer_type, fresh, 5), 0) == IR_COMMIT_REFUSED_CLOSED);
    BUSTER_TEST(arguments, ir_protocol_unchanged(snapshot, function));
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, 1, ir_protocol_return(arena, fixture.void_type, fixture.argument), 0) ==
                               IR_COMMIT_REFUSED_CLOSED);
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block + 1, ir_protocol_constant(arena, fixture.integer_type, fresh, 5), 0) ==
                               IR_COMMIT_REFUSED_BLOCK);
    BUSTER_TEST(arguments, ir_protocol_commit(0, function, open_block, ir_protocol_constant(arena, fixture.integer_type, fresh, 5), 0) ==
                               IR_COMMIT_REFUSED_BLOCK);
    IrInstruction dangling_operand = ir_protocol_binary(arena, fixture.integer_type, fresh, IR_BINARY_INTEGER_ADD, fixture.argument,
                                                        (IrValueId){.value = function->value_count});
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, dangling_operand, 0) == IR_COMMIT_REFUSED_OPERAND);
    IrInstruction dangling_target = ir_protocol_row(IR_OPCODE_BRANCH, fixture.void_type, IR_VALUE_ID_INVALID);
    dangling_target.targets = arena_allocate(arena, IrBlockId, 1);
    dangling_target.targets[0] = (IrBlockId){.value = function->block_count};
    dangling_target.target_count = 1;
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, dangling_target, 0) == IR_COMMIT_REFUSED_TARGET);
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, ir_protocol_constant(arena, fixture.integer_type, fixture.zero, 5), 0) ==
                               IR_COMMIT_REFUSED_RESULT);
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, ir_protocol_constant(arena, fixture.bool_type, fresh, 1), 0) ==
                               IR_COMMIT_REFUSED_RESULT);
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block,
                                              ir_protocol_constant(arena, fixture.integer_type, (IrValueId){.value = function->value_count}, 5), 0) ==
                               IR_COMMIT_REFUSED_RESULT);
    IrInstruction missing_storage = ir_protocol_return(arena, fixture.void_type, fixture.argument);
    missing_storage.operands = 0;
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, missing_storage, 0) == IR_COMMIT_REFUSED_STORAGE);
    BUSTER_TEST(arguments, ir_protocol_unchanged(snapshot, function));
    BUSTER_TEST(arguments, ir_commit_refusal_name(IR_COMMIT_REFUSED_CLOSED).length && !string_equal(ir_commit_refusal_name(IR_COMMIT_REFUSAL_COUNT),
                                                                                                    ir_commit_refusal_name(IR_COMMIT_ACCEPTED)));

    // The accepted path after all of that: the open block closes and the
    // oracle agrees nothing invalid was committed along the way.
    IrInstructionId constant = IR_INSTRUCTION_ID_INVALID;
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, ir_protocol_constant(arena, fixture.integer_type, fresh, 5), &constant) ==
                               IR_COMMIT_ACCEPTED);
    BUSTER_TEST(arguments, function->values[fresh.value].definition.value == constant.value && !function->blocks[open_block].terminated);
    BUSTER_TEST(arguments, ir_protocol_commit(arena, function, open_block, ir_protocol_return(arena, fixture.void_type, fresh), 0) == IR_COMMIT_ACCEPTED);
    BUSTER_TEST(arguments, function->blocks[open_block].terminated && ir_function_first_open_block(function).value == IR_ID_UNDERLYING_INVALID);
    BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_NONE);
    scratch_end(temporary);
    return result;
}

// Retraction, truncation and insertion: each misuse is refused unchanged, each
// accepted use leaves a function the oracle accepts.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_edit_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    IrProtocolFixture fixture = ir_protocol_fixture(arena);
    IrInstruction rows[8];
    u32 block_of[8];
    u32 row_count = ir_protocol_rows(arena, &fixture, rows, block_of);
    BUSTER_TEST(arguments, ir_protocol_build_committed(arena, &fixture, rows, block_of, row_count));
    IrFunction* function = fixture.function;
    IrBlock* third = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                       .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
    IrBlockId b3 = third ? third->id : IR_BLOCK_ID_INVALID;
    IrBlock* fourth = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                        .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
    IrBlockId b4 = fourth ? fourth->id : IR_BLOCK_ID_INVALID;
    if (BUSTER_REQUIRE(arguments, b3.value != IR_ID_UNDERLYING_INVALID && b4.value != IR_ID_UNDERLYING_INVALID))
    {
        IrValueId values[4];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
        {
            values[index] = ir_protocol_value(arena, function, fixture.integer_type);
        }

        // Tail retraction.
        IrInstructionId only = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b3.value, ir_protocol_constant(arena, fixture.integer_type, values[0], 1), &only) ==
                                   IR_COMMIT_ACCEPTED);
        IrProtocolSnapshot one_row = ir_protocol_snapshot(arena, function);
        BUSTER_TEST(arguments, !ir_block_retract_tail(function, b3, (IrInstructionId){.value = 0}));
        BUSTER_TEST(arguments, !ir_block_retract_tail(function, b4, IR_INSTRUCTION_ID_INVALID));
        BUSTER_TEST(arguments, !ir_block_retract_tail(function, (IrBlockId){.value = 1}, IR_INSTRUCTION_ID_INVALID));
        BUSTER_TEST(arguments, ir_protocol_unchanged(one_row, function));
        BUSTER_TEST(arguments, ir_block_retract_tail(function, b3, IR_INSTRUCTION_ID_INVALID));
        BUSTER_TEST(arguments, function->instruction_count == only.value && function->values[values[0].value].definition.value == IR_ID_UNDERLYING_INVALID &&
                                   function->blocks[b3.value].first_instruction.value == IR_ID_UNDERLYING_INVALID &&
                                   function->blocks[b3.value].last_instruction.value == IR_ID_UNDERLYING_INVALID);

        // A tail that is not the function's newest row cannot be retracted.
        IrInstructionId third_row = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b3.value, ir_protocol_constant(arena, fixture.integer_type, values[0], 1), &third_row) ==
                                   IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value, ir_protocol_constant(arena, fixture.integer_type, values[1], 2), 0) ==
                                   IR_COMMIT_ACCEPTED);
        IrProtocolSnapshot interleaved = ir_protocol_snapshot(arena, function);
        BUSTER_TEST(arguments, !ir_block_retract_tail(function, b3, IR_INSTRUCTION_ID_INVALID));
        BUSTER_TEST(arguments, ir_protocol_unchanged(interleaved, function));

        // Truncation after b3's row would free b4's newest row, then a b3 row
        // after it: refused. Truncation from a row of another block: refused.
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b3.value, ir_protocol_constant(arena, fixture.integer_type, values[2], 3), 0) ==
                                   IR_COMMIT_ACCEPTED);
        IrProtocolSnapshot split = ir_protocol_snapshot(arena, function);
        BUSTER_TEST(arguments, !ir_block_truncate_after(function, b3, third_row));
        BUSTER_TEST(arguments, !ir_block_truncate_after(function, b4, third_row));
        BUSTER_TEST(arguments, !ir_block_truncate_after(function, b3, (IrInstructionId){.value = function->instruction_count}));
        BUSTER_TEST(arguments, ir_protocol_unchanged(split, function));

        // b4 gains two consecutive rows; truncating after its first frees
        // exactly the newest one and unbinds it.
        IrInstructionId keep = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value,
                                                  ir_protocol_binary(arena, fixture.integer_type, values[3], IR_BINARY_INTEGER_ADD, values[1], values[1]),
                                                  &keep) == IR_COMMIT_ACCEPTED);
        IrValueId extra = ir_protocol_value(arena, function, fixture.integer_type);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value, ir_protocol_constant(arena, fixture.integer_type, extra, 9), 0) ==
                                   IR_COMMIT_ACCEPTED);
        u32 before = function->instruction_count;
        BUSTER_TEST(arguments, ir_block_truncate_after(function, b4, keep));
        BUSTER_TEST(arguments, function->instruction_count == before - 1 && function->values[extra.value].definition.value == IR_ID_UNDERLYING_INVALID &&
                                   function->blocks[b4.value].last_instruction.value == keep.value &&
                                   function->instructions[keep.value].next.value == IR_ID_UNDERLYING_INVALID);
        // The unbound value can be committed again, once.
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value, ir_protocol_constant(arena, fixture.integer_type, extra, 9), 0) ==
                                   IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value, ir_protocol_constant(arena, fixture.integer_type, extra, 9), 0) ==
                                   IR_COMMIT_REFUSED_RESULT);

        // Insertion: never a terminator, never behind one, never after a row
        // that does not exist; a closed block still takes a row before its
        // terminator.
        IrValueId inserted = ir_protocol_value(arena, function, fixture.integer_type);
        IrProtocolSnapshot insertion = ir_protocol_snapshot(arena, function);
        IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
        ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 1}, IR_INSTRUCTION_ID_INVALID,
                                          ir_protocol_return(arena, fixture.void_type, fixture.argument), (IrSourceRange){0}, &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_REFUSED_POSITION);
        ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 0}, (IrInstructionId){.value = 4},
                                          ir_protocol_constant(arena, fixture.integer_type, inserted, 4), (IrSourceRange){0}, &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_REFUSED_POSITION);
        ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 0}, (IrInstructionId){.value = function->instruction_count},
                                          ir_protocol_constant(arena, fixture.integer_type, inserted, 4), (IrSourceRange){0}, &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_REFUSED_POSITION);
        ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 0}, (IrInstructionId){.value = 0},
                                          ir_protocol_constant(arena, fixture.integer_type, fixture.zero, 4), (IrSourceRange){0}, &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_REFUSED_RESULT);
        BUSTER_TEST(arguments, ir_protocol_unchanged(insertion, function));
        IrInstructionId head = ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 1}, IR_INSTRUCTION_ID_INVALID,
                                                                 ir_protocol_constant(arena, fixture.integer_type, inserted, 4), (IrSourceRange){0},
                                                                 &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_ACCEPTED && function->blocks[1].first_instruction.value == head.value &&
                                   function->instructions[head.value].next.value == 5 && function->blocks[1].terminated &&
                                   function->values[inserted.value].definition.value == head.value);

        // Only an edge-less UNREACHABLE that is still the newest row can be
        // retracted from a closed block: a RETURN tail is final, and the
        // reopened block takes the next row where the marker stood.
        IrBlock* fifth = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                           .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
        IrBlockId b5 = fifth ? fifth->id : IR_BLOCK_ID_INVALID;
        IrValueId guarded = ir_protocol_value(arena, function, fixture.integer_type);
        IrInstructionId guard = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b5.value, ir_protocol_constant(arena, fixture.integer_type, guarded, 7), &guard) ==
                                   IR_COMMIT_ACCEPTED);
        IrInstructionId closing = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b5.value, ir_protocol_return(arena, fixture.void_type, guarded), &closing) ==
                                   IR_COMMIT_ACCEPTED);
        IrProtocolSnapshot returned = ir_protocol_snapshot(arena, function);
        BUSTER_TEST(arguments, function->blocks[b5.value].terminated && !ir_block_retract_tail(function, b5, guard));
        BUSTER_TEST(arguments, ir_protocol_unchanged(returned, function));
        // Rebuild the same block ending in UNREACHABLE instead.
        function->blocks[b5.value].terminated = false;
        function->blocks[b5.value].last_instruction = guard;
        function->instructions[guard.value].next = IR_INSTRUCTION_ID_INVALID;
        function->instruction_count = closing.value;
        IrInstructionId marker = IR_INSTRUCTION_ID_INVALID;
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b5.value, ir_protocol_row(IR_OPCODE_UNREACHABLE, fixture.void_type, IR_VALUE_ID_INVALID),
                                                  &marker) == IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, function->blocks[b5.value].terminated && ir_block_retract_tail(function, b5, guard));
        BUSTER_TEST(arguments, !function->blocks[b5.value].terminated && function->blocks[b5.value].last_instruction.value == guard.value &&
                                   function->instruction_count == marker.value);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b5.value, ir_protocol_return(arena, fixture.void_type, guarded), &closing) ==
                                   IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, closing.value == marker.value && function->instructions[guard.value].next.value == closing.value);

        // Finalization names the first open block until every block closes.
        BUSTER_TEST(arguments, ir_function_first_open_block(function).value == b3.value);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b3.value, ir_protocol_return(arena, fixture.void_type, values[0]), 0) ==
                                   IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, ir_function_first_open_block(function).value == b4.value);
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_UNTERMINATED_BLOCK);
        BUSTER_TEST(arguments, ir_protocol_commit(arena, function, b4.value, ir_protocol_return(arena, fixture.void_type, values[3]), 0) ==
                                   IR_COMMIT_ACCEPTED);
        BUSTER_TEST(arguments, ir_function_first_open_block(function).value == IR_ID_UNDERLYING_INVALID);
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, ir_function_publish_cfg(arena, function).error == IR_VALIDATION_NONE);
        // Published rows are immutable spans; tail edits require reopening.
        BUSTER_TEST(arguments, !ir_block_retract_tail(function, b4, IR_INSTRUCTION_ID_INVALID) && !ir_block_truncate_after(function, b4, keep));
    }
    scratch_end(temporary);
    return result;
}

// A published function's blocks are all closed: a refused append leaves the
// publication in place, and an accepted insertion reopens construction first.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_published_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    IrProtocolFixture fixture = ir_protocol_fixture(arena);
    IrInstruction rows[8];
    u32 block_of[8];
    u32 row_count = ir_protocol_rows(arena, &fixture, rows, block_of);
    BUSTER_TEST(arguments, ir_protocol_build_committed(arena, &fixture, rows, block_of, row_count));
    IrFunction* function = fixture.function;
    IrValueId fresh = ir_protocol_value(arena, function, fixture.integer_type);
    BUSTER_TEST(arguments, ir_function_publish_cfg(arena, function).error == IR_VALIDATION_NONE);
    IrPublishedCfg const* cfg = function->published_cfg;
    if (BUSTER_REQUIRE(arguments, cfg != 0))
    {
        IrCommitRefusal refusal = IR_COMMIT_REFUSAL_COUNT;
        ir_block_append_instruction(arena, function, (IrBlockId){.value = 1}, ir_protocol_constant(arena, fixture.integer_type, fresh, 6),
                                    (IrSourceRange){0}, &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_REFUSED_CLOSED && function->published_cfg == cfg);
        // Published b1 is the single row after b0's four.
        IrInstructionId head = ir_block_insert_instruction_after(arena, function, (IrBlockId){.value = 1}, IR_INSTRUCTION_ID_INVALID,
                                                                 ir_protocol_constant(arena, fixture.integer_type, fresh, 6), (IrSourceRange){0},
                                                                 &refusal);
        BUSTER_TEST(arguments, refusal == IR_COMMIT_ACCEPTED && !function->published_cfg && function->blocks[1].first_instruction.value == head.value &&
                                   function->instructions[head.value].next.value == 4);
        BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_NONE);
        BUSTER_TEST(arguments, ir_function_publish_cfg(arena, function).error == IR_VALIDATION_NONE);
    }
    scratch_end(temporary);
    return result;
}

// Raw construction can still write the family's invalid states. Publication,
// which every producer passes, rejects each with the oracle's own error and
// location; before the protocol it published the second one as a block that
// falls off its end and misreported the third as INVALID_ID.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_publication_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    for (u32 fault = 0; fault < 3; fault += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Arena* arena = arguments->arena;
        IrProtocolFixture fixture = ir_protocol_fixture(arena);
        IrInstruction rows[8];
        u32 block_of[8];
        u32 row_count = ir_protocol_rows(arena, &fixture, rows, block_of);
        ir_protocol_build_raw(arena, &fixture, rows, block_of, row_count);
        IrFunction* function = fixture.function;
        IrValidationError expected = IR_VALIDATION_NONE;
        IrBlockId block = {.value = 1};
        IrInstructionId instruction = IR_INSTRUCTION_ID_INVALID;
        if (fault == 0)
        {
            // b1: RETURN a; RETURN a -- a terminator with a successor.
            instruction = ir_function_add_instruction(arena, function, ir_protocol_return(arena, fixture.void_type, fixture.argument), (IrSourceRange){0});
            function->instructions[5].next = instruction;
            function->blocks[1].last_instruction = instruction;
            expected = IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR;
        }
        else if (fault == 1)
        {
            // b1 ends in a constant: a block that falls off its end.
            IrValueId extra = ir_protocol_value(arena, function, fixture.integer_type);
            instruction = ir_function_add_instruction(arena, function, ir_protocol_constant(arena, fixture.integer_type, extra, 3), (IrSourceRange){0});
            function->values[extra.value].definition = instruction;
            function->instructions[5].next = instruction;
            function->blocks[1].last_instruction = instruction;
            function->blocks[1].terminated = false;
            expected = IR_VALIDATION_UNTERMINATED_BLOCK;
        }
        else
        {
            // A fourth block that never received a row.
            IrBlock* empty = ir_function_add_block(arena, function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
                                                                               .last_instruction = IR_INSTRUCTION_ID_INVALID, .sealed = true});
            block = empty ? empty->id : IR_BLOCK_ID_INVALID;
            expected = IR_VALIDATION_UNTERMINATED_BLOCK;
        }
        IrValidationResult oracle = ir_validate_canonical_module(&fixture.program, fixture.program.modules);
        IrValidationResult publication = ir_function_publish_cfg(arena, function);
        BUSTER_TEST(arguments, oracle.error == expected && publication.error == expected);
        BUSTER_TEST(arguments, publication.boundary == IR_VALIDATION_BOUNDARY_CFG_PUBLICATION && !function->published_cfg);
        BUSTER_TEST(arguments, oracle.block.value == block.value && publication.block.value == block.value);
        BUSTER_TEST(arguments, oracle.instruction.value == instruction.value && publication.instruction.value == instruction.value);
        scratch_end(temporary);
    }
    return result;
}

// A value has one definition. A row result that is also a block parameter's
// value passed the pre-protocol validator, CFG publication included.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_definition_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    IrProtocolFixture fixture = ir_protocol_fixture(arena);
    IrInstruction rows[8];
    u32 block_of[8];
    u32 row_count = ir_protocol_rows(arena, &fixture, rows, block_of);
    BUSTER_TEST(arguments, ir_protocol_build_committed(arena, &fixture, rows, block_of, row_count));
    IrFunction* function = fixture.function;
    BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_NONE);
    // b2 gains a parameter whose value is `one`, which row 2 already defines;
    // its single incoming value arrives from b0, b2's only predecessor.
    IrBlock* join = function->blocks + 2;
    IrPredecessor* predecessor = arena_allocate(arena, IrPredecessor, 1);
    *predecessor = (IrPredecessor){.block = {.value = 0}};
    IrIncoming* incoming = arena_allocate(arena, IrIncoming, 1);
    *incoming = (IrIncoming){.predecessor = {.value = 0}, .value = fixture.argument};
    IrBlockParameter* parameter = arena_allocate(arena, IrBlockParameter, 1);
    *parameter = (IrBlockParameter){.first_incoming = incoming, .last_incoming = incoming, .canonical_type = fixture.integer_type,
                                    .canonical_local = IR_LOCAL_ID_INVALID, .value = fixture.one, .incoming_count = 1};
    join->first_predecessor = predecessor;
    join->last_predecessor = predecessor;
    join->predecessor_count = 1;
    join->first_parameter = parameter;
    join->last_parameter = parameter;
    join->parameter_count = 1;
    IrValidationResult doubled = ir_validate_canonical_module(&fixture.program, fixture.program.modules);
    BUSTER_TEST(arguments, doubled.error == IR_VALIDATION_BLOCK_PARAMETER && doubled.instruction.value == 2);
    // The same parameter over a value no row defines is the valid shape.
    parameter->value = ir_protocol_value(arena, function, fixture.integer_type);
    BUSTER_TEST(arguments, ir_validate_canonical_module(&fixture.program, fixture.program.modules).error == IR_VALIDATION_NONE);
    scratch_end(temporary);
    return result;
}

// Expression tails after a noreturn operand. Before the protocol the frontend
// committed these rows behind UNREACHABLE (and left the block ending in them)
// while certifying the module; `-fverify-codegen` rejected them with
// INSTRUCTION_AFTER_TERMINATOR. The builder now retracts the edge-less marker
// once per such function and the tail follows the call in the same block, so
// the certified output validates, publishes, and passes selected-MIR
// verification (definitions still dominate their uses) in both frontend forms
// and every allocator. Functions without such a tail reopen nothing.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_protocol_frontend_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 source = S8("__attribute__((noreturn)) void die(void);\n"
                        "__attribute__((noreturn)) int die_value(void);\n"
                        "void sink(int);\n"
                        "struct pair { int a; };\n"
                        "void take(struct pair);\n"
                        "int after_argument(int x) { sink((die(), x)); return x; }\n"
                        "int after_switch(void) { switch (die_value()) { case 1: return 1; default: return 2; } }\n"
                        "int after_initializer(int x) { int y = (die(), x + 1); return y; }\n"
                        "int after_compound(int x) { take((die(), (struct pair){x})); return 0; }\n");
    String8 control = S8("__attribute__((noreturn)) void die(void);\n"
                         "int control(int x) { if (x) die(); return x + 1; }\n"
                         "int statement(int x) { die(); return x; }\n");
    String8 triples[] = {S8("x86_64-unknown-linux-gnu"), S8("aarch64-unknown-linux-gnu")};
    for (u32 triple = 0; triple < BUSTER_ARRAY_LENGTH(triples); triple += 1)
    {
        TargetParseResult target = target_parse_triple(triples[triple]);
        BUSTER_TEST(arguments, target.error == TARGET_PARSE_ERROR_NONE);
        for (u32 mode = 0; target.error == TARGET_PARSE_ERROR_NONE && mode < 4; mode += 1)
        {
            bool tails = mode < 2;
            CodegenRegisterAllocatorMode allocators[] = {CODEGEN_REGISTER_ALLOCATOR_NONE, CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                                                         CODEGEN_REGISTER_ALLOCATOR_FAST, CODEGEN_REGISTER_ALLOCATOR_QUALITY};
            for (u32 allocator = 0; allocator < BUSTER_ARRAY_LENGTH(allocators); allocator += 1)
            {
                TemporalArena temporary = scratch_begin(&arguments->arena, 1);
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters before = ir_construction_counters();
#endif
                CIRLowerResult lowered = ir_complex_test_lower(temporary.arena, tails ? source : control, target.target, (mode & 1) != 0);
#if BUSTER_BENCH_ALLOCATIONS
                IrConstructionCounters after = ir_construction_counters();
                u64 reopened = after.values[IR_CONSTRUCTION_COMMIT_REOPENED_MARKERS] - before.values[IR_CONSTRUCTION_COMMIT_REOPENED_MARKERS];
                BUSTER_TEST(arguments, tails ? reopened == 4 : reopened == 0);
                BUSTER_TEST(arguments, after.values[IR_CONSTRUCTION_COMMIT_REFUSALS] == before.values[IR_CONSTRUCTION_COMMIT_REFUSALS]);
#endif
                BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
                if (lowered.program && !lowered.diagnostic_count)
                {
                    IrProgram* program = lowered.program;
                    IrModule* module = program->modules;
                    BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                    BUSTER_TEST(arguments, ir_prepare_canonical_module(program, module, false).error == IR_VALIDATION_NONE);
                    CodegenModule artifact = codegen_generate_canonical_module(temporary.arena, program, module, target.target,
                        (CodegenModuleOptions){.register_allocator = (u8)allocators[allocator], .verify_invariants = true});
                    BUSTER_TEST(arguments, artifact.error == CODEGEN_ERROR_NONE);
                }
                scratch_end(temporary);
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_construction_protocol_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = ir_protocol_differential_tests(arguments);
    UnitTestResult parts[] = {
        ir_protocol_refusal_tests(arguments),
        ir_protocol_edit_tests(arguments),
        ir_protocol_published_tests(arguments),
        ir_protocol_publication_tests(arguments),
        ir_protocol_definition_tests(arguments),
        ir_protocol_frontend_tests(arguments),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(parts); index += 1)
    {
        result.test_count += parts[index].test_count;
        result.succeeded_test_count += parts[index].succeeded_test_count;
    }
    return result;
}
