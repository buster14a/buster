// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original generator of src/buster/compiler/c_check_feature_map.inc (#2093): what clang's X86TargetInfo::initFeatureMap
// writes into a function's feature map, and in which order, projected from two inputs,
// llvm/include/llvm/TargetParser/X86TargetParser.def and llvm/lib/TargetParser/X86TargetParser.cpp:
//
// - each feature of the .def (X86_FEATURE and X86_FEATURE_COMPAT rows), in its order, which is FeatureInfos' index order
//   and so the order llvm::X86::getFeaturesForCPU and updateImpliedFeatures write a map's keys in: its name (empty for
//   a feature no map names) and its direct implications (its ImpliedFeatures set of the .cpp), as two 64-bit words of
//   feature indices;
// - each processor of the .cpp's ProcessorDefs, the first row of each name, as llvm::X86::getFeaturesForCPU finds it:
//   its name and its Features set, the same two words.
//
// The .cpp's sets are constexpr FeatureBitset definitions: a feature (Feature + its enumerator, one bit), a set defined
// before, `{}`, and `~`, `&`, `^`, `|` and parentheses of them, with C++'s precedence. Rows keep the inputs' order. The
// output is derived LLVM data and keeps the inputs' terms (its header says so); the inputs' identity, which the header
// records, is the caller's: see THIRD_PARTY_NOTICES.md for the recorded run.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_feature_map.c -o build/generate_x86_feature_map
//   build/generate_x86_feature_map --self-test
//   build/generate_x86_feature_map DEF_PATH CPP_PATH "SOURCE" "DEF_SHA256" "CPP_SHA256" > OUTPUT
//
// It succeeds (exit status 0) only when it read both inputs whole, parsed every set and row it takes, found features
// and processors, and wrote and flushed all of its output. --self-test checks that on synthetic inputs: the rows it must
// print, a write to /dev/full, an input that can't be read and a malformed set. Write to a temporary file and replace
// the checked-in table only after the generator succeeds.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FEATURE_MAP_FEATURES 128
#define FEATURE_MAP_SETS 512
#define FEATURE_MAP_NAME 64
#define FEATURE_MAP_TEXT (1u << 20)

typedef struct FeatureMapSet FeatureMapSet;
struct FeatureMapSet
{
    uint64_t words[2];
};

// The .def's features and the .cpp's named sets.
typedef struct FeatureMapState FeatureMapState;
struct FeatureMapState
{
    char enumerators[FEATURE_MAP_FEATURES][FEATURE_MAP_NAME];
    char names[FEATURE_MAP_FEATURES][FEATURE_MAP_NAME];
    uint32_t feature_count;
    char set_names[FEATURE_MAP_SETS][FEATURE_MAP_NAME];
    FeatureMapSet sets[FEATURE_MAP_SETS];
    uint32_t set_count;
};

// The parser of a set's expression: its text and cursor, and whether it has failed.
typedef struct FeatureMapParser FeatureMapParser;
struct FeatureMapParser
{
    const FeatureMapState* state;
    const char* text;
    uint32_t at;
    bool valid;
};

static FeatureMapSet feature_map_or(const FeatureMapSet a, const FeatureMapSet b)
{
    return (FeatureMapSet){ { a.words[0] | b.words[0], a.words[1] | b.words[1] } };
}

static void feature_map_space(FeatureMapParser* parser)
{
    while (parser->text[parser->at] == ' ' || parser->text[parser->at] == '\n' || parser->text[parser->at] == '\t')
    {
        parser->at += 1;
    }
}

static bool feature_map_identifier_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static FeatureMapSet feature_map_or_expression(FeatureMapParser* parser);

