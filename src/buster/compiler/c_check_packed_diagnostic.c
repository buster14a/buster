// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Cancel pending packed-member diagnostics at actual consuming conversions. A byte per graph node carries the
// smallest requested pointee alignment (0 also covers integer/incomplete-pointer conversions, 255 means none).
// Reverse postorder propagates the enclosing target through C's comma RHS and conditional arms, matching Clang's
// implicit-conversion analysis. Explicit casts are boundaries and sizeof/typeof retain their source witnesses.
// Scratch has graph.count logical bytes, no padding; source records and graph/stream identities remain stable.

static uint8_t check_packed_conversion_alignment(const TypeTable* types, TypeId from, TypeId to)
{
    uint8_t result = UINT8_MAX;
    if (to != 0 && to != from)
    {
        if (check_action_is_integer(types, to))
        {
            result = 0;
        }
        else if (check_action_kind(types, to) == BUSTER_TYPE_KIND_POINTER)
        {
            const TypeId pointee = types->types.base[type_index(type_strip_aligned(types, to))];
            result = type_is_complete(types, pointee) ? (uint8_t)type_alignment_log2(types, pointee) : 0;
        }
    }
    return result;
}

static bool check_input_packed_finalize(CheckInput* input, const Parser* parser, const TypeTable* types, const CheckStream* stream, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const CheckGraph* graph = &input->graph;
    uint8_t* targets = input->packed_member_count != 0 && graph->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, graph->count) : 0;
    const bool valid = input->packed_member_count == 0 || graph->count == 0 || targets != 0;
    input->resource |= !valid;
    if (valid && targets != 0)
    {
        memset(targets, UINT8_MAX, graph->count);
        for (uint32_t node = graph->count; node-- > 0;)
        {
            const CheckOp op = graph->ops[node];
            const uint32_t token = graph->tokens[node];
            const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operands); i += 1)
            {
                const uint32_t operand = operands[i];
                if (operand != BUSTER_CHECK_NONE)
                {
                    const bool explicit = op == CHECK_OP_CAST && parser->tree.roles[token] == PARSE_ROLE_CLOSE_CAST;
                    const bool comparison = op >= CHECK_OP_LESS && op <= CHECK_OP_NOT_EQUAL;
                    const bool boolean = op == CHECK_OP_CONDITION || op == CHECK_OP_NOT || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR ||
                                         (op == CHECK_OP_CONDITIONAL && i == 0);
                    // Explicit casts cancel only a directly addressed member, already handled by the source
                    // witness. Their target must not flow into a conditional/comma expression inside the cast.
                    const TypeId target = explicit || comparison ? 0 : boolean ? BUSTER_TYPE_BOOL : stream->conversions[node].operands[i].type;
                    uint8_t alignment = check_packed_conversion_alignment(types, stream->values[operand].type, target);
                    const bool inherit = op == CHECK_OP_SELECT || (op == CHECK_OP_COMMA && i == 1) || (op == CHECK_OP_CONDITIONAL && i != 0);
                    alignment = inherit && targets[node] < alignment ? targets[node] : alignment;
                    targets[operand] = alignment < targets[operand] ? alignment : targets[operand];
                }
            }
            if (check_graph_has_arguments(op))
            {
                const uint32_t begin = graph->argument_offsets[node];
                for (uint32_t i = 0; i < graph->argument_counts[node]; i += 1)
                {
                    const uint32_t operand = graph->argument_nodes[begin + i];
                    // assume_aligned validates a generic pointer but Clang retains the original argument's
                    // type in its AST. Our lowering conversion to void * is not a source implicit conversion.
                    const bool generic_pointer = op == CHECK_OP_BUILTIN_MISC && graph->members[node] == CBUILTIN_ASSUME_ALIGNED && i == 0;
                    const TypeId target = generic_pointer ? 0 : stream->argument_conversions[begin + i].type;
                    const uint8_t alignment = check_packed_conversion_alignment(types, stream->values[operand].type, target);
                    targets[operand] = alignment < targets[operand] ? alignment : targets[operand];
                }
            }
        }
        for (uint32_t i = 0; i < input->packed_member_count; i += 1)
        {
            CheckInputPackedMember* member = input->packed_members + i;
            const uint32_t node = input->token_nodes[member->address];
            member->suppressed |= member->implicit_conversions && node < graph->count && targets[node] <= member->alignment_log2;
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}
