// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Retained format_matches contracts. Only attributed declarations and their calls enter this path. A contract
// stores one record per consumed argument; positions, width/precision roles and exact conversion widths survive
// decomposition. Literal scanning uses masked byte batches, and positional records use a stable radix ordering.

typedef struct CheckFormatMatchSpec CheckFormatMatchSpec;
struct CheckFormatMatchSpec
{
    uint32_t position;
    uint32_t owner;
    uint32_t offset;
    uint32_t modifier;
    uint16_t bits;
    uint8_t role;
    uint8_t family;
    uint8_t sign;
    uint8_t sensitivity;
    uint8_t letter;
    uint8_t modifier_length;
};

typedef struct CheckFormatMatch CheckFormatMatch;
struct CheckFormatMatch
{
    CheckFormatMatchSpec* specs;
    uint32_t count;
    uint32_t token;
    String8 text;
    bool valid;
    bool malformed;
    bool invalid_reference;
    uint8_t dialect;
};

typedef struct CheckFormatContract CheckFormatContract;
struct CheckFormatContract
{
    uint32_t first;
    uint32_t declaration;
    CheckFormatMatch format;
};

typedef struct CheckFormatContracts CheckFormatContracts;
struct CheckFormatContracts
{
    CheckFormatContract* records;
    uint32_t count;
};

static uint32_t check_common_format_matches_token(const Parser* parser, const CheckInputAttribute* attribute)
{
    uint32_t first = attribute->argument_first;
    uint32_t end = attribute->argument_end;
    const bool located = check_input_attribute_argument(attribute, 2, &first, &end);
    while (located && first + 1 < end && parser->tokens.kinds[first] == PARSE_KIND_L_PAREN && parser->tree.parents[first] == end - 1)
    {
        first += 1;
        end -= 1;
    }
    return located ? first : BUSTER_CHECK_NONE;
}

static String8 check_common_format_matches_archetype(const Parser* parser, Preprocessor* pp, const CheckInputAttribute* attribute)
{
    const uint32_t name = attribute->argument_first < parser->tokens.count ? parser->tokens.names[attribute->argument_first] : 0;
    String8 text = { (char*)intern_bytes(pp->table, name), intern_length(pp->table, name) };
    if (text.length >= 4 && text.pointer[0] == '_' && text.pointer[1] == '_' && text.pointer[text.length - 2] == '_' && text.pointer[text.length - 1] == '_')
    {
        text.pointer += 2;
        text.length -= 4;
    }
    return text;
}

static bool check_common_format_matches_family(const Parser* parser, Preprocessor* pp, const CheckInputAttribute* attribute)
{
    const String8 text = check_common_format_matches_archetype(parser, pp, attribute);
    static const char* const families[] = { "printf", "printf0", "gnu_printf", "NSString", "CFString", "kprintf", "freebsd_kprintf", "os_log", "os_trace" };
    bool result = false;
    for (uint32_t i = 0; !result && i < BUSTER_ARRAY_LENGTH(families); i += 1)
    {
        result = check_input_attribute_equal(text, families[i]);
    }
    return result;
}

// Only diagnostics need this walk. Decode through the shared literal step so concatenated tokens and escaped
// bytes keep their actual spelling locations rather than adding decoded offsets to the first opening quote.
static uint32_t check_common_format_matches_location(const Parser* parser, Preprocessor* pp, uint32_t token, uint32_t offset, Arena* scratch)
{
    uint32_t location = 0;
    uint64_t decoded = 0;
    for (uint32_t part_token = token; location == 0 && part_token < parser->tokens.count && parser->tokens.kinds[part_token] == PARSE_KIND_STRING; part_token += 1)
    {
        const uint32_t source = parse_source_index(&parser->tokens, part_token);
        if (source >= pp->output.count || pp->output.locations == NULL || pp->output.locations[source] == 0 ||
            pp->output.values[source] < pp->output.locations[source])
        {
            break;
        }
        const uint64_t capacity = (uint64_t)pp->output.values[source] - pp->output.locations[source] + 1;
        uint8_t* buffer = CHECK_COMMON_PUSH_ARRAY(scratch, uint8_t, capacity);
        if (buffer == NULL)
        {
            check_common_exhausted(pp);
            break;
        }
        CheckStringPart part = { 0 };
        part.length = pp_spelling(pp, pp->output.kinds[source], pp->output.values[source], pp->output.locations[source], buffer, &part.bytes);
        const CheckStringSpan span = check_literal_string_span(&part);
        uint32_t position = span.begin;
        bool valid = span.valid;
        while (valid && position < span.end && location == 0)
        {
            const uint32_t begin = position;
            valid = check_literal_string_step(&part, &span, span.encoding, parser->language.standard >= 2023, &position, NULL, &decoded, true);
            if (valid && decoded > offset)
            {
                location = pp->output.locations[source] + begin;
            }
        }
        if (valid && decoded == offset && (part_token + 1 >= parser->tokens.count || parser->tokens.kinds[part_token + 1] != PARSE_KIND_STRING))
        {
            location = pp->output.locations[source] + span.end;
        }
    }
    return location;
}

