// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Clang-following component: see THIRD_PARTY_NOTICES.md ("Unicode character names") and LICENSES/LLVM.txt. Part of
// the LLVM Project, under the Apache License v2.0 with LLVM Exceptions: its name matching and diagnostic text are a C
// translation of llvm/lib/Support/UnicodeNameToCodepoint.cpp and clang/lib/Basic/Diagnostic.cpp at llvm-project
// 2875c6073a7e. Local modifications Copyright (c) 2026 David Gonzalez Martin, under the same terms.
//
// Unicode character names (#1447): what a named universal character name, \N{NAME}, stands for, as clang 23 reads one
// (LLVM's llvm/lib/Support/UnicodeNameToCodepoint.cpp, whose rules these follow), and the text clang's diagnostics of a
// bad one print. c_lexer.c includes it; the table is c_unicode_names.inc (tools/generate_unicode_names.c, from the
// Unicode Character Database: see THIRD_PARTY_NOTICES.md), derived Unicode data under its own terms.
//
// - unicode_name_strict: a name exactly as Unicode writes it (a stored name or alias, a Hangul syllable's, or one made by
//   rule from its code point), as nameToCodepointStrict.
// - unicode_name_loose: the same, ignoring case, spaces, underscores and medial hyphens (UAX #44's LM2), as
//   nameToCodepointLooseMatching, which clang's error notes and its lexer's recovery use.
// - unicode_name_nearest: the five stored names nearest a bad one, by the edit distance of their letters and digits, as
//   nearestMatchesForCodepointName gives clang's "did you mean" notes.
// - unicode_escape_*: clang's EscapeStringForDiagnostic and EscapeSingleCodepointForDiagnostic, the way its diagnostics
//   print a name's bytes and a character.
//
// All of it is a cold path: only a literal or a token with a \N reads the table. The stored names are sorted by their
// loose keys and front-coded in buckets, which an exact or a loose lookup finds by a binary search over their first
// names (unicode_name_stored); the nearest search, an error's, reads every name once, in order, and keeps the edit
// distances' rows of the prefix a name shares with the one before (the rows of LLVM's trie walk).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct UnicodeNameRange UnicodeNameRange;
struct UnicodeNameRange
{
    const char* prefix;
    uint32_t first;
    uint32_t last;
    bool listed;
};

#include "buster/compiler/c_unicode_names.inc"

// The entry points' linkage, so that the executable has one copy of the table (about 300 KB): main.c's units call the
// lexer's home's (BUSTER_LEXER_ENTRY), and so does a unit of its own that includes the lexer, a benchmark's, which the
// executable links with them (its definitions are inline ones, which clang emits only to inline them, and noinline
// keeps it from that). Where these definitions aren't the home's, they call this file's static functions, a C2y
// feature that clang takes in C17 as an extension (-Wstatic-in-inline).
#if defined(BUSTER_HOME_DRIVER)
#define BUSTER_UNICODE_ENTRY BUSTER_LEXER_ENTRY
#else
#define BUSTER_UNICODE_ENTRY inline __attribute__((noinline))
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstatic-in-inline"

BUSTER_UNICODE_ENTRY __attribute__((unused)) bool unicode_name_strict(const uint8_t* name, uint32_t length, uint32_t* code_point);
BUSTER_UNICODE_ENTRY __attribute__((unused)) bool unicode_name_loose(const uint8_t* name, uint32_t length, uint32_t* code_point);

// The longest name a lookup reads (a longer one is no name: the longest stored one has 88 bytes).
#define BUSTER_UNICODE_NAME_BYTES 128
// The suggestions of unicode_name_nearest, clang's MaxMatchesCount.
#define BUSTER_UNICODE_NAME_MATCHES 5
// The bytes clang's EscapeSingleCodepointForDiagnostic writes at most ("'X' U+10FFFF" with X four bytes, and a NUL).
#define BUSTER_UNICODE_ESCAPE_BYTES 16

typedef struct UnicodeNameMatch UnicodeNameMatch;
struct UnicodeNameMatch
{
    char name[BUSTER_UNICODE_NAME_BYTES];
    uint32_t distance;
    uint32_t code_point;
};

// A position in the stored names: the name just decoded, its code point, and the next name's offset in the stream.
typedef struct UnicodeNameCursor UnicodeNameCursor;
struct UnicodeNameCursor
{
    uint32_t offset;
    // The next name's index, whose bucket's first name has its code point whole (the others, the difference).
    uint32_t index;
    uint32_t code_point;
    uint32_t length;
    // The bytes this name shares with the one before it.
    uint32_t shared;
    char name[BUSTER_UNICODE_NAME_BYTES];
};

