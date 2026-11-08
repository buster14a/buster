// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original generator of src/buster/compiler/c_check_cpu_dispatch.inc (#1447): the CPU names clang's cpu_specific and
// cpu_dispatch attributes take, projected from two LLVM inputs. llvm/lib/TargetParser/X86TargetParser.cpp's
// ProcessorDefs rows give each processor's name, feature set and cpu_specific mangling character (clang's
// X86TargetInfo::validateCPUSpecificCPUDispatch takes every name of them, CPUSpecificManglingCharacter gives the
// character, and getCPUSpecificCPUDispatchFeatures the features, llvm::X86::getFeaturesForCPU's: the set's features but
// 64bit, in the features' order). Its feature sets are expressions of `constexpr FeatureBitset` constants, single
// features (Feature##ENUM, one per X86_FEATURE row) and the operators |, &, ^ and ~, which this evaluates.
// llvm/include/llvm/TargetParser/X86TargetParser.def's X86_FEATURE and X86_FEATURE_COMPAT rows give the features' names,
// in their enumeration's order, and the COMPAT rows the ABI value a resolver tests (llvm::X86::getCpuSupportsMask, word
// ABI_VALUE / 32 of libgcc's CPU model, bit ABI_VALUE % 32). Each output row is
// BUSTER_CHECK_CPU_DISPATCH(name, mangling, word0, word1, word2, word3, features): the mangling character, or 0 where
// LLVM has none; the resolver's mask of the CPU's features a resolver can test; and its features' names, comma-separated.
// Rows keep ProcessorDefs' order; a name that is no C identifier (empty, or with a '-') is left out, as no attribute
// argument can spell it. The output is derived LLVM data and keeps the inputs' terms (its header says so); the inputs'
// identity, which the header records, is the caller's: see THIRD_PARTY_NOTICES.md for the recorded run.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_cpu_dispatch.c -o build/generate_x86_cpu_dispatch
//   build/generate_x86_cpu_dispatch --self-test
//   build/generate_x86_cpu_dispatch DEF_PATH CPP_PATH "SOURCE" "DEF_SHA256" "CPP_SHA256" > OUTPUT
//
// It succeeds (exit status 0) only when it read both inputs whole, evaluated every feature set and every processor row,
// found at least one row, and wrote and flushed all of its output. --self-test checks that on synthetic inputs: the rows
// it must print, a write to /dev/full, an unknown name in an expression and a malformed processor row.
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPATCH_FEATURES 256
#define DISPATCH_WORDS (DISPATCH_FEATURES / 64)
#define DISPATCH_SETS 512
#define DISPATCH_NAME 96

typedef struct DispatchBits DispatchBits;
struct DispatchBits
{
    uint64_t words[DISPATCH_WORDS];
};

typedef struct DispatchFeature DispatchFeature;
struct DispatchFeature
{
    char name[DISPATCH_NAME];
    char string[DISPATCH_NAME];
    int32_t abi;
};

typedef struct DispatchSet DispatchSet;
struct DispatchSet
{
    char name[DISPATCH_NAME];
    DispatchBits bits;
};

typedef struct DispatchState DispatchState;
struct DispatchState
{
    DispatchFeature features[DISPATCH_FEATURES];
    uint32_t feature_count;
    DispatchSet sets[DISPATCH_SETS];
    uint32_t set_count;
};

// A whole input, read into a NUL-terminated buffer, or NULL.
static char* dispatch_read(FILE* file)
{
    char* text = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool valid = fseek(file, 0, SEEK_SET) == 0;
    while (valid)
    {
        if (length + 4096 + 1 > capacity)
        {
            capacity = capacity != 0 ? capacity * 2 : 65536;
            char* grown = realloc(text, capacity);
            valid = grown != NULL;
            text = grown != NULL ? grown : text;
        }
        const size_t read = valid ? fread(text + length, 1, 4096, file) : 0;
        length += read;
        valid = valid && read != 0;
    }
    if (ferror(file) || text == NULL)
    {
        free(text);
        text = NULL;
    }
    else
    {
        text[length] = 0;
    }
    return text;
}

