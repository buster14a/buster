// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original generator of src/buster/compiler/c_check_target_versions.inc (#1838): what clang's GNU function
// multiversioning (target with several strings, and target_clones) needs of LLVM's X86 target parser, projected from two
// inputs, llvm/include/llvm/TargetParser/X86TargetParser.def and llvm/lib/TargetParser/X86TargetParser.cpp:
//
// - each X86_FEATURE_COMPAT row of a nonzero priority, the features a version may name (clang's CheckMultiVersionValue
//   and SemaX86::checkTargetClonesAttr refuse a feature whose X86TargetInfo::getFMVPriority is 0): its name, its
//   priority and its ABI value's word and bit in libgcc's CPU model;
// - each X86_MICROARCH_LEVEL row (x86-64-v2 to v4), which a resolver tests as a feature: its name, word and bit;
// - each processor of X86TargetParser.cpp's ProcessorDefs that llvm::X86::parseArchX86 finds (the first row of its
//   name that isn't only cpu_specific's and cpu_dispatch's): its name and its key feature's priority, which
//   getFMVPriority doubles and adds one to for an arch= version, or -1 where the key feature has none (~0U, or a feature
//   of no X86_FEATURE_COMPAT row, where clang's getFeaturePriority is unreachable).
//
// Rows keep the inputs' order. The output is derived LLVM data and keeps the inputs' terms (its header says so); the
// inputs' identity, which the header records, is the caller's: see THIRD_PARTY_NOTICES.md for the recorded run.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_multiversion.c -o build/generate_x86_multiversion
//   build/generate_x86_multiversion --self-test
//   build/generate_x86_multiversion DEF_PATH CPP_PATH "SOURCE" "DEF_SHA256" "CPP_SHA256" > OUTPUT
//
// It succeeds (exit status 0) only when it read every line of both inputs, parsed every row it takes, found rows of each
// kind and wrote and flushed all of its output. --self-test checks that on synthetic inputs: the rows it must print, a
// write to /dev/full, an input that can't be read and a malformed row. Write to a temporary file and replace the
// checked-in table only after the generator succeeds.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MULTIVERSION_FEATURES 256

// A compat feature of the .def: its enumerator (without FEATURE_), its priority.
typedef struct MultiversionFeature MultiversionFeature;
struct MultiversionFeature
{
    char enumerator[64];
    uint32_t priority;
};

// The fields of a .def row after its macro's `(`: its enumerator, its string and its numbers (`count` of them, at most 2).
static bool multiversion_def_row(const char* cursor, char* enumerator, char* name, uint64_t* numbers, uint32_t count)
{
    const char* comma = strchr(cursor, ',');
    const char* string = comma != NULL ? strchr(comma, '"') : NULL;
    const char* closing = string != NULL ? strchr(string + 1, '"') : NULL;
    bool valid = comma != NULL && closing != NULL && (size_t)(comma - cursor) < 64 && (size_t)(closing - string - 1) < 64;
    if (valid)
    {
        const char* start = cursor;
        while (*start == ' ')
        {
            start += 1;
        }
        const char* end = comma;
        while (end > start && end[-1] == ' ')
        {
            end -= 1;
        }
        memcpy(enumerator, start, (size_t)(end - start));
        enumerator[end - start] = 0;
        memcpy(name, string + 1, (size_t)(closing - string - 1));
        name[closing - string - 1] = 0;
    }
    const char* at = closing != NULL ? closing + 1 : NULL;
    for (uint32_t n = 0; valid && n < count; n += 1)
    {
        while (*at == ' ')
        {
            at += 1;
        }
        valid = *at == ',';
        char* next = NULL;
        numbers[n] = valid ? strtoull(at + 1, &next, 10) : 0;
        valid = valid && next != at + 1;
        at = next;
    }
    while (valid && *at == ' ')
    {
        at += 1;
    }
    return valid && *at == ')';
}

