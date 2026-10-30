// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #806: source capabilities, not parser recognition. Include after the preprocessor test helpers.
// clang 23.1.1's builtin table, whose facts the gates of __has_builtin read (#148).
#include "c_builtin_intrinsic_catalog.c"
static bool pp_test_source_feature_matrix(void)
{
    static const uint32_t standards[] = { 1999, 2011, 2017, 2023 };
    static const struct { const char* name; uint32_t standard; } features[] = {
        { "c_alignas", 2011 }, { "c_alignof", 2011 }, { "c_atomic", 2011 }, { "c_generic_selections", 2011 },
        { "c_static_assert", 2011 }, { "c_thread_local", 2011 }, { "c_fixed_enum", 2023 },
    };
    static const char* const attributes[] = {
        "aligned", "cleanup", "constructor", "destructor", "ext_vector_type", "fallthrough", "gnu_inline", "mode", "naked",
        "overloadable", "packed", "section", "target", "transparent_union", "used", "vector_size", "visibility", "weak",
    };
    static const char* const refused[] = {
        "alias", "ifunc", "weakref", "retain", "common",
        "always_inline", "noinline", "format", "unused", "noreturn", "nodiscard", "maybe_unused", "deprecated", "not_an_attribute",
    };
    static const struct { const char* name; uint32_t value; } c_attributes[] = {
        { "fallthrough", 201910 }, { "gnu::aligned", 1 }, { "gnu::packed", 1 }, { "gnu::vector_size", 1 },
        { "gnu::section", 1 }, { "gnu::visibility", 1 }, { "clang::ext_vector_type", 1 },
        { "gnu::cleanup", 1 }, { "gnu::constructor", 1 }, { "gnu::destructor", 1 }, { "gnu::weak", 1 }, { "clang::overloadable", 1 },
        { "nodiscard", 0 }, { "maybe_unused", 0 }, { "deprecated", 0 },
        { "other::aligned", 0 }, { "aligned", 0 }, { "gnu::packed_typo", 0 }, { "gnu::overloadable", 0 },
    };
    // Independent of the admission table: these exact calls have ordinary checked signatures and Tier 0 consumers.
    static const char* const fortified[] = {
        "__builtin___memcpy_chk", "__builtin___memmove_chk", "__builtin___mempcpy_chk", "__builtin___memset_chk",
        "__builtin___strcpy_chk", "__builtin___stpcpy_chk", "__builtin___strncpy_chk", "__builtin___stpncpy_chk",
        "__builtin___strcat_chk", "__builtin___strncat_chk", "__builtin___sprintf_chk", "__builtin___snprintf_chk",
        "__builtin___vsprintf_chk", "__builtin___vsnprintf_chk",
    };
    static const char* const refused_fortify[] = {
        "__builtin___memccpy_chk", "__builtin___memcpy_chkk", "__builtin___memcpy_ch", "__builtin___MEMCPY_chk",
    };
    bool result = BUSTER_ARRAY_LENGTH(fortified) == CBUILTIN_FORTIFY_COUNT;
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(standards); mode += 1)
    {
        for (uint32_t gnu = 0; gnu < 2; gnu += 1)
        {
            const PPOptions options = { .language = { .standard = standards[mode], .gnu = gnu != 0 } };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(features); i += 1)
            {
                const uint8_t* name = (const uint8_t*)features[i].name;
                const uint32_t length = (uint32_t)strlen(features[i].name);
                result &= pp_feature(&options, PP_BUILTIN_HAS_FEATURE, name, length) == (standards[mode] >= features[i].standard);
                result &= pp_feature(&options, PP_BUILTIN_HAS_EXTENSION, name, length) == 1;
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(attributes); i += 1)
            {
                result &= pp_feature(&options, PP_BUILTIN_HAS_ATTRIBUTE, (const uint8_t*)attributes[i], (uint32_t)strlen(attributes[i])) == 1;
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
            {
                result &= pp_feature(&options, PP_BUILTIN_HAS_ATTRIBUTE, (const uint8_t*)refused[i], (uint32_t)strlen(refused[i])) == 0;
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(c_attributes); i += 1)
            {
                result &= pp_feature(&options, PP_BUILTIN_HAS_C_ATTRIBUTE, (const uint8_t*)c_attributes[i].name,
                                     (uint32_t)strlen(c_attributes[i].name)) == c_attributes[i].value;
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fortified); i += 1)
            {
                const uint8_t* name = (const uint8_t*)fortified[i];
                const uint32_t length = (uint32_t)strlen(fortified[i]);
                result &= cbuiltin_fortify_source(name, length);
                result &= pp_feature(&options, PP_BUILTIN_HAS_BUILTIN, name, length) == 1;
                result &= pp_feature(&options, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == 0;
            }
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused_fortify); i += 1)
            {
                const uint8_t* name = (const uint8_t*)refused_fortify[i];
                const uint32_t length = (uint32_t)strlen(refused_fortify[i]);
                result &= !cbuiltin_fortify_source(name, length);
                result &= pp_feature(&options, PP_BUILTIN_HAS_BUILTIN, name, length) == 0;
                result &= pp_feature(&options, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == 0;
            }
            static const char* const unknown_features[] = { "c_static_assert_typo", "cxx_exceptions", "address_sanitizer", "objc_arc", "c_blocks" };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unknown_features); i += 1)
            {
                const uint8_t* name = (const uint8_t*)unknown_features[i];
                const uint32_t length = (uint32_t)strlen(unknown_features[i]);
                result &= pp_feature(&options, PP_BUILTIN_HAS_FEATURE, name, length) == 0;
                result &= pp_feature(&options, PP_BUILTIN_HAS_EXTENSION, name, length) == 0;
            }
        }
    }
    const PPFeature* const tables[] = { pp_implemented_attribute, pp_implemented_c_attribute, pp_implemented_feature, pp_implemented_builtin };
    const uint32_t counts[] = { BUSTER_ARRAY_LENGTH(pp_implemented_attribute), BUSTER_ARRAY_LENGTH(pp_implemented_c_attribute),
                               BUSTER_ARRAY_LENGTH(pp_implemented_feature), BUSTER_ARRAY_LENGTH(pp_implemented_builtin) };
    for (uint32_t table = 0; table < BUSTER_ARRAY_LENGTH(tables); table += 1)
    {
        for (uint32_t i = 1; i < counts[table]; i += 1)
        {
            result &= strcmp(tables[table][i - 1].name, tables[table][i].name) < 0;
        }
    }
    static const struct { const char* name; bool source; } intrinsics[] = {
        { "__rdtsc", true }, { "__builtin_ia32_pause", true }, { "__builtin_rotateleft32", true },
        { "__builtin_assume", true }, { "__builtin_unpredictable", true }, { "__builtin_ia32_selectd_512", true },
        { "__builtin_ia32_palignr256", true }, { "__builtin_ia32_pblendw128", true }, { "__builtin_ia32_permdi256", true },
        { "__builtin_ia32_pshufd256", true }, { "__builtin_ia32_shufpd", true },
        { "__builtin_ia32_pblendw256", true }, { "__builtin_ia32_pshufd257", false },
        // EMMS has both a shared SYSTEM lowering row and a declaration in the frozen source scope.
        { "__builtin_ia32_emms", true }, { "__builtin_ia32_tilezero", false }, { "__rdtscx", false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(intrinsics); i += 1)
    {
        result &= cbuiltin_source_intrinsic((const uint8_t*)intrinsics[i].name, (uint32_t)strlen(intrinsics[i].name)) == intrinsics[i].source;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = page_size > 0 ? mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool guarded = mapping != MAP_FAILED && mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) == 0;
    result &= guarded;
    if (guarded)
    {
        uint8_t* end = mapping + page_size;
        result &= !cbuiltin_source_intrinsic(end, 0) && !cbuiltin_source_intrinsic(end, UINT32_MAX) && !cbuiltin_source_intrinsic(NULL, 1);
        result &= !cbuiltin_fortify_source(end, 0) && !cbuiltin_fortify_source(end, UINT32_MAX) && !cbuiltin_fortify_source(NULL, 1);
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(intrinsics); i += 1)
        {
            const uint32_t length = (uint32_t)strlen(intrinsics[i].name);
            memcpy(end - length, intrinsics[i].name, length);
            result &= cbuiltin_source_intrinsic(end - length, length) == intrinsics[i].source;
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fortified); i += 1)
        {
            const uint32_t length = (uint32_t)strlen(fortified[i]);
            memcpy(end - length, fortified[i], length);
            result &= cbuiltin_fortify_source(end - length, length);
        }
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused_fortify); i += 1)
        {
            const uint32_t length = (uint32_t)strlen(refused_fortify[i]);
            memcpy(end - length, refused_fortify[i], length);
            result &= !cbuiltin_fortify_source(end - length, length);
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page_size * 2);
    }
    // Recognition in the frozen oracle remains separate: Clang knows this name, the shipped source path does not.
    const PPOptions oracle = { .clang_features = true, .language = { .standard = 2017, .gnu = true } };
    result &= pp_feature(&oracle, PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)"__builtin___memccpy_chk", sizeof("__builtin___memccpy_chk") - 1) == 1;
    printf("source feature modes, attributes and guarded intrinsic/fortify names: %s\n", result ? "as expected" : "WRONG");
    return result;
}

