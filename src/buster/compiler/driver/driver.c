#include <buster/compiler/driver/driver.h>

#include <buster/compiler/frontend/buster/parser.h>
#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/compiler/ir/ir.h>
#include <buster/compiler/codegen/codegen.h>
#include <buster/compiler/object/object.h>
#include <buster/file.h>
#include <buster/string.h>

BUSTER_GLOBAL_LOCAL String8
compiler_driver_parser_diagnostic(
    Arena* arena,
    String8 path,
    ParserDiagnostic* diagnostic)
{
    if (!diagnostic)
    {
        return string_format(
            arena,
            S8("{S8}: parsing failed"),
            path);
    }
    return string_format(
        arena,
        S8("{S8}:{u32}:{u32}: {S8}"),
        path,
        diagnostic->range.line,
        diagnostic->range.column,
        diagnostic->message);
}

CompilerDriverResult compiler_driver_compile_with_libc(
    Arena* arena,
    CompilerDriverOptions options)
{
    CompilerDriverResult result = {0};
    if (!arena ||
        !options.source_path.length ||
        !options.output_path.length ||
        !options.object_path.length)
    {
        result.error =
            COMPILER_DRIVER_ERROR_INVALID_INPUT;
        return result;
    }
    if (options.target.cpu_arch >= CPU_ARCH_COUNT ||
        options.target.os >= OPERATING_SYSTEM_COUNT)
    {
        options.target = target_native;
    }
    Arena* expression_arena =
        arena_create((ArenaCreation){0});
    if (!expression_arena)
    {
        result.error =
            COMPILER_DRIVER_ERROR_INVALID_INPUT;
        return result;
    }
    AnalysisProgram analysis =
        analysis_program_load(
            arena,
            expression_arena,
            (AnalysisProgramOptions){
                .root_path = options.source_path,
                .module_root =
                    options.module_root.length ?
                        options.module_root :
                        S8("."),
                .pointer_size = 8,
                .pointer_alignment = 8,
            });
    arena_destroy(expression_arena, 1);
    result.parser_diagnostic_count =
        analysis.parser_diagnostic_count;
    result.analysis_diagnostic_count =
        analysis.analysis_diagnostic_count;
    if (analysis.load_failed)
    {
        result.error =
            COMPILER_DRIVER_ERROR_FILE_READ;
        result.diagnostic = string_format(
            arena,
            S8("could not load {S8} or one of its imported modules"),
            options.source_path);
        return result;
    }
    if (analysis.parser_diagnostic_count)
    {
        result.error =
            COMPILER_DRIVER_ERROR_PARSE;
        for (u32 module_index = 0;
            module_index < analysis.module_count;
            module_index += 1)
        {
            AnalysisProgramModule* module =
                &analysis.modules[module_index];
            if (module->parser.first_diagnostic)
            {
                result.diagnostic =
                    compiler_driver_parser_diagnostic(
                        arena,
                        module->path,
                        module->parser.first_diagnostic);
                break;
            }
        }
        if (!result.diagnostic.length)
        {
            result.diagnostic = string_format(
                arena,
                S8("tokenization failed with {u32} error(s)"),
                analysis.parser_diagnostic_count);
        }
        return result;
    }
    if (analysis.analysis_diagnostic_count)
    {
        result.error =
            COMPILER_DRIVER_ERROR_ANALYSIS;
        for (u32 module_index = 0;
            module_index < analysis.module_count;
            module_index += 1)
        {
            AnalysisResult* module =
                analysis.module_results[module_index];
            if (module && module->first_diagnostic)
            {
                result.diagnostic =
                    analysis_format_diagnostic(
                        arena,
                        module,
                        module->first_diagnostic);
                break;
            }
        }
        return result;
    }
    IrProgram ir = ir_generate_program(arena, &analysis);
    ObjectFile* objects = arena_allocate(
        arena,
        ObjectFile,
        analysis.module_count);
    u32 object_count = 0;
    for (u32 module_index = 0;
        module_index < analysis.module_count;
        module_index += 1)
    {
        AnalysisResult* module_analysis =
            analysis.module_results[module_index];
        if (!module_analysis)
        {
            continue;
        }
        IrModule* module_ir = &ir.modules[module_index];
        IrValidationResult validation =
            ir_validate_module(
                module_analysis,
                module_ir);
        if (validation.error != IR_VALIDATION_NONE)
        {
            result.error = COMPILER_DRIVER_ERROR_IR;
            result.diagnostic = string_format(
                arena,
                S8("IR validation failed in module {S8} with error {u32}"),
                module_analysis->module.name,
                (u32)validation.error);
            return result;
        }
        CodegenModule code = codegen_generate_module(
            arena,
            module_analysis,
            module_ir,
            options.target);
        result.codegen_error = code.error;
        if (code.error != CODEGEN_ERROR_NONE)
        {
            result.error =
                COMPILER_DRIVER_ERROR_CODEGEN;
            result.diagnostic = string_format(
                arena,
                S8("native code generation failed in module {S8} with error {u32}"),
                module_analysis->module.name,
                (u32)code.error);
            return result;
        }
        ObjectFile object =
            object_from_codegen_module(
                arena,
                module_analysis,
                &code,
                options.target);
        result.object_error = object.error;
        if (object.error != OBJECT_ERROR_NONE)
        {
            result.error =
                COMPILER_DRIVER_ERROR_OBJECT;
            result.diagnostic = string_format(
                arena,
                S8("object generation failed in module {S8} with error {u32}"),
                module_analysis->module.name,
                (u32)object.error);
            return result;
        }
        objects[object_count++] = object;
    }
    if (!object_count)
    {
        result.error =
            COMPILER_DRIVER_ERROR_INVALID_INPUT;
        result.diagnostic = S8(
            "the program contains no compilable modules");
        return result;
    }
    LinkObjectResult linked = link_objects(
        arena,
        objects,
        object_count,
        (LinkOptions){
            .allow_undefined_symbols = true,
        });
    if (linked.error != LINK_ERROR_NONE)
    {
        result.error =
            COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic = string_format(
            arena,
            S8("object linking failed with error {u32}: {S8}"),
            (u32)linked.error,
            linked.symbol);
        return result;
    }
    result.link = link_object_with_libc(
        arena,
        &linked.object,
        (LibcLinkOptions){
            .output_path = options.output_path,
            .object_path = options.object_path,
            .linker_executable =
                options.linker_executable,
        });
    if (result.link.error != LINK_ERROR_NONE)
    {
        result.error =
            COMPILER_DRIVER_ERROR_LINK;
        result.diagnostic =
            result.link.standard_error.length ?
                (String8){
                    .pointer = (char8*)
                        result.link.standard_error.pointer,
                    .length =
                        result.link.standard_error.length,
                } :
                string_format(
                    arena,
                    S8("libc linking failed with error {u32}"),
                    (u32)result.link.error);
    }
    return result;
}

