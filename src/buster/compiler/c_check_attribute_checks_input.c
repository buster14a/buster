// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// clang's checks-only attributes across a unit (#1447): what clang checks of enforce_tcb, enforce_tcb_leaf and
// modular_format where a redeclaration merges its entity's attributes (check_input_attribute_merge_checks, from
// check_input_bind), modular_format's format (check_input_modular_format), and TCB's warnings at calls once the unit's
// bindings are resolved (check_input_tcb_calls). A declaration's own checks are c_check_attribute.c's
// (check_input_attribute_checks), but diagnose_as_builtin's, which read the builtins' catalogue and the unit's library
// declarations (check_input_attribute_diagnose_builtin). None of these attributes changes what a program does in C on
// x86-64 Linux. Their units are rare: each check scans the unit's attributed declarations (check_input_attributed_next),
// as the late attributes' do, and a lane that reads one refuses (check_input_attribute_read_item), so these run in
// source order.
// Included by c_check_input.c after c_check_implicit_input.c, whose library builtins it reads.

// ---------------------------------------------------------------------------------------------------------------------
// availability and external_source_symbol, whose arguments clang's parser reads its own way (ParseAvailabilityAttribute,
// ParseExternalSourceSymbolAttribute): their tokens are read here as clang's parser reads them, with its errors and
// warnings, before their place decides anything (check_input_attribute_read_item); then availability's Sema's checks
// once its subject takes it (check_input_availability_handle), and where its declaration's own availability attributes
// and its redeclarations' meet (mergeAvailabilityAttr: check_input_availability_merge). None changes what a program
// does: the attribute decides nothing on a Linux target, whose platform no availability attribute names.

// One of availability's or external_source_symbol's parser's errors (#1447) at `token`, the token its message names in the
// report's value (BUSTER_CHECK_NONE for none): the declaration's first attribute error.
static void check_input_custom_error(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t token,
                                     CheckAttributeArgumentError error, uint32_t number, uint32_t value)
{
    const uint32_t before = reader->output->report_count;
    check_input_attribute_argument_error(reader, attributes, attribute, token, error, number);
    if (reader->output->report_count > before)
    {
        reader->output->reports[before].value = value;
    }
}

// clang's "expected ')'" at `token`, with its note at the arguments' `(` (`open`): the declaration's first attribute error.
// A `;` just before the `)` is clang's "unexpected ';' before ')'" alone (BalancedDelimiterTracker::consumeClose).
static void check_input_custom_close(CheckInputReader* reader, CheckInputAttributes* attributes, const CheckInputAttribute* attribute, uint32_t token, uint32_t open)
{
    const uint32_t before = reader->output->report_count;
    const bool semi = token + 1 == attribute->argument_end && reader->parser->tokens.kinds[token] == PARSE_KIND_SEMI;
    check_input_attribute_argument_error(reader, attributes, attribute, token, semi ? CHECK_ARGUMENT_UNEXPECTED_SEMI : CHECK_ARGUMENT_EXPECTED_PAREN, 0);
    if (reader->output->report_count > before && !semi)
    {
        reader->output->reports[before].note_token = open;
        reader->output->reports[before].note_kind = CHECK_INPUT_NOTE_MATCH_PAREN;
    }
}

// One of availability's warnings (#1447, CHECK_WARNING_EVENT_AVAILABILITY_*) at `token`, given with `completion` in lookahead,
// `related` in the payload's token and `data` in its data; with a note (`note`, CHECK_WARNING_NOTE_KIND_COUNT for none) at
// `note_token`, whose data is the same.
static void check_input_availability_event(CheckInputReader* reader, CheckWarningId id, CheckWarningEventKind kind, uint32_t token, uint32_t completion,
                                             uint32_t related, uint32_t data, CheckWarningNoteKind note, uint32_t note_token)
{
    const CheckWarningEvent event =
    {
        .id = id,
        .kind = kind,
        .token = token,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
        .completion = completion,
        .payload = { .token = related, .data = data },
    };
    const CheckWarningNote noted =
    {
        .kind = note,
        .token = note_token,
        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
        .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = data },
    };
    check_input_warning_event(reader, &event, note != CHECK_WARNING_NOTE_KIND_COUNT ? &noted : NULL, note != CHECK_WARNING_NOTE_KIND_COUNT ? 1u : 0u);
}

// A text an availability warning prints (CheckInput.availability_texts), appended; its index, or UINT32_MAX where the arena
// can't hold it (CheckInput.resource).
static uint32_t check_input_availability_text(CheckInputReader* reader, CheckInputAvailabilityText text)
{
    CheckInput* output = reader->output;
    if (output->availability_text_count == output->availability_text_capacity)
    {
        const uint32_t capacity = output->availability_text_capacity == 0 ? 8 : output->availability_text_capacity * 2;
        CheckInputAvailabilityText* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAvailabilityText, capacity);
        output->resource |= grown == NULL;
        if (grown != NULL)
        {
            if (output->availability_text_count != 0)
            {
                memcpy(grown, output->availability_texts, (uint64_t)output->availability_text_count * sizeof(*grown));
            }
            output->availability_texts = grown;
            output->availability_text_capacity = capacity;
        }
    }
    const bool room = output->availability_text_count < output->availability_text_capacity;
    if (room)
    {
        output->availability_texts[output->availability_text_count] = text;
    }
    const uint32_t result = room ? output->availability_text_count : UINT32_MAX;
    output->availability_text_count += room ? 1u : 0u;
    return result;
}