// Decodes the stored name at the cursor's offset, front-coded on the one it holds, and moves past it.
static __attribute__((unused)) void unicode_name_next(UnicodeNameCursor* cursor)
{
    const uint8_t* stream = unicode_name_stream;
    uint32_t at = cursor->offset;
    const uint32_t shared = stream[at];
    at += 1;
    uint64_t zigzag = 0;
    uint32_t shift = 0;
    uint8_t byte = 0x80;
    while ((byte & 0x80) != 0)
    {
        byte = stream[at];
        zigzag |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
        at += 1;
    }
    const int64_t difference = (zigzag & 1) != 0 ? -(int64_t)((zigzag + 1) >> 1) : (int64_t)(zigzag >> 1);
    cursor->code_point = cursor->index % BUSTER_UNICODE_NAME_BUCKET == 0 ? (uint32_t)difference : (uint32_t)((int64_t)cursor->code_point + difference);
    uint32_t length = shared;
    while (stream[at] != 0)
    {
        const uint32_t begin = unicode_name_piece_starts[stream[at]];
        const uint32_t size = unicode_name_piece_starts[stream[at] + 1] - begin;
        memcpy(cursor->name + length, unicode_name_piece_text + begin, size);
        length += size;
        at += 1;
    }
    cursor->name[length] = 0;
    cursor->length = length;
    cursor->shared = shared;
    cursor->offset = at + 1;
    cursor->index += 1;
}

// A cursor at bucket `bucket`'s first name, decoded.
static __attribute__((unused)) UnicodeNameCursor unicode_name_bucket(uint32_t bucket)
{
    UnicodeNameCursor cursor = {
        .offset = unicode_name_buckets[bucket],
        .index = bucket * BUSTER_UNICODE_NAME_BUCKET,
    };
    unicode_name_next(&cursor);
    return cursor;
}

BUSTER_INLINE bool unicode_is_alphanumeric(uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

BUSTER_INLINE uint8_t unicode_upper(uint8_t c)
{
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32) : c;
}

// clang's IgnoreSpaces in startsWith: past the spaces, underscores and medial hyphens at `at` (before `end`), each
// byte read becoming `*previous`. A hyphen at the end of a prefix (`prefix`) is medial too.
static __attribute__((unused)) uint32_t unicode_name_skip(const uint8_t* text, uint32_t at, uint32_t end, uint8_t* previous, bool prefix)
{
    bool skipping = true;
    while (skipping && at != end)
    {
        const uint8_t c = text[at];
        const bool medial = c == '-' && unicode_is_alphanumeric(*previous) && ((at + 1 != end && unicode_is_alphanumeric(text[at + 1])) || (at + 1 == end && prefix));
        skipping = c == ' ' || c == '_' || medial;
        *previous = c;
        at += skipping ? 1 : 0;
    }
    return at;
}

// clang's startsWith: whether `name` (of `length` bytes) starts with `needle`, exactly or (!`strict`) loosely, with the
// bytes of `name` it took in `*consumed`. `*previous` is the byte before `name` in the loose comparison, moved past
// what it took when it matches. A loose `prefix` takes its needle's last hyphen as medial.
static __attribute__((unused)) bool unicode_name_starts_with(const uint8_t* name, uint32_t length, const char* needle, bool strict, uint32_t* consumed, uint8_t* previous, bool prefix)
{
    const uint32_t needle_length = (uint32_t)strlen(needle);
    const uint8_t* wanted = (const uint8_t*)needle;
    bool result = false;
    *consumed = 0;
    if (strict)
    {
        result = length >= needle_length && memcmp(name, needle, needle_length) == 0;
        *consumed = result ? needle_length : 0;
    }
    else if (needle_length == 0)
    {
        result = true;
    }
    else
    {
        const uint8_t origin = *previous;
        uint8_t previous_needle = wanted[0];
        uint32_t at = 0;
        uint32_t needle_at = 0;
        bool comparing = true;
        while (comparing)
        {
            at = unicode_name_skip(name, at, length, previous, false);
            needle_at = unicode_name_skip(wanted, needle_at, needle_length, &previous_needle, prefix);
            comparing = needle_at != needle_length && at != length && unicode_upper(wanted[needle_at]) == unicode_upper(name[at]);
            at += comparing ? 1 : 0;
            needle_at += comparing ? 1 : 0;
        }
        *consumed = at;
        result = needle_at == needle_length;
        *previous = result ? *previous : origin;
    }
    return result;
}

