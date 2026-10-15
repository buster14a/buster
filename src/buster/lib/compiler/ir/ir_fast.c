// Compact canonical FAST pipeline, included by ir.c after ir_promote.c.
// ir_fast_function owns bounded scratch and the shared ir_rewrite_compact map.
// ir_instruction_is_pure is the semantic DCE authority. ir_fast_fold performs
// one forward pass and takes each scalar rule from ir_fast_fold_rule, which
// the diagnostic-only ir_construction_fold_census also asks at C-builder
// commit (#3010; ir_construction.fold_birth_*/fold_late_*), and
// ir_construction_fold_census_begin/_hold/_end at the end of a C body
// (fold_net_*, fold_form_*, fold_size_*). The commit census also tags the
// row (IrInstruction.fold_birth, IR_FAST_BIRTH_*): ir_fast_fold_census_late
// matches each late fold against the tag (fold_match_direct/cascade/post/
// clone_*), while ir_fast_fold_census_entry and _exit follow birth folds into
// FAST (fold_survival_*). ir_fast_fold_candidate asks the same rule of a row C
// lowering has not committed (-fcanonical-construction-fold); ir_fast_dce uses
// counts plus a deletion queue (no use CSR); ir_fast_parameters has a hard
// sweep cap. The opt-in ir_fast_cse (#3011) value-numbers admitted rows within
// each block through a bounded generation-tagged table and runs between address
// and DCE. ir_prepare_canonical_module owns
// input, inlining-output, promotion-output and FAST-output certification boundaries, then
// publishes the final canonical CFG after all selected transformations.
// ir_validate_promotion_output leaves out of the promotion-output check any
// function whose certified input already failed the strict validator, retaining
// successful per-function proofs until FAST admission in the same preparation.
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
// Every per-rule counter family follows IrFastFoldRule.
#define IR_FAST_FOLD_RULE_RANGE_CHECK(family) \
    BUSTER_CT_CHECK(IR_CONSTRUCTION_##family##_BINARY_LEFT_IDENTITY - IR_CONSTRUCTION_##family##_CAST_IDENTITY == \
                    IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY - IR_FAST_FOLD_RULE_CAST_IDENTITY)
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_BIRTH);
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_LATE);
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_BIRTH_CASCADE);
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_MATCH_DIRECT);
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_MATCH_CASCADE);
IR_FAST_FOLD_RULE_RANGE_CHECK(FOLD_MATCH_POST);
#undef IR_FAST_FOLD_RULE_RANGE_CHECK

// IrInstruction.fold_birth (IR_FAST_BIRTH_* in ir.h) is written once by
// ir_construction_fold_census when the C builder commits a CAST, UNARY or
// BINARY row. A zero tag is a row the builder never committed: SSA
// insertion, promotion and other post-construction producers start from a
// zeroed row. Copies keep the tag; the inliner adds CLONE but keeps the
// callee's verdict, so a clone foldable from its callee's construction facts
// still matches while one an argument enabled does not.
BUSTER_CT_CHECK(IR_FAST_FOLD_RULE_COUNT - 1 <= IR_FAST_BIRTH_RULE_MASK);

// Classifies one fold ir_fast_fold applied against the birth tag of its row:
// DIRECT when the same rule already folded the committed row, CASCADE when it
// did so through earlier birth folds, otherwise POST (a fact that construction
// did not have: SSA, promotion, inlining or a root the late scan supplied).
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_late(IrFastFoldRule rule, bool replaced_roots, IrInstruction* row)
{
    if (rule != IR_FAST_FOLD_RULE_NONE)
    {
        u32 birth = row->fold_birth;
        bool clone = (birth & IR_FAST_BIRTH_CLONE) != 0;
        u32 birth_rule = birth & IR_FAST_BIRTH_RULE_MASK;
        bool probed = (birth & IR_FAST_BIRTH_PROBED) != 0;
        bool matched = probed && birth_rule == (u32)rule;
        IrConstructionCounter family = !matched ? IR_CONSTRUCTION_FOLD_MATCH_POST_CAST_IDENTITY
                                       : (birth & IR_FAST_BIRTH_CASCADE) ? IR_CONSTRUCTION_FOLD_MATCH_CASCADE_CAST_IDENTITY
                                                                         : IR_CONSTRUCTION_FOLD_MATCH_DIRECT_CAST_IDENTITY;
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_LATE_CAST_IDENTITY + (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
        ir_construction_record((IrConstructionCounter)(family + (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
        IR_CONSTRUCTION_RECORD(FOLD_LATE_REPLACED_ROOTS, replaced_roots);
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_POST_UNPROBED, !probed);
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_POST_UNFOLDABLE_AT_BIRTH, probed && birth_rule == IR_FAST_FOLD_RULE_NONE);
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_POST_RULE_CHANGED, probed && birth_rule != IR_FAST_FOLD_RULE_NONE && !matched);
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_CLONE_DIRECT, clone && matched && !(birth & IR_FAST_BIRTH_CASCADE));
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_CLONE_CASCADE, clone && matched && (birth & IR_FAST_BIRTH_CASCADE));
        IR_CONSTRUCTION_RECORD(FOLD_MATCH_CLONE_POST, clone && !matched);
        row->fold_birth = (u8)(birth | IR_FAST_BIRTH_LATE_FOLDED);
    }
}

