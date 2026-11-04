// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original generator of src/buster/compiler/c_builtin_cpu.inc (#148): the names __builtin_cpu_supports and
// __builtin_cpu_is take and the CPU model's bits and values clang's code tests them against, projected from one input,
// LLVM's llvm/include/llvm/TargetParser/X86TargetParser.def. Its X86_FEATURE_COMPAT and X86_MICROARCH_LEVEL rows are the
// names clang's X86TargetInfo::validateCpuSupports takes, each with its ABI value (word ABI_VALUE / 32 of the model's
// features, bit ABI_VALUE % 32); its X86_VENDOR, X86_CPU_TYPE and X86_CPU_SUBTYPE rows, aliases among them, are the
// names validateCpuIs takes, each with the value clang compares the model's vendor, type or subtype with. Rows keep the
// input's order, the features' first. The output is derived LLVM data and keeps the input's terms (its header says so);
// the input's identity, which the header records, is the caller's: see THIRD_PARTY_NOTICES.md for the recorded run.
//
//   cc -std=c17 -O2 -Wall -Wextra -Werror tools/generate_x86_cpu_model.c -o build/generate_x86_cpu_model
//   build/generate_x86_cpu_model --self-test
//   build/generate_x86_cpu_model DEF_PATH "SOURCE" "SHA256" > OUTPUT
//
// It succeeds (exit status 0) only when it read every line of the input, parsed every row it takes, found rows of both
// kinds and wrote and flushed all of its output: a full disk or a closed pipe fails it (#1126's review). --self-test
// checks that on a synthetic input: the rows it must print, a write to /dev/full, an input that can't be read and a
// malformed row. Write to a temporary file and replace the checked-in table only after the generator succeeds.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A row kind the table takes: its macro, its argument count, and what it is (a feature, or a CPU name's field: 0 the
// vendor, 1 the type, 2 the subtype).
typedef struct CpuModelKind CpuModelKind;
struct CpuModelKind
{
    const char* macro;
    uint32_t arguments;
    int32_t field;
};

static const CpuModelKind cpu_model_kinds[] =
{
    { "X86_FEATURE_COMPAT", 4, -1 },
    { "X86_MICROARCH_LEVEL", 4, -1 },
    { "X86_VENDOR", 3, 0 },
    { "X86_CPU_TYPE", 3, 1 },
    { "X86_CPU_SUBTYPE", 3, 2 },
};

// A row's string and ABI value, from `line` (a row of `kind`, at its macro's end): whether it parsed.
static bool cpu_model_parse(const char* cursor, const CpuModelKind* kind, char* name, size_t capacity, uint64_t* value)
{
    bool valid = *cursor == '(';
    uint32_t commas = 0;
    const char* string = NULL;
    const char* last = NULL;
    for (const char* at = cursor; valid && *at != 0 && *at != ')'; at += 1)
    {
        commas += *at == ',';
        string = string == NULL && *at == '"' ? at + 1 : string;
        last = *at == ',' ? at + 1 : last;
    }
    const char* closing = string != NULL ? strchr(string, '"') : NULL;
    valid = valid && commas + 1 == kind->arguments && closing != NULL && (size_t)(closing - string) < capacity && last != NULL;
    if (valid)
    {
        memcpy(name, string, (size_t)(closing - string));
        name[closing - string] = 0;
        char* end = NULL;
        *value = strtoull(last, &end, 10);
        while (end != NULL && (*end == ' ' || *end == '\t'))
        {
            end += 1;
        }
        valid = end != NULL && end != last && *end == ')';
    }
    return valid;
}

