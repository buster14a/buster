// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original generator of src/buster/compiler/c_unicode_names.inc (#1447): the Unicode character names a named universal
// character name (\N{NAME}) takes, as clang 23 reads them (LLVM's llvm::sys::unicode::nameToCodepointStrict), and the
// characters clang's diagnostics print as printable. Its inputs are two files of the Unicode Character Database,
// UnicodeData.txt and NameAliases.txt, of one Unicode version. Its output is derived Unicode data and keeps their terms
// (the Unicode License V3, LICENSES/Unicode-3.0.txt), which its header says; the inputs' identity, which the header
// records, is the caller's: see THIRD_PARTY_NOTICES.md for the recorded run.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_unicode_names.c -o build/generate_unicode_names
//   build/generate_unicode_names --self-test
//   build/generate_unicode_names UnicodeData.txt NameAliases.txt VERSION "SOURCE" > OUTPUT
//
// What it takes, as LLVM's generator (llvm/utils/UnicodeData/UnicodeNameMappingGenerator.cpp) takes it into its
// dictionary: every name of UnicodeData.txt but the <...> ones, and every alias of NameAliases.txt, of any type, each
// written with capital letters, digits, spaces, hyphens and underscores. The names that Unicode makes by rule from the
// code point (Table 4-8 of the Unicode Standard, "NAME-XXXX") are not stored one by one: a run of consecutive characters
// whose UnicodeData.txt names are a prefix ending in a hyphen and their own code point in four to six capital hexadecimal
// digits is a "listed" range (a name of the dictionary, which clang's suggestions name), and each <..., First> and
// <..., Last> pair whose label Unicode names by rule is an unlisted one: the CJK unified ideographs, the Tangut
// ideographs, the seal and the Jurchen characters. The Hangul syllables' pair must be U+AC00 to U+D7A3, whose names the
// reader makes by Unicode's algorithm. Any other pair but the surrogates' and private use's fails the generator, so a
// new version's rule is reviewed before it is taken.
//
// The stored names are sorted by their loose keys (UAX #44's LM2: no spaces, underscores or medial hyphens, uppercased),
// then by their bytes, and front-coded in buckets of BUCKET (a binary search finds a name's bucket by its first name,
// written whole, whether it is looked up exactly or loosely): each name is the length of the prefix it shares with the one before, its
// code point's difference from the one before (zigzag, seven bits a byte, low first), and the rest of its name as bytes
// up to a 0. A byte of the name's alphabet (space, hyphen, digits and capital letters) is itself; each other byte but 0
// stands for one of the pieces of names (a word, with or without the space before it) that save the most bytes.
//
// It succeeds (exit status 0) only when it read every line of both inputs, took every name of the alphabet, refused no
// range and wrote and flushed all of its output. --self-test checks that on a synthetic input, a write to /dev/full, an
// input that can't be read and a malformed line. Write to a temporary file and replace the checked-in table only after
// the generator succeeds.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUCKET 16
#define CODE_POINTS 0x110000u
#define LONGEST_NAME 128
#define PIECES 4096

typedef struct Name Name;
struct Name
{
    char text[LONGEST_NAME];
    uint32_t code_point;
};

typedef struct Range Range;
struct Range
{
    char prefix[LONGEST_NAME];
    uint32_t first;
    uint32_t last;
    bool listed;
};

typedef struct Piece Piece;
struct Piece
{
    char text[LONGEST_NAME];
    uint64_t saving;
};

typedef struct Table Table;
struct Table
{
    Name* names;
    uint32_t name_count;
    uint32_t name_capacity;
    Range ranges[64];
    uint32_t range_count;
    // The general category's printable characters (L, M, N, P, S and Zs, as LLVM's isPrintable takes them) and the
    // formatting ones (Cf, its isFormatting).
    uint8_t* printable;
    uint8_t* formatting;
    // The open <..., First> pair: its label and first code point.
    char open_label[LONGEST_NAME];
    uint32_t open_first;
    bool open;
    bool valid;
};