// The Hangul syllables' jamo (clang's HangulSyllables): each column's initial, medial and final spellings, NULL past its
// count.
static const char* const unicode_hangul_jamo[28][3] = {
    { "G", "A", "" },
    { "GG", "AE", "G" },
    { "N", "YA", "GG" },
    { "D", "YAE", "GS" },
    { "DD", "EO", "N" },
    { "R", "E", "NJ" },
    { "M", "YEO", "NH" },
    { "B", "YE", "D" },
    { "BB", "O", "L" },
    { "S", "WA", "LG" },
    { "SS", "WAE", "LM" },
    { "", "OE", "LB" },
    { "J", "YO", "LS" },
    { "JJ", "U", "LT" },
    { "C", "WEO", "LP" },
    { "K", "WE", "LH" },
    { "T", "WI", "M" },
    { "P", "YU", "B" },
    { "H", "EU", "BS" },
    { NULL, "YI", "S" },
    { NULL, "I", "SS" },
    { NULL, NULL, "NG" },
    { NULL, NULL, "J" },
    { NULL, NULL, "C" },
    { NULL, NULL, "K" },
    { NULL, NULL, "T" },
    { NULL, NULL, "P" },
    { NULL, NULL, "H" },
};

// clang's findSyllable: the longest jamo of `column` that `name` starts with (a longer spelling taken only past the bytes
// a shorter one took), its index in `*index` and the bytes it took returned, or -1 for none.
static __attribute__((unused)) int32_t unicode_hangul_find(const uint8_t* name, uint32_t length, bool strict, uint8_t* previous, int32_t* index, uint32_t column)
{
    static const uint32_t counts[3] = { 19, 21, 28 };
    int32_t taken = -1;
    uint8_t chosen_previous = *previous;
    for (uint32_t i = 0; i < counts[column]; i += 1)
    {
        const char* jamo = unicode_hangul_jamo[i][column];
        uint32_t consumed = 0;
        uint8_t copy = *previous;
        if ((int32_t)strlen(jamo) > taken && unicode_name_starts_with(name, length, jamo, strict, &consumed, &copy, false))
        {
            taken = (int32_t)consumed;
            *index = (int32_t)i;
            chosen_previous = copy;
        }
    }
    *previous = taken >= 0 ? chosen_previous : *previous;
    return taken;
}

// clang's nameToHangulCodePoint: "HANGUL SYLLABLE " and its initial, medial and final jamo.
static __attribute__((unused)) bool unicode_name_hangul(const uint8_t* name, uint32_t length, bool strict, uint32_t* code_point)
{
    uint32_t consumed = 0;
    uint8_t previous = 0;
    bool result = unicode_name_starts_with(name, length, "HANGUL SYLLABLE ", strict, &consumed, &previous, false);
    int32_t jamo[3] = { -1, -1, -1 };
    uint32_t at = consumed;
    for (uint32_t column = 0; result && column < 3; column += 1)
    {
        const int32_t taken = unicode_hangul_find(name + at, length - at, strict, &previous, &jamo[column], column);
        at += taken > 0 ? (uint32_t)taken : 0;
    }
    result = result && jamo[0] >= 0 && jamo[1] >= 0 && jamo[2] >= 0 && at == length;
    *code_point = result ? 0xAC00u + ((uint32_t)jamo[0] * 21 + (uint32_t)jamo[1]) * 28 + (uint32_t)jamo[2] : *code_point;
    return result;
}

// clang's nameToGeneratedCodePoint: a prefix of the ranges' names made by rule, then a hexadecimal number in one of its
// ranges (LLVM's getAsUnsignedInteger: one digit at least, no sign or "0x", within 64 bits). Strictly, a lowercase
// digit is none.
static __attribute__((unused)) bool unicode_name_generated(const uint8_t* name, uint32_t length, bool strict, uint32_t* code_point)
{
    bool found = false;
    bool refused = false;
    for (uint32_t r = 0; !found && !refused && r < BUSTER_ARRAY_LENGTH(unicode_name_ranges); r += 1)
    {
        const UnicodeNameRange* range = &unicode_name_ranges[r];
        uint32_t consumed = 0;
        uint8_t previous = 0;
        if (unicode_name_starts_with(name, length, range->prefix, strict, &consumed, &previous, true))
        {
            uint64_t value = 0;
            bool valid = consumed < length;
            for (uint32_t i = consumed; i < length; i += 1)
            {
                const uint8_t c = name[i];
                const uint32_t digit = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : 16;
                refused |= strict && c >= 'a' && c <= 'f';
                valid &= digit < 16 && (value >> 60) == 0;
                value = valid ? value * 16 + digit : value;
            }
            found = !refused && valid && value >= range->first && value <= range->last;
            *code_point = found ? (uint32_t)value : *code_point;
        }
    }
    return found;
}

