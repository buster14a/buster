#pragma once
#include <buster/build/build_common.h>

BUSTER_IMPL BuildTarget build_target_native = {
    .pointer = &target_native,
};

#define BUILD_DEFINE(s, v) (SOs("-D" s "=" #v))
#define BUILD_BOOLEAN_DEFINE(b, s) ((b) ? BUILD_DEFINE(#s, 1) : BUILD_DEFINE(#s, 0))

BUSTER_GLOBAL_LOCAL StringOs enable_warning_flags[] = {
    SOs("-Wall"),
    SOs("-Wextra"),
    SOs("-Wpedantic"),
    SOs("-pedantic"),
    SOs("-Wconversion"),
    SOs("-Wstrict-overflow=5"),
    SOs("-Woverflow"),
    SOs("-Wshift-overflow"),
    SOs("-Walloca"),
    SOs("-Warray-bounds-pointer-arithmetic"),
    SOs("-Wassign-enum"),
    SOs("-Wbool-conversion"),
    SOs("-Wbool-operation"),
    SOs("-Wcomma"),
    SOs("-Wconditional-uninitialized"),
    SOs("-Wdangling"),
    SOs("-Wdouble-promotion"),
    SOs("-Wenum-compare-conditional"),
    SOs("-Wenum-too-large"),
    SOs("-Wexperimental-lifetime-safety"),
    SOs("-Wfixed-point-overflow"),
    SOs("-Wflag-enum"),
    SOs("-Wformat"),
    SOs("-Wfortify-source"),
    SOs("-Wfour-char-constants"),
    SOs("-Whigher-precision-for-complex-division"),
    SOs("-Wimplicit"),
    SOs("-Wimplicit-fallthrough"),
    SOs("-Wimplicit-fallthrough-per-function"),
    SOs("-Wimplicit-float-conversion"),
    SOs("-Wimplicit-int-conversion"),
    SOs("-Wimplicit-void-ptr-cast"),
    SOs("-Winfinite-recursion"),
    SOs("-Winvalid-utf8"),
    SOs("-Wlarge-by-value-copy"),
    SOs("-Wlinker-warnings"),
    SOs("-Wloop-analysis"),
    SOs("-Wmain"),
    SOs("-Wmisleading-indentation"),
    SOs("-Wmissing-braces"),
    SOs("-Wmissing-noreturn"),
    SOs("-Wnon-power-of-two-alignment"),
    SOs("-Woption-ignored"),
    SOs("-Woverlength-strings"),
    SOs("-Wpacked"),
    SOs("-Wpadded"),
    SOs("-Wparentheses"),
    SOs("-Wpedantic-macros"),
    SOs("-Wpointer-arith"),
    SOs("-Wpragma-pack"),
    SOs("-Wpragma-pack-suspicious-include"),
    SOs("-Wpragmas"),
    SOs("-Wread-only-types"),
    SOs("-Wredundant-parens"),
    SOs("-Wreserved-identifier"),
    SOs("-Wreserved-macro-identifier"),
    SOs("-Wreserved-module-identifier"),
    SOs("-Wself-assign"),
    SOs("-Wself-assign-field"),
    SOs("-Wshadow"),
    SOs("-Wshadow-all"),
    SOs("-Wshadow-field"),
    SOs("-Wshift-bool"),
    SOs("-Wshift-sign-overflow"),
    SOs("-Wsigned-enum-bitfield"),
    SOs("-Wtautological-compare"),
    SOs("-Wtype-limits"),
    SOs("-Wtautological-constant-in-range-compare"),
    SOs("-Wthread-safety"),
    SOs("-Wuninitialized"),
    SOs("-Wunaligned-access"),
    SOs("-Wunique-object-duplication"),
    SOs("-Wunreachable-code"),
    SOs("-Wunreachable-code-return"),
    SOs("-Wvector-conversion"),
};

