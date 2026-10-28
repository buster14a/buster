// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #pragma clang section (#619), as clang 23 reads and applies it. The preprocessor retains the line as written; this reads
// it as Parse's PragmaClangSectionHandler does: pairs of KIND = "name", KIND one of bss, data, rodata, text and relro,
// adjacent ordinary strings joined, an empty name clearing its kind, a setting before an error in the line kept. clang
// reads the first kind and every '=' macro-expanded and the rest as written, so a line with a name in those places that
// was a macro's anywhere in the unit is left to the caller's refusal: the retained line can't show what it expanded to.
// So is an error at the line's end, whose place the retained line doesn't keep.
//
// A setting names a section for the definitions after it: text the functions', and bss, data, rodata and relro those
// of variables with static storage, each without a section attribute of its own (or inherited). The emission refuses
// such a definition by name until it publishes sections (CheckInputSections.definitions).
//
// Sema's UnifySection gives each section name one set of flags, its first use's: a setting, or a section attribute of a
// function's declaration or of a variable's definition with static storage (a tentative one at the unit's end, at its
// last tentative declaration, unless the unit defines it). A later use with other flags is clang's section type
// conflict, at the use. The uses come in that order from the walk (check_input_section_declaration) and the settings,
// and check_input_section_conflicts takes them in turn after it: the first use of a name decides, so the pass is
// ordered by what it computes, over a unit's few section events, and costs nothing in a unit without any.

#define BUSTER_CHECK_SECTION_INPUT_IMPLEMENTATION 1

// The kinds, as clang's PragmaClangSectionKind numbers them.
#define CHECK_SECTION_BSS 1
#define CHECK_SECTION_DATA 2
#define CHECK_SECTION_RODATA 3
#define CHECK_SECTION_TEXT 4
#define CHECK_SECTION_RELRO 5
#define CHECK_SECTION_KINDS 6

// A section's flags, as Sema's ASTContext::PragmaSectionFlag has them.
#define CHECK_SECTION_READ 0x01u
#define CHECK_SECTION_WRITE 0x02u
#define CHECK_SECTION_EXECUTE 0x04u
#define CHECK_SECTION_ZERO_INIT 0x10u

// CheckInputSectionEvent.flags.
#define CHECK_SECTION_EVENT_FUNCTION 0x01u
#define CHECK_SECTION_EVENT_DEFINITION 0x02u
#define CHECK_SECTION_EVENT_TENTATIVE 0x04u
#define CHECK_SECTION_EVENT_CONSTANT 0x08u
// A definition of its object came before it (CheckInputReader.definition_seen).
#define CHECK_SECTION_EVENT_DEFINED_BEFORE 0x10u
#define CHECK_SECTION_EVENT_NAMED 0x20u

static const char* const check_section_kind_names[CHECK_SECTION_KINDS] = { "invalid", "bss", "data", "rodata", "text", "relro" };
static const uint8_t check_section_kind_flags[CHECK_SECTION_KINDS] =
{
    0,
    CHECK_SECTION_READ | CHECK_SECTION_WRITE | CHECK_SECTION_ZERO_INIT,
    CHECK_SECTION_READ | CHECK_SECTION_WRITE,
    CHECK_SECTION_READ,
    CHECK_SECTION_READ | CHECK_SECTION_EXECUTE,
    CHECK_SECTION_READ,
};

// One KIND = "name" of a line: its pragma, the parser token the pragma comes before, and its kind's token in the output,
// where clang places what it reports of it.
struct CheckInputSectionSetting
{
    String8 name;
    uint32_t pragma;
    uint32_t position;
    uint32_t source;
    uint8_t kind;
};

// A declaration the settings and the conflicts read: its name's token, the token after its declarator (and
// initializer), and its own section attribute's name (CHECK_SECTION_EVENT_NAMED).
struct CheckInputSectionEvent
{
    String8 name;
    uint32_t token;
    uint32_t position;
    uint32_t object;
    uint8_t flags;
};

// An error of a line (`pragma` its pragma) or of a declaration's section attribute (`pragma` BUSTER_CHECK_NONE): its
// report's token, the output token clang reports at, and clang's message.
typedef struct CheckInputSectionDiagnostic CheckInputSectionDiagnostic;
struct CheckInputSectionDiagnostic
{
    String8 text;
    uint32_t pragma;
    uint32_t token;
    uint32_t source;
};

