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

// A warning of `kind` (and `id`) at the string literal `token`, its data as above. A string too long for the data's
// fields keeps its warning out.
static void check_input_target_clones_warning(CheckInputReader* reader, const CheckInputAttribute* attribute, uint32_t id, uint32_t kind, uint32_t token,
                                              uint32_t start, uint32_t length, uint32_t offset, bool cpu)
{
    if (start < BUSTER_CHECK_TARGET_CLONES_FIELD && length < BUSTER_CHECK_TARGET_CLONES_FIELD && offset < BUSTER_CHECK_TARGET_CLONES_FIELD)
    {
        const CheckWarningEvent event =
        {
            .id = id,
            .kind = kind,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = attribute->argument_first,
            .payload = { .name = attribute->name, .token = token, .data = start | length << 10 | offset << 20 | (cpu ? BUSTER_CHECK_TARGET_CLONES_CPU : 0) },
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
    // The versions seen, as bytes of the strings' decoded texts (Params in clang).
    const uint32_t capacity = 64;
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
            check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED, first, 0, 0, 0,
                                              false);
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
                                                  item_first + 5, item_last - item_first - 5, offset, true);
                unsupported = true;
            }
            else if (!arch && !is_default && check_target_version_feature(item) < 0)
            {
                check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_IGNORED_ATTRIBUTES, CHECK_WARNING_EVENT_TARGET_CLONES_UNSUPPORTED, first,
                                                  item_first, item_last - item_first, offset, false);
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
                                                  item_first, item_last - item_first, offset, false);
            }
            if (!unsupported && seen_count < capacity)
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
        check_input_target_clones_warning(reader, attribute, CHECK_WARNING_ID_TARGET_CLONES_MIXED_SPECIFIERS, CHECK_WARNING_EVENT_TARGET_CLONES_MIXED, first, 0, 0, 0,
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
    arena_pop_to(reader->scratch, mark);
    return valid;
}

#endif
