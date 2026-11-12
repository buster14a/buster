// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Private preprocessor bodies and tables. c_preprocessor.c supplies the shared declarations and readers, then
// includes this file in FRONTEND, standalone consumers and the fuzz driver's complete source interface. Keep the
// private definitions in their original relative order, including the expansion-history implementation's position.

static uint32_t pp_lexed_start(const Preprocessor* pp, uint32_t location);
static bool pp_diagnostic_warning_visible(Preprocessor* pp, uint32_t location, uint32_t origin);
static void pp_render_diagnostic(Preprocessor* pp, PPSeverity severity, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const char* format, va_list arguments);
static void pp_collect_diagnostic(Preprocessor* pp, PPSeverity severity, uint64_t position, uint32_t location, uint32_t origin, const PPDiagnosticNote* note, const PPOutputDiagnosticNote* source_notes, uint32_t source_note_count, const PPDiagnosticSourceNote* placed_notes, uint32_t placed_note_count, const char* format, va_list arguments);
static void* pp_array_small(Preprocessor* pp, bool* reserved, uint64_t count, uint64_t size, uint64_t small);
static PPMacroId pp_new_macro(Preprocessor* pp, IdentId name);
static void pp_scan_body_scalar(const uint8_t* kinds, const uint32_t* values, uint32_t begin, uint32_t end, bool* pastes, bool* hashes, bool* va_opt);
static uint32_t pp_start_with_splices(const uint8_t* bytes, uint32_t start, bool trigraphs);
static void pp_continue_relex(Preprocessor* pp, uint32_t file_context, uint32_t begin, uint32_t expansion_end);
static uint32_t pp_copy_file_run_scalar(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit);
static uint32_t pp_copy_buffer_run_scalar(Preprocessor* pp, const PPTokens* tokens, uint32_t position, uint32_t end);
static void pp_directive(Preprocessor* pp, PPContext* context);
static void pp_finish_invocation(Preprocessor* pp);
static void pp_close_builtin(Preprocessor* pp, bool closed);
static void pp_start_stream(Preprocessor* pp);
static bool pp_copy_relexed(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end, PPTokens* into);
static void pp_run_contexts(Preprocessor* pp, uint32_t stop_depth);
static bool pp_pragma(Preprocessor* pp, uint32_t begin, uint32_t end, uint32_t location, bool from_operator, bool ident, uint32_t restore);
static uint32_t pp_pragma_strings(Preprocessor* pp, const PPTokens* tokens, uint32_t* i, uint32_t end, uint8_t* out, uint32_t capacity, const char* what, uint32_t location);
static uint64_t pp_has_embed(Preprocessor* pp, const PPTokens* tokens, uint32_t first, uint32_t last, uint32_t location);
static void pp_resolve_keywords_range(Preprocessor* pp, uint32_t begin, uint32_t end);
static void pp_finish_condition(Preprocessor* pp);
static void pp_finish_include(Preprocessor* pp);
static void pp_finish_line(Preprocessor* pp);
static void pp_finish_pragma(Preprocessor* pp);
static void pp_finish_embed(Preprocessor* pp);
static bool pp_evaluate(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location);
static void pp_embed(Preprocessor* pp, PPContext* context, uint32_t d);
static void pp_resolve_keywords(Preprocessor* pp);
static void pp_resolve_keywords_scalar(Preprocessor* pp);
static uint64_t pp_embed_text_scalar(const uint8_t* bytes, uint64_t count, bool separated, uint8_t* out);
static void pp_diagnose_output_location(Preprocessor* pp, PPSeverity severity, uint32_t token, uint32_t location, uint32_t position, PPDiagnosticPhase phase,
                                        const PPOutputDiagnosticNote* notes, uint32_t note_count, const char* format, va_list arguments);

static const char* const pp_region_names[3] = { "assume_nonnull", "arc_cf_code_audited", "unsafe_buffer_usage" };

// Reserves the columns, all or none (pp_tokens_release releases them).
static bool pp_tokens_reserve(PPTokens* tokens, uint32_t capacity)
{
    const uint64_t slots = (uint64_t)capacity + BUSTER_PP_TOKEN_SLACK;
    ArenaTable columns[] = {
        { .bytes = slots, .small = BUSTER_ARENA_SMALL_BYTES },
        { .bytes = slots, .small = BUSTER_ARENA_SMALL_BYTES },
        { .bytes = slots * sizeof(uint32_t), .small = BUSTER_ARENA_SMALL_BYTES },
        { .bytes = slots * sizeof(uint32_t), .small = BUSTER_ARENA_SMALL_BYTES },
    };
    tokens->columns = arena_reserve_tables(columns, BUSTER_ARRAY_LENGTH(columns));
    tokens->kinds = columns[0].base;
    tokens->flags = columns[1].base;
    tokens->values = columns[2].base;
    tokens->locations = columns[3].base;
    tokens->count = 0;
    tokens->capacity = capacity;
    return tokens->columns.base != 0;
}

BUSTER_INLINE void pp_tokens_set(PPTokens* tokens, uint32_t index, uint8_t kind, uint8_t flags, uint32_t value, uint32_t location)
{
    tokens->kinds[index] = kind;
    tokens->flags[index] = flags;
    tokens->values[index] = value;
    tokens->locations[index] = location;
}

// Both source and output-token diagnostics pass this gate before rendering their complete group, including notes.
// The cutoff has no source location or notes; once fatal, later errors and warnings contribute neither text nor counts.
static bool pp_diagnostic_begin(Preprocessor* pp, PPSeverity* severity, bool* limited)
{
    // A source pragma's error, or a command-line warning whose complete policy was already resolved, is MAPPED_ERROR
    // and keeps its ordinary error severity under -Wfatal-errors; every other error becomes fatal below. It has no
    // effect on warnings, and the error budget can't cut in first: the diagnostic is already fatal.
    const bool source_error = *severity == PP_SEVERITY_MAPPED_ERROR;
    *severity = (source_error || *severity == PP_SEVERITY_DEFERRED_ERROR) ? PP_SEVERITY_ERROR :
                *severity == PP_SEVERITY_MAPPED_FATAL ? PP_SEVERITY_FATAL : *severity;
    *severity = pp->options.fatal_errors && *severity == PP_SEVERITY_ERROR && !source_error ? PP_SEVERITY_FATAL : *severity;
    const bool accepted = !pp->fatal && !(*severity == PP_SEVERITY_WARNING && pp->options.suppress_warnings);
    *limited = accepted && !pp->options.collect_diagnostics && *severity == PP_SEVERITY_ERROR && pp->options.error_limit != 0 && pp->errors >= pp->options.error_limit;
    if (accepted)
    {
        *severity = *limited ? PP_SEVERITY_FATAL : *severity;
        pp->warnings += *severity == PP_SEVERITY_WARNING;
        pp->errors += *severity != PP_SEVERITY_WARNING;
        pp->fatal |= *severity == PP_SEVERITY_FATAL && (!pp->options.collect_diagnostics || !pp->diagnostic_source);
    }
    return accepted;
}

static void pp_diagnostic_limit(Preprocessor* pp)
{
    FILE* stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr;
    fputs("fatal error: too many errors emitted, stopping now [-ferror-limit=]\n", stream);
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnostic_summary(const Preprocessor* pp)
{
    if (!pp->options.quiet && !pp->options.no_diagnostic_summary && (pp->warnings != 0 || pp->errors != 0))
    {
        FILE* stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr;
        if (pp->warnings != 0)
        {
            fprintf(stream, "%u warning%s%s", pp->warnings, pp->warnings == 1 ? "" : "s", pp->errors != 0 ? " and " : "");
        }
        if (pp->errors != 0)
        {
            fprintf(stream, "%u error%s", pp->errors, pp->errors == 1 ? "" : "s");
        }
        fputs(" generated.\n", stream);
    }
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((format(printf, 4, 5))) void pp_diagnose(Preprocessor* pp, PPSeverity severity, uint32_t location, const char* format, ...)
{
    const bool always = severity == PP_SEVERITY_WARNING_ALWAYS;
    severity = always ? PP_SEVERITY_WARNING : severity;
    const PPSeverity requested = severity;
    const bool visible = severity != PP_SEVERITY_WARNING || always || pp_diagnostic_warning_visible(pp, location, location != 0 ? pp->diagnostic_origin : 0);
    bool limited = false;
    const bool accepted = visible && pp_diagnostic_begin(pp, &severity, &limited);
    if (accepted && !pp->options.quiet && limited)
    {
        pp_diagnostic_limit(pp);
    }
    else if (accepted && (!pp->options.quiet || (pp->options.collect_diagnostics && !pp->history_deferred)))
    {
        va_list arguments;
        va_start(arguments, format);
        const uint32_t position = pp->diagnostic_position != BUSTER_PP_NONE ? pp->diagnostic_position : pp->output.count;
        if (pp->options.collect_diagnostics)
        {
            pp_collect_diagnostic(pp, requested, (uint64_t)position * PP_DIAGNOSTIC_PHASE_COUNT + PP_DIAGNOSTIC_LEXICAL, location, location != 0 ? pp->diagnostic_origin : 0, NULL, NULL, 0, NULL, 0, format, arguments);
        }
        else
        {
            pp_render_diagnostic(pp, severity, location, location != 0 ? pp->diagnostic_origin : 0, NULL, format, arguments);
        }
        va_end(arguments);
    }
}

// The answer of pp_warning_severity for a warning that isn't given (#1934): no severity a diagnostic is given with.
#define BUSTER_PP_IGNORED PP_SEVERITY_NOTE

// What a #pragma clang diagnostic made of a warning group (#1934): nothing, or its severity from there on.
typedef enum PPPragmaMapping
{
    PP_PRAGMA_MAPPING_NONE,
    PP_PRAGMA_MAPPING_IGNORED,
    PP_PRAGMA_MAPPING_WARNING,
    PP_PRAGMA_MAPPING_ERROR,
    PP_PRAGMA_MAPPING_FATAL,
} PPPragmaMapping;

// The preprocessor's warnings that follow clang's options and pragmas (#1934, #1790, #1970, pp_warning_severity): each a
// kind of clang's diagnostic, named by its group's flag, the kinds of one group mapped alike by its options and pragmas.
typedef enum PPWarning
{
    // A bit-precise constant's suffix in #if, #elifdef and #elifndef before C23, and #embed before C23.
    PP_WARNING_C23_EXTENSIONS,
    // A \N read as no UCN (#1447), and a UCN before C99 (an extension).
    PP_WARNING_UNICODE,
    PP_WARNING_UNICODE_C99,
    // A named UCN that starts a token (#1447).
    PP_WARNING_DELIMITED_ESCAPE,
    // #warning's text, and #pragma message's and #pragma GCC warning's (#1790).
    PP_WARNING_WARNINGS,
    PP_WARNING_PRAGMA_MESSAGES,
    // #warning itself, before C23 an extension and in C23 incompatible with earlier standards.
    PP_WARNING_PEDANTIC,
    PP_WARNING_PRE_C23_COMPAT,
    // The directives' (#1970).
    PP_WARNING_EXTRA_TOKENS,
    PP_WARNING_C99_EXTENSIONS,
    PP_WARNING_MACRO_REDEFINED,
    PP_WARNING_BUILTIN_MACRO_REDEFINED,
    PP_WARNING_VARIADIC_MACROS,
    PP_WARNING_UNKNOWN_PRAGMAS,
    PP_WARNING_UNKNOWN_WARNING_OPTION,
    PP_WARNING_PRAGMA_ONCE_OUTSIDE_HEADER,
    PP_WARNING_PRAGMA_SYSTEM_HEADER_OUTSIDE_HEADER,
    PP_WARNING_NONPORTABLE_INCLUDE_PATH,
    // The literals' in #if and in pragmas' strings (#1970).
    PP_WARNING_MULTICHAR,
    PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE,
    PP_WARNING_IMPLICITLY_UNSIGNED_LITERAL,
    PP_WARNING_INVALID_SOURCE_ENCODING,
    // A parsed unit without a declaration (#2222), an extension off by default.
    PP_WARNING_EMPTY_TRANSLATION_UNIT,
    // A #define's or #undef's name reserved in every context (#2222), off by default.
    PP_WARNING_RESERVED_MACRO_IDENTIFIER,
    // A warning of no group (poisoning an existing macro, a character constant too long for its type), and an extension
    // of no group (a C90 macro name with no whitespace after it before a character outside its basic set): only -w,
    // -Wno-everything, -Werror and an extension's -pedantic-errors map it, its flag "[-Werror]" where promoted.
    PP_WARNING_PLAIN,
    PP_WARNING_PLAIN_EXTENSION,
    BUSTER_PP_WARNINGS,
} PPWarning;

typedef struct PPWarningKind PPWarningKind;
struct PPWarningKind
{
    // Its group's names without -W: its own (null for none), its old one, and the groups that hold it, separated by spaces.
    const char* name;
    const char* alias;
    const char* parents;
    // Whether it is clang's extension (-pedantic-errors promotes it), and whether it is on by default (else -Weverything
    // or its names give it, and -pedantic one that is an extension).
    bool extension;
    bool on;
    // Whether it is seen in a system header too (clang's ShowInSystemHeader), and whether -Werror leaves it a warning
    // (DefaultWarnNoWerror: only -Werror= of it and a pragma make it an error).
    bool system;
    bool no_werror;
};

static const PPWarningKind pp_warnings[BUSTER_PP_WARNINGS] =
{
    { "c23-extensions", "c2x-extensions", NULL, true, true, false, false },
    { "unicode", NULL, NULL, false, true, false, false },
    { "unicode", NULL, NULL, true, true, false, false },
    { "delimited-escape-sequence-extension", NULL, "pedantic", true, false, false, false },
    { "#warnings", NULL, NULL, false, true, true, false },
    { "#pragma-messages", NULL, NULL, false, true, false, true },
    { "pedantic", NULL, NULL, true, false, false, false },
    { "pre-c23-compat", "pre-c2x-compat", NULL, false, false, false, false },
    { "extra-tokens", NULL, NULL, true, true, false, false },
    { "c99-extensions", NULL, NULL, true, true, false, false },
    { "macro-redefined", NULL, NULL, true, true, false, false },
    { "builtin-macro-redefined", NULL, NULL, true, true, false, false },
    { "variadic-macros", NULL, NULL, true, true, false, false },
    { "unknown-pragmas", NULL, "pragmas most all", true, true, false, false },
    { "unknown-warning-option", NULL, NULL, true, true, false, false },
    { "pragma-once-outside-header", NULL, NULL, false, true, false, false },
    { "pragma-system-header-outside-header", NULL, NULL, false, true, false, false },
    { "nonportable-include-path", NULL, NULL, false, true, false, false },
    { "multichar", NULL, "most all", false, true, false, false },
    { "unknown-escape-sequence", NULL, NULL, true, true, false, false },
    { "implicitly-unsigned-literal", NULL, NULL, true, true, false, false },
    { "invalid-source-encoding", NULL, NULL, true, true, false, false },
    { "empty-translation-unit", NULL, NULL, true, false, false, false },
    { "reserved-macro-identifier", NULL, "reserved-identifier", false, false, false, false },
    { NULL, NULL, NULL, false, true, false, false },
    { NULL, NULL, NULL, true, true, false, false },
};

_Static_assert(BUSTER_PP_WARNINGS <= sizeof(((Preprocessor*)0)->warning_mappings), "a mapping for each warning");

// Whether `length` bytes at `name` (an option's name without -W) name warning `w`'s group: its own name, its old one, a
// group that holds it, or everything.
static bool pp_warning_group_named(uint32_t w, const char* name, uint32_t length)
{
    const PPWarningKind* kind = pp_warnings + w;
    const char* names[3] = { kind->name, kind->alias, "everything" };
    bool result = false;
    for (uint32_t k = 0; k < 3; k += 1)
    {
        result |= names[k] != NULL && strlen(names[k]) == length && memcmp(names[k], name, length) == 0;
    }
    for (const char* parent = kind->parents; parent != NULL && *parent != 0;)
    {
        const char* space = strchr(parent, ' ');
        const uint32_t size = space != NULL ? (uint32_t)(space - parent) : (uint32_t)strlen(parent);
        result |= size == length && memcmp(parent, name, length) == 0;
        parent = space != NULL ? space + 1 : parent + size;
    }
    return result;
}

// The severity a warning of `warning` at `location` is given with, as clang maps it (#1934); BUSTER_PP_IGNORED for none.
// First whether it is seen at all, as a warning, before any promotion counts: none under -w, and none in a system header
// without -Wsystem-headers, unless its kind is seen there (PPWarningKind.system). Then the command line's: its last -W or
// -Wno- of its group's names (pp_warning_group_named), or for an extension off by default -pedantic, -Wpedantic or
// -pedantic-errors, promoted by -Werror (unless -Werror leaves it alone), -Werror= of the group, or -pedantic-errors of an
// extension, unless -Wno-error= of it demotes it. The source's #pragma clang diagnostic for it replaces that
// (Preprocessor.warning_mappings).
static PPSeverity pp_warning_severity(Preprocessor* pp, PPWarning warning, uint32_t location)
{
    const PPWarningKind* info = pp_warnings + warning;
    bool enabled = info->on;
    bool errors = false;
    bool group_error = false;
    bool demoted = false;
    bool pedantic_errors = false;
    for (uint32_t i = 0; i < pp->options.semantic_warning_option_count; i += 1)
    {
        const char* option = pp->options.semantic_warning_options[i];
        const uint32_t length = (uint32_t)strlen(option);
        const bool pedantic = strcmp(option, "-pedantic") == 0 || strcmp(option, "-pedantic-errors") == 0 || strcmp(option, "-Wpedantic") == 0;
        const bool on = (length > 2 && option[0] == '-' && option[1] == 'W' && strncmp(option, "-Wno-", 5) != 0 && strncmp(option, "-Werror", 7) != 0 &&
                         pp_warning_group_named(warning, option + 2, length - 2)) ||
                        (pedantic && info->extension && !info->on);
        const bool off = strncmp(option, "-Wno-", 5) == 0 && strncmp(option, "-Wno-error", 10) != 0 && pp_warning_group_named(warning, option + 5, length - 5);
        const bool promote = strncmp(option, "-Werror=", 8) == 0 && pp_warning_group_named(warning, option + 8, length - 8);
        const bool demote = strncmp(option, "-Wno-error=", 11) == 0 && pp_warning_group_named(warning, option + 11, length - 11);
        enabled = on || promote ? true : off ? false : enabled;
        group_error = promote ? true : demote ? false : group_error;
        demoted = demote ? true : promote ? false : demoted;
        errors = strcmp(option, "-Werror") == 0 ? !info->no_werror : strcmp(option, "-Wno-error") == 0 ? false : errors;
        pedantic_errors |= strcmp(option, "-pedantic-errors") == 0;
    }
    PPSeverity result = !enabled ? BUSTER_PP_IGNORED :
                        group_error || ((errors || (pedantic_errors && info->extension)) && !demoted) ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING;
    const uint8_t mapping = pp->warning_mappings[warning];
    result = mapping == PP_PRAGMA_MAPPING_IGNORED ? BUSTER_PP_IGNORED : mapping == PP_PRAGMA_MAPPING_WARNING ? PP_SEVERITY_WARNING :
             mapping == PP_PRAGMA_MAPPING_ERROR ? PP_SEVERITY_MAPPED_ERROR : mapping == PP_PRAGMA_MAPPING_FATAL ? PP_SEVERITY_MAPPED_FATAL : result;
    const bool seen = !pp->options.suppress_warnings && (info->system || pp_diagnostic_warning_visible(pp, location, location != 0 ? pp->diagnostic_origin : 0));
    return seen ? result : BUSTER_PP_IGNORED;
}

// The room for a warning's flag (pp_warning_flag): " [-Werror,-W" and the longest group's name.
#define BUSTER_PP_WARNING_FLAG_BYTES 64

// The severity a warning of `warning` at `location` is given with (pp_warning_severity), PP_SEVERITY_WARNING_ALWAYS for a
// warning seen in system headers too, and its flag as clang prints it after the message, into `flag`: " [-Wgroup]", or
// " [-Werror,-Wgroup]" where promoted; for one of no group " [-Werror]" where promoted, else nothing.
static PPSeverity pp_warning_flag(Preprocessor* pp, PPWarning warning, uint32_t location, char flag[BUSTER_PP_WARNING_FLAG_BYTES])
{
    const PPWarningKind* kind = pp_warnings + warning;
    const PPSeverity severity = pp_warning_severity(pp, warning, location);
    const bool promoted = severity == PP_SEVERITY_ERROR || severity == PP_SEVERITY_MAPPED_ERROR;
    flag[0] = 0;
    if (kind->name != NULL)
    {
        snprintf(flag, BUSTER_PP_WARNING_FLAG_BYTES, " [%s-W%s]", promoted ? "-Werror," : "", kind->name);
    }
    else if (promoted)
    {
        snprintf(flag, BUSTER_PP_WARNING_FLAG_BYTES, " [-Werror]");
    }
    return severity == PP_SEVERITY_WARNING && kind->system ? PP_SEVERITY_WARNING_ALWAYS : severity;
}

// A warning of `warning` (pp_warning_flag) at `location`, the `length` bytes of its message at `message` with its flag.
static __attribute__((noinline, cold)) void pp_diagnose_group_text(Preprocessor* pp, PPWarning warning, uint32_t location, const char* message, uint32_t length)
{
    char flag[BUSTER_PP_WARNING_FLAG_BYTES];
    const PPSeverity severity = pp_warning_flag(pp, warning, location, flag);
    if (severity != BUSTER_PP_IGNORED)
    {
        pp_diagnose(pp, severity, location, "%.*s%s", (int)length, message, flag);
    }
}

// A warning of `warning` at `location` (pp_diagnose_group_text), its message formatted from `format`.
static __attribute__((noinline, cold, format(printf, 4, 5))) void pp_diagnose_group(Preprocessor* pp, PPWarning warning, uint32_t location, const char* format, ...)
{
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    const uint32_t length = written < 0 ? 0 : (uint32_t)written < sizeof(message) ? (uint32_t)written : (uint32_t)sizeof(message) - 1;
    pp_diagnose_group_text(pp, warning, location, message, length);
}

// The line marker's note that a location in range `range` is in (#1954), defined below; the history's include stacks
// read it.
static uint32_t pp_line_note_at(const Preprocessor* pp, uint32_t range, uint32_t location);
#include "buster/compiler/c_preprocessor_history_impl.c"

// Room for `more` tokens in a stack, or a fatal error.
static bool pp_room(Preprocessor* pp, PPTokens* tokens, uint32_t more)
{
    if (tokens == &pp->arguments && more != 0 && tokens->count < pp->argument_indexed)
    {
        pp->argument_indexed = tokens->count;
    }
    const bool result = (uint64_t)tokens->count + more <= tokens->capacity;
    if (!result && !pp->fatal)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many tokens (macro expansion past %u tokens)", tokens->capacity);
    }
    return result;
}

// A new range of `length` locations (at least 1) after the last one. Returns its index, or BUSTER_PP_NONE (and a fatal
// error) if the location space is full.
static uint32_t pp_add_range(Preprocessor* pp, uint32_t length, uint32_t file, const uint8_t* bytes, uint32_t parent, uint32_t include_location)
{
    uint32_t result = BUSTER_PP_NONE;
    const uint64_t size = (uint64_t)length + 1;
    const uint64_t limit = pp->options.location_limit != 0 ? pp->options.location_limit : UINT32_MAX;
    if ((uint64_t)pp->location_top + size >= limit || pp->range_count == (BUSTER_PP_MAX_RANGES >> pp->capacity_shift))
    {
        // As clang reports it: an inclusion at its #include, and generated spellings without a place; once.
        if (pp->fatal)
        {
        }
        else if (include_location != 0)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, include_location, "this include makes the translation unit too large: it passes the location space (%lu bytes)",
                        (unsigned long)limit);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "the translation unit is too large: it passes the location space (%lu bytes)", (unsigned long)limit);
        }
    }
    else
    {
        result = pp->range_count;
        pp->ranges[result] = (PPRange){
            .base = pp->location_top,
            .length = (uint32_t)size,
            .file = file,
            .bytes = bytes,
            .parent = parent,
            .include_location = include_location,
            .first_line_directive = BUSTER_PP_NONE,
            .last_line_directive = BUSTER_PP_NONE,
        };
        pp->range_count += 1;
        pp->location_top += (uint32_t)size;
    }
    return result;
}

BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) bool pp_location_in_main_file(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    const bool result = range != BUSTER_PP_NONE && pp->ranges[range].file == pp->main_file;
    return result;
}

// A parsed unit without a token (#2222): clang's parser's -Wempty-translation-unit extension, "ISO C requires a translation
// unit to contain at least one declaration", at the main file's end of file, which clang's lexer puts at a final newline.
BUSTER_PREPROCESSOR_ENTRY __attribute__((unused)) void pp_diagnose_empty_unit(Preprocessor* pp)
{
    // The main file's own range: its bytes, not the scratch space's; its length past the bytes' terminator.
    const PPFileData* data = pp->main_file < pp->file_count ? pp->datas + pp->files[pp->main_file].data : NULL;
    const uint8_t* bytes = data != NULL ? data->bytes : NULL;
    uint32_t range = BUSTER_PP_NONE;
    for (uint32_t r = 0; bytes != NULL && range == BUSTER_PP_NONE && r < pp->range_count; r += 1)
    {
        range = pp->ranges[r].file == pp->main_file && pp->ranges[r].bytes == bytes ? r : BUSTER_PP_NONE;
    }
    if (range != BUSTER_PP_NONE)
    {
        const PPRange* main = pp->ranges + range;
        const uint32_t length = data->length < main->length ? data->length : main->length;
        const bool newline = length != 0 && main->bytes != NULL && (main->bytes[length - 1] == '\n' || main->bytes[length - 1] == '\r');
        pp_diagnose_group(pp, PP_WARNING_EMPTY_TRANSLATION_UNIT, main->base + length - (newline ? 1u : 0u),
                          "ISO C requires a translation unit to contain at least one declaration");
    }
}

// Copies `length` bytes into the scratch space, and returns their location (or 0 and a fatal error). A zero byte follows
// them, so that an identifier spelled there ends where its spelling does when it is read as written
// (pp_identifier_as_written), rather than running into the next spelling.
static uint32_t pp_scratch_in(Preprocessor* pp, const uint8_t* bytes, uint32_t length, bool clean)
{
    uint32_t result = 0;
    uint32_t* current = &pp->scratch_range[clean];
    uint32_t* used = &pp->scratch_used[clean];
    if (*current == BUSTER_PP_NONE || *used + length >= pp->ranges[*current].length - 1)
    {
        const uint32_t chunk = length >= BUSTER_PP_SCRATCH_CHUNK_BYTES ? length + 1 : BUSTER_PP_SCRATCH_CHUNK_BYTES;
        uint8_t* memory = arena_push(&pp->scratch[clean], chunk, 1);
        *current = memory != 0 ? pp_add_range(pp, chunk, BUSTER_PP_NONE, memory, BUSTER_PP_NONE, 0) : BUSTER_PP_NONE;
        *used = 0;
        if (memory == 0)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of scratch memory for generated spellings");
        }
    }
    if (*current != BUSTER_PP_NONE)
    {
        const PPRange* range = &pp->ranges[*current];
        memcpy((uint8_t*)range->bytes + *used, bytes, length);
        ((uint8_t*)range->bytes)[*used + length] = 0;
        result = range->base + *used;
        *used += length + 1;
    }
    return result;
}

// Scratch spellings that are read as source again (pastes and _Pragma's text, which clang lexes again): their line
// splices and trigraphs come out when they are read.
static uint32_t pp_scratch(Preprocessor* pp, const uint8_t* bytes, uint32_t length)
{
    return pp_scratch_in(pp, bytes, length, false);
}

// Scratch spellings that are final (stringized strings, the builtins' strings and numbers), which clang makes as clean
// tokens: they are read as they are, so a "??=" a stringized argument spells stays one, even with trigraphs on.
static uint32_t pp_scratch_clean(Preprocessor* pp, const uint8_t* bytes, uint32_t length)
{
    return pp_scratch_in(pp, bytes, length, true);
}

// The body of a string literal's spelling (pp_spelling's) if it is a raw string literal: its prefix, R and the opening
// quote, the delimiter and '(', then the body, then ')', the delimiter and the closing quote (C has no user-defined
// suffix), as the lexer makes one. The body's characters are as written, without escape sequences.
static bool pp_raw_string_body(const uint8_t* spelling, uint32_t length, const uint8_t** body, uint32_t* body_length)
{
    const uint8_t* quote = memchr(spelling, '"', length);
    const uint8_t* open = quote != 0 && quote > spelling && quote[-1] == 'R' ? memchr(quote, '(', length - (uint32_t)(quote - spelling)) : 0;
    const bool result = open != 0;
    if (result)
    {
        const uint32_t delimiter = (uint32_t)(open - quote) - 1;
        *body = open + 1;
        *body_length = length - (uint32_t)(open + 1 - spelling) - delimiter - 2;
    }
    return result;
}

// The place after an output token of `kind` at `location`, from the place before it: a pragma line starts at its '#'
// (the scratch "#", pp_emit_pragma_start's) and ends at its BUSTER_PP_KIND_PRAGMA_END.
static uint32_t pp_pragma_state(const Preprocessor* pp, uint32_t state, uint8_t kind, uint32_t location)
{
    const bool start = kind == C_TOKEN_HASH && location == pp->hash_location;
    const bool end = kind == BUSTER_PP_KIND_PRAGMA_END;
    return start ? BUSTER_PP_PRAGMA_NAME : end ? BUSTER_PP_PRAGMA_OUTSIDE : state == BUSTER_PP_PRAGMA_NAME ? BUSTER_PP_PRAGMA_TOKENS : state;
}

// Whether -E spells an output token of `kind` at place `state` as written.
static bool pp_spelled_as_written(uint32_t state, uint8_t kind)
{
    const bool identifier = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT);
    return state == BUSTER_PP_PRAGMA_TOKENS && identifier;
}

// The spelling -E prints for an identifier it spells as written (pp_spelled_as_written), in `arena`:
// pp_identifier_as_written's, or the interned one when its location is in no range. Sets *spelling to null if `arena`
// is full. Returns the length.
static uint32_t pp_written_spelling(Preprocessor* pp, uint32_t value, uint32_t location, Arena* arena, const uint8_t** spelling)
{
    const uint32_t range = pp_find_range(pp, location);
    uint32_t result = intern_length(pp->table, value);
    *spelling = intern_bytes(pp->table, value);
    if (range != BUSTER_PP_NONE)
    {
        const PPRange* bytes_range = &pp->ranges[range];
        const uint32_t start = location - bytes_range->base;
        const uint32_t end = pp_identifier_end(pp, bytes_range->bytes, bytes_range->length, start);
        uint8_t* buffer = arena_push(arena, (uint64_t)(end - start) + 1, 1);
        result = buffer != 0 ? pp_without_splices(pp, C_TOKEN_RAW_IDENTIFIER, bytes_range->bytes + start, end - start, buffer, spelling) : 0;
        *spelling = buffer != 0 ? *spelling : 0;
    }
    return result;
}

// The record of the file at `path` (`length` bytes, NUL-terminated), probing the file system only the first time the
// cache is asked about the path; BUSTER_PP_NONE if no file can be read there, or if the cache is full, with why in
// `*error` (the probe's errno value or -1, or EMFILE for a full cache). A file of up to the cache's read limit is read
// into the calling lane's bytes arena, which the cache keeps as long as its records, and a larger one is mapped with
// MAP_POPULATE (all of them if the arena can't be reserved, so that the file's preparation, which needs the lane's other
// arenas, says why it fails). A path whose first `directory_length` bytes are a directory the search looks in, followed
// by a '/' (BUSTER_PP_NONE for none), is answered by the directory's listing once it has one (file_cache_listed_absent):
// a path the listing rules out gets the entry a probe that found no file there (ENOENT) gives it, without the probe.
// `*known` (if `known` isn't null) caches the directory's FileCacheDirectory for its caller: a search directory's.
// Until some directory has a listing, only a failed probe looks its directory up. One lane at a time.
static uint32_t file_cache_lookup_path(FileCache* cache, const char* path, uint32_t length, uint32_t directory_length, FileCacheDirectory** known,
                                       FileCacheCounts* counts, int* error)
{
    const uint64_t hash = intern_hash_bytes((const uint8_t*)path, length);
    FileCachePath* entry = file_cache_find_path(cache, path, length, hash);
    int unheld = 0;
    if (entry == 0)
    {
        const bool in_directory = file_cache_in_directory(path, length, directory_length);
        FileCacheDirectory* directory = known != 0 ? *known : 0;
        directory = directory == 0 && in_directory && cache->listed_directories != 0 ? file_cache_directory(cache, path, directory_length) : directory;
        FileProbe probe = { .record = BUSTER_PP_NONE, .error = ENOENT };
        const bool absent = directory != 0 && file_cache_listed_absent(directory, path, length);
        counts->listed += absent;
        if (!absent)
        {
            Arena* bytes = &file_cache_arenas(cache, lane_index())->bytes;
            file_cache_probe(cache, path, FILE_CACHE_MAP_POPULATED, cache->read_limit, bytes->base != 0 ? bytes : 0, &probe, counts);
            directory = directory == 0 && in_directory && probe.error == ENOENT ? file_cache_directory(cache, path, directory_length) : directory;
            file_cache_count_miss(cache, directory, &probe, counts);
        }
        if (known != 0 && directory != 0)
        {
            *known = directory;
        }
        *error = probe.error;
        entry = file_cache_add_path(cache, path, length, hash, &probe);
        if (entry != 0)
        {
            file_cache_identify(cache, entry);
        }
        else if (probe.mapped)
        {
            munmap((void*)probe.bytes, probe.length);
        }
        unheld = probe.found ? EMFILE : probe.error;
    }
    const uint32_t result = entry != 0 ? entry->record : BUSTER_PP_NONE;
    *error = result != BUSTER_PP_NONE ? 0 : entry == 0 ? unheld : entry->probe.found ? EMFILE : entry->probe.error;
    return result;
}

// Prepares record `record` (pp_prepare_into) unless it is prepared, into lane `lane`'s arenas and with the cache's intern
// table, which only one lane at a time may write. Adds each phase's TSC ticks to `ticks` unless it is null.
static PPPrepareStatus file_cache_prepare(FileCache* cache, uint32_t record, uint32_t lane, uint64_t* ticks)
{
    PPFileData* data = &cache->records[record];
    PPPrepareStatus result = PP_PREPARE_DONE;
    if (!data->prepared)
    {
        FileCacheArenas* arenas = file_cache_arenas(cache, lane);
        result = pp_prepare_into(&cache->prepare, cache->table, arenas->columns, &arenas->arena, data, ticks);
        cache->prepared_records += result == PP_PREPARE_DONE;
        cache->prepared_tokens += result == PP_PREPARE_DONE ? data->token_count : 0;
    }
    return result;
}

// How this unit prepares files (pp_prepare_options of its options).
static PPPrepare pp_prepare_config(const Preprocessor* pp)
{
    return (PPPrepare){
        .refine_flags = pp->refine_flags,
        .split_kinds = pp->split_kinds,
        .dollar_punctuation = pp_dollar_punctuation(&pp->options),
        .vectorized = pp->vectorized,
        .window_bytes = pp->options.window_bytes,
    };
}

// This unit's data of cache record `record`: made the first time the unit names the record's file, with the record's
// bytes but nothing prepared yet (pp_prepare_data copies that when the unit first enters the file). Returns
// BUSTER_PP_NONE if the unit has as many files as it can hold.
static uint32_t pp_data_of_record(Preprocessor* pp, uint32_t record)
{
    uint32_t result = pp->record_data[record] - 1;
    if (pp->record_data[record] == 0 && pp->data_count < (BUSTER_PP_MAX_FILES >> pp->capacity_shift))
    {
        const PPFileData* shared = &pp->cache->records[record];
        result = pp->data_count;
        pp->datas[result] = (PPFileData){
            .device = shared->device,
            .inode = shared->inode,
            .modified = shared->modified,
            .has_modified = shared->has_modified,
            .bytes = shared->bytes,
            .length = shared->length,
            .record = record,
        };
        pp->data_count += 1;
        pp->record_data[record] = result + 1;
    }
    return result;
}

// A unit's data of a cache record takes what the record has prepared.
static void pp_copy_prepared(PPFileData* data, const PPFileData* record)
{
    data->token_count = record->token_count;
    data->kinds = record->kinds;
    data->starts = record->starts;
    data->values = record->values;
    data->space = record->space;
    data->raw_errors = record->raw_errors;
    data->raw_error_count = record->raw_error_count;
    data->directive_count = record->directive_count;
    data->directive_hash = record->directive_hash;
    data->directive_end = record->directive_end;
    data->directive_kind = record->directive_kind;
    data->directive_next = record->directive_next;
    data->note_count = record->note_count;
    data->note_directives = record->note_directives;
    data->note_kinds = record->note_kinds;
    data->guard = record->guard;
    data->unterminated_comment = record->unterminated_comment;
    data->unterminated_comment_starts_line = record->unterminated_comment_starts_line;
    data->guard_opener = record->guard_opener;
    data->line_pages = record->line_pages;
    data->prepared = true;
}

// Prepares a file this unit enters for the first time: one it made (the predefines, the command line) into its own
// arenas, and any other through the file cache, which prepares each file once for every unit (file_cache_prepare) and
// whose record the unit's data then copies. Diagnoses a file that can't be prepared, and returns false.
static bool pp_prepare_data(Preprocessor* pp, PPFileData* data, const char* path)
{
    uint64_t* ticks = pp->options.timing ? pp->ticks : 0;
    PPPrepareStatus status;
    if (data->record == BUSTER_PP_NONE)
    {
        const PPPrepare prepare = pp_prepare_config(pp);
        status = pp_prepare_into(&prepare, pp->table, pp->columns, &pp->arena, data, ticks);
    }
    else
    {
        status = file_cache_prepare(pp->cache, data->record, lane_index(), ticks);
        if (status == PP_PREPARE_DONE)
        {
            pp_copy_prepared(data, &pp->cache->records[data->record]);
        }
    }
    if (status == PP_PREPARE_DONE)
    {
        pp->input_tokens += data->token_count;
    }
    else if (status == PP_PREPARE_NO_MEMORY)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "%s: out of memory", path);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "%s: can't tokenize (%s)", path, status == PP_PREPARE_TOO_BIG ? "too big" : "out of memory");
    }
    return status == PP_PREPARE_DONE;
}

// Doubles the path table and places every path again. The table starts small and doubles when it is half full, so
// that the pages it touches are about the ones it needs: at its largest size, the first paths would each touch a huge
// page of their own, which the kernel clears (2% of the time of sqlite3.c's run).
static void pp_grow_file_slots(Preprocessor* pp)
{
    const uint32_t slots = 2 * (pp->file_slot_mask + 1);
    memset(pp->file_slots, 0, slots * sizeof(uint32_t));
    pp->file_slot_mask = slots - 1;
    for (uint32_t f = 0; f < pp->file_count; f += 1)
    {
        uint32_t slot = (uint32_t)intern_hash_bytes((const uint8_t*)pp->files[f].path, pp->files[f].path_length) & pp->file_slot_mask;
        while (pp->file_slots[slot] != 0)
        {
            slot = (slot + 1) & pp->file_slot_mask;
        }
        pp->file_slots[slot] = f + 1;
    }
}

// This unit's record of `path` (`length` bytes, not NUL-terminated): looked up by name, and the first time through the
// file cache (file_cache_lookup_path), which probes the file system only for a path it never looked up. Its data is
// BUSTER_PP_NONE if no file can be read there, and its error why, when that isn't that there is no file there (no such
// file, a part of the path that isn't a directory, or a directory, which clang's header search takes as none there:
// HeaderSearch::getFileAndSuggestModule). `search_length` is the length of the directory the search looked for the path
// in (a search directory's, or the includer's), which the path's next byte, a '/', ends, or BUSTER_PP_NONE for none:
// the file cache answers from the directory's listing once it has one (file_cache_lookup_path), through `search`, the
// unit's FileCacheDirectory of a search directory (or null). Returns BUSTER_PP_NONE if memory runs out.
static uint32_t pp_lookup_path(Preprocessor* pp, const char* path, uint32_t length, uint32_t search_length, FileCacheDirectory** search, uint8_t system)
{
    const uint64_t hash = intern_hash_bytes((const uint8_t*)path, length);
    uint32_t slot = (uint32_t)hash & pp->file_slot_mask;
    uint32_t result = BUSTER_PP_NONE;
    bool done = false;
    while (!done)
    {
        const uint32_t entry = pp->file_slots[slot];
        if (entry == 0)
        {
            done = true;
        }
        else if (pp->files[entry - 1].path_length == length && memcmp(pp->files[entry - 1].path, path, length) == 0)
        {
            result = entry - 1;
            done = true;
        }
        else
        {
            slot = (slot + 1) & pp->file_slot_mask;
        }
    }
    // The table has room: it is never more than half full, and can grow to twice as large as the most files there can be.
    if (result == BUSTER_PP_NONE && pp->file_count < (BUSTER_PP_MAX_FILES >> pp->capacity_shift))
    {
        char* copy = arena_push(&pp->arena, (uint64_t)length + 1, 1);
        if (copy != 0)
        {
            memcpy(copy, path, length);
            copy[length] = 0;
            uint32_t directory_length = BUSTER_PP_NONE;
            for (uint32_t i = 0; i < length; i += 1)
            {
                directory_length = path[i] == '/' ? i : directory_length;
            }
            int error = 0;
            const uint64_t start = pp->options.timing ? __rdtsc() : 0;
            const uint32_t record = file_cache_lookup_path(pp->cache, copy, length, search_length, search, &pp->probes, &error);
            const uint32_t data = record != BUSTER_PP_NONE ? pp_data_of_record(pp, record) : BUSTER_PP_NONE;
            error = record != BUSTER_PP_NONE && data == BUSTER_PP_NONE ? EMFILE : error;
            pp->ticks[PP_PHASE_READ] += pp->options.timing ? __rdtsc() - start : 0;
            result = pp->file_count;
            pp->files[result] = (PPFile){
                .path = copy,
                .path_length = length,
                .directory_length = directory_length,
                .data = data,
                .error = error,
                .system = system,
            };
            pp->file_count += 1;
            pp->file_slots[slot] = result + 1;
            if (2 * pp->file_count > pp->file_slot_mask + 1)
            {
                pp_grow_file_slots(pp);
            }
        }
    }
    return result;
}

// Whether buster's own headers' directory has a file `name` (`length` bytes), which clang's resource directory after
// it (pp->fallback_directory) then doesn't supply, to the search or beside one of its own headers: so a #include_next
// in buster's header goes on past clang's header of the same name, to the C library's. The path is the one the search
// of buster's directory looks up, so its record is usually in the table already (a #include_next in buster's header
// names that header).
static bool pp_buster_supplies(Preprocessor* pp, const uint8_t* name, uint32_t length)
{
    char path[PATH_MAX];
    const uint32_t directory_length = pp->buster_directory_length;
    bool result = false;
    if ((uint64_t)directory_length + 1 + length < PATH_MAX)
    {
        memcpy(path, pp->buster_directory, directory_length);
        path[directory_length] = '/';
        memcpy(path + directory_length + 1, name, length);
        const uint32_t record = pp_lookup_path(pp, path, directory_length + 1 + length, directory_length, 0, BUSTER_PP_SYSTEM_FILE);
        result = record != BUSTER_PP_NONE && pp->files[record].data != BUSTER_PP_NONE;
    }
    return result;
}

// Finds the file an include names, looking at each of its search's candidates in turn (pp_include_candidate) until a
// file is at one. Returns the PPFile, or BUSTER_PP_NONE if no file has the name; `*found_in` gets the search directory
// it was found in, or BUSTER_PP_NONE (the includer's directory, or an absolute path). A place the search can't look in,
// a path that can't be opened for another reason than there being no file there (PPFile.error) or one PATH_MAX bytes
// long or longer, which no system call takes (ENAMETOOLONG), is a fatal error at `report`, as clang's
// getFileAndSuggestModule reports it, and ends the search; with `report` BUSTER_PP_NONE, it is a place without the file.
// With --buster-headers, clang's resource directory after buster's (pp->fallback_directory) doesn't supply a name
// buster's directory has (pp_buster_supplies), neither as a search directory nor as the directory of an includer of
// clang's: a #include_next in buster's header goes on past clang's header of the same name, to the C library's.
// The optional timing spans this whole search, including candidate construction, cached hits/misses, file probing
// and first byte acquisition, but not header-name expansion or the later preparation/execution of the found file.
// It overlaps PP_PHASE_READ and the main-loop remainder; it is not another additive phase. No search nests in it.
static uint32_t pp_find_include(Preprocessor* pp, const uint8_t* name, uint32_t length, bool angled, bool next, uint32_t includer, uint32_t includer_search,
                                uint32_t* found_in, uint32_t report)
{
    const uint64_t opens = pp->options.timing ? pp->probes.opens : 0;
    const uint64_t fstats = pp->options.timing ? pp->probes.fstats : 0;
    const uint64_t start = pp->options.timing ? __rdtsc() : 0;
    char path[PATH_MAX];
    const PPFile* from = includer != BUSTER_PP_NONE ? &pp->files[includer] : 0;
    PPIncluder source = { 0 };
    if (from != 0)
    {
        source = (PPIncluder){ .path = from->path, .directory_length = from->directory_length, .system = from->system, .present = true };
    }
    uint32_t result = BUSTER_PP_NONE;
    bool failed = false;
    *found_in = BUSTER_PP_NONE;
    PPCandidate candidate = PP_CANDIDATE_SKIP;
    for (uint32_t index = 0; result == BUSTER_PP_NONE && !failed && candidate != PP_CANDIDATE_END; index += 1)
    {
        uint32_t path_length = 0;
        uint8_t system = 0;
        uint32_t directory = BUSTER_PP_NONE;
        candidate = pp_include_candidate(pp, name, length, angled, next, &source, includer_search, index, path, &path_length, &system, &directory);
        // The place's directory as clang names it: the includer's, a search directory, or none for an absolute name.
        const bool absolute = length != 0 && name[0] == '/';
        const bool own = candidate != PP_CANDIDATE_END && !absolute && directory == BUSTER_PP_NONE;
        const char* prefix = own ? (source.directory_length != BUSTER_PP_NONE ? source.path : ".") : directory != BUSTER_PP_NONE ? pp->directories[directory] : "";
        const uint32_t prefix_length = own ? (source.directory_length != BUSTER_PP_NONE ? source.directory_length : 1)
                                           : directory != BUSTER_PP_NONE ? pp->directory_lengths[directory] : 0;
        // Clang's resource directory behind buster's, which doesn't supply a name buster's directory has.
        const uint32_t fallback = pp->fallback_directory;
        const bool behind = fallback != BUSTER_PP_NONE &&
                            (own ? prefix_length == pp->directory_lengths[fallback] && memcmp(prefix, pp->directories[fallback], prefix_length) == 0 : directory == fallback);
        const bool shadowed = candidate != PP_CANDIDATE_END && behind && pp_buster_supplies(pp, name, length);
        int error = candidate == PP_CANDIDATE_SKIP && !shadowed ? ENAMETOOLONG : 0;
        if (candidate == PP_CANDIDATE_PATH && !shadowed)
        {
            const uint32_t file = pp_lookup_path(pp, path, path_length, absolute ? BUSTER_PP_NONE : prefix_length,
                                                 directory != BUSTER_PP_NONE ? &pp->directory_entries[directory] : 0, system);
            result = file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE ? file : BUSTER_PP_NONE;
            *found_in = result != BUSTER_PP_NONE ? directory : BUSTER_PP_NONE;
            error = file != BUSTER_PP_NONE ? pp->files[file].error : 0;
        }
        failed = error != 0 && !pp_include_error_absent(error) && report != BUSTER_PP_NONE;
        if (failed)
        {
            // The place as clang names it: the directory, a '/' and the name, or an absolute name alone.
            pp_diagnose(pp, PP_SEVERITY_FATAL, report, "cannot open file '%.*s%s%.*s': %s", (int)prefix_length, prefix, absolute ? "" : "/", (int)length,
                        (const char*)name, strerror(error));
        }
    }
    if (pp->options.timing)
    {
        pp->statistics.include_ticks += __rdtsc() - start;
        pp->statistics.include_searches += 1;
        pp->statistics.include_found += result != BUSTER_PP_NONE;
        pp->statistics.include_opens += pp->probes.opens - opens;
        pp->statistics.include_fstats += pp->probes.fstats - fstats;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Macros

BUSTER_INLINE bool pp_is_macro_vector(const Preprocessor* pp, IdentId id)
{
    return (pp->is_macro[id / 64] >> (id % 64)) & 1;
}

// The scalar reference reads the macro table itself, independently of the cached name bits. Poisoned names and
// `defined` also have records: both must stop a run, although pp_is_defined does not consider them definitions.
BUSTER_INLINE bool pp_is_macro(const Preprocessor* pp, IdentId id)
{
    return BUSTER_VECTOR_PATH(pp->vectorized) ? pp_is_macro_vector(pp, id) : pp->macro_of[id] != 0;
}

// Sets `name`'s macro (0 for none: a poisoned name then gets its own record back, as pp_poison makes it).
static void pp_set_macro(Preprocessor* pp, IdentId name, PPMacroId macro)
{
    const bool poisoned = macro == 0 && pp->poison_count != 0 && ((pp->poisoned[name / 64] >> (name % 64)) & 1);
    const PPMacroId id = poisoned ? pp_new_macro(pp, name) : macro;
    if (poisoned && id != 0)
    {
        pp->macros[id].flags = BUSTER_PP_MACRO_BUILTIN;
        pp->macros[id].builtin = (uint8_t)PP_BUILTIN_POISONED;
    }
    pp->macro_of[name] = id;
    pp->macro_epoch += 1;
    if (id != 0)
    {
        pp->is_macro[name / 64] |= 1ull << (name % 64);
    }
    else
    {
        pp->is_macro[name / 64] &= ~(1ull << (name % 64));
    }
}

// A new macro record (id from 1). Returns 0 and a fatal error if there are too many.
static PPMacroId pp_new_macro(Preprocessor* pp, IdentId name)
{
    PPMacroId result = 0;
    if (pp->macro_count < (BUSTER_PP_MAX_MACROS >> pp->capacity_shift))
    {
        result = pp->macro_count;
        pp->macros[result] = (PPMacro){ .name = name };
        pp->macro_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many macro definitions");
    }
    return result;
}

static void pp_define_builtin(Preprocessor* pp, IdentId name, PPBuiltin builtin, bool function_like)
{
    const PPMacroId macro = pp_new_macro(pp, name);
    if (macro != 0)
    {
        pp->macros[macro].flags = BUSTER_PP_MACRO_BUILTIN | (function_like ? BUSTER_PP_MACRO_FUNCTION : 0);
        pp->macros[macro].builtin = (uint8_t)builtin;
        pp_set_macro(pp, name, macro);
    }
}

// Poisoned names (#pragma GCC poison), as clang has them: a name is poisoned for good, whether or not it is a macro, and
// an identifier with its name is an error where clang reads it with a lexer (Preprocessor::HandleIdentifier): from a
// file or a _Pragma's text, outside a skipped group, in a directive's line (but #error's and #warning's text, a header
// name and poison's own names), among a macro's arguments, and from a paste (TokenLexer::Lex), but not from a macro's
// expansion otherwise. A poisoned name that isn't a macro has a record of PP_BUILTIN_POISONED, which stops the file
// runs at it (pp_check_poison then); one that is keeps its macro, which expands after the error.
BUSTER_INLINE bool pp_is_poisoned(const Preprocessor* pp, IdentId id)
{
    return pp->poison_count != 0 && ((pp->poisoned[id / 64] >> (id % 64)) & 1);
}

// Whether `id` names a macro, as #ifdef asks: a poisoned name's own record isn't one, nor is `defined`'s.
BUSTER_INLINE bool pp_is_defined(const Preprocessor* pp, IdentId id)
{
    const uint8_t builtin = pp_is_macro(pp, id) ? pp->macros[pp->macro_of[id]].builtin : PP_BUILTIN_POISONED;
    return builtin != PP_BUILTIN_POISONED && builtin != PP_BUILTIN_DEFINED;
}

BUSTER_INLINE void pp_check_poison(Preprocessor* pp, IdentId id, uint32_t location)
{
    if (pp_is_poisoned(pp, id))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "attempt to use a poisoned identifier");
    }
}

// pp_check_poison for the identifiers of tokens [begin, end) of a file read in the range at `base` (`tokens` null) or of
// a stack, but for a header name's in <...>, which clang lexes as one token: at `header` (an #include's or a dependency
// pragma's operand, or BUSTER_PP_NONE), and after `__has_include(`, `__has_include_next(` and `__has_embed(` when
// `queries` (a directive's line, where clang evaluates them).
static void pp_check_poison_range(Preprocessor* pp, const PPFileData* data, uint32_t base, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t header,
                                  bool queries)
{
    bool in_header = false;
    uint8_t before = 0;
    IdentId before_name = 0;
    for (uint32_t t = begin; t < end; t += 1)
    {
        const uint8_t kind = tokens != 0 ? tokens->kinds[t] : (uint8_t)(data->kinds[t] & 0x7F);
        const uint32_t value = tokens != 0 ? tokens->values[t] : data->values[t];
        const uint32_t location = tokens != 0 ? tokens->locations[t] : base + data->starts[t];
        const bool query = queries && before == C_TOKEN_L_PAREN &&
                           (before_name == C_NAME_HAS_INCLUDE || before_name == C_NAME_HAS_INCLUDE_NEXT || before_name == C_NAME_HAS_EMBED);
        if (in_header)
        {
            in_header = kind != C_TOKEN_GREATER;
        }
        else if (kind == C_TOKEN_LESS && (t == header || query))
        {
            in_header = true;
        }
        else if (kind == C_TOKEN_RAW_IDENTIFIER)
        {
            pp_check_poison(pp, value, location);
        }
        before_name = kind == C_TOKEN_L_PAREN ? before_name : kind == C_TOKEN_RAW_IDENTIFIER ? value : 0;
        before = kind;
    }
}

// Poisons `name` (pp_is_poisoned): a macro now gets a warning and stays one, `defined` stays itself, and another name gets
// its record.
static void pp_poison(Preprocessor* pp, IdentId name, uint32_t location)
{
    if (!pp_is_poisoned(pp, name))
    {
        if (pp_is_defined(pp, name))
        {
            pp_diagnose_group(pp, PP_WARNING_PLAIN, location, "poisoning existing macro");
        }
        else if (!pp_is_macro(pp, name))
        {
            pp_define_builtin(pp, name, PP_BUILTIN_POISONED, false);
        }
        pp->poisoned[name / 64] |= 1ull << (name % 64);
        pp->poison_count += 1;
    }
}

// The bytes of a file token (from its start to its end, which an identifier's id replaced: refine_identifier_end
// finds it again).
static uint32_t pp_file_token_end(const Preprocessor* pp, const PPFileData* data, uint32_t token)
{
    uint32_t result = data->values[token];
    if ((data->kinds[token] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        result = pp_identifier_end(pp, data->bytes, data->length, data->starts[token]);
    }
    return result;
}

// A scalar lexer over a file's bytes, for the scans of what lies between its tokens: they read the bytes as the lexer
// does (get_char: line splices removed, and trigraphs with `refine_flags`'s).
BUSTER_INLINE ScalarLexer pp_file_lexer(uint32_t refine_flags, const PPFileData* data)
{
    const ScalarLexer result = {
        .source = data->bytes,
        .length = data->length,
        .trigraphs = (refine_flags & C_TOKENIZER_TRIGRAPHS) != 0,
        .cplusplus = false,
    };
    return result;
}

// Past the comment that starts at byte `p` of a file, its delimiters read as the lexer reads them (get_char, so that a
// line splice may split one: `*\` and a '/' on the next line close a block comment): past a block comment's "*/" (or at
// the end of the file without one), or at a line comment's newline, which a splice doesn't end. `p` if no comment starts
// there.
static uint64_t pp_skip_comment(const ScalarLexer* lexer, uint64_t p)
{
    uint64_t size;
    uint64_t at;
    uint64_t result = p;
    if (get_char(lexer, p, &size, &at) == '/')
    {
        uint64_t second;
        const int32_t kind = get_char(lexer, p + size, &second, &at);
        uint64_t q = p + size + second;
        bool done = kind != '*' && kind != '/';
        bool star = false;
        while (!done)
        {
            uint64_t step;
            const int32_t c = get_char(lexer, q, &step, &at);
            done = c == BUSTER_LEX_END_OF_FILE || (kind == '*' ? star && c == '/' : c == '\n' || c == '\r');
            star = c == '*';
            q = kind == '/' && done ? at : q + step;
        }
        result = kind == '*' || kind == '/' ? q : p;
    }
    return result;
}

// The first place at or past byte `p` of a file that isn't a blank, a line splice or a comment, read as the lexer reads
// them: where a token starts, a newline outside a comment, or the end of the file.
static uint64_t pp_skip_blanks(const ScalarLexer* lexer, uint64_t p)
{
    uint64_t result = p;
    bool done = false;
    while (!done)
    {
        uint64_t size;
        uint64_t at;
        const int32_t c = get_char(lexer, result, &size, &at);
        const uint64_t past = pp_skip_comment(lexer, result);
        const bool blank = c == ' ' || c == '\t' || c == '\v' || c == '\f';
        done = c == BUSTER_LEX_END_OF_FILE || (!blank && past == result);
        result = done ? at : past != result ? past : result + size;
    }
    return result;
}

// The end of the line that byte `p` of a file is on: its newline (not one a line splice joins to the next line), or the
// end of the file.
static uint64_t pp_line_end(const ScalarLexer* lexer, uint64_t p)
{
    uint64_t result = p;
    bool done = false;
    while (!done)
    {
        uint64_t size;
        uint64_t at;
        const int32_t c = get_char(lexer, result, &size, &at);
        done = c == '\n' || c == '\r' || c == BUSTER_LEX_END_OF_FILE;
        result = done ? at : result + size;
    }
    return result;
}

// The end of a directive's line as clang lexes it on from byte `stop` of a file, past a header name the file's tokens
// didn't know (pp_header_name_tail): its first newline outside a block comment, which carries the line on past its
// newlines, or the end of the file. Each piece of the line, from where the lexer is between tokens (`stop`, or the end
// of a block comment) to the next newline (pp_line_end), is lexed once: when it ends inside a block comment, the next
// piece starts past that comment's end.
static uint32_t pp_relex_end(uint32_t refine_flags, const PPFileData* data, uint32_t stop)
{
    const ScalarLexer lexer = pp_file_lexer(refine_flags, data);
    uint64_t from = stop;
    uint64_t result = pp_line_end(&lexer, from);
    bool inside = true;
    while (inside && result < data->length)
    {
        const uint32_t length = (uint32_t)(result - from);
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
        uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
        uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        const int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(data->bytes + from, length, kinds, starts, ends, refine_flags) : 0;
        inside = count > 0 && pp_is_unterminated_comment(data->bytes + from, kinds[count - 1], starts[count - 1]);
        from = inside ? pp_skip_comment(&lexer, from + starts[count - 1]) : from;
        result = inside ? pp_line_end(&lexer, from) : result;
        scratch_end(scratch);
    }
    return (uint32_t)result;
}

// A block comment in a confirmed source header name can hide later file lines, unlike a line comment or a closed
// same-line comment, whose suffix is already restored by pp_header_name_tail. Inspect only the prepared token gap
// before the header's end, using the current masked lexical bytes so a repaired name never consumes capacity twice.
static bool pp_header_comment_overlap(const Preprocessor* pp, PPContext* file, uint32_t first, uint32_t stop)
{
    const PPFileData* data = &pp->datas[file->data];
    const uint8_t* bytes = data->lexical_bytes != 0 ? data->lexical_bytes : data->bytes;
    bool result = false;
    if (memchr(bytes + first, '*', stop - first) != 0)
    {
        if (file->header_gap_data != file->data)
        {
            file->header_gap_data = file->data;
            file->header_gap_token = 0;
        }
        // Confirmed source names advance monotonically within a view. Each prepared token is passed at most once.
        while (file->header_gap_token < data->token_count && data->starts[file->header_gap_token] < stop)
        {
            file->header_gap_token += 1;
        }
        ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
        lexer.source = bytes;
        uint64_t p = file->header_gap_token != 0 ? pp_file_token_end(pp, data, file->header_gap_token - 1) : stop;
        bool done = p < first;
        while (!done && p < stop)
        {
            uint64_t size;
            uint64_t at;
            const int32_t c = get_char(&lexer, p, &size, &at);
            uint64_t second;
            const int32_t next = c == '/' ? get_char(&lexer, p + size, &second, &at) : 0;
            const bool block = c == '/' && next == '*';
            const bool blank = c == ' ' || c == '\t' || c == '\v' || c == '\f';
            if (block)
            {
                const uint64_t past = pp_skip_comment(&lexer, p);
                ScalarLexer comment = lexer;
                comment.length = past;
                const bool unclosed = data->unterminated_comment != 0 && data->unterminated_comment - 1 >= first && data->unterminated_comment - 1 < stop;
                // Bound the newline scan to this comment. Scanning the remaining line for each query is quadratic.
                result = pp_line_end(&comment, stop) < past || unclosed;
                p = past;
            }
            else
            {
                p += size != 0 ? size : 1;
            }
            done = result || (!block && !blank);
        }
    }
    return result;
}

// The rest of a directive's line [first, end) past a header name read from the file up to its '>' (pp_read_header_name),
// which ends at byte `stop`: the first of the line's tokens there or past it. The lexer read the line before the name was
// known, so one of its tokens or comments may run past `stop` (`<a.h>>`, or `<a//b.h>`, whose "//" began a comment to
// the line's end), and then its tokens from `stop` on aren't those clang lexes there, on from the name. `*relex` gets
// the end of what must then be lexed again, the end of the line as clang lexes it from `stop` (pp_relex_end), which may
// be past the directive's end in the file's tokens (pp_resume_after); else `stop`.
static uint32_t pp_header_name_tail(const Preprocessor* pp, const PPFileData* data, uint32_t first, uint32_t end, uint32_t stop, uint32_t* relex)
{
    uint32_t result = first + 1;
    while (result < end && data->starts[result] < stop)
    {
        result += 1;
    }
    // Where clang lexes its first token past the name, or the line's end, against where the file's next token is. With
    // none of the file's tokens past the name on its line, both lines must end at the same newline: a block comment clang
    // begins after the name may carry its line past the one the file's tokens end at (`<a//b> /*` with `*/` below).
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    const uint64_t next = pp_skip_blanks(&lexer, stop);
    const bool line_ended = next >= data->length || data->bytes[next] == '\n' || data->bytes[next] == '\r';
    const bool same = result < end ? next == data->starts[result] : line_ended && next == pp_skip_blanks(&lexer, pp_file_token_end(pp, data, result - 1));
    *relex = same ? stop : pp_relex_end(pp->refine_flags, data, stop);
    return result;
}

// A file context goes on after a directive whose line clang lexes to byte `end` of the file (pp_relex_end), past where
// the file's tokens end it: a block comment begun after a header name in which "//" began a comment for them. It goes on
// at its first token past `end`, and the directives before that are left out: they are in that comment for clang.
static void pp_resume_after(PPContext* context, const PPFileData* data, uint32_t end)
{
    while (context->position < data->token_count && data->starts[context->position] < end)
    {
        context->position += 1;
    }
    while (context->next_directive < data->directive_count && data->directive_hash[context->next_directive] < context->position)
    {
        context->next_directive += 1;
        context->hidden_directives = true;
    }
}

// Where clang reports what a directive's line lacks: its end, the newline after its last token (`last`), past blanks,
// comments and line splices (pp_skip_blanks), or the end of the file.
static uint32_t pp_line_end_location(const Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t last)
{
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    return base + (uint32_t)pp_skip_blanks(&lexer, pp_file_token_end(pp, data, last));
}

// Where a file's lexer is after directive d's line, past the newline that ends it: where clang's printer is after the
// change of file or of presumed position that the directive makes (FileChanged's location: an included file left, a
// #line).
static uint32_t pp_directive_resume(const Preprocessor* pp, const PPContext* context, uint32_t d)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t end = pp_line_end_location(pp, data, base, data->directive_end[d] - 1) - base;
    const uint32_t crlf = end + 1 < data->length && data->bytes[end] == '\r' && data->bytes[end + 1] == '\n';
    return base + (end < data->length ? end + 1 + crlf : end);
}

// Do two file tokens have the same spelling? (Identifiers by id; others by their spellings, which differ for 1 and 01
// or <: and [ even when their kinds match, but not for 1\<newline>0 and 10: line splices are removed first.)
static bool pp_same_file_token(const Preprocessor* pp, const PPFileData* a, uint32_t i, const PPFileData* b, uint32_t j)
{
    bool result = (a->kinds[i] & 0x7F) == (b->kinds[j] & 0x7F);
    if (result && (a->kinds[i] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
    {
        result = a->values[i] == b->values[j];
    }
    else if (result)
    {
        const uint32_t a_length = a->values[i] - a->starts[i];
        const uint32_t b_length = b->values[j] - b->starts[j];
        Scratch scratch = scratch_begin(0);
        uint8_t* a_buffer = arena_push(scratch.arena, (uint64_t)a_length + 1, 1);
        uint8_t* b_buffer = arena_push(scratch.arena, (uint64_t)b_length + 1, 1);
        const uint8_t* a_spelling = a->bytes + a->starts[i];
        const uint8_t* b_spelling = b->bytes + b->starts[j];
        uint32_t a_spelled = a_length;
        uint32_t b_spelled = b_length;
        if (a_buffer != 0 && b_buffer != 0)
        {
            a_spelled = pp_without_splices(pp, a->kinds[i] & 0x7F, a->bytes + a->starts[i], a_length, a_buffer, &a_spelling);
            b_spelled = pp_without_splices(pp, b->kinds[j] & 0x7F, b->bytes + b->starts[j], b_length, b_buffer, &b_spelling);
        }
        result = a_spelled == b_spelled && memcmp(a_spelling, b_spelling, a_spelled) == 0;
        scratch_end(scratch);
    }
    return result;
}

// Are two definitions the same (C 6.10.4p2): the same form, parameters and replacement list, with whitespace in the
// same places? A redefinition that isn't is diagnosed.
static bool pp_same_definition(const Preprocessor* pp, const PPMacro* a, const PPMacro* b)
{
    const PPFileData* a_data = &pp->datas[a->data];
    const PPFileData* b_data = &pp->datas[b->data];
    const uint32_t a_parameters_end = (a->flags & BUSTER_PP_MACRO_FUNCTION) ? a->body_begin : a->parameters_begin;
    const uint32_t b_parameters_end = (b->flags & BUSTER_PP_MACRO_FUNCTION) ? b->body_begin : b->parameters_begin;
    bool result = ((a->flags ^ b->flags) & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_VARIADIC | BUSTER_PP_MACRO_BUILTIN)) == 0 &&
                  a->parameter_count == b->parameter_count && a->body_end - a->body_begin == b->body_end - b->body_begin &&
                  a_parameters_end - a->parameters_begin == b_parameters_end - b->parameters_begin && (a->flags & BUSTER_PP_MACRO_BUILTIN) == 0;
    for (uint32_t i = 0; result && i < a_parameters_end - a->parameters_begin; i += 1)
    {
        result = pp_same_file_token(pp, a_data, a->parameters_begin + i, b_data, b->parameters_begin + i);
    }
    for (uint32_t i = 0; result && i < a->body_end - a->body_begin; i += 1)
    {
        const uint32_t ai = a->body_begin + i;
        const uint32_t bi = b->body_begin + i;
        result = pp_same_file_token(pp, a_data, ai, b_data, bi) && (i == 0 || pp_file_space(a_data, ai) == pp_file_space(b_data, bi));
    }
    return result;
}

static void pp_scan_body_vector(const uint8_t* kinds, const uint32_t* values, uint32_t begin, uint32_t end, bool* pastes, bool* hashes, bool* va_opt)
{
#if !BUSTER_HOST_VECTOR
    pp_scan_body_scalar(kinds, values, begin, end, pastes, hashes, va_opt);
#else
    uint64_t found_pastes = 0;
    uint64_t found_hashes = 0;
    uint64_t found_va_opt = 0;
    for (uint32_t t = begin; t < end; t += 64)
    {
        const __mmask64 lanes = _bzhi_u64(~0ull, end - t < 64 ? end - t : 64);
        const __m512i kind = _mm512_and_si512(_mm512_maskz_loadu_epi8(lanes, kinds + t), _mm512_set1_epi8(0x7F));
        found_pastes |= _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_HASHHASH));
        found_hashes |= _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_HASH));
        // An identifier's value is its id; another token's is its end, which may equal __VA_OPT__'s id.
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        for (uint32_t j = 0; j < 4 && (identifiers >> (16 * j)) != 0; j += 1)
        {
            const __mmask16 named = (__mmask16)(identifiers >> (16 * j));
            const __m512i ids = _mm512_maskz_loadu_epi32(named, values + t + 16 * j);
            found_va_opt |= _mm512_mask_cmpeq_epi32_mask(named, ids, _mm512_set1_epi32(C_NAME_VA_OPT));
        }
    }
    *pastes = found_pastes != 0;
    *hashes = found_hashes != 0;
    *va_opt = found_va_opt != 0;
#endif
}

// pp_scan_body_vector's scalar twin.
static void pp_scan_body_scalar(const uint8_t* kinds, const uint32_t* values, uint32_t begin, uint32_t end, bool* pastes, bool* hashes, bool* va_opt)
{
    *pastes = false;
    *hashes = false;
    *va_opt = false;
    for (uint32_t t = begin; t < end; t += 1)
    {
        *pastes |= (kinds[t] & 0x7F) == C_TOKEN_HASHHASH;
        *hashes |= (kinds[t] & 0x7F) == C_TOKEN_HASH;
        *va_opt |= (kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && values[t] == C_NAME_VA_OPT;
    }
}

// Where clang reports token t of a definition's line, [.., end): the token, or the line's end if t is past its last.
static uint32_t pp_define_location(const Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t t, uint32_t end)
{
    return t < end ? base + pp_start_with_splices(data->bytes, data->starts[t], pp->options.trigraphs) : pp_line_end_location(pp, data, base, end - 1);
}

// The punctuators' spellings, from C_TOKEN_L_SQUARE on (a digraph's is the token it stands for).
static const char* const pp_punctuator_spellings[] = {
    "[", "]", "(", ")", "{", "}", ".", "...", "&", "&&", "&=", "*", "*=", "+", "++", "+=", "-", "->", "--", "-=", "~", "!", "!=", "/", "/=", "%", "%=",
    "<", "<<", "<=", "<<=", ">", ">>", ">=", ">>=", "^", "^=", "|", "||", "|=", "?", ":", "::", ";", "=", "==", ",", "#", "##", ".*", "->*", "<=>",
};
_Static_assert(BUSTER_ARRAY_LENGTH(pp_punctuator_spellings) == C_TOKEN_KIND_COUNT - C_TOKEN_L_SQUARE, "a spelling per punctuator");

// How clang names token t's kind in a diagnostic ("missing ')' after %s"): a punctuator's spelling in quotes, a
// keyword's bare, "identifier", or another kind's name in angle brackets (<numeric_constant>).
static void pp_token_name(const Preprocessor* pp, const PPFileData* data, uint32_t t, char* name, uint32_t capacity)
{
    static const char* const kinds[] = {
        "unknown", "identifier", "numeric_constant", "char_constant", "wide_char_constant", "utf8_char_constant", "utf16_char_constant",
        "utf32_char_constant", "string_literal", "wide_string_literal", "utf8_string_literal", "utf16_string_literal", "utf32_string_literal",
    };
    _Static_assert(BUSTER_ARRAY_LENGTH(kinds) == C_TOKEN_L_SQUARE, "a name per kind before the punctuators");
    const uint8_t kind = data->kinds[t] & 0x7F;
    const IdentId id = data->values[t];
    const bool keyword = kind == C_TOKEN_RAW_IDENTIFIER && id < BUSTER_PP_KEYWORD_TABLE && pp->keyword_kinds[id] != C_TOKEN_RAW_IDENTIFIER;
    if (keyword)
    {
        snprintf(name, capacity, "%.*s", (int)intern_length(pp->table, id), (const char*)intern_bytes(pp->table, id));
    }
    else if (kind >= C_TOKEN_L_SQUARE && kind < C_TOKEN_KIND_COUNT)
    {
        snprintf(name, capacity, "'%s'", pp_punctuator_spellings[kind - C_TOKEN_L_SQUARE]);
    }
    else
    {
        snprintf(name, capacity, kind == C_TOKEN_RAW_IDENTIFIER ? "%s" : "<%s>", kinds[kind < C_TOKEN_L_SQUARE ? kind : 0]);
    }
}

static void pp_parameter_index_begin(PPParameterIndex* index, bool vectorized)
{
    (void)vectorized;

    // Only the occupied prefix is read. Clearing the inline arrays would add stores to every small definition.
    index->slots = 0;
    index->capacity = 0;
    index->count = 0;
    index->vectorized = BUSTER_VECTOR_PATH(vectorized);
    index->repeated = false;
    index->scratch.arena = 0;
}

// Only large macros allocate even the index's descriptor. Keep its arrays out of the ordinary compiler stack frame.
static PPParameterIndex* pp_parameter_index_allocate(bool vectorized)
{
    (void)vectorized;

    Scratch scratch = scratch_begin(0);
    PPParameterIndex* result = arena_push(scratch.arena, sizeof(PPParameterIndex), _Alignof(PPParameterIndex));
    if (result != 0)
    {
        pp_parameter_index_begin(result, BUSTER_VECTOR_PATH(vectorized));
        result->scratch = scratch;
    }
    else
    {
        scratch_end(scratch);
    }
    return result;
}

static uint32_t pp_parameter_slot(const uint64_t* slots, uint32_t capacity, IdentId id)
{
    // Mix high identifier bits too: ids separated by a table's capacity must not all start at one slot.
    const uint64_t hash = (uint64_t)id * 0x9E3779B97F4A7C15ull;
    uint32_t result = (uint32_t)(hash >> 32) & (capacity - 1);
    while (slots[result] != 0 && (IdentId)(slots[result] >> 32) != id)
    {
        result = (result + 1) & (capacity - 1);
    }
    return result;
}

static __attribute__((noinline)) uint32_t pp_parameter_index(const PPParameterIndex* index, IdentId id)
{
    uint32_t result = BUSTER_PP_NONE;
    if (index->slots != 0)
    {
        const uint32_t slot = pp_parameter_slot(index->slots, index->capacity, id);
        const uint32_t ordinal = (uint32_t)index->slots[slot];
        result = ordinal != 0 ? ordinal - 1 : BUSTER_PP_NONE;
    }
    else if (BUSTER_VECTOR_PATH(index->vectorized))
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, index->count);
        const __m512i names = _mm512_maskz_loadu_epi32(lanes, index->names);
        const uint32_t matches = _mm512_mask_cmpeq_epi32_mask(lanes, names, _mm512_set1_epi32((int)id));
        if (matches != 0)
        {
            result = index->ordinals[__builtin_ctz(matches)];
        }
    }
    else
    {
        for (uint32_t p = 0; result == BUSTER_PP_NONE && p < index->count; p += 1)
        {
            result = index->names[p] == id ? index->ordinals[p] : BUSTER_PP_NONE;
        }
    }
    return result;
}

// Inserts only the first occurrence, returning its previous ordinal in *previous. In particular, an explicit
// __VA_ARGS__ parameter precedes the implicit one from `...`, as clang's parameter lookup requires.
static bool pp_parameter_insert(PPParameterIndex* index, IdentId id, uint32_t ordinal, uint32_t* previous)
{
    *previous = pp_parameter_index(index, id);
    bool result = true;
    if (*previous == BUSTER_PP_NONE)
    {
        if (index->slots == 0 && index->count < BUSTER_PP_SMALL_PARAMETERS)
        {
            index->names[index->count] = id;
            index->ordinals[index->count] = ordinal;
        }
        else
        {
            if (index->count >= index->capacity / 2)
            {
                if (index->scratch.arena == 0)
                {
                    index->scratch = scratch_begin(0);
                }
                const uint32_t capacity = index->capacity == 0 ? 4 * BUSTER_PP_SMALL_PARAMETERS : index->capacity * 2;
                uint64_t* slots = capacity > index->capacity ? arena_push_array(index->scratch.arena, capacity, sizeof(uint64_t), 8) : 0;
                result = slots != 0;
                if (result)
                {
                    memset(slots, 0, (uint64_t)capacity * sizeof(uint64_t));
                    const uint32_t end = index->slots != 0 ? index->capacity : index->count;
                    for (uint32_t p = 0; p < end; p += 1)
                    {
                        const uint64_t entry = index->slots != 0 ? index->slots[p] : ((uint64_t)index->names[p] << 32) | (index->ordinals[p] + 1ull);
                        if (entry != 0)
                        {
                            slots[pp_parameter_slot(slots, capacity, (IdentId)(entry >> 32))] = entry;
                        }
                    }
                    index->slots = slots;
                    index->capacity = capacity;
                }
            }
            if (result)
            {
                index->slots[pp_parameter_slot(index->slots, index->capacity, id)] = ((uint64_t)id << 32) | (ordinal + 1ull);
            }
        }
        index->count += result;
    }
    return result;
}

static void pp_parameter_index_end(PPParameterIndex* index)
{
    if (index->scratch.arena != 0)
    {
        scratch_end(index->scratch);
    }
}

// Return the descriptor directly: passing the address of the caller's local pointer would force it onto the stack
// and add a stack-protector check to every ordinary macro. Keep this cold construction in its own frame.
static __attribute__((noinline)) PPParameterIndex* pp_parameter_index_build(const IdentId* names, uint32_t count, bool vectorized)
{
    (void)vectorized;

    PPParameterIndex* result = pp_parameter_index_allocate(BUSTER_VECTOR_PATH(vectorized));
    bool complete = result != 0;
    for (uint32_t p = 0; complete && p < count; p += 1)
    {
        uint32_t previous;
        complete = pp_parameter_insert(result, names[p], p, &previous);
    }
    if (!complete && result != 0)
    {
        pp_parameter_index_end(result);
        result = 0;
    }
    return result;
}

// Small compiled lists already have contiguous ids. Keep 0--4 names on the original bounded scalar lookup; a wider
// list takes one masked comparison. The caller selects the mode once per macro. Neither path copies the list.
BUSTER_INLINE uint32_t pp_parameter_index_small(const IdentId* names, uint32_t count, IdentId id, bool vectorized)
{
    (void)vectorized;

    uint32_t result = BUSTER_PP_NONE;
    if (BUSTER_VECTOR_PATH(vectorized))
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xFFFF, count);
        const __m512i ids = _mm512_maskz_loadu_epi32(lanes, names);
        const uint32_t matches = _mm512_mask_cmpeq_epi32_mask(lanes, ids, _mm512_set1_epi32((int)id));
        if (matches != 0)
        {
            result = (uint32_t)__builtin_ctz(matches);
        }
    }
    else
    {
        for (uint32_t p = 0; result == BUSTER_PP_NONE && p < count; p += 1)
        {
            result = names[p] == id ? p : BUSTER_PP_NONE;
        }
    }
    return result;
}

// A small definition's source list, including the implicit name of `...`. Its length is bounded by 16 parameters.
static bool pp_names_parameter(const PPFileData* data, const PPMacro* macro, IdentId id)
{
    bool result = false;
    for (uint32_t t = macro->parameters_begin; !result && t < macro->body_begin; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        const bool unnamed = kind == C_TOKEN_ELLIPSIS && (data->kinds[t - 1] & 0x7F) != C_TOKEN_RAW_IDENTIFIER;
        result = (kind == C_TOKEN_RAW_IDENTIFIER && data->values[t] == id) || (unnamed && id == C_NAME_VA_ARGS);
    }
    return result;
}

// Checks a function-like macro's replacement list as it is defined, for what pp_compile_macro would reject at its first
// expansion: # not followed by a parameter (or by __VA_OPT__ and its '('), __VA_OPT__ not followed by '(', inside
// another, or left open, and ## at either end of its content. Returns false after a diagnostic. As in clang, __VA_OPT__
// is read in a macro that isn't variadic too.
static bool pp_check_replacement_list(Preprocessor* pp, const PPFileData* data, const PPMacro* macro, const PPParameterIndex* parameters)
{
    const uint32_t end = macro->body_end;
    bool result = true;
    uint32_t va_opt_depth = 0;
    uint32_t va_opt_open = 0;
    for (uint32_t t = macro->body_begin; result && t < end; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        const bool next_is_identifier = t + 1 < macro->body_end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
        const bool va_opt = kind == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_VA_OPT;
        const bool next_va_opt = next_is_identifier && data->values[t + 1] == C_NAME_VA_OPT;
        const bool names_parameter = kind == C_TOKEN_HASH && next_is_identifier &&
                                     (macro->parameter_count <= BUSTER_PP_SMALL_PARAMETERS ? pp_names_parameter(data, macro, data->values[t + 1])
                                                                                        : pp_parameter_index(parameters, data->values[t + 1]) != BUSTER_PP_NONE);
        if (kind == C_TOKEN_HASH && !names_parameter && !next_va_opt)
        {
            // At the token after '#' (the line's end after the last), as clang reports it.
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t + 1, end), "'#' is not followed by a macro parameter");
            result = false;
        }
        else if (va_opt && va_opt_depth != 0)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "__VA_OPT__ cannot be nested within its own replacement tokens");
            result = false;
        }
        else if (va_opt && (t + 1 >= end || (data->kinds[t + 1] & 0x7F) != C_TOKEN_L_PAREN))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t + 1, end), "missing '(' following __VA_OPT__");
            result = false;
        }
        else if (va_opt && t + 2 < end && (data->kinds[t + 2] & 0x7F) == C_TOKEN_HASHHASH)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t + 2, end), "'##' cannot appear at start of __VA_OPT__ argument");
            result = false;
        }
        else if (va_opt)
        {
            va_opt_depth = 1;
            va_opt_open = t + 1;
            t += 1;
        }
        else if (va_opt_depth != 0 && kind == C_TOKEN_L_PAREN)
        {
            va_opt_depth += 1;
        }
        else if (va_opt_depth == 1 && kind == C_TOKEN_R_PAREN && (data->kinds[t - 1] & 0x7F) == C_TOKEN_HASHHASH)
        {
            // At the ')' that closes the group, as clang reports it.
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "'##' cannot appear at end of __VA_OPT__ argument");
            result = false;
        }
        else if (va_opt_depth != 0 && kind == C_TOKEN_R_PAREN)
        {
            va_opt_depth -= 1;
        }
    }
    if (result && va_opt_depth != 0)
    {
        // At the line's end, after the list's last token (the __VA_OPT__ itself when the '(' ends the list: clang reads
        // that '(' with it), with a note at the '('.
        char name[64];
        pp_token_name(pp, data, end - 1 == va_opt_open ? va_opt_open - 1 : end - 1, name, sizeof(name));
        const PPDiagnosticNote note =
        {
            .location = pp_define_location(pp, data, macro->base, va_opt_open, end),
            .message = "to match this '('",
        };
        pp_diagnose_note(pp, PP_SEVERITY_ERROR, pp_line_end_location(pp, data, macro->base, end - 1), &note, "missing ')' after %s", name);
        result = false;
    }
    return result;
}

// Is a defined macro one of clang's language-defined ones, whose redefinition and #undef it reports as "builtin": a
// builtin (__FILE__, __LINE__, _Pragma, __has_include, ...), or a name from __STDC that the predefines define?
static bool pp_language_defined(const Preprocessor* pp, const PPMacro* macro)
{
    const uint8_t* name = intern_bytes(pp->table, macro->name);
    const bool stdc = intern_length(pp->table, macro->name) >= 6 && memcmp(name, "__STDC", 6) == 0;
    bool result = (macro->flags & BUSTER_PP_MACRO_BUILTIN) != 0;
    if (!result && stdc)
    {
        const uint32_t range = pp_find_range(pp, macro->location);
        result = range != BUSTER_PP_NONE && pp->files[pp->ranges[range].file].data == pp->predefines_data;
    }
    return result;
}

// The cold continuation once the preceding 16 identifiers have passed their bounded duplicate/grammar checks.
static __attribute__((noinline)) PPParameterIndex* pp_define_parameter_large(Preprocessor* pp, const PPFileData* data, const PPMacro* macro, PPParameterIndex* parameters, IdentId name, uint32_t ordinal)
{
    PPParameterIndex* result = parameters;
    if (result == 0)
    {
        result = pp_parameter_index_allocate(BUSTER_VECTOR_PATH(pp->vectorized));
        if (result != 0)
        {
            for (uint32_t p = 0; p < BUSTER_PP_SMALL_PARAMETERS; p += 1)
            {
                result->names[p] = data->values[macro->parameters_begin + 2 * p];
                result->ordinals[p] = p;
            }
            result->count = BUSTER_PP_SMALL_PARAMETERS;
        }
    }
    if (result != 0)
    {
        uint32_t previous;
        const bool complete = pp_parameter_insert(result, name, ordinal, &previous);
        result->repeated = previous != BUSTER_PP_NONE;
        if (!complete)
        {
            pp_parameter_index_end(result);
            result = 0;
        }
    }
    return result;
}

// The parameter list of a function-like macro being defined, from its '(' at `open`, as clang reads it
// (ReadMacroParameterList): identifiers (keywords too) separated by commas, then `...` or GNU's `name...`, or none.
// Sets the macro's parameters, flags and body start, or reports the first error where clang does and returns false.
BUSTER_INLINE bool pp_define_parameters(Preprocessor* pp, const PPFileData* data, PPMacro* macro, uint32_t open, uint32_t end, PPParameterIndex** parameters)
{
    bool result = true;
    bool closed = false;
    uint32_t parameter_count = 0;
    uint32_t t = open + 1;
    macro->flags |= BUSTER_PP_MACRO_FUNCTION;
    macro->parameters_begin = t;
    while (result && !closed)
    {
        const uint8_t kind = t < end ? data->kinds[t] & 0x7F : C_TOKEN_UNKNOWN;
        if (t >= end)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "missing ')' in macro parameter list");
            result = false;
        }
        else if (kind == C_TOKEN_R_PAREN && parameter_count == 0)
        {
            closed = true;
        }
        else if (kind == C_TOKEN_R_PAREN)
        {
            // #define F(a,)
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "expected identifier in macro parameter list");
            result = false;
        }
        else if (kind == C_TOKEN_ELLIPSIS)
        {
            macro->flags |= BUSTER_PP_MACRO_VARIADIC;
            if (parameter_count >= BUSTER_PP_SMALL_PARAMETERS)
            {
                *parameters = pp_define_parameter_large(pp, data, macro, *parameters, C_NAME_VA_ARGS, parameter_count);
                result = *parameters != 0;
            }
            if (!result)
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, macro->location, "out of memory indexing macro parameters");
            }
            parameter_count += 1;
            t += 1;
            closed = t < end && (data->kinds[t] & 0x7F) == C_TOKEN_R_PAREN;
            if (result && !closed)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "missing ')' in macro parameter list");
                result = false;
            }
        }
        else if (kind != C_TOKEN_RAW_IDENTIFIER)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "invalid token in macro parameter list");
            result = false;
        }
        else
        {
            const IdentId parameter = data->values[t];
            bool repeated = false;
            if (parameter_count < BUSTER_PP_SMALL_PARAMETERS)
            {
                // Every preceding parameter was an identifier followed by a comma. This bounded path needs no index.
                for (uint32_t p = macro->parameters_begin; p < t; p += 2)
                {
                    repeated |= data->values[p] == parameter;
                }
            }
            else
            {
                *parameters = pp_define_parameter_large(pp, data, macro, *parameters, parameter, parameter_count);
                result = *parameters != 0;
                repeated = result && (*parameters)->repeated;
            }
            if (!result)
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, macro->location, "out of memory indexing macro parameters");
            }
            if (repeated)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "duplicate macro parameter name '%.*s'",
                            (int)intern_length(pp->table, parameter), intern_bytes(pp->table, parameter));
                result = false;
            }
            parameter_count += 1;
            t += 1;
            const uint8_t next = t < end ? data->kinds[t] & 0x7F : C_TOKEN_UNKNOWN;
            if (result && t < end && next == C_TOKEN_COMMA)
            {
                // Another parameter follows.
            }
            else if (result && t < end && next == C_TOKEN_R_PAREN)
            {
                closed = true;
            }
            else if (result && t < end && next == C_TOKEN_ELLIPSIS)
            {
                // GNU's named variadic parameter: `name...`.
                macro->flags |= BUSTER_PP_MACRO_VARIADIC;
                t += 1;
                closed = t < end && (data->kinds[t] & 0x7F) == C_TOKEN_R_PAREN;
                if (!closed)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "missing ')' in macro parameter list");
                    result = false;
                }
            }
            else if (result)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, macro->base, t, end), "expected comma in macro parameter list");
                result = false;
            }
        }
        t += 1;
    }
    if (result && parameter_count > BUSTER_PP_MAX_PARAMETERS)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, macro->location, "too many macro parameters (more than %u)", BUSTER_PP_MAX_PARAMETERS);
        result = false;
    }
    macro->parameter_count = result ? (uint16_t)parameter_count : 0;
    macro->body_begin = t;
    return result;
}

// The frequent empty and one-name lists need neither a duplicate scan nor the general grammar loop. Only complete,
// valid shapes take this path; variadics and every malformed list keep the ordered reader and its first diagnostic.
BUSTER_INLINE bool pp_define_parameters_tiny(const PPFileData* data, PPMacro* macro, uint32_t open, uint32_t end)
{
    const uint32_t first = open + 1;
    const uint8_t kind = first < end ? data->kinds[first] & 0x7F : C_TOKEN_UNKNOWN;
    const bool named = kind == C_TOKEN_RAW_IDENTIFIER;
    const bool result = kind == C_TOKEN_R_PAREN || (named && first + 1 < end && (data->kinds[first + 1] & 0x7F) == C_TOKEN_R_PAREN);
    if (result)
    {
        macro->flags |= BUSTER_PP_MACRO_FUNCTION;
        macro->parameters_begin = first;
        macro->parameter_count = named;
        macro->body_begin = first + 1 + named;
    }
    return result;
}

// #define, on the line [begin, end) of a file's tokens (the name's token first), read in the range at `base`. Its
// constraints (C 6.10.4) are checked as clang checks them, in its order, and reported where and as it reports them.
// -Wreserved-macro-identifier (#2222, clang's Preprocessor::CheckMacroName): a #define's or #undef's name `name` reserved in
// every context, `__x` or `_X`, "macro name is a reserved identifier" at it, before the directive's other checks. A
// #define of a feature-test macro clang's isFeatureTestMacro lists (`_GNU_SOURCE`) gives none, nor does a name in the
// predefines or the command line's definitions, which are virtual files, as clang's are its <built-in> buffer.
static void pp_reserved_macro_name(Preprocessor* pp, const PPFileData* data, IdentId name, uint32_t location, bool define)
{
    static const char* const features[] =
    {
        "_ATFILE_SOURCE", "_BSD_SOURCE", "_CRT_NONSTDC_NO_WARNINGS", "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES", "_CRT_SECURE_NO_WARNINGS",
        "_FILE_OFFSET_BITS", "_FORTIFY_SOURCE", "_GLIBCXX_ASSERTIONS", "_GLIBCXX_CONCEPT_CHECKS", "_GLIBCXX_DEBUG", "_GLIBCXX_DEBUG_PEDANTIC",
        "_GLIBCXX_PARALLEL", "_GLIBCXX_PARALLEL_ASSERTIONS", "_GLIBCXX_SANITIZE_VECTOR", "_GLIBCXX_USE_CXX11_ABI", "_GLIBCXX_USE_DEPRECATED",
        "_GNU_SOURCE", "_ISOC11_SOURCE", "_ISOC95_SOURCE", "_ISOC99_SOURCE", "_LARGEFILE64_SOURCE", "_POSIX_C_SOURCE", "_REENTRANT",
        "_SVID_SOURCE", "_THREAD_SAFE", "_XOPEN_SOURCE", "_XOPEN_SOURCE_EXTENDED", "__STDCPP_WANT_MATH_SPEC_FUNCS__", "__STDC_FORMAT_MACROS",
    };
    const uint32_t length = intern_length(pp->table, name);
    const char* bytes = (const char*)intern_bytes(pp->table, name);
    const bool reserved = length >= 2 && bytes[0] == '_' && (bytes[1] == '_' || (bytes[1] >= 'A' && bytes[1] <= 'Z'));
    const bool virtual_file = data->inode >= UINT64_MAX - BUSTER_PP_MAX_FILES;
    bool feature = false;
    for (uint32_t i = 0; reserved && define && !feature && i < BUSTER_ARRAY_LENGTH(features); i += 1)
    {
        feature = strlen(features[i]) == length && memcmp(features[i], bytes, length) == 0;
    }
    if (reserved && !feature && !virtual_file)
    {
        pp_diagnose_group(pp, PP_WARNING_RESERVED_MACRO_IDENTIFIER, location, "macro name is a reserved identifier");
    }
}

static void pp_define(Preprocessor* pp, uint32_t data_index, uint32_t base, uint32_t begin, uint32_t end)
{
    const PPFileData* data = &pp->datas[data_index];
    const uint32_t location = base + (begin < data->token_count ? data->starts[begin] : data->length);
    if (begin >= end)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, pp_line_end_location(pp, data, base, begin - 1), "macro name missing");
    }
    else if ((data->kinds[begin] & 0x7F) != C_TOKEN_RAW_IDENTIFIER)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "macro name must be an identifier");
    }
    else if (data->values[begin] == C_NAME_DEFINED)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'defined' cannot be used as a macro name");
    }
    else
    {
        const IdentId name = data->values[begin];
        pp_reserved_macro_name(pp, data, name, location, true);
        PPMacro macro = {
            .name = name,
            .data = data_index,
            .base = base,
            .parameters_begin = begin + 1,
            .body_begin = begin + 1,
            .body_end = end,
            .location = location,
        };
        bool valid = true;
        PPParameterIndex* parameters = 0;
        // Function-like when '(' follows the name with no whitespace or comment between them (a line splice is none).
        if (begin + 1 < end && (data->kinds[begin + 1] & 0x7F) == C_TOKEN_L_PAREN && !pp_file_space(data, begin + 1))
        {
            valid = pp_define_parameters_tiny(data, &macro, begin + 1, end) || pp_define_parameters(pp, data, &macro, begin + 1, end, &parameters);
        }
        else if (begin + 1 < end && !pp_file_space(data, begin + 1))
        {
            // An object-like macro's name needs whitespace after it: C99 requires it, C90 for characters outside its
            // basic set (clang's '@' and unknown tokens).
            const bool unknown = (data->kinds[begin + 1] & 0x7F) == C_TOKEN_UNKNOWN;
            const char* message = pp->options.language.standard >= 1999 ? "ISO C99 requires whitespace after the macro name"
                                  : unknown                              ? "whitespace required after macro name"
                                                                         : "whitespace recommended after macro name";
            // C99's -Wc99-extensions, and C90's of no group, an extension for a character outside its basic set.
            pp_diagnose_group(pp, pp->options.language.standard >= 1999 ? PP_WARNING_C99_EXTENSIONS : unknown ? PP_WARNING_PLAIN_EXTENSION : PP_WARNING_PLAIN,
                              pp_define_location(pp, data, base, begin + 1, end), "%s", message);
        }
        // ## can't begin or end a replacement list, and in a function-like macro # must precede a parameter and
        // __VA_OPT__ a '(' (pp_check_replacement_list): clang rejects such a definition when it reads it, used or not,
        // at the first of these it finds, in that order. __VA_OPT__ outside a variadic macro is a warning.
        if (valid && macro.body_begin < macro.body_end)
        {
            bool pastes;
            bool hashes;
            bool va_opt;
            if (BUSTER_VECTOR_PATH(pp->vectorized))
            {
                pp_scan_body_vector(data->kinds, data->values, macro.body_begin, macro.body_end, &pastes, &hashes, &va_opt);
            }
            else
            {
                pp_scan_body_scalar(data->kinds, data->values, macro.body_begin, macro.body_end, &pastes, &hashes, &va_opt);
            }
            macro.flags |= pastes ? BUSTER_PP_MACRO_PASTES : 0;
            const bool variadic = (macro.flags & BUSTER_PP_MACRO_VARIADIC) != 0;
            for (uint32_t t = macro.body_begin; va_opt && !variadic && t < macro.body_end; t += 1)
            {
                if ((data->kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_VA_OPT)
                {
                    pp_diagnose_group(pp, PP_WARNING_VARIADIC_MACROS, pp_define_location(pp, data, base, t, end), "__VA_OPT__ can only appear in the expansion of a variadic macro");
                }
            }
            if ((data->kinds[macro.body_begin] & 0x7F) == C_TOKEN_HASHHASH)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, base, macro.body_begin, end), "'##' cannot appear at start of macro expansion");
                valid = false;
            }
            // A function-like macro with a '#' or __VA_OPT__ gets the whole check: clang reads __VA_OPT__ in any
            // function-like macro (with a warning in one that isn't variadic, where its content is never used).
            const bool to_check = hashes || va_opt;
            valid = valid && (!to_check || !(macro.flags & BUSTER_PP_MACRO_FUNCTION) || pp_check_replacement_list(pp, data, &macro, parameters));
            if (valid && (data->kinds[macro.body_end - 1] & 0x7F) == C_TOKEN_HASHHASH)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, base, macro.body_end - 1, end), "'##' cannot appear at end of macro expansion");
                valid = false;
            }
        }
        if (valid)
        {
            const PPMacroId previous = pp_is_defined(pp, name) ? pp->macro_of[name] : 0;
            if (previous != 0 && pp_language_defined(pp, &pp->macros[previous]))
            {
                pp_diagnose_group(pp, PP_WARNING_BUILTIN_MACRO_REDEFINED, location, "redefining builtin macro");
            }
            else if (previous != 0 && !(pp->macros[previous].flags & BUSTER_PP_MACRO_REDEFINABLE) && !pp_same_definition(pp, &pp->macros[previous], &macro))
            {
                const PPDiagnosticNote note =
                {
                    .location = pp->macros[previous].location,
                    .message = "previous definition is here",
                };
                char flag[BUSTER_PP_WARNING_FLAG_BYTES];
                const PPSeverity severity = pp_warning_flag(pp, PP_WARNING_MACRO_REDEFINED, location, flag);
                if (severity != BUSTER_PP_IGNORED)
                {
                    pp_diagnose_note(pp, severity, location, &note, "'%.*s' macro redefined%s", (int)intern_length(pp->table, name), intern_bytes(pp->table, name), flag);
                }
            }
            const PPMacroId id = pp_new_macro(pp, name);
            if (id != 0)
            {
                pp->macros[id] = macro;
                pp_set_macro(pp, name, id);
            }
        }
        if (parameters != 0)
        {
            pp_parameter_index_end(parameters);
        }
    }
}

// Whether clang's substitution, reading body token t, sets NextTokGetsSpace (TokenLexer::ExpandFunctionArguments): it
// has a leading space, and it isn't the first token or one after ##.
BUSTER_INLINE bool pp_space_read(const PPMacro* macro, const PPFileData* data, uint32_t t)
{
    return t != macro->body_begin && pp_file_space(data, t) && (data->kinds[t - 1] & 0x7F) != C_TOKEN_HASHHASH;
}

// Compiles a macro's replacement list into items (PPItem): its parameters by index, # with its parameter, ## and
// __VA_OPT__ with its content. Returns false (with a diagnostic) if the list is invalid. A function-like macro without
// parameters has only tokens and ##, as in clang (TokenLexer::Init substitutes nothing then): its definition's checks
// passed, and its __VA_OPT__, '(' and ')' and a # before them stay tokens.
static bool pp_compile_macro(Preprocessor* pp, PPMacroId id)
{
    PPMacro* macro = &pp->macros[id];
    const PPFileData* data = &pp->datas[macro->data];
    const uint32_t parameter_count = macro->parameter_count;
    const bool substitutes = (macro->flags & BUSTER_PP_MACRO_FUNCTION) != 0 && parameter_count != 0;
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    bool result = pp->item_count + (macro->body_end - macro->body_begin) <= (BUSTER_PP_MAX_ITEMS >> pp->capacity_shift) &&
                  pp->parameter_count + parameter_count <= (BUSTER_PP_MAX_ITEMS >> pp->capacity_shift);
    // The parameters' ids: the identifiers of the parameter list, and __VA_ARGS__ for `...`.
    macro->parameters = pp->parameter_count;
    uint32_t parameter = 0;
    for (uint32_t t = macro->parameters_begin; result && substitutes && t < macro->body_begin; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        if (kind == C_TOKEN_RAW_IDENTIFIER)
        {
            pp->parameters[macro->parameters + parameter] = data->values[t];
            parameter += 1;
        }
        else if (kind == C_TOKEN_ELLIPSIS && (data->kinds[t - 1] & 0x7F) != C_TOKEN_RAW_IDENTIFIER)
        {
            pp->parameters[macro->parameters + parameter] = C_NAME_VA_ARGS;
            parameter += 1;
        }
    }
    pp->parameter_count += parameter_count;
    const IdentId* parameter_ids = result ? pp->parameters + macro->parameters : 0;
    PPParameterIndex* parameters = 0;
    if (result && parameter_count > BUSTER_PP_SMALL_PARAMETERS)
    {
        parameters = pp_parameter_index_build(parameter_ids, parameter_count, BUSTER_VECTOR_PATH(pp->vectorized));
        result = parameters != 0;
        if (!result)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, macro->location, "out of memory indexing macro parameters");
        }
    }
    const bool vector_parameters = BUSTER_VECTOR_PATH(pp->vectorized) && parameter_count > 4;
    macro->items_begin = pp->item_count;
    macro->expanded_parameters = 0;
    uint32_t va_opt_item = BUSTER_PP_NONE; // The open __VA_OPT__'s item,
    uint32_t va_opt_depth = 0; // its parentheses' depth,
    uint32_t va_opt_content = 0; // and its content's first token.
    for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
    {
        const uint8_t kind = data->kinds[t] & 0x7F;
        const uint8_t flags = pp_space_read(macro, data, t) ? BUSTER_PP_ITEM_SPACE : 0;
        PPItem item = { .kind = PP_ITEM_TOKEN, .flags = flags, .token = t };
        // __VA_OPT__ is the operator even when a parameter has its name, as in clang.
        const bool va_opt = substitutes && kind == C_TOKEN_RAW_IDENTIFIER && data->values[t] == C_NAME_VA_OPT;
        const uint32_t index = kind == C_TOKEN_RAW_IDENTIFIER && substitutes && !va_opt
                                   ? parameters != 0 ? pp_parameter_index(parameters, data->values[t])
                                                     : pp_parameter_index_small(parameter_ids, parameter_count, data->values[t], vector_parameters)
                                   : BUSTER_PP_NONE;
        if (index != BUSTER_PP_NONE)
        {
            item.kind = PP_ITEM_PARAMETER;
            item.parameter = (uint16_t)index;
        }
        else if (kind == C_TOKEN_HASHHASH)
        {
            item.kind = PP_ITEM_PASTE;
        }
        else if (va_opt)
        {
            if (va_opt_item != BUSTER_PP_NONE || t + 1 >= macro->body_end || (data->kinds[t + 1] & 0x7F) != C_TOKEN_L_PAREN)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t], "__VA_OPT__ must be followed by '(' and can't nest");
                result = false;
            }
            item.kind = PP_ITEM_VA_OPT;
            va_opt_item = pp->item_count;
            va_opt_depth = 1;
            t += 1;
            va_opt_content = t + 1;
        }
        else if (kind == C_TOKEN_HASH && substitutes)
        {
            const uint32_t next = t + 1;
            const bool next_va_opt = next < macro->body_end && (data->kinds[next] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && data->values[next] == C_NAME_VA_OPT;
            const uint32_t next_index = next < macro->body_end && (data->kinds[next] & 0x7F) == C_TOKEN_RAW_IDENTIFIER && !next_va_opt
                                            ? parameters != 0 ? pp_parameter_index(parameters, data->values[next])
                                                              : pp_parameter_index_small(parameter_ids, parameter_count, data->values[next], vector_parameters)
                                            : BUSTER_PP_NONE;
            if (next_index != BUSTER_PP_NONE)
            {
                item.kind = PP_ITEM_STRINGIZE;
                item.parameter = (uint16_t)next_index;
                t += 1;
            }
            else if (next_va_opt && va_opt_item == BUSTER_PP_NONE && next + 1 < macro->body_end && (data->kinds[next + 1] & 0x7F) == C_TOKEN_L_PAREN)
            {
                item.kind = PP_ITEM_STRINGIZE_VA_OPT;
                item.flags |= pp_space_read(macro, data, next) ? BUSTER_PP_ITEM_VA_OPT_SPACE : 0;
                va_opt_item = pp->item_count;
                va_opt_depth = 1;
                t += 2;
                va_opt_content = t + 1;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, macro->base + data->starts[t + (t + 1 < macro->body_end)], "'#' is not followed by a macro parameter");
                result = false;
            }
        }
        else if (va_opt_item != BUSTER_PP_NONE && kind == C_TOKEN_L_PAREN)
        {
            va_opt_depth += 1;
        }
        else if (va_opt_item != BUSTER_PP_NONE && kind == C_TOKEN_R_PAREN)
        {
            va_opt_depth -= 1;
            if (va_opt_depth == 0)
            {
                // The closing ')' isn't an item: the __VA_OPT__ item counts its content, and has its tokens' spaces.
                bool skipped = false;
                for (uint32_t k = va_opt_content; k <= t; k += 1)
                {
                    skipped |= pp_space_read(macro, data, k);
                }
                pp->items[va_opt_item].flags |= (uint8_t)((pp_space_read(macro, data, t) ? BUSTER_PP_ITEM_CLOSE_SPACE : 0) | (skipped ? BUSTER_PP_ITEM_SKIP_SPACE : 0));
                pp->items[va_opt_item].parameter = (uint16_t)(pp->item_count - va_opt_item - 1);
                va_opt_item = BUSTER_PP_NONE;
                continue;
            }
        }
        if (result)
        {
            pp->items[pp->item_count] = item;
            pp->item_count += 1;
        }
    }
    if (result && va_opt_item != BUSTER_PP_NONE)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, macro->location, "unterminated __VA_OPT__");
        result = false;
    }
    // The parameters substituted expanded: those that aren't an operand of ## (or of #, which has its own item). A
    // parameter that ends a __VA_OPT__'s content is no operand of a ## after the __VA_OPT__: the content is expanded first.
    // Without __VA_OPT__, the macro is BUSTER_PP_MACRO_IN_ORDER: each parameter's first such use is listed after the
    // items, as a PP_ITEM_PARAMETER item (`listed` has a bit per parameter), and its arguments are expanded in that order.
    // If there's no room for the list, its arguments are expanded by walking its body.
    PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = pp->item_count - macro->items_begin;
    // Literal runs belong to the immutable recipe. Record each token's suffix once instead of walking the same
    // items at every invocation. A removed __VA_OPT__ closing parenthesis leaves a gap in the source token indices,
    // so no run crosses its boundary. Capping a suffix splits a longer run into copies without growing PPItem.
    for (uint32_t i = count; result && i != 0;)
    {
        i -= 1;
        if (items[i].kind == PP_ITEM_TOKEN)
        {
            const bool consecutive = i + 1 < count && items[i + 1].kind == PP_ITEM_TOKEN && items[i + 1].token == items[i].token + 1;
            const uint32_t following = consecutive ? (uint32_t)items[i + 1].parameter + 1 : 0;
            items[i].parameter = (uint16_t)(following < UINT16_MAX ? following : UINT16_MAX);
        }
    }
    Scratch scratch = scratch_begin(0);
    const uint64_t words = ((uint64_t)parameter_count + 63) / 64;
    uint64_t* listed = arena_push_array(scratch.arena, words, sizeof(uint64_t), 8);
    if (listed != 0)
    {
        memset(listed, 0, words * sizeof(uint64_t));
    }
    uint32_t content_end = BUSTER_PP_NONE;
    bool in_order = listed != 0;
    uint32_t order_count = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        if (items[i].kind == PP_ITEM_PARAMETER)
        {
            const bool pasted = (i > 0 && items[i - 1].kind == PP_ITEM_PASTE) || (i + 1 < count && i + 1 != content_end && items[i + 1].kind == PP_ITEM_PASTE);
            const uint32_t parameter = items[i].parameter;
            macro->expanded_parameters |= pasted ? 0 : 1ull << (parameter < 63 ? parameter : 63);
            if (!pasted && in_order && !((listed[parameter / 64] >> (parameter % 64)) & 1))
            {
                listed[parameter / 64] |= 1ull << (parameter % 64);
                in_order = pp->item_count + order_count < (BUSTER_PP_MAX_ITEMS >> pp->capacity_shift);
                if (in_order)
                {
                    pp->items[pp->item_count + order_count] = (PPItem){ .kind = PP_ITEM_PARAMETER, .parameter = (uint16_t)parameter };
                    order_count += 1;
                }
            }
        }
        else if (items[i].kind == PP_ITEM_VA_OPT || items[i].kind == PP_ITEM_STRINGIZE_VA_OPT)
        {
            // __VA_OPT__ tests whether the expanded variadic argument has tokens (a macro that isn't variadic has none).
            const uint32_t variadic_index = parameter_count - 1U;
            macro->expanded_parameters |= variadic ? 1ull << (variadic_index < 63 ? variadic_index : 63) : 0;
            content_end = i + 1 + items[i].parameter;
            in_order = false;
        }
    }
    scratch_end(scratch);
    in_order = in_order && result;
    bool simple = in_order;
    uint32_t name_count = 0;
    for (uint32_t i = 0; simple && i < count; i += 1)
    {
        simple = items[i].kind == PP_ITEM_TOKEN || items[i].kind == PP_ITEM_PARAMETER;
        name_count += items[i].kind == PP_ITEM_TOKEN && (data->kinds[items[i].token] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
    }
    // A simple macro's body identifiers, after the order, if they fit.
    simple = simple && name_count <= UINT16_MAX && (uint64_t)pp->item_count + order_count + name_count <= (BUSTER_PP_MAX_ITEMS >> pp->capacity_shift);
    uint32_t named = 0;
    for (uint32_t i = 0; simple && i < count; i += 1)
    {
        if (items[i].kind == PP_ITEM_TOKEN && (data->kinds[items[i].token] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
        {
            pp->items[pp->item_count + order_count + named] = (PPItem){ .kind = PP_ITEM_TOKEN, .token = items[i].token };
            named += 1;
        }
    }
    // A macro with ## and # but no __VA_OPT__ (in_order) has a template too when each ## has a body token or a parameter
    // on either side, but not the variadic parameter on its right (GNU's `, ## __VA_ARGS__`, which pp_substitute reads
    // its own way). Its chains of pastes and its #s make their tokens in each invocation (pp_template_operators).
    uint32_t pastes = 0;
    uint32_t chains = 0;
    uint32_t strings = 0;
    bool operators = in_order && !simple && (macro->flags & BUSTER_PP_MACRO_FUNCTION) && count <= BUSTER_PP_TEMPLATE_TOKENS;
    for (uint32_t i = 0; operators && i < count; i += 1)
    {
        const uint8_t kind = items[i].kind;
        operators = kind == PP_ITEM_TOKEN || kind == PP_ITEM_PARAMETER || kind == PP_ITEM_STRINGIZE || kind == PP_ITEM_PASTE;
        if (kind == PP_ITEM_PASTE)
        {
            const bool inside = i > 0 && i + 1 < count;
            const uint8_t left = inside ? items[i - 1].kind : PP_ITEM_PASTE;
            const uint8_t right = inside ? items[i + 1].kind : PP_ITEM_PASTE;
            operators = (left == PP_ITEM_TOKEN || left == PP_ITEM_PARAMETER) && (right == PP_ITEM_TOKEN || right == PP_ITEM_PARAMETER) &&
                        !(variadic && right == PP_ITEM_PARAMETER && items[i + 1].parameter == parameter_count - 1u);
            // A chain starts at the operand before a ## that has no ## before it.
            chains |= inside && (i < 2 || !((pastes >> (i - 2)) & 1)) ? 1u << (i - 1) : 0;
            pastes |= 1u << i;
        }
        strings |= kind == PP_ITEM_STRINGIZE ? 1u << i : 0;
    }
    // A simple macro's template: its items are its body's tokens, one each. Only the vectorized kernels write expansions
    // from templates, in a run that records no history (a deferred unit's), which alone has their array
    // (pp_initialize): the scalar twins and the history's runs take pp_emit_simple_write and pp_substitute_simple, their
    // twin, or for a macro with ## and # the invocation stack and pp_substitute.
    const bool templated = (simple || operators) && pp->templates != 0 && count <= BUSTER_PP_TEMPLATE_TOKENS &&
                           parameter_count <= BUSTER_PP_TEMPLATE_TOKENS && pp->template_count < (BUSTER_PP_MAX_TEMPLATES >> pp->capacity_shift);
    if (templated)
    {
        PPTemplate* template = &pp->templates[pp->template_count];
        // A chain ends at an operand of a ## that no ## follows.
        *template = (PPTemplate){
            .count = (uint8_t)count,
            .pastes = (uint16_t)pastes,
            .chains = (uint16_t)chains,
            .ends = (uint16_t)((pastes << 1) & ~(pastes >> 1)),
            .strings = (uint16_t)strings,
        };
        // The operands of ##: a chain's token is its pastes' (or its one operand's that has a token).
        const uint32_t chained = pastes | (pastes << 1) | (pastes >> 1);
        // The items pp_substitute's loop reads, in the vectorized kernels: a run of body tokens as one, and a ## with
        // its right operand.
        for (uint32_t i = 0; i < count; i += 1)
        {
            template->anchor = (uint8_t)i;
            template->spelling = (uint8_t)(items[i].kind == PP_ITEM_PASTE ? i - 1 : i);
            i += items[i].kind == PP_ITEM_TOKEN ? items[i].parameter : items[i].kind == PP_ITEM_PASTE ? 1u : 0u;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t t = items[i].token;
            const uint8_t kind = data->kinds[t] & 0x7F;
            template->kinds[i] = kind;
            template->flags[i] = (uint8_t)(BUSTER_PP_FROM_MACRO | (pp_file_space(data, t) ? BUSTER_PP_SPACE : 0));
            template->values[i] = kind == C_TOKEN_RAW_IDENTIFIER ? data->values[t] : macro->base + data->values[t];
            template->locations[i] = macro->base + data->starts[t];
            template->parameters[i] = items[i].kind == PP_ITEM_PARAMETER || items[i].kind == PP_ITEM_STRINGIZE ? (uint8_t)items[i].parameter : 0;
            template->parameter_mask |= (uint16_t)(items[i].kind == PP_ITEM_PARAMETER ? 1u << i : 0);
            template->spaces |= (uint16_t)((items[i].flags & BUSTER_PP_ITEM_SPACE) ? 1u << i : 0);
            template->names |= (uint16_t)(items[i].kind == PP_ITEM_TOKEN && kind == C_TOKEN_RAW_IDENTIFIER && !((chained >> i) & 1) ? 1u << i : 0);
        }
        pp->template_count += 1;
    }
    macro->template_index = templated ? pp->template_count : 0;
    macro->flags |= in_order ? BUSTER_PP_MACRO_IN_ORDER : 0;
    macro->flags |= simple ? BUSTER_PP_MACRO_SIMPLE : 0;
    macro->order_count = (uint16_t)(in_order ? order_count : 0);
    macro->name_count = (uint16_t)(simple ? name_count : 0);
    pp->item_count += (in_order ? order_count : 0) + (simple ? name_count : 0);
    macro->item_count = result ? count : 0;
    macro->flags |= result ? BUSTER_PP_MACRO_COMPILED : 0;
    if (!result)
    {
        pp->item_count = macro->items_begin;
    }
    if (parameters != 0)
    {
        pp_parameter_index_end(parameters);
    }
    return result;
}

// Where tokens go: the innermost capture, or the output.
BUSTER_INLINE PPTokens* pp_target(Preprocessor* pp)
{
    return pp->capture_count != 0 ? &pp->captures : &pp->output;
}

static void pp_emit(Preprocessor* pp, PPToken token)
{
    PPTokens* target = pp_target(pp);
    if (pp_room(pp, target, 1))
    {
        pp_tokens_set(target, target->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, target, target->count, target->count + 1, token.origin);
        target->count += 1;
    }
}

// Token `token` of a prepared file read in the range at `base`, as buffers hold it.
// A token's flags with its context's pending ones added (from a `file` context, or a buffer), as clang ORs an empty
// expansion's name's flags into the next token's: a space of its own (not a name's) stays its own, unless the pending
// space is real, which clang's token then has whatever its own (BUSTER_PP_NAME_SPACE without BUSTER_PP_NAME_LINE says
// so). But a file's token that starts a line keeps its own space: clang's lexer clears a leading space at a newline.
BUSTER_INLINE uint8_t pp_with_pending(uint8_t flags, uint8_t pending, bool file)
{
    const bool line = file && (flags & BUSTER_PP_START_OF_LINE);
    const uint8_t taken = (uint8_t)(line ? pending & ~(BUSTER_PP_SPACE | BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE) : pending);
    const bool own = (flags & BUSTER_PP_SPACE) && !(flags & BUSTER_PP_NAME_SPACE);
    const bool spaced = (taken & BUSTER_PP_SPACE) && !(taken & BUSTER_PP_NAME_LINE);
    const uint8_t dropped = own ? (spaced ? BUSTER_PP_NAME_LINE : BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE) : 0;
    // An #embed's BUSTER_PP_JOIN_LINE takes the token's line start away.
    const uint8_t joined = (pending & BUSTER_PP_JOIN_LINE) ? BUSTER_PP_START_OF_LINE | BUSTER_PP_JOIN_LINE : 0;
    return (uint8_t)((flags | taken | (own && spaced ? BUSTER_PP_NAME_SPACE : 0)) & ~dropped & ~joined);
}

// BUSTER_PP_NAME_LINE for a token that takes the flags of a name (`flags`, at `location`): an expansion's first token,
// the token after an empty expansion, a builtin's or a feature query's answer. A name that took its own from another
// name passes that one's on; one that started a line (BUSTER_PP_START_OF_LINE, or BUSTER_PP_LINE_SPACE among a
// macro's arguments) gets it when only the line's start is before it, where its lexer starts it (pp_lexed_start). Only
// runs that keep expansion starts compute it: the others' tokens never have it.
// Always inlined (#880), as profile-guided builds do; hot caller: pp_invoke, once per macro invocation.
static inline __attribute__((always_inline)) uint8_t pp_name_line(const Preprocessor* pp, uint8_t flags, uint32_t location)
{
    uint8_t result = (uint8_t)(flags & BUSTER_PP_NAME_LINE);
    if (pp->options.expansion_starts && !(flags & BUSTER_PP_NAME_SPACE) && (flags & (BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE)))
    {
        const uint32_t start = pp_lexed_start(pp, location);
        const uint32_t range = pp_find_range(pp, start);
        const PPRange* record = range != BUSTER_PP_NONE ? &pp->ranges[range] : 0;
        const uint32_t offset = record != 0 ? start - record->base : 0;
        const bool line = record != 0 && record->file != BUSTER_PP_NONE && (offset == 0 || record->bytes[offset - 1] == '\n' || record->bytes[offset - 1] == '\r');
        result = line ? BUSTER_PP_NAME_LINE : 0;
    }
    return result;
}

BUSTER_INLINE PPToken pp_file_token(const PPFileData* data, uint32_t base, uint32_t token)
{
    const uint8_t kind = data->kinds[token] & 0x7F;
    PPToken result = {
        .kind = kind,
        .flags = (uint8_t)((pp_file_space(data, token) ? BUSTER_PP_SPACE : 0) | ((data->kinds[token] & BUSTER_C_TOKEN_START_OF_LINE) ? BUSTER_PP_START_OF_LINE : 0)),
        .value = kind == C_TOKEN_RAW_IDENTIFIER ? data->values[token] : base + data->values[token],
        .location = base + data->starts[token],
    };
    return result;
}

static PPToken pp_context_token(const Preprocessor* pp, const PPContext* context, uint32_t position)
{
    PPToken result;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        const PPFileData* data = &pp->datas[context->data];
        result = pp_file_token(data, pp->ranges[context->range].base, position);
    }
    else
    {
        const PPTokens* tokens = context->tokens;
        result = (PPToken){
            .kind = tokens->kinds[position],
            .flags = tokens->flags[position],
            .value = tokens->values[position],
            .location = tokens->locations[position],
            .origin = tokens->origins != 0 ? tokens->origins[position] : 0,
        };
    }
    return result;
}

static PPContext* pp_push_context(Preprocessor* pp, uint8_t kind)
{
    PPContext* result = 0;
    if (pp->context_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift))
    {
        result = &pp->contexts[pp->context_count];
        *result = (PPContext){ .kind = kind };
        pp->context_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// A buffer context over [begin, end) of `tokens`. `owned`: popping it gives the tokens back when they are on top.
static PPContext* pp_push_buffer(Preprocessor* pp, PPTokens* tokens, uint32_t begin, uint32_t end, PPMacroId macro, uint32_t expansion_end, bool barrier, bool owned)
{
    PPContext* result = pp_push_context(pp, BUSTER_PP_CONTEXT_BUFFER);
    if (result != 0)
    {
        result->tokens = tokens;
        result->position = begin;
        result->end = end;
        result->begin = owned ? begin : end;
        result->macro = macro;
        result->expansion_end = expansion_end;
        result->barrier = barrier;
        pp->macros[macro].disabled += macro != 0;
    }
    return result;
}

// A file ends: an assume_nonnull or arc_cf_code_audited region open in it is an error, and so is an unsafe_buffer_usage
// region open at the main file's end, as clang has them (pp_pragma_region). One open at the main file's end is left
// open by the run (Preprocessor.unterminated).
static void pp_end_regions(Preprocessor* pp, bool main_file)
{
    for (uint32_t region = 0; region < BUSTER_ARRAY_LENGTH(pp->region_locations); region += 1)
    {
        if (pp->region_locations[region] != 0 && (region != BUSTER_PP_REGION_UNSAFE_BUFFER || main_file))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp->region_locations[region], region == BUSTER_PP_REGION_UNSAFE_BUFFER ? "'#pragma unsafe_buffer_usage' was not ended"
                                                                                                                     : "'#pragma clang %s' was not ended within this file",
                        pp_region_names[region]);
            pp->region_locations[region] = 0;
            pp->unterminated += main_file;
        }
    }
}

// clang's error of a name that isn't Unicode's in a literal (DiagnoseInvalidUnicodeCharacterName), whose `length` bytes
// start at byte `offset` of the token at `location`, with its notes, each at its byte of the name.
static __attribute__((noinline, cold)) void pp_diagnose_named_ucn_name(Preprocessor* pp, const uint8_t* name, uint32_t length, uint32_t location, uint32_t offset)
{
    UnicodeNameDiagnosis diagnosis;
    unicode_name_diagnose(name, length, true, &diagnosis);
    PPDiagnosticSourceNote notes[BUSTER_UNICODE_NAME_NOTES];
    for (uint32_t n = 0; n < diagnosis.note_count; n += 1)
    {
        notes[n] = (PPDiagnosticSourceNote){ pp_spelling_offset_location(pp, location, offset + diagnosis.notes[n].offset), pp->diagnostic_origin, diagnosis.notes[n].message };
    }
    pp_diagnose_notes(pp, PP_SEVERITY_ERROR, pp_spelling_offset_location(pp, location, offset), notes, diagnosis.note_count, "%s", diagnosis.error);
}

// The error clang gives of a UCN's character below U+00A0, at `location`, outside a literal and before C23 in one
// (tryReadUCN's, ProcessUCNEscape's): a control character's, or a basic one's naming it.
static void pp_diagnose_ucn_basic(Preprocessor* pp, uint32_t location, uint32_t code_point)
{
    if (code_point >= 0x20 && code_point < 0x7F)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "character '%c' cannot be specified by a universal character name", (char)code_point);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "universal character name refers to a control character");
    }
}

// The letter of the universal character name whose backslash is byte `offset` of `data` (#1447): N, u or U, after any line
// splices, as the lexer reads it.
static char pp_ucn_letter(const Preprocessor* pp, const PPFileData* data, uint32_t offset)
{
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    uint64_t size = 0;
    uint64_t at = 0;
    get_char(&lexer, offset, &size, &at);
    uint64_t letter_size = 0;
    return (char)peek_char(&lexer, offset + size, &letter_size);
}

// The error of a `\u` or `\U` that starts a token (BUSTER_PP_UCN_ERROR, #1447), whose backslash is byte `offset` of `data`,
// at `location`: its value from its digits (braced or not), as clang's tryReadUCN words it there, a surrogate's
// "invalid universal character", a control character's (below U+0020, or U+007F to U+009F) that it refers to one, and
// any other's below U+00A0 that it "cannot be specified by a universal character name". Cold: such a UCN's alone.
static __attribute__((noinline, cold)) void pp_diagnose_ucn(Preprocessor* pp, const PPFileData* data, uint32_t offset, uint32_t location)
{
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    uint64_t size = 0;
    uint64_t at = 0;
    get_char(&lexer, offset, &size, &at);
    uint64_t p = offset + size;
    uint64_t step = 0;
    const int32_t letter = peek_char(&lexer, p, &step);
    p += step;
    const bool braced = peek_char(&lexer, p, &step) == '{';
    p += braced ? step : 0;
    uint32_t value = 0;
    uint32_t digits = 0;
    bool reading = true;
    while (reading && digits < (braced ? 8u : letter == 'u' ? 4u : 8u))
    {
        const int32_t c = peek_char(&lexer, p, &step);
        const bool decimal = c >= '0' && c <= '9';
        const bool hex = decimal || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f');
        value = hex ? value << 4 | (uint32_t)(decimal ? c - '0' : (c | 0x20) - 'a' + 10) : value;
        digits += hex ? 1 : 0;
        p += hex ? step : 0;
        reading = hex;
    }
    if (value >= 0xD800 && value <= 0xDFFF)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid universal character");
    }
    else if (value < 0x20 || (value >= 0x7F && value < 0xA0))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "universal character name refers to a control character");
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "character '%c' cannot be specified by a universal character name", (char)value);
    }
}

// The diagnostics of a named UCN that starts a token (BUSTER_PP_NAMED_UCN_ERROR), whose backslash is byte `offset` of
// `data`, at `base`: as clang's tryReadNamedUCN, a name that isn't exactly Unicode's is an error at the name, with a note
// where it is one loosely, whose character clang reads it as; then as tryReadUCN, that character (or the exact name's)
// below U+00A0 is an error at the backslash. Cold: a file's bad named UCNs alone.
static __attribute__((noinline, cold)) void pp_diagnose_named_ucn(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t offset)
{
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    uint8_t name[BUSTER_PP_NAMED_UCN_NAME_BYTES];
    uint64_t length = 0;
    uint64_t start = 0;
    uint64_t end = 0;
    if (read_ucn_name(&lexer, offset, name, sizeof(name), &length, &start, &end))
    {
        const uint32_t shown = length < sizeof(name) ? (uint32_t)length : (uint32_t)sizeof(name);
        uint32_t code_point = 0;
        const bool exact = length < sizeof(name) && unicode_name_strict(name, shown, &code_point);
        bool found = exact;
        if (exact)
        {
            // -pedantic's (#1447), at the backslash, before its character's error.
            pp_diagnose_group(pp, PP_WARNING_DELIMITED_ESCAPE, base + offset, "named escape sequences are a Clang extension");
        }
        if (!exact)
        {
            UnicodeNameDiagnosis diagnosis;
            unicode_name_diagnose(name, shown, false, &diagnosis);
            const uint32_t location = base + (uint32_t)start;
            if (diagnosis.note_count != 0)
            {
                const PPDiagnosticNote note = { .location = location, .message = diagnosis.notes[0].message };
                pp_diagnose_note(pp, PP_SEVERITY_ERROR, location, &note, "%s", diagnosis.error);
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "%s", diagnosis.error);
            }
            found = diagnosis.loose;
            code_point = diagnosis.loose_code_point;
        }
        if (found && code_point < 0xA0)
        {
            pp_diagnose_ucn_basic(pp, base + offset, code_point);
        }
    }
}

// Read only the exceptional errors in this consumed file-token range. Macro definitions diagnose once when read,
// never each time expanded. Excluded group bodies never call this; directive lines still do. Header queries defer
// it until their opaque spans are known.
static __attribute__((noinline, cold)) void pp_read_raw_errors_slow(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end)
{
    const PPFileData* data = &pp->datas[context->data];
    uint32_t low = 0;
    uint32_t high = data->raw_error_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = data->raw_errors[middle].token < begin;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    for (uint32_t i = low; i < data->raw_error_count && data->raw_errors[i].token < end; i += 1)
    {
        const PPRawError error = data->raw_errors[i];
        const uint32_t start = data->starts[error.token];
        bool opaque = false;
        for (const PPOpaqueSpan* span = context->opaque_spans; !opaque && span != 0; span = span->next)
        {
            opaque = start >= span->begin && start < span->end;
        }
        if (!opaque)
        {
            const uint32_t saved_position = pp->diagnostic_position;
            if (pp->diagnostic_run_first != BUSTER_PP_NONE)
            {
                pp->diagnostic_position = pp->diagnostic_run_first + error.token - begin;
            }
            // A UCN's -Wunicode warning is at its token's start, the first of any line splices before its backslash, where clang's
            // lexer starts the token (#1447).
            const bool spliced = error.kind >= BUSTER_PP_NAMED_UCN_INCOMPLETE && error.kind <= BUSTER_PP_UCN_ERROR;
            const uint32_t offset = spliced ? pp_start_with_splices(data->bytes, error.offset, (pp->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0) : error.offset;
            const uint32_t location = pp->ranges[context->range].base + offset;
            switch (error.kind)
            {
                case C_RAW_STRING_DELIMITER_LENGTH:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "raw string delimiter longer than 16 characters; use PREFIX( )PREFIX to delimit raw string");
                    break;
                case C_RAW_STRING_DELIMITER_NEWLINE:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid newline character in raw string delimiter; use PREFIX( )PREFIX to delimit raw string");
                    break;
                case C_RAW_STRING_DELIMITER_CHARACTER:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid character '%c' in raw string delimiter; use PREFIX( )PREFIX to delimit raw string", error.offset < data->length ? data->bytes[error.offset] : 0);
                    break;
                case C_RAW_STRING_UNTERMINATED:
                {
                    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
                    uint64_t position = start;
                    int32_t c = 0;
                    while (c != '"' && position < data->length)
                    {
                        uint64_t size;
                        uint64_t at;
                        c = get_char(&lexer, position, &size, &at);
                        position += size;
                    }
                    uint32_t length = 0;
                    while (length < 16 && position + length < data->length && data->bytes[position + length] != '(')
                    {
                        length += 1;
                    }
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "raw string missing terminating delimiter )%.*s\"", (int)length, data->bytes + position);
                    pp->unterminated += 1;
                    break;
                }
                case BUSTER_PP_NAMED_UCN_ERROR:
                case BUSTER_PP_NAMED_UCN_EXTENSION:
                    pp_diagnose_named_ucn(pp, data, pp->ranges[context->range].base, error.offset);
                    break;
                case BUSTER_PP_NAMED_UCN_INCOMPLETE:
                    pp_diagnose_group(pp, PP_WARNING_UNICODE, location, "incomplete universal character name; treating as '\\' followed by identifier");
                    break;
                case BUSTER_PP_NAMED_UCN_EMPTY:
                case BUSTER_PP_NAMED_UCN_UNTERMINATED:
                case BUSTER_PP_UCN_NO_DIGITS:
                {
                    // Its letter's words (#1447): N, u or U.
                    const char letter = pp_ucn_letter(pp, data, error.offset);
                    char message[96];
                    snprintf(message, sizeof(message),
                             error.kind == BUSTER_PP_NAMED_UCN_EMPTY ? "empty delimited universal character name; treating as '\\' '%c' '{' '}'" :
                             error.kind == BUSTER_PP_NAMED_UCN_UNTERMINATED ? "incomplete delimited universal character name; treating as '\\' '%c' '{' identifier" :
                                                                             "\\%c used with no following hex digits; treating as '\\' followed by identifier",
                             letter);
                    pp_diagnose_group(pp, PP_WARNING_UNICODE, location, "%s", message);
                    break;
                }
                case BUSTER_PP_UCN_ERROR:
                    pp_diagnose_ucn(pp, data, error.offset, location);
                    break;
                case C_RAW_STRING_OK:
                    break;
            }
            pp->diagnostic_position = saved_position;
        }
    }
}

BUSTER_INLINE void pp_read_raw_errors(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end)
{
    if (pp->datas[context->data].raw_error_count != 0)
    {
        pp_read_raw_errors_slow(pp, context, begin, end);
    }
}

// A file's trailing unterminated comment has been reached. Clang's raw lexer suppresses its diagnostic in a skipped
// group's body, but reads a recognized directive's own line actively. Each inclusion reads it independently.
static void pp_read_file_comment(Preprocessor* pp, PPContext* context, bool directive)
{
    const PPFileData* data = &pp->datas[context->data];
    if (data->unterminated_comment != 0 && !context->comment_read)
    {
        context->comment_read = true;
        const bool skipping = !directive && pp->conditional_count > context->conditional_base && pp->conditionals[pp->conditional_count - 1].skipping;
        if (!skipping)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp->ranges[context->range].base + data->unterminated_comment - 1, "unterminated /* comment");
            pp->unterminated += 1;
        }
    }
}

// A recognized directive reads a trailing comment on its own line actively. Conditions do this after source header
// queries have restored any opaque names, before their expression is evaluated; a body comment is left to the file.
static void pp_read_directive_comment(Preprocessor* pp, PPContext* context, uint32_t directive)
{
    const PPFileData* data = &pp->datas[context->data];
    if (data->unterminated_comment != 0 && !data->unterminated_comment_starts_line && data->directive_end[directive] == data->token_count)
    {
        pp_read_file_comment(pp, context, true);
    }
}

// The output from here on comes from range `range` at system level `level`, its tokens there from location `from` on
// (PPSystemRun). A run that starts at another level while an invocation is collecting, or an expansion read before it is
// still being read, would give their tokens a level that isn't theirs, so the runs are then inexact; so are they when the
// arena has no room for a run.
static void pp_note_system_run(Preprocessor* pp, uint32_t range, uint32_t from, uint8_t level)
{
    const uint32_t count = pp->system_run_count;
    const uint8_t previous = count != 0 ? pp->system_runs[count - 1].level : BUSTER_PP_USER_FILE;
    const bool open = pp->invocation_count != 0 || (pp->context_count != 0 && pp->contexts[pp->context_count - 1].kind != BUSTER_PP_CONTEXT_FILE);
    const bool replaces = count != 0 && pp->system_runs[count - 1].token == pp->output.count;
    if (!replaces && count == pp->system_run_capacity)
    {
        const uint32_t capacity = count != 0 ? count * 2 : 64;
        PPSystemRun* grown = arena_push(&pp->arena, (uint64_t)capacity * sizeof(PPSystemRun), _Alignof(PPSystemRun));
        if (grown != 0 && count != 0)
        {
            memcpy(grown, pp->system_runs, (uint64_t)count * sizeof(PPSystemRun));
        }
        pp->system_runs = grown != 0 ? grown : pp->system_runs;
        pp->system_run_capacity = grown != 0 ? capacity : pp->system_run_capacity;
    }
    const bool room = replaces || count < pp->system_run_capacity;
    pp->system_runs_inexact |= (open && level != previous) || !room;
    if (room)
    {
        pp->system_runs[replaces ? count - 1 : count] = (PPSystemRun){
            .token = pp->output.count,
            .range = range,
            .from = from,
            .level = level,
        };
        pp->system_run_count = replaces ? count : count + 1;
    }
}

// After a file context ended: the output comes from the innermost file context left, at its range's latest line note,
// or at its start (pp_note_system_run).
static void pp_note_file_run(Preprocessor* pp)
{
    uint32_t c = pp->context_count;
    while (c != 0 && pp->contexts[c - 1].kind != BUSTER_PP_CONTEXT_FILE)
    {
        c -= 1;
    }
    if (c != 0)
    {
        const uint32_t range = pp->contexts[c - 1].range;
        const uint32_t note = pp->ranges[range].last_line_directive;
        const bool noted = note != BUSTER_PP_NONE;
        pp_note_system_run(pp, range, noted ? pp->line_directives[note].location : pp->ranges[range].base,
                           noted ? pp->line_directives[note].system : pp->ranges[range].system);
    }
}

BUSTER_INLINE void pp_pop_context(Preprocessor* pp)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    const bool relexed = context->relexed;
    const uint32_t relex_position = context->relex_position;
    const uint32_t relex_file = context->relex_file;
    const uint32_t expansion_end = context->expansion_end;
    pp->macros[context->macro].disabled -= context->macro != 0;
    if (context->kind == BUSTER_PP_CONTEXT_BUFFER)
    {
        if (context->tokens->count == context->end && context->begin < context->end)
        {
            context->tokens->count = context->begin;
        }
    }
    else
    {
        pp_read_file_comment(pp, context, false);
        while (pp->conditional_count > context->conditional_base)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_lexed_start(pp, pp->conditionals[pp->conditional_count - 1].location), "unterminated conditional directive");
            pp->conditional_count -= 1;
            pp->unterminated += 1;
        }
        pp->include_depth -= pp->include_depth != 0;
        pp_end_regions(pp, context->file == pp->main_file);
        pp->datas[pp->files[context->file].data].guarded |= context->guard_taken;
    }
    pp->context_count -= 1;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        pp_note_file_run(pp);
    }
    if (context->kind == BUSTER_PP_CONTEXT_FILE && pp->context_count != 0 && pp->contexts[pp->context_count - 1].kind == BUSTER_PP_CONTEXT_FILE)
    {
        // Back after the #include's line.
        const PPContext* includer = &pp->contexts[pp->context_count - 1];
        pp->file_change_token = pp->output.count;
        pp->file_change_location = pp_directive_resume(pp, includer, includer->next_directive - 1);
    }
    // An expansion that ends passes its pending flags on to the context it was read from; an argument's or a line's
    // barrier doesn't. An artificial source split passes them to its successor, which can reuse this context's slot.
    const uint8_t passed = (uint8_t)((context->pending_flags | context->trailing_flags) & ~((context->trailing_flags & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE : 0));
    const bool passes_flags = context->kind == BUSTER_PP_CONTEXT_BUFFER && !context->barrier;
    if (relexed && relex_position != 0)
    {
        pp_continue_relex(pp, relex_file, relex_position, expansion_end);
    }
    if (passes_flags && passed != 0 && pp->context_count != 0)
    {
        pp->contexts[pp->context_count - 1].pending_flags |= passed;
    }
}

// The token where a file context's run ends: its next directive, or its end.
BUSTER_INLINE uint32_t pp_run_limit(const PPFileData* data, const PPContext* context)
{
    return context->next_directive < data->directive_count ? data->directive_hash[context->next_directive] : data->token_count;
}

// Stores tokens p to p + 63 of a prepared file, read in the range at `base`, at `index` of `out` as buffers hold them,
// with whole-vector stores (the lanes `valid` leaves out get junk, which the slack past a stack's capacity has room
// for): kinds without the start-of-line bit, flags from the leading-space bits and either the start-of-line bits or, for
// a macro's body (`from_macro`), BUSTER_PP_FROM_MACRO, values and locations, the values of non-identifiers and the
// locations offset by the base. `kinds` are the file's kinds p to p + 63, loaded with `valid`. Only the groups of 16
// values and locations that hold the first `stored` tokens (at most `available`) are written: a run that stops at a
// macro's name keeps only the tokens before it.
BUSTER_INLINE void pp_store_file_tokens(PPTokens* out, uint32_t index, const PPFileData* data, uint32_t base, uint32_t p, uint32_t available, uint32_t stored, __m512i kinds,
                                        bool from_macro)
{
    const __mmask64 valid = _bzhi_u64(~0ull, available);
    const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
    const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
    // The leading-space bits of tokens p to p + 63.
    const uint32_t shift = p % 64;
    uint64_t space = data->space[p / 64] >> shift;
    if (shift != 0)
    {
        // pp_prepare_data gives the bits a word past the last token's.
        space |= data->space[p / 64 + 1] << (64 - shift);
    }
    const __m512i lines = from_macro ? _mm512_set1_epi8(BUSTER_PP_FROM_MACRO) : _mm512_maskz_mov_epi8(_mm512_movepi8_mask(kinds), _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
    const __m512i flags = _mm512_or_si512(_mm512_maskz_mov_epi8(space, _mm512_set1_epi8(BUSTER_PP_SPACE)), lines);
    _mm512_storeu_si512(out->kinds + index, plain);
    _mm512_storeu_si512(out->flags + index, flags);
    const __m512i bases = _mm512_set1_epi32((int)base);
    // Only the groups of 16 that hold tokens: most runs are short.
    for (uint32_t group = 0; group * 16 < stored; group += 1)
    {
        const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
        const __mmask16 others = (__mmask16)~(identifiers >> (group * 16));
        const __m512i values = _mm512_maskz_loadu_epi32(lanes, data->values + p + group * 16);
        const __m512i starts = _mm512_maskz_loadu_epi32(lanes, data->starts + p + group * 16);
        _mm512_storeu_si512(out->values + index + group * 16, _mm512_mask_add_epi32(values, others, values, bases));
        _mm512_storeu_si512(out->locations + index + group * 16, _mm512_add_epi32(starts, bases));
    }
}

// Appends a prepared file's tokens [begin, end), read in the range at `base`, to `out`, 64 at a time
// (pp_store_file_tokens). Returns false, after a fatal error, if `out` has no room.
static bool pp_append_file_tokens(Preprocessor* pp, PPTokens* out, const PPFileData* data, uint32_t base, uint32_t begin, uint32_t end, bool from_macro)
{
    const bool result = pp_room(pp, out, end - begin);
    if (result)
    {
        uint32_t count = out->count;
        for (uint32_t p = begin; p < end; p += 64)
        {
            const uint32_t available = end - p < 64 ? end - p : 64;
            pp_store_file_tokens(out, count, data, base, p, available, available, _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, available), data->kinds + p), from_macro);
            count += available;
        }
        pp_history_fill(pp, out, out->count, count, from_macro ? pp->history_current : 0);
        out->count = count;
    }
    return result;
}

// Appends tokens [begin, end) of a stack to `out` as they are, 16 at a time with whole-vector stores. `tokens` may be
// `out` if [begin, end) is below its count. Returns false, after a fatal error, if `out` has no room.
// Always inlined (#880), as profile-guided builds do; hot caller: pp_collect_arguments, once per argument.
static inline __attribute__((always_inline)) bool pp_append_stack_tokens(Preprocessor* pp, PPTokens* out, const PPTokens* tokens, uint32_t begin, uint32_t end)
{
    const bool result = pp_room(pp, out, end - begin);
    if (result)
    {
        uint32_t count = out->count;
        for (uint32_t p = begin; p < end; p += 16)
        {
            const __mmask16 valid = (__mmask16)_bzhi_u32(~0u, end - p < 16 ? end - p : 16);
            const __m128i kinds = _mm_maskz_loadu_epi8(valid, tokens->kinds + p);
            const __m128i flags = _mm_maskz_loadu_epi8(valid, tokens->flags + p);
            const __m512i values = _mm512_maskz_loadu_epi32(valid, tokens->values + p);
            const __m512i locations = _mm512_maskz_loadu_epi32(valid, tokens->locations + p);
            _mm_storeu_si128((__m128i*)(out->kinds + count), kinds);
            _mm_storeu_si128((__m128i*)(out->flags + count), flags);
            _mm512_storeu_si512(out->values + count, values);
            _mm512_storeu_si512(out->locations + count, locations);
            count += (uint32_t)__builtin_popcount(valid);
        }
        pp_history_copy(pp, out, out->count, tokens, begin, end);
        out->count = count;
    }
    return result;
}

static uint32_t pp_copy_file_run_vector(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit)
{
#if !BUSTER_HOST_VECTOR
    return pp_copy_file_run_scalar(pp, data, base, position, limit);
#else
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < limit && pp_room(pp, target, BUSTER_PP_TOKEN_SLACK))
    {
        const uint32_t available = limit - p < 64 ? limit - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, data->kinds + p);
        const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        uint32_t take = available;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            if (pp_is_macro_vector(pp, data->values[p + lane]))
            {
                take = lane;
                stopped = true;
                break;
            }
        }
        if (take != 0)
        {
            pp_store_file_tokens(target, target->count, data, base, p, available, take, kinds, false);
        }
        target->count += take;
        p += take;
    }
    return p;
#endif
}

// pp_copy_file_run_vector for a file's text read straight into the output (pp_step_file's `text`), which doesn't stop at
// the name of an object-like macro whose body is one token that names no macro: it writes the expansion in the name's
// place, as pp_expand_file_direct would after the run stopped there (the expansion's record, its token with the name's
// flags, the output's history, the statistics), and goes on. Most expansions in a file's text are so (sqlite3.c's
// 12,431 of 13,197), and each one stopped a run, and started another unless it was the last token before `limit`.
// Returns where it stopped (at a name it doesn't expand, or at `limit`), in `*directs` the expansions it wrote, and in
// `*runs` the runs the old path would have started after them.
static uint32_t pp_copy_file_text_vector(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit, uint32_t* directs,
                                         uint32_t* runs)
{
    PPTokens* out = &pp->output;
    uint32_t p = position;
    uint32_t written = 0;
    bool last_written = false;
    bool stopped = false;
    while (!stopped && p < limit && pp_room(pp, out, BUSTER_PP_TOKEN_SLACK))
    {
        const uint32_t available = limit - p < 64 ? limit - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, data->kinds + p);
        const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        uint32_t take = available;
        uint64_t replaced = 0;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            const IdentId value = data->values[p + lane];
            if (pp_is_macro_vector(pp, value))
            {
                const PPMacro* macro = &pp->macros[pp->macro_of[value]];
                const PPFileData* body = &pp->datas[macro->data];
                const uint32_t b = macro->body_begin;
                const bool direct = !pp->options.preprocessed && (macro->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN | BUSTER_PP_MACRO_PASTES)) == 0 && macro->disabled == 0 &&
                                    macro->body_end - b == 1 && ((body->kinds[b] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro_vector(pp, body->values[b]));
                if (!direct)
                {
                    take = lane;
                    stopped = true;
                    break;
                }
                replaced |= 1ull << lane;
            }
        }
        if (take != 0)
        {
            pp_store_file_tokens(out, out->count, data, base, p, available, take, kinds, false);
        }
        // Each name replaced, in order, by its body's token, which takes the name's flags as stored (its leading space
        // and line start) in place of its own space.
        for (uint64_t bits = replaced; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            const uint32_t at = out->count + lane;
            const PPMacroId id = pp->macro_of[data->values[p + lane]];
            const PPMacro* macro = &pp->macros[id];
            const PPFileData* body = &pp->datas[macro->data];
            const uint32_t b = macro->body_begin;
            const uint32_t expansion = pp_history_expansion(pp, id, base + data->starts[p + lane], 0);
            const uint8_t kind = body->kinds[b] & 0x7F;
            out->kinds[at] = kind;
            out->flags[at] = (uint8_t)((out->flags[at] & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)) | BUSTER_PP_FROM_MACRO | BUSTER_PP_NAME_SPACE);
            out->values[at] = kind == C_TOKEN_RAW_IDENTIFIER ? body->values[b] : macro->base + body->values[b];
            out->locations[at] = macro->base + body->starts[b];
            if (!pp->options.no_history && !pp->fatal)
            {
                pp_history_change(pp, at, expansion);
                pp_history_change(pp, at + 1, 0);
            }
        }
        const uint32_t count = (uint32_t)__builtin_popcountll(replaced);
        if (pp->options.timing)
        {
            pp->statistics.direct_expansions += count;
            pp->statistics.final_direct_object_tokens += count;
        }
        written += count;
        last_written = take != 0 && ((replaced >> (take - 1)) & 1) != 0;
        out->count += take;
        p += take;
    }
    *directs = written;
    *runs = written - (last_written && p == limit);
    return p;
}

// The same, a token at a time, testing the macro table without the vector path's cached name bits.
static uint32_t pp_copy_file_run_scalar(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t position, uint32_t limit)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < limit && pp_room(pp, target, 1))
    {
        const PPToken token = pp_file_token(data, base, p);
        stopped = token.kind == C_TOKEN_RAW_IDENTIFIER && pp->macro_of[token.value] != 0;
        if (!stopped)
        {
            pp_tokens_set(target, target->count, token.kind, token.flags, token.value, token.location);
            target->count += 1;
            p += 1;
        }
    }
    return p;
}

static uint32_t pp_copy_buffer_run_vector(Preprocessor* pp, const PPTokens* tokens, uint32_t position, uint32_t end)
{
#if !BUSTER_HOST_VECTOR
    return pp_copy_buffer_run_scalar(pp, tokens, position, end);
#else
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < end && pp_room(pp, target, BUSTER_PP_TOKEN_SLACK))
    {
        const uint32_t available = end - p < 64 ? end - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, tokens->kinds + p);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, tokens->flags + p);
        const uint64_t unpainted = _mm512_mask_testn_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(unpainted, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        uint32_t take = available;
        uint64_t paint = 0;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            const IdentId id = tokens->values[p + lane];
            if (pp_is_macro_vector(pp, id))
            {
                if (pp->macros[pp->macro_of[id]].disabled != 0)
                {
                    paint |= 1ull << lane;
                }
                else
                {
                    take = lane;
                    stopped = true;
                    break;
                }
            }
        }
        const uint32_t out = target->count;
        _mm512_storeu_si512(target->kinds + out, kinds);
        _mm512_storeu_si512(target->flags + out, _mm512_mask_mov_epi8(flags, paint, _mm512_or_si512(flags, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND))));
        // Only the groups of 16 that hold the tokens taken.
        for (uint32_t group = 0; group * 16 < take; group += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
            _mm512_storeu_si512(target->values + out + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->values + p + group * 16));
            _mm512_storeu_si512(target->locations + out + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->locations + p + group * 16));
        }
        target->count += take;
        p += take;
    }
    return p;
#endif
}

static uint32_t pp_copy_buffer_run_scalar(Preprocessor* pp, const PPTokens* tokens, uint32_t position, uint32_t end)
{
    PPTokens* target = pp_target(pp);
    uint32_t p = position;
    bool stopped = false;
    while (!stopped && p < end && pp_room(pp, target, 1))
    {
        uint8_t flags = tokens->flags[p];
        const bool candidate = tokens->kinds[p] == C_TOKEN_RAW_IDENTIFIER && !(flags & BUSTER_PP_NO_EXPAND) && pp->macro_of[tokens->values[p]] != 0;
        if (candidate && pp->macros[pp->macro_of[tokens->values[p]]].disabled == 0)
        {
            stopped = true;
        }
        else
        {
            flags |= candidate ? BUSTER_PP_NO_EXPAND : 0;
            pp_tokens_set(target, target->count, tokens->kinds[p], flags, tokens->values[p], tokens->locations[p]);
            target->count += 1;
            p += 1;
        }
    }
    return p;
}

// The end of a new expansion's range, for __LINE__: the invocation's last token's location, or if a macro made that
// token, the end of the expansion it came from.
BUSTER_INLINE uint32_t pp_expansion_end(const PPContext* context, PPToken last)
{
    return (last.flags & BUSTER_PP_FROM_MACRO) && context->kind == BUSTER_PP_CONTEXT_BUFFER ? context->expansion_end : last.location;
}

// Is the token after the top context's current one '('? Looks into the contexts below an exhausted buffer, but not
// past a barrier, a directive or the end of a file.
static bool pp_next_is_lparen(const Preprocessor* pp)
{
    bool result = false;
    bool done = false;
    for (uint32_t c = pp->context_count; !done && c > 0; c -= 1)
    {
        const PPContext* context = &pp->contexts[c - 1];
        const uint32_t position = context->position + (c == pp->context_count);
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            const PPFileData* data = &pp->datas[context->data];
            result = position < pp_run_limit(data, context) && (data->kinds[position] & 0x7F) == C_TOKEN_L_PAREN;
            done = true;
        }
        else if (position < context->end)
        {
            result = context->tokens->kinds[position] == C_TOKEN_L_PAREN;
            done = true;
        }
        else
        {
            done = context->barrier;
        }
    }
    return result;
}

// Reads the next token from the top context on, popping the buffers it exhausts (which enables their macros again).
// Returns false at the end of a barrier, at a directive and at the end of a file. Its callers read the '(' that
// pp_next_is_lparen saw, and `defined`'s operand in a #if line, neither of which can pass a directive: anything that
// can, the main loop collects (pp_collect_step), so that a directive among an invocation's arguments runs there.
// `expansion_end` gets the token's expansion end (pp_expansion_end).
static bool pp_read_token(Preprocessor* pp, PPToken* token, uint32_t* expansion_end)
{
    bool result = false;
    bool done = false;
    while (!done && !pp->fatal)
    {
        PPContext* context = &pp->contexts[pp->context_count - 1];
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            const PPFileData* data = &pp->datas[context->data];
            if (context->position < pp_run_limit(data, context))
            {
                pp_read_raw_errors(pp, context, context->position, context->position + 1);
                *token = pp_file_token(data, pp->ranges[context->range].base, context->position);
                *expansion_end = token->location;
                context->position += 1;
                result = true;
            }
            done = true;
        }
        else if (context->position < context->end)
        {
            *token = pp_context_token(pp, context, context->position);
            *expansion_end = pp_expansion_end(context, *token);
            context->position += 1;
            result = true;
            done = true;
        }
        else if (context->barrier)
        {
            done = true;
        }
        else
        {
            pp_pop_context(pp);
        }
    }
    return result;
}

static bool pp_push_token(Preprocessor* pp, PPTokens* tokens, PPToken token)
{
    const bool result = pp_room(pp, tokens, 1);
    if (result)
    {
        pp_tokens_set(tokens, tokens->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, tokens, tokens->count, tokens->count + 1, token.origin != 0 ? token.origin : pp->history_current);
        tokens->count += 1;
    }
    return result;
}

BUSTER_INLINE PPToken pp_stack_token(const PPTokens* tokens, uint32_t index)
{
    return (PPToken){
        .kind = tokens->kinds[index],
        .flags = tokens->flags[index],
        .value = tokens->values[index],
        .location = tokens->locations[index],
        .origin = tokens->origins != 0 ? tokens->origins[index] : 0,
    };
}

// A string or character literal: stringizing escapes its quotes and backslashes.
BUSTER_INLINE bool pp_is_literal(uint8_t kind)
{
    return kind >= C_TOKEN_CHAR_CONSTANT && kind <= C_TOKEN_UTF32_STRING_LITERAL;
}

// Lexes `length` bytes of scratch (at `location`) and appends their tokens to `into`, as buffers hold them (identifiers
// interned), but for an unterminated block comment at their end, which sets *comment. Returns the number of tokens, or
// BUSTER_PP_NONE if they don't fit.
static uint32_t pp_lex_scratch(Preprocessor* pp, uint32_t location, uint32_t length, PPTokens* into, uint8_t first_flags, bool* comment)
{
    const uint8_t* bytes = pp_location_bytes(pp, location);
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
    uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
    uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    const PPPrepare mode = pp_prepare_config(pp);
    int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(bytes, length, kinds, starts, ends, pp_lex_flags(&mode)) : -1;
    count = count > 0 ? pp_lex_for_mode(&mode, bytes, length, kinds, starts, ends, (uint32_t)count, false) : count;
    *comment = count > 0 && pp_is_unterminated_comment(bytes, kinds[count - 1], starts[count - 1]);
    count -= *comment;
    uint32_t result = count >= 0 && pp_room(pp, into, (uint32_t)count) ? (uint32_t)count : BUSTER_PP_NONE;
    for (uint32_t i = 0; result != BUSTER_PP_NONE && i < (uint32_t)count; i += 1)
    {
        const uint8_t kind = kinds[i] & 0x7F;
        uint32_t value = location + ends[i];
        if (kind == C_TOKEN_RAW_IDENTIFIER)
        {
            value = refine_intern_identifier(pp->table, bytes, length, starts[i], ends[i] - starts[i], pp->refine_flags);
        }
        const bool space = i == 0 ? (first_flags & BUSTER_PP_SPACE) != 0 : starts[i] != ends[i - 1];
        const PPToken token = { .kind = kind, .flags = (uint8_t)((space ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO), .value = value, .location = location + starts[i] };
        pp_tokens_set(into, into->count, token.kind, token.flags, token.value, token.location);
        pp_history_fill(pp, into, into->count, into->count + 1, pp->history_current);
        into->count += 1;
    }
    scratch_end(scratch);
    return result;
}

// Whether `length` bytes have one past 0x7F: 64 at a time in masked loads, or for the scalar kernels one at a time.
BUSTER_INLINE bool pp_has_non_ascii(const uint8_t* bytes, uint32_t length, bool vectorized)
{
    (void)vectorized;

    bool result = false;
    for (uint32_t k = 0; BUSTER_VECTOR_PATH(vectorized) && !result && k < length; k += 64)
    {
        const uint32_t available = length - k < 64 ? length - k : 64;
        result = _mm512_movepi8_mask(_mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, available), bytes + k)) != 0;
    }
    for (uint32_t k = 0; !BUSTER_VECTOR_PATH(vectorized) && k < length; k += 1)
    {
        result |= bytes[k] >= 0x80;
    }
    return result;
}

// An identifier's spelling as it was written, for pp_paste_spelling: in `arena`, which gets up to ten bytes per byte
// of its interned spelling's `length` (\U00000024 is `$`). Returns its length, or `length` (and leaves *spelling) if it
// has none.
static uint32_t pp_paste_written(Preprocessor* pp, uint32_t location, uint32_t length, Arena* arena, const uint8_t** spelling)
{
    uint8_t* written = arena_push(arena, 10 * (uint64_t)length + 24, 1);
    const uint8_t* as_written = 0;
    const uint32_t written_length = written != 0 ? pp_identifier_as_written(pp, location, written, &as_written) : 0;
    *spelling = written_length != 0 ? as_written : *spelling;
    return written_length != 0 ? written_length : length;
}

// A token's spelling for a paste: an identifier's as it was written when its name has a byte past 0x7F (so with its
// UCNs, as clang pastes it: pp_paste_written), and any other's from pp_spelling, which `buffer` has room for.
BUSTER_INLINE uint32_t pp_paste_spelling(Preprocessor* pp, PPToken token, uint8_t* buffer, Arena* arena, const uint8_t** spelling)
{
    const uint32_t length = pp_spelling(pp, token.kind, token.value, token.location, buffer, spelling);
    const bool non_ascii = token.kind == C_TOKEN_RAW_IDENTIFIER && pp_has_non_ascii(*spelling, length, BUSTER_VECTOR_PATH(pp->vectorized));
    return non_ascii ? pp_paste_written(pp, token.location, length, arena, spelling) : length;
}

// Pastes two tokens (C 6.10.5.3): their spellings joined (pp_paste_spelling) must lex as exactly one token, which takes
// the left one's leading space and the line it started among a macro's arguments (BUSTER_PP_LINE_SPACE), as clang's
// TokenLexer::pasteTokens gives it the left one's flags. Returns false with an error if they don't, as clang does, and
// the caller keeps them both.
BUSTER_INLINE bool pp_paste(Preprocessor* pp, PPToken left, PPToken right, PPToken* result)
{
    Scratch scratch = scratch_begin(0);
    uint8_t left_buffer[24];
    uint8_t right_buffer[24];
    const uint8_t* left_spelling;
    const uint8_t* right_spelling;
    const bool left_name = left.kind == C_TOKEN_RAW_IDENTIFIER;
    const bool right_name = right.kind == C_TOKEN_RAW_IDENTIFIER;
    uint8_t* left_space = left_name || left.kind == BUSTER_PP_KIND_INTEGER ? left_buffer : arena_push(scratch.arena, left.value - left.location + 1, 1);
    uint8_t* right_space = right_name || right.kind == BUSTER_PP_KIND_INTEGER ? right_buffer : arena_push(scratch.arena, right.value - right.location + 1, 1);
    const uint32_t left_length = pp_paste_spelling(pp, left, left_space, scratch.arena, &left_spelling);
    const uint32_t right_length = pp_paste_spelling(pp, right, right_space, scratch.arena, &right_spelling);
    uint8_t* joined = arena_push(scratch.arena, (uint64_t)left_length + right_length + 1, 1);
    bool valid = false;
    if (joined != 0)
    {
        memcpy(joined, left_spelling, left_length);
        memcpy(joined + left_length, right_spelling, right_length);
        const uint32_t length = left_length + right_length;
        pp->statistics.pastes += pp->options.timing;
        // The commonest paste, an identifier and an identifier or a number (`prefix_ ## name`, `x ## 1`), makes an
        // identifier whenever both are ASCII identifier characters, whose interned spelling is the joined bytes: no
        // need to lex them. The scalar kernels lex every paste.
        bool identifier = BUSTER_VECTOR_PATH(pp->vectorized) && left.kind == C_TOKEN_RAW_IDENTIFIER &&
                          (right.kind == C_TOKEN_RAW_IDENTIFIER || right.kind == C_TOKEN_NUMERIC_CONSTANT || right.kind == BUSTER_PP_KIND_INTEGER);
        for (uint32_t k = 0; identifier && k < length; k += 64)
        {
            // Letters (either case, with 0x20 set), digits, '_' and '$', 64 bytes at a time.
            const uint32_t available = length - k < 64 ? length - k : 64;
            const __mmask64 valid = _bzhi_u64(~0ull, available);
            const __m512i c = _mm512_maskz_loadu_epi8(valid, joined + k);
            const __mmask64 letters = _mm512_cmplt_epu8_mask(_mm512_sub_epi8(_mm512_or_si512(c, _mm512_set1_epi8(0x20)), _mm512_set1_epi8('a')), _mm512_set1_epi8(26));
            const __mmask64 digits = _mm512_cmplt_epu8_mask(_mm512_sub_epi8(c, _mm512_set1_epi8('0')), _mm512_set1_epi8(10));
            const __mmask64 others = _mm512_cmpeq_epi8_mask(c, _mm512_set1_epi8('_')) | _mm512_cmpeq_epi8_mask(c, _mm512_set1_epi8('$'));
            identifier = ((letters | digits | others) & valid) == valid;
        }
        uint8_t kind = C_TOKEN_RAW_IDENTIFIER;
        if (identifier)
        {
            valid = true;
        }
        else
        {
            const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
            uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
            uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
            uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
            // Where '$' is punctuation (pp_dollar_punctuation), `$ ## x` gives two tokens.
            const uint32_t flags = pp->refine_flags | (pp_dollar_punctuation(&pp->options) ? C_TOKENIZER_NO_DOLLARS : 0);
            const int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(joined, length, kinds, starts, ends, flags) : -1;
            // A prefixed literal the mode doesn't have lexes as two tokens (pp_split_kinds).
            valid = count == 1 && starts[0] == 0 && ends[0] == length && !pp_splits_kind(pp->split_kinds, kinds[0]) &&
                    !pp_splits_unknown(joined, starts[0], ends[0], pp->split_kinds) && !pp_is_unterminated_comment(joined, kinds[0], 0);
            kind = valid ? kinds[0] & 0x7F : kind;
        }
        if (valid)
        {
            const uint32_t location = pp_scratch(pp, joined, length);
            uint32_t value = location + length;
            if (identifier)
            {
                value = intern_find_or_add(pp->table, joined, length);
            }
            else if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                value = refine_intern_identifier(pp->table, joined, length, 0, length, pp->refine_flags);
            }
            *result = (PPToken){ .kind = kind, .flags = (uint8_t)((left.flags & (BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE)) | BUSTER_PP_FROM_MACRO), .value = value, .location = location };
            // A poisoned name a paste makes is an error (pp_is_poisoned).
            if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                pp_check_poison(pp, value, left.location);
            }
            const PPSource sources[] = {
                { .location = left.location, .origin = left.origin },
                { .location = right.location, .origin = right.origin },
            };
            result->origin = pp_history_generated(pp, PP_GENERATED_PASTE, location, pp->history_spelling, sources, 2);
        }
        else if (!pp->options.assembly)
        {
            // Assembler-with-cpp has no invalid paste: as in GCC and Clang, the two tokens stay, so `% ## eax` is
            // `%eax`, and in `$ ## X` the X is rescanned, which expands a macro X.
            const uint32_t saved = pp->diagnostic_origin;
            pp->diagnostic_origin = pp->history_current;
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp->history_anchor, "pasting formed '%.*s', an invalid preprocessing token", (int)length, (const char*)joined);
            pp->diagnostic_origin = saved;
        }
    }
    scratch_end(scratch);
    return valid;
}

// Stringizes tokens [begin, end) of a stack (C 6.10.4.2): their spellings with one space wherever whitespace separated
// them, quotes and backslashes in literals escaped, in a new string literal.
static PPToken pp_stringize(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t flags, uint32_t anchor)
{
    // An identifier written with UCNs may be up to 10 times as long as its interned spelling (\U00000024 is `$`).
    uint64_t room = 3;
    for (uint32_t i = begin; i < end; i += 1)
    {
        room += tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER ? 10 * (uint64_t)intern_length(pp->table, tokens->values[i]) + 24 : 2 * (uint64_t)(tokens->values[i] - tokens->locations[i]) + 24;
    }
    Scratch scratch = scratch_begin(0);
    uint8_t* text = arena_push(scratch.arena, room, 1);
    uint8_t* spelling_buffer = arena_push(scratch.arena, room, 1);
    PPToken result = { .kind = C_TOKEN_STRING_LITERAL, .flags = (uint8_t)((flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO) };
    if (text != 0 && spelling_buffer != 0)
    {
        uint64_t length = 0;
        text[length] = '"';
        length += 1;
        bool first = true;
        for (uint32_t i = begin; i < end; i += 1)
        {
            const uint8_t kind = tokens->kinds[i];
            if (kind != BUSTER_PP_KIND_PLACEMARKER)
            {
                if (!first && (tokens->flags[i] & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE)))
                {
                    text[length] = ' ';
                    length += 1;
                }
                const uint8_t* spelling;
                uint32_t spelling_length = pp_spelling(pp, kind, tokens->values[i], tokens->locations[i], spelling_buffer, &spelling);
                // Only an identifier whose interned spelling has a byte past 0x7F can have been written with a UCN.
                bool non_ascii = false;
                for (uint32_t k = 0; kind == C_TOKEN_RAW_IDENTIFIER && k < spelling_length; k += 1)
                {
                    non_ascii |= spelling[k] >= 0x80;
                }
                if (non_ascii)
                {
                    const uint8_t* written;
                    const uint32_t written_length = pp_identifier_as_written(pp, tokens->locations[i], spelling_buffer, &written);
                    spelling = written_length != 0 ? written : spelling;
                    spelling_length = written_length != 0 ? written_length : spelling_length;
                }
                const bool escape = pp_is_literal(kind);
                for (uint32_t k = 0; k < spelling_length; k += 1)
                {
                    const uint8_t c = spelling[k];
                    // A raw string literal's line break is \n, as clang's Stringify writes one (\r\n and \n\r too).
                    const bool line_break = escape && (c == '\n' || c == '\r');
                    if (escape && (c == '"' || c == '\\' || line_break))
                    {
                        text[length] = '\\';
                        length += 1;
                    }
                    text[length] = line_break ? 'n' : c;
                    length += 1;
                    k += line_break && k + 1 < spelling_length && (spelling[k + 1] == '\n' || spelling[k + 1] == '\r') && spelling[k + 1] != c;
                }
                first = false;
            }
        }
        // A string can't end in an odd number of backslashes (C 6.10.3.2p2 makes it undefined): clang drops the last
        // one and warns, as for `#define F(x) #x` and `F(\)`.
        uint64_t backslashes = 0;
        while (backslashes + 1 < length && text[length - 1 - backslashes] == '\\')
        {
            backslashes += 1;
        }
        if ((backslashes & 1) != 0)
        {
            length -= 1;
            pp_diagnose_group(pp, PP_WARNING_PLAIN, end != begin ? tokens->locations[end - 1] : 0, "invalid string literal, ignoring final '\\'");
        }
        text[length] = '"';
        length += 1;
        result.location = pp_scratch_clean(pp, text, (uint32_t)length);
        result.value = result.location + (uint32_t)length;
        if (!pp->options.no_history)
        {
            PPSource* sources = arena_push_array(scratch.arena, (uint64_t)(end - begin), sizeof(PPSource), 8);
            if (sources != 0 || begin == end)
            {
                for (uint32_t i = begin; i < end; i += 1)
                {
                    sources[i - begin] = (PPSource){ .location = tokens->locations[i], .origin = tokens->origins[i] };
                }
                result.origin = pp_history_generated(pp, PP_GENERATED_STRING, result.location, anchor, sources, end - begin);
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory retaining stringification inputs");
            }
        }
    }
    scratch_end(scratch);
    return result;
}

// The tokens a parameter stands for in an invocation: its expanded argument, or its raw one (next to ##).
static void pp_argument_range(Preprocessor* pp, const PPInvocation* invocation, uint32_t parameter, bool expanded, PPTokens** tokens, uint32_t* begin, uint32_t* end)
{
    const PPArgument* argument = &pp->argument_records[invocation->arguments + parameter];
    if (expanded && argument->expanded_captured)
    {
        *tokens = &pp->captures;
        *begin = argument->expanded_begin;
        *end = argument->expanded_end;
    }
    else
    {
        *tokens = &pp->arguments;
        *begin = expanded ? argument->expanded_begin : argument->raw_begin;
        *end = expanded ? argument->expanded_end : argument->raw_end;
    }
}

// Whether argument `parameter` of an invocation has no tokens as it was written.
BUSTER_INLINE bool pp_argument_empty(const Preprocessor* pp, const PPInvocation* invocation, uint32_t parameter)
{
    const PPArgument* argument = &pp->argument_records[invocation->arguments + parameter];
    return argument->raw_begin == argument->raw_end;
}

// A body token of a macro, as buffers hold it.
BUSTER_INLINE PPToken pp_body_token(const Preprocessor* pp, const PPMacro* macro, const PPItem* item)
{
    PPToken result = pp_file_token(&pp->datas[macro->data], macro->base, item->token);
    result.flags = (uint8_t)((item->flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
    result.origin = pp->history_current;
    return result;
}

// Appends tokens [begin, end) of a stack to the expansions, all of them from a macro and none starting a line (one that
// did gets a leading space instead). With `replace_first`, the first takes `first_flags`' leading space: an argument's
// first token takes its parameter's.
static void pp_append_tokens(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, bool replace_first, uint8_t first_flags)
{
    PPTokens* out = &pp->expansions;
    if (pp_room(pp, out, end - begin))
    {
        // The columns in locals: a store through a uint8_t pointer may change any field, which the loop would reload.
        const uint8_t* kinds = tokens->kinds;
        const uint8_t* flags = tokens->flags;
        const uint32_t* values = tokens->values;
        const uint32_t* locations = tokens->locations;
        uint8_t* out_kinds = out->kinds;
        uint8_t* out_flags = out->flags;
        uint32_t* out_values = out->values;
        uint32_t* out_locations = out->locations;
        uint32_t count = out->count;
        for (uint32_t i = begin; i < end; i += 1)
        {
            uint8_t token_flags = flags[i];
            token_flags = (uint8_t)((token_flags & ~BUSTER_PP_START_OF_LINE) | ((token_flags & BUSTER_PP_START_OF_LINE) ? BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE : 0) | BUSTER_PP_FROM_MACRO);
            if (i == begin && replace_first)
            {
                token_flags = (uint8_t)((token_flags & ~(BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE)) | (first_flags & BUSTER_PP_SPACE));
            }
            out_kinds[count] = kinds[i];
            out_flags[count] = token_flags;
            out_values[count] = values[i];
            out_locations[count] = locations[i];
            count += 1;
        }
        pp_history_argument(pp, tokens, begin, end, out->count, pp->history_anchor);
        out->count = count;
    }
}

// Pastes the token at `at` in `out` onto the one before it, as ## does, when ## is on the left of a __VA_OPT__ and `at`
// is the first token of its result: a placemarker on either side gives the other (the right one with its own leading
// space, which clang gave it, since it skipped the ##), and an invalid paste keeps both (after an error). The tokens
// after `at` move down when the two become one.
static void pp_paste_in_place(Preprocessor* pp, PPTokens* out, uint32_t at)
{
    const PPToken left = pp_stack_token(out, at - 1);
    const PPToken right = pp_stack_token(out, at);
    PPToken result = right;
    bool joined = true;
    if (left.kind == BUSTER_PP_KIND_PLACEMARKER)
    {
        result.flags = (uint8_t)((right.flags & ~BUSTER_PP_START_OF_LINE) | BUSTER_PP_FROM_MACRO);
    }
    else if (right.kind == BUSTER_PP_KIND_PLACEMARKER)
    {
        result = left;
    }
    else
    {
        joined = pp_paste(pp, left, right, &result);
    }
    if (joined)
    {
        pp_tokens_set(out, at - 1, result.kind, result.flags, result.value, result.location);
        pp_history_fill(pp, out, at - 1, at, result.origin);
        for (uint32_t i = at + 1; i < out->count; i += 1)
        {
            pp_tokens_set(out, i - 1, out->kinds[i], out->flags[i], out->values[i], out->locations[i]);
            if (out->origins != 0)
            {
                out->origins[i - 1] = out->origins[i];
            }
        }
        out->count -= 1;
    }
    else
    {
        out->flags[at] = (uint8_t)((right.flags & ~BUSTER_PP_START_OF_LINE) | BUSTER_PP_FROM_MACRO);
    }
}

// Sets the leading space of `out`'s token `at`.
BUSTER_INLINE void pp_set_space(PPTokens* out, uint32_t at, bool space)
{
    out->flags[at] = (uint8_t)((out->flags[at] & ~BUSTER_PP_SPACE) | (space ? BUSTER_PP_SPACE : 0));
}

// Substitutes an invocation's arguments into its macro's compiled body (C 6.10.4), appending the result to the
// expansions, where it starts at `*begin`: parameters by their expanded arguments (raw next to ##), # by a string
// literal, ## by pasting (with placemarkers for empty arguments), __VA_OPT__ by its content when the expanded variadic
// argument has tokens, and GNU's `, ## __VA_ARGS__` by the comma and the arguments, or nothing when the variadic argument
// was omitted. Placemarkers are then removed.
//
// A __VA_OPT__ next to ## is a placemarker when the variadic argument is absent or its content gives no tokens (C
// 6.10.5.2), and otherwise ## pastes the content's first token (## on the left) or last token (## on the right). An
// empty argument at either end of the content is a placemarker for that end's ##, as in clang: `__VA_OPT__(a x) ## y`
// gives `a y` for an empty x.
//
// Leading spaces are clang's (TokenLexer::ExpandFunctionArguments): `next_space` is its NextTokGetsSpace, which each
// token of the replacement list it reads sets when it has a leading space, but the first token and one after ##
// (BUSTER_PP_ITEM_SPACE and the other item bits, from pp_compile_macro), and which the next token it gives takes: a
// body token, an argument's first token, a string. A placemarker takes none (clang has no token there, and skips the
// ## next to it), a ## between two tokens takes it, and a paste's result has its left operand's. A space left over after
// the last token goes to `*trailing`, which the token after the expansion takes (TokenLexer::Lex).
static void pp_substitute(Preprocessor* pp, const PPInvocation* invocation, uint32_t* begin, uint8_t* trailing)
{
    const uint32_t saved_history = pp->history_current;
    pp->history_current = invocation->history;
    const PPMacro* macro = &pp->macros[invocation->macro];
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = macro->item_count;
    PPTokens* out = &pp->expansions;
    *begin = out->count;
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    const uint32_t variadic_index = macro->parameter_count - 1u;
    bool variadic_present = false;
    if (variadic && invocation->argument_count != 0)
    {
        PPTokens* tokens;
        uint32_t first;
        uint32_t last;
        pp_argument_range(pp, invocation, variadic_index, true, &tokens, &first, &last);
        variadic_present = first != last;
    }
    uint32_t va_opt_end = BUSTER_PP_NONE; // The item where the open __VA_OPT__'s content ends,
    uint32_t va_opt_first = BUSTER_PP_NONE; // the content's first item,
    uint32_t va_opt_begin = 0; // where its tokens start in `out`,
    uint32_t va_opt_mark = BUSTER_PP_NONE; // and for # __VA_OPT__, where its tokens start
    uint8_t va_opt_flags = 0; // The open __VA_OPT__'s item flags.
    bool va_opt_string_space = false; // # __VA_OPT__'s string's leading space.
    // Where the expansion history places # __VA_OPT__'s string, and the ## before a __VA_OPT__ (pp->history_anchor
    // and history_spelling when it was read).
    uint32_t va_opt_string_location = 0;
    uint32_t va_opt_paste_location = 0;
    uint32_t va_opt_paste_spelling = 0;
    bool va_opt_pasted_after = false; // Whether ## follows the open __VA_OPT__.
    bool va_opt_paste_left = false; // Whether ## precedes the next __VA_OPT__ (set by the ##).
    bool va_opt_merged = false; // Whether the open __VA_OPT__'s first token was pasted onto the ##'s left operand.
    bool va_opt_pseudo_paste = false; // Whether its content starts with GNU's comma's __VA_ARGS__ (set by the ##).
    uint32_t va_opt_pseudo_lead = BUSTER_PP_NONE; // That __VA_ARGS__'s item, after the empty arguments pasted onto it.
    bool next_space = false;
    bool literal_paste = false; // Whether the last token is a ## that stays a token, after which a body token keeps its own space.
    bool placemarkers = false;
    for (uint32_t i = 0; i <= count && !pp->fatal; i += 1)
    {
        if (i == va_opt_end)
        {
            // The content's ')' is read.
            next_space |= (va_opt_flags & BUSTER_PP_ITEM_CLOSE_SPACE) != 0;
            if (va_opt_mark != BUSTER_PP_NONE)
            {
                // # __VA_OPT__: the content becomes a string, with the space pending at the # (which stays pending).
                PPToken string = pp_stringize(pp, out, va_opt_mark, out->count, va_opt_string_space ? BUSTER_PP_SPACE : 0, va_opt_string_location);
                out->count = va_opt_mark;
                pp_push_token(pp, out, string);
                va_opt_mark = BUSTER_PP_NONE;
            }
            // Content of placemarkers alone gives no tokens (but one whose first token the ## on its left took has one).
            bool only_placemarkers = !va_opt_merged;
            for (uint32_t k = va_opt_begin; only_placemarkers && k < out->count; k += 1)
            {
                only_placemarkers = out->kinds[k] == BUSTER_PP_KIND_PLACEMARKER;
            }
            out->count = only_placemarkers ? va_opt_begin : out->count;
            if (out->count == va_opt_begin && !va_opt_merged)
            {
                // A placemarker, which ## on the right takes as its left operand and ## on the left leaves alone.
                if (va_opt_pasted_after && !va_opt_paste_left)
                {
                    pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER });
                    placemarkers = true;
                }
            }
            else if (va_opt_paste_left)
            {
                // # __VA_OPT__'s string, or a placemarker that stayed first.
                pp->history_anchor = va_opt_paste_location;
                pp->history_spelling = va_opt_paste_spelling;
                pp_paste_in_place(pp, out, va_opt_begin);
            }
            va_opt_paste_left = false;
            va_opt_merged = false;
            va_opt_pseudo_paste = false;
            va_opt_pseudo_lead = BUSTER_PP_NONE;
            va_opt_end = BUSTER_PP_NONE;
            va_opt_first = BUSTER_PP_NONE;
        }
        if (i == count)
        {
            break;
        }
        const PPItem* item = &items[i];
        pp->history_anchor = macro->base + pp->datas[macro->data].starts[item->token];
        pp->history_spelling = item->kind == PP_ITEM_PASTE && i != 0 ? macro->base + pp->datas[macro->data].starts[items[i - 1].token] : pp->history_anchor;
        // ## after the item, in the same __VA_OPT__'s content when it is in one.
        const bool pasted_after = i + 1 < count && i + 1 != va_opt_end && items[i + 1].kind == PP_ITEM_PASTE;
        const bool after_literal_paste = literal_paste;
        literal_paste = false;
        // The item's token is read (a ## only when clang doesn't skip it, below).
        next_space |= item->kind != PP_ITEM_PASTE && (item->flags & BUSTER_PP_ITEM_SPACE) != 0;
        switch (item->kind)
        {
            case PP_ITEM_TOKEN:
            {
                const uint32_t at = out->count;
                if (BUSTER_VECTOR_PATH(pp->vectorized))
                {
                    // The compiled suffix is a run of consecutive body tokens with their own leading spaces.
                    // Keep the active __VA_OPT__ boundary explicit even though its removed ')' breaks the run.
                    uint32_t last = i + item->parameter;
                    last = va_opt_end > i && va_opt_end <= last ? va_opt_end - 1 : last;
                    pp_append_file_tokens(pp, out, &pp->datas[macro->data], macro->base, item->token, items[last].token + 1, true);
                    i = last;
                }
                else
                {
                    pp_push_token(pp, out, pp_body_token(pp, macro, item));
                }
                if (at < out->count)
                {
                    pp_set_space(out, at, next_space || (after_literal_paste && pp_file_space(&pp->datas[macro->data], item->token)));
                }
                next_space = false;
                break;
            }
            case PP_ITEM_PARAMETER:
            {
                // Expanded, but next to ## (one that a `## ##` left alone before it, too: PP_ITEM_PASTE's).
                const bool pasted_before = i > 0 && items[i - 1].kind == PP_ITEM_PASTE;
                PPTokens* tokens;
                uint32_t first;
                uint32_t last;
                pp_argument_range(pp, invocation, item->parameter, !pasted_after && !pasted_before, &tokens, &first, &last);
                if (first != last)
                {
                    // GNU's comma's variadic argument keeps its first token's own space (VaArgsPseudoPaste).
                    const bool pseudo_paste = va_opt_pseudo_paste && i == va_opt_first;
                    pp_append_tokens(pp, tokens, first, last, !pseudo_paste, next_space ? BUSTER_PP_SPACE : 0);
                    next_space = false;
                }
                else
                {
                    // An empty argument: a placemarker next to ##, or at an end of a __VA_OPT__'s content next to ## outside
                    // it.
                    const bool edge = va_opt_end != BUSTER_PP_NONE && ((i == va_opt_first && va_opt_paste_left) || (i + 1 == va_opt_end && va_opt_pasted_after));
                    if (pasted_after || edge)
                    {
                        pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER });
                        placemarkers = true;
                    }
                }
                break;
            }
            case PP_ITEM_STRINGIZE:
            {
                PPTokens* tokens;
                uint32_t first;
                uint32_t last;
                pp_argument_range(pp, invocation, item->parameter, false, &tokens, &first, &last);
                pp_push_token(pp, out, pp_stringize(pp, tokens, first, last, next_space ? BUSTER_PP_SPACE : 0, pp->history_anchor));
                next_space = false;
                break;
            }
            case PP_ITEM_VA_OPT:
            case PP_ITEM_STRINGIZE_VA_OPT:
            {
                const uint32_t content_end = i + 1 + item->parameter;
                const bool content_pasted_after = content_end < count && items[content_end].kind == PP_ITEM_PASTE;
                // # __VA_OPT__'s string takes the space pending at the #, which stays pending; its __VA_OPT__ is read next.
                const bool string_space = next_space;
                next_space |= (item->flags & BUSTER_PP_ITEM_VA_OPT_SPACE) != 0;
                if (variadic_present)
                {
                    va_opt_end = content_end;
                    va_opt_first = i + 1;
                    va_opt_begin = out->count;
                    va_opt_flags = item->flags;
                    va_opt_pasted_after = content_pasted_after;
                    va_opt_mark = item->kind == PP_ITEM_STRINGIZE_VA_OPT ? out->count : BUSTER_PP_NONE;
                    va_opt_string_space = string_space;
                    va_opt_string_location = pp->history_anchor;
                    if (va_opt_pseudo_paste && va_opt_pseudo_lead > i + 1)
                    {
                        // GNU's comma past empty arguments: clang drops them with their ##, and the variadic argument
                        // keeps its first token's own space, as at the content's start, which it is read as.
                        va_opt_first = va_opt_pseudo_lead;
                        i = va_opt_pseudo_lead - 1;
                    }
                }
                else
                {
                    // Skipped, as clang skips the content: its tokens and its ')' are read, and set the pending space.
                    next_space |= (item->flags & BUSTER_PP_ITEM_SKIP_SPACE) != 0;
                    if (item->kind == PP_ITEM_STRINGIZE_VA_OPT)
                    {
                        pp_push_token(pp, out, pp_stringize(pp, out, out->count, out->count, string_space ? BUSTER_PP_SPACE : 0, pp->history_anchor));
                        if (va_opt_paste_left)
                        {
                            pp->history_anchor = va_opt_paste_location;
                            pp->history_spelling = va_opt_paste_spelling;
                            pp_paste_in_place(pp, out, out->count - 1);
                        }
                    }
                    else if (content_pasted_after && !va_opt_paste_left)
                    {
                        pp_push_token(pp, out, (PPToken){ .kind = BUSTER_PP_KIND_PLACEMARKER });
                        placemarkers = true;
                    }
                    va_opt_paste_left = false;
                    i += item->parameter;
                }
                break;
            }
            case PP_ITEM_PASTE:
            {
                // The left operand is the last token so far (a placemarker for an empty argument); the right one is the
                // next item's first token, whose other tokens follow the result. Two ## in a row are as clang has them
                // (ExpandFunctionArguments, then TokenLexer::pasteTokens): an empty argument after the second takes it
                // away, and the first's right operand is then what follows the argument; an empty left argument takes
                // the first away, and the second pastes what came before it; else the second ## is the first's right
                // operand, which never makes one token, and stays a token. A ## after a token takes the pending space
                // (clang gives it the ## token); one after a placemarker is skipped, and its right operand takes it.
                uint32_t operand = i + 1;
                while (operand + 1 < count && operand != va_opt_end && operand + 1 != va_opt_end && items[operand].kind == PP_ITEM_PASTE &&
                       items[operand + 1].kind == PP_ITEM_PARAMETER && pp_argument_empty(pp, invocation, items[operand + 1].parameter))
                {
                    operand += 2;
                }
                if (out->count == *begin)
                {
                    // A ## an empty argument left first: it is a token.
                    PPToken hash = pp_body_token(pp, macro, item);
                    hash.flags = (uint8_t)((hash.flags & ~BUSTER_PP_SPACE) | (next_space ? BUSTER_PP_SPACE : 0));
                    pp_push_token(pp, out, hash);
                    next_space = false;
                    literal_paste = true;
                    break;
                }
                PPToken left = pp_stack_token(out, out->count - 1);
                const bool placemarker_left = left.kind == BUSTER_PP_KIND_PLACEMARKER;
                next_space = next_space && placemarker_left;
                if (operand >= count || operand == va_opt_end)
                {
                    // Nothing after the arguments it took away: the left operand stays.
                    i = operand - 1;
                    break;
                }
                if (items[operand].kind == PP_ITEM_PASTE && placemarker_left)
                {
                    out->count -= 1;
                    i = operand - 1;
                    break;
                }
                if (items[operand].kind == PP_ITEM_PASTE)
                {
                    const PPToken hash = pp_body_token(pp, macro, &items[operand]);
                    PPToken pasted;
                    if (pp_paste(pp, left, hash, &pasted))
                    {
                        out->count -= 1;
                        pp_push_token(pp, out, pasted);
                    }
                    else
                    {
                        pp_push_token(pp, out, hash);
                    }
                    i = operand;
                    break;
                }
                const PPItem* next = &items[operand];
                // GNU's `, ## __VA_ARGS__`, with the comma the last token so far wherever it came from, as clang has it
                // (TokenLexer::MaybeRemoveCommaBeforeVaArgs): an omitted variadic argument drops it, from before an empty
                // argument's placemarker too (`, x ## __VA_ARGS__`), where clang has no token, but not with ## after
                // __VA_ARGS__, which clang skips first.
                const bool variadic_right = variadic && next->kind == PP_ITEM_PARAMETER && next->parameter == variadic_index;
                const bool variadic_pasted_after = operand + 1 < count && operand + 1 != va_opt_end && items[operand + 1].kind == PP_ITEM_PASTE;
                uint32_t last_real = out->count;
                while (last_real > *begin && out->kinds[last_real - 1] == BUSTER_PP_KIND_PLACEMARKER)
                {
                    last_real -= 1;
                }
                const bool comma_before = last_real > *begin && out->kinds[last_real - 1] == C_TOKEN_COMMA;
                const bool gnu_comma = variadic_right && (invocation->variadic_omitted ? !variadic_pasted_after && (left.kind == C_TOKEN_COMMA || (placemarker_left && comma_before))
                                                                                     : left.kind == C_TOKEN_COMMA);
                // `X ## , ## __VA_ARGS__` with the variadic argument omitted is X: the comma goes, and the ## before it.
                const PPFileData* body = &pp->datas[macro->data];
                const bool comma_paste = !placemarker_left && invocation->variadic_omitted && next->kind == PP_ITEM_TOKEN &&
                                         (body->kinds[next->token] & 0x7F) == C_TOKEN_COMMA && operand + 2 < count && operand + 1 != va_opt_end &&
                                         operand + 2 != va_opt_end && items[operand + 1].kind == PP_ITEM_PASTE && items[operand + 2].kind == PP_ITEM_PARAMETER &&
                                         items[operand + 2].parameter == variadic_index;
                PPTokens* tokens = out;
                uint32_t first = 0;
                uint32_t last = 0;
                PPToken right_token;
                bool right_single = false;
                if (next->kind == PP_ITEM_PARAMETER)
                {
                    // Unexpanded, but for an argument past one a `## ##` took away that isn't next to ## itself.
                    const bool pasted = items[operand - 1].kind == PP_ITEM_PASTE || (operand + 1 < count && operand + 1 != va_opt_end && items[operand + 1].kind == PP_ITEM_PASTE);
                    pp_argument_range(pp, invocation, next->parameter, !pasted, &tokens, &first, &last);
                }
                else if (next->kind == PP_ITEM_STRINGIZE)
                {
                    PPTokens* argument_tokens;
                    uint32_t argument_first;
                    uint32_t argument_last;
                    pp_argument_range(pp, invocation, next->parameter, false, &argument_tokens, &argument_first, &argument_last);
                    right_token = pp_stringize(pp, argument_tokens, argument_first, argument_last, 0, macro->base + pp->datas[macro->data].starts[next->token]);
                    right_single = true;
                }
                else if (next->kind == PP_ITEM_TOKEN)
                {
                    right_token = pp_body_token(pp, macro, next);
                    right_single = true;
                }
                if (comma_paste)
                {
                    next_space = false;
                    i = operand + 2;
                }
                else if (gnu_comma)
                {
                    // `, ## __VA_ARGS__`: no paste. An omitted variadic argument drops the comma (a placemarker, which
                    // goes with the others); any other is placed after it unexpanded, its first token with its own
                    // leading space; and no space is pending after either (VaArgsPseudoPaste in clang).
                    if (invocation->variadic_omitted)
                    {
                        out->kinds[last_real - 1] = BUSTER_PP_KIND_PLACEMARKER;
                        placemarkers = true;
                    }
                    else
                    {
                        pp->history_anchor = macro->base + pp->datas[macro->data].starts[next->token];
                        pp_append_tokens(pp, tokens, first, last, false, 0);
                    }
                    next_space = false;
                    i = operand;
                }
                else if (next->kind == PP_ITEM_VA_OPT || next->kind == PP_ITEM_STRINGIZE_VA_OPT)
                {
                    // `## __VA_OPT__(...)`: the __VA_OPT__'s first token pastes onto the left operand as soon as it is
                    // known (below), and # __VA_OPT__'s string when it is made (above). But `, ## __VA_OPT__(__VA_ARGS__
                    // ## ...)` is GNU's comma, as clang sees the comma before the ## (VaArgsPseudoPaste): the variadic
                    // argument follows it unpasted. So is one after empty arguments pasted onto what follows them
                    // (`d ## __VA_ARGS__`), which clang drops with their ## (TokenLexer::ExpandFunctionArguments), so
                    // that the variadic argument is the ##'s operand.
                    const uint32_t content_end = operand + 1 + next->parameter;
                    uint32_t lead = operand + 1;
                    bool skipping = true;
                    while (skipping && lead + 2 < content_end && items[lead].kind == PP_ITEM_PARAMETER && items[lead].parameter != variadic_index &&
                           items[lead + 1].kind == PP_ITEM_PASTE)
                    {
                        PPTokens* lead_tokens;
                        uint32_t lead_first;
                        uint32_t lead_last;
                        pp_argument_range(pp, invocation, items[lead].parameter, false, &lead_tokens, &lead_first, &lead_last);
                        skipping = lead_first == lead_last;
                        lead += skipping ? 2 : 0;
                    }
                    va_opt_pseudo_paste = variadic_present && next->kind == PP_ITEM_VA_OPT && left.kind == C_TOKEN_COMMA && lead < content_end &&
                                          items[lead].kind == PP_ITEM_PARAMETER && items[lead].parameter == variadic_index &&
                                          (lead > operand + 1 || (lead + 1 < content_end && items[lead + 1].kind == PP_ITEM_PASTE));
                    va_opt_pseudo_lead = va_opt_pseudo_paste ? lead : BUSTER_PP_NONE;
                    va_opt_paste_left = !va_opt_pseudo_paste;
                    va_opt_paste_location = pp->history_anchor;
                    va_opt_paste_spelling = pp->history_spelling;
                    i = operand - 1;
                }
                else
                {
                    const bool right_empty = !right_single && first == last;
                    if (right_empty)
                    {
                        // X ## placemarker is X.
                    }
                    else
                    {
                        const PPToken right = right_single ? right_token : pp_stack_token(tokens, first);
                        if (placemarker_left)
                        {
                            out->count -= 1;
                            PPToken moved = right;
                            // As for an argument placed first, clang clears its line start, including one retained
                            // while an outer argument expanded (BUSTER_PP_LINE_SPACE).
                            moved.flags = (uint8_t)((moved.flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE)) | (next_space ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO);
                            if (!right_single)
                            {
                                moved.origin = pp_history_substitution(pp, right.origin, macro->base + pp->datas[macro->data].starts[next->token]);
                            }
                            pp_push_token(pp, out, moved);
                            next_space = false;
                        }
                        else
                        {
                            PPToken pasted;
                            if (pp_paste(pp, left, right, &pasted))
                            {
                                out->count -= 1;
                                pp_push_token(pp, out, pasted);
                            }
                            else
                            {
                                PPToken kept = right;
                                kept.flags = (uint8_t)((kept.flags & ~BUSTER_PP_START_OF_LINE) | BUSTER_PP_FROM_MACRO);
                                if (!right_single)
                                {
                                    kept.origin = pp_history_substitution(pp, right.origin, macro->base + pp->datas[macro->data].starts[next->token]);
                                }
                                pp_push_token(pp, out, kept);
                            }
                        }
                        if (!right_single && last - first > 1)
                        {
                            pp->history_anchor = macro->base + pp->datas[macro->data].starts[next->token];
                            pp_append_tokens(pp, tokens, first + 1, last, false, 0);
                        }
                    }
                    i = operand;
                }
                break;
            }
        }
        // The first token of the content of a __VA_OPT__ after ## pastes onto the ##'s left operand as soon as it is
        // made, as clang's pastes go left to right (TokenLexer::Lex): `x ## __VA_OPT__(a ## b)` is (x a) b. A
        // placemarker first (an empty argument) waits: one that a ## gives a token takes it then, and one that stays
        // takes the ## away when the content ends (above), as clang's placeholder does.
        if (va_opt_paste_left && va_opt_end != BUSTER_PP_NONE && va_opt_mark == BUSTER_PP_NONE && out->count > va_opt_begin &&
            out->kinds[va_opt_begin] != BUSTER_PP_KIND_PLACEMARKER)
        {
            pp->history_anchor = va_opt_paste_location;
            pp->history_spelling = va_opt_paste_spelling;
            pp_paste_in_place(pp, out, va_opt_begin);
            va_opt_paste_left = false;
            va_opt_merged = true;
        }
    }
    // The pending space: the token after the expansion takes it.
    *trailing = next_space ? BUSTER_PP_SPACE : 0;
    // Remove the placemarkers.
    if (placemarkers)
    {
        uint32_t kept = *begin;
        for (uint32_t i = *begin; i < out->count; i += 1)
        {
            if (out->kinds[i] != BUSTER_PP_KIND_PLACEMARKER)
            {
                pp_tokens_set(out, kept, out->kinds[i], out->flags[i], out->values[i], out->locations[i]);
                if (out->origins != 0)
                {
                    out->origins[kept] = out->origins[i];
                }
                kept += 1;
            }
        }
        out->count = kept;
    }
    pp->history_current = saved_history;
}

// Does an argument name an enabled macro that isn't painted? Then expanding it can change it. (A name of a disabled
// macro is painted when the expansion is rescanned anyway, since the macros disabled now still are then.)
static bool pp_needs_expansion(const Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end)
{
    bool result = false;
    for (uint32_t i = begin; !result && i < end; i += 1)
    {
        const IdentId id = tokens->values[i];
        result = tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER && !(tokens->flags[i] & BUSTER_PP_NO_EXPAND) && pp_is_macro(pp, id) &&
                 pp->macros[pp->macro_of[id]].disabled == 0;
    }
    return result;
}

static bool pp_push_capture(Preprocessor* pp, uint8_t kind, uint32_t invocation, uint32_t argument)
{
    const bool result = pp->capture_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift);
    if (result)
    {
        pp->capture_frames[pp->capture_count] = (PPCapture){ .kind = kind, .begin = pp->captures.count, .invocation = invocation, .argument = argument, .pragma_mark = pp->pragma_lines };
        pp->capture_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// Expands tokens [begin, end) of pp->arguments as a line of their own (a directive's, or a builtin's operand) without
// running the main loop here: pushes `capture` and a barrier context over the tokens, and returns to the main loop,
// which expands them into the capture and, when the barrier ends, runs the capture's continuation (pp_finish_capture).
// So a directive in a macro's arguments, a #if line's builtins and their operands nest in explicit stacks, never in
// calls. Returns false (after a fatal error) if a stack is full.
static bool pp_start_capture(Preprocessor* pp, PPCapture capture, uint32_t begin, uint32_t end, uint32_t expansion_end)
{
    bool result = pp->capture_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift);
    if (result)
    {
        capture.begin = pp->captures.count;
        capture.pragma_mark = pp->pragma_lines;
        capture.counted_to = capture.begin;
        capture.counted = 0;
        capture.counted_state = BUSTER_PP_PRAGMA_OUTSIDE;
        pp->capture_frames[pp->capture_count] = capture;
        pp->capture_count += 1;
        result = pp_push_buffer(pp, &pp->arguments, begin, end, 0, expansion_end, true, false) != 0;
        pp->capture_count -= !result;
        pp->limit_count += result && capture.limit != 0;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "macro expansion nests too deeply");
    }
    return result;
}

// Starts expanding the top invocation's argument `parameter` if it names an enabled macro: a barrier context with a
// capture, which the main loop runs (pp_finish_argument takes it from there). Else the argument is its own expansion.
// Returns whether the main loop must run the capture.
BUSTER_INLINE bool pp_expand_argument(Preprocessor* pp, uint32_t parameter)
{
    const uint32_t index = pp->invocation_count - 1;
    const PPInvocation* invocation = &pp->invocations[index];
    PPArgument* argument = &pp->argument_records[invocation->arguments + parameter];
    bool result = false;
    if (pp_needs_expansion(pp, &pp->arguments, argument->raw_begin, argument->raw_end))
    {
        pp->statistics.expanded_arguments += pp->options.timing;
        result = pp_push_capture(pp, BUSTER_PP_CAPTURE_ARGUMENT, index, parameter) &&
                 pp_push_buffer(pp, &pp->arguments, argument->raw_begin, argument->raw_end, 0, invocation->expansion_end, true, false) != 0;
    }
    else
    {
        argument->expanded_begin = argument->raw_begin;
        argument->expanded_end = argument->raw_end;
        argument->expanded_captured = false;
        argument->expanded = true;
    }
    return result;
}

// Expands the top invocation's arguments that need it, one at a time (pp_expand_argument), in the order clang does
// (TokenLexer::ExpandFunctionArguments, which expands an argument at its first use): a BUSTER_PP_MACRO_IN_ORDER macro's in
// order; any other's by walking its body, where an argument is expanded at the first use of its parameter that isn't an
// operand of # or ## (as pp_compile_macro has them), and the variadic one at a __VA_OPT__ with content, which the walk
// skips when that has no tokens (or the macro isn't variadic), as substituting it does. Once the walk is done, the
// arguments it didn't expand stand for themselves, and the invocation is substituted.
static void pp_continue_invocation(Preprocessor* pp)
{
    const uint32_t index = pp->invocation_count - 1;
    PPInvocation* invocation = &pp->invocations[index];
    const PPMacro* macro = &pp->macros[invocation->macro];
    bool waiting = false;
    if (macro->flags & BUSTER_PP_MACRO_IN_ORDER)
    {
        const PPItem* order = pp->items + macro->items_begin + macro->item_count;
        while (!waiting && invocation->next < macro->order_count && !pp->fatal)
        {
            const uint32_t parameter = order[invocation->next].parameter;
            waiting = parameter < invocation->argument_count && !pp->argument_records[invocation->arguments + parameter].expanded && pp_expand_argument(pp, parameter);
            invocation->next += !waiting;
        }
    }
    else
    {
        const PPItem* items = pp->items + macro->items_begin;
        const uint32_t count = macro->item_count;
        const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0 && invocation->argument_count != 0;
        const uint32_t variadic_index = macro->parameter_count - 1u;
        while (!waiting && invocation->next < count && !pp->fatal)
        {
            const uint32_t i = invocation->next;
            const PPItem* item = &items[i];
            const bool va_opt = item->kind == PP_ITEM_VA_OPT || item->kind == PP_ITEM_STRINGIZE_VA_OPT;
            const bool pasted = (i > 0 && items[i - 1].kind == PP_ITEM_PASTE) || (i + 1 < count && i + 1 != invocation->content_end && items[i + 1].kind == PP_ITEM_PASTE);
            // An empty __VA_OPT__() expands nothing (clang looks at the variadic argument for its content's first token).
            const bool tests = va_opt && variadic && item->parameter != 0;
            const uint32_t parameter = item->kind == PP_ITEM_PARAMETER && !pasted ? item->parameter : tests ? variadic_index : BUSTER_PP_NONE;
            const PPArgument* argument = parameter != BUSTER_PP_NONE ? &pp->argument_records[invocation->arguments + parameter] : 0;
            waiting = argument != 0 && !argument->expanded && pp_expand_argument(pp, parameter);
            if (!waiting)
            {
                const bool present = tests && argument->expanded_begin != argument->expanded_end;
                invocation->content_end = present ? i + 1 + item->parameter : invocation->content_end;
                invocation->next = va_opt && !present ? i + 1 + item->parameter : i + 1;
                invocation->content_end = invocation->next >= invocation->content_end ? BUSTER_PP_NONE : invocation->content_end;
            }
        }
    }
    for (uint32_t a = 0; !waiting && a < invocation->argument_count; a += 1)
    {
        PPArgument* argument = &pp->argument_records[invocation->arguments + a];
        if (!argument->expanded)
        {
            argument->expanded_begin = argument->raw_begin;
            argument->expanded_end = argument->raw_end;
            argument->expanded_captured = false;
            argument->expanded = true;
        }
    }
    if (!waiting && !pp->fatal)
    {
        pp_finish_invocation(pp);
    }
}

// The barrier of an argument's capture ended: the capture is the expanded argument, and the walk goes on from the use
// that expanded it.
static void pp_finish_argument(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp->capture_count -= 1;
    PPInvocation* invocation = &pp->invocations[capture.invocation];
    PPArgument* argument = &pp->argument_records[invocation->arguments + capture.argument];
    argument->expanded_begin = capture.begin;
    argument->expanded_end = pp->captures.count;
    argument->expanded_captured = true;
    argument->expanded = true;
    pp_continue_invocation(pp);
}

// An invocation's captures go when it is substituted, from pp->captures' token `begin` on, but for the pragma lines
// among them, which directives among its arguments lifted onto the capture below (pp_lift_pragma_lines): they stay
// there, moved down to `begin` in order, where a stream below has read them. Returns the count after them.
static uint32_t pp_keep_pragma_lines(Preprocessor* pp, uint32_t begin)
{
    PPTokens* tokens = &pp->captures;
    uint32_t kept = begin;
    uint32_t state = BUSTER_PP_PRAGMA_OUTSIDE;
    for (uint32_t i = begin; i < tokens->count; i += 1)
    {
        const PPToken token = pp_stack_token(tokens, i);
        state = pp_pragma_state(pp, state, token.kind, token.location);
        if (state != BUSTER_PP_PRAGMA_OUTSIDE || token.kind == BUSTER_PP_KIND_PRAGMA_END)
        {
            pp_tokens_set(tokens, kept, token.kind, token.flags, token.value, token.location);
            pp_history_fill(pp, tokens, kept, kept + 1, token.origin);
            kept += 1;
        }
    }
    return kept;
}

// Substitutes the top invocation, frees its arguments and pushes its expansion, with its macro disabled.
static void pp_finish_invocation(Preprocessor* pp)
{
    const PPInvocation invocation = pp->invocations[pp->invocation_count - 1];
    uint32_t begin;
    uint8_t trailing;
    pp_substitute(pp, &invocation, &begin, &trailing);
    const uint32_t end = pp->expansions.count;
    // The name's line start as clang's token has it: BUSTER_PP_LINE_SPACE among a macro's arguments, and its
    // BUSTER_PP_NAME_LINE (which the invocation took with its name, pp_name_line).
    const uint8_t name_flags = invocation.name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE);
    if (begin < end)
    {
        uint8_t* flags = &pp->expansions.flags[begin];
        *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags | BUSTER_PP_NAME_SPACE);
    }
    pp->captures.count = pp->pragma_lines != invocation.pragma_mark ? pp_keep_pragma_lines(pp, invocation.captures_begin) : invocation.captures_begin;
    pp->arguments.count = invocation.arguments_begin;
    pp->argument_record_count = invocation.arguments;
    pp->invocation_count -= 1;
    if (begin < end)
    {
        PPContext* context = pp_push_buffer(pp, &pp->expansions, begin, end, invocation.macro, invocation.expansion_end, false, true);
        if (context != 0)
        {
            context->trailing_flags = trailing;
        }
    }
    else
    {
        // An empty expansion: the next token read after the invocation's ')' takes its name's flags.
        const uint8_t name_space = (name_flags & BUSTER_PP_SPACE) && !(trailing & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE : 0;
        pp->contexts[pp->context_count - 1].pending_flags |= (uint8_t)((name_flags & ~(name_space != 0 ? 0 : BUSTER_PP_NAME_LINE)) | trailing | name_space);
    }
}

static bool pp_new_argument(Preprocessor* pp)
{
    const bool result = pp->argument_record_count < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift);
    if (result)
    {
        const uint32_t at = pp->arguments.count;
        pp->argument_records[pp->argument_record_count] = (PPArgument){ .raw_begin = at, .raw_end = at };
        pp->argument_record_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many macro arguments");
    }
    return result;
}

// Index each newly written argument token once. At '(' the link is its matching ')'; at ')' it is the first comma
// at that depth, or itself; each comma links to the next comma or ')'. While building, openers and commas form an
// explicit backwards stack. Closing a level reverses its commas once, so all pairing work is linear. An unmatched
// opener retains a backwards link, which cannot be mistaken for a matching closer. Delimiter masks batch the scan;
// only delimiters need ordered pairing, with exactly the same pairing on the scalar path.
// Each appended segment is a complete argument collection, not a continuation of an unmatched indexed opener:
// pp_invoke calls this only after collection has closed, while prescanning its retained argument buffer. A rewind
// keeps a live owner's complete prefix and later collections append complete segments after it. Restarting previous
// at NONE therefore never needs to connect a new closer to an opener before argument_indexed. Unmatched delimiters
// may occur in an argument's tokens, but only complete invocations inside its own context are borrowed.
static bool pp_index_arguments(Preprocessor* pp)
{
    if (pp->argument_links == 0)
    {
        // A table committed whole (arena_reserve_tables), uncharged where commit charge isn't bounded (#1521).
        ArenaTable links = {
            .bytes = (uint64_t)(BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift) * sizeof(uint32_t),
            .small = BUSTER_PP_TABLE_SMALL_BYTES,
        };
        pp->argument_link_arena = arena_reserve_tables(&links, 1);
        pp->argument_links = links.base;
        if (pp->argument_links == 0)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory indexing macro arguments");
        }
    }
    const bool result = pp->argument_links != 0;
    const uint8_t* kinds = pp->arguments.kinds;
    uint32_t* links = pp->argument_links;
    uint32_t previous = BUSTER_PP_NONE;
    for (uint32_t t = pp->argument_indexed; result && t < pp->arguments.count; t += 64)
    {
        const uint32_t count = pp->arguments.count - t < 64 ? pp->arguments.count - t : 64;
        uint64_t delimiters = 0;
        if (BUSTER_VECTOR_PATH(pp->vectorized))
        {
            const __mmask64 valid = _bzhi_u64(~0ull, count);
            const __m512i plain = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, kinds + t), _mm512_set1_epi8(0x7F));
            delimiters = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_L_PAREN)) |
                         _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_R_PAREN)) |
                         _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_COMMA));
        }
        else
        {
            for (uint32_t j = 0; j < count; j += 1)
            {
                const uint8_t kind = kinds[t + j] & 0x7F;
                delimiters |= (uint64_t)(kind == C_TOKEN_L_PAREN || kind == C_TOKEN_R_PAREN || kind == C_TOKEN_COMMA) << j;
            }
        }
        for (uint64_t bits = delimiters; bits != 0; bits &= bits - 1)
        {
            const uint32_t at = t + (uint32_t)__builtin_ctzll(bits);
            const uint8_t kind = kinds[at] & 0x7F;
            if (kind == C_TOKEN_L_PAREN || (kind == C_TOKEN_COMMA && previous != BUSTER_PP_NONE))
            {
                links[at] = previous;
                previous = at;
            }
            else if (kind == C_TOKEN_R_PAREN && previous != BUSTER_PP_NONE)
            {
                uint32_t first = at;
                while ((kinds[previous] & 0x7F) == C_TOKEN_COMMA)
                {
                    const uint32_t before = links[previous];
                    links[previous] = first;
                    first = previous;
                    previous = before;
                }
                const uint32_t parent = links[previous];
                links[previous] = at;
                links[at] = first;
                previous = parent;
            }
        }
    }
    pp->argument_indexed = result ? pp->arguments.count : pp->argument_indexed;
    return result;
}

// Borrow a nested invocation's raw arguments from its current argument buffer. No token, flag or origin is changed;
// each record names a slice that stays alive until the owning outer invocation is substituted. The context must
// contain the whole invocation: barriers, expansion boundaries and directives still use the ordinary collectors.
// A new invocation saves the unchanged stack count, so finishing it frees only its own appended tokens and records.
static bool pp_collect_argument_slice(Preprocessor* pp, PPInvocation* invocation, bool variadic, uint32_t parameter_count)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    const uint32_t open = context->position;
    bool result = context->kind == BUSTER_PP_CONTEXT_BUFFER && context->barrier && context->tokens == &pp->arguments && context->pending_flags == 0 &&
                  pp->capture_count != 0 && pp->capture_frames[pp->capture_count - 1].kind == BUSTER_PP_CAPTURE_ARGUMENT &&
                  open < context->end && pp->arguments.kinds[open] == C_TOKEN_L_PAREN &&
                  (context->end - open >= BUSTER_PP_ARGUMENT_INDEX_MIN || context->end <= pp->argument_indexed);
    result = result && pp_index_arguments(pp);
    const uint32_t close = result ? pp->argument_links[open] : BUSTER_PP_NONE;
    result = result && close > open && close < context->end;
    if (result)
    {
        const uint32_t first = pp->argument_record_count - 1;
        uint32_t record = first;
        uint32_t begin = open + 1;
        uint32_t next = pp->argument_links[close];
        while (result && next < close && !(variadic && record - first + 1 >= parameter_count))
        {
            pp->argument_records[record] = (PPArgument){ .raw_begin = begin, .raw_end = next };
            result = record + 1 < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift);
            record += result;
            begin = next + 1;
            next = pp->argument_links[next];
        }
        if (result)
        {
            pp->argument_records[record] = (PPArgument){ .raw_begin = begin, .raw_end = close };
            pp->argument_record_count = record + 1;
            const PPToken token = pp_stack_token(&pp->arguments, close);
            invocation->expansion_end = (token.flags & BUSTER_PP_FROM_MACRO) ? context->expansion_end : token.location;
            if (invocation->history != 0)
            {
                pp->history[invocation->history - 1].end = token.location;
                pp->history[invocation->history - 1].end_origin = token.origin;
            }
            context->position = close + 1;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many macro arguments");
        }
    }
    return result;
}

// The byte indices 0 to 63, for pp_prefix_sum_bytes' permutes.
static const uint8_t pp_byte_indices[64] __attribute__((aligned(64))) = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63 };

// The inclusive prefix sums of 64 signed bytes, wrapping at 8 bits: byte i becomes the sum of bytes 0 to i. Six steps
// of a permute that moves the bytes up by 1, 2, 4, 8, 16 and 32 places, zeroing the places it shifts in (vpermb: one
// uop on Zen 5, latency 4), and an add.
BUSTER_INLINE __m512i pp_prefix_sum_bytes(__m512i x)
{
    const __m512i indices = _mm512_load_si512(pp_byte_indices);
    for (uint32_t shift = 1; shift < 64; shift *= 2)
    {
        const __m512i from = _mm512_sub_epi8(indices, _mm512_set1_epi8((char)shift));
        x = _mm512_add_epi8(x, _mm512_maskz_permutexvar_epi8(~0ull << shift, from, x));
    }
    return x;
}

// Finds an invocation's ')' and the commas between its arguments in the kinds of a run, its '(' at `open` (the run ends at
// `limit`), when both are in the run, as most are (an invocation in a file's text, or in one macro's expansion), without
// a branch per token. For 64 kinds at a time: the depth of parentheses after each token is a prefix sum of +1 at '(' and
// -1 at ')' (pp_prefix_sum_bytes) on the depth before them; the invocation's ')' is the first token after which the
// depth is -1 (a compare and a tzcnt); and the commas between its arguments are the commas at depth 0 before it (a
// variadic macro's last argument keeps the commas past its other parameters': pdep keeps the first ones). The ')' is
// BUSTER_PP_NONE when there is no '(' at `open`, the ')' isn't in the run, or there are more commas or deeper
// parentheses than the scan holds.
static void pp_scan_arguments(const uint8_t* kinds, uint32_t open, uint32_t limit, bool variadic, uint32_t parameter_count, PPScan* scan)
{
    uint32_t* separators = scan->separators;
    uint32_t separator_count = 0;
    uint32_t close = BUSTER_PP_NONE;
    bool overflow = false;
    if (open < limit && (kinds[open] & 0x7F) == C_TOKEN_L_PAREN)
    {
        // The depth before the block's first token, from 0 inside the invocation's '('.
        uint32_t depth = 0;
        for (uint32_t t = open + 1; close == BUSTER_PP_NONE && !overflow && t < limit; t += 64)
        {
            const __mmask64 valid = _bzhi_u64(~0ull, limit - t < 64 ? limit - t : 64);
            const __m512i plain = _mm512_and_si512(_mm512_maskz_loadu_epi8(valid, kinds + t), _mm512_set1_epi8(0x7F));
            const uint64_t opens = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_L_PAREN));
            const uint64_t closes = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_R_PAREN));
            const uint64_t commas = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_COMMA));
            const uint64_t before_first_close = closes != 0 ? _blsmsk_u64(closes) >> 1 : ~0ull;
            uint64_t closing;
            uint64_t top;
            if (depth == 0 && (opens & before_first_close) == 0)
            {
                // Flat arguments: no nested opener before the first closer, so every earlier comma separates
                // arguments. The masks already give the answer, without six dependent depth-prefix steps.
                closing = closes;
                top = commas & before_first_close;
            }
            else if ((opens | closes) == 0)
            {
                // A block wholly inside an open parenthesis changes no depth and has no argument separators.
                closing = 0;
                top = 0;
            }
            else
            {
                const __m512i steps = _mm512_mask_mov_epi8(_mm512_maskz_mov_epi8(opens, _mm512_set1_epi8(1)), closes, _mm512_set1_epi8(-1));
                const __m512i depths = _mm512_add_epi8(pp_prefix_sum_bytes(steps), _mm512_set1_epi8((char)depth));
                closing = _mm512_mask_cmpeq_epi8_mask(valid, depths, _mm512_set1_epi8(-1));
                const uint64_t before = closing != 0 ? _blsmsk_u64(closing) >> 1 : ~0ull;
                top = commas & before & _mm512_cmpeq_epi8_mask(depths, _mm512_setzero_si512());
            }
            if (variadic)
            {
                const uint32_t room = parameter_count - 1 > separator_count ? parameter_count - 1 - separator_count : 0;
                top = _pdep_u64(_bzhi_u64(~0ull, room), top);
            }
            // A loop per argument, not per token.
            overflow = separator_count + (uint32_t)__builtin_popcountll(top) > BUSTER_PP_FAST_SEPARATORS;
            for (uint64_t bits = overflow ? 0 : top; bits != 0; bits &= bits - 1)
            {
                separators[separator_count] = t + (uint32_t)__builtin_ctzll(bits);
                separator_count += 1;
            }
            close = closing != 0 ? t + (uint32_t)__builtin_ctzll(closing) : BUSTER_PP_NONE;
            depth += (uint32_t)__builtin_popcountll(opens) - (uint32_t)__builtin_popcountll(closes);
            // Past BUSTER_PP_FAST_DEPTH, the next block's 8-bit depths could wrap.
            overflow |= close == BUSTER_PP_NONE && depth > BUSTER_PP_FAST_DEPTH;
        }
    }
    scan->separator_count = separator_count;
    scan->close = overflow ? BUSTER_PP_NONE : close;
}

// Collects an invocation's arguments when its '(' and ')' are both in the top context's run (pp_scan_arguments): the
// tokens between the parentheses go to pp->arguments in one copy (the commas between arguments with them, outside every
// argument's range) and the arguments' records are set, the first of which pp_invoke made. Returns false, having changed
// nothing, otherwise: pp_invoke then reads the tokens one at a time, through the contexts below and any directive on the
// way, as the scalar kernels always do.
static bool pp_collect_arguments(Preprocessor* pp, PPInvocation* invocation, bool variadic, uint32_t parameter_count)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    const bool file = context->kind == BUSTER_PP_CONTEXT_FILE;
    const PPFileData* data = file ? &pp->datas[context->data] : 0;
    const uint8_t* kinds = file ? data->kinds : context->tokens->kinds;
    const uint32_t limit = file ? pp_run_limit(data, context) : context->end;
    const uint32_t open = context->position;
    PPScan scan;
    pp_scan_arguments(kinds, open, limit, variadic, parameter_count, &scan);
    const uint32_t* separators = scan.separators;
    const uint32_t separator_count = scan.separator_count;
    const uint32_t close = scan.close;
    const bool result = close != BUSTER_PP_NONE && pp->argument_record_count + separator_count < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift) &&
                        pp_room(pp, &pp->arguments, close - open);
    if (result)
    {
        // Token t of the context goes to pp->arguments at `at` + t - (open + 1).
        const uint32_t at = pp->arguments.count;
        if (file)
        {
            const uint32_t base = pp->ranges[context->range].base;
            pp_read_raw_errors(pp, context, open + 1, close);
            pp_append_file_tokens(pp, &pp->arguments, data, base, open + 1, close, false);
            invocation->expansion_end = base + data->starts[close];
            if (pp->poison_count != 0)
            {
                pp_check_poison_range(pp, data, base, 0, open + 1, close, BUSTER_PP_NONE, false);
            }
        }
        else
        {
            const PPTokens* tokens = context->tokens;
            pp_append_stack_tokens(pp, &pp->arguments, tokens, open + 1, close);
            invocation->expansion_end = (tokens->flags[close] & BUSTER_PP_FROM_MACRO) ? context->expansion_end : tokens->locations[close];
        }
        PPArgument* records = pp->argument_records;
        uint32_t record = pp->argument_record_count - 1;
        for (uint32_t i = 0; i < separator_count; i += 1)
        {
            records[record].raw_end = at + separators[i] - (open + 1);
            record += 1;
            records[record] = (PPArgument){ .raw_begin = at + separators[i] - open, .raw_end = at + separators[i] - open };
        }
        records[record].raw_end = at + close - (open + 1);
        if (invocation->history != 0)
        {
            pp->history[invocation->history - 1].end = file ? pp->ranges[context->range].base + data->starts[close] : context->tokens->locations[close];
            pp->history[invocation->history - 1].end_origin = !file && context->tokens->origins != 0 ? context->tokens->origins[close] : 0;
        }
        pp->argument_record_count = record + 1;
        context->position = close + 1;
    }
    return result;
}

// Ends the top invocation's collection (`closed`: its ')' was read). A macro's: checks its arguments' count and starts
// expanding them (pp_continue_invocation); an unterminated or invalid invocation is dropped with its tokens. A
// builtin's operand goes to pp_close_builtin.
static void pp_close_invocation(Preprocessor* pp, bool closed)
{
    PPInvocation* invocation = &pp->invocations[pp->invocation_count - 1];
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->diagnostic_origin = invocation->history != 0 ? pp->history[invocation->history - 1].parent : 0;
    invocation->collecting = false;
    if (invocation->builtin)
    {
        pp_close_builtin(pp, closed);
    }
    else
    {
        pp->in_macro_arguments = false;
        const PPMacro* macro = &pp->macros[invocation->macro];
        const PPDiagnosticNote note =
        {
            .location = macro->location,
            .macro = macro->name,
        };
        const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
        const uint32_t parameter_count = macro->parameter_count;
        uint32_t count = pp->argument_record_count - invocation->arguments;
        bool valid = closed;
        pp->unterminated += !closed;
        if (!closed && !pp->fatal)
        {
            pp_diagnose_note(pp, PP_SEVERITY_ERROR, invocation->name_location, &note, "unterminated function-like macro invocation");
        }
        if (valid)
        {
            // f() has no argument when f has no parameter, and one empty argument when it has one.
            const PPArgument* first = &pp->argument_records[invocation->arguments];
            if (parameter_count == 0 && count == 1 && first->raw_begin == first->raw_end)
            {
                count = 0;
            }
            // A variadic macro's variadic argument may be omitted altogether.
            if (variadic && count + 1 == parameter_count)
            {
                invocation->variadic_omitted = true;
                valid = pp_new_argument(pp);
                count += 1;
            }
            // So is it in F() when the variadic parameter is F's only one. GNU's `, ## __VA_ARGS__` then drops its comma,
            // except in C99 and later without GNU extensions, as clang has it.
            else if (variadic && parameter_count == 1)
            {
                invocation->variadic_omitted = count == 1 && first->raw_begin == first->raw_end && (pp->options.language.gnu || pp->options.language.standard < 1999);
            }
            if (valid && count != parameter_count)
            {
                uint32_t where = invocation->name_location;
                if (count < parameter_count && invocation->history != 0)
                {
                    where = pp->history[invocation->history - 1].end;
                    pp->diagnostic_origin = pp->history[invocation->history - 1].end_origin;
                }
                else if (count > parameter_count)
                {
                    const uint32_t first = pp->argument_records[invocation->arguments + parameter_count].raw_begin;
                    if (first < pp->arguments.count)
                    {
                        where = pp->arguments.locations[first];
                        pp->diagnostic_origin = pp->arguments.origins != 0 ? pp->arguments.origins[first] : 0;
                    }
                }
                pp_diagnose_note(pp, PP_SEVERITY_ERROR, where, &note, "too %s arguments provided to function-like macro invocation",
                            count > parameter_count ? "many" : "few");
                valid = false;
            }
        }
        pp->diagnostic_origin = saved_origin;
        invocation->argument_count = count;
        if (pp->options.timing)
        {
            pp->statistics.invocations += 1;
            pp->statistics.arguments += count;
        }
        if (valid && (macro->flags & BUSTER_PP_MACRO_COMPILED))
        {
            pp_continue_invocation(pp);
        }
        else
        {
            pp->arguments.count = invocation->arguments_begin;
            pp->argument_record_count = invocation->arguments;
            pp->invocation_count -= 1;
        }
    }
    pp->diagnostic_origin = saved_origin;
}

// The builtins whose operand clang reads expanded, a token at a time, to the ')' that closes it
// (EvaluateFeatureLikeBuiltinMacro with ExpandArgs): the attribute queries. A macro's expansion may close it, and what
// follows the ')' there is read after the answer (pp_stream_check).
BUSTER_INLINE bool pp_builtin_streams(uint8_t builtin)
{
    return builtin == PP_BUILTIN_HAS_ATTRIBUTE || builtin == PP_BUILTIN_HAS_C_ATTRIBUTE || builtin == PP_BUILTIN_HAS_CPP_ATTRIBUTE ||
           builtin == PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE;
}

// Adds one token to the collecting invocation (C 6.10.4: commas separate arguments only outside of nested parentheses,
// and not in the variadic argument; a builtin's operand isn't split).
static PPCollected pp_collect_token(Preprocessor* pp, PPInvocation* invocation, PPToken token, uint32_t expansion_end)
{
    PPCollected result = PP_COLLECTED_MORE;
    const PPMacro* macro = &pp->macros[invocation->macro];
    const bool variadic = !invocation->builtin && (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    const uint32_t current = pp->argument_record_count - 1 - invocation->arguments;
    if (!invocation->opened)
    {
        invocation->opened = token.kind == C_TOKEN_L_PAREN;
        result = !invocation->opened ? PP_COLLECTED_INVALID : pp_builtin_streams(macro->builtin) ? PP_COLLECTED_STREAM : PP_COLLECTED_MORE;
    }
    else if (token.kind == C_TOKEN_R_PAREN && invocation->depth == 0)
    {
        invocation->expansion_end = expansion_end;
        if (invocation->history != 0)
        {
            pp->history[invocation->history - 1].end = token.location;
            pp->history[invocation->history - 1].end_origin = token.origin;
        }
        result = PP_COLLECTED_CLOSED;
    }
    else if (!invocation->builtin && token.kind == C_TOKEN_COMMA && invocation->depth == 0 && !(variadic && current + 1 >= macro->parameter_count))
    {
        result = pp_new_argument(pp) ? PP_COLLECTED_MORE : PP_COLLECTED_INVALID;
    }
    else
    {
        invocation->depth += token.kind == C_TOKEN_L_PAREN;
        invocation->depth -= token.kind == C_TOKEN_R_PAREN;
        result = pp_push_token(pp, &pp->arguments, token) ? PP_COLLECTED_MORE : PP_COLLECTED_INVALID;
        pp->argument_records[pp->argument_record_count - 1].raw_end = pp->arguments.count;
    }
    return result;
}

// Is the main loop collecting the top invocation's tokens? Not while a capture started among them (a directive's line)
// is being expanded.
BUSTER_INLINE bool pp_collecting(const Preprocessor* pp)
{
    return pp->invocation_count != 0 && pp->invocations[pp->invocation_count - 1].collecting && pp->capture_count == pp->invocations[pp->invocation_count - 1].capture_mark;
}

// The directives clang rejects among a function-like macro's arguments (Preprocessor::HandleDirective's
// err_embedded_directive): the inclusions, #pragma and #embed. Their names, by kind, or null for the others.
static const char* pp_embedded_directive_name(uint8_t kind)
{
    const char* result = 0;
    switch (kind)
    {
        case PP_DIRECTIVE_INCLUDE: result = "include"; break;
        case PP_DIRECTIVE_INCLUDE_NEXT: result = "include_next"; break;
        case PP_DIRECTIVE_IMPORT: result = "import"; break;
        case PP_DIRECTIVE_PRAGMA: result = "pragma"; break;
        case PP_DIRECTIVE_EMBED: result = "embed"; break;
        default: break;
    }
    return result;
}

// Did the ordinary lexer read the token before byte `stop` as a raw string that runs past it? Only that rare case
// can hide source after a confirmed header name. Read just its prefix through phases 1 and 2, including a spliced R.
static bool pp_query_raw_overlap(const Preprocessor* pp, const PPFileData* data, uint32_t first, uint32_t stop)
{
    uint32_t low = 0;
    uint32_t high = data->token_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = data->starts[middle] < stop;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    const uint32_t token = low != 0 ? low - 1 : 0;
    const uint8_t kind = low != 0 ? data->kinds[token] & 0x7F : 0;
    const bool candidate = low != 0 && data->starts[token] > first &&
                           (kind == C_TOKEN_UNKNOWN || pp_is_string_literal(kind)) && pp_file_token_end(pp, data, token) > stop;
    bool result = false;
    if (candidate)
    {
        const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
        uint64_t at = data->starts[token];
        int32_t previous = 0;
        bool quoted = false;
        for (uint32_t i = 0; !quoted && i < 4; i += 1)
        {
            uint64_t size;
            uint64_t start;
            const int32_t c = get_char(&lexer, at, &size, &start);
            result = c == '"' && previous == 'R';
            quoted = c == '"';
            at += size;
            previous = c;
        }
    }
    return result;
}

// The executing directive or expanded query has proved [begin, stop) opaque to ordinary C lexing. Prepare a
// private view for this inclusion with only that span masked. Original source bytes/offsets, old macro data indexes,
// other inclusions and shared cache records stay unchanged. Previous masks are retained. Callers invoke this only
// for an exceptional lexical overlap, and reload their data afterward: position moves to the rebuilt directive end.
// Every call must mask new bytes: source length bounds progress, with no arbitrary per-inclusion repair count.
// Immutable token/directive snapshots remain alive for old macro definitions. One masking buffer is reused per
// inclusion: O(N + sum of prepared columns) storage for N bytes. Preparation remains O(R * N) for R exceptional
// repairs; ordinary inputs allocate no private view. Real arena/index exhaustion is fatal.
static bool pp_recover_opaque_span(Preprocessor* pp, uint32_t file_context, uint32_t directive, uint32_t begin, uint32_t stop)
{
    PPContext* file = &pp->contexts[file_context];
    const PPFileData* data = &pp->datas[file->data];
    bool result = true;
    if (begin >= stop || stop > data->length)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, pp->ranges[file->range].base + begin, "invalid opaque directive span");
        result = false;
    }
    else
    {
        uint8_t* lexical = pp->data_count >= (BUSTER_PP_MAX_FILES >> pp->capacity_shift) ? 0 : data->lexical_bytes != 0 ? (uint8_t*)data->lexical_bytes : arena_push(&pp->arena, data->length, 1);
        result = lexical != 0;
        if (result)
        {
            if (data->lexical_bytes == 0)
            {
                memcpy(lexical, data->bytes, data->length);
            }
            const ScalarLexer lexer = { .source = data->bytes, .length = data->length, .trigraphs = pp->options.trigraphs };
            uint64_t p = begin;
            bool changed = false;
            while (p < stop)
            {
                uint64_t size;
                uint64_t at;
                get_char(&lexer, p, &size, &at);
                // Leave physical splices before `at` intact, preserving logical lines and original source offsets.
                const uint64_t end = p + size < stop ? p + size : stop;
                for (uint64_t i = at; i < end; i += 1)
                {
                    changed |= lexical[i] != ' ';
                }
                memset(lexical + at, ' ', at < end ? end - at : 0);
                p += size != 0 ? size : 1;
            }
            PPFileData recovered = *data;
            recovered.bytes = lexical;
            recovered.lexical_bytes = lexical;
            recovered.mapped = false;
            recovered.prepared = false;
            recovered.unterminated_comment = 0;
            recovered.unterminated_comment_starts_line = false;
            const PPPrepare prepare = pp_prepare_config(pp);
            if (!changed)
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, pp->ranges[file->range].base + begin, "opaque directive recovery made no progress");
            }
            result = changed && pp_prepare_into(&prepare, pp->table, pp->columns, &pp->arena, &recovered, 0) == PP_PREPARE_DONE;
            if (result)
            {
                recovered.bytes = data->bytes;
                pp->datas[pp->data_count] = recovered;
                file->data = pp->data_count;
                pp->data_count += 1;
                file->end = recovered.token_count;
                file->position = recovered.directive_end[directive];
                file->next_directive = directive + 1;
                file->comment_read = false;
            }
        }
        if (!result && !pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, pp->ranges[file->range].base + begin, "out of memory recovering an opaque directive span");
        }
    }
    return result;
}

// Raw-looking header bytes need this correction only after expansion confirms the query's source header context.
// Genuine multiline raw macro arguments never call the primitive merely because they appear on a directive line.
static bool pp_recover_query_raw(Preprocessor* pp, uint32_t file_context, uint32_t directive, uint32_t first, uint32_t stop)
{
    const PPContext* file = &pp->contexts[file_context];
    const PPFileData* data = &pp->datas[file->data];
    const bool overlap = (pp->refine_flags & C_TOKENIZER_RAW_STRINGS) && pp_query_raw_overlap(pp, data, first, stop);
    return !overlap || pp_recover_opaque_span(pp, file_context, directive, first + 1, stop - 1);
}

// Only an executing direct header directive owns this source span. An excluded directive never reaches this hook,
// so raw-looking text in skipped groups retains the ordinary lexer's behavior and cannot reveal hidden directives.
static bool pp_recover_direct_raw(Preprocessor* pp, PPContext* context, uint32_t directive)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint8_t kind = data->directive_kind[directive];
    const bool header = kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT || kind == PP_DIRECTIVE_EMBED;
    const uint32_t first = data->directive_hash[directive] + 2;
    const uint32_t end = data->directive_end[directive];
    bool result = true;
    if (header && (pp->refine_flags & C_TOKENIZER_RAW_STRINGS) && first < end && (data->kinds[first] & 0x7F) == C_TOKEN_LESS)
    {
        uint32_t length;
        bool whole;
        const uint32_t stop = pp_scan_header_name(pp->refine_flags, data, first, end, 0, 0, &length, &whole);
        if (stop != 0 && pp_query_raw_overlap(pp, data, data->starts[first], stop))
        {
            result = pp_recover_opaque_span(pp, (uint32_t)(context - pp->contexts), directive, data->starts[first] + 1, stop - 1);
        }
    }
    return result;
}

// A source header's literal spelling is confirmed before the ordinary lexer's comment is corrected. The private view
// restores file tokens and directive links after it, while old macro definitions and other inclusions keep their data.
static bool pp_recover_header_comment(Preprocessor* pp, uint32_t file_context, uint32_t directive, uint32_t first, uint32_t stop)
{
    PPContext* file = &pp->contexts[file_context];
    bool result = !pp_header_comment_overlap(pp, file, first, stop) || pp_recover_opaque_span(pp, file_context, directive, first + 1, stop - 1);
    const PPFileData* data = &pp->datas[file->data];
    bool invalid_raw = false;
    for (uint32_t i = 0; !invalid_raw && i < data->raw_error_count; i += 1)
    {
        const uint32_t start = data->starts[data->raw_errors[i].token];
        invalid_raw = start > first && start < stop;
    }
    if (result && invalid_raw)
    {
        PPOpaqueSpan* span = arena_push(&pp->arena, sizeof(PPOpaqueSpan), _Alignof(PPOpaqueSpan));
        result = span != 0;
        if (result)
        {
            *span = (PPOpaqueSpan){ .next = file->opaque_spans, .begin = first, .end = stop };
            file->opaque_spans = span;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, pp->ranges[file->range].base + first, "out of memory remembering an opaque header name");
        }
    }
    return result;
}

// Only an executing direct header directive confirms this opaque source span. Excluded directives keep ordinary
// lexing, including their comments; computed headers are identified during macro expansion instead.
static bool pp_recover_direct_header_comment(Preprocessor* pp, PPContext* context, uint32_t directive)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint8_t kind = data->directive_kind[directive];
    const bool header = kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT || kind == PP_DIRECTIVE_EMBED;
    const uint32_t first = data->directive_hash[directive] + 2;
    const uint32_t end = data->directive_end[directive];
    bool result = true;
    if (header && first < end && (data->kinds[first] & 0x7F) == C_TOKEN_LESS)
    {
        uint32_t length;
        bool whole;
        const uint32_t stop = pp_scan_header_name(pp->refine_flags, data, first, end, 0, 0, &length, &whole);
        if (stop != 0)
        {
            result = pp_recover_header_comment(pp, (uint32_t)(context - pp->contexts), directive, data->starts[first], stop);
        }
    }
    return result;
}

// An include-like builtin's '(' (__has_include's, __has_include_next's or __has_embed's) was just read from its #if or
// #elif line itself, the barrier of the line's capture: a '<' next is a header name as the line writes it, read as
// #include reads one (pp_scan_header_name), where clang's LexHeaderName lexes one from its file, and not joined from
// tokens as one a macro or an argument gives (pp_header_name): `__has_include(<  x.h>)` names "  x.h". The operand takes
// the '<' alone, spanning the name to its '>' (pp_written_angled), and the line's tokens inside the name are skipped.
// When the line's tokens past the '>' aren't those clang lexes on from it (pp_header_name_tail: a token or a comment
// of the file's ran past it, as in `<x.h>>` or `<a//b.h>`), the rest of the line is lexed again from the '>', into a
// buffer the main loop reads next, and the barrier's own tokens are left: `<x.h>>` leaves a '>' in the operand, an
// error, and `<a//b.h>) && X` keeps its `&& X`. Without a '>' on the line, nothing changes. Returns what collecting the
// '<' did, or PP_COLLECTED_MORE.
static PPCollected pp_collect_written_header(Preprocessor* pp, PPInvocation* invocation, PPContext* context)
{
    PPCollected result = PP_COLLECTED_MORE;
    const uint8_t builtin = pp->macros[invocation->macro].builtin;
    const bool header = builtin == PP_BUILTIN_HAS_INCLUDE || builtin == PP_BUILTIN_HAS_INCLUDE_NEXT || builtin == PP_BUILTIN_HAS_EMBED;
    const PPCapture* line = pp->capture_count != 0 ? &pp->capture_frames[pp->capture_count - 1] : 0;
    if (header && context->relexed && context->position == context->end && context->relex_position != 0)
    {
        const PPContext* file = &pp->contexts[context->relex_file];
        const PPFileData* data = &pp->datas[file->data];
        const uint32_t base = pp->ranges[file->range].base;
        const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
        const uint32_t first = (uint32_t)pp_skip_blanks(&lexer, context->relex_position);
        uint64_t size;
        uint64_t at;
        const int32_t c = get_char(&lexer, first, &size, &at);
        uint32_t length;
        bool whole;
        const uint32_t stop = c == '<' ? pp_scan_header_bytes(pp->refine_flags, data, first, data->length, 0, 0, &length, &whole) : 0;
        if (stop != 0 && line != 0 && pp_recover_query_raw(pp, context->relex_file, line->directive, first, stop) &&
            pp_recover_header_comment(pp, context->relex_file, line->directive, first, stop))
        {
            const PPToken token = { .kind = C_TOKEN_LESS, .value = base + stop, .location = base + first };
            context->relex_position = stop;
            result = pp_collect_token(pp, invocation, token, pp_expansion_end(context, token));
        }
    }
    else if (header && context->barrier && line != 0 && line->kind == BUSTER_PP_CAPTURE_CONDITION && context->position < context->end &&
             context->tokens->kinds[context->position] == C_TOKEN_LESS)
    {
        // The barrier holds the line's tokens from the one after the directive's name (pp_start_condition).
        PPContext* file = &pp->contexts[line->context];
        const PPFileData* data = &pp->datas[file->data];
        const uint32_t base = pp->ranges[file->range].base;
        const uint32_t first = data->directive_hash[line->directive] + 2 + (context->position - line->arguments_begin);
        const uint32_t end = data->directive_end[line->directive];
        PPToken token = pp_context_token(pp, context, context->position);
        uint32_t length;
        bool whole;
        const uint32_t stop = first < end && base + data->starts[first] == token.location ? pp_scan_header_name(pp->refine_flags, data, first, end, 0, 0, &length, &whole) : 0;
        if (stop != 0 && pp_recover_query_raw(pp, line->context, line->directive, data->starts[first], stop) &&
            pp_recover_header_comment(pp, line->context, line->directive, data->starts[first], stop))
        {
            uint32_t relex;
            const uint32_t after = pp_header_name_tail(pp, data, first, end, stop, &relex);
            token.value = base + stop;
            const uint32_t expansion_end = pp_expansion_end(context, token);
            context->position = relex != stop ? context->end : context->position + (after - first);
            result = pp_collect_token(pp, invocation, token, expansion_end);
            if (relex != stop)
            {
                pp_continue_relex(pp, line->context, stop, context->expansion_end);
            }
        }
    }
    return result;
}

// One step of the main loop while it collects the top invocation's arguments (or a builtin's operand) a token at a time:
// the top context's tokens up to its end or its next directive. An exhausted buffer is popped (which enables its macro
// again); a directive runs, as clang runs one among a macro's arguments (its line may start a capture, during which the
// collection waits), but for those clang rejects there (pp_embedded_directive_name) while its flag says the arguments are
// being collected (Preprocessor.in_macro_arguments), which are an error and skipped; an included file's end goes back to
// the includer, and the end of the run's file or of a barrier leaves the invocation unterminated. A builtin's header name a #if line writes is
// read as the line writes it (pp_collect_written_header).
static void pp_collect_step(Preprocessor* pp)
{
    PPInvocation* invocation = &pp->invocations[pp->invocation_count - 1];
    PPContext* context = &pp->contexts[pp->context_count - 1];
    PPCollected collected = PP_COLLECTED_MORE;
    bool unterminated = false;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        const PPFileData* data = &pp->datas[context->data];
        const uint32_t base = pp->ranges[context->range].base;
        const uint32_t limit = pp_run_limit(data, context);
        if (context->position < limit)
        {
            while (collected == PP_COLLECTED_MORE && context->position < limit)
            {
                pp_read_raw_errors(pp, context, context->position, context->position + 1);
                const PPToken token = pp_file_token(data, base, context->position);
                context->position += 1;
                if (token.kind == C_TOKEN_RAW_IDENTIFIER)
                {
                    pp_check_poison(pp, token.value, token.location);
                }
                collected = pp_collect_token(pp, invocation, token, token.location);
            }
        }
        else if (context->next_directive < data->directive_count)
        {
            const uint32_t d = context->next_directive;
            const char* rejected = invocation->builtin || !pp->in_macro_arguments ? 0 : pp_embedded_directive_name(data->directive_kind[d]);
            if (rejected != 0)
            {
                // At the directive's name, as clang reports it.
                pp_diagnose(pp, PP_SEVERITY_ERROR, base + data->starts[data->directive_hash[d] + 1], "embedding a #%s directive within macro arguments is not supported",
                            rejected);
                context->position = data->directive_end[d];
                context->next_directive = d + 1;
            }
            else
            {
                // Undefined behavior that clang allows, and warns about only with -pedantic (-Wembedded-directive).
                pp_directive(pp, context);
            }
        }
        else if (pp->context_count > 1)
        {
            // An included file's end: clang's lexer goes back to the includer and the collection goes on there, whether
            // the invocation started in the file or an #include among its arguments entered it.
            const uint32_t range = context->range;
            pp_pop_context(pp);
            if (pp->options.expansion_starts && pp->contexts[pp->context_count - 1].kind == BUSTER_PP_CONTEXT_FILE && pp->contexts[pp->context_count - 1].range != range)
            {
                PPIncludeExit* event = arena_push(&pp->arena, sizeof(PPIncludeExit), _Alignof(PPIncludeExit));
                if (event != 0)
                {
                    *event = (PPIncludeExit){
                        .token = pp->output.count,
                        .range = range,
                        .location = pp->file_change_location,
                    };
                    if (pp->last_include_exit != 0)
                    {
                        pp->last_include_exit->next = event;
                    }
                    else
                    {
                        pp->include_exits = event;
                    }
                    pp->last_include_exit = event;
                    pp->ranges[range].collected_exit = pp->output.count + 1;
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_FATAL, pp->file_change_location, "out of memory recording an include exit");
                }
            }
        }
        else
        {
            pp_read_file_comment(pp, context, false);
            unterminated = true;
        }
    }
    else if (context->position < context->end)
    {
        while (collected == PP_COLLECTED_MORE && context->position < context->end)
        {
            PPToken token = pp_context_token(pp, context, context->position);
            const uint32_t expansion_end = pp_expansion_end(context, token);
            const bool opening = !invocation->opened;
            context->position += 1;
            // A name of a macro disabled now is painted: the expansion it is in can end before the argument expands,
            // which enables the macro again (Preprocessor::ReadMacroCallArgumentList, C 6.10.4.4p2).
            const bool disabled = token.kind == C_TOKEN_RAW_IDENTIFIER && pp_is_macro(pp, token.value) && pp->macros[pp->macro_of[token.value]].disabled != 0;
            token.flags |= disabled ? BUSTER_PP_NO_EXPAND : 0;
            collected = pp_collect_token(pp, invocation, token, expansion_end);
            if (opening && invocation->builtin && collected == PP_COLLECTED_MORE)
            {
                collected = pp_collect_written_header(pp, invocation, context);
            }
        }
    }
    else if (context->barrier)
    {
        unterminated = true;
    }
    else
    {
        pp_pop_context(pp);
    }
    if (collected == PP_COLLECTED_STREAM)
    {
        pp_start_stream(pp);
    }
    else if (collected != PP_COLLECTED_MORE || unterminated)
    {
        pp_close_invocation(pp, collected == PP_COLLECTED_CLOSED);
    }
}

// Invokes a function-like macro whose name was just read and whose '(' is next: collects its arguments, then expands
// and substitutes them. When they are all in the top context, pp_collect_arguments collects them at once; else the main
// loop collects them a token at a time (pp_collect_step), as it does for the scalar kernels.
// pp_substitute for a BUSTER_PP_MACRO_SIMPLE macro whose arguments expand to themselves: its body's runs of tokens, and
// each parameter's raw argument, with the leading spaces pp_substitute gives them, into pp->expansions from `*begin`.
static void pp_substitute_simple(Preprocessor* pp, const PPInvocation* invocation, uint32_t* begin, uint8_t* trailing)
{
    const uint32_t saved_history = pp->history_current;
    pp->history_current = invocation->history;
    const PPMacro* macro = &pp->macros[invocation->macro];
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = macro->item_count;
    const PPFileData* data = &pp->datas[macro->data];
    PPTokens* out = &pp->expansions;
    *begin = out->count;
    bool next_space = false;
    for (uint32_t i = 0; i < count && !pp->fatal; i += 1)
    {
        const PPItem* item = &items[i];
        pp->history_anchor = macro->base + data->starts[item->token];
        pp->history_spelling = pp->history_anchor;
        next_space |= (item->flags & BUSTER_PP_ITEM_SPACE) != 0;
        if (item->kind == PP_ITEM_TOKEN)
        {
            // A run of consecutive body tokens with their own leading spaces (pp_compile_macro's suffix counts).
            const uint32_t at = out->count;
            const uint32_t last = i + item->parameter;
            pp_append_file_tokens(pp, out, data, macro->base, item->token, items[last].token + 1, true);
            i = last;
            if (at < out->count)
            {
                pp_set_space(out, at, next_space);
            }
            next_space = false;
        }
        else
        {
            const PPArgument* argument = &pp->argument_records[invocation->arguments + item->parameter];
            if (argument->raw_begin != argument->raw_end)
            {
                pp_append_tokens(pp, &pp->arguments, argument->raw_begin, argument->raw_end, true, next_space ? BUSTER_PP_SPACE : 0);
                next_space = false;
            }
        }
    }
    *trailing = next_space ? BUSTER_PP_SPACE : 0;
    pp->history_current = saved_history;
}

// Whether an enabled object-like macro's expansion goes straight into the target (pp_expand_object's `direct`): it isn't
// a builtin, its body has tokens and no ##, and none of them names a macro.
static bool pp_direct_object(const Preprocessor* pp, const PPMacro* macro)
{
    const PPFileData* body = &pp->datas[macro->data];
    bool result = (macro->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN | BUSTER_PP_MACRO_PASTES)) == 0 && macro->body_begin < macro->body_end;
    for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
    {
        result = (body->kinds[t] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro_vector(pp, body->values[t]);
    }
    return result;
}

// Begins a run of the output's history at token `token` if `origin` isn't the origin of the token before (`*previous`):
// the expansion's first run as pp_history_change begins it (it may take the place of the run before, at the same
// token), and each later one appended, with room that pp_emit_simple made for every token of the expansion.
BUSTER_INLINE void pp_emit_origin_at(Preprocessor* pp, uint32_t token, uint32_t origin, uint32_t* previous)
{
    if (origin != *previous && *previous == BUSTER_PP_NONE)
    {
        pp_history_change(pp, token, origin);
    }
    else if (origin != *previous)
    {
        pp->history_runs[pp->history_run_count] = (PPHistoryRun){ .token = token, .origin = origin };
        pp->history_run_count += 1;
    }
    *previous = origin;
}

BUSTER_INLINE void pp_emit_origin(Preprocessor* pp, uint32_t origin, uint32_t* previous)
{
    pp_emit_origin_at(pp, pp->output.count, origin, previous);
}

// Tokens [begin, end) of a prepared file read in the range at `base` into the output, as a buffer holds a macro's body
// (pp_store_file_tokens with BUSTER_PP_FROM_MACRO), 64 at a time, of origin `origin`. (A token at a time was slower even
// for runs of one or two tokens.)
static void pp_emit_body_run(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t begin, uint32_t end, uint32_t origin, uint32_t* previous)
{
    PPTokens* out = &pp->output;
    if (begin < end)
    {
        pp_emit_origin(pp, origin, previous);
    }
    uint32_t count = out->count;
    for (uint32_t p = begin; p < end; p += 64)
    {
        const uint32_t available = end - p < 64 ? end - p : 64;
        pp_store_file_tokens(out, count, data, base, p, available, available, _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, available), data->kinds + p), true);
        count += available;
    }
    out->count = count;
}

// An argument's tokens [begin, end) of pp->arguments into the output, as pp_append_tokens gives them (a line's start a
// leading space, BUSTER_PP_FROM_MACRO) and the buffer's run copies them (a name of a macro painted: the argument names
// no enabled one), 64 at a time; then the history: a substitution per run of tokens of one origin (pp_history_argument),
// whose parameter is pp->history_anchor, and a run of the output's history where each begins.
static void pp_emit_argument_run(Preprocessor* pp, uint32_t begin, uint32_t end, uint32_t* previous)
{
    PPTokens* out = &pp->output;
    const PPTokens* tokens = &pp->arguments;
    const uint32_t at = out->count;
    uint32_t count = at;
    // An argument of a token or two a token at a time, as pp_append_tokens copies it.
    for (uint32_t a = begin; end - begin <= 2 && a < end; a += 1)
    {
        const uint8_t flags = tokens->flags[a];
        const uint8_t kind = tokens->kinds[a];
        const bool paint = kind == C_TOKEN_RAW_IDENTIFIER && !(flags & BUSTER_PP_NO_EXPAND) && pp_is_macro_vector(pp, tokens->values[a]);
        out->kinds[count] = kind;
        out->flags[count] = (uint8_t)((flags & ~BUSTER_PP_START_OF_LINE) | ((flags & BUSTER_PP_START_OF_LINE) ? BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE : 0) |
                                      BUSTER_PP_FROM_MACRO | (paint ? BUSTER_PP_NO_EXPAND : 0));
        out->values[count] = tokens->values[a];
        out->locations[count] = tokens->locations[a];
        count += 1;
    }
    for (uint32_t p = begin; end - begin > 2 && p < end; p += 64)
    {
        const uint32_t available = end - p < 64 ? end - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, tokens->kinds + p);
        const __m512i flags = _mm512_maskz_loadu_epi8(valid, tokens->flags + p);
        const __mmask64 lines = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
        __m512i moved = _mm512_or_si512(_mm512_andnot_si512(_mm512_set1_epi8(BUSTER_PP_START_OF_LINE), flags), _mm512_set1_epi8(BUSTER_PP_FROM_MACRO));
        moved = _mm512_mask_mov_epi8(moved, lines, _mm512_or_si512(moved, _mm512_set1_epi8(BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE)));
        // Names of macros not painted yet: disabled ones (pp_needs_expansion found no enabled one), which the buffer's
        // run paints.
        uint64_t paint = 0;
        const uint64_t named = _mm512_mask_cmpeq_epi8_mask(_mm512_mask_testn_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND)), kinds,
                                                           _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        for (uint64_t bits = named; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            paint |= (uint64_t)pp_is_macro_vector(pp, tokens->values[p + lane]) << lane;
        }
        moved = _mm512_mask_mov_epi8(moved, paint, _mm512_or_si512(moved, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND)));
        _mm512_storeu_si512(out->kinds + count, kinds);
        _mm512_storeu_si512(out->flags + count, moved);
        for (uint32_t group = 0; group * 16 < available; group += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
            _mm512_storeu_si512(out->values + count + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->values + p + group * 16));
            _mm512_storeu_si512(out->locations + count + group * 16, _mm512_maskz_loadu_epi32(lanes, tokens->locations + p + group * 16));
        }
        count += available;
    }
    out->count = count;
    for (uint32_t a = begin; !pp->options.no_history && a < end;)
    {
        // The run of one origin from `a`, 16 origins at a time.
        const uint32_t source = tokens->origins[a];
        uint32_t stop = end;
        for (uint32_t o = a + 1; stop == end && o < end; o += 16)
        {
            const __mmask16 lanes = (__mmask16)_bzhi_u32(~0u, end - o < 16 ? end - o : 16);
            const __mmask16 other = _mm512_mask_cmpneq_epi32_mask(lanes, _mm512_maskz_loadu_epi32(lanes, tokens->origins + o), _mm512_set1_epi32((int)source));
            stop = other != 0 ? o + (uint32_t)__builtin_ctz(other) : stop;
        }
        // pp_history_substitution's record, with room that pp_emit_simple made for one per token.
        pp->substitution_count += 1;
        pp->substitutions[pp->substitution_count - 1] = (PPSubstitution){
            .expansion = pp->history_current,
            .source = source,
            .parameter = pp->history_anchor,
        };
        pp_emit_origin_at(pp, at + a - begin, BUSTER_PP_ORIGIN_ARGUMENT | pp->substitution_count, previous);
        a = stop;
    }
}

// An argument's tokens [begin, end) of a file context's text (`file`, read in the range at `file_base`) into the output,
// as pp_collect_arguments' copy into pp->arguments and pp_emit_argument_run would give them: a line's start a leading
// space, BUSTER_PP_FROM_MACRO, and a name of a macro painted (the argument names no enabled one), 64 at a time; then the
// history: the file's tokens have no origin, so one substitution (pp_history_argument's), whose parameter is
// pp->history_anchor, and a run of the output's history where it begins.
static void pp_emit_file_argument_run(Preprocessor* pp, const PPFileData* file, uint32_t file_base, uint32_t begin, uint32_t end, uint32_t* previous)
{
    PPTokens* out = &pp->output;
    const uint32_t at = out->count;
    uint32_t count = at;
    const __m512i bases = _mm512_set1_epi32((int)file_base);
    for (uint32_t p = begin; p < end; p += 64)
    {
        const uint32_t available = end - p < 64 ? end - p : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, file->kinds + p);
        const __m512i plain = _mm512_and_si512(kinds, _mm512_set1_epi8(0x7F));
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, plain, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        // The leading-space bits of tokens p to p + 63 (pp_store_file_tokens'), and the line starts, which become spaces.
        const uint32_t shift = p % 64;
        uint64_t space = file->space[p / 64] >> shift;
        if (shift != 0)
        {
            space |= file->space[p / 64 + 1] << (64 - shift);
        }
        const uint64_t lines = _mm512_movepi8_mask(kinds) & valid;
        // Names of macros: disabled ones (the argument names no enabled one), which the buffer's run paints.
        uint64_t paint = 0;
        for (uint64_t bits = identifiers; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
            paint |= (uint64_t)pp_is_macro_vector(pp, file->values[p + lane]) << lane;
        }
        __m512i flags = _mm512_maskz_mov_epi8(space | lines, _mm512_set1_epi8(BUSTER_PP_SPACE));
        flags = _mm512_or_si512(flags, _mm512_maskz_mov_epi8(lines, _mm512_set1_epi8(BUSTER_PP_LINE_SPACE)));
        flags = _mm512_or_si512(flags, _mm512_maskz_mov_epi8(paint, _mm512_set1_epi8(BUSTER_PP_NO_EXPAND)));
        flags = _mm512_or_si512(flags, _mm512_set1_epi8(BUSTER_PP_FROM_MACRO));
        _mm512_storeu_si512(out->kinds + count, plain);
        _mm512_storeu_si512(out->flags + count, flags);
        for (uint32_t group = 0; group * 16 < available; group += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (group * 16));
            const __mmask16 others = (__mmask16)~(identifiers >> (group * 16));
            const __m512i values = _mm512_maskz_loadu_epi32(lanes, file->values + p + group * 16);
            const __m512i starts = _mm512_maskz_loadu_epi32(lanes, file->starts + p + group * 16);
            _mm512_storeu_si512(out->values + count + group * 16, _mm512_mask_add_epi32(values, others, values, bases));
            _mm512_storeu_si512(out->locations + count + group * 16, _mm512_add_epi32(starts, bases));
        }
        count += available;
    }
    out->count = count;
    if (!pp->options.no_history)
    {
        // pp_history_substitution's record, with room that pp_emit_simple_plan made for one per token.
        pp->substitution_count += 1;
        pp->substitutions[pp->substitution_count - 1] = (PPSubstitution){
            .expansion = pp->history_current,
            .source = 0,
            .parameter = pp->history_anchor,
        };
        pp_emit_origin_at(pp, at, BUSTER_PP_ORIGIN_ARGUMENT | pp->substitution_count, previous);
    }
}

// Sets the flags of output token `at` as a buffer's token is given its run's first space, or the expansion's first token
// its name's flags: the bits in `clear` cleared and those in `set` set.
BUSTER_INLINE void pp_emit_flags(Preprocessor* pp, uint32_t at, uint8_t clear, uint8_t set)
{
    pp->output.flags[at] = (uint8_t)((pp->output.flags[at] & ~clear) | set);
}

// The names part of pp_emit_simple_plan, which needs no argument: whether a simple macro's body names no macro but disabled
// ones and object-like ones that go straight into the target, with the tokens going to no capture and -E's markers not
// needing their starts. Leaves the names in `plan`, and its size the tokens those bodies add.
static bool pp_emit_simple_names(Preprocessor* pp, PPMacroId id, PPEmitPlan* plan)
{
    const PPMacro* macro = &pp->macros[id];
    const PPFileData* data = &pp->datas[macro->data];
    bool result = pp->capture_count == 0 && !pp->options.expansion_starts;
    // The body's identifiers (pp_compile_macro lists them): a name of a macro is painted or expands straight in.
    PPEmitName* names = plan->names;
    uint32_t name_count = 0;
    uint32_t size = 0;
    const PPItem* identifiers = pp->items + macro->items_begin + macro->item_count + macro->order_count;
    for (uint32_t k = 0; result && k < macro->name_count; k += 1)
    {
        const uint32_t t = identifiers[k].token;
        if (pp_is_macro_vector(pp, data->values[t]))
        {
            const PPMacroId nested_id = pp->macro_of[data->values[t]];
            const PPMacro* nested = &pp->macros[nested_id];
            const bool expands = nested_id != id && nested->disabled == 0;
            result = name_count < BUSTER_PP_EMIT_NAMES && (!expands || pp_direct_object(pp, nested));
            size += expands && result ? nested->body_end - nested->body_begin - 1 : 0;
            names[name_count < BUSTER_PP_EMIT_NAMES ? name_count : 0] = (PPEmitName){ .token = t, .expands = expands ? nested_id : 0 };
            name_count += result;
        }
    }
    plan->name_count = name_count;
    plan->size = size;
    return result;
}

// The rest of pp_emit_simple_plan, once pp_emit_simple_names has planned the names: the expansion's size, from its body's
// runs and its arguments' records (in pp->arguments or in a file's tokens alike), and its room, with room for its
// history made.
static bool pp_emit_simple_size(Preprocessor* pp, const PPInvocation* invocation, PPEmitPlan* plan)
{
    const PPMacro* macro = &pp->macros[invocation->macro];
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = macro->item_count;
    const PPTokens* out = &pp->output;
    uint64_t size = plan->size;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const PPItem* item = &items[i];
        if (item->kind == PP_ITEM_TOKEN)
        {
            size += item->parameter + 1u;
            i += item->parameter;
        }
        else
        {
            const PPArgument* argument = &pp->argument_records[invocation->arguments + item->parameter];
            size += argument->raw_end - argument->raw_begin;
        }
    }
    // The buffer's runs check for room for BUSTER_PP_TOKEN_SLACK tokens: so does this.
    bool result = size != 0 && out->count + size + BUSTER_PP_TOKEN_SLACK <= out->capacity;
    // Room for a run of the output's history and a substitution per token, which pp_emit_origin_at and
    // pp_emit_argument_run append without checking. Where a table can't have it (past its limit, or out of memory, both
    // fatal) the main loop's path takes the expansion, recording what it can as it does at that point.
    const bool history = !pp->options.no_history;
    result = result && (!history || (pp_history_room(pp, (void**)&pp->history_runs, &pp->history_run_capacity, pp->history_run_count + (uint32_t)size + 2,
                                                     (BUSTER_PP_MAX_OUTPUT_TOKENS >> pp->capacity_shift) + 1, sizeof(PPHistoryRun), BUSTER_ARENA_SMALL_BYTES) &&
                                     pp_history_room(pp, (void**)&pp->substitutions, &pp->substitution_capacity, pp->substitution_count + (uint32_t)size,
                                                     (BUSTER_PP_MAX_HISTORY >> pp->capacity_shift), sizeof(PPSubstitution), BUSTER_PP_TABLE_SMALL_BYTES)));
    plan->size = (uint32_t)size;
    return result;
}

// Whether a simple invocation's expansion (pp_finish_simple) can be written straight into the output (pp_emit_simple):
// its names (pp_emit_simple_names) and its size and room (pp_emit_simple_size). Leaves both in `plan`, and makes room for
// its history; changes nothing else.
static bool pp_emit_simple_plan(Preprocessor* pp, const PPInvocation* invocation, PPEmitPlan* plan)
{
    return pp_emit_simple_names(pp, invocation->macro, plan) && pp_emit_simple_size(pp, invocation, plan);
}

// Writes a simple invocation's expansion that pp_emit_simple_plan planned into the output at once: what
// pp_substitute_simple, the push of its buffer (with its macro disabled), the main loop's steps over the buffer and its
// pop would write (pp_step_buffer, whose runs paint the disabled macros' names, and pp_expand_object, which writes the
// others' bodies), with the same history and statistics. Its runs of body tokens and its arguments go 64 at a time, and
// then each one's first token gets the flags the substitution gives it. Its arguments' records are in pp->arguments, or
// (`file` nonzero) in the file's tokens, read in the range at `file_base`, whose invocation pp_invoke_file read without
// copying them. Leaves its trailing space in `*trailing`.
static void pp_emit_simple_write(Preprocessor* pp, const PPInvocation* invocation, const PPEmitPlan* plan, const PPFileData* file, uint32_t file_base, uint8_t* trailing)
{
    const PPMacro* macro = &pp->macros[invocation->macro];
    const PPItem* items = pp->items + macro->items_begin;
    const uint32_t count = macro->item_count;
    const PPFileData* data = &pp->datas[macro->data];
    PPTokens* out = &pp->output;
    const PPEmitName* names = plan->names;
    const uint32_t name_count = plan->name_count;
    const uint32_t size = plan->size;
    const bool history = !pp->options.no_history;
    // The substitutions belong to this expansion, as in pp_substitute_simple.
    const uint32_t saved_history = pp->history_current;
    pp->history_current = invocation->history;
    const uint32_t begin = out->count;
    const uint8_t name_flags = invocation->name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE);
    const uint8_t name_clear = BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE;
    // Without history every origin is 0, so no run of it begins.
    uint32_t previous = history ? BUSTER_PP_NONE : 0;
    // The bodies written straight in, for the statistics; and whether the last thing written was one of them.
    uint32_t directs = 0;
    uint32_t direct_tokens = 0;
    bool after_direct = false;
    uint32_t name = 0;
    bool next_space = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const PPItem* item = &items[i];
        pp->history_anchor = macro->base + data->starts[item->token];
        pp->history_spelling = pp->history_anchor;
        next_space |= (item->flags & BUSTER_PP_ITEM_SPACE) != 0;
        if (item->kind == PP_ITEM_TOKEN)
        {
            const uint32_t last = i + item->parameter;
            const uint32_t end = items[last].token + 1;
            // The run's segments between the names that expand straight in, each name's flags as the buffer would
            // hold them (pp_append_file_tokens, the run's first space, the expansion's first token's name flags).
            uint32_t t = item->token;
            while (t < end)
            {
                const uint32_t stop = name < name_count && names[name].token < end ? names[name].token : end;
                const bool expands = stop < end && names[name].expands != 0;
                const uint32_t segment = expands ? stop : (stop < end ? stop + 1 : end);
                const uint32_t at = out->count;
                pp_emit_body_run(pp, data, macro->base, t, segment, invocation->history, &previous);
                if (t == item->token && at < out->count)
                {
                    pp_emit_flags(pp, at, BUSTER_PP_SPACE, next_space ? BUSTER_PP_SPACE : 0);
                }
                if (at == begin && at < out->count)
                {
                    pp_emit_flags(pp, at, name_clear, name_flags | BUSTER_PP_NAME_SPACE);
                }
                if (stop < end && !expands)
                {
                    // A painted name, the segment's last token.
                    pp_emit_flags(pp, out->count - 1, 0, BUSTER_PP_NO_EXPAND);
                }
                after_direct = after_direct && at == out->count;
                if (expands)
                {
                    uint8_t flags = (uint8_t)((pp_file_space(data, stop) ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO);
                    flags = stop == item->token ? (uint8_t)((flags & ~BUSTER_PP_SPACE) | (next_space ? BUSTER_PP_SPACE : 0)) : flags;
                    flags = out->count == begin ? (uint8_t)((flags & ~name_clear) | name_flags | BUSTER_PP_NAME_SPACE) : flags;
                    const PPMacro* nested = &pp->macros[names[name].expands];
                    const uint32_t expansion = history ? pp_history_expansion(pp, names[name].expands, macro->base + data->starts[stop], invocation->history) : 0;
                    const uint32_t at_body = out->count;
                    pp_emit_body_run(pp, &pp->datas[nested->data], nested->base, nested->body_begin, nested->body_end, expansion, &previous);
                    pp_emit_flags(pp, at_body, name_clear, (uint8_t)((flags & name_clear) | BUSTER_PP_NAME_SPACE));
                    directs += 1;
                    direct_tokens += nested->body_end - nested->body_begin;
                    after_direct = true;
                }
                name += stop < end;
                t = stop < end ? stop + 1 : end;
            }
            i = last;
            next_space = false;
        }
        else
        {
            const PPArgument* argument = &pp->argument_records[invocation->arguments + item->parameter];
            if (argument->raw_begin != argument->raw_end)
            {
                const uint32_t at = out->count;
                if (file != 0)
                {
                    pp_emit_file_argument_run(pp, file, file_base, argument->raw_begin, argument->raw_end, &previous);
                }
                else
                {
                    pp_emit_argument_run(pp, argument->raw_begin, argument->raw_end, &previous);
                }
                pp_emit_flags(pp, at, BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE, next_space ? BUSTER_PP_SPACE : 0);
                if (at == begin)
                {
                    pp_emit_flags(pp, at, name_clear, name_flags | BUSTER_PP_NAME_SPACE);
                }
                after_direct = false;
                next_space = false;
            }
        }
    }
    if (history)
    {
        pp_history_change(pp, out->count, 0);
        // The buffer's origins would have reached its whole substituted expansion (pp_history_fill's peak).
        const uint32_t peak = pp->expansions.count + (uint32_t)size - (direct_tokens - directs);
        pp->origin_peak[2] = pp->origin_peak[2] < peak ? peak : pp->origin_peak[2];
    }
    *trailing = next_space ? BUSTER_PP_SPACE : 0;
    if (pp->options.timing)
    {
        // Each body written straight in ends a run (an empty one before it, too), and one more run copies the
        // tokens after the last one, unless it ends the buffer.
        const uint32_t run_tokens = out->count - begin - direct_tokens;
        pp->statistics.buffer_runs += directs + !after_direct;
        pp->statistics.buffer_run_tokens += run_tokens;
        pp->statistics.final_buffer_run_tokens += run_tokens;
        pp->statistics.direct_expansions += directs;
        pp->statistics.final_direct_object_tokens += direct_tokens;
    }
    pp->history_current = saved_history;
}

// A simple invocation's expansion written straight into the output (pp_emit_simple_plan, pp_emit_simple_write), its
// arguments in pp->arguments. Returns false, having changed nothing but its history's room, when it can't be.
// An argument's tokens [begin, end), at most BUSTER_PP_TEMPLATE_TOKENS, as pp_append_tokens gives them from
// pp->arguments, or pp_emit_file_argument_run from a file context's text (`file`, read in the range at `file_base`): a
// line's start a leading space and BUSTER_PP_LINE_SPACE, and BUSTER_PP_FROM_MACRO; with `paint`, a name of a macro
// painted too, as pp_emit_argument_run and pp_emit_file_argument_run paint it (the argument names no enabled one). In
// lanes 0 to end - begin - 1 of each column.
BUSTER_INLINE void pp_template_argument(const Preprocessor* pp, const PPFileData* file, uint32_t file_base, uint32_t begin, uint32_t end, bool paint,
                                        __m128i* kinds, __m128i* flags, __m512i* values, __m512i* locations)
{
    const __mmask16 valid = (__mmask16)_bzhi_u32(0xFFFF, end - begin);
    uint32_t painted = 0;
    if (file != 0)
    {
        const __m128i raw = _mm_maskz_loadu_epi8(valid, file->kinds + begin);
        *kinds = _mm_and_si128(raw, _mm_set1_epi8(0x7F));
        const __mmask16 identifiers = _mm_mask_cmpeq_epi8_mask(valid, *kinds, _mm_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        const __mmask16 lines = (__mmask16)(_mm_movepi8_mask(raw) & valid);
        // The leading-space bits of tokens begin to begin + 15 (pp_store_file_tokens').
        const uint32_t shift = begin % 64;
        uint64_t space = file->space[begin / 64] >> shift;
        if (shift != 0)
        {
            space |= file->space[begin / 64 + 1] << (64 - shift);
        }
        for (uint32_t bits = paint ? identifiers : 0; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(bits);
            painted |= (uint32_t)pp_is_macro_vector(pp, file->values[begin + lane]) << lane;
        }
        __m128i made = _mm_maskz_mov_epi8((__mmask16)space | lines, _mm_set1_epi8(BUSTER_PP_SPACE));
        made = _mm_or_si128(made, _mm_maskz_mov_epi8(lines, _mm_set1_epi8(BUSTER_PP_LINE_SPACE)));
        *flags = _mm_or_si128(made, _mm_set1_epi8(BUSTER_PP_FROM_MACRO));
        const __m512i bases = _mm512_set1_epi32((int)file_base);
        const __m512i read = _mm512_maskz_loadu_epi32(valid, file->values + begin);
        *values = _mm512_mask_add_epi32(read, (__mmask16)~identifiers, read, bases);
        *locations = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, file->starts + begin), bases);
    }
    else
    {
        const PPTokens* tokens = &pp->arguments;
        *kinds = _mm_maskz_loadu_epi8(valid, tokens->kinds + begin);
        const __m128i read = _mm_maskz_loadu_epi8(valid, tokens->flags + begin);
        const __mmask16 lines = _mm_test_epi8_mask(read, _mm_set1_epi8(BUSTER_PP_START_OF_LINE));
        const __mmask16 named = _mm_mask_cmpeq_epi8_mask(_mm_mask_testn_epi8_mask(valid, read, _mm_set1_epi8(BUSTER_PP_NO_EXPAND)), *kinds,
                                                         _mm_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        for (uint32_t bits = paint ? named : 0; bits != 0; bits &= bits - 1)
        {
            const uint32_t lane = (uint32_t)__builtin_ctz(bits);
            painted |= (uint32_t)pp_is_macro_vector(pp, tokens->values[begin + lane]) << lane;
        }
        __m128i moved = _mm_or_si128(_mm_andnot_si128(_mm_set1_epi8(BUSTER_PP_START_OF_LINE), read), _mm_set1_epi8(BUSTER_PP_FROM_MACRO));
        *flags = _mm_mask_mov_epi8(moved, lines, _mm_or_si128(moved, _mm_set1_epi8(BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE)));
        *values = _mm512_maskz_loadu_epi32(valid, tokens->values + begin);
        *locations = _mm512_maskz_loadu_epi32(valid, tokens->locations + begin);
    }
    *flags = _mm_mask_mov_epi8(*flags, (__mmask16)painted, _mm_or_si128(*flags, _mm_set1_epi8(BUSTER_PP_NO_EXPAND)));
}

// The body of an object-like macro whose expansion goes straight into the target (pp_direct_object), of at most
// BUSTER_PP_TEMPLATE_TOKENS tokens, as pp_emit_body_run writes it (pp_store_file_tokens from a macro): in lanes 0 to
// its length - 1 of each column.
BUSTER_INLINE void pp_template_body(const Preprocessor* pp, const PPMacro* macro, __m128i* kinds, __m128i* flags, __m512i* values, __m512i* locations)
{
    const PPFileData* data = &pp->datas[macro->data];
    const uint32_t begin = macro->body_begin;
    const __mmask16 valid = (__mmask16)_bzhi_u32(0xFFFF, macro->body_end - begin);
    *kinds = _mm_and_si128(_mm_maskz_loadu_epi8(valid, data->kinds + begin), _mm_set1_epi8(0x7F));
    const __mmask16 identifiers = _mm_mask_cmpeq_epi8_mask(valid, *kinds, _mm_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
    const uint32_t shift = begin % 64;
    uint64_t space = data->space[begin / 64] >> shift;
    if (shift != 0)
    {
        space |= data->space[begin / 64 + 1] << (64 - shift);
    }
    *flags = _mm_or_si128(_mm_maskz_mov_epi8((__mmask16)space, _mm_set1_epi8(BUSTER_PP_SPACE)), _mm_set1_epi8(BUSTER_PP_FROM_MACRO));
    const __m512i bases = _mm512_set1_epi32((int)macro->base);
    const __m512i read = _mm512_maskz_loadu_epi32(valid, data->values + begin);
    *values = _mm512_mask_add_epi32(read, (__mmask16)~identifiers, read, bases);
    *locations = _mm512_add_epi32(_mm512_maskz_loadu_epi32(valid, data->starts + begin), bases);
}

// The template of a macro, or null (pp_compile_macro makes them for the vectorized runs that record no history).
BUSTER_INLINE PPTemplate* pp_template_of(Preprocessor* pp, const PPMacro* macro)
{
    return macro->template_index != 0 ? &pp->templates[macro->template_index - 1] : 0;
}

// pp_template_names' search: the template's names of macros as pp_emit_simple_names finds them, kept in the template.
static void pp_template_find_names(Preprocessor* pp, PPMacroId id, PPTemplate* template)
{
    uint32_t painted = 0;
    uint32_t expanding = 0;
    uint32_t extra = 0;
    bool direct = true;
    bool disabled = false;
    for (uint32_t bits = template->names; bits != 0; bits &= bits - 1)
    {
        const uint32_t j = (uint32_t)__builtin_ctz(bits);
        const IdentId value = template->values[j];
        if (pp_is_macro_vector(pp, value))
        {
            const PPMacroId nested_id = pp->macro_of[value];
            const PPMacro* nested = &pp->macros[nested_id];
            const bool expands = nested_id != id && nested->disabled == 0;
            disabled |= nested_id != id && nested->disabled != 0;
            direct = direct && (!expands || pp_direct_object(pp, nested));
            painted |= expands ? 0 : 1u << j;
            expanding |= expands ? 1u << j : 0;
            template->nested[j] = expands ? nested_id : 0;
            extra += expands ? nested->body_end - nested->body_begin - 1 : 0;
        }
    }
    template->painted = (uint16_t)painted;
    template->expanding = (uint16_t)expanding;
    template->extra = extra;
    template->direct = direct;
    // A name painted because its macro is disabled is painted only while that expansion is under way.
    template->epoch = disabled ? 0 : pp->macro_epoch + 1;
}

// pp_emit_simple_names for a macro with a template, which keeps what it found (painted names, names whose bodies go
// straight in, and the tokens those add) while the macros' table is the one it was found in and those bodies are still
// enabled; found again otherwise (pp_template_find_names). Returns whether every name of a macro is painted or goes
// straight in, with no capture open and no -E markers' starts, as pp_emit_simple_names does.
BUSTER_INLINE bool pp_template_names(Preprocessor* pp, PPMacroId id, PPTemplate* template)
{
    bool kept = template->epoch == pp->macro_epoch + 1;
    for (uint32_t bits = kept ? template->expanding : 0; bits != 0; bits &= bits - 1)
    {
        kept = kept && pp->macros[template->nested[__builtin_ctz(bits)]].disabled == 0;
    }
    if (!kept)
    {
        pp_template_find_names(pp, id, template);
    }
    return template->direct && pp->capture_count == 0 && !pp->options.expansion_starts;
}

// The tokens a simple invocation's expansion has from its macro's template: its body's tokens but the parameters, each
// parameter's argument (the records at `arguments`), and with `direct` (an expansion written straight in, after
// pp_template_names) the tokens the bodies of names that go straight in add. Saturates past UINT32_MAX / 2.
BUSTER_INLINE uint32_t pp_template_size(const Preprocessor* pp, const PPTemplate* template, uint32_t arguments, bool direct)
{
    uint64_t size = (uint64_t)template->count - (uint32_t)__builtin_popcount(template->parameter_mask) + (direct ? template->extra : 0);
    for (uint32_t bits = template->parameter_mask; bits != 0; bits &= bits - 1)
    {
        const PPArgument* argument = &pp->argument_records[arguments + template->parameters[__builtin_ctz(bits)]];
        size += argument->raw_end - argument->raw_begin;
    }
    return size < UINT32_MAX / 2 ? (uint32_t)size : UINT32_MAX / 2;
}

// Item j of a template with ## and # in an invocation, as pp_substitute reads an operand of ##: a body token
// (pp_body_token's), or a parameter's argument as written, with its first token (pp_stack_token's of pp->arguments, or
// pp_file_token's of a file context's text, `file` read in the range at `file_base`). The first `given` records are
// the invocation's; a variadic argument past them was omitted, and is empty. Returns how many tokens the item has.
BUSTER_INLINE uint32_t pp_template_operand(const Preprocessor* pp, const PPTemplate* template, uint32_t j, const PPArgument* records, uint32_t given,
                                           const PPFileData* file, uint32_t file_base, PPToken* token)
{
    uint32_t result = 1;
    if (((template->parameter_mask >> j) & 1) != 0)
    {
        const uint32_t parameter = template->parameters[j];
        const PPArgument argument = parameter < given ? records[parameter] : (PPArgument){ 0 };
        result = argument.raw_end - argument.raw_begin;
        if (result != 0)
        {
            *token = file != 0 ? pp_file_token(file, file_base, argument.raw_begin) : pp_stack_token(&pp->arguments, argument.raw_begin);
        }
    }
    else
    {
        *token = (PPToken){
            .kind = template->kinds[j],
            .flags = (uint8_t)((((template->spaces >> j) & 1) != 0 ? BUSTER_PP_SPACE : 0) | BUSTER_PP_FROM_MACRO),
            .value = template->values[j],
            .location = template->locations[j],
        };
    }
    return result;
}

// A name an invocation of a template with ## and # makes, in lane j, if it is a macro's: one that expands (but the
// invoked macro, which the rescan of its expansion has disabled) makes the main loop read the expansion again, and any
// other is painted where the expansion goes straight in, as that rescan would paint it.
BUSTER_INLINE void pp_template_name(const Preprocessor* pp, PPMacroId id, IdentId value, uint32_t j, PPOperators* operators)
{
    if (value != 0 && pp_is_macro_vector(pp, value))
    {
        const PPMacroId named = pp->macro_of[value];
        const bool expands = named != id && pp->macros[named].disabled == 0;
        operators->rescan |= expands;
        operators->painted |= (uint16_t)(expands ? 0 : 1u << j);
    }
}

// Whether `length` bytes are ASCII letters, digits and '_', which an identifier or a pp-number may join.
BUSTER_INLINE bool pp_template_spelled(const uint8_t* bytes, uint32_t length)
{
    const __mmask64 valid = _bzhi_u64(~0ull, length < 64 ? length : 64);
    const __m512i c = _mm512_maskz_loadu_epi8(valid, bytes);
    const __mmask64 letters = _mm512_cmplt_epu8_mask(_mm512_sub_epi8(_mm512_or_si512(c, _mm512_set1_epi8(0x20)), _mm512_set1_epi8('a')), _mm512_set1_epi8(26));
    const __mmask64 digits = _mm512_cmplt_epu8_mask(_mm512_sub_epi8(c, _mm512_set1_epi8('0')), _mm512_set1_epi8(10));
    const __mmask64 underscores = _mm512_cmpeq_epi8_mask(c, _mm512_set1_epi8('_'));
    return length <= 64 && ((letters | digits | underscores) & valid) == valid;
}

// Whether an invocation of a template with ## and # takes the templates' path (pp_template_make, then
// pp_template_write), and what it makes, changing nothing: each # a string, from its argument in pp->arguments (`file`
// null); and each chain of pastes the token of its one operand with a token, or, where two operands or more have one,
// the token pp_paste makes of them one after another, when they are identifiers and numbers spelled in at most 64
// ASCII letters, digits and '_', an identifier or a number first: each paste then makes an identifier (pp_paste's own
// test) or a number (one pp-number). An argument next to ## has at most one token. A chain's token takes the leading
// space pending at its first operand (pp_template_write gives it), and its first token's other flags: as
// pp_append_tokens places an argument's, as pp_substitute moves a token past an empty operand before it, or, from a
// paste, the line start of what it was pasted onto. Notes the expansion's size, and the names made of macros that
// expand and that don't (pp_template_name).
static bool pp_template_operators(const Preprocessor* pp, PPMacroId id, const PPTemplate* template, const PPArgument* records, uint32_t given,
                                  const PPFileData* file, uint32_t file_base, PPOperators* operators)
{
#if __has_feature(memory_sanitizer)
    // pp_template_write's permutes pick their lanes from whole vectors of these columns, of which MSan's model reads
    // every lane: those no token fills are zero there.
    memset(operators, 0, sizeof(*operators));
#endif
    bool result = file == 0 || template->strings == 0;
    uint32_t size = 0;
    uint32_t joined = 0;
    operators->made = 0;
    operators->pasted = 0;
    operators->operands = 0;
    operators->painted = 0;
    operators->rescan = false;
    for (uint32_t j = 0; result && j < template->count;)
    {
        const uint32_t bit = 1u << j;
        if ((template->chains & bit) != 0)
        {
            // The chain's operands: its first item, and the item after each ## that follows.
            uint32_t last = j;
            while (((template->pastes >> (last + 1)) & 1) != 0)
            {
                last += 2;
            }
            PPToken tokens[BUSTER_PP_TEMPLATE_TOKENS / 2];
            uint32_t items[BUSTER_PP_TEMPLATE_TOKENS / 2];
            uint32_t count = 0;
            for (uint32_t k = j; result && k <= last; k += 2)
            {
                const uint32_t length = pp_template_operand(pp, template, k, records, given, file, file_base, &tokens[count]);
                result = length <= 1;
                items[count] = k;
                operators->operands |= (uint16_t)(length == 1 ? 1u << k : 0);
                count += length == 1;
            }
            // The first operand with a token, read only when one has: a chain whose operands are all empty writes none.
            const PPToken first = count != 0 ? tokens[0] : (PPToken){ 0 };
            const bool name = count != 0 && first.kind == C_TOKEN_RAW_IDENTIFIER;
            if (result && count == 1)
            {
                // Placed as the chain's first item (an argument's token as pp_append_tokens places it, or a body token),
                // or moved past the empty operands before it.
                const bool argument = ((template->parameter_mask >> items[0]) & 1) != 0;
                const uint8_t placed = argument ? (uint8_t)(first.flags & ~(BUSTER_PP_START_OF_LINE | BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE)) : 0;
                const uint8_t moved = (uint8_t)(first.flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE));
                operators->kinds[j] = first.kind;
                operators->flags[j] = (uint8_t)((items[0] == j ? placed : moved) | BUSTER_PP_FROM_MACRO);
                operators->values[j] = first.value;
                operators->locations[j] = first.location;
                if (name && !(first.flags & BUSTER_PP_NO_EXPAND))
                {
                    pp_template_name(pp, id, first.value, j, operators);
                }
            }
            else if (result && count >= 2)
            {
                // The operands' spellings, joined.
                const uint32_t begin = joined;
                for (uint32_t k = 0; result && k < count; k += 1)
                {
                    const PPToken token = tokens[k];
                    const bool number = token.kind == C_TOKEN_NUMERIC_CONSTANT || token.kind == BUSTER_PP_KIND_INTEGER;
                    result = (token.kind == C_TOKEN_RAW_IDENTIFIER || number) && (token.kind != C_TOKEN_NUMERIC_CONSTANT || token.value - token.location <= 64);
                    uint8_t buffer[64];
                    const uint8_t* spelling = buffer;
                    const uint32_t length = result ? pp_spelling((Preprocessor*)pp, token.kind, token.value, token.location, buffer, &spelling) : 0;
                    result = result && joined + length <= BUSTER_PP_TEMPLATE_JOINED_BYTES && pp_template_spelled(spelling, length);
                    if (result)
                    {
                        memcpy(operators->joined + joined, spelling, length);
                        joined += length;
                        operators->ends[items[k]] = (uint16_t)joined;
                    }
                }
                // A placed or moved first operand has lost its line start before pp_paste reads it.
                operators->kinds[j] = name ? C_TOKEN_RAW_IDENTIFIER : C_TOKEN_NUMERIC_CONSTANT;
                operators->flags[j] = BUSTER_PP_FROM_MACRO;
                operators->locations[j] = first.location;
                operators->begins[j] = (uint16_t)begin;
                operators->pasted |= (uint16_t)bit;
                if (result && name)
                {
                    pp_template_name(pp, id, intern_find(pp->table, operators->joined + begin, joined - begin), j, operators);
                }
            }
            operators->made |= (uint16_t)(count != 0 ? bit : 0);
            size += count != 0;
            j = last + 1;
        }
        else if ((template->strings & bit) != 0)
        {
            operators->kinds[j] = C_TOKEN_STRING_LITERAL;
            operators->flags[j] = BUSTER_PP_FROM_MACRO;
            operators->made |= (uint16_t)bit;
            size += 1;
            j += 1;
        }
        else if ((template->parameter_mask & bit) != 0)
        {
            const uint32_t parameter = template->parameters[j];
            size += parameter < given ? records[parameter].raw_end - records[parameter].raw_begin : 0;
            j += 1;
        }
        else
        {
            size += 1;
            j += 1;
        }
    }
    operators->size = size;
    return result;
}

// Makes what pp_template_operators found an invocation of a template with ## and # makes, in the order of its items,
// as pp_substitute makes them: each # its string (pp_stringize, of its argument in pp->arguments), and each paste of a
// chain, after its first operand with a token, its spelling (the chain's joined spellings to the end of its right
// operand's) in the scratch text and, for a name, in the table, with the error a poisoned name gets and the
// statistics, as pp_paste makes it.
static void pp_template_make(Preprocessor* pp, const PPTemplate* template, const PPArgument* records, uint32_t given, PPOperators* operators)
{
    for (uint32_t bits = operators->pasted | (template->strings & operators->made); bits != 0; bits &= bits - 1)
    {
        const uint32_t j = (uint32_t)__builtin_ctz(bits);
        if (((template->strings >> j) & 1) != 0)
        {
            const uint32_t parameter = template->parameters[j];
            const uint32_t empty = pp->arguments.count;
            const uint32_t begin = parameter < given ? records[parameter].raw_begin : empty;
            const uint32_t end = parameter < given ? records[parameter].raw_end : empty;
            const PPToken string = pp_stringize(pp, &pp->arguments, begin, end, 0, template->locations[j]);
            operators->values[j] = string.value;
            operators->locations[j] = string.location;
        }
        else
        {
            const bool name = operators->kinds[j] == C_TOKEN_RAW_IDENTIFIER;
            const uint8_t* bytes = operators->joined + operators->begins[j];
            uint32_t left = operators->locations[j];
            uint32_t value = 0;
            // The chain's operands with a token after its first one: a paste each.
            uint32_t operands = operators->operands & ~_bzhi_u32(~0u, j);
            operands &= operands - 1;
            for (uint32_t k = j + 1; k < template->count && ((template->pastes >> k) & 1) != 0; k += 2)
            {
                if (((operands >> (k + 1)) & 1) != 0)
                {
                    const uint32_t length = operators->ends[k + 1] - operators->begins[j];
                    pp->statistics.pastes += pp->options.timing;
                    const uint32_t location = pp_scratch(pp, bytes, length);
                    value = name ? intern_find_or_add(pp->table, bytes, length) : location + length;
                    if (name)
                    {
                        pp_check_poison(pp, value, left);
                    }
                    left = location;
                }
            }
            operators->values[j] = value;
            operators->locations[j] = left;
        }
    }
}

// A simple invocation's expansion of at most BUSTER_PP_TEMPLATE_TOKENS tokens from its macro's template, built in
// registers a column at a time and stored at once at the end of `out`, which has room for BUSTER_PP_TEMPLATE_TOKENS
// more tokens. Its segments go in order, each moved to its place in the expansion with a masked permute: a run of the
// template's tokens, an argument's tokens (pp_template_argument), the body of a name that goes straight in
// (pp_template_body), which ends the run around it, or (with `operators`, a template with ## and # that
// pp_template_operators planned and pp_template_make made) the token each chain of pastes and each # makes, if any,
// which takes the space pending before it as an argument's first token does, and after which a body token starts a
// run. Then each run's first token gets the space before it in place of its own and each argument's first its space
// alone.
//
// With `emit`, it is what pp_emit_simple_write writes into the output: the names `painted` painted, the bodies of the
// names `expanding` written in their places (with the flags pp_emit_simple_write gives their first tokens),
// arguments' names of macros painted, the made names the plan paints painted, the expansion's first token given its
// name's flags, and the statistics. Without it, it is what pp_substitute_simple (or pp_substitute) writes into the
// expansions for the main loop to read again: names left alone (`painted` and `expanding` 0), and the first token's
// flags left to the caller. Leaves the space after the last token in `*trailing`, and the history's anchors as the item
// it read last left them.
BUSTER_INLINE void pp_template_write(Preprocessor* pp, const PPInvocation* invocation, const PPTemplate* template, const PPFileData* file, uint32_t file_base,
                                     PPTokens* out, uint32_t painted, uint32_t expanding, bool emit, const PPOperators* operators, uint8_t* trailing)
{
    const __m128i lanes8 = _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m512i lanes32 = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m128i template_kinds = _mm_loadu_si128((const __m128i*)template->kinds);
    const __m128i template_flags = _mm_loadu_si128((const __m128i*)template->flags);
    const __m128i template_painted = _mm_mask_mov_epi8(template_flags, (__mmask16)painted, _mm_or_si128(template_flags, _mm_set1_epi8(BUSTER_PP_NO_EXPAND)));
    const __m512i template_values = _mm512_loadu_si512(template->values);
    const __m512i template_locations = _mm512_loadu_si512(template->locations);
    // The tokens the chains and #s made, in their first items' lanes.
    const uint32_t made_items = operators != 0 ? template->chains | template->strings : 0;
    __m128i made_kinds = _mm_setzero_si128();
    __m128i made_flags = _mm_setzero_si128();
    __m512i made_values = _mm512_setzero_si512();
    __m512i made_locations = _mm512_setzero_si512();
    if (operators != 0)
    {
        made_kinds = _mm_loadu_si128((const __m128i*)operators->kinds);
        made_flags = _mm_loadu_si128((const __m128i*)operators->flags);
        made_flags = _mm_mask_mov_epi8(made_flags, (__mmask16)(emit ? operators->painted : 0), _mm_or_si128(made_flags, _mm_set1_epi8(BUSTER_PP_NO_EXPAND)));
        made_values = _mm512_loadu_si512(operators->values);
        made_locations = _mm512_loadu_si512(operators->locations);
    }
    const uint8_t name_flags = invocation->name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE);
    const uint8_t name_clear = BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE;
    __m128i kinds = _mm_setzero_si128();
    __m128i flags = _mm_setzero_si128();
    __m512i values = _mm512_setzero_si512();
    __m512i locations = _mm512_setzero_si512();
    // Where a run of the template's tokens stops: each parameter, each name that goes straight in, each chain and #, and
    // the end.
    const uint32_t stops = template->parameter_mask | expanding | made_items | (1u << template->count);
    uint32_t at = 0;
    uint32_t runs = 0; // The expansion's lanes that start a run of body tokens,
    uint32_t starts = 0; // that start an argument's tokens,
    uint32_t spaced = 0; // and of those and the made tokens, the ones given a space;
    uint32_t named = 0; // those of a name's body's first tokens,
    uint32_t named_spaced = 0; // and of those, the ones whose name had a space.
    bool after_direct = false;
    bool next_space = false;
    bool run_first = true; // Whether a body token starts a run: the first one, and one after a parameter, a chain or a #.
    for (uint32_t j = 0; j < template->count;)
    {
        const bool parameter = ((template->parameter_mask >> j) & 1) != 0;
        const bool made = ((made_items >> j) & 1) != 0;
        // An item is read when it starts a run, is a parameter, or makes a token (the others are the run's, or a
        // chain's past its first operand, which follow a ## and have no space).
        next_space |= (run_first || parameter || made) && ((template->spaces >> j) & 1) != 0;
        const uint32_t first = at;
        uint32_t taken = 1;
        __mmask16 lanes = 0;
        __m128i segment_kinds;
        __m128i segment_flags;
        __m512i segment_values;
        __m512i segment_locations;
        int32_t from = 0; // The segment's first token's lane in its source.
        if (made)
        {
            // A chain's items, to its last operand, or a #.
            taken = ((template->chains >> j) & 1) != 0 ? (uint32_t)__builtin_ctz(template->ends >> j) + 1 : 1;
            if (((operators->made >> j) & 1) != 0)
            {
                segment_kinds = made_kinds;
                segment_flags = made_flags;
                segment_values = made_values;
                segment_locations = made_locations;
                from = (int32_t)j;
                lanes = (__mmask16)(1u << at);
                spaced |= next_space ? 1u << at : 0;
                next_space = false;
                after_direct = false;
                at += 1;
            }
            run_first = true;
        }
        else if (parameter)
        {
            const PPArgument* argument = &pp->argument_records[invocation->arguments + template->parameters[j]];
            const uint32_t count = argument->raw_end - argument->raw_begin;
            if (count != 0)
            {
                pp_template_argument(pp, file, file_base, argument->raw_begin, argument->raw_end, emit, &segment_kinds, &segment_flags, &segment_values,
                                     &segment_locations);
                lanes = (__mmask16)(_bzhi_u32(0xFFFF, at + count) & ~_bzhi_u32(0xFFFF, at));
                starts |= 1u << at;
                spaced |= next_space ? 1u << at : 0;
                next_space = false;
                after_direct = false;
                at += count;
            }
            run_first = true;
        }
        else if ((expanding >> j) & 1)
        {
            // A name whose body goes straight in: its first token takes the name's space (the space before it, if it
            // starts a run), and the expansion's first its name's flags.
            const PPMacro* body = &pp->macros[template->nested[j]];
            const uint32_t count = body->body_end - body->body_begin;
            pp_template_body(pp, body, &segment_kinds, &segment_flags, &segment_values, &segment_locations);
            const bool name_space = run_first ? next_space : (template->flags[j] & BUSTER_PP_SPACE) != 0;
            named |= 1u << at;
            named_spaced |= name_space ? 1u << at : 0;
            lanes = (__mmask16)(_bzhi_u32(0xFFFF, at + count) & ~_bzhi_u32(0xFFFF, at));
            next_space = run_first ? false : next_space;
            run_first = false;
            after_direct = true;
            at += count;
        }
        else
        {
            // A run of the template's tokens [j, j + taken).
            taken = (uint32_t)__builtin_ctz(stops >> j);
            segment_kinds = template_kinds;
            segment_flags = template_painted;
            segment_values = template_values;
            segment_locations = template_locations;
            from = (int32_t)j;
            lanes = (__mmask16)(_bzhi_u32(0xFFFF, at + taken) & ~_bzhi_u32(0xFFFF, at));
            runs |= run_first ? 1u << at : 0;
            spaced |= run_first && next_space ? 1u << at : 0;
            next_space = run_first ? false : next_space;
            run_first = false;
            after_direct = false;
            at += taken;
        }
        if (lanes != 0)
        {
            // The segment's tokens moved from lane `from` of their source to lane `first`.
            const __m128i index8 = _mm_add_epi8(lanes8, _mm_set1_epi8((char)(from - (int32_t)first)));
            const __m512i index32 = _mm512_add_epi32(lanes32, _mm512_set1_epi32(from - (int32_t)first));
            kinds = _mm_mask_permutexvar_epi8(kinds, lanes, index8, segment_kinds);
            flags = _mm_mask_permutexvar_epi8(flags, lanes, index8, segment_flags);
            values = _mm512_mask_permutexvar_epi32(values, lanes, index32, segment_values);
            locations = _mm512_mask_permutexvar_epi32(locations, lanes, index32, segment_locations);
        }
        j += taken;
    }
    flags = _mm_mask_mov_epi8(flags, (__mmask16)runs, _mm_andnot_si128(_mm_set1_epi8(BUSTER_PP_SPACE), flags));
    flags = _mm_mask_mov_epi8(flags, (__mmask16)starts, _mm_andnot_si128(_mm_set1_epi8(BUSTER_PP_SPACE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE), flags));
    flags = _mm_mask_mov_epi8(flags, (__mmask16)spaced, _mm_or_si128(flags, _mm_set1_epi8(BUSTER_PP_SPACE)));
    // A name's body's first token: its name's space and BUSTER_PP_NAME_SPACE (pp_emit_simple_write's), which the
    // expansion's first token, next, gives up for its name's flags as any other first token does.
    flags = _mm_mask_mov_epi8(flags, (__mmask16)named, _mm_set1_epi8(BUSTER_PP_FROM_MACRO | BUSTER_PP_NAME_SPACE));
    flags = _mm_mask_mov_epi8(flags, (__mmask16)named_spaced, _mm_set1_epi8(BUSTER_PP_FROM_MACRO | BUSTER_PP_NAME_SPACE | BUSTER_PP_SPACE));
    flags = _mm_mask_mov_epi8(flags, emit ? 1 : 0, _mm_or_si128(_mm_andnot_si128(_mm_set1_epi8((char)name_clear), flags), _mm_set1_epi8((char)(name_flags | BUSTER_PP_NAME_SPACE))));
    _mm_storeu_si128((__m128i*)(out->kinds + out->count), kinds);
    _mm_storeu_si128((__m128i*)(out->flags + out->count), flags);
    _mm512_storeu_si512(out->values + out->count, values);
    _mm512_storeu_si512(out->locations + out->count, locations);
    out->count += at;
    *trailing = next_space ? BUSTER_PP_SPACE : 0;
    // pp_emit_simple_write's, pp_substitute_simple's and pp_substitute's history state: the anchors of the last item
    // they read.
    pp->history_anchor = template->locations[template->anchor];
    pp->history_spelling = template->locations[template->spelling];
    if (emit && pp->options.timing)
    {
        // Each name that went straight in wrote its body: its token and the tokens the template's `extra` counts.
        const uint32_t directs = (uint32_t)__builtin_popcount(expanding);
        const uint32_t direct_tokens = directs + (directs != 0 ? template->extra : 0);
        const uint32_t run_tokens = at - direct_tokens;
        pp->statistics.buffer_runs += directs + !after_direct;
        pp->statistics.buffer_run_tokens += run_tokens;
        pp->statistics.final_buffer_run_tokens += run_tokens;
        pp->statistics.direct_expansions += directs;
        pp->statistics.final_direct_object_tokens += direct_tokens;
    }
}

// pp_emit_simple_write of an expansion of at most BUSTER_PP_TEMPLATE_TOKENS tokens (pp_template_size) from its macro's
// template, whose names pp_template_names found (painted, or going straight in), with pp_emit_simple_size's room made:
// written by pp_template_write. The callers test the template and the size, so that the runs without templates (the
// history's) don't call it.
static void pp_emit_template(Preprocessor* pp, const PPInvocation* invocation, const PPTemplate* template, const PPFileData* file, uint32_t file_base,
                             uint8_t* trailing)
{
    pp_template_write(pp, invocation, template, file, file_base, &pp->output, template->painted, template->expanding, true, 0, trailing);
}

static bool pp_emit_simple(Preprocessor* pp, const PPInvocation* invocation, uint8_t* trailing)
{
    PPTemplate* template = pp_template_of(pp, &pp->macros[invocation->macro]);
    PPEmitPlan plan;
    bool result;
    if (template != 0)
    {
        // pp_emit_simple_plan's names and size, from the template (its expansion records no history).
        result = pp_template_names(pp, invocation->macro, template);
        const uint32_t size = result ? pp_template_size(pp, template, invocation->arguments, true) : 0;
        result = result && size != 0 && (uint64_t)pp->output.count + size + BUSTER_PP_TOKEN_SLACK <= pp->output.capacity;
        if (result && size <= BUSTER_PP_TEMPLATE_TOKENS)
        {
            pp_emit_template(pp, invocation, template, 0, 0, trailing);
        }
        else if (result)
        {
            pp_emit_simple_names(pp, invocation->macro, &plan);
            pp_emit_simple_size(pp, invocation, &plan);
            pp_emit_simple_write(pp, invocation, &plan, 0, 0, trailing);
        }
    }
    else
    {
        result = pp_emit_simple_plan(pp, invocation, &plan);
        if (result)
        {
            pp_emit_simple_write(pp, invocation, &plan, 0, 0, trailing);
        }
    }
    return result;
}

// pp_substitute_simple of an invocation whose macro has a template, when its expansion has 1 to
// BUSTER_PP_TEMPLATE_TOKENS tokens and the expansions have room for them: written by pp_template_write, its names left for
// the main loop to read again. Returns false, having written nothing, otherwise (pp_substitute_simple takes an empty
// one in a few instructions).
static bool pp_substitute_template(Preprocessor* pp, const PPInvocation* invocation, uint32_t* begin, uint8_t* trailing)
{
    const PPTemplate* template = pp_template_of(pp, &pp->macros[invocation->macro]);
    const uint32_t size = template != 0 ? pp_template_size(pp, template, invocation->arguments, false) : 0;
    PPTokens* out = &pp->expansions;
    const bool result = size != 0 && size <= BUSTER_PP_TEMPLATE_TOKENS && (uint64_t)out->count + size <= out->capacity;
    if (result)
    {
        *begin = out->count;
        pp_template_write(pp, invocation, template, 0, 0, out, 0, 0, false, 0, trailing);
    }
    return result;
}

// Whether an invocation of a template with ## and # takes the templates' path, and how: its plan
// (pp_template_operators, with `given` of its argument records), then its expansion empty, or written straight into
// the output (pp_emit_template's conditions: every name of a macro its body or its plan has is painted or goes straight
// in, and it fits), or else, but in file text (`file`), written into the expansions for the main loop to read again,
// as pp_substitute writes it (pp_substitute_template's conditions). Changes nothing but what the template keeps of its
// names (pp_template_names).
static uint32_t pp_plan_operators(Preprocessor* pp, PPMacroId id, PPTemplate* template, const PPArgument* records, uint32_t given, const PPFileData* file,
                                  uint32_t file_base, PPOperators* made)
{
    uint32_t result = BUSTER_PP_OPERATORS_NONE;
    if (pp_template_operators(pp, id, template, records, given, file, file_base, made))
    {
        const bool direct = pp_template_names(pp, id, template) && !made->rescan;
        const uint64_t emitted = (uint64_t)made->size + template->extra;
        if (made->size == 0)
        {
            result = BUSTER_PP_OPERATORS_EMPTY;
        }
        else if (direct && emitted <= BUSTER_PP_TEMPLATE_TOKENS && (uint64_t)pp->output.count + emitted + BUSTER_PP_TOKEN_SLACK <= pp->output.capacity)
        {
            result = BUSTER_PP_OPERATORS_EMIT;
        }
        else if (file == 0 && made->size <= BUSTER_PP_TEMPLATE_TOKENS && (uint64_t)pp->expansions.count + made->size <= pp->expansions.capacity)
        {
            result = BUSTER_PP_OPERATORS_SUBSTITUTE;
        }
    }
    return result;
}

// Finishes an invocation of a template with ## and # as pp_plan_operators planned it (`mode`, `made`), its arguments in
// pp->arguments or (`file` nonzero) in the file's tokens, read in the range at `file_base`: its pastes and strings
// made (pp_template_make), then its expansion written (pp_template_write), or for an empty one its trailing space,
// which is any item's that a token would have taken (but a ##'s, which pp_substitute doesn't read), and the history's
// anchors, as pp_substitute leaves them.
static void pp_finish_operators(Preprocessor* pp, const PPInvocation* invocation, const PPTemplate* template, uint32_t mode, const PPFileData* file,
                                uint32_t file_base, PPOperators* made, uint8_t* trailing)
{
    pp_template_make(pp, template, &pp->argument_records[invocation->arguments], invocation->argument_count, made);
    if (mode == BUSTER_PP_OPERATORS_EMIT)
    {
        pp_template_write(pp, invocation, template, file, file_base, &pp->output, template->painted, template->expanding, true, made, trailing);
    }
    else if (mode == BUSTER_PP_OPERATORS_SUBSTITUTE)
    {
        pp_template_write(pp, invocation, template, 0, 0, &pp->expansions, 0, 0, false, made, trailing);
    }
    else
    {
        *trailing = (template->spaces & ~template->pastes) != 0 ? BUSTER_PP_SPACE : 0;
        pp->history_anchor = template->locations[template->anchor];
        pp->history_spelling = template->locations[template->spelling];
    }
}

// Whether the name of a function-like macro at token `name` of pp->arguments, in an argument that ends at `end`, opens an
// invocation that pp_expand_argument_in_place can expand, as the main loop would behind the argument's barrier (pp_invoke
// and pp_finish_simple, which substitutes it into the expansions there, since a capture keeps it from going straight
// out), then read back: its macro has a template (a vectorized run with its history deferred), its '(' follows its name
// and its ')' is in the argument, it is given as many arguments as it takes, none naming an enabled macro, and its
// expansion has 1 to BUSTER_PP_TEMPLATE_TOKENS tokens (a template with ## and # may make none) and names no macro its
// rescan would expand: its body's names (found again by pp_template_names where the expansions under way changed them)
// and its pastes' (pp_template_operators) are of the invoked macro or of disabled macros, which that rescan paints, or no
// macro's. Fills `nested` for the expansion, compiling the macro as pp_invoke does, and changes nothing else.
static bool pp_nested_invocation(Preprocessor* pp, uint32_t name, uint32_t end, PPNested* nested)
{
    const PPMacroId id = pp->macro_of[pp->arguments.values[name]];
    PPMacro* macro = &pp->macros[id];
    bool result = pp->templates != 0 && (macro->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN)) == BUSTER_PP_MACRO_FUNCTION &&
                  name + 1 < end && pp->arguments.kinds[name + 1] == C_TOKEN_L_PAREN &&
                  pp->argument_record_count + BUSTER_PP_TEMPLATE_TOKENS < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift);
    if (result && !(macro->flags & BUSTER_PP_MACRO_COMPILED))
    {
        result = pp_compile_macro(pp, id);
    }
    PPTemplate* template = result ? pp_template_of(pp, macro) : 0;
    PPScan scan;
    result = template != 0 && macro->parameter_count <= BUSTER_PP_TEMPLATE_TOKENS;
    if (result)
    {
        pp_scan_arguments(pp->arguments.kinds, name + 1, end, (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0, macro->parameter_count, &scan);
        result = scan.close != BUSTER_PP_NONE && scan.separator_count < BUSTER_PP_TEMPLATE_TOKENS;
    }
    if (result)
    {
        // pp_collect_arguments' records, but into the argument's tokens, and pp_finish_simple's count of them.
        uint32_t begin = name + 2;
        for (uint32_t s = 0; s < scan.separator_count; s += 1)
        {
            nested->records[s] = (PPArgument){ .raw_begin = begin, .raw_end = scan.separators[s] };
            begin = scan.separators[s] + 1;
        }
        nested->records[scan.separator_count] = (PPArgument){ .raw_begin = begin, .raw_end = scan.close };
        uint32_t count = scan.separator_count + 1;
        count = macro->parameter_count == 0 && count == 1 && begin == scan.close ? 0 : count;
        const bool omitted = (macro->flags & BUSTER_PP_MACRO_VARIADIC) && count + 1 == macro->parameter_count;
        if (omitted)
        {
            nested->records[count] = (PPArgument){ .raw_begin = scan.close, .raw_end = scan.close };
        }
        result = count + omitted == macro->parameter_count;
        for (uint32_t a = 0; result && a < count; a += 1)
        {
            result = !pp_needs_expansion(pp, &pp->arguments, nested->records[a].raw_begin, nested->records[a].raw_end);
        }
        pp_template_names(pp, id, template);
        result = result && template->expanding == 0;
        if (result && (template->pastes | template->strings) != 0)
        {
            result = pp_template_operators(pp, id, template, nested->records, count, 0, 0, &nested->made) && !nested->made.rescan &&
                     nested->made.size <= BUSTER_PP_TEMPLATE_TOKENS;
        }
        else if (result)
        {
            // pp_template_size's, from these records.
            uint32_t size = (uint32_t)template->count - (uint32_t)__builtin_popcount(template->parameter_mask);
            for (uint32_t bits = template->parameter_mask; bits != 0; bits &= bits - 1)
            {
                const PPArgument* argument = &nested->records[template->parameters[__builtin_ctz(bits)]];
                size += argument->raw_end - argument->raw_begin;
            }
            result = size != 0 && size <= BUSTER_PP_TEMPLATE_TOKENS;
        }
        nested->macro = id;
        nested->template = template;
        nested->close = scan.close;
        nested->count = count;
        nested->omitted = omitted;
    }
    return result;
}

// Expands an invocation that pp_nested_invocation took, at the end of pp->arguments (`name` its name, with the flags
// pending before it), as the main loop's path leaves it in the argument's capture: pp_invoke's history record, then
// pp_finish_simple's statistics and substitution into the expansions there (pp_finish_operators' pastes and strings,
// pp_template_write), the expansion's first token with the name's flags; then the buffer it pushes read back by a run of
// pp_step_buffer's, which paints the names of the invoked macro and of disabled ones, and its trailing flags left pending
// for the next token, as the buffer's pop leaves them (pp_pop_context); or for an empty expansion, the name's flags left
// pending as pp_finish_simple leaves them. Returns false, after a fatal error, if there is no room.
static bool pp_expand_nested(Preprocessor* pp, PPNested* nested, PPToken name, uint8_t* pending)
{
    PPTokens* out = &pp->arguments;
    const PPTemplate* template = nested->template;
    const uint32_t given = nested->count + nested->omitted;
    const uint32_t base = pp->argument_record_count;
    const bool result = pp_room(pp, out, BUSTER_PP_TEMPLATE_TOKENS);
    if (result)
    {
        memcpy(&pp->argument_records[base], nested->records, (uint64_t)given * sizeof(PPArgument));
        pp->argument_record_count = base + given;
        const PPInvocation invocation = {
            .macro = nested->macro,
            .history = pp_history_expansion(pp, nested->macro, name.location, name.origin),
            .arguments = base,
            .argument_count = given,
            .name_location = name.location,
            .name_flags = (uint8_t)(name.flags | pp_name_line(pp, name.flags, name.location)),
        };
        if (pp->options.timing)
        {
            pp->statistics.invocations += 1;
            pp->statistics.arguments += given;
        }
        const uint32_t saved_origin = pp->diagnostic_origin;
        pp->diagnostic_origin = invocation.history != 0 ? pp->history[invocation.history - 1].parent : 0;
        const uint32_t at = out->count;
        uint8_t trailing;
        if ((template->pastes | template->strings) != 0)
        {
            pp_template_make(pp, template, &pp->argument_records[base], given, &nested->made);
            if (nested->made.size != 0)
            {
                pp_template_write(pp, &invocation, template, 0, 0, out, 0, 0, false, &nested->made, &trailing);
            }
            else
            {
                trailing = (template->spaces & ~template->pastes) != 0 ? BUSTER_PP_SPACE : 0;
                pp->history_anchor = template->locations[template->anchor];
                pp->history_spelling = template->locations[template->spelling];
            }
        }
        else
        {
            pp_template_write(pp, &invocation, template, 0, 0, out, 0, 0, false, 0, &trailing);
        }
        pp->diagnostic_origin = saved_origin;
        const uint8_t name_flags = invocation.name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE);
        if (at < out->count)
        {
            out->flags[at] = (uint8_t)((out->flags[at] & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags |
                                       BUSTER_PP_NAME_SPACE);
            for (uint32_t t = at; t < out->count; t += 1)
            {
                const PPMacroId named = out->kinds[t] == C_TOKEN_RAW_IDENTIFIER && !(out->flags[t] & BUSTER_PP_NO_EXPAND) ? pp->macro_of[out->values[t]] : 0;
                out->flags[t] |= named != 0 && (named == nested->macro || pp->macros[named].disabled != 0) ? BUSTER_PP_NO_EXPAND : 0;
            }
            if (pp->options.timing)
            {
                pp->statistics.buffer_runs += 1;
                pp->statistics.buffer_run_tokens += out->count - at;
            }
            *pending = (uint8_t)(trailing & ~((trailing & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE | BUSTER_PP_NAME_LINE : 0));
        }
        else
        {
            const uint8_t name_space = (name_flags & BUSTER_PP_SPACE) && !(trailing & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE : 0;
            *pending |= (uint8_t)((name_flags & ~(name_space != 0 ? 0 : BUSTER_PP_NAME_LINE)) | trailing | name_space);
        }
        pp->argument_record_count = base;
    }
    return result;
}

// Whether every enabled macro that tokens [begin, end) of pp->arguments name is a direct object (pp_direct_object) or opens
// an invocation pp_nested_invocation takes: then the argument's expansion is its tokens with each such name replaced by
// its body, or with each invocation replaced by its expansion, which names no macro that expands. Each invocation's plan
// is kept in pp->nested (BUSTER_PP_NESTED_PLANS of them for one invocation's arguments).
static bool pp_argument_in_place(Preprocessor* pp, uint32_t begin, uint32_t end)
{
    bool result = true;
    for (uint32_t i = begin; result && i < end; i += 1)
    {
        const IdentId id = pp->arguments.values[i];
        const bool name = pp->arguments.kinds[i] == C_TOKEN_RAW_IDENTIFIER && !(pp->arguments.flags[i] & BUSTER_PP_NO_EXPAND) && pp_is_macro(pp, id) &&
                          pp->macros[pp->macro_of[id]].disabled == 0;
        if (name && (pp->macros[pp->macro_of[id]].flags & BUSTER_PP_MACRO_FUNCTION))
        {
            result = pp->nested_count < BUSTER_PP_NESTED_PLANS && pp_nested_invocation(pp, i, end, &pp->nested[pp->nested_count]);
            i = result ? pp->nested[pp->nested_count].close : i;
            pp->nested_count += result;
        }
        else
        {
            result = !name || pp_direct_object(pp, &pp->macros[pp->macro_of[id]]);
        }
    }
    return result;
}

// An argument that pp_argument_in_place takes, expanded as the main loop expands it behind its barrier, at the end of
// pp->arguments, which the argument's record then names: its runs of other tokens copied (pp_step_buffer's, but for
// their names of disabled macros, which the main loop paints there and the expansion's rescan or pp_emit_simple paints
// as surely, as pp_finish_simple leaves the arguments that need no expansion), each with the flags pending before it on
// its first token (pp_take_pending_flags), each direct object's body in its name's place under an expansion record of its
// own, its first token with the name's flags (pp_expand_object's direct expansion), and each invocation's expansion in
// its place (pp_expand_nested). With the statistics the main loop counts: a buffer run where the argument starts and
// after each name it expands and each invocation, while tokens are left. Returns false, after a fatal error, if there is
// no room.
static bool pp_expand_argument_in_place(Preprocessor* pp, PPArgument* argument)
{
    PPTokens* out = &pp->arguments;
    const uint32_t begin = out->count;
    uint64_t runs = 1;
    uint64_t copied = 0;
    uint32_t run = argument->raw_begin;
    uint8_t pending = 0;
    bool result = true;
    for (uint32_t i = argument->raw_begin; result && i < argument->raw_end; i += 1)
    {
        PPToken token = pp_stack_token(out, i);
        const PPMacroId id = token.kind == C_TOKEN_RAW_IDENTIFIER && !(token.flags & BUSTER_PP_NO_EXPAND) ? pp->macro_of[token.value] : 0;
        if (id != 0 && pp->macros[id].disabled == 0)
        {
            const PPMacro* macro = &pp->macros[id];
            const uint32_t first = out->count;
            result = pp_append_stack_tokens(pp, out, out, run, i);
            if (result && pending != 0 && run < i)
            {
                out->flags[first] = pp_with_pending(out->flags[first], pending, false);
                pending = 0;
            }
            copied += i - run;
            token.flags = pending != 0 ? pp_with_pending(token.flags, pending, false) : token.flags;
            pending = 0;
            if (macro->flags & BUSTER_PP_MACRO_FUNCTION)
            {
                // pp_argument_in_place planned it, and nothing its plan reads has changed since.
                PPNested* nested = &pp->nested[pp->nested_taken];
                pp->nested_taken += 1;
                result = result && pp_expand_nested(pp, nested, token, &pending);
                runs += nested->close + 1 < argument->raw_end;
                run = nested->close + 1;
                i = nested->close;
            }
            else
            {
                const uint32_t saved_history = pp->history_current;
                pp->history_current = pp_history_expansion(pp, id, token.location, token.origin);
                const uint32_t at = out->count;
                result = result && pp_append_file_tokens(pp, out, &pp->datas[macro->data], macro->base, macro->body_begin, macro->body_end, true);
                pp->history_current = saved_history;
                if (result)
                {
                    const uint8_t name_flags = (uint8_t)((token.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE)) | pp_name_line(pp, token.flags, token.location));
                    uint8_t* flags = &out->flags[at];
                    *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags | BUSTER_PP_NAME_SPACE);
                    pp->statistics.direct_expansions += pp->options.timing;
                }
                runs += i + 1 < argument->raw_end;
                run = i + 1;
            }
        }
    }
    const uint32_t first = out->count;
    result = result && pp_append_stack_tokens(pp, out, out, run, argument->raw_end);
    if (result && pending != 0 && run < argument->raw_end)
    {
        out->flags[first] = pp_with_pending(out->flags[first], pending, false);
    }
    copied += argument->raw_end - run;
    argument->raw_begin = begin;
    argument->raw_end = out->count;
    if (pp->options.timing)
    {
        pp->statistics.expanded_arguments += 1;
        pp->statistics.buffer_runs += runs;
        pp->statistics.buffer_run_tokens += copied;
    }
    return result;
}

// Finishes an invocation pp_collect_arguments collected, as pp_close_invocation, pp_continue_invocation and
// pp_finish_invocation would, when that is simple: a BUSTER_PP_MACRO_SIMPLE macro, or one whose template has ## and #
// and plans the invocation (pp_plan_operators), given as many arguments as it takes, none of the ones it expands naming
// an enabled macro (so each is its own expansion) but, for a simple macro, direct objects and invocations of templates
// whose expansions name no macro that expands, which take their places (pp_expand_argument_in_place), read while no
// stream or capture with a limit is open. Then it isn't
// pushed on the invocation stack: its expansion is substituted at once (pp_substitute_simple, pp_finish_operators) and
// pushed as a buffer, or written straight into the output. Returns false, having changed nothing, when the invocation
// isn't simple. (Copying an expansion that names no macro straight to the target, rather than pushing it, saved 75
// instructions per such expansion on sqlite3.c and cost 80 for the check on every other, a 2 million loss.)
static bool pp_finish_simple(Preprocessor* pp, PPInvocation* invocation)
{
    const PPMacro* macro = &pp->macros[invocation->macro];
    PPArgument* records = &pp->argument_records[invocation->arguments];
    // pp_close_invocation's count: f() has no argument when f has no parameter, and a variadic macro's variadic
    // argument may be omitted altogether (it gets an empty one).
    uint32_t count = pp->argument_record_count - invocation->arguments;
    count = macro->parameter_count == 0 && count == 1 && records[0].raw_begin == records[0].raw_end ? 0 : count;
    const bool omitted = (macro->flags & BUSTER_PP_MACRO_VARIADIC) && count + 1 == macro->parameter_count;
    // A macro with ## and # takes its template's path alone (pp_finish_operators), when its plan has one.
    PPTemplate* template = pp_template_of(pp, macro);
    const bool operators = template != 0 && (template->pastes | template->strings) != 0;
    bool result = ((macro->flags & BUSTER_PP_MACRO_SIMPLE) || operators) && (pp->stream_count | pp->limit_count) == 0 && !pp->fatal &&
                  count + omitted == macro->parameter_count && pp->argument_record_count < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift);
    const PPItem* order = pp->items + macro->items_begin + macro->item_count;
    // The arguments to expand in place, by their places in the order (a macro expands at most 64 so), and the plans of
    // the invocations in them.
    uint64_t direct = 0;
    pp->nested_count = 0;
    pp->nested_taken = 0;
    for (uint32_t k = 0; result && k < macro->order_count; k += 1)
    {
        const uint32_t parameter = order[k].parameter;
        const bool needs = parameter < count && pp_needs_expansion(pp, &pp->arguments, records[parameter].raw_begin, records[parameter].raw_end);
        const bool expands = needs && (macro->flags & BUSTER_PP_MACRO_SIMPLE) && k < 64 && pp_argument_in_place(pp, records[parameter].raw_begin, records[parameter].raw_end);
        result = !needs || expands;
        direct |= expands ? 1ull << k : 0;
    }
    PPOperators made;
    uint32_t mode = BUSTER_PP_OPERATORS_NONE;
    if (result && operators)
    {
        mode = pp_plan_operators(pp, invocation->macro, template, records, count, 0, 0, &made);
        result = mode != BUSTER_PP_OPERATORS_NONE;
    }
    if (result)
    {
        if (omitted)
        {
            pp_new_argument(pp);
        }
        pp->in_macro_arguments = false;
        invocation->argument_count = count + omitted;
        if (pp->options.timing)
        {
            pp->statistics.invocations += 1;
            pp->statistics.arguments += count + omitted;
        }
        // In the order the general path expands them in (pp_continue_invocation's), each record then naming its expansion.
        for (uint64_t pending = direct; pending != 0 && !pp->fatal; pending &= pending - 1)
        {
            pp_expand_argument_in_place(pp, &records[order[__builtin_ctzll(pending)].parameter]);
        }
        if (pp->fatal)
        {
            // An argument without room to expand in (pp_room's, or its origins' growth, pp_history_fill's) ends the run,
            // as the main loop stops before it substitutes: the invocation is consumed and nothing is substituted, which
            // would read the argument's origins past what was allocated.
            pp->arguments.count = invocation->arguments_begin;
            pp->argument_record_count = invocation->arguments;
        }
        else
        {
            // pp_close_invocation's place for diagnostics while it substitutes (a stack that overflows).
            const uint32_t saved_origin = pp->diagnostic_origin;
            pp->diagnostic_origin = invocation->history != 0 ? pp->history[invocation->history - 1].parent : 0;
            uint32_t begin = 0;
            uint32_t end = 0;
            uint8_t trailing;
            bool emitted;
            if (operators)
            {
                emitted = mode == BUSTER_PP_OPERATORS_EMIT;
                begin = pp->expansions.count;
                pp_finish_operators(pp, invocation, template, mode, 0, 0, &made, &trailing);
            }
            else
            {
                emitted = pp_emit_simple(pp, invocation, &trailing);
                if (!emitted && !pp_substitute_template(pp, invocation, &begin, &trailing))
                {
                    pp_substitute_simple(pp, invocation, &begin, &trailing);
                }
            }
            end = emitted ? 0 : pp->expansions.count;
            const uint8_t name_flags = invocation->name_flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE);
            if (begin < end)
            {
                uint8_t* flags = &pp->expansions.flags[begin];
                *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags | BUSTER_PP_NAME_SPACE);
            }
            pp->arguments.count = invocation->arguments_begin;
            pp->argument_record_count = invocation->arguments;
            if (emitted)
            {
                // The buffer's pop would pass its trailing space on to the context below it.
                pp->contexts[pp->context_count - 1].pending_flags |= trailing;
            }
            else if (begin < end)
            {
                PPContext* context = pp_push_buffer(pp, &pp->expansions, begin, end, invocation->macro, invocation->expansion_end, false, true);
                if (context != 0)
                {
                    context->trailing_flags = trailing;
                }
            }
            else
            {
                // An empty expansion: the next token read after the invocation's ')' takes its name's flags.
                const uint8_t name_space = (name_flags & BUSTER_PP_SPACE) && !(trailing & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE : 0;
                pp->contexts[pp->context_count - 1].pending_flags |= (uint8_t)((name_flags & ~(name_space != 0 ? 0 : BUSTER_PP_NAME_LINE)) | trailing | name_space);
            }
            pp->diagnostic_origin = saved_origin;
        }
    }
    return result;
}

// pp_expand_current, pp_invoke, pp_collect_arguments and pp_finish_simple for the name of a function-like macro at a file
// context's position, when its invocation is simple and its expansion goes straight into the output (pp_emit_simple)
// or is empty: the arguments are found in the file's tokens 64 at a time (pp_scan_arguments) and written into the output
// from there, never copied into pp->arguments, with the history, statistics and state that path leaves. Everything is
// checked before anything changes (but compiling the macro and making room for history, as that path does first), what
// needs no argument first. Returns false, having changed nothing else, when the invocation isn't so: pp_expand_current
// then reads it, scanning its arguments again (handing it the scan cost more, by keeping pp_invoke from being inlined into
// pp_expand_current: 3.7 million instructions on main.c). The caller has checked what reading the file's text needs
// (pp_step_file's), and that nothing is pending (pp_expand_current would give it to the name).
static bool pp_invoke_file(Preprocessor* pp, PPContext* context, const PPFileData* data, uint32_t base, uint32_t limit)
{
    const uint32_t position = context->position;
    const PPMacroId id = pp->macro_of[data->values[position]];
    PPMacro* macro = &pp->macros[id];
    bool result = false;
    bool simple = (macro->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN)) == BUSTER_PP_MACRO_FUNCTION && macro->disabled == 0 && !pp->fatal &&
                  position + 1 < limit && (data->kinds[position + 1] & 0x7F) == C_TOKEN_L_PAREN;
    if (simple && !(macro->flags & BUSTER_PP_MACRO_COMPILED))
    {
        pp_compile_macro(pp, id);
    }
    PPEmitPlan plan;
    PPTemplate* template = simple ? pp_template_of(pp, macro) : 0;
    // A template with ## (but not #, which stringizes from pp->arguments) is planned once its arguments are found.
    const bool operators = template != 0 && (template->pastes | template->strings) != 0;
    simple = simple && (operators ? template->strings == 0
                                  : (macro->flags & BUSTER_PP_MACRO_SIMPLE) && (template != 0 ? pp_template_names(pp, id, template) : pp_emit_simple_names(pp, id, &plan)));
    if (simple)
    {
        const uint32_t open = position + 1;
        PPScan scan;
        pp_scan_arguments(data->kinds, open, limit, (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0, macro->parameter_count, &scan);
        const uint32_t close = scan.close;
        // pp_close_invocation's count: f() has no argument when f has no parameter, and a variadic macro's variadic
        // argument may be omitted altogether (it gets an empty one). The records go where pp_invoke would put them, in
        // the file's token indices, above the stack's count.
        const uint32_t records_begin = pp->argument_record_count;
        uint32_t count = scan.separator_count + 1;
        count = macro->parameter_count == 0 && count == 1 && close == open + 1 ? 0 : count;
        const bool omitted = (macro->flags & BUSTER_PP_MACRO_VARIADIC) && count + 1 == macro->parameter_count;
        simple = close != BUSTER_PP_NONE && count + omitted == macro->parameter_count && pp->invocation_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift) &&
                 records_begin + scan.separator_count + 1 < (BUSTER_PP_MAX_STACK_TOKENS >> pp->capacity_shift) && pp_room(pp, &pp->arguments, close - open);
        PPArgument* records = &pp->argument_records[records_begin];
        if (simple)
        {
            uint32_t argument_begin = open + 1;
            for (uint32_t i = 0; i < scan.separator_count; i += 1)
            {
                records[i] = (PPArgument){ .raw_begin = argument_begin, .raw_end = scan.separators[i] };
                argument_begin = scan.separators[i] + 1;
            }
            records[scan.separator_count] = (PPArgument){ .raw_begin = argument_begin, .raw_end = close };
            records[scan.separator_count + 1] = (PPArgument){ .raw_begin = close, .raw_end = close };
            // The arguments its expansion uses name no enabled macro (pp_needs_expansion: a file's tokens are never
            // painted).
            const PPItem* order = pp->items + macro->items_begin + macro->item_count;
            for (uint32_t k = 0; simple && k < macro->order_count; k += 1)
            {
                const uint32_t parameter = order[k].parameter;
                for (uint32_t t = parameter < count ? records[parameter].raw_begin : 0; simple && parameter < count && t < records[parameter].raw_end; t += 1)
                {
                    const IdentId value = data->values[t];
                    simple = (data->kinds[t] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro_vector(pp, value) || pp->macros[pp->macro_of[value]].disabled != 0;
                }
            }
        }
        // A template with ##: its plan, which goes straight into the output or is empty, or the invocation isn't simple.
        PPOperators made;
        const uint32_t mode = simple && operators ? pp_plan_operators(pp, id, template, records, count, data, base, &made) : BUSTER_PP_OPERATORS_NONE;
        simple = simple && (!operators || mode != BUSTER_PP_OPERATORS_NONE);
        // An empty expansion (pp_finish_simple's): every item a parameter given an empty argument. Its trailing space is
        // the items' that no token took (pp_substitute_simple's), but a ##'s. A template has the expansion's size, which
        // only an empty one doesn't have tokens, and its items' spaces.
        const uint32_t size = !simple || template == 0 ? 0 : operators ? (mode == BUSTER_PP_OPERATORS_EMIT ? made.size + template->extra : 0)
                                                                       : pp_template_size(pp, template, records_begin, true);
        bool empty = simple && (template == 0 || size == 0);
        uint8_t empty_trailing = template != 0 && (template->spaces & ~template->pastes) != 0 ? BUSTER_PP_SPACE : 0;
        const PPItem* items = pp->items + macro->items_begin;
        for (uint32_t i = 0; template == 0 && empty && i < macro->item_count; i += 1)
        {
            empty = items[i].kind == PP_ITEM_PARAMETER && records[items[i].parameter].raw_begin == records[items[i].parameter].raw_end;
            empty_trailing |= (items[i].flags & BUSTER_PP_ITEM_SPACE) ? BUSTER_PP_SPACE : 0;
        }
        PPInvocation invocation = {
            .macro = id,
            .arguments = records_begin,
            .argument_count = count + omitted,
        };
        // pp_emit_simple_size's room, which a template's size needs alone (its expansion records no history).
        const bool emitted = simple && !empty &&
                             (template != 0 ? (uint64_t)pp->output.count + size + BUSTER_PP_TOKEN_SLACK <= pp->output.capacity : pp_emit_simple_size(pp, &invocation, &plan));
        if (emitted || empty)
        {
            // pp_invoke's record of the expansion, its ')' as pp_collect_arguments records it.
            const uint32_t location = base + data->starts[position];
            invocation.history = pp_history_expansion(pp, id, location, 0);
            if (invocation.history != 0)
            {
                pp->history[invocation.history - 1].end = base + data->starts[close];
                pp->history[invocation.history - 1].end_origin = 0;
            }
            invocation.name_flags = (uint8_t)((pp_file_space(data, position) ? BUSTER_PP_SPACE : 0) | ((data->kinds[position] & BUSTER_C_TOKEN_START_OF_LINE) ? BUSTER_PP_START_OF_LINE : 0));
            // The arguments' copy would have given its origins (pp_history_fill's peak).
            const uint32_t copied = close - open - 1;
            if (!pp->options.no_history && copied != 0)
            {
                pp->origin_peak[0] = pp->origin_peak[0] < pp->arguments.count + copied ? pp->arguments.count + copied : pp->origin_peak[0];
            }
            pp->in_macro_arguments = false;
            if (pp->options.timing)
            {
                pp->statistics.invocations += 1;
                pp->statistics.arguments += count + omitted;
            }
            context->position = close + 1;
            result = true;
        }
        if (emitted)
        {
            const uint32_t saved_origin = pp->diagnostic_origin;
            pp->diagnostic_origin = 0;
            uint8_t trailing;
            if (operators)
            {
                pp_finish_operators(pp, &invocation, template, mode, data, base, &made, &trailing);
            }
            else if (template != 0 && size <= BUSTER_PP_TEMPLATE_TOKENS)
            {
                pp_emit_template(pp, &invocation, template, data, base, &trailing);
            }
            else
            {
                // Past a template's size, pp_emit_simple_write, with the plan the template's names and size stood for.
                if (template != 0)
                {
                    pp_emit_simple_names(pp, id, &plan);
                    pp_emit_simple_size(pp, &invocation, &plan);
                }
                pp_emit_simple_write(pp, &invocation, &plan, data, base, &trailing);
            }
            pp->diagnostic_origin = saved_origin;
            // The buffer's pop would pass its trailing space on to the context below it.
            context->pending_flags |= trailing;
        }
        if (empty)
        {
            // The next token read after the invocation's ')' takes its name's flags (pp_finish_simple).
            const uint8_t name_flags = invocation.name_flags;
            const uint8_t name_space = (name_flags & BUSTER_PP_SPACE) && !(empty_trailing & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE : 0;
            context->pending_flags |= (uint8_t)((name_flags & ~(name_space != 0 ? 0 : BUSTER_PP_NAME_LINE)) | empty_trailing | name_space);
            // pp_substitute_simple left the anchors of its last item, and pp_substitute of the last item it read.
            if (operators)
            {
                pp->history_anchor = template->locations[template->anchor];
                pp->history_spelling = template->locations[template->spelling];
            }
            else if (macro->item_count != 0)
            {
                pp->history_anchor = macro->base + pp->datas[macro->data].starts[items[macro->item_count - 1].token];
                pp->history_spelling = pp->history_anchor;
            }
        }
    }
    return result;
}

static void pp_invoke(Preprocessor* pp, PPMacroId id, PPToken name)
{
    PPMacro* macro = &pp->macros[id];
    if (!(macro->flags & BUSTER_PP_MACRO_COMPILED))
    {
        pp_compile_macro(pp, id);
    }
    const bool variadic = (macro->flags & BUSTER_PP_MACRO_VARIADIC) != 0;
    PPInvocation invocation = {
        .macro = id,
        .history = pp_history_expansion(pp, id, name.location, name.origin),
        .arguments = pp->argument_record_count,
        .arguments_begin = pp->arguments.count,
        .captures_begin = pp->captures.count,
        .pragma_mark = pp->pragma_lines,
        .name_location = name.location,
        .name_flags = (uint8_t)(name.flags | pp_name_line(pp, name.flags, name.location)),
        .opened = true,
        .content_end = BUSTER_PP_NONE,
    };
    bool valid = pp->invocation_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift);
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, name.location, "macro expansion nests too deeply");
    }
    valid = valid && pp_new_argument(pp);
    const bool collected = valid && (pp_collect_argument_slice(pp, &invocation, variadic, macro->parameter_count) ||
                                    (pp->vectorized && pp_collect_arguments(pp, &invocation, variadic, macro->parameter_count)));
    const bool finished = collected && pp->vectorized && pp_finish_simple(pp, &invocation);
    valid = valid && !finished;
    if (valid && !collected)
    {
        PPToken token;
        uint32_t expansion_end;
        pp_read_token(pp, &token, &expansion_end); // The '(' pp_next_is_lparen saw.
        invocation.collecting = true;
        invocation.capture_mark = pp->capture_count;
    }
    if (valid)
    {
        pp->invocations[pp->invocation_count] = invocation;
        pp->invocation_count += 1;
        pp->in_macro_arguments = !collected;
    }
    if (valid && collected)
    {
        pp_close_invocation(pp, true);
    }
}

// Expands an object-like macro whose name was just read. One whose body names no macro and has no ## goes straight to the
// target (the main loop's commonest expansion: a constant); any other is built in a buffer and pushed as a context.
static void pp_expand_object(Preprocessor* pp, PPMacroId id, PPToken name, uint32_t expansion_end)
{
    const uint32_t saved_history = pp->history_current;
    pp->history_current = pp_history_expansion(pp, id, name.location, name.origin);
    PPMacro* macro = &pp->macros[id];
    const PPFileData* data = &pp->datas[macro->data];
    // Straight into the target, when nothing in the body expands (but while a stream is open, which reads a token at a
    // time: pp_step_limit).
    bool direct = (macro->flags & BUSTER_PP_MACRO_PASTES) == 0 && pp->stream_count == 0;
    for (uint32_t t = macro->body_begin; direct && t < macro->body_end; t += 1)
    {
        direct = (data->kinds[t] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro(pp, data->values[t]);
    }
    PPTokens* tokens = direct ? pp_target(pp) : &pp->expansions;
    const uint32_t begin = tokens->count;
    if (macro->flags & BUSTER_PP_MACRO_PASTES)
    {
        if ((macro->flags & BUSTER_PP_MACRO_COMPILED) || pp_compile_macro(pp, id))
        {
            const PPInvocation invocation = { .macro = id, .history = pp->history_current };
            uint32_t substituted;
            uint8_t trailing;
            pp_substitute(pp, &invocation, &substituted, &trailing);
        }
    }
    else if (BUSTER_VECTOR_PATH(pp->vectorized))
    {
        pp_append_file_tokens(pp, tokens, data, macro->base, macro->body_begin, macro->body_end, true);
    }
    else if (pp_room(pp, tokens, macro->body_end - macro->body_begin))
    {
        for (uint32_t t = macro->body_begin; t < macro->body_end; t += 1)
        {
            PPToken token = pp_file_token(data, macro->base, t);
            token.flags = (uint8_t)((token.flags & BUSTER_PP_SPACE) | BUSTER_PP_FROM_MACRO);
            pp_tokens_set(tokens, tokens->count, token.kind, token.flags, token.value, token.location);
            tokens->count += 1;
        }
        pp_history_fill(pp, tokens, begin, tokens->count, pp->history_current);
    }
    const bool empty = begin == tokens->count;
    // The name's line start as clang's token has it: BUSTER_PP_LINE_SPACE among a macro's arguments, and pp_name_line's.
    const uint8_t name_flags = (uint8_t)((name.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE)) | pp_name_line(pp, name.flags, name.location));
    if (!empty)
    {
        uint8_t* flags = &tokens->flags[begin];
        *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags | BUSTER_PP_NAME_SPACE);
    }
    else
    {
        // An empty expansion: the next token read from this context takes the name's flags.
        const uint8_t name_space = (name.flags & BUSTER_PP_SPACE) ? BUSTER_PP_NAME_SPACE : 0;
        pp->contexts[pp->context_count - 1].pending_flags |= (uint8_t)((name_flags & ~(name_space != 0 ? 0 : BUSTER_PP_NAME_LINE)) | name_space);
    }
    if (!direct && !empty)
    {
        pp_push_buffer(pp, &pp->expansions, begin, pp->expansions.count, id, expansion_end, false, true);
    }
    if (pp->options.timing)
    {
        pp->statistics.direct_expansions += direct;
        pp->statistics.buffered_expansions += !direct;
        pp->statistics.final_direct_object_tokens += direct && tokens == &pp->output ? tokens->count - begin : 0;
    }
    pp->history_current = saved_history;
}

// A numeric constant spelled in the scratch space.
static PPToken pp_number_token(Preprocessor* pp, uint64_t value, uint8_t flags)
{
    char digits[24];
    const uint32_t length = (uint32_t)snprintf(digits, sizeof(digits), "%lu", (unsigned long)value);
    const uint32_t location = value == 0 ? pp->zero_location : value == 1 ? pp->one_location : pp_scratch_clean(pp, (const uint8_t*)digits, length);
    return (PPToken){ .kind = C_TOKEN_NUMERIC_CONSTANT, .flags = (uint8_t)(flags | BUSTER_PP_FROM_MACRO), .value = location + length, .location = location,
                      .origin = pp->history_current };
}

// A feature query's answer: a numeric constant, with an L after one past 1 (a date), as clang's feature-like builtins
// spell it (EvaluateFeatureLikeBuiltinMacro); __has_include's is never past 1.
static PPToken pp_feature_token(Preprocessor* pp, uint64_t value, uint8_t flags)
{
    PPToken result = pp_number_token(pp, value, flags);
    if (value > 1)
    {
        char digits[24];
        const uint32_t length = (uint32_t)snprintf(digits, sizeof(digits), "%luL", (unsigned long)value);
        result.location = pp_scratch_clean(pp, (const uint8_t*)digits, length);
        result.value = result.location + length;
    }
    return result;
}

// A string literal of `bytes`, with quotes and backslashes escaped, spelled in the scratch space.
static PPToken pp_string_token(Preprocessor* pp, const uint8_t* bytes, uint32_t length, uint8_t flags)
{
    Scratch scratch = scratch_begin(0);
    uint8_t* text = arena_push(scratch.arena, 2 * (uint64_t)length + 2, 1);
    PPToken result = { .kind = C_TOKEN_STRING_LITERAL, .flags = (uint8_t)(flags | BUSTER_PP_FROM_MACRO) };
    if (text != 0)
    {
        uint32_t n = 0;
        text[n] = '"';
        n += 1;
        for (uint32_t i = 0; i < length; i += 1)
        {
            // Quotes and backslashes are escaped, and a line break (\n, \r, \r\n or \n\r) is written \n, as clang's
            // Lexer::Stringify writes them.
            const bool line_break = bytes[i] == '\n' || bytes[i] == '\r';
            const bool pair = line_break && i + 1 < length && (bytes[i + 1] == '\n' || bytes[i + 1] == '\r') && bytes[i + 1] != bytes[i];
            if (bytes[i] == '"' || bytes[i] == '\\' || line_break)
            {
                text[n] = '\\';
                n += 1;
            }
            text[n] = line_break ? 'n' : bytes[i];
            n += 1;
            i += pair ? 1 : 0;
        }
        text[n] = '"';
        n += 1;
        result.location = pp_scratch_clean(pp, text, n);
        result.value = result.location + n;
        result.origin = pp->history_current;
    }
    scratch_end(scratch);
    return result;
}

// Is the innermost capture a #if line's?
BUSTER_INLINE bool pp_in_expression(const Preprocessor* pp)
{
    return pp->capture_count != 0 && pp->capture_frames[pp->capture_count - 1].kind == BUSTER_PP_CAPTURE_CONDITION;
}

// Is a #if line being expanded, here or around the macro arguments and builtin operands being expanded in it? (As in
// clang, __has_include may be in a macro's argument there; `defined` is evaluated in the line itself.)
static bool pp_in_condition(const Preprocessor* pp)
{
    uint32_t c = pp->capture_count;
    while (c != 0 && (pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_ARGUMENT || pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_BUILTIN ||
                      pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_STREAM))
    {
        c -= 1;
    }
    return c != 0 && pp->capture_frames[c - 1].kind == BUSTER_PP_CAPTURE_CONDITION;
}

// The innermost file context (for #include_next, __has_include_next and pragmas), or null.
static const PPContext* pp_current_file_context(const Preprocessor* pp)
{
    const PPContext* result = 0;
    for (uint32_t c = pp->context_count; result == 0 && c > 0; c -= 1)
    {
        result = pp->contexts[c - 1].kind == BUSTER_PP_CONTEXT_FILE ? &pp->contexts[c - 1] : 0;
    }
    return result;
}

// The file of the innermost file context, or BUSTER_PP_NONE.
static uint32_t pp_current_file(const Preprocessor* pp)
{
    const PPContext* context = pp_current_file_context(pp);
    return context != 0 ? context->file : BUSTER_PP_NONE;
}

// Whether token `i` of a stack is a '<' that spans a header name a #if line writes, up to its '>'
// (pp_collect_written_header).
static bool pp_written_angled(const Preprocessor* pp, const PPTokens* tokens, uint32_t i)
{
    const uint8_t* last = tokens->kinds[i] == C_TOKEN_LESS && tokens->values[i] - tokens->locations[i] > 1 ? pp_location_bytes(pp, tokens->values[i] - 1) : 0;
    return last != 0 && *last == '>';
}

// A header name from tokens [begin, end) of a stack: a string literal's contents, the name a '<' spans as a #if line
// writes it (pp_written_angled), or the spellings between '<' and '>' with a space where whitespace was before each token
// after the '<', the '>' too, as clang joins a header name that macros made (Preprocessor::LexHeaderName): `< c.h >` is
// " c.h ". Returns false if they are none of those (a name a '<' spans with tokens after it too).
static bool pp_header_name(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t* name, uint32_t capacity, uint32_t* length, bool* angled)
{
    bool result = false;
    *length = 0;
    if (begin < end && end - begin == 1 && (tokens->kinds[begin] == C_TOKEN_STRING_LITERAL || pp_written_angled(pp, tokens, begin)))
    {
        // The token's spelling without its line splices (translation phase 2), as for a direct #include, inside its
        // quotes or its '<' and '>'.
        Scratch scratch = scratch_begin(0);
        uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(tokens->values[begin] - tokens->locations[begin]) + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t spelled = buffer != 0 ? pp_spelling(pp, tokens->kinds[begin], tokens->values[begin], tokens->locations[begin], buffer, &spelling) : 0;
        // (A raw string literal is no header name, as in clang.)
        result = spelled >= 2 && spelled - 2 < capacity && (tokens->kinds[begin] == C_TOKEN_LESS || spelling[0] == '"');
        if (result)
        {
            memcpy(name, spelling + 1, spelled - 2);
            *length = spelled - 2;
            *angled = tokens->kinds[begin] == C_TOKEN_LESS;
        }
        scratch_end(scratch);
    }
    else if (begin < end && tokens->kinds[begin] == C_TOKEN_LESS && tokens->kinds[end - 1] == C_TOKEN_GREATER && !pp_written_angled(pp, tokens, begin))
    {
        result = true;
        for (uint32_t i = begin + 1; result && i < end; i += 1)
        {
            uint8_t buffer[64];
            const uint8_t* spelling;
            // The '>' gives only its space.
            const bool closing = i + 1 == end;
            const uint32_t spelled = closing ? 0 : tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER ? intern_length(pp->table, tokens->values[i]) : tokens->values[i] - tokens->locations[i];
            const bool space = (tokens->flags[i] & BUSTER_PP_SPACE) != 0;
            result = spelled < sizeof(buffer) && *length + spelled + 1 < capacity;
            if (result)
            {
                const uint32_t n = closing ? 0 : pp_spelling(pp, tokens->kinds[i], tokens->values[i], tokens->locations[i], buffer, &spelling);
                if (space)
                {
                    name[*length] = ' ';
                    *length += 1;
                }
                memcpy(name + *length, closing ? (const uint8_t*)"" : spelling, n);
                *length += n;
            }
        }
        *angled = true;
    }
    return result;
}

// Builds the name a feature query's argument holds: an identifier, or `scope::identifier`; names with leading and
// trailing "__" lose them for the attribute, feature and extension queries, as clang normalizes them.
// Whether a token of `kind` has a name that a feature query takes: an identifier or a keyword (clang's tokens with an
// IdentifierInfo).
BUSTER_INLINE bool pp_names_kind(uint8_t kind)
{
    return kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END);
}

static bool pp_feature_name(Preprocessor* pp, PPBuiltin builtin, const PPTokens* tokens, uint32_t begin, uint32_t end, uint8_t* name, uint32_t* length)
{
    bool result = false;
    *length = 0;
    // Attribute, feature and extension names may be written with "__" around them, which clang drops.
    const bool attribute = builtin == PP_BUILTIN_HAS_ATTRIBUTE || builtin == PP_BUILTIN_HAS_C_ATTRIBUTE || builtin == PP_BUILTIN_HAS_CPP_ATTRIBUTE ||
                           builtin == PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE || builtin == PP_BUILTIN_HAS_FEATURE || builtin == PP_BUILTIN_HAS_EXTENSION;
    // An identifier or a keyword; only __has_c_attribute and __has_cpp_attribute take a scope (`gnu::cold`).
    const bool scoped = (builtin == PP_BUILTIN_HAS_C_ATTRIBUTE || builtin == PP_BUILTIN_HAS_CPP_ATTRIBUTE) && end - begin == 3 && pp_names_kind(tokens->kinds[begin]) &&
                        tokens->kinds[begin + 1] == C_TOKEN_COLONCOLON && pp_names_kind(tokens->kinds[begin + 2]);
    const bool named = (end - begin == 1 && pp_names_kind(tokens->kinds[begin])) || scoped;
    if (named && builtin != PP_BUILTIN_HAS_WARNING)
    {
        result = true;
        for (uint32_t i = begin; i < end; i += 1)
        {
            if (tokens->kinds[i] == C_TOKEN_COLONCOLON)
            {
                memcpy(name + *length, "::", 2);
                *length += 2;
            }
            else
            {
                const IdentId id = tokens->values[i];
                const uint8_t* bytes = intern_bytes(pp->table, id);
                uint32_t n = intern_length(pp->table, id);
                if (n > 200)
                {
                    n = 200;
                }
                const bool last = i + 1 == end;
                if (attribute && last && n > 4 && bytes[0] == '_' && bytes[1] == '_' && bytes[n - 1] == '_' && bytes[n - 2] == '_')
                {
                    bytes += 2;
                    n -= 4;
                }
                memcpy(name + *length, bytes, n);
                *length += n;
            }
        }
    }
    else if (builtin == PP_BUILTIN_HAS_WARNING && end - begin >= 1)
    {
        // __has_warning("-Wname"): the strings' contents (adjacent strings joined), from their spellings; a raw string
        // literal's is its body.
        result = true;
        for (uint32_t i = begin; result && i < end; i += 1)
        {
            const uint32_t spelled = tokens->values[i] - tokens->locations[i];
            result = tokens->kinds[i] == C_TOKEN_STRING_LITERAL && spelled >= 2 && *length + spelled < 256;
            Scratch scratch = scratch_begin(0);
            uint8_t* buffer = result ? arena_push(scratch.arena, (uint64_t)spelled + 1, 1) : 0;
            result = buffer != 0;
            if (result)
            {
                const uint8_t* spelling;
                const uint32_t length_spelled = pp_spelling(pp, tokens->kinds[i], tokens->values[i], tokens->locations[i], buffer, &spelling);
                const uint8_t* body = spelling + 1;
                uint32_t body_length = length_spelled - 2;
                pp_raw_string_body(spelling, length_spelled, &body, &body_length);
                memcpy(name + *length, body, body_length);
                *length += body_length;
            }
            scratch_end(scratch);
        }
    }
    return result;
}

// _Pragma("..."): the string destringized (C 6.10.9), lexed and handled as a #pragma line's tokens. Returns whether a
// capture expands them (pp_pragma), which gives pp->arguments back to `restore` when it ends.
static bool pp_pragma_string(Preprocessor* pp, PPToken string, uint32_t name_location, uint32_t restore)
{
    bool result = false;
    Scratch scratch = scratch_begin(0);
    const uint32_t spelled = string.value - string.location;
    uint8_t* buffer = arena_push(scratch.arena, (uint64_t)spelled + 1, 1);
    uint8_t* text = arena_push(scratch.arena, (uint64_t)spelled + 1, 1);
    if (buffer != 0 && text != 0)
    {
        const uint8_t* spelling;
        const uint32_t length = pp_spelling(pp, string.kind, string.value, string.location, buffer, &spelling);
        uint32_t open_quote = 0;
        while (open_quote < length && spelling[open_quote] != '"')
        {
            open_quote += 1;
        }
        // A raw string literal's body is as written (clang takes off its prefix, delimiters and parentheses); another
        // string loses its escaped quotes' and backslashes' escapes.
        const uint8_t* body;
        uint32_t body_length;
        const bool raw = pp_raw_string_body(spelling, length, &body, &body_length);
        uint32_t n = 0;
        if (raw)
        {
            memcpy(text, body, body_length);
            n = body_length;
        }
        for (uint32_t i = open_quote + 1; !raw && i + 1 < length; i += 1)
        {
            if (spelling[i] == '\\' && i + 2 < length && (spelling[i + 1] == '"' || spelling[i + 1] == '\\'))
            {
                i += 1;
            }
            text[n] = spelling[i];
            n += 1;
        }
        // Clang lexes the text with a newline after it (prepare_PragmaString), which splices away a `\` that ends it.
        text[n] = '\n';
        n += 1;
        const uint32_t location = pp_scratch(pp, text, n);
        const uint32_t begin = pp->arguments.count;
        bool comment = false;
        const bool lexed = pp_lex_scratch(pp, location, n, &pp->arguments, 0, &comment) != BUSTER_PP_NONE;
        if (comment)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "unterminated /* comment");
        }
        const uint32_t emitted = pp->output.count;
        result = lexed && pp_pragma(pp, begin, pp->arguments.count, name_location, true, false, restore);
        if (lexed && !result && pp->options.expansion_starts && pp->capture_count == 0 && pp->output.count == emitted &&
            pp->silent_pragma_count < (BUSTER_PP_MAX_SILENT_PRAGMAS >> pp->capacity_shift))
        {
            pp->silent_pragmas[pp->silent_pragma_count] = (PPExpansionStart){ .token = emitted, .location = pp->expansion_location };
            pp->silent_pragma_count += 1;
        }
        pp->arguments.count = result ? pp->arguments.count : begin;
    }
    scratch_end(scratch);
    return result;
}

// Dependency observations are semantic reads: an active include (even guard/once-skipped), or a successful
// __has_include probe. Speculative cache opens do not reach this helper. The index lives in the unit's data copy,
// deduplicating aliases by the cache's device/inode identity; the first spelling and first-observation order survive.
static void pp_dependency_observe(Preprocessor* pp, uint32_t file, uint32_t parent)
{
    if (pp->dependencies != NULL && file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE)
    {
        PPFileData* data = &pp->datas[pp->files[file].data];
        if (data->record != BUSTER_PP_NONE)
        {
            const uint32_t note = parent != BUSTER_PP_NONE ? pp->ranges[parent].last_line_directive : BUSTER_PP_NONE;
            const uint8_t inherited = parent == BUSTER_PP_NONE ? BUSTER_PP_USER_FILE : note != BUSTER_PP_NONE ? pp->line_directives[note].system : pp->ranges[parent].system;
            const bool system = pp->files[file].system != BUSTER_PP_USER_FILE || inherited != BUSTER_PP_USER_FILE;
            if (data->dependency == 0)
            {
                // At most one observation per data record, whose capacity admission already bounded.
                const uint32_t index = pp->dependency_count++;
                data->dependency = index + 1;
                pp->dependencies[index] = (DependencyFile){ .path = dependency_prerequisite(pp->files[file].path), .used = true, .system = system };
            }
            else
            {
                pp->dependencies[data->dependency - 1].system &= system;
            }
        }
    }
}

// A feature query's or __has_include's answer, from its operand's tokens [first, last) of `tokens` (expanded, unless
// __has_include's is a header name as written). A place __has_include's search can't look in is clang's fatal error at
// the name, as #include's is (pp_find_include).
static uint64_t pp_builtin_value(Preprocessor* pp, PPBuiltin builtin, const PPTokens* tokens, uint32_t first, uint32_t last, uint32_t location)
{
    uint64_t result = 0;
    const bool include = builtin == PP_BUILTIN_HAS_INCLUDE || builtin == PP_BUILTIN_HAS_INCLUDE_NEXT;
    uint8_t text[512];
    uint32_t length;
    bool angled;
    if ((include || builtin == PP_BUILTIN_HAS_EMBED) && !pp_in_condition(pp))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "%s must be used within a preprocessing directive", include ? "__has_include" : "__has_embed");
    }
    else if (builtin == PP_BUILTIN_HAS_EMBED)
    {
        result = pp_has_embed(pp, tokens, first, last, location);
    }
    else if (include && pp_header_name(pp, tokens, first, last, text, sizeof(text), &length, &angled))
    {
        const PPContext* includer = pp_current_file_context(pp);
        const uint32_t saved_origin = pp->diagnostic_origin;
        pp->diagnostic_origin = tokens->origins != 0 ? tokens->origins[first] : 0;
        uint32_t found_in;
        const uint32_t file = pp_find_include(pp, text, length, angled, builtin == PP_BUILTIN_HAS_INCLUDE_NEXT, includer != 0 ? includer->file : BUSTER_PP_NONE,
                                              includer != 0 ? includer->search_index : BUSTER_PP_NONE, &found_in, tokens->locations[first]);
        pp->diagnostic_origin = saved_origin;
        result = file != BUSTER_PP_NONE;
        pp_dependency_observe(pp, file, includer != NULL ? includer->range : BUSTER_PP_NONE);
    }
    else if (include)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected \"FILENAME\" or <FILENAME>");
    }
    else if (builtin == PP_BUILTIN_IS_IDENTIFIER && last == first + 1)
    {
        // 1 for an identifier that isn't a keyword in the language mode (a macro's name too: it isn't expanded), and 0
        // for a keyword or another token, as clang answers.
        result = tokens->kinds[first] == C_TOKEN_RAW_IDENTIFIER && !c_name_is_keyword(tokens->values[first], pp->options.language);
    }
    else if (builtin == PP_BUILTIN_IS_IDENTIFIER)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "__is_identifier takes one token");
    }
    else if (pp_feature_name(pp, builtin, tokens, first, last, text, &length))
    {
        result = pp_feature(&pp->options, builtin, text, length);
        // The target's builtins (x86_target.c) as clang answers them, in the oracle's mode: buster implements none yet (#148).
        if (builtin == PP_BUILTIN_HAS_BUILTIN && result == 0 && pp->options.clang_features)
        {
            result = x86_target_has_builtin(&pp->options.target, text, length);
        }
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "builtin feature check macro requires a parenthesized identifier");
    }
    return result;
}

// Whether an argument is being expanded (a capture of one is open), when clang doesn't run a _Pragma
// (Preprocessor::Handle_Pragma, in InMacroArgPreExpansion) but checks it and gives its tokens back (pp_defer_pragma),
// so that it runs when the argument's substitution is rescanned, and is stringized and pasted as tokens before that.
static bool pp_expanding_argument(const Preprocessor* pp)
{
    bool result = false;
    for (uint32_t c = 0; !result && c < pp->capture_count; c += 1)
    {
        result = pp->capture_frames[c].kind == BUSTER_PP_CAPTURE_ARGUMENT;
    }
    return result;
}

// A _Pragma's tokens given back in an argument's expansion: its name, its '(', its string literal (its operand
// expanded) and its ')'.
static void pp_defer_pragma(Preprocessor* pp, uint32_t name_location, uint8_t name_flags, PPToken open, PPToken string, PPToken close)
{
    // The operator will be expanded again after substitution. Preserve its incoming origin, rather than the
    // temporary builtin expansion, so a macro-body operator still diagnoses the original argument invocation.
    pp_emit(pp, (PPToken)
    {
        .kind = C_TOKEN_RAW_IDENTIFIER,
        .flags = name_flags,
        .value = C_NAME_PRAGMA_OPERATOR,
        .location = name_location,
        .origin = pp->diagnostic_origin,
    });
    pp_emit(pp, open);
    pp_emit(pp, string);
    pp_emit(pp, close);
}

// Starts a builtin that takes a parenthesized operand (the feature queries, __has_include): the main loop
// collects the operand (pp_collect_step, as an invocation's arguments, from its '(' on), then pp_close_builtin takes it.
static void pp_start_builtin(Preprocessor* pp, PPMacroId id, PPToken name, uint8_t flags, uint32_t expansion_end)
{
    bool valid = pp->invocation_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift);
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, name.location, "macro expansion nests too deeply");
    }
    const PPInvocation invocation = {
        .macro = id,
        .history = pp->history_current,
        .arguments = pp->argument_record_count,
        .arguments_begin = pp->arguments.count,
        .captures_begin = pp->captures.count,
        .expansion_end = expansion_end,
        .name_location = name.location,
        .name_flags = flags,
        .collecting = true,
        .builtin = true,
        .capture_mark = pp->capture_count,
    };
    if (valid && pp_new_argument(pp))
    {
        pp->invocations[pp->invocation_count] = invocation;
        pp->invocation_count += 1;
    }
}

// A builtin's operand is collected (`closed`: its ')' was read): attribute queries expand their operand through
// pp_finish_builtin, name queries read it literally, and __has_include does either.
// An unterminated or malformed operand is an error, and a query's answer then 0.
static void pp_close_builtin(Preprocessor* pp, bool closed)
{
    const PPInvocation invocation = pp->invocations[pp->invocation_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = invocation.history;
    pp->history_anchor = invocation.name_location;
    pp->diagnostic_origin = invocation.history != 0 ? pp->history[invocation.history - 1].parent : 0;
    pp->invocation_count -= 1;
    pp->unterminated += !closed;
    pp->argument_record_count = invocation.arguments;
    const PPBuiltin builtin = (PPBuiltin)pp->macros[invocation.macro].builtin;
    const uint32_t begin = invocation.arguments_begin;
    const uint32_t end = pp->arguments.count;
    const IdentId name = pp->macros[invocation.macro].name;
    const PPTokens* tokens = &pp->arguments;
    const bool include = builtin == PP_BUILTIN_HAS_INCLUDE || builtin == PP_BUILTIN_HAS_INCLUDE_NEXT || builtin == PP_BUILTIN_HAS_EMBED;
    // A header name in <...> or a string literal is read as it is, and so are the operands clang reads unexpanded
    // (EvaluateFeatureLikeBuiltinMacro without ExpandArgs): the queries' that come here (an attribute query's
    // operand is a stream, pp_start_stream, and one comes here only without its '('). A computed header name is
    // expanded first.
    const bool direct = (include && begin < end && (tokens->kinds[begin] == C_TOKEN_LESS || tokens->kinds[begin] == C_TOKEN_STRING_LITERAL)) || !include;
    const PPCapture capture = {
        .kind = BUSTER_PP_CAPTURE_BUILTIN,
        .history = invocation.history,
        .arguments_begin = begin,
        .builtin = (uint8_t)builtin,
        .flags = invocation.name_flags,
        .location = invocation.name_location,
    };
    bool started = false;
    uint64_t value = 0;
    if (!closed)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, invocation.name_location, "missing '(' or ')' after '%.*s'", (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
    }
    else if (direct)
    {
        value = pp_builtin_value(pp, builtin, tokens, begin, end, invocation.name_location);
    }
    else
    {
        started = pp_start_capture(pp, capture, begin, end, invocation.expansion_end);
    }
    if (!started)
    {
        pp->arguments.count = begin;
        pp_emit(pp, include ? pp_number_token(pp, value, invocation.name_flags) : pp_feature_token(pp, value, invocation.name_flags));
    }
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// The barrier over a feature query's (or __has_include's) operand ended: the capture is the operand expanded.
static void pp_finish_builtin(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = capture.history;
    pp->history_anchor = capture.location;
    pp->diagnostic_origin = capture.history != 0 ? pp->history[capture.history - 1].parent : 0;
    pp->capture_count -= 1;
    const uint64_t value = pp_builtin_value(pp, (PPBuiltin)capture.builtin, &pp->captures, capture.begin, pp->captures.count, capture.location);
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
    // __has_embed's 2 has no L (clang prints its answer as an int).
    const bool numeric = capture.builtin == PP_BUILTIN_HAS_INCLUDE || capture.builtin == PP_BUILTIN_HAS_INCLUDE_NEXT || capture.builtin == PP_BUILTIN_HAS_EMBED;
    pp_emit(pp, numeric ? pp_number_token(pp, value, capture.flags) : pp_feature_token(pp, value, capture.flags));
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// Writes `token` at `index` of `tokens`, with its origin: in a temporary buffer's column, or in the output's history
// (pp_history_fill).
BUSTER_INLINE void pp_tokens_put(Preprocessor* pp, PPTokens* tokens, uint32_t index, PPToken token)
{
    pp_tokens_set(tokens, index, token.kind, token.flags, token.value, token.location);
    pp_history_fill(pp, tokens, index, index + 1, token.origin);
}

// An attribute query's '(' was read (pp_collect_token): its invocation becomes a stream, a capture without a barrier,
// and the main loop goes on expanding into it, as clang reads the operand with Lex, until pp_stream_check finds the ')'
// that closes it, wherever that comes from.
static void pp_start_stream(Preprocessor* pp)
{
    const PPInvocation invocation = pp->invocations[pp->invocation_count - 1];
    pp->invocation_count -= 1;
    pp->argument_record_count = invocation.arguments;
    pp->arguments.count = invocation.arguments_begin;
    if (pp->capture_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift))
    {
        pp->capture_frames[pp->capture_count] = (PPCapture){
            .kind = BUSTER_PP_CAPTURE_STREAM,
            .history = invocation.history,
            .begin = pp->captures.count,
            .builtin = pp->macros[invocation.macro].builtin,
            .flags = invocation.name_flags,
            .location = invocation.name_location,
            .pragma_mark = pp->pragma_lines,
            .depth = 1,
            .scanned = pp->captures.count,
            .stage = BUSTER_PP_STREAM_FIRST,
        };
        pp->capture_count += 1;
        pp->stream_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, invocation.name_location, "macro expansion nests too deeply");
    }
}

// An expansion is read from a file into the output, from the name at `location`: its tokens start at the output's end
// (PPExpansionStart). One that gives none is replaced by the next.
BUSTER_INLINE void pp_note_expansion(Preprocessor* pp, uint32_t location)
{
    const uint32_t token = pp->output.count;
    const uint32_t count = pp->expansion_start_count;
    const bool replaces = count != 0 && pp->expansion_starts[count - 1].token == token;
    pp->expansion_starts[replaces ? count - 1 : count] = (PPExpansionStart){ .token = token, .location = location };
    pp->expansion_start_count = replaces ? count : count + 1;
}

// Moves the pragma lines that a _Pragma or a #pragma among the top capture's tokens emitted into it (clang prints them
// as it reads them, and the line or the operand being read keeps the rest) to the target below, before the capture's
// other tokens, in order: into the output, or onto the end of the capture below, whose tokens end where this one's
// begin.
static void pp_lift_pragma_lines(Preprocessor* pp)
{
    PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    if (capture->pragma_mark != pp->pragma_lines)
    {
        PPTokens* tokens = &pp->captures;
        const uint32_t begin = capture->begin;
        const uint32_t end = tokens->count;
        Scratch scratch = scratch_begin(0);
        PPToken* kept = arena_push(scratch.arena, (uint64_t)(end - begin + 1) * sizeof(PPToken), _Alignof(PPToken));
        if (kept != 0)
        {
            // A stable partition: the pragma lines' tokens move to the front, and the others after them.
            uint32_t lifted = begin;
            uint32_t count = 0;
            uint32_t state = BUSTER_PP_PRAGMA_OUTSIDE;
            for (uint32_t i = begin; i < end; i += 1)
            {
                const PPToken token = pp_stack_token(tokens, i);
                state = pp_pragma_state(pp, state, token.kind, token.location);
                if (state != BUSTER_PP_PRAGMA_OUTSIDE || token.kind == BUSTER_PP_KIND_PRAGMA_END)
                {
                    pp_tokens_put(pp, tokens, lifted, token);
                    lifted += 1;
                }
                else
                {
                    kept[count] = token;
                    count += 1;
                }
            }
            if (pp->capture_count == 1 && pp_room(pp, &pp->output, lifted - begin))
            {
                PPTokens* output = &pp->output;
                for (uint32_t i = begin; i < lifted; i += 1)
                {
                    pp_tokens_put(pp, output, output->count, pp_stack_token(tokens, i));
                    output->count += 1;
                }
                if (pp->displaced_location != 0)
                {
                    pp_note_expansion(pp, pp->displaced_location);
                    pp->displaced_location = 0;
                }
                for (uint32_t k = 0; k < count; k += 1)
                {
                    pp_tokens_put(pp, tokens, begin + k, kept[k]);
                }
                tokens->count = begin + count;
            }
            else if (pp->capture_count != 1)
            {
                for (uint32_t k = 0; k < count; k += 1)
                {
                    pp_tokens_put(pp, tokens, lifted + k, kept[k]);
                }
                capture->begin = lifted;
            }
        }
        scratch_end(scratch);
        capture->pragma_mark = pp->pragma_lines;
    }
}

// A stream ended: at its operand's ')', index `close` of pp->captures, or unterminated (BUSTER_PP_NONE) at the end of
// a barrier or of the input, which clang reports and answers nothing for. Its pragma lines go first, then the answer
// replaces the operand, and the tokens read after the ')' in the same step follow it: they are read as they would be
// without the stream (a run's or an expansion's tokens that name no macro).
// _Pragma: a stream (BUSTER_PP_STREAM_PRAGMA_OPEN) that the main loop expands into, as clang's Handle_Pragma reads its
// '(', its string literal and its ')' with Lex, wherever they come from; pp_stream_check reads them, and
// pp_end_pragma_stream runs the pragma.
static void pp_start_pragma_stream(Preprocessor* pp, PPToken name, uint8_t flags)
{
    if (pp->capture_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift))
    {
        pp->capture_frames[pp->capture_count] = (PPCapture){
            .kind = BUSTER_PP_CAPTURE_STREAM,
            .history = pp->history_current,
            .begin = pp->captures.count,
            .builtin = PP_BUILTIN_PRAGMA,
            .flags = flags,
            .location = name.location,
            .pragma_mark = pp->pragma_lines,
            .scanned = pp->captures.count,
            .stage = BUSTER_PP_STREAM_PRAGMA_OPEN,
        };
        pp->capture_count += 1;
        pp->stream_count += 1;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, name.location, "macro expansion nests too deeply");
    }
}

// A _Pragma's stream ended (pp_stream_check), `valid` after its ')', or after a token it doesn't take, which with the
// rest of the `back` tokens after it are read next, as clang's Handle_Pragma leaves the token after its operand to Lex.
// A valid one's pragma runs (pp_pragma_string), or in an argument's expansion its tokens are given back
// (pp_defer_pragma). The pragma lines of directives among its tokens go first (pp_lift_pragma_lines).
static void pp_end_pragma_stream(Preprocessor* pp, bool valid, uint32_t back)
{
    // The tokens given back are saved and taken off first, a pragma line among them too, since lifting the pragma lines
    // before them moves the capture's tokens, and the pragma's line may take their places.
    const uint32_t end = pp->captures.count;
    Scratch scratch = scratch_begin(0);
    PPToken* rest = back != 0 ? arena_push(scratch.arena, (uint64_t)back * sizeof(PPToken), _Alignof(PPToken)) : 0;
    for (uint32_t i = 0; rest != 0 && i < back; i += 1)
    {
        rest[i] = pp_stack_token(&pp->captures, end - back + i);
    }
    pp->captures.count = end - back;
    pp_lift_pragma_lines(pp);
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = capture.history;
    pp->history_anchor = capture.location;
    pp->diagnostic_origin = capture.history != 0 ? pp->history[capture.history - 1].parent : 0;
    pp->capture_count -= 1;
    pp->stream_count -= 1;
    // The operand, `(` "string" `)`, is the capture's tokens.
    const uint32_t operand_end = pp->captures.count;
    const PPToken open = valid ? pp_stack_token(&pp->captures, capture.begin) : (PPToken){ 0 };
    const PPToken string = valid ? pp_stack_token(&pp->captures, capture.begin + 1) : (PPToken){ 0 };
    const PPToken close = valid ? pp_stack_token(&pp->captures, operand_end - 1) : (PPToken){ 0 };
    pp->captures.count = capture.begin;
    if (valid && pp_expanding_argument(pp))
    {
        pp_defer_pragma(pp, capture.location, capture.flags, open, string, close);
    }
    else if (valid)
    {
        pp_pragma_string(pp, string, capture.location, pp->arguments.count);
    }
    for (uint32_t i = 0; rest != 0 && i < back; i += 1)
    {
        pp_emit(pp, rest[i]);
    }
    scratch_end(scratch);
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

static void pp_finish_stream(Preprocessor* pp, uint32_t close)
{
    // The tokens read after the ')' are saved and taken off first, a pragma line among them too, as
    // pp_end_pragma_stream does: lifting the pragma lines before them moves the capture's tokens.
    const uint32_t end = pp->captures.count;
    const uint32_t after = close != BUSTER_PP_NONE ? close + 1 : end;
    const uint32_t back = end - after;
    Scratch scratch = scratch_begin(0);
    PPToken* rest = back != 0 ? arena_push(scratch.arena, (uint64_t)back * sizeof(PPToken), _Alignof(PPToken)) : 0;
    for (uint32_t i = 0; rest != 0 && i < back; i += 1)
    {
        rest[i] = pp_stack_token(&pp->captures, after + i);
    }
    pp->captures.count = after;
    pp_lift_pragma_lines(pp);
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = capture.history;
    pp->history_anchor = capture.location;
    pp->diagnostic_origin = capture.history != 0 ? pp->history[capture.history - 1].parent : 0;
    pp->capture_count -= 1;
    pp->stream_count -= 1;
    uint64_t value = 0;
    if (close == BUSTER_PP_NONE)
    {
        pp->unterminated += 1;
        pp_diagnose(pp, PP_SEVERITY_ERROR, capture.location, "unterminated function-like macro invocation");
    }
    else
    {
        // The operand, without its ')'.
        value = pp_builtin_value(pp, (PPBuiltin)capture.builtin, &pp->captures, capture.begin, pp->captures.count - 1, capture.location);
    }
    pp->captures.count = capture.begin;
    if (close != BUSTER_PP_NONE)
    {
        pp_emit(pp, pp_feature_token(pp, value, capture.flags));
    }
    for (uint32_t i = 0; rest != 0 && i < back; i += 1)
    {
        pp_emit(pp, rest[i]);
    }
    scratch_end(scratch);
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// The top stream ends without its ')', at the end of a line, of an argument or of the main file, where clang's Lex
// gives an eod or an eof: an attribute query's is unterminated (pp_finish_stream), and a _Pragma's malformed (once).
static void pp_cut_stream(Preprocessor* pp)
{
    const PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    if (capture->builtin == PP_BUILTIN_PRAGMA)
    {
        if (capture->stage != BUSTER_PP_STREAM_PRAGMA_SKIP)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, capture->location, "_Pragma takes a parenthesized string literal");
        }
        pp->unterminated += 1;
        pp_end_pragma_stream(pp, false, 0);
    }
    else
    {
        pp_finish_stream(pp, BUSTER_PP_NONE);
    }
}

// A stream's first identifier, __has_c_attribute's or __has_cpp_attribute's, is followed by a token clang reads
// unexpanded (to see a `::`): a macro's name next in the top context is taken as it is, painted. Any other token is read
// alike either way, and the main loop reads it; at a directive or the end of a context, the main loop goes first (it
// runs the directive, or pops the context), and the stream waits.
static void pp_stream_unexpanded(Preprocessor* pp)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    PPToken token = { 0 };
    bool available = false;
    if (context->kind == BUSTER_PP_CONTEXT_FILE)
    {
        const PPFileData* data = &pp->datas[context->data];
        available = context->position < pp_run_limit(data, context);
        token = available ? pp_file_token(data, pp->ranges[context->range].base, context->position) : token;
    }
    else
    {
        available = context->position < context->end;
        token = available ? pp_context_token(pp, context, context->position) : token;
    }
    const bool name = available && token.kind == C_TOKEN_RAW_IDENTIFIER && !(token.flags & BUSTER_PP_NO_EXPAND) && pp_is_macro(pp, token.value);
    if (name)
    {
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            pp_check_poison(pp, token.value, token.location);
        }
        context->position += 1;
        token.flags = (uint8_t)(pp_with_pending(token.flags, context->pending_flags, context->kind == BUSTER_PP_CONTEXT_FILE) | BUSTER_PP_NO_EXPAND);
        context->pending_flags = 0;
        pp_emit(pp, token);
    }
    else if (available)
    {
        pp->capture_frames[pp->capture_count - 1].stage = BUSTER_PP_STREAM_REST;
    }
}

// Reads an attribute query's stream's new tokens, but its pragma lines, counting parentheses as clang does (a ',' and
// any token after the first are errors that pp_builtin_value finds), and ends it at the ')' that closes its operand
// (pp_finish_stream). Returns whether the stream below is to be read too.
static bool pp_check_query_stream(Preprocessor* pp)
{
    PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    const PPTokens* tokens = &pp->captures;
    const bool scoped = capture->builtin == PP_BUILTIN_HAS_C_ATTRIBUTE || capture->builtin == PP_BUILTIN_HAS_CPP_ATTRIBUTE;
    uint32_t close = BUSTER_PP_NONE;
    uint32_t i = capture->scanned;
    bool result = false;
    while (close == BUSTER_PP_NONE && i < tokens->count)
    {
        const uint8_t kind = tokens->kinds[i];
        const bool pragma = capture->in_pragma || (kind == C_TOKEN_HASH && tokens->locations[i] == pp->hash_location);
        capture->in_pragma = pragma && kind != BUSTER_PP_KIND_PRAGMA_END;
        if (!pragma)
        {
            const bool paren = kind == C_TOKEN_L_PAREN || kind == C_TOKEN_R_PAREN || kind == C_TOKEN_COMMA;
            const bool identifier = pp_names_kind(kind);
            if (capture->stage == BUSTER_PP_STREAM_UNEXPANDED)
            {
                capture->stage = BUSTER_PP_STREAM_REST;
            }
            else if (capture->stage == BUSTER_PP_STREAM_FIRST && !paren)
            {
                capture->stage = scoped && identifier ? BUSTER_PP_STREAM_UNEXPANDED : BUSTER_PP_STREAM_REST;
            }
            capture->depth += kind == C_TOKEN_L_PAREN;
            capture->depth -= kind == C_TOKEN_R_PAREN;
            close = capture->depth == 0 ? i : BUSTER_PP_NONE;
        }
        i += 1;
    }
    capture->scanned = i;
    if (close != BUSTER_PP_NONE)
    {
        pp_finish_stream(pp, close);
        result = true;
    }
    else if (capture->stage == BUSTER_PP_STREAM_UNEXPANDED && pp->context_count != 0)
    {
        // (With no context left, the main file ended, and pp_run_file ends the stream.)
        const uint32_t count = tokens->count;
        pp_stream_unexpanded(pp);
        result = tokens->count != count;
    }
    return result;
}

// Reads a _Pragma's stream's new tokens, but its pragma lines: a '(', a string literal and a ')' (each malformed
// operand is reported once, at the _Pragma), and ends it at its ')' or at the token it doesn't take
// (pp_end_pragma_stream). After another token where the string literal goes, that token and the next ones up to a ')'
// go, or up to a line's first token, which it doesn't take. Returns whether the stream below is to be read too.
static bool pp_check_pragma_stream(Preprocessor* pp)
{
    PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    const PPTokens* tokens = &pp->captures;
    uint32_t i = capture->scanned;
    bool ended = false;
    bool valid = false;
    uint32_t next = 0; // The first token it doesn't take.
    while (!ended && i < tokens->count)
    {
        const uint8_t kind = tokens->kinds[i];
        const bool pragma = capture->in_pragma || (kind == C_TOKEN_HASH && tokens->locations[i] == pp->hash_location);
        capture->in_pragma = pragma && kind != BUSTER_PP_KIND_PRAGMA_END;
        if (!pragma)
        {
            const uint8_t stage = capture->stage;
            const bool string = kind >= C_TOKEN_STRING_LITERAL && kind <= C_TOKEN_UTF32_STRING_LITERAL;
            const bool line_start = (tokens->flags[i] & BUSTER_PP_START_OF_LINE) != 0;
            const bool taken = (stage == BUSTER_PP_STREAM_PRAGMA_OPEN && kind == C_TOKEN_L_PAREN) || (stage == BUSTER_PP_STREAM_PRAGMA_STRING && string) ||
                               (stage == BUSTER_PP_STREAM_PRAGMA_CLOSE && kind == C_TOKEN_R_PAREN);
            if (!taken && stage != BUSTER_PP_STREAM_PRAGMA_SKIP)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, capture->location, "_Pragma takes a parenthesized string literal");
            }
            valid = taken && stage == BUSTER_PP_STREAM_PRAGMA_CLOSE;
            // Without a string literal, a ')' ends it, and any other token goes; past that, a line's first token ends it too.
            const bool skipping = stage == BUSTER_PP_STREAM_PRAGMA_SKIP || (!taken && stage == BUSTER_PP_STREAM_PRAGMA_STRING);
            const bool kept = skipping && stage == BUSTER_PP_STREAM_PRAGMA_SKIP && kind != C_TOKEN_R_PAREN && line_start;
            ended = valid || (!taken && !skipping) || (skipping && (kind == C_TOKEN_R_PAREN || kept));
            next = (!taken && !skipping) || kept ? i : i + 1;
            capture->stage = taken ? (uint8_t)(stage + 1) : skipping ? BUSTER_PP_STREAM_PRAGMA_SKIP : stage;
        }
        i += 1;
    }
    capture->scanned = i;
    if (ended)
    {
        pp_end_pragma_stream(pp, valid, tokens->count - next);
    }
    return ended;
}

// After a step of the main loop with a stream open: reads the top stream's new tokens, which may end it, and then the
// stream below it. Kept out of the main loop's code, which only tests whether a stream is open.
static __attribute__((noinline, cold)) void pp_stream_check(Preprocessor* pp)
{
    bool more = true;
    while (more && pp->capture_count != 0 && pp->capture_frames[pp->capture_count - 1].kind == BUSTER_PP_CAPTURE_STREAM)
    {
        more = pp->capture_frames[pp->capture_count - 1].builtin == PP_BUILTIN_PRAGMA ? pp_check_pragma_stream(pp) : pp_check_query_stream(pp);
    }
}

// The fixed macro spellings need a four-digit year, valid table indices and bounded numeric fields. Check only
// fields that the formatters read; libc supplies the calendar's consistency (including leap seconds).
static bool pp_calendar_valid(const struct tm* calendar)
{
    return calendar->tm_year >= -1900 && calendar->tm_year <= 8099 && calendar->tm_mon >= 0 && calendar->tm_mon < 12 &&
           calendar->tm_wday >= 0 && calendar->tm_wday < 7 && calendar->tm_mday >= 1 && calendar->tm_mday <= 31 &&
           calendar->tm_hour >= 0 && calendar->tm_hour < 24 && calendar->tm_min >= 0 && calendar->tm_min < 60 &&
           calendar->tm_sec >= 0 && calendar->tm_sec <= 60;
}

// Seconds at the environment/stat boundary, before narrowing to libc's time_t. Never inspect a failed conversion's
// output: gmtime_r/localtime_r may leave it untouched. This cold metadata path is shared by both preprocessing modes.
static bool pp_calendar(Preprocessor* pp, int64_t seconds, bool utc, uint32_t location, const char* source, struct tm* calendar)
{
    const time_t value = (time_t)seconds;
    bool result = (int64_t)value == seconds;
    if (result)
    {
        result = (utc ? gmtime_r(&value, calendar) : localtime_r(&value, calendar)) != 0;
    }
    if (!result)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, location, "can't convert %s to a calendar time", source);
    }
    else if (!pp_calendar_valid(calendar))
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, location, "%s is outside the supported calendar range (years 0000 through 9999)", source);
        result = false;
    }
    return result;
}

// __DATE__'s and __TIME__'s text for a validated calendar: "Jan  1 2000" and "00:00:00".
static void pp_format_date_time(Preprocessor* pp, const struct tm* calendar)
{
    static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    snprintf(pp->date, sizeof(pp->date), "%s %2d %04d", months[calendar->tm_mon], calendar->tm_mday, calendar->tm_year + 1900);
    snprintf(pp->time, sizeof(pp->time), "%02d:%02d:%02d", calendar->tm_hour, calendar->tm_min, calendar->tm_sec);
}

// __DATE__'s and __TIME__'s text, made from the unit's time in local time at the first expansion of either that needs
// it (pp_initialize_time). False, with a fatal diagnostic at `where`, if the time can't be converted.
static bool pp_unit_time(Preprocessor* pp, uint32_t where)
{
    struct tm calendar;
    const bool made = pp->date[0] != 0;
    const bool result = made || pp_calendar(pp, pp->unit_seconds, false, where, "current time", &calendar);
    if (result && !made)
    {
        pp_format_date_time(pp, &calendar);
    }
    return result;
}

// __TIMESTAMP__'s text for a validated calendar: "Sat Jan  1 00:00:00 2000" (C locale).
static void pp_format_timestamp(char* text, uint32_t capacity, const struct tm* calendar)
{
    static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char days[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    snprintf(text, capacity, "%s %s %2d %02d:%02d:%02d %04d", days[calendar->tm_wday], months[calendar->tm_mon], calendar->tm_mday, calendar->tm_hour,
             calendar->tm_min, calendar->tm_sec, calendar->tm_year + 1900);
}

static void pp_eval_method_use(Preprocessor* pp, PPToken name)
{
    if (pp->options.semantic_pragmas && !pp->fatal)
    {
        const uint32_t count = pp->eval_method_use_count;
        if (count == pp->eval_method_use_capacity)
        {
            const uint64_t capacity = count != 0 ? (uint64_t)count * 2 : 8;
            PPEvalMethodUse* records = capacity <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(&pp->arena, PPEvalMethodUse, capacity) : NULL;
            if (records == NULL)
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory for floating evaluation macro uses");
            }
            else
            {
                if (count != 0)
                {
                    memcpy(records, pp->eval_method_uses, (uint64_t)count * sizeof(PPEvalMethodUse));
                }
                pp->eval_method_uses = records;
                pp->eval_method_use_capacity = (uint32_t)capacity;
            }
        }
        if (!pp->fatal)
        {
            pp->eval_method_uses[count] = (PPEvalMethodUse)
            {
                .position = pp->output.count,
                .pragmas = pp->pragma_lines,
                .location = name.location,
                .origin = name.origin,
            };
            pp->eval_method_use_count = count + 1;
        }
    }
}

static void pp_expand_builtin(Preprocessor* pp, PPMacroId id, PPToken name, uint32_t expansion_end)
{
    const uint32_t saved_history = pp->history_current;
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->history_current = pp_history_expansion(pp, id, name.location, name.origin);
    pp->history_anchor = name.location;
    pp->diagnostic_origin = name.origin;
    const PPBuiltin builtin = (PPBuiltin)pp->macros[id].builtin;
    // The answer takes the name's flags, as clang's ExpandBuiltinMacro gives them: its line start among a macro's
    // arguments too (BUSTER_PP_LINE_SPACE), and pp_name_line's.
    const uint8_t flags = (uint8_t)((name.flags & (BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE)) | pp_name_line(pp, name.flags, name.location));
    // A name a macro made is at the end of its outermost expansion, the last token's place as clang's lexer has it (with
    // the line splices right before it); a name in the file at its first character.
    const uint32_t where = (name.flags & BUSTER_PP_FROM_MACRO) ? pp_lexed_start(pp, expansion_end) : name.location;
    const char* file_name;
    uint32_t file_name_length;
    switch (builtin)
    {
        case PP_BUILTIN_FILE:
        case PP_BUILTIN_FILE_NAME:
        {
            pp_presumed(pp, where, &file_name, &file_name_length);
            const char* component = file_name;
            const uint32_t component_length = builtin == PP_BUILTIN_FILE_NAME ? pp_path_file_name(file_name, file_name_length, &component) : file_name_length;
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)component, component_length, flags));
            break;
        }
        case PP_BUILTIN_BASE_FILE:
        {
            // The presumed name at the root of the include stack, walking up from the name as clang does: #line in the
            // main file changes it, and a forced include's root is the <built-in> file that includes it.
            uint32_t at = where;
            uint32_t range = pp_find_range(pp, at);
            while (range != BUSTER_PP_NONE && pp->ranges[range].parent != BUSTER_PP_NONE)
            {
                at = pp->ranges[range].include_location;
                range = pp->ranges[range].parent;
            }
            pp_presumed(pp, at, &file_name, &file_name_length);
            pp_emit(pp, pp_string_token(pp, (const uint8_t*)file_name, file_name_length, flags));
            break;
        }
        case PP_BUILTIN_LINE:
            pp_emit(pp, pp_number_token(pp, pp_presumed(pp, where, &file_name, &file_name_length), flags));
            break;
        case PP_BUILTIN_COUNTER:
            pp_emit(pp, pp_number_token(pp, pp->counter, flags));
            pp->counter += 1;
            break;
        case PP_BUILTIN_INCLUDE_LEVEL:
            pp_emit(pp, pp_number_token(pp, pp->include_depth, flags));
            break;
        case PP_BUILTIN_FLT_EVAL_METHOD:
            // The translation unit's: under -E, clang's `#pragma clang fp eval_method` doesn't change it.
            pp_eval_method_use(pp, name);
            pp_emit(pp, pp_number_token(pp, pp->flt_eval_method, flags));
            break;
        case PP_BUILTIN_DATE:
            if (pp_unit_time(pp, where))
            {
                pp_emit(pp, pp_string_token(pp, (const uint8_t*)pp->date, (uint32_t)strlen(pp->date), flags));
            }
            break;
        case PP_BUILTIN_TIME:
            if (pp_unit_time(pp, where))
            {
                pp_emit(pp, pp_string_token(pp, (const uint8_t*)pp->time, (uint32_t)strlen(pp->time), flags));
            }
            break;
        case PP_BUILTIN_TIMESTAMP:
        {
            // SOURCE_DATE_EPOCH's time when it is set, else the modification time of the file being read (where a
            // macro's expansion is, the file it is expanded in), as clang does: in local time, or ??? for a file the
            // preprocessor made.
            char text[32];
            bool valid = true;
            const PPContext* file_context = pp_current_file_context(pp);
            const PPFileData* data = file_context != 0 ? &pp->datas[file_context->data] : 0;
            if (pp->has_epoch)
            {
                snprintf(text, sizeof(text), "%s", pp->timestamp);
            }
            else if (data != 0 && data->has_modified)
            {
                struct tm calendar;
                valid = pp_calendar(pp, data->modified, false, where, "file modification time for __TIMESTAMP__", &calendar);
                if (valid)
                {
                    pp_format_timestamp(text, sizeof(text), &calendar);
                }
            }
            else
            {
                snprintf(text, sizeof(text), "??? ??? ?? ??:??:?? ????");
            }
            if (valid)
            {
                pp_emit(pp, pp_string_token(pp, (const uint8_t*)text, (uint32_t)strlen(text), flags));
            }
            break;
        }
        case PP_BUILTIN_PRAGMA:
            pp_start_pragma_stream(pp, name, flags);
            break;
        case PP_BUILTIN_POISONED:
            // The name, which pp_expand_current reported if a file has it.
            pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = name.flags | BUSTER_PP_NO_EXPAND, .value = name.value, .location = name.location, .origin = name.origin });
            break;
        case PP_BUILTIN_DEFINED:
            if (!pp_in_expression(pp))
            {
                // An identifier here. In a macro's argument that a #if line expands, which clang expands without
                // evaluating it (its names after `defined` too), it stays one the substitution's rescan in the line
                // reads as the operator.
                const uint8_t painted = pp_in_condition(pp) ? 0 : BUSTER_PP_NO_EXPAND;
                pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = (uint8_t)(name.flags | painted), .value = name.value, .location = name.location, .origin = name.origin });
            }
            else
            {
                // `defined X` or `defined ( X )`: X unexpanded.
                PPToken token;
                uint32_t end;
                bool valid = pp_read_token(pp, &token, &end);
                const bool parenthesized = valid && token.kind == C_TOKEN_L_PAREN;
                valid = parenthesized ? pp_read_token(pp, &token, &end) : valid;
                valid = valid && token.kind == C_TOKEN_RAW_IDENTIFIER;
                const bool defined = valid && pp_is_defined(pp, token.value);
                PPToken close;
                if (valid && parenthesized)
                {
                    valid = pp_read_token(pp, &close, &end) && close.kind == C_TOKEN_R_PAREN;
                }
                if (!valid)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, name.location, "macro name missing or malformed after 'defined'");
                }
                pp_emit(pp, pp_number_token(pp, defined, flags));
            }
            break;
        default:
            // The builtins that take a parenthesized argument and answer with a number.
            pp_start_builtin(pp, id, name, flags, expansion_end);
            break;
    }
    pp->history_current = saved_history;
    pp->diagnostic_origin = saved_origin;
}

// A run of `copied` tokens read from `context` was just copied to the target: its first token takes the context's
// pending flags, which are rare.
BUSTER_INLINE void pp_take_pending_flags(Preprocessor* pp, PPContext* context, uint32_t copied)
{
    if (context->pending_flags != 0 && copied != 0)
    {
        PPTokens* target = pp_target(pp);
        target->flags[target->count - copied] = pp_with_pending(target->flags[target->count - copied], context->pending_flags, context->kind == BUSTER_PP_CONTEXT_FILE);
        context->pending_flags = 0;
    }
}

// A name read from a #line's line, at `location`: the line's first token comes from the expansion of the last name read
// while its capture has no token outside pragma lines (a _Pragma's, which go before the line), as clang's line note is
// at that token's expansion.
static __attribute__((noinline, cold)) void pp_note_line_anchor(Preprocessor* pp, uint32_t location)
{
    PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    const PPTokens* tokens = &pp->captures;
    uint32_t state = BUSTER_PP_PRAGMA_OUTSIDE;
    bool empty = true;
    for (uint32_t i = capture->begin; empty && i < tokens->count; i += 1)
    {
        state = pp_pragma_state(pp, state, tokens->kinds[i], tokens->locations[i]);
        empty = state != BUSTER_PP_PRAGMA_OUTSIDE || tokens->kinds[i] == BUSTER_PP_KIND_PRAGMA_END;
    }
    capture->anchor = empty ? location : capture->anchor;
}

// Expands the macro whose name is the top context's current token. A stream open reads what came before the name first
// (pp_stream_check): it may end there, or take the name as it is.
static void pp_expand_current(Preprocessor* pp)
{
    PPContext* context = &pp->contexts[pp->context_count - 1];
    const uint32_t position = context->position;
    const uint32_t depth = pp->context_count;
    if (pp->stream_count != 0)
    {
        pp_stream_check(pp);
    }
    // A _Pragma that ended may have started a capture for its pragma's operands, which the main loop runs first.
    if (context->position == position && pp->context_count == depth)
    {
        PPToken name = pp_context_token(pp, context, context->position);
        if (context->pending_flags != 0)
        {
            name.flags = pp_with_pending(name.flags, context->pending_flags, context->kind == BUSTER_PP_CONTEXT_FILE);
            context->pending_flags = 0;
        }
        if (pp->options.expansion_starts && context->kind == BUSTER_PP_CONTEXT_FILE && pp->capture_count == 0)
        {
            // Every expansion read from a file starts where its first token goes (a token among its arguments that
            // started a line can start an output line too).
            pp->expansion_location = name.location;
            pp_note_expansion(pp, name.location);
            // The next token after a _Pragma that printed nothing comes from this expansion, if any.
            const uint32_t silent = pp->silent_pragma_count;
            if (silent != 0 && pp->silent_pragmas[silent - 1].token == pp->output.count)
            {
                pp->silent_pragmas[silent - 1].location = name.location;
            }
        }
        if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            pp_check_poison(pp, name.value, name.location);
        }
        else if (context->barrier)
        {
            PPCapture* top = &pp->capture_frames[pp->capture_count - 1];
            top->name_location = name.location;
            if (top->kind == BUSTER_PP_CAPTURE_LINE)
            {
                pp_note_line_anchor(pp, name.location);
            }
        }
        const PPMacroId id = pp->macro_of[name.value];
        PPMacro* macro = &pp->macros[id];
        const uint32_t expansion_end = pp_expansion_end(context, name);
        if (macro->disabled != 0)
        {
            // A macro disabled while a file is read can only come from a directive in an invocation's arguments.
            context->position += 1;
            name.flags |= BUSTER_PP_NO_EXPAND;
            pp_emit(pp, name);
        }
        else if (macro->flags & BUSTER_PP_MACRO_BUILTIN)
        {
            context->position += 1;
            pp->statistics.builtins += pp->options.timing;
            pp_expand_builtin(pp, id, name, expansion_end);
        }
        else if (!(macro->flags & BUSTER_PP_MACRO_FUNCTION))
        {
            context->position += 1;
            pp_expand_object(pp, id, name, expansion_end);
        }
        else if (pp_next_is_lparen(pp))
        {
            context->position += 1;
            pp_invoke(pp, id, name);
        }
        else
        {
            context->position += 1;
            pp_emit(pp, name);
        }
    }
}

// The end of the run a step of the main loop copies from `position` (up to `limit`): one token while a stream is open,
// so that a stream ends at the token that closes it, before any token after it is read: a _Pragma's pragma runs before
// them, as clang's does (it can change what they expand to). So too while a capture with a limit is open, which ends at
// its last token, before the next one expands (pp_check_limit).
BUSTER_INLINE uint32_t pp_step_limit(const Preprocessor* pp, uint32_t position, uint32_t limit)
{
    return (pp->stream_count | pp->limit_count) != 0 && limit - position > 1 ? position + 1 : limit;
}

// The expansion of the object-like macro whose name is the file context's current token, read in the range at `base`,
// as pp_expand_current and pp_expand_object make it when it goes straight into the target: when its body names no
// macro and has no ##, and nothing is pending or open that a name read from a file needs (pending flags, a stream,
// a capture with a limit, expansion starts for -E's markers, poisoned names). Returns false, having changed nothing,
// otherwise.
static bool pp_expand_file_direct(Preprocessor* pp, PPContext* context, const PPFileData* data, uint32_t base)
{
    const uint32_t position = context->position;
    const IdentId value = data->values[position];
    const PPMacroId id = pp->macro_of[value];
    const PPMacro* macro = &pp->macros[id];
    const PPFileData* body = &pp->datas[macro->data];
    PPTokens* target = pp_target(pp);
    bool result = (macro->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN | BUSTER_PP_MACRO_PASTES)) == 0 && macro->disabled == 0 &&
                  macro->body_begin < macro->body_end && (pp->stream_count | pp->limit_count | pp->poison_count | context->pending_flags) == 0 &&
                  !pp->options.expansion_starts && (uint64_t)target->count + (macro->body_end - macro->body_begin) <= target->capacity;
    for (uint32_t t = macro->body_begin; result && t < macro->body_end; t += 1)
    {
        result = (body->kinds[t] & 0x7F) != C_TOKEN_RAW_IDENTIFIER || !pp_is_macro_vector(pp, body->values[t]);
    }
    if (result)
    {
        context->position = position + 1;
        const uint32_t location = base + data->starts[position];
        const uint32_t saved_history = pp->history_current;
        pp->history_current = pp_history_expansion(pp, id, location, 0);
        const uint32_t begin = target->count;
        // The name's leading space and line start go to the first token (pp_expand_object).
        const uint8_t name_flags = (uint8_t)((pp_file_space(data, position) ? BUSTER_PP_SPACE : 0) | ((data->kinds[position] & BUSTER_C_TOKEN_START_OF_LINE) ? BUSTER_PP_START_OF_LINE : 0));
        if (macro->body_end - macro->body_begin == 1)
        {
            // Most bodies are one token (sqlite3.c's 12,431 of 13,197): pp_append_file_tokens' token with the name's flags,
            // in place of its own space, written without its 64 lanes.
            const uint32_t b = macro->body_begin;
            const uint8_t kind = body->kinds[b] & 0x7F;
            target->kinds[begin] = kind;
            target->flags[begin] = (uint8_t)(BUSTER_PP_FROM_MACRO | name_flags | BUSTER_PP_NAME_SPACE);
            target->values[begin] = kind == C_TOKEN_RAW_IDENTIFIER ? body->values[b] : macro->base + body->values[b];
            target->locations[begin] = macro->base + body->starts[b];
            target->count = begin + 1;
            pp_history_fill(pp, target, begin, begin + 1, pp->history_current);
        }
        else
        {
            pp_append_file_tokens(pp, target, body, macro->base, macro->body_begin, macro->body_end, true);
            uint8_t* flags = &target->flags[begin];
            *flags = (uint8_t)((*flags & ~(BUSTER_PP_SPACE | BUSTER_PP_START_OF_LINE | BUSTER_PP_LINE_SPACE | BUSTER_PP_NAME_LINE)) | name_flags | BUSTER_PP_NAME_SPACE);
        }
        if (pp->options.timing)
        {
            pp->statistics.direct_expansions += 1;
            pp->statistics.final_direct_object_tokens += target == &pp->output ? target->count - begin : 0;
        }
        pp->history_current = saved_history;
    }
    return result;
}

static void pp_step_file(Preprocessor* pp, PPContext* context)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t limit = pp_run_limit(data, context);
    if (context->position < limit)
    {
        const uint32_t base = pp->ranges[context->range].base;
        // A direct expansion leaves the context as the main loop's next step finds it: that step's run is copied here.
        bool more = true;
        while (more)
        {
            const uint32_t run_end = pp_step_limit(pp, context->position, limit);
            // A file's text read straight into the output: nothing open that a token read from a file goes to or is counted
            // by, no poisoned name, no raw string error to report, no -E markers' starts, nothing pending.
            const bool text = BUSTER_VECTOR_PATH(pp->vectorized) && (pp->capture_count | pp->stream_count | pp->limit_count | pp->poison_count | context->pending_flags) == 0 &&
                              !pp->options.expansion_starts && data->raw_error_count == 0;
            uint32_t directs = 0;
            uint32_t runs = 0;
            const uint32_t stop = text            ? pp_copy_file_text_vector(pp, data, base, context->position, run_end, &directs, &runs)
                                  : BUSTER_VECTOR_PATH(pp->vectorized) ? pp_copy_file_run_vector(pp, data, base, context->position, run_end)
                                                   : pp_copy_file_run_scalar(pp, data, base, context->position, run_end);
            if (pp->options.collect_diagnostics && data->raw_error_count != 0 && pp_target(pp) == &pp->output)
            {
                pp->diagnostic_run_first = pp->output.count - (stop - context->position);
            }
            pp_read_raw_errors(pp, context, context->position, stop);
            pp->diagnostic_run_first = BUSTER_PP_NONE;
            if (pp->capture_count != 0)
            {
                pp_history_fill(pp, &pp->captures, pp->captures.count - (stop - context->position), pp->captures.count, 0);
            }
            pp_take_pending_flags(pp, context, stop - context->position);
            if (pp->options.timing)
            {
                // A run ended at each expansion pp_copy_file_text_vector wrote, and those names weren't the runs'.
                pp->statistics.file_runs += 1 + runs;
                pp->statistics.file_run_tokens += stop - context->position - directs;
                pp->statistics.final_file_run_tokens += pp_target(pp) == &pp->output ? stop - context->position - directs : 0;
            }
            context->position = stop;
            more = false;
            // _Pragma is the one builtin still valid in already-preprocessed text: it is an operator, not a macro to
            // keep literal, so it expands as it does in any file (#1552's review). values[] is an IdentId only for a
            // raw identifier; anything else the run stopped on is an offset, not an index into macro_of.
            const PPMacroId stopped_macro = stop < run_end && (data->kinds[stop] & 0x7F) == C_TOKEN_RAW_IDENTIFIER ? pp->macro_of[data->values[stop]] : 0;
            const bool pragma = stopped_macro != 0 && pp->macros[stopped_macro].builtin == PP_BUILTIN_PRAGMA;
            if (stop < run_end && !pp->fatal && pp->options.preprocessed && pp_target(pp) == &pp->output && !pragma)
            {
                // The file was already expanded. Keep the macro's name as an identifier. #if still expands, because
                // that capture is not the output.
                PPToken token = pp_file_token(data, base, context->position);
                if (context->pending_flags != 0)
                {
                    token.flags = pp_with_pending(token.flags, context->pending_flags, true);
                    context->pending_flags = 0;
                }
                context->position += 1;
                pp_emit(pp, token);
                more = context->position < limit && !pp->fatal;
            }
            else if (stop < run_end && !pp->fatal)
            {
                more = BUSTER_VECTOR_PATH(pp->vectorized) && pp_expand_file_direct(pp, context, data, base);
                // A simple invocation whose expansion goes straight into the output, after which the main loop's next step
                // would be this context's next run.
                const bool invocation_read = !more && text && (pp->macros[pp->macro_of[data->values[stop]]].flags & BUSTER_PP_MACRO_FUNCTION);
                more = more || (invocation_read && pp_invoke_file(pp, context, data, base, limit));
                if (!more)
                {
                    pp_expand_current(pp);
                }
                more = more && context->position < limit && !pp->fatal;
            }
        }
    }
    else if (context->next_directive < data->directive_count)
    {
        // The output's keywords a chunk at a time, while its tokens are in the caches: nothing reads their kinds until the
        // run ends, and the output only grows at its end. Read back at the end, sqlite3.c's took 0.12 ms on the 9700X;
        // checked at each step, 0.7 million instructions.
        if (BUSTER_VECTOR_PATH(pp->vectorized) && pp->output.count - pp->keywords_resolved >= BUSTER_PP_KEYWORD_CHUNK)
        {
            pp_resolve_keywords_range(pp, pp->keywords_resolved, pp->output.count);
            pp->keywords_resolved = pp->output.count;
        }
        pp_directive(pp, context);
    }
    else
    {
        pp_pop_context(pp);
    }
}

static void pp_step_buffer(Preprocessor* pp, PPContext* context)
{
    const uint32_t run_end = pp_step_limit(pp, context->position, context->end);
    const uint32_t stop = BUSTER_VECTOR_PATH(pp->vectorized) ? pp_copy_buffer_run_vector(pp, context->tokens, context->position, run_end)
                                         : pp_copy_buffer_run_scalar(pp, context->tokens, context->position, run_end);
    PPTokens* target = pp_target(pp);
    pp_history_copy(pp, target, target->count - (stop - context->position), context->tokens, context->position, stop);
    pp_take_pending_flags(pp, context, stop - context->position);
    if (pp->options.timing)
    {
        pp->statistics.buffer_runs += 1;
        pp->statistics.buffer_run_tokens += stop - context->position;
        pp->statistics.final_buffer_run_tokens += target == &pp->output ? stop - context->position : 0;
    }
    context->position = stop;
    if (stop < run_end && !pp->fatal)
    {
        pp_expand_current(pp);
    }
}

// A barrier ended: its capture's continuation takes the capture (every barrier is pushed with a capture). The streams
// above it, which have no barrier, end there unterminated, as clang's read of an operand stops at the end of a line or
// of an argument. The pragma lines a _Pragma in its tokens emitted go below it first (but for a computed #include's
// line, where clang reads the _Pragma's string as a header name and rejects it, and keeps an error).
static void pp_finish_capture(Preprocessor* pp)
{
    while (pp->capture_frames[pp->capture_count - 1].kind == BUSTER_PP_CAPTURE_STREAM)
    {
        pp_cut_stream(pp);
    }
    if (pp->capture_frames[pp->capture_count - 1].kind != BUSTER_PP_CAPTURE_INCLUDE)
    {
        pp_lift_pragma_lines(pp);
    }
    pp->limit_count -= pp->capture_frames[pp->capture_count - 1].limit != 0;
    switch (pp->capture_frames[pp->capture_count - 1].kind)
    {
        case BUSTER_PP_CAPTURE_ARGUMENT:
            pp_finish_argument(pp);
            break;
        case BUSTER_PP_CAPTURE_CONDITION:
            pp_finish_condition(pp);
            break;
        case BUSTER_PP_CAPTURE_INCLUDE:
            pp_finish_include(pp);
            break;
        case BUSTER_PP_CAPTURE_LINE:
            pp_finish_line(pp);
            break;
        case BUSTER_PP_CAPTURE_BUILTIN:
            pp_finish_builtin(pp);
            break;
        case BUSTER_PP_CAPTURE_PRAGMA:
            pp_finish_pragma(pp);
            break;
        case BUSTER_PP_CAPTURE_EMBED:
            pp_finish_embed(pp);
            break;
    }
}

// The top capture, when it has a limit (PPCapture.limit), counts its new tokens outside pragma lines. Once it has its
// limit's, clang has read what the directive takes from the line expanded, and reads the rest as it is and drops it
// (Preprocessor::CheckEndOfDirective and DiscardUntilEndOfDirective): so the tokens after them go, but the line's next
// token, as it is, which tells the directive that some came after; the contexts down to the line's barrier are popped
// (their macros enabled again), and the capture's continuation runs. Tokens past the limit were read in a run that names
// no macro, which reading them as they are gives too. A computed #include's limit is its header name, a string literal
// or '<' to the first '>' (Preprocessor::LexHeaderName), and one token more.
static __attribute__((noinline, cold)) void pp_check_limit(Preprocessor* pp)
{
    PPCapture* capture = &pp->capture_frames[pp->capture_count - 1];
    PPTokens* tokens = &pp->captures;
    uint32_t keep = BUSTER_PP_NONE;
    uint32_t i = capture->counted_to;
    // A step reads a token at a time (pp_step_limit), so no invocation collects past the last token: but if one did,
    // its tokens would be counted when it ends.
    while (capture->limit != 0 && !pp_collecting(pp) && keep == BUSTER_PP_NONE && i < tokens->count)
    {
        const uint8_t kind = tokens->kinds[i];
        capture->counted_state = (uint8_t)pp_pragma_state(pp, capture->counted_state, kind, tokens->locations[i]);
        const bool outside = capture->counted_state == BUSTER_PP_PRAGMA_OUTSIDE && kind != BUSTER_PP_KIND_PRAGMA_END;
        capture->counted += outside;
        if (outside && capture->kind == BUSTER_PP_CAPTURE_INCLUDE)
        {
            const bool opens = capture->counted == 1 && kind == C_TOKEN_LESS;
            const bool closes = capture->limit == BUSTER_PP_NONE && kind == C_TOKEN_GREATER;
            capture->limit = opens ? BUSTER_PP_NONE : closes ? capture->counted + 1 : capture->limit;
        }
        i += 1;
        keep = outside && capture->counted == capture->limit ? i : BUSTER_PP_NONE;
    }
    capture->counted_to = i;
    if (keep != BUSTER_PP_NONE)
    {
        // The next token: the first past the limit, or the first left in the contexts down to the barrier.
        bool found = keep < tokens->count;
        tokens->count = found ? keep + 1 : keep;
        bool below = false;
        for (uint32_t c = pp->context_count; !found && !below; c -= 1)
        {
            const PPContext* context = &pp->contexts[c - 1];
            found = context->position < context->end;
            if (found)
            {
                pp_emit(pp, pp_context_token(pp, context, context->position));
            }
            below = context->barrier;
        }
        bool popped = false;
        while (!popped)
        {
            popped = pp->contexts[pp->context_count - 1].barrier;
            pp->contexts[pp->context_count - 1].relexed = false;
            pp_pop_context(pp);
        }
        pp_finish_capture(pp);
    }
}

// The main loop, the preprocessor's only loop over contexts: it runs the top context until the stack falls to
// `stop_depth` contexts. Everything that nests is a frame on an explicit stack, never a call: a context per file and
// buffer, an invocation per macro whose arguments are collected (here, a token at a time, when they aren't in one
// context: pp_collect_step) and expanded, and a capture per line expanded for a directive or a builtin, whose
// continuation runs when its barrier ends (pp_finish_capture).
static void pp_run_contexts(Preprocessor* pp, uint32_t stop_depth)
{
    while (!pp->fatal && pp->context_count > stop_depth)
    {
        PPContext* context = &pp->contexts[pp->context_count - 1];
        if (pp_collecting(pp))
        {
            pp_collect_step(pp);
        }
        else if (context->kind == BUSTER_PP_CONTEXT_FILE)
        {
            pp_step_file(pp, context);
        }
        else if (context->position < context->end)
        {
            pp_step_buffer(pp, context);
        }
        else
        {
            const bool barrier = context->barrier;
            pp_pop_context(pp);
            if (barrier && pp->capture_count != 0)
            {
                pp_finish_capture(pp);
            }
        }
        if (pp->stream_count != 0)
        {
            pp_stream_check(pp);
        }
        if (pp->limit_count != 0)
        {
            pp_check_limit(pp);
        }
    }
}

// Copies a file's tokens [begin, end) to pp->arguments, as buffers hold them, for expanding a directive's line: 64 at a
// time (pp_append_file_tokens), their line starts then cleared, or a token at a time by the scalar kernels and for a line
// of a few tokens (#if 1: a line of one token cost 60 instructions more the vectorized way).
static bool pp_copy_line(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t base = pp->ranges[context->range].base;
    PPTokens* arguments = &pp->arguments;
    bool result;
    if (BUSTER_VECTOR_PATH(pp->vectorized) && end - begin > 3)
    {
        const uint32_t at = arguments->count;
        result = pp_append_file_tokens(pp, arguments, data, base, begin, end, false);
        for (uint32_t i = at; result && i < arguments->count; i += 64)
        {
            const __mmask64 valid = _bzhi_u64(~0ull, arguments->count - i < 64 ? arguments->count - i : 64);
            const __m512i flags = _mm512_maskz_loadu_epi8(valid, arguments->flags + i);
            _mm512_mask_storeu_epi8(arguments->flags + i, valid, _mm512_andnot_si512(_mm512_set1_epi8(BUSTER_PP_START_OF_LINE), flags));
        }
    }
    else
    {
        result = pp_room(pp, arguments, end - begin);
        for (uint32_t t = begin; result && t < end; t += 1)
        {
            const PPToken token = pp_file_token(data, base, t);
            pp_tokens_set(arguments, arguments->count, token.kind, (uint8_t)(token.flags & ~BUSTER_PP_START_OF_LINE), token.value, token.location);
            pp_history_fill(pp, arguments, arguments->count, arguments->count + 1, 0);
            arguments->count += 1;
        }
    }
    return result;
}

// Lexes bytes [begin, end) of the context's file again and appends their tokens to `into` as pp_copy_line copies a
// line's: the rest of a line past a header name read from it, where the file's tokens aren't those clang lexes
// (pp_header_name_tail). An unterminated block comment at their end is an error, and left out. Returns false
// if they don't fit.
static bool pp_copy_relexed(Preprocessor* pp, const PPContext* context, uint32_t begin, uint32_t end, PPTokens* into)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t base = pp->ranges[context->range].base;
    const uint8_t* bytes = data->bytes + begin;
    const uint32_t length = end - begin;
    Scratch scratch = scratch_begin(0);
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)length);
    uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
    uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
    const PPPrepare mode = pp_prepare_config(pp);
    int64_t count = kinds != 0 && starts != 0 && ends != 0 ? c_tokenize_scalar(bytes, length, kinds, starts, ends, pp_lex_flags(&mode)) : -1;
    count = count > 0 ? pp_lex_for_mode(&mode, bytes, length, kinds, starts, ends, (uint32_t)count, false) : count;
    // An unterminated block comment at their end runs to the file's end (pp_relex_end), which clang's lexer reports where
    // it starts: the file's tokens, which read it as part of a line comment, didn't.
    const bool unterminated = count > 0 && pp_is_unterminated_comment(bytes, kinds[count - 1], starts[count - 1]);
    if (unterminated)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, base + begin + starts[count - 1], "unterminated /* comment");
    }
    count -= unterminated;
    bool result = count >= 0 && pp_room(pp, into, (uint32_t)count);
    for (uint32_t i = 0; result && i < (uint32_t)count; i += 1)
    {
        const uint8_t kind = kinds[i] & 0x7F;
        const uint32_t value = kind == C_TOKEN_RAW_IDENTIFIER ? refine_intern_identifier(pp->table, bytes, length, starts[i], ends[i] - starts[i], pp->refine_flags)
                                                              : base + begin + ends[i];
        // A gap of line splices alone isn't a space, as refinement has it.
        const uint32_t gap = i == 0 ? 0 : ends[i - 1];
        const bool space = starts[i] != gap && !refine_gap_is_splices(bytes, length, gap, starts[i], pp->refine_flags);
        pp_tokens_set(into, into->count, kind, space ? BUSTER_PP_SPACE : 0, value, base + begin + starts[i]);
        pp_history_fill(pp, into, into->count, into->count + 1, 0);
        into->count += 1;
    }
    scratch_end(scratch);
    return result;
}

// Continue a condition's file-sourced suffix in pieces. Stop each piece after '(' so an include-like builtin can read
// the next header name raw (pp_collect_written_header). Other parentheses just continue ordinary lexing when their
// piece ends. Windows grow geometrically until they contain a parenthesis, the line's end, or a complete token prefix;
// the last three tokens of an incomplete window are withheld because a punctuator or literal may span its edge.
// Thus each byte is lexed a bounded number of times, including a line with many names containing '//'. This is a rare
// correction of file tokens that were lexed before a header name was known; ordinary conditions keep their file runs.
static void pp_continue_relex(Preprocessor* pp, uint32_t file_context, uint32_t begin, uint32_t expansion_end)
{
    PPContext* file = &pp->contexts[file_context];
    const PPFileData* data = &pp->datas[file->data];
    const PPPrepare mode = pp_prepare_config(pp);
    const uint32_t remaining = data->length - begin;
    uint32_t window = remaining < 128 ? remaining : 128;
    uint32_t end = begin;
    uint32_t next = 0;
    bool done = false;
    bool valid = true;
    while (!done && valid)
    {
        Scratch scratch = scratch_begin(0);
        const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY((uint64_t)window);
        uint8_t* kinds = arena_push(scratch.arena, capacity, 64);
        uint32_t* starts = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        uint32_t* ends = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 64);
        valid = kinds != 0 && starts != 0 && ends != 0;
        uint64_t resume = window;
        const uint64_t count = valid ? c_tokenize_scalar_range(data->bytes + begin, window, 0, false, 0, kinds, starts, ends, 0, pp_lex_flags(&mode), &resume) : 0;
        uint32_t paren = 0;
        while (paren < count && (kinds[paren] & 0x7F) != C_TOKEN_L_PAREN)
        {
            paren += 1;
        }
        const bool line_ended = resume < window || window == remaining;
        if (paren < count)
        {
            end = begin + ends[paren];
            next = end;
            done = true;
        }
        else if (line_ended)
        {
            end = begin + (uint32_t)resume;
            done = true;
        }
        else if (count > 3)
        {
            end = begin + ends[count - 4];
            next = end;
            done = true;
        }
        else
        {
            const uint64_t grown = (uint64_t)window * 2;
            window = grown < remaining ? (uint32_t)grown : remaining;
        }
        scratch_end(scratch);
    }
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, pp->ranges[file->range].base + begin, "out of memory re-lexing a header query");
    }
    const uint32_t start = pp->expansions.count;
    if (valid && pp_copy_relexed(pp, file, begin, end, &pp->expansions))
    {
        pp_check_poison_range(pp, data, pp->ranges[file->range].base, &pp->expansions, start, pp->expansions.count, BUSTER_PP_NONE, true);
        pp_resume_after(file, data, end);
        if (pp->expansions.count != start)
        {
            PPContext* piece = pp_push_buffer(pp, &pp->expansions, start, pp->expansions.count, 0, expansion_end, false, true);
            if (piece != 0)
            {
                piece->relexed = true;
                piece->relex_position = next;
                piece->relex_file = file_context;
            }
        }
    }
}

// -H (#1380): a header the main file's run entered, as clang 23 prints it on the diagnostics' stream: a dot per level
// below the main file, a space and the file's path as the line markers spell it. The prelude's -include files and the
// headers they include aren't shown (clang's are its predefines buffer's), and neither is a file that a guard or
// #pragma once skipped, nor one the depth limit refused. -E prints the lines among the preprocessor's diagnostics, as
// clang does; the checker's driver collects those (PPOptions.collect_diagnostics), so the lines come before them.
static void pp_show_include(Preprocessor* pp, uint32_t file)
{
    if (pp->showing_includes && pp->include_depth != 0)
    {
        FILE* stream = pp->options.diagnostics != 0 ? pp->options.diagnostics : stderr;
        for (uint32_t level = 0; level < pp->include_depth; level += 1)
        {
            fputc('.', stream);
        }
        fprintf(stream, " %.*s\n", (int)pp->files[file].path_length, pp->files[file].path);
    }
}

// Enters a file (prepared on first use) included at `include_location` from range `parent`, unless its guard is defined
// or #pragma once read it. The inclusion is as much a system header as the file and as the includer there are, as
// clang has it (HandleHeaderIncludeOrImport): a file included from a system header is one too. As in clang, an
// #include in the 199th file below the main one is an error (at the name, `name_location`, with `name_origin`, the
// history of a name a macro made), after which a file included before isn't entered again: the limit is usually a
// cycle's.
static void pp_enter_file(Preprocessor* pp, uint32_t file, uint32_t parent, uint32_t include_location, uint32_t search_index, uint32_t name_location,
                          uint32_t name_origin)
{
    pp_dependency_observe(pp, file, parent);
    PPFileData* data = &pp->datas[pp->files[file].data];
    const bool guarded = data->guarded && data->guard != 0 && !pp->options.no_guard_skip && pp_is_defined(pp, data->guard);
    const bool skipped = data->prepared && (data->once || guarded);
    pp->statistics.skipped_once += data->prepared && data->once && pp->options.timing;
    pp->statistics.skipped_guards += data->prepared && !data->once && guarded && pp->options.timing;
    // Whether the inclusion takes the file (clang's ShouldEnterIncludeFile, which marks it included before the depth
    // check, so that the file the check refuses isn't entered after it either).
    const bool taken = !skipped && !(pp->include_limit_reached && data->included);
    data->included |= taken;
    if (!taken)
    {
        // Not entered.
    }
    else if (pp->include_depth + 1 >= BUSTER_PP_MAX_INCLUDE_DEPTH)
    {
        const uint32_t saved_origin = pp->diagnostic_origin;
        pp->diagnostic_origin = name_origin;
        pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "#include nested too deeply");
        pp->diagnostic_origin = saved_origin;
        pp->include_limit_reached = true;
    }
    else if (data->prepared || pp_prepare_data(pp, data, pp->files[file].path))
    {
        const uint32_t range = pp_add_range(pp, data->length, file, data->bytes, parent, include_location);
        PPContext* context = range != BUSTER_PP_NONE ? pp_push_context(pp, BUSTER_PP_CONTEXT_FILE) : 0;
        if (context != 0)
        {
            const uint32_t note = parent != BUSTER_PP_NONE ? pp->ranges[parent].last_line_directive : BUSTER_PP_NONE;
            const uint8_t includer = parent == BUSTER_PP_NONE ? BUSTER_PP_USER_FILE : note != BUSTER_PP_NONE ? pp->line_directives[note].system : pp->ranges[parent].system;
            pp->ranges[range].system = pp->files[file].system > includer ? pp->files[file].system : includer;
            context->file = file;
            context->data = pp->files[file].data;
            context->search_index = search_index;
            context->range = range;
            context->end = data->token_count;
            context->conditional_base = pp->conditional_count;
            pp->file_change_token = pp->output.count;
            pp->file_change_location = pp->ranges[range].base;
            pp_note_system_run(pp, range, pp->ranges[range].base, pp->ranges[range].system);
            pp->include_depth += pp->context_count > 1;
            pp_show_include(pp, file);
        }
    }
}

// Only active diagnostic directives read their message opaquely. Detect a token or comment that ordinary lexing
// carried past its logical newline, or an unterminated comment on it, then restore the following source. A token's
// interior is skipped here, so a comment marker inside a quoted message doesn't consume the rare-repair budget.
static bool pp_recover_diagnostic_line(Preprocessor* pp, PPContext* context, uint32_t directive)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t name = data->directive_hash[directive] + 1;
    const uint32_t begin = pp_file_token_end(pp, data, name);
    const ScalarLexer lexer = { .source = data->bytes, .length = data->length, .trigraphs = pp->options.trigraphs };
    const uint32_t stop = (uint32_t)skip_line_comment(&lexer, begin);
    bool overlap = data->unterminated_comment > begin && data->unterminated_comment - 1 < stop;
    uint32_t token = name + 1;
    uint64_t position = begin;
    while (!overlap && position < stop)
    {
        if (token < data->token_count && data->starts[token] <= position)
        {
            position = pp_file_token_end(pp, data, token);
            overlap = position > stop;
            token += 1;
        }
        else
        {
            uint64_t size;
            uint64_t at;
            const int32_t c = get_char(&lexer, position, &size, &at);
            position += size != 0 ? size : 1;
            uint64_t next_size;
            const int32_t next = c == '/' ? peek_char(&lexer, position, &next_size) : 0;
            if (c == '/' && next == '*')
            {
                const bool closed = skip_block_comment(&lexer, position + next_size, &position);
                overlap = !closed || position > stop;
            }
            else if (c == '/' && next == '/')
            {
                position = skip_line_comment(&lexer, position + next_size);
            }
        }
    }
    return !overlap || pp_recover_opaque_span(pp, (uint32_t)(context - pp->contexts), directive, begin, stop);
}

// #error and #warning read text, not tokens: retain comments, quote characters and trailing blanks, removing only
// translation phases 1 and 2 and the leading blanks after the directive name, as clang does. #error's is an error, and
// #warning's a warning of -W#warnings (#1790), which its options and pragmas map and which is seen in system headers.
static void pp_diagnostic_text(Preprocessor* pp, const PPFileData* data, uint32_t name, uint32_t location, bool error)
{
    const uint32_t begin = pp_file_token_end(pp, data, name);
    const ScalarLexer lexer = { .source = data->bytes, .length = data->length, .trigraphs = pp->options.trigraphs };
    const uint32_t end = (uint32_t)skip_line_comment(&lexer, begin);
    Scratch scratch = scratch_begin(0);
    uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(end - begin) + 1, 1);
    if (buffer != 0)
    {
        const uint8_t* text;
        const uint32_t length = pp_remove_splices_by(data->bytes + begin, end - begin, pp->refine_flags, C_TOKEN_RAW_IDENTIFIER, pp->options.scalar, buffer, &text);
        uint32_t first = 0;
        while (first < length && (text[first] == ' ' || text[first] == '\t' || text[first] == '\v' || text[first] == '\f'))
        {
            first += 1;
        }
        if (error)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, "%.*s", (int)(length - first), (const char*)text + first);
        }
        else
        {
            pp_diagnose_group_text(pp, PP_WARNING_WARNINGS, location, (const char*)text + first, length - first);
        }
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, location, "out of memory reading diagnostic text");
    }
    scratch_end(scratch);
}

// Where clang puts token t of the context's file in diagnostics: where its lexer starts the token, at the first of any
// line splices right before it (pp_start_with_splices).
static uint32_t pp_token_location(const Preprocessor* pp, const PPContext* context, const PPFileData* data, uint32_t t)
{
    return pp->ranges[context->range].base + pp_start_with_splices(data->bytes, data->starts[t], pp->options.trigraphs);
}

// A conditional directive's name, for diagnostics.
static const char* pp_conditional_name(uint8_t kind)
{
    return kind == PP_DIRECTIVE_ELIFDEF ? "elifdef" : kind == PP_DIRECTIVE_ELIFNDEF ? "elifndef" : kind == PP_DIRECTIVE_ELSE ? "else" : "elif";
}

// The notes of the directives a skip passes over (PPFileData.note_*), from directive `from` (not included) to `to`
// (the next directive of the group, or the directive count for a skip to the end of the file): what clang reports as
// it skips them, at their names.
static void pp_report_skipped(Preprocessor* pp, const PPContext* context, const PPFileData* data, uint32_t from, uint32_t to)
{
    uint32_t low = 0;
    uint32_t high = data->note_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = data->note_directives[middle] <= from ? middle + 1 : low;
        high = data->note_directives[middle] <= from ? high : middle;
    }
    uint32_t last = low;
    for (; last < data->note_count && data->note_directives[last] < to; last += 1)
    {
        const uint32_t d = data->note_directives[last];
        const uint8_t notes = data->note_kinds[last];
        const uint32_t name = pp_token_location(pp, context, data, data->directive_hash[d] + 1);
        const char* spelling = pp_conditional_name(data->directive_kind[d]);
        if ((notes & BUSTER_PP_NOTE_ELIFDEF) != 0 && pp->options.language.standard < 2023)
        {
            pp_diagnose_group(pp, PP_WARNING_C23_EXTENSIONS, name, "use of a '#%s' directive is a C23 extension", spelling);
        }
        if ((notes & BUSTER_PP_NOTE_AFTER_ELSE) != 0)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, name, "#%s after #else", spelling);
        }
    }
    // A skip to the end leaves the groups opened inside it open, which are unterminated, innermost (last) first.
    for (uint32_t i = last; to == data->directive_count && i > low; i -= 1)
    {
        if ((data->note_kinds[i - 1] & BUSTER_PP_NOTE_UNTERMINATED) != 0)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, data->directive_hash[data->note_directives[i - 1]] + 1), "unterminated conditional directive");
        }
    }
}

// A condition's corrected source hid directives from the file's table. Find its next branch from the first directive
// not consumed by that source, and check the nested skipped groups' structure too: their precomputed notes may have
// paired an opener or an else with a directive that turned out to be inside a comment. Each skipped directive is read
// once. Ordinary contexts use their precomputed links and notes.
static uint32_t pp_skip_relexed_group(Preprocessor* pp, const PPContext* context, const PPFileData* data)
{
    Scratch scratch = scratch_begin(0);
    const uint32_t capacity = data->directive_count - context->next_directive;
    uint32_t* openers = arena_push_array(scratch.arena, capacity, sizeof(uint32_t), 4);
    uint8_t* seen_else = arena_push(scratch.arena, capacity, 1);
    const bool valid = capacity == 0 || (openers != 0 && seen_else != 0);
    uint32_t next = BUSTER_PP_NONE;
    uint32_t depth = 0;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory checking a re-lexed condition");
    }
    for (uint32_t e = context->next_directive; valid && next == BUSTER_PP_NONE && e < data->directive_count; e += 1)
    {
        const uint8_t kind = data->directive_kind[e];
        const bool opens = kind == PP_DIRECTIVE_IF || kind == PP_DIRECTIVE_IFDEF || kind == PP_DIRECTIVE_IFNDEF;
        const bool branch = kind == PP_DIRECTIVE_ELIF || kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF || kind == PP_DIRECTIVE_ELSE;
        const bool closes = kind == PP_DIRECTIVE_ENDIF;
        next = depth == 0 && (branch || closes) ? e : BUSTER_PP_NONE;
        if (opens)
        {
            openers[depth] = e;
            seen_else[depth] = false;
            depth += 1;
        }
        else if (branch && depth != 0)
        {
            const uint32_t location = pp_token_location(pp, context, data, data->directive_hash[e] + 1);
            const char* name = pp_conditional_name(kind);
            if ((kind == PP_DIRECTIVE_ELIFDEF || kind == PP_DIRECTIVE_ELIFNDEF) && pp->options.language.standard < 2023)
            {
                pp_diagnose_group(pp, PP_WARNING_C23_EXTENSIONS, location, "use of a '#%s' directive is a C23 extension", name);
            }
            if (seen_else[depth - 1])
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#%s after #else", name);
            }
            seen_else[depth - 1] |= kind == PP_DIRECTIVE_ELSE;
        }
        else if (closes && depth != 0)
        {
            depth -= 1;
        }
    }
    for (uint32_t i = depth; next == BUSTER_PP_NONE && i > 0; i -= 1)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, data->directive_hash[openers[i - 1]] + 1), "unterminated conditional directive");
    }
    scratch_end(scratch);
    return next;
}

// Skip to the next branch of d's group, reporting the skipped directives' structural diagnostics.
static void pp_skip_group(Preprocessor* pp, PPContext* context, uint32_t d)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t next = context->hidden_directives ? pp_skip_relexed_group(pp, context, data) : data->directive_next[d];
    if (next == BUSTER_PP_NONE)
    {
        context->position = data->token_count;
        context->next_directive = data->directive_count;
    }
    else
    {
        context->position = data->directive_hash[next];
        context->next_directive = next;
    }
    if (pp->conditional_count > context->conditional_base)
    {
        pp->conditionals[pp->conditional_count - 1].skipping = true;
    }
    if (!context->hidden_directives && data->note_count != 0)
    {
        pp_report_skipped(pp, context, data, d, context->next_directive);
    }
}

// A #if's, #ifdef's or #ifndef's group is open (taken or not), or a #elif's, #elifdef's or #elifndef's branch is
// decided, at directive d: a group not taken is skipped to its next directive.
static void pp_apply_condition(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind, bool taken)
{
    if (kind == PP_DIRECTIVE_IF || kind == PP_DIRECTIVE_IFDEF || kind == PP_DIRECTIVE_IFNDEF)
    {
        const PPFileData* data = &pp->datas[context->data];
        context->guard_taken |= d == data->guard_opener && data->guard != 0 && taken;
        const uint32_t name = pp->ranges[context->range].base + data->starts[data->directive_hash[d] + 1];
        if (pp->conditional_count < (BUSTER_PP_MAX_CONTEXTS >> pp->capacity_shift))
        {
            pp->conditionals[pp->conditional_count] = (PPConditional){ .taken = taken, .location = name };
            pp->conditional_count += 1;
        }
    }
    else
    {
        pp->conditionals[pp->conditional_count - 1].taken = taken;
        pp->conditionals[pp->conditional_count - 1].skipping = false;
    }
    if (!taken)
    {
        pp_skip_group(pp, context, d);
    }
}

// pp_start_condition, the main loop's steps over the line's capture and pp_finish_condition for a #if or #elif line whose
// expansion only ever writes into the capture: its tokens but names, `defined` with its operand (a name, or one in
// parentheses), names of no macro, and names of object-like macros that expand, through bodies of one token each, to a
// number or a name of no macro (sqlite3.c's 1,409 lines of 1,737). Its expanded tokens go straight into pp->captures as
// the capture would hold them (kinds, values, locations and origins: the evaluation reads nothing else), with the
// expansions' records, the statistics and the temporary origins' peaks the copy of the line and those steps would leave,
// and are evaluated and decide the group as pp_finish_condition does. The line is checked whole before anything changes:
// returns false, having changed nothing, when it isn't so, or something is open that the capture's steps would read.
static bool pp_condition_direct(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind, uint32_t begin, uint32_t end, uint32_t location)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t base = pp->ranges[context->range].base;
    PPTokens* captures = &pp->captures;
    bool result = BUSTER_VECTOR_PATH(pp->vectorized) && begin < end && end - begin <= BUSTER_PP_CONDITION_TOKENS &&
                  (pp->capture_count | pp->stream_count | pp->limit_count | pp->poison_count) == 0 && !pp->options.expansion_starts && !pp->fatal &&
                  data->raw_error_count == 0 && !pp_collecting(pp) && pp->arguments.count + (end - begin) <= pp->arguments.capacity &&
                  (uint64_t)captures->count + (end - begin) <= captures->capacity &&
                  pp->expansions.count + BUSTER_PP_CONDITION_CHAIN <= pp->expansions.capacity &&
                  (pp->options.no_history || pp_history_origin_room(pp, captures, captures->count + (end - begin)));
    for (uint32_t t = begin; result && t < end; t += 1)
    {
        const PPMacroId id = (data->kinds[t] & 0x7F) == C_TOKEN_RAW_IDENTIFIER ? pp->macro_of[data->values[t]] : 0;
        const PPMacro* macro = &pp->macros[id];
        if (id != 0 && (macro->flags & BUSTER_PP_MACRO_BUILTIN) && macro->builtin == PP_BUILTIN_DEFINED)
        {
            // `defined X` or `defined ( X )`.
            const bool named = t + 1 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
            const bool parenthesized = t + 3 < end && (data->kinds[t + 1] & 0x7F) == C_TOKEN_L_PAREN && (data->kinds[t + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER &&
                                       (data->kinds[t + 3] & 0x7F) == C_TOKEN_R_PAREN;
            result = named || parenthesized;
            t += named ? 1 : 3;
        }
        else if (id != 0)
        {
            // Object-like macros, each of a body of one token that names the next, to a number or a name of no macro; none
            // twice, which would be painted.
            PPMacroId chain[BUSTER_PP_CONDITION_CHAIN];
            uint32_t depth = 0;
            bool ended = false;
            PPMacroId next = id;
            while (result && !ended)
            {
                const PPMacro* link = &pp->macros[next];
                bool repeated = false;
                for (uint32_t k = 0; k < depth; k += 1)
                {
                    repeated |= chain[k] == next;
                }
                // The body is read only for an object-like macro of one token (an empty one's first token would be past
                // its replacement list, and a builtin has none).
                const bool single = depth < BUSTER_PP_CONDITION_CHAIN && !repeated &&
                                    (link->flags & (BUSTER_PP_MACRO_FUNCTION | BUSTER_PP_MACRO_BUILTIN | BUSTER_PP_MACRO_PASTES)) == 0 && link->disabled == 0 &&
                                    link->body_end - link->body_begin == 1;
                const PPFileData* body = single ? &pp->datas[link->data] : 0;
                const uint8_t body_kind = single ? body->kinds[link->body_begin] & 0x7F : 0;
                result = single && (body_kind == C_TOKEN_NUMERIC_CONSTANT || body_kind == C_TOKEN_RAW_IDENTIFIER);
                chain[depth < BUSTER_PP_CONDITION_CHAIN ? depth : 0] = next;
                depth += 1;
                next = result && body_kind == C_TOKEN_RAW_IDENTIFIER ? pp->macro_of[body->values[link->body_begin]] : 0;
                ended = next == 0;
            }
        }
    }
    if (result)
    {
        const bool history = !pp->options.no_history;
        const uint32_t capture_begin = captures->count;
        // pp_copy_line's copy into pp->arguments would have given its origins.
        if (history)
        {
            const uint32_t copied = pp->arguments.count + (end - begin);
            pp->origin_peak[0] = pp->origin_peak[0] < copied ? copied : pp->origin_peak[0];
        }
        // The capture's steps: a run of the line's tokens from the start and after each expansion with tokens after it,
        // and a run of none in the buffer of each expansion whose body names a macro.
        uint32_t runs = 1;
        uint32_t run_tokens = 0;
        uint32_t direct = 0;
        uint32_t buffered = 0;
        uint32_t builtins = 0;
        uint32_t expansions_peak = 0;
        uint32_t anchor = BUSTER_PP_NONE;
        uint32_t count = capture_begin;
        for (uint32_t t = begin; t < end; t += 1)
        {
            const uint8_t token_kind = data->kinds[t] & 0x7F;
            const PPMacroId id = token_kind == C_TOKEN_RAW_IDENTIFIER ? pp->macro_of[data->values[t]] : 0;
            const PPMacro* macro = &pp->macros[id];
            const uint32_t name_location = base + data->starts[t];
            // The name's flags in the line's buffer (pp_copy_line's), which the answer or the body's token takes.
            const uint8_t name_flags = pp_file_space(data, t) ? BUSTER_PP_SPACE : 0;
            if (id == 0)
            {
                captures->kinds[count] = token_kind;
                captures->flags[count] = name_flags;
                captures->values[count] = token_kind == C_TOKEN_RAW_IDENTIFIER ? data->values[t] : base + data->values[t];
                captures->locations[count] = name_location;
                if (history)
                {
                    captures->origins[count] = 0;
                }
                count += 1;
                run_tokens += 1;
            }
            else if (macro->flags & BUSTER_PP_MACRO_BUILTIN)
            {
                // pp_expand_builtin's `defined`: its record, and its answer with its record as origin.
                const uint32_t record = pp_history_expansion(pp, id, name_location, 0);
                const bool parenthesized = (data->kinds[t + 1] & 0x7F) == C_TOKEN_L_PAREN;
                const IdentId operand = data->values[t + (parenthesized ? 2 : 1)];
                const uint32_t saved_history = pp->history_current;
                pp->history_current = record;
                const PPToken answer = pp_number_token(pp, pp_is_defined(pp, operand), name_flags);
                pp->history_current = saved_history;
                captures->kinds[count] = answer.kind;
                captures->flags[count] = answer.flags;
                captures->values[count] = answer.value;
                captures->locations[count] = answer.location;
                if (history)
                {
                    captures->origins[count] = answer.origin;
                }
                count += 1;
                builtins += 1;
                anchor = name_location;
                t += parenthesized ? 3 : 1;
                runs += t + 1 < end;
            }
            else
            {
                // pp_expand_object's records along the chain: each body that names a macro is buffered, its name read
                // from that buffer with the expansion as origin, and the last body goes straight into the capture.
                uint32_t parent = 0;
                uint32_t place = name_location;
                PPMacroId next = id;
                uint32_t depth = 0;
                while (next != 0)
                {
                    const PPMacro* link = &pp->macros[next];
                    const PPFileData* body = &pp->datas[link->data];
                    const uint32_t b = link->body_begin;
                    const uint8_t body_kind = body->kinds[b] & 0x7F;
                    const uint32_t record = pp_history_expansion(pp, next, place, parent);
                    const PPMacroId after = body_kind == C_TOKEN_RAW_IDENTIFIER ? pp->macro_of[body->values[b]] : 0;
                    if (after != 0)
                    {
                        buffered += 1;
                        runs += 1;
                        depth += 1;
                        expansions_peak = expansions_peak < depth ? depth : expansions_peak;
                    }
                    else
                    {
                        captures->kinds[count] = body_kind;
                        captures->flags[count] = (uint8_t)(name_flags | BUSTER_PP_FROM_MACRO | BUSTER_PP_NAME_SPACE);
                        captures->values[count] = body_kind == C_TOKEN_RAW_IDENTIFIER ? body->values[b] : link->base + body->values[b];
                        captures->locations[count] = link->base + body->starts[b];
                        if (history)
                        {
                            captures->origins[count] = record;
                        }
                        count += 1;
                        direct += 1;
                    }
                    parent = record;
                    place = link->base + body->starts[b];
                    next = after;
                }
                runs += t + 1 < end;
            }
        }
        captures->count = count;
        if (history)
        {
            pp->origin_peak[1] = pp->origin_peak[1] < count ? count : pp->origin_peak[1];
            const uint32_t buffers = pp->expansions.count + expansions_peak;
            pp->origin_peak[2] = expansions_peak != 0 && pp->origin_peak[2] < buffers ? buffers : pp->origin_peak[2];
        }
        if (anchor != BUSTER_PP_NONE)
        {
            pp->history_anchor = anchor;
        }
        if (pp->options.timing)
        {
            pp->statistics.buffer_runs += runs;
            pp->statistics.buffer_run_tokens += run_tokens;
            pp->statistics.builtins += builtins;
            pp->statistics.direct_expansions += direct;
            pp->statistics.buffered_expansions += buffered;
        }
        // pp_finish_condition's.
        pp_read_directive_comment(pp, context, d);
        const bool taken = pp_evaluate(pp, captures, capture_begin, captures->count, location);
        if (pp->options.timing)
        {
            const uint64_t tokens = captures->count - capture_begin;
            pp->statistics.conditions += 1;
            pp->statistics.condition_tokens += tokens;
            pp->statistics.long_conditions += tokens > 32;
            pp->statistics.longest_condition = tokens > pp->statistics.longest_condition ? tokens : pp->statistics.longest_condition;
        }
        captures->count = capture_begin;
        pp_apply_condition(pp, context, d, kind, taken);
    }
    return result;
}

// A #if or #elif line (tokens [begin, end) of the context's file) is copied to pp->arguments and expanded through a
// capture (pp_start_capture); pp_finish_condition evaluates it and decides the group. An empty line is an error, and
// false at once. A line pp_condition_direct takes doesn't need the capture.
static void pp_start_condition(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind, uint32_t begin, uint32_t end, uint32_t location)
{
    if (!pp_condition_direct(pp, context, d, kind, begin, end, location))
    {
        const uint32_t line = pp->arguments.count;
        bool started = false;
        if (begin == end)
        {
            pp_read_directive_comment(pp, context, d);
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#if with no expression");
        }
        else if (pp_copy_line(pp, context, begin, end))
        {
            const PPCapture capture = {
                .kind = BUSTER_PP_CAPTURE_CONDITION,
                .arguments_begin = line,
                .directive = d,
                .context = (uint32_t)(context - pp->contexts),
                .directive_kind = kind,
                .location = location,
            };
            started = pp_start_capture(pp, capture, line, pp->arguments.count, location);
        }
        if (!started)
        {
            pp_read_directive_comment(pp, context, d);
            pp->arguments.count = line;
            pp_apply_condition(pp, context, d, kind, false);
        }
    }
}

// The barrier over a #if or #elif line ended: the capture is the line expanded, which decides the group.
static void pp_finish_condition(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp_read_directive_comment(pp, &pp->contexts[capture.context], capture.directive);
    const PPContext* file = &pp->contexts[capture.context];
    const PPFileData* data = &pp->datas[file->data];
    pp_read_raw_errors(pp, file, data->directive_hash[capture.directive], data->directive_end[capture.directive]);
    const bool taken = pp_evaluate(pp, &pp->captures, capture.begin, pp->captures.count, capture.location);
    if (pp->options.timing)
    {
        const uint64_t tokens = pp->captures.count - capture.begin;
        pp->statistics.conditions += 1;
        pp->statistics.condition_tokens += tokens;
        pp->statistics.long_conditions += tokens > 32;
        pp->statistics.longest_condition = tokens > pp->statistics.longest_condition ? tokens : pp->statistics.longest_condition;
    }
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
    pp_apply_condition(pp, &pp->contexts[capture.context], capture.directive, capture.directive_kind, taken);
}

// A directive at `hash` whose line [hash, end) lacks the macro name it needs, reported as clang reports it: missing at the
// line's end, or not an identifier at the token.
static void pp_report_macro_name(Preprocessor* pp, const PPFileData* data, uint32_t base, uint32_t hash, uint32_t end)
{
    if (hash + 2 < end)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, base, hash + 2, end), "macro name must be an identifier");
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, pp_line_end_location(pp, data, base, hash + 1), "macro name missing");
    }
}

// The conditional directives (C 6.10.2), at directive d. Their structure is reported at the directive's name and
// extra tokens at the first of them, where clang reports them, and in clang's order.
static void pp_conditional(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    const bool open = pp->conditional_count > context->conditional_base;
    PPConditional* group = open ? &pp->conditionals[pp->conditional_count - 1] : 0;
    switch (kind)
    {
        case PP_DIRECTIVE_IF:
            pp_start_condition(pp, context, d, kind, hash + 2, end, location);
            break;
        case PP_DIRECTIVE_IFDEF:
        case PP_DIRECTIVE_IFNDEF:
        {
            const bool named = hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
            if (!named)
            {
                pp_report_macro_name(pp, data, base, hash, end);
            }
            else if (hash + 3 < end)
            {
                pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_token_location(pp, context, data, hash + 3), "extra tokens at end of #%s directive", kind == PP_DIRECTIVE_IFDEF ? "ifdef" : "ifndef");
            }
            const bool defined = named && pp_is_defined(pp, data->values[hash + 2]);
            pp_apply_condition(pp, context, d, kind, named && (kind == PP_DIRECTIVE_IFDEF ? defined : !defined));
            break;
        }
        case PP_DIRECTIVE_ELIF:
        case PP_DIRECTIVE_ELIFDEF:
        case PP_DIRECTIVE_ELIFNDEF:
            if (kind != PP_DIRECTIVE_ELIF && pp->options.language.standard < 2023)
            {
                pp_diagnose_group(pp, PP_WARNING_C23_EXTENSIONS, pp_token_location(pp, context, data, hash + 1), "use of a '#%s' directive is a C23 extension", pp_conditional_name(kind));
            }
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, hash + 1), "#%s without #if", pp_conditional_name(kind));
                pp->unterminated += 1;
            }
            else if (group->seen_else)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, hash + 1), "#%s after #else", pp_conditional_name(kind));
                pp_skip_group(pp, context, d);
            }
            else if (group->taken)
            {
                pp_skip_group(pp, context, d);
            }
            else if (kind == PP_DIRECTIVE_ELIF)
            {
                pp_start_condition(pp, context, d, kind, hash + 2, end, location);
            }
            else
            {
                const bool named = hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER;
                if (!named)
                {
                    pp_report_macro_name(pp, data, base, hash, end);
                }
                else if (hash + 3 < end)
                {
                    pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_token_location(pp, context, data, hash + 3), "extra tokens at end of #%s directive", pp_conditional_name(kind));
                }
                const bool defined = named && pp_is_defined(pp, data->values[hash + 2]);
                pp_apply_condition(pp, context, d, kind, named && (kind == PP_DIRECTIVE_ELIFDEF ? defined : !defined));
            }
            break;
        case PP_DIRECTIVE_ELSE:
        {
            // clang checks the line's end before the structure in a group it is reading (or with no group), after it
            // where it enters the #else from a skip, and not at all where it skips past a taken group.
            const bool entering = open && !group->taken;
            const bool extra = hash + 2 < end && !(open && group->taken && group->skipping);
            if (extra && !entering)
            {
                pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_token_location(pp, context, data, hash + 2), "extra tokens at end of #else directive");
            }
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, hash + 1), "#else without #if");
                pp->unterminated += 1;
            }
            else
            {
                if (group->seen_else)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, hash + 1), "#else after #else");
                }
                if (extra && entering)
                {
                    pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_token_location(pp, context, data, hash + 2), "extra tokens at end of #else directive");
                }
                group->seen_else = true;
                if (group->taken)
                {
                    pp_skip_group(pp, context, d);
                }
                group->skipping &= group->taken;
                group->taken = true;
            }
            break;
        }
        case PP_DIRECTIVE_ENDIF:
            if (hash + 2 < end)
            {
                pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_token_location(pp, context, data, hash + 2), "extra tokens at end of #endif directive");
            }
            if (!open)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_token_location(pp, context, data, hash + 1), "#endif without #if");
                pp->unterminated += 1;
            }
            else
            {
                pp->conditional_count -= 1;
            }
            break;
    }
}

// Whether a byte is a letter or a digit (ASCII), as clang's typo correction of a header name reads it.
static bool pp_alphanumeric(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// The file a #include, #include_next or #import (`kind`) at `location` in the context's file names, written at
// `name_location` (with `name_origin`, the history of a name a macro made): entered, unless #import read it. As
// clang's HandleHeaderIncludeOrImport goes: an empty name is an error; a name that ends in a space or a dot is
// nonportable; an #include in an assume_nonnull or arc_cf_code_audited region is an error, which ends the region. A
// file not found is fatal, but for clang's recoveries, which are errors that include the file: an angled name found as
// a quoted one, and a name found without the characters other than letters and digits it starts and ends with. A place
// the search can't look in (a path too long for one, a file that can't be read: pp_find_include) is fatal, and
// nothing after it is reported, as clang reports nothing after a fatal error.
static void pp_include_file(Preprocessor* pp, PPContext* context, uint8_t kind, const uint8_t* name, uint32_t length, bool angled, uint32_t location,
                            uint32_t name_location, uint32_t name_origin)
{
    const uint32_t saved_origin = pp->diagnostic_origin;
    pp->diagnostic_origin = name_origin;
    uint32_t file = BUSTER_PP_NONE;
    uint32_t found_in = BUSTER_PP_NONE;
    if (length == 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "empty filename");
    }
    else
    {
        if (name[length - 1] == ' ' || name[length - 1] == '.')
        {
            pp_diagnose_group(pp, PP_WARNING_NONPORTABLE_INCLUDE_PATH, name_location, "non-portable path to file '%.*s'; specified path contains trailing %s", (int)length, (const char*)name,
                        name[length - 1] == ' ' ? "whitespace" : "dot");
        }
        pp->diagnostic_origin = 0;
        for (uint32_t region = 0; region < BUSTER_PP_REGION_UNSAFE_BUFFER; region += 1)
        {
            if (pp->region_locations[region] != 0)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "cannot #include files inside '#pragma clang %s'", pp_region_names[region]);
                pp->region_locations[region] = 0;
            }
        }
        pp->diagnostic_origin = name_origin;
        const bool next = kind == PP_DIRECTIVE_INCLUDE_NEXT && pp->include_depth != 0;
        file = pp_find_include(pp, name, length, angled, next, context->file, context->search_index, &found_in, name_location);
        if (file == BUSTER_PP_NONE && angled && !pp->fatal)
        {
            file = pp_find_include(pp, name, length, false, next, context->file, context->search_index, &found_in, name_location);
            if (file != BUSTER_PP_NONE)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "'%.*s' file not found with <angled> include; use \"quotes\" instead", (int)length, (const char*)name);
            }
        }
        uint32_t first = 0;
        uint32_t last = length;
        while (first < last && !pp_alphanumeric(name[first]))
        {
            first += 1;
        }
        while (last > first && !pp_alphanumeric(name[last - 1]))
        {
            last -= 1;
        }
        if (file == BUSTER_PP_NONE && last - first != length && !pp->fatal)
        {
            file = pp_find_include(pp, name + first, last - first, angled, next, context->file, context->search_index, &found_in, name_location);
            if (file != BUSTER_PP_NONE)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "'%.*s' file not found, did you mean '%.*s'?", (int)length, (const char*)name, (int)(last - first),
                            (const char*)name + first);
            }
        }
        if (file == BUSTER_PP_NONE && !pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, name_location, "'%.*s' file not found", (int)length, (const char*)name);
        }
    }
    pp->diagnostic_origin = saved_origin;
    if (file == BUSTER_PP_NONE)
    {
        // Not found. pp_enter_file handles an already imported file like #pragma once, counting its skipped inclusion.
    }
    else
    {
        pp_enter_file(pp, file, context->range, location, found_in, name_location, name_origin);
        if (kind == PP_DIRECTIVE_IMPORT)
        {
            pp->datas[pp->files[file].data].once = true;
        }
    }
}

// The name of an #include-like directive of `kind`, for its diagnostics.
static const char* pp_include_name(uint8_t kind)
{
    return kind == PP_DIRECTIVE_INCLUDE_NEXT ? "include_next" : kind == PP_DIRECTIVE_IMPORT ? "import" : "include";
}

// The end of a computed header name in tokens [begin, end) of a stack: past a string literal, or past the first '>'
// after a '<' (`*unclosed` when there is none, which clang reports as an expected '>'), else `begin`.
static uint32_t pp_computed_name_end(const PPTokens* tokens, uint32_t begin, uint32_t end, bool* unclosed)
{
    uint32_t stop = begin;
    *unclosed = false;
    if (stop < end && tokens->kinds[stop] == C_TOKEN_STRING_LITERAL)
    {
        stop += 1;
    }
    else if (stop < end && tokens->kinds[stop] == C_TOKEN_LESS)
    {
        while (stop < end && tokens->kinds[stop] != C_TOKEN_GREATER)
        {
            stop += 1;
        }
        *unclosed = stop == end;
        stop += stop < end;
    }
    return stop;
}

// The barrier over a computed #include's line ended: the capture is the line expanded, a string literal or <...> (to
// the first '>', as clang reads it), and tokens after it are a warning. The diagnostics are at the tokens (with their
// histories), or at the line's end for what it lacks, as clang's are.
static void pp_finish_include(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    const PPTokens* tokens = &pp->captures;
    PPContext* context = &pp->contexts[capture.context];
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t line_end = pp_line_end_location(pp, data, pp->ranges[context->range].base, data->directive_end[capture.directive] - 1);
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    bool unclosed;
    const uint32_t stop = pp_computed_name_end(tokens, capture.begin, tokens->count, &unclosed);
    const bool valid = !unclosed && pp_header_name(pp, tokens, capture.begin, stop, name, sizeof(name), &length, &angled);
    const bool extra = valid && stop < tokens->count;
    const uint32_t name_location = capture.begin < tokens->count ? tokens->locations[capture.begin] : line_end;
    const uint32_t name_origin = capture.begin < tokens->count && tokens->origins != 0 ? tokens->origins[capture.begin] : 0;
    const uint32_t extra_location = extra ? tokens->locations[stop] : 0;
    const uint32_t extra_origin = extra && tokens->origins != 0 ? tokens->origins[stop] : 0;
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
    const uint32_t saved_origin = pp->diagnostic_origin;
    if (unclosed)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, line_end, "expected '>'");
    }
    else if (!valid)
    {
        pp->diagnostic_origin = name_origin;
        pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "expected \"FILENAME\" or <FILENAME>");
    }
    else
    {
        if (extra)
        {
            pp->diagnostic_origin = extra_origin;
            pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, extra_location, "extra tokens at end of #%s directive", pp_include_name(capture.directive_kind));
        }
        pp->diagnostic_origin = saved_origin;
        pp_include_file(pp, context, capture.directive_kind, name, length, angled, capture.location, name_location, name_origin);
    }
    pp->diagnostic_origin = saved_origin;
}

// A header name as it is written at token `first` of a directive's line [.., end): <...>, read from the source up to the
// first '>' on the line, or a string literal, without line splices. `*literal` says whether the token is one of those
// (a '<' with its '>' on the line, or a string literal), and `*after` gets the first token past the name. When the
// line's tokens from there aren't those clang lexes on from the '>' (pp_header_name_tail), the line's bytes
// [*relex_begin, *relex_end) must be lexed again for them; otherwise the two are equal. Returns whether it is a name:
// false also for a literal that isn't a plain string, and for one longer than `capacity`, which is still a literal. A
// computed name is any other token.
static bool pp_read_header_name(Preprocessor* pp, const PPFileData* data, uint32_t first, uint32_t end, uint8_t* name, uint32_t capacity, uint32_t* length,
                                bool* angled, bool* literal, uint32_t* after, uint32_t* relex_begin, uint32_t* relex_end)
{
    bool valid = false;
    *length = 0;
    *angled = false;
    *literal = false;
    *after = end;
    *relex_begin = 0;
    *relex_end = 0;
    if (first < end && (data->kinds[first] & 0x7F) == C_TOKEN_LESS)
    {
        // The lexer doesn't make header names: read <...> from the source (pp_scan_header_name). Without its '>' on the
        // line, the name is the line's tokens up to a '>' (clang's LexHeaderName joins them): with a comment that runs on
        // to the next line, `<a/*x` then `*/b.h>` is `a b.h`. With it, the name is written out, also when `name` can't
        // hold it all, and never a computed one, whose identifiers would expand.
        bool whole;
        const uint32_t stop = pp_scan_header_name(pp->refine_flags, data, first, end, name, capacity, length, &whole);
        *literal = stop != 0;
        valid = *literal && whole;
        *angled = valid;
        // The first token at or after the '>''s end, and what must be lexed again from there (pp_header_name_tail).
        *after = *literal ? pp_header_name_tail(pp, data, first, end, stop, relex_end) : end;
        *relex_begin = *literal ? stop : 0;
    }
    else if (first < end && (data->kinds[first] & 0x7F) == C_TOKEN_STRING_LITERAL)
    {
        *literal = true;
        *after = first + 1;
        // The string literal without its line splices.
        const uint32_t start = data->starts[first];
        const uint32_t stop = data->values[first];
        Scratch scratch = scratch_begin(0);
        uint8_t* buffer = arena_push(scratch.arena, (uint64_t)(stop - start) + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t spelled = buffer != 0 ? pp_without_splices(pp, C_TOKEN_STRING_LITERAL, data->bytes + start, stop - start, buffer, &spelling) : 0;
        valid = spelled >= 2 && spelled - 2 < capacity && spelling[0] == '"';
        if (valid)
        {
            *length = spelled - 2;
            memcpy(name, spelling + 1, *length);
        }
        scratch_end(scratch);
    }
    return valid;
}

// #include, #include_next and #import, at directive d. A computed name (the line's macros expanded) goes through a
// capture, which pp_finish_include takes. A written one is read whole, however long (it is no longer than its line), so
// that one too long for a path is clang's error about the path (pp_find_include).
static void pp_include(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    const uint32_t first = hash + 2;
    const uint32_t line_end = end < data->token_count ? data->starts[end] : data->length;
    const uint32_t room = first < end ? line_end - data->starts[first] : 0;
    Scratch scratch = scratch_begin(0);
    uint8_t* name = arena_push(scratch.arena, (uint64_t)room + 1, 1);
    uint32_t length = 0;
    bool angled = false;
    bool started = false;
    uint32_t stop = 0;
    const PPHeaderForm form = pp_literal_header_name(data, d, pp->refine_flags, name, name != 0 ? room : 0, &length, &angled, &stop, scratch.arena);
    // Where clang reports the name: its token, or the line's end without one.
    const uint32_t name_location = first < end ? base + data->starts[first] : pp_line_end_location(pp, data, base, end - 1);
    if (form == PP_HEADER_COMPUTED)
    {
        // A computed include: the line expanded, then a string literal or <...> from its tokens, and one token more,
        // after which clang reads the line as it is (pp_check_limit).
        const uint32_t line = pp->arguments.count;
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_INCLUDE,
            .arguments_begin = line,
            .directive = d,
            .context = (uint32_t)(context - pp->contexts),
            .directive_kind = kind,
            .location = location,
            .limit = 2,
        };
        started = pp_copy_line(pp, context, hash + 2, end) && pp_start_capture(pp, capture, line, pp->arguments.count, location);
        pp->arguments.count = started ? pp->arguments.count : line;
    }
    if (form != PP_HEADER_LITERAL && !started)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "expected \"FILENAME\" or <FILENAME>");
    }
    else if (form == PP_HEADER_LITERAL)
    {
        // A token after the name, where clang lexes on from it (pp_skip_blanks, past what the file's tokens may hold as
        // a comment begun in the name: `<a//b.h> x`), is a warning at it.
        const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
        const uint64_t next = pp_skip_blanks(&lexer, stop);
        if (next < data->length && data->bytes[next] != '\n' && data->bytes[next] != '\r')
        {
            pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, base + (uint32_t)next, "extra tokens at end of #%s directive", pp_include_name(kind));
        }
        // The file goes on past the line as clang lexes it (pp_resume_after), before the header is entered.
        uint32_t relex;
        pp_header_name_tail(pp, data, first, end, stop, &relex);
        pp_resume_after(context, data, relex);
        pp_include_file(pp, context, kind, name, length, angled, location, name_location, 0);
    }
    scratch_end(scratch);
}

// A #line's or line marker's file name from its string literal's spelling (quotes included, line splices removed), read as
// clang reads an unevaluated string literal (#147): a simple escape or a UCN (as UTF-8) stands for its character, and any
// other escape sequence is clang's error at its backslash (check_literal_unevaluated_escapes), from the token at
// `string_location`, after which clang drops the directive. Writes the name to `out` (room for the spelling's length) and
// returns its length, or BUSTER_PP_NONE after a diagnostic.
static uint32_t pp_decode_file_name(Preprocessor* pp, const uint8_t* spelling, uint32_t length, char* out, uint32_t location, uint32_t string_location)
{
    static const char simple[] = "'\"?\\abfnrtv";
    static const char meaning[] = "'\"?\\\a\b\f\n\r\t\v";
    uint32_t result = 0;
    bool valid = length >= 2 && spelling[0] == '"' && spelling[length - 1] == '"';
    bool reported = false;
    bool counted_error = false;
    const uint32_t escapes = valid ? check_literal_unevaluated_escapes(spelling, length, pp->options.language.standard >= 2023, false, NULL, &counted_error) : 0;
    if (escapes != 0)
    {
        Scratch scratch = scratch_begin(0);
        CheckEscapeDiagnostic* diagnostics = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, CheckEscapeDiagnostic, BUSTER_CHECK_ESCAPE_DIAGNOSTICS(length));
        bool had_error = false;
        const uint32_t count = diagnostics != NULL ? check_literal_unevaluated_escapes(spelling, length, pp->options.language.standard >= 2023, false, diagnostics, &had_error) : 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            const CheckEscapeDiagnostic* diagnostic = diagnostics + i;
            const uint32_t at = pp_spelling_offset_location(pp, string_location, diagnostic->offset);
            const uint8_t character = diagnostic->character;
            switch (diagnostic->kind)
            {
                case CHECK_ESCAPE_DIAGNOSTIC_UNKNOWN:
                    // As the preprocessor's other warnings, and its pragmas' strings' unknown escapes (pp_string_value).
                    if (character >= 0x20 && character < 0x7f)
                    {
                        pp_diagnose_group(pp, PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE, at, "unknown escape sequence '\\%c'", (char)character);
                    }
                    else
                    {
                        pp_diagnose_group(pp, PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE, at, "unknown escape sequence '\\x%X'", (unsigned)(int32_t)(int8_t)character);
                    }
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_HEX_DIGITS:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "\\x used with no following hex digits");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE:
                case CHECK_ESCAPE_DIAGNOSTIC_OCTAL_RANGE:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "%s escape sequence out of range", diagnostic->kind == CHECK_ESCAPE_DIAGNOSTIC_HEX_RANGE ? "hex" : "octal");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_EMPTY:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "delimited escape sequence cannot be empty");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_DIGIT:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "invalid digit '%c' in escape sequence", (char)character);
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_OPEN_BRACE:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "expected '{' after '\\%c' escape sequence", (char)character);
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_CLOSE_BRACE:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "expected '}'");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_UCN_DIGITS:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "\\%c used with no following hex digits", (char)character);
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_UCN_INCOMPLETE:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "incomplete universal character name");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_UCN_INVALID:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "invalid universal character");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_UCN_CONTROL:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "universal character name refers to a control character");
                    break;
                case CHECK_ESCAPE_DIAGNOSTIC_UCN_BASIC:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "character '%c' cannot be specified by a universal character name", (char)character);
                    break;
                default:
                    pp_diagnose(pp, PP_SEVERITY_ERROR, at, "invalid escape sequence '%.*s' in an unevaluated string literal", (int)diagnostic->length,
                                (const char*)spelling + diagnostic->offset);
                    break;
            }
        }
        scratch_end(scratch);
        valid = false;
        reported = true;
    }
    for (uint32_t i = 1; valid && i + 1 < length; i += 1)
    {
        uint8_t c = spelling[i];
        if (c == '\\' && i + 2 < length)
        {
            const uint8_t e = spelling[i + 1];
            const char* found = e != 0 ? strchr(simple, e) : 0;
            if (found != 0)
            {
                c = (uint8_t)meaning[found - simple];
                i += 1;
            }
            else if (e == 'u' || e == 'U')
            {
                // A UCN: four or eight hex digits, written as UTF-8.
                const uint32_t digits = e == 'u' ? 4 : 8;
                uint32_t code = 0;
                valid = i + 2 + digits < length;
                for (uint32_t k = 0; valid && k < digits; k += 1)
                {
                    const int32_t v = hex_digit_value(spelling[i + 2 + k]);
                    valid = v >= 0;
                    code = code * 16 + (uint32_t)(v >= 0 ? v : 0);
                }
                valid = valid && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF);
                if (valid)
                {
                    uint8_t utf8[4];
                    const uint32_t n = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
                    utf8[0] = (uint8_t)(n == 1 ? code : n == 2 ? 0xC0 | (code >> 6) : n == 3 ? 0xE0 | (code >> 12) : 0xF0 | (code >> 18));
                    for (uint32_t k = 1; k < n; k += 1)
                    {
                        utf8[k] = (uint8_t)(0x80 | ((code >> (6 * (n - 1 - k))) & 0x3F));
                    }
                    memcpy(out + result, utf8, n - 1);
                    result += n - 1;
                    c = utf8[n - 1];
                }
                i += 1 + digits;
            }
            else
            {
                // An octal or hex escape (or another): clang allows none in an unevaluated string.
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid escape sequence in the file name of a #line directive");
                valid = false;
                reported = true;
            }
        }
        out[result] = (char)c;
        result += 1;
    }
    if (!valid && !reported)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid file name in a #line directive");
    }
    return valid ? result : BUSTER_PP_NONE;
}

// Where clang's lexer starts a token that starts at `start`: at the first of any line splices right before it (a
// backslash, or with `trigraphs` a ??/, then blanks, and a newline or CRLF, with nothing between them and the token),
// since it reads them as part of the token after them. Blanks between the splice and the token are whitespace it skips
// first.
static uint32_t pp_start_with_splices(const uint8_t* bytes, uint32_t start, bool trigraphs)
{
    uint32_t result = start;
    bool moved = true;
    while (moved)
    {
        uint32_t p = result;
        const bool newline = p >= 1 && (bytes[p - 1] == '\n' || bytes[p - 1] == '\r');
        p -= newline ? 1 : 0;
        p -= newline && bytes[p] == '\n' && p >= 1 && bytes[p - 1] == '\r' ? 1 : 0;
        while (newline && p >= 1 && (bytes[p - 1] == ' ' || bytes[p - 1] == '\t'))
        {
            p -= 1;
        }
        const bool backslash = newline && p >= 1 && bytes[p - 1] == '\\';
        const bool trigraph = newline && trigraphs && p >= 3 && bytes[p - 1] == '/' && bytes[p - 2] == '?' && bytes[p - 3] == '?';
        moved = backslash || trigraph;
        result = backslash ? p - 1 : trigraph ? p - 3 : result;
    }
    return result;
}

// pp_start_with_splices for a token's location in a file (another place has no line splices).
static uint32_t pp_lexed_start(const Preprocessor* pp, uint32_t location)
{
    const uint32_t range = pp_find_range(pp, location);
    const PPRange* record = range != BUSTER_PP_NONE ? &pp->ranges[range] : 0;
    return record != 0 && record->file != BUSTER_PP_NONE ? record->base + pp_start_with_splices(record->bytes, location - record->base, pp->options.trigraphs) : location;
}

// The line note in effect at `location` in `range` (the latest one at or before it), or BUSTER_PP_NONE.
static uint32_t pp_line_note_at(const Preprocessor* pp, uint32_t range, uint32_t location)
{
    uint32_t result = BUSTER_PP_NONE;
    for (uint32_t i = range != BUSTER_PP_NONE ? pp->ranges[range].first_line_directive : BUSTER_PP_NONE; i != BUSTER_PP_NONE && pp->line_directives[i].location <= location;
         i = pp->line_directives[i].next_in_range)
    {
        result = i;
    }
    return result;
}

// Adds a line note to a range, from `location` on (PPLineDirective): `name` null keeps the presumed name, the name of
// the range's latest note, and `system` BUSTER_PP_NONE keeps the system level (that note's, else the inclusion's).
// `entry` is a line marker's 1 (a file entered) or 2 (one left), else 0, which set its include location as clang's
// LineTableInfo::AddLineNote does (#1954): 1 the place right before the note, 2 the one in effect where the latest
// note's file was entered, and 0 the latest note's.
static void pp_add_line_note(Preprocessor* pp, uint32_t range, uint32_t location, int64_t delta, const char* name, uint32_t name_length, uint32_t system, uint32_t entry)
{
    PPRange* record = &pp->ranges[range];
    const uint32_t latest = record->last_line_directive;
    if (pp->line_directive_count < (BUSTER_PP_MAX_LINE_DIRECTIVES >> pp->capacity_shift))
    {
        const uint32_t index = pp->line_directive_count;
        const bool inherits = name == 0 && latest != BUSTER_PP_NONE;
        const uint8_t level = latest != BUSTER_PP_NONE ? pp->line_directives[latest].system : record->system;
        const uint32_t kept = latest != BUSTER_PP_NONE ? pp->line_directives[latest].include_location : BUSTER_PP_NONE;
        const uint32_t outer = entry == 2 && kept != BUSTER_PP_NONE ? pp_line_note_at(pp, range, kept) : BUSTER_PP_NONE;
        const uint32_t include = entry == 1 ? location - 1 : entry == 2 ? (outer != BUSTER_PP_NONE ? pp->line_directives[outer].include_location : BUSTER_PP_NONE) : kept;
        pp->line_directives[index] = (PPLineDirective){
            .range = range,
            .location = location,
            .delta = delta,
            .name = inherits ? pp->line_directives[latest].name : name,
            .name_length = inherits ? pp->line_directives[latest].name_length : name_length,
            .next_in_range = BUSTER_PP_NONE,
            .include_location = include,
            .system = system == BUSTER_PP_NONE ? level : (uint8_t)system,
        };
        pp->line_directive_count += 1;
        if (latest != BUSTER_PP_NONE)
        {
            pp->line_directives[latest].next_in_range = index;
        }
        else
        {
            record->first_line_directive = index;
        }
        record->last_line_directive = index;
        pp_note_system_run(pp, range, location, pp->line_directives[index].system);
    }
}

// A #line's or a line marker's number, token `i` of `tokens`, as clang's GetLineValue reads it: digits (without the
// line splices in its spelling, and skipping a digit separator, C23's, in a number C23 lexes), each step of whose value
// in 32 bits doesn't go below the last (clang's overflow test, which lets some wrap). A leading 0 is a warning (the
// number is decimal), named for a line marker's number with `marker` (and for a #line's, or a marker's flags, without).
// Returns whether it is one.
static bool pp_line_value(Preprocessor* pp, const PPTokens* tokens, uint32_t i, bool marker, uint64_t* number)
{
    bool result = tokens->kinds[i] == C_TOKEN_NUMERIC_CONSTANT;
    uint32_t value = 0;
    Scratch scratch = scratch_begin(0);
    uint8_t* buffer = result ? arena_push(scratch.arena, (uint64_t)(tokens->values[i] - tokens->locations[i]) + 1, 1) : 0;
    const uint8_t* digits = 0;
    const uint32_t count = buffer != 0 ? pp_spelling(pp, tokens->kinds[i], tokens->values[i], tokens->locations[i], buffer, &digits) : 0;
    result = count != 0;
    for (uint32_t k = 0; result && k < count; k += 1)
    {
        const bool separator = digits[k] == '\'';
        const uint32_t next = separator ? value : value * 10u + (uint32_t)(digits[k] - '0');
        result = separator || (digits[k] >= '0' && digits[k] <= '9' && next >= value);
        value = next;
    }
    if (result && digits[0] == '0' && value != 0)
    {
        pp_diagnose_group(pp, PP_WARNING_PLAIN, tokens->locations[i], "%s directive interprets number as decimal, not octal", marker ? "GNU line marker" : "#line");
    }
    scratch_end(scratch);
    *number = value;
    return result;
}

// The lines a #line or a line marker at directive d numbers, from its tokens [begin, stop) of `tokens` (expanded, for
// #line): lines from the next on are numbered from the digits, and the file is named by the string literal if there is
// one. A line marker's flags come after the name, as clang reads them (ReadLineMarkerFlags): 1 (a file entered) or 2
// (one left, which a marker with 1 in the same inclusion entered), then 3 (a system header), then 4 (one in extern
// "C"); anything else is an error, which drops the marker. The marker's file is a system header for 3 and a user file
// without it; a #line keeps what the file is, and tokens after its name are a warning.
static void pp_apply_line(Preprocessor* pp, PPContext* context, uint32_t d, const PPTokens* tokens, uint32_t begin, uint32_t stop, uint32_t location, uint32_t anchor)
{
    const PPFileData* data = &pp->datas[context->data];
    uint64_t number = 0;
    const bool marker = data->directive_kind[d] == PP_DIRECTIVE_LINE_MARKER;
    bool valid = begin < stop && pp_line_value(pp, tokens, begin, marker, &number);
    // The operands' spellings without their line splices (translation phase 2), as clang reads them.
    Scratch scratch = scratch_begin(0);
    const char* name = 0;
    uint32_t name_length = 0;
    bool name_reported = false;
    if (valid && begin + 1 < stop)
    {
        valid = tokens->kinds[begin + 1] == C_TOKEN_STRING_LITERAL;
        const uint32_t spelled = tokens->values[begin + 1] - tokens->locations[begin + 1];
        uint8_t* buffer = valid ? arena_push(scratch.arena, (uint64_t)spelled + 1, 1) : 0;
        char* copy = valid ? arena_push(&pp->arena, (uint64_t)spelled + 1, 1) : 0;
        const uint8_t* spelling = 0;
        const uint32_t length = buffer != 0 && copy != 0 ? pp_spelling(pp, tokens->kinds[begin + 1], tokens->values[begin + 1], tokens->locations[begin + 1], buffer, &spelling) : 0;
        // A raw string literal names the file with its body as written.
        const uint8_t* body;
        uint32_t body_length;
        const bool raw = length >= 2 && pp_raw_string_body(spelling, length, &body, &body_length);
        if (raw)
        {
            memcpy(copy, body, body_length);
        }
        name_length = raw ? body_length : length >= 2 ? pp_decode_file_name(pp, spelling, length, copy, location, tokens->locations[begin + 1]) : BUSTER_PP_NONE;
        name_reported = length >= 2 && name_length == BUSTER_PP_NONE;
        valid = valid && name_length != BUSTER_PP_NONE;
        name = valid ? copy : 0;
        name_length = valid ? name_length : 0;
    }
    scratch_end(scratch);
    uint32_t system = marker ? BUSTER_PP_USER_FILE : BUSTER_PP_NONE;
    bool entered = false;
    bool left = false;
    uint32_t stage = 0;
    bool flags_reported = false;
    for (uint32_t i = begin + 2; valid && marker && i < stop; i += 1)
    {
        uint64_t flag = 0;
        const bool expected = pp_line_value(pp, tokens, i, false, &flag) && ((stage == 0 && flag >= 1 && flag <= 3) || (stage == 1 && flag == 3) || (stage == 2 && flag == 4));
        valid = expected;
        flags_reported = !expected;
        entered |= flag == 1;
        left |= flag == 2;
        system = flag == 3 ? BUSTER_PP_SYSTEM_FILE : flag == 4 ? BUSTER_PP_EXTERN_C_FILE : system;
        stage = flag <= 2 ? 1 : flag == 3 ? 2 : 3;
    }
    if (flags_reported)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid flag line marker directive");
    }
    else if (valid && left && pp->ranges[context->range].marker_entries == 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid line marker flag '2': cannot pop empty include stack");
        valid = false;
        flags_reported = true;
    }
    if (valid && !marker && begin + 2 < stop)
    {
        pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, location, "extra tokens at end of #line directive");
    }
    if (!valid && !name_reported && !flags_reported)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "#line directive requires a simple digit sequence (and optionally a file name)");
    }
    else if (valid)
    {
        pp->ranges[context->range].marker_entries += entered;
        pp->ranges[context->range].marker_entries -= left;
        // The line after the number token's is `number`, as clang counts, and its line note starts at that token
        // (SourceManager::AddLineNote at DigitTok): where clang lexes it, or for one an expansion made (a macro's, or a
        // builtin's in the scratch space), where it lexes the expansion's name (`anchor`, pp_note_line_anchor's); with
        // the line splices right before it, since its lexer reads a splice as part of the token after it.
        const uint32_t digits = tokens->locations[begin];
        const bool made = (tokens->flags[begin] & BUSTER_PP_FROM_MACRO) || pp_find_range(pp, digits) != context->range;
        const uint32_t place = pp_lexed_start(pp, made && anchor != BUSTER_PP_NONE ? anchor : digits);
        const uint32_t note = pp_find_range(pp, place) == context->range ? place : location + 1;
        const uint32_t physical = pp_line_of(pp, context->range, note, 0) + 1;
        // A #line without a name keeps the presumed one.
        pp_add_line_note(pp, context->range, note, (int64_t)number - (int64_t)physical, name, name_length, system, entered ? 1u : left ? 2u : 0u);
        pp->file_change_token = pp->output.count;
        pp->file_change_location = pp_directive_resume(pp, context, d);
    }
}

// The barrier over a #line's line ended: the capture is the line expanded.
static void pp_finish_line(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp_apply_line(pp, &pp->contexts[capture.context], capture.directive, &pp->captures, capture.begin, pp->captures.count, capture.location, capture.anchor);
    pp->capture_count -= 1;
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
}

// #line digits ["file"], or a GNU line marker (`# digits "file" flags`), at directive d. #line's line, and a line
// marker's after its digits, are expanded through a capture, which pp_finish_line takes.
static void pp_line(Preprocessor* pp, PPContext* context, uint32_t d, uint8_t kind)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t location = pp->ranges[context->range].base + data->starts[hash];
    const uint32_t first = kind == PP_DIRECTIVE_LINE_MARKER ? hash + 1 : hash + 2;
    const uint32_t line = pp->arguments.count;
    const bool copied = first < end && pp_copy_line(pp, context, first, end);
    // A #line reads its number, its name and one token more expanded (Preprocessor::HandleLineDirective), and the rest as
    // it is (pp_check_limit); a line marker reads its flags expanded.
    const PPCapture capture = {
        .kind = BUSTER_PP_CAPTURE_LINE,
        .arguments_begin = line,
        .directive = d,
        .context = (uint32_t)(context - pp->contexts),
        .directive_kind = kind,
        .location = location,
        .anchor = BUSTER_PP_NONE,
        .limit = kind == PP_DIRECTIVE_LINE ? 3 : 0,
    };
    // A #line's line is read expanded, and so is a line marker's after its digits (Preprocessor::HandleDigitDirective's
    // Lex), which go first: but not among a macro's arguments, where clang's macro expansion is off, and only #if's and
    // #elif's turn it back on (Preprocessor::EvaluateDirectiveExpression).
    const uint32_t expanded = kind == PP_DIRECTIVE_LINE_MARKER ? line + 1 : line;
    const bool started = copied && expanded < pp->arguments.count && !pp_collecting(pp) && pp_start_capture(pp, capture, expanded, pp->arguments.count, location);
    if (started && kind == PP_DIRECTIVE_LINE_MARKER)
    {
        pp_emit(pp, pp_stack_token(&pp->arguments, line));
    }
    if (!started)
    {
        pp_apply_line(pp, context, d, &pp->arguments, line, copied ? pp->arguments.count : line, location, BUSTER_PP_NONE);
        pp->arguments.count = line;
    }
}

// Emits the start of a #pragma or #ident line to the output: '#' starting a line, and the name (`pragma` or `ident`).
// The name's location is the directive's, or for _Pragma (`from_operator`) the operator's, whose '#' is then one a
// macro made (-E's line markers place it as the expansion the operator is in).
static void pp_emit_pragma_start(Preprocessor* pp, IdentId name, uint32_t location, bool from_operator)
{
    // The '#' isn't in any source here: its spelling is the scratch "#".
    const uint8_t flags = (uint8_t)(BUSTER_PP_START_OF_LINE | (from_operator ? BUSTER_PP_FROM_MACRO : 0));
    pp->pragma_lines += 1;
    if (from_operator && pp->options.expansion_starts)
    {
        // At the line its expansion came from, or for a _Pragma in a directive's line, at its expansion there (the
        // latest name read from the line: the _Pragma, or the macro it came from), where clang's printer moves to for it.
        uint32_t where = pp->expansion_location;
        for (uint32_t c = pp->capture_count; c > 0; c -= 1)
        {
            const PPCapture* frame = &pp->capture_frames[c - 1];
            const bool directive = frame->kind == BUSTER_PP_CAPTURE_CONDITION || frame->kind == BUSTER_PP_CAPTURE_INCLUDE || frame->kind == BUSTER_PP_CAPTURE_LINE ||
                                   frame->kind == BUSTER_PP_CAPTURE_PRAGMA || frame->kind == BUSTER_PP_CAPTURE_EMBED;
            where = directive && where == pp->expansion_location ? (frame->name_location != 0 ? frame->name_location : frame->location) : where;
        }
        const uint32_t count = pp->expansion_start_count;
        const bool displaces = pp->capture_count != 0 && count != 0 && pp->expansion_starts[count - 1].token == pp->output.count;
        pp->displaced_location = displaces && pp->displaced_location == 0 ? pp->expansion_starts[count - 1].location : pp->displaced_location;
        pp_note_expansion(pp, where);
    }
    pp_emit(pp, (PPToken){ .kind = C_TOKEN_HASH, .flags = flags, .value = pp->hash_location + 1, .location = pp->hash_location });
    pp_emit(pp, (PPToken){ .kind = C_TOKEN_RAW_IDENTIFIER, .flags = 0, .value = name, .location = location });
}

// One of a pragma line's tokens after its name, which is never expanded again: with `first`, a space before it.
static void pp_emit_pragma_token(Preprocessor* pp, PPToken token, bool first)
{
    token.flags = (uint8_t)((token.flags & BUSTER_PP_SPACE) | (first ? BUSTER_PP_SPACE : 0) | BUSTER_PP_NO_EXPAND);
    pp_emit(pp, token);
}

// Emits a #pragma or #ident line to the output: its start, tokens [begin, end) of a stack, and BUSTER_PP_KIND_PRAGMA_END.
static void pp_emit_pragma(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, IdentId name, uint32_t location, bool from_operator)
{
    pp_emit_pragma_start(pp, name, location, from_operator);
    for (uint32_t i = begin; i < end; i += 1)
    {
        pp_emit_pragma_token(pp, pp_stack_token(tokens, i), i == begin);
    }
    pp_emit(pp, (PPToken){ .kind = BUSTER_PP_KIND_PRAGMA_END });
}

// A token spelled in the scratch space: a pragma's '(' and ')', and its strings as clang prints them.
static PPToken pp_scratch_token(Preprocessor* pp, uint8_t kind, const uint8_t* bytes, uint32_t length, uint8_t flags)
{
    const uint32_t location = pp_scratch_clean(pp, bytes, length);
    const PPToken result = { .kind = kind, .flags = flags, .value = location + length, .location = location };
    return result;
}

// The kind of the pragma whose tokens (after `pragma`) are [begin, end), and where its operands start (`*operands`, after
// its one or two names).
static PPPragmaKind pp_pragma_kind(const Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t* operands)
{
    const IdentId first = begin < end && tokens->kinds[begin] == C_TOKEN_RAW_IDENTIFIER ? tokens->values[begin] : 0;
    const IdentId second = begin + 1 < end && tokens->kinds[begin + 1] == C_TOKEN_RAW_IDENTIFIER ? tokens->values[begin + 1] : 0;
    const bool space = first == C_NAME_GCC || first == C_NAME_CLANG;
    PPPragmaKind result = PP_PRAGMA_OTHER;
    *operands = begin + 1;
    if (first == C_NAME_ONCE)
    {
        result = PP_PRAGMA_ONCE;
    }
    else if (pp_token_is_name(pp, tokens, begin, end, "mark") || pp_token_is_name(pp, tokens, begin, end, "region") ||
             pp_token_is_name(pp, tokens, begin, end, "endregion"))
    {
        result = PP_PRAGMA_QUIET;
    }
    else if (first == C_NAME_PUSH_MACRO || first == C_NAME_POP_MACRO)
    {
        result = first == C_NAME_PUSH_MACRO ? PP_PRAGMA_PUSH_MACRO : PP_PRAGMA_POP_MACRO;
    }
    else if (pp_token_is_name(pp, tokens, begin, end, "message"))
    {
        result = PP_PRAGMA_MESSAGE;
    }
    else if (pp_token_is_name(pp, tokens, begin, end, "omp"))
    {
        result = PP_PRAGMA_OMP;
    }
    else if (pp->options.semantic_pragmas &&
             (pp_token_is_name(pp, tokens, begin, end, "pack") || pp_token_is_name(pp, tokens, begin, end, "weak") ||
              pp_token_is_name(pp, tokens, begin, end, "redefine_extname") || pp_token_is_name(pp, tokens, begin, end, "ms_struct") ||
              pp_token_is_name(pp, tokens, begin, end, "float_control")))
    {
        result = PP_PRAGMA_SEMANTIC;
    }
    else if (space && second != 0)
    {
        *operands = begin + 2;
        const bool clang = first == C_NAME_CLANG;
        result = second == C_NAME_POISON ? PP_PRAGMA_POISON
                 : second == C_NAME_SYSTEM_HEADER ? PP_PRAGMA_SYSTEM_HEADER
                 : second == C_NAME_DEPENDENCY ? PP_PRAGMA_DEPENDENCY
                 : pp_token_is_name(pp, tokens, begin + 1, end, "diagnostic") ? PP_PRAGMA_DIAGNOSTIC
                 : !clang && second == C_NAME_WARNING ? PP_PRAGMA_WARNING
                 : !clang && second == C_NAME_ERROR ? PP_PRAGMA_ERROR
                 : clang && (pp_token_is_name(pp, tokens, begin + 1, end, "assume_nonnull") || pp_token_is_name(pp, tokens, begin + 1, end, "arc_cf_code_audited") ||
                             pp_token_is_name(pp, tokens, begin + 1, end, "unsafe_buffer_usage")) ? PP_PRAGMA_REGION
                 : clang && pp->options.semantic_pragmas && pp_token_is_name(pp, tokens, begin + 1, end, "fp") ? PP_PRAGMA_SEMANTIC
                 : clang && (pp_token_is_name(pp, tokens, begin + 1, end, "deprecated") || pp_token_is_name(pp, tokens, begin + 1, end, "final") ||
                             pp_token_is_name(pp, tokens, begin + 1, end, "restrict_expansion")) ? PP_PRAGMA_MACRO_NOTE
                 : PP_PRAGMA_OTHER;
    }
    return result;
}

// The value of an ordinary string literal's spelling (with its quotes), as clang's StringLiteralParser decodes it: the
// simple escapes and GNU's \e, octal and hex ones up to a byte, and UCNs as UTF-8 (in C17, not one for a basic or a
// control character), into `out` (room for `length` bytes). An unknown escape is its character, with a warning. Returns
// the value's length, or BUSTER_PP_NONE after an error.
static uint32_t pp_string_value(Preprocessor* pp, const uint8_t* spelling, uint32_t length, uint8_t* out, uint32_t location)
{
    static const char simple[] = "'\"?\\abfnrtveE";
    static const char meaning[] = "'\"?\\\a\b\f\n\r\t\v\x1B\x1B";
    uint32_t result = 0;
    bool valid = length >= 2 && spelling[0] == '"' && spelling[length - 1] == '"';
    for (uint32_t i = 1; valid && i + 1 < length; i += 1)
    {
        const uint8_t c = spelling[i];
        const uint8_t e = c == '\\' && i + 2 < length ? spelling[i + 1] : 0;
        const char* found = e != 0 ? strchr(simple, e) : 0;
        if (c != '\\')
        {
            out[result] = c;
            result += 1;
        }
        else if (found != 0)
        {
            out[result] = (uint8_t)meaning[found - simple];
            result += 1;
            i += 1;
        }
        else if (e >= '0' && e <= '7')
        {
            uint32_t value = 0;
            uint32_t digits = 0;
            while (digits < 3 && i + 1 + digits + 1 < length && spelling[i + 1 + digits] >= '0' && spelling[i + 1 + digits] <= '7')
            {
                value = value * 8 + (uint32_t)(spelling[i + 1 + digits] - '0');
                digits += 1;
            }
            valid = value <= 0xFF;
            if (!valid)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "octal escape sequence out of range");
            }
            out[result] = (uint8_t)value;
            result += 1;
            i += digits;
        }
        else if (e == 'x')
        {
            uint32_t value = 0;
            uint32_t digits = 0;
            bool overflow = false;
            while (i + 2 + digits + 1 < length && hex_digit_value(spelling[i + 2 + digits]) >= 0)
            {
                value = value * 16 + (uint32_t)hex_digit_value(spelling[i + 2 + digits]);
                overflow |= value > 0xFF;
                digits += 1;
            }
            valid = digits != 0 && !overflow;
            if (!valid)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, digits == 0 ? "\\x used with no following hex digits" : "hex escape sequence out of range");
            }
            out[result] = (uint8_t)value;
            result += 1;
            i += 1 + digits;
        }
        else if (e == 'u' || e == 'U')
        {
            const uint32_t digits = e == 'u' ? 4 : 8;
            uint32_t code = 0;
            uint32_t read = 0;
            while (read < digits && i + 2 + read + 1 < length && hex_digit_value(spelling[i + 2 + read]) >= 0)
            {
                code = code * 16 + (uint32_t)hex_digit_value(spelling[i + 2 + read]);
                read += 1;
            }
            // C23 lets a UCN name any character in a string; before it, not a basic one or a control one ($, @ and `
            // excepted).
            const bool basic = code < 0xA0 && code != 0x24 && code != 0x40 && code != 0x60;
            valid = read == digits && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF) && (!basic || pp->options.language.standard >= 2023);
            if (!valid)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, read != digits ? "incomplete universal character name" : "invalid universal character");
            }
            const uint32_t n = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
            out[result] = (uint8_t)(n == 1 ? code : n == 2 ? 0xC0 | (code >> 6) : n == 3 ? 0xE0 | (code >> 12) : 0xF0 | (code >> 18));
            for (uint32_t k = 1; k < n; k += 1)
            {
                out[result + k] = (uint8_t)(0x80 | ((code >> (6 * (n - 1 - k))) & 0x3F));
            }
            result += n;
            i += 1 + digits;
        }
        else
        {
            pp_diagnose_group(pp, PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE, location, "unknown escape sequence '\\%c'", e != 0 ? (char)e : '\\');
            out[result] = e != 0 ? e : c;
            result += 1;
            i += e != 0;
        }
    }
    return valid ? result : BUSTER_PP_NONE;
}

// A pragma's string, as clang's FinishLexStringLiteral reads one: ordinary string literals from token *i of [.., end),
// their values joined into `out` (room for `capacity` bytes); a first token that isn't one is an error. *i goes past
// them. Returns the value's length, or BUSTER_PP_NONE after an error.
static uint32_t pp_pragma_strings(Preprocessor* pp, const PPTokens* tokens, uint32_t* i, uint32_t end, uint8_t* out, uint32_t capacity, const char* what, uint32_t location)
{
    uint32_t result = 0;
    if (*i >= end || tokens->kinds[*i] != C_TOKEN_STRING_LITERAL)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, *i < end ? tokens->locations[*i] : location, "expected string literal in %s", what);
        result = BUSTER_PP_NONE;
    }
    while (result != BUSTER_PP_NONE && *i < end && tokens->kinds[*i] == C_TOKEN_STRING_LITERAL)
    {
        Scratch scratch = scratch_begin(0);
        const uint32_t spelled = tokens->values[*i] - tokens->locations[*i];
        uint8_t* buffer = arena_push(scratch.arena, (uint64_t)spelled + 1, 1);
        const uint8_t* spelling = 0;
        const uint32_t n = buffer != 0 ? pp_spelling(pp, C_TOKEN_STRING_LITERAL, tokens->values[*i], tokens->locations[*i], buffer, &spelling) : 0;
        const uint32_t decoded = n <= capacity - result ? pp_string_value(pp, spelling, n, out + result, tokens->locations[*i]) : BUSTER_PP_NONE;
        result = decoded != BUSTER_PP_NONE ? result + decoded : BUSTER_PP_NONE;
        scratch_end(scratch);
        *i += 1;
    }
    return result;
}

// #pragma message, GCC warning and GCC error, on their operands [begin, end) (expanded): a string, or a parenthesized one
// (message's MSVC form), and nothing after it; a diagnostic, and the line in clang's form, `#pragma message("...")`,
// `#pragma GCC warning "..."` or `#pragma GCC error "..."`, with the strings' value written as clang's printer writes
// it (outputPrintable): a printable byte as it is, and a backslash, a quote or any other byte as an octal escape.
static void pp_pragma_message(Preprocessor* pp, PPPragmaKind kind, const PPTokens* names, uint32_t names_begin, const PPTokens* tokens, uint32_t begin, uint32_t end,
                              uint32_t location, bool from_operator)
{
    const char* what = kind == PP_PRAGMA_MESSAGE ? "pragma message" : kind == PP_PRAGMA_WARNING ? "pragma warning" : "pragma error";
    Scratch scratch = scratch_begin(0);
    uint32_t capacity = 2;
    for (uint32_t t = begin; t < end; t += 1)
    {
        capacity += tokens->kinds[t] == C_TOKEN_STRING_LITERAL ? 4 * (tokens->values[t] - tokens->locations[t]) : 0;
    }
    uint8_t* text = arena_push(scratch.arena, capacity, 1);
    uint32_t i = begin;
    const bool parenthesized = i < end && tokens->kinds[i] == C_TOKEN_L_PAREN;
    i += parenthesized;
    uint32_t length = BUSTER_PP_NONE;
    if (text == 0 || (!parenthesized && (i >= end || tokens->kinds[i] != C_TOKEN_STRING_LITERAL)))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "%s requires parenthesized string", what);
    }
    else
    {
        length = pp_pragma_strings(pp, tokens, &i, end, text + 1, capacity - 2, what, location);
        const bool closed = !parenthesized || (i < end && tokens->kinds[i] == C_TOKEN_R_PAREN);
        i += parenthesized && closed;
        if (length != BUSTER_PP_NONE && (!closed || i != end))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, i < end ? tokens->locations[i] : location, "%s requires parenthesized string", what);
            length = BUSTER_PP_NONE;
        }
    }
    uint8_t* written = length != BUSTER_PP_NONE ? arena_push(scratch.arena, 4 * (uint64_t)length + 2, 1) : 0;
    if (written != 0)
    {
        const uint32_t name = names_begin + (kind != PP_PRAGMA_MESSAGE);
        const uint32_t diagnostic_location = names->locations[name];
        const uint32_t saved_origin = pp->diagnostic_origin;
        if (from_operator)
        {
            // The destringized pragma has its own scratch spelling. Retain that step before the operator's parent
            // expansion, so its note and relative column remain distinct from the macro that contained _Pragma.
            const PPSource source = { .location = names->locations[names_begin] };
            const uint32_t origin = pp_history_generated(pp, PP_GENERATED_PRAGMA, diagnostic_location, location, &source, 1);
            if (origin != 0)
            {
                pp->generated[(origin & BUSTER_PP_ORIGIN_INDEX) - 1].expansion = saved_origin;
                pp->diagnostic_origin = origin;
            }
        }
        if (!pp->fatal)
        {
            if (kind == PP_PRAGMA_ERROR)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, diagnostic_location, "%.*s", (int)length, (const char*)text + 1);
            }
            else
            {
                // -W#pragma-messages' (#1790), which its options and pragmas map but -Werror leaves a warning.
                pp_diagnose_group_text(pp, PP_WARNING_PRAGMA_MESSAGES, diagnostic_location, (const char*)text + 1, length);
            }
        }
        pp->diagnostic_origin = saved_origin;
        uint32_t n = 0;
        written[n] = '"';
        n += 1;
        for (uint32_t k = 0; k < length; k += 1)
        {
            const uint8_t c = text[1 + k];
            if (c >= 0x20 && c < 0x7F && c != '\\' && c != '"')
            {
                written[n] = c;
                n += 1;
            }
            else
            {
                written[n] = '\\';
                written[n + 1] = (uint8_t)('0' + ((c >> 6) & 7));
                written[n + 2] = (uint8_t)('0' + ((c >> 3) & 7));
                written[n + 3] = (uint8_t)('0' + (c & 7));
                n += 4;
            }
        }
        written[n] = '"';
        n += 1;
        pp_emit_pragma_start(pp, C_NAME_PRAGMA, location, from_operator);
        pp_emit_pragma_token(pp, pp_stack_token(names, names_begin), true);
        if (kind == PP_PRAGMA_MESSAGE)
        {
            pp_emit(pp, pp_scratch_token(pp, C_TOKEN_L_PAREN, (const uint8_t*)"(", 1, BUSTER_PP_NO_EXPAND));
            pp_emit(pp, pp_scratch_token(pp, C_TOKEN_STRING_LITERAL, written, n, 0));
            pp_emit(pp, pp_scratch_token(pp, C_TOKEN_R_PAREN, (const uint8_t*)")", 1, 0));
        }
        else
        {
            pp_emit_pragma_token(pp, pp_stack_token(names, names_begin + 1), true);
            pp_emit(pp, pp_scratch_token(pp, C_TOKEN_STRING_LITERAL, written, n, BUSTER_PP_SPACE));
        }
        pp_emit(pp, (PPToken){ .kind = BUSTER_PP_KIND_PRAGMA_END });
    }
    scratch_end(scratch);
}

// The remark groups #pragma clang diagnostic takes as "-R<group>" (clang 23.1.3's, as docs/c-preprocessor.md has them).
static const char* const pp_remark_groups[] = {
    "ctu-remarks", "module-build", "module-import", "module-include-translation", "module-lock", "module-map", "module-validation", "modules-driver", "pass",
    "pass-analysis", "pass-missed", "remark-backend-plugin", "round-trip-cc1-args", "sanitize-address", "search-path-usage", "sloc-usage",
};

// What a #pragma clang diagnostic made of a warning group (#1447): nothing, or its severity from there on.
// Room in Preprocessor.warning_saved past depth `depth` (#1934): a buffer of twice the room in the arena, the old one's
// mappings copied into it (cold: only nested pushes past the room grow it). A fatal error if the arena has none.
static __attribute__((noinline, cold)) void pp_grow_pragma_mappings(Preprocessor* pp, uint32_t depth)
{
    const uint64_t wanted = (uint64_t)depth + 1;
    uint64_t capacity = pp->warning_capacity != 0 ? (uint64_t)pp->warning_capacity * 2 : 64;
    capacity = capacity > wanted ? capacity : wanted;
    uint8_t* grown = capacity <= UINT32_MAX / BUSTER_PP_WARNINGS ? arena_push(&pp->arena, capacity * BUSTER_PP_WARNINGS, 1) : 0;
    if (grown != 0)
    {
        if (pp->warning_capacity != 0)
        {
            memcpy(grown, pp->warning_saved, (uint64_t)pp->warning_capacity * BUSTER_PP_WARNINGS);
        }
        pp->warning_saved = grown;
        pp->warning_capacity = (uint32_t)capacity;
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "too many nested #pragma clang diagnostic pushes");
    }
}

// #pragma GCC diagnostic and #pragma clang diagnostic, on their operands [begin, end) (unexpanded). clang prints one only
// when it takes it, in its own form: push; pop with a push to pop (the two namespaces share the pushes); and ignored,
// warning, error or fatal with a string (ordinary literals joined; another first token is an error) that names a
// warning group ("-W", the table of __has_warning's names, or "-Weverything") or a remark group ("-R"), and nothing after
// it. Anything else is a warning, and isn't printed. The severities aren't applied to this preprocessor's diagnostics.
static void pp_pragma_diagnostic(Preprocessor* pp, const PPTokens* tokens, uint32_t names_begin, uint32_t begin, uint32_t end, uint32_t location, bool from_operator)
{
    const bool push = pp_token_is_name(pp, tokens, begin, end, "push");
    const bool pop = pp_token_is_name(pp, tokens, begin, end, "pop");
    const bool severity = pp_token_is_name(pp, tokens, begin, end, "ignored") || pp_token_is_name(pp, tokens, begin, end, "warning") ||
                          pp_token_is_name(pp, tokens, begin, end, "error") || pp_token_is_name(pp, tokens, begin, end, "fatal");
    bool printed = false;
    uint8_t text[512];
    uint32_t length = BUSTER_PP_NONE;
    if (push || (pop && pp->diagnostic_depth != 0))
    {
        // (The warning groups' mappings, #1934: saved by a push, restored by its pop.)
        const uint32_t saved = push ? pp->diagnostic_depth : pp->diagnostic_depth - 1;
        if (push && saved >= pp->warning_capacity)
        {
            pp_grow_pragma_mappings(pp, saved);
        }
        for (uint32_t g = 0; saved < pp->warning_capacity && g < BUSTER_PP_WARNINGS; g += 1)
        {
            uint8_t* slot = pp->warning_saved + (uint64_t)saved * BUSTER_PP_WARNINGS + g;
            *slot = push ? pp->warning_mappings[g] : *slot;
            pp->warning_mappings[g] = *slot;
        }
        pp->diagnostic_depth = push ? pp->diagnostic_depth + 1 : pp->diagnostic_depth - 1;
        printed = true;
        if (begin + 1 < end)
        {
            pp_diagnose_group(pp, PP_WARNING_UNKNOWN_PRAGMAS, tokens->locations[begin + 1], "unexpected token in pragma diagnostic");
        }
    }
    else if (severity)
    {
        uint32_t i = begin + 1;
        length = pp_pragma_strings(pp, tokens, &i, end, text + 1, sizeof(text) - 2, "pragma diagnostic", location);
        const uint8_t* name = text + 1;
        const bool other = length != BUSTER_PP_NONE && i != end;
        const bool option = length != BUSTER_PP_NONE && !other && length >= 3 && name[0] == '-' && (name[1] == 'W' || name[1] == 'R');
        bool known = option && length == 12 && memcmp(name + 2, "everything", 10) == 0;
        known |= option && name[1] == 'W' && pp_feature(&pp->options, PP_BUILTIN_HAS_WARNING, name, length) != 0;
        for (uint32_t g = 0; option && name[1] == 'R' && g < BUSTER_ARRAY_LENGTH(pp_remark_groups); g += 1)
        {
            known |= strlen(pp_remark_groups[g]) == length - 2 && memcmp(pp_remark_groups[g], name + 2, length - 2) == 0;
        }
        if (other)
        {
            pp_diagnose_group(pp, PP_WARNING_UNKNOWN_PRAGMAS, tokens->locations[i], "unexpected token in pragma diagnostic");
        }
        else if (length != BUSTER_PP_NONE && !known)
        {
            pp_diagnose_group(pp, option ? PP_WARNING_UNKNOWN_WARNING_OPTION : PP_WARNING_UNKNOWN_PRAGMAS, location, "%s%.*s%s",
                              option ? "unknown warning group '" : "pragma diagnostic expected option name (e.g. \"-W", (int)length, (const char*)name,
                              option ? "', ignored" : "\")");
        }
        printed = known;
        // The mapping from here on of each warning group it names (#1934): by its name, its old one, a group that holds it, or
        // everything.
        const uint8_t mapping = pp_token_is_name(pp, tokens, begin, end, "ignored") ? PP_PRAGMA_MAPPING_IGNORED :
                                pp_token_is_name(pp, tokens, begin, end, "warning") ? PP_PRAGMA_MAPPING_WARNING :
                                pp_token_is_name(pp, tokens, begin, end, "error") ? PP_PRAGMA_MAPPING_ERROR : PP_PRAGMA_MAPPING_FATAL;
        for (uint32_t g = 0; known && name[1] == 'W' && g < BUSTER_PP_WARNINGS; g += 1)
        {
            pp->warning_mappings[g] = pp_warning_group_named(g, (const char*)name + 2, length - 2) ? mapping : pp->warning_mappings[g];
        }
    }
    else
    {
        uint32_t diagnostic_location = begin < end ? tokens->locations[begin] : location;
        const uint32_t range = pop && begin < end ? pp_find_range(pp, diagnostic_location) : BUSTER_PP_NONE;
        if (range != BUSTER_PP_NONE)
        {
            const PPRange* source = pp->ranges + range;
            const uint32_t after = pp_identifier_end(pp, source->bytes, source->length, diagnostic_location - source->base);
            const ScalarLexer lexer = { .source = source->bytes, .length = source->length, .trigraphs = pp->options.trigraphs };
            diagnostic_location = source->base + (uint32_t)pp_skip_blanks(&lexer, after);
        }
        pp_diagnose_group(pp, PP_WARNING_UNKNOWN_PRAGMAS, diagnostic_location, "%s",
                          pop ? "pragma diagnostic pop could not pop, no matching push" : "pragma diagnostic expected 'error', 'warning', 'ignored', 'fatal', 'push', or 'pop'");
    }
    if (printed)
    {
        pp_emit_pragma_start(pp, C_NAME_PRAGMA, location, from_operator);
        pp_emit_pragma_token(pp, pp_stack_token(tokens, names_begin), true);
        pp_emit_pragma_token(pp, pp_stack_token(tokens, names_begin + 1), true);
        pp_emit_pragma_token(pp, pp_stack_token(tokens, begin), true);
        if (severity)
        {
            text[0] = '"';
            text[length + 1] = '"';
            pp_emit(pp, pp_scratch_token(pp, C_TOKEN_STRING_LITERAL, text, length + 2, BUSTER_PP_SPACE));
        }
        pp_emit(pp, (PPToken){ .kind = BUSTER_PP_KIND_PRAGMA_END });
    }
}

// push_macro("NAME") and pop_macro("NAME"), on their operands [begin, end) (expanded); anything else is an error. As in
// clang, the name is the string's spelling between its quotes, and tokens after the ')' are left alone.
static void pp_pragma_push_pop(Preprocessor* pp, PPPragmaKind kind, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location)
{
    const bool valid = begin + 2 < end && tokens->kinds[begin] == C_TOKEN_L_PAREN && tokens->kinds[begin + 1] == C_TOKEN_STRING_LITERAL && tokens->kinds[begin + 2] == C_TOKEN_R_PAREN;
    Scratch scratch = scratch_begin(0);
    const uint32_t room = valid ? tokens->values[begin + 1] - tokens->locations[begin + 1] + 1 : 1;
    uint8_t* buffer = arena_push(scratch.arena, room, 1);
    const uint8_t* spelling = 0;
    const uint32_t spelled = valid && buffer != 0 ? pp_spelling(pp, C_TOKEN_STRING_LITERAL, tokens->values[begin + 1], tokens->locations[begin + 1], buffer, &spelling) : 0;
    const IdentId name = spelled >= 2 ? intern_find_or_add(pp->table, spelling + 1, spelled - 2) : 0;
    scratch_end(scratch);
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "pragma %s requires a parenthesized string", kind == PP_PRAGMA_PUSH_MACRO ? "push_macro" : "pop_macro");
    }
    else if (name != 0 && kind == PP_PRAGMA_PUSH_MACRO && pp->pushed_count < (BUSTER_PP_MAX_PUSHED >> pp->capacity_shift))
    {
        pp->pushed_names[pp->pushed_count] = name;
        pp->pushed_macros[pp->pushed_count] = pp->macro_of[name];
        pp->pushed_count += 1;
        if (pp->macro_of[name] != 0)
        {
            pp->macros[pp->macro_of[name]].flags |= BUSTER_PP_MACRO_REDEFINABLE;
        }
    }
    else if (name != 0 && kind == PP_PRAGMA_POP_MACRO)
    {
        for (uint32_t i = pp->pushed_count; i > 0; i -= 1)
        {
            if (pp->pushed_names[i - 1] == name)
            {
                pp_set_macro(pp, name, pp->pushed_macros[i - 1]);
                memmove(pp->pushed_names + i - 1, pp->pushed_names + i, (pp->pushed_count - i) * sizeof(IdentId));
                memmove(pp->pushed_macros + i - 1, pp->pushed_macros + i, (pp->pushed_count - i) * sizeof(PPMacroId));
                pp->pushed_count -= 1;
                break;
            }
        }
    }
}

// #ident and #sccs, on their operands [begin, end) (expanded when the first is a macro): a string literal, narrow or wide,
// is printed as `#ident "..."` (for #sccs too, as clang prints it), and the tokens after it are dropped with a warning;
// anything else is an error.
static void pp_ident(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location)
{
    if (begin < end && (tokens->kinds[begin] == C_TOKEN_STRING_LITERAL || tokens->kinds[begin] == C_TOKEN_WIDE_STRING_LITERAL))
    {
        if (begin + 1 < end)
        {
            pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, tokens->locations[begin + 1], "extra tokens at end of #ident directive");
        }
        pp_emit_pragma(pp, tokens, begin, begin + 1, C_NAME_IDENT, location, false);
    }
    else
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, begin < end ? tokens->locations[begin] : location, "invalid #ident directive");
    }
}

// #pragma clang assume_nonnull, arc_cf_code_audited or unsafe_buffer_usage, on their operands [begin, end): begin or
// end (anything else is an error), with clang's rules: beginning an open region or ending one that isn't is an error.
// Only assume_nonnull is printed, as `#pragma clang assume_nonnull begin` (or end), but not an end that is an error.
// A file that ends inside an assume_nonnull or arc_cf_code_audited region, an #include inside one, and the translation
// unit's end inside unsafe_buffer_usage are errors too (pp_pop_context, pp_include_file).
static void pp_pragma_region(Preprocessor* pp, const PPTokens* tokens, uint32_t names_begin, uint32_t begin, uint32_t end, uint32_t location, bool from_operator)
{
    const uint32_t region = pp_token_is_name(pp, tokens, names_begin + 1, begin, "assume_nonnull") ? BUSTER_PP_REGION_ASSUME_NONNULL
                            : pp_token_is_name(pp, tokens, names_begin + 1, begin, "arc_cf_code_audited") ? BUSTER_PP_REGION_ARC_CF
                                                                                                      : BUSTER_PP_REGION_UNSAFE_BUFFER;
    const bool opening = pp_token_is_name(pp, tokens, begin, end, "begin");
    const bool closing = pp_token_is_name(pp, tokens, begin, end, "end");
    const char* quoted = region == BUSTER_PP_REGION_UNSAFE_BUFFER ? "#pragma unsafe_buffer_usage" : region == BUSTER_PP_REGION_ARC_CF ? "#pragma clang arc_cf_code_audited"
                                                                                                                                    : "#pragma clang assume_nonnull";
    bool printed = false;
    if (!opening && !closing)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, begin < end ? tokens->locations[begin] : location, "expected 'begin' or 'end'");
    }
    else if (opening)
    {
        if (pp->region_locations[region] != 0)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, "already inside '%s'", quoted);
        }
        pp->region_locations[region] = location;
        printed = true;
    }
    else if (pp->region_locations[region] == 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "not currently inside '%s'", quoted);
    }
    else
    {
        pp->region_locations[region] = 0;
        printed = true;
    }
    if (printed && region != BUSTER_PP_REGION_UNSAFE_BUFFER && begin + 1 < end)
    {
        pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, tokens->locations[begin + 1], "extra tokens at end of #pragma directive");
    }
    if (printed && region == BUSTER_PP_REGION_ASSUME_NONNULL)
    {
        // clang's -E places an end on the line before its directive's (with a blank line after it): at the line break
        // that ends that line.
        uint32_t placed = location;
        const uint32_t range = closing && !from_operator ? pp_find_range(pp, location) : BUSTER_PP_NONE;
        if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
        {
            const uint8_t* bytes = pp->ranges[range].bytes;
            uint32_t offset = location - pp->ranges[range].base;
            while (offset > 0 && bytes[offset - 1] != '\n')
            {
                offset -= 1;
            }
            placed = pp->ranges[range].base + (offset > 0 ? offset - 1 : 0);
        }
        pp_emit_pragma(pp, tokens, names_begin, begin + 1, C_NAME_PRAGMA, placed, from_operator);
    }
}

// #pragma clang deprecated, final or restrict_expansion, on their operands [begin, end): '(', the name of a macro
// defined now, for deprecated and restrict_expansion an optional ',' and a string, and ')' (anything else is an error);
// not printed. The warnings they ask for on later uses aren't given.
static void pp_pragma_macro_note(Preprocessor* pp, const PPTokens* tokens, uint32_t names_begin, uint32_t begin, uint32_t end, uint32_t location)
{
    const bool message = !pp_token_is_name(pp, tokens, names_begin + 1, begin, "final");
    uint32_t i = begin;
    const char* expected = 0;
    if (i >= end || tokens->kinds[i] != C_TOKEN_L_PAREN)
    {
        expected = "(";
    }
    else if (i + 1 >= end || tokens->kinds[i + 1] != C_TOKEN_RAW_IDENTIFIER)
    {
        expected = "identifier";
        i += 1;
    }
    else if (!pp_is_defined(pp, tokens->values[i + 1]))
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, tokens->locations[i + 1], "no macro named '%.*s'", (int)intern_length(pp->table, tokens->values[i + 1]),
                    intern_bytes(pp->table, tokens->values[i + 1]));
    }
    else
    {
        i += 2;
        uint8_t text[512];
        bool valid = true;
        if (message && i < end && tokens->kinds[i] == C_TOKEN_COMMA)
        {
            i += 1;
            valid = pp_pragma_strings(pp, tokens, &i, end, text, sizeof(text), "pragma", location) != BUSTER_PP_NONE;
        }
        expected = valid && (i >= end || tokens->kinds[i] != C_TOKEN_R_PAREN) ? ")" : 0;
    }
    if (expected != 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, i < end ? tokens->locations[i] : location, "expected %s", expected);
    }
}

// Acts on a pragma of `kind`, with its names [names_begin, operands) of `names` and its operands [begin, end) of
// `tokens` (expanded, for the kinds clang expands).
static void pp_pragma_run(Preprocessor* pp, PPPragmaKind kind, const PPTokens* names, uint32_t names_begin, uint32_t operands, const PPTokens* tokens, uint32_t begin,
                          uint32_t end, uint32_t location, bool from_operator)
{
    const uint32_t file = pp_current_file(pp);
    switch (kind)
    {
        case PP_PRAGMA_ONCE:
        {
            if (begin < end)
            {
                pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, location, "extra tokens at end of #pragma once directive");
            }
            if (file != BUSTER_PP_NONE && file != pp->main_file)
            {
                pp->datas[pp->files[file].data].once = true;
            }
            else
            {
                pp_diagnose_group(pp, PP_WARNING_PRAGMA_ONCE_OUTSIDE_HEADER, location, "#pragma once in main file");
            }
        } break;
        case PP_PRAGMA_QUIET:
        {
        } break;
        case PP_PRAGMA_PUSH_MACRO:
        case PP_PRAGMA_POP_MACRO:
        {
            pp_pragma_push_pop(pp, kind, tokens, begin, end, location);
        } break;
        case PP_PRAGMA_MESSAGE:
        case PP_PRAGMA_WARNING:
        case PP_PRAGMA_ERROR:
        {
            pp_pragma_message(pp, kind, names, names_begin, tokens, begin, end, location, from_operator);
        } break;
        case PP_PRAGMA_POISON:
        {
            // Each identifier until the line's end; another token is an error, which ends the pragma.
            bool valid = true;
            for (uint32_t i = begin; valid && i < end; i += 1)
            {
                valid = tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER;
                if (valid)
                {
                    pp_poison(pp, tokens->values[i], tokens->locations[i]);
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, tokens->locations[i], "can only poison identifier tokens");
                }
            }
        } break;
        case PP_PRAGMA_SYSTEM_HEADER:
        {
            // The rest of the file is a system header, as are its later inclusions (clang's MarkFileSystemHeader), and the
            // files it includes; a line note marks where, after the directive, with the presumed line and name kept.
            const PPContext* context = pp_current_file_context(pp);
            if (file != BUSTER_PP_NONE && file != pp->main_file && context != 0)
            {
                pp->files[file].system = BUSTER_PP_SYSTEM_FILE;
                const PPFileData* data = &pp->datas[context->data];
                const PPRange* range = &pp->ranges[context->range];
                // A change of file for clang's printer too (FileChanged with SystemHeaderPragma), which starts a line
                // where the directive's line ends: the note starts there, before blanks and lines without tokens, so that
                // an #embed's tokens, which start the line there (pp_embed_line), are in the system header. A _Pragma's
                // note starts at the token after it.
                const uint32_t next = range->base + (context->position < data->token_count ? data->starts[context->position] : data->length);
                const uint32_t at = from_operator ? next : pp_directive_resume(pp, context, context->next_directive - 1);
                const uint32_t latest = range->last_line_directive;
                pp_add_line_note(pp, context->range, at, latest != BUSTER_PP_NONE ? pp->line_directives[latest].delta : 0, 0, 0, BUSTER_PP_SYSTEM_FILE, 0);
                pp->file_change_token = pp->output.count;
                pp->file_change_location = at;
            }
            else
            {
                pp_diagnose_group(pp, PP_WARNING_PRAGMA_SYSTEM_HEADER_OUTSIDE_HEADER, location, "#pragma system_header ignored in main file");
            }
        } break;
        case PP_PRAGMA_DEPENDENCY:
        {
            // A header name, which the include search must find (a fatal error otherwise); whether it is newer than this
            // file is only a warning, left out.
            uint8_t name[PATH_MAX];
            uint32_t length = 0;
            bool angled = false;
            uint32_t stop = begin;
            if (stop < end && tokens->kinds[stop] == C_TOKEN_LESS)
            {
                while (stop < end && tokens->kinds[stop] != C_TOKEN_GREATER)
                {
                    stop += 1;
                }
            }
            stop += stop < end;
            if (begin < end && pp_header_name(pp, tokens, begin, stop, name, sizeof(name), &length, &angled))
            {
                const PPContext* includer = pp_current_file_context(pp);
                uint32_t found_in;
                const uint32_t found = pp_find_include(pp, name, length, angled, false, includer != 0 ? includer->file : BUSTER_PP_NONE, BUSTER_PP_NONE, &found_in,
                                                       BUSTER_PP_NONE);
                if (found == BUSTER_PP_NONE)
                {
                    pp_diagnose(pp, PP_SEVERITY_FATAL, location, "'%.*s' file not found", (int)length, (const char*)name);
                }
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected \"FILENAME\" or <FILENAME>");
            }
        } break;
        case PP_PRAGMA_DIAGNOSTIC:
        {
            pp_pragma_diagnostic(pp, tokens, names_begin, begin, end, location, from_operator);
        } break;
        case PP_PRAGMA_OMP:
        case PP_PRAGMA_SEMANTIC:
        {
            pp_emit_pragma_start(pp, C_NAME_PRAGMA, location, from_operator);
            for (uint32_t i = names_begin; i < operands; i += 1)
            {
                pp_emit_pragma_token(pp, pp_stack_token(names, i), i == names_begin);
            }
            for (uint32_t i = begin; i < end; i += 1)
            {
                pp_emit_pragma_token(pp, pp_stack_token(tokens, i), i == begin);
            }
            pp_emit(pp, (PPToken){ .kind = BUSTER_PP_KIND_PRAGMA_END });
        } break;
        case PP_PRAGMA_IDENT:
        {
            pp_ident(pp, tokens, begin, end, location);
        } break;
        case PP_PRAGMA_REGION:
        {
            pp_pragma_region(pp, tokens, names_begin, begin, end, location, from_operator);
        } break;
        case PP_PRAGMA_MACRO_NOTE:
        {
            pp_pragma_macro_note(pp, tokens, names_begin, begin, end, location);
        } break;
        case PP_PRAGMA_OTHER:
        {
            pp_emit_pragma(pp, names, names_begin, end, C_NAME_PRAGMA, location, from_operator);
        } break;
    }
}

// A pragma's tokens [begin, end) of pp->arguments (after `pragma`), or an #ident's (`ident`): the ones the preprocessor
// acts on (pp_pragma_kind), and any other goes to the output as clang -E prints it. `from_operator` for _Pragma. The
// kinds clang reads expanded (message, GCC warning and error, push_macro, pop_macro, omp and #ident) whose operands name
// a macro are expanded first, through a capture that pp_finish_pragma takes: pp->arguments must then stay as they are
// until it does, and it gives them back to `restore` (returning true). Otherwise the pragma runs now, and the caller
// gives them back.
static bool pp_pragma(Preprocessor* pp, uint32_t begin, uint32_t end, uint32_t location, bool from_operator, bool ident, uint32_t restore)
{
    const PPTokens* tokens = &pp->arguments;
    uint32_t operands = begin;
    const PPPragmaKind kind = ident ? PP_PRAGMA_IDENT : pp_pragma_kind(pp, tokens, begin, end, &operands);
    if (pp->poison_count != 0)
    {
        // The line's poisoned names (pp_is_poisoned), but poison's own, which clang reads raw, and a dependency's
        // header name.
        pp_check_poison_range(pp, 0, 0, tokens, begin, kind == PP_PRAGMA_POISON ? operands : end, kind == PP_PRAGMA_DEPENDENCY ? operands : BUSTER_PP_NONE, false);
    }
    const bool expanded = kind == PP_PRAGMA_MESSAGE || kind == PP_PRAGMA_WARNING || kind == PP_PRAGMA_ERROR || kind == PP_PRAGMA_PUSH_MACRO || kind == PP_PRAGMA_POP_MACRO ||
                          kind == PP_PRAGMA_OMP || kind == PP_PRAGMA_SEMANTIC || kind == PP_PRAGMA_IDENT;
    bool started = false;
    // Not among a macro's arguments (or a builtin's operand), where clang's macro expansion is off (as for #line). An
    // #ident reads its string expanded (Preprocessor::HandleIdentSCCSDirective), and push_macro and pop_macro their '(',
    // string and ')' (ParsePragmaPushOrPopMacro); clang reads the rest of their lines as it is (pp_check_limit).
    if (expanded && !pp_collecting(pp) && pp_needs_expansion(pp, tokens, operands, end))
    {
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_PRAGMA,
            .arguments_begin = restore,
            .directive = begin,
            .context = operands,
            .directive_kind = (uint8_t)kind,
            .flags = from_operator,
            .location = location,
            .limit = kind == PP_PRAGMA_IDENT ? 1 : kind == PP_PRAGMA_PUSH_MACRO || kind == PP_PRAGMA_POP_MACRO ? 3 : 0,
        };
        started = pp_start_capture(pp, capture, operands, end, location);
    }
    if (!started)
    {
        pp_pragma_run(pp, kind, tokens, begin, operands, tokens, operands, end, location, from_operator);
    }
    return started;
}

// The barrier over a pragma's operands ended (pp_pragma): the capture is the operands expanded. They are copied out first,
// since what the pragma emits may go to a capture below this one.
static void pp_finish_pragma(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    pp->capture_count -= 1;
    const uint32_t count = pp->captures.count - capture.begin;
    const uint32_t names = capture.context - capture.directive;
    Scratch scratch = scratch_begin(0);
    PPTokens copy = { 0 };
    copy.kinds = arena_push(scratch.arena, (uint64_t)count + names + 1, 1);
    copy.flags = arena_push(scratch.arena, (uint64_t)count + names + 1, 1);
    copy.values = arena_push_array(scratch.arena, (uint64_t)count + names + 1, sizeof(uint32_t), 4);
    copy.locations = arena_push_array(scratch.arena, (uint64_t)count + names + 1, sizeof(uint32_t), 4);
    copy.origins = !pp->options.no_history ? arena_push_array(scratch.arena, (uint64_t)count + names + 1, sizeof(uint32_t), 4) : NULL;
    if (copy.kinds != 0 && copy.flags != 0 && copy.values != 0 && copy.locations != 0 && (pp->options.no_history || copy.origins != NULL))
    {
        // The names first, then the expanded operands. The copy outlives this capture's reset, so it must retain
        // the tokens' origins as well as their spelling locations for later semantic diagnostics.
        for (uint32_t i = 0; i < names; i += 1)
        {
            const PPToken token = pp_stack_token(&pp->arguments, capture.directive + i);
            pp_tokens_set(&copy, i, token.kind, token.flags, token.value, token.location);
            if (copy.origins != NULL)
            {
                copy.origins[i] = token.origin;
            }
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const PPToken token = pp_stack_token(&pp->captures, capture.begin + i);
            pp_tokens_set(&copy, names + i, token.kind, token.flags, token.value, token.location);
            if (copy.origins != NULL)
            {
                copy.origins[names + i] = token.origin;
            }
        }
        copy.count = names + count;
        pp->captures.count = capture.begin;
        pp->arguments.count = capture.arguments_begin;
        pp_pragma_run(pp, (PPPragmaKind)capture.directive_kind, &copy, 0, names, &copy, names, names + count, capture.location, capture.flags != 0);
    }
    else
    {
        pp->captures.count = capture.begin;
        pp->arguments.count = capture.arguments_begin;
        pp_diagnose(pp, PP_SEVERITY_FATAL, capture.location, "out of memory");
    }
    scratch_end(scratch);
}

// Runs a directive after its active opaque context has been recovered, if needed. The context goes on after its line
// unless a conditional skipped elsewhere.
static void pp_prepared_directive(Preprocessor* pp, PPContext* context)
{
    const uint32_t d = context->next_directive;
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t data_index = context->data;
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint8_t kind = data->directive_kind[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    const PPConditional* group = pp->conditional_count > context->conditional_base ? &pp->conditionals[pp->conditional_count - 1] : 0;
    const bool condition = kind == PP_DIRECTIVE_IF || (kind == PP_DIRECTIVE_ELIF && group != 0 && !group->seen_else && !group->taken);
    if (!condition)
    {
        pp_read_directive_comment(pp, context, d);
        const bool text = kind == PP_DIRECTIVE_ERROR || kind == PP_DIRECTIVE_WARNING;
        if (!text)
        {
            pp_read_raw_errors(pp, context, hash, end);
        }
    }
    context->position = end;
    context->next_directive = d + 1;
    pp->statistics.directives += pp->options.timing;
    if (pp->poison_count != 0 && kind != PP_DIRECTIVE_PRAGMA && kind != PP_DIRECTIVE_IDENT)
    {
        // The line's poisoned names (pp_is_poisoned): #error's and #warning's text isn't lexed, a pragma's are
        // pp_pragma's, and an #include's header name is one token. An #embed's line past an angled name is pp_embed's,
        // as clang may lex it otherwise than the file's tokens (pp_header_name_tail).
        const bool text = kind == PP_DIRECTIVE_ERROR || kind == PP_DIRECTIVE_WARNING;
        const bool include = kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT || kind == PP_DIRECTIVE_EMBED;
        const bool angled_embed = kind == PP_DIRECTIVE_EMBED && hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_LESS;
        pp_check_poison_range(pp, data, base, 0, hash + 1, text || angled_embed ? hash + 2 : end, include ? hash + 2 : BUSTER_PP_NONE, true);
    }
    switch (kind)
    {
        case PP_DIRECTIVE_NULL:
            break;
        case PP_DIRECTIVE_DEFINE:
            pp_define(pp, data_index, base, hash + 2, end);
            break;
        case PP_DIRECTIVE_UNDEF:
            // As clang: the name, then the line's end, then whether it undefines a builtin.
            if (hash + 2 < end && (data->kinds[hash + 2] & 0x7F) == C_TOKEN_RAW_IDENTIFIER)
            {
                const IdentId name = data->values[hash + 2];
                const uint32_t name_location = pp_define_location(pp, data, base, hash + 2, end);
                if (name == C_NAME_DEFINED)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "'defined' cannot be used as a macro name");
                }
                else
                {
                    pp_reserved_macro_name(pp, data, name, name_location, false);
                    if (hash + 3 < end)
                    {
                        pp_diagnose_group(pp, PP_WARNING_EXTRA_TOKENS, pp_define_location(pp, data, base, hash + 3, end), "extra tokens at end of #undef directive");
                    }
                    const PPMacroId previous = pp->macro_of[name];
                    if (previous != 0 && pp_language_defined(pp, &pp->macros[previous]))
                    {
                        pp_diagnose_group(pp, PP_WARNING_BUILTIN_MACRO_REDEFINED, name_location, "undefining builtin macro");
                    }
                    pp_set_macro(pp, name, 0);
                }
            }
            else if (hash + 2 < end)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_define_location(pp, data, base, hash + 2, end), "macro name must be an identifier");
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, pp_line_end_location(pp, data, base, hash + 1), "macro name missing");
            }
            break;
        case PP_DIRECTIVE_INCLUDE:
        case PP_DIRECTIVE_INCLUDE_NEXT:
        case PP_DIRECTIVE_IMPORT:
            pp_include(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_IF:
        case PP_DIRECTIVE_IFDEF:
        case PP_DIRECTIVE_IFNDEF:
        case PP_DIRECTIVE_ELIF:
        case PP_DIRECTIVE_ELIFDEF:
        case PP_DIRECTIVE_ELIFNDEF:
        case PP_DIRECTIVE_ELSE:
        case PP_DIRECTIVE_ENDIF:
            pp_conditional(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_LINE:
        case PP_DIRECTIVE_LINE_MARKER:
            pp_line(pp, context, d, kind);
            break;
        case PP_DIRECTIVE_ERROR:
        case PP_DIRECTIVE_WARNING:
        {
            const uint32_t location = pp_token_location(pp, context, data, hash + 1);
            if (kind == PP_DIRECTIVE_WARNING)
            {
                // The directive itself, before its text (#1790): C23's, an extension before it.
                const bool c23 = pp->options.language.standard >= 2023;
                pp_diagnose_group(pp, c23 ? PP_WARNING_PRE_C23_COMPAT : PP_WARNING_PEDANTIC, location, "%s",
                                  c23 ? "#warning is incompatible with C standards before C23" : "#warning is a C23 extension");
            }
            pp_diagnostic_text(pp, data, hash + 1, location, kind == PP_DIRECTIVE_ERROR);
            break;
        }
        case PP_DIRECTIVE_PRAGMA:
        case PP_DIRECTIVE_IDENT:
        {
            if (kind == PP_DIRECTIVE_IDENT)
            {
                // #ident and #sccs, clang's extension under -pedantic (#2222), at the directive's name, before its string.
                pp_diagnose_group(pp, PP_WARNING_PEDANTIC, pp_token_location(pp, context, data, hash + 1), "#ident is a language extension");
            }
            // The line stays on the stack while a capture expands it (pp_pragma).
            const uint32_t line = pp->arguments.count;
            const bool started = pp_copy_line(pp, context, hash + 2, end) && pp_pragma(pp, line, pp->arguments.count, location, false, kind == PP_DIRECTIVE_IDENT, line);
            pp->arguments.count = started ? pp->arguments.count : line;
            break;
        }
        case PP_DIRECTIVE_EMBED:
            pp_embed(pp, context, d);
            break;
        case PP_DIRECTIVE_ASSERT:
        case PP_DIRECTIVE_UNKNOWN:
            if (!pp->options.assembly)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid preprocessing directive");
            }
            break;
    }
}

// Only a directive the main loop is executing can establish an active opaque message. Skipped lines retain their
// original lexing; a recovery failure is fatal and never dispatches the incomplete prepared directive.
static void pp_directive(Preprocessor* pp, PPContext* context)
{
    const uint32_t directive = context->next_directive;
    const PPFileData* data = &pp->datas[context->data];
    const uint8_t kind = data->directive_kind[directive];
    const bool text = kind == PP_DIRECTIVE_ERROR || kind == PP_DIRECTIVE_WARNING;
    // Only a header name's line can need the raw or comment repairs (pp_recover_direct_raw and
    // pp_recover_direct_header_comment test the kind first themselves).
    const bool header = kind == PP_DIRECTIVE_INCLUDE || kind == PP_DIRECTIVE_INCLUDE_NEXT || kind == PP_DIRECTIVE_IMPORT || kind == PP_DIRECTIVE_EMBED;
    if ((!text || pp_recover_diagnostic_line(pp, context, directive)) && (!header || (pp_recover_direct_raw(pp, context, directive) &&
                                                                                      pp_recover_direct_header_comment(pp, context, directive))))
    {
        // Recovery resumes a capture after its directive; this caller still has to dispatch that directive itself.
        context->next_directive = directive;
        pp_prepared_directive(pp, context);
    }
}

static const uint8_t pp_operator_precedence[] = {
    [PP_OPERATOR_PAREN] = 0,
    [PP_OPERATOR_PLUS] = 14,
    [PP_OPERATOR_MINUS] = 14,
    [PP_OPERATOR_NOT] = 14,
    [PP_OPERATOR_COMPLEMENT] = 14,
    [PP_OPERATOR_MULTIPLY] = 13,
    [PP_OPERATOR_DIVIDE] = 13,
    [PP_OPERATOR_REMAINDER] = 13,
    [PP_OPERATOR_ADD] = 12,
    [PP_OPERATOR_SUBTRACT] = 12,
    [PP_OPERATOR_SHIFT_LEFT] = 11,
    [PP_OPERATOR_SHIFT_RIGHT] = 11,
    [PP_OPERATOR_LESS] = 10,
    [PP_OPERATOR_LESS_EQUAL] = 10,
    [PP_OPERATOR_GREATER] = 10,
    [PP_OPERATOR_GREATER_EQUAL] = 10,
    [PP_OPERATOR_EQUAL] = 9,
    [PP_OPERATOR_NOT_EQUAL] = 9,
    [PP_OPERATOR_AND] = 8,
    [PP_OPERATOR_XOR] = 7,
    [PP_OPERATOR_OR] = 6,
    [PP_OPERATOR_LOGICAL_AND] = 5,
    [PP_OPERATOR_LOGICAL_OR] = 4,
    [PP_OPERATOR_QUESTION] = 3,
    [PP_OPERATOR_COLON] = 3,
    [PP_OPERATOR_COMMA] = 2,
};

// The binary operator of a token kind, or BUSTER_PP_NONE.
static uint32_t pp_binary_operator(uint8_t kind)
{
    uint32_t result = BUSTER_PP_NONE;
    switch (kind)
    {
        case C_TOKEN_STAR: result = PP_OPERATOR_MULTIPLY; break;
        case C_TOKEN_SLASH: result = PP_OPERATOR_DIVIDE; break;
        case C_TOKEN_PERCENT: result = PP_OPERATOR_REMAINDER; break;
        case C_TOKEN_PLUS: result = PP_OPERATOR_ADD; break;
        case C_TOKEN_MINUS: result = PP_OPERATOR_SUBTRACT; break;
        case C_TOKEN_LESSLESS: result = PP_OPERATOR_SHIFT_LEFT; break;
        case C_TOKEN_GREATERGREATER: result = PP_OPERATOR_SHIFT_RIGHT; break;
        case C_TOKEN_LESS: result = PP_OPERATOR_LESS; break;
        case C_TOKEN_LESSEQUAL: result = PP_OPERATOR_LESS_EQUAL; break;
        case C_TOKEN_GREATER: result = PP_OPERATOR_GREATER; break;
        case C_TOKEN_GREATEREQUAL: result = PP_OPERATOR_GREATER_EQUAL; break;
        case C_TOKEN_EQUALEQUAL: result = PP_OPERATOR_EQUAL; break;
        case C_TOKEN_EXCLAIMEQUAL: result = PP_OPERATOR_NOT_EQUAL; break;
        case C_TOKEN_AMP: result = PP_OPERATOR_AND; break;
        case C_TOKEN_CARET: result = PP_OPERATOR_XOR; break;
        case C_TOKEN_PIPE: result = PP_OPERATOR_OR; break;
        case C_TOKEN_AMPAMP: result = PP_OPERATOR_LOGICAL_AND; break;
        case C_TOKEN_PIPEPIPE: result = PP_OPERATOR_LOGICAL_OR; break;
        case C_TOKEN_QUESTION: result = PP_OPERATOR_QUESTION; break;
        case C_TOKEN_COLON: result = PP_OPERATOR_COLON; break;
        case C_TOKEN_COMMA: result = PP_OPERATOR_COMMA; break;
    }
    return result;
}

#include "buster/compiler/c_preprocessor_literal.c"

// Parses an integer constant's spelling (C 6.4.4.1, with C23's binary prefix, C2y's 0o and digit separators, and the suffixes
// u, l, ll, z, wb in any case and order; ll's and wb's letters in one case, #1933). Returns false (with an error) if it isn't one.
static bool pp_parse_integer(Preprocessor* pp, const uint8_t* spelling, uint32_t length, uint32_t location, PPValue* value)
{
    uint32_t i = 0;
    uint32_t radix = 10;
    if (length >= 2 && spelling[0] == '0' && (spelling[1] == 'x' || spelling[1] == 'X'))
    {
        radix = 16;
        i = 2;
    }
    else if (length >= 2 && spelling[0] == '0' && (spelling[1] == 'b' || spelling[1] == 'B'))
    {
        radix = 2;
        i = 2;
    }
    else if (length >= 3 && spelling[0] == '0' && (spelling[1] | 0x20) == 'o' && spelling[2] >= '0' && spelling[2] <= '7')
    {
        radix = 8;
        i = 2;
    }
    else if (spelling[0] == '0')
    {
        radix = 8;
    }
    unsigned __int128 accumulated = 0;
    bool overflow = false;
    bool digits = false;
    bool valid = true;
    while (valid && i < length)
    {
        const uint8_t c = spelling[i];
        int32_t digit = -1;
        if (c >= '0' && c <= '9')
        {
            digit = c - '0';
        }
        else if (radix == 16 && (c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        {
            digit = (c | 0x20) - 'a' + 10;
        }
        if (c == '\'' && digits)
        {
            i += 1;
        }
        else if (digit >= 0 && (uint32_t)digit < radix)
        {
            accumulated = accumulated * radix + (uint32_t)digit;
            overflow |= accumulated > UINT64_MAX;
            accumulated &= UINT64_MAX;
            digits = true;
            i += 1;
        }
        else
        {
            break;
        }
    }
    // The suffix.
    bool is_unsigned = false;
    uint32_t longs = 0;
    while (valid && i < length)
    {
        const uint8_t c = spelling[i] | 0x20;
        if (c == 'u' && !is_unsigned)
        {
            is_unsigned = true;
            i += 1;
        }
        else if (c == 'l' && longs == 0)
        {
            longs = i + 1 < length && spelling[i + 1] == spelling[i] ? 2 : 1;
            i += longs;
        }
        else if (c == 'z' && longs == 0)
        {
            longs = 3;
            i += 1;
        }
        else if (c == 'w' && i + 1 < length && (spelling[i + 1] ^ spelling[i]) == ('b' ^ 'w') && longs == 0)
        {
            longs = 4;
            i += 2;
        }
        else
        {
            valid = false;
        }
    }
    valid &= digits || radix == 8;
    if (valid && longs == 4 && pp->options.language.standard < 2023)
    {
        // As clang's NumericLiteralParser warns of the suffix (#1447), before the value's errors, as -Wc23-extensions' options
        // and pragmas map it (#1934).
        pp_diagnose_group(pp, PP_WARNING_C23_EXTENSIONS, location, "'_BitInt' suffix for literals is a C23 extension");
    }
    if (!valid)
    {
        pp_diagnose_integer(pp, spelling, length, location);
    }
    else if (overflow)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "integer literal is too large to be represented in any integer type");
    }
    else if (!is_unsigned && accumulated > INT64_MAX)
    {
        is_unsigned = true;
        if (radix == 10)
        {
            pp_diagnose_group(pp, PP_WARNING_IMPLICITLY_UNSIGNED_LITERAL, location, "integer literal is too large to be represented in a signed integer type, interpreting as unsigned");
        }
    }
    *value = (PPValue){ .bits = (uint64_t)accumulated, .is_unsigned = is_unsigned };
    return valid && !overflow;
}

BUSTER_INLINE void pp_characters_add(PPCharacters* characters, uint32_t c)
{
    characters->too_long |= (characters->packed >> 24) != 0;
    characters->packed = (characters->packed << 8) | (c & 0xFF);
    characters->last = c;
    characters->count += 1;
}

// The named universal character name \N{NAME} whose backslash is spelling[escape] of a character constant whose token is
// at `location`, as clang reads one in a literal (ProcessNamedUCNEscape), *at past its N: its '{', a '}' before a line
// break or the end, and a name between them, exactly Unicode's (unicode_name_strict), else errors at the escape (or
// the name), a bad name's with clang's notes (unicode_name_diagnose). Moves *at past it, and returns whether its name is
// a character's, which is `*code_point`.
static bool pp_parse_named_ucn(Preprocessor* pp, const uint8_t* spelling, uint32_t end, uint32_t* at, uint32_t escape, uint32_t location, uint32_t* code_point)
{
    const uint32_t backslash = pp_spelling_offset_location(pp, location, escape);
    uint32_t i = *at;
    bool result = false;
    if (i == end || spelling[i] != '{')
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, backslash, "expected '{' after '\\N' escape sequence");
    }
    else
    {
        i += 1;
        uint32_t close = i;
        while (close < end && spelling[close] != '}' && spelling[close] != '\n' && spelling[close] != '\r')
        {
            close += 1;
        }
        if (close == end || close == i)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, backslash, close == end ? "incomplete universal character name" : "delimited escape sequence cannot be empty");
            i = close == end ? close : close + 1;
        }
        else
        {
            result = unicode_name_strict(spelling + i, close - i, code_point);
            if (!result)
            {
                pp_diagnose_named_ucn_name(pp, spelling + i, close - i, location, i);
            }
            i = close + 1;
        }
    }
    *at = i;
    return result;
}

// The universal character name at spelling[*at] (its backslash) of a character constant, as clang reads one there
// (ProcessUCNEscape): \u and four hex digits, \U and eight, or \u{...}; one that names a surrogate or a code point
// past U+10FFFF is an error, and so before C23 is one below U+00A0 but $, @ and `. \N{name} names a character by
// Unicode's name for it (pp_parse_named_ucn). Moves *at past it, and returns whether it is valid, with its code point.
static bool pp_parse_ucn(Preprocessor* pp, const uint8_t* spelling, uint32_t end, uint32_t* at, uint32_t location, uint32_t* code_point)
{
    const uint8_t letter = spelling[*at + 1];
    uint32_t i = *at + 2;
    uint32_t value = 0;
    bool result = false;
    if (letter == 'N')
    {
        // Its diagnostics at their own bytes, as clang's.
        const uint32_t escape = pp_spelling_offset_location(pp, location, *at);
        result = pp_parse_named_ucn(pp, spelling, end, &i, *at, location, &value);
        if (result && value < 0xA0 && value != 0x24 && value != 0x40 && value != 0x60 && pp->options.language.standard < 2023)
        {
            pp_diagnose_ucn_basic(pp, escape, value);
            result = false;
        }
        else if (result && pp->options.language.standard < 1999)
        {
            pp_diagnose_group(pp, PP_WARNING_UNICODE_C99, escape, "universal character names are only valid in C99 or C++");
        }
    }
    else
    {
        const bool delimited = letter == 'u' && i < end && spelling[i] == '{';
        const uint32_t wanted = letter == 'u' ? 4 : 8;
        if (!delimited && (i >= end || hex_digit_value(spelling[i]) < 0))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, "\\%c used with no following hex digits", letter);
        }
        else
        {
            i += delimited;
            bool overflow = false;
            bool invalid = false;
            bool closed = false;
            bool stopped = false;
            uint32_t count = 0;
            while (!stopped && i < end && (delimited || count != wanted))
            {
                const int32_t digit = hex_digit_value(spelling[i]);
                if (delimited && spelling[i] == '}')
                {
                    closed = true;
                    stopped = true;
                }
                else if (digit < 0 && !delimited)
                {
                    invalid = true;
                    stopped = true;
                    i -= 1;
                }
                else if (digit < 0)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid digit '%c' in escape sequence", spelling[i]);
                    invalid = true;
                    count += 1;
                }
                else if (value & 0xF0000000u)
                {
                    overflow = true;
                }
                else
                {
                    value = (value << 4) | (uint32_t)digit;
                    count += 1;
                }
                i += 1;
            }
            if (overflow)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "hex escape sequence out of range");
            }
            else if (delimited && !closed)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected '}'");
            }
            else if (count == 0 || (!delimited && count != wanted))
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, delimited ? "delimited escape sequence cannot be empty" : "incomplete universal character name");
            }
            else if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid universal character");
            }
            else if (value < 0xA0 && value != 0x24 && value != 0x40 && value != 0x60 && pp->options.language.standard < 2023)
            {
                if (value >= 0x20 && value < 0x7F)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "character '%c' cannot be specified by a universal character name", (char)value);
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "universal character name refers to a control character");
                }
            }
            else
            {
                result = !invalid;
                if (result && pp->options.language.standard < 1999)
                {
                    pp_diagnose_group(pp, PP_WARNING_UNICODE_C99, location, "universal character names are only valid in C99 or C++");
                }
            }
        }
    }
    *at = i;
    *code_point = value;
    return result;
}

// The escape at spelling[*at] (its backslash) of a character constant whose code units are `width` bits wide, as clang
// reads it (ProcessCharEscape): its value, and *error for one that makes the constant invalid (a \x past the code
// unit, \o{} and \x{} that are empty, out of range or have another digit). An octal escape past the code unit and \x
// without digits are errors that leave a value, and an unknown escape is its character (a byte past ASCII as a signed
// char gives it), after a warning. Moves *at past it.
static uint32_t pp_parse_escape(Preprocessor* pp, const uint8_t* spelling, uint32_t end, uint32_t* at, uint32_t width, uint32_t location, bool* error)
{
    const uint8_t e = spelling[*at + 1];
    uint32_t i = *at + 2;
    uint32_t result = e;
    const uint32_t unit_mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    switch (e)
    {
        case 'a': result = 7; break;
        case 'b': result = 8; break;
        case 'f': result = 12; break;
        case 'n': result = 10; break;
        case 'r': result = 13; break;
        case 't': result = 9; break;
        case 'v': result = 11; break;
        case 'e': case 'E': result = 27; break;
        case '\\': case '\'': case '"': case '?': case '(': case '{': case '[': case '%': break;
        case 'x':
        case 'o':
        {
            // Hex digits, as many as there are, or in braces (\x{} and \o{}, whose digits are all checked).
            const bool hex = e == 'x';
            const bool delimited = i < end && spelling[i] == '{';
            bool overflow = false;
            bool invalid = false;
            bool closed = !delimited;
            bool stopped = false;
            uint32_t count = 0;
            result = 0;
            i += delimited;
            if (!hex && !delimited)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected '{' after '\\o' escape sequence");
                invalid = true;
                stopped = true;
                result = 'o';
            }
            while (!stopped && i < end)
            {
                const int32_t digit = hex ? hex_digit_value(spelling[i]) : spelling[i] >= '0' && spelling[i] <= '7' ? spelling[i] - '0' : -1;
                if (delimited && spelling[i] == '}')
                {
                    closed = true;
                    stopped = true;
                }
                else if (digit < 0 && !delimited)
                {
                    stopped = true;
                    i -= 1;
                }
                else if (digit < 0)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid digit '%c' in escape sequence", spelling[i]);
                    invalid = true;
                }
                else
                {
                    overflow |= (result & (hex ? 0xF0000000u : 0xE0000000u)) != 0;
                    result = (result << (hex ? 4 : 3)) | (uint32_t)digit;
                    count += 1;
                }
                i += 1;
            }
            if (hex && !delimited && count == 0)
            {
                // An error that leaves 0.
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "\\x used with no following hex digits");
            }
            else if (delimited && count == 0 && !invalid && closed)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "delimited escape sequence cannot be empty");
                invalid = true;
            }
            else if (!invalid && (overflow || (result & ~unit_mask) != 0))
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, hex ? "hex escape sequence out of range" : "octal escape sequence out of range");
                invalid = true;
            }
            if (delimited && !closed)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected '}'");
            }
            result &= unit_mask;
            *error |= invalid;
            break;
        }
        default:
            if (e >= '0' && e <= '7')
            {
                // One to three octal digits; past the code unit, an error that leaves its low bits.
                result = e - '0';
                for (uint32_t k = 1; k < 3 && i < end && spelling[i] >= '0' && spelling[i] <= '7'; k += 1)
                {
                    result = result * 8 + (uint32_t)(spelling[i] - '0');
                    i += 1;
                }
                if ((result & ~unit_mask) != 0)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "octal escape sequence out of range");
                    result &= unit_mask;
                }
            }
            else
            {
                result = (uint32_t)(int32_t)(int8_t)e;
                if (e >= 0x20 && e < 0x7F)
                {
                    pp_diagnose_group(pp, PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE, location, "unknown escape sequence '\\%c'", e);
                }
                else
                {
                    pp_diagnose_group(pp, PP_WARNING_UNKNOWN_ESCAPE_SEQUENCE, location, "unknown escape sequence '\\x%X'", result);
                }
            }
            break;
    }
    *at = i;
    return result;
}

// A character constant's value (C 6.4.4.4), as clang's CharLiteralParser and #if read it. Its characters are UTF-8
// sequences (an ordinary constant's bytes, after a warning, where a run of them isn't one: a constant of another kind
// can't have that), escapes (pp_parse_escape) and universal character names (pp_parse_ucn); one past the kind's
// largest (0x7F for an ordinary or a u8 constant, U+FFFF for u, U+10FFFF for U) is an error, and so are several in a
// constant that isn't ordinary. An ordinary constant is a char, or an int of its characters' bytes when it has several
// (a warning), and the others are their types' values: wchar_t's int, and u8, u and U's unsigned ones. Returns false
// after an error that leaves no value (as for clang's hadError); the other errors leave one.
static bool pp_parse_character(Preprocessor* pp, uint8_t kind, const uint8_t* spelling, uint32_t length, uint32_t location, PPValue* value)
{
    const bool ordinary = kind == C_TOKEN_CHAR_CONSTANT;
    const uint32_t largest = kind == C_TOKEN_WIDE_CHAR_CONSTANT ? (pp->options.predefines.short_wchar ? 0xFFFFu : 0xFFFFFFFFu)
                            : kind == C_TOKEN_UTF16_CHAR_CONSTANT ? 0xFFFFu : kind == C_TOKEN_UTF32_CHAR_CONSTANT ? 0x10FFFFu : 0x7Fu;
    const uint32_t width = kind == C_TOKEN_WIDE_CHAR_CONSTANT ? (pp->options.predefines.short_wchar ? 16 : 32)
                          : kind == C_TOKEN_UTF32_CHAR_CONSTANT ? 32 : kind == C_TOKEN_UTF16_CHAR_CONSTANT ? 16 : 8;
    uint32_t i = 0;
    while (i < length && spelling[i] != '\'')
    {
        i += 1;
    }
    i += 1;
    // The characters end at the closing quote, the last byte of a lexed constant.
    const uint32_t end = length > i && spelling[length - 1] == '\'' ? length - 1 : length;
    bool error = i > end || end == length;
    if (error)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid character constant in preprocessor expression");
    }
    PPCharacters characters = { 0 };
    while (!error && i < end)
    {
        if (spelling[i] != '\\' || i + 1 >= end)
        {
            // A run up to the next backslash: UTF-8 (clang converts it strictly), or for an ordinary constant, bytes.
            uint32_t run = i + 1;
            while (run < end && spelling[run] != '\\')
            {
                run += 1;
            }
            bool decoded = true;
            for (uint32_t k = i; decoded && k < run;)
            {
                uint32_t code_point;
                const uint32_t n = pp_utf8_sequence(spelling + k, run - k, &code_point);
                decoded = n != 0;
                k += n;
            }
            if (!decoded)
            {
                if (ordinary)
                {
                    pp_diagnose_group(pp, PP_WARNING_INVALID_SOURCE_ENCODING, location, "illegal character encoding in character literal");
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "illegal character encoding in character literal");
                }
                error |= !ordinary;
            }
            for (uint32_t k = i; k < run;)
            {
                uint32_t code_point = spelling[k];
                const uint32_t n = decoded ? pp_utf8_sequence(spelling + k, run - k, &code_point) : 1;
                if (decoded && code_point > largest)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, location, "character too large for enclosing character literal type");
                    error = true;
                }
                pp_characters_add(&characters, code_point);
                k += n;
            }
            i = run;
        }
        else if (spelling[i + 1] == 'u' || spelling[i + 1] == 'U' || spelling[i + 1] == 'N')
        {
            uint32_t code_point;
            bool valid = pp_parse_ucn(pp, spelling, end, &i, location, &code_point);
            if (valid && code_point > largest)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "character too large for enclosing character literal type");
                valid = false;
            }
            error |= !valid;
            pp_characters_add(&characters, code_point);
        }
        else
        {
            pp_characters_add(&characters, pp_parse_escape(pp, spelling, end, &i, width, location, &error));
        }
    }
    if (characters.count == 0 && !error)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "invalid character constant in preprocessor expression");
        error = true;
    }
    else if (characters.count > 1 && ordinary && characters.count != 4)
    {
        // Four characters have a warning of their own, which clang leaves off.
        pp_diagnose_group(pp, PP_WARNING_MULTICHAR, location, "multi-character character constant");
    }
    else if (characters.count > 1 && !ordinary)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "%s character literals may not contain multiple characters", kind == C_TOKEN_WIDE_CHAR_CONSTANT ? "wide" : "Unicode");
        error = true;
    }
    if (!error && ordinary && characters.count > 1 && characters.too_long)
    {
        pp_diagnose_group(pp, PP_WARNING_PLAIN, location, "character constant too long for its type");
    }
    int64_t bits;
    switch (kind)
    {
        case C_TOKEN_CHAR_CONSTANT:
            bits = characters.count > 1 ? (pp->options.predefines.char_unsigned ? (int64_t)characters.packed : (int64_t)(int32_t)characters.packed)
                                       : pp->options.predefines.char_unsigned ? (int64_t)(uint8_t)characters.last : (int64_t)(int8_t)(uint8_t)characters.last;
            break;
        case C_TOKEN_UTF8_CHAR_CONSTANT:
            bits = (uint8_t)characters.last;
            break;
        case C_TOKEN_UTF16_CHAR_CONSTANT:
            bits = (uint16_t)characters.last;
            break;
        case C_TOKEN_UTF32_CHAR_CONSTANT:
            bits = characters.last;
            break;
        default:
            bits = pp->options.predefines.short_wchar ? (int64_t)(uint16_t)characters.last : (int64_t)(int32_t)characters.last;
            break;
    }
    // UTF-8, UTF-16 and UTF-32 character constants are unsigned in C (unsigned char, char16_t, char32_t), as in clang.
    const bool is_unsigned = kind == C_TOKEN_UTF8_CHAR_CONSTANT || kind == C_TOKEN_UTF16_CHAR_CONSTANT || kind == C_TOKEN_UTF32_CHAR_CONSTANT ||
                             (kind == C_TOKEN_CHAR_CONSTANT && pp->options.predefines.char_unsigned) ||
                             (kind == C_TOKEN_WIDE_CHAR_CONSTANT && pp->options.predefines.short_wchar);
    *value = (PPValue){ .bits = (uint64_t)bits, .is_unsigned = is_unsigned };
    return !error;
}

// Applies an operator to the values on top of the stack (two for a binary one, three for ?:, which is COLON here).
static bool pp_reduce(Preprocessor* pp, PPValue* values, uint32_t* value_count, uint32_t operator, uint32_t location, PPExpressionContext context)
{
    bool result = true;
    const bool unary = operator == PP_OPERATOR_PLUS || operator == PP_OPERATOR_MINUS || operator == PP_OPERATOR_NOT || operator == PP_OPERATOR_COMPLEMENT;
    const uint32_t operands = unary ? 1 : operator == PP_OPERATOR_COLON ? 3 : 2;
    if (*value_count < operands)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "missing operand in preprocessor expression");
        result = false;
    }
    else if (unary)
    {
        PPValue* v = &values[*value_count - 1];
        switch (operator)
        {
            case PP_OPERATOR_MINUS: v->bits = (uint64_t)0 - v->bits; break;
            case PP_OPERATOR_NOT: v->bits = v->bits == 0; v->is_unsigned = false; break;
            case PP_OPERATOR_COMPLEMENT: v->bits = ~v->bits; break;
        }
    }
    else if (operator == PP_OPERATOR_COLON)
    {
        const PPValue condition = values[*value_count - 3];
        const PPValue yes = values[*value_count - 2];
        const PPValue no = values[*value_count - 1];
        PPValue chosen = condition.bits != 0 ? yes : no;
        chosen.is_unsigned = yes.is_unsigned || no.is_unsigned;
        chosen.division_by_zero |= condition.division_by_zero;
        *value_count -= 2;
        values[*value_count - 1] = chosen;
    }
    else
    {
        const PPValue a = values[*value_count - 2];
        const PPValue b = values[*value_count - 1];
        const bool is_unsigned = a.is_unsigned || b.is_unsigned;
        PPValue r = { .is_unsigned = is_unsigned, .division_by_zero = a.division_by_zero || b.division_by_zero };
        const int64_t sa = (int64_t)a.bits;
        const int64_t sb = (int64_t)b.bits;
        // Shifts, logical operators and comma do not apply the usual arithmetic conversions. ?: keeps its
        // common unsigned type above, but clang does not warn for the conversion of its selected arm itself.
        const bool converts = operator != PP_OPERATOR_SHIFT_LEFT && operator != PP_OPERATOR_SHIFT_RIGHT &&
                              operator != PP_OPERATOR_LOGICAL_AND && operator != PP_OPERATOR_LOGICAL_OR && operator != PP_OPERATOR_COMMA;
        if (context.live && is_unsigned && converts && ((!a.is_unsigned && sa < 0) || (!b.is_unsigned && sb < 0)))
        {
            const uint32_t saved_origin = pp->diagnostic_origin;
            pp->diagnostic_origin = context.origin;
            // A promoted warning must still obey -w and system-header suppression before it becomes an error.
            const bool promoted = pp->options.negative_conversion_error && !pp->options.suppress_warnings &&
                                  pp_diagnostic_warning_visible(pp, context.location, context.origin);
            // Otherwise a warning of no group (#1970), which -Werror promotes.
            char flag[BUSTER_PP_WARNING_FLAG_BYTES] = { 0 };
            const PPSeverity severity = promoted ? PP_SEVERITY_ERROR : pp_warning_flag(pp, PP_WARNING_PLAIN, context.location, flag);
            if (severity != BUSTER_PP_IGNORED && !a.is_unsigned && sa < 0)
            {
                pp_diagnose(pp, severity, context.location, "left side of operator converted from negative value to unsigned: %ld to %lu%s", (long)sa, (unsigned long)a.bits, flag);
            }
            if (severity != BUSTER_PP_IGNORED && !b.is_unsigned && sb < 0)
            {
                pp_diagnose(pp, severity, context.location, "right side of operator converted from negative value to unsigned: %ld to %lu%s", (long)sb, (unsigned long)b.bits, flag);
            }
            pp->diagnostic_origin = saved_origin;
        }
        switch (operator)
        {
            case PP_OPERATOR_MULTIPLY: r.bits = a.bits * b.bits; break;
            case PP_OPERATOR_DIVIDE:
            case PP_OPERATOR_REMAINDER:
                if (b.bits == 0)
                {
                    r.division_by_zero = true;
                }
                else if (is_unsigned)
                {
                    r.bits = operator == PP_OPERATOR_DIVIDE ? a.bits / b.bits : a.bits % b.bits;
                }
                else if (sa == INT64_MIN && sb == -1)
                {
                    r.bits = operator == PP_OPERATOR_DIVIDE ? a.bits : 0;
                }
                else
                {
                    r.bits = (uint64_t)(operator == PP_OPERATOR_DIVIDE ? sa / sb : sa % sb);
                }
                break;
            case PP_OPERATOR_ADD: r.bits = a.bits + b.bits; break;
            case PP_OPERATOR_SUBTRACT: r.bits = a.bits - b.bits; break;
            case PP_OPERATOR_SHIFT_LEFT:
                r.is_unsigned = a.is_unsigned;
                r.bits = b.bits >= 64 ? 0 : a.bits << b.bits;
                break;
            case PP_OPERATOR_SHIFT_RIGHT:
            {
                // clang shifts right by the amount's low 32 bits, and by 63 when they are 64 or more
                // (EvaluateDirectiveSubExpr), where a left shift of 64 or more is 0.
                const uint32_t low = (uint32_t)b.bits;
                const uint32_t amount = low >= 64 ? 63 : low;
                r.is_unsigned = a.is_unsigned;
                r.bits = a.is_unsigned ? a.bits >> amount : (uint64_t)(sa >> amount);
            } break;
            case PP_OPERATOR_LESS: r.bits = is_unsigned ? a.bits < b.bits : sa < sb; r.is_unsigned = false; break;
            case PP_OPERATOR_LESS_EQUAL: r.bits = is_unsigned ? a.bits <= b.bits : sa <= sb; r.is_unsigned = false; break;
            case PP_OPERATOR_GREATER: r.bits = is_unsigned ? a.bits > b.bits : sa > sb; r.is_unsigned = false; break;
            case PP_OPERATOR_GREATER_EQUAL: r.bits = is_unsigned ? a.bits >= b.bits : sa >= sb; r.is_unsigned = false; break;
            case PP_OPERATOR_EQUAL: r.bits = a.bits == b.bits; r.is_unsigned = false; break;
            case PP_OPERATOR_NOT_EQUAL: r.bits = a.bits != b.bits; r.is_unsigned = false; break;
            case PP_OPERATOR_AND: r.bits = a.bits & b.bits; break;
            case PP_OPERATOR_XOR: r.bits = a.bits ^ b.bits; break;
            case PP_OPERATOR_OR: r.bits = a.bits | b.bits; break;
            case PP_OPERATOR_LOGICAL_AND:
                r.bits = a.bits != 0 && b.bits != 0;
                r.is_unsigned = false;
                r.division_by_zero = a.division_by_zero || (a.bits != 0 && b.division_by_zero);
                break;
            case PP_OPERATOR_LOGICAL_OR:
                r.bits = a.bits != 0 || b.bits != 0;
                r.is_unsigned = false;
                r.division_by_zero = a.division_by_zero || (a.bits == 0 && b.division_by_zero);
                break;
            case PP_OPERATOR_COMMA:
                r = b;
                r.division_by_zero |= a.division_by_zero;
                break;
            default:
                pp_diagnose(pp, PP_SEVERITY_ERROR, location, "'?' without ':' in preprocessor expression");
                result = false;
                break;
        }
        *value_count -= 1;
        values[*value_count - 1] = r;
    }
    return result;
}

// Evaluates a #if line's expanded tokens (C 6.10.1): a precedence parser with explicit operator and value stacks, where
// an identifier left after expansion is 0 (true is 1 from C23 on). Returns false for zero or an error.
static bool pp_evaluate_value(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location, PPValue* value)
{
    const uint32_t saved_origin = pp->diagnostic_origin;
    PPValue values[BUSTER_PP_EXPRESSION_DEPTH];
    uint8_t operators[BUSTER_PP_EXPRESSION_DEPTH];
    PPExpressionContext contexts[BUSTER_PP_EXPRESSION_DEPTH];
    uint32_t value_count = 0;
    uint32_t operator_count = 0;
    bool valid = true;
    bool expect_operand = true;
    bool live = true;
    for (uint32_t i = begin; valid && i < end; i += 1)
    {
        const uint8_t kind = tokens->kinds[i];
        const uint32_t token_location = tokens->locations[i];
        pp->diagnostic_origin = tokens->origins != 0 ? tokens->origins[i] : 0;
        valid = value_count < BUSTER_PP_EXPRESSION_DEPTH && operator_count < BUSTER_PP_EXPRESSION_DEPTH;
        if (!valid)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "preprocessor expression nests too deeply");
        }
        else if (expect_operand)
        {
            if (kind == C_TOKEN_PLUS || kind == C_TOKEN_MINUS || kind == C_TOKEN_EXCLAIM || kind == C_TOKEN_TILDE)
            {
                operators[operator_count] = kind == C_TOKEN_PLUS ? PP_OPERATOR_PLUS : kind == C_TOKEN_MINUS ? PP_OPERATOR_MINUS : kind == C_TOKEN_EXCLAIM ? PP_OPERATOR_NOT : PP_OPERATOR_COMPLEMENT;
                contexts[operator_count] = (PPExpressionContext){ .location = token_location, .origin = pp->diagnostic_origin, .live = live };
                operator_count += 1;
            }
            else if (kind == C_TOKEN_L_PAREN)
            {
                operators[operator_count] = PP_OPERATOR_PAREN;
                contexts[operator_count] = (PPExpressionContext){ .location = token_location, .origin = pp->diagnostic_origin, .live = live };
                operator_count += 1;
            }
            else if (kind == C_TOKEN_NUMERIC_CONSTANT || kind == BUSTER_PP_KIND_INTEGER)
            {
                uint8_t buffer[256];
                const uint8_t* spelling;
                const uint32_t length = tokens->values[i] - token_location;
                if (kind == BUSTER_PP_KIND_INTEGER)
                {
                    values[value_count] = (PPValue){ .bits = tokens->values[i] };
                }
                else if (length < sizeof(buffer))
                {
                    const uint32_t n = pp_spelling(pp, kind, tokens->values[i], token_location, buffer, &spelling);
                    valid = pp_parse_integer(pp, spelling, n, token_location, &values[value_count]);
                }
                else
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "integer literal is too large to be represented in any integer type");
                    valid = false;
                }
                value_count += 1;
                expect_operand = false;
            }
            else if (kind >= C_TOKEN_CHAR_CONSTANT && kind <= C_TOKEN_UTF32_CHAR_CONSTANT)
            {
                Scratch scratch = scratch_begin(0);
                uint8_t* buffer = arena_push(scratch.arena, tokens->values[i] - token_location + 1, 1);
                const uint8_t* spelling;
                const uint32_t n = buffer != 0 ? pp_spelling(pp, kind, tokens->values[i], token_location, buffer, &spelling) : 0;
                valid = buffer != 0 && pp_parse_character(pp, kind, spelling, n, token_location, &values[value_count]);
                scratch_end(scratch);
                value_count += 1;
                expect_operand = false;
            }
            else if (kind == C_TOKEN_RAW_IDENTIFIER)
            {
                // A remaining name followed by '(' is an undefined function-like invocation, even if expansion
                // painted a recursive macro name. Diagnose the name before the ordinary identifier-to-zero rule.
                if (i + 1 < end && tokens->kinds[i + 1] == C_TOKEN_L_PAREN)
                {
                    const IdentId name = tokens->values[i];
                    pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "function-like macro '%.*s' is not defined",
                                (int)intern_length(pp->table, name), intern_bytes(pp->table, name));
                    valid = false;
                }
                else
                {
                    // An identifier left after expansion is 0; in C23, true is 1 (and false 0).
                    const bool is_true = tokens->values[i] == C_NAME_TRUE && pp->options.language.standard >= 2023;
                    values[value_count] = (PPValue){ .bits = is_true };
                    value_count += 1;
                    expect_operand = false;
                }
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "invalid token at start of a preprocessor expression");
                valid = false;
            }
        }
        else if (kind == C_TOKEN_R_PAREN)
        {
            while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_PAREN)
            {
                operator_count -= 1;
                live = contexts[operator_count].live;
                valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location, contexts[operator_count]);
            }
            if (valid && operator_count == 0)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "unbalanced ')' in preprocessor expression");
                valid = false;
            }
            if (valid)
            {
                live = contexts[operator_count - 1].live;
                operator_count -= 1;
            }
        }
        else
        {
            const uint32_t operator = pp_binary_operator(kind);
            if (operator == BUSTER_PP_NONE)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "token is not a valid binary operator in a preprocessor subexpression");
                valid = false;
            }
            else
            {
                const uint8_t precedence = pp_operator_precedence[operator];
                // Left to right, but ?: from right to left: reduce what binds at least as tightly (more tightly for ?).
                while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_PAREN &&
                       (pp_operator_precedence[operators[operator_count - 1]] > precedence ||
                        (pp_operator_precedence[operators[operator_count - 1]] == precedence && operator != PP_OPERATOR_QUESTION && operator != PP_OPERATOR_COLON)))
                {
                    if (operators[operator_count - 1] == PP_OPERATOR_QUESTION)
                    {
                        break;
                    }
                    operator_count -= 1;
                    live = contexts[operator_count].live;
                    valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location, contexts[operator_count]);
                }
                // clang parses a #if line from ?:'s precedence on, so a comma is only an operator inside parentheses or
                // between a '?' and its ':' (`#if 1, 2` is an error, `#if (1, 2)` isn't). Comma binds loosest, so the
                // reductions above stop only at such a '(' or '?'.
                if (valid && operator == PP_OPERATOR_COMMA &&
                    (operator_count == 0 || (operators[operator_count - 1] != PP_OPERATOR_PAREN && operators[operator_count - 1] != PP_OPERATOR_QUESTION)))
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "expected end of line in preprocessor expression");
                    valid = false;
                }
                if (valid && operator == PP_OPERATOR_COLON)
                {
                    // The ':' completes the innermost '?': it becomes the ternary operator.
                    while (valid && operator_count != 0 && operators[operator_count - 1] != PP_OPERATOR_QUESTION && operators[operator_count - 1] != PP_OPERATOR_PAREN)
                    {
                        operator_count -= 1;
                        live = contexts[operator_count].live;
                        valid = pp_reduce(pp, values, &value_count, operators[operator_count], token_location, contexts[operator_count]);
                    }
                    if (valid && (operator_count == 0 || operators[operator_count - 1] != PP_OPERATOR_QUESTION))
                    {
                        pp_diagnose(pp, PP_SEVERITY_ERROR, token_location, "':' without '?' in preprocessor expression");
                        valid = false;
                    }
                    if (valid)
                    {
                        operators[operator_count - 1] = PP_OPERATOR_COLON;
                        live = contexts[operator_count - 1].live && value_count >= 2 && values[value_count - 2].bits == 0;
                    }
                }
                else if (valid)
                {
                    operators[operator_count] = (uint8_t)operator;
                    contexts[operator_count] = (PPExpressionContext){ .location = token_location, .origin = pp->diagnostic_origin, .live = live };
                    operator_count += 1;
                    if (operator == PP_OPERATOR_LOGICAL_AND || operator == PP_OPERATOR_QUESTION)
                    {
                        live = live && value_count != 0 && values[value_count - 1].bits != 0;
                    }
                    else if (operator == PP_OPERATOR_LOGICAL_OR)
                    {
                        live = live && value_count != 0 && values[value_count - 1].bits == 0;
                    }
                }
                expect_operand = true;
            }
        }
    }
    pp->diagnostic_origin = saved_origin;
    if (valid && expect_operand)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, value_count == 0 && operator_count == 0 ? "#if with no expression" : "expected value in expression");
        valid = false;
    }
    while (valid && operator_count != 0)
    {
        operator_count -= 1;
        if (operators[operator_count] == PP_OPERATOR_PAREN || operators[operator_count] == PP_OPERATOR_QUESTION)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, location, operators[operator_count] == PP_OPERATOR_PAREN ? "missing ')' in expression" : "'?' without ':' in preprocessor expression");
            valid = false;
        }
        else
        {
            valid = pp_reduce(pp, values, &value_count, operators[operator_count], location, contexts[operator_count]);
        }
    }
    if (valid && value_count != 1)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "malformed preprocessor expression");
        valid = false;
    }
    if (valid && values[0].division_by_zero)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "division by zero in preprocessor expression");
        valid = false;
    }
    *value = valid ? values[0] : (PPValue){ 0 };
    return valid;
}

// A #if line's value: whether it is valid and nonzero.
static bool pp_evaluate(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t location)
{
    PPValue value;
    return pp_evaluate_value(pp, tokens, begin, end, location, &value) && value.bits != 0;
}

static const char* const pp_embed_parameter_names[BUSTER_PP_EMBED_UNKNOWN] = { "limit", "prefix", "suffix", "if_empty", "clang::offset" };

// Whether token i of a line [.., end) is an identifier as clang's lexer gives one: a keyword of the language mode isn't.
static bool pp_embed_identifier(const Preprocessor* pp, const PPTokens* tokens, uint32_t i, uint32_t end)
{
    return i < end && tokens->kinds[i] == C_TOKEN_RAW_IDENTIFIER && !c_name_is_keyword(tokens->values[i], pp->options.language);
}

// limit's or clang::offset's parenthesized #if expression at *at of a line [.., end), as clang reads one
// (Preprocessor::LexEmbedParameters' LexParenthesizedIntegerExpr): it ends at a ')' that closes no '(' of its own, its
// value must be at least 0, and `defined` can't be in it (C23 6.10.3.2p2). *at moves past its ')'. What the line lacks is
// reported at its end, `end_location`.
static bool pp_embed_value(Preprocessor* pp, const PPTokens* tokens, uint32_t* at, uint32_t end, uint32_t end_location, uint64_t* value)
{
    const uint32_t open = *at;
    bool valid = open < end && tokens->kinds[open] == C_TOKEN_L_PAREN;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, open < end ? tokens->locations[open] : end_location, "expected '('");
    }
    else
    {
        uint32_t close = open + 1;
        uint32_t depth = 0;
        bool defined = false;
        while (close < end && (depth != 0 || tokens->kinds[close] != C_TOKEN_R_PAREN))
        {
            depth += tokens->kinds[close] == C_TOKEN_L_PAREN;
            depth -= tokens->kinds[close] == C_TOKEN_R_PAREN;
            defined |= tokens->kinds[close] == C_TOKEN_RAW_IDENTIFIER && tokens->values[close] == C_NAME_DEFINED;
            close += 1;
        }
        // Clang reads the expression's end at the ')', or at the line's end without one; then the token after the ')'.
        const uint32_t closing = close < end ? tokens->locations[close] : end_location;
        const uint32_t after = close + 1 < end ? tokens->locations[close + 1] : end_location;
        PPValue result = { 0 };
        if (close == open + 1)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, closing, "expected value in expression");
            valid = false;
        }
        else if (!defined)
        {
            valid = pp_evaluate_value(pp, tokens, open + 1, close, closing, &result);
        }
        if (valid && close == end)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, end_location, "expected ')'");
            valid = false;
        }
        if (valid && defined)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, after, "'defined' cannot appear within this context");
            valid = false;
        }
        if (valid && !result.is_unsigned && (int64_t)result.bits < 0)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, after, "invalid value '%lld'; must be positive", (long long)(int64_t)result.bits);
            valid = false;
        }
        *value = result.bits;
        *at = close + 1;
    }
    return valid;
}

// The parenthesized tokens of prefix, suffix, if_empty or an unknown parameter at *at of a line [.., end), as clang reads
// them (LexEmbedParameters' LexParenthesizedBalancedTokenSoup): [*begin, *finish), with *at past the ')'. A closing
// bracket must match the last one open, but the tokens end at a ')' unless a '(' was read since the last ')' (clang
// keeps a flag, not a depth: `prefix(((a)b)c)` ends after b), whatever brackets are still open.
static bool pp_embed_tokens(Preprocessor* pp, const PPTokens* tokens, uint32_t* at, uint32_t end, uint32_t end_location, uint32_t* begin, uint32_t* finish)
{
    const uint32_t open = *at;
    bool valid = open < end && tokens->kinds[open] == C_TOKEN_L_PAREN;
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, open < end ? tokens->locations[open] : end_location, "expected '('");
    }
    else
    {
        Scratch scratch = scratch_begin(0);
        // The brackets open, innermost last.
        uint8_t* brackets = arena_push(scratch.arena, end - open, 1);
        valid = brackets != 0;
        uint32_t depth = 0;
        bool waiting = false;
        uint32_t i = open + 1;
        while (valid && i < end && (waiting || tokens->kinds[i] != C_TOKEN_R_PAREN))
        {
            const uint8_t kind = tokens->kinds[i];
            const bool opens = kind == C_TOKEN_L_PAREN || kind == C_TOKEN_L_BRACE || kind == C_TOKEN_L_SQUARE;
            const bool closes = kind == C_TOKEN_R_PAREN || kind == C_TOKEN_R_BRACE || kind == C_TOKEN_R_SQUARE;
            waiting = kind == C_TOKEN_L_PAREN || (waiting && kind != C_TOKEN_R_PAREN);
            if (opens)
            {
                brackets[depth] = kind;
                depth += 1;
            }
            else if (closes)
            {
                const uint8_t last = depth != 0 ? brackets[depth - 1] : C_TOKEN_L_PAREN;
                const uint8_t expected = last == C_TOKEN_L_PAREN ? C_TOKEN_R_PAREN : last == C_TOKEN_L_BRACE ? C_TOKEN_R_BRACE : C_TOKEN_R_SQUARE;
                const char* spelled = expected == C_TOKEN_R_PAREN ? ")" : expected == C_TOKEN_R_BRACE ? "}" : "]";
                valid = depth != 0 && kind == expected;
                if (!valid)
                {
                    pp_diagnose(pp, PP_SEVERITY_ERROR, tokens->locations[i], "expected '%s'", spelled);
                }
                depth -= valid;
            }
            i += valid;
        }
        if (valid && i == end)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, end_location, "expected ')'");
            valid = false;
        }
        *begin = open + 1;
        *finish = i;
        *at = i + 1;
        scratch_end(scratch);
    }
    return valid;
}

// The parameters of an #embed, or with `query` of a __has_embed, in tokens [begin, end) of a stack, as clang reads them
// (Preprocessor::LexEmbedParameters): each a name (an identifier, or `identifier :: identifier`), which "__" around it
// doesn't change (C23 6.10p5; clang strips them from a scoped name's whole spelling, so __clang::offset__ is
// clang::offset), then its parenthesized tokens: an #if expression for limit and clang::offset (pp_embed_value), tokens
// for prefix, suffix and if_empty (pp_embed_tokens), and an unknown one's skipped, which only a query allows. One given
// twice is an error, and the later one counts. What the line lacks is reported at its end, `end_location`. Returns
// false on an error that drops the directive (or the query).
static bool pp_embed_parameters(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end, uint32_t end_location, bool query, PPEmbedParameters* parameters)
{
    *parameters = (PPEmbedParameters){ 0 };
    bool valid = true;
    uint32_t i = begin;
    Scratch scratch = scratch_begin(0);
    while (valid && i < end)
    {
        const uint32_t start = i;
        const bool scoped = i + 1 < end && tokens->kinds[i + 1] == C_TOKEN_COLONCOLON;
        const uint32_t bad = !pp_embed_identifier(pp, tokens, i, end) ? i : scoped && !pp_embed_identifier(pp, tokens, i + 2, end) ? i + 2 : BUSTER_PP_NONE;
        valid = bad == BUSTER_PP_NONE;
        if (!valid)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, bad < end ? tokens->locations[bad] : end_location, "expected identifier");
        }
        uint32_t parameter = BUSTER_PP_EMBED_UNKNOWN;
        const char* name = "";
        uint32_t length = 0;
        if (valid)
        {
            const uint32_t first_length = intern_length(pp->table, tokens->values[i]);
            const uint32_t second_length = scoped ? intern_length(pp->table, tokens->values[i + 2]) : 0;
            const uint32_t total = first_length + (scoped ? 2 + second_length : 0);
            char* spelled = arena_push(scratch.arena, (uint64_t)total + 1, 1);
            valid = spelled != 0;
            if (valid)
            {
                memcpy(spelled, intern_bytes(pp->table, tokens->values[i]), first_length);
                if (scoped)
                {
                    memcpy(spelled + first_length, "::", 2);
                    memcpy(spelled + first_length + 2, intern_bytes(pp->table, tokens->values[i + 2]), second_length);
                }
                const bool underscored = total > 4 && memcmp(spelled, "__", 2) == 0 && memcmp(spelled + total - 2, "__", 2) == 0;
                name = underscored ? spelled + 2 : spelled;
                length = underscored ? total - 4 : total;
            }
            for (uint32_t k = 0; valid && k < BUSTER_PP_EMBED_UNKNOWN; k += 1)
            {
                parameter = strlen(pp_embed_parameter_names[k]) == length && memcmp(name, pp_embed_parameter_names[k], length) == 0 ? k : parameter;
            }
            i += scoped ? 3 : 1;
        }
        if (valid && parameter != BUSTER_PP_EMBED_UNKNOWN && ((parameters->given >> parameter) & 1))
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, i < end ? tokens->locations[i] : end_location, "cannot specify parameter '%s' twice in the same '#embed' directive",
                        pp_embed_parameter_names[parameter]);
        }
        if (valid && (parameter == BUSTER_PP_EMBED_LIMIT || parameter == BUSTER_PP_EMBED_OFFSET))
        {
            uint64_t value = 0;
            valid = pp_embed_value(pp, tokens, &i, end, end_location, &value);
            parameters->limit = parameter == BUSTER_PP_EMBED_LIMIT ? value : parameters->limit;
            parameters->offset = parameter == BUSTER_PP_EMBED_OFFSET ? value : parameters->offset;
            parameters->given |= 1u << parameter;
        }
        else if (valid && (parameter != BUSTER_PP_EMBED_UNKNOWN || (i < end && tokens->kinds[i] == C_TOKEN_L_PAREN)))
        {
            uint32_t first;
            uint32_t last;
            valid = pp_embed_tokens(pp, tokens, &i, end, end_location, &first, &last);
            if (parameter != BUSTER_PP_EMBED_UNKNOWN)
            {
                parameters->begins[parameter] = first;
                parameters->ends[parameter] = last;
                parameters->given |= 1u << parameter;
            }
        }
        if (valid && parameter == BUSTER_PP_EMBED_UNKNOWN)
        {
            parameters->unknown += 1;
            if (!query)
            {
                pp_diagnose(pp, PP_SEVERITY_ERROR, tokens->locations[start], "unknown embed preprocessor parameter '%.*s'", (int)length, name);
                valid = false;
            }
        }
    }
    scratch_end(scratch);
    return valid;
}

// The file an #embed or a __has_embed in the file of `context` names, as clang's Preprocessor::LookupEmbedFile finds it:
// an absolute name as it is; a quoted one in that file's directory, then in the working directory, then in the
// --embed-dir directories (each joined to the name with a '/' unless it is empty, the working directory then); an angled
// one in those alone. Any file but a directory is found. Returns whether one is, with its path in `path` (PATH_MAX bytes)
// and `*path_length`, and with `*regular` set for a regular file, whose size goes to `*size`: another kind of file is a
// device, which #embed refuses and __has_embed finds empty. The file is only looked at, never read: #embed reads it
// (pp_lookup_path), and __has_embed answers from its size, as clang's EvaluateHasEmbed does (LookupEmbedFile without
// opening it), so that a file too big or unreadable for #embed is there for it.
// Optional timing covers only this search and its stat attempts, not parameter expansion or the later resource read.
// It is within the main-loop remainder, separate from include lookup, and is not an additive PPPhase.
static bool pp_find_embed(Preprocessor* pp, const PPContext* context, const uint8_t* name, uint32_t length, bool angled, char* path, uint32_t* path_length,
                          uint64_t* size, bool* regular)
{
    const uint64_t start = pp->options.timing ? __rdtsc() : 0;
    const PPFile* from = context != 0 ? &pp->files[context->file] : 0;
    const bool absolute = length != 0 && name[0] == '/';
    const uint32_t count = absolute ? 1 : 2 + pp->options.embed_directory_count;
    bool found = false;
    *path_length = 0;
    *size = 0;
    *regular = false;
    for (uint32_t c = absolute || !angled ? 0 : 2; !pp->options.no_external_files && !found && c < count; c += 1)
    {
        int written;
        if (absolute)
        {
            written = snprintf(path, PATH_MAX, "%.*s", (int)length, (const char*)name);
        }
        else if (c == 0 && from != 0 && from->directory_length != BUSTER_PP_NONE)
        {
            written = snprintf(path, PATH_MAX, "%.*s/%.*s", (int)from->directory_length, from->path, (int)length, (const char*)name);
        }
        else if (c < 2)
        {
            written = snprintf(path, PATH_MAX, "./%.*s", (int)length, (const char*)name);
        }
        else
        {
            const char* directory = pp->options.embed_directories[c - 2];
            written = snprintf(path, PATH_MAX, "%s%s%.*s", directory, directory[0] != 0 ? "/" : "", (int)length, (const char*)name);
        }
        struct stat stats;
        const bool fits = written > 0 && written < PATH_MAX;
        if (pp->options.timing)
        {
            pp->statistics.embed_stats += fits;
        }
        found = fits && stat(path, &stats) == 0 && !S_ISDIR(stats.st_mode);
        *path_length = found ? (uint32_t)written : 0;
        *regular = found && S_ISREG(stats.st_mode);
        *size = *regular ? (uint64_t)stats.st_size : 0;
    }
    if (pp->options.timing)
    {
        pp->statistics.embed_ticks += __rdtsc() - start;
        pp->statistics.embed_searches += 1;
        pp->statistics.embed_found += found;
    }
    return found;
}

// __has_embed's answer (C23 6.10.1), as clang gives it (Preprocessor::EvaluateHasEmbed), from its operand's tokens
// [first, last): a header name (a string literal, a '<' that spans one a #if line writes, or '<' to the first '>'), then
// embed parameters (pp_embed_parameters). 0 (not found) when a parameter is one clang doesn't know or no file has the
// name (pp_find_embed, which only looks at the file: its size is the answer's); 2 (empty) when the file has no bytes
// left after clang::offset and limit; else 1. Clang takes a limit past the bytes left as none left, not as all of them,
// and a device's size as 0.
static uint64_t pp_has_embed(Preprocessor* pp, const PPTokens* tokens, uint32_t first, uint32_t last, uint32_t location)
{
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    uint32_t i = first;
    if (i < last && (tokens->kinds[i] == C_TOKEN_STRING_LITERAL || pp_written_angled(pp, tokens, i)))
    {
        i += 1;
    }
    else if (i < last && tokens->kinds[i] == C_TOKEN_LESS)
    {
        while (i < last && tokens->kinds[i] != C_TOKEN_GREATER)
        {
            i += 1;
        }
        i += i < last;
    }
    bool valid = i > first && pp_header_name(pp, tokens, first, i, name, sizeof(name) - 1, &length, &angled);
    if (!valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, location, "expected \"FILENAME\" or <FILENAME>");
    }
    PPEmbedParameters parameters;
    valid = valid && pp_embed_parameters(pp, tokens, i, last, location, true, &parameters);
    uint64_t result = 0;
    if (valid && parameters.unknown == 0)
    {
        char path[PATH_MAX];
        uint32_t path_length;
        uint64_t size;
        bool regular;
        if (pp_find_embed(pp, pp_current_file_context(pp), name, length, angled, path, &path_length, &size, &regular))
        {
            size = (parameters.given >> BUSTER_PP_EMBED_OFFSET) & 1 ? (parameters.offset > size ? 0 : size - parameters.offset) : size;
            size = (parameters.given >> BUSTER_PP_EMBED_LIMIT) & 1 ? (parameters.limit > size ? 0 : parameters.limit) : size;
            result = size == 0 ? 2 : 1;
        }
    }
    return result;
}

// Whether the token after an #embed's line in a file (the context's next) is on the next line, with no newline outside
// comments between it and the line's end, and isn't a directive's '#': after an #embed's tokens clang's lexer starts no
// line there, since the end of the tokens it entered passes no line start on (Preprocessor::HandleEndOfFile's
// PropagateLineStartLeadingSpaceInfo), so its printer prints the token on the line of the embed's output.
static bool pp_embed_joins(const Preprocessor* pp, const PPFileData* data, const PPContext* context, uint32_t line_end)
{
    // The token after the line: the context's next one, past the line as clang lexes it when it was lexed again
    // (pp_resume_after), from the line's end: the end of the token before it, or where the line lexed again ends
    // (`line_end`; 0 when it wasn't), since the token before may be the file's, inside the header name.
    const uint32_t end = context->position;
    const bool directive = context->next_directive < data->directive_count && data->directive_hash[context->next_directive] == end;
    const uint32_t stop = end < data->token_count && !directive ? data->starts[end] : 0;
    const ScalarLexer lexer = pp_file_lexer(pp->refine_flags, data);
    const uint64_t last = pp_file_token_end(pp, data, end - 1);
    uint64_t p = line_end > last ? line_end : last;
    uint32_t newlines = 0;
    // The newlines outside comments, at each of which pp_skip_blanks stops (a CR LF is one).
    while (p < stop)
    {
        p = pp_skip_blanks(&lexer, p);
        uint64_t size = 0;
        uint64_t at;
        const int32_t c = p < stop ? get_char(&lexer, p, &size, &at) : 0;
        uint64_t next;
        newlines += c == '\n' || (c == '\r' && peek_char(&lexer, p + size, &next) != '\n');
        p += size;
    }
    return stop != 0 && newlines == 1;
}

// Pushes tokens [begin, end) of a stack onto pp->expansions as an #embed's prefix, suffix or if_empty: with their spaces,
// starting no line, and never expanded (BUSTER_PP_NO_EXPAND): clang enters them with macro expansion off, having read
// them expanded.
static bool pp_embed_push(Preprocessor* pp, const PPTokens* tokens, uint32_t begin, uint32_t end)
{
    bool result = true;
    for (uint32_t i = begin; result && i < end; i += 1)
    {
        PPToken token = pp_stack_token(tokens, i);
        token.flags = (uint8_t)((token.flags & BUSTER_PP_SPACE) | BUSTER_PP_NO_EXPAND);
        result = pp_push_token(pp, &pp->expansions, token);
    }
    return result;
}

// An #embed's line read, [begin, end) of a stack: its header name, unless one was written (`literal`: `name`, `length`
// and `angled`), then its parameters. The file is found (pp_find_embed) and its bytes past clang::offset and up to limit
// go in as one BUSTER_PP_KIND_EMBED token between prefix's tokens and suffix's, or if_empty's tokens go in when there are
// none (C23 6.10.3), in a buffer the main loop reads next (pp_embed_push), as clang enters them
// (HandleEmbedDirectiveImpl). Then the file's next token may join their line (pp_embed_joins), which ends at
// `relexed_end` when it was lexed again from the header name (pp_copy_relexed; else 0).
static void pp_embed_line(Preprocessor* pp, PPContext* context, uint32_t d, const PPTokens* tokens, uint32_t begin, uint32_t end, bool literal, uint8_t* name,
                          uint32_t length, bool angled, uint32_t relexed_end)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t hash = data->directive_hash[d];
    const uint32_t line_end = pp_line_end_location(pp, data, base, data->directive_end[d] - 1);
    // A computed name: a string literal, or '<' to the first '>', of the line's first tokens.
    uint32_t i = begin;
    uint32_t name_location = literal ? base + data->starts[hash + 2] : begin < end ? tokens->locations[begin] : line_end;
    bool valid = literal;
    if (!literal)
    {
        bool unclosed;
        i = pp_computed_name_end(tokens, begin, end, &unclosed);
        valid = !unclosed && i > begin && pp_header_name(pp, tokens, begin, i, name, PATH_MAX, &length, &angled);
        if (unclosed)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, line_end, "expected '>'");
        }
        else if (!valid)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "expected \"FILENAME\" or <FILENAME>");
        }
    }
    if (valid && length == 0)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "empty filename");
        valid = false;
    }
    PPEmbedParameters parameters;
    if (valid && pp_embed_parameters(pp, tokens, i, end, line_end, false, &parameters))
    {
        char path[PATH_MAX];
        uint32_t path_length;
        uint64_t size;
        bool regular;
        const bool found = pp_find_embed(pp, context, name, length, angled, path, &path_length, &size, &regular);
        const uint32_t file = regular ? pp_lookup_path(pp, path, path_length, BUSTER_PP_NONE, 0, BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
        const uint32_t source = file != BUSTER_PP_NONE ? pp->files[file].data : BUSTER_PP_NONE;
        if (found && !regular)
        {
            pp_diagnose(pp, PP_SEVERITY_ERROR, name_location, "device files are not yet supported by '#embed' directive");
        }
        else if (source == BUSTER_PP_NONE)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, name_location, "'%.*s' file not found", (int)length, (const char*)name);
        }
        else
        {
            pp_dependency_observe(pp, file, context->range);
            // clang::offset first, then limit.
            const uint32_t size = pp->datas[source].length;
            const uint32_t offset = parameters.offset < size ? (uint32_t)parameters.offset : size;
            const uint32_t left = size - offset;
            const uint32_t taken = (parameters.given >> BUSTER_PP_EMBED_LIMIT) & 1 && parameters.limit < left ? (uint32_t)parameters.limit : left;
            const uint32_t start = pp->expansions.count;
            const uint32_t location = base + data->starts[hash];
            bool fits = true;
            if (taken == 0)
            {
                fits = !((parameters.given >> BUSTER_PP_EMBED_IF_EMPTY) & 1) ||
                       pp_embed_push(pp, tokens, parameters.begins[BUSTER_PP_EMBED_IF_EMPTY], parameters.ends[BUSTER_PP_EMBED_IF_EMPTY]);
            }
            else if (pp->embed_count < (BUSTER_PP_MAX_EMBEDS >> pp->capacity_shift))
            {
                pp->embeds[pp->embed_count] = (PPEmbed){ .data = source, .offset = offset, .length = taken };
                fits = (!((parameters.given >> BUSTER_PP_EMBED_PREFIX) & 1) ||
                        pp_embed_push(pp, tokens, parameters.begins[BUSTER_PP_EMBED_PREFIX], parameters.ends[BUSTER_PP_EMBED_PREFIX])) &&
                       pp_push_token(pp, &pp->expansions, (PPToken){ .kind = BUSTER_PP_KIND_EMBED, .value = pp->embed_count, .location = location }) &&
                       (!((parameters.given >> BUSTER_PP_EMBED_SUFFIX) & 1) ||
                        pp_embed_push(pp, tokens, parameters.begins[BUSTER_PP_EMBED_SUFFIX], parameters.ends[BUSTER_PP_EMBED_SUFFIX]));
                pp->embed_count += 1;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, location, "too many #embed directives");
                fits = false;
            }
            if (fits && pp->expansions.count != start)
            {
                // The first tokens after a change of file or of presumed position: clang's printer started a line for the
                // change, and prints them there, where the change left it (as tokens a macro made at that place).
                if (pp->file_change_token == pp->output.count && pp->capture_count == 0 && !pp_collecting(pp))
                {
                    pp->expansions.flags[start] |= BUSTER_PP_START_OF_LINE | BUSTER_PP_FROM_MACRO;
                    if (pp->options.expansion_starts)
                    {
                        pp_note_expansion(pp, pp->file_change_location);
                    }
                }
                const bool joins = !pp_collecting(pp) && pp_embed_joins(pp, data, context, relexed_end);
                if (pp_push_buffer(pp, &pp->expansions, start, pp->expansions.count, 0, 0, false, true) != 0 && joins)
                {
                    context->pending_flags |= BUSTER_PP_JOIN_LINE;
                }
            }
        }
    }
}

// The barrier over an #embed's line ended: the capture is the line expanded (after the header name, if one was written).
static void pp_finish_embed(Preprocessor* pp)
{
    const PPCapture capture = pp->capture_frames[pp->capture_count - 1];
    PPContext* context = &pp->contexts[capture.context];
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t hash = data->directive_hash[capture.directive];
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    uint32_t after;
    bool literal;
    uint32_t relex_begin;
    uint32_t relex_end;
    pp_read_header_name(pp, data, hash + 2, data->directive_end[capture.directive], name, sizeof(name), &length, &angled, &literal, &after, &relex_begin,
                        &relex_end);
    pp->capture_count -= 1;
    pp_embed_line(pp, context, capture.directive, &pp->captures, capture.begin, pp->captures.count, literal, name, length, angled,
                  relex_begin != relex_end ? relex_end : 0);
    pp->captures.count = capture.begin;
    pp->arguments.count = capture.arguments_begin;
}

// #embed at directive d (C23 6.10.3): a header name, written or computed as an #include's (pp_read_header_name), then
// parameters, which clang reads expanded: when a macro's name is among them, or the name is computed, through a capture
// that pp_finish_embed takes (but among a builtin's operand, where the main loop collects tokens unexpanded, as they
// are). After a written name, the parameters are the line lexed again from it when the file's tokens there aren't
// clang's (pp_copy_relexed): `<a//b.bin> limit(1)`'s "//" began a comment in them. Before C23 it is an extension, which
// clang warns about.
static void pp_embed(Preprocessor* pp, PPContext* context, uint32_t d)
{
    const PPFileData* data = &pp->datas[context->data];
    const uint32_t hash = data->directive_hash[d];
    const uint32_t end = data->directive_end[d];
    const uint32_t base = pp->ranges[context->range].base;
    const uint32_t location = base + data->starts[hash];
    if (pp->options.language.standard < 2023)
    {
        pp_diagnose_group(pp, PP_WARNING_C23_EXTENSIONS, base + data->starts[hash + 1], "#embed is a C23 extension");
    }
    uint8_t name[PATH_MAX];
    uint32_t length = 0;
    bool angled = false;
    bool literal = false;
    uint32_t after = end;
    uint32_t relex_begin = 0;
    uint32_t relex_end = 0;
    const bool valid = pp_read_header_name(pp, data, hash + 2, end, name, sizeof(name), &length, &angled, &literal, &after, &relex_begin, &relex_end);
    const uint32_t line = pp->arguments.count;
    if (literal && !valid)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, base + data->starts[hash + 2], "expected \"FILENAME\" or <FILENAME>");
    }
    else if (relex_begin != relex_end ? pp_copy_relexed(pp, context, relex_begin, relex_end, &pp->arguments) : pp_copy_line(pp, context, literal ? after : hash + 2, end))
    {
        pp_resume_after(context, data, relex_end);
        // The poisoned names of the line past an angled name, which pp_directive leaves to this: the line lexed again
        // (where the file's tokens may have none of them, in a comment, or others), or else the file's tokens past it.
        if (relex_begin != relex_end)
        {
            pp_check_poison_range(pp, data, base, &pp->arguments, line, pp->arguments.count, BUSTER_PP_NONE, true);
        }
        else if (angled)
        {
            pp_check_poison_range(pp, data, base, 0, after, end, BUSTER_PP_NONE, true);
        }
        bool expands = !literal;
        for (uint32_t i = line; i < pp->arguments.count; i += 1)
        {
            expands |= pp->arguments.kinds[i] == C_TOKEN_RAW_IDENTIFIER && pp_is_macro(pp, pp->arguments.values[i]);
        }
        const PPCapture capture = {
            .kind = BUSTER_PP_CAPTURE_EMBED,
            .arguments_begin = line,
            .directive = d,
            .context = (uint32_t)(context - pp->contexts),
            .directive_kind = PP_DIRECTIVE_EMBED,
            .location = location,
        };
        if (!(expands && line < pp->arguments.count && !pp_collecting(pp) && pp_start_capture(pp, capture, line, pp->arguments.count, location)))
        {
            pp_embed_line(pp, context, d, &pp->arguments, line, pp->arguments.count, literal, name, length, angled, relex_begin != relex_end ? relex_end : 0);
            pp->arguments.count = line;
        }
    }
}

// clang 23's predefined macros for x86_64-pc-linux-gnu in gnu17 (`clang -dM -E -x c /dev/null`), without the ones the
// language mode decides (pp_predefines adds those) and the ones the target and the other options decide
// (c_predefines.c's predefines_text).
static const char pp_predefines_base[] =
    // Clang's first two definitions also anchor expansion notes at their real built-in source lines.
    "#define __llvm__ 1\n"
    "#define __clang__ 1\n"
    "#define _LP64 1\n"
    "#define __ATOMIC_ACQUIRE 2\n"
    "#define __ATOMIC_ACQ_REL 4\n"
    "#define __ATOMIC_CONSUME 1\n"
    "#define __ATOMIC_RELAXED 0\n"
    "#define __ATOMIC_RELEASE 3\n"
    "#define __ATOMIC_SEQ_CST 5\n"
    "#define __BIGGEST_ALIGNMENT__ 16\n"
    "#define __BITINT_MAXWIDTH__ 8388608\n"
    "#define __BOOL_WIDTH__ 1\n"
    "#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__\n"
    "#define __CHAR16_TYPE__ unsigned short\n"
    "#define __CHAR32_TYPE__ unsigned int\n"
    "#define __CHAR_BIT__ 8\n"
    "#define __CLANG_ATOMIC_BOOL_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR16_T_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR32_T_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_CHAR_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_INT_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_LLONG_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_LONG_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_POINTER_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_SHORT_LOCK_FREE 2\n"
    "#define __CLANG_ATOMIC_WCHAR_T_LOCK_FREE 2\n"
    "#define __DBL_DECIMAL_DIG__ 17\n"
    "#define __DBL_DENORM_MIN__ 4.9406564584124654e-324\n"
    "#define __DBL_DIG__ 15\n"
    "#define __DBL_EPSILON__ 2.2204460492503131e-16\n"
    "#define __DBL_HAS_DENORM__ 1\n"
    "#define __DBL_HAS_INFINITY__ 1\n"
    "#define __DBL_HAS_QUIET_NAN__ 1\n"
    "#define __DBL_MANT_DIG__ 53\n"
    "#define __DBL_MAX_10_EXP__ 308\n"
    "#define __DBL_MAX_EXP__ 1024\n"
    "#define __DBL_MAX__ 1.7976931348623157e+308\n"
    "#define __DBL_MIN_10_EXP__ (-307)\n"
    "#define __DBL_MIN_EXP__ (-1021)\n"
    "#define __DBL_MIN__ 2.2250738585072014e-308\n"
    "#define __DBL_NORM_MAX__ 1.7976931348623157e+308\n"
    "#define __DECIMAL_DIG__ __LDBL_DECIMAL_DIG__\n"
    "#define __ELF__ 1\n"
    "#define __FLOAT128__ 1\n"
    "#define __FLT_DECIMAL_DIG__ 9\n"
    "#define __FLT_DENORM_MIN__ 1.40129846e-45F\n"
    "#define __FLT_DIG__ 6\n"
    "#define __FLT_EPSILON__ 1.19209290e-7F\n"
    "#define __FLT_HAS_DENORM__ 1\n"
    "#define __FLT_HAS_INFINITY__ 1\n"
    "#define __FLT_HAS_QUIET_NAN__ 1\n"
    "#define __FLT_MANT_DIG__ 24\n"
    "#define __FLT_MAX_10_EXP__ 38\n"
    "#define __FLT_MAX_EXP__ 128\n"
    "#define __FLT_MAX__ 3.40282347e+38F\n"
    "#define __FLT_MIN_10_EXP__ (-37)\n"
    "#define __FLT_MIN_EXP__ (-125)\n"
    "#define __FLT_MIN__ 1.17549435e-38F\n"
    "#define __FLT_NORM_MAX__ 3.40282347e+38F\n"
    "#define __FLT_RADIX__ 2\n"
    "#define __FPCLASS_NEGINF 0x0004\n"
    "#define __FPCLASS_NEGNORMAL 0x0008\n"
    "#define __FPCLASS_NEGSUBNORMAL 0x0010\n"
    "#define __FPCLASS_NEGZERO 0x0020\n"
    "#define __FPCLASS_POSINF 0x0200\n"
    "#define __FPCLASS_POSNORMAL 0x0100\n"
    "#define __FPCLASS_POSSUBNORMAL 0x0080\n"
    "#define __FPCLASS_POSZERO 0x0040\n"
    "#define __FPCLASS_QNAN 0x0002\n"
    "#define __FPCLASS_SNAN 0x0001\n"
    "#define __GCC_ASM_FLAG_OUTPUTS__ 1\n"
    "#define __GCC_CONSTRUCTIVE_SIZE 64\n"
    "#define __GCC_DESTRUCTIVE_SIZE 64\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_1 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_2 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 1\n"
    "#define __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1\n"
    "#define __INT16_C(c) c\n"
    "#define __INT16_C_SUFFIX__\n"
    "#define __INT16_FMTd__ \"hd\"\n"
    "#define __INT16_FMTi__ \"hi\"\n"
    "#define __INT16_MAX__ 32767\n"
    "#define __INT16_TYPE__ short\n"
    "#define __INT32_C(c) c\n"
    "#define __INT32_C_SUFFIX__\n"
    "#define __INT32_FMTd__ \"d\"\n"
    "#define __INT32_FMTi__ \"i\"\n"
    "#define __INT32_MAX__ 2147483647\n"
    "#define __INT32_TYPE__ int\n"
    "#define __INT64_C(c) c##L\n"
    "#define __INT64_C_SUFFIX__ L\n"
    "#define __INT64_FMTd__ \"ld\"\n"
    "#define __INT64_FMTi__ \"li\"\n"
    "#define __INT64_MAX__ 9223372036854775807L\n"
    "#define __INT64_TYPE__ long int\n"
    "#define __INT8_C(c) c\n"
    "#define __INT8_C_SUFFIX__\n"
    "#define __INT8_FMTd__ \"hhd\"\n"
    "#define __INT8_FMTi__ \"hhi\"\n"
    "#define __INT8_MAX__ 127\n"
    "#define __INT8_TYPE__ signed char\n"
    "#define __INTMAX_C(c) c##L\n"
    "#define __INTMAX_C_SUFFIX__ L\n"
    "#define __INTMAX_FMTd__ \"ld\"\n"
    "#define __INTMAX_FMTi__ \"li\"\n"
    "#define __INTMAX_MAX__ 9223372036854775807L\n"
    "#define __INTMAX_TYPE__ long int\n"
    "#define __INTMAX_WIDTH__ 64\n"
    "#define __INTPTR_FMTd__ \"ld\"\n"
    "#define __INTPTR_FMTi__ \"li\"\n"
    "#define __INTPTR_MAX__ 9223372036854775807L\n"
    "#define __INTPTR_TYPE__ long int\n"
    "#define __INTPTR_WIDTH__ 64\n"
    "#define __INT_FAST16_FMTd__ \"hd\"\n"
    "#define __INT_FAST16_FMTi__ \"hi\"\n"
    "#define __INT_FAST16_MAX__ 32767\n"
    "#define __INT_FAST16_TYPE__ short\n"
    "#define __INT_FAST16_WIDTH__ 16\n"
    "#define __INT_FAST32_FMTd__ \"d\"\n"
    "#define __INT_FAST32_FMTi__ \"i\"\n"
    "#define __INT_FAST32_MAX__ 2147483647\n"
    "#define __INT_FAST32_TYPE__ int\n"
    "#define __INT_FAST32_WIDTH__ 32\n"
    "#define __INT_FAST64_FMTd__ \"ld\"\n"
    "#define __INT_FAST64_FMTi__ \"li\"\n"
    "#define __INT_FAST64_MAX__ 9223372036854775807L\n"
    "#define __INT_FAST64_TYPE__ long int\n"
    "#define __INT_FAST64_WIDTH__ 64\n"
    "#define __INT_FAST8_FMTd__ \"hhd\"\n"
    "#define __INT_FAST8_FMTi__ \"hhi\"\n"
    "#define __INT_FAST8_MAX__ 127\n"
    "#define __INT_FAST8_TYPE__ signed char\n"
    "#define __INT_FAST8_WIDTH__ 8\n"
    "#define __INT_LEAST16_FMTd__ \"hd\"\n"
    "#define __INT_LEAST16_FMTi__ \"hi\"\n"
    "#define __INT_LEAST16_MAX__ 32767\n"
    "#define __INT_LEAST16_TYPE__ short\n"
    "#define __INT_LEAST16_WIDTH__ 16\n"
    "#define __INT_LEAST32_FMTd__ \"d\"\n"
    "#define __INT_LEAST32_FMTi__ \"i\"\n"
    "#define __INT_LEAST32_MAX__ 2147483647\n"
    "#define __INT_LEAST32_TYPE__ int\n"
    "#define __INT_LEAST32_WIDTH__ 32\n"
    "#define __INT_LEAST64_FMTd__ \"ld\"\n"
    "#define __INT_LEAST64_FMTi__ \"li\"\n"
    "#define __INT_LEAST64_MAX__ 9223372036854775807L\n"
    "#define __INT_LEAST64_TYPE__ long int\n"
    "#define __INT_LEAST64_WIDTH__ 64\n"
    "#define __INT_LEAST8_FMTd__ \"hhd\"\n"
    "#define __INT_LEAST8_FMTi__ \"hhi\"\n"
    "#define __INT_LEAST8_MAX__ 127\n"
    "#define __INT_LEAST8_TYPE__ signed char\n"
    "#define __INT_LEAST8_WIDTH__ 8\n"
    "#define __INT_MAX__ 2147483647\n"
    "#define __INT_WIDTH__ 32\n"
    "#define __LITTLE_ENDIAN__ 1\n"
    "#define __LLONG_WIDTH__ 64\n"
    "#define __LONG_LONG_MAX__ 9223372036854775807LL\n"
    "#define __LONG_MAX__ 9223372036854775807L\n"
    "#define __LONG_WIDTH__ 64\n"
    "#define __LP64__ 1\n"
    "#define __MEMORY_SCOPE_CLUSTR 5\n"
    "#define __MEMORY_SCOPE_DEVICE 1\n"
    "#define __MEMORY_SCOPE_SINGLE 4\n"
    "#define __MEMORY_SCOPE_SYSTEM 0\n"
    "#define __MEMORY_SCOPE_WRKGRP 2\n"
    "#define __MEMORY_SCOPE_WVFRNT 3\n"
    "#define __NO_MATH_INLINES 1\n"
    "#define __OBJC_BOOL_IS_BOOL 0\n"
    "#define __OPENCL_MEMORY_SCOPE_ALL_SVM_DEVICES 3\n"
    "#define __OPENCL_MEMORY_SCOPE_DEVICE 2\n"
    "#define __OPENCL_MEMORY_SCOPE_SUB_GROUP 4\n"
    "#define __OPENCL_MEMORY_SCOPE_WORK_GROUP 1\n"
    "#define __OPENCL_MEMORY_SCOPE_WORK_ITEM 0\n"
    "#define __ORDER_BIG_ENDIAN__ 4321\n"
    "#define __ORDER_LITTLE_ENDIAN__ 1234\n"
    "#define __ORDER_PDP_ENDIAN__ 3412\n"
    "#define __POINTER_WIDTH__ 64\n"
    "#define __PRAGMA_REDEFINE_EXTNAME 1\n"
    "#define __PTRDIFF_FMTd__ \"ld\"\n"
    "#define __PTRDIFF_FMTi__ \"li\"\n"
    "#define __PTRDIFF_MAX__ 9223372036854775807L\n"
    "#define __PTRDIFF_TYPE__ long int\n"
    "#define __PTRDIFF_WIDTH__ 64\n"
    "#define __REGISTER_PREFIX__\n"
    "#define __SCHAR_MAX__ 127\n"
    "#define __SEG_FS 1\n"
    "#define __SEG_GS 1\n"
    "#define __SHRT_MAX__ 32767\n"
    "#define __SHRT_WIDTH__ 16\n"
    "#define __SIG_ATOMIC_MAX__ 2147483647\n"
    "#define __SIG_ATOMIC_MIN__ (-__SIG_ATOMIC_MAX__ - 1)\n"
    "#define __SIG_ATOMIC_TYPE__ int\n"
    "#define __SIG_ATOMIC_WIDTH__ 32\n"
    "#define __SIZEOF_DOUBLE__ 8\n"
    "#define __SIZEOF_FLOAT128__ 16\n"
    "#define __SIZEOF_FLOAT__ 4\n"
    "#define __SIZEOF_INT128__ 16\n"
    "#define __SIZEOF_INT__ 4\n"
    "#define __SIZEOF_LONG_LONG__ 8\n"
    "#define __SIZEOF_LONG__ 8\n"
    "#define __SIZEOF_POINTER__ 8\n"
    "#define __SIZEOF_PTRDIFF_T__ 8\n"
    "#define __SIZEOF_SHORT__ 2\n"
    "#define __SIZEOF_SIZE_T__ 8\n"
    "#define __SIZEOF_WINT_T__ 4\n"
    "#define __SIZE_FMTX__ \"lX\"\n"
    "#define __SIZE_FMTo__ \"lo\"\n"
    "#define __SIZE_FMTu__ \"lu\"\n"
    "#define __SIZE_FMTx__ \"lx\"\n"
    "#define __SIZE_MAX__ 18446744073709551615UL\n"
    "#define __SIZE_TYPE__ long unsigned int\n"
    "#define __SIZE_WIDTH__ 64\n"
    "#define __STDC_EMBED_EMPTY__ 2\n"
    "#define __STDC_EMBED_FOUND__ 1\n"
    "#define __STDC_EMBED_NOT_FOUND__ 0\n"
    "#define __STDC_UTF_16__ 1\n"
    "#define __STDC_UTF_32__ 1\n"
    "#define __STDC__ 1\n"
    "#define __UINT16_C(c) c\n"
    "#define __UINT16_C_SUFFIX__\n"
    "#define __UINT16_FMTX__ \"hX\"\n"
    "#define __UINT16_FMTo__ \"ho\"\n"
    "#define __UINT16_FMTu__ \"hu\"\n"
    "#define __UINT16_FMTx__ \"hx\"\n"
    "#define __UINT16_MAX__ 65535\n"
    "#define __UINT16_TYPE__ unsigned short\n"
    "#define __UINT32_C(c) c##U\n"
    "#define __UINT32_C_SUFFIX__ U\n"
    "#define __UINT32_FMTX__ \"X\"\n"
    "#define __UINT32_FMTo__ \"o\"\n"
    "#define __UINT32_FMTu__ \"u\"\n"
    "#define __UINT32_FMTx__ \"x\"\n"
    "#define __UINT32_MAX__ 4294967295U\n"
    "#define __UINT32_TYPE__ unsigned int\n"
    "#define __UINT64_C(c) c##UL\n"
    "#define __UINT64_C_SUFFIX__ UL\n"
    "#define __UINT64_FMTX__ \"lX\"\n"
    "#define __UINT64_FMTo__ \"lo\"\n"
    "#define __UINT64_FMTu__ \"lu\"\n"
    "#define __UINT64_FMTx__ \"lx\"\n"
    "#define __UINT64_MAX__ 18446744073709551615UL\n"
    "#define __UINT64_TYPE__ long unsigned int\n"
    "#define __UINT8_C(c) c\n"
    "#define __UINT8_C_SUFFIX__\n"
    "#define __UINT8_FMTX__ \"hhX\"\n"
    "#define __UINT8_FMTo__ \"hho\"\n"
    "#define __UINT8_FMTu__ \"hhu\"\n"
    "#define __UINT8_FMTx__ \"hhx\"\n"
    "#define __UINT8_MAX__ 255\n"
    "#define __UINT8_TYPE__ unsigned char\n"
    "#define __UINTMAX_C(c) c##UL\n"
    "#define __UINTMAX_C_SUFFIX__ UL\n"
    "#define __UINTMAX_FMTX__ \"lX\"\n"
    "#define __UINTMAX_FMTo__ \"lo\"\n"
    "#define __UINTMAX_FMTu__ \"lu\"\n"
    "#define __UINTMAX_FMTx__ \"lx\"\n"
    "#define __UINTMAX_MAX__ 18446744073709551615UL\n"
    "#define __UINTMAX_TYPE__ long unsigned int\n"
    "#define __UINTMAX_WIDTH__ 64\n"
    "#define __UINTPTR_FMTX__ \"lX\"\n"
    "#define __UINTPTR_FMTo__ \"lo\"\n"
    "#define __UINTPTR_FMTu__ \"lu\"\n"
    "#define __UINTPTR_FMTx__ \"lx\"\n"
    "#define __UINTPTR_MAX__ 18446744073709551615UL\n"
    "#define __UINTPTR_TYPE__ long unsigned int\n"
    "#define __UINTPTR_WIDTH__ 64\n"
    "#define __UINT_FAST16_FMTX__ \"hX\"\n"
    "#define __UINT_FAST16_FMTo__ \"ho\"\n"
    "#define __UINT_FAST16_FMTu__ \"hu\"\n"
    "#define __UINT_FAST16_FMTx__ \"hx\"\n"
    "#define __UINT_FAST16_MAX__ 65535\n"
    "#define __UINT_FAST16_TYPE__ unsigned short\n"
    "#define __UINT_FAST32_FMTX__ \"X\"\n"
    "#define __UINT_FAST32_FMTo__ \"o\"\n"
    "#define __UINT_FAST32_FMTu__ \"u\"\n"
    "#define __UINT_FAST32_FMTx__ \"x\"\n"
    "#define __UINT_FAST32_MAX__ 4294967295U\n"
    "#define __UINT_FAST32_TYPE__ unsigned int\n"
    "#define __UINT_FAST64_FMTX__ \"lX\"\n"
    "#define __UINT_FAST64_FMTo__ \"lo\"\n"
    "#define __UINT_FAST64_FMTu__ \"lu\"\n"
    "#define __UINT_FAST64_FMTx__ \"lx\"\n"
    "#define __UINT_FAST64_MAX__ 18446744073709551615UL\n"
    "#define __UINT_FAST64_TYPE__ long unsigned int\n"
    "#define __UINT_FAST8_FMTX__ \"hhX\"\n"
    "#define __UINT_FAST8_FMTo__ \"hho\"\n"
    "#define __UINT_FAST8_FMTu__ \"hhu\"\n"
    "#define __UINT_FAST8_FMTx__ \"hhx\"\n"
    "#define __UINT_FAST8_MAX__ 255\n"
    "#define __UINT_FAST8_TYPE__ unsigned char\n"
    "#define __UINT_LEAST16_FMTX__ \"hX\"\n"
    "#define __UINT_LEAST16_FMTo__ \"ho\"\n"
    "#define __UINT_LEAST16_FMTu__ \"hu\"\n"
    "#define __UINT_LEAST16_FMTx__ \"hx\"\n"
    "#define __UINT_LEAST16_MAX__ 65535\n"
    "#define __UINT_LEAST16_TYPE__ unsigned short\n"
    "#define __UINT_LEAST32_FMTX__ \"X\"\n"
    "#define __UINT_LEAST32_FMTo__ \"o\"\n"
    "#define __UINT_LEAST32_FMTu__ \"u\"\n"
    "#define __UINT_LEAST32_FMTx__ \"x\"\n"
    "#define __UINT_LEAST32_MAX__ 4294967295U\n"
    "#define __UINT_LEAST32_TYPE__ unsigned int\n"
    "#define __UINT_LEAST64_FMTX__ \"lX\"\n"
    "#define __UINT_LEAST64_FMTo__ \"lo\"\n"
    "#define __UINT_LEAST64_FMTu__ \"lu\"\n"
    "#define __UINT_LEAST64_FMTx__ \"lx\"\n"
    "#define __UINT_LEAST64_MAX__ 18446744073709551615UL\n"
    "#define __UINT_LEAST64_TYPE__ long unsigned int\n"
    "#define __UINT_LEAST8_FMTX__ \"hhX\"\n"
    "#define __UINT_LEAST8_FMTo__ \"hho\"\n"
    "#define __UINT_LEAST8_FMTu__ \"hhu\"\n"
    "#define __UINT_LEAST8_FMTx__ \"hhx\"\n"
    "#define __UINT_LEAST8_MAX__ 255\n"
    "#define __UINT_LEAST8_TYPE__ unsigned char\n"
    "#define __USER_LABEL_PREFIX__\n"
    "#define __VERSION__ \"Clang 23.1.3\"\n"
    "#define __WINT_MAX__ 4294967295U\n"
    "#define __WINT_MIN__ 0U\n"
    "#define __WINT_TYPE__ unsigned int\n"
    "#define __WINT_UNSIGNED__ 1\n"
    "#define __WINT_WIDTH__ 32\n"
    "#define __amd64 1\n"
    "#define __amd64__ 1\n"
    "#define __clang_literal_encoding__ \"UTF-8\"\n"
    "#define __clang_major__ 23\n"
    "#define __clang_minor__ 1\n"
    "#define __clang_patchlevel__ 3\n"
    "#define __clang_version__ \"23.1.3 \"\n"
    "#define __gnu_linux__ 1\n"
    "#define __linux 1\n"
    "#define __linux__ 1\n"
    "#define __seg_fs __attribute__((address_space(257)))\n"
    "#define __seg_gs __attribute__((address_space(256)))\n"
    "#define __unix 1\n"
    "#define __unix__ 1\n"
    "#define __x86_64 1\n"
    "#define __x86_64__ 1\n"
    ;

// What C23 adds.
static const char pp_predefines_c23[] =
    "#define __CHAR8_TYPE__ unsigned char\n"
    "#define __CLANG_ATOMIC_CHAR8_T_LOCK_FREE 2\n"
    "#define __SIZE_FMTB__ \"lB\"\n"
    "#define __SIZE_FMTb__ \"lb\"\n"
    "#define __UINT16_FMTB__ \"hB\"\n"
    "#define __UINT16_FMTb__ \"hb\"\n"
    "#define __UINT32_FMTB__ \"B\"\n"
    "#define __UINT32_FMTb__ \"b\"\n"
    "#define __UINT64_FMTB__ \"lB\"\n"
    "#define __UINT64_FMTb__ \"lb\"\n"
    "#define __UINT8_FMTB__ \"hhB\"\n"
    "#define __UINT8_FMTb__ \"hhb\"\n"
    "#define __UINTMAX_FMTB__ \"lB\"\n"
    "#define __UINTMAX_FMTb__ \"lb\"\n"
    "#define __UINTPTR_FMTB__ \"lB\"\n"
    "#define __UINTPTR_FMTb__ \"lb\"\n"
    "#define __UINT_FAST16_FMTB__ \"hB\"\n"
    "#define __UINT_FAST16_FMTb__ \"hb\"\n"
    "#define __UINT_FAST32_FMTB__ \"B\"\n"
    "#define __UINT_FAST32_FMTb__ \"b\"\n"
    "#define __UINT_FAST64_FMTB__ \"lB\"\n"
    "#define __UINT_FAST64_FMTb__ \"lb\"\n"
    "#define __UINT_FAST8_FMTB__ \"hhB\"\n"
    "#define __UINT_FAST8_FMTb__ \"hhb\"\n"
    "#define __UINT_LEAST16_FMTB__ \"hB\"\n"
    "#define __UINT_LEAST16_FMTb__ \"hb\"\n"
    "#define __UINT_LEAST32_FMTB__ \"B\"\n"
    "#define __UINT_LEAST32_FMTb__ \"b\"\n"
    "#define __UINT_LEAST64_FMTB__ \"lB\"\n"
    "#define __UINT_LEAST64_FMTb__ \"lb\"\n"
    "#define __UINT_LEAST8_FMTB__ \"hhB\"\n"
    "#define __UINT_LEAST8_FMTb__ \"hhb\"\n"
    ;

// The macros clang keeps with -undef, in every mode (the rest of `clang -undef -dM -E` is __STDC_VERSION__ and
// __STDC_HOSTED__, which pp_predefines adds).
static const char pp_predefines_undefined[] =
    "#define __STDC_EMBED_EMPTY__ 2\n"
    "#define __STDC_EMBED_FOUND__ 1\n"
    "#define __STDC_EMBED_NOT_FOUND__ 0\n"
    "#define __STDC_UTF_16__ 1\n"
    "#define __STDC_UTF_32__ 1\n"
    "#define __STDC__ 1\n"
    ;

_Static_assert(sizeof(pp_predefines_undefined) <= sizeof(pp_predefines_base), "-undef fits in the predefines buffer");

// The predefines buffer for the options, into `buffer` (room for BUSTER_PP_PREDEFINES_BYTES): the fixed part, the
// language mode's, the target's and the other options' (predefines_text) and __BUSTER__. *flt_eval_method gets
// __FLT_EVAL_METHOD__'s value. Returns its length, or BUSTER_PP_PREDEFINES_BYTES + 1 after an error in the options,
// reported on stderr.
static uint32_t pp_predefines(const PPOptions* options, char* buffer, uint32_t* flt_eval_method)
{
    uint32_t length = 0;
    if (options->undefine)
    {
        *flt_eval_method = predefines_eval_method(&options->predefines);
        memcpy(buffer, pp_predefines_undefined, sizeof(pp_predefines_undefined) - 1);
        length += sizeof(pp_predefines_undefined) - 1;
    }
    else
    {
        memcpy(buffer, pp_predefines_base, sizeof(pp_predefines_base) - 1);
        length += sizeof(pp_predefines_base) - 1;
        // clang pushes these two after defining them, so that a #pragma pop_macro gets them back and -D redefines them
        // without a warning.
        length += (uint32_t)sprintf(buffer + length, "#pragma push_macro(\"__GCC_DESTRUCTIVE_SIZE\")\n#pragma push_macro(\"__GCC_CONSTRUCTIVE_SIZE\")\n");
    }
    const CLanguage language = options->language;
    // C94's 199409L is that of any C89 mode that isn't GNU's and has digraphs (CDigraphs), as clang's
    // LangOptions::getCLangStd has it.
    const char* version = language.standard >= 2023                   ? "202311L"
                          : language.standard >= 2017                 ? "201710L"
                          : language.standard >= 2011                 ? "201112L"
                          : language.standard >= 1999                 ? "199901L"
                          : !language.gnu && c_language_digraphs(language) ? "199409L"
                                                                      : 0;
    if (version != 0)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __STDC_VERSION__ %s\n", version);
    }
    if (!options->undefine && !language.gnu)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __STRICT_ANSI__ 1\n");
    }
    else if (!options->undefine && !options->preprocessed)
    {
        length += (uint32_t)sprintf(buffer + length, "#define linux 1\n#define unix 1\n");
    }
    length += (uint32_t)sprintf(buffer + length, "#define __STDC_HOSTED__ %d\n", options->freestanding ? 0 : 1);
    if (!options->undefine && language.standard >= 2023)
    {
        memcpy(buffer + length, pp_predefines_c23, sizeof(pp_predefines_c23) - 1);
        length += sizeof(pp_predefines_c23) - 1;
    }
    if (!options->undefine)
    {
        const uint32_t target = predefines_text(&options->predefines, language, options->freestanding, buffer + length, BUSTER_PREDEFINES_TEXT_BYTES, flt_eval_method);
        length = target <= BUSTER_PREDEFINES_TEXT_BYTES ? length + target : BUSTER_PP_PREDEFINES_BYTES + 1;
        // The one macro that isn't clang's (#117: clang's configuration, plus this). --predefines replaces it too, so that
        // test-preprocessor --clang starts from clang's own macros alone.
        length += length <= BUSTER_PP_PREDEFINES_BYTES ? (uint32_t)sprintf(buffer + length, "#define __BUSTER__ 1\n") : 0;
    }
    if (options->assembly && length <= BUSTER_PP_PREDEFINES_BYTES)
    {
        length += (uint32_t)sprintf(buffer + length, "#define __ASSEMBLER__ 1\n");
    }
    return length;
}

// A file that isn't on disk (the predefines, the command line, the forced includes' lines), at a name in angle brackets,
// and what it is for line markers: clang's predefines are a system header, and the rest of its <built-in> buffer, the
// command line's and the forced includes', isn't. Returns its PPFile.
static uint32_t pp_virtual_file(Preprocessor* pp, const char* name, const char* text, uint32_t length, uint8_t system)
{
    uint32_t result = BUSTER_PP_NONE;
    uint8_t* bytes = arena_push(&pp->arena, (uint64_t)length + 64, 64);
    if (bytes != 0 && pp->data_count < (BUSTER_PP_MAX_FILES >> pp->capacity_shift) && pp->file_count < (BUSTER_PP_MAX_FILES >> pp->capacity_shift))
    {
        memcpy(bytes, text, length);
        memset(bytes + length, 0, 64);
        const uint32_t data = pp->data_count;
        pp->datas[data] = (PPFileData){ .device = 0, .inode = UINT64_MAX - data, .bytes = bytes, .length = length, .record = BUSTER_PP_NONE };
        pp->data_count += 1;
        result = pp->file_count;
        pp->files[result] = (PPFile){
            .path = (char*)name,
            .path_length = (uint32_t)strlen(name),
            .directory_length = BUSTER_PP_NONE,
            .data = data,
            .system = system,
        };
        pp->file_count += 1;
    }
    return result;
}

// Why pp_virtual_file couldn't make a file, for a message: the unit's files table is full, or else memory ran out.
static const char* pp_virtual_file_error(const Preprocessor* pp)
{
    return pp->data_count >= (BUSTER_PP_MAX_FILES >> pp->capacity_shift) || pp->file_count >= (BUSTER_PP_MAX_FILES >> pp->capacity_shift) ? "too many files" : strerror(ENOMEM);
}

// Runs a file to its end, as the main file or one that runs before it.
static void pp_run_file(Preprocessor* pp, uint32_t file)
{
    const uint32_t depth = pp->context_count;
    pp_enter_file(pp, file, BUSTER_PP_NONE, 0, BUSTER_PP_NONE, 0, 0);
    pp_run_contexts(pp, depth);
    // A stream (an attribute query's operand) reads on past an included file's end, but not past this one's.
    while (!pp->fatal && pp->stream_count != 0)
    {
        pp_cut_stream(pp);
    }
}

// The tree whose build/ holds the executable at `path`, an absolute path, cut in place to it: the parent of the path's
// last build/ directory, whatever is below it (build/compiler, which build.sh publishes, and
// build/cmake/VARIANT/CONFIG/compiler, each configuration's own), or, in a path without one, two levels up, as from
// build/compiler. Returns whether a directory is left.
static bool pp_buster_tree(char* path)
{
    char* build = 0;
    for (char* found = strstr(path, "/build/"); found != 0; found = strstr(found + 1, "/build/"))
    {
        build = found;
    }
    bool result = true;
    if (build != 0)
    {
        result = build != path;
        *build = 0;
    }
    for (uint32_t up = 0; build == 0 && result && up < 2; up += 1)
    {
        char* slash = strrchr(path, '/');
        result = slash != 0 && slash != path;
        if (result)
        {
            *slash = 0;
        }
    }
    return result;
}

// buster's own headers' directory: src/buster/include in the tree whose build/ holds the executable (as clang finds its
// resource directory from its executable). Returns whether it exists; optionally counts its stat attempt.
static bool pp_buster_headers_directory(char* path, uint32_t capacity, uint64_t* stat_count)
{
    char executable[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    bool result = length > 0;
    if (result)
    {
        executable[length] = 0;
        result = pp_buster_tree(executable);
        result = result && (uint32_t)snprintf(path, capacity, "%s/src/buster/include", executable) < capacity;
        struct stat stats;
        if (stat_count != 0)
        {
            *stat_count += result;
        }
        result = result && stat(path, &stats) == 0 && S_ISDIR(stats.st_mode);
    }
    return result;
}

// An explicitly selected Clang resource directory, or the highest installed
// version's headers (Clang searches its resource directory first), if any.
// Optionally counts its explicit stat attempt, not the directory enumeration's libc-internal system calls.
static bool pp_clang_resource_directory(char* path, uint32_t capacity, uint64_t* stat_count)
{
    bool result = false;
    const char* selected = getenv("BUSTER_CLANG_RESOURCE_DIR");
    const bool configured = selected != 0 && selected[0] != 0;
    DIR* directory = configured ? 0 : opendir("/usr/lib/clang");
    uint32_t best = 0;
    if (directory != 0)
    {
        struct dirent* entry;
        while ((entry = readdir(directory)) != 0)
        {
            const uint32_t version = (uint32_t)strtoul(entry->d_name, 0, 10);
            if (version > best)
            {
                best = version;
            }
        }
        closedir(directory);
        if (best != 0)
        {
            snprintf(path, capacity, "/usr/lib/clang/%u/include", best);
            struct stat stats;
            if (stat_count != 0)
            {
                *stat_count += 1;
            }
            result = stat(path, &stats) == 0 && S_ISDIR(stats.st_mode);
        }
    }
    if (configured)
    {
        const int length = snprintf(path, capacity, "%s/include", selected);
        result = length >= 0 && (uint32_t)length < capacity;
        if (result)
        {
            struct stat stats;
            if (stat_count != 0)
            {
                *stat_count += 1;
            }
            result = stat(path, &stats) == 0 && S_ISDIR(stats.st_mode);
        }
    }
    return result;
}

static bool pp_add_directory(Preprocessor* pp, const char* path, uint8_t system)
{
    uint32_t length = (uint32_t)strlen(path);
    while (length > 1 && path[length - 1] == '/')
    {
        length -= 1;
    }
    char* copy = arena_push(&pp->arena, (uint64_t)length + 1, 1);
    const bool result = copy != 0 && pp->directory_count < (BUSTER_PP_MAX_PUSHED >> pp->capacity_shift);
    if (result)
    {
        memcpy(copy, path, length);
        copy[length] = 0;
        pp->directories[pp->directory_count] = copy;
        pp->directory_lengths[pp->directory_count] = length;
        pp->directory_system[pp->directory_count] = system;
        pp->directory_entries[pp->directory_count] = 0;
        pp->directory_count += 1;
    }
    return result;
}

// Drops the search directories that don't exist, and the later of two that are the same directory (the same device and
// inode), as clang does (InitHeaderSearch): among the -iquote ones, then among the others, where a directory that is
// both a system one and not drops the one that isn't. So #include_next can't find a file again through a repeated -I.
static void pp_dedupe_directories(Preprocessor* pp)
{
    Scratch scratch = scratch_begin(0);
    const uint32_t count = pp->directory_count;
    uint64_t* devices = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(uint64_t), 8);
    uint64_t* inodes = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(uint64_t), 8);
    bool* kept = arena_push_array(scratch.arena, (uint64_t)count + 1, sizeof(bool), 8);
    if (devices != 0 && inodes != 0 && kept != 0)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            struct stat stats;
            if (pp->options.timing)
            {
                pp->statistics.directory_stats += 1;
            }
            kept[i] = stat(pp->directories[i], &stats) == 0 && S_ISDIR(stats.st_mode);
            devices[i] = kept[i] ? (uint64_t)stats.st_dev : 0;
            inodes[i] = kept[i] ? (uint64_t)stats.st_ino : 0;
        }
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t group_first = i < pp->angled_first ? 0 : pp->angled_first;
            for (uint32_t j = group_first; kept[i] && j < i; j += 1)
            {
                if (kept[j] && devices[j] == devices[i] && inodes[j] == inodes[i])
                {
                    const bool system_over_user = pp->directory_system[i] && !pp->directory_system[j];
                    kept[j] = !system_over_user;
                    kept[i] = system_over_user;
                }
            }
        }
        uint32_t written = 0;
        uint32_t angled_first = 0;
        // --buster-headers' fallback keeps its rule where it moves to; dropped (an earlier directory is the same), it
        // has none.
        uint32_t fallback = BUSTER_PP_NONE;
        for (uint32_t i = 0; i < count; i += 1)
        {
            angled_first += i < pp->angled_first && kept[i];
            if (kept[i])
            {
                fallback = i == pp->fallback_directory ? written : fallback;
                pp->directories[written] = pp->directories[i];
                pp->directory_lengths[written] = pp->directory_lengths[i];
                pp->directory_system[written] = pp->directory_system[i];
                pp->directory_entries[written] = 0;
                written += 1;
            }
        }
        pp->directory_count = written;
        pp->angled_first = angled_first;
        pp->fallback_directory = fallback;
    }
    scratch_end(scratch);
}

// An array of `count` records of `size` bytes in an arena of its own (address space, touched as it is used), whose first
// `small` bytes are base pages, while `*reserved` (which becomes false if it can't be): where a history table moves when
// it outgrows its slice (pp_history_grow). It is committed whole, up to 2 GB, as a table (arena_reserve_tables), which
// charges no commit for it where commit charge isn't bounded (#1521).
static void* pp_array_small(Preprocessor* pp, bool* reserved, uint64_t count, uint64_t size, uint64_t small)
{
    void* result = 0;
    if (*reserved && pp->array_count < BUSTER_PP_ARRAYS)
    {
        ArenaTable array = {
            .bytes = count * size + 64,
            .small = small,
        };
        pp->arrays[pp->array_count] = arena_reserve_tables(&array, 1);
        result = array.base;
        pp->array_count += 1;
    }
    *reserved = result != 0;
    return result;
}

static void pp_table(PPTables* plan, void** field, uint64_t count, uint64_t size)
{
    *field = 0;
    if (count != 0 && plan->count < BUSTER_ARENA_TABLES)
    {
        const uint64_t bytes = count * size + 64;
        plan->tables[plan->count] = (ArenaTable){ .bytes = bytes, .small = bytes };
        plan->fields[plan->count] = field;
        plan->count += 1;
    }
    else
    {
        plan->full = plan->full || count != 0;
    }
}

// A stack of `capacity` tokens (and BUSTER_PP_TOKEN_SLACK), its columns tables of the unit's (pp_table), as
// pp_tokens_reserve's are of the output's own arena of tables.
static void pp_tokens_plan(PPTables* plan, PPTokens* tokens, uint32_t capacity)
{
    const uint64_t slots = (uint64_t)capacity + BUSTER_PP_TOKEN_SLACK;
    pp_table(plan, (void**)&tokens->kinds, slots, 1);
    pp_table(plan, (void**)&tokens->flags, slots, 1);
    pp_table(plan, (void**)&tokens->values, slots, sizeof(uint32_t));
    pp_table(plan, (void**)&tokens->locations, slots, sizeof(uint32_t));
    tokens->count = 0;
    tokens->capacity = capacity;
}

// SOURCE_DATE_EPOCH is a complete, nonnegative decimal integer. strtoll handles overflow; the first-byte and end
// checks reject whitespace, signs, empty values and suffixes rather than silently using a partial timestamp.
static bool pp_initialize_time(Preprocessor* pp)
{
    const char* epoch = getenv("SOURCE_DATE_EPOCH");
    int64_t seconds = 0;
    bool result;
    if (epoch != 0)
    {
        char* end;
        errno = 0;
        const long long parsed = strtoll(epoch, &end, 10);
        result = errno == 0 && epoch[0] >= '0' && epoch[0] <= '9' && end != epoch && *end == 0 && parsed >= 0;
        if (result)
        {
            seconds = parsed;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "invalid SOURCE_DATE_EPOCH: expected a nonnegative decimal integer in range");
        }
    }
    else
    {
        const time_t now = time(0);
        result = now != (time_t)-1;
        if (result)
        {
            seconds = (int64_t)now;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't read the current time for predefined macros");
        }
    }
    // SOURCE_DATE_EPOCH's calendar, in UTC, here, where its refusals are reported. The current time's, in local time,
    // waits for the first __DATE__ or __TIME__ (pp_unit_time, #652): it reads the time zone's file, 7 system calls of
    // every unit's start, which few units expand either of.
    struct tm calendar;
    pp->unit_seconds = seconds;
    result = result && (epoch == 0 || pp_calendar(pp, seconds, true, 0, "SOURCE_DATE_EPOCH", &calendar));
    if (result && epoch != 0)
    {
        pp_format_date_time(pp, &calendar);
        pp_format_timestamp(pp->timestamp, sizeof(pp->timestamp), &calendar);
        pp->has_epoch = true;
    }
    return result;
}

static bool pp_initialize_once(Preprocessor* pp, const PPOptions* options, InternTable* table)
{
    *pp = (Preprocessor){ 0 };
    pp->diagnostic_include_range = BUSTER_PP_NONE;
    pp->diagnostic_position = BUSTER_PP_NONE;
    pp->diagnostic_run_first = BUSTER_PP_NONE;
    pp->fallback_directory = BUSTER_PP_NONE;
    pp->predefines_data = BUSTER_PP_NONE;
    pp->options = *options;
    pp->table = table;
    // A deferred history records nothing until pp_history_ensure (PPOptions.defer_history); its run prints no
    // diagnostic (pp_run), but this setup's are printed, its fatal errors among them.
    pp->history_deferred = options->defer_history && !options->no_history && !options->show_includes;
    pp->history_quiet = options->quiet;
    pp->options.no_history |= pp->history_deferred;
    const PPPrepare prepare = pp_prepare_options(options);
    pp->vectorized = prepare.vectorized;
    pp->refine_flags = prepare.refine_flags;
    pp->split_kinds = prepare.split_kinds;
    // The memory, reserved in turn until a reservation fails (pp_release releases what was).
    // Paths and the predefines' text, about 40 KB on sqlite3.c and 90 KB on this compiler's main.c: base pages.
    pp->arena = arena_reserve_small(BUSTER_PP_ARENA_BYTES, 256ull << 10);
    bool result = pp->arena.base != 0;
    for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(pp->columns); i += 1)
    {
        pp->columns[i] = arena_reserve(BUSTER_PP_ARENA_BYTES);
        result = pp->columns[i].base != 0;
    }
    for (uint32_t i = 0; result && i < 2; i += 1)
    {
        pp->scratch[i] = arena_reserve(BUSTER_PP_SCRATCH_BYTES);
        result = pp->scratch[i].base != 0;
    }
    pp->scratch_range[0] = BUSTER_PP_NONE;
    pp->scratch_range[1] = BUSTER_PP_NONE;
    const uint64_t largest_tables[] = {
        (uint64_t)BUSTER_PP_MAX_OUTPUT_TOKENS * sizeof(PPExpansionStart),
        (uint64_t)BUSTER_PP_MAX_STACK_TOKENS * sizeof(PPArgument),
        (uint64_t)BUSTER_PP_MAX_ITEMS * sizeof(PPItem),
        (uint64_t)BUSTER_PP_MAX_MACROS * sizeof(PPMacro),
        (uint64_t)BUSTER_PP_MAX_TEMPLATES * sizeof(PPTemplate),
        (uint64_t)BUSTER_PP_MAX_CONTEXTS * sizeof(PPInvocation),
        (uint64_t)BUSTER_PP_MAX_FILES * sizeof(PPFileData),
    };
    uint64_t largest = (uint64_t)BUSTER_PP_MAX_OUTPUT_TOKENS * sizeof(uint32_t) + BUSTER_PP_TOKEN_SLACK * sizeof(uint32_t);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(largest_tables); i += 1)
    {
        largest = largest > largest_tables[i] ? largest : largest_tables[i];
    }
    pp->capacity_shift = arena_capacity_shift(largest);
    result = result && pp_tokens_reserve(&pp->output, (BUSTER_PP_MAX_OUTPUT_TOKENS >> pp->capacity_shift));
    // The unit's tables, in one arena (pp_table), the first of `arrays`: the records', the stacks' columns and the unit's
    // own file cache (#652), which had four arenas of their own, a mapping and a commit each, though a small unit
    // touches a page of each or none.
    const uint32_t shift = pp->capacity_shift;
    PPTables plan = { 0 };
    pp_table(&plan, (void**)&pp->ranges, BUSTER_PP_MAX_RANGES >> shift, sizeof(PPRange));
    pp_table(&plan, (void**)&pp->datas, BUSTER_PP_MAX_FILES >> shift, sizeof(PPFileData));
    pp_table(&plan, (void**)&pp->dependencies, options->dependencies.enabled ? BUSTER_PP_MAX_FILES >> shift : 0, sizeof(DependencyFile));
    pp_table(&plan, (void**)&pp->record_data, BUSTER_PP_MAX_FILES, sizeof(uint32_t));
    pp_table(&plan, (void**)&pp->files, BUSTER_PP_MAX_FILES >> shift, sizeof(PPFile));
    pp_table(&plan, (void**)&pp->file_slots, BUSTER_PP_FILE_SLOTS >> shift, sizeof(uint32_t));
    pp->file_slot_mask = BUSTER_PP_INITIAL_FILE_SLOTS - 1;
    pp_table(&plan, (void**)&pp->directories, BUSTER_PP_MAX_PUSHED >> shift, sizeof(char*));
    pp_table(&plan, (void**)&pp->directory_lengths, BUSTER_PP_MAX_PUSHED >> shift, sizeof(uint32_t));
    pp_table(&plan, (void**)&pp->directory_system, BUSTER_PP_MAX_PUSHED >> shift, sizeof(uint8_t));
    pp_table(&plan, (void**)&pp->directory_entries, BUSTER_PP_MAX_PUSHED >> shift, sizeof(FileCacheDirectory*));
    pp_table(&plan, (void**)&pp->line_directives, BUSTER_PP_MAX_LINE_DIRECTIVES >> shift, sizeof(PPLineDirective));
    pp_table(&plan, (void**)&pp->expansion_starts, (uint64_t)(BUSTER_PP_MAX_OUTPUT_TOKENS >> shift) + 1, sizeof(PPExpansionStart));
    pp_table(&plan, (void**)&pp->silent_pragmas, BUSTER_PP_MAX_SILENT_PRAGMAS >> shift, sizeof(PPExpansionStart));
    pp_table(&plan, (void**)&pp->embeds, BUSTER_PP_MAX_EMBEDS >> shift, sizeof(PPEmbed));
    pp_table(&plan, (void**)&pp->macro_of, intern_id_limit(table), sizeof(PPMacroId));
    pp_table(&plan, (void**)&pp->is_macro, intern_id_limit(table) / 64, sizeof(uint64_t));
    pp_table(&plan, (void**)&pp->poisoned, intern_id_limit(table) / 64, sizeof(uint64_t));
    pp_table(&plan, (void**)&pp->macros, BUSTER_PP_MAX_MACROS >> shift, sizeof(PPMacro));
    pp_table(&plan, (void**)&pp->items, BUSTER_PP_MAX_ITEMS >> shift, sizeof(PPItem));
    pp_table(&plan, (void**)&pp->parameters, BUSTER_PP_MAX_ITEMS >> shift, sizeof(IdentId));
    // The vectorized path's compiled bodies, which a unit with history doesn't use.
    pp_table(&plan, (void**)&pp->templates, BUSTER_VECTOR_PATH(pp->vectorized) && pp->options.no_history ? BUSTER_PP_MAX_TEMPLATES >> shift : 0, sizeof(PPTemplate));
    pp_table(&plan, (void**)&pp->pushed_names, BUSTER_PP_MAX_PUSHED >> shift, sizeof(IdentId));
    pp_table(&plan, (void**)&pp->pushed_macros, BUSTER_PP_MAX_PUSHED >> shift, sizeof(PPMacroId));
    pp_table(&plan, (void**)&pp->contexts, BUSTER_PP_MAX_CONTEXTS >> shift, sizeof(PPContext));
    pp_table(&plan, (void**)&pp->conditionals, BUSTER_PP_MAX_CONTEXTS >> shift, sizeof(PPConditional));
    pp_table(&plan, (void**)&pp->invocations, BUSTER_PP_MAX_CONTEXTS >> shift, sizeof(PPInvocation));
    pp_table(&plan, (void**)&pp->argument_records, BUSTER_PP_MAX_STACK_TOKENS >> shift, sizeof(PPArgument));
    pp_table(&plan, (void**)&pp->capture_frames, BUSTER_PP_MAX_CONTEXTS >> shift, sizeof(PPCapture));
    pp_tokens_plan(&plan, &pp->arguments, BUSTER_PP_MAX_STACK_TOKENS >> shift);
    pp_tokens_plan(&plan, &pp->captures, BUSTER_PP_MAX_STACK_TOKENS >> shift);
    pp_tokens_plan(&plan, &pp->expansions, BUSTER_PP_MAX_STACK_TOKENS >> shift);
    // The unit's own file cache, 120 KB, most of it the lanes' arenas, and its four arrays (file_cache_start): zero as
    // tables, so that a small unit neither writes 30 pages of it nor reserves four arenas for a page of each (#652).
    pp_table(&plan, (void**)&pp->cache, options->files == 0, sizeof(FileCache));
    const uint32_t cache_shift = file_cache_shift();
    uint64_t cache_counts[4];
    uint64_t cache_sizes[4];
    void* cache_arrays[4];
    file_cache_array_shape(cache_shift, cache_counts, cache_sizes);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cache_arrays); i += 1)
    {
        pp_table(&plan, &cache_arrays[i], options->files == 0 ? cache_counts[i] : 0, cache_sizes[i]);
    }
    if (result && !plan.full)
    {
        pp->arrays[0] = arena_reserve_tables(plan.tables, plan.count);
        pp->array_count = 1;
    }
    result = result && !plan.full && pp->arrays[0].base != 0;
    for (uint32_t i = 0; i < plan.count; i += 1)
    {
        *plan.fields[i] = plan.tables[i].base;
    }
    if (result && !pp->options.no_history)
    {
        // Five retained tables and three temporary columns share base pages for their first few records. Only a
        // table that outgrows its slice reserves a large arena, avoiding eight mappings/advice calls on small units.
        pp->history_initial = arena_reserve(8 * BUSTER_PP_INITIAL_HISTORY_BYTES);
        pp->history = pp_history_initial(pp, sizeof(PPExpansion), &pp->history_capacity);
        pp->substitutions = pp_history_initial(pp, sizeof(PPSubstitution), &pp->substitution_capacity);
        pp->generated = pp_history_initial(pp, sizeof(PPGenerated), &pp->generated_capacity);
        pp->history_sources = pp_history_initial(pp, sizeof(PPSource), &pp->history_source_capacity);
        pp->history_runs = pp_history_initial(pp, sizeof(PPHistoryRun), &pp->history_run_capacity);
        PPTokens* buffers[] = { &pp->arguments, &pp->captures, &pp->expansions };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(buffers); i += 1)
        {
            PPTokens* tokens = buffers[i];
            tokens->origins = pp_history_initial(pp, sizeof(uint32_t), &tokens->origin_capacity);
            result = result && tokens->origins != 0;
        }
        result = result && pp->history != 0 && pp->substitutions != 0 && pp->generated != 0 && pp->history_sources != 0 && pp->history_runs != 0;
    }
    // The file cache: the options', made for the same language mode and intern table, or one of the unit's own, in its
    // tables.
    pp->cache = options->files != 0 ? options->files : pp->cache;
    if (options->files == 0)
    {
        pp->owns_cache = result;
        result = result && file_cache_start(pp->cache, table, pp_prepare_config(pp), cache_shift, cache_arrays);
    }
    else if (pp->cache->table != table || pp->cache->prepare.refine_flags != pp->refine_flags || pp->cache->prepare.split_kinds != pp->split_kinds ||
             pp->cache->prepare.dollar_punctuation != pp_dollar_punctuation(options))
    {
        fprintf(stderr, "error: the file cache was made for another language mode or intern table\n");
        result = false;
    }
    if (result)
    {
        // Macro 0 is none; location 0 is none.
        pp->macro_count = 1;
        pp->location_top = 1;
        // The search directories: -iquote ones for quoted names only, then -I, -isystem, the standard ones and -idirafter.
        for (uint32_t i = 0; result && i < options->quote_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->quote_directories[i], BUSTER_PP_USER_FILE);
        }
        pp->angled_first = pp->directory_count;
        for (uint32_t i = 0; result && i < options->include_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->include_directories[i], BUSTER_PP_USER_FILE);
        }
        for (uint32_t i = 0; result && i < options->system_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->system_directories[i], BUSTER_PP_SYSTEM_FILE);
        }
        if (result && !options->no_standard_includes)
        {
            // The compiler's headers, which -nobuiltininc leaves out: clang's resource directory (the default), or with
            // --buster-headers buster's own, then clang's for the headers buster's doesn't supply. That one has no name
            // buster's directory has (pp_find_include), so buster's headers' #include_next goes on to the C library's,
            // and every other name is found as without the flag.
            const bool builtin = !options->no_builtin_includes;
            char buster[PATH_MAX];
            char resource[PATH_MAX];
            bool buster_added = false;
            uint64_t* stat_count = options->timing ? &pp->statistics.directory_stats : 0;
            if (builtin && options->buster_headers && !pp_buster_headers_directory(buster, sizeof(buster), stat_count))
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "--buster-headers: no src/buster/include beside the executable's build directory");
            }
            else if (builtin && options->buster_headers)
            {
                result = pp_add_directory(pp, buster, BUSTER_PP_SYSTEM_FILE);
                buster_added = result;
                pp->buster_directory = result ? pp->directories[pp->directory_count - 1] : 0;
                pp->buster_directory_length = result ? pp->directory_lengths[pp->directory_count - 1] : 0;
            }
            if (result && builtin && pp_clang_resource_directory(resource, sizeof(resource), stat_count))
            {
                pp->fallback_directory = buster_added ? pp->directory_count : BUSTER_PP_NONE;
                result = pp_add_directory(pp, resource, BUSTER_PP_SYSTEM_FILE);
            }
            // Then the C library's, as clang's Linux toolchain lists them: Debian's multiarch directory and /include
            // between /usr/local/include and /usr/include. pp_dedupe_directories drops those a system doesn't have.
            result = result && pp_add_directory(pp, "/usr/local/include", BUSTER_PP_EXTERN_C_FILE) &&
                     pp_add_directory(pp, "/usr/include/x86_64-linux-gnu", BUSTER_PP_EXTERN_C_FILE) &&
                     pp_add_directory(pp, "/include", BUSTER_PP_EXTERN_C_FILE) && pp_add_directory(pp, "/usr/include", BUSTER_PP_EXTERN_C_FILE);
        }
        for (uint32_t i = 0; result && i < options->after_directory_count; i += 1)
        {
            result = pp_add_directory(pp, options->after_directories[i], BUSTER_PP_SYSTEM_FILE);
        }
        if (result)
        {
            pp_dedupe_directories(pp);
        }
        // The builtin macros.
        static const struct
        {
            IdentId name;
            PPBuiltin builtin;
            bool function_like;
        } builtins[] = {
            { C_NAME_FILE_MACRO, PP_BUILTIN_FILE, false },
            { C_NAME_LINE_MACRO, PP_BUILTIN_LINE, false },
            { C_NAME_COUNTER, PP_BUILTIN_COUNTER, false },
            { C_NAME_DATE, PP_BUILTIN_DATE, false },
            { C_NAME_TIME, PP_BUILTIN_TIME, false },
            { C_NAME_TIMESTAMP, PP_BUILTIN_TIMESTAMP, false },
            { C_NAME_BASE_FILE, PP_BUILTIN_BASE_FILE, false },
            { C_NAME_FILE_NAME, PP_BUILTIN_FILE_NAME, false },
            { C_NAME_INCLUDE_LEVEL, PP_BUILTIN_INCLUDE_LEVEL, false },
            { C_NAME_FLT_EVAL_METHOD, PP_BUILTIN_FLT_EVAL_METHOD, false },
            { C_NAME_PRAGMA_OPERATOR, PP_BUILTIN_PRAGMA, true },
            { C_NAME_DEFINED, PP_BUILTIN_DEFINED, false },
            { C_NAME_HAS_INCLUDE, PP_BUILTIN_HAS_INCLUDE, true },
            { C_NAME_HAS_INCLUDE_NEXT, PP_BUILTIN_HAS_INCLUDE_NEXT, true },
            { C_NAME_HAS_ATTRIBUTE, PP_BUILTIN_HAS_ATTRIBUTE, true },
            { C_NAME_HAS_BUILTIN, PP_BUILTIN_HAS_BUILTIN, true },
            { C_NAME_HAS_FEATURE, PP_BUILTIN_HAS_FEATURE, true },
            { C_NAME_HAS_EXTENSION, PP_BUILTIN_HAS_EXTENSION, true },
            { C_NAME_HAS_C_ATTRIBUTE, PP_BUILTIN_HAS_C_ATTRIBUTE, true },
            { C_NAME_HAS_DECLSPEC_ATTRIBUTE, PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE, true },
            { C_NAME_HAS_WARNING, PP_BUILTIN_HAS_WARNING, true },
            { C_NAME_HAS_EMBED, PP_BUILTIN_HAS_EMBED, true },
            { C_NAME_IS_IDENTIFIER, PP_BUILTIN_IS_IDENTIFIER, true },
            { C_NAME_IS_TARGET_ARCH, PP_BUILTIN_IS_TARGET_ARCH, true },
            { C_NAME_IS_TARGET_VENDOR, PP_BUILTIN_IS_TARGET_VENDOR, true },
            { C_NAME_IS_TARGET_OS, PP_BUILTIN_IS_TARGET_OS, true },
            { C_NAME_IS_TARGET_ENVIRONMENT, PP_BUILTIN_IS_TARGET_ENVIRONMENT, true },
            { C_NAME_IS_TARGET_VARIANT_OS, PP_BUILTIN_IS_TARGET_VARIANT_OS, true },
            { C_NAME_IS_TARGET_VARIANT_ENVIRONMENT, PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT, true },
            { C_NAME_BUILDING_MODULE, PP_BUILTIN_BUILDING_MODULE, true },
        };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(builtins); i += 1)
        {
            pp_define_builtin(pp, builtins[i].name, builtins[i].builtin, builtins[i].function_like);
        }
        // Clang's query that isn't among c_intern.c's predefined names.
        static const struct
        {
            const char* name;
            PPBuiltin builtin;
            bool function_like;
        } named_builtins[] = {
            { "__has_constexpr_builtin", PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, true },
        };
        for (uint32_t i = 0; result && i < BUSTER_ARRAY_LENGTH(named_builtins); i += 1)
        {
            const IdentId id = intern_find_or_add(table, (const uint8_t*)named_builtins[i].name, (uint32_t)strlen(named_builtins[i].name));
            result = id != 0;
            if (result)
            {
                pp_define_builtin(pp, id, named_builtins[i].builtin, named_builtins[i].function_like);
            }
        }
        // __DATE__ and __TIME__ from SOURCE_DATE_EPOCH if it is set (reproducible builds), else now; __TIMESTAMP__ from
        // SOURCE_DATE_EPOCH too, else each file's modification time (pp_expand_builtin).
        if (pp_initialize_time(pp))
        {
            pp->zero_location = pp_scratch_clean(pp, (const uint8_t*)"0", 1);
            pp->one_location = pp_scratch_clean(pp, (const uint8_t*)"1", 1);
            pp->hash_location = pp_scratch_clean(pp, (const uint8_t*)"#", 1);
            // The output's keyword kinds: a predefined name that is a keyword in this mode, by id (below 128).
            for (uint32_t id = 0; id < BUSTER_PP_KEYWORD_TABLE; id += 1)
            {
                pp->keyword_kinds[id] = id < C_NAME_COUNT && c_name_is_keyword(id, options->language) ? (uint8_t)(BUSTER_PP_KIND_KEYWORD + id - 1) : C_TOKEN_RAW_IDENTIFIER;
            }
        }
        result = !pp->fatal;
    }
    return result;
}

BUSTER_PREPROCESSOR_ENTRY bool pp_initialize(Preprocessor* pp, const PPOptions* options, InternTable* table)
{
    const bool compact = arena_compact || arena_commit_refused;
    const ArenaFailure failure = arena_failure;
    bool result = pp_initialize_once(pp, options, table);
    if (!result && !pp->fatal && !compact && (arena_compact || arena_commit_refused))
    {
        ArenaPool* pool = arena_pool;
        arena_pool = 0;
        pp_release(pp);
        arena_pool = pool;
        result = pp_initialize_once(pp, options, table);
        arena_failure = result ? failure : arena_failure;
    }
    return result;
}

// Why the file of record `file` (BUSTER_PP_NONE if its record couldn't be made) has no data, for a message.
static const char* pp_read_error(const Preprocessor* pp, uint32_t file)
{
    const int error = file != BUSTER_PP_NONE ? pp->files[file].error : ENOMEM;
    return error > 0 ? strerror(error) : "not a regular file";
}

// Runs what comes before the main file: the predefines (or PPOptions.predefines_path's), then the command line's -D,
// -U and -include. Returns false, after a fatal error, if the predefines can't be read, a part of the prelude can't be
// made (out of memory, or the unit's files table full) or running a part was fatal; the parts after it don't run.
// Bytes of `text` before a newline, less any backslashes at its end: the synthesized directive's own newline must not
// be a line splice's other half, which would join the next directive onto this one's last line.
static uint32_t pp_command_line_length(const char* text)
{
    uint32_t length = 0;
    while (text[length] != 0 && text[length] != '\n' && text[length] != '\r')
    {
        length += 1;
    }
    while (length != 0 && text[length - 1] == '\\')
    {
        length -= 1;
    }
    return length;
}

// Whether `path` can be pasted as `#include "..."`'s spelling: no '"' to end the literal, no newline to break the
// directive, and no '\' whose escape would change the name (a trailing one would splice the next line).
static bool pp_command_include_safe(const char* path)
{
    bool safe = true;
    for (uint32_t i = 0; safe && path[i] != 0; i += 1)
    {
        safe = path[i] != '"' && path[i] != '\\' && path[i] != '\n' && path[i] != '\r';
    }
    return safe;
}

// A raw -include's file run with the parentage a synthesized `#include` inside <built-in> would give it: entered at
// include depth 1 under a <built-in> range, so __INCLUDE_LEVEL__ is 1, the __BASE_FILE__ root walk names <built-in>,
// and #include_next resumes the search the raw lookup made. The <built-in> file is made, entered and popped for its
// range alone — its dead conditional gives the range a span — then the depth the popped context would have kept is
// added by hand for the raw file's run.
static bool pp_command_include_raw(Preprocessor* pp, const char* path)
{
    bool result = true;
    const bool newline = strpbrk(path, "\n\r") != NULL;
    if (newline)
    {
        pp_diagnose(pp, PP_SEVERITY_ERROR, 0, "-include path contains a newline");
        result = false;
    }
    else
    {
        uint32_t found_in = 0;
        const uint64_t bytes = strlen(path);
        // A quoted include searches the includer's directory first. The command line has none, so "." stands in for it.
        const uint32_t anchor = pp_virtual_file(pp, ".", "", 0, BUSTER_PP_USER_FILE);
        const uint32_t file = bytes <= UINT32_MAX && anchor != BUSTER_PP_NONE ? pp_find_include(pp, (const uint8_t*)path, (uint32_t)bytes, false, false, anchor, 0, &found_in, 0) : BUSTER_PP_NONE;
        if (file == BUSTER_PP_NONE && !pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "'%s' file not found", path);
        }
        if (file != BUSTER_PP_NONE)
        {
            const uint32_t built_in = pp_virtual_file(pp, "<built-in>", "#if 0\n#endif\n", 13, BUSTER_PP_USER_FILE);
            uint32_t built_in_range = BUSTER_PP_NONE;
            const uint32_t depth = pp->context_count;
            if (built_in != BUSTER_PP_NONE)
            {
                pp_enter_file(pp, built_in, BUSTER_PP_NONE, 0, BUSTER_PP_NONE, 0, 0);
                PPContext* base = pp->context_count > depth ? &pp->contexts[pp->context_count - 1] : 0;
                if (base != 0 && base->kind == BUSTER_PP_CONTEXT_FILE && base->file == built_in)
                {
                    built_in_range = base->range;
                    pp_pop_context(pp);
                }
            }
            if (built_in_range != BUSTER_PP_NONE && pp->context_count == depth)
            {
                const uint32_t include_depth = pp->include_depth;
                pp->include_depth += 1;
                pp_enter_file(pp, file, built_in_range, pp->ranges[built_in_range].base, found_in, 0, 0);
                if (pp->context_count == depth)
                {
                    // A skipped/refused file has no context whose pop would consume the synthetic level.
                    pp->include_depth = include_depth;
                }
                pp_run_contexts(pp, depth);
            }
            else if (!pp->fatal)
            {
                pp_run_file(pp, file);
            }
        }
        result = file != BUSTER_PP_NONE && !pp->fatal;
    }
    return result;
}

// Runs the main file, then gives the output's keywords their kinds. Returns false if there were errors.
static bool pp_run_prelude(Preprocessor* pp)
{
    pp_warning_options_diagnose(pp);
    bool result = !pp->fatal;
    // The predefines.
    if (result && pp->options.predefines_path != 0)
    {
        const uint32_t file = pp_lookup_path(pp, pp->options.predefines_path, (uint32_t)strlen(pp->options.predefines_path), BUSTER_PP_NONE, 0, BUSTER_PP_SYSTEM_FILE);
        pp->flt_eval_method = predefines_eval_method(&pp->options.predefines);
        result = file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE;
        if (result)
        {
            pp->predefines_data = pp->files[file].data;
            pp_run_file(pp, file);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't read the predefines file '%s': %s", pp->options.predefines_path, pp_read_error(pp, file));
        }
    }
    else if (result)
    {
        // Made in scratch memory, which only pp_virtual_file's copy of the text outlives.
        Scratch scratch = scratch_begin(&pp->arena);
        char* text = arena_push(scratch.arena, BUSTER_PP_PREDEFINES_BYTES, 1);
        const uint32_t length = text != 0 ? pp_predefines(&pp->options, text, &pp->flt_eval_method) : BUSTER_PP_PREDEFINES_BYTES + 1;
        const uint32_t file = length <= BUSTER_PP_PREDEFINES_BYTES ? pp_virtual_file(pp, "<built-in>", text, length, BUSTER_PP_SYSTEM_FILE) : BUSTER_PP_NONE;
        scratch_end(scratch);
        result = file != BUSTER_PP_NONE;
        if (result)
        {
            pp->predefines_data = pp->files[file].data;
            pp_run_file(pp, file);
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't make the predefined macros for these options");
        }
    }
    // The command line: -municode's UNICODE, then -D and -U in order, then -include. Each line is at most its option's
    // bytes and 16 more, and the file's length must fit pp_virtual_file's 32 bits.
    uint64_t room = 32;
    bool fits = true;
    for (uint32_t i = 0; i < pp->options.macro_count; i += 1)
    {
        fits &= !__builtin_add_overflow(room, (uint64_t)strlen(pp->options.macros[i]) + 16, &room);
    }
    for (uint32_t i = 0; i < pp->options.include_count; i += 1)
    {
        fits &= !__builtin_add_overflow(room, (uint64_t)strlen(pp->options.includes[i]) + 16, &room);
    }
    fits &= room <= UINT32_MAX;
    result = result && !pp->fatal;
    char* command_line = result && fits ? arena_push(&pp->arena, room, 1) : 0;
    if (result && command_line == 0)
    {
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, fits ? "out of memory for the command line's -D, -U and -include options" : "the command line's -D, -U and -include options are too long");
        result = false;
    }
    if (result)
    {
        uint64_t length = pp->options.predefines.unicode ? (uint64_t)sprintf(command_line, "#define UNICODE 1\n") : 0;
        for (uint32_t i = 0; i < pp->options.macro_count; i += 1)
        {
            const char* macro = pp->options.macros[i];
            const uint32_t line = pp_command_line_length(macro + 1);
            // `line` lost any backslashes spliced onto the break, so the break's presence is asked of the text itself:
            // a -D ending in one has text cut but no embedded newline to warn of.
            const bool cut = strpbrk(macro + 1, "\n\r") != NULL;
            const char* equals = memchr(macro + 1, '=', line);
            if (cut)
            {
                const uint32_t name = equals != NULL ? (uint32_t)(equals - (macro + 1)) : line;
                // A warning like the preprocessor's others: -w silences it, -Werror promotes it (negative_conversion_error
                // is pp_parse_options' -Werror). ALWAYS: the command line is no system header, and location 0 would read as
                // one to the visibility gate.
                const PPSeverity severity = pp->options.negative_conversion_error && !pp->options.suppress_warnings ? PP_SEVERITY_ERROR : PP_SEVERITY_WARNING_ALWAYS;
                pp_diagnose(pp, severity, 0, "macro '%.*s' contains embedded newline; text after the newline is ignored", (int)name, macro + 1);
            }
            if (macro[0] == 'U')
            {
                length += (uint64_t)sprintf(command_line + length, "#undef %.*s\n", (int)line, macro + 1);
            }
            else if (equals != NULL)
            {
                const uint32_t name = (uint32_t)(equals - (macro + 1));
                const uint32_t body = pp_command_line_length(equals + 1);
                length += (uint64_t)sprintf(command_line + length, "#define %.*s %.*s\n", (int)name, macro + 1, (int)body, equals + 1);
            }
            else
            {
                length += (uint64_t)sprintf(command_line + length, "#define %.*s 1\n", (int)line, macro + 1);
            }
        }
        const uint32_t file = pp_virtual_file(pp, "<command line>", command_line, (uint32_t)length, BUSTER_PP_USER_FILE);
        result = file != BUSTER_PP_NONE;
        if (result)
        {
            pp_run_file(pp, file);
            result = !pp->fatal;
        }
        else
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't make the file of the command line's -D and -U options: %s", pp_virtual_file_error(pp));
        }
    }
    // The -include lines, in a file of their own named as clang's: its forced includes are in its <built-in> buffer,
    // after the command line's macros, which __BASE_FILE__ in a forced header gives. Without -include there is no file.
    if (result && pp->options.include_count != 0)
    {
        bool raw = false;
        for (uint32_t i = 0; !raw && i < pp->options.include_count; i += 1)
        {
            raw = !pp_command_include_safe(pp->options.includes[i]);
        }
        if (!raw)
        {
            uint64_t length = 0;
            for (uint32_t i = 0; i < pp->options.include_count; i += 1)
            {
                length += (uint64_t)sprintf(command_line + length, "#include \"%s\"\n", pp->options.includes[i]);
            }
            const uint32_t includes = pp_virtual_file(pp, "<built-in>", command_line, (uint32_t)length, BUSTER_PP_USER_FILE);
            result = includes != BUSTER_PP_NONE;
            if (result)
            {
                pp_run_file(pp, includes);
                result = !pp->fatal;
            }
            else
            {
                pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't make the file of the command line's -include options: %s", pp_virtual_file_error(pp));
            }
        }
        else
        {
            for (uint32_t i = 0; result && i < pp->options.include_count; i += 1)
            {
                const char* path = pp->options.includes[i];
                if (pp_command_include_safe(path))
                {
                    const uint64_t length = (uint64_t)sprintf(command_line, "#include \"%s\"\n", path);
                    const uint32_t includes = pp_virtual_file(pp, "<built-in>", command_line, (uint32_t)length, BUSTER_PP_USER_FILE);
                    result = includes != BUSTER_PP_NONE;
                    if (result)
                    {
                        pp_run_file(pp, includes);
                        result = !pp->fatal;
                    }
                    else
                    {
                        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't make the file of the command line's -include options: %s", pp_virtual_file_error(pp));
                    }
                }
                else
                {
                    result = pp_command_include_raw(pp, path);
                }
            }
        }
    }
    return result;
}

static bool pp_run_main(Preprocessor* pp, uint32_t file)
{
    pp->main_file = file;
    pp->include_depth = 0;
    pp->showing_includes = pp->options.show_includes && !pp->options.quiet;
    pp_run_file(pp, file);
    pp_resolve_keywords(pp);
    return pp->errors == 0 && !pp->fatal;
}

// Preprocesses the main file: the prelude (pp_run_prelude), then the file. The output is pp->output, with keywords'
// kinds. Returns false if there were errors. pp_run's work, which a deferred history's replay does again
// (pp_history_record).
static bool pp_preprocess_file(Preprocessor* pp, const char* main_path)
{
    bool result = pp_run_prelude(pp);
    const uint32_t file = result && !pp->fatal ? pp_lookup_path(pp, main_path, (uint32_t)strlen(main_path), BUSTER_PP_NONE, 0, BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
    if (file != BUSTER_PP_NONE && pp->files[file].data != BUSTER_PP_NONE)
    {
        result = pp_run_main(pp, file);
    }
    else
    {
        if (!pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "can't read '%s': %s", main_path, pp_read_error(pp, file));
        }
        pp_resolve_keywords(pp);
        result = false;
    }
    return result;
}

// pp_preprocess_file for a main file in memory, `length` bytes at `bytes`, named `name`: pp_run_source's work.
static bool pp_preprocess_source(Preprocessor* pp, const char* name, const uint8_t* bytes, uint32_t length)
{
    bool result = pp_run_prelude(pp);
    const uint32_t file = result && !pp->fatal ? pp_virtual_file(pp, name, (const char*)bytes, length, BUSTER_PP_USER_FILE) : BUSTER_PP_NONE;
    if (file != BUSTER_PP_NONE)
    {
        result = pp_run_main(pp, file);
    }
    else
    {
        if (!pp->fatal)
        {
            pp_diagnose(pp, PP_SEVERITY_FATAL, 0, "out of memory for %s", name);
        }
        pp_resolve_keywords(pp);
        result = false;
    }
    return result;
}

// Whether two streams are the same, token for token.
static bool pp_tokens_same(const PPTokens* a, const PPTokens* b)
{
    return a->count == b->count && memcmp(a->kinds, b->kinds, a->count) == 0 && memcmp(a->flags, b->flags, a->count) == 0 &&
           memcmp(a->values, b->values, (uint64_t)a->count * sizeof(uint32_t)) == 0 && memcmp(a->locations, b->locations, (uint64_t)a->count * sizeof(uint32_t)) == 0;
}

// The semantic event timeline is part of replay identity even for expansions that left no output token. Origins
// are intentionally excluded: the deferred first pass has none, and the replay supplies their exact history.
static bool pp_eval_method_uses_same(const Preprocessor* a, const Preprocessor* b)
{
    const uint32_t count = a->eval_method_use_count;
    bool same = count == b->eval_method_use_count;
    for (uint32_t i = 0; same && i < count; i += 1)
    {
        const PPEvalMethodUse* left = a->eval_method_uses + i;
        const PPEvalMethodUse* right = b->eval_method_uses + i;
        same = left->position == right->position && left->pragmas == right->pragmas && left->location == right->location;
    }
    return same;
}

// A released output (pp_release_output) taken from `replay`, whose output is the unit's again: its columns move to the
// unit, and the replay keeps none. Nothing for an output that wasn't released.
static void pp_output_adopt(Preprocessor* pp, Preprocessor* replay)
{
    if (pp->output_released)
    {
        // A poisoned output's pages are still mapped, unreadable (pp_release_output): they go now.
        arena_release(&pp->output.columns);
        pp->output.columns = replay->output.columns;
        pp->output.kinds = replay->output.kinds;
        pp->output.flags = replay->output.flags;
        pp->output.values = replay->output.values;
        pp->output.locations = replay->output.locations;
        pp->output.capacity = replay->output.capacity;
        replay->output.columns = (Arena){ 0 };
        replay->output.kinds = NULL;
        replay->output.flags = NULL;
        replay->output.values = NULL;
        replay->output.locations = NULL;
        replay->output.count = 0;
        replay->output.capacity = 0;
        pp->output_released = false;
    }
}

// A released output whose replay failed: columns of zeros for its count, which place each token at no location, so that
// the diagnostics after the failure's are unlocated rather than reads of columns that are gone. Nothing for an output
// that wasn't released; a count that can't have its columns leaves an empty output.
static void pp_output_restore_empty(Preprocessor* pp)
{
    if (pp->output_released)
    {
        const uint32_t count = pp->output.count;
        arena_release(&pp->output.columns);
        if (!pp_tokens_reserve(&pp->output, count))
        {
            pp->output = (PPTokens){ 0 };
        }
        pp->output.count = pp->output.kinds != NULL ? count : 0;
        pp->output_released = false;
    }
}

// Records a deferred history (PPOptions.defer_history): preprocesses the unit again as pp_run or pp_run_source ran it,
// with history, into a preprocessor of its own (in an arena of its own) with the same options, file cache, intern table
// and time, which prints the diagnostics if `report` says so, and takes its history, which the replay keeps until
// pp_release. The replay must be the run again: its result, its errors, warnings and fatal error, and its output, token
// for token. One that isn't (it ran out of memory for its history, say, which an empty output wouldn't show), or can't
// be made, leaves the unit without history, and a fatal error says so.
static void pp_history_record(Preprocessor* pp, bool report)
{
    pp->history_deferred = false;
    const bool ran = pp->history_path != 0 || pp->history_name != 0;
    pp->history_replay_arena = ran ? arena_reserve(sizeof(Preprocessor) + 64) : (Arena){ 0 };
    Preprocessor* replay = arena_push(&pp->history_replay_arena, sizeof(Preprocessor), _Alignof(Preprocessor));
    PPOptions options = pp->options;
    options.no_history = false;
    options.defer_history = false;
    options.quiet = !report;
    options.show_includes = false;
    options.timing = false;
    options.files = pp->cache;
    options.dependencies.enabled = false;
    const bool initialized = replay != 0 && pp_initialize(replay, &options, pp->table);
    bool same = initialized;
    if (initialized)
    {
        // The unit's time, which __DATE__, __TIME__ and __TIMESTAMP__ give.
        memcpy(replay->date, pp->date, sizeof(pp->date));
        memcpy(replay->time, pp->time, sizeof(pp->time));
        memcpy(replay->timestamp, pp->timestamp, sizeof(pp->timestamp));
        replay->has_epoch = pp->has_epoch;
        replay->unit_seconds = pp->unit_seconds;
        const bool result = pp->history_path != 0 ? pp_preprocess_file(replay, pp->history_path) :
                                                    pp_preprocess_source(replay, pp->history_name, pp->history_bytes, pp->history_length);
        same = result == pp->history_result && replay->fatal == pp->history_fatal && replay->errors == pp->history_errors &&
               (pp->history_warning_filter_deferred || replay->warnings == pp->history_warnings) &&
               (pp->output_released ? replay->output.count == pp->output.count : pp_tokens_same(&replay->output, &pp->output)) &&
               pp_eval_method_uses_same(pp, replay);
    }
    if (same)
    {
        // An inexact run cannot classify a warning from an expansion that spans a system-level transition. The
        // first pass counted it provisionally; the replay has exact expansion locations and supplies its count.
        if (pp->history_warning_filter_deferred)
        {
            pp->warnings = pp->warnings - pp->history_warnings + replay->warnings;
            pp->history_warnings = replay->warnings;
        }
        pp->history = replay->history;
        pp->history_count = replay->history_count;
        pp->history_capacity = replay->history_capacity;
        pp->substitutions = replay->substitutions;
        pp->substitution_count = replay->substitution_count;
        pp->substitution_capacity = replay->substitution_capacity;
        pp->generated = replay->generated;
        pp->generated_count = replay->generated_count;
        pp->generated_capacity = replay->generated_capacity;
        pp->history_sources = replay->history_sources;
        pp->history_source_count = replay->history_source_count;
        pp->history_source_capacity = replay->history_source_capacity;
        pp->history_runs = replay->history_runs;
        pp->history_run_count = replay->history_run_count;
        pp->history_run_capacity = replay->history_run_capacity;
        pp->eval_method_uses = replay->eval_method_uses;
        pp->eval_method_use_count = replay->eval_method_use_count;
        pp->eval_method_use_capacity = replay->eval_method_use_capacity;
        // The include stack the replay's diagnostics printed last, as the unit's own run would have left it: a later
        // diagnostic prints "In file included from" only for another one (pp_diagnostic_includes).
        pp->diagnostic_include_range = replay->diagnostic_include_range;
        pp->history_replay = replay;
        if (pp->options.collect_diagnostics)
        {
            pp_diagnostic_take(pp, replay);
        }
        // A released output (pp_release_output) is the replay's: its count is the unit's, and the replay is the run again.
        pp_output_adopt(pp, replay);
        // The replay's output is the unit's, token for token (pp_tokens_same), and nothing reads it after that comparison:
        // its history and diagnostics name the unit's tokens, which the unit's own output holds. Its columns go now rather
        // than at pp_release, so that a unit with a diagnostic doesn't keep a second output to its end (#628: 12 MB of
        // sqlite3.c's, in the backend's peak). The final output has no origin column: what the diagnostics render through
        // stays, the replay's history runs, ranges, files, substitutions and generated tokens.
        arena_release(&replay->output.columns);
        replay->output.kinds = NULL;
        replay->output.flags = NULL;
        replay->output.values = NULL;
        replay->output.locations = NULL;
        replay->output.count = 0;
        replay->output.capacity = 0;
    }
    else
    {
        // A reporting replay replaces the first pass's silent diagnostics. Keep only the groups it actually
        // published. Its own fatal is already final; otherwise allow the replacement fatal even when the first
        // pass had already stopped silently.
        const bool published_fatal = report && replay != 0 && replay->fatal;
        // Collected replay groups still own their origin tables. Keep that rejected replay alive for publication,
        // but never expose it as successful history to source checking. A silent failed replay is replaced as before.
        const bool retained = pp->options.collect_diagnostics && report && replay != NULL && replay->diagnostic_first != NULL;
        pp->diagnostic_collection_failed |= pp->options.collect_diagnostics && replay != NULL && replay->diagnostic_collection_failed;
        const uint32_t replacement_position = retained && replay->output.count > pp->output.count ? replay->output.count : pp->output.count;
        pp->history_failed = true;
        pp_output_restore_empty(pp);
        if (report)
        {
            pp->errors = replay != 0 ? replay->errors : 0;
            pp->warnings = replay != 0 ? replay->warnings : 0;
        }
        if (retained)
        {
            pp->history_replay = replay;
            pp_diagnostic_take(pp, replay);
        }
        else if (replay != 0)
        {
            pp_release_unit(replay);
        }
        if (!retained)
        {
            arena_release(&pp->history_replay_arena);
        }
        pp->fatal = published_fatal;
        const uint32_t saved_position = pp->diagnostic_position;
        pp->diagnostic_position = replacement_position;
        pp_diagnose(pp, PP_SEVERITY_FATAL, 0, !ran         ? "no expansion history: the translation unit wasn't run by pp_run or pp_run_source" :
                                              !initialized ? "can't preprocess the translation unit again for its expansion history (out of memory)" :
                                                             "the translation unit gave another result when preprocessed again for its expansion history");
        pp->diagnostic_position = saved_position;
        pp->fatal = true;
    }
}

// The end of a run, with its result: its outcome is kept for a replay to match (pp_history_record). Then, for a deferred
// run (PPOptions.defer_history), the diagnostics after it are printed as the options ask, and its own, which it counted
// without printing them, are printed with their notes by recording the history, as the unit's own run would have
// printed them.
static void pp_history_report(Preprocessor* pp, bool result)
{
    pp->history_result = result;
    pp->history_fatal = pp->fatal;
    pp->history_errors = pp->errors;
    pp->history_warnings = pp->warnings;
    if (pp->history_deferred)
    {
        pp->options.quiet = pp->history_quiet;
        if (((!pp->history_quiet || pp->options.collect_diagnostics) && (pp->errors != 0 || pp->warnings != 0)) || pp->history_warning_filter_deferred)
        {
            pp_history_record(pp, !pp->history_quiet);
        }
    }
}

BUSTER_PREPROCESSOR_ENTRY bool pp_history_ensure(Preprocessor* pp)
{
    if (pp->history_deferred)
    {
        pp_history_record(pp, false);
    }
    return pp->history != 0;
}

// A file's prepared token columns forgotten (pp_release_file_columns): the kinds, starts, values and leading-space
// words, which are in the columns' arenas, and with `whole`, a cache record's, everything else preparing gives it, so
// that preparing it again (file_cache_prepare) starts from a record as the cache made it.
static void pp_forget_prepared(PPFileData* data, bool whole)
{
    data->kinds = NULL;
    data->starts = NULL;
    data->values = NULL;
    data->space = NULL;
    data->prepared = false;
    if (whole)
    {
        data->token_count = 0;
        data->raw_errors = NULL;
        data->raw_error_count = 0;
        data->directive_count = 0;
        data->directive_hash = NULL;
        data->directive_end = NULL;
        data->directive_kind = NULL;
        data->directive_next = NULL;
        data->note_count = 0;
        data->note_directives = NULL;
        data->note_kinds = NULL;
        data->guard = 0;
        data->unterminated_comment = 0;
        data->unterminated_comment_starts_line = false;
        data->guard_opener = 0;
        data->line_pages = NULL;
    }
}

BUSTER_PREPROCESSOR_ENTRY void pp_release_file_columns(Preprocessor* pp, bool poison)
{
    if (pp->owns_cache && pp->history_replay == NULL)
    {
        FileCache* cache = pp->cache;
        for (uint32_t r = 0; r < cache->record_count; r += 1)
        {
            pp_forget_prepared(&cache->records[r], true);
        }
        for (uint32_t d = 0; d < pp->data_count; d += 1)
        {
            if (pp->datas[d].record != BUSTER_PP_NONE)
            {
                pp_forget_prepared(&pp->datas[d], false);
            }
        }
        // The lanes whose arenas were reserved alone (FileCache.lanes_reserved): the others' are zero, and on pages a unit
        // on one lane never touches.
        for (uint32_t lane = 0; lane < BUSTER_LANE_MAX_COUNT; lane += 1)
        {
            const bool reserved = (cache->lanes_reserved[lane / 64] >> (lane % 64) & 1) != 0;
            for (uint32_t i = 0; reserved && i < BUSTER_ARRAY_LENGTH(cache->lanes[lane].columns); i += 1)
            {
                Arena* columns = &cache->lanes[lane].columns[i];
                if (poison && columns->base != NULL && columns->mapping_bytes != 0 && columns->peak != 0)
                {
                    // Whole pages from the arena's start, which is a page's, to its peak's; the arena is then never pooled
                    // (arena_release unmaps an arena whose writable extent it doesn't know).
                    const uint64_t end = (columns->peak + BUSTER_ARENA_PAGE_BYTES - 1) & ~(uint64_t)(BUSTER_ARENA_PAGE_BYTES - 1);
                    if (mprotect(columns->base, end, PROT_NONE) == 0)
                    {
                        columns->length = end;
                        columns->peak = end;
                        columns->unaccounted = true;
                    }
                }
                else
                {
                    arena_pop_to(columns, 0);
                    arena_discard(columns);
                }
            }
        }
    }
}

BUSTER_PREPROCESSOR_ENTRY void pp_release_output(Preprocessor* pp, bool poison)
{
    Arena* columns = &pp->output.columns;
    if (pp->history_deferred && !pp->output_released && columns->base != NULL)
    {
        pp->output_released = true;
        if (poison && columns->mapping_bytes != 0 && columns->committed != 0 &&
            mprotect(columns->base - columns->mapping_offset, columns->committed, PROT_NONE) == 0)
        {
            // The arena is then never pooled: arena_release unmaps an arena whose writable extent it doesn't know.
            columns->unaccounted = true;
        }
        else
        {
            arena_release(columns);
            pp->output.kinds = NULL;
            pp->output.flags = NULL;
            pp->output.values = NULL;
            pp->output.locations = NULL;
            pp->output.capacity = 0;
        }
    }
}

BUSTER_PREPROCESSOR_ENTRY bool pp_output_ensure(Preprocessor* pp)
{
    bool result = !pp->output_released;
    if (!result)
    {
        Arena arena = pp->history_path != 0 || pp->history_name != 0 ? arena_reserve(sizeof(Preprocessor) + 64) : (Arena){ 0 };
        Preprocessor* replay = arena_push(&arena, sizeof(Preprocessor), _Alignof(Preprocessor));
        PPOptions options = pp->options;
        options.no_history = true;
        options.defer_history = false;
        options.quiet = true;
        options.show_includes = false;
        options.collect_diagnostics = false;
        options.timing = false;
        options.files = pp->cache;
        options.dependencies.enabled = false;
        const bool initialized = replay != 0 && pp_initialize(replay, &options, pp->table);
        if (initialized)
        {
            // The unit's time, which __DATE__, __TIME__ and __TIMESTAMP__ give.
            memcpy(replay->date, pp->date, sizeof(pp->date));
            memcpy(replay->time, pp->time, sizeof(pp->time));
            memcpy(replay->timestamp, pp->timestamp, sizeof(pp->timestamp));
            replay->has_epoch = pp->has_epoch;
            replay->unit_seconds = pp->unit_seconds;
            const bool ran = pp->history_path != 0 ? pp_preprocess_file(replay, pp->history_path) :
                                                     pp_preprocess_source(replay, pp->history_name, pp->history_bytes, pp->history_length);
            result = ran == pp->history_result && replay->fatal == pp->history_fatal && replay->errors == pp->history_errors &&
                     replay->output.count == pp->output.count;
            if (result)
            {
                pp_output_adopt(pp, replay);
            }
            pp_release_unit(replay);
        }
        arena_release(&arena);
        pp_output_restore_empty(pp);
    }
    return result;
}

BUSTER_PREPROCESSOR_ENTRY void pp_diagnose_eval_method_use(Preprocessor* pp, uint32_t use, uint32_t position, uint32_t pragma_source)
{
    pp_history_ensure(pp);
    if (use < pp->eval_method_use_count && !pp->fatal)
    {
        const PPEvalMethodUse record = pp->eval_method_uses[use];
        const PPOutputDiagnosticNote note = { .token = pragma_source, .message = "#pragma entered here" };
        pp_diagnose_source_notes_phase(pp, PP_SEVERITY_ERROR, record.location, record.origin, position, PP_DIAGNOSTIC_LEXICAL, &note, 1,
                                      "'__FLT_EVAL_METHOD__' cannot be expanded inside a scope containing '#pragma clang fp eval_method'");
    }
}

BUSTER_PREPROCESSOR_ENTRY bool pp_run(Preprocessor* pp, const char* main_path)
{
    pp->history_path = main_path;
    // A deferred run counts its diagnostics without printing them, which its end does (pp_history_report).
    pp->options.quiet |= pp->history_deferred;
    const bool result = pp_preprocess_file(pp, main_path);
    pp_history_report(pp, result);
    return result && !pp->fatal;
}

BUSTER_PREPROCESSOR_ENTRY bool pp_run_source(Preprocessor* pp, const char* name, const uint8_t* bytes, uint32_t length)
{
    pp->history_name = name;
    pp->history_bytes = bytes;
    pp->history_length = length;
    pp->options.quiet |= pp->history_deferred;
    const bool result = pp_preprocess_source(pp, name, bytes, length);
    pp_history_report(pp, result);
    return result && !pp->fatal;
}

// The command's cached bytes for `path`, or null when this input is an ordinary file.
static const PPStreamInput* pp_cached_stream(const Preprocessor* pp, const char* path)
{
    const PPStreamInput* result = NULL;
    for (uint32_t i = 0; result == NULL && i < pp->options.stream_count; i += 1)
    {
        if (strcmp(pp->options.streams[i].path, path) == 0)
        {
            result = pp->options.streams + i;
        }
    }
    return result;
}

BUSTER_PREPROCESSOR_ENTRY bool pp_run_input(Preprocessor* pp, const char* path)
{
    bool result = false;
    const bool input_stdin = strcmp(path, "-") == 0;
    const PPStreamInput* cached = input_stdin ? NULL : pp_cached_stream(pp, path);
    if (input_stdin)
    {
        result = pp->options.stdin_ready && pp_run_source(pp, "<stdin>", (const uint8_t*)pp->options.stdin_source.pointer, (uint32_t)pp->options.stdin_source.length);
    }
    else if (cached != NULL)
    {
        result = cached->bytes.length <= UINT32_MAX &&
                 pp_run_source(pp, path, (const uint8_t*)cached->bytes.pointer, (uint32_t)cached->bytes.length);
    }
    else
    {
        String8 bytes = { 0 };
        const int kind = driver_read_nonregular(&pp->arena, path, &bytes);
        if (kind > 0)
        {
            result = bytes.length <= UINT32_MAX && pp_run_source(pp, path, (const uint8_t*)bytes.pointer, (uint32_t)bytes.length);
        }
        else if (kind == 0)
        {
            result = pp_run(pp, path);
        }
    }
    return result;
}

BUSTER_PREPROCESSOR_ENTRY bool pp_dependencies(Preprocessor* pp, const char* source, const char* primary, Arena* arena, DependencyOutput* output, FILE* errors)
{
    if (pp->options.dependencies.enabled && pp->main_file < pp->file_count)
    {
        const uint32_t index = pp->datas[pp->files[pp->main_file].data].dependency;
        if (index != 0)
        {
            pp->dependencies[index - 1].used = false;
        }
    }
    return dependency_prepare(output, &pp->options.dependencies, pp->dependencies, pp->dependency_count, source, primary, pp->options.scalar, arena, errors);
}

// The deferred first run has no expansion graph. Its exact source-level runs already record where its current
// expansion was read; an inexact run instead requests canonical warning classification during the usual replay.
static bool pp_diagnostic_warning_visible(Preprocessor* pp, uint32_t location, uint32_t origin)
{
    bool visible = pp->options.system_header_warnings;
    if (!visible && pp->history_deferred)
    {
        pp->history_warning_filter_deferred |= pp->system_runs_inexact;
        visible = pp->system_runs_inexact || pp->system_run_count == 0 || pp->system_runs[pp->system_run_count - 1].level == BUSTER_PP_USER_FILE;
    }
    else if (!visible)
    {
        visible = pp_system_level_at(pp, pp_expansion_place(pp, location, origin)) == BUSTER_PP_USER_FILE;
    }
    return visible;
}

// Gives each identifier of output tokens [begin, end) that is a keyword in the language mode its keyword's kind, 64 tokens
// at a time: one compare finds the identifiers, and for each 16, one compare their ids below the table's size, and two
// vpermi2b look their kinds up in pp->keyword_kinds's halves, one picked by bit 7 of the id.
static void pp_resolve_keywords_range(Preprocessor* pp, uint32_t begin, uint32_t end)
{
    PPTokens* out = &pp->output;
    const __m512i table0 = _mm512_loadu_si512(pp->keyword_kinds);
    const __m512i table1 = _mm512_loadu_si512(pp->keyword_kinds + 64);
    const __m512i table2 = _mm512_loadu_si512(pp->keyword_kinds + 128);
    const __m512i table3 = _mm512_loadu_si512(pp->keyword_kinds + 192);
    for (uint32_t i = begin; i < end; i += 64)
    {
        const __mmask64 valid = end - i >= 64 ? ~0ull : _bzhi_u64(~0ull, end - i);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, out->kinds + i);
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER));
        if (identifiers != 0)
        {
            __m512i looked = _mm512_setzero_si512();
            uint64_t small = 0;
            for (uint32_t group = 0; group < 4; group += 1)
            {
                const __mmask16 lanes = (__mmask16)(identifiers >> (group * 16));
                const __m512i values = _mm512_maskz_loadu_epi32(lanes, out->values + i + group * 16);
                const __mmask16 keywords = _mm512_mask_cmplt_epu32_mask(lanes, values, _mm512_set1_epi32(BUSTER_PP_KEYWORD_TABLE));
                const __m128i indices = _mm512_cvtepi32_epi8(values);
                const __m512i wide = _mm512_zextsi128_si512(indices);
                const __m128i lower = _mm512_castsi512_si128(_mm512_permutex2var_epi8(table0, wide, table1));
                const __m128i upper = _mm512_castsi512_si128(_mm512_permutex2var_epi8(table2, wide, table3));
                const __m128i kinds_here = _mm_mask_blend_epi8(_mm_movepi8_mask(indices), lower, upper);
                looked = group == 0 ? _mm512_inserti32x4(looked, kinds_here, 0)
                       : group == 1 ? _mm512_inserti32x4(looked, kinds_here, 1)
                       : group == 2 ? _mm512_inserti32x4(looked, kinds_here, 2)
                                    : _mm512_inserti32x4(looked, kinds_here, 3);
                small |= (uint64_t)keywords << (group * 16);
            }
            _mm512_mask_storeu_epi8(out->kinds + i, small, looked);
        }
    }
}

static __attribute__((unused)) void pp_resolve_keywords_vector(Preprocessor* pp)
{
#if !BUSTER_HOST_VECTOR
    pp_resolve_keywords_scalar(pp);
#else
    pp_resolve_keywords_range(pp, 0, pp->output.count);
#endif
}

static void pp_resolve_keywords_scalar(Preprocessor* pp)
{
    PPTokens* out = &pp->output;
    for (uint32_t i = 0; i < out->count; i += 1)
    {
        if (out->kinds[i] == C_TOKEN_RAW_IDENTIFIER && out->values[i] < BUSTER_PP_KEYWORD_TABLE)
        {
            out->kinds[i] = pp->keyword_kinds[out->values[i]];
        }
    }
}

// The output's keywords their kinds: the tokens the vectorized kernels' chunks left (pp_run_contexts), or all of them.
static void pp_resolve_keywords(Preprocessor* pp)
{
#if !BUSTER_HOST_VECTOR
    pp_resolve_keywords_scalar(pp);
#else
    if (BUSTER_VECTOR_PATH(pp->vectorized))
    {
        pp_resolve_keywords_range(pp, pp->keywords_resolved, pp->output.count);
        pp->keywords_resolved = pp->output.count;
    }
    else
    {
        pp_resolve_keywords_scalar(pp);
    }
#endif
}

// The characters that can join a punctuator before them into a longer one, or start a comment after '/'.
static bool pp_punctuation_joins(uint8_t before, uint8_t after)
{
    static const char joining[] = "+-*/%<>=!&|^#.:?";
    return before != 0 && after != 0 && strchr(joining, before) != 0 && strchr(joining, after) != 0;
}

// Would printing `after` right after `before` (their last and first characters) lex differently? Then a space goes
// between them. It errs towards a space: -E's output only has to lex into the same tokens.
static bool pp_needs_separation(uint8_t before_last, uint8_t before_kind, uint8_t after_first, bool dollar_punctuation)
{
    bool result = false;
    if (before_kind == C_TOKEN_NUMERIC_CONSTANT)
    {
        // A pp-number goes on with word characters, '.', a digit separator and an exponent's sign.
        result = pp_word_character(after_first, dollar_punctuation) || after_first == '.' || after_first == '\'' || after_first == '+' || after_first == '-';
    }
    else if (pp_word_character(before_last, dollar_punctuation))
    {
        // An identifier goes on with word characters, and a prefix makes a literal with a quote.
        result = pp_word_character(after_first, dollar_punctuation) || after_first == '"' || after_first == '\'';
    }
    else if (before_last == '.')
    {
        result = (after_first >= '0' && after_first <= '9') || pp_punctuation_joins(before_last, after_first);
    }
    else
    {
        result = pp_punctuation_joins(before_last, after_first);
    }
    return result;
}

// The markers' state before the first token: the stack is the main file's, at its first line, which the text gives
// (`# 1 "main.c"`, as clang's first line is), or nothing without a main file.
static bool pp_markers_initialize(const Preprocessor* pp, PPMarkers* markers, Arena* arena)
{
    *markers = (PPMarkers){
        .range = BUSTER_PP_NONE,
        .root = BUSTER_PP_NONE,
        .cursors = arena_push_array(arena, (uint64_t)pp->range_count + 1, sizeof(uint32_t), 4),
        .arena = arena,
        .include_exit = pp->include_exits,
        .capacity = 4096,
        .text = arena_push(arena, 4096, 1),
    };
    for (uint32_t r = 0; markers->cursors != 0 && r <= pp->range_count; r += 1)
    {
        markers->cursors[r] = BUSTER_PP_NONE;
    }
    for (uint32_t r = 0; markers->root == BUSTER_PP_NONE && r < pp->range_count; r += 1)
    {
        markers->root = pp->ranges[r].parent == BUSTER_PP_NONE && pp->ranges[r].file == pp->main_file ? r : BUSTER_PP_NONE;
    }
    return markers->cursors != 0 && markers->text != 0;
}

// A range's parent on the stack: its includer, or the main file's range for another root (the command line's and the
// forced includes' files).
static uint32_t pp_print_parent(const Preprocessor* pp, const PPMarkers* markers, uint32_t range)
{
    const uint32_t parent = pp->ranges[range].parent;
    return parent != BUSTER_PP_NONE || range == markers->root ? parent : markers->root;
}

static uint32_t pp_print_depth(const Preprocessor* pp, const PPMarkers* markers, uint32_t range)
{
    uint32_t result = 0;
    for (uint32_t at = pp_print_parent(pp, markers, range); at != BUSTER_PP_NONE; at = pp_print_parent(pp, markers, at))
    {
        result += 1;
    }
    return result;
}

// The presumed position of `location` in `range`, at physical line `physical`: by the range's latest line note at or
// before it, found from the range's cursor (going forward, as the printer mostly asks, else from the range's first).
static PPPresumed pp_print_presumed(const Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical)
{
    const PPRange* record = &pp->ranges[range];
    uint32_t at = markers->cursors[range];
    at = at != BUSTER_PP_NONE && pp->line_directives[at].location > location ? BUSTER_PP_NONE : at;
    uint32_t next = at == BUSTER_PP_NONE ? record->first_line_directive : pp->line_directives[at].next_in_range;
    while (next != BUSTER_PP_NONE && pp->line_directives[next].location <= location)
    {
        at = next;
        next = pp->line_directives[next].next_in_range;
    }
    markers->cursors[range] = at;
    const PPFile* file = &pp->files[record->file];
    const PPLineDirective* note = at != BUSTER_PP_NONE ? &pp->line_directives[at] : 0;
    const bool renamed = note != 0 && note->name != 0;
    const int64_t delta = note != 0 ? note->delta : 0;
    return (PPPresumed){
        .line = (uint32_t)((int64_t)physical + delta),
        .delta = delta,
        .name = renamed ? note->name : file->path,
        .name_length = renamed ? note->name_length : file->path_length,
        .system = note != 0 ? note->system : record->system,
    };
}

// The presumed position where a range includes another (`child`): the line after the directive when `after`, for a
// marker that goes back to it.
static PPPresumed pp_print_includer(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t child, bool after)
{
    const uint32_t location = pp->ranges[child].include_location;
    PPPresumed result = pp_print_presumed(pp, markers, range, location, pp_line_of(pp, range, location, 0));
    result.line += after;
    return result;
}

// Adds a line marker to the step's text: `# line "name"`, the flags, the system level's (3, or 3 4) and a line break,
// with a break first when something is on the output's current line. The name is escaped as clang writes it
// (raw_ostream::write_escaped): \\, \", \t and \n, and a byte outside ' ' to '~' as three octal digits.
static bool pp_print_marker_line(PPMarkers* markers, bool* line_start, uint32_t line, const char* name, uint32_t name_length, const char* flags, uint8_t system)
{
    const uint64_t need = markers->length + 4 * (uint64_t)name_length + 64;
    if (need > markers->capacity)
    {
        char* text = arena_push(markers->arena, 2 * need, 1);
        if (text != 0)
        {
            memcpy(text, markers->text, markers->length);
            markers->text = text;
            markers->capacity = 2 * need;
        }
    }
    const bool result = need <= markers->capacity;
    if (result)
    {
        char* out = markers->text + markers->length;
        uint64_t n = 0;
        out[n] = '\n';
        n += !*line_start;
        n += (uint64_t)sprintf(out + n, "# %u \"", line);
        for (uint32_t i = 0; i < name_length; i += 1)
        {
            const uint8_t c = (uint8_t)name[i];
            const char escape = c == '\\' ? '\\' : c == '"' ? '"' : c == '\t' ? 't' : c == '\n' ? 'n' : 0;
            if (escape != 0)
            {
                out[n] = '\\';
                out[n + 1] = escape;
                n += 2;
            }
            else if (c >= ' ' && c <= '~')
            {
                out[n] = (char)c;
                n += 1;
            }
            else
            {
                out[n] = '\\';
                out[n + 1] = (char)('0' + (c >> 6));
                out[n + 2] = (char)('0' + ((c >> 3) & 7));
                out[n + 3] = (char)('0' + (c & 7));
                n += 4;
            }
        }
        n += (uint64_t)sprintf(out + n, "\"%s%s\n", flags, system == BUSTER_PP_EXTERN_C_FILE ? " 3 4" : system == BUSTER_PP_SYSTEM_FILE ? " 3" : "");
        markers->length += n;
        *line_start = true;
    }
    return result;
}

// The first step: `# 1 "main.c"`, the main file's marker, in markers->text.
static bool pp_print_start(const Preprocessor* pp, PPMarkers* markers)
{
    markers->length = 0;
    bool line_start = true;
    bool result = true;
    if (markers->root != BUSTER_PP_NONE)
    {
        const PPRange* root = &pp->ranges[markers->root];
        const PPFile* file = &pp->files[root->file];
        result = pp_print_marker_line(markers, &line_start, 1, file->path, file->path_length, "", root->system);
        markers->range = markers->root;
        markers->line = 1;
        markers->delta = 0;
        markers->name = file->path;
        markers->name_length = file->path_length;
        markers->system = root->system;
    }
    return result;
}

// Clang's assume_nonnull end callback supplies an invalid source location, so -E keeps its current presumed line.
// Recognize that event only in the cold pragma path; the token stream's spelling and expansion locations stay intact.
static __attribute__((noinline)) bool pp_print_keeps_line(const Preprocessor* pp, uint32_t token)
{
    const PPTokens* out = &pp->output;
    bool result = out->count - token >= 6 && out->values[token + 1] == C_NAME_PRAGMA && out->kinds[token + 5] == BUSTER_PP_KIND_PRAGMA_END &&
                  out->kinds[token + 2] == C_TOKEN_RAW_IDENTIFIER && out->kinds[token + 3] == C_TOKEN_RAW_IDENTIFIER && out->kinds[token + 4] == C_TOKEN_RAW_IDENTIFIER;
    if (result)
    {
        const IdentId space = out->values[token + 2];
        const IdentId name = out->values[token + 3];
        const IdentId end = out->values[token + 4];
        result = intern_length(pp->table, space) == 5 && memcmp(intern_bytes(pp->table, space), "clang", 5) == 0 &&
                 intern_length(pp->table, name) == 14 && memcmp(intern_bytes(pp->table, name), "assume_nonnull", 14) == 0 &&
                 intern_length(pp->table, end) == 3 && memcmp(intern_bytes(pp->table, end), "end", 3) == 0;
    }
    return result;
}

// Where -E places a token that starts an output line: at its own location; or for one a macro made, at the start of
// the expansion it came from (the expansion starts are passed in the output's order), as clang places a token by its
// expansion location; or for a #pragma's '#', which is in no source, at the directive (its name's location). The
// printers then move it to where clang's lexer starts that token, with the line splices right before it.
static uint32_t pp_print_location(const Preprocessor* pp, PPMarkers* markers, uint32_t token)
{
    markers->token = token;
    uint32_t at = markers->expansion;
    while (at < pp->expansion_start_count && pp->expansion_starts[at].token <= token)
    {
        at += 1;
    }
    markers->expansion = at;
    const uint8_t flags = pp->output.flags[token];
    const uint32_t own = pp->output.locations[token];
    const bool pragma = pp->output.kinds[token] == C_TOKEN_HASH && own == pp->hash_location;
    const bool directive = pragma && !(flags & BUSTER_PP_FROM_MACRO);
    markers->keep_line = pragma && pp_print_keeps_line(pp, token);
    return (flags & BUSTER_PP_FROM_MACRO) && at != 0 ? pp->expansion_starts[at - 1].location : directive ? pp->output.locations[token + 1] : own;
}

// Places a token that starts an output line, at `location` in `range` (pp_print_location), at physical line
// `physical`, as clang's printer does (FileChanged, MoveToLine): going back to an includer marks each file it goes back
// to with flag 2, and going into an included file each file it goes into with flag 1; a #line, a new name or a new
// system level, a marker; a jump forward of up to 8 lines, that many line breaks; any other jump, a marker. The markers
// go to markers->text (markers->length bytes, ending in a line break); returns the line breaks to put after them, before
// the token, and false in `*written` if memory ran out. pp_print_place takes the common case inline.
static __attribute__((noinline)) uint32_t pp_print_place_marking(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical,
                                                                 bool line_start, bool file_change, bool* written)
{
    markers->length = 0;
    const uint32_t previous_note = markers->cursors[range];
    PPPresumed here = pp_print_presumed(pp, markers, range, location, physical);
    if (!file_change && pp->ranges[range].collected_exit != 0 && pp->ranges[range].collected_exit - 1 <= markers->token)
    {
        // MoveToLine takes the expansion's presumed line, but FileChanged has already selected the includer.
        range = markers->range;
        here.delta = markers->delta;
        here.name = markers->name;
        here.name_length = markers->name_length;
        here.system = markers->system;
    }
    if (markers->keep_line && range == markers->range && previous_note == markers->cursors[range] && here.name == markers->name &&
        here.delta == markers->delta && here.system == markers->system)
    {
        here.line = markers->line;
    }
    uint32_t result = 0;
    bool fits = true;
    if (range != markers->range)
    {
        // The ranges from `range` up to below the stack's common part with the top, bottom up, then the common part.
        uint32_t path[BUSTER_PP_MAX_INCLUDE_DEPTH + 8];
        uint32_t path_count = 0;
        uint32_t common = markers->range;
        uint32_t down = range;
        uint32_t common_depth = common != BUSTER_PP_NONE ? pp_print_depth(pp, markers, common) : 0;
        uint32_t down_depth = pp_print_depth(pp, markers, down);
        while (down != BUSTER_PP_NONE && down_depth > common_depth && path_count < BUSTER_ARRAY_LENGTH(path))
        {
            path[path_count] = down;
            path_count += 1;
            down = pp_print_parent(pp, markers, down);
            down_depth -= 1;
        }
        while (common != BUSTER_PP_NONE && common_depth > down_depth)
        {
            common = pp_print_parent(pp, markers, common);
            common_depth -= 1;
        }
        while (common != down && path_count < BUSTER_ARRAY_LENGTH(path))
        {
            path[path_count] = down;
            path_count += 1;
            down = pp_print_parent(pp, markers, down);
            common = pp_print_parent(pp, markers, common);
        }
        if (common == BUSTER_PP_NONE)
        {
            // No common part (no main file's range): the token's position alone.
            fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
        }
        else
        {
            for (uint32_t at = markers->range; fits && at != common; at = pp_print_parent(pp, markers, at))
            {
                const uint32_t to = pp_print_parent(pp, markers, at);
                const PPPresumed back = to == range ? here : pp_print_includer(pp, markers, to, at, true);
                fits = pp_print_marker_line(markers, &line_start, back.line, back.name, back.name_length, " 2", back.system);
            }
            for (uint32_t k = path_count; fits && k > 0; k -= 1)
            {
                const uint32_t to = path[k - 1];
                const PPPresumed into = to == range ? here : pp_print_includer(pp, markers, to, path[k - 2], false);
                fits = pp_print_marker_line(markers, &line_start, into.line, into.name, into.name_length, " 1", into.system);
            }
        }
    }
    else if (here.name != markers->name || here.delta != markers->delta || here.system != markers->system)
    {
        fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
    }
    else if (here.line > markers->line && here.line - markers->line <= 8)
    {
        result = here.line - markers->line;
    }
    else if (here.line != markers->line || !line_start)
    {
        fits = pp_print_marker_line(markers, &line_start, here.line, here.name, here.name_length, "", here.system);
    }
    markers->range = range;
    markers->line = here.line;
    markers->delta = here.delta;
    markers->name = here.name;
    markers->name_length = here.name_length;
    markers->system = here.system;
    *written = fits;
    return result;
}

// Whether a line start at `location` in `range`, at presumed line `line` (its physical line and the markers' delta), is
// pp_print_place's common case: in the same range as the line before, past the same line note (none passed since, and
// none ahead of `location`), a jump forward of 1 to 8 lines, which is that many line breaks and no marker.
BUSTER_INLINE bool pp_print_place_common(const Preprocessor* pp, const PPMarkers* markers, uint32_t range, uint32_t location, uint32_t line)
{
    const uint32_t at = markers->cursors[range];
    const uint32_t next = at == BUSTER_PP_NONE ? pp->ranges[range].first_line_directive : pp->line_directives[at].next_in_range;
    return !markers->keep_line && range == markers->range && (at == BUSTER_PP_NONE || pp->line_directives[at].location <= location) &&
           (next == BUSTER_PP_NONE || pp->line_directives[next].location > location) && line > markers->line && line - markers->line <= 8;
}

// The common case of a line start (pp_print_place_common), inline; anything else is pp_print_place_marking's.
BUSTER_INLINE uint32_t pp_print_place(Preprocessor* pp, PPMarkers* markers, uint32_t range, uint32_t location, uint32_t physical, bool line_start, bool* written)
{
    const uint32_t line = (uint32_t)((int64_t)physical + markers->delta);
    const bool same = pp_print_place_common(pp, markers, range, location, line);
    uint32_t result = line - markers->line;
    markers->length = 0;
    *written = true;
    if (same)
    {
        markers->line = line;
    }
    else
    {
        result = pp_print_place_marking(pp, markers, range, location, physical, line_start, false, written);
    }
    return result;
}

// Replay the include exits that happened before this token was returned. A header with no preceding output still
// gets its entry marker before its exit. The vector printer flushes its batch buffer before this cold stdio path.
static __attribute__((noinline)) bool pp_print_include_exits(Preprocessor* pp, PPMarkers* markers, uint32_t token, bool* line_start, FILE* stream)
{
    bool result = true;
    while (result && markers->include_exit != 0 && markers->include_exit->token <= token)
    {
        const PPIncludeExit* event = markers->include_exit;
        markers->length = 0;
        if (markers->range != event->range)
        {
            pp_print_place_marking(pp, markers, event->range, pp->ranges[event->range].base, 1, *line_start, true, &result);
            *line_start = *line_start || markers->length != 0;
        }
        const uint32_t parent = pp_print_parent(pp, markers, event->range);
        const PPPresumed back = pp_print_presumed(pp, markers, parent, event->location, pp_line_of(pp, parent, event->location, 0));
        result = result && pp_print_marker_line(markers, line_start, back.line, back.name, back.name_length, " 2", back.system);
        markers->range = parent;
        markers->line = back.line;
        markers->delta = back.delta;
        markers->name = back.name;
        markers->name_length = back.name_length;
        markers->system = back.system;
        result = result && fwrite(markers->text, 1, markers->length, stream) == markers->length;
        markers->include_exit = event->next;
    }
    return result;
}

// The line breaks in an unknown token's spelling (\r\n and \n\r count as one), which clang's printer adds to its line
// (HandleNewlinesInToken): an unknown token the lexer read as a raw string literal (its delimiter invalid) can have
// some. It counts no other token's, a raw string literal's among them.
static __attribute__((noinline)) uint32_t pp_print_token_lines(const uint8_t* spelling, uint32_t length)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < length; i += 1)
    {
        const bool line_break = spelling[i] == '\n' || spelling[i] == '\r';
        result += line_break;
        i += line_break && i + 1 < length && (spelling[i + 1] == '\n' || spelling[i + 1] == '\r') && spelling[i + 1] != spelling[i];
    }
    return result;
}

static __attribute__((noinline, cold)) uint32_t pp_print_moves(Preprocessor* pp, PPMarkers* markers, uint32_t token, uint32_t* silent)
{
    uint32_t result = BUSTER_PP_NONE;
    if (pp->output.flags[token] & BUSTER_PP_LINE_SPACE)
    {
        const uint32_t place = pp_lexed_start(pp, pp_print_location(pp, markers, token));
        const char* name;
        uint32_t length;
        const uint32_t range = pp_find_range(pp, place);
        const bool exited = range != BUSTER_PP_NONE && pp->ranges[range].collected_exit != 0 && pp->ranges[range].collected_exit - 1 <= token;
        const bool moves = (!exited && range != markers->range) || pp_presumed(pp, place, &name, &length) != markers->line;
        result = moves ? place : BUSTER_PP_NONE;
    }
    while (*silent < pp->silent_pragma_count && pp->silent_pragmas[*silent].token <= token)
    {
        if (pp->silent_pragmas[*silent].token == token)
        {
            const uint32_t own = pp_lexed_start(pp, pp->output.locations[token]);
            const uint32_t own_range = pp_find_range(pp, own);
            const bool made = (pp->output.flags[token] & BUSTER_PP_FROM_MACRO) || own_range == BUSTER_PP_NONE || pp->ranges[own_range].file == BUSTER_PP_NONE;
            const uint32_t place = made ? pp_lexed_start(pp, pp->silent_pragmas[*silent].location) : own;
            const char* name;
            uint32_t length;
            const uint32_t range = pp_find_range(pp, place);
            const bool exited = range != BUSTER_PP_NONE && pp->ranges[range].collected_exit != 0 && pp->ranges[range].collected_exit - 1 <= token;
            const bool moves = (!exited && range != markers->range) || pp_presumed(pp, place, &name, &length) != markers->line;
            result = moves ? place : BUSTER_PP_NONE;
        }
        *silent += 1;
    }
    return result;
}

static uint64_t pp_embed_text_vector(const uint8_t* bytes, uint64_t count, bool separated, uint8_t* out)
{
#if !BUSTER_HOST_VECTOR
    return pp_embed_text_scalar(bytes, count, separated, out);
#else
    // Slot j of 8: the separator (quarter 3's ", "), byte j's hundreds, tens and ones (quarters 0 to 2), and zeros.
    static const uint8_t slots[2][64] __attribute__((aligned(64))) = {
        {
            48, 49, 0, 16, 32, 63, 63, 63, 48, 49, 1, 17, 33, 63, 63, 63, 48, 49, 2, 18, 34, 63, 63, 63, 48, 49, 3, 19, 35, 63, 63, 63,
            48, 49, 4, 20, 36, 63, 63, 63, 48, 49, 5, 21, 37, 63, 63, 63, 48, 49, 6, 22, 38, 63, 63, 63, 48, 49, 7, 23, 39, 63, 63, 63,
        },
        {
            48, 49, 8, 24, 40, 63, 63, 63, 48, 49, 9, 25, 41, 63, 63, 63, 48, 49, 10, 26, 42, 63, 63, 63, 48, 49, 11, 27, 43, 63, 63, 63,
            48, 49, 12, 28, 44, 63, 63, 63, 48, 49, 13, 29, 45, 63, 63, 63, 48, 49, 14, 30, 46, 63, 63, 63, 48, 49, 15, 31, 47, 63, 63, 63,
        },
    };
    const __m512i low_slots = _mm512_load_si512(slots[0]);
    const __m512i high_slots = _mm512_load_si512(slots[1]);
    const __m128i separator = _mm_setr_epi8(',', ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i tenth = _mm256_set1_epi16(205);
    const __m256i ten = _mm256_set1_epi16(10);
    const __m256i zero_digit = _mm256_set1_epi16('0');
    uint64_t used = 0;
    for (uint64_t i = 0; i < count; i += 16)
    {
        const uint64_t left = count - i < 16 ? count - i : 16;
        const __m256i values = _mm256_cvtepu8_epi16(_mm_maskz_loadu_epi8((__mmask16)_bzhi_u32(~0u, (uint32_t)left), bytes + i));
        // v / 10 is (v * 205) >> 11 for v below 1029, and its / 10 again for the hundreds.
        const __m256i tens_value = _mm256_srli_epi16(_mm256_mullo_epi16(values, tenth), 11);
        const __m256i hundreds = _mm256_srli_epi16(_mm256_mullo_epi16(tens_value, tenth), 11);
        const __m256i ones = _mm256_sub_epi16(values, _mm256_mullo_epi16(tens_value, ten));
        const __m256i tens = _mm256_sub_epi16(tens_value, _mm256_mullo_epi16(hundreds, ten));
        const __m128i hundred_digits = _mm256_cvtepi16_epi8(_mm256_maskz_add_epi16(_mm256_test_epi16_mask(hundreds, hundreds), hundreds, zero_digit));
        const __m128i ten_digits = _mm256_cvtepi16_epi8(_mm256_maskz_add_epi16(_mm256_test_epi16_mask(tens_value, tens_value), tens, zero_digit));
        const __m128i one_digits = _mm256_cvtepi16_epi8(_mm256_add_epi16(ones, zero_digit));
        const __m512i digits =
            _mm512_inserti32x4(_mm512_inserti32x4(_mm512_inserti32x4(_mm512_castsi128_si512(hundred_digits), ten_digits, 1), one_digits, 2), separator, 3);
        const __m512i low = _mm512_permutexvar_epi8(low_slots, digits);
        const __m512i high = _mm512_permutexvar_epi8(high_slots, digits);
        // The slots of the bytes there are, without the first separator unless `separated`.
        const uint64_t first = i == 0 && !separated ? ~3ull : ~0ull;
        const uint64_t low_kept = _mm512_test_epi8_mask(low, low) & _bzhi_u64(first, (uint32_t)(left < 8 ? 8 * left : 64));
        const uint64_t high_kept = _mm512_test_epi8_mask(high, high) & _bzhi_u64(~0ull, (uint32_t)(left > 8 ? 8 * (left - 8) : 0));
        _mm512_storeu_si512(out + used, _mm512_maskz_compress_epi8(low_kept, low));
        used += (uint64_t)__builtin_popcountll(low_kept);
        _mm512_storeu_si512(out + used, _mm512_maskz_compress_epi8(high_kept, high));
        used += (uint64_t)__builtin_popcountll(high_kept);
    }
    return used;
#endif
}

// pp_embed_text_vector's scalar twin, a byte at a time.
static uint64_t pp_embed_text_scalar(const uint8_t* bytes, uint64_t count, bool separated, uint8_t* out)
{
    uint64_t used = 0;
    for (uint64_t i = 0; i < count; i += 1)
    {
        const uint32_t value = bytes[i];
        if (separated || i != 0)
        {
            out[used] = ',';
            out[used + 1] = ' ';
            used += 2;
        }
        if (value >= 100)
        {
            out[used] = (uint8_t)('0' + value / 100);
            used += 1;
        }
        if (value >= 10)
        {
            out[used] = (uint8_t)('0' + value / 10 % 10);
            used += 1;
        }
        out[used] = (uint8_t)('0' + value % 10);
        used += 1;
    }
    return used;
}

// The bytes an #embed's data token (BUSTER_PP_KIND_EMBED, `value`) stands for, and how many.
static const uint8_t* pp_embed_bytes(const Preprocessor* pp, uint32_t value, uint32_t* length)
{
    const PPEmbed* embed = &pp->embeds[value];
    *length = embed->length;
    return pp->datas[embed->data].bytes + embed->offset;
}

// The first and last characters of an #embed's text (pp_embed_text_vector), for the printers' spaces around it: its first
// value's leading digit and its last value's last digit.
static void pp_embed_ends(const Preprocessor* pp, uint32_t value, uint8_t* first, uint8_t* last)
{
    uint32_t length;
    const uint8_t* bytes = pp_embed_bytes(pp, value, &length);
    *first = (uint8_t)('0' + (bytes[0] >= 100 ? bytes[0] / 100 : bytes[0] >= 10 ? bytes[0] / 10 : bytes[0]));
    *last = (uint8_t)('0' + bytes[length - 1] % 10);
}

// Prints an #embed's text (pp_embed_text_vector, or with `scalar` pp_embed_text_scalar) through `buffer`, which holds
// `*used` bytes of `capacity` (and 64 more of slack) and is written to `stream` as it fills.
static bool pp_print_embed(const Preprocessor* pp, uint32_t value, bool scalar, uint8_t* buffer, uint64_t* used, uint64_t capacity, FILE* stream)
{
    (void)scalar;

    uint32_t length;
    const uint8_t* bytes = pp_embed_bytes(pp, value, &length);
    bool result = true;
    // 64 bits, so that the last piece's step past a length near 4 GiB doesn't wrap the cursor.
    for (uint64_t done = 0; result && done < length; done += BUSTER_PP_EMBED_PRINT_BYTES)
    {
        const uint32_t step = (uint32_t)(length - done < BUSTER_PP_EMBED_PRINT_BYTES ? length - done : BUSTER_PP_EMBED_PRINT_BYTES);
        if (*used + 5ull * BUSTER_PP_EMBED_PRINT_BYTES > capacity)
        {
            result = fwrite(buffer, 1, *used, stream) == *used;
            *used = 0;
        }
        uint8_t* out = buffer + *used;
        *used += BUSTER_SCALAR_PATH(scalar) ? pp_embed_text_scalar(bytes + done, step, done != 0, out) : pp_embed_text_vector(bytes + done, step, done != 0, out);
    }
    return result;
}

static bool pp_print_scalar(Preprocessor* pp, FILE* stream, bool markers)
{
    const PPTokens* out = &pp->output;
    Scratch scratch = scratch_begin(0);
    // Room for a spelling, and for an #embed's text a piece at a time (pp_print_embed).
    uint32_t buffer_size = 1 << 16;
    _Static_assert(5 * BUSTER_PP_EMBED_PRINT_BYTES + 128 <= 1 << 16, "the scalar printer's buffer holds an #embed's piece");
    uint8_t* buffer = arena_push(scratch.arena, buffer_size, 1);
    uint8_t previous_last = 0;
    uint8_t previous_kind = 0;
    bool line_start = true;
    bool force_line = false;
    uint32_t pragma = BUSTER_PP_PRAGMA_OUTSIDE;
    uint32_t silent = 0;
    PPMarkers marked;
    bool result = buffer != 0 && pp_markers_initialize(pp, &marked, scratch.arena) && (!markers || pp_print_start(pp, &marked));
    if (result && markers)
    {
        fwrite(marked.text, 1, marked.length, stream);
    }
    for (uint32_t i = 0; result && i < out->count; i += 1)
    {
        if (markers && BUSTER_UNLIKELY(marked.include_exit != 0 && marked.include_exit->token <= i))
        {
            result = pp_print_include_exits(pp, &marked, i, &line_start, stream);
            force_line = true;
            if (!result)
            {
                break;
            }
        }
        const uint8_t kind = out->kinds[i];
        const uint8_t flags = out->flags[i];
        const bool written = pp_spelled_as_written(pragma, kind);
        pragma = pp_pragma_state(pp, pragma, kind, out->locations[i]);
        if (kind == BUSTER_PP_KIND_PRAGMA_END)
        {
            force_line = true;
            continue;
        }
        const bool starts = (flags & BUSTER_PP_START_OF_LINE) || force_line || i == 0;
        const uint32_t moved = markers && !starts && ((flags & BUSTER_PP_LINE_SPACE) || (silent < pp->silent_pragma_count && pp->silent_pragmas[silent].token <= i))
                                   ? pp_print_moves(pp, &marked, i, &silent)
                                   : BUSTER_PP_NONE;
        if (starts || moved != BUSTER_PP_NONE)
        {
            uint32_t breaks = !line_start;
            const uint32_t located = markers ? pp_print_location(pp, &marked, i) : 0;
            uint32_t place = moved != BUSTER_PP_NONE ? moved : located;
            const uint32_t range = markers ? pp_find_range(pp, place) : BUSTER_PP_NONE;
            if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
            {
                place = pp->ranges[range].base + pp_start_with_splices(pp->ranges[range].bytes, place - pp->ranges[range].base, pp->options.trigraphs);
                breaks = pp_print_place(pp, &marked, range, place, pp_line_of(pp, range, place, 0), line_start, &result);
                fwrite(marked.text, 1, marked.length, stream);
            }
            for (uint32_t n = 0; n < breaks; n += 1)
            {
                fputc('\n', stream);
            }
            line_start = true;
            force_line = false;
            previous_last = 0;
        }
        if (kind == BUSTER_PP_KIND_EMBED)
        {
            // An #embed's text, through the buffer, which has room for its pieces: then like a number before what comes
            // after.
            uint8_t first;
            uint8_t last;
            pp_embed_ends(pp, out->values[i], &first, &last);
            if (!line_start && ((flags & BUSTER_PP_SPACE) || pp_needs_separation(previous_last, previous_kind, first, pp->options.assembly)))
            {
                fputc(' ', stream);
            }
            uint64_t used = 0;
            result = pp_print_embed(pp, out->values[i], true, buffer, &used, buffer_size - 64, stream) && fwrite(buffer, 1, used, stream) == used;
            previous_last = last;
            previous_kind = C_TOKEN_NUMERIC_CONSTANT;
            line_start = false;
            continue;
        }
        const uint32_t room = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END) ? 0 : out->values[i] - out->locations[i];
        if (room + 64 > buffer_size)
        {
            buffer_size = room + 64;
            buffer = arena_push(scratch.arena, buffer_size, 1);
            result = buffer != 0;
        }
        const uint64_t mark = scratch.arena->length;
        const uint8_t* spelling = 0;
        const uint32_t length = !result ? 0
                                : written ? pp_written_spelling(pp, out->values[i], out->locations[i], scratch.arena, &spelling)
                                          : pp_spelling(pp, kind, out->values[i], out->locations[i], buffer, &spelling);
        result = result && spelling != 0;
        if (result)
        {
            if (!line_start && length != 0 && ((flags & BUSTER_PP_SPACE) || pp_needs_separation(previous_last, previous_kind, spelling[0], pp_dollar_punctuation(&pp->options))))
            {
                fputc(' ', stream);
            }
            fwrite(spelling, 1, length, stream);
            marked.line += markers && kind == C_TOKEN_UNKNOWN ? pp_print_token_lines(spelling, length) : 0;
            previous_last = length != 0 ? spelling[length - 1] : previous_last;
            previous_kind = kind;
            line_start = false;
        }
        arena_pop_to(scratch.arena, mark);
    }
    if (result && markers && marked.include_exit != 0)
    {
        result = pp_print_include_exits(pp, &marked, out->count, &line_start, stream);
    }
    if (!line_start)
    {
        fputc('\n', stream);
    }
    scratch_end(scratch);
    return result && ferror(stream) == 0;
}

static uint32_t* pp_build_directory(const Preprocessor* pp, Arena* arena)
{
    const uint32_t pages = (pp->location_top >> BUSTER_PP_DIRECTORY_SHIFT) + 1;
    uint32_t* result = arena_push_array(arena, pages, sizeof(uint32_t), 64);
    uint32_t page = 0;
    for (uint32_t r = 0; result != 0 && r < pp->range_count; r += 1)
    {
        const uint32_t last = (pp->ranges[r].base + pp->ranges[r].length - 1) >> BUSTER_PP_DIRECTORY_SHIFT;
        for (; page <= last && page < pages; page += 1)
        {
            result[page] = r;
        }
    }
    for (; result != 0 && page < pages; page += 1)
    {
        result[page] = pp->range_count != 0 ? pp->range_count - 1 : 0;
    }
    return result;
}

// The range that holds `location`, or BUSTER_PP_NONE, as pp_find_range gives it.
BUSTER_INLINE uint32_t pp_directory_range(const Preprocessor* pp, const uint32_t* directory, uint32_t location)
{
    uint32_t result = directory[location >> BUSTER_PP_DIRECTORY_SHIFT];
    while (result + 1 < pp->range_count && location >= pp->ranges[result].base + pp->ranges[result].length)
    {
        result += 1;
    }
    const bool inside = pp->range_count != 0 && location >= pp->ranges[result].base && location - pp->ranges[result].base < pp->ranges[result].length;
    return inside ? result : BUSTER_PP_NONE;
}

static const uint8_t pp_after_classes[128] __attribute__((aligned(64))) = {
    ['a' ... 'z'] = 1, ['A' ... 'Z'] = 1, ['_'] = 1, ['$'] = 1, ['\\'] = 1, ['0' ... '9'] = 2, ['.'] = 3, ['\''] = 4, ['"'] = 5, ['+'] = 6, ['-'] = 6,
    ['*'] = 7, ['/'] = 7, ['%'] = 7, ['<'] = 7, ['>'] = 7, ['='] = 7, ['!'] = 7, ['&'] = 7, ['|'] = 7, ['^'] = 7, ['#'] = 7, [':'] = 7, ['?'] = 7,
};

static const uint8_t pp_before_classes[128] __attribute__((aligned(64))) = {
    ['a' ... 'z'] = 1, ['A' ... 'Z'] = 1, ['_'] = 1, ['$'] = 1, ['\\'] = 1, ['0' ... '9'] = 1, ['.'] = 2, ['+'] = 3, ['-'] = 3, ['*'] = 3, ['/'] = 3,
    ['%'] = 3, ['<'] = 3, ['>'] = 3, ['='] = 3, ['!'] = 3, ['&'] = 3, ['|'] = 3, ['^'] = 3, ['#'] = 3, [':'] = 3, ['?'] = 3,
};

// Whether a space goes between two tokens, by before-class * 8 + after-class: pp_needs_separation as a table. A word
// goes on with a word, a digit or a quote (a prefix makes a literal); '.' with a digit or a joining character; a joining
// character with one; a pp-number with a word, a digit, '.', '\'' (a digit separator) or an exponent's sign.
static const uint8_t pp_separation_table[64] __attribute__((aligned(64))) = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 1, 0, 0, 1, 1,
    0, 1, 1, 1, 1, 0, 1, 0,
};

// For pp_print_spellings, by kind (below 128: a keyword's, from 128 on, is an identifier's), a character of the class
// (pp_after_classes, pp_before_classes) of the first and of the last character of a token's spelling as written, where
// the kind fixes it: a word character for an identifier, a punctuator's own characters, and a character or string
// literal's prefix or quote and its closing quote. The spaces read these classes alone. Zero where the kind doesn't fix
// them, whose bytes are read: an unknown token's, a pp-number's (a digit or '.' first) and a string literal's without a
// prefix (a quote first, or a raw one's R). A bracket or a brace may be a digraph (`<:`, `:>`, `<%`, `%>`), whose
// characters join; '#' and "##" have the classes of theirs (`%:`, `%:%:`). A splice or a trigraph in a spelling changes
// its characters, but the printer makes such a chunk again by its general loop, which reads the bytes.
static const uint8_t pp_print_kind_firsts[128] __attribute__((aligned(64))) = {
    [C_TOKEN_RAW_IDENTIFIER] = 'a',
    [C_TOKEN_CHAR_CONSTANT] = '\'',
    [C_TOKEN_WIDE_CHAR_CONSTANT] = 'L',
    [C_TOKEN_UTF8_CHAR_CONSTANT] = 'u',
    [C_TOKEN_UTF16_CHAR_CONSTANT] = 'u',
    [C_TOKEN_UTF32_CHAR_CONSTANT] = 'U',
    [C_TOKEN_WIDE_STRING_LITERAL] = 'L',
    [C_TOKEN_UTF8_STRING_LITERAL] = 'u',
    [C_TOKEN_UTF16_STRING_LITERAL] = 'u',
    [C_TOKEN_UTF32_STRING_LITERAL] = 'U',
    [C_TOKEN_L_SQUARE] = '[',
    [C_TOKEN_R_SQUARE] = ']',
    [C_TOKEN_L_PAREN] = '(',
    [C_TOKEN_R_PAREN] = ')',
    [C_TOKEN_L_BRACE] = '{',
    [C_TOKEN_R_BRACE] = '}',
    [C_TOKEN_PERIOD] = '.',
    [C_TOKEN_ELLIPSIS] = '.',
    [C_TOKEN_AMP] = '&',
    [C_TOKEN_AMPAMP] = '&',
    [C_TOKEN_AMPEQUAL] = '&',
    [C_TOKEN_STAR] = '*',
    [C_TOKEN_STAREQUAL] = '*',
    [C_TOKEN_PLUS] = '+',
    [C_TOKEN_PLUSPLUS] = '+',
    [C_TOKEN_PLUSEQUAL] = '+',
    [C_TOKEN_MINUS] = '-',
    [C_TOKEN_ARROW] = '-',
    [C_TOKEN_MINUSMINUS] = '-',
    [C_TOKEN_MINUSEQUAL] = '-',
    [C_TOKEN_TILDE] = '~',
    [C_TOKEN_EXCLAIM] = '!',
    [C_TOKEN_EXCLAIMEQUAL] = '!',
    [C_TOKEN_SLASH] = '/',
    [C_TOKEN_SLASHEQUAL] = '/',
    [C_TOKEN_PERCENT] = '%',
    [C_TOKEN_PERCENTEQUAL] = '%',
    [C_TOKEN_LESS] = '<',
    [C_TOKEN_LESSLESS] = '<',
    [C_TOKEN_LESSEQUAL] = '<',
    [C_TOKEN_LESSLESSEQUAL] = '<',
    [C_TOKEN_GREATER] = '>',
    [C_TOKEN_GREATERGREATER] = '>',
    [C_TOKEN_GREATEREQUAL] = '>',
    [C_TOKEN_GREATERGREATEREQUAL] = '>',
    [C_TOKEN_CARET] = '^',
    [C_TOKEN_CARETEQUAL] = '^',
    [C_TOKEN_PIPE] = '|',
    [C_TOKEN_PIPEPIPE] = '|',
    [C_TOKEN_PIPEEQUAL] = '|',
    [C_TOKEN_QUESTION] = '?',
    [C_TOKEN_COLON] = ':',
    [C_TOKEN_COLONCOLON] = ':',
    [C_TOKEN_SEMI] = ';',
    [C_TOKEN_EQUAL] = '=',
    [C_TOKEN_EQUALEQUAL] = '=',
    [C_TOKEN_COMMA] = ',',
    [C_TOKEN_HASH] = '#',
    [C_TOKEN_HASHHASH] = '#',
    [C_TOKEN_PERIODSTAR] = '.',
    [C_TOKEN_ARROWSTAR] = '-',
    [C_TOKEN_SPACESHIP] = '<',
    [BUSTER_PP_KIND_KEYWORD ... 127] = 'a',
};

static const uint8_t pp_print_kind_lasts[128] __attribute__((aligned(64))) = {
    [C_TOKEN_RAW_IDENTIFIER] = 'a',
    [C_TOKEN_CHAR_CONSTANT ... C_TOKEN_UTF32_CHAR_CONSTANT] = '\'',
    [C_TOKEN_STRING_LITERAL ... C_TOKEN_UTF32_STRING_LITERAL] = '"',
    [C_TOKEN_L_SQUARE] = '[',
    [C_TOKEN_R_SQUARE] = ']',
    [C_TOKEN_L_PAREN] = '(',
    [C_TOKEN_R_PAREN] = ')',
    [C_TOKEN_L_BRACE] = '{',
    [C_TOKEN_R_BRACE] = '}',
    [C_TOKEN_PERIOD] = '.',
    [C_TOKEN_ELLIPSIS] = '.',
    [C_TOKEN_AMP] = '&',
    [C_TOKEN_AMPAMP] = '&',
    [C_TOKEN_AMPEQUAL] = '=',
    [C_TOKEN_STAR] = '*',
    [C_TOKEN_STAREQUAL] = '=',
    [C_TOKEN_PLUS] = '+',
    [C_TOKEN_PLUSPLUS] = '+',
    [C_TOKEN_PLUSEQUAL] = '=',
    [C_TOKEN_MINUS] = '-',
    [C_TOKEN_ARROW] = '>',
    [C_TOKEN_MINUSMINUS] = '-',
    [C_TOKEN_MINUSEQUAL] = '=',
    [C_TOKEN_TILDE] = '~',
    [C_TOKEN_EXCLAIM] = '!',
    [C_TOKEN_EXCLAIMEQUAL] = '=',
    [C_TOKEN_SLASH] = '/',
    [C_TOKEN_SLASHEQUAL] = '=',
    [C_TOKEN_PERCENT] = '%',
    [C_TOKEN_PERCENTEQUAL] = '=',
    [C_TOKEN_LESS] = '<',
    [C_TOKEN_LESSLESS] = '<',
    [C_TOKEN_LESSEQUAL] = '=',
    [C_TOKEN_LESSLESSEQUAL] = '=',
    [C_TOKEN_GREATER] = '>',
    [C_TOKEN_GREATERGREATER] = '>',
    [C_TOKEN_GREATEREQUAL] = '=',
    [C_TOKEN_GREATERGREATEREQUAL] = '=',
    [C_TOKEN_CARET] = '^',
    [C_TOKEN_CARETEQUAL] = '=',
    [C_TOKEN_PIPE] = '|',
    [C_TOKEN_PIPEPIPE] = '|',
    [C_TOKEN_PIPEEQUAL] = '=',
    [C_TOKEN_QUESTION] = '?',
    [C_TOKEN_COLON] = ':',
    [C_TOKEN_COLONCOLON] = ':',
    [C_TOKEN_SEMI] = ';',
    [C_TOKEN_EQUAL] = '=',
    [C_TOKEN_EQUALEQUAL] = '=',
    [C_TOKEN_COMMA] = ',',
    [C_TOKEN_HASH] = '#',
    [C_TOKEN_HASHHASH] = '#',
    [C_TOKEN_PERIODSTAR] = '*',
    [C_TOKEN_ARROWSTAR] = '*',
    [C_TOKEN_SPACESHIP] = '>',
    [BUSTER_PP_KIND_KEYWORD ... 127] = 'a',
};

static uint32_t pp_print_line(Preprocessor* pp, PPPrintLines* lines, uint32_t range, uint32_t location)
{
    const PPRange* record = &pp->ranges[range];
    const PPFileData* data = &pp->datas[pp->files[record->file].data];
    const uint32_t offset = location - record->base < data->length ? location - record->base : data->length;
    uint32_t result;
    if (range == lines->range && offset >= lines->offset)
    {
        result = lines->line + pp_count_lines(data, lines->offset, offset);
    }
    else
    {
        result = pp_line_of(pp, range, location, 0);
    }
    *lines = (PPPrintLines){ .range = range, .offset = offset, .line = result };
    return result;
}

static uint32_t pp_print_offsets(const uint32_t* lengths, uint32_t count, uint64_t ones, const uint8_t* breaks, uint16_t* offsets)
{
    uint32_t total = 0;
    bool ordinary = true;
    for (uint32_t first = 0; first < count; first += 32)
    {
        const uint32_t take = count - first < 32 ? count - first : 32;
        const uint32_t low = take < 16 ? take : 16;
        const uint32_t high = take > 16 ? take - 16 : 0;
        const __m512i a = _mm512_maskz_loadu_epi32((__mmask16)_bzhi_u32(~0u, low), lengths + first);
        const __m512i b = high != 0 ? _mm512_maskz_loadu_epi32((__mmask16)_bzhi_u32(~0u, high), lengths + first + 16) : _mm512_setzero_si512();
        ordinary &= (_mm512_mask_cmplt_epu32_mask((__mmask16)_bzhi_u32(~0u, low), _mm512_sub_epi32(a, _mm512_set1_epi32(1)), _mm512_set1_epi32(64)) == (__mmask16)_bzhi_u32(~0u, low));
        ordinary &= (_mm512_mask_cmplt_epu32_mask((__mmask16)_bzhi_u32(~0u, high), _mm512_sub_epi32(b, _mm512_set1_epi32(1)), _mm512_set1_epi32(64)) == (__mmask16)_bzhi_u32(~0u, high));
        const __m512i words = _mm512_inserti64x4(_mm512_castsi256_si512(_mm512_cvtepi32_epi16(a)), _mm512_cvtepi32_epi16(b), 1);
        const __mmask32 lanes = (__mmask32)_bzhi_u64(~0ull, take);
        const __m512i counted = breaks != 0 ? _mm512_add_epi16(words, _mm512_cvtepu8_epi16(_mm256_maskz_loadu_epi8(lanes, breaks + first))) : words;
        const __m512i sizes = _mm512_mask_add_epi16(counted, (__mmask32)((ones >> first) & lanes), counted, _mm512_set1_epi16(1));
        const __m512i ends = _mm512_add_epi16(pp_prefix_sum_words(sizes), _mm512_set1_epi16((short)total));
        _mm512_mask_storeu_epi16(offsets + first, (__mmask32)_bzhi_u64(~0ull, take), _mm512_sub_epi16(ends, words));
        // Inactive words are zero, so the final lane also holds the last active word's end.
        total = (uint16_t)((__v32hi)ends)[31];
    }
    return ordinary ? total : 0;
}

// pp_print_vector's step 1 for a chunk of `valid` tokens (of the columns at `kinds`, `values` and `locations`, from
// the chunk's first) of which none is a pragma line's identifier spelled as written, an integer, an #embed's data or
// another internal kind, without a branch on each token's kind (#649): the identifiers' spellings from the intern
// table, a loop over their bits; and every other token's bytes at its location, which is its spelling unless a line
// splice or a trigraph is in it (pp_print_unspliced, or the batch's copies, check that), its length its value less its
// location. Their ranges are found a range at a time: the printer's directory gives the range of the first token not
// placed yet, and compares of the chunk's locations with that range's bounds place every token inside it; a chunk
// holds 1.5 to 2 ranges on average (sqlite3.c, main.c), and at most 7 on those corpora. Each range costs compares of
// all 64 locations, so a chunk past BUSTER_PP_PRINT_RANGES of them goes to the general loop, whose lookup per token
// costs less from about 10 ranges up. With `prefetch`, a prefetch of each of those tokens' spellings, whose bytes nothing
// may have read since their file was prepared: with -P, the copies would wait for them (#649: on the 9700X, without the
// two bytes of every token step 1 used to read, -P printed main.c and sqlite3.c 9-13% slower than with them, and with the
// prefetches 2-5% faster); with line markers, pp_print_place_lines counts the lines up to each line start by reading
// those bytes, and the prefetches cost 2-3%. Then, into all 64 lanes of `firsts` and `lasts` (64-byte aligned), a character
// of the class of each token's first and last: by its kind (pp_print_kind_firsts, pp_print_kind_lasts), and the bytes
// themselves of the kinds the table doesn't fix, of a bracket or a brace longer than one byte (a digraph) and, where
// '$' is punctuation, of every identifier, which pp_split_dollars leaves without a '$' there, but the classes don't
// count on that (#649: reading every token's two bytes, and step 2's load of the 128 byte stores, were a third of -P's
// printing on the 9700X). Returns false, for the general loop to give the chunk's spellings, when a token's location
// is in no range, the other tokens (identifiers take no range) are in more ranges than that or a spelling is empty.
static bool pp_print_spellings(const Preprocessor* pp, const uint32_t* directory, const uint8_t* kinds, const uint32_t* values, const uint32_t* locations,
                               uint64_t identifiers, uint64_t valid, bool prefetch, const uint8_t* records, const InternSpan* spans, const uint8_t** spellings,
                               uint32_t* lengths, uint8_t* firsts, uint8_t* lasts)
{
    const uint64_t others = valid & ~identifiers;
    // Each token's bytes less its location (its range's bytes less the range's base), 8 tokens a vector.
    __m512i deltas[8];
    for (uint32_t q = 0; q < 8; q += 1)
    {
        deltas[q] = _mm512_setzero_si512();
    }
    uint64_t pending = others;
    bool result = true;
    uint32_t looked = 0;
    while (result && pending != 0)
    {
        const uint32_t range = looked < BUSTER_PP_PRINT_RANGES ? pp_directory_range(pp, directory, locations[__builtin_ctzll(pending)]) : BUSTER_PP_NONE;
        looked += 1;
        result = range != BUSTER_PP_NONE;
        if (result)
        {
            const PPRange* record = &pp->ranges[range];
            const __m512i base = _mm512_set1_epi32((int)record->base);
            const __m512i extent = _mm512_set1_epi32((int)record->length);
            uint64_t inside = 0;
            for (uint32_t q = 0; q < 4; q += 1)
            {
                const __mmask16 lanes = (__mmask16)(pending >> (16 * q));
                const __m512i location = _mm512_maskz_loadu_epi32(lanes, locations + 16 * q);
                inside |= (uint64_t)_mm512_mask_cmplt_epu32_mask(lanes, _mm512_sub_epi32(location, base), extent) << (16 * q);
            }
            const __m512i delta = _mm512_set1_epi64((long long)((uintptr_t)record->bytes - record->base));
            for (uint32_t q = 0; q < 8; q += 1)
            {
                deltas[q] = _mm512_mask_mov_epi64(deltas[q], (__mmask8)(inside >> (8 * q)), delta);
            }
            pending &= ~inside;
        }
    }
    uint64_t empty = 0;
    // The other tokens longer than one byte.
    uint64_t longer = 0;
    for (uint32_t q = 0; result && q < 4; q += 1)
    {
        const __mmask16 lanes = (__mmask16)(others >> (16 * q));
        const __m512i length = _mm512_sub_epi32(_mm512_maskz_loadu_epi32(lanes, values + 16 * q), _mm512_maskz_loadu_epi32(lanes, locations + 16 * q));
        _mm512_mask_storeu_epi32(lengths + 16 * q, lanes, length);
        empty |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(lanes, length, _mm512_setzero_si512()) << (16 * q);
        longer |= (uint64_t)_mm512_mask_cmpgt_epu32_mask(lanes, length, _mm512_set1_epi32(1)) << (16 * q);
    }
    for (uint32_t q = 0; result && q < 8; q += 1)
    {
        const __mmask8 lanes = (__mmask8)(others >> (8 * q));
        const __m512i location = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(lanes, locations + 8 * q));
        _mm512_mask_storeu_epi64((void*)(spellings + 8 * q), lanes, _mm512_add_epi64(deltas[q], location));
    }
    for (uint64_t bits = result ? identifiers & valid : 0; bits != 0; bits &= bits - 1)
    {
        const uint32_t k = (uint32_t)__builtin_ctzll(bits);
        const InternSpan span = spans[values[k]];
        spellings[k] = records + span.offset;
        lengths[k] = span.length;
        empty |= (uint64_t)(span.length == 0) << k;
    }
    result = result && empty == 0;
    for (uint64_t bits = result && prefetch ? others : 0; bits != 0; bits &= bits - 1)
    {
        __builtin_prefetch(spellings[__builtin_ctzll(bits)], 0, 3);
    }
    // The classes' characters by kind (a keyword's kind may be past the tables' 128, which the identifiers' mask
    // covers), then the bytes of the tokens whose kind doesn't fix them, in the registers.
    const __m512i kind = _mm512_maskz_loadu_epi8(valid, kinds);
    const __m512i fixed = _mm512_permutex2var_epi8(_mm512_load_si512(pp_print_kind_firsts), kind, _mm512_load_si512(pp_print_kind_firsts + 64));
    const uint64_t unfixed = _mm512_mask_testn_epi8_mask(valid, fixed, fixed) & ~identifiers;
    __m512i first = _mm512_mask_mov_epi8(fixed, identifiers, _mm512_set1_epi8('a'));
    __m512i last = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(_mm512_load_si512(pp_print_kind_lasts), kind, _mm512_load_si512(pp_print_kind_lasts + 64)),
                                        identifiers, _mm512_set1_epi8('a'));
    const uint64_t brackets = _mm512_mask_cmplt_epu8_mask(valid, _mm512_sub_epi8(kind, _mm512_set1_epi8(C_TOKEN_L_SQUARE)), _mm512_set1_epi8(6)) &
                              ~_mm512_mask_cmplt_epu8_mask(valid, _mm512_sub_epi8(kind, _mm512_set1_epi8(C_TOKEN_L_PAREN)), _mm512_set1_epi8(2));
    const uint64_t read = (unfixed | (brackets & longer) | (pp_dollar_punctuation(&pp->options) ? identifiers : 0)) & valid;
    for (uint64_t bits = result ? read : 0; bits != 0; bits &= bits - 1)
    {
        const uint32_t k = (uint32_t)__builtin_ctzll(bits);
        first = _mm512_mask_set1_epi8(first, 1ull << k, (char)spellings[k][0]);
        last = _mm512_mask_set1_epi8(last, 1ull << k, (char)spellings[k][lengths[k] - 1]);
    }
    _mm512_store_si512(firsts, first);
    _mm512_store_si512(lasts, last);
    return result;
}

// Whether no spelling of `others` among pp_print_spellings' may hold a line splice or, with `trigraphs`, a trigraph,
// so that each is its bytes as written (pp_without_splices): the check of a chunk whose copies are step 3's. A splice
// is a backslash, whitespace (every byte up to ' ' counts here) and a line break, so a backslash may start one only
// when a blank follows it, or nothing in its 64 bytes (the masked load's zeros past the spelling count as blanks, and
// so does a backslash in the last lane); a backslash before anything else, as in an escape sequence, may not.
static bool pp_print_unspliced(const uint8_t* const* spellings, const uint32_t* lengths, uint64_t others, bool trigraphs)
{
    const __mmask64 questions = trigraphs ? ~0ull : 0;
    uint64_t possible = 0;
    for (uint64_t bits = others; bits != 0; bits &= bits - 1)
    {
        const uint32_t k = (uint32_t)__builtin_ctzll(bits);
        for (uint32_t b = 0; b < lengths[k]; b += 64)
        {
            const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, lengths[k] - b < 64 ? lengths[k] - b : 64), spellings[k] + b);
            const uint64_t blanks = _mm512_cmple_epu8_mask(bytes, _mm512_set1_epi8(' '));
            possible |= (_mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\')) & ((blanks >> 1) | (1ull << 63))) |
                        _mm512_mask_cmpeq_epi8_mask(questions, bytes, _mm512_set1_epi8('?'));
        }
    }
    return possible == 0;
}

// With line markers, the line breaks before each line start of `starts` (a chunk's, from its token `c`), in their
// order, as pp_print_place puts them in its common case (pp_print_place_common): into `counts`, and the markers' state
// and the printer's lines advanced past them as pp_print_place advances them. Returns the first line start it can't
// place so (a marker, a jump of more than 8 lines, a place in no file, or a #pragma's line kept), with nothing of its
// own placed but the idempotent steps the per-token copies take again (its expansion, its place's line), or 64.
static uint32_t pp_print_place_lines(Preprocessor* pp, PPMarkers* markers, PPPrintLines* lines, const uint32_t* directory, uint32_t c, uint64_t starts, uint8_t* counts)
{
    uint32_t result = 64;
    for (uint64_t bits = starts; result == 64 && bits != 0; bits &= bits - 1)
    {
        const uint32_t k = (uint32_t)__builtin_ctzll(bits);
        uint32_t place = pp_print_location(pp, markers, c + k);
        const uint32_t range = pp_directory_range(pp, directory, place);
        bool common = range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE;
        if (common)
        {
            const PPRange* record = &pp->ranges[range];
            place = record->base + pp_start_with_splices(record->bytes, place - record->base, pp->options.trigraphs);
            const uint32_t line = (uint32_t)((int64_t)pp_print_line(pp, lines, range, place) + markers->delta);
            common = pp_print_place_common(pp, markers, range, place, line);
            counts[k] = (uint8_t)(line - markers->line);
            markers->line = common ? line : markers->line;
        }
        result = common ? result : k;
    }
    markers->length = 0;
    return result;
}

// Prints the output as pp_print_scalar does, byte for byte, 64 tokens at a time into one buffer that is written as it
// fills. For each chunk: each token's spelling (pp_print_spellings: an identifier's from the intern table, else its
// bytes in the location space, found a range at a time; or the general loop's, token by token, for a chunk with a
// pragma line, an integer, an #embed or a possible line splice), then the spaces for all 64 at once: a token at the
// start of a line gets none, any other one if whitespace was before it or if its first character would lex together
// with the last character of the token before (pp_separation_table, indexed by the two characters' classes from
// vpermi2b lookups, which pp_print_spellings gives by kind), and then the copies, one masked 64-byte store per token
// (more for a longer spelling), with the line breaks and line markers between: in batches at prefix-sum offsets
// (pp_print_offsets), each up to a spelling past 64 bytes and with line markers up to each line start that needs a
// marker (pp_print_place_lines), and that token and the rest token by token. pp_print_vector compiles it
// for each value of `markers`: with -P, the batch's gaps are one bit each, as they were before the line markers' batch,
// whose code in one copy for both cost -P 8-12% (#649, on an AVX-512 Xeon).
BUSTER_INLINE bool pp_print_vector_mode(Preprocessor* pp, FILE* stream, const bool markers)
{
    // The columns and tables in locals: a store through a byte pointer may change any field, which the loops would load
    // again for every token.
    const uint8_t* out_kinds = pp->output.kinds;
    const uint8_t* out_flags = pp->output.flags;
    const uint32_t* out_values = pp->output.values;
    const uint32_t* out_locations = pp->output.locations;
    const uint32_t count = pp->output.count;
    const uint8_t* records = pp->table->records;
    const InternSpan* spans = pp->table->spans;
    // With trigraphs, any '?' in a fast spelling sends its chunk to the general loop, as a possible line splice does.
    const bool trigraphs = (pp->refine_flags & C_TOKENIZER_TRIGRAPHS) != 0;
    // By a byte's low 6 bits (vpermb): '\\' for those of '\\', '?' for those of '?' with trigraphs, and else a byte whose
    // low 6 bits differ, which no byte with those bits equals; so a byte equals its lookup if it is one of those. The
    // zeros past a spelling look up 32.
    __m512i specials = _mm512_and_si512(_mm512_add_epi8(_mm512_load_si512(pp_byte_indices), _mm512_set1_epi8(32)), _mm512_set1_epi8(63));
    specials = _mm512_mask_mov_epi8(specials, 1ull << ('\\' & 63), _mm512_set1_epi8('\\'));
    specials = _mm512_mask_mov_epi8(specials, (uint64_t)trigraphs << ('?' & 63), _mm512_set1_epi8('?'));
    const bool per_token = pp->options.print_per_token;
    const __m512i hash_location = _mm512_set1_epi32((int)pp->hash_location);
    Scratch scratch = scratch_begin(0);
    const uint32_t* directory = pp_build_directory(pp, scratch.arena);
    const uint64_t capacity = 1ull << 20;
    uint8_t* buffer = arena_push(scratch.arena, capacity + 64, 64);
    bool result = directory != 0 && buffer != 0;
    uint64_t used = 0;
    // A chunk's spellings, lengths, and first and last bytes.
    const uint8_t* spellings[64];
    uint32_t lengths[64];
    uint8_t firsts[64] __attribute__((aligned(64))) = { 0 };
    uint8_t lasts[64] __attribute__((aligned(64))) = { 0 };
    PPPrintLines lines = { .range = BUSTER_PP_NONE };
    // The state between tokens, as pp_print_scalar keeps it.
    bool line_start = true;
    bool force_line = false;
    uint32_t silent = 0;
    PPMarkers marked;
    result = result && pp_markers_initialize(pp, &marked, scratch.arena) && (!markers || pp_print_start(pp, &marked));
    if (result && markers)
    {
        memcpy(buffer, marked.text, marked.length);
        used = marked.length;
    }
    // The last character and kind of the token before the chunk, for its first token's space.
    uint8_t previous_last = 0;
    uint8_t previous_kind = 0;
    // Where '$' is punctuation (assembly, -fno-dollars-in-identifiers: pp_dollar_punctuation), it is split from names
    // and numbers. Classify it as punctuation once for all chunks; source spaces stay.
    const bool dollar_punctuation = pp_dollar_punctuation(&pp->options);
    const __mmask64 word_classes = ~((uint64_t)dollar_punctuation << '$');
    uint64_t pragma_inside = 0;
    uint64_t pragma_name = 0;
    const __m512i after_low = _mm512_maskz_loadu_epi8(word_classes, pp_after_classes);
    const __m512i after_high = _mm512_load_si512(pp_after_classes + 64);
    const __m512i before_low = _mm512_maskz_loadu_epi8(word_classes, pp_before_classes);
    const __m512i before_high = _mm512_load_si512(pp_before_classes + 64);
    const __m512i separation = _mm512_load_si512(pp_separation_table);
    const __m512i previous_lanes = _mm512_sub_epi8(_mm512_load_si512(pp_byte_indices), _mm512_set1_epi8(1));
    for (uint32_t c = 0; result && c < count; c += 64)
    {
        const uint32_t available = count - c < 64 ? count - c : 64;
        const __mmask64 valid = _bzhi_u64(~0ull, available);
        const __m512i kinds = _mm512_maskz_loadu_epi8(valid, out_kinds + c);
        // 0. The identifiers of pragma lines after their names, which -E spells as written (pp_spelled_as_written): a
        // line from its '#' (a hash at the scratch "#") to its end, which alternate, is the bits [start, end) of ends
        // less starts, less one when the chunk starts inside a line, whose end comes first.
        uint64_t pragma_starts = 0;
        for (uint32_t q = 0; q < 4; q += 1)
        {
            const __mmask16 lanes = (__mmask16)(valid >> (16 * q));
            const __m512i locations = _mm512_maskz_loadu_epi32(lanes, out_locations + c + 16 * q);
            pragma_starts |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(lanes, locations, hash_location) << (16 * q);
        }
        pragma_starts &= _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_TOKEN_HASH));
        const uint64_t pragma_ends = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END));
        const uint64_t pragma_lines = pragma_ends - pragma_starts - pragma_inside;
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_TOKEN_RAW_IDENTIFIER)) |
                                     _mm512_mask_cmplt_epu8_mask(valid, _mm512_sub_epi8(kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_KEYWORD)),
                                                                 _mm512_set1_epi8(C_NAME_KEYWORD_COUNT));
        const uint64_t written = pragma_lines & ~pragma_starts & ~((pragma_starts << 1) | pragma_name) & identifiers & valid;
        pragma_inside = (pragma_lines >> (available - 1)) & 1;
        pragma_name = pragma_starts >> 63;
        // 1. Spellings: an identifier's from the intern table, any other token's bytes at its location. Without a
        // pragma line's identifiers, an integer or an #embed, pp_print_spellings gives them, with no branch on the kind
        // (#649: the general loop's branch and directory lookup per token were 57-59% of -P's printing of sqlite3.c and
        // main.c on an AVX-512 Xeon, docs/c-preprocessor.md). Its spellings are checked for line splices by the batch's
        // copies, which load their bytes anyway, or else before step 3's copies (pp_print_unspliced); a chunk that may
        // hold one goes through the general loop, token by token, which takes every case, and its spaces and the copies
        // of its tokens not printed yet are made again.
        const uint64_t spare_mark = scratch.arena->length;
        const uint64_t internal = _mm512_mask_cmpge_epu8_mask(valid, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_PRAGMA_END));
        bool general = per_token || (written | internal) != 0 ||
                       !pp_print_spellings(pp, directory, out_kinds + c, out_values + c, out_locations + c, identifiers, valid, !markers, records, spans, spellings,
                                           lengths, firsts, lasts);
        const uint64_t embeds = _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_EMBED));
        // With markers, the unknown tokens, whose line breaks the printer's line counts (pp_print_token_lines).
        const uint64_t unknowns = markers ? _mm512_mask_cmpeq_epi8_mask(valid, kinds, _mm512_set1_epi8(C_TOKEN_UNKNOWN)) : 0;
        // With markers, the tokens that pp_print_moves may place on a line of their own, which the copies place.
        const uint64_t line_spaces = markers ? _mm512_mask_test_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, out_flags + c), _mm512_set1_epi8(BUSTER_PP_LINE_SPACE)) : 0;
        // The state before the chunk, which step 2 reads whenever it runs again.
        const uint8_t entry_last = previous_last;
        const uint8_t entry_kind = previous_kind;
        const bool entry_force_line = force_line;
        const bool entry_line_start = line_start;
        uint64_t line_starts = 0;
        uint64_t spaced = 0;
        // The tokens a batch can't copy, an empty spelling or one past 64 bytes: each ends a batch and goes token by
        // token, and the next batch starts after it.
        uint64_t longs = 0;
        // With line markers, each line start's breaks in a batch (pp_print_place_lines).
        uint8_t line_breaks[64] __attribute__((aligned(64)));
        uint16_t offsets[64];
        // Steps 1 and 2 run again when `stale`: once, with the general loop's spellings, if pp_print_spellings' may
        // hold a splice. Step 3 prints the chunk's tokens from `from` on, a batch and token by token, in turns.
        bool stale = true;
        uint32_t from = 0;
        while (result && from < available)
        {
            for (uint32_t k = 0; stale && general && result && k < available; k += 1)
            {
                const uint32_t i = c + k;
                const uint8_t kind = out_kinds[i];
                const uint32_t value = out_values[i];
                const uint32_t location = out_locations[i];
                const uint8_t* spelling = 0;
                uint32_t length = 0;
                if (BUSTER_UNLIKELY((written >> k) & 1))
                {
                    length = pp_written_spelling(pp, value, location, scratch.arena, &spelling);
                    result = spelling != 0;
                    spelling = result ? spelling : (const uint8_t*)"";
                    length = result ? length : 0;
                }
                else if (kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_KEYWORD + C_NAME_KEYWORD_COUNT))
                {
                    spelling = records + spans[value].offset;
                    length = spans[value].length;
                }
                else if (kind != BUSTER_PP_KIND_PRAGMA_END && kind != BUSTER_PP_KIND_INTEGER && kind != BUSTER_PP_KIND_EMBED)
                {
                    const uint32_t range = pp_directory_range(pp, directory, location);
                    spelling = range != BUSTER_PP_NONE ? pp->ranges[range].bytes + (location - pp->ranges[range].base) : (const uint8_t*)"";
                    length = range != BUSTER_PP_NONE ? value - location : 0;
                    const __m512i bytes = _mm512_maskz_loadu_epi8(_bzhi_u64(~0ull, length < 64 ? length : 64), spelling);
                    bool special = _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('\\')) != 0 || (trigraphs && _mm512_cmpeq_epi8_mask(bytes, _mm512_set1_epi8('?')) != 0);
                    for (uint32_t b = 64; !special && b < length; b += 1)
                    {
                        special = spelling[b] == '\\' || (trigraphs && spelling[b] == '?');
                    }
                    if (special)
                    {
                        uint8_t* spare = arena_push(scratch.arena, (uint64_t)length + 1, 1);
                        result = spare != 0;
                        const uint32_t raw = length;
                        length = result ? pp_without_splices(pp, kind, spelling, length, spare, &spelling) : 0;
                        length = result && kind == C_TOKEN_UNKNOWN && spelling == spare ? pp_trailing_splice(pp, location, raw, spare, length) : length;
                    }
                }
                else if (kind == BUSTER_PP_KIND_INTEGER)
                {
                    uint8_t* spare = arena_push(scratch.arena, 24, 1);
                    result = spare != 0;
                    length = result ? pp_spelling(pp, kind, value, location, spare, &spelling) : 0;
                }
                spellings[k] = spelling;
                lengths[k] = length;
                firsts[k] = length != 0 ? spelling[0] : 0;
                lasts[k] = length != 0 ? spelling[length - 1] : 0;
                if (BUSTER_UNLIKELY(kind == BUSTER_PP_KIND_EMBED))
                {
                    // Its text goes out in pieces (pp_print_embed); its ends' characters give the spaces around it.
                    spellings[k] = (const uint8_t*)"";
                    lengths[k] = 1;
                    pp_embed_ends(pp, value, &firsts[k], &lasts[k]);
                }
            }
            if (stale)
            {
                // 2. Spaces, for the 64 tokens at once: line starts (a token that starts a line, or follows a pragma's
                // end, or is the output's first), whitespace, and the separation table on the class of the last
                // character before and of the first character after.
                const __m512i flags = _mm512_maskz_loadu_epi8(valid, out_flags + c);
                // Whole loads, which pp_print_spellings' two stores forward to; the lanes past `valid` count for no
                // token's space.
                const __m512i first_bytes = _mm512_load_si512(firsts);
                const __m512i last_bytes = _mm512_load_si512(lasts);
                const uint64_t starts = _mm512_mask_test_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_START_OF_LINE));
                const uint64_t spaces = _mm512_mask_test_epi8_mask(valid, flags, _mm512_set1_epi8(BUSTER_PP_SPACE));
                const __m512i after = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(after_low, first_bytes, after_high), _mm512_movepi8_mask(first_bytes),
                                                           _mm512_set1_epi8(BUSTER_PP_AFTER_WORD));
                __m512i before = _mm512_mask_mov_epi8(_mm512_permutex2var_epi8(before_low, last_bytes, before_high), _mm512_movepi8_mask(last_bytes),
                                                      _mm512_set1_epi8(BUSTER_PP_BEFORE_WORD));
                // An #embed's text ends in a number.
                const uint64_t numbers =
                    _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(C_TOKEN_NUMERIC_CONSTANT)) | _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8((char)BUSTER_PP_KIND_EMBED));
                before = _mm512_mask_mov_epi8(before, numbers, _mm512_set1_epi8(BUSTER_PP_BEFORE_NUMBER));
                // Each token's before-class is its previous token's: the classes move up a lane, the chunk's first
                // taking the class of the token before the chunk.
                const uint8_t carried = entry_kind == C_TOKEN_NUMERIC_CONSTANT || entry_kind == BUSTER_PP_KIND_EMBED ? BUSTER_PP_BEFORE_NUMBER
                                        : entry_last >= 0x80 ? BUSTER_PP_BEFORE_WORD
                                        : dollar_punctuation && entry_last == '$' ? 0 : pp_before_classes[entry_last];
                before = _mm512_mask_permutexvar_epi8(_mm512_set1_epi8((char)carried), ~1ull, previous_lanes, before);
                const __m512i pair = _mm512_add_epi8(_mm512_slli_epi16(before, 3), after);
                const uint64_t joins = _mm512_test_epi8_mask(_mm512_permutexvar_epi8(pair, separation), _mm512_set1_epi8(1));
                line_starts = starts | (pragma_ends << 1) | (uint64_t)entry_force_line | (uint64_t)(c == 0 && entry_line_start);
                spaced = (spaces | joins) & ~line_starts & valid;
                longs = 0;
                for (uint32_t q = 0; q < 4; q += 1)
                {
                    const __mmask16 lanes = (__mmask16)(valid >> (16 * q));
                    const __m512i length = _mm512_maskz_loadu_epi32(lanes, lengths + 16 * q);
                    longs |= (uint64_t)_mm512_mask_cmpge_epu32_mask(lanes, _mm512_sub_epi32(length, _mm512_set1_epi32(1)), _mm512_set1_epi32(64)) << (16 * q);
                }
                stale = false;
            }
            // 3. Copies. A batch of the tokens from `from` without a branch per token, their output offsets by a prefix
            // sum (pp_print_offsets), up to the first spelling past 64 bytes: with -P, of a chunk without a pragma end
            // or an #embed; with line markers, of a chunk without those or what pp_print_moves, pp_print_include_exits
            // or pp_print_token_lines would handle, and up to the first line start that isn't pp_print_place's common
            // case (pp_print_place_lines), whose line breaks are its gap. The token it stops at goes token by token, as
            // every other token does, and the batch goes on after it.
            const PPMarkers entry_marked = marked;
            const PPPrintLines entry_lines = lines;
            const uint64_t ahead = valid & ~_bzhi_u64(~0ull, from);
            const bool batchable = result && pragma_ends == 0 && embeds == 0 &&
                                   (!markers || ((unknowns | (line_spaces & ~line_starts)) == 0 && !(marked.include_exit != 0 && marked.include_exit->token < c + available) &&
                                                 !(silent < pp->silent_pragma_count && pp->silent_pragmas[silent].token < c + available)));
            const uint64_t stops = longs & ahead;
            const uint32_t stop = stops != 0 ? (uint32_t)__builtin_ctzll(stops) : 64;
            // With -P, a line start's one break, but the output's first token's; with markers, as many as it takes.
            const uint64_t batch_breaks = (line_starts & ahead & _bzhi_u64(~0ull, stop)) & ~(uint64_t)(markers ? 0 : line_start);
            // The tokens whose gap is one byte: a space, or with -P a break.
            const uint64_t ones = (markers ? 0 : batch_breaks) | spaced;
            uint32_t to = available;
            uint32_t batch_bytes = 0;
            bool printed = false;
            if (batchable)
            {
                if (markers)
                {
                    _mm512_store_si512(line_breaks, _mm512_setzero_si512());
                }
                const uint32_t placed = markers ? pp_print_place_lines(pp, &marked, &lines, directory, c, batch_breaks, line_breaks) : 64;
                to = placed < available ? placed : available;
                to = stop < to ? stop : to;
                batch_bytes = to > from ? pp_print_offsets(lengths + from, to - from, ones >> from, markers ? line_breaks + from : 0, offsets) : 0;
            }
            if (batch_bytes != 0)
            {
                if (used + batch_bytes > capacity)
                {
                    result = fwrite(buffer, 1, used, stream) == used;
                    used = 0;
                }
                // The gaps: spaces over the batch's bytes, then each line start's breaks, then the spellings at their
                // offsets, a masked load and store each (#649: the copies wrote each gap a byte at a time, which with
                // the general bookkeeping took 17 instructions a token on the 9700X). The spaces' stores are masked to
                // the batch, which also keeps clang from making them a call to memset.
                uint8_t* batch = buffer + used;
                for (uint32_t b = 0; result && b < batch_bytes; b += 64)
                {
                    _mm512_mask_storeu_epi8(batch + b, _bzhi_u64(~0ull, batch_bytes - b < 64 ? batch_bytes - b : 64), _mm512_set1_epi8(' '));
                }
                for (uint64_t bits = batch_breaks & _bzhi_u64(~0ull, to); result && bits != 0; bits &= bits - 1)
                {
                    const uint32_t k = (uint32_t)__builtin_ctzll(bits);
                    if (markers)
                    {
                        _mm_mask_storeu_epi8(batch + offsets[k - from] - line_breaks[k], (__mmask16)_bzhi_u32(~0u, line_breaks[k]), _mm_set1_epi8('\n'));
                    }
                    else
                    {
                        batch[offsets[k - from] - 1] = '\n';
                    }
                }
                // pp_print_unspliced's check, where a copy loads a backslash, or with trigraphs a '?' (`specials`
                // gives each byte back for those and something else for every other byte): a backslash followed by a
                // blank (the zero past a spelling of less than 64 bytes counts as one) or in the last lane may start a
                // line splice, and with trigraphs so may any '?'. An identifier's interned spelling has none, and a lane
                // checked in vain would only cost the general loop. Most batches have neither byte, so the rest of the
                // check waits for one, and each copy only lowers a running minimum of its bytes' differences from
                // their lookups, which is zero once one of them was found.
                __m512i nearest = _mm512_set1_epi8(-1);
                for (uint32_t k = from; result && k < to; k += 1)
                {
                    const __mmask64 bytes = _bzhi_u64(~0ull, lengths[k]);
                    const __m512i spelling = _mm512_maskz_loadu_epi8(bytes, spellings[k]);
                    _mm512_mask_storeu_epi8(batch + offsets[k - from], bytes, spelling);
                    nearest = _mm512_min_epu8(nearest, _mm512_xor_si512(_mm512_permutexvar_epi8(spelling, specials), spelling));
                }
                const uint64_t copied = valid & ~identifiers & ~_bzhi_u64(~0ull, from) & _bzhi_u64(~0ull, to);
                const bool found = _mm512_testn_epi8_mask(nearest, nearest) != 0;
                printed = result && (general || !found || pp_print_unspliced(spellings, lengths, copied, trigraphs));
                if (printed)
                {
                    used += batch_bytes;
                    line_start = false;
                    force_line = false;
                    previous_last = lasts[to - 1];
                    previous_kind = out_kinds[c + to - 1];
                    from = to;
                }
            }
            if (markers && !printed)
            {
                marked = entry_marked;
                lines = entry_lines;
            }
            // Token by token: the line start the batch stopped at (or started at), else every token left, once
            // pp_print_unspliced finds no possible splice in their spellings.
            const uint32_t last = batchable && to < available && to == from ? from + 1 : available;
            const bool spliced = batch_bytes != 0 && !printed;
            const uint64_t alone = valid & ~identifiers & ~_bzhi_u64(~0ull, from) & _bzhi_u64(~0ull, last);
            const bool unspliced = !spliced && (general || from == last || pp_print_unspliced(spellings, lengths, alone, trigraphs));
            general = general || !unspliced;
            stale = !unspliced;
            for (uint32_t k = unspliced ? from : last; result && k < last; k += 1)
            {
                const uint32_t i = c + k;
                const uint32_t length = lengths[k];
                const bool exited = markers && BUSTER_UNLIKELY(marked.include_exit != 0 && marked.include_exit->token <= i);
                if (exited)
                {
                    result = result && fwrite(buffer, 1, used, stream) == used && pp_print_include_exits(pp, &marked, i, &line_start, stream);
                    used = 0;
                    if (!result)
                    {
                        break;
                    }
                }
                if ((pragma_ends >> k) & 1)
                {
                    force_line = true;
                    continue;
                }
                // Room for the breaks, a space and the spelling, or a write of what the buffer holds.
                const uint64_t room = (uint64_t)length + 16 + 64;
                if (used + room > capacity)
                {
                    result = fwrite(buffer, 1, used, stream) == used;
                    used = 0;
                }
                const uint32_t moved = markers && !((line_starts >> k) & 1) &&
                                               BUSTER_UNLIKELY((out_flags[i] & BUSTER_PP_LINE_SPACE) || (silent < pp->silent_pragma_count && pp->silent_pragmas[silent].token <= i))
                                           ? pp_print_moves(pp, &marked, i, &silent)
                                           : BUSTER_PP_NONE;
                const bool moves = moved != BUSTER_PP_NONE;
                const uint32_t starts_line = (uint32_t)((line_starts >> k) & 1) | (uint32_t)moves | (uint32_t)exited;
                // A line start's break after something on the line; with markers, as many as pp_print_place puts.
                uint32_t breaks = starts_line & (uint32_t)!line_start;
                if (markers && starts_line)
                {
                    const uint32_t located = pp_print_location(pp, &marked, i);
                    uint32_t place = moves ? moved : located;
                    const uint32_t range = pp_directory_range(pp, directory, place);
                    if (range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE)
                    {
                        place = pp->ranges[range].base + pp_start_with_splices(pp->ranges[range].bytes, place - pp->ranges[range].base, pp->options.trigraphs);
                        bool placed = true;
                        breaks = pp_print_place(pp, &marked, range, place, pp_print_line(pp, &lines, range, place), line_start, &placed);
                        // The markers go through the buffer, or past it when they are longer than its room.
                        if (marked.length != 0 && used + marked.length + room > capacity)
                        {
                            result = result && fwrite(buffer, 1, used, stream) == used;
                            used = 0;
                        }
                        if (marked.length != 0 && marked.length + room <= capacity)
                        {
                            memcpy(buffer + used, marked.text, marked.length);
                            used += marked.length;
                        }
                        else if (marked.length != 0)
                        {
                            result = result && fwrite(marked.text, 1, marked.length, stream) == marked.length;
                        }
                        result = result && placed;
                        line_start = line_start || marked.length != 0;
                    }
                }
                if (BUSTER_LIKELY(breaks <= 1))
                {
                    buffer[used] = '\n';
                    used += breaks;
                }
                else
                {
                    memset(buffer + used, '\n', breaks);
                    used += breaks;
                }
                line_start = line_start || starts_line;
                force_line = false;
                buffer[used] = ' ';
                used += ((spaced >> k) & 1) & (length != 0) & (uint32_t)!moves & (uint32_t)!exited;
                // The spelling: one masked 64-byte copy, or more for a longer one (written straight out past the
                // buffer's room), or an #embed's text in pieces.
                const uint8_t* spelling = spellings[k];
                const __mmask64 head = _bzhi_u64(~0ull, length < 64 ? length : 64);
                bool direct = BUSTER_UNLIKELY(out_kinds[i] == BUSTER_PP_KIND_EMBED);
                if (direct)
                {
                    result = result && pp_print_embed(pp, out_values[i], false, buffer, &used, capacity, stream);
                }
                else
                {
                    _mm512_mask_storeu_epi8(buffer + used, head, _mm512_maskz_loadu_epi8(head, spelling));
                }
                if (BUSTER_UNLIKELY(!direct && length > 64))
                {
                    direct = used + length > capacity;
                    for (uint32_t b = 64; !direct && b < length; b += 64)
                    {
                        const __mmask64 bytes = _bzhi_u64(~0ull, length - b < 64 ? length - b : 64);
                        _mm512_mask_storeu_epi8(buffer + used + b, bytes, _mm512_maskz_loadu_epi8(bytes, spelling + b));
                    }
                    if (direct)
                    {
                        result = fwrite(buffer, 1, used, stream) == used && fwrite(spelling, 1, length, stream) == length;
                        used = 0;
                    }
                }
                used += direct ? 0 : length;
                if (BUSTER_UNLIKELY((unknowns >> k) & 1))
                {
                    marked.line += pp_print_token_lines(spelling, length);
                }
                line_start = false;
                previous_last = length != 0 ? lasts[k] : previous_last;
                previous_kind = out_kinds[i];
            }
            from = unspliced ? last : from;
        }
        arena_pop_to(scratch.arena, spare_mark);
    }
    if (result && markers && marked.include_exit != 0)
    {
        result = fwrite(buffer, 1, used, stream) == used && pp_print_include_exits(pp, &marked, count, &line_start, stream);
        used = 0;
    }
    if (result && !line_start)
    {
        buffer[used] = '\n';
        used += 1;
    }
    result = result && fwrite(buffer, 1, used, stream) == used;
    scratch_end(scratch);
    return result && ferror(stream) == 0;
}

static bool pp_print_vector(Preprocessor* pp, FILE* stream, bool markers)
{
#if !BUSTER_HOST_VECTOR
    return pp_print_scalar(pp, stream, markers);
#else
    return markers ? pp_print_vector_mode(pp, stream, true) : pp_print_vector_mode(pp, stream, false);
#endif
}

BUSTER_PREPROCESSOR_ENTRY bool pp_print(Preprocessor* pp, FILE* stream, bool markers)
{
#if !BUSTER_HOST_VECTOR
    return pp_print_scalar(pp, stream, markers);
#else
    return BUSTER_VECTOR_PATH(pp->vectorized) ? pp_print_vector(pp, stream, markers) : pp_print_scalar(pp, stream, markers);
#endif
}

BUSTER_PREPROCESSOR_ENTRY bool pp_parse_options(int count, char** arguments, PPOptions* options, Arena* arena, const char** input, const char** output, bool* markers)
{
    return pp_parse_options_inputs(count, arguments, options, arena, input, output, markers, NULL, NULL);
}

// Removes the directory pp_clang_fallback_links made, with its links.
static void pp_clang_remove_links(const char* links)
{
    DIR* directory = opendir(links);
    for (struct dirent* entry = directory != 0 ? readdir(directory) : 0; entry != 0; entry = readdir(directory))
    {
        char link[PATH_MAX];
        const bool dots = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        if (!dots && (size_t)snprintf(link, sizeof(link), "%s/%s", links, entry->d_name) < sizeof(link))
        {
            unlink(link);
        }
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    rmdir(links);
}

// What --buster-headers searches after buster's directory `ours`, for clang: a new directory, made from the template
// `links` (mkdtemp's, which gives it its name, where it ends in XXXXXX, and otherwise the name itself: a test's, in a
// directory of its own, whose clang runs the oracle cache keeps only when their arguments are the same from one test
// run to the next), with a link to each entry of clang's resource directory that `ours` hasn't. Returns whether it made
// it; if not, it leaves nothing.
static bool pp_clang_fallback_links(const char* ours, char* links)
{
    char resource[PATH_MAX];
    const size_t length = strlen(links);
    const bool template = length >= 6 && strcmp(links + length - 6, "XXXXXX") == 0;
    const bool made = pp_clang_resource_directory(resource, sizeof(resource), 0) && (template ? mkdtemp(links) != 0 : mkdir(links, 0700) == 0);
    DIR* directory = made ? opendir(resource) : 0;
    bool result = directory != 0;
    for (struct dirent* entry = result ? readdir(directory) : 0; result && entry != 0; entry = readdir(directory))
    {
        char target[PATH_MAX];
        char shadow[PATH_MAX];
        char link[PATH_MAX];
        struct stat stats;
        const bool dots = strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0;
        result = (size_t)snprintf(target, sizeof(target), "%s/%s", resource, entry->d_name) < sizeof(target) &&
                 (size_t)snprintf(shadow, sizeof(shadow), "%s/%s", ours, entry->d_name) < sizeof(shadow) &&
                 (size_t)snprintf(link, sizeof(link), "%s/%s", links, entry->d_name) < sizeof(link);
        if (result && !dots && stat(shadow, &stats) != 0)
        {
            result = symlink(target, link) == 0;
        }
    }
    if (directory != 0)
    {
        closedir(directory);
    }
    if (made && !result)
    {
        pp_clang_remove_links(links);
    }
    return result;
}

// clang's arguments for ours: `arguments` (`count` of them), from which `options` were read, in a new array with a null
// at its end (one block, with the names it holds, for free), and their count in `*clang_count`; null if it can't be
// made. Our own options are left out; any other option's value is copied as it is, whatever it spells. With
// --buster-headers, and without -nostdinc or
// -nobuiltininc (which leave out buster's headers and clang's alike), clang gets ours' search instead of the
// directories' options, from a preprocessor initialized with `options`: each directory pp_dedupe_directories keeps
// (whose decisions, by identity and a system directory over a user's, clang's links would hide from it) with its
// option, -iquote, -I, -isystem or -idirafter, and in clang's resource directory's place, when it is kept, a new
// directory of links made from `links`, a template or a name (pp_clang_fallback_links, with buster's directory in
// `ours`, PATH_MAX bytes). -nobuiltininc comes first, and clang adds the standard directories, /usr/local/include and
// /usr/include, after the -isystem ones, as ours has them. `*linked` says whether the links were made, for
// pp_clang_remove_links.
static __attribute__((unused)) char** pp_clang_arguments(const PPOptions* options, int count, char** arguments, char* ours, char* links, bool* linked, int* clang_count)
{
    const bool search = options->buster_headers && !options->no_standard_includes && !options->no_builtin_includes;
    // Ours' search, as pp_initialize makes it, without its diagnostics.
    static InternTable table;
    static Preprocessor pp;
    PPOptions quiet = *options;
    quiet.quiet = true;
    const bool found = search && pp_buster_headers_directory(ours, PATH_MAX, 0);
    const bool interned = found && intern_initialize(&table);
    const bool initialized = interned && pp_initialize(&pp, &quiet, &table) && !pp.fatal;
    const uint32_t directories = initialized ? pp.directory_count : 0;
    uint64_t bytes = 0;
    for (uint32_t d = 0; d < directories; d += 1)
    {
        bytes += (uint64_t)pp.directory_lengths[d] + 1;
    }
    const uint64_t slots = (uint64_t)count + 2 * (uint64_t)directories + 2;
    char** result = !search || initialized ? malloc(slots * sizeof(char*) + bytes) : 0;
    char* names = result != 0 ? (char*)(result + slots) : 0;
    bool ready = result != 0;
    int n = 0;
    *linked = false;
    for (int i = 0; ready && i < count; i += 1)
    {
        const char* argument = arguments[i];
        const bool value = pp_option_takes_value(argument) && i + 1 < count;
        // With ours' search, the directories' options are in it.
        const bool directory = search && (strncmp(argument, "-I", 2) == 0 || strcmp(argument, "-isystem") == 0 || strcmp(argument, "-iquote") == 0 ||
                                          strcmp(argument, "-idirafter") == 0);
        if (!directory && !pp_own_option_with_value(argument) && strcmp(argument, "--buster-headers") != 0 && strcmp(argument, "--scalar") != 0 &&
            strcmp(argument, "--clang-features") != 0)
        {
            result[n] = arguments[i];
            n += 1;
            if (value)
            {
                result[n] = arguments[i + 1];
                n += 1;
            }
        }
        i += value;
    }
    if (ready && search)
    {
        result[n] = "-nobuiltininc";
        n += 1;
        // Past a standard directory, the system ones are -idirafter's. When -isystem took the standard ones' places
        // instead, every later one is an -isystem in clang's order too.
        bool after = false;
        for (uint32_t d = 0; ready && d < directories; d += 1)
        {
            const uint8_t system = pp.directory_system[d];
            const bool fallback = d == pp.fallback_directory;
            after = after || system == BUSTER_PP_EXTERN_C_FILE;
            *linked = *linked || (fallback && pp_clang_fallback_links(ours, links));
            ready = !fallback || *linked;
            if (ready && system != BUSTER_PP_EXTERN_C_FILE)
            {
                memcpy(names, pp.directories[d], (size_t)pp.directory_lengths[d] + 1);
                result[n] = d < pp.angled_first ? "-iquote" : system == BUSTER_PP_USER_FILE ? "-I" : after ? "-idirafter" : "-isystem";
                result[n + 1] = fallback ? links : names;
                n += 2;
                names += pp.directory_lengths[d] + 1;
            }
        }
    }
    if (search && !found)
    {
        fprintf(stderr, "--buster-headers: no src/buster/include beside the executable's build directory\n");
    }
    else if (search && (!ready || !initialized))
    {
        fprintf(stderr, "--buster-headers: can't make clang's search, with links to clang's resource headers in %s\n", links);
    }
    if (interned)
    {
        pp_release(&pp);
        intern_release(&table);
    }
    if (ready)
    {
        result[n] = 0;
        *clang_count = n;
    }
    else
    {
        free(result);
        result = 0;
    }
    return result;
}

// Runs clang with `arguments` (`count` of them, as pp_clang_arguments gives them) and -E -P -o `output` `file`, as a
// process of its own without CPATH and C_INCLUDE_PATH, and waits for it: benchmark-preprocessor's clang runs. Its command
// is an array of its own count (#255's review: a fixed one of 256 dropped the arguments past it). Returns whether clang
// exited with status 0, naming the file if it didn't.
static __attribute__((unused)) bool pp_clang_preprocess(int count, char** arguments, const char* output, const char* file)
{
    char** command = calloc((size_t)count + 7, sizeof(char*));
    bool result = command != 0;
    int status = 0;
    if (result)
    {
        int n = 0;
        command[n++] = "clang";
        for (int i = 0; i < count; i += 1)
        {
            command[n++] = arguments[i];
        }
        command[n++] = "-E";
        command[n++] = "-P";
        command[n++] = "-o";
        command[n++] = (char*)output;
        command[n++] = (char*)file;
        command[n] = 0;
        const pid_t child = fork();
        if (child == 0)
        {
            unsetenv("CPATH");
            unsetenv("C_INCLUDE_PATH");
            execvp("clang", command);
            _exit(127);
        }
        pid_t waited = -1;
        do
        {
            waited = child > 0 ? waitpid(child, &status, 0) : -1;
        }
        while (waited < 0 && child > 0 && errno == EINTR);
        result = waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    if (!result)
    {
        fprintf(stderr, "clang -E failed on %s (wait status %d)\n", file, status);
    }
    free(command);
    return result;
}
