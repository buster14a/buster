// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850 Step 3. A flat chunk has postorder records, empty slots and byte distances. Compact its live slots, then
// make the kernel's inputs from these records. A chunk's starting slot is shared with the lean window so two inputs
// can be compared in place. Edges into an earlier chunk or outside the distance byte have no input in this version.
#define BUSTER_NODE_FLAT_CROSS 1u
#define BUSTER_NODE_FLAT_FAR 2u
#define BUSTER_NODE_FLAT_UNMAPPED 3u

typedef struct NodeFlatWindow NodeFlatWindow;
struct NodeFlatWindow
{
    // The writers operate on a NodeFront. Only its kernel columns, leaf payload scratch and operator tables are used.
    NodeFront candidate;
    CheckInputLiterals entries;
    NodeFront* target;
    NodeFacts facts;
    uint32_t ordinals[BUSTER_NODE_FLAT_RECORDS + 16];
    uint16_t places[BUSTER_NODE_FLAT_RECORDS];
    uint8_t refusals[BUSTER_NODE_FRONT_RECORDS];
    uint8_t taken[BUSTER_NODE_FRONT_RECORDS];
};

static void node_facts_flat_begin(NodeFront* front, CheckInputReader* reader)
{
    NodeFlatWindow* window = arena_push_array(reader->scratch, 1, sizeof(NodeFlatWindow), 64);
    const NodeFacts* tables = front->window_facts;
    const uint64_t declarations = (uint64_t)tables->input->token_count + 1;
    uint32_t* numbers = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, declarations);
    uint64_t* lines = arena_push_array(reader->scratch, 4 * (uint64_t)tables->line_capacity, sizeof(uint64_t), 64);
    if (window != NULL && numbers != NULL && lines != NULL)
    {
        front->flat->window = window;
        front->flat->window_literals = &window->entries;
        window->target = front;
        // The unit's type rows and names are inputs. Each producer interns its own declaration lines. The first
        // numbers are reserved for literals in its current window, just as for the reference.
        memset(numbers, 0, declarations * sizeof(uint32_t));
        window->facts = (NodeFacts){
            .machine = tables->machine,
            .input = tables->input,
            .parser = tables->parser,
            .enum_values = tables->enum_values,
            .rows = tables->rows,
            .lines = lines,
            .numbers = numbers,
            .line_count = tables->line_count,
            .line_capacity = tables->line_capacity,
        };
        window->candidate.window_facts = &window->facts;
        window->candidate.window_numbers = numbers;
        memcpy(window->candidate.window_op_kinds, front->window_op_kinds, sizeof(front->window_op_kinds));
        memcpy(window->candidate.window_op_codes, front->window_op_codes, sizeof(front->window_op_codes));
    }
    else
    {
        reader->output->resource = true;
    }
}

