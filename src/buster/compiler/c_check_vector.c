// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU and Clang vector expression constraints for #147. Included by c_check_actions.c after its scalar helpers.
// Vector kinds stay separate from C's scalar/arithmetic predicates. Conversions record their destination, but
// never pretend that a scalar CheckValue payload represents a folded vector. The source adapter decodes member
// spellings once; this module checks their bounds only after the base expression has its checked type.
#ifndef BUSTER_C_CHECK_VECTOR_INCLUDED
#define BUSTER_C_CHECK_VECTOR_INCLUDED

typedef enum CheckVectorMemberKind
{
    CHECK_VECTOR_MEMBER_INVALID,
    CHECK_VECTOR_MEMBER_LANES,
    CHECK_VECTOR_MEMBER_LO,
    CHECK_VECTOR_MEMBER_HI,
    CHECK_VECTOR_MEMBER_EVEN,
    CHECK_VECTOR_MEMBER_ODD,
} CheckVectorMemberKind;

struct CheckVectorMember
{
    const uint8_t* lanes;
    uint32_t count;
    uint8_t kind;
};

static bool check_vector_type(const TypeTable* table, TypeId type)
{
    const uint8_t kind = check_action_kind(table, type);
    return kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
}

static uint32_t check_vector_count(const TypeTable* table, TypeId type)
{
    return (uint32_t)table->types.extent[type_index(type_strip_aligned(table, type))];
}

static TypeId check_vector_element(const TypeTable* table, TypeId type)
{
    return type_unqualified(type_element(table, type_strip_aligned(table, type)));
}

static uint64_t check_vector_bits(const TypeTable* table, TypeId type)
{
    const TypeId element = check_vector_element(table, type);
    const uint64_t width = element == BUSTER_TYPE_BOOL ? 1 : type_size(table, element) * 8;
    return width * check_vector_count(table, type);
}

static bool check_vector_real(const TypeTable* table, TypeId type)
{
    return check_action_is_integer(table, type) || check_action_kind(table, type) == BUSTER_TYPE_KIND_FLOATING;
}

static void check_vector_convert(CheckValue* value, TypeId destination)
{
    value->type = destination;
    value->category = CHECK_RVALUE;
    value->constant_kind = CHECK_CONSTANT_NONE;
    value->integer = 0;
    value->floating = 0;
    value->imaginary = 0;
    value->object = 0;
    value->offset = 0;
    value->bit_width = 0;
    value->flags &= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_FP_ENVIRONMENT;
}

static bool check_vector_assignable(const TypeTable* table, TypeId destination, const CheckValue* source, CheckActionResult* result)
{
    const TypeId target = type_unqualified(type_strip_aligned(table, destination));
    const TypeId input = type_unqualified(type_strip_aligned(table, source->type));
    const uint8_t tk = type_kind(table, target);
    const uint8_t sk = type_kind(table, input);
    const bool tv = check_vector_type(table, target);
    const bool sv = check_vector_type(table, input);
    bool accepted = false;
    if (tv && sv)
    {
        accepted = tk == BUSTER_TYPE_KIND_VECTOR || sk == BUSTER_TYPE_KIND_VECTOR ?
                   check_vector_bits(table, target) == check_vector_bits(table, input) :
                   check_action_compatible(table, target, input, result);
    }
    else if (tk == BUSTER_TYPE_KIND_EXT_VECTOR && check_vector_real(table, input))
    {
        accepted = true;
    }
    return accepted;
}

static uint32_t check_vector_precision(TypeId type)
{
    uint32_t result = 0;
    switch (type_unqualified(type))
    {
        case BUSTER_TYPE_FLOAT: result = 24; break;
        case BUSTER_TYPE_DOUBLE: result = 53; break;
        case BUSTER_TYPE_LONG_DOUBLE: result = 64; break;
        default: break;
    }
    return result;
}

