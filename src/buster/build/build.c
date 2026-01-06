#if 0
#!/usr/bin/env bash
source build.sh
#endif

#pragma once

#define BUSTER_USE_PADDING 0

#include <buster/base.h>
#include <buster/target.h>
#include <buster/entry_point.h>
// #include <martins/md5.h>
#include <buster/system_headers.h>
#include <buster/string_os.h>
#include <buster/path.h>
#include <buster/file.h>
#include <buster/build/compiler_options.h>

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
#include <buster/integer.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#if defined(__aarch64__)
#include <buster/aarch64.c>
#endif
#include <buster/entry_point.c>
#include <buster/path.c>
#include <buster/file.c>
#endif

STRUCT(BuildTarget)
{
    Target* pointer;
    StringOs string;
    StringOs march_string;
    StringOs directory_path;
};

STRUCT(LLVMVersion)
{
    u8 major;
    u8 minor;
    u8 revision;
    StringOs string;
};

BUSTER_LOCAL __attribute__((used)) StringOs toolchain_path = {};
BUSTER_LOCAL StringOs clang_path = {};
BUSTER_LOCAL StringOs xc_sdk_path = {};

#define BUSTER_TODO() print(S8("TODO\n")); fail()

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
    u64 reserved:60;
};

STRUCT(ModuleInstantiation)
{
    BuildTarget* target;
    u64 index;
    ModuleId id;
    u8 reserved[7];
};

