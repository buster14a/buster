#if 0
#!/usr/bin/env bash
set -eu

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=0
fi

if [[ "$#" != "0" ]]; then
    set -x
fi

if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    export CLANG=$(which clang)
else
    export CLANG=$CMAKE_PREFIX_PATH/bin/clang
fi

#if 0BUSTER_REGENERATE=0 build/builder $@ 2>/dev/null
#endif
#if [[ "$?" != "0" && "$?" != "333" ]]; then
    mkdir -p build
    $CLANG build.c -o build/builder -Isrc -std=gnu2x -march=native -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_IO_RING=1 -DBUSTER_USE_PTHREAD=1 -g -Werror -Wall -Wextra -Wpedantic -pedantic -Wno-gnu-auto-type -Wno-gnu-empty-struct -Wno-bitwise-instead-of-logical -Wno-unused-function -Wno-gnu-flexible-array-initializer -Wno-missing-field-initializers -Wno-pragma-once-outside-header -luring -pthread #-ferror-limit=1 -ftime-trace -ftime-trace-verbose
    if [[ "$?" == "0" ]]; then
        BUSTER_REGENERATE=1 build/builder $@
    fi
#endif fi
exit $?
#endif

#pragma once

#include <lib.h>

#if BUSTER_UNITY_BUILD
#include <lib.c>
#include <entry_point.c>
#else
#include <system_headers.h>
#endif
#include <md5.h>
#include <stdio.h>

#define BUSTER_TODO() BUSTER_TRAP()

typedef enum CompilationModel
{
    COMPILATION_MODEL_INCREMENTAL,
    COMPILATION_MODEL_SINGLE_UNIT,
} CompilationModel;

typedef enum ModuleId : u8
{
    MODULE_LIB,
    MODULE_SYSTEM_HEADERS,
    MODULE_ENTRY_POINT,
    MODULE_BUILDER,
    MODULE_MD5,
    MODULE_CC_MAIN,
    MODULE_COUNT,
} ModuleId;

typedef enum DirectoryId
{
    DIRECTORY_SRC_ROOT,
    DIRECTORY_ROOT,
    DIRECTORY_CC,
    DIRECTORY_COUNT,
} DirectoryId;

BUSTER_LOCAL String directory_paths[] = {
    [DIRECTORY_ROOT] = S(""),
    [DIRECTORY_SRC_ROOT] = S("src"),
    [DIRECTORY_CC] = S("src/compiler/frontend/cc"),
};

static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == DIRECTORY_COUNT);

typedef enum CpuArch
{
    CPU_ARCH_X86_64,
} CpuArch;

typedef enum CpuModel
{
    CPU_MODEL_GENERIC,
    CPU_MODEL_NATIVE,
} CpuModel;

typedef enum OperatingSystem
{
    OPERATING_SYSTEM_LINUX,
    OPERATING_SYSTEM_MACOS,
    OPERATING_SYSTEM_WINDOWS,
    OPERATING_SYSTEM_UEFI,
    OPERATING_SYSTEM_ANDROID,
    OPERATING_SYSTEM_IOS,
    OPERATING_SYSTEM_FREESTANDING,
} OperatingSystem;

STRUCT(Target)
{
    CpuArch arch;
    CpuModel model;
    OperatingSystem os;
};

BUSTER_LOCAL constexpr Target target_native = {
#if defined(__x86_64__)
    .arch = CPU_ARCH_X86_64,
#else
#pragma error
#endif
#if defined(__linux__)
    .os = OPERATING_SYSTEM_LINUX,
#else
#pragma error
#endif
    .model = CPU_MODEL_NATIVE,
};

STRUCT(Module)
{
    DirectoryId directory;
    bool no_header;
    bool no_source;
};

STRUCT(TargetBuildFile)
{
    FileStats stats;
    String full_path;
    Target target;
    CompilationModel model;
    bool has_debug_info;
    bool use_io_ring;
};

STRUCT(ModuleInstantiation)
{
    Target target;
    ModuleId id;
    u64 index;
};

STRUCT(LinkModule)
{
    ModuleId id;
    u64 index;
};

STRUCT(ModuleSlice)
{
    LinkModule* pointer;
    u64 length;
};