// One warning and its optional comparison/definition note form a single diagnostic group. A standalone
// PP_SEVERITY_NOTE must never pass the diagnostic counting gate: it would incorrectly reject a warning-only unit.
// Offsets are zero for a token's beginning, or one plus a decoded literal byte index.
static void check_common_format_matches_emit(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                            uint32_t token, uint32_t offset, uint32_t id, const char* message, uint32_t* order,
                                            uint32_t note_token, uint32_t note_offset, const char* note_message, Arena* scratch)
{
    const uint32_t source = token < parser->tokens.count ? parse_source_index(&parser->tokens, token) : UINT32_MAX;
    const CheckWarningSeverity severity = check_common_severity(pp, parser, input, token, id, false);
    if (source < pp->output.count && pp->output.locations != NULL && severity != CHECK_WARNING_IGNORE && message != NULL)
    {
        const uint32_t location = offset != 0 ? check_common_format_matches_location(parser, pp, token, offset - 1, scratch) : pp->output.locations[source];
        const uint32_t note_source = note_token < parser->tokens.count ? parse_source_index(&parser->tokens, note_token) : UINT32_MAX;
        const uint32_t note_location = note_message == NULL || note_source >= pp->output.count ? 0 :
                                       note_offset != 0 ? check_common_format_matches_location(parser, pp, note_token, note_offset - 1, scratch) :
                                                          pp->output.locations[note_source];
        const char* option = id < BUSTER_ARRAY_LENGTH(check_warning_descriptions) ? check_warning_descriptions[id].option : NULL;
        const bool error = severity >= CHECK_WARNING_ERROR;
        const char* suffix = option != NULL ? check_warning_text(scratch, " [%s%s]", error ? "-Werror," : "", option) : error ? " [-Werror]" : "";
        const PPSeverity disposition = severity == CHECK_WARNING_FATAL ? PP_SEVERITY_MAPPED_FATAL :
                                       error ? PP_SEVERITY_MAPPED_ERROR : PP_SEVERITY_WARNING;
        if (location != 0 && suffix != NULL)
        {
            pp_history_ensure(pp);
            const PPDiagnosticSourceNote note = { note_location, note_location != 0 ? pp_history_at(pp, note_source) : 0, note_message };
            *order = *order < source ? source : *order;
            PPDiagnosticRecord* previous = pp->diagnostic_last;
            pp_diagnose_placed_sources_phase(pp, disposition, location, pp_history_at(pp, source), *order, PP_DIAGNOSTIC_SEMANTIC,
                                             note_location != 0 ? &note : NULL, note_location != 0 ? 1u : 0u, "%s%s", message, suffix);
            if (pp->diagnostic_last != previous)
            {
                pp->diagnostic_last->token = source + 1;
            }
        }
    }
}

static void check_common_format_matches_warning(const Parser* parser, Preprocessor* pp, const CheckInput* input, uint32_t token,
                                               uint64_t offset, uint32_t* order, uint32_t id, const char* message, Arena* scratch)
{
    if (offset < UINT32_MAX)
    {
        check_common_format_matches_emit(parser, pp, input, token, (uint32_t)offset + 1, id, message, order,
                                         BUSTER_CHECK_NONE, 0, NULL, scratch);
    }
}

static void check_common_format_matches_report(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckFormatMatch* actual,
                                              uint32_t actual_offset, const CheckFormatMatch* expected, uint32_t expected_offset,
                                              uint32_t* order, uint32_t id, const char* message, bool whole, Arena* scratch)
{
    check_common_format_matches_emit(parser, pp, input, actual->token, whole ? 0 : actual_offset + 1, id, message, order,
                                     expected->token, whole ? 0 : expected_offset + 1,
                                     whole ? "comparing with this format string" : "comparing with this specifier", scratch);
}