BUSTER_GLOBAL_LOCAL bool ir_fast_birth_foldable(IrInstruction const* row)
{
    return (row->fold_birth & (IR_FAST_BIRTH_PROBED | IR_FAST_BIRTH_CLONE)) == IR_FAST_BIRTH_PROBED &&
           (row->fold_birth & IR_FAST_BIRTH_RULE_MASK) != IR_FAST_FOLD_RULE_NONE;
}

// On ir_fast_function entry: the committed, non-clone birth-foldable rows of
// an admitted function (fold_survival_reached), whose per-run LATE_FOLDED
// marks are cleared, or of a skipped one (fold_survival_skipped). A budget
// probe's rows may not be backed, so rows are read only when they exist.
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_entry(IrFunction* function, bool admitted)
{
    u64 births = 0;
    for (u32 index = 0; function->instructions && index < function->instruction_count; index += 1)
    {
        IrInstruction* row = function->instructions + index;
        row->fold_birth = admitted ? (u8)(row->fold_birth & ~IR_FAST_BIRTH_LATE_FOLDED) : row->fold_birth;
        births += ir_fast_birth_foldable(row);
    }
    ir_construction_record(admitted ? IR_CONSTRUCTION_FOLD_SURVIVAL_REACHED : IR_CONSTRUCTION_FOLD_SURVIVAL_SKIPPED, births);
}

// After the selected passes and before compaction: reached birth-foldable
// rows that the fold pass did not fold in this run (fold_survival_unfolded).
BUSTER_GLOBAL_LOCAL void ir_fast_fold_census_exit(IrFunction const* function)
{
    u64 unfolded = 0;
    for (u32 index = 0; index < function->instruction_count; index += 1)
    {
        IrInstruction const* row = function->instructions + index;
        unfolded += ir_fast_birth_foldable(row) && !(row->fold_birth & IR_FAST_BIRTH_LATE_FOLDED);
    }
    IR_CONSTRUCTION_RECORD(FOLD_SURVIVAL_UNFOLDED, unfolded);
}
#define IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots, row) ir_fast_fold_census_late(rule, replaced_roots, row)
#define IR_FAST_FOLD_CENSUS_ENTRY(function, admitted) ir_fast_fold_census_entry(function, admitted)
#define IR_FAST_FOLD_CENSUS_EXIT(function) ir_fast_fold_census_exit(function)
#else
#define IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots, row) ((void)0)
#define IR_FAST_FOLD_CENSUS_ENTRY(function, admitted) ((void)0)
#define IR_FAST_FOLD_CENSUS_EXIT(function) ((void)0)
#endif

// The operands one scalar rule reads: the values `first` and `second` they
// resolve to (IR_PROMOTE_NONE when absent) and, for each that is a known
// constant, its bits. FAST resolves them through its replacement map and
// CONSTANT_INTEGER rows; the construction census also through birth folds.
typedef struct IrFastFoldOperands IrFastFoldOperands;
struct IrFastFoldOperands
{
    u64 left;
    u64 right;
    u32 first;
    u32 second;
    bool left_constant;
    bool right_constant;
};

// The scalar rule ir_fast_fold applies to one CAST, UNARY or BINARY row whose
// operands resolve as `operands` says. An identity names the existing value
// in *replacement; a constant leaves the folded immediate in *bits. The
// caller still owns the replacement metadata check, so the diagnostic
// construction census can ask the same question of a row at commit.
BUSTER_GLOBAL_LOCAL IrFastFoldRule ir_fast_fold_rule(IrProgram* program, IrFunction* function, IrInstruction const* row,
                                                     IrFastFoldOperands const* operands, u32* replacement, u64* bits)
{
    IrFastFoldRule rule = IR_FAST_FOLD_RULE_NONE;
    u32 width = ir_fast_width(program, row->canonical_type);
    u64 folded_bits = 0;
    bool constant = false;
    u32 first = operands->first;
    u32 second = operands->second;
    u64 left = operands->left;
    u64 right = operands->right;
    bool left_constant = operands->left_constant;
    bool right_constant = operands->right_constant;
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

bool ir_fast_fold_candidate(IrProgram* program, IrFunction* function, IrInstruction const* candidate, u64 const* right_bits, u64* bits)
{
    bool result = false;
    if (candidate->operands && candidate->operand_count && candidate->operand_count <= 2 && candidate->operands[0].value < function->value_count &&
        (candidate->operand_count < 2 || candidate->operands[1].value < function->value_count) &&
        (candidate->opcode == IR_OPCODE_CAST || candidate->opcode == IR_OPCODE_BINARY))
    {
        u32 replacement = IR_PROMOTE_NONE;
        u64 folded = 0;
        IrFastFoldOperands operands = {
            .first = candidate->operands[0].value,
            .second = candidate->operand_count > 1 ? candidate->operands[1].value : IR_PROMOTE_NONE,
        };
        operands.left_constant = ir_fast_constant(program, function, operands.first, &operands.left);
        operands.right_constant = operands.second != IR_PROMOTE_NONE &&
                                  (right_bits || ir_fast_constant(program, function, operands.second, &operands.right));
        operands.right = right_bits ? *right_bits : operands.right;
        IrFastFoldRule rule = ir_fast_fold_rule(program, function, candidate, &operands, &replacement, &folded);
        result = rule == IR_FAST_FOLD_RULE_CAST_CONSTANT || rule == IR_FAST_FOLD_RULE_UNARY_CONSTANT || rule == IR_FAST_FOLD_RULE_BINARY_CONSTANT;
        *bits = result ? folded : *bits;
    }
    return result;
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
            IrFastFoldOperands operands = {.first = first, .second = second};
            operands.left_constant = first != IR_PROMOTE_NONE && ir_fast_constant(program, function, first, &operands.left);
            operands.right_constant = second != IR_PROMOTE_NONE && ir_fast_constant(program, function, second, &operands.right);
            rule = ir_fast_fold_rule(program, function, row, &operands, &replacement, &bits);
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
                IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots, row);
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
            IR_FAST_FOLD_CENSUS_LATE(rule, replaced_roots, row);
        }
    }
}

