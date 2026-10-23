// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Validate every source initializer after expression checking, including automatic objects. Compact retained
// plans own their converted subobject writes; a declaration-token column gives consumers direct access without
// reparsing or name lookup. This phase is required for source acceptance, independently of data/code emission.
// Include after c_check_input.c. Output and scratch are disjoint and the output remains live with CheckInput.
#ifndef BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_INPUT_INCLUDED

typedef struct CheckInitializers CheckInitializers;
struct CheckInitializers
{
    CheckInitializer* plans;
    uint32_t* declaration_tokens;
    // Index plus one into plans; zero means this declaration has no explicit initializer.
    uint32_t* declaration_plans;
    uint32_t count;
    uint32_t token_count;
    CheckInitStatus status;
    uint32_t token;
};

static bool check_initializers_run(CheckInitializers* result, const CheckInput* input, const CheckStream* stream,
                                   const Parser* parser, TypeTable* types, Arena* output, Arena* scratch)
{
    *result = (CheckInitializers){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t tokens = input->token_count;
    bool valid = output != scratch && tokens == parser->tokens.count && tokens < UINT32_MAX &&
                 stream->count == input->graph.count && (tokens == 0 ||
                 (input->initializer_begins != 0 && input->initializer_ends != 0 && input->declared_types != 0 &&
                  input->static_objects != 0 && input->token_nodes != 0));
    result->status = valid ? CHECK_INIT_OK : CHECK_INIT_INVALID;
    if (valid && (input->resource || input->report_count != 0 || stream->status != CHECK_STATUS_OK || stream->report_count != 0))
    {
        valid = false;
        result->status = input->resource || stream->status == CHECK_STATUS_RESOURCE ? CHECK_INIT_RESOURCE : CHECK_INIT_CHECK_FAILED;
    }
    uint32_t count = 0;
    for (uint32_t token = 0; valid && token < tokens; token += 1)
    {
        if (input->initializer_begins[token] != BUSTER_CHECK_NONE)
        {
            const uint32_t begin = input->initializer_begins[token];
            const uint32_t end = input->initializer_ends[token];
            valid = begin < end && end <= tokens && type_is_valid(types, input->declared_types[token]);
            if (valid)
            {
                count += 1;
            }
            else
            {
                result->status = CHECK_INIT_INVALID;
                result->token = token;
            }
        }
    }
    if (valid)
    {
        result->plans = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitializer, count) : 0;
        result->declaration_tokens = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, count) : 0;
        result->declaration_plans = BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)tokens + 1);
        valid = result->declaration_plans != 0 && (count == 0 || (result->plans != 0 && result->declaration_tokens != 0));
        result->status = valid ? CHECK_INIT_OK : CHECK_INIT_RESOURCE;
        if (valid)
        {
            memset(result->declaration_plans, 0, ((uint64_t)tokens + 1) * sizeof(uint32_t));
        }
    }
    uint32_t at = 0;
    for (uint32_t token = 0; valid && token < tokens; token += 1)
    {
        if (input->initializer_begins[token] != BUSTER_CHECK_NONE)
        {
            CheckInitializer* plan = result->plans + at;
            valid = check_initializer_run(types, parser, input->token_nodes, stream, input->strings, input->string_count,
                                          input->declared_types[token], input->initializer_begins[token], input->initializer_ends[token],
                                          input->static_objects[token] != 0, output, scratch, plan);
            if (valid)
            {
                result->declaration_tokens[at] = token;
                result->declaration_plans[token] = at + 1;
                at += 1;
            }
            else
            {
                result->status = plan->status;
                result->token = plan->failed_token;
            }
        }
    }
    if (valid)
    {
        result->count = count;
        result->token_count = tokens;
    }
    else
    {
        const CheckInitStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckInitializers){ .status = status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

#endif