struct CheckInputSections
{
    CheckInputSectionSetting* settings;
    uint32_t setting_count;
    CheckInputSectionDiagnostic* diagnostics;
    uint32_t diagnostic_count;
    uint32_t diagnostic_capacity;
    // The definitions a setting places, their names' tokens ascending: the emission refuses the first it emits, by name.
    uint32_t* definitions;
    uint32_t definition_count;
};

static const char check_section_kind_message[] = "expected one of [bss|data|rodata|text|relro] section kind in '#pragma clang section'";
static const char check_section_string_message[] = "expected string literal in pragma clang section";

// The name the ordinary string literals from output token `source` to `end` (exclusive) spell, joined, into the arena.
// Returns false if one doesn't decode, as clang's StringLiteralParser's errors leave the line.
static bool check_input_section_string(CheckInputReader* reader, uint32_t source, uint32_t end, String8* name)
{
    const PPTokens* output = &reader->pp->output;
    const uint64_t mark = reader->scratch->length;
    const uint32_t count = end - source;
    CheckStringPart* parts = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckStringPart, count);
    bool valid = parts != 0;
    reader->output->resource |= !valid;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        const uint32_t location = output->locations[source + i];
        const uint32_t value = output->values[source + i];
        valid = value >= location;
        uint8_t* buffer = valid ? arena_push(reader->scratch, (uint64_t)value - location + 1, 1) : 0;
        reader->output->resource |= valid && buffer == 0;
        valid &= buffer != 0;
        if (valid)
        {
            parts[i] = (CheckStringPart){ 0 };
            parts[i].length = pp_spelling(reader->pp, C_TOKEN_STRING_LITERAL, value, location, buffer, &parts[i].bytes);
        }
    }
    if (valid)
    {
        const CheckStringLiteral decoded = check_literal_strings(parts, count, reader->pp->options.language.standard >= 2023, reader->arena);
        reader->output->resource |= decoded.resource;
        valid = decoded.valid && decoded.byte_count != 0;
        if (valid)
        {
            *name = (String8){ .pointer = (char*)decoded.bytes, .length = decoded.byte_count - 1 };
        }
    }
    reader->scratch->length = mark;
    return valid;
}

// Records an error of a line or a declaration: its message (copied into the arena) at output token `source`.
static void check_input_section_diagnose(CheckInputReader* reader, CheckInputSections* sections, uint32_t pragma, uint32_t token, uint32_t source, const char* format, ...)
{
    if (sections->diagnostic_count == sections->diagnostic_capacity)
    {
        const uint32_t capacity = sections->diagnostic_capacity != 0 ? sections->diagnostic_capacity * 2 : 4;
        CheckInputSectionDiagnostic* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSectionDiagnostic, capacity);
        reader->output->resource |= grown == 0;
        if (grown != 0)
        {
            if (sections->diagnostic_count != 0)
            {
                memcpy(grown, sections->diagnostics, (uint64_t)sections->diagnostic_count * sizeof(*grown));
            }
            sections->diagnostics = grown;
            sections->diagnostic_capacity = capacity;
        }
    }
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    char* copy = length > 0 && sections->diagnostic_count < sections->diagnostic_capacity ? arena_push(reader->arena, (uint64_t)length + 1, 1) : 0;
    reader->output->resource |= length > 0 && copy == 0 && sections->diagnostic_count < sections->diagnostic_capacity;
    if (copy != 0)
    {
        const uint32_t kept = (uint32_t)length < sizeof(text) ? (uint32_t)length : (uint32_t)sizeof(text) - 1;
        memcpy(copy, text, kept);
        copy[kept] = 0;
        sections->diagnostics[sections->diagnostic_count++] = (CheckInputSectionDiagnostic)
        {
            .text = { .pointer = copy, .length = kept },
            .pragma = pragma,
            .token = token,
            .source = source,
        };
    }
}