// availability's arguments in parentheses as clang's ParseAvailabilityAttribute reads them (#1447): a platform's name (one
// with xros or xrOS in it warned of as unknown), a `,`, then changes, each after a `,`: strict, unavailable, Swift's bare
// deprecated, or a keyword, `=` and its value: message's and replacement's a string (message's the last), environment's a
// name, introduced's and deprecated's NA, or a version (ParseVersionTuple: check_input_availability_number) of a stage
// (introduced, deprecated, obsoleted; another keyword's an error); then `)`. A repeated change is an error, unavailable with
// stages its warning, which clears them, and an anyAppleOS version before 26 an error. Into attribute->availability; whether
// it is kept, an error, or dropped without a word (an empty version).
static uint32_t check_input_availability_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t end = attribute->argument_end;
    const uint32_t open = attribute->argument_first - 1;
    CheckInputAvailabilityParse* parse = BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAvailabilityParse, 1);
    reader->output->resource |= parse == NULL;
    uint32_t result = parse != NULL ? BUSTER_CHECK_CUSTOM_KEPT : BUSTER_CHECK_CUSTOM_ERROR;
    if (parse != NULL)
    {
        *parse = (CheckInputAvailabilityParse){
            .platform = BUSTER_CHECK_NONE,
            .platform_index = UINT32_MAX,
            .keywords = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
            .version_tokens = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE, BUSTER_CHECK_NONE },
            .unavailable = BUSTER_CHECK_NONE,
            .environment = BUSTER_CHECK_NONE,
        };
    }
    uint32_t t = attribute->argument_first;
    if (result == BUSTER_CHECK_CUSTOM_KEPT && (t >= end || parser->tokens.kinds[t] != PARSE_KIND_IDENTIFIER))
    {
        check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_AVAILABILITY_PLATFORM, 0, BUSTER_CHECK_NONE);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    else if (result == BUSTER_CHECK_CUSTOM_KEPT)
    {
        const uint32_t name = parser->tokens.names[t];
        bool xros = false;
        parse->platform = t;
        parse->platform_index = check_availability_attribute_platform(intern_bytes(reader->pp->table, name), intern_length(reader->pp->table, name), &xros);
        if (xros)
        {
            check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_PLATFORM, t, t + 1, t, 0,
                                             CHECK_WARNING_NOTE_KIND_COUNT, 0);
        }
        t += 1;
    }
    // Its `,`, expected just past the platform's name.
    if (result == BUSTER_CHECK_CUSTOM_KEPT && (t >= end || parser->tokens.kinds[t] != PARSE_KIND_COMMA))
    {
        check_input_custom_error(reader, attributes, attribute, t - 1, CHECK_ARGUMENT_AVAILABILITY_COMMA, 0, BUSTER_CHECK_NONE);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    t += result == BUSTER_CHECK_CUSTOM_KEPT ? 1u : 0u;
    uint32_t strict = BUSTER_CHECK_NONE;
    bool more = result == BUSTER_CHECK_CUSTOM_KEPT;
    while (more)
    {
        bool next = false;
        const uint32_t keyword = t;
        const uint32_t word = t < end && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[t] : 0;
        const bool swift = parse->platform_index == BUSTER_CHECK_AVAILABILITY_SWIFT;
        const bool is_strict = check_input_checks_spelled(reader, word, "strict");
        const bool is_unavailable = check_input_checks_spelled(reader, word, "unavailable");
        const bool introduced = check_input_checks_spelled(reader, word, "introduced");
        const bool deprecated = check_input_checks_spelled(reader, word, "deprecated");
        const bool obsoleted = check_input_checks_spelled(reader, word, "obsoleted");
        const bool environment = check_input_checks_spelled(reader, word, "environment");
        const bool message = check_input_checks_spelled(reader, word, "message");
        const bool replacement = check_input_checks_spelled(reader, word, "replacement");
        const uint32_t stage = introduced ? 0u : deprecated ? 1u : obsoleted ? 2u : 3u;
        uint32_t* seen = is_strict ? &strict : is_unavailable ? &parse->unavailable : deprecated && swift ? &parse->keywords[1] :
                         environment ? &parse->environment : NULL;
        t += word != 0 ? 1u : 0u;
        if (word == 0)
        {
            check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_AVAILABILITY_CHANGE, 0, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (seen != NULL && *seen != BUSTER_CHECK_NONE)
        {
            check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_AVAILABILITY_REDUNDANT, 0, keyword);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (is_strict || is_unavailable || (deprecated && swift))
        {
            *seen = keyword;
            parse->version_tokens[1] = deprecated ? keyword : parse->version_tokens[1];
            parse->versions[1] = deprecated ? (CheckVersionTuple){ .major = 1, .components = 1 } : parse->versions[1];
            next = true;
        }
        else if (t >= end || parser->tokens.kinds[t] != PARSE_KIND_EQUAL)
        {
            check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_AVAILABILITY_EQUAL, 0, keyword);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if ((message || replacement) && (t + 1 >= end || parser->tokens.kinds[t + 1] != PARSE_KIND_STRING))
        {
            check_input_custom_error(reader, attributes, attribute, t + 1, CHECK_ARGUMENT_AVAILABILITY_MESSAGE, 0, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (message || replacement)
        {
            t += 1;
            while (t < end && parser->tokens.kinds[t] == PARSE_KIND_STRING)
            {
                t += 1;
            }
            // message is the last change.
            next = replacement;
        }
        else if (environment && (t + 1 >= end || parser->tokens.kinds[t + 1] != PARSE_KIND_IDENTIFIER))
        {
            check_input_custom_error(reader, attributes, attribute, t + 1, CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT_NAME, 0, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (environment)
        {
            parse->environment = t + 1;
            t += 2;
            next = true;
        }
        else if ((introduced || deprecated) && t + 1 < end && parser->tokens.kinds[t + 1] == PARSE_KIND_IDENTIFIER &&
                 check_input_checks_spelled(reader, parser->tokens.names[t + 1], "NA"))
        {
            parse->unavailable = introduced ? keyword : parse->unavailable;
            t += 2;
            next = true;
        }
        else
        {
            t += 1;
            const CheckAvailabilityVersion version = check_input_availability_number(reader, t);
            if (version.mixed)
            {
                check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_VERSION_SEPARATORS, t, t, BUSTER_CHECK_WARNING_NO_SOURCE, 0,
                                                 CHECK_WARNING_NOTE_KIND_COUNT, 0);
            }
            if (version.status == BUSTER_CHECK_VERSION_EXPECTED)
            {
                check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_AVAILABILITY_VERSION, 0, BUSTER_CHECK_NONE);
                result = BUSTER_CHECK_CUSTOM_ERROR;
            }
            else if (version.status == BUSTER_CHECK_VERSION_ZERO)
            {
                // At its lookahead, past the number.
                check_input_custom_error(reader, attributes, attribute, t + 1, CHECK_ARGUMENT_AVAILABILITY_ZERO, 0, BUSTER_CHECK_NONE);
                result = BUSTER_CHECK_CUSTOM_ERROR;
            }
            else if (version.status == BUSTER_CHECK_VERSION_EMPTY)
            {
                result = BUSTER_CHECK_CUSTOM_DROPPED;
            }
            else if (stage == 3)
            {
                check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_AVAILABILITY_STAGE, 0, keyword);
                result = BUSTER_CHECK_CUSTOM_ERROR;
            }
            else if (parse->keywords[stage] != BUSTER_CHECK_NONE)
            {
                check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_AVAILABILITY_REDUNDANT, 0, keyword);
                result = BUSTER_CHECK_CUSTOM_ERROR;
            }
            else
            {
                parse->keywords[stage] = keyword;
                parse->version_tokens[stage] = t;
                parse->versions[stage] = check_version_tuple(version);
                t += 1;
                next = true;
            }
        }
        more = result == BUSTER_CHECK_CUSTOM_KEPT && next && t < end && parser->tokens.kinds[t] == PARSE_KIND_COMMA;
        t += more ? 1u : 0u;
    }
    if (result == BUSTER_CHECK_CUSTOM_KEPT && t != end)
    {
        check_input_custom_close(reader, attributes, attribute, t, open);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    // unavailable overrides the stages: clang's warning at it, where one is given, which clears them.
    const bool stages = result == BUSTER_CHECK_CUSTOM_KEPT &&
                        (parse->keywords[0] != BUSTER_CHECK_NONE || parse->keywords[1] != BUSTER_CHECK_NONE || parse->keywords[2] != BUSTER_CHECK_NONE);
    if (stages && parse->unavailable != BUSTER_CHECK_NONE)
    {
        check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_UNAVAILABLE, parse->unavailable, end + 1,
                                         BUSTER_CHECK_WARNING_NO_SOURCE, 0, CHECK_WARNING_NOTE_KIND_COUNT, 0);
        for (uint32_t i = 0; i < 3; i += 1)
        {
            parse->keywords[i] = BUSTER_CHECK_NONE;
            parse->version_tokens[i] = BUSTER_CHECK_NONE;
            parse->versions[i] = (CheckVersionTuple){ 0 };
        }
    }
    // anyAppleOS's versions start at 26.
    for (uint32_t i = 0; result == BUSTER_CHECK_CUSTOM_KEPT && parse->platform_index == BUSTER_CHECK_AVAILABILITY_ANY_APPLE && i < 3; i += 1)
    {
        if (parse->version_tokens[i] != BUSTER_CHECK_NONE && parse->versions[i].major < 26)
        {
            check_input_custom_error(reader, attributes, attribute, parse->keywords[i], CHECK_ARGUMENT_AVAILABILITY_ANY_APPLE, 0, parse->version_tokens[i]);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
    }
    attribute->availability = result == BUSTER_CHECK_CUSTOM_KEPT ? parse : NULL;
    return result;
}

// external_source_symbol's arguments in parentheses as clang's ParseExternalSourceSymbolAttribute reads them (#1447):
// clauses after `,`s, each generated_declaration, or language, defined_in or USR, `=` (expected just past the keyword) and
// a string; none twice; then `)`. Whether it is kept or an error.
static uint32_t check_input_external_source_parse(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const Parser* parser = reader->parser;
    const uint32_t end = attribute->argument_end;
    const uint32_t open = attribute->argument_first - 1;
    bool seen[4] = { false, false, false, false };
    uint32_t result = BUSTER_CHECK_CUSTOM_KEPT;
    uint32_t t = attribute->argument_first;
    bool more = true;
    while (more)
    {
        bool next = false;
        const uint32_t keyword = t;
        const uint32_t word = t < end && parser->tokens.kinds[t] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[t] : 0;
        const uint32_t clause = check_input_checks_spelled(reader, word, "language") ? 0u : check_input_checks_spelled(reader, word, "defined_in") ? 1u :
                                check_input_checks_spelled(reader, word, "USR") ? 2u : check_input_checks_spelled(reader, word, "generated_declaration") ? 3u : 4u;
        if (clause == 4)
        {
            check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_SOURCE_KEYWORD, 0, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (clause == 3 && seen[3])
        {
            check_input_custom_error(reader, attributes, attribute, t, CHECK_ARGUMENT_SOURCE_DUPLICATE, 0, keyword);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (clause == 3)
        {
            seen[3] = true;
            t += 1;
            next = true;
        }
        else if (t + 1 >= end || parser->tokens.kinds[t + 1] != PARSE_KIND_EQUAL)
        {
            check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_SOURCE_EQUAL, 0, keyword);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (t + 2 >= end || parser->tokens.kinds[t + 2] != PARSE_KIND_STRING)
        {
            check_input_custom_error(reader, attributes, attribute, t + 2, CHECK_ARGUMENT_SOURCE_STRING, clause, BUSTER_CHECK_NONE);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else if (seen[clause])
        {
            check_input_custom_error(reader, attributes, attribute, keyword, CHECK_ARGUMENT_SOURCE_DUPLICATE, 0, keyword);
            result = BUSTER_CHECK_CUSTOM_ERROR;
        }
        else
        {
            seen[clause] = true;
            t += 2;
            while (t < end && parser->tokens.kinds[t] == PARSE_KIND_STRING)
            {
                t += 1;
            }
            next = true;
        }
        more = result == BUSTER_CHECK_CUSTOM_KEPT && next && t < end && parser->tokens.kinds[t] == PARSE_KIND_COMMA;
        t += more ? 1u : 0u;
    }
    if (result == BUSTER_CHECK_CUSTOM_KEPT && t != end)
    {
        check_input_custom_close(reader, attributes, attribute, t, open);
        result = BUSTER_CHECK_CUSTOM_ERROR;
    }
    return result;
}

// An availability attribute's entry where its declarations meet (#1447, the AvailabilityAttr clang's Sema makes): its
// platform (its index, or BUSTER_CHECK_AVAILABILITY_NAMED with an unknown one's name) and its token, its stages' versions as
// Sema takes them (canonical: check_availability_canonical), unavailable, its first token, where its messages are, and
// whether a redeclaration inherited it.
#define BUSTER_CHECK_AVAILABILITY_NAMED 0x80000000u

typedef struct CheckAvailabilityEntry CheckAvailabilityEntry;
struct CheckAvailabilityEntry
{
    uint32_t platform;
    uint32_t platform_token;
    CheckVersionTuple versions[3];
    uint32_t location;
    bool unavailable;
    bool inherited;
};

// Whether Sema drops an availability attribute of these arguments before it merges it (#1447, handleAvailabilityAttr), with
// its warning: Swift's with a stage but deprecation, or with neither unavailable nor deprecated; Fuchsia's introduced with a
// minor or subminor version.
static bool check_input_availability_drops(const CheckInputAvailabilityParse* parse)
{
    const bool swift = parse->platform_index == BUSTER_CHECK_AVAILABILITY_SWIFT &&
                       (!check_version_empty(parse->versions[0]) || !check_version_empty(parse->versions[2]) ||
                        (parse->unavailable == BUSTER_CHECK_NONE && check_version_empty(parse->versions[1])));
    const bool fuchsia = parse->platform_index == BUSTER_CHECK_AVAILABILITY_FUCHSIA && parse->versions[0].components >= 2;
    return swift || fuchsia;
}

// The entry an availability attribute's record makes (#1447): false for one that makes none (another attribute, one of no
// arguments, anyAppleOS's, which meets nothing here, or one Sema drops or refuses).
static bool check_input_availability_entry(CheckInputReader* reader, const CheckInputAttribute* attribute, CheckAvailabilityEntry* entry)
{
    const CheckInputAvailabilityParse* parse = attribute->kind == CHECK_ATTRIBUTE_UNKNOWN && !attribute->keyword ? attribute->availability : NULL;
    const bool made = parse != NULL && attribute->count_check == BUSTER_CHECK_COUNT_NONE && parse->platform_index != BUSTER_CHECK_AVAILABILITY_ANY_APPLE &&
                      parse->environment == BUSTER_CHECK_NONE && !check_input_availability_drops(parse) &&
                      check_input_attribute_equal(check_input_attribute_name(reader, attribute->name), "availability");
    if (made)
    {
        *entry = (CheckAvailabilityEntry){
            .platform = parse->platform_index != UINT32_MAX ? parse->platform_index : BUSTER_CHECK_AVAILABILITY_NAMED | reader->parser->tokens.names[parse->platform],
            .platform_token = parse->platform,
            .location = attribute->first,
            .unavailable = parse->unavailable != BUSTER_CHECK_NONE,
        };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            check_availability_canonical(parse->platform_index, parse->versions[i], entry->versions + i);
        }
    }
    return made;
}

// Whether two stages' versions match (a redeclaration's versionsMatch): one empty, or both the same.
static bool check_input_availability_match(CheckVersionTuple a, CheckVersionTuple b)
{
    return check_version_empty(a) || check_version_empty(b) || check_version_compare(a, b) == 0;
}

// Whether an entry's stages are out of order (checkAvailabilityAttr): deprecated before introduced, obsoleted before
// introduced, or obsoleted before deprecated, the first of them clang's warning at `location` where `report`, given at
// `completion`, naming the platform (`platform`'s token).
static bool check_input_availability_ordering(CheckInputReader* reader, uint32_t platform, const CheckVersionTuple* versions, uint32_t location, bool report,
                                              uint32_t completion)
{
    static const uint32_t pairs[3][2] = { { 1, 0 }, { 2, 0 }, { 2, 1 } };
    uint32_t failed = 3;
    for (uint32_t p = 0; failed == 3 && p < 3; p += 1)
    {
        const CheckVersionTuple later = versions[pairs[p][0]];
        const CheckVersionTuple earlier = versions[pairs[p][1]];
        failed = !check_version_empty(later) && !check_version_empty(earlier) && check_version_compare(earlier, later) > 0 ? p : 3;
    }
    if (failed != 3 && report)
    {
        const CheckInputAvailabilityText text = {
            .platform = platform,
            .first_stage = pairs[failed][0],
            .second_stage = pairs[failed][1],
            .first = versions[pairs[failed][0]],
            .second = versions[pairs[failed][1]],
        };
        const uint32_t index = check_input_availability_text(reader, text);
        if (index != UINT32_MAX)
        {
            check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_ORDERING, location, completion,
                                             BUSTER_CHECK_WARNING_NO_SOURCE, index, CHECK_WARNING_NOTE_KIND_COUNT, 0);
        }
    }
    return failed != 3;
}

// mergeAvailabilityAttr (#1447) of `incoming` into a declaration's availability entries (`list`, `*count` of them, with room
// for one more), in their order (inherited ones first, as Decl::addAttr keeps them): each of its platform's that doesn't
// match it (a redeclaration's versionsMatch, and unavailable) clang's warning at it with its note at `incoming`, and is
// dropped; each that does lends its stages to the merged versions, unless they are then out of order, its warning at it,
// and it is dropped; then, unless entries of its platform gave nothing more, `incoming` itself where its merged versions
// are in order (its warning at it otherwise). With diagnostics where `report`, given at `completion`.
static void check_input_availability_merge(CheckInputReader* reader, CheckAvailabilityEntry* list, uint32_t* count, const CheckAvailabilityEntry* incoming,
                                           bool report, uint32_t completion)
{
    CheckVersionTuple merged[3] = { incoming->versions[0], incoming->versions[1], incoming->versions[2] };
    bool found = false;
    uint32_t i = 0;
    while (i < *count)
    {
        const CheckAvailabilityEntry* old = list + i;
        bool erase = false;
        if (old->platform == incoming->platform)
        {
            found = true;
            const bool matches = check_input_availability_match(old->versions[0], incoming->versions[0]) &&
                                 check_input_availability_match(incoming->versions[1], old->versions[1]) &&
                                 check_input_availability_match(incoming->versions[2], old->versions[2]) && old->unavailable == incoming->unavailable;
            CheckVersionTuple candidate[3];
            for (uint32_t k = 0; k < 3; k += 1)
            {
                candidate[k] = check_version_empty(merged[k]) ? old->versions[k] : merged[k];
            }
            if (!matches && report)
            {
                check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_MISMATCH, old->location, completion,
                                                 BUSTER_CHECK_WARNING_NO_SOURCE, 0, CHECK_WARNING_NOTE_PREVIOUS_ATTRIBUTE, incoming->location);
            }
            erase = !matches || check_input_availability_ordering(reader, incoming->platform_token, candidate, old->location, report, completion);
            for (uint32_t k = 0; !erase && k < 3; k += 1)
            {
                merged[k] = candidate[k];
            }
        }
        if (erase)
        {
            memmove(list + i, list + i + 1, (uint64_t)(*count - i - 1) * sizeof(*list));
            *count -= 1;
        }
        else
        {
            i += 1;
        }
    }
    bool same = found;
    for (uint32_t k = 0; same && k < 3; k += 1)
    {
        same = check_version_compare(merged[k], incoming->versions[k]) == 0;
    }
    if (!same && !check_input_availability_ordering(reader, incoming->platform_token, merged, incoming->location, report, completion))
    {
        // An inherited one after the inherited ones before it, an own one last.
        uint32_t at = *count;
        while (incoming->inherited && at > 0 && !list[at - 1].inherited)
        {
            at -= 1;
        }
        memmove(list + at + 1, list + at, (uint64_t)(*count - at) * sizeof(*list));
        list[at] = *incoming;
        *count += 1;
    }
}

// A declaration's own availability entries, merged in order from its attributes' records before `limit`, into `list`
// (`*count`), without diagnostics: as each was where its handler merged it.
static void check_input_availability_own(CheckInputReader* reader, const CheckInputAttributes* attributes, uint32_t limit, CheckAvailabilityEntry* list,
                                         uint32_t* count)
{
    for (uint32_t i = 0; attributes != NULL && i < limit && i < attributes->count; i += 1)
    {
        CheckAvailabilityEntry entry;
        if (check_input_availability_entry(reader, attributes->records + i, &entry))
        {
            check_input_availability_merge(reader, list, count, &entry, false, 0);
        }
    }
}

// availability's Sema (#1447, handleAvailabilityAttr) once its subject takes it: its platform clang doesn't know, its
// warning at it; each of its stages' versions in its OS's gap (check_availability_canonical), -Winvalid-version-availability's
// warning at the platform with its note of the version taken instead; Swift's stages but deprecation (or neither
// unavailable nor deprecated) and Fuchsia's minor introduced version, their warnings at it, which drop it; an environment,
// an error in C; and then its merging with its declaration's availability attributes before it
// (check_input_availability_merge). anyAppleOS's merges with nothing. Whether the declaration goes on.
static bool check_input_availability_handle(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const CheckInputAvailabilityParse* parse = attribute->availability;
    const uint32_t platform = parse->platform;
    bool valid = true;
    if (parse->platform_index == UINT32_MAX)
    {
        check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_PLATFORM, platform, attribute->first, platform, 0,
                                         CHECK_WARNING_NOTE_KIND_COUNT, 0);
    }
    for (uint32_t i = 0; parse->platform_index != BUSTER_CHECK_AVAILABILITY_ANY_APPLE && i < 3; i += 1)
    {
        CheckVersionTuple canonical;
        if (!check_availability_canonical(parse->platform_index, parse->versions[i], &canonical))
        {
            const CheckInputAvailabilityText text = { .platform = platform, .first = parse->versions[i], .second = canonical };
            const uint32_t index = check_input_availability_text(reader, text);
            if (index != UINT32_MAX)
            {
                check_input_availability_event(reader, CHECK_WARNING_ID_INVALID_VERSION_AVAILABILITY, CHECK_WARNING_EVENT_AVAILABILITY_VERSION, platform, attribute->first,
                                                 BUSTER_CHECK_WARNING_NO_SOURCE, index, CHECK_WARNING_NOTE_IMPLICIT_VERSION, platform);
            }
        }
    }
    if (check_input_availability_drops(parse))
    {
        const bool swift = parse->platform_index == BUSTER_CHECK_AVAILABILITY_SWIFT;
        check_input_availability_event(reader, CHECK_WARNING_ID_AVAILABILITY, swift ? CHECK_WARNING_EVENT_AVAILABILITY_SWIFT : CHECK_WARNING_EVENT_AVAILABILITY_FUCHSIA,
                                         attribute->first, attribute->first, BUSTER_CHECK_WARNING_NO_SOURCE, 0, CHECK_WARNING_NOTE_KIND_COUNT, 0);
        attribute->ignored = true;
        attribute->subject_ignored = true;
    }
    else if (parse->environment != BUSTER_CHECK_NONE)
    {
        check_input_custom_error(reader, attributes, attribute, parse->environment, CHECK_ARGUMENT_AVAILABILITY_ENVIRONMENT, 0, BUSTER_CHECK_NONE);
        valid = false;
    }
    else if (parse->platform_index != BUSTER_CHECK_AVAILABILITY_ANY_APPLE)
    {
        const uint64_t mark = reader->scratch->length;
        const uint32_t index = (uint32_t)(attribute - attributes->records);
        CheckAvailabilityEntry* list = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAvailabilityEntry, (uint64_t)index + 1);
        reader->output->resource |= list == NULL;
        uint32_t count = 0;
        CheckAvailabilityEntry entry;
        if (list != NULL && check_input_availability_entry(reader, attribute, &entry))
        {
            check_input_availability_own(reader, attributes, index, list, &count);
            check_input_availability_merge(reader, list, &count, &entry, true, attribute->end);
        }
        arena_pop_to(reader->scratch, mark);
    }
    return valid;
}

