// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin.
// Original bounded extractor; no LLVM implementation code is incorporated.
//
// Input contract: the four approved-host Clang 23.1.1 files, byte-for-byte
// identified below. Files are at most 4 MiB, contain no raw NUL, and remain
// unchanged while read. No compiler, TableGen, shell or external process runs.
// Output: deterministic name-sorted TSV by default, or C macro rows with
// --format c. Both retain exact field bytes through format-specific escaping.
// All inputs, offsets, enum/name joins and duplicates are checked before any
// stdout is written. A stdout I/O failure can leave a partial output: consumers
// must require exit status zero and retain the complete provenance header.
//
// This is DATA extraction, not builtin implementation or semantic validation.
// Type/attribute/feature encodings remain exact opaque bytes. In particular a
// custom-typechecking ('t') record is NOT an ordinary function prototype, 'E'
// does not promise C ICE acceptance, and an 'I' type modifier alone does not
// supply immediate ranges, allowed sets, operand relations or feature closure.
// All semantic_constraints fields deliberately say unmodeled.
//
// Primary sources, all at llvmorg-23.1.1 in llvm/llvm-project:
// clang/utils/TableGen/ClangBuiltinsEmitter.cpp: emitted sections/offsets.
// clang/include/clang/Basic/CMakeLists.txt: -gen-clang-builtins inputs.
// clang/include/clang/Basic/{Builtins,BuiltinsBase,BuiltinsX86Base}.td:
//   attributes, template expansion and required spelling prefixes.
// clang/lib/Basic/Targets/X86.cpp: common/64-bit shards and __builtin_ia32_.
// clang/lib/AST/ASTContext.cpp, DecodeTypeFromStr/GetBuiltinType: type grammar.
//   The first encoded type is the result, followed by parameters; '.' denotes
//   varargs, 'I' marks an integer constant argument, V<count><type> is a vector.
//   Qualifiers, pointers, target-sized integers, references, complex/extended
//   types and target/context-dependent forms must be decoded by the consumer.
// clang/lib/Basic/BuiltinTargetFeatures.h: ',' AND, '|' OR, parentheses.
// clang/lib/Sema/SemaX86.cpp and clang/lib/CodeGen/CodeGenFunction.cpp:
//   additional immediate, architecture and value-dependent feature checks.
// Prefix those paths with:
// https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/
//
// Generated output is derived LLVM data, Apache-2.0 WITH LLVM-exception. Before
// importing it into a repository, retain LLVM license/attribution notices and
// record exact source tag/commit, all four input digests, installed generator
// provenance (or the approved-host package receipt), this extractor revision,
// invocation and output digest in THIRD_PARTY_NOTICES and an input manifest.
// The input files are generated data, not new first-party builtin declarations.
// Handwritten Sema/CodeGen constraints need their own bounded, reviewed manifest.
//
// Usage: clang-builtin-extract --builtins Builtins.inc --x86 BuiltinsX86.inc
//          --x86-64 BuiltinsX86_64.inc --version Version.inc [--names names.txt]
//          [--format tsv|c]
// Optional names: nonempty identifier lines, no blanks or duplicates; CRLF is
// accepted. Every name must resolve or match an explicitly classified special
// form/predefined type below. Missing table records never acquire a prototype.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUSTER_EXTRACT_MAX_INPUT (4u * 1024u * 1024u)
#define BUSTER_EXTRACT_MAX_STRINGS (1024u * 1024u)
#define BUSTER_EXTRACT_MAX_ROWS 16384u
#define BUSTER_EXTRACT_MAX_NAME 1024u

typedef enum OutputFormat
{
    OUTPUT_FORMAT_TSV,
    OUTPUT_FORMAT_C
} OutputFormat;

typedef struct
{
    const unsigned char *data;
    size_t length;
} Slice;

typedef struct
{
    Slice input;
    size_t position;
    size_t line;
    bool ok;
} Reader;

typedef struct
{
    Slice text;
    size_t line;
} Token;

typedef struct
{
    char name[BUSTER_EXTRACT_MAX_NAME];
    const char *type;
    const char *attributes;
    const char *features;
    Slice header;
    Slice language;
    const char *shard;
    const char *record_kind;
    size_t source_line;
    size_t source_ordinal;
} Row;

typedef struct
{
    const char *path;
    const char *label;
    const char *expected_digest;
    char actual_digest[65];
    unsigned char *input;
    size_t input_length;
    unsigned char *strings;
    size_t string_length;
    Slice *enumerators;
    size_t enumerator_count;
    Row *rows;
    size_t row_count;
} Table;

static uint32_t rotate_right(uint32_t value, unsigned shift)
{
    uint32_t result = (value >> shift) | (value << (32u - shift));
    return result;
}

