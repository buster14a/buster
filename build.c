#if 0
#!/usr/bin/env bash
source build.sh
#endif

#pragma once

#define BUSTER_USE_PADDING 0

#include <buster/lib.h>
#include <buster/target.h>
#include <buster/entry_point.h>
#include <martins/md5.h>
#include <buster/system_headers.h>

STRUCT(BuildTarget)
{
    Target* pointer;
    OsString string;
    OsString march_string;
    OsString directory_path;
};

STRUCT(LLVMVersion)
{
    u8 major;
    u8 minor;
    u8 revision;
    OsString string;
};

BUSTER_LOCAL __attribute__((used)) OsString toolchain_path = {};
BUSTER_LOCAL OsString clang_path = {};
BUSTER_LOCAL OsString xc_sdk_path = {};

#define BUSTER_TODO() BUSTER_TRAP()

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
    BuildTarget* target;
    bool has_debug_information;
    bool use_io_ring;
    bool optimize;
    bool fuzz;
};

STRUCT(ModuleInstantiation)
{
    BuildTarget* target;
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

#define LINK_UNIT(_name, ...) (LinkUnitSpecification) { .name = OsS(#_name), .modules = { .pointer = _name ## _modules, .length = build_flag_get(BUILD_FLAG_UNITY_BUILD) ? 1 : BUSTER_ARRAY_LENGTH(_name ## _modules) }, __VA_ARGS__ }

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    OsString name;
    ModuleSlice modules;
    OsString artifact_path;
    BuildTarget* target;
    OsString* object_paths;
    bool use_io_ring;
    bool has_debug_information;
    bool optimize;
    bool fuzz;
    bool is_builder;
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
    BUILD_COMMAND_COUNT,
);

typedef enum BuildFlag
{
    BUILD_FLAG_OPTIMIZE,
    BUILD_FLAG_FUZZ,
    BUILD_FLAG_CI,
    BUILD_FLAG_HAS_DEBUG_INFORMATION,
    BUILD_FLAG_UNITY_BUILD,
    BUILD_FLAG_JUST_PREPROCESSOR,
    BUILD_FLAG_SELF_HOSTED,
    BUILD_FLAG_COUNT,
} BuildFlag;

typedef u64 FlagBackingType;

STRUCT(BuildProgramState)
{
    ProgramState general_state;
    BuildCommand command;
    FlagBackingType value_flags;
    FlagBackingType set_flags;
};

static_assert(sizeof(FlagBackingType) * 8 > BUILD_FLAG_COUNT);

BUSTER_LOCAL BuildProgramState build_program_state = {};
BUSTER_IMPL ProgramState* program_state = &build_program_state.general_state;

BUSTER_LOCAL bool build_flag_is_set(BuildFlag flag)
{
    return (build_program_state.set_flags & ((FlagBackingType)1 << flag)) != 0;
}

BUSTER_LOCAL bool build_flag_get(BuildFlag flag)
{
    return (build_program_state.value_flags & ((FlagBackingType)1 << flag)) != 0;
}

BUSTER_LOCAL void build_flag_set(BuildFlag flag, bool value)
{
    build_program_state.value_flags |= (FlagBackingType)value << flag;
    build_program_state.set_flags |= (FlagBackingType)1 << flag;
}

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
    BuildTarget* target;
    OsChar* compiler;
    OsStringList compilation_arguments;
    bool optimize;
    bool has_debug_information;
    bool fuzz;
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
    BuildTarget* target;
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

BUSTER_LOCAL bool target_equal(BuildTarget* a, BuildTarget* b)
{
    bool result = a == b;
    if (!result)
    {
        result = memcmp(a->pointer, b->pointer, sizeof(*a->pointer)) == 0;
    }
    return result;
}

BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    let argv = program_state->input.argv;
    let envp = program_state->input.envp;
    bool any_arguments;

    {
        BUSTER_UNUSED(argv);
        BUSTER_UNUSED(envp);
        // TODO: arg processing

        let arg_iterator = os_string_list_initialize(argv);
        let arg_it = &arg_iterator;
        let first_argument = os_string_list_next(arg_it);
        BUSTER_UNUSED(first_argument);
        let command = os_string_list_next(arg_it);
        any_arguments = command.pointer != 0;

        if (any_arguments)
        {
            OsString possible_commands[] = {
                [BUILD_COMMAND_BUILD] = OsS("build"),
                [BUILD_COMMAND_TEST] = OsS("test"),
                [BUILD_COMMAND_DEBUG] = OsS("debug"),
            };
            static_assert(BUSTER_ARRAY_LENGTH(possible_commands) == BUILD_COMMAND_COUNT);

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

        OsString argument;
        while ((argument = os_string_list_next(arg_it)).pointer)
        {
            OsString build_flag_strings[] = {
                [BUILD_FLAG_OPTIMIZE] = OsS("--optimize="),
                [BUILD_FLAG_FUZZ] = OsS("--fuzz="),
                [BUILD_FLAG_CI] = OsS("--ci="),
                [BUILD_FLAG_HAS_DEBUG_INFORMATION] = OsS("--has-debug-information="),
                [BUILD_FLAG_UNITY_BUILD] = OsS("--unity-build="),
                [BUILD_FLAG_JUST_PREPROCESSOR] = OsS("--just-preprocessor="),
                [BUILD_FLAG_SELF_HOSTED] = OsS("--self-hosted="),
            };
            static_assert(BUSTER_ARRAY_LENGTH(build_flag_strings) == BUILD_FLAG_COUNT);

            FlagBackingType i;
            for (i = 0; i < BUILD_FLAG_COUNT; i += 1)
            {
                let build_flag_string = build_flag_strings[i];
                if (os_string_starts_with(argument, build_flag_string))
                {
                    bool is_valid_value = argument.length == build_flag_string.length + 1;

                    if (is_valid_value)
                    {
                        let arg_value = argument.pointer[build_flag_string.length];
                        switch (arg_value)
                        {
                            break; case '1': case '0': build_flag_set(i, arg_value == '1');
                            break; default:
                            {
                                is_valid_value = false;
                            }
                        }
                    }

                    if (!is_valid_value)
                    {
                        print(S8("For argument '{OsS}', unrecognized value\n"), argument);
                        result = PROCESS_RESULT_FAILED;
                    }

                    break;
                }
            }

            if (i == BUILD_FLAG_COUNT)
            {
                print(S8("Unrecognized argument: '{OsS}'\n"), argument);
                result = PROCESS_RESULT_FAILED;
                break;
            }
        }
    }

    if (!build_flag_is_set(BUILD_FLAG_HAS_DEBUG_INFORMATION))
    {
        build_flag_set(BUILD_FLAG_HAS_DEBUG_INFORMATION, true);
    }

    if (!build_flag_is_set(BUILD_FLAG_UNITY_BUILD))
    {
        build_flag_set(BUILD_FLAG_UNITY_BUILD, build_flag_get(BUILD_FLAG_OPTIMIZE));
    }

    if (any_arguments & !program_state->input.verbose)
    {
        program_state->input.verbose = true;
    }

    return result;
}

BUSTER_LOCAL bool builder_tests(TestArguments* arguments)
{
    return lib_tests(arguments);
}

STRUCT(CompileLinkOptions)
{
    OsString destination_path;
    OsString* source_paths;
    u64 source_count;
    BuildTarget* target;

    bool optimize;
    bool fuzz;
    bool has_debug_information;
    bool ci;
    bool unity_build;
    bool use_io_ring;
    bool just_preprocessor;
    bool compile;
    bool link;
};