// A redeclaration's availability (#1447, mergeDeclAttributes' mergeAvailabilityAttr of the previous declaration's, each
// inherited): the entries of its entity's earlier declarations it sees (`object`'s before `token`; all where it has no
// visible previous declaration, `visible`), each declaration's own merged with those of the one before it, in source
// order, without diagnostics, as each was; then the redeclaration's own, and the last of those merged into them, with
// their warnings, given at `point`.
static void check_input_availability_redeclaration(CheckInputReader* reader, uint32_t token, uint32_t object, bool visible, uint32_t point)
{
    const uint64_t mark = reader->scratch->length;
    const bool scalar = reader->parser->options.scalar;
    const CheckSparseWide all = reader->output->attributes;
    const CheckInputAttributes* own = check_input_attributes_at(reader->output, token);
    // Room for every record of the declarations, twice: the earlier list and the one being made.
    uint64_t room = own != NULL ? own->count + 1u : 1u;
    for (uint32_t first = check_input_attributed_next(all, 0, token, scalar); first < token; first = check_input_attributed_next(all, first + 16, token, scalar))
    {
        for (uint32_t selected = check_input_attributed_group(all, first, token, scalar); selected != 0; selected &= selected - 1)
        {
            const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
            const bool seen = reader->objects[d] == object && (!visible || check_input_declaration_visible(reader, d, token));
            const CheckInputAttributes* attributes = seen ? check_input_attributes_at(reader->output, d) : NULL;
            room += attributes != NULL ? attributes->count : 0u;
        }
    }
    CheckAvailabilityEntry* earlier = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAvailabilityEntry, room);
    CheckAvailabilityEntry* made = earlier != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckAvailabilityEntry, room) : NULL;
    reader->output->resource |= made == NULL;
    uint32_t earlier_count = 0;
    for (uint32_t first = made != NULL ? check_input_attributed_next(all, 0, token, scalar) : token; first < token;
         first = check_input_attributed_next(all, first + 16, token, scalar))
    {
        for (uint32_t selected = check_input_attributed_group(all, first, token, scalar); selected != 0; selected &= selected - 1)
        {
            const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
            const bool seen = reader->objects[d] == object && (!visible || check_input_declaration_visible(reader, d, token));
            const CheckInputAttributes* attributes = seen ? check_input_attributes_at(reader->output, d) : NULL;
            uint32_t made_count = 0;
            if (attributes != NULL)
            {
                check_input_availability_own(reader, attributes, attributes->count, made, &made_count);
                for (uint32_t i = 0; i < earlier_count; i += 1)
                {
                    CheckAvailabilityEntry inherited = earlier[i];
                    inherited.inherited = true;
                    check_input_availability_merge(reader, made, &made_count, &inherited, false, 0);
                }
                memcpy(earlier, made, (uint64_t)made_count * sizeof(*made));
                earlier_count = made_count;
            }
        }
    }
    uint32_t made_count = 0;
    if (made != NULL && earlier_count != 0)
    {
        check_input_availability_own(reader, own, own != NULL ? own->count : 0, made, &made_count);
        for (uint32_t i = 0; i < earlier_count; i += 1)
        {
            CheckAvailabilityEntry inherited = earlier[i];
            inherited.inherited = true;
            check_input_availability_merge(reader, made, &made_count, &inherited, true, point);
        }
    }
    arena_pop_to(reader->scratch, mark);
}

