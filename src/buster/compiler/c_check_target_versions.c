// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's function multiversioning by target strings (#1838): target_clones, and several target attributes of one
// function, as clang 23's Sema checks them on x86-64 Linux (SemaX86::checkTargetClonesAttr, at llvmorg-23.1.4, read for
// its rules and written again in C). Include from c_check_attribute.c, after the multiversioning attribute checks (#839).
//
// - target_clones' strings (check_input_target_clones): each is a comma-separated list of versions, each trimmed: an
//   empty string or one that ends in a comma, an arch= of no x86-64 CPU, or a feature a version can't name (one whose
//   multiversioning priority is 0, c_check_target_versions.inc) is -Wignored-attributes' "unsupported ... in the
//   'target_clones' attribute string" at the version, and drops the attribute; a version already listed is
//   -Wfunction-multiversion's "version list contains duplicate entries" at it; a string of several versions among
//   several strings is -Wtarget-clones-mixed-specifiers' warning at the first; and a list without "default" is clang's
//   error at the attribute's name. clang places a version at its string literal's first character (the quote) plus its
//   offset in the trimmed string, as getLocWithOffset counts it.
//
// These attributes are rare (a unit without them never calls this file's functions), and each check reads one
// attribute's few strings: scalar, as a vector pass over tens of bytes would cost more than it saves.
#ifndef BUSTER_C_CHECK_TARGET_VERSIONS_INCLUDED
#define BUSTER_C_CHECK_TARGET_VERSIONS_INCLUDED

// A feature a version may name (c_check_target_versions.inc): its name, its multiversioning priority and its bit of
// libgcc's CPU model (`word` 0 at __cpu_model + 12, 1 to 3 __cpu_features2[0] to [2]).
typedef struct CheckTargetVersionFeature CheckTargetVersionFeature;
struct CheckTargetVersionFeature
{
    const char* name;
    uint32_t priority;
    uint32_t word;
    uint32_t bit;
};

static const CheckTargetVersionFeature check_target_version_features[] =
{
#define BUSTER_CHECK_TARGET_VERSION_FEATURE(NAME, PRIORITY, WORD, BIT) { NAME, PRIORITY, WORD, BIT },
#define BUSTER_CHECK_TARGET_VERSION_LEVEL(NAME, WORD, BIT)
#define BUSTER_CHECK_TARGET_VERSION_ARCH(NAME, PRIORITY)
#include "c_check_target_versions.inc"
#undef BUSTER_CHECK_TARGET_VERSION_FEATURE
#undef BUSTER_CHECK_TARGET_VERSION_LEVEL
#undef BUSTER_CHECK_TARGET_VERSION_ARCH
};

// The row of the feature `name` names, or -1.
static int32_t check_target_version_feature(String8 name)
{
    int32_t result = -1;
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(check_target_version_features) && result < 0; r += 1)
    {
        const char* row = check_target_version_features[r].name;
        result = strlen(row) == name.length && memcmp(row, name.pointer, name.length) == 0 ? (int32_t)r : -1;
    }
    return result;
}

// Whether `c` is a character llvm::StringRef::trim removes.
static bool check_target_version_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// The bytes [first, last) of `text` without the characters trim removes at either end.
static void check_target_version_trim(String8 text, uint32_t* first, uint32_t* last)
{
    while (*first < *last && check_target_version_space(text.pointer[*first]))
    {
        *first += 1;
    }
    while (*last > *first && check_target_version_space(text.pointer[*last - 1]))
    {
        *last -= 1;
    }
}

// A warning of `kind` (and `id`) at the string literal `token`: the version's first byte in the string's decoded text
// (`start`), its place's offset from the trimmed text's start, and whether an unsupported one is a CPU's
// (CHECK_WARNING_EVENT_TARGET_CLONES_*'s payload).
static void check_input_target_clones_warning(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t id, uint32_t kind, uint32_t token,
                                              uint32_t start, uint32_t offset, bool cpu)
{
    if (offset < BUSTER_CHECK_TARGET_CLONES_CPU)
    {
        const CheckWarningEvent event =
        {
            .id = id,
            .kind = kind,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = attribute->argument_first,
            .payload = { .name = start, .token = token, .data = offset | (cpu ? BUSTER_CHECK_TARGET_CLONES_CPU : 0) },
        };
        check_input_warning_event(reader, &event, NULL, 0);
    }
}