// A primary: `~` of one, a parenthesized expression, `{}`, or a name: a feature's or a set's defined before.
static FeatureMapSet feature_map_unary(FeatureMapParser* parser)
{
    FeatureMapSet result = { { 0, 0 } };
    feature_map_space(parser);
    const char c = parser->text[parser->at];
    if (c == '~')
    {
        parser->at += 1;
        const FeatureMapSet operand = feature_map_unary(parser);
        result = (FeatureMapSet){ { ~operand.words[0], ~operand.words[1] } };
    }
    else if (c == '(')
    {
        parser->at += 1;
        result = feature_map_or_expression(parser);
        feature_map_space(parser);
        parser->valid = parser->valid && parser->text[parser->at] == ')';
        parser->at += parser->valid;
    }
    else if (c == '{')
    {
        parser->at += 1;
        feature_map_space(parser);
        parser->valid = parser->valid && parser->text[parser->at] == '}';
        parser->at += parser->valid;
    }
    else
    {
        const uint32_t start = parser->at;
        while (feature_map_identifier_char(parser->text[parser->at]))
        {
            parser->at += 1;
        }
        const uint32_t length = parser->at - start;
        bool found = false;
        for (uint32_t s = parser->state->set_count; !found && s-- > 0;)
        {
            if (strlen(parser->state->set_names[s]) == length && memcmp(parser->state->set_names[s], parser->text + start, length) == 0)
            {
                result = parser->state->sets[s];
                found = true;
            }
        }
        for (uint32_t f = 0; !found && length > 7 && memcmp(parser->text + start, "Feature", 7) == 0 && f < parser->state->feature_count; f += 1)
        {
            if (strlen(parser->state->enumerators[f]) == length - 7 && memcmp(parser->state->enumerators[f], parser->text + start + 7, length - 7) == 0)
            {
                result.words[f / 64] |= 1ull << (f % 64);
                found = true;
            }
        }
        if (!found && parser->valid)
        {
            fprintf(stderr, "generate_x86_feature_map: unknown name '%.*s'\n", (int)length, parser->text + start);
        }
        parser->valid = parser->valid && found;
    }
    return result;
}

static FeatureMapSet feature_map_and_expression(FeatureMapParser* parser)
{
    FeatureMapSet result = feature_map_unary(parser);
    feature_map_space(parser);
    while (parser->valid && parser->text[parser->at] == '&')
    {
        parser->at += 1;
        const FeatureMapSet operand = feature_map_unary(parser);
        result = (FeatureMapSet){ { result.words[0] & operand.words[0], result.words[1] & operand.words[1] } };
        feature_map_space(parser);
    }
    return result;
}

static FeatureMapSet feature_map_xor_expression(FeatureMapParser* parser)
{
    FeatureMapSet result = feature_map_and_expression(parser);
    feature_map_space(parser);
    while (parser->valid && parser->text[parser->at] == '^')
    {
        parser->at += 1;
        const FeatureMapSet operand = feature_map_and_expression(parser);
        result = (FeatureMapSet){ { result.words[0] ^ operand.words[0], result.words[1] ^ operand.words[1] } };
        feature_map_space(parser);
    }
    return result;
}

static FeatureMapSet feature_map_or_expression(FeatureMapParser* parser)
{
    FeatureMapSet result = feature_map_xor_expression(parser);
    feature_map_space(parser);
    while (parser->valid && parser->text[parser->at] == '|')
    {
        parser->at += 1;
        result = feature_map_or(result, feature_map_xor_expression(parser));
        feature_map_space(parser);
    }
    return result;
}

// The set of expression `text`, which must be the whole text; whether it parsed.
static bool feature_map_evaluate(const FeatureMapState* state, const char* text, FeatureMapSet* set)
{
    FeatureMapParser parser = { .state = state, .text = text, .at = 0, .valid = true };
    *set = feature_map_or_expression(&parser);
    feature_map_space(&parser);
    const bool valid = parser.valid && text[parser.at] == 0;
    if (!valid)
    {
        fprintf(stderr, "generate_x86_feature_map: can't read the set: %s\n", text);
    }
    return valid;
}