// Whether a library builtin's row makes it printf-like or scanf-like (its attributes' p, P, s or S with an argument):
// clang's AddKnownFunctionAttributes gives such a builtin's declaration an implicit format attribute.
static bool check_input_checks_format_like(const BuiltinIntrinsicInfo* info)
{
    bool result = false;
    uint32_t i = 0;
    while (!result && i < info->attributes_length)
    {
        const char c = info->attributes[i];
        const bool argued = i + 1 < info->attributes_length && info->attributes[i + 1] == ':';
        result = argued && (c == 'p' || c == 'P' || c == 's' || c == 'S');
        i += argued ? 2u : 1u;
        while (argued && i < info->attributes_length && info->attributes[i] != ':')
        {
            i += 1;
        }
        i += argued ? 1u : 0u;
    }
    return result;
}

// The library builtin a function's declaration at `declaration` is, as clang's getBuiltinID finds it, or NULL: the C
// library's function of its name where the unit's options take it (check_input_library_enabled), declared first at file
// scope with external linkage and the builtin's own type (Sema's BuiltinAttr), and not static (getBuiltinID's).
static const BuiltinIntrinsicInfo* check_input_checks_library(CheckInputReader* reader, uint32_t declaration)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint32_t object = reader->objects[declaration];
    const uint32_t first = object != 0 && object <= count ? object - 1 : declaration;
    const BuiltinIntrinsicInfo* info = parser->tokens.kinds[declaration] == PARSE_KIND_IDENTIFIER ? check_input_implicit_info(reader, parser->tokens.names[declaration]) : NULL;
    const bool library = info != NULL && memchr(info->attributes, 'f', info->attributes_length) != NULL && check_input_library_enabled(info, &reader->pp->options);
    const bool file = check_sparse_get(reader->output->declaration_bodies, first) >= count;
    const bool external = check_input_linkage(reader->declaration_bits, declaration) == 2 && check_input_linkage(reader->declaration_bits, first) == 2;
    const bool implicit = reader->output->function_flags != NULL && (reader->output->function_flags[first] & CHECK_INPUT_FUNCTION_IMPLICIT_LIBRARY) != 0;
    bool builtin = library && (file || implicit) && external && reader->declaration_kinds[first] == C_BINDING_FUNCTION;
    if (builtin && !implicit && memchr(info->attributes, 'T', info->attributes_length) == NULL)
    {
        const CheckInputIntrinsicDeclaration decoded = check_input_intrinsic_decode(reader, info);
        const TypeId declared = reader->declared_types[first];
        builtin = decoded.types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS && declared != 0 &&
                  type_same_canonical(reader->types, decoded.types.function_type, declared) == TYPE_ANSWER_YES;
    }
    return builtin ? info : NULL;
}

