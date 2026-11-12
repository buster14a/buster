// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Checked switch constraints and diagnostic-only scans of complete earlier graph fragments. Include after the
// program case conversion helpers. This consumer never repairs source metadata or clears real checker reports.
// Switch completion orders default, singleton and range checks separately. The diagnostic location remains
// separate from the source position at which the complete constraint is available. Rules were verified with
// Clang 23 source oracles and SemaStmt.cpp; no implementation was copied:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.3/clang/lib/Sema/SemaStmt.cpp
#ifndef BUSTER_C_CHECK_PROGRAM_DIAGNOSTIC_INCLUDED
#define BUSTER_C_CHECK_PROGRAM_DIAGNOSTIC_INCLUDED

typedef struct CheckProgramDiagnostic CheckProgramDiagnostic;
struct CheckProgramDiagnostic
{
    CheckProgramStatus status;
    uint32_t token;
    uint32_t position;
    CheckProgramFailure failure;
    // check_program_diagnose_all's: every constraint error, at clang's places (check_program_diagnostic_location's).
    const CheckProgramConstraint* constraints;
    uint32_t constraint_count;
};

// A constraint error at `token`, complete at `position`: the first one, by position, is the program's; a list
// (check_program_diagnose_all) takes every one, with its words' facts and its note's token. `kept` false lists it alone.
static void check_program_constraint_record(CheckProgram* program, uint32_t token, uint32_t position, CheckProgramFailure failure, uint32_t previous,
                                            uint32_t node, bool kept)
{
    if (kept && (program->status == CHECK_PROGRAM_OK || (program->status == CHECK_PROGRAM_INVALID && position <= program->diagnostic_position)))
    {
        program->status = CHECK_PROGRAM_INVALID;
        program->token = token;
        program->diagnostic_position = position;
        program->failure = failure;
    }
    if (program->constraints != 0 && program->constraint_count < program->constraint_capacity)
    {
        program->constraints[program->constraint_count++] = (CheckProgramConstraint){
            .token = token,
            .position = position,
            .previous = previous,
            .node = node,
            .failure = failure,
        };
    }
}

static void check_program_constraint_at(CheckProgram* program, uint32_t token, uint32_t position)
{
    check_program_constraint_record(program, token, position, (CheckProgramFailure){ 0 }, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, true);
}

static void check_program_constraint(CheckProgram* program, uint32_t token)
{
    check_program_constraint_at(program, token, token);
}

static uint32_t check_program_diagnostic_location(const Parser* parser, uint32_t token)
{
    uint32_t result = token;
    const uint32_t count = parser->tokens.count;
    const uint8_t role = token < count ? parser->tree.roles[token] : PARSE_ROLE_NONE;
    const bool control = role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_CASE;
    for (uint32_t i = 0; control && i < token && i < count && result == token; i += 1)
    {
        if (parser->tree.parents[i] == token)
        {
            if (role == PARSE_ROLE_CLOSE_CONDITION && parser->tokens.kinds[i] == PARSE_KIND_SWITCH)
            {
                result = i;
            }
            else if (role == PARSE_ROLE_CLOSE_CASE && parser->tree.roles[i] == PARSE_ROLE_OPEN_CASE)
            {
                result = parser->tokens.kinds[i] == PARSE_KIND_DEFAULT ? i : i + 1;
            }
        }
    }
    return result;
}

// Statement roots need not follow their children in token order. A bounded leaf queue computes subtree ends
// once without recursion or a per-switch walk. This is cold diagnostic storage, never retained in CheckProgram.
static uint32_t* check_program_statement_ends(CheckProgram* program, const Parser* parser, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint32_t* children = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    uint32_t* queue = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count);
    bool valid = ends != 0 && children != 0 && queue != 0;
    if (valid)
    {
        memset(children, 0, (uint64_t)count * sizeof(uint32_t));
        for (uint32_t token = 0; token < count; token += 1)
        {
            ends[token] = token;
            const uint32_t parent = parser->tree.parents[token];
            if (parent < count && parent != token)
            {
                children[parent] += 1;
            }
        }
        uint32_t length = 0;
        for (uint32_t token = 0; token < count; token += 1)
        {
            if (children[token] == 0)
            {
                queue[length++] = token;
            }
        }
        for (uint32_t at = 0; at < length; at += 1)
        {
            const uint32_t token = queue[at];
            const uint32_t parent = parser->tree.parents[token];
            if (parent < count && parent != token)
            {
                ends[parent] = ends[token] > ends[parent] ? ends[token] : ends[parent];
                children[parent] -= 1;
                if (children[parent] == 0)
                {
                    queue[length++] = parent;
                }
            }
        }
        valid = length == count;
        if (!valid)
        {
            program->status = CHECK_PROGRAM_INVALID;
            program->token = BUSTER_CHECK_NONE;
        }
    }
    else
    {
        program->status = CHECK_PROGRAM_RESOURCE;
        program->token = BUSTER_CHECK_NONE;
    }
    uint32_t* result = valid ? ends : 0;
    return result;
}

