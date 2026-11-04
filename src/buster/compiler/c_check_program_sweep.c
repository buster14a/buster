// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1048's statement sweep, an experiment (BUSTER_LOWER_EXPERIMENT bit 64): a statement whose every node is of the
// ordinary shapes is lowered by two passes over its nodes, in the graph's postorder, rather than by the tasks: the first,
// from its root down, gives each operand its context (evaluated, or not) and its conversion's target and decides each
// node's own entries; the second, from its first node up, writes each node's entries and then its conversion, which is
// the order the tasks write them in for these shapes. Any other statement goes to the tasks unchanged. Included by
// c_check_program_lower.c after check_lower_expand, whose rules it follows case for case.

// The first node of `node`'s subtree in the graph's postorder: its first operands' down to a leaf (at most 4096 of them).
static uint32_t check_lower_subtree_first(const CheckGraph* graph, uint32_t node)
{
    uint32_t first = node;
    uint32_t next = node;
    for (uint32_t steps = 0; steps < 4096 && next != BUSTER_CHECK_NONE; steps += 1)
    {
        first = next;
        const uint32_t left = graph->operand0[first];
        const uint32_t right = graph->operand1[first];
        next = left != BUSTER_CHECK_NONE && left < first ? left : right != BUSTER_CHECK_NONE && right < first ? right : BUSTER_CHECK_NONE;
    }
    return first;
}

#define BUSTER_CHECK_LOWER_SWEEP_UNSET 0u
#define BUSTER_CHECK_LOWER_SWEEP_VALUE 1u
#define BUSTER_CHECK_LOWER_SWEEP_SKIP 2u

// Whether a type's class and facts (CheckLowerShared.type_templates' layout) are a plain scalar's: a class from I8 to F64
// (or VOID, where `void_ok`), and none of the facts whose rules go elsewhere.
static inline __attribute__((always_inline)) bool check_lower_sweep_plain(uint32_t info, bool void_ok)
{
    const uint8_t class = (uint8_t)info;
    const uint8_t facts = (uint8_t)(info >> 8);
    const bool classed = (class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_F64) || (void_ok && class == BUSTER_TYPED_CLASS_VOID);
    return classed && (facts & (BUSTER_CHECK_LOWER_TYPE_AGGREGATE | BUSTER_CHECK_LOWER_TYPE_COMPLEX | BUSTER_CHECK_LOWER_TYPE_ATOMIC | BUSTER_CHECK_LOWER_TYPE_BIT_INT)) == 0;
}

static inline __attribute__((always_inline)) uint32_t check_lower_sweep_info(const CheckLowerWriter* writer, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < writer->type_count ? writer->type_templates[index] : UINT8_MAX;
}

// Gives operand `child` of node `node` (at `first`'s offset) its state and its conversion's target: false where it isn't a
// node of the statement's range below `node`, or another operand already gave it one (a shared operand).
static inline __attribute__((always_inline)) bool check_lower_sweep_mark(CheckLowerWriter* writer, uint32_t first, uint32_t node, uint32_t child, uint8_t state, TypeId target)
{
    const bool valid = child != BUSTER_CHECK_NONE && child >= first && child < node && writer->sweep_state[child - first] == BUSTER_CHECK_LOWER_SWEEP_UNSET;
    if (valid)
    {
        writer->sweep_state[child - first] = state;
        writer->sweep_targets[child - first] = target;
    }
    return valid;
}

