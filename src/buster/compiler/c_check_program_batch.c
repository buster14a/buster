// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Straight-line checked expressions (#624), included by c_check_program_lower.c. Sixteen checked records are
// classified together; reached leaves supply payloads and small contiguous subtrees share an entry template.
// Each lane retains four bounded windows so a task's descent and return do not repeatedly classify a window.
// The original task loop is the exact twin. A bulk write is published only after its whole template fits.

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

static void check_lower_batch_reset(CheckLowerWriter* writer)
{
    for (uint32_t i = 0; i < BUSTER_CHECK_LOWER_WINDOWS; i += 1)
    {
        writer->batches[i].first = BUSTER_CHECK_NONE;
    }
}

// Cache only immutable operators and type facts. Semantic payloads are read only for expressions the
// task loop actually reaches; preparing all constants and local maps made short expressions slower.
static __attribute__((noinline)) void check_lower_batch_prepare(CheckLowerWriter* writer, CheckLowerBatch* batch, uint32_t node)
{
    const CheckStream* stream = writer->program->stream;
    const CheckGraph* graph = &writer->program->input->graph;
    batch->first = node & ~(BUSTER_CHECK_LOWER_BATCH - 1);
    const uint32_t count = stream->count - batch->first < BUSTER_CHECK_LOWER_BATCH ? stream->count - batch->first : BUSTER_CHECK_LOWER_BATCH;
    const __mmask16 active = (__mmask16)((1u << count) - 1);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32(sizeof(CheckValue)));
    const __m512i types = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, offsets, stream->values + batch->first, 1);
    const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 typed = active & _mm512_cmplt_epu32_mask(indices, _mm512_set1_epi32((int)writer->type_count));
    const __m512i metadata = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), typed, indices, writer->type_templates, 4);
    const __m512i classes = _mm512_and_si512(metadata, _mm512_set1_epi32(UINT8_MAX));
    const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + batch->first);
    const __mmask16 small = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
    const __m512i keys = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
    const __m128i selected = _mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, _mm512_load_si512(check_lower_batch_ops)));
    const __m512i recipes = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(selected));
    const __m512i forbidden = _mm512_set1_epi32((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS) << 8);
    batch->valid = active & _mm512_cmpge_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)) &
                   _mm512_cmple_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_F64)) &
                   _mm512_cmpeq_epi32_mask(_mm512_and_si512(metadata, forbidden), _mm512_setzero_si512());
    _mm256_mask_storeu_epi16(batch->kinds, active, _mm512_cvtepi32_epi16(recipes));
    _mm256_mask_storeu_epi16(batch->classes, active, _mm512_cvtepi32_epi16(classes));
    _mm512_mask_storeu_epi32(batch->types, active, types);
    _mm512_mask_storeu_epi32(batch->metadata, active, metadata);
    writer->classified_nodes += count;
}

