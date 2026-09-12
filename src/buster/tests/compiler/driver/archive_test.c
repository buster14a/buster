// Archive extraction oracle and deterministic generated graphs. The retained
// scan oracle deliberately shares no name-state/worklist code with production.
#include <buster/lib/compiler/driver/archive_internal.h>

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_test_needed(ObjectFile* member, ObjectFile* selected, u32 count)
{
    bool result = false;
    bool weak_extracts = object_format_for_target(member->target) != OBJECT_FORMAT_ELF64;
    for (u32 member_index = 0; member_index < member->symbol_count && !result; member_index += 1)
    {
        ObjectSymbol* definition = &member->symbols[member_index];
        if (!definition->global || definition->section == OBJECT_SECTION_UNDEFINED) continue;
        bool unresolved = false;
        bool defined = false;
        for (u32 object_index = 0; object_index < count; object_index += 1)
        {
            ObjectFile* object = &selected[object_index];
            for (u32 index = 0; index < object->symbol_count; index += 1)
            {
                ObjectSymbol* symbol = &object->symbols[index];
                if (!symbol->global || !string_equal(symbol->name, definition->name)) continue;
                if (symbol->section == OBJECT_SECTION_UNDEFINED) unresolved |= !symbol->weak || weak_extracts;
                else defined = true;
            }
        }
        result = unresolved && !defined;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_archive_test_scan(Arena* arena, ObjectArchive* archive, ObjectFile* selected, u32* count)
{
    bool* used = arena_allocate_zeroed(arena, bool, archive->object_count);
    bool added;
    do
    {
        added = false;
        for (u32 index = 0; index < archive->object_count; index += 1)
        {
            if (!used[index] && compiler_driver_archive_test_needed(&archive->objects[index], selected, *count))
            {
                used[index] = true;
                selected[(*count)++] = archive->objects[index];
                added = true;
            }
        }
    } while (added);
}

BUSTER_GLOBAL_LOCAL ObjectFile compiler_driver_archive_test_object(Arena* arena, Target target, ObjectSymbol* symbols, u32 count, u8 value)
{
    ObjectFile result = {.target = target, .symbols = symbols, .symbol_count = count, .section_count = OBJECT_SECTION_COUNT};
    result.sections = arena_allocate_zeroed(arena, ObjectSection, result.section_count);
    for (u32 section = 0; section < result.section_count; section += 1)
    {
        result.sections[section].kind = (ObjectSectionKind)section;
        result.sections[section].name = object_section_name_for_kind((ObjectSectionKind)section);
        result.sections[section].alignment = object_section_default_alignment((ObjectSectionKind)section);
    }
    result.sections[OBJECT_SECTION_DATA].data = (ByteSlice){arena_allocate(arena, u8, 1), 1};
    result.sections[OBJECT_SECTION_DATA].data.pointer[0] = value;
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_compare(UnitTestArguments* arguments, ObjectFile root,
                                                                       ObjectArchive* archives, u32 archive_count, bool artifacts)
{
    UnitTestResult result = {0};
    Arena* arena = arguments->arena;
    u32 capacity = 1;
    for (u32 index = 0; index < archive_count; index += 1) capacity += archives[index].object_count;
    ObjectFile* expected = arena_allocate(arena, ObjectFile, capacity);
    ObjectFile* actual = arena_allocate(arena, ObjectFile, capacity);
    expected[0] = actual[0] = root;
    u32 expected_count = 1;
    u32 actual_count = 1;
    CompilerDriverArchiveState state = {.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}})};
    for (u32 index = 0; index < archive_count; index += 1)
    {
        compiler_driver_archive_test_scan(arena, &archives[index], expected, &expected_count);
        compiler_driver_archive_extract(arena, &state, &archives[index], actual, &actual_count);
        BUSTER_TEST(arguments, expected_count == actual_count);
        for (u32 member = 0; member < BUSTER_MIN(expected_count, actual_count); member += 1)
        {
            BUSTER_TEST(arguments, expected[member].symbols == actual[member].symbols);
        }
    }
    if (artifacts)
    {
        LinkObjectResult expected_link = link_objects(arena, expected, expected_count, (LinkOptions){0});
        LinkObjectResult actual_link = link_objects(arena, actual, actual_count, (LinkOptions){0});
        BUSTER_TEST(arguments, expected_link.error == actual_link.error);
        BUSTER_TEST(arguments, string_equal(expected_link.symbol, actual_link.symbol));
        if (expected_link.error == LINK_ERROR_NONE && actual_link.error == LINK_ERROR_NONE)
        {
            ObjectFormat format = object_format_for_target(root.target);
            ObjectArtifact expected_artifact = object_write(arena, &expected_link.object, format);
            ObjectArtifact actual_artifact = object_write(arena, &actual_link.object, format);
            BUSTER_TEST(arguments, expected_artifact.error == OBJECT_ERROR_NONE && actual_artifact.error == OBJECT_ERROR_NONE);
            BUSTER_TEST(arguments, expected_artifact.bytes.length == actual_artifact.bytes.length);
            if (expected_artifact.bytes.length == actual_artifact.bytes.length)
            {
                BUSTER_TEST(arguments, !memcmp(expected_artifact.bytes.pointer, actual_artifact.bytes.pointer, actual_artifact.bytes.length));
            }
        }
    }
    if (state.arena) arena_destroy(state.arena, 1);
    return result;
}

