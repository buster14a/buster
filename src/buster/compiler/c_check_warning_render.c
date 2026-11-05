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

// Clang names a promoted warning's flag as "[-Werror,-Wflag]" while the diagnostic is an ordinary error, and
// "[-Wflag]" once it is fatal.
static const char* check_driver_warning_error_prefix(bool promoted)
{
    const char* result = promoted ? "-Werror," : "";
    return result;
}

static CheckWarningSeverity check_driver_warning_severity(Preprocessor* pp, const CheckInput* input, const CheckWarningEvent* event)
{
    const CheckWarningCommandLine command = check_warning_command_line(&pp->options, event->id,
        (event->flags & BUSTER_CHECK_WARNING_DEFAULT_IGNORED) != 0, false);
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

// A string token's complete spelling in scratch, including its quotes and encoding prefix.
static String8 check_warning_string_spelling(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    String8 result = { 0 };
    const uint32_t source = token < parser->tokens.count ? parser->tokens.sources[token] : UINT32_MAX;
    const PPTokens* output = &pp->output;
    if (source < output->count)
    {
        const uint64_t capacity = (uint64_t)output->values[source] - output->locations[source] + 1;
        uint8_t* buffer = capacity < (UINT64_C(1) << 20) ? arena_push(scratch, capacity, 1) : NULL;
        const uint8_t* bytes = NULL;
        const uint32_t length = buffer != NULL ? pp_spelling(pp, output->kinds[source], output->values[source], output->locations[source], buffer, &bytes) : 0;
        result = (String8){ (char*)bytes, length };
    }
    return result;
}

// The text of the string literal as written, between its quotes (past an encoding prefix); empty for none.
static String8 check_warning_string_text(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
    uint64_t open = 0;
    while (open < spelling.length && spelling.pointer[open] != '"')
    {
        open += 1;
    }
    const bool quoted = open + 1 < spelling.length && spelling.pointer[spelling.length - 1] == '"';
    return quoted ? (String8){ spelling.pointer + open + 1, spelling.length - open - 2 } : (String8){ 0 };
}

// Attribute handlers classify decoded bytes, so their diagnostic excerpts must use the same bytes. An escape's
// source width is unrelated to the offsets retained in CHECK_WARNING_EVENT_TARGET_UNSUPPORTED's payload.
static String8 check_warning_string_value(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    const String8 spelling = check_warning_string_spelling(parser, pp, token, scratch);
    const CheckStringPart part = { .bytes = (const uint8_t*)spelling.pointer, .length = spelling.length };
    const CheckStringLiteral decoded = spelling.length != 0 ? check_literal_strings(&part, 1, pp->options.language.standard >= 2023, scratch) :
                                                             (CheckStringLiteral){ 0 };
    return decoded.valid && decoded.byte_count != 0 ? (String8){ (char*)decoded.bytes, decoded.byte_count - 1 } : (String8){ 0 };
}

// One publication pass's name views for type_print (#992): the identifier table's spelling columns and, where
// the unit has unnamed tags, a `file:line:col` or typedef-name text by tag index. Both are built once before the
// pass's events take their marks, so per-event release cannot invalidate them; `anonymous` stays empty where
// the unit's tags are all named or the input keeps no declaration columns.
typedef struct CheckWarningSpelling CheckWarningSpelling;
struct CheckWarningSpelling
{
    const uint8_t** bytes;
    uint32_t* lengths;
    uint32_t count;
    String8* anonymous;
    uint32_t anonymous_count;
    bool built;
};

// Each unnamed tag's diagnostic name (#992): the first typedef naming it, as clang's `T`/`T *` and `T2 *`'s
// `T *` aka do, else `(unnamed at file:line:col)` where the declaration was written — the expansion's own
// location for a macro body's tag, as clang's expansion-point answer is (#992). One scan of the input's tag-
// and typedef-kind declarations per publication pass; the rows sit under the pass's event marks in the
// caller's scratch, so they survive until the pass ends. Returns false on an allocation failure, which the
// caller reports like the renderer's other allocations rather than dropping the spelling.
static bool check_driver_spelling_build(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                        CheckWarningSpelling* spell, Arena* scratch)
{
    bool valid = true;
    if (input != NULL && input->declaration_kinds != NULL && input->declared_types != NULL)
    {
        const uint32_t tag_count = types->tags.count;
        const uint32_t named_words = tag_count / 64 + 1;
        uint64_t* named = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, named_words);
        String8* anonymous = named != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, tag_count) : NULL;
        valid = anonymous != NULL;
        if (anonymous != NULL)
        {
            // The history is already ensured for the diagnostic the pass is about to publish; an expanded
            // tag's location is the expansion's then, not the definition's (#992).
            const bool history = pp_history_ensure(pp);
            memset(named, 0, (uint64_t)named_words * sizeof(*named));
            memset(anonymous, 0, (uint64_t)tag_count * sizeof(*anonymous));
            const uint32_t count = input->token_count < parser->tokens.count ? input->token_count : parser->tokens.count;
            // The brackets open before each token, innermost last: whether each is a struct's or union's body.
            bool* opens = BUSTER_ARENA_PUSH_ARRAY(scratch, bool, (uint64_t)count + 1);
            uint32_t open_count = 0;
            valid = opens != NULL;
            for (uint32_t i = 0; valid && i < count; i += 1)
            {
                const uint8_t bracket = i != 0 ? parser->tokens.kinds[i - 1] : PARSE_KIND_END;
                if (bracket == PARSE_KIND_L_PAREN || bracket == PARSE_KIND_L_SQUARE || bracket == PARSE_KIND_L_BRACE)
                {
                    opens[open_count] = bracket == PARSE_KIND_L_BRACE && parser->tree.roles[i - 1] == PARSE_ROLE_OPEN_STRUCT;
                    open_count += 1;
                }
                open_count -= (bracket == PARSE_KIND_R_PAREN || bracket == PARSE_KIND_R_SQUARE || bracket == PARSE_KIND_R_BRACE) && open_count != 0;
                // C_BINDING_TYPEDEF is also every other token's zero (a use's, a member's): a typedef declares an object of
                // its own.
                const uint8_t binding = input->declaration_kinds[i];
                const bool declared = binding != C_BINDING_TYPEDEF || (input->objects != NULL && input->objects[i] == i + 1);
                if ((binding == C_BINDING_TAG_KIND || binding == C_BINDING_TYPEDEF) && declared)
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
                                const uint32_t origin = history ? pp_history_at(pp, source) : 0;
                                const uint32_t location = source < pp->output.count ? pp_expansion_location(pp, source, origin) : 0;
                                const char* file = NULL;
                                uint32_t file_length = 0;
                                const uint32_t line = pp_presumed(pp, location, &file, &file_length);
                                const uint32_t range = pp_find_range(pp, location);
                                uint32_t column = 1;
                                if (range != BUSTER_PP_NONE)
                                {
                                    (void)pp_line_of(pp, range, location, &column);
                                }
                                // clang's anonymous member's record (a definition with no declarator in a record's
                                // body) is "anonymous", any other "unnamed".
                                const uint32_t body = check_input_skip_attributes(parser, i + 1, parser->tokens.count);
                                const uint32_t closer = body < parser->tokens.count && parser->tokens.kinds[body] == PARSE_KIND_L_BRACE ? parser->tree.parents[body] : parser->tokens.count;
                                const uint32_t after = closer < parser->tokens.count && closer > body ? check_input_skip_attributes(parser, closer + 1, parser->tokens.count) :
                                                       parser->tokens.count;
                                const bool member = after < parser->tokens.count && parser->tokens.kinds[after] == PARSE_KIND_SEMI && open_count != 0 &&
                                                    opens[open_count - 1];
                                const char* text = check_warning_text(scratch, "(%s at %.*s:%u:%u)", member ? "anonymous" : "unnamed", (int)file_length, file, line, column);
                                valid = text != NULL;
                                if (valid)
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
        }
    }
    return valid;
}

