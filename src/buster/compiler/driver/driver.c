#include <buster/compiler/driver/driver.h>

#include <buster/compiler/frontend/buster/parser.h>
#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/compiler/frontend/c/c.h>
#include <buster/compiler/ir/ir.h>
#include <buster/compiler/codegen/codegen.h>
#include <buster/compiler/object/object.h>
#include <buster/file.h>
#include <buster/string.h>

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
    invocation->target.cpu_features = 0;
    return true;
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
            string_equal(argument, S8("-mcpu")) || string_equal(argument, S8("-isysroot")) || string_equal(argument, S8("--sysroot")) ||
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
                TargetParseResult parsed = target_parse_triple(value);
                if (parsed.error != TARGET_PARSE_ERROR_NONE)
                {
                    compiler_driver_argument_error(arena, &invocation, S8("unsupported target: {S8}"), value);
                    return invocation;
                }
                invocation.target = parsed.target;
            }
            else if (string_equal(argument, S8("-march")) || string_equal(argument, S8("-mcpu")))
            {
                if (!compiler_driver_set_cpu_model(arena, &invocation, value))
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
            TargetParseResult parsed = target_parse_triple(value);
            if (parsed.error != TARGET_PARSE_ERROR_NONE)
            {
                compiler_driver_argument_error(arena, &invocation, S8("unsupported target: {S8}"), value);
                return invocation;
            }
            invocation.target = parsed.target;
            continue;
        }
        value = compiler_driver_option_value(argument, S8("--sysroot="));
        if (value.length)
        {
            invocation.sysroot = value;
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
    if (!target_cpu_features_are_valid(invocation.target))
    {
        compiler_driver_argument_error(arena, &invocation, S8("CPU model is incompatible with target: {S8}"),
                                       cpu_model_to_string_os(invocation.target.cpu_model));
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
            capacity += preprocess.tokens[index].spelling.length + 1;
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
        memcpy(text + length, token.spelling.pointer, token.spelling.length);
        length += token.spelling.length;
    }
    text[length++] = '\n';
    text[length] = 0;
    return (String8){
        .pointer = text,
        .length = length,
    };
}

BUSTER_GLOBAL_LOCAL String8 compiler_driver_default_object_path(Arena* arena, String8 input);

static CompilerDriverResult compiler_driver_execute_c_single(Arena* arena, CompilerDriverInvocation invocation, bool suppress_object_write)
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
    if (preprocess.diagnostic_count)
    {
        CDiagnostic diagnostic = preprocess.diagnostics[0];
        result.error = COMPILER_DRIVER_ERROR_TOKENIZE;
        result.tokenizer_error_count = (u32)preprocess.diagnostic_count;
        result.diagnostic = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}"), invocation.input_paths[0], diagnostic.location.line, diagnostic.location.column,
                                          diagnostic.message);
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
                source_line = failed_instruction->source.line;
                source_column = failed_instruction->source.column;
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
    if (!arena || invocation.error != COMPILER_DRIVER_ERROR_NONE)
    {
        return compiler_driver_execute_c_single(arena, invocation, false);
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
            CompilerDriverResult result = {
                .error = COMPILER_DRIVER_ERROR_INVALID_INPUT,
                .diagnostic = S8("cannot mix C and Buster inputs in one invocation"),
            };
            return result;
        }
        return compiler_driver_execute_buster(arena, invocation);
    }
    if (invocation.input_count <= 1 && !invocation.library_count &&
        (!invocation.input_count || (!compiler_driver_object_input(invocation.input_paths[0]) && !compiler_driver_archive_input(invocation.input_paths[0]))))
    {
        return compiler_driver_execute_c_single(arena, invocation, false);
    }
    CompilerDriverResult result = {0};
    for (u32 input_index = 0; input_index < invocation.input_count; input_index += 1)
    {
        bool object_input = compiler_driver_object_input(invocation.input_paths[input_index]);
        bool archive_input = compiler_driver_archive_input(invocation.input_paths[input_index]);
        if ((object_input || archive_input) && invocation.action != COMPILER_DRIVER_ACTION_LINK)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("prebuilt input {S8} is only valid while linking"), invocation.input_paths[input_index]);
            return result;
        }
        if (!object_input && !archive_input && !compiler_driver_c_input(invocation, invocation.input_paths[input_index]))
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = string_format(arena, S8("unsupported C input {S8}"), invocation.input_paths[input_index]);
            return result;
        }
    }
    if ((invocation.action == COMPILER_DRIVER_ACTION_OBJECT || invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ||
         invocation.action == COMPILER_DRIVER_ACTION_SYNTAX_ONLY) && invocation.output_path.length)
    {
        result.error = COMPILER_DRIVER_ERROR_ARGUMENT;
        result.diagnostic = invocation.action == COMPILER_DRIVER_ACTION_OBJECT   ? S8("cannot specify -o with -c and multiple input files")
                             : invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY ? S8("cannot specify -o with -S and multiple input files")
                                                                                     : S8("cannot specify -o with -fsyntax-only and multiple input files");
        return result;
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
            return result;
        }
        input_archives[input_index] = object_archive_read(arena, archive_file.bytes, invocation.target);
        file_map_unmap(archive_file);
        if (input_archives[input_index].error != OBJECT_ERROR_NONE || input_archives[input_index].object_count > UINT32_MAX - object_capacity)
        {
            result.error = COMPILER_DRIVER_ERROR_OBJECT;
            result.object_error = input_archives[input_index].error;
            result.diagnostic = string_format(arena, S8("could not read archive {S8}: error {u32}"), input_path, (u32)input_archives[input_index].error);
            return result;
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
            return result;
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
                return result;
            }
            ObjectFile object = object_read(arena, object_file.bytes, invocation.target);
            file_map_unmap(object_file);
            if (object.error != OBJECT_ERROR_NONE)
            {
                result.error = COMPILER_DRIVER_ERROR_OBJECT;
                result.object_error = object.error;
                result.diagnostic = string_format(arena, S8("could not read object {S8}: error {u32}"), input_path, (u32)object.error);
                return result;
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
            // Large system headers can make one translation unit
            // retain gigabytes of preprocessing and semantic data.
            // Keep that transient state out of the result arena so
            // multi-file builds scale with object size, not with the
            // sum of every frontend working set.
            .reserved_size = BUSTER_GB(4),
        });
        if (!unit_arena)
        {
            result.error = COMPILER_DRIVER_ERROR_INVALID_INPUT;
            result.diagnostic = S8("could not allocate C translation-unit arena");
            return result;
        }
        CompilerDriverResult unit = compiler_driver_execute_c_single(unit_arena, single, suppress_object_write);
        result.tokenizer_error_count += unit.tokenizer_error_count;
        result.parser_diagnostic_count += unit.parser_diagnostic_count;
        result.analysis_diagnostic_count += unit.analysis_diagnostic_count;
        result.codegen_statistics.instruction_count += unit.codegen_statistics.instruction_count;
        result.codegen_statistics.value_count += unit.codegen_statistics.value_count;
        result.codegen_statistics.stack_value_bytes += unit.codegen_statistics.stack_value_bytes;
        result.codegen_statistics.stack_frame_bytes += unit.codegen_statistics.stack_frame_bytes;
        result.codegen_statistics.code_bytes += unit.codegen_statistics.code_bytes;
        result.codegen_statistics.native_vector_operation_count += unit.codegen_statistics.native_vector_operation_count;
        result.codegen_statistics.split_vector_operation_count += unit.codegen_statistics.split_vector_operation_count;
        result.codegen_statistics.vzeroupper_count += unit.codegen_statistics.vzeroupper_count;
        result.codegen_statistics.forwarded_wide_vector_load_count += unit.codegen_statistics.forwarded_wide_vector_load_count;
        result.codegen_statistics.function_count += unit.codegen_statistics.function_count;
        result.codegen_statistics.maximum_stack_frame_bytes =
            BUSTER_MAX(result.codegen_statistics.maximum_stack_frame_bytes, unit.codegen_statistics.maximum_stack_frame_bytes);
        if (unit.error != COMPILER_DRIVER_ERROR_NONE)
        {
            if (unit.diagnostic.length)
            {
                unit.diagnostic = string_duplicate_arena(arena, unit.diagnostic, false);
            }
            arena_destroy(unit_arena, 1);
            return unit;
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
        return result;
    }
    if (invocation.action == COMPILER_DRIVER_ACTION_ASSEMBLY)
    {
        result.output = string_join_arena(arena,
                                          (SliceString8){
                                              .pointer = preprocessed,
                                              .length = invocation.input_count,
                                          },
                                          false);
        return result;
    }
    if (invocation.action != COMPILER_DRIVER_ACTION_LINK)
    {
        return result;
    }
    LinkObjectResult linked = link_objects(arena, objects, object_count,
                                           (LinkOptions){
                                               .allow_undefined_symbols = true,
                                           });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error = COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(arena, S8("C object linking failed with error {u32}"), (u32)linked.error);
        return result;
    }
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

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL bool compiler_driver_test_elf_section_find(ByteSlice image, String8 name, u64* offset, u64* size, u64* address)
{
    enum
    {
        ELF_HEADER_SIZE = 64,
        ELF_SECTION_HEADER_SIZE = 64,
    };
    if (image.length < ELF_HEADER_SIZE || memcmp(image.pointer, "\x7f" "ELF", 4) != 0)
    {
        return false;
    }
    u64 section_table;
    u16 section_count;
    u16 string_index;
    memcpy(&section_table, image.pointer + 40, sizeof(section_table));
    memcpy(&section_count, image.pointer + 60, sizeof(section_count));
    memcpy(&string_index, image.pointer + 62, sizeof(string_index));
    if (!section_count || string_index >= section_count || section_table > image.length ||
        (u64)section_count * ELF_SECTION_HEADER_SIZE > image.length - section_table)
    {
        return false;
    }
    u64 string_header = section_table + (u64)string_index * ELF_SECTION_HEADER_SIZE;
    u64 string_offset;
    u64 string_size;
    memcpy(&string_offset, image.pointer + string_header + 24, sizeof(string_offset));
    memcpy(&string_size, image.pointer + string_header + 32, sizeof(string_size));
    if (string_offset > image.length || string_size > image.length - string_offset)
    {
        return false;
    }
    for (u16 section_index = 0; section_index < section_count; section_index += 1)
    {
        u64 header = section_table + (u64)section_index * ELF_SECTION_HEADER_SIZE;
        u32 name_offset;
        memcpy(&name_offset, image.pointer + header, sizeof(name_offset));
        if (name_offset >= string_size || string_size - name_offset <= name.length)
        {
            continue;
        }
        if (memcmp(image.pointer + string_offset + name_offset, name.pointer, name.length) != 0 ||
            image.pointer[string_offset + name_offset + name.length] != 0)
        {
            continue;
        }
        memcpy(offset, image.pointer + header + 24, sizeof(*offset));
        memcpy(size, image.pointer + header + 32, sizeof(*size));
        memcpy(address, image.pointer + header + 16, sizeof(*address));
        return *offset <= image.length && *size <= image.length - *offset;
    }
    return false;
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_test_elf_section(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return (ByteSlice){0};
    }
    return (ByteSlice){
        .pointer = image.pointer + offset,
        .length = size,
    };
}

