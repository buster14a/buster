// Included by driver_test.c. Per-input metrics and -fkeep-going for batched
// invocations (CompilerDriverInputResult, compiler_driver_metrics_format):
// compiler_driver_test_input_metrics covers a multi-input -c batch, the
// unchanged default path, continue-on-failure, record determinism and the
// `ide cc -fmetrics-out` process record; compiler_driver_test_input_metrics_lanes
// checks input-ordered records on the -fcompile-jobs link path.

// Where a multi-input -c invocation writes `input`'s object: its basename
// with the extension replaced, in the working directory.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_metrics_test_object_path(Arena* arena, String8 input)
{
    u64 name = 0;
    u64 extension = input.length;
    for (u64 index = 0; index < input.length; index += 1)
    {
        char8 byte = input.pointer[index];
        if (byte == '/' || byte == '\\')
        {
            name = index + 1;
            extension = input.length;
        }
        else if (byte == '.')
        {
            extension = index;
        }
    }
    return string_format_z(arena, S8("{S8}.o"), (String8){.pointer = input.pointer + name, .length = extension - name});
}

// The records with every field that legitimately differs between runs
// removed: timings, the process peak and the worker request/outcome.
BUSTER_GLOBAL_LOCAL String8 compiler_driver_metrics_test_mask(Arena* arena, String8 text)
{
    char8* output = arena_allocate(arena, char8, text.length + 1);
    u64 length = 0;
    u64 start = 0;
    while (start < text.length)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != ' ' && text.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 token = {.pointer = text.pointer + start, .length = end - start};
        u64 equal = string_first_code_unit(token, '=');
        String8 key = equal == BUSTER_STRING_NO_MATCH ? token : string_slice(token, 0, equal);
        bool volatile_field = (key.length > 3 && string_equal(string_slice(key, key.length - 3, key.length), S8("_ns"))) ||
                              string_equal(key, S8("peak_rss_bytes")) || string_equal(key, S8("compile_jobs")) ||
                              string_equal(key, S8("compilation_workers"));
        if (!volatile_field && token.length)
        {
            memcpy(output + length, token.pointer, token.length);
            length += token.length;
        }
        if (end < text.length)
        {
            output[length] = text.pointer[end];
            length += 1;
        }
        start = end + 1;
    }
    return (String8){.pointer = output, .length = length};
}

