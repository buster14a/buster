// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Keep ordinary declaration conflicts' reasons and prior source locations. This is diagnostic metadata only:
// the binding checks still reject exactly the same declarations and retain their recovery behavior.
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
    CHECK_REDECLARATION_THREAD_LOCAL,
    CHECK_REDECLARATION_NON_THREAD_LOCAL,
    // A typedef's redefinition with another type (#1447, phase 2): "typedef redefinition with different types ('N' vs 'O')".
    CHECK_REDECLARATION_TYPEDEF,
    // A name redeclared as another kind of entity (#1447, phase 2): "redefinition of 'X' as different kind of symbol".
    CHECK_REDECLARATION_KIND,
    // An enum redeclared with another fixed underlying type (the report's type, the earlier's its value), without the
    // fixed type it had, or with one it hadn't (#1447): clang's CheckEnumRedeclaration.
    CHECK_REDECLARATION_ENUM_BASE,
    CHECK_REDECLARATION_ENUM_FIXED,
    CHECK_REDECLARATION_ENUM_NONFIXED,
    // An __auto_type redeclaration of an entity with linkage whose type clang's mergeTypes refuses (#1844): the report's
    // type the placeholder's through its declarator (check_semantic_auto_type_spelling), the earlier one's its value;
    // "redefinition", or with extern "redeclaration", "with a different type".
    CHECK_REDECLARATION_AUTO_TYPE,
    CHECK_REDECLARATION_AUTO_TYPE_EXTERN,
    // A block's object whose earlier declaration in it has no linkage and itself extern, or the other way around (#2441):
    // clang's MergeVarDecl, "extern declaration of 'x' follows non-extern declaration" and its reverse.
    CHECK_REDECLARATION_EXTERN,
    CHECK_REDECLARATION_NON_EXTERN,
} CheckRedeclarationKind;

// Whether the declaration of `token`, a declarator's name, says extern or __private_extern__: a storage-class specifier
// back from the name to its declaration's boundary (check_input_declaration_boundary's roles), over its declarator's
// chunks (arrays, groups, pointers), the declarators before it and its specifiers whatever they spell (`_Alignas(8)`,
// `_BitInt(8)`). A tag's body and anything parenthesized (a statement expression in a `typeof` or in an earlier
// declarator's initializer) are skipped: their terminators and storage classes are none of this declaration's. Clang
// words an object's type conflict by whether the new declaration is a definition before its initializer is read
// (diagnoseVarDeclTypeMismatch's isThisDeclarationADefinition), so an extern one is a "redeclaration" even with an
// initializer, and any other one (a tentative definition too) a "redefinition" (#1447, review on #1453). A storage class
// is its declaration's specifier alone: a tag's member or a prototype's parameter can't say extern.
static bool check_input_redeclaration_storage(const Parser* parser, uint32_t token, uint32_t storage, uint32_t other);

static bool check_input_redeclaration_extern(const Parser* parser, uint32_t token)
{
    return check_input_redeclaration_storage(parser, token, C_NAME_EXTERN, C_NAME_PRIVATE_EXTERN);
}

// Whether the declaration of `token` says the storage class `storage` or `other` (check_input_redeclaration_extern's scan).
static bool check_input_redeclaration_storage(const Parser* parser, uint32_t token, uint32_t storage, uint32_t other)
{
    const uint8_t* roles = parser->tree.roles;
    const uint8_t* kinds = parser->tokens.kinds;
    bool result = false;
    bool scanning = token < parser->tokens.count;
    uint32_t depth = 0;
    uint32_t parentheses = 0;
    for (uint32_t at = token; scanning && at > 0;)
    {
        at -= 1;
        const uint8_t role = roles[at];
        const uint32_t name = parser->tokens.names[at];
        depth += role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM;
        depth -= (role == PARSE_ROLE_OPEN_STRUCT || role == PARSE_ROLE_OPEN_ENUM) && depth != 0;
        parentheses += kinds[at] == PARSE_KIND_R_PAREN;
        parentheses -= kinds[at] == PARSE_KIND_L_PAREN && parentheses != 0;
        const bool outside = depth == 0 && parentheses == 0;
        result = outside && role == PARSE_ROLE_SPECIFIER && (name == storage || name == other);
        const bool boundary = role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_OPEN_BLOCK || role == PARSE_ROLE_CLOSE_BLOCK ||
                              role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_CLOSE_BODY;
        scanning = !result && (!outside || !boundary);
    }
    return result;
}

