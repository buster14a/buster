// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Constant vector expressions retain target bytes independently of scalar CheckValue payloads. The enclosing
// initializer's graph slice is visited once in postorder. A vector compound body's scalar lanes read this
// already-computed cache through a separate iterative cursor. It never calls the general initializer walker,
// keeping the complete call graph acyclic. Source records and caller arenas bound all retained values.
#ifndef BUSTER_C_CHECK_INITIALIZER_VECTOR_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_VECTOR_INCLUDED

#include "c_check_constant_bytes.c"

typedef struct CheckInitConstant CheckInitConstant;
struct CheckInitConstant
{
    const uint8_t* bytes;
    const CheckValue* scalar;
    uint32_t opener;
};

struct CheckInitConstants
{
    CheckInitConstant* entries;
    uint32_t first;
    uint32_t count;
};

typedef struct CheckInitObject CheckInitObject;
struct CheckInitObject
{
    uint32_t object;
    uint32_t end;
    TypeId type;
    const uint8_t* bytes;
};

// Only earlier explicitly initialized, nonvolatile const vectors enter this sparse table. The source phase
// owns its scratch lifetime; expression evaluation never resolves or recursively plans another declaration.
struct CheckInitObjects
{
    CheckInitObject* entries;
    uint32_t capacity;
};

static uint32_t check_init_object_slot(const CheckInitObjects* objects, uint32_t object)
{
    uint32_t slot = BUSTER_CHECK_NONE;
    if (objects != NULL && objects->capacity != 0 && object != 0)
    {
        uint32_t at = (object * UINT32_C(2654435761)) & (objects->capacity - 1);
        for (uint32_t probe = 0; slot == BUSTER_CHECK_NONE && probe < objects->capacity; probe += 1)
        {
            const uint32_t key = objects->entries[at].object;
            if (key == 0 || key == object)
            {
                slot = at;
            }
            at = (at + 1) & (objects->capacity - 1);
        }
    }
    return slot;
}

static const CheckInitConstant* check_init_constant(const CheckInitWalker* walker, uint32_t node)
{
    const CheckInitConstants* cache = walker->constants;
    const CheckInitConstant* result = NULL;
    if (cache != NULL && node >= cache->first && node - cache->first < cache->count)
    {
        result = cache->entries + node - cache->first;
    }
    return result;
}

static const uint8_t* check_init_constant_bytes(const CheckInitWalker* walker, uint32_t node)
{
    const CheckInitConstant* entry = check_init_constant(walker, node);
    const uint8_t* result = entry != NULL ? entry->bytes : NULL;
    if (result == NULL && walker->graph != NULL && node < walker->stream->count)
    {
        const CheckValue value = walker->stream->values[node];
        const TypeId type = type_strip_aligned(walker->types, value.type);
        const uint32_t slot = check_init_object_slot(walker->objects, value.object);
        if (slot != BUSTER_CHECK_NONE && value.offset == 0 && check_vector_type(walker->types, type) &&
            (type_qualifiers(type) & (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE)) == BUSTER_TYPE_CONST)
        {
            const CheckInitObject* object = walker->objects->entries + slot;
            if (object->object == value.object && object->end <= walker->graph->tokens[node] &&
                object->type == type_unqualified(type) && value.fault == CHECK_FAULT_NONE && (value.flags & BUSTER_CHECK_SIDE_EFFECT) == 0)
            {
                result = object->bytes;
            }
        }
    }
    return result;
}

static CheckValue check_init_constant_value(const CheckInitWalker* walker, uint32_t node)
{
    const CheckInitConstant* entry = check_init_constant(walker, node);
    return entry != NULL && entry->scalar != NULL ? *entry->scalar : walker->stream->values[node];
}

static bool check_init_constant_known(const CheckValue* value)
{
    return (value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING) &&
           value->fault == CHECK_FAULT_NONE && (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_DISCARDED_UNEVALUABLE)) == 0;
}