// clang's SemaX86::checkTargetClonesAttr for `attribute`, a target_clones attribute that its subject took: its strings'
// warnings, in clang's order, and its error where it has no default. An unsupported version's warning, or the error,
// drops the attribute. Whether no error was reported (a warning leaves the declaration valid).
static bool check_input_target_clones(CheckInputReader* reader, CheckInputAttributes* attributes, CheckInputAttribute* attribute)
{
    const uint64_t mark = reader->scratch->length;
    // The versions seen, as bytes of the strings' decoded texts (Params in clang), in scratch, which grows them by doubling.
    uint32_t capacity = 64;
    String8* seen = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, String8, capacity);
    reader->output->resource |= seen == NULL;
    uint32_t seen_count = 0;
    bool unsupported = seen == NULL;
    bool decoded = true;
    bool has_default = false;
    bool has_comma = false;
    for (uint32_t a = 0; !unsupported && decoded && a < attribute->argument_count; a += 1)
    {
        uint32_t first = 0;
        uint32_t end = 0;
        String8 text = { 0 };
        decoded = check_input_attribute_argument(attribute, a, &first, &end) && check_input_attribute_string(reader, first, end, false, &text);
        uint32_t param_first = 0;
        uint32_t param_last = decoded ? (uint32_t)text.length : 0;
        check_target_version_trim(text, &param_first, &param_last);
        if (decoded && (param_first == param_last || text.pointer[param_last - 1] == ','))
        {
            // An empty version: where the trimmed text ends, after its comma (or its start where it is empty).
            check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED, first,
                                              param_last, 0, false);
            unsupported = true;
        }
        for (uint32_t cursor = param_first; decoded && !unsupported && cursor < param_last;)
        {
            uint32_t item_first = cursor;
            uint32_t item_last = cursor;
            while (item_last < param_last && text.pointer[item_last] != ',')
            {
                item_last += 1;
            }
            has_comma |= item_last < param_last;
            cursor = item_last < param_last ? item_last + 1 : param_last;
            check_target_version_trim(text, &item_first, &item_last);
            const String8 item = { text.pointer + item_first, item_last - item_first };
            const uint32_t offset = item_first - param_first;
            const bool arch = item.length >= 5 && memcmp(item.pointer, "arch=", 5) == 0;
            const bool is_default = item.length == 7 && memcmp(item.pointer, "default", 7) == 0;
            if (arch && predefines_cpu_index_of(item.pointer + 5, (uint32_t)item.length - 5) < 0)
            {
                check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED, first,
                                                  item_first + 5, offset, true);
                unsupported = true;
            }
            else if (!arch && !is_default && check_target_version_feature(item) < 0)
            {
                check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED, first,
                                                  item_first, offset, false);
                unsupported = true;
            }
            has_default |= is_default;
            bool repeated = false;
            for (uint32_t s = 0; !unsupported && !repeated && s < seen_count; s += 1)
            {
                repeated = seen[s].length == item.length && (item.length == 0 || memcmp(seen[s].pointer, item.pointer, item.length) == 0);
            }
            if (repeated)
            {
                check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_FUNCTION_MULTIVERSION, CHECK_WARNING_EVENT_TARGET_CLONES_DUPLICATE, first,
                                                  item_first, offset, false);
            }
            if (!unsupported && seen_count == capacity)
            {
                String8* grown = capacity <= UINT32_MAX / 2 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, String8, (uint64_t)capacity * 2) : NULL;
                reader->output->resource |= grown == NULL;
                unsupported = grown == NULL;
                if (grown != NULL)
                {
                    memcpy(grown, seen, (uint64_t)seen_count * sizeof(String8));
                    seen = grown;
                    capacity *= 2;
                }
            }
            if (!unsupported)
            {
                seen[seen_count++] = item;
            }
        }
    }
    bool valid = true;
    if (decoded && !unsupported && has_comma && attribute->argument_count > 1)
    {
        uint32_t first = 0;
        uint32_t end = 0;
        (void)check_input_attribute_argument(attribute, 0, &first, &end);
        check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_TARGET_CLONES_MIXED_SPECIFIERS, CHECK_WARNING_EVENT_TARGET_CLONES_MIXED, first, 0, 0,
                                          false);
    }
    if (decoded && !unsupported && !has_default)
    {
        const uint32_t before = reader->output->report_count;
        check_input_attribute_argument_error(reader, attributes, attribute, attribute->first, CHECK_ARGUMENT_TARGET_CLONES_DEFAULT, 0);
        if (reader->output->report_count > before)
        {
            // After the strings' warnings (completed at the first string), as clang's handler reports them: the subject
            // checks' end orders each by its completion (check_input_attribute_subjects).
            reader->output->reports[before].completion = attribute->argument_first + 1;
        }
        valid = false;
    }
    if (unsupported && seen != NULL)
    {
        attribute->ignored = true;
        attribute->subject_ignored = true;
    }
    // The versions, joined by commas, are the attribute's text, which code generation reads (#1838,
    // c_check_program_target_versions.c).
    uint64_t joined = 0;
    for (uint32_t s = 0; decoded && !unsupported && s < seen_count; s += 1)
    {
        joined += seen[s].length + 1;
    }
    char* text = decoded && !unsupported && joined != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, char, joined) : NULL;
    reader->output->resource |= decoded && !unsupported && joined != 0 && text == NULL;
    uint64_t length = 0;
    for (uint32_t s = 0; text != NULL && s < seen_count; s += 1)
    {
        memcpy(text + length, seen[s].pointer, seen[s].length);
        length += seen[s].length;
        text[length++] = ',';
    }
    attribute->text = text != NULL ? (String8){ text, length - 1 } : (String8){ 0 };
    arena_pop_to(reader->scratch, mark);
    return valid;
}

static uint32_t check_input_callee_end(const Parser* parser, uint32_t token, uint32_t* first);
static bool check_input_warning_unevaluated(CheckInputReader* reader, uint32_t token);

// The rest is multiversioning's rules for a function's declarations (clang's CheckMultiVersionFunction and its helpers in
// SemaDecl.cpp, for target and target_clones), each declaration against the visible one before it (O), and binding's
// answer to it:
//
// - A function becomes multiversioned at its first declaration of target("default") or target_clones, or where a target
//   declaration follows one of another target string (CheckDeclarationCausesMultiVersioning). Its versions are entities
//   of their own, which share the name: binding keeps each apart (check_input_target_versions_match), and a declaration
//   of a version's string (or of the same target_clones list) redeclares that version.
// - A declaration it refuses is invalid (CheckInputReader.cpu_invalid, shared with cpu_specific's): the later
//   declarations' rules skip it, as clang's lookup does.
// - Whether O's function was used before (clang's isUsed) is read from the binding events: an evaluated use of its name
//   that reached one of its declarations. A unit with a multiversioned function is read in source order for it
//   (CheckInput.target_multiversion, check_deferred_bodies), so that its bodies' uses are there when a later
//   declaration is read.
// - A call reached by no default version (target("default") or target_clones) of a multiversioned function visible
//   there is "no matching function for call to 'f'" (check_input_target_versions_calls), as clang's overload set has
//   no viable candidate.

// A declaration's multiversioning by target strings: none, target or target_clones.
typedef enum CheckTargetVersionKind
{
    CHECK_TARGET_VERSION_NONE,
    CHECK_TARGET_VERSION_TARGET,
    CHECK_TARGET_VERSION_CLONES,
} CheckTargetVersionKind;

// `attributes`' first attribute of `kind` that isn't ignored, or NULL.
static const CheckInputAttribute* check_target_versions_own(const CheckInputAttributes* attributes, CheckInputAttributeKind kind)
{
    const CheckInputAttribute* result = NULL;
    for (uint32_t i = 0; attributes != NULL && result == NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        result = !attribute->ignored && attribute->kind == kind ? attribute : NULL;
    }
    return result;
}

// The attribute of `kind` declaration `declaration` has, its own or one an earlier declaration of its function gave it
// (clang's inherited attribute), searched along its name's chain from binding `handle` (the most recent first), or NULL.
static const CheckInputAttribute* check_target_versions_attribute(const CheckInputReader* reader, uint32_t handle, uint32_t declaration, CheckInputAttributeKind kind)
{
    const Parser* parser = reader->parser;
    const CheckInputAttribute* result = check_target_versions_own(check_input_attributes_at(reader->output, declaration), kind);
    for (uint32_t h = handle; result == NULL && h != 0; h = reader->bindings.previous[h - 1])
    {
        const uint32_t earlier = reader->bindings.declaration[h - 1];
        const bool same = earlier < declaration && earlier < parser->tokens.count && reader->declaration_kinds[earlier] == C_BINDING_FUNCTION &&
                          reader->objects[earlier] == reader->objects[declaration];
        result = same ? check_target_versions_own(check_input_attributes_at(reader->output, earlier), kind) : NULL;
    }
    return result;
}

static bool check_target_versions_default(const CheckInputAttribute* target)
{
    return target != NULL && check_input_attribute_equal(target->text, "default");
}

// The declaration that made the function of `object` multiversioned, or BUSTER_CHECK_NONE where it isn't.
static uint32_t check_target_versions_since(const CheckInputReader* reader, uint32_t object)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t i = 0; result == BUSTER_CHECK_NONE && i < reader->target_mv_count; i += 1)
    {
        result = reader->objects[reader->target_mv_tokens[i]] == object ? reader->target_mv_since[i] : BUSTER_CHECK_NONE;
    }
    return result;
}