// diagnose_as_builtin's checks (#1447, clang's handleDiagnoseAsBuiltinAttr), on a function of type `function` named at `name`
// whose declarator's named parameters CheckInputReader.attribute_parameters links. Its first argument names a builtin
// function (getBuiltinID): a name no declaration hides of one of the catalogue's builtins this language has but a library
// function's, which clang declares implicitly, or the unit's declaration of a library function that is its builtin
// (check_input_checks_library); any other first argument is clang's error at its expression's place. Then its builtin's
// parameters (its prototype as GetBuiltinType declares it, past any custom checking) must be as many as its other
// arguments, each of which is the index, from 1, of the function's parameter whose type (canonical, unqualified) is that
// builtin parameter's: in 32 bits (checkUInt32Argument's errors), within the function's parameters. Refused by name where
// clang's diagnostics are its own (an undeclared library function's implicit declaration, a builtin's name in another
// expression, a redeclared builtin), where it crashes (an index of 0), where the checker can't spell a builtin parameter's
// type in its error, and for more than 32 arguments. A definition's identifier list compares its parameters by their
// declarations' types, the promoted prototype's standing aside (clang's ParmVarDecl types). Nothing else changes: the
// -Wfortify-source warnings it would give the function's calls, as a builtin's, the checker doesn't make. Whether the
// declaration goes on.
static bool check_input_attribute_diagnose_builtin(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute, TypeId function,
                                                   uint32_t name)
{
    const Parser* parser = reader->parser;
    TypeTable* types = reader->types;
    const InternTable* table = reader->pp->table;
    const uint32_t first = attribute->argument_first;
    const uint32_t first_end = check_input_checks_argument_end(parser, first, attribute->argument_end);
    const bool lone = first_end == first + 1 && parser->tokens.kinds[first] == PARSE_KIND_IDENTIFIER;
    const uint32_t word = lone ? parser->tokens.names[first] : 0;
    const uint32_t handle = lone ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, word) : 0;
    const uint8_t bound = handle != 0 ? reader->bindings.kind[handle - 1] : 0;
    const BuiltinIntrinsicInfo* row = lone ? builtin_intrinsic_lookup((const char*)intern_bytes(table, word), intern_length(table, word)) : NULL;
    const bool library = row != NULL && memchr(row->attributes, 'f', row->attributes_length) != NULL;
    const PPOptions* options = &reader->pp->options;
    const bool language = row != NULL && (strcmp(row->language_token, "ALL_LANGUAGES") == 0 ||
                                          (strcmp(row->language_token, "ALL_GNU_LANGUAGES") == 0 && options->language.gnu) ||
                                          (strcmp(row->language_token, "C23_LANG") == 0 && options->language.standard >= 2023));
    const BuiltinIntrinsicInfo* info = NULL;
    bool refused = false;
    if (lone && handle == 0)
    {
        info = row != NULL && !library && language ? row : NULL;
        refused = info == NULL;
    }
    else if (lone && bound == C_BINDING_FUNCTION && row != NULL && !library)
    {
        // A redeclared builtin (`int __builtin_abs(int);`) is still that builtin where it has external linkage, as
        // getBuiltinID finds it; a static one is no builtin. Its type clang compared where it was declared.
        const uint32_t declaration = reader->bindings.declaration[handle - 1];
        info = language && check_input_linkage(reader->declaration_bits, declaration) == 2 ? row : NULL;
    }
    else if (lone && bound == C_BINDING_FUNCTION)
    {
        info = check_input_checks_library(reader, reader->bindings.declaration[handle - 1]);
    }
    else if (lone)
    {
        refused = bound == C_BINDING_TYPEDEF;
    }
    // A builtin's name in parentheses alone is no builtin's reference, at its first `(`. In any other expression, a builtin's
    // name that isn't called is clang's "builtin functions must be directly called", at the name, the first alone; the
    // expression clang recovers then is no builtin's, its error at its first token (a lone `&name`'s at the token after
    // it), but in sizeof's, alignof's or typeof's operand, which clang doesn't recover, the attribute is dropped.
    uint32_t opened = 0;
    while (!lone && first + opened < first_end && parser->tokens.kinds[first + opened] == PARSE_KIND_L_PAREN)
    {
        opened += 1;
    }
    const bool parenthesized = opened != 0 && first + 2 * opened + 1 == first_end && parser->tokens.kinds[first + opened] == PARSE_KIND_IDENTIFIER &&
                               parser->tree.parents[first] == first_end - 1;
    uint32_t uncalled = BUSTER_CHECK_NONE;
    for (uint32_t token = first; !refused && uncalled == BUSTER_CHECK_NONE && !lone && !parenthesized && token < first_end; token += 1)
    {
        const uint32_t spelled = parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER ? parser->tokens.names[token] : 0;
        // A builtin's name no declaration hides, or that an external redeclaration of the builtin keeps (a static one hides it).
        const uint32_t spelled_handle = spelled != 0 ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, spelled) : 0;
        const uint32_t spelled_declaration = spelled_handle != 0 ? reader->bindings.declaration[spelled_handle - 1] : BUSTER_CHECK_NONE;
        const bool redeclared = spelled_handle != 0 && reader->bindings.kind[spelled_handle - 1] == C_BINDING_FUNCTION &&
                                check_input_linkage(reader->declaration_bits, spelled_declaration) == 2;
        const BuiltinIntrinsicInfo* named = spelled != 0 && (spelled_handle == 0 || redeclared) ?
                                            builtin_intrinsic_lookup((const char*)intern_bytes(table, spelled), intern_length(table, spelled)) : NULL;
        const bool called = token + 1 < first_end && parser->tokens.kinds[token + 1] == PARSE_KIND_L_PAREN && parser->tree.roles[token + 1] == PARSE_ROLE_OPEN_CALL;
        // An undeclared library function's name is its implicit declaration, which clang's diagnostics are their own.
        // A declared library function is an ordinary function.
        const bool library_name = named != NULL && memchr(named->attributes, 'f', named->attributes_length) != NULL;
        refused = library_name && spelled_handle == 0;
        uncalled = named != NULL && !library_name && !called ? token : BUSTER_CHECK_NONE;
    }
    // The builtin's parameters, its prototype decoded without its custom checking, as clang's declaration of it has them.
    CheckInputIntrinsicDeclaration decoded = { 0 };
    if (!refused && info != NULL)
    {
        BuiltinIntrinsicInfo plain = *info;
        plain.attributes = "";
        plain.attributes_length = 0;
        plain.custom_typechecking = false;
        decoded = check_input_intrinsic_decode(reader, &plain);
        reader->output->resource |= decoded.types.status == BUILTIN_INTRINSIC_TYPES_CAPACITY;
        refused = decoded.types.status != BUILTIN_INTRINSIC_TYPES_SUCCESS;
    }
    const bool prototyped = (types->types.flags[type_index(function)] & BUSTER_TYPE_FLAG_PROTOTYPE) != 0;
    uint32_t parameter_count = 0;
    const TypeId* parameter_types = type_parameters(types, function, &parameter_count);
    const uint32_t declared = prototyped ? parameter_count : 0;
    refused |= !prototyped && reader->attribute_parameters != 0;
    // A K&R definition's type counts its parameters' promoted types, but clang compares the declarations' own:
    // subject_identifier_list's parameters' declared types, in the same order, stand in for the type's.
    if (!refused && reader->subject_identifier_list && reader->attribute_parameters != 0)
    {
        const uint32_t names = reader->attribute_parameters - 1;
        uint32_t count = 0;
        for (uint32_t parameter = names; parameter != BUSTER_CHECK_NONE; parameter = check_sparse_get(reader->parameter_next, parameter))
        {
            count += 1;
        }
        TypeId* kr = arena_push_array(reader->scratch, count, sizeof(TypeId), 4);
        reader->output->resource |= kr == 0;
        if (kr != NULL)
        {
            uint32_t k = 0;
            for (uint32_t parameter = names; parameter != BUSTER_CHECK_NONE; parameter = check_sparse_get(reader->parameter_next, parameter))
            {
                kr[k++] = reader->declared_types[parameter];
            }
            parameter_types = kr;
        }
    }
    bool valid = !refused && uncalled == BUSTER_CHECK_NONE;
    if (refused)
    {
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
    }
    else if (uncalled != BUSTER_CHECK_NONE)
    {
        // At the outermost parentheses around the name, the operand clang checks.
        uint32_t operand = uncalled;
        uint32_t after = uncalled;
        while (operand > first && after + 1 < first_end && parser->tokens.kinds[operand - 1] == PARSE_KIND_L_PAREN && parser->tree.parents[operand - 1] == after + 1)
        {
            operand -= 1;
            after += 1;
        }
        check_input_report(reader, operand, CHECK_INPUT_BUILTIN_NOT_CALLED);
        bool unevaluated = false;
        for (uint32_t at = first; !unevaluated && at < uncalled; at += 1)
        {
            const uint32_t word = parser->tokens.names[at];
            const bool keyword = word == C_NAME_SIZEOF || word == C_NAME_ALIGNOF || word == C_NAME_ALIGNOF_C11 || word == C_NAME_GNU_ALIGNOF ||
                                 word == C_NAME_GNU_ALIGNOF2 || word == C_NAME_TYPEOF || word == C_NAME_TYPEOF_UNQUAL || word == C_NAME_GNU_TYPEOF ||
                                 word == C_NAME_GNU_TYPEOF2 || word == C_NAME_GNU_TYPEOF_UNQUAL || word == C_NAME_GNU_TYPEOF_UNQUAL2;
            unevaluated = keyword && (at + 1 == uncalled || (parser->tokens.kinds[at + 1] == PARSE_KIND_L_PAREN && parser->tree.parents[at + 1] > uncalled));
        }
        if (!unevaluated)
        {
            const bool address = parser->tokens.kinds[first] == PARSE_KIND_AMP && first + 2 == first_end;
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, address ? first_end : first, CHECK_ARGUMENT_BUILTIN_FUNCTION, 1);
            // After the name's error, wherever it is.
            for (uint32_t r = before; r < reader->output->report_count; r += 1)
            {
                reader->output->reports[r].sort = operand;
                reader->output->reports[r].repeated = true;
            }
        }
    }
    else if (info == NULL)
    {
        // At the expression's first token, as clang gives it (a group's `(`, a binary operator's left operand).
        check_input_attribute_argument_error(reader, attributes, attribute, first, CHECK_ARGUMENT_BUILTIN_FUNCTION, 1);
        valid = false;
    }
    else if (attribute->argument_count - 1 != decoded.types.parameter_count)
    {
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_BUILTIN_COUNT, decoded.types.parameter_count);
        valid = false;
    }
    else if (attribute->argument_count > 32)
    {
        // The argument masks hold 32; no builtin takes as many parameters.
        check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
        valid = false;
    }
    uint32_t argument = first_end + 1;
    for (uint32_t i = 1; valid && i < attribute->argument_count; i += 1)
    {
        const uint32_t stop = check_input_checks_argument_end(parser, argument, attribute->argument_end);
        const bool evaluated = ((attribute->constants >> i) & 1u) != 0 && attribute->argument_values != NULL;
        const bool wide = ((attribute->wide >> i) & 1u) != 0;
        const uint64_t index = evaluated ? (uint32_t)attribute->argument_values[i] : 0;
        const uint64_t named = (uint64_t)name << 32;
        if (!evaluated)
        {
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_PARAMETER_INTEGER, i + 1);
            valid = false;
        }
        else if (wide)
        {
            check_input_attribute_too_large(reader, attributes, attribute, argument, stop, attribute->argument_values[i]);
            valid = false;
        }
        else if (index == 0)
        {
            // clang's handler reads the parameter before the first, and crashes.
            check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            valid = false;
        }
        else if (index > declared)
        {
            const uint32_t before = reader->output->report_count;
            check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_BUILTIN_BOUNDS, declared);
            if (reader->output->report_count > before)
            {
                reader->output->reports[before].value = index | named;
            }
            valid = false;
        }
        else if (type_same_canonical(types, type_unqualified(decoded.types.parameters[i - 1]), type_unqualified(parameter_types[index - 1])) != TYPE_ANSWER_YES)
        {
            // The function's parameter's type as its declaration has it (its typedef's spelling where its adjustment kept
            // it), and the builtin's as clang spells it, which the driver spells again from its row.
            // A named type (FILE, jmp_buf, sigjmp_buf, ucontext_t) the prototype took, the unit's typedef's, which the driver
            // spells (CheckInput.implicit_library_types).
            static const char* const spellings[4] = { "FILE", "jmp_buf", "sigjmp_buf", "ucontext_t" };
            const char* jump = memchr(info->encoding, 'J', info->encoding_length);
            const bool signal = jump != NULL && jump != info->encoding && jump[-1] == 'S';
            const bool taken[4] = { memchr(info->encoding, 'P', info->encoding_length) != NULL, jump != NULL && !signal, signal,
                                    memchr(info->encoding, 'K', info->encoding_length) != NULL };
            const char* found[4] = { NULL, NULL, NULL, NULL };
            for (uint32_t k = 0; k < 4; k += 1)
            {
                const TypeId named_type = taken[k] ? check_input_file_type(reader, spellings[k]) : 0;
                reader->output->implicit_library_types[k] = named_type != 0 ? named_type : reader->output->implicit_library_types[k];
                found[k] = named_type != 0 ? spellings[k] : NULL;
            }
            const CBuiltinLibraryNamed named_types = { .file = found[0], .jmp_buf = found[1], .sigjmp_buf = found[2], .ucontext = found[3] };
            char spelling[256];
            bool sized = false;
            const bool spellable = cbuiltin_library_spell_parameter(info->encoding, info->encoding_length, info->attributes, info->attributes_length, i - 1, true,
                                                                    parser->language.standard >= 1999, &named_types, spelling, sizeof(spelling), &sized);
            const uint32_t place = name < parser->tokens.count ? check_input_checks_parameter_place(reader, name, (uint32_t)index - 1) : BUSTER_CHECK_NONE;
            const TypeId parameter = parameter_types[index - 1];
            const bool declarator = place < parser->tokens.count && parser->tokens.kinds[place] == PARSE_KIND_IDENTIFIER && reader->declaration_kinds[place] == C_BINDING_OBJECT;
            const TypeId own = declarator ? reader->declared_types[place] : 0;
            const bool spelled = declarator && own != 0 && type_unqualified(type_strip_aligned(types, own)) == type_unqualified(type_strip_aligned(types, parameter));
            if (!spellable)
            {
                check_input_attribute_error(reader, attributes, attribute->name_token, attribute->end, CHECK_ATTRIBUTE_ERROR_UNKNOWN);
            }
            else
            {
                const uint32_t before = reader->output->report_count;
                check_input_attribute_argument_error(reader, attributes, attribute, argument, CHECK_ARGUMENT_BUILTIN_TYPES, i);
                if (reader->output->report_count > before)
                {
                    reader->output->reports[before].value = index | named;
                    reader->output->reports[before].type = spelled ? own : parameter;
                    reader->output->reports[before].type_spelling = spelled ? check_input_typedef_spelling(reader->output, parser, place) : 0;
                }
            }
            valid = false;
        }
        argument = stop + 1;
    }
    return valid;
}

