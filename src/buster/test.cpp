#pragma once
#include <buster/test.h>

#include <buster/os.h>
#include <buster/arena.h>
#include <buster/string.h>

BUSTER_F_IMPL bool unit_test_succeeded(UnitTestResult result)
{
    return result.succeeded_test_count == result.test_count;
}

BUSTER_F_IMPL void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test)
{
    batch->succeeded_unit_test_count += unit_test.succeeded_test_count;
    batch->unit_test_count += unit_test.test_count;
    batch->succeeded_module_test_count += unit_test_succeeded(unit_test);
    batch->module_test_count += 1;
}

BUSTER_F_IMPL void consume_external_tests(BatchTestResult* batch, ProcessResult result)
{
    batch->succeeded_external_test_count += result == ProcessResult::Success;
    batch->external_test_count += 1;
}

BUSTER_F_IMPL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    let scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let message = string8_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    string8_print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), message, file_path, function, line);
    scratch_end(scratch);

    if (is_debugger_present())
    {
        os_fail();
    }
}

BUSTER_F_IMPL void default_show(UnitTestArguments* arguments, String8 format, ...)
{
    BUSTER_UNUSED(arguments);
    let scratch = scratch_begin(0, 0);
    va_list variable_arguments;
    va_start(variable_arguments, format);
    let string = string8_format_va(scratch.arena, format, variable_arguments);
    va_end(variable_arguments);

    if (string.length)
    {
        os_file_write(os_get_stdout(), BUSTER_SLICE_TO_BYTE_SLICE(string));
    }

    scratch_end(scratch);
}

BUSTER_F_IMPL bool batch_test_succeeded(BatchTestResult test)
{
    let unit_result = test.succeeded_unit_test_count == test.unit_test_count;
    let module_result = test.succeeded_module_test_count == test.module_test_count;
    let external_result = test.succeeded_external_test_count == test.external_test_count;

    let result = unit_result && module_result && external_result;
    return result;
}

BUSTER_F_IMPL bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test)
{
    arguments->show(arguments, S8("[{u64}/{u64}] Unit tests\n"), test.succeeded_unit_test_count, test.unit_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] Module tests\n"), test.succeeded_module_test_count, test.module_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] External tests\n"), test.succeeded_external_test_count, test.external_test_count);
    return batch_test_succeeded(test);
}

#if BUSTER_INCLUDE_TESTS
BUSTER_GLOBAL_LOCAL TestFunction* test_functions[] = {
    &string_tests,
};

BUSTER_F_IMPL BatchTestResult library_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_functions); i += 1)
    {
        let unit_test_result = test_functions[i](arguments);
        consume_unit_tests(&result, unit_test_result);
    }

    return result;
}
#endif