// The .def's compat features (into `features`, `*count` of them) and, when `out` isn't NULL, its rows of the table: the
// features of a nonzero priority, then the microarchitecture levels. Whether every line was read and each row parsed.
static bool multiversion_def(FILE* def, FILE* out, MultiversionFeature* features, uint32_t* count, uint32_t* rows)
{
    char line[512];
    bool valid = fseek(def, 0, SEEK_SET) == 0;
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        const char* macro = pass == 0 ? "X86_FEATURE_COMPAT(" : "X86_MICROARCH_LEVEL(";
        valid = fseek(def, 0, SEEK_SET) == 0;
        while (valid && fgets(line, sizeof(line), def) != NULL)
        {
            if (strncmp(line, macro, strlen(macro)) == 0)
            {
                char enumerator[64];
                char name[64];
                uint64_t numbers[2] = { 0, 0 };
                valid = multiversion_def_row(line + strlen(macro), enumerator, name, numbers, 2) && numbers[1] < 128 && numbers[0] < 1024;
                if (!valid)
                {
                    fprintf(stderr, "generate_x86_multiversion: can't read the row: %s", line);
                }
                else if (pass == 0 && *count < MULTIVERSION_FEATURES)
                {
                    strcpy(features[*count].enumerator, enumerator);
                    features[*count].priority = (uint32_t)numbers[0];
                    *count += 1;
                    if (numbers[0] != 0)
                    {
                        fprintf(out, "BUSTER_CHECK_TARGET_VERSION_FEATURE(\"%s\", %u, %u, %u)\n", name, (uint32_t)numbers[0], (uint32_t)(numbers[1] / 32),
                                (uint32_t)(numbers[1] % 32));
                        *rows += 1;
                    }
                }
                else if (pass == 1)
                {
                    fprintf(out, "BUSTER_CHECK_TARGET_VERSION_LEVEL(\"%s\", %u, %u)\n", name, (uint32_t)(numbers[1] / 32), (uint32_t)(numbers[1] % 32));
                    *rows += 1;
                }
                else
                {
                    valid = false;
                }
            }
        }
        valid = valid && !ferror(def);
    }
    if (!valid)
    {
        fprintf(stderr, "generate_x86_multiversion: can't read the .def input\n");
    }
    return valid;
}

// The processors of the .cpp's ProcessorDefs, each `{ {"NAME"}, {CK_KIND, KEY, FEATURES, MANGLING, ONLY} },`, written as
// rows: the first of each name that isn't only cpu_specific's and cpu_dispatch's, with its key feature's priority.
static bool multiversion_cpp(FILE* cpp, FILE* out, const MultiversionFeature* features, uint32_t count, uint32_t* rows)
{
    char line[512];
    // The names written, to keep each name's first row.
    static char written[512][64];
    uint32_t written_count = 0;
    bool valid = fseek(cpp, 0, SEEK_SET) == 0;
    bool inside = false;
    bool ended = false;
    while (valid && !ended && fgets(line, sizeof(line), cpp) != NULL)
    {
        if (!inside)
        {
            inside = strstr(line, "ProcessorDefs[] = {") != NULL;
            continue;
        }
        ended = strncmp(line, "};", 2) == 0;
        const char* row = strstr(line, "{ {\"");
        if (ended || row == NULL)
        {
            continue;
        }
        const char* name = row + 4;
        const char* name_end = strchr(name, '"');
        const char* kind = name_end != NULL ? strstr(name_end, "{CK_") : NULL;
        const char* key = kind != NULL ? strchr(kind, ',') : NULL;
        const char* key_end = key != NULL ? strchr(key + 1, ',') : NULL;
        // The row's last field, after the last comma before its closing brace.
        const char* close = key_end != NULL ? strchr(key_end, '}') : NULL;
        const char* last = NULL;
        for (const char* at = key_end; close != NULL && at < close; at += 1)
        {
            last = *at == ',' ? at : last;
        }
        valid = name_end != NULL && key_end != NULL && last != NULL && (size_t)(name_end - name) < 64 && written_count < 512;
        if (!valid)
        {
            fprintf(stderr, "generate_x86_multiversion: can't read the processor: %s", line);
            continue;
        }
        const char* only = last + 1;
        while (*only == ' ')
        {
            only += 1;
        }
        const bool dispatch_only = strncmp(only, "true", 4) == 0;
        valid = dispatch_only || strncmp(only, "false", 5) == 0;
        char spelling[64];
        memcpy(spelling, name, (size_t)(name_end - name));
        spelling[name_end - name] = 0;
        bool seen = false;
        for (uint32_t w = 0; w < written_count; w += 1)
        {
            seen |= strcmp(written[w], spelling) == 0;
        }
        if (!valid || dispatch_only || seen)
        {
            continue;
        }
        const char* feature = key + 1;
        while (*feature == ' ')
        {
            feature += 1;
        }
        const char* feature_end = feature;
        while (feature_end < key_end && *feature_end != ' ')
        {
            feature_end += 1;
        }
        int32_t priority = -1;
        const bool named = (size_t)(feature_end - feature) > 8 && strncmp(feature, "FEATURE_", 8) == 0;
        valid = named || strncmp(feature, "~0U", 3) == 0;
        for (uint32_t f = 0; named && f < count; f += 1)
        {
            const size_t length = (size_t)(feature_end - feature) - 8;
            priority = strlen(features[f].enumerator) == length && memcmp(features[f].enumerator, feature + 8, length) == 0 ? (int32_t)features[f].priority : priority;
        }
        if (valid)
        {
            strcpy(written[written_count++], spelling);
            fprintf(out, "BUSTER_CHECK_TARGET_VERSION_ARCH(\"%s\", %d)\n", spelling, priority);
            *rows += 1;
        }
        else
        {
            fprintf(stderr, "generate_x86_multiversion: can't read the key feature: %s", line);
        }
    }
    valid = valid && inside && ended && !ferror(cpp);
    if (!valid)
    {
        fprintf(stderr, "generate_x86_multiversion: can't read the .cpp input\n");
    }
    return valid;
}