BUSTER_LOCAL OsStringList build_compile_link_arguments(Arena* arena, CompileLinkOptions options)
{
    // Forced to do it so early because we would need another arena here otherwise (since arena is used for the argument builder)
    OsString march_os_string = options.target->march_string;

    let builder = argument_builder_start(arena, clang_path);
    argument_add(builder, OsS("-ferror-limit=1"));
    if (options.just_preprocessor)
    {
        argument_add(builder, OsS("-E"));
    }

    argument_add(builder, OsS("-o"));
    argument_add(builder, options.destination_path);
    for (u64 i = 0; i < options.source_count; i += 1)
    {
        argument_add(builder, options.source_paths[i]);
    }

    bool sanitize = BUSTER_SELECT(options.ci, options.has_debug_information & ((!options.optimize) | options.fuzz), options.fuzz);
    if (sanitize)
    {
        if (!(options.target->pointer->cpu.arch == CPU_ARCH_AARCH64 && options.target->pointer->os == OPERATING_SYSTEM_WINDOWS))
        {
            argument_add(builder, OsS("-fsanitize=address"));
        }

        argument_add(builder, OsS("-fsanitize=undefined"));
        argument_add(builder, OsS("-fsanitize=bounds"));
        argument_add(builder, OsS("-fsanitize-recover=all"));

        if (options.fuzz)
        {
            argument_add(builder, OsS("-fsanitize=fuzzer"));
        }
    }

    if (options.has_debug_information)
    {
        argument_add(builder, OsS("-g"));
    }

    if (xc_sdk_path.pointer)
    {
        argument_add(builder, OsS("-isysroot"));
        argument_add(builder, xc_sdk_path);
    }

    if (options.compile)
    {
        if (!options.link || options.just_preprocessor)
        {
            argument_add(builder, OsS("-c"));
        }

        argument_add(builder, OsS("-Isrc"));

        argument_add(builder, OsS("-std=gnu2x"));

        if (!options.just_preprocessor)
        {
            argument_add(builder, OsS("-Werror"));
        }
        argument_add(builder, OsS("-Wall"));
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

        argument_add(builder, OsS("-DBUSTER_CLANG_ABSOLUTE_PATH=" OS_STRING_DOUBLE_QUOTE BUSTER_CLANG_ABSOLUTE_PATH OS_STRING_DOUBLE_QUOTE));
        argument_add(builder, OsS("-DBUSTER_TOOLCHAIN_ABSOLUTE_PATH=" OS_STRING_DOUBLE_QUOTE BUSTER_TOOLCHAIN_ABSOLUTE_PATH OS_STRING_DOUBLE_QUOTE));
        argument_add(builder, options.unity_build ? OsS("-DBUSTER_UNITY_BUILD=1") : OsS("-DBUSTER_UNITY_BUILD=0"));
        argument_add(builder, options.fuzz ? OsS("-DBUSTER_FUZZING=1") : OsS("-DBUSTER_FUZZING=0"));
        argument_add(builder, options.use_io_ring ? OsS("-DBUSTER_USE_IO_RING=1") : OsS("-DBUSTER_USE_IO_RING=0"));

        argument_add(builder, march_os_string);

        if (options.optimize)
        {
            argument_add(builder, OsS("-O2"));
        }
        else
        {
            argument_add(builder, OsS("-O0"));
        }
    }

    if (!options.just_preprocessor && options.link)
    {
        argument_add(builder, OsS("-fuse-ld=lld"));

        if (options.use_io_ring)
        {
            argument_add(builder, OsS("-luring"));
        }

        if (options.target->pointer->os == OPERATING_SYSTEM_WINDOWS)
        {
            argument_add(builder, OsS("-lws2_32"));
        }
    }

    return argument_builder_end(builder);
}

BUSTER_LOCAL BuildTarget build_target_native = {
    .pointer = &target_native,
};