#if BUSTER_BENCH_ALLOCATIONS
// Folded bits of the most recent birth-constant rows, one direct-mapped slot
// per value index. A cascade reads a slot only when the defining row's tag
// says its rule folded to a constant, and that tag and slot were written by
// the same commit, so a matching (function, value) key is that row's result.
// An overwritten slot is a miss (fold_birth_cascade_uncached) rather than a
// guess. Thread-local and static: probes allocate nothing, so allocation.*
// counters are untouched.
#define IR_FAST_BIRTH_CONSTANT_CAPACITY 1024u
typedef struct IrFastBirthConstant IrFastBirthConstant;
struct IrFastBirthConstant
{
    IrFunction const* function;
    u64 bits;
    u32 value;
};
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL IrFastBirthConstant ir_fast_birth_constants[IR_FAST_BIRTH_CONSTANT_CAPACITY];

BUSTER_GLOBAL_LOCAL bool ir_fast_birth_rule_is_constant(u32 rule)
{
    return rule == IR_FAST_FOLD_RULE_CAST_CONSTANT || rule == IR_FAST_FOLD_RULE_UNARY_CONSTANT || rule == IR_FAST_FOLD_RULE_BINARY_CONSTANT;
}

// The value `value` stands for once every identity birth fold before it is
// applied: the birth-time counterpart of ir_promote_root. Committed operands
// precede their row, so the walk only moves to earlier rows; the step bound
// covers a retracted and reused definition.
BUSTER_GLOBAL_LOCAL u32 ir_fast_birth_root(IrFunction const* function, u32 value)
{
    u32 result = value;
    bool following = value < function->value_count;
    for (u32 steps = 0; following && steps < function->instruction_count; steps += 1)
    {
        u32 definition = function->values[result].definition.value;
        IrInstruction const* row = definition < function->instruction_count ? function->instructions + definition : 0;
        u32 rule = row ? row->fold_birth & IR_FAST_BIRTH_RULE_MASK : IR_FAST_FOLD_RULE_NONE;
        u32 operand = rule == IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY ? 1 : 0;
        following = row && row->operands && operand < row->operand_count &&
                    (rule == IR_FAST_FOLD_RULE_CAST_IDENTITY || rule == IR_FAST_FOLD_RULE_BINARY_RIGHT_IDENTITY ||
                     rule == IR_FAST_FOLD_RULE_BINARY_LEFT_IDENTITY) &&
                    row->operands[operand].value < function->value_count;
        result = following ? row->operands[operand].value : result;
    }
    return result;
}

// Whether `value` is a constant once earlier birth folds are applied: a
// CONSTANT_INTEGER row as FAST sees it, or a row a birth fold made constant.
BUSTER_GLOBAL_LOCAL bool ir_fast_birth_constant(IrProgram* program, IrFunction* function, u32 value, u64* bits, bool* uncached)
{
    bool result = ir_fast_constant(program, function, value, bits);
    u32 definition = function->values[value].definition.value;
    if (!result && definition < function->instruction_count &&
        ir_fast_birth_rule_is_constant(function->instructions[definition].fold_birth & IR_FAST_BIRTH_RULE_MASK))
    {
        IrFastBirthConstant const* slot = ir_fast_birth_constants + value % IR_FAST_BIRTH_CONSTANT_CAPACITY;
        result = slot->function == function && slot->value == value;
        *bits = result ? slot->bits : *bits;
        *uncached = *uncached || !result;
    }
    return result;
}

