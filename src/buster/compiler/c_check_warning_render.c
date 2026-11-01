// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Sparse semantic facts to atomic diagnostic groups. The located token governs source policy and system-header
// visibility; the independent completion governs ordering. The collector copies all temporary text and notes.

static const char* check_warning_text(Arena* arena, const char* format, ...) __attribute__((format(printf, 2, 3)));
static const char* check_warning_text(Arena* arena, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    va_list sizing;
    va_copy(sizing, arguments);
    const int length = vsnprintf(NULL, 0, format, sizing);
    va_end(sizing);
    char* result = length >= 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, char, (uint64_t)length + 1) : NULL;
    if (result != NULL)
    {
        vsnprintf(result, (uint64_t)length + 1, format, arguments);
    }
    va_end(arguments);
    return result;
}

static CheckWarningSeverity check_driver_warning_severity(Preprocessor* pp, const CheckInput* input, const CheckWarningEvent* event)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, event->id,
        (event->flags & BUSTER_CHECK_WARNING_DEFAULT_IGNORED) != 0);
    CheckWarningSeverity severity = command.quiet || (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) != 0 ? CHECK_WARNING_IGNORE :
        check_diagnostic_severity_id_from(input, event->token, event->source, event->id, command.severity);
    if (severity != CHECK_WARNING_IGNORE && !command.system_headers)
    {
        bool system = check_graph_system_token(&input->graph, event->token);
        if (event->kind == CHECK_WARNING_EVENT_PRAGMA)
        {
            pp_history_ensure(pp);
            system = pp_system_level_at(pp, pp_expansion_location(pp, event->source, pp_history_at(pp, event->source))) != BUSTER_PP_USER_FILE;
        }
        severity = system ? CHECK_WARNING_IGNORE : severity;
    }
    return severity;
}

static String8 check_driver_warning_name(const Preprocessor* pp, uint32_t name)
{
    const String8 result = name < pp->table->count ? (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) } :
                                                                  (String8){ 0 };
    return result;
}

static bool check_driver_warning_promoted(Preprocessor* pp, const CheckInput* input, const CheckWarningEvents* view, const CheckRepair* repair)
{
    bool promoted = false;
    for (uint32_t i = 0; !promoted && i < view->count; i += 1)
    {
        const CheckWarningEvent* event = view->events + i;
        promoted = event->kind != CHECK_WARNING_EVENT_MESSAGE && (repair == NULL || check_repair_warning_token(repair, event->token)) &&
                   check_driver_warning_severity(pp, input, event) >= CHECK_WARNING_ERROR;
    }
    return promoted;
}

static String8 check_driver_warning_type(const Preprocessor* pp, const TypeTable* types, Arena* scratch, TypeId type)
{
    String8 result = { 0 };
    if (type != 0 && type_is_valid(types, type))
    {
        // Only diagnostic rendering needs these spelling arrays; clean checking retains no identifier-sized view.
        const uint32_t count = pp->table->count;
        const uint8_t** bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, count);
        uint32_t* lengths = bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : NULL;
        if (lengths != NULL)
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                bytes[i] = intern_bytes(pp->table, i);
                lengths[i] = intern_length(pp->table, i);
            }
            const TypeNames names = { .bytes = bytes, .lengths = lengths, .count = count };
            result = type_print(types, scratch, type, (String8){ 0 }, &names, pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0);
        }
    }
    return result;
}

