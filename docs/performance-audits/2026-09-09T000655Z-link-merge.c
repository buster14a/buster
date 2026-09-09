// Isolated link_objects measurement for audit 2026-09-09T000655Z.
// This snapshot uses ide's module include list, with an entry_point that
// measures fresh and reused output arenas. Build commands are in the audit.

#define BUSTER_USE_GRAPHICS 0

#include <buster/lib/base.h>
#include <buster/lib/entry_point.h>
#include <buster/lib/time.h>
#include <buster/lib/arena.h>
#include <buster/lib/file.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/assembly/aarch64_exact_bridge.h>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_registers.h>
#include <buster/lib/compiler/assembly/aarch64_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_system_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_syntax.h>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.h>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_memory_semantics.h>
#include <buster/lib/compiler/assembly/aarch64_alias_projection.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/assembly/x86_64_metadata.h>
#include <buster/lib/compiler/assembly/x86_64_completion_census.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/debug/debug.h>
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/jit/jit.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/wasm/wasm.h>
#include <buster/lib/compiler/gpu/gpu.h>
#include <buster/lib/compiler/llvm/bitcode.h>
#include <buster/lib/compiler/ebpf/ebpf.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/integer.h>
#include <buster/lib/string.h>
#include <buster/lib/target.h>
#if BUSTER_INCLUDE_TESTS
#include <buster/tests/test.h>
#endif

#if BUSTER_UNITY_BUILD
#if BUSTER_INCLUDE_TESTS
// Keep the intrinsic vocabulary outside the optnone region used for test
// bodies; otherwise Clang can make production SIMD intrinsics uninlinable.
#include <buster/lib/simd.h>
#if BUSTER_COMPILER_CLANG
#pragma clang attribute push (__attribute__((optnone)), apply_to=function)
#endif
#include <buster/tests/test.c>
#if BUSTER_COMPILER_CLANG
#pragma clang attribute pop
#endif
#endif
#include <buster/lib/arena.c>
#include <buster/lib/integer.c>
#include <buster/lib/os.c>
#include <buster/lib/string.c>
#include <buster/lib/entry_point.c>
#include <buster/lib/target.c>
#include <buster/lib/simd.c>
#include <buster/lib/file.c>
#if BUSTER_ANDROID || BUSTER_IOS
// Mobile platforms own the process lifecycle through the window module even
// though the compiler program itself is headless.
#include <buster/lib/window.c>
#endif
#include <buster/lib/time.c>
#include <buster/lib/float.c>
#include <buster/lib/compiler/frontend/c/c.c>
#include <buster/lib/compiler/assembly/aarch64_encoding.c>
#include <buster/lib/compiler/assembly/aarch64_exact_bridge.c>
#include <buster/lib/compiler/assembly/aarch64_control_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_system_registers.c>
#include <buster/lib/compiler/assembly/aarch64_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_system_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_syntax.c>
#include <buster/lib/compiler/assembly/aarch64_semantic_vm.c>
#include <buster/lib/compiler/assembly/aarch64_direct_simd_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_complex_simd_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_memory_semantics.c>
#include <buster/lib/compiler/assembly/aarch64_alias_projection.c>
#include <buster/lib/compiler/assembly/assembly.c>
#include <buster/lib/compiler/assembly/assembly_unit.c>
#include <buster/lib/compiler/assembly/x86_64_metadata.c>
#include <buster/lib/compiler/assembly/x86_64_completion_census.c>
#include <buster/lib/compiler/ir/ir.c>
#include <buster/lib/compiler/debug/debug.c>
#include <buster/lib/compiler/codegen/machine.c>
#include <buster/lib/compiler/codegen/bootstrap_trace.c>
#include <buster/lib/compiler/codegen/codegen.c>
#include <buster/lib/compiler/dwarf/dwarf.c>
#include <buster/lib/compiler/codeview/codeview.c>
#include <buster/lib/compiler/pdb/pdb.c>
#include <buster/lib/compiler/object/object.c>
#include <buster/lib/compiler/jit/jit.c>
#include <buster/lib/compiler/link/link.c>
#include <buster/lib/compiler/wasm/wasm.c>
#include <buster/lib/compiler/gpu/gpu.c>
#include <buster/lib/compiler/llvm/bitcode.c>
#include <buster/lib/compiler/ebpf/ebpf.c>
#include <buster/lib/compiler/driver/driver.c>
#include <buster/lib/hash.c>
#endif


BUSTER_GLOBAL_LOCAL ProgramState link_bench_state;
BUSTER_V_IMPL ProgramState* program_state = &link_bench_state;
ProcessResult process_arguments(void)
{
    return PROCESS_RESULT_SUCCESS;
}
ProcessResult entry_point(void)
{
    u64 sizes[] = {1024, 65536};
    ObjectSectionKind kinds[] = {OBJECT_SECTION_TEXT, OBJECT_SECTION_DATA, OBJECT_SECTION_READ_ONLY_DATA};
    volatile u64 checksum = 0;
    for (u32 shape = 0; shape < BUSTER_ARRAY_LENGTH(sizes); shape += 1)
    {
        u64 size = sizes[shape];
        u8* source = arena_allocate(program_state->arena, u8, size);
        memset(source, 0x41, size);
        ObjectFile inputs[2] = {0};
        for (u32 input = 0; input < 2; input += 1)
        {
            inputs[input].target = (Target){.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
            inputs[input].section_count = OBJECT_SECTION_COUNT;
            inputs[input].sections = arena_allocate_zeroed(program_state->arena, ObjectSection, OBJECT_SECTION_COUNT);
            for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
            {
                inputs[input].sections[kind].kind = (ObjectSectionKind)kind;
                inputs[input].sections[kind].alignment = object_section_default_alignment((ObjectSectionKind)kind);
            }
            for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(kinds); index += 1)
            {
                ObjectSection* section = inputs[input].sections + kinds[index];
                section->data = (ByteSlice){.pointer = source, .length = size - 3};
                section->virtual_size = size - 1;
                section->alignment = 64;
            }
        }
        for (u32 reuse = 0; reuse < 2; reuse += 1)
        {
            Arena* output = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(64), .initial_size = BUSTER_MB(64), .flags = {.no_pool = 1}});
            if (reuse)
            {
                memset(arena_allocate(output, u8, BUSTER_MB(1)), 0xa5, BUSTER_MB(1));
                arena_reset_to_start(output);
            }
            TimeDataType start = timestamp_take();
            for (u32 iteration = 0; iteration < 128; iteration += 1)
            {
                LinkObjectResult linked = link_objects(output, inputs, 2, (LinkOptions){0});
                BUSTER_VALIDATE(linked.error == LINK_ERROR_NONE);
                checksum += linked.object.sections[OBJECT_SECTION_DATA].data.pointer[size - 2];
                if (reuse)
                {
                    arena_reset_to_start(output);
                }
            }
            u64 duration = timestamp_ns_between(start, timestamp_take());
            string_print(S8("LINK_MERGE_BENCH input_bytes={u64} merged_bytes={u64} reuse={u32} iterations=128 duration_ns={u64}\n"), 6 * (size - 3), 3 * (2 * size - 1), reuse, duration);
            arena_destroy(output, 1);
        }
    }
    string_print(S8("checksum={u64}\n"), (u64)checksum);
    return PROCESS_RESULT_SUCCESS;
}
