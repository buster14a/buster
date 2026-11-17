// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source ranges for -Wsign-compare (#1391), included by c_check_common_warnings.c. These are diagnostic ranges,
// including Clang's deliberately approximate arithmetic rules, never optimizer or constant-evaluation facts.
// check_common_sign_range visits only the queried operand's dependency tree. Its explicit postorder stack borrows
// unused emission frames and returns the opaque type range if their capacity or its DAG work budget is exhausted.

typedef struct CheckCommonSignRange CheckCommonSignRange;
struct CheckCommonSignRange
{
    uint32_t width;
    bool nonnegative;
};

// Widths are at most 128. A saved left range occupies the low nine bits, with the caller's truncation width above it.
#define BUSTER_CHECK_SIGN_CAP_SHIFT 16u
#define BUSTER_CHECK_SIGN_SAVED_MASK 0xffffu
#define BUSTER_CHECK_SIGN_ENTER 0u
#define BUSTER_CHECK_SIGN_UNARY 1u
#define BUSTER_CHECK_SIGN_LEFT 2u
#define BUSTER_CHECK_SIGN_RIGHT 3u

static uint32_t check_common_sign_bits(unsigned __int128 bits)
{
    const uint64_t high = (uint64_t)(bits >> 64);
    const uint64_t low = (uint64_t)bits;
    const uint32_t result = high != 0 ? 128u - (uint32_t)__builtin_clzll(high) : low != 0 ? 64u - (uint32_t)__builtin_clzll(low) : 0;
    return result;
}

// The actual converted value, not an inner constant's pre-conversion sign. Negative values retain one sign bit;
// nonnegative ones are limited to the caller's truncation width before their active bits are counted.
static CheckCommonSignRange check_common_sign_constant(const CheckValue* value, uint32_t source_width, bool source_signed,
                                                      uint32_t target_width, bool target_signed, bool boolean, uint32_t cap)
{
    const unsigned __int128 source_mask = check_action_mask(source_width);
    const unsigned __int128 source = value->integer & source_mask;
    const bool negative_source = source_signed && ((source >> (source_width - 1u)) & 1u) != 0;
    const unsigned __int128 extended = negative_source ? source | ~source_mask : source;
    const unsigned __int128 mask = check_action_mask(target_width);
    const unsigned __int128 converted = boolean ? source != 0 : extended & mask;
    const bool negative = target_signed && ((converted >> (target_width - 1u)) & 1u) != 0;
    const unsigned __int128 magnitude = negative ? (~converted & mask) : converted & check_action_mask(cap);
    const CheckCommonSignRange result =
    {
        .width = check_common_sign_bits(magnitude) + (negative ? 1u : 0u),
        .nonnegative = !negative,
    };
    return result;
}

// Implicit assignment/arithmetic conversion. An explicit cast is handled as an opaque leaf instead. The exact
// constant path matters for narrowing (128 to signed char) and for converting a negative constant to unsigned.
static CheckCommonSignRange check_common_sign_convert(const CheckCommonEmission* emission, uint32_t source, TypeId target,
                                                     uint32_t cap, CheckCommonSignRange range)
{
    uint32_t width = 0;
    bool is_signed = false;
    const bool integer = check_common_integer(emission->types, target, &width, &is_signed);
    const TypeId from = type_unqualified(type_strip_aligned(emission->types, check_stream_type(emission->stream, source)));
    const TypeId to = type_unqualified(type_strip_aligned(emission->types, target));
    CheckCommonSignRange result = range;
    if (integer && from != to)
    {
        const CheckValue value = check_stream_value(emission->stream, source);
        uint32_t source_width = 0;
        bool source_signed = false;
        const bool constant = value.constant_kind == CHECK_CONSTANT_INTEGER && check_value_evaluated_constant(&value) &&
                              check_common_integer(emission->types, value.type, &source_width, &source_signed);
        const bool boolean = type_unqualified(type_strip_aligned(emission->types, target)) == BUSTER_TYPE_BOOL;
        if (constant)
        {
            result = check_common_sign_constant(&value, source_width, source_signed, width, is_signed, boolean, cap);
        }
        else if (boolean || range.width >= width)
        {
            result = (CheckCommonSignRange){ .width = width, .nonnegative = !is_signed };
        }
        else
        {
            result.nonnegative |= !is_signed;
        }
    }
    return result;
}