// Appends `value` to a growable list of the reader's arena (its `count` and `capacity`), and `second` to its twin where
// that isn't NULL.
static void check_target_versions_append(CheckInputReader* reader, uint32_t** list, uint32_t** twin, uint32_t* count, uint32_t* capacity, uint32_t value, uint32_t second)
{
    if (*count == *capacity)
    {
        const uint32_t grown_capacity = *capacity != 0 ? *capacity * 2 : 8;
        uint32_t* grown = BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, grown_capacity);
        uint32_t* grown_twin = grown != NULL && twin != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, uint32_t, grown_capacity) : NULL;
        const bool valid = grown != NULL && (twin == NULL || grown_twin != NULL);
        reader->output->resource |= !valid;
        if (valid && *count != 0)
        {
            memcpy(grown, *list, (uint64_t)*count * sizeof(uint32_t));
            if (twin != NULL)
            {
                memcpy(grown_twin, *twin, (uint64_t)*count * sizeof(uint32_t));
            }
        }
        if (valid)
        {
            *list = grown;
            *twin = twin != NULL ? grown_twin : NULL;
            *capacity = grown_capacity;
        }
    }
    if (*count < *capacity)
    {
        (*list)[*count] = value;
        if (twin != NULL)
        {
            (*twin)[*count] = second;
        }
        *count += 1;
    }
}

// Records that the function of declaration `declaration` is multiversioned from declaration `since` on.
static void check_target_versions_mark(CheckInputReader* reader, uint32_t declaration, uint32_t since)
{
    uint32_t* twin = reader->target_mv_since;
    check_target_versions_append(reader, &reader->target_mv_tokens, &twin, &reader->target_mv_count, &reader->target_mv_capacity, declaration, since);
    reader->target_mv_since = twin;
    reader->output->target_multiversion = true;
}

// Records binding's answer for declaration `token`: a version apart (`redeclared` BUSTER_CHECK_NONE) or the redeclaration
// of the earlier declaration `redeclared`.
static void check_target_versions_bind(CheckInputReader* reader, uint32_t token, uint32_t redeclared)
{
    uint32_t* twin = reader->target_bind_redeclares;
    check_target_versions_append(reader, &reader->target_bind_tokens, &twin, &reader->target_bind_count, &reader->target_bind_capacity, token, redeclared);
    reader->target_bind_redeclares = twin;
}

// A target string's parse, as clang's TargetInfo::parseTargetAttr gives it: its items between commas, trimmed, fpmath=
// left out; arch= and tune= (a second of either is its duplicate), branch-protection=, and the features (no-X as -X).
// Each piece is bytes of the string's text; the features' arrays are in scratch, one entry for each item of the string.

typedef struct CheckTargetVersionParse CheckTargetVersionParse;
struct CheckTargetVersionParse
{
    String8 cpu;
    String8 tune;
    String8 branch;
    // 0 for none, 1 a duplicate arch=, 2 a duplicate tune=.
    uint32_t duplicate;
    // Each feature's item, with whether it is negated (no-), in the string's order, then sorted.
    String8* features;
    bool* negated;
    uint32_t feature_count;
    // Whether scratch had room for the arrays (the output's resource flag set where it hadn't).
    bool parsed;
};

static String8 check_target_version_slice(String8 text, uint32_t first, uint32_t last)
{
    check_target_version_trim(text, &first, &last);
    return (String8){ text.pointer + first, last - first };
}

static bool check_target_version_prefix(String8 item, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return item.length >= length && memcmp(item.pointer, prefix, length) == 0;
}

// The caller pops scratch back past the arrays.
static void check_target_versions_parse(CheckInputReader* reader, String8 text, CheckTargetVersionParse* parse)
{
    *parse = (CheckTargetVersionParse){ 0 };
    const bool is_default = check_input_attribute_equal(text, "default");
    uint64_t items = 1;
    for (uint64_t i = 0; i < text.length; i += 1)
    {
        items += text.pointer[i] == ',';
    }
    parse->features = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, String8, items);
    parse->negated = parse->features != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, bool, items) : NULL;
    parse->parsed = parse->negated != NULL;
    reader->output->resource |= !parse->parsed;
    for (uint32_t cursor = 0; parse->parsed && !is_default && cursor <= text.length;)
    {
        uint32_t end = cursor;
        while (end < text.length && text.pointer[end] != ',')
        {
            end += 1;
        }
        const String8 item = check_target_version_slice(text, cursor, end);
        cursor = end + 1;
        // The value after an item's first '=', trimmed.
        uint32_t equals = 0;
        while (equals < item.length && item.pointer[equals] != '=')
        {
            equals += 1;
        }
        const String8 value = equals < item.length ? check_target_version_slice(item, equals + 1, (uint32_t)item.length) : (String8){ 0 };
        if (check_target_version_prefix(item, "fpmath="))
        {
        }
        else if (check_target_version_prefix(item, "branch-protection="))
        {
            parse->branch = value;
        }
        else if (check_target_version_prefix(item, "arch=") || check_target_version_prefix(item, "tune="))
        {
            String8* slot = item.pointer[0] == 'a' ? &parse->cpu : &parse->tune;
            parse->duplicate = slot->pointer != NULL ? (item.pointer[0] == 'a' ? 1u : 2u) : parse->duplicate;
            *slot = slot->pointer != NULL ? *slot : value;
        }
        else
        {
            const bool negated = check_target_version_prefix(item, "no-");
            // clang's "-" + the item after its first '-'.
            uint32_t dash = 0;
            while (negated && dash < item.length && item.pointer[dash] != '-')
            {
                dash += 1;
            }
            parse->features[parse->feature_count] = negated ? (String8){ item.pointer + dash + 1, item.length - dash - 1 } : item;
            parse->negated[parse->feature_count] = negated;
            parse->feature_count += 1;
        }
    }
}

static int check_target_versions_compare(String8 a, bool a_negated, String8 b, bool b_negated)
{
    // "+X" sorts after "-X" ('+' 0x2b is below '-' 0x2d: clang's llvm::sort of the strings with their signs).
    const char a_sign = a_negated ? '-' : '+';
    const char b_sign = b_negated ? '-' : '+';
    int result = a_sign < b_sign ? -1 : a_sign > b_sign ? 1 : 0;
    const uint64_t shorter = a.length < b.length ? a.length : b.length;
    const int bytes = result == 0 && shorter != 0 ? memcmp(a.pointer, b.pointer, shorter) : 0;
    result = result != 0 ? result : bytes != 0 ? bytes : a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
    return result;
}

static bool check_target_versions_text_equal(String8 a, String8 b)
{
    return a.length == b.length && (a.length == 0 || memcmp(a.pointer, b.pointer, a.length) == 0);
}