static unsigned __int128 check_init_bytes_integer(const uint8_t* bytes, uint32_t size)
{
    unsigned __int128 bits = 0;
    for (uint32_t i = 0; i < size; i += 1)
    {
        bits |= (unsigned __int128)bytes[i] << (i * 8);
    }
    return bits;
}

static bool check_init_scalar_bytes(const TypeTable* types, const CheckValue* value, uint8_t bytes[16])
{
    const TypeId type = type_unqualified(type_strip_aligned(types, value->type));
    const uint64_t size = type_size(types, type);
    bool valid = check_init_constant_known(value) && size != 0 && size <= 16;
    memset(bytes, 0, 16);
    if (valid && value->constant_kind == CHECK_CONSTANT_INTEGER)
    {
        uint32_t width = 0;
        bool sign = false;
        valid = check_action_integer_type(types, type, &width, &sign) && width <= size * 8;
        if (valid)
        {
            const unsigned __int128 mask = check_action_mask(width);
            valid = (value->integer & ~mask) == 0 && (type != BUSTER_TYPE_BOOL || value->integer <= 1);
            if (valid)
            {
                const bool negative = sign && (value->integer >> (width - 1)) != 0;
                check_data_integer_bytes(bytes, value->integer | (negative ? ~mask : 0), (uint32_t)size);
            }
        }
    }
    else if (valid)
    {
        valid = type == BUSTER_TYPE_FLOAT || type == BUSTER_TYPE_DOUBLE || type == BUSTER_TYPE_LONG_DOUBLE || check_action_half_format(type) != CHECK_HALF_NONE;
        if (valid)
        {
            const long double rounded = check_action_round_float(type, value->floating);
            valid = isnan(value->floating) || (rounded == value->floating && signbit(rounded) == signbit(value->floating));
            if (valid)
            {
                check_data_float_bytes(bytes, type, value->floating);
            }
        }
    }
    return valid;
}

static CheckValue check_init_bytes_scalar(const TypeTable* types, TypeId type, const uint8_t* bytes)
{
    type = type_unqualified(type_strip_aligned(types, type));
    CheckValue result = { .type = type, .category = CHECK_RVALUE };
    const uint64_t size = type_size(types, type);
    uint32_t width = 0;
    bool sign = false;
    if (size != 0 && size <= 16 && check_action_integer_type(types, type, &width, &sign))
    {
        result.constant_kind = CHECK_CONSTANT_INTEGER;
        result.integer = check_init_bytes_integer(bytes, (uint32_t)size) & check_action_mask(width);
    }
    else if (check_action_half_format(type) != CHECK_HALF_NONE)
    {
        result.constant_kind = CHECK_CONSTANT_FLOATING;
        result.floating = check_action_half_value(check_action_half_format(type), (uint16_t)check_init_bytes_integer(bytes, 2));
    }
    else if (type == BUSTER_TYPE_FLOAT)
    {
        const uint32_t bits = (uint32_t)check_init_bytes_integer(bytes, 4);
        float number = 0;
        memcpy(&number, &bits, sizeof(number));
        result.constant_kind = CHECK_CONSTANT_FLOATING;
        result.floating = number;
    }
    else if (type == BUSTER_TYPE_DOUBLE)
    {
        const uint64_t bits = (uint64_t)check_init_bytes_integer(bytes, 8);
        double number = 0;
        memcpy(&number, &bits, sizeof(number));
        result.constant_kind = CHECK_CONSTANT_FLOATING;
        result.floating = number;
    }
    else if (type == BUSTER_TYPE_LONG_DOUBLE)
    {
        const uint64_t significand = (uint64_t)check_init_bytes_integer(bytes, 8);
        const uint32_t field = (uint32_t)check_init_bytes_integer(bytes + 8, 2);
        const uint32_t exponent = field & 0x7fff;
        const bool negative = (field & 0x8000) != 0;
        const bool canonical = exponent == 0 ? (significand >> 63) == 0 : (significand >> 63) != 0;
        if (canonical)
        {
            const long double magnitude = exponent == 0x7fff ?
                                          (significand == UINT64_C(0x8000000000000000) ? INFINITY : __builtin_nanl("")) :
                                          ldexpl((long double)significand, exponent == 0 ? -16445 : (int)exponent - 16446);
            result.constant_kind = CHECK_CONSTANT_FLOATING;
            result.floating = copysignl(magnitude, negative ? -1.0L : 1.0L);
        }
    }
    return result;
}

