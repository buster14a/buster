// Compact canonical FAST pipeline, included by ir.c after ir_promote.c.
// ir_fast_function owns bounded scratch and the shared ir_rewrite_compact map.
// ir_instruction_is_pure is the semantic DCE authority. ir_fast_fold performs
// one forward pass and takes each scalar rule from ir_fast_fold_rule, which
// the diagnostic-only ir_construction_fold_census also asks at C-builder
// commit (#3010; ir_construction.fold_birth_*/fold_late_*). That probe tags
// the row, and ir_fast_fold_census_late/_entry/_exit match each late fold to
// the tag (fold_match_*, fold_size_*, fold_survival_*, fold_net_*); ir_fast_dce uses
// counts plus a deletion queue (no use CSR); ir_fast_parameters has a hard
// sweep cap. The opt-in ir_fast_cse (#3011) value-numbers admitted rows within
// each block through a bounded generation-tagged table and runs between address
// and DCE. ir_prepare_canonical_module owns
// input, inlining-output, promotion-output and FAST-output certification boundaries, then
// publishes the final canonical CFG after all selected transformations.
// ir_validate_promotion_output leaves out of the promotion-output check any
// function whose certified input already failed the strict validator.
// ir_validate_fast_output likewise excludes only bodies FAST left untouched.
#include <buster/lib/time.h>

String8 ir_fast_pass_name(IrFastPass pass)
{
    String8 names[] = {S8("fold"), S8("address"), S8("dce"), S8("parameters"), S8("cse")};
    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(names) == IR_FAST_PASS_COUNT);
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

// Function-size buckets for the matched census, by rows at FAST entry.
#define IR_FAST_FOLD_CENSUS_SMALL_ROWS 64u
#define IR_FAST_FOLD_CENSUS_MEDIUM_ROWS 512u
#define IR_FAST_FOLD_CENSUS_LARGE_ROWS 4096u

BUSTER_GLOBAL_LOCAL bool ir_fast_fold_census_late_folded(IrFunction const* function, u32 value)
{
    u32 definition = value != IR_PROMOTE_NONE ? function->values[value].definition.value : IR_PROMOTE_NONE;
    return definition < function->instruction_count && (function->instructions[definition].fold_census & IR_FOLD_CENSUS_LATE_FOLDED);
}