// Sixteen consecutive 16-byte records give four vectors. Two permutes collect their operator words, and a compress
// writes the live ordinals in postorder. Stop at the end of the ring before each vector load. The scalar loop also
// finishes the last partial group on a vector host.
static uint32_t node_facts_flat_compact(const NodeFlat* flat, uint32_t* ordinals)
{
    uint32_t count = 0;
    for (uint32_t i = flat->chunk_ordinal; i != flat->ordinal;)
    {
        const uint32_t slot = i & (BUSTER_NODE_FLAT_RECORDS - 1);
        const uint32_t run = flat->ordinal - i < BUSTER_NODE_FLAT_RECORDS - slot ? flat->ordinal - i : BUSTER_NODE_FLAT_RECORDS - slot;
        const uint32_t end = i + run;
        if (!BUSTER_SCALAR_PATH(false))
        {
            const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
            const __m512i pick = _mm512_setr_epi32(1, 5, 9, 13, 17, 21, 25, 29, 1, 5, 9, 13, 17, 21, 25, 29);
            for (; end - i >= 16; i += 16)
            {
                const NodeFlatRecord* records = flat->records + (i & (BUSTER_NODE_FLAT_RECORDS - 1));
                const __m512i low = _mm512_permutex2var_epi32(_mm512_loadu_si512(records), pick, _mm512_loadu_si512(records + 4));
                const __m512i high = _mm512_permutex2var_epi32(_mm512_loadu_si512(records + 8), pick, _mm512_loadu_si512(records + 12));
                const __m512i words = _mm512_mask_blend_epi32(0xff00, low, high);
                const __mmask16 live = _mm512_cmpneq_epi32_mask(_mm512_and_si512(words, _mm512_set1_epi32(255)), _mm512_set1_epi32(BUSTER_NODE_FLAT_EMPTY));
                _mm512_storeu_si512(ordinals + count, _mm512_maskz_compress_epi32(live, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)i))));
                count += (uint32_t)__builtin_popcount((uint32_t)live);
            }
        }
        for (; i != end; i += 1)
        {
            if (flat->records[i & (BUSTER_NODE_FLAT_RECORDS - 1)].op != BUSTER_NODE_FLAT_EMPTY)
            {
                ordinals[count] = i;
                count += 1;
            }
        }
    }
    return count;
}

static uint8_t node_facts_flat_operand(const NodeFlat* flat, uint32_t ordinal, uint32_t distance, uint32_t* operand)
{
    uint8_t refusal = BUSTER_NODE_FLAT_UNMAPPED;
    if (distance == BUSTER_NODE_FRONT_FAR)
    {
        refusal = BUSTER_NODE_FLAT_FAR;
    }
    else if (distance > ordinal - flat->chunk_ordinal)
    {
        refusal = BUSTER_NODE_FLAT_CROSS;
    }
    else
    {
        const uint32_t slot = (ordinal - distance) & (BUSTER_NODE_FLAT_RECORDS - 1);
        if (distance != 0 && flat->records[slot].op != BUSTER_NODE_FLAT_EMPTY)
        {
            *operand = flat->window->places[slot];
            refusal = flat->window->refusals[*operand];
        }
    }
    return refusal;
}

static uint8_t node_facts_flat_leaf(NodeFlat* flat, CheckInputReader* reader, uint32_t place, const NodeFlatRecord* record)
{
    NodeFront* source = &flat->window->candidate;
    const uint32_t token = record->token;
    const uint32_t kind = (record->flags & BUSTER_NODE_FLAT_LEAF_MASK) >> BUSTER_NODE_FLAT_LEAF_SHIFT;
    const uint32_t payload = record->payload;
    CheckLiteral decoded = { 0 };
    bool given = kind == BUSTER_NODE_FRONT_LEAF_IDENTIFIER || payload < flat->side_count;
    if (given && kind != BUSTER_NODE_FRONT_LEAF_IDENTIFIER)
    {
        const TypeId type = flat->side_types[payload];
        source->leaf_types[place] = type;
        source->leaf_integers[place] = flat->side_integers[payload];
        if (kind == BUSTER_NODE_FRONT_LEAF_LITERAL && (source->window_facts->rows[type_index(type)].flags & BUSTER_NODE_FACTS_INTEGER) == 0)
        {
            given = node_front_literal(reader, token, &decoded);
        }
    }
    if (given)
    {
        node_facts_window_leaf(source, reader, place, token, kind, payload, &decoded);
        if (kind == BUSTER_NODE_FRONT_LEAF_IDENTIFIER)
        {
            node_facts_window_name(&flat->window->facts, source->window_kinds, source->window_entries, place, token);
        }
    }
    return given ? 0 : BUSTER_NODE_FLAT_UNMAPPED;
}

