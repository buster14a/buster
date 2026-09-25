from pathlib import Path
import difflib
import sys
root=Path(sys.argv[1]).resolve()
path='src/buster/lib/compiler/frontend/c/c_gen.c'
base=(root/path).read_text()
out=Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
def replace(s,a,b):
    assert s.count(a)==1,(a[:100],s.count(a))
    return s.replace(a,b)
def patch(name,s):
    (out/(name+'.patch')).write_text(''.join(difflib.unified_diff(base.splitlines(True),s.splitlines(True),fromfile='a/'+path,tofile='b/'+path)))
    (out/(name+'.c')).write_text(s)
# Strong narrow repair in the same scope: retain the bool API, repeat the
# existing _Bool array/scalar rule, and reject right-only certification.
s=base
unary_old='''    else if (value->kind == C_IR_CONSTANT_UNKNOWN)
    {
        success = true;
    }
    else if (operation == C_CONDITIONAL_DEREFERENCE)'''
unary_new='''    else if (operation == C_CONDITIONAL_LOGICAL_NOT)
    {
        IrType* type = ir_type_from_id(&builder->program->types, value->type);
        if (value->kind == C_IR_CONSTANT_LVALUE && type && type->kind == IR_TYPE_ARRAY)
        {
            value->kind = C_IR_CONSTANT_POINTER;
        }
        success = c_ir_constant_normalize(builder, value);
        if (success)
        {
            *value = value->kind == C_IR_CONSTANT_UNKNOWN
                         ? (CIrConstantValue){.type = builder->s32_type, .kind = C_IR_CONSTANT_UNKNOWN}
                         : c_ir_constant_integer(builder->s32_type, !c_ir_constant_truth(builder, value));
        }
    }
'''+unary_old
s=replace(s,unary_old,unary_new)
unary_inner='''        if (operation == C_CONDITIONAL_LOGICAL_NOT)
        {
            *value = c_ir_constant_integer(builder->s32_type, !c_ir_constant_truth(builder, value));
            success = true;
        }
        else if (operation == C_CONDITIONAL_UNARY_PLUS || operation == C_CONDITIONAL_UNARY_MINUS || operation == C_CONDITIONAL_BITWISE_NOT)'''
s=replace(s,unary_inner,'''        if (operation == C_CONDITIONAL_UNARY_PLUS || operation == C_CONDITIONAL_UNARY_MINUS || operation == C_CONDITIONAL_BITWISE_NOT)''')
logical_start='''    if (operation == C_CONDITIONAL_LOGICAL_AND || operation == C_CONDITIONAL_LOGICAL_OR)
    {
        if (left.kind != C_IR_CONSTANT_UNKNOWN && !c_ir_constant_normalize(builder, &left))'''
s=replace(s,logical_start,'''    if (operation == C_CONDITIONAL_LOGICAL_AND || operation == C_CONDITIONAL_LOGICAL_OR)
    {
        IrType* left_type = ir_type_from_id(&builder->program->types, left.type);
        IrType* right_type = ir_type_from_id(&builder->program->types, right.type);
        if (left.kind == C_IR_CONSTANT_LVALUE && left_type && left_type->kind == IR_TYPE_ARRAY)
        {
            left.kind = C_IR_CONSTANT_POINTER;
        }
        if (right.kind == C_IR_CONSTANT_LVALUE && right_type && right_type->kind == IR_TYPE_ARRAY)
        {
            right.kind = C_IR_CONSTANT_POINTER;
        }
        if (left.kind != C_IR_CONSTANT_UNKNOWN && !c_ir_constant_normalize(builder, &left))''')
right_annihilator='''        if (right_known)
        {
            bool right_truth = c_ir_constant_truth(builder, &right);
            if ((operation == C_CONDITIONAL_LOGICAL_AND && !right_truth) || (operation == C_CONDITIONAL_LOGICAL_OR && right_truth))
            {
                *result = c_ir_constant_integer(builder->s32_type, operation == C_CONDITIONAL_LOGICAL_OR);
                return true;
            }
        }
'''
s=replace(s,right_annihilator,'')
select='''            CIrConstantValue selected = c_ir_constant_truth(builder, &condition) ? true_value : false_value;
            if (condition.kind == C_IR_CONSTANT_UNKNOWN)'''
s=replace(s,select,'''            IrType* condition_type = ir_type_from_id(&builder->program->types, condition.type);
            if (condition.kind == C_IR_CONSTANT_LVALUE && condition_type && condition_type->kind == IR_TYPE_ARRAY)
            {
                condition.kind = C_IR_CONSTANT_POINTER;
            }
            bool normalized = c_ir_constant_normalize(builder, &condition);
            CIrConstantValue selected = {0};
            if (normalized && condition.kind == C_IR_CONSTANT_UNKNOWN)''')
s=replace(s,'''            else if (common.value != IR_ID_UNDERLYING_INVALID)
            {
                // ?: converts''','''            else if (normalized && common.value != IR_ID_UNDERLYING_INVALID)
            {
                selected = c_ir_constant_truth(builder, &condition) ? true_value : false_value;
                // ?: converts''')
