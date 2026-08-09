#include <buster/lib/compiler/driver/driver.h>

#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>

void compiler_prewarm(void)
{
    tokenizer_prewarm();
    c_prewarm();
    codegen_prewarm();
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_option_value(String8 argument, String8 prefix)
{
    if (!string_starts_with_sequence(argument, prefix))
    {
        return (String8){0};
    }
    return (String8){
        .pointer = argument.pointer + prefix.length,
        .length = argument.length - prefix.length,
    };
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_dialect(CompilerDriverInvocation* invocation, String8 dialect)
{
    if (string_equal(dialect, S8("gnu11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU11;
    }
    else if (string_equal(dialect, S8("gnu17")) || string_equal(dialect, S8("gnu18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17;
    }
    else if (string_equal(dialect, S8("gnu23")) || string_equal(dialect, S8("gnu2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_GNU23;
    }
    else if (string_equal(dialect, S8("c11")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C11;
    }
    else if (string_equal(dialect, S8("c17")) || string_equal(dialect, S8("c18")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C17;
    }
    else if (string_equal(dialect, S8("c23")) || string_equal(dialect, S8("c2x")))
    {
        invocation->c_dialect = COMPILER_DRIVER_C_DIALECT_C23;
    }
    else
    {
        return false;
    }
    return true;
}

BUSTER_GLOBAL_LOCAL CPreprocessDialect compiler_driver_preprocess_dialect(CompilerDriverCDialect dialect)
{
    switch (dialect)
    {
    case COMPILER_DRIVER_C_DIALECT_GNU11:
        return C_PREPROCESS_DIALECT_GNU11;
    case COMPILER_DRIVER_C_DIALECT_GNU17:
        return C_PREPROCESS_DIALECT_GNU17;
    case COMPILER_DRIVER_C_DIALECT_GNU23:
        return C_PREPROCESS_DIALECT_GNU23;
    case COMPILER_DRIVER_C_DIALECT_C11:
        return C_PREPROCESS_DIALECT_C11;
    case COMPILER_DRIVER_C_DIALECT_C17:
        return C_PREPROCESS_DIALECT_C17;
    case COMPILER_DRIVER_C_DIALECT_C23:
        return C_PREPROCESS_DIALECT_C23;
    case COMPILER_DRIVER_C_DIALECT_COUNT:
        return C_PREPROCESS_DIALECT_COUNT;
    }
    return C_PREPROCESS_DIALECT_COUNT;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_argument_error(Arena* arena, CompilerDriverInvocation* invocation, String8 format, String8 argument)
{
    invocation->error = COMPILER_DRIVER_ERROR_ARGUMENT;
    invocation->diagnostic = string_format(arena, format, argument);
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_cpu_model(Arena* arena, CompilerDriverInvocation* invocation, String8 model_string)
{
    CpuModel model = cpu_model_from_string(model_string);
    if (model == CPU_MODEL_ERROR)
    {
        compiler_driver_argument_error(arena, invocation, S8("unsupported CPU model: {S8}"), model_string);
        return false;
    }
    invocation->target.cpu_model = model;
    invocation->target.cpu_features_explicit = false;
    invocation->target.cpu_features = target_cpu_features_empty();
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_target(Arena* arena, CompilerDriverInvocation* invocation, String8 target_string)
{
    TargetParseResult parsed = target_parse_triple(target_string);
    switch (parsed.error)
    {
    case TARGET_PARSE_ERROR_NONE:
        invocation->target = parsed.target;
        return true;
    case TARGET_PARSE_ERROR_CPU_MODEL:
        compiler_driver_argument_error(arena, invocation, S8("CPU model must be selected with -march=: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_EXCESS_COMPONENT:
        compiler_driver_argument_error(arena, invocation, S8("unsupported target component: {S8}"), parsed.invalid_component);
        return false;
    case TARGET_PARSE_ERROR_EMPTY:
    case TARGET_PARSE_ERROR_ARCHITECTURE:
    case TARGET_PARSE_ERROR_OPERATING_SYSTEM:
    case TARGET_PARSE_ERROR_COUNT:
        break;
    }
    compiler_driver_argument_error(arena, invocation, S8("unsupported target: {S8}"), target_string);
    return false;
}

typedef struct CompilerDriverFeatureOverride CompilerDriverFeatureOverride;
struct CompilerDriverFeatureOverride
{
    String8 name;
    bool enable;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_parse_feature_overrides(Arena* arena, CompilerDriverInvocation* invocation, String8 value,
                                                                  CompilerDriverFeatureOverride* overrides, u64 override_capacity, u64* override_count)
{
    u64 start = 0;
    while (start < value.length)
    {
        u64 end = start;
        while (end < value.length && value.pointer[end] != ',')
        {
            end += 1;
        }
        String8 item = string_slice(value, start, end);
        if (item.length < 2 || (item.pointer[0] != '+' && item.pointer[0] != '-'))
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), item);
            return false;
        }
        if (*override_count >= override_capacity)
        {
            compiler_driver_argument_error(arena, invocation, S8("too many target feature overrides: {S8}"), value);
            return false;
        }
        overrides[*override_count] = (CompilerDriverFeatureOverride){
            .name = string_slice(item, 1, item.length),
            .enable = item.pointer[0] == '+',
        };
        *override_count += 1;
        if (end == value.length)
        {
            return true;
        }
        start = end + 1;
        if (start == value.length)
        {
            compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
            return false;
        }
    }
    compiler_driver_argument_error(arena, invocation, S8("invalid target feature override: {S8}"), value);
    return false;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_set_assembly_syntax(Arena* arena, CompilerDriverInvocation* invocation, String8 value)
{
    if (string_equal(value, S8("att")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_ATT;
        return true;
    }
    if (string_equal(value, S8("intel")))
    {
        invocation->assembly_syntax = ASSEMBLY_SYNTAX_INTEL;
        return true;
    }
    compiler_driver_argument_error(arena, invocation, S8("unsupported assembly syntax: {S8}"), value);
    return false;
}

CompilerDriverInvocation compiler_driver_parse_arguments(Arena* arena, SliceString8 arguments)
{
    CompilerDriverInvocation invocation = {
        .target = target_native,
        .language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC,
        .action = COMPILER_DRIVER_ACTION_LINK,
        .c_dialect = COMPILER_DRIVER_C_DIALECT_GNU17,
        .debug_info = true,
    };
    if (!arena)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        return invocation;
    }
    invocation.input_paths = arena_allocate(arena, String8, arguments.length);
    invocation.include_paths = arena_allocate(arena, String8, arguments.length);
    invocation.system_include_paths = arena_allocate(arena, String8, arguments.length + 64);
    invocation.definitions = arena_allocate(arena, String8, arguments.length);
    invocation.undefinitions = arena_allocate(arena, String8, arguments.length);
    invocation.library_paths = arena_allocate(arena, String8, arguments.length);
    invocation.libraries = arena_allocate(arena, String8, arguments.length);
    invocation.framework_paths = arena_allocate(arena, String8, arguments.length);
    invocation.frameworks = arena_allocate(arena, String8, arguments.length);
    invocation.linker_arguments = arena_allocate(arena, String8, arguments.length);
    u64 feature_override_capacity = 0;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        feature_override_capacity += arguments.pointer[argument_index].length / 2 + 1;
    }
    CompilerDriverFeatureOverride* feature_overrides = arena_allocate(arena, CompilerDriverFeatureOverride, feature_override_capacity);
    u64 feature_override_count = 0;
    bool options_ended = false;
    bool action_seen = false;
    for (u64 argument_index = 0; argument_index < arguments.length; argument_index += 1)
    {
        String8 argument = arguments.pointer[argument_index];
        if (options_ended || !argument.length || argument.pointer[0] != '-')
        {
            invocation.input_paths[invocation.input_count++] = argument;
            continue;
        }
        if (string_equal(argument, S8("--")))
        {
            options_ended = true;
            continue;
        }
        if (string_equal(argument, S8("-E")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_PREPROCESS)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                return invocation;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_PREPROCESS;
            continue;
        }
        if (string_equal(argument, S8("-S")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_ASSEMBLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                return invocation;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_ASSEMBLY;
            continue;
        }
        if (string_equal(argument, S8("-c")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_OBJECT)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                return invocation;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_OBJECT;
            continue;
        }
        if (string_equal(argument, S8("-fsyntax-only")))
        {
            if (action_seen && invocation.action != COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
            {
                compiler_driver_argument_error(arena, &invocation, S8("conflicting compiler actions: {S8}"), argument);
                return invocation;
            }
            action_seen = true;
            invocation.action = COMPILER_DRIVER_ACTION_SYNTAX_ONLY;
            continue;
        }
        if (string_equal(argument, S8("-v")) || string_equal(argument, S8("--verbose")))
        {
            invocation.verbose = true;
            continue;
        }
        if (string_equal(argument, S8("-g")) || string_equal(argument, S8("-g0")))
        {
            invocation.debug_info = !string_equal(argument, S8("-g0"));
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-g")))
        {
            compiler_driver_argument_error(arena, &invocation, S8("unsupported debug option: {S8}"), argument);
            return invocation;
        }
        if (string_equal(argument, S8("-nostdinc")))
        {
            invocation.no_standard_includes = true;
            continue;
        }
        if (string_equal(argument, S8("-o")) || string_equal(argument, S8("-I")) || string_equal(argument, S8("-isystem")) ||
            string_equal(argument, S8("-D")) || string_equal(argument, S8("-U")) || string_equal(argument, S8("-L")) || string_equal(argument, S8("-l")) ||
            string_equal(argument, S8("-F")) || string_equal(argument, S8("-framework")) || string_equal(argument, S8("-x")) ||
            string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")) || string_equal(argument, S8("-march")) ||
            string_equal(argument, S8("-mcpu")) || string_equal(argument, S8("-mattr")) || string_equal(argument, S8("-masm")) ||
            string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")) ||
            string_equal(argument, S8("-Xlinker")) || string_equal(argument, S8("-fmodule-root")) || string_equal(argument, S8("--module-root")))
        {
            if (argument_index + 1 >= arguments.length)
            {
                compiler_driver_argument_error(arena, &invocation, S8("missing argument after {S8}"), argument);
                return invocation;
            }
            String8 value = arguments.pointer[++argument_index];
            if (string_equal(argument, S8("-o")))
            {
                invocation.output_path = value;
            }
            else if (string_equal(argument, S8("-I")))
            {
                invocation.include_paths[invocation.include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-isystem")))
            {
                invocation.system_include_paths[invocation.system_include_path_count++] = value;
            }
            else if (string_equal(argument, S8("-D")))
            {
                invocation.definitions[invocation.definition_count++] = value;
            }
            else if (string_equal(argument, S8("-U")))
            {
                invocation.undefinitions[invocation.undefinition_count++] = value;
            }
            else if (string_equal(argument, S8("-L")))
            {
                invocation.library_paths[invocation.library_path_count++] = value;
            }
            else if (string_equal(argument, S8("-l")))
            {
                invocation.libraries[invocation.library_count++] = value;
            }
            else if (string_equal(argument, S8("-F")))
            {
                invocation.framework_paths[invocation.framework_path_count++] = value;
            }
            else if (string_equal(argument, S8("-framework")))
            {
                invocation.frameworks[invocation.framework_count++] = value;
            }
            else if (string_equal(argument, S8("-x")))
            {
                if (string_equal(value, S8("c")) || string_equal(value, S8("cpp-output")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_C;
                }
                else if (string_equal(value, S8("buster")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_BUSTER;
                }
                else if (string_equal(value, S8("none")))
                {
                    invocation.language = COMPILER_DRIVER_LANGUAGE_AUTOMATIC;
                }
                else
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported language: {S8}"), value);
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-target")) || string_equal(argument, S8("--target")))
            {
                if (!compiler_driver_set_target(arena, &invocation, value))
                {
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-march")) || string_equal(argument, S8("-mcpu")))
            {
                if (!compiler_driver_set_cpu_model(arena, &invocation, value))
                {
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-mattr")))
            {
                if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity,
                                                             &feature_override_count))
                {
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-masm")))
            {
                if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
                {
                    return invocation;
                }
            }
            else if (string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")))
            {
                invocation.sysroot = value;
            }
            else if (string_equal(argument, S8("-fmodule-root")) || string_equal(argument, S8("--module-root")))
            {
                invocation.module_root = value;
            }
            else
            {
                invocation.linker_arguments[invocation.linker_argument_count++] = value;
            }
            continue;
        }
        String8 value = compiler_driver_option_value(argument, S8("--target="));
        if (value.length)
        {
            if (!compiler_driver_set_target(arena, &invocation, value))
            {
                return invocation;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--sysroot="));
        if (value.length)
        {
            invocation.sysroot = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fsource-metrics="));
        if (value.length)
        {
            invocation.source_metrics_path = value;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-fregister-allocator="));
        if (value.length)
        {
            if (string_equal(value, S8("none")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_NONE;
            }
            else if (string_equal(value, S8("mir-stack")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK;
            }
            else if (string_equal(value, S8("fast")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST;
            }
            else if (string_equal(value, S8("quality")))
            {
                invocation.register_allocator = CODEGEN_REGISTER_ALLOCATOR_QUALITY;
            }
            else
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported register allocator: {S8}"), value);
                return invocation;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-march="));
        if (!value.length)
        {
            value = compiler_driver_option_value(argument, S8("-mcpu="));
        }
        if (value.length)
        {
            if (!compiler_driver_set_cpu_model(arena, &invocation, value))
            {
                return invocation;
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-mattr=")))
        {
            value = string_slice(argument, S8("-mattr=").length, argument.length);
            if (!compiler_driver_parse_feature_overrides(arena, &invocation, value, feature_overrides, feature_override_capacity, &feature_override_count))
            {
                return invocation;
            }
            continue;
        }
        if (string_starts_with_sequence(argument, S8("-masm=")))
        {
            value = string_slice(argument, S8("-masm=").length, argument.length);
            if (!compiler_driver_set_assembly_syntax(arena, &invocation, value))
            {
                return invocation;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-std="));
        if (value.length)
        {
            if (!compiler_driver_set_dialect(&invocation, value))
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported C dialect: {S8}"), value);
                return invocation;
            }
            continue;
        }
        value = compiler_driver_option_value(argument, S8("-Wl,"));
        if (value.length)
        {
            invocation.linker_arguments[invocation.linker_argument_count++] = value;
            continue;
        }
        String8 prefix = {
            .pointer = argument.pointer,
            .length = BUSTER_MIN(argument.length, (u64)2),
        };
        value = argument.length > 2 ?
            (String8){
                .pointer = argument.pointer + 2,
                .length = argument.length - 2,
            } :
            (String8){0};
        if (string_equal(prefix, S8("-I")) && value.length)
        {
            invocation.include_paths[invocation.include_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-D")) && value.length)
        {
            invocation.definitions[invocation.definition_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-U")) && value.length)
        {
            invocation.undefinitions[invocation.undefinition_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-L")) && value.length)
        {
            invocation.library_paths[invocation.library_path_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-l")) && value.length)
        {
            invocation.libraries[invocation.library_count++] = value;
            continue;
        }
        if (string_equal(prefix, S8("-F")) && value.length)
        {
            invocation.framework_paths[invocation.framework_path_count++] = value;
            continue;
        }
        bool optimization_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'O';
        bool debug_option = argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'g';
        bool warning_option =
            argument.length >= 2 && argument.pointer[0] == '-' && argument.pointer[1] == 'W' && !string_starts_with_sequence(argument, S8("-Wl,"));
        bool compatible_codegen_option =
            string_equal(argument, S8("-pipe")) || string_equal(argument, S8("-pthread")) || string_equal(argument, S8("-fPIC")) ||
            string_equal(argument, S8("-fpic")) || string_equal(argument, S8("-fPIE")) || string_equal(argument, S8("-fpie")) ||
            string_equal(argument, S8("-fno-pic")) || string_equal(argument, S8("-fno-pie")) || string_equal(argument, S8("-fno-builtin")) ||
            string_equal(argument, S8("-fwrapv")) || string_equal(argument, S8("-fno-strict-aliasing")) || string_equal(argument, S8("-funsigned-char")) ||
            string_equal(argument, S8("-fsigned-char")) || string_equal(argument, S8("-fcommon")) || string_equal(argument, S8("-fno-common"));
        if (optimization_option || debug_option || warning_option || compatible_codegen_option)
        {
            continue;
        }
        compiler_driver_argument_error(arena, &invocation, S8("unsupported option: {S8}"), argument);
        return invocation;
    }
    if (feature_override_count)
    {
        invocation.target.cpu_features = target_cpu_features_effective(invocation.target);
        invocation.target.cpu_features_explicit = true;
        for (u64 override_index = 0; override_index < feature_override_count; override_index += 1)
        {
            CompilerDriverFeatureOverride override = feature_overrides[override_index];
            TargetCpuFeature feature = target_cpu_feature_from_string(invocation.target.cpu_arch, override.name);
            if (feature == TARGET_CPU_FEATURE_NONE)
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported target feature: {S8}"), override.name);
                return invocation;
            }
            if (override.enable)
            {
                invocation.target.cpu_features = target_cpu_features_add(invocation.target.cpu_features, feature);
            }
            else
            {
                invocation.target.cpu_features = target_cpu_features_remove(invocation.target.cpu_features, feature);
            }
        }
    }
    if (!target_cpu_features_are_valid(invocation.target))
    {
        if (feature_override_count)
        {
            compiler_driver_argument_error(arena, &invocation, S8("invalid target feature combination: {S8}"),
                                           target_cpu_features_to_string(arena, invocation.target));
        }
        else
        {
            compiler_driver_argument_error(arena, &invocation, S8("CPU model is incompatible with target: {S8}"),
                                           cpu_model_to_string_os(invocation.target.cpu_model));
        }
        return invocation;
    }
    if (invocation.target.cpu_arch != CPU_ARCH_X86_64 && invocation.assembly_syntax != ASSEMBLY_SYNTAX_DEFAULT)
    {
        compiler_driver_argument_error(arena, &invocation, S8("assembly syntax is incompatible with target: {S8}"),
                                       invocation.assembly_syntax == ASSEMBLY_SYNTAX_ATT ? S8("att") : S8("intel"));
        return invocation;
    }
    if (!invocation.no_standard_includes)
    {
#if defined(BUSTER_HOST_C_RESOURCE_INCLUDE)
        if (sizeof(BUSTER_HOST_C_RESOURCE_INCLUDE) > 1)
        {
            invocation.system_include_paths[invocation.system_include_path_count++] = S8(BUSTER_HOST_C_RESOURCE_INCLUDE);
        }
#endif
        if (invocation.sysroot.length)
        {
            invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/usr/local/include"), invocation.sysroot);
            if (invocation.target.os == OPERATING_SYSTEM_LINUX || invocation.target.os == OPERATING_SYSTEM_ANDROID)
            {
                String8 multiarch = invocation.target.cpu_arch == CPU_ARCH_AARCH64
                                        ? (invocation.target.os == OPERATING_SYSTEM_ANDROID ? S8("aarch64-linux-android") : S8("aarch64-linux-gnu"))
                                        : (invocation.target.os == OPERATING_SYSTEM_ANDROID ? S8("x86_64-linux-android") : S8("x86_64-linux-gnu"));
                invocation.system_include_paths[invocation.system_include_path_count++] =
                    string_format(arena, S8("{S8}/usr/include/{S8}"), invocation.sysroot, multiarch);
            }
            else if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
            {
                invocation.system_include_paths[invocation.system_include_path_count++] =
                    string_format(arena, S8("{S8}/x86_64-w64-mingw32/include"), invocation.sysroot);
                invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/include"), invocation.sysroot);
            }
            invocation.system_include_paths[invocation.system_include_path_count++] = string_format(arena, S8("{S8}/usr/include"), invocation.sysroot);
        }
        else if (invocation.target.cpu_arch == target_native.cpu_arch && invocation.target.os == target_native.os)
        {
#if BUSTER_LINUX
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/local/include");
#if BUSTER_CPU_ARCH_X86_64
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include/x86_64-linux-gnu");
#else
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include/aarch64-linux-gnu");
#endif
            invocation.system_include_paths[invocation.system_include_path_count++] = S8("/usr/include");
#endif
#if BUSTER_WINDOWS
            String8 system_includes = os_get_environment_variable(S8("INCLUDE"));
            for (u64 start = 0; start < system_includes.length;)
            {
                u64 end = start;
                while (end < system_includes.length && system_includes.pointer[end] != ';')
                {
                    end += 1;
                }
                if (end != start)
                {
                    invocation.system_include_paths[invocation.system_include_path_count++] = string_slice(system_includes, start, end);
                }
                start = end + 1;
            }
#endif
        }
    }
    if (invocation.framework_count && invocation.target.os != OPERATING_SYSTEM_MACOS && invocation.target.os != OPERATING_SYSTEM_IOS)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("-framework is only supported for Apple targets");
    }
    else if (!invocation.input_count)
    {
        invocation.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        invocation.diagnostic = S8("no input files");
    }
    return invocation;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_c_input(CompilerDriverInvocation invocation, String8 path)
{
    if (invocation.language == COMPILER_DRIVER_LANGUAGE_C)
    {
        return true;
    }
    if (invocation.language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 2)
    {
        return false;
    }
    return path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'c' || path.pointer[path.length - 1] == 'i');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_buster_input(CompilerDriverInvocation invocation, String8 path)
{
    if (invocation.language == COMPILER_DRIVER_LANGUAGE_BUSTER)
    {
        return true;
    }
    if (invocation.language != COMPILER_DRIVER_LANGUAGE_AUTOMATIC || path.length < 4)
    {
        return false;
    }
    return string_equal(string_slice(path, path.length - 4, path.length), S8(".bbb"));
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_object_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && path.pointer[path.length - 1] == 'o')
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'o' || path.pointer[path.length - 3] == 'O') &&
           (path.pointer[path.length - 2] == 'b' || path.pointer[path.length - 2] == 'B') &&
           (path.pointer[path.length - 1] == 'j' || path.pointer[path.length - 1] == 'J');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_input(String8 path)
{
    if (path.length >= 2 && path.pointer[path.length - 2] == '.' && (path.pointer[path.length - 1] == 'a' || path.pointer[path.length - 1] == 'A'))
    {
        return true;
    }
    return path.length >= 4 && path.pointer[path.length - 4] == '.' && (path.pointer[path.length - 3] == 'l' || path.pointer[path.length - 3] == 'L') &&
           (path.pointer[path.length - 2] == 'i' || path.pointer[path.length - 2] == 'I') &&
           (path.pointer[path.length - 1] == 'b' || path.pointer[path.length - 1] == 'B');
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_archive_member_needed(ObjectFile* member, ObjectFile* selected, u32 selected_count)
{
    for (u32 member_symbol_index = 0; member_symbol_index < member->symbol_count; member_symbol_index += 1)
    {
        ObjectSymbol* member_symbol = &member->symbols[member_symbol_index];
        if (!member_symbol->global || member_symbol->section == OBJECT_SECTION_UNDEFINED)
        {
            continue;
        }
        bool unresolved = false;
        bool defined = false;
        for (u32 object_index = 0; object_index < selected_count; object_index += 1)
        {
            ObjectFile* object = &selected[object_index];
            for (u32 symbol_index = 0; symbol_index < object->symbol_count; symbol_index += 1)
            {
                ObjectSymbol* symbol = &object->symbols[symbol_index];
                if (!symbol->global || !string_equal(symbol->name, member_symbol->name))
                {
                    continue;
                }
                if (symbol->section == OBJECT_SECTION_UNDEFINED)
                {
                    unresolved = true;
                }
                else
                {
                    defined = true;
                }
            }
        }
        if (unresolved && !defined)
        {
            return true;
        }
    }
    return false;
}

typedef struct CompilerDriverDynamicLibraries CompilerDriverDynamicLibraries;
struct CompilerDriverDynamicLibraries
{
    NativeDynamicLibrary* pointer;
    NativeDynamicLibrary runtime;
    FileMapRead* export_maps;
    u32 count;
    u32 export_map_count;
};

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u16(ByteSlice bytes, u64 offset, u16* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_read_u32(ByteSlice bytes, u64 offset, u32* value)
{
    if (offset > bytes.length || sizeof(*value) > bytes.length - offset)
    {
        return false;
    }
    memcpy(value, bytes.pointer + offset, sizeof(*value));
    return true;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_pe_rva_offset(ByteSlice bytes, u64 section_table, u16 section_count, u32 rva, u64* offset_out)
{
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 section = section_table + (u64)section_index * 40;
        u32 virtual_size = 0;
        u32 virtual_address = 0;
        u32 raw_size = 0;
        u32 raw_offset = 0;
        if (!compiler_driver_read_u32(bytes, section + 8, &virtual_size) || !compiler_driver_read_u32(bytes, section + 12, &virtual_address) ||
            !compiler_driver_read_u32(bytes, section + 16, &raw_size) || !compiler_driver_read_u32(bytes, section + 20, &raw_offset))
        {
            return false;
        }
        u64 span = BUSTER_MAX(virtual_size, raw_size);
        if (rva < virtual_address || (u64)rva - virtual_address >= span)
        {
            continue;
        }
        u64 offset = raw_offset + ((u64)rva - virtual_address);
        if (offset >= bytes.length)
        {
            return false;
        }
        *offset_out = offset;
        return true;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_pe_library_exports(Arena* arena, CompilerDriverInvocation invocation, NativeDynamicLibrary* library,
                                                            FileMapRead* export_map)
{
    String8 path = {0};
    FileMapRead file = {0};
    ByteSlice bytes = {0};
    *export_map = (FileMapRead){0};

    for (u32 path_index = 0; path_index < invocation.library_path_count && !bytes.pointer; path_index += 1)
    {
        if (file.bytes.pointer)
        {
            file_map_unmap(file);
        }
        path = string_format(arena, S8("{S8}/{S8}"), invocation.library_paths[path_index], library->name);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
    if (!bytes.pointer && invocation.sysroot.length)
    {
        path = string_format(arena, S8("{S8}/Windows/System32/{S8}"), invocation.sysroot, library->name);
        file_map_unmap(file);
        file = file_map_read(arena, path, (FileReadOptions){0});
        bytes = file.bytes;
    }
#if BUSTER_WINDOWS
    if (!bytes.pointer && invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        String8 system_root = os_get_environment_variable(S8("SystemRoot"));
        if (system_root.length)
        {
            path = string_format(arena, S8("{S8}/System32/{S8}"), system_root, library->name);
            file_map_unmap(file);
            file = file_map_read(arena, path, (FileReadOptions){0});
            bytes = file.bytes;
        }
    }
#endif
    if (!bytes.pointer)
    {
        file_map_unmap(file);
        file = file_map_read(arena, library->name, (FileReadOptions){0});
        bytes = file.bytes;
    }
    u32 pe_offset = 0;
    u16 section_count = 0;
    u16 optional_size = 0;
    if (!bytes.pointer || bytes.length < 0x40 || bytes.pointer[0] != 'M' || bytes.pointer[1] != 'Z' || !compiler_driver_read_u32(bytes, 0x3c, &pe_offset) ||
        pe_offset > bytes.length || bytes.length - pe_offset < 24 || memcmp(bytes.pointer + pe_offset, "PE\0\0", 4) != 0 ||
        !compiler_driver_read_u16(bytes, pe_offset + 6, &section_count) || !compiler_driver_read_u16(bytes, pe_offset + 20, &optional_size))
    {
        file_map_unmap(file);
        return;
    }
    u64 optional = pe_offset + 24;
    u16 magic = 0;
    if (!compiler_driver_read_u16(bytes, optional, &magic))
    {
        file_map_unmap(file);
        return;
    }
    u64 directory = optional + (magic == 0x20b ? 112 : 96);
    u32 export_rva = 0;
    if ((magic != 0x20b && magic != 0x10b) || directory + 8 > optional + optional_size || !compiler_driver_read_u32(bytes, directory, &export_rva))
    {
        file_map_unmap(file);
        return;
    }
    library->exports_known = true;
    if (!export_rva)
    {
        *export_map = file;
        return;
    }
    u64 section_table = optional + optional_size;
    u64 export_offset = 0;
    u32 name_count = 0;
    u32 names_rva = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, export_rva, &export_offset) ||
        !compiler_driver_read_u32(bytes, export_offset + 24, &name_count) || !compiler_driver_read_u32(bytes, export_offset + 32, &names_rva) ||
        name_count > (bytes.length / sizeof(u32)))
    {
        file_map_unmap(file);
        return;
    }
    u64 names_offset = 0;
    if (!compiler_driver_pe_rva_offset(bytes, section_table, section_count, names_rva, &names_offset) || names_offset > bytes.length ||
        (u64)name_count * sizeof(u32) > bytes.length - names_offset)
    {
        file_map_unmap(file);
        return;
    }
    library->exported_symbols = arena_allocate(arena, String8, name_count);
    for (u32 name_index = 0; name_index < name_count; name_index += 1)
    {
        u32 name_rva = 0;
        u64 name_offset = 0;
        if (!compiler_driver_read_u32(bytes, names_offset + (u64)name_index * sizeof(u32), &name_rva) ||
            !compiler_driver_pe_rva_offset(bytes, section_table, section_count, name_rva, &name_offset))
        {
            continue;
        }
        u64 length = 0;
        while (name_offset + length < bytes.length && bytes.pointer[name_offset + length])
        {
            length += 1;
        }
        if (name_offset + length >= bytes.length)
        {
            continue;
        }
        library->exported_symbols[library->exported_symbol_count++] = (String8){
            .pointer = (char8*)bytes.pointer + name_offset,
            .length = length,
        };
    }
    *export_map = file;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_dynamic_libraries_release(CompilerDriverDynamicLibraries* libraries)
{
    for (u32 index = 0; index < libraries->export_map_count; index += 1)
    {
        file_map_unmap(libraries->export_maps[index]);
    }
}

BUSTER_GLOBAL_LOCAL CompilerDriverDynamicLibraries compiler_driver_dynamic_libraries(Arena* arena, CompilerDriverInvocation invocation, bool* static_libraries)
{
    CompilerDriverDynamicLibraries result = {0};
    static String8 const windows_system_libraries[] = {
        S8_INITIALIZER("kernel32.dll"),
        S8_INITIALIZER("user32.dll"),
        S8_INITIALIZER("gdi32.dll"),
        S8_INITIALIZER("ws2_32.dll"),
        S8_INITIALIZER("dwmapi.dll"),
        S8_INITIALIZER("shell32.dll"),
        S8_INITIALIZER("vcruntime140.dll"),
    };
    u32 default_library_count = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? BUSTER_ARRAY_LENGTH(windows_system_libraries) : 0;
    NativeDynamicLibrary* libraries =
        arena_allocate(arena, NativeDynamicLibrary, invocation.library_count + invocation.framework_count + default_library_count);
    u32 count = 0;
    for (u32 index = 0; index < default_library_count; index += 1)
    {
        libraries[count++] = (NativeDynamicLibrary){
            .name = windows_system_libraries[index],
        };
    }
    for (u32 index = 0; index < invocation.library_count; index += 1)
    {
        if (static_libraries && static_libraries[index])
        {
            continue;
        }
        String8 requested = invocation.libraries[index];
        if (!requested.length || string_equal(requested, S8("c")))
        {
            continue;
        }
        String8 name = {0};
        if (requested.pointer[0] == ':')
        {
            name = (String8){
                .pointer = requested.pointer + 1,
                .length = requested.length - 1,
            };
        }
        else if (invocation.target.os == OPERATING_SYSTEM_ANDROID)
        {
            name = string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_LINUX)
        {
            name = string_equal(requested, S8("m"))         ? S8("libm.so.6")
                   : string_equal(requested, S8("pthread")) ? S8("libpthread.so.0")
                   : string_equal(requested, S8("dl"))      ? S8("libdl.so.2")
                   : string_equal(requested, S8("rt"))      ? S8("librt.so.1")
                                                            : string_format(arena, S8("lib{S8}.so"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
        {
            if (string_equal(requested, S8("m")))
            {
                continue;
            }
            name = string_format(arena, S8("/usr/lib/lib{S8}.dylib"), requested);
        }
        else if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
        {
            bool has_dll_suffix = requested.length >= 4 && string_equal(
                                                               (String8){
                                                                   .pointer = requested.pointer + requested.length - 4,
                                                                   .length = 4,
                                                               },
                                                               S8(".dll"));
            name = has_dll_suffix ? requested : string_format(arena, S8("{S8}.dll"), requested);
        }
        else
        {
            name = requested;
        }
        if (!name.length)
        {
            continue;
        }
        bool duplicate = false;
        for (u32 previous = 0; previous < count; previous += 1)
        {
            duplicate |= string_equal(libraries[previous].name, name);
        }
        if (!duplicate)
        {
            libraries[count++] = (NativeDynamicLibrary){
                .name = name,
            };
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS)
    {
        for (u32 index = 0; index < invocation.framework_count; index += 1)
        {
            String8 framework = invocation.frameworks[index];
            if (!framework.length)
            {
                continue;
            }
            String8 root = invocation.framework_path_count ? invocation.framework_paths[0] : S8("/System/Library/Frameworks");
            String8 name = string_format(arena, S8("{S8}/{S8}.framework/{S8}"), root, framework, framework);
            bool duplicate = false;
            for (u32 previous = 0; previous < count; previous += 1)
            {
                duplicate |= string_equal(libraries[previous].name, name);
            }
            if (!duplicate)
            {
                libraries[count++] = (NativeDynamicLibrary){
                    .name = name,
                };
            }
        }
    }
    if (invocation.target.os == OPERATING_SYSTEM_WINDOWS)
    {
        result.export_maps = arena_allocate(arena, FileMapRead, count + 1);
        result.runtime.name = S8("ucrtbase.dll");
        FileMapRead* export_map = result.export_maps + result.export_map_count;
        compiler_driver_pe_library_exports(arena, invocation, &result.runtime, export_map);
        result.export_map_count += export_map->bytes.pointer != 0;
        for (u32 index = 0; index < count; index += 1)
        {
            export_map = result.export_maps + result.export_map_count;
            compiler_driver_pe_library_exports(arena, invocation, &libraries[index], export_map);
            result.export_map_count += export_map->bytes.pointer != 0;
        }
    }
    result.pointer = libraries;
    result.count = count;
    return result;
}

BUSTER_GLOBAL_LOCAL CPreprocessorDefinition compiler_driver_c_definition(String8 definition)
{
    for (u64 index = 0; index < definition.length; index += 1)
    {
        if (definition.pointer[index] == '=')
        {
            return (CPreprocessorDefinition){
                .name =
                    {
                        .pointer = definition.pointer,
                        .length = index,
                    },
                .value =
                    {
                        .pointer = definition.pointer + index + 1,
                        .length = definition.length - index - 1,
                    },
            };
        }
    }
    return (CPreprocessorDefinition){
        .name = definition,
        .value = S8("1"),
    };
}

BUSTER_GLOBAL_LOCAL ObjectArchive compiler_driver_library_archive(Arena* arena, CompilerDriverInvocation invocation, String8 requested, bool* found,
                                                                  String8* path_out)
{
    ObjectArchive result = {0};
    bool exact = requested.length && requested.pointer[0] == ':';
    String8 exact_name = exact ?
        (String8){
            .pointer = requested.pointer + 1,
            .length = requested.length - 1,
        } :
        (String8){0};
    bool exact_archive = exact && compiler_driver_archive_input(exact_name);
    for (u32 path_index = 0; path_index < invocation.library_path_count; path_index += 1)
    {
        String8 root = invocation.library_paths[path_index];
        if (!exact_archive)
        {
            String8 shared_name = invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.dll"), requested)
                                  : invocation.target.os == OPERATING_SYSTEM_MACOS || invocation.target.os == OPERATING_SYSTEM_IOS
                                      ? string_format(arena, S8("lib{S8}.dylib"), requested)
                                      : string_format(arena, S8("lib{S8}.so"), requested);
            String8 shared_path = string_format_z(arena, S8("{S8}/{S8}"), root, shared_name);
            FileMapRead shared_map = file_map_read(arena, shared_path, (FileReadOptions){0});
            ByteSlice shared = shared_map.bytes;
            if (shared.pointer)
            {
                file_map_unmap(shared_map);
                return result;
            }
            file_map_unmap(shared_map);
        }
        String8 archive_name = exact_archive                                      ? exact_name
                               : invocation.target.os == OPERATING_SYSTEM_WINDOWS ? string_format(arena, S8("{S8}.lib"), requested)
                                                                                  : string_format(arena, S8("lib{S8}.a"), requested);
        String8 archive_path = string_format_z(arena, S8("{S8}/{S8}"), root, archive_name);
        FileMapRead archive_map = file_map_read(arena, archive_path, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (!archive_bytes.pointer)
        {
            file_map_unmap(archive_map);
            continue;
        }
        *found = true;
        *path_out = archive_path;
        ObjectArchive archive = object_archive_read(arena, archive_bytes, invocation.target);
        file_map_unmap(archive_map);
        return archive;
    }
    if (exact_archive)
    {
        FileMapRead archive_map = file_map_read(arena, exact_name, (FileReadOptions){0});
        ByteSlice archive_bytes = archive_map.bytes;
        if (archive_bytes.pointer)
        {
            *found = true;
            *path_out = exact_name;
            ObjectArchive archive = object_archive_read(arena, archive_bytes, invocation.target);
            file_map_unmap(archive_map);
            return archive;
        }
        file_map_unmap(archive_map);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_preprocess_text(Arena* arena, CPreprocessResult preprocess)
{
    u64 capacity = 1;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        if (preprocess.tokens[index].kind != C_TOKEN_END_OF_FILE)
        {
            capacity += preprocess.tokens[index].length + 1;
        }
    }
    char8* text = arena_allocate(arena, char8, capacity);
    u64 length = 0;
    for (u64 index = 0; index < preprocess.token_count; index += 1)
    {
        CToken token = preprocess.tokens[index];
        if (token.kind == C_TOKEN_END_OF_FILE)
        {
            break;
        }
        if (length)
        {
            text[length++] = ' ';
        }
        String8 spelling = c_token_spelling(preprocess.spelling_base, token);
        memcpy(text + length, spelling.pointer, spelling.length);
        length += spelling.length;
    }
    text[length++] = '\n';
    text[length] = 0;
    return (String8){
        .pointer = text,
        .length = length,
    };
}

typedef struct CompilerDriverWarningChunk CompilerDriverWarningChunk;
struct CompilerDriverWarningChunk
{
    CompilerDriverWarningChunk* next;
    String8 text;
};

typedef struct CompilerDriverWarningCollector CompilerDriverWarningCollector;
struct CompilerDriverWarningCollector
{
    Arena* arena;
    CompilerDriverWarningChunk* first;
    CompilerDriverWarningChunk* last;
    u64 length;
};

BUSTER_GLOBAL_LOCAL void compiler_driver_warning_append_text(CompilerDriverWarningCollector* collector, String8 text)
{
    if (!collector || !collector->arena || !text.length)
    {
        return;
    }
    CompilerDriverWarningChunk* chunk = arena_allocate(collector->arena, CompilerDriverWarningChunk, 1);
    *chunk = (CompilerDriverWarningChunk){
        .text = text,
    };
    if (collector->last)
    {
        collector->last->next = chunk;
    }
    else
    {
        collector->first = chunk;
    }
    collector->last = chunk;
    collector->length += text.length;
}

BUSTER_GLOBAL_LOCAL void compiler_driver_append_warning(CompilerDriverWarningCollector* collector, String8 path, CDiagnostic diagnostic)
{
    if (!collector || !collector->arena || diagnostic.severity != C_DIAGNOSTIC_WARNING)
    {
        return;
    }
    String8 formatted = string_format(collector->arena, S8("{S8}:{u32}:{u32}: warning: {S8}\n"), path, diagnostic.location.line, diagnostic.location.column,
                                      diagnostic.message);
    compiler_driver_warning_append_text(collector, formatted);
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_warning_flatten(CompilerDriverWarningCollector collector)
{
    if (!collector.arena || !collector.length)
    {
        return (String8){0};
    }
    char8* text = arena_allocate(collector.arena, char8, collector.length + 1);
    u64 offset = 0;
    for (CompilerDriverWarningChunk* chunk = collector.first; chunk; chunk = chunk->next)
    {
        memcpy(text + offset, chunk->text.pointer, chunk->text.length);
        offset += chunk->text.length;
    }
    text[offset] = 0;
    return (String8){
        .pointer = text,
        .length = offset,
    };
}

BUSTER_GLOBAL_LOCAL CDiagnostic* compiler_driver_first_preprocess_error(CPreprocessResult preprocess)
{
    for (u64 diagnostic_index = 0; diagnostic_index < preprocess.diagnostic_count; diagnostic_index += 1)
    {
        CDiagnostic* diagnostic = preprocess.diagnostics + diagnostic_index;
        if (diagnostic->severity != C_DIAGNOSTIC_WARNING)
        {
            return diagnostic;
        }
    }
    return 0;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input);

static CompilerDriverResult compiler_driver_execute_c_single(Arena* arena, CompilerDriverInvocation invocation, bool suppress_object_write,
                                                             CompilerDriverWarningCollector* warnings)
{
    CompilerDriverResult result = {
        .error = invocation.error,
        .diagnostic = invocation.diagnostic,
    };
    FileMapRead source_file = {0};
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return result;
    }
    if (invocation.input_count != 1 || !compiler_driver_c_input(invocation, invocation.input_paths[0]))
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the C frontend currently requires exactly one C input");
        goto end;
    }
    source_file = file_map_read(arena, invocation.input_paths[0], (FileReadOptions){0});
    ByteSlice bytes = source_file.bytes;
    if (!bytes.pointer)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not read {S8}"), invocation.input_paths[0]);
        goto end;
    }
    CPreprocessorDefinition* definitions = arena_allocate(arena, CPreprocessorDefinition, invocation.definition_count);
    for (u32 index = 0; index < invocation.definition_count; index += 1)
    {
        definitions[index] = compiler_driver_c_definition(invocation.definitions[index]);
    }
    CPreprocessResult preprocess = c_preprocess(arena, BYTE_SLICE_TO_STRING(8, bytes),
                                                (CPreprocessOptions){
                                                    .definitions = definitions,
                                                    .undefinitions = invocation.undefinitions,
                                                    .include_paths = invocation.include_paths,
                                                    .system_include_paths = invocation.system_include_paths,
                                                    .source_path = invocation.input_paths[0],
                                                    .target = invocation.target,
                                                    .data_layout = target_data_layout(invocation.target),
                                                    .dialect = compiler_driver_preprocess_dialect(invocation.c_dialect),
                                                    .definition_count = invocation.definition_count,
                                                    .undefinition_count = invocation.undefinition_count,
                                                    .include_path_count = invocation.include_path_count,
                                                    .system_include_path_count = invocation.system_include_path_count,
                                                });
    // Reported even when a later stage fails: the units the frontend read are
    // measured by then, and a failing compile is exactly when the size of
    // what it read is worth knowing.
    result.source_lexed = preprocess.source_lexed;
    result.source_unique = preprocess.source_unique;
    result.preprocessed = preprocess.preprocessed;
    for (u64 diagnostic_index = 0; diagnostic_index < preprocess.diagnostic_count; diagnostic_index += 1)
    {
        compiler_driver_append_warning(warnings, invocation.input_paths[0], preprocess.diagnostics[diagnostic_index]);
    }
    result.tokenizer_warning_count = (u32)preprocess.warning_count;
    if (preprocess.error_count)
    {
        CDiagnostic* diagnostic = compiler_driver_first_preprocess_error(preprocess);
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.error_count;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic->location.line, diagnostic->location.column,
                                          diagnostic->message);
        goto end;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = compiler_driver_preprocess_text(arena, preprocess);
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        goto end;
    }
    CParserResult syntax = c_parse_ast(arena, preprocess);
    result.parser_diagnostic_count = syntax.diagnostic_count;
    if (syntax.diagnostic_count)
    {
        CDiagnostic diagnostic = syntax.diagnostics[0];
        result.error = COMPILER_DRIVER_ERROR_PARSE;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line, diagnostic.location.column,
                                          diagnostic.message);
        goto end;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        CIRLowerResult semantic = c_analyze(arena, invocation.input_paths[0], preprocess, syntax, invocation.target);
        result.analysis_diagnostic_count = semantic.diagnostic_count;
        if (semantic.diagnostic_count || !semantic.program)
        {
            result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
            if (semantic.diagnostic_count)
            {
                CDiagnostic diagnostic = semantic.diagnostics[0];
                result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line,
                                                  diagnostic.location.column, diagnostic.message);
            }
            else
            {
                result.diagnostic = string_format(arena, S8("{S8}: semantic validation failed"), invocation.input_paths[0]);
            }
        }
        goto end;
    }
    CIRLowerResult lowered = c_analyze(arena, invocation.input_paths[0], preprocess, syntax, invocation.target);
    result.analysis_diagnostic_count = lowered.diagnostic_count;
    if (!lowered.program || lowered.diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
        if (lowered.diagnostic_count)
        {
            CDiagnostic diagnostic = lowered.diagnostics[0];
            result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line,
                                              diagnostic.location.column, diagnostic.message);
        }
        goto end;
    }
    IrModule* module = &lowered.program->modules[0];
    IrValidationResult validation = ir_validate_canonical_module(lowered.program, module);
    if (validation.error != IR_VALIDATION_NONE)
    {
        String8 function_name = validation.function.value < module->function_count ? module->functions[validation.function.value].name : S8("<invalid>");
        u32 opcode = IR_OPCODE_COUNT;
        if (validation.function.value < module->function_count)
        {
            IrFunction* failed_function = &module->functions[validation.function.value];
            if (validation.instruction.value < failed_function->instruction_count)
            {
                opcode = (u32)failed_function->instructions[validation.instruction.value].opcode;
            }
        }
        result.error = COMPILER_DRIVER_ERROR_IR;
        result.diagnostic =
            string_format(arena, S8("canonical C IR validation failed: error {u32}, function {u32} ('{S8}'), block {u32}, instruction {u32}, opcode {u32}"),
                          (u32)validation.error, validation.function.value, function_name, validation.block.value, validation.instruction.value, opcode);
        goto end;
    }
    CodegenModule code = codegen_generate_canonical_module(arena, lowered.program, module, invocation.target,
                                                           (CodegenModuleOptions){
                                                               .debug_info = invocation.debug_info,
                                                               .assume_validated = true,
                                                               .register_allocator = invocation.register_allocator,
                                                           });
    result.codegen_statistics = code.statistics;
    result.codegen_error = code.error;
    if (code.error != CODEGEN_ERROR_NONE)
    {
        String8 function_name = code.failed_function.value < module->function_count ? module->functions[code.failed_function.value].name : S8("<none>");
        u32 operation = IR_BINARY_COUNT;
        u32 source_line = 0;
        u32 source_column = 0;
        u32 function_state = IR_FUNCTION_STATE_COUNT;
        u32 function_block_count = 0;
        u32 function_instruction_count = 0;
        String8 referenced_symbol = S8("<none>");
        if (code.failed_function.value < module->function_count)
        {
            IrFunction* failed_function = &module->functions[code.failed_function.value];
            function_state = (u32)failed_function->state;
            function_block_count = failed_function->block_count;
            function_instruction_count = failed_function->instruction_count;
            if (code.failed_instruction.value < failed_function->instruction_count)
            {
                IrInstruction* failed_instruction = &failed_function->instructions[code.failed_instruction.value];
                ParserSourceRange failed_source = ir_instruction_source(failed_function, failed_instruction->id);
                source_line = failed_source.line;
                source_column = failed_source.column;
                operation = failed_instruction->opcode == IR_OPCODE_BINARY  ? (u32)failed_instruction->binary_operation
                            : failed_instruction->opcode == IR_OPCODE_UNARY ? (u32)failed_instruction->unary_operation
                                                                            : (u32)IR_BINARY_COUNT;
                if (failed_instruction->opcode == IR_OPCODE_CALL && failed_instruction->operand_count &&
                    failed_instruction->operands[0].value < failed_function->value_count)
                {
                    IrInstructionId definition = failed_function->values[failed_instruction->operands[0].value].definition;
                    if (definition.value < failed_function->instruction_count)
                    {
                        IrInstruction* reference = &failed_function->instructions[definition.value];
                        IrSymbol* symbol = ir_symbol_from_id(&lowered.program->symbols, reference->symbol);
                        if (reference->opcode == IR_OPCODE_FUNCTION && symbol)
                        {
                            referenced_symbol = symbol->link_name;
                        }
                    }
                }
            }
        }
        result.error = COMPILER_DRIVER_ERROR_CODEGEN;
        result.diagnostic =
            string_format(arena,
                          S8("C code generation failed with error {u32}, function {u32} ('{S8}', state {u32}, blocks {u32}, instructions {u32}), instruction "
                             "{u32}, opcode {u32}, operation {u32}, source {u32}:{u32}, referenced symbol '{S8}'"),
                          (u32)code.error, code.failed_function.value, function_name, function_state, function_block_count, function_instruction_count,
                          code.failed_instruction.value, (u32)code.failed_opcode, operation, source_line, source_column, referenced_symbol);
        goto end;
    }
    ObjectFile object = object_from_canonical_codegen_module(arena, lowered.program, &code, invocation.target);
    result.object_error = object.error;
    if (object.error != OBJECT_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_OBJECT;
        result.diagnostic = string_format(arena, S8("C object generation failed with error {u32}"), (u32)object.error);
        goto end;
    }
    result.object = object;
    result.has_object = true;
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = object_print_assembly(arena, &object);
        if (!result.output.length)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = S8("could not format native object as textual assembly");
            return result;
        }
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
    {
        if (suppress_object_write)
        {
            goto end;
        }
        ObjectArtifact artifact = object_write(arena, &object, object_format_for_target(invocation.target));
        if (artifact.error != OBJECT_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = artifact.error;
            goto end;
        }
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        if (!file_write(output, artifact.bytes))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), output);
        }
        goto end;
    }
    LinkObjectResult linked = link_objects(arena, &object, 1,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        goto end;
    }
    String8 output = invocation.output_path.length ? invocation.output_path :
#if BUSTER_WINDOWS
                                                   S8("a.exe");
#else
                                                   S8("a.out");
#endif
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_dynamic_libraries(arena, invocation, 0);
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = S8("main"),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("native C link failed with error {u32}: {S8}"), (u32)result.native_link.error, result.native_link.symbol);
    }
end:
    file_map_unmap(source_file);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input)
{
    u64 extension = input.length;
    for (u64 index = input.length; index != 0; index -= 1)
    {
        char8 byte = input.pointer[index - 1];
        if (byte == '.')
        {
            extension = index - 1;
            break;
        }
        if (byte == '/' || byte == '\\')
        {
            break;
        }
    }
    return string_format_z(arena, S8("{S8}.o"), (String8){
                                                           .pointer = input.pointer,
                                                           .length = extension,
                                                       });
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_parser_diagnostic(Arena* arena, String8 path, ParserDiagnostic* diagnostic);

static CompilerDriverResult compiler_driver_execute_buster(Arena* arena, CompilerDriverInvocation invocation)
{
    CompilerDriverResult result = {
        .error = invocation.error,
        .diagnostic = invocation.diagnostic,
    };
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = S8("Buster input does not support preprocessing");
        return result;
    }
    if (invocation.input_count != 1 || !compiler_driver_buster_input(invocation, invocation.input_paths[0]))
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the Buster frontend currently requires exactly one .bbb input");
        return result;
    }

    Arena* expression_arena = arena_create((ArenaCreation){0});
    if (!expression_arena)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("could not allocate Buster expression arena");
        return result;
    }
    TargetDataLayout data_layout = target_data_layout(invocation.target);
    AnalysisProgram analysis = analysis_program_load(arena, expression_arena,
                                                     (AnalysisProgramOptions){
                                                         .root_path = invocation.input_paths[0],
                                                         .module_root = invocation.module_root.length ? invocation.module_root : S8("."),
                                                         .data_layout = data_layout,
                                                         .pointer_size = data_layout.pointer.size,
                                                         .pointer_alignment = data_layout.pointer.alignment,
                                                     });
    result.parser_diagnostic_count = analysis.parser_diagnostic_count;
    result.analysis_diagnostic_count = analysis.analysis_diagnostic_count;
    if (analysis.load_failed)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not load {S8} or one of its imported modules"), invocation.input_paths[0]);
        goto cleanup;
    }
    if (analysis.parser_diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_PARSE;
        for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
        {
            AnalysisProgramModule* module = &analysis.modules[module_index];
            if (module->parser.first_diagnostic)
            {
                result.diagnostic = compiler_driver_parser_diagnostic(arena, module->path, module->parser.first_diagnostic);
                break;
            }
        }
        if (!result.diagnostic.length)
        {
            result.diagnostic = string_format(arena, S8("Buster parsing failed with {u32} diagnostic(s)"), analysis.parser_diagnostic_count);
        }
        goto cleanup;
    }
    if (analysis.analysis_diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
        for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
        {
            AnalysisResult* module = analysis.module_results[module_index];
            if (module && module->first_diagnostic)
            {
                result.diagnostic = analysis_format_diagnostic(arena, module, module->first_diagnostic);
                break;
            }
        }
        goto cleanup;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY)
    {
        goto cleanup;
    }

    IrProgram ir = ir_generate_program(arena, &analysis);
    ObjectFile* objects = arena_allocate(arena, ObjectFile, analysis.module_count);
    u32 object_count = 0;
    for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
    {
        AnalysisResult* module_analysis = analysis.module_results[module_index];
        if (!module_analysis)
        {
            continue;
        }
        IrModule* module_ir = &ir.modules[module_index];
        IrValidationResult validation = ir_validate_module(module_analysis, module_ir);
        if (validation.error != IR_VALIDATION_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_IR;
            result.diagnostic = string_format(arena, S8("Buster IR validation failed in module {S8} with error {u32}"), module_analysis->module.name,
                                              (u32)validation.error);
            goto cleanup;
        }
        CodegenModule code = codegen_generate_module(arena, module_analysis, module_ir, invocation.target,
                                                     (CodegenModuleOptions){
                                                         .debug_info = invocation.debug_info,
                                                         .register_allocator = invocation.register_allocator,
                                                     });
        result.codegen_statistics = code.statistics;
        result.codegen_error = code.error;
        if (code.error != CODEGEN_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_CODEGEN;
            result.diagnostic = string_format(arena, S8("Buster native code generation failed in module {S8} with error {u32}"), module_analysis->module.name,
                                              (u32)code.error);
            goto cleanup;
        }
        ObjectFile object = object_from_codegen_module(arena, module_analysis, &code, invocation.target);
        result.object_error = object.error;
        if (object.error != OBJECT_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = string_format(arena, S8("Buster object generation failed in module {S8} with error {u32}"), module_analysis->module.name,
                                              (u32)object.error);
            goto cleanup;
        }
        objects[object_count++] = object;
    }
    if (!object_count)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the Buster program contains no compilable modules");
        goto cleanup;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("Buster object linking failed with error {u32}: {S8}"), (u32)linked.error, linked.symbol);
        goto cleanup;
    }
    result.object = linked.object;
    result.has_object = true;
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = object_print_assembly(arena, &linked.object);
        if (!result.output.length)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = S8("could not format Buster object as textual assembly");
            goto cleanup;
        }
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        goto cleanup;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
    {
        ObjectArtifact artifact = object_write(arena, &linked.object, object_format_for_target(invocation.target));
        if (artifact.error != OBJECT_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = artifact.error;
            goto cleanup;
        }
        String8 output = invocation.output_path.length ? invocation.output_path : compiler_driver_default_object_path(arena, invocation.input_paths[0]);
        if (!file_write(output, artifact.bytes))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), output);
        }
        goto cleanup;
    }
    String8 output = invocation.output_path.length ? invocation.output_path :
#if BUSTER_WINDOWS
                                                   S8("a.exe");
#else
                                                   S8("a.out");
#endif
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_dynamic_libraries(arena, invocation, 0);
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = S8("main"),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("Buster native executable linking failed with error {u32}: {S8}"),
                                          (u32)result.native_link.error, result.native_link.symbol);
    }

cleanup:
    analysis_program_unmap_sources(&analysis);
    arena_destroy(expression_arena, 1);
    return result;
}

CompilerDriverResult compiler_driver_execute_invocation(Arena* arena, CompilerDriverInvocation invocation)
{
    CompilerDriverWarningCollector warnings = {
        .arena = arena,
    };
    CompilerDriverResult result = {0};
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        result = compiler_driver_execute_c_single(arena, invocation, false, &warnings);
        goto finish;
    }
    bool has_buster_input = false;
    bool has_c_input = false;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 path = invocation.input_paths[input_index];
        if (compiler_driver_object_input(path) || compiler_driver_archive_input(path))
        {
            continue;
        }
        has_buster_input |= compiler_driver_buster_input(invocation, path);
        has_c_input |= compiler_driver_c_input(invocation, path);
    }
    if (has_buster_input)
    {
        if (has_c_input)
        {
            result = (CompilerDriverResult){
                .error = COMPILER_DRIVER_ERROR_INVALID_INPUT,
                .diagnostic = S8("cannot mix C and Buster inputs in one invocation"),
            };
            goto finish;
        }
        result = compiler_driver_execute_buster(arena, invocation);
        goto finish;
    }
    if (invocation.input_count <= 1 && !invocation.library_count &&
        (!invocation.input_count || (!compiler_driver_object_input(invocation.input_paths[0]) && !compiler_driver_archive_input(invocation.input_paths[0]))))
    {
        result = compiler_driver_execute_c_single(arena, invocation, false, &warnings);
        goto finish;
    }
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        bool object_input = compiler_driver_object_input(invocation.input_paths[input_index]);
        bool archive_input = compiler_driver_archive_input(invocation.input_paths[input_index]);
        if ((object_input || archive_input) && invocation.action != COMPILER_DRIVER_ACTION_LINK)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("prebuilt input {S8} is only valid while linking"), invocation.input_paths[input_index]);
            goto finish;
        }
        if (!object_input && !archive_input && !compiler_driver_c_input(invocation, invocation.input_paths[input_index]))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("unsupported C input {S8}"), invocation.input_paths[input_index]);
            goto finish;
        }
    }
    if ((invocation.action == COMPILER_DRIVER_ACTION_OBJECT || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
         invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY) && invocation.output_path.length)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = invocation.action == COMPILER_DRIVER_ACTION_OBJECT   ? S8("cannot specify -o with -c and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ? S8("cannot specify -o with -S and multiple input files")
                                                                                     : S8("cannot specify -o with -fsyntax-only and multiple input files");
        goto finish;
    }
    ObjectArchive* input_archives = arena_allocate(arena, ObjectArchive, invocation.input_count);
    u32 object_capacity = invocation.input_count;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 input_path = invocation.input_paths[input_index];
        if (!compiler_driver_archive_input(input_path))
        {
            continue;
        }
        FileMapRead archive_file = file_map_read(arena, input_path, (FileReadOptions){0});
        if (!archive_file.bytes.pointer)
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not read {S8}"), input_path);
            file_map_unmap(archive_file);
            goto finish;
        }
        input_archives[input_index] = object_archive_read(arena, archive_file.bytes, invocation.target);
        file_map_unmap(archive_file);
        if (input_archives[input_index].error != OBJECT_ERROR_NONE || input_archives[input_index].object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = input_archives[input_index].error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), input_path, (u32)input_archives[input_index].error);
            goto finish;
        }
        object_capacity += input_archives[input_index].object_count;
    }
    ObjectArchive* library_archives = arena_allocate(arena, ObjectArchive, invocation.library_count);
    bool* static_libraries = arena_allocate(arena, bool, invocation.library_count);
    memset(static_libraries, 0, sizeof(*static_libraries) * invocation.library_count);
    for (u32 library_index = 0; library_index < invocation.library_count; library_index += 1)
    {
        bool found = false;
        String8 archive_path = {0};
        ObjectArchive archive = compiler_driver_library_archive(arena, invocation, invocation.libraries[library_index], &found, &archive_path);
        if (!found)
        {
            continue;
        }
        static_libraries[library_index] = true;
        library_archives[library_index] = archive;
        if (archive.error != OBJECT_ERROR_NONE || archive.object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = archive.error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), archive_path, (u32)archive.error);
            goto finish;
        }
        object_capacity += archive.object_count;
    }
    ObjectFile* objects = arena_allocate(arena, ObjectFile, object_capacity);
    String8* preprocessed = arena_allocate(arena, String8, invocation.input_count);
    u32 object_count = 0;
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        String8 input_path = invocation.input_paths[input_index];
        if (compiler_driver_archive_input(input_path))
        {
            ObjectArchive* archive = &input_archives[input_index];
            bool* selected = arena_allocate(arena, bool, archive->object_count);
            memset(selected, 0, sizeof(*selected) * archive->object_count);
            bool added = false;
            do
            {
                added = false;
                for (u32 member_index = 0; member_index < archive->object_count; member_index += 1)
                {
                    if (selected[member_index] || !compiler_driver_archive_member_needed(&archive->objects[member_index], objects, object_count))
                    {
                        continue;
                    }
                    selected[member_index] = true;
                    objects[object_count++] = archive->objects[member_index];
                    added = true;
                }
            } while (added);
            continue;
        }
        if (compiler_driver_object_input(input_path))
        {
            FileMapRead object_file = file_map_read(arena, input_path, (FileReadOptions){0});
            if (!object_file.bytes.pointer)
            {
                result.error = COMPILER_DRIVER_ERROR_FILE_READ;
                result.diagnostic = string_format(arena, S8("could not read {S8}"), input_path);
                file_map_unmap(object_file);
                goto finish;
            }
            ObjectFile object = object_read(arena, object_file.bytes, invocation.target);
            file_map_unmap(object_file);
            if (object.error != OBJECT_ERROR_NONE)
            {
                result.error = COMPILER_DRIVER_ERROR_OBJECT;
                result.object_error = object.error;
                result.diagnostic = string_format(arena, S8("could not read object {S8}: error {u32}"), input_path, (u32)object.error);
                goto finish;
            }
            objects[object_count++] = object;
            continue;
        }
        CompilerDriverInvocation single = invocation;
        single.input_paths = invocation.input_paths + input_index;
        single.input_count = 1;
        single.output_path = (String8){0};
        bool suppress_object_write = invocation.action == COMPILER_DRIVER_ACTION_LINK;
        if (invocation.action == COMPILER_DRIVER_ACTION_OBJECT)
        {
            String8 input = invocation.input_paths[input_index];
            u64 extension = input.length;
            for (u64 index = input.length; index != 0; index -= 1)
            {
                char8 byte = input.pointer[index - 1];
                if (byte == '.')
                {
                    extension = index - 1;
                    break;
                }
                if (byte == '/' || byte == '\\')
                {
                    break;
                }
            }
            single.output_path = string_format(arena, S8("{S8}.o"),
                                               (String8){
                                                   .pointer = input.pointer,
                                                   .length = extension,
                                               });
        }
        else if (invocation.action == COMPILER_DRIVER_ACTION_LINK)
        {
            single.action = COMPILER_DRIVER_ACTION_OBJECT;
        }
        Arena* unit_arena = arena_create((ArenaCreation){
            .reserved_size = COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE,
            // Per-unit arenas churn once per translation unit (and once per
            // driver test fixture); reusing the parked mapping keeps its
            // already-faulted pages instead of paying mmap + first-touch
            // zeroing + munmap every time. The C pipeline never assumes
            // zeroed arena memory.
            .flags = {.pool_reuse = 1},
        });
        if (!unit_arena)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("could not allocate C translation-unit arena");
            goto finish;
        }
        CompilerDriverResult unit = compiler_driver_execute_c_single(unit_arena, single, suppress_object_write, &warnings);
        result.tokenizer_error_count += unit.tokenizer_error_count;
        result.tokenizer_warning_count += unit.tokenizer_warning_count;
        result.parser_diagnostic_count += unit.parser_diagnostic_count;
        result.analysis_diagnostic_count += unit.analysis_diagnostic_count;
        // Each unit dedups its own include closure, so across several inputs
        // the unique aggregate is a sum of per-unit uniques and still counts
        // a shared header once per unit that included it.
        c_source_metrics_add(&result.source_lexed, &unit.source_lexed);
        c_source_metrics_add(&result.source_unique, &unit.source_unique);
        result.preprocessed.tokens += unit.preprocessed.tokens;
        result.preprocessed.bytes += unit.preprocessed.bytes;
        result.preprocessed.spelling_bytes += unit.preprocessed.spelling_bytes;
        result.preprocessed.expansions += unit.preprocessed.expansions;
        result.preprocessed.definitions += unit.preprocessed.definitions;
        result.codegen_statistics.instruction_count += unit.codegen_statistics.instruction_count;
        result.codegen_statistics.value_count += unit.codegen_statistics.value_count;
        result.codegen_statistics.stack_value_bytes += unit.codegen_statistics.stack_value_bytes;
        result.codegen_statistics.stack_frame_bytes += unit.codegen_statistics.stack_frame_bytes;
        result.codegen_statistics.code_bytes += unit.codegen_statistics.code_bytes;
        result.codegen_statistics.native_vector_operation_count += unit.codegen_statistics.native_vector_operation_count;
        result.codegen_statistics.split_vector_operation_count += unit.codegen_statistics.split_vector_operation_count;
        result.codegen_statistics.vzeroupper_count += unit.codegen_statistics.vzeroupper_count;
        result.codegen_statistics.forwarded_wide_vector_load_count += unit.codegen_statistics.forwarded_wide_vector_load_count;
        result.codegen_statistics.simd_operation_count += unit.codegen_statistics.simd_operation_count;
        result.codegen_statistics.function_count += unit.codegen_statistics.function_count;
        result.codegen_statistics.maximum_stack_frame_bytes =
            BUSTER_MAX(result.codegen_statistics.maximum_stack_frame_bytes, unit.codegen_statistics.maximum_stack_frame_bytes);
        if (unit.error != COMPILER_DRIVER_ERROR_NONE)
        {
            if (unit.diagnostic.length)
            {
                result.diagnostic = string_duplicate_arena(arena, unit.diagnostic, false);
            }
            result.error = unit.error;
            result.codegen_error = unit.codegen_error;
            result.object_error = unit.object_error;
            arena_destroy(unit_arena, 1);
            goto finish;
        }
        if (unit.has_object)
        {
            ObjectFile object = {
                .target = unit.object.target,
                .error = unit.object.error,
                .section_count = unit.object.section_count,
                .symbol_count = unit.object.symbol_count,
                .relocation_count = unit.object.relocation_count,
                .debug_module_count = unit.object.debug_module_count,
            };
            object.sections = arena_allocate(arena, ObjectSection, object.section_count);
            for (u32 section_index = 0; section_index < object.section_count; section_index += 1)
            {
                ObjectSection source = unit.object.sections[section_index];
                ObjectSection* destination = &object.sections[section_index];
                *destination = source;
                destination->name = string_duplicate_arena(arena, source.name, false);
                destination->data.pointer = arena_allocate(arena, u8, source.data.length);
                if (source.data.length)
                {
                    memcpy(destination->data.pointer, source.data.pointer, source.data.length);
                }
            }
            object.symbols = arena_allocate(arena, ObjectSymbol, object.symbol_count);
            for (u32 symbol_index = 0; symbol_index < object.symbol_count; symbol_index += 1)
            {
                object.symbols[symbol_index] = unit.object.symbols[symbol_index];
                object.symbols[symbol_index].name = string_duplicate_arena(arena, unit.object.symbols[symbol_index].name, false);
            }
            object.relocations = arena_allocate(arena, ObjectRelocation, object.relocation_count);
            if (object.relocation_count)
            {
                memcpy(object.relocations, unit.object.relocations, sizeof(ObjectRelocation) * object.relocation_count);
            }
            object.debug_modules = arena_allocate(arena, ObjectDebugModule, object.debug_module_count);
            for (u32 module_index = 0; module_index < object.debug_module_count; module_index += 1)
            {
                object.debug_modules[module_index] = unit.object.debug_modules[module_index];
                object.debug_modules[module_index].name = string_duplicate_arena(arena, unit.object.debug_modules[module_index].name, false);
            }
            objects[object_count++] = object;
        }
        if (unit.output.length)
        {
            preprocessed[input_index] = string_duplicate_arena(arena, unit.output, false);
        }
        arena_destroy(unit_arena, 1);
    }
    for (u32 library_index = 0; library_index < invocation.library_count; library_index += 1)
    {
        if (!static_libraries[library_index])
        {
            continue;
        }
        ObjectArchive* archive = &library_archives[library_index];
        bool* selected = arena_allocate(arena, bool, archive->object_count);
        memset(selected, 0, sizeof(*selected) * archive->object_count);
        bool added = false;
        do
        {
            added = false;
            for (u32 member_index = 0; member_index < archive->object_count; member_index += 1)
            {
                if (selected[member_index] || !compiler_driver_archive_member_needed(&archive->objects[member_index], objects, object_count))
                {
                    continue;
                }
                selected[member_index] = true;
                objects[object_count++] = archive->objects[member_index];
                added = true;
            }
        } while (added);
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_PREPROCESS)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        if (invocation.output_path.length && !file_write(invocation.output_path, BUSTER_SLICE_TO_BYTE_SLICE(result.output)))
        {
            result.error = COMPILER_DRIVER_ERROR_FILE_READ;
            result.diagnostic = string_format(arena, S8("could not write {S8}"), invocation.output_path);
        }
        goto finish;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        goto finish;
    }
    if (invocation.action != COMPILER_DRIVER_ACTION_LINK)
    {
        goto finish;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        goto finish;
    }
    result.object = linked.object;
    result.has_object = true;
    String8 output = invocation.output_path.length ? invocation.output_path :
#if BUSTER_WINDOWS
                                                   S8("a.exe");
#else
                                                   S8("a.out");
#endif
    CompilerDriverDynamicLibraries dynamic_libraries = compiler_driver_dynamic_libraries(arena, invocation, static_libraries);
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = output,
                                                    .entry_symbol = S8("main"),
                                                    .sysroot = invocation.sysroot,
                                                    .library_paths = invocation.library_paths,
                                                    .framework_paths = invocation.framework_paths,
                                                    .frameworks = invocation.frameworks,
                                                    .linker_arguments = invocation.linker_arguments,
                                                    .library_path_count = invocation.library_path_count,
                                                    .framework_path_count = invocation.framework_path_count,
                                                    .framework_count = invocation.framework_count,
                                                    .linker_argument_count = invocation.linker_argument_count,
                                                    .dynamic_libraries = dynamic_libraries.pointer,
                                                    .dynamic_library_count = dynamic_libraries.count,
                                                    .runtime_exported_symbols = dynamic_libraries.runtime.exported_symbols,
                                                    .runtime_exported_symbol_count = dynamic_libraries.runtime.exported_symbol_count,
                                                    .runtime_exports_known = dynamic_libraries.runtime.exports_known,
                                                    .debug_info = invocation.debug_info,
                                                });
    compiler_driver_dynamic_libraries_release(&dynamic_libraries);
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("native C link failed with error {u32}: {S8}"), (u32)result.native_link.error, result.native_link.symbol);
    }
finish:
    result.warning = compiler_driver_warning_flatten(warnings);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_parser_diagnostic(Arena* arena, String8 path, ParserDiagnostic* diagnostic)
{
    if (!diagnostic)
    {
        return string_format(arena, S8("{S8}: parsing failed"), path);
    }
    return string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), path, diagnostic->range.line, diagnostic->range.column, diagnostic->message);
}

CompilerDriverResult compiler_driver_compile(Arena* arena, CompilerDriverOptions options)
{
    CompilerDriverResult result = {0};
    if (!arena || !options.source_path.length || !options.output_path.length)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        return result;
    }
    if (options.target.cpu_arch >= CPU_ARCH_COUNT || options.target.os >= OPERATING_SYSTEM_COUNT)
    {
        options.target = target_native;
    }
    TargetDataLayout data_layout = target_data_layout(options.target);
    Arena* expression_arena = arena_create((ArenaCreation){0});
    if (!expression_arena)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        return result;
    }
    AnalysisProgram analysis = analysis_program_load(arena, expression_arena,
                                                     (AnalysisProgramOptions){
                                                         .root_path = options.source_path,
                                                         .module_root = options.module_root.length ? options.module_root : S8("."),
                                                         .data_layout = data_layout,
                                                         .pointer_size = data_layout.pointer.size,
                                                         .pointer_alignment = data_layout.pointer.alignment,
                                                     });
    arena_destroy(expression_arena, 1);
    result.parser_diagnostic_count = analysis.parser_diagnostic_count;
    result.analysis_diagnostic_count = analysis.analysis_diagnostic_count;
    if (analysis.load_failed)
    {
        result.error = COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(arena, S8("could not load {S8} or one of its imported modules"), options.source_path);
        return result;
    }
    if (analysis.parser_diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_PARSE;
        for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
        {
            AnalysisProgramModule* module = &analysis.modules[module_index];
            if (module->parser.first_diagnostic)
            {
                result.diagnostic = compiler_driver_parser_diagnostic(arena, module->path, module->parser.first_diagnostic);
                break;
            }
        }
        if (!result.diagnostic.length)
        {
            result.diagnostic = string_format(arena, S8("tokenization failed with {u32} error(s)"), analysis.parser_diagnostic_count);
        }
        return result;
    }
    if (analysis.analysis_diagnostic_count)
    {
        result.error = COMPILER_DRIVER_ERROR_ANALYSIS;
        for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
        {
            AnalysisResult* module = analysis.module_results[module_index];
            if (module && module->first_diagnostic)
            {
                result.diagnostic = analysis_format_diagnostic(arena, module, module->first_diagnostic);
                break;
            }
        }
        return result;
    }
    IrProgram ir = ir_generate_program(arena, &analysis);
    ObjectFile* objects = arena_allocate(arena, ObjectFile, analysis.module_count);
    u32 object_count = 0;
    for (u32 module_index = 0; module_index < analysis.module_count; module_index += 1)
    {
        AnalysisResult* module_analysis = analysis.module_results[module_index];
        if (!module_analysis)
        {
            continue;
        }
        IrModule* module_ir = &ir.modules[module_index];
        IrValidationResult validation = ir_validate_module(module_analysis, module_ir);
        if (validation.error != IR_VALIDATION_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_IR;
            result.diagnostic =
                string_format(arena, S8("IR validation failed in module {S8} with error {u32}"), module_analysis->module.name, (u32)validation.error);
            return result;
        }
        CodegenModule code = codegen_generate_module(arena, module_analysis, module_ir, options.target,
                                                     (CodegenModuleOptions){
                                                         .debug_info = options.debug_info,
                                                         .register_allocator = options.register_allocator,
                                                     });
        result.codegen_error = code.error;
        if (code.error != CODEGEN_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_CODEGEN;
            result.diagnostic =
                string_format(arena, S8("native code generation failed in module {S8} with error {u32}"), module_analysis->module.name, (u32)code.error);
            return result;
        }
        ObjectFile object = object_from_codegen_module(arena, module_analysis, &code, options.target);
        result.object_error = object.error;
        if (object.error != OBJECT_ERROR_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic =
                string_format(arena, S8("object generation failed in module {S8} with error {u32}"), module_analysis->module.name, (u32)object.error);
            return result;
        }
        objects[object_count++] = object;
    }
    if (!object_count)
    {
        result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8("the program contains no compilable modules");
        return result;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("object linking failed with error {u32}: {S8}"), (u32)linked.error, linked.symbol);
        return result;
    }
    result.native_link = link_native_executable(arena, &linked.object,
                                                (NativeExecutableLinkOptions){
                                                    .output_path = options.output_path,
                                                    .entry_symbol = S8("main"),
                                                    .debug_info = options.debug_info,
                                                });
    if (result.native_link.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic =
            string_format(arena, S8("native executable linking failed with error {u32}: {S8}"), (u32)result.native_link.error, result.native_link.symbol);
    }
    return result;
}
