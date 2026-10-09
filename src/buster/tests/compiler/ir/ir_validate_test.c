// Included by ir_test.c. The fused single-walk canonical validator against
// two independent checkers: the historical three-pass traversal
// (ir_test_validate_canonical_module_reference, same leaf predicates) and a
// brute-force structural oracle written from the definitions alone. The shapes
// cover a reducible loop with a parameter cycle, an irreducible region whose
// two headers carry mutually dependent parameters, unreachable blocks with an
// edge into a live join, a dead self-loop and a predecessor-less block, switch
// fan-out with duplicate targets, a wide multi-parameter join, LOCAL/LOAD
// rows, the published form and fixed-seed random graphs. Each mutation injects
// exactly one fault; the two validators must report the same error at the
// same site, except for the one documented chain-order divergence.

typedef struct IrValidateTestBuilder IrValidateTestBuilder;
struct IrValidateTestBuilder
{
    Arena* arena;
    IrProgram program;
    IrFunction* function;
    IrTypeId void_type;
    IrTypeId i32_type;
    IrTypeId bool_type;
    IrTypeId i64_type;
    IrTypeId function_type;
    // An integer type whose layout is unresolved: naming it is the one way a
    // metadata-free value fails the shape predicate.
    IrTypeId unresolved_type;
};

typedef enum IrValidateTestMutation
{
    IR_VALIDATE_TEST_NONE,
    IR_VALIDATE_TEST_OPERAND_RANGE,
    IR_VALIDATE_TEST_TARGET_RANGE,
    IR_VALIDATE_TEST_RESULT_REBOUND,
    IR_VALIDATE_TEST_RESULT_TYPE,
    IR_VALIDATE_TEST_VALUE_TYPE_RANGE,
    IR_VALIDATE_TEST_VALUE_UNRESOLVED,
    IR_VALIDATE_TEST_VALUE_ALIGNMENT,
    IR_VALIDATE_TEST_CHAIN_CYCLE,
    IR_VALIDATE_TEST_WRONG_TAIL,
    IR_VALIDATE_TEST_AFTER_TERMINATOR,
    IR_VALIDATE_TEST_UNOWNED_ROW,
    IR_VALIDATE_TEST_SHARED_TAIL_EARLIER,
    IR_VALIDATE_TEST_SHARED_TAIL_LATER,
    IR_VALIDATE_TEST_UNTERMINATED_FLAG,
    IR_VALIDATE_TEST_UNSEALED,
    IR_VALIDATE_TEST_PARAMETER_RANGE,
    IR_VALIDATE_TEST_PARAMETER_DUPLICATE,
    IR_VALIDATE_TEST_PARAMETER_IS_RESULT,
    IR_VALIDATE_TEST_PARAMETER_TYPE,
    IR_VALIDATE_TEST_INCOMING_SHORT,
    IR_VALIDATE_TEST_INCOMING_ORDER,
    IR_VALIDATE_TEST_INCOMING_TYPE,
    IR_VALIDATE_TEST_INCOMING_RANGE,
    IR_VALIDATE_TEST_INCOMING_TAIL,
    IR_VALIDATE_TEST_ORPHAN_UNDEFINED,
    IR_VALIDATE_TEST_ORPHAN_RANGE,
    IR_VALIDATE_TEST_ORPHAN_ACCEPTED,
    IR_VALIDATE_TEST_OPCODE_RANGE,
    IR_VALIDATE_TEST_STORAGE_NULL,
    IR_VALIDATE_TEST_LOAD_NO_OPERAND,
    IR_VALIDATE_TEST_RETURN_TYPE,
    IR_VALIDATE_TEST_CONDITION_TYPE,
    IR_VALIDATE_TEST_SWITCH_COUNT,
    IR_VALIDATE_TEST_BINARY_MISMATCH,
    IR_VALIDATE_TEST_ENTRY_RANGE,
    IR_VALIDATE_TEST_SIGNATURE,
    IR_VALIDATE_TEST_MUTATION_COUNT,
} IrValidateTestMutation;

// The error each mutation must produce from both validators, and whether the
// (block, instruction) site must agree as well. SHARED_TAIL_LATER is the one
// mutation where the traversals legitimately differ: a terminator whose next
// link runs into a later block's chain violates ownership, termination order
// and sharing at once; the reference's module-wide ownership pass reaches the
// tail mismatch first while the fused walk refuses the first row behind the
// terminator. Both reject; the pair is pinned below.
BUSTER_GLOBAL_LOCAL IrValidationError ir_validate_test_expected(IrValidateTestMutation mutation)
{
    IrValidationError result;
    switch (mutation)
    {
    case IR_VALIDATE_TEST_NONE:
    case IR_VALIDATE_TEST_ORPHAN_ACCEPTED: result = IR_VALIDATION_NONE; break;
    case IR_VALIDATE_TEST_OPERAND_RANGE:
    case IR_VALIDATE_TEST_VALUE_TYPE_RANGE:
    case IR_VALIDATE_TEST_ORPHAN_UNDEFINED:
    case IR_VALIDATE_TEST_ORPHAN_RANGE:
    case IR_VALIDATE_TEST_OPCODE_RANGE:
    case IR_VALIDATE_TEST_ENTRY_RANGE:
    case IR_VALIDATE_TEST_SIGNATURE: result = IR_VALIDATION_INVALID_ID; break;
    case IR_VALIDATE_TEST_TARGET_RANGE:
    case IR_VALIDATE_TEST_CONDITION_TYPE:
    case IR_VALIDATE_TEST_SWITCH_COUNT: result = IR_VALIDATION_BRANCH_TARGET; break;
    case IR_VALIDATE_TEST_RESULT_REBOUND:
    case IR_VALIDATE_TEST_RESULT_TYPE: result = IR_VALIDATION_RESULT_TYPE; break;
    case IR_VALIDATE_TEST_VALUE_UNRESOLVED:
    case IR_VALIDATE_TEST_STORAGE_NULL:
    case IR_VALIDATE_TEST_LOAD_NO_OPERAND:
    case IR_VALIDATE_TEST_BINARY_MISMATCH: result = IR_VALIDATION_OPERATION; break;
    case IR_VALIDATE_TEST_VALUE_ALIGNMENT: result = IR_VALIDATION_ALIGNMENT; break;
    case IR_VALIDATE_TEST_CHAIN_CYCLE:
    case IR_VALIDATE_TEST_WRONG_TAIL:
    case IR_VALIDATE_TEST_UNOWNED_ROW:
    case IR_VALIDATE_TEST_SHARED_TAIL_EARLIER:
    case IR_VALIDATE_TEST_SHARED_TAIL_LATER: result = IR_VALIDATION_INSTRUCTION_OWNERSHIP; break;
    case IR_VALIDATE_TEST_AFTER_TERMINATOR: result = IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR; break;
    case IR_VALIDATE_TEST_UNTERMINATED_FLAG: result = IR_VALIDATION_UNTERMINATED_BLOCK; break;
    case IR_VALIDATE_TEST_RETURN_TYPE: result = IR_VALIDATION_RETURN_TYPE; break;
    case IR_VALIDATE_TEST_UNSEALED:
    case IR_VALIDATE_TEST_PARAMETER_RANGE:
    case IR_VALIDATE_TEST_PARAMETER_DUPLICATE:
    case IR_VALIDATE_TEST_PARAMETER_IS_RESULT:
    case IR_VALIDATE_TEST_PARAMETER_TYPE:
    case IR_VALIDATE_TEST_INCOMING_SHORT:
    case IR_VALIDATE_TEST_INCOMING_ORDER:
    case IR_VALIDATE_TEST_INCOMING_TYPE:
    case IR_VALIDATE_TEST_INCOMING_RANGE:
    case IR_VALIDATE_TEST_INCOMING_TAIL: result = IR_VALIDATION_BLOCK_PARAMETER; break;
    case IR_VALIDATE_TEST_MUTATION_COUNT:
    default: BUSTER_TODO();
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ir_validate_test_random(u64* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 33;
}

BUSTER_GLOBAL_LOCAL IrValidateTestBuilder ir_validate_test_builder(Arena* arena)
{
    IrValidateTestBuilder builder = {.arena = arena};
    builder.program = ir_program_initialize(arena, 1, 8, 0, 0);
    builder.void_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_VOID, .layout = {.alignment = 1, .resolved = true}});
    builder.i32_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32, .is_signed = true,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    builder.bool_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_BOOLEAN, .bit_width = 1,
        .layout = {.size = 1, .alignment = 1, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    builder.i64_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 64, .is_signed = true,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_INTEGER, .resolved = true}});
    builder.function_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = builder.i32_type,
        .calling_convention = IR_CALLING_CONVENTION_C, .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    builder.unresolved_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_INTEGER, .bit_width = 32, .is_signed = true,
        .layout = {.size = 4, .alignment = 4, .abi_class = IR_ABI_CLASS_INTEGER}});
    builder.function = ir_module_add_function(arena, builder.program.modules,
                                              (IrFunction){.canonical_type = builder.function_type, .entry = {.value = 0}, .state = IR_FUNCTION_LOWERED});
    return builder;
}