// A name's loose key, into `key` (room for its length): its bytes but spaces, underscores and medial hyphens,
// uppercased (UAX #44's LM2, as clang's loose comparison skips them: unicode_name_skip). Its length.
static __attribute__((unused)) uint32_t unicode_name_loose_key(const uint8_t* name, uint32_t length, uint8_t* key)
{
    uint32_t count = 0;
    uint8_t previous = 0;
    for (uint32_t at = unicode_name_skip(name, 0, length, &previous, false); at < length; at = unicode_name_skip(name, at, length, &previous, false))
    {
        key[count] = unicode_upper(name[at]);
        count += 1;
        at += 1;
    }
    return count;
}

// Compares two keys, as strcmp.
static __attribute__((unused)) int32_t unicode_name_compare_keys(const uint8_t* a, uint32_t a_length, const uint8_t* b, uint32_t b_length)
{
    const uint32_t common = a_length < b_length ? a_length : b_length;
    const int32_t order = common != 0 ? memcmp(a, b, common) : 0;
    return order != 0 ? order : a_length < b_length ? -1 : a_length > b_length ? 1 : 0;
}

// A stored name or alias, written exactly (`exact`) or loosely (the trie walk of clang's compareNode, which matches when
// both names' loose keys are equal). The names are sorted by their loose keys (then by their bytes): a binary search
// over the buckets' first names finds the last bucket whose first key is below the name's, and a scan from there reads
// the names of its key. clang's one exception loosely: HANGUL JUNGSEONG O-E and OE share a key, and a name with "O-E"
// is the former.
static __attribute__((unused)) bool unicode_name_stored(const uint8_t* name, uint32_t length, bool exact, uint32_t* code_point)
{
    uint8_t key[BUSTER_UNICODE_NAME_BYTES];
    uint8_t candidate[BUSTER_UNICODE_NAME_BYTES];
    const uint32_t key_length = length < BUSTER_UNICODE_NAME_BYTES ? unicode_name_loose_key(name, length, key) : 0;
    const uint32_t buckets = (uint32_t)BUSTER_ARRAY_LENGTH(unicode_name_buckets);
    uint32_t low = 0;
    uint32_t high = key_length != 0 ? buckets : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const UnicodeNameCursor first = unicode_name_bucket(middle);
        const uint32_t first_length = unicode_name_loose_key((const uint8_t*)first.name, first.length, candidate);
        const bool below = unicode_name_compare_keys(candidate, first_length, key, key_length) < 0;
        low = below ? middle + 1 : low;
        high = below ? high : middle;
    }
    bool found = false;
    UnicodeNameCursor cursor = { 0 };
    if (key_length != 0)
    {
        cursor = unicode_name_bucket(low != 0 ? low - 1 : 0);
    }
    int32_t order = -1;
    while (key_length != 0 && !found && order <= 0)
    {
        const uint32_t candidate_length = unicode_name_loose_key((const uint8_t*)cursor.name, cursor.length, candidate);
        order = unicode_name_compare_keys(candidate, candidate_length, key, key_length);
        found = order == 0 && (!exact || (cursor.length == length && memcmp(cursor.name, name, length) == 0));
        const bool more = !found && order <= 0 && cursor.index < BUSTER_UNICODE_NAME_COUNT;
        order = more ? order : 1;
        if (more)
        {
            unicode_name_next(&cursor);
        }
    }
    if (found)
    {
        bool hyphenated = false;
        for (uint32_t i = 0; i + 2 < length; i += 1)
        {
            hyphenated |= unicode_upper(name[i]) == 'O' && name[i + 1] == '-' && unicode_upper(name[i + 2]) == 'E';
        }
        const bool jungseong = !exact && (cursor.code_point == 0x116C || cursor.code_point == 0x1180);
        *code_point = jungseong ? (hyphenated ? 0x1180 : 0x116C) : cursor.code_point;
    }
    return found;
}

