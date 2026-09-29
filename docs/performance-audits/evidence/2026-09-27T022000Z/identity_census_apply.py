#!/usr/bin/env python3
"""Disposable identity census: apply anchor-checked counters to a buster tree.

usage: identity_census_apply.py <tree> [--lenient]
Copies identity_census.{h,c} next to this script into <tree>/src/buster/lib and
patches every counting site. Each anchor must match exactly once (unless
--lenient, which skips anchors a candidate removed). Build with
-DBUSTER_IDENTITY_CENSUS=1 -Wno-frame-address; run with
BUSTER_IDENTITY_CENSUS_OUT=<file>. Never commit the result.
"""
import os, shutil, sys
tree = sys.argv[1]
lenient = '--lenient' in sys.argv
here = os.path.dirname(os.path.abspath(__file__))
lib = os.path.join(tree, 'src/buster/lib')
for name in ('identity_census.h', 'identity_census.c'):
    shutil.copy(os.path.join(here, name), os.path.join(lib, name))
skipped = []
def patch(rel, old, new, count=1):
    path = os.path.join(tree, rel)
    s = open(path).read()
    n = s.count(old)
    if n != count:
        if lenient and n == 0:
            skipped.append((rel, old[:70]))
            return
        raise SystemExit(f'anchor count {n} != {count} in {rel}: {old[:80]!r}')
    open(path, 'w').write(s.replace(old, new))
def prepend(rel, text):
    path = os.path.join(tree, rel)
    s = open(path).read()
    if not s.startswith(text):
        open(path, 'w').write(text + s)

INC = '#include <buster/lib/identity_census.h>\n'
# string_equal / buster_hash_64
patch('src/buster/lib/string.c', 'bool string_equal(String8 s1, String8 s2)\n{', INC + 'IDENTITY_CENSUS_NOINLINE bool string_equal(String8 s1, String8 s2)\n{')
patch('src/buster/lib/string.c', '''#endif
        }
    }

    return result;
}

bool string16_equal''', '''#endif
        }
    }
    IDENTITY_CENSUS_RECORD(STRING_EQUAL, (s1.length == s2.length && s1.length && s1.pointer && s2.pointer && s1.pointer != s2.pointer) ? s1.length : 0, result);

    return result;
}

bool string16_equal''')
patch('src/buster/lib/hash.c', 'u64 buster_hash_64(u8* pointer, u64 length)\n{', INC + 'IDENTITY_CENSUS_NOINLINE u64 buster_hash_64(u8* pointer, u64 length)\n{\n    IDENTITY_CENSUS_RECORD(HASH64, length, 0);')
patch('src/buster/apps/ide/ide.c', '#include <buster/lib/hash.c>\n', '#include <buster/lib/hash.c>\n#include <buster/lib/identity_census.c>\n')
# interner
cs = 'src/buster/lib/compiler/frontend/c/c_source.c'
prepend(cs, INC)
patch(cs, 'BUSTER_C_SHARED u32 c_symbol_intern(CSymbolTable* table, String8 name)\n{\n    CSymbolKey key = c_symbol_key(name);', '''IDENTITY_CENSUS_NOINLINE BUSTER_C_SHARED u32 c_symbol_intern(CSymbolTable* table, String8 name)
{
    CSymbolKey key = c_symbol_key(name);
    u64 census_probes = 0;
    void* census_site0 = IDENTITY_CENSUS_SITE0;
    void* census_site1 = IDENTITY_CENSUS_SITE1;''')