#if BUSTER_INCLUDE_TESTS
UnitTestResult compiler_driver_tests(
    UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINK_LIBC && !BUSTER_ANDROID && !BUSTER_IOS && \
    !BUSTER_SANITIZE
    String8 source_path =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-test"),
            S8(".bbb"));
    String8 object_path =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-test"),
#if BUSTER_WINDOWS
            S8(".obj"));
#else
            S8(".o"));
#endif
    String8 output_path =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-test"),
#if BUSTER_WINDOWS
            S8(".exe"));
#else
            S8(""));
#endif
    String8 source = S8(
        "code main[export] : fn () s32\n"
        "{\n"
        "    return 0;\n"
        "}\n");
    BUSTER_TEST(arguments,
        file_write(
            source_path,
            (ByteSlice){
                .pointer = (u8*)source.pointer,
                .length = source.length,
            }));
    CompilerDriverResult compile =
        compiler_driver_compile_with_libc(
            arguments->arena,
            (CompilerDriverOptions){
                .source_path = source_path,
                .output_path = output_path,
                .object_path = object_path,
                .target = target_native,
            });
    if (compile.error !=
            COMPILER_DRIVER_ERROR_NONE &&
        compile.diagnostic.length)
    {
        arguments->show(
            arguments,
            S8("compiler driver error: {S8}\n"),
            compile.diagnostic);
    }
    BUSTER_TEST(arguments,
        compile.error ==
            COMPILER_DRIVER_ERROR_NONE);
    if (compile.error ==
        COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn(
            (SliceString8)
                BUSTER_ARRAY_TO_SLICE(
                    run_arguments),
            (SliceString8){0},
            (SliceString8){0},
            (ProcessSpawnOptions){
                .use_process_environment = true,
            });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
    String8 module_directory =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-modules"),
            S8(""));
    String8 module_child_directory = string_format_z(
        arguments->arena,
        S8("{S8}/core"),
        module_directory);
    String8 module_root_path = string_format_z(
        arguments->arena,
        S8("{S8}/main.bbb"),
        module_directory);
    String8 module_dependency_path = string_format_z(
        arguments->arena,
        S8("{S8}/core/math.bbb"),
        module_directory);
    String8 module_object_path =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-modules"),
