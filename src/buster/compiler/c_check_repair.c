// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Diagnostic-only recovery of completed statements in the first malformed declaration. The original parser has
// already rejected the unit and erased that declaration's tree. Retain tokens through its last complete statement
// or proven compound opening (an empty prefix can still retain an earlier original pragma),
// close only original compound statements/function bodies, and reparse with the existing bounded scalar parser.
// An original `do {` additionally needs exactly `while (0);`; a private preprocessor-output view appends that integer
// token for ordinary literal decoding. A recognized struct/union prefix can instead retain complete original
// members and receive `};` tails. Only independently diagnosed incomplete members are publishable inside those
// records; synthetic type completion/layout never supplies an expression diagnostic. Initializers remain excluded.
//
// This never accepts or emits repaired source. A caller must preserve the original rejection and diagnostics,
// elaborate READY views only with check_input_fragment(..., original_end), and select reports only through the
// whitelist below. That entry point defers TU-end obligations and unresolved labels in synthetically closed bodies;
// earlier complete functions and operation-site type history retain their normal checks. Synthetic tokens cannot
// supply declarations, labels, types or operands of a reported original expression.
//
// Include after c_check_input.c, c_checker.c and c_check_prefix.c. Output owns the padded view, not Parser arenas;
// do not call parse_release on it. Sources and retained pragmas borrow the original preprocessor/parser lifetime.
// Use repair.preprocessor only for this view's source elaboration; never run or release that shallow PP view.
// Output and scratch must be distinct. All scratch and all failed output allocations are restored.

#ifndef BUSTER_CHECK_REPAIR_IMPLEMENTATION
#define BUSTER_CHECK_REPAIR_IMPLEMENTATION 1

typedef enum CheckRepairStatus
{
    CHECK_REPAIR_READY,
    CHECK_REPAIR_UNAVAILABLE,
    CHECK_REPAIR_INVALID,
    CHECK_REPAIR_RESOURCE,
} CheckRepairStatus;

typedef struct CheckRepair CheckRepair;
struct CheckRepair
{
    Parser parser;
    Preprocessor* preprocessor;
    CheckRepairStatus status;
    uint32_t diagnostic;
    // Original tokens are [0, original_end); every token after that is synthetic and ineligible for a report.
    uint32_t original_end;
    uint32_t record_begin;
    uint8_t* warning_complete;
};

static Preprocessor* check_repair_preprocessor(Preprocessor* original, Arena* output)
{
    // The view shares the original's expansion history, which its diagnostics read: recorded first, if the unit put it
    // off (PPOptions.defer_history), so that the view never records one of its own.
    pp_history_ensure(original);
    Preprocessor* view = BUSTER_ARENA_PUSH_ARRAY(output, Preprocessor, 1);
    if (view != 0)
    {
        *view = *original;
        const uint32_t count = original->output.count;
        view->output = (PPTokens)
        {
            .kinds = check_prefix_column(output, count + 1, sizeof(uint8_t)),
            .flags = check_prefix_column(output, count + 1, sizeof(uint8_t)),
            .values = check_prefix_column(output, count + 1, sizeof(uint32_t)),
            .locations = check_prefix_column(output, count + 1, sizeof(uint32_t)),
            .count = count + 1,
            .capacity = count + 1,
        };
        if (view->output.kinds == 0 || view->output.flags == 0 || view->output.values == 0 || view->output.locations == 0)
        {
            view = 0;
        }
        else
        {
            memcpy(view->output.kinds, original->output.kinds, count);
            memcpy(view->output.flags, original->output.flags, count);
            memcpy(view->output.values, original->output.values, (uint64_t)count * sizeof(uint32_t));
            memcpy(view->output.locations, original->output.locations, (uint64_t)count * sizeof(uint32_t));
            view->output.kinds[count] = BUSTER_PP_KIND_INTEGER;
        }
    }
    return view;
}

