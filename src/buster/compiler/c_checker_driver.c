// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source checking and object emission share one typed stream. Unsupported constructs are errors, never silently
// accepted. Diagnostics retain source/macro history and are ordered by the point their constraint is known.

static const char* check_driver_action_message(CheckDiagnostic diagnostic)
{
    const char* result = "invalid expression";
    switch (diagnostic)
    {
        case CHECK_DIAG_INVALID_TYPE: result = "invalid operand type"; break;
        case CHECK_DIAG_ARITHMETIC_REQUIRED: result = "arithmetic operands required"; break;
        case CHECK_DIAG_INTEGER_REQUIRED: result = "integer operands required"; break;
        case CHECK_DIAG_SCALAR_REQUIRED: result = "scalar operand required"; break;
        case CHECK_DIAG_POINTER_REQUIRED: result = "pointer operand required"; break;
        case CHECK_DIAG_LVALUE_REQUIRED: result = "lvalue required"; break;
        case CHECK_DIAG_MODIFIABLE_REQUIRED: result = "modifiable lvalue required"; break;
        case CHECK_DIAG_INCOMPATIBLE: result = "incompatible operand or conversion types"; break;
        case CHECK_DIAG_INCOMPLETE: result = "incomplete type is not permitted here"; break;
        case CHECK_DIAG_BIT_FIELD: result = "invalid use of a bit-field"; break;
        case CHECK_DIAG_REGISTER_ADDRESS: result = "address of register object requested"; break;
        case CHECK_DIAG_MEMBER: result = "no such member in the record type"; break;
        case CHECK_DIAG_CALL: result = "called object is not a function or function pointer"; break;
        case CHECK_DIAG_ARGUMENT_COUNT: result = "incorrect number of arguments"; break;
        case CHECK_DIAG_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_DIAG_UNSUPPORTED: result = "expression is not yet supported by the type checker"; break;
        case CHECK_DIAG_NONE: break;
    }
    return result;
}

static const char* check_driver_fault_message(CheckFault fault)
{
    const char* result = "invalid constant expression";
    switch (fault)
    {
        case CHECK_FAULT_SIGNED_OVERFLOW: result = "signed overflow in constant expression"; break;
        case CHECK_FAULT_DIVIDE_BY_ZERO: result = "division by zero in constant expression"; break;
        case CHECK_FAULT_SHIFT_COUNT: result = "invalid shift count in constant expression"; break;
        case CHECK_FAULT_SHIFT_VALUE: result = "invalid signed shift in constant expression"; break;
        case CHECK_FAULT_FLOAT_RANGE: result = "constant conversion is outside the target type's range"; break;
        case CHECK_FAULT_UNSUPPORTED_CONSTANT: result = "constant expression is not yet supported"; break;
        case CHECK_FAULT_NONE: break;
    }
    return result;
}

static const char* check_driver_input_message(CheckInputMessage message)
{
    const char* result = "invalid checker input";
    switch (message)
    {
        case CHECK_INPUT_UNSUPPORTED: result = "construct is not yet supported by the type checker"; break;
        case CHECK_INPUT_DECLARATION: result = "invalid or unsupported declaration"; break;
        case CHECK_INPUT_UNDECLARED: result = "use of undeclared identifier"; break;
        case CHECK_INPUT_LITERAL: result = "invalid or unsupported literal"; break;
        case CHECK_INPUT_RESOURCE: result = "type checker ran out of memory"; break;
        case CHECK_INPUT_REDECLARATION: result = "conflicting declaration"; break;
        case CHECK_INPUT_RETURN: result = "return does not match the function result type"; break;
        case CHECK_INPUT_TARGET: result = "unsupported checker language or target model"; break;
        case CHECK_INPUT_INCOMPLETE_MEMBER: result = "record member has incomplete type"; break;
        case CHECK_INPUT_INCOMPLETE_OBJECT: result = "object has incomplete type"; break;
    }
    return result;
}