// The code point a name stands for, exactly as Unicode writes it (clang's nameToCodepointStrict).
BUSTER_UNICODE_ENTRY bool unicode_name_strict(const uint8_t* name, uint32_t length, uint32_t* code_point)
{
    const bool valid = length != 0 && length < BUSTER_UNICODE_NAME_BYTES;
    return valid && (unicode_name_hangul(name, length, true, code_point) || unicode_name_generated(name, length, true, code_point) ||
                     unicode_name_stored(name, length, true, code_point));
}

// The code point a name stands for, ignoring case, spaces, underscores and medial hyphens (clang's
// nameToCodepointLooseMatching).
BUSTER_UNICODE_ENTRY bool unicode_name_loose(const uint8_t* name, uint32_t length, uint32_t* code_point)
{
    const bool valid = length != 0 && length < BUSTER_UNICODE_NAME_BYTES;
    return valid && (unicode_name_hangul(name, length, false, code_point) || unicode_name_generated(name, length, false, code_point) ||
                     unicode_name_stored(name, length, false, code_point));
}

// The edit distances' rows of the name being measured (LLVM's matrix: a row per letter or digit of the name, a column
// per letter or digit of the pattern, its first BUSTER_UNICODE_NAME_LONGEST).
typedef struct UnicodeNameDistances UnicodeNameDistances;
struct UnicodeNameDistances
{
    uint8_t pattern[BUSTER_UNICODE_NAME_LONGEST];
    uint32_t columns;
    uint8_t rows[BUSTER_UNICODE_NAME_BYTES + 1][BUSTER_UNICODE_NAME_LONGEST + 1];
    // The row after each byte of the name: rows_at[b] is the row count once bytes [0, b) are read.
    uint32_t rows_at[BUSTER_UNICODE_NAME_BYTES + 1];
};

// The edit distance of the name `name` (of `length` bytes) and the pattern, its rows from byte `from` on computed afresh
// (the bytes before it are the last name's, whose rows stand).
static __attribute__((unused)) uint32_t unicode_name_distance(UnicodeNameDistances* distances, const char* name, uint32_t length, uint32_t from)
{
    uint32_t row = distances->rows_at[from];
    for (uint32_t b = from; b < length; b += 1)
    {
        const uint8_t c = (uint8_t)name[b];
        if (unicode_is_alphanumeric(c))
        {
            distances->rows[row][0] = (uint8_t)row;
            for (uint32_t i = 1; i < distances->columns; i += 1)
            {
                const uint32_t deletion = distances->rows[row][i - 1] + 1u;
                const uint32_t insertion = distances->rows[row - 1][i] + 1u;
                const uint32_t replacement = distances->rows[row - 1][i - 1] + (distances->pattern[i - 1] != c ? 1u : 0u);
                const uint32_t least = deletion < insertion ? deletion : insertion;
                distances->rows[row][i] = (uint8_t)(least < replacement ? least : replacement);
            }
            row += 1;
        }
        distances->rows_at[b + 1] = row;
    }
    return distances->rows[row - 1][distances->columns - 1];
}

// Keeps a name among the nearest `*count` (sorted by distance, then by name), as clang's Insert does.
static __attribute__((unused)) void unicode_name_offer(UnicodeNameMatch* matches, uint32_t* count, const char* name, uint32_t distance, uint32_t code_point)
{
    uint32_t place = *count;
    for (uint32_t i = 0; i < *count && place == *count; i += 1)
    {
        const bool before = distance < matches[i].distance || (distance == matches[i].distance && strcmp(name, matches[i].name) < 0);
        place = before ? i : place;
    }
    if (place < BUSTER_UNICODE_NAME_MATCHES)
    {
        const uint32_t kept = *count < BUSTER_UNICODE_NAME_MATCHES ? *count : BUSTER_UNICODE_NAME_MATCHES - 1;
        memmove(matches + place + 1, matches + place, (kept - place) * sizeof(UnicodeNameMatch));
        snprintf(matches[place].name, sizeof(matches[place].name), "%s", name);
        matches[place].distance = distance;
        matches[place].code_point = code_point;
        *count = kept + 1;
    }
}