static uint32_t check_vector_active_bits(unsigned __int128 bits)
{
    const uint64_t high = (uint64_t)(bits >> 64);
    const uint64_t low = (uint64_t)bits;
    return high != 0 ? 128u - (uint32_t)__builtin_clzll(high) : low != 0 ? 64u - (uint32_t)__builtin_clzll(low) : 0;
}

static bool check_vector_splat(const TypeTable* table, TypeId vector, const CheckValue* scalar)
{
    const TypeId element = check_vector_element(table, vector);
    const TypeId input = type_unqualified(type_strip_aligned(table, scalar->type));
    const bool target_integer = check_action_is_integer(table, element);
    const bool source_integer = check_action_is_integer(table, input);
    bool valid = check_vector_real(table, input);
    if (valid && check_action_kind(table, vector) == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        valid = !target_integer || source_integer;
    }
    else if (valid && target_integer && source_integer)
    {
        uint32_t tw = 0;
        uint32_t sw = 0;
        bool ts = false;
        bool ss = false;
        valid = check_action_integer_type(table, element, &tw, &ts) && check_action_integer_type(table, input, &sw, &ss);
        if (valid && scalar->constant_kind == CHECK_CONSTANT_INTEGER && scalar->fault == CHECK_FAULT_NONE && (scalar->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            const unsigned __int128 bits = scalar->integer & check_action_mask(sw);
            const bool negative = ss && (bits >> (sw - 1)) != 0;
            const uint32_t needed = negative ? check_vector_active_bits((~bits) & check_action_mask(sw)) + 1 : check_vector_active_bits(bits);
            valid = needed <= tw;
        }
        else if (valid)
        {
            // Integer rank resolves equal-width signed/unsigned splats directionally, as ordinary C does.
            uint64_t tr = 0;
            uint64_t sr = 0;
            uint64_t ignored_width = 0;
            bool ignored_sign = false;
            type_integer_properties(table, element, &tr, &ignored_width, &ignored_sign);
            TypeId integer_input = input;
            if (type_kind(table, integer_input) == BUSTER_TYPE_KIND_ENUM)
            {
                integer_input = table->types.base[type_index(integer_input)];
            }
            type_integer_properties(table, integer_input, &sr, &ignored_width, &ignored_sign);
            valid = tr > sr || (tr == sr && (!ts || ss));
        }
    }
    else if (valid && target_integer)
    {
        valid = type_size(table, element) == type_size(table, input);
    }
    else if (valid && source_integer)
    {
        uint32_t width = 0;
        bool sign = false;
        valid = check_action_integer_type(table, input, &width, &sign);
        const uint32_t precision = check_vector_precision(element);
        if (valid && scalar->constant_kind == CHECK_CONSTANT_INTEGER && scalar->fault == CHECK_FAULT_NONE && (scalar->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            const unsigned __int128 bits = scalar->integer & check_action_mask(width);
            const unsigned __int128 magnitude = sign && (bits >> (width - 1)) != 0 ? ((~bits) + 1) & check_action_mask(width) : bits;
            const uint32_t active = check_vector_active_bits(magnitude);
            valid = precision != 0 && (active <= precision || (magnitude & check_action_mask(active - precision)) == 0);
        }
        else
        {
            valid = valid && width <= precision;
        }
    }
    else if (valid)
    {
        valid = check_vector_precision(element) >= check_vector_precision(input);
        if (!valid && check_action_float_supported(table, element) && scalar->constant_kind == CHECK_CONSTANT_FLOATING && scalar->fault == CHECK_FAULT_NONE && (scalar->flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
        {
            const long double rounded = check_action_round_float(element, scalar->floating);
            valid = rounded == scalar->floating || (isnan(rounded) && isnan(scalar->floating));
        }
    }
    return valid;
}

static void check_vector_conversion(CheckValue* value, TypeId destination, uint32_t flags, CheckConversion* conversion)
{
    conversion->type = destination;
    conversion->flags |= flags;
    check_vector_convert(value, destination);
}

static TypeId check_vector_mask_type(TypeTable* table, TypeId vector)
{
    const TypeId element = check_vector_element(table, vector);
    TypeId mask = 0;
    if (element == BUSTER_TYPE_BOOL)
    {
        mask = BUSTER_TYPE_BOOL;
    }
    else
    {
        switch (type_size(table, element))
        {
            case 1: mask = BUSTER_TYPE_SCHAR; break;
            case 2: mask = BUSTER_TYPE_SHORT; break;
            case 4: mask = BUSTER_TYPE_INT; break;
            case 8: mask = BUSTER_TYPE_LLONG; break;
            case 16: mask = BUSTER_TYPE_INT128; break;
            default: break;
        }
    }
    return mask == 0 ? 0 : type_vector(table, mask, check_vector_count(table, vector), check_action_kind(table, vector) == BUSTER_TYPE_KIND_EXT_VECTOR);
}

static TypeId check_vector_common(const TypeTable* table, const CheckValue* left, const CheckValue* right, CheckActionResult* result)
{
    const bool lv = check_vector_type(table, left->type);
    const bool rv = check_vector_type(table, right->type);
    TypeId common = 0;
    if (lv && rv && check_vector_bits(table, left->type) == check_vector_bits(table, right->type))
    {
        common = left->type;
        if (check_vector_element(table, left->type) == check_vector_element(table, right->type) &&
            check_vector_count(table, left->type) == check_vector_count(table, right->type) &&
            check_action_kind(table, right->type) == BUSTER_TYPE_KIND_EXT_VECTOR)
        {
            common = right->type;
        }
    }
    else if (lv && !rv && check_vector_splat(table, left->type, right))
    {
        common = left->type;
    }
    else if (!lv && rv && check_vector_splat(table, right->type, left))
    {
        common = right->type;
    }
    if (common == 0)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, rv ? 0 : 1);
    }
    return common;
}

static void check_vector_binary(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue left = check_action_operand(table, action, result, 0);
    CheckValue right = check_action_operand(table, action, result, 1);
    const bool lv = check_vector_type(table, left.type);
    const bool rv = check_vector_type(table, right.type);
    const TypeId le = lv ? check_vector_element(table, left.type) : left.type;
    const TypeId re = rv ? check_vector_element(table, right.type) : right.type;
    const bool shift = action->op == CHECK_OP_SHIFT_LEFT || action->op == CHECK_OP_SHIFT_RIGHT;
    const bool integer = shift || action->op == CHECK_OP_REMAINDER || action->op == CHECK_OP_BIT_AND || action->op == CHECK_OP_BIT_OR || action->op == CHECK_OP_BIT_XOR;
    const bool compare = action->op >= CHECK_OP_LESS && action->op <= CHECK_OP_NOT_EQUAL;
    const bool logical = action->op == CHECK_OP_LOGICAL_AND || action->op == CHECK_OP_LOGICAL_OR;
    const bool boolean = (lv && le == BUSTER_TYPE_BOOL) || (rv && re == BUSTER_TYPE_BOOL);
    const bool bool_allowed = compare || logical || action->op == CHECK_OP_BIT_AND || action->op == CHECK_OP_BIT_OR || action->op == CHECK_OP_BIT_XOR;
    if (integer && (!check_action_is_integer(table, le) || !check_action_is_integer(table, re)))
    {
        check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, !check_action_is_integer(table, le) ? 0 : 1);
    }
    else if (boolean && !bool_allowed)
    {
        check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
    }
    else if (logical && ((boolean && (!lv || !rv || le != BUSTER_TYPE_BOOL || re != BUSTER_TYPE_BOOL)) ||
                         (check_action_kind(table, left.type) != BUSTER_TYPE_KIND_EXT_VECTOR && check_action_kind(table, right.type) != BUSTER_TYPE_KIND_EXT_VECTOR)))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    else if (result->diagnostic == CHECK_DIAG_NONE)
    {
        TypeId common = 0;
        if (shift)
        {
            if (lv && rv && (check_vector_count(table, left.type) != check_vector_count(table, right.type) || type_size(table, le) != type_size(table, re)))
            {
                check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
            }
            else
            {
                const uint32_t count = check_vector_count(table, lv ? left.type : right.type);
                const TypeId lt = lv ? left.type : type_vector(table, re, count, true);
                const TypeId rt = rv ? right.type : type_vector(table, type_promote(table, right.type), count, true);
                common = lt;
                if (lt == 0 || rt == 0)
                {
                    check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
                }
                else
                {
                    check_vector_conversion(&left, lt, lv ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[0]);
                    check_vector_conversion(&right, rt, rv ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[1]);
                }
            }
        }
        else
        {
            common = check_vector_common(table, &left, &right, result);
            if (common != 0)
            {
                check_vector_conversion(&left, common, left.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[0]);
                check_vector_conversion(&right, common, right.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[1]);
            }
        }
        if (result->diagnostic == CHECK_DIAG_NONE)
        {
            result->value.type = compare || logical ? check_vector_mask_type(table, common) : common;
            result->value.category = CHECK_RVALUE;
            check_action_inherit(&result->value, &left);
            check_action_inherit(&result->value, &right);
            if (result->value.type == 0)
            {
                check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
            }
        }
    }
}

static void check_vector_unary(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue input = check_action_operand(table, action, result, 0);
    const TypeId element = check_vector_element(table, input.type);
    if (action->op == CHECK_OP_BIT_NOT && !check_action_is_integer(table, element))
    {
        check_action_diagnose(result, CHECK_DIAG_INTEGER_REQUIRED, 0);
    }
    else if (action->op == CHECK_OP_NOT && check_action_kind(table, input.type) != BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    else if ((action->op == CHECK_OP_POSITIVE || action->op == CHECK_OP_NEGATIVE) && element == BUSTER_TYPE_BOOL)
    {
        check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
    }
    else
    {
        result->value.type = action->op == CHECK_OP_NOT ? check_vector_mask_type(table, input.type) : input.type;
        result->value.category = CHECK_RVALUE;
        check_action_inherit(&result->value, &input);
        if (result->value.type == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
        }
    }
}

static void check_vector_cast(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    CheckValue source = check_action_operand(table, action, result, 0);
    const TypeId target = type_unqualified(type_strip_aligned(table, action->type));
    const bool tv = check_vector_type(table, target);
    const bool sv = check_vector_type(table, source.type);
    bool accepted = target == BUSTER_TYPE_VOID;
    if (tv && sv)
    {
        accepted = check_vector_bits(table, target) == check_vector_bits(table, source.type);
    }
    else if (tv)
    {
        accepted = check_action_kind(table, target) == BUSTER_TYPE_KIND_EXT_VECTOR ? check_vector_real(table, source.type) :
                   check_action_is_integer(table, source.type) && check_vector_bits(table, target) == type_size(table, source.type) * 8;
    }
    else if (sv && target != BUSTER_TYPE_VOID)
    {
        accepted = check_action_is_integer(table, target) && type_size(table, target) * 8 == check_vector_bits(table, source.type);
    }
    if (!accepted)
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    else
    {
        check_vector_conversion(&source, target, BUSTER_CHECK_CONVERT_CAST, &result->conversions[0]);
        result->value = source;
    }
}

static void check_vector_subscript(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue base = action->operands[0];
    CheckValue index = check_action_operand(table, action, result, 1);
    if (!check_vector_type(table, base.type) || !check_action_is_integer(table, index.type))
    {
        check_action_diagnose(result, !check_vector_type(table, base.type) ? CHECK_DIAG_POINTER_REQUIRED : CHECK_DIAG_INTEGER_REQUIRED, 1);
    }
    else
    {
        check_action_promote(table, &index, &result->conversions[1], action->node);
        result->value.type = check_vector_element(table, base.type) | type_qualifiers(base.type);
        result->value.category = base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
        result->value.flags = BUSTER_CHECK_VECTOR_ELEMENT | (base.flags & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART));
        check_action_inherit(&result->value, &base);
        check_action_inherit(&result->value, &index);
    }
}

// A false return means the caller's lane storage was too small. Invalid spelling is a successful decode with
// INVALID kind: an ordinary record member may use that spelling and must remain unaffected by vector parsing.
static bool check_vector_member_decode(const uint8_t* spelling, uint32_t length, uint8_t* lanes, uint32_t capacity, CheckVectorMember* member)
{
    *member = (CheckVectorMember){ 0 };
    bool fits = true;
    if (length == 2 && memcmp(spelling, "lo", 2) == 0)
    {
        member->kind = CHECK_VECTOR_MEMBER_LO;
    }
    else if (length == 2 && memcmp(spelling, "hi", 2) == 0)
    {
        member->kind = CHECK_VECTOR_MEMBER_HI;
    }
    else if (length == 4 && memcmp(spelling, "even", 4) == 0)
    {
        member->kind = CHECK_VECTOR_MEMBER_EVEN;
    }
    else if (length == 3 && memcmp(spelling, "odd", 3) == 0)
    {
        member->kind = CHECK_VECTOR_MEMBER_ODD;
    }
    else if (length != 0)
    {
        const bool numeric = spelling[0] == 's' || spelling[0] == 'S';
        const uint32_t first = numeric ? 1 : 0;
        const bool color = spelling[0] == 'r' || spelling[0] == 'g' || spelling[0] == 'b' || spelling[0] == 'a';
        bool valid = length > first;
        for (uint32_t i = first; valid && i < length; i += 1)
        {
            const uint8_t byte = spelling[i];
            const uint8_t folded = byte | 32;
            uint8_t lane = 255;
            if (numeric)
            {
                lane = byte >= '0' && byte <= '9' ? (uint8_t)(byte - '0') : folded >= 'a' && folded <= 'f' ? (uint8_t)(folded - 'a' + 10) : 255;
            }
            else
            {
                const char* alphabet = color ? "rgba" : "xyzw";
                for (uint8_t j = 0; j < 4; j += 1)
                {
                    if (byte == (uint8_t)alphabet[j])
                    {
                        lane = j;
                    }
                }
            }
            valid = lane != 255;
            if (valid && i - first < capacity && lanes != NULL)
            {
                lanes[i - first] = lane;
            }
        }
        if (valid)
        {
            fits = length - first <= capacity && lanes != NULL;
            if (fits)
            {
                member->kind = CHECK_VECTOR_MEMBER_LANES;
                member->lanes = lanes;
                member->count = length - first;
            }
        }
    }
    return fits;
}

static void check_vector_member(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue base = action->operands[0];
    const CheckVectorMember* member = action->vector_member;
    const uint32_t count = check_vector_count(table, base.type);
    const TypeId element = check_vector_element(table, base.type);
    bool valid = check_action_kind(table, base.type) == BUSTER_TYPE_KIND_EXT_VECTOR && element != BUSTER_TYPE_BOOL && member != NULL &&
                 member->kind > CHECK_VECTOR_MEMBER_INVALID && member->kind <= CHECK_VECTOR_MEMBER_ODD;
    bool duplicate = (base.flags & BUSTER_CHECK_VECTOR_DUPLICATE) != 0;
    uint32_t selected = 0;
    if (valid && member->kind == CHECK_VECTOR_MEMBER_LANES)
    {
        valid = member->count != 0 && member->lanes != NULL;
        selected = member->count;
        uint32_t seen = 0;
        for (uint32_t i = 0; valid && i < selected; i += 1)
        {
            const uint32_t lane = member->lanes[i];
            valid = lane < count && lane < 16;
            if (valid)
            {
                const uint32_t bit = 1u << lane;
                duplicate |= (seen & bit) != 0;
                seen |= bit;
            }
        }
    }
    else if (valid)
    {
        selected = (count + 1) / 2;
    }
    if (!valid)
    {
        check_action_diagnose(result, CHECK_DIAG_MEMBER, 0);
    }
    else
    {
        const TypeId target = selected == 1 ? element : type_vector(table, element, selected, true);
        if (target == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, 0);
        }
        else
        {
            result->value.type = target | type_qualifiers(base.type);
            result->value.category = base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            result->value.flags = BUSTER_CHECK_VECTOR_ELEMENT | (duplicate ? BUSTER_CHECK_VECTOR_DUPLICATE : 0) |
                                  (base.flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART));
            check_action_inherit(&result->value, &base);
        }
    }
}

