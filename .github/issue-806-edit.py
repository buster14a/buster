from pathlib import Path
import re, subprocess
root=Path.cwd(); d=root/'src/buster/compiler'
def original(p): return subprocess.check_output(['git','show','HEAD:'+str(p.relative_to(root))],cwd=root,text=True)
p=d/'c_builtin_catalog.c';s=original(p)
s=s.replace('// A fold action is not a source capability: SOURCE requires checking and both required runtime consumers, and\n// CONSTEXPR additionally requires source constant evaluation. Keep these flags separate when integration lands.', '// A fold action is not a source capability: SOURCE requires source checking and the shipped Tier 0 consumer.\n// CONSTEXPR additionally requires source constant evaluation. Tier 1 remains an independent, unadvertised capability.')
s=s.replace('#define BUSTER_CBUILTIN_MAX_NAME 48u', '#define BUSTER_CBUILTIN_MAX_NAME 48u\n\n// Source C emits through Tier 0; the experimental Tier 1 action tests are not a source-availability gate (#806).\n#define BUSTER_CBUILTIN_SOURCE (BUSTER_CBUILTIN_CAP_SOURCE | BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0)\n#define BUSTER_CBUILTIN_CONSTANT (BUSTER_CBUILTIN_SOURCE | BUSTER_CBUILTIN_CAP_CONSTEXPR)')
s=s.replace('// Exact source spellings only. These stable identities do not advertise implementation or invent a signature.\n// Family ranges leave room for later additions without renumbering any published identity.', '// Exact source spellings and audited source capabilities. A zero entry is recognized but has no complete native\n// source path. In particular the generic GNU atomics and vector bit reversal remain refused. Availability does not\n// promise every operand type, ISA or ABI: the typed checker and lowering retain their explicit restrictions.\n// Family ranges leave room for later additions without renumbering any published identity.')
refused={'ATOMIC_LOAD','ATOMIC_STORE','ATOMIC_EXCHANGE','ATOMIC_COMPARE_EXCHANGE','ATOMIC_TEST_AND_SET','ATOMIC_CLEAR','ATOMIC_ALWAYS_LOCK_FREE','ATOMIC_IS_LOCK_FREE','ELEMENTWISE_BITREVERSE'}
const={'OFFSETOF','EXPECT','CONSTANT_P','TYPES_COMPATIBLE_P','CHOOSE_EXPR'}
s=re.sub(r'X\((\w+), (0x[0-9a-f]+u), ("[^"]+")\)',lambda m:f'X({m[1]}, {m[2]}, {m[3]}, '+('0' if m[1] in refused else 'BUSTER_CBUILTIN_CONSTANT' if m[1] in const else 'BUSTER_CBUILTIN_SOURCE')+')',s)
s=s.replace('#define BUSTER_CBUILTIN_SPECIAL_ENUM(id, value, name)', '#define BUSTER_CBUILTIN_SPECIAL_ENUM(id, value, name, capabilities)')
s=s.replace('{ CBUILTIN_##id, name, sizeof(name) - 1, BUSTER_CBUILTIN_CAP_FOLD },','{ CBUILTIN_##id, name, sizeof(name) - 1, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_SOURCE | \\\n    (CBUILTIN_##id <= CBUILTIN_FFSLL ? BUSTER_CBUILTIN_CAP_CONSTEXPR : 0) },')
s=s.replace('#define BUSTER_CBUILTIN_SPECIAL_ENTRY(id, value, name) { CBUILTIN_##id, name, sizeof(name) - 1, 0 },', '#define BUSTER_CBUILTIN_SPECIAL_ENTRY(id, value, name, capabilities) { CBUILTIN_##id, name, sizeof(name) - 1, capabilities },')
s=s.replace('required |= BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0 | BUSTER_CBUILTIN_CAP_TIER1;', 'required |= BUSTER_CBUILTIN_CAP_CHECKER | BUSTER_CBUILTIN_CAP_TIER0;')
p.write_text(s)
p=d/'c_check_program_x86.c';s=original(p);start=s.index('#define BUSTER_CHECK_X86_RULE(');end=s.index('static const CheckX86LowerRule* check_x86_lower_rule(')
block=s[start:end].replace('static const CheckX86LowerRule check_x86_lower_rules[] = {\n','').replace('\n};\n\n#undef BUSTER_CHECK_X86_HALF_CONVERSIONS','\n\n#undef BUSTER_CHECK_X86_HALF_CONVERSIONS')
block,count=re.subn(r'\{ ([^{}]*?) \},',r'BUSTER_CBUILTIN_X86_ROW(\1)',block,flags=re.S)
assert count>10 and '{' not in block and '}' not in block
license='// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.\n'
(d/'c_builtin_x86_rows.inc').write_text((license+'//\n// Shared source-lowering inventory (#806), originally in c_check_program_x86.c. The caller supplies\n// BUSTER_CBUILTIN_X86_ROW: lowering retains each full row; feature queries retain only its exact name.\n// Intentionally no include guard: both consumers expand the same rules, without an encoder dependency in the query.\n\n'+block).rstrip()+'\n')
s=s[:start]+'''#define BUSTER_CBUILTIN_X86_ROW(...) { __VA_ARGS__ },
static const CheckX86LowerRule check_x86_lower_rules[] = {
#include "c_builtin_x86_rows.inc"
};
#undef BUSTER_CBUILTIN_X86_ROW

'''+s[end:];p.write_text(s)
p=d/'c_builtin_x86_helpers.c';s=original(p);start=s.index('#define BUSTER_CBUILTIN_X86_HELPER(name');end=s.index('static CBuiltinX86HelperKind cbuiltin_x86_helper_kind(',start)
block=s[start:end];a=block.index('static const CBuiltinX86HelperInfo');b=block.index('= {',a)+len('= {');c=block.index('\n};',b)
rows=block[b:c];rows=re.sub(r'\[(CBUILTIN_X86_HELPER_\w+)\] = ',r'BUSTER_CBUILTIN_X86_LEAF(\1, ',rows)
rows=re.sub(r'BUSTER_CBUILTIN_X86_LEAF\((CBUILTIN_X86_HELPER_\w+), \{ ([^{}]+?) \},',r'BUSTER_CBUILTIN_X86_LEAF(\1, \2)',rows,flags=re.S)
rows=re.sub(r'BUSTER_CBUILTIN_X86_LEAF\((CBUILTIN_X86_HELPER_\w+), BUSTER_CBUILTIN_X86_HELPER\((.*?)\),',r'BUSTER_CBUILTIN_X86_LEAF(\1, BUSTER_CBUILTIN_X86_HELPER(\2))',rows,flags=re.S)
assert rows.count('BUSTER_CBUILTIN_X86_LEAF(')==45
constructor=block[:a].replace('{ name,','name,').replace('false }','false')
(d/'c_builtin_x86_leaves.inc').write_text(license+'//\n// Shared helper identities and native records. BUSTER_CBUILTIN_X86_LEAF takes the stable helper ID and\n// its record fields; the preprocessor discards every field except the name. No encoder types are needed there.\n\n'+constructor+rows+'\n#undef BUSTER_CBUILTIN_X86_HELPER\n')
s=s[:start]+'''#define BUSTER_CBUILTIN_X86_LEAF(id, ...) [id] = { __VA_ARGS__ },
static const CBuiltinX86HelperInfo cbuiltin_x86_helper_infos[CBUILTIN_X86_HELPER_COUNT] = {
#include "c_builtin_x86_leaves.inc"
};
#undef BUSTER_CBUILTIN_X86_LEAF

'''+s[end:];p.write_text(s)
(d/'c_builtin_source_intrinsics.c').write_text(license+'''//
// The actual x86 rows/leaves, not Clang's recognition catalogue. These queries attest a source implementation,
// not permission to execute instructions: target/operand/privilege checks remain the lowering's responsibility.
// Shared with source-level drift tests; all names are bounded slices and need no terminator or readable padding.
#ifndef BUSTER_C_BUILTIN_SOURCE_INTRINSICS_INCLUDED
#define BUSTER_C_BUILTIN_SOURCE_INTRINSICS_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct CBuiltinSourceName CBuiltinSourceName;
struct CBuiltinSourceName
{
    const char* name;
    uint32_t length;
};

#define BUSTER_CBUILTIN_X86_ROW(name, ...) { name, sizeof(name) - 1 },
static const CBuiltinSourceName cbuiltin_source_rows[] = {
#include "c_builtin_x86_rows.inc"
};
#undef BUSTER_CBUILTIN_X86_ROW

#define BUSTER_CBUILTIN_SOURCE_LEAF(name, ...) { name, sizeof(name) - 1 },
#define BUSTER_CBUILTIN_X86_LEAF(id, ...) BUSTER_CBUILTIN_SOURCE_LEAF(__VA_ARGS__)
static const CBuiltinSourceName cbuiltin_source_leaves[] = {
#include "c_builtin_x86_leaves.inc"
};
#undef BUSTER_CBUILTIN_X86_LEAF
#undef BUSTER_CBUILTIN_SOURCE_LEAF

// The source checker deliberately accepts only this frozen workload's intrinsic declarations. A lowering row
// outside it is not a callable source builtin. Reuse its exact sorted name inventory, without importing its types.
#define BUSTER_BUILTIN_METADATA(n, t, a, f, h, l, s, line, ordinal, custom, evaluable, kind, constraints) { n, sizeof(n) - 1 },
static const CBuiltinSourceName cbuiltin_source_intrinsic_scope[] = {
#include "generated/clang_builtin_own_main_23_1_1.inc"
};
#undef BUSTER_BUILTIN_METADATA

static __attribute__((unused)) bool cbuiltin_source_intrinsic(const uint8_t* name, uint32_t length)
{
    bool scoped = false;
    bool result = false;
    uint32_t low = 0;
    uint32_t high = sizeof(cbuiltin_source_intrinsic_scope) / sizeof(cbuiltin_source_intrinsic_scope[0]);
    while (name != NULL && length != 0 && length < 128 && low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const CBuiltinSourceName* entry = cbuiltin_source_intrinsic_scope + middle;
        const uint32_t common = length < entry->length ? length : entry->length;
        const int bytes = memcmp(name, entry->name, common);
        const int compared = bytes != 0 ? bytes : (length > entry->length) - (length < entry->length);
        if (compared == 0)
        {
            scoped = true;
            low = high;
        }
        else if (compared < 0)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    for (uint32_t i = 0; scoped && !result && i < sizeof(cbuiltin_source_rows) / sizeof(cbuiltin_source_rows[0]); i += 1)
    {
        const CBuiltinSourceName* entry = cbuiltin_source_rows + i;
        result = length == entry->length && memcmp(name, entry->name, length) == 0;
    }
    for (uint32_t i = 0; scoped && !result && i < sizeof(cbuiltin_source_leaves) / sizeof(cbuiltin_source_leaves[0]); i += 1)
    {
        const CBuiltinSourceName* entry = cbuiltin_source_leaves + i;
        result = length == entry->length && memcmp(name, entry->name, length) == 0;
    }
    // These two prototype-checked generic intrinsics have dedicated source lowering, not an x86 row or leaf.
    result |= scoped && ((length == sizeof("__builtin_assume") - 1 && memcmp(name, "__builtin_assume", length) == 0) ||
                         (length == sizeof("__builtin_unpredictable") - 1 && memcmp(name, "__builtin_unpredictable", length) == 0));
    return result;
}

#endif
''')
p=d/'c_preprocessor.c';s=original(p)
s=s.replace('#include "c_builtin_catalog.c"', '#include "c_builtin_catalog.c"\n#include "c_builtin_source_intrinsics.c"')
attrs=['aligned','ext_vector_type','fallthrough','gnu_inline','mode','naked','overloadable','packed','section','target','transparent_union','used','vector_size','visibility']
cattrs={'fallthrough':201910,'clang::ext_vector_type':1,'clang::overloadable':1};cattrs.update({'gnu::'+x:1 for x in attrs if x not in ('ext_vector_type','overloadable')})
features={x:2011 for x in ['c_alignas','c_alignof','c_atomic','c_generic_selections','c_static_assert','c_thread_local']};features['c_fixed_enum']=2023
start=s.index('// What buster implements, which the feature queries answer outside');end=s.index('static const PPFeature pp_implemented_builtin[]',start)
s=s[:start]+'''// Implemented source capabilities, independent of the frozen Clang oracle. Attribute acceptance alone is not
// implementation: cleanup, diagnostic-only attributes and unpublished linkage obligations remain zero. These
// entries have declaration/type, control, symbol or executable lowering consumers. C23 date codes describe the
// standardized attribute, not the language mode (the spelling is also accepted as an extension in earlier modes).
// Feature dates are language-mode gates; __has_extension asks about the same implemented syntax in older modes.
// Sorted for binary search. __has_warning remains the diagnostic vocabulary, not a promise to emit each warning.
static const PPFeature pp_implemented_attribute[] = {
'''+''.join('    { "'+x+'", 1 },\n' for x in sorted(attrs))+''' };
static const PPFeature pp_implemented_c_attribute[] = {
'''+''.join('    { "'+x+'", '+str(v)+' },\n' for x,v in sorted(cattrs.items()))+''' };
static const PPFeature pp_implemented_feature[] = {
'''+''.join('    { "'+x+'", 1, '+str(v)+' },\n' for x,v in sorted(features.items()))+''' };
'''+s[end:]
s=s.replace('\n };','\n};')
s=s.replace('cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE);','cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE) | cbuiltin_source_intrinsic(name, length);')
s=s.replace('pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) | sanitizer : 0;', 'pp_feature_lookup(pp_features_feature, BUSTER_ARRAY_LENGTH(pp_features_feature), name, length, standard) | sanitizer\n                           : pp_feature_lookup(pp_implemented_feature, BUSTER_ARRAY_LENGTH(pp_implemented_feature), name, length, standard);')
s=s.replace('pp_feature_lookup(pp_features_extension, BUSTER_ARRAY_LENGTH(pp_features_extension), name, length, standard) | sanitizer\n                           : 0;', 'pp_feature_lookup(pp_features_extension, BUSTER_ARRAY_LENGTH(pp_features_extension), name, length, standard) | sanitizer\n                           : pp_feature_lookup(pp_implemented_feature, BUSTER_ARRAY_LENGTH(pp_implemented_feature), name, length, UINT32_MAX);')
p.write_text(s)
p=d/'c_preprocessor_test.c';s=original(p)
s=s.replace('"A 0 0 0 0 1 1 1 1 0 1 B 0 0 C 0 1 D 0 0 E 0 0 0 1\\n",', '"A 1 1 1 1 1 1 1 1 0 1 B 0 1 C 0 1 D 1 0 E 0 0 0 1\\n",')
s=s.replace('// This explicit acceptance inventory is independent of the catalog\'s generation macros. Recognized special\n    // identities have no source capability; only the existing integer actions have standalone folding capability.', '// This explicit acceptance inventory is independent of the catalog\'s generation macros. A source entry needs\n    // checking and native lowering, never the experimental Tier 1 path. Standalone folding is a separate fact.')
start=s.index('    // This explicit acceptance inventory');end=s.index('    static const char* const own[]',start)
part=s[start:end].replace('        bool fold;','        bool fold;\n        bool source;\n        bool constant;')
part=re.sub(r'\{ CBUILTIN_(\w+), ("[^"]+"), (true|false) \},',lambda m:'{ CBUILTIN_'+m[1]+', '+m[2]+', '+m[3]+', '+('false' if m[1] in refused else 'true')+', '+('true' if (m[3]=='true' and m[1] not in ('ADD_OVERFLOW','SUB_OVERFLOW','MUL_OVERFLOW')) or m[1] in const else 'false')+' },',part)
part=part.replace('result &= (cbuiltin_catalog[i].capabilities & compile_capabilities) == 0;', '''const uint32_t capabilities = cbuiltin_catalog[i].capabilities;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_TIER1) == 0;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_SOURCE) == 0 || (capabilities & BUSTER_CBUILTIN_SOURCE) == BUSTER_CBUILTIN_SOURCE;
        result &= (capabilities & BUSTER_CBUILTIN_CAP_CONSTEXPR) == 0 || (capabilities & BUSTER_CBUILTIN_SOURCE) == BUSTER_CBUILTIN_SOURCE;''')