static void check_common_format_matches_sort(CheckFormatMatch* format, Preprocessor* pp, Arena* scratch)
{
    if (format->count > 1)
    {
        CheckFormatMatchSpec* work = CHECK_COMMON_PUSH_ARRAY(scratch, CheckFormatMatchSpec, format->count);
        if (work == NULL)
        {
            check_common_exhausted(pp);
            format->valid = false;
        }
        else
        {
            uint32_t maximum = 0;
            for (uint32_t i = 0; i < format->count; i += 1)
            {
                maximum |= format->specs[i].position;
            }
            CheckFormatMatchSpec* from = format->specs;
            CheckFormatMatchSpec* to = work;
            for (uint32_t shift = 0; shift < 32 && (maximum >> shift) != 0; shift += 8)
            {
                uint32_t bins[256] = { 0 };
                for (uint32_t i = 0; i < format->count; i += 1)
                {
                    bins[(from[i].position >> shift) & 255] += 1;
                }
                uint32_t prefix = 0;
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bins); i += 1)
                {
                    const uint32_t count = bins[i];
                    bins[i] = prefix;
                    prefix += count;
                }
                for (uint32_t i = 0; i < format->count; i += 1)
                {
                    to[bins[(from[i].position >> shift) & 255]++] = from[i];
                }
                CheckFormatMatchSpec* swap = from;
                from = to;
                to = swap;
            }
            format->specs = from;
        }
    }
}

// The representative argument type, including the unpromoted width required by format_matches. Long and long
// long are compatible at this target width, while a pointer's pointee category remains distinct from its width.
static void check_common_format_matches_type(CheckFormatMatchSpec* spec, const uint8_t* bytes)
{
    const uint8_t letter = spec->letter;
    const uint32_t begin = spec->modifier;
    const uint32_t length = spec->modifier_length;
    const uint8_t modifier = length != 0 ? bytes[begin] : 0;
    uint32_t bits = modifier == 'h' ? (length == 2 ? 8 : 16) :
                    modifier == 'l' || modifier == 'L' || modifier == 'q' || modifier == 'j' || modifier == 'z' || modifier == 't' ? 64 : 32;
    if (modifier == 'w' || modifier == 'I')
    {
        uint32_t at = begin + 1;
        at += at < begin + length && bytes[at] == 'f';
        bits = 0;
        for (; at < begin + length; at += 1)
        {
            bits = bits * 10 + (uint32_t)(bytes[at] - '0');
            bits = bits > UINT16_MAX ? UINT16_MAX : bits;
        }
        bits = bits != 0 ? bits : 64;
    }
    const bool integer = letter == 'd' || letter == 'i' || letter == 'u' || letter == 'o' || letter == 'x' || letter == 'X' || letter == 'b' || letter == 'B';
    const bool floating = letter == 'a' || letter == 'A' || letter == 'e' || letter == 'E' || letter == 'f' || letter == 'F' || letter == 'g' || letter == 'G';
    spec->family = letter == '@' ? 0 : integer || letter == 'c' || letter == 'C' ? 1 : floating ? 2 : letter == 's' || letter == 'S' ? 3 : letter == 'n' ? 4 : letter == 'p' ? 5 : 6;
    spec->bits = floating ? (modifier == 'L' ? 128 : 64) : letter == 'c' || letter == 'C' ? 32 :
                 letter == 's' || letter == 'S' ? (modifier == 'l' || modifier == 'w' || letter == 'S' ? 32 : 8) : (uint16_t)bits;
    spec->sign = integer && !(letter == 'd' || letter == 'i');
}

