// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The common warnings' operator candidates (#1340), included after CheckCommonScan. One classification of the graph
// gives the repeated passes their masks, a uint16_t per 16 nodes and class: one byte per node across all eight classes.
// Each class is a conservative set of operators; the consumer keeps its own semantic tests and diagnostic order.

typedef enum CheckCommonCandidate
{
    CHECK_COMMON_CANDIDATE_REACH,
    CHECK_COMMON_CANDIDATE_COMPARE,
    CHECK_COMMON_CANDIDATE_RETURN,
    CHECK_COMMON_CANDIDATE_CALL,
    CHECK_COMMON_CANDIDATE_SELECT,
    CHECK_COMMON_CANDIDATE_MASK,
    CHECK_COMMON_CANDIDATE_DISCARD,
    CHECK_COMMON_CANDIDATE_SHIFT,
    CHECK_COMMON_CANDIDATE_COUNT,
} CheckCommonCandidate;

typedef struct CheckCommonCandidates CheckCommonCandidates;
struct CheckCommonCandidates
{
    // Each class's blocks are contiguous. Its mask at node / 16 has bit node % 16 set where the class may use that node.
    uint16_t* masks;
    uint32_t count;
    uint32_t blocks;
};

// The operators whose own reach can have a bit; comparisons have their own class so that -Wsign-compare and the
// out-of-range comparison batches can select them separately. DISCARD covers both CAST and COMMA, whose first operand
// may have an unused value. CALL includes the explicit library builtins' source-call receipts (BUILTIN_X86).
static const uint8_t check_common_candidate_classes[128] =
{
    [CHECK_OP_NOT] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_DIVIDE] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_REMAINDER] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_SHIFT_LEFT] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_SHIFT),
    [CHECK_OP_SHIFT_RIGHT] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_SHIFT),
    [CHECK_OP_LESS] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_LESS_EQUAL] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_GREATER] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_GREATER_EQUAL] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_EQUAL] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_NOT_EQUAL] = 1u << CHECK_COMMON_CANDIDATE_COMPARE,
    [CHECK_OP_LOGICAL_AND] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_MASK),
    [CHECK_OP_LOGICAL_OR] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_MASK),
    [CHECK_OP_CONDITIONAL] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_MASK),
    [CHECK_OP_CAST] = 1u << CHECK_COMMON_CANDIDATE_DISCARD,
    [CHECK_OP_ADDRESS] = 1u << CHECK_COMMON_CANDIDATE_RETURN,
    [CHECK_OP_MEMBER] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_ARROW] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_DEREFERENCE] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_ASSIGN] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_COMMA] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_DISCARD),
    [CHECK_OP_SUBSCRIPT] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_SIZEOF] = 1u << CHECK_COMMON_CANDIDATE_MASK,
    [CHECK_OP_ALIGNOF] = 1u << CHECK_COMMON_CANDIDATE_MASK,
    [CHECK_OP_CALL] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_CALL),
    [CHECK_OP_INITIALIZE] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_CONVERT] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_RETURN),
    [CHECK_OP_CONDITION] = 1u << CHECK_COMMON_CANDIDATE_REACH,
    [CHECK_OP_SHIFT_LEFT_ASSIGN] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_SHIFT),
    [CHECK_OP_SHIFT_RIGHT_ASSIGN] = (1u << CHECK_COMMON_CANDIDATE_REACH) | (1u << CHECK_COMMON_CANDIDATE_SHIFT),
    [CHECK_OP_SELECT] = 1u << CHECK_COMMON_CANDIDATE_SELECT,
    [CHECK_OP_BUILTIN_X86] = 1u << CHECK_COMMON_CANDIDATE_CALL,
};
_Static_assert(CHECK_OP_COUNT <= BUSTER_ARRAY_LENGTH(check_common_candidate_classes), "the candidate classes fit two byte-permute tables");
_Static_assert(CHECK_COMMON_CANDIDATE_COUNT == 8, "one byte holds the common-warning candidate classes");
_Static_assert(CHECK_OP_VALUE == 0, "zeroed invalid operators have no candidate classes");