// The construction half of the #3010 census, run on every row the C builder
// commits. It asks ir_fast_fold_rule, with no replacement map, whether the row
// already folds from the operands committed before it (fold_birth_<rule>); if
// not, whether it folds once earlier birth folds are applied
// (fold_birth_cascade_<rule>). The answer is stored in the row's fold_birth
// tag, which ir_fast_fold reads to classify each late fold. Probes change no
// other field and do not count as semantic work. The bytes are logical
// row/value/operand storage a direct birth fold would not have filled (an
// identity needs none of them; a constant still needs its row and immediate),
// not arena calls or RSS.
void ir_construction_fold_census(IrProgram* program, IrFunction* function, IrInstructionId id)
{
    IrInstruction* row = id.value < function->instruction_count ? function->instructions + id.value : 0;
    if (row && row->result.value < function->value_count && row->operand_count && row->operands &&
        row->operands[0].value < function->value_count && (row->operand_count < 2 || row->operands[1].value < function->value_count) &&
        (row->opcode == IR_OPCODE_CAST || row->opcode == IR_OPCODE_UNARY || row->opcode == IR_OPCODE_BINARY))
    {
        u32 replacement = IR_PROMOTE_NONE;
        u64 bits = 0;
        u32 tag = IR_FAST_BIRTH_PROBED;
        bool suppressed = ir_semantic_suppress(true);
        IrFastFoldOperands committed = {
            .first = row->operands[0].value,
            .second = row->operand_count > 1 ? row->operands[1].value : IR_PROMOTE_NONE,
        };
        committed.left_constant = ir_fast_constant(program, function, committed.first, &committed.left);
        committed.right_constant = committed.second != IR_PROMOTE_NONE && ir_fast_constant(program, function, committed.second, &committed.right);
        IrFastFoldRule rule = ir_fast_fold_rule(program, function, row, &committed, &replacement, &bits);
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
        else
        {
            // Only a changed root or a newly known constant can change the
            // rule's answer, so an unchanged view is not asked twice.
            bool uncached = false;
            IrFastFoldOperands cascade = {
                .first = ir_fast_birth_root(function, committed.first),
                .second = committed.second != IR_PROMOTE_NONE ? ir_fast_birth_root(function, committed.second) : IR_PROMOTE_NONE,
            };
            cascade.left_constant = ir_fast_birth_constant(program, function, cascade.first, &cascade.left, &uncached);
            cascade.right_constant = cascade.second != IR_PROMOTE_NONE &&
                                     ir_fast_birth_constant(program, function, cascade.second, &cascade.right, &uncached);
            if (cascade.first != committed.first || cascade.second != committed.second || cascade.left_constant != committed.left_constant ||
                cascade.right_constant != committed.right_constant)
            {
                replacement = IR_PROMOTE_NONE;
                rule = ir_fast_fold_rule(program, function, row, &cascade, &replacement, &bits);
                if (replacement != IR_PROMOTE_NONE && !ir_fast_replacement_compatible(function, row->result.value, replacement))
                {
                    rule = IR_FAST_FOLD_RULE_NONE;
                }
                if (rule != IR_FAST_FOLD_RULE_NONE)
                {
                    tag |= IR_FAST_BIRTH_CASCADE;
                    ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_BIRTH_CASCADE_CAST_IDENTITY +
                                                                   (rule - IR_FAST_FOLD_RULE_CAST_IDENTITY)), 1);
                }
            }
            IR_CONSTRUCTION_RECORD(FOLD_BIRTH_CASCADE_UNCACHED, uncached && rule == IR_FAST_FOLD_RULE_NONE);
        }
        if (ir_fast_birth_rule_is_constant(rule))
        {
            // Cache the bits a later reader decodes from the CONSTANT_INTEGER
            // row ir_fast_fold writes, decoded the same way.
            u64 immediate = bits;
            IrInstruction folded = {.immediates = &immediate, .immediate_count = 1, .opcode = IR_OPCODE_CONSTANT_INTEGER};
            IrInteger decoded = {0};
            u32 width = ir_fast_width(program, function->values[row->result.value].canonical_type);
            bool known = width && ir_integer_constant_decode(&folded, width, &decoded);
            // An undecodable result clears the slot, so no older entry for
            // this value can answer for it.
            ir_fast_birth_constants[row->result.value % IR_FAST_BIRTH_CONSTANT_CAPACITY] =
                (IrFastBirthConstant){.function = known ? function : 0, .bits = decoded.low, .value = row->result.value};
        }
        ir_semantic_suppress(suppressed);
        row->fold_birth = (u8)(tag | (u32)rule);
    }
}

// The end-of-body half of the census, taken where the C builder has committed
// a function's rows but SSA finish has not yet renumbered values or forwarded
// stores: the rows are those the birth probe saw, less retracted ones. It
// counts objects, never bytes times a presumed per-row size. A birth fold
// leaves no operand slice for its row; an identity also needs no row or value;
// a literal whose every reader is a folded row needs no row, value or
// immediate. FAST's own work for these rows is not matched here: promotion
// renumbers rows first, so fold_late_* and fold_late_dce_constants only bound
// it (#3010; docs/canonical-fast-pipeline.md).
typedef enum IrFoldCensusShape
{
    IR_FOLD_CENSUS_SHAPE_REINTERPRET,
    IR_FOLD_CENSUS_SHAPE_SIGN_EXTEND,
    IR_FOLD_CENSUS_SHAPE_ZERO_EXTEND,
    IR_FOLD_CENSUS_SHAPE_TRUNCATE,
    IR_FOLD_CENSUS_SHAPE_BOOL_COMPARE,
    IR_FOLD_CENSUS_SHAPE_OTHER,
    IR_FOLD_CENSUS_SHAPE_COUNT,
} IrFoldCensusShape;