static void sha256_block(uint32_t state[8], const unsigned char block[64])
{
    // FIPS 180-4 SHA-256 round constants; original implementation of the spec.
    static const uint32_t constants[64] =
    {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i)
    {
        words[i] = ((uint32_t)block[4u * i] << 24) | ((uint32_t)block[4u * i + 1u] << 16) |
                   ((uint32_t)block[4u * i + 2u] << 8) | (uint32_t)block[4u * i + 3u];
    }
    for (unsigned i = 16; i < 64; ++i)
    {
        uint32_t left = words[i - 15u];
        uint32_t right = words[i - 2u];
        uint32_t sigma0 = rotate_right(left, 7) ^ rotate_right(left, 18) ^ (left >> 3);
        uint32_t sigma1 = rotate_right(right, 17) ^ rotate_right(right, 19) ^ (right >> 10);
        words[i] = words[i - 16u] + sigma0 + words[i - 7u] + sigma1;
    }
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + sigma1 + choose + constants[i] + words[i];
        uint32_t sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = sigma0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_hex(Slice input, char result[65])
{
    uint32_t state[8] =
    {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    size_t position = 0;
    while (input.length - position >= 64)
    {
        sha256_block(state, input.data + position);
        position += 64;
    }
    unsigned char tail[128] = {0};
    size_t remaining = input.length - position;
    memcpy(tail, input.data + position, remaining);
    tail[remaining] = 0x80;
    size_t tail_length = remaining < 56 ? 64 : 128;
    uint64_t bit_length = (uint64_t)input.length * 8u;
    for (unsigned i = 0; i < 8; ++i)
    {
        tail[tail_length - 1u - i] = (unsigned char)(bit_length >> (8u * i));
    }
    sha256_block(state, tail);
    if (tail_length == 128)
    {
        sha256_block(state, tail + 64);
    }
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
    {
        unsigned byte = (state[i / 4u] >> (24u - 8u * (i % 4u))) & 255u;
        result[2u * i] = hex[byte >> 4];
        result[2u * i + 1u] = hex[byte & 15u];
    }
    result[64] = 0;
}

static bool slice_equal(Slice value, const char *text)
{
    size_t length = strlen(text);
    bool result = value.length == length && (length == 0 || memcmp(value.data, text, length) == 0);
    return result;
}

static bool load_table(Table *table)
{
    bool ok = false;
    FILE *file = fopen(table->path, "rb");
    if (file != NULL)
    {
        table->input = malloc(BUSTER_EXTRACT_MAX_INPUT + 1u);
        if (table->input != NULL)
        {
            table->input_length = fread(table->input, 1, BUSTER_EXTRACT_MAX_INPUT + 1u, file);
            ok = !ferror(file) && table->input_length <= BUSTER_EXTRACT_MAX_INPUT &&
                 memchr(table->input, 0, table->input_length) == NULL;
        }
        if (fclose(file) != 0)
        {
            ok = false;
        }
    }
    if (ok)
    {
        sha256_hex((Slice){table->input, table->input_length}, table->actual_digest);
        ok = table->expected_digest == NULL || strcmp(table->actual_digest, table->expected_digest) == 0;
        if (!ok)
        {
            fprintf(stderr, "%s: SHA256 mismatch (actual %s)\n", table->path, table->actual_digest);
        }
    }
    else
    {
        fprintf(stderr, "%s: cannot read a bounded NUL-free input\n", table->path);
    }
    return ok;
}

static Slice section(Table *table, const char *name, size_t *first_line, bool *ok)
{
    Slice result = {0};
    char opening[96];
    char closing[96];
    int open_length = snprintf(opening, sizeof(opening), "#ifdef %s", name);
    int close_length = snprintf(closing, sizeof(closing), "#endif // %s", name);
    bool bounded = open_length > 0 && (size_t)open_length < sizeof(opening) &&
                   close_length > 0 && (size_t)close_length < sizeof(closing);
    bool found_open = false;
    bool found_close = false;
    size_t position = 0;
    size_t line = 1;
    while (bounded && position < table->input_length)
    {
        size_t start = position;
        while (position < table->input_length && table->input[position] != '\n')
        {
            ++position;
        }
        Slice current = {table->input + start, position - start};
        if (slice_equal(current, opening))
        {
            bounded = !found_open;
            found_open = true;
            result.data = table->input + position + (position < table->input_length);
            *first_line = line + 1;
        }
        if (slice_equal(current, closing))
        {
            bounded = bounded && found_open && !found_close;
            found_close = true;
            if (bounded)
            {
                result.length = (size_t)(table->input + start - result.data);
            }
        }
        position += position < table->input_length;
        ++line;
    }
    *ok = *ok && bounded && found_open && found_close;
    return result;
}

static bool is_identifier(unsigned char byte)
{
    bool result = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                  (byte >= '0' && byte <= '9') || byte == '_';
    return result;
}

static void skip_space(Reader *reader)
{
    bool keep = true;
    while (reader->ok && keep && reader->position < reader->input.length)
    {
        size_t position = reader->position;
        unsigned char byte = reader->input.data[position];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\f' || byte == '\v')
        {
            reader->line += byte == '\n';
            ++reader->position;
        }
        else if (byte == '/' && reader->input.length - position >= 2 && reader->input.data[position + 1] == '/')
        {
            reader->position += 2;
            while (reader->position < reader->input.length && reader->input.data[reader->position] != '\n')
            {
                ++reader->position;
            }
        }
        else if (byte == '/' && reader->input.length - position >= 2 && reader->input.data[position + 1] == '*')
        {
            reader->position += 2;
            bool closed = false;
            while (!closed && reader->position < reader->input.length)
            {
                position = reader->position;
                closed = reader->input.length - position >= 2 && reader->input.data[position] == '*' &&
                         reader->input.data[position + 1] == '/';
                reader->line += reader->input.data[position] == '\n';
                reader->position += closed ? 2 : 1;
            }
            reader->ok = closed;
        }
        else
        {
            keep = false;
        }
    }
}

static Token next_token(Reader *reader)
{
    skip_space(reader);
    const unsigned char *data = reader->input.data;
    if (reader->position != 0)
    {
        data += reader->position;
    }
    Token result = {{data, 0}, reader->line};
    if (reader->ok && reader->position < reader->input.length)
    {
        size_t start = reader->position++;
        unsigned char byte = reader->input.data[start];
        if (is_identifier(byte))
        {
            while (reader->position < reader->input.length && is_identifier(reader->input.data[reader->position]))
            {
                ++reader->position;
            }
        }
        else if (byte == '"')
        {
            bool closed = false;
            while (!closed && reader->ok && reader->position < reader->input.length)
            {
                byte = reader->input.data[reader->position++];
                closed = byte == '"';
                reader->ok = byte != '\n' && byte != '\r';
                if (byte == '\\')
                {
                    reader->ok = reader->position < reader->input.length;
                    if (reader->ok)
                    {
                        byte = reader->input.data[reader->position++];
                        reader->ok = byte != '\n' && byte != '\r';
                    }
                }
            }
            reader->ok = reader->ok && closed;
        }
        result.text.length = reader->position - start;
    }
    return result;
}

static Token expect_token(Reader *reader, const char *text)
{
    Token token = next_token(reader);
    reader->ok = reader->ok && slice_equal(token.text, text);
    return token;
}

static bool parse_offset(Reader *reader, size_t *result)
{
    Token token = next_token(reader);
    bool ok = token.text.length != 0;
    size_t value = 0;
    for (size_t i = 0; ok && i < token.text.length; ++i)
    {
        unsigned char byte = token.text.data[i];
        ok = byte >= '0' && byte <= '9';
        unsigned digit = ok ? (unsigned)(byte - '0') : 0;
        ok = ok && value <= (BUSTER_EXTRACT_MAX_STRINGS - digit) / 10u;
        if (ok)
        {
            value = value * 10u + digit;
        }
    }
    reader->ok = reader->ok && ok;
    *result = value;
    return reader->ok;
}

static int hex_digit(unsigned char byte)
{
    int result = -1;
    if (byte >= '0' && byte <= '9')
    {
        result = byte - '0';
    }
    else if (byte >= 'a' && byte <= 'f')
    {
        result = byte - 'a' + 10;
    }
    else if (byte >= 'A' && byte <= 'F')
    {
        result = byte - 'A' + 10;
    }
    return result;
}

static bool append_literal(Table *table, Slice literal)
{
    bool ok = literal.length >= 2 && literal.data[0] == '"' && literal.data[literal.length - 1] == '"';
    size_t position = 1;
    while (ok && position < literal.length - 1)
    {
        unsigned value = literal.data[position++];
        if (value == '\\')
        {
            ok = position < literal.length - 1;
            if (ok)
            {
                unsigned char escape = literal.data[position++];
                value = escape;
                if (escape >= '0' && escape <= '7')
                {
                    value = escape - '0';
                    unsigned digits = 1;
                    while (digits < 3 && position < literal.length - 1 && literal.data[position] >= '0' && literal.data[position] <= '7')
                    {
                        value = value * 8u + (unsigned)(literal.data[position++] - '0');
                        ++digits;
                    }
                }
                else if (escape == 'x')
                {
                    value = 0;
                    size_t digits = 0;
                    while (ok && position < literal.length - 1 && hex_digit(literal.data[position]) >= 0)
                    {
                        unsigned digit = (unsigned)hex_digit(literal.data[position++]);
                        ok = value <= (255u - digit) / 16u;
                        value = ok ? value * 16u + digit : 0;
                        ++digits;
                    }
                    ok = ok && digits != 0;
                }
                else if (escape == 'a')
                {
                    value = 7;
                }
                else if (escape == 'b')
                {
                    value = 8;
                }
                else if (escape == 't')
                {
                    value = 9;
                }
                else if (escape == 'n')
                {
                    value = 10;
                }
                else if (escape == 'v')
                {
                    value = 11;
                }
                else if (escape == 'f')
                {
                    value = 12;
                }
                else if (escape == 'r')
                {
                    value = 13;
                }
                else
                {
                    ok = escape == '\\' || escape == '"' || escape == '\'' || escape == '?';
                }
            }
        }
        ok = ok && value <= 255 && table->string_length < BUSTER_EXTRACT_MAX_STRINGS;
        if (ok)
        {
            table->strings[table->string_length++] = (unsigned char)value;
        }
    }
    return ok;
}

static bool parse_strings(Table *table)
{
    bool ok = true;
    size_t first_line = 0;
    Slice input = section(table, "GET_BUILTIN_STR_TABLE", &first_line, &ok);
    Reader reader = {input, 0, first_line, ok};
    bool found = false;
    while (reader.ok && !found)
    {
        Token token = next_token(&reader);
        found = slice_equal(token.text, "BuiltinStringsStorage");
        reader.ok = reader.ok && token.text.length != 0;
    }
    expect_token(&reader, "[");
    expect_token(&reader, "]");
    expect_token(&reader, "=");
    bool done = false;
    while (reader.ok && !done)
    {
        Token token = next_token(&reader);
        done = slice_equal(token.text, ";");
        if (!done)
        {
            reader.ok = reader.ok && append_literal(table, token.text);
        }
    }
    // Every emitted entry is explicitly NUL-terminated. The implicit C array
    // terminator is irrelevant: no Info offset may refer to that extra byte.
    ok = reader.ok && table->string_length != 0 && table->strings[0] == 0 &&
         table->strings[table->string_length - 1] == 0;
    return ok;
}

static bool parse_enumerators(Table *table)
{
    bool ok = true;
    size_t first_line = 0;
    Slice input = section(table, "GET_BUILTIN_ENUMERATORS", &first_line, &ok);
    Reader reader = {input, 0, first_line, ok};
    bool done = false;
    while (reader.ok && !done)
    {
        Token token = next_token(&reader);
        done = token.text.length == 0;
        if (!done)
        {
            reader.ok = token.text.length > 2 && memcmp(token.text.data, "BI", 2) == 0 &&
                        table->enumerator_count < BUSTER_EXTRACT_MAX_ROWS;
            for (size_t i = 2; reader.ok && i < token.text.length; ++i)
            {
                reader.ok = is_identifier(token.text.data[i]);
            }
            if (reader.ok)
            {
                table->enumerators[table->enumerator_count++] = (Slice){token.text.data + 2, token.text.length - 2};
            }
            expect_token(&reader, ",");
        }
    }
    ok = reader.ok && table->enumerator_count != 0;
    return ok;
}

static const char *table_string(Table *table, size_t offset, bool *ok)
{
    const char *result = "";
    bool valid = offset < table->string_length && (offset == 0 || table->strings[offset - 1] == 0);
    if (valid)
    {
        valid = memchr(table->strings + offset, 0, table->string_length - offset) != NULL;
        if (valid)
        {
            result = (const char *)table->strings + offset;
        }
    }
    *ok = *ok && valid;
    return result;
}

static bool parse_infos(Table *table, bool prefixed)
{
    bool ok = true;
    size_t first_line = 0;
    const char *name = prefixed ? "GET_BUILTIN_PREFIXED_INFOS" : "GET_BUILTIN_INFOS";
    const char *prefix = prefixed ? "__builtin_ia32_" : "";
    Slice input = section(table, name, &first_line, &ok);
    Reader reader = {input, 0, first_line, ok};
    bool done = false;
    while (reader.ok && !done)
    {
        Token token = next_token(&reader);
        done = token.text.length == 0;
        if (!done)
        {
            reader.ok = slice_equal(token.text, "Builtin") && table->row_count < BUSTER_EXTRACT_MAX_ROWS;
            expect_token(&reader, ":");
            expect_token(&reader, ":");
            expect_token(&reader, "Info");
            expect_token(&reader, "{");
            expect_token(&reader, "Builtin");
            expect_token(&reader, ":");
            expect_token(&reader, ":");
            expect_token(&reader, "Info");
            expect_token(&reader, ":");
            expect_token(&reader, ":");
            expect_token(&reader, "StrOffsets");
            expect_token(&reader, "{");
            size_t offsets[4] = {0};
            for (unsigned i = 0; i < 4; ++i)
            {
                parse_offset(&reader, &offsets[i]);
                expect_token(&reader, i == 3 ? "}" : ",");
            }
            expect_token(&reader, ",");
            expect_token(&reader, "HeaderDesc");
            expect_token(&reader, ":");
            expect_token(&reader, ":");
            Token header = next_token(&reader);
            expect_token(&reader, ",");
            Token language = next_token(&reader);
            expect_token(&reader, "}");
            expect_token(&reader, ",");
            reader.ok = reader.ok && header.text.length != 0 && language.text.length != 0;
            for (size_t i = 0; reader.ok && i < header.text.length; ++i)
            {
                reader.ok = is_identifier(header.text.data[i]);
            }
            for (size_t i = 0; reader.ok && i < language.text.length; ++i)
            {
                reader.ok = is_identifier(language.text.data[i]);
            }
            const char *spelling = table_string(table, offsets[0], &reader.ok);
            const char *type = table_string(table, offsets[1], &reader.ok);
            const char *attributes = table_string(table, offsets[2], &reader.ok);
            const char *features = table_string(table, offsets[3], &reader.ok);
            size_t prefix_length = strlen(prefix);
            size_t spelling_length = strlen(spelling);
            reader.ok = reader.ok && spelling_length != 0 && prefix_length + spelling_length < BUSTER_EXTRACT_MAX_NAME &&
                        table->row_count < table->enumerator_count;
            if (reader.ok)
            {
                Row *row = &table->rows[table->row_count];
                memcpy(row->name, prefix, prefix_length);
                memcpy(row->name + prefix_length, spelling, spelling_length + 1);
                reader.ok = slice_equal(table->enumerators[table->row_count], row->name);
                row->type = type;
                row->attributes = attributes;
                row->features = features;
                row->header = header.text;
                row->language = language.text;
                row->shard = table->label;
                row->record_kind = "table-record";
                row->source_line = token.line;
                row->source_ordinal = table->row_count;
                table->row_count += reader.ok;
            }
        }
    }
    ok = reader.ok;
    return ok;
}

static bool parse_table(Table *table)
{
    table->strings = malloc(BUSTER_EXTRACT_MAX_STRINGS);
    table->enumerators = calloc(BUSTER_EXTRACT_MAX_ROWS, sizeof(*table->enumerators));
    table->rows = calloc(BUSTER_EXTRACT_MAX_ROWS, sizeof(*table->rows));
    bool ok = table->strings != NULL && table->enumerators != NULL && table->rows != NULL;
    if (ok)
    {
        ok = parse_strings(table) && parse_enumerators(table) && parse_infos(table, false) && parse_infos(table, true) &&
             table->row_count == table->enumerator_count;
    }
    if (!ok)
    {
        fprintf(stderr, "%s: malformed generated builtin metadata or capacity exhausted\n", table->path);
    }
    return ok;
}

static void sort_rows(Row **rows, Row **scratch, size_t count)
{
    // Iterative stable merge sort: bounded work, no input-depth recursion.
    for (size_t width = 1; width < count; width *= 2)
    {
        for (size_t start = 0; start < count; start += 2 * width)
        {
            size_t middle = start + width < count ? start + width : count;
            size_t end = middle + width < count ? middle + width : count;
            size_t left = start;
            size_t right = middle;
            for (size_t output = start; output < end; ++output)
            {
                bool take_left = right == end || (left < middle && strcmp(rows[left]->name, rows[right]->name) <= 0);
                scratch[output] = take_left ? rows[left++] : rows[right++];
            }
        }
        memcpy(rows, scratch, count * sizeof(*rows));
    }
}

static void write_field(Slice value)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < value.length; ++i)
    {
        unsigned char byte = value.data[i];
        if (byte == '\\')
        {
            fputs("\\\\", stdout);
        }
        else if (byte < 32 || byte > 126)
        {
            fputs("\\x", stdout);
            fputc(hex[byte >> 4], stdout);
            fputc(hex[byte & 15], stdout);
        }
        else
        {
            fputc(byte, stdout);
        }
    }
}