// The .def's features, in its order: X86_FEATURE(ENUM, "name") and X86_FEATURE_COMPAT(ENUM, "name", ...) rows.
static bool feature_map_def(FILE* def, FeatureMapState* state)
{
    char line[512];
    bool valid = true;
    while (valid && fgets(line, sizeof(line), def) != NULL)
    {
        // The macro's name, then spaces before its `(`, as the .def aligns its rows.
        const bool compat = strncmp(line, "X86_FEATURE_COMPAT", 18) == 0;
        const bool plain = !compat && strncmp(line, "X86_FEATURE", 11) == 0;
        const char* open = line + (compat ? 18 : 11);
        while ((compat || plain) && *open == ' ')
        {
            open += 1;
        }
        if ((!compat && !plain) || *open != '(')
        {
            continue;
        }
        const char* cursor = open + 1;
        while (*cursor == ' ')
        {
            cursor += 1;
        }
        const char* enumerator_end = strchr(cursor, ',');
        const char* name = enumerator_end != NULL ? strchr(enumerator_end, '"') : NULL;
        const char* name_end = name != NULL ? strchr(name + 1, '"') : NULL;
        const char* trimmed = enumerator_end;
        while (trimmed != NULL && trimmed > cursor && trimmed[-1] == ' ')
        {
            trimmed -= 1;
        }
        valid = name_end != NULL && trimmed > cursor && (size_t)(trimmed - cursor) < FEATURE_MAP_NAME && (size_t)(name_end - name - 1) < FEATURE_MAP_NAME &&
                state->feature_count < FEATURE_MAP_FEATURES;
        if (valid)
        {
            memcpy(state->enumerators[state->feature_count], cursor, (size_t)(trimmed - cursor));
            state->enumerators[state->feature_count][trimmed - cursor] = 0;
            memcpy(state->names[state->feature_count], name + 1, (size_t)(name_end - name - 1));
            state->names[state->feature_count][name_end - name - 1] = 0;
            state->feature_count += 1;
        }
        else
        {
            fprintf(stderr, "generate_x86_feature_map: can't read the feature: %s", line);
        }
    }
    valid = valid && !ferror(def) && state->feature_count != 0;
    if (!valid)
    {
        fprintf(stderr, "generate_x86_feature_map: can't read the .def input\n");
    }
    return valid;
}

// The .cpp's text without its comments, `text` of `capacity` bytes.
static bool feature_map_text(FILE* cpp, char* text, uint32_t capacity)
{
    char line[1024];
    uint32_t length = 0;
    bool valid = true;
    while (valid && fgets(line, sizeof(line), cpp) != NULL)
    {
        char* comment = strstr(line, "//");
        if (comment != NULL)
        {
            comment[0] = '\n';
            comment[1] = 0;
        }
        const size_t size = strlen(line);
        valid = length + size + 1 < capacity;
        if (valid)
        {
            memcpy(text + length, line, size);
            length += (uint32_t)size;
        }
    }
    text[length] = 0;
    return valid && !ferror(cpp);
}