// Whether two target strings parse the same (clang's ParsedTargetAttr ==, each one's features sorted).
static bool check_target_versions_same(CheckInputReader* reader, String8 a, String8 b)
{
    const uint64_t mark = reader->scratch->length;
    CheckTargetVersionParse left;
    CheckTargetVersionParse right;
    check_target_versions_parse(reader, a, &left);
    check_target_versions_parse(reader, b, &right);
    CheckTargetVersionParse* parses[2] = { &left, &right };
    for (uint32_t p = 0; p < 2; p += 1)
    {
        CheckTargetVersionParse* parse = parses[p];
        for (uint32_t i = 1; i < parse->feature_count; i += 1)
        {
            const String8 feature = parse->features[i];
            const bool negated = parse->negated[i];
            uint32_t at = i;
            while (at > 0 && check_target_versions_compare(parse->features[at - 1], parse->negated[at - 1], feature, negated) > 0)
            {
                parse->features[at] = parse->features[at - 1];
                parse->negated[at] = parse->negated[at - 1];
                at -= 1;
            }
            parse->features[at] = feature;
            parse->negated[at] = negated;
        }
    }
    bool same = left.parsed && right.parsed && left.duplicate == right.duplicate && left.feature_count == right.feature_count &&
                check_target_versions_text_equal(left.cpu, right.cpu) && check_target_versions_text_equal(left.tune, right.tune) &&
                check_target_versions_text_equal(left.branch, right.branch);
    for (uint32_t i = 0; same && i < left.feature_count; i += 1)
    {
        same = check_target_versions_compare(left.features[i], left.negated[i], right.features[i], right.negated[i]) == 0;
    }
    arena_pop_to(reader->scratch, mark);
    return same;
}

// Whether `name` is one clang's validateCpuIs takes: a vendor, a CPU type or a subtype of libgcc's CPU model.
static bool check_target_versions_cpu_is(String8 name)
{
    static const char* const names[] =
    {
#define BUSTER_CBUILTIN_CPU_FEATURE(NAME, WORD, BIT)
#define BUSTER_CBUILTIN_CPU_NAME(NAME, FIELD, VALUE) NAME,
#include "c_builtin_cpu.inc"
#undef BUSTER_CBUILTIN_CPU_FEATURE
#undef BUSTER_CBUILTIN_CPU_NAME
    };
    bool found = false;
    for (uint32_t i = 0; !found && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        found = strlen(names[i]) == name.length && memcmp(names[i], name.pointer, name.length) == 0;
    }
    return found;
}

// clang's CheckMultiVersionValue for the target attribute `target`: an arch= that validateCpuIs doesn't take, a negated
// feature, or a feature a version can't name, its error at `name` (with the caused-here note at `caused` where it isn't
// BUSTER_CHECK_NONE). Whether it was refused.
static bool check_target_versions_value(CheckInputReader* reader, const CheckInputAttribute* target, uint32_t name, uint32_t caused)
{
    const uint64_t mark = reader->scratch->length;
    CheckTargetVersionParse parse;
    check_target_versions_parse(reader, target->text, &parse);
    CheckCpuDispatchError error = CHECK_CPU_DISPATCH_INVALID_OPTION;
    String8 item = { 0 };
    if (parse.cpu.length != 0 && !check_target_versions_cpu_is(parse.cpu))
    {
        error = CHECK_CPU_DISPATCH_TARGET_ARCHITECTURE;
        item = parse.cpu;
    }
    for (uint32_t i = 0; error == CHECK_CPU_DISPATCH_INVALID_OPTION && i < parse.feature_count; i += 1)
    {
        const bool refused = parse.negated[i] || check_target_version_feature(parse.features[i]) < 0;
        error = refused ? CHECK_CPU_DISPATCH_TARGET_FEATURE : error;
        // A negated one is named as written: "no-" and the rest.
        item = refused ? (parse.negated[i] ? (String8){ parse.features[i].pointer - 3, parse.features[i].length + 3 } : parse.features[i]) : item;
    }
    if (error != CHECK_CPU_DISPATCH_INVALID_OPTION)
    {
        const uint32_t before = reader->output->multiversion_report_count;
        check_input_cpu_dispatch_report(reader, name, error, caused, caused != BUSTER_CHECK_NONE ? CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED : CHECK_INPUT_NOTE_NONE,
                                        target->argument_first);
        if (reader->output->multiversion_report_count != before)
        {
            const uint64_t start = (uint64_t)(item.pointer - target->text.pointer);
            reader->output->multiversion_reports[before].value = start | (uint64_t)item.length << 32;
        }
    }
    arena_pop_to(reader->scratch, mark);
    return error != CHECK_CPU_DISPATCH_INVALID_OPTION;
}

// Whether `token` is in the body of the definition of the function of declaration `declaration` (where clang's use doesn't
// mark the function used).
static bool check_target_versions_inside(const CheckInputReader* reader, uint32_t declaration, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint32_t definition = reader->definition_origins[declaration] != 0 ? reader->definition_origins[declaration] - 1 : BUSTER_CHECK_NONE;
    uint32_t brace = definition;
    while (brace < token && parser->tree.roles[brace] != PARSE_ROLE_OPEN_BODY)
    {
        brace += 1;
    }
    return brace < token && token < parser->tree.parents[brace];
}

// Whether the function of declaration `declaration` had an evaluated use before the binding events' end: a use event
// of `name` that reached a declaration of its function (clang's isUsed).
static bool check_target_versions_used(CheckInputReader* reader, uint32_t name, uint32_t declaration)
{
    const CBindings* bindings = &reader->bindings;
    const uint32_t key = (uint32_t)C_BINDING_ORDINARY * bindings->name_capacity + name;
    const uint32_t object = reader->objects[declaration];
    bool used = false;
    for (uint32_t e = bindings->count; !used && e > bindings->base; e -= 1)
    {
        const uint32_t event = e - 1;
        const uint32_t reached = bindings->operation[event] == C_BINDING_USE && bindings->key[event] == key && bindings->previous[event] != 0 ?
                                 bindings->declaration[bindings->previous[event] - 1] : BUSTER_CHECK_NONE;
        used = reached < reader->parser->tokens.count && reader->objects[reached] == object && !check_input_warning_unevaluated(reader, bindings->declaration[event]) &&
               !check_target_versions_inside(reader, reached, bindings->declaration[event]);
    }
    return used;
}