// The table of `def` and `cpp` written to `out`, its header naming `source` and the inputs' hashes: whether both inputs
// were read whole, each kind of row was found, and every byte of the output was written and flushed.
static bool multiversion_generate(FILE* def, FILE* cpp, FILE* out, const char* source, const char* def_sha256, const char* cpp_sha256)
{
    fprintf(out, "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n");
    fprintf(out, "// Generated by tools/generate_x86_multiversion.c; do not edit. Derived LLVM data, part of the LLVM Project, under\n");
    fprintf(out, "// the Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): the features of\n");
    fprintf(out, "// llvm/include/llvm/TargetParser/X86TargetParser.def (sha256 %s) and the processors of\n", def_sha256);
    fprintf(out, "// llvm/lib/TargetParser/X86TargetParser.cpp (sha256 %s), from\n", cpp_sha256);
    fprintf(out, "// %s; see THIRD_PARTY_NOTICES.md.\n", source);
    fprintf(out, "// What GNU function multiversioning takes (#1838, c_check_target_versions.c): BUSTER_CHECK_TARGET_VERSION_FEATURE(name,\n");
    fprintf(out, "// priority, word, bit) for a feature a version may name, its priority and its bit of libgcc's CPU model (word 0 at\n");
    fprintf(out, "// __cpu_model + 12, words 1 to 3 __cpu_features2[0] to [2]); BUSTER_CHECK_TARGET_VERSION_LEVEL(name, word, bit) for a\n");
    fprintf(out, "// microarchitecture level a resolver tests as a feature; BUSTER_CHECK_TARGET_VERSION_ARCH(name, priority) for a\n");
    fprintf(out, "// processor, its key feature's priority (-1 for none).\n");
    MultiversionFeature features[MULTIVERSION_FEATURES];
    uint32_t count = 0;
    uint32_t def_rows = 0;
    uint32_t cpp_rows = 0;
    const bool read = multiversion_def(def, out, features, &count, &def_rows) && multiversion_cpp(cpp, out, features, count, &cpp_rows) && def_rows != 0 &&
                      cpp_rows != 0;
    const bool written = fflush(out) == 0 && !ferror(out);
    if (!written)
    {
        fprintf(stderr, "generate_x86_multiversion: can't write the table\n");
    }
    return read && written;
}

// A synthetic input of `text` in a temporary file, or NULL.
static FILE* multiversion_input(const char* text)
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