static const char* check_driver_warning_message(const Preprocessor* pp, const TypeTable* types, const CheckWarningEvent* event, Arena* scratch)
{
    const CheckWarningPayload* payload = &event->payload;
    const String8 name = check_driver_warning_name(pp, payload->name);
    const char* result = event->id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[event->id].message : NULL;
    switch (event->kind)
    {
        case CHECK_WARNING_EVENT_GNU_INLINE_WITHOUT_INLINE:
            result = "'gnu_inline' attribute requires function to be marked 'inline', attribute ignored";
            break;
        case CHECK_WARNING_EVENT_GNU_ARRAY_FOLD:
            result = "variable length array folded to constant array as an extension";
            break;
        case CHECK_WARNING_EVENT_GNU_ICE_FOLD:
            result = "expression is not an integer constant expression; folding it to a constant is a GNU extension";
            break;
        case CHECK_WARNING_EVENT_NON_LITERAL_NULL:
        {
            const String8 type = check_driver_warning_type(pp, types, scratch, payload->first_type);
            result = type.length != 0 ? check_warning_text(scratch, "expression which evaluates to zero treated as a null pointer constant of type '%.*s'",
                (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_LITERAL_OVERFLOW:
        case CHECK_WARNING_EVENT_LITERAL_UNDERFLOW:
        {
            const bool large = event->kind == CHECK_WARNING_EVENT_LITERAL_OVERFLOW;
            const TypeId type = type_unqualified(payload->first_type);
            const char* name = type == BUSTER_TYPE_FLOAT ? "float" : type == BUSTER_TYPE_DOUBLE ? "double" :
                               type == BUSTER_TYPE_LONG_DOUBLE ? "long double" : type == BUSTER_TYPE_FLOAT16 ? "_Float16" : NULL;
            const char* limit = type == BUSTER_TYPE_FLOAT ? (large ? "3.40282347E+38" : "1.40129846E-45") :
                                type == BUSTER_TYPE_DOUBLE ? (large ? "1.7976931348623157E+308" : "4.9406564584124654E-324") :
                                type == BUSTER_TYPE_LONG_DOUBLE ? (large ? "1.18973149535723176502E+4932" : "3.64519953188247460253E-4951") :
                                type == BUSTER_TYPE_FLOAT16 ? (large ? "65504" : "5.9605E-8") : NULL;
            result = name != NULL ? check_warning_text(scratch, "magnitude of floating-point constant too %s for type '%s'; %s is %s",
                                                        large ? "large" : "small", name, large ? "maximum" : "minimum", limit) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER:
        {
            const String8 first = check_driver_warning_type(pp, types, scratch, payload->first_type);
            const String8 second = check_driver_warning_type(pp, types, scratch, payload->second_type);
            result = first.length != 0 && second.length != 0 ? check_warning_text(scratch, "ordered comparison between pointer and integer ('%.*s' and '%.*s')",
                (int)first.length, first.pointer, (int)second.length, second.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER:
        {
            const String8 type = check_driver_warning_type(pp, types, scratch, payload->first_type);
            result = type.length != 0 ? check_warning_text(scratch, "field '%.*s' with variable sized type '%.*s' not at the end of a struct or class is a GNU extension",
                (int)name.length, name.pointer, (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_LIBRARY_REDECLARATION:
            result = check_warning_text(scratch, "incompatible redeclaration of library function '%.*s'", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_BUILTIN_HEADER:
            result = check_warning_text(scratch, "declaration of built-in function '%.*s' requires inclusion of its header", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_UNDEFINED_INTERNAL:
            result = check_warning_text(scratch, "function '%.*s' has internal linkage but is not defined", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES:
            result = payload->data != 0 ? "initializer partially overrides prior initialization of this subobject" :
                                        "initializer overrides prior initialization of this subobject";
            break;
        case CHECK_WARNING_EVENT_TYPEDEF_NAMELESS:
            result = "typedef requires a name";
            break;
        case CHECK_WARNING_EVENT_STANDALONE_SPECIFIER:
            result = name.length != 0 ? check_warning_text(scratch, "'%.*s' ignored on this declaration", (int)name.length, name.pointer) : NULL;
            break;
        case CHECK_WARNING_EVENT_UNUSED_VARIABLE:
            result = check_warning_text(scratch, "unused variable '%.*s'", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_UNUSED_FUNCTION:
            result = check_warning_text(scratch, "unused function '%.*s'", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_UNUSED_PARAMETER:
            result = check_warning_text(scratch, "unused parameter '%.*s'", (int)name.length, name.pointer);
            break;
        default: break;
    }
    return result;
}

static const char* check_driver_warning_note(const Preprocessor* pp, const CheckWarningNote* note, Arena* scratch)
{
    const String8 name = check_driver_warning_name(pp, note->payload.name);
    const char* result = NULL;
    switch (note->kind)
    {
        case CHECK_WARNING_NOTE_PRIOR_INITIALIZATION:
            result = note->payload.data != 0 ? "previous initialization with side effects is here (side effects will not occur at run time)" :
                                              "previous initialization is here";
            break;
        case CHECK_WARNING_NOTE_INLINE_LINKAGE:
            result = check_warning_text(scratch, "use 'static' to give inline function '%.*s' internal linkage", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_USED_HERE: result = "used here"; break;
        case CHECK_WARNING_NOTE_PREVIOUS_DEFINITION: result = "previous definition is here"; break;
        default: break;
    }
    return result;
}

static bool check_driver_warning_events_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                            const CheckWarningEvents* view, const CheckRepair* repair, uint32_t publication_end, Arena* scratch)
{
    bool valid = view != NULL && (view->count == 0 || view->events != NULL) && (view->note_count == 0 || view->notes != NULL);
    for (uint32_t i = 0; valid && i < view->count && !pp->fatal; i += 1)
    {
        const CheckWarningEvent* event = view->events + i;
        const bool retained = event->kind != CHECK_WARNING_EVENT_MESSAGE &&
                              (event->token < parser->tokens.count || (event->kind == CHECK_WARNING_EVENT_PRAGMA && event->token == parser->tokens.count)) &&
                              (repair == NULL || check_repair_warning_token(repair, event->token));
        if (retained)
        {
            valid = event->id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) && event->note_first <= view->note_count &&
                    event->note_count <= view->note_count - event->note_first &&
                    event->kind < CHECK_WARNING_EVENT_KIND_COUNT && (event->flags & ~(BUSTER_CHECK_WARNING_DEFAULT_IGNORED | BUSTER_CHECK_WARNING_SUPPRESSED)) == 0 &&
                    (event->source == BUSTER_CHECK_WARNING_NO_SOURCE || event->source < pp->output.count) &&
                    (event->completion_kind == CHECK_WARNING_COMPLETE_TU_END ||
                     (event->completion_kind == CHECK_WARNING_COMPLETE_AT_TOKEN ? event->completion <= parser->tokens.count :
                      event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER && event->completion < parser->tokens.count));
            const CheckWarningSeverity severity = valid ? check_driver_warning_severity(pp, input, event) : CHECK_WARNING_IGNORE;
            if (valid && severity != CHECK_WARNING_IGNORE)
            {
                const uint64_t mark = scratch->length;
                const char* message = check_driver_warning_message(pp, types, event, scratch);
                PPOutputDiagnosticNote* notes = event->note_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, PPOutputDiagnosticNote, event->note_count) : NULL;
                valid = message != NULL && (event->note_count == 0 || notes != NULL);
                for (uint32_t n = 0; valid && n < event->note_count; n += 1)
                {
                    const CheckWarningNote* note = view->notes + event->note_first + n;
                    const bool located = note->token < parser->tokens.count && (note->source == BUSTER_CHECK_WARNING_NO_SOURCE || note->source < pp->output.count);
                    const char* text = located ? check_driver_warning_note(pp, note, scratch) : NULL;
                    valid = text != NULL;
                    if (valid)
                    {
                        notes[n] = (PPOutputDiagnosticNote){ .token = note->source != BUSTER_CHECK_WARNING_NO_SOURCE ? note->source : parser->tokens.sources[note->token], .message = text };
                    }
                }
                const char* option = check_warning_descriptions[event->id].option;
                const bool error = severity >= CHECK_WARNING_ERROR;
                const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", error ? "-Werror," : "", option) : error ? " [-Werror]" : "";
                valid = valid && suffix != NULL;
                if (valid)
                {
                    const uint32_t source = event->source != BUSTER_CHECK_WARNING_NO_SOURCE ? event->source : parser->tokens.sources[event->token];
                    const uint32_t boundary = event->completion_kind == CHECK_WARNING_COMPLETE_TU_END ? pp->output.count + 1 :
                        check_driver_publication_position(parser, pp, event->completion + (event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER ? 1u : 0u), publication_end);
                    const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL : error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                    if (event->kind == CHECK_WARNING_EVENT_PRAGMA)
                    {
                        pp_diagnose_output_phase(pp, disposition, source, event->payload.token, PP_DIAGNOSTIC_LEXICAL, "%s%s", message, suffix);
                    }
                    else
                    {
                        pp_diagnose_output_notes_at(pp, disposition, source, boundary, notes, event->note_count, "%s%s", message, suffix);
                    }
                    valid = !pp->diagnostic_collection_failed;
                }
                arena_pop_to(scratch, mark);
            }
        }
    }
    return valid;
}

// Whole-unit callers have no retained original lookahead beyond their parser. Prefix/repair driver routes use
// the explicit entry point above so a completed source group cannot drift behind later preprocessing events.
static bool check_driver_warning_events(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                        const CheckWarningEvents* view, const CheckRepair* repair, Arena* scratch)
{
    return check_driver_warning_events_at(parser, pp, input, types, view, repair, pp->output.count, scratch);
}
