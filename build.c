#if 0
#!/usr/bin/env bash
source build.sh
#endif

#pragma once

#define BUSTER_USE_PADDING 0

#include <buster/lib.h>
#include <buster/target.h>
#include <buster/entry_point.h>

#if BUSTER_UNITY_BUILD
#include <buster/lib.c>
#include <buster/target.h>
#include <buster/entry_point.c>
#endif

#include <martins/md5.h>
#include <buster/system_headers.h>

#define BUSTER_TODO() BUSTER_TRAP()

ENUM(CompilationModel,
    COMPILATION_MODEL_INCREMENTAL,
    COMPILATION_MODEL_SINGLE_UNIT,
);

ENUM_T(ModuleId, u8,
    MODULE_LIB,
    MODULE_SYSTEM_HEADERS,
    MODULE_ENTRY_POINT,
    MODULE_TARGET,
    MODULE_X86_64,
    MODULE_AARCH64,
    MODULE_BUILDER,
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
    DIRECTORY_COUNT,
);
STRUCT(Module)
{
    DirectoryId directory;
    bool no_header;
    bool no_source;
};

STRUCT(TargetBuildFile)
{
    FileStats stats;
    OsString full_path;
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
    [MODULE_TARGET] = {},
    [MODULE_X86_64] = {},
    [MODULE_AARCH64] = {},
    [MODULE_SYSTEM_HEADERS] = {
        .no_source = true,
    },
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

#define LINK_UNIT(_name, ...) (LinkUnitSpecification) { .name = OsS(#_name), .modules = { .pointer = _name ## _modules, .length = BUSTER_ARRAY_LENGTH(_name ## _modules) }, __VA_ARGS__ }

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    OsString name;
    ModuleSlice modules;
    OsString artifact_path;
    Target target;
    bool use_io_ring;
    bool has_debug_info;
};

#if defined(__x86_64__)
BUSTER_LOCAL constexpr ModuleId native_module = MODULE_X86_64;
#elif defined(__aarch64__)
BUSTER_LOCAL constexpr ModuleId native_module = MODULE_AARCH64;
#endif

ENUM(BuildCommand,
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_TEST,
    BUILD_COMMAND_DEBUG,
);

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

STRUCT(Process)
{
    ProcessResources resources;
    ProcessHandle* handle;
    OsStringList argv;
    OsStringList envp;
    bool waited;
};

static_assert(alignof(Process) == 8);

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
    Target target;
    CompilationModel model;
    OsChar* compiler;
    OsStringList compilation_arguments;
    bool has_debug_info;
    bool use_io_ring;
    bool include_tests;
    OsString object_path;
    OsString source_path;
    Process process;
#if BUSTER_USE_PADDING
    u8 cache_padding[BUSTER_MIN(BUSTER_CACHE_LINE_GUESS - ((5 * sizeof(u64))), BUSTER_CACHE_LINE_GUESS)];
#endif
};

#if BUSTER_USE_PADDING
static_assert(sizeof(CompilationUnit) % BUSTER_CACHE_LINE_GUESS == 0);
#endif

STRUCT(LinkUnit)
{
    Process link_process;
    Process run_process;
    String8 artifact_path;
    Target target;
    u64* compilations;
    u64 compilation_count;
    bool use_io_ring;
    bool run;
};

BUSTER_LOCAL void append_string8(Arena* arena, String8 s)
{
    arena_duplicate_string8(arena, s, false);
}

BUSTER_LOCAL void append_string16(Arena* arena, String16 s)
{
    string16_to_string8(arena, s);
}

#if defined(_WIN32)
#define append_os_string append_string16
#else
#define append_os_string append_string8
#endif