static CheckValue check_init_vector_lane(const TypeTable* types, TypeId vector, const uint8_t* bytes, uint32_t lane)
{
    const TypeId element = check_vector_element(types, vector);
    CheckValue value = { .type = element, .category = CHECK_RVALUE };
    if (element == BUSTER_TYPE_BOOL)
    {
        value.constant_kind = CHECK_CONSTANT_INTEGER;
        value.integer = (bytes[lane / 8] >> (lane % 8)) & 1;
    }
    else
    {
        value = check_init_bytes_scalar(types, element, bytes + (uint64_t)lane * type_size(types, element));
    }
    return value;
}

static bool check_init_vector_store(const TypeTable* types, TypeId vector, uint8_t* bytes, uint32_t lane, CheckValue value)
{
    const TypeId element = check_vector_element(types, vector);
    check_action_convert_value(types, &value, element, BUSTER_CHECK_NONE);
    uint8_t scalar[16];
    const bool valid = check_init_scalar_bytes(types, &value, scalar);
    if (valid && element == BUSTER_TYPE_BOOL)
    {
        const uint8_t bit = (uint8_t)(1u << (lane % 8));
        bytes[lane / 8] = (uint8_t)((bytes[lane / 8] & ~bit) | (value.integer != 0 ? bit : 0));
    }
    else if (valid)
    {
        const uint64_t size = type_size(types, element);
        memcpy(bytes + size * lane, scalar, size);
    }
    return valid;
}

static const uint8_t* check_init_vector_operand(CheckInitWalker* walker, uint32_t node, TypeId target, bool integer_bitcast)
{
    const CheckValue original = check_init_constant_value(walker, node);
    const uint8_t* result = NULL;
    const uint64_t size = type_size(walker->types, target);
    if (check_vector_type(walker->types, original.type))
    {
        const bool volatile_read = original.category == CHECK_LVALUE &&
                                   ((type_qualifiers(type_strip_aligned(walker->types, original.type)) & BUSTER_TYPE_VOLATILE) != 0 ||
                                    (original.flags & BUSTER_CHECK_VOLATILE_PART) != 0);
        if (!volatile_read && check_vector_bits(walker->types, original.type) == check_vector_bits(walker->types, target))
        {
            result = check_init_constant_bytes(walker, node);
        }
    }
    else if (check_init_constant_known(&original))
    {
        uint8_t* bytes = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, uint8_t, size);
        bool valid = bytes != NULL;
        if (!valid)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, walker->graph->tokens[node]);
        }
        else
        {
            memset(bytes, 0, size);
            if (integer_bitcast)
            {
                uint8_t scalar[16];
                valid = size <= sizeof(scalar) && type_size(walker->types, original.type) == size && check_init_scalar_bytes(walker->types, &original, scalar);
                if (valid)
                {
                    memcpy(bytes, scalar, size);
                }
            }
            else
            {
                const uint32_t count = check_vector_count(walker->types, target);
                for (uint32_t lane = 0; valid && lane < count; lane += 1)
                {
                    valid = check_init_vector_store(walker->types, target, bytes, lane, original);
                }
            }
            result = valid ? bytes : NULL;
        }
    }
    return result;
}