static void write_cstring(const char *value)
{
    write_field((Slice){(const unsigned char *)value, strlen(value)});
}

static Row *find_row(Row **rows, size_t count, Slice name)
{
    Row *result = NULL;
    size_t lower = 0;
    size_t upper = count;
    while (lower < upper && result == NULL)
    {
        size_t middle = lower + (upper - lower) / 2;
        const char *candidate = rows[middle]->name;
        size_t length = strlen(candidate);
        size_t common = name.length < length ? name.length : length;
        int comparison = memcmp(name.data, candidate, common);
        if (comparison == 0)
        {
            comparison = name.length < length ? -1 : (name.length > length ? 1 : 0);
        }
        if (comparison == 0)
        {
            result = rows[middle];
        }
        else if (comparison < 0)
        {
            upper = middle;
        }
        else
        {
            lower = middle + 1;
        }
    }
    return result;
}

static bool filter_rows(Table *names, Row **rows, size_t count, Row **filtered, size_t *filtered_count, Row gaps[3])
{
    // These are syntax/type names, not ordinary builtin declarations. The
    // classifications follow exact-release TokenKinds.def/ParseExpr.cpp and
    // ASTContext's builtin va_list typedef creation; no callable type is made.
    static const char *gap_names[3] =
    {
        "__builtin_bit_cast",
        "__builtin_va_list",
        "__builtin_ms_va_list"
    };
    for (unsigned i = 0; i < 3; ++i)
    {
        memcpy(gaps[i].name, gap_names[i], strlen(gap_names[i]) + 1);
        gaps[i].type = "";
        gaps[i].attributes = "";
        gaps[i].features = "";
        gaps[i].header = (Slice){(const unsigned char *)"NO_HEADER", 9};
        gaps[i].language = (Slice){(const unsigned char *)"UNMODELED", 9};
        gaps[i].shard = "special";
        gaps[i].record_kind = i == 0 ? "parser-special-form-gap" : "predefined-type-gap";
    }
    bool ok = true;
    size_t position = 0;
    while (ok && position < names->input_length)
    {
        size_t start = position;
        while (position < names->input_length && names->input[position] != '\n')
        {
            ++position;
        }
        size_t end = position;
        if (end > start && names->input[end - 1] == '\r')
        {
            --end;
        }
        Slice name = {names->input + start, end - start};
        ok = name.length != 0 && name.length < BUSTER_EXTRACT_MAX_NAME && *filtered_count < BUSTER_EXTRACT_MAX_ROWS;
        for (size_t i = 0; ok && i < name.length; ++i)
        {
            ok = is_identifier(name.data[i]) && (i != 0 || name.data[i] < '0' || name.data[i] > '9');
        }
        if (ok)
        {
            Row *found = find_row(rows, count, name);
            for (unsigned i = 0; found == NULL && i < 3; ++i)
            {
                if (slice_equal(name, gaps[i].name))
                {
                    found = &gaps[i];
                }
            }
            ok = found != NULL;
            if (ok)
            {
                filtered[(*filtered_count)++] = found;
            }
            else
            {
                fputs("unresolved requested builtin: ", stderr);
                fwrite(name.data, 1, name.length, stderr);
                fputc('\n', stderr);
            }
        }
        position += position < names->input_length;
    }
    ok = ok && *filtered_count != 0;
    if (!ok)
    {
        fprintf(stderr, "%s: malformed, oversized or unresolved name filter\n", names->path);
    }
    return ok;
}

