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

// One publication pass's anonymous-tag spellings (#992): a `file:line:col` text by tag index, built on first need
// inside a rendered type. Its scratch rows cannot outlive a rendered event's mark, so the pass rebuilds them
// there whenever a later type needs them again.
typedef struct CheckWarningSpelling CheckWarningSpelling;
struct CheckWarningSpelling
{
    String8* anonymous;
    uint32_t anonymous_count;
    // 0 = unbuilt; 1 = built; 2 = the table holds no unnamed tag at all, so no later type can need the scan.
    uint8_t built;
};

// Whether `type` reaches an unnamed tag through bases and parameters (#992): where `type_print` would otherwise
// print `(unnamed)` without a location. `visited` is a caller bitset over the type indices, so the worklist stays
// linear in what it reaches; a full one says "maybe" rather than walking deeper than its slots.
static bool check_driver_type_anonymous(const TypeTable* types, TypeId type, uint64_t* visited)
{
    TypeId worklist[64];
    uint32_t depth = 0;
    worklist[depth++] = type;
    bool result = false;
    bool full = false;
    while (!result && depth != 0)
    {
        const TypeId current = type_unqualified(type_strip_aligned(types, worklist[--depth]));
        const uint32_t index = type_index(current);
        if (!type_is_valid(types, current) || index >= types->types.count || (visited[index / 64] & ((uint64_t)1 << (index & 63))) != 0)
        {
            continue;
        }
        visited[index / 64] |= (uint64_t)1 << (index & 63);
        const uint8_t kind = type_kind(types, current);
        result = (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM) &&
                 types->types.index[index] < types->tags.count && types->tags.name[types->types.index[index]] == 0;
        const TypeId base = type_is_valid(types, current) ? types->types.base[index] : 0;
        if (!result && base != 0)
        {
            full |= depth == sizeof(worklist) / sizeof(worklist[0]);
            if (depth < sizeof(worklist) / sizeof(worklist[0]))
            {
                worklist[depth++] = base;
            }
        }
        if (!result && kind == BUSTER_TYPE_KIND_FUNCTION)
        {
            uint32_t count = 0;
            const TypeId* parameters = type_parameters(types, current, &count);
            for (uint32_t p = 0; p < count; p += 1)
            {
                full |= depth == sizeof(worklist) / sizeof(worklist[0]);
                if (depth < sizeof(worklist) / sizeof(worklist[0]))
                {
                    worklist[depth++] = parameters[p];
                }
            }
        }
    }
    return result || full;
}

// Each unnamed tag's diagnostic name (#992): the first typedef naming it, as clang's `T`/`T *` and `T2 *`'s
// `T *` aka do, else `(unnamed at file:line:col)` at its own declaration. One scan of the input's tag- and
// typedef-kind declarations on first need in a publication pass; the rows' arena is the caller's scratch.
static void check_driver_spelling_build(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                        CheckWarningSpelling* spell, Arena* scratch)
{
    const uint32_t tag_count = types->tags.count;
    bool any = false;
    for (uint32_t t = 1; t < tag_count; t += 1)
    {
        any |= types->tags.name[t] == 0;
    }
    const uint32_t named_words = tag_count / 64 + 1;
    uint64_t* named = any ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, named_words) : NULL;
    String8* anonymous = named != NULL && input != NULL && input->declaration_kinds != NULL && input->declared_types != NULL ?
        BUSTER_ARENA_PUSH_ARRAY(scratch, String8, tag_count) : NULL;
    if (anonymous != NULL)
    {
        memset(named, 0, (uint64_t)named_words * sizeof(*named));
        memset(anonymous, 0, (uint64_t)tag_count * sizeof(*anonymous));
        const uint32_t count = input->token_count < parser->tokens.count ? input->token_count : parser->tokens.count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint8_t binding = input->declaration_kinds[i];
            if (binding == C_BINDING_TAG_KIND || binding == C_BINDING_TYPEDEF)
            {
                const TypeId declared = type_unqualified(type_strip_aligned(types, input->declared_types[i]));
                const uint32_t index = type_is_valid(types, declared) ? type_index(declared) : types->types.count;
                if (index < types->types.count)
                {
                    const uint8_t kind = type_kind(types, declared);
                    const uint32_t tag = types->types.index[index];
                    if ((kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM) &&
                        tag < tag_count && types->tags.name[tag] == 0)
                    {
                        if (binding == C_BINDING_TYPEDEF)
                        {
                            // The typedef is the anonymous record's canonical name; its first one wins.
                            if ((named[tag / 64] & ((uint64_t)1 << (tag & 63))) == 0)
                            {
                                named[tag / 64] |= (uint64_t)1 << (tag & 63);
                                const uint32_t name = parser->tokens.names[i];
                                if (name < pp->table->count)
                                {
                                    anonymous[tag] = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
                                }
                            }
                        }
                        else if (anonymous[tag].length == 0)
                        {
                            const uint32_t source = parser->tokens.sources[i];
                            const uint32_t location = source < pp->output.count ? pp->output.locations[source] : 0;
                            const char* file = NULL;
                            uint32_t file_length = 0;
                            const uint32_t line = pp_presumed(pp, location, &file, &file_length);
                            const uint32_t range = pp_find_range(pp, location);
                            uint32_t column = 1;
                            if (range != BUSTER_PP_NONE)
                            {
                                (void)pp_line_of(pp, range, location, &column);
                            }
                            const char* text = check_warning_text(scratch, "(unnamed at %.*s:%u:%u)", (int)file_length, file, line, column);
                            if (text != NULL)
                            {
                                anonymous[tag] = (String8){ .pointer = (char*)text, .length = (uint64_t)strlen(text) };
                            }
                        }
                    }
                }
            }
        }
        spell->anonymous = anonymous;
        spell->anonymous_count = tag_count;
        spell->built = 1;
    }
    else
    {
        spell->built = 2;
    }
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

