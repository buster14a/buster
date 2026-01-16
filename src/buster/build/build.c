#if 0
#!/usr/bin/env bash
source build.sh
#endif

#pragma once

#define BUSTER_INCLUDE_TESTS 1
#define BUSTER_USE_PADDING 0

#include <buster/base.h>
#include <buster/integer.h>
#include <buster/arena.h>
#include <buster/target.h>
#include <buster/entry_point.h>
#include <buster/assertion.h>
// #include <martins/md5.h>
#include <buster/system_headers.h>
#include <buster/string_os.h>
#include <buster/path.h>
#include <buster/file.h>
#include <buster/build/build_common.h>
#include <buster/os.h>

#if BUSTER_INCLUDE_TESTS
#include <buster/test.h>
#endif

#if BUSTER_UNITY_BUILD
#include <buster/os.c>
#include <buster/arena.c>
#include <buster/assertion.c>
#include <buster/target.c>
#include <buster/memory.c>
#include <buster/string.c>
#include <buster/string8.c>
#include <buster/string_os.c>
#if _WIN32
#include <buster/string16.c>
#endif
#include <buster/integer.c>
#include <buster/file.c>
#include <buster/build/build_common.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/entry_point.c>
#include <buster/path.c>
#include <buster/file.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.c>
#endif
#endif

BUSTER_GLOBAL_LOCAL __attribute__((used)) StringOs toolchain_path = {};
BUSTER_GLOBAL_LOCAL StringOs clang_path = {};
BUSTER_GLOBAL_LOCAL __attribute__((used)) StringOs xc_sdk_path = {};
BUSTER_GLOBAL_LOCAL bool is_stderr_tty = true;

#define BUSTER_TODO() print(S8("TODO\n")); fail()

ENUM_T(ModuleId, u64,
    MODULE_BASE,
    MODULE_ARENA,
    MODULE_ASSERTION,
    MODULE_BUILD_COMMON,
    MODULE_BUILDER,
    MODULE_FILE,
    MODULE_INTEGER,
    MODULE_MEMORY,
    MODULE_OS,
    MODULE_PATH,
    MODULE_STRING16,
    MODULE_STRING8,
    MODULE_STRING_OS,
    MODULE_STRING_COMMON,
    MODULE_SYSTEM_HEADERS,
    MODULE_TEST,
    MODULE_TIME,
    MODULE_ENTRY_POINT,
    MODULE_TARGET,
    MODULE_X86_64,
    MODULE_AARCH64,
    MODULE_MD5,
    MODULE_CC_MAIN,
    MODULE_ASM_MAIN,
    MODULE_IR,
    MODULE_CODEGEN,
    MODULE_LINK,
    MODULE_LINK_JIT,
    MODULE_LINK_ELF,
    MODULE_COUNT,
);

ENUM(DirectoryId,
    DIRECTORY_SRC_BUSTER,
    DIRECTORY_SRC_MARTINS,
    DIRECTORY_ROOT,
    DIRECTORY_CC,
    DIRECTORY_ASM,
    DIRECTORY_IR,
    DIRECTORY_BACKEND,
    DIRECTORY_LINK,
    DIRECTORY_BUILD,
    DIRECTORY_COUNT,
);
STRUCT(Module)
{
    DirectoryId directory;
    bool no_header;
    bool no_source;
    u8 reserved[2];
};

STRUCT(TargetBuildFile)
{
    FileStats stats;
    StringOs full_path;
    BuildTarget* target;
    u64 has_debug_information:1;
    u64 use_io_ring:1;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 reserved:59;
};

STRUCT(ModuleInstantiation)
{
    BuildTarget* target;
    u64 index;
    ModuleId id;
};

STRUCT(LinkModule)
{
    u64 index;
    ModuleId id;
};

STRUCT(ModuleSlice)
{
    LinkModule* pointer;
    u64 length;
};

BUSTER_GLOBAL_LOCAL Module modules[] = {
    [MODULE_ARENA] = {},
    [MODULE_ASSERTION] = {},
    [MODULE_BUILD_COMMON] = {},
    [MODULE_FILE] = {},
    [MODULE_INTEGER] = {},
    [MODULE_MEMORY] = {},
    [MODULE_OS] = {},
    [MODULE_PATH] = {},
    [MODULE_STRING16] = {},
    [MODULE_STRING8] = {},
    [MODULE_STRING_OS] = {},
    [MODULE_STRING_COMMON] = {},
    [MODULE_TEST] = {},
    [MODULE_TIME] = {},
    [MODULE_ENTRY_POINT] = {},
    [MODULE_TARGET] = {},
    [MODULE_X86_64] = {},
    [MODULE_AARCH64] = {},
    [MODULE_BASE] = { .no_source = true },
    [MODULE_SYSTEM_HEADERS] = { .no_source = true, },
    [MODULE_BUILDER] = {
        .directory = DIRECTORY_ROOT,
        .no_header = true,
    },
    [MODULE_MD5] = {
        .directory = DIRECTORY_SRC_MARTINS,
        .no_source = true,
    },
    [MODULE_CC_MAIN] = {
        .directory = DIRECTORY_CC,
        .no_header = true,
    },
    [MODULE_ASM_MAIN] = {
        .directory = DIRECTORY_ASM,
        .no_header = true,
    },
    [MODULE_IR] = {
        .directory = DIRECTORY_IR,
    },
    [MODULE_CODEGEN] = {
        .directory = DIRECTORY_BACKEND,
    },
    [MODULE_LINK] = {
        .directory = DIRECTORY_LINK,
    },
    [MODULE_LINK_JIT] = {
        .directory = DIRECTORY_LINK,
    },
    [MODULE_LINK_ELF] = {
        .directory = DIRECTORY_LINK,
    },
};