#if BUSTER_WINDOWS
            S8(".obj"));
#else
            S8(".o"));
#endif
    String8 module_output_path =
        buster_test_temporary_path(
            arguments->arena,
            S8("buster-driver-modules-executable"),
#if BUSTER_WINDOWS
            S8(".exe"));
#else
            S8(""));
#endif
    os_make_directory(module_directory);
    os_make_directory(module_child_directory);
    String8 module_root_source = S8(
        "import math = \"core/math\";\n"
        "code main[export] : fn[cc(c)] () s32\n"
        "{\n"
        "    return math.answer() - 42;\n"
        "}\n");
    String8 module_dependency_source = S8(
        "code answer : fn () s32\n"
        "{\n"
        "    return 42;\n"
        "}\n");
    BUSTER_TEST(arguments,
        file_write(
            module_root_path,
            (ByteSlice){
                .pointer =
                    (u8*)module_root_source.pointer,
                .length =
                    module_root_source.length,
            }));
    BUSTER_TEST(arguments,
        file_write(
            module_dependency_path,
            (ByteSlice){
                .pointer =
                    (u8*)module_dependency_source.pointer,
                .length =
                    module_dependency_source.length,
            }));
    CompilerDriverResult module_compile =
        compiler_driver_compile_with_libc(
            arguments->arena,
            (CompilerDriverOptions){
                .source_path = module_root_path,
                .output_path = module_output_path,
                .object_path = module_object_path,
                .module_root = module_directory,
                .target = target_native,
            });
    if (module_compile.error !=
            COMPILER_DRIVER_ERROR_NONE &&
        module_compile.diagnostic.length)
    {
        arguments->show(
            arguments,
            S8("module compiler driver error: {S8}\n"),
            module_compile.diagnostic);
    }
    BUSTER_TEST(arguments,
        module_compile.error ==
            COMPILER_DRIVER_ERROR_NONE);
    if (module_compile.error ==
        COMPILER_DRIVER_ERROR_NONE)
    {
        String8 run_arguments[] = {
            module_output_path,
        };
        ProcessSpawnResult spawn = os_process_spawn(
            (SliceString8)
                BUSTER_ARRAY_TO_SLICE(
                    run_arguments),
            (SliceString8){0},
            (SliceString8){0},
            (ProcessSpawnOptions){
                .use_process_environment = true,
            });
        BUSTER_TEST(arguments, spawn.handle != 0);
        if (spawn.handle)
        {
            ProcessWaitResult wait =
                os_process_wait_sync(
                    arguments->arena,
                    spawn);
            BUSTER_TEST(arguments,
                wait.result ==
                    PROCESS_RESULT_SUCCESS);
        }
    }
#else
    BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