BUSTER_GLOBAL_LOCAL u64 compiler_driver_test_elf_section_address(ByteSlice image, String8 name)
{
    u64 offset = 0;
    u64 size = 0;
    u64 address = 0;
    if (!compiler_driver_test_elf_section_find(image, name, &offset, &size, &address))
    {
        return 0;
    }
    return address;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_bytes_contain(ByteSlice bytes, String8 needle)
{
    if (!needle.length || needle.length > bytes.length)
    {
        return false;
    }
    for (u64 offset = 0; offset + needle.length <= bytes.length; offset += 1)
    {
        if (memcmp(bytes.pointer + offset, needle.pointer, needle.length) == 0)
        {
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL ByteSlice compiler_driver_test_archive(Arena* arena, ByteSlice* members, String8* names, u32 member_count)
{
    u64 size = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        size += 60 + members[member_index].length;
        size += size & 1;
    }
    ByteSlice result = {
        .pointer = arena_allocate(arena, u8, size),
        .length = size,
    };
    memcpy(result.pointer, "!<arch>\n", 8);
    u64 cursor = 8;
    for (u32 member_index = 0; member_index < member_count; member_index += 1)
    {
        memset(result.pointer + cursor, ' ', 60);
        u64 name_length = BUSTER_MIN(names[member_index].length, (u64)15);
        memcpy(result.pointer + cursor, names[member_index].pointer, name_length);
        result.pointer[cursor + name_length] = '/';
        String8 member_size = string_format(arena, S8("{u64}"), members[member_index].length);
        memcpy(result.pointer + cursor + 48, member_size.pointer, member_size.length);
        result.pointer[cursor + 58] = '`';
        result.pointer[cursor + 59] = '\n';
        cursor += 60;
        memcpy(result.pointer + cursor, members[member_index].pointer, members[member_index].length);
        cursor += members[member_index].length;
        if (cursor & 1)
        {
            result.pointer[cursor++] = '\n';
        }
    }
    return result;
}

UnitTestResult compiler_driver_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    String8 command_line[] = {
        S8("-c"),
        S8("-std=gnu23"),
        S8("-target"),
        S8("aarch64-linux-android"),
        S8("-mcpu=apple-m4"),
        S8("--sysroot=/sdk"),
        S8("-Iinclude"),
        S8("-isystem"),
        S8("system"),
        S8("-DDEBUG=1"),
        S8("-U"),
        S8("NDEBUG"),
        S8("-O2"),
        S8("-g"),
        S8("-Wall"),
        S8("-fPIC"),
        S8("-pthread"),
        S8("-L/sdk/lib"),
        S8("-l:libandroid.so"),
        S8("-Wl,--gc-sections"),
        S8("-o"),
        S8("output.o"),
        S8("source.c"),
    };
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command_line));
    BUSTER_TEST(arguments, invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, invocation.action == COMPILER_DRIVER_ACTION_OBJECT);
    BUSTER_TEST(arguments, invocation.c_dialect == COMPILER_DRIVER_C_DIALECT_GNU23);
    BUSTER_TEST(arguments, invocation.target.cpu_arch == CPU_ARCH_AARCH64);
    BUSTER_TEST(arguments, invocation.target.os == OPERATING_SYSTEM_ANDROID);
    BUSTER_TEST(arguments, invocation.target.cpu_model == CPU_MODEL_A64_APPLE_M4);
    BUSTER_TEST(arguments, invocation.debug_info);
    BUSTER_TEST(arguments, invocation.include_path_count == 1);
    BUSTER_TEST(arguments, invocation.system_include_path_count >= 4);
    BUSTER_STRING_TEST(arguments, invocation.system_include_paths[0], S8("system"));
    bool found_sysroot_multiarch = false;
    bool found_sysroot_include = false;
    for (u32 path_index = 0; path_index < invocation.system_include_path_count; path_index += 1)
    {
        found_sysroot_multiarch |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include/"
                                                                                                "aarch64-linux-android"));
        found_sysroot_include |= string_equal(invocation.system_include_paths[path_index], S8("/sdk/usr/include"));
    }
    BUSTER_TEST(arguments, found_sysroot_multiarch);
    BUSTER_TEST(arguments, found_sysroot_include);
    BUSTER_TEST(arguments, invocation.definition_count == 1);
    BUSTER_TEST(arguments, invocation.undefinition_count == 1);
    BUSTER_TEST(arguments, invocation.library_path_count == 1);
    BUSTER_TEST(arguments, invocation.library_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.library_paths[0], S8("/sdk/lib"));
    BUSTER_STRING_TEST(arguments, invocation.libraries[0], S8(":libandroid.so"));
    BUSTER_TEST(arguments, invocation.linker_argument_count == 1);
    BUSTER_TEST(arguments, invocation.input_count == 1);
    BUSTER_STRING_TEST(arguments, invocation.output_path, S8("output.o"));
    BUSTER_STRING_TEST(arguments, invocation.sysroot, S8("/sdk"));
    String8 x86_cpu_command_line[] = {
        S8("-c"),
        S8("--target=x86_64-linux"),
        S8("-march=znver5"),
        S8("source.c"),
    };
    CompilerDriverInvocation x86_cpu_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(x86_cpu_command_line));
    BUSTER_TEST(arguments, x86_cpu_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, x86_cpu_invocation.target.cpu_model == CPU_MODEL_AMD_ZEN_5);
    BUSTER_TEST(arguments, target_vector_register_size(x86_cpu_invocation.target) == 64);
    String8 no_debug_command_line[] = {S8("-g0"), S8("-c"), S8("source.c")};
    CompilerDriverInvocation no_debug_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(no_debug_command_line));
    BUSTER_TEST(arguments, no_debug_invocation.error == COMPILER_DRIVER_ERROR_NONE && !no_debug_invocation.debug_info);
    String8 unsupported_debug_command_line[] = {S8("-g1"), S8("source.c")};
    CompilerDriverInvocation unsupported_debug =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unsupported_debug_command_line));
    BUSTER_TEST(arguments, unsupported_debug.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unsupported_debug.diagnostic, S8("unsupported debug option: -g1"));
    String8 incompatible_cpu_command_line[] = {
        S8("--target=x86_64-linux"),
        S8("-mcpu=apple-m4"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation incompatible_cpu =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(incompatible_cpu_command_line));
    BUSTER_TEST(arguments, incompatible_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, incompatible_cpu.diagnostic, S8("CPU model is incompatible with target: apple-m4"));
    String8 unknown_cpu_command_line[] = {
        S8("-march=future-fast"),
        S8("-c"),
        S8("source.c"),
    };
    CompilerDriverInvocation unknown_cpu = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(unknown_cpu_command_line));
    BUSTER_TEST(arguments, unknown_cpu.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, unknown_cpu.diagnostic, S8("unsupported CPU model: future-fast"));
    String8 isolated_command_line[] = {
        S8("-isysroot"), S8("/isolated-sdk"), S8("-nostdinc"), S8("-isystem"), S8("explicit-system"), S8("source.c"),
    };
    CompilerDriverInvocation isolated_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(isolated_command_line));
    BUSTER_TEST(arguments, isolated_invocation.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, isolated_invocation.no_standard_includes);
    BUSTER_STRING_TEST(arguments, isolated_invocation.sysroot, S8("/isolated-sdk"));
    BUSTER_TEST(arguments, isolated_invocation.system_include_path_count == 1);
    BUSTER_STRING_TEST(arguments, isolated_invocation.system_include_paths[0], S8("explicit-system"));
    String8 invalid_command_line[] = {
        S8("-target"),
        S8("riscv64-unknown-linux-gnu"),
        S8("source.c"),
    };
    CompilerDriverInvocation invalid_invocation = compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(invalid_command_line));
    BUSTER_TEST(arguments, invalid_invocation.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 preprocess_command_line[] = {
        S8("-E"),
        S8("-DADDED=5"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverInvocation preprocess_invocation =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(preprocess_command_line));
    CompilerDriverResult preprocess = compiler_driver_execute_invocation(arguments->arena, preprocess_invocation);
    BUSTER_TEST(arguments, preprocess.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(preprocess.output, S8("int answer = 37 ;")) != BUSTER_STRING_NO_MATCH);
    String8 syntax_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_c_driver.c"),
    };
    CompilerDriverResult syntax = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(syntax_command_line)));
    BUSTER_TEST(arguments, syntax.error == COMPILER_DRIVER_ERROR_NONE);
    String8 assembly_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("x86_64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_command_line)));
    BUSTER_TEST(arguments, assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, assembly.output.length != 0);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.intel_syntax noprefix\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.text\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tpush rbp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\tmov rbp, rsp\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly.output, S8("\t.byte ")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_c23_command_line[] = {
        S8("-S"), S8("-std=c23"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_constexpr.c"),
    };
    CompilerDriverResult assembly_c23 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_c23_command_line)));
    BUSTER_TEST(arguments, assembly_c23.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_c23.output, S8("[rip + \"offset\"]")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_tls_command_line[] = {
        S8("-S"), S8("-target"), S8("x86_64-unknown-linux-gnu"), S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult assembly_tls = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_tls_command_line)));
    BUSTER_TEST(arguments, assembly_tls.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_tls.output, S8("@TPOFF")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_aarch64_command_line[] = {
        S8("-S"),
        S8("-target"),
        S8("aarch64-unknown-linux-gnu"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_aarch64_command_line)));
    BUSTER_TEST(arguments, assembly_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tstp x29, x30")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_aarch64.output, S8("\tret\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_multi_command_line[] = {
        S8("-S"),
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult assembly_multi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_multi_command_line)));
    BUSTER_TEST(arguments, assembly_multi.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    BUSTER_TEST(arguments, string_first_sequence(assembly_multi.output, S8("add_values:\n")) != BUSTER_STRING_NO_MATCH);
    String8 assembly_output_path = buster_test_temporary_path(arguments->arena, S8("buster-c-assembly"), S8(".s"));
    String8 assembly_file_command_line[] = {
        S8("-S"),
        S8("-o"),
        assembly_output_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult assembly_file = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(assembly_file_command_line)));
    BUSTER_TEST(arguments, assembly_file.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice assembly_file_bytes = file_read(arguments->arena, assembly_output_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, assembly_file_bytes.length == assembly_file.output.length);
    if (assembly_file_bytes.length == assembly_file.output.length)
    {
        BUSTER_TEST(arguments, memcmp(assembly_file_bytes.pointer, assembly_file.output.pointer, assembly_file.output.length) == 0);
    }
    String8 buster_syntax_command_line[] = {
        S8("-fsyntax-only"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_syntax = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_syntax_command_line)));
    BUSTER_TEST(arguments, buster_syntax.error == COMPILER_DRIVER_ERROR_NONE);
    String8 buster_assembly_command_line[] = {
        S8("-S"),
        S8("-x"),
        S8("buster"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_assembly = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_assembly_command_line)));
    BUSTER_TEST(arguments, buster_assembly.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, string_first_sequence(buster_assembly.output, S8("main:\n")) != BUSTER_STRING_NO_MATCH);
    String8 buster_object_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-object"), S8(".o"));
    String8 buster_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        buster_object_path,
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_object = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_object_command_line)));
    BUSTER_TEST(arguments, buster_object.error == COMPILER_DRIVER_ERROR_NONE);
    ByteSlice buster_object_bytes = file_read(arguments->arena, buster_object_path, (FileReadOptions){0});
    BUSTER_TEST(arguments, buster_object_bytes.length != 0);
    String8 buster_module_command_line[] = {
        S8("-fsyntax-only"),
        S8("-fmodule-root"),
        S8("tests/modules"),
        S8("tests/basic_import.bbb"),
    };
    CompilerDriverResult buster_modules = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_module_command_line)));
    BUSTER_TEST(arguments, buster_modules.error == COMPILER_DRIVER_ERROR_NONE);
    String8 buster_preprocess_command_line[] = {
        S8("-E"),
        S8("tests/basic_minimal.bbb"),
    };
    CompilerDriverResult buster_preprocess = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(buster_preprocess_command_line)));
    BUSTER_TEST(arguments, buster_preprocess.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    BUSTER_STRING_TEST(arguments, buster_preprocess.diagnostic, S8("Buster input does not support preprocessing"));
    String8 conflicting_actions[] = {
        S8("-c"),
        S8("-S"),
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverInvocation conflicting =
        compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(conflicting_actions));
    BUSTER_TEST(arguments, conflicting.error == COMPILER_DRIVER_ERROR_ARGUMENT);
    String8 dialect_flags[] = {
        S8("-std=gnu11"), S8("-std=gnu17"), S8("-std=gnu23"), S8("-std=c11"), S8("-std=c17"), S8("-std=c23"),
    };
    String8 dialect_versions[] = {
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
        S8("-DEXPECTED_STDC_VERSION=201112L"), S8("-DEXPECTED_STDC_VERSION=201710L"), S8("-DEXPECTED_STDC_VERSION=202311L"),
    };
    for (u32 dialect_index = 0; dialect_index < BUSTER_ARRAY_LENGTH(dialect_flags); dialect_index += 1)
    {
        TemporalArena dialect_temporary = arena_begin_temporal(arguments->arena);
        Arena* dialect_arena = dialect_temporary.arena;
        String8 dialect_object_path =
            buster_test_temporary_path(dialect_arena, S8("buster-c-dialect"), string_format(dialect_arena, S8("-{u32}.o"), dialect_index));
        String8 dialect_command_line[] = {
            S8("-c"), dialect_flags[dialect_index], dialect_versions[dialect_index], dialect_index < 3 ? S8("-DEXPECTED_GNU=1") : S8("-DEXPECTED_GNU=0"),
            S8("-o"), dialect_object_path,          S8("tests/basic_c_dialect.c"),
        };
        CompilerDriverResult dialect_result = compiler_driver_execute_invocation(
            dialect_arena, compiler_driver_parse_arguments(dialect_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(dialect_command_line)));
        BUSTER_TEST(arguments, dialect_result.error == COMPILER_DRIVER_ERROR_NONE);
        scratch_end(dialect_temporary);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    Arena* c_object_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_object_temporary = scratch_begin(c_object_conflicts, BUSTER_ARRAY_LENGTH(c_object_conflicts));
    Arena* c_object_arena = c_object_temporary.arena;
    String8 c_object_path = buster_test_temporary_path(c_object_arena, S8("buster-c-driver"), S8(".o"));
    String8 c_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_object_path,
        S8("tests/basic_c_compile.c"),
    };
    CompilerDriverResult c_object = compiler_driver_execute_invocation(
        c_object_arena, compiler_driver_parse_arguments(c_object_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_object_command_line)));
    BUSTER_TEST(arguments, c_object.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_object.codegen_statistics.function_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.instruction_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.value_count > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_value_bytes > 0);
    BUSTER_TEST(arguments, c_object.codegen_statistics.stack_frame_bytes >= c_object.codegen_statistics.maximum_stack_frame_bytes);
    BUSTER_TEST(arguments, c_object.codegen_statistics.code_bytes > 0);
    FileMapRead c_object_map = file_map_read(c_object_arena, c_object_path, (FileReadOptions){0});
    ByteSlice c_object_bytes = c_object_map.bytes;
    BUSTER_TEST(arguments, c_object_bytes.length != 0);
    file_map_unmap(c_object_map);
    String8 c_object_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_object_targets); target_index += 1)
    {
        TemporalArena cross_temp = arena_begin_temporal(c_object_arena);
        String8 cross_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-object"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 cross_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), cross_object_path, S8("tests/basic_c_compile.c"),
        };
        CompilerDriverResult cross = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(cross_command_line)));
        BUSTER_TEST(arguments, cross.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead cross_map = file_map_read(cross_temp.arena, cross_object_path, (FileReadOptions){0});
        ByteSlice cross_bytes = cross_map.bytes;
        BUSTER_TEST(arguments, cross_bytes.length != 0);
        // Debug sections and their relocations must survive a round trip
        // through every object format, or linking a previously compiled
        // object back in fails.
        BUSTER_TEST(arguments, cross.has_object);
        if (cross.has_object)
        {
            ObjectFile cross_round_trip = object_read(cross_temp.arena, cross_bytes, cross.object.target);
            BUSTER_TEST(arguments, cross_round_trip.error == OBJECT_ERROR_NONE);
            if (cross_round_trip.error == OBJECT_ERROR_NONE)
            {
                bool debug_found = false;
                for (u32 kind = 0; kind < OBJECT_SECTION_COUNT; kind += 1)
                {
                    if (!object_section_kind_is_debug((ObjectSectionKind)kind))
                    {
                        continue;
                    }
                    BUSTER_TEST(arguments, cross_round_trip.sections[kind].data.length == cross.object.sections[kind].data.length);
                    debug_found = debug_found || cross_round_trip.sections[kind].data.length != 0;
                }
                BUSTER_TEST(arguments, debug_found);
            }
        }
        file_map_unmap(cross_map);
        String8 fixed_enum_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-fixed-enum"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 fixed_enum_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), fixed_enum_object_path, S8("tests/basic_c_fixed_enum.c"),
        };
        CompilerDriverResult fixed_enum = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fixed_enum_command_line)));
        BUSTER_TEST(arguments, fixed_enum.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead fixed_enum_map = file_map_read(cross_temp.arena, fixed_enum_object_path, (FileReadOptions){0});
        ByteSlice fixed_enum_bytes = fixed_enum_map.bytes;
        BUSTER_TEST(arguments, fixed_enum_bytes.length != 0);
        file_map_unmap(fixed_enum_map);
        String8 string_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-string"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 string_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), string_object_path, S8("tests/basic_c_string_concat.c"),
        };
        CompilerDriverResult string_literals = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_command_line)));
        BUSTER_TEST(arguments, string_literals.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead string_map = file_map_read(cross_temp.arena, string_object_path, (FileReadOptions){0});
        ByteSlice string_bytes = string_map.bytes;
        BUSTER_TEST(arguments, string_bytes.length != 0);
        file_map_unmap(string_map);
        String8 nullptr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-nullptr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 nullptr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), nullptr_object_path, S8("tests/basic_c_nullptr.c"),
        };
        CompilerDriverResult nullptr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
        BUSTER_TEST(arguments, nullptr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead nullptr_map = file_map_read(cross_temp.arena, nullptr_object_path, (FileReadOptions){0});
        ByteSlice nullptr_bytes = nullptr_map.bytes;
        BUSTER_TEST(arguments, nullptr_bytes.length != 0);
        file_map_unmap(nullptr_map);
        String8 constexpr_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-constexpr"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 constexpr_command_line[] = {
            S8("-c"), S8("-std=c23"), S8("-target"), c_object_targets[target_index], S8("-o"), constexpr_object_path, S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_result = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_result.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead constexpr_map = file_map_read(cross_temp.arena, constexpr_object_path, (FileReadOptions){0});
        ByteSlice constexpr_bytes = constexpr_map.bytes;
        BUSTER_TEST(arguments, constexpr_bytes.length != 0);
        file_map_unmap(constexpr_map);
        String8 atomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-atomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 atomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), atomic_object_path, S8("tests/basic_c_atomic.c"),
        };
        CompilerDriverResult atomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(atomic_command_line)));
        BUSTER_TEST(arguments, atomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead atomic_map = file_map_read(cross_temp.arena, atomic_object_path, (FileReadOptions){0});
        ByteSlice atomic_bytes = atomic_map.bytes;
        BUSTER_TEST(arguments, atomic_bytes.length != 0);
        file_map_unmap(atomic_map);
        String8 stdatomic_object_path =
            buster_test_temporary_path(cross_temp.arena, S8("buster-c-cross-stdatomic"), string_format(cross_temp.arena, S8("-{u32}.o"), target_index));
        String8 stdatomic_command_line[] = {
            S8("-c"), S8("-target"), c_object_targets[target_index], S8("-o"), stdatomic_object_path, S8("tests/basic_c_stdatomic.c"),
        };
        CompilerDriverResult stdatomic = compiler_driver_execute_invocation(
            cross_temp.arena, compiler_driver_parse_arguments(cross_temp.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stdatomic_command_line)));
        BUSTER_TEST(arguments, stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead stdatomic_map = file_map_read(cross_temp.arena, stdatomic_object_path, (FileReadOptions){0});
        ByteSlice stdatomic_bytes = stdatomic_map.bytes;
        BUSTER_TEST(arguments, stdatomic_bytes.length != 0);
        file_map_unmap(stdatomic_map);
        scratch_end(cross_temp);
    }
    {
        TemporalArena large_frame_temporary = arena_begin_temporal(c_object_arena);
        String8 large_frame_object_path = buster_test_temporary_path(large_frame_temporary.arena, S8("buster-c-large-frame"), S8(".o"));
        String8 large_frame_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), large_frame_object_path, S8("tests/basic_c_large_frame.c"),
        };
        CompilerDriverResult large_frame = compiler_driver_execute_invocation(
            large_frame_temporary.arena,
            compiler_driver_parse_arguments(large_frame_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(large_frame_command_line)));
        BUSTER_TEST(arguments, large_frame.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead large_frame_map = file_map_read(large_frame_temporary.arena, large_frame_object_path, (FileReadOptions){0});
        ByteSlice large_frame_bytes = large_frame_map.bytes;
        BUSTER_TEST(arguments, large_frame_bytes.length != 0);
        file_map_unmap(large_frame_map);
        scratch_end(large_frame_temporary);
    }
    {
        TemporalArena ucontext_temporary = arena_begin_temporal(c_object_arena);
        String8 ucontext_object_path = buster_test_temporary_path(ucontext_temporary.arena, S8("buster-c-ucontext"), S8(".o"));
        String8 ucontext_command_line[] = {
            S8("-c"), S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), ucontext_object_path, S8("tests/basic_c_ucontext.c"),
        };
        CompilerDriverResult ucontext = compiler_driver_execute_invocation(
            ucontext_temporary.arena, compiler_driver_parse_arguments(ucontext_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ucontext_command_line)));
        BUSTER_TEST(arguments, ucontext.error == COMPILER_DRIVER_ERROR_NONE);
        FileMapRead ucontext_map = file_map_read(ucontext_temporary.arena, ucontext_object_path, (FileReadOptions){0});
        ByteSlice ucontext_bytes = ucontext_map.bytes;
        BUSTER_TEST(arguments, ucontext_bytes.length != 0);
        file_map_unmap(ucontext_map);
        scratch_end(ucontext_temporary);
    }
    scratch_end(c_object_temporary);
