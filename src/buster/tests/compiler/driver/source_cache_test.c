// Cached lexical files are replayed through the real driver after adversarial
// edits. Exact diagnostics and same-path artifacts are compared with fresh work.
// The cache owns no result memory; direct preprocessing lifetime is checked too.
#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool source_cache_test_location_equal(CompilerDiagnosticLocation a, CompilerDiagnosticLocation b)
{
    bool equal = string_equal(a.path, b.path) && string_equal(a.original_path, b.original_path) &&
                 a.has_range == b.has_range && a.range.source.value == b.range.source.value &&
                 a.range.offset == b.range.offset && a.range.length == b.range.length &&
                 a.position.source == b.position.source && a.position.offset == b.position.offset &&
                 a.position.line == b.position.line && a.position.column == b.position.column &&
                 a.original_position.source == b.original_position.source && a.original_position.offset == b.original_position.offset &&
                 a.original_position.line == b.original_position.line && a.original_position.column == b.original_position.column;
    return equal;
}

BUSTER_GLOBAL_LOCAL bool source_cache_test_diagnostics_equal(CompilerDriverResult a, CompilerDriverResult b)
{
    bool equal = a.error == b.error && string_equal(a.diagnostic, b.diagnostic) && string_equal(a.warning, b.warning) &&
                 a.diagnostic_count == b.diagnostic_count && a.tokenizer_error_count == b.tokenizer_error_count &&
                 a.tokenizer_warning_count == b.tokenizer_warning_count && a.parser_diagnostic_count == b.parser_diagnostic_count &&
                 a.analysis_diagnostic_count == b.analysis_diagnostic_count;
    if (equal && a.diagnostic_count)
    {
        equal = a.diagnostics && b.diagnostics;
    }
    for (u32 index = 0; equal && index < a.diagnostic_count; index += 1)
    {
        CompilerDiagnostic x = a.diagnostics[index];
        CompilerDiagnostic y = b.diagnostics[index];
        equal = x.severity == y.severity && x.note_count == y.note_count && string_equal(x.code, y.code) &&
                string_equal(x.symbol, y.symbol) && string_equal(x.message, y.message) &&
                source_cache_test_location_equal(x.primary, y.primary) && ((x.backend == 0) == (y.backend == 0));
        if (equal && x.note_count)
        {
            equal = x.notes && y.notes;
        }
        for (u32 note = 0; equal && note < x.note_count; note += 1)
        {
            equal = string_equal(x.notes[note].message, y.notes[note].message) &&
                    source_cache_test_location_equal(x.notes[note].location, y.notes[note].location);
        }
        if (equal && x.backend)
        {
            CompilerDiagnosticBackend p = *x.backend;
            CompilerDiagnosticBackend q = *y.backend;
            equal = string_equal(p.target, q.target) && string_equal(p.allocator, q.allocator) &&
                    string_equal(p.function, q.function) && string_equal(p.opcode, q.opcode) &&
                    string_equal(p.operation, q.operation) && string_equal(p.reason, q.reason) &&
                    string_equal(p.referenced_symbol, q.referenced_symbol) && p.error_id == q.error_id &&
                    p.function_id == q.function_id && p.instruction_id == q.instruction_id &&
                    p.opcode_id == q.opcode_id && p.operation_id == q.operation_id;
        }
    }
    return equal;
}