static const char* check_driver_report_message(const CheckReport* report)
{
    const char* result = "invalid expression";
    switch (report->message)
    {
        case CHECK_MESSAGE_ACTION: result = check_driver_action_message(report->diagnostic); break;
        case CHECK_MESSAGE_CONSTANT: result = check_driver_fault_message(report->fault); break;
        case CHECK_MESSAGE_ICE: result = "expression is not an integer constant expression"; break;
        case CHECK_MESSAGE_STATIC_ASSERT: result = "static assertion failed"; break;
        case CHECK_MESSAGE_STATIC_INITIALIZER: result = "initializer is not a constant expression"; break;
    }
    return result;
}

static const char* check_driver_initializer_message(CheckInitStatus status)
{
    const char* result = "invalid initializer";
    switch (status)
    {
        case CHECK_INIT_NON_CONSTANT: result = "initializer is not a constant expression"; break;
        case CHECK_INIT_UNSUPPORTED: result = "initializer construct is not supported"; break;
        case CHECK_INIT_RESOURCE: result = "initializer checking capacity exhausted"; break;
        case CHECK_INIT_INVALID:
        case CHECK_INIT_CHECK_FAILED:
        case CHECK_INIT_OK: break;
    }
    return result;
}

static void check_driver_diagnose(const Parser* parser, Preprocessor* pp, uint32_t token, const char* message)
{
    if (token < parser->tokens.count)
    {
        pp_diagnose_output(pp, PP_SEVERITY_ERROR, parser->tokens.sources[token], "%s", message);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, parse_end_location(pp), "%s", message);
    }
}

typedef struct CheckDriverDiagnostic CheckDriverDiagnostic;
struct CheckDriverDiagnostic
{
    uint32_t token;
    const char* message;
    uint32_t position;
};

typedef struct CheckDriverWarning CheckDriverWarning;
struct CheckDriverWarning
{
    uint16_t flags;
    uint16_t enabled;
};