// The classifier's scalar twin, expressed as the consumers' operator predicates rather than reading the vector table.
static uint8_t check_common_candidate_scalar(CheckOp op)
{
    const bool reach = op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW || op == CHECK_OP_INITIALIZE || op == CHECK_OP_ASSIGN || op == CHECK_OP_CONVERT || op == CHECK_OP_CALL ||
                       op == CHECK_OP_SUBSCRIPT || op == CHECK_OP_COMMA || op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND ||
                       op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITION || op == CHECK_OP_CONDITIONAL || op == CHECK_OP_DEREFERENCE ||
                       op == CHECK_OP_DIVIDE || op == CHECK_OP_REMAINDER || op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT ||
                       op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
    const bool compare = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
    const bool returned = op == CHECK_OP_ADDRESS || op == CHECK_OP_CONVERT;
    const bool call = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86;
    const bool selected = op == CHECK_OP_SELECT;
    const bool masked = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF || op == CHECK_OP_CONDITIONAL ||
                        op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
    const bool discard = op == CHECK_OP_CAST || op == CHECK_OP_COMMA;
    const bool shift = op == CHECK_OP_SHIFT_LEFT || op == CHECK_OP_SHIFT_RIGHT || op == CHECK_OP_SHIFT_LEFT_ASSIGN || op == CHECK_OP_SHIFT_RIGHT_ASSIGN;
    const uint8_t result = (uint8_t)((reach ? 1u << CHECK_COMMON_CANDIDATE_REACH : 0u) |
                                     (compare ? 1u << CHECK_COMMON_CANDIDATE_COMPARE : 0u) |
                                     (returned ? 1u << CHECK_COMMON_CANDIDATE_RETURN : 0u) |
                                     (call ? 1u << CHECK_COMMON_CANDIDATE_CALL : 0u) |
                                     (selected ? 1u << CHECK_COMMON_CANDIDATE_SELECT : 0u) |
                                     (masked ? 1u << CHECK_COMMON_CANDIDATE_MASK : 0u) |
                                     (discard ? 1u << CHECK_COMMON_CANDIDATE_DISCARD : 0u) |
                                     (shift ? 1u << CHECK_COMMON_CANDIDATE_SHIFT : 0u));
    return result;
}

