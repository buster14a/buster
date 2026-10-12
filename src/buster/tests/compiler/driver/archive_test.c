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

BUSTER_GLOBAL_LOCAL s32 compiler_driver_archive_test_bound_byte(LinkObjectResult* link, String8 name)
{
    s32 result = -1;
    if (link->error == LINK_ERROR_NONE)
    {
        ByteSlice data = link->object.sections[OBJECT_SECTION_DATA].data;
        for (u32 index = 0; index < link->object.symbol_count && result < 0; index += 1)
        {
            ObjectSymbol* symbol = &link->object.symbols[index];
            if (string_equal(symbol->name, name) && symbol->section == OBJECT_SECTION_DATA && symbol->value < data.length)
            {
                result = data.pointer[symbol->value];
            }
        }
    }
    return result;
}

enum CompilerDriverArchiveContextObjectIndex
{
    COMPILER_DRIVER_ARCHIVE_CONTEXT_MAIN,
    COMPILER_DRIVER_ARCHIVE_CONTEXT_ROOT_BOTH,
    COMPILER_DRIVER_ARCHIVE_CONTEXT_P,
    COMPILER_DRIVER_ARCHIVE_CONTEXT_Q,
    COMPILER_DRIVER_ARCHIVE_CONTEXT_Q_ALT,
    COMPILER_DRIVER_ARCHIVE_CONTEXT_INERT_BASE,
};

typedef struct CompilerDriverArchiveContextCase CompilerDriverArchiveContextCase;
struct CompilerDriverArchiveContextCase
{
    ObjectFile* root;
    ObjectArchive* archives;
    u32 archive_count;
    ObjectFile** expected_selected;
    u32 expected_count;
    LinkError link_error;
    String8 unresolved_symbol;
    s32 q_value;
    s32 p_value;
    bool indexed;
    bool retained_prelude;
};