static bool check_driver_reports(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                  const CheckRepair* repair, const CheckDriverDiagnostic* extra, uint32_t extra_count,
                                  const uint32_t* excess_tokens, uint32_t excess_count, bool quiet, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    const uint32_t count = parser->tokens.count;
    const bool reports = input->report_count != 0 || (stream != 0 && stream->report_count != 0) || extra_count != 0;
    uint64_t* first = reports ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : NULL;
    const bool show_warnings = !quiet;
    CheckDriverWarning* warnings = show_warnings ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDriverWarning, (uint64_t)count + 1) : NULL;
    const bool ready = (!reports || first != NULL) && (!show_warnings || warnings != NULL);
    if (ready)
    {
        for (uint32_t token = 0; reports && token <= count; token += 1)
        {
            first[token] = UINT64_MAX;
        }
        if (show_warnings)
        {
            memset(warnings, 0, ((uint64_t)count + 1) * sizeof(*warnings));
            for (uint32_t token = 0; input->constant_warnings != NULL && token < count && token < input->token_count; token += 1)
            {
                const uint8_t flags = input->constant_warnings[token];
                const uint8_t ignored = input->constant_warning_ignored != NULL ? input->constant_warning_ignored[token] : 0;
                warnings[token].flags = flags;
                warnings[token].enabled = flags & ~ignored;
            }
            for (uint32_t node = 0; stream != NULL && node < stream->count; node += 1)
            {
                const uint32_t token = input->graph.tokens[node];
                if (token < count)
                {
                    const uint32_t flags = stream->warnings[node];
                    const uint32_t ignored = stream->warning_ignored != NULL ? stream->warning_ignored[node] : 0;
                    warnings[token].flags |= flags;
                    warnings[token].enabled |= flags & ~ignored;
                }
            }
            for (uint32_t i = 0; i < excess_count; i += 1)
            {
                if (excess_tokens[i] < count)
                {
                    warnings[excess_tokens[i]].flags |= CHECK_WARNING_EXCESS_INITIALIZERS;
                    warnings[excess_tokens[i]].enabled |= CHECK_WARNING_EXCESS_INITIALIZERS;
                }
            }
        }
        for (uint32_t i = 0; i < input->report_count; i += 1)
        {
            const uint32_t token = input->reports[i].token < count ? input->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_input_report(repair, input->reports + i)))
            {
                first[token] = i;
            }
        }
        for (uint32_t i = 0; stream != 0 && i < stream->report_count; i += 1)
        {
            const uint32_t token = stream->reports[i].token < count ? stream->reports[i].token : count;
            if (first[token] == UINT64_MAX && (repair == NULL || check_repair_graph_report(repair, &input->graph, stream->reports + i)))
            {
                first[token] = (UINT64_C(1) << 32) | i;
            }
        }
        for (uint32_t i = 0; i < extra_count; i += 1)
        {
            const uint32_t token = extra[i].position < count ? extra[i].position : count;
            if (first[token] == UINT64_MAX)
            {
                first[token] = (UINT64_C(2) << 32) | i;
            }
        }
        uint32_t pragma = 0;
        for (uint32_t token = 0; !pp->fatal && token <= count; token += 1)
        {
            while (!pp->fatal && pragma < input->fp_pragma_count && input->fp_pragmas[pragma].token == token)
            {
                const CheckInputFpPragma* record = input->fp_pragmas + pragma;
                const bool retained = repair == NULL || token < repair->record_begin;
                if (show_warnings && retained && (record->warnings & CHECK_FP_PRAGMA_UNKNOWN_STATE) != 0)
                {
                    const CheckWarningSeverity severity = check_diagnostic_severity_at(input, token, record->source_begin, CHECK_WARNING_UNKNOWN_PRAGMA, false);
                    if (severity != CHECK_WARNING_IGNORE)
                    {
                        const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                                       severity == CHECK_WARNING_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
                        pp_diagnose_output(pp, pp_severity, record->source_begin, "%s", check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA));
                    }
                }
                pragma += 1;
            }
            uint32_t flags = show_warnings && (repair == NULL || check_repair_warning_token(repair, token)) ?
                             warnings[token].flags : 0;
            while (!pp->fatal && flags != 0)
            {
                const uint32_t warning = flags & (0u - flags);
                const bool ignored = (warnings[token].enabled & warning) == 0;
                const CheckWarningSeverity severity = check_diagnostic_severity_default(input, token, warning, ignored);
                if (severity != CHECK_WARNING_IGNORE)
                {
                    const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
                    pp_diagnose_output(pp, pp_severity, parser->tokens.sources[token], "%s", check_diagnostic_message(warning));
                }
                flags &= flags - 1;
            }
            if (!pp->fatal && reports && first[token] != UINT64_MAX)
            {
                const uint32_t index = (uint32_t)first[token];
                const uint32_t phase = (uint32_t)(first[token] >> 32);
                const char* message = phase == 2 ? extra[index].message : phase == 1 ? check_driver_report_message(stream->reports + index) :
                                                                                     check_driver_input_message(input->reports[index].message);
                const uint32_t source = phase == 0 ? check_input_report_source(parser, input->reports + index) : BUSTER_CHECK_NONE;
                if (source != BUSTER_CHECK_NONE)
                {
                    pp_diagnose_output(pp, PP_SEVERITY_ERROR, source, "%s", message);
                }
                else
                {
                    check_driver_diagnose(parser, pp, phase == 2 ? extra[index].token : token, message);
                }
            }
        }
    }
    arena_pop_to(scratch, mark);
    return ready;
}