// Copies the identifier at `*cursor` (after spaces) into `name`: whether there was one that fits.
static bool dispatch_identifier(const char** cursor, char* name)
{
    const char* at = *cursor;
    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r')
    {
        at += 1;
    }
    size_t length = 0;
    while ((isalnum((unsigned char)at[length]) || at[length] == '_') && length + 1 < DISPATCH_NAME)
    {
        name[length] = at[length];
        length += 1;
    }
    name[length] = 0;
    const bool valid = length != 0 && !(isalnum((unsigned char)at[length]) || at[length] == '_');
    *cursor = at + length;
    return valid;
}

// The .def's X86_FEATURE and X86_FEATURE_COMPAT rows, in order: whether every row parsed.
static bool dispatch_features(DispatchState* state, const char* def)
{
    bool valid = true;
    for (const char* line = def; valid && line != NULL && *line != 0; line = strchr(line, '\n'), line = line != NULL ? line + 1 : NULL)
    {
        const bool compat = strncmp(line, "X86_FEATURE_COMPAT", 18) == 0;
        const bool plain = !compat && strncmp(line, "X86_FEATURE", 11) == 0 && (line[11] == ' ' || line[11] == '(');
        if (!compat && !plain)
        {
            continue;
        }
        const char* at = line + (compat ? 18 : 11);
        while (*at == ' ')
        {
            at += 1;
        }
        DispatchFeature feature = { .abi = -1 };
        valid = *at == '(' && state->feature_count < DISPATCH_FEATURES;
        at += 1;
        valid = valid && dispatch_identifier(&at, feature.name);
        const char* quote = valid ? strchr(at, '"') : NULL;
        const char* closing = quote != NULL ? strchr(quote + 1, '"') : NULL;
        const char* end = strchr(at, '\n');
        valid = valid && closing != NULL && (end == NULL || closing < end) && (size_t)(closing - quote - 1) < DISPATCH_NAME;
        if (valid)
        {
            memcpy(feature.string, quote + 1, (size_t)(closing - quote - 1));
            feature.string[closing - quote - 1] = 0;
        }
        if (valid && compat)
        {
            // The ABI value: the last argument.
            const char* comma = NULL;
            for (const char* c = closing; *c != 0 && *c != ')' && *c != '\n'; c += 1)
            {
                comma = *c == ',' ? c : comma;
            }
            char* number_end = NULL;
            const long value = comma != NULL ? strtol(comma + 1, &number_end, 10) : -1;
            valid = comma != NULL && number_end != comma + 1 && value >= 0 && value < 128;
            feature.abi = (int32_t)value;
        }
        if (valid)
        {
            state->features[state->feature_count++] = feature;
        }
        else
        {
            fprintf(stderr, "generate_x86_cpu_dispatch: can't read the feature row: %.80s\n", line);
        }
    }
    return valid && state->feature_count != 0;
}

static int32_t dispatch_feature_index(const DispatchState* state, const char* enum_name)
{
    int32_t result = -1;
    for (uint32_t f = 0; f < state->feature_count && result < 0; f += 1)
    {
        result = strcmp(state->features[f].name, enum_name) == 0 ? (int32_t)f : -1;
    }
    return result;
}

// The value of the named set: Feature##ENUM, one feature, or a constant evaluated before.
static bool dispatch_lookup(const DispatchState* state, const char* name, DispatchBits* bits)
{
    bool found = false;
    *bits = (DispatchBits){ 0 };
    for (uint32_t s = state->set_count; s != 0 && !found; s -= 1)
    {
        found = strcmp(state->sets[s - 1].name, name) == 0;
        *bits = found ? state->sets[s - 1].bits : *bits;
    }
    const int32_t feature = !found && strncmp(name, "Feature", 7) == 0 && strncmp(name, "Features", 8) != 0 ? dispatch_feature_index(state, name + 7) : -1;
    if (feature >= 0)
    {
        bits->words[feature / 64] |= 1ull << (feature % 64);
        found = true;
    }
    if (!found)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: unknown feature set %s\n", name);
    }
    return found;
}

