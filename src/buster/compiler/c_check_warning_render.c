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

// A grouped diagnose_if's warning group (#1447, CHECK_WARNING_EVENT_DIAGNOSE_IF's payload.data), its index in
// pp_features_warning, or UINT32_MAX.
static uint32_t check_driver_warning_group(const CheckWarningEvent* event)
{
    const uint32_t group = event->kind == CHECK_WARNING_EVENT_DIAGNOSE_IF ? event->payload.data & ~BUSTER_CHECK_WARNING_GROUP_ERROR : 0;
    return group != 0 ? group - 1 : UINT32_MAX;
}

static CheckWarningSeverity check_driver_warning_severity(Preprocessor* pp, const CheckInput* input, const CheckWarningEvent* event)
{
    const uint32_t group = check_driver_warning_group(event);
    bool errors = false;
    const CheckWarningCommandLine command = group != UINT32_MAX ?
        check_warning_group_command_line(&pp->options, group, (event->payload.data & BUSTER_CHECK_WARNING_GROUP_ERROR) != 0, &errors) :
        check_warning_command_line(&pp->options, event->id, (event->flags & BUSTER_CHECK_WARNING_DEFAULT_IGNORED) != 0, false);
    CheckWarningSeverity severity = CHECK_WARNING_IGNORE;
    if (group != UINT32_MAX && (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) == 0)
    {
        // clang's custom diagnostic of that group: its pragma, then -w on all but an error by default that is still one,
        // -Werror on a warning, and -Wfatal-errors.
        severity = check_diagnostic_group_severity_from(input, event->token, event->source, group, command.severity);
        const bool default_error = (event->payload.data & BUSTER_CHECK_WARNING_GROUP_ERROR) != 0;
        severity = command.quiet && (!default_error || severity == CHECK_WARNING_WARNING) ? CHECK_WARNING_IGNORE : severity;
        severity = severity == CHECK_WARNING_WARNING && errors ? CHECK_WARNING_ERROR : severity;
        severity = severity == CHECK_WARNING_ERROR && input->fatal_errors ? CHECK_WARNING_FATAL : severity;
    }
    else if (group == UINT32_MAX)
    {
        severity = command.quiet || (event->flags & BUSTER_CHECK_WARNING_SUPPRESSED) != 0 ? CHECK_WARNING_IGNORE :
            check_diagnostic_severity_id_from(input, event->token, event->source, event->id, command.severity);
    }
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

// The name a use at `token` took, where its spelling `name` is a typo's that the reader corrected (#1447,
// CHECK_INPUT_UNDECLARED_CORRECTED): its declaration's, as clang's diagnostics after the correction spell it. Only a
// unit with an error report has a correction, so a valid unit's warnings read nothing more.
static uint32_t check_driver_corrected_name(const Parser* parser, const CheckInput* input, uint32_t token, uint32_t name)
{
    const uint32_t count = parser->tokens.count;
    const bool use = input->report_count != 0 && token < count && token < input->token_count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                     parser->tokens.names[token] == name;
    const uint32_t declaration = use ? input->declarations[token] : BUSTER_CHECK_NONE;
    const bool corrected = declaration < count && parser->tokens.kinds[declaration] == PARSE_KIND_IDENTIFIER && parser->tokens.names[declaration] != name;
    return corrected ? parser->tokens.names[declaration] : name;
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

// A run of string literals from `token`, decoded and concatenated, as clang's messages print an attribute's string
// (#1447: a TCB's, an aspect's), in `scratch`; empty where it decodes to nothing.
static String8 check_warning_strings_value(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch)
{
    const uint32_t count = parser->tokens.count;
    uint32_t end = token;
    while (end < count && parser->tokens.kinds[end] == PARSE_KIND_STRING)
    {
        end += 1;
    }
    CheckStringPart* parts = end > token ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckStringPart, end - token) : NULL;
    bool valid = parts != NULL;
    for (uint32_t i = token; valid && i < end; i += 1)
    {
        const String8 spelling = check_warning_string_spelling(parser, pp, i, scratch);
        parts[i - token] = (CheckStringPart){ .bytes = (const uint8_t*)spelling.pointer, .length = spelling.length };
        valid = spelling.length != 0;
    }
    const CheckStringLiteral decoded = valid ? check_literal_strings(parts, end - token, pp->options.language.standard >= 2023, scratch) : (CheckStringLiteral){ 0 };
    return decoded.valid && decoded.byte_count != 0 ? (String8){ (char*)decoded.bytes, decoded.byte_count - 1 } : (String8){ "", 0 };
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
    // The target address spaces a macro writes throughout the unit (check_driver_space_macros, #1447).
    uint32_t* space_macro_spaces;
    String8* space_macro_texts;
    uint32_t space_macro_count;
    // Whether an address_space operand this reads isn't an integer constant, where no macro names a space.
    bool space_unreadable;
    // The macro that writes each calling convention throughout the unit, by TypeFunctionABI (check_driver_convention_macros).
    String8 convention_macros[TYPE_FUNCTION_ABI_COUNT];
    bool built;
};

// Each unnamed tag's diagnostic name (#992): the first typedef naming it, as clang's `T`/`T *` and `T2 *`'s
// `T *` aka do, else `(unnamed at file:line:col)` where the declaration was written — the expansion's own
// location for a macro body's tag, as clang's expansion-point answer is (#992). One scan of the input's tag-
// and typedef-kind declarations per publication pass; the rows sit under the pass's event marks in the
// caller's scratch, so they survive until the pass ends. Returns false on an allocation failure, which the
// caller reports like the renderer's other allocations rather than dropping the spelling.
// Whether the typedef type `type` carries qualifiers, through an aligned typedef's too: such a typedef of an anonymous tag
// isn't its name (#1447, clang's typedef name for linkage).
static bool check_driver_typedef_qualified(const TypeTable* types, TypeId type)
{
    return type_unqualified(type) != type || type_unqualified(type_strip_aligned(types, type)) != type_strip_aligned(types, type);
}

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
                                // The typedef is the anonymous record's canonical name; its first one wins. A qualified
                                // one names none (clang's typedef name for linkage is the tag's own type's, #1447).
                                if ((named[tag / 64] & ((uint64_t)1 << (tag & 63))) == 0 &&
                                    !check_driver_typedef_qualified(types, input->declared_types[i]))
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

// c_check_semantic_diagnostic.c's, included after this file.
static String8 check_semantic_token(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch);

// The distinct spaces a unit's map holds; a space past them keeps its attribute spelled out.
#define BUSTER_CHECK_DRIVER_SPACE_MACROS 16

// The outermost macro expansion a token's origin comes from (its PPExpansion's index plus one), through the macros
// expanded inside others; 0 for a token the source wrote or a generated one.
static uint32_t check_driver_macro_root(const Preprocessor* pp, uint32_t origin)
{
    uint32_t result = 0;
    for (uint32_t steps = 0; origin != 0 && steps < 256; steps += 1)
    {
        const uint32_t index = (origin & BUSTER_PP_ORIGIN_INDEX) - 1;
        const uint32_t kind = origin & ~BUSTER_PP_ORIGIN_INDEX;
        result = kind == 0 ? index + 1 : kind == BUSTER_PP_ORIGIN_ARGUMENT ? result : 0;
        origin = kind == 0 ? pp->history[index].parent : kind == BUSTER_PP_ORIGIN_ARGUMENT ? pp->substitutions[index].source : 0;
    }
    return result;
}

// The macro that writes an `__attribute__` list (#1447: `t` its keyword, `close` its last `)`), as clang's parser names one
// for a MacroQualifiedType (FindLocsWithCommonFileID): the list is a whole expansion, its `__attribute__` that
// expansion's first token and its `)` the last, and not spelled in the predefines (`__seg_gs`, isWrittenInBuiltinFile).
// An object-like macro by its name, a function-like one by its invocation as written, the outermost where macros nest;
// empty for none. The caller has ensured the token histories.
static String8 check_driver_attribute_macro(const Parser* parser, Preprocessor* pp, uint32_t t, uint32_t close)
{
    const uint32_t first = parser->tokens.sources[t];
    const uint32_t last = parser->tokens.sources[close];
    const uint32_t root = check_driver_macro_root(pp, pp_history_at(pp, first));
    const uint32_t range = root != 0 && first < pp->output.count ? pp_find_range(pp, pp->output.locations[first]) : BUSTER_PP_NONE;
    const bool builtin = range != BUSTER_PP_NONE && pp->files[pp->ranges[range].file].data == pp->predefines_data;
    const bool whole = root != 0 && !builtin && last < pp->output.count && root == check_driver_macro_root(pp, pp_history_at(pp, last)) &&
                       (first == 0 || check_driver_macro_root(pp, pp_history_at(pp, first - 1)) != root) &&
                       (last + 1 >= pp->output.count || check_driver_macro_root(pp, pp_history_at(pp, last + 1)) != root);
    const PPExpansion* expansion = whole ? &pp->history[root - 1] : NULL;
    const bool invocation = expansion != NULL && expansion->end > expansion->begin && pp_find_range(pp, expansion->begin) == pp_find_range(pp, expansion->end);
    const uint8_t* bytes = invocation ? pp_location_bytes(pp, expansion->begin) : NULL;
    return bytes != NULL ? (String8){ .pointer = (char*)bytes, .length = (uint64_t)(expansion->end - expansion->begin) + 1 } :
           expansion != NULL ? check_driver_warning_name(pp, expansion->name) : (String8){ 0 };
}

// The address spaces an attribute list from `t` (its `__attribute__`) to its `)` at `close` writes (#1447): for each
// `address_space(N)` of an integer constant N, its BUSTER_TYPE_SPACE_TARGET-based word, with the macro whose expansion
// wrote the list (check_driver_attribute_macro, empty where none did), into `spaces` and `texts` from `*count`, at most
// BUSTER_CHECK_DRIVER_SPACE_MACROS; `*unreadable` set where an operand isn't such a constant (`address_space(1+0)`).
static void check_driver_space_list(const Parser* parser, Preprocessor* pp, uint32_t t, uint32_t close, uint32_t* spaces, String8* texts, uint32_t* count,
                                    bool* unreadable, Arena* scratch)
{
    for (uint32_t a = t + 2; close < parser->tokens.count && a + 3 < close; a += 1)
    {
        const String8 name = parser->tokens.kinds[a] == PARSE_KIND_IDENTIFIER ? check_driver_warning_name(pp, parser->tokens.names[a]) : (String8){ 0 };
        const bool space = (name.length == 13 && memcmp(name.pointer, "address_space", 13) == 0) ||
                           (name.length == 17 && memcmp(name.pointer, "__address_space__", 17) == 0);
        const String8 number = space && parser->tokens.kinds[a + 1] == PARSE_KIND_L_PAREN && parser->tokens.kinds[a + 2] == PARSE_KIND_NUMBER &&
                               parser->tokens.kinds[a + 3] == PARSE_KIND_R_PAREN ? check_semantic_token(parser, pp, a + 2, scratch) : (String8){ 0 };
        char digits[24] = { 0 };
        const bool read = number.length != 0 && number.length < sizeof(digits);
        if (read)
        {
            memcpy(digits, number.pointer, number.length);
        }
        char* end = NULL;
        const unsigned long long value = read ? strtoull(digits, &end, 0) : 0;
        // An integer suffix (`1u`, `1UL`) is the same constant.
        while (read && end != NULL && (*end == 'u' || *end == 'U' || *end == 'l' || *end == 'L'))
        {
            end += 1;
        }
        const bool constant = read && end != NULL && *end == 0 && value <= BUSTER_TYPE_SPACE_MASK - BUSTER_TYPE_SPACE_TARGET;
        // Any other operand (`1+0`, a named constant) may write any space directly: no macro names one then.
        *unreadable |= space && !constant;
        if (constant && *count < BUSTER_CHECK_DRIVER_SPACE_MACROS)
        {
            spaces[*count] = BUSTER_TYPE_SPACE_TARGET + (uint32_t)value;
            texts[*count] = check_driver_attribute_macro(parser, pp, t, close);
            *count += 1;
        }
    }
}

// Whether token `t` is an identifier spelling address_space (or __address_space__).
static bool check_driver_space_name(const Parser* parser, Preprocessor* pp, uint32_t t)
{
    const String8 name = parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? check_driver_warning_name(pp, parser->tokens.names[t]) : (String8){ 0 };
    return (name.length == 13 && memcmp(name.pointer, "address_space", 13) == 0) || (name.length == 17 && memcmp(name.pointer, "__address_space__", 17) == 0);
}

// The macros that write the unit's target address spaces (#1447), as clang's MacroQualifiedType names a type whose
// attribute list one macro's expansion wrote (`#define AS1 __attribute__((address_space(1)))`, `'AS1 int *'`,
// check_driver_attribute_macro). These are the fallback for a type no declaration's own spelling describes
// (check_driver_declared_spaces gives a declaration's): a space this unit writes both ways, or with two macros, keeps its
// attribute spelled out, and every space does where an operand isn't an integer constant this reads
// (`address_space(1+0)`), whose space could be any one. One scan of the `__attribute__` tokens, with the pass's other
// spelling rows (check_driver_spelling_ready).
static bool check_driver_space_macros(const Parser* parser, Preprocessor* pp, CheckWarningSpelling* spell, Arena* scratch)
{
    uint32_t* spaces = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_CHECK_DRIVER_SPACE_MACROS);
    String8* texts = spaces != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, BUSTER_CHECK_DRIVER_SPACE_MACROS) : NULL;
    bool* mixed = texts != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, bool, BUSTER_CHECK_DRIVER_SPACE_MACROS) : NULL;
    const bool valid = mixed != NULL;
    // The token histories only where an `address_space` attribute is written at all: most units have none.
    bool written = false;
    for (uint32_t t = 0; valid && !written && t < parser->tokens.count; t += 1)
    {
        written = check_driver_space_name(parser, pp, t);
    }
    const bool history = written && pp_history_ensure(pp);
    uint32_t count = 0;
    bool unreadable = false;
    const uint32_t token_count = parser->tokens.count;
    for (uint32_t t = 0; history && t + 1 < token_count; t += 1)
    {
        const uint32_t close = parser->tokens.kinds[t] == PARSE_KIND_ATTRIBUTE && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN ? parser->tree.parents[t + 1] :
                               BUSTER_CHECK_NONE;
        uint32_t list_spaces[BUSTER_CHECK_DRIVER_SPACE_MACROS];
        String8 list_texts[BUSTER_CHECK_DRIVER_SPACE_MACROS];
        uint32_t listed = 0;
        if (close < token_count)
        {
            check_driver_space_list(parser, pp, t, close, list_spaces, list_texts, &listed, &unreadable, scratch);
        }
        for (uint32_t k = 0; k < listed; k += 1)
        {
            uint32_t entry = count;
            for (uint32_t i = 0; entry == count && i < count; i += 1)
            {
                entry = spaces[i] == list_spaces[k] ? i : count;
            }
            if (entry == count && count < BUSTER_CHECK_DRIVER_SPACE_MACROS)
            {
                spaces[count] = list_spaces[k];
                texts[count] = list_texts[k];
                mixed[count] = list_texts[k].length == 0;
                count += 1;
            }
            else if (entry < count)
            {
                const String8 text = list_texts[k];
                mixed[entry] |= text.length != texts[entry].length || (text.length != 0 && memcmp(text.pointer, texts[entry].pointer, text.length) != 0);
            }
        }
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        spaces[kept] = spaces[i];
        texts[kept] = texts[i];
        kept += mixed[i] ? 0 : 1;
    }
    spell->space_macro_spaces = spaces;
    spell->space_macro_texts = texts;
    spell->space_macro_count = unreadable ? 0 : kept;
    spell->space_unreadable = unreadable;
    return valid;
}