static void check_vector_conditional(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const CheckValue condition = check_action_operand(table, action, result, 0);
    CheckValue left = check_action_operand(table, action, result, 1);
    CheckValue right = check_action_operand(table, action, result, 2);
    const bool vc = check_vector_type(table, condition.type);
    const bool lv = check_vector_type(table, left.type);
    const bool rv = check_vector_type(table, right.type);
    TypeId common = 0;
    if (vc && (check_action_kind(table, condition.type) != BUSTER_TYPE_KIND_EXT_VECTOR || !check_action_is_integer(table, check_vector_element(table, condition.type))))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    else if (!vc && !check_action_is_scalar(table, condition.type))
    {
        check_action_diagnose(result, CHECK_DIAG_SCALAR_REQUIRED, 0);
    }
    else if (lv || rv)
    {
        common = check_vector_common(table, &left, &right, result);
    }
    else if (vc && check_vector_real(table, left.type) && check_vector_real(table, right.type))
    {
        const TypeId scalar = type_usual_arithmetic(table, left.type, right.type);
        common = scalar == 0 ? 0 : type_vector(table, scalar, check_vector_count(table, condition.type), true);
        if (common == 0)
        {
            check_action_diagnose(result, CHECK_DIAG_RESOURCE, 1);
        }
    }
    else
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 1);
    }
    if (result->diagnostic == CHECK_DIAG_NONE && vc &&
        (check_vector_count(table, condition.type) != check_vector_count(table, common) ||
         (check_vector_element(table, condition.type) != BUSTER_TYPE_BOOL && type_size(table, check_vector_element(table, condition.type)) != type_size(table, check_vector_element(table, common)))))
    {
        check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
    }
    if (result->diagnostic == CHECK_DIAG_NONE)
    {
        check_vector_conversion(&left, common, left.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[1]);
        check_vector_conversion(&right, common, right.type == common ? 0 : BUSTER_CHECK_CONVERT_ARITHMETIC, &result->conversions[2]);
        result->value.type = common;
        result->value.category = CHECK_RVALUE;
        check_action_inherit(&result->value, &condition);
        bool truth = false;
        const bool known = !vc && check_action_truth(table, &condition, &truth);
        if (!known || truth)
        {
            check_action_inherit(&result->value, &left);
        }
        if (!known || !truth)
        {
            check_action_inherit(&result->value, &right);
        }
    }
}

