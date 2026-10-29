// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Prepare sizeof's operand types after binding, before graph construction. The outermost expression fold publishes
// every nested sizeof's operand type in sizeof_types; query_types remains exclusively the type-name query column.
// The graph still retains and checks expression operands (including bit-fields). No result survives this reader.
// Only this pass enables capture: earlier declaration/deferred-lane folds can hold private, not-yet-remapped TypeIds.

static TypeId check_input_sizeof_operand(CheckInputReader* reader, uint32_t token)
{
    const uint32_t first = token + 1;
    const uint32_t end = reader->ends[token] + 1;
    reader->output->sizeof_queries += 1;
    reader->output->sizeof_query_tokens += end - first;
    CheckValue value = { 0 };
    const bool evaluated = check_input_evaluate_cached(reader, first, end, &value);
    return evaluated ? value.type : 0;
}

static void check_input_sizeof_prepare(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    uint32_t covered_end = 0;
    reader->sizeof_capture = true;
    for (uint32_t first = 0; first < count && !reader->output->resource; first += 64)
    {
        // Classify bounded 64-token blocks; the scalar twin visits each token. sizeof intervals are nested or
        // disjoint, so source order coalesces unevaluated spans without a stack, sorting, or per-query rescans.
        uint64_t active = scalar ? check_input_scan_all(first, count) : check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_SIZEOF);
        while (active != 0 && !reader->output->resource)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(active);
            active &= active - 1;
            if (token < covered_end || parser->tokens.kinds[token] != PARSE_KIND_SIZEOF || parser->tokens.names[token] != C_NAME_SIZEOF ||
                parser->tree.roles[token] != PARSE_ROLE_PREFIX || reader->ignored[token] ||
                check_input_expression_end(reader, token) > check_input_expression_limit(reader, token) ||
                (reader->output->deferred_tokens != NULL && reader->output->deferred_tokens[token] != 0))
            {
                continue;
            }
            TypeId requested = reader->query_types[token] != 0 ? reader->query_types[token] : reader->sizeof_types[token];
            if (requested == 0)
            {
                requested = check_input_sizeof_operand(reader, token);
            }
            reader->sizeof_types[token] = requested;
            if (requested != 0 && !check_input_vla_runtime_size(reader->types, requested))
            {
                const uint32_t end = reader->ends[token] + 1;
                // Independent type-name bounds remain live: warning_range excludes their marked tokens. Nested
                // expression queries already have their operand types, and need neither another fold nor this scan.
                check_input_warning_suppress(reader, token + 1, end, check_constant_unevaluated_warnings());
                covered_end = end;
            }
        }
    }
    reader->sizeof_capture = false;
}
