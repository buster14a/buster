// Included by archive_test.c. Opt-in timing compares the independent scan
// oracle and production extraction on identical ObjectFiles, including setup
// and destruction. No wall-time threshold is a test gate.
#include <buster/lib/time.h>

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_archive_test_scaling(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    bool benchmark = os_get_environment_variable(S8("BUSTER_ARCHIVE_BENCH")).length != 0;
    u32 sizes[] = {1, 8, 32, 128, 512};
    for (u32 shape = 0; shape < 4; shape += 1)
    {
        for (u32 size_index = 0; size_index < BUSTER_ARRAY_LENGTH(sizes); size_index += 1)
        {
            u32 count = sizes[size_index];
            if (!benchmark && count > 32) continue;
            TemporalArena temporary = arena_begin_temporal(arguments->arena);
            Arena* arena = arguments->arena;
            Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
            String8* names = arena_allocate(arena, String8, count);
            for (u32 index = 0; index < count; index += 1) names[index] = string_format(arena, S8("chain_{u32}"), index);
            ObjectFile* members = arena_allocate_zeroed(arena, ObjectFile, count);
            for (u32 index = 0; index < count; index += 1)
            {
                u32 logical = shape == 0 ? count - index - 1 : index;
                ObjectSymbol* symbols = arena_allocate_zeroed(arena, ObjectSymbol, 2);
                symbols[0] = (ObjectSymbol){.name = names[logical], .section = OBJECT_SECTION_DATA, .global = true};
                symbols[1] = (ObjectSymbol){.name = names[(logical + 1) % count], .section = OBJECT_SECTION_UNDEFINED, .global = true};
                members[index] = compiler_driver_archive_test_object(arena, target, symbols,
                    shape >= 2 || logical + 1 == count ? 1 : 2, (u8)index);
            }
            u32 root_count = shape == 3 ? count : 1;
            ObjectSymbol* root_symbols = arena_allocate_zeroed(arena, ObjectSymbol, root_count);
            for (u32 index = 0; index < root_count; index += 1)
            {
                root_symbols[index] = (ObjectSymbol){
                    .name = index ? string_format(arena, S8("missing_{u32}"), index) : names[0],
                    .section = OBJECT_SECTION_UNDEFINED, .global = true,
                };
            }
            ObjectFile root = compiler_driver_archive_test_object(arena, target, root_symbols, root_count, 0);
            ObjectArchive archive = {.objects = members, .object_count = count};
            UnitTestResult compared = compiler_driver_archive_test_compare(arguments, root, &archive, 1, true);
            result.test_count += compared.test_count;
            result.succeeded_test_count += compared.succeeded_test_count;
            if (benchmark)
            {
                ObjectFile* expected = arena_allocate(arena, ObjectFile, (u64)count + 1);
                ObjectFile* actual = arena_allocate(arena, ObjectFile, (u64)count + 1);
                for (u32 repeat = 0; repeat < 5; repeat += 1)
                {
                    u64 times[2] = {0};
                    u64 retained = 0;
                    u32 selected[2] = {1, 1};
                    for (u32 step = 0; step < 2; step += 1)
                    {
                        u32 method = (repeat + step) & 1;
                        TemporalArena timed = arena_begin_temporal(arena);
                        TimeDataType start = timestamp_take();
                        if (method)
                        {
                            CompilerDriverArchiveState state = {0};
                            actual[0] = root;
                            compiler_driver_archive_extract(arena, &state, &archive, actual, &selected[1]);
                            retained = state.arena ? state.arena->position - arena_minimum_position : 0;
                            if (state.arena) arena_destroy(state.arena, 1);
                        }
                        else
                        {
                            expected[0] = root;
                            compiler_driver_archive_test_scan(arena, &archive, expected, &selected[0]);
                        }
                        scratch_end(timed);
                        times[method] = timestamp_ns_between(start, timestamp_take());
                    }
                    BUSTER_TEST(arguments, selected[0] == selected[1]);
                    arguments->show(arguments,
                        S8("ARCHIVE_BENCH shape={u32} members={u32} repeat={u32} selected={u32} scan_ns={u64} indexed_ns={u64} state_bytes={u64}\n"),
                        shape, count, repeat, selected[1] - 1, times[0], times[1], retained);
                }
            }
            scratch_end(temporary);
        }
    }
    return result;
}