// One line, from output token `source` (its '#') to `end` (its PRAGMA_END), at parser position `position`: its settings
// into `settings` (`*count` of them; NULL counts them alone, without decoding their names), and what it is to the
// caller: 1 handled, 3 an error at clang's place (recorded in `sections`, when it isn't NULL), 0 left to its refusal.
static uint8_t check_input_section_line(CheckInputReader* reader, uint32_t pragma, uint32_t source, uint32_t end, uint32_t position,
                                        uint64_t** macros, CheckInputSections* sections, CheckInputSectionSetting* settings, uint32_t* count)
{
    const PPTokens* output = &reader->pp->output;
    uint8_t result = 1;
    uint32_t error = BUSTER_CHECK_NONE;
    uint8_t error_kind = 0;
    const char* message = 0;
    bool expanded = true;
    uint32_t t = source + 4;
    while (result == 1 && t < end)
    {
        // The kind, an identifier (not a keyword: clang's tok::identifier), the first one read expanded.
        const IdentId kind_name = check_input_pragma_name(reader, t);
        uint8_t kind = 0;
        for (uint8_t k = 1; output->kinds[t] == C_TOKEN_RAW_IDENTIFIER && k < CHECK_SECTION_KINDS; k += 1)
        {
            kind = pp_token_is_name(reader->pp, output, t, end, check_section_kind_names[k]) ? k : kind;
        }
        if (expanded && kind_name != 0 && check_input_pragma_macro(reader, macros, kind_name))
        {
            result = 0;
        }
        else if (kind == 0)
        {
            result = 3;
            error = t;
            message = check_section_kind_message;
        }
        // The '=', read expanded.
        const uint32_t equal = t + 1;
        if (result == 1)
        {
            const IdentId equal_name = equal < end ? check_input_pragma_name(reader, equal) : 0;
            if (equal == end || (equal_name != 0 && check_input_pragma_macro(reader, macros, equal_name)))
            {
                result = 0;
            }
            else if (output->kinds[equal] != C_TOKEN_EQUAL)
            {
                result = 3;
                error = equal;
                error_kind = kind;
            }
        }
        // The name, ordinary string literals read as written; the token after them is the next kind, read so too.
        const uint32_t first = equal + 1;
        if (result == 1 && first == end)
        {
            result = 0;
        }
        else if (result == 1 && output->kinds[first] != C_TOKEN_STRING_LITERAL)
        {
            result = 3;
            error = first;
            message = check_section_string_message;
        }
        uint32_t last = first;
        while (result == 1 && last < end && output->kinds[last] == C_TOKEN_STRING_LITERAL)
        {
            last += 1;
        }
        String8 name = { 0 };
        if (result == 1 && settings != 0 && !check_input_section_string(reader, first, last, &name))
        {
            result = 0;
        }
        if (result == 1)
        {
            if (settings != 0)
            {
                settings[*count] = (CheckInputSectionSetting)
                {
                    .name = name,
                    .pragma = pragma,
                    .position = position,
                    .source = t,
                    .kind = kind,
                };
            }
            *count += 1;
        }
        t = last;
        expanded = false;
    }
    if (result == 3 && sections != 0)
    {
        if (message != 0)
        {
            check_input_section_diagnose(reader, sections, pragma, position, error, "%s", message);
        }
        else
        {
            check_input_section_diagnose(reader, sections, pragma, position, error, "expected '=' following '#pragma clang section %s'",
                                         check_section_kind_names[error_kind]);
        }
    }
    return result;
}

