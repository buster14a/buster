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

