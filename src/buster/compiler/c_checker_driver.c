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

static void check_driver_pragma_report(Preprocessor* pp, const CheckInput* input, const CheckInputFpPragma* record, bool quiet)
{
    if ((record->warnings & CHECK_FP_PRAGMA_INVALID_PLACEMENT) != 0)
    {
        pp_diagnose_output(pp, PP_SEVERITY_ERROR, check_diagnostic_pragma_source(record), "floating-point pragma is not permitted within an expression");
    }
    else if (!quiet && (record->warnings & CHECK_FP_PRAGMA_UNKNOWN_STATE) != 0)
    {
        const CheckWarningSeverity severity = check_diagnostic_severity_at(input, record->token, record->source_begin,
                                                                           CHECK_WARNING_UNKNOWN_PRAGMA, false);
        if (severity != CHECK_WARNING_IGNORE)
        {
            const PPSeverity pp_severity = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_FATAL :
                                           severity == CHECK_WARNING_ERROR ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
            pp_diagnose_output(pp, pp_severity, check_diagnostic_pragma_source(record), "%s", check_diagnostic_message(CHECK_WARNING_UNKNOWN_PRAGMA));
        }
    }
}

static bool check_driver_reports(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream,
                                  const CheckRepair* repair, const CheckInput* original_pragmas, const CheckDriverDiagnostic* extra, uint32_t extra_count,
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
            for (uint32_t token = 0; token < count && token < input->token_count; token += 1)
            {
                const uint32_t flags = check_diagnostic_attribute_warnings(input, token);
                warnings[token].flags = flags;
                warnings[token].enabled = flags;
            }
            for (uint32_t token = 0; input->constant_warnings != NULL && token < count && token < input->token_count; token += 1)
            {
                const uint8_t flags = input->constant_warnings[token];
                const uint8_t ignored = input->constant_warning_ignored != NULL ? input->constant_warning_ignored[token] : 0;
                warnings[token].flags |= flags;
                warnings[token].enabled |= flags & ~ignored;
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
        const CheckInput* pragma_input = original_pragmas != NULL ? original_pragmas : input;
        uint32_t pragma = 0;
        for (uint32_t token = 0; !pp->fatal && token <= count; token += 1)
        {
            while (!pp->fatal && pragma < pragma_input->fp_pragma_count && pragma_input->fp_pragmas[pragma].token == token)
            {
                const CheckInputFpPragma* record = pragma_input->fp_pragmas + pragma;
                const bool retained = original_pragmas != NULL || repair == NULL || token < check_repair_pragma_end(repair);
                if (retained)
                {
                    check_driver_pragma_report(pp, pragma_input, record, quiet);
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
                const uint32_t source = phase == 0 ? check_diagnostic_input_report_source(input, parser, input->reports + index) : BUSTER_CHECK_NONE;
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
        // A directive in an unfinished expression can lie beyond every safely checked prefix. Its original
        // source spelling and warning policy are independent of that expression and precede the parser failure.
        for (; !pp->fatal && original_pragmas != NULL && pragma < pragma_input->fp_pragma_count; pragma += 1)
        {
            check_driver_pragma_report(pp, pragma_input, pragma_input->fp_pragmas + pragma, quiet);
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

// Why check_program_emit failed, at its token: an unsupported construct names the feature it needs (#145), and the
// others are the compiler's own failures, with what the backend reported.
static void check_driver_emission_failed(const Parser* parser, Preprocessor* pp, const CheckProgramCode* code)
{
    char message[256];
    if (code->status == CHECK_PROGRAM_RESOURCE)
    {
        snprintf(message, sizeof(message), "code output capacity exhausted");
    }
    else if (code->status == CHECK_PROGRAM_UNSUPPORTED)
    {
        snprintf(message, sizeof(message), "unsupported: %s", code->reason != NULL ? code->reason : "this C type or operation");
    }
    else if (code->status == CHECK_PROGRAM_INVALID && code->reason != NULL)
    {
        snprintf(message, sizeof(message), "internal compiler error: the lowered function breaks a rule of Tier 0's (%s) at entry %u", code->reason, code->backend_node);
    }
    else if (code->status == CHECK_PROGRAM_BACKEND && code->reason != NULL)
    {
        snprintf(message, sizeof(message), "%s", code->reason);
    }
    else if (code->status == CHECK_PROGRAM_BACKEND)
    {
        snprintf(message, sizeof(message), "internal compiler error: Tier 0 failed (error %u) at entry %u", (uint32_t)code->backend_error, code->backend_node);
    }
    else
    {
        snprintf(message, sizeof(message), "internal compiler error: the checked program could not be emitted");
    }
    check_driver_diagnose(parser, pp, code->token, message);
}

static bool check_driver_object(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                 Preprocessor* pp, TypeTable* types, const char* source_path, const char* output_path,
                                 bool scalar, bool verify, bool dump_static, Arena* scratch)
{
    Arena output = arena_reserve(UINT64_C(1) << 34);
    Arena bytes = arena_reserve(UINT64_C(1) << 34);
    const uint64_t mark = scratch->length;
    CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
    CheckDataResult emission = { .status = CHECK_DATA_RESOURCE, .failed_token = BUSTER_CHECK_NONE };
    bool valid = output.base != 0 && bytes.base != 0 &&
                 check_program_emit(&code, program, parser, pp->table, data->capacity, scalar, &output, &bytes, scratch, NULL, NULL, verify);
    if (!valid)
    {
        check_driver_emission_failed(parser, pp, &code);
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

// executable_write's passes are a lane group's collectives: an executable is written by a group of one lane.
typedef struct CheckDriverWrite CheckDriverWrite;
struct CheckDriverWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    bool succeeded;
};

static void check_driver_write_main(void* argument)
{
    CheckDriverWrite* write = argument;
    write->succeeded = executable_write(write->link, &write->options, &write->result);
}

// A binding's failure, about the name `name` (empty for none).
static void check_driver_bind_failed(BindError error, const char* name)
{
    if (error == BIND_ERROR_UNDEFINED)
    {
        fprintf(stderr, "error: undefined reference to '%s'\n", name);
    }
    else if (error == BIND_ERROR_DUPLICATE)
    {
        fprintf(stderr, "error: duplicate definition of '%s': the C runtime defines it too\n", name);
    }
    else if (error == BIND_ERROR_SYMBOLS)
    {
        fprintf(stderr, "error: two global symbols are named '%s'\n", name);
    }
    else if (error == BIND_ERROR_DISCARDED)
    {
        fprintf(stderr, "error: a reference to '%s', a symbol of a discarded COMDAT group\n", name);
    }
    else if (error == BIND_ERROR_SNAPSHOT)
    {
        fprintf(stderr, "error: the C runtime's snapshot is damaged\n");
    }
    else
    {
        fprintf(stderr, "fatal error: binding capacity exhausted\n");
    }
}

// Whether the link's name `name` is a symbol of the program's whose declaration a system header wrote: an undefined one
// is then a library's function that the C library doesn't have (libm's sin, linked by -lm).
static bool check_driver_system_declared(const CheckProgram* program, const Parser* parser, const Preprocessor* pp, const Link* link, NameId name)
{
    bool system = false;
    for (uint32_t s = 0; name != 0 && s < program->symbol_count && s + 1 < link->symbols.count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
        if (link->symbols.name[s + 1] == name && declaration < parser->tokens.count)
        {
            system = check_lazy_system(pp, parser->tokens.sources[declaration]);
        }
    }
    return system;
}

// A name of the link's, for a diagnostic, as a string in `buffer`.
static const char* check_driver_link_name(const Link* link, NameId name, char* buffer, uint64_t capacity)
{
    buffer[0] = 0;
    if (name != 0 && name < link->names.count)
    {
        snprintf(buffer, capacity, "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
    }
    return buffer;
}

// The libraries the command line names, -lNAME (or -l NAME), and the directories it adds to their search, -LDIR (or
// -L DIR), each in its order, for the direct path to link (runtime_libraries_import).
typedef struct CheckDriverLibraries CheckDriverLibraries;
struct CheckDriverLibraries
{
    String8* names;
    uint32_t count;
    String8* directories;
    uint32_t directory_count;
};

// Says why the -l libraries didn't import (runtime_libraries_import's error).
static void check_driver_libraries_failed(const RuntimeLibraries* libraries, RuntimeError error)
{
    if (error == RUNTIME_ERROR_MISSING)
    {
        fprintf(stderr, "error: cannot find %s\n", libraries->failed);
    }
    else if (error == RUNTIME_ERROR_ARCHIVE)
    {
        fprintf(stderr, "error: unsupported: static libraries: %s is an archive, which the direct path doesn't link yet (-c and a system linker "
                        "take it)\n", libraries->failed);
    }
    else if (error == RUNTIME_ERROR_FILE)
    {
        fprintf(stderr, "error: can't read %s: %s\n", libraries->failed, strerror(libraries->system_error));
    }
    else if (error == RUNTIME_ERROR_IMPORT)
    {
        fprintf(stderr, "error: %s isn't a shared library or a linker script the direct path reads (import error %u)\n", libraries->failed,
                (uint32_t)libraries->import.error);
    }
    else
    {
        fprintf(stderr, "fatal error: can't reserve memory for the libraries\n");
    }
}

// Links the checked program straight into an executable at `output_path` (#157's direct path, #145's slice): a dynamic
// PIE against the system's C runtime, which comes from its snapshot in the cache directory (imported and written there
// when it is missing or stale) and which binding joins between the program's symbols and its atoms. A binding that finds
// a piece of the snapshot damaged (BIND_ERROR_SNAPSHOT) discards the snapshot and emits once more from a fresh import.
// With `debug` (-g), the executable carries DWARF: a unit of the program's functions with their source rows (#395,
// c_check_debug.c).
static bool check_driver_executable(const CheckProgram* program, const CheckDataPrepared* data, const Parser* parser,
                                     Preprocessor* pp, TypeTable* types, const char* output_path, bool scalar, bool verify, bool debug,
                                     const CheckDriverLibraries* libraries, Arena* scratch)
{
    char cache[PATH_MAX];
    const bool cached = snapshot_directory(cache, sizeof(cache));
    bool valid = false;
    bool again = true;
    for (uint32_t attempt = 0; again && attempt < 2; attempt += 1)
    {
        again = false;
        Runtime runtime;
        const SnapshotResult opened = runtime_open(&runtime, cached ? cache : NULL);
        // The libraries -l names, imported for this runtime, whose version indices theirs follow.
        static RuntimeLibraries named;
        const bool naming = libraries->count != 0 && opened.error == RUNTIME_ERROR_NONE;
        const RuntimeError imported = naming ? runtime_libraries_import(&named, &runtime, libraries->names, libraries->count, libraries->directories,
                                                                        libraries->directory_count) : RUNTIME_ERROR_NONE;
        Arena output = arena_reserve(UINT64_C(1) << 34);
        Arena bytes = arena_reserve(UINT64_C(1) << 34);
        Arena arena = arena_reserve(UINT64_C(1) << 32);
        const uint64_t mark = scratch->length;
        Bind bind = { 0 };
        CheckProgramBinding binding = {
            .runtime = &runtime,
            .bind = &bind,
            .path = scalar ? BIND_PATH_EXACT : BIND_PATH_VECTOR,
            .arena = &arena,
            .data_objects = data->objects,
            .data_count = data->count,
        };
        CheckProgramCode code = { .status = CHECK_PROGRAM_RESOURCE, .token = BUSTER_CHECK_NONE };
        CheckDataResult emission = { .status = CHECK_DATA_RESOURCE, .failed_token = BUSTER_CHECK_NONE };
        CheckProgramRows rows = { 0 };
        CheckDebug dwarf = { 0 };
        char name[256] = { 0 };
        valid = opened.error == RUNTIME_ERROR_NONE && imported == RUNTIME_ERROR_NONE && output.base != 0 && bytes.base != 0 && arena.base != 0;
        if (opened.error != RUNTIME_ERROR_NONE)
        {
            fprintf(stderr, "error: can't import the C runtime%s%s\n", runtime.failed_file[0] != 0 ? ": " : "", runtime.failed_file);
        }
        else if (imported != RUNTIME_ERROR_NONE)
        {
            check_driver_libraries_failed(&named, imported);
        }
        else if (!valid)
        {
            fprintf(stderr, "fatal error: can't reserve linker memory\n");
        }
        if (valid && !check_program_emit(&code, program, parser, pp->table, data->capacity, scalar, &output, &bytes, scratch, &binding, debug ? &rows : NULL, verify))
        {
            valid = false;
            again = bind.error == BIND_ERROR_SNAPSHOT;
            if (bind.error != BIND_ERROR_NONE && !again)
            {
                check_driver_bind_failed(bind.error, binding.failed_name);
            }
            else if (!again)
            {
                check_driver_emission_failed(parser, pp, &code);
            }
        }
        if (valid && debug)
        {
            valid = check_debug_build(&dwarf, program, &rows, parser, pp, scalar, &output, scratch);
            if (!valid)
            {
                fprintf(stderr, "error: can't make the debug information: no memory, or a source name or file name DWARF can't hold\n");
            }
        }
        if (valid)
        {
            valid = check_data_publish(data, types, program->stream, program->object_symbols, program->object_count, &code.link, 0, &bytes, scratch, &emission);
            if (!valid)
            {
                check_driver_diagnose(parser, pp, emission.failed_token, "checked static data could not be emitted");
            }
        }
        if (valid)
        {
            // Binding takes the libraries between bind_begin, which check_program_emit ran, and bind_end.
            bind.libraries = naming ? &named : 0;
            valid = bind_end(&bind, &code.link);
            again = !valid && bind.error == BIND_ERROR_SNAPSHOT;
            if (!valid && !again)
            {
                check_driver_link_name(&code.link, bind.name, name, sizeof(name));
                if (bind.error == BIND_ERROR_UNDEFINED && check_driver_system_declared(program, parser, pp, &code.link, bind.name))
                {
                    fprintf(stderr, "error: undefined reference to '%s', which a system header declares: its library isn't linked (-lm links "
                                    "libm's functions)\n", name);
                }
                else if (bind.error == BIND_ERROR_LIBRARY_UNDEFINED)
                {
                    // A needed library's own reference that nothing defines, as GNU ld words it, with the version it requires.
                    const String8 path = named.paths[bind.library];
                    const ImportReferences* references = &named.libraries[bind.library].references;
                    const String8 version = named.reference_versions[bind.library][bind.reference];
                    fprintf(stderr, "error: %.*s: undefined reference to '%.*s%s%.*s'\n", (int)path.length, path.pointer, (int)references->length[bind.reference],
                            (const char*)references->name[bind.reference], version.length != 0 ? "@" : "", (int)version.length, version.pointer);
                }
                else if (bind.error == BIND_ERROR_LIBRARY_VERSION)
                {
                    // A version a loaded library needs of another that that one doesn't define, as glibc's loader words it: the
                    // named library's path, or libc.so.6's or the loader's soname.
                    const RuntimeVersionNeeds* needs = &named.needs[bind.library];
                    const String8 path = named.paths[bind.library];
                    const String8 provider = bind.provider != UINT32_MAX ? named.paths[bind.provider] : needs->file[bind.need];
                    const String8 version = needs->name[bind.need];
                    fprintf(stderr, "error: %.*s: version '%.*s' not found (required by %.*s)\n", (int)provider.length, provider.pointer, (int)version.length,
                            version.pointer, (int)path.length, path.pointer);
                }
                else
                {
                    check_driver_bind_failed(bind.error, name);
                }
            }
        }
        ExecutableDynamic dynamic = { 0 };
        if (valid)
        {
            const LinkError checked = link_check(&code.link);
            valid = checked == LINK_ERROR_NONE && dynamic_inputs(&dynamic, &runtime, &bind, &arena);
            // An import the code reaches other than through the GOT, by a call or from data, which a shared library's
            // data needs without copy relocations, which the direct path makes none of (#84). Tier 0 reaches every symbol
            // the unit doesn't define through the GOT (check_program_emit's access column), so this is the compiler's
            // error.
            SymbolId direct = 0;
            for (FixupId f = 1; checked == LINK_ERROR_BINDING && direct == 0 && f < code.link.fixups.count; f += 1)
            {
                const SymbolId target = code.link.fixups.target[f];
                const uint8_t kind = code.link.fixups.kind[f] & BUSTER_LINK_FIXUP_KIND;
                if (target != 0 && code.link.symbols.source[target] == BUSTER_LINK_SOURCE_RUNTIME_SHARED && kind != BUSTER_LINK_FIXUP_CALL &&
                    kind != BUSTER_LINK_FIXUP_CALL_GOT && kind != BUSTER_LINK_FIXUP_LOAD_GOT && kind != BUSTER_LINK_FIXUP_GOTPCREL && kind != BUSTER_LINK_FIXUP_ABS64)
                {
                    direct = target;
                }
            }
            if (direct != 0)
            {
                fprintf(stderr, "internal compiler error: '%s', a shared library's symbol, reached directly rather than through the GOT\n",
                        check_driver_link_name(&code.link, code.link.symbols.name[direct], name, sizeof(name)));
            }
            else if (!valid)
            {
                fprintf(stderr, "internal compiler error: the bound link isn't valid (link error %u)\n", (uint32_t)checked);
            }
        }
        if (valid)
        {
            CheckDriverWrite write = {
                .link = &code.link,
                .options = {
                    .path = { .pointer = (char*)output_path, .length = strlen(output_path) },
                    .entry = dynamic_global(&runtime, &bind, "_start"),
                    .dynamic = &dynamic,
                    .debug = debug ? &dwarf.attach : NULL,
                    .exact = scalar,
                },
            };
            valid = lane_run(1, check_driver_write_main, &write, 0) && write.succeeded;
            const ExecutableError error = write.result.error;
            if (!valid && error == EXECUTABLE_ERROR_UNDEFINED && write.result.symbol < code.link.symbols.count)
            {
                fprintf(stderr, "error: undefined reference to '%s'\n",
                        check_driver_link_name(&code.link, code.link.symbols.name[write.result.symbol], name, sizeof(name)));
            }
            else if (!valid && error == EXECUTABLE_ERROR_FILE)
            {
                fprintf(stderr, "error: can't write %s: %s: %s\n", output_path, write.result.call != NULL ? write.result.call : "write",
                        strerror(write.result.system_error));
            }
            else if (!valid)
            {
                fprintf(stderr, "error: can't link %s (executable error %u)\n", output_path, (uint32_t)error);
            }
        }
        if (again && cached)
        {
            snapshot_discard(cache, RUNTIME_KIND_DYNAMIC);
        }
        arena_pop_to(scratch, mark);
        arena_release(&arena);
        arena_release(&bytes);
        arena_release(&output);
        if (naming)
        {
            runtime_libraries_release(&named);
        }
        runtime_release(&runtime);
    }
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

// The default C command with -g, in the unit whose lowering keeps the typed entries' source tokens
// (BUSTER_CHECK_SOURCE_ROWS, main.c), which run_check_command sends such a link to from every other unit.
int run_source_debug_command(int count, char** arguments);

// The semantic driver: `compiler check` (`executable` false) checks, and with -c writes an object; the default C command
// (`executable` true) also links an executable at -o (a.out by default) unless -c or -fsyntax-only says otherwise.
static int run_check_command(int count, char** arguments, bool executable)
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
    // --verify checks every function's records with x86_validate (the c-testsuite harness passes it).
    bool verify = false;
    bool dump = false;
    bool dump_static = false;
    bool emit_object = false;
    bool syntax_only = false;
    bool quiet = false;
    bool lazy_system_headers = false;
    // The direct path's -l libraries and -L directories, which every other mode leaves alone.
    CheckDriverLibraries libraries = {
        .names = arena_push_array(&arena, (uint64_t)count + 1, sizeof(String8), _Alignof(String8)),
        .directories = arena_push_array(&arena, (uint64_t)count + 1, sizeof(String8), _Alignof(String8)),
    };
    bool missing_value = false;
    for (int i = 0; filtered != 0 && libraries.names != 0 && libraries.directories != 0 && i < count; i += 1)
    {
        scalar |= strcmp(arguments[i], "--scalar") == 0;
        quiet |= strcmp(arguments[i], "-w") == 0;
        // An option that takes the next argument as its value (pp_option_takes_value: pp_parse_options' and
        // c_predefines.c's) passes on with it, unread: `-o -lresult` writes -lresult, as clang's driver takes it.
        const bool valued = i + 1 < count && pp_option_takes_value(arguments[i]);
        const bool library = !valued && arguments[i][0] == '-' && arguments[i][1] == 'l';
        const bool directory = !valued && arguments[i][0] == '-' && arguments[i][1] == 'L';
        if (valued)
        {
            // The option and its value, however the value is spelled, for pp_parse_options.
            filtered[filtered_count++] = arguments[i];
            filtered[filtered_count++] = arguments[i + 1];
            i += 1;
        }
        else if (library || directory)
        {
            const char* value = arguments[i][2] != 0 ? arguments[i] + 2 : i + 1 < count ? arguments[++i] : 0;
            const String8 text = {
                .pointer = (char*)value,
                .length = value != 0 ? strlen(value) : 0,
            };
            missing_value |= text.length == 0;
            if (library)
            {
                libraries.names[libraries.count++] = text;
            }
            else
            {
                libraries.directories[libraries.directory_count++] = text;
            }
        }
        else if (strcmp(arguments[i], "--verify") == 0)
        {
            verify = true;
        }
        else if (strcmp(arguments[i], "-flazy-system-headers") == 0)
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
        else if (strcmp(arguments[i], "-fsyntax-only") == 0)
        {
            syntax_only = true;
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
    // The default C command links unless -c, -fsyntax-only or a dump says otherwise.
    const bool link = executable && !emit_object && !syntax_only && !dump && !dump_static;
    PPOptions options;
    const char* path = 0;
    const char* output = 0;
    bool markers = false;
    const bool parsed = filtered != 0 && !missing_value && pp_parse_options((int)filtered_count, filtered, &options, &arena, &path, &output, &markers);
    options.semantic_pragmas = true;
    // The expansion history is recorded only when something asks for it (PPOptions.defer_history): a diagnostic, and the
    // system-header judgment of a unit whose runs can't make it (check_input_system_tokens). --scalar judges by the
    // history, and -flazy-system-headers reads every declaration's, so they record it as the unit runs.
    options.defer_history = !lazy_system_headers && !scalar;
    if (!parsed)
    {
        fprintf(stderr, "usage: compiler [check] [--scalar] [--verify] [--dump-typed] [--dump-static] [-flazy-system-headers] [-fsyntax-only | -c] "
                        "[-o file] [-lNAME] [-LDIR] [preprocessor options] file.c\n");
    }
    else if (!check_input_target_supported(&options))
    {
        fprintf(stderr, "error: checker requires C, signed char, 32-bit wchar_t, x87 long double and source-precision floating evaluation\n");
    }
    else if (!BUSTER_CHECK_SOURCE_ROWS && link && (options.predefines.debug || options.predefines.debug_modules))
    {
        // -g's link, whole, in the unit whose lowering keeps the source rows (its own unit folds this branch away). This
        // call's arenas, which hold only the parsed options, go first: that call reserves its own, and the two calls'
        // together would take twice the address space (an RLIMIT_AS that a compile without -g fits would refuse it).
        arena_release(&output_arena);
        arena_release(&scratch);
        arena_release(&arena);
        result = run_source_debug_command(count, arguments);
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
            CheckInput original_pragmas = { 0 };
            bool original_pragmas_ready = false;
            bool original_pragmas_printed = false;
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
            if (parse_failed)
            {
                original_pragmas_ready = check_diagnostic_original_pragmas(&original_pragmas, &parser, &pp,
                                                                           check_diagnostic_parse_end(&parser), &arena, &scratch);
                if (!original_pragmas_ready)
                {
                    fprintf(stderr, "fatal error: original pragma diagnostic capacity exhausted\n");
                    ready = false;
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
                        const CheckInput* pragma_input = original_pragmas_ready ? &original_pragmas : &input;
                        const uint32_t pragma_end = original_pragmas_ready || !repairing ? BUSTER_CHECK_NONE : check_repair_pragma_end(&repair);
                        promoted |= check_diagnostic_first_pragma_error_mode(pragma_input, pragma_end, &promoted_token, &promoted_source, quiet);
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
                        if (!check_driver_reports(checking, &pp, &input, &stream, repairing ? &repair : NULL,
                                                  original_pragmas_ready ? &original_pragmas : NULL, additional, additional_count,
                                                  excess_tokens, excess_count, quiet, &scratch))
                        {
                            fprintf(stderr, "fatal error: checker diagnostic capacity exhausted\n");
                            ready = false;
                        }
                        original_pragmas_printed = true;
                        if (input_valid && dump && !prefix_only)
                        {
                            check_driver_dump(&input.graph, &stream, &types, &scratch);
                        }
                        result = ready && expression_valid && additional_count == 0 && !repairing && pp.errors == 0 ? 0 : 1;
                        CheckProgram program;
                        CheckDataPrepared data = { 0 };
                        if (result == 0 && (emit_object || link) && !prefix_only && !check_data_prepare(&data, &input, &stream, &initializers, &types, &output_arena, &scratch))
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
                                                                               object_path, scalar, verify, dump_static, &scratch) ? 0 : 1;
                            }
                            else
                            {
                                check_driver_diagnose(checking, &pp, selected.token, "checked definition selection failed");
                                result = 1;
                            }
                        }
                        if (result == 0 && link && !prefix_only)
                        {
                            CheckProgramSelection selected;
                            if (check_program_select(&selected, &program, &data, scalar, &output_arena, &scratch))
                            {
                                result = check_driver_executable(&selected.program, &selected.data, checking, &pp, &types, output != 0 ? output : "a.out", scalar, verify,
                                                                 BUSTER_CHECK_SOURCE_ROWS && (options.predefines.debug || options.predefines.debug_modules), &libraries,
                                                                 &scratch) ? 0 : 1;
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
                else
                {
                    if (!check_driver_reports(checking, &pp, &input, NULL, repairing ? &repair : NULL,
                                              original_pragmas_ready ? &original_pragmas : NULL, NULL, 0, NULL, 0, quiet, &scratch))
                    {
                        fprintf(stderr, "fatal error: checker diagnostic capacity exhausted\n");
                    }
                    original_pragmas_printed = true;
                }
            }
            if (parse_failed)
            {
                for (uint32_t i = 0; original_pragmas_ready && !original_pragmas_printed && !pp.fatal && i < original_pragmas.fp_pragma_count; i += 1)
                {
                    check_driver_pragma_report(&pp, &original_pragmas, original_pragmas.fp_pragmas + i, quiet);
                }
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
