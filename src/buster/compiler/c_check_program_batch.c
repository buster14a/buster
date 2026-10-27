// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Straight-line checked expressions (#624), included by c_check_program_lower.c. Sixteen checked records are
// classified together; only ownership, contiguous subtree joins and exceptional conversions remain scalar.
// Each lane retains four bounded windows so a task's descent and return do not repeatedly classify a window.
// The original task loop is the exact twin. Nothing is published before the whole interval fits.

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

// All gather offsets are relative to the start of at most 16 records, not to the whole graph: even the
// largest graph cannot overflow a signed gather index. The active mask bounds every field at the last row.
static inline __attribute__((always_inline)) __m512i check_lower_batch_field(const CheckValue* values, __m512i offsets, __mmask16 active, uint32_t field)
{
    return _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), active, offsets, (const uint8_t*)values + field, 1);
}

static void check_lower_batch_classify(CheckLowerWriter* writer, CheckLowerBatch* batch, uint32_t first, uint32_t count)
{
    const CheckStream* stream = writer->program->stream;
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckValue* values = stream->values + batch->first + first;
    const __mmask16 active = (__mmask16)((1u << count) - 1);
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const __m512i offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32(sizeof(CheckValue)));
    const __m512i types = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, type));
    const __m512i indices = _mm512_srli_epi32(types, BUSTER_TYPE_INDEX_SHIFT);
    const __mmask16 typed = active & _mm512_cmplt_epu32_mask(indices, _mm512_set1_epi32((int)writer->type_count));
    const __m512i metadata = _mm512_mask_i32gather_epi32(_mm512_set1_epi32(UINT8_MAX), typed, indices, writer->type_templates, 4);
    const __m512i classes = _mm512_and_si512(metadata, _mm512_set1_epi32(UINT8_MAX));
    const __m512i flags = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, flags));
    const __m512i constants = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, constant_kind));
    const __m512i categories = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, category));
    const __m512i faults = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, fault));
    const __m512i overloads = check_lower_batch_field(values, offsets, active, offsetof(CheckValue, overload_head));
    const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + batch->first + first);
    const __mmask16 small = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
    const __m512i lookup = _mm512_load_si512(check_lower_batch_ops);
    const __m512i keys = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
    const __m128i selected = _mm512_castsi512_si128(_mm512_permutexvar_epi8(keys, lookup));
    const __m512i recipes = _mm512_maskz_mov_epi32(small, _mm512_cvtepu8_epi32(selected));
    const __m512i zero = _mm512_setzero_si512();
    const __m512i forbidden = _mm512_set1_epi32((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS) << 8);
    const __mmask16 ordinary = active & _mm512_cmpge_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_I8)) &
                               _mm512_cmple_epu32_mask(classes, _mm512_set1_epi32(BUSTER_TYPED_CLASS_F64)) &
                               _mm512_cmpeq_epi32_mask(_mm512_and_si512(metadata, forbidden), zero) & _mm512_cmpeq_epi32_mask(overloads, zero) &
                               _mm512_cmpneq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_STATEMENT_VALUE));
    const __mmask16 proof = _mm512_cmpneq_epi32_mask(constants, _mm512_set1_epi32(CHECK_CONSTANT_NONE)) &
                            _mm512_cmpeq_epi32_mask(faults, _mm512_set1_epi32(CHECK_FAULT_NONE)) &
                            _mm512_cmpeq_epi32_mask(_mm512_and_si512(flags, _mm512_set1_epi32(BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)), zero) &
                            _mm512_cmpeq_epi32_mask(categories, _mm512_set1_epi32(CHECK_RVALUE)) &
                            _mm512_cmpneq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_INITIALIZE)) &
                            _mm512_cmpneq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_BUILTIN_QUERY));
    // Floating/address constants keep the task loop. Treating one as an ordinary operation would evaluate
    // operands which its scalar constant proof skips, even when that operation happens to have a template.
    const __mmask16 folded = ordinary & proof & _mm512_cmpeq_epi32_mask(constants, _mm512_set1_epi32(CHECK_CONSTANT_INTEGER));
    const __mmask16 leaves = ordinary & ~proof & _mm512_cmpeq_epi32_mask(recipes, _mm512_set1_epi32(BUSTER_CHECK_LOWER_BATCH_VALUE)) &
                             _mm512_cmpeq_epi32_mask(categories, _mm512_set1_epi32(CHECK_LVALUE));
    const __mmask16 regular = ordinary & ~proof & _mm512_cmpneq_epi32_mask(recipes, zero) &
                              _mm512_cmpneq_epi32_mask(recipes, _mm512_set1_epi32(BUSTER_CHECK_LOWER_BATCH_VALUE));
    const __mmask16 binary = regular & _mm512_cmpge_epu32_mask(ops, _mm512_set1_epi32(CHECK_OP_ADD)) &
                             _mm512_cmple_epu32_mask(ops, _mm512_set1_epi32(CHECK_OP_BIT_XOR));
    __m512i kinds = _mm512_maskz_mov_epi32(regular | leaves, recipes);
    kinds = _mm512_mask_mov_epi32(kinds, _mm512_cmpeq_epi32_mask(recipes, _mm512_set1_epi32(BUSTER_CHECK_LOWER_BATCH_WRAPPER)), zero);
    kinds = _mm512_mask_mov_epi32(kinds, folded, _mm512_set1_epi32(BUSTER_TYPED_CONST));
    const __m512i arities = _mm512_mask_mov_epi32(_mm512_maskz_mov_epi32(regular, _mm512_set1_epi32(1)), binary, _mm512_set1_epi32(2));
    const __m512i converted_offsets = _mm512_mullo_epi32(lanes, _mm512_set1_epi32(sizeof(CheckNodeConversions)));
    const uint8_t* conversions = (const uint8_t*)(stream->conversions + batch->first + first) +
                                 offsetof(CheckNodeConversions, operands) + offsetof(CheckConversion, type);
    __m512i left = _mm512_mask_i32gather_epi32(zero, regular, converted_offsets, conversions, 1);
    const __m512i right = _mm512_mask_i32gather_epi32(zero, binary, converted_offsets, conversions + sizeof(CheckConversion), 1);
    const __mmask16 casts = _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CAST)) |
                           _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_CONVERT)) |
                           _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_POSITIVE));
    left = _mm512_mask_mov_epi32(left, casts, types);
    left = _mm512_mask_mov_epi32(left, _mm512_cmpeq_epi32_mask(ops, _mm512_set1_epi32(CHECK_OP_SELECT)), zero);
    _mm512_mask_storeu_epi32(batch->types + first, active, types);
    _mm512_mask_storeu_epi32(batch->metadata + first, active, metadata);
    _mm512_mask_storeu_epi32(batch->targets[0] + first, active, left);
    _mm512_mask_storeu_epi32(batch->targets[1] + first, active, right);
    _mm256_mask_storeu_epi16(batch->kinds + first, active, _mm512_cvtepi32_epi16(kinds));
    _mm256_mask_storeu_epi16(batch->classes + first, active, _mm512_cvtepi32_epi16(classes));
    _mm_mask_storeu_epi8(batch->arities + first, active, _mm512_cvtepi32_epi8(arities));
    batch->valid |= (uint64_t)(folded | leaves | regular) << first;
}

