#pragma once

// Test-only executable semantics over canonical records. ir_oracle_evaluate is
// the entry point; ir_oracle_scalar owns independent integer rules;
// ir_oracle_preflight checks dominance; ir_oracle_transfer snapshots edge
// arguments; ir_oracle_memory owns tagged, initialized byte storage. No folder,
// lowering, ABI classifier, host pointer, or host resolver participates.
enum
{
    IR_ORACLE_VALUES = 256,
    IR_ORACLE_ROWS = 1024,
    IR_ORACLE_BLOCKS = 64,
    IR_ORACLE_FRAMES = 16,
    IR_ORACLE_ARGUMENTS = 8,
    IR_ORACLE_ALLOCATIONS = 64,
    IR_ORACLE_BYTES = 4096,
    IR_ORACLE_STEPS = 4096,
};

typedef enum IrOracleStatus
{
    IR_ORACLE_OK,
    IR_ORACLE_INVALID,
    IR_ORACLE_UNSUPPORTED,
    IR_ORACLE_LIMIT,
} IrOracleStatus;

typedef enum IrOracleKind
{
    IR_ORACLE_INTEGER,
    IR_ORACLE_POINTER,
    IR_ORACLE_FUNCTION,
} IrOracleKind;

typedef struct IrOracleValue IrOracleValue;
struct IrOracleValue
{
    u64 bits;
    u32 allocation;
    IrOracleKind kind;
    bool defined;
};

typedef struct IrOracleAllocation IrOracleAllocation;
struct IrOracleAllocation
{
    u32 start;
    u32 size;
    u32 owner;
    bool alive;
    bool read_only;
};

typedef struct IrOracleFrame IrOracleFrame;
struct IrOracleFrame
{
    IrFunction* function;
    IrOracleValue values[IR_ORACLE_VALUES];
    IrOracleValue arguments[IR_ORACLE_ARGUMENTS];
    u32 block;
    u32 row;
    IrValueId return_value;
};

typedef struct IrOracleRun IrOracleRun;
struct IrOracleRun
{
    IrProgram* program;
    IrModule* module;
    IrOracleFrame frames[IR_ORACLE_FRAMES];
    IrOracleAllocation allocations[IR_ORACLE_ALLOCATIONS];
    u8 bytes[IR_ORACLE_BYTES];
    bool initialized[IR_ORACLE_BYTES];
    u32 globals[IR_ORACLE_ALLOCATIONS];
    u32 allocation_count;
    u32 byte_count;
    u32 depth;
    u32 steps;
    u32 limit;
    u32 last_row;
    u32 last_opcode;
    IrFunction* last_function;
    IrOracleStatus status;
    IrOracleValue returned;
};

BUSTER_GLOBAL_LOCAL IrType* ir_oracle_type(IrProgram* program, IrTypeId id)
{
    return id.value < program->types.count ? program->types.types + id.value : 0;
}

BUSTER_GLOBAL_LOCAL u32 ir_oracle_width(IrType* type)
{
    u32 width = 0;
    if (type && !type->is_atomic && !type->is_volatile && type->layout.resolved &&
        ((type->kind == IR_TYPE_BOOLEAN && type->bit_width == 1 && type->layout.size == 1) ||
         (type->kind == IR_TYPE_INTEGER && (type->bit_width == 8 || type->bit_width == 16 ||
          type->bit_width == 32 || type->bit_width == 64) && type->layout.size == type->bit_width / 8)))
    {
        if (type->layout.alignment == type->layout.size) width = type->bit_width;
    }
    return width;
}

BUSTER_GLOBAL_LOCAL u64 ir_oracle_mask(u32 width)
{
    return width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
}

BUSTER_GLOBAL_LOCAL IrOracleValue ir_oracle_integer(u64 bits)
{
    return (IrOracleValue){.bits = bits, .kind = IR_ORACLE_INTEGER, .defined = true};
}

BUSTER_GLOBAL_LOCAL IrFunction* ir_oracle_function(IrModule* module, u32 symbol)
{
    IrFunction* found = 0;
    for (u32 index = 0; index < module->function_count; index += 1)
    {
        if (module->functions[index].symbol.value == symbol && module->functions[index].state == IR_FUNCTION_LOWERED)
        {
            found = module->functions + index;
        }
    }
    return found;
}

