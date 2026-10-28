// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Ordinary expression fragments (#624), included by c_check_program_lower.c.
// Partial vectors classify operators, classes and facts into register words. Reached leaves supply payloads;
// occupied-slot masks expand their templates into typed columns. The original task loop is the exact twin.

#define BUSTER_CHECK_LOWER_BATCH_VALUE 254u
#define BUSTER_CHECK_LOWER_BATCH_WRAPPER 255u

static const uint8_t check_lower_batch_ops[64] __attribute__((aligned(64))) = {
    [CHECK_OP_VALUE] = BUSTER_CHECK_LOWER_BATCH_VALUE,
    [CHECK_OP_POSITIVE] = BUSTER_CHECK_LOWER_BATCH_WRAPPER,
    [CHECK_OP_NEGATIVE] = BUSTER_TYPED_NEG,
    [CHECK_OP_BIT_NOT] = BUSTER_TYPED_NOT,
    [CHECK_OP_ADD] = BUSTER_TYPED_ADD,
    [CHECK_OP_SUBTRACT] = BUSTER_TYPED_SUB,
    [CHECK_OP_MULTIPLY] = BUSTER_TYPED_MUL,
    [CHECK_OP_DIVIDE] = BUSTER_TYPED_DIV,
    [CHECK_OP_REMAINDER] = BUSTER_TYPED_MOD,
    [CHECK_OP_SHIFT_LEFT] = BUSTER_TYPED_SHL,
    [CHECK_OP_SHIFT_RIGHT] = BUSTER_TYPED_SHR,
    [CHECK_OP_LESS] = BUSTER_TYPED_LT,
    [CHECK_OP_LESS_EQUAL] = BUSTER_TYPED_LE,
    [CHECK_OP_GREATER] = BUSTER_TYPED_GT,
    [CHECK_OP_GREATER_EQUAL] = BUSTER_TYPED_GE,
    [CHECK_OP_EQUAL] = BUSTER_TYPED_EQ,
    [CHECK_OP_NOT_EQUAL] = BUSTER_TYPED_NE,
    [CHECK_OP_BIT_AND] = BUSTER_TYPED_AND,
    [CHECK_OP_BIT_OR] = BUSTER_TYPED_OR,
    [CHECK_OP_BIT_XOR] = BUSTER_TYPED_XOR,
    [CHECK_OP_CAST] = BUSTER_CHECK_LOWER_BATCH_WRAPPER,
    [CHECK_OP_DEREFERENCE] = BUSTER_TYPED_LOAD,
    [CHECK_OP_CONVERT] = BUSTER_CHECK_LOWER_BATCH_WRAPPER,
    [CHECK_OP_SELECT] = BUSTER_CHECK_LOWER_BATCH_WRAPPER,
};

// Classify at most 16 immutable operator/type records. The register result avoids an intermediate plan
// or speculative payload walk; production callers supply the reached two/three-node fragment.
static __attribute__((noinline)) __m512i check_lower_batch_classify(CheckLowerWriter* writer, uint32_t first, uint32_t count)
{
    const CheckStream* stream = writer->program->stream;
    const CheckGraph* graph = &writer->program->input->graph;
    __m512i result;
    if (count <= 4)
    {
        uint32_t metadata[4] = { 0 };
#pragma clang loop vectorize(disable) unroll_count(4)
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t index = type_index(stream->values[first + i].type);
            metadata[i] = index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
        }
        const __mmask8 active = (__mmask8)((1u << count) - 1);
        const __m128i ops = _mm_maskz_loadu_epi32(active, graph->ops + first);
        const __mmask8 small = active & _mm_cmplt_epu32_mask(ops, _mm_set1_epi32(64));
        const __m256i keys = _mm256_zextsi128_si256(_mm_cvtepi32_epi8(ops));
        const __m256i selected = _mm256_permutex2var_epi8(_mm256_load_si256((const __m256i*)check_lower_batch_ops), keys,
                                                        _mm256_load_si256((const __m256i*)(check_lower_batch_ops + 32)));
        const __m128i recipes = _mm_maskz_mov_epi32(small, _mm_cvtepu8_epi32(_mm256_castsi256_si128(selected)));
        result = _mm512_zextsi128_si512(_mm_or_si128(_mm_loadu_si128((const __m128i*)metadata), _mm_slli_epi32(recipes, 16)));
    }
    else
    {
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        const __m512i offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32(sizeof(CheckValue)));
        const __m512i types = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, offsets, stream->values + first, 1);
        const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
        const __mmask16 typed = active & _mm512_cmplt_epu32_mask(indices, _mm512_set1_epi32((int)writer->type_count));
        const __m512i metadata = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), typed, indices, writer->type_templates, 4);
        const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + first);
        const __mmask16 small = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
        const __m512i keys = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
        const __m128i selected = _mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, _mm512_load_si512(check_lower_batch_ops)));
        const __m512i recipes = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(selected));
        result = _mm512_or_si512(metadata, _mm512_slli_epi32(recipes, 16));
    }
    writer->classified_nodes += count;
    return result;
}