// Lowers the expression `root`, converted to `target` (check_lower_expression's, not as an address), by the sweep: true
// where it took the statement, having written its entries; false, having written nothing, where the statement has a node
// of another shape.
static bool check_lower_sweep(CheckLowerWriter* writer, uint32_t root, TypeId target)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckInput* input = program->input;
    bool taken = root < stream->count && stream->dense_values == NULL && stream->overload_address_count == 0 && input->overload_heads == NULL &&
                 writer->type_templates != NULL && !writer->scalar && !writer->bit_ints && !writer->track && !writer->sources && !writer->templates &&
                 graph->argument_counts != NULL;
    const uint32_t first = taken ? check_lower_subtree_first(graph, root) : root;
    const uint32_t size = root + 1 - first;
    taken = taken && size <= BUSTER_CHECK_LOWER_SWEEP_NODES;
    // The shapes' ops, a word's bits: a statement with any other op goes to the tasks before any of its columns is read.
    const uint64_t accepted = (UINT64_C(1) << CHECK_OP_VALUE) | (UINT64_C(1) << CHECK_OP_CAST) | (UINT64_C(1) << CHECK_OP_CONVERT) | (UINT64_C(1) << CHECK_OP_POSITIVE) |
                              (UINT64_C(1) << CHECK_OP_NEGATIVE) | (UINT64_C(1) << CHECK_OP_BIT_NOT) | (UINT64_C(1) << CHECK_OP_NOT) | (UINT64_C(1) << CHECK_OP_CONDITION) |
                              (UINT64_C(1) << CHECK_OP_ASSIGN) | (((UINT64_C(1) << (CHECK_OP_BIT_XOR + 1)) - 1) & ~((UINT64_C(1) << CHECK_OP_ADD) - 1));
    for (uint32_t i = 0; taken && i < size; i += 1)
    {
        const CheckOp op = graph->ops[first + i];
        taken = op < 64 && ((accepted >> op) & 1) != 0;
    }
    if (taken)
    {
        memset(writer->sweep_state, 0, size);
        writer->sweep_state[size - 1] = BUSTER_CHECK_LOWER_SWEEP_VALUE;
        writer->sweep_targets[size - 1] = target;
    }
    for (uint32_t i = size; taken && i-- > 0;)
    {
        const uint32_t n = first + i;
        const uint8_t state = writer->sweep_state[i];
        const CheckOp op = graph->ops[n];
        const uint32_t a = graph->operand0[n];
        const uint32_t b = graph->operand1[n];
        const uint32_t c = graph->operand2[n];
        uint8_t count = 0;
        taken = state != BUSTER_CHECK_LOWER_SWEEP_UNSET && graph->argument_counts[n] == 0;
        if (taken && state == BUSTER_CHECK_LOWER_SWEEP_SKIP)
        {
            taken = (a == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0)) &&
                    (b == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, b, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0)) &&
                    (c == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, c, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0));
        }
        else if (taken)
        {
            const uint32_t row = stream->payload_rows[n];
            const CheckValue* payload = row != 0 && row <= stream->payload_count ? stream->payloads + row - 1 : NULL;
            const TypeId type = payload != NULL ? payload->type : stream->value_types[n];
            const uint32_t flags = payload != NULL ? payload->flags : stream->value_flags[n];
            const CheckCategory category = payload != NULL ? payload->category : (CheckCategory)stream->value_categories[n];
            const CheckConstantKind constant_kind = payload != NULL ? payload->constant_kind : (CheckConstantKind)stream->value_constants[n];
            const uint32_t object = payload != NULL ? payload->object : stream->value_objects[n];
            const bool faulted = payload != NULL && payload->fault != CHECK_FAULT_NONE;
            const uint32_t info = check_lower_sweep_info(writer, type);
            const uint8_t facts = (uint8_t)(info >> 8);
            const uint8_t class = (uint8_t)info;
            const bool address_value = (facts & BUSTER_CHECK_LOWER_TYPE_ADDRESS) != 0;
            const TypeId type_a = stream->conversions[n].operands[0].type;
            const TypeId type_b = stream->conversions[n].operands[1].type;
            const uint32_t info_a = check_lower_sweep_info(writer, type_a);
            const uint32_t info_b = check_lower_sweep_info(writer, type_b);
            const bool constant = constant_kind != CHECK_CONSTANT_NONE && !faulted && (flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_FP_ENVIRONMENT)) == 0;
            const bool object_valid = object != 0 && object < program->object_count;
            // Its own type, converted to its target (the conversion the tasks write after it): both plain scalars' types.
            const TypeId to = writer->sweep_targets[i];
            taken = check_lower_sweep_plain(info, op == CHECK_OP_CAST || op == CHECK_OP_ASSIGN) &&
                    (to == 0 || to == type || check_lower_sweep_plain(check_lower_sweep_info(writer, to), true)) && class < writer->class_limit;
            if (!taken)
            {
            }
            else if (op == CHECK_OP_VALUE && object_valid && check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND) &&
                     !check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_STATIC))
            {
                taken = false;
            }
            else if (op == CHECK_OP_STATEMENT_VALUE || (category == CHECK_LVALUE && (facts & BUSTER_CHECK_LOWER_TYPE_ATOMIC) != 0))
            {
                taken = false;
            }
            else if (constant && op != CHECK_OP_INITIALIZE && op != CHECK_OP_BUILTIN_QUERY && (category == CHECK_RVALUE || (address_value && constant_kind == CHECK_CONSTANT_ADDRESS)))
            {
                // check_lower_constant's integer of the classes I8 to U64, its bits sign- or zero-extended from its class's.
                taken = category == CHECK_RVALUE && constant_kind == CHECK_CONSTANT_INTEGER && class >= BUSTER_TYPED_CLASS_I8 && class <= BUSTER_TYPED_CLASS_U64;
                uint64_t bits = payload != NULL ? (uint64_t)payload->integer : 0;
                const uint32_t width = 8u << ((class - 1) / 2);
                if (taken && width < 64)
                {
                    const uint64_t mask = (UINT64_C(1) << width) - 1;
                    bits &= mask;
                    bits |= (class & 1) != 0 && (bits >> (width - 1)) != 0 ? ~mask : 0;
                }
                writer->sweep_kinds[i][0] = BUSTER_TYPED_CONST;
                writer->sweep_classes[i][0] = class;
                writer->sweep_values[i][0] = bits;
                count = 1;
                taken = taken && (a == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0)) &&
                        (b == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, b, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0)) &&
                        (c == BUSTER_CHECK_NONE || check_lower_sweep_mark(writer, first, n, c, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0));
            }
            else if (op == CHECK_OP_VALUE)
            {
                const uint32_t local = object_valid ? check_lower_local_of(writer, object) : BUSTER_CHECK_NONE;
                const SymbolId symbol = object_valid ? program->object_symbols[object] : 0;
                taken = type != BUSTER_TYPE_VOID && !address_value && category == CHECK_LVALUE && object_valid && (local != BUSTER_CHECK_NONE || symbol != 0);
                writer->sweep_kinds[i][0] = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_GLOBAL;
                writer->sweep_classes[i][0] = class;
                writer->sweep_values[i][0] = local != BUSTER_CHECK_NONE ? local : symbol;
                count = 1;
            }
            else if (op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_POSITIVE)
            {
                taken = check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_VALUE, type);
            }
            else if (op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT)
            {
                writer->sweep_kinds[i][0] = op == CHECK_OP_NEGATIVE ? BUSTER_TYPED_NEG : BUSTER_TYPED_NOT;
                writer->sweep_classes[i][0] = class;
                writer->sweep_values[i][0] = 0;
                count = 1;
                taken = check_lower_sweep_plain(info_a, false) && check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_VALUE, type_a);
            }
            else if (op == CHECK_OP_NOT || op == CHECK_OP_CONDITION)
            {
                // check_lower_truth's: a narrow operand converted to I32 first, then compared with zero.
                const uint8_t class_a = (uint8_t)info_a;
                const bool narrow = check_lower_narrow(class_a);
                if (narrow)
                {
                    writer->sweep_kinds[i][count] = BUSTER_TYPED_CONVERT;
                    writer->sweep_classes[i][count] = BUSTER_TYPED_CLASS_I32;
                    writer->sweep_values[i][count] = 0;
                    count += 1;
                }
                writer->sweep_kinds[i][count] = BUSTER_TYPED_CONST;
                writer->sweep_classes[i][count] = narrow ? BUSTER_TYPED_CLASS_I32 : class_a;
                writer->sweep_values[i][count] = 0;
                count += 1;
                writer->sweep_kinds[i][count] = op == CHECK_OP_NOT ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE;
                writer->sweep_classes[i][count] = BUSTER_TYPED_CLASS_I32;
                writer->sweep_values[i][count] = 0;
                count += 1;
                taken = check_lower_sweep_plain(info_a, false) && check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_VALUE, type_a);
            }
            else if (op == CHECK_OP_ASSIGN)
            {
                const TypeId destination = a < stream->count ? stream->value_types[a] : 0;
                const uint32_t destination_object = a < stream->count && graph->ops[a] == CHECK_OP_VALUE ? check_stream_object(stream, a) : 0;
                const bool destination_valid = destination_object != 0 && destination_object < program->object_count;
                const uint32_t local = destination_valid ? check_lower_local_of(writer, destination_object) : BUSTER_CHECK_NONE;
                const SymbolId symbol = destination_valid ? program->object_symbols[destination_object] : 0;
                taken = a < stream->count && (check_lower_sweep_info(writer, destination) >> 8 & BUSTER_CHECK_LOWER_TYPE_ATOMIC) == 0 &&
                        (stream->value_flags[a] & BUSTER_CHECK_BIT_FIELD) == 0 && class != BUSTER_TYPED_CLASS_VOID && (local != BUSTER_CHECK_NONE || symbol != 0);
                writer->sweep_kinds[i][0] = local != BUSTER_CHECK_NONE ? BUSTER_TYPED_STORE_LOCAL : BUSTER_TYPED_STORE_GLOBAL;
                writer->sweep_classes[i][0] = class;
                writer->sweep_values[i][0] = local != BUSTER_CHECK_NONE ? local : symbol;
                count = 1;
                taken = taken && check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_SKIP, 0) &&
                        check_lower_sweep_mark(writer, first, n, b, BUSTER_CHECK_LOWER_SWEEP_VALUE, type);
            }
            else if (op >= CHECK_OP_ADD && op <= CHECK_OP_BIT_XOR)
            {
                const bool pointer = ((info_a | info_b) >> 8 & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0;
                writer->sweep_kinds[i][0] = check_lower_binary(op);
                writer->sweep_classes[i][0] = class;
                writer->sweep_values[i][0] = 0;
                count = 1;
                taken = !pointer && check_lower_binary(op) != BUSTER_TYPED_NONE && check_lower_sweep_plain(info_a, false) && check_lower_sweep_plain(info_b, false) &&
                        check_lower_sweep_mark(writer, first, n, a, BUSTER_CHECK_LOWER_SWEEP_VALUE, type_a) &&
                        check_lower_sweep_mark(writer, first, n, b, BUSTER_CHECK_LOWER_SWEEP_VALUE, type_b);
            }
            else
            {
                taken = false;
            }
            writer->sweep_types[i] = type;
        }
        writer->sweep_count[i] = count;
    }
    if (taken)
    {
        // The entries in node order: each node's own, then its conversion to its operator's operand (check_lower_convert's
        // test, check_lower_conversion_body's entries).
        for (uint32_t i = 0; i < size; i += 1)
        {
            if (writer->sweep_state[i] == BUSTER_CHECK_LOWER_SWEEP_VALUE)
            {
                for (uint32_t k = 0; k < writer->sweep_count[i]; k += 1)
                {
                    check_lower_emit(writer, writer->sweep_kinds[i][k], writer->sweep_classes[i][k], writer->sweep_values[i][k]);
                }
                const TypeId from = writer->sweep_types[i];
                const TypeId to = writer->sweep_targets[i];
                if (to != 0 && from != to && !check_lower_conversion_empty(writer, from, to))
                {
                    check_lower_conversion_body(writer, from, to, false);
                }
                writer->sweep_nodes += 1;
            }
        }
        writer->sweep_statements += 1;
    }
    return taken;
}