// The stored and listed names nearest `pattern` (clang's nearestMatchesForCodepointName with its five): the edit
// distance of their letters and digits, uppercased, with the pattern's first BUSTER_UNICODE_NAME_LONGEST, ties by name.
// Their count is returned, into `matches` (room for BUSTER_UNICODE_NAME_MATCHES). Every name is read: an error's cold
// path, some milliseconds.
static __attribute__((unused)) uint32_t unicode_name_nearest(const uint8_t* pattern, uint32_t length, UnicodeNameMatch* matches)
{
    UnicodeNameDistances distances;
    uint32_t letters = 0;
    for (uint32_t i = 0; i < length; i += 1)
    {
        if (unicode_is_alphanumeric(pattern[i]) && letters < BUSTER_UNICODE_NAME_LONGEST)
        {
            distances.pattern[letters] = unicode_upper(pattern[i]);
            letters += 1;
        }
    }
    distances.columns = letters + 1;
    for (uint32_t i = 0; i < distances.columns; i += 1)
    {
        distances.rows[0][i] = (uint8_t)i;
    }
    distances.rows_at[0] = 1;
    uint32_t count = 0;
    UnicodeNameCursor cursor = { 0 };
    for (uint32_t n = 0; n < BUSTER_UNICODE_NAME_COUNT; n += 1)
    {
        unicode_name_next(&cursor);
        const uint32_t distance = unicode_name_distance(&distances, cursor.name, cursor.length, cursor.shared);
        unicode_name_offer(matches, &count, cursor.name, distance, cursor.code_point);
    }
    for (uint32_t r = 0; r < BUSTER_ARRAY_LENGTH(unicode_name_ranges); r += 1)
    {
        const UnicodeNameRange* range = &unicode_name_ranges[r];
        const uint32_t prefix = (uint32_t)strlen(range->prefix);
        for (uint32_t c = range->first; range->listed && c <= range->last; c += 1)
        {
            char name[BUSTER_UNICODE_NAME_BYTES];
            const uint32_t name_length = (uint32_t)snprintf(name, sizeof(name), "%s%04X", range->prefix, c);
            const uint32_t distance = unicode_name_distance(&distances, name, name_length, c == range->first ? 0 : prefix);
            unicode_name_offer(matches, &count, name, distance, c);
        }
    }
    return count;
}

static __attribute__((unused)) bool unicode_in_ranges(const uint32_t (*ranges)[2], uint32_t count, uint32_t code_point)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool after = ranges[middle][1] < code_point;
        low = after ? middle + 1 : low;
        high = after ? high : middle;
    }
    return low < count && ranges[low][0] <= code_point;
}

// LLVM's isPrintable: the general categories L, M, N, P, S and Zs, and U+00AD.
static __attribute__((unused)) bool unicode_is_printable(uint32_t code_point)
{
    return code_point == 0xAD || unicode_in_ranges(unicode_printable_ranges, BUSTER_ARRAY_LENGTH(unicode_printable_ranges), code_point);
}

// LLVM's isFormatting: the general category Cf.
static __attribute__((unused)) bool unicode_is_formatting(uint32_t code_point)
{
    return unicode_in_ranges(unicode_formatting_ranges, BUSTER_ARRAY_LENGTH(unicode_formatting_ranges), code_point);
}

// The length of the well-formed UTF-8 sequence at `bytes` (of `length`), with its code point, or 0 (LLVM's
// isLegalUTF8Sequence).
static __attribute__((unused)) uint32_t unicode_utf8_sequence(const uint8_t* bytes, uint32_t length, uint32_t* code_point)
{
    const uint8_t lead = length != 0 ? bytes[0] : 0x80;
    uint32_t size = lead < 0x80 ? 1 : lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    const uint8_t low = lead == 0xE0 ? 0xA0 : lead == 0xF0 ? 0x90 : 0x80;
    const uint8_t high = lead == 0xED ? 0x9F : lead == 0xF4 ? 0x8F : 0xBF;
    size = size <= length ? size : 0;
    uint32_t value = size == 1 ? lead : size == 2 ? lead & 0x1Fu : size == 3 ? lead & 0x0Fu : lead & 0x07u;
    for (uint32_t k = 1; k < size; k += 1)
    {
        const bool valid = k == 1 ? bytes[1] >= low && bytes[1] <= high : (bytes[k] & 0xC0) == 0x80;
        value = (value << 6) | (bytes[k] & 0x3Fu);
        size = valid ? size : 0;
    }
    *code_point = value;
    return size;
}

