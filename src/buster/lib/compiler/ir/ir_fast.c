// Compact canonical FAST pipeline, included by ir.c after ir_promote.c.
// ir_fast_function owns bounded scratch and the shared ir_rewrite_compact map.
// ir_instruction_is_pure is the semantic DCE authority. ir_fast_fold performs
// one forward pass and takes each scalar rule from ir_fast_fold_rule, which
// the diagnostic-only ir_construction_fold_census also asks at C-builder
// commit (#3010; ir_construction.fold_birth_*/fold_late_*); ir_fast_dce uses counts plus a deletion queue (no use CSR);
// ir_fast_parameters has a hard sweep cap. ir_prepare_canonical_module owns
// input, inlining-output, promotion-output and FAST-output certification boundaries, then
// publishes the final canonical CFG after all selected transformations.
// ir_validate_promotion_output leaves out of the promotion-output check any
// function whose certified input already failed the strict validator.
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

// Folding stays within one immediate limb: a folded row is a
// CONSTANT_INTEGER with a single u64, so wider, atomic and volatile values
// are never candidates. The arithmetic itself is ir_integer_* (ir_integer.c).
BUSTER_GLOBAL_LOCAL u32 ir_fast_width(IrProgram* program, IrTypeId id)
{
    IrType* type = ir_type_from_id(&program->types, id);
    u32 result = 0;
    if (type && (type->kind == IR_TYPE_INTEGER || type->kind == IR_TYPE_BOOLEAN) && !type->is_atomic && !type->is_volatile)
    {
        result = ir_integer_type_width(type);
        if (result > 64) result = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ir_fast_constant(IrProgram* program, IrFunction* function, u32 value, u64* bits)
{
    IrValue* slot = function->values + value;
    bool result = false;
    if (slot->definition.value < function->instruction_count)
    {
        IrInstruction* row = function->instructions + slot->definition.value;
        IrInteger decoded = {0};
        u32 width = ir_fast_width(program, slot->canonical_type);
        if (row->opcode == IR_OPCODE_CONSTANT_INTEGER && row->immediate_count == 1 && width && ir_integer_constant_decode(row, width, &decoded))
        {
            *bits = decoded.low;
            result = true;
        }
    }
    return result;
}

// The faults after which a row is left for run time: the FAST contract folds
// no division or remainder at all, and an out-of-range shift count or a
// non-integer operation has no canonical value to fold to. Wrapping integer
// arithmetic is canonical and folds.
enum
{
    IR_FAST_UNFOLDABLE_FAULTS = IR_INTEGER_FAULT_SHIFT_COUNT | IR_INTEGER_FAULT_DIVIDE_BY_ZERO | IR_INTEGER_FAULT_ZERO_COUNT |
                                IR_INTEGER_FAULT_UNSUPPORTED,
};

BUSTER_GLOBAL_LOCAL bool ir_fast_binary(u32 operation, u64 left, u64 right, u32 width, u32 right_width, u64* bits)
{
    bool result = operation != IR_BINARY_SIGNED_DIVIDE && operation != IR_BINARY_UNSIGNED_DIVIDE && operation != IR_BINARY_SIGNED_REMAINDER &&
                  operation != IR_BINARY_UNSIGNED_REMAINDER;
    if (result)
    {
        IrIntegerResult folded = ir_integer_binary((IrBinaryOperation)operation, (IrInteger){.low = left}, (IrInteger){.low = right}, width, right_width);
        result = !(folded.faults & IR_FAST_UNFOLDABLE_FAULTS);
        *bits = folded.bits.low;
    }
    return result;
}

// The scalar rules ir_fast_fold_rule can apply, in the order of the
// FOLD_BIRTH_* and FOLD_LATE_* counters in ir_construction.h.
typedef enum IrFastFoldRule
{
    IR_FAST_FOLD_RULE_NONE,
    IR_FAST_FOLD_RULE_CAST_IDENTITY,
    IR_FAST_FOLD_RULE_CAST_CONSTANT,
    IR_FAST_FOLD_RULE_UNARY_CONSTANT,
    IR_FAST_FOLD_RULE_BINARY_CONSTANT,
    IR_FAST_FOLD_RULE_BINARY_RIGHT_IDENTITY,
    IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY,
    IR_FAST_FOLD_RULE_COUNT,
} IrFastFoldRule;

#if BUSTER_BENCH_ALLOCATIONS
// The FOLD_BIRTH_* and FOLD_LATE_* counters follow IrFastFoldRule.
BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_BIRTH_BINARY_LEFT_IDENTITY - IR_CONSTRUCTION_FOLD_BIRTH_CAST_IDENTITY ==
                IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY - IR_FAST_FOLD_RULE_CAST_IDENTITY);
BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_LATE_BINARY_LEFT_IDENTITY - IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY ==
                IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY - IR_FAST_FOLD_RULE_CAST_IDENTITY);

BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_late(IrFastFoldRule rule, bool replaced_roots)
{
    if (rule != IR_FAST_FOLD_RULE_NONE)
    {
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY + (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
        IR_CONSTRUCTION_RECORD(FOLD_LATE_REPLACED_ROOTS, replaced_roots);
    }
}
#define IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots) ir_fast_fold_census_late(rule, replaced_roots)
#else
#define IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots) ((void)0)
#endif

// The scalar rule ir_fast_fold applies to one CAST, UNARY or BINARY row whose
// operands resolve to `first` and `second`. An identity names the existing
// value in *replacement; a constant leaves the folded immediate in *bits. The
// caller still owns the replacement metadata check, so the diagnostic
// construction census can ask the same question of a row at commit.
BUSTER_GLOBAL_LOCAL IrFastFoldRule ir_fast_fold_rule(IrProgram* program, IrFunction* function, IrInstruction const* row, u32 first, u32 second,
                                                     u32* replacement, u64* bits)
{
    IrFastFoldRule rule = IR_FAST_FOLD_RULE_NONE;
    u32 width = ir_fast_width(program, row->canonical_type);
    u64 left = 0, right = 0, folded_bits = 0;
    bool constant = false;
    bool left_constant = first != IR_PROMOTE_NONE && ir_fast_constant(program, function, first, &left);
    bool right_constant = second != IR_PROMOTE_NONE && ir_fast_constant(program, function, second, &right);
    if (row->opcode == IR_OPCODE_CAST && first != IR_PROMOTE_NONE)
    {
        if (row->canonical_type.value == function->values[first].canonical_type.value && ir_instruction_is_pure(program, function, row))
        {
            *replacement = first;
            rule = IR_FAST_FOLD_RULE_CAST_IDENTITY;
        }
        else if (width && left_constant)
        {
            u32 source_width = ir_fast_width(program, function->values[first].canonical_type);
            if (row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ||
                row->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ||
                row->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE ||
                row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET)
            {
                IrIntegerResult converted = ir_integer_convert((IrConversionOperation)row->conversion_operation, (IrInteger){.low = left},
                                                               source_width, width);
                constant = source_width && !(converted.faults & IR_INTEGER_FAULT_UNSUPPORTED);
                folded_bits = converted.bits.low;
                rule = constant ? IR_FAST_FOLD_RULE_CAST_CONSTANT : rule;
                IR_SEMANTIC_RECORD(FAST_FOLD_ATTEMPTS, 1);
                IR_SEMANTIC_RECORD(FAST_FOLD_REJECTED, !constant);
            }
        }
    }
    else if (width && row->opcode == IR_OPCODE_UNARY && left_constant)
    {
        u32 source_width = ir_fast_width(program, function->values[first].canonical_type);
        if (source_width && (row->unary_operation == IR_UNARY_INTEGER_NEGATE || row->unary_operation == IR_UNARY_INTEGER_BITWISE_NOT ||
                             row->unary_operation == IR_UNARY_BOOLEAN_NOT))
        {
            IrIntegerResult folded = ir_integer_unary((IrUnaryOperation)row->unary_operation, (IrInteger){.low = left}, source_width);
            constant = !(folded.faults & IR_FAST_UNFOLDABLE_FAULTS);
            folded_bits = folded.bits.low;
            rule = constant ? IR_FAST_FOLD_RULE_UNARY_CONSTANT : rule;
            IR_SEMANTIC_RECORD(FAST_FOLD_ATTEMPTS, 1);
            IR_SEMANTIC_RECORD(FAST_FOLD_REJECTED, !constant);
        }
    }
    else if (width && row->opcode == IR_OPCODE_BINARY && second != IR_PROMOTE_NONE)
    {
        u32 source_width = ir_fast_width(program, function->values[first].canonical_type);
        u32 right_width = ir_fast_width(program, function->values[second].canonical_type);
        if (source_width && right_width && left_constant && right_constant)
        {
            constant = ir_fast_binary(row->binary_operation, left, right, source_width, right_width, &folded_bits);
            rule = constant ? IR_FAST_FOLD_RULE_BINARY_CONSTANT : rule;
            IR_SEMANTIC_RECORD(FAST_FOLD_ATTEMPTS, 1);
            IR_SEMANTIC_RECORD(FAST_FOLD_REJECTED, !constant);
        }
        if (!constant && right_constant)
        {
            bool zero_identity = row->binary_operation == IR_BINARY_INTEGER_ADD || row->binary_operation == IR_BINARY_INTEGER_SUBTRACT ||
                                 row->binary_operation == IR_BINARY_INTEGER_BITWISE_OR || row->binary_operation == IR_BINARY_INTEGER_BITWISE_XOR ||
                                 row->binary_operation == IR_BINARY_SHIFT_LEFT || row->binary_operation == IR_BINARY_SIGNED_SHIFT_RIGHT ||
                                 row->binary_operation == IR_BINARY_UNSIGNED_SHIFT_RIGHT;
            if ((right == 0 && zero_identity) || (right == 1 && row->binary_operation == IR_BINARY_INTEGER_MULTIPLY) ||
                (right == ir_integer_mask((IrInteger){.low = UINT64_MAX}, width).low && row->binary_operation == IR_BINARY_INTEGER_BITWISE_AND))
            {
                *replacement = first;
                rule = IR_FAST_FOLD_RULE_BINARY_RIGHT_IDENTITY;
            }
        }
        if (!constant && left_constant && ((left == 0 && (row->binary_operation == IR_BINARY_INTEGER_ADD ||
             row->binary_operation == IR_BINARY_INTEGER_BITWISE_OR || row->binary_operation == IR_BINARY_INTEGER_BITWISE_XOR)) ||
            (left == 1 && row->binary_operation == IR_BINARY_INTEGER_MULTIPLY)))
        {
            *replacement = second;
            rule = IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY;
        }
    }
    if (constant)
    {
        *bits = ir_integer_from_u64(folded_bits, width).low;
    }
    return rule;
}

// A row may be replaced by an existing value only when the two values are
// indistinguishable to every later reader.
BUSTER_GLOBAL_LOCAL bool ir_fast_replacement_compatible(IrFunction const* function, u32 value, u32 replacement)
{
    IrValue a = function->values[value];
    IrValue b = function->values[replacement];
    return a.canonical_type.value == b.canonical_type.value && a.category == b.category && a.alignment == b.alignment &&
           a.is_read_only == b.is_read_only && a.points_to_read_only == b.points_to_read_only && a.is_volatile == b.is_volatile;
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
        u64 bits = 0;
        IrFastFoldRule rule = IR_FAST_FOLD_RULE_NONE;
        if (address)
        {
            if (first != IR_PROMOTE_NONE)
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
        else
        {
            rule = ir_fast_fold_rule(program, function, row, first, second, &replacement, &bits);
        }
#if BUSTER_BENCH_ALLOCATIONS
        // A late fold needed a replacement-map root when an operand no longer
        // names the value it was committed with: an earlier identity in this
        // scan or a parameter sweep supplied the fact. Read before a constant
        // rewrite clears the operands.
        bool replaced_roots = (first != IR_PROMOTE_NONE && first != row->operands[0].value) ||
                              (second != IR_PROMOTE_NONE && second != row->operands[1].value);
#endif
        if (replacement != IR_PROMOTE_NONE)
        {
            if (ir_fast_replacement_compatible(function, row->result.value, replacement))
            {
                replacements[row->result.value] = replacement;
                removed[index] = 1;
                statistics->changes += 1;
                IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots);
            }
        }
        else if (rule != IR_FAST_FOLD_RULE_NONE)
        {
            IR_SEMANTIC_RECORD(FAST_FOLD_ACCEPTED, 1);
            row->opcode = IR_OPCODE_CONSTANT_INTEGER;
            row->operands = 0;
            row->operand_count = 0;
            row->immediates = arena_allocate(program->arena, u64, 1);
            row->immediates[0] = bits;
            row->immediate_count = 1;
            row->immediate_is_negative = false;
            statistics->changes += 1;
            IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots);
        }
    }
}