static void check_driver_dump(const CheckGraph* graph, const CheckStream* stream, TypeTable* types, Arena* scratch)
{
    const TypeNames names = { 0 };
    uint32_t effect = 0;
    uint32_t varargs = 0;
    for (uint32_t node = 0; node < stream->count; node += 1)
    {
        const uint64_t mark = scratch->length;
        const CheckValue* value = stream->values + node;
        const String8 type = value->type != 0 ? type_print(types, scratch, value->type, (String8){ 0 }, &names, 0) : (String8){ 0 };
        printf("%u token=%u op=%u type=%.*s category=%u", node, graph->tokens[node], graph->ops[node], (int)type.length, type.pointer != 0 ? type.pointer : "", value->category);
        if (value->constant_kind == CHECK_CONSTANT_INTEGER)
        {
            printf(" integer=0x%016llx%016llx", (unsigned long long)(value->integer >> 64), (unsigned long long)value->integer);
        }
        else if (value->constant_kind == CHECK_CONSTANT_FLOATING)
        {
            printf(" floating=%La", value->floating);
        }
        else if (value->constant_kind == CHECK_CONSTANT_COMPLEX)
        {
            printf(" complex=(floating:%La,imaginary:%La,integer:0x%016llx%016llx)", value->floating, value->imaginary,
                   (unsigned long long)(value->integer >> 64), (unsigned long long)value->integer);
        }
        else if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
        {
            printf(" address=%u%+lld", value->object, (long long)value->offset);
        }
        printf(" flags=0x%x fault=%u warnings=0x%x", value->flags, value->fault, stream->warnings[node]);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            const CheckConversion conversion = stream->conversions[node].operands[i];
            if (conversion.flags != 0)
            {
                printf(" operand%u=(type:%u,conversions:0x%x)", i, conversion.type, conversion.flags);
            }
        }
        const CheckConversion store = stream->conversions[node].store;
        if (store.flags != 0)
        {
            printf(" store=(type:%u,conversions:0x%x)", store.type, store.flags);
        }
        if (effect < stream->effect_count && stream->effects[effect].node == node)
        {
            const CheckStoreEffect* write = stream->effects + effect++;
            printf(" effect=(store-type:%u,destination-type:%u,value-kind:%u,runtime-builtin:%u,evaluated:0x%x,warnings:0x%x,warning-operands:0x%x,unsupported:0x%x)",
                   write->value.type, write->destination.type, write->value.constant_kind, write->runtime_builtin, write->evaluated_operands,
                   write->warnings, write->warning_operands, write->unsupported);
        }
        if (varargs < stream->varargs_count && stream->varargs[varargs].node == node)
        {
            const CheckVarargsEffect* action = stream->varargs + varargs++;
            printf(" varargs=(type:%u,evaluated:0x%x,warnings:0x%x,mutates:%u)", action->requested_type, action->evaluated_operands,
                   action->warnings, action->mutates_list);
        }
        putchar('\n');
        arena_pop_to(scratch, mark);
    }
}

static bool check_driver_dump_link(const Link* link, const CheckDataResult* emission)
{
    printf("static-data: %u objects, %u fixups\n", emission->atom_count, emission->fixup_count);
    for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        if (atom != 0 && link->atoms.class[atom] != BUSTER_LINK_CLASS_TEXT)
        {
            const NameId name = link->symbols.name[symbol];
            printf("object=%u name=%.*s class=%u align=%" PRIu64 " size=%" PRIu64 " bytes=", symbol,
                   (int)link->names.length[name], link->names.bytes[name], link->atoms.class[atom],
                   UINT64_C(1) << link->atoms.alignment_log2[atom], link->atoms.size[atom]);
            if (link->atoms.bytes[atom] == 0)
            {
                printf("zero");
            }
            else
            {
                for (uint64_t byte = 0; byte < link->atoms.size[atom]; byte += 1)
                {
                    printf("%02x", link->atoms.bytes[atom][byte]);
                }
            }
            for (uint32_t i = 0; i < link->atoms.fixup_count[atom]; i += 1)
            {
                const FixupId fixup = link->atoms.fixup_begin[atom] + i;
                const SymbolId target = link->fixups.target[fixup];
                const NameId target_name = link->symbols.name[target];
                printf(" fixup=(offset:%u,target:%.*s,addend:%" PRId64 ")", link->fixups.offset[fixup],
                       (int)link->names.length[target_name], link->names.bytes[target_name], link_fixup_addend(link, fixup));
            }
            putchar('\n');
        }
    }
    return ferror(stdout) == 0;
}

static bool check_driver_dump_static(const CheckInput* input, const CheckStream* stream, const CheckInitializers* initializers, const Parser* parser,
                                      Preprocessor* pp, TypeTable* types, Arena* scratch)
{
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    CheckDataInput data = { .token = BUSTER_CHECK_NONE, .status = CHECK_DATA_INPUT_RESOURCE };
    bool valid = output.base != 0 && bytes.base != 0 && check_data_input_prepared(&data, input, stream, initializers, parser, pp->table, types, &output, &bytes, scratch);
    if (!valid)
    {
        const char* message = data.status == CHECK_DATA_INPUT_RESOURCE ? "static-data output capacity exhausted" :
                              data.status == CHECK_DATA_INPUT_INVALID ? "invalid static-data input" : "static-data emission for this object is not yet supported";
        check_driver_diagnose(parser, pp, data.token, message);
    }
    else
    {
        valid = check_driver_dump_link(&data.link, &data.emission);
    }
    arena_release(&bytes);
    arena_release(&output);
    return valid;
}

