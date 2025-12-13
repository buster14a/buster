#if 0
#!/usr/bin/env bash
set -eu

if [[ -z "${BUSTER_CI:-}" ]]; then
    BUSTER_CI=0
fi

if [[ -z "${BUSTER_OPTIMIZE:-}" ]]; then
    BUSTER_OPTIMIZE=0
fi

if [[ -z "${BUSTER_LLVM_VERSION:-}" ]]; then
    BUSTER_LLVM_VERSION=21.1.7
fi

if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
    if [[ "$BUSTER_CI" == "1" ]]; then
        source ./setup_ci.sh
    else
        export BUSTER_ARCH=x86_64
        export BUSTER_OS=linux
        export CMAKE_PREFIX_PATH=$HOME/dev/toolchain/install/llvm_${BUSTER_LLVM_VERSION}_${BUSTER_ARCH}-${BUSTER_OS}-Release
    fi
fi

if [[ -z "${CLANG:-}" ]]; then
    export CLANG=$CMAKE_PREFIX_PATH/bin/clang
fi

if [[ "$#" != "0" ]]; then
    set -x
fi

XC_SDK_PATH=""
CLANG_EXTRA_FLAGS=""
if [[ "$BUSTER_OS" == "macos" ]]; then
    export XC_SDK_PATH=$(xcrun --sdk macosx --show-sdk-path)
    CLANG_EXTRA_FLAGS="-isysroot $XC_SDK_PATH"
fi

#if 0BUSTER_REGENERATE=0 build/builder $@ 2>/dev/null
#endif
#if [[ "$?" != "0" && "$?" != "333" ]]; then
    mkdir -p build
    $CLANG build.c -o build/builder -fuse-ld=lld $CLANG_EXTRA_FLAGS -Isrc -std=gnu2x -march=native -DBUSTER_UNITY_BUILD=1 -DBUSTER_USE_IO_RING=0 -DBUSTER_USE_PTHREAD=1 -g -Werror -Wall -Wextra -Wpedantic -pedantic -Wno-language-extension-token -Wno-gnu-auto-type -Wno-gnu-empty-struct -Wno-bitwise-instead-of-logical -Wno-unused-function -Wno-gnu-flexible-array-initializer -Wno-missing-field-initializers -Wno-pragma-once-outside-header -pthread -fwrapv -fno-strict-aliasing -funsigned-char -ferror-limit=1 #-ftime-trace -ftime-trace-verbose
    if [[ "$?" == "0" ]]; then
        BUSTER_REGENERATE=1 build/builder $@
        # BUSTER_REGENERATE=1 lldb -b -o run -o 'bt all' -- build/builder $@
    fi
#endif fi
exit $?
#endif

#pragma once

#define BUSTER_USE_PADDING 0

#include <buster/lib.h>

#if BUSTER_UNITY_BUILD
#include <buster/lib.c>
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
    MODULE_BUILDER,
    MODULE_MD5,
    MODULE_CC_MAIN,
    MODULE_ASM_MAIN,
    MODULE_COUNT,
);

ENUM(DirectoryId,
    DIRECTORY_SRC_BUSTER,
    DIRECTORY_SRC_MARTINS,
    DIRECTORY_ROOT,
    DIRECTORY_CC,
    DIRECTORY_ASM,
    DIRECTORY_COUNT,
);

ENUM(CpuArch,
    CPU_ARCH_X86_64,
    CPU_ARCH_AARCH64,
);

ENUM(CpuModel,
    CPU_MODEL_GENERIC,
    CPU_MODEL_NATIVE,
);

ENUM(OperatingSystem,
    OPERATING_SYSTEM_LINUX,
    OPERATING_SYSTEM_MACOS,
    OPERATING_SYSTEM_WINDOWS,
    OPERATING_SYSTEM_UEFI,
    OPERATING_SYSTEM_ANDROID,
    OPERATING_SYSTEM_IOS,
    OPERATING_SYSTEM_FREESTANDING,
);

STRUCT(Target)
{
    CpuArch arch;
    CpuModel model;
    OperatingSystem os;
};