// A calling convention attribute's TypeFunctionABI by its name, `__name__` too; TYPE_FUNCTION_ABI_COUNT for none.
static TypeFunctionABI check_driver_convention_name(String8 name)
{
    static const char* const names[] = {
        "sysv_abi", "ms_abi", "vectorcall", "regcall", "preserve_most", "preserve_all", "preserve_none", "no_caller_saved_registers",
        "intel_ocl_bicc", "swiftcall", "swiftasynccall",
    };
    _Static_assert(sizeof(names) / sizeof(names[0]) == TYPE_FUNCTION_ABI_COUNT, "every convention has a name");
    const bool wrapped = name.length > 4 && memcmp(name.pointer, "__", 2) == 0 && memcmp(name.pointer + name.length - 2, "__", 2) == 0;
    const char* bare = wrapped ? name.pointer + 2 : name.pointer;
    const uint64_t length = wrapped ? name.length - 4 : name.length;
    TypeFunctionABI result = TYPE_FUNCTION_ABI_COUNT;
    for (uint32_t i = 0; result == TYPE_FUNCTION_ABI_COUNT && i < TYPE_FUNCTION_ABI_COUNT; i += 1)
    {
        result = strlen(names[i]) == length && memcmp(names[i], bare, length) == 0 ? (TypeFunctionABI)i : TYPE_FUNCTION_ABI_COUNT;
    }
    return result;
}

// The macros that write the unit's calling conventions (#1447), as clang names a function type by the macro its
// convention's attribute list came from (MacroQualifiedType: `#define MS __attribute__((ms_abi))`,
// `'MS void (void) __attribute__((ms_abi))'`; check_driver_attribute_macro). As for spaces, a convention this unit
// writes both ways, or with two macros, has none, and none has one where a convention keyword (`__regcall`, which
// clang names by no macro) is written at all. sysv_abi and no_caller_saved_registers aren't named: the type table
// keeps no sysv_abi, and the other is no convention of clang's.
static void check_driver_convention_macros(const Parser* parser, Preprocessor* pp, CheckWarningSpelling* spell)
{
    bool written = false;
    bool keyword = false;
    for (uint32_t t = 0; !keyword && t < parser->tokens.count; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        keyword = kind == PARSE_KIND_CALLING_CONVENTION;
        written |= kind == PARSE_KIND_IDENTIFIER && check_driver_convention_name(check_driver_warning_name(pp, parser->tokens.names[t])) < TYPE_FUNCTION_ABI_COUNT;
    }
    bool mixed[TYPE_FUNCTION_ABI_COUNT] = { 0 };
    const bool history = written && !keyword && pp_history_ensure(pp);
    const uint32_t token_count = parser->tokens.count;
    for (uint32_t t = 0; history && t + 1 < token_count; t += 1)
    {
        const uint32_t close = parser->tokens.kinds[t] == PARSE_KIND_ATTRIBUTE && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN ? parser->tree.parents[t + 1] :
                               BUSTER_CHECK_NONE;
        String8 text = { 0 };
        bool read = false;
        for (uint32_t a = t + 2; close < token_count && a < close; a += 1)
        {
            const TypeFunctionABI abi = parser->tokens.kinds[a] == PARSE_KIND_IDENTIFIER ?
                                        check_driver_convention_name(check_driver_warning_name(pp, parser->tokens.names[a])) : TYPE_FUNCTION_ABI_COUNT;
            if (abi < TYPE_FUNCTION_ABI_COUNT)
            {
                text = read ? text : check_driver_attribute_macro(parser, pp, t, close);
                read = true;
                const String8 known = spell->convention_macros[abi];
                mixed[abi] |= text.length == 0 || (known.length != 0 && (known.length != text.length || memcmp(known.pointer, text.pointer, text.length) != 0));
                spell->convention_macros[abi] = text;
            }
        }
    }
    for (uint32_t i = 0; i < TYPE_FUNCTION_ABI_COUNT; i += 1)
    {
        const bool named = history && !mixed[i] && i != TYPE_FUNCTION_ABI_SYSV && i != TYPE_FUNCTION_ABI_NO_CALLER_SAVED;
        spell->convention_macros[i] = named ? spell->convention_macros[i] : (String8){ 0 };
    }
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
        // With the other rows, below every event's mark: a pass's later events read the map too (#1447).
        if (valid)
        {
            valid = check_driver_space_macros(parser, pp, spell, scratch);
        }
        if (valid)
        {
            check_driver_convention_macros(parser, pp, spell);
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
        // An unused file-scope declaration's at the unit's end is published after every other, where none was an error
        // (#2141): its own promotion rejects the unit then.
        const bool unit_end = (event->kind == CHECK_WARNING_EVENT_UNUSED_VARIABLE || event->kind == CHECK_WARNING_EVENT_UNUSED_FUNCTION) &&
                              event->completion_kind == CHECK_WARNING_COMPLETE_TU_END;
        promoted = !unit_end && event->kind != CHECK_WARNING_EVENT_MESSAGE && event->kind != CHECK_WARNING_EVENT_ENUM_MIX && (repair == NULL || check_repair_warning_token(repair, event->token)) &&
                   check_driver_warning_severity(pp, input, event) >= CHECK_WARNING_ERROR;
    }
    return promoted;
}

typedef struct CheckWarningParen CheckWarningParen;
struct CheckWarningParen
{
    CheckWarningParen* next;
    uint32_t close;
    uint32_t pointers;
    uint32_t depth;
};

// The parentheses of this declarator occurrence, for the shared type printer. Prefix stars and suffix chunks count
// the levels from its name outwards; a group's level is the chunks inside it. Skip attribute arguments, array bounds
// and parameter lists, and start again at a sibling declarator or the parameter list containing the name. This cold
// walk allocates only for actual enclosing groups and keeps no token-wide spelling column in checked input.
static bool check_driver_warning_parentheses(const Parser* parser, const TypeTable* types, uint32_t name, TypeId declared,
                                             Arena* scratch, TypeNames* names)
{
    CheckWarningParen* groups = NULL;
    uint32_t count = 0;
    uint32_t pointers = 0;
    bool valid = true;
    const bool contextual = name < parser->tokens.count && type_is_valid(types, declared);
    const bool named = contextual && parser->tokens.kinds[name] == PARSE_KIND_IDENTIFIER && parser->tree.roles[name] == PARSE_ROLE_OPERAND;
    const uint32_t begin = contextual ? check_input_declaration_begin(parser, name) : 0;
    for (uint32_t i = begin; contextual && valid && i < name;)
    {
        if (check_input_attribute_starts(parser, i, name))
        {
            const uint32_t after = check_input_skip_attributes(parser, i, name);
            i = after > i ? after : i + 1;
            continue;
        }
        const uint8_t kind = parser->tokens.kinds[i];
        const uint8_t role = parser->tree.roles[i];
        const uint32_t close = parser->tree.parents[i];
        const bool open = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        if (open && close > i && close < name)
        {
            i = close + 1;
            continue;
        }
        if (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_EQUAL || (open && close >= name && role != PARSE_ROLE_OPEN_GROUP))
        {
            groups = NULL;
            count = 0;
            pointers = 0;
        }
        else if (open && close >= name && role == PARSE_ROLE_OPEN_GROUP)
        {
            CheckWarningParen* group = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckWarningParen, 1);
            valid = group != NULL;
            if (valid)
            {
                *group = (CheckWarningParen){ .next = groups, .close = close, .pointers = pointers, .depth = UINT32_MAX };
                groups = group;
                count += 1;
            }
        }
        pointers += kind == PARSE_KIND_STAR && role == PARSE_ROLE_PREFIX;
        i += 1;
    }
    CheckWarningParen* pending = groups;
    uint32_t suffixes = 0;
    for (uint32_t i = contextual ? name + named : parser->tokens.count; valid && i < parser->tokens.count;)
    {
        if (check_input_attribute_starts(parser, i, parser->tokens.count))
        {
            const uint32_t after = check_input_skip_attributes(parser, i, parser->tokens.count);
            i = after > i ? after : i + 1;
        }
        else if (pending != NULL && i == pending->close)
        {
            pending->depth = pointers - pending->pointers + suffixes;
            pending = pending->next;
            i += 1;
        }
        else if ((parser->tree.roles[i] == PARSE_ROLE_OPEN_FUNCTION || parser->tokens.kinds[i] == PARSE_KIND_L_SQUARE) &&
                 parser->tree.parents[i] > i && parser->tree.parents[i] < parser->tokens.count)
        {
            suffixes += 1;
            i = parser->tree.parents[i] + 1;
        }
        else
        {
            break;
        }
    }
    TypePrintParen* parentheses = valid && pending == NULL && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, TypePrintParen, count) : NULL;
    valid &= pending != NULL || count == 0 || parentheses != NULL;
    uint32_t kept = 0;
    uint32_t depth = 0;
    TypeId type = contextual ? type_strip_aligned(types, declared) : 0;
    for (CheckWarningParen* group = groups; valid && parentheses != NULL && group != NULL; group = group->next)
    {
        while (depth < group->depth && type_is_valid(types, type))
        {
            const uint8_t kind = type_kind(types, type);
            const bool derived = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind);
            type = derived ? type_strip_aligned(types, types->types.base[type_index(type)]) : 0;
            depth += 1;
        }
        if (type_is_valid(types, type))
        {
            if (kept != 0 && parentheses[kept - 1].type == type)
            {
                parentheses[kept - 1].count += 1;
            }
            else
            {
                parentheses[kept++] = (TypePrintParen){ .type = type, .count = 1 };
            }
        }
    }
    names->parentheses = parentheses;
    names->parenthesis_count = kept;
    return valid;
}

// c_check_semantic_diagnostic.c's, included after this file: an expression's text with expression spacing.
static const char* check_semantic_requirement(const Parser* parser, Preprocessor* pp, uint32_t first, uint32_t end, const char* const* spelled,
                                              Arena* scratch);

// The sugar a typeof specifier prints as (#1837), clang's TypePrinter's: `typeof(T)` (`typeof_unqual(T)`) with its operand
// type T as clang prints it, a typedef name as written and a nested typeof as its own sugar; `typeof (x)` of an expression
// that is one name. Empty where it can't tell (another expression, or a type name that spells a typedef inside it), which
// prints no sugar. `keyword` is the typeof's keyword; `names` prints the operand's type, its aliases unused.
static String8 check_driver_typeof_text(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, Arena* scratch,
                                        const TypeNames* names, uint32_t keyword, uint32_t options)
{
    const uint32_t count = parser->tokens.count;
    // The keywords from the outermost in, while each one's operand is exactly the next typeof.
    uint32_t chain[8];
    uint32_t depth = 0;
    String8 inner = { 0 };
    bool expression = false;
    bool walking = keyword + 1 < count && parser->tokens.kinds[keyword + 1] == PARSE_KIND_L_PAREN;
    while (walking && depth < BUSTER_ARRAY_LENGTH(chain))
    {
        chain[depth++] = keyword;
        const uint32_t close = parser->tree.parents[keyword + 1];
        const uint32_t first = keyword + 2;
        walking = false;
        const bool one = close < count && first + 1 == close;
        const uint32_t resolved = one && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER && input->declarations != NULL ? input->declarations[first] : 0;
        const bool typedef_name = resolved != 0 && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF;
        const bool nested = close < count && first < close && parser->tokens.kinds[first] == PARSE_KIND_TYPEOF && check_input_typeof_name(parser->tokens.names[first]) &&
                            first + 1 < close && parser->tokens.kinds[first + 1] == PARSE_KIND_L_PAREN && parser->tree.parents[first + 1] + 1 == close;
        // A type name of keywords, tags and typedef names alone: its type prints as clang prints it, each typedef name as
        // written (`typeof(L *)`, #1837).
        bool keywords = close < count && first < close;
        TypeId alias_types[4];
        String8 alias_names[4];
        uint32_t alias_count = 0;
        for (uint32_t token = first; keywords && token < close; token += 1)
        {
            const uint8_t kind = parser->tokens.kinds[token];
            const uint32_t named = kind == PARSE_KIND_IDENTIFIER && input->declarations != NULL && token < input->token_count ? input->declarations[token] : 0;
            const bool alias = named != 0 && named != token && named < input->token_count && input->declaration_kinds[named] == C_BINDING_TYPEDEF &&
                               !(token > first && (parser->tokens.kinds[token - 1] == PARSE_KIND_STRUCT || parser->tokens.kinds[token - 1] == PARSE_KIND_ENUM));
            keywords = kind == PARSE_KIND_TYPE || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM ||
                       kind == PARSE_KIND_STAR || (kind == PARSE_KIND_ATOMIC && !(token + 1 < close && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN)) ||
                       (kind == PARSE_KIND_IDENTIFIER && token > first && (parser->tokens.kinds[token - 1] == PARSE_KIND_STRUCT || parser->tokens.kinds[token - 1] == PARSE_KIND_ENUM)) ||
                       (alias && alias_count < BUSTER_ARRAY_LENGTH(alias_types) && type_is_valid(types, input->declared_types[named]));
            if (keywords && alias)
            {
                const uint32_t name = parser->tokens.names[token];
                alias_types[alias_count] = input->declared_types[named];
                alias_names[alias_count] = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
                alias_count += 1;
            }
        }
        // Any other operand that is an expression prints as clang's StmtPrinter does, with expression spacing
        // (`typeof (x + 1)`, check_semantic_requirement).
        const uint8_t lead = close < count && first < close ? parser->tokens.kinds[first] : PARSE_KIND_SEMI;
        const uint32_t lead_named = lead == PARSE_KIND_IDENTIFIER && input->declarations != NULL && first < input->token_count ? input->declarations[first] : 0;
        const bool lead_type = lead == PARSE_KIND_TYPE || lead == PARSE_KIND_QUALIFIER || lead == PARSE_KIND_STRUCT || lead == PARSE_KIND_ENUM ||
                               lead == PARSE_KIND_ATOMIC || lead == PARSE_KIND_TYPEOF ||
                               (lead_named != 0 && lead_named < input->token_count && input->declaration_kinds[lead_named] == C_BINDING_TYPEDEF);
        const bool operand = close < count && first < close && !lead_type && !nested && !(one && lead == PARSE_KIND_IDENTIFIER);
        if (typedef_name)
        {
            const uint32_t name = parser->tokens.names[first];
            inner = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
        }
        else if (one && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER && resolved != 0)
        {
            const uint32_t name = parser->tokens.names[first];
            inner = (String8){ .pointer = (char*)intern_bytes(pp->table, name), .length = intern_length(pp->table, name) };
            expression = true;
        }
        else if (nested)
        {
            keyword = first;
            walking = true;
        }
        else if (keywords && chain[0] < input->token_count && type_is_valid(types, input->declared_types[chain[0]]))
        {
            // The outermost keyword's type: a nested typeof's own is read by the constant reader, which records none, and
            // is that one's.
            TypeNames written = *names;
            written.alias_types = alias_count != 0 ? alias_types : names->alias_types;
            written.alias_names = alias_count != 0 ? alias_names : names->alias_names;
            written.alias_count = alias_count != 0 ? alias_count : names->alias_count;
            inner = type_print(types, scratch, input->declared_types[chain[0]], (String8){ 0 }, &written, options);
        }
        else if (operand)
        {
            const char* printed = check_semantic_requirement(parser, pp, first, close, NULL, scratch);
            inner = printed != NULL ? (String8){ .pointer = (char*)printed, .length = strlen(printed) } : (String8){ 0 };
            expression = inner.length != 0;
        }
    }
    String8 result = { 0 };
    const char* text = NULL;
    if (inner.length != 0 && !walking)
    {
        // From the innermost out: `typeof(` ... `)`, the innermost `typeof (` for an expression.
        text = check_warning_text(scratch, "%.*s", (int)inner.length, inner.pointer);
        for (uint32_t level = depth; text != NULL && level > 0; level -= 1)
        {
            const uint32_t name = parser->tokens.names[chain[level - 1]];
            const bool unqualified = name == C_NAME_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL || name == C_NAME_GNU_TYPEOF_UNQUAL2;
            const bool spaced = expression && level == depth;
            text = check_warning_text(scratch, "%s%s(%s)", unqualified ? "typeof_unqual" : "typeof", spaced ? " " : "", text);
        }
    }
    result = text != NULL ? (String8){ .pointer = (char*)text, .length = (uint64_t)strlen(text) } : (String8){ 0 };
    return result;
}