// Magnitude arithmetic avoids implementation-defined signed casts/shifts and
// host INT_MIN/-1. The operation enum, not the host C type, selects signedness.
BUSTER_GLOBAL_LOCAL IrOracleValue ir_oracle_scalar(IrOracleRun* run, IrInstruction* row, u32 width, u64 a, u64 b)
{
    IrOracleValue value = ir_oracle_integer(0);
    u64 mask = ir_oracle_mask(width);
    u64 sign = UINT64_C(1) << (width - 1);
    bool an = (a & sign) != 0;
    bool bn = (b & sign) != 0;
    u64 am = an ? (0 - a) & mask : a;
    u64 bm = bn ? (0 - b) & mask : b;
    bool less = an != bn ? an : a < b;
    if (row->opcode == IR_OPCODE_UNARY)
    {
        switch (row->unary_operation)
        {
            case IR_UNARY_INTEGER_NEGATE: value.bits = 0 - a; break;
            case IR_UNARY_INTEGER_BITWISE_NOT: value.bits = ~a; break;
            case IR_UNARY_BOOLEAN_NOT: value.bits = !a; break;
            case IR_UNARY_INTEGER_POPULATION_COUNT:
            {
                for (u32 bit = 0; bit < width; bit += 1) value.bits += (a >> bit) & 1;
                break;
            }
            case IR_UNARY_INTEGER_COUNT_LEADING_ZEROS:
            case IR_UNARY_INTEGER_COUNT_TRAILING_ZEROS:
            {
                if (!a) run->status = IR_ORACLE_INVALID;
                else
                {
                    u32 bit = row->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ? width - 1 : 0;
                    while (!(a & (UINT64_C(1) << bit)))
                    {
                        value.bits += 1;
                        bit = row->unary_operation == IR_UNARY_INTEGER_COUNT_LEADING_ZEROS ? bit - 1 : bit + 1;
                    }
                }
                break;
            }
            default: run->status = IR_ORACLE_UNSUPPORTED; break;
        }
    }
    else
    {
        switch (row->binary_operation)
        {
            case IR_BINARY_INTEGER_ADD: value.bits = a + b; break;
            case IR_BINARY_INTEGER_SUBTRACT: value.bits = a - b; break;
            case IR_BINARY_INTEGER_MULTIPLY: value.bits = a * b; break;
            case IR_BINARY_INTEGER_BITWISE_AND: value.bits = a & b; break;
            case IR_BINARY_INTEGER_BITWISE_OR: value.bits = a | b; break;
            case IR_BINARY_INTEGER_BITWISE_XOR: value.bits = a ^ b; break;
            case IR_BINARY_BOOLEAN_AND: value.bits = a && b; break;
            case IR_BINARY_BOOLEAN_OR: value.bits = a || b; break;
            case IR_BINARY_INTEGER_EQUAL: case IR_BINARY_BOOLEAN_EQUAL: value.bits = a == b; break;
            case IR_BINARY_INTEGER_NOT_EQUAL: case IR_BINARY_BOOLEAN_NOT_EQUAL: value.bits = a != b; break;
            case IR_BINARY_SIGNED_LESS: value.bits = less; break;
            case IR_BINARY_SIGNED_LESS_EQUAL: value.bits = less || a == b; break;
            case IR_BINARY_SIGNED_GREATER: value.bits = !less && a != b; break;
            case IR_BINARY_SIGNED_GREATER_EQUAL: value.bits = !less; break;
            case IR_BINARY_UNSIGNED_LESS: value.bits = a < b; break;
            case IR_BINARY_UNSIGNED_LESS_EQUAL: value.bits = a <= b; break;
            case IR_BINARY_UNSIGNED_GREATER: value.bits = a > b; break;
            case IR_BINARY_UNSIGNED_GREATER_EQUAL: value.bits = a >= b; break;
            case IR_BINARY_UNSIGNED_DIVIDE: case IR_BINARY_UNSIGNED_REMAINDER:
            case IR_BINARY_SIGNED_DIVIDE: case IR_BINARY_SIGNED_REMAINDER:
            {
                bool signed_op = row->binary_operation == IR_BINARY_SIGNED_DIVIDE || row->binary_operation == IR_BINARY_SIGNED_REMAINDER;
                bool remainder = row->binary_operation == IR_BINARY_SIGNED_REMAINDER || row->binary_operation == IR_BINARY_UNSIGNED_REMAINDER;
                if (!b || (signed_op && a == sign && b == mask)) run->status = IR_ORACLE_INVALID;
                else if (signed_op)
                {
                    value.bits = remainder ? am % bm : am / bm;
                    if (remainder ? an : an != bn) value.bits = 0 - value.bits;
                }
                else value.bits = remainder ? a % b : a / b;
                break;
            }
            case IR_BINARY_SHIFT_LEFT: case IR_BINARY_UNSIGNED_SHIFT_RIGHT: case IR_BINARY_SIGNED_SHIFT_RIGHT:
            {
                if (b >= width) run->status = IR_ORACLE_INVALID;
                else if (row->binary_operation == IR_BINARY_SHIFT_LEFT) value.bits = a << b;
                else
                {
                    value.bits = a >> b;
                    if (row->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT && an && b)
                    {
                        value.bits |= mask ^ (mask >> b);
                    }
                }
                break;
            }
            default: run->status = IR_ORACLE_UNSUPPORTED; break;
        }
    }
    value.bits &= mask;
    return value;
}