#endif
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SANITIZE
    String8 c_executable_path = buster_test_temporary_path(arguments->arena, S8("buster-c-driver"),
#if BUSTER_WINDOWS
                                                           S8(".exe"));
#else
                                                           S8(""));
#endif
    String8 c_link_command_line[] = {
        S8("-o"),
        c_executable_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_link_command_line)));
    BUSTER_TEST(arguments, c_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_run_arguments[] = {
            c_executable_path,
        };
        ProcessSpawnResult c_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                      (ProcessSpawnOptions){
                                                          .use_process_environment = true,
                                                      });
        BUSTER_TEST(arguments, c_spawn.handle != 0);
        if (c_spawn.handle)
        {
            ProcessWaitResult c_wait = os_process_wait_sync(arguments->arena, c_spawn);
            BUSTER_TEST(arguments, c_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    // Windows targets must select CodeView rather than DWARF: the COFF object
    // carries .debug$S/.debug$T with SECREL32/SECTION relocations, and no
    // DWARF sections at all.
    {
        TemporalArena codeview_temporary = scratch_begin(&arguments->arena, 1);
        String8 codeview_object_path = buster_test_temporary_path(codeview_temporary.arena, S8("buster-c-codeview"), S8(".o"));
        String8 codeview_command_line[] = {
            S8("-target"), S8("x86_64-windows"), S8("-c"), S8("-o"), codeview_object_path, S8("tests/basic_c_operations.c"),
        };
        CompilerDriverResult codeview_compile = compiler_driver_execute_invocation(
            codeview_temporary.arena, compiler_driver_parse_arguments(codeview_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(codeview_command_line)));
        BUSTER_TEST(arguments, codeview_compile.error == COMPILER_DRIVER_ERROR_NONE);
        if (codeview_compile.error == COMPILER_DRIVER_ERROR_NONE)
        {
            BUSTER_TEST(arguments, codeview_compile.has_object);
            ObjectFile* codeview_object = &codeview_compile.object;
            ByteSlice codeview_symbols = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS].data;
            ByteSlice codeview_types = codeview_object->sections[OBJECT_SECTION_DEBUG_CODEVIEW_TYPES].data;
            BUSTER_TEST(arguments, codeview_symbols.length > 16);
            BUSTER_TEST(arguments, codeview_types.length >= 4);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_INFO].data.length == 0);
            BUSTER_TEST(arguments, codeview_object->sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
            u32 codeview_signature = 0;
            memcpy(&codeview_signature, codeview_symbols.pointer, sizeof(codeview_signature));
            BUSTER_TEST(arguments, codeview_signature == 4);
            u32 secrel_count = 0;
            u32 section_count = 0;
            for (u32 relocation_index = 0; relocation_index < codeview_object->relocation_count; relocation_index += 1)
            {
                ObjectRelocation relocation = codeview_object->relocations[relocation_index];
                if (relocation.section != OBJECT_SECTION_DEBUG_CODEVIEW_SYMBOLS)
                {
                    continue;
                }
                secrel_count += relocation.kind == OBJECT_RELOCATION_COFF_SECREL32;
                section_count += relocation.kind == OBJECT_RELOCATION_COFF_SECTION16;
                BUSTER_TEST(arguments, relocation.symbol < codeview_object->symbol_count);
                BUSTER_TEST(arguments, relocation.offset + 2 <= codeview_symbols.length);
            }
            // Functions and materialized globals each contribute matching
            // section-relative and section-index slots.
            BUSTER_TEST(arguments, secrel_count != 0 && secrel_count == section_count);
        }
        scratch_end(codeview_temporary);
    }
    // Recording line rows must not change the code that is generated for
    // them: the machine code has to be identical with and without -g.
    {
        TemporalArena debug_parity_temporary = scratch_begin(&arguments->arena, 1);
        String8 debug_parity_targets[] = {
            S8("x86_64-unknown-linux-gnu"),
            S8("aarch64-apple-macos"),
        };
        String8 debug_parity_sources[] = {
            S8("tests/basic_c_operations.c"),
            S8("tests/basic_c_archive_bias.c"),
            S8("tests/basic_c_multi_add.c"),
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(debug_parity_targets); target_index += 1)
        {
            for (u32 source_index = 0; source_index < BUSTER_ARRAY_LENGTH(debug_parity_sources); source_index += 1)
            {
                String8 source = debug_parity_sources[source_index];
                String8 debug_command_line[] = {
                    S8("-c"), S8("-g"), S8("-target"), debug_parity_targets[target_index], source,
                };
                String8 stripped_command_line[] = {
                    S8("-c"), S8("-g0"), S8("-target"), debug_parity_targets[target_index], source,
                };
                CompilerDriverResult with_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(debug_command_line)));
                CompilerDriverResult without_debug = compiler_driver_execute_invocation(
                    debug_parity_temporary.arena,
                    compiler_driver_parse_arguments(debug_parity_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(stripped_command_line)));
                BUSTER_TEST(arguments, with_debug.error == COMPILER_DRIVER_ERROR_NONE && with_debug.has_object);
                BUSTER_TEST(arguments, without_debug.error == COMPILER_DRIVER_ERROR_NONE && without_debug.has_object);
                if (with_debug.has_object && without_debug.has_object)
                {
                    ByteSlice debug_text = with_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    ByteSlice stripped_text = without_debug.object.sections[OBJECT_SECTION_TEXT].data;
                    BUSTER_TEST(arguments, debug_text.length != 0);
                    BUSTER_TEST(arguments, debug_text.length == stripped_text.length);
                    if (debug_text.length == stripped_text.length)
                    {
                        BUSTER_TEST(arguments, memcmp(debug_text.pointer, stripped_text.pointer, debug_text.length) == 0);
                    }
                    BUSTER_TEST(arguments, with_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length != 0);
                    BUSTER_TEST(arguments, without_debug.object.sections[OBJECT_SECTION_DEBUG_LINE].data.length == 0);
                }
            }
        }
        scratch_end(debug_parity_temporary);
    }