// Records one late fold by rule and matches it to the row's commit verdict:
// the same rule then (category 1), probed with no rule then (category 2,
// split by whether an operand was folded or replaced earlier in this scan),
// a different rule, an inliner clone, or a row never probed. Called before a
// constant rewrite clears the operands. Category 1 also records the logical
// storage a birth fold leaves unallocated and marks the literal operand rows
// so ir_fast_fold_census_exit can count the ones the fold leaves dead.
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_late(IrFunction* function, IrInstruction* row, IrFastFoldRule rule, u32 first, u32 second,
                                                  bool replaced_roots, bool identity)
{
    // The address pass reaches the identity path with no scalar rule.
    if (rule != IR_FAST_FOLD_RULE_NONE)
    {
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY + (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
        IR_CONSTRUCTION_RECORD(FOLD_LATE_REPLACED_ROOTS, replaced_roots);
        u32 verdict = row->fold_census & IR_FOLD_CENSUS_VERDICT;
        bool cascade = replaced_roots || ir_fast_fold_census_late_folded(function, first) || ir_fast_fold_census_late_folded(function, second);
        IrConstructionCounter match = (row->fold_census & IR_FOLD_CENSUS_INLINED) ? IR_CONSTRUCTION_FOLD_MATCH_INLINED
                                      : verdict == 0                              ? IR_CONSTRUCTION_FOLD_MATCH_UNPROBED
                                      : verdict == IR_FOLD_CENSUS_PROBED + rule   ? IR_CONSTRUCTION_FOLD_MATCH_BIRTH
                                      : verdict != IR_FOLD_CENSUS_PROBED          ? IR_CONSTRUCTION_FOLD_MATCH_OTHER_RULE
                                      : cascade                                   ? IR_CONSTRUCTION_FOLD_MATCH_LATER_CASCADE
                                                                                  : IR_CONSTRUCTION_FOLD_MATCH_LATER_OTHER;
        ir_construction_record(match, 1);
        u32 rows = function->instruction_count;
        u32 bucket = rows < IR_FAST_FOLD_CENSUS_SMALL_ROWS ? 0 : rows < IR_FAST_FOLD_CENSUS_MEDIUM_ROWS ? 1 : rows < IR_FAST_FOLD_CENSUS_LARGE_ROWS ? 2 : 3;
        if (match == IR_CONSTRUCTION_FOLD_MATCH_BIRTH)
        {
            ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_SMALL_BIRTH + bucket), 1);
            IrConstructionCounter shape = IR_CONSTRUCTION_FOLD_MATCH_BIRTH_OTHER;
            if (row->opcode == IR_OPCODE_CAST)
            {
                shape = row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET  ? IR_CONSTRUCTION_FOLD_MATCH_BIRTH_REINTERPRET
                        : row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ? IR_CONSTRUCTION_FOLD_MATCH_BIRTH_SIGN_EXTEND
                        : row->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ? IR_CONSTRUCTION_FOLD_MATCH_BIRTH_ZERO_EXTEND
                        : row->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE    ? IR_CONSTRUCTION_FOLD_MATCH_BIRTH_TRUNCATE
                                                                                         : shape;
            }
            else if (row->opcode == IR_OPCODE_BINARY && row->binary_operation == IR_BINARY_INTEGER_NOT_EQUAL)
            {
                shape = IR_CONSTRUCTION_FOLD_MATCH_BIRTH_NOT_EQUAL;
            }
            ir_construction_record(shape, 1);
            // An identity needs none of the row's storage; a constant still needs
            // its row and one immediate, which the late rewrite allocates anew
            // after the operands it no longer reads.
            u64 operand_bytes = (u64)row->operand_count * sizeof(IrValueId);
            u64 source_bytes = function->instruction_canonical_sources ? sizeof(IrSourceRange) : 0;
            IR_CONSTRUCTION_RECORD(FOLD_NET_ROW_BYTES, identity ? sizeof(IrInstruction) + sizeof(IrValue) + operand_bytes + source_bytes
                                                                : operand_bytes + sizeof(u64));
            for (u32 operand = 0; !identity && operand < row->operand_count; operand += 1)
            {
                u32 definition = function->values[row->operands[operand].value].definition.value;
                if (definition < function->instruction_count && function->instructions[definition].opcode == IR_OPCODE_CONSTANT_INTEGER)
                {
                    function->instructions[definition].fold_census |= IR_FOLD_CENSUS_BIRTH_LITERAL;
                }
            }
        }
        else if (match == IR_CONSTRUCTION_FOLD_MATCH_LATER_CASCADE || match == IR_CONSTRUCTION_FOLD_MATCH_LATER_OTHER)
        {
            ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_SMALL_LATER + bucket), 1);
        }
        row->fold_census |= IR_FOLD_CENSUS_LATE_FOLDED;
    }
}

// On FAST entry: clears the per-run marks and counts the birth-foldable,
// non-cloned rows that reach an admitted function or sit in a skipped one.
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_entry(IrFunction* function, bool admitted)
{
    u64 birth = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        row->fold_census &= (u8)~(IR_FOLD_CENSUS_LATE_FOLDED | IR_FOLD_CENSUS_BIRTH_LITERAL);
        birth += !(row->fold_census & IR_FOLD_CENSUS_INLINED) && (row->fold_census & IR_FOLD_CENSUS_VERDICT) > IR_FOLD_CENSUS_PROBED;
    }
    if (admitted) IR_CONSTRUCTION_RECORD(FOLD_SURVIVAL_REACHED, birth);
    else IR_CONSTRUCTION_RECORD(FOLD_SURVIVAL_SKIPPED, birth);
}