STRUCT(LinkModule)
{
    u64 index;
    ModuleId id;
    u8 reserved[7];
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

#define LINK_UNIT(_name, ...) (LinkUnitSpecification) { .name = SOs(#_name), .modules = { .pointer = _name ## _modules, .length = build_flag_get(BUILD_FLAG_UNITY_BUILD) ? 1 : BUSTER_ARRAY_LENGTH(_name ## _modules) }, __VA_ARGS__ }

// TODO: better naming convention
STRUCT(LinkUnitSpecification)
{
    StringOs name;
    ModuleSlice modules;
    StringOs artifact_path;
    BuildTarget* target;
    StringOs* object_paths;
    u64 use_io_ring:1;
    u64 has_debug_information:1;
    u64 optimize:1;
    u64 fuzz:1;
    u64 is_builder:1;
    u64 reserved:59;
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

ENUM(BuildFlag,
    BUILD_FLAG_OPTIMIZE,
    BUILD_FLAG_FUZZ,
    BUILD_FLAG_CI,
    BUILD_FLAG_HAS_DEBUG_INFORMATION,
    BUILD_FLAG_UNITY_BUILD,
    BUILD_FLAG_JUST_PREPROCESSOR,
    BUILD_FLAG_SELF_HOSTED,
    BUILD_FLAG_COUNT,
);

ENUM(BuildIntegerOption,
    BUILD_INTEGER_OPTION_FUZZ_DURATION_SECONDS,
    BUILD_INTEGER_OPTION_COUNT,
);

UNION(BuildIntegerOptionValue)
{
    s64 signed_value;
    u64 unsigned_value;
};

ENUM_T(BuildIntegerOptionFormat, u8,
    BUILD_INTEGER_OPTION_FORMAT_HEXADECIMAL,
    BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE,
    BUILD_INTEGER_OPTION_FORMAT_DECIMAL_NEGATIVE,
    BUILD_INTEGER_OPTION_FORMAT_OCTAL,
    BUILD_INTEGER_OPTION_FORMAT_BINARY,
);

STRUCT(BuildIntegerOptionSettings)
{
    BuildIntegerOptionFormat format:3;
    u8 is_set:1;
    u8 reserved:4;
};
static_assert(sizeof(BuildIntegerOptionSettings) == 1);

STRUCT(BuildIntegerOptions)
{
    BuildIntegerOptionValue values[BUILD_INTEGER_OPTION_COUNT];
    BuildIntegerOptionSettings settings[BUILD_INTEGER_OPTION_COUNT];
    u8 reserved[7];
};

typedef u64 FlagBackingType;

STRUCT(BuildProgramState)
{
    ProgramState general_state;
    FlagBackingType value_flags;
    FlagBackingType set_flags;
    BuildIntegerOptions integer;
    BuildCommand command;
    u8 reserved[4];
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

BUSTER_LOCAL u64 build_integer_option_get_unsigned(BuildIntegerOption option_id)
{
    let option_value = build_program_state.integer.values[option_id];
    return option_value.unsigned_value;
}

BUSTER_LOCAL u128 hash_file(u8* pointer, u64 length)
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

STRUCT(Process)
{
    ProcessResources resources;
    ProcessHandle* handle;
    StringOsList argv;
    StringOsList envp;
    u64 waited:1;
    u64 reserved:63;
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
    StringOs compiler;
    StringOsList compilation_arguments;
    StringOs object_path;
    StringOs source_path;
    Process process;
    u64 optimize:1;
    u64 has_debug_information:1;
    u64 fuzz:1;
    u64 use_io_ring:1;
    u64 include_tests:1;
    u64 reserved:59;
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
    string8_duplicate_arena(arena, s, false);
}

#if defined(_WIN32)
BUSTER_LOCAL void append_string16(Arena* arena, String16 s)
{
    string16_to_string8(arena, s);
}

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
    bool is_command_present = false;

    {
        let arg_it = os_string_list_iterator_initialize(argv);
        let first_argument = os_string_list_iterator_next(&arg_it);
        BUSTER_UNUSED(first_argument);
        let command = os_string_list_iterator_next(&arg_it);
        u64 argument_count = 1;

        if (command.pointer)
        {
            if (os_string_starts_with_sequence(command, SOs("--")))
            {
                arg_it = os_string_list_iterator_initialize(argv);
                os_string_list_iterator_next(&arg_it);
            }
            else
            {
                StringOs possible_commands[] = {
                    [BUILD_COMMAND_BUILD] = SOs("build"),
                    [BUILD_COMMAND_TEST] = SOs("test"),
                    [BUILD_COMMAND_DEBUG] = SOs("debug"),
                };
                static_assert(BUSTER_ARRAY_LENGTH(possible_commands) == BUILD_COMMAND_COUNT);

                u64 possible_command_count = BUSTER_ARRAY_LENGTH(possible_commands);

                u64 i;
                for (i = 0; i < possible_command_count; i += 1)
                {
                    if (string_equal(command, possible_commands[i]))
                    {
                        break;
                    }
                }

                if (i != possible_command_count)
                {
                    is_command_present = true;
                    build_program_state.command = (BuildCommand)i;
                }
                else
                {
                    string8_print(S8("Command not recognized: {OsS}!\n"), command);
                    result = PROCESS_RESULT_FAILED;
                }

                argument_count += 1;
            }
        }

        StringOs argument;
        while ((argument = os_string_list_iterator_next(&arg_it)).pointer)
        {
            string8_print(S8("Argument: {SOs}\n"), argument);
            StringOs build_flag_strings[] = {
                [BUILD_FLAG_OPTIMIZE] = SOs("--optimize="),
                [BUILD_FLAG_FUZZ] = SOs("--fuzz="),
                [BUILD_FLAG_CI] = SOs("--ci="),
                [BUILD_FLAG_HAS_DEBUG_INFORMATION] = SOs("--has-debug-information="),
                [BUILD_FLAG_UNITY_BUILD] = SOs("--unity-build="),
                [BUILD_FLAG_JUST_PREPROCESSOR] = SOs("--just-preprocessor="),
                [BUILD_FLAG_SELF_HOSTED] = SOs("--self-hosted="),
            };
            static_assert(BUSTER_ARRAY_LENGTH(build_flag_strings) == BUILD_FLAG_COUNT);

            FlagBackingType i;
            for (i = 0; i < BUILD_FLAG_COUNT; i += 1)
            {
                let build_flag_string = build_flag_strings[i];
                if (os_string_starts_with_sequence(argument, build_flag_string))
                {
                    bool is_valid_value = argument.length == build_flag_string.length + 1;

                    if (is_valid_value)
                    {
                        let arg_value = argument.pointer[build_flag_string.length];
                        switch (arg_value)
                        {
                            break; case '1': case '0': build_flag_set((BuildFlag)i, arg_value == '1');
                            break; default:
                            {
                                is_valid_value = false;
                            }
                        }
                    }

                    if (!is_valid_value)
                    {
                        string8_print(S8("For argument '{OsS}', unrecognized value\n"), argument);
                        result = PROCESS_RESULT_FAILED;
                    }

                    break;
                }
            }

            bool found = i != BUILD_FLAG_COUNT;

            if (!found)
            {
                StringOs integer_option_strings[] = {
                    [BUILD_INTEGER_OPTION_FUZZ_DURATION_SECONDS] = SOs("--fuzz-duration="),
                };
                static_assert(BUSTER_ARRAY_LENGTH(integer_option_strings) == BUILD_INTEGER_OPTION_COUNT);

                u64 build_integer_option_i;
                for (build_integer_option_i = 0; build_integer_option_i < BUILD_INTEGER_OPTION_COUNT; build_integer_option_i += 1)
                {
                    let integer_option_string = integer_option_strings[build_integer_option_i];

                    if (os_string_starts_with_sequence(argument, integer_option_string))
                    {
                        let value_string = BUSTER_SLICE_START(integer_option_string, integer_option_string.length);

                        BuildIntegerOptionFormat format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE;

                        // TODO: handle negative numbers
                        IntegerParsingU64 p = {};
                        u64 prefix_character_count = 0;
                        if (value_string.length > 2 && value_string.pointer[0] == '0')
                        {
                            let second_ch = value_string.pointer[1];
                            switch (second_ch)
                            {
                                break; case 'x': format = BUILD_INTEGER_OPTION_FORMAT_HEXADECIMAL; prefix_character_count = 2; p = os_string_parse_u64_hexadecimal(value_string.pointer + 2);
                                break; case 'd': format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE; prefix_character_count = 2; p = os_string_parse_u64_decimal(value_string.pointer + 2);
                                break; case 'o': format = BUILD_INTEGER_OPTION_FORMAT_OCTAL; prefix_character_count = 2; p = os_string_parse_u64_octal(value_string.pointer + 2);
                                break; case 'b': format = BUILD_INTEGER_OPTION_FORMAT_BINARY; prefix_character_count = 2; p = os_string_parse_u64_binary(value_string.pointer + 2);
                                break; default: {}
                            }
                        }
                        else if (value_string.length > 1 && value_string.pointer[0] == '-')
                        {
                            format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_NEGATIVE;
                            prefix_character_count = 1;
                            p = os_string_parse_u64_decimal(value_string.pointer + 1);
                        }

                        if (format == BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE && p.length == 0)
                        {
                            p = os_string_parse_u64_decimal(value_string.pointer);
                        }

                        if (p.length + prefix_character_count == value_string.length)
                        {
                            if (format == BUILD_INTEGER_OPTION_FORMAT_DECIMAL_NEGATIVE)
                            {
                                let negative_value = (s64)0-(s64)p.value;
                                let cast_value = (u64)((s64)0 - negative_value);
                                if (cast_value != p.value)
                                {
                                    string8_print(S8("Negative value is too low: \n"), argument);
                                    result = PROCESS_RESULT_FAILED;
                                }
                                else
                                {
                                    build_program_state.integer.values[build_integer_option_i] = (BuildIntegerOptionValue) {
                                        .signed_value = negative_value,
                                    };
                                }
                            }
                            else
                            {
                                build_program_state.integer.values[build_integer_option_i] = (BuildIntegerOptionValue) {
                                    .unsigned_value = p.value,
                                };
                            }

                            build_program_state.integer.settings[build_integer_option_i] = (BuildIntegerOptionSettings) {
                                .format = format,
                                .is_set = true,
                            };
                        }
                        else
                        {
                            string8_print(S8("For argument '{OsS}', unrecognized value\n"), argument);
                            result = PROCESS_RESULT_FAILED;
                        }

                        break;
                    }
                }

                found = i != BUILD_INTEGER_OPTION_COUNT;
            }

            if (!found)
            {
                let os_argument_process_result = buster_argument_process(argv, envp, argument_count, command);
                if (os_argument_process_result != PROCESS_RESULT_SUCCESS)
                {
                    string8_print(S8("Unrecognized argument: '{OsS}'\n"), argument);
                    result = os_argument_process_result;
                    break;
                }
            }

            argument_count += 1;
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

    if (is_command_present & !program_state->input.verbose)
    {
        program_state->input.verbose = true;
    }

    return result;
}

BUSTER_LOCAL bool builder_tests(TestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    return true;
}

STRUCT(CompileLinkOptions)
{
    StringOs destination_path;
    StringOs* source_paths;
    u64 source_count;
    BuildTarget* target;

    u64 optimize:1;
    u64 fuzz:1;
    u64 has_debug_information:1;
    u64 ci:1;
    u64 unity_build:1;
    u64 use_io_ring:1;
    u64 just_preprocessor:1;
    u64 compile:1;
    u64 link:1;
    u64 reserved:55;
};

BUSTER_LOCAL StringOsList build_compile_link_arguments(Arena* arena, CompileLinkOptions options)
{
    // Forced to do it so early because we would need another arena here otherwise (since arena is used for the argument builder)
    StringOs march_os_string = options.target->march_string;

    let builder = argument_builder_start(arena, clang_path);
    argument_add(builder, SOs("-ferror-limit=1"));
    if (options.just_preprocessor)
    {
        argument_add(builder, SOs("-E"));
    }

    argument_add(builder, SOs("-o"));
    argument_add(builder, options.destination_path);
    for (u64 i = 0; i < options.source_count; i += 1)
    {
        argument_add(builder, options.source_paths[i]);
    }

    bool sanitize = BUSTER_SELECT(options.ci, options.has_debug_information & ((!options.optimize) | options.fuzz), options.fuzz);
    if (sanitize)
    {
        if (!(options.target->pointer->cpu_arch == CPU_ARCH_AARCH64 && options.target->pointer->os == OPERATING_SYSTEM_WINDOWS))
        {
            argument_add(builder, SOs("-fsanitize=address"));
        }

        argument_add(builder, SOs("-fsanitize=undefined"));
        argument_add(builder, SOs("-fsanitize=bounds"));
        argument_add(builder, SOs("-fsanitize-recover=all"));

        if (options.fuzz)
        {
            argument_add(builder, SOs("-fsanitize=fuzzer"));
        }
    }

    if (options.has_debug_information)
    {
        argument_add(builder, SOs("-g"));
    }

    if (xc_sdk_path.pointer)
    {
        argument_add(builder, SOs("-isysroot"));
        argument_add(builder, xc_sdk_path);
    }

    if (options.compile)
    {
        if (!options.link || options.just_preprocessor)
        {
            argument_add(builder, SOs("-c"));
        }

        argument_add(builder, SOs("-Isrc"));

        argument_add(builder, SOs("-std=gnu2x"));

        if (!options.just_preprocessor)
        {
            argument_add(builder, SOs("-Werror"));
        }
        argument_add(builder, SOs("-Wall"));
        argument_add(builder, SOs("-Wextra"));
        argument_add(builder, SOs("-Wpedantic"));
        argument_add(builder, SOs("-pedantic"));

        argument_add(builder, SOs("-Wno-gnu-auto-type"));
        argument_add(builder, SOs("-Wno-pragma-once-outside-header"));
        argument_add(builder, SOs("-Wno-gnu-empty-struct"));
        argument_add(builder, SOs("-Wno-bitwise-instead-of-logical"));
        argument_add(builder, SOs("-Wno-unused-function"));
        argument_add(builder, SOs("-Wno-gnu-flexible-array-initializer"));
        argument_add(builder, SOs("-Wno-missing-field-initializers"));
        argument_add(builder, SOs("-Wno-language-extension-token"));
        argument_add(builder, SOs("-Wno-zero-length-array"));

        argument_add(builder, SOs("-funsigned-char"));
        argument_add(builder, SOs("-fwrapv"));
        argument_add(builder, SOs("-fno-strict-aliasing"));

        argument_add(builder, SOs("-DBUSTER_CLANG_ABSOLUTE_PATH=" OS_STRING_DOUBLE_QUOTE BUSTER_CLANG_ABSOLUTE_PATH OS_STRING_DOUBLE_QUOTE));
        argument_add(builder, SOs("-DBUSTER_TOOLCHAIN_ABSOLUTE_PATH=" OS_STRING_DOUBLE_QUOTE BUSTER_TOOLCHAIN_ABSOLUTE_PATH OS_STRING_DOUBLE_QUOTE));
        argument_add(builder, options.unity_build ? SOs("-DBUSTER_UNITY_BUILD=1") : SOs("-DBUSTER_UNITY_BUILD=0"));
        argument_add(builder, options.fuzz ? SOs("-DBUSTER_FUZZING=1") : SOs("-DBUSTER_FUZZING=0"));
        argument_add(builder, options.use_io_ring ? SOs("-DBUSTER_USE_IO_RING=1") : SOs("-DBUSTER_USE_IO_RING=0"));

        argument_add(builder, march_os_string);

        if (options.optimize)
        {
            argument_add(builder, SOs("-O2"));
        }
        else
        {
            argument_add(builder, SOs("-O0"));
        }
    }

    if (!options.just_preprocessor && options.link)
    {
        argument_add(builder, SOs("-fuse-ld=lld"));

        if (options.use_io_ring)
        {
            argument_add(builder, SOs("-luring"));
        }

        if (options.target->pointer->os == OPERATING_SYSTEM_WINDOWS)
        {
            argument_add(builder, SOs("-lws2_32"));
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
    let cwd = path_absolute_arena(general_arena, SOs("."));

#if defined(BUSTER_XC_SDK_PATH)
    xc_sdk_path = SOs(BUSTER_XC_SDK_PATH);
#endif
    clang_path = SOs(BUSTER_CLANG_ABSOLUTE_PATH);
    toolchain_path = SOs(BUSTER_TOOLCHAIN_ABSOLUTE_PATH);

    LinkModule builder_modules[] = {
        { .id = MODULE_BUILDER },
        { .id = MODULE_LIB },
        { .id = MODULE_SYSTEM_HEADERS },
        { .id = MODULE_ENTRY_POINT },
        // { MODULE_MD5 },
        { .id = native_module },
        { .id = MODULE_TARGET }
    };
    LinkModule cc_modules[] = {
        { .id = MODULE_CC_MAIN },
        { .id = MODULE_LIB },
        { .id = MODULE_SYSTEM_HEADERS },
        { .id = MODULE_ENTRY_POINT },
        { .id = MODULE_IR },
        { .id = MODULE_CODEGEN },
        { .id = MODULE_LINK },
        { .id = MODULE_LINK_JIT },
        { .id = MODULE_LINK_ELF },
        { .id = native_module },
        { .id = MODULE_TARGET }
    };
    LinkModule asm_modules[] = {
        { .id = MODULE_ASM_MAIN },
        { .id = MODULE_LIB },
        { .id = MODULE_SYSTEM_HEADERS },
        { .id = MODULE_ENTRY_POINT },
        { .id = native_module },
        { .id = MODULE_TARGET },
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
            target->march_string = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, march_parts), true);

            StringOs triple_parts[2 * TARGET_STRING_COMPONENT_COUNT - 1];
            for (u64 triple_i = 0; triple_i < TARGET_STRING_COMPONENT_COUNT; triple_i += 1)
            {
                triple_parts[triple_i * 2 + 0] = target_strings.s[triple_i];
                if (triple_i < (TARGET_STRING_COMPONENT_COUNT - 1))
                {
                    triple_parts[triple_i * 2 + 1] = SOs("-");
                }
            }

            let target_triple = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, triple_parts), true);
            target->string = target_triple;

            StringOs directory_path_parts[] = {
                cwd,
                SOs("/build/"),
                target_triple,
            };
            let directory = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, directory_path_parts), true);
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

        let artifact_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, artifact_path_parts), true);
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
    };

    static_assert(BUSTER_ARRAY_LENGTH(directory_paths) == DIRECTORY_COUNT);

    StringOs module_names[] = {
        [MODULE_LIB] = SOs("lib"),
        [MODULE_SYSTEM_HEADERS] = SOs("system_headers"),
        [MODULE_ENTRY_POINT] = SOs("entry_point"),
        [MODULE_TARGET] = SOs("target"),
        [MODULE_X86_64] = SOs("x86_64"),
        [MODULE_AARCH64] = SOs("aarch64"),
        [MODULE_BUILDER] = SOs("build"),
        [MODULE_MD5] = SOs("md5"),
        [MODULE_CC_MAIN] = SOs("cc_main"),
        [MODULE_ASM_MAIN] = SOs("asm_main"),
        [MODULE_IR] = SOs("ir"),
        [MODULE_CODEGEN] = SOs("code_generation"),
        [MODULE_LINK] = SOs("link"),
        [MODULE_LINK_ELF] = SOs("elf"),
        [MODULE_LINK_JIT] = SOs("jit"),
    };

    static_assert(BUSTER_ARRAY_LENGTH(module_names) == MODULE_COUNT);

    if (!build_flag_get(BUILD_FLAG_UNITY_BUILD))
    {
        let cache_manifest = os_file_open(SOs("build/cache_manifest"), (OpenFlags) { .read = 1 }, (OpenPermissions){});
        let cache_manifest_stats = os_file_get_stats(cache_manifest, (FileStatsOptions){ .size = 1, .modified_time = 1 });
        let cache_manifest_buffer = (u8*)arena_allocate_bytes(thread_arena(), cache_manifest_stats.size, 64);
        os_file_read(cache_manifest, (ByteSlice){ cache_manifest_buffer, cache_manifest_stats.size }, cache_manifest_stats.size);
        os_file_close(cache_manifest);
        let cache_manifest_hash = hash_file(cache_manifest_buffer, cache_manifest_stats.size);
        BUSTER_UNUSED(cache_manifest_hash);
        if (cache_manifest)
        {
            string8_print(S8("TODO: Cache manifest found!\n"));
            os_fail();
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
        SOs("clang_rt.asan_dynamic-x86_64.dll");
#elif defined(__aarch64__)
        SOs("clang_rt.asan_dynamic-aarch64.dll");
#else
#pragma error
#endif
    OsString original_dll_parts[] = {
        toolchain_path,
        SOs("/lib/clang/21/lib/windows/"),
        dll_filename,
    };
    let original_asan_dll = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, original_dll_parts), true);
    let target_strings = target_to_split_os_string(target_native);
    OsString target_native_dir_path_parts[] = {
        SOs("build/"),
        target_strings.s[0],
        SOs("-"),
        target_strings.s[1],
        SOs("-"),
        target_strings.s[2],
    };
    let target_native_dir_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(OsStringSlice, target_native_dir_path_parts), true);
    os_make_directory(target_native_dir_path);
    OsString destination_dll_parts[] = {
        target_native_dir_path,
        SOs("/"),
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
                StringOs parts[] = {
                    cwd,
                    SOs("/"),
                    directory_paths[module_specification.directory],
                    module_specification.directory == DIRECTORY_ROOT ? SOs("") : SOs("/"),
                    module_names[module->id],
                    module_specification.no_source ? SOs(".h") : SOs(".c"),
                };
                let c_full_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, parts), true);

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
            };
        }
    }

    ProcessResult result = PROCESS_RESULT_SUCCESS;

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

        let object_path = arena_join_os_string(general_arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, object_absolute_path_parts), true);
        unit->object_path = object_path;

        CharOs buffer[max_path_length];
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
            if (slash_index == string_no_match)
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

            unit->compiler = clang_path;
            unit->compilation_arguments = args;

            append_string8(compile_commands, S8("\t{\n\t\t\"directory\": \""));
            append_string8(compile_commands, os_string_to_string8(general_arena, cwd));
            append_string8(compile_commands, S8("\",\n\t\t\"command\": \""));

            let arg_it = os_string_list_iterator_initialize(args);
            for (let arg = os_string_list_iterator_next(&arg_it); arg.pointer; arg = os_string_list_iterator_next(&arg_it))
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
            append_string8(compile_commands, os_string_to_string8(general_arena, source_absolute_path));
            append_string8(compile_commands, S8("\"\n"));
            append_string8(compile_commands, S8("\t},\n"));
        }

        compile_commands->position -= 2;

        append_string8(compile_commands, S8("\n]"));

        let compile_commands_str = (String8){ .pointer = (char8*)compile_commands + compile_commands_start, .length = compile_commands->position - compile_commands_start };
        result = file_write(SOs("build/compile_commands.json"), BUSTER_SLICE_TO_BYTE_SLICE(compile_commands_str)) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
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
                    string8_print(S8("Some of the compilations failed\n"));
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

                let source_paths = (StringOs*)align_forward((u64)((u8*)general_arena + general_arena->position), alignof(StringOs));
                u64 source_path_count = 0;

                for (u64 module_i = 0; module_i < link_modules.length; module_i += 1)
                {
                    let module = &link_modules.pointer[module_i];

                    if (!modules[module->id].no_source)
                    {
                        let unit = &compilation_units[module->index];
                        let object_path = arena_allocate(general_arena, StringOs, 1);
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
                let process = os_process_spawn(clang_path, link_arguments, program_state->input.envp);
                processes[link_unit_i] = process;
            }

            for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
            {
                let process = processes[link_unit_i];
                ProcessResources resources = {};
                let link_result = os_process_wait_sync(process, resources);
                if (link_result != PROCESS_RESULT_SUCCESS)
                {
                    string8_print(S8("Some of the linkage failed\n"));
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
                        string8_print(S8("Build tests failed!\n"));
                        result = PROCESS_RESULT_FAILED;
                    }

                    u64 fuzz_max_length = 4096;
                    let length_argument = arena_os_string_format(general_arena, SOs("-max_len={u64}"), fuzz_max_length);
                    u64 max_total_time = build_integer_option_get_unsigned(BUILD_INTEGER_OPTION_FUZZ_DURATION_SECONDS);
                    if (max_total_time == 0)
                    {
                        max_total_time = 30;
                    }
                    let max_total_time_argument = arena_os_string_format(general_arena, SOs("-max_total_time={u64}"), max_total_time);

                    // Skip builder executable since we execute the tests ourselves
                    for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
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
                        let os_arguments = os_string_list_create(general_arena, os_argument_slice);
                        processes[link_unit_i] = os_process_spawn(first_argument, os_arguments, program_state->input.envp);
                    }

                    for (u64 link_unit_i = link_unit_start; link_unit_i < link_unit_count; link_unit_i += 1)
                    {
                        let process = processes[link_unit_i];
                        ProcessResources resources = {};
                        let test_result = os_process_wait_sync(process, resources);
                        if (test_result != PROCESS_RESULT_SUCCESS)
                        {
                            string8_print(S8("Some of the unit tests failed\n"));
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
        string8_print(S8("Error writing compile commands: {OsS}"), get_last_error_message(general_arena));
    }

    return result;
}