// The .cpp's sets, each `constexpr FeatureBitset NAME = EXPRESSION;` in order, then its processors' rows written to
// `out`, then the features' rows; `*rows` counts the processors.
static bool feature_map_cpp(FILE* cpp, FILE* out, FeatureMapState* state, uint32_t* rows)
{
    char* text = malloc(FEATURE_MAP_TEXT);
    char* expression = malloc(FEATURE_MAP_TEXT);
    bool valid = text != NULL && expression != NULL && feature_map_text(cpp, text, FEATURE_MAP_TEXT);
    const char* cursor = text;
    const char* marker = "constexpr FeatureBitset ";
    while (valid && cursor != NULL && (cursor = strstr(cursor, marker)) != NULL)
    {
        const char* name = cursor + strlen(marker);
        const char* name_end = name;
        while (feature_map_identifier_char(*name_end))
        {
            name_end += 1;
        }
        const char* equals = name_end;
        while (*equals == ' ')
        {
            equals += 1;
        }
        const char* end = strchr(name_end, ';');
        // The macro's definition of each feature's set (`Feature##ENUM = {X86::FEATURE_##ENUM}`) names no set.
        const bool named = name_end > name && *equals == '=' && end != NULL;
        if (named)
        {
            valid = (size_t)(name_end - name) < FEATURE_MAP_NAME && state->set_count < FEATURE_MAP_SETS && (size_t)(end - equals) < FEATURE_MAP_TEXT;
            if (valid)
            {
                memcpy(expression, equals + 1, (size_t)(end - equals - 1));
                expression[end - equals - 1] = 0;
                FeatureMapSet set = { { 0, 0 } };
                valid = feature_map_evaluate(state, expression, &set);
                memcpy(state->set_names[state->set_count], name, (size_t)(name_end - name));
                state->set_names[state->set_count][name_end - name] = 0;
                state->sets[state->set_count++] = set;
            }
        }
        cursor = named ? end : name;
    }
    // The processors: `{ {"name"}, {CK_KIND, KEY, SET, 'mangling', only} },`, the first row of each name.
    const char* processors = valid ? strstr(text, "ProcessorDefs[] = {") : NULL;
    const char* processors_end = processors != NULL ? strstr(processors, "\n};") : NULL;
    valid = valid && processors_end != NULL;
    static char written[512][FEATURE_MAP_NAME];
    uint32_t written_count = 0;
    for (const char* row = processors != NULL ? strstr(processors, "{ {\"") : NULL; valid && row != NULL && row < processors_end; row = strstr(row + 1, "{ {\""))
    {
        const char* name = row + 4;
        const char* name_end = strchr(name, '"');
        const char* kind = name_end != NULL ? strstr(name_end, "{CK_") : NULL;
        const char* key = kind != NULL ? strchr(kind, ',') : NULL;
        const char* set = key != NULL ? strchr(key + 1, ',') : NULL;
        const char* set_end = set != NULL ? strstr(set, ", '") : NULL;
        valid = set_end != NULL && set_end < processors_end && (size_t)(name_end - name) < FEATURE_MAP_NAME && written_count < 512;
        if (valid)
        {
            char spelling[FEATURE_MAP_NAME];
            memcpy(spelling, name, (size_t)(name_end - name));
            spelling[name_end - name] = 0;
            bool seen = false;
            for (uint32_t w = 0; w < written_count; w += 1)
            {
                seen |= strcmp(written[w], spelling) == 0;
            }
            memcpy(expression, set + 1, (size_t)(set_end - set - 1));
            expression[set_end - set - 1] = 0;
            FeatureMapSet bits = { { 0, 0 } };
            valid = feature_map_evaluate(state, expression, &bits);
            if (valid && !seen)
            {
                strcpy(written[written_count++], spelling);
                fprintf(out, "BUSTER_CHECK_FEATURE_MAP_CPU(\"%s\", 0x%llxull, 0x%llxull)\n", spelling, (unsigned long long)bits.words[0],
                        (unsigned long long)bits.words[1]);
                *rows += 1;
            }
        }
        else
        {
            fprintf(stderr, "generate_x86_feature_map: can't read the processor at: %.60s\n", row);
        }
    }
    // Each feature's direct implications, ImpliedFeatures##ENUM, which FeatureInfos holds.
    for (uint32_t f = 0; valid && f < state->feature_count; f += 1)
    {
        char wanted[FEATURE_MAP_NAME + 16];
        snprintf(wanted, sizeof(wanted), "ImpliedFeatures%s", state->enumerators[f]);
        int32_t found = -1;
        for (uint32_t s = 0; s < state->set_count; s += 1)
        {
            found = found < 0 && strcmp(state->set_names[s], wanted) == 0 ? (int32_t)s : found;
        }
        valid = found >= 0;
        if (valid)
        {
            fprintf(out, "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"%s\", 0x%llxull, 0x%llxull)\n", state->names[f], (unsigned long long)state->sets[found].words[0],
                    (unsigned long long)state->sets[found].words[1]);
        }
        else
        {
            fprintf(stderr, "generate_x86_feature_map: no %s\n", wanted);
        }
    }
    valid = valid && *rows != 0;
    if (!valid)
    {
        fprintf(stderr, "generate_x86_feature_map: can't read the .cpp input\n");
    }
    free(expression);
    free(text);
    return valid;
}