BUSTER_GLOBAL_LOCAL u32 compiler_driver_archive_test_random(u32* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_generated(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    for (u32 format = 0; format < BUSTER_ARRAY_LENGTH(systems); format += 1)
    {
        Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[format]};
        for (u32 seed = 0; seed < 128; seed += 1)
        {
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = arguments->arena;
            String8 names[48];
            for (u32 name = 0; name < BUSTER_ARRAY_LENGTH(names); name += 1)
            {
                names[name] = string_format(arena, S8("archive_symbol_{u32}"), name);
            }
            ObjectFile objects[25];
            u32 random = seed + 1;
            for (u32 object = 0; object < BUSTER_ARRAY_LENGTH(objects); object += 1)
            {
                ObjectSymbol* symbols = arena_allocate_zeroed(arena, ObjectSymbol, 6);
                for (u32 index = 0; index < 6; index += 1)
                {
                    u32 bits = compiler_driver_archive_test_random(&random);
                    symbols[index] = (ObjectSymbol){
                        .name = names[(bits >> 16) % BUSTER_ARRAY_LENGTH(names)],
                        .section = !object || (bits & 2) ? OBJECT_SECTION_UNDEFINED : OBJECT_SECTION_DATA,
                        .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = (bits & 12) != 0, .weak = (bits & 16) != 0,
                    };
                }
                objects[object] = compiler_driver_archive_test_object(arena, target, symbols, 6, (u8)object);
            }
            // Repeated archives require reusing selected state but rebuilding
            // this occurrence's candidates. Earlier archives are never revisited
            // implicitly when a later archive introduces an unresolved name.
            ObjectArchive archives[] = {
                {.objects = objects + 1, .object_count = 12},
                {.objects = objects + 13, .object_count = 12},
                {.objects = objects + 1, .object_count = 12},
            };
            UnitTestResult test = compiler_driver_archive_test_compare(arguments, objects[0], archives, BUSTER_ARRAY_LENGTH(archives), true);
            result.test_count += test.test_count;
            result.succeeded_test_count += test.succeeded_test_count;
            scratch_end(temporary);
        }
    }
    return result;
}