static bool is_alphabet(int c)
{
    return c == ' ' || c == '-' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

static bool is_alphanumeric(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// A name's characters LLVM's generator takes ("Letters"): the alphabet and underscores.
static bool is_name(const char* name)
{
    bool result = name[0] != 0;
    for (const char* c = name; result && *c != 0; c += 1)
    {
        result = is_alphabet(*c) || *c == '_';
    }
    return result;
}

// (A name an alias repeats for its own character, which LLVM's generator takes once, is taken out once sorted.)
static void add_name(Table* table, const char* name, uint32_t code_point)
{
    if (table->name_count == table->name_capacity)
    {
        table->name_capacity = table->name_capacity == 0 ? 1024 : table->name_capacity * 2;
        Name* grown = realloc(table->names, (size_t)table->name_capacity * sizeof(Name));
        table->valid &= grown != NULL;
        table->names = grown != NULL ? grown : table->names;
        table->name_capacity = grown != NULL ? table->name_capacity : table->name_count;
    }
    if (table->name_count < table->name_capacity && strlen(name) < LONGEST_NAME)
    {
        snprintf(table->names[table->name_count].text, LONGEST_NAME, "%s", name);
        table->names[table->name_count].code_point = code_point;
        table->name_count += 1;
    }
}

// The prefix of the names a <..., First> pair's label gives by rule, or NULL: "" for the Hangul syllables', whose names
// the reader makes, and for the code points that have no names.
static const char* range_prefix(const char* label)
{
    static const struct
    {
        const char* label;
        const char* prefix;
    } rules[] = {
        { "CJK Ideograph", "CJK UNIFIED IDEOGRAPH-" },
        { "Tangut Ideograph", "TANGUT IDEOGRAPH-" },
        { "Seal Character", "SEAL CHARACTER-" },
        { "Jurchen Character", "JURCHEN CHARACTER-" },
        { "Hangul Syllable", "" },
        { "Non Private Use High Surrogate", "" },
        { "Private Use High Surrogate", "" },
        { "Low Surrogate", "" },
        { "Private Use", "" },
        { "Plane 15 Private Use", "" },
        { "Plane 16 Private Use", "" },
    };
    const char* result = NULL;
    for (uint32_t i = 0; result == NULL && i < sizeof(rules) / sizeof(rules[0]); i += 1)
    {
        const size_t length = strlen(rules[i].label);
        // "CJK Ideograph Extension A" is a CJK ideograph's, "Tangut Ideograph Supplement" a Tangut one's.
        const bool extended = rules[i].prefix[0] != 0 && strncmp(label, rules[i].label, length) == 0 && label[length] == ' ';
        result = strcmp(label, rules[i].label) == 0 || extended ? rules[i].prefix : NULL;
    }
    return result;
}

// One line of UnicodeData.txt: its name, its range's ends and its category's printability. Whether it parsed.
static bool read_character(Table* table, char* line)
{
    char* fields[4] = { line, NULL, NULL, NULL };
    for (uint32_t f = 1; f < 4 && fields[f - 1] != NULL; f += 1)
    {
        char* semicolon = strchr(fields[f - 1], ';');
        fields[f] = semicolon != NULL ? semicolon + 1 : NULL;
        if (semicolon != NULL)
        {
            *semicolon = 0;
        }
    }
    char* end = NULL;
    const unsigned long code_point = strtoul(fields[0], &end, 16);
    bool valid = fields[3] != NULL && end != fields[0] && *end == 0 && code_point < CODE_POINTS && strlen(fields[2]) == 2;
    const char* name = valid ? fields[1] : "";
    const char* category = valid ? fields[2] : "Cn";
    const size_t length = strlen(name);
    const bool first = valid && name[0] == '<' && length > 8 && strcmp(name + length - 8, ", First>") == 0;
    const bool last = valid && name[0] == '<' && length > 7 && strcmp(name + length - 7, ", Last>") == 0;
    uint32_t low = (uint32_t)code_point;
    if (first)
    {
        valid = !table->open && length - 9 < LONGEST_NAME;
        snprintf(table->open_label, LONGEST_NAME, "%.*s", (int)(length - 9), name + 1);
        table->open_first = (uint32_t)code_point;
        table->open = true;
    }
    else if (last)
    {
        const char* prefix = range_prefix(table->open_label);
        valid = valid && table->open && length - 8 == strlen(table->open_label) && strncmp(name + 1, table->open_label, length - 8) == 0 && prefix != NULL &&
                table->range_count < sizeof(table->ranges) / sizeof(table->ranges[0]);
        if (!valid)
        {
            fprintf(stderr, "generate_unicode_names: a range no rule names: <%s>\n", table->open_label);
        }
        // The Hangul syllables are the reader's algorithm's, which must cover them all.
        valid = valid && (strcmp(table->open_label, "Hangul Syllable") != 0 || (table->open_first == 0xAC00 && code_point == 0xD7A3));
        if (valid && prefix[0] != 0)
        {
            Range* range = &table->ranges[table->range_count++];
            snprintf(range->prefix, LONGEST_NAME, "%s", prefix);
            range->first = table->open_first;
            range->last = (uint32_t)code_point;
            range->listed = false;
        }
        low = table->open_first;
        table->open = false;
    }
    else if (valid && name[0] != '<' && is_name(name))
    {
        add_name(table, name, (uint32_t)code_point);
    }
    else if (valid && name[0] != '<')
    {
        fprintf(stderr, "generate_unicode_names: a name of other characters: %s\n", name);
        valid = false;
    }
    // A <First> line's category waits for its <Last> line's, whose range it is.
    const bool printable = category[0] == 'L' || category[0] == 'M' || category[0] == 'N' || category[0] == 'P' || category[0] == 'S' || strcmp(category, "Zs") == 0;
    for (uint32_t c = low; valid && !first && c <= code_point; c += 1)
    {
        table->printable[c] = printable;
        table->formatting[c] = strcmp(category, "Cf") == 0;
    }
    return valid;
}

// One line of NameAliases.txt: its alias, as a name. Whether it parsed.
static bool read_alias(Table* table, char* line)
{
    char* name = strchr(line, ';');
    char* type = name != NULL ? strchr(name + 1, ';') : NULL;
    char* end = NULL;
    const unsigned long code_point = strtoul(line, &end, 16);
    bool valid = type != NULL && end == name && code_point < CODE_POINTS;
    if (valid)
    {
        *type = 0;
        valid = is_name(name + 1);
        add_name(table, name + 1, (uint32_t)code_point);
    }
    return valid;
}

// Every line of `file` that starts with a hexadecimal digit, read by `read`: whether every one parsed and the file was
// read whole.
static bool read_lines(Table* table, FILE* file, bool (*read)(Table*, char*))
{
    char line[1024];
    char parsed[1024];
    bool valid = true;
    while (valid && fgets(line, sizeof(line), file) != NULL)
    {
        const size_t length = strlen(line);
        valid = length != 0 && line[length - 1] == '\n';
        line[length - (valid ? 1 : 0)] = 0;
        memcpy(parsed, line, sizeof(parsed));
        const bool data = (line[0] >= '0' && line[0] <= '9') || (line[0] >= 'A' && line[0] <= 'F');
        valid = valid && (!data || read(table, parsed));
        if (!valid)
        {
            fprintf(stderr, "generate_unicode_names: can't read the line: %s\n", line);
        }
    }
    const bool read_whole = !ferror(file);
    if (!read_whole)
    {
        fprintf(stderr, "generate_unicode_names: can't read an input\n");
    }
    return valid && read_whole;
}

// Whether `name` is a name made by rule of its own code point: a prefix ending in a hyphen, then the code point in four
// to six capital hexadecimal digits. Its prefix's length is `*prefix`.
static bool is_rule_name(const Name* name, size_t* prefix)
{
    char digits[16];
    snprintf(digits, sizeof(digits), "%04X", name->code_point);
    const size_t length = strlen(name->text);
    const size_t count = strlen(digits);
    *prefix = length > count ? length - count : 0;
    return *prefix > 1 && name->text[*prefix - 1] == '-' && strcmp(name->text + *prefix, digits) == 0;
}

// A name's loose key (UAX #44's LM2, as LLVM's loose matching compares names): its bytes but spaces, underscores and
// medial hyphens (a hyphen between two letters or digits), uppercased.
static void loose_key(const char* name, char* key)
{
    size_t count = 0;
    for (size_t i = 0; name[i] != 0; i += 1)
    {
        const char c = name[i];
        const bool medial = c == '-' && i != 0 && is_alphanumeric(name[i - 1]) && is_alphanumeric(name[i + 1]);
        if (c != ' ' && c != '_' && !medial)
        {
            key[count++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
        }
    }
    key[count] = 0;
}

// The stored names' order: by loose key, then by bytes, so that a binary search finds a name exactly and loosely.
static int compare_names(const void* a, const void* b)
{
    char x[LONGEST_NAME];
    char y[LONGEST_NAME];
    loose_key(((const Name*)a)->text, x);
    loose_key(((const Name*)b)->text, y);
    const int order = strcmp(x, y);
    return order != 0 ? order : strcmp(((const Name*)a)->text, ((const Name*)b)->text);
}

static int compare_code_points(const void* a, const void* b)
{
    const Name* x = a;
    const Name* y = b;
    return x->code_point != y->code_point ? (x->code_point < y->code_point ? -1 : 1) : strcmp(x->text, y->text);
}

static int compare_ranges(const void* a, const void* b)
{
    const Range* x = a;
    const Range* y = b;
    const int prefix = strcmp(x->prefix, y->prefix);
    return prefix != 0 ? prefix : x->first < y->first ? -1 : x->first > y->first ? 1 : 0;
}

static int compare_pieces(const void* a, const void* b)
{
    const Piece* x = a;
    const Piece* y = b;
    return x->saving != y->saving ? (x->saving > y->saving ? -1 : 1) : strcmp(x->text, y->text);
}

// The UnicodeData.txt names made by rule, taken out of the stored names into listed ranges of consecutive code points.
static void take_rule_names(Table* table, uint32_t data_names)
{
    qsort(table->names, data_names, sizeof(Name), compare_code_points);
    uint32_t kept = 0;
    for (uint32_t i = 0; table->valid && i < table->name_count; i += 1)
    {
        size_t prefix = 0;
        const Name name = table->names[i];
        if (i < data_names && is_rule_name(&name, &prefix))
        {
            Range* last = table->range_count != 0 ? &table->ranges[table->range_count - 1] : NULL;
            const bool extends = last != NULL && last->listed && last->last + 1 == name.code_point && strlen(last->prefix) == prefix &&
                                 strncmp(last->prefix, name.text, prefix) == 0;
            if (extends)
            {
                last->last = name.code_point;
            }
            else if (table->range_count < sizeof(table->ranges) / sizeof(table->ranges[0]))
            {
                Range* range = &table->ranges[table->range_count++];
                snprintf(range->prefix, LONGEST_NAME, "%.*s", (int)prefix, name.text);
                range->first = name.code_point;
                range->last = name.code_point;
                range->listed = true;
            }
            else
            {
                table->valid = false;
            }
        }
        else
        {
            table->names[kept++] = name;
        }
    }
    table->name_count = kept;
}

// Writes `bytes` [0, length) as a C string literal's contents: the alphabet as it is, any other byte as an octal escape.
static void write_bytes(FILE* out, const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; i += 1)
    {
        if (is_alphabet(bytes[i]))
        {
            fputc(bytes[i], out);
        }
        else
        {
            fprintf(out, "\\%03o", bytes[i]);
        }
    }
}

// Appends `value` zigzag-encoded, seven bits a byte, low first, to `out`: its length.
static size_t encode_difference(int64_t value, uint8_t* out)
{
    uint64_t zigzag = value < 0 ? ((uint64_t)(-value) << 1) - 1 : (uint64_t)value << 1;
    size_t length = 0;
    do
    {
        out[length++] = (uint8_t)((zigzag & 0x7F) | (zigzag >= 0x80 ? 0x80 : 0));
        zigzag >>= 7;
    } while (zigzag != 0);
    return length;
}

// The length of the prefix that name `i` shares with the one before it in its bucket.
static size_t shared_prefix(const Table* table, uint32_t i)
{
    size_t shared = 0;
    if (i % BUCKET != 0)
    {
        const char* a = table->names[i - 1].text;
        const char* b = table->names[i].text;
        while (a[shared] != 0 && a[shared] == b[shared])
        {
            shared += 1;
        }
    }
    return shared;
}

// The next piece of `suffix` at `at`: a word with the space before it if there is one, or one other byte. Its length.
static size_t next_piece(const char* suffix, size_t at)
{
    size_t end = at + (suffix[at] == ' ' ? 1 : 0);
    while (is_alphanumeric(suffix[end]))
    {
        end += 1;
    }
    return end > at + (suffix[at] == ' ' ? 1 : 0) ? end - at : 1;
}

// The pieces that save the most bytes, as many as the bytes the alphabet leaves (`codes`, the byte each stands for).
static uint32_t choose_pieces(const Table* table, Piece* pieces, uint8_t* codes)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < table->name_count; i += 1)
    {
        const char* suffix = table->names[i].text + shared_prefix(table, i);
        for (size_t at = 0; suffix[at] != 0;)
        {
            const size_t length = next_piece(suffix, at);
            uint32_t found = count;
            for (uint32_t p = 0; length > 1 && p < count && found == count; p += 1)
            {
                found = strlen(pieces[p].text) == length && strncmp(pieces[p].text, suffix + at, length) == 0 ? p : count;
            }
            if (length > 1 && found == count && count < PIECES)
            {
                snprintf(pieces[count].text, LONGEST_NAME, "%.*s", (int)length, suffix + at);
                pieces[count].saving = 0;
                count += 1;
            }
            if (length > 1 && found < count)
            {
                pieces[found].saving += length - 1;
            }
            at += length;
        }
    }
    qsort(pieces, count, sizeof(Piece), compare_pieces);
    uint32_t chosen = 0;
    for (uint32_t b = 1; b < 256; b += 1)
    {
        if (!is_alphabet((int)b) && chosen < count)
        {
            codes[chosen++] = (uint8_t)b;
        }
    }
    return chosen;
}