#if BUSTER_BENCH_ALLOCATIONS
// The construction half of the #3010 census: asks ir_fast_fold_rule, with no
// replacement map, whether a row the C builder just committed would already
// fold from the operands committed before it. Probes neither change the row
// nor count as semantic work. The bytes are logical row/value/operand storage
// a birth fold would not have filled (an identity needs none of them; a
// constant still needs its row and immediate), not arena calls or RSS.
void ir_construction_fold_census(IrProgram* program, IrFunction* function, IrInstructionId id)
{
    IrInstruction const* row = id.value < function->instruction_count ? function->instructions + id.value : 0;
    if (row && row->result.value < function->value_count && row->operand_count && row->operands &&
        (row->opcode == IR_OPCODE_CAST || row->opcode == IR_OPCODE_UNARY || row->opcode == IR_OPCODE_BINARY))
    {
        u32 replacement = IR_PROMOTE_NONE;
        u64 bits = 0;
        u32 second = row->operand_count > 1 ? row->operands[1].value : IR_PROMOTE_NONE;
        bool suppressed = ir_semantic_suppress(true);
        IrFastFoldRule rule = ir_fast_fold_rule(program, function, row, row->operands[0].value, second, &replacement, &bits);
        ir_semantic_suppress(suppressed);
        IR_CONSTRUCTION_RECORD(FOLD_BIRTH_SCALAR_ROWS, 1);
        if (replacement != IR_PROMOTE_NONE && !ir_fast_replacement_compatible(function, row->result.value, replacement))
        {
            IR_CONSTRUCTION_RECORD(FOLD_BIRTH_METADATA_MISMATCHES, 1);
            rule = IR_FAST_FOLD_RULE_NONE;
        }
        if (rule != IR_FAST_FOLD_RULE_NONE)
        {
            u64 operand_bytes = (u64)row->operand_count * sizeof(IrValueId);
            u64 source_bytes = function->instruction_canonical_sources ? sizeof(IrSourceRange) : 0;
            ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_BIRTH_CAST_IDENTITY + (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
            IR_CONSTRUCTION_RECORD(FOLD_BIRTH_AVOIDABLE_BYTES, replacement != IR_PROMOTE_NONE
                                                                   ? sizeof(IrInstruction) + sizeof(IrValue) + operand_bytes + source_bytes
                                                                   : operand_bytes);
        }
    }
}
#endif

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
    u64 operands = function->operand_total;
    bool provenance = function->label_metadata_count != 0;
    u64 provenance_opcodes = IR_OPCODE_BIT(IR_OPCODE_LABEL_ADDRESS) | IR_OPCODE_BIT(IR_OPCODE_INDIRECT_BRANCH);
    if ((function->opcode_summary & IR_OPCODE_SUMMARY_KNOWN) && function->operand_total_rows == function->instruction_count)
    {
        provenance |= ir_function_may_contain_opcodes(function, provenance_opcodes);
    }
    else
    {
        operands = 0;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction* row = function->instructions + index;
            if (operands <= IR_FAST_WORK_BUDGET) operands += row->operand_count;
            provenance |= row->opcode == IR_OPCODE_LABEL_ADDRESS || row->opcode == IR_OPCODE_INDIRECT_BRANCH;
        }
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

#define IR_PROMOTION_INPUT_INVALID 1u
#define IR_PROMOTION_CHANGED 2u

// The promotion-output check for certified input in a checked build. It skips
// functions that failed the strict validator before promotion, and a function
// promotion left unchanged that fails now had the same defect before it ran.
// Both count in `excluded`; only a changed function that was valid fails.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_promotion_output(IrProgram* program, IrModule* module, u8 const* attribution, u32* excluded)
{
    IrValidationResult result = ir_validate_canonical_scope(program, module);
    for (u32 index = 0; result.error == IR_VALIDATION_NONE && index < module->function_count; index += 1)
    {
        IrFunction* function = module->functions + index;
        if (function->state == IR_FUNCTION_LOWERED)
        {
            if (attribution[index] & IR_PROMOTION_INPUT_INVALID)
            {
                *excluded += 1;
            }
            else
            {
                IrValidationResult checked = ir_validate_canonical_function(program, function);
                if (checked.error != IR_VALIDATION_NONE && (attribution[index] & IR_PROMOTION_CHANGED))
                {
                    result = checked;
                }
                else if (checked.error != IR_VALIDATION_NONE)
                {
                    *excluded += 1;
                }
            }
        }
    }
    return result;
}

IrValidationResult ir_prepare_canonical_module(IrProgram* program, IrModule* module, bool input_certified)
{
    IrValidationResult result = ir_validation_ok();
    IR_CONSTRUCTION_RECORD(PREPARATION_CALLS, 1);
    if (!program || !program->arena || !module)
    {
        result.error = IR_VALIDATION_INVALID_ID;
        result.boundary = IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
    }
    else
    {
        // True while the strict canonical validator has passed over the
        // module in exactly its current state. Every scan that succeeds sets
        // it and every mutation clears it, so the FAST input guard below --
        // which asks precisely this predicate -- pays for a scan only when
        // nothing has already performed it on the unchanged rows.
        bool validated = false;
        if (!input_certified)
        {
            IR_CONSTRUCTION_RECORD(PREPARATION_INPUT_VALIDATIONS, 1);
            result = ir_validate_canonical_module(program, module);
            result.boundary = IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
            validated = result.error == IR_VALIDATION_NONE;
        }
        if (result.error == IR_VALIDATION_NONE && !module->inline_complete)
        {
            bool inline_requested = program->inline_options.tiny;
            for (u32 symbol = 0; !inline_requested && symbol < program->symbols.count; symbol += 1)
            {
                inline_requested = program->symbols.symbols[symbol].always_inline;
            }
            if (inline_requested)
            {
                // Expansion checks each caller and callee it reads, and each
                // caller it rewrites, against the strict canonical form; a
                // legacy certified shape stays a call when optional and
                // reports its call when required. A module-wide scan here
                // would repeat the FAST input guard on every default compile.
                // Rewritten callers pass that check, so `validated` still
                // describes the module.
                module->inlining = (IrInlineStatistics){0};
                result = ir_inline_module(program, module);
            }
            module->inline_complete = result.error == IR_VALIDATION_NONE;
        }
        if (result.error == IR_VALIDATION_NONE && !program->disable_local_promotion && !module->local_promotion_complete)
        {
            module->local_promotion = (IrLocalPromotionStatistics){0};
            // Certified input has had no strict scan, so a checked build
            // records which functions already fail the validator before
            // promotion can rewrite them (only functions with LOCAL rows can
            // change). The output check leaves those out: promotion did not
            // cause their defects, and an unchecked build accepts them.
            TemporalArena scratch = scratch_begin(&program->arena, 1);
            u8* attribution = 0;
            if (input_certified && BUSTER_IR_TRANSFORM_CHECKS && module->function_count)
            {
                attribution = arena_allocate(scratch.arena, u8, module->function_count);
                memset(attribution, 0, module->function_count);
            }
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    // Function validation carries its own ownership proof, so
                    // one function attributes without a module-wide scan.
                    if (attribution && ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_LOCAL)) &&
                        ir_validate_canonical_function(program, function).error != IR_VALIDATION_NONE)
                    {
                        attribution[index] |= IR_PROMOTION_INPUT_INVALID;
                    }
                    IR_CONSTRUCTION_RECORD(PREPARATION_PROMOTION_FUNCTIONS, 1);
                    u64 promoted_before = module->local_promotion.promoted_locals;
                    ir_promote_function(program, function, &module->local_promotion);
                    if (attribution && module->local_promotion.promoted_locals != promoted_before)
                    {
                        attribution[index] |= IR_PROMOTION_CHANGED;
                    }
                }
            }
            if (module->local_promotion.promoted_locals)
            {
                // Mutation ends the input certificate's scope. The optimized
                // production fast path trusts this pass's own contract, not
                // the producer's certificate. Debug/test/sanitizer consumers
                // check the transformed rows before publication instead.
                bool checked = !input_certified || BUSTER_IR_TRANSFORM_CHECKS;
                u32 excluded = 0;
                if (checked)
                {
                    IR_CONSTRUCTION_RECORD(PREPARATION_PROMOTION_OUTPUT_VALIDATIONS, 1);
                    result = attribution ? ir_validate_promotion_output(program, module, attribution, &excluded)
                                         : ir_validate_canonical_module(program, module);
                    result.boundary = IR_VALIDATION_BOUNDARY_LOCAL_PROMOTION_OUTPUT;
                }
                validated = checked && result.error == IR_VALIDATION_NONE && !excluded;
            }
            scratch_end(scratch);
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
            if (!validated)
            {
                IR_CONSTRUCTION_RECORD(PREPARATION_FAST_INPUT_VALIDATIONS, 1);
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
                    if (function->state == IR_FUNCTION_LOWERED)
                    {
                        IR_CONSTRUCTION_RECORD(PREPARATION_FAST_FUNCTIONS, 1);
                        ir_fast_function(program, function, &module->fast);
                    }
                }
                u64 changes = 0;
                for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) changes += module->fast.passes[pass].changes;
                if (changes && (!input_certified || BUSTER_IR_TRANSFORM_CHECKS))
                {
                    IR_CONSTRUCTION_RECORD(PREPARATION_FAST_OUTPUT_VALIDATIONS, 1);
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
                IR_CONSTRUCTION_RECORD(PREPARATION_PUBLICATION_FUNCTIONS, 1);
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
