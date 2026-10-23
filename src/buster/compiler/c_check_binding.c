// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checker-side consumption of the ordered CBindings source events. Include after CheckInputReader and its resolver,
// before check_input. Declaration/type elaboration still needs immediate visibility in source order; expression
// uses are resolved here, together, and the expression reader consumes the resulting token columns without lookup.
// The two kernels return activation handles. Only original declaration tokens escape this helper, so prototype
// and body activations of a parameter retain one source identity. All temporary event results belong to scratch.
// Event count includes restores; statistics.identifiers counts declaration/use events, including reactivation,
// not distinct source identifier tokens. Scalar replay has zero candidate-walk/fallback counters. An unresolved
// use writes BUSTER_CHECK_NONE and type 0 before reporting it. A failed kernel or event-result allocation writes no
// use results; any false result leaves the unit unusable. Empty streams allocate nothing; no column has padding.

static bool check_input_bindings(CheckInputReader* reader, bool scalar)
{
    CheckInput* output = reader->output;
    const CBindings* bindings = &reader->bindings;
    const uint64_t mark = reader->scratch->length;
    output->binding_event_count = bindings->count;
    output->binding_statistics = (CBindingStatistics){ 0 };
    bool valid = bindings->valid && !output->resource;
    uint32_t* resolved = valid && bindings->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, bindings->count) : 0;
    valid = valid && (bindings->count == 0 || resolved != 0);
    if (valid && bindings->count != 0)
    {
        valid = scalar ? c_bindings_scalar(bindings, resolved, reader->scratch) :
                         c_bindings_batch(bindings, resolved, reader->scratch, &output->binding_statistics);
    }
    if (!valid)
    {
        output->resource = true;
    }
    for (uint32_t event = 0; valid && event < bindings->count; event += 1)
    {
        const uint8_t operation = bindings->operation[event];
        if (scalar)
        {
            output->binding_statistics.identifiers += operation != C_BINDING_RESTORE;
            output->binding_statistics.declarations += operation == C_BINDING_DECLARE;
        }
        if (operation == C_BINDING_USE)
        {
            const uint32_t token = bindings->declaration[event];
            const uint32_t handle = resolved[event];
            const uint32_t declaration = handle != 0 ? bindings->declaration[handle - 1] : BUSTER_CHECK_NONE;
            output->declarations[token] = declaration;
            if (bindings->key[event] / bindings->name_capacity == C_BINDING_ORDINARY)
            {
                reader->declared_types[token] = handle != 0 ? reader->declared_types[declaration] : 0;
                if (handle != 0 && (reader->declared_types[token] == 0 ||
                    (bindings->kind[handle - 1] == C_BINDING_TYPEDEF && reader->parser->tree.roles[token] == PARSE_ROLE_OPERAND)))
                {
                    check_input_report(reader, token, CHECK_INPUT_DECLARATION);
                }
            }
            if (handle == 0)
            {
                check_input_report(reader, token, CHECK_INPUT_UNDECLARED);
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return valid && !output->resource && output->report_count == 0;
}