static CheckRepairStatus check_repair_make(CheckRepair* repair, const Parser* original, Preprocessor* pp, Arena* output, Arena* scratch)
{
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = original->tokens.count;
    *repair = (CheckRepair){ .status = CHECK_REPAIR_UNAVAILABLE, .diagnostic = BUSTER_PARSE_NONE };
    uint32_t limit = count;
    uint32_t cut = 0;
    uint32_t depth = 0;
    uint32_t other = 0;
    uint32_t unsafe = 0;
    uint32_t records = 0;
    uint8_t parenthesis_keyword = PARSE_KIND_END;
    bool preceding_statement_block = false;
    uint32_t* stack = 0;
    uint32_t* closers = 0;
    uint8_t* statement_blocks = 0;
    if (output == scratch)
    {
        repair->status = CHECK_REPAIR_INVALID;
    }
    else if (original->errors != 0 && original->fatal == PARSE_MESSAGE_NONE)
    {
        bool valid = count <= BUSTER_PARSE_MAX_TOKENS && pp->output.count <= BUSTER_PARSE_MAX_TOKENS / 2 && original->diagnostics != 0 &&
                     original->diagnostic_count <= original->diagnostic_capacity && original->tokens.kinds != 0 &&
                     original->tokens.names != 0 && original->tokens.sources != 0 && original->tokens.flags != 0 &&
                     original->meanings.capacity != 0 && pp->output.kinds != 0 && pp->output.flags != 0 && pp->output.values != 0 && pp->output.locations != 0 &&
                     (original->tokens.pragma_count == 0 || (original->tokens.pragma_sources != 0 && original->tokens.pragma_positions != 0));
        for (uint32_t i = 0; valid && i < original->diagnostic_count; i += 1)
        {
            const ParseDiagnostic* diagnostic = original->diagnostics + i;
            valid = diagnostic->token <= count;
            if (valid && diagnostic->error && (repair->diagnostic == BUSTER_PARSE_NONE || diagnostic->token < limit))
            {
                limit = diagnostic->token;
                repair->diagnostic = i;
            }
        }
        for (uint32_t i = 0; valid && i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            valid = original->predeclared[i] < original->meanings.capacity;
        }
        for (uint32_t i = 0; valid && i < original->tokens.pragma_count; i += 1)
        {
            valid = original->tokens.pragma_sources[i] < pp->output.count && original->tokens.pragma_positions[i] <= count &&
                    (i == 0 || original->tokens.pragma_positions[i - 1] <= original->tokens.pragma_positions[i]);
        }
        repair->status = valid ? CHECK_REPAIR_UNAVAILABLE : CHECK_REPAIR_INVALID;
        if (valid && repair->diagnostic != BUSTER_PARSE_NONE)
        {
            stack = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
            closers = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
            statement_blocks = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1);
            repair->status = stack != 0 && closers != 0 && statement_blocks != 0 ? CHECK_REPAIR_READY : CHECK_REPAIR_RESOURCE;
        }
    }
    for (uint32_t token = 0; repair->status == CHECK_REPAIR_READY && token < limit; token += 1)
    {
        const uint8_t kind = original->tokens.kinds[token];
        if (kind == PARSE_KIND_END || kind >= PARSE_KIND_COUNT || original->tokens.names[token] >= original->meanings.capacity ||
            original->tokens.sources[token] >= pp->output.count)
        {
            repair->status = CHECK_REPAIR_INVALID;
        }
        else if (kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE)
        {
            // A member's semicolon must not replace the last complete statement before an unfinished record.
            // These are only candidate statement scopes: the single reparse below proves their exact roles.
            // Unknown braces (types, initializers, compound literals) freeze the candidate until they close.
            const uint8_t previous = token != 0 ? original->tokens.kinds[token - 1] : PARSE_KIND_END;
            const bool control = previous == PARSE_KIND_R_PAREN && (parenthesis_keyword == PARSE_KIND_IF ||
                                 parenthesis_keyword == PARSE_KIND_FOR || parenthesis_keyword == PARSE_KIND_WHILE || parenthesis_keyword == PARSE_KIND_SWITCH);
            const bool outer = depth == 0 && (previous == PARSE_KIND_R_PAREN || previous == PARSE_KIND_SEMI);
            const bool inner = depth != 0 && other == 0 && unsafe == 0 && (control || previous == PARSE_KIND_DO || previous == PARSE_KIND_ELSE ||
                               previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_SEMI || previous == PARSE_KIND_COLON ||
                               (previous == PARSE_KIND_R_BRACE && preceding_statement_block));
            const bool statement = kind == PARSE_KIND_L_BRACE && (outer || inner);
            const bool record = kind == PARSE_KIND_L_BRACE && (previous == PARSE_KIND_STRUCT ||
                                (previous == PARSE_KIND_IDENTIFIER && token >= 2 && original->tokens.kinds[token - 2] == PARSE_KIND_STRUCT));
            statement_blocks[depth] = record ? 2 : statement ? 1 : 0;
            stack[depth++] = token;
            other += kind != PARSE_KIND_L_BRACE;
            unsafe += kind == PARSE_KIND_L_BRACE && !statement;
            records += record;
            if (statement && other == 0 && unsafe == 0)
            {
                cut = token + 1;
            }
        }
        else if (kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE)
        {
            const uint8_t expected = kind == PARSE_KIND_R_BRACE ? PARSE_KIND_L_BRACE : kind == PARSE_KIND_R_PAREN ? PARSE_KIND_L_PAREN : PARSE_KIND_L_SQUARE;
            if (depth == 0 || original->tokens.kinds[stack[depth - 1]] != expected)
            {
                repair->status = CHECK_REPAIR_UNAVAILABLE;
            }
            else
            {
                const uint32_t opener = stack[--depth];
                const bool brace = original->tokens.kinds[opener] == PARSE_KIND_L_BRACE;
                other -= !brace;
                unsafe -= brace && statement_blocks[depth] != 1;
                records -= brace && statement_blocks[depth] == 2;
                if (kind == PARSE_KIND_R_PAREN)
                {
                    parenthesis_keyword = opener != 0 ? original->tokens.kinds[opener - 1] : PARSE_KIND_END;
                }
                if (brace)
                {
                    preceding_statement_block = statement_blocks[depth] == 1;
                    // Keep an original completed switch/loop/block close, so constraints that become available
                    // there are not postponed until a synthetic close. A DO body still needs its while clause;
                    // retain the preceding statement candidate until that clause's semicolon completes it.
                    const bool awaiting_while = opener != 0 && original->tokens.kinds[opener - 1] == PARSE_KIND_DO;
                    if (preceding_statement_block && !awaiting_while && depth != 0 && other == 0 && unsafe == 0)
                    {
                        cut = token + 1;
                    }
                }
            }
        }
        else if (kind == PARSE_KIND_SEMI && depth != 0 && other == 0 && unsafe == records)
        {
            cut = token + 1;
        }
    }
    if (repair->status == CHECK_REPAIR_READY && cut == 0)
    {
        repair->status = CHECK_REPAIR_UNAVAILABLE;
    }
    // Reconstruct only the delimiter stack at the chosen statement boundary, never one copied stack per token.
    depth = 0;
    for (uint32_t token = 0; repair->status == CHECK_REPAIR_READY && token < cut; token += 1)
    {
        const uint8_t kind = original->tokens.kinds[token];
        if (kind == PARSE_KIND_L_BRACE || kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE)
        {
            const uint8_t previous = token != 0 ? original->tokens.kinds[token - 1] : PARSE_KIND_END;
            statement_blocks[depth] = kind == PARSE_KIND_L_BRACE && (previous == PARSE_KIND_STRUCT ||
                                      (previous == PARSE_KIND_IDENTIFIER && token >= 2 && original->tokens.kinds[token - 2] == PARSE_KIND_STRUCT)) ? 2 : 1;
            stack[depth++] = token;
        }
        else if (kind == PARSE_KIND_R_BRACE || kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE)
        {
            depth -= 1;
        }
    }
    uint32_t do_count = 0;
    uint32_t record_count = 0;
    repair->record_begin = cut;
    for (uint32_t i = 0; repair->status == CHECK_REPAIR_READY && i < depth; i += 1)
    {
        do_count += stack[i] != 0 && original->tokens.kinds[stack[i] - 1] == PARSE_KIND_DO;
        record_count += statement_blocks[i] == 2;
        if (statement_blocks[i] == 2 && repair->record_begin > stack[i])
        {
            repair->record_begin = stack[i];
        }
    }
    const uint64_t required = (uint64_t)cut + depth + record_count + (uint64_t)do_count * 5;
    if (repair->status == CHECK_REPAIR_READY && (depth == 0 || required > BUSTER_PARSE_MAX_TOKENS))
    {
        repair->status = CHECK_REPAIR_UNAVAILABLE;
    }
    const uint32_t total = repair->status == CHECK_REPAIR_READY ? (uint32_t)required : 0;
    Parser* view = &repair->parser;
    uint8_t* expected = 0;
    if (repair->status == CHECK_REPAIR_READY)
    {
        repair->preprocessor = do_count != 0 ? check_repair_preprocessor(pp, output) : pp;
        *view = (Parser){ .options = original->options, .language = original->language, .preprocessor = repair->preprocessor };
        memcpy(view->predeclared, original->predeclared, sizeof(view->predeclared));
        view->tokens.count = total;
        view->tokens.capacity = total;
        view->tokens.kinds = check_prefix_column(output, total, 1);
        view->tokens.names = check_prefix_column(output, total, sizeof(uint32_t));
        view->tokens.sources = check_prefix_column(output, total, sizeof(uint32_t));
        view->tokens.flags = check_prefix_column(output, total, 1);
        view->tree.roles = check_prefix_column(output, total, 1);
        view->tree.parents = check_prefix_column(output, total, sizeof(uint32_t));
        view->tree.depths = check_prefix_column(output, total, sizeof(uint32_t));
        view->postorder = check_prefix_column(output, total, sizeof(uint32_t));
        view->sizes = check_prefix_column(output, total, sizeof(uint32_t));
        view->diagnostic_capacity = total + 16;
        view->diagnostics = BUSTER_ARENA_PUSH_ARRAY(output, ParseDiagnostic, view->diagnostic_capacity);
        view->meanings.capacity = original->meanings.capacity;
        view->meanings.meanings = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, view->meanings.capacity);
        expected = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, total);
        if (view->tokens.kinds == 0 || view->tokens.names == 0 || view->tokens.sources == 0 || view->tokens.flags == 0 || view->tree.roles == 0 ||
            view->tree.parents == 0 || view->tree.depths == 0 || view->postorder == 0 || view->sizes == 0 || view->diagnostics == 0 ||
            view->meanings.meanings == 0 || expected == 0 || repair->preprocessor == 0)
        {
            repair->status = CHECK_REPAIR_RESOURCE;
        }
    }
    if (repair->status == CHECK_REPAIR_READY)
    {
        memcpy(view->tokens.kinds, original->tokens.kinds, cut);
        memcpy(view->tokens.names, original->tokens.names, (uint64_t)cut * sizeof(uint32_t));
        memcpy(view->tokens.sources, original->tokens.sources, (uint64_t)cut * sizeof(uint32_t));
        memcpy(view->tokens.flags, original->tokens.flags, cut);
        memset(view->meanings.meanings, PARSE_MEANING_ORDINARY, view->meanings.capacity);
        for (uint32_t i = 0; i < BUSTER_PARSE_PREDECLARED_TYPEDEFS; i += 1)
        {
            view->meanings.meanings[view->predeclared[i]] = PARSE_MEANING_TYPEDEF;
        }
        // A directive between the last retained token and the first excluded token belongs to the original
        // prefix. Its insertion position can equal cut, but its complete spelling remains in original PP output.
        while (view->tokens.pragma_count < original->tokens.pragma_count && original->tokens.pragma_positions[view->tokens.pragma_count] <= cut)
        {
            view->tokens.pragma_count += 1;
        }
        view->tokens.pragma_positions = original->tokens.pragma_positions;
        view->tokens.pragma_sources = original->tokens.pragma_sources;
        uint32_t written = cut;
        for (uint32_t level = depth; level-- > 0;)
        {
            closers[level] = written;
            view->tokens.kinds[written] = PARSE_KIND_R_BRACE;
            expected[written++] = statement_blocks[level] == 2 ? PARSE_ROLE_CLOSE_STRUCT : PARSE_ROLE_NONE;
            const uint32_t opener = stack[level];
            if (statement_blocks[level] == 2)
            {
                view->tokens.kinds[written] = PARSE_KIND_SEMI;
                expected[written++] = PARSE_ROLE_TERMINATOR;
            }
            else if (opener != 0 && original->tokens.kinds[opener - 1] == PARSE_KIND_DO)
            {
                const uint8_t kinds[] = { PARSE_KIND_WHILE, PARSE_KIND_L_PAREN, PARSE_KIND_NUMBER, PARSE_KIND_R_PAREN, PARSE_KIND_SEMI };
                const uint8_t roles[] = { PARSE_ROLE_LEAF_RIGHT, PARSE_ROLE_OPEN_CONDITION, PARSE_ROLE_OPERAND, PARSE_ROLE_CLOSE_CONDITION, PARSE_ROLE_TERMINATOR };
                memcpy(view->tokens.kinds + written, kinds, sizeof(kinds));
                memcpy(expected + written, roles, sizeof(roles));
                view->tokens.names[written] = C_NAME_WHILE;
                view->tokens.sources[written + 2] = pp->output.count;
                written += 5;
            }
        }
        const ParseScalarResult parsed = parse_scalar(view->tokens.kinds, view->tokens.names, view->tokens.flags, total, view->language,
                                                      &view->meanings, &view->tree, scratch, view->diagnostics, view->diagnostic_capacity,
                                                      &view->diagnostic_count);
        repair->status = parsed.out_of_memory ? CHECK_REPAIR_RESOURCE : parsed.errors != 0 ? CHECK_REPAIR_UNAVAILABLE : CHECK_REPAIR_READY;
    }
    for (uint32_t token = cut; repair->status == CHECK_REPAIR_READY && token < total; token += 1)
    {
        const bool valid = expected[token] != PARSE_ROLE_NONE ? view->tree.roles[token] == expected[token] :
                           view->tree.roles[token] == PARSE_ROLE_CLOSE_BODY || view->tree.roles[token] == PARSE_ROLE_CLOSE_BLOCK;
        repair->status = valid ? CHECK_REPAIR_READY : CHECK_REPAIR_UNAVAILABLE;
    }
    for (uint32_t i = 0; repair->status == CHECK_REPAIR_READY && i < depth; i += 1)
    {
        const uint32_t opener = stack[i];
        const uint32_t closer = closers[i];
        const uint8_t role = view->tree.roles[opener];
        const bool record = statement_blocks[i] == 2;
        bool valid = (record ? role == PARSE_ROLE_OPEN_STRUCT : role == PARSE_ROLE_OPEN_BODY || role == PARSE_ROLE_OPEN_BLOCK) && view->tree.parents[opener] == closer &&
                     view->tree.roles[closer] == parse_closer_roles[role];
        if (valid && opener != 0 && original->tokens.kinds[opener - 1] == PARSE_KIND_DO)
        {
            valid = view->tree.roles[opener - 1] == PARSE_ROLE_DO && role == PARSE_ROLE_OPEN_BLOCK && view->tree.parents[closer] == opener - 1;
        }
        repair->status = valid ? CHECK_REPAIR_READY : CHECK_REPAIR_UNAVAILABLE;
    }
    if (repair->status == CHECK_REPAIR_READY)
    {
        const CheckPrefixStatus ordered = check_prefix_order(view, scratch);
        repair->status = ordered == CHECK_PREFIX_READY ? CHECK_REPAIR_READY : ordered == CHECK_PREFIX_RESOURCE ? CHECK_REPAIR_RESOURCE : CHECK_REPAIR_INVALID;
    }
    if (repair->status == CHECK_REPAIR_READY)
    {
        uint32_t* ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, total);
        repair->warning_complete = check_prefix_column(output, total, 1);
        repair->status = ends != 0 && repair->warning_complete != 0 ? CHECK_REPAIR_READY : CHECK_REPAIR_RESOURCE;
        if (repair->status == CHECK_REPAIR_READY)
        {
            for (uint32_t token = 0; token < total; token += 1)
            {
                ends[token] = token;
            }
            for (uint32_t order = 0; order < total; order += 1)
            {
                const uint32_t token = view->postorder[order];
                const uint32_t parent = view->tree.parents[token];
                if (parent < total && ends[parent] < ends[token])
                {
                    ends[parent] = ends[token];
                }
            }
            for (uint32_t order = total; order-- > 0;)
            {
                const uint32_t token = view->postorder[order];
                const uint32_t parent = view->tree.parents[token];
                const uint8_t role = view->tree.roles[token];
                const bool condition = role == PARSE_ROLE_CLOSE_CONDITION || role == PARSE_ROLE_CLOSE_FOR;
                const bool boundary = condition || role == PARSE_ROLE_TERMINATOR || role == PARSE_ROLE_CLOSE_ENUM ||
                                      role == PARSE_ROLE_CLOSE_SUBSCRIPT || role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_SIZEOF ||
                                      role == PARSE_ROLE_CLOSE_KEYWORD || role == PARSE_ROLE_CLOSE_ATTRIBUTE || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER ||
                                      role == PARSE_ROLE_CLOSE_ATTRIBUTE_LEFT || role == PARSE_ROLE_CLOSE_ATTRIBUTE_RIGHT;
                // A complete declaration or constant/type operand supplies all syntax needed by a token-only
                // folding warning. A control condition ends at its ')' even when its statement body is later.
                repair->warning_complete[token] = token < repair->record_begin && ((boundary && (condition ? token : ends[token]) < repair->record_begin) ||
                                                   (parent < total && repair->warning_complete[parent]));
            }
        }
    }
    arena_pop_to(scratch, scratch_mark);
    if (repair->status == CHECK_REPAIR_READY)
    {
        repair->original_end = cut;
    }
    else
    {
        arena_pop_to(output, output_mark);
        repair->parser = (Parser){ 0 };
        repair->preprocessor = 0;
        repair->warning_complete = 0;
    }
    return repair->status;
}