static void dispatch_space(const char** cursor)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\n' || **cursor == '\r')
    {
        *cursor += 1;
    }
}

static bool dispatch_or(const DispatchState* state, const char** cursor, DispatchBits* bits);

// A unary operand: ~x, (x), {}, or a name.
static bool dispatch_unary(const DispatchState* state, const char** cursor, DispatchBits* bits)
{
    bool valid = true;
    dispatch_space(cursor);
    if (**cursor == '~')
    {
        *cursor += 1;
        valid = dispatch_unary(state, cursor, bits);
        for (uint32_t w = 0; w < DISPATCH_WORDS; w += 1)
        {
            bits->words[w] = ~bits->words[w];
        }
    }
    else if (**cursor == '(')
    {
        *cursor += 1;
        valid = dispatch_or(state, cursor, bits);
        dispatch_space(cursor);
        valid = valid && **cursor == ')';
        *cursor += valid;
    }
    else if (**cursor == '{')
    {
        *cursor += 1;
        dispatch_space(cursor);
        valid = **cursor == '}';
        *cursor += valid;
        *bits = (DispatchBits){ 0 };
    }
    else
    {
        char name[DISPATCH_NAME];
        valid = dispatch_identifier(cursor, name) && dispatch_lookup(state, name, bits);
    }
    return valid;
}

// C's precedence: & binds tighter than ^, which binds tighter than |.
static bool dispatch_and(const DispatchState* state, const char** cursor, DispatchBits* bits)
{
    bool valid = dispatch_unary(state, cursor, bits);
    dispatch_space(cursor);
    while (valid && **cursor == '&')
    {
        *cursor += 1;
        DispatchBits right;
        valid = dispatch_unary(state, cursor, &right);
        for (uint32_t w = 0; w < DISPATCH_WORDS; w += 1)
        {
            bits->words[w] &= right.words[w];
        }
        dispatch_space(cursor);
    }
    return valid;
}

static bool dispatch_xor(const DispatchState* state, const char** cursor, DispatchBits* bits)
{
    bool valid = dispatch_and(state, cursor, bits);
    dispatch_space(cursor);
    while (valid && **cursor == '^')
    {
        *cursor += 1;
        DispatchBits right;
        valid = dispatch_and(state, cursor, &right);
        for (uint32_t w = 0; w < DISPATCH_WORDS; w += 1)
        {
            bits->words[w] ^= right.words[w];
        }
        dispatch_space(cursor);
    }
    return valid;
}

static bool dispatch_or(const DispatchState* state, const char** cursor, DispatchBits* bits)
{
    bool valid = dispatch_xor(state, cursor, bits);
    dispatch_space(cursor);
    while (valid && **cursor == '|')
    {
        *cursor += 1;
        DispatchBits right;
        valid = dispatch_xor(state, cursor, &right);
        for (uint32_t w = 0; w < DISPATCH_WORDS; w += 1)
        {
            bits->words[w] |= right.words[w];
        }
        dispatch_space(cursor);
    }
    return valid;
}

// Every `constexpr FeatureBitset NAME = EXPRESSION;` of the .cpp, in order: whether each evaluated.
static bool dispatch_sets(DispatchState* state, const char* cpp)
{
    static const char marker[] = "constexpr FeatureBitset ";
    bool valid = true;
    uint32_t found = 0;
    for (const char* at = strstr(cpp, marker); valid && at != NULL; at = strstr(at, marker))
    {
        at += sizeof(marker) - 1;
        DispatchSet set = { 0 };
        valid = dispatch_identifier(&at, set.name) && state->set_count < DISPATCH_SETS;
        dispatch_space(&at);
        // `Feature##ENUM = {X86::FEATURE_##ENUM}`, the macro's single features, are dispatch_lookup's own.
        const bool macro = valid && strcmp(set.name, "Feature") == 0;
        valid = valid && (macro || *at == '=');
        if (valid && !macro)
        {
            at += 1;
            valid = dispatch_or(state, &at, &set.bits);
            dispatch_space(&at);
            valid = valid && *at == ';';
        }
        if (valid && !macro)
        {
            state->sets[state->set_count++] = set;
            found += 1;
        }
        else if (!valid)
        {
            fprintf(stderr, "generate_x86_cpu_dispatch: can't evaluate the feature set %s\n", set.name);
        }
    }
    return valid && found != 0;
}