BUSTER_LOCAL constexpr Target target_native = {
#if defined(__x86_64__)
    .arch = CPU_ARCH_X86_64,
#elif defined(__aarch64__)
    .arch = CPU_ARCH_AARCH64,
#else
#pragma error
#endif
#if defined(__linux__)
    .os = OPERATING_SYSTEM_LINUX,
#elif defined(_WIN32)
    .os = OPERATING_SYSTEM_WINDOWS,
#elif defined(__APPLE__)
#define BUSTER_APPLE 1
#include <TargetConditionals.h>
#if TARGET_OS_MAC == 1
    .os = OPERATING_SYSTEM_MACOS,
#elif (TARGET_OS_IPHONE == 1) || (TARGET_IPHONE_SIMULATOR == 1)
    .os = OPERATING_SYSTEM_IOS
#else
#pragma error
#endif
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
};

static_assert(BUSTER_ARRAY_LENGTH(modules) == MODULE_COUNT);

#define LINK_UNIT_MODULES(_name, ...) BUSTER_LOCAL LinkModule _name ## _modules[] = { __VA_ARGS__ }
#define LINK_UNIT(_name, ...) (LinkUnitSpecification) { .name = OsS(#_name), .modules = { .pointer = _name ## _modules, .length = BUSTER_ARRAY_LENGTH(_name ## _modules) }, __VA_ARGS__ }

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    OsString name;
    ModuleSlice modules;
    OsString artifact_path;
    Target target;
    bool use_io_ring;
};

LINK_UNIT_MODULES(builder, { MODULE_LIB }, { MODULE_SYSTEM_HEADERS }, { MODULE_ENTRY_POINT }, { MODULE_BUILDER }, { MODULE_MD5 });
LINK_UNIT_MODULES(cc, { MODULE_LIB }, { MODULE_SYSTEM_HEADERS }, { MODULE_ENTRY_POINT }, { MODULE_CC_MAIN }, );
LINK_UNIT_MODULES(asm, { MODULE_LIB }, { MODULE_SYSTEM_HEADERS }, { MODULE_ENTRY_POINT }, { MODULE_ASM_MAIN }, );

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

BUSTER_LOCAL OsString target_to_string_builder(Target target)
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
                        break; case OPERATING_SYSTEM_LINUX: return OsS("x86_64-linux-baseline");
                        break; case OPERATING_SYSTEM_MACOS: return OsS("x86_64-macos-baseline");
                        break; case OPERATING_SYSTEM_WINDOWS: return OsS("x86_64-windows-baseline");
                        break; case OPERATING_SYSTEM_UEFI: return OsS("x86_64-uefi-baseline");
                        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("x86_64-freestanding-baseline");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; case CPU_MODEL_NATIVE:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return OsS("x86_64-linux-native");
                        break; case OPERATING_SYSTEM_MACOS: return OsS("x86_64-macos-native");
                        break; case OPERATING_SYSTEM_WINDOWS: return OsS("x86_64-windows-native");
                        break; case OPERATING_SYSTEM_UEFI: return OsS("x86_64-uefi-native");
                        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("x86_64-freestanding-native");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; default: BUSTER_UNREACHABLE();
            }
        }
        break; case CPU_ARCH_AARCH64:
        {
            switch (target.model)
            {
                break; case CPU_MODEL_GENERIC:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return OsS("aarch64-linux-baseline");
                        break; case OPERATING_SYSTEM_MACOS: return OsS("aarch64-macos-baseline");
                        break; case OPERATING_SYSTEM_WINDOWS: return OsS("aarch64-windows-baseline");
                        break; case OPERATING_SYSTEM_UEFI: return OsS("aarch64-uefi-baseline");
                        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("aarch64-freestanding-baseline");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; case CPU_MODEL_NATIVE:
                {
                    switch (target.os)
                    {
                        break; case OPERATING_SYSTEM_LINUX: return OsS("aarch64-linux-native");
                        break; case OPERATING_SYSTEM_MACOS: return OsS("aarch64-macos-native");
                        break; case OPERATING_SYSTEM_WINDOWS: return OsS("aarch64-windows-native");
                        break; case OPERATING_SYSTEM_UEFI: return OsS("aarch64-uefi-native");
                        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("aarch64-freestanding-native");
                        break; default: BUSTER_UNREACHABLE();
                    }
                }
                break; default: BUSTER_UNREACHABLE();
            }
        }
        break; default: BUSTER_UNREACHABLE();
    }
}