// The unit's lines, by two scans of the retained pragmas (a count, then the settings), into CheckInput.sections and the
// handled mask: 1 for a line read, 3 for an error at clang's place (`messages` CHECK_INPUT_SECTION), 0 for one left to
// the caller's refusal. A unit without one gets none here. Returns false for want of memory, with both arenas as they
// were.
static bool check_input_section_pragmas(CheckInputReader* reader, uint8_t* handled, uint8_t* messages)
{
    const uint64_t output_mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    const uint32_t pragmas = reader->parser->tokens.pragma_count;
    const PPTokens* output = &reader->pp->output;
    uint64_t* macros = 0;
    uint32_t lines = 0;
    uint32_t capacity = 0;
    uint32_t previous_end = 0;
    uint32_t previous_token = 0;
    for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
    {
        uint32_t source = 0;
        uint32_t end = 0;
        uint32_t token = 0;
        if (check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token) && end - source >= 4 &&
            pp_token_is_name(reader->pp, output, source + 2, end, "clang") && pp_token_is_name(reader->pp, output, source + 3, end, "section"))
        {
            lines += 1;
            check_input_section_line(reader, pragma, source, end, token, &macros, 0, 0, &capacity);
        }
    }
    bool valid = !reader->output->resource;
    if (valid && lines != 0)
    {
        CheckInputSections* sections = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSections, 1);
        CheckInputSectionSetting* settings = sections != 0 && capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSectionSetting, capacity) : 0;
        valid = sections != 0 && (capacity == 0 || settings != 0);
        if (valid)
        {
            *sections = (CheckInputSections){ .settings = settings };
            previous_end = 0;
            previous_token = 0;
            for (uint32_t pragma = 0; pragma < pragmas; pragma += 1)
            {
                uint32_t source = 0;
                uint32_t end = 0;
                uint32_t token = 0;
                if (check_input_retained_pragma(reader, pragma, &previous_end, &previous_token, &source, &end, &token) && end - source >= 4 &&
                    pp_token_is_name(reader->pp, output, source + 2, end, "clang") && pp_token_is_name(reader->pp, output, source + 3, end, "section"))
                {
                    const uint8_t line = check_input_section_line(reader, pragma, source, end, token, &macros, sections, settings, &sections->setting_count);
                    handled[pragma] = line;
                    messages[pragma] = line == 3 ? (uint8_t)CHECK_INPUT_SECTION : messages[pragma];
                }
            }
            valid = !reader->output->resource;
            reader->output->sections = valid ? sections : 0;
            reader->section_record_from = valid && sections->setting_count != 0 && settings[0].position < reader->section_record_from ? settings[0].position :
                                          reader->section_record_from;
        }
    }
    reader->output->resource |= !valid;
    if (!valid)
    {
        reader->arena->length = output_mark;
    }
    reader->scratch->length = scratch_mark;
    return valid;
}

// The walk's declarator `name`, ended at token `position` (after its initializer, if any): an event for the settings
// and the conflicts when it has a section attribute of its own, or, from the first setting or such an attribute on, when
// it defines a function or declares a variable with static storage. The walk calls it from there on alone
// (CheckInputReader.section_record_from, which the attribute reader lowers to each section attribute's token). A lane
// leaves a unit with an event to source order.
static void check_input_section_declaration(CheckInputReader* reader, uint32_t name, uint32_t position, const CheckInputAttributes* attributes,
                                            bool function, bool body, bool static_storage, TypeId type)
{
    const CheckInputAttribute* attribute = NULL;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* record = attributes->records + i;
        attribute = record->kind == CHECK_ATTRIBUTE_SECTION && !record->ignored ? record : attribute;
    }
    const bool recording = position >= reader->section_record_from;
    const bool event = attribute != NULL || (recording && (function ? body : static_storage));
    if (event && reader->lane)
    {
        check_input_lane_refuse(reader, true, "a section attribute or a '#pragma clang section'");
    }
    else if (event)
    {
        if (reader->section_event_count == reader->section_event_capacity)
        {
            const uint32_t capacity = reader->section_event_capacity != 0 ? reader->section_event_capacity * 2 : 64;
            CheckInputSectionEvent* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSectionEvent, capacity);
            reader->output->resource |= grown == 0;
            if (grown != 0)
            {
                if (reader->section_event_count != 0)
                {
                    memcpy(grown, reader->section_events, (uint64_t)reader->section_event_count * sizeof(*grown));
                }
                reader->section_events = grown;
                reader->section_event_capacity = capacity;
            }
        }
        if (reader->section_event_count < reader->section_event_capacity)
        {
            const bool tentative = !function && reader->output->tentative[name];
            const bool defined = function ? body : reader->defined[name];
            reader->section_events[reader->section_event_count++] = (CheckInputSectionEvent)
            {
                .name = attribute != NULL ? attribute->text : (String8){ 0 },
                .token = name,
                .position = position,
                .flags = (uint8_t)((function ? CHECK_SECTION_EVENT_FUNCTION : 0) | (defined ? CHECK_SECTION_EVENT_DEFINITION : 0) |
                                   (tentative ? CHECK_SECTION_EVENT_TENTATIVE : 0) | (attribute != NULL ? CHECK_SECTION_EVENT_NAMED : 0) |
                                   (!function && (type & BUSTER_TYPE_CONST) != 0 ? CHECK_SECTION_EVENT_CONSTANT : 0)),
            };
        }
    }
}

// A section name's first use (check_input_section_conflicts): its flags, and the setting it was (`event`
// BUSTER_CHECK_NONE) or the walk's event.
typedef struct CheckSectionUse CheckSectionUse;
struct CheckSectionUse
{
    String8 name;
    uint32_t setting;
    uint32_t event;
    uint8_t flags;
    bool used;
};