// A joined child already has an ordinary class. Most edges are exactly the same type (or request no
// conversion); they must not pay for either type lookup. Bool normalization and memory classes defer.
static bool check_lower_batch_conversion(const CheckLowerWriter* writer, const CheckLowerBatch* batch, uint32_t child, TypeId to, uint16_t* entry)
{
    bool valid = true;
    *entry = 0;
    if (to != 0 && batch->types[child] != to)
    {
        const uint32_t index = type_index(to);
        const uint32_t metadata = index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
        const uint8_t source = (uint8_t)batch->metadata[child];
        const uint8_t target = (uint8_t)metadata;
        valid = target <= BUSTER_TYPED_CLASS_F64 &&
                (metadata & ((BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS | BUSTER_CHECK_LOWER_TYPE_BOOL) << 8)) == 0;
        if (valid && target == BUSTER_TYPED_CLASS_VOID)
        {
            *entry = BUSTER_TYPED_DISCARD | (uint16_t)source << 8;
        }
        else if (valid && source != target)
        {
            *entry = BUSTER_TYPED_CONVERT | (uint16_t)target << 8;
        }
    }
    return valid;
}

static __attribute__((noinline)) void check_lower_batch_prepare(CheckLowerWriter* writer, CheckLowerBatch* batch, uint32_t node)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    batch->first = node & ~(BUSTER_CHECK_LOWER_BATCH - 1);
    const uint32_t count = stream->count - batch->first < BUSTER_CHECK_LOWER_BATCH ? stream->count - batch->first : BUSTER_CHECK_LOWER_BATCH;
    batch->valid = 0;
    for (uint32_t first = 0; first < count; first += 16)
    {
        check_lower_batch_classify(writer, batch, first, count - first < 16 ? count - first : 16);
    }
    writer->classified_nodes += count;
    memset(batch->starts, BUSTER_CHECK_LOWER_BATCH_NONE, sizeof(batch->starts));
    memset(batch->parents, BUSTER_CHECK_LOWER_BATCH_NONE, sizeof(batch->parents));
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t current = batch->first + i;
        const CheckOp op = graph->ops[current];
        uint8_t kind = (uint8_t)batch->kinds[i];
        const uint8_t class = (uint8_t)batch->metadata[i];
        const uint32_t arity = batch->arities[i];
        bool valid = ((batch->valid >> i) & 1) != 0;
        uint64_t bits = 0;
        if (valid && (kind == BUSTER_CHECK_LOWER_BATCH_VALUE || (kind == BUSTER_TYPED_CONST && op == CHECK_OP_VALUE)))
        {
            const uint32_t object = stream->values[current].object;
            const bool known = object != 0 && object < program->object_count;
            const bool compound = known && program->input->compound_objects[object - 1] && !program->input->static_objects[object - 1];
            valid = !compound;
            if (valid && kind == BUSTER_CHECK_LOWER_BATCH_VALUE)
            {
                // Source object identities are canonical declaration tokens plus one. A window may straddle
                // bodies; reading another body's mutable local map would race its lane, so only our body may.
                const uint32_t declaration = object - 1;
                const bool own = writer->body == NULL || (declaration < program->input->token_count &&
                                                           program->input->declaration_bodies[declaration] == writer->body->opening);
                const uint32_t local = known && own ? writer->local_map[object] : BUSTER_CHECK_NONE;
                const SymbolId symbol = known ? program->object_symbols[object] : 0;
                valid = known && (local != BUSTER_CHECK_NONE || symbol != 0);
                kind = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
                bits = local != BUSTER_CHECK_NONE ? local : symbol;
            }
        }
        if (valid && kind == BUSTER_TYPED_CONST)
        {
            bits = (uint64_t)stream->values[current].integer;
            if (class <= BUSTER_TYPED_CLASS_U32)
            {
                const uint32_t width = 8u << ((class - 1) / 2);
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                bits &= mask;
                bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
            }
        }
        uint32_t start = i;
        uint32_t last_node = i;
        uint32_t entries = kind != 0;
        uint32_t tasks = 0;
        const uint32_t a = graph->operand0[current] - batch->first;
        const uint32_t b = graph->operand1[current] - batch->first;
        uint16_t conversion_a = 0;
        uint16_t conversion_b = 0;
        if (valid && arity != 0)
        {
            if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
            {
                const uint32_t left = type_index(batch->targets[0][i]);
                const uint32_t right = type_index(batch->targets[1][i]);
                const uint32_t facts = (left < writer->type_count ? writer->type_templates[left] : 0) |
                                       (right < writer->type_count ? writer->type_templates[right] : 0);
                valid = (facts & (BUSTER_CHECK_LOWER_TYPE_POINTER << 8)) == 0;
            }
            valid = valid && a < i && batch->starts[a] != BUSTER_CHECK_LOWER_BATCH_NONE && batch->parents[a] == BUSTER_CHECK_LOWER_BATCH_NONE &&
                    check_lower_batch_conversion(writer, batch, a, batch->targets[0][i], &conversion_a);
            if (valid && arity == 2)
            {
                valid = b < i && b + 1 == i && batch->starts[b] != BUSTER_CHECK_LOWER_BATCH_NONE &&
                        a + 1 == batch->starts[b] && batch->parents[b] == BUSTER_CHECK_LOWER_BATCH_NONE &&
                        check_lower_batch_conversion(writer, batch, b, batch->targets[1][i], &conversion_b);
            }
            else if (valid)
            {
                valid = a + 1 == i;
            }
            if (valid)
            {
                start = batch->starts[a];
                last_node = batch->last_nodes[arity == 2 ? b : a];
                const uint32_t pending = (kind != 0) + (conversion_a != 0) + (arity == 2 ? 1 + (conversion_b != 0) : 0);
                tasks = pending + 1;
                const uint32_t left_tasks = pending + batch->tasks[a];
                tasks = left_tasks > tasks ? left_tasks : tasks;
                entries += batch->entries[a] + (conversion_a != 0);
                batch->parents[a] = (uint8_t)i;
                batch->kinds[a] |= (uint16_t)(uint8_t)conversion_a << 8;
                batch->classes[a] |= conversion_a & UINT16_C(0xff00);
                if (arity == 2)
                {
                    const uint32_t right_tasks = (kind != 0) + (conversion_b != 0) + batch->tasks[b];
                    tasks = right_tasks > tasks ? right_tasks : tasks;
                    entries += batch->entries[b] + (conversion_b != 0);
                    batch->parents[b] = (uint8_t)i;
                    batch->kinds[b] |= (uint16_t)(uint8_t)conversion_b << 8;
                    batch->classes[b] |= conversion_b & UINT16_C(0xff00);
                }
            }
        }
        batch->kinds[i] = valid ? kind : 0;
        batch->classes[i] = valid ? class : 0;
        batch->values[i] = bits;
        batch->starts[i] = valid ? (uint8_t)start : BUSTER_CHECK_LOWER_BATCH_NONE;
        batch->last_nodes[i] = (uint8_t)last_node;
        batch->entries[i] = (uint16_t)entries;
        batch->tasks[i] = (uint16_t)tasks;
    }
}