patch('local',s)
# The structural result is private and cannot implicitly convert to C bool.
s=base
decl='BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value);'
s=replace(s,decl,'''typedef enum CIrConstantTruthKind
{
    C_IR_TRUTH_INVALID,
    C_IR_TRUTH_UNKNOWN,
    C_IR_TRUTH_FALSE,
    C_IR_TRUTH_TRUE,
} CIrConstantTruthKind;

// A query result, not a Boolean value. UNKNOWN cannot silently become false.
// Kept in a struct so using the result as an if/! operand is a C type error.
// This is ephemeral proof for one consumer, never a cached IR certificate.
typedef struct CIrConstantTruth
{
    CIrConstantTruthKind kind;
} CIrConstantTruth;

BUSTER_C_INTERNAL CIrConstantTruth c_ir_constant_truth_query(CIntegerIrBuilder* builder, const CIrConstantValue* value);''')
start=s.index('BUSTER_C_INTERNAL bool c_ir_constant_truth(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)')
end=s.index('\nBUSTER_C_INTERNAL ',start+30)
new_function='''BUSTER_C_INTERNAL CIrConstantTruth c_ir_constant_truth_query(CIntegerIrBuilder* builder, const CIrConstantValue* value_input)
{
    CIrConstantValue value = *value_input;
    CIrConstantTruth result = {.kind = C_IR_TRUTH_INVALID};
    bool valid = true;
    bool array_address = false;
    if (value.kind == C_IR_CONSTANT_LVALUE)
    {
        IrType* place_type = ir_type_from_id(&builder->program->types, value.type);
        array_address = place_type && place_type->kind == IR_TYPE_ARRAY;
        if (array_address)
        {
            // Decay for the zero comparison without interning a pointer type.
            value.kind = C_IR_CONSTANT_POINTER;
        }
        else
        {
            valid = c_ir_constant_normalize(builder, &value);
        }
    }
    if (valid && value.kind == C_IR_CONSTANT_UNKNOWN)
    {
        result.kind = C_IR_TRUTH_UNKNOWN;
    }
    else if (valid && value.kind == C_IR_CONSTANT_POINTER)
    {
        IrType* type = ir_type_from_id(&builder->program->types, value.type);
        if (array_address || (type && type->kind == IR_TYPE_POINTER))
        {
            bool nonzero = value.symbol.value != IR_ID_UNDERLYING_INVALID || value.addend != 0;
            result.kind = nonzero ? C_IR_TRUTH_TRUE : C_IR_TRUTH_FALSE;
        }
    }
    else if (valid && value.kind == C_IR_CONSTANT_FLOAT)
    {
        IrType* type = ir_type_from_id(&builder->program->types, value.type);
        if (type && type->kind == IR_TYPE_FLOAT)
        {
            bool nonzero = type->bit_width > 64
                               ? value.integer != 0 ||
                                     (value.integer_high & (type->bit_width == 80 ? UINT64_C(0x7fff) : UINT64_C(0x7fffffffffffffff))) != 0
                               : value.floating != 0.0;
            result.kind = nonzero ? C_IR_TRUTH_TRUE : C_IR_TRUTH_FALSE;
        }
    }
    else if (valid && value.kind == C_IR_CONSTANT_INTEGER)
    {
        IrType* type = ir_type_from_id(&builder->program->types, value.type);
        if (c_ir_constant_type_is_integer(type))
        {
            bool nonzero = (value.integer & c_ir_integer_type_mask(type)) != 0 || value.integer_high != 0;
            result.kind = nonzero ? C_IR_TRUTH_TRUE : C_IR_TRUTH_FALSE;
        }
    }
    return result;
}
'''
s=s[:start]+new_function+s[end:]
cast_start=s.index('BUSTER_C_INTERNAL bool c_ir_constant_cast(CIntegerIrBuilder* builder, const CIrConstantValue* source_input')
cast_end=s.index('\nBUSTER_C_INTERNAL IrTypeId c_ir_constant_common_type',cast_start)
cast=s[cast_start:cast_end]
cast=replace(cast,'    IrType* source_type = ir_type_from_id(&builder->program->types, source.type);\n','    IrType* source_type = 0;\n')
cast=replace(cast,'''        else if (target->kind == IR_TYPE_POINTER)
        {
            if (source.kind''','''        else if (target->kind == IR_TYPE_POINTER)
        {
            source_type = ir_type_from_id(&builder->program->types, source.type);
            if (source.kind''')
array_block='''        else
        {
            // An array decays before scalar conversion; a scalar lvalue must
            // instead be read/normalized, not mistaken for its own address.
            if (target->kind == IR_TYPE_BOOLEAN && source.kind == C_IR_CONSTANT_LVALUE && source_type && source_type->kind == IR_TYPE_ARRAY)
            {
                source.kind = C_IR_CONSTANT_POINTER;
            }
            if (c_ir_constant_normalize(builder, &source))'''
