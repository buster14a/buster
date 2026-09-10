from pathlib import Path

p = Path('src/buster/lib/compiler/frontend/c/c_parse.c')
s = p.read_text()
a = s.index('BUSTER_C_SHARED bool c_parse_validate_constexpr_declaration(')
b = s.index('\ntypedef struct CTypePair CTypePair;', a)
old = s[a:b]
walk = old[old.index('    while (work_count)'):old.index('    scratch_end(temporary);')]
walk = walk.replace('|| visited[type_id.value]', '|| (visited && visited[type_id.value])')
walk = walk.replace('        visited[type_id.value] = true;', '        if (visited)\n        {\n            visited[type_id.value] = true;\n        }')
walk = walk.replace('            valid = false;\n', '').replace('                valid = false;\n', '')
walk = ''.join('        ' + line if line.strip() else line for line in walk.splitlines(keepends=True))
new = '''BUSTER_C_SHARED bool c_parse_validate_constexpr_declaration(CTypeParseMachine* machine, Arena* arena, CParseResult* result,
                                                                CPreprocessResult preprocess, CDeclaration* declaration)
{
    bool valid = true;
    if (declaration->is_constexpr)
    {
        CSourceLocation location = c_preprocess_token_location(&preprocess, preprocess.tokens[declaration->token_start]);
        String8 message = {0};
        if (declaration->kind != C_DECLARATION_OBJECT)
        {
            message = S8("constexpr may only declare an object");
        }
        else if (!declaration->is_definition)
        {
            message = S8("constexpr object declaration requires an initializer");
        }
        else
        {
            u32 end = declaration->token_start + declaration->token_count;
            for (u32 index = declaration->token_start; index < end && !message.length; index += 1)
            {
                String8 spelling = c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]);
                if (string_equal(spelling, S8("extern")) || string_equal(spelling, S8("_Thread_local")) || string_equal(spelling, S8("__thread")))
                {
                    location = c_preprocess_token_location(&preprocess, preprocess.tokens[index]);
                    message = S8("constexpr cannot be combined with this storage-class specifier");
                }
            }
            if (!message.length && declaration->type.value >= result->type_count)
            {
                message = S8("constexpr object has an invalid type");
            }
        }
        if (!message.length)
        {
            // A leaf has no subobjects to enqueue: validate the local root with
            // the same checks as a graph entry, without clearing the type universe.
            // Composite queries retain private rewindable scratch and no cached
            // answers, so bound evaluation and type mutation keep their ordering.
            CTypeId root = declaration->type;
            CTypeId* work = &root;
            bool* visited = 0;
            TemporalArena temporary = {0};
            CTypeKind root_kind = result->types[root.value].kind;
            if (root_kind == C_TYPE_ARRAY || root_kind == C_TYPE_STRUCT || root_kind == C_TYPE_UNION)
            {
                Arena* conflicts[] = {arena};
                temporary = scratch_begin(conflicts, BUSTER_ARRAY_LENGTH(conflicts));
                work = arena_allocate(temporary.arena, CTypeId, result->type_count + result->member_count + 1);
                visited = arena_allocate(temporary.arena, bool, result->type_count + 1);
                memset(visited, 0, sizeof(*visited) * (result->type_count + 1));
                work[0] = root;
            }
            u32 work_count = 1;
''' + walk + '''            if (temporary.arena)
            {
                scratch_end(temporary);
            }
        }
        valid = !message.length;
        if (!valid)
        {
            c_parse_diagnostic(result, location, C_DIAGNOSTIC_INVALID_CONSTEXPR, message);
        }
    }
    return valid;
}

#if BUSTER_INCLUDE_TESTS
bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess, CDeclaration* declaration)
{
    return c_parse_validate_constexpr_declaration(0, arena, result, preprocess, declaration);
}
#endif
'''
s = s[:a] + new + s[b:]
s = s.replace('#include "c_internal.h"', '#include "c_internal.h"\n#include <buster/lib/compiler/frontend/c/c_parse_internal.h>', 1)
s = s.replace('                            message = S8("constexpr object cannot have variably modified type");', '                        message = S8("constexpr object cannot have variably modified type");')
p.write_text(s)
Path('src/buster/lib/compiler/frontend/c/c_parse_internal.h').write_text('''#pragma once

// Private test seam for the production constexpr type query. No parse-machine
// dependency escapes; these tests use leaves and already resolved array bounds.
#include <buster/lib/compiler/frontend/c/c.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL bool c_test_validate_constexpr_declaration(Arena* arena, CParseResult* result, CPreprocessResult preprocess,
                                                          CDeclaration* declaration);
#endif
''')