// One pass's spelling rows, built on its first rendered type (#992): a pass that renders none never scans the
// token columns. A built pass keeps its rows until it ends, above every per-event mark.
static bool check_driver_spelling_ready(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                        CheckWarningSpelling* spell, Arena* scratch)
{
    if (!spell->built)
    {
        spell->built = true;
        spell->count = pp->table->count;
        spell->bytes = BUSTER_ARENA_PUSH_ARRAY(scratch, const uint8_t*, spell->count);
        spell->lengths = spell->bytes != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, spell->count) : NULL;
        bool valid = spell->count == 0 || spell->lengths != NULL;
        for (uint32_t i = 0; valid && i < spell->count; i += 1)
        {
            spell->bytes[i] = intern_bytes(pp->table, i);
            spell->lengths[i] = intern_length(pp->table, i);
        }
        bool unnamed = false;
        for (uint32_t tag = 1; valid && types != NULL && tag < types->tags.count; tag += 1)
        {
            unnamed |= types->tags.name[tag] == 0;
        }
        if (valid && unnamed)
        {
            valid = check_driver_spelling_build(parser, pp, input, types, spell, scratch);
        }
        if (!valid)
        {
            spell->bytes = NULL;
        }
    }
    return spell->count == 0 || spell->bytes != NULL;
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
        // The pass's columns live in `spell`, built on the pass's first rendered type below its marks (#992).
        if (check_driver_spelling_ready(parser, pp, input, types, spell, scratch))
        {
            TypeNames names = {
                .bytes = spell->bytes,
                .lengths = spell->lengths,
                .count = spell->count,
                .anonymous = spell->anonymous,
                .anonymous_count = spell->anonymous_count,
            };
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
                const uint32_t name = spelling < parser->tokens.count ? parser->tokens.names[spelling] : spell->count;
                if (type_is_valid(types, declared) && name < spell->count)
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
                // Without an a.k.a., clang describes a vector type: "'T' (vector of N 'E' values)" (ConvertTypeToDiagnosticString).
                const TypeId vector = type_strip_aligned(types, type);
                const uint8_t kind = type_kind(types, vector);
                const bool described = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR;
                const uint64_t count = described ? types->types.extent[type_index(vector)] : 0;
                TypeNames plain = names;
                plain.alias_count = 0;
                const String8 element = described ? type_print(types, scratch, types->types.base[type_index(vector)], (String8){ 0 }, &plain, options) : (String8){ 0 };
                const char* quoted = described ? check_warning_text(scratch, "'%.*s' (vector of %llu '%.*s' %s)", (int)sugared.length, sugared.pointer,
                                                                    (unsigned long long)count, (int)element.length, element.pointer, count > 1 ? "values" : "value") :
                                                 check_warning_text(scratch, "'%.*s'", (int)sugared.length, sugared.pointer);
                result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = (uint64_t)strlen(quoted) } : (String8){ 0 };
            }
        }
    }
    return result;
}

// An attribute's spelling without a keyword's surrounding underscores (__packed__ as packed), as clang normalizes one.
static String8 check_warning_attribute_normalized(String8 name)
{
    const bool surrounded = name.length >= 4 && name.pointer[0] == '_' && name.pointer[1] == '_' && name.pointer[name.length - 1] == '_' &&
                            name.pointer[name.length - 2] == '_';
    return surrounded ? (String8){ name.pointer + 2, name.length - 4 } : name;
}

static bool check_warning_text_equal(String8 text, const char* expected)
{
    const uint64_t length = strlen(expected);
    return text.length == length && memcmp(text.pointer, expected, length) == 0;
}

// A C23 attribute's scope as clang normalizes it (canonicalizeScopeName): __gnu__'s as gnu, _Clang's as clang, another as
// written.
static String8 check_warning_attribute_scope(String8 scope)
{
    const bool gnu = check_warning_text_equal(scope, "__gnu__");
    const bool clang = check_warning_text_equal(scope, "_Clang");
    return gnu ? (String8){ "gnu", 3 } : clang ? (String8){ "clang", 5 } : scope;
}

// An attribute's name as clang normalizes it (normalizeAttrName): a GNU one's, and a C23 one's of no scope, gnu's or
// clang's, without a keyword's surrounding underscores; another scope's as written.
static String8 check_warning_attribute_name(String8 name, String8 scope, bool standard)
{
    const bool strip = !standard || scope.length == 0 || check_warning_text_equal(scope, "gnu") || check_warning_text_equal(scope, "clang");
    return strip ? check_warning_attribute_normalized(name) : name;
}