static bool check_init_vector_literal(CheckInitWalker* walker, uint32_t node, uint32_t opener, uint8_t* bytes, uint64_t size)
{
    const Parser* parser = walker->parser;
    const TypeId type = walker->stream->values[node].type;
    const TypeId element = check_vector_element(walker->types, type);
    const uint32_t count = check_vector_count(walker->types, type);
    const uint32_t end = parser->tree.parents[opener];
    uint32_t token = opener + 1;
    uint32_t lane = 0;
    bool valid = token <= end && end < parser->tokens.count && parser->tokens.kinds[end] == PARSE_KIND_R_BRACE;
    while (valid && token < end && lane < count)
    {
        const uint32_t stop = check_init_expression_end(parser, token, end);
        uint32_t first = token;
        uint32_t last = stop;
        bool wrapped = false;
        valid = first < last && parser->tokens.kinds[first] != PARSE_KIND_PERIOD && parser->tokens.kinds[first] != PARSE_KIND_L_SQUARE;
        while (valid && first < last && parser->tokens.kinds[first] == PARSE_KIND_L_BRACE)
        {
            wrapped = true;
            const uint32_t close = check_init_excess_end(parser, first + 1, last);
            valid = close < last && close + 1 == last;
            first += 1;
            last = valid ? check_init_expression_end(parser, first, close) : first;
            const bool trailing = last < close && parser->tokens.kinds[last] == PARSE_KIND_COMMA && last + 1 == close;
            valid = valid && (walker->ignore_excess || last == close || trailing);
        }
        const uint32_t value_node = valid && first < last ? check_init_node(walker, first, last) : BUSTER_CHECK_NONE;
        CheckValue value = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER };
        if (valid && first < last)
        {
            valid = value_node < node;
            if (valid)
            {
                value = check_init_constant_value(walker, value_node);
            }
        }
        if (valid && !wrapped && lane == 0 && check_vector_type(walker->types, value.type))
        {
            CheckActionResult assignment = { 0 };
            valid = check_vector_assignable(walker->types, type, &value, &assignment);
            const uint8_t* whole = valid ? check_init_constant_bytes(walker, value_node) : NULL;
            valid = whole != NULL;
            if (valid)
            {
                memcpy(bytes, whole, size);
                lane = count;
            }
        }
        else if (valid)
        {
            const CheckAction action = {
                .op = CHECK_OP_INITIALIZE, .operands = { value }, .type = element,
                .node = value_node, .extensions = check_init_extensions(walker, first),
            };
            CheckActionResult converted = { 0 };
            check_action_scalar(walker->types, &action, &converted);
            valid = converted.diagnostic == CHECK_DIAG_NONE && check_init_vector_store(walker->types, type, bytes, lane, converted.value);
            if (converted.diagnostic == CHECK_DIAG_RESOURCE)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, first);
            }
            lane += 1;
        }
        token = stop;
        if (valid && token < end)
        {
            valid = parser->tokens.kinds[token] == PARSE_KIND_COMMA;
            token += 1;
        }
    }
    valid = valid && (token == end || walker->ignore_excess);
    return valid;
}