#if BUSTER_LINUX
    // -g must produce loadable executables that carry DWARF line and info
    // sections resolvable back to source lines.
    String8 c_debug_path = buster_test_temporary_path(arguments->arena, S8("buster-c-debug"), S8(""));
    String8 c_debug_command_line[] = {
        S8("-g"),
        S8("-o"),
        c_debug_path,
        S8("tests/basic_c_operations.c"),
    };
    CompilerDriverResult c_debug_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_command_line)));
    BUSTER_TEST(arguments, c_debug_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_debug_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice c_debug_image = file_read(arguments->arena, c_debug_path, (FileReadOptions){0});
        ByteSlice c_debug_line = compiler_driver_test_elf_section(c_debug_image, S8(".debug_line"));
        ByteSlice c_debug_info = compiler_driver_test_elf_section(c_debug_image, S8(".debug_info"));
        ByteSlice c_debug_text = compiler_driver_test_elf_section(c_debug_image, S8(".text"));
        u64 c_debug_text_address = compiler_driver_test_elf_section_address(c_debug_image, S8(".text"));
        BUSTER_TEST(arguments, c_debug_line.length > 4);
        BUSTER_TEST(arguments, c_debug_info.length > 11);
        BUSTER_TEST(arguments, c_debug_text.length != 0);
        BUSTER_TEST(arguments, c_debug_text_address != 0);
        DwarfLineRow c_debug_row = {0};
        BUSTER_TEST(arguments, dwarf_line_lookup(c_debug_line, c_debug_text_address, &c_debug_row));
        BUSTER_TEST(arguments, c_debug_row.line != 0 && c_debug_row.file != 0);
        String8 c_debug_run_arguments[] = {
            c_debug_path,
        };
        ProcessSpawnResult c_debug_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_debug_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_debug_spawn.handle != 0);
        if (c_debug_spawn.handle)
        {
            ProcessWaitResult c_debug_wait = os_process_wait_sync(arguments->arena, c_debug_spawn);
            BUSTER_TEST(arguments, c_debug_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    String8 string_concat_path = buster_test_temporary_path(arguments->arena, S8("buster-c-string-concat"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    String8 string_concat_command_line[] = {
        S8("-o"),
        string_concat_path,
        S8("tests/basic_c_string_concat.c"),
    };
    CompilerDriverResult string_concat_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_command_line)));
    BUSTER_TEST(arguments, string_concat_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (string_concat_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 string_concat_run_arguments[] = {
            string_concat_path,
        };
        ProcessSpawnResult string_concat_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(string_concat_run_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, string_concat_spawn.handle != 0);
        if (string_concat_spawn.handle)
        {
            ProcessWaitResult string_concat_wait = os_process_wait_sync(arguments->arena, string_concat_spawn);
            BUSTER_TEST(arguments, string_concat_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 nullptr_path = buster_test_temporary_path(arguments->arena, S8("buster-c-nullptr"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 nullptr_command_line[] = {
        S8("-std=c23"),
        S8("-o"),
        nullptr_path,
        S8("tests/basic_c_nullptr.c"),
    };
    CompilerDriverResult nullptr_link = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_command_line)));
    BUSTER_TEST(arguments, nullptr_link.error == COMPILER_DRIVER_ERROR_NONE);
    if (nullptr_link.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 nullptr_run_arguments[] = {
            nullptr_path,
        };
        ProcessSpawnResult nullptr_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(nullptr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, nullptr_spawn.handle != 0);
        if (nullptr_spawn.handle)
        {
            ProcessWaitResult nullptr_wait = os_process_wait_sync(arguments->arena, nullptr_spawn);
            BUSTER_TEST(arguments, nullptr_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    {
        TemporalArena constexpr_temporary = arena_begin_temporal(arguments->arena);
        String8 constexpr_path = buster_test_temporary_path(constexpr_temporary.arena, S8("buster-c-constexpr"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
        String8 constexpr_command_line[] = {
            S8("-std=c23"),
            S8("-o"),
            constexpr_path,
            S8("tests/basic_c_constexpr.c"),
        };
        CompilerDriverResult constexpr_link = compiler_driver_execute_invocation(
            constexpr_temporary.arena, compiler_driver_parse_arguments(constexpr_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_command_line)));
        BUSTER_TEST(arguments, constexpr_link.error == COMPILER_DRIVER_ERROR_NONE);
        if (constexpr_link.error == COMPILER_DRIVER_ERROR_NONE)
        {
            String8 constexpr_run_arguments[] = {
                constexpr_path,
            };
            ProcessSpawnResult constexpr_spawn =
                os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(constexpr_run_arguments), (SliceString8){0}, (SliceString8){0},
                                 (ProcessSpawnOptions){
                                     .use_process_environment = true,
                                 });
            BUSTER_TEST(arguments, constexpr_spawn.handle != 0);
            if (constexpr_spawn.handle)
            {
                ProcessWaitResult constexpr_wait = os_process_wait_sync(constexpr_temporary.arena, constexpr_spawn);
                BUSTER_TEST(arguments, constexpr_wait.result == PROCESS_RESULT_SUCCESS);
            }
        }
        scratch_end(constexpr_temporary);
    }
#if BUSTER_LINUX
    String8 c_dynamic_library_path = buster_test_temporary_path(arguments->arena, S8("buster-c-dynamic-library"), S8(""));
    String8 c_dynamic_library_command_line[] = {
        S8("-o"),
        c_dynamic_library_path,
        S8("-l:libm.so.6"),
        S8("tests/basic_c_dynamic_library.c"),
    };
    CompilerDriverResult c_dynamic_library = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_dynamic_library_command_line)));
    BUSTER_TEST(arguments, c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_dynamic_library.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_dynamic_library.native_link.executable, S8("libm.so.6")));
        String8 run_arguments[] = {
            c_dynamic_library_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#endif
    String8 c_atomic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-atomic"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_atomic_command_line[] = {
        S8("-o"),
        c_atomic_path,
        S8("tests/basic_c_atomic.c"),
    };
    CompilerDriverResult c_atomic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_command_line)));
    BUSTER_TEST(arguments, c_atomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_atomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_atomic_arguments[] = {
            c_atomic_path,
        };
        ProcessSpawnResult c_atomic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_atomic_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_atomic_spawn.handle != 0);
        if (c_atomic_spawn.handle)
        {
            ProcessWaitResult c_atomic_wait = os_process_wait_sync(arguments->arena, c_atomic_spawn);
            BUSTER_TEST(arguments, c_atomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    TemporalArena c_stdatomic_temporary = arena_begin_temporal(arguments->arena);
    String8 c_stdatomic_path = buster_test_temporary_path(c_stdatomic_temporary.arena, S8("buster-c-stdatomic"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_stdatomic_command_line[] = {
        S8("-o"),
        c_stdatomic_path,
        S8("tests/basic_c_stdatomic.c"),
    };
    CompilerDriverResult c_stdatomic = compiler_driver_execute_invocation(
        c_stdatomic_temporary.arena,
        compiler_driver_parse_arguments(c_stdatomic_temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_command_line)));
    BUSTER_TEST(arguments, c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_stdatomic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_stdatomic_arguments[] = {
            c_stdatomic_path,
        };
        ProcessSpawnResult c_stdatomic_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_stdatomic_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_stdatomic_spawn.handle != 0);
        if (c_stdatomic_spawn.handle)
        {
            ProcessWaitResult c_stdatomic_wait = os_process_wait_sync(c_stdatomic_temporary.arena, c_stdatomic_spawn);
            BUSTER_TEST(arguments, c_stdatomic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_stdatomic_temporary);
    String8 c_generic_path = buster_test_temporary_path(arguments->arena, S8("buster-c-generic"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_generic_command_line[] = {
        S8("-o"),
        c_generic_path,
        S8("tests/basic_c_generic.c"),
    };
    CompilerDriverResult c_generic = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_command_line)));
    BUSTER_TEST(arguments, c_generic.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_generic.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_generic_arguments[] = {
            c_generic_path,
        };
        ProcessSpawnResult c_generic_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_generic_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_generic_spawn.handle != 0);
        if (c_generic_spawn.handle)
        {
            ProcessWaitResult c_generic_wait = os_process_wait_sync(arguments->arena, c_generic_spawn);
            BUSTER_TEST(arguments, c_generic_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_alignas_path = buster_test_temporary_path(arguments->arena, S8("buster-c-alignas"),
#if BUSTER_WINDOWS
                                                        S8(".exe"));
#else
                                                        S8(""));
#endif
    String8 c_alignas_command_line[] = {
        S8("-o"),
        c_alignas_path,
        S8("tests/basic_c_alignas.c"),
    };
    CompilerDriverResult c_alignas = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_command_line)));
    BUSTER_TEST(arguments, c_alignas.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_alignas.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_alignas_arguments[] = {
            c_alignas_path,
        };
        ProcessSpawnResult c_alignas_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_alignas_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_alignas_spawn.handle != 0);
        if (c_alignas_spawn.handle)
        {
            ProcessWaitResult c_alignas_wait = os_process_wait_sync(arguments->arena, c_alignas_spawn);
            BUSTER_TEST(arguments, c_alignas_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vla_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vla"),
#if BUSTER_WINDOWS
                                                    S8(".exe"));
#else
                                                    S8(""));
#endif
    String8 c_vla_command_line[] = {
        S8("-o"),
        c_vla_path,
        S8("tests/basic_c_vla.c"),
    };
    CompilerDriverResult c_vla = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_command_line)));
    BUSTER_TEST(arguments, c_vla.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_vla.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vla_arguments[] = {
            c_vla_path,
        };
        ProcessSpawnResult c_vla_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vla_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_vla_spawn.handle != 0);
        if (c_vla_spawn.handle)
        {
            ProcessWaitResult c_vla_wait = os_process_wait_sync(arguments->arena, c_vla_spawn);
            BUSTER_TEST(arguments, c_vla_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_aggregate_path = buster_test_temporary_path(arguments->arena, S8("buster-c-aggregate"),
#if BUSTER_WINDOWS
                                                          S8(".exe"));
#else
                                                          S8(""));
#endif
    String8 c_aggregate_command_line[] = {
        S8("-o"),
        c_aggregate_path,
        S8("tests/basic_c_argv_aggregate.c"),
    };
    CompilerDriverResult c_aggregate = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_command_line)));
    BUSTER_TEST(arguments, c_aggregate.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_aggregate.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_aggregate_arguments[] = {
            c_aggregate_path,
            S8("cc"),
            S8("a"),
            S8("b"),
        };
        ProcessSpawnResult c_aggregate_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_aggregate_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_aggregate_spawn.handle != 0);
        if (c_aggregate_spawn.handle)
        {
            ProcessWaitResult c_aggregate_wait = os_process_wait_sync(arguments->arena, c_aggregate_spawn);
            BUSTER_TEST(arguments, c_aggregate_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#if (BUSTER_LINUX && BUSTER_CPU_ARCH_X86_64) || BUSTER_MACOS
    String8 c_thread_local_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local"), S8(""));
    String8 c_thread_local_command_line[] = {
        S8("-o"),
        c_thread_local_path,
        S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_command_line)));
    BUSTER_TEST(arguments, c_thread_local.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_thread_local.has_object);
    if (c_thread_local.has_object)
    {
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_DATA].data.length == sizeof(u32));
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].data.length == 0);
        BUSTER_TEST(arguments, c_thread_local.object.sections[OBJECT_SECTION_THREAD_LOCAL_ZERO].virtual_size == sizeof(u32));
        bool found_thread_local_symbol = false;
        bool found_thread_local_zero_symbol = false;
        bool found_thread_local_relocation = false;
        for (u32 symbol_index = 0; symbol_index < c_thread_local.object.symbol_count; symbol_index += 1)
        {
            ObjectSymbol* symbol = &c_thread_local.object.symbols[symbol_index];
            found_thread_local_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_DATA && string_equal(symbol->name, S8("thread_local_value"));
            found_thread_local_zero_symbol |= symbol->section == OBJECT_SECTION_THREAD_LOCAL_ZERO && string_equal(symbol->name, S8("thread_local_zero"));
        }
        for (u32 relocation_index = 0; relocation_index < c_thread_local.object.relocation_count; relocation_index += 1)
        {
            ObjectRelocationKind kind = c_thread_local.object.relocations[relocation_index].kind;
            found_thread_local_relocation |= kind == OBJECT_RELOCATION_X86_64_TPOFF32 || kind == OBJECT_RELOCATION_X86_64_PE_TLS_INDEX_PC32 ||
                                             kind == OBJECT_RELOCATION_PE_TLS_OFFSET32 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_ADRP ||
                                             kind == OBJECT_RELOCATION_AARCH64_PE_TLS_INDEX_LO12 || kind == OBJECT_RELOCATION_AARCH64_PE_TLS_OFFSET12 ||
                                             kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_HI12 || kind == OBJECT_RELOCATION_AARCH64_TLSLE_ADD_TPREL_LO12 ||
                                             kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21 ||
                                             kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGEOFF12;
        }
        BUSTER_TEST(arguments, found_thread_local_symbol);
        BUSTER_TEST(arguments, found_thread_local_zero_symbol);
        BUSTER_TEST(arguments, found_thread_local_relocation);
    }
    if (c_thread_local.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_thread_local_arguments[] = {
            c_thread_local_path,
        };
        ProcessSpawnResult c_thread_local_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_thread_local_spawn.handle != 0);
        if (c_thread_local_spawn.handle)
        {
            ProcessWaitResult c_thread_local_wait = os_process_wait_sync(arguments->arena, c_thread_local_spawn);
            BUSTER_TEST(arguments, c_thread_local_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_thread_local_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-aarch64"), S8(""));
    String8 c_thread_local_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_thread_local_aarch64_path, S8("tests/basic_c_thread_local.c"),
    };
    CompilerDriverResult c_thread_local_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_thread_local_aarch64_command_line)));
    BUSTER_TEST(arguments, c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_thread_local_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice executable = c_thread_local_aarch64.native_link.executable;
        bool machine_matches = executable.length >= 20 && executable.pointer[18] == 183 && executable.pointer[19] == 0;
        bool found_thread_pointer_add = false;
        static u8 const thread_pointer_add[] = {
            0x29,
            0x41,
            0x00,
            0x91,
        };
        for (u64 byte_index = 0; byte_index + sizeof(thread_pointer_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, thread_pointer_add, sizeof(thread_pointer_add)) == 0)
            {
                found_thread_pointer_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, machine_matches);
        BUSTER_TEST(arguments, found_thread_pointer_add);
    }
    String8 c_thread_local_windows_targets[] = {
        S8("x86_64-pc-windows-msvc"),
        S8("aarch64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_windows_targets); target_index += 1)
    {
        String8 c_thread_local_windows_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-windows"), string_format(arguments->arena, S8("-{u32}.exe"), target_index));
        String8 windows_tls_command_line[] = {
            S8("-target"), c_thread_local_windows_targets[target_index], S8("-o"), c_thread_local_windows_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult windows_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(windows_tls_command_line)));
        BUSTER_TEST(arguments, windows_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (windows_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = windows_tls.native_link.executable;
            bool pe_header = executable.length > 0x84 && executable.pointer[0] == 'M' && executable.pointer[1] == 'Z' && executable.pointer[0x80] == 'P' &&
                             executable.pointer[0x81] == 'E';
            u32 tls_directory_rva = 0;
            if (executable.length >= 0x80 + 4 + 20 + 188)
            {
                memcpy(&tls_directory_rva, executable.pointer + 0x80 + 4 + 20 + 184, sizeof(tls_directory_rva));
            }
            BUSTER_TEST(arguments, pe_header);
            BUSTER_TEST(arguments, tls_directory_rva != 0);
        }
    }
    String8 c_thread_local_apple_targets[] = {
        S8("x86_64-apple-macos"),
        S8("arm64-apple-macos"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_thread_local_apple_targets); target_index += 1)
    {
        String8 apple_tls_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-thread-local-apple"), string_format(arguments->arena, S8("-{u32}"), target_index));
        String8 apple_tls_command_line[] = {
            S8("-target"),  c_thread_local_apple_targets[target_index], S8("-framework"), S8("CoreFoundation"), S8("-o"),
            apple_tls_path, S8("tests/basic_c_thread_local.c"),
        };
        CompilerDriverResult apple_tls = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(apple_tls_command_line)));
        BUSTER_TEST(arguments, apple_tls.error == COMPILER_DRIVER_ERROR_NONE);
        if (apple_tls.error == COMPILER_DRIVER_ERROR_NONE)
        {
            ByteSlice executable = apple_tls.native_link.executable;
            u32 mach_flags = 0;
            if (executable.length >= 28)
            {
                memcpy(&mach_flags, executable.pointer + 24, sizeof(mach_flags));
            }
            BUSTER_TEST(arguments, executable.length >= 32 && executable.pointer[0] == 0xcf && executable.pointer[1] == 0xfa && executable.pointer[2] == 0xed &&
                                       executable.pointer[3] == 0xfe);
            BUSTER_TEST(arguments, (mach_flags & 0x800000) != 0);
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_vars")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_data")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__thread_bss")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("__tlv_bootstrap")));
            BUSTER_TEST(arguments, compiler_driver_bytes_contain(executable, S8("/System/Library/Frameworks/"
                                                                                "CoreFoundation.framework/"
                                                                                "CoreFoundation")));
            bool found_tlv_relocation = false;
            for (u32 relocation_index = 0; relocation_index < apple_tls.object.relocation_count; relocation_index += 1)
            {
                ObjectRelocationKind kind = apple_tls.object.relocations[relocation_index].kind;
                found_tlv_relocation |= kind == OBJECT_RELOCATION_X86_64_MACH_TLV_PC32 || kind == OBJECT_RELOCATION_AARCH64_MACH_TLVP_PAGE21;
            }
            BUSTER_TEST(arguments, found_tlv_relocation);
        }
    }
    String8 c_float_abi_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi"), S8(""));
    String8 c_float_abi_command_line[] = {
        S8("-o"),
        c_float_abi_path,
        S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_command_line)));
    BUSTER_TEST(arguments, c_float_abi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_float_abi_arguments[] = {
            c_float_abi_path,
        };
        ProcessSpawnResult c_float_abi_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_float_abi_spawn.handle != 0);
        if (c_float_abi_spawn.handle)
        {
            ProcessWaitResult c_float_abi_wait = os_process_wait_sync(arguments->arena, c_float_abi_spawn);
            BUSTER_TEST(arguments, c_float_abi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector"),
#if BUSTER_WINDOWS
                                                       S8(".exe"));
#else
                                                       S8(""));