// The statements' ends, made at the first conflict: a program without one makes none.
static bool check_program_conflict_ends(CheckProgram* program, const Parser* parser, const uint32_t** ends, Arena* scratch)
{
    if (*ends == 0)
    {
        *ends = check_program_statement_ends(program, parser, scratch);
    }
    return *ends != 0;
}

static void check_program_case_conflicts(CheckProgram* program, const Parser* parser, const uint32_t* ends, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = program->case_count;
    uint32_t* order = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* temporary = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    bool valid = count == 0 || (order != 0 && temporary != 0);
    if (!valid)
    {
        program->status = CHECK_PROGRAM_RESOURCE;
        program->token = BUSTER_CHECK_NONE;
    }
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        order[i] = i;
    }
    for (uint64_t width = 1; valid && width < count; width *= 2)
    {
        for (uint64_t begin = 0; begin < count; begin += width * 2)
        {
            const uint32_t middle = begin + width < count ? (uint32_t)(begin + width) : count;
            const uint32_t end = begin + width * 2 < count ? (uint32_t)(begin + width * 2) : count;
            uint32_t a = (uint32_t)begin;
            uint32_t b = middle;
            for (uint32_t at = (uint32_t)begin; at < end; at += 1)
            {
                const bool take_a = a < middle && (b == end || !check_program_case_before(program, order[b], order[a]));
                temporary[at] = take_a ? order[a++] : order[b++];
            }
        }
        uint32_t* swap = order;
        order = temporary;
        temporary = swap;
    }
    uint32_t first = 0;
    while (valid && first < count)
    {
        const uint32_t owner = program->cases[order[first]].owner;
        uint32_t end = first + 1;
        while (end < count && program->cases[order[end]].owner == owner)
        {
            end += 1;
        }
        uint32_t values = first;
        while (values < end && program->cases[order[values]].fallback)
        {
            values += 1;
        }
        uint32_t ranges = values;
        while (ranges < end && !program->cases[order[ranges]].range)
        {
            ranges += 1;
        }
        // Clang's order (ActOnFinishSwitchStmt), each error noted at the case it repeats, all published at the switch's
        // end: the defaults by a reverse source walk, each but the last one's at it, noting the next; then the singletons
        // in converted value order (source order among equals), each repeating the one before; then the nonempty GNU
        // ranges by their low value, each noting the last singleton within it, or else the range before it where the two
        // overlap, with that range's high value. Only the switch's first is the program's where there's no list.
        bool kept = true;
        // Clang makes a default's statement once its own statement is parsed, so the defaults' walk is by their
        // statements' ends, an inner one's (a default labelling another) before its outer one's.
        if (values - first >= 2)
        {
            valid = check_program_conflict_ends(program, parser, &ends, scratch);
        }
        for (uint32_t i = first + 1; valid && values - first >= 2 && i < values; i += 1)
        {
            const uint32_t moved = order[i];
            const uint32_t moved_end = ends[program->cases[moved].token];
            uint32_t at = i;
            while (at > first && (ends[program->cases[order[at - 1]].token] > moved_end ||
                                  (ends[program->cases[order[at - 1]].token] == moved_end && program->cases[order[at - 1]].token < program->cases[moved].token)))
            {
                order[at] = order[at - 1];
                at -= 1;
            }
            order[at] = moved;
        }
        for (uint32_t i = values - first >= 2 ? values - 1 : first; valid && i > first; i -= 1)
        {
            const CheckProgramCase* item = program->cases + order[i - 1];
            const CheckProgramFailure failure = { .message = CHECK_PROGRAM_MESSAGE_DUPLICATE_DEFAULT, .type = item->type };
            valid = valid && check_program_conflict_ends(program, parser, &ends, scratch);
            if (valid)
            {
                check_program_constraint_record(program, item->token, ends[owner], failure, program->cases[order[i]].token, BUSTER_CHECK_NONE, kept);
            }
            kept = false;
        }
        for (uint32_t i = values + 1; valid && i < ranges; i += 1)
        {
            const CheckProgramCase* item = program->cases + order[i];
            if (check_program_case_compare(program, program->cases + order[i - 1], false, item, false) == 0)
            {
                const CheckProgramFailure failure = { .message = CHECK_PROGRAM_MESSAGE_DUPLICATE_CASE, .type = item->type, .value = item->lower,
                                                      .wide = item->wide_lower };
                valid = valid && check_program_conflict_ends(program, parser, &ends, scratch);
                if (valid)
                {
                    check_program_constraint_record(program, item->token, ends[owner], failure, program->cases[order[i - 1]].token, BUSTER_CHECK_NONE, kept);
                }
                kept = false;
            }
        }
        uint32_t previous = BUSTER_CHECK_NONE;
        for (uint32_t i = ranges; valid && i < end; i += 1)
        {
            const CheckProgramCase* item = program->cases + order[i];
            if (check_program_case_compare(program, item, false, item, true) <= 0)
            {
                // The first singleton not below the range, then the first above it.
                uint32_t begin = values;
                uint32_t limit = ranges;
                while (begin < limit)
                {
                    const uint32_t middle = begin + (limit - begin) / 2;
                    const bool below = check_program_case_compare(program, program->cases + order[middle], false, item, false) < 0;
                    begin = below ? middle + 1 : begin;
                    limit = below ? limit : middle;
                }
                uint32_t above = begin;
                limit = ranges;
                while (above < limit)
                {
                    const uint32_t middle = above + (limit - above) / 2;
                    const bool within = check_program_case_compare(program, program->cases + order[middle], false, item, true) <= 0;
                    above = within ? middle + 1 : above;
                    limit = within ? limit : middle;
                }
                uint32_t overlap = BUSTER_CHECK_NONE;
                unsigned __int128 duplicate = 0;
                const uint64_t* duplicate_wide = NULL;
                if (begin < ranges && check_program_case_compare(program, program->cases + order[begin], false, item, true) < 0)
                {
                    overlap = order[begin];
                    duplicate = program->cases[overlap].lower;
                    duplicate_wide = program->cases[overlap].wide_lower;
                }
                if (above > values && check_program_case_compare(program, program->cases + order[above - 1], false, item, false) >= 0)
                {
                    overlap = order[above - 1];
                    duplicate = program->cases[overlap].lower;
                    duplicate_wide = program->cases[overlap].wide_lower;
                }
                if (previous != BUSTER_CHECK_NONE && check_program_case_compare(program, item, false, program->cases + previous, true) <= 0)
                {
                    overlap = previous;
                    duplicate = program->cases[previous].upper;
                    duplicate_wide = program->cases[previous].wide_upper;
                }
                if (overlap != BUSTER_CHECK_NONE)
                {
                    const CheckProgramFailure failure = { .message = CHECK_PROGRAM_MESSAGE_DUPLICATE_CASE, .type = item->type, .value = duplicate,
                                                          .wide = duplicate_wide };
                    valid = valid && check_program_conflict_ends(program, parser, &ends, scratch);
                    if (valid)
                    {
                        check_program_constraint_record(program, item->token, ends[owner], failure, program->cases[overlap].token, BUSTER_CHECK_NONE, kept);
                    }
                    kept = false;
                }
                previous = order[i];
            }
        }
        first = end;
    }
    arena_pop_to(scratch, mark);
}