// The table of `def` and `cpp` written to `out`, its header naming `source` and the inputs' hashes: whether both inputs
// were read whole, each kind of row was found, and every byte of the output was written and flushed.
static bool feature_map_generate(FILE* def, FILE* cpp, FILE* out, const char* source, const char* def_sha256, const char* cpp_sha256)
{
    fprintf(out, "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n");
    fprintf(out, "// Generated by tools/generate_x86_feature_map.c; do not edit. Derived LLVM data, part of the LLVM Project, under\n");
    fprintf(out, "// the Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): the features of\n");
    fprintf(out, "// llvm/include/llvm/TargetParser/X86TargetParser.def (sha256 %s) and the sets and processors of\n", def_sha256);
    fprintf(out, "// llvm/lib/TargetParser/X86TargetParser.cpp (sha256 %s), from\n", cpp_sha256);
    fprintf(out, "// %s; see THIRD_PARTY_NOTICES.md.\n", source);
    fprintf(out, "// What clang's X86TargetInfo::initFeatureMap writes into a feature map (#2093, c_check_feature_map.c), in feature\n");
    fprintf(out, "// indices, FeatureInfos' order: BUSTER_CHECK_FEATURE_MAP_CPU(name, word0, word1) for a processor's features\n");
    fprintf(out, "// (getFeaturesForCPU's), and BUSTER_CHECK_FEATURE_MAP_FEATURE(name, word0, word1) for each feature in index order:\n");
    fprintf(out, "// its name (empty for one no map names) and its direct implications (ImpliedFeatures).\n");
    static FeatureMapState state;
    memset(&state, 0, sizeof(state));
    uint32_t rows = 0;
    const bool read = feature_map_def(def, &state) && feature_map_cpp(cpp, out, &state, &rows);
    const bool written = fflush(out) == 0 && !ferror(out);
    if (!written)
    {
        fprintf(stderr, "generate_x86_feature_map: can't write the table\n");
    }
    return read && written;
}

// A synthetic input of `text` in a temporary file, or NULL.
static FILE* feature_map_input(const char* text)
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