static bool check_repair_warning_token(const CheckRepair* repair, uint32_t token)
{
    return repair->status == CHECK_REPAIR_READY && token < repair->original_end && repair->warning_complete != 0 && repair->warning_complete[token];
}

// Exclusive bound for retained pragma insertion positions only. Original directives just after a retained block
// opening/statement can sit at original_end, where the private view's first synthetic closer will be inserted.
// Ordinary token diagnostics keep their strict original_end/record_begin bounds; a synthetic token is never a
// pragma source. Inside a synthetically completed record, keep the existing stricter record boundary.
static uint32_t check_repair_pragma_end(const CheckRepair* repair)
{
    const uint32_t end = repair->status == CHECK_REPAIR_READY ? repair->record_begin + (repair->record_begin == repair->original_end) : 0;
    return end;
}

static bool check_repair_input_report(const CheckRepair* repair, const CheckInputReport* report)
{
    bool result = false;
    const Parser* view = &repair->parser;
    if (repair->status == CHECK_REPAIR_READY && report->token < repair->original_end)
    {
        const uint32_t token = report->token;
        const uint8_t kind = view->tokens.kinds[token];
        const uint8_t role = view->tree.roles[token];
        const uint32_t parent = view->tree.parents[token];
        // GNU label addresses are not ordinary uses: their target may occur after the retained statements.
        const bool label_address = parent < view->tokens.count && view->tokens.kinds[parent] == PARSE_KIND_AMPAMP &&
                                   view->tree.roles[parent] == PARSE_ROLE_PREFIX;
        // A declarator's complete asm annotation is an original postfix attribute, not an asm statement. Its
        // register-name validity is fixed by the retained string; later statements cannot repair that name.
        const bool asm_name = kind == PARSE_KIND_STRING && role == PARSE_ROLE_OPERAND && token >= 2 &&
                              view->tokens.kinds[token - 2] == PARSE_KIND_ASM && view->tree.roles[token - 1] == PARSE_ROLE_OPEN_ATTRIBUTE &&
                              parent < repair->original_end && view->tree.roles[parent] == PARSE_ROLE_CLOSE_ATTRIBUTE &&
                              view->tree.parents[token - 1] == parent;
        result = (report->message == CHECK_INPUT_UNDECLARED && kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_OPERAND && !label_address) ||
                 (report->message == CHECK_INPUT_LITERAL && (kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING)) ||
                 (report->message == CHECK_INPUT_REDECLARATION && ((kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_OPERAND) || role == PARSE_ROLE_LABEL)) ||
                 // The source reader proves the absence of an enclosing loop/switch from the original control
                 // headers. Appending closing braces cannot create such an enclosing statement around this use.
                 (report->message == CHECK_INPUT_DECLARATION && ((role == PARSE_ROLE_RETURN && (kind == PARSE_KIND_BREAK || kind == PARSE_KIND_CONTINUE)) || asm_name)) ||
                 (report->message == CHECK_INPUT_RETURN && kind == PARSE_KIND_RETURN);
        // The asm's complete original operands fix a naked parameter reference. A synthetic body closer
        // cannot supply the non-asm body constraint, which remains outside this whitelist.
        result |= report->message == CHECK_INPUT_NAKED_PARAMETER && role == PARSE_ROLE_CLOSE_ASM;
        if (report->message == CHECK_INPUT_UNDECLARED && kind == PARSE_KIND_GOTO)
        {
            uint32_t ancestor = token;
            uint32_t steps = 0;
            while (ancestor < view->tokens.count && view->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY && steps < view->tokens.count)
            {
                ancestor = view->tree.parents[ancestor];
                steps += 1;
            }
            result = ancestor < repair->original_end && view->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BODY;
        }
        result &= token < repair->record_begin;
        if (report->message == CHECK_INPUT_INCOMPLETE_MEMBER || report->message == CHECK_INPUT_INCOMPLETE_OBJECT)
        {
            uint32_t ancestor = token;
            uint32_t steps = 0;
            while (ancestor < repair->original_end && view->tree.roles[ancestor] != PARSE_ROLE_TERMINATOR && steps < view->tokens.count)
            {
                ancestor = view->tree.parents[ancestor];
                steps += 1;
            }
            const uint32_t close = ancestor < repair->original_end ? view->tree.parents[ancestor] : view->tokens.count;
            const uint8_t close_role = close < view->tokens.count ? view->tree.roles[close] : PARSE_ROLE_NONE;
            result = report->message == CHECK_INPUT_INCOMPLETE_MEMBER ? close_role == PARSE_ROLE_CLOSE_STRUCT :
                     close_role == PARSE_ROLE_CLOSE_BODY || close_role == PARSE_ROLE_CLOSE_BLOCK ||
                     (close_role == PARSE_ROLE_CLOSE_FOR && close < repair->original_end);
        }
    }
    return result;
}