static_assert(BUSTER_ARRAY_LENGTH(modules) == MODULE_COUNT);

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    StringOs name;
    ModuleSlice modules;
    StringOs artifact_path;
    BuildTarget* target;
    StringOs* object_paths;
    StringOsList link_arguments;
    StringOsList run_arguments;
    ProcessSpawnResult link_spawn;
    ProcessSpawnResult run_spawn;
    u64 use_io_ring:1;
    u64 has_debug_information:1;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 is_builder:1;
    u64 reserved:58;
};

#if defined(__x86_64__)
BUSTER_GLOBAL_LOCAL constexpr ModuleId cpu_native_module = MODULE_X86_64;
#elif defined(__aarch64__)
BUSTER_GLOBAL_LOCAL constexpr ModuleId cpu_native_module = MODULE_AARCH64;
#endif

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL constexpr ModuleId string_native_module = MODULE_STRING16;
#else
BUSTER_GLOBAL_LOCAL constexpr ModuleId string_native_module = MODULE_STRING8;
#endif

// LinkModule builder_modules[] = {
//     { .id = MODULE_BUILDER },
//     { .id = MODULE_LIB },
//     { .id = MODULE_SYSTEM_HEADERS },
//     { .id = MODULE_ENTRY_POINT },
//     // { MODULE_MD5 },
//     { .id = native_module },
//     { .id = MODULE_TARGET }
// };

BUSTER_GLOBAL_LOCAL LinkModule cc_modules[] = {
    { .id = MODULE_CC_MAIN },
    { .id = MODULE_BASE },
    { .id = MODULE_OS },
    { .id = MODULE_ASSERTION },
    { .id = MODULE_ARENA },
    { .id = MODULE_INTEGER },
    { .id = MODULE_MEMORY },
    { .id = MODULE_SYSTEM_HEADERS },
    { .id = MODULE_ENTRY_POINT },
    { .id = MODULE_IR },
    { .id = MODULE_FILE },
    { .id = MODULE_PATH },
    { .id = MODULE_CODEGEN },
    { .id = MODULE_LINK },
    { .id = MODULE_LINK_JIT },
    { .id = MODULE_LINK_ELF },
    { .id = cpu_native_module },
    { .id = MODULE_TARGET },
    { .id = MODULE_STRING_OS },
    { .id = MODULE_STRING_COMMON },
    { .id = MODULE_STRING8 },
    { .id = string_native_module },
    { .id = MODULE_TEST },
};

BUSTER_GLOBAL_LOCAL LinkModule asm_modules[] = {
    { .id = MODULE_ASM_MAIN },
    { .id = MODULE_BASE },
    { .id = MODULE_ASSERTION },
    { .id = MODULE_OS },
    { .id = MODULE_ARENA },
    { .id = MODULE_INTEGER },
    { .id = MODULE_MEMORY },
    { .id = MODULE_SYSTEM_HEADERS },
    { .id = MODULE_ENTRY_POINT },
    { .id = cpu_native_module },
    { .id = MODULE_TARGET },
    { .id = MODULE_STRING_OS },
    { .id = MODULE_STRING_COMMON },
    { .id = MODULE_STRING8 },
    { .id = string_native_module },
    { .id = MODULE_TEST },
};

BUSTER_GLOBAL_LOCAL u128 hash_file(u8* pointer, u64 length)
{
    BUSTER_CHECK(((u64)pointer & (64 - 1)) == 0);
    u128 digest = 0;
    if (length)
    {
        // TODO:
        // md5_ctx ctx;
        // md5_init(&ctx);
        // md5_update(&ctx, pointer, length);
        // static_assert(sizeof(digest) == MD5_DIGEST_SIZE);
        // md5_finish(&ctx, (u8*)&digest);
    }
    return digest;
}

ENUM(TaskId,
    TASK_ID_COMPILATION,
    TASK_ID_LINKING,
);

ENUM(ProjectId,
    PROJECT_OPERATING_SYSTEM_BUILDER,
    PROJECT_OPERATING_SYSTEM_BOOTLOADER,
    PROJECT_OPERATING_SYSTEM_KERNEL,
    PROJECT_COUNT,
);

