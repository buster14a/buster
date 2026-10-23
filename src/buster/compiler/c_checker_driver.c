// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Explicit source-checking command for #133's current supported reader. The normal syntax driver remains the
// parser's until the checker passes the full source corpus. Unsupported constructs are errors, never silently
// accepted. Diagnostics retain the preprocessor's source/macro history and are emitted in token order.

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

static void check_driver_dump(const CheckGraph* graph, const CheckStream* stream, TypeTable* types, Arena* scratch)
{
    const TypeNames names = { 0 };
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
        else if (value->constant_kind == CHECK_CONSTANT_ADDRESS)
        {
            printf(" address=%u%+lld", value->object, (long long)value->offset);
        }
        printf(" flags=0x%x fault=%u", value->flags, value->fault);
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
        putchar('\n');
        arena_pop_to(scratch, mark);
    }
}

static bool check_driver_dump_static(const CheckInput* input, const CheckStream* stream, const Parser* parser,
                                      Preprocessor* pp, const TypeTable* types, Arena* scratch)
{
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    CheckDataInput data = { .token = BUSTER_CHECK_NONE, .status = CHECK_DATA_INPUT_RESOURCE };
    const bool valid = output.base != 0 && bytes.base != 0 && check_data_input(&data, input, stream, parser, pp->table, types, &output, &bytes, scratch);
    if (!valid)
    {
        const char* message = data.status == CHECK_DATA_INPUT_RESOURCE ? "static-data output capacity exhausted" :
                              data.status == CHECK_DATA_INPUT_INVALID ? "invalid static-data input" : "static-data emission for this object is not yet supported";
        check_driver_diagnose(parser, pp, data.token, message);
    }
    else
    {
        const Link* link = &data.link;
        printf("static-data: %u objects, %u fixups\n", data.emission.atom_count, data.emission.fixup_count);
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            if (atom != 0)
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
    }
    arena_release(&bytes);
    arena_release(&output);
    return valid;
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
    for (int i = 0; filtered != 0 && i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
        if (strcmp(arguments[i], "--dump-typed") == 0)
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
    if (!parsed)
    {
        fprintf(stderr, "usage: compiler check [--scalar] [--dump-typed] [--dump-static] [preprocessor options] file.c\n");
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
            ready = pp_run(&pp, path);
            if (ready)
            {
                ready = parse_translation_unit(&parser, &pp);
                parse_print_diagnostics(&parser, &pp);
            }
            const uint32_t tokens = parser.tokens.count;
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
            if (ready)
            {
                CheckInput input;
                ready = check_input(&input, &parser, &pp, &types, &arena, &scratch);
                // Adapter reports come from declaration elaboration and the postorder reader. A token-indexed
                // column orders them in linear time and keeps one report per token without discarding failure.
                const uint64_t report_mark = scratch.length;
                uint32_t* first_report = !ready ? BUSTER_ARENA_PUSH_ARRAY(&scratch, uint32_t, (uint64_t)tokens + 1) : 0;
                if (!ready && first_report != 0)
                {
                    for (uint32_t token = 0; token <= tokens; token += 1)
                    {
                        first_report[token] = BUSTER_CHECK_NONE;
                    }
                    for (uint32_t i = 0; i < input.report_count; i += 1)
                    {
                        const uint32_t token = input.reports[i].token < tokens ? input.reports[i].token : tokens;
                        if (first_report[token] == BUSTER_CHECK_NONE)
                        {
                            first_report[token] = i;
                        }
                    }
                    for (uint32_t token = 0; token <= tokens; token += 1)
                    {
                        if (first_report[token] != BUSTER_CHECK_NONE)
                        {
                            check_driver_diagnose(&parser, &pp, token, check_driver_input_message(input.reports[first_report[token]].message));
                        }
                    }
                }
                if (input.resource || (!ready && first_report == 0))
                {
                    fprintf(stderr, "fatal error: checker input capacity exhausted\n");
                }
                arena_pop_to(&scratch, report_mark);
                if (ready)
                {
                    const CheckOptions check_options = { .scalar = scalar };
                    CheckStream stream;
                    ready = check_graph_run(&types, &input.graph, &output_arena, &scratch, &check_options, &stream);
                    if (ready)
                    {
                        for (uint32_t i = 0; i < stream.report_count; i += 1)
                        {
                            check_driver_diagnose(&parser, &pp, stream.reports[i].token, check_driver_report_message(stream.reports + i));
                        }
                        if (dump)
                        {
                            check_driver_dump(&input.graph, &stream, &types, &scratch);
                        }
                        result = stream.report_count == 0 ? 0 : 1;
                        if (result == 0 && dump_static)
                        {
                            result = check_driver_dump_static(&input, &stream, &parser, &pp, &types, &scratch) ? 0 : 1;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "fatal error: %s\n", stream.status == CHECK_STATUS_RESOURCE ? "checker output capacity exhausted" : "invalid checker input graph");
                    }
                }
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