// modular_format's format (#1447, clang's checkAttributesAfterMerging: err_modular_format_attribute_no_format): a function's
// declaration at `token` with a modular_format attribute and no format attribute, its own or one it inherited
// (check_input_contract_inherit), nor a printf-like or scanf-like library builtin's implicit one, is clang's error at its
// name.
static void check_input_modular_format(CheckInputReader* reader, uint32_t token)
{
    const CheckInputAttributes* attributes = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : NULL;
    const CheckInputAttribute* modular = NULL;
    bool format = false;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        modular = modular == NULL && check_input_checks_named(reader, attribute, "modular_format") ? attribute : modular;
        format |= attribute->kind == CHECK_ATTRIBUTE_FORMAT && !attribute->ignored && !attribute->subject_ignored;
    }
    const BuiltinIntrinsicInfo* library = modular != NULL && !format ? check_input_checks_library(reader, token) : NULL;
    if (modular != NULL && !format && (library == NULL || !check_input_checks_format_like(library)))
    {
        const uint32_t before = reader->output->report_count;
        check_input_report_argument(reader, token, CHECK_INPUT_ATTRIBUTE_ARGUMENT, CHECK_ARGUMENT_REQUIRES_FORMAT);
        if (reader->output->report_count > before)
        {
            reader->output->reports[before].attribute = modular->first;
        }
    }
}