// The edit distance of `typo` to `candidate`, insertions, deletions and replacements counted (llvm's
// StringRef::edit_distance), by rows over the candidate's bytes: a candidate is one of clang's spellings, of at most 63.
static uint32_t check_warning_edit_distance(String8 typo, String8 candidate)
{
    uint32_t row[64];
    const uint32_t width = candidate.length < 63 ? (uint32_t)candidate.length : 63;
    for (uint32_t j = 0; j <= width; j += 1)
    {
        row[j] = j;
    }
    for (uint64_t i = 1; i <= typo.length; i += 1)
    {
        uint32_t diagonal = row[0];
        row[0] = (uint32_t)i;
        for (uint32_t j = 1; j <= width; j += 1)
        {
            const uint32_t above = row[j];
            const uint32_t replace = diagonal + (typo.pointer[i - 1] != candidate.pointer[j - 1] ? 1u : 0u);
            const uint32_t insert = row[j - 1] + 1;
            const uint32_t remove = above + 1;
            const uint32_t least = insert < remove ? insert : remove;
            row[j] = replace < least ? replace : least;
            diagonal = above;
        }
    }
    return row[width];
}

// clang's correction of a misspelled name (SimpleTypoCorrection): of `list`'s `count` names in order, the first of the fewest
// edits from `typo`, within (its length + 2) / 3 edits, a candidate skipped whose length differs from the typo's by more than
// a third of the typo's; its index, or `count` for none.
static uint32_t check_warning_attribute_closest(const CheckInputClangAttribute* list, uint32_t count, String8 typo)
{
    const uint32_t most = (uint32_t)((typo.length + 2) / 3);
    uint32_t best = most + 1;
    uint32_t result = count;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const String8 candidate = { (char*)list[i].name, list[i].length };
        const uint64_t difference = candidate.length > typo.length ? candidate.length - typo.length : typo.length - candidate.length;
        const bool skipped = candidate.length == 0 || (difference != 0 && typo.length / difference < 3);
        const uint32_t distance = skipped ? best : check_warning_edit_distance(typo, candidate);
        result = distance < best ? i : result;
        best = distance < best ? distance : best;
    }
    return best <= most ? result : count;
}