static CheckFormatMatch check_common_format_matches_parse(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                                          uint32_t token, String8 text, uint32_t* order, Arena* scratch, uint8_t dialect)
{
    CheckFormatMatch result = { .token = token, .text = text, .valid = text.length <= UINT32_MAX, .dialect = dialect };
    const uint8_t* bytes = (const uint8_t*)text.pointer;
    const uint32_t length = result.valid ? (uint32_t)text.length : 0;
    result.specs = result.valid && length != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckFormatMatchSpec, length) : NULL;
    if (!result.valid || (length != 0 && result.specs == NULL))
    {
        check_common_exhausted(pp);
        result.valid = false;
    }
    uint32_t next = 1;
    bool positional = false;
    bool ordinary = false;
    bool stopped = false;
    for (uint64_t cursor = 0; result.valid && !stopped && cursor < length;)
    {
        const uint32_t left = length - (uint32_t)cursor;
        const __mmask64 live = left >= 64 ? UINT64_MAX : (UINT64_C(1) << left) - 1;
        const __m512i batch = _mm512_maskz_loadu_epi8(live, bytes + cursor);
        const uint64_t interesting = live & (_mm512_cmpeq_epi8_mask(batch, _mm512_set1_epi8('%')) | _mm512_cmpeq_epi8_mask(batch, _mm512_setzero_si512()));
        if (interesting == 0)
        {
            cursor += left < 64 ? left : 64;
            continue;
        }
        cursor += (uint32_t)__builtin_ctzll(interesting);
        const uint32_t percent = (uint32_t)cursor;
        if (bytes[cursor] == 0)
        {
            check_common_format_matches_warning(parser, pp, input, token, cursor, order, CHECK_WARNING_ID_FORMAT,
                                              "format string contains '\\0' within the string body", scratch);
            result.malformed = true;
            break;
        }
        cursor += 1;
        uint32_t position = 0;
        const bool placed = check_common_format_position(bytes, length, &cursor, &position);
        uint8_t sensitivity = 0;
        if (cursor < length && bytes[cursor] == '{')
        {
            const uint64_t begin = ++cursor;
            while (cursor < length && bytes[cursor] != '}')
            {
                cursor += 1;
            }
            uint64_t annotation = begin;
            while (annotation < cursor && sensitivity == 0)
            {
                uint64_t end = annotation;
                while (end < cursor && bytes[end] != ',')
                {
                    end += 1;
                }
                const String8 privacy = { (char*)bytes + annotation, end - annotation };
                sensitivity = check_input_attribute_equal(privacy, "private") ? 1 : check_input_attribute_equal(privacy, "public") ? 2 :
                              check_input_attribute_equal(privacy, "sensitive") ? 3 : 0;
                if (dialect != 2 && (sensitivity == 1 || sensitivity == 2))
                {
                    const char* message = check_warning_text(scratch, "using '%.*s' format specifier annotation outside of os_log()/os_trace()",
                                                             (int)privacy.length, privacy.pointer);
                    check_common_format_matches_warning(parser, pp, input, token, annotation, order, CHECK_WARNING_ID_FORMAT, message, scratch);
                    result.malformed = true;
                }
                annotation = end + 1;
            }
            cursor += cursor < length;
        }
        while (cursor < length && (bytes[cursor] == '-' || bytes[cursor] == '+' || bytes[cursor] == ' ' || bytes[cursor] == '#' || bytes[cursor] == '0' || bytes[cursor] == '\''))
        {
            cursor += 1;
        }
        const uint32_t first_modifier = result.count;
        for (uint8_t role = 1; role <= 2; role += 1)
        {
            if (role == 2)
            {
                if (cursor >= length || bytes[cursor] != '.')
                {
                    break;
                }
                cursor += 1;
            }
            if (cursor < length && bytes[cursor] == '*')
            {
                const uint32_t at = (uint32_t)cursor++;
                uint32_t index = 0;
                const bool indexed = check_common_format_position(bytes, length, &cursor, &index);
                if ((placed && !indexed) || (indexed && index == 0))
                {
                    check_common_format_matches_warning(parser, pp, input, token, at, order, CHECK_WARNING_ID_FORMAT,
                                                        role == 2 ? "invalid position specified for field precision" : "invalid position specified for field width", scratch);
                    result.malformed = true;
                    stopped = true;
                    break;
                }
                // Clang's amount location is the positional star/dot, or the byte after an ordinary width star.
                const uint32_t amount_offset = indexed ? at - (role == 2) : at + (role == 1);
                result.specs[result.count++] = (CheckFormatMatchSpec){ .position = indexed ? index : next++, .offset = amount_offset, .role = role };
                positional |= indexed;
                ordinary |= !indexed;
            }
            else
            {
                while (cursor < length && bytes[cursor] >= '0' && bytes[cursor] <= '9')
                {
                    cursor += 1;
                }
            }
        }
        const uint32_t modifier = (uint32_t)cursor;
        if (cursor < length && (bytes[cursor] == 'h' || bytes[cursor] == 'l' || bytes[cursor] == 'L' || bytes[cursor] == 'j' || bytes[cursor] == 'z' || bytes[cursor] == 't' || bytes[cursor] == 'q'))
        {
            const uint8_t first = bytes[cursor++];
            cursor += cursor < length && (first == 'h' || first == 'l') && bytes[cursor] == first;
        }
        else if (cursor < length && (bytes[cursor] == 'w' || bytes[cursor] == 'I'))
        {
            const uint8_t first = bytes[cursor++];
            cursor += cursor < length && first == 'w' && bytes[cursor] == 'f';
            while (cursor < length && bytes[cursor] >= '0' && bytes[cursor] <= '9')
            {
                cursor += 1;
            }
        }
        const uint8_t letter = cursor < length ? bytes[cursor] : 0;
        if (!stopped && letter == 0)
        {
            const bool embedded = cursor < length;
            check_common_format_matches_warning(parser, pp, input, token, embedded ? cursor : percent, order, CHECK_WARNING_ID_FORMAT,
                                                embedded ? "format string contains '\\0' within the string body" : "incomplete format specifier", scratch);
            result.malformed = true;
            stopped = true;
        }
        else if (!stopped && strchr("diuoxXbBcCsSpnaAeEfFgG%@m", letter) == NULL)
        {
            char spelling[6];
            if (letter >= 0x20 && letter < 0x7f)
            {
                spelling[0] = (char)letter;
                spelling[1] = 0;
            }
            else
            {
                snprintf(spelling, sizeof(spelling), "\\x%02x", letter);
            }
            const char* message = check_warning_text(scratch, "invalid conversion specifier '%s'", spelling);
            check_common_format_matches_warning(parser, pp, input, token, cursor, order, CHECK_WARNING_ID_FORMAT_INVALID_SPECIFIER, message, scratch);
            result.malformed = true;
            stopped = true;
        }
        else if (!stopped && letter != '%')
        {
            const uint32_t value = placed ? position : next++;
            positional |= placed;
            ordinary |= !placed;
            if (value == 0 || (positional && ordinary))
            {
                check_common_format_matches_warning(parser, pp, input, token, value == 0 ? percent : cursor, order, CHECK_WARNING_ID_FORMAT,
                                                  value == 0 ? "position arguments in format strings start counting at 1 (not 0)" :
                                                               "cannot mix positional and non-positional arguments in format string", scratch);
                // A mixed argument convention invalidates the whole contract. A zero position keeps the
                // earlier complete specifiers, so call comparison still reports its missing trailing arguments.
                result.valid = value == 0;
                result.malformed = true;
                stopped = true;
            }
            else
            {
                if (letter == '@' && dialect == 0)
                {
                    check_common_format_matches_warning(parser, pp, input, token, cursor, order, CHECK_WARNING_ID_FORMAT_INVALID_SPECIFIER,
                                                        "invalid conversion specifier '@'", scratch);
                    result.malformed = true;
                    result.invalid_reference = true;
                }
                for (uint32_t i = first_modifier; i < result.count; i += 1)
                {
                    result.specs[i].owner = value;
                }
                CheckFormatMatchSpec spec = { .position = value, .offset = (uint32_t)cursor, .modifier = modifier,
                                             .modifier_length = (uint8_t)(cursor - modifier), .letter = letter, .sensitivity = sensitivity };
                check_common_format_matches_type(&spec, bytes);
                spec.family = letter == '@' && dialect != 0 ? 7 : spec.family;
                result.specs[result.count++] = spec;
            }
        }
        if (stopped)
        {
            result.count = first_modifier;
        }
        cursor += cursor < length;
    }
    if (result.valid)
    {
        check_common_format_matches_sort(&result, pp, scratch);
    }
    return result;
}