// Encodes name `i`'s rest past its shared prefix into `out` with the chosen pieces, then a 0: its length.
static size_t encode_rest(const char* suffix, const Piece* pieces, const uint8_t* codes, uint32_t chosen, uint8_t* out)
{
    size_t length = 0;
    for (size_t at = 0; suffix[at] != 0;)
    {
        const size_t size = next_piece(suffix, at);
        uint32_t found = chosen;
        for (uint32_t p = 0; size > 1 && p < chosen && found == chosen; p += 1)
        {
            found = strlen(pieces[p].text) == size && strncmp(pieces[p].text, suffix + at, size) == 0 ? p : chosen;
        }
        if (found < chosen)
        {
            out[length++] = codes[found];
        }
        else
        {
            memcpy(out + length, suffix + at, size);
            length += size;
        }
        at += size;
    }
    out[length++] = 0;
    return length;
}

// Writes the ranges of code points whose `flags` are set as rows { first, last }.
static void write_code_point_ranges(FILE* out, const char* name, const uint8_t* flags)
{
    fprintf(out, "static const uint32_t %s[][2] = {\n", name);
    uint32_t count = 0;
    for (uint32_t c = 0; c < CODE_POINTS; c += 1)
    {
        if (flags[c] && (c == 0 || !flags[c - 1]))
        {
            uint32_t last = c;
            while (last + 1 < CODE_POINTS && flags[last + 1])
            {
                last += 1;
            }
            fprintf(out, "%s{ 0x%04X, 0x%04X },", count % 6 == 0 ? "    " : " ", c, last);
            count += 1;
            fputs(count % 6 == 0 ? "\n" : "", out);
        }
    }
    fprintf(out, "%s};\n", count % 6 != 0 ? "\n" : "");
}