// A queued member can lose one dependency and gain another after its scan
// position. Also cover ordinary objects arriving between archive occurrences.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_transitions(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    ObjectSymbol root_symbols[] = {
        {.name = S8("a"), .global = true, .section = OBJECT_SECTION_UNDEFINED},
        {.name = S8("b"), .global = true, .section = OBJECT_SECTION_UNDEFINED},
    };
    ObjectSymbol first[] = {
        {.name = S8("a"), .global = true, .section = OBJECT_SECTION_DATA},
        {.name = S8("b"), .global = true, .section = OBJECT_SECTION_DATA, .weak = true},
        {.name = S8("d"), .global = true, .section = OBJECT_SECTION_UNDEFINED},
    };
    ObjectSymbol skipped[] = {
        {.name = S8("b"), .global = true, .section = OBJECT_SECTION_DATA, .weak = true},
        {.name = S8("c"), .global = true, .section = OBJECT_SECTION_DATA},
    };
    ObjectSymbol last[] = {
        {.name = S8("d"), .global = true, .section = OBJECT_SECTION_DATA},
        {.name = S8("c"), .global = true, .section = OBJECT_SECTION_UNDEFINED},
    };
    ObjectFile root = compiler_driver_archive_test_object(arguments->arena, target, root_symbols, BUSTER_ARRAY_LENGTH(root_symbols), 0);
    ObjectFile members[] = {
        compiler_driver_archive_test_object(arguments->arena, target, first, BUSTER_ARRAY_LENGTH(first), 1),
        compiler_driver_archive_test_object(arguments->arena, target, skipped, BUSTER_ARRAY_LENGTH(skipped), 2),
        compiler_driver_archive_test_object(arguments->arena, target, last, BUSTER_ARRAY_LENGTH(last), 3),
    };
    ObjectArchive archive = {.objects = members, .object_count = BUSTER_ARRAY_LENGTH(members)};
    UnitTestResult compared = compiler_driver_archive_test_compare(arguments, root, &archive, 1, true);
    result.test_count += compared.test_count;
    result.succeeded_test_count += compared.succeeded_test_count;
    CompilerDriverArchiveState state = {0};
    ObjectFile actual[8] = {root};
    u32 count = 0;
    compiler_driver_archive_extract(arguments->arena, &state, &archive, actual, &count);
    BUSTER_TEST(arguments, count == 0 && !state.arena);
    count = 1;
    ObjectArchive empty = {0};
    compiler_driver_archive_extract(arguments->arena, &state, &empty, actual, &count);
    BUSTER_TEST(arguments, count == 1 && !state.arena);
    state.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}});
    compiler_driver_archive_extract(arguments->arena, &state, &archive, actual, &count);
    BUSTER_TEST(arguments, count == 4 && actual[1].symbols == first && actual[2].symbols == last && actual[3].symbols == skipped);
    if (state.arena) arena_destroy(state.arena, 1);
    state = (CompilerDriverArchiveState){0};
    // An earlier archive cannot anticipate a later direct object's reference.
    ObjectSymbol later_symbol = {.name = S8("c"), .global = true, .section = OBJECT_SECTION_UNDEFINED};
    ObjectFile later = compiler_driver_archive_test_object(arguments->arena, target, &later_symbol, 1, 4);
    ObjectFile no_symbols = compiler_driver_archive_test_object(arguments->arena, target, 0, 0, 0);
    actual[0] = no_symbols;
    count = 1;
    ObjectArchive earlier = {.objects = members + 1, .object_count = 1};
    compiler_driver_archive_extract(arguments->arena, &state, &earlier, actual, &count);
    BUSTER_TEST(arguments, count == 1);
    actual[count++] = later;
    compiler_driver_archive_extract(arguments->arena, &state, &earlier, actual, &count);
    BUSTER_TEST(arguments, count == 3 && actual[2].symbols == skipped);
    if (state.arena) arena_destroy(state.arena, 1);
    return result;
}