// LLVM's findMaximalSubpartOfIllFormedUTF8Sequence: the bytes of an ill-formed sequence at `bytes` that the Unicode
// Standard's maximal subpart takes (one at least).
static __attribute__((unused)) uint32_t unicode_utf8_subpart(const uint8_t* bytes, uint32_t length)
{
    const uint8_t lead = bytes[0];
    const uint32_t size = lead >= 0xC2 && lead <= 0xDF ? 2 : lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 1;
    const uint8_t low = lead == 0xE0 ? 0xA0 : lead == 0xF0 ? 0x90 : 0x80;
    const uint8_t high = lead == 0xED ? 0x9F : lead == 0xF4 ? 0x8F : 0xBF;
    uint32_t taken = 1;
    bool continuing = size > 1;
    while (continuing && taken < size && taken < length)
    {
        const uint8_t c = bytes[taken];
        continuing = taken == 1 ? c >= low && c <= high : (c & 0xC0) == 0x80;
        taken += continuing ? 1 : 0;
    }
    return taken;
}

// Appends `text` to `out` (room for `capacity`), clipped.
static __attribute__((unused)) void unicode_append(char* out, uint32_t capacity, uint32_t* length, const char* text, uint32_t size)
{
    const uint32_t room = capacity > *length + 1 ? capacity - *length - 1 : 0;
    const uint32_t taken = size < room ? size : room;
    memcpy(out + *length, text, taken);
    *length += taken;
    out[*length] = 0;
}

// clang's EscapeStringForDiagnostic of `bytes` (`for_code_point`: EscapeSingleCodepointForDiagnostic's, of the first
// character alone) into `out`, NUL-terminated: a printable ASCII byte (and in a string, whitespace) as it is; another
// character as itself where it is printable (in a string, also formatting), quoted and followed by U+XXXX for one
// character, else as U+XXXX (<U+XXXX> in a string); an ill-formed byte as <0xXX>.
static __attribute__((unused)) uint32_t unicode_escape(const uint8_t* bytes, uint32_t length, bool for_code_point, char* out, uint32_t capacity)
{
    uint32_t written = 0;
    out[0] = 0;
    uint32_t end = length;
    if (for_code_point && length != 0)
    {
        uint32_t ignored = 0;
        const uint32_t size = unicode_utf8_sequence(bytes, length, &ignored);
        end = size != 0 ? size : unicode_utf8_subpart(bytes, length);
    }
    uint32_t at = 0;
    while (at < end)
    {
        const uint8_t c = bytes[at];
        const bool whitespace = c == ' ' || (c >= '\t' && c <= '\r');
        const bool printable = c >= 0x20 && c < 0x7F;
        uint32_t code_point = 0;
        const uint32_t size = unicode_utf8_sequence(bytes + at, end - at, &code_point);
        char text[BUSTER_UNICODE_ESCAPE_BYTES + 8];
        int32_t length_written = 0;
        if (!for_code_point && (printable || whitespace))
        {
            length_written = snprintf(text, sizeof(text), "%c", c);
            at += 1;
        }
        else if (for_code_point && printable)
        {
            length_written = snprintf(text, sizeof(text), "'%c'", c);
            at += 1;
        }
        else if (size != 0)
        {
            const bool shown = unicode_is_printable(code_point) || (!for_code_point && unicode_is_formatting(code_point));
            length_written = shown && !for_code_point ? snprintf(text, sizeof(text), "%.*s", (int)size, (const char*)bytes + at) :
                             shown ? snprintf(text, sizeof(text), "'%.*s' U+%04X", (int)size, (const char*)bytes + at, code_point) :
                             snprintf(text, sizeof(text), for_code_point ? "U+%04X" : "<U+%04X>", code_point);
            at += size;
        }
        else
        {
            length_written = snprintf(text, sizeof(text), "<0x%02X>", c);
            at += 1;
        }
        unicode_append(out, capacity, &written, text, (uint32_t)length_written);
    }
    return written;
}

// clang's EscapeSingleCodepointForDiagnostic of a code point (at most U+10FFFF, not a surrogate). U+FEFF is nothing: LLVM's
// convertUTF32ToUTF8String drops a byte order mark that starts its text.
static __attribute__((unused)) uint32_t unicode_escape_code_point(uint32_t code_point, char* out, uint32_t capacity)
{
    uint8_t bytes[4];
    const uint32_t size = code_point < 0x80 ? 1 : code_point < 0x800 ? 2 : code_point < 0x10000 ? 3 : 4;
    bytes[0] = (uint8_t)(size == 1 ? code_point : size == 2 ? 0xC0 | (code_point >> 6) : size == 3 ? 0xE0 | (code_point >> 12) : 0xF0 | (code_point >> 18));
    for (uint32_t k = 1; k < size; k += 1)
    {
        bytes[k] = (uint8_t)(0x80 | ((code_point >> (6 * (size - k - 1))) & 0x3F));
    }
    out[0] = 0;
    return code_point != 0xFEFF ? unicode_escape(bytes, size, true, out, capacity) : 0;
}