static bool check_lower_batch_proof(const CheckValue* value, CheckOp op)
{
    return value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
           (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && value->category == CHECK_RVALUE &&
           op != CHECK_OP_STATEMENT_VALUE && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY;
}

// These nodes only schedule one checked operand. Their operation-specific helpers own all conversions,
// truth tests and effects; unrelated expression classes and operands need not be loaded first.
static inline __attribute__((always_inline)) bool check_lower_plain(CheckLowerWriter* writer, uint32_t node, bool address)
{
    const CheckStream* stream = writer->program->stream;
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckOp op = graph->ops[node];
    const bool wrapper = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE || op == CHECK_OP_SELECT;
    const bool truth = op == CHECK_OP_NOT || op == CHECK_OP_CONDITION;
    const bool plain = (wrapper || truth) && stream->values[node].constant_kind == CHECK_CONSTANT_NONE;
    if (plain)
    {
        writer->token = graph->tokens[node];
        if (wrapper)
        {
            check_lower_operand(writer, node, 0, op == CHECK_OP_SELECT && address, op == CHECK_OP_SELECT ? 0 : stream->values[node].type);
        }
        else
        {
            const TypeId type = stream->conversions[node].operands[0].type;
            check_lower_truth(writer, type, op == CHECK_OP_NOT);
            check_lower_operand(writer, node, 0, false, type);
        }
    }
    return plain;
}

// The scalar payload of a reached leaf. A two/three-node template calls this only after its parent has
// proved the original evaluation order. A neighboring body's speculative type classification never reads its map.
static inline __attribute__((always_inline)) bool check_lower_value_payload(const CheckLowerWriter* writer, uint32_t node, uint32_t metadata, bool initialized, uint8_t* kind, uint64_t* bits)
{
    const CheckProgram* program = writer->program;
    const CheckValue* value = program->stream->values + node;
    const CheckOp op = program->input->graph.ops[node];
    const uint8_t class = (uint8_t)metadata;
    const uint32_t object = value->object;
    const bool known = object != 0 && object < program->object_count;
    // Not a _BitInt's, whose values check_lower_bit_int_node extends from its width: so no template takes an expression of
    // one, whose operands are of one or convert to one (check_lower_conversion_empty).
    bool valid = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 &&
                 (metadata & ((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT) << 8)) == 0 &&
                 value->overload_head == 0;
    valid = valid && (initialized || op != CHECK_OP_VALUE || !known || !program->input->compound_objects[object - 1] || program->input->static_objects[object - 1]);
    *kind = 0;
    *bits = 0;
    if (valid && check_lower_batch_proof(value, op))
    {
        valid = value->constant_kind == CHECK_CONSTANT_INTEGER;
        if (valid)
        {
            *kind = BUSTER_TYPED_CONST;
            *bits = (uint64_t)value->integer;
            if (class <= BUSTER_TYPED_CLASS_U32)
            {
                const uint32_t width = 8u << ((class - 1) / 2);
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                *bits &= mask;
                *bits |= (class & 1) != 0 && (*bits >> (width - 1)) != 0 ? ~mask : 0;
            }
        }
    }
    else if (valid && op == CHECK_OP_VALUE && value->category == CHECK_LVALUE && known)
    {
        const uint32_t local = writer->local_map[object];
        const SymbolId symbol = program->object_symbols[object];
        valid = local != BUSTER_CHECK_NONE || symbol != 0;
        *kind = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
        *bits = local != BUSTER_CHECK_NONE ? local : symbol;
    }
    else
    {
        valid = false;
    }
    return valid;
}

static bool check_lower_batch_target(const CheckLowerWriter* writer, TypeId target, bool pointer)
{
    const uint32_t index = type_index(target);
    const uint32_t metadata = index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
    const uint32_t forbidden = (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_ATOMIC |
                                (pointer ? BUSTER_CHECK_LOWER_TYPE_POINTER : 0)) << 8;
    return target == 0 || ((uint8_t)metadata <= BUSTER_TYPED_CLASS_F64 && (metadata & forbidden) == 0);
}

// Up to three native entry slots, one per reached node. A wrapper has an empty final slot. Their prefix
// counts are the popcounts of the occupied slot masks; compress expands those templates into all three columns.
static void check_lower_batch_write(CheckLowerWriter* writer, uint32_t kinds, uint32_t classes, uint64_t left, uint64_t right, uint8_t occupied)
{
    const uint32_t count = (uint32_t)__builtin_popcount(occupied);
    const __mmask8 prefix = (__mmask8)((1u << count) - 1);
    const __m128i kind_bytes = _mm_maskz_compress_epi8(occupied, _mm_cvtsi32_si128((int)kinds));
    const __m128i class_bytes = _mm_maskz_compress_epi8(occupied, _mm_cvtsi32_si128((int)classes));
    _mm_mask_storeu_epi8(writer->kinds + writer->count, prefix, kind_bytes);
    _mm_mask_storeu_epi8(writer->classes + writer->count, prefix, class_bytes);
    // The unary's skipped middle value is zero, as is its native entry's payload.
    _mm256_mask_storeu_epi64(writer->values + writer->count, prefix, _mm256_setr_epi64x((int64_t)left, (int64_t)right, 0, 0));
    writer->count += count;
}

// Scalar leaf templates share their payload rule with joined nodes. Standalone VALUE tasks retain
// the original expression handler; the hot loop uses this helper for proven constants of other ops.
static inline __attribute__((always_inline)) bool check_lower_value(CheckLowerWriter* writer, uint32_t node, bool address, bool initialized)
{
    const CheckProgram* program = writer->program;
    const CheckValue* value = program->stream->values + node;
    bool valid = false;
    const CheckOp op = program->input->graph.ops[node];
    if (op == CHECK_OP_VALUE || (!address && check_lower_batch_proof(value, op)))
    {
        const uint32_t object = value->object;
        const bool known = object != 0 && object < program->object_count;
        const bool compound = op == CHECK_OP_VALUE && !initialized && known && program->input->compound_objects[object - 1] && !program->input->static_objects[object - 1];
        if (op == CHECK_OP_VALUE && address && value->category == CHECK_LVALUE && value->type != BUSTER_TYPE_VOID && value->overload_head == 0 && known && !compound)
        {
            writer->token = program->input->graph.tokens[node];
            check_lower_address(writer, object, value->offset);
            valid = true;
        }
        else if (!address)
        {
            const uint32_t index = type_index(value->type);
            const uint32_t metadata = index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
            uint8_t kind = 0;
            uint64_t bits = 0;
            valid = check_lower_value_payload(writer, node, metadata, initialized, &kind, &bits);
            if (valid)
            {
                writer->token = program->input->graph.tokens[node];
                check_lower_emit(writer, kind, (uint8_t)metadata, bits);
            }
        }
    }
    return valid;
}

static inline __attribute__((always_inline)) bool check_lower_batch_expression(CheckLowerWriter* writer, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckValue* value = program->stream->values + node;
    const CheckOp op = graph->ops[node];
    const uint8_t recipe = op < 64 ? check_lower_batch_ops[op] : 0;
    const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR;
    const bool pointer = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
    const uint32_t nodes = 2 + binary;
    const uint32_t entries = 1 + binary + (recipe != BUSTER_CHECK_LOWER_BATCH_WRAPPER);
    const uint32_t a = graph->operand0[node];
    const uint32_t b = binary ? graph->operand1[node] : 0;
    const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
    bool valid = recipe != 0 && recipe != BUSTER_CHECK_LOWER_BATCH_VALUE && value->overload_head == 0 && !check_lower_batch_proof(value, op) &&
                 node >= 1 + binary && a + 1 + binary == node && (!binary || b + 1 == node) && writer->count <= room &&
                 entries <= room - writer->count && writer->depth <= writer->capacity && 5 <= writer->capacity - writer->depth;
    if (valid)
    {
        const TypeId left = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE ? value->type :
                            op == CHECK_OP_SELECT ? 0 : program->stream->conversions[node].operands[0].type;
        const TypeId right = binary ? program->stream->conversions[node].operands[1].type : 0;
        valid = (graph->ops[a] == CHECK_OP_VALUE || program->stream->values[a].constant_kind == CHECK_CONSTANT_INTEGER) &&
                (!binary || graph->ops[b] == CHECK_OP_VALUE || program->stream->values[b].constant_kind == CHECK_CONSTANT_INTEGER) &&
                (left == 0 || left == program->stream->values[a].type || check_lower_conversion_empty(writer, program->stream->values[a].type, left)) &&
                (!binary || right == 0 || right == program->stream->values[b].type || check_lower_conversion_empty(writer, program->stream->values[b].type, right)) &&
                check_lower_batch_target(writer, left, pointer) && (!binary || check_lower_batch_target(writer, right, pointer));
        if (valid)
        {
            const __m128i classified = _mm512_castsi512_si128(check_lower_batch_classify(writer, a, nodes));
            const uint32_t a_info = (uint32_t)_mm_cvtsi128_si32(classified);
            const uint32_t b_info = (uint32_t)_mm_extract_epi32(classified, 1);
            const uint32_t root_info = binary ? (uint32_t)_mm_extract_epi32(classified, 2) : b_info;
            const uint8_t kind = (uint8_t)(root_info >> 16);
            const uint8_t class = (uint8_t)root_info;
            uint8_t a_kind = 0;
            uint8_t b_kind = 0;
            uint64_t a_bits = 0;
            uint64_t b_bits = 0;
            valid = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 &&
                    (root_info & ((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_ATOMIC) << 8)) == 0 &&
                    check_lower_value_payload(writer, a, a_info, false, &a_kind, &a_bits) &&
                    (!binary || check_lower_value_payload(writer, b, b_info, false, &b_kind, &b_bits));
            if (valid)
            {
                const uint32_t kinds = a_kind | (uint32_t)b_kind << 8 | (uint32_t)(kind == BUSTER_CHECK_LOWER_BATCH_WRAPPER ? 0 : kind) << 16;
                const uint32_t classes = (uint8_t)a_info | (binary ? (uint32_t)(uint8_t)b_info << 8 : 0) | (uint32_t)class << 16;
                const uint8_t occupied = 1 | (binary ? 2 : 0) | (kind != BUSTER_CHECK_LOWER_BATCH_WRAPPER ? 4 : 0);
                check_lower_batch_write(writer, kinds, classes, a_bits, b_bits, occupied);
                writer->token = graph->tokens[binary ? b : a];
                writer->expression_nodes += nodes;
                writer->batch_nodes += nodes;
            }
        }
    }
    return valid;
}