#include <buster/tests/compiler/driver/archive_bench.c>

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = compiler_driver_archive_test_generated(arguments);
    UnitTestResult transitions = compiler_driver_archive_test_transitions(arguments);
    result.test_count += transitions.test_count;
    result.succeeded_test_count += transitions.succeeded_test_count;
    UnitTestResult scaling = compiler_driver_archive_test_scaling(arguments);
    result.test_count += scaling.test_count;
    result.succeeded_test_count += scaling.succeeded_test_count;
    // Members 0 and 2 both weakly define shared. Selecting newly needed member
    // 0 immediately after member 1 would change shared from byte 2 to byte 0.
    ObjectSymbol root_symbols[] = {
        {.name = S8("start"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
        {.name = S8("late"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
    };
    ObjectSymbol back_symbols[] = {
        {.name = S8("back"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .global = true},
        {.name = S8("shared"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
        {.name = S8("start"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
    };
    ObjectSymbol start_symbols[] = {
        {.name = S8("start"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .global = true},
        {.name = S8("back"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
    };
    ObjectSymbol late_symbols[] = {
        {.name = S8("late"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .global = true},
        {.name = S8("shared"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .global = true, .weak = true},
    };
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    for (u32 format = 0; format < BUSTER_ARRAY_LENGTH(systems); format += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[format]};
        ObjectFile root = compiler_driver_archive_test_object(arguments->arena, target, root_symbols, BUSTER_ARRAY_LENGTH(root_symbols), 9);
        ObjectFile members[] = {
            compiler_driver_archive_test_object(arguments->arena, target, back_symbols, BUSTER_ARRAY_LENGTH(back_symbols), 0),
            compiler_driver_archive_test_object(arguments->arena, target, start_symbols, BUSTER_ARRAY_LENGTH(start_symbols), 1),
            compiler_driver_archive_test_object(arguments->arena, target, late_symbols, BUSTER_ARRAY_LENGTH(late_symbols), 2),
        };
        ObjectArchive archive = {.objects = members, .object_count = BUSTER_ARRAY_LENGTH(members)};
        UnitTestResult test = compiler_driver_archive_test_compare(arguments, root, &archive, 1, true);
        result.test_count += test.test_count;
        result.succeeded_test_count += test.succeeded_test_count;
        ObjectFile actual[4] = {root};
        u32 count = 1;
        CompilerDriverArchiveState state = {.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}})};
        compiler_driver_archive_extract(arguments->arena, &state, &archive, actual, &count);
        BUSTER_TEST(arguments, count == 4);
        BUSTER_TEST(arguments, actual[1].symbols == start_symbols && actual[2].symbols == late_symbols && actual[3].symbols == back_symbols);
        LinkObjectResult bound = link_objects(arguments->arena, actual, count, (LinkOptions){0});
        BUSTER_TEST(arguments, bound.error == LINK_ERROR_NONE);
        bool shared_is_late = false;
        if (bound.error == LINK_ERROR_NONE)
        {
            for (u32 index = 0; index < bound.object.symbol_count; index += 1)
            {
                ObjectSymbol* symbol = &bound.object.symbols[index];
                if (string_equal(symbol->name, S8("shared")) && symbol->section == OBJECT_SECTION_DATA)
                {
                    ByteSlice data = bound.object.sections[OBJECT_SECTION_DATA].data;
                    shared_is_late = symbol->value < data.length && data.pointer[symbol->value] == 2;
                }
            }
        }
        BUSTER_TEST(arguments, shared_is_late);
        if (state.arena) arena_destroy(state.arena, 1);
        // A weak-only root selects on COFF/Mach-O but never on ELF.
        root.symbol_count = 1;
        root_symbols[0].weak = true;
        test = compiler_driver_archive_test_compare(arguments, root, &archive, 1, true);
        result.test_count += test.test_count;
        result.succeeded_test_count += test.succeeded_test_count;
        state = (CompilerDriverArchiveState){.arena = arena_create((ArenaCreation){.flags = {.no_pool = true}})};
        actual[0] = root;
        count = 1;
        compiler_driver_archive_extract(arguments->arena, &state, &archive, actual, &count);
        BUSTER_TEST(arguments, count == (format ? 3u : 1u));
        if (state.arena) arena_destroy(state.arena, 1);
        root_symbols[0].weak = false;
        scratch_end(temporary);
    }
    return result;
}