// The ordering position is exclusive, so it may equal original_end after a complete original statement. A synthetic
// closer would put completion past that bound. Never infer placement/conflicts from a synthetically closed record.
static bool check_repair_loop_report(const CheckRepair* repair, const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputLoopDiagnostic* loop = input->loop_diagnostic;
    return repair->status == CHECK_REPAIR_READY && loop != 0 && report->pragma == loop->pragma && report->token == loop->position &&
           loop->statement < repair->original_end && loop->position <= repair->original_end && repair->record_begin == repair->original_end;
}

static bool check_repair_graph_report(const CheckRepair* repair, const CheckGraph* graph, const CheckReport* report)
{
    bool result = false;
    if (repair->status == CHECK_REPAIR_READY && report->token < repair->record_begin && report->node < graph->count &&
        graph->source_positions != 0 && graph->source_positions[report->node] <= repair->record_begin)
    {
        if (report->message == CHECK_MESSAGE_ACTION)
        {
            switch (report->diagnostic)
            {
                case CHECK_DIAG_ARITHMETIC_REQUIRED:
                case CHECK_DIAG_INTEGER_REQUIRED:
                case CHECK_DIAG_SCALAR_REQUIRED:
                case CHECK_DIAG_POINTER_REQUIRED:
                case CHECK_DIAG_LVALUE_REQUIRED:
                case CHECK_DIAG_MODIFIABLE_REQUIRED:
                case CHECK_DIAG_INCOMPATIBLE:
                case CHECK_DIAG_INCOMPLETE:
                case CHECK_DIAG_BIT_FIELD:
                case CHECK_DIAG_REGISTER_ADDRESS:
                case CHECK_DIAG_MEMBER:
                case CHECK_DIAG_CALL:
                case CHECK_DIAG_ARGUMENT_COUNT: result = true; break;
                default: break;
            }
        }
        else if (report->message == CHECK_MESSAGE_CONSTANT)
        {
            switch (report->fault)
            {
                case CHECK_FAULT_SIGNED_OVERFLOW:
                case CHECK_FAULT_DIVIDE_BY_ZERO:
                case CHECK_FAULT_SHIFT_COUNT:
                case CHECK_FAULT_SHIFT_VALUE: result = true; break;
                default: break;
            }
        }
        else
        {
            result = report->message == CHECK_MESSAGE_ICE || report->message == CHECK_MESSAGE_STATIC_ASSERT ||
                     report->message == CHECK_MESSAGE_STATIC_INITIALIZER;
        }
    }
    return result;
}

#endif