BUSTER_LOCAL bool target_equal(Target a, Target b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

BUSTER_LOCAL OsString xc_sdk_path = {};

BUSTER_LOCAL bool build_compile_commands(Arena* arena, Arena* compile_commands, CompilationUnit* units, u64 unit_count, OsString cwd, OsString clang_path)
{
    bool result = true;

    constexpr u64 max_target_count = 16;
    Target targets[max_target_count];
    u64 target_count = 0;
    BUSTER_UNUSED(targets);
    BUSTER_UNUSED(target_count);

    let compile_commands_start = compile_commands->position;
    append_string8(compile_commands, S8("[\n"));
    print(S8("Unit count: {u64}\n"), unit_count);

    for (u64 unit_i = 0; unit_i < unit_count; unit_i += 1)
    {
        let unit = &units[unit_i];
        print(S8("Unit: {u64}\n"), unit_i);

        let source_absolute_path = unit->source_path;
        let source_relative_path = string_slice_start(source_absolute_path, cwd.length + 1);
        let target_strings = target_to_split_os_string(unit->target);
        static_assert(BUSTER_ARRAY_LENGTH(target_strings.s) == 3);
        OsString object_absolute_path_parts[] = {
            cwd,
            OsS("/build/"),
            target_strings.s[0],
            OsS("-"),
            target_strings.s[1],
            OsS("-"),
            target_strings.s[2],
            OsS("/"),
            source_relative_path,
            OsS(".o"),
        };

        let object_path = arena_join_os_string(arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, object_absolute_path_parts), true);
        unit->object_path = object_path;
        // Forced to do it so early because we would need another arena here otherwise (since arena is used for the argument builder)
        let march = unit->target.cpu.arch == CPU_ARCH_X86_64 ? OsS("-march=") : OsS("-mcpu=");
        let cpu_model_string = cpu_model_to_os_string(unit->target.cpu.model);
        OsString march_parts[] = {
            march,
            cpu_model_string,
        };
        let march_os_string = arena_join_os_string(arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, march_parts), true);

        u64 target_i;
        for (target_i = 0; target_i < target_count; target_i += 1)
        {
            if (target_equal(unit->target, targets[target_i]))
            {
                break;
            }
        }

        OsChar buffer[max_path_length];
        u64 buffer_i = 0;
        let os_char_size = sizeof(buffer[0]);

        for (u64 i = 1; i < BUSTER_ARRAY_LENGTH(target_strings.s) * 2 - 1 + 2; i += 1)
        {
            memcpy(buffer + buffer_i, object_absolute_path_parts[i].pointer + (i == 1), string_size(object_absolute_path_parts[i]) - (((i == 1) * os_char_size)));
            buffer_i += object_absolute_path_parts[i].length - (i == 1);
            if (i == 1)
            {
                BUSTER_CHECK(object_absolute_path_parts[i].pointer[0] == '/');
            }
        }

        buffer[buffer_i] = 0;
        print(S8("Memcpy end\n"));

        if (target_i == target_count)
        {
            let directory = os_string_from_pointer_length(buffer, buffer_i);
            os_make_directory(directory);
            targets[target_count] = unit->target;
            target_count += 1;
        }
        print(S8("First directory\n"));

        let buffer_start = buffer_i;
        u64 source_i = 0;
        while (1)
        {
            let source_remaining = string_slice_start(source_relative_path, source_i);
            let slash_index = os_string_first_character(source_remaining, '/');
            if (slash_index == string_no_match)
            {
                break;
            }

            OsString source_chunk = { source_remaining.pointer, slash_index };

            buffer[buffer_start + source_i] = '/';
            source_i += 1;

            let dst = buffer + buffer_start + source_i;
            let src = source_chunk;
            let byte_count = slash_index;

            memcpy(dst, src.pointer, byte_count * sizeof(src.pointer));
            let length = buffer_start + source_i + byte_count;
            buffer[length] = 0;

            let directory = os_string_from_pointer_length(buffer, length);
            os_make_directory(directory);

            source_i += byte_count; 
        }
        print(S8("All directory. Arena position: {u64:x}\n"), arena->position);

        let builder = argument_builder_start(arena, clang_path);
        if (!builder)
        {
            print(S8("Failed to allocate memory for string builder\n"));
        }
        print(S8("After builder\n"));
        argument_add(builder, OsS("-ferror-limit=1"));
        argument_add(builder, OsS("-c"));
        argument_add(builder, source_absolute_path);
        argument_add(builder, OsS("-o"));
        argument_add(builder, object_path);
        argument_add(builder, OsS("-std=gnu2x"));
        print(S8("-std\n"));

        // if (unit->target.os == OPERATING_SYSTEM_WINDOWS)
        // {
        //     argument_add(builder, OsS("-nostdlib"));
        // }

        if (xc_sdk_path.pointer)
        {
            argument_add(builder, OsS("-isysroot"));
            argument_add(builder, xc_sdk_path);
        }
        print(S8("After SDK path\n"));

        argument_add(builder, OsS("-Isrc"));
        argument_add(builder, OsS("-Wall"));
        argument_add(builder, OsS("-Werror"));
        argument_add(builder, OsS("-Wextra"));
        argument_add(builder, OsS("-Wpedantic"));
        argument_add(builder, OsS("-pedantic"));
        argument_add(builder, OsS("-Wno-gnu-auto-type"));
        argument_add(builder, OsS("-Wno-pragma-once-outside-header"));
        argument_add(builder, OsS("-Wno-gnu-empty-struct"));
        argument_add(builder, OsS("-Wno-bitwise-instead-of-logical"));
        argument_add(builder, OsS("-Wno-unused-function"));
        argument_add(builder, OsS("-Wno-gnu-flexible-array-initializer"));
        argument_add(builder, OsS("-Wno-missing-field-initializers"));
        argument_add(builder, OsS("-Wno-language-extension-token"));

        argument_add(builder, OsS("-funsigned-char"));
        argument_add(builder, OsS("-fwrapv"));
        argument_add(builder, OsS("-fno-strict-aliasing"));

        argument_add(builder, march_os_string);

        if (unit->has_debug_info)
        {
            argument_add(builder, OsS("-g"));
        }

        argument_add(builder, unit->model == COMPILATION_MODEL_SINGLE_UNIT ? OsS("-DBUSTER_UNITY_BUILD=1") : OsS("-DBUSTER_UNITY_BUILD=0"));

        argument_add(builder, unit->use_io_ring ? OsS("-DBUSTER_USE_IO_RING=1") : OsS("-DBUSTER_USE_IO_RING=0"));

        let args = argument_builder_end(builder);

        unit->compiler = (OsChar*)clang_path.pointer;
        unit->compilation_arguments = args;

        append_string8(compile_commands, S8("\t{\n\t\t\"directory\": \""));
        append_string8(compile_commands, os_string_to_string8(arena, cwd));
        append_string8(compile_commands, S8("\",\n\t\t\"command\": \""));

        print(S8("os string start\n"));
        let arg_it = os_string_list_initialize(args);
        for (let arg = os_string_list_next(&arg_it); arg.pointer; arg = os_string_list_next(&arg_it))
        {
            append_os_string(compile_commands, arg);
            append_string8(compile_commands, S8(" "));
        }
        print(S8("os string end\n"));

        compile_commands->position -= 1;
        append_string8(compile_commands, S8("\",\n\t\t\"file\": \""));
        append_string8(compile_commands, os_string_to_string8(arena, source_absolute_path));
        append_string8(compile_commands, S8("\"\n"));
        append_string8(compile_commands, S8("\t},\n"));
    }

    compile_commands->position -= 2;

    append_string8(compile_commands, S8("\n]"));

    let compile_commands_str = (String8){ .pointer = (u8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };

    print(S8("Before writing compile commands\n"));

    if (result)
    {
        result = file_write(OsS("build/compile_commands.json"), compile_commands_str);
        if (!result)
        {
            print(S8("Error writing compile commands: {OsS}"), get_last_error_message(arena));
        }
    }
    print(S8("After writing compile commands\n"));

    return result;
}

BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    let argv = program_state->input.argv;
    let envp = program_state->input.envp;

    {
        BUSTER_UNUSED(argv);
        BUSTER_UNUSED(envp);
        // TODO: arg processing

        let arg_iterator = os_string_list_initialize(argv);
        let arg_it = &arg_iterator;
        let first_argument = os_string_list_next(arg_it);
        BUSTER_UNUSED(first_argument);
        let command = os_string_list_next(arg_it);
        if (command.pointer)
        {
            OsString possible_commands[] = {
                [BUILD_COMMAND_BUILD] = OsS("build"),
                [BUILD_COMMAND_TEST] = OsS("test"),
                [BUILD_COMMAND_DEBUG] = OsS("debug"),
            };

            u64 possible_command_count = BUSTER_ARRAY_LENGTH(possible_commands);

            u64 i;
            for (i = 0; i < possible_command_count; i += 1)
            {
                if (os_string_equal(command, possible_commands[i]))
                {
                    break;
                }
            }

            if (i == possible_command_count)
            {
                u64 argument_index = 1;
                let os_argument_process_result = buster_argument_process(argv, envp, argument_index, command);
                if (os_argument_process_result != PROCESS_RESULT_SUCCESS)
                {
                    result = os_argument_process_result;
                    print(S8("Command not recognized!\n"));
                }
            }
            else
            {
                build_program_state.command = (BuildCommand)i;
            }
        }

        let second_argument = os_string_list_next(arg_it);
        if (second_argument.pointer)
        {
            print(S8("Arguments > 2 not supported\n"));
            result = PROCESS_RESULT_FAILED;
        }
    }

    if (!program_state->input.verbose & (build_program_state.command != BUILD_COMMAND_BUILD))
    {
        program_state->input.verbose = true;
    }

    return result;
}