// The rules against the declaration before (clang's CheckMultiVersionAdditionalRules after its attribute and use checks,
// areMultiversionVariantFunctionsCompatible): `previous`'s prototype (with the note at `name` where `causes`), the
// new one's, then the return type, inline specification and linkage. Whether one refused it.
static bool check_target_versions_compatible(CheckInputReader* reader, uint32_t previous, uint32_t name, TypeId type, bool is_inline, bool is_static)
{
    const TypeTable* types = reader->types;
    bool refused = true;
    if (previous == BUSTER_CHECK_NONE)
    {
        refused = (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0;
        if (refused)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_PROTOTYPE, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
        }
    }
    else if ((types->types.flags[type_index(reader->declared_types[previous])] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
    {
        check_input_cpu_dispatch_report(reader, previous, CHECK_CPU_DISPATCH_PROTOTYPE, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, BUSTER_CHECK_NONE);
    }
    else if ((types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_PROTOTYPE) == 0)
    {
        check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_PROTOTYPE, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
    }
    else
    {
        const uint8_t previous_flags = reader->output->function_flags != NULL ? reader->output->function_flags[previous] : 0;
        const bool previous_static = check_input_linkage(reader->declaration_bits, previous) == 1;
        const TypeAnswer same = type_same_canonical(types, type_return(types, reader->declared_types[previous]), type_return(types, type));
        reader->output->resource |= same == TYPE_ANSWER_FULL;
        const CheckCpuDispatchError different = same != TYPE_ANSWER_YES                                         ? CHECK_CPU_DISPATCH_RETURN_TYPE :
                                                ((previous_flags & CHECK_INPUT_FUNCTION_INLINE) != 0) != is_inline ? CHECK_CPU_DISPATCH_INLINE :
                                                previous_static != is_static                                     ? CHECK_CPU_DISPATCH_LINKAGE :
                                                                                                                   CHECK_CPU_DISPATCH_INVALID_OPTION;
        refused = different != CHECK_CPU_DISPATCH_INVALID_OPTION;
        if (refused)
        {
            check_input_cpu_dispatch_report(reader, name, different, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
        }
    }
    return refused;
}

// A walk over a target_clones attribute's versions: each string argument's items between commas, trimmed, in order.
typedef struct CheckTargetClonesWalk CheckTargetClonesWalk;
struct CheckTargetClonesWalk
{
    const CheckInputAttribute* attribute;
    String8 text;
    uint32_t argument;
    uint32_t cursor;
    bool valid;
};

// The walk's next version into `item`; false at its end or on an argument that is not a string.
static bool check_target_clones_next(CheckInputReader* reader, CheckTargetClonesWalk* walk, String8* item)
{
    while (walk->valid && walk->cursor >= walk->text.length && walk->argument < walk->attribute->argument_count)
    {
        uint32_t first = 0;
        uint32_t end = 0;
        walk->text = (String8){ 0 };
        walk->valid = check_input_attribute_argument(walk->attribute, walk->argument, &first, &end) && check_input_attribute_string(reader, first, end, false, &walk->text);
        walk->argument += 1;
        walk->cursor = 0;
    }
    const bool found = walk->valid && walk->cursor < walk->text.length;
    if (found)
    {
        uint32_t item_end = walk->cursor;
        while (item_end < walk->text.length && walk->text.pointer[item_end] != ',')
        {
            item_end += 1;
        }
        *item = check_target_version_slice(walk->text, walk->cursor, item_end);
        walk->cursor = item_end + 1;
    }
    return found;
}

// Whether the target_clones lists of `a` and `b` are the same strings (clang's std::equal of their versions, each string
// of several versions its versions, as checkTargetClonesAttr split them); any number of versions, walked side by side.
static bool check_target_versions_same_clones(CheckInputReader* reader, const CheckInputAttribute* a, const CheckInputAttribute* b)
{
    CheckTargetClonesWalk walks[2] = {
        { .attribute = a, .valid = true },
        { .attribute = b, .valid = true },
    };
    bool same = true;
    bool more = true;
    while (same && more)
    {
        String8 items[2] = { 0 };
        const bool has_a = check_target_clones_next(reader, &walks[0], &items[0]);
        const bool has_b = check_target_clones_next(reader, &walks[1], &items[1]);
        same = has_a == has_b && (!has_a || check_target_versions_text_equal(items[0], items[1]));
        more = has_a;
    }
    return same && walks[0].valid && walks[1].valid;
}

// clang's CheckMultiVersionFunction for a function's declaration at `name`, of `type` and `attributes`, against the
// visible declaration of a function before it (`handle` its name's binding), where either is multiversioned by target
// strings; a cpu_specific or cpu_dispatch one is c_check_cpu_dispatch.c's. Records binding's answer
// (check_target_versions_bind) and the functions it makes multiversioned.
static void check_target_versions_rules(CheckInputReader* reader, const CheckInputAttribute* target, const CheckInputAttribute* clones, CheckTargetVersionKind kind,
                                        const CheckInputAttributes* attributes, uint32_t name, TypeId type, uint32_t handle, bool is_inline, bool is_static, bool main)
{
    const Parser* parser = reader->parser;
    const uint32_t previous = check_cpu_dispatch_visible(reader, handle);
    const bool function_previous = previous < parser->tokens.count && reader->declaration_kinds[previous] == C_BINDING_FUNCTION;
    const bool cpu = check_cpu_dispatch_kind(attributes) != CHECK_CPU_DISPATCH_NONE ||
                     (function_previous && check_cpu_dispatch_kind(check_input_attributes_at(reader->output, previous)) != CHECK_CPU_DISPATCH_NONE);
    // The previous declaration's attributes, its own or inherited, and whether its function is multiversioned.
    const CheckInputAttribute* previous_target = function_previous ? check_target_versions_attribute(reader, handle, previous, CHECK_ATTRIBUTE_TARGET) : NULL;
    const CheckInputAttribute* previous_clones = function_previous ? check_target_versions_attribute(reader, handle, previous, CHECK_ATTRIBUTE_TARGET_CLONES) : NULL;
    const CheckTargetVersionKind previous_kind = previous_clones != NULL ? CHECK_TARGET_VERSION_CLONES :
                                                 previous_target != NULL ? CHECK_TARGET_VERSION_TARGET : CHECK_TARGET_VERSION_NONE;
    const bool multiversioned = function_previous && check_target_versions_since(reader, reader->objects[previous]) != BUSTER_CHECK_NONE;
    const uint32_t reports = reader->output->multiversion_report_count;
    bool valid = true;
    // Binding's answer: BUSTER_CHECK_NONE for the visible declaration's (no record), `name` for a version apart, or the
    // earlier declaration redeclared.
    uint32_t bind = BUSTER_CHECK_NONE;
    if (cpu || (kind == CHECK_TARGET_VERSION_NONE && !multiversioned))
    {
    }
    else if (main)
    {
        // main may have a target attribute of a string other than "default" (an optimization hint).
        valid = kind == CHECK_TARGET_VERSION_TARGET && !check_target_versions_default(target);
        if (!valid)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_MAIN, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
        }
    }
    else if (!function_previous)
    {
        // The function's first declaration (CheckMultiVersionFirstFunction): a target other than "default" alone isn't
        // multiversioning.
        if (kind == CHECK_TARGET_VERSION_CLONES || check_target_versions_default(target))
        {
            valid = !check_target_versions_compatible(reader, BUSTER_CHECK_NONE, name, type, is_inline, is_static);
            if (valid)
            {
                check_target_versions_mark(reader, name, name);
            }
        }
    }
    else if (multiversioned && kind == CHECK_TARGET_VERSION_NONE)
    {
        // A redeclaration of a target_clones function may leave its attribute out; one of target's may not.
        if (previous_kind != CHECK_TARGET_VERSION_CLONES)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_TARGET_REQUIRED, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
            valid = false;
        }
    }
    else if (!multiversioned && kind == CHECK_TARGET_VERSION_TARGET)
    {
        // CheckDeclarationCausesMultiVersioning: another string than the previous one's, or "default", makes the function
        // multiversioned.
        const bool causes = check_target_versions_default(target) || (previous_target != NULL && !check_target_versions_text_equal(previous_target->text, target->text));
        if (!causes)
        {
        }
        else if (check_target_versions_used(reader, parser->tokens.names[name], previous))
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_AFTER_USE, previous, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (check_target_versions_compatible(reader, previous, name, type, is_inline, is_static) ||
                 check_target_versions_value(reader, target, name, BUSTER_CHECK_NONE))
        {
            valid = false;
        }
        else if (check_target_versions_default(target) && previous_target == NULL)
        {
            // The default after a plain declaration redeclares it.
            check_target_versions_mark(reader, previous, name);
        }
        else if (previous_target != NULL && check_target_versions_value(reader, previous_target, previous, name))
        {
            valid = false;
        }
        else if (previous_target != NULL && check_target_versions_same(reader, previous_target->text, target->text))
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_TARGET_DUPLICATE, previous, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
            valid = false;
        }
        else
        {
            // A declaration of the function without its own target attribute after one with it (most recent first).
            uint32_t missing = BUSTER_CHECK_NONE;
            for (uint32_t h = handle; missing == BUSTER_CHECK_NONE && h != 0; h = reader->bindings.previous[h - 1])
            {
                const uint32_t declaration = reader->bindings.declaration[h - 1];
                const bool same = declaration < parser->tokens.count && reader->declaration_kinds[declaration] == C_BINDING_FUNCTION &&
                                  reader->objects[declaration] == reader->objects[previous];
                const bool own = same && check_target_versions_own(check_input_attributes_at(reader->output, declaration), CHECK_ATTRIBUTE_TARGET) != NULL;
                const uint32_t earlier = h - 1 < reader->bindings.count ? reader->bindings.previous[h - 1] : 0;
                missing = same && !own && earlier != 0 &&
                          check_target_versions_attribute(reader, earlier, declaration, CHECK_ATTRIBUTE_TARGET) != NULL ? declaration : BUSTER_CHECK_NONE;
            }
            if (missing != BUSTER_CHECK_NONE)
            {
                check_input_cpu_dispatch_report(reader, missing, CHECK_CPU_DISPATCH_TARGET_REQUIRED, name, CHECK_INPUT_NOTE_MULTIVERSIONING_CAUSED, BUSTER_CHECK_NONE);
                valid = false;
            }
            else
            {
                check_target_versions_mark(reader, previous, name);
                check_target_versions_mark(reader, name, name);
                bind = name;
            }
        }
    }
    else
    {
        // CheckMultiVersionAdditionalDecl, after a target_clones declaration made the function multiversioned (where a use
        // before refuses it).
        const bool clones_causes = !multiversioned && kind == CHECK_TARGET_VERSION_CLONES;
        if (clones_causes && check_target_versions_used(reader, parser->tokens.names[name], previous))
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_AFTER_USE, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, BUSTER_CHECK_NONE);
            valid = false;
        }
        else if (clones_causes)
        {
            // clang makes the earlier function multiversioned before the rules below.
            check_target_versions_mark(reader, previous, name);
        }
        if (valid && kind != CHECK_TARGET_VERSION_NONE && previous_kind != CHECK_TARGET_VERSION_NONE && kind != previous_kind)
        {
            check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_MIXED, previous, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
            valid = false;
        }
        // The earlier versions, most recent first: a redeclaration of one, or a duplicate of it. Any number of them, in
        // scratch, room for each binding of the name's chain.
        const uint64_t mark = reader->scratch->length;
        uint32_t chain = 0;
        for (uint32_t h = handle; valid && kind != CHECK_TARGET_VERSION_NONE && h != 0; h = reader->bindings.previous[h - 1])
        {
            chain += 1;
        }
        uint32_t* visited = chain != 0 ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, chain) : NULL;
        reader->output->resource |= chain != 0 && visited == NULL;
        const uint32_t count = visited != NULL ? check_cpu_dispatch_previous(reader, handle, visited, chain) : 0;
        bool redeclaration = false;
        for (uint32_t v = 0; valid && !redeclaration && v < count; v += 1)
        {
            const uint32_t current = visited[v];
            if (kind == CHECK_TARGET_VERSION_TARGET)
            {
                const CheckInputAttribute* current_target = check_target_versions_attribute(reader, handle, current, CHECK_ATTRIBUTE_TARGET);
                redeclaration = current_target != NULL && check_target_versions_text_equal(current_target->text, target->text);
                if (!redeclaration && current_target != NULL && check_target_versions_same(reader, current_target->text, target->text))
                {
                    check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_TARGET_DUPLICATE, current, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
                    valid = false;
                }
            }
            else
            {
                const CheckInputAttribute* current_clones = check_target_versions_attribute(reader, handle, current, CHECK_ATTRIBUTE_TARGET_CLONES);
                if (current_clones != NULL && !check_target_versions_same_clones(reader, current_clones, clones))
                {
                    check_input_cpu_dispatch_report(reader, name, CHECK_CPU_DISPATCH_CLONES_MISMATCH, current, CHECK_INPUT_NOTE_PREVIOUS_DECLARATION, BUSTER_CHECK_NONE);
                    valid = false;
                }
                redeclaration = valid;
            }
            bind = redeclaration ? current : bind;
        }
        arena_pop_to(reader->scratch, mark);
        if (!valid || redeclaration || kind == CHECK_TARGET_VERSION_NONE)
        {
        }
        else if (kind == CHECK_TARGET_VERSION_TARGET && check_target_versions_value(reader, target, name, BUSTER_CHECK_NONE))
        {
            valid = false;
        }
        else if (check_target_versions_compatible(reader, previous, name, type, is_inline, is_static))
        {
            valid = false;
        }
        else
        {
            // A new version of its own.
            check_target_versions_mark(reader, name, name);
            bind = name;
        }
    }
    if (!valid || reader->output->multiversion_report_count != reports)
    {
        check_cpu_dispatch_invalidate(reader, name);
    }
    else if (bind != BUSTER_CHECK_NONE)
    {
        check_target_versions_bind(reader, name, bind == name ? BUSTER_CHECK_NONE : bind);
    }
}

