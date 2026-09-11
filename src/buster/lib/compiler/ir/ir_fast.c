// Compact canonical FAST pipeline, included by ir.c after ir_promote.c.
// ir_fast_function owns bounded scratch and the shared ir_rewrite_compact map.
// ir_instruction_is_pure is the semantic DCE authority. ir_fast_fold performs
// one forward pass; ir_fast_dce uses counts plus a deletion queue (no use CSR);
// ir_fast_parameters has a hard sweep cap. ir_prepare_canonical_module owns
// input, promotion-output and FAST-output certification boundaries, then
// publishes the final canonical CFG after all selected transformations.
#include <buster/lib/time.h>

String8 ir_fast_pass_name(IrFastPass pass)
{
    String8 names[] = {S8("fold"), S8("address"), S8("dce"), S8("parameters")};
    String8 result = (u32)pass < BUSTER_ARRAY_LENGTH(names) ? names[pass] : S8("invalid");
    return result;
}

bool ir_instruction_is_pure(IrProgram* program, IrFunction* function, IrInstruction const* row)
{
    bool result = false;
    if (row && row->result.value < function->value_count && !row->volatile_access)
    {
        switch (row->opcode)
        {
        case IR_OPCODE_CONSTANT_INTEGER:
        case IR_OPCODE_CONSTANT_FLOAT:
        case IR_OPCODE_CONSTANT_STRING:
        case IR_OPCODE_UNDEFINED:
        case IR_OPCODE_FUNCTION:
        case IR_OPCODE_GLOBAL:
        case IR_OPCODE_ADDRESS_OF:
        case IR_OPCODE_DEREFERENCE:
            result = true;
            break;
        case IR_OPCODE_FIELD:
        case IR_OPCODE_INDEX:
            // A value projection can load storage. Only constructing a PLACE
            // is known to be free of memory reads and floating exceptions.
            result = function->values[row->result.value].category == IR_VALUE_PLACE;
            break;
        case IR_OPCODE_CAST:
            result = row->conversion_operation == IR_CONVERSION_IDENTITY ||
                     row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                     row->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                     row->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE ||
                     row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET ||
                     row->conversion_operation == IR_CONVERSION_POINTER_REINTERPRET ||
                     row->conversion_operation == IR_CONVERSION_POINTER_TO_INTEGER ||
                     row->conversion_operation == IR_CONVERSION_INTEGER_TO_POINTER;
            break;
        case IR_OPCODE_UNARY:
            result = row->unary_operation == IR_UNARY_INTEGER_NEGATE ||
                     row->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
                     row->unary_operation == IR_UNARY_BOOLEAN_NOT;
            break;
        case IR_OPCODE_BINARY:
            switch (row->binary_operation)
            {
            case IR_BINARY_INTEGER_ADD:
            case IR_BINARY_INTEGER_SUBTRACT:
            case IR_BINARY_INTEGER_MULTIPLY:
            case IR_BINARY_SHIFT_LEFT:
            case IR_BINARY_SIGNED_SHIFT_RIGHT:
            case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
            case IR_BINARY_INTEGER_BITWISE_AND:
            case IR_BINARY_INTEGER_BITWISE_OR:
            case IR_BINARY_INTEGER_BITWISE_XOR:
            case IR_BINARY_BOOLEAN_AND:
            case IR_BINARY_BOOLEAN_OR:
            case IR_BINARY_INTEGER_EQUAL:
            case IR_BINARY_INTEGER_NOT_EQUAL:
            case IR_BINARY_POINTER_EQUAL:
            case IR_BINARY_POINTER_NOT_EQUAL:
            case IR_BINARY_BOOLEAN_EQUAL:
            case IR_BINARY_BOOLEAN_NOT_EQUAL:
            case IR_BINARY_SIGNED_LESS:
            case IR_BINARY_SIGNED_LESS_EQUAL:
            case IR_BINARY_SIGNED_GREATER:
            case IR_BINARY_SIGNED_GREATER_EQUAL:
            case IR_BINARY_UNSIGNED_LESS:
            case IR_BINARY_UNSIGNED_LESS_EQUAL:
            case IR_BINARY_UNSIGNED_GREATER:
            case IR_BINARY_UNSIGNED_GREATER_EQUAL:
                result = true;
                break;
            default: break;
            }
            break;
        default: break;
        }
        if (result)
        {
            IrType* type = ir_type_from_id(&program->types, row->canonical_type);
            result = type && !type->is_atomic && !type->is_volatile;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u32 ir_fast_width(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    u32 result = 0;
    if (type && (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN) && !type->is_atomic && !type->is_volatile)
    {
        result = type->kind == IR_TYPE_BOOLEAN ? 1 : type->bit_width;
        if (result > 64) result = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 ir_fast_mask(u32 width)
{
    u64 result = width == 64 ? UINT64_MAX : ((u64)1 << width) - 1;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_fast_constant(IrProgram* program, IrFunction* function, u32 value, u64* bits)
{
    IrValue* slot = function->values + value;
    bool result = false;
    if (slot->definition.value < function->instruction_count)
    {
        IrInstruction* row = function->instructions + slot->definition.value;
        if (row->opcode == IR_OPCODE_CONSTANT_INTEGER && row->immediate_count == 1)
        {
            u32 width = ir_fast_width(program, slot->canonical_type);
            if (width)
            {
                *bits = (row->immediate_is_negative ? (u64)0 - row->immediates[0] : row->immediates[0]) & ir_fast_mask(width);
                result = true;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_fast_binary(u32 operation, u64 left, u64 right, u32 width, u64* bits)
{
    bool result = true;
    u64 sign = (u64)1 << (width - 1);
    switch (operation)
    {
    case IR_BINARY_INTEGER_ADD: *bits = left + right; break;
    case IR_BINARY_INTEGER_SUBTRACT: *bits = left - right; break;
    case IR_BINARY_INTEGER_MULTIPLY: *bits = left * right; break;
    case IR_BINARY_INTEGER_BITWISE_AND: *bits = left & right; break;
    case IR_BINARY_INTEGER_BITWISE_OR: *bits = left | right; break;
    case IR_BINARY_INTEGER_BITWISE_XOR: *bits = left ^ right; break;
    case IR_BINARY_BOOLEAN_AND: *bits = left && right; break;
    case IR_BINARY_BOOLEAN_OR: *bits = left || right; break;
    case IR_BINARY_INTEGER_EQUAL:
    case IR_BINARY_BOOLEAN_EQUAL: *bits = left == right; break;
    case IR_BINARY_INTEGER_NOT_EQUAL:
    case IR_BINARY_BOOLEAN_NOT_EQUAL: *bits = left != right; break;
    case IR_BINARY_UNSIGNED_LESS: *bits = left < right; break;
    case IR_BINARY_UNSIGNED_LESS_EQUAL: *bits = left <= right; break;
    case IR_BINARY_UNSIGNED_GREATER: *bits = left > right; break;
    case IR_BINARY_UNSIGNED_GREATER_EQUAL: *bits = left >= right; break;
    case IR_BINARY_SIGNED_LESS: *bits = (left ^ sign) < (right ^ sign); break;
    case IR_BINARY_SIGNED_LESS_EQUAL: *bits = (left ^ sign) <= (right ^ sign); break;
    case IR_BINARY_SIGNED_GREATER: *bits = (left ^ sign) > (right ^ sign); break;
    case IR_BINARY_SIGNED_GREATER_EQUAL: *bits = (left ^ sign) >= (right ^ sign); break;
    case IR_BINARY_SHIFT_LEFT:
        result = right < width;
        if (result) *bits = left << right;
        break;
    case IR_BINARY_UNSIGNED_SHIFT_RIGHT:
        result = right < width;
        if (result) *bits = left >> right;
        break;
    case IR_BINARY_SIGNED_SHIFT_RIGHT:
        result = right < width;
        if (result)
        {
            u64 extended = left | ((left & sign) ? ~ir_fast_mask(width) : 0);
            *bits = extended >> right;
            if (right && (left & sign)) *bits |= UINT64_MAX << (64 - right);
        }
        break;
    default: result = false; break;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ir_fast_fold(IrProgram* program, IrFunction* function, u32* replacements, u8* removed,
                                      IrFastPass pass, IrFastPassStatistics* statistics)
{
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        statistics->visits += 1;
        if (removed[index] || row->result.value >= function->value_count) continue;
        bool address = pass == IR_FAST_ADDRESS;
        // Most rows cannot participate in either rewrite. In particular the
        // address pass has no reason to query scalar widths or constants.
        if (address ? (row->opcode != IR_OPCODE_ADDRESS_OF && row->opcode != IR_OPCODE_DEREFERENCE) :
            (row->opcode != IR_OPCODE_CAST && row->opcode != IR_OPCODE_UNARY && row->opcode != IR_OPCODE_BINARY)) continue;
        u32 replacement = IR_PROMOTE_NONE;
        u32 first = row->operand_count ? ir_promote_root(replacements, row->operands[0].value) : IR_PROMOTE_NONE;
        u32 second = !address && row->operand_count > 1 ? ir_promote_root(replacements, row->operands[1].value) : IR_PROMOTE_NONE;
        u32 width = address ? 0 : ir_fast_width(program, row->canonical_type);
        u64 left = 0, right = 0, bits = 0;
        bool constant = false;
        bool left_constant = !address && first != IR_PROMOTE_NONE && ir_fast_constant(program, function, first, &left);
        bool right_constant = second != IR_PROMOTE_NONE && ir_fast_constant(program, function, second, &right);
        if (address)
        {
            if ((row->opcode == IR_OPCODE_ADDRESS_OF || row->opcode == IR_OPCODE_DEREFERENCE) && first != IR_PROMOTE_NONE)
            {
                u32 definition = function->values[first].definition.value;
                if (definition < function->instruction_count)
                {
                    IrInstruction* source = function->instructions + definition;
                    if (source->operand_count == 1 &&
                        ((row->opcode == IR_OPCODE_ADDRESS_OF && source->opcode == IR_OPCODE_DEREFERENCE) ||
                         (row->opcode == IR_OPCODE_DEREFERENCE && source->opcode == IR_OPCODE_ADDRESS_OF)))
                    {
                        replacement = ir_promote_root(replacements, source->operands[0].value);
                    }
                }
            }
        }
        else if (row->opcode == IR_OPCODE_CAST && first != IR_PROMOTE_NONE)
        {
            if (row->canonical_type.value == function->values[first].canonical_type.value && ir_instruction_is_pure(program, function, row))
            {
                replacement = first;
            }
            else if (width && left_constant)
            {
                u32 source_width = ir_fast_width(program, function->values[first].canonical_type);
                if (row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND)
                {
                    bits = left | ((left & ((u64)1 << (source_width - 1))) ? ~ir_fast_mask(source_width) : 0);
                    constant = true;
                }
                else if (row->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                         row->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE ||
                         row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET)
                {
                    bits = left;
                    constant = true;
                }
            }
        }
        else if (width && row->opcode == IR_OPCODE_UNARY && left_constant)
        {
            switch (row->unary_operation)
            {
            case IR_UNARY_INTEGER_NEGATE: bits = (u64)0 - left; constant = true; break;
            case IR_UNARY_INTEGER_BITWISE_NOT: bits = ~left; constant = true; break;
            case IR_UNARY_BOOLEAN_NOT: bits = !left; constant = true; break;
            default: break;
            }
        }
        else if (width && row->opcode == IR_OPCODE_BINARY && second != IR_PROMOTE_NONE)
        {
            u32 source_width = ir_fast_width(program, function->values[first].canonical_type);
            if (source_width && left_constant && right_constant)
            {
                constant = ir_fast_binary(row->binary_operation, left, right, source_width, &bits);
            }
            if (!constant && right_constant)
            {
                bool zero_identity = row->binary_operation == IR_BINARY_INTEGER_ADD || row->binary_operation == IR_BINARY_INTEGER_SUBTRACT ||
                                     row->binary_operation == IR_BINARY_INTEGER_BITWISE_OR || row->binary_operation == IR_BINARY_INTEGER_BITWISE_XOR ||
                                     row->binary_operation == IR_BINARY_SHIFT_LEFT || row->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                                     row->binary_operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
                if ((right == 0 && zero_identity) || (right == 1 && row->binary_operation == IR_BINARY_INTEGER_MULTIPLY) ||
                    (right == ir_fast_mask(width) && row->binary_operation == IR_BINARY_INTEGER_BITWISE_AND)) replacement = first;
            }
            if (!constant && left_constant && ((left == 0 && (row->binary_operation == IR_BINARY_INTEGER_ADD ||
                 row->binary_operation == IR_BINARY_INTEGER_BITWISE_OR || row->binary_operation == IR_BINARY_INTEGER_BITWISE_XOR)) ||
                (left == 1 && row->binary_operation == IR_BINARY_INTEGER_MULTIPLY))) replacement = second;
        }
        if (replacement != IR_PROMOTE_NONE)
        {
            IrValue a = function->values[row->result.value];
            IrValue b = function->values[replacement];
            if (a.canonical_type.value == b.canonical_type.value && a.category == b.category && a.alignment == b.alignment &&
                a.is_read_only == b.is_read_only && a.points_to_read_only == b.points_to_read_only && a.is_volatile == b.is_volatile)
            {
                replacements[row->result.value] = replacement;
                removed[index] = 1;
                statistics->changes += 1;
            }
        }
        else if (constant)
        {
            row->opcode = IR_OPCODE_CONSTANT_INTEGER;
            row->operands = 0;
            row->operand_count = 0;
            row->immediates = arena_allocate(program->arena, u64, 1);
            row->immediates[0] = bits & ir_fast_mask(width);
            row->immediate_count = 1;
            row->immediate_is_negative = false;
            statistics->changes += 1;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_fast_dce(IrProgram* program, IrFunction* function, u32* replacements, u8* removed,
                                     u32* uses, u32* queue, IrFastPassStatistics* statistics)
{
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        statistics->visits += 1;
        for (u32 operand = 0; !removed[index] && operand < row->operand_count; operand += 1)
        {
            uses[ir_promote_root(replacements, row->operands[operand].value)] += 1;
            statistics->visits += 1;
        }
    }
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
        {
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                uses[ir_promote_root(replacements, incoming->value.value)] += 1;
                statistics->visits += 1;
            }
        }
    }
    u32 head = 0, tail = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        if (!removed[index] && row->result.value < function->value_count && !uses[row->result.value] &&
            ir_instruction_is_pure(program, function, row)) queue[tail++] = index;
    }
    while (head < tail)
    {
        u32 index = queue[head++];
        IrInstruction* row = function->instructions + index;
        removed[index] = 1;
        statistics->changes += 1;
        statistics->visits += 1;
        for (u32 operand = 0; operand < row->operand_count; operand += 1)
        {
            u32 value = ir_promote_root(replacements, row->operands[operand].value);
            BUSTER_CHECK(uses[value] != 0);
            uses[value] -= 1;
            statistics->visits += 1;
            u32 definition = function->values[value].definition.value;
            if (!uses[value] && definition < function->instruction_count && !removed[definition] &&
                ir_instruction_is_pure(program, function, function->instructions + definition)) queue[tail++] = definition;
        }
    }
}

BUSTER_GLOBAL_LOCAL void ir_fast_parameters(IrFunction* function, u32* replacements, IrFastStatistics* statistics)
{
    IrFastPassStatistics* pass = statistics->passes + IR_FAST_PARAMETERS;
    bool changed = true;
    u32 sweep = 0;
    while (changed && sweep < IR_FAST_PARAMETER_SWEEPS)
    {
        changed = false;
        sweep += 1;
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            IrBlock* destination = function->blocks + block;
            IrBlockParameter** link = &destination->first_parameter;
            destination->last_parameter = 0;
            while (*link)
            {
                IrBlockParameter* parameter = *link;
                u32 same = IR_PROMOTE_NONE;
                bool trivial = true;
                pass->visits += 1;
                for (IrIncoming* incoming = parameter->first_incoming; incoming && trivial; incoming = incoming->next)
                {
                    u32 value = ir_promote_root(replacements, incoming->value.value);
                    pass->visits += 1;
                    if (value != parameter->value.value)
                    {
                        trivial = same == IR_PROMOTE_NONE || value == same;
                        same = value;
                    }
                }
                if (trivial && same != IR_PROMOTE_NONE)
                {
                    replacements[parameter->value.value] = same;
                    *link = parameter->next;
                    destination->parameter_count -= 1;
                    pass->changes += 1;
                    changed = true;
                }
                else
                {
                    destination->last_parameter = parameter;
                    link = &parameter->next;
                }
            }
        }
    }
    statistics->parameter_budget_hits += changed;
}

BUSTER_GLOBAL_LOCAL void ir_fast_function(IrProgram* program, IrFunction* function, IrFastStatistics* statistics)
{
    statistics->functions += 1;
    statistics->instructions_before += function->instruction_count;
    u64 operands = 0;
    bool provenance = function->label_metadata_count != 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        if (operands <= IR_FAST_WORK_BUDGET) operands += row->operand_count;
        provenance |= row->opcode == IR_OPCODE_LABEL_ADDRESS || row->opcode == IR_OPCODE_INDIRECT_BRANCH;
    }
    u64 work = operands + function->instruction_count + function->value_count + function->block_count;
    u64 reopening_bytes = 0;
    IrPublishedCfg const* cfg = function->published_cfg;
    if (cfg)
    {
        // Publication already counted the incoming population. Inspect those
        // counts before reopening allocates any mutable builder storage.
        work += (u64)cfg->parameter_count + cfg->argument_count;
        reopening_bytes = (u64)cfg->parameter_count * sizeof(IrBlockParameter) +
                          (u64)cfg->argument_count * sizeof(IrIncoming) + (u64)cfg->edge_count * sizeof(IrPredecessor) +
                          (BUSTER_ALIGN_OF(IrBlockParameter) - 1u) + (BUSTER_ALIGN_OF(IrIncoming) - 1u) +
                          (BUSTER_ALIGN_OF(IrPredecessor) - 1u);
    }
    else
    {
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
            {
                if (work <= IR_FAST_WORK_BUDGET) work += (u64)parameter->incoming_count + 1;
            }
        }
    }
    // Includes maps used only by compaction, the DCE queue/counts and padding.
    u64 scratch_bytes = (u64)function->value_count * 16 + (u64)function->instruction_count * 16 + 256;
    u64 retained_bytes = operands * sizeof(IrValueId) + (u64)function->instruction_count * sizeof(u64);
    if (provenance) statistics->provenance_skips += 1;
    else if (scratch_bytes > IR_FAST_SCRATCH_BUDGET || retained_bytes + reopening_bytes > IR_FAST_RETAINED_BUDGET || work > IR_FAST_WORK_BUDGET) statistics->budget_skips += 1;
    else
    {
        ir_function_invalidate_cfg(function);
        // Reopening retains its arrays even if no optional pass changes a row.
        statistics->retained_bytes += reopening_bytes;
        TemporalArena scratch = scratch_begin(&program->arena, 1);
        Arena* arena = scratch.arena;
        u32* replacements = arena_allocate(arena, u32, function->value_count);
        u8* removed = arena_allocate(arena, u8, function->instruction_count);
        memset(removed, 0, function->instruction_count);
        for (u32 value = 0; value < function->value_count; value += 1) replacements[value] = value;
        statistics->scratch_peak_bytes = BUSTER_MAX(statistics->scratch_peak_bytes, scratch_bytes);
        u64 changes = 0;
        for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1)
        {
            if (!(program->fast_passes & IR_FAST_PASS_BIT(pass))) continue;
            IrFastPassStatistics* measurement = statistics->passes + pass;
            u64 before = measurement->changes;
            TimeDataType start = program->measure_fast_passes ? timestamp_take() : (TimeDataType){0};
            if (pass == IR_FAST_FOLD || pass == IR_FAST_ADDRESS)
            {
                ir_fast_fold(program, function, replacements, removed, (IrFastPass)pass, measurement);
            }
            else if (pass == IR_FAST_DCE)
            {
                u32* uses = arena_allocate(arena, u32, function->value_count);
                u32* queue = arena_allocate(arena, u32, function->instruction_count);
                memset(uses, 0, sizeof(u32) * function->value_count);
                ir_fast_dce(program, function, replacements, removed, uses, queue, measurement);
            }
            else ir_fast_parameters(function, replacements, statistics);
            if (program->measure_fast_passes) measurement->nanoseconds += timestamp_ns_between(start, timestamp_take());
            changes += measurement->changes - before;
        }
        if (changes)
        {
            TimeDataType start = program->measure_fast_passes ? timestamp_take() : (TimeDataType){0};
            ir_rewrite_compact(arena, program, function, replacements, removed);
            if (program->measure_fast_passes) statistics->compact_nanoseconds += timestamp_ns_between(start, timestamp_take());
            statistics->retained_bytes += retained_bytes;
        }
        scratch_end(scratch);
    }
    statistics->instructions_after += function->instruction_count;
}
#if BUSTER_INCLUDE_TESTS
IrFastStatistics ir_test_fast_function(IrProgram* program, IrFunction* function)
{
    IrFastStatistics result = {0};
    ir_fast_function(program, function, &result);
    return result;
}
#endif

IrValidationResult ir_prepare_canonical_module(IrProgram* program, IrModule* module, bool input_certified)
{
    IrValidationResult result = ir_validation_ok();
    if (!program || !program->arena || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
        result.boundary = IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
    }
    else
    {
        if (!input_certified)
        {
            result = ir_validate_canonical_module(program, module);
            result.boundary = IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
        }
        if (result.error == IR_VALIDATION_NONE && !program->disable_local_promotion && !module->local_promotion_complete)
        {
            module->local_promotion = (IrLocalPromotionStatistics){0};
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    ir_promote_function(program, function, &module->local_promotion);
                }
            }
            if (module->local_promotion.promoted_locals)
            {
                // Mutation ends the input certificate's scope. The optimized
                // production fast path trusts this pass's own contract, not
                // the producer's certificate. Debug/test/sanitizer consumers
                // check the transformed rows before publication instead.
                if (!input_certified || BUSTER_IR_TRANSFORM_CHECKS)
                {
                    result = ir_validate_canonical_module(program, module);
                    result.boundary = IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT;
                }
            }
            module->local_promotion_complete = result.error == IR_VALIDATION_NONE;
        }
        if (result.error == IR_VALIDATION_NONE && program->fast_passes && !module->fast_complete)
        {
            module->fast = (IrFastStatistics){0};
            // A producer certificate is sufficient for the ordinary backend,
            // but some legacy accepted shapes do not yet satisfy the stricter
            // canonical validator. Optional rewrites decline those modules as
            // a unit: this keeps explicit/default FAST safe without turning an
            // existing accepted source into a diagnostic.
            bool fast_input_valid = true;
            if (input_certified)
            {
                IrValidationResult fast_input = ir_validate_canonical_module(program, module);
                fast_input_valid = fast_input.error == IR_VALIDATION_NONE;
            }
            if (!fast_input_valid)
            {
                module->fast.validation_skips = 1;
            }
            else
            {
                for (u32 index = 0; index < module->function_count; index += 1)
                {
                    IrFunction* function = module->functions + index;
                    if (function->state == IR_FUNCTION_LOWERED) ir_fast_function(program, function, &module->fast);
                }
                u64 changes = 0;
                for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) changes += module->fast.passes[pass].changes;
                if (changes && (!input_certified || BUSTER_IR_TRANSFORM_CHECKS))
                {
                    result = ir_validate_canonical_module(program, module);
                    result.boundary = IR_VALIDATION_BOUNDARY_FAST_OUTPUT;
                }
            }
            module->fast_complete = result.error == IR_VALIDATION_NONE;
        }
        for (u32 index = 0; index < module->function_count && result.error == IR_VALIDATION_NONE; index += 1)
        {
            IrFunction* function = module->functions + index;
            if (function->state == IR_FUNCTION_LOWERED)
            {
                IrValidationResult published = ir_function_publish_cfg(program->arena, function);
                if (published.error != IR_VALIDATION_NONE)
                {
                    result = published;
                }
            }
        }
    }
    return result;
}
