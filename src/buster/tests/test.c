#include <buster/tests/test.h>

#include <buster/lib/os.h>
#include <buster/lib/arena.h>
#include <buster/lib/string.h>
#include <buster/lib/file.h>
#include <buster/lib/hash.h>
#include <buster/lib/compiler/frontend/buster/parser.h>
#include <buster/lib/compiler/frontend/buster/analysis.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/assembly/assembly.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/compiler/ir/interpreter.h>
#include <buster/lib/compiler/dwarf/dwarf.h>
#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/compiler/link/link.h>
#include <buster/lib/compiler/driver/driver.h>
#include <buster/lib/ide_document.h>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/lib/x86_64.h>
#endif

#if BUSTER_INCLUDE_TESTS
#include <buster/tests/arena_test.h>
#include <buster/tests/hash_test.h>
#include <buster/tests/string_test.h>
#include <buster/tests/os_test.h>
#include <buster/tests/file_test.h>
#include <buster/tests/ide_document_test.h>
#include <buster/tests/window_test.h>
#include <buster/tests/rendering_test.h>
#include <buster/tests/target_test.h>
#include <buster/tests/compiler/frontend/buster/parser_test.h>
#include <buster/tests/compiler/frontend/buster/analysis_test.h>
#include <buster/tests/compiler/frontend/c/c_test.h>
#include <buster/tests/compiler/assembly/assembly_test.h>
#include <buster/tests/compiler/ir/ir_test.h>
#include <buster/tests/compiler/ir/interpreter_test.h>
#include <buster/tests/compiler/codegen/codegen_test.h>
#include <buster/tests/compiler/debug/debug_test.h>
#include <buster/tests/compiler/dwarf/dwarf_test.h>
#include <buster/tests/compiler/codeview/codeview_test.h>
#include <buster/tests/compiler/pdb/pdb_test.h>
#include <buster/tests/compiler/object/object_test.h>
#include <buster/tests/compiler/link/link_test.h>
#include <buster/tests/compiler/driver/driver_test.h>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.h>
#endif

#include <buster/tests/arena_test.c>
#include <buster/tests/hash_test.c>
#include <buster/tests/string_test.c>
#include <buster/tests/os_test.c>
#include <buster/tests/file_test.c>
#include <buster/tests/ide_document_test.c>
#include <buster/tests/window_test.c>
#include <buster/tests/rendering_test.c>
#include <buster/tests/target_test.c>
#include <buster/tests/compiler/frontend/buster/parser_test.c>
#include <buster/tests/compiler/frontend/buster/analysis_test.c>
#include <buster/tests/compiler/frontend/c/c_test.c>
#include <buster/tests/compiler/assembly/assembly_test.c>
#include <buster/tests/compiler/ir/ir_test.c>
#include <buster/tests/compiler/ir/interpreter_test.c>
#include <buster/tests/compiler/codegen/codegen_test.c>
#include <buster/tests/compiler/debug/debug_test.c>
#include <buster/tests/compiler/dwarf/dwarf_test.c>
#include <buster/tests/compiler/codeview/codeview_test.c>
#include <buster/tests/compiler/pdb/pdb_test.c>
#include <buster/tests/compiler/object/object_test.c>
#include <buster/tests/compiler/link/link_test.c>
#include <buster/tests/compiler/driver/driver_test.c>
#if BUSTER_CPU_ARCH_X86_64
#include <buster/tests/x86_64_test.c>
#endif
#endif

bool unit_test_succeeded(UnitTestResult result)
{
    return result.succeeded_test_count == result.test_count;
}

void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test)
{
    batch->succeeded_unit_test_count += unit_test.succeeded_test_count;
    batch->unit_test_count += unit_test.test_count;
    batch->succeeded_module_test_count += unit_test_succeeded(unit_test);
    batch->module_test_count += 1;
}

void consume_external_tests(BatchTestResult* batch, ProcessResult result)
{
    batch->succeeded_external_test_count += result == PROCESS_RESULT_SUCCESS;
    batch->external_test_count += 1;
}

void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 message = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    string_print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
    scratch_end(scratch);

    if (is_debugger_present())
    {
        os_fail();
    }
}

String8 buster_test_temporary_path(Arena* arena, String8 name, String8 suffix)
{
#if BUSTER_WINDOWS
    String8 prefix = S8("build/");
#elif BUSTER_ANDROID
    String8 prefix = buster_android_internal_data_path.length ? buster_android_internal_data_path : S8(".");
    String8 separator = prefix.pointer[prefix.length - 1] == '/' ? S8("") : S8("/");
    return string_format_z(arena, S8("{S8}{S8}{S8}-{u64}{S8}"), prefix, separator, name, os_get_current_process_id(), suffix);
#else
    String8 prefix = S8("/tmp/");
#endif
    return string_format_z(arena, S8("{S8}{S8}-{u64}{S8}"), prefix, name, os_get_current_process_id(), suffix);
}

void default_show(UnitTestArguments* arguments, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    TemporalArena scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    String8 string = string_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    if (string.length)
    {
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }

    scratch_end(scratch);
}

bool batch_test_succeeded(BatchTestResult test)
{
    bool unit_result = test.succeeded_unit_test_count == test.unit_test_count;
    bool module_result = test.succeeded_module_test_count == test.module_test_count;
    bool external_result = test.succeeded_external_test_count == test.external_test_count;

    bool result = unit_result && module_result && external_result;
    return result;
}

bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test)
{
    arguments->show(arguments, S8("[{u64}/{u64}] Unit tests\n"), test.succeeded_unit_test_count, test.unit_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] Module tests\n"), test.succeeded_module_test_count, test.module_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] External tests\n"), test.succeeded_external_test_count, test.external_test_count);
    return batch_test_succeeded(test);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL TestFunction* test_functions[] = {
    &arena_tests,
    &hash_tests,
    &string_tests,
    &os_tests,
    &file_tests,
    &ide_document_tests,
    &window_tests,
    &rendering_tests,
    &target_tests,
    &parser_tokenizer_tests,
    &parser_expression_tests,
    &parser_result_tests,
    &parser_file_tests,
    &c_frontend_tests,
    &assembly_tests,
    &analysis_tests,
    &ir_tests,
    &ir_interpreter_tests,
    &codegen_tests,
    &debug_model_tests,
    &dwarf_tests,
    &codeview_tests,
    &pdb_tests,
    &object_tests,
    &link_tests,
    &compiler_driver_tests,
#if BUSTER_CPU_ARCH_X86_64
    &x86_64_tests,
#endif
};

BatchTestResult library_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {0};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_functions); i += 1)
    {
        u64 arena_position = arguments->arena->position;
        UnitTestResult unit_test_result = test_functions[i](arguments);
        consume_unit_tests(&result, unit_test_result);
        arena_set_position(arguments->arena, arena_position);
    }

    return result;
}
#endif