BUSTER_GLOBAL_LOCAL UnitTestResult source_cache_test_pair(UnitTestArguments* arguments, Arena* arena, CSourceCache* cache,
    String8 input, String8 output, String8 target,
    SliceString8 options, bool succeeds, bool destroy_before_read)
{
    UnitTestResult result = {0};
    String8* command = arena_allocate(arena, String8, options.length + 8);
    String8 prefix[] = {S8("-c"), S8("-g"), S8("-nostdinc"), S8("-target"), target, S8("-o"), output};
    memcpy(command, prefix, sizeof(prefix));
    if (options.length)
    {
        memcpy(command + BUSTER_ARRAY_LENGTH(prefix), options.pointer, options.length * sizeof(String8));
    }
    command[options.length + BUSTER_ARRAY_LENGTH(prefix)] = input;
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = options.length + 8});
    if (BUSTER_REQUIRE(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE))
    {
        invocation.source_cache = cache;
        BUSTER_TEST(arguments, os_file_delete(output));
        CompilerDriverResult cached = compiler_driver_execute_invocation(arena, invocation);
        if (destroy_before_read)
        {
            c_source_cache_destroy(cache);
        }
        BUSTER_TEST_RAW(arguments, (cached.error == COMPILER_DRIVER_ERROR_NONE) == succeeds, cached.diagnostic);
        if (!succeeds)
        {
            BUSTER_TEST(arguments, file_read_checked(arena, output, (FileReadOptions){0}).status != OS_FILE_READ_OK);
        }
        ByteSlice cached_bytes = cached.error == COMPILER_DRIVER_ERROR_NONE ? file_read(arena, output, (FileReadOptions){0}) : (ByteSlice){0};
        BUSTER_TEST(arguments, os_file_delete(output));
        invocation.source_cache = 0;
        invocation.enable_source_cache = false;
        CompilerDriverResult fresh = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, source_cache_test_diagnostics_equal(cached, fresh), fresh.diagnostic);
        if (!succeeds)
        {
            BUSTER_TEST(arguments, file_read_checked(arena, output, (FileReadOptions){0}).status != OS_FILE_READ_OK);
        }
        BUSTER_TEST(arguments, cached.has_object == fresh.has_object && cached.codegen_error == fresh.codegen_error && cached.object_error == fresh.object_error);
        BUSTER_TEST(arguments, memcmp(&cached.source_lexed, &fresh.source_lexed, sizeof(CSourceMetrics)) == 0 &&
                               memcmp(&cached.source_unique, &fresh.source_unique, sizeof(CSourceMetrics)) == 0 &&
                               memcmp(&cached.preprocessed, &fresh.preprocessed, sizeof(CPreprocessedMetrics)) == 0);
        if (succeeds && BUSTER_REQUIRE(arguments, fresh.error == COMPILER_DRIVER_ERROR_NONE && cached.error == COMPILER_DRIVER_ERROR_NONE))
        {
            ByteSlice fresh_bytes = file_read(arena, output, (FileReadOptions){0});
            if (BUSTER_REQUIRE(arguments, cached_bytes.length && cached_bytes.length == fresh_bytes.length && cached_bytes.pointer && fresh_bytes.pointer))
            {
                BUSTER_TEST(arguments, memcmp(cached_bytes.pointer, fresh_bytes.pointer, cached_bytes.length) == 0);
            }
        }
        BUSTER_TEST(arguments, os_file_delete(output));
    }
    return result;
}

#define BUSTER_CACHE_REPLAY(arguments, ...) \
    do \
    { \
        UnitTestResult pair_ = source_cache_test_pair((arguments), __VA_ARGS__); \
        result.test_count += pair_.test_count; \
        result.succeeded_test_count += pair_.succeeded_test_count; \
    } while (0)

#define BUSTER_CACHE_REPLAY0(arguments, cache, input, succeeds, destroy) \
    BUSTER_CACHE_REPLAY((arguments), arena, (cache), (input), output, target, (SliceString8){0}, (succeeds), (destroy))

#define BUSTER_CACHE_WRITE(path, content) \
    BUSTER_TEST(arguments, file_write((path), BUSTER_SLICE_TO_BYTE_SLICE(content)))