typedef enum IrFoldCensusConsumer
{
    IR_FOLD_CENSUS_CONSUMER_STORE,
    IR_FOLD_CENSUS_CONSUMER_AGGREGATE,
    IR_FOLD_CENSUS_CONSUMER_RETURN,
    IR_FOLD_CENSUS_CONSUMER_CALL,
    IR_FOLD_CENSUS_CONSUMER_BRANCH,
    IR_FOLD_CENSUS_CONSUMER_OPERATOR,
    IR_FOLD_CENSUS_CONSUMER_OTHER,
    IR_FOLD_CENSUS_CONSUMER_UNUSED,
    IR_FOLD_CENSUS_CONSUMER_COUNT,
} IrFoldCensusConsumer;

BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_FORM_OTHER_UNUSED - IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_STORE + 1 ==
                (u32)IR_FOLD_CENSUS_SHAPE_COUNT * (u32)IR_FOLD_CENSUS_CONSUMER_COUNT);
BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_FORM_TRUNCATE_STORE - IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_STORE ==
                (u32)IR_FOLD_CENSUS_SHAPE_TRUNCATE * (u32)IR_FOLD_CENSUS_CONSUMER_COUNT);
BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_BRANCH - IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_STORE == (u32)IR_FOLD_CENSUS_CONSUMER_BRANCH);

// Upper row bounds of the size buckets; the last bucket is unbounded.
BUSTER_GLOBAL_LOCAL u32 const ir_fold_census_size_limits[] = {64, 256, 1024, 4096};
#define IR_FOLD_CENSUS_SIZE_FIELDS (IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_256_FUNCTIONS - IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_FUNCTIONS)
BUSTER_CT_CHECK(IR_CONSTRUCTION_FOLD_SIZE_ROWS_GE_4096_DEAD_LITERALS - IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_FUNCTIONS + 1 ==
                IR_FOLD_CENSUS_SIZE_FIELDS * (BUSTER_ARRAY_LENGTH(ir_fold_census_size_limits) + 1));

// Per-value marks: the low bits hold a folded row's shape plus one.
enum
{
    IR_FOLD_CENSUS_MARK_SHAPE = 0x07,
    IR_FOLD_CENSUS_MARK_CONSUMED = 0x08,
    IR_FOLD_CENSUS_MARK_HELD = 0x10,
    IR_FOLD_CENSUS_MARK_DEAD = 0x20,
};

// Scratch lives in its own thread-local arena and is carved with the raw arena
// primitive, so allocation.* request counts and every other arena's dirty
// prefix stay exactly as without the census. The arena is reserved once per
// thread, reused per function and never released before process exit.
// A function with more values than fit the default 256 MiB reservation at
// five bytes each is skipped and counted rather than refused.
#define IR_FOLD_CENSUS_VALUE_LIMIT ((u32)1 << 25)
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL Arena* ir_fold_census_arena;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL u64 ir_fold_census_arena_start;

IrFoldCensus ir_construction_fold_census_begin(IrFunction* function)
{
    IrFoldCensus census = {0};
    if (!ir_fold_census_arena)
    {
        ir_fold_census_arena = arena_create((ArenaCreation){0});
        ir_fold_census_arena_start = ir_fold_census_arena->position;
    }
    arena_set_position(ir_fold_census_arena, ir_fold_census_arena_start);
    if (function && function->value_count && function->value_count <= IR_FOLD_CENSUS_VALUE_LIMIT)
    {
        census.value_count = function->value_count;
        census.uses = (u32*)arena_benchmark_allocate_zeroed_bytes_raw(ir_fold_census_arena, arena_array_size(sizeof(u32), census.value_count),
                                                                      BUSTER_ALIGN_OF(u32));
        census.marks = (u8*)arena_benchmark_allocate_zeroed_bytes_raw(ir_fold_census_arena, census.value_count, 1);
    }
    IR_CONSTRUCTION_RECORD(FOLD_NET_SKIPPED_FUNCTIONS, function && function->value_count > IR_FOLD_CENSUS_VALUE_LIMIT);
    return census;
}

void ir_construction_fold_census_hold(IrFoldCensus* census, IrValueId value)
{
    if (value.value < census->value_count)
    {
        census->uses[value.value] += 1;
        census->marks[value.value] |= IR_FOLD_CENSUS_MARK_HELD;
        IR_CONSTRUCTION_RECORD(FOLD_NET_SSA_USES, 1);
    }
}

BUSTER_GLOBAL_LOCAL IrFoldCensusConsumer ir_fold_census_consumer(u8 opcode)
{
    IrFoldCensusConsumer consumer;
    switch (opcode)
    {
    case IR_OPCODE_STORE:
    case IR_OPCODE_ATOMIC_STORE: consumer = IR_FOLD_CENSUS_CONSUMER_STORE; break;
    case IR_OPCODE_ARRAY:
    case IR_OPCODE_AGGREGATE: consumer = IR_FOLD_CENSUS_CONSUMER_AGGREGATE; break;
    case IR_OPCODE_RETURN: consumer = IR_FOLD_CENSUS_CONSUMER_RETURN; break;
    case IR_OPCODE_CALL: consumer = IR_FOLD_CENSUS_CONSUMER_CALL; break;
    case IR_OPCODE_BRANCH_IF:
    case IR_OPCODE_SWITCH: consumer = IR_FOLD_CENSUS_CONSUMER_BRANCH; break;
    case IR_OPCODE_CAST:
    case IR_OPCODE_UNARY:
    case IR_OPCODE_BINARY: consumer = IR_FOLD_CENSUS_CONSUMER_OPERATOR; break;
    default: consumer = IR_FOLD_CENSUS_CONSUMER_OTHER; break;
    }
    return consumer;
}