// What clang checks where a function's or a variable's redeclaration at `token` merges the attributes of its entity's
// earlier declarations (#1447, mergeDeclAttributes, in their order: an earlier declaration's before a later one's, each's
// in source order), those of `previous`'s entity before it that it sees (all where it has no visible previous
// declaration, `visible`, as check_input_attribute_merge_exclusions finds them): an earlier enforce_tcb's TCB that the
// redeclaration's own enforce_tcb_leaf has, or the other way around, clang's error at the redeclaration's, with its note
// at the earlier one (mergeEnforceTCBAttrImpl); and an earlier modular_format whose arguments its own one's differ from,
// the error at its own, with its note at the earlier one.
static void check_input_attribute_merge_checks(CheckInputReader* reader, uint32_t token, uint32_t previous, bool visible, uint32_t point)
{
    const CheckInputAttributes* own = check_sparse_wide_present(reader->output->attributes) ? check_input_attributes_at(reader->output, token) : NULL;
    const uint32_t object = reader->objects != NULL ? reader->objects[previous] : 0;
    bool checked = false;
    const CheckInputAttribute* modular = NULL;
    for (uint32_t i = 0; own != NULL && i < own->count; i += 1)
    {
        checked |= check_input_tcb_kind(reader, own->records + i) != 0;
        modular = modular == NULL && check_input_checks_named(reader, own->records + i, "modular_format") ? own->records + i : modular;
    }
    checked |= modular != NULL;
    bool reported = false;
    const bool scalar = reader->parser->options.scalar;
    const CheckSparseWide all = reader->output->attributes;
    for (uint32_t first = checked && !reader->lane && object != 0 ? check_input_attributed_next(all, 0, token, scalar) : token; !reported && first < token;
         first = check_input_attributed_next(all, first + 16, token, scalar))
    {
        const uint32_t lanes = check_input_attributed_group(all, first, token, scalar);
        for (uint32_t selected = lanes; !reported && selected != 0; selected &= selected - 1)
        {
            const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
            const bool seen = reader->objects[d] == object && (!visible || check_input_declaration_visible(reader, d, token));
            const CheckInputAttributes* earlier = seen ? check_input_attributes_at(reader->output, d) : NULL;
            for (uint32_t i = 0; !reported && earlier != NULL && i < earlier->count; i += 1)
            {
                const CheckInputAttribute* old = earlier->records + i;
                const uint32_t kind = check_input_tcb_kind(reader, old);
                const String8 name = kind != 0 ? check_input_tcb_name(reader, old) : (String8){ 0 };
                const CheckInputAttribute* conflict = NULL;
                for (uint32_t j = 0; kind != 0 && conflict == NULL && j < own->count; j += 1)
                {
                    const uint32_t other = check_input_tcb_kind(reader, own->records + j);
                    conflict = other != 0 && other != kind && check_input_attribute_text_equal(name, check_input_tcb_name(reader, own->records + j)) ? own->records + j : NULL;
                }
                const bool differs = modular != NULL && check_input_checks_named(reader, old, "modular_format") && !check_input_modular_same(reader, old, modular);
                const CheckInputAttribute* at = conflict != NULL ? conflict : differs ? modular : NULL;
                if (at != NULL)
                {
                    const uint32_t before = reader->output->report_count;
                    check_input_report_argument(reader, at->first, CHECK_INPUT_ATTRIBUTE_ARGUMENT, conflict != NULL ? CHECK_ARGUMENT_TCB_CONFLICT : CHECK_ARGUMENT_DIFFERENT_ARGUMENTS);
                    if (reader->output->report_count > before)
                    {
                        CheckInputReport* report = reader->output->reports + before;
                        report->attribute = at->first;
                        report->value = conflict != NULL ? (uint64_t)old->name_token | (uint64_t)conflict->argument_first << 32 : 0;
                        report->note_token = old->first;
                        report->note_kind = CHECK_INPUT_NOTE_CONFLICTING_ATTRIBUTE;
                    }
                    reported = true;
                }
            }
        }
    }
    // Its availability's, which a redeclaration's own attributes needn't have to meet its earlier declarations' (after the
    // errors above, which end the unit).
    if (!reported && !reader->lane && object != 0 && check_sparse_wide_present(all))
    {
        check_input_availability_redeclaration(reader, token, object, visible, point);
    }
}