BUSTER_GLOBAL_LOCAL UnitTestResult source_cache_test_preprocess_lifetime(UnitTestArguments* arguments, Arena* arena, String8 input)
{
    UnitTestResult result = {0};
    CSourceCache* cache = c_source_cache_create(arena, BUSTER_MB(4));
    if (BUSTER_REQUIRE(arguments, cache != 0))
    {
        String8 source = S8("#define VALUE 1\\\r\n2\r\n#line 91 \"logical-owned.c\"\r\nint cache_value(void) { return VALUE; }\r\n");
        CPreprocessOptions options = {.source_path = input, .target = target_native, .data_layout = target_data_layout(target_native), .source_cache = cache};
        CPreprocessResult cold = c_preprocess(arena, source, options);
        CPreprocessResult warm = c_preprocess(arena, source, options);
        BUSTER_TEST(arguments, !cold.error_count && !warm.error_count && c_source_cache_stats(cache).hits != 0);
        c_source_cache_destroy(cache);
        options.source_cache = 0;
        CPreprocessResult fresh = c_preprocess(arena, source, options);
        if (BUSTER_REQUIRE(arguments, !fresh.error_count && warm.token_count == fresh.token_count && warm.tokens && fresh.tokens))
        {
            for (u32 index = 0; index < warm.token_count; index += 1)
            {
                CToken a = warm.tokens[index];
                CToken b = fresh.tokens[index];
                BUSTER_TEST(arguments, a.kind == b.kind && a.punctuator == b.punctuator);
                BUSTER_STRING_TEST(arguments, c_token_spelling(warm.spelling_base, a), c_token_spelling(fresh.spelling_base, b));
                CSourceLocation x = c_preprocess_token_location(&warm, a);
                CSourceLocation y = c_preprocess_token_location(&fresh, b);
                BUSTER_TEST(arguments, x.offset == y.offset && x.line == y.line && x.column == y.column && x.file == y.file && x.map_offset == y.map_offset);
            }
            CParserResult syntax = c_parse_ast(arena, warm);
            CIRLowerResult lowered = c_analyze(arena, input, warm, syntax, target_native);
            BUSTER_TEST(arguments, !syntax.diagnostic_count && !lowered.diagnostic_count && lowered.program && lowered.canonical_ir_certified);
        }
    }
    return result;
}