static bool check_driver_object(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                 Preprocessor* pp, TypeTable* types, const char* source_path, const char* output_path,
                                 bool scalar, bool dump_static, Arena* scratch)
{
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    const uint64_t mark = scratch->length;
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE, .failed_token = BUSTER_CHECK_NONE };
    bool valid = output.base != 0 && bytes.base != 0 &&
                 check_program_emit(&code, program, parser, pp->table, data->capacity, scalar, &output, &bytes, scratch);
    if (!valid)
    {
        const char* message = code.status == CHECK_PROGRAM_RESOURCE ? "code output capacity exhausted" :
                              code.status == CHECK_PROGRAM_UNSUPPORTED ? "checked C type or operation is not supported by this backend" :
                              "checked program could not be emitted";
        check_driver_diagnose(parser, pp, code.token, message);
    }
    if (valid)
    {
        valid = check_data_publish(data, types, program->stream, program->object_symbols, program->object_count,
                                   &code.link, 0, &bytes, scratch, &emission);
        if (!valid)
        {
            check_driver_diagnose(parser, pp, emission.failed_token, "checked static data could not be emitted");
        }
    }
    if (valid)
    {
        ObjectUnits units = { 0 };
        ObjectSymbols symbols = { 0 };
        valid = link_check(&code.link) == LINK_ERROR_NONE && object_prepare(&units, &code.link, 1, scratch) &&
                object_map_symbols(&symbols, &code.link, scratch);
        if (valid)
        {
            const String8 source = { .pointer = (char*)source_path, .length = strlen(source_path) };
            const Object object = object_build(&code.link, &units, &symbols, 0, (ObjectOptions){ 0 }, source, scratch);
            // The optional dump consumes the same resolved Link as the object writer. Validate and render it
            // before publishing the file, so a later diagnostic cannot leave a newly emitted rejected object.
            valid = object.error == OBJECT_ERROR_NONE && (!dump_static || check_driver_dump_link(&code.link, &emission)) &&
                    object_write_file(output_path, object.bytes, object.size) == OBJECT_ERROR_NONE;
        }
        if (!valid)
        {
            fprintf(stderr, "error: could not write object file %s\n", output_path);
        }
    }
    arena_pop_to(scratch, mark);
    arena_release(&bytes);
    arena_release(&output);
    return valid;
}

static const char* check_driver_object_name(Arena* arena, const char* source)
{
    const char* slash = strrchr(source, '/');
    const char* name = slash != 0 ? slash + 1 : source;
    const char* dot = strrchr(name, '.');
    const uint64_t length = dot != 0 && dot != name ? (uint64_t)(dot - name) : strlen(name);
    char* result = length <= UINT64_MAX - 3 ? BUSTER_ARENA_PUSH_ARRAY(arena, char, length + 3) : 0;
    if (result != 0)
    {
        memcpy(result, name, length);
        memcpy(result + length, ".o", 3);
    }
    return result;
}