// After the passes and before compaction: birth-foldable rows FAST left
// unfolded, and literal rows that fed a category-1 constant fold and that
// the passes then deleted. Such a literal also had no other surviving
// reader, so a birth fold would not have built it either.
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_exit(IrFunction const* function, u8 const* removed)
{
    u64 source_bytes = function->instruction_canonical_sources ? sizeof(IrSourceRange) : 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction const* row = function->instructions + index;
        IR_CONSTRUCTION_RECORD(FOLD_SURVIVAL_UNFOLDED, !(row->fold_census & (IR_FOLD_CENSUS_INLINED | IR_FOLD_CENSUS_LATE_FOLDED)) &&
                                                           (row->fold_census & IR_FOLD_CENSUS_VERDICT) > IR_FOLD_CENSUS_PROBED);
        if (removed[index] && (row->fold_census & IR_FOLD_CENSUS_BIRTH_LITERAL) && row->opcode == IR_OPCODE_CONSTANT_INTEGER)
        {
            IR_CONSTRUCTION_RECORD(FOLD_NET_LITERAL_ROWS, 1);
            IR_CONSTRUCTION_RECORD(FOLD_NET_LITERAL_BYTES, sizeof(IrInstruction) + sizeof(IrValue) + (u64)row->immediate_count * sizeof(u64) + source_bytes);
        }
    }
}
#define IR_FAST_FOLD_CENSUS_LATE(function, row, rule, first, second, replaced_roots, identity) \
    ir_fast_fold_census_late(function, row, rule, first, second, replaced_roots, identity)
#define IR_FAST_FOLD_CENSUS_ENTRY(function, admitted) ir_fast_fold_census_entry(function, admitted)
#define IR_FAST_FOLD_CENSUS_EXIT(function, removed) ir_fast_fold_census_exit(function, removed)
#else
#define IR_FAST_FOLD_CENSUS_LATE(function, row, rule, first, second, replaced_roots, identity) ((void)0)
#define IR_FAST_FOLD_CENSUS_ENTRY(function, admitted) ((void)0)
#define IR_FAST_FOLD_CENSUS_EXIT(function, removed) ((void)0)
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
                IR_FAST_FOLD_CENSUS_LATE(function, row, rule, first, second, replaced_roots, true);
                replacements[row->result.value] = replacement;
                removed[index] = 1;
                statistics->changes += 1;
            }
        }
        else if (rule != IR_FAST_FOLD_RULE_NONE)
        {
            IR_SEMANTIC_RECORD(FAST_FOLD_ACCEPTED, 1);
            IR_FAST_FOLD_CENSUS_LATE(function, row, rule, first, second, replaced_roots, false);
            row->opcode = IR_OPCODE_CONSTANT_INTEGER;
            row->operands = 0;
            row->operand_count = 0;
            row->immediates = arena_allocate(program->arena, u64, 1);
            row->immediates[0] = bits;
            row->immediate_count = 1;
            row->immediate_is_negative = false;
            statistics->changes += 1;
        }
    }
}