part=part.replace('!cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_SOURCE)', 'cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_SOURCE) == inventory[i].source')
part=part.replace('!cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR)', 'cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR) == inventory[i].constant')
part=part.replace('!cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_CAP_SOURCE)', 'cbuiltin_has_capability((const uint8_t*)name, length, BUSTER_CBUILTIN_CAP_FOLD | BUSTER_CBUILTIN_CAP_SOURCE) == (inventory[i].fold && inventory[i].source)')
part=part.replace('PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)name, length) == 0', 'PP_BUILTIN_HAS_BUILTIN, (const uint8_t*)name, length) == inventory[i].source')
part=part.replace('PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)name, length) == 0', 'PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, (const uint8_t*)name, length) == inventory[i].constant')
s=s[:start]+part+s[end:]
s=s.replace('!cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE);', 'cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_SOURCE) == inventory[i].source;')
s=s.replace('!cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR);', 'cbuiltin_has_capability(name, length, BUSTER_CBUILTIN_CAP_CONSTEXPR) == inventory[i].constant;')
s=s.replace('pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, name, length) == 0;', 'pp_feature(&defaults, PP_BUILTIN_HAS_BUILTIN, name, length) == inventory[i].source;')
s=s.replace('pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == 0;', 'pp_feature(&defaults, PP_BUILTIN_HAS_CONSTEXPR_BUILTIN, name, length) == inventory[i].constant;')
p.write_text(s)