// Whether `alias`, unqualified, is one of `type`'s levels, as type_print reaches them: the type itself, then through
// pointers, arrays and functions' results to the specifier.
static bool check_driver_spelling_level(const TypeTable* types, TypeId type, TypeId alias)
{
    const TypeId wanted = type_unqualified(type_strip_aligned(types, alias));
    TypeId level = type;
    bool found = false;
    for (uint32_t depth = 0; !found && depth < 256 && level != 0 && type_is_valid(types, level); depth += 1)
    {
        const TypeId bare = type_unqualified(type_strip_aligned(types, level));
        const uint8_t kind = type_kind(types, bare);
        found = bare == wanted;
        level = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind) ? types->types.base[type_index(bare)] : 0;
    }
    return found;
}

// The typedef of `spelling`'s chain that names one of `type`'s levels (#1447), as clang's sugar keeps it through
// dereferences and subscripts: a declaration's levels sit over its specifier's typedef, whose own levels sit over its
// specifier's, and so on, so the first typedef along that chain whose type is a level of `type` is the one clang's type
// names there (`*p` of a `pv p`, `typedef v4si *pv`, is `v4si`; `**b` of a `VPP b`, `typedef VP *VPP`, is `VP`). A
// chain's types only shrink, so no later one can be a level above it. `spelling` where none is.
static uint32_t check_driver_spelling_chain(const Parser* parser, const CheckInput* input, const TypeTable* types, TypeId type, uint32_t spelling)
{
    uint32_t result = spelling;
    uint32_t at = spelling;
    bool found = false;
    for (uint32_t steps = 0; !found && at != 0 && at < input->token_count && steps < 64; steps += 1)
    {
        found = check_driver_spelling_level(types, type, input->declared_types[at]);
        result = found ? at : result;
        at = found ? at : check_input_typedef_spelling(input, parser, at);
    }
    return result;
}

// c_check_semantic_diagnostic.c's, included after this file.
static const char* check_semantic_requirement(const Parser* parser, Preprocessor* pp, uint32_t first, uint32_t end, const char* const* spelled,
                                              Arena* scratch);

#define BUSTER_CHECK_DRIVER_VLA_BOUNDS 8

// The size expressions of `type`'s variable array levels (#1447), through pointers, arrays and functions' results, as
// clang prints them (`int (*)[n + 1]`): a VARIABLE_ARRAY's extent is its expression's first token plus one
// (check_input_vla_bound), which ends at its `]`, spelled with expression spacing (check_semantic_requirement). At most
// BUSTER_CHECK_DRIVER_VLA_BOUNDS; the rest print `[]`. Returns how many it wrote.
static uint32_t check_driver_vla_bounds(const Parser* parser, Preprocessor* pp, const TypeTable* types, TypeId type, uint64_t* ids, String8* texts,
                                        Arena* scratch)
{
    uint32_t count = 0;
    TypeId level = type;
    for (uint32_t depth = 0; depth < 256 && level != 0 && type_is_valid(types, level); depth += 1)
    {
        const TypeId bare = type_unqualified(type_strip_aligned(types, level));
        const uint8_t kind = type_kind(types, bare);
        const uint64_t extent = kind == BUSTER_TYPE_KIND_VARIABLE_ARRAY ? types->types.extent[type_index(bare)] : 0;
        const uint32_t first = extent != 0 && extent - 1 < parser->tokens.count ? (uint32_t)(extent - 1) : BUSTER_CHECK_NONE;
        uint32_t close = first;
        uint32_t nesting = 0;
        bool scanning = first != BUSTER_CHECK_NONE;
        while (scanning && close < parser->tokens.count)
        {
            const uint8_t token = parser->tokens.kinds[close];
            scanning = !(token == PARSE_KIND_R_SQUARE && nesting == 0);
            nesting = token == PARSE_KIND_L_PAREN || token == PARSE_KIND_L_SQUARE || token == PARSE_KIND_L_BRACE ? nesting + 1 :
                      (token == PARSE_KIND_R_PAREN || token == PARSE_KIND_R_SQUARE || token == PARSE_KIND_R_BRACE) && nesting != 0 ? nesting - 1 : nesting;
            close += scanning ? 1 : 0;
        }
        const char* text = !scanning && first != BUSTER_CHECK_NONE && close > first && count < BUSTER_CHECK_DRIVER_VLA_BOUNDS ?
                           check_semantic_requirement(parser, pp, first, close, NULL, scratch) : NULL;
        if (text != NULL)
        {
            ids[count] = extent;
            texts[count] = (String8){ .pointer = (char*)text, .length = strlen(text) };
            count += 1;
        }
        level = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind) ? types->types.base[type_index(bare)] : 0;
    }
    return count;
}

// Whether `alias` is the value type of one of `type`'s _Atomic levels, through pointers, arrays and functions' results.
static bool check_driver_atomic_value(const TypeTable* types, TypeId type, TypeId alias)
{
    const TypeId wanted = type_unqualified(type_strip_aligned(types, alias));
    bool found = false;
    TypeId level = type;
    for (uint32_t depth = 0; !found && depth < 64 && level != 0 && type_is_valid(types, level); depth += 1)
    {
        const TypeId bare = type_unqualified(type_strip_aligned(types, level));
        const uint8_t kind = type_kind(types, bare);
        const TypeId base = types->types.base[type_index(bare)];
        found = kind == BUSTER_TYPE_KIND_ATOMIC && type_unqualified(type_strip_aligned(types, base)) == wanted;
        level = kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind) ? base : 0;
    }
    return found;
}