// Writes the rows of `def` whose kinds are features (`features`) or CPU names (!`features`) to `out`, in the input's
// order: whether every line was read and every row taken parsed.
static bool cpu_model_rows(FILE* def, FILE* out, bool features, uint32_t* count)
{
    char line[512];
    bool valid = fseek(def, 0, SEEK_SET) == 0;
    while (valid && fgets(line, sizeof(line), def) != NULL)
    {
        for (uint32_t k = 0; valid && k < sizeof(cpu_model_kinds) / sizeof(cpu_model_kinds[0]); k += 1)
        {
            const CpuModelKind* kind = cpu_model_kinds + k;
            const size_t length = strlen(kind->macro);
            if ((kind->field < 0) == features && strncmp(line, kind->macro, length) == 0 && line[length] == '(')
            {
                char name[128];
                uint64_t value = 0;
                valid = cpu_model_parse(line + length, kind, name, sizeof(name), &value) && (features ? value < 128 : value <= UINT32_MAX);
                if (valid && features)
                {
                    fprintf(out, "BUSTER_CBUILTIN_CPU_FEATURE(\"%s\", %u, %u)\n", name, (uint32_t)(value / 32), (uint32_t)(value % 32));
                }
                else if (valid)
                {
                    fprintf(out, "BUSTER_CBUILTIN_CPU_NAME(\"%s\", %d, %u)\n", name, kind->field, (uint32_t)value);
                }
                else
                {
                    fprintf(stderr, "generate_x86_cpu_model: can't read the row: %s", line);
                }
                *count += valid;
            }
        }
    }
    const bool read = !ferror(def);
    if (!read)
    {
        fprintf(stderr, "generate_x86_cpu_model: can't read the input\n");
    }
    return valid && read;
}

// The table of `def` written to `out`, its header naming `source` and `sha256`: whether the input was read whole, it
// had rows of both kinds, and every byte of the output was written and flushed.
static bool cpu_model_generate(FILE* def, FILE* out, const char* source, const char* sha256)
{
    fprintf(out, "// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n");
    fprintf(out, "// Generated by tools/generate_x86_cpu_model.c; do not edit. Derived LLVM data, part of the LLVM Project, under the\n");
    fprintf(out, "// Apache License v2.0 with LLVM Exceptions (LICENSES/LLVM.txt): the names and ABI values of\n");
    fprintf(out, "// llvm/include/llvm/TargetParser/X86TargetParser.def, from\n");
    fprintf(out, "// %s,\n", source);
    fprintf(out, "// sha256 %s; see THIRD_PARTY_NOTICES.md.\n", sha256);
    fprintf(out, "// The names __builtin_cpu_supports and __builtin_cpu_is take (#148, c_check_builtin_misc.c), and how clang's code tests\n");
    fprintf(out, "// each against libgcc's (and compiler-rt's) CPU model: BUSTER_CBUILTIN_CPU_FEATURE(name, word, bit) for a feature, the\n");
    fprintf(out, "// bit of word 0 (__cpu_model.features[0], at __cpu_model + 12) or of words 1 to 3 (__cpu_features2[0] to [2]), and\n");
    fprintf(out, "// BUSTER_CBUILTIN_CPU_NAME(name, field, value) for a vendor (field 0, __cpu_model.vendor), a CPU type (1, its type) or\n");
    fprintf(out, "// a subtype (2, its subtype) and the value clang compares it with. test-checker-driver compares each with clang's\n");
    fprintf(out, "// code (c_check_library_driver_test.c's check_test_cpu_model_runs, which runs both builds' tests of every name over\n");
    fprintf(out, "// every bit and value of the model).\n");
    uint32_t features = 0;
    uint32_t names = 0;
    const bool read = cpu_model_rows(def, out, true, &features) && cpu_model_rows(def, out, false, &names) && features != 0 && names != 0;
    const bool written = fflush(out) == 0 && !ferror(out);
    if (!written)
    {
        fprintf(stderr, "generate_x86_cpu_model: can't write the table\n");
    }
    return read && written;
}

// A synthetic input of `text` in a temporary file, or NULL.
static FILE* cpu_model_input(const char* text)
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