static void check_input_target_versions_declaration(CheckInputReader* reader, const CheckInputAttributes* attributes, uint32_t name, TypeId type,
                                                    uint32_t handle, bool is_inline, bool is_static, bool main)
{
    const CheckInputAttribute* target = check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET);
    const CheckInputAttribute* clones = check_target_versions_own(attributes, CHECK_ATTRIBUTE_TARGET_CLONES);
    const CheckTargetVersionKind kind = clones != NULL ? CHECK_TARGET_VERSION_CLONES : target != NULL ? CHECK_TARGET_VERSION_TARGET : CHECK_TARGET_VERSION_NONE;
    // The usual declaration, of neither attribute in a unit of no multiversioned function (a header's intrinsics are
    // target functions of their own names), asks nothing more.
    if (kind != CHECK_TARGET_VERSION_NONE || reader->target_mv_count != 0)
    {
        check_target_versions_rules(reader, target, clones, kind, attributes, name, type, handle, is_inline, is_static, main);
    }
}

// For check_input_bind: the earlier declaration a declaration `token` of a function multiversioned by target strings
// redeclares, or BUSTER_CHECK_NONE; *separate is whether it is an entity apart from the visible declaration before it (a
// version of its own, or one its rules refused).
static uint32_t check_input_target_versions_match(CheckInputReader* reader, uint32_t token, bool* separate)
{
    uint32_t result = BUSTER_CHECK_NONE;
    bool found = false;
    for (uint32_t i = reader->target_bind_count; !found && i > 0; i -= 1)
    {
        found = reader->target_bind_tokens[i - 1] == token;
        result = found ? reader->target_bind_redeclares[i - 1] : result;
    }
    // A redeclaration of a version is apart from the visible declaration too: check_input_cpu_dispatch_redeclare joins it
    // to its version.
    *separate = found || check_cpu_dispatch_invalid(reader, token);
    return result;
}