#endif
    String8 c_vector_command_line[] = {
        S8("-o"),
        c_vector_path,
        S8("tests/basic_c_vector.c"),
    };
    CompilerDriverResult c_vector = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_command_line)));
    BUSTER_TEST(arguments, c_vector.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, target_native.cpu_arch != CPU_ARCH_X86_64 || c_vector.codegen_statistics.native_vector_operation_count > 0);
    String8 c_vector_baseline_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-baseline"), S8(".o"));
    String8 c_vector_baseline_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-o"), c_vector_baseline_path, S8("tests/basic_c_vector.c"),
    };
    Arena* c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_baseline_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_baseline_command_line));
    CompilerDriverResult c_vector_baseline = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_baseline_invocation);
    BUSTER_TEST(arguments, c_vector_baseline.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.split_vector_operation_count == 6);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.vzeroupper_count == 0);
    BUSTER_TEST(arguments, c_vector_baseline.codegen_statistics.forwarded_wide_vector_load_count == 0);
    u64 c_vector_baseline_native_operations = c_vector_baseline.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx2_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx2"), S8(".o"));
    String8 c_vector_avx2_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=haswell"), S8("-o"), c_vector_avx2_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx2_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx2_command_line));
    CompilerDriverResult c_vector_avx2 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx2_invocation);
    BUSTER_TEST(arguments, c_vector_avx2.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.split_vector_operation_count == 4);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.native_vector_operation_count == c_vector_baseline_native_operations + 2);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.vzeroupper_count == 1);
    BUSTER_TEST(arguments, c_vector_avx2.codegen_statistics.forwarded_wide_vector_load_count == 1);
    u64 c_vector_avx2_native_operations = c_vector_avx2.codegen_statistics.native_vector_operation_count;
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    String8 c_vector_avx512_path = buster_test_temporary_path(arguments->arena, S8("buster-c-vector-avx512"), S8(".o"));
    String8 c_vector_avx512_command_line[] = {
        S8("-c"), S8("--target=x86_64-linux"), S8("-march=znver5"), S8("-o"), c_vector_avx512_path, S8("tests/basic_c_vector.c"),
    };
    c_vector_target_arena = arena_create((ArenaCreation){0});
    CompilerDriverInvocation c_vector_avx512_invocation =
        compiler_driver_parse_arguments(c_vector_target_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_avx512_command_line));
    CompilerDriverResult c_vector_avx512 = compiler_driver_execute_invocation(c_vector_target_arena, c_vector_avx512_invocation);
    BUSTER_TEST(arguments, c_vector_avx512.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.split_vector_operation_count == 0);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.native_vector_operation_count == c_vector_avx2_native_operations + 4);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.vzeroupper_count == 3);
    BUSTER_TEST(arguments, c_vector_avx512.codegen_statistics.forwarded_wide_vector_load_count == 3);
    BUSTER_TEST(arguments, arena_destroy(c_vector_target_arena, 1));
    if (c_vector.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_vector_arguments[] = {
            c_vector_path,
        };
        ProcessSpawnResult c_vector_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_arguments), (SliceString8){0}, (SliceString8){0},
                                                             (ProcessSpawnOptions){
                                                                 .use_process_environment = true,
                                                             });
        BUSTER_TEST(arguments, c_vector_spawn.handle != 0);
        if (c_vector_spawn.handle)
        {
            ProcessWaitResult c_vector_wait = os_process_wait_sync(arguments->arena, c_vector_spawn);
            BUSTER_TEST(arguments, c_vector_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_vector_cross_targets[] = {
        S8("x86_64-unknown-linux-gnu"),  S8("x86_64-pc-windows-msvc"),  S8("x86_64-apple-macos"),  S8("x86_64-linux-android"),  S8("x86_64-apple-ios"),
        S8("aarch64-unknown-linux-gnu"), S8("aarch64-pc-windows-msvc"), S8("aarch64-apple-macos"), S8("aarch64-linux-android"), S8("aarch64-apple-ios"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(c_vector_cross_targets); target_index += 1)
    {
        String8 c_vector_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-vector-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_vector_cross_command_line[] = {
            S8("-c"), S8("-target"), c_vector_cross_targets[target_index], S8("-o"), c_vector_cross_path, S8("tests/basic_c_vector.c"),
        };
        CompilerDriverResult c_vector_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_vector_cross_command_line)));
        BUSTER_TEST(arguments, c_vector_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_vector_cross.has_object);
    }
    String8 c_conversions_path = buster_test_temporary_path(arguments->arena, S8("buster-c-conversions"), S8(""));
    String8 c_conversions_command_line[] = {
        S8("-o"),
        c_conversions_path,
        S8("tests/basic_c_conversions.c"),
    };
    CompilerDriverResult c_conversions = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_command_line)));
    BUSTER_TEST(arguments, c_conversions.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_conversions.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_conversions_arguments[] = {
            c_conversions_path,
        };
        ProcessSpawnResult c_conversions_spawn =
            os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_arguments), (SliceString8){0}, (SliceString8){0},
                             (ProcessSpawnOptions){
                                 .use_process_environment = true,
                             });
        BUSTER_TEST(arguments, c_conversions_spawn.handle != 0);
        if (c_conversions_spawn.handle)
        {
            ProcessWaitResult c_conversions_wait = os_process_wait_sync(arguments->arena, c_conversions_spawn);
            BUSTER_TEST(arguments, c_conversions_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 conversion_cross_targets[] = {
        S8("aarch64-unknown-linux-gnu"),
        S8("x86_64-pc-windows-msvc"),
    };
    for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(conversion_cross_targets); target_index += 1)
    {
        String8 c_conversions_cross_path =
            buster_test_temporary_path(arguments->arena, S8("buster-c-conversions-cross"), string_format(arguments->arena, S8("-{u32}.o"), target_index));
        String8 c_conversions_cross_command_line[] = {
            S8("-c"), S8("-target"), conversion_cross_targets[target_index], S8("-o"), c_conversions_cross_path, S8("tests/basic_c_conversions.c"),
        };
        CompilerDriverResult c_conversions_cross = compiler_driver_execute_invocation(
            arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_conversions_cross_command_line)));
        BUSTER_TEST(arguments, c_conversions_cross.error == COMPILER_DRIVER_ERROR_NONE);
        BUSTER_TEST(arguments, c_conversions_cross.has_object);
    }
    String8 c_float_abi_aarch64_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-aarch64"), S8(""));
    String8 c_float_abi_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_float_abi_aarch64_path, S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_aarch64 = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_aarch64_command_line)));
    BUSTER_TEST(arguments, c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const aarch64_float_add[] = {
            0x00,
            0x28,
            0x61,
            0x1e,
        };
        ByteSlice executable = c_float_abi_aarch64.native_link.executable;
        bool found_float_add = false;
        for (u64 byte_index = 0; byte_index + sizeof(aarch64_float_add) <= executable.length; byte_index += 1)
        {
            if (memcmp(executable.pointer + byte_index, aarch64_float_add, sizeof(aarch64_float_add)) == 0)
            {
                found_float_add = true;
                break;
            }
        }
        BUSTER_TEST(arguments, found_float_add);
    }
    String8 c_float_abi_windows_path = buster_test_temporary_path(arguments->arena, S8("buster-c-float-abi-windows"), S8(".obj"));
    String8 c_float_abi_windows_command_line[] = {
        S8("-c"), S8("-target"), S8("x86_64-pc-windows-msvc"), S8("-o"), c_float_abi_windows_path, S8("tests/basic_c_float_abi.c"),
    };
    CompilerDriverResult c_float_abi_windows = compiler_driver_execute_invocation(
        arguments->arena, compiler_driver_parse_arguments(arguments->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_float_abi_windows_command_line)));
    BUSTER_TEST(arguments, c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_float_abi_windows.error == COMPILER_DRIVER_ERROR_NONE)
    {
        static u8 const shadow_space[] = {
            0x48,
            0x83,
            0xec,
            0x20,
        };
        static u8 const load_xmm0[] = {
            0xf2,
            0x0f,
            0x10,
            0x85,
        };
        static u8 const load_indirect_second_part[] = {
            0x48,
            0x8b,
            0x41,
            0x08,
        };
        ByteSlice text = c_float_abi_windows.object.sections[OBJECT_SECTION_TEXT].data;
        bool found_shadow_space = false;
        bool found_load_xmm0 = false;
        bool found_indirect_second_part = false;
        for (u64 byte_index = 0; byte_index < text.length; byte_index += 1)
        {
            if (byte_index + sizeof(shadow_space) <= text.length && memcmp(text.pointer + byte_index, shadow_space, sizeof(shadow_space)) == 0)
            {
                found_shadow_space = true;
            }
            if (byte_index + sizeof(load_xmm0) <= text.length && memcmp(text.pointer + byte_index, load_xmm0, sizeof(load_xmm0)) == 0)
            {
                found_load_xmm0 = true;
            }
            if (byte_index + sizeof(load_indirect_second_part) <= text.length &&
                memcmp(text.pointer + byte_index, load_indirect_second_part, sizeof(load_indirect_second_part)) == 0)
            {
                found_indirect_second_part = true;
            }
        }
        BUSTER_TEST(arguments, found_shadow_space);
        BUSTER_TEST(arguments, found_load_xmm0);
        BUSTER_TEST(arguments, found_indirect_second_part);
    }
    Arena* c_asm_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_asm_temporary = scratch_begin(c_asm_conflicts, BUSTER_ARRAY_LENGTH(c_asm_conflicts));
    Arena* c_asm_arena = c_asm_temporary.arena;
    String8 c_asm_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm"), S8(""));
    String8 c_asm_command_line[] = {
        S8("-o"),
        c_asm_path,
        S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm =
        compiler_driver_execute_invocation(c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_command_line)));
    BUSTER_TEST(arguments, c_asm.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_asm_arguments[] = {
            c_asm_path,
        };
        ProcessSpawnResult c_asm_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_arguments), (SliceString8){0}, (SliceString8){0},
                                                          (ProcessSpawnOptions){
                                                              .use_process_environment = true,
                                                          });
        BUSTER_TEST(arguments, c_asm_spawn.handle != 0);
        if (c_asm_spawn.handle)
        {
            ProcessWaitResult c_asm_wait = os_process_wait_sync(c_asm_arena, c_asm_spawn);
            BUSTER_TEST(arguments, c_asm_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_asm_aarch64_path = buster_test_temporary_path(c_asm_arena, S8("buster-c-asm-aarch64"), S8(""));
    String8 c_asm_aarch64_command_line[] = {
        S8("-target"), S8("aarch64-unknown-linux-gnu"), S8("-o"), c_asm_aarch64_path, S8("tests/basic_c_asm.c"),
    };
    CompilerDriverResult c_asm_aarch64 = compiler_driver_execute_invocation(
        c_asm_arena, compiler_driver_parse_arguments(c_asm_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_asm_aarch64_command_line)));
    BUSTER_TEST(arguments, c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_asm_aarch64.error == COMPILER_DRIVER_ERROR_NONE)
    {
        BUSTER_TEST(arguments, compiler_driver_bytes_contain(c_asm_aarch64.native_link.executable, (String8){
                                                                                                       .pointer =
                                                                                                           (char8*)(u8[]){
                                                                                                               0x1f,
                                                                                                               0x20,
                                                                                                               0x03,
                                                                                                               0xd5,
                                                                                                           },
                                                                                                       .length = 4,
                                                                                                   }));
    }
    scratch_end(c_asm_temporary);
#endif
    Arena* c_multi_conflicts[] = {
        arguments->arena,
    };
    TemporalArena c_multi_temporary = scratch_begin(c_multi_conflicts, BUSTER_ARRAY_LENGTH(c_multi_conflicts));
    Arena* c_multi_arena = c_multi_temporary.arena;
    String8 c_multi_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_multi_command_line[] = {
        S8("-o"),
        c_multi_path,
        S8("tests/basic_c_multi_main.c"),
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_command_line)));
    BUSTER_TEST(arguments, c_multi.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_multi.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_multi_arguments[] = {
            c_multi_path,
        };
        ProcessSpawnResult c_multi_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_multi_spawn.handle != 0);
        if (c_multi_spawn.handle)
        {
            ProcessWaitResult c_multi_wait = os_process_wait_sync(c_multi_arena, c_multi_spawn);
            BUSTER_TEST(arguments, c_multi_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_multi_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-multi-object"),
#if BUSTER_WINDOWS
                                                             S8(".obj"));
#else
                                                             S8(".o"));