// The processors' rows of the .cpp, each written to `out`: whether every row parsed and at least one was written.
static bool dispatch_processors(const DispatchState* state, const char* cpp, FILE* out)
{
    const char* begin = strstr(cpp, "ProcessorDefs[] = {");
    const char* end = begin != NULL ? strstr(begin, "\n};") : NULL;
    bool valid = begin != NULL && end != NULL;
    uint32_t rows = 0;
    const char* line = valid ? strchr(begin, '\n') : NULL;
    while (valid && line != NULL && line < end)
    {
        line += 1;
        const char* at = line;
        dispatch_space(&at);
        if (strncmp(at, "{ {\"", 4) == 0)
        {
            const char* name = at + 4;
            const char* closing = strchr(name, '"');
            const char* newline = strchr(at, '\n');
            valid = closing != NULL && newline != NULL && closing < newline && (size_t)(closing - name) < DISPATCH_NAME;
            // {CK_KIND, KEY, FEATURES, 'M', bool}: the third and fourth fields.
            const char* fields = valid ? strstr(closing, "}, {") : NULL;
            valid = valid && fields != NULL && fields < newline;
            const char* cursor = valid ? fields + 4 : NULL;
            for (uint32_t comma = 0; valid && comma < 2; comma += 1)
            {
                cursor = strchr(cursor, ',');
                valid = cursor != NULL && cursor < newline;
                cursor += valid;
            }
            DispatchBits bits = { 0 };
            valid = valid && dispatch_or(state, &cursor, &bits);
            dispatch_space(&cursor);
            valid = valid && *cursor == ',';
            cursor += valid;
            dispatch_space(&cursor);
            char mangling = 0;
            if (valid && strncmp(cursor, "'\\0'", 4) == 0)
            {
                mangling = 0;
            }
            else if (valid && cursor[0] == '\'' && isalpha((unsigned char)cursor[1]) && cursor[2] == '\'')
            {
                mangling = cursor[1];
            }
            else
            {
                valid = false;
            }
            bool identifier = closing != name;
            for (const char* c = name; valid && c < closing; c += 1)
            {
                identifier &= isalnum((unsigned char)*c) || *c == '_';
            }
            if (valid && identifier)
            {
                uint32_t mask[4] = { 0, 0, 0, 0 };
                for (uint32_t f = 0; f < state->feature_count; f += 1)
                {
                    const bool set = (bits.words[f / 64] >> (f % 64) & 1) != 0 && strcmp(state->features[f].string, "64bit") != 0;
                    if (set && state->features[f].abi >= 0)
                    {
                        mask[state->features[f].abi / 32] |= 1u << (state->features[f].abi % 32);
                    }
                }
                fprintf(out, "BUSTER_CHECK_CPU_DISPATCH(\"%.*s\", ", (int)(closing - name), name);
                if (mangling != 0)
                {
                    fprintf(out, "'%c'", mangling);
                }
                else
                {
                    fprintf(out, "0");
                }
                fprintf(out, ", 0x%08xu, 0x%08xu, 0x%08xu, 0x%08xu, \"", mask[0], mask[1], mask[2], mask[3]);
                bool first = true;
                for (uint32_t f = 0; f < state->feature_count; f += 1)
                {
                    const bool set = (bits.words[f / 64] >> (f % 64) & 1) != 0 && strcmp(state->features[f].string, "64bit") != 0 &&
                                     state->features[f].string[0] != 0;
                    if (set)
                    {
                        fprintf(out, "%s%s", first ? "" : ",", state->features[f].string);
                        first = false;
                    }
                }
                fprintf(out, "\")\n");
                rows += 1;
            }
            if (!valid)
            {
                fprintf(stderr, "generate_x86_cpu_dispatch: can't read the processor row: %.100s\n", line);
            }
        }
        line = strchr(line, '\n');
    }
    return valid && rows != 0;
}

