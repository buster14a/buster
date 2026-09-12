#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/time.h>
#if defined(INITIALIZER_REGRESSION)
#include <buster/tests/compiler/link/link_test.h>
#endif
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/hash.c>
#include <buster/lib/time.c>
#include <buster/lib/compiler/object/object.c>
#include <buster/lib/compiler/link/link.c>
#include <stdio.h>
#include <time.h>

#if defined(INITIALIZER_REGRESSION)
#include <buster/tests/compiler/link/link_test.c>
void buster_test_error_arguments(UnitTestArguments* arguments, u32 line, String8 function, String8 file_path, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    BUSTER_UNUSED(format);
    fprintf(stderr, "%.*s:%u: %.*s failed\n", (int)file_path.length, file_path.pointer, line, (int)function.length, function.pointer);
}
#endif

int main(int argc, char** argv)
{
    u32 count = argc > 1 ? (u32)strtoul(argv[1], 0, 10) : 65536;
    u32 shape = argc > 2 ? (u32)strtoul(argv[2], 0, 10) : 0;
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){0});
#if defined(INITIALIZER_REGRESSION)
    UnitTestArguments arguments = {.arena = arena};
    UnitTestResult tests = link_test_initializer_order(&arguments);
    printf("initializer assertions: %llu / %llu\n", (unsigned long long)tests.succeeded_test_count, (unsigned long long)tests.test_count);
    BUSTER_UNUSED(count);
    BUSTER_UNUSED(shape);
    int status = tests.succeeded_test_count != tests.test_count;
#else
    ObjectSection sections[OBJECT_SECTION_COUNT] = {0};
    for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
    {
        sections[kind].kind = (ObjectSectionKind)kind;
        sections[kind].alignment = object_section_default_alignment((ObjectSectionKind)kind);
    }
    u32* priorities = arena_allocate(arena, u32, count);
    u64* entries = arena_allocate(arena, u64, count);
    ObjectRelocation* relocations = arena_allocate(arena, ObjectRelocation, count);
    ObjectSymbol* symbols = arena_allocate(arena, ObjectSymbol, count);
    for (u32 entry = 0; entry < count; entry += 1)
    {
        priorities[entry] = shape == 1 ? entry : shape == 2 ? UINT32_MAX : count - entry;
        entries[entry] = entry;
        symbols[entry] = (ObjectSymbol){.name = S8("entry"), .value = (u64)entry * 8,
            .section = OBJECT_SECTION_INIT_ARRAY, .kind = OBJECT_SYMBOL_DATA};
        relocations[entry] = (ObjectRelocation){.offset = (u64)entry * 8, .symbol = entry,
            .section = OBJECT_SECTION_INIT_ARRAY, .kind = OBJECT_RELOCATION_ABSOLUTE64};
    }
    sections[OBJECT_SECTION_INIT_ARRAY].data = (ByteSlice){.pointer = (u8*)entries, .length = (u64)count * 8};
    ObjectFile object = {.sections = sections, .section_count = OBJECT_SECTION_COUNT,
        .symbols = symbols, .symbol_count = count, .relocations = relocations, .relocation_count = count,
        .initializer_priorities = {priorities, 0},
        .target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX}};
    u64 position = arena->position;
    Arena* sort_scratch = thread_context_get_scratch(&arena, 1);
    u64 scratch_position = sort_scratch->position;
    int status = 0;
    for (u32 repeat = 0; repeat < 8; repeat += 1)
    {
        clock_t cpu_start = clock();
        TimeDataType start = timestamp_take();
        LinkObjectResult linked = link_objects(arena, &object, 1, (LinkOptions){0});
        u64 ns = timestamp_ns_between(start, timestamp_take());
        u64 cpu_ns = (u64)(clock() - cpu_start) * (UINT64_C(1000000000) / CLOCKS_PER_SEC);
        u64 checksum = 0;
        status |= linked.error != LINK_ERROR_NONE;
        if (!status)
        {
            for (u32 entry = 0; entry < count; entry += 1)
            {
                u32 original = shape ? entry : count - entry - 1;
                u64 data = 0;
                memcpy(&data, linked.object.sections[OBJECT_SECTION_INIT_ARRAY].data.pointer + (u64)entry * 8, 8);
                status |= data != original || linked.object.initializer_priorities[0][entry] != priorities[original];
                status |= linked.object.symbols[original].value != (u64)entry * 8;
                status |= linked.object.relocations[original].offset != (u64)entry * 8;
                checksum += data + linked.object.symbols[original].value + linked.object.relocations[original].offset;
            }
        }
        printf("%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%d\n", count, shape, repeat,
               (unsigned long long)ns, (unsigned long long)cpu_ns, (unsigned long long)(arena->position - position),
               (unsigned long long)(arena_dirty_position(sort_scratch) - scratch_position),
               (unsigned long long)checksum, status);
        arena_set_position(arena, position);
    }
#endif
    arena_destroy(arena, 1);
    thread_context_release(context);
    return status;
}