BUSTER_LOCAL OsString arch_to_string(CpuArch arch)
{
    switch (arch)
    {
        break; case CPU_ARCH_X86_64: return OsS("x86_64");
        break; case CPU_ARCH_AARCH64: return OsS("aarch64");
        break; default: return OsS("");
    }
}

BUSTER_LOCAL OsString os_to_string(OperatingSystem os)
{
    switch (os)
    {
        break; case OPERATING_SYSTEM_LINUX: return OsS("linux");
        break; case OPERATING_SYSTEM_MACOS: return OsS("macos");
        break; case OPERATING_SYSTEM_WINDOWS: return OsS("windows");
        break; case OPERATING_SYSTEM_UEFI: return OsS("uefi");
        break; case OPERATING_SYSTEM_ANDROID: return OsS("android");
        break; case OPERATING_SYSTEM_IOS: return OsS("ios");
        break; case OPERATING_SYSTEM_FREESTANDING: return OsS("freestanding");
        break; default: return OsS("");
    }
}

STRUCT(CompilationUnit)
{
    Target target;
    CompilationModel model;
    OsChar* compiler;
    OsStringList compilation_arguments;
    bool has_debug_info;
    bool use_io_ring;
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

    for (u64 unit_i = 0; unit_i < unit_count; unit_i += 1)
    {
        let unit = &units[unit_i];

        let source_absolute_path = unit->source_path;
        let source_relative_path = string_slice_start(source_absolute_path, cwd.length + 1);
        let target_string_builder = target_to_string_builder(unit->target);
        OsString object_absolute_path_parts[] = {
            cwd,
            OsS("/build/"),
            target_string_builder,
            OsS("/"),
            source_relative_path,
            OsS(".o"),
        };

        let object_path = arena_join_os_string(arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, object_absolute_path_parts), true);
        unit->object_path = object_path;

        u64 target_i;
        for (target_i = 0; target_i < target_count; target_i += 1)
        {
            if (target_equal(unit->target, targets[target_i]))
            {
                break;
            }
        }

        OsChar buffer[4096];
        u64 buffer_i = 0;
        let os_char_size = sizeof(buffer[0]);

        for (u64 i = 1; i < 3; i += 1)
        {
            memcpy(buffer + buffer_i, object_absolute_path_parts[i].pointer + (i == 1), string_size(object_absolute_path_parts[i]) - (((i == 1) * os_char_size)));
            buffer_i += object_absolute_path_parts[i].length - (i == 1);
            if (i == 1)
            {
                BUSTER_CHECK(object_absolute_path_parts[i].pointer[0] == '/');
            }
        }

        buffer[buffer_i] = 0;

        if (target_i == target_count)
        {
            os_make_directory(os_string_from_pointer_length(buffer, buffer_i));
            targets[target_count] = unit->target;
            target_count += 1;
        }

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

            os_make_directory(os_string_from_pointer_length(buffer, length));

            source_i += byte_count; 
        }

        let builder = argument_builder_start(arena, clang_path);
        argument_add(builder, OsS("-ferror-limit=1"));
        argument_add(builder, OsS("-c"));
        argument_add(builder, source_absolute_path);
        argument_add(builder, OsS("-o"));
        argument_add(builder, object_path);
        argument_add(builder, OsS("-std=gnu2x"));

        if (unit->target.os == OPERATING_SYSTEM_WINDOWS)
        {
            argument_add(builder, OsS("-nostdlib"));
        }

        if (xc_sdk_path.pointer)
        {
            argument_add(builder, OsS("-isysroot"));
            argument_add(builder, xc_sdk_path);
        }

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

        switch (unit->target.model)
        {
            break; case CPU_MODEL_NATIVE: argument_add(builder, OsS("-march=native"));
            break; case CPU_MODEL_GENERIC: {}
        }

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