static bool check_common_format_matches_spec(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckFormatMatch* actual,
                                            const CheckFormatMatchSpec* given, const CheckFormatMatch* expected, const CheckFormatMatchSpec* required,
                                            uint32_t* order, Arena* scratch)
{
    static const char* const roles[] = { "a value", "an indirect field width", "an indirect precision", "an auxiliary argument" };
    const char* message = NULL;
    uint32_t id = CHECK_WARNING_ID_FORMAT;
    const bool privacy = given->sensitivity != required->sensitivity;
    if (privacy)
    {
        static const char* const sensitivities[] = { "unspecified", "private", "public", "sensitive" };
        const char* difference = check_warning_text(scratch, "argument sensitivity is %s, but it should be %s",
                                                   sensitivities[given->sensitivity], sensitivities[required->sensitivity]);
        check_common_format_matches_report(parser, pp, input, actual, given->offset, expected, required->offset, order, id, difference, false, scratch);
    }
    if (given->role != required->role)
    {
        message = check_warning_text(scratch, "format argument is %s, but it should be %s", roles[given->role], roles[required->role]);
    }
    else if (given->role != 0 && given->owner != required->owner)
    {
        message = check_warning_text(scratch, "format argument modifies specifier at position %u, but it should modify specifier at position %u", given->owner, required->owner);
    }
    else if (given->role == 0)
    {
        const bool type = given->family == 0 || required->family == 0 || (given->family != 6 && given->family == required->family && given->bits == required->bits);
        const bool sign = type && given->family == 1 && required->family == 1 && given->sign != required->sign;
        if (!type || sign)
        {
            id = sign ? CHECK_WARNING_ID_FORMAT_SIGNEDNESS : CHECK_WARNING_ID_FORMAT;
            message = check_warning_text(scratch, "%sformat specifier '%.*s%c' is incompatible with '%.*s%c'", sign ? "signedness of " : "",
                                         given->modifier_length, actual->text.pointer + given->modifier, given->letter,
                                         required->modifier_length, expected->text.pointer + required->modifier, required->letter);
        }
    }
    const bool valid = !privacy && message == NULL;
    if (message != NULL)
    {
        check_common_format_matches_report(parser, pp, input, actual, given->offset, expected, required->offset, order, id, message, false, scratch);
    }
    return valid;
}