BUSTER_GLOBAL_LOCAL u64 compiler_driver_metrics_test_count_lines(String8 text, String8 prefix)
{
    u64 result = 0;
    u64 start = 0;
    while (start < text.length)
    {
        u64 end = start;
        while (end < text.length && text.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = {.pointer = text.pointer + start, .length = end - start};
        result += (u64)string_starts_with_sequence(line, prefix);
        start = end + 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_same_bytes(ByteSlice a, ByteSlice b)
{
    return a.pointer && b.pointer && a.length && a.length == b.length && memory_compare(a.pointer, b.pointer, a.length);
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_input_metrics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 names[] = {S8("buster-metrics-first"), S8("buster-metrics-rejected"), S8("buster-metrics-third")};
    String8 sources[] = {
        S8("int metrics_data = 7;\nconst char* metrics_text = \"rodata\";\nstatic int metrics_zero[100];\n"
           "int metrics_first(int value) { return value + metrics_data + metrics_zero[value & 63]; }\n"
           "int metrics_second(void) { return metrics_first(2) + (int)metrics_text[0]; }\n"),
        S8("#warning metrics-warning\nint metrics_rejected(void) { return missing_metrics_value; }\n"),
        S8("int metrics_third(int value) { return value * 3; }\n"),
    };
    String8 paths[BUSTER_ARRAY_LENGTH(names)];
    String8 objects[BUSTER_ARRAY_LENGTH(names)];
    bool written = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        paths[index] = buster_test_temporary_path(arena, names[index], S8(".c"));
        objects[index] = compiler_driver_metrics_test_object_path(arena, paths[index]);
        written = file_write(paths[index], BUSTER_SLICE_TO_BYTE_SLICE(sources[index])) && written;
        (void)os_file_delete(objects[index]);
    }
    String8 metrics_path = buster_test_temporary_path(arena, S8("buster-metrics-records"), S8(".txt"));
    String8 metrics_option = string_format(arena, S8("-fmetrics-out={S8}"), metrics_path);

    String8 parse_command[] = {metrics_option, S8("-fkeep-going"), S8("-fno-keep-going"), S8("-fmetrics-functions"), paths[0]};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parse_command));
    BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.collect_input_metrics && parsed.collect_function_sizes &&
                               !parsed.keep_going && string_equal(parsed.metrics_output_path, metrics_path));
    String8 gpu_command[] = {S8("-target"), S8("amdgcn-amd-amdhsa"), S8("--gpu-arch=gfx1201"), S8("-x"), S8("hip"), S8("-fkeep-going"), paths[0]};
    CompilerDriverInvocation gpu = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(gpu_command));
    BUSTER_TEST_RAW(arguments, gpu.error == COMPILER_DRIVER_ERROR_ARGUMENT &&
                                   string_first_sequence(gpu.diagnostic, S8("-fkeep-going")) != BUSTER_STRING_NO_MATCH, gpu.diagnostic);

    if (BUSTER_REQUIRE(arguments, written))
    {
        Target target = {0};
        // A clean batch: every record, section and function size.
        String8 good_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), S8("-fmetrics-functions"), paths[0], paths[2]};
        CompilerDriverInvocation good = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(good_command));
        target = good.target;
        CompilerDriverResult measured = compiler_driver_execute_invocation(arena, good);
        BUSTER_TEST_RAW(arguments, measured.error == COMPILER_DRIVER_ERROR_NONE, measured.diagnostic);
        ByteSlice measured_first = file_read(arena, objects[0], (FileReadOptions){0});
        ByteSlice measured_third = file_read(arena, objects[2], (FileReadOptions){0});
        if (BUSTER_REQUIRE(arguments, measured.inputs && measured.input_result_count == 2 && measured.failed_input_count == 0))
        {
            ByteSlice bytes[] = {measured_first, measured_third};
            for (u32 index = 0; index < 2; index += 1)
            {
                CompilerDriverInputResult const* input = &measured.inputs[index];
                BUSTER_TEST(arguments, input->index == index && string_equal(input->path, index ? paths[2] : paths[0]));
                BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_OK && input->measured && input->error_count == 0);
                u64 phases = 0;
                for (u32 phase = 0; phase < COMPILER_DRIVER_PHASE_COUNT; phase += 1)
                {
                    phases += input->phase_nanoseconds[phase];
                }
                BUSTER_TEST(arguments, input->nanoseconds && phases == input->nanoseconds);
                BUSTER_TEST(arguments, input->phase_nanoseconds[COMPILER_DRIVER_PHASE_CODEGEN] && input->phase_nanoseconds[COMPILER_DRIVER_PHASE_EMIT]);
                BUSTER_TEST(arguments, input->arena_peak_bytes >= input->arena_retained_bytes && input->arena_retained_bytes != 0);
                BUSTER_TEST(arguments, input->arena_reserved_bytes == COMPILER_DRIVER_C_TRANSLATION_UNIT_RESERVED_SIZE);
                BUSTER_TEST(arguments, input->object_file_bytes == bytes[index].length && input->object_file_bytes != 0);
                BUSTER_TEST(arguments, input->section_bytes[COMPILER_DRIVER_SECTION_CLASS_TEXT] == input->codegen.code_bytes &&
                                           input->codegen.code_bytes != 0);
                BUSTER_TEST(arguments, input->source_bytes == sources[index ? 2 : 0].length && input->preprocessed_tokens != 0);
                BUSTER_TEST(arguments, input->function_count == input->codegen.function_count && input->functions_omitted == 0);
                ObjectFile object = object_read(arena, bytes[index], target);
                if (BUSTER_REQUIRE(arguments, object.error == OBJECT_ERROR_NONE))
                {
                    u64 function_bytes = 0;
                    for (u32 function = 0; function < input->function_count; function += 1)
                    {
                        CompilerDriverFunctionSize size = input->functions[function];
                        bool matched = false;
                        for (u32 symbol = 0; symbol < object.symbol_count && !matched; symbol += 1)
                        {
                            matched = object.symbols[symbol].kind == OBJECT_SYMBOL_FUNCTION && string_equal(object.symbols[symbol].name, size.name) &&
                                      object.symbols[symbol].size == size.code_bytes;
                        }
                        BUSTER_TEST_RAW(arguments, matched && size.code_bytes != 0, size.name);
                        function_bytes += size.code_bytes;
                    }
                    BUSTER_TEST(arguments, function_bytes <= input->codegen.code_bytes);
                }
            }
            CompilerDriverInputResult const* first = &measured.inputs[0];
            BUSTER_TEST(arguments, first->function_count == 2 && string_equal(first->functions[0].name, S8("metrics_first")) &&
                                       string_equal(first->functions[1].name, S8("metrics_second")));
            BUSTER_TEST(arguments, first->section_bytes[COMPILER_DRIVER_SECTION_CLASS_DATA] != 0 &&
                                       first->section_bytes[COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA] != 0 &&
                                       first->section_bytes[COMPILER_DRIVER_SECTION_CLASS_ZERO] == 100 * sizeof(int));
        }

        // Without the options: no records and byte-identical objects.
        String8 plain_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), paths[0], paths[2]};
        CompilerDriverResult plain = compiler_driver_execute_invocation(arena,
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(plain_command)));
        BUSTER_TEST_RAW(arguments, plain.error == COMPILER_DRIVER_ERROR_NONE, plain.diagnostic);
        BUSTER_TEST(arguments, !plain.inputs && plain.input_result_count == 0 && plain.failed_input_count == 0);
        ByteSlice plain_first = file_read(arena, objects[0], (FileReadOptions){0});
        ByteSlice plain_third = file_read(arena, objects[2], (FileReadOptions){0});
        BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(plain_first, measured_first));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(plain_third, measured_third));

        // A rejected input in the middle, by default and with metrics only:
        // the first failure still stops the batch and nothing after it runs.
        String8 failing_default[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), paths[0], paths[1], paths[2]};
        String8 failing_measured[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), metrics_option, paths[0], paths[1], paths[2]};
        (void)os_file_delete(objects[2]);
        CompilerDriverResult stopped = compiler_driver_execute_invocation(arena,
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(failing_default)));
        BUSTER_TEST(arguments, stopped.error == COMPILER_DRIVER_ERROR_ANALYSIS && !stopped.inputs);
        BUSTER_TEST(arguments, !file_read(arena, objects[2], (FileReadOptions){0}).pointer);
        CompilerDriverResult stopped_measured = compiler_driver_execute_invocation(arena,
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(failing_measured)));
        BUSTER_TEST(arguments, stopped_measured.error == stopped.error && string_equal(stopped_measured.diagnostic, stopped.diagnostic));
        BUSTER_TEST(arguments, stopped_measured.diagnostic_count == stopped.diagnostic_count && string_equal(stopped_measured.warning, stopped.warning));
        BUSTER_TEST(arguments, !file_read(arena, objects[2], (FileReadOptions){0}).pointer);
        if (BUSTER_REQUIRE(arguments, stopped_measured.inputs && stopped_measured.input_result_count == 3))
        {
            BUSTER_TEST(arguments, stopped_measured.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            BUSTER_TEST(arguments, stopped_measured.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED);
            BUSTER_TEST(arguments, stopped_measured.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_NOT_RUN && !stopped_measured.inputs[2].measured);
            BUSTER_TEST(arguments, stopped_measured.failed_input_count == 1);
        }

        // -fkeep-going: the later input still compiles to the same object.
        String8 keep_going[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), S8("-fkeep-going"), S8("-fmetrics-functions"),
                                paths[0], paths[1], paths[2]};
        CompilerDriverInvocation continued_invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(keep_going));
        CompilerDriverResult continued = compiler_driver_execute_invocation(arena, continued_invocation);
        BUSTER_TEST(arguments, continued.error == COMPILER_DRIVER_ERROR_ANALYSIS && string_equal(continued.diagnostic, stopped.diagnostic));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(file_read(arena, objects[0], (FileReadOptions){0}), plain_first));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(file_read(arena, objects[2], (FileReadOptions){0}), plain_third));
        if (BUSTER_REQUIRE(arguments, continued.inputs && continued.input_result_count == 3))
        {
            CompilerDriverInputResult const* rejected = &continued.inputs[1];
            BUSTER_TEST(arguments, continued.failed_input_count == 1);
            BUSTER_TEST(arguments, continued.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK && continued.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            BUSTER_TEST(arguments, rejected->status == COMPILER_DRIVER_INPUT_STATUS_REJECTED && rejected->error == COMPILER_DRIVER_ERROR_ANALYSIS);
            BUSTER_TEST(arguments, rejected->error_count == 1 && rejected->warning_count == 1 && rejected->diagnostic_line == 2);
            BUSTER_TEST(arguments, string_equal(rejected->message, stopped.diagnostic) && string_equal(rejected->diagnostic_path, paths[1]));
            BUSTER_TEST(arguments, rejected->object_file_bytes == 0 && rejected->function_count == 0);
            BUSTER_TEST(arguments, continued.inputs[2].object_file_bytes == plain_third.length);
        }

        // The record text: fixed schema, one line per input and function,
        // identical across runs once timings are removed.
        CompilerDriverResult repeated = compiler_driver_execute_invocation(arena, continued_invocation);
        CompilerDriverProcessMetrics process = {.wall_nanoseconds = 1, .peak_resident_bytes = 2, .exit_status = 1};
        String8 records = compiler_driver_metrics_format(arena, &continued_invocation, &continued, process);
        String8 again = compiler_driver_metrics_format(arena, &continued_invocation, &repeated, process);
        BUSTER_TEST(arguments, string_starts_with_sequence(records, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=3 records=3 ok=2 rejected=1 failed=0 ")));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS ")) == 1);
        BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_INPUT version=1 index=")) == 3);
        BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_FUNCTION version=1 input=")) == 3);
        BUSTER_TEST(arguments, string_first_sequence(records, S8(" status=rejected error=driver.analysis errors=1 warnings=1 ")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, string_first_sequence(records, S8(" name_bytes=13 name_hex=6d6574726963735f6669727374\n")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, string_first_sequence(records, S8(" exit_status=1 ")) != BUSTER_STRING_NO_MATCH);
        BUSTER_TEST(arguments, records.length && records.pointer[records.length - 1] == '\n');
        BUSTER_TEST(arguments, string_equal(compiler_driver_metrics_test_mask(arena, records), compiler_driver_metrics_test_mask(arena, again)));

#if !BUSTER_ANDROID && !BUSTER_IOS
        // The process: nonzero exit, one error line per failed input, and a
        // header record carrying the exit status, wall time and peak RSS.
        ProcessSpawnOptions capture = {.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                       .use_process_environment = 1, .search_path = 1};
        String8 command[] = {program_state->input.arguments.pointer[0], S8("cc"), S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"),
                             S8("-fkeep-going"), metrics_option, paths[0], paths[1], paths[2]};
        (void)os_file_delete(metrics_path);
        ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0}, capture);
        if (BUSTER_REQUIRE(arguments, spawned.handle != 0))
        {
            ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
            String8 error = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]);
            BUSTER_TEST(arguments, !waited.timed_out && waited.result != PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, string_first_sequence(error, S8("cc: error:")) != BUSTER_STRING_NO_MATCH &&
                                       string_first_sequence(error, S8("missing_metrics_value")) != BUSTER_STRING_NO_MATCH);
            String8 file = BYTE_SLICE_TO_STRING(8, file_read(arena, metrics_path, (FileReadOptions){0}));
            BUSTER_TEST(arguments, string_starts_with_sequence(file, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=3 records=3 ok=2 rejected=1 ")));
            BUSTER_TEST(arguments, string_first_sequence(file, S8(" exit_status=1 action=object target=x86_64-linux ")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, string_first_sequence(file, S8(" compilation_workers=1 keep_going=1 function_sizes=0 wall_ns=")) != BUSTER_STRING_NO_MATCH);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(file, S8("CC_METRICS_INPUT ")) == 3);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(file_read(arena, objects[2], (FileReadOptions){0}), plain_third));
        }