// Archive boundaries are not neutral partitions (#1353): closure is per
// archive occurrence, so a backward dependency across a split strands its
// provider; forward and initially demanded splits, inert padding (small vs.
// indexed at nine members), and an inert prelude retaining name state must
// preserve selection. An earlier competing provider is not inert.
BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_context_laws(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    OperatingSystem systems[] = {OPERATING_SYSTEM_LINUX, OPERATING_SYSTEM_WINDOWS, OPERATING_SYSTEM_MACOS};
    for (u32 format = 0; format < BUSTER_ARRAY_LENGTH(systems); format += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        Arena* arena = arguments->arena;
        Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = systems[format]};
        ObjectSymbol main_symbols[] = {
            {.name = S8("main"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true},
            {.name = S8("q"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
        };
        ObjectSymbol root_both_symbols[] = {
            {.name = S8("main"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true},
            {.name = S8("p"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
            {.name = S8("q"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
        };
        ObjectSymbol p_symbols[] = {
            {.name = S8("p"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true},
        };
        ObjectSymbol q_symbols[] = {
            {.name = S8("q"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true},
            {.name = S8("p"), .section = OBJECT_SECTION_UNDEFINED, .global = true},
        };
        ObjectSymbol q_alt_symbols[] = {
            {.name = S8("q"), .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true},
        };
        ObjectSymbol inert_symbols[9];
        ObjectFile objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_INERT_BASE + 9];
        objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_MAIN] = compiler_driver_archive_test_object(arena, target, main_symbols, BUSTER_ARRAY_LENGTH(main_symbols), 0);
        objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_ROOT_BOTH] = compiler_driver_archive_test_object(arena, target, root_both_symbols, BUSTER_ARRAY_LENGTH(root_both_symbols), 0);
        objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P] = compiler_driver_archive_test_object(arena, target, p_symbols, BUSTER_ARRAY_LENGTH(p_symbols), 42);
        objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q] = compiler_driver_archive_test_object(arena, target, q_symbols, BUSTER_ARRAY_LENGTH(q_symbols), 42);
        objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q_ALT] = compiler_driver_archive_test_object(arena, target, q_alt_symbols, BUSTER_ARRAY_LENGTH(q_alt_symbols), 41);
        ObjectFile inert_objects[9];
        for (u32 inert = 0; inert < BUSTER_ARRAY_LENGTH(inert_objects); inert += 1)
        {
            inert_symbols[inert] = (ObjectSymbol){
                .name = string_format(arena, S8("inert_{u32}"), inert),
                .section = OBJECT_SECTION_DATA, .kind = OBJECT_SYMBOL_DATA, .size = 1, .global = true,
            };
            inert_objects[inert] = compiler_driver_archive_test_object(arena, target, &inert_symbols[inert], 1, (u8)inert);
            objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_INERT_BASE + inert] = inert_objects[inert];
        }

        ObjectFile combined_pq_members[] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P], objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q]};
        ObjectFile combined_qp_members[] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q], objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P]};
        ObjectFile only_p_members[] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P]};
        ObjectFile only_q_members[] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q]};
        ObjectFile padded8_members[8] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P], objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q]};
        ObjectFile padded9_members[9] = {objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P], objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q]};
        ObjectFile unselected9_members[9];
        ObjectFile conflicting_members[] = {
            objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q_ALT], objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P],
            objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q],
        };
        for (u32 inert = 0; inert < 6; inert += 1) padded8_members[2 + inert] = inert_objects[inert];
        for (u32 inert = 0; inert < 7; inert += 1) padded9_members[2 + inert] = inert_objects[inert];
        for (u32 inert = 0; inert < BUSTER_ARRAY_LENGTH(unselected9_members); inert += 1)
        {
            unselected9_members[inert] = inert_objects[inert];
        }

        ObjectArchive archives_baseline_reverse[] = {{.objects = combined_pq_members, .object_count = BUSTER_ARRAY_LENGTH(combined_pq_members)}};
        ObjectArchive archives_invalid_split_backward[] = {
            {.objects = only_p_members, .object_count = BUSTER_ARRAY_LENGTH(only_p_members)},
            {.objects = only_q_members, .object_count = BUSTER_ARRAY_LENGTH(only_q_members)},
        };
        ObjectArchive archives_baseline_forward[] = {{.objects = combined_qp_members, .object_count = BUSTER_ARRAY_LENGTH(combined_qp_members)}};
        ObjectArchive archives_valid_split_forward[] = {
            {.objects = only_q_members, .object_count = BUSTER_ARRAY_LENGTH(only_q_members)},
            {.objects = only_p_members, .object_count = BUSTER_ARRAY_LENGTH(only_p_members)},
        };
        ObjectArchive archives_initially_demanded_baseline[] = {
            {.objects = combined_pq_members, .object_count = BUSTER_ARRAY_LENGTH(combined_pq_members)},
        };
        ObjectArchive archives_initially_demanded_split[] = {
            {.objects = only_p_members, .object_count = BUSTER_ARRAY_LENGTH(only_p_members)},
            {.objects = only_q_members, .object_count = BUSTER_ARRAY_LENGTH(only_q_members)},
        };
        ObjectArchive archives_explicit_repeat[] = {
            {.objects = only_p_members, .object_count = BUSTER_ARRAY_LENGTH(only_p_members)},
            {.objects = only_q_members, .object_count = BUSTER_ARRAY_LENGTH(only_q_members)},
            {.objects = only_p_members, .object_count = BUSTER_ARRAY_LENGTH(only_p_members)},
        };
        ObjectArchive archives_padding8[] = {{.objects = padded8_members, .object_count = BUSTER_ARRAY_LENGTH(padded8_members)}};
        ObjectArchive archives_padding9[] = {{.objects = padded9_members, .object_count = BUSTER_ARRAY_LENGTH(padded9_members)}};
        ObjectArchive archives_inert_prelude[] = {
            {.objects = unselected9_members, .object_count = BUSTER_ARRAY_LENGTH(unselected9_members)},
            {.objects = combined_pq_members, .object_count = BUSTER_ARRAY_LENGTH(combined_pq_members)},
        };
        ObjectArchive archives_invalid_padding_conflict[] = {
            {.objects = conflicting_members, .object_count = BUSTER_ARRAY_LENGTH(conflicting_members)},
        };
        ObjectFile* selected_qp[] = {
            &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q], &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P],
        };
        ObjectFile* selected_pq[] = {
            &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_P], &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q],
        };
        ObjectFile* selected_q[] = {&objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q]};
        ObjectFile* selected_q_alt[] = {&objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_Q_ALT]};
        ObjectFile* main = &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_MAIN];
        ObjectFile* root_both = &objects[COMPILER_DRIVER_ARCHIVE_CONTEXT_ROOT_BOTH];
        CompilerDriverArchiveContextCase cases[] = {
            // baseline-reverse
            {.root = main, .archives = archives_baseline_reverse,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_baseline_reverse), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // invalid-split-backward
            {.root = main, .archives = archives_invalid_split_backward,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_invalid_split_backward), .expected_selected = selected_q, .expected_count = 1,
             .link_error = LINK_ERROR_UNRESOLVED_SYMBOL, .unresolved_symbol = S8("p"), .q_value = 42, .p_value = -1},
            // baseline-forward
            {.root = main, .archives = archives_baseline_forward,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_baseline_forward), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // valid-split-forward
            {.root = main, .archives = archives_valid_split_forward,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_valid_split_forward), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // initially-demanded-baseline
            {.root = root_both, .archives = archives_initially_demanded_baseline,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_initially_demanded_baseline), .expected_selected = selected_pq,
             .expected_count = 2, .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // initially-demanded-split
            {.root = root_both, .archives = archives_initially_demanded_split,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_initially_demanded_split), .expected_selected = selected_pq,
             .expected_count = 2, .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // explicit-repeat
            {.root = main, .archives = archives_explicit_repeat,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_explicit_repeat), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // padding-eight
            {.root = main, .archives = archives_padding8,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_padding8), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42},
            // padding-nine
            {.root = main, .archives = archives_padding9,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_padding9), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42, .indexed = true},
            // inert-prelude
            {.root = main, .archives = archives_inert_prelude,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_inert_prelude), .expected_selected = selected_qp, .expected_count = 2,
             .link_error = LINK_ERROR_NONE, .q_value = 42, .p_value = 42, .indexed = true, .retained_prelude = true},
            // invalid-padding-conflict
            {.root = main, .archives = archives_invalid_padding_conflict,
             .archive_count = BUSTER_ARRAY_LENGTH(archives_invalid_padding_conflict), .expected_selected = selected_q_alt,
             .expected_count = 1, .link_error = LINK_ERROR_NONE, .q_value = 41, .p_value = -1},
        };

        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
        {
            CompilerDriverArchiveContextCase* test_case = &cases[index];
            UnitTestResult compared = compiler_driver_archive_test_compare(arguments, *test_case->root, test_case->archives,
                                                                            test_case->archive_count, true);
            result.test_count += compared.test_count;
            result.succeeded_test_count += compared.succeeded_test_count;

            CompilerDriverArchiveState state = {0};
            ObjectFile actual[12] = {*test_case->root};
            u32 count = 1;
            for (u32 archive_index = 0; archive_index < test_case->archive_count; archive_index += 1)
            {
                compiler_driver_archive_extract(arena, &state, &test_case->archives[archive_index], actual, &count);
                if (test_case->retained_prelude && archive_index == 0)
                {
                    BUSTER_TEST(arguments, count == 1);
                    BUSTER_TEST(arguments, state.arena != 0);
                }
            }
            bool selection_count_matches = count == 1 + test_case->expected_count;
            if (BUSTER_REQUIRE(arguments, selection_count_matches))
            {
                for (u32 selected = 0; selected < test_case->expected_count; selected += 1)
                {
                    BUSTER_TEST(arguments, actual[1 + selected].symbols == test_case->expected_selected[selected]->symbols);
                }
            }
            if (test_case->indexed) BUSTER_TEST(arguments, state.arena != 0);
            else BUSTER_TEST(arguments, !state.arena);

            LinkObjectResult link = link_objects(arena, actual, count, (LinkOptions){0});
            BUSTER_TEST(arguments, link.error == test_case->link_error);
            if (test_case->link_error == LINK_ERROR_UNRESOLVED_SYMBOL)
            {
                BUSTER_TEST(arguments, string_equal(link.symbol, test_case->unresolved_symbol));
            }
            if (link.error == LINK_ERROR_NONE)
            {
                if (test_case->q_value >= 0)
                {
                    BUSTER_TEST(arguments, compiler_driver_archive_test_bound_byte(&link, S8("q")) == test_case->q_value);
                }
                if (test_case->p_value >= 0)
                {
                    BUSTER_TEST(arguments, compiler_driver_archive_test_bound_byte(&link, S8("p")) == test_case->p_value);
                }
            }
            if (state.arena) arena_destroy(state.arena, 1);
        }
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_context_driver_replay(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64 && !BUSTER_ANDROID && !BUSTER_IOS
    TemporalArena temporary = arena_begin_temporal(arguments->arena);
    Arena* arena = arguments->arena;
    enum
    {
        COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN,
        COMPILER_DRIVER_ARCHIVE_REPLAY_ROOT_BOTH,
        COMPILER_DRIVER_ARCHIVE_REPLAY_P,
        COMPILER_DRIVER_ARCHIVE_REPLAY_Q,
        COMPILER_DRIVER_ARCHIVE_REPLAY_Q_ALT,
        COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE,
        COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT = COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE + 9,
    };
    typedef struct CompilerDriverArchiveReplaySource CompilerDriverArchiveReplaySource;
    struct CompilerDriverArchiveReplaySource
    {
        String8 name;
        String8 contents;
    };
    CompilerDriverArchiveReplaySource sources[COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT] = {
        [COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN] = {
            .name = S8("main"),
            .contents = S8("int q(void);\nint main(void) { return q() != 42; }\n"),
        },
        [COMPILER_DRIVER_ARCHIVE_REPLAY_ROOT_BOTH] = {
            .name = S8("root_both"),
            .contents = S8("int p(void);\nint q(void);\nint main(void) { return p() != 42 || q() != 42; }\n"),
        },
        [COMPILER_DRIVER_ARCHIVE_REPLAY_P] = {
            .name = S8("p"),
            .contents = S8("int p(void) { return 42; }\n"),
        },
        [COMPILER_DRIVER_ARCHIVE_REPLAY_Q] = {
            .name = S8("q"),
            .contents = S8("int p(void);\nint q(void) { return p(); }\n"),
        },
        [COMPILER_DRIVER_ARCHIVE_REPLAY_Q_ALT] = {
            .name = S8("q_alt"),
            .contents = S8("int q(void) { return 41; }\n"),
        },
    };
    for (u32 inert = 0; inert < 9; inert += 1)
    {
        u32 source_index = COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE + inert;
        sources[source_index].name = string_format(arena, S8("inert_{u32}"), inert);
        sources[source_index].contents =
            string_format(arena, S8("int {S8}(void) {{ return {u32}; }}\n"), sources[source_index].name, inert);
    }

    String8 directory = buster_test_temporary_unique_path(arena, S8("buster-archive-context-driver-replay"), S8(""));
    if (BUSTER_REQUIRE(arguments, os_make_directory_attempt(directory)))
    {
        String8 source_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT];
        String8 object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT];
        ByteSlice object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT] = {0};
        bool sources_ready = true;
        for (u32 source_index = 0; source_index < COMPILER_DRIVER_ARCHIVE_REPLAY_SOURCE_COUNT; source_index += 1)
        {
            source_paths[source_index] = string_format_z(arena, S8("{S8}/{S8}.c"), directory, sources[source_index].name);
            object_paths[source_index] = string_format_z(arena, S8("{S8}/{S8}.o"), directory, sources[source_index].name);
            bool source_written = file_write(source_paths[source_index], BUSTER_SLICE_TO_BYTE_SLICE(sources[source_index].contents));
            BUSTER_TEST_RAW(arguments, source_written, source_paths[source_index]);
            if (source_written)
            {
                String8 compile_arguments[] = {
                    S8("-c"), S8("-g0"), source_paths[source_index], S8("-o"), object_paths[source_index],
                };
                CompilerDriverResult compiled = compiler_driver_execute_invocation(
                    arena, compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(compile_arguments)));
                if (BUSTER_REQUIRE(arguments, compiled.error == COMPILER_DRIVER_ERROR_NONE))
                {
                    FileReadResult object_read = file_read_checked(arena, object_paths[source_index], (FileReadOptions){0});
                    bool object_read_ok = object_read.bytes.pointer && object_read.bytes.length != 0;
                    BUSTER_TEST(arguments, object_read_ok);
                    if (object_read_ok)
                    {
                        object_bytes[source_index] = object_read.bytes;
                    }
                    else
                    {
                        sources_ready = false;
                    }
                }
                else
                {
                    sources_ready = false;
                }
            }
            else
            {
                sources_ready = false;
            }
        }

        if (BUSTER_REQUIRE(arguments, sources_ready))
        {
            enum
            {
                COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ,
                COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_QP,
                COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P,
                COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q,
                COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED8,
                COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED9,
                COMPILER_DRIVER_ARCHIVE_REPLAY_UNSELECTED9,
                COMPILER_DRIVER_ARCHIVE_REPLAY_CONFLICTING,
                COMPILER_DRIVER_ARCHIVE_REPLAY_ARCHIVE_COUNT,
            };
            ByteSlice combined_pq_members[] = {
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P], object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q],
            };
            String8 combined_pq_names[] = {S8("p.o"), S8("q.o")};
            ByteSlice combined_qp_members[] = {
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q], object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P],
            };
            String8 combined_qp_names[] = {S8("q.o"), S8("p.o")};
            ByteSlice only_p_members[] = {object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P]};
            String8 only_p_names[] = {S8("p.o")};
            ByteSlice only_q_members[] = {object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q]};
            String8 only_q_names[] = {S8("q.o")};
            ByteSlice padded8_members[8] = {
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P], object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q],
            };
            String8 padded8_names[8] = {S8("p.o"), S8("q.o")};
            ByteSlice padded9_members[9] = {
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P], object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q],
            };
            String8 padded9_names[9] = {S8("p.o"), S8("q.o")};
            ByteSlice unselected9_members[9];
            String8 unselected9_names[9];
            ByteSlice conflicting_members[] = {
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q_ALT],
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_P],
                object_bytes[COMPILER_DRIVER_ARCHIVE_REPLAY_Q],
            };
            String8 conflicting_names[] = {S8("q_alt.o"), S8("p.o"), S8("q.o")};
            for (u32 inert = 0; inert < 6; inert += 1)
            {
                u32 source_index = COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE + inert;
                padded8_members[2 + inert] = object_bytes[source_index];
                padded8_names[2 + inert] = string_format(arena, S8("{S8}.o"), sources[source_index].name);
            }
            for (u32 inert = 0; inert < 7; inert += 1)
            {
                u32 source_index = COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE + inert;
                padded9_members[2 + inert] = object_bytes[source_index];
                padded9_names[2 + inert] = string_format(arena, S8("{S8}.o"), sources[source_index].name);
            }
            for (u32 inert = 0; inert < BUSTER_ARRAY_LENGTH(unselected9_members); inert += 1)
            {
                u32 source_index = COMPILER_DRIVER_ARCHIVE_REPLAY_INERT_BASE + inert;
                unselected9_members[inert] = object_bytes[source_index];
                unselected9_names[inert] = string_format(arena, S8("{S8}.o"), sources[source_index].name);
            }

            typedef struct CompilerDriverArchiveReplayArchiveBuild CompilerDriverArchiveReplayArchiveBuild;
            struct CompilerDriverArchiveReplayArchiveBuild
            {
                ByteSlice* members;
                String8* member_names;
                u32 member_count;
            };
            CompilerDriverArchiveReplayArchiveBuild archive_builds[COMPILER_DRIVER_ARCHIVE_REPLAY_ARCHIVE_COUNT] = {
                [COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ] = {
                    .members = combined_pq_members, .member_names = combined_pq_names, .member_count = BUSTER_ARRAY_LENGTH(combined_pq_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_QP] = {
                    .members = combined_qp_members, .member_names = combined_qp_names, .member_count = BUSTER_ARRAY_LENGTH(combined_qp_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P] = {
                    .members = only_p_members, .member_names = only_p_names, .member_count = BUSTER_ARRAY_LENGTH(only_p_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q] = {
                    .members = only_q_members, .member_names = only_q_names, .member_count = BUSTER_ARRAY_LENGTH(only_q_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED8] = {
                    .members = padded8_members, .member_names = padded8_names, .member_count = BUSTER_ARRAY_LENGTH(padded8_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED9] = {
                    .members = padded9_members, .member_names = padded9_names, .member_count = BUSTER_ARRAY_LENGTH(padded9_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_UNSELECTED9] = {
                    .members = unselected9_members, .member_names = unselected9_names, .member_count = BUSTER_ARRAY_LENGTH(unselected9_members),
                },
                [COMPILER_DRIVER_ARCHIVE_REPLAY_CONFLICTING] = {
                    .members = conflicting_members, .member_names = conflicting_names, .member_count = BUSTER_ARRAY_LENGTH(conflicting_members),
                },
            };
            String8 archive_bases[COMPILER_DRIVER_ARCHIVE_REPLAY_ARCHIVE_COUNT] = {
                [COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ] = S8("combined_pq"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_QP] = S8("combined_qp"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P] = S8("only_p"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q] = S8("only_q"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED8] = S8("padded8"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED9] = S8("padded9"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_UNSELECTED9] = S8("unselected9"),
                [COMPILER_DRIVER_ARCHIVE_REPLAY_CONFLICTING] = S8("conflicting"),
            };
            String8 archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ARCHIVE_COUNT];
            bool archives_ready = true;
            for (u32 archive_index = 0; archive_index < COMPILER_DRIVER_ARCHIVE_REPLAY_ARCHIVE_COUNT; archive_index += 1)
            {
                archive_paths[archive_index] =
                    string_format_z(arena, S8("{S8}/{S8}.a"), directory, archive_bases[archive_index]);
                ByteSlice archive = compiler_driver_test_archive(
                    arena, archive_builds[archive_index].members, archive_builds[archive_index].member_names,
                    archive_builds[archive_index].member_count);
                bool archive_written = file_write(archive_paths[archive_index], archive);
                BUSTER_TEST_RAW(arguments, archive_written, archive_paths[archive_index]);
                if (!archive_written)
                {
                    archives_ready = false;
                }
            }

            if (BUSTER_REQUIRE(arguments, archives_ready))
            {
                String8 baseline_reverse_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ],
                };
                String8 invalid_split_backward_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q],
                };
                String8 baseline_forward_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_QP],
                };
                String8 valid_split_forward_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P],
                };
                String8 initially_demanded_baseline_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ROOT_BOTH],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ],
                };
                String8 initially_demanded_split_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ROOT_BOTH],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q],
                };
                String8 explicit_repeat_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_Q],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_ONLY_P],
                };
                String8 padding8_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED8],
                };
                String8 padding9_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_PADDED9],
                };
                String8 inert_prelude_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_UNSELECTED9],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_COMBINED_PQ],
                };
                String8 conflicting_inputs[] = {
                    object_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_MAIN],
                    archive_paths[COMPILER_DRIVER_ARCHIVE_REPLAY_CONFLICTING],
                };
                typedef struct CompilerDriverArchiveDriverReplayCase CompilerDriverArchiveDriverReplayCase;
                struct CompilerDriverArchiveDriverReplayCase
                {
                    String8 name;
                    String8* inputs;
                    u32 input_count;
                    bool expect_link_success;
                    bool expect_program_success;
                };
                CompilerDriverArchiveDriverReplayCase cases[] = {
                    {
                        .name = S8("baseline-reverse"), .inputs = baseline_reverse_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(baseline_reverse_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("invalid-split-backward"), .inputs = invalid_split_backward_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(invalid_split_backward_inputs),
                        .expect_link_success = false, .expect_program_success = false,
                    },
                    {
                        .name = S8("baseline-forward"), .inputs = baseline_forward_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(baseline_forward_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("valid-split-forward"), .inputs = valid_split_forward_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(valid_split_forward_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("initially-demanded-baseline"), .inputs = initially_demanded_baseline_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(initially_demanded_baseline_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("initially-demanded-split"), .inputs = initially_demanded_split_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(initially_demanded_split_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("explicit-repeat"), .inputs = explicit_repeat_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(explicit_repeat_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("padding-eight"), .inputs = padding8_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(padding8_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("padding-nine"), .inputs = padding9_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(padding9_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("inert-prelude"), .inputs = inert_prelude_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(inert_prelude_inputs),
                        .expect_link_success = true, .expect_program_success = true,
                    },
                    {
                        .name = S8("invalid-padding-conflict"), .inputs = conflicting_inputs,
                        .input_count = BUSTER_ARRAY_LENGTH(conflicting_inputs),
                        .expect_link_success = true, .expect_program_success = false,
                    },
                };

                for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(cases); case_index += 1)
                {
                    CompilerDriverArchiveDriverReplayCase* test_case = &cases[case_index];
                    String8 output_path =
                        buster_test_temporary_unique_path(arena, S8("buster-archive-context-driver-output"), S8(""));
                    String8 link_arguments[6];
                    u32 link_argument_count = 0;
                    for (u32 input_index = 0; input_index < test_case->input_count; input_index += 1)
                    {
                        link_arguments[link_argument_count++] = test_case->inputs[input_index];
                    }
                    link_arguments[link_argument_count++] = S8("-o");
                    link_arguments[link_argument_count++] = output_path;
                    CompilerDriverResult linked = compiler_driver_execute_invocation(
                        arena, compiler_driver_parse_arguments(arena, (SliceString8){link_arguments, link_argument_count}));
                    if (test_case->expect_link_success)
                    {
                        String8 context = string_format(arena, S8("{S8}: {S8}"), test_case->name, linked.diagnostic);
                        BUSTER_TEST_RAW(arguments, linked.error == COMPILER_DRIVER_ERROR_NONE, context);
                        if (linked.error == COMPILER_DRIVER_ERROR_NONE)
                        {
                            String8 run_arguments[] = {output_path};
                            ProcessSpawnOptions capture = {
                                .capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                .use_process_environment = true,
                                .search_path = true,
                            };
                            ProcessSpawnResult spawned = os_process_spawn(
                                (SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0}, capture);
                            if (BUSTER_REQUIRE(arguments, spawned.handle != 0))
                            {
                                ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
                                if (test_case->expect_program_success)
                                {
                                    BUSTER_TEST_RAW(arguments,
                                                    !waited.timed_out && waited.result == PROCESS_RESULT_SUCCESS,
                                                    BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]));
                                    BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_OUTPUT].length == 0);
                                    BUSTER_TEST(arguments, waited.streams[STANDARD_STREAM_ERROR].length == 0);
                                }
                                else
                                {
                                    BUSTER_TEST(arguments, !waited.timed_out);
                                    BUSTER_TEST(arguments, waited.result != PROCESS_RESULT_SUCCESS);
                                }
                            }
                        }
                    }
                    else
                    {
                        BUSTER_TEST_RAW(arguments, linked.error == COMPILER_DRIVER_ERROR_LINK, test_case->name);
                        BUSTER_TEST(arguments, string_ends_with_sequence(linked.diagnostic, S8("unresolved symbol: p")));
                        BUSTER_TEST(arguments, linked.diagnostic_count == 1);
                        if (linked.diagnostic_count == 1)
                        {
                            BUSTER_STRING_TEST(arguments, linked.diagnostics[0].code, S8("link.unresolved-symbol"));
                        }
                        FileReadResult failed_output = file_read_checked(arena, output_path, (FileReadOptions){0});
                        BUSTER_TEST(arguments, failed_output.status == OS_FILE_READ_ERROR);
                    }
                }
            }
        }
    }
    scratch_end(temporary);
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}