BUSTER_LOCAL bool builder_tests(TestArguments* arguments)
{
    return lib_tests(arguments);
}

BUSTER_IMPL ProcessResult thread_entry_point()
{
    print(S8("Reached thread entry point\n"));
#if defined(__APPLE__)
    xc_sdk_path = os_get_environment_variable(OsS("XC_SDK_PATH"));
#endif

    LinkModule builder_modules[] = {
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { MODULE_BUILDER },
        { MODULE_MD5 },
        { native_module },
        { MODULE_TARGET }
    };
    LinkModule cc_modules[] = {
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { MODULE_CC_MAIN },
        { MODULE_IR },
        { MODULE_CODEGEN },
        { MODULE_LINK },
        { MODULE_LINK_JIT },
        { MODULE_LINK_ELF },
        { native_module },
        { MODULE_TARGET }
    };
    LinkModule asm_modules[] = {
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { MODULE_ASM_MAIN },
        { native_module },
        { MODULE_TARGET },
    };

    LinkUnitSpecification specifications[] = {
        LINK_UNIT(builder, .target = target_native, .has_debug_info = true),
        LINK_UNIT(cc, .target = target_native, .has_debug_info = true),
        LINK_UNIT(asm, .target = target_native, .has_debug_info = true),
    };
    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);

    OsString directory_paths[] = {
        [DIRECTORY_SRC_BUSTER] = OsS("src/buster"),
        [DIRECTORY_SRC_MARTINS] = OsS("src/martins"),
        [DIRECTORY_ROOT] = OsS(""),
        [DIRECTORY_CC] = OsS("src/buster/compiler/frontend/cc"),
        [DIRECTORY_ASM] = OsS("src/buster/compiler/frontend/asm"),
        [DIRECTORY_IR] = OsS("src/buster/compiler/ir"),
        [DIRECTORY_BACKEND] = OsS("src/buster/compiler/backend"),
        [DIRECTORY_LINK] = OsS("src/buster/compiler/link"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == DIRECTORY_COUNT);

    OsString module_names[] = {
        [MODULE_LIB] = OsS("lib"),
        [MODULE_SYSTEM_HEADERS] = OsS("system_headers"),
        [MODULE_ENTRY_POINT] = OsS("entry_point"),
        [MODULE_TARGET] = OsS("target"),
        [MODULE_X86_64] = OsS("x86_64"),
        [MODULE_AARCH64] = OsS("aarch64"),
        [MODULE_BUILDER] = OsS("build"),
        [MODULE_MD5] = OsS("md5"),
        [MODULE_CC_MAIN] = OsS("cc_main"),
        [MODULE_ASM_MAIN] = OsS("asm_main"),
        [MODULE_IR] = OsS("ir"),
        [MODULE_CODEGEN] = OsS("code_generation"),
        [MODULE_LINK] = OsS("link"),
        [MODULE_LINK_ELF] = OsS("elf"),
        [MODULE_LINK_JIT] = OsS("jit"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(module_names) == MODULE_COUNT);

    let cache_manifest = os_file_open(OsS("build/cache_manifest"), (OpenFlags) { .read = 1 }, (OpenPermissions){});
    let cache_manifest_stats = os_file_get_stats(cache_manifest, (FileStatsOptions){ .size = 1, .modified_time = 1 });
    let cache_manifest_buffer = (u8*)arena_allocate_bytes(thread_arena(), cache_manifest_stats.size, 64);
    os_file_read(cache_manifest, (String8){ cache_manifest_buffer, cache_manifest_stats.size }, cache_manifest_stats.size);
    os_file_close(cache_manifest);
    let cache_manifest_hash = hash_file(cache_manifest_buffer, cache_manifest_stats.size);
    BUSTER_UNUSED(cache_manifest_hash);
    if (cache_manifest)
    {
        print(S8("TODO: Cache manifest found!\n"));
        BUSTER_TRAP();
    }
    else
    {
        BUSTER_UNUSED(target_native);
    }

    let cwd = path_absolute(thread_arena(), OsS("."));
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
                OsString parts[] = {
                    cwd,
                    OsS("/"),
                    directory_paths[module_specification.directory],
                    module_specification.directory == DIRECTORY_ROOT ? OsS("") : OsS("/"),
                    module_names[module->id],
                    module_specification.no_source ? OsS(".h") : OsS(".c"),
                };
                let c_full_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, parts), true);

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
                    let h_full_path = arena_duplicate_os_string(general_arena, c_full_path, true);
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
        let fd = os_file_open(string_slice_start(source_file->full_path, cwd.length + 1), (OpenFlags){ .read = 1 }, (OpenPermissions){});
        let stats = os_file_get_stats(fd, (FileStatsOptions){ .raw = UINT64_MAX });
        let buffer = arena_allocate_bytes(general_arena, stats.size, 64);
        String8 buffer_slice = { buffer, stats.size};
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
    let clang_env = os_get_environment_variable(OsS("CLANG"));
    let clang_path = clang_env;
    if (!clang_path.pointer)
    {
#if defined(_WIN32)
        let home = os_get_environment_variable(OsS("USERPROFILE"));
#else
        let home = os_get_environment_variable(OsS("HOME"));
#endif
        OsString clang_path_parts[] = {
            home,
            OsS("/dev/toolchain/install/llvm_"),
            OsS("21.1.7"), // TODO
            OsS("_"),
            cpu_arch_to_os_string(target_native.cpu.arch),
            OsS("-"),
            operating_system_to_os_string(target_native.os),
            OsS("-Release"),
            OsS("/bin/clang.exe"),
        };
        clang_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, clang_path_parts), true);
    }

    ProcessResult result = {};

    print(S8("Reached compiled commands\n"));
    if (build_compile_commands(general_arena, compile_commands, compilation_units, compilation_unit_count, cwd, clang_path))
    {
        print(S8("Passed compiled commands\n"));

        let selected_compilation_count = compilation_unit_count;
        let selected_compilation_units = compilation_units;

        for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
        {
            let unit = &selected_compilation_units[unit_i];
            unit->process.handle = os_process_spawn(unit->compiler, unit->compilation_arguments, program_state->input.envp);
        }

        print(S8("Spawned compile commands\n"));

        for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
        {
            let unit = &selected_compilation_units[unit_i];
            let unit_compilation_result = os_process_wait_sync(unit->process.handle, unit->process.resources);
            if (unit_compilation_result != PROCESS_RESULT_SUCCESS)
            {
                result = PROCESS_RESULT_FAILED;
            }
        }

        print(S8("Waited compile commands\n"));

        u64 link_unit_start = 1;
        // TODO: depend more-fine grainedly, ie: link those objects which succeeded compiling instead of all or nothing
        if (result == PROCESS_RESULT_SUCCESS)
        {
            let argument_arena = arena_create((ArenaInitialization){});
            ProcessHandle* processes[link_unit_count];
            print(S8("Before spawning linking commands\n"));

            for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit_specification = &specifications[link_unit_i];
                let link_modules = link_unit_specification->modules;

                let builder = argument_builder_start(argument_arena, clang_path);

                argument_add(builder, OsS("-fuse-ld=lld"));
                argument_add(builder, OsS("-o"));

                bool is_builder = link_unit_i == 0; // str_equal(link_unit_specification->name, S("builder"));
                let target_strings = target_to_split_os_string(link_unit_specification->target);
                static_assert(BUSTER_ARRAY_LENGTH(target_strings.s) == 3);

                OsString artifact_path_parts[] = {
                    OsS("build/"),
                    is_builder ? OsS("") : target_strings.s[0],
                    is_builder ? OsS("") : OsS("-"),
                    is_builder ? OsS("") : target_strings.s[1],
                    is_builder ? OsS("") : OsS("-"),
                    is_builder ? OsS("") : target_strings.s[2],
                    is_builder ? OsS("") : OsS("/"),
                    link_unit_specification->name,
                    link_unit_specification->target.os == OPERATING_SYSTEM_WINDOWS ? OsS(".exe") : OsS(""),
                };
                let artifact_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, artifact_path_parts), true);
                link_unit_specification->artifact_path = artifact_path;
                argument_add(builder, artifact_path);

                for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
                {
                    let module = &link_modules.pointer[module_i];
                    let unit = &compilation_units[module->index];
                    let artifact_path = unit->object_path;
                    if (!modules[module->id].no_source)
                    {
                        argument_add(builder, artifact_path);
                    }
                }

                if (link_unit_specification->has_debug_info)
                {
                    argument_add(builder, OsS("-g"));
                }

                if (link_unit_specification->target.os == OPERATING_SYSTEM_WINDOWS)
                {
                    argument_add(builder, OsS("-lws2_32"));
                    //     argument_add(builder, OsS("-nostdlib"));
                    //     argument_add(builder, OsS("-lkernel32"));
                    //     argument_add(builder, OsS("-Wl,-entry:mainCRTStartup"));
                    //     argument_add(builder, OsS("-Wl,-subsystem:console"));
                }

                if (xc_sdk_path.pointer)
                {
                    argument_add(builder, OsS("-isysroot"));
                    argument_add(builder, xc_sdk_path);
                }

                if (link_unit_specification->use_io_ring)
                {
                    argument_add(builder, OsS("-luring"));
                }

                let argv = argument_builder_end(builder);

                let process = os_process_spawn((OsChar*)clang_path.pointer, argv, program_state->input.envp);
                processes[link_unit_i] = process;
            }

            print(S8("Before waiting linking commands\n"));

            for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
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
                    print(S8("Before running tests\n"));
                    ProcessHandle* processes[link_unit_count];

                    // Skip builder tests

                    TestArguments arguments = {
                        .arena = general_arena,
                    };
                    if (!builder_tests(&arguments))
                    {
                        result = PROCESS_RESULT_FAILED;
                    }

                    for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit_specification = &specifications[link_unit_i];

#if defined(_WIN32)
                        OsString argv_parts[] = {
                            link_unit_specification->artifact_path,
                            OsS(" test"),
                        };
                        let argv_os_string = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, argv_parts), true);
                        let argv = argv_os_string.pointer;
                        let first_arg = argv_parts[0].pointer;
#else
                        OsChar* argv[] = {
                            os_string_to_c(link_unit_specification->artifact_path),
                            "test",
                            0,
                        };
                        let first_arg = argv[0];
#endif

                        processes[link_unit_i] = os_process_spawn(first_arg, argv, program_state->input.envp);
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
    }
    else
    {
        result = PROCESS_RESULT_FAILED;
    }

    return result;
}