tests = '''// A leaf query must not acquire storage proportional to unrelated types. Dirty
// high-water marks observe even allocations rewound before the query returns.
BUSTER_GLOBAL_LOCAL UnitTestResult c_test_constexpr_leaf_storage(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(0, 0);
    CPreprocessResult tokens = c_preprocess(temporary.arena,
        S8("constexpr int first = 7;\\n"
           "constexpr double fraction = 1.5;\\n"
           "constexpr int *nothing = nullptr;\\n"
           "constexpr unsigned char octet = 23;\\n"),
        (CPreprocessOptions){.dialect = C_PREPROCESS_DIALECT_C23, .source_path = S8("constexpr-leaf-storage.c")});
    CParseResult parse = c_parse(temporary.arena, tokens);
    BUSTER_TEST(arguments, tokens.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.diagnostic_count == 0);
    BUSTER_TEST(arguments, parse.declaration_count == 4);
    if (!parse.diagnostic_count && parse.declaration_count == 4)
    {
        u32 original_count = parse.type_count;
        u32 unrelated_counts[] = {0, 1, 63, 64, 65, 1024, 65536, 0};
        CType* types = arena_allocate_zeroed(temporary.arena, CType, original_count + 65536);
        memcpy(types, parse.types, sizeof(*types) * original_count);
        parse.types = types;
        parse.type_capacity = original_count + 65536;
        bool census = os_get_environment_variable(S8("BUSTER_CONSTEXPR_CENSUS")).length != 0;
        for (u32 count_index = 0; count_index < BUSTER_ARRAY_LENGTH(unrelated_counts); count_index += 1)
        {
            parse.type_count = original_count + unrelated_counts[count_index];
            for (u32 declaration_index = 0; declaration_index < parse.declaration_count; declaration_index += 1)
            {
                TemporalArena probe = scratch_begin(&temporary.arena, 1);
                // Move through the public allocator to the previous high-water
                // mark, so a reused dirty arena cannot hide another allocation.
                u64 dirty = arena_dirty_position(probe.arena);
                if (dirty > probe.arena->position)
                {
                    arena_allocate(probe.arena, u8, dirty - probe.arena->position);
                }
                u64 before = probe.arena->position;
                bool valid = c_test_validate_constexpr_declaration(temporary.arena, &parse, tokens,
                                                                      &parse.declarations[declaration_index]);
                u64 growth = arena_dirty_position(probe.arena) - before;
                if (census)
                {
                    string_print(S8("CONSTEXPR_LEAF_CENSUS types={u32} declaration={u32} scratch_growth={u64}\\n"),
                                 parse.type_count, declaration_index, growth);
                }
                BUSTER_TEST(arguments, valid);
                BUSTER_TEST(arguments, parse.diagnostic_count == 0);
                BUSTER_TEST(arguments, probe.arena->position == before);
                BUSTER_TEST(arguments, growth == 0);
                scratch_end(probe);
            }
        }
        parse.type_count = original_count;

        // The same type id can change during speculative semantic construction.
        // A successful query must not hide a later qualifier/type error, and a
        // failed query must not poison the restored type. Diagnostic precedence
        // remains atomic, then volatile/restrict, then complete-object kind.
        CDeclaration declaration = parse.declarations[0];
        CType saved = parse.types[declaration.type.value];
        struct
        {
            CTypeKind kind;
            bool atomic;
            bool volatile_qualified;
            bool restrict_qualified;
            String8 message;
        } invalid[] = {
            {C_TYPE_VOID, true, true, true, S8("constexpr object or subobject cannot have atomic type")},
            {C_TYPE_VOID, false, true, true, S8("constexpr object or subobject cannot be volatile or restrict-qualified")},
            {C_TYPE_INT, false, false, true, S8("constexpr object or subobject cannot be volatile or restrict-qualified")},
            {C_TYPE_VOID, false, false, false, S8("constexpr requires a complete object type")},
            {C_TYPE_FUNCTION, false, false, false, S8("constexpr requires a complete object type")},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
        {
            CType* type = &parse.types[declaration.type.value];
            type->kind = invalid[index].kind;
            type->is_atomic = invalid[index].atomic;
            type->is_volatile = invalid[index].volatile_qualified;
            type->is_restrict = invalid[index].restrict_qualified;
            u32 before = parse.diagnostic_count;
            BUSTER_TEST(arguments, !c_test_validate_constexpr_declaration(temporary.arena, &parse, tokens, &declaration));
            BUSTER_TEST(arguments, parse.diagnostic_count == before + 1);
            if (parse.diagnostic_count == before + 1)
            {
                CDiagnostic diagnostic = parse.diagnostics[before];
                BUSTER_TEST(arguments, diagnostic.kind == C_DIAGNOSTIC_INVALID_CONSTEXPR);
                BUSTER_STRING_TEST(arguments, diagnostic.message, invalid[index].message);
                BUSTER_TEST(arguments, diagnostic.location.line == 1 && diagnostic.location.column == 1);
            }
            *type = saved;
            BUSTER_TEST(arguments, c_test_validate_constexpr_declaration(temporary.arena, &parse, tokens, &declaration));
            BUSTER_TEST(arguments, parse.diagnostic_count == before + 1);
        }
        // A non-constexpr declaration bypasses type inspection entirely, even
        // when no type universe exists yet; an invalid constexpr id diagnoses.
        u32 before = parse.diagnostic_count;
        declaration.is_constexpr = false;
        declaration.type = C_TYPE_ID_INVALID;
        parse.type_count = 0;
        BUSTER_TEST(arguments, c_test_validate_constexpr_declaration(temporary.arena, &parse, tokens, &declaration));
        BUSTER_TEST(arguments, parse.diagnostic_count == before);
        declaration.is_constexpr = true;
        BUSTER_TEST(arguments, !c_test_validate_constexpr_declaration(temporary.arena, &parse, tokens, &declaration));
        BUSTER_TEST(arguments, parse.diagnostic_count == before + 1);
        if (parse.diagnostic_count == before + 1)
        {
            BUSTER_STRING_TEST(arguments, parse.diagnostics[before].message, S8("constexpr object has an invalid type"));
        }
    }
    scratch_end(temporary);
    return result;
}

'''
p = Path('src/buster/tests/compiler/frontend/c/c_test.c')
s = p.read_text().replace('#include <buster/lib/compiler/frontend/c/c_gen_internal.h>', '#include <buster/lib/compiler/frontend/c/c_gen_internal.h>\n#include <buster/lib/compiler/frontend/c/c_parse_internal.h>', 1)
a = s.index('// A syntax pass retains declarations/assertions,')
s = s[:a] + tests + s[a:]
s = s.replace('    c_test_result_add(&result, c_test_parser_body_frame_storage(arguments));', '    c_test_result_add(&result, c_test_parser_body_frame_storage(arguments));\n    c_test_result_add(&result, c_test_constexpr_leaf_storage(arguments));', 1)
p.write_text(s)