#endif
    String8 c_multi_object_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_multi_object_path,
        S8("tests/basic_c_multi_add.c"),
    };
    CompilerDriverResult c_multi_object = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_multi_object_command_line)));
    BUSTER_TEST(arguments, c_multi_object.error == COMPILER_DRIVER_ERROR_NONE);
    String8 c_mixed_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-mixed-driver"),
#if BUSTER_WINDOWS
                                                      S8(".exe"));
#else
                                                      S8(""));
#endif
    String8 c_mixed_command_line[] = {
        S8("-o"),
        c_mixed_path,
        S8("tests/basic_c_multi_main.c"),
        c_multi_object_path,
    };
    CompilerDriverResult c_mixed = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_command_line)));
    BUSTER_TEST(arguments, c_mixed.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_mixed.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_mixed_arguments[] = {
            c_mixed_path,
        };
        ProcessSpawnResult c_mixed_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_mixed_arguments), (SliceString8){0}, (SliceString8){0},
                                                            (ProcessSpawnOptions){
                                                                .use_process_environment = true,
                                                            });
        BUSTER_TEST(arguments, c_mixed_spawn.handle != 0);
        if (c_mixed_spawn.handle)
        {
            ProcessWaitResult c_mixed_wait = os_process_wait_sync(c_multi_arena, c_mixed_spawn);
            BUSTER_TEST(arguments, c_mixed_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    String8 c_archive_bias_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-bias"),
#if BUSTER_WINDOWS
                                                                    S8(".obj"));
#else
                                                                    S8(".o"));
