from pathlib import Path
root=Path.cwd(); d=root/'src/buster/compiler'
license='// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.\n'
(d/'c_preprocessor_feature_test.c').write_text(license+r'''
// #806: source capabilities, not parser recognition. Include after the preprocessor test helpers.
static bool pp_test_source_feature_matrix(void)
{
    static const uint32_t standards[] = { 1999, 2011, 2017, 2023 };
    static const struct { const char* name; uint32_t standard; } features[] = {
        { "c_alignas", 2011 }, { "c_alignof", 2011 }, { "c_atomic", 2011 }, { "c_generic_selections", 2011 },
        { "c_static_assert", 2011 }, { "c_thread_local", 2011 }, { "c_fixed_enum", 2023 },
    };
    static const char* const attributes[] = {
        "aligned", "ext_vector_type", "fallthrough", "gnu_inline", "mode", "naked", "overloadable", "packed",
        "section", "target", "transparent_union", "used", "vector_size", "visibility",
    };
    static const char* const refused[] = {
        "cleanup", "alias", "ifunc", "constructor", "destructor", "weak", "weakref", "retain", "common",
        "always_inline", "noinline", "format", "unused", "noreturn", "nodiscard", "maybe_unused", "deprecated", "not_an_attribute",
    };
    static const struct { const char* name; uint32_t value; } c_attributes[] = {
        { "fallthrough", 201910 }, { "gnu::aligned", 1 }, { "gnu::packed", 1 }, { "gnu::vector_size", 1 },
        { "gnu::section", 1 }, { "gnu::visibility", 1 }, { "clang::ext_vector_type", 1 },
        { "nodiscard", 0 }, { "maybe_unused", 0 }, { "deprecated", 0 }, { "gnu::cleanup", 0 },
        { "other::aligned", 0 }, { "aligned", 0 }, { "gnu::packed_typo", 0 },
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
''')
p=d/'c_preprocessor_test.c';s=p.read_text();s=s.replace('static bool pp_test_implemented_features(const char* directory)', '#include "c_preprocessor_feature_test.c"\n\nstatic bool pp_test_implemented_features(const char* directory)')
a=s.index('static bool pp_test_implemented_features(');b=s.index('// This explicit acceptance inventory',a);part=s[a:b]
part=part.replace('    bool result = true;', '    bool result = pp_test_source_feature_matrix();')
part=part.replace(r'"E __has_attribute(packed_typo) __has_c_attribute(other::packed) __has_c_attribute(packed) __has_c_attribute(gnu::__packed__)\n";',r'''"E __has_attribute(packed_typo) __has_c_attribute(other::packed) __has_c_attribute(packed) __has_c_attribute(gnu::__packed__)\n"
        "F __has_attribute(__aligned__) __has_c_attribute(gnu::__aligned__) __has_feature(__c_static_assert__) __has_c_attribute(__fallthrough__)\n"
        "G __has_attribute(cleanup) __has_builtin(__atomic_load) __has_c_attribute(nodiscard)\n"
        "#define aligned packed_typo\n#define ATTR aligned\nH __has_attribute(aligned) __has_attribute(ATTR)\n";''')