Path('tests/basic_c_constexpr_leaf.c').write_text('''// C23 constexpr leaves and composite subobjects share the same type checks.
// Pointees are not subobjects: a null pointer to a volatile object is valid.
struct ConstexprPair
{
    int first;
    int second;
};

struct ConstexprNested
{
    struct ConstexprPair pair;
    int values[3];
};

union ConstexprUnion
{
    int integer;
    unsigned other;
};

constexpr int width = 3;
constexpr double fraction = 1.5;
constexpr volatile int* no_object = nullptr;
constexpr int fixed[width] = {2, 3, 5};
constexpr int inferred[] = {7, 11};
constexpr struct ConstexprNested nested = {{13, 17}, {19, 23, 29}};
constexpr union ConstexprUnion selected = {31};

static_assert(width == 3);
static_assert(sizeof(inferred) / sizeof(inferred[0]) == 2);

static int scoped_constants(void)
{
    int value = width;
    {
        constexpr int width = 6;
        int local[width];
        local[0] = width;
        value += local[0];
    }
    return value + width;
}

int main(void)
{
    int failed = 0;
    failed |= width != 3 || fraction != 1.5 || no_object != nullptr;
    failed |= fixed[0] + fixed[1] + fixed[2] != 10;
    failed |= inferred[0] + inferred[1] != 18;
    failed |= nested.pair.first != 13 || nested.pair.second != 17;
    failed |= nested.values[0] + nested.values[1] + nested.values[2] != 71;
    failed |= selected.integer != 31;
    failed |= scoped_constants() != 12;
    return failed;
}
''')
p = Path('src/buster/tests/compiler/driver/driver_test.c')
s = p.read_text()
a = s.index('    // Running the fixture cannot tell whether [[noreturn]] was read:')
s = s[:a] + '''    // Keep the constexpr storage/semantic fixture in the existing native mode
    // matrix. Unlike the attribute fixtures, constexpr requires the C23 dialect.
    for (u64 allocator_index = 0; allocator_index < BUSTER_ARRAY_LENGTH(c_lz4_regression_allocators); allocator_index += 1)
    {
        TemporalArena constexpr_temporary = scratch_begin(&arguments->arena, 1);
        String8 fixture_path = buster_test_temporary_path(constexpr_temporary.arena, S8("buster-c-constexpr-leaf"), S8(""));
        String8 fixture_command_line[] = {
            S8("-std=c23"), c_lz4_regression_allocators[allocator_index], S8("-o"), fixture_path, S8("tests/basic_c_constexpr_leaf.c"),
        };
        CompilerDriverResult fixture = compiler_driver_execute_invocation(
            constexpr_temporary.arena,
            compiler_driver_parse_arguments(constexpr_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_command_line)));
        BUSTER_TEST(arguments, fixture.error == COMPILER_DRIVER_ERROR_NONE);
        if (fixture.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 fixture_arguments[] = {fixture_path};
            ProcessSpawnResult fixture_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(fixture_arguments), (SliceString8){0}, (SliceString8){0},
                                                                (ProcessSpawnOptions){.use_process_environment = true});
            BUSTER_TEST(arguments, fixture_spawn.handle != 0);
            if (fixture_spawn.handle)
            {
                BUSTER_TEST(arguments, os_process_wait_sync(constexpr_temporary.arena, fixture_spawn).result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(constexpr_temporary);
    }
''' + s[a:]
p.write_text(s)
p = Path('docs/agents/frontend/foundations.md')
s = p.read_text()
a = s.index('- **GNU\'s `__alignof__` takes an expression; `_Alignof` takes only a type')
s = s[:a] + '''- `c_parse_validate_constexpr_declaration` validates a leaf root from one local
  work entry, without acquiring scratch or clearing the translation-unit type
  universe. Arrays, structs and unions retain the explicit private graph walk.
  Both paths use the same qualifier and complete-object checks, in the same
  diagnostic order. Pointees are not subobjects. No result is cached across
  mutation or rollback. `c_test_constexpr_leaf_storage` checks reused scratch,
  unrelated type populations, qualifier mutation/restoration and diagnostics;
  `tests/basic_c_constexpr_leaf.c` runs in C23 under every native allocator.
  Composite-query universe-sized scratch remains tracked in GitHub #259.
''' + s[a:]
p.write_text(s)