BUSTER_GLOBAL_LOCAL u32 ir_validate_test_block(IrValidateTestBuilder* builder)
{
    IrBlock* block = ir_function_add_block(builder->arena, builder->function, (IrBlock){.first_instruction = IR_INSTRUCTION_ID_INVALID,
        .last_instruction = IR_INSTRUCTION_ID_INVALID, .terminated = true, .sealed = true});
    return block->id.value;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_validate_test_value(IrValidateTestBuilder* builder, IrTypeId type, IrValueCategory category)
{
    return ir_function_add_value(builder->arena, builder->function,
                                 (IrValue){.canonical_type = type, .definition = IR_INSTRUCTION_ID_INVALID, .category = (u8)category});
}

// Appends the row, links it as the block's tail and binds its result. Pointers
// into the function's arrays are taken after the append, which may move them.
BUSTER_GLOBAL_LOCAL IrInstructionId ir_validate_test_row(IrValidateTestBuilder* builder, u32 block_index, IrInstruction instruction)
{
    IrFunction* function = builder->function;
    instruction.next = IR_INSTRUCTION_ID_INVALID;
    IrInstructionId id = ir_function_add_instruction(builder->arena, function, instruction, (IrSourceRange){0});
    IrBlock* block = function->blocks + block_index;
    if (block->last_instruction.value != IR_ID_UNDERLYING_INVALID)
    {
        function->instructions[block->last_instruction.value].next = id;
    }
    else
    {
        block->first_instruction = id;
    }
    block->last_instruction = id;
    if (instruction.result.value != IR_ID_UNDERLYING_INVALID)
    {
        function->values[instruction.result.value].definition = id;
    }
    return id;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_validate_test_constant(IrValidateTestBuilder* builder, u32 block, IrTypeId type, u64 immediate)
{
    IrValueId value = ir_validate_test_value(builder, type, IR_VALUE_VALUE);
    u64* immediates = arena_allocate(builder->arena, u64, 1);
    immediates[0] = immediate;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER, .canonical_type = type, .result = value,
        .immediates = immediates, .immediate_count = 1});
    return value;
}

BUSTER_GLOBAL_LOCAL IrValueId ir_validate_test_binary(IrValidateTestBuilder* builder, u32 block, IrBinaryOperation operation, IrValueId left,
                                                     IrValueId right, IrTypeId result_type)
{
    IrValueId value = ir_validate_test_value(builder, result_type, IR_VALUE_VALUE);
    IrValueId* operands = arena_allocate(builder->arena, IrValueId, 2);
    operands[0] = left;
    operands[1] = right;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_BINARY, .binary_operation = (u8)operation, .canonical_type = result_type,
        .result = value, .operands = operands, .operand_count = 2});
    return value;
}

BUSTER_GLOBAL_LOCAL void ir_validate_test_branch(IrValidateTestBuilder* builder, u32 block, u32 target)
{
    IrBlockId* targets = arena_allocate(builder->arena, IrBlockId, 1);
    targets[0].value = target;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_BRANCH, .canonical_type = builder->void_type,
        .result = IR_VALUE_ID_INVALID, .targets = targets, .target_count = 1});
}

BUSTER_GLOBAL_LOCAL void ir_validate_test_branch_if(IrValidateTestBuilder* builder, u32 block, IrValueId condition, u32 then_target, u32 else_target)
{
    IrBlockId* targets = arena_allocate(builder->arena, IrBlockId, 2);
    IrValueId* operands = arena_allocate(builder->arena, IrValueId, 1);
    targets[0].value = then_target;
    targets[1].value = else_target;
    operands[0] = condition;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_BRANCH_IF, .canonical_type = builder->void_type,
        .result = IR_VALUE_ID_INVALID, .operands = operands, .operand_count = 1, .targets = targets, .target_count = 2});
}

// `targets[0]` is the default; cases take the immediates 0 .. count - 2.
BUSTER_GLOBAL_LOCAL void ir_validate_test_switch(IrValidateTestBuilder* builder, u32 block, IrValueId switched, u32 const* target_blocks, u32 target_count)
{
    IrBlockId* targets = arena_allocate(builder->arena, IrBlockId, target_count);
    u64* immediates = arena_allocate(builder->arena, u64, target_count);
    IrValueId* operands = arena_allocate(builder->arena, IrValueId, 1);
    for (u32 index = 0; index < target_count; index += 1)
    {
        targets[index].value = target_blocks[index];
        immediates[index] = index;
    }
    operands[0] = switched;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_SWITCH, .canonical_type = builder->void_type, .result = IR_VALUE_ID_INVALID,
        .operands = operands, .operand_count = 1, .targets = targets, .target_count = (u16)target_count, .immediates = immediates,
        .immediate_count = (u16)(target_count - 1)});
}

BUSTER_GLOBAL_LOCAL void ir_validate_test_return(IrValidateTestBuilder* builder, u32 block, IrValueId value)
{
    IrValueId* operands = arena_allocate(builder->arena, IrValueId, 1);
    operands[0] = value;
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_RETURN, .canonical_type = builder->void_type,
        .result = IR_VALUE_ID_INVALID, .operands = operands, .operand_count = 1});
}

BUSTER_GLOBAL_LOCAL void ir_validate_test_unreachable(IrValidateTestBuilder* builder, u32 block)
{
    ir_validate_test_row(builder, block, (IrInstruction){.opcode = IR_OPCODE_UNREACHABLE, .canonical_type = builder->void_type, .result = IR_VALUE_ID_INVALID});
}

BUSTER_GLOBAL_LOCAL IrBlockParameter* ir_validate_test_parameter(IrValidateTestBuilder* builder, u32 block_index, IrValueId value)
{
    IrBlockParameter* parameter = arena_allocate(builder->arena, IrBlockParameter, 1);
    *parameter = (IrBlockParameter){.value = value, .canonical_type = builder->function->values[value.value].canonical_type,
        .canonical_local = IR_LOCAL_ID_INVALID};
    IrBlock* block = builder->function->blocks + block_index;
    if (block->last_parameter)
    {
        block->last_parameter->next = parameter;
    }
    else
    {
        block->first_parameter = parameter;
    }
    block->last_parameter = parameter;
    block->parameter_count += 1;
    return parameter;
}

BUSTER_GLOBAL_LOCAL void ir_validate_test_incoming(IrValidateTestBuilder* builder, IrBlockParameter* parameter, u32 predecessor, IrValueId value)
{
    IrIncoming* incoming = arena_allocate(builder->arena, IrIncoming, 1);
    *incoming = (IrIncoming){.predecessor = {.value = predecessor}, .value = value};
    if (parameter->last_incoming)
    {
        parameter->last_incoming->next = incoming;
    }
    else
    {
        parameter->first_incoming = incoming;
    }
    parameter->last_incoming = incoming;
    parameter->incoming_count += 1;
}

// Predecessor lists from the terminators: one entry per distinct source
// block, ascending by source, the order the frontend publishes and every
// incoming list here follows. Quadratic in blocks, which stay small.
BUSTER_GLOBAL_LOCAL void ir_validate_test_seal(IrValidateTestBuilder* builder)
{
    IrFunction* function = builder->function;
    for (u32 target = 0; target < function->block_count; target += 1)
    {
        for (u32 source = 0; source < function->block_count; source += 1)
        {
            IrBlock* origin = function->blocks + source;
            bool edge = false;
            if (origin->last_instruction.value != IR_ID_UNDERLYING_INVALID)
            {
                IrInstruction* terminator = function->instructions + origin->last_instruction.value;
                for (u32 index = 0; index < terminator->target_count; index += 1)
                {
                    edge |= terminator->targets[index].value == target;
                }
            }
            if (edge)
            {
                IrBlock* destination = function->blocks + target;
                IrPredecessor* predecessor = arena_allocate(builder->arena, IrPredecessor, 1);
                *predecessor = (IrPredecessor){.block = {.value = source}};
                if (destination->last_predecessor)
                {
                    destination->last_predecessor->next = predecessor;
                }
                else
                {
                    destination->first_predecessor = predecessor;
                }
                destination->last_predecessor = predecessor;
                destination->predecessor_count += 1;
            }
        }
    }
}

typedef enum IrValidateTestShape
{
    IR_VALIDATE_TEST_SHAPE_LINEAR,
    IR_VALIDATE_TEST_SHAPE_DIAMOND,
    IR_VALIDATE_TEST_SHAPE_LOOP,
    IR_VALIDATE_TEST_SHAPE_IRREDUCIBLE,
    IR_VALIDATE_TEST_SHAPE_UNREACHABLE,
    IR_VALIDATE_TEST_SHAPE_SWITCH,
    IR_VALIDATE_TEST_SHAPE_WIDE_JOIN,
    IR_VALIDATE_TEST_SHAPE_LOCAL_LOAD,
    IR_VALIDATE_TEST_SHAPE_COUNT,
} IrValidateTestShape;