// #148: __has_builtin of every builtin buster implements answers as clang's does: where clang makes the name a builtin
// of the unit's language modes and its required target features hold (pp_builtin_language, pp_builtin_features), and 0
// for every name buster doesn't implement. Each of clang's 3,775 builtin names, in eleven target and language
// configurations, against `clang -E`'s own answers; then the scopes' and the catalog's gates against clang's table.
static bool pp_test_builtin_targets(const char* directory)
{
    static const char* const configurations[] = {
        "-std=gnu17 -march=x86-64",
        "-std=gnu17 -march=x86-64-v2",
        "-std=gnu17 -march=x86-64-v3",
        "-std=gnu17 -march=x86-64-v4",
        "-std=gnu17 -march=znver5",
        "-std=c23 -march=znver5",
        "-std=c17 -march=znver5 -mno-avx512f",
        "-std=gnu17 -march=x86-64 -mraoint -mavxneconvert -mxop",
        "-std=gnu17 -march=sapphirerapids",
        "-std=gnu23 -march=diamondrapids",
        "-std=gnu17 -march=x86-64-v3 -mno-avx2 -mfma4",
    };
    char path[4200];
    snprintf(path, sizeof(path), "%s/builtin-targets.c", directory);
    // Each name on a line of its own: `HB name answer`, as clang -E prints it.
    const uint32_t count = BUSTER_BUILTIN_INTRINSIC_COUNT;
    const size_t capacity = (size_t)count * 160;
    char* source = malloc(capacity);
    bool result = source != 0;
    size_t used = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        used += (size_t)snprintf(source + used, capacity - used, "HB %s __has_builtin(%s)\n", info->name, info->name);
        result = used < capacity;
    }
    result = result && pp_test_write_file(path, source);
    uint64_t implemented_answers = 0;
    uint64_t compared = 0;
    uint64_t available = 0;
    for (uint32_t c = 0; result && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        char text[512];
        snprintf(text, sizeof(text), "%s", configurations[c]);
        char* arguments[BUSTER_PP_TEST_MAX_ARGUMENTS + 1];
        int argument_count = 0;
        for (char* token = strtok(text, " "); token != 0 && argument_count < BUSTER_PP_TEST_MAX_ARGUMENTS; token = strtok(0, " "))
        {
            arguments[argument_count] = token;
            argument_count += 1;
        }
        arguments[argument_count] = path;
        Arena arena = arena_reserve(1ull << 20);
        PPOptions options;
        const char* input;
        const char* output;
        bool markers;
        const bool parsed = pp_parse_options(argument_count + 1, arguments, &options, &arena, &input, &output, &markers);
        size_t length = 0;
        char* clang_text = parsed ? pp_test_clang(arguments, argument_count, path, 0, &length) : 0;
        result = parsed && clang_text != 0;
        // clang's answers, in the table's order: the lines that start with HB.
        uint32_t index = 0;
        for (char* line = clang_text; result && line != 0 && line < clang_text + length;)
        {
            char* end = memchr(line, '\n', (size_t)(clang_text + length - line));
            end = end != 0 ? end : clang_text + length;
            if (end - line > 3 && memcmp(line, "HB ", 3) == 0)
            {
                const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(index);
                const char* answer = line + 3 + info->name_length + 1;
                result = index < count && (size_t)(end - line) > 3 + info->name_length && memcmp(line + 3, info->name, info->name_length) == 0;
                const bool clang_yes = result && *answer != '0';
                const uint8_t* name = (const uint8_t*)info->name;
                const bool implemented = cbuiltin_has_capability(name, info->name_length, BUSTER_CBUILTIN_CAP_SOURCE) ||
                                         cbuiltin_source_intrinsic(name, info->name_length) || cbuiltin_fortify_source(name, info->name_length);
                const uint32_t ours = pp_feature(&options, PP_BUILTIN_HAS_BUILTIN, name, info->name_length);
                const bool right = implemented ? (ours != 0) == clang_yes : ours == 0;
                if (result && !right)
                {
                    printf("builtin targets: %s under %s: %u, clang %s\n", info->name, configurations[c], ours, clang_yes ? "1" : "0");
                }
                result &= right;
                implemented_answers += implemented;
                available += implemented && ours != 0;
                compared += 1;
                index += 1;
            }
            line = end + 1;
        }
        result &= index == count;
        free(clang_text);
        arena_release(&arena);
    }
    // The gates' facts against clang's table: each source scope's row (the frozen inventory's and the supplementary
    // names', whose features c_builtin_supplementary_scope.inc writes out) and each catalog name's language modes.
    uint32_t facts = 0;
    for (uint32_t i = 0; result && i < count; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_at(i);
        const uint8_t* name = (const uint8_t*)info->name;
        const CBuiltinSourceScope* scope = cbuiltin_source_scope(name, info->name_length);
        if (scope != 0)
        {
            result &= strcmp(scope->features, info->features) == 0 && strcmp(scope->languages, info->language_token) == 0;
            facts += 1;
        }
        if (cbuiltin_resolve(info->name, info->name_length) != CBUILTIN_NONE)
        {
            result &= strcmp(cbuiltin_languages(name, info->name_length), info->language_token) == 0 && info->features[0] == 0;
            facts += 1;
        }
    }
    unlink(path);
    free(source);
    printf("builtin targets: %llu answers in %lu configurations, %llu of implemented builtins (%llu available), as clang's; %u gates' facts as "
           "clang's table: %s\n",
           (unsigned long long)compared, (unsigned long)BUSTER_ARRAY_LENGTH(configurations), (unsigned long long)implemented_answers,
           (unsigned long long)available, facts, result ? "pass" : "FAIL");
    return result;
}
