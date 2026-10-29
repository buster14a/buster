// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

// #806: source capabilities, not parser recognition. Include after the preprocessor test helpers.
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
    bool result = true;
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
        { "__builtin_ia32_emms", false }, { "__builtin_ia32_tilezero", false }, { "__rdtscx", false },
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
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(intrinsics); i += 1)
        {
            const uint32_t length = (uint32_t)strlen(intrinsics[i].name);
            memcpy(end - length, intrinsics[i].name, length);
            result &= cbuiltin_source_intrinsic(end - length, length) == intrinsics[i].source;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, (size_t)page_size * 2);
    }
    printf("source feature modes, attributes and guarded intrinsics: %s\n", result ? "as expected" : "WRONG");
    return result;
}