STRUCT(CompilationUnit)
{
    BuildTarget* target;
    StringOs compiler;
    StringOsList compilation_arguments;
    StringOs object_path;
    StringOs source_path;
    ProcessSpawnResult compile_spawn;
    // Process process;
    u64 optimize:1;
    u64 has_debug_information:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 use_io_ring:1;
    u64 include_tests:1;
    u64 reserved:58;
#if BUSTER_USE_PADDING
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((5 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
#endif
};

#if BUSTER_USE_PADDING
static_assert(sizeof(CompilationUnit) % BUSTER_CACHE_LINE_GUESS == 0);
#endif

STRUCT(LinkUnit)
{
    // Process link_process;
    // Process run_process;
    String8 artifact_path;
    BuildTarget* target;
    u64* compilations;
    u64 compilation_count;
    bool use_io_ring;
    bool run;
};

BUSTER_GLOBAL_LOCAL void append_string8(Arena* arena, String8 s)
{
    string8_duplicate_arena(arena, s, false);
}

#if defined(_WIN32)
BUSTER_GLOBAL_LOCAL void append_string16(Arena* arena, String16 s)
{
    string16_to_string8_arena(arena, s, false);
}

#define append_os_string append_string16
#else
#define append_os_string append_string8
#endif

BUSTER_GLOBAL_LOCAL bool target_equal(BuildTarget* a, BuildTarget* b)
{
    bool result = a == b;
    if (!result)
    {
        result = memcmp(a->pointer, b->pointer, sizeof(*a->pointer)) == 0;
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL UnitTestResult builder_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {};
    return result;
}
#endif

ENUM(BatchArena,
    BATCH_ARENA_GENERAL,
    BATCH_ARENA_MODULE_LIST,
    BATCH_ARENA_FILE_LIST,
    BATCH_ARENA_COMPILE_COMMANDS,
    BATCH_ARENA_COUNT,
);

STRUCT(BatchTestConfiguration)
{
    Arena* arenas[BATCH_ARENA_COUNT];
    u64 fuzz_time_seconds;
    u64 optimize:1;
    u64 fuzz:1;
    u64 sanitize:1;
    u64 has_debug_information:1;
    u64 unity_build:1;
    u64 just_preprocessor:1;
    u64 reserved:58;
};

BUSTER_GLOBAL_LOCAL BatchTestResult single_run(const BatchTestConfiguration* const configuration)
{
    BatchTestResult result = {};
    let general_arena = configuration->arenas[BATCH_ARENA_GENERAL];
    let cwd = path_absolute_arena(general_arena, SOs("."));

    LinkUnitSpecification specifications[] = {
        { .name = SOs("cc"), .modules = BUSTER_ARRAY_TO_SLICE(ModuleSlice, cc_modules), },
        { .name = SOs("asm"), .modules = BUSTER_ARRAY_TO_SLICE(ModuleSlice, asm_modules), },
    };
    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);

    BuildTarget* target_buffer[link_unit_count];
    u64 target_count = 0;

    for (u64 i = 0; i < link_unit_count; i += 1)
    {
        let link_unit = &specifications[i];
        let target = &build_target_native;
        if (configuration->unity_build)
        {
            link_unit->modules.length = 1;
        }
        link_unit->target = target;
        link_unit->optimize = configuration->optimize;
        link_unit->has_debug_information = configuration->has_debug_information;
        link_unit->fuzz = configuration->fuzz;
        link_unit->sanitize = configuration->sanitize;
        link_unit->is_builder = string_equal(link_unit->name, SOs("builder"));

        u64 target_i;
        for (target_i = 0; target_i < target_count; target_i += 1)
        {
            let target_candidate = target_buffer[target_i];
            if (target == target_candidate)
            {
                break;
            }
        }

        if (target_i == target_count)
        {
            target_buffer[target_i] = target;

            let march = target->pointer->cpu_arch == CPU_ARCH_X86_64 ? SOs("-march=") : SOs("-mcpu=");
            let target_strings = target_to_split_string_os(*target->pointer);
            StringOs march_parts[] = {
                march,
                target_strings.s[TARGET_CPU_MODEL],
            };
            target->march_string = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, march_parts), true);

            StringOs triple_parts[2 * TARGET_STRING_COMPONENT_COUNT - 1];
            for (u64 triple_i = 0; triple_i < TARGET_STRING_COMPONENT_COUNT; triple_i += 1)
            {
                triple_parts[triple_i * 2 + 0] = target_strings.s[triple_i];
                if (triple_i < (TARGET_STRING_COMPONENT_COUNT - 1))
                {
                    triple_parts[triple_i * 2 + 1] = SOs("-");
                }
            }

            let target_triple = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, triple_parts), true);
            target->string = target_triple;

            StringOs directory_path_parts[] = {
                cwd,
                SOs("/build/"),
                target_triple,
            };
            let directory = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, directory_path_parts), true);
            target->directory_path = directory;
            os_make_directory(directory);
            target_count += 1;
        }

        StringOs artifact_path_parts[] = {
            cwd,
            SOs("/build/"),
            link_unit->is_builder ? SOs("") : target->string,
            link_unit->is_builder ? SOs("") : SOs("/"),
            link_unit->name,
            target->pointer->os == OPERATING_SYSTEM_WINDOWS ? SOs(".exe") : SOs(""),
        };

        let artifact_path = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, artifact_path_parts), true);
        link_unit->artifact_path = artifact_path;
    }

    StringOs directory_paths[] = {
        [DIRECTORY_SRC_BUSTER] = SOs("src/buster"),
        [DIRECTORY_SRC_MARTINS] = SOs("src/martins"),
        [DIRECTORY_ROOT] = SOs(""),
        [DIRECTORY_CC] = SOs("src/buster/compiler/frontend/cc"),
        [DIRECTORY_ASM] = SOs("src/buster/compiler/frontend/asm"),
        [DIRECTORY_IR] = SOs("src/buster/compiler/ir"),
        [DIRECTORY_BACKEND] = SOs("src/buster/compiler/backend"),
        [DIRECTORY_LINK] = SOs("src/buster/compiler/link"),
        [DIRECTORY_BUILD] = SOs("src/buster/build"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == DIRECTORY_COUNT);

    StringOs module_names[] = {
        [MODULE_BASE] = SOs("base"),
        [MODULE_SYSTEM_HEADERS] = SOs("system_headers"),
        [MODULE_ENTRY_POINT] = SOs("entry_point"),
        [MODULE_TARGET] = SOs("target"),
        [MODULE_X86_64] = SOs("x86_64"),
        [MODULE_AARCH64] = SOs("aarch64"),
        [MODULE_BUILDER] = SOs("build"),
        [MODULE_MD5] = SOs("md5"),
        [MODULE_MEMORY] = SOs("memory"),
        [MODULE_CC_MAIN] = SOs("cc_main"),
        [MODULE_ASM_MAIN] = SOs("asm_main"),
        [MODULE_IR] = SOs("ir"),
        [MODULE_CODEGEN] = SOs("code_generation"),
        [MODULE_LINK] = SOs("link"),
        [MODULE_LINK_ELF] = SOs("elf"),
        [MODULE_LINK_JIT] = SOs("jit"),
        [MODULE_ARENA] = SOs("arena"),
        [MODULE_ASSERTION] = SOs("assertion"),
        [MODULE_BUILD_COMMON] = SOs("build_common"),
        [MODULE_FILE] = SOs("file"),
        [MODULE_INTEGER] = SOs("integer"),
        [MODULE_OS] = SOs("os"),
        [MODULE_PATH] = SOs("path"),
        [MODULE_STRING16] = SOs("string16"),
        [MODULE_STRING8] = SOs("string8"),
        [MODULE_STRING_OS] = SOs("string_os"),
        [MODULE_STRING_COMMON] = SOs("string"),
        [MODULE_TEST] = SOs("test"),
        [MODULE_TIME] = SOs("time"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(module_names) == MODULE_COUNT);

    if (!configuration->unity_build)
    {
        let cache_manifest = os_file_open(SOs("build/cache_manifest"), (OpenFlags) { .read = 1 }, (OpenPermissions){});
        if (cache_manifest)
        {
            let cache_manifest_stats = os_file_get_stats(cache_manifest, (FileStatsOptions){ .size = 1, .modified_time = 1 });
            let cache_manifest_buffer = (u8*)arena_allocate_bytes(thread_arena(), cache_manifest_stats.size, 64);
            os_file_read(cache_manifest, (ByteSlice){ cache_manifest_buffer, cache_manifest_stats.size }, cache_manifest_stats.size);
            os_file_close(cache_manifest);
            let cache_manifest_hash = hash_file(cache_manifest_buffer, cache_manifest_stats.size);
            BUSTER_UNUSED(cache_manifest_hash);
            string8_print(S8("TODO: Cache manifest found!\n"));
            os_fail();
        }
        else
        {
            BUSTER_UNUSED(target_native);
        }
    }

    let file_list_arena = configuration->arenas[BATCH_ARENA_FILE_LIST];
    let file_list_start = file_list_arena->position;
    let file_list = (TargetBuildFile*)((u8*)file_list_arena + file_list_start);
    u64 file_list_count = 0;

    let module_list_arena = configuration->arenas[BATCH_ARENA_MODULE_LIST];
    let module_list_start = module_list_arena->position;
    let module_list = (ModuleInstantiation*)((u8*)module_list_arena + module_list_start);
    u64 module_list_count = 0;

    u64 c_source_file_count = 0;

    for (u64 link_unit_index = 0; link_unit_index < link_unit_count; link_unit_index += 1)
    {
        let link_unit = &specifications[link_unit_index];
        let link_unit_modules = link_unit->modules;
        let link_unit_target = link_unit->target;

        for (u64 module_index = 0; module_index < link_unit_modules.length; module_index += 1)
        {
            let module = &link_unit_modules.pointer[module_index];
            let module_specification = modules[module->id];

            u64 i;
            for (i = 0; i < module_list_count; i += 1)
            {
                let existing_module = &module_list[i];
                if ((existing_module->id == module->id) & target_equal(existing_module->target, link_unit_target))
                {
                    break;
                }
            }

            if (i == module_list_count)
            {
                let count = (u64)1 + (!module_specification.no_header & !module_specification.no_source);
                file_list_count += count;
                let new_file = arena_allocate(file_list_arena, TargetBuildFile, count);
                let module_name = module_names[module->id];

                // This is wasteful, but it might not matter?
                StringOs parts[] = {
                    cwd,
                    SOs("/"),
                    directory_paths[module_specification.directory],
                    module_specification.directory == DIRECTORY_ROOT ? SOs("") : SOs("/"),
                    module_name,
                    module_specification.no_source ? SOs(".h") : SOs(".c"),
                };
                let c_full_path = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, parts), true);

                *new_file = (TargetBuildFile) {
                    .full_path = c_full_path,
                    .target = link_unit_target,
                    .has_debug_information = link_unit->has_debug_information,
                    .optimize = link_unit->optimize,
                    .fuzz = link_unit->fuzz,
                    .sanitize = link_unit->sanitize,
                };

                let c_source_file_index = c_source_file_count;
                module->index = c_source_file_index;
                c_source_file_count = c_source_file_index + !module_specification.no_source;

                module_list[module_list_count++] = (ModuleInstantiation) {
                    .target = link_unit_target,
                    .id = module->id,
                    .index = module->index,
                };

                if (!module_specification.no_source & !module_specification.no_header)
                {
                    let h_full_path = string_os_duplicate_arena(general_arena, c_full_path, true);
                    h_full_path.pointer[h_full_path.length - 1] = 'h';
                    *(new_file + 1) = (TargetBuildFile) {
                        .full_path = h_full_path,
                        .target = link_unit_target,
                        .has_debug_information = link_unit->has_debug_information,
                        .optimize = link_unit->optimize,
                        .fuzz = link_unit->fuzz,
                        .sanitize = link_unit->sanitize,
                    };
                }
            }
            else
            {
                module->index = module_list[i].index;
            }
        }
    }

    let compilation_unit_count = c_source_file_count;
    let compilation_units = arena_allocate(general_arena, CompilationUnit, c_source_file_count);

    for (u64 file_i = 0, compilation_unit_i = 0; file_i < file_list_count; file_i += 1)
    {
        TargetBuildFile* source_file = &file_list[file_i]; 
        if (!configuration->unity_build)
        {
            let fd = os_file_open(BUSTER_SLICE_START(source_file->full_path, cwd.length + 1), (OpenFlags){ .read = 1 }, (OpenPermissions){});
            let stats = os_file_get_stats(fd, (FileStatsOptions){ .raw = UINT64_MAX });
            let buffer = (u8*)arena_allocate_bytes(general_arena, stats.size, 64);
            ByteSlice buffer_slice = { buffer, stats.size};
            os_file_read(fd, buffer_slice, stats.size);
            os_file_close(fd);
            let __attribute__((unused)) hash = hash_file(buffer_slice.pointer, buffer_slice.length);
        }

        if (source_file->full_path.pointer[source_file->full_path.length - 1] == 'c')
        {
            let compilation_unit = &compilation_units[compilation_unit_i];
            compilation_unit_i += 1;
            *compilation_unit = (CompilationUnit) {
                .target = source_file->target,
                .has_debug_information = source_file->has_debug_information,
                .use_io_ring = source_file->use_io_ring,
                .source_path = source_file->full_path,
                .optimize = source_file->optimize,
                .fuzz = source_file->fuzz,
                .sanitize = source_file->sanitize,
            };
        }
    }

    for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
    {
        let unit = &compilation_units[unit_i];

        let source_absolute_path = unit->source_path;
        let source_relative_path = BUSTER_SLICE_START(source_absolute_path, cwd.length + 1);
        let target_directory_path = unit->target->directory_path;
        StringOs object_absolute_path_parts[] = {
            target_directory_path,
            SOs("/"),
            source_relative_path,
#if _WIN32
            SOs(".obj"),
#else
            SOs(".o"),
#endif
        };

        let object_path = string_os_join_arena(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, object_absolute_path_parts), true);
        unit->object_path = object_path;

        CharOs buffer[BUSTER_MAX_PATH_LENGTH];
        let os_char_size = sizeof(buffer[0]);
        let copy_character_count = target_directory_path.length;
        memcpy(buffer, target_directory_path.pointer, (copy_character_count + 1) * os_char_size);
        buffer[copy_character_count] = 0;

        u64 buffer_i = copy_character_count;
        let buffer_start = buffer_i;
        u64 source_i = 0;
        while (1)
        {
            let source_remaining = BUSTER_SLICE_START(source_relative_path, source_i);
            let slash_index = string_first_code_point(source_remaining, '/');
            if (slash_index == BUSTER_STRING_NO_MATCH)
            {
                break;
            }

            StringOs source_chunk = { source_remaining.pointer, slash_index };

            buffer[buffer_start + source_i] = '/';
            source_i += 1;

            let dst = buffer + buffer_start + source_i;
            let src = source_chunk;
            let byte_count = slash_index;

            memcpy(dst, src.pointer, byte_count * sizeof(src.pointer));
            let length = buffer_start + source_i + byte_count;
            buffer[length] = 0;

            let directory = string_os_from_pointer_length(buffer, length);
            os_make_directory(directory);

            source_i += byte_count; 
        }
    }

    if (!configuration->unity_build)
    {
        let compile_commands = configuration->arenas[BATCH_ARENA_COMPILE_COMMANDS];
        let compile_commands_start = compile_commands->position;
        append_string8(compile_commands, S8("[\n"));

        for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
        {
            let unit = &compilation_units[unit_i];
            let source_absolute_path = unit->source_path;
            let object_path = unit->object_path;

            CompileLinkOptions options = {
                .clang_path = clang_path,
                .xc_sdk_path = xc_sdk_path,
                .destination_path = object_path,
                .source_paths = &unit->source_path,
                .source_count = 1,
                .target = unit->target,
                .optimize = unit->optimize,
                .fuzz = unit->fuzz,
                .sanitize = unit->sanitize,
                .has_debug_information = unit->has_debug_information,
                .unity_build = configuration->unity_build,
                .use_io_ring = unit->use_io_ring,
                .just_preprocessor = configuration->just_preprocessor,
                .include_tests = 1,
                .force_color = is_stderr_tty,
                .compile = 1,
                .link = configuration->unity_build,
            };
            let args = build_compile_link_arguments(general_arena, &options);

            unit->compiler = clang_path;
            unit->compilation_arguments = args;

            append_string8(compile_commands, S8("\t{\n\t\t\"directory\": \""));
            append_string8(compile_commands, string_os_to_string8_arena(general_arena, cwd));
            append_string8(compile_commands, S8("\",\n\t\t\"command\": \""));

            let arg_it = string_os_list_iterator_initialize(args);
            for (let arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it))
            {
                let a = arg;
#ifndef _WIN32
                let double_quote_count = string8_code_point_count(a, '"');
                if (double_quote_count != 0)
                {
                    let new_length = a.length + double_quote_count;
                    a = (String8) { .pointer = arena_allocate(compile_commands, char8, new_length), .length = new_length };
                    bool is_double_quote;
                    for (u64 i = 0, double_quote_i = 0; i < arg.length; i += 1)
                    {
                        let original_ch = arg.pointer[i];
                        is_double_quote = original_ch == '"';
                        let escape_i = double_quote_i;
                        let character_i = double_quote_i + is_double_quote;
                        a.pointer[escape_i] = '\\';
                        a.pointer[character_i] = original_ch;
                        double_quote_i = character_i + 1;
                    }
                }
                else
                {
                    append_os_string(compile_commands, a);
                }
#else
                append_os_string(compile_commands, a);
#endif
                append_string8(compile_commands, S8(" "));
            }

            compile_commands->position -= 1;
            append_string8(compile_commands, S8("\",\n\t\t\"file\": \""));
            append_string8(compile_commands, string_os_to_string8_arena(general_arena, source_absolute_path));
            append_string8(compile_commands, S8("\"\n"));
            append_string8(compile_commands, S8("\t},\n"));
        }

        compile_commands->position -= 2;

        append_string8(compile_commands, S8("\n]"));

        let compile_commands_str = (String8){ .pointer = (char8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };
        result.process = file_write(SOs("build/compile_commands.json"), BUSTER_SLICE_TO_BYTE_SLICE(compile_commands_str)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }

    if (result.process == PROCESS_RESULT_SUCCESS)
    {
        let selected_compilation_count = compilation_unit_count;
        let selected_compilation_units = compilation_units;

        if (!configuration->unity_build)
        {
            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                unit->compile_spawn = os_process_spawn(unit->compiler, unit->compilation_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = (1 << STANDARD_STREAM_OUTPUT) | (1 << STANDARD_STREAM_ERROR) });
            }

            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                let unit_compilation_result = os_process_wait_sync(general_arena, unit->compile_spawn);

                for (StandardStream stream = STANDARD_STREAM_OUTPUT; stream < STANDARD_STREAM_COUNT; stream += 1)
                {
                    let standard_stream = unit_compilation_result.streams[stream];
                    if (standard_stream.length)
                    {
                        let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                        string8_print(stream_string);
                    }
                }

                if (unit_compilation_result.result != PROCESS_RESULT_SUCCESS)
                {
                    result.process = unit_compilation_result.result;

                    if (program_state->input.verbose)
                    {
                        string8_print(S8("FAILED to run the following compiling process: {SOsL}\n"), unit->compilation_arguments);
                    }
                }
            }
        }

        // TODO: depend more-fine grainedly, ie: link those objects which succeeded compiling instead of all or nothing
        if (result.process == PROCESS_RESULT_SUCCESS)
        {
            for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit_specification = &specifications[link_unit_i];
                let link_modules = link_unit_specification->modules;

                let source_paths = (StringOs*)align_forward((u64)((u8*)general_arena + general_arena->position), alignof(StringOs));
                u64 source_path_count = 0;

                u64 module_bitflag = 0;
                static_assert(sizeof(module_bitflag) * 8 >= MODULE_COUNT);

                for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
                {
                    let module = &link_modules.pointer[module_i];

                    if ((module_bitflag & (1 << module->id)) == 0 && !modules[module->id].no_source)
                    {
                        let unit = &compilation_units[module->index];
                        let object_path = arena_allocate(general_arena, StringOs, 1);
                        *object_path = (configuration->just_preprocessor | configuration->unity_build) ? unit->source_path : unit->object_path;
                        source_path_count += 1;
                        module_bitflag |= 1 << module->id;
                    }
                }

                CompileLinkOptions options = {
                    .clang_path = clang_path,
                    .xc_sdk_path = xc_sdk_path,
                    .destination_path = link_unit_specification->artifact_path,
                    .source_paths = source_paths,
                    .source_count = source_path_count,
                    .target = link_unit_specification->target,
                    .optimize = link_unit_specification->optimize,
                    .fuzz = link_unit_specification->fuzz,
                    .has_debug_information = link_unit_specification->has_debug_information,
                    .sanitize = link_unit_specification->sanitize,
                    .unity_build = configuration->unity_build,
                    .use_io_ring = link_unit_specification->use_io_ring,
                    .just_preprocessor = configuration->just_preprocessor,
                    .include_tests = 1,
                    .force_color = is_stderr_tty,
                    .compile = configuration->unity_build,
                    .link = 1,
                };
                let link_arguments = build_compile_link_arguments(general_arena, &options);
                link_unit_specification->link_arguments = link_arguments;
                link_unit_specification->link_spawn = os_process_spawn(clang_path, link_arguments, program_state->input.envp, (ProcessSpawnOptions){ .capture = (1 << STANDARD_STREAM_OUTPUT) | (1 << STANDARD_STREAM_ERROR) });
            }

            for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit = &specifications[link_unit_i];
                let link_result = os_process_wait_sync(general_arena, link_unit->link_spawn);

                for (StandardStream stream = STANDARD_STREAM_OUTPUT; stream < STANDARD_STREAM_COUNT; stream += 1)
                {
                    let standard_stream = link_result.streams[stream];
                    if (standard_stream.length)
                    {
                        let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                        string8_print(stream_string);
                    }
                }

                if (link_result.result != PROCESS_RESULT_SUCCESS)
                {
                    result.process = link_result.result;

                    if (program_state->input.verbose)
                    {
                        string8_print(S8("FAILED to run the following linking process: {SOsL}\n"), link_unit->link_arguments);
                    }
                }
            }
        }

        if (!configuration->just_preprocessor && result.process == PROCESS_RESULT_SUCCESS)
        {
            switch (build_program_state.command)
            {
                break; case BUILD_COMMAND_COUNT: BUSTER_UNREACHABLE();
                break; case BUILD_COMMAND_BUILD: {}
                // TODO: fill
                break; case BUILD_COMMAND_TEST_ALL: case BUILD_COMMAND_TEST:
                {
#if BUSTER_INCLUDE_TESTS
                    UnitTestArguments arguments = {
                        .arena = general_arena,
                    };

                    let builder_unit_tests = builder_tests(&arguments);
                    consume_unit_tests(&result, builder_unit_tests);
                    if (!unit_test_succeeded(builder_unit_tests))
                    {
                        string8_print(S8("Build tests failed!\n"));
                    }

                    u64 fuzz_max_length = 4096;
                    let length_argument = string_os_format_arena(general_arena, SOs("-max_len={u64}"), fuzz_max_length);
                    let fuzz_time_seconds = configuration->fuzz_time_seconds;
                    // Override momentarily since the fuzzing is a no-op for now
                    fuzz_time_seconds = 1;
                    let max_total_time_argument = string_os_format_arena(general_arena, SOs("-max_total_time={u64}"), fuzz_time_seconds);

                    // Skip builder executable since we execute the tests ourselves
                    for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit_specification = &specifications[link_unit_i];

                        let first_argument = link_unit_specification->artifact_path;
                        StringOs fuzz_arguments[] = {
                            first_argument,
                            length_argument,
                            max_total_time_argument,
                        };

                        StringOs test_arguments[] = {
                            first_argument,
                            SOs("test"),
                        };

                        let os_argument_slice = link_unit_specification->fuzz ? BUSTER_ARRAY_TO_SLICE(StringOsSlice, fuzz_arguments) : BUSTER_ARRAY_TO_SLICE(StringOsSlice, test_arguments);
                        let os_arguments = string_os_list_create_from(general_arena, os_argument_slice);
                        link_unit_specification->run_arguments = os_arguments;
                        link_unit_specification->run_spawn = os_process_spawn(first_argument, os_arguments, program_state->input.envp, (ProcessSpawnOptions){ (1 << STANDARD_STREAM_OUTPUT) | (1 << STANDARD_STREAM_ERROR) });
                    }

                    for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit = &specifications[link_unit_i];
                        let test_result = os_process_wait_sync(general_arena, link_unit->run_spawn);

                        for (StandardStream stream = STANDARD_STREAM_OUTPUT; stream < STANDARD_STREAM_COUNT; stream += 1)
                        {
                            let standard_stream = test_result.streams[stream];
                            if (standard_stream.length)
                            {
                                let stream_string = string8_from_pointer_length((char8*)standard_stream.pointer, standard_stream.length);
                                string8_print(stream_string);
                            }
                        }

                        consume_external_tests(&result, test_result.result);
                        if (test_result.result != PROCESS_RESULT_SUCCESS)
                        {
                            let specification = &specifications[link_unit_i];
                            if (program_state->input.verbose)
                            {
                                string8_print(S8("FAILED to run the following executable: {SOsL}\n"), specification->run_arguments);
                            }
                        }
                    }
#endif
                }
                break; case BUILD_COMMAND_DEBUG: {}
            }
        }
    }
    else
    {
        string8_print(S8("Error writing compile commands: {EOs}"), os_get_last_error());
    }

    return result;
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    Arena* arenas[BATCH_ARENA_COUNT];
    for (u64 i = 0; i < BATCH_ARENA_COUNT; i += 1)
    {
        arenas[i] = arena_create((ArenaInitialization){});
    }

    xc_sdk_path = build_program_state.string.values[BUILD_STRING_OPTION_XC_SDK_PATH];
    let toolchain_information = toolchain_get_information(arenas[BATCH_ARENA_GENERAL], current_llvm_version);
    clang_path = toolchain_information.clang_path;
    toolchain_path = toolchain_information.prefix_path;
    is_stderr_tty = os_is_tty(os_get_standard_stream(STANDARD_STREAM_ERROR));