patch(cs, '''        CSymbolSlot* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;
        if (!length_and_id)
        {
            break;
        }
        if (entry->low == key.low && entry->high == key.high && (length_and_id & UINT64_C(0xFFFFFFFF00000000)) == length_word)
        {
            u32 id = (u32)length_and_id;
            if (name.length <= 16 || c_symbol_middle_equal(table->names[id], name))
            {
                return id;
            }
        }
        slot = (slot + 1) & mask;
    }''', '''        CSymbolSlot* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;
        census_probes += 1;
        if (!length_and_id)
        {
            break;
        }
        if (entry->low == key.low && entry->high == key.high && (length_and_id & UINT64_C(0xFFFFFFFF00000000)) == length_word)
        {
            u32 id = (u32)length_and_id;
            if (name.length > 16) identity_census_record(IDENTITY_CENSUS_SYMBOL_MIDDLE, census_site0, census_site1, name.length - 16, 0);
            if (name.length <= 16 || c_symbol_middle_equal(table->names[id], name))
            {
                identity_census_record(IDENTITY_CENSUS_SYMBOL_INTERN, census_site0, census_site1, name.length < 16 ? name.length : 16, census_probes);
                return id;
            }
        }
        slot = (slot + 1) & mask;
    }
    identity_census_record(IDENTITY_CENSUS_SYMBOL_INTERN, census_site0, census_site1, name.length < 16 ? name.length : 16, census_probes);
    identity_census_record(IDENTITY_CENSUS_SYMBOL_INSERT, census_site0, census_site1, name.length, 0);''')
# read-only probe (candidates only)
patch(cs, '''    u32 result = 0;
    for (;;)
    {
        CSymbolSlot const* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;''', '''    u32 result = 0;
    identity_census_record_tag(IDENTITY_CENSUS_PROBE, 7, name.length < 16 ? name.length : 16, 0);
    for (;;)
    {
        CSymbolSlot const* entry = &table->slots[slot];
        u64 length_and_id = entry->length_and_id;''')
patch(cs, 'BUSTER_C_SHARED u64 c_macro_name_hash(String8 name)\n{', 'IDENTITY_CENSUS_NOINLINE BUSTER_C_SHARED u64 c_macro_name_hash(String8 name)\n{\n    IDENTITY_CENSUS_RECORD(FNV, name.length, 0);')
# phases
d = 'src/buster/lib/compiler/driver/driver.c'
prepend(d, INC)
patch(d, '    CPreprocessResult preprocess = c_preprocess(arena, BYTE_SLICE_TO_STRING(8, bytes),', '    IDENTITY_CENSUS_PHASE(PREPROCESS);\n    CPreprocessResult preprocess = c_preprocess(arena, BYTE_SLICE_TO_STRING(8, bytes),')
patch(d, '''    CParserResult syntax = c_parse_ast(arena, preprocess);
    result.parser_diagnostic_count = syntax.diagnostic_count;''', '''    IDENTITY_CENSUS_PHASE(SYNTAX);
    CParserResult syntax = c_parse_ast(arena, preprocess);
    IDENTITY_CENSUS_PHASE(SEMANTIC);
    result.parser_diagnostic_count = syntax.diagnostic_count;''')
patch(d, '''    IrValidationResult validation = ir_prepare_canonical_module(lowered.program, module,
                                                                lowered.canonical_ir_certified''', '''    IDENTITY_CENSUS_PHASE(IR_PREPARE);
    IrValidationResult validation = ir_prepare_canonical_module(lowered.program, module,
                                                                lowered.canonical_ir_certified''')
patch(d, '    CodegenModule code = codegen_generate_canonical_module_with_trace(arena, lowered.program, module, invocation.target,', '    IDENTITY_CENSUS_PHASE(CODEGEN);\n    CodegenModule code = codegen_generate_canonical_module_with_trace(arena, lowered.program, module, invocation.target,')
patch(d, '    ObjectFile object = object_from_canonical_codegen_module(arena, lowered.program, &code, invocation.target);', '    IDENTITY_CENSUS_PHASE(OBJECT);\n    ObjectFile object = object_from_canonical_codegen_module(arena, lowered.program, &code, invocation.target);')
patch(d, '        ObjectArtifact artifact = object_write(arena, &object, object_format_for_target(invocation.target));', '        IDENTITY_CENSUS_PHASE(WRITE);\n        ObjectArtifact artifact = object_write(arena, &object, object_format_for_target(invocation.target));')
patch(d, '    LinkObjectResult linked = link_objects(arena, link_inputs, link_input_count,', '    IDENTITY_CENSUS_PHASE(LINK);\n    LinkObjectResult linked = link_objects(arena, link_inputs, link_input_count,')
patch(d, '    LinkObjectResult linked = link_objects(arena, objects, object_count,', '    IDENTITY_CENSUS_PHASE(LINK);\n    LinkObjectResult linked = link_objects(arena, objects, object_count,')
c = 'src/buster/lib/compiler/frontend/c/c.c'
prepend(c, INC)
patch(c, '''    else
    {
        result = c_lower_to_ir_with_options(arena, source_path, preprocess, analysis, target, options);''', '''    else
    {
        IDENTITY_CENSUS_PHASE(LOWER);
        result = c_lower_to_ir_with_options(arena, source_path, preprocess, analysis, target, options);''')