// A warning's type spelling (#992): `spelling` is the operand's typedef specifier's declaration, so the base's
// specifier prints the typedef's name and clang's `(aka '...')` shows the type without that sugar; the sentinel
// BUSTER_CHECK_NONE prints `__size_t` for a sizeof/alignof operand's own type. The anonymous map prints an
// unnamed tag's typedef name or its `(unnamed at file:line:col)`. Where the typedef's own type isn't one of the
// type's levels, a typedef its own declaration spells may be (check_driver_spelling_chain).
static String8 check_driver_warning_type_at(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                            CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling,
                                            uint32_t declaration, TypeId declared)
{
    String8 result = { 0 };
    if (type != 0 && type_is_valid(types, type))
    {
        // Builtin scalar types carry no tag or typedef name when there is no source spelling. Printing one
        // needs neither the interned-name columns nor the unit-wide anonymous-tag map.
        const uint32_t index = type_index(type);
        if (spelling == 0 && index >= BUSTER_TYPE_CODE_BOOL && index <= BUSTER_TYPE_CODE_FLOAT128)
        {
            const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
            const String8 text = type_print(types, scratch, type, (String8){ 0 }, NULL, options);
            const char* quoted = text.length != 0 ? check_warning_text(scratch, "'%.*s'", (int)text.length, text.pointer) : NULL;
            result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = (uint64_t)strlen(quoted) } : (String8){ 0 };
        }
        // Only diagnostic rendering needs these spelling arrays; clean checking retains no identifier-sized view.
        // The pass's columns live in `spell`, built on the pass's first rendered type below its marks (#992).
        else if (check_driver_spelling_ready(parser, pp, input, types, spell, scratch))
        {
            uint64_t bound_ids[BUSTER_CHECK_DRIVER_VLA_BOUNDS];
            String8 bound_texts[BUSTER_CHECK_DRIVER_VLA_BOUNDS];
            TypeNames names = {
                .bytes = spell->bytes,
                .lengths = spell->lengths,
                .count = spell->count,
                .anonymous = spell->anonymous,
                .anonymous_count = spell->anonymous_count,
                .bound_ids = bound_ids,
                .bound_texts = bound_texts,
                .bound_count = check_driver_vla_bounds(parser, pp, types, type, bound_ids, bound_texts, scratch),
            };
            names.space_macro_spaces = spell->space_macro_spaces;
            names.space_macro_texts = spell->space_macro_texts;
            names.space_macro_count = spell->space_macro_count;
            TypeId alias_type = 0;
            String8 alias_name = { 0 };
            uint32_t alias_written = 0;
            if (spelling == BUSTER_CHECK_NONE)
            {
                alias_type = type_unqualified(type_strip_aligned(types, type));
                alias_name = BUSTER_S8("__size_t");
            }
            else if (spelling != 0 && input != NULL && input->declared_types != NULL && spelling < input->token_count &&
                     parser->tokens.kinds[spelling] == PARSE_KIND_TYPEOF)
            {
                // A typeof specifier's sugar (#1837): its type prints as `typeof(...)`.
                const TypeId declared = input->declared_types[spelling];
                const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
                alias_name = type_is_valid(types, declared) ? check_driver_typeof_text(parser, pp, input, types, scratch, &names, spelling, options) : (String8){ 0 };
                alias_type = alias_name.length != 0 ? declared : 0;
            }
            else if (spelling != 0 && input != NULL && input->declared_types != NULL && spelling < input->token_count)
            {
                // A typedef's use (check_input_typedef_sugar) is its declaration, with the qualifiers written again
                // beside it (`const CI`, #1447).
                const uint32_t direct = check_input_sugar_declaration(input, parser, spelling, &alias_written);
                spelling = check_driver_spelling_chain(parser, input, types, type, direct);
                alias_written = spelling == direct ? alias_written : 0;
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
                names.alias_written = &alias_written;
                names.alias_count = 1;
            }
            const bool contextual = check_driver_warning_parentheses(parser, types, declaration, declared, scratch, &names);
            // A calling convention one macro writes names the type by the macro, before the canonical spelling clang's
            // MacroQualifiedType prints under it (`'MS void (void) __attribute__((ms_abi))'`, #1447): the first function
            // level's, through pointers and arrays.
            TypeId level = type;
            TypeFunctionABI abi = TYPE_FUNCTION_ABI_COUNT;
            for (uint32_t depth = 0; abi == TYPE_FUNCTION_ABI_COUNT && depth < 64 && level != 0 && type_is_valid(types, level); depth += 1)
            {
                const TypeId bare = type_unqualified(type_strip_aligned(types, level));
                const uint8_t kind = type_kind(types, bare);
                abi = kind == BUSTER_TYPE_KIND_FUNCTION ? type_function_convention(types, bare) : TYPE_FUNCTION_ABI_COUNT;
                level = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? types->types.base[type_index(bare)] : 0;
            }
            const String8 convention = abi < TYPE_FUNCTION_ABI_COUNT && alias_type == 0 ? spell->convention_macros[abi] : (String8){ 0 };
            const uint32_t sugar_options = convention.length != 0 ? options | BUSTER_TYPE_PRINT_CANONICAL_CC : options;
            String8 sugared = contextual ? type_print(types, scratch, type, (String8){ 0 }, &names, sugar_options) : (String8){ 0 };
            const char* macro_named = sugared.length != 0 && convention.length != 0 ?
                                      check_warning_text(scratch, "%.*s %.*s", (int)convention.length, convention.pointer, (int)sugared.length, sugared.pointer) : NULL;
            sugared = macro_named != NULL ? (String8){ .pointer = (char*)macro_named, .length = strlen(macro_named) } : sugared;
            // A function designator's own preserve_* convention prints before it, as its AttributedType's
            // printAttributedBefore puts it (`'__attribute__((preserve_most)) void (void)'`): a function's convention is
            // the last thing its type prints.
            const uint8_t top = type_is_valid(types, type) ? type_kind(types, type_unqualified(type_strip_aligned(types, type))) : BUSTER_TYPE_KIND_NONE;
            const char* preserve = convention.length == 0 && top == BUSTER_TYPE_KIND_FUNCTION ?
                                   (abi == TYPE_FUNCTION_ABI_PRESERVE_MOST ? "preserve_most" : abi == TYPE_FUNCTION_ABI_PRESERVE_ALL ? "preserve_all" :
                                    abi == TYPE_FUNCTION_ABI_PRESERVE_NONE ? "preserve_none" : NULL) : NULL;
            const char* suffix = preserve != NULL ? check_warning_text(scratch, " __attribute__((%s))", preserve) : NULL;
            const uint64_t suffix_length = suffix != NULL ? strlen(suffix) : 0;
            const bool trailing = suffix != NULL && sugared.length > suffix_length &&
                                  memcmp(sugared.pointer + sugared.length - suffix_length, suffix, suffix_length) == 0;
            const char* moved = trailing ? check_warning_text(scratch, "__attribute__((%s)) %.*s", preserve, (int)(sugared.length - suffix_length), sugared.pointer) :
                                NULL;
            sugared = moved != NULL ? (String8){ .pointer = (char*)moved, .length = strlen(moved) } : sugared;
            String8 canonical = { 0 };
            // clang's desugaring stops at a vector type's own typedef, the one whose underlying type is the vector
            // (desugarForDiagnostic: "People want their vec4"), at any level (#1447): a typedef of that typedef's
            // a.k.a. is that one's name, a pointer typedef's (`typedef v4si *pv`) keeps it at the pointee
            // (`'pv' (aka 'v4si *')`), and that one has none. It is the last vector typedef along the chain of
            // typedef specifiers from the alias's declaration.
            // (A typeof's sugar has no typedef chain, #1837.)
            const bool typeof_spelling = spelling != BUSTER_CHECK_NONE && spelling < parser->tokens.count && parser->tokens.kinds[spelling] == PARSE_KIND_TYPEOF;
            uint32_t vector_name = 0;
            uint32_t chain = alias_type != 0 && spelling != BUSTER_CHECK_NONE && !typeof_spelling ? spelling : 0;
            for (uint32_t steps = 0; chain != 0 && chain < input->token_count && steps < 64; steps += 1)
            {
                const uint8_t kind = type_kind(types, type_strip_aligned(types, input->declared_types[chain]));
                vector_name = kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR ? chain : vector_name;
                chain = check_input_typedef_spelling(input, parser, chain);
            }
            TypeId vector_type = vector_name != 0 ? input->declared_types[vector_name] : 0;
            const uint32_t vector_identifier = vector_name != 0 ? parser->tokens.names[vector_name] : 0;
            String8 vector_alias = vector_name != 0 ? (String8){ .pointer = (char*)intern_bytes(pp->table, vector_identifier),
                                                                 .length = intern_length(pp->table, vector_identifier) } : (String8){ 0 };
            if (names.alias_count != 0 && vector_name != spelling)
            {
                // clang's `(aka '...')`: the same type with the typedef sugar expanded — an anonymous record's
                // typedef stays, since it is that record's own name; a vector's own typedef stays too. Where the
                // alias's declaration spells the record's definition rather than a typedef, its type as written is the
                // tag's, which prints its keyword (`'SP' (aka 'struct S *')`, #1447).
                TypeNames expand = names;
                // (The typedef that names the record is its own spelling: no a.k.a.)
                const TypeId alias_tag = alias_type != 0 ? type_unqualified(type_strip_aligned(types, alias_type)) : 0;
                const uint8_t alias_tag_kind = alias_tag != 0 ? type_kind(types, alias_tag) : BUSTER_TYPE_KIND_VOID;
                const uint32_t tag = alias_tag_kind == BUSTER_TYPE_KIND_STRUCT || alias_tag_kind == BUSTER_TYPE_KIND_UNION || alias_tag_kind == BUSTER_TYPE_KIND_ENUM ?
                                     types->types.index[type_index(alias_tag)] : names.anonymous_count;
                const bool naming = tag < names.anonymous_count && names.anonymous[tag].length == alias_name.length &&
                                    memcmp(names.anonymous[tag].pointer, alias_name.pointer, alias_name.length) == 0;
                expand.anonymous_keyword = spelling != BUSTER_CHECK_NONE && !naming && (typeof_spelling || check_input_typedef_spelling(input, parser, spelling) == 0);
                expand.alias_count = vector_name != 0 ? 1 : 0;
                expand.alias_types = &vector_type;
                expand.alias_names = &vector_alias;
                // clang's desugarForDiagnostic doesn't look inside an _Atomic (#1447): a typedef that is an atomic's value
                // type stays in the a.k.a. (`_Atomic(I) *` has none).
                const bool atomic_value = vector_name == 0 && alias_type != 0 && !typeof_spelling && check_driver_atomic_value(types, type, alias_type);
                expand.alias_count = atomic_value ? 1 : expand.alias_count;
                expand.alias_types = atomic_value ? &alias_type : expand.alias_types;
                expand.alias_names = atomic_value ? &alias_name : expand.alias_names;
                expand.parenthesis_count = 0;
                // desugarForDiagnostic strips a macro's name too: the a.k.a. spells the space out.
                expand.space_macro_count = 0;
                canonical = type_print(types, scratch, type, (String8){ 0 }, &expand, options | BUSTER_TYPE_PRINT_CANONICAL_CC);
            }
            // A macro's name alone gives no a.k.a. (it sets no ShouldAKA): the comparison is without it.
            TypeNames bare = names;
            bare.space_macro_count = 0;
            const String8 compared = canonical.length != 0 && contextual ? type_print(types, scratch, type, (String8){ 0 }, &bare, options | BUSTER_TYPE_PRINT_CANONICAL_CC) :
                                     sugared;
            // The returned text carries its own quotes: clang's `(aka '...')` sits outside the type's, so the
            // messages print `%.*s` without quoting it themselves.
            result = sugared;
            if (sugared.length != 0 && canonical.length != 0 &&
                (canonical.length != compared.length || memcmp(canonical.pointer, compared.pointer, canonical.length) != 0))
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
                plain.parenthesis_count = 0;
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

static String8 check_driver_warning_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                         CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling)
{
    return check_driver_warning_type_at(parser, pp, input, types, spell, scratch, type, spelling, BUSTER_CHECK_NONE, 0);
}

// The type sugar of a warning operand (#1447, c_types_sugar.c): its declaration's (check_input_declared_sugar), through the
// addresses, indirections and subscripts whose levels the sugar's printer finds (type_sugar_print); 0 for none.
static uint32_t check_driver_operand_sugar(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = stream->graph;
    uint32_t result = 0;
    bool walking = input->sugar_entries != 0;
    for (uint32_t steps = 0; walking && node < graph->count && steps < 16; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        const uint32_t declaration = op == CHECK_OP_VALUE && token < input->token_count && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
        result = declaration < input->token_count ? check_input_declared_sugar(input, declaration) : 0;
        walking = op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT;
        node = walking ? graph->operand0[node] : node;
    }
    return result;
}

// A warning's type as its declaration's sugar writes it (#1447, type_sugar_print), quoted: `'MS void (*)(void)'`, with
// each MACRO node's list resolved by the macro that writes it (check_driver_attribute_macro). Like clang's, no a.k.a.:
// desugarForDiagnostic doesn't look through attributed, macro-qualified or parenthesized types. Empty where `sugar` is 0
// or doesn't describe `type`, where the caller prints it as before.
static String8 check_driver_sugar_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, CheckWarningSpelling* spell,
                                       Arena* scratch, TypeId type, uint32_t sugar)
{
    String8 result = { 0 };
    if (sugar != 0 && input != NULL && input->sugar.slots != NULL && type != 0 && check_driver_spelling_ready(parser, pp, input, types, spell, scratch))
    {
        uint32_t tokens[8];
        String8 texts[8];
        const uint32_t count = type_sugar_macro_tokens(types, &input->sugar, sugar, type, tokens, 8);
        const bool history = count != 0 && pp_history_ensure(pp);
        for (uint32_t i = 0; i < count; i += 1)
        {
            // (A MACRO node's word is its list's token plus one.)
            const uint32_t token = tokens[i] - 1;
            const bool list = token + 1 < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_ATTRIBUTE &&
                              parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN;
            const uint32_t close = list ? parser->tree.parents[token + 1] : token;
            texts[i] = history && token < parser->tokens.count && close < parser->tokens.count ? check_driver_attribute_macro(parser, pp, token, close) : (String8){ 0 };
        }
        const TypeNames names = {
            .bytes = spell->bytes,
            .lengths = spell->lengths,
            .count = spell->count,
            .anonymous = spell->anonymous,
            .anonymous_count = spell->anonymous_count,
            .sugar_macro_tokens = tokens,
            .sugar_macro_texts = texts,
            .sugar_macro_count = count,
        };
        const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
        const String8 text = type_sugar_print(types, &input->sugar, scratch, sugar, type, (String8){ 0 }, &names, options);
        const char* quoted = text.length != 0 ? check_warning_text(scratch, "'%.*s'", (int)text.length, text.pointer) : NULL;
        result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = (uint64_t)strlen(quoted) } : (String8){ 0 };
    }
    return result;
}

// The typedefs by place of one declaration's types (#1447 item 5, TypeNames.alias_order), as clang's sugar keeps a
// parameter's: entry k of `order` the alias of the k-th specifier sequence type_print prints, in the order a C declaration
// writes them (the declaration's own specifiers, then each nested parameter's), UINT32_MAX for none.
#define BUSTER_CHECK_DRIVER_PLACES 32u
typedef struct CheckDriverPlaces CheckDriverPlaces;
struct CheckDriverPlaces
{
    TypeId types[BUSTER_CHECK_DRIVER_PLACES];
    uint32_t covered[BUSTER_CHECK_DRIVER_PLACES];
    String8 names[BUSTER_CHECK_DRIVER_PLACES];
    uint32_t count;
    uint32_t order[BUSTER_CHECK_DRIVER_PLACES];
    uint32_t order_count;
    // Whether a parameter's specifiers name a typedef (an order entry past the first has an alias).
    bool parameter;
    bool valid;
};

// The typedef declaration a specifier token names, or BUSTER_CHECK_NONE.
static uint32_t check_driver_place_typedef(const Parser* parser, const CheckInput* input, uint32_t t)
{
    const bool identifier = t < parser->tokens.count && t < input->token_count && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER;
    const uint32_t resolved = identifier ? input->declarations[t] : BUSTER_CHECK_NONE;
    const bool named = resolved != t && resolved < input->token_count && input->declaration_kinds[resolved] == C_BINDING_TYPEDEF;
    return named ? resolved : BUSTER_CHECK_NONE;
}

// Whether token `t` starts a parameter's specifiers: a type keyword, a tag, a qualifier, a storage class, _Atomic, typeof,
// _Alignas, an attribute or a typedef's name.
static bool check_driver_place_starts(const Parser* parser, const CheckInput* input, uint32_t t)
{
    const uint8_t kind = t < parser->tokens.count ? parser->tokens.kinds[t] : PARSE_KIND_END;
    return kind == PARSE_KIND_TYPE || kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_QUALIFIER || kind == PARSE_KIND_STORAGE ||
           kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ALIGNAS || kind == PARSE_KIND_ATTRIBUTE ||
           check_driver_place_typedef(parser, input, t) != BUSTER_CHECK_NONE;
}

// A qualifier keyword's qualifier (const, volatile, restrict and their underscored forms), or 0.
static uint32_t check_driver_place_qualifier(const Parser* parser, Preprocessor* pp, uint32_t t)
{
    const uint32_t identifier = parser->tokens.kinds[t] == PARSE_KIND_QUALIFIER ? parser->tokens.names[t] : 0;
    const char* spelling = identifier != 0 ? (const char*)intern_bytes(pp->table, identifier) : "";
    return strstr(spelling, "const") != NULL ? BUSTER_TYPE_CONST : strstr(spelling, "volatile") != NULL ? BUSTER_TYPE_VOLATILE :
           strstr(spelling, "restrict") != NULL ? BUSTER_TYPE_RESTRICT : 0;
}

// The tokens of declaration `name`'s own declarator and specifiers: a parameter's from the `(` or `,` before it, any other
// from its declaration's start (whose siblings' declarators the walk drops), to the `,`, `;`, `=` or body after it, or
// its parameter list's `)`. A grouping parenthesis around it is crossed.
static void check_driver_place_extent(const Parser* parser, uint32_t name, uint32_t* begin, uint32_t* end)
{
    const uint32_t start = check_input_declaration_begin(parser, name);
    uint32_t depth = 0;
    uint32_t comma = BUSTER_CHECK_NONE;
    uint32_t list = BUSTER_CHECK_NONE;
    for (uint32_t t = name; t > start && list == BUSTER_CHECK_NONE;)
    {
        t -= 1;
        const uint8_t kind = parser->tokens.kinds[t];
        const uint8_t role = parser->tree.roles[t];
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        list = opener && depth == 0 && role == PARSE_ROLE_OPEN_FUNCTION ? t : list;
        comma = kind == PARSE_KIND_COMMA && depth == 0 && comma == BUSTER_CHECK_NONE ? t : comma;
        depth = closer ? depth + 1 : opener && depth != 0 ? depth - 1 : depth;
    }
    *begin = list == BUSTER_CHECK_NONE ? start : (comma != BUSTER_CHECK_NONE ? comma : list) + 1;
    depth = 0;
    uint32_t t = name + 1;
    for (bool scanning = true; scanning && t < parser->tokens.count;)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint8_t role = parser->tree.roles[t];
        const bool opener = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || (kind == PARSE_KIND_L_BRACE && role != PARSE_ROLE_OPEN_BODY);
        const bool closer = kind == PARSE_KIND_R_PAREN || kind == PARSE_KIND_R_SQUARE || kind == PARSE_KIND_R_BRACE;
        const bool ends = depth == 0 && (kind == PARSE_KIND_COMMA || kind == PARSE_KIND_SEMI || kind == PARSE_KIND_EQUAL || role == PARSE_ROLE_OPEN_BODY ||
                                         (closer && role != PARSE_ROLE_CLOSE_GROUP));
        scanning = !ends;
        depth = opener ? depth + 1 : closer && depth != 0 ? depth - 1 : depth;
        t += scanning ? 1 : 0;
    }
    *end = t;
}