#if defined(_WIN32)
#if defined(__x86_64__)
    let dll_filename = 
#if defined(__x86_64__)
        SOs("clang_rt.asan_dynamic-x86_64.dll");
#elif defined(__aarch64__)
        SOs("clang_rt.asan_dynamic-aarch64.dll");
#else
#pragma error
#endif
    StringOs original_dll_parts[] = {
        toolchain_path,
        SOs("/lib/clang/21/lib/windows/"),
        dll_filename,
    };
    let original_asan_dll = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, original_dll_parts), true);
    let target_strings = target_to_split_string_os(target_native);
    StringOs target_native_dir_path_parts[] = {
        SOs("build/"),
        target_strings.s[0],
        SOs("-"),
        target_strings.s[1],
        SOs("-"),
        target_strings.s[2],
    };
    let target_native_dir_path = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, target_native_dir_path_parts), true);
    os_make_directory(target_native_dir_path);
    StringOs destination_dll_parts[] = {
        target_native_dir_path,
        SOs("/"),
        dll_filename,
    };
    let destination_asan_dll = string_os_join_arena(arenas[BATCH_ARENA_GENERAL], BUSTER_ARRAY_TO_SLICE(StringOsSlice, destination_dll_parts), true);
    file_copy((CopyFileArguments){ .original_path = original_asan_dll, .new_path = destination_asan_dll });