#if BUSTER_BENCH_ALLOCATIONS
// The construction half of the #3010 census: asks ir_fast_fold_rule, with no
// replacement map, whether a row the C builder just committed would already
// fold from the operands committed before it, and leaves the verdict in the
// row's fold_census tag for ir_fast_fold_census_late. Probes change nothing
// else and do not count as semantic work. The bytes are logical row/value/operand storage
// a birth fold would not have filled (an identity needs none of them; a
// constant still needs its row and immediate), not arena calls or RSS.
void ir_construction_fold_census(IrProgram* program, IrFunction* function, IrInstructionId id)
{
    IrInstruction* row = id.value < function->instruction_count ? function->instructions + id.value : 0;
    // A committed row may be a copy of an earlier one; only this probe's
    // verdict describes it.
    if (row) row->fold_census = 0;
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
        row->fold_census = (u8)(IR_FOLD_CENSUS_PROBED + rule);
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

// Opt-in block-local value numbering (#3011). One forward walk of each
// block's linked instruction chain keys the admitted rows -- wrapping unsigned
// scalar add/subtract/multiply/and/or/xor whose operands and result share one
// canonical type, and single-limb constants of such a type -- by operation,
// type and resolved operand roots (or immediate bits), with commutative
// operands ordered. A later exact match records its result as a
// replacement of the earlier representative and is removed by the shared
// compaction. Entries carry the block's generation tag, so no state crosses a
// block boundary and no per-block clearing is needed. Within a block a keyed
// operand's root is final: it is defined earlier in the block (already
// decided) or in another block. A block walked later can still replace that
// operand, but a replacement only joins equal values, so an earlier hit stays
// sound and its entry has already expired. A full table or exhausted probe
// sequence declines insertion; it never changes a result. The census's
// linear arm (IrProgram.fast_cse_window) instead compares each candidate
// with the block's previous IR_FAST_CSE_WINDOW keyed candidates, held in a
// ring in the table's first slots. Every candidate takes the next slot,
// recording its representative after a hit, so the arm never saturates.
typedef struct IrFastCseEntry IrFastCseEntry;
struct IrFastCseEntry
{
    u32 left;
    u32 right;
    u32 type;
    u32 value;
    u32 ordinal;
    u32 generation;
    u32 operation;
};

// One table serves a module preparation. Each block takes a fresh generation;
// when the counter wraps, the table is cleared and generation 1 restarts.
typedef struct IrFastCseTable IrFastCseTable;
struct IrFastCseTable
{
    IrFastCseEntry* entries;
    u32 generation;
};

BUSTER_GLOBAL_LOCAL IrFastCseTable ir_fast_cse_table(Arena* arena, u32 generation, IrFastCseStatistics* census)
{
    IrFastCseTable result = {.entries = arena_allocate(arena, IrFastCseEntry, IR_FAST_CSE_SLOTS), .generation = generation};
    memset(result.entries, 0, sizeof(IrFastCseEntry) * IR_FAST_CSE_SLOTS);
    census->table_bytes += sizeof(IrFastCseEntry) * IR_FAST_CSE_SLOTS;
    return result;
}

// Key operation of a constant row; binary rows use their IrBinaryOperation.
#define IR_FAST_CSE_CONSTANT ((u32)IR_BINARY_COUNT + 1u)

// Returns the admitted key operation and sets its commutativity, or returns
// IR_BINARY_COUNT for a row outside the domain. Signed, Boolean, enum, wider
// than one limb, pointer, floating, place and volatile shapes are excluded.
BUSTER_GLOBAL_LOCAL u32 ir_fast_cse_operation(IrProgram* program, IrFunction* function, IrInstruction const* row, bool* commutative)
{
    u32 result = IR_BINARY_COUNT;
    if (!row->volatile_access && !row->target_count && row->result.value < function->value_count)
    {
        if (row->opcode == IR_OPCODE_CONSTANT_INTEGER && !row->operand_count && row->immediate_count == 1 && !row->immediate_is_negative)
        {
            *commutative = false;
            result = IR_FAST_CSE_CONSTANT;
        }
        else if (row->opcode == IR_OPCODE_BINARY && row->operand_count == 2 && !row->immediate_count)
        {
            switch (row->binary_operation)
            {
            case IR_BINARY_INTEGER_ADD:
            case IR_BINARY_INTEGER_MULTIPLY:
            case IR_BINARY_INTEGER_BITWISE_AND:
            case IR_BINARY_INTEGER_BITWISE_OR:
            case IR_BINARY_INTEGER_BITWISE_XOR:
                *commutative = true;
                result = row->binary_operation;
                break;
            case IR_BINARY_INTEGER_SUBTRACT:
                *commutative = false;
                result = row->binary_operation;
                break;
            default: break;
            }
        }
        IrType* type = result != IR_BINARY_COUNT ? ir_type_from_id(&program->types, row->canonical_type) : 0;
        IrValue const* value = function->values + row->result.value;
        if (!type || type->kind != IR_TYPE_INTEGER || type->is_signed || type->is_atomic || type->is_volatile ||
            !type->bit_width || type->bit_width > 64 || value->category != IR_VALUE_VALUE || value->is_volatile ||
            value->canonical_type.value != row->canonical_type.value) result = IR_BINARY_COUNT;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ir_fast_cse(IrProgram* program, IrFunction* function, u32* replacements, u8* removed, IrFastCseTable* cse,
                                     IrFastPassStatistics* statistics, IrFastCseStatistics* census)
{
    IrFastCseEntry* table = cse->entries;
    bool window = program->fast_cse_window;
    u32 mask = IR_FAST_CSE_SLOTS - 1;
    u32 limit = IR_FAST_CSE_SLOTS - IR_FAST_CSE_SLOTS / 4;
    for (u32 block = 0; block < function->block_count; block += 1)
    {
        // Slots tagged with any other generation are empty for this block.
        cse->generation += 1;
        if (!cse->generation)
        {
            memset(table, 0, sizeof(IrFastCseEntry) * IR_FAST_CSE_SLOTS);
            census->table_bytes += sizeof(IrFastCseEntry) * IR_FAST_CSE_SLOTS;
            cse->generation = 1;
        }
        u32 generation = cse->generation;
        u32 entries = 0;
        u32 ordinal = 0;
        census->blocks += 1;
        for (u32 index = function->blocks[block].first_instruction.value; index < function->instruction_count;
             index = function->instructions[index].next.value)
        {
            IrInstruction* row = function->instructions + index;
            bool commutative = false;
            statistics->visits += 1;
            u32 operation = removed[index] ? IR_BINARY_COUNT : ir_fast_cse_operation(program, function, row, &commutative);
            u32 type = row->canonical_type.value;
            u32 left = IR_PROMOTE_NONE, right = IR_PROMOTE_NONE;
            bool keyed = operation == IR_FAST_CSE_CONSTANT;
            if (keyed)
            {
                left = (u32)row->immediates[0];
                right = (u32)(row->immediates[0] >> 32);
            }
            else if (operation != IR_BINARY_COUNT)
            {
                left = ir_promote_root(replacements, row->operands[0].value);
                right = ir_promote_root(replacements, row->operands[1].value);
                keyed = left < function->value_count && right < function->value_count && function->values[left].canonical_type.value == type &&
                        function->values[right].canonical_type.value == type;
            }
            if (keyed)
            {
                bool commuted = commutative && right < left;
                if (commuted)
                {
                    u32 swap = left;
                    left = right;
                    right = swap;
                }
                ordinal += 1;
                census->candidates += 1;
                IrFastCseEntry* match = 0;
                IrFastCseEntry* empty = 0;
                if (window)
                {
                    // Linear arm: compare against the block's previous
                    // IR_FAST_CSE_WINDOW keyed candidates, newest first. Every
                    // candidate, hit or miss, then takes the oldest slot.
                    u32 previous = ordinal - 1;
                    u32 live = BUSTER_MIN(previous, IR_FAST_CSE_WINDOW);
                    for (u32 offset = 1; !match && offset <= live; offset += 1)
                    {
                        IrFastCseEntry* entry = table + ((previous - offset) % IR_FAST_CSE_WINDOW);
                        census->probes += 1;
                        if (entry->operation == operation && entry->type == type && entry->left == left && entry->right == right) match = entry;
                    }
                    empty = table + (previous % IR_FAST_CSE_WINDOW);
                }
                else
                {
                    u32 hash = left * 0x9E3779B1u ^ right * 0x85EBCA77u ^ type * 0xC2B2AE3Du ^ operation;
                    hash ^= hash >> 15;
                    for (u32 probe = 0; !match && !empty && probe < IR_FAST_CSE_PROBES; probe += 1)
                    {
                        IrFastCseEntry* entry = table + ((hash + probe) & mask);
                        census->probes += 1;
                        if (entry->generation != generation) empty = entry;
                        else if (entry->operation == operation && entry->type == type && entry->left == left && entry->right == right) match = entry;
                        else census->collisions += 1;
                    }
                }
                // The window's slot for this candidate keeps the value it now
                // stands for: the representative after a hit, itself otherwise.
                u32 representative = row->result.value;
                if (match)
                {
                    IrValue a = function->values[row->result.value];
                    IrValue b = function->values[match->value];
                    if (a.alignment == b.alignment && a.category == b.category && a.is_read_only == b.is_read_only &&
                        a.points_to_read_only == b.points_to_read_only && a.is_volatile == b.is_volatile)
                    {
                        representative = match->value;
                        replacements[row->result.value] = match->value;
                        removed[index] = 1;
                        statistics->changes += 1;
                        census->hits += 1;
                        census->window_hits += ordinal - match->ordinal <= IR_FAST_CSE_WINDOW;
                        census->commuted_hits += commuted;
                        census->constant_hits += operation == IR_FAST_CSE_CONSTANT;
                    }
                    else census->metadata_declines += 1;
                }
                if (window)
                {
                    *empty = (IrFastCseEntry){.left = left, .right = right, .type = type, .value = representative, .ordinal = ordinal,
                        .generation = generation, .operation = operation};
                }
                else if (!match && empty && entries < limit)
                {
                    *empty = (IrFastCseEntry){.left = left, .right = right, .type = type, .value = representative, .ordinal = ordinal,
                        .generation = generation, .operation = operation};
                    entries += 1;
                }
                else if (!match) census->saturation_declines += 1;
            }
        }
        census->max_block_candidates = BUSTER_MAX(census->max_block_candidates, ordinal);
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

BUSTER_GLOBAL_LOCAL void ir_fast_function(IrProgram* program, IrFunction* function, IrFastCseTable* cse, IrFastStatistics* statistics)
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
    bool budget = scratch_bytes > IR_FAST_SCRATCH_BUDGET || retained_bytes + reopening_bytes > IR_FAST_RETAINED_BUDGET || work > IR_FAST_WORK_BUDGET;
    IR_FAST_FOLD_CENSUS_ENTRY(function, !provenance && !budget);
    if (provenance) statistics->provenance_skips += 1;
    else if (budget) statistics->budget_skips += 1;
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
        // CSE consumes folded and address-collapsed roots; DCE then counts
        // uses of the surviving representatives.
        IrFastPass const order[] = {IR_FAST_FOLD, IR_FAST_ADDRESS, IR_FAST_CSE, IR_FAST_DCE, IR_FAST_PARAMETERS};
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(order) == IR_FAST_PASS_COUNT);
        for (u32 step = 0; step < IR_FAST_PASS_COUNT; step += 1)
        {
            IrFastPass pass = order[step];
            if (!(program->fast_passes & IR_FAST_PASS_BIT(pass))) continue;
            IrFastPassStatistics* measurement = statistics->passes + pass;
            u64 before = measurement->changes;
            TimeDataType start = program->measure_fast_passes ? timestamp_take() : (TimeDataType){0};
            if (pass == IR_FAST_FOLD || pass == IR_FAST_ADDRESS)
            {
                ir_fast_fold(program, function, replacements, removed, pass, measurement);
            }
            else if (pass == IR_FAST_CSE)
            {
                ir_fast_cse(program, function, replacements, removed, cse, measurement, &statistics->cse);
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
        IR_FAST_FOLD_CENSUS_EXIT(function, removed);
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
IrFastStatistics ir_test_fast_cse_function(IrProgram* program, IrFunction* function, u32 generation)
{
    IrFastStatistics result = {0};
    TemporalArena scratch = scratch_begin(&program->arena, 1);
    IrFastCseTable cse = ir_fast_cse_table(scratch.arena, generation, &result.cse);
    ir_fast_function(program, function, &cse, &result);
    scratch_end(scratch);
    return result;
}

IrFastStatistics ir_test_fast_function(IrProgram* program, IrFunction* function)
{
    IrFastStatistics result = {0};
    if (program->fast_passes & IR_FAST_PASS_BIT(IR_FAST_CSE)) result = ir_test_fast_cse_function(program, function, 0);
    else ir_fast_function(program, function, 0, &result);
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

// Only declined functions are excluded: the shared scope and every admitted
// function must still satisfy the strict output oracle after a FAST rewrite.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_fast_output(IrProgram* program, IrModule* module, u8 const* declined)
{
    IrValidationResult result = ir_validate_canonical_scope(program, module);
    for (u32 index = 0; result.error == IR_VALIDATION_NONE && index < module->function_count; index += 1)
    {
        IrFunction* function = module->functions + index;
        if (function->state == IR_FUNCTION_LOWERED && !declined[index])
        {
            result = ir_validate_canonical_function(program, function);
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
                // Expansion checks each caller and callee it reads against
                // the strict canonical form; a legacy certified shape stays a
                // call when optional and reports its call when required. A
                // module-wide scan here would repeat the FAST input guard on
                // every default compile. A rewritten caller with a mandatory
                // splice is checked at once. When FAST will run, one whose
                // splices were all optional is listed instead and proven by
                // the next module-wide scan below, which already covers its
                // rows (#48); otherwise it too is checked at once.
                module->inlining = (IrInlineStatistics){0};
                result = ir_inline_module(program, module, program->fast_passes != 0);
                if (module->inline_unchecked_count) validated = false;
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
            // A producer certificate preserves legacy acceptance, but optional
            // FAST needs strict validity. Check shared state once, then each
            // lowered body immediately before its function-local transforms.
            // Reuse a prior whole-module proof without any extra validation.
            TemporalArena scratch = scratch_begin(&program->arena, 1);
            IrValidationResult scope = ir_validation_ok();
            if (!validated)
            {
                IR_CONSTRUCTION_RECORD(PREPARATION_FAST_INPUT_VALIDATIONS, 1);
                scope = ir_validate_canonical_scope(program, module);
            }
            bool check_output = !input_certified || BUSTER_IR_TRANSFORM_CHECKS;
            u8* declined = 0;
            IrFastDecline* declines = 0;
            // The CSE table is allocated before, and outlives, each function's
            // own scratch region.
            IrFastCseTable cse = {0};
            if (program->fast_passes & IR_FAST_PASS_BIT(IR_FAST_CSE)) cse = ir_fast_cse_table(scratch.arena, 0, &module->fast.cse);
            // Callers whose optional splices skipped their own output check
            // (ir_inline_module) are proven here by the same per-function input
            // validation; one that fails is an inlining-output error, never a
            // decline. Other failures keep their legacy decline.
            u8* unchecked = 0;
            if (!validated && module->inline_unchecked_count)
            {
                unchecked = arena_allocate_zeroed(scratch.arena, u8, module->function_count);
                for (u32 listed = 0; listed < module->inline_unchecked_count; listed += 1)
                {
                    unchecked[module->inline_unchecked_callers[listed]] = 1;
                }
            }
            module->inline_unchecked_count = 0;
            for (u32 index = 0; index < module->function_count && result.error == IR_VALIDATION_NONE; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    IrValidationResult input = scope;
                    if (!validated && input.error == IR_VALIDATION_NONE)
                    {
                        input = ir_validate_canonical_function(program, function);
                    }
                    if (input.error != IR_VALIDATION_NONE && unchecked && unchecked[index])
                    {
                        // A shared-scope failure says nothing about the
                        // splice, so attribute only the caller's own rows.
                        IrValidationResult own = scope.error == IR_VALIDATION_NONE
                                                     ? input
                                                     : ir_validate_canonical_function(program, function);
                        if (own.error != IR_VALIDATION_NONE)
                        {
                            result = own;
                            result.boundary = IR_VALIDATION_BOUNDARY_INLINE_OUTPUT;
                        }
                    }
                    if (result.error == IR_VALIDATION_NONE && input.error != IR_VALIDATION_NONE)
                    {
                        if (!declines)
                        {
                            declines = arena_allocate(scratch.arena, IrFastDecline, module->function_count);
                            if (check_output)
                            {
                                declined = arena_allocate(scratch.arena, u8, module->function_count);
                                memset(declined, 0, module->function_count);
                            }
                        }
                        input.boundary = IR_VALIDATION_BOUNDARY_CANONICAL_INPUT;
                        declines[module->fast.decline_count++] = (IrFastDecline){.validation = input, .function_name = function->name};
                        if (declined) declined[index] = 1;
                        module->fast.validation_skips += 1;
                    }
                    else if (result.error == IR_VALIDATION_NONE)
                    {
                        IR_CONSTRUCTION_RECORD(PREPARATION_FAST_FUNCTIONS, 1);
                        ir_fast_function(program, function, &cse, &module->fast);
                    }
                }
            }
            u64 changes = 0;
            for (u32 pass = 0; pass < IR_FAST_PASS_COUNT; pass += 1) changes += module->fast.passes[pass].changes;
            if (result.error == IR_VALIDATION_NONE && changes && check_output)
            {
                IR_CONSTRUCTION_RECORD(PREPARATION_FAST_OUTPUT_VALIDATIONS, 1);
                result = declined ? ir_validate_fast_output(program, module, declined) : ir_validate_canonical_module(program, module);
                result.boundary = IR_VALIDATION_BOUNDARY_FAST_OUTPUT;
            }
            if (module->fast.decline_count)
            {
                module->fast.declines = arena_allocate(program->arena, IrFastDecline, module->fast.decline_count);
                memcpy(module->fast.declines, declines, sizeof(*declines) * module->fast.decline_count);
            }
            scratch_end(scratch);
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