static bool check_common_format_matches_validate(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckFormatMatch* format,
                                                uint32_t* order, Arena* scratch)
{
    bool valid = format->valid;
    uint32_t group = 0;
    for (uint32_t i = 1; format->valid && i < format->count; i += 1)
    {
        if (format->specs[i].position != format->specs[group].position)
        {
            group = i;
        }
        else
        {
            valid &= check_common_format_matches_spec(parser, pp, input, format, format->specs + i, format, format->specs + group, order, scratch);
        }
    }
    return valid;
}

static void check_common_format_matches_compare(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckFormatMatch* actual,
                                                const CheckFormatMatch* expected, uint32_t* order, Arena* scratch)
{
    uint32_t a = 0;
    uint32_t e = 0;
    while (a < actual->count && e < expected->count)
    {
        const uint32_t position = expected->specs[e].position;
        while (a < actual->count && actual->specs[a].position <= position)
        {
            if (actual->specs[a].position == position)
            {
                check_common_format_matches_spec(parser, pp, input, actual, actual->specs + a, expected, expected->specs + e, order, scratch);
            }
            a += 1;
        }
        do
        {
            e += 1;
        }
        while (e < expected->count && expected->specs[e].position == position);
    }
    if (a < actual->count || e < expected->count)
    {
        check_common_format_matches_report(parser, pp, input, actual, 0, expected, 0, order, CHECK_WARNING_ID_FORMAT_INSUFFICIENT_ARGS,
                                          a < actual->count ? "more specifiers in format string than expected" : "fewer specifiers in format string than expected", true, scratch);
    }
}

static const CheckFormatMatch* check_common_format_matches_find(const CheckFormatContracts* contracts, uint32_t first)
{
    const CheckFormatMatch* result = NULL;
    for (uint32_t i = 0; result == NULL && i < contracts->count; i += 1)
    {
        result = contracts->records[i].first == first ? &contracts->records[i].format : NULL;
    }
    return result;
}