// The places of declaration `name` (CheckDriverPlaces), as __builtin_dump_struct's printer reads them
// (check_lower_dump_places): a parameter list's `(` opens one place per parameter (a `(void)` and `...` none), any other
// `(` groups, but an attribute's, _Alignas's and typeof's, whose tokens are skipped; a record body is skipped; a `,`
// outside every parenthesis starts a sibling declarator, whose places aren't this one's. Invalid where it holds more than
// the room.
static void check_driver_places(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, uint32_t name,
                                CheckDriverPlaces* places)
{
    uint8_t groups[BUSTER_CHECK_DRIVER_PLACES];
    uint32_t lists[BUSTER_CHECK_DRIVER_PLACES];
    uint32_t group_depth = 0;
    uint32_t list_depth = 0;
    uint32_t braces = 0;
    uint32_t skipped = 0;
    uint32_t top = 0;
    uint32_t begin = 0;
    uint32_t end = 0;
    *places = (CheckDriverPlaces){ .order = { UINT32_MAX }, .order_count = 1 };
    places->valid = name < parser->tokens.count && name < input->token_count && input->declarations != NULL && input->declaration_kinds != NULL &&
                    input->declared_types != NULL;
    if (places->valid)
    {
        check_driver_place_extent(parser, name, &begin, &end);
    }
    for (uint32_t t = begin; places->valid && t < end && t < input->token_count; t += 1)
    {
        const uint8_t kind = parser->tokens.kinds[t];
        const uint8_t previous = t > begin ? parser->tokens.kinds[t - 1] : PARSE_KIND_END;
        if (braces != 0 || kind == PARSE_KIND_L_BRACE)
        {
            braces = kind == PARSE_KIND_L_BRACE ? braces + 1 : kind == PARSE_KIND_R_BRACE ? braces - 1 : braces;
        }
        else if (skipped != 0 || (kind == PARSE_KIND_L_PAREN && (previous == PARSE_KIND_ATTRIBUTE || previous == PARSE_KIND_ALIGNAS || previous == PARSE_KIND_TYPEOF)))
        {
            skipped = kind == PARSE_KIND_L_PAREN ? skipped + 1 : kind == PARSE_KIND_R_PAREN ? skipped - 1 : skipped;
        }
        else if (kind == PARSE_KIND_L_PAREN)
        {
            const uint32_t identifier = t + 1 < end ? parser->tokens.names[t + 1] : 0;
            const bool none = t + 2 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_TYPE && parser->tokens.kinds[t + 2] == PARSE_KIND_R_PAREN &&
                              intern_length(pp->table, identifier) == 4 && memcmp(intern_bytes(pp->table, identifier), "void", 4) == 0;
            const bool list = previous != PARSE_KIND_ATOMIC && !none && check_driver_place_starts(parser, input, t + 1);
            places->valid = group_depth < BUSTER_CHECK_DRIVER_PLACES &&
                            (!list || (list_depth < BUSTER_CHECK_DRIVER_PLACES && places->order_count < BUSTER_CHECK_DRIVER_PLACES));
            if (places->valid)
            {
                groups[group_depth++] = list;
            }
            if (places->valid && list)
            {
                lists[list_depth++] = top;
                top = places->order_count;
                places->order[places->order_count++] = UINT32_MAX;
            }
        }
        else if (kind == PARSE_KIND_R_PAREN && group_depth != 0)
        {
            group_depth -= 1;
            if (groups[group_depth] && list_depth != 0)
            {
                top = lists[--list_depth];
            }
        }
        else if (kind == PARSE_KIND_COMMA && group_depth == 0)
        {
            // A sibling declarator's: its places are dropped, the shared specifiers' kept.
            places->order_count = 1;
            top = 0;
        }
        else if (kind == PARSE_KIND_COMMA && groups[group_depth - 1])
        {
            places->valid = places->order_count < BUSTER_CHECK_DRIVER_PLACES;
            top = places->order_count;
            places->order[places->valid ? places->order_count++ : 0] = UINT32_MAX;
        }
        else if (kind == PARSE_KIND_ELLIPSIS && top == places->order_count - 1 && places->order[top] == UINT32_MAX)
        {
            // `...` is no parameter's specifiers.
            places->order_count -= 1;
            top = UINT32_MAX;
        }
        else if (top < places->order_count && places->order[top] == UINT32_MAX && check_driver_place_typedef(parser, input, t) != BUSTER_CHECK_NONE)
        {
            // Its typedef's type, unqualified, under the qualifiers it covers: its own, but those the declaration writes
            // beside it (`const J`), which print before its name.
            const uint32_t resolved = check_driver_place_typedef(parser, input, t);
            uint32_t written = 0;
            for (uint32_t k = t; k > begin && (parser->tokens.kinds[k - 1] == PARSE_KIND_QUALIFIER || parser->tokens.kinds[k - 1] == PARSE_KIND_STORAGE); k -= 1)
            {
                written |= check_driver_place_qualifier(parser, pp, k - 1);
            }
            for (uint32_t k = t + 1; k < end && parser->tokens.kinds[k] == PARSE_KIND_QUALIFIER; k += 1)
            {
                written |= check_driver_place_qualifier(parser, pp, k);
            }
            const TypeId declared = type_strip_aligned(types, input->declared_types[resolved]);
            const uint32_t identifier = parser->tokens.names[t];
            places->valid = places->count < BUSTER_CHECK_DRIVER_PLACES && type_is_valid(types, declared);
            if (places->valid)
            {
                places->types[places->count] = type_unqualified(declared);
                places->covered[places->count] = type_qualifiers(declared) & ~written;
                places->names[places->count] = (String8){ .pointer = (char*)intern_bytes(pp->table, identifier), .length = intern_length(pp->table, identifier) };
                places->parameter |= top != 0;
                places->order[top] = places->count++;
            }
        }
    }
}

// A warning operand's type as declaration `name` writes it where a parameter's specifiers name a typedef (#1447 item 5),
// with clang's a.k.a. of the type without them: `'void (*)(I)' (aka 'void (*)(int)')`. Empty where none does, or the
// places don't match the specifiers the printer prints (a level the declaration doesn't spell), where the caller prints
// the type as before.
static String8 check_driver_placed_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, CheckWarningSpelling* spell,
                                        Arena* scratch, TypeId type, uint32_t name)
{
    String8 result = { 0 };
    CheckDriverPlaces places = { 0 };
    const bool candidate = name < input->token_count && type != 0 && type_is_valid(types, type);
    if (candidate)
    {
        check_driver_places(parser, pp, input, types, name, &places);
    }
    if (candidate && places.valid && places.parameter && check_driver_spelling_ready(parser, pp, input, types, spell, scratch))
    {
        uint64_t bound_ids[BUSTER_CHECK_DRIVER_VLA_BOUNDS];
        String8 bound_texts[BUSTER_CHECK_DRIVER_VLA_BOUNDS];
        uint32_t printed = UINT32_MAX;
        TypeNames names = {
            .bytes = spell->bytes,
            .lengths = spell->lengths,
            .count = spell->count,
            .anonymous = spell->anonymous,
            .anonymous_count = spell->anonymous_count,
            .bound_ids = bound_ids,
            .bound_texts = bound_texts,
            .bound_count = check_driver_vla_bounds(parser, pp, types, type, bound_ids, bound_texts, scratch),
            .space_macro_spaces = spell->space_macro_spaces,
            .space_macro_texts = spell->space_macro_texts,
            .space_macro_count = spell->space_macro_count,
        };
        const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
        // The a.k.a. drops the typedefs and a macro's name, as desugarForDiagnostic does.
        TypeNames plain = names;
        plain.space_macro_count = 0;
        const String8 canonical = type_print(types, scratch, type, (String8){ 0 }, &plain, options);
        names.alias_types = places.types;
        names.alias_names = places.names;
        names.alias_covered = places.covered;
        names.alias_count = places.count;
        names.alias_order = places.order;
        names.alias_order_count = places.order_count;
        names.alias_order_printed = &printed;
        const String8 sugared = type_print(types, scratch, type, (String8){ 0 }, &names, options);
        const bool differs = sugared.length != canonical.length || memcmp(sugared.pointer, canonical.pointer, sugared.length) != 0;
        const char* text = printed == places.order_count && sugared.length != 0 && canonical.length != 0 && differs ?
                           check_warning_text(scratch, "'%.*s' (aka '%.*s')", (int)sugared.length, sugared.pointer, (int)canonical.length, canonical.pointer) : NULL;
        result = text != NULL ? (String8){ .pointer = (char*)text, .length = strlen(text) } : (String8){ 0 };
    }
    return result;
}

// The declaration a warning operand names (#1447 item 5), through conversions, addresses, indirections and subscripts:
// its declared name's token, or BUSTER_CHECK_NONE.
static uint32_t check_driver_operand_declaration(const CheckInput* input, const CheckStream* stream, uint32_t node)
{
    const CheckGraph* graph = stream->graph;
    uint32_t result = BUSTER_CHECK_NONE;
    bool walking = input->declarations != NULL;
    for (uint32_t steps = 0; walking && node < graph->count && steps < 16; steps += 1)
    {
        const CheckOp op = graph->ops[node];
        const uint32_t token = graph->tokens[node];
        result = op == CHECK_OP_VALUE && token < input->token_count ? input->declarations[token] : BUSTER_CHECK_NONE;
        walking = op == CHECK_OP_CONVERT || op == CHECK_OP_ADDRESS || op == CHECK_OP_DEREFERENCE || op == CHECK_OP_SUBSCRIPT;
        node = walking ? graph->operand0[node] : node;
    }
    return result;
}

// A counted_by family attribute's keyword as clang's CountAttributedType prints it (#1842's 3c), by its
// TypeMember.count_kind less one: `__counted_by`, `__sized_by`, `__counted_by_or_null`, `__sized_by_or_null`.
static const char* const check_driver_counted_names[] = { "counted_by", "sized_by", "counted_by_or_null", "sized_by_or_null" };

// A warning operand's type with clang's CountAttributedType sugar where it is a member a counted_by family attribute
// counts (#1842's 3c), through an array's decay and one `&`, quoted with its a.k.a.: `'int * __counted_by(n)' (aka
// 'int *')`, `'int[] __counted_by(n)' (aka 'int[]')`, `&b->p`'s `'int * __counted_by(n)*' (aka 'int **')`. As clang's
// printer places it, the attribute follows a pointer's before the placeholder and an array's after it. Empty for any
// other operand, which prints as before.
static String8 check_driver_counted_type(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckStream* stream, const TypeTable* types,
                                         CheckWarningSpelling* spell, Arena* scratch, uint32_t node, TypeId type)
{
    const CheckGraph* graph = stream->graph;
    String8 result = { 0 };
    uint32_t addresses = 0;
    for (uint32_t steps = 0; node < graph->count && steps < 8 && (graph->ops[node] == CHECK_OP_CONVERT || graph->ops[node] == CHECK_OP_ADDRESS); steps += 1)
    {
        addresses += graph->ops[node] == CHECK_OP_ADDRESS ? 1 : 0;
        node = graph->operand0[node];
    }
    const bool member = node < graph->count && addresses <= 1 && (graph->ops[node] == CHECK_OP_MEMBER || graph->ops[node] == CHECK_OP_ARROW) && type != 0;
    const uint32_t selected = member ? check_semantic_member_of(parser, input, stream, types, node) : 0;
    // The member's count, which the reader kept (TypeMember.count_kind, count_name).
    const uint32_t kind = selected != 0 && selected < types->members.count ?
                          (uint32_t)((types->members.flags[selected] & BUSTER_TYPE_MEMBER_COUNT_MASK) >> BUSTER_TYPE_MEMBER_COUNT_SHIFT) : BUSTER_TYPE_COUNT_NONE;
    const bool counted = kind != BUSTER_TYPE_COUNT_NONE && kind <= BUSTER_ARRAY_LENGTH(check_driver_counted_names) && types->members.count_name[selected] != 0 &&
                         check_driver_spelling_ready(parser, pp, input, types, spell, scratch);
    if (counted)
    {
        const TypeNames names = { .bytes = spell->bytes, .lengths = spell->lengths, .count = spell->count, .anonymous = spell->anonymous, .anonymous_count = spell->anonymous_count };
        const uint32_t options = pp->options.language.standard >= 2023 ? BUSTER_TYPE_PRINT_C23 : 0;
        const TypeId inner = types->members.type[selected];
        const bool array = type_kind_is_array(type_kind(types, type_strip_aligned(types, inner)));
        const String8 count = check_driver_warning_name(pp, types->members.count_name[selected]);
        const char* attribute = check_warning_text(scratch, " __%s(%.*s)", check_driver_counted_names[kind - 1], (int)count.length, count.pointer);
        const char mark[1] = { BUSTER_TYPE_SUGAR_MARK };
        const String8 text = type_print(types, scratch, inner, addresses != 0 ? (String8){ .pointer = (char*)mark, .length = 1 } : (String8){ 0 }, &names, options);
        uint64_t split = text.length;
        for (uint64_t b = 0; addresses != 0 && b < text.length && split == text.length; b += 1)
        {
            split = text.pointer[b] == BUSTER_TYPE_SUGAR_MARK ? b : split;
        }
        const String8 before = { .pointer = text.pointer, .length = split };
        const String8 after = split < text.length ? (String8){ .pointer = text.pointer + split + 1, .length = text.length - split - 1 } : (String8){ 0 };
        const String8 canonical = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, 0);
        const char* quoted = text.length != 0 && canonical.length != 0 ?
                             check_warning_text(scratch, "'%.*s%s%s%.*s%s' (aka %.*s)", (int)before.length, before.pointer, array ? "" : attribute, addresses != 0 ? "*" : "",
                                                (int)after.length, after.pointer, array ? attribute : "", (int)canonical.length, canonical.pointer) : NULL;
        result = quoted != NULL ? (String8){ .pointer = (char*)quoted, .length = strlen(quoted) } : (String8){ 0 };
    }
    return result;
}

// The spelling's address space macros while one declaration's type prints (#1447 item 2, check_driver_space_scope).
typedef struct CheckDriverSpaceScope CheckDriverSpaceScope;
struct CheckDriverSpaceScope
{
    uint32_t* spaces;
    String8* texts;
    uint32_t count;
    bool scoped;
};