BUSTER_GLOBAL_LOCAL StringOs disable_warning_flags[] = {
    SOs("-Wno-language-extension-token"),
    SOs("-Wno-gnu-auto-type"),
    SOs("-Wno-gnu-empty-struct"),
    SOs("-Wno-bitwise-instead-of-logical"),
    SOs("-Wno-unused-function"),
    SOs("-Wno-gnu-flexible-array-initializer"),
    SOs("-Wno-missing-field-initializers"),
    SOs("-Wno-pragma-once-outside-header"),
    SOs("-Wno-zero-length-array"),
    SOs("-Wno-gnu-zero-variadic-macro-arguments"),
    SOs("-Wno-gnu-statement-expression-from-macro-expansion"),
};

BUSTER_GLOBAL_LOCAL StringOs f_flags[] = {
    SOs("-fwrapv"),
    SOs("-fno-strict-aliasing"),
    SOs("-funsigned-char"),
    SOs("-fno-exceptions"),
    SOs("-fno-rtti"),
};

BUSTER_GLOBAL_LOCAL StringOs include_flags[] = {
    SOs("-Isrc"),
};

BUSTER_GLOBAL_LOCAL StringOs std_flags[] = {
    SOs("-std=gnu2x"),
};

BUSTER_IMPL StringOsList build_compile_link_arguments(Arena* arena, const CompileLinkOptions * const restrict options)
{
    // Forced to do it so early because we would need another arena here otherwise (since arena is used for the argument builder)
    StringOs march_string_os = options->target->march_string;

    let builder = string_os_list_builder_create(arena, options->clang_path);
    string_os_list_builder_append(builder, SOs("-ferror-limit=1"));
    if (options->just_preprocessor)
    {
        string_os_list_builder_append(builder, SOs("-E"));
    }

    if (options->force_color)
    {
        string_os_list_builder_append(builder, SOs("-fdiagnostics-color=always"));
    }
    else
    {
        string_os_list_builder_append(builder, SOs("-fdiagnostics-color=auto"));
    }

    string_os_list_builder_append(builder, SOs("-o"));
    string_os_list_builder_append(builder, options->destination_path);
    for (u64 i = 0; i < options->source_count; i += 1)
    {
        string_os_list_builder_append(builder, options->source_paths[i]);
    }

    if (options->sanitize)
    {
        if (!(options->target->pointer->cpu_arch == CPU_ARCH_AARCH64 && options->target->pointer->os == OPERATING_SYSTEM_WINDOWS))
        {
            string_os_list_builder_append(builder, SOs("-fsanitize=address"));
        }

        string_os_list_builder_append(builder, SOs("-fsanitize=undefined"));
        string_os_list_builder_append(builder, SOs("-fsanitize=bounds"));
        string_os_list_builder_append(builder, SOs("-fsanitize-recover=all"));
    }

    if (options->fuzz)
    {
        string_os_list_builder_append(builder, SOs("-fsanitize=fuzzer"));
    }

    if (options->has_debug_information)
    {
        string_os_list_builder_append(builder, SOs("-g"));
    }

    if (options->xc_sdk_path.pointer)
    {
        string_os_list_builder_append(builder, SOs("-isysroot"));
        string_os_list_builder_append(builder, options->xc_sdk_path);
    }

    if (options->compile)
    {
        if (!options->link || options->just_preprocessor)
        {
            string_os_list_builder_append(builder, SOs("-c"));
        }

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(include_flags); i += 1)
        {
            let include_flag = include_flags[i];
            string_os_list_builder_append(builder, include_flag);
        }

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(std_flags); i += 1)
        {
            let std_flag = std_flags[i];
            string_os_list_builder_append(builder, std_flag);
        }

        if (!options->just_preprocessor)
        {
            string_os_list_builder_append(builder, SOs("-Werror"));
        }

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(enable_warning_flags); i += 1)
        {
            let enable_warning_flag = enable_warning_flags[i];
            string_os_list_builder_append(builder, enable_warning_flag);
        }

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(disable_warning_flags); i += 1)
        {
            let disable_warning_flag = disable_warning_flags[i];
            string_os_list_builder_append(builder, disable_warning_flag);
        }

        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(f_flags); i += 1)
        {
            let f_flag = f_flags[i];
            string_os_list_builder_append(builder, f_flag);
        }

        string_os_list_builder_append(builder, BUILD_BOOLEAN_DEFINE(options->unity_build, BUSTER_UNITY_BUILD));
        string_os_list_builder_append(builder, BUILD_BOOLEAN_DEFINE(options->fuzz, BUSTER_FUZZING));
        string_os_list_builder_append(builder, BUILD_BOOLEAN_DEFINE(options->use_io_ring, BUSTER_USE_IO_RING));
        string_os_list_builder_append(builder, BUILD_BOOLEAN_DEFINE(options->include_tests, BUSTER_INCLUDE_TESTS));

        string_os_list_builder_append(builder, march_string_os);

        if (options->optimize)
        {
            string_os_list_builder_append(builder, SOs("-O2"));
        }
        else
        {
            string_os_list_builder_append(builder, SOs("-O0"));
        }
    }

    if (!options->just_preprocessor && options->link)
    {
        string_os_list_builder_append(builder, SOs("-fuse-ld=lld"));

        if (options->use_io_ring)
        {
            string_os_list_builder_append(builder, SOs("-luring"));
        }

        if (options->target->pointer->os == OPERATING_SYSTEM_WINDOWS)
        {
            string_os_list_builder_append(builder, SOs("-lws2_32"));
        }
    }

    return string_os_list_builder_end(builder);
}