// `thread_local` says whether a thread-local storage mismatch is still this declaration's to report: one reported against
// the visible declaration already isn't its linked declaration's too (#1786).
static void check_input_redeclaration_report(CheckInputReader* reader, uint32_t token, uint32_t previous, TypeAnswer compatible,
                                             bool repeatable, bool parameter_conflict, bool thread_local, CheckInputMessage message)
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
        const bool current_tls = check_input_bit(reader->declaration_bits, token, BUSTER_CHECK_BITS_THREAD_LOCAL);
        const bool same_tls = !thread_local || current_tls == check_input_bit(reader->declaration_bits, previous, BUSTER_CHECK_BITS_THREAD_LOCAL);
        const uint32_t definition = reader->definition_origins != NULL ? reader->definition_origins[previous] : 0;
        const uint32_t current_definition = reader->definition_origins != NULL ? reader->definition_origins[token] : 0;
        const bool object = kind == C_BINDING_OBJECT;
        const bool function = kind == C_BINDING_FUNCTION;
        // Another kind of entity's, and a typedef's of another type (#1447, phase 2): clang's notes say "definition" for
        // both.
        if (!same_kind || (kind == C_BINDING_TYPEDEF && compatible == TYPE_ANSWER_NO))
        {
            detail = same_kind ? CHECK_REDECLARATION_TYPEDEF : CHECK_REDECLARATION_KIND;
            note_kind = CHECK_INPUT_NOTE_PREVIOUS_DEFINITION;
        }
        else if (same_kind && (object || function))
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
                // A duplicate local declaration is a redefinition before clang checks TLS. A repeatable declaration's
                // TLS mismatch follows the type and linkage checks, but precedes the check for two definitions.
                else if (object && repeatable && !same_tls)
                {
                    detail = current_tls ? CHECK_REDECLARATION_THREAD_LOCAL : CHECK_REDECLARATION_NON_THREAD_LOCAL;
                }
                else if (compatible == TYPE_ANSWER_YES && (!repeatable || (current_definition != 0 && definition != 0)))
                {
                    detail = CHECK_REDECLARATION_DEFINITION;
                    noted = definition != 0 ? definition - 1 : previous;
                }
            }
            // A block's object of no linkage beside one with it, of a type that merges (#2441, clang's MergeVarDecl, after
            // its static checks): a static one follows a non-static one, an extern one a block's static one; else the
            // extern and non-extern errors.
            if (object && detail == CHECK_REDECLARATION_GENERIC && reader->scope_count != 0 && compatible != TYPE_ANSWER_NO && (linkage == 0) != (earlier_linkage == 0))
            {
                const bool earlier_static = earlier_linkage == 0 && check_input_redeclaration_storage(reader->parser, previous, C_NAME_STATIC, C_NAME_STATIC);
                const bool current_static = linkage == 0 && check_input_redeclaration_storage(reader->parser, token, C_NAME_STATIC, C_NAME_STATIC);
                detail = current_static ? CHECK_REDECLARATION_STATIC : earlier_static ? CHECK_REDECLARATION_NON_STATIC :
                         linkage != 0 ? CHECK_REDECLARATION_EXTERN : CHECK_REDECLARATION_NON_EXTERN;
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
            // path alone.) TLS conflicts instead note the visible declaration, as function conflicts do.
            const uint32_t name = reader->parser->tokens.names[token];
            const uint32_t entity = reader->objects != NULL ? reader->objects[previous] : 0;
            uint32_t latest = BUSTER_CHECK_NONE;
            const bool conflict = object && detail == CHECK_REDECLARATION_OBJECT_TYPE && linkage != 0 && earlier_linkage != 0 && entity != 0;
            // Bodies read apart (c_check_deferred.c, #1447) haven't read one another's declarations: a deferred top level
            // hasn't read the bodies it skipped, nor a lane the other bodies. A scan that reaches another body's closing
            // brace before its declaration sends the unit to source order.
            const uint8_t* roles = reader->parser->tree.roles;
            const bool apart = (reader->deferred != 0 || reader->lane) && roles != NULL;
            bool crossed = false;
            for (uint32_t t = token; conflict && latest == BUSTER_CHECK_NONE && !crossed && t > previous + 1;)
            {
                t -= 1;
                crossed = apart && roles[t] == PARSE_ROLE_CLOSE_BODY;
                const bool same = !crossed && reader->parser->tokens.names[t] == name && reader->declaration_kinds[t] == kind &&
                                  check_input_linkage(reader->declaration_bits, t) != 0 && reader->objects[t] == entity && reader->declared_types[t] != 0;
                latest = same ? t : BUSTER_CHECK_NONE;
            }
            check_input_lane_refuse(reader, crossed && reader->lane, "a body's redeclaration notes another body's declaration");
            if (crossed && reader->deferred != 0)
            {
                check_deferred_note_scan(reader);
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