BUSTER_GLOBAL_LOCAL u32 ir_oracle_allocate(IrOracleRun* run, u32 size, u32 owner, bool read_only)
{
    u32 allocation = run->allocation_count;
    if (!size || size > IR_ORACLE_BYTES - run->byte_count || allocation >= IR_ORACLE_ALLOCATIONS)
    {
        run->status = IR_ORACLE_LIMIT;
    }
    else
    {
        run->allocations[allocation] = (IrOracleAllocation){.start = run->byte_count, .size = size, .owner = owner,
                                                          .alive = true, .read_only = read_only};
        run->byte_count += size;
        run->allocation_count += 1;
    }
    return allocation;
}

BUSTER_GLOBAL_LOCAL IrOracleValue ir_oracle_memory(IrOracleRun* run, IrOracleValue pointer, u32 size, bool store, u64 bits)
{
    IrOracleValue value = ir_oracle_integer(0);
    if (pointer.kind != IR_ORACLE_POINTER || pointer.allocation >= run->allocation_count) run->status = IR_ORACLE_INVALID;
    else
    {
        IrOracleAllocation* allocation = run->allocations + pointer.allocation;
        if (!allocation->alive || pointer.bits > allocation->size || size > allocation->size - pointer.bits ||
            pointer.bits % size || (store && allocation->read_only)) run->status = IR_ORACLE_INVALID;
        else
        {
            u32 start = allocation->start + (u32)pointer.bits;
            for (u32 byte = 0; byte < size; byte += 1)
            {
                if (store)
                {
                    run->bytes[start + byte] = (u8)(bits >> (byte * 8));
                    run->initialized[start + byte] = true;
                }
                else if (!run->initialized[start + byte]) run->status = IR_ORACLE_INVALID;
                else value.bits |= (u64)run->bytes[start + byte] << (byte * 8);
            }
        }
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool ir_oracle_dominates(IrFunction* function, u32 value, u32 block, u32 position,
                                            u32* value_blocks, u32* positions, u64* dominators)
{
    bool valid = value < function->value_count && value_blocks[value] != UINT32_MAX &&
                 (dominators[block] & (UINT64_C(1) << value_blocks[value])) != 0;
    if (valid)
    {
        u32 definition = function->values[value].definition.value;
        valid = definition == UINT32_MAX || value_blocks[value] != block || positions[definition] < position;
    }
    return valid;
}

// The production validator deliberately does not establish dominance. This
// independent <=64-block check prevents stale values surviving loop iterations.
BUSTER_GLOBAL_LOCAL void ir_oracle_preflight(IrOracleRun* run, IrFunction* function)
{
    if (!function->block_count || function->block_count > IR_ORACLE_BLOCKS || function->instruction_count > IR_ORACLE_ROWS ||
        function->value_count > IR_ORACLE_VALUES || function->entry.value >= function->block_count)
    {
        run->status = IR_ORACLE_LIMIT;
    }
    else
    {
        u32 owners[IR_ORACLE_ROWS];
        u32 positions[IR_ORACLE_ROWS];
        u32 value_blocks[IR_ORACLE_VALUES];
        u64 predecessors[IR_ORACLE_BLOCKS] = {0};
        u64 dominators[IR_ORACLE_BLOCKS];
        u64 reachable = UINT64_C(1) << function->entry.value;
        for (u32 i = 0; i < IR_ORACLE_ROWS; i += 1) owners[i] = UINT32_MAX;
        for (u32 i = 0; i < IR_ORACLE_VALUES; i += 1) value_blocks[i] = UINT32_MAX;
        for (u32 block = 0; block < function->block_count && run->status == IR_ORACLE_OK; block += 1)
        {
            u32 position = 0;
            for (IrInstructionId id = function->blocks[block].first_instruction; id.value != IR_ID_UNDERLYING_INVALID && run->status == IR_ORACLE_OK;
                 id = ir_block_next_instruction(function, function->blocks + block, id))
            {
                if (id.value >= function->instruction_count || owners[id.value] != UINT32_MAX) run->status = IR_ORACLE_INVALID;
                else
                {
                    owners[id.value] = block;
                    positions[id.value] = position++;
                    IrInstruction* row = function->instructions + id.value;
                    run->last_function = function;
                    run->last_row = id.value;
                    run->last_opcode = row->opcode;
                    switch (row->opcode)
                    {
                        case IR_OPCODE_ARGUMENT: case IR_OPCODE_LOCAL: case IR_OPCODE_GLOBAL:
                        case IR_OPCODE_LOAD: case IR_OPCODE_STORE: case IR_OPCODE_CONSTANT_INTEGER:
                        case IR_OPCODE_FUNCTION: case IR_OPCODE_CALL: case IR_OPCODE_CAST:
                        case IR_OPCODE_ADDRESS_OF: case IR_OPCODE_DEREFERENCE: case IR_OPCODE_INDEX:
                        case IR_OPCODE_UNARY: case IR_OPCODE_BINARY:
                        case IR_OPCODE_BRANCH: case IR_OPCODE_BRANCH_IF: case IR_OPCODE_SWITCH: case IR_OPCODE_RETURN: break;
                        default: run->status = IR_ORACLE_UNSUPPORTED; break;
                    }
                    if (row->result.value < function->value_count) value_blocks[row->result.value] = block;
                    for (u32 target = 0; target < row->target_count; target += 1)
                    {
                        if (row->targets[target].value >= function->block_count) run->status = IR_ORACLE_INVALID;
                        else predecessors[row->targets[target].value] |= UINT64_C(1) << block;
                    }
                }
            }
            if (function->published_cfg)
            {
                IrCfgBlock const* span = function->published_cfg->blocks + block;
                for (u32 p = 0; p < span->parameter_count; p += 1)
                    value_blocks[function->published_cfg->parameters[span->parameter_offset + p].value.value] = block;
            }
            else
            {
                for (IrBlockParameter* p = function->blocks[block].first_parameter; p; p = p->next) value_blocks[p->value.value] = block;
            }
        }
        for (u32 sweep = 0; sweep < function->block_count; sweep += 1)
        {
            for (u32 block = 0; block < function->block_count; block += 1)
                if (predecessors[block] & reachable) reachable |= UINT64_C(1) << block;
        }
        for (u32 block = 0; block < function->block_count; block += 1) dominators[block] = reachable;
        dominators[function->entry.value] = UINT64_C(1) << function->entry.value;
        for (u32 sweep = 0; sweep < function->block_count; sweep += 1)
        {
            for (u32 block = 0; block < function->block_count; block += 1)
            {
                if (block != function->entry.value && (reachable & (UINT64_C(1) << block)))
                {
                    u64 common = reachable;
                    for (u32 p = 0; p < function->block_count; p += 1)
                        if (predecessors[block] & reachable & (UINT64_C(1) << p)) common &= dominators[p];
                    dominators[block] = common | (UINT64_C(1) << block);
                }
            }
        }
        for (u32 row_index = 0; row_index < function->instruction_count && run->status == IR_ORACLE_OK; row_index += 1)
        {
            IrInstruction* row = function->instructions + row_index;
            u32 block = owners[row_index];
            if (block == UINT32_MAX) run->status = IR_ORACLE_INVALID;
            else if (reachable & (UINT64_C(1) << block))
            {
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    u32 value = row->operands[operand].value;
                    if (!ir_oracle_dominates(function, value, block, positions[row_index], value_blocks, positions, dominators))
                        run->status = IR_ORACLE_INVALID;
                }
            }
        }
        // Incoming values execute on the predecessor edge, before the parallel
        // assignment of destination parameters.
        for (u32 block = 0; block < function->block_count && run->status == IR_ORACLE_OK; block += 1)
        {
            if (function->published_cfg)
            {
                IrPublishedCfg const* cfg = function->published_cfg;
                IrCfgBlock const* span = cfg->blocks + block;
                for (u32 edge_index = 0; edge_index < span->successor_count; edge_index += 1)
                {
                    IrCfgEdge const* edge = cfg->edges + span->successor_offset + edge_index;
                    for (u32 p = 0; p < cfg->blocks[edge->destination.value].parameter_count; p += 1)
                        if ((reachable & (UINT64_C(1) << block)) &&
                            !ir_oracle_dominates(function, cfg->arguments[edge->argument_offset + p].value, block,
                                                UINT32_MAX, value_blocks, positions, dominators)) run->status = IR_ORACLE_INVALID;
                }
            }
            else
            {
                for (IrBlockParameter* p = function->blocks[block].first_parameter; p; p = p->next)
                {
                    for (IrIncoming* incoming = p->first_incoming; incoming; incoming = incoming->next)
                        if ((reachable & (UINT64_C(1) << incoming->predecessor.value)) &&
                            !ir_oracle_dominates(function, incoming->value.value, incoming->predecessor.value, UINT32_MAX,
                                                value_blocks, positions, dominators)) run->status = IR_ORACLE_INVALID;
                }
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_oracle_transfer(IrOracleRun* run, IrOracleFrame* frame, u32 destination)
{
    IrFunction* function = frame->function;
    IrOracleValue snapshot[IR_ORACLE_VALUES];
    IrValueId values[IR_ORACLE_VALUES];
    u32 count = 0;
    if (function->published_cfg)
    {
        IrPublishedCfg const* cfg = function->published_cfg;
        IrCfgBlock const* source = cfg->blocks + frame->block;
        IrCfgBlock const* target = cfg->blocks + destination;
        IrCfgEdge const* edge = 0;
        for (u32 i = 0; i < source->successor_count; i += 1)
        {
            IrCfgEdge const* candidate = cfg->edges + source->successor_offset + i;
            if (candidate->destination.value == destination) edge = candidate;
        }
        if (!edge) run->status = IR_ORACLE_INVALID;
        else
        {
            count = target->parameter_count;
            for (u32 i = 0; i < count; i += 1)
            {
                values[i] = cfg->parameters[target->parameter_offset + i].value;
                snapshot[i] = frame->values[cfg->arguments[edge->argument_offset + i].value];
            }
        }
    }
    else
    {
        for (IrBlockParameter* p = function->blocks[destination].first_parameter; p; p = p->next)
        {
            IrIncoming* found = 0;
            for (IrIncoming* incoming = p->first_incoming; incoming; incoming = incoming->next)
                if (incoming->predecessor.value == frame->block) found = incoming;
            if (!found) run->status = IR_ORACLE_INVALID;
            else
            {
                values[count] = p->value;
                snapshot[count++] = frame->values[found->value.value];
            }
        }
    }
    for (u32 i = 0; i < count; i += 1)
    {
        if (!snapshot[i].defined) run->status = IR_ORACLE_INVALID;
        frame->values[values[i].value] = snapshot[i];
    }
    frame->block = destination;
    frame->row = function->blocks[destination].first_instruction.value;
}

BUSTER_GLOBAL_LOCAL void ir_oracle_push(IrOracleRun* run, IrFunction* function, IrOracleValue* arguments, u32 count, IrValueId returned)
{
    IrType* type = function ? ir_oracle_type(run->program, function->canonical_type) : 0;
    IrType* result_type = type ? ir_oracle_type(run->program, type->return_type) : 0;
    if (!function || !type || type->kind != IR_TYPE_FUNCTION || type->is_variadic || type->is_noreturn || type->is_unprototyped ||
        !result_type || (!ir_oracle_width(result_type) && result_type->kind != IR_TYPE_VOID) ||
        type->parameter_count != count || count > IR_ORACLE_ARGUMENTS) run->status = IR_ORACLE_UNSUPPORTED;
    else if (run->depth >= IR_ORACLE_FRAMES) run->status = IR_ORACLE_LIMIT;
    else
    {
        ir_oracle_preflight(run, function);
        if (run->status == IR_ORACLE_OK)
        {
            IrOracleFrame* frame = run->frames + run->depth++;
            memset(frame, 0, sizeof(*frame));
            frame->function = function;
            frame->block = function->entry.value;
            frame->row = function->blocks[frame->block].first_instruction.value;
            frame->return_value = returned;
            for (u32 i = 0; i < count; i += 1)
            {
                u32 width = ir_oracle_width(ir_oracle_type(run->program, type->parameter_types[i]));
                if (!width || arguments[i].kind != IR_ORACLE_INTEGER || !arguments[i].defined) run->status = IR_ORACLE_UNSUPPORTED;
                else frame->arguments[i] = ir_oracle_integer(arguments[i].bits & ir_oracle_mask(width));
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL IrOracleRun* ir_oracle_evaluate(Arena* arena, IrProgram* program, IrModule* module, IrFunction* function,
                                                  IrOracleValue* arguments, u32 count, u32 limit)
{
    IrOracleRun* run = arena_allocate(arena, IrOracleRun, 1);
    memset(run, 0, sizeof(*run));
    run->program = program;
    run->module = module;
    run->limit = limit;
    if (limit > IR_ORACLE_STEPS) run->status = IR_ORACLE_LIMIT;
    else if (program->data_layout.endianness != TARGET_ENDIAN_LITTLE || program->data_layout.pointer.size != 8 ||
        module->global_count > IR_ORACLE_ALLOCATIONS || module->initializer_count || module->assembly_count || module->alias_count)
        run->status = IR_ORACLE_UNSUPPORTED;
    else if (ir_validate_canonical_module(program, module).error != IR_VALIDATION_NONE) run->status = IR_ORACLE_INVALID;
    for (u32 i = 0; i < module->global_count && run->status == IR_ORACLE_OK; i += 1)
    {
        IrGlobal* global = module->globals + i;
        IrType* type = ir_oracle_type(program, global->type);
        u32 width = ir_oracle_width(type);
        if (!width || global->is_thread_local || global->relocation_count ||
            (global->initializer_kind != IR_GLOBAL_INITIALIZER_ZERO && global->initializer_kind != IR_GLOBAL_INITIALIZER_INTEGER))
            run->status = IR_ORACLE_UNSUPPORTED;
        else
        {
            u32 allocation = ir_oracle_allocate(run, (u32)type->layout.size, UINT32_MAX, false);
            run->globals[i] = allocation;
            if (run->status == IR_ORACLE_OK)
            {
                u64 bits = global->initializer_kind == IR_GLOBAL_INITIALIZER_ZERO ? 0 : global->initializer_bits;
                if (global->initializer_is_negative) bits = 0 - bits;
                (void)ir_oracle_memory(run, (IrOracleValue){.kind = IR_ORACLE_POINTER, .allocation = allocation, .defined = true},
                                       (u32)type->layout.size, true, bits);
                run->allocations[allocation].read_only = global->is_read_only;
            }
        }
    }
    if (run->status == IR_ORACLE_OK) ir_oracle_push(run, function, arguments, count, IR_VALUE_ID_INVALID);
    while (run->depth && run->status == IR_ORACLE_OK)
    {
        if (run->steps >= run->limit) run->status = IR_ORACLE_LIMIT;
        else
        {
            run->steps += 1;
            IrOracleFrame* frame = run->frames + run->depth - 1;
            IrFunction* current = frame->function;
            if (frame->row >= current->instruction_count) run->status = IR_ORACLE_INVALID;
            else
            {
                IrInstruction* row = current->instructions + frame->row;
                run->last_row = frame->row;
                run->last_opcode = row->opcode;
                run->last_function = current;
                IrType* type = ir_oracle_type(program, row->canonical_type);
                IrOracleValue operands[IR_ORACLE_ARGUMENTS + 1] = {0};
                if (row->operand_count > IR_ORACLE_ARGUMENTS + 1) run->status = IR_ORACLE_UNSUPPORTED;
                for (u32 i = 0; i < row->operand_count && run->status == IR_ORACLE_OK; i += 1)
                {
                    operands[i] = frame->values[row->operands[i].value];
                    if (!operands[i].defined) run->status = IR_ORACLE_INVALID;
                }
                IrOracleValue value = {0};
                u32 width = ir_oracle_width(type);
                frame->row = ir_block_next_instruction(current, current->blocks + frame->block, (IrInstructionId){.value = frame->row}).value;
                if (row->volatile_access || (type && (type->is_atomic || type->is_volatile))) run->status = IR_ORACLE_UNSUPPORTED;
                if (run->status == IR_ORACLE_OK)
                {
                    switch (row->opcode)
                    {
                        case IR_OPCODE_ARGUMENT:
                            value = frame->arguments[row->immediates[0]];
                            if (!value.defined) run->status = IR_ORACLE_INVALID;
                            break;
                        case IR_OPCODE_CONSTANT_INTEGER:
                            if (!width || row->immediate_count != 1) run->status = IR_ORACLE_UNSUPPORTED;
                            else value = ir_oracle_integer((row->immediate_is_negative ? 0 - row->immediates[0] : row->immediates[0]) & ir_oracle_mask(width));
                            break;
                        case IR_OPCODE_LOCAL:
                            if (!width) run->status = IR_ORACLE_UNSUPPORTED;
                            else if (frame->values[row->result.value].defined) run->status = IR_ORACLE_UNSUPPORTED;
                            else value = (IrOracleValue){.kind = IR_ORACLE_POINTER, .defined = true,
                                 .allocation = ir_oracle_allocate(run, (u32)type->layout.size, run->depth - 1, false)};
                            break;
                        case IR_OPCODE_GLOBAL:
                        {
                            bool found = false;
                            for (u32 i = 0; i < module->global_count; i += 1)
                            {
                                if (module->globals[i].symbol.value == row->symbol.value)
                                {
                                    found = true;
                                    value = (IrOracleValue){.kind = IR_ORACLE_POINTER, .defined = true, .allocation = run->globals[i]};
                                }
                            }
                            if (!found) run->status = IR_ORACLE_UNSUPPORTED;
                            break;
                        }
                        case IR_OPCODE_LOAD: case IR_OPCODE_STORE:
                        {
                            IrType* memory_type = ir_oracle_type(program, current->values[row->operands[0].value].canonical_type);
                            u32 memory_width = ir_oracle_width(memory_type);
                            if (!memory_width || (row->opcode == IR_OPCODE_STORE && operands[1].kind != IR_ORACLE_INTEGER))
                                run->status = IR_ORACLE_UNSUPPORTED;
                            else value = ir_oracle_memory(run, operands[0], (u32)memory_type->layout.size, row->opcode == IR_OPCODE_STORE, operands[1].bits);
                            break;
                        }
                        case IR_OPCODE_ADDRESS_OF: case IR_OPCODE_DEREFERENCE:
                            if (operands[0].kind != IR_ORACLE_POINTER) run->status = IR_ORACLE_INVALID;
                            else value = operands[0];
                            break;
                        case IR_OPCODE_INDEX:
                        {
                            IrType* base = ir_oracle_type(program, current->values[row->operands[0].value].canonical_type);
                            IrType* index_type = ir_oracle_type(program, current->values[row->operands[1].value].canonical_type);
                            u32 index_width = ir_oracle_width(index_type);
                            if (!width || !base || base->kind != IR_TYPE_POINTER || operands[0].kind != IR_ORACLE_POINTER ||
                                operands[1].kind != IR_ORACLE_INTEGER) run->status = IR_ORACLE_UNSUPPORTED;
                            else if (index_width && index_type->is_signed && (operands[1].bits & (UINT64_C(1) << (index_width - 1))))
                                run->status = IR_ORACLE_UNSUPPORTED;
                            else
                            {
                                u64 size = type->layout.size;
                                value = operands[0];
                                if (operands[1].bits > UINT64_MAX / size || value.bits > UINT64_MAX - operands[1].bits * size)
                                    run->status = IR_ORACLE_INVALID;
                                else
                                {
                                    value.bits += operands[1].bits * size;
                                    if (value.allocation >= run->allocation_count || value.bits > run->allocations[value.allocation].size)
                                        run->status = IR_ORACLE_INVALID;
                                }
                            }
                            break;
                        }
                        case IR_OPCODE_FUNCTION:
                            value = (IrOracleValue){.bits = row->symbol.value, .kind = IR_ORACLE_FUNCTION, .defined = true};
                            break;
                        case IR_OPCODE_CALL:
                            if (operands[0].kind != IR_ORACLE_FUNCTION) run->status = IR_ORACLE_UNSUPPORTED;
                            else ir_oracle_push(run, ir_oracle_function(module, (u32)operands[0].bits), operands + 1,
                                                row->operand_count - 1, row->result);
                            break;
                        case IR_OPCODE_CAST:
                        {
                            IrType* source = ir_oracle_type(program, current->values[row->operands[0].value].canonical_type);
                            u32 source_width = ir_oracle_width(source);
                            if (!width || !source_width || operands[0].kind != IR_ORACLE_INTEGER ||
                                row->conversion_operation > IR_CONVERSION_INTEGER_REINTERPRET) run->status = IR_ORACLE_UNSUPPORTED;
                            else
                            {
                                value = operands[0];
                                if (row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND &&
                                    (value.bits & (UINT64_C(1) << (source_width - 1)))) value.bits |= ~ir_oracle_mask(source_width);
                                value.bits &= ir_oracle_mask(width);
                            }
                            break;
                        }
                        case IR_OPCODE_UNARY: case IR_OPCODE_BINARY:
                        {
                            IrType* operand_type = ir_oracle_type(program, current->values[row->operands[0].value].canonical_type);
                            u32 operand_width = ir_oracle_width(operand_type);
                            if (!width || !operand_width || operands[0].kind != IR_ORACLE_INTEGER ||
                                (row->operand_count == 2 && operands[1].kind != IR_ORACLE_INTEGER)) run->status = IR_ORACLE_UNSUPPORTED;
                            else value = ir_oracle_scalar(run, row, operand_width, operands[0].bits, operands[1].bits);
                            break;
                        }
                        case IR_OPCODE_BRANCH: case IR_OPCODE_BRANCH_IF: case IR_OPCODE_SWITCH:
                        {
                            u32 target = 0;
                            if (row->opcode == IR_OPCODE_BRANCH_IF) target = operands[0].bits ? 0 : 1;
                            else if (row->opcode == IR_OPCODE_SWITCH)
                            {
                                target = row->immediate_count;
                                for (u32 i = 0; i < row->immediate_count; i += 1)
                                    if (operands[0].bits == row->immediates[i]) target = i;
                            }
                            ir_oracle_transfer(run, frame, row->targets[target].value);
                            break;
                        }
                        case IR_OPCODE_RETURN:
                            value = row->operand_count ? operands[0] : ir_oracle_integer(0);
                            for (u32 i = 0; i < run->allocation_count; i += 1)
                                if (run->allocations[i].owner == run->depth - 1) run->allocations[i].alive = false;
                            run->depth -= 1;
                            if (!run->depth) run->returned = value;
                            else if (frame->return_value.value != UINT32_MAX)
                                run->frames[run->depth - 1].values[frame->return_value.value] = value;
                            break;
                        default: run->status = IR_ORACLE_UNSUPPORTED; break;
                    }
                    if (row->opcode != IR_OPCODE_CALL && row->opcode != IR_OPCODE_RETURN && row->result.value != UINT32_MAX)
                    {
                        if (width && value.kind == IR_ORACLE_INTEGER) value.bits &= ir_oracle_mask(width);
                        frame->values[row->result.value] = value;
                    }
                }
            }
        }
    }
    return run;
}