ENUM(BuildCommand,
    BUILD_COMMAND_BUILD,
    BUILD_COMMAND_TEST,
    BUILD_COMMAND_DEBUG,
    BUILD_COMMAND_TEST_ALL,
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
    BUILD_FLAG_SANITIZE,
    BUILD_FLAG_MAIN_BRANCH,
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

ENUM(BuildStringOption,
    BUILD_STRING_OPTION_XC_SDK_PATH,
    BUILD_STRING_OPTION_COUNT,
);

STRUCT(BuildStringOptionSettings)
{
    u8 is_set:1;
    u8 reserved:7;
};

static_assert(sizeof(BuildStringOptionSettings) == 1);

STRUCT(BuildStringOptions)
{
    StringOs values[BUILD_STRING_OPTION_COUNT];
    BuildStringOptionSettings settings[BUILD_STRING_OPTION_COUNT];
    u8 reserved[7];
};

typedef u64 FlagBackingType;

STRUCT(BuildProgramState)
{
    ProgramState general_state;
    FlagBackingType value_flags;
    FlagBackingType set_flags;
    BuildIntegerOptions integer;
    BuildStringOptions string;
    BuildCommand command;
    u8 reserved[4];
};

static_assert(sizeof(FlagBackingType) * 8 > BUILD_FLAG_COUNT);

BUSTER_GLOBAL_LOCAL BuildProgramState build_program_state = {};
BUSTER_IMPL ProgramState* program_state = &build_program_state.general_state;

BUSTER_GLOBAL_LOCAL bool build_flag_is_set(BuildFlag flag)
{
    return (build_program_state.set_flags & ((FlagBackingType)1 << flag)) != 0;
}

BUSTER_GLOBAL_LOCAL bool build_flag_get(BuildFlag flag)
{
    return (build_program_state.value_flags & ((FlagBackingType)1 << flag)) != 0;
}

BUSTER_GLOBAL_LOCAL void build_flag_set(BuildFlag flag, bool value)
{
    build_program_state.value_flags |= (FlagBackingType)value << flag;
    build_program_state.set_flags |= (FlagBackingType)1 << flag;
}

BUSTER_GLOBAL_LOCAL u64 build_integer_option_get_unsigned(BuildIntegerOption option_id)
{
    let option_value = build_program_state.integer.values[option_id];
    return option_value.unsigned_value;
}

BUSTER_IMPL ProcessResult process_arguments()
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;
    let argv = program_state->input.argv;
    let envp = program_state->input.envp;
    bool is_command_present = false;

    {
        let arg_it = string_os_list_iterator_initialize(argv);
        let first_argument = string_os_list_iterator_next(&arg_it);
        BUSTER_UNUSED(first_argument);
        let command = string_os_list_iterator_next(&arg_it);
        u64 argument_count = 1;

        if (command.pointer)
        {
            if (string_os_starts_with_sequence(command, SOs("--")))
            {
                arg_it = string_os_list_iterator_initialize(argv);
                string_os_list_iterator_next(&arg_it);
            }
            else
            {
                StringOs possible_commands[] = {
                    [BUILD_COMMAND_BUILD] = SOs("build"),
                    [BUILD_COMMAND_TEST] = SOs("test"),
                    [BUILD_COMMAND_TEST_ALL] = SOs("test_all"),
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
                    string8_print(S8("Command not recognized: {SOs}!\n"), command);
                    result = PROCESS_RESULT_FAILED;
                }

                argument_count += 1;
            }
        }

        StringOs argument;
        while ((argument = string_os_list_iterator_next(&arg_it)).pointer)
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
                [BUILD_FLAG_SANITIZE] = SOs("--sanitize="),
                [BUILD_FLAG_MAIN_BRANCH] = SOs("--main-branch="),
            };
            static_assert(BUSTER_ARRAY_LENGTH(build_flag_strings) == BUILD_FLAG_COUNT);

            BuildFlag build_flag;
            for (build_flag = 0; build_flag < BUILD_FLAG_COUNT; build_flag += 1)
            {
                let build_flag_string = build_flag_strings[build_flag];
                if (string_os_starts_with_sequence(argument, build_flag_string))
                {
                    bool is_valid_value = argument.length == build_flag_string.length + 1;

                    if (is_valid_value)
                    {
                        let arg_value = argument.pointer[build_flag_string.length];
                        switch (arg_value)
                        {
                            break; case '1': case '0': build_flag_set(build_flag, arg_value == '1');
                            break; default:
                            {
                                is_valid_value = false;
                            }
                        }
                    }

                    if (!is_valid_value)
                    {
                        string8_print(S8("For argument '{SOs}', unrecognized value\n"), argument);
                        result = PROCESS_RESULT_FAILED;
                    }

                    break;
                }
            }

            bool found = build_flag != BUILD_FLAG_COUNT;

            if (!found)
            {
                StringOs integer_option_strings[] = {
                    [BUILD_INTEGER_OPTION_FUZZ_DURATION_SECONDS] = SOs("--fuzz-duration="),
                };
                static_assert(BUSTER_ARRAY_LENGTH(integer_option_strings) == BUILD_INTEGER_OPTION_COUNT);

                BuildIntegerOption build_integer_option;
                for (build_integer_option = 0; build_integer_option < BUILD_INTEGER_OPTION_COUNT; build_integer_option += 1)
                {
                    let integer_option_string = integer_option_strings[build_integer_option];

                    if (string_os_starts_with_sequence(argument, integer_option_string))
                    {
                        let value_string = BUSTER_SLICE_START(argument, integer_option_string.length);

                        BuildIntegerOptionFormat format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE;

                        // TODO: handle negative numbers
                        IntegerParsingU64 p = {};
                        u64 prefix_character_count = 0;
                        if (value_string.length > 2 && value_string.pointer[0] == '0')
                        {
                            let second_ch = value_string.pointer[1];
                            switch (second_ch)
                            {
                                break; case 'x': format = BUILD_INTEGER_OPTION_FORMAT_HEXADECIMAL; prefix_character_count = 2; p = string_os_parse_u64_hexadecimal(value_string.pointer + 2);
                                break; case 'd': format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE; prefix_character_count = 2; p = string_os_parse_u64_decimal(value_string.pointer + 2);
                                break; case 'o': format = BUILD_INTEGER_OPTION_FORMAT_OCTAL; prefix_character_count = 2; p = string_os_parse_u64_octal(value_string.pointer + 2);
                                break; case 'b': format = BUILD_INTEGER_OPTION_FORMAT_BINARY; prefix_character_count = 2; p = string_os_parse_u64_binary(value_string.pointer + 2);
                                break; default: {}
                            }
                        }
                        else if (value_string.length > 1 && value_string.pointer[0] == '-')
                        {
                            format = BUILD_INTEGER_OPTION_FORMAT_DECIMAL_NEGATIVE;
                            prefix_character_count = 1;
                            p = string_os_parse_u64_decimal(value_string.pointer + 1);
                        }

                        if (format == BUILD_INTEGER_OPTION_FORMAT_DECIMAL_POSITIVE && p.length == 0)
                        {
                            p = string_os_parse_u64_decimal(value_string.pointer);
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
                                    build_program_state.integer.values[build_integer_option] = (BuildIntegerOptionValue) {
                                        .signed_value = negative_value,
                                    };
                                }
                            }
                            else
                            {
                                build_program_state.integer.values[build_integer_option] = (BuildIntegerOptionValue) {
                                    .unsigned_value = p.value,
                                };
                            }

                            build_program_state.integer.settings[build_integer_option] = (BuildIntegerOptionSettings) {
                                .format = format,
                                .is_set = true,
                            };
                        }
                        else
                        {
                            string8_print(S8("For argument '{SOs}', unrecognized value\n"), argument);
                            result = PROCESS_RESULT_FAILED;
                        }

                        break;
                    }
                }

                found = build_integer_option != BUILD_INTEGER_OPTION_COUNT;
            }

            if (!found)
            {
                StringOs string_option_strings[] = {
                    [BUILD_STRING_OPTION_XC_SDK_PATH] = SOs("--xc-sdk-path="),
                };
                static_assert(BUSTER_ARRAY_LENGTH(string_option_strings) == BUILD_STRING_OPTION_COUNT);

                BuildStringOption build_string_option;
                for (build_string_option = 0; build_string_option < BUILD_STRING_OPTION_COUNT; build_string_option += 1)
                {
                    let string_option_string = string_option_strings[build_string_option];

                    if (string_os_starts_with_sequence(argument, string_option_string))
                    {
                        let value_string = BUSTER_SLICE_START(argument, string_option_string.length);

                        build_program_state.string.values[build_string_option] = value_string;
                        build_program_state.string.settings[build_string_option] = (BuildStringOptionSettings) {
                            .is_set = true,
                        };

                        break;
                    }
                }

                found = build_string_option != BUILD_STRING_OPTION_COUNT;
            }

            if (!found)
            {
                let os_argument_process_result = buster_argument_process(argv, envp, argument_count, command);
                if (os_argument_process_result != PROCESS_RESULT_SUCCESS)
                {
                    string8_print(S8("Unrecognized argument: '{SOs}'\n"), argument);
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

BUSTER_IMPL ToolchainInformation toolchain_get_information(Arena* arena, LLVMVersion llvm_version)
{
    StringOs home_path;
#if defined(_WIN32)
    home_path = os_get_environment_variable(SOs("USERPROFILE"));
#else
    home_path = os_get_environment_variable(SOs("HOME"));
#endif
    StringOs install_path_parts[] = {
        home_path,
        SOs("/dev/toolchain/install"),
    };
    let install_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, install_path_parts), true);
    StringOs llvm_basename_parts[] = {
        SOs("llvm_"),
        llvm_version.string,
        SOs("_"),
#if defined(__x86_64__)
        SOs("x86_64"),
#elif defined(__aarch64__)
        SOs("aarch64"),
#endif
        SOs("-"),
#if defined(__linux__)
        SOs("linux"),
#elif defined(__APPLE__)
        SOs("macos"),
#elif defined(_WIN32)
        SOs("windows"),
#endif
        SOs("-Release"),
    };
    let llvm_basename = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, llvm_basename_parts), true);

    StringOs url_parts[] = {
        SOs("https://github.com/buster14a/toolchain/releases/download/v"),
        llvm_version.string,
        SOs("/"),
        llvm_basename,
        SOs(".7z"),
    };
    let url = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, url_parts), true);

    StringOs prefix_path_parts[] = {
        install_path,
        SOs("/"),
        llvm_basename,
    };
    let prefix_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, prefix_path_parts), true);
    StringOs clang_path_parts[] = {
        prefix_path,
        SOs("/bin/clang"),
#if defined(_WIN32)
        SOs(".exe"),
#else
        SOs(""),
#endif
    };
    let clang_path = string_os_join_arena(arena, BUSTER_ARRAY_TO_SLICE(StringOsSlice, clang_path_parts), true);
    return (ToolchainInformation) {
        .clang_path = clang_path,
        .prefix_path = prefix_path,
        .install_path = install_path,
        .url = url,
    };
}
