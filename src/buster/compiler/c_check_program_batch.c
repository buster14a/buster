// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Straight-line checked expressions (#624), included by c_check_program_lower.c. The task loop remains the
// scalar twin. A lazy, lane-owned window classifies operators by byte permute; its bounded postorder scan
// proves contiguous evaluation and attaches each consuming edge's conversion to its operand's template.
// Expansion compresses the templates' two entry slots into the typed columns. No speculative entry is published.

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

// One ordinary edge's optional entry. Complex and memory classes, bool normalization and conversions whose
// shape is more than one entry keep the scalar path. Type equality matters: a conversion to the same bool
// does nothing, whereas a conversion from another type of its class still normalizes it.
static bool check_lower_batch_conversion(const CheckLowerWriter* writer, TypeId from, TypeId to, uint16_t* entry)
{
    const uint8_t source = check_lower_class(writer, from);
    const uint8_t target = to != 0 ? check_lower_class(writer, to) : source;
    const uint8_t source_facts = check_lower_facts(writer, from);
    const uint8_t target_facts = to != 0 ? check_lower_facts(writer, to) : source_facts;
    bool valid = source >= BUSTER_TYPED_CLASS_I8 && source <= BUSTER_TYPED_CLASS_F64 && target <= BUSTER_TYPED_CLASS_F64 &&
                 ((source_facts | target_facts) & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS)) == 0;
    *entry = 0;
    if (valid && to != 0 && from != to)
    {
        if ((target_facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0)
        {
            valid = false;
        }
        else if (target == BUSTER_TYPED_CLASS_VOID)
        {
            *entry = BUSTER_TYPED_DISCARD | (uint16_t)source << 8;
        }
        else if (source != target)
        {
            *entry = BUSTER_TYPED_CONVERT | (uint16_t)target << 8;
        }
    }
    return valid;
}

// The scalar rules for one node's native entry and arity, after vector operator classification. These read
// checked facts and the current body's local map; no helper here may allocate, publish a failure or make a
// symbol. A constant is a leaf even when its source operator has operands: the scalar twin skips them too.
static bool check_lower_batch_node(const CheckLowerWriter* writer, uint32_t node, uint8_t recipe, uint8_t* kind, uint8_t* class, uint64_t* bits,
                                  uint32_t* arity, TypeId* left, TypeId* right)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckValue* value = program->stream->values + node;
    const CheckOp op = graph->ops[node];
    const uint8_t facts = check_lower_facts(writer, value->type);
    *class = check_lower_class(writer, value->type);
    *kind = 0;
    *bits = 0;
    *arity = 0;
    bool valid = op != CHECK_OP_STATEMENT_VALUE && value->overload_head == 0 && *class >= BUSTER_TYPED_CLASS_I8 && *class <= BUSTER_TYPED_CLASS_F64 &&
                 (facts & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_ADDRESS)) == 0;
    const bool constant = value->constant_kind != CHECK_CONSTANT_NONE && value->fault == CHECK_FAULT_NONE &&
                          (value->flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
    if (valid && constant && value->category == CHECK_RVALUE && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY)
    {
        valid = value->constant_kind == CHECK_CONSTANT_INTEGER || value->constant_kind == CHECK_CONSTANT_FLOATING;
        if (valid)
        {
            *kind = BUSTER_TYPED_CONST;
            *bits = (uint64_t)value->integer;
            if (value->constant_kind == CHECK_CONSTANT_FLOATING && *class == BUSTER_TYPED_CLASS_F32)
            {
                const float number = (float)value->floating;
                uint32_t word;
                memcpy(&word, &number, sizeof(word));
                *bits = word;
            }
            else if (value->constant_kind == CHECK_CONSTANT_FLOATING && *class == BUSTER_TYPED_CLASS_F64)
            {
                const double number = (double)value->floating;
                memcpy(bits, &number, sizeof(*bits));
            }
            else if (*class <= BUSTER_TYPED_CLASS_U32)
            {
                const uint32_t width = 8u << ((*class - 1) / 2);
                const uint64_t mask = (UINT64_C(1) << width) - 1;
                *bits &= mask;
                *bits |= (*class & 1) != 0 && (*bits >> (width - 1)) != 0 ? ~mask : 0;
            }
        }
    }
    else if (valid && recipe == BUSTER_CHECK_LOWER_BATCH_VALUE)
    {
        const uint32_t object = value->object;
        valid = value->category == CHECK_LVALUE && object != 0 && object < program->object_count &&
                (!program->input->compound_objects[object - 1] || program->input->static_objects[object - 1]);
        if (valid)
        {
            // A window may straddle bodies. Another lane owns that body's local_map entries, so speculative
            // classification must never read them. Globals use their immutable symbol, and each body's
            // cache is invalidated after its own locals have been assigned.
            const uint32_t declaration = program->object_declarations != NULL ? program->object_declarations[object] : BUSTER_CHECK_NONE;
            const bool own = writer->body == NULL || (declaration < program->input->token_count &&
                                                       program->input->declaration_bodies[declaration] == writer->body->opening);
            const uint32_t local = own ? writer->local_map[object] : BUSTER_CHECK_NONE;
            const SymbolId symbol = program->object_symbols[object];
            valid = local != BUSTER_CHECK_NONE || symbol != 0;
            *kind = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
            *bits = local != BUSTER_CHECK_NONE ? local : symbol;
        }
    }
    else if (valid && recipe != 0)
    {
        const bool binary = op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR;
        *arity = binary ? 2 : 1;
        *left = program->stream->conversions[node].operands[0].type;
        *right = program->stream->conversions[node].operands[1].type;
        *kind = recipe == BUSTER_CHECK_LOWER_BATCH_WRAPPER ? 0 : recipe;
        if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
        {
            *left = value->type;
        }
        else if (op == CHECK_OP_SELECT)
        {
            *left = 0;
        }
        if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
        {
            valid = ((check_lower_facts(writer, *left) | check_lower_facts(writer, *right)) & BUSTER_CHECK_LOWER_TYPE_POINTER) == 0;
        }
    }
    else
    {
        valid = false;
    }
    return valid;
}