// The diagnostic's conventional positive-mask idiom. The spelling must be a literal one; an enum or a folded
// arithmetic value of one is not the same rule. Every stripped cast's operand precedes it in the checked graph.
static bool check_common_sign_one(const CheckCommonEmission* emission, uint32_t node)
{
    const CheckGraph* graph = &emission->input->graph;
    while (node < graph->count && (graph->ops[node] == CHECK_OP_CAST || graph->ops[node] == CHECK_OP_CONVERT || graph->ops[node] == CHECK_OP_SELECT) &&
           graph->operand0[node] < node)
    {
        node = graph->operand0[node];
    }
    const bool literal = node < graph->count && graph->ops[node] == CHECK_OP_VALUE && graph->tokens[node] < emission->parser->tokens.count &&
                         emission->parser->tokens.kinds[graph->tokens[node]] == PARSE_KIND_NUMBER;
    const CheckValue value = literal ? check_stream_value(emission->stream, node) : (CheckValue){ 0 };
    const bool result = literal && value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == 1;
    return result;
}

static CheckCommonSignRange check_common_sign_range(const CheckCommonEmission* emission, uint32_t root)
{
    const CheckGraph* graph = &emission->input->graph;
    const CheckStream* stream = emission->stream;
    const TypeTable* types = emission->types;
    const uint32_t count = graph->count < stream->count ? graph->count : stream->count;
    uint32_t root_width = 0;
    bool root_signed = false;
    const bool integer = root < count && check_common_integer(types, check_stream_type(stream, root), &root_width, &root_signed);
    const CheckCommonSignRange opaque = { .width = integer ? root_width : 128u, .nonnegative = integer && !root_signed };
    CheckCommonSignRange result = opaque;
    uint32_t* const nodes = emission->work_nodes;
    uint8_t* const phases = emission->work_kinds;
    uint32_t* const saved = emission->work_owners;
    const uint32_t first = emission->work_count;
    const uint32_t capacity = emission->work_capacity;
    uint32_t top = first;
    uint32_t remaining = integer ? root + 1u : 0;
    bool exhausted = !integer || nodes == NULL || phases == NULL || saved == NULL || top == capacity;
    if (!exhausted)
    {
        nodes[top] = root;
        phases[top] = BUSTER_CHECK_SIGN_ENTER;
        saved[top++] = root_width << BUSTER_CHECK_SIGN_CAP_SHIFT;
    }
    while (top != first && !exhausted)
    {
        const uint32_t frame = top - 1u;
        const uint32_t node = nodes[frame];
        const uint8_t phase = phases[frame];
        const uint32_t cap = saved[frame] >> BUSTER_CHECK_SIGN_CAP_SHIFT;
        const CheckOp op = graph->ops[node];
        const TypeId type = check_stream_type(stream, node);
        uint32_t width = 0;
        bool is_signed = false;
        const bool typed = check_common_integer(types, type, &width, &is_signed);
        const uint32_t left = graph->operand0[node];
        const uint32_t right = op == CHECK_OP_CONDITIONAL ? graph->operand2[node] : graph->operand1[node];
        uint32_t child = BUSTER_CHECK_NONE;
        uint32_t child_cap = cap;
        if (phase == BUSTER_CHECK_SIGN_ENTER)
        {
            exhausted = remaining == 0;
            remaining -= remaining != 0;
            const CheckValue value = check_stream_value(stream, node);
            const bool constant = typed && value.constant_kind == CHECK_CONSTANT_INTEGER && check_value_evaluated_constant(&value);
            const bool boolean = op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR ||
                                 (op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL);
            const bool conditional = op == CHECK_OP_CONDITIONAL && left < node && graph->operand1[node] < node && right < node &&
                                     graph->operand1[node] != left;
            result = (CheckCommonSignRange){ .width = typed ? width : 128u, .nonnegative = typed && !is_signed };
            if (constant)
            {
                result = check_common_sign_constant(&value, width, is_signed, width, is_signed, false, cap);
            }
            else if (typed && boolean)
            {
                result = (CheckCommonSignRange){ .width = 1u, .nonnegative = true };
            }
            else if (typed && conditional)
            {
                const CheckValue condition = check_stream_value(stream, left);
                bool truth = false;
                const bool known = check_value_evaluated_constant(&condition) && check_action_truth(types, &condition, &truth);
                child = known && !truth ? right : graph->operand1[node];
                phases[frame] = known ? BUSTER_CHECK_SIGN_UNARY : BUSTER_CHECK_SIGN_LEFT;
            }
            else if (typed && (op == CHECK_OP_ASSIGN || op == CHECK_OP_COMMA))
            {
                child = right;
                child_cap = op == CHECK_OP_ASSIGN && width < cap ? width : cap;
                phases[frame] = BUSTER_CHECK_SIGN_UNARY;
            }
            else if (typed && (op == CHECK_OP_SELECT || op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ||
                               op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_RIGHT_ASSIGN ||
                               (op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT)))
            {
                child = left;
                child_cap = (op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE) && width < cap ? width : cap;
                phases[frame] = BUSTER_CHECK_SIGN_UNARY;
            }
            else if (typed && ((op >= CHECK_OP_ADD && op <= CHECK_OP_REMAINDER) || (op >= CHECK_OP_BIT_AND && op <= CHECK_OP_BIT_XOR) ||
                               op == CHECK_OP_BIT_AND_ASSIGN))
            {
                child = left;
                child_cap = width;
                const CheckValue divisor = op == CHECK_OP_DIVIDE && right < node ? check_stream_value(stream, right) : (CheckValue){ 0 };
                const bool fixed_divisor = divisor.constant_kind == CHECK_CONSTANT_INTEGER && (divisor.flags & BUSTER_CHECK_ICE) != 0 &&
                                           check_value_evaluated_constant(&divisor);
                phases[frame] = fixed_divisor ? BUSTER_CHECK_SIGN_UNARY : BUSTER_CHECK_SIGN_LEFT;
            }
            else if (typed && op == CHECK_OP_SHIFT_LEFT && check_common_sign_one(emission, left))
            {
                result.nonnegative = true;
            }
            else if (typed && (value.flags & BUSTER_CHECK_BIT_FIELD) != 0 && value.bit_width != 0)
            {
                result.width = value.bit_width < width ? value.bit_width : width;
            }
            if (phases[frame] == BUSTER_CHECK_SIGN_ENTER)
            {
                top -= 1u;
            }
        }
        else if (phase == BUSTER_CHECK_SIGN_LEFT)
        {
            saved[frame] |= result.width * 2u + (result.nonnegative ? 1u : 0u);
            phases[frame] = BUSTER_CHECK_SIGN_RIGHT;
            child = right;
            child_cap = op == CHECK_OP_CONDITIONAL ? cap : width;
        }
        else
        {
            if (phase == BUSTER_CHECK_SIGN_RIGHT)
            {
                const uint32_t packed = saved[frame] & BUSTER_CHECK_SIGN_SAVED_MASK;
                const uint32_t first_child = op == CHECK_OP_CONDITIONAL ? graph->operand1[node] : left;
                const uint32_t conversion_cap = op == CHECK_OP_CONDITIONAL ? cap : width;
                CheckCommonSignRange first_range = { .width = packed / 2u, .nonnegative = (packed & 1u) != 0 };
                // &= computes with its original operands' promoted types. Its result type is the stored LHS's,
                // so converting the RHS to that type here would turn a positive mask such as 255 into -1 for char.
                if (op != CHECK_OP_BIT_AND_ASSIGN)
                {
                    first_range = check_common_sign_convert(emission, first_child, type, conversion_cap, first_range);
                    result = check_common_sign_convert(emission, right, type, conversion_cap, result);
                }
                const uint32_t first_bits = first_range.width - (first_range.nonnegative ? 0u : 1u);
                const uint32_t last_bits = result.width - (result.nonnegative ? 0u : 1u);
                if (op == CHECK_OP_BIT_AND || op == CHECK_OP_BIT_AND_ASSIGN)
                {
                    const uint32_t largest = first_range.width > result.width ? first_range.width : result.width;
                    const uint32_t first_limit = first_range.nonnegative ? first_range.width : largest;
                    const uint32_t last_limit = result.nonnegative ? result.width : largest;
                    result.width = first_limit < last_limit ? first_limit : last_limit;
                    result.nonnegative |= first_range.nonnegative;
                }
                else if (op == CHECK_OP_REMAINDER)
                {
                    result.nonnegative = first_range.nonnegative;
                    result.width = (first_bits < last_bits ? first_bits : last_bits) + (result.nonnegative ? 0u : 1u);
                }
                else if (op == CHECK_OP_DIVIDE)
                {
                    result.width = first_range.width;
                    result.nonnegative &= first_range.nonnegative;
                }
                else
                {
                    // Approximate diagnostic arithmetic keeps the operand ranges: it does not claim that an add,
                    // subtract or multiply is overflow-free. Ordinary conditional arms and OR/XOR use the same join.
                    result.nonnegative &= first_range.nonnegative;
                    result.width = (first_bits > last_bits ? first_bits : last_bits) + (result.nonnegative ? 0u : 1u);
                }
                result.nonnegative |= !is_signed;
                result.width = result.width < cap ? result.width : cap;
            }
            else if (op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE || op == CHECK_OP_CONDITIONAL)
            {
                uint32_t source = op == CHECK_OP_ASSIGN ? right : left;
                if (op == CHECK_OP_CONDITIONAL)
                {
                    const CheckValue condition = check_stream_value(stream, left);
                    bool truth = false;
                    check_action_truth(types, &condition, &truth);
                    source = truth ? graph->operand1[node] : right;
                }
                result = check_common_sign_convert(emission, source, type, cap, result);
            }
            else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
            {
                if (is_signed)
                {
                    const uint32_t added = op == CHECK_OP_NEGATIVE || result.nonnegative ? 1u : 0u;
                    result.width = result.width + added < cap ? result.width + added : cap;
                    result.nonnegative = false;
                }
            }
            else if (op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_RIGHT_ASSIGN || op == CHECK_OP_DIVIDE)
            {
                result = check_common_sign_convert(emission, left, type, op == CHECK_OP_DIVIDE ? width : cap, result);
                const CheckValue amount = right < node ? check_stream_value(stream, right) : (CheckValue){ 0 };
                uint32_t amount_width = 0;
                bool amount_signed = false;
                const bool fixed = amount.constant_kind == CHECK_CONSTANT_INTEGER && (amount.flags & BUSTER_CHECK_ICE) != 0 &&
                                   check_common_integer(types, amount.type, &amount_width, &amount_signed) && check_value_evaluated_constant(&amount);
                const unsigned __int128 bits = fixed ? amount.integer & check_action_mask(amount_width) : 0;
                const bool negative = fixed && amount_signed && ((bits >> (amount_width - 1u)) & 1u) != 0;
                if (fixed && (op == CHECK_OP_DIVIDE || !negative))
                {
                    const uint32_t active = op == CHECK_OP_DIVIDE ? check_common_sign_bits(bits) : 0;
                    const unsigned __int128 drop = op == CHECK_OP_DIVIDE ? (active != 0 ? active - 1u : 128u) : bits;
                    result.width = drop >= result.width ? (result.nonnegative ? 0u : 1u) : result.width - (uint32_t)drop;
                    if (op == CHECK_OP_DIVIDE && result.width > cap)
                    {
                        result.width = cap;
                    }
                }
            }
            top -= 1u;
        }
        const bool descending = phase == BUSTER_CHECK_SIGN_LEFT || (phase == BUSTER_CHECK_SIGN_ENTER && phases[frame] != BUSTER_CHECK_SIGN_ENTER);
        if (descending)
        {
            // Missing and forward edges both reject the proof, including a binary operator's second operand.
            exhausted |= child >= node || top == capacity;
            if (!exhausted)
            {
                nodes[top] = child;
                phases[top] = BUSTER_CHECK_SIGN_ENTER;
                saved[top++] = child_cap << BUSTER_CHECK_SIGN_CAP_SHIFT;
            }
        }
    }
    result = exhausted ? opaque : result;
    return result;
}
