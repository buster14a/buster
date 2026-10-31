// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Root-owned, bounded postorder fragments (#868), included after the leaf template helpers.
// The reverse walk proves a closed executable tree before any local map is read. Only then are its immutable
// records classified and its occupied entry slots compacted. Failure publishes nothing; the task loop is the twin.

#if BUSTER_HOST_VECTOR
#define BUSTER_CHECK_LOWER_FRAGMENT_NODES 16u

typedef enum CheckLowerFragmentResult
{
    CHECK_LOWER_FRAGMENT_OK,
    CHECK_LOWER_FRAGMENT_ORDER,
    CHECK_LOWER_FRAGMENT_BOUND,
    CHECK_LOWER_FRAGMENT_OPERATOR,
    CHECK_LOWER_FRAGMENT_EFFECT,
    CHECK_LOWER_FRAGMENT_TYPE,
    CHECK_LOWER_FRAGMENT_CONVERSION,
    CHECK_LOWER_FRAGMENT_PAYLOAD,
    CHECK_LOWER_FRAGMENT_CAPACITY,
} CheckLowerFragmentResult;

// The initial extension is deliberately integer-only. Shifts retain check_lower_count, and loads, pointers,
// floating and narrow arithmetic retain their operation-specific expansion. A folded node is a leaf instead.
static bool check_lower_fragment_operator(CheckOp op)
{
    return op < 64 && check_lower_batch_ops[op] != 0 && (op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT ||
           (op >= CHECK_OP_ADD && op <= CHECK_OP_REMAINDER) || (op >= CHECK_OP_LESS && op <= CHECK_OP_BIT_XOR) ||
           op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_SELECT);
}

