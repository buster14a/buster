// Standalone diagnostic fixture; build identically in baseline/candidate trees.
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/system_headers.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>

OsState os_state;
BUSTER_GLOBAL_LOCAL ProgramState bench_program;
ProgramState* program_state = &bench_program;
BUSTER_GLOBAL_LOCAL u64 now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * UINT64_C(1000000000) + (u64)ts.tv_nsec;
}
int main(void)
{
    ThreadContext* context = thread_context_allocate();
    thread_context_select(context);
    Arena* arena = arena_create((ArenaCreation){.reserved_size = 1024 * 1024, .flags = {.no_pool = true}});
    u64 mark = arena->position;
    u64 begin = now_ns();
    buster_x86_metadata_prewarm();
    u64 prewarm_ns = now_ns() - begin;
    Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
    begin = now_ns();
    ObjectFile cold = link_elf_libc_runtime_object(arena, target);
    u64 cold_ns = now_ns() - begin;
    assert(!cold.error && cold.sections[OBJECT_SECTION_TEXT].data.length == 18);
    u64 checksum = 0;
    for (u32 index = 0; index < 1000; ++index)
    {
        arena_set_position(arena, mark);
        ObjectFile object = link_elf_libc_runtime_object(arena, target);
        checksum += object.sections[OBJECT_SECTION_TEXT].data.pointer[0];
    }
    u32 const count = 250000;
    begin = now_ns();
    for (u32 index = 0; index < count; ++index)
    {
        arena_set_position(arena, mark);
        ObjectFile object = link_elf_libc_runtime_object(arena, target);
        checksum += object.sections[OBJECT_SECTION_TEXT].data.pointer[0] + object.relocations[1].offset;
    }
    u64 hot_ns = now_ns() - begin;
    printf("{\"prewarm_ns\":%llu,\"cold_ns\":%llu,\"hot_ns\":%llu,\"count\":%u,\"checksum\":%llu}\n", (unsigned long long)prewarm_ns,
           (unsigned long long)cold_ns, (unsigned long long)hot_ns, count, (unsigned long long)checksum);
    arena_destroy(arena, 1);
    thread_context_release(context);
    arena_pool_release_thread();
    return 0;
}