#endif
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        (void)os_file_delete(paths[index]);
        (void)os_file_delete(objects[index]);
    }
    (void)os_file_delete(metrics_path);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_input_metrics_lanes(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 main_path = buster_test_temporary_path(arena, S8("buster-metrics-lane-main"), S8(".c"));
    String8 helper_path = buster_test_temporary_path(arena, S8("buster-metrics-lane-helper"), S8(".c"));
    String8 data_path = buster_test_temporary_path(arena, S8("buster-metrics-lane-data"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-metrics-lane"), S8(".out"));
    bool written = file_write(main_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int lane_helper(int value);\nint lane_data(void);\n"
                                                                         "int main(void) { return lane_helper(0) + lane_data() - 5; }\n"))) &&
                   file_write(helper_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int lane_helper(int value) { return value * 3; }\n"))) &&
                   file_write(data_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("static int lane_zero[8];\nint lane_value = 5;\n"
                                                                         "int lane_data(void) { return lane_value + lane_zero[1]; }\n")));
    u32 workers = (u32)buster_test_worker_count(BUSTER_MIN((u64)2, (u64)BUSTER_MAX(os_get_logical_thread_count(), (u32)1)));
    workers = BUSTER_MAX(workers, (u32)1);
    if (BUSTER_REQUIRE(arguments, written))
    {
        String8 command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fmetrics-functions"), S8("-o"), output,
                             main_path, helper_path, data_path};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        invocation.compile_jobs = 1;
        CompilerDriverResult serial = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, serial.error == COMPILER_DRIVER_ERROR_NONE, serial.diagnostic);
        ByteSlice serial_image = file_read(arena, output, (FileReadOptions){0});
        invocation.compile_jobs = workers;
        CompilerDriverResult parallel = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, parallel.error == COMPILER_DRIVER_ERROR_NONE, parallel.diagnostic);
        BUSTER_TEST(arguments, parallel.compilation_workers == workers);
        BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(file_read(arena, output, (FileReadOptions){0}), serial_image));
        String8 expected[] = {main_path, helper_path, data_path};
        if (BUSTER_REQUIRE(arguments, parallel.input_result_count == 3 && serial.input_result_count == 3))
        {
            for (u32 index = 0; index < 3; index += 1)
            {
                CompilerDriverInputResult const* input = &parallel.inputs[index];
                BUSTER_TEST(arguments, input->index == index && string_equal(input->path, expected[index]));
                BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_OK && input->measured && input->nanoseconds != 0);
                BUSTER_TEST(arguments, input->object_file_bytes == 0 && input->section_bytes[COMPILER_DRIVER_SECTION_CLASS_TEXT] != 0);
                BUSTER_TEST(arguments, input->function_count == 1 && input->arena_peak_bytes == serial.inputs[index].arena_peak_bytes);
            }
            BUSTER_TEST(arguments, string_equal(parallel.inputs[0].functions[0].name, S8("main")) &&
                                       string_equal(parallel.inputs[2].functions[0].name, S8("lane_data")));
            BUSTER_TEST(arguments, parallel.inputs[2].section_bytes[COMPILER_DRIVER_SECTION_CLASS_ZERO] == 8 * sizeof(int));
            CompilerDriverProcessMetrics process = {0};
            String8 serial_records = compiler_driver_metrics_format(arena, &invocation, &serial, process);
            String8 parallel_records = compiler_driver_metrics_format(arena, &invocation, &parallel, process);
            BUSTER_TEST(arguments, string_equal(compiler_driver_metrics_test_mask(arena, serial_records),
                                                compiler_driver_metrics_test_mask(arena, parallel_records)));
        }
    }
    (void)os_file_delete(main_path);
    (void)os_file_delete(helper_path);
    (void)os_file_delete(data_path);
    (void)os_file_delete(output);
    scratch_end(temporary);
    return result;
}