static int run_check_command(int count, char** arguments)
{
    int result = 1;
    Arena arena = arena_reserve(1ull << 36);
    Arena scratch = arena_reserve(1ull << 36);
    Arena output_arena = arena_reserve(1ull << 36);
    Arena type_arena = { 0 };
    Arena index_arena = { 0 };
    char** filtered = arena_push_array(&arena, (uint64_t)count + 1, sizeof(char*), _Alignof(char*));
    uint32_t filtered_count = 0;
    bool scalar = false;
    bool dump = false;
    bool dump_static = false;
    bool emit_object = false;
    bool quiet = false;
    bool lazy_system_headers = false;
    for (int i = 0; filtered != 0 && i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
        quiet |= strcmp(arguments[i], "-w") == 0;
        if (strcmp(arguments[i], "-flazy-system-headers") == 0)
        {
            lazy_system_headers = true;
        }
        else if (strcmp(arguments[i], "-fno-lazy-system-headers") == 0)
        {
            lazy_system_headers = false;
        }
        else if (strcmp(arguments[i], "-c") == 0)
        {
            emit_object = true;
        }
        else if (strcmp(arguments[i], "--dump-typed") == 0)
        {
            dump = true;
        }
        else if (strcmp(arguments[i], "--dump-static") == 0)
        {
            dump_static = true;
        }
        else
        {
            filtered[filtered_count++] = arguments[i];
        }
    }
    PPOptions options;
    const char* path = 0;
    const char* output = 0;
    bool markers = false;
    const bool parsed = filtered != 0 && pp_parse_options((int)filtered_count, filtered, &options, &arena, &path, &output, &markers);
    options.semantic_pragmas = true;
    if (!parsed)
    {
        fprintf(stderr, "usage: compiler check [--scalar] [--dump-typed] [--dump-static] [-flazy-system-headers] [-c [-o file.o]] [preprocessor options] file.c\n");
    }
    else if (!check_input_target_supported(&options))
    {
        fprintf(stderr, "error: checker requires C, signed char, 32-bit wchar_t, x87 long double and source-precision floating evaluation\n");
    }
    else
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        const ParseOptions parser_options = { .scalar = scalar };
        bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &options, &identifiers) && parse_initialize(&parser, &parser_options, options.language);
        if (!ready || scratch.base == 0 || output_arena.base == 0)
        {
            fprintf(stderr, "fatal error: can't reserve checker memory\n");
        }
        else
        {
            CheckPrefix prefix = { 0 };
            CheckRepair repair = { 0 };
            const Parser* checking = &parser;
            Preprocessor* checking_pp = &pp;
            bool prefix_only = false;
            bool repairing = false;
            bool parse_failed = false;
            ready = pp_run(&pp, path);
            if (ready)
            {
                ready = parse_translation_unit(&parser, &pp);
                parse_failed = !ready;
                if (ready)
                {
                    parse_print_diagnostics(&parser, &pp);
                }
                else if (check_repair_make(&repair, &parser, &pp, &arena, &scratch) == CHECK_REPAIR_READY)
                {
                    // Only completed original expressions can contribute diagnostics. The synthetic closure
                    // belongs to a private checking view and can never produce a successful source result.
                    checking = &repair.parser;
                    checking_pp = repair.preprocessor;
                    prefix_only = true;
                    repairing = true;
                    ready = true;
                }
                else if (check_prefix_make(&prefix, &parser, &arena, &scratch) == CHECK_PREFIX_READY)
                {
                    // Only complete earlier declarations survive this view. Its checking suppresses obligations
                    // that belong to the full translation-unit end; the original parser rejection stays final.
                    checking = &prefix.parser;
                    prefix_only = true;
                    ready = true;
                }
            }
            const uint32_t tokens = checking->tokens.count;
            const TypeCapacity capacity = {
                .types = tokens <= (1u << 27) ? tokens * 4 + 64 : 0,
                .parameters = tokens,
                .tags = tokens,
                .members = tokens,
            };
            TypeTable types = { 0 };
            if (ready)
            {
                type_arena = arena_reserve(type_capacity_bytes(capacity));
                index_arena = arena_reserve(type_index_capacity_bytes(capacity));
                ready = capacity.types != 0 && type_initialize(&types, &type_arena, &index_arena, capacity);
                if (!ready)
                {
                    fprintf(stderr, "fatal error: can't reserve type table\n");
                }
            }
            CheckLazy lazy = { 0 };
            if (ready && lazy_system_headers && !prefix_only)
            {
                ready = check_lazy_plan(&lazy, checking, checking_pp, &arena, &scratch, scalar);
                if (!ready)
                {
                    fprintf(stderr, "fatal error: system-header checking selection failed\n");
                }
            }
            if (ready)
            {
                CheckInput input;
                const bool input_valid = repairing ? check_input_fragment(&input, checking, checking_pp, &types, &arena, &scratch, repair.original_end) :
                                         prefix_only ? check_input_prefix(&input, checking, checking_pp, &types, &arena, &scratch) :
                                         lazy_system_headers ? check_input_selected(&input, checking, checking_pp, &types, &arena, &scratch, lazy.deferred_tokens) :
                                                       check_input(&input, checking, checking_pp, &types, &arena, &scratch);
                if (input.resource)
                {
                    fprintf(stderr, "fatal error: checker input capacity exhausted\n");
                }
                // A rejected reader may retain complete earlier expressions solely for diagnostics. Their typed
                // reports share source order with declaration reports, but this fragment can never be emitted.
                if (input_valid || input.diagnostic_graph)
                {
                    const CheckOptions check_options = { .scalar = scalar };
                    CheckStream stream;
                    ready = check_graph_run(&types, &input.graph, &output_arena, &scratch, &check_options, &stream);
                    if (ready)
                    {
                        CheckDriverDiagnostic additional[3] = { 0 };
                        uint32_t additional_count = 0;
                        const uint32_t* excess_tokens = NULL;
                        uint32_t excess_count = 0;
                        CheckAsms asms = { 0 };
                        CheckInitializers initializers = { 0 };
                        const bool expression_valid = input_valid && stream.report_count == 0;
                        if (expression_valid && !repairing)
                        {
                            if (!check_asms_run(&asms, &input, &stream, &types, &output_arena, &scratch))
                            {
                                additional[additional_count++] = (CheckDriverDiagnostic){ asms.token,
                                    asms.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", asms.token };
                            }
                            if (check_initializers_run(&initializers, &input, &stream, checking, &types, &output_arena, &scratch))
                            {
                                excess_tokens = initializers.warning_tokens;
                                excess_count = initializers.warning_count;
                            }
                        }
                        if (!expression_valid || repairing || initializers.status != CHECK_INIT_OK)
                        {
                            CheckInitDiagnostic earlier = { 0 };
                            const uint32_t before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                            CheckInitWarningSink warnings = { .capacity = !quiet ? tokens : 0 };
                            warnings.tokens = warnings.capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(&output_arena, uint32_t, warnings.capacity) : NULL;
                            const bool storage = warnings.capacity == 0 || warnings.tokens != NULL;
                            const bool diagnosed = storage && initializers.status != CHECK_INIT_RESOURCE &&
                                                   check_initializers_diagnose_warnings(&earlier, &input, &stream, checking, &types, before,
                                                                                         &output_arena, &scratch, !quiet ? &warnings : NULL);
                            if (!storage || initializers.status == CHECK_INIT_RESOURCE)
                            {
                                earlier.status = CHECK_INIT_RESOURCE;
                                earlier.token = initializers.token < tokens ? initializers.token : 0;
                            }
                            else if (!diagnosed && initializers.status != CHECK_INIT_OK)
                            {
                                earlier.status = initializers.status;
                                earlier.token = initializers.token;
                            }
                            if ((diagnosed && earlier.status != CHECK_INIT_OK) || earlier.status == CHECK_INIT_RESOURCE ||
                                (!diagnosed && initializers.status != CHECK_INIT_OK))
                            {
                                additional[additional_count++] = (CheckDriverDiagnostic){ earlier.token, check_driver_initializer_message(earlier.status), earlier.token };
                            }
                            if (diagnosed)
                            {
                                excess_tokens = warnings.tokens;
                                excess_count = warnings.count;
                            }
                        }
                        if (!expression_valid || repairing)
                        {
                            CheckAsmDiagnostic assembly = { 0 };
                            const uint32_t before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                            const bool asm_diagnosed = check_asms_diagnose(&assembly, &input, &stream, &types, before, &scratch);
                            if ((asm_diagnosed && assembly.status != CHECK_ASM_OK) || assembly.status == CHECK_ASM_RESOURCE)
                            {
                                additional[additional_count++] = (CheckDriverDiagnostic){ assembly.token,
                                    assembly.status == CHECK_ASM_RESOURCE ? "assembly operand checking capacity exhausted" : "invalid assembly operand or constraint", assembly.token };
                            }
                        }
                        uint32_t promoted_token = BUSTER_CHECK_NONE;
                        bool promoted = !quiet && check_diagnostic_first_error(&input, &stream, &promoted_token);
                        uint32_t promoted_source = BUSTER_CHECK_NONE;
                        promoted |= !quiet && check_diagnostic_first_pragma_error(&input, repairing ? repair.record_begin : BUSTER_CHECK_NONE,
                                                                                &promoted_token, &promoted_source);
                        for (uint32_t i = 0; !quiet && !promoted && i < excess_count; i += 1)
                        {
                            promoted = check_diagnostic_severity(&input, excess_tokens[i], CHECK_WARNING_EXCESS_INITIALIZERS) >= CHECK_WARNING_ERROR;
                        }
                        if (!expression_valid || repairing || additional_count != 0 || promoted)
                        {
                            CheckProgramDiagnostic control = { 0 };
                            const uint32_t control_before = repairing ? repair.record_begin : BUSTER_CHECK_NONE;
                            const bool control_diagnosed = check_program_diagnose(&control, &input, &stream, checking, &types, control_before, &scratch);
                            if ((control_diagnosed && control.status != CHECK_PROGRAM_OK) || control.status == CHECK_PROGRAM_RESOURCE)
                            {
                                additional[additional_count++] = (CheckDriverDiagnostic){ control.token,
                                    control.status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" : "invalid C statement or control flow", control.position };
                            }
                        }
                        if (!check_driver_reports(checking, &pp, &input, &stream, repairing ? &repair : NULL, additional, additional_count,
                                                  excess_tokens, excess_count, quiet, &scratch))
                        {
                            fprintf(stderr, "fatal error: checker diagnostic capacity exhausted\n");
                            ready = false;
                        }
                        if (input_valid && dump && !prefix_only)
                        {
                            check_driver_dump(&input.graph, &stream, &types, &scratch);
                        }
                        result = ready && expression_valid && additional_count == 0 && !repairing && pp.errors == 0 ? 0 : 1;
                        CheckProgram program;
                        CheckDataPrepared data = { 0 };
                        if (result == 0 && emit_object && !prefix_only && !check_data_prepare(&data, &input, &stream, &initializers, &types, &output_arena, &scratch))
                        {
                            check_driver_diagnose(checking, &pp, data.token, "static-data preparation failed");
                            result = 1;
                        }
                        const CheckProgramOptions program_options = {
                            .initializers = &initializers,
                            .asms = &asms,
                            .identifiers = pp.table,
                            .initializer_references = data.references,
                            .initializer_reference_count = data.reference_count,
                            .scalar = scalar,
                        };
                        if (result == 0 && !check_program(&program, checking, &input, &stream, &types, program_options, &output_arena, &scratch))
                        {
                            const char* message = program.status == CHECK_PROGRAM_RESOURCE ? "checker program capacity exhausted" :
                                                  program.status == CHECK_PROGRAM_UNSUPPORTED ? "unsupported C statement" : "invalid C statement or control flow";
                            check_driver_diagnose(checking, &pp, program.token, message);
                            result = 1;
                        }
                        if (result == 0 && emit_object && !prefix_only)
                        {
                            CheckProgramSelection selected;
                            if (check_program_select(&selected, &program, &data, scalar, &output_arena, &scratch))
                            {
                                const char* object_path = output != 0 ? output : check_driver_object_name(&arena, path);
                                result = object_path != 0 && check_driver_object(&selected.program, &selected.data, checking, &pp, &types, path,
                                                                               object_path, scalar, dump_static, &scratch) ? 0 : 1;
                            }
                            else
                            {
                                check_driver_diagnose(checking, &pp, selected.token, "checked definition selection failed");
                                result = 1;
                            }
                        }
                        if (result == 0 && dump_static && !emit_object && !prefix_only)
                        {
                            result = check_driver_dump_static(&input, &stream, &initializers, checking, &pp, &types, &scratch) ? 0 : 1;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "fatal error: %s\n", stream.status == CHECK_STATUS_RESOURCE ? "checker output capacity exhausted" : "invalid checker input graph");
                    }
                }
                else if (!check_driver_reports(checking, &pp, &input, NULL, repairing ? &repair : NULL, NULL, 0, NULL, 0, quiet, &scratch))
                {
                    fprintf(stderr, "fatal error: checker diagnostic capacity exhausted\n");
                }
            }
            if (parse_failed)
            {
                if (!pp.fatal)
                {
                    parse_print_diagnostics(&parser, &pp);
                }
                result = 1;
            }
        }
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    arena_release(&index_arena);
    arena_release(&type_arena);
    arena_release(&output_arena);
    arena_release(&scratch);
    arena_release(&arena);
    return result;
}