// Narrows the pass's address space macros (check_driver_space_macros) to declaration `name`'s own spelling while its
// type prints, as clang's MacroQualifiedType names each declaration's: a space its declaration writes by a macro prints
// by that macro (`ASA int *` where another declaration's `AS1` writes the same space), one it writes directly prints
// spelled out, and a space it doesn't write prints as the unit's macros give it. The previous macros go into `*saved`,
// which check_driver_space_restore puts back. Nothing changes where `name` is none, the unit writes no space, or an
// operand isn't an integer constant.
static void check_driver_space_scope(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, CheckWarningSpelling* spell,
                                     uint32_t name, CheckDriverSpaceScope* saved, Arena* scratch)
{
    *saved = (CheckDriverSpaceScope){ 0 };
    const bool ready = name < parser->tokens.count && name < input->token_count && check_driver_spelling_ready(parser, pp, input, types, spell, scratch) &&
                       spell->space_macro_spaces != NULL && !spell->space_unreadable && pp_history_ensure(pp);
    uint32_t begin = 0;
    uint32_t end = 0;
    if (ready)
    {
        check_driver_place_extent(parser, name, &begin, &end);
    }
    uint32_t spaces[BUSTER_CHECK_DRIVER_SPACE_MACROS];
    String8 texts[BUSTER_CHECK_DRIVER_SPACE_MACROS];
    uint32_t count = 0;
    bool unreadable = false;
    for (uint32_t t = begin; ready && t + 1 < end; t += 1)
    {
        const uint32_t close = parser->tokens.kinds[t] == PARSE_KIND_ATTRIBUTE && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN ? parser->tree.parents[t + 1] :
                               BUSTER_CHECK_NONE;
        if (close < parser->tokens.count)
        {
            check_driver_space_list(parser, pp, t, close, spaces, texts, &count, &unreadable, scratch);
            t = close;
        }
    }
    uint32_t* merged = count != 0 && !unreadable ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_CHECK_DRIVER_SPACE_MACROS * 2) : NULL;
    String8* merged_texts = merged != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, BUSTER_CHECK_DRIVER_SPACE_MACROS * 2) : NULL;
    if (merged_texts != NULL)
    {
        uint32_t kept = 0;
        // The declaration's own: its first spelling of each space, a macro's kept, a direct one's spelled out.
        for (uint32_t i = 0; i < count; i += 1)
        {
            bool first = true;
            for (uint32_t k = 0; k < i; k += 1)
            {
                first &= spaces[k] != spaces[i];
            }
            if (first && texts[i].length != 0)
            {
                merged[kept] = spaces[i];
                merged_texts[kept] = texts[i];
                kept += 1;
            }
        }
        // Then the unit's, for the spaces it doesn't write.
        for (uint32_t u = 0; u < spell->space_macro_count; u += 1)
        {
            bool written = false;
            for (uint32_t i = 0; i < count; i += 1)
            {
                written |= spaces[i] == spell->space_macro_spaces[u];
            }
            if (!written)
            {
                merged[kept] = spell->space_macro_spaces[u];
                merged_texts[kept] = spell->space_macro_texts[u];
                kept += 1;
            }
        }
        *saved = (CheckDriverSpaceScope){ .spaces = spell->space_macro_spaces, .texts = spell->space_macro_texts, .count = spell->space_macro_count, .scoped = true };
        spell->space_macro_spaces = merged;
        spell->space_macro_texts = merged_texts;
        spell->space_macro_count = kept;
    }
}

static void check_driver_space_restore(CheckWarningSpelling* spell, const CheckDriverSpaceScope* saved)
{
    if (saved->scoped)
    {
        spell->space_macro_spaces = saved->spaces;
        spell->space_macro_texts = saved->texts;
        spell->space_macro_count = saved->count;
    }
}

// A warning operand's type (check_driver_warning_type) with its declaration's own address space macros
// (check_driver_space_scope, #1447 item 2): `declaration` the operand's (check_driver_operand_declaration).
static String8 check_driver_warning_type_declared(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                  CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling, uint32_t declaration)
{
    CheckDriverSpaceScope saved;
    check_driver_space_scope(parser, pp, input, types, spell, declaration, &saved, scratch);
    const String8 result = check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
    check_driver_space_restore(spell, &saved);
    return result;
}

// A warning operand's type (check_driver_warning_type), as its sugar writes it where it has some and no typedef or typeof
// spelling (check_driver_sugar_type).
static String8 check_driver_warning_type_sugared(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                 CheckWarningSpelling* spell, Arena* scratch, TypeId type, uint32_t spelling, uint32_t sugar)
{
    const String8 sugared = spelling == 0 ? check_driver_sugar_type(parser, pp, input, types, spell, scratch, type, sugar) : (String8){ 0 };
    return sugared.length != 0 ? sugared : check_driver_warning_type(parser, pp, input, types, spell, scratch, type, spelling);
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

// The macro name clang gives an objc_gc's or objc_ownership's warning that its type doesn't take it (#1447, Sema's
// diagnoseBadTypeAttribute): "__strong" for a strong one, "__weak" for a weak one, where the outermost macro that wrote it
// is named so (pp_output_macro_place), and its place there into *location and *origin; else NULL, and the token's own.
static const char* check_warning_objc_macro(const Parser* parser, Preprocessor* pp, const CheckWarningEvent* event, uint32_t* location, uint32_t* origin)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t argument = event->token + 2;
    const bool argued = (event->kind == CHECK_WARNING_EVENT_OBJC_POINTER_TYPE || event->kind == CHECK_WARNING_EVENT_OBJC_OBJECT_TYPE) && argument < count &&
                        parser->tokens.kinds[event->token + 1] == PARSE_KIND_L_PAREN && parser->tokens.kinds[argument] == PARSE_KIND_IDENTIFIER;
    const uint32_t word = argued ? parser->tokens.names[argument] : 0;
    const bool strong = word != 0 && intern_length(pp->table, word) == 6 && memcmp(intern_bytes(pp->table, word), "strong", 6) == 0;
    const bool weak = word != 0 && intern_length(pp->table, word) == 4 && memcmp(intern_bytes(pp->table, word), "weak", 4) == 0;
    const char* macro = strong ? "__strong" : weak ? "__weak" : NULL;
    const uint32_t source = event->source != BUSTER_CHECK_WARNING_NO_SOURCE ? event->source : parser->tokens.sources[event->token];
    const bool lifted = pp_output_macro_place(pp, source, event->kind == CHECK_WARNING_EVENT_OBJC_WEAK_TYPE ? NULL : macro != NULL ? macro : "", location, origin);
    return lifted && event->kind != CHECK_WARNING_EVENT_OBJC_WEAK_TYPE ? macro : NULL;
}

// c_check_semantic_diagnostic.c's: a record's name as clang's -Wodr and incompatible-definition errors spell it.
static String8 check_semantic_tag_spelling(const Parser* parser, Preprocessor* pp, uint32_t token, Arena* scratch);