// A warning's type spelling (#992): `spelling` is the operand's typedef specifier's declaration, so the base's
// specifier prints the typedef's name and clang's `(aka '...')` shows the type without that sugar; the sentinel
// BUSTER_CHECK_NONE prints `__size_t` for a sizeof/alignof operand's own type. The anonymous map prints an
// unnamed tag's typedef name or its `(unnamed at file:line:col)`.
static String8 check_driver_warning_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                         CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling)
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
            TypeNames names = { .bytes = bytes, .lengths = lengths, .count = count };
            if (spell->built == 0)
            {
                // The scan pays for itself only where the printed type can reach an unnamed tag (#992).
                bool any = false;
                for (uint32_t t = 1; t < types->tags.count; t += 1)
                {
                    any |= types->tags.name[t] == 0;
                }
                if (any)
                {
                    const uint32_t words = types->types.count / 64 + 1;
                    uint64_t* visited = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, words);
                    if (visited != NULL)
                    {
                        memset(visited, 0, (uint64_t)words * sizeof(*visited));
                        if (check_driver_type_anonymous(types, type, visited))
                        {
                            check_driver_spelling_build(parser, pp, input, types, spell, scratch);
                        }
                    }
                    else
                    {
                        spell->built = 2;
                    }
                }
                else
                {
                    spell->built = 2;
                }
            }
            if (spell->built == 1)
            {
                names.anonymous = spell->anonymous;
                names.anonymous_count = spell->anonymous_count;
            }
            TypeId alias_type = 0;
            String8 alias_name = { 0 };
            if (spelling == BUSTER_CHECK_NONE)
            {
                alias_type = type_unqualified(type_strip_aligned(types, type));
                alias_name = BUSTER_S8("__size_t");
            }
            else if (spelling != 0 && input != NULL && input->declared_types != NULL && spelling < input->token_count)
            {
                const TypeId declared = input->declared_types[spelling];
                const uint32_t name = spelling < parser->tokens.count ? parser->tokens.names[spelling] : count;
                if (type_is_valid(types, declared) && name < count)
                {
                    alias_type = declared;
                    alias_name = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
                }
            }
            const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
            if (alias_type != 0)
            {
                names.alias_types = &alias_type;
                names.alias_names = &alias_name;
                names.alias_count = 1;
            }
            const String8 sugared = type_print(types, scratch, type, (String8){ 0 }, &names, options);
            String8 canonical = { 0 };
            if (names.alias_count != 0)
            {
                // clang's `(aka '...')`: the same type with the typedef sugar expanded — an anonymous record's
                // typedef stays, since it is that record's own name.
                TypeNames expand = names;
                expand.alias_count = 0;
                canonical = type_print(types, scratch, type, (String8){ 0 }, &expand, options);
            }
            // The returned text carries its own quotes: clang's `(aka '...')` sits outside the type's, so the
            // messages print `%.*s` without quoting it themselves.
            result = sugared;
            if (sugared.length != 0 && canonical.length != 0 &&
                (canonical.length != sugared.length || memcmp(canonical.pointer, sugared.pointer, canonical.length) != 0))
            {
                const char* combined = check_warning_text(scratch, "'%.*s' (aka '%.*s')", (int)sugared.length, sugared.pointer,
                                                          (int)canonical.length, canonical.pointer);
                result = combined != NULL ? (String8){ .pointer = (char*)combined, .length = (uint64_t)strlen(combined) } : (String8){ 0 };
            }
            else if (sugared.length != 0)
            {
                const char* quoted = check_warning_text(scratch, "'%.*s'", (int)sugared.length, sugared.pointer);
                result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = (uint64_t)strlen(quoted) } : (String8){ 0 };
            }
        }
    }
    return result;
}

static const char* check_driver_warning_message(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                CheckWarningSpelling* spell, const CheckWarningEvent* event, Arena* scratch)
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
            // payload.data is the destination operand's typedef-specifier declaration, or NONE for `__size_t` (#992).
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            result = type.length != 0 ? check_warning_text(scratch, "expression which evaluates to zero treated as a null pointer constant of type %.*s",
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
            // payload.name/payload.data are the operands' own typedef-specifier declarations, or NONE (#992).
            const String8 first = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->name);
            const String8 second = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->second_type, payload->data);
            result = first.length != 0 && second.length != 0 ? check_warning_text(scratch, "ordered comparison between pointer and integer (%.*s and %.*s)",
                (int)first.length, first.pointer, (int)second.length, second.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            result = type.length != 0 ? check_warning_text(scratch, "field '%.*s' with variable sized type %.*s not at the end of a struct or class is a GNU extension",
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
    // One pass's anonymous-tag spellings; its rows are per-event scratch, rebuilt when a later type needs them.
    CheckWarningSpelling spell = { 0 };
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
                const char* message = check_driver_warning_message(parser, pp, input, types, &spell, event, scratch);
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
                // The spelling rows' arena was inside the mark's span; a later type rebuilds them (#992).
                spell.built = 0;
                spell.anonymous = NULL;
                spell.anonymous_count = 0;
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