// The table of the two inputs, written to `out` with its header naming `version` and `source`: whether both inputs were
// read whole, nothing was refused, and every byte of the output was written and flushed.
static bool generate(FILE* data, FILE* aliases, FILE* out, const char* version, const char* source)
{
    Table table = {
        .printable = calloc(CODE_POINTS, 1),
        .formatting = calloc(CODE_POINTS, 1),
        .valid = true,
    };
    table.valid = table.printable != NULL && table.formatting != NULL;
    table.valid = table.valid && read_lines(&table, data, read_character) && !table.open;
    const uint32_t data_names = table.name_count;
    table.valid = table.valid && read_lines(&table, aliases, read_alias);
    // The listed ranges come from UnicodeData.txt's names alone, before the aliases, which keep their names.
    take_rule_names(&table, data_names);
    qsort(table.ranges, table.range_count, sizeof(Range), compare_ranges);
    qsort(table.names, table.name_count, sizeof(Name), compare_names);
    uint32_t unique = 0;
    for (uint32_t i = 0; i < table.name_count; i += 1)
    {
        // (A repeat is next to its name: they share a key.)
        const bool repeated = unique != 0 && strcmp(table.names[unique - 1].text, table.names[i].text) == 0;
        // Two characters of one name: Unicode's names are unique, so the inputs are wrong.
        table.valid &= !repeated || table.names[unique - 1].code_point == table.names[i].code_point;
        table.names[unique] = table.names[i];
        unique += repeated ? 0 : 1;
    }
    table.name_count = unique;
    // The most letters and digits of a name, LLVM's UnicodeNameToCodepointLargestNameSize, which bounds its suggestions'
    // edit distances: of a stored name or a listed range's longest one.
    size_t longest = 0;
    for (uint32_t i = 0; i < table.name_count; i += 1)
    {
        size_t letters = 0;
        for (const char* c = table.names[i].text; *c != 0; c += 1)
        {
            letters += is_alphanumeric(*c);
        }
        longest = letters > longest ? letters : longest;
    }
    for (uint32_t r = 0; r < table.range_count; r += 1)
    {
        size_t letters = table.ranges[r].last > 0xFFFF ? (table.ranges[r].last > 0xFFFFF ? 6 : 5) : 4;
        for (const char* c = table.ranges[r].prefix; *c != 0; c += 1)
        {
            letters += is_alphanumeric(*c);
        }
        longest = table.ranges[r].listed && letters > longest ? letters : longest;
    }
    Piece* pieces = table.valid ? calloc(PIECES, sizeof(Piece)) : NULL;
    uint8_t codes[256] = { 0 };
    const uint32_t chosen = pieces != NULL ? choose_pieces(&table, pieces, codes) : 0;
    table.valid = table.valid && pieces != NULL && table.name_count != 0 && table.range_count != 0;

    fprintf(out, "// SPDX-License-Identifier: Unicode-3.0\n");
    fprintf(out, "// Generated by tools/generate_unicode_names.c; do not edit. Derived Unicode data: Copyright (c) 1991-2026 Unicode,\n");
    fprintf(out, "// Inc., under the Unicode License V3 (LICENSES/Unicode-3.0.txt), from UnicodeData.txt and NameAliases.txt of the\n");
    fprintf(out, "// Unicode Character Database %s, %s;\n", version, source);
    fprintf(out, "// see THIRD_PARTY_NOTICES.md. The names a named universal character name takes (c_unicode_names.c reads them):\n");
    fprintf(out, "// the stored names, sorted and front-coded in buckets of BUSTER_UNICODE_NAME_BUCKET (the generator's header has the\n");
    fprintf(out, "// format), the ranges of names made by rule, and the printable and formatting characters of clang's diagnostics.\n");
    fprintf(out, "#define BUSTER_UNICODE_NAMES_VERSION \"%s\"\n", version);
    fprintf(out, "#define BUSTER_UNICODE_NAME_COUNT %u\n", table.name_count);
    fprintf(out, "#define BUSTER_UNICODE_NAME_BUCKET %u\n", BUCKET);
    fprintf(out, "#define BUSTER_UNICODE_NAME_LONGEST %zu\n\n", longest);
    // The text each byte stands for: a byte of the alphabet itself, a chosen one its piece, 0 and the rest none.
    char text[1 << 16];
    uint32_t text_length = 0;
    uint16_t starts[257];
    for (uint32_t b = 0; b < 256; b += 1)
    {
        starts[b] = (uint16_t)text_length;
        uint32_t piece = chosen;
        for (uint32_t p = 0; p < chosen && piece == chosen; p += 1)
        {
            piece = codes[p] == b ? p : chosen;
        }
        const char single[2] = { (char)b, 0 };
        const char* expansion = is_alphabet((int)b) ? single : piece < chosen ? pieces[piece].text : "";
        const size_t length = strlen(expansion);
        table.valid &= text_length + length < sizeof(text);
        memcpy(text + text_length, expansion, table.valid ? length : 0);
        text_length += table.valid ? (uint32_t)length : 0;
    }
    starts[256] = (uint16_t)text_length;
    fprintf(out, "// Byte b of a name stands for unicode_name_piece_text [unicode_name_piece_starts[b], unicode_name_piece_starts[b + 1]).\n");
    fprintf(out, "static const char unicode_name_piece_text[] =\n    \"");
    for (uint32_t i = 0; i < text_length; i += 1)
    {
        fputc(text[i], out);
        fputs(i + 1 < text_length && (i + 1) % 100 == 0 ? "\"\n    \"" : "", out);
    }
    fprintf(out, "\";\nstatic const uint16_t unicode_name_piece_starts[257] = {\n");
    for (uint32_t b = 0; b <= 256; b += 1)
    {
        fprintf(out, "%s%u,%s", b % 16 == 0 ? "    " : " ", starts[b], b % 16 == 15 || b == 256 ? "\n" : "");
    }
    fprintf(out, "};\n\n");
    // The names' stream, and the offset of each bucket's first name in it.
    uint32_t* buckets = calloc(table.name_count / BUCKET + 1, sizeof(uint32_t));
    uint8_t entry[LONGEST_NAME * 2 + 16];
    uint64_t offset = 0;
    uint32_t previous = 0;
    table.valid = table.valid && buckets != NULL;
    fprintf(out, "static const uint8_t unicode_name_stream[] =\n");
    for (uint32_t i = 0; table.valid && i < table.name_count; i += 1)
    {
        const size_t shared = shared_prefix(&table, i);
        size_t length = 0;
        entry[length++] = (uint8_t)shared;
        length += encode_difference(i % BUCKET == 0 ? (int64_t)table.names[i].code_point : (int64_t)table.names[i].code_point - previous, entry + length);
        length += encode_rest(table.names[i].text + shared, pieces, codes, chosen, entry + length);
        buckets[i / BUCKET] = i % BUCKET == 0 ? (uint32_t)offset : buckets[i / BUCKET];
        fputs(i % 4 == 0 ? "    \"" : "", out);
        write_bytes(out, entry, length);
        fputs(i % 4 == 3 || i + 1 == table.name_count ? "\"\n" : "", out);
        offset += length;
        previous = table.names[i].code_point;
    }
    fprintf(out, "    ;\n// %llu bytes.\n", (unsigned long long)offset);
    fprintf(out, "static const uint32_t unicode_name_buckets[%u] = {\n", (table.name_count + BUCKET - 1) / BUCKET);
    for (uint32_t b = 0; table.valid && b < (table.name_count + BUCKET - 1) / BUCKET; b += 1)
    {
        fprintf(out, "%s%u,%s", b % 10 == 0 ? "    " : " ", buckets[b], b % 10 == 9 || b + 1 == (table.name_count + BUCKET - 1) / BUCKET ? "\n" : "");
    }
    fprintf(out, "};\n\n");
    fprintf(out, "// The names made by rule: a prefix, then the code point in four to six hexadecimal digits, from `first` to `last`;\n");
    fprintf(out, "// `listed` where UnicodeData.txt lists each by name (clang's suggestions name them).\n");
    fprintf(out, "static const UnicodeNameRange unicode_name_ranges[%u] = {\n", table.range_count);
    for (uint32_t r = 0; r < table.range_count; r += 1)
    {
        fprintf(out, "    { \"%s\", 0x%04X, 0x%04X, %s },\n", table.ranges[r].prefix, table.ranges[r].first, table.ranges[r].last, table.ranges[r].listed ? "true" : "false");
    }
    fprintf(out, "};\n\n");
    fprintf(out, "// The printable characters (general categories L, M, N, P, S and Zs) and the formatting ones (Cf).\n");
    write_code_point_ranges(out, "unicode_printable_ranges", table.printable);
    write_code_point_ranges(out, "unicode_formatting_ranges", table.formatting);
    const bool written = fflush(out) == 0 && !ferror(out);
    if (!written)
    {
        fprintf(stderr, "generate_unicode_names: can't write the table\n");
    }
    if (table.valid && written && out != stdout)
    {
        // (The self-test's outputs are quiet.)
    }
    else if (table.valid && written)
    {
        fprintf(stderr, "generate_unicode_names: %u names, %u ranges, %u pieces, %llu bytes of names, longest %zu\n", table.name_count, table.range_count, chosen,
                (unsigned long long)offset, longest);
    }
    free(buckets);
    free(pieces);
    free(table.names);
    free(table.printable);
    free(table.formatting);
    return table.valid && written;
}