BUSTER_IMPL ProcessResult thread_entry_point()
{
    let general_arena = arena_create((ArenaInitialization){});
    let cwd = path_absolute(general_arena, OsS("."));

#if defined(BUSTER_XC_SDK_PATH)
    xc_sdk_path = OsS(BUSTER_XC_SDK_PATH);
#endif
    clang_path = OsS(BUSTER_CLANG_ABSOLUTE_PATH);
    toolchain_path = OsS(BUSTER_TOOLCHAIN_ABSOLUTE_PATH);

    LinkModule builder_modules[] = {
        { MODULE_BUILDER },
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { MODULE_MD5 },
        { native_module },
        { MODULE_TARGET }
    };
    LinkModule cc_modules[] = {
        { MODULE_CC_MAIN },
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { MODULE_IR },
        { MODULE_CODEGEN },
        { MODULE_LINK },
        { MODULE_LINK_JIT },
        { MODULE_LINK_ELF },
        { native_module },
        { MODULE_TARGET }
    };
    LinkModule asm_modules[] = {
        { MODULE_ASM_MAIN },
        { MODULE_LIB },
        { MODULE_SYSTEM_HEADERS },
        { MODULE_ENTRY_POINT },
        { native_module },
        { MODULE_TARGET },
    };

    LinkUnitSpecification specifications[] = {
        LINK_UNIT(builder),
        LINK_UNIT(cc),
        LINK_UNIT(asm),
    };

    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);
    BuildTarget* target_buffer[link_unit_count];
    u64 target_count = 0;

    for (u64 i = 0; i < link_unit_count; i += 1)
    {
        let link_unit = &specifications[i];
        let target = &build_target_native;
        link_unit->target = target;
        link_unit->optimize = build_flag_get(BUILD_FLAG_OPTIMIZE);
        link_unit->has_debug_information = build_flag_get(BUILD_FLAG_HAS_DEBUG_INFORMATION);
        link_unit->fuzz = build_flag_get(BUILD_FLAG_FUZZ);
        link_unit->is_builder = os_string_equal(link_unit->name, OsS("builder"));

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

            let march = target->pointer->cpu.arch == CPU_ARCH_X86_64 ? OsS("-march=") : OsS("-mcpu=");
            let target_strings = target_to_split_os_string(*target->pointer);
            OsString march_parts[] = {
                march,
                target_strings.s[TARGET_CPU_MODEL],
            };
            target->march_string = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, march_parts), true);

            OsString triple_parts[2 * TARGET_STRING_COMPONENT_COUNT - 1];
            for (u64 i = 0; i < TARGET_STRING_COMPONENT_COUNT; i += 1)
            {
                triple_parts[i * 2 + 0] = target_strings.s[i];
                if (i < (TARGET_STRING_COMPONENT_COUNT - 1))
                {
                    triple_parts[i * 2 + 1] = OsS("-");
                }
            }

            let target_triple = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, triple_parts), true);
            target->string = target_triple;

            OsString directory_path_parts[] = {
                cwd,
                OsS("/build/"),
                target_triple,
            };
            let directory = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, directory_path_parts), true);
            target->directory_path = directory;
            os_make_directory(directory);
            target_count += 1;
        }

        OsString artifact_path_parts[] = {
            cwd,
            OsS("/build/"),
            link_unit->is_builder ? OsS("") : target->string,
            link_unit->is_builder ? OsS("") : OsS("/"),
            link_unit->name,
            target->pointer->os == OPERATING_SYSTEM_WINDOWS ? OsS(".exe") : OsS(""),
        };

        let artifact_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, artifact_path_parts), true);
        link_unit->artifact_path = artifact_path;
    }

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

    if (!build_flag_get(BUILD_FLAG_UNITY_BUILD))
    {
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
    }

#if defined(_WIN32)
#if defined(__x86_64__)
    let dll_filename = 
#if defined(__x86_64__)
        OsS("clang_rt.asan_dynamic-x86_64.dll");
#elif defined(__aarch64__)
        OsS("clang_rt.asan_dynamic-aarch64.dll");
#else
#pragma error
#endif
    OsString original_dll_parts[] = {
        toolchain_path,
        OsS("/lib/clang/21/lib/windows/"),
        dll_filename,
    };
    let original_asan_dll = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, original_dll_parts), true);
    let target_strings = target_to_split_os_string(target_native);
    OsString target_native_dir_path_parts[] = {
        OsS("build/"),
        target_strings.s[0],
        OsS("-"),
        target_strings.s[1],
        OsS("-"),
        target_strings.s[2],
    };
    let target_native_dir_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, target_native_dir_path_parts), true);
    os_make_directory(target_native_dir_path);
    OsString destination_dll_parts[] = {
        target_native_dir_path,
        OsS("/"),
        dll_filename,
    };
    let destination_asan_dll = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, destination_dll_parts), true);
    copy_file((CopyFileArguments){ .original_path = original_asan_dll, .new_path = destination_asan_dll });