// The self-test: synthetic inputs' rows, then the failures it must report. Returns the exit status.
static int multiversion_self_test(void)
{
    static const char def_text[] =
        "#define X86_FEATURE_COMPAT(ENUM, STR, PRIORITY, ABI_VALUE)\n"
        "X86_FEATURE_COMPAT(CMOV,               \"cmov\",                  0,   0)\n"
        "X86_FEATURE_COMPAT(AVX2,               \"avx2\",                  18, 10)\n"
        "X86_FEATURE_COMPAT(GFNI,               \"gfni\",                  28, 32)\n"
        "X86_MICROARCH_LEVEL(X86_64_V3,   \"x86-64-v3\",                 0, 97)\n"
        "X86_FEATURE       (NF,                 \"nf\")\n";
    static const char cpp_text[] =
        "constexpr EnumStringDef<ProcInfo> ProcessorDefs[] = {\n"
        "  { {\"i386\"}, {CK_i386, ~0U, FeatureX87, '\\0', false} },\n"
        "  { {\"core_i7\"}, {CK_Nehalem, FEATURE_AVX2, FeaturesNehalem, 'P', true} },\n"
        "  { {\"haswell\"}, {CK_Haswell, FEATURE_AVX2, FeaturesHaswell, 'V', false} },\n"
        "  { {\"haswell\"}, {CK_Haswell, FEATURE_GFNI, FeaturesHaswell, 'V', false} },\n"
        "  { {\"knm\"}, {CK_KNM, FEATURE_AVX5124FMAPS, FeaturesKNM, 'j', false} },\n"
        "};\n";
    static const char rows[] =
        "BUSTER_CHECK_TARGET_VERSION_FEATURE(\"avx2\", 18, 0, 10)\n"
        "BUSTER_CHECK_TARGET_VERSION_FEATURE(\"gfni\", 28, 1, 0)\n"
        "BUSTER_CHECK_TARGET_VERSION_LEVEL(\"x86-64-v3\", 3, 1)\n"
        "BUSTER_CHECK_TARGET_VERSION_ARCH(\"i386\", -1)\n"
        "BUSTER_CHECK_TARGET_VERSION_ARCH(\"haswell\", 18)\n"
        "BUSTER_CHECK_TARGET_VERSION_ARCH(\"knm\", -1)\n";
    uint32_t failures = 0;
    FILE* def = multiversion_input(def_text);
    FILE* cpp = multiversion_input(cpp_text);
    FILE* out = tmpfile();
    char text[8192] = { 0 };
    const bool generated = def != NULL && cpp != NULL && out != NULL && multiversion_generate(def, cpp, out, "self-test", "0", "0") && fseek(out, 0, SEEK_SET) == 0;
    const size_t length = generated ? fread(text, 1, sizeof(text) - 1, out) : 0;
    const char* newline = length != 0 ? strstr(text, "\nBUSTER_CHECK_TARGET_VERSION_FEATURE(\"") : NULL;
    const char* found = newline != NULL ? newline + 1 : NULL;
    const bool rows_match = found != NULL && strcmp(found, rows) == 0;
    failures += !rows_match;
    if (!rows_match)
    {
        fprintf(stderr, "generate_x86_multiversion: self-test: the synthetic inputs' rows differ:\n%s", found != NULL ? found : "(none)\n");
    }
    FILE* full = fopen("/dev/full", "w");
    const bool full_refused = def != NULL && cpp != NULL && full != NULL && !multiversion_generate(def, cpp, full, "self-test", "0", "0");
    failures += !full_refused;
    if (!full_refused)
    {
        fprintf(stderr, "generate_x86_multiversion: self-test: a write to /dev/full succeeded\n");
    }
    FILE* directory = fopen("/", "rb");
    FILE* sink = tmpfile();
    const bool unread_refused = directory != NULL && cpp != NULL && sink != NULL && !multiversion_generate(directory, cpp, sink, "self-test", "0", "0");
    failures += !unread_refused;
    if (!unread_refused)
    {
        fprintf(stderr, "generate_x86_multiversion: self-test: an unreadable input succeeded\n");
    }
    FILE* malformed = multiversion_input("X86_FEATURE_COMPAT(AVX2, \"avx2\", 18)\n");
    const bool malformed_refused = malformed != NULL && cpp != NULL && sink != NULL && !multiversion_generate(malformed, cpp, sink, "self-test", "0", "0");
    failures += !malformed_refused;
    if (!malformed_refused)
    {
        fprintf(stderr, "generate_x86_multiversion: self-test: a malformed row succeeded\n");
    }
    FILE* files[] = { def, cpp, out, full, directory, sink, malformed };
    for (uint32_t f = 0; f < sizeof(files) / sizeof(files[0]); f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    fprintf(stderr, "generate_x86_multiversion: self-test: 4 checks, %u failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    const bool self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    FILE* def = !self_test && argc == 6 ? fopen(argv[1], "rb") : NULL;
    FILE* cpp = def != NULL ? fopen(argv[2], "rb") : NULL;
    int status = 1;
    if (self_test)
    {
        status = multiversion_self_test();
    }
    else if (def == NULL || cpp == NULL)
    {
        fprintf(stderr, "usage: generate_x86_multiversion DEF_PATH CPP_PATH SOURCE DEF_SHA256 CPP_SHA256, or generate_x86_multiversion --self-test\n");
    }
    else
    {
        status = multiversion_generate(def, cpp, stdout, argv[3], argv[4], argv[5]) ? 0 : 1;
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