// A synthetic input of `text` in a temporary file, or NULL.
static FILE* synthetic_input(const char* text)
{
    FILE* file = tmpfile();
    const bool written = file != NULL && fputs(text, file) >= 0 && fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0;
    if (file != NULL && !written)
    {
        fclose(file);
        file = NULL;
    }
    return file;
}

// Whether generating `data` and `aliases` (texts) succeeds, written to `out` (or a temporary file); its output in
// `text` when it is given.
static bool self_test_generate(const char* data, const char* aliases, FILE* out, char* text, size_t capacity)
{
    FILE* data_file = synthetic_input(data);
    FILE* alias_file = synthetic_input(aliases);
    FILE* sink = out != NULL ? out : tmpfile();
    const bool result = data_file != NULL && alias_file != NULL && sink != NULL && generate(data_file, alias_file, sink, "0.0.0", "self-test");
    if (text != NULL && sink != NULL && fseek(sink, 0, SEEK_SET) == 0)
    {
        text[fread(text, 1, capacity - 1, sink)] = 0;
    }
    FILE* files[] = { data_file, alias_file, out != NULL ? NULL : sink };
    for (uint32_t f = 0; f < sizeof(files) / sizeof(files[0]); f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    return result;
}

// The self-test: a synthetic input's table, then the failures it must report. Returns the exit status.
static int self_test(void)
{
    static const char data[] =
        "0000;<control>;Cc;0;BN;;;;;N;NULL;;;;\n"
        "0041;LATIN CAPITAL LETTER A;Lu;0;L;;;;;N;;;;0061;\n"
        "0042;LATIN CAPITAL LETTER B;Lu;0;L;;;;;N;;;;0062;\n"
        "00AD;SOFT HYPHEN;Cf;0;BN;;;;;N;;;;;\n"
        "3400;<CJK Ideograph Extension A, First>;Lo;0;L;;;;;N;;;;;\n"
        "4DBF;<CJK Ideograph Extension A, Last>;Lo;0;L;;;;;N;;;;;\n"
        "AC00;<Hangul Syllable, First>;Lo;0;L;;;;;N;;;;;\n"
        "D7A3;<Hangul Syllable, Last>;Lo;0;L;;;;;N;;;;;\n"
        "F900;CJK COMPATIBILITY IDEOGRAPH-F900;Lo;0;L;8C48;;;;N;;;;;\n"
        "F901;CJK COMPATIBILITY IDEOGRAPH-F901;Lo;0;L;66F4;;;;N;;;;;\n";
    static const char aliases[] = "# a comment\n0000;NULL;control\n0000;NUL;abbreviation\n";
    static const char* const expected[] = {
        "#define BUSTER_UNICODE_NAME_COUNT 5\n",
        "{ \"CJK COMPATIBILITY IDEOGRAPH-\", 0xF900, 0xF901, true },",
        "{ \"CJK UNIFIED IDEOGRAPH-\", 0x3400, 0x4DBF, false },",
        "{ 0x0041, 0x0042 }, { 0x3400, 0x4DBF }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xF901 },",
        "unicode_formatting_ranges[][2] = {\n    { 0x00AD, 0x00AD },",
    };
    uint32_t failures = 0;
    char text[1 << 16];
    const bool generated = self_test_generate(data, aliases, NULL, text, sizeof(text));
    for (uint32_t e = 0; e < sizeof(expected) / sizeof(expected[0]); e += 1)
    {
        const bool found = generated && strstr(text, expected[e]) != NULL;
        failures += !found;
        if (!found)
        {
            fprintf(stderr, "generate_unicode_names: self-test: the synthetic table lacks: %s\n", expected[e]);
        }
    }
    // A full disk: the output's write or flush fails, and so must the generator.
    FILE* full = fopen("/dev/full", "w");
    const bool full_refused = full != NULL && !self_test_generate(data, aliases, full, NULL, 0);
    failures += !full_refused;
    if (!full_refused)
    {
        fprintf(stderr, "generate_unicode_names: self-test: a write to /dev/full succeeded\n");
    }
    // An input that can't be read: a directory, whose read fails (ferror).
    FILE* directory = fopen("/", "rb");
    FILE* alias_file = synthetic_input(aliases);
    FILE* sink = tmpfile();
    const bool unread_refused = directory != NULL && alias_file != NULL && sink != NULL && !generate(directory, alias_file, sink, "0.0.0", "self-test");
    failures += !unread_refused;
    if (!unread_refused)
    {
        fprintf(stderr, "generate_unicode_names: self-test: an unreadable input succeeded\n");
    }
    // Malformed lines: a name of other characters, a range of no rule, a line without its fields.
    static const char* const malformed[] = {
        "0041;LATIN CAPITAL LETTER a;Lu;0;L;;;;;N;;;;0061;\n",
        "E0000;<Some Range, First>;Lo;0;L;;;;;N;;;;;\nE0010;<Some Range, Last>;Lo;0;L;;;;;N;;;;;\n",
        "0041;LATIN CAPITAL LETTER A\n",
    };
    for (uint32_t m = 0; m < sizeof(malformed) / sizeof(malformed[0]); m += 1)
    {
        const bool refused = !self_test_generate(malformed[m], aliases, NULL, NULL, 0);
        failures += !refused;
        if (!refused)
        {
            fprintf(stderr, "generate_unicode_names: self-test: a malformed input succeeded: %s", malformed[m]);
        }
    }
    FILE* files[] = { full, directory, alias_file, sink };
    for (uint32_t f = 0; f < sizeof(files) / sizeof(files[0]); f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    const uint32_t checks = (uint32_t)(sizeof(expected) / sizeof(expected[0]) + 2 + sizeof(malformed) / sizeof(malformed[0]));
    fprintf(stderr, "generate_unicode_names: self-test: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    const bool testing = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    FILE* data = !testing && argc == 5 ? fopen(argv[1], "rb") : NULL;
    FILE* aliases = !testing && argc == 5 ? fopen(argv[2], "rb") : NULL;
    int status = 1;
    if (testing)
    {
        status = self_test();
    }
    else if (data == NULL || aliases == NULL)
    {
        fprintf(stderr, "usage: generate_unicode_names UnicodeData.txt NameAliases.txt VERSION SOURCE, or generate_unicode_names --self-test\n");
    }
    else
    {
        status = generate(data, aliases, stdout, argv[3], argv[4]) ? 0 : 1;
    }
    if (data != NULL)
    {
        fclose(data);
    }
    if (aliases != NULL)
    {
        fclose(aliases);
    }
    return status;
}