// The self-test (#1126's review): a synthetic input's rows, then the failures it must report. Returns the exit status.
static int cpu_model_self_test(void)
{
    static const char input[] =
        "#define X86_VENDOR(ENUM, STR, ABI_VALUE)\n"
        "X86_VENDOR(VENDOR_INTEL, \"intel\", 1)\n"
        "X86_CPU_TYPE(INTEL_BONNELL,          \"bonnell\",          1)\n"
        "X86_CPU_SUBTYPE(INTEL_COREI7_NEHALEM, \"nehalem\", 1)\n"
        "X86_FEATURE_COMPAT(CMOV,            \"cmov\",                   0, 0)\n"
        "X86_MICROARCH_LEVEL(X86_64_V2,        \"x86-64-v2\",              0, 96)\n"
        "X86_FEATURE       (NF,                 \"nf\")\n";
    static const char rows[] =
        "BUSTER_CBUILTIN_CPU_FEATURE(\"cmov\", 0, 0)\n"
        "BUSTER_CBUILTIN_CPU_FEATURE(\"x86-64-v2\", 3, 0)\n"
        "BUSTER_CBUILTIN_CPU_NAME(\"intel\", 0, 1)\n"
        "BUSTER_CBUILTIN_CPU_NAME(\"bonnell\", 1, 1)\n"
        "BUSTER_CBUILTIN_CPU_NAME(\"nehalem\", 2, 1)\n";
    uint32_t failures = 0;
    // The rows, after the header, in a temporary file.
    FILE* def = cpu_model_input(input);
    FILE* out = tmpfile();
    char text[4096] = { 0 };
    const bool generated = def != NULL && out != NULL && cpu_model_generate(def, out, "self-test", "0") && fseek(out, 0, SEEK_SET) == 0;
    const size_t length = generated ? fread(text, 1, sizeof(text) - 1, out) : 0;
    // The first row is the first line that starts with a row's macro, which the header's comment names too.
    const char* newline = length != 0 ? strstr(text, "\nBUSTER_CBUILTIN_CPU_") : NULL;
    const char* found = newline != NULL ? newline + 1 : NULL;
    const bool rows_match = found != NULL && strcmp(found, rows) == 0;
    failures += !rows_match;
    if (!rows_match)
    {
        fprintf(stderr, "generate_x86_cpu_model: self-test: the synthetic input's rows differ:\n%s", found != NULL ? found : "(none)\n");
    }
    // A full disk: the output's write or flush fails, and so must the generator.
    FILE* full = fopen("/dev/full", "w");
    const bool full_refused = def != NULL && full != NULL && !cpu_model_generate(def, full, "self-test", "0");
    failures += !full_refused;
    if (!full_refused)
    {
        fprintf(stderr, "generate_x86_cpu_model: self-test: a write to /dev/full succeeded\n");
    }
    // An input that can't be read: a directory, whose read fails (ferror).
    FILE* directory = fopen("/", "rb");
    FILE* sink = tmpfile();
    const bool unread_refused = directory != NULL && sink != NULL && !cpu_model_generate(directory, sink, "self-test", "0");
    failures += !unread_refused;
    if (!unread_refused)
    {
        fprintf(stderr, "generate_x86_cpu_model: self-test: an unreadable input succeeded\n");
    }
    // A malformed row: a feature without its ABI value.
    FILE* malformed = cpu_model_input("X86_VENDOR(VENDOR_INTEL, \"intel\", 1)\nX86_FEATURE_COMPAT(CMOV, \"cmov\", 0)\n");
    const bool malformed_refused = malformed != NULL && sink != NULL && !cpu_model_generate(malformed, sink, "self-test", "0");
    failures += !malformed_refused;
    if (!malformed_refused)
    {
        fprintf(stderr, "generate_x86_cpu_model: self-test: a malformed row succeeded\n");
    }
    FILE* files[] = { def, out, full, directory, sink, malformed };
    for (uint32_t f = 0; f < sizeof(files) / sizeof(files[0]); f += 1)
    {
        if (files[f] != NULL)
        {
            fclose(files[f]);
        }
    }
    fprintf(stderr, "generate_x86_cpu_model: self-test: 4 checks, %u failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    const bool self_test = argc == 2 && strcmp(argv[1], "--self-test") == 0;
    FILE* def = !self_test && argc == 4 ? fopen(argv[1], "rb") : NULL;
    int status = 1;
    if (self_test)
    {
        status = cpu_model_self_test();
    }
    else if (def == NULL)
    {
        fprintf(stderr, "usage: generate_x86_cpu_model DEF_PATH SOURCE SHA256, or generate_x86_cpu_model --self-test\n");
    }
    else
    {
        status = cpu_model_generate(def, stdout, argv[2], argv[3]) ? 0 : 1;
        fclose(def);
    }
    return status;
}