BUSTER_GLOBAL_LOCAL bool ir_fold_census_literal(IrFunction* function, u32 value)
{
    u32 definition = function->values[value].definition.value;
    return definition < function->instruction_count && function->instructions[definition].opcode == IR_OPCODE_CONSTANT_INTEGER;
}

// The birth rule of row `index` from the operands as committed, or NONE when
// an operand's definition does not precede the row, as at commit.
BUSTER_GLOBAL_LOCAL IrFastFoldRule ir_fold_census_row_rule(IrProgram* program, IrFunction* function, u32 index, u32* replacement)
{
    IrInstruction const* row = function->instructions + index;
    IrFastFoldRule rule = IR_FAST_FOLD_RULE_NONE;
    if (row->result.value < function->value_count && row->operand_count && row->operands &&
        (row->opcode == IR_OPCODE_CAST || row->opcode == IR_OPCODE_UNARY || row->opcode == IR_OPCODE_BINARY))
    {
        u64 bits = 0;
        IrFastFoldOperands committed = {
            .first = row->operands[0].value,
            .second = row->operand_count > 1 ? row->operands[1].value : IR_PROMOTE_NONE,
        };
        bool suppressed = ir_semantic_suppress(true);
        committed.left_constant = committed.first != IR_PROMOTE_NONE && ir_fast_constant(program, function, committed.first, &committed.left);
        committed.right_constant = committed.second != IR_PROMOTE_NONE && ir_fast_constant(program, function, committed.second, &committed.right);
        rule = ir_fast_fold_rule(program, function, row, &committed, replacement, &bits);
        ir_semantic_suppress(suppressed);
        if (*replacement != IR_PROMOTE_NONE && !ir_fast_replacement_compatible(function, row->result.value, *replacement))
        {
            rule = IR_FAST_FOLD_RULE_NONE;
        }
        for (u32 operand = 0; rule != IR_FAST_FOLD_RULE_NONE && operand < row->operand_count; operand += 1)
        {
            u32 definition = function->values[row->operands[operand].value].definition.value;
            rule = definition < index || definition >= function->instruction_count ? rule : IR_FAST_FOLD_RULE_NONE;
        }
    }
    return rule;
}

BUSTER_GLOBAL_LOCAL IrFoldCensusShape ir_fold_census_shape(IrProgram* program, IrFunction* function, IrInstruction const* row, IrFastFoldRule rule)
{
    IrFoldCensusShape shape = IR_FOLD_CENSUS_SHAPE_OTHER;
    if (rule == IR_FAST_FOLD_RULE_CAST_CONSTANT)
    {
        shape = row->conversion_operation == IR_CONVERSION_INTEGER_REINTERPRET   ? IR_FOLD_CENSUS_SHAPE_REINTERPRET
                : row->conversion_operation == IR_CONVERSION_INTEGER_SIGN_EXTEND ? IR_FOLD_CENSUS_SHAPE_SIGN_EXTEND
                : row->conversion_operation == IR_CONVERSION_INTEGER_ZERO_EXTEND ? IR_FOLD_CENSUS_SHAPE_ZERO_EXTEND
                : row->conversion_operation == IR_CONVERSION_INTEGER_TRUNCATE    ? IR_FOLD_CENSUS_SHAPE_TRUNCATE
                                                                                  : shape;
    }
    else if (rule == IR_FAST_FOLD_RULE_BINARY_CONSTANT)
    {
        IrType* type = ir_type_from_id(&program->types, function->values[row->result.value].canonical_type);
        shape = type && type->kind == IR_TYPE_BOOLEAN ? IR_FOLD_CENSUS_SHAPE_BOOL_COMPARE : shape;
    }
    return shape;
}

