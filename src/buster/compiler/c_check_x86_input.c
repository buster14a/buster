// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// An unselected choose/generic arm is checked outside Clang's deferred runtime CFG analysis: its range errors
// survive dead statements and short circuits. An enclosing unevaluated operand still suppresses them. Transfer
// that source context to the sparse x86 call records before checking, without reconnecting executable operands.
// Two bounded interval scans share the packed diagnostic's scalar/vector prefix kernel. Scratch is six bytes per
// token plus one u32 sentinel, allocated only for inputs with both selections and x86 calls and released here.

static void check_input_x86_contexts(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    CheckGraph* graph = &reader->output->graph;
    const uint64_t mark = reader->scratch->length;
    uint32_t* events = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)count + 1);
    uint8_t* selected = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
    uint8_t* evaluated = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, count);
    const bool valid = events != NULL && selected != NULL && evaluated != NULL;
    reader->output->resource |= !valid;
    if (valid)
    {
        memset(events, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t first = 0; first < count; first += 16)
        {
            const uint32_t end = count - first < 16 ? count : first + 16;
            uint32_t present = parser->options.scalar ? (uint32_t)check_input_scan_all(first, end) :
                               check_input_scan_below(reader->selected_tokens, first, count, BUSTER_CHECK_NONE);
            for (; present != 0; present &= present - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(present);
                const uint32_t chosen = check_input_packed_selection(reader, token);
                if (chosen != BUSTER_CHECK_NONE)
                {
                    const uint32_t keyword = reader->first_child[token];
                    check_input_packed_exclude(events, count, keyword + 2, check_input_first_token(reader, chosen));
                    check_input_packed_exclude(events, count, reader->ends[chosen] + 1, token);
                }
            }
        }
        check_input_packed_analyzed(events, selected, count, parser->options.scalar);
        memset(events, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t first = 0; first < count; first += 16)
        {
            const uint32_t end = count - first < 16 ? count : first + 16;
            uint32_t present = parser->options.scalar ? (uint32_t)check_input_scan_all(first, end) :
                               check_input_scan_below(reader->selected_tokens, first, count, BUSTER_CHECK_NONE) |
                               (uint32_t)check_input_scan_equal(parser->tokens.kinds, first, end, PARSE_KIND_SIZEOF) |
                               (uint32_t)check_input_scan_equal(parser->tokens.kinds, first, end, PARSE_KIND_TYPEOF);
            for (; present != 0; present &= present - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(present);
                const uint8_t kind = parser->tokens.kinds[token];
                if (kind == PARSE_KIND_SIZEOF)
                {
                    check_input_packed_exclude(events, count, token + 1, reader->ends[token] + 1);
                }
                else if (kind == PARSE_KIND_TYPEOF && token + 1 < count)
                {
                    check_input_packed_exclude(events, count, token + 2, parser->tree.parents[token + 1]);
                }
                else if (reader->selected_tokens[token] != BUSTER_CHECK_NONE && reader->first_child[token] < count &&
                         parser->tokens.names[reader->first_child[token]] == C_NAME_GENERIC)
                {
                    const uint32_t first = reader->first_child[token] + 2;
                    const uint32_t separator = check_constant_punctuation(parser, first, token, PARSE_KIND_COMMA);
                    check_input_packed_exclude(events, count, first, separator);
                }
            }
        }
        check_input_packed_analyzed(events, evaluated, count, parser->options.scalar);
        for (uint32_t i = 0; i < graph->x86_count; i += 1)
        {
            CheckX86Call* call = graph->x86_calls + i;
            const uint32_t token = graph->tokens[call->node];
            call->runtime_diagnostic_suppressed = evaluated[token] == 0;
            call->unselected_context = selected[token] == 0;
        }
    }
    arena_pop_to(reader->scratch, mark);
}