static bool check_init_vector_fold(CheckInitWalker* walker, uint32_t node, uint8_t* bytes)
{
    const CheckGraph* graph = walker->graph;
    const CheckOp op = graph->ops[node];
    const TypeId target = walker->stream->values[node].type;
    const uint32_t count = check_vector_count(walker->types, target);
    const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR;
    const uint32_t left_node = graph->operand0[node];
    const uint32_t right_node = binary ? graph->operand1[node] : left_node;
    const CheckNodeConversions* conversions = walker->stream->conversions != NULL ? walker->stream->conversions + node : NULL;
    TypeId left_type = conversions != NULL ? conversions->operands[0].type : 0;
    TypeId right_type = conversions != NULL ? conversions->operands[1].type : 0;
    left_type = check_vector_type(walker->types, left_type) ? left_type : walker->stream->values[left_node].type;
    right_type = binary && check_vector_type(walker->types, right_type) ? right_type : left_type;
    bool valid = check_vector_type(walker->types, left_type) && check_vector_type(walker->types, right_type) &&
                 check_vector_count(walker->types, left_type) == count && check_vector_count(walker->types, right_type) == count;
    const uint8_t* left = valid ? check_init_vector_operand(walker, left_node, left_type, false) : NULL;
    const uint8_t* right = valid && binary ? check_init_vector_operand(walker, right_node, right_type, false) : left;
    valid = valid && left != NULL && right != NULL;
    for (uint32_t lane = 0; valid && lane < count; lane += 1)
    {
        const CheckValue a = check_init_vector_lane(walker->types, left_type, left, lane);
        const CheckValue b = check_init_vector_lane(walker->types, right_type, right, lane);
        CheckValue answer = { .type = a.type, .category = CHECK_RVALUE };
        const bool logical = op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
        const bool compare = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
        const bool raw_unary = !binary && !logical && (op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE) &&
                               a.constant_kind == CHECK_CONSTANT_FLOATING;
        if (raw_unary)
        {
            const uint64_t unit = type_size(walker->types, a.type);
            memcpy(bytes + lane * unit, left + lane * unit, unit);
            if (op == CHECK_OP_NEGATIVE)
            {
                const uint64_t sign_byte = a.type == BUSTER_TYPE_LONG_DOUBLE ? 9 : unit - 1;
                bytes[lane * unit + sign_byte] ^= 0x80;
            }
        }
        else if (logical)
        {
            bool at = false;
            bool bt = false;
            valid = check_action_truth(walker->types, &a, &at) && (!binary || check_action_truth(walker->types, &b, &bt));
            answer.constant_kind = CHECK_CONSTANT_INTEGER;
            answer.type = BUSTER_TYPE_INT;
            answer.integer = op == CHECK_OP_NOT ? !at : op == CHECK_OP_LOGICAL_AND ? at && bt : at || bt;
        }
        else if (a.constant_kind == CHECK_CONSTANT_INTEGER && b.constant_kind == CHECK_CONSTANT_INTEGER)
        {
            uint32_t width = 0;
            uint32_t right_width = 0;
            bool sign = false;
            bool right_sign = false;
            valid = check_action_integer_type(walker->types, a.type, &width, &sign) &&
                    check_action_integer_type(walker->types, b.type, &right_width, &right_sign);
            if (valid)
            {
                const CheckIntegerFold fold = {
                    .active = true, .op = op, .left = a.integer, .right = b.integer, .width = width, .right_width = right_width,
                    .is_signed = sign, .right_signed = right_sign, .node = node, .extensions = BUSTER_CHECK_EXTENSION_GNU_CONSTANT,
                };
                check_action_fold_integer(fold, &answer);
            }
        }
        else if (a.constant_kind == CHECK_CONSTANT_FLOATING && b.constant_kind == CHECK_CONSTANT_FLOATING)
        {
            if (op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE)
            {
                answer = a;
                answer.floating = op == CHECK_OP_NEGATIVE ? -a.floating : a.floating;
            }
            else
            {
                check_action_fold_floating(op, &a, &b, &answer, node);
            }
        }
        else
        {
            valid = false;
        }
        if (valid && (op == CHECK_OP_NOT || compare))
        {
            answer.type = check_vector_element(walker->types, target);
            answer.integer = answer.integer != 0 ? check_action_mask((uint32_t)type_size(walker->types, answer.type) * 8) : 0;
            answer.integer = answer.type == BUSTER_TYPE_BOOL ? answer.integer != 0 : answer.integer;
        }
        valid = valid && (raw_unary || check_init_vector_store(walker->types, target, bytes, lane, answer));
    }
    return valid;
}