static __attribute__((noinline)) void check_lower_batch_prepare(CheckLowerWriter* writer, uint32_t node)
{
    CheckLowerBatch* batch = &writer->batch;
    const CheckGraph* graph = &writer->program->input->graph;
    const CheckStream* stream = writer->program->stream;
    batch->first = node & ~(BUSTER_CHECK_LOWER_BATCH - 1);
    const uint32_t count = stream->count - batch->first < BUSTER_CHECK_LOWER_BATCH ? stream->count - batch->first : BUSTER_CHECK_LOWER_BATCH;
    uint8_t recipes[BUSTER_CHECK_LOWER_BATCH];
    const __m512i lookup = _mm512_load_si512(check_lower_batch_ops);
    for (uint32_t first = 0; first < count; first += 16)
    {
        const uint32_t some = count - first < 16 ? count - first : 16;
        const __mmask16 active = (__mmask16)((UINT32_C(1) << some) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(active, graph->ops + batch->first + first);
        const __m512i indices = _mm512_castsi128_si512(_mm512_cvtepi32_epi8(ops));
        const __mmask16 eligible = active & _mm512_cmplt_epu32_mask(ops, _mm512_set1_epi32(64));
        const __m128i selected = _mm512_castsi512_si128(_mm512_permutexvar_epi8(indices, lookup));
        _mm_mask_storeu_epi8(recipes + first, eligible, selected);
        _mm_mask_storeu_epi8(recipes + first, active & ~eligible, _mm_setzero_si128());
    }
    memset(batch->starts, BUSTER_CHECK_LOWER_BATCH_NONE, sizeof(batch->starts));
    memset(batch->parents, BUSTER_CHECK_LOWER_BATCH_NONE, sizeof(batch->parents));
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint8_t kind;
        uint8_t class;
        uint64_t bits;
        uint32_t arity;
        TypeId left = 0;
        TypeId right = 0;
        const uint32_t current = batch->first + i;
        bool valid = check_lower_batch_node(writer, current, recipes[i], &kind, &class, &bits, &arity, &left, &right);
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
            valid = a < i && batch->starts[a] != BUSTER_CHECK_LOWER_BATCH_NONE && batch->parents[a] == BUSTER_CHECK_LOWER_BATCH_NONE &&
                    check_lower_batch_conversion(writer, stream->values[batch->first + a].type, left, &conversion_a);
            if (valid && arity == 2)
            {
                valid = b < i && b + 1 == i && batch->starts[b] != BUSTER_CHECK_LOWER_BATCH_NONE &&
                        a + 1 == batch->starts[b] && batch->parents[b] == BUSTER_CHECK_LOWER_BATCH_NONE &&
                        check_lower_batch_conversion(writer, stream->values[batch->first + b].type, right, &conversion_b);
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
    CheckLowerBatch* batch = &writer->batch;
    if (node < batch->first || node - batch->first >= BUSTER_CHECK_LOWER_BATCH)
    {
        check_lower_batch_prepare(writer, node);
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
            // A root's conversion belongs to its parent; this occurrence's caller already pushed its own conversion.
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
        // Instructions do not change the scalar loop's diagnostic token: its last expression is the rightmost leaf.
        writer->token = writer->program->input->graph.tokens[batch->first + batch->last_nodes[root]];
    }
    return valid;
}
