#include <buster/tests/compiler/frontend/c/once_test.h>

#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/frontend/c/c_source_internal.h>
#include <buster/lib/hash.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/system_headers.h>

#if BUSTER_INCLUDE_TESTS

// These fixtures intentionally keep the root source smaller than the include
// closure.  The old once-path array used the root byte count as its capacity,
// so the distinct-import cases cross that boundary while remaining small
// enough for every supported test runner.
#define C_ONCE_TEST_IMPORT_COUNT 96u
#define C_ONCE_TEST_PRAGMA_COUNT 96u
#define C_ONCE_TEST_LOOKUP_COUNT 96u
#define C_ONCE_TEST_DEPTH_COUNT 16u
#define C_ONCE_TEST_DEPTH_LIMIT 8u
#define C_ONCE_TEST_SCALE_DEPTH 24u

BUSTER_GLOBAL_LOCAL String8 c_once_test_directory(String8 path)
{
    String8 result = {0};
    u64 index = path.length;
    while (index && !result.length)
    {
        index -= 1;
        if (path.pointer[index] == '/' || path.pointer[index] == '\\')
        {
            result = string_slice(path, 0, index);
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 c_once_test_child_path(Arena* arena, String8 directory, String8 name)
{
    String8 result = string_format_z(arena, S8("{S8}/{S8}"), directory, name);
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write(String8 path, String8 source)
{
    ByteSlice bytes = {
        .pointer = (u8*)source.pointer,
        .length = source.length,
    };
    bool result = file_write(path, bytes);
    return result;
}

typedef enum COnceTestLink
{
    C_ONCE_TEST_LINK_CREATED,
    C_ONCE_TEST_LINK_UNSUPPORTED,
    C_ONCE_TEST_LINK_FAILED,
} COnceTestLink;

// Creates `link_name` beside `target_name`. Windows symbolic-link privilege
// and mobile sandbox/storage refusals are explicit skips; every other failure
// is a regression in the real alias fixture.
BUSTER_GLOBAL_LOCAL COnceTestLink c_once_test_link(UnitTestArguments* arguments, Arena* arena, bool symbolic, String8 directory,
                                                   String8 target_name, String8 link_name)
{
    String8 target = c_once_test_child_path(arena, directory, target_name);
    String8 path = c_once_test_child_path(arena, directory, link_name);
    os_file_delete(path);
    bool created;
    u32 error;
    bool unsupported;
#if BUSTER_WINDOWS
    String16 path_w = string16_from_string8(arena, path, true);
    if (symbolic)
    {
        String16 target_name_w = string16_from_string8(arena, target_name, true);
        created = CreateSymbolicLinkW(path_w.pointer, target_name_w.pointer, 0x2) != 0;
        error = created ? 0 : (u32)GetLastError();
        if (error == (u32)ERROR_INVALID_PARAMETER)
        {
            created = CreateSymbolicLinkW(path_w.pointer, target_name_w.pointer, 0) != 0;
            error = created ? 0 : (u32)GetLastError();
        }
    }
    else
    {
        String16 target_w = string16_from_string8(arena, target, true);
        created = CreateHardLinkW(path_w.pointer, target_w.pointer, 0) != 0;
        error = created ? 0 : (u32)GetLastError();
    }
    unsupported = symbolic && error == (u32)ERROR_PRIVILEGE_NOT_HELD;
#else
    if (symbolic)
    {
        created = symlink((const char*)target_name.pointer, (const char*)path.pointer) == 0;
    }
    else
    {
        created = link((const char*)target.pointer, (const char*)path.pointer) == 0;
    }
    error = created ? 0 : (u32)errno;
    unsupported = (BUSTER_ANDROID || BUSTER_IOS) &&
                  (error == (u32)EACCES || error == (u32)EPERM || error == (u32)EROFS);
#endif
    COnceTestLink result = created ? C_ONCE_TEST_LINK_CREATED :
                           unsupported ? C_ONCE_TEST_LINK_UNSUPPORTED : C_ONCE_TEST_LINK_FAILED;
    if (!created)
    {
        arguments->show(arguments, S8("C_ONCE_LINK kind={S8} status={S8} error={u32}\n"),
                        symbolic ? S8("symbolic") : S8("hard"),
                        unsupported ? S8("unsupported") : S8("failed"), error);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_once_test_file_identity_equal(FileIdentity left, FileIdentity right)
{
    bool result = left.valid && right.valid && left.device == right.device && left.index == right.index;
    return result;
}

BUSTER_GLOBAL_LOCAL void c_once_test_append(char8* destination, u64 capacity, u64* length, String8 source)
{
    BUSTER_CHECK(*length <= capacity && source.length <= capacity - *length);
    memcpy(destination + *length, source.pointer, source.length);
    *length += source.length;
}

BUSTER_GLOBAL_LOCAL String8 c_once_test_include_lines(Arena* arena, String8 prefix, String8 directive, u32 count, u32 repetitions)
{
    u64 capacity = (u64)count * repetitions * 96 + 1;
    char8* bytes = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    for (u32 index = 0; index < count; index += 1)
    {
        for (u32 repetition = 0; repetition < repetitions; repetition += 1)
        {
            String8 line = string_format(arena, S8("{S8} \"{S8}-{u32}.h\"\n"), directive, prefix, index);
            c_once_test_append(bytes, capacity, &length, line);
        }
    }
    return (String8){.pointer = bytes, .length = length};
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write_number_leaves(Arena* arena, String8 directory, String8 prefix, u32 count)
{
    bool result = true;
    for (u32 index = 0; index < count; index += 1)
    {
        String8 name = string_format(arena, S8("{S8}-{u32}.h"), prefix, index);
        String8 path = c_once_test_child_path(arena, directory, name);
        String8 source = string_format(arena, S8("{u32}\n"), index);
        result = c_once_test_write(path, source) && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool c_once_test_write_pragma_leaves(Arena* arena, String8 directory, String8 prefix, u32 count)
{
    bool result = true;
    for (u32 index = 0; index < count; index += 1)
    {
        String8 name = string_format(arena, S8("{S8}-{u32}.h"), prefix, index);
        String8 path = c_once_test_child_path(arena, directory, name);
        String8 source = string_format(arena, S8("#pragma once\n{u32}\n"), index);
        result = c_once_test_write(path, source) && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void c_once_test_numbers(UnitTestArguments* arguments, UnitTestResult* outer_result, CPreprocessResult preprocess,
                                             u32 count)
{
    UnitTestResult result = {0};
    BUSTER_TEST(arguments, preprocess.diagnostic_count == 0);
    BUSTER_TEST(arguments, preprocess.token_count == (u64)count + 1);
    if (BUSTER_REQUIRE(arguments, preprocess.tokens != 0 && preprocess.spelling_base != 0 && preprocess.token_count >= (u64)count + 1))
    {
        for (u32 index = 0; index < count; index += 1)
        {
            BUSTER_TEST(arguments, preprocess.tokens[index].kind == C_TOKEN_PREPROCESSING_NUMBER);
            String8 expected = string_format(arguments->arena, S8("{u32}"), index);
            BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]), expected);
        }
        BUSTER_TEST(arguments, preprocess.tokens[count].kind == C_TOKEN_END_OF_FILE);
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL void c_once_test_single_lexed_rows(UnitTestArguments* arguments, UnitTestResult* outer_result,
                                                       CPreprocessResult preprocess, u32 expected_file_count)
{
    UnitTestResult result = {0};
    CPreprocessDetail const* detail = c_preprocess_detail(preprocess);
    BUSTER_TEST(arguments, detail->source_lexed.files == expected_file_count);
    BUSTER_TEST(arguments, detail->source_unique.files == expected_file_count);
    BUSTER_TEST(arguments, detail->lexed_file_count == expected_file_count);
    if (BUSTER_REQUIRE(arguments, detail->lexed_files != 0 && detail->lexed_file_count == expected_file_count))
    {
        for (u32 index = 0; index < expected_file_count; index += 1)
        {
            BUSTER_TEST(arguments, detail->lexed_files[index].lex_count == 1);
        }
    }
    outer_result->test_count += result.test_count;
    outer_result->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_import_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-import-growth"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-import-leaf");
    String8 fanout_name = S8("buster-issue81-import-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#import"), C_ONCE_TEST_IMPORT_COUNT, 1);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_number_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_IMPORT_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_IMPORT_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_IMPORT_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_pragma_growth(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-pragma-growth"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-pragma-leaf");
    String8 fanout_name = S8("buster-issue81-pragma-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#include"), C_ONCE_TEST_PRAGMA_COUNT, 2);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_pragma_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_PRAGMA_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_PRAGMA_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_PRAGMA_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_duplicate_imports(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-lookup-work"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 prefix = S8("buster-issue81-lookup-leaf");
    String8 fanout_name = S8("buster-issue81-lookup-fanout.h");
    String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
    // Each leaf is imported twice. This fixture proves suppression and one
    // lex per file; c_once_test_table_probe_scaling measures actual probes.
    String8 fanout_source = c_once_test_include_lines(temporary.arena, prefix, S8("#import"), C_ONCE_TEST_LOOKUP_COUNT, 2);
    String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
    bool files_written = c_once_test_write(root_path, root_source);
    files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
    files_written = c_once_test_write_number_leaves(temporary.arena, directory, prefix, C_ONCE_TEST_LOOKUP_COUNT) && files_written;
    BUSTER_TEST(arguments, root_path.pointer != 0 && directory.pointer != 0);
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, root_path.pointer != 0 && directory.pointer != 0 && files_written))
    {
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        c_once_test_numbers(arguments, &result, preprocess, C_ONCE_TEST_LOOKUP_COUNT);
        c_once_test_single_lexed_rows(arguments, &result, preprocess, C_ONCE_TEST_LOOKUP_COUNT + 2);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_failures(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 missing_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-missing-include"), S8(".c"));
    String8 malformed_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-malformed-include"), S8(".c"));
    String8 depth_root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-depth-limit"), S8(".c"));
    String8 directory = c_once_test_directory(missing_root_path);
    String8 valid_name = S8("buster-issue81-valid.h");
    String8 valid_path = c_once_test_child_path(temporary.arena, directory, valid_name);
    bool files_written = c_once_test_write(valid_path, S8("71\n"));
    for (u32 index = 0; index < C_ONCE_TEST_DEPTH_COUNT; index += 1)
    {
        String8 name = string_format(temporary.arena, S8("buster-issue81-depth-{u32}.h"), index);
        String8 path = c_once_test_child_path(temporary.arena, directory, name);
        String8 source = index + 1 < C_ONCE_TEST_DEPTH_COUNT
                             ? string_format(temporary.arena, S8("#include \"buster-issue81-depth-{u32}.h\"\n"), index + 1)
                             : S8("91\n");
        files_written = c_once_test_write(path, source) && files_written;
    }
    BUSTER_TEST(arguments, missing_root_path.pointer != 0 && malformed_root_path.pointer != 0 && depth_root_path.pointer != 0);
    BUSTER_TEST(arguments, directory.pointer != 0 && files_written);
    if (BUSTER_REQUIRE(arguments, directory.pointer != 0 && files_written))
    {
        CPreprocessResult missing = c_preprocess(temporary.arena,
                                                 string_format(temporary.arena,
                                                               S8("#include \"buster-issue81-absent.h\"\n#include \"{S8}\"\n"), valid_name),
                                                 (CPreprocessOptions){.source_path = missing_root_path});
        BUSTER_TEST(arguments, missing.diagnostic_count == 1 && missing.error_count == 1);
        if (BUSTER_REQUIRE(arguments, missing.diagnostic_count == 1 && missing.diagnostics != 0))
        {
            BUSTER_TEST(arguments, missing.diagnostics[0].kind == C_DIAGNOSTIC_INCLUDE_NOT_FOUND);
        }
        BUSTER_TEST(arguments, missing.token_count == 2);
        if (BUSTER_REQUIRE(arguments, missing.tokens != 0 && missing.spelling_base != 0 && missing.token_count >= 1))
        {
            BUSTER_TEST(arguments, missing.tokens[0].kind == C_TOKEN_PREPROCESSING_NUMBER);
            BUSTER_STRING_TEST(arguments, c_token_spelling(missing.spelling_base, missing.tokens[0]), S8("71"));
        }

        CPreprocessResult malformed = c_preprocess(temporary.arena, S8("#include malformed_header.h\n"),
                                                   (CPreprocessOptions){.source_path = malformed_root_path});
        BUSTER_TEST(arguments, malformed.diagnostic_count == 1 && malformed.error_count == 1);
        if (BUSTER_REQUIRE(arguments, malformed.diagnostic_count == 1 && malformed.diagnostics != 0))
        {
            BUSTER_TEST(arguments, malformed.diagnostics[0].kind == C_DIAGNOSTIC_INVALID_INCLUDE);
        }
        BUSTER_TEST(arguments, malformed.token_count == 1);

        CPreprocessResult depth = c_preprocess(temporary.arena,
                                               S8("#include \"buster-issue81-depth-0.h\"\n"),
                                               (CPreprocessOptions){
                                                   .source_path = depth_root_path,
                                                   .include_depth_limit = C_ONCE_TEST_DEPTH_LIMIT,
                                               });
        BUSTER_TEST(arguments, depth.diagnostic_count == 1 && depth.error_count == 1);
        if (BUSTER_REQUIRE(arguments, depth.diagnostic_count == 1 && depth.diagnostics != 0))
        {
            BUSTER_TEST(arguments, depth.diagnostics[0].kind == C_DIAGNOSTIC_INCLUDE_DEPTH);
        }
        BUSTER_TEST(arguments, depth.token_count == 1);
    }
    scratch_end(temporary);
    return result;
}

// Direct table regressions: no filesystem identities are synthesized here.
// The physical-key rows test the index only, not the still-pending loader API.
#define C_ONCE_TEST_PROBE_REPETITIONS 4u

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_table_hashes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CIncludeFileTable table = {.arena = temporary.arena};
    u32 even_hashes = 0;
    for (u32 index = 0; index < 128; index += 1)
    {
        String8 path = string_format(arguments->arena, S8("once-hash-{u32}.h"), index);
        u64 hash = buster_hash_64((u8*)path.pointer, path.length);
        even_hashes += hash != 0 && !(hash & 1);
        CIncludeFileEntry* entry = 0;
        CIncludeFileStatus status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = path}, path, &entry);
        if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
        {
            BUSTER_TEST(arguments, entry->hash == (hash ? hash : 1));
        }
    }
    BUSTER_TEST(arguments, even_hashes != 0);
    BUSTER_TEST(arguments, table.count == 128);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_table_probe_scaling(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 counts[] = {128, 512, 2048};
    u64 previous_probes = 0;
    for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(counts); size_index += 1)
    {
        u32 count = counts[size_index];
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        CIncludeFileTable table = {.arena = temporary.arena};
        String8* paths = arena_allocate(temporary.arena, String8, count);
        for (u32 index = 0; index < count; index += 1)
        {
            paths[index] = string_format(temporary.arena, S8("once-probe-{u32}.h"), index);
            CIncludeFileEntry* entry = 0;
            CIncludeFileStatus status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[index]}, paths[index], &entry);
            if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
            {
                entry->guard_symbol = index + 1;
                entry->once = (index & 1) != 0;
            }
        }
        for (u32 repetition = 0; repetition < C_ONCE_TEST_PROBE_REPETITIONS; repetition += 1)
        {
            for (u32 query = 0; query < count; query += 1)
            {
                // An odd multiplier permutes each power-of-two workload.
                u32 index = (query * 17 + repetition) & (count - 1);
                CIncludeFileEntry* entry = 0;
                CIncludeFileStatus status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[index]}, paths[index], &entry);
                if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
                {
                    BUSTER_TEST(arguments, entry->guard_symbol == index + 1);
                    BUSTER_TEST(arguments, entry->once == ((index & 1) != 0));
                    BUSTER_STRING_TEST(arguments, entry->spelling, paths[index]);
                }
            }
        }
        u64 operations = (u64)count * (C_ONCE_TEST_PROBE_REPETITIONS + 1);
        BUSTER_TEST(arguments, table.count == count);
        BUSTER_TEST(arguments, table.capacity == count * 2);
        // Count actual slot examinations, including rehash work; not elapsed
        // time or the number of files lexed. A linear scan fails these bounds.
        BUSTER_TEST(arguments, table.probe_count >= operations);
        BUSTER_TEST(arguments, table.probe_count <= operations * 12);
        if (previous_probes)
        {
            BUSTER_TEST(arguments, table.probe_count <= previous_probes * 6);
        }
        arguments->show(arguments, S8("C_ONCE_PROBES_V1 entries={u32} operations={u64} probes={u64}\n"), count, operations, table.probe_count);
        previous_probes = table.probe_count;
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_table_allocation_failure(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    // A dedicated real arena keeps injected reservation limits away from
    // diagnostics, fixture accounting and the caller's scratch allocators.
    Arena* arena = arena_create((ArenaCreation){.reserved_size = 1u << 20});
    if (BUSTER_REQUIRE(arguments, arena != 0))
    {
        CIncludeFileTable table = {.arena = arena};
        CIncludeFileEntry sentinel = {0};
        CIncludeFileEntry* entry = &sentinel;
        u64 reservation = arena->reserved_size;
        u64 position = arena->position;
        arena->reserved_size = position;
        CIncludeFileStatus status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = S8("initial.h")}, S8("initial.h"), &entry);
        arena->reserved_size = reservation;
        BUSTER_TEST(arguments, status == C_INCLUDE_FILE_ALLOCATION_FAILED);
        BUSTER_TEST(arguments, entry == 0 && table.entries == 0 && table.count == 0 && table.capacity == 0);
        BUSTER_TEST(arguments, arena->position == position);

        // Reject a reservation that fits the bytes but not alignment padding.
        arena_allocate(arena, u8, 1);
        position = arena->position;
        arena->reserved_size = position + C_INCLUDE_FILE_INITIAL_CAPACITY * sizeof(CIncludeFileEntry);
        entry = &sentinel;
        status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = S8("aligned.h")}, S8("aligned.h"), &entry);
        arena->reserved_size = reservation;
        BUSTER_TEST(arguments, status == C_INCLUDE_FILE_ALLOCATION_FAILED && entry == 0);
        BUSTER_TEST(arguments, table.entries == 0 && table.count == 0 && table.capacity == 0);
        BUSTER_TEST(arguments, arena->position == position);

        String8 paths[C_INCLUDE_FILE_INITIAL_CAPACITY / 2 + 1];
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(paths); index += 1)
        {
            paths[index] = string_format(arguments->arena, S8("once-failure-{u32}.h"), index);
        }
        for (u32 index = 0; index < C_INCLUDE_FILE_INITIAL_CAPACITY / 2; index += 1)
        {
            entry = 0;
            status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[index]}, paths[index], &entry);
            if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
            {
                entry->once = true;
                entry->guard_symbol = index + 1;
            }
        }
        CIncludeFileEntry* entries = table.entries;
        u32 count = table.count;
        u32 capacity = table.capacity;
        position = arena->position;
        arena->reserved_size = position;
        entry = &sentinel;
        status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[C_INCLUDE_FILE_INITIAL_CAPACITY / 2]}, paths[C_INCLUDE_FILE_INITIAL_CAPACITY / 2], &entry);
        BUSTER_TEST(arguments, status == C_INCLUDE_FILE_ALLOCATION_FAILED && entry == 0);
        BUSTER_TEST(arguments, table.entries == entries && table.count == count && table.capacity == capacity);
        BUSTER_TEST(arguments, arena->position == position);
        // Hits still succeed without growth, including at an exhausted limit.
        for (u32 index = 0; index < count; index += 1)
        {
            entry = 0;
            status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[index]}, paths[index], &entry);
            if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
            {
                BUSTER_TEST(arguments, entry->once && entry->guard_symbol == index + 1);
                BUSTER_STRING_TEST(arguments, entry->spelling, paths[index]);
            }
        }
        arena->reserved_size = reservation;
        entry = 0;
        status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = paths[C_INCLUDE_FILE_INITIAL_CAPACITY / 2]}, paths[C_INCLUDE_FILE_INITIAL_CAPACITY / 2], &entry);
        BUSTER_TEST(arguments, status == C_INCLUDE_FILE_OK && entry != 0);
        BUSTER_TEST(arguments, table.count == count + 1 && table.capacity == capacity * 2);
        // The doubling overflow guard must reject before allocating or reading
        // an entry. This is a synthetic capacity boundary, not a huge mapping.
        CIncludeFileTable overflow = {.arena = arena, .capacity = UINT32_MAX / 2 + 1};
        position = arena->position;
        BUSTER_TEST(arguments, !c_test_include_file_table_grow(&overflow));
        BUSTER_TEST(arguments, overflow.entries == 0 && overflow.count == 0 && overflow.capacity == UINT32_MAX / 2 + 1);
        BUSTER_TEST(arguments, arena->position == position);
        BUSTER_TEST(arguments, arena_destroy(arena, 1));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_table_identity(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    CIncludeFileTable table = {.arena = temporary.arena};
    CIncludeFileEntry sentinel = {0};
    CIncludeFileEntry* entry = &sentinel;
    CIncludeFileStatus status = c_test_include_file_entry(&table, (CIncludeFileIdentity){0}, S8("invalid.h"), &entry);
    BUSTER_TEST(arguments, status == C_INCLUDE_FILE_INVALID_IDENTITY && entry == 0 && table.count == 0);
    CIncludeFileIdentity physical = {.physical = true, .device = 3, .index = 7};
    status = c_test_include_file_entry(&table, physical, S8("first-spelling.h"), &entry);
    if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
    {
        entry->once = true;
        entry->guard_symbol = 17;
        status = c_test_include_file_entry(&table, physical, S8("alias-spelling.h"), &entry);
        if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
        {
            BUSTER_TEST(arguments, entry->once && entry->guard_symbol == 17 && table.count == 1);
            BUSTER_STRING_TEST(arguments, entry->spelling, S8("first-spelling.h"));
        }
    }
    status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.path = S8("first-spelling.h")}, S8("first-spelling.h"), &entry);
    if (BUSTER_REQUIRE(arguments, status == C_INCLUDE_FILE_OK && entry != 0))
    {
        BUSTER_TEST(arguments, !entry->physical && !entry->once && entry->guard_symbol == 0 && table.count == 2);
    }
    status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.physical = true, .device = 4, .index = 7}, S8("other-device.h"), &entry);
    BUSTER_TEST(arguments, status == C_INCLUDE_FILE_OK && table.count == 3);
    status = c_test_include_file_entry(&table, (CIncludeFileIdentity){.physical = true, .device = 3, .index = 8}, S8("other-index.h"), &entry);
    BUSTER_TEST(arguments, status == C_INCLUDE_FILE_OK && table.count == 4);
    scratch_end(temporary);
    return result;
}


BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_filesystem_aliases(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-physical-aliases"), S8(".c"));
    String8 directory = c_once_test_directory(root_path);
    String8 nested_name = S8("buster-issue81-alias-nested");
    String8 nested_path = c_once_test_child_path(temporary.arena, directory, nested_name);
    String8 names[] = {
        S8("buster-issue81-alias-pragma.h"),
        S8("buster-issue81-alias-guard.h"),
        S8("buster-issue81-alias-import.h"),
    };
    String8 hard_names[] = {
        S8("buster-issue81-alias-pragma-hard.h"),
        S8("buster-issue81-alias-guard-hard.h"),
        S8("buster-issue81-alias-import-hard.h"),
    };
    String8 symbolic_names[] = {
        S8("buster-issue81-alias-pragma-symbolic.h"),
        S8("buster-issue81-alias-guard-symbolic.h"),
        S8("buster-issue81-alias-import-symbolic.h"),
    };
    String8 case_names[] = {
        S8("BUSTER-ISSUE81-ALIAS-PRAGMA.H"),
        S8("BUSTER-ISSUE81-ALIAS-GUARD.H"),
        S8("BUSTER-ISSUE81-ALIAS-IMPORT.H"),
    };
    String8 directives[] = {S8("#include"), S8("#include"), S8("#import")};
    String8 sources[] = {
        S8("#pragma once\n811\n"),
        S8("#ifndef BUSTER_ISSUE81_ALIAS_GUARD\n#define BUSTER_ISSUE81_ALIAS_GUARD\n812\n#endif\n"),
        S8("813\n"),
    };
    bool files_written = root_path.pointer && directory.pointer && os_make_directory_attempt(nested_path);
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        files_written = c_once_test_write(c_once_test_child_path(temporary.arena, directory, names[index]), sources[index]) && files_written;
    }
    COnceTestLink hard_links[BUSTER_ARRAY_LENGTH(names)];
    COnceTestLink symbolic_links[BUSTER_ARRAY_LENGTH(names)];
    bool case_aliases[BUSTER_ARRAY_LENGTH(names)];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        hard_links[index] = C_ONCE_TEST_LINK_FAILED;
        symbolic_links[index] = C_ONCE_TEST_LINK_FAILED;
        case_aliases[index] = false;
    }
    BUSTER_TEST(arguments, files_written);
    if (BUSTER_REQUIRE(arguments, files_written))
    {
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
        {
            hard_links[index] = c_once_test_link(arguments, temporary.arena, false, directory, names[index], hard_names[index]);
            symbolic_links[index] = c_once_test_link(arguments, temporary.arena, true, directory, names[index], symbolic_names[index]);
            BUSTER_TEST(arguments, hard_links[index] != C_ONCE_TEST_LINK_FAILED);
            BUSTER_TEST(arguments, symbolic_links[index] != C_ONCE_TEST_LINK_FAILED);

            FileMapRead original = file_map_read(temporary.arena, c_once_test_child_path(temporary.arena, directory, names[index]),
                                                 (FileReadOptions){0});
            BUSTER_TEST(arguments, original.bytes.pointer != 0 && original.identity.valid);
            if (hard_links[index] == C_ONCE_TEST_LINK_CREATED)
            {
                FileMapRead hard = file_map_read(temporary.arena, c_once_test_child_path(temporary.arena, directory, hard_names[index]),
                                                 (FileReadOptions){0});
                BUSTER_TEST(arguments, hard.bytes.pointer != 0 && c_once_test_file_identity_equal(original.identity, hard.identity));
                file_map_unmap(hard);
            }
            if (symbolic_links[index] == C_ONCE_TEST_LINK_CREATED)
            {
                FileMapRead symbolic = file_map_read(temporary.arena,
                                                     c_once_test_child_path(temporary.arena, directory, symbolic_names[index]),
                                                     (FileReadOptions){0});
                BUSTER_TEST(arguments, symbolic.bytes.pointer != 0 &&
                                           c_once_test_file_identity_equal(original.identity, symbolic.identity));
                file_map_unmap(symbolic);
            }
            FileMapRead case_alias = file_map_read(temporary.arena,
                                                   c_once_test_child_path(temporary.arena, directory, case_names[index]),
                                                   (FileReadOptions){0});
            case_aliases[index] = case_alias.bytes.pointer != 0;
#if BUSTER_WINDOWS
            BUSTER_TEST(arguments, case_aliases[index]);
#endif
            if (case_aliases[index])
            {
                BUSTER_TEST(arguments, c_once_test_file_identity_equal(original.identity, case_alias.identity));
            }
            file_map_unmap(case_alias);
            file_map_unmap(original);
        }

        u64 root_capacity = BUSTER_KB(16);
        char8* root_bytes = arena_allocate(temporary.arena, char8, root_capacity);
        u64 root_length = 0;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
        {
            c_once_test_append(root_bytes, root_capacity, &root_length,
                               string_format(temporary.arena, S8("{S8} \"{S8}\"\n"), directives[index], names[index]));
            c_once_test_append(root_bytes, root_capacity, &root_length,
                               string_format(temporary.arena, S8("{S8} \"./{S8}\"\n"), directives[index], names[index]));
            c_once_test_append(root_bytes, root_capacity, &root_length,
                               string_format(temporary.arena, S8("{S8} \"{S8}/../{S8}\"\n"),
                                             directives[index], nested_name, names[index]));
            if (hard_links[index] == C_ONCE_TEST_LINK_CREATED)
            {
                c_once_test_append(root_bytes, root_capacity, &root_length,
                                   string_format(temporary.arena, S8("{S8} \"{S8}\"\n"), directives[index], hard_names[index]));
            }
            if (symbolic_links[index] == C_ONCE_TEST_LINK_CREATED)
            {
                c_once_test_append(root_bytes, root_capacity, &root_length,
                                   string_format(temporary.arena, S8("{S8} \"{S8}\"\n"), directives[index], symbolic_names[index]));
            }
            if (case_aliases[index])
            {
                c_once_test_append(root_bytes, root_capacity, &root_length,
                                   string_format(temporary.arena, S8("{S8} \"{S8}\"\n"), directives[index], case_names[index]));
            }
        }
        String8 root_source = {.pointer = root_bytes, .length = root_length};
        BUSTER_TEST(arguments, c_once_test_write(root_path, root_source));
        CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
        BUSTER_TEST(arguments, preprocess.diagnostic_count == 0 && preprocess.error_count == 0);
        BUSTER_TEST(arguments, preprocess.token_count == 4);
        u32 expected[] = {811, 812, 813};
        if (BUSTER_REQUIRE(arguments, preprocess.tokens != 0 && preprocess.spelling_base != 0 && preprocess.token_count == 4))
        {
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
            {
                BUSTER_TEST(arguments, preprocess.tokens[index].kind == C_TOKEN_PREPROCESSING_NUMBER);
                BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index]),
                                   string_format(arguments->arena, S8("{u32}"), expected[index]));
            }
            BUSTER_TEST(arguments, preprocess.tokens[3].kind == C_TOKEN_END_OF_FILE);
        }
        CPreprocessDetail const* detail = c_preprocess_detail(preprocess);
        BUSTER_TEST(arguments, detail->source_lexed.files == 4 && detail->source_unique.files == 4);
        u32 lexed_once = 0;
        for (u32 index = 0; index < detail->lexed_file_count; index += 1)
        {
            BUSTER_TEST(arguments, detail->lexed_files[index].lex_count <= 1);
            lexed_once += detail->lexed_files[index].lex_count == 1;
        }
        BUSTER_TEST(arguments, lexed_once == 4);
        BUSTER_TEST(arguments, detail->include_file_probe_count != 0);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_once_test_preprocess_probe_scaling(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 counts[] = {64, 256};
    for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(counts); size_index += 1)
    {
        u32 count = counts[size_index];
        TemporalArena temporary = scratch_begin(&arguments->arena, 1);
        String8 root_path = buster_test_temporary_path(temporary.arena, S8("buster-issue81-preprocess-scaling"),
                                                       string_format(temporary.arena, S8("-{u32}.c"), count));
        String8 directory = c_once_test_directory(root_path);
        String8 fanout_name = string_format(temporary.arena, S8("buster-issue81-scale-{u32}-fanout.h"), count);
        String8 fanout_path = c_once_test_child_path(temporary.arena, directory, fanout_name);
        u64 fanout_capacity = (u64)count * 192 + 1;
        char8* fanout_bytes = arena_allocate(temporary.arena, char8, fanout_capacity);
        u64 fanout_length = 0;
        bool files_written = root_path.pointer && directory.pointer;
        for (u32 depth = 0; depth < C_ONCE_TEST_SCALE_DEPTH; depth += 1)
        {
            String8 name = string_format(temporary.arena, S8("buster-issue81-scale-{u32}-depth-{u32}.h"), count, depth);
            String8 path = c_once_test_child_path(temporary.arena, directory, name);
            String8 source = depth + 1 < C_ONCE_TEST_SCALE_DEPTH
                                 ? string_format(temporary.arena,
                                                 S8("#import \"buster-issue81-scale-{u32}-depth-{u32}.h\"\n"), count, depth + 1)
                                 : S8("9000\n");
            files_written = c_once_test_write(path, source) && files_written;
        }
        for (u32 index = 0; index < count; index += 1)
        {
            String8 name = string_format(temporary.arena, S8("buster-issue81-scale-{u32}-leaf-{u32}.h"), count, index);
            String8 path = c_once_test_child_path(temporary.arena, directory, name);
            String8 source = string_format(temporary.arena,
                                           S8("#import \"buster-issue81-scale-{u32}-depth-0.h\"\n{u32}\n"), count, index);
            files_written = c_once_test_write(path, source) && files_written;
            c_once_test_append(fanout_bytes, fanout_capacity, &fanout_length,
                               string_format(temporary.arena, S8("#import \"{S8}\"\n#import \"./{S8}\"\n"), name, name));
        }
        String8 fanout_source = {.pointer = fanout_bytes, .length = fanout_length};
        String8 root_source = string_format(temporary.arena, S8("#include \"{S8}\"\n"), fanout_name);
        files_written = c_once_test_write(fanout_path, fanout_source) && files_written;
        files_written = c_once_test_write(root_path, root_source) && files_written;
        BUSTER_TEST(arguments, files_written);
        if (BUSTER_REQUIRE(arguments, files_written))
        {
            CPreprocessResult preprocess = c_preprocess(temporary.arena, root_source, (CPreprocessOptions){.source_path = root_path});
            BUSTER_TEST(arguments, preprocess.diagnostic_count == 0 && preprocess.error_count == 0);
            BUSTER_TEST(arguments, preprocess.token_count == (u64)count + 2);
            if (BUSTER_REQUIRE(arguments, preprocess.tokens != 0 && preprocess.spelling_base != 0 &&
                                              preprocess.token_count == (u64)count + 2))
            {
                BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[0]), S8("9000"));
                for (u32 index = 0; index < count; index += 1)
                {
                    BUSTER_STRING_TEST(arguments, c_token_spelling(preprocess.spelling_base, preprocess.tokens[index + 1]),
                                       string_format(arguments->arena, S8("{u32}"), index));
                }
                BUSTER_TEST(arguments, preprocess.tokens[count + 1].kind == C_TOKEN_END_OF_FILE);
            }
            CPreprocessDetail const* detail = c_preprocess_detail(preprocess);
            u64 operations = (u64)count * 3 + C_ONCE_TEST_SCALE_DEPTH;
            u64 probes = detail->include_file_probe_count;
            BUSTER_TEST(arguments, detail->source_lexed.files == (u64)count + C_ONCE_TEST_SCALE_DEPTH + 2);
            BUSTER_TEST(arguments, detail->source_unique.files == (u64)count + C_ONCE_TEST_SCALE_DEPTH + 2);
            BUSTER_TEST(arguments, probes >= operations);
            // Physical file identities depend on the simulator's filesystem.
            // Bound each workload by its own include operations: a lucky
            // small table must not make an independent larger table fail.
            BUSTER_TEST(arguments, probes <= operations * 8);
            arguments->show(arguments, S8("C_ONCE_PREPROCESS_PROBES_V1 entries={u32} operations={u64} probes={u64}\n"),
                            count + C_ONCE_TEST_SCALE_DEPTH + 1, operations, probes);
        }
        scratch_end(temporary);
    }
    return result;
}

UnitTestResult c_once_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    BUSTER_TEST_FIXTURE(arguments, c_once_test_import_growth);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_pragma_growth);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_duplicate_imports);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_failures);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_hashes);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_probe_scaling);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_allocation_failure);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_table_identity);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_filesystem_aliases);
    BUSTER_TEST_FIXTURE(arguments, c_once_test_preprocess_probe_scaling);
    return result;
}

#endif