// Reconstruct the visible predecessor once, in source order. Only units with multiple contracts allocate this
// temporary object column. The existing exceptional lookup links preserve declarations in closed block scopes.
static void check_common_format_matches_redeclarations(const Parser* parser, Preprocessor* pp, const CheckInput* input,
                                                       const CheckFormatContracts* contracts, Arena* scratch)
{
    const uint32_t count = contracts->count > 1 && input->objects != NULL && input->declarations != NULL &&
                           input->declaration_kinds != NULL ? input->token_count : 0;
    uint32_t* latest = count != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : NULL;
    if (count != 0 && latest == NULL)
    {
        check_common_exhausted(pp);
    }
    if (latest != NULL)
    {
        memset(latest, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
        uint32_t link = 0;
        for (uint32_t first = 0; first < count && !pp->fatal; first += 64)
        {
            const uint32_t live = count - first < 64 ? count - first : 64;
            const __mmask64 lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
            const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, input->declaration_kinds + first);
            uint64_t selected = _mm512_mask_cmpeq_epi8_mask(lanes, kinds, _mm512_set1_epi8((char)C_BINDING_FUNCTION));
            for (; selected != 0 && !pp->fatal; selected &= selected - 1)
            {
                const uint32_t declaration = first + (uint32_t)__builtin_ctzll(selected);
                const uint32_t object = input->declarations[declaration] == declaration ? input->objects[declaration] : 0;
                if (object == 0 || object > count)
                {
                    continue;
                }
                while (link < input->redeclaration_link_count && input->redeclaration_links[link * 2] < declaration)
                {
                    link += 1;
                }
                const bool linked = link < input->redeclaration_link_count && input->redeclaration_links[link * 2] == declaration;
                const uint32_t previous = linked ? input->redeclaration_links[link * 2 + 1] : latest[object];
                const CheckInputAttributes* attributes = check_input_attributes_at(input, declaration);
                const CheckInputAttributes* prior = previous < count ? check_input_attributes_at(input, previous) : NULL;
                for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* own = attributes->records + i;
                    const CheckFormatContract* current = NULL;
                    for (uint32_t c = 0; current == NULL && c < contracts->count; c += 1)
                    {
                        const CheckFormatContract* candidate = contracts->records + c;
                        current = candidate->first == own->first && candidate->declaration == declaration ? candidate : NULL;
                    }
                    if (current == NULL || !current->format.valid)
                    {
                        continue;
                    }
                    const String8 family = check_common_format_matches_archetype(parser, pp, own);
                    const uint32_t prior_count = prior != NULL ? prior->count : 0;
                    for (uint32_t p = 0; p < prior_count + i; p += 1)
                    {
                        const CheckInputAttribute* old = p < prior_count ? prior->records + p : attributes->records + p - prior_count;
                        if (old->kind != CHECK_ATTRIBUTE_FORMAT_MATCHES || old->first == own->first || old->argument_values == NULL ||
                            old->argument_values[1] != own->argument_values[1])
                        {
                            continue;
                        }
                        const String8 old_family = check_common_format_matches_archetype(parser, pp, old);
                        const CheckFormatMatch* given = check_common_format_matches_find(contracts, old->first);
                        if (given != NULL && given->valid && family.length == old_family.length && memcmp(family.pointer, old_family.pointer, family.length) == 0)
                        {
                            uint32_t order = parse_source_index(&parser->tokens, own->end - 1);
                            check_common_format_matches_compare(parser, pp, input, given, &current->format, &order, scratch);
                            break;
                        }
                    }
                }
                latest[object] = declaration;
            }
        }
    }
}

// Scan the existing sparse attribute column, not the source tokens. No allocation is made until a format_matches
// record is found. Inherited copies share the source attribute's contract and never repeat its diagnostics.
static CheckFormatContracts check_common_format_matches_declarations(const Parser* parser, Preprocessor* pp, const CheckInput* input, Arena* scratch)
{
    CheckFormatContracts result = { 0 };
    uint32_t capacity = 0;
    const uint32_t end = check_sparse_wide_present(input->attributes) ? input->token_count : 0;
    for (uint32_t block = 0; block < end && !pp->fatal; block += BUSTER_CHECK_SPARSE_TOKENS)
    {
        block = check_sparse_wide_next(input->attributes, block, end);
        for (uint32_t set = block < end ? check_sparse_wide_group(input->attributes, block, end) : 0; set != 0 && !pp->fatal; set &= set - 1)
        {
            const uint32_t token = block + (uint32_t)__builtin_ctz(set);
            const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
            for (uint32_t i = 0; attributes != NULL && i < attributes->count && !pp->fatal; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (attribute->kind != CHECK_ATTRIBUTE_FORMAT_MATCHES || attribute->ignored || attribute->subject_ignored ||
                    attribute->argument_values == NULL || attribute->deferred != NULL ||
                    !check_common_format_matches_family(parser, pp, attribute) || check_common_format_matches_find(&result, attribute->first) != NULL)
                {
                    continue;
                }
                if (result.count == capacity)
                {
                    const uint32_t grown = capacity == 0 ? 4 : capacity <= UINT32_MAX / 2 ? capacity * 2 : 0;
                    CheckFormatContract* records = grown != 0 ? CHECK_COMMON_PUSH_ARRAY(scratch, CheckFormatContract, grown) : NULL;
                    if (records == NULL)
                    {
                        check_common_exhausted(pp);
                        break;
                    }
                    if (result.count != 0)
                    {
                        memcpy(records, result.records, (uint64_t)result.count * sizeof(CheckFormatContract));
                    }
                    result.records = records;
                    capacity = grown;
                }
                uint32_t order = parse_source_index(&parser->tokens, attribute->end - 1);
                const uint32_t literal = check_common_format_matches_token(parser, attribute);
                const String8 name = check_common_format_matches_archetype(parser, pp, attribute);
                const uint8_t dialect = check_input_attribute_equal(name, "NSString") || check_input_attribute_equal(name, "CFString") ? 1 :
                                        check_input_attribute_equal(name, "os_log") || check_input_attribute_equal(name, "os_trace") ? 2 : 0;
                CheckFormatMatch format = check_common_format_matches_parse(parser, pp, input, literal, attribute->text, &order, scratch, dialect);
                format.valid = check_common_format_matches_validate(parser, pp, input, &format, &order, scratch) && !format.invalid_reference;
                result.records[result.count++] = (CheckFormatContract){ attribute->first, token, format };
            }
        }
    }
    check_common_format_matches_redeclarations(parser, pp, input, &result, scratch);
    return result;
}