static __attribute__((noinline)) bool check_lower_batch_expression(CheckLowerWriter* writer, uint32_t node)
{
    CheckLowerBatch* batch = writer->batches + ((node / BUSTER_CHECK_LOWER_BATCH) & (BUSTER_CHECK_LOWER_WINDOWS - 1));
    if (node < batch->first || node - batch->first >= BUSTER_CHECK_LOWER_BATCH)
    {
        check_lower_batch_prepare(writer, batch, node);
    }
    const uint32_t root = node - batch->first;
    const uint32_t start = batch->starts[root];
    const uint32_t entries = batch->entries[root];
    const uint32_t room = writer->capacity < BUSTER_TIER0_MAX_ENTRIES ? writer->capacity : BUSTER_TIER0_MAX_ENTRIES;
    const bool valid = start != BUSTER_CHECK_LOWER_BATCH_NONE && writer->count <= room && entries <= room - writer->count &&
                       writer->depth <= writer->capacity && batch->tasks[root] <= writer->capacity - writer->depth;
    if (valid)
    {
        if (start == root)
        {
            writer->kinds[writer->count] = (uint8_t)batch->kinds[root];
            writer->classes[writer->count] = (uint8_t)batch->classes[root];
            writer->values[writer->count] = batch->values[root];
            writer->count += 1;
        }
        for (uint32_t first = start; start != root && first <= root; first += 32)
        {
            const uint32_t count = root - first + 1 < 32 ? root - first + 1 : 32;
            const __mmask32 active = count < 32 ? (UINT32_C(1) << count) - 1 : UINT32_MAX;
            const __m512i kinds = _mm512_maskz_loadu_epi16(active, batch->kinds + first);
            const __m512i classes = _mm512_maskz_loadu_epi16(active, batch->classes + first);
            // The root's consuming edge belongs to its caller, which has already pushed that conversion.
            const uint64_t last = first + count - 1 == root ? UINT64_C(1) << (2 * count - 1) : 0;
            const uint64_t mask = _mm512_cmpneq_epi8_mask(kinds, _mm512_setzero_si512()) & ~last;
            _mm512_mask_compressstoreu_epi8(writer->kinds + writer->count, mask, kinds);
            _mm512_mask_compressstoreu_epi8(writer->classes + writer->count, mask, classes);
            uint32_t written = 0;
            for (uint32_t i = 0; i < count; i += 4)
            {
                const uint32_t some = count - i < 4 ? count - i : 4;
                const __m512i values = _mm512_maskz_loadu_epi64((__mmask8)((1u << some) - 1), batch->values + first + i);
                const __m512i expanded = _mm512_maskz_expand_epi64(0x55, values);
                const uint8_t take = (uint8_t)(mask >> (2 * i));
                _mm512_mask_compressstoreu_epi64(writer->values + writer->count + written, take, expanded);
                written += (uint32_t)__builtin_popcount(take);
            }
            writer->count += written;
        }
        const uint32_t nodes = root - start + 1;
        writer->expression_nodes += nodes;
        writer->batch_nodes += nodes;
        writer->token = writer->program->input->graph.tokens[batch->first + batch->last_nodes[root]];
    }
    return valid;
}