static bool check_init_vector_select(CheckInitWalker* walker, uint32_t node, uint8_t* bytes, CheckValue* scalar)
{
    const CheckGraph* graph = walker->graph;
    const uint32_t base_node = graph->operand0[node];
    const TypeId base_type = walker->stream->values[base_node].type;
    const uint8_t* base = check_init_constant_bytes(walker, base_node);
    const TypeId target = walker->stream->values[node].type;
    const bool vector = check_vector_type(walker->types, target);
    const uint32_t count = vector ? check_vector_count(walker->types, target) : 1;
    bool valid = check_vector_type(walker->types, base_type) && base != NULL &&
                 (type_qualifiers(type_strip_aligned(walker->types, base_type)) & BUSTER_TYPE_VOLATILE) == 0 &&
                 (walker->stream->values[base_node].flags & BUSTER_CHECK_VOLATILE_PART) == 0;
    CheckAction action = { 0 };
    if (valid)
    {
        check_graph_action(graph, walker->stream, node, NULL, &action);
    }
    for (uint32_t lane = 0; valid && lane < count; lane += 1)
    {
        uint32_t source = 0;
        if (graph->ops[node] == CHECK_OP_SUBSCRIPT)
        {
            const CheckValue index = check_init_constant_value(walker, graph->operand1[node]);
            valid = check_init_constant_known(&index) && index.constant_kind == CHECK_CONSTANT_INTEGER &&
                    index.integer < check_vector_count(walker->types, base_type) && graph->ops[base_node] == CHECK_OP_VALUE;
            source = (uint32_t)index.integer;
        }
        else
        {
            const CheckVectorMember* member = action.vector_member;
            valid = member != NULL && (walker->stream->values[base_node].category != CHECK_LVALUE ||
                    (!vector && graph->ops[base_node] != CHECK_OP_MEMBER));
            if (valid)
            {
                const uint32_t half = (check_vector_count(walker->types, base_type) + 1) / 2;
                source = member->kind == CHECK_VECTOR_MEMBER_LANES ? member->lanes[lane] :
                         member->kind == CHECK_VECTOR_MEMBER_LO ? lane : member->kind == CHECK_VECTOR_MEMBER_HI ? lane + half :
                         member->kind == CHECK_VECTOR_MEMBER_EVEN ? lane * 2 : lane * 2 + 1;
            }
        }
        if (valid)
        {
            // The special half selections may name a padded lane of a three-element vector. Its bytes were
            // initialized to zero, so no host or arena padding can leak into the selected representation.
            const uint64_t unit = type_size(walker->types, check_vector_element(walker->types, base_type));
            valid = check_vector_element(walker->types, base_type) == BUSTER_TYPE_BOOL ? source < check_vector_count(walker->types, base_type) :
                    unit != 0 && source < type_size(walker->types, base_type) / unit;
        }
        if (valid)
        {
            const CheckValue value = check_init_vector_lane(walker->types, base_type, base, source);
            if (vector)
            {
                const uint64_t unit = type_size(walker->types, value.type);
                memcpy(bytes + lane * unit, base + source * unit, unit);
            }
            else
            {
                *scalar = value;
            }
        }
    }
    return valid;
}