static __attribute__((noinline)) bool check_lower_fragment(CheckLowerWriter* writer, uint32_t root)
{
    const CheckProgram* program = writer->program;
    const CheckStream* stream = program->stream;
    const CheckGraph* graph = &program->input->graph;
    uint32_t pending[BUSTER_CHECK_LOWER_FRAGMENT_NODES] = { root };
    uint32_t visited[BUSTER_CHECK_LOWER_FRAGMENT_NODES];
    uint32_t depth = 1;
    uint32_t count = 0;
    uint32_t leaves = 0;
    uint32_t unproved = 0;
    uint32_t last_leaf = BUSTER_CHECK_NONE;
    CheckLowerFragmentResult result = CHECK_LOWER_FRAGMENT_OK;
    // The structural pass reads only the op, its constant kind and its operand edges. Constant kind alone decides
    // whether a folded node can prove leaf here; the full proof and the effect reads wait for pass two, so a walk
    // that fails on order or depth costs one or two lines per node instead of the whole value record.
    while (result == CHECK_LOWER_FRAGMENT_OK && depth != 0 && count < BUSTER_CHECK_LOWER_FRAGMENT_NODES)
    {
        const uint32_t node = pending[--depth];
        if (count > root || node != root - count || node >= stream->count || node >= graph->count)
        {
            result = CHECK_LOWER_FRAGMENT_ORDER;
        }
        else
        {
            const CheckOp op = graph->ops[node];
            const CheckValue* value = stream->values + node;
            const bool supported = check_lower_fragment_operator(op);
            bool leaf = op == CHECK_OP_VALUE;
            if (!leaf && value->constant_kind != CHECK_CONSTANT_NONE)
            {
                leaf = check_lower_batch_proof(value, op);
            }
            visited[count] = node;
            leaves = (leaves << 1) | leaf;
            unproved = (unproved << 1) | (!leaf && !supported);
            count += 1;
            if (leaf)
            {
                last_leaf = last_leaf == BUSTER_CHECK_NONE ? node : last_leaf;
            }
            else if (!supported)
            {
                // Unsupported operators cannot grow the walk; keeping them provisional leaves lets deeper pops
                // still report ORDER, after which pass two returns OPERATOR for the first unprovable one.
            }
            else
            {
                const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR;
                const uint32_t a = graph->operand0[node];
                const uint32_t b = binary ? graph->operand1[node] : 0;
                if (a >= node || (binary && b >= node))
                {
                    result = CHECK_LOWER_FRAGMENT_ORDER;
                }
                else if (depth + 1 + binary > BUSTER_CHECK_LOWER_FRAGMENT_NODES)
                {
                    result = CHECK_LOWER_FRAGMENT_BOUND;
                }
                else
                {
                    // Reverse postorder visits the right subtree first. Matching each popped identity to the next
                    // index rejects gaps, repeated/reordered operands and a folded child's skipped descendants.
                    pending[depth++] = a;
                    if (binary)
                    {
                        pending[depth++] = b;
                    }
                }
            }
        }
    }
    // Effects and unprovable operators are settled in pop order only for runs the structure already proved; the
    // verdicts are identical to the inline checks this pass replaces.
    for (uint32_t i = 0; result == CHECK_LOWER_FRAGMENT_OK && i < count; i += 1)
    {
        const uint32_t node = visited[i];
        const CheckValue* value = stream->values + node;
        if (value->overload_head != 0 || value->fault != CHECK_FAULT_NONE ||
            (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) != 0)
        {
            result = CHECK_LOWER_FRAGMENT_EFFECT;
        }
        else if ((unproved & (1u << i)) != 0)
        {
            result = CHECK_LOWER_FRAGMENT_OPERATOR;
        }
    }
    if (result == CHECK_LOWER_FRAGMENT_OK && depth != 0)
    {
        result = CHECK_LOWER_FRAGMENT_BOUND;
    }
    if (result == CHECK_LOWER_FRAGMENT_OK && count < 3)
    {
        result = CHECK_LOWER_FRAGMENT_ORDER;
    }
    uint8_t kinds[BUSTER_CHECK_LOWER_FRAGMENT_NODES] = { 0 };
    uint8_t classes[BUSTER_CHECK_LOWER_FRAGMENT_NODES] = { 0 };
    uint64_t values[BUSTER_CHECK_LOWER_FRAGMENT_NODES] = { 0 };
    uint32_t occupied = 0;
    if (result == CHECK_LOWER_FRAGMENT_OK)
    {
        const uint32_t first = root + 1 - count;
        uint32_t metadata[BUSTER_CHECK_LOWER_FRAGMENT_NODES];
        const __m512i classified = check_lower_batch_classify(writer, first, count);
        _mm512_storeu_si512(metadata, classified);
        const __mmask16 active = (__mmask16)((1u << count) - 1);
        const __m512i class = _mm512_and_si512(classified, _mm512_set1_epi32(UINT8_MAX));
        const uint32_t forbidden = (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX |
                                   BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_POINTER |
                                   BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT |
                                   BUSTER_CHECK_LOWER_TYPE_VOLATILE_COPY) << 8;
        const __mmask16 invalid = _mm512_cmplt_epu32_mask(class, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I32)) |
                                 _mm512_cmpgt_epu32_mask(class, _mm512_set1_epi32(BUSTER_TYPED_CLASS_U64)) |
                                 _mm512_cmpge_epu32_mask(class, _mm512_set1_epi32(writer->class_limit)) |
                                 _mm512_test_epi32_mask(classified, _mm512_set1_epi32((int)forbidden));
        if ((active & invalid) != 0)
        {
            result = CHECK_LOWER_FRAGMENT_TYPE;
        }
        _mm_storeu_si128((__m128i*)classes, _mm512_cvtepi32_epi8(class));
        for (uint32_t i = 0; result == CHECK_LOWER_FRAGMENT_OK && i < count; i += 1)
        {
            const uint32_t node = first + i;
            if ((leaves & (1u << i)) != 0)
            {
                if (!check_lower_value_payload(writer, node, metadata[i], false, kinds + i, values + i))
                {
                    result = CHECK_LOWER_FRAGMENT_PAYLOAD;
                }
                occupied |= 1u << i;
            }
            else
            {
                const CheckOp op = graph->ops[node];
                const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR;
                const bool wrapper = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE;
                for (uint32_t edge = 0; result == CHECK_LOWER_FRAGMENT_OK && edge < 1u + binary; edge += 1)
                {
                    const uint32_t child = edge == 0 ? graph->operand0[node] : graph->operand1[node];
                    const TypeId from = stream->values[child].type;
                    const TypeId to = wrapper ? stream->values[node].type : op == CHECK_OP_SELECT ? 0 : stream->conversions[node].operands[edge].type;
                    if (!check_lower_batch_target(writer, to, true) || (to != 0 && from != to && !check_lower_conversion_empty(writer, from, to)))
                    {
                        result = CHECK_LOWER_FRAGMENT_CONVERSION;
                    }
                }
                const uint8_t recipe = (uint8_t)(metadata[i] >> 16);
                if (recipe != BUSTER_CHECK_LOWER_BATCH_WRAPPER)
                {
                    kinds[i] = recipe;
                    occupied |= 1u << i;
                }
            }
        }
    }
    const uint32_t entries = (uint32_t)__builtin_popcount(occupied);
    const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
    if (result == CHECK_LOWER_FRAGMENT_OK && (writer->count > room || entries > room - writer->count ||
        writer->depth > writer->capacity || 3 * count > writer->capacity - writer->depth))
    {
        // At most an instruction and two operand tasks per supported node. Although this path uses no tasks,
        // reserve that conservative upper bound so it cannot turn a scalar task-capacity failure into success.
        result = CHECK_LOWER_FRAGMENT_CAPACITY;
    }
    if (result == CHECK_LOWER_FRAGMENT_OK)
    {
        const __mmask16 prefix = (__mmask16)((1u << entries) - 1);
        _mm_mask_storeu_epi8(writer->kinds + writer->count, prefix, _mm_maskz_compress_epi8((__mmask16)occupied, _mm_loadu_si128((const __m128i*)kinds)));
        _mm_mask_storeu_epi8(writer->classes + writer->count, prefix, _mm_maskz_compress_epi8((__mmask16)occupied, _mm_loadu_si128((const __m128i*)classes)));
        const uint32_t low_count = (uint32_t)__builtin_popcount(occupied & UINT8_MAX);
        const uint32_t high_count = entries - low_count;
        const __m512i low = _mm512_maskz_compress_epi64((__mmask8)occupied, _mm512_loadu_si512(values));
        const __m512i high = _mm512_maskz_compress_epi64((__mmask8)(occupied >> 8), _mm512_loadu_si512(values + 8));
        _mm512_mask_storeu_epi64(writer->values + writer->count, (__mmask8)((1u << low_count) - 1), low);
        _mm512_mask_storeu_epi64(writer->values + writer->count + low_count, (__mmask8)((1u << high_count) - 1), high);
        writer->count += entries;
        writer->token = graph->tokens[last_leaf];
        writer->expression_nodes += count;
        writer->batch_nodes += count;
    }
#if defined(BUSTER_CHECK_LOWER_FRAGMENT_TRACE)
    // Qualification-only records: attempted roots, not all evaluated occurrences. Never time a trace build.
    fprintf(stderr, "lower-fragment root=%u visited=%u result=%u entries=%u\n", root, count, (unsigned)result, result == CHECK_LOWER_FRAGMENT_OK ? entries : 0);
#endif
    return result == CHECK_LOWER_FRAGMENT_OK;
}
#endif