# object name hash
o = 'src/buster/lib/compiler/object/object.c'
prepend(o, INC)
patch(o, 'BUSTER_GLOBAL_LOCAL u64 object_symbol_name_hash(String8 name)\n{', 'IDENTITY_CENSUS_NOINLINE BUSTER_GLOBAL_LOCAL u64 object_symbol_name_hash(String8 name)\n{\n    IDENTITY_CENSUS_RECORD(OBJECT_NAME_HASH, name.length, 0);')
# structural type comparisons
p = 'src/buster/lib/compiler/frontend/c/c_parse.c'
patch(p, '''    CTypePair* stack = arena_allocate(temporary.arena, CTypePair, result->type_count * 2 + 1);
    u32 stack_count = 0;
    stack[stack_count++] = (CTypePair){
        .left = left,
        .right = right,
    };
    bool compatible = true;
    while (stack_count)
    {
        CTypePair pair = stack[--stack_count];''', '''    CTypePair* stack = arena_allocate(temporary.arena, CTypePair, result->type_count * 2 + 1);
    IDENTITY_CENSUS_TAG(STRUCTURAL, 3, (result->type_count * 2 + 1) * sizeof(CTypePair), 0);
    u32 stack_count = 0;
    stack[stack_count++] = (CTypePair){
        .left = left,
        .right = right,
    };
    bool compatible = true;
    while (stack_count)
    {
        CTypePair pair = stack[--stack_count];
        IDENTITY_CENSUS_TAG(STRUCTURAL, 2, 0, pair.left.value == pair.right.value);''')
patch(p, '''BUSTER_C_INTERNAL bool c_parse_types_compatible_core(Arena* result_arena, CParseResult* result, CPreprocessResult preprocess,
                                                       CTypeId left, CTypeId right, bool ignore_nested_qualifiers)
{''', '''BUSTER_C_INTERNAL bool c_parse_types_compatible_core(Arena* result_arena, CParseResult* result, CPreprocessResult preprocess,
                                                       CTypeId left, CTypeId right, bool ignore_nested_qualifiers)
{
    IDENTITY_CENSUS_TAG(STRUCTURAL, 1, 0, left.value == right.value);
    if (left.value == right.value && left.value < result->type_count)
    {
        IDENTITY_CENSUS_TAG(STRUCTURAL, 100 + (u32)result->types[left.value].kind, 0, 0);
    }''')
g = 'src/buster/lib/compiler/frontend/c/c_gen.c'
patch(g, '''    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* type = program->types.types + type_index;
        if (type->is_atomic == is_atomic && type->is_volatile == is_volatile && type->unqualified_type.value == unqualified.value)
        {
            return type->id;
        }
    }''', '''    for (u32 type_index = 0; type_index < program->types.count; type_index += 1)
    {
        IrType* type = program->types.types + type_index;
        if (type->is_atomic == is_atomic && type->is_volatile == is_volatile && type->unqualified_type.value == unqualified.value)
        {
            IDENTITY_CENSUS_TAG(STRUCTURAL, 4, type_index + 1, 1);
            return type->id;
        }
    }
    IDENTITY_CENSUS_TAG(STRUCTURAL, 4, program->types.count, 0);''')
patch(g, '''    u32 task_count = 1;
    tasks[0] = (CIrTypeCompatibilityTask){.left = left_id, .right = right_id};
    bool compatible = true;''', '''    u32 task_count = 1;
    tasks[0] = (CIrTypeCompatibilityTask){.left = left_id, .right = right_id};
    bool compatible = true;
    IDENTITY_CENSUS_TAG(STRUCTURAL, 5, 0, left_id.value == right_id.value);''')
for rel, old in skipped:
    print(f'skipped (lenient): {rel}: {old!r}')
print('identity census applied')