// The slot of `name` in `table` (`capacity` slots, a power of two): its use, or the empty slot it would take.
static CheckSectionUse* check_section_use(CheckSectionUse* table, uint64_t capacity, String8 name)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint64_t i = 0; i < name.length; i += 1)
    {
        hash = (hash ^ (uint8_t)name.pointer[i]) * UINT64_C(1099511628211);
    }
    uint64_t slot = hash & (capacity - 1);
    while (table[slot].used && !(table[slot].name.length == name.length && (name.length == 0 || memcmp(table[slot].name.pointer, name.pointer, name.length) == 0)))
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return table + slot;
}

// The name of the walk's event `e`'s declaration, for clang's 'name'.
static String8 check_input_section_declaration_name(const CheckInputReader* reader, uint32_t e)
{
    const IdentId name = reader->parser->tokens.names[reader->section_events[e].token];
    return (String8){ .pointer = (char*)intern_bytes(reader->pp->table, name), .length = intern_length(reader->pp->table, name) };
}

// A use of `name` with `flags`, as UnifySection takes it: true if it is the name's first use or has its flags; false,
// reporting clang's conflict, if not. `setting` is the use's setting (`event` BUSTER_CHECK_NONE) or else its event.
static bool check_input_section_unify(CheckInputReader* reader, CheckInputSections* sections, CheckSectionUse* table, uint64_t capacity, String8 name,
                                      uint8_t flags, uint32_t setting, uint32_t event)
{
    CheckSectionUse* use = check_section_use(table, capacity, name);
    const bool result = !use->used || use->flags == flags;
    if (!use->used)
    {
        *use = (CheckSectionUse){ .name = name, .setting = setting, .event = event, .flags = flags, .used = true };
    }
    else if (!result)
    {
        CheckInput* input = reader->output;
        char prior[320];
        if (use->event != BUSTER_CHECK_NONE)
        {
            const String8 declared = check_input_section_declaration_name(reader, use->event);
            snprintf(prior, sizeof(prior), "'%.*s'", (int)declared.length, declared.pointer);
        }
        else
        {
            snprintf(prior, sizeof(prior), "a prior #pragma section");
        }
        // A token has one report (CheckInput.report_capacity): a line's report at the setting's token that comes after
        // this one, the walk's of the line it stopped at among them, becomes this one, and an earlier one stands.
        const uint32_t token = event == BUSTER_CHECK_NONE ? sections->settings[setting].position : reader->section_events[event].token;
        uint32_t existing = BUSTER_CHECK_NONE;
        for (uint32_t r = 0; r < input->report_count; r += 1)
        {
            existing = input->reports[r].token == token ? r : existing;
        }
        if (event == BUSTER_CHECK_NONE)
        {
            const CheckInputSectionSetting* at = sections->settings + setting;
            check_input_section_diagnose(reader, sections, at->pragma, at->position, at->source, "this causes a section type conflict with %s", prior);
            if (existing != BUSTER_CHECK_NONE && (input->reports[existing].pragma == 0 || input->reports[existing].pragma - 1 >= at->pragma))
            {
                input->reports[existing].pragma = at->pragma + 1;
                input->reports[existing].message = CHECK_INPUT_SECTION;
            }
            else if (existing == BUSTER_CHECK_NONE)
            {
                const uint32_t before = input->report_count;
                check_input_report(reader, at->position, CHECK_INPUT_SECTION);
                if (input->report_count != before)
                {
                    input->reports[before].pragma = at->pragma + 1;
                }
            }
        }
        else if (existing == BUSTER_CHECK_NONE)
        {
            const String8 declared = check_input_section_declaration_name(reader, event);
            check_input_section_diagnose(reader, sections, BUSTER_CHECK_NONE, token, reader->parser->tokens.sources[token],
                                         "'%.*s' causes a section type conflict with %s", (int)declared.length, declared.pointer, prior);
            check_input_report(reader, token, CHECK_INPUT_SECTION);
        }
    }
    return result;
}