// What clang says of a named universal character name whose name isn't Unicode's (#1447): the error, "'NAME' is not a
// valid Unicode character name", then its notes, each at a byte of the name (`offset`). In a literal
// (`literal`, clang's DiagnoseInvalidUnicodeCharacterName): the first byte no name has ("character C cannot appear in a
// Unicode character name"), then where the name matches loosely, that names are sensitive to case and whitespace,
// else where every byte may be a name's, the nearest names ("did you mean NAME (C)?") while each is at most 3 farther
// than the one before. In an identifier (clang's tryReadNamedUCN), the loose match's note alone. `loose` is the loose
// match's code point where there is one.
#define BUSTER_UNICODE_NAME_NOTES (BUSTER_UNICODE_NAME_MATCHES + 1)
#define BUSTER_UNICODE_NAME_TEXT (2 * BUSTER_UNICODE_NAME_BYTES + 64)

typedef struct UnicodeNameNote UnicodeNameNote;
struct UnicodeNameNote
{
    uint32_t offset;
    char message[BUSTER_UNICODE_NAME_TEXT];
};

typedef struct UnicodeNameDiagnosis UnicodeNameDiagnosis;
struct UnicodeNameDiagnosis
{
    char error[4 * BUSTER_UNICODE_NAME_TEXT];
    UnicodeNameNote notes[BUSTER_UNICODE_NAME_NOTES];
    uint32_t note_count;
    bool loose;
    uint32_t loose_code_point;
};

BUSTER_UNICODE_ENTRY __attribute__((unused)) void unicode_name_diagnose(const uint8_t* name, uint32_t length, bool literal, UnicodeNameDiagnosis* diagnosis);

BUSTER_UNICODE_ENTRY void unicode_name_diagnose(const uint8_t* name, uint32_t length, bool literal, UnicodeNameDiagnosis* diagnosis)
{
    char escaped[4 * BUSTER_UNICODE_NAME_TEXT - 64];
    unicode_escape(name, length, false, escaped, sizeof(escaped));
    snprintf(diagnosis->error, sizeof(diagnosis->error), "'%s' is not a valid Unicode character name", escaped);
    diagnosis->note_count = 0;
    diagnosis->loose_code_point = 0;
    diagnosis->loose = unicode_name_loose(name, length, &diagnosis->loose_code_point);
    uint32_t illegal = length;
    for (uint32_t i = 0; literal && i < length && illegal == length; i += 1)
    {
        const uint8_t c = name[i];
        illegal = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ' ' ? illegal : i;
    }
    if (illegal != length)
    {
        char character[BUSTER_UNICODE_ESCAPE_BYTES + 8];
        unicode_escape(name + illegal, length - illegal, true, character, sizeof(character));
        UnicodeNameNote* note = &diagnosis->notes[diagnosis->note_count++];
        note->offset = illegal;
        snprintf(note->message, sizeof(note->message), "character %s cannot appear in a Unicode character name", character);
    }
    if (diagnosis->loose)
    {
        UnicodeNameNote* note = &diagnosis->notes[diagnosis->note_count++];
        note->offset = 0;
        snprintf(note->message, sizeof(note->message), "character names in Unicode escape sequences are sensitive to case and whitespaces");
    }
    else if (literal && illegal == length)
    {
        UnicodeNameMatch matches[BUSTER_UNICODE_NAME_MATCHES];
        const uint32_t count = unicode_name_nearest(name, length, matches);
        uint32_t distance = 0;
        bool listing = true;
        for (uint32_t m = 0; listing && m < count; m += 1)
        {
            distance = distance == 0 ? matches[m].distance : distance;
            const uint32_t gap = distance > matches[m].distance ? distance - matches[m].distance : matches[m].distance - distance;
            listing = gap <= 3;
            if (listing)
            {
                distance = matches[m].distance;
                char character[BUSTER_UNICODE_ESCAPE_BYTES + 8];
                unicode_escape_code_point(matches[m].code_point, character, sizeof(character));
                UnicodeNameNote* note = &diagnosis->notes[diagnosis->note_count++];
                note->offset = 0;
                snprintf(note->message, sizeof(note->message), "did you mean %s (%s)?", matches[m].name, character);
            }
        }
    }
}

#pragma clang diagnostic pop