cast=replace(cast,array_block,'''        else if (target->kind == IR_TYPE_BOOLEAN)
        {
            CIrConstantTruth truth = c_ir_constant_truth_query(builder, &source);
            success = truth.kind != C_IR_TRUTH_INVALID;
            if (success)
            {
                *result = truth.kind == C_IR_TRUTH_UNKNOWN
                              ? (CIrConstantValue){.type = target_type, .kind = C_IR_CONSTANT_UNKNOWN}
                              : c_ir_constant_integer(target_type, truth.kind == C_IR_TRUTH_TRUE);
            }
        }
        else
        {
            if (c_ir_constant_normalize(builder, &source))''')
boolean_block='''                else if (target->kind == IR_TYPE_BOOLEAN)
                {
                    // C 6.3.1.2: compare to zero before any truncation. This
                    // includes floating fractions, NaNs, pointers and i128's
                    // high limb; a one-bit integer mask cannot implement it.
                    success = source.kind == C_IR_CONSTANT_POINTER || source.kind == C_IR_CONSTANT_FLOAT ||
                              (source.kind == C_IR_CONSTANT_INTEGER && c_ir_constant_type_is_integer(source_type));
                    if (success)
                        *result = c_ir_constant_integer(target_type, c_ir_constant_truth(builder, &source));
                }
'''
cast=replace(cast,boolean_block,'')
s=s[:cast_start]+cast+s[cast_end:]
s=replace(s,unary_old,'''    else if (operation == C_CONDITIONAL_LOGICAL_NOT)
    {
        CIrConstantTruth truth = c_ir_constant_truth_query(builder, value);
        success = truth.kind != C_IR_TRUTH_INVALID;
        if (success)
        {
            *value = truth.kind == C_IR_TRUTH_UNKNOWN
                         ? (CIrConstantValue){.type = builder->s32_type, .kind = C_IR_CONSTANT_UNKNOWN}
                         : c_ir_constant_integer(builder->s32_type, truth.kind == C_IR_TRUTH_FALSE);
        }
    }
'''+unary_old)
s=replace(s,unary_inner,'''        if (operation == C_CONDITIONAL_UNARY_PLUS || operation == C_CONDITIONAL_UNARY_MINUS || operation == C_CONDITIONAL_BITWISE_NOT)''')
start=s.index('    if (operation == C_CONDITIONAL_LOGICAL_AND || operation == C_CONDITIONAL_LOGICAL_OR)\n',s.index('BUSTER_C_INTERNAL bool c_ir_constant_apply_binary('))
end=s.index('    if (left.kind == C_IR_CONSTANT_UNKNOWN || right.kind == C_IR_CONSTANT_UNKNOWN)',start)
s=s[:start]+'''    if (operation == C_CONDITIONAL_LOGICAL_AND || operation == C_CONDITIONAL_LOGICAL_OR)
    {
        CIrConstantTruth left_truth = c_ir_constant_truth_query(builder, &left);
        CIrConstantTruth right_truth = c_ir_constant_truth_query(builder, &right);
        bool valid = left_truth.kind != C_IR_TRUTH_INVALID && right_truth.kind != C_IR_TRUTH_INVALID;
        *result = (CIrConstantValue){.type = builder->s32_type, .kind = C_IR_CONSTANT_UNKNOWN};
        if (valid && left_truth.kind != C_IR_TRUTH_UNKNOWN)
        {
            bool short_circuit = (operation == C_CONDITIONAL_LOGICAL_AND && left_truth.kind == C_IR_TRUTH_FALSE) ||
                                 (operation == C_CONDITIONAL_LOGICAL_OR && left_truth.kind == C_IR_TRUTH_TRUE);
            if (short_circuit)
            {
                *result = c_ir_constant_integer(builder->s32_type, left_truth.kind == C_IR_TRUTH_TRUE);
            }
            else if (right_truth.kind != C_IR_TRUTH_UNKNOWN)
            {
                *result = c_ir_constant_integer(builder->s32_type, right_truth.kind == C_IR_TRUTH_TRUE);
            }
        }
        // An uncertified live left operand cannot be rescued by a predictable
        // right result: this value is also proof for __builtin_constant_p.
        return valid;
    }
'''+s[end:]
s=replace(s,select,'''            CIrConstantTruth truth = c_ir_constant_truth_query(builder, &condition);
            CIrConstantValue selected = {0};
            if (truth.kind == C_IR_TRUTH_UNKNOWN)''')
s=replace(s,'''            else if (common.value != IR_ID_UNDERLYING_INVALID)
            {
                // ?: converts''','''            else if (truth.kind != C_IR_TRUTH_INVALID && common.value != IR_ID_UNDERLYING_INVALID)
            {
                selected = truth.kind == C_IR_TRUTH_TRUE ? true_value : false_value;
                // ?: converts''')
s=s.replace('!c_ir_constant_truth(builder, &assertion)','c_ir_constant_truth_query(builder, &assertion).kind != C_IR_TRUTH_TRUE')
s=s.replace('!c_ir_constant_truth(&constant_builder, &assertion)','c_ir_constant_truth_query(&constant_builder, &assertion).kind != C_IR_TRUTH_TRUE')
assert 'c_ir_constant_truth(' not in s
patch('structural',s)
print('Base bytes',len(base),'local',len((out/'local.patch').read_text()),'structural',len((out/'structural.patch').read_text()))