#endif
    String8 c_archive_add_object_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-add"),
#if BUSTER_WINDOWS
                                                                   S8(".obj"));
#else
                                                                   S8(".o"));
#endif
    String8 c_archive_bias_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_bias_object_path,
        S8("tests/basic_c_archive_bias.c"),
    };
    String8 c_archive_add_command_line[] = {
        S8("-c"),
        S8("-o"),
        c_archive_add_object_path,
        S8("tests/basic_c_archive_add.c"),
    };
    CompilerDriverResult c_archive_bias = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_bias_command_line)));
    CompilerDriverResult c_archive_add = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_add_command_line)));
    BUSTER_TEST(arguments, c_archive_bias.error == COMPILER_DRIVER_ERROR_NONE);
    BUSTER_TEST(arguments, c_archive_add.error == COMPILER_DRIVER_ERROR_NONE);
    FileMapRead c_archive_bias_map = file_map_read(c_multi_arena, c_archive_bias_object_path, (FileReadOptions){0});
    FileMapRead c_archive_add_map = file_map_read(c_multi_arena, c_archive_add_object_path, (FileReadOptions){0});
    ByteSlice c_archive_members[] = {
        c_archive_bias_map.bytes,
        c_archive_add_map.bytes,
    };
    String8 c_archive_names[] = {
        S8("bias.o"),
        S8("add.o"),
    };
    ByteSlice c_archive_bytes = compiler_driver_test_archive(c_multi_arena, c_archive_members, c_archive_names, BUSTER_ARRAY_LENGTH(c_archive_members));
    file_map_unmap(c_archive_bias_map);
    file_map_unmap(c_archive_add_map);
    String8 c_archive_directory = buster_test_temporary_path(c_multi_arena, S8("buster-c-driver-archive"), S8(""));
    os_make_directory(c_archive_directory);
    String8 c_archive_path = string_format_z(c_multi_arena,
#if BUSTER_WINDOWS
                                             S8("{S8}/archive_chain.lib"),
#else
                                             S8("{S8}/libarchive_chain.a"),
#endif
                                             c_archive_directory);
    BUSTER_TEST(arguments, file_write(c_archive_path, c_archive_bytes));
    String8 c_archive_executable_path = buster_test_temporary_path(c_multi_arena, S8("buster-c-archive-driver"),
#if BUSTER_WINDOWS
                                                                   S8(".exe"));