BUSTER_LOCAL Module modules[] = {
    [MODULE_LIB] = {},
    [MODULE_ENTRY_POINT] = {},
    [MODULE_SYSTEM_HEADERS] = {
        .no_source = true,
    },
    [MODULE_BUILDER] = {
        .directory = DIRECTORY_ROOT,
        .no_header = true,
    },
    [MODULE_MD5] = {
        .no_source = true,
    },
    [MODULE_CC_MAIN] = {
        .directory = DIRECTORY_CC,
        .no_header = true,
    },
};

static_assert(BUSTER_ARRAY_LENGTH(modules) == MODULE_COUNT);

BUSTER_LOCAL String module_names[] = {
    [MODULE_LIB] = S("lib"),
    [MODULE_SYSTEM_HEADERS] = S("system_headers"),
    [MODULE_ENTRY_POINT] = S("entry_point"),
    [MODULE_BUILDER] = S("build"),
    [MODULE_MD5] = S("md5"),
    [MODULE_CC_MAIN] = S("cc_main"),
};

static_assert(BUSTER_ARRAY_LENGTH(module_names) == MODULE_COUNT);

#define LINK_UNIT_MODULES(_name, ...) BUSTER_LOCAL LinkModule _name ## _modules[] = { __VA_ARGS__ }
#define LINK_UNIT(_name, ...) (LinkUnitSpecification) { .name = S(#_name), .modules = { .pointer = _name ## _modules, .length = BUSTER_ARRAY_LENGTH(_name ## _modules) }, __VA_ARGS__ }

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    String name;
    ModuleSlice modules;
    String artifact_path;
    Target target;
    bool use_io_ring;
};

LINK_UNIT_MODULES(builder, { MODULE_LIB }, { MODULE_SYSTEM_HEADERS }, { MODULE_ENTRY_POINT }, { MODULE_BUILDER }, { MODULE_MD5 });
LINK_UNIT_MODULES(cc, { MODULE_LIB }, { MODULE_SYSTEM_HEADERS }, { MODULE_ENTRY_POINT }, { MODULE_CC_MAIN }, );
BUSTER_LOCAL LinkUnitSpecification specifications[] = {
    LINK_UNIT(builder, .target = target_native),
    LINK_UNIT(cc, .target = target_native),
};

typedef enum BuildCommand
{
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_TEST,
    BUILD_COMMAND_DEBUG,
} BuildCommand;

STRUCT(BuildProgramState)
{
    ProgramState general_state;
    BuildCommand command;
};

BUSTER_LOCAL BuildProgramState build_program_state = {};
BUSTER_IMPL ProgramState* program_state = &build_program_state.general_state;

BUSTER_LOCAL u128 hash_file(u8* pointer, u64 length)
{
    BUSTER_CHECK(((u64)pointer & (64 - 1)) == 0);
    u128 digest = 0;
    if (length)
    {
        md5_ctx ctx;
        md5_init(&ctx);
        md5_update(&ctx, pointer, length);
        static_assert(sizeof(digest) == MD5_DIGEST_SIZE);
        md5_finish(&ctx, (u8*)&digest);
    }
    return digest;
}

#include <sys/resource.h>

STRUCT(Process)
{
    ProcessResources resources;
    ProcessHandle* handle;
    char** argv;
    char** envp;
    bool waited;
};

static_assert(alignof(Process) == 8);

BUSTER_LOCAL void spawn_process(Process* process, char* argv[], char* envp[])
{
    let pid = fork();

    if (pid == -1)
    {
        if (program_state->input.verbose) printf("Failed to fork\n");
    }
    else if (pid == 0)
    {
        execve(argv[0], argv, envp);
        BUSTER_TRAP();
    }

    if (program_state->input.verbose)
    {
        printf("Launched: ");

        for (let a = argv; *a; a += 1)
        {
            printf("%s ", *a);
        }

        printf("\n");
    }

    *process = (Process) {
        .argv = argv,
        .envp = envp,
        .handle = pid == -1 ? (ProcessHandle*)0 : (ProcessHandle*)(u64)pid,
    };
}

typedef enum TaskId
{
    TASK_ID_COMPILATION,
    TASK_ID_LINKING,
} TaskId;

typedef enum ProjectId
{
    PROJECT_OPERATING_SYSTEM_BUILDER,
    PROJECT_OPERATING_SYSTEM_BOOTLOADER,
    PROJECT_OPERATING_SYSTEM_KERNEL,
    PROJECT_COUNT,
} ProjectId;

