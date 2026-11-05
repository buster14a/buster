// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Keep ordinary declaration conflicts' reasons and prior source locations. This is diagnostic metadata only:
// the binding checks still reject exactly the same declarations and stop at the same error.
typedef enum CheckRedeclarationKind
{
    CHECK_REDECLARATION_GENERIC,
    CHECK_REDECLARATION_OBJECT_TYPE,
    CHECK_REDECLARATION_FUNCTION_TYPE,
    CHECK_REDECLARATION_DEFINITION,
    CHECK_REDECLARATION_PARAMETER,
    CHECK_REDECLARATION_STATIC,
} CheckRedeclarationKind;

static void check_input_redeclaration_report(CheckInputReader* reader, uint32_t token, uint32_t previous,
                                             TypeAnswer compatible, bool repeatable, bool parameter_conflict, CheckInputMessage message)
{
    CheckInput* input = reader->output;
    CheckRedeclarationKind detail = CHECK_REDECLARATION_GENERIC;
    uint32_t noted = previous;
    uint8_t note_kind = CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
    const uint32_t count = input->token_count;
    if (token < count && previous < count)
    {
        const uint8_t kind = reader->declaration_kinds[token];
        const bool same_kind = kind == reader->declaration_kinds[previous];
        const uint32_t linkage = check_input_linkage(reader->declaration_bits, token);
        const uint32_t earlier_linkage = check_input_linkage(reader->declaration_bits, previous);
        const bool same_tls = check_input_bit(reader->declaration_bits, token, BUSTER_CHECK_BITS_THREAD_LOCAL) ==
                              check_input_bit(reader->declaration_bits, previous, BUSTER_CHECK_BITS_THREAD_LOCAL);
        const uint32_t definition = reader->definition_origins != NULL ? reader->definition_origins[previous] : 0;
        const uint32_t current_definition = reader->definition_origins != NULL ? reader->definition_origins[token] : 0;
        const bool object = kind == C_BINDING_OBJECT;
        const bool function = kind == C_BINDING_FUNCTION;
        if (same_kind && same_tls && (object || function))
        {
            if (linkage == 1 && earlier_linkage == 2)
            {
                detail = CHECK_REDECLARATION_STATIC;
            }
            else if (linkage == earlier_linkage)
            {
                if (compatible == TYPE_ANSWER_NO || parameter_conflict)
                {
                    detail = function ? CHECK_REDECLARATION_FUNCTION_TYPE : CHECK_REDECLARATION_OBJECT_TYPE;
                }
                else if (compatible == TYPE_ANSWER_YES && (!repeatable || (current_definition != 0 && definition != 0)))
                {
                    detail = CHECK_REDECLARATION_DEFINITION;
                    noted = definition != 0 ? definition - 1 : previous;
                }
            }
            const bool defined = definition != 0 || (object && check_input_bit(reader->declaration_bits, previous, BUSTER_CHECK_BITS_TENTATIVE)) ||
                                 (object && earlier_linkage == 0);
            note_kind = defined ? CHECK_INPUT_NOTE_PREVIOUS_DEFINITION : CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
        }
    }
    const uint32_t before = input->report_count;
    check_input_report_subject(reader, token, message, previous);
    if (input->report_count > before && !input->resource && detail != CHECK_REDECLARATION_GENERIC)
    {
        CheckInputReport* report = input->reports + before;
        report->detail = detail;
        report->note_token = noted;
        report->note_kind = note_kind;
        report->type = reader->declared_types[token];
        report->type_spelling = check_input_typedef_spelling(input, reader->parser, token);
        report->value = reader->declared_types[previous];
    }
}

// A duplicate parameter is already an error. Walk its current prototype's sparse list once to retain the first
// parameter's spelling; no per-token column or successful-declaration scan is added.
static void check_input_parameter_redefinition(CheckInputReader* reader, uint32_t token, uint32_t parameters)
{
    const uint32_t name = reader->parser->tokens.names[token];
    uint32_t previous = BUSTER_CHECK_NONE;
    for (uint32_t p = parameters; p != BUSTER_CHECK_NONE && previous == BUSTER_CHECK_NONE; p = check_sparse_get(reader->parameter_next, p))
    {
        previous = reader->parser->tokens.names[p] == name ? p : BUSTER_CHECK_NONE;
    }
    const uint32_t before = reader->output->report_count;
    check_input_report_subject(reader, token, CHECK_INPUT_REDECLARATION, previous);
    if (reader->output->report_count > before && !reader->output->resource && previous != BUSTER_CHECK_NONE)
    {
        CheckInputReport* report = reader->output->reports + before;
        report->detail = CHECK_REDECLARATION_PARAMETER;
        report->note_token = previous;
        report->note_kind = CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
    }
}