#else
                                                                   S8(""));
#endif
    String8 c_archive_command_line[] = {
        S8("-o"), c_archive_executable_path, S8("tests/basic_c_multi_main.c"), S8("-L"), c_archive_directory, S8("-larchive_chain"),
    };
    CompilerDriverResult c_archive = compiler_driver_execute_invocation(
        c_multi_arena, compiler_driver_parse_arguments(c_multi_arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_command_line)));
    BUSTER_TEST(arguments, c_archive.error == COMPILER_DRIVER_ERROR_NONE);
    if (c_archive.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 c_archive_arguments[] = {
            c_archive_executable_path,
        };
        ProcessSpawnResult c_archive_spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(c_archive_arguments), (SliceString8){0}, (SliceString8){0},
                                                              (ProcessSpawnOptions){
                                                                  .use_process_environment = true,
                                                              });
        BUSTER_TEST(arguments, c_archive_spawn.handle != 0);
        if (c_archive_spawn.handle)
        {
            ProcessWaitResult c_archive_wait = os_process_wait_sync(c_multi_arena, c_archive_spawn);
            BUSTER_TEST(arguments, c_archive_wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
    scratch_end(c_multi_temporary);
    String8 source_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-test"), S8(".bbb"));
    String8 output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-test"),
#if BUSTER_WINDOWS
                                                     S8(".exe"));
#else
                                                     S8(""));
#endif
    String8 source = S8("code main[export] : fn () s32\n"
                        "{\n"
                        "    return 0;\n"
                        "}\n");
    BUSTER_TEST(arguments, file_write(source_path, (ByteSlice){
                                                       .pointer = (u8*)source.pointer,
                                                       .length = source.length,
                                                   }));
    CompilerDriverResult compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                 .source_path = source_path,
                                                                                 .output_path = output_path,
                                                                                 .target = target_native,
                                                                             });
    if (compile.error != COMPILER_DRIVER_ERROR_NONE && compile.diagnostic.length)
    {
        arguments->show(arguments, S8("compiler driver error: {S8}\n"), compile.diagnostic);
    }
    BUSTER_TEST(arguments, compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#if BUSTER_LINUX
    // The buster driver path must also emit resolvable DWARF when asked.
    String8 buster_debug_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-debug"), S8(""));
    CompilerDriverResult buster_debug_compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                              .source_path = source_path,
                                                                                              .output_path = buster_debug_path,
                                                                                              .target = target_native,
                                                                                              .debug_info = true,
                                                                                          });
    BUSTER_TEST(arguments, buster_debug_compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (buster_debug_compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        ByteSlice buster_debug_image = file_read(arguments->arena, buster_debug_path, (FileReadOptions){0});
        ByteSlice buster_debug_line = compiler_driver_test_elf_section(buster_debug_image, S8(".debug_line"));
        ByteSlice buster_debug_info = compiler_driver_test_elf_section(buster_debug_image, S8(".debug_info"));
        u64 buster_debug_text_address = compiler_driver_test_elf_section_address(buster_debug_image, S8(".text"));
        BUSTER_TEST(arguments, buster_debug_line.length > 4);
        BUSTER_TEST(arguments, buster_debug_info.length > 11);
        BUSTER_TEST(arguments, buster_debug_text_address != 0);
        DwarfLineRow buster_debug_row = {0};
        BUSTER_TEST(arguments, dwarf_line_lookup(buster_debug_line, buster_debug_text_address, &buster_debug_row));
        BUSTER_TEST(arguments, buster_debug_row.line == 1 && buster_debug_row.file == 1);
    }
#endif
    String8 module_directory = buster_test_temporary_path(arguments->arena, S8("buster-driver-modules"), S8(""));
    String8 module_child_directory = string_format_z(arguments->arena, S8("{S8}/core"), module_directory);
    String8 module_root_path = string_format_z(arguments->arena, S8("{S8}/main.bbb"), module_directory);
    String8 module_dependency_path = string_format_z(arguments->arena, S8("{S8}/core/math.bbb"), module_directory);
    String8 module_output_path = buster_test_temporary_path(arguments->arena, S8("buster-driver-modules-executable"),
#if BUSTER_WINDOWS
                                                            S8(".exe"));
#else
                                                            S8(""));
#endif
    os_make_directory(module_directory);
    os_make_directory(module_child_directory);
    String8 module_root_source = S8("import math = \"core/math\";\n"
                                    "code main[export] : fn[cc(c)] () s32\n"
                                    "{\n"
                                    "    return math.answer() - 42;\n"
                                    "}\n");
    String8 module_dependency_source = S8("code answer : fn () s32\n"
                                          "{\n"
                                          "    return 42;\n"
                                          "}\n");
    BUSTER_TEST(arguments, file_write(module_root_path, (ByteSlice){
                                                            .pointer = (u8*)module_root_source.pointer,
                                                            .length = module_root_source.length,
                                                        }));
    BUSTER_TEST(arguments, file_write(module_dependency_path, (ByteSlice){
                                                                  .pointer = (u8*)module_dependency_source.pointer,
                                                                  .length = module_dependency_source.length,
                                                              }));
    CompilerDriverResult module_compile = compiler_driver_compile(arguments->arena, (CompilerDriverOptions){
                                                                                        .source_path = module_root_path,
                                                                                        .output_path = module_output_path,
                                                                                        .module_root = module_directory,
                                                                                        .target = target_native,
                                                                                    });
    if (module_compile.error != COMPILER_DRIVER_ERROR_NONE && module_compile.diagnostic.length)
    {
        arguments->show(arguments, S8("module compiler driver error: {S8}\n"), module_compile.diagnostic);
    }
    BUSTER_TEST(arguments, module_compile.error == COMPILER_DRIVER_ERROR_NONE);
    if (module_compile.error == COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            module_output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(run_arguments), (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){
                                                        .use_process_environment = true,
                                                    });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait = os_process_wait_sync(arguments->arena, spawn);
            BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_SUCCESS);
        }
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