#include <buster/tests/compiler/driver/archive_bench.c>
#include <buster/tests/compiler/driver/archive_lazy_test.c>

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = compiler_driver_archive_test_generated(arguments);
    UnitTestResult lazy = compiler_driver_archive_test_lazy(arguments);
    result.test_count += lazy.test_count;
    result.succeeded_test_count += lazy.succeeded_test_count;
    UnitTestResult unused_payload = compiler_driver_archive_test_unused_size_relocation(arguments);
    result.test_count += unused_payload.test_count;
    result.succeeded_test_count += unused_payload.succeeded_test_count;
    UnitTestResult unreadable = compiler_driver_archive_test_unindexed_unreadable_member(arguments);
    result.test_count += unreadable.test_count;
    result.succeeded_test_count += unreadable.succeeded_test_count;
    UnitTestResult transitions = compiler_driver_archive_test_transitions(arguments);
    result.test_count += transitions.test_count;
    result.succeeded_test_count += transitions.succeeded_test_count;
    UnitTestResult context_laws = compiler_driver_archive_test_context_laws(arguments);
    result.test_count += context_laws.test_count;
    result.succeeded_test_count += context_laws.succeeded_test_count;
    UnitTestResult context_driver_replay = compiler_driver_archive_test_context_driver_replay(arguments);
    result.test_count += context_driver_replay.test_count;
    result.succeeded_test_count += context_driver_replay.succeeded_test_count;
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