static const char* check_driver_warning_message(const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types,
                                                CheckWarningSpelling* spell, const CheckWarningEvent* event, Arena* scratch)
{
    const CheckWarningPayload* payload = &event->payload;
    const String8 name = check_driver_warning_name(pp, check_driver_corrected_name(parser, input, event->token, payload->name));
    const char* result = event->id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[event->id].message : NULL;
    switch (event->kind)
    {
        case CHECK_WARNING_EVENT_DUPLICATE_DECLSPEC:
            // _Complex is "complex" in clang's words (DeclSpec::getSpecifierName), as its other errors name it.
            result = payload->name == C_NAME_COMPLEX_C99 ? "duplicate 'complex' declaration specifier" :
                     check_warning_text(scratch, "duplicate '%.*s' declaration specifier", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_NORETURN_RETURN:
            result = check_warning_text(scratch, "function '%.*s' declared 'noreturn' should not return", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_NORETURN_FALLS:
            result = "function declared 'noreturn' should not return";
            break;
        case CHECK_WARNING_EVENT_PRAGMA:
        {
            const CheckInputSymbolPragma* record = check_input_symbol_warning(input, event);
            const CheckInputMsPragma* layout = check_input_ms_warning(input, event);
            const CheckInputPackPragma* packing = check_input_pack_warning(input, event);
            result = packing != NULL ? check_input_pack_message(packing, event, scratch) : result;
            if (layout != NULL)
            {
                result = layout->warning == 1 ? "incorrect use of '#pragma ms_struct on|off' - ignored" :
                                               "extra tokens at end of '#pragma ms_struct' - ignored";
            }
            if (record != NULL)
            {
                const char* handler = record->kind == CHECK_SYMBOL_PRAGMA_REDEFINE ? "redefine_extname" : "weak";
                if (record->warning == CHECK_SYMBOL_WARNING_IDENTIFIER || record->warning == CHECK_SYMBOL_WARNING_EXTRA)
                {
                    result = check_warning_text(scratch, "%s '#pragma %s' - ignored",
                        record->warning == CHECK_SYMBOL_WARNING_IDENTIFIER ? "expected identifier in" : "extra tokens at end of", handler);
                }
                else if (record->warning == CHECK_SYMBOL_WARNING_UNDECLARED)
                {
                    result = check_warning_text(scratch, "weak identifier '%.*s' never declared", (int)name.length, name.pointer);
                }
                else if (record->warning == CHECK_SYMBOL_WARNING_TYPEDEF)
                {
                    result = "'weak' attribute only applies to variables and functions";
                }
                else if (record->warning == CHECK_SYMBOL_WARNING_INTERNAL)
                {
                    const bool function = record->declaration < input->token_count && input->declaration_kinds[record->declaration] == C_BINDING_FUNCTION;
                    result = check_warning_text(scratch, "#pragma redefine_extname is applicable to external C declarations only; not applied to %s '%.*s'",
                                                function ? "function" : "variable", (int)name.length, name.pointer);
                }
            }
            break;
        }
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
                               type == BUSTER_TYPE_LONG_DOUBLE ? "long double" : type == BUSTER_TYPE_FLOAT16 ? "_Float16" :
                               type == BUSTER_TYPE_FLOAT128 ? "__float128" : NULL;
            const char* limit = type == BUSTER_TYPE_FLOAT ? (large ? "3.40282347E+38" : "1.40129846E-45") :
                                type == BUSTER_TYPE_DOUBLE ? (large ? "1.7976931348623157E+308" : "4.9406564584124654E-324") :
                                type == BUSTER_TYPE_LONG_DOUBLE ? (large ? "1.18973149535723176502E+4932" : "3.64519953188247460253E-4951") :
                                type == BUSTER_TYPE_FLOAT16 ? (large ? "65504" : "5.9605E-8") :
                                type == BUSTER_TYPE_FLOAT128 ? (large ? "1.18973149535723176508575932662800702E+4932" : "6.47517511943802511092443895822764655E-4966") :
                                NULL;
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
        case CHECK_WARNING_EVENT_PROTOTYPE_TAG:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, 0);
            result = type.length != 0 ? check_warning_text(scratch, "declaration of %.*s will not be visible outside of this function", (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_PROTOTYPE_REDEFINITION:
            result = check_warning_text(scratch, "redefinition of '%.*s' will not be visible outside of this function", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_ODR_ATTRIBUTE:
        {
            const String8 spelled = check_semantic_tag_spelling(parser, pp, event->token, scratch);
            result = spelled.length != 0 ? check_warning_text(scratch, "type '%.*s' has %s which currently causes the types to be treated as though they are incompatible",
                                                              (int)spelled.length, spelled.pointer, payload->data != 0 ? "a member with an attribute" : "an attribute") : NULL;
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
        case CHECK_WARNING_EVENT_SWIFT_NAME:
        {
            // swift_name's and swift_async_name's warnings (#1447, check_input_attribute_swift_name), the attribute as written.
            static const char* const endings[] = {
                "argument must be a string literal specifying a Swift function name",
                "has invalid identifier for the base name",
                "has invalid identifier for the context name",
                "has invalid identifier for the parameter name",
                "cannot be applied to this declaration",
                "for 'subscript' must be a getter or setter",
                "for 'subscript' must have at least one parameter",
                "for 'subscript' must have a 'self:' parameter",
                "is missing parameter label clause",
                "for setter must have one parameter for new value",
                "cannot specify more than one 'self:' parameter",
                "for getter must not have any parameters besides 'self:'",
                "for 'subscript' setter must have a 'newValue:' parameter",
                "for 'subscript' setter cannot have multiple 'newValue:' parameters",
                "for 'subscript' getter cannot have a 'newValue:' parameter",
                "",
                "cannot be applied to a function with no parameters",
                "only applies to non-K&R-style functions",
            };
            const uint32_t code = payload->data & 0x1fu;
            const uint32_t expected = (payload->data >> 5) & 0x1fffu;
            const uint32_t given = (payload->data >> 18) & 0x1fffu;
            result = code == BUSTER_CHECK_SWIFT_NAME_COUNT ?
                     check_warning_text(scratch, "too %s parameters in the signature specified by the '%.*s' attribute (expected %u; got %u)",
                                        given > expected ? "many" : "few", (int)name.length, name.pointer, expected, given) :
                     code < BUSTER_ARRAY_LENGTH(endings) ? check_warning_text(scratch, "'%.*s' attribute %s", (int)name.length, name.pointer, endings[code]) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE:
        {
            const String8 type = check_driver_warning_type_at(parser, pp, input, types, spell, scratch, payload->first_type, payload->data,
                                                              payload->token, payload->second_type);
            result = type.length != 0 ? check_warning_text(scratch, "'%.*s' only applies to function types; type here is %.*s", (int)name.length, name.pointer,
                                                           (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_OBJC_POINTER_TYPE:
        case CHECK_WARNING_EVENT_OBJC_OBJECT_TYPE:
        case CHECK_WARNING_EVENT_OBJC_WEAK_TYPE:
        {
            const String8 type = check_driver_warning_type_at(parser, pp, input, types, spell, scratch, payload->first_type, payload->data,
                                                              payload->token, payload->second_type);
            uint32_t location = 0;
            uint32_t origin = 0;
            const char* macro = check_warning_objc_macro(parser, pp, event, &location, &origin);
            const String8 named = event->kind == CHECK_WARNING_EVENT_OBJC_WEAK_TYPE ? (String8){ "__weak", 6 } :
                                  macro != NULL ? (String8){ (char*)macro, strlen(macro) } : name;
            result = type.length != 0 ? check_warning_text(scratch, "'%.*s' only applies to %s types; type here is %.*s", (int)named.length, named.pointer,
                                                           event->kind == CHECK_WARNING_EVENT_OBJC_POINTER_TYPE ? "pointer" : "Objective-C object or block pointer",
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
            // A C23 attribute's scope is at payload.token (#1447), its spelling normalized.
            static const char* const keywords[] = { "struct", "union", "enum" };
            const bool scoped = payload->token != BUSTER_CHECK_WARNING_NO_SOURCE && payload->token < parser->tokens.count;
            const String8 scope = scoped ? check_warning_attribute_scope(check_driver_warning_name(pp, parser->tokens.names[payload->token])) : (String8){ "", 0 };
            const String8 attribute = scoped ? check_warning_attribute_name(name, scope, true) : name;
            result = check_warning_text(scratch, "attribute '%.*s%s%.*s' is ignored, place it after \"%s\" to apply attribute to type declaration", (int)scope.length,
                                        scope.pointer, scoped ? "::" : "", (int)attribute.length, attribute.pointer, keywords[payload->data < 3 ? payload->data : 0]);
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
        case CHECK_WARNING_EVENT_NODEREF_PLACEMENT:
            result = "'noderef' can only be used on an array or pointer type";
            break;
        case CHECK_WARNING_EVENT_NODEREF_DEREFERENCE:
            result = name.length == 0 ? "dereferencing expression marked as 'noderef'" :
                     check_warning_text(scratch, "dereferencing %.*s; was declared with a 'noderef' type", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_EVENT_NODEREF_CONVERSION:
            result = "casting to dereferenceable pointer removes 'noderef' attribute";
            break;
        case CHECK_WARNING_EVENT_AVAILABILITY_PLATFORM:
        {
            const uint32_t at = payload->token < parser->tokens.count ? parser->tokens.names[payload->token] : 0;
            const String8 platform = check_driver_warning_name(pp, at);
            result = check_warning_text(scratch, "unknown platform '%.*s' in availability macro", (int)platform.length, platform.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_AVAILABILITY_UNAVAILABLE:
            result = "'unavailable' availability overrides all other availability information";
            break;
        case CHECK_WARNING_EVENT_AVAILABILITY_SWIFT:
            result = "only 'unavailable' and 'deprecated' are supported for Swift availability";
            break;
        case CHECK_WARNING_EVENT_AVAILABILITY_FUCHSIA:
            result = "Fuchsia API Level prohibits specifying a minor or sub-minor version";
            break;
        case CHECK_WARNING_EVENT_AVAILABILITY_MISMATCH:
            result = "availability does not match previous declaration";
            break;
        case CHECK_WARNING_EVENT_AVAILABILITY_VERSION:
        case CHECK_WARNING_EVENT_AVAILABILITY_ORDERING:
        {
            // The platform's pretty name (or its name as written), and the versions as VersionTuple prints them.
            static const char* const stages[] = { "introduced", "deprecated", "obsoleted" };
            const CheckInputAvailabilityText* text = payload->data < input->availability_text_count ? input->availability_texts + payload->data : NULL;
            const uint32_t at = text != NULL && text->platform < parser->tokens.count ? parser->tokens.names[text->platform] : 0;
            const String8 written = check_driver_warning_name(pp, at);
            bool xros = false;
            const uint32_t platform = check_availability_attribute_platform((const uint8_t*)written.pointer, (uint32_t)written.length, &xros);
            const String8 named = platform < BUSTER_ARRAY_LENGTH(check_availability_pretty) ?
                                  (String8){ (char*)check_availability_pretty[platform], strlen(check_availability_pretty[platform]) } : written;
            char first[48];
            char second[48];
            check_version_print(text != NULL ? text->first : (CheckVersionTuple){ 0 }, first, sizeof(first));
            check_version_print(text != NULL ? text->second : (CheckVersionTuple){ 0 }, second, sizeof(second));
            result = text == NULL ? NULL :
                     event->kind == CHECK_WARNING_EVENT_AVAILABILITY_VERSION ?
                     check_warning_text(scratch, "invalid %.*s version '%s' in availability attribute", (int)named.length, named.pointer, first) :
                     check_warning_text(scratch, "feature cannot be %s in %.*s version %s before it was %s in version %s; attribute ignored",
                                        stages[text->first_stage % 3], (int)named.length, named.pointer, first, stages[text->second_stage % 3], second);
            break;
        }
        case CHECK_WARNING_EVENT_TCB_VIOLATION:
        {
            const String8 tcb = check_warning_strings_value(parser, pp, payload->token, scratch);
            result = check_warning_text(scratch, "calling '%.*s' is a violation of trusted computing base '%.*s'", (int)name.length, name.pointer, (int)tcb.length,
                                        tcb.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_EXCESSIVE_REGSAVE:
            result = payload->data == 0 ?
                     "interrupt service routine should only call a function with attribute 'no_caller_saved_registers' or be compiled with '-mgeneral-regs-only'" :
                     "function with attribute 'no_caller_saved_registers' should only call a function with attribute 'no_caller_saved_registers' or be compiled with "
                     "'-mgeneral-regs-only'";
            break;
        case CHECK_WARNING_EVENT_POINTER_TO_INT_CAST:
        {
            const String8 target = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->second_type, payload->data);
            const String8 source = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->name);
            result = target.length != 0 && source.length != 0 ?
                     check_warning_text(scratch, "cast to smaller integer type %.*s from %.*s", (int)target.length, target.pointer, (int)source.length, source.pointer) :
                     NULL;
            break;
        }
        case CHECK_WARNING_EVENT_VOID_PTR_DEREFERENCE:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            result = type.length != 0 ? check_warning_text(scratch, "ISO C does not allow indirection on operand of type %.*s", (int)type.length, type.pointer) : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_DIAGNOSE_IF:
        {
            // diagnose_if's message, its string literals decoded and joined, as clang prints it (#1447).
            const String8 message = check_warning_strings_value(parser, pp, payload->token, scratch);
            result = check_warning_text(scratch, "%.*s", (int)message.length, message.pointer);
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
        case CHECK_WARNING_EVENT_UNKNOWN_ESCAPE:
            result = check_warning_text(scratch, "unknown escape sequence '\\%c'", (char)payload->name);
            break;
        case CHECK_WARNING_EVENT_NONSTANDARD_ESCAPE:
            result = check_warning_text(scratch, "use of non-standard escape character '\\%c'", (char)payload->name);
            break;
        case CHECK_WARNING_EVENT_DELIMITED_ESCAPE:
            result = payload->data != 0 ? "named escape sequences are a Clang extension" : "delimited escape sequences are a C2y extension";
            break;
        case CHECK_WARNING_EVENT_UNEVALUATED_PREFIX:
            result = check_warning_text(scratch, "encoding prefix '%s' on an unevaluated string literal has no effect",
                                        payload->data == 2 ? "u8" : payload->name == 'L' ? "L" : payload->name == 'U' ? "U" : "u");
            break;
        case CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED:
        {
            // A version of target_clones' string (#1838): its bytes in the string's decoded text.
            const String8 text = check_warning_strings_value(parser, pp, payload->token, scratch);
            const uint32_t start = payload->name <= text.length ? payload->name : (uint32_t)text.length;
            uint32_t shown = 0;
            while (start + shown < text.length && text.pointer[start + shown] != ',')
            {
                shown += 1;
            }
            while (shown != 0 && check_target_version_space(text.pointer[start + shown - 1]))
            {
                shown -= 1;
            }
            result = check_warning_text(scratch, "unsupported%s '%.*s' in the 'target_clones' attribute string; 'target_clones' attribute ignored",
                                        (payload->data & BUSTER_CHECK_TARGET_CLONES_CPU) != 0 ? " CPU" : "", (int)shown,
                                        text.pointer != NULL ? text.pointer + start : "");
            break;
        }
        case CHECK_WARNING_EVENT_TARGET_CLONES_DUPLICATE:
            result = "version list contains duplicate entries";
            break;
        case CHECK_WARNING_EVENT_TARGET_CLONES_MIXED:
            result = "mixing 'target_clones' specifier mechanisms is permitted for GCC compatibility; use a comma separated sequence of string literals, or a "
                     "string literal containing a comma-separated list of versions";
            break;
        case CHECK_WARNING_EVENT_BIT_INT_SUFFIX:
            result = "'_BitInt' suffix for literals is a C23 extension";
            break;
        case CHECK_WARNING_EVENT_ASM_LABEL_ON_AUTO:
        {
            // The label's decoded string, as clang's warn_asm_label_on_auto_decl prints it (#2370).
            const String8 label = check_warning_strings_value(parser, pp, payload->token, scratch);
            result = check_warning_text(scratch, "ignored asm label '%.*s' on automatic variable", (int)label.length, label.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_GNU_DESIGNATOR:
            result = payload->data == BUSTER_CHECK_GNU_DESIGNATOR_OLD_STYLE ? "use of GNU old-style field designator extension" :
                     payload->data == BUSTER_CHECK_GNU_DESIGNATOR_RANGE ? "use of GNU array range extension" :
                     "use of GNU 'missing =' extension in designator";
            break;
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
        case CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN:
        case CHECK_WARNING_EVENT_OBJC_UNSUPPORTED:
        case CHECK_WARNING_EVENT_ASSUME_SIDE_EFFECTS:
        case CHECK_WARNING_EVENT_DIFFERENT_ARGUMENTS:
        case CHECK_WARNING_EVENT_NO_CALLS:
        case CHECK_WARNING_EVENT_NOINLINE_STATEMENT:
        case CHECK_WARNING_EVENT_ALWAYS_INLINE_STATEMENT:
        case CHECK_WARNING_EVENT_PRECEDENCE_ALWAYS_INLINE:
        case CHECK_WARNING_EVENT_PRECEDENCE_FLATTEN:
        case CHECK_WARNING_EVENT_PRECEDENCE_NOINLINE:
        case CHECK_WARNING_EVENT_LIKELIHOOD_CONFLICT:
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
                                event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED || event->kind == CHECK_WARNING_EVENT_OBJC_UNSUPPORTED ||
                                event->kind == CHECK_WARNING_EVENT_CAPTURE_DEPRECATED ||
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
            // target's warning's piece of its string (check_input_target_warning's data), from the whole string, which can be
            // longer than `argument`.
            const String8 whole = target ? check_warning_strings_value(parser, pp, payload->token, scratch) : (String8){ "", 0 };
            const uint32_t offset = (payload->data >> 4) & 0x3fffu;
            const uint32_t piece = offset <= whole.length ? ((payload->data >> 18) & 0x3fffu) : 0u;
            const uint32_t shown = offset <= whole.length ? (piece < whole.length - offset ? piece : (uint32_t)whole.length - offset) : 0u;
            static const char* const target_cases[] = { "unsupported", "duplicate", "unknown", "unknown" };
            static const char* const target_places[] = { "", " CPU", " tune CPU", "" };
            // An identifier's or a keyword's spelling as written, which swift_newtype's warning quotes (#1447).
            const uint32_t word = event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN && payload->token < count ? parser->tokens.names[payload->token] : 0;
            const String8 token_word = word != 0 ? check_driver_warning_name(pp, word) : (String8){ "", 0 };
            char token_text[160] = "(null)";
            if (word != 0)
            {
                snprintf(token_text, sizeof(token_text), "'%.*s'", (int)token_word.length, token_word.pointer);
            }
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
                                   target_places[(payload->data >> 2) & 3u], (int)shown, whole.pointer + (offset <= whole.length ? offset : 0)) :
                event->kind == CHECK_WARNING_EVENT_UNKNOWN_SANITIZER ? check_warning_text(scratch, "unknown sanitizer '%s' ignored", argument) :
                event->kind == CHECK_WARNING_EVENT_SANITIZER_GLOBAL ?
                check_warning_text(scratch, "'%.*s%s%.*s' attribute argument '%s' not supported on a global variable", (int)scope.length, scope.pointer,
                                   scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, argument) :
                event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED || event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_QUOTED ||
                event->kind == CHECK_WARNING_EVENT_OBJC_UNSUPPORTED ?
                check_warning_text(scratch, event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED ? "'%.*s%s%.*s' attribute argument not supported: %s" :
                                                                                                     "'%.*s%s%.*s' attribute argument not supported: '%s'",
                                   (int)scope.length, scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, argument) :
                event->kind == CHECK_WARNING_EVENT_ARGUMENT_UNSUPPORTED_TOKEN ?
                check_warning_text(scratch, "'%.*s%s%.*s' attribute argument not supported: %s", (int)scope.length, scope.pointer,
                                   scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer, token_text) :
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
                                        event->kind == CHECK_WARNING_EVENT_LIKELIHOOD_CONFLICT ? "conflicting attributes '%.*s%s%.*s' are ignored" :
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
        case CHECK_WARNING_EVENT_CPU_LIST_DUPLICATE:
            result = "CPU list contains duplicate entries; attribute ignored";
            break;
        case CHECK_WARNING_EVENT_CPU_DISPATCH_BODY:
            result = "body of cpu_dispatch function will be ignored";
            break;
        case CHECK_WARNING_EVENT_ADDRESS_SPACE_DEPRECATED:
        {
            // (clang's message quotes the quoted name again; a C23 one's with its scope, #1447.)
            const bool standard = (payload->data & BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE) != 0;
            const uint32_t scope_name = payload->data & ~BUSTER_CHECK_WARNING_STANDARD_ATTRIBUTE;
            const String8 scope = scope_name != 0 ? check_warning_attribute_scope(check_driver_warning_name(pp, scope_name)) : (String8){ "", 0 };
            const String8 attribute = standard ? check_warning_attribute_name(name, scope, true) : name;
            result = check_warning_text(scratch, "''%.*s%s%.*s'' attribute is deprecated and may be removed in a future version of Clang", (int)scope.length,
                                        scope.pointer, scope.length != 0 ? "::" : "", (int)attribute.length, attribute.pointer);
            break;
        }
        case CHECK_WARNING_EVENT_ADDRESS_SPACE_IDENTICAL:
            result = "multiple identical address spaces specified for type";
            break;
        case CHECK_WARNING_EVENT_GC_WEAK_LOCAL:
            result = "__weak attribute cannot be specified on an automatic variable when ARC is not enabled";
            break;
        case CHECK_WARNING_EVENT_GC_WEAK_FIELD:
            result = "__weak attribute cannot be specified on a field declaration";
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
        case CHECK_WARNING_EVENT_TENTATIVE_ARRAY:
            result = "tentative array definition assumed to have one element";
            break;
        case CHECK_WARNING_EVENT_DEFAULT_CONST_INIT:
        {
            // The object's type as declared, with its typedef (#2168): 'volatile CI[2]' (aka 'const volatile int[2]').
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->data);
            const bool field = event->id == CHECK_WARNING_ID_DEFAULT_CONST_INIT_FIELD_UNSAFE;
            const bool unsafe = event->id != CHECK_WARNING_ID_DEFAULT_CONST_INIT_VAR;
            // An unsafe one says it is incompatible with C++ too where -Wc++-compat is on there, as clang's words select
            // by its fake diagnostic's policy.
            const CheckWarningEvent compatibility = {
                .id = CHECK_WARNING_ID_CXX_COMPAT,
                .kind = event->kind,
                .token = event->token,
                .source = event->source,
                .flags = BUSTER_CHECK_WARNING_DEFAULT_IGNORED,
            };
            const bool incompatible = unsafe && check_driver_warning_severity(pp, input, &compatibility) != CHECK_WARNING_IGNORE;
            result = type.length != 0 ? check_warning_text(scratch, "default initialization of an object of type %.*s%s %s%s", (int)type.length, type.pointer,
                                                           field ? " with const member" : "", unsafe ? "leaves the object uninitialized" : "is incompatible with C++",
                                                           incompatible ? " and is incompatible with C++" : "") :
                     NULL;
            break;
        }
        case CHECK_WARNING_EVENT_UNUSED_RESULT:
        {
            static const char* const messages[] =
            {
                "left operand of comma operator has no effect",
                "expression result unused; should this cast be to 'void'?",
                "equality comparison result unused",
                "inequality comparison result unused",
                "relational comparison result unused",
            };
            result = messages[payload->data < BUSTER_ARRAY_LENGTH(messages) ? payload->data : 0];
            break;
        }
        case CHECK_WARNING_EVENT_UNREACHABLE_ASSOCIATION:
        {
            const String8 type = check_driver_warning_type(parser, pp, input, types, spell, scratch, payload->first_type, payload->name);
            result = type.length != 0 ? check_warning_text(scratch, "due to lvalue conversion of the controlling expression, association of type %.*s will never "
                                                           "be selected because it is %s", (int)type.length, type.pointer,
                                                           payload->data == 0 ? "of array type" : "qualified") : NULL;
            break;
        }
        case CHECK_WARNING_EVENT_UNDERLYING_CV_QUALIFIER:
            result = payload->data == (BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE) ? "'const' and 'volatile' qualifiers in enumeration underlying type ignored" :
                     payload->data == BUSTER_TYPE_VOLATILE ? "'volatile' qualifier in enumeration underlying type ignored" :
                                                             "'const' qualifier in enumeration underlying type ignored";
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
        case CHECK_WARNING_EVENT_DIVISION_BY_ZERO:
            result = payload->data != 0 ? "remainder by zero is undefined" : "division by zero is undefined";
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
        case CHECK_WARNING_NOTE_PRIVATE_EXTERN:
            result = "use __attribute__((visibility(\"hidden\"))) attribute instead";
            break;
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
        case CHECK_WARNING_NOTE_PREVIOUS_ATTRIBUTE: result = "previous attribute is here"; break;
        case CHECK_WARNING_NOTE_IMPLICIT_VERSION:
        {
            const CheckInputAvailabilityText* text = note->payload.data < input->availability_text_count ? input->availability_texts + note->payload.data : NULL;
            char canonical[48];
            check_version_print(text != NULL ? text->second : (CheckVersionTuple){ 0 }, canonical, sizeof(canonical));
            result = text != NULL ? check_warning_text(scratch, "implicitly treating version as '%s'", canonical) : NULL;
            break;
        }
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
        case CHECK_WARNING_NOTE_NODEREF_DECLARATION:
            result = check_warning_text(scratch, "%.*s declared here", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_DECLARED_HERE:
            result = check_warning_text(scratch, "'%.*s' declared here", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_CONST_MEMBER:
            result = check_warning_text(scratch, "member '%.*s' declared 'const' here", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_ATTRIBUTE_HERE:
            result = check_warning_text(scratch, "attribute '%.*s' here", (int)name.length, name.pointer);
            break;
        case CHECK_WARNING_NOTE_COMPARISON_ASSIGNMENT:
            result = note->payload.data == BUSTER_CHECK_UNUSED_INEQUALITY ? "use '|=' to turn this inequality comparison into an or-assignment" :
                                                                            "use '=' to turn this equality comparison into an assignment";
            break;
        case CHECK_WARNING_NOTE_DIAGNOSE_IF:
            result = check_warning_text(scratch, "from 'diagnose_if' attribute on '%.*s':", (int)name.length, name.pointer);
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
                   event->kind == CHECK_WARNING_EVENT_OBJC_POINTER_TYPE || event->kind == CHECK_WARNING_EVENT_OBJC_OBJECT_TYPE ||
                   event->kind == CHECK_WARNING_EVENT_OBJC_WEAK_TYPE ||
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
    // A function's unused parameters after its body's unused variables: clang's ActOnFinishFunctionBody diagnoses the
    // parameters once the body's scope has popped its own (both at the body's `}`), so they publish in a second pass,
    // which only a pass with one takes.
    bool parameters = false;
    for (uint32_t i = 0; !parameters && i < view->count; i += 1)
    {
        parameters = view->events[i].kind == CHECK_WARNING_EVENT_UNUSED_PARAMETER && view->events[i].completion_kind != CHECK_WARNING_COMPLETE_TU_END;
    }
    const uint64_t steps = (uint64_t)view->count * (parameters ? 2u : 1u);
    for (uint64_t step = 0; valid && step < steps && !pp->fatal; step += 1)
    {
        const uint32_t i = (uint32_t)(step % view->count);
        const CheckWarningEvent* event = view->events + i;
        const bool late = event->kind == CHECK_WARNING_EVENT_UNUSED_PARAMETER && event->completion_kind != CHECK_WARNING_COMPLETE_TU_END;
        const bool retained = late == (step >= view->count) && event->kind != CHECK_WARNING_EVENT_MESSAGE && event->kind != CHECK_WARNING_EVENT_ENUM_MIX &&
                              (event->token < parser->tokens.count || (event->kind == CHECK_WARNING_EVENT_PRAGMA && event->token == parser->tokens.count)) &&
                              (repair == NULL || check_repair_warning_token(repair, event->token)) &&
                              (input->interrupt_range_count == 0 || check_driver_interrupt_kept(input, event->token));
        if (retained)
        {
            valid = event->id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) && event->note_first <= view->note_count &&
                    event->note_count <= view->note_count - event->note_first &&
                    event->kind < CHECK_WARNING_EVENT_KIND_COUNT && check_warning_flags_valid(event->kind, event->flags) &&
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
                // A grouped diagnose_if's option is its group's; one that is an error by default has no -Werror (#1447).
                const uint32_t group = check_driver_warning_group(event);
                const char* option = group != UINT32_MAX ? pp_features_warning[group].name : check_warning_descriptions[event->id].option;
                const bool default_error = group != UINT32_MAX && (event->payload.data & BUSTER_CHECK_WARNING_GROUP_ERROR) != 0;
                const char* prefix = check_driver_warning_error_prefix(severity == CHECK_WARNING_ERROR && !default_error);
                const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", prefix, option) : prefix[0] != 0 ? " [-Werror]" : "";
                valid = valid && suffix != NULL;
                if (valid)
                {
                    const PPDiagnosticRecord* previous = pp->diagnostic_last;
                    const uint32_t source = event->source != BUSTER_CHECK_WARNING_NO_SOURCE ? event->source : parser->tokens.sources[event->token];
                    const uint32_t boundary = event->completion_kind == CHECK_WARNING_COMPLETE_TU_END ? pp->output.count + 1 :
                        check_driver_publication_position(parser, pp, event->completion + (event->completion_kind == CHECK_WARNING_COMPLETE_AFTER_DELIMITER ? 1u : 0u), publication_end);
                    const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                                   severity == CHECK_WARNING_ERROR ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
                    if (event->kind == CHECK_WARNING_EVENT_CLANG_MACRO_SCOPE)
                    {
                        pp_diagnose_output_notes_phase(pp, disposition, source, boundary, PP_DIAGNOSTIC_LEXICAL, notes, event->note_count, "%s%s", message, suffix);
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_PRAGMA)
                    {
                        const CheckInputSymbolPragma* record = check_input_symbol_warning(input, event);
                        const CheckInputMsPragma* layout = check_input_ms_warning(input, event);
                        const CheckInputPackPragma* packing = check_input_pack_warning(input, event);
                        // An align=reset's empty stack is Sema's, where the parser handles the directive, and so are pack's
                        // own Sema warnings and its unterminated pushes at the unit's end (#1849).
                        const bool semantic = (record != NULL && record->warning >= CHECK_SYMBOL_WARNING_UNDECLARED) ||
                                              (packing != NULL && check_input_pack_semantic(packing, event));
                        const uint32_t location = record != NULL ? record->diagnostic_location : layout != NULL ? layout->diagnostic_location :
                                                  packing != NULL && !semantic ? packing->diagnostic_location : 0;
                        // An unterminated push's note (#1849): at the directive that set the current value, or without a
                        // place where none did.
                        const bool pack_note = packing != NULL && event->payload.name == CHECK_PACK_MESSAGE_UNTERMINATED && event->payload.token != BUSTER_CHECK_PACK_NO_NOTE;
                        const bool pack_placed = pack_note && event->payload.token != BUSTER_CHECK_PACK_UNLOCATED_NOTE && event->payload.token < pp->output.count;
                        const PPDiagnosticSourceNote pack_notes[1] = {
                            {
                                .location = pack_placed ? pp->output.locations[event->payload.token] : 0,
                                .origin = pack_placed ? pp_history_at(pp, event->payload.token) : 0,
                                .message = "did you intend to use '#pragma pack (pop)' instead of '#pragma pack()'?",
                            },
                        };
                        // An inclusion that changes the value (#1849): at its file name, where its output ends, with clang's note
                        // at the directive that set the value there, or without a place where none did.
                        const CheckPackInclude* included = packing != NULL && event->payload.name == CHECK_PACK_MESSAGE_INCLUDE &&
                                                           event->payload.token < input->pack_pragmas->include_count ? input->pack_pragmas->includes + event->payload.token : NULL;
                        const bool include_placed = included != NULL && included->note < pp->output.count;
                        const PPDiagnosticSourceNote include_notes[1] = {
                            {
                                .location = include_placed ? pp->output.locations[included->note] : 0,
                                .origin = include_placed ? pp_history_at(pp, included->note) : 0,
                                .message = "previous '#pragma pack' directive that modifies alignment is here",
                            },
                        };
                        if (included != NULL)
                        {
                            pp_diagnose_placed_sources_phase(pp, disposition, included->location, 0, included->exit, PP_DIAGNOSTIC_LEXICAL, include_notes, 1, "%s%s",
                                                             message, suffix);
                        }
                        else if (pack_note)
                        {
                            pp_diagnose_placed_sources_phase(pp, disposition, pp->output.locations[source], pp_history_at(pp, source), boundary, PP_DIAGNOSTIC_SEMANTIC,
                                                             pack_notes, 1, "%s%s", message, suffix);
                        }
                        else if (location != 0)
                        {
                            const uint32_t saved_position = pp->diagnostic_position;
                            pp->diagnostic_position = event->payload.token;
                            pp_diagnose(pp, disposition, location, "%s%s", message, suffix);
                            pp->diagnostic_position = saved_position;
                        }
                        else
                        {
                            // A pack directive's own Sema warning is at the directive, which the parser consumes before it
                            // reads on (#1849): after its own parser warnings, before the next directive's.
                            // That is the parser's boundary at the next token, where it handles the annotation, but before the
                            // next pack directive, whose line it lexes only after.
                            const bool directive = packing != NULL && semantic && event->completion_kind != CHECK_WARNING_COMPLETE_TU_END && !packing->align;
                            const uint32_t following = directive ? check_input_pack_following(parser, input->pack_pragmas, packing) : UINT32_MAX;
                            pp_diagnose_output_phase(pp, disposition, source, directive && following < boundary ? following : semantic ? boundary : event->payload.token,
                                                     semantic ? PP_DIAGNOSTIC_SEMANTIC : PP_DIAGNOSTIC_LEXICAL, "%s%s", message, suffix);
                        }
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_UNEVALUATED_PREFIX || event->kind == CHECK_WARNING_EVENT_DELIMITED_ESCAPE)
                    {
                        // At the literal, with the macro argument it came from (#1447).
                        pp->diagnostic_arguments = true;
                        pp_diagnose_output_offset_at(pp, disposition, source, 0, boundary, "%s%s", message, suffix);
                        pp->diagnostic_arguments = false;
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_UNKNOWN_ESCAPE || event->kind == CHECK_WARNING_EVENT_NONSTANDARD_ESCAPE)
                    {
                        // At its backslash inside the literal's token (#1447), with the macro argument it came from.
                        pp->diagnostic_arguments = true;
                        pp_diagnose_output_offset_at(pp, disposition, source, event->payload.data, boundary, "%s%s", message, suffix);
                        pp->diagnostic_arguments = false;
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED || event->kind == CHECK_WARNING_EVENT_TARGET_CLONES_DUPLICATE ||
                             event->kind == CHECK_WARNING_EVENT_TARGET_CLONES_MIXED)
                    {
                        // At the version's offset in its trimmed string, from the literal's quote, as clang places it (#1838).
                        pp_diagnose_output_offset_at(pp, disposition, source, event->payload.data & ~BUSTER_CHECK_TARGET_CLONES_CPU, boundary, "%s%s", message, suffix);
                    }
                    else if ((event->flags & BUSTER_CHECK_WARNING_UNLOCATED) != 0)
                    {
                        // An implicit attribute's warning, which clang gives no location (#147: interrupt's used).
                        pp_diagnose_unlocated_notes(pp, disposition, boundary, notes, event->note_count, "%s%s", message, suffix);
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_OBJC_POINTER_TYPE || event->kind == CHECK_WARNING_EVENT_OBJC_OBJECT_TYPE ||
                             event->kind == CHECK_WARNING_EVENT_OBJC_WEAK_TYPE)
                    {
                        // At the macro's expansion clang gives them (check_warning_objc_macro), before the declaration's
                        // attributes' checks at the same boundary.
                        uint32_t location = 0;
                        uint32_t origin = 0;
                        (void)check_warning_objc_macro(parser, pp, event, &location, &origin);
                        pp_diagnose_placed_notes_phase(pp, disposition, location, origin, boundary, PP_DIAGNOSTIC_TYPE, notes, event->note_count, "%s%s", message, suffix);
                    }
                    else if (event->kind == CHECK_WARNING_EVENT_FUNCTION_TYPE_ATTRIBUTE || event->kind == CHECK_WARNING_EVENT_CONVENTION_UNSUPPORTED ||
                             event->kind == CHECK_WARNING_EVENT_OBJC_UNSUPPORTED)
                    {
                        // A declarator's type's warnings precede its declaration's attributes' checks at the same boundary.
                        pp_diagnose_output_notes_phase(pp, disposition, source, boundary, PP_DIAGNOSTIC_TYPE, notes, event->note_count, "%s%s", message, suffix);
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
                    else if (event->kind == CHECK_WARNING_EVENT_UNUSED_RESULT && event->completion_kind == CHECK_WARNING_COMPLETE_AT_TOKEN &&
                             event->completion > event->token + 1)
                    {
                        // A constant bound's comma in a function body (#2202, check_input_unused_completion): with the body's
                        // queued runtime diagnostics, at its end.
                        pp_diagnose_output_notes_phase(pp, disposition, source, boundary, PP_DIAGNOSTIC_DEFERRED, notes, event->note_count, "%s%s", message, suffix);
                    }
                    else
                    {
                        pp_diagnose_output_notes_at(pp, disposition, source, boundary, notes, event->note_count, "%s%s", message, suffix);
                    }
                    pp_diagnostic_suborder(pp, previous, event->suborder);
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