static bool check_lower_batch_proof(const CheckValue* value, CheckOp op)
{
    return value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
           (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0 && value->category == CHECK_RVALUE &&
           op != CHECK_OP_STATEMENT_VALUE && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY;
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
    bool valid = class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64 &&
                 (metadata & ((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS) << 8)) == 0 && value->overload_head == 0;
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
    const uint32_t forbidden = (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_ADDRESS |
                                (pointer ? BUSTER_CHECK_LOWER_TYPE_POINTER : 0)) << 8;
    return target == 0 || ((uint8_t)metadata <= BUSTER_TYPED_CLASS_F64 && (metadata & forbidden) == 0);
}

// Up to three native entry slots, one per reached node. A wrapper has an empty final slot. Their prefix
// counts are the popcounts of the occupied slot masks; compress expands those templates into all three columns.
static void check_lower_batch_write(CheckLowerWriter* writer, uint32_t kinds, uint32_t classes, uint64_t left, uint64_t right, uint8_t occupied)
{
    _mm_mask_compressstoreu_epi8(writer->kinds + writer->count, occupied, _mm_cvtsi32_si128((int)kinds));
    _mm_mask_compressstoreu_epi8(writer->classes + writer->count, occupied, _mm_cvtsi32_si128((int)classes));
    _mm512_mask_compressstoreu_epi64(writer->values + writer->count, occupied, _mm512_setr_epi64((int64_t)left, (int64_t)right, 0, 0, 0, 0, 0, 0));
    writer->count += (uint32_t)__builtin_popcount(occupied);
}

// Isolated VALUE nodes need no speculative window. Their scalar template uses the same packed type
// lookup; taking an address keeps the original helper, including VLA indirection and storage flags.
static inline __attribute__((always_inline)) bool check_lower_value(CheckLowerWriter* writer, uint32_t node, bool address, bool initialized)
{
    const CheckProgram* program = writer->program;
    const CheckValue* value = program->stream->values + node;
    bool valid = false;
    if (program->input->graph.ops[node] == CHECK_OP_VALUE)
    {
        const uint32_t object = value->object;
        const bool known = object != 0 && object < program->object_count;
        const bool compound = !initialized && known && program->input->compound_objects[object - 1] && !program->input->static_objects[object - 1];
        if (address && value->category == CHECK_LVALUE && value->type != BUSTER_TYPE_VOID && value->overload_head == 0 && known && !compound)
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
    bool valid = recipe != 0 && recipe != BUSTER_CHECK_LOWER_BATCH_VALUE && value->overload_head == 0 && !check_lower_batch_proof(value, op);
    if (valid)
    {
        CheckLowerBatch* batch = writer->batches + ((node / BUSTER_CHECK_LOWER_BATCH) & (BUSTER_CHECK_LOWER_WINDOWS - 1));
        if (node < batch->first || node - batch->first >= BUSTER_CHECK_LOWER_BATCH)
        {
            check_lower_batch_prepare(writer, batch, node);
        }
        const uint32_t root = node - batch->first;
        const uint8_t kind = (uint8_t)batch->kinds[root];
        const uint8_t class = (uint8_t)batch->classes[root];
        const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR;
        const bool pointer = op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT;
        const TypeId left = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE ? value->type :
                            op == CHECK_OP_SELECT ? 0 : program->stream->conversions[node].operands[0].type;
        const TypeId right = binary ? program->stream->conversions[node].operands[1].type : 0;
        valid = ((batch->valid >> root) & 1) != 0 && check_lower_batch_target(writer, left, pointer) &&
                (!binary || check_lower_batch_target(writer, right, pointer));
        if (valid)
        {
            uint32_t nodes = 1;
            const uint32_t a = graph->operand0[node];
            const uint32_t b = binary ? graph->operand1[node] : 0;
            const uint32_t entries = 1 + binary + (kind != BUSTER_CHECK_LOWER_BATCH_WRAPPER);
            const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
            bool joined = root >= 1 + binary && a + 1 + binary == node && (!binary || b + 1 == node) &&
                          (left == 0 || left == batch->types[root - 1 - binary]) && (!binary || right == 0 || right == batch->types[root - 1]) &&
                          writer->count <= room && entries <= room - writer->count && writer->depth <= writer->capacity && 5 <= writer->capacity - writer->depth;
            uint8_t a_kind = 0;
            uint8_t b_kind = 0;
            uint64_t a_bits = 0;
            uint64_t b_bits = 0;
            joined = joined && check_lower_value_payload(writer, a, batch->metadata[root - 1 - binary], false, &a_kind, &a_bits);
            joined = joined && (!binary || check_lower_value_payload(writer, b, batch->metadata[root - 1], false, &b_kind, &b_bits));
            writer->token = graph->tokens[node];
            if (joined)
            {
                const uint32_t kinds = a_kind | (uint32_t)b_kind << 8 | (uint32_t)(kind == BUSTER_CHECK_LOWER_BATCH_WRAPPER ? 0 : kind) << 16;
                const uint32_t classes = batch->classes[root - 1 - binary] | (binary ? (uint32_t)batch->classes[root - 1] << 8 : 0) | (uint32_t)class << 16;
                const uint8_t occupied = 1 | (binary ? 2 : 0) | (kind != BUSTER_CHECK_LOWER_BATCH_WRAPPER ? 4 : 0);
                check_lower_batch_write(writer, kinds, classes, a_bits, b_bits, occupied);
                writer->token = graph->tokens[binary ? b : a];
                nodes += 1 + binary;
            }
            else
            {
                if (kind != BUSTER_CHECK_LOWER_BATCH_WRAPPER)
                {
                    check_lower_instruction(writer, kind, class, 0);
                }
                if (binary)
                {
                    check_lower_operand(writer, node, 1, false, right);
                }
                check_lower_operand(writer, node, 0, false, left);
            }
            writer->expression_nodes += nodes;
            writer->batch_nodes += nodes;
        }
    }
    return valid;
}