#endif
#endif

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
                    .has_debug_information = link_unit->has_debug_information,
                    .optimize = link_unit->optimize,
                    .fuzz = link_unit->fuzz,
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
                        .has_debug_information = link_unit->has_debug_information,
                        .optimize = link_unit->optimize,
                        .fuzz = link_unit->fuzz,
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
        if (!build_flag_get(BUILD_FLAG_UNITY_BUILD))
        {
            let fd = os_file_open(string_slice_start(source_file->full_path, cwd.length + 1), (OpenFlags){ .read = 1 }, (OpenPermissions){});
            let stats = os_file_get_stats(fd, (FileStatsOptions){ .raw = UINT64_MAX });
            let buffer = arena_allocate_bytes(general_arena, stats.size, 64);
            String8 buffer_slice = { buffer, stats.size};
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
            };
        }
    }

    ProcessResult result = PROCESS_RESULT_SUCCESS;

    for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
    {
        let unit = &compilation_units[unit_i];

        let source_absolute_path = unit->source_path;
        let source_relative_path = string_slice_start(source_absolute_path, cwd.length + 1);
        let target_directory_path = unit->target->directory_path;
        OsString object_absolute_path_parts[] = {
            target_directory_path,
            OsS("/"),
            source_relative_path,
#if _WIN32
            OsS(".obj"),
#else
            OsS(".o"),
#endif
        };

        let object_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, object_absolute_path_parts), true);
        unit->object_path = object_path;

        OsChar buffer[max_path_length];
        let os_char_size = sizeof(buffer[0]);
        let copy_character_count = target_directory_path.length;
        memcpy(buffer, target_directory_path.pointer, (copy_character_count + 1) * os_char_size);
        buffer[copy_character_count] = 0;

        u64 buffer_i = copy_character_count;
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
    }

    if (!build_flag_get(BUILD_FLAG_UNITY_BUILD))
    {
        let compile_commands = arena_create((ArenaInitialization){});
        let compile_commands_start = compile_commands->position;
        append_string8(compile_commands, S8("[\n"));

        for (u64 unit_i = 0; unit_i < compilation_unit_count; unit_i += 1)
        {
            let unit = &compilation_units[unit_i];
            let source_absolute_path = unit->source_path;
            let object_path = unit->object_path;

            CompileLinkOptions options = {
                .destination_path = object_path,
                .source_paths = &unit->source_path,
                .source_count = 1,
                .target = unit->target,
                .optimize = unit->optimize,
                .fuzz = unit->fuzz,
                .has_debug_information = unit->has_debug_information,
                .ci = build_flag_get(BUILD_FLAG_CI),
                .unity_build = build_flag_get(BUILD_FLAG_UNITY_BUILD),
                .use_io_ring = unit->use_io_ring,
                .just_preprocessor = build_flag_get(BUILD_FLAG_JUST_PREPROCESSOR),
                .compile = 1,
                .link = build_flag_get(BUILD_FLAG_UNITY_BUILD),
            };
            let args = build_compile_link_arguments(general_arena, options);

            unit->compiler = (OsChar*)clang_path.pointer;
            unit->compilation_arguments = args;

            append_string8(compile_commands, S8("\t{\n\t\t\"directory\": \""));
            append_string8(compile_commands, os_string_to_string8(general_arena, cwd));
            append_string8(compile_commands, S8("\",\n\t\t\"command\": \""));

            let arg_it = os_string_list_initialize(args);
            for (let arg = os_string_list_next(&arg_it); arg.pointer; arg = os_string_list_next(&arg_it))
            {
                let a = arg;
#ifndef _WIN32
                let double_quote_count = string8_occurrence_count(a, '"');
                if (double_quote_count != 0)
                {
                    let new_length = a.length + double_quote_count;
                    a = (String8) { .pointer = arena_allocate(compile_commands, u8, new_length), .length = new_length };
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
            append_string8(compile_commands, os_string_to_string8(general_arena, source_absolute_path));
            append_string8(compile_commands, S8("\"\n"));
            append_string8(compile_commands, S8("\t},\n"));
        }

        compile_commands->position -= 2;

        append_string8(compile_commands, S8("\n]"));

        let compile_commands_str = (String8){ .pointer = (u8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };
        result = file_write(OsS("build/compile_commands.json"), compile_commands_str) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }

    if (result == PROCESS_RESULT_SUCCESS)
    {
        let selected_compilation_count = compilation_unit_count;
        let selected_compilation_units = compilation_units;

        if (!build_flag_get(BUILD_FLAG_UNITY_BUILD))
        {
            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                unit->process.handle = os_process_spawn(unit->compiler, unit->compilation_arguments, program_state->input.envp);
            }

            for (u64 unit_i = 0; unit_i < selected_compilation_count; unit_i += 1)
            {
                let unit = &selected_compilation_units[unit_i];
                let unit_compilation_result = os_process_wait_sync(unit->process.handle, unit->process.resources);
                if (unit_compilation_result != PROCESS_RESULT_SUCCESS)
                {
                    print(S8("Some of the compilations failed\n"));
                    result = PROCESS_RESULT_FAILED;
                }
            }
        }

        u64 link_unit_start = 1;
        // TODO: depend more-fine grainedly, ie: link those objects which succeeded compiling instead of all or nothing
        if (result == PROCESS_RESULT_SUCCESS)
        {
            ProcessHandle* processes[link_unit_count];

            for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let link_unit_specification = &specifications[link_unit_i];
                let link_modules = link_unit_specification->modules;

                let source_paths = (OsString*)align_forward((u64)((u8*)general_arena + general_arena->position), alignof(OsString));
                u64 source_path_count = 0;

                for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
                {
                    let module = &link_modules.pointer[module_i];

                    if (!modules[module->id].no_source)
                    {
                        let unit = &compilation_units[module->index];
                        let object_path = arena_allocate(general_arena, OsString, 1);
                        *object_path = (build_flag_get(BUILD_FLAG_JUST_PREPROCESSOR) | build_flag_get(BUILD_FLAG_UNITY_BUILD)) ? unit->source_path : unit->object_path;
                        source_path_count += 1;
                    }
                }

                CompileLinkOptions options = {
                    .destination_path = link_unit_specification->artifact_path,
                    .source_paths = source_paths,
                    .source_count = source_path_count,
                    .target = link_unit_specification->target,
                    .optimize = link_unit_specification->optimize,
                    .fuzz = link_unit_specification->fuzz,
                    .has_debug_information = link_unit_specification->has_debug_information,
                    .ci = build_flag_get(BUILD_FLAG_CI),
                    .unity_build = build_flag_get(BUILD_FLAG_UNITY_BUILD),
                    .use_io_ring = link_unit_specification->use_io_ring,
                    .just_preprocessor = build_flag_get(BUILD_FLAG_JUST_PREPROCESSOR),
                    .compile = build_flag_get(BUILD_FLAG_UNITY_BUILD),
                    .link = 1,
                };
                let link_arguments = build_compile_link_arguments(general_arena, options);
                let process = os_process_spawn((OsChar*)clang_path.pointer, link_arguments, program_state->input.envp);
                processes[link_unit_i] = process;
            }

            for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let process = processes[link_unit_i];
                ProcessResources resources = {};
                let link_result = os_process_wait_sync(process, resources);
                if (link_result != PROCESS_RESULT_SUCCESS)
                {
                    print(S8("Some of the linkage failed\n"));
                    result = link_result;
                }
            }
        }

        if (!build_flag_get(BUILD_FLAG_JUST_PREPROCESSOR) && result == PROCESS_RESULT_SUCCESS)
        {
            switch (build_program_state.command)
            {
                break; case BUILD_COMMAND_COUNT: BUSTER_UNREACHABLE();
                break; case BUILD_COMMAND_BUILD: {}
                break; case BUILD_COMMAND_TEST:
                {
                    ProcessHandle* processes[link_unit_count];

                    TestArguments arguments = {
                        .arena = general_arena,
                    };
                    if (!builder_tests(&arguments))
                    {
                        print(S8("Build tests failed!\n"));
                        result = PROCESS_RESULT_FAILED;
                    }

                    // Skip builder executable since we execute the tests ourselves
                    for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let link_unit_specification = &specifications[link_unit_i];

                        if (link_unit_specification->fuzz)
                        {
#if defined(_WIN32)
                            OsString argv_parts[] = {
                                link_unit_specification->artifact_path,
                                OsS(" -max_len=4096"),
                                OsS(" -max_total_time=30"),
                            };
                            let argv_os_string = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, argv_parts), true);
                            let argv = argv_os_string.pointer;
                            let first_arg = argv_parts[0].pointer;
#else
                            OsChar* argv[] = {
                                os_string_to_c(link_unit_specification->artifact_path),
                                "-max_len=4096",
                                "-max_total_time=30",
                                0,
                            };
                            let first_arg = argv[0];
#endif
                            processes[link_unit_i] = os_process_spawn(first_arg, argv, program_state->input.envp);
                        }
                        else
                        {
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
                    }

                    for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let process = processes[link_unit_i];
                        ProcessResources resources = {};
                        let test_result = os_process_wait_sync(process, resources);
                        if (test_result != PROCESS_RESULT_SUCCESS)
                        {
                            print(S8("Some of the unit tests failed\n"));
                            result = test_result;
                        }
                    }
                }
                break; case BUILD_COMMAND_DEBUG: {}
            }
        }
    }
    else
    {
        print(S8("Error writing compile commands: {OsS}"), get_last_error_message(general_arena));
    }

    return result;
}