// After the walk: the uses of section names in clang's order (the settings and the walk's events by position, then
// the tentative definitions' at the unit's end, in their first declarations' order), each conflict reported where clang
// reports it, and the definitions the settings place. A setting after the walk's first report doesn't count, nor the
// unit's end after one.
static void check_input_section_conflicts(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    const Parser* parser = reader->parser;
    const uint32_t events = reader->section_event_count;
    CheckInputSections* sections = input->sections;
    if (sections == 0 && events != 0 && !input->resource)
    {
        sections = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputSections, 1);
        input->resource |= sections == 0;
        if (sections != 0)
        {
            *sections = (CheckInputSections){ 0 };
            input->sections = sections;
        }
    }
    if (sections != 0 && !input->resource)
    {
        const uint64_t mark = reader->scratch->length;
        // Where the walk stopped: its first report's token, and its pragma when a line stopped it (the pragmas at that
        // token after it come after its error).
        uint32_t stop = parser->tokens.count + 1;
        uint32_t stop_pragma = BUSTER_CHECK_NONE;
        for (uint32_t r = 0; r < input->report_count; r += 1)
        {
            const bool earlier = input->reports[r].token < stop;
            stop_pragma = earlier ? (input->reports[r].pragma != 0 ? input->reports[r].pragma - 1 : BUSTER_CHECK_NONE) : stop_pragma;
            stop = earlier ? input->reports[r].token : stop;
        }
        const bool complete = input->report_count == 0;
        uint32_t objects = 1;
        for (uint32_t e = 0; e < events; e += 1)
        {
            const uint32_t object = reader->objects[reader->section_events[e].token];
            objects = object + 1 > objects ? object + 1 : objects;
        }
        uint64_t capacity = 4;
        while (capacity < 2 * ((uint64_t)sections->setting_count + events) + 2)
        {
            capacity *= 2;
        }
        CheckSectionUse* table = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckSectionUse, capacity);
        // Each object's latest event with a section attribute, its first and last tentative definitions, the attributed
        // event its last one had, and whether a definition of it was in the walk's events.
        uint32_t* attributed = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, (uint64_t)objects * 4);
        uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, objects);
        uint32_t* placed = events != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, events) : 0;
        const bool valid = table != 0 && attributed != 0 && defined != 0 && (events == 0 || placed != 0);
        input->resource |= !valid;
        if (valid)
        {
            memset(table, 0, capacity * sizeof(*table));
            memset(attributed, 0xff, (uint64_t)objects * 4 * sizeof(uint32_t));
            memset(defined, 0, objects);
            uint32_t* tentative_first = attributed + objects;
            uint32_t* tentative_last = tentative_first + objects;
            uint32_t* tentative_attributed = tentative_last + objects;
            uint32_t active[CHECK_SECTION_KINDS];
            for (uint32_t k = 0; k < CHECK_SECTION_KINDS; k += 1)
            {
                active[k] = BUSTER_CHECK_NONE;
            }
            uint32_t count = 0;
            uint32_t s = 0;
            for (uint32_t e = 0; e <= events && !input->resource; e += 1)
            {
                // The settings before the event (all that are left, after the last), up to the walk's first report.
                const uint32_t position = e < events ? reader->section_events[e].position : UINT32_MAX;
                for (; s < sections->setting_count && sections->settings[s].position <= position &&
                       (sections->settings[s].position < stop || (sections->settings[s].position == stop && sections->settings[s].pragma <= stop_pragma)); s += 1)
                {
                    const CheckInputSectionSetting* setting = sections->settings + s;
                    if (setting->name.length == 0)
                    {
                        active[setting->kind] = BUSTER_CHECK_NONE;
                    }
                    else if (check_input_section_unify(reader, sections, table, capacity, setting->name, check_section_kind_flags[setting->kind], s, BUSTER_CHECK_NONE))
                    {
                        active[setting->kind] = s;
                    }
                }
                if (e < events)
                {
                    const CheckInputSectionEvent* event = reader->section_events + e;
                    const uint32_t object = reader->objects[event->token];
                    const bool named = (event->flags & CHECK_SECTION_EVENT_NAMED) != 0;
                    const uint32_t effective = named ? e : attributed[object];
                    attributed[object] = named && object != 0 ? e : attributed[object];
                    const bool data = active[CHECK_SECTION_BSS] != BUSTER_CHECK_NONE || active[CHECK_SECTION_DATA] != BUSTER_CHECK_NONE ||
                                      active[CHECK_SECTION_RODATA] != BUSTER_CHECK_NONE || active[CHECK_SECTION_RELRO] != BUSTER_CHECK_NONE;
                    if ((event->flags & CHECK_SECTION_EVENT_FUNCTION) != 0)
                    {
                        if (named)
                        {
                            check_input_section_unify(reader, sections, table, capacity, event->name, CHECK_SECTION_READ | CHECK_SECTION_EXECUTE,
                                                      BUSTER_CHECK_NONE, e);
                        }
                        if ((event->flags & CHECK_SECTION_EVENT_DEFINITION) != 0 && effective == BUSTER_CHECK_NONE && active[CHECK_SECTION_TEXT] != BUSTER_CHECK_NONE)
                        {
                            placed[count++] = event->token;
                        }
                    }
                    else if ((event->flags & CHECK_SECTION_EVENT_DEFINITION) != 0)
                    {
                        defined[object] = 1;
                        if (effective != BUSTER_CHECK_NONE)
                        {
                            const uint8_t flags = (event->flags & CHECK_SECTION_EVENT_CONSTANT) != 0 ? CHECK_SECTION_READ : CHECK_SECTION_READ | CHECK_SECTION_WRITE;
                            check_input_section_unify(reader, sections, table, capacity, reader->section_events[effective].name, flags, BUSTER_CHECK_NONE, e);
                        }
                        else if (data)
                        {
                            placed[count++] = event->token;
                        }
                    }
                    else if ((event->flags & CHECK_SECTION_EVENT_TENTATIVE) != 0)
                    {
                        tentative_first[object] = tentative_first[object] == BUSTER_CHECK_NONE ? e : tentative_first[object];
                        tentative_last[object] = e;
                        tentative_attributed[object] = effective;
                        if (effective == BUSTER_CHECK_NONE && data)
                        {
                            placed[count++] = event->token;
                        }
                    }
                }
            }
            // A tentative definition's attribute at the unit's end, where its last one stands for the definition, if
            // the unit has none (before the events, CheckInputReader.definition_seen says).
            for (uint32_t e = 0; complete && e < events && !input->resource; e += 1)
            {
                const uint32_t object = reader->objects[reader->section_events[e].token];
                const uint32_t last = object != 0 && tentative_first[object] == e ? tentative_last[object] : BUSTER_CHECK_NONE;
                if (last != BUSTER_CHECK_NONE && defined[object] == 0 && !reader->definition_seen[reader->section_events[last].token] &&
                    tentative_attributed[object] != BUSTER_CHECK_NONE)
                {
                    const uint8_t flags = (reader->section_events[last].flags & CHECK_SECTION_EVENT_CONSTANT) != 0 ? CHECK_SECTION_READ :
                                                                                                                  CHECK_SECTION_READ | CHECK_SECTION_WRITE;
                    check_input_section_unify(reader, sections, table, capacity, reader->section_events[tentative_attributed[object]].name, flags,
                                              BUSTER_CHECK_NONE, last);
                }
            }
            // The walk's events are in source order; keep the tokens ascending anyway: the emission refuses the first it emits.
            for (uint32_t i = 1; i < count; i += 1)
            {
                const uint32_t token = placed[i];
                uint32_t j = i;
                for (; j > 0 && placed[j - 1] > token; j -= 1)
                {
                    placed[j] = placed[j - 1];
                }
                placed[j] = token;
            }
            sections->definitions = placed;
            sections->definition_count = count;
        }
        reader->scratch->length = mark;
    }
}

// The diagnostic of an input report of a line or a declaration (CHECK_INPUT_SECTION), or NULL: of a line's, the first in
// it.
static const CheckInputSectionDiagnostic* check_input_section_report(const CheckInput* input, const CheckInputReport* report)
{
    const CheckInputSections* sections = input->sections;
    const CheckInputSectionDiagnostic* result = NULL;
    for (uint32_t i = 0; report->message == CHECK_INPUT_SECTION && sections != NULL && i < sections->diagnostic_count; i += 1)
    {
        const CheckInputSectionDiagnostic* diagnostic = sections->diagnostics + i;
        const bool match = report->pragma != 0 ? diagnostic->pragma == report->pragma - 1 :
                                                 diagnostic->pragma == BUSTER_CHECK_NONE && diagnostic->token == report->token;
        result = match && (result == NULL || diagnostic->source < result->source) ? diagnostic : result;
    }
    return result;
}
