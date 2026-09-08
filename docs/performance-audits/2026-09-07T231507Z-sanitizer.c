/* Standalone Linux ASan/UBSan evidence for #39. Production cache and classifier,
   real arena allocation, zero-capacity published types, and retry checkpoints. */
#define BUSTER_UNITY_BUILD 1
#define BUSTER_SINGLE_THREADED 1
#define BUSTER_INCLUDE_TESTS 0
#include <buster/lib/base.h>
#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.c>
#include <buster/lib/os.c>
#include <buster/lib/arena.c>
#include <buster/lib/file.c>
#include <buster/lib/integer.c>
#include <stdio.h>
BUSTER_GLOBAL_LOCAL void audit_initialize(void)
{
    static ProgramState state;
    program_state = &state;
    os_state.page_size = (u64)sysconf(_SC_PAGESIZE);
    os_state.allocation_granularity = os_state.page_size;
    os_state.logical_thread_count = 1;
    pthread_mutex_init(&os_state.entity_mutex, 0);
    os_state.entity_arena = arena_create((ArenaCreation){0});
    state.arena = arena_create((ArenaCreation){0});
    thread_context_select(thread_context_allocate());
}

#include <buster/lib/hash.c>
#include <buster/lib/target.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#endif
#include <buster/lib/compiler/ir/ir.c>

int main(void)
{
    audit_initialize();
    Arena* arena = program_state->arena;
    IrType types[260];
    for (u32 i = 0; i < 260; i += 1)
    {
        types[i] = (IrType){.id = {i}, .kind = IR_TYPE_FLOAT, .bit_width = 64,
                           .layout = {.size = 8, .alignment = 8, .resolved = true}};
    }
    IrProgram program = {.arena = arena, .types = {.types = types, .count = 260}};
    IrAbiContext contexts[IR_ABI_CONVENTION_COUNT];
    for (u32 c = 0; c < IR_ABI_CONVENTION_COUNT; c += 1)
    {
        contexts[c] = ir_abi_context_initialize(arena, &program.types, (IrAbiConvention)c);
        ir_abi_context_reserve(contexts + c, program.types.count);
    }
    TemporalArena checkpoint = arena_begin_temporal(arena);
    u32 checked = 0;
    for (u32 repeat = 0; repeat < 3; repeat += 1)
    {
        for (u32 t = 0; t < 260; t += 1)
        {
            for (u32 c = 0; c < IR_ABI_CONVENTION_COUNT; c += 1)
            {
                for (u32 use = 0; use < IR_ABI_USE_COUNT; use += 1)
                {
                    IrAbiValue got = ir_abi_context_value(&program, contexts + c, (IrTypeId){t}, (IrAbiUse)use);
                    IrAbiValue expected = ir_classify_abi_value(&program, (IrTypeId){t}, (IrAbiConvention)c, use == IR_ABI_USE_RESULT,
                                                               c == IR_ABI_CONVENTION_WINDOWS_AARCH64 && use == IR_ABI_USE_VARIADIC_ARGUMENT);
                    if (memcmp(&got, &expected, sizeof(got)) || arena->position != checkpoint.position) abort();
                    checked += 1;
                }
            }
        }
        memset(arena_allocate(arena, u8, 1024 * 1024), 0xcd, 1024 * 1024);
        scratch_end(checkpoint);
    }
    for (u32 c = 0; c < IR_ABI_CONVENTION_COUNT; c += 1) ir_abi_context_invalidate(contexts + c);
    types[259].kind = IR_TYPE_INTEGER;
    for (u32 c = 0; c < IR_ABI_CONVENTION_COUNT; c += 1)
    {
        IrAbiValue got = ir_abi_context_value(&program, contexts + c, (IrTypeId){259}, IR_ABI_USE_ARGUMENT);
        if (got.parts[0].abi_class != IR_ABI_CLASS_INTEGER) abort();
    }
    printf("ABI sanitizer checks: %u; page/type bytes: %zu/%zu\n", checked, sizeof(IrAbiCachePage), sizeof(IrType));
    return 0;
}