BUSTER_GLOBAL_LOCAL void ir_validate_test_shape(IrValidateTestBuilder* builder, IrValidateTestShape shape)
{
    switch (shape)
    {
    case IR_VALIDATE_TEST_SHAPE_LINEAR:
    {
        u32 entry = ir_validate_test_block(builder);
        IrValueId constant = ir_validate_test_constant(builder, entry, builder->i32_type, 7);
        ir_validate_test_return(builder, entry, constant);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_DIAMOND:
    {
        // The join parameter is the last value, so PARAMETER_IS_RESULT can
        // retire it without leaving an orphan behind.
        u32 entry = ir_validate_test_block(builder);
        u32 then_block = ir_validate_test_block(builder);
        u32 else_block = ir_validate_test_block(builder);
        u32 join = ir_validate_test_block(builder);
        ir_validate_test_constant(builder, entry, builder->i32_type, 0);
        IrValueId condition = ir_validate_test_constant(builder, entry, builder->bool_type, 1);
        ir_validate_test_branch_if(builder, entry, condition, then_block, else_block);
        IrValueId then_value = ir_validate_test_constant(builder, then_block, builder->i32_type, 1);
        ir_validate_test_branch(builder, then_block, join);
        IrValueId else_value = ir_validate_test_constant(builder, else_block, builder->i32_type, 2);
        ir_validate_test_branch(builder, else_block, join);
        IrValueId merged = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_return(builder, join, merged);
        IrBlockParameter* parameter = ir_validate_test_parameter(builder, join, merged);
        ir_validate_test_incoming(builder, parameter, then_block, then_value);
        ir_validate_test_incoming(builder, parameter, else_block, else_value);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_LOOP:
    {
        // header(p): p == limit ? leave : body; body: p + 1 -> header. The
        // parameter and its increment form a cycle.
        u32 entry = ir_validate_test_block(builder);
        u32 header = ir_validate_test_block(builder);
        u32 body = ir_validate_test_block(builder);
        u32 leave = ir_validate_test_block(builder);
        IrValueId initial = ir_validate_test_constant(builder, entry, builder->i32_type, 0);
        IrValueId one = ir_validate_test_constant(builder, entry, builder->i32_type, 1);
        IrValueId limit = ir_validate_test_constant(builder, entry, builder->i32_type, 10);
        ir_validate_test_branch(builder, entry, header);
        IrValueId induction = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        IrValueId done = ir_validate_test_binary(builder, header, IR_BINARY_INTEGER_EQUAL, induction, limit, builder->bool_type);
        ir_validate_test_branch_if(builder, header, done, leave, body);
        IrValueId next = ir_validate_test_binary(builder, body, IR_BINARY_INTEGER_ADD, induction, one, builder->i32_type);
        ir_validate_test_branch(builder, body, header);
        ir_validate_test_return(builder, leave, induction);
        IrBlockParameter* parameter = ir_validate_test_parameter(builder, header, induction);
        ir_validate_test_incoming(builder, parameter, entry, initial);
        ir_validate_test_incoming(builder, parameter, body, next);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_IRREDUCIBLE:
    {
        // entry -> {a, b}; a(pa) -> b; b(pb) -> {a, leave}. Each header's
        // parameter takes the other's as an incoming value.
        u32 entry = ir_validate_test_block(builder);
        u32 a = ir_validate_test_block(builder);
        u32 b = ir_validate_test_block(builder);
        u32 leave = ir_validate_test_block(builder);
        IrValueId initial = ir_validate_test_constant(builder, entry, builder->i32_type, 0);
        IrValueId condition = ir_validate_test_constant(builder, entry, builder->bool_type, 1);
        ir_validate_test_branch_if(builder, entry, condition, a, b);
        IrValueId pa = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        IrValueId pb = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_branch(builder, a, b);
        IrValueId again = ir_validate_test_constant(builder, b, builder->bool_type, 0);
        ir_validate_test_branch_if(builder, b, again, a, leave);
        ir_validate_test_return(builder, leave, pb);
        IrBlockParameter* parameter_a = ir_validate_test_parameter(builder, a, pa);
        IrBlockParameter* parameter_b = ir_validate_test_parameter(builder, b, pb);
        ir_validate_test_incoming(builder, parameter_a, entry, initial);
        ir_validate_test_incoming(builder, parameter_a, b, pb);
        ir_validate_test_incoming(builder, parameter_b, entry, initial);
        ir_validate_test_incoming(builder, parameter_b, a, pa);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_UNREACHABLE:
    {
        // A dead block feeds the live join; a dead self-loop carries a
        // self-referencing parameter; a predecessor-less dead block carries
        // a parameter with no incoming at all.
        u32 entry = ir_validate_test_block(builder);
        u32 dead = ir_validate_test_block(builder);
        u32 join = ir_validate_test_block(builder);
        u32 self_loop = ir_validate_test_block(builder);
        u32 isolated = ir_validate_test_block(builder);
        IrValueId live_value = ir_validate_test_constant(builder, entry, builder->i32_type, 0);
        ir_validate_test_branch(builder, entry, join);
        IrValueId dead_value = ir_validate_test_constant(builder, dead, builder->i32_type, 5);
        ir_validate_test_branch(builder, dead, join);
        IrValueId merged = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_return(builder, join, merged);
        IrValueId spinning = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_branch(builder, self_loop, self_loop);
        IrValueId orphaned = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_return(builder, isolated, orphaned);
        IrBlockParameter* parameter = ir_validate_test_parameter(builder, join, merged);
        ir_validate_test_incoming(builder, parameter, entry, live_value);
        ir_validate_test_incoming(builder, parameter, dead, dead_value);
        IrBlockParameter* self_parameter = ir_validate_test_parameter(builder, self_loop, spinning);
        ir_validate_test_incoming(builder, self_parameter, self_loop, spinning);
        ir_validate_test_parameter(builder, isolated, orphaned);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_SWITCH:
    {
        u32 entry = ir_validate_test_block(builder);
        u32 first = ir_validate_test_block(builder);
        u32 second = ir_validate_test_block(builder);
        u32 join = ir_validate_test_block(builder);
        u32 targets[] = {first, second, first, second};
        IrValueId selector = ir_validate_test_constant(builder, entry, builder->i32_type, 3);
        ir_validate_test_switch(builder, entry, selector, targets, BUSTER_ARRAY_LENGTH(targets));
        IrValueId first_value = ir_validate_test_constant(builder, first, builder->i32_type, 1);
        ir_validate_test_branch(builder, first, join);
        IrValueId second_value = ir_validate_test_constant(builder, second, builder->i32_type, 2);
        ir_validate_test_branch(builder, second, join);
        IrValueId merged = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        ir_validate_test_return(builder, join, merged);
        IrBlockParameter* parameter = ir_validate_test_parameter(builder, join, merged);
        ir_validate_test_incoming(builder, parameter, first, first_value);
        ir_validate_test_incoming(builder, parameter, second, second_value);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_WIDE_JOIN:
    {
        enum
        {
            IR_VALIDATE_TEST_WIDE_DEGREE = 6,
            IR_VALIDATE_TEST_WIDE_WIDTH = 3,
        };
        u32 entry = ir_validate_test_block(builder);
        u32 arms[IR_VALIDATE_TEST_WIDE_DEGREE];
        IrValueId arm_values[IR_VALIDATE_TEST_WIDE_DEGREE];
        for (u32 arm = 0; arm < IR_VALIDATE_TEST_WIDE_DEGREE; arm += 1)
        {
            arms[arm] = ir_validate_test_block(builder);
        }
        u32 join = ir_validate_test_block(builder);
        IrValueId selector = ir_validate_test_constant(builder, entry, builder->i32_type, 1);
        ir_validate_test_switch(builder, entry, selector, arms, IR_VALIDATE_TEST_WIDE_DEGREE);
        for (u32 arm = 0; arm < IR_VALIDATE_TEST_WIDE_DEGREE; arm += 1)
        {
            arm_values[arm] = ir_validate_test_constant(builder, arms[arm], builder->i32_type, arm);
            ir_validate_test_branch(builder, arms[arm], join);
        }
        IrValueId merged[IR_VALIDATE_TEST_WIDE_WIDTH];
        for (u32 width = 0; width < IR_VALIDATE_TEST_WIDE_WIDTH; width += 1)
        {
            merged[width] = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        }
        ir_validate_test_return(builder, join, merged[0]);
        for (u32 width = 0; width < IR_VALIDATE_TEST_WIDE_WIDTH; width += 1)
        {
            IrBlockParameter* parameter = ir_validate_test_parameter(builder, join, merged[width]);
            for (u32 arm = 0; arm < IR_VALIDATE_TEST_WIDE_DEGREE; arm += 1)
            {
                ir_validate_test_incoming(builder, parameter, arms[arm], arm_values[arm]);
            }
        }
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_LOCAL_LOAD:
    {
        u32 entry = ir_validate_test_block(builder);
        IrValueId place = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_PLACE);
        IrValueId loaded = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        IrValueId* operands = arena_allocate(builder->arena, IrValueId, 1);
        operands[0] = place;
        builder->function->local_count = 1;
        ir_validate_test_row(builder, entry, (IrInstruction){.opcode = IR_OPCODE_LOCAL, .canonical_type = builder->i32_type, .result = place,
            .canonical_local = {.value = 0}});
        ir_validate_test_row(builder, entry, (IrInstruction){.opcode = IR_OPCODE_LOAD, .canonical_type = builder->i32_type, .result = loaded,
            .operands = operands, .operand_count = 1});
        ir_validate_test_return(builder, entry, loaded);
    }
    break;
    case IR_VALIDATE_TEST_SHAPE_COUNT:
    default: BUSTER_TODO();
    }
    ir_validate_test_seal(builder);
}

// A fixed-seed random function: blocks with constants and one terminator of
// any kind over random targets (duplicates included), then parameters at a
// random subset of blocks whose incoming values are any integer value of the
// function -- later parameters, the parameter itself or a value in a block
// that never reaches the join, since the validator checks no dominance.
BUSTER_GLOBAL_LOCAL void ir_validate_test_random_function(IrValidateTestBuilder* builder, u64 seed)
{
    u64 state = seed;
    u32 block_count = 2 + (u32)(ir_validate_test_random(&state) % 12);
    for (u32 block = 0; block < block_count; block += 1)
    {
        ir_validate_test_block(builder);
    }
    for (u32 block = 0; block < block_count; block += 1)
    {
        IrValueId integer = ir_validate_test_constant(builder, block, builder->i32_type, ir_validate_test_random(&state) % 100);
        IrValueId truth = IR_VALUE_ID_INVALID;
        u32 extra = (u32)(ir_validate_test_random(&state) % 3);
        for (u32 index = 0; index < extra; index += 1)
        {
            if (ir_validate_test_random(&state) % 2)
            {
                truth = ir_validate_test_constant(builder, block, builder->bool_type, ir_validate_test_random(&state) % 2);
            }
            else
            {
                integer = ir_validate_test_constant(builder, block, builder->i32_type, ir_validate_test_random(&state) % 100);
            }
        }
        u32 kind = (u32)(ir_validate_test_random(&state) % 5);
        if (kind == 0)
        {
            ir_validate_test_branch(builder, block, (u32)(ir_validate_test_random(&state) % block_count));
        }
        else if (kind == 1)
        {
            if (truth.value == IR_ID_UNDERLYING_INVALID)
            {
                truth = ir_validate_test_constant(builder, block, builder->bool_type, 1);
            }
            ir_validate_test_branch_if(builder, block, truth, (u32)(ir_validate_test_random(&state) % block_count),
                                       (u32)(ir_validate_test_random(&state) % block_count));
        }
        else if (kind == 2)
        {
            u32 targets[4];
            u32 target_count = 1 + (u32)(ir_validate_test_random(&state) % 4);
            for (u32 index = 0; index < target_count; index += 1)
            {
                targets[index] = (u32)(ir_validate_test_random(&state) % block_count);
            }
            ir_validate_test_switch(builder, block, integer, targets, target_count);
        }
        else if (kind == 3)
        {
            ir_validate_test_return(builder, block, integer);
        }
        else
        {
            ir_validate_test_unreachable(builder, block);
        }
    }
    ir_validate_test_seal(builder);
    // Every parameter value exists before any incoming list is drawn, so a
    // list can name a parameter of a later block, or its own.
    IrFunction* function = builder->function;
    u32 parameter_counts[16];
    u32 first_parameter_value = function->value_count;
    for (u32 block = 0; block < block_count; block += 1)
    {
        u32 draw = (u32)(ir_validate_test_random(&state) % 4);
        parameter_counts[block] = draw ? draw - 1 : 0;
        for (u32 index = 0; index < parameter_counts[block]; index += 1)
        {
            ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
        }
    }
    u32 next_parameter_value = first_parameter_value;
    for (u32 block = 0; block < block_count; block += 1)
    {
        for (u32 index = 0; index < parameter_counts[block]; index += 1)
        {
            IrValueId value = {.value = next_parameter_value++};
            IrBlockParameter* parameter = ir_validate_test_parameter(builder, block, value);
            for (IrPredecessor* predecessor = function->blocks[block].first_predecessor; predecessor; predecessor = predecessor->next)
            {
                IrValueId incoming = IR_VALUE_ID_INVALID;
                while (incoming.value == IR_ID_UNDERLYING_INVALID)
                {
                    u32 candidate = (u32)(ir_validate_test_random(&state) % function->value_count);
                    if (function->values[candidate].canonical_type.value == builder->i32_type.value)
                    {
                        incoming.value = candidate;
                    }
                }
                ir_validate_test_incoming(builder, parameter, predecessor->block.value, incoming);
            }
        }
    }
}

// The structural family from its definitions, independent of both validators:
// every row in exactly one chain (each chain walked under a step bound), every
// chain ending at its recorded tail in the block's one terminator, every block
// terminated and sealed, every value defined by exactly one row result or one
// parameter -- or by nothing while naming a row, the accepted orphan -- and
// every parameter carrying one incoming per distinct terminator source in
// ascending source order with the parameter's own type. Builder form only.
BUSTER_GLOBAL_LOCAL bool ir_validate_test_oracle(Arena* arena, IrFunction* function)
{
    bool valid = function->entry.value < function->block_count && !function->published_cfg;
    u32 instruction_count = function->instruction_count;
    u32* owners = arena_allocate(arena, u32, instruction_count);
    u32* definitions = arena_allocate(arena, u32, function->value_count);
    memset(owners, 0, sizeof(*owners) * instruction_count);
    memset(definitions, 0, sizeof(*definitions) * function->value_count);
    for (u32 block_index = 0; block_index < function->block_count && valid; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        valid = block->terminated && block->sealed;
        u32 steps = 0;
        u32 id = block->first_instruction.value;
        u32 tail = IR_ID_UNDERLYING_INVALID;
        while (valid && id != IR_ID_UNDERLYING_INVALID && steps <= instruction_count)
        {
            valid = id < instruction_count;
            if (valid)
            {
                owners[id] += 1;
                valid = tail == IR_ID_UNDERLYING_INVALID || !ir_instruction_is_terminator(function->instructions + tail);
                tail = id;
                steps += 1;
                id = function->instructions[id].next.value;
            }
        }
        valid = valid && steps <= instruction_count && tail != IR_ID_UNDERLYING_INVALID && tail == block->last_instruction.value &&
                ir_instruction_is_terminator(function->instructions + tail);
        u32 parameter_count = 0;
        for (IrBlockParameter* parameter = block->first_parameter; parameter && valid; parameter = parameter->next)
        {
            parameter_count += 1;
            valid = parameter->value.value < function->value_count &&
                    function->values[parameter->value.value].canonical_type.value == parameter->canonical_type.value &&
                    function->values[parameter->value.value].definition.value == IR_ID_UNDERLYING_INVALID &&
                    function->values[parameter->value.value].category == IR_VALUE_VALUE;
            if (valid)
            {
                definitions[parameter->value.value] += 1;
                IrIncoming* incoming = parameter->first_incoming;
                IrIncoming* last = 0;
                u32 incoming_count = 0;
                u32 previous_source = 0;
                for (u32 source = 0; source < function->block_count && valid; source += 1)
                {
                    IrBlock* origin = function->blocks + source;
                    bool edge = false;
                    IrInstruction* terminator = origin->last_instruction.value < instruction_count ? function->instructions + origin->last_instruction.value : 0;
                    for (u32 index = 0; terminator && index < terminator->target_count; index += 1)
                    {
                        edge |= terminator->targets[index].value == block_index;
                    }
                    if (edge)
                    {
                        valid = incoming && incoming->predecessor.value == source && (!incoming_count || previous_source < source) &&
                                incoming->value.value < function->value_count &&
                                function->values[incoming->value.value].canonical_type.value == parameter->canonical_type.value;
                        if (valid)
                        {
                            previous_source = source;
                            incoming_count += 1;
                            last = incoming;
                            incoming = incoming->next;
                        }
                    }
                }
                valid = valid && !incoming && last == parameter->last_incoming && incoming_count == parameter->incoming_count &&
                        incoming_count == block->predecessor_count;
            }
        }
        valid = valid && parameter_count == block->parameter_count;
    }
    for (u32 index = 0; index < instruction_count && valid; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        valid = owners[index] == 1 && row->opcode < IR_OPCODE_COUNT;
        for (u32 operand = 0; operand < row->operand_count && valid; operand += 1)
        {
            valid = row->operands && row->operands[operand].value < function->value_count;
        }
        for (u32 target = 0; target < row->target_count && valid; target += 1)
        {
            valid = row->targets && row->targets[target].value < function->block_count;
        }
        if (valid && row->result.value != IR_ID_UNDERLYING_INVALID)
        {
            valid = row->result.value < function->value_count && function->values[row->result.value].definition.value == index;
            if (valid)
            {
                definitions[row->result.value] += 1;
            }
        }
    }
    for (u32 value = 0; value < function->value_count && valid; value += 1)
    {
        u32 definition = function->values[value].definition.value;
        valid = definitions[value] == 1 || (definitions[value] == 0 && definition < instruction_count);
    }
    return valid;
}

// The row positions a mutation can act on, found by walking the function in
// either form. Each is the first of its kind in block order, or INVALID.
typedef struct IrValidateTestSites IrValidateTestSites;
struct IrValidateTestSites
{
    u32 entry_result_row;
    u32 operand_row;
    u32 target_row;
    u32 constant_row;
    u32 binary_row;
    u32 branch_if_row;
    u32 switch_row;
    u32 return_row;
    u32 load_row;
    u32 long_block;
    u32 parameter_block;
    u32 two_parameter_block;
    u32 earlier_block;
    u32 later_block;
    IrBlockParameter* parameter;
    IrBlockParameter* wide_parameter;
    IrBlockParameter* last_value_parameter;
    u32 bool_value;
    u32 i32_result;
};

BUSTER_GLOBAL_LOCAL IrValidateTestSites ir_validate_test_sites(IrValidateTestBuilder* builder)
{
    IrFunction* function = builder->function;
    IrValidateTestSites sites;
    memset(&sites, 0xff, sizeof(sites));
    sites.parameter = 0;
    sites.wide_parameter = 0;
    sites.last_value_parameter = 0;
    u32 chained_blocks = 0;
    for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
    {
        IrBlock* block = function->blocks + block_index;
        u32 rows = 0;
        for (u32 id = block->first_instruction.value; id != IR_ID_UNDERLYING_INVALID; id = ir_block_next_instruction(function, block, (IrInstructionId){.value = id}).value)
        {
            IrInstruction* row = function->instructions + id;
            rows += 1;
            if (row->result.value != IR_ID_UNDERLYING_INVALID && block_index == function->entry.value && sites.entry_result_row == UINT32_MAX)
            {
                sites.entry_result_row = id;
            }
            // A LOAD-family row's first operand is also what its result's
            // transfer predicate reads, so an out-of-range operand there is a
            // value fault (OPERATION) before it is a row fault (INVALID_ID).
            bool transfer_sensitive = row->opcode == IR_OPCODE_LOAD || row->opcode == IR_OPCODE_ATOMIC_LOAD || row->opcode == IR_OPCODE_CAST ||
                                      row->opcode == IR_OPCODE_FIELD || row->opcode == IR_OPCODE_INDEX || row->opcode == IR_OPCODE_DEREFERENCE;
            if (row->operand_count && !transfer_sensitive && sites.operand_row == UINT32_MAX) sites.operand_row = id;
            if (row->target_count && sites.target_row == UINT32_MAX) sites.target_row = id;
            if (row->opcode == IR_OPCODE_CONSTANT_INTEGER && sites.constant_row == UINT32_MAX) sites.constant_row = id;
            if (row->opcode == IR_OPCODE_BINARY && sites.binary_row == UINT32_MAX) sites.binary_row = id;
            if (row->opcode == IR_OPCODE_BRANCH_IF && sites.branch_if_row == UINT32_MAX) sites.branch_if_row = id;
            if (row->opcode == IR_OPCODE_SWITCH && sites.switch_row == UINT32_MAX) sites.switch_row = id;
            if (row->opcode == IR_OPCODE_RETURN && sites.return_row == UINT32_MAX) sites.return_row = id;
            if (row->opcode == IR_OPCODE_LOAD && sites.load_row == UINT32_MAX) sites.load_row = id;
            if (row->result.value != IR_ID_UNDERLYING_INVALID && function->values[row->result.value].canonical_type.value == builder->i32_type.value &&
                function->values[row->result.value].category == IR_VALUE_VALUE && sites.i32_result == UINT32_MAX)
            {
                sites.i32_result = row->result.value;
            }
        }
        if (rows >= 2 && sites.long_block == UINT32_MAX) sites.long_block = block_index;
        if (rows)
        {
            if (chained_blocks == 0) sites.earlier_block = block_index;
            if (chained_blocks == 1) sites.later_block = block_index;
            chained_blocks += 1;
        }
        if (block->first_parameter && sites.parameter_block == UINT32_MAX)
        {
            sites.parameter_block = block_index;
            sites.parameter = block->first_parameter;
        }
        if (block->parameter_count >= 2 && sites.two_parameter_block == UINT32_MAX) sites.two_parameter_block = block_index;
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            if (parameter->incoming_count >= 2 && !sites.wide_parameter) sites.wide_parameter = parameter;
            if (parameter->value.value + 1 == function->value_count) sites.last_value_parameter = parameter;
        }
    }
    for (u32 value = 0; value < function->value_count && sites.bool_value == UINT32_MAX; value += 1)
    {
        if (function->values[value].canonical_type.value == builder->bool_type.value) sites.bool_value = value;
    }
    // Retiring the last value is only a single fault when nothing names it.
    if (sites.last_value_parameter)
    {
        u32 last = function->value_count - 1;
        bool referenced = false;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            for (u32 operand = 0; operand < row->operand_count; operand += 1) referenced |= row->operands[operand].value == last;
        }
        for (u32 block_index = 0; block_index < function->block_count; block_index += 1)
        {
            for (IrBlockParameter* parameter = function->blocks[block_index].first_parameter; parameter; parameter = parameter->next)
            {
                for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next) referenced |= incoming->value.value == last;
            }
        }
        if (referenced) sites.last_value_parameter = 0;
    }
    return sites;
}

// Applies one mutation to the builder's function. False when the function has
// no site for it. `published` restricts the set to mutations that touch
// neither chains, parameter lists nor terminator targets, which the published
// CFG check would refuse first under its own error.
BUSTER_GLOBAL_LOCAL bool ir_validate_test_mutate(IrValidateTestBuilder* builder, IrValidateTestMutation mutation, bool published)
{
    IrFunction* function = builder->function;
    IrValidateTestSites sites = ir_validate_test_sites(builder);
    IrInstruction* rows = function->instructions;
    bool applied = true;
    bool chain_mutation = mutation == IR_VALIDATE_TEST_CHAIN_CYCLE || mutation == IR_VALIDATE_TEST_WRONG_TAIL ||
                          mutation == IR_VALIDATE_TEST_AFTER_TERMINATOR || mutation == IR_VALIDATE_TEST_UNOWNED_ROW ||
                          mutation == IR_VALIDATE_TEST_SHARED_TAIL_EARLIER || mutation == IR_VALIDATE_TEST_SHARED_TAIL_LATER ||
                          mutation == IR_VALIDATE_TEST_UNTERMINATED_FLAG || mutation == IR_VALIDATE_TEST_UNSEALED ||
                          (mutation >= IR_VALIDATE_TEST_PARAMETER_RANGE && mutation <= IR_VALIDATE_TEST_INCOMING_TAIL) ||
                          mutation == IR_VALIDATE_TEST_TARGET_RANGE;
    if (published && chain_mutation)
    {
        applied = false;
    }
    else
    {
        switch (mutation)
        {
        case IR_VALIDATE_TEST_NONE: break;
        case IR_VALIDATE_TEST_OPERAND_RANGE:
            applied = sites.operand_row != UINT32_MAX;
            if (applied) rows[sites.operand_row].operands[0].value = function->value_count;
            break;
        case IR_VALIDATE_TEST_TARGET_RANGE:
            applied = sites.target_row != UINT32_MAX;
            if (applied) rows[sites.target_row].targets[0].value = function->block_count;
            break;
        case IR_VALIDATE_TEST_RESULT_REBOUND:
            applied = sites.entry_result_row != UINT32_MAX && function->instruction_count >= 2;
            if (applied)
            {
                u32 other = sites.entry_result_row + 1 < function->instruction_count ? sites.entry_result_row + 1 : sites.entry_result_row - 1;
                function->values[rows[sites.entry_result_row].result.value].definition.value = other;
            }
            break;
        case IR_VALIDATE_TEST_RESULT_TYPE:
        case IR_VALIDATE_TEST_VALUE_TYPE_RANGE:
        case IR_VALIDATE_TEST_VALUE_UNRESOLVED:
            // The entry chain's first result: nothing checked before it can
            // read the value's type (an entry parameter's incoming could).
            applied = sites.entry_result_row != UINT32_MAX && !function->blocks[function->entry.value].first_parameter &&
                      !function->blocks[function->entry.value].parameter_count;
            if (applied && mutation == IR_VALIDATE_TEST_RESULT_TYPE)
            {
                function->values[rows[sites.entry_result_row].result.value].canonical_type = builder->bool_type;
            }
            else if (applied)
            {
                IrTypeId type = mutation == IR_VALIDATE_TEST_VALUE_UNRESOLVED ? builder->unresolved_type : (IrTypeId){.value = builder->program.types.count};
                rows[sites.entry_result_row].canonical_type = type;
                function->values[rows[sites.entry_result_row].result.value].canonical_type = type;
            }
            break;
        case IR_VALIDATE_TEST_VALUE_ALIGNMENT:
            applied = sites.entry_result_row != UINT32_MAX;
            if (applied) function->values[rows[sites.entry_result_row].result.value].alignment = 3;
            break;
        case IR_VALIDATE_TEST_CHAIN_CYCLE:
            applied = sites.earlier_block != UINT32_MAX;
            if (applied)
            {
                IrBlock* block = function->blocks + sites.earlier_block;
                rows[block->last_instruction.value].next = block->first_instruction;
            }
            break;
        case IR_VALIDATE_TEST_WRONG_TAIL:
            applied = sites.long_block != UINT32_MAX;
            if (applied)
            {
                IrBlock* block = function->blocks + sites.long_block;
                block->last_instruction = block->first_instruction;
            }
            break;
        case IR_VALIDATE_TEST_AFTER_TERMINATOR:
        case IR_VALIDATE_TEST_UNOWNED_ROW:
            applied = sites.earlier_block != UINT32_MAX;
            if (applied)
            {
                IrValueId value = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
                u64* immediates = arena_allocate(builder->arena, u64, 1);
                immediates[0] = 9;
                IrInstructionId added = ir_function_add_instruction(builder->arena, function, (IrInstruction){.opcode = IR_OPCODE_CONSTANT_INTEGER,
                    .canonical_type = builder->i32_type, .result = value, .immediates = immediates, .immediate_count = 1, .next = IR_INSTRUCTION_ID_INVALID},
                    (IrSourceRange){0});
                function->values[value.value].definition = added;
                if (mutation == IR_VALIDATE_TEST_AFTER_TERMINATOR)
                {
                    IrBlock* block = function->blocks + sites.earlier_block;
                    function->instructions[block->last_instruction.value].next = added;
                    block->last_instruction = added;
                }
            }
            break;
        case IR_VALIDATE_TEST_SHARED_TAIL_EARLIER:
        case IR_VALIDATE_TEST_SHARED_TAIL_LATER:
            applied = sites.earlier_block != UINT32_MAX && sites.later_block != UINT32_MAX;
            if (applied)
            {
                IrBlock* source = function->blocks + (mutation == IR_VALIDATE_TEST_SHARED_TAIL_EARLIER ? sites.later_block : sites.earlier_block);
                IrBlock* shared = function->blocks + (mutation == IR_VALIDATE_TEST_SHARED_TAIL_EARLIER ? sites.earlier_block : sites.later_block);
                rows[source->last_instruction.value].next = shared->first_instruction;
            }
            break;
        case IR_VALIDATE_TEST_UNTERMINATED_FLAG: function->blocks[0].terminated = false; break;
        case IR_VALIDATE_TEST_UNSEALED: function->blocks[0].sealed = false; break;
        case IR_VALIDATE_TEST_PARAMETER_RANGE:
            applied = sites.parameter != 0;
            if (applied) sites.parameter->value.value = function->value_count;
            break;
        case IR_VALIDATE_TEST_PARAMETER_DUPLICATE:
            applied = sites.two_parameter_block != UINT32_MAX;
            if (applied)
            {
                IrBlockParameter* first = function->blocks[sites.two_parameter_block].first_parameter;
                first->next->value = first->value;
            }
            break;
        case IR_VALIDATE_TEST_PARAMETER_IS_RESULT:
            applied = sites.last_value_parameter != 0 && sites.i32_result != UINT32_MAX;
            if (applied)
            {
                sites.last_value_parameter->value.value = sites.i32_result;
                function->value_count -= 1;
            }
            break;
        case IR_VALIDATE_TEST_PARAMETER_TYPE:
            applied = sites.parameter != 0;
            if (applied) sites.parameter->canonical_type = builder->i64_type;
            break;
        case IR_VALIDATE_TEST_INCOMING_SHORT:
            applied = sites.parameter != 0 && sites.parameter->incoming_count != 0;
            if (applied) sites.parameter->incoming_count -= 1;
            break;
        case IR_VALIDATE_TEST_INCOMING_ORDER:
            applied = sites.wide_parameter != 0;
            if (applied)
            {
                IrIncoming* first = sites.wide_parameter->first_incoming;
                IrBlockId swapped = first->predecessor;
                first->predecessor = first->next->predecessor;
                first->next->predecessor = swapped;
            }
            break;
        case IR_VALIDATE_TEST_INCOMING_TYPE:
            applied = sites.parameter != 0 && sites.parameter->first_incoming && sites.bool_value != UINT32_MAX;
            if (applied) sites.parameter->first_incoming->value.value = sites.bool_value;
            break;
        case IR_VALIDATE_TEST_INCOMING_RANGE:
            applied = sites.parameter != 0 && sites.parameter->first_incoming;
            if (applied) sites.parameter->first_incoming->value.value = function->value_count;
            break;
        case IR_VALIDATE_TEST_INCOMING_TAIL:
            applied = sites.wide_parameter != 0;
            if (applied) sites.wide_parameter->last_incoming = sites.wide_parameter->first_incoming;
            break;
        case IR_VALIDATE_TEST_ORPHAN_UNDEFINED: ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE); break;
        case IR_VALIDATE_TEST_ORPHAN_RANGE:
        case IR_VALIDATE_TEST_ORPHAN_ACCEPTED:
        {
            IrValueId value = ir_validate_test_value(builder, builder->i32_type, IR_VALUE_VALUE);
            function->values[value.value].definition.value = mutation == IR_VALIDATE_TEST_ORPHAN_RANGE ? function->instruction_count + 3 : 0;
            applied = mutation == IR_VALIDATE_TEST_ORPHAN_RANGE || function->instructions[0].result.value != value.value;
        }
        break;
        case IR_VALIDATE_TEST_OPCODE_RANGE:
            applied = sites.constant_row != UINT32_MAX;
            if (applied) rows[sites.constant_row].opcode = IR_OPCODE_COUNT;
            break;
        case IR_VALIDATE_TEST_STORAGE_NULL:
            applied = sites.binary_row != UINT32_MAX;
            if (applied) rows[sites.binary_row].operands = 0;
            break;
        case IR_VALIDATE_TEST_LOAD_NO_OPERAND:
            applied = sites.load_row != UINT32_MAX;
            if (applied) rows[sites.load_row].operand_count = 0;
            break;
        case IR_VALIDATE_TEST_RETURN_TYPE:
            applied = sites.return_row != UINT32_MAX && sites.bool_value != UINT32_MAX;
            if (applied) rows[sites.return_row].operands[0].value = sites.bool_value;
            break;
        case IR_VALIDATE_TEST_CONDITION_TYPE:
            applied = sites.branch_if_row != UINT32_MAX && sites.i32_result != UINT32_MAX;
            if (applied) rows[sites.branch_if_row].operands[0].value = sites.i32_result;
            break;
        case IR_VALIDATE_TEST_SWITCH_COUNT:
            applied = sites.switch_row != UINT32_MAX;
            if (applied) rows[sites.switch_row].immediate_count += 1;
            break;
        case IR_VALIDATE_TEST_BINARY_MISMATCH:
            applied = sites.binary_row != UINT32_MAX && sites.bool_value != UINT32_MAX;
            if (applied) rows[sites.binary_row].operands[1].value = sites.bool_value;
            break;
        case IR_VALIDATE_TEST_ENTRY_RANGE: function->entry.value = function->block_count; break;
        case IR_VALIDATE_TEST_SIGNATURE: function->canonical_type = builder->i32_type; break;
        case IR_VALIDATE_TEST_MUTATION_COUNT:
        default: BUSTER_TODO();
        }
    }
    return applied;
}

// Both validators and the structural oracle on the builder's function, for
// one mutation. The two verdicts must agree on the error; every mutation but
// the documented divergence must also agree on the site. The oracle must
// reject whenever it sees a structural fault, and must accept whatever the
// validators accept.
BUSTER_GLOBAL_LOCAL UnitTestResult ir_validate_test_agree(UnitTestArguments* arguments, IrValidateTestBuilder* builder, IrValidateTestMutation mutation,
                                                          String8 label)
{
    UnitTestResult result = {0};
    IrValidationError expected = ir_validate_test_expected(mutation);
    IrValidationResult fused = ir_validate_canonical_module(&builder->program, builder->program.modules);
    IrValidationResult reference = ir_test_validate_canonical_module_reference(&builder->program, builder->program.modules);
    bool structural = true;
    if (!builder->function->published_cfg)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        structural = ir_validate_test_oracle(temporary.arena, builder->function);
        scratch_end(temporary);
    }
    if (mutation == IR_VALIDATE_TEST_SHARED_TAIL_LATER)
    {
        BUSTER_TEST_RAW(arguments, fused.error == IR_VALIDATION_INSTRUCTION_AFTER_TERMINATOR, label);
        BUSTER_TEST_RAW(arguments, reference.error == IR_VALIDATION_INSTRUCTION_OWNERSHIP, label);
        BUSTER_TEST_RAW(arguments, fused.block.value == reference.block.value, label);
    }
    else
    {
        BUSTER_TEST_RAW(arguments, fused.error == expected, label);
        BUSTER_TEST_RAW(arguments, reference.error == expected, label);
        BUSTER_TEST_RAW(arguments, fused.block.value == reference.block.value, label);
        BUSTER_TEST_RAW(arguments, fused.instruction.value == reference.instruction.value, label);
    }
    BUSTER_TEST_RAW(arguments, fused.function.value == reference.function.value, label);
    BUSTER_TEST_RAW(arguments, structural || (fused.error != IR_VALIDATION_NONE && reference.error != IR_VALIDATION_NONE), label);
    BUSTER_TEST_RAW(arguments, fused.error != IR_VALIDATION_NONE || structural, label);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_validate_equivalence_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 applied_counts[IR_VALIDATE_TEST_MUTATION_COUNT] = {0};
    for (u32 shape = 0; shape < IR_VALIDATE_TEST_SHAPE_COUNT; shape += 1)
    {
        for (u32 published = 0; published < 2; published += 1)
        {
            for (u32 mutation = 0; mutation < IR_VALIDATE_TEST_MUTATION_COUNT; mutation += 1)
            {
                TemporalArena temporary = arena_begin_temporal(arguments->arena);
                IrValidateTestBuilder builder = ir_validate_test_builder(temporary.arena);
                ir_validate_test_shape(&builder, (IrValidateTestShape)shape);
                bool ready = true;
                if (published)
                {
                    IrValidationResult publication = ir_function_publish_cfg(temporary.arena, builder.function);
                    ready = publication.error == IR_VALIDATION_NONE && builder.function->published_cfg != 0;
                    BUSTER_TEST(arguments, ready);
                }
                if (ready && ir_validate_test_mutate(&builder, (IrValidateTestMutation)mutation, published != 0))
                {
                    applied_counts[mutation] += 1;
                    String8 label = string_format(temporary.arena, S8("shape {u32} published {u32} mutation {u32}"), shape, published, mutation);
                    UnitTestResult agreement = ir_validate_test_agree(arguments, &builder, (IrValidateTestMutation)mutation, label);
                    result.test_count += agreement.test_count;
                    result.succeeded_test_count += agreement.succeeded_test_count;
                }
                scratch_end(temporary);
            }
        }
    }
    for (u32 mutation = 0; mutation < IR_VALIDATE_TEST_MUTATION_COUNT; mutation += 1)
    {
        BUSTER_TEST_RAW(arguments, applied_counts[mutation] != 0, string_format(arguments->arena, S8("mutation {u32} never applied"), mutation));
    }
    // Random graphs: the unmutated function must pass every checker; each
    // applicable mutation must produce the same verdict from both traversals.
    enum
    {
        IR_VALIDATE_TEST_RANDOM_ROUNDS = 400,
    };
    u32 random_applied = 0;
    for (u32 round = 0; round < IR_VALIDATE_TEST_RANDOM_ROUNDS; round += 1)
    {
        u64 seed = UINT64_C(0x9e3779b97f4a7c15) * (round + 1);
        for (u32 mutation = 0; mutation < IR_VALIDATE_TEST_MUTATION_COUNT; mutation += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            IrValidateTestBuilder builder = ir_validate_test_builder(temporary.arena);
            ir_validate_test_random_function(&builder, seed);
            if (ir_validate_test_mutate(&builder, (IrValidateTestMutation)mutation, false))
            {
                random_applied += mutation != IR_VALIDATE_TEST_NONE;
                String8 label = string_format(temporary.arena, S8("random round {u32} mutation {u32}"), round, mutation);
                UnitTestResult agreement = ir_validate_test_agree(arguments, &builder, (IrValidateTestMutation)mutation, label);
                result.test_count += agreement.test_count;
                result.succeeded_test_count += agreement.succeeded_test_count;
            }
            scratch_end(temporary);
        }
    }
    BUSTER_TEST(arguments, random_applied >= IR_VALIDATE_TEST_RANDOM_ROUNDS * 8);
    return result;
}

BUSTER_GLOBAL_LOCAL IrValidateTestBuilder ir_validate_test_exception_builder(Arena* arena)
{
    IrValidateTestBuilder builder = ir_validate_test_builder(arena);
    IrTypeId data_pointer_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_POINTER, .element_type = builder.i32_type,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrTypeId* callback_parameter_types = arena_allocate(arena, IrTypeId, 2);
    callback_parameter_types[0] = data_pointer_type;
    callback_parameter_types[1] = builder.bool_type;
    IrTypeId callback_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = builder.void_type,
        .parameter_types = callback_parameter_types, .parameter_count = 2, .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrTypeId callback_pointer_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_POINTER, .element_type = callback_type,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrTypeId* function_parameter_types = arena_allocate(arena, IrTypeId, 2);
    function_parameter_types[0] = callback_pointer_type;
    function_parameter_types[1] = data_pointer_type;
    IrTypeId function_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_FUNCTION, .return_type = builder.bool_type,
        .parameter_types = function_parameter_types, .parameter_count = 2, .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    builder.function->canonical_type = function_type;
    u32 normal = ir_validate_test_block(&builder);
    u32 exception = ir_validate_test_block(&builder);
    builder.function->entry = (IrBlockId){.value = normal};
    builder.function->exception_entry_plus_one = exception + 1;
    IrValueId callback = ir_validate_test_value(&builder, callback_pointer_type, IR_VALUE_VALUE);
    u64* callback_index = arena_allocate(arena, u64, 1);
    callback_index[0] = 0;
    ir_validate_test_row(&builder, normal, (IrInstruction){.opcode = IR_OPCODE_ARGUMENT, .canonical_type = callback_pointer_type,
        .result = callback, .immediates = callback_index, .immediate_count = 1});
    IrValueId captured_pointer = ir_validate_test_value(&builder, data_pointer_type, IR_VALUE_VALUE);
    u64* captured_index = arena_allocate(arena, u64, 1);
    captured_index[0] = 1;
    ir_validate_test_row(&builder, normal, (IrInstruction){.opcode = IR_OPCODE_ARGUMENT, .canonical_type = data_pointer_type,
        .result = captured_pointer, .immediates = captured_index, .immediate_count = 1});
    IrValueId normal_value = ir_validate_test_constant(&builder, normal, builder.bool_type, 0);
    IrValueId* call_operands = arena_allocate(arena, IrValueId, 3);
    call_operands[0] = callback;
    call_operands[1] = captured_pointer;
    call_operands[2] = normal_value;
    ir_validate_test_row(&builder, normal, (IrInstruction){.opcode = IR_OPCODE_CALL, .canonical_type = builder.void_type,
        .result = IR_VALUE_ID_INVALID, .symbol = IR_SYMBOL_ID_INVALID, .operands = call_operands, .operand_count = 3});
    ir_validate_test_return(&builder, normal, normal_value);
    IrValueId exception_value = ir_validate_test_constant(&builder, exception, builder.bool_type, 1);
    ir_validate_test_return(&builder, exception, exception_value);
    ir_validate_test_seal(&builder);
    return builder;
}

BUSTER_GLOBAL_LOCAL IrValidateTestBuilder ir_validate_test_exception_returns_twice_builder(Arena* arena, bool attribute)
{
    IrValidateTestBuilder builder = ir_validate_test_builder(arena);
    IrTypeId exception_function_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_FUNCTION,
        .return_type = builder.bool_type, .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrTypeId returns_twice_type = ir_program_add_type(&builder.program, (IrType){.kind = IR_TYPE_FUNCTION,
        .return_type = builder.void_type, .calling_convention = IR_CALLING_CONVENTION_C,
        .layout = {.size = 8, .alignment = 8, .abi_class = IR_ABI_CLASS_POINTER, .resolved = true}});
    IrSymbolId returns_twice_symbol = ir_program_add_symbol(&builder.program, (IrSymbol){
        .name = attribute ? S8("custom_returns_twice") : S8("setjmp"),
        .type = returns_twice_type,
        .kind = IR_SYMBOL_FUNCTION,
        .linkage = IR_LINKAGE_EXTERNAL,
        .is_returns_twice = attribute,
    });
    builder.function->canonical_type = exception_function_type;
    u32 normal = ir_validate_test_block(&builder);
    u32 exception = ir_validate_test_block(&builder);
    builder.function->entry = (IrBlockId){.value = normal};
    builder.function->exception_entry_plus_one = exception + 1;
    IrValueId callee = ir_validate_test_value(&builder, returns_twice_type, IR_VALUE_VALUE);
    ir_validate_test_row(&builder, normal, (IrInstruction){.opcode = IR_OPCODE_FUNCTION, .canonical_type = returns_twice_type,
        .result = callee, .symbol = returns_twice_symbol});
    IrValueId normal_value = ir_validate_test_constant(&builder, normal, builder.bool_type, 0);
    IrValueId* call_operands = arena_allocate(arena, IrValueId, 1);
    call_operands[0] = callee;
    ir_validate_test_row(&builder, normal, (IrInstruction){.opcode = IR_OPCODE_CALL, .canonical_type = builder.void_type,
        .result = IR_VALUE_ID_INVALID, .symbol = returns_twice_symbol, .operands = call_operands, .operand_count = 1});
    ir_validate_test_return(&builder, normal, normal_value);
    IrValueId exception_value = ir_validate_test_constant(&builder, exception, builder.bool_type, 1);
    ir_validate_test_return(&builder, exception, exception_value);
    ir_validate_test_seal(&builder);
    return builder;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_validate_exception_root_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    IrValidateTestBuilder ordinary = ir_validate_test_builder(temporary.arena);
    ir_validate_test_shape(&ordinary, IR_VALIDATE_TEST_SHAPE_LINEAR);
    BUSTER_TEST_RAW(arguments, !ir_module_has_exception_root(ordinary.program.modules),
                    "ordinary canonical modules do not claim an exception root");
    scratch_end(temporary);
    temporary = arena_begin_temporal(arguments->arena);
    IrValidateTestBuilder valid = ir_validate_test_exception_builder(temporary.arena);
    IrValidationResult fused = ir_validate_canonical_module(&valid.program, valid.program.modules);
    IrValidationResult reference = ir_test_validate_canonical_module_reference(&valid.program, valid.program.modules);
    BUSTER_TEST_RAW(arguments, fused.error == IR_VALIDATION_NONE && reference.error == IR_VALIDATION_NONE,
                    "canonical exception root accepts captured arguments and a call before its false return");
    BUSTER_TEST_RAW(arguments, ir_module_has_exception_root(valid.program.modules), "canonical module reports an exception root");
    BUSTER_TEST_RAW(arguments, valid.function->blocks[1].predecessor_count == 0,
                    "handler is a separate root with no invented predecessor");
    IrValidationResult publication = ir_function_publish_cfg(temporary.arena, valid.function);
    BUSTER_TEST_RAW(arguments, publication.error == IR_VALIDATION_NONE && valid.function->published_cfg &&
                                valid.function->published_cfg->blocks[1].predecessor_count == 0,
                    "CFG publication preserves the root without adding an edge");
    fused = ir_validate_canonical_module(&valid.program, valid.program.modules);
    reference = ir_test_validate_canonical_module_reference(&valid.program, valid.program.modules);
    BUSTER_TEST_RAW(arguments, fused.error == IR_VALIDATION_NONE && reference.error == IR_VALIDATION_NONE,
                    "published canonical exception root remains valid");
    u32 valid_root_block = valid.function->exception_entry_plus_one - 1;
    u32 valid_normal_entry = valid.function->entry.value;
    IrValidationResult prepared = ir_prepare_canonical_module(&valid.program, valid.program.modules, false);
    fused = ir_validate_canonical_module(&valid.program, valid.program.modules);
    reference = ir_test_validate_canonical_module_reference(&valid.program, valid.program.modules);
    BUSTER_TEST_RAW(arguments, prepared.error == IR_VALIDATION_NONE && fused.error == IR_VALIDATION_NONE &&
                                reference.error == IR_VALIDATION_NONE &&
                                valid.function->exception_entry_plus_one == valid_root_block + 1 &&
                                valid.function->entry.value == valid_normal_entry &&
                                valid.function->blocks[valid_root_block].predecessor_count == 0,
                    "publish, validate, prepare and revalidate preserve the second root");
    scratch_end(temporary);

    for (u32 returns_twice_case = 0; returns_twice_case < 2; returns_twice_case += 1)
    {
        temporary = arena_begin_temporal(arguments->arena);
        IrValidateTestBuilder returns_twice =
            ir_validate_test_exception_returns_twice_builder(temporary.arena, returns_twice_case != 0);
        u32 root_block = returns_twice.function->exception_entry_plus_one - 1;
        fused = ir_validate_canonical_module(&returns_twice.program, returns_twice.program.modules);
        reference = ir_test_validate_canonical_module_reference(&returns_twice.program, returns_twice.program.modules);
        BUSTER_TEST_RAW(arguments, fused.error == IR_VALIDATION_EXCEPTION_ROOT && reference.error == IR_VALIDATION_EXCEPTION_ROOT,
                        string_format(temporary.arena, S8("returns-twice root call {u32} is refused"), returns_twice_case));
        BUSTER_TEST_RAW(arguments, fused.function.value == returns_twice.function->id.value &&
                                    fused.block.value == root_block &&
                                    fused.instruction.value == IR_ID_UNDERLYING_INVALID &&
                                    reference.function.value == returns_twice.function->id.value &&
                                    reference.block.value == root_block &&
                                    reference.instruction.value == IR_ID_UNDERLYING_INVALID,
                        "exception-root refusal preserves structured function and root-block context");
        scratch_end(temporary);
    }

    String8 promotion_barrier_sources[] = {
        S8("int setjmp(void*);int test(void* p){int value=1;setjmp(p);value=2;return value;}"),
        S8("int custom_twice(void*) __attribute__((returns_twice));int test(void* p){int value=1;custom_twice(p);value=2;return value;}"),
    };
    for (u32 barrier_case = 0; barrier_case < BUSTER_ARRAY_LENGTH(promotion_barrier_sources); barrier_case += 1)
    {
        temporary = arena_begin_temporal(arguments->arena);
        CIRLowerResult lowered = ir_promotion_lower(temporary.arena, promotion_barrier_sources[barrier_case], target_native);
        BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count);
        if (lowered.program && !lowered.diagnostic_count)
        {
            IrModule* module = lowered.program->modules;
            IrFunction* function = 0;
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                if (string_equal(module->functions[index].name, S8("test"))) function = module->functions + index;
            }
            BUSTER_TEST(arguments, function != 0);
            if (function)
            {
                u32 instruction_count = function->instruction_count;
                u32 value_count = function->value_count;
                u32 local_count = ir_test_opcode_count(function, IR_OPCODE_LOCAL);
                u32 store_count = ir_test_opcode_count(function, IR_OPCODE_STORE);
                u32 load_count = ir_test_opcode_count(function, IR_OPCODE_LOAD);
                bool returns_twice_call = false;
                for (u32 index = 0; index < instruction_count; index += 1)
                {
                    IrInstruction* row = function->instructions + index;
                    returns_twice_call |= row->opcode == IR_OPCODE_CALL && ir_call_returns_twice(lowered.program, row);
                }
                BUSTER_TEST(arguments, local_count && store_count && load_count && returns_twice_call);
                IrInstruction* instruction_snapshot = arena_allocate(temporary.arena, IrInstruction, instruction_count);
                IrValue* value_snapshot = arena_allocate(temporary.arena, IrValue, value_count);
                memcpy(instruction_snapshot, function->instructions, sizeof(*instruction_snapshot) * instruction_count);
                memcpy(value_snapshot, function->values, sizeof(*value_snapshot) * value_count);
                lowered.program->fast_passes = 0;
                IrValidationResult promotion = ir_prepare_canonical_module(lowered.program, module, false);
                BUSTER_TEST(arguments, promotion.error == IR_VALIDATION_NONE && module->local_promotion_complete);
                BUSTER_TEST(arguments, module->local_promotion.promoted_locals == 0 && module->local_promotion.barrier_functions == 1);
                BUSTER_TEST(arguments, function->instruction_count == instruction_count && function->value_count == value_count &&
                                            ir_test_opcode_count(function, IR_OPCODE_LOCAL) == local_count &&
                                            ir_test_opcode_count(function, IR_OPCODE_STORE) == store_count &&
                                            ir_test_opcode_count(function, IR_OPCODE_LOAD) == load_count);
                bool instructions_unchanged = function->instruction_count == instruction_count;
                for (u32 index = 0; index < instruction_count && instructions_unchanged; index += 1)
                {
                    IrInstruction before_row = instruction_snapshot[index];
                    IrInstruction after_row = function->instructions[index];
                    bool payload_unchanged = before_row.operand_count == after_row.operand_count &&
                                             before_row.target_count == after_row.target_count &&
                                             before_row.immediate_count == after_row.immediate_count;
                    for (u32 operand = 0; operand < before_row.operand_count && payload_unchanged; operand += 1)
                    {
                        payload_unchanged = before_row.operands[operand].value == after_row.operands[operand].value;
                    }
                    for (u32 target = 0; target < before_row.target_count && payload_unchanged; target += 1)
                    {
                        payload_unchanged = before_row.targets[target].value == after_row.targets[target].value;
                    }
                    for (u32 immediate = 0; immediate < before_row.immediate_count && payload_unchanged; immediate += 1)
                    {
                        payload_unchanged = before_row.immediates[immediate] == after_row.immediates[immediate];
                    }
                    before_row.next = after_row.next;
                    before_row.operands = after_row.operands;
                    before_row.targets = after_row.targets;
                    before_row.immediates = after_row.immediates;
                    instructions_unchanged = payload_unchanged && memory_compare(&before_row, &after_row, sizeof(before_row));
                }
                BUSTER_TEST(arguments, instructions_unchanged && memory_compare(value_snapshot, function->values,
                                                                                 sizeof(*value_snapshot) * value_count));
            }
        }
        scratch_end(temporary);
    }

    for (u32 mutation = 0; mutation < 5; mutation += 1)
    {
        temporary = arena_begin_temporal(arguments->arena);
        IrValidateTestBuilder builder = ir_validate_test_exception_builder(temporary.arena);
        IrFunction* function = builder.function;
        u32 root_block = function->exception_entry_plus_one - 1;
        if (mutation == 0)
        {
            function->exception_entry_plus_one = function->block_count + 1;
        }
        else if (mutation == 1)
        {
            function->exception_entry_plus_one = function->entry.value + 1;
        }
        else if (mutation == 2)
        {
            u32 return_row = function->blocks[function->entry.value].last_instruction.value;
            IrBlockId* targets = arena_allocate(temporary.arena, IrBlockId, 1);
            targets[0] = (IrBlockId){.value = root_block};
            function->instructions[return_row] = (IrInstruction){.opcode = IR_OPCODE_BRANCH, .canonical_type = builder.void_type,
                .result = IR_VALUE_ID_INVALID, .next = IR_INSTRUCTION_ID_INVALID, .targets = targets, .target_count = 1};
            function->blocks[root_block].predecessor_count = 0;
            function->blocks[root_block].first_predecessor = 0;
            function->blocks[root_block].last_predecessor = 0;
            ir_validate_test_seal(&builder);
        }
        else if (mutation == 3)
        {
            IrValueId live_in = ir_validate_test_value(&builder, builder.bool_type, IR_VALUE_VALUE);
            ir_validate_test_parameter(&builder, root_block, live_in);
        }
        else
        {
            u32 constant_row = function->blocks[root_block].first_instruction.value;
            function->instructions[constant_row].immediates[0] = 0;
        }
        fused = ir_validate_canonical_module(&builder.program, builder.program.modules);
        reference = ir_test_validate_canonical_module_reference(&builder.program, builder.program.modules);
        BUSTER_TEST_RAW(arguments, fused.error == IR_VALIDATION_EXCEPTION_ROOT && reference.error == IR_VALIDATION_EXCEPTION_ROOT,
                        string_format(temporary.arena, S8("exception-root mutation {u32} is rejected by both validators"), mutation));
        if (mutation == 2)
        {
            publication = ir_function_publish_cfg(temporary.arena, function);
            BUSTER_TEST_RAW(arguments, publication.error == IR_VALIDATION_EXCEPTION_ROOT && !function->published_cfg,
                            "CFG publication refuses a normal edge into the exception root");
        }
        scratch_end(temporary);
    }
    return result;
}