static void check_init_constants(CheckInitWalker* walker, uint32_t begin, uint32_t end, uint32_t root, CheckInitConstants* cache)
{
    const CheckGraph* graph = walker->graph;
    uint32_t first = root;
    for (uint32_t token = begin; token < end; token += 1)
    {
        const uint32_t node = walker->token_nodes[token];
        first = node < first ? node : first;
    }
    cache->entries = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitConstant, (uint64_t)root - first + 1);
    if (cache->entries == NULL)
    {
        check_init_fail(walker, CHECK_INIT_RESOURCE, begin);
    }
    else
    {
        cache->first = first;
        cache->count = root - first + 1;
        for (uint32_t i = 0; i < cache->count; i += 1)
        {
            cache->entries[i] = (CheckInitConstant){ .opener = BUSTER_CHECK_NONE };
        }
    }
    for (uint32_t token = begin; walker->result->status == CHECK_INIT_OK && token < end; token += 1)
    {
        if (walker->parser->tree.roles[token] == PARSE_ROLE_OPEN_COMPOUND_BODY)
        {
            const uint32_t close = walker->parser->tree.parents[token];
            const uint32_t node = close < end ? check_init_node(walker, close, close + 1) : BUSTER_CHECK_NONE;
            if (node >= first && node <= root)
            {
                cache->entries[node - first].opener = token;
            }
        }
    }
    const CheckInitConstants* previous = walker->constants;
    walker->constants = cache;
    for (uint32_t node = first; walker->result->status == CHECK_INIT_OK && node <= root; node += 1)
    {
        const CheckValue checked = walker->stream->values[node];
        CheckInitConstant* entry = cache->entries + node - first;
        const CheckOp op = graph->ops[node];
        const bool vector = check_vector_type(walker->types, checked.type);
        const bool leaf = vector && op == CHECK_OP_VALUE && entry->opener != BUSTER_CHECK_NONE;
        const bool unary = op >= CHECK_OP_POSITIVE && op <= CHECK_OP_BIT_NOT;
        const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_LOGICAL_OR;
        const bool convert = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE || op == CHECK_OP_SELECT;
        const bool select = op == CHECK_OP_MEMBER || op == CHECK_OP_SUBSCRIPT;
        // Automatic compound leaves carry their runtime materialization effect, inherited by enclosing
        // operations. Prove their value from the actual constant lanes instead of erasing graph flags or
        // rejecting that marker wholesale. Calls, updates and runtime scalar operands cannot fill this cache.
        const bool clean = walker->stream->diagnostics[node] == CHECK_DIAG_NONE && checked.fault == CHECK_FAULT_NONE;
        if (clean && (vector || leaf || ((unary || binary || convert || select) && checked.constant_kind == CHECK_CONSTANT_NONE)))
        {
            const uint64_t allocation = walker->scratch->length;
            const uint64_t size = vector || leaf ? type_size(walker->types, checked.type) : 0;
            uint8_t* bytes = size != 0 ? BUSTER_ARENA_PUSH_ARRAY(walker->scratch, uint8_t, size) : NULL;
            CheckValue* scalar = size == 0 ? BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckValue, 1) : NULL;
            const uint64_t temporary = walker->scratch->length;
            bool valid = size != 0 ? bytes != NULL : scalar != NULL;
            if (!valid)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, graph->tokens[node]);
            }
            else
            {
                if (bytes != NULL)
                {
                    memset(bytes, 0, size);
                }
                else
                {
                    *scalar = (CheckValue){ .type = checked.type, .category = CHECK_RVALUE };
                }
                const uint32_t source = graph->operand0[node];
                if (leaf)
                {
                    valid = check_init_vector_literal(walker, node, entry->opener, bytes, size);
                }
                else if (select)
                {
                    valid = source < node && check_init_vector_select(walker, node, bytes, scalar);
                }
                else if (vector && (unary || binary))
                {
                    valid = source < node && (!binary || graph->operand1[node] < node) && check_init_vector_fold(walker, node, bytes);
                }
                else if (vector && (convert || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_COMMA))
                {
                    uint32_t selected = source;
                    if (op == CHECK_OP_CONDITIONAL)
                    {
                        bool truth = false;
                        const CheckValue condition = source < node ? check_init_constant_value(walker, source) : (CheckValue){ 0 };
                        valid = source < node && condition.fault == CHECK_FAULT_NONE && (condition.flags & BUSTER_CHECK_SIDE_EFFECT) == 0 &&
                                check_action_truth(walker->types, &condition, &truth);
                        selected = truth ? graph->operand1[node] : graph->operand2[node];
                    }
                    else if (op == CHECK_OP_COMMA)
                    {
                        CheckAction action = { 0 };
                        CheckActionResult converted = { 0 };
                        check_graph_action(graph, walker->stream, node, NULL, &action);
                        if (source < node)
                        {
                            action.operands[0] = check_init_constant_value(walker, source);
                        }
                        const CheckValue left = check_action_operand(walker->types, &action, &converted, 0);
                        valid = source < node && converted.diagnostic == CHECK_DIAG_NONE &&
                                (check_init_constant_known(&left) || check_init_constant_bytes(walker, source) != NULL ||
                                 (left.constant_kind == CHECK_CONSTANT_ADDRESS &&
                                  (left.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_SIDE_EFFECT)) == BUSTER_CHECK_STATIC_OBJECT));
                        selected = graph->operand1[node];
                    }
                    const bool bitcast = op == CHECK_OP_CAST && check_action_kind(walker->types, checked.type) == BUSTER_TYPE_KIND_VECTOR;
                    const uint8_t* converted = valid && selected < node ? check_init_vector_operand(walker, selected, checked.type, bitcast) : NULL;
                    valid = converted != NULL;
                    if (valid)
                    {
                        memcpy(bytes, converted, size);
                    }
                }
                else if (!vector && convert && source < node && check_vector_type(walker->types, walker->stream->values[source].type))
                {
                    // Clang accepts this bitcast as an ordinary expression, but excludes vector-to-scalar
                    // reinterpretation from C static constant expressions. Keep that source constraint here.
                    valid = false;
                }
                else if (!vector && (unary || binary || convert))
                {
                    CheckAction action = { 0 };
                    check_graph_action(graph, walker->stream, node, NULL, &action);
                    const uint32_t operands[] = { source, graph->operand1[node], graph->operand2[node] };
                    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
                    {
                        if (operands[i] < node)
                        {
                            action.operands[i] = check_init_constant_value(walker, operands[i]);
                        }
                    }
                    CheckActionResult folded = { 0 };
                    check_action_scalar(walker->types, &action, &folded);
                    valid = folded.diagnostic == CHECK_DIAG_NONE;
                    *scalar = folded.value;
                }
                else
                {
                    valid = false;
                }
                valid = valid && (scalar == NULL || check_init_constant_known(scalar));
            }
            arena_pop_to(walker->scratch, temporary);
            if (valid)
            {
                entry->bytes = bytes;
                entry->scalar = scalar;
            }
            else
            {
                arena_pop_to(walker->scratch, allocation);
            }
        }
    }
    walker->constants = previous;
}