// The self-test: synthetic inputs' rows, then the failures it must report. Returns the exit status.
static int feature_map_self_test(void)
{
    static const char def_text[] = "#ifndef X86_FEATURE\n#define X86_FEATURE(ENUM, STR)\n#endif\n"
                                   "X86_FEATURE_COMPAT(CMOV,               \"cmov\",                  0,   0)\n"
                                   "X86_FEATURE_COMPAT(SSE,                \"sse\",                   2,   3)\n"
                                   "X86_FEATURE_COMPAT(SSE2,               \"sse2\",                  3,   4)\n"
                                   "X86_FEATURE       (64BIT,              \"64bit\")\n"
                                   "X86_FEATURE       (RETPOLINE,          \"\")\n";
    static const char cpp_text[] = "#define X86_FEATURE(ENUM, STRING) \\\n  constexpr FeatureBitset Feature##ENUM = {X86::FEATURE_##ENUM};\n"
                                   "// constexpr FeatureBitset Commented = Bogus;\n"
                                   "constexpr FeatureBitset FeaturesA = FeatureCMOV | FeatureSSE2;\n"
                                   "constexpr FeatureBitset FeaturesB =\n    (FeaturesA & ~FeatureCMOV) | Feature64BIT ^ FeatureSSE2;\n"
                                   "constexpr EnumStringDef<ProcInfo> ProcessorDefs[] = {\n"
                                   "  { {\"one\"}, {CK_One, ~0U, FeaturesA, '\\0', false} },\n"
                                   "  { {\"two\"}, {CK_Two, FEATURE_SSE2, FeaturesB | FeatureSSE, 'A', true} },\n"
                                   "  { {\"one\"}, {CK_One, ~0U, FeatureSSE, '\\0', false} },\n"
                                   "};\n"
                                   "constexpr FeatureBitset ImpliedFeaturesCMOV = {};\n"
                                   "constexpr FeatureBitset ImpliedFeaturesSSE = {};\n"
                                   "constexpr FeatureBitset ImpliedFeaturesSSE2 = FeatureSSE;\n"
                                   "constexpr FeatureBitset ImpliedFeatures64BIT = {};\n"
                                   "constexpr FeatureBitset ImpliedFeaturesRETPOLINE = ImpliedFeaturesSSE2 | FeatureCMOV;\n";
    static const char expected[] = "BUSTER_CHECK_FEATURE_MAP_CPU(\"one\", 0x5ull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_CPU(\"two\", 0xeull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"cmov\", 0x0ull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"sse\", 0x0ull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"sse2\", 0x2ull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"64bit\", 0x0ull, 0x0ull)\n"
                                   "BUSTER_CHECK_FEATURE_MAP_FEATURE(\"\", 0x3ull, 0x0ull)\n";
    bool passed = true;
    {
        FILE* def = feature_map_input(def_text);
        FILE* cpp = feature_map_input(cpp_text);
        FILE* out = tmpfile();
        char buffer[4096] = { 0 };
        const bool generated = def != NULL && cpp != NULL && out != NULL && feature_map_generate(def, cpp, out, "self-test", "0", "0");
        const bool rewound = out != NULL && fseek(out, 0, SEEK_SET) == 0;
        const size_t length = rewound ? fread(buffer, 1, sizeof(buffer) - 1, out) : 0;
        const char* rows = strstr(buffer, "BUSTER_CHECK_FEATURE_MAP_CPU(\"");
        passed = generated && rows != NULL && strcmp(rows, expected) == 0 && length != 0;
        fprintf(stderr, "self-test: rows %s\n", passed ? "ok" : "FAILED");
        if (!passed && rows != NULL)
        {
            fprintf(stderr, "%s", rows);
        }
        fclose(def);
        fclose(cpp);
        fclose(out);
    }
    {
        // A write that fails must fail the run.
        FILE* def = feature_map_input(def_text);
        FILE* cpp = feature_map_input(cpp_text);
        FILE* full = fopen("/dev/full", "w");
        const bool failed = full == NULL || !feature_map_generate(def, cpp, full, "self-test", "0", "0");
        passed = passed && failed;
        fprintf(stderr, "self-test: /dev/full %s\n", failed ? "ok" : "FAILED");
        fclose(def);
        fclose(cpp);
        if (full != NULL)
        {
            fclose(full);
        }
    }
    {
        // A set of a name defined nowhere, and an input that can't be read, must fail the run.
        FILE* def = feature_map_input(def_text);
        FILE* cpp = feature_map_input("constexpr FeatureBitset FeaturesA = FeatureNONE;\nconstexpr EnumStringDef<ProcInfo> ProcessorDefs[] = {\n};\n");
        FILE* out = tmpfile();
        const bool malformed = def != NULL && cpp != NULL && out != NULL && !feature_map_generate(def, cpp, out, "self-test", "0", "0");
        FILE* unreadable = fopen("/", "r");
        FILE* def2 = feature_map_input(def_text);
        const bool failed = unreadable == NULL || (def2 != NULL && out != NULL && !feature_map_generate(def2, unreadable, out, "self-test", "0", "0"));
        passed = passed && malformed && failed;
        fprintf(stderr, "self-test: malformed %s, unreadable %s\n", malformed ? "ok" : "FAILED", failed ? "ok" : "FAILED");
        fclose(def);
        fclose(cpp);
        fclose(def2);
        fclose(out);
        if (unreadable != NULL)
        {
            fclose(unreadable);
        }
    }
    return passed ? 0 : 1;
}

int main(int argc, char** argv)
{
    int status = 2;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
    {
        status = feature_map_self_test();
    }
    else if (argc == 6)
    {
        FILE* def = fopen(argv[1], "r");
        FILE* cpp = fopen(argv[2], "r");
        status = def != NULL && cpp != NULL && feature_map_generate(def, cpp, stdout, argv[3], argv[4], argv[5]) ? 0 : 1;
        if (def != NULL)
        {
            fclose(def);
        }
        if (cpp != NULL)
        {
            fclose(cpp);
        }
    }
    else
    {
        fprintf(stderr, "usage: generate_x86_feature_map --self-test | DEF CPP SOURCE DEF_SHA256 CPP_SHA256\n");
    }
    return status;
}