static uint32_t check_common_format_matches_owner(const CheckInput* input, const CheckGraph* graph, uint32_t node, uint32_t* ordinal)
{
    while (node < graph->count && (graph->ops[node] == CHECK_OP_CONVERT || graph->ops[node] == CHECK_OP_CAST ||
                                  graph->ops[node] == CHECK_OP_POSITIVE) && graph->operand0[node] < node)
    {
        node = graph->operand0[node];
    }
    const uint32_t token = node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
    const uint32_t declaration = token < input->token_count && input->declarations != NULL ? input->declarations[token] : BUSTER_CHECK_NONE;
    const uint32_t body = declaration < input->token_count ? check_common_sparse_get(input->declaration_bodies, declaration) : BUSTER_CHECK_NONE;
    *ordinal = declaration < input->token_count ? check_common_sparse_get(input->parameters, declaration) : BUSTER_CHECK_NONE;
    const uint32_t owner = body < input->token_count && *ordinal != BUSTER_CHECK_NONE ?
                           check_common_sparse_get(input->body_declarations, body) : BUSTER_CHECK_NONE;
    return owner;
}

// A format parameter reads the annotation on its own body declaration, including an inherited prototype's.
// Only units with contracts pay this lookup, and only nonliteral format leaves ask it.
static const CheckFormatMatch* check_common_format_matches_parameter(const Parser* parser, const CheckInput* input, const CheckGraph* graph,
                                                                   uint32_t node, const CheckFormatContracts* contracts)
{
    uint32_t ordinal = BUSTER_CHECK_NONE;
    const uint32_t function = check_common_format_matches_owner(input, graph, node, &ordinal);
    const CheckFormatMatch* result = NULL;
    const CheckInputAttributes* attributes = function < input->token_count ? check_input_attributes_at(input, function) : NULL;
    for (uint32_t i = 0; result == NULL && attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        if (attribute->kind == CHECK_ATTRIBUTE_FORMAT_MATCHES && !attribute->ignored && !attribute->subject_ignored &&
            attribute->argument_values != NULL && attribute->argument_values[1] == (int64_t)ordinal + 1)
        {
            const CheckFormatMatch* found = check_common_format_matches_find(contracts, attribute->first);
            result = found != NULL && found->valid ? found : NULL;
        }
    }
    (void)parser;
    return result;
}

// A forwarded parameter can carry the same obligation on its own function. Keep the suggestion and its note
// together; when that warning is disabled the caller still gets the ordinary nonliteral policy.
static bool check_common_format_matches_suggest(const Parser* parser, Preprocessor* pp, const CheckInput* input, const CheckGraph* graph,
                                               uint32_t node, const CheckInputAttribute* attribute, uint32_t token, uint32_t* order, Arena* scratch)
{
    uint32_t ordinal = BUSTER_CHECK_NONE;
    const uint32_t function = check_common_format_matches_owner(input, graph, node, &ordinal);
    const bool suggest = function < parser->tokens.count &&
                         check_common_severity(pp, parser, input, token, CHECK_WARNING_ID_MISSING_FORMAT_ATTRIBUTE, false) != CHECK_WARNING_IGNORE;
    if (suggest)
    {
        const String8 family = check_common_format_matches_archetype(parser, pp, attribute);
        const uint32_t name = parser->tokens.names[function];
        const char* spelling = (const char*)intern_bytes(pp->table, name);
        const int name_length = (int)intern_length(pp->table, name);
        const char* message = check_warning_text(scratch,
            "diagnostic behavior may be improved by adding the 'format_matches(%.*s, %u, \"%.*s\")' attribute to the declaration of '%.*s'",
            (int)family.length, family.pointer, ordinal + 1, (int)attribute->text.length, attribute->text.pointer, name_length, spelling);
        const char* note_text = check_warning_text(scratch, "'%.*s' declared here", name_length, spelling);
        const CheckCommonNote note = { function, note_text };
        check_common_report_published(parser, pp, input, token, *order, CHECK_WARNING_ID_MISSING_FORMAT_ATTRIBUTE, false, message, &note, 1, scratch);
    }
    return suggest;
}