// before_token is exclusive; NONE supplies no additional cutoff. Only successful dependency closures before
// every existing reader report are eligible. True means scanning completed, even if result contains a C constraint
// diagnostic. False means unusable inputs or resource failure. No output is published and scratch is restored.
// `output`, when given, takes the list of every constraint error (CheckProgramDiagnostic.constraints, #1447), in the
// order the driver publishes them among errors of one place: a switch's conflicts in clang's order.
static bool check_program_diagnose_all(CheckProgramDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                       const Parser* parser, const TypeTable* types, uint32_t before_token, Arena* output, Arena* scratch)
{
    *result = (CheckProgramDiagnostic){ .token = BUSTER_CHECK_NONE, .position = BUSTER_CHECK_NONE };
    const uint64_t mark = scratch != 0 ? scratch->length : 0;
    bool valid = input != 0 && stream != 0 && parser != 0 && types != 0 && scratch != 0;
    if (valid)
    {
        valid = input->token_count == parser->tokens.count && stream->count == input->graph.count && stream->status == CHECK_STATUS_OK &&
                !input->resource && (input->report_count == 0 || input->diagnostic_graph || input->recovered_valid) &&
                (input->token_count == 0 || (input->token_nodes != 0 && check_sparse_present(input->control_owners) && check_sparse_present(input->case_ranges) &&
                 parser->tokens.kinds != 0 && parser->tree.roles != 0 && parser->tree.parents != 0)) &&
                (input->report_count == 0 || input->reports != 0) && (stream->report_count == 0 || stream->reports != 0) &&
                (stream->count == 0 || (check_stream_has_values(stream) && stream->diagnostics != 0 && input->graph.ops != 0 && input->graph.tokens != 0 &&
                 input->graph.operand0 != 0 && input->graph.operand1 != 0 && input->graph.operand2 != 0));
    }
    if (!valid)
    {
        result->status = (input != 0 && input->resource) || (stream != 0 && stream->status == CHECK_STATUS_RESOURCE) ?
                         CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_INVALID;
    }
    uint32_t cutoff = valid ? input->token_count : 0;
    cutoff = before_token < cutoff ? before_token : cutoff;
    for (uint32_t i = valid ? input->diagnostic_report_count : 0; valid && i < input->report_count; i += 1)
    {
        // A recovered report (#1447) ends nothing: its declaration stayed valid.
        const uint32_t position = input->reports[i].recovered ? cutoff : input->reports[i].completion != 0 ? input->reports[i].completion : input->reports[i].token;
        cutoff = position < cutoff ? position : cutoff;
    }
    // Not the graph's reports: an expression's error ends nothing but what reads it (#1447's semantic recovery, phase 3),
    // which `complete` declines, as clang checks a switch's cases whatever its statements' errors.
    uint8_t* complete = valid && stream->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, stream->count) : 0;
    uint32_t capacity = 0;
    for (uint32_t token = 0; valid && token < cutoff; token += 1)
    {
        capacity += parser->tree.roles[token] == PARSE_ROLE_CLOSE_CASE;
    }
    CheckProgram program = { .input = input, .stream = stream, .types = types, .token = BUSTER_CHECK_NONE, .diagnostic_position = BUSTER_CHECK_NONE };
    program.cases = valid && capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramCase, capacity) : 0;
    // Each switch's condition, and each case's value or conflict, gives at most one.
    uint32_t constraints = capacity;
    for (uint32_t token = 0; valid && output != 0 && token < cutoff; token += 1)
    {
        constraints += parser->tokens.kinds[token] == PARSE_KIND_SWITCH;
    }
    program.constraints = valid && output != 0 && constraints != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckProgramConstraint, constraints) : 0;
    program.constraint_capacity = program.constraints != 0 ? constraints : 0;
    if (valid && ((stream->count != 0 && complete == 0) || (capacity != 0 && program.cases == 0) ||
                  (output != 0 && constraints != 0 && program.constraints == 0)))
    {
        valid = false;
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    const uint32_t* ends = valid && capacity != 0 ? check_program_statement_ends(&program, parser, scratch) : 0;
    if (valid && capacity != 0 && ends == 0)
    {
        valid = false;
        result->status = program.status;
    }
    for (uint32_t node = 0; valid && node < stream->count; node += 1)
    {
        const CheckGraph* graph = &input->graph;
        const uint32_t local = check_input_diagnostic_limit(input, graph->tokens[node]);
        const uint32_t limit = local < cutoff ? local : cutoff;
        bool checked = graph->tokens[node] < limit && (graph->source_positions == 0 || graph->source_positions[node] <= limit) &&
                       stream->diagnostics[node] == CHECK_DIAG_NONE && type_is_valid(types, check_stream_type(stream, node));
        const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
        for (uint32_t i = 0; checked && i < BUSTER_ARRAY_LENGTH(operands); i += 1)
        {
            checked = operands[i] == BUSTER_CHECK_NONE || (operands[i] < node && complete[operands[i]]);
        }
        if (checked && check_graph_has_arguments(graph->ops[node]))
        {
            checked = graph->argument_counts != 0 && graph->argument_offsets != 0;
            const uint32_t first = checked ? graph->argument_offsets[node] : 0;
            const uint32_t count = checked ? graph->argument_counts[node] : 0;
            checked = checked && first <= graph->argument_count && count <= graph->argument_count - first && (count == 0 || graph->argument_nodes != 0);
            for (uint32_t i = 0; checked && i < count; i += 1)
            {
                const uint32_t operand = graph->argument_nodes[first + i];
                checked = operand < node && complete[operand];
            }
        }
        complete[node] = checked;
    }
    for (uint32_t token = 0; valid && token < cutoff; token += 1)
    {
        const uint32_t end = parser->tree.parents[token];
        const uint32_t local = check_input_diagnostic_limit(input, token);
        const bool selection = parser->tokens.kinds[token] == PARSE_KIND_SWITCH && end < cutoff && end < local && parser->tree.roles[end] == PARSE_ROLE_CLOSE_CONDITION;
        const bool label = parser->tree.roles[token] == PARSE_ROLE_OPEN_CASE && end < cutoff && end < local && parser->tree.roles[end] == PARSE_ROLE_CLOSE_CASE;
        if (selection || label)
        {
            const uint32_t owner = selection ? end : check_sparse_get(input->control_owners, end);
            const uint32_t condition = owner < cutoff ? input->token_nodes[owner] : BUSTER_CHECK_NONE;
            if (condition < stream->count && complete[condition])
            {
                if (!check_program_switch_type(types, check_stream_type(stream, condition)))
                {
                    // Once, by its switch, where there's a list.
                    if (selection || program.constraints == 0)
                    {
                        const CheckProgramFailure failure = { .message = CHECK_PROGRAM_MESSAGE_CONDITION, .type = check_stream_type(stream, condition) };
                        check_program_constraint_record(&program, owner, owner, failure, BUSTER_CHECK_NONE, condition, true);
                    }
                }
                else if (label && ends != 0 && ends[owner] < cutoff)
                {
                    // A case whose value holds no missing name is one of its switch's, wherever the switch's other
                    // statements hold one, as clang checks a switch's cases at its end (#1077); the switch's end, where
                    // its conflicts are published, must come before the unit's cutoff.
                    const uint32_t node = input->token_nodes[end];
                    const uint32_t high = check_sparse_get(input->case_ranges, end) != BUSTER_CHECK_NONE ? check_sparse_get(input->case_ranges, end) : node;
                    const bool fallback = parser->tokens.kinds[token] == PARSE_KIND_DEFAULT;
                    const bool checked = fallback ? node == BUSTER_CHECK_NONE : node < stream->count && high < stream->count && complete[node] && complete[high];
                    if (checked)
                    {
                        CheckProgramCase item = { .type = check_stream_type(stream, condition), .token = end, .owner = owner,
                                                  .fallback = fallback, .range = check_sparse_get(input->case_ranges, end) != BUSTER_CHECK_NONE };
                        if (!fallback && (!check_program_case_value(&program, node, item.type, &item.lower, &item.wide_lower) ||
                            !check_program_case_value(&program, high, item.type, &item.upper, &item.wide_upper)))
                        {
                            // The reader's error, which a list doesn't repeat: clang drops the case.
                            if (program.constraints == 0)
                            {
                                check_program_constraint(&program, end);
                            }
                        }
                        else
                        {
                            program.cases[program.case_count++] = item;
                        }
                    }
                }
            }
        }
    }
    if (valid)
    {
        check_program_case_conflicts(&program, parser, ends, scratch);
        result->status = program.status;
        result->token = check_program_diagnostic_location(parser, program.token);
        result->position = program.diagnostic_position;
        result->failure = program.failure;
        valid = program.status != CHECK_PROGRAM_RESOURCE;
        for (uint32_t i = 0; valid && i < program.constraint_count; i += 1)
        {
            CheckProgramConstraint* constraint = program.constraints + i;
            constraint->token = check_program_diagnostic_location(parser, constraint->token);
            constraint->previous = constraint->previous != BUSTER_CHECK_NONE ? check_program_diagnostic_location(parser, constraint->previous) : BUSTER_CHECK_NONE;
        }
        result->constraints = valid ? program.constraints : 0;
        result->constraint_count = valid ? program.constraint_count : 0;
    }
    if (scratch != 0)
    {
        arena_pop_to(scratch, mark);
    }
    return valid;
}

static bool check_program_diagnose(CheckProgramDiagnostic* result, const CheckInput* input, const CheckStream* stream,
                                   const Parser* parser, const TypeTable* types, uint32_t before_token, Arena* scratch)
{
    return check_program_diagnose_all(result, input, stream, parser, types, before_token, 0, scratch);
}

#endif