BUSTER_LOCAL String target_to_string_builder(Target target)
{
    switch (target.arch)
    {
        break; case CPU_ARCH_X86_64:
        {
            switch (target.model)
            {
                break; case CPU_MODEL_GENERIC:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return S("x86_64-linux-baseline");
                        break; case OPERATING_SYSTEM_MACOS: return S("x86_64-macos-baseline");
                        break; case OPERATING_SYSTEM_WINDOWS: return S("x86_64-windows-baseline");
                        break; case OPERATING_SYSTEM_UEFI: return S("x86_64-uefi-baseline");
                        break; case OPERATING_SYSTEM_FREESTANDING: return S("x86_64-freestanding-baseline");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; case CPU_MODEL_NATIVE:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return S("x86_64-linux-native");
                        break; case OPERATING_SYSTEM_MACOS: return S("x86_64-macos-native");
                        break; case OPERATING_SYSTEM_WINDOWS: return S("x86_64-windows-native");
                        break; case OPERATING_SYSTEM_UEFI: return S("x86_64-uefi-native");
                        break; case OPERATING_SYSTEM_FREESTANDING: return S("x86_64-freestanding-native");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; default: BUSTER_UNREACHABLE();
            }
        }
        break; default: BUSTER_UNREACHABLE();
    }
}