static uint8_t node_facts_flat_operator(NodeFlat* flat, CheckInputReader* reader, uint32_t ordinal, uint32_t place, const NodeFlatRecord* record)
{
    const CheckOp op = (CheckOp)record->op;
    const uint32_t count = record->count;
    const bool call = op == CHECK_OP_CALL;
    NodeFront* source = &flat->window->candidate;
    const bool listed = !call || (count < 255 && record->payload <= flat->argument_count && count - 1 <= flat->argument_count - record->payload &&
                                  count <= BUSTER_NODE_FRONT_RECORDS - source->window_argument_count);
    uint8_t refusal = count != 0 && listed && (call || count <= 3) ? 0 : BUSTER_NODE_FLAT_UNMAPPED;
    uint32_t operands[255];
    operands[1] = BUSTER_NODE_FRONT_UNKNOWN;
    operands[2] = BUSTER_NODE_FRONT_UNKNOWN;
    for (uint32_t k = 0; refusal == 0 && k < count; k += 1)
    {
        const uint32_t distance = call && k != 0 ? flat->argument_distances[record->payload + k - 1] : record->distances[k];
        refusal = node_facts_flat_operand(flat, ordinal, distance, &operands[k]);
    }
    for (uint32_t k = 0; refusal == 0 && k < count; k += 1)
    {
        source->window_ups[operands[k]] = (uint16_t)(place - operands[k]);
    }
    const uint32_t token = record->token;
    if (refusal == 0 && call)
    {
        node_facts_window_call(source, reader, place, token, operands, count);
    }
    else if (refusal == 0)
    {
        const uint32_t entry = op == CHECK_OP_CAST ? ((record->flags & BUSTER_NODE_FLAT_ROOT) != 0 ? BUSTER_TYPE_VOID : reader->query_types[token]) :
                               op == CHECK_OP_INITIALIZE ? reader->query_types[token] : op == CHECK_OP_CONVERT ? reader->return_types[token] :
                               op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW ? reader->parser->tokens.names[token + 1] : 0;
        node_facts_window_operator(source, reader, place, op, token, operands[0], operands[1], operands[2], entry);
    }
    return refusal;
}

static void node_facts_flat_chunk(NodeFront* front, CheckInputReader* reader)
{
    const uint64_t t0 = __rdtsc();
    NodeFlat* flat = front->flat;
    NodeFlatWindow* window = flat->window;
    NodeFront* source = &window->candidate;
    source->window_argument_count = 0;
    uint32_t place = front->made;
    // A ring overrun loses the chunk's first records and hence its compacted numbering.
    if (flat->ordinal - flat->chunk_ordinal < BUSTER_NODE_FLAT_RECORDS)
    {
        const uint32_t count = node_facts_flat_compact(flat, window->ordinals);
        for (uint32_t i = 0; i < count && place < BUSTER_NODE_FRONT_RECORDS; i += 1)
        {
            const uint32_t ordinal = window->ordinals[i];
            const uint32_t slot = ordinal & (BUSTER_NODE_FLAT_RECORDS - 1);
            const NodeFlatRecord* record = &flat->records[slot];
            window->places[slot] = (uint16_t)place;
            source->ops[place] = record->op;
            source->tokens[place] = record->token;
            source->window_ups[place] = 0;
            window->refusals[place] = record->op == CHECK_OP_VALUE ? node_facts_flat_leaf(flat, reader, place, record) :
                                                                     node_facts_flat_operator(flat, reader, ordinal, place, record);
            if ((reader->pp->options.node_reader & BUSTER_NODE_READER_FLAT_WINDOW_FAULT) != 0 && window->refusals[place] == 0)
            {
                source->window_positions[place] ^= 1u;
            }
            place += 1;
        }
    }
    source->count = place;
    front->shadow->flat_window_ticks += __rdtsc() - t0;
    memset(window->taken, 0, front->count);
}