// The table of `def` and `cpp` written to `out`, its header naming `source` and the inputs' hashes: whether both inputs
// were read whole, every row was taken, and every byte of the output was written and flushed.
static bool dispatch_generate(FILE* def, FILE* cpp, FILE* out, const char* source, const char* def_sha256, const char* cpp_sha256)
{
    static DispatchState state;
    memset(&state, 0, sizeof(state));
    char* def_text = dispatch_read(def);
    char* cpp_text = dispatch_read(cpp);
    if (def_text == NULL || cpp_text == NULL)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: can't read an input\n");
    }
    fprintf(out, "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n");
    fprintf(out, "// Generated by tools/generate_x86_cpu_dispatch.c; do not edit. Derived LLVM data, part of the LLVM Project, under\n");
    fprintf(out, "// the Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): the processors of\n");
    fprintf(out, "// llvm/lib/TargetParser/X86TargetParser.cpp (sha256 %s) and the features of\n", cpp_sha256);
    fprintf(out, "// llvm/include/llvm/TargetParser/X86TargetParser.def (sha256 %s), from\n", def_sha256);
    fprintf(out, "// %s; see THIRD_PARTY_NOTICES.md.\n", source);
    fprintf(out, "// The CPU names cpu_specific and cpu_dispatch take (#1447, c_check_cpu_dispatch.c), each\n");
    fprintf(out, "// BUSTER_CHECK_CPU_DISPATCH(name, mangling, word0, word1, word2, word3, features): its version's mangling character\n");
    fprintf(out, "// (0 for none), the mask of libgcc's CPU model a resolver tests it with (word 0 at __cpu_model + 12, words 1 to 3\n");
    fprintf(out, "// __cpu_features2[0] to [2]) and the features its version's body is compiled with.\n");
    const bool read = def_text != NULL && cpp_text != NULL && dispatch_features(&state, def_text) && dispatch_sets(&state, cpp_text) &&
                      dispatch_processors(&state, cpp_text, out);
    const bool written = fflush(out) == 0 && !ferror(out);
    if (!written)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: can't write the table\n");
    }
    free(def_text);
    free(cpp_text);
    return read && written;
}

// A synthetic input of `text` in a temporary file, or NULL.
static FILE* dispatch_input(const char* text)
{
    FILE* file = tmpfile();
    const bool written = file != NULL && fputs(text, file) >= 0 && fflush(file) == 0;
    if (file != NULL && !written)
    {
        fclose(file);
        file = NULL;
    }
    return file;
}