// Retain the candidate visible at an identifier use for later call/address diagnostics (#2424). The ordinary unit's
// sparse pool has no extra directory or blocks: only an actual target overload set pays for this separate column.
static void check_target_versions_candidate(CheckInputReader* reader, uint32_t use, uint32_t candidate)
{
    CheckInput* output = reader->output;
    if (!check_sparse_present(output->target_candidates) && !output->resource)
    {
        output->resource |= !check_sparse_reserve(reader->arena, &reader->target_candidate_pool, &output->target_candidates, 1,
                                                 reader->parser->tokens.count, false, false);
    }
    if (!output->resource)
    {
        check_sparse_set(reader->target_candidate_pool, output->target_candidates, use, candidate);
        output->resource |= reader->target_candidate_pool->exhausted;
    }
}

// A target callee may also be the address of the function, with parentheses on either side of the '&'. Keep this
// local: the builtin call rules use check_input_callee_end's name-only syntax. The parent check leaves '&f()' at f's
// call, rather than treating the address of its result as part of its callee.
static uint32_t check_target_versions_callee_end(const Parser* parser, uint32_t token, uint32_t* first)
{
    uint32_t low = token;
    uint32_t high = token;
    bool addressed = false;
    bool more = true;
    while (more && low > 0 && high + 1 < parser->tokens.count)
    {
        const bool group = parser->tree.roles[low - 1] == PARSE_ROLE_OPEN_GROUP &&
                           parser->tree.roles[high + 1] == PARSE_ROLE_CLOSE_GROUP && parser->tree.parents[low - 1] == high + 1;
        const bool address = !group && !addressed && parser->tree.roles[low - 1] == PARSE_ROLE_PREFIX &&
                             parser->tokens.kinds[low - 1] == PARSE_KIND_AMP && parser->tree.parents[high] == low - 1;
        low -= group || address;
        high += group;
        addressed |= address;
        more = group || address;
    }
    *first = low;
    return high;
}

static bool check_target_versions_overloadable(const CheckGraph* graph, uint32_t head)
{
    bool marked = false;
    for (uint32_t current = head; !marked && current < graph->overload_candidate_count;)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + current;
        marked = (candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0;
        current = candidate->previous < current ? candidate->previous : BUSTER_CHECK_NONE;
    }
    return marked;
}

// An unrelated overloadable declaration also records ordinary functions' lexical chains. For a target function's
// noncallee use that is not its overload set: the versions are one dispatcher address, as in a unit without those
// tables. Copy only this use's leaf, leaving the objects, source bindings, earlier uses, and genuine overloads alone.
static void check_target_versions_value_use(CheckInputReader* reader, uint32_t use)
{
    CheckGraph* graph = &reader->output->graph;
    const uint32_t node = reader->output->token_nodes[use];
    if (node < graph->count && graph->ops[node] == CHECK_OP_VALUE)
    {
        CheckValue value = check_graph_leaf_value(graph, node);
        if (value.overload_head != 0 && !check_target_versions_overloadable(graph, value.overload_head - 1))
        {
            value.overload_head = 0;
            check_graph_update_leaf(graph, node, &value);
        }
    }
}

// Attach the existing call selector to an actual target set's default, without changing the function value's binding
// or overload head. In particular, an integer-to-pointer argument must fail overload resolution even if the ordinary
// C conversion diagnostic is disabled. The sorted prefix `previous` may have receipts for ordinary functions too:
// an unrelated overloadable declaration enables those tables. Replace such a call's set, but leave a genuinely
// overloadable function's receipt alone. This runs after graph construction, so no declaration index is extended.
static void check_target_versions_call(CheckInputReader* reader, uint32_t close, uint32_t declaration, uint32_t previous)
{
    CheckInput* input = reader->output;
    CheckGraph* graph = &input->graph;
    const uint32_t node = input->token_nodes[close];
    uint32_t low = 0;
    uint32_t high = previous;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (graph->overload_calls[middle].node < node)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const bool present = low < previous && graph->overload_calls[low].node == node;
    const uint32_t prior = present ? graph->overload_calls[low].head : BUSTER_CHECK_NONE;
    const uint32_t captured = present && graph->overload_calls[low].captured_head != 0 ? graph->overload_calls[low].captured_head :
                              present ? prior + 1 : 0;
    const bool overloaded = check_target_versions_overloadable(graph, prior);
    const bool call = node < graph->count && graph->ops[node] == CHECK_OP_CALL && !overloaded;
    if (call && !input->resource && graph->overload_candidate_count == reader->overload_capacity)
    {
        const uint32_t limit = 1u << 30;
        const uint32_t capacity = reader->overload_capacity == 0 ? 8 :
                                  reader->overload_capacity < limit / 2 ? reader->overload_capacity * 2 : limit;
        CheckOverloadCandidate* grown = graph->overload_candidate_count < capacity ?
            BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCandidate, capacity) : NULL;
        input->resource |= grown == NULL;
        if (grown != NULL)
        {
            if (graph->overload_candidate_count != 0)
            {
                memcpy(grown, reader->overload_candidates, (uint64_t)graph->overload_candidate_count * sizeof(CheckOverloadCandidate));
            }
            reader->overload_candidates = grown;
            reader->overload_capacity = capacity;
            graph->overload_candidates = grown;
        }
    }
    if (call && !present && !input->resource && graph->overload_count == graph->overload_capacity)
    {
        const uint32_t doubled = graph->overload_capacity == 0 ? 8 :
                                 graph->overload_capacity < graph->count / 2 ? graph->overload_capacity * 2 : graph->count;
        const uint32_t capacity = doubled < graph->count ? doubled : graph->count;
        CheckOverloadCall* grown = graph->overload_count < capacity ? BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckOverloadCall, capacity) : NULL;
        input->resource |= grown == NULL;
        if (grown != NULL)
        {
            if (graph->overload_count != 0)
            {
                memcpy(grown, graph->overload_calls, (uint64_t)graph->overload_count * sizeof(CheckOverloadCall));
            }
            graph->overload_calls = grown;
            graph->overload_capacity = capacity;
        }
    }
    if (call && !input->resource)
    {
        const uint32_t head = graph->overload_candidate_count++;
        const uint32_t object = reader->objects[declaration];
        reader->overload_candidates[head] = (CheckOverloadCandidate)
        {
            .type = reader->declared_types[declaration],
            .declaration = declaration,
            .canonical = object - 1,
            .object = object,
            .previous = BUSTER_CHECK_NONE,
            .position = declaration,
            .flags = BUSTER_CHECK_OVERLOAD_TARGET,
        };
        const uint32_t receipt = present ? low : graph->overload_count++;
        graph->overload_calls[receipt] = (CheckOverloadCall){ .node = node, .head = head, .captured_head = captured };
    }
}

