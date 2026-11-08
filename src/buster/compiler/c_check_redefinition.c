// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Keep ordinary declaration conflicts' reasons and prior source locations. This is diagnostic metadata only:
// the binding checks still reject exactly the same declarations and stop at the same error.
typedef enum CheckRedeclarationKind
{
    CHECK_REDECLARATION_GENERIC,
    CHECK_REDECLARATION_OBJECT_TYPE,
    // An object's type conflict whose new declaration says extern (or __private_extern__): clang's "redeclaration".
    CHECK_REDECLARATION_OBJECT_TYPE_EXTERN,
    CHECK_REDECLARATION_FUNCTION_TYPE,
    CHECK_REDECLARATION_DEFINITION,
    CHECK_REDECLARATION_PARAMETER,
    CHECK_REDECLARATION_STATIC,
    CHECK_REDECLARATION_NON_STATIC,
} CheckRedeclarationKind;

// Whether the declaration of `token`, a declarator's name, says extern or __private_extern__: a storage-class specifier
// back from the name to its declaration's boundary (check_input_declaration_boundary's roles), over its declarator's chunks
// (arrays, groups, pointers), the declarators before it and its specifiers whatever they spell (`_Alignas(8)`,
// `_BitInt(8)`), and past a tag's body, whose members' terminators are no boundary of this declaration. Clang words an
// object's type conflict by whether the new declaration is a definition before its initializer is read
// (diagnoseVarDeclTypeMismatch's isThisDeclarationADefinition), so an extern one is a "redeclaration" even with an
// initializer, and any other one (a tentative definition too) a "redefinition" (#1447, review on #1453). A storage class
// is its declaration's specifier alone: a tag's member or a prototype's parameter can't say extern.
static bool check_input_redeclaration_extern(const Parser* parser, uint32_t token)
{
    const uint8_t* roles = parser->tree.roles;
    bool result = false;
    bool scanning = token < parser->tokens.count;
    uint32_t depth = 0;
    for (uint32_t at = token; scanning && at > 0;)
    {
        at -= 1;
        const uint8_t role = roles[at];
        const uint32_t name = parser->tokens.names[at];
        depth += role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM;
        depth -= (role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM) && depth != 0;
        result = depth == 0 && role == PARSE_ROLE_SPECIFIER && (name == C_NAME_EXTERN || name == C_NAME_PRIVATE_EXTERN);
        const bool boundary = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_CLOSE_BLOCK ||
                              role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_CLOSE_BODY;
        scanning = !result && (depth != 0 || !boundary);
    }
    return result;
}

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
            if (object && compatible == TYPE_ANSWER_NO)
            {
                detail = CHECK_REDECLARATION_OBJECT_TYPE;
            }
            else if (function && compatible == TYPE_ANSWER_NO && linkage == 2 && earlier_linkage == 1)
            {
                detail = CHECK_REDECLARATION_FUNCTION_TYPE;
            }
            else if (linkage == 1 && earlier_linkage == 2)
            {
                detail = CHECK_REDECLARATION_STATIC;
            }
            else if (linkage == 2 && earlier_linkage == 1)
            {
                detail = CHECK_REDECLARATION_NON_STATIC;
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
            const bool defined = (definition != 0 && (noted != previous || definition == previous + 1)) ||
                                 (object && check_input_bit(reader->declaration_bits, previous, BUSTER_CHECK_BITS_TENTATIVE)) ||
                                 (object && earlier_linkage == 0);
            note_kind = defined ? CHECK_INPUT_NOTE_PREVIOUS_DEFINITION : CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
            // An object's type conflict with linkage notes its latest earlier declaration, as clang's MergeVarDecl does (it
            // merges with the most recent declaration first), though another block's, out of scope here (`int y; void
            // f(void) { extern int y; }` then g's `extern double y;`, review on #1453): the last declaration before `token`
            // of the same name, kind and object, which linkage gave it. A function's (MergeFunctionDecl) notes the
            // declaration its lookup found, `previous`, never another block's. (A scan back over the tokens, on the error
            // path alone: a unit's checking ends at its error.)
            const uint32_t name = reader->parser->tokens.names[token];
            const uint32_t entity = reader->objects != NULL ? reader->objects[previous] : 0;
            uint32_t latest = BUSTER_CHECK_NONE;
            const bool conflict = object && detail == CHECK_REDECLARATION_OBJECT_TYPE && linkage != 0 && earlier_linkage != 0 && entity != 0;
            for (uint32_t t = token; conflict && latest == BUSTER_CHECK_NONE && t > previous + 1;)
            {
                t -= 1;
                const bool same = reader->parser->tokens.names[t] == name && reader->declaration_kinds[t] == kind &&
                                  check_input_linkage(reader->declaration_bits, t) != 0 && reader->objects[t] == entity && reader->declared_types[t] != 0;
                latest = same ? t : BUSTER_CHECK_NONE;
            }
            if (latest < token && latest != previous)
            {
                noted = latest;
                note_kind = check_input_bit(reader->declaration_bits, latest, BUSTER_CHECK_BITS_DEFINED) ||
                            (object && check_input_bit(reader->declaration_bits, latest, BUSTER_CHECK_BITS_TENTATIVE)) ?
                            CHECK_INPUT_NOTE_PREVIOUS_DEFINITION : CHECK_INPUT_NOTE_PREVIOUS_DECLARATION;
            }
        }
    }
    // (A function's other explicit calling convention reports CHECK_INPUT_CONVENTION_CHANGE instead, with its own detail
    // and note: check_input_report_redeclaration.)
    const uint32_t before = input->report_count;
    check_input_report_redeclaration(reader, token, message, previous);
    if (input->report_count > before && !input->resource && detail != CHECK_REDECLARATION_GENERIC && input->reports[before].message == message)
    {
        CheckInputReport* report = input->reports + before;
        report->detail = detail == CHECK_REDECLARATION_OBJECT_TYPE && check_input_redeclaration_extern(reader->parser, token) ?
                         CHECK_REDECLARATION_OBJECT_TYPE_EXTERN : detail;
        report->note_token = noted;
        report->note_kind = note_kind;
        report->type = reader->declared_types[token];
        report->type_spelling = check_input_typedef_spelling(input, reader->parser, token);
        report->value = reader->declared_types[noted < count ? noted : previous];
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