// Classifies every node once, 64 at a time on the vector path: four operator vectors narrowed to one byte vector, one
// table lookup and eight mask tests, writing four adjacent 16-node masks per class. Operators outside CHECK_OP_COUNT
// become VALUE before the lookup, so an invalid operator has no class and cannot alias a real one. The tail takes masked
// 16-node loads and every output mask is written, including the empty classes and the last block's high zero bits.
// No graph column other than ops is read.
static bool check_common_candidates_build(const CheckGraph* graph, Arena* scratch, bool scalar, CheckCommonCandidates* candidates)
{
    const uint32_t count = graph->count;
    const CheckOp* operators = graph->ops;
    const uint32_t blocks = (uint32_t)(((uint64_t)count + 15) / 16);
    uint16_t* masks = blocks != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint16_t, (uint64_t)blocks * CHECK_COMMON_CANDIDATE_COUNT) : NULL;
    const bool valid = blocks == 0 || masks != NULL;
    const bool vector = !BUSTER_SCALAR_PATH(scalar);
    const __m512i low = _mm512_loadu_si512(check_common_candidate_classes);
    const __m512i high = _mm512_loadu_si512(check_common_candidate_classes + 64);
    const __m512i limit = _mm512_set1_epi32(CHECK_OP_COUNT);
    uint32_t block = 0;
    for (; valid && vector && count - block * 16 >= 64; block += 4)
    {
        const uint32_t first = block * 16;
        const __m512i a = _mm512_loadu_si512(operators + first);
        const __m512i b = _mm512_loadu_si512(operators + first + 16);
        const __m512i c = _mm512_loadu_si512(operators + first + 32);
        const __m512i d = _mm512_loadu_si512(operators + first + 48);
        // Mask each narrowing from its full-width operators rather than joining four masks through scalar registers.
        const __m128i first_bytes = _mm_maskz_mov_epi8(_mm512_cmplt_epu32_mask(a, limit), _mm512_cvtepi32_epi8(a));
        const __m128i second_bytes = _mm_maskz_mov_epi8(_mm512_cmplt_epu32_mask(b, limit), _mm512_cvtepi32_epi8(b));
        const __m128i third_bytes = _mm_maskz_mov_epi8(_mm512_cmplt_epu32_mask(c, limit), _mm512_cvtepi32_epi8(c));
        const __m128i fourth_bytes = _mm_maskz_mov_epi8(_mm512_cmplt_epu32_mask(d, limit), _mm512_cvtepi32_epi8(d));
        __m512i indices = _mm512_zextsi128_si512(first_bytes);
        indices = _mm512_inserti32x4(indices, second_bytes, 1);
        indices = _mm512_inserti32x4(indices, third_bytes, 2);
        indices = _mm512_inserti32x4(indices, fourth_bytes, 3);
        const __m512i classes = _mm512_permutex2var_epi8(low, indices, high);
        for (uint32_t kind = 0; kind < CHECK_COMMON_CANDIDATE_COUNT; kind += 1)
        {
            const uint64_t selected = _mm512_test_epi8_mask(classes, _mm512_set1_epi8((char)(1u << kind)));
            // Four masks in little-endian node order; memcpy permits the first block to have only uint16_t alignment.
            memcpy(masks + (uint64_t)kind * blocks + block, &selected, sizeof(selected));
        }
    }
    for (; valid && block < blocks; block += 1)
    {
        const uint32_t first = block * 16;
        const uint32_t live = count - first < 16 ? count - first : 16;
        const __mmask16 lanes = (__mmask16)(live == 16 ? 0xffffu : (1u << live) - 1);
        if (vector)
        {
            const __m512i ops = _mm512_maskz_loadu_epi32(lanes, operators + first);
            const __mmask16 present = _mm512_mask_cmplt_epu32_mask(lanes, ops, limit);
            const __m512i classes = _mm512_permutex2var_epi8(low, _mm512_zextsi128_si512(_mm512_cvtepi32_epi8(ops)), high);
            for (uint32_t kind = 0; kind < CHECK_COMMON_CANDIDATE_COUNT; kind += 1)
            {
                masks[(uint64_t)kind * blocks + block] = (uint16_t)_mm512_mask_test_epi8_mask((__mmask64)present, classes,
                                                                                            _mm512_set1_epi8((char)(1u << kind)));
            }
        }
        else
        {
            uint16_t selected[CHECK_COMMON_CANDIDATE_COUNT] = { 0 };
            for (uint32_t lane = 0; lane < live; lane += 1)
            {
                const uint8_t classes = check_common_candidate_scalar(operators[first + lane]);
                for (uint32_t kind = 0; kind < CHECK_COMMON_CANDIDATE_COUNT; kind += 1)
                {
                    selected[kind] |= (classes & (1u << kind)) != 0 ? (uint16_t)(1u << lane) : 0u;
                }
            }
            for (uint32_t kind = 0; kind < CHECK_COMMON_CANDIDATE_COUNT; kind += 1)
            {
                masks[(uint64_t)kind * blocks + block] = selected[kind];
            }
        }
    }
    *candidates = (CheckCommonCandidates)
    {
        .masks = masks,
        .count = count,
        .blocks = blocks,
    };
    return valid;
}

// A class's candidates in ascending node order. The scalar path keeps the original scan's oracle behavior: every node
// is returned and the consumer applies its own tests. A vector caller uses only a successfully built index.
static uint32_t check_common_next_candidate(const CheckCommonCandidates* candidates, CheckCommonCandidate kind, CheckCommonScan* scan, bool vector)
{
    const uint32_t count = candidates->count;
    const uint16_t* masks = candidates->masks;
    const uint64_t column = (uint64_t)kind * candidates->blocks;
    while (scan->pending == 0 && scan->next < count)
    {
        const uint32_t first = scan->next;
        const uint32_t live = count - first < 16 ? count - first : 16;
        scan->base = first;
        scan->next = first + live;
        scan->pending = vector ? masks[column + first / 16] :
                                 live == 16 ? 0xffffu : (1u << live) - 1;
    }
    const uint32_t result = check_common_scan_take(scan, count);
    return result;
}