// Identical raw bytes lexed under another trigraph or dialect setting must miss:
// phase-one replacement and identifier UCN policy change the template.
BUSTER_GLOBAL_LOCAL UnitTestResult source_cache_test_lexical_key(UnitTestArguments* arguments, Arena* arena, String8 input)
{
    UnitTestResult result = {0};
    CSourceCache* cache = c_source_cache_create(arena, BUSTER_MB(4));
    if (BUSTER_REQUIRE(arguments, cache != 0))
    {
        String8 trigraph_source = S8("\?\?=define VALUE 3\nint cache_value(void) { return VALUE; }\n");
        String8 ucn_source = S8("int caf\\u00e9 = 1;\n");
        struct { String8 source; CPreprocessDialect dialect; bool already_preprocessed; bool hit; } steps[] = {
            {trigraph_source, C_PREPROCESS_DIALECT_GNU17, false, false},
            {trigraph_source, C_PREPROCESS_DIALECT_C17, false, false},
            {trigraph_source, C_PREPROCESS_DIALECT_C17, true, false},
            {trigraph_source, C_PREPROCESS_DIALECT_C17, false, true},
            {trigraph_source, C_PREPROCESS_DIALECT_GNU17, false, true},
            {ucn_source, C_PREPROCESS_DIALECT_GNU17, false, false},
            {ucn_source, C_PREPROCESS_DIALECT_GNU89, false, false},
            {ucn_source, C_PREPROCESS_DIALECT_GNU17, false, true},
        };
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(steps); index += 1)
        {
            CPreprocessOptions options = {
                .source_path = input,
                .target = target_native,
                .data_layout = target_data_layout(target_native),
                .dialect = steps[index].dialect,
                .already_preprocessed = steps[index].already_preprocessed,
                .source_cache = cache,
            };
            CSourceCacheStats before = c_source_cache_stats(cache);
            CPreprocessResult cached = c_preprocess(arena, steps[index].source, options);
            CSourceCacheStats after = c_source_cache_stats(cache);
            BUSTER_TEST(arguments, (after.hits > before.hits) == steps[index].hit);
            options.source_cache = 0;
            CPreprocessResult fresh = c_preprocess(arena, steps[index].source, options);
            BUSTER_TEST(arguments, cached.error_count == fresh.error_count && cached.warning_count == fresh.warning_count);
            if (BUSTER_REQUIRE(arguments, cached.token_count == fresh.token_count && (!fresh.token_count || (cached.tokens && fresh.tokens))))
            {
                for (u64 token_index = 0; token_index < fresh.token_count; token_index += 1)
                {
                    CToken a = cached.tokens[token_index];
                    CToken b = fresh.tokens[token_index];
                    BUSTER_TEST(arguments, a.kind == b.kind && a.punctuator == b.punctuator);
                    BUSTER_STRING_TEST(arguments, c_token_spelling(cached.spelling_base, a), c_token_spelling(fresh.spelling_base, b));
                }
            }
        }
        c_source_cache_destroy(cache);
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_source_cache_replay(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if !BUSTER_ANDROID && !BUSTER_IOS
    Arena* arena = arena_create((ArenaCreation){0});
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        String8 root = buster_test_temporary_path(arena, S8("buster-source-cache-replay"), S8(""));
        String8 early = string_format_z(arena, S8("{S8}/early"), root);
        String8 late = string_format_z(arena, S8("{S8}/late"), root);
        String8 input = string_format_z(arena, S8("{S8}/input.c"), root);
        String8 other = string_format_z(arena, S8("{S8}/other.c"), root);
        String8 header = string_format_z(arena, S8("{S8}/shared.h"), root);
        String8 optional = string_format_z(arena, S8("{S8}/optional.h"), root);
        String8 early_header = string_format_z(arena, S8("{S8}/early/choice.h"), root);
        String8 late_header = string_format_z(arena, S8("{S8}/late/choice.h"), root);
        String8 output = string_format_z(arena, S8("{S8}/output.o"), root);
        String8 target = S8("x86_64-unknown-linux-gnu");
        bool ready = os_make_directory_attempt(root) && os_make_directory_attempt(early) && os_make_directory_attempt(late);
        if (BUSTER_REQUIRE(arguments, ready))
        {
            CSourceCache* cache = c_source_cache_create(arena, BUSTER_MB(4));
            if (BUSTER_REQUIRE(arguments, cache != 0))
            {
                String8 source = S8("#include \"shared.h\"\nint cache_value(void) { return VALUE; }\n");
                BUSTER_CACHE_WRITE(input, source);
                BUSTER_CACHE_WRITE(header, S8("#ifndef VALUE\n#define VALUE 11\n#endif\n"));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                CSourceCacheStats cold = c_source_cache_stats(cache);
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                CSourceCacheStats warm = c_source_cache_stats(cache);
                BUSTER_TEST(arguments, warm.hits > cold.hits && warm.reused_bytes > cold.reused_bytes && warm.reused_tokens > cold.reused_tokens);
                BUSTER_TEST(arguments, warm.retained_bytes <= warm.byte_limit && warm.entry_count <= 64);
                BUSTER_CACHE_WRITE(header, S8("#ifndef VALUE\n#define VALUE 12\n#endif\n"));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_TEST(arguments, c_source_cache_stats(cache).misses > warm.misses);
                String8 definitions[][3] = {
                    {S8("-DVALUE=3"), S8("-UVALUE"), S8("-DVALUE=4")},
                    {S8("-DVALUE=4"), S8("-UVALUE"), S8("-DVALUE=3")},
                };
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(definitions); index += 1)
                {
                    BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(definitions[index]), true, false);
                }
                BUSTER_CACHE_WRITE(input, S8("#if __has_include(\"optional.h\")\n#include \"optional.h\"\n#else\n#define VALUE 21\n#endif\nint cache_value(void) { return VALUE; }\n"));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_CACHE_WRITE(optional, S8("#define VALUE 22\n"));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_TEST(arguments, os_file_delete(optional));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_CACHE_WRITE(input, S8("#include <choice.h>\nint cache_value(void) { return VALUE; }\n"));
                BUSTER_CACHE_WRITE(late_header, S8("#define VALUE 31\n"));
                String8 paths[] = {S8("-I"), early, S8("-I"), late};
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(paths), true, false);
                BUSTER_CACHE_WRITE(early_header, S8("#define VALUE 32\n"));
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(paths), true, false);
                String8 reversed[] = {S8("-I"), late, S8("-I"), early};
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(reversed), true, false);
                BUSTER_CACHE_WRITE(early_header, S8("#include_next <choice.h>\n#define VALUE (BOTTOM + 1)\n"));
                BUSTER_CACHE_WRITE(late_header, S8("#define BOTTOM 40\n"));
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(paths), true, false);
                BUSTER_CACHE_WRITE(input, S8("#if PACKING == 1\n#pragma pack(push, 1)\n#else\n#pragma pack(push, 2)\n#endif\nstruct item { char c; int i; };\n#pragma pack(pop)\n#pragma push_macro(\"VALUE\")\n#undef VALUE\n#define VALUE 99\n#pragma pop_macro(\"VALUE\")\nint cache_value(void) { return sizeof(struct item) + VALUE; }\n"));
                String8 packing[][2] = {{S8("-DPACKING=1"), S8("-DVALUE=5")}, {S8("-DPACKING=2"), S8("-DVALUE=6")}};
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(packing); index += 1)
                {
                    BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(packing[index]), true, false);
                }
                // The same unguarded header is expanded with two macro environments.
                BUSTER_CACHE_WRITE(header, S8("X(5)\nX(9)\n"));
                BUSTER_CACHE_WRITE(input, S8("#define X(n) + (n)\nint cache_sum = 0\n#include \"shared.h\"\n;\n#undef X\n#define X(n) * (n)\nint cache_product = 1\n#include \"shared.h\"\n;\n"));
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_CACHE_WRITE(header, S8("#pragma once\nconst char cache_header_file[] = __FILE__;\n"));
                source = S8("#include \"shared.h\"\n#include \"shared.h\"\n#line 71 \"logical-cache.c\"\nconst char cache_file[] = __FILE__;\nint cache_line = __LINE__;\nint cache_layout = sizeof(long);\nint cache_char = ((char)-1 < 0);\nconst char cache_date[] = __DATE__;\nconst char cache_time[] = __TIME__;\n");
                BUSTER_CACHE_WRITE(input, source);
                BUSTER_CACHE_WRITE(other, source);
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                BUSTER_CACHE_REPLAY0(arguments, cache, other, true, false);
                String8 signed_char[] = {S8("-fsigned-char")};
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, S8("x86_64-pc-windows-msvc"), (SliceString8)BUSTER_ARRAY_TO_SLICE(signed_char), true, false);
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, S8("aarch64-unknown-linux-gnu"), (SliceString8){0}, true, false);
                String8 dates[] = {S8("-D__DATE__=\"captured-date\""), S8("-D__TIME__=\"captured-time\"")};
                BUSTER_CACHE_REPLAY(arguments, arena, cache, input, output, target, (SliceString8)BUSTER_ARRAY_TO_SLICE(dates), true, false);
                struct { String8 text; bool succeeds; } edits[] = {
                    {S8("#define VALUE 1\\\r\n2\r\nint cache_value(void) { return VALUE; }\r\n"), true},
                    {S8("int cache_value = 3 \x60 4;\n"), false},
                    {S8("int cache_value = 3 \x60 4;\n"), false},
                    {S8("int cache_value\nint other;\n"), false},
                    {S8("#warning retained-warning\nint cache_value(void) { return missing; }\n"), false},
                    {S8("int cache_value(void) { return __COUNTER__; }\n"), true},
                    {S8("const char* cache_base = __BASE_FILE__;\n"), true},
                    {S8("int cache_level = __INCLUDE_LEVEL__;\n"), true},
                    {S8("const char* cache_timestamp = __TIMESTAMP__;\n"), true},
                    {S8("int cache_value(void) { return 51; }\n"), true},
                };
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(edits); index += 1)
                {
                    BUSTER_CACHE_WRITE(input, edits[index].text);
                    BUSTER_CACHE_REPLAY0(arguments, cache, input, edits[index].succeeds, false);
                }
                CSourceCacheStats before_clear = c_source_cache_stats(cache);
                c_source_cache_clear(cache);
                CSourceCacheStats cleared = c_source_cache_stats(cache);
                BUSTER_TEST(arguments, !cleared.entry_count && !cleared.retained_bytes && cleared.hits == before_clear.hits && cleared.misses == before_clear.misses);
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                u64 charge = c_source_cache_stats(cache).retained_bytes;
                if (BUSTER_REQUIRE(arguments, charge > 1 && charge < BUSTER_MB(1)))
                {
                    CSourceCache* bounded = c_source_cache_create(arena, charge + charge / 2);
                    if (BUSTER_REQUIRE(arguments, bounded != 0))
                    {
                        BUSTER_CACHE_REPLAY0(arguments, bounded, input, true, false);
                        CSourceCacheStats first = c_source_cache_stats(bounded);
                        BUSTER_CACHE_WRITE(input, S8("int cache_value(void) { return 52; }\n"));
                        BUSTER_CACHE_REPLAY0(arguments, bounded, input, true, false);
                        CSourceCacheStats second = c_source_cache_stats(bounded);
                        BUSTER_TEST(arguments, second.resets > first.resets && second.retained_bytes <= second.byte_limit && second.entry_count <= 64);
                        c_source_cache_destroy(bounded);
                    }
                    CSourceCache* bypass = c_source_cache_create(arena, charge / 2);
                    if (BUSTER_REQUIRE(arguments, bypass != 0))
                    {
                        BUSTER_CACHE_REPLAY0(arguments, bypass, input, true, false);
                        CSourceCacheStats skipped = c_source_cache_stats(bypass);
                        BUSTER_TEST(arguments, skipped.bypasses && !skipped.retained_bytes && !skipped.entry_count);
                        c_source_cache_destroy(bypass);
                    }
                }
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, false);
                CSourceCacheStats observed = c_source_cache_stats(cache);
                string_print(S8("SOURCE_CACHE_REPLAY version=1 hits={u64} misses={u64} bypasses={u64} resets={u64} reused_bytes={u64} reused_tokens={u64} retained_bytes={u64} byte_limit={u64} assertions={u64} failed={u64}\n"),
                             observed.hits, observed.misses, observed.bypasses, observed.resets, observed.reused_bytes, observed.reused_tokens,
                             observed.retained_bytes, observed.byte_limit, result.test_count, result.test_count - result.succeeded_test_count);
                BUSTER_CACHE_REPLAY0(arguments, cache, input, true, true);
                CSourceCache* diagnostic_cache = c_source_cache_create(arena, BUSTER_MB(4));
                if (BUSTER_REQUIRE(arguments, diagnostic_cache != 0))
                {
                    BUSTER_CACHE_WRITE(input, S8("#line 91 \"logical-owned.c\"\n#warning owned-warning\nint cache_value(void) { return missing; }\n"));
                    BUSTER_CACHE_REPLAY0(arguments, diagnostic_cache, input, false, false);
                    BUSTER_CACHE_REPLAY0(arguments, diagnostic_cache, input, false, true);
                }
            }
            UnitTestResult lifetime = source_cache_test_preprocess_lifetime(arguments, arena, input);
            result.test_count += lifetime.test_count;
            result.succeeded_test_count += lifetime.succeeded_test_count;
            UnitTestResult lexical_key = source_cache_test_lexical_key(arguments, arena, input);
            result.test_count += lexical_key.test_count;
            result.succeeded_test_count += lexical_key.succeeded_test_count;
            String8 flags[] = {S8("-fsource-cache"), S8("-fno-source-cache"), S8("-fsyntax-only"), input};
            CompilerDriverInvocation disabled = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(flags));
            BUSTER_TEST(arguments, disabled.error == COMPILER_DRIVER_ERROR_NONE && !disabled.enable_source_cache);
            flags[0] = S8("-fno-source-cache");
            flags[1] = S8("-fsource-cache");
            CompilerDriverInvocation enabled = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(flags));
            BUSTER_TEST(arguments, enabled.error == COMPILER_DRIVER_ERROR_NONE && enabled.enable_source_cache);
            u64 name_offset = root.length;
            while (name_offset && root.pointer[name_offset - 1] != '/' && root.pointer[name_offset - 1] != '\\')
            {
                name_offset -= 1;
            }
            String8 name = {.pointer = root.pointer + name_offset, .length = root.length - name_offset};
            String8 first = string_format_z(arena, S8("{S8}/{S8}-first.c"), root, name);
            String8 second = string_format_z(arena, S8("{S8}/{S8}-second.c"), root, name);
            String8 objects[] = {string_format_z(arena, S8("{S8}-first.o"), name), string_format_z(arena, S8("{S8}-second.o"), name)};
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(objects); index += 1)
            {
                BUSTER_TEST(arguments, os_file_delete(objects[index]));
            }
            BUSTER_CACHE_WRITE(header, S8("#define VALUE 7\n"));
            BUSTER_CACHE_WRITE(first, S8("#include \"shared.h\"\nint first(void) { return VALUE; }\n"));
            BUSTER_CACHE_WRITE(second, S8("#include \"shared.h\"\nint second(void) { return VALUE; }\n"));
            String8 batch[] = {S8("-fsource-cache"), S8("-nostdinc"), S8("-g0"), S8("-target"), target, S8("-c"), first, second};
            CompilerDriverInvocation batch_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(batch));
            if (BUSTER_REQUIRE(arguments, batch_invocation.error == COMPILER_DRIVER_ERROR_NONE))
            {
                CompilerDriverResult cached_batch = compiler_driver_execute_invocation(arena, batch_invocation);
                BUSTER_TEST_RAW(arguments, cached_batch.error == COMPILER_DRIVER_ERROR_NONE, cached_batch.diagnostic);
                BUSTER_TEST(arguments, cached_batch.source_cache.hits && cached_batch.source_cache.reused_bytes && cached_batch.source_cache.reused_tokens &&
                                       cached_batch.source_cache.retained_bytes <= cached_batch.source_cache.byte_limit && cached_batch.compilation_workers == 1);
                ByteSlice cached_objects[] = {file_read(arena, objects[0], (FileReadOptions){0}), file_read(arena, objects[1], (FileReadOptions){0})};
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(objects); index += 1)
                {
                    BUSTER_TEST(arguments, os_file_delete(objects[index]));
                }
                batch_invocation.enable_source_cache = false;
                CompilerDriverResult fresh_batch = compiler_driver_execute_invocation(arena, batch_invocation);
                BUSTER_TEST_RAW(arguments, source_cache_test_diagnostics_equal(cached_batch, fresh_batch), fresh_batch.diagnostic);
                for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(objects); index += 1)
                {
                    ByteSlice actual = file_read(arena, objects[index], (FileReadOptions){0});
                    if (BUSTER_REQUIRE(arguments, cached_objects[index].length && cached_objects[index].length == actual.length && cached_objects[index].pointer && actual.pointer))
                    {
                        BUSTER_TEST(arguments, memcmp(cached_objects[index].pointer, actual.pointer, actual.length) == 0);
                    }
                }
            }
            String8 native_output = string_format_z(arena, S8("{S8}/native-link{S8}"), root,
                target_native.os == OPERATING_SYSTEM_WINDOWS ? S8(".exe") : (String8){0});
            BUSTER_CACHE_WRITE(first, S8("#include \"shared.h\"\nextern int second(void);\nint main(void) { return second() != VALUE; }\n"));
            BUSTER_CACHE_WRITE(second, S8("#include \"shared.h\"\nint second(void) { return VALUE; }\n"));
            CSourceCache* link_cache = c_source_cache_create(arena, BUSTER_MB(4));
            if (BUSTER_REQUIRE(arguments, link_cache != 0))
            {
                String8 link_args[] = {S8("-fsource-cache"), S8("-fcompile-jobs=4"),
                    S8("-g0"), S8("-nostdinc"), S8("-o"), native_output, first, second};
                CompilerDriverInvocation link_invocation = compiler_driver_parse_arguments(arena,
                    (SliceString8)BUSTER_ARRAY_TO_SLICE(link_args));
                if (BUSTER_REQUIRE(arguments, link_invocation.error == COMPILER_DRIVER_ERROR_NONE))
                {
                    link_invocation.source_cache = link_cache;
                    BUSTER_TEST(arguments, os_file_delete(native_output));
                    CompilerDriverResult cached_link = compiler_driver_execute_invocation(arena, link_invocation);
                    BUSTER_TEST_RAW(arguments, cached_link.error == COMPILER_DRIVER_ERROR_NONE, cached_link.diagnostic);
                    BUSTER_TEST(arguments, cached_link.compilation_workers == 1 && cached_link.source_cache.hits &&
                        cached_link.source_cache.reused_bytes && cached_link.source_cache.reused_tokens);
                    ByteSlice expected_link = cached_link.error == COMPILER_DRIVER_ERROR_NONE
                        ? file_read(arena, native_output, (FileReadOptions){0}) : (ByteSlice){0};
                    BUSTER_TEST(arguments, os_file_delete(native_output));
                    link_invocation.source_cache = 0;
                    link_invocation.enable_source_cache = false;
                    CompilerDriverResult fresh_link = compiler_driver_execute_invocation(arena, link_invocation);
                    BUSTER_TEST_RAW(arguments, source_cache_test_diagnostics_equal(cached_link, fresh_link), fresh_link.diagnostic);
                    if (BUSTER_REQUIRE(arguments, cached_link.error == COMPILER_DRIVER_ERROR_NONE &&
                        fresh_link.error == COMPILER_DRIVER_ERROR_NONE))
                    {
                        ByteSlice actual_link = file_read(arena, native_output, (FileReadOptions){0});
                        if (BUSTER_REQUIRE(arguments, expected_link.pointer && actual_link.pointer &&
                            expected_link.length && expected_link.length == actual_link.length))
                        {
                            BUSTER_TEST(arguments, memcmp(expected_link.pointer, actual_link.pointer, actual_link.length) == 0);
                        }
                    }
                    BUSTER_TEST(arguments, os_file_delete(native_output));
                }
                c_source_cache_destroy(link_cache);
            }
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(objects); index += 1)
            {
                BUSTER_TEST(arguments, os_file_delete(objects[index]));
            }
            BUSTER_TEST(arguments, os_directory_delete(root));
        }
        BUSTER_TEST(arguments, arena_destroy(arena, 1));
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
#undef BUSTER_CACHE_WRITE
#undef BUSTER_CACHE_REPLAY0
#undef BUSTER_CACHE_REPLAY
#endif