#endif
#endif

    ProcessResult result = PROCESS_RESULT_SUCCESS;

    u64 fuzz_time_seconds = build_integer_option_get_unsigned(BUILD_INTEGER_OPTION_FUZZ_DURATION_SECONDS);
    if (fuzz_time_seconds == 0)
    {
        fuzz_time_seconds = build_flag_get(BUILD_FLAG_CI) ? 10 : 2;

        if (build_flag_get(BUILD_FLAG_CI))
        {
            if (!build_flag_get(BUILD_FLAG_SELF_HOSTED))
            {
                fuzz_time_seconds = build_flag_get(BUILD_FLAG_MAIN_BRANCH) ? 360 : fuzz_time_seconds;
            }
        }
    }

    if (build_program_state.command == BUILD_COMMAND_TEST_ALL)
    {
        u64 succeeded_configuration_run = 0;
        u64 configuration_run = 0;

        let is_arm64_windows = target_native.cpu_arch == CPU_ARCH_AARCH64 && target_native.os == OPERATING_SYSTEM_WINDOWS;
        let is_windows = target_native.os == OPERATING_SYSTEM_WINDOWS;

        for (u64 fuzz = 0; fuzz < 1 + (!is_arm64_windows); fuzz += 1)
        {
            for (u64 optimize = 0; optimize < 2; optimize += 1)
            {
                for (u64 sanitize = 0; sanitize < 2; sanitize += 1)
                {
                    if (fuzz && !sanitize && is_windows)
                    {
                        continue;
                    }

                    for (u64 has_debug_information = 0; has_debug_information < 2; has_debug_information += 1)
                    {
                        for (u64 unity_build = 0; unity_build < 2; unity_build += 1)
                        {
                            string8_print(S8("================================\n"));
                            string8_print(S8("START\n"));
                            string8_print(S8("{S8}: {u64}\n"), S8("Fuzz"), fuzz);
                            string8_print(S8("{S8}: {u64}\n"), S8("Optimize"), optimize);
                            string8_print(S8("{S8}: {u64}\n"), S8("Sanitize"), sanitize);
                            string8_print(S8("{S8}: {u64}\n"), S8("Has debug information: "), has_debug_information);
                            string8_print(S8("{S8}: {u64}\n"), S8("Unity build"), unity_build);
                            string8_print(S8("================================\n"));

                            // system("rm -rf build");
                            os_make_directory(SOs("build"));

                            u64 arena_positions[BATCH_ARENA_COUNT];
                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
                            {
                                arena_positions[i] = arenas[i]->position;
                            }

                            BatchTestConfiguration configuration = {
                                .optimize = optimize,
                                .fuzz = fuzz,
                                .sanitize = sanitize,
                                .has_debug_information = has_debug_information,
                                .unity_build = unity_build,
                                .fuzz_time_seconds = fuzz_time_seconds,
                            };
                            memcpy(configuration.arenas, arenas, sizeof(arenas));

                            let run_result = single_run(&configuration);

                            bool success = run_result.process == PROCESS_RESULT_SUCCESS &&
                                run_result.module_test_count == run_result.succeeded_module_test_count && 
                                run_result.unit_test_count == run_result.succeeded_unit_test_count && 
                                run_result.external_test_count == run_result.succeeded_external_test_count;
                            if (!success)
                            {
                                string8_print(S8("================================\n"));
                                string8_print(S8("{S8}: {u32}\n"), S8("Fuzz"), fuzz);
                                string8_print(S8("{S8}: {u32}\n"), S8("Optimize"), optimize);
                                string8_print(S8("{S8}: {u32}\n"), S8("Sanitize"), sanitize);
                                string8_print(S8("{S8}: {u32}\n"), S8("Has debug information: "), has_debug_information);
                                string8_print(S8("{S8}: {u32}\n"), S8("Unity build"), unity_build);
                                string8_print(S8("{S8}\n"), success ? S8("SUCCEEDED") : S8("FAILED"));
                                string8_print(S8("================================\n"));
                            }

                            succeeded_configuration_run += success;
                            configuration_run += 1;

                            for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(arenas); i += 1)
                            {
                                arenas[i]->position = arena_positions[i];
                            }
                        }
                    }
                }
            }
        }

        result = succeeded_configuration_run == configuration_run ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else
    {
        BatchTestConfiguration configuration = {
            .optimize = build_flag_get(BUILD_FLAG_OPTIMIZE),
            .fuzz = build_flag_get(BUILD_FLAG_FUZZ),
            .sanitize = build_flag_get(BUILD_FLAG_SANITIZE),
            .has_debug_information = build_flag_get(BUILD_FLAG_HAS_DEBUG_INFORMATION),
            .unity_build = build_flag_get(BUILD_FLAG_UNITY_BUILD),
            .just_preprocessor = build_flag_get(BUILD_FLAG_JUST_PREPROCESSOR),
            .fuzz_time_seconds = fuzz_time_seconds,
        };
        memcpy(configuration.arenas, arenas, sizeof(arenas));
        let run_result = single_run(&configuration);
        result = run_result.process;

        if (result == PROCESS_RESULT_SUCCESS)
        {
            bool success = run_result.unit_test_count == run_result.succeeded_unit_test_count && run_result.module_test_count == run_result.succeeded_module_test_count && run_result.external_test_count == run_result.succeeded_external_test_count;
            if (!success)
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
    }

    return result;
}