static bool write_tsv_rows(Table tables[4], Table *names, Row **rows, size_t count)
{
    fputs("# buster-clang-builtin-metadata-tsv-v1\n# clang=23.1.1\n", stdout);
    fputs("# upstream=https://github.com/llvm/llvm-project/tree/llvmorg-23.1.1\n", stdout);
    fputs("# data-license=Apache-2.0 WITH LLVM-exception\n", stdout);
    fputs("# semantic-constraints=unmodeled; no typing, ICE, feature-eligibility or runtime claim\n", stdout);
    for (unsigned i = 0; i < 4; ++i)
    {
        fprintf(stdout, "# input-%s-sha256=%s\n", tables[i].label, tables[i].expected_digest);
    }
    if (names->path != NULL)
    {
        fprintf(stdout, "# input-names-sha256=%s\n", names->actual_digest);
    }
    fprintf(stdout, "# rows=%zu\n", count);
    fputs("name\ttype_encoding\tattributes\tfeatures\theader_token\tlanguage_token\tshard\tsource_line\tsource_ordinal\tcustom_typechecking\tconstant_evaluable\trecord_kind\tsemantic_constraints\n", stdout);
    for (size_t i = 0; i < count; ++i)
    {
        Row *row = rows[i];
        write_cstring(row->name);
        fputc('\t', stdout);
        write_cstring(row->type);
        fputc('\t', stdout);
        write_cstring(row->attributes);
        fputc('\t', stdout);
        write_cstring(row->features);
        fputc('\t', stdout);
        write_field(row->header);
        fputc('\t', stdout);
        write_field(row->language);
        fprintf(stdout, "\t%s\t%zu\t%zu\t%u\t%u\t%s\tunmodeled\n", row->shard, row->source_line, row->source_ordinal,
                strchr(row->attributes, 't') != NULL ? 1u : 0u, strchr(row->attributes, 'E') != NULL ? 1u : 0u,
                row->record_kind);
    }
    bool ok = fflush(stdout) == 0 && !ferror(stdout);
    return ok;
}