static void check_init_vector_parts(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t token, uint32_t node, const uint8_t* bytes)
{
    const uint64_t size = type_size(walker->types, selected->type);
    const uint64_t source_size = type_size(walker->types, walker->stream->values[node].type);
    CheckActionResult assignment = { 0 };
    if (size != source_size || !check_vector_assignable(walker->types, selected->type, walker->stream->values + node, &assignment))
    {
        check_init_fail(walker, CHECK_INIT_INVALID, token);
    }
    for (uint64_t offset = 0; walker->result->status == CHECK_INIT_OK && offset < size;)
    {
        const uint64_t remaining = size - offset;
        const uint32_t unit = remaining >= 16 ? 16 : remaining >= 8 ? 8 : remaining >= 4 ? 4 : remaining >= 2 ? 2 : 1;
        const TypeId type = unit == 16 ? BUSTER_TYPE_UINT128 : unit == 8 ? BUSTER_TYPE_ULLONG : unit == 4 ? BUSTER_TYPE_UINT :
                            unit == 2 ? BUSTER_TYPE_USHORT : BUSTER_TYPE_UCHAR;
        const CheckInitPart part = {
            .kind = CHECK_INIT_VALUE, .type = type, .node = node, .token = token, .bit_offset = selected->offset + offset * 8,
            .value = { .type = type, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER,
                       .integer = check_init_bytes_integer(bytes + offset, unit) },
        };
        check_init_append(walker, &part);
        offset += unit;
    }
}

#endif