// The shadow admits a complete reference expression only where every record has the same slot and the flat producer
// supplied all its inputs. Compare every written field before installing it for the regular T, tests, C and X runs.
static void node_facts_flat_expression(NodeFlatWindow* window, uint32_t record, uint32_t start, uint32_t size, uint32_t count)
{
    NodeFront* target = window->target;
    NodeFront* source = &window->candidate;
    uint8_t refusal = start < target->made ? BUSTER_NODE_FLAT_CROSS : 0;
    for (uint32_t r = 0; refusal == 0 && r < count; r += 1)
    {
        const uint32_t x = r < size ? start + r : record;
        refusal = x >= source->count || source->tokens[x] != target->tokens[x] || source->ops[x] != target->ops[x] ? BUSTER_NODE_FLAT_UNMAPPED : window->refusals[x];
    }
    if (refusal != 0)
    {
        target->shadow->flat_window_cross += refusal == BUSTER_NODE_FLAT_CROSS;
        target->shadow->flat_window_far += refusal == BUSTER_NODE_FLAT_FAR;
        target->shadow->flat_window_other += refusal == BUSTER_NODE_FLAT_UNMAPPED;
        return;
    }
    target->shadow->flat_window_roots += 1;
    target->shadow->flat_window_records += count;
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t x = r < size ? start + r : record;
        window->taken[x] = 1;
        const uint8_t kind = source->window_kinds[x];
        const uint32_t entry = source->window_entries[x];
        const uint32_t prior = target->window_entries[x];
        bool same = target->window_kinds[x] == kind && target->window_classes[x] == source->window_classes[x] &&
                    target->window_left[x] == source->window_left[x] && target->window_right[x] == source->window_right[x] && target->window_positions[x] == source->window_positions[x];
        if (r + 1 < count)
        {
            same = same && target->window_ups[x] == source->window_ups[x];
            target->window_ups[x] = source->window_ups[x];
        }
        if (kind == BUSTER_NODE_FACTS_LITERAL_LINE)
        {
            const uint64_t* own = window->facts.lines + 4 * (uint64_t)entry;
            same = same && prior == entry && memcmp(target->window_facts->lines + 4 * (uint64_t)entry, own, 4 * sizeof(uint64_t)) == 0;
            memcpy(target->window_facts->lines + 4 * (uint64_t)entry, own, 4 * sizeof(uint64_t));
            target->window_entries[x] = entry;
        }
        else if ((CheckOp)source->ops[x] == CHECK_OP_CALL)
        {
            const uint32_t args = source->window_counts[x];
            same = same && args == target->window_counts[x] && memcmp(target->window_arguments + prior, source->window_arguments + entry, args * sizeof(uint16_t)) == 0;
            if (args == target->window_counts[x] && prior <= target->window_argument_count && args <= target->window_argument_count - prior)
            {
                memcpy(target->window_arguments + prior, source->window_arguments + entry, args * sizeof(uint16_t));
            }
        }
        else
        {
            if (kind == BUSTER_NODE_FACTS_IDENTIFIER)
            {
                // The kernel uses the unit's common arena. Its storage slot can differ between two independently
                // built name tables. Compare the four words, then give it the flat producer's words in that slot.
                const bool room = target->window_kinds[x] == kind && prior != 0 && prior < target->window_facts->line_capacity &&
                                  entry != 0 && entry < window->facts.line_capacity;
                if (room)
                {
                    const uint64_t* own = window->facts.lines + 4 * (uint64_t)entry;
                    uint64_t* placed = target->window_facts->lines + 4 * (uint64_t)prior;
                    same = same && memcmp(placed, own, 4 * sizeof(uint64_t)) == 0;
                    memcpy(placed, own, 4 * sizeof(uint64_t));
                }
                else
                {
                    same = false;
                }
            }
            else
            {
                same = same && prior == entry;
                target->window_entries[x] = entry;
            }
        }
        target->window_kinds[x] = kind;
        target->window_classes[x] = source->window_classes[x];
        target->window_left[x] = source->window_left[x];
        target->window_right[x] = source->window_right[x];
        target->window_positions[x] = source->window_positions[x];
        if (!same)
        {
            node_flat_differs(target->shadow, source->tokens[x], BUSTER_NODE_FLAT_FIELD_WINDOW);
        }
    }
}