// The self-test: a synthetic input's rows, then the failures it must report. Returns the exit status.
static int dispatch_self_test(void)
{
    static const char def[] =
        "X86_FEATURE_COMPAT(CMOV,               \"cmov\",                  0,   0)\n"
        "X86_FEATURE_COMPAT(SSE3,               \"sse3\",                  4,   5)\n"
        "X86_FEATURE_COMPAT(CMPXCHG16B,         \"cx16\",                  0,  46)\n"
        "X86_FEATURE       (64BIT,              \"64bit\")\n"
        "X86_FEATURE       (X87,                \"x87\")\n";
    static const char cpp[] =
        "#define X86_FEATURE(ENUM, STRING) \\\n"
        "  constexpr FeatureBitset Feature##ENUM = {X86::FEATURE_##ENUM};\n"
        "constexpr FeatureBitset FeaturesA = FeatureX87 | FeatureCMOV;\n"
        "static constexpr FeatureBitset FeaturesB =\n"
        "    (FeaturesA & ~FeatureCMOV) | FeatureSSE3 ^ FeatureX87 | FeatureCMPXCHG16B | Feature64BIT;\n"
        "constexpr FeatureBitset ImpliedFeaturesX = {};\n"
        "constexpr EnumStringDef<ProcInfo> ProcessorDefs[] = {\n"
        "  { {\"\"}, {CK_None, ~0U, FeatureX87, '\\0', false} },\n"
        "  { {\"alpha\"}, {CK_A, ~0U, FeaturesA, 'A', true} },\n"
        "  { {\"be-ta\"}, {CK_B, ~0U, FeaturesB, 'B', false} },\n"
        "  { {\"gamma\"}, {CK_B, FEATURE_SSE3, FeaturesB, '\\0', false} },\n"
        "};\n";
    static const char rows[] =
        "BUSTER_CHECK_CPU_DISPATCH(\"alpha\", 'A', 0x00000001u, 0x00000000u, 0x00000000u, 0x00000000u, \"cmov,x87\")\n"
        "BUSTER_CHECK_CPU_DISPATCH(\"gamma\", 0, 0x00000020u, 0x00004000u, 0x00000000u, 0x00000000u, \"sse3,cx16,x87\")\n";
    uint32_t failures = 0;
    FILE* def_file = dispatch_input(def);
    FILE* cpp_file = dispatch_input(cpp);
    FILE* out = tmpfile();
    char text[8192] = { 0 };
    const bool generated = def_file != NULL && cpp_file != NULL && out != NULL && dispatch_generate(def_file, cpp_file, out, "self-test", "0", "0") &&
                           fseek(out, 0, SEEK_SET) == 0;
    const size_t length = generated ? fread(text, 1, sizeof(text) - 1, out) : 0;
    const char* newline = length != 0 ? strstr(text, "\nBUSTER_CHECK_CPU_DISPATCH(\"") : NULL;
    const char* found = newline != NULL ? newline + 1 : NULL;
    const bool rows_match = found != NULL && strcmp(found, rows) == 0;
    failures += !rows_match;
    if (!rows_match)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: self-test: the synthetic input's rows differ:\n%s", found != NULL ? found : "(none)\n");
    }
    FILE* full = fopen("/dev/full", "w");
    const bool full_refused = full != NULL && !dispatch_generate(def_file, cpp_file, full, "self-test", "0", "0");
    failures += !full_refused;
    if (!full_refused)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: self-test: a write to /dev/full succeeded\n");
    }
    FILE* sink = tmpfile();
    FILE* unknown = dispatch_input("constexpr FeatureBitset FeaturesA = FeatureX87 | FeatureNOPE;\nProcessorDefs[] = {\n};\n");
    const bool unknown_refused = unknown != NULL && sink != NULL && !dispatch_generate(def_file, unknown, sink, "self-test", "0", "0");
    failures += !unknown_refused;
    if (!unknown_refused)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: self-test: an unknown feature succeeded\n");
    }
    FILE* malformed = dispatch_input("constexpr FeatureBitset FeaturesA = FeatureX87;\nconstexpr EnumStringDef<ProcInfo> ProcessorDefs[] = {\n"
                                     "  { {\"alpha\"}, {CK_A, ~0U, FeaturesA, 'AB', true} },\n};\n");
    const bool malformed_refused = malformed != NULL && sink != NULL && !dispatch_generate(def_file, malformed, sink, "self-test", "0", "0");
    failures += !malformed_refused;
    if (!malformed_refused)
    {
        fprintf(stderr, "generate_x86_cpu_dispatch: self-test: a malformed processor row succeeded\n");
    }
    FILE* files[] = { def_file, cpp_file, out, full, sink, unknown, malformed };
    for (uint32_t f = 0; f < sizeof(files) / sizeof(files[0]); f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    fprintf(stderr, "generate_x86_cpu_dispatch: self-test: 4 checks, %u failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    const bool self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    FILE* def = !self_test && argc == 6 ? fopen(argv[1], "rb") : NULL;
    FILE* cpp = !self_test && argc == 6 ? fopen(argv[2], "rb") : NULL;
    int status = 1;
    if (self_test)
    {
        status = dispatch_self_test();
    }
    else if (def == NULL || cpp == NULL)
    {
        fprintf(stderr, "usage: generate_x86_cpu_dispatch DEF_PATH CPP_PATH SOURCE DEF_SHA256 CPP_SHA256, or generate_x86_cpu_dispatch --self-test\n");
    }
    else
    {
        status = dispatch_generate(def, cpp, stdout, argv[3], argv[4], argv[5]) ? 0 : 1;
    }
    if (def != NULL)
    {
        fclose(def);
    }
    if (cpp != NULL)
    {
        fclose(cpp);
    }
    return status;
}