#if defined(_WIN32)
        let length = string16_length(args);
        OsString arg_strings = { args, length };
        let arg_it = arg_strings;
        bool is_space;
        do
        {
            let space = string16_first_character(arg_it, ' ');
            is_space = space != string_no_match;
            let end = is_space ? (space + 1) : arg_it.length;
            let arg_chunk = (OsString){ arg_it.pointer, end };
            append_string16(compile_commands, arg_chunk);
            arg_it.pointer += end;
            arg_it.length -= end;
        } while (is_space);
#else
        for (OsChar** a = args; *a; a += 1)
        {
            let arg_ptr = *a;
            let arg_len = strlen((char*)arg_ptr);
            let arg = (String8){ (u8*)arg_ptr, arg_len };
            append_string8(compile_commands, arg);
            append_string8(compile_commands, S8(" "));
        }
#endif

        compile_commands->position -= 1;
        append_string8(compile_commands, S8("\",\n\t\t\"file\": \""));
        append_string8(compile_commands, os_string_to_string8(arena, source_absolute_path));
        append_string8(compile_commands, S8("\"\n"));
        append_string8(compile_commands, S8("\t},\n"));
    }

    compile_commands->position -= 2;

    append_string8(compile_commands, S8("\n]"));

    let compile_commands_str = (String8){ .pointer = (u8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };

    if (result)
    {
        result = file_write(OsS("build/compile_commands.json"), compile_commands_str);
        if (!result)
        {
            print(S8(""), get_last_error_message());
        }
    }

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
        os_string_list_next(arg_it);
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

BUSTER_IMPL ProcessResult thread_entry_point()
{
#if defined(__APPLE__)
    xc_sdk_path = os_string_from_pointer(getenv("XC_SDK_PATH"));
#endif
    LinkUnitSpecification specifications[] = {
        LINK_UNIT(builder, .target = target_native),
        LINK_UNIT(cc, .target = target_native),
        LINK_UNIT(asm, .target = target_native),
    };
    constexpr u64 link_unit_count = BUSTER_ARRAY_LENGTH(specifications);

    OsString directory_paths[] = {
        [DIRECTORY_ROOT] = OsS(""),
        [DIRECTORY_SRC_BUSTER] = OsS("src/buster"),
        [DIRECTORY_CC] = OsS("src/buster/compiler/frontend/cc"),
        [DIRECTORY_ASM] = OsS("src/buster/compiler/frontend/asm"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == DIRECTORY_COUNT);

    OsString module_names[] = {
        [MODULE_LIB] = OsS("lib"),
        [MODULE_SYSTEM_HEADERS] = OsS("system_headers"),
        [MODULE_ENTRY_POINT] = OsS("entry_point"),
        [MODULE_BUILDER] = OsS("build"),
        [MODULE_MD5] = OsS("md5"),
        [MODULE_CC_MAIN] = OsS("cc_main"),
        [MODULE_ASM_MAIN] = OsS("asm_main"),
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
            arch_to_string(target_native.arch),
            OsS("-"),
            os_to_string(target_native.os),
            OsS("-Release"),
            OsS("/bin/clang.exe"),
        };
        clang_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, clang_path_parts), true);
    }

    ProcessResult result = {};

    if (build_compile_commands(general_arena, compile_commands, compilation_units, compilation_unit_count, cwd, clang_path))
    {
        let selected_compilation_count = compilation_unit_count;
        let selected_compilation_units = compilation_units;

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

                argument_add(builder, OsS("-fuse-ld=lld"));
                argument_add(builder, OsS("-o"));

                bool is_builder = link_unit_i == 0; // str_equal(link_unit_specification->name, S("builder"));

                OsString artifact_path_parts[] = {
                    OsS("build/"),
                    is_builder ? OsS("") : target_to_string_builder(link_unit_specification->target),
                    is_builder ? OsS("") : OsS("/"),
                    link_unit_specification->name,
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


                if (link_unit_specification->target.os == OPERATING_SYSTEM_WINDOWS)
                {
                    argument_add(builder, OsS("-nostdlib"));
                    argument_add(builder, OsS("-lkernel32"));
                    argument_add(builder, OsS("-lws2_32"));
                    argument_add(builder, OsS("-Wl,-entry:mainCRTStartup"));
                    argument_add(builder, OsS("-Wl,-subsystem:console"));
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