// Binding uses arrive in source order, but a nested call's node precedes its enclosing call's. Sort only these sparse
// call receipts, using binding's scalar/vector radix twins; no discovery pass over the expression graph is needed.
static void check_target_versions_calls_sort(CheckInputReader* reader, uint32_t previous)
{
    CheckGraph* graph = &reader->output->graph;
    const uint32_t count = graph->overload_count;
    if (!reader->output->resource && count > previous && count > 1)
    {
        const uint64_t mark = reader->scratch->length;
        uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint64_t, (uint64_t)count * 2);
        CheckOverloadCall* calls = keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(reader->scratch, CheckOverloadCall, count) : NULL;
        reader->output->resource |= calls == NULL;
        uint32_t any = 0;
        uint32_t every = UINT32_MAX;
        for (uint32_t i = 0; calls != NULL && i < count; i += 1)
        {
            const CheckOverloadCall call = graph->overload_calls[i];
            calls[i] = call;
            keys[i] = (uint64_t)call.node << 32 | i;
            any |= call.node;
            every &= call.node;
        }
        const uint64_t* sorted = calls != NULL ? c_bindings_sort(keys, keys + count, count, !BUSTER_SCALAR_PATH(reader->parser->options.scalar), any ^ every) : NULL;
        uint32_t written = 0;
        for (uint32_t i = 0; sorted != NULL && i < count; i += 1)
        {
            const uint32_t node = (uint32_t)(sorted[i] >> 32);
            if (written == 0 || graph->overload_calls[written - 1].node != node)
            {
                graph->overload_calls[written++] = calls[(uint32_t)sorted[i]];
            }
        }
        graph->overload_count = sorted != NULL ? written : count;
        arena_pop_to(reader->scratch, mark);
    }
}

// The target overload set at each function-name use (#1838, #2424): two distinct valid target entities visible in the
// use's lexical scope, with its latest default declaration retained. One visible version, repeated declarations of
// that version, and target_clones are ordinary functions for these diagnostics. The binding chain preserves source
// order; its depth keeps an inner declaration from reviving a hidden outer overload set. A call with no default still
// gets "no matching function for call to 'f'" here. Run once the walk is done, for a unit with multiversioning.
static void check_input_target_versions_calls(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    const CBindings* bindings = &reader->bindings;
    const uint32_t previous = reader->output->graph.overload_count;
    for (uint32_t event = bindings->base; reader->output->target_multiversion && !reader->output->resource && bindings->valid && event < bindings->count; event += 1)
    {
        const uint32_t handle = bindings->operation[event] == C_BINDING_USE ? bindings->previous[event] : 0;
        const uint32_t reached = handle != 0 ? bindings->declaration[handle - 1] : BUSTER_CHECK_NONE;
        const uint32_t use = bindings->declaration[event];
        const bool function = reached < parser->tokens.count && reader->declaration_kinds[reached] == C_BINDING_FUNCTION && use < parser->tokens.count;
        const uint32_t depth = function ? bindings->depth[handle - 1] : 0;
        uint32_t group = use;
        const uint32_t end = function ? check_target_versions_callee_end(parser, use, &group) : use;
        const uint32_t close = function ? parser->tree.parents[end] : BUSTER_CHECK_NONE;
        const bool call = close < parser->tokens.count && parser->tree.roles[close] == PARSE_ROLE_CLOSE_CALL && end + 1 < close &&
                          parser->tree.roles[end + 1] == PARSE_ROLE_OPEN_CALL;
        uint32_t object = BUSTER_CHECK_NONE;
        uint32_t candidate = 0;
        bool overloaded = false;
        for (uint32_t h = function ? handle : 0; h != 0; h = bindings->previous[h - 1])
        {
            const uint32_t declaration = bindings->declaration[h - 1];
            const bool version = declaration < parser->tokens.count && declaration < use && bindings->depth[h - 1] == depth &&
                                 reader->declaration_kinds[declaration] == C_BINDING_FUNCTION && !check_cpu_dispatch_invalid(reader, declaration);
            const uint32_t since = version ? check_target_versions_since(reader, reader->objects[declaration]) : BUSTER_CHECK_NONE;
            const CheckInputAttribute* target = since < use ? check_target_versions_attribute(reader, bindings->previous[h - 1], declaration, CHECK_ATTRIBUTE_TARGET) : NULL;
            const bool visible = target != NULL;
            const uint32_t entity = visible ? reader->objects[declaration] : BUSTER_CHECK_NONE;
            overloaded |= visible && object != BUSTER_CHECK_NONE && object != entity;
            object = visible && object == BUSTER_CHECK_NONE ? entity : object;
            candidate = visible && candidate == 0 && check_target_versions_default(target) ? declaration + 1 : candidate;
        }
        if (overloaded)
        {
            check_target_versions_candidate(reader, use, candidate);
            if (!call && candidate != 0)
            {
                check_target_versions_value_use(reader, use);
            }
        }
        if (call && overloaded && candidate != 0)
        {
            check_target_versions_call(reader, close, candidate - 1, previous);
        }
        if (call && overloaded && candidate == 0)
        {
            // At the callee's outermost parenthesis, as clang's call begins; the name is `attribute`'s.
            const uint32_t before = reader->output->multiversion_report_count;
            check_input_cpu_dispatch_report(reader, group, CHECK_CPU_DISPATCH_NO_MATCH, BUSTER_CHECK_NONE, CHECK_INPUT_NOTE_NONE, use);
            if (reader->output->multiversion_report_count != before)
            {
                // Published where the call is (no declaration's attributes complete it).
                reader->output->multiversion_reports[before].completion = 0;
            }
        }
    }
    check_target_versions_calls_sort(reader, previous);
}

#endif