static void write_c_field(Slice value)
{
    fputc('"', stdout);
    for (size_t i = 0; i < value.length; ++i)
    {
        const unsigned char byte = value.data[i];
        if (byte == '"' || byte == '\\' || byte == '?')
        {
            // Escaping '?' also prevents C17 trigraph replacement before
            // string-literal decoding. Each nonprinting byte uses exactly
            // three octal digits so a following digit cannot join its escape.
            fputc('\\', stdout);
            fputc(byte, stdout);
        }
        else if (byte < 32 || byte > 126)
        {
            fputc('\\', stdout);
            fputc('0' + (byte >> 6), stdout);
            fputc('0' + ((byte >> 3) & 7u), stdout);
            fputc('0' + (byte & 7u), stdout);
        }
        else
        {
            fputc(byte, stdout);
        }
    }
    fputc('"', stdout);
}

static void write_c_string(const char *value)
{
    write_c_field((Slice){(const unsigned char *)value, strlen(value)});
}

static bool write_c_rows(Table tables[4], Table *names, Row **rows, size_t count)
{
    fputs("// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n", stdout);
    fputs("// buster-clang-builtin-metadata-c-v1\n// clang=23.1.1\n", stdout);
    fputs("// upstream=https://github.com/llvm/llvm-project/tree/llvmorg-23.1.1\n", stdout);
    fputs("// data-license=Apache-2.0 WITH LLVM-exception\n", stdout);
    fputs("// semantic-constraints=unmodeled; no typing, ICE, feature-eligibility or runtime claim\n", stdout);
    for (unsigned i = 0; i < 4; ++i)
    {
        fprintf(stdout, "// input-%s-sha256=%s\n", tables[i].label, tables[i].expected_digest);
    }
    if (names->path != NULL)
    {
        fprintf(stdout, "// input-names-sha256=%s\n", names->actual_digest);
    }
    fprintf(stdout, "// rows=%zu\n", count);
    fputs("// BUSTER_BUILTIN_METADATA(name, type_encoding, attributes, features, header_token, language_token, shard, source_line, source_ordinal, custom_typechecking, constant_evaluable, record_kind, semantic_constraints)\n", stdout);
    for (size_t i = 0; i < count; ++i)
    {
        const Row *row = rows[i];
        fputs("BUSTER_BUILTIN_METADATA(", stdout);
        write_c_string(row->name);
        fputs(", ", stdout);
        write_c_string(row->type);
        fputs(", ", stdout);
        write_c_string(row->attributes);
        fputs(", ", stdout);
        write_c_string(row->features);
        fputs(", ", stdout);
        write_c_field(row->header);
        fputs(", ", stdout);
        write_c_field(row->language);
        fputs(", ", stdout);
        write_c_string(row->shard);
        fprintf(stdout, ", %zu, %zu, %u, %u, ", row->source_line, row->source_ordinal,
                strchr(row->attributes, 't') != NULL ? 1u : 0u, strchr(row->attributes, 'E') != NULL ? 1u : 0u);
        write_c_string(row->record_kind);
        fputs(", ", stdout);
        write_c_string("unmodeled");
        fputs(")\n", stdout);
    }
    const bool ok = fflush(stdout) == 0 && !ferror(stdout);
    return ok;
}