// The kept enforce_tcb attributes (`leaves`: and enforce_tcb_leaf) of `declaration`'s function, its own and those of its
// entity's earlier declarations it sees, in clang's order (an earlier declaration's before a later one's, each's in source
// order: Decl::addAttr puts inherited ones first), into `list` (at most `capacity`); their count.
static uint32_t check_input_tcb_collect(CheckInputReader* reader, uint32_t declaration, bool leaves, const CheckInputAttribute** list, uint32_t capacity)
{
    const uint32_t object = reader->objects[declaration];
    const bool scalar = reader->parser->options.scalar;
    const CheckSparseWide all = reader->output->attributes;
    uint32_t total = 0;
    for (uint32_t first = object != 0 ? check_input_attributed_next(all, 0, declaration + 1, scalar) : declaration + 1; first <= declaration;
         first = check_input_attributed_next(all, first + 16, declaration + 1, scalar))
    {
        const uint32_t lanes = check_input_attributed_group(all, first, declaration + 1, scalar);
        for (uint32_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t d = first + (uint32_t)__builtin_ctz(selected);
            const bool seen = reader->objects[d] == object && reader->declaration_kinds[d] == C_BINDING_FUNCTION && check_input_declaration_visible(reader, d, declaration);
            const CheckInputAttributes* attributes = seen ? check_input_attributes_at(reader->output, d) : NULL;
            for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
            {
                const uint32_t kind = check_input_tcb_kind(reader, attributes->records + i);
                if ((kind == 1 || (leaves && kind == 2)) && total < capacity)
                {
                    list[total] = attributes->records + i;
                }
                total += kind == 1 || (leaves && kind == 2) ? 1u : 0u;
            }
        }
    }
    return total;
}

// The function a call's callee names directly, as clang's BuildCallExpr finds it (its NDecl): the callee's tokens before
// the call's `(` at `call`, from `first`, through parentheses and one `&` (and parentheses inside it); the name's token,
// and in `*begin` the callee's first token, the call's; else BUSTER_CHECK_NONE.
static uint32_t check_input_tcb_callee(const CheckInputReader* reader, uint32_t first, uint32_t call, uint32_t* begin)
{
    const Parser* parser = reader->parser;
    const uint32_t last = call > first ? call - 1 : BUSTER_CHECK_NONE;
    uint32_t start = last != BUSTER_CHECK_NONE && parser->tokens.kinds[last] == PARSE_KIND_IDENTIFIER ? last : BUSTER_CHECK_NONE;
    if (last != BUSTER_CHECK_NONE && parser->tokens.kinds[last] == PARSE_KIND_R_PAREN && parser->tree.roles[last] == PARSE_ROLE_CLOSE_GROUP)
    {
        // The group's `(`, back from its `)` (a `(`'s parent is its `)`).
        uint32_t open = last;
        while (open > first && !(parser->tokens.kinds[open - 1] == PARSE_KIND_L_PAREN && parser->tree.parents[open - 1] == last))
        {
            open -= 1;
        }
        start = open > first ? open - 1 : BUSTER_CHECK_NONE;
    }
    *begin = start;
    uint32_t at = start;
    uint32_t end = call;
    bool addressed = false;
    bool unwrapping = at != BUSTER_CHECK_NONE;
    while (unwrapping && at + 1 < end)
    {
        const uint8_t kind = parser->tokens.kinds[at];
        const bool group = kind == PARSE_KIND_L_PAREN && parser->tree.roles[at] == PARSE_ROLE_OPEN_GROUP && parser->tree.parents[at] == end - 1;
        const bool address = !addressed && kind == PARSE_KIND_AMP && parser->tree.roles[at] == PARSE_ROLE_PREFIX;
        at += group || address ? 1u : 0u;
        end -= group ? 1u : 0u;
        addressed |= address;
        unwrapping = group || address;
    }
    return at != BUSTER_CHECK_NONE && at + 1 == end && parser->tokens.kinds[at] == PARSE_KIND_IDENTIFIER ? at : BUSTER_CHECK_NONE;
}

// clang's TCB enforcement (#1447, Sema::CheckTCBEnforcement): in the body of a function its enforce_tcb attributes (its own
// and its earlier declarations') put in TCBs, each call of a function its callee names directly (check_input_tcb_callee)
// that isn't a library builtin (check_input_checks_library) gets -Wtcb-enforcement's warning for each of those TCBs the
// function isn't in (by its own or its earlier declarations' enforce_tcb or enforce_tcb_leaf, those its use sees), in
// their order, at the call's first token, given when the call is complete (its `)`). A call in an operand sizeof,
// _Alignof or typeof doesn't evaluate, in a _Generic's controlling expression or in an attribute is none of clang's
// evaluated ones; a function's own name may call it.
static void check_input_tcb_calls(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    CheckInput* input = reader->output;
    const uint32_t count = parser->tokens.count;
    const bool vector = BUSTER_VECTOR_PATH(!parser->options.scalar);
    const uint64_t mark = reader->scratch->length;
    for (uint32_t opening = check_sparse_present(input->body_declarations) ? check_sparse_next(input->body_declarations, 0, count, vector) : count; opening < count;
         opening = check_sparse_next(input->body_declarations, opening + 1, count, vector))
    {
        const uint32_t function = check_sparse_get(input->body_declarations, opening);
        const uint32_t closer = parser->tree.parents[opening];
        const uint32_t tcbs = function < count && closer < count ? check_input_tcb_collect(reader, function, false, NULL, 0) : 0;
        const CheckInputAttribute** caller = tcbs != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, const CheckInputAttribute*, tcbs) : NULL;
        input->resource |= tcbs != 0 && caller == NULL;
        if (caller != NULL)
        {
            check_input_tcb_collect(reader, function, false, caller, tcbs);
        }
        uint32_t t = caller != NULL ? opening + 1 : closer;
        while (caller != NULL && t < closer)
        {
            const uint8_t kind = parser->tokens.kinds[t];
            const bool grouped = t + 1 < closer && parser->tokens.kinds[t + 1] == PARSE_KIND_L_PAREN && parser->tree.parents[t + 1] > t + 1 &&
                                 parser->tree.parents[t + 1] < closer;
            const bool unevaluated = kind == PARSE_KIND_SIZEOF || kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATTRIBUTE;
            // A call, not a declarator's parameters: its callee's name declares nothing but an implicit declaration's.
            const bool declarator = t > 0 && reader->declaration_kinds[t - 1] != 0 &&
                                    !(input->function_flags != NULL && (input->function_flags[t - 1] & CHECK_INPUT_FUNCTION_IMPLICIT) != 0);
            const bool call = parser->tree.roles[t] == PARSE_ROLE_OPEN_CALL && !declarator;
            if (unevaluated)
            {
                t = grouped ? parser->tree.parents[t + 1] + 1 : kind != PARSE_KIND_ATTRIBUTE && reader->ends != NULL ? reader->ends[t] + 1 : t + 1;
                continue;
            }
            if (kind == PARSE_KIND_GENERIC && grouped)
            {
                // Its associations are evaluated, its controlling expression isn't.
                t = check_input_checks_argument_end(parser, t + 2, parser->tree.parents[t + 1]) + 1;
                continue;
            }
            uint32_t begin = BUSTER_CHECK_NONE;
            const uint32_t callee = call ? check_input_tcb_callee(reader, opening, t, &begin) : BUSTER_CHECK_NONE;
            const uint32_t declaration = callee != BUSTER_CHECK_NONE ? input->declarations[callee] : BUSTER_CHECK_NONE;
            const bool direct = declaration < count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION && check_input_checks_library(reader, declaration) == NULL;
            const uint32_t own = direct ? check_input_tcb_collect(reader, declaration, true, NULL, 0) : 0;
            const CheckInputAttribute** callee_tcbs = own != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, const CheckInputAttribute*, own) : NULL;
            input->resource |= own != 0 && callee_tcbs == NULL;
            if (callee_tcbs != NULL)
            {
                check_input_tcb_collect(reader, declaration, true, callee_tcbs, own);
            }
            for (uint32_t i = 0; direct && (own == 0 || callee_tcbs != NULL) && i < tcbs; i += 1)
            {
                const String8 name = check_input_tcb_name(reader, caller[i]);
                bool member = false;
                for (uint32_t j = 0; !member && j < own; j += 1)
                {
                    member = check_input_attribute_text_equal(name, check_input_tcb_name(reader, callee_tcbs[j]));
                }
                if (!member)
                {
                    const CheckWarningEvent event =
                    {
                        .id = CHECK_WARNING_ID_TCB_ENFORCEMENT,
                        .kind = CHECK_WARNING_EVENT_TCB_VIOLATION,
                        .token = begin,
                        .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                        .completion_kind = CHECK_WARNING_COMPLETE_AFTER_DELIMITER,
                        .completion = parser->tree.parents[t],
                        .payload = { .name = parser->tokens.names[callee], .token = caller[i]->argument_first },
                    };
                    check_input_warning_event(reader, &event, NULL, 0);
                }
            }
            t += 1;
        }
        arena_pop_to(reader->scratch, mark);
    }
}