old='A 1 1 1 1 1 1 1 1 0 1 B 0 1 C 0 1 D 1 0 E 0 0 0 1'
part=part.replace(old+r'\n',old+r' F 1 1 1 201910 G 0 0 0 H 1 0\n',1);part=part.replace(old+r'\n',old+r' F 1 1 1 201910 G 1 1 202003 H 1 0\n',1)
s=s[:a]+part+s[b:];p.write_text(s)
p=d/'c_check_program_x86_test.c';s=p.read_text();at=s.index('// Every rule\'s row, with every feature')
s=s[:at]+r'''// #806: answers must agree with the checker's source scope and actual native lowering.
static void check_test_program_intrinsic_features(CheckTest* test)
{
    uint32_t advertised = 0;
    for (uint32_t i = 0; i < BUSTER_BUILTIN_INTRINSIC_OWN_MAIN_COUNT; i += 1)
    {
        const BuiltinIntrinsicInfo* info = builtin_intrinsic_own_main_infos + i;
        const bool special = strcmp(info->name, "__builtin_assume") == 0 || strcmp(info->name, "__builtin_unpredictable") == 0;
        const bool implemented = special || check_x86_lower_rule(info->name, info->name_length) != NULL ||
                                 cbuiltin_x86_helper_kind(info->name, info->name_length) != CBUILTIN_X86_HELPER_NONE;
        const bool expected = builtin_intrinsic_own_main_prototype_in_scope(info->name, info->name_length) && implemented;
        const bool actual = cbuiltin_source_intrinsic((const uint8_t*)info->name, info->name_length);
        char description[192];
        snprintf(description, sizeof(description), "intrinsic query agrees with source scope and native lowering: %s", info->name);
        check_test_expect(test, actual == expected, description);
        advertised += actual;
    }
    check_test_expect(test, advertised > 100, "native intrinsic capabilities are not an empty or helper-only list");
}

'''+s[at:]
s=s.replace('static void check_test_program_x86_rows(CheckTest* test)\n{','static void check_test_program_x86_rows(CheckTest* test)\n{\n    check_test_program_intrinsic_features(test);');p.write_text(s)
p=d/'c_check_program_test.c';s=p.read_text();at=s.index('    // A feature query is useful only if the selected layout')
s=s[:at]+r'''    check_test_program_source(test,
        "#if !__has_attribute(aligned) || !__has_attribute(vector_size) || !__has_attribute(section) || !__has_attribute(visibility)\n"
        "#error implemented declaration attributes must be advertised\n#endif\n"
        "#if !__has_feature(c_static_assert) || !__has_feature(c_alignas) || !__has_feature(c_alignof) || !__has_feature(c_generic_selections) || !__has_feature(c_atomic) || !__has_feature(c_thread_local)\n"
        "#error implemented C11 features must be advertised\n#endif\n"
        "#if !__has_builtin(__builtin_expect) || !__has_builtin(__builtin_popcount) || !__has_builtin(__builtin_add_overflow) || !__has_builtin(__builtin_memcpy) || !__has_builtin(__c11_atomic_fetch_add) || !__has_builtin(__atomic_fetch_xor) || !__has_builtin(__sync_fetch_and_add) || !__has_builtin(__builtin_fabs) || !__has_builtin(__builtin_constant_p) || !__has_builtin(__builtin_choose_expr) || !__has_builtin(__builtin_types_compatible_p)\n"
        "#error implemented source builtins must be advertised\n#endif\n"
        "#if !__has_constexpr_builtin(__builtin_bswap32) || !__has_constexpr_builtin(__builtin_popcount)\n"
        "#error source constant evaluation must be advertised\n#endif\n"
        "typedef int v4si __attribute__((vector_size(16)));typedef int wide __attribute__((aligned(32)));"
        "_Static_assert(sizeof(v4si)==16 && _Alignof(wide)==32,\"attribute effects\");"
        "_Static_assert(__builtin_popcount(31)==5 && __builtin_bswap32(0x01020304U)==0x04030201U,\"builtin constants\");"
        "__attribute__((section(\".query_data\"),visibility(\"hidden\"))) int seed=7;"
        "_Thread_local int local=3;"
        "int tested(int n){_Alignas(32) int a[4]={n,2,3,4};int b[4];_Atomic(int) value=5;int other=1,out=0;"
        "__builtin_memcpy(b,a,sizeof(a));int old=__c11_atomic_fetch_add(&value,n,5);"
        "int prior=__atomic_fetch_xor(&other,3,5);other+=prior;prior=__sync_fetch_and_add(&other,2);other+=prior;"
        "int overflow=__builtin_add_overflow(n,9,&out);int picked=_Generic(n,int:3,default:9);"
        "int constant=__builtin_choose_expr(__builtin_types_compatible_p(int,int),11,17)+__builtin_constant_p(42);"
        "double magnitude=__builtin_fabs((double)(n%97));local=n%13;"
        "return b[0]+(int)__builtin_popcount((unsigned)n)+__builtin_expect(out,0)+old+value+other+overflow+picked+constant+(int)magnitude+seed+local;}",
        "implemented-feature-query-native", clang);
'''+s[at:];p.write_text(s)
p=root/'docs/c-preprocessor.md';s=p.read_text();a=s.index('- **By default, what buster implements**');b=s.index('- **In the oracle\'s mode',a)
s=s[:a]+'''- **By default, what buster implements** (`pp_implemented_*`, #806). GNU
  attributes advertise verified declaration/type, control and native consumers:
  `aligned`, `ext_vector_type`, `fallthrough`, `gnu_inline`, `mode`, `naked`,
  `overloadable`, `packed`, `section`, `target`, `transparent_union`, `used`,
  `vector_size` and `visibility`. Paired underscores are normalized. C attribute
  queries accept the corresponding `gnu::` names (Clang-specific type/overload
  attributes use `clang::`) and return **201910** for standard `fallthrough`.
  `c_alignas`, `c_alignof`, `c_atomic`, `c_generic_selections`, `c_static_assert`
  and `c_thread_local` are features from C11; `c_fixed_enum` from C23.
  `__has_extension` also exposes these implemented constructs in older modes.
  Builtins use the shared catalog's explicit source and constant-expression
  capabilities, plus the preprocessor's four `__is_target_*` builtins. Native
  intrinsic names come from the actual x86 lowering rows/leaves, intersected
  with the checker's frozen source scope; the full Clang recognition catalog
  is not an implementation list.
'''+s[b:]
needle='`__has_warning` and `__has_declspec_attribute` answer as clang does in both'
s=s.replace(needle,'''Source capability means a checked implementation through the shipped Tier 0
source pipeline, not the experimental Tier 1 action API. It does not enable
instruction-set features or waive argument/type/ABI restrictions: native
lowering still refuses unsupported cases. `__has_constexpr_builtin` has its
own narrower source-evaluation qualification, not the standalone fold bit.

Accepted but unimplemented attributes remain zero, including `cleanup`,
`always_inline`, `noinline`, `format`, `noreturn`, `unused`, `nodiscard`,
`maybe_unused` and `deprecated`. The last three have parsed records but no
implemented diagnostic consumer; reporting their C23 date codes would falsely
advertise that consumer. Unpublished alias/weak/constructor/retention effects
also remain zero. Generic GNU atomic load/store/exchange/compare-exchange,
GNU lock-free queries, test-and-set/clear and elementwise bit reversal remain
unadvertised; scalar GNU atomics and implemented C11 atomics are advertised.
The query fix does not implement these outstanding semantics.

`test-preprocessor` checks an independent 180-name builtin inventory, old and
new language modes, scoped/underscore/macro spellings, oracle isolation and
protected input tails. `test-checker` compares intrinsic answers against the
actual source-scope/lowering intersection, and compiles, links and executes
query-selected layout, constant, memory, atomic, TLS and arithmetic behavior
against Clang. Adding source lowering must update its explicit capability;
adding an x86 row or leaf automatically updates the shared inventory.

'''+needle);p.write_text(s)
p=root/'docs/c-builtins-integration.md';s=p.read_text();s=s.replace('Source availability requires the source path and both\nruntime consumers; constexpr additionally requires source constant evaluation.','Source availability now requires the source checker and the shipped Tier 0\nsource path (#806); Tier 1 remains a separate capability, not a gate on the\nexisting C compiler. Constexpr additionally requires source constant evaluation.')
s=s.replace('Until source and two-tier acceptance\nare established, these new entries remain unavailable to `__has_builtin`.','The catalog now explicitly advertises the implemented source paths; recognized\nbut refused GNU atomic variants and elementwise bit reversal remain zero.')
s=s.replace('`__has_builtin` answers 0 for them, as it does for the rest of the floating\nfamily, since the catalog\'s source capability asks for Tier 1 too.','`__has_builtin` now advertises these source-lowered floating builtins (#806).\nThis does not grant their separate source-constexpr or experimental Tier 1 capabilities.');p.write_text(s)
p=root/'THIRD_PARTY_NOTICES.md';s=p.read_text();s+='''
### Feature-query integration (#806)

`c_builtin_source_intrinsics.c`, `c_builtin_x86_rows.inc`,
`c_builtin_x86_leaves.inc` and `c_preprocessor_feature_test.c` under
`src/buster/compiler/` are original project material under the default license.
The two `.inc` inventories relocate the existing first-party native lowering
and helper records without importing implementation. The intrinsic query also
instantiates the already-reviewed
`generated/clang_builtin_own_main_23_1_1.inc` name inventory; that file's LLVM
provenance and license remain unchanged. No new upstream data was imported.
''';p.write_text(s)