STRUCT(CompilationUnit)
{
    Target target;
    CompilationModel model;
    char** compilation_arguments;
    bool has_debug_info;
    bool use_io_ring;
    String object_path;
    String source_path;
    Process process;
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((5 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
};

static_assert(sizeof(CompilationUnit) % BUSTER_CACHE_LINE_GUESS == 0);

STRUCT(LinkUnit)
{
    Process link_process;
    Process run_process;
    String artifact_path;
    Target target;
    u64* compilations;
    u64 compilation_count;
    bool use_io_ring;
    bool run;
};

BUSTER_LOCAL void append_string(Arena* arena, String s)
{
    let allocation = arena_allocate_bytes(arena, s.length, 1);
    memcpy(allocation, s.pointer, s.length);
}

BUSTER_LOCAL bool target_equal(Target a, Target b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

BUSTER_LOCAL bool build_compile_commands(Arena* arena, Arena* compile_commands, CompilationUnit* units, u64 unit_count, String cwd, char* clang_path)
{
    bool result = true;

    constexpr u64 max_target_count = 16;
    Target targets[max_target_count];
    u64 target_count = 0;
    BUSTER_UNUSED(targets);
    BUSTER_UNUSED(target_count);

    let compile_commands_start = compile_commands->position;
    append_string(compile_commands, S("[\n"));

    for (u64 unit_i = 0; unit_i < unit_count; unit_i += 1)
    {
        let unit = &units[unit_i];

        let source_absolute_path = unit->source_path;
        let source_relative_path = str_slice_start(source_absolute_path, cwd.length + 1);
        let target_string_builder = target_to_string_builder(unit->target);
        String object_absolute_path_parts[] = {
            cwd,
            S("/build/"),
            target_string_builder,
            S("/"),
            source_relative_path,
            S(".o"),
        };

        let object_path = arena_join_string(arena, BUSTER_STRING_ARRAY_TO_SLICE(object_absolute_path_parts), true);
        unit->object_path = object_path;

        u64 target_i;
        for (target_i = 0; target_i < target_count; target_i += 1)
        {
            if (target_equal(unit->target, targets[target_i]))
            {
                break;
            }
        }

        char buffer[PATH_MAX];
        u64 buffer_i = 0;
        {
            memcpy(buffer + buffer_i, object_absolute_path_parts[1].pointer + 1, object_absolute_path_parts[1].length - 1);
            BUSTER_CHECK(object_absolute_path_parts[1].pointer[0] == '/');
            buffer_i += object_absolute_path_parts[1].length - 1;
        }

        {
            memcpy(buffer + buffer_i, object_absolute_path_parts[2].pointer, object_absolute_path_parts[2].length);
            buffer_i += object_absolute_path_parts[2].length;
        }

        buffer[buffer_i] = 0;

        if (target_i == target_count)
        {
            mkdir(buffer, 0755);
            targets[target_count] = unit->target;
            target_count += 1;
        }

        if (program_state->input.verbose) printf("Start making paths for %s. Starting point: %.*s\n", object_path.pointer, (int)buffer_i, buffer);

        let buffer_start = buffer_i;
        u64 source_i = 0;
        while (1)
        {
            String source_remaining = str_slice_start(source_relative_path, source_i);
            let slash_index = str_first_ch(source_remaining, '/');
            if (slash_index == string_no_match)
            {
                break;
            }

            String source_chunk = { source_remaining.pointer, slash_index };

            buffer[buffer_start + source_i] = '/';
            source_i += 1;

            let dst = buffer + buffer_start + source_i;
            let src = source_chunk;
            let byte_count = slash_index;

            memcpy(dst, src.pointer, byte_count);
            buffer[buffer_start + source_i + byte_count] = 0;

            mkdir(buffer, 0755);

            source_i += byte_count; 
        }

        let builder = argument_builder_start(arena, clang_path);
        argument_add(builder, "-ferror-limit=1");
        argument_add(builder, "-c");
        argument_add(builder, (char*)source_absolute_path.pointer);
        argument_add(builder, "-o");
        argument_add(builder, (char*)object_path.pointer);
        argument_add(builder, "-std=gnu2x");
        argument_add(builder, "-Isrc");
        argument_add(builder, "-Wall");
        argument_add(builder, "-Werror");
        argument_add(builder, "-Wextra");
        argument_add(builder, "-Wpedantic");
        argument_add(builder, "-pedantic");
        argument_add(builder, "-Wno-gnu-auto-type");
        argument_add(builder, "-Wno-pragma-once-outside-header");
        argument_add(builder, "-Wno-gnu-empty-struct");
        argument_add(builder, "-Wno-bitwise-instead-of-logical");
        argument_add(builder, "-Wno-unused-function");
        argument_add(builder, "-Wno-gnu-flexible-array-initializer");
        argument_add(builder, "-Wno-missing-field-initializers");

        switch (unit->target.model)
        {
            break; case CPU_MODEL_NATIVE: argument_add(builder, "-march=native");
            break; case CPU_MODEL_GENERIC: {}
        }

        if (unit->has_debug_info)
        {
            argument_add(builder, "-g");
        }

        argument_add(builder, unit->model == COMPILATION_MODEL_SINGLE_UNIT ? "-DBUSTER_UNITY_BUILD=1" : "-DBUSTER_UNITY_BUILD=0");

        argument_add(builder, unit->use_io_ring ? "-DBUSTER_USE_IO_RING=1" : "-DBUSTER_USE_IO_RING=0");

        let args = argument_builder_end(builder);

        unit->compilation_arguments = args;

        append_string(compile_commands, S("\t{\n\t\t\"directory\": \""));
        append_string(compile_commands, cwd);
        append_string(compile_commands, S("\",\n\t\t\"command\": \""));

        for (char** a = args; *a; a += 1)
        {
            let arg_ptr = *a;
            let arg_len = strlen(arg_ptr);
            let arg = (String){ (u8*)arg_ptr, arg_len };
            append_string(compile_commands, arg);
            append_string(compile_commands, S(" "));
        }

        compile_commands->position -= 1;
        append_string(compile_commands, S("\",\n\t\t\"file\": \""));
        append_string(compile_commands, source_absolute_path);
        append_string(compile_commands, S("\"\n"));
        append_string(compile_commands, S("\t},\n"));
    }
    compile_commands->position -= 2;

    append_string(compile_commands, S("\n]"));

    let compile_commands_str = (String){ .pointer = (u8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };

    if (result)
    {
        result = file_write(S("build/compile_commands.json"), compile_commands_str);
        if (!result)
        {
            perror(get_last_error_message());
        }
    }

    return result;
}

BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    let argc = program_state->input.argc;
    let argv = program_state->input.argv;
    let envp = program_state->input.envp;

    {
        let arg_ptr = argv + 1;
        let arg_top = argv + argc;

        if (arg_ptr != arg_top)
        {
            let a = *arg_ptr;
            arg_ptr += 1;

            if (strcmp(a, "test") == 0)
            {
                build_program_state.command = BUILD_COMMAND_TEST;
            }
            else if (strcmp(a, "debug") == 0)
            {
                build_program_state.command = BUILD_COMMAND_DEBUG;
            }
            else
            {
                result = buster_argument_process(argc, argv, envp, 1);

                if (result != PROCESS_RESULT_SUCCESS)
                {
                    printf("Command '%s' not recognized\n", a);
                }
            }
        }

        if (result == PROCESS_RESULT_SUCCESS)
        {
            while (arg_ptr != arg_top)
            {
                printf("Arguments > 2 not supported\n");
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
    }

    if (!program_state->input.verbose & (build_program_state.command != BUILD_COMMAND_BUILD))
    {
        program_state->input.verbose = true;
    }

    return result;
}

BUSTER_IMPL ProcessResult thread_entry_point(Thread* thread)
{
    let cache_manifest = os_file_open(S("build/cache_manifest"), (OpenFlags) { .read = 1 }, (OpenPermissions){});
    let cache_manifest_stats = os_file_get_stats(cache_manifest, (FileStatsOptions){ .size = 1, .modified_time = 1 });
    let cache_manifest_buffer = (u8*)arena_allocate_bytes(thread->arena, cache_manifest_stats.size, 64);
    os_file_read(cache_manifest, (String){ cache_manifest_buffer, cache_manifest_stats.size }, cache_manifest_stats.size);
    os_file_close(cache_manifest);
    let cache_manifest_hash = hash_file(cache_manifest_buffer, cache_manifest_stats.size);
    BUSTER_UNUSED(cache_manifest_hash);
    if (cache_manifest)
    {
        BUSTER_TRAP();
    }
    else
    {
        BUSTER_UNUSED(target_native);
    }

    let cwd = path_absolute(thread->arena, ".");
    let general_arena = arena_create((ArenaInitialization){});
    let file_list_arena = arena_create((ArenaInitialization){});
    let file_list_start = file_list_arena->position;
    let file_list = (TargetBuildFile*)((u8*)file_list_arena + file_list_start);
    u64 file_list_count = 0;

    let module_list_arena = arena_create((ArenaInitialization){});
    let module_list_start = module_list_arena->position;
    let module_list = (ModuleInstantiation*)((u8*)module_list_arena + module_list_start);
    u64 module_list_count = 0;

    u64 c_source_file_count = 0;

    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);

    for (u64 link_unit_index = 0; link_unit_index < link_unit_count; link_unit_index += 1)
    {
        let link_unit = &specifications[link_unit_index];
        let link_unit_modules = link_unit->modules;
        let link_unit_target = link_unit->target;

        for (u64 module_index = 0; module_index < link_unit_modules.length; module_index += 1)
        {
            let module = &link_unit_modules.pointer[module_index];
            // if ((!recompile_builder) & (module == MODULE_BUILDER))
            // {
            //     continue;
            // }

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

                // This is wasteful, but it might not matter?
                String parts[] = {
                    cwd,
                    S("/"),
                    directory_paths[module_specification.directory],
                    module_specification.directory == DIRECTORY_ROOT ? S("") : S("/"),
                    module_names[module->id],
                    module_specification.no_source ? S(".h") : S(".c"),
                };
                let c_full_path = arena_join_string(general_arena, BUSTER_STRING_ARRAY_TO_SLICE(parts), true);

                *new_file = (TargetBuildFile) {
                    .full_path = c_full_path,
                    .target = link_unit_target,
                    .has_debug_info = true,
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
                    let h_full_path = arena_duplicate_string(general_arena, c_full_path, true);
                    h_full_path.pointer[h_full_path.length - 1] = 'h';
                    *(new_file + 1) = (TargetBuildFile) {
                        .full_path = h_full_path,
                        .target = link_unit_target,
                        .has_debug_info = true,
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
        let fd = os_file_open(str_slice_start(source_file->full_path, cwd.length + 1), (OpenFlags){ .read = 1 }, (OpenPermissions){});
        let stats = os_file_get_stats(fd, (FileStatsOptions){ .raw = UINT64_MAX });
        let buffer = arena_allocate_bytes(general_arena, stats.size, 64);
        String buffer_slice = { buffer, stats.size};
        os_file_read(fd, buffer_slice, stats.size);
        os_file_close(fd);
        let __attribute__((unused)) hash = hash_file(buffer_slice.pointer, buffer_slice.length);

        if (source_file->full_path.pointer[source_file->full_path.length - 1] == 'c')
        {
            let compilation_unit = &compilation_units[compilation_unit_i];
            compilation_unit_i += 1;
            *compilation_unit = (CompilationUnit) {
                .target = source_file->target,
                .model = source_file->model,
                .has_debug_info = source_file->has_debug_info,
                .use_io_ring = source_file->use_io_ring,
                .source_path = source_file->full_path,
            };
        }
    }

    let compile_commands = arena_create((ArenaInitialization){});
    let clang_env = getenv("CLANG");
    char* clang_path = clang_env ? clang_env : "/usr/bin/clang";
    build_compile_commands(general_arena, compile_commands, compilation_units, compilation_unit_count, cwd, clang_path);

    let selected_compilation_count = compilation_unit_count;
    let selected_compilation_units = compilation_units;

    for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
    {
        let unit = &selected_compilation_units[unit_i];
        spawn_process(&unit->process, unit->compilation_arguments, program_state->input.envp);
    }

    ProcessResult result = {};

    for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
    {
        let unit = &selected_compilation_units[unit_i];
        ProcessResources resources = {};
        let unit_compilation_result = os_process_wait_sync(unit->process.handle, resources);
        if (unit_compilation_result != PROCESS_RESULT_SUCCESS)
        {
            result = PROCESS_RESULT_FAILED;
        }
    }

    // TODO: depend more-fine grainedly, ie: link those objects which succeeded compiling instead of all or nothing
    if (result == PROCESS_RESULT_SUCCESS)
    {
        let argument_arena = arena_create((ArenaInitialization){});
        ProcessHandle* processes[link_unit_count];

        for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
        {
            let link_unit_specification = &specifications[link_unit_i];
            let link_modules = link_unit_specification->modules;

            let builder = argument_builder_start(argument_arena, clang_path);

            argument_add(builder, "-fuse-ld=lld");
            argument_add(builder, "-o");

            bool is_builder = link_unit_i == 0; // str_equal(link_unit_specification->name, S("builder"));

            String artifact_path_parts[] = {
                S("build/"),
                is_builder ? S("") : target_to_string_builder(link_unit_specification->target),
                is_builder ? S("") : S("/"),
                link_unit_specification->name,
            };
            let artifact_path = arena_join_string(general_arena, BUSTER_STRING_ARRAY_TO_SLICE(artifact_path_parts), true);
            link_unit_specification->artifact_path = artifact_path;
            argument_add(builder, (char*)artifact_path.pointer);

            for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
            {
                let module = &link_modules.pointer[module_i];
                let unit = &compilation_units[module->index];
                let artifact_path = unit->object_path;
                if (!modules[module->id].no_source)
                {
                    argument_add(builder, (char*)artifact_path.pointer);
                }
            }

            if (link_unit_specification->use_io_ring)
            {
                argument_add(builder, "-luring");
            }

            let argv = argument_builder_end(builder);
            bool debug = false;
            if (debug)
            {
                let a = argv;
                while (*a)
                {
                    printf("%s ", *a);
                    a += 1;
                }
                printf("\n");
            }

            let process = os_process_spawn(argv, program_state->input.envp);
            processes[link_unit_i] = process;
        }

        for (u64 link_unit_i = 0; link_unit_i < link_unit_count; link_unit_i += 1)
        {
            let process = processes[link_unit_i];
            ProcessResources resources = {};
            let link_result = os_process_wait_sync(process, resources);
            if (link_result != PROCESS_RESULT_SUCCESS)
            {
                result = PROCESS_RESULT_FAILED;
            }
        }
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
        switch (build_program_state.command)
        {
            break; case BUILD_COMMAND_BUILD: {}
            break; case BUILD_COMMAND_TEST:
            {
                ProcessHandle* processes[link_unit_count];
                
                // Skip builder tests
                u64 link_unit_start = 1;

                for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                {
                    let link_unit_specification = &specifications[link_unit_i];

                    char* argv[] = {
                        (char*)link_unit_specification->artifact_path.pointer,
                        "test",
                        0,
                    };

                    processes[link_unit_i] = os_process_spawn(argv, program_state->input.envp);
                }

                for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                {
                    let process = processes[link_unit_i];
                    ProcessResources resources = {};
                    let test_result = os_process_wait_sync(process, resources);
                    if (test_result != PROCESS_RESULT_SUCCESS)
                    {
                        result = PROCESS_RESULT_FAILED;
                    }
                }
            }
            break; case BUILD_COMMAND_DEBUG: { }
        }
    }

    return result;
}