static bool check_vector_action(TypeTable* table, const CheckAction* action, CheckActionResult* result)
{
    const bool left = check_vector_type(table, action->operands[0].type);
    const bool right = check_vector_type(table, action->operands[1].type);
    bool handled = false;
    switch (action->op)
    {
        case CHECK_OP_POSITIVE:
        case CHECK_OP_NEGATIVE:
        case CHECK_OP_NOT:
        case CHECK_OP_BIT_NOT:
        {
            handled = left;
            if (handled)
            {
                check_vector_unary(table, action, result);
            }
        } break;
        case CHECK_OP_ADD:
        case CHECK_OP_SUBTRACT:
        case CHECK_OP_MULTIPLY:
        case CHECK_OP_DIVIDE:
        case CHECK_OP_REMAINDER:
        case CHECK_OP_SHIFT_LEFT:
        case CHECK_OP_SHIFT_RIGHT:
        case CHECK_OP_LESS:
        case CHECK_OP_LESS_EQUAL:
        case CHECK_OP_GREATER:
        case CHECK_OP_GREATER_EQUAL:
        case CHECK_OP_EQUAL:
        case CHECK_OP_NOT_EQUAL:
        case CHECK_OP_BIT_AND:
        case CHECK_OP_BIT_OR:
        case CHECK_OP_BIT_XOR:
        case CHECK_OP_LOGICAL_AND:
        case CHECK_OP_LOGICAL_OR:
        {
            handled = left || right;
            if (handled)
            {
                check_vector_binary(table, action, result);
            }
        } break;
        case CHECK_OP_CAST:
        {
            handled = left || check_vector_type(table, action->type);
            if (handled)
            {
                check_vector_cast(table, action, result);
            }
        } break;
        case CHECK_OP_SUBSCRIPT:
        {
            handled = left || right;
            if (handled)
            {
                check_vector_subscript(table, action, result);
            }
        } break;
        case CHECK_OP_MEMBER:
        {
            handled = left;
            if (handled)
            {
                check_vector_member(table, action, result);
            }
        } break;
        case CHECK_OP_CONDITIONAL:
        {
            handled = left || right || check_vector_type(table, action->operands[2].type);
            if (handled)
            {
                check_vector_conditional(table, action, result);
            }
        } break;
        case CHECK_OP_PRE_INCREMENT:
        case CHECK_OP_PRE_DECREMENT:
        case CHECK_OP_POST_INCREMENT:
        case CHECK_OP_POST_DECREMENT:
        {
            handled = left;
            if (handled)
            {
                check_action_diagnose(result, CHECK_DIAG_ARITHMETIC_REQUIRED, 0);
            }
        } break;
        case CHECK_OP_ADD_ASSIGN:
        case CHECK_OP_SUBTRACT_ASSIGN:
        case CHECK_OP_MULTIPLY_ASSIGN:
        case CHECK_OP_DIVIDE_ASSIGN:
        case CHECK_OP_REMAINDER_ASSIGN:
        case CHECK_OP_SHIFT_LEFT_ASSIGN:
        case CHECK_OP_SHIFT_RIGHT_ASSIGN:
        case CHECK_OP_BIT_AND_ASSIGN:
        case CHECK_OP_BIT_OR_ASSIGN:
        case CHECK_OP_BIT_XOR_ASSIGN:
        {
            handled = left || right;
            if (handled && check_action_modifiable(table, &action->operands[0], action, result))
            {
                static const CheckOp operations[] = {
                    CHECK_OP_ADD,
                    CHECK_OP_SUBTRACT,
                    CHECK_OP_MULTIPLY,
                    CHECK_OP_DIVIDE,
                    CHECK_OP_REMAINDER,
                    CHECK_OP_SHIFT_LEFT,
                    CHECK_OP_SHIFT_RIGHT,
                    CHECK_OP_BIT_AND,
                    CHECK_OP_BIT_OR,
                    CHECK_OP_BIT_XOR,
                };
                CheckAction arithmetic = *action;
                arithmetic.op = operations[action->op - CHECK_OP_ADD_ASSIGN];
                check_vector_binary(table, &arithmetic, result);
                const TypeId target = type_conversion_operand(table, action->operands[0].type);
                if (result->diagnostic == CHECK_DIAG_NONE && !check_vector_assignable(table, target, &result->value, result))
                {
                    check_action_diagnose(result, CHECK_DIAG_INCOMPATIBLE, 0);
                }
                if (result->diagnostic == CHECK_DIAG_NONE)
                {
                    check_vector_conversion(&result->value, target, BUSTER_CHECK_CONVERT_ASSIGNMENT, &result->store_conversion);
                    result->value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                }
            }
        } break;
        default: break;
    }
    return handled;
}

#endif