int main(int argc, char **argv)
{
    Table tables[4] =
    {
        {.label = "generic", .expected_digest = "72fe0f2489802c4ac25b263d545c51c36fc1afae08e8151a33b15821548945ec"},
        {.label = "x86", .expected_digest = "821ca469485d5079920df9d695781a8bdad6576739be0d3ad8fb52c6bca91b03"},
        {.label = "x86_64", .expected_digest = "bc85a6cefc1bb94b25f73a2158cbe3ddc1c3d3ce82c9d170e5ca2becd8818628"},
        {.label = "version", .expected_digest = "2cf74bd66f8958e1936307158bec163fac911e30f4534bab376dd2dff067d5fd"}
    };
    Table names = {.label = "names"};
    OutputFormat format = OUTPUT_FORMAT_TSV;
    bool format_seen = false;
    bool ok = argc >= 9 && argc <= 13 && argc % 2 == 1;
    for (int i = 1; ok && i < argc; i += 2)
    {
        unsigned index = 5;
        if (strcmp(argv[i], "--builtins") == 0)
        {
            index = 0;
        }
        else if (strcmp(argv[i], "--x86") == 0)
        {
            index = 1;
        }
        else if (strcmp(argv[i], "--x86-64") == 0)
        {
            index = 2;
        }
        else if (strcmp(argv[i], "--version") == 0)
        {
            index = 3;
        }
        else if (strcmp(argv[i], "--names") == 0)
        {
            index = 4;
        }
        else if (strcmp(argv[i], "--format") == 0)
        {
            index = 5;
        }
        else
        {
            index = 6;
        }
        ok = index < 6 && argv[i + 1][0] != 0;
        if (ok && index == 5)
        {
            ok = !format_seen && (strcmp(argv[i + 1], "tsv") == 0 || strcmp(argv[i + 1], "c") == 0);
            format_seen = true;
            format = strcmp(argv[i + 1], "c") == 0 ? OUTPUT_FORMAT_C : OUTPUT_FORMAT_TSV;
        }
        else if (ok)
        {
            Table *selected = index == 4 ? &names : &tables[index];
            ok = selected->path == NULL;
            selected->path = argv[i + 1];
        }
    }
    for (unsigned i = 0; ok && i < 4; ++i)
    {
        ok = tables[i].path != NULL;
    }
    if (!ok)
    {
        fputs("usage: clang-builtin-extract --builtins PATH --x86 PATH --x86-64 PATH --version PATH [--names PATH] [--format tsv|c]\n", stderr);
    }
    for (unsigned i = 0; ok && i < 4; ++i)
    {
        ok = tables[i].path != NULL && load_table(&tables[i]);
    }
    for (unsigned i = 0; ok && i < 3; ++i)
    {
        ok = parse_table(&tables[i]);
    }
    if (ok && names.path != NULL)
    {
        ok = load_table(&names);
    }
    size_t count = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        count += tables[i].row_count;
    }
    Row **rows = NULL;
    Row **scratch = NULL;
    Row **filtered = NULL;
    Row gaps[3] = {0};
    if (ok)
    {
        rows = calloc(count, sizeof(*rows));
        size_t scratch_count = count > BUSTER_EXTRACT_MAX_ROWS ? count : BUSTER_EXTRACT_MAX_ROWS;
        scratch = calloc(scratch_count, sizeof(*scratch));
        ok = rows != NULL && scratch != NULL && count != 0;
    }
    if (ok)
    {
        size_t output = 0;
        for (unsigned i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < tables[i].row_count; ++j)
            {
                rows[output++] = &tables[i].rows[j];
            }
        }
        sort_rows(rows, scratch, count);
        for (size_t i = 1; ok && i < count; ++i)
        {
            ok = strcmp(rows[i - 1]->name, rows[i]->name) != 0;
            if (!ok)
            {
                fprintf(stderr, "duplicate builtin spelling: %s\n", rows[i]->name);
            }
        }
    }
    if (ok)
    {
        if (names.path != NULL)
        {
            filtered = calloc(BUSTER_EXTRACT_MAX_ROWS, sizeof(*filtered));
            size_t filtered_count = 0;
            ok = filtered != NULL && filter_rows(&names, rows, count, filtered, &filtered_count, gaps);
            if (ok)
            {
                sort_rows(filtered, scratch, filtered_count);
                for (size_t i = 1; ok && i < filtered_count; ++i)
                {
                    ok = strcmp(filtered[i - 1]->name, filtered[i]->name) != 0;
                    if (!ok)
                    {
                        fprintf(stderr, "%s: duplicate requested builtin: %s\n", names.path, filtered[i]->name);
                    }
                }
            }
            if (ok)
            {
                ok = format == OUTPUT_FORMAT_C ? write_c_rows(tables, &names, filtered, filtered_count) : write_tsv_rows(tables, &names, filtered, filtered_count);
            }
        }
        else
        {
            ok = format == OUTPUT_FORMAT_C ? write_c_rows(tables, &names, rows, count) : write_tsv_rows(tables, &names, rows, count);
        }
    }
    free(filtered);
    free(names.input);
    free(scratch);
    free(rows);
    for (unsigned i = 0; i < 4; ++i)
    {
        free(tables[i].rows);
        free(tables[i].enumerators);
        free(tables[i].strings);
        free(tables[i].input);
    }
    int result = ok ? 0 : 1;
    return result;
}