void ir_construction_fold_census_end(IrProgram* program, IrFunction* function, IrFoldCensus* census)
{
    if (census->value_count == function->value_count)
    {
        u32* uses = census->uses;
        u8* marks = census->marks;
        u64 folds = 0;
        u64 dead = 0;
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction const* row = function->instructions + index;
            for (u32 operand = 0; operand < row->operand_count; operand += 1)
            {
                u32 value = row->operands[operand].value;
                if (value < census->value_count)
                {
                    uses[value] += 1;
                }
            }
        }
        for (u32 block = 0; block < function->block_count; block += 1)
        {
            for (IrBlockParameter* parameter = function->blocks[block].first_parameter; parameter; parameter = parameter->next)
            {
                for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
                {
                    if (incoming->value.value < census->value_count)
                    {
                        uses[incoming->value.value] += 1;
                    }
                }
            }
        }
        // Folded rows: mark each result with its shape and drop the reads a
        // folded row makes of literals it does not forward.
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction const* row = function->instructions + index;
            u32 replacement = IR_PROMOTE_NONE;
            IrFastFoldRule rule = ir_fold_census_row_rule(program, function, index, &replacement);
            IR_CONSTRUCTION_RECORD(FOLD_NET_SCALAR_ROWS, row->opcode == IR_OPCODE_CAST || row->opcode == IR_OPCODE_UNARY ||
                                                             row->opcode == IR_OPCODE_BINARY);
            if (rule != IR_FAST_FOLD_RULE_NONE)
            {
                folds += 1;
                marks[row->result.value] |= (u8)(ir_fold_census_shape(program, function, row, rule) + 1);
                IR_CONSTRUCTION_RECORD(FOLD_NET_IDENTITY_ROWS, replacement != IR_PROMOTE_NONE);
                IR_CONSTRUCTION_RECORD(FOLD_NET_CONSTANT_ROWS, replacement == IR_PROMOTE_NONE);
                IR_CONSTRUCTION_RECORD(FOLD_NET_OPERAND_SLOTS, row->operand_count);
                for (u32 operand = 0; operand < row->operand_count; operand += 1)
                {
                    u32 value = row->operands[operand].value;
                    if (value != replacement && ir_fold_census_literal(function, value))
                    {
                        BUSTER_CHECK(uses[value] != 0);
                        uses[value] -= 1;
                        IR_CONSTRUCTION_RECORD(FOLD_NET_LITERAL_USES, 1);
                    }
                }
            }
        }
        // Each folded result is attributed once: to a frontend store when SSA
        // holds it, otherwise to its first reader in row order.
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction const* row = function->instructions + index;
            for (u32 operand = 0; operand < row->operand_count; operand += 1)
            {
                u32 value = row->operands[operand].value;
                u8 mark = value < census->value_count ? marks[value] : 0;
                if ((mark & IR_FOLD_CENSUS_MARK_SHAPE) && !(mark & IR_FOLD_CENSUS_MARK_CONSUMED))
                {
                    IrFoldCensusConsumer consumer = (mark & IR_FOLD_CENSUS_MARK_HELD) ? IR_FOLD_CENSUS_CONSUMER_STORE
                                                                                       : ir_fold_census_consumer(row->opcode);
                    u32 shape = (u32)(mark & IR_FOLD_CENSUS_MARK_SHAPE) - 1;
                    ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_STORE +
                                                                   shape * (u32)IR_FOLD_CENSUS_CONSUMER_COUNT + (u32)consumer), 1);
                    marks[value] = (u8)(mark | IR_FOLD_CENSUS_MARK_CONSUMED);
                }
            }
        }
        // Results no row reads, and literals left with no reader.
        for (u32 index = 0; index < function->instruction_count; index += 1)
        {
            IrInstruction const* row = function->instructions + index;
            u32 result = row->result.value;
            u8 mark = result < census->value_count ? marks[result] : 0;
            if ((mark & IR_FOLD_CENSUS_MARK_SHAPE) && !(mark & IR_FOLD_CENSUS_MARK_CONSUMED))
            {
                IrFoldCensusConsumer consumer = (mark & IR_FOLD_CENSUS_MARK_HELD) ? IR_FOLD_CENSUS_CONSUMER_STORE
                                                : uses[result] ? IR_FOLD_CENSUS_CONSUMER_OTHER
                                                               : IR_FOLD_CENSUS_CONSUMER_UNUSED;
                u32 shape = (u32)(mark & IR_FOLD_CENSUS_MARK_SHAPE) - 1;
                ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_FORM_REINTERPRET_STORE +
                                                               shape * (u32)IR_FOLD_CENSUS_CONSUMER_COUNT + (u32)consumer), 1);
                marks[result] = (u8)(mark | IR_FOLD_CENSUS_MARK_CONSUMED);
            }
            for (u32 operand = 0; (mark & IR_FOLD_CENSUS_MARK_SHAPE) && operand < row->operand_count; operand += 1)
            {
                u32 value = row->operands[operand].value;
                if (!uses[value] && !(marks[value] & IR_FOLD_CENSUS_MARK_DEAD) && ir_fold_census_literal(function, value))
                {
                    u32 definition = function->values[value].definition.value;
                    marks[value] |= IR_FOLD_CENSUS_MARK_DEAD;
                    dead += 1;
                    // Committed straight before its only folded reader in the
                    // same block: the frontend could have rewritten it in place.
                    IR_CONSTRUCTION_RECORD(FOLD_NET_ADJACENT_DEAD_LITERALS,
                                           definition + 1 == index && function->instructions[definition].next.value == index);
                }
            }
        }
        u32 bucket = 0;
        while (bucket < BUSTER_ARRAY_LENGTH(ir_fold_census_size_limits) && function->instruction_count >= ir_fold_census_size_limits[bucket])
        {
            bucket += 1;
        }
        u32 offset = bucket * IR_FOLD_CENSUS_SIZE_FIELDS;
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_FUNCTIONS + offset), 1);
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_ROWS + offset), function->instruction_count);
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_FOLDS + offset), folds);
        ir_construction_record((IrConstructionCounter)(IR_CONSTRUCTION_FOLD_SIZE_ROWS_LT_64_DEAD_LITERALS + offset), dead);
        IR_CONSTRUCTION_RECORD(FOLD_NET_FUNCTIONS, 1);
        IR_CONSTRUCTION_RECORD(FOLD_NET_ROWS, function->instruction_count);
        IR_CONSTRUCTION_RECORD(FOLD_NET_DEAD_LITERALS, dead);
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
        // Bounds the DCE work a birth fold's dead literals would have spared
        // (#3010); folded rows rewritten to constants count here too.
        IR_CONSTRUCTION_RECORD(FOLD_LATE_DCE_CONSTANTS, row->opcode == IR_OPCODE_CONSTANT_INTEGER);
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
    if (provenance)
    {
        statistics->provenance_skips += 1;
        IR_FAST_FOLD_CENSUS_ENTRY(function, false);
    }
    else if (scratch_bytes > IR_FAST_SCRATCH_BUDGET || retained_bytes + reopening_bytes > IR_FAST_RETAINED_BUDGET || work > IR_FAST_WORK_BUDGET)
    {
        statistics->budget_skips += 1;
        IR_FAST_FOLD_CENSUS_ENTRY(function, false);
    }
    else
    {
        IR_FAST_FOLD_CENSUS_ENTRY(function, true);
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
        IR_FAST_FOLD_CENSUS_EXIT(function);
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
#define IR_PROMOTION_OUTPUT_VALID 4u
#define IR_PROMOTION_INPUT_VALID 8u

// The promotion-output check for certified input in a checked build. It skips
// functions that failed the strict validator before promotion, and a function
// promotion left unchanged that fails now had the same defect before it ran.
// Both count in `excluded`; only a changed function that was valid fails.
BUSTER_GLOBAL_LOCAL IrValidationResult ir_validate_promotion_output(IrProgram* program, IrModule* module, u8* attribution, u32* excluded)
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
            else if ((attribution[index] & (IR_PROMOTION_INPUT_VALID | IR_PROMOTION_CHANGED)) == IR_PROMOTION_INPUT_VALID)
            {
                // Promotion examined this body but left it unchanged. Its
                // strict input proof therefore also proves the output.
                attribution[index] |= IR_PROMOTION_OUTPUT_VALID;
                IR_CONSTRUCTION_RECORD(PREPARATION_PROMOTION_INPUT_PROOFS_REUSED, 1);
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
                else
                {
                    attribution[index] |= IR_PROMOTION_OUTPUT_VALID;
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
        TemporalArena preparation_scratch = scratch_begin(&program->arena, 1);
        u8* promotion_proofs = 0;
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
                // every default compile. Each rewritten caller is checked at
                // once by the splice proof over what its splices rewrote
                // (ir_validate_canonical_splice), so `validated` still holds
                // afterwards. Every function proven and left unchanged is
                // published in module->inline_proven for the guard below
                // (#3854).
                module->inlining = (IrInlineStatistics){0};
                result = ir_inline_module(program, module, IR_INLINE_SPLICE_FAULT_NONE);
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
            u8* attribution = 0;
            if (input_certified && BUSTER_IR_TRANSFORM_CHECKS && module->function_count)
            {
                attribution = arena_allocate(preparation_scratch.arena, u8, module->function_count);
                memset(attribution, 0, module->function_count);
            }
            for (u32 index = 0; index < module->function_count; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    // Function validation carries its own ownership proof, so
                    // one function attributes without a module-wide scan.
                    if (attribution && ir_function_may_contain_opcodes(function, IR_OPCODE_BIT(IR_OPCODE_LOCAL)))
                    {
                        IrValidationResult input = ir_validate_canonical_function(program, function);
                        attribution[index] |= input.error == IR_VALIDATION_NONE ? IR_PROMOTION_INPUT_VALID : IR_PROMOTION_INPUT_INVALID;
                    }
                    IR_CONSTRUCTION_RECORD(PREPARATION_PROMOTION_FUNCTIONS, 1);
                    u64 promoted_before = module->local_promotion.promoted_locals;
                    ir_promote_function(program, function, &module->local_promotion);
                    // ir_promote_function rewrites rows only when it promotes.
                    bool promoted = module->local_promotion.promoted_locals != promoted_before;
                    if (attribution && promoted)
                    {
                        attribution[index] |= IR_PROMOTION_CHANGED;
                    }
                    if (module->inline_proven && promoted)
                    {
                        module->inline_proven[index] = 0;
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
                if (result.error == IR_VALIDATION_NONE)
                {
                    promotion_proofs = attribution;
                }
            }
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
            // A function ir_inline_module proved, in the state it still has,
            // needs no second per-function check; the shared scope above is
            // still checked.
            u8 const* proven = module->inline_proven;
            for (u32 index = 0; index < module->function_count && result.error == IR_VALIDATION_NONE; index += 1)
            {
                IrFunction* function = module->functions + index;
                if (function->state == IR_FUNCTION_LOWERED)
                {
                    IrValidationResult input = scope;
                    if (!validated && input.error == IR_VALIDATION_NONE)
                    {
                        if (proven && proven[index])
                        {
                            module->inlining.reused_proofs += 1;
                        }
                        else if (promotion_proofs && (promotion_proofs[index] & IR_PROMOTION_OUTPUT_VALID))
                        {
                            // A legacy-invalid neighbor cannot discard this
                            // body's strict post-promotion proof. Nothing has
                            // changed its rows since that check; shared scope
                            // is still checked above, and FAST output below.
                            IR_CONSTRUCTION_RECORD(PREPARATION_PROMOTION_PROOFS_REUSED, 1);
                        }
                        else
                        {
                            input = ir_validate_canonical_function(program, function);
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
        // The inliner's proofs describe rows a later call may have changed.
        module->inline_proven = 0;
        scratch_end(preparation_scratch);
    }
    return result;
}