// clang's suggestion for an attribute it doesn't know (Sema::DiagnoseUnknownAttribute): a C23 attribute's scope that isn't
// one of its spellings' (`*scope`, normalized) corrected to the closest of them, then its name, normalized for that scope, that
// isn't one of its spellings' corrected to the closest, where clang knows that one in the attribute's syntax and scope
// (check_input_attribute_clang_knows). `*scope` and `*name` are the suggestion's; whether there is one.
static bool check_warning_attribute_suggestion(String8 written, bool standard, String8* scope, String8* name)
{
    bool corrected = false;
    if (standard && scope->length != 0 &&
        !check_input_attribute_listed(check_input_clang_scopes, BUSTER_ARRAY_LENGTH(check_input_clang_scopes), *scope))
    {
        const uint32_t index = check_warning_attribute_closest(check_input_clang_scopes, BUSTER_ARRAY_LENGTH(check_input_clang_scopes), *scope);
        corrected = index < BUSTER_ARRAY_LENGTH(check_input_clang_scopes);
        *scope = corrected ? (String8){ (char*)check_input_clang_scopes[index].name, check_input_clang_scopes[index].length } : *scope;
    }
    *name = check_warning_attribute_name(written, *scope, standard);
    if (!check_input_attribute_listed(check_input_clang_spellings, BUSTER_ARRAY_LENGTH(check_input_clang_spellings), *name))
    {
        const uint32_t index = check_warning_attribute_closest(check_input_clang_spellings, BUSTER_ARRAY_LENGTH(check_input_clang_spellings), *name);
        const String8 candidate = index < BUSTER_ARRAY_LENGTH(check_input_clang_spellings) ?
                                  (String8){ (char*)check_input_clang_spellings[index].name, check_input_clang_spellings[index].length } : (String8){ "", 0 };
        // clang's hasAttribute normalizes the candidate as it does the attribute's name (`__device__` is device).
        const bool known = candidate.length != 0 &&
                           check_input_attribute_clang_knows(*scope, check_warning_attribute_name(candidate, *scope, standard), scope->length != 0, standard);
        *name = known ? candidate : *name;
        corrected |= known;
    }
    return corrected;
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
        case CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE:
        {
            // clang spells a GNU attribute's name as written, and a C23 attribute's normalized: its scope's __gnu__ as gnu and
            // _Clang as clang, and its name without a keyword's surrounding underscores where its scope is none, gnu or clang
            // (another scope's as written). Its suggestion (check_warning_attribute_suggestion) follows.
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            String8 suggestion_scope = scope;
            String8 suggestion_name = attribute;
            const bool suggested = check_warning_attribute_suggestion(name, standard, &suggestion_scope, &suggestion_name);
            char suggestion[160] = { 0 };
            if (suggested)
            {
                snprintf(suggestion, sizeof(suggestion), "; did you mean '%.*s%s%.*s'?", (int)suggestion_scope.length, suggestion_scope.pointer,
                         suggestion_scope.length != 0 ? "::" : "", (int)suggestion_name.length, suggestion_name.pointer);
            }
            result = check_warning_text(scratch, "unknown attribute '%.*s%s%.*s' ignored%s", (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "",
                                        (int)attribute.length, attribute.pointer, suggestion);
            break;
        }
        case CHECK_WARNING_EVENT_ATTRIBUTE_SUBJECT:
        {
            // clang's "'X' attribute only applies to SUBJECTS", the attribute named as the unknown one's warning names it and
            // its subjects its row's (check_input_attribute_subjects).
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            const CheckInputSubjectRow* row = check_input_subject_row(scope, check_warning_attribute_normalized(name), scope_name != 0, standard);
            result = row != NULL ? check_warning_text(scratch, "'%.*s%s%.*s' attribute only applies to %s", (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "",
                                                      (int)attribute.length, attribute.pointer, row->message) :
                                   NULL;
            break;
        }
        case CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            result = type.length != 0 ? check_warning_text(scratch, "'%.*s' only applies to function types; type here is %.*s", (int)name.length, name.pointer,
                                                           (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_NONSTRING_TYPE:
        {
            // clang's handleNonStringAttr's warning: the attribute named as the subject warning names it (its scope's token
            // in the payload's, when it has one) and the declaration's type as clang spells it.
            const bool standard = payload->token != BUSTER_CHECK_WARNING_NO_SOURCE && payload->token < parser->tokens.count;
            const String8 scope = standard ? check_warning_attribute_scope(check_driver_warning_name(pp, parser->tokens.names[payload->token])) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            result = type.length != 0 ? check_warning_text(scratch, "'%.*s%s%.*s' attribute only applies to fields or variables of character array type; type is %.*s",
                                                           (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer,
                                                           (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_TRANSPARENT_UNION:
        {
            // handleTransparentUnionAttr's warnings (check_input_transparent_union_warning).
            const uint32_t which = payload->data & 0xffu;
            const uint32_t bits = payload->data >> 8;
            const bool size = which == BUSTER_CHECK_TRANSPARENT_SIZE;
            if (which == BUSTER_CHECK_TRANSPARENT_INCOMPLETE)
            {
                result = "transparent_union attribute can only be applied to a union definition; attribute ignored";
            }
            else if (which == BUSTER_CHECK_TRANSPARENT_EMPTY)
            {
                result = "transparent union definition must contain at least one field; transparent_union attribute ignored";
            }
            else if (which == BUSTER_CHECK_TRANSPARENT_FLOAT || which == BUSTER_CHECK_TRANSPARENT_VECTOR)
            {
                const uint32_t spelling = payload->token != BUSTER_CHECK_WARNING_NO_SOURCE ? payload->token : 0;
                const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, spelling);
                result = type.length != 0 ? check_warning_text(scratch, "first field of a transparent union cannot have %s type %.*s; transparent_union attribute ignored",
                                                               which == BUSTER_CHECK_TRANSPARENT_FLOAT ? "floating point" : "vector", (int)type.length, type.pointer) : NULL;
            }
            else if (size || which == BUSTER_CHECK_TRANSPARENT_ALIGNMENT)
            {
                result = check_warning_text(scratch, "%s of field '%.*s' (%u bits) does not match the %s of the first field in transparent union; transparent_union attribute ignored",
                                            size ? "size" : "alignment", (int)name.length, name.pointer, bits, size ? "size" : "alignment");
            }
            break;
        }
        case CHECK_WARNING_EVENT_NONNULL_POINTERS:
        case CHECK_WARNING_EVENT_NONNULL_NO_POINTERS:
        case CHECK_WARNING_EVENT_NONNULL_PARAMETER_ARGUMENTS:
        {
            // Named as the subject warning names an attribute (CHECK_WARNING_EVENT_ATTRIBUTE_SUBJECT's).
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            const char* ending = event->kind == CHECK_WARNING_EVENT_NONNULL_POINTERS ? "attribute only applies to pointer arguments" :
                                 event->kind == CHECK_WARNING_EVENT_NONNULL_NO_POINTERS ? "attribute applied to function with no pointer arguments" :
                                 "attribute when used on parameters takes no arguments";
            result = check_warning_text(scratch, "'%.*s%s%.*s' %s", (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length,
                                        attribute.pointer, ending);
            break;
        }
        case CHECK_WARNING_EVENT_THREAD_CAPABILITY:
        case CHECK_WARNING_EVENT_THREAD_ARGUMENT:
        case CHECK_WARNING_EVENT_THREAD_METHOD:
        case CHECK_WARNING_EVENT_THREAD_POINTER:
        case CHECK_WARNING_EVENT_THREAD_CONTEXT:
        case CHECK_WARNING_EVENT_THREAD_FUNCTION_POINTER:
        case CHECK_WARNING_EVENT_THREAD_PARAMETER:
        {
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            const bool typed = event->kind == CHECK_WARNING_EVENT_THREAD_CAPABILITY || event->kind == CHECK_WARNING_EVENT_THREAD_POINTER;
            const String8 type = typed ? check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->token) : (String8){ "", 0 };
            const char* suffix = event->kind == CHECK_WARNING_EVENT_THREAD_CAPABILITY ?
                                 "attribute requires arguments whose type is annotated with 'capability' attribute; type here is " :
                                 event->kind == CHECK_WARNING_EVENT_THREAD_POINTER ? "only applies to pointer types; type here is " :
                                 event->kind == CHECK_WARNING_EVENT_THREAD_METHOD ?
                                 "attribute without capability arguments can only be applied to non-static methods of a class" :
                                 event->kind == CHECK_WARNING_EVENT_THREAD_PARAMETER ?
                                 "attribute applies to function parameters only if their type is a reference to a 'scoped_lockable'-annotated type" :
                                 event->kind == CHECK_WARNING_EVENT_THREAD_FUNCTION_POINTER ?
                                 (payload->token != 0 ? "attribute on a field requires the field to be of function pointer type" :
                                                       "attribute on a variable requires the variable to be of function pointer type") :
                                 "attribute can only be applied in a context annotated with 'capability' attribute";
            result = event->kind == CHECK_WARNING_EVENT_THREAD_ARGUMENT ?
                     check_warning_text(scratch, "ignoring '%.*s%s%.*s' attribute because its argument is invalid", (int)scope.length, scope.pointer,
                                        scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer) :
                     check_warning_text(scratch, "'%.*s%s%.*s' %s%.*s", (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "",
                                        (int)attribute.length, attribute.pointer, suffix, (int)type.length, type.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_TAG_ATTRIBUTE_PLACE:
        {
            static const char* const keywords[] = { "struct", "union", "enum" };
            result = check_warning_text(scratch, "attribute '%.*s' is ignored, place it after \"%s\" to apply attribute to type declaration", (int)name.length,
                                        name.pointer, keywords[payload->data < 3 ? payload->data : 0]);
            break;
        }
        case CHECK_WARNING_EVENT_TAG_ATTRIBUTE_DEFINED:
        {
            result = "attribute declaration must precede definition";
            break;
        }
        case CHECK_WARNING_EVENT_UNAVAILABLE_USE:
        {
            const String8 message = check_warning_string_text(parser, pp, payload->token, scratch);
            result = message.length != 0 ? check_warning_text(scratch, "'%.*s' is unavailable: %.*s", (int)name.length, name.pointer, (int)message.length, message.pointer) :
                                          check_warning_text(scratch, "'%.*s' is unavailable", (int)name.length, name.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_DEPRECATED_USE:
        {
            // The message is the attribute's string as written, without its quotes or prefix. An anonymous tag's name is its
            // type's spelling, with its own quotes (BUSTER_CHECK_DEPRECATED_TYPE_NAME).
            const String8 message = check_warning_string_text(parser, pp, payload->token, scratch);
            const bool typed = payload->data == BUSTER_CHECK_DEPRECATED_TYPE_NAME;
            const char* quotes = typed ? "" : "'";
            const String8 used = typed ? check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, 0) : name;
            result = used.length == 0 ? NULL :
                     message.length != 0 ? check_warning_text(scratch, "%s%.*s%s is deprecated: %.*s", quotes, (int)used.length, used.pointer, quotes, (int)message.length,
                                                              message.pointer) :
                                          check_warning_text(scratch, "%s%.*s%s is deprecated", quotes, (int)used.length, used.pointer, quotes);
            break;
        }
        case CHECK_WARNING_EVENT_ALIGNOF_EXPRESSION:
        {
            result = check_warning_text(scratch, "'%.*s' applied to an expression is a GNU extension", (int)name.length, name.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_CFSTRING_TRUNCATED:
        {
            result = "input conversion stopped due to an input byte that does not belong to the input codeset UTF-8";
            break;
        }
        case CHECK_WARNING_EVENT_UNGUARDED_AVAILABILITY:
        {
            result = "__builtin_available does not guard availability here; use if (__builtin_available) instead";
            break;
        }
        case CHECK_WARNING_EVENT_VERSION_SEPARATORS:
        {
            result = "use same version number separators '_' or '.'; as in 'major[.minor[.subminor]]'";
            break;
        }
        case CHECK_WARNING_EVENT_COUNTED_BY_ELEMENT:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, 0);
            result = type.length != 0 ? check_warning_text(scratch, "'%s' should not be applied to an array with element of unknown size because %.*s is a struct type "
                                                           "with a flexible array member. This will be an error in a future compiler version",
                                                           check_input_counted_names[payload->name < 4 ? payload->name + 1 : 1], (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_TYPE_NAME_ATTRIBUTE:
        {
            result = check_warning_text(scratch, "'%.*s' attribute ignored when parsing type", (int)name.length, name.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_TYPE_POSITION:
        case CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE:
        case CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED:
        case CHECK_WARNING_EVENT_ATTRIBUTE_IGNORED:
        case CHECK_WARNING_EVENT_RETURN_POINTERS:
        case CHECK_WARNING_EVENT_RETURN_POINTERS_REFERENCES:
        case CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED:
        case CHECK_WARNING_EVENT_GCC_VARIADIC:
        case CHECK_WARNING_EVENT_TARGET_UNSUPPORTED:
        case CHECK_WARNING_EVENT_UNKNOWN_SANITIZER:
        case CHECK_WARNING_EVENT_SANITIZER_GLOBAL:
        case CHECK_WARNING_EVENT_FUNCTIONS_METHODS:
        case CHECK_WARNING_EVENT_OWNERSHIP_RETURN_OBJECT:
        case CHECK_WARNING_EVENT_OWNERSHIP_RETURN_POINTER:
        case CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_CF:
        case CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_OS:
        case CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS:
        case CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS_PARAMETERS:
        case CHECK_WARNING_EVENT_OWNERSHIP_RETAINED:
        case CHECK_WARNING_EVENT_CONSUMED_OBJECT:
        case CHECK_WARNING_EVENT_CONSUMED_POINTER:
        case CHECK_WARNING_EVENT_NOMERGE_POINTER:
        case CHECK_WARNING_EVENT_OVERFLOW_BEHAVIOR:
        case CHECK_WARNING_EVENT_TYPE_ON_DECLARATION:
        case CHECK_WARNING_EVENT_REGISTER_COUNT_DEPRECATED:
        case CHECK_WARNING_EVENT_IBOUTLET:
        case CHECK_WARNING_EVENT_MIG_ROUTINE:
        case CHECK_WARNING_EVENT_SPAN_ONLY:
        case CHECK_WARNING_EVENT_NSOBJECT_TYPEDEF:
        case CHECK_WARNING_EVENT_INDEPENDENT_TYPEDEF:
        case CHECK_WARNING_EVENT_INDEPENDENT_POINTER:
        case CHECK_WARNING_EVENT_CAPTURE_DEPRECATED:
        case CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED:
        case CHECK_WARNING_EVENT_ASSUME_SIDE_EFFECTS:
        case CHECK_WARNING_EVENT_DIFFERENT_ARGUMENTS:
        case CHECK_WARNING_EVENT_NO_CALLS:
        case CHECK_WARNING_EVENT_NOINLINE_STATEMENT:
        case CHECK_WARNING_EVENT_ALWAYS_INLINE_STATEMENT:
        case CHECK_WARNING_EVENT_PRECEDENCE_ALWAYS_INLINE:
        case CHECK_WARNING_EVENT_PRECEDENCE_FLATTEN:
        case CHECK_WARNING_EVENT_PRECEDENCE_NOINLINE:
        {
            // Named as CHECK_WARNING_EVENT_UNKNOWN_ATTRIBUTE's are (target's data is its warning's own).
            const bool target = event->kind == CHECK_WARNING_EVENT_TARGET_UNSUPPORTED;
            const bool standard = !target && (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = target ? 0 : payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            // An unsupported argument's text (#147): a string literal's, with the literals after it, or an identifier's
            // without a keyword's underscores.
            char argument[256] = { 0 };
            uint32_t length = 0;
            const uint32_t count = parser->tokens.count;
            const bool argued = event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED || event->kind == CHECK_WARNING_EVENT_TARGET_UNSUPPORTED ||
                                event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED || event->kind == CHECK_WARNING_EVENT_CAPTURE_DEPRECATED ||
                                event->kind == CHECK_WARNING_EVENT_UNKNOWN_SANITIZER || event->kind == CHECK_WARNING_EVENT_SANITIZER_GLOBAL;
            for (uint32_t token = payload->token; argued && token < count &&
                 (parser->tokens.kinds[token] == PARSE_KIND_STRING || (token == payload->token && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER));
                 token += 1)
            {
                const bool string = parser->tokens.kinds[token] == PARSE_KIND_STRING;
                const String8 text = string ? (target ? check_warning_string_value(parser, pp, token, scratch) : check_warning_string_text(parser, pp, token, scratch)) :
                                              check_warning_attribute_normalized(check_driver_warning_name(pp, parser->tokens.names[token]));
                const uint32_t copied = text.length < sizeof(argument) - 1 - length ? (uint32_t)text.length : (uint32_t)(sizeof(argument) - 1 - length);
                memcpy(argument + length, text.pointer, copied);
                length += copied;
                token = string ? token : count;
            }
            // target's warning's piece of its string (check_input_target_warning's data).
            const uint32_t offset = (payload->data >> 4) & 0x3fffu;
            const uint32_t piece = offset <= length ? ((payload->data >> 18) & 0x3fffu) : 0u;
            const uint32_t shown = offset <= length ? (piece < length - offset ? piece : length - offset) : 0u;
            static const char* const target_cases[] = { "unsupported", "duplicate", "unknown", "unknown" };
            static const char* const target_places[] = { "", " CPU", " tune CPU", "" };
            // ns_returns_retained's type check names it by a string, and consumed's by their names alone (#147).
            const String8 plain = check_warning_attribute_normalized(name);
            const String8 variable = event->kind == CHECK_WARNING_EVENT_NOMERGE_POINTER && payload->token < count ?
                                     check_driver_warning_name(pp, parser->tokens.names[payload->token]) : (String8){ "", 0 };
            result = event->kind == CHECK_WARNING_EVENT_OWNERSHIP_RETAINED ? "'ns_returns_retained' attribute only applies to functions that return an Objective-C object" :
                event->kind == CHECK_WARNING_EVENT_NOMERGE_POINTER ?
                check_warning_text(scratch, "'%.*s%s%.*s' attribute is ignored because '%.*s' is not a function pointer", (int)scope.length, scope.pointer,
                                   scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, (int)variable.length, variable.pointer) :
                event->kind == CHECK_WARNING_EVENT_REGISTER_COUNT_DEPRECATED ?
                check_warning_text(scratch, "the '%.*s%s%.*s' attribute is deprecated; use 'amdgpu_waves_per_eu' instead", (int)scope.length, scope.pointer,
                                   scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer) :
                event->kind == CHECK_WARNING_EVENT_TYPE_ON_DECLARATION ?
                check_warning_text(scratch, "applying attribute '%.*s%s%.*s' to a declaration is deprecated; apply it to the type instead", (int)scope.length,
                                   scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer) :
                event->kind == CHECK_WARNING_EVENT_OVERFLOW_BEHAVIOR ?
                check_warning_text(scratch, "'%.*s%s%.*s' attribute is ignored because it is not enabled; pass -Xclang -fexperimental-overflow-behavior-types",
                                   (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer) :
                event->kind == CHECK_WARNING_EVENT_CONSUMED_OBJECT || event->kind == CHECK_WARNING_EVENT_CONSUMED_POINTER ?
                check_warning_text(scratch, "%.*s attribute only applies to %s parameters", (int)plain.length, plain.pointer,
                                   event->kind == CHECK_WARNING_EVENT_CONSUMED_OBJECT ? "Objective-C object" : "pointer") :
                event->kind == CHECK_WARNING_EVENT_TARGET_UNSUPPORTED ?
                check_warning_text(scratch, "%s%s '%.*s' in the 'target' attribute string; 'target' attribute ignored", target_cases[payload->data & 3u],
                                   target_places[(payload->data >> 2) & 3u], (int)shown, argument + (offset <= length ? offset : 0)) :
                event->kind == CHECK_WARNING_EVENT_UNKNOWN_SANITIZER ? check_warning_text(scratch, "unknown sanitizer '%s' ignored", argument) :
                event->kind == CHECK_WARNING_EVENT_SANITIZER_GLOBAL ?
                check_warning_text(scratch, "'%.*s%s%.*s' attribute argument '%s' not supported on a global variable", (int)scope.length, scope.pointer,
                                   scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, argument) :
                event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED || event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED ?
                check_warning_text(scratch, event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED ? "'%.*s%s%.*s' attribute argument not supported: %s" :
                                                                                                     "'%.*s%s%.*s' attribute argument not supported: '%s'",
                                   (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, argument) :
                event->kind == CHECK_WARNING_EVENT_CAPTURE_DEPRECATED ?
                check_warning_text(scratch, "'lifetime_capture_by(%s)' is deprecated; use 'lifetime_capture_by_%s' instead", argument, argument) :
                event->kind == CHECK_WARNING_EVENT_MIG_ROUTINE ? "'mig_server_routine' attribute only applies to routines that return a kern_return_t" :
                event->kind == CHECK_WARNING_EVENT_NOINLINE_STATEMENT ?
                "attribute is ignored on this statement as it only applies to functions; use '[[clang::noinline]]' on statements" :
                event->kind == CHECK_WARNING_EVENT_ALWAYS_INLINE_STATEMENT ?
                "attribute is ignored on this statement as it only applies to functions; use '[[clang::always_inline]]' on statements" :
                event->kind == CHECK_WARNING_EVENT_ASSUME_SIDE_EFFECTS ? "assumption is ignored because it contains (potential) side-effects" :
                event->kind == CHECK_WARNING_EVENT_NSOBJECT_TYPEDEF ? "'NSObject' attribute may be put on a typedef only; attribute is ignored" :
                event->kind == CHECK_WARNING_EVENT_INDEPENDENT_TYPEDEF ? "'objc_independent_class' attribute may be put on a typedef only; attribute is ignored" :
                event->kind == CHECK_WARNING_EVENT_INDEPENDENT_POINTER ?
                "'objc_independent_class' attribute may be put on Objective-C object pointer type only; attribute is ignored" :
                check_warning_text(scratch,
                                        event->kind == CHECK_WARNING_EVENT_TYPE_POSITION ? "attribute '%.*s%s%.*s' ignored, because it cannot be applied to a type" :
                                        event->kind == CHECK_WARNING_EVENT_RETURN_POINTERS ? "'%.*s%s%.*s' attribute only applies to return values that are pointers" :
                                        event->kind == CHECK_WARNING_EVENT_GCC_VARIADIC ? "GCC requires a function with the '%.*s%s%.*s' attribute to be variadic" :
                                        event->kind == CHECK_WARNING_EVENT_FUNCTIONS_METHODS || event->kind == CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS ?
                                            "'%.*s%s%.*s' attribute only applies to functions and methods" :
                                        event->kind == CHECK_WARNING_EVENT_OWNERSHIP_FUNCTIONS_PARAMETERS ? "'%.*s%s%.*s' attribute only applies to functions, methods, and parameters" :
                                        event->kind == CHECK_WARNING_EVENT_OWNERSHIP_RETURN_OBJECT ?
                                            "'%.*s%s%.*s' attribute only applies to functions that return an Objective-C object" :
                                        event->kind == CHECK_WARNING_EVENT_OWNERSHIP_RETURN_POINTER ? "'%.*s%s%.*s' attribute only applies to functions that return a pointer" :
                                        event->kind == CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_CF ? "'%.*s%s%.*s' attribute only applies to pointer-to-CF-pointer parameters" :
                                        event->kind == CHECK_WARNING_EVENT_OWNERSHIP_PARAMETER_OS ?
                                            "'%.*s%s%.*s' attribute only applies to pointer/reference-to-OSObject-pointer parameters" :
                                        event->kind == CHECK_WARNING_EVENT_RETURN_POINTERS_REFERENCES ?
                                            "'%.*s%s%.*s' attribute only applies to return values that are pointers or references" :
                                        event->kind == CHECK_WARNING_EVENT_GCC_TYPE_ATTRIBUTE ? "GCC does not allow the '%.*s%s%.*s' attribute to be written on a type" :
                                        event->kind == CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED ? "'%.*s%s%.*s' calling convention is not supported for this target" :
                                        event->kind == CHECK_WARNING_EVENT_IBOUTLET ? "'%.*s%s%.*s' attribute can only be applied to instance variables or properties" :
                                        event->kind == CHECK_WARNING_EVENT_SPAN_ONLY ? "'%.*s%s%.*s' attribute only applies to functions that return span-like structures" :
                                        event->kind == CHECK_WARNING_EVENT_DIFFERENT_ARGUMENTS ? "attribute '%.*s%s%.*s' is already applied with different arguments" :
                                        event->kind == CHECK_WARNING_EVENT_PRECEDENCE_ALWAYS_INLINE ?
                                            "statement attribute '%.*s%s%.*s' has higher precedence than function attribute 'always_inline'" :
                                        event->kind == CHECK_WARNING_EVENT_PRECEDENCE_FLATTEN ?
                                            "statement attribute '%.*s%s%.*s' has higher precedence than function attribute 'flatten'" :
                                        event->kind == CHECK_WARNING_EVENT_PRECEDENCE_NOINLINE ?
                                            "statement attribute '%.*s%s%.*s' has higher precedence than function attribute 'noinline'" :
                                        event->kind == CHECK_WARNING_EVENT_NO_CALLS ?
                                            "'%.*s%s%.*s' attribute is ignored because there exists no call expression inside the statement" :
                                        "'%.*s%s%.*s' attribute ignored",
                                        (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_ATTRIBUTE_VARIABLES_FUNCTIONS:
        {
            // clang's handleInternalLinkageAttr's subjects (#147), the attribute named as the unknown one's warning names it.
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            result = check_warning_text(scratch, "'%.*s%s%.*s' attribute only applies to variables and functions", (int)scope.length, scope.pointer,
                                        scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_ATTRIBUTE_DEFINITION:
            // clang's diagnostic takes the attribute's name as a string: 'weak_import' however it is spelled.
            result = "'weak_import' attribute cannot be specified on a definition";
            break;
        case CHECK_WARNING_EVENT_INTERNAL_LINKAGE_LOCAL:
            result = "'internal_linkage' attribute on a non-static local variable is ignored";
            break;
        case CHECK_WARNING_EVENT_NOCF_CHECK_IGNORED:
            result = "'nocf_check' attribute ignored; use -fcf-protection to enable the attribute";
            break;
        case CHECK_WARNING_EVENT_EXTERN_INITIALIZER:
            result = "'extern' variable has an initializer";
            break;
        case CHECK_WARNING_EVENT_VECTOR_MODE:
            result = "specifying vector types with the 'mode' attribute is deprecated; use the 'vector_size' attribute instead";
            break;
        case CHECK_WARNING_EVENT_GCC_ATTRIBUTE_POSITION:
            result = check_warning_text(scratch, "GCC does not allow '%.*s' attribute in this position on a function definition", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE:
            result = "'__clang__' is a predefined macro name, not an attribute scope specifier; did you mean '_Clang' instead?";
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

static const char* check_driver_warning_note(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                             CheckWarningSpelling* spell, const CheckWarningNote* note, Arena* scratch)
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
        case CHECK_WARNING_NOTE_CONFLICTING_ATTRIBUTE: result = "conflicting attribute is here"; break;
        case CHECK_WARNING_NOTE_DEPRECATED_HERE:
        {
            // An anonymous tag's name is its type's spelling, which carries its own quotes (BUSTER_CHECK_DEPRECATED_TYPE_NAME).
            if (note->payload.data == BUSTER_CHECK_DEPRECATED_TYPE_NAME)
            {
                const String8 spelled = check_driver_warning_type(parser, pp, input, types, spell, scratch, note->payload.first_type, 0);
                result = spelled.length != 0 ? check_warning_text(scratch, "%.*s has been explicitly marked deprecated here", (int)spelled.length, spelled.pointer) : NULL;
            }
            else
            {
                const String8 noted = check_driver_warning_name(pp, note->payload.name);
                result = check_warning_text(scratch, "'%.*s' has been explicitly marked deprecated here", (int)noted.length, noted.pointer);
            }
            break;
        }
        case CHECK_WARNING_NOTE_UNAVAILABLE_HERE:
            result = check_warning_text(scratch, "'%.*s' has been explicitly marked unavailable here", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_SPAN_REASON:
        {
            static const char* const places[] = { "", "1st", "2nd" };
            const uint32_t reason = note->payload.data & 0xffu;
            const uint32_t number = note->payload.data >> 8;
            result = reason == 1 ? "returned type is incomplete" : reason == 2 ? "returned type is not a struct type" :
                     reason == 3 ? check_warning_text(scratch, "returned struct has %u fields, expected 2", number) :
                     reason == 4 ? check_warning_text(scratch, "%s field is expected to be an integer", places[number < 3 ? number : 0]) :
                     "returned struct fields are not a supported combination for a span-like type (expected pointer/integer or pointer/pointer)";
            break;
        }
        case CHECK_WARNING_NOTE_TRANSPARENT_FIRST_FIELD:
            result = check_warning_text(scratch, "%s of first field is %u bits", (note->payload.data & 0xffu) == BUSTER_CHECK_TRANSPARENT_SIZE ? "size" : "alignment",
                                        note->payload.data >> 8);
            break;
        default: break;
    }
    return result;
}

static bool check_driver_warning_events_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                            const CheckWarningEvents* view, const CheckRepair* repair, uint32_t publication_end, Arena* scratch)
{
    bool valid = view != NULL && (view->count == 0 || view->events != NULL) && (view->note_count == 0 || view->notes != NULL);
    // A pass with no event publishes nothing and builds nothing (#992): an empty view returns before any
    // allocation, so a clean unit pays neither the name columns nor a token scan.
    if (!valid || view->count == 0)
    {
        return valid;
    }
    // Only these facts render a type (#992, and #147's function-type attributes); a pass of typeless messages, pragmas or
    // notes needs no spelling rows at all.
    bool renders = false;
    for (uint32_t i = 0; !renders && i < view->count; i += 1)
    {
        const CheckWarningEvent* event = view->events + i;
        renders = event->kind != CHECK_WARNING_EVENT_MESSAGE &&
                  (event->token < parser->tokens.count || (event->kind == CHECK_WARNING_EVENT_PRAGMA && event->token == parser->tokens.count)) &&
                  (repair == NULL || check_repair_warning_token(repair, event->token)) &&
                  (event->kind == CHECK_WARNING_EVENT_NON_LITERAL_NULL || event->kind == CHECK_WARNING_EVENT_ORDERED_POINTER_INTEGER ||
                   event->kind == CHECK_WARNING_EVENT_VARIABLE_SIZED_MEMBER || event->kind == CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE ||
                   event->kind == CHECK_WARNING_EVENT_NONSTRING_TYPE || event->kind == CHECK_WARNING_EVENT_TRANSPARENT_UNION ||
                   event->kind == CHECK_WARNING_EVENT_COUNTED_BY_ELEMENT ||
                   event->kind == CHECK_WARNING_EVENT_THREAD_CAPABILITY || event->kind == CHECK_WARNING_EVENT_THREAD_POINTER ||
                   (event->kind == CHECK_WARNING_EVENT_DEPRECATED_USE && event->note_count != 0 && event->note_first < view->note_count &&
                    view->notes[event->note_first].payload.data == BUSTER_CHECK_DEPRECATED_TYPE_NAME));
    }
    // One pass's name columns and anonymous-tag spellings (#992), built once below the events' marks when some
    // event renders a type: every event's mark then sits above them, so per-event release keeps them, and each
    // rendered type pays nothing for what the pass already scanned. A unit with no unnamed tag skips the
    // spelling rows entirely.
    const uint64_t spelling_mark = scratch->length;
    CheckWarningSpelling spell = { 0 };
    if (renders)
    {
        valid = check_driver_spelling_ready(parser, pp, input, types, &spell, scratch);
    }
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
                    const char* text = located ? check_driver_warning_note(parser, pp, input, types, &spell, note, scratch) : NULL;
                    valid = text != NULL;
                    if (valid)
                    {
                        notes[n] = (PPOutputDiagnosticNote){ .token = note->source != BUSTER_CHECK_WARNING_NO_SOURCE ? note->source : parser->tokens.sources[note->token], .message = text };
                    }
                }
                const char* option = check_warning_descriptions[event->id].option;
                const char* prefix = check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR);
                const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", prefix, option) : prefix[0] != 0 ? " [-Werror]" : "";
                valid = valid && suffix != NULL;
                if (valid)
                {
                    const uint32_t source = event->source != BUSTER_CHECK_WARNING_NO_SOURCE ? event->source : parser->tokens.sources[event->token];
                    const uint32_t boundary = event->completion_kind == CHECK_WARNING_COMPLETE_TU_END ? pp->output.count + 1 :
                        check_driver_publication_position(parser, pp, event->completion + (event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER ? 1u : 0u), publication_end);
                    const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                    if (event->kind == CHECK_WARNING_EVENT_PRAGMA)
                    {
                        pp_diagnose_output_phase(pp, disposition, source, event->payload.token, PP_DIAGNOSTIC_LEXICAL, "%s%s", message, suffix);
                    }
                    else if ((event->kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE || event->kind == CHECK_WARNING_EVENT_UNUSED_PARAMETER) &&
                             event->completion_kind != CHECK_WARNING_COMPLETE_TU_END)
                    {
                        // Scope-pop warnings: clang pops a scope's unused declarations before replaying the
                        // body's deferred diagnostics at that boundary. At the translation unit's end the
                        // scope rule no longer applies — every unused warning shares one boundary, and a
                        // lexical phase would order file-scope objects ahead of earlier declarations.
                        pp_diagnose_output_notes_phase(pp, disposition, source, boundary, PP_DIAGNOSTIC_LEXICAL, notes, event->note_count, "%s%s", message, suffix);
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
    arena_pop_to(scratch, spelling_mark);
    return valid;
}

// Whole-unit callers have no retained original lookahead beyond their parser. Prefix/repair driver routes use
// the explicit entry point above so a completed source group cannot drift behind later preprocessing events.
static bool check_driver_warning_events(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                        const CheckWarningEvents* view, const CheckRepair* repair, Arena* scratch)
{
    return check_driver_warning_events_at(parser, pp, input, types, view, repair, pp->output.count, scratch);
}
