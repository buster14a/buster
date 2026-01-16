#pragma once
#include <buster/test.h>
#include <buster/string8.h>
#include <buster/os.h>
#include <buster/arena.h>

BUSTER_IMPL bool unit_test_succeeded(UnitTestResult result)
{
    return result.succeeded_test_count == result.test_count;
}

BUSTER_IMPL void consume_unit_tests(BatchTestResult* batch, UnitTestResult unit_test)
{
    batch->succeeded_unit_test_count += unit_test.succeeded_test_count;
    batch->unit_test_count += unit_test.test_count;
    batch->succeeded_module_test_count += unit_test_succeeded(unit_test);
    batch->module_test_count += 1;
}

BUSTER_IMPL void consume_external_tests(BatchTestResult* batch, ProcessResult result)
{
    batch->succeeded_external_test_count += result == PROCESS_RESULT_SUCCESS;
    batch->external_test_count += 1;
}

BUSTER_IMPL void buster_test_error(u32 line, String8 function, String8 file_path, String8 format, ...)
{
    string8_print(S8("{S8} failed at {S8}:{S8}:{u32}\n"), format, file_path, function, line);

    if (is_debugger_present())
    {
        os_fail();
    }
}

BUSTER_GLOBAL_LOCAL TestFunction* test_functions[] = {
    &string8_tests,
};

BUSTER_IMPL BatchTestResult library_tests(UnitTestArguments* arguments)
{
    BatchTestResult result = {};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(test_functions); i += 1)
    {
        let unit_test_result = test_functions[i](arguments);
        consume_unit_tests(&result, unit_test_result);
    }

    return result;
}

BUSTER_IMPL void default_show(UnitTestArguments* arguments, String8 format, ...)
{
    let arena = arguments->arena;
    let arena_position = arena->position;
    bool null_terminate = false;
    va_list variable_arguments;

    va_start(variable_arguments);
    StringFormatResult buffer_result = string8_format_va((String8){}, format, variable_arguments);
    va_end(variable_arguments);

    let code_unit_count = buffer_result.needed_code_unit_count;
    let buffer = string8_from_pointer_length(arena_allocate(arena, char8, code_unit_count + null_terminate), code_unit_count);

    if (buffer.pointer)
    {
        va_start(variable_arguments);
        StringFormatResult final_result = string8_format_va(buffer, format, variable_arguments);
        va_end(variable_arguments);

        if (final_result.needed_code_unit_count == code_unit_count)
        {
            if (null_terminate)
            {
                buffer.pointer[code_unit_count] = 0;
            }

            string8_print(buffer);
        }
    }

    arena->position = arena_position;
}

BUSTER_IMPL bool batch_test_succeeded(BatchTestResult test)
{
    let unit_result = test.succeeded_unit_test_count == test.unit_test_count;
    let module_result = test.succeeded_module_test_count == test.module_test_count;
    let external_result = test.succeeded_external_test_count == test.external_test_count;

    let result = unit_result && module_result && external_result;
    return result;
}

BUSTER_IMPL bool batch_test_report(UnitTestArguments* arguments, BatchTestResult test)
{
    arguments->show(arguments, S8("[{u64}/{u64}] Unit tests\n"), test.succeeded_unit_test_count, test.unit_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] Module tests\n"), test.succeeded_module_test_count, test.module_test_count);
    arguments->show(arguments, S8("[{u64}/{u64}] External tests\n"), test.succeeded_external_test_count, test.external_test_count);
    return batch_test_succeeded(test);
}
