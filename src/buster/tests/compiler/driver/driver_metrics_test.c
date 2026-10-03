// Included by driver_test.c. Per-input metrics and -fkeep-going for batched
// invocations (CompilerDriverInputResult, compiler_driver_metrics_format).
// compiler_driver_test_input_metrics runs multi-input -c batches inside a
// private temporary working directory (they write `<input>.o` there): record
// contents, setup outside input 0, ordered intervals, scratch-inclusive arena
// peaks, diagnostic digests, the unchanged default path, continue-on-failure,
// caps and truncation, and the `ide cc -fmetrics-out` process record.
// compiler_driver_test_input_metrics_lanes checks input order and link
// suppression on the serial and -fcompile-jobs link paths.

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL String8 compiler_driver_metrics_test_join(Arena* arena, String8 directory, String8 name)
{
    return string_format_z(arena, S8("{S8}/{S8}"), directory, name);
}
#endif

// The records with every field that legitimately differs between runs
// removed: timings and interval offsets, the process peak, the worker
// request/outcome, and the arena peak (alignment padding follows the
// scratch arenas' start positions, so it can cross a granule between runs).
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
                              string_equal(key, S8("compilation_workers")) || string_equal(key, S8("intervals")) ||
                              string_equal(key, S8("arena_peak_bytes"));
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

#if !BUSTER_ANDROID && !BUSTER_IOS
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
#endif

BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_contains(String8 text, String8 needle)
{
    return string_first_sequence(text, needle) != BUSTER_STRING_NO_MATCH;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_same_bytes(ByteSlice a, ByteSlice b)
{
    return a.pointer && b.pointer && a.length && a.length == b.length && memory_compare(a.pointer, b.pointer, a.length);
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_metrics_test_run(Arena* arena, String8* command, u64 count, CompilerDriverInvocation* parsed)
{
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
    if (parsed)
    {
        *parsed = invocation;
    }
    return compiler_driver_execute_invocation(arena, invocation);
}
#endif

// Serial records: each interval closed, ordered after the previous one,
// partitioned exactly by its phases, and inside [0, wall].
BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_intervals(CompilerDriverResult const* result, u64 wall)
{
    bool ordered = result->input_result_count != 0;
    u64 previous_end = 0;
    for (u32 index = 0; index < result->input_result_count && ordered; index += 1)
    {
        CompilerDriverInputResult const* input = &result->inputs[index];
        u64 phases = 0;
        for (u32 phase = 0; phase < COMPILER_DRIVER_PHASE_COUNT; phase += 1)
        {
            phases += input->phase_nanoseconds[phase];
        }
        ordered = !input->measured ||
                  (input->start_nanoseconds < input->end_nanoseconds && input->start_nanoseconds >= previous_end && input->end_nanoseconds <= wall &&
                   phases == input->end_nanoseconds - input->start_nanoseconds);
        previous_end = input->measured ? input->end_nanoseconds : previous_end;
    }
    return ordered;
}

#if !BUSTER_ANDROID && !BUSTER_IOS
BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_change_directory(String8 path)
{
#if BUSTER_WINDOWS
    TemporalArena scratch = scratch_begin(0, 0);
    String16 wide_path = string16_from_string8(scratch.arena, path, true);
    bool result = SetCurrentDirectoryW(wide_path.pointer) != 0;
    scratch_end(scratch);
    return result;
#else
    return chdir((const char*)path.pointer) == 0;
#endif
}

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_metrics_test_batches(UnitTestArguments* arguments, Arena* arena, String8 root, String8 ide)
{
    UnitTestResult result = {0};
    // Inputs 0 and 1 are identical sources under different names, so input
    // 0 can be compared with a later copy of the same work.
    String8 names[] = {S8("twin_first"), S8("twin_second"), S8("globals"), S8("rejected"), S8("third"), S8("long_name"), S8("long_error")};
    // Messages and function names past the text limit are cut and say so.
    char8* long_name = arena_allocate(arena, char8, COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100);
    for (u32 index = 0; index < COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100; index += 1)
    {
        long_name[index] = (char8)('a' + index % 26);
    }
    String8 identifier = {.pointer = long_name, .length = COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100};
    String8 sources[] = {
        S8("int twin(int value) { return value * 7 + 1; }\n"),
        S8("int twin(int value) { return value * 7 + 1; }\n"),
        S8("int metrics_data = 7;\nconst char* metrics_text = \"rodata\";\nstatic int metrics_zero[100];\n"
           "int metrics_first(int value) { return value + metrics_data + metrics_zero[value & 63]; }\n"
           "int metrics_second(void) { return metrics_first(2) + (int)metrics_text[0]; }\n"),
        S8("#warning metrics-warning\nint metrics_rejected(void) { return missing_metrics_value; }\n"),
        S8("int metrics_third(int value) { return value * 3; }\n"),
        string_format(arena, S8("{S8}{S8}{S8}"), S8("int "), identifier, S8("(void) { return 3; }\n")),
        string_format(arena, S8("{S8}{S8}{S8}"), S8("int broken(void) { return "), identifier, S8("; }\n")),
    };
    String8 paths[BUSTER_ARRAY_LENGTH(names)];
    String8 objects[BUSTER_ARRAY_LENGTH(names)];
    bool written = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        paths[index] = compiler_driver_metrics_test_join(arena, root, string_format(arena, S8("{S8}.c"), names[index]));
        objects[index] = compiler_driver_metrics_test_join(arena, root, string_format(arena, S8("{S8}.o"), names[index]));
        written = file_write(paths[index], BUSTER_SLICE_TO_BYTE_SLICE(sources[index])) && written;
    }
    String8 metrics_path = compiler_driver_metrics_test_join(arena, root, S8("records.txt"));
    String8 metrics_option = string_format(arena, S8("-fmetrics-out={S8}"), metrics_path);
    if (BUSTER_REQUIRE(arguments, written))
    {
        // A clean batch: intervals, setup outside input 0, sections, arena
        // peaks and function sizes against the written objects.
        String8 good_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), metrics_option, S8("-fmetrics-functions"),
                                  paths[0], paths[1], paths[2]};
        CompilerDriverInvocation good = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(good_command));
        good.metrics_origin = timestamp_take();
        good.has_metrics_origin = true;
        // The per-input function cap, lowered through the private test seam
        // so the two-function input reaches it.
        compiler_driver_test_set_function_limit(1);
        CompilerDriverResult measured = compiler_driver_execute_invocation(arena, good);
        compiler_driver_test_set_function_limit(0);
        u64 wall = timestamp_ns_between(good.metrics_origin, timestamp_take());
        BUSTER_TEST_RAW(arguments, measured.error == COMPILER_DRIVER_ERROR_NONE, measured.diagnostic);
        ByteSlice measured_objects[3];
        for (u32 index = 0; index < 3; index += 1)
        {
            measured_objects[index] = file_read(arena, objects[index], (FileReadOptions){0});
        }
        if (BUSTER_REQUIRE(arguments, measured.inputs && measured.input_result_count == 3 && measured.failed_input_count == 0))
        {
            BUSTER_TEST(arguments, compiler_driver_metrics_test_intervals(&measured, wall));
            CompilerDriverInputResult const* first = &measured.inputs[0];
            CompilerDriverInputResult const* twin = &measured.inputs[1];
            // Table preparation and first-use setup used to add ~20 ms to
            // input 0's codegen phase (600x its twin's). Setup now happens
            // before any interval opens; the bound only allows cache warmth.
            u64 first_total = first->end_nanoseconds - first->start_nanoseconds;
            u64 twin_total = twin->end_nanoseconds - twin->start_nanoseconds;
            BUSTER_TEST(arguments, first->phase_nanoseconds[COMPILER_DRIVER_PHASE_CODEGEN] <= 8 * twin->phase_nanoseconds[COMPILER_DRIVER_PHASE_CODEGEN]);
            BUSTER_TEST(arguments, first_total <= 8 * twin_total);
            for (u32 index = 0; index < 3; index += 1)
            {
                CompilerDriverInputResult const* input = &measured.inputs[index];
                BUSTER_TEST(arguments, input->index == index && string_equal(input->path, paths[index]));
                BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_OK && input->measured && input->error_count == 0);
                BUSTER_TEST(arguments, input->phase_nanoseconds[COMPILER_DRIVER_PHASE_CODEGEN] && input->phase_nanoseconds[COMPILER_DRIVER_PHASE_EMIT]);
                // Code generation uses the thread's scratch arenas, so the
                // peak carries at least one scratch granule beyond the TU
                // arena's own rounded use.
                BUSTER_TEST(arguments, input->arena_retained_bytes != 0 && input->arena_peak_bytes % BUSTER_KB(64) == 0 &&
                                           input->arena_peak_bytes >= align_forward(input->arena_retained_bytes, BUSTER_KB(64)) + BUSTER_KB(64));
                BUSTER_TEST(arguments, input->object_file_bytes == measured_objects[index].length && input->object_file_bytes != 0);
                BUSTER_TEST(arguments, input->section_bytes[COMPILER_DRIVER_SECTION_CLASS_TEXT] == input->codegen.code_bytes && input->codegen.code_bytes);
                BUSTER_TEST(arguments, input->source_bytes == sources[index].length && input->preprocessed_tokens != 0);
                BUSTER_TEST(arguments, input->function_count == 1 && input->function_count + input->functions_omitted == input->codegen.function_count);
                BUSTER_TEST(arguments, input->diagnostic_record_count == 0 && input->diagnostic_digest.length == 64);
                ObjectFile object = object_read(arena, measured_objects[index], good.target);
                if (BUSTER_REQUIRE(arguments, object.error == OBJECT_ERROR_NONE))
                {
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
                    }
                }
            }
            CompilerDriverInputResult const* globals = &measured.inputs[2];
            BUSTER_TEST(arguments, globals->functions_omitted == 1 && string_equal(globals->functions[0].name, S8("metrics_first")));
            BUSTER_TEST(arguments, globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_DATA] != 0 &&
                                       globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA] != 0 &&
                                       globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_ZERO] == 100 * sizeof(int));
            String8 records = compiler_driver_metrics_format(arena, &good, &measured, (CompilerDriverProcessMetrics){.wall_nanoseconds = wall});
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" compilation_workers=1 intervals=serial ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_FUNCTION version=1 input=")) == 3);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" function_records=1 function_records_omitted=1 ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" name_bytes=13 name_truncated=0 name_hex=6d6574726963735f6669727374\n")));
        }

        // Without the options: no records and byte-identical objects.
        String8 plain_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), paths[0], paths[1], paths[2]};
        CompilerDriverResult plain = compiler_driver_metrics_test_run(arena, plain_command, BUSTER_ARRAY_LENGTH(plain_command), 0);
        BUSTER_TEST_RAW(arguments, plain.error == COMPILER_DRIVER_ERROR_NONE, plain.diagnostic);
        BUSTER_TEST(arguments, !plain.inputs && plain.input_result_count == 0 && plain.failed_input_count == 0);
        for (u32 index = 0; index < 3; index += 1)
        {
            BUSTER_TEST(arguments, compiler_driver_metrics_test_same_bytes(file_read(arena, objects[index], (FileReadOptions){0}), measured_objects[index]));
        }

        // A rejected input in the middle, by default and with metrics only:
        // the first failure still stops the batch and nothing after it runs.
        String8 failing_default[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), paths[3], paths[4]};
        String8 failing_measured[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), metrics_option, S8("-fmetrics-functions"),
                                      paths[2], paths[3], paths[4]};
        (void)os_file_delete(objects[4]);
        CompilerDriverResult stopped = compiler_driver_metrics_test_run(arena, failing_default, BUSTER_ARRAY_LENGTH(failing_default), 0);
        BUSTER_TEST(arguments, stopped.error == COMPILER_DRIVER_ERROR_ANALYSIS && !stopped.inputs);
        BUSTER_TEST(arguments, !file_read(arena, objects[4], (FileReadOptions){0}).pointer);
        CompilerDriverInvocation stopped_invocation = {0};
        CompilerDriverResult stopped_measured = compiler_driver_metrics_test_run(arena, failing_measured, BUSTER_ARRAY_LENGTH(failing_measured), &stopped_invocation);
        BUSTER_TEST(arguments, stopped_measured.error == stopped.error && string_equal(stopped_measured.diagnostic, stopped.diagnostic));
        BUSTER_TEST(arguments, string_equal(stopped_measured.warning, stopped.warning));
        BUSTER_TEST(arguments, !file_read(arena, objects[4], (FileReadOptions){0}).pointer);
        bool stopped_recorded = stopped_measured.input_result_count == 3;
        if (BUSTER_REQUIRE(arguments, stopped_recorded))
        {
            BUSTER_TEST(arguments, stopped_measured.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            BUSTER_TEST(arguments, stopped_measured.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED);
            BUSTER_TEST(arguments, stopped_measured.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_NOT_RUN && !stopped_measured.inputs[2].measured);
        }

        // -fkeep-going over the same inputs plus the two long ones: later
        // inputs still compile, and the inputs both runs compiled give the
        // same records (stable digest) once timings are removed.
        String8 keep_going[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), S8("-fkeep-going"), metrics_option,
                                S8("-fmetrics-functions"), paths[2], paths[3], paths[4], paths[5], paths[6]};
        CompilerDriverInvocation continued_invocation = {0};
        CompilerDriverResult continued = compiler_driver_metrics_test_run(arena, keep_going, BUSTER_ARRAY_LENGTH(keep_going), &continued_invocation);
        BUSTER_TEST(arguments, continued.error == COMPILER_DRIVER_ERROR_ANALYSIS && string_equal(continued.diagnostic, stopped.diagnostic));
        ByteSlice third_object = file_read(arena, objects[4], (FileReadOptions){0});
        BUSTER_TEST(arguments, third_object.length != 0);
        if (BUSTER_REQUIRE(arguments, stopped_recorded && continued.input_result_count == 5))
        {
            CompilerDriverInputResult const* rejected = &continued.inputs[1];
            BUSTER_TEST(arguments, continued.failed_input_count == 2);
            BUSTER_TEST(arguments, continued.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK && continued.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_OK &&
                                       continued.inputs[3].status == COMPILER_DRIVER_INPUT_STATUS_OK &&
                                       continued.inputs[4].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED);
            BUSTER_TEST(arguments, rejected->status == COMPILER_DRIVER_INPUT_STATUS_REJECTED && rejected->error == COMPILER_DRIVER_ERROR_ANALYSIS);
            BUSTER_TEST(arguments, rejected->error_count == 1 && rejected->warning_count == 1 && rejected->diagnostic_line == 2);
            BUSTER_TEST(arguments, rejected->diagnostic_record_count == 2 && string_equal(rejected->diagnostic_path, paths[3]));
            BUSTER_TEST(arguments, string_equal(rejected->message, stopped.diagnostic) && rejected->object_file_bytes == 0);
            BUSTER_TEST(arguments, continued.inputs[2].object_file_bytes == third_object.length);
            BUSTER_TEST(arguments, string_equal(rejected->diagnostic_digest, stopped_measured.inputs[1].diagnostic_digest));
            BUSTER_TEST(arguments, !string_equal(rejected->diagnostic_digest, continued.inputs[0].diagnostic_digest) &&
                                       !string_equal(rejected->diagnostic_digest, continued.inputs[4].diagnostic_digest));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_intervals(&continued, UINT64_MAX));
            BUSTER_TEST(arguments, continued.inputs[4].message.length > COMPILER_DRIVER_METRICS_TEXT_LIMIT);

            CompilerDriverProcessMetrics process = {.wall_nanoseconds = 1, .peak_resident_bytes = 2, .exit_status = 1};
            String8 records = compiler_driver_metrics_format(arena, &continued_invocation, &continued, process);
            String8 earlier = compiler_driver_metrics_format(arena, &stopped_invocation, &stopped_measured, process);
            BUSTER_TEST(arguments, string_starts_with_sequence(records, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=5 records=5 ok=3 rejected=2 failed=0 ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS ")) == 1);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_INPUT version=1 index=")) == 5);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" status=rejected error=driver.analysis errors=1 warnings=1 measured=1 start_ns=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" diagnostic_records=2 diagnostic_digest=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" message_truncated=0 message_hex=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" message_truncated=1 message_hex=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, string_format(arena, S8(" name_bytes={u32} name_truncated=1 name_hex="),
                                                                                               COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100)));
            BUSTER_TEST(arguments, records.length && records.pointer[records.length - 1] == '\n');
            // Header and input 2 differ between the two runs by design; the
            // first two inputs and the first input's functions must not.
            String8 masked = compiler_driver_metrics_test_mask(arena, records);
            String8 earlier_masked = compiler_driver_metrics_test_mask(arena, earlier);
            u64 masked_start = string_first_sequence(masked, S8("CC_METRICS_INPUT version=1 index=0 "));
            u64 masked_end = string_first_sequence(masked, S8("CC_METRICS_INPUT version=1 index=2 "));
            u64 earlier_start = string_first_sequence(earlier_masked, S8("CC_METRICS_INPUT version=1 index=0 "));
            u64 earlier_end = string_first_sequence(earlier_masked, S8("CC_METRICS_INPUT version=1 index=2 "));
            BUSTER_TEST(arguments, masked_start < masked_end && masked_end != BUSTER_STRING_NO_MATCH && earlier_start < earlier_end &&
                                       earlier_end != BUSTER_STRING_NO_MATCH &&
                                       string_equal(string_slice(masked, masked_start, masked_end), string_slice(earlier_masked, earlier_start, earlier_end)));

            // A changed diagnostic changes the digest (the two batches above
            // already showed the unchanged one is stable across runs).
            String8 changed = S8("#warning metrics-warning\nint metrics_rejected(void) { return other_missing_value; }\n");
            BUSTER_TEST(arguments, file_write(paths[3], BUSTER_SLICE_TO_BYTE_SLICE(changed)));
            String8 altered_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fsyntax-only"), metrics_option, paths[3]};
            CompilerDriverResult altered = compiler_driver_metrics_test_run(arena, altered_command, BUSTER_ARRAY_LENGTH(altered_command), 0);
            BUSTER_TEST(arguments, altered.input_result_count == 1 && altered.inputs[0].diagnostic_record_count == 2 &&
                                       !string_equal(altered.inputs[0].diagnostic_digest, rejected->diagnostic_digest));
            BUSTER_TEST(arguments, file_write(paths[3], BUSTER_SLICE_TO_BYTE_SLICE(sources[3])));
        }

        // The one child process: nonzero exit, one error line per failed
        // input, and a header carrying exit status, interval order and wall.
        ProcessSpawnOptions capture = {.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                       .use_process_environment = 1, .search_path = 1};
        String8 command[] = {ide, S8("cc"), S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"),
                             S8("-fkeep-going"), metrics_option, paths[3], paths[4]};
        (void)os_file_delete(metrics_path);
        ProcessSpawnResult spawned = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), (SliceString8){0}, (SliceString8){0}, capture);
        if (BUSTER_REQUIRE(arguments, spawned.handle != 0))
        {
            ProcessWaitResult waited = os_process_wait_deadline(arena, spawned, 30000000);
            String8 error = BYTE_SLICE_TO_STRING(8, waited.streams[STANDARD_STREAM_ERROR]);
            BUSTER_TEST(arguments, !waited.timed_out && waited.result != PROCESS_RESULT_SUCCESS);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(error, S8("cc: error:")) &&
                                       compiler_driver_metrics_test_contains(error, S8("missing_metrics_value")));
            String8 file = BYTE_SLICE_TO_STRING(8, file_read(arena, metrics_path, (FileReadOptions){0}));
            BUSTER_TEST(arguments, string_starts_with_sequence(file, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=2 records=2 ok=1 rejected=1 ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(file, S8(" exit_status=1 action=object target=x86_64-linux ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(file, S8(" compilation_workers=1 intervals=serial keep_going=1 function_sizes=0 wall_ns=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(file, S8("CC_METRICS_INPUT ")) == 2);
        }
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        (void)os_file_delete(paths[index]);
        (void)os_file_delete(objects[index]);
    }
    (void)os_file_delete(metrics_path);
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL UnitTestResult compiler_driver_test_input_metrics(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;
    String8 path = buster_test_temporary_path(arena, S8("buster-metrics-parse"), S8(".c"));
    String8 metrics_option = S8("-fmetrics-out=records.txt");
    String8 parse_command[] = {metrics_option, S8("-fkeep-going"), S8("-fno-keep-going"), S8("-fmetrics-functions"), path};
    CompilerDriverInvocation parsed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(parse_command));
    BUSTER_TEST(arguments, parsed.error == COMPILER_DRIVER_ERROR_NONE && parsed.collect_input_metrics && parsed.collect_function_sizes &&
                               !parsed.keep_going && string_equal(parsed.metrics_output_path, S8("records.txt")));
    String8 functions_only[] = {S8("-fmetrics-functions"), path};
    CompilerDriverInvocation lone = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(functions_only));
    BUSTER_TEST_RAW(arguments, lone.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_metrics_test_contains(lone.diagnostic, S8("-fmetrics-out")),
                    lone.diagnostic);
    String8 gpu_command[] = {S8("-target"), S8("amdgcn-amd-amdhsa"), S8("--gpu-arch=gfx1201"), S8("-x"), S8("hip"), S8("-fkeep-going"), path};
    CompilerDriverInvocation gpu = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(gpu_command));
    BUSTER_TEST_RAW(arguments, gpu.error == COMPILER_DRIVER_ERROR_ARGUMENT && compiler_driver_metrics_test_contains(gpu.diagnostic, S8("-fkeep-going")),
                    gpu.diagnostic);
    // The digest needs every structured record, so metrics refuse the API's
    // record suppression instead of publishing an empty digest.
    BUSTER_TEST(arguments, file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int suppressed(void) { return 1; }\n"))));
    String8 suppressed_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fsyntax-only"), metrics_option, path};
    CompilerDriverInvocation suppressed = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(suppressed_command));
    suppressed.suppress_diagnostic_records = true;
    BUSTER_TEST(arguments, compiler_driver_execute_invocation(arena, suppressed).error == COMPILER_DRIVER_ERROR_ARGUMENT);
    CompilerDriverInvocation functions = suppressed;
    functions.collect_input_metrics = false;
    functions.collect_function_sizes = true;
    functions.suppress_diagnostic_records = false;
    CompilerDriverResult functions_refused = compiler_driver_execute_invocation(arena, functions);
    BUSTER_TEST(arguments, functions_refused.error == COMPILER_DRIVER_ERROR_ARGUMENT &&
                               string_equal(functions_refused.diagnostic, S8("function sizes require per-input metrics")));
    // Native count limits still refuse before reading an input when its
    // metrics records are requested; no interval or partial result opens.
    for (u32 count_kind = 0; count_kind < 3; count_kind += 1)
    {
        CompilerDriverInvocation limits = suppressed;
        limits.suppress_diagnostic_records = false;
        limits.library_count = count_kind == 0 ? UINT32_MAX : 0;
        limits.framework_count = count_kind == 1 ? UINT32_MAX : 0;
        limits.library_path_count = count_kind == 2 ? UINT32_MAX : 0;
        CompilerDriverResult refused = compiler_driver_execute_invocation(arena, limits);
        BUSTER_TEST(arguments, refused.error == COMPILER_DRIVER_ERROR_ARGUMENT &&
                                   string_equal(refused.diagnostic, S8("native library counts exceed driver limits")));
        if (BUSTER_REQUIRE(arguments, refused.input_result_count == 1))
        {
            BUSTER_TEST(arguments, refused.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_NOT_RUN && !refused.inputs[0].measured &&
                                       !refused.failed_input_count && !refused.output.length);
        }
    }
    // Metrics retain the checked publication refusal and count no object
    // bytes when an existing directory cannot become the output file.
    String8 refused_output = buster_test_temporary_path(arena, S8("buster-metrics-refused-output"), S8(".o"));
    if (BUSTER_REQUIRE(arguments, os_make_directory_attempt(refused_output)))
    {
        String8 command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-g0"),
                             S8("-c"), S8("-o"), refused_output, path};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        CompilerDriverResult plain = compiler_driver_execute_invocation(arena, invocation);
        invocation.collect_input_metrics = true;
        CompilerDriverResult measured = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, plain.error == COMPILER_DRIVER_ERROR_FILE_WRITE, plain.diagnostic);
        BUSTER_TEST(arguments, measured.error == plain.error && string_equal(measured.diagnostic, plain.diagnostic));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(measured.diagnostic, refused_output) &&
                                   compiler_driver_metrics_test_contains(measured.diagnostic, S8("directories and non-stream special destinations are refused")));
        if (BUSTER_REQUIRE(arguments, measured.input_result_count == 1 && measured.failed_input_count == 1))
        {
            CompilerDriverInputResult* input = &measured.inputs[0];
            BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_FAILED && input->error == COMPILER_DRIVER_ERROR_FILE_WRITE &&
                                       input->measured && input->object_file_bytes == 0 && input->codegen.code_bytes != 0);
            BUSTER_TEST(arguments, string_equal(input->message, measured.diagnostic));
        }
        BUSTER_TEST(arguments, os_directory_delete(refused_output));
    }
    (void)os_file_delete(path);

    // Both assembly spellings hand -E text to the emit phase. Measuring
    // them preserves the text and partitions the complete input interval.
    String8 extensions[] = {S8(".s"), S8(".S")};
    String8 assembly_sources[] = {S8(".text\n.globl metrics_entry\nmetrics_entry: ret\n"),
                                 S8("#define ENTRY metrics_entry\n.text\n.globl ENTRY\nENTRY: ret\n")};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(extensions); index += 1)
    {
        String8 assembly = buster_test_temporary_path(arena, S8("buster-metrics-preprocess"), extensions[index]);
        String8 output = buster_test_temporary_path(arena, S8("buster-metrics-preprocess"), S8(".txt"));
        if (BUSTER_REQUIRE(arguments, file_write(assembly, BUSTER_SLICE_TO_BYTE_SLICE(assembly_sources[index]))))
        {
            String8 command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-E"), S8("-o"), output, assembly};
            CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
            CompilerDriverResult plain = compiler_driver_execute_invocation(arena, invocation);
            BUSTER_TEST_RAW(arguments, plain.error == COMPILER_DRIVER_ERROR_NONE && plain.output.length, plain.diagnostic);
            invocation.collect_input_metrics = true;
            CompilerDriverResult measured = compiler_driver_execute_invocation(arena, invocation);
            BUSTER_TEST_RAW(arguments, measured.error == COMPILER_DRIVER_ERROR_NONE && string_equal(measured.output, plain.output), measured.diagnostic);
            if (BUSTER_REQUIRE(arguments, measured.input_result_count == 1))
            {
                CompilerDriverInputResult* input = &measured.inputs[0];
                BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_OK && input->measured);
                BUSTER_TEST(arguments, input->phase_nanoseconds[COMPILER_DRIVER_PHASE_EMIT] != 0);
                BUSTER_TEST(arguments, compiler_driver_metrics_test_intervals(&measured, UINT64_MAX));
                ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, bytes.length == plain.output.length && memory_compare(bytes.pointer, plain.output.pointer, bytes.length));
            }
        }
        (void)os_file_delete(assembly);
        (void)os_file_delete(output);
    }
#if !BUSTER_ANDROID && !BUSTER_IOS
    // Multi-input -c writes each object beside the working directory, so the
    // batches run inside a private directory and restore the original one.
    String8 root = buster_test_temporary_path(arena, S8("buster-metrics-batch"), S8(""));
    os_make_directory(root);
    String8 root_absolute = os_path_absolute(arena, root, true);
    String8 original = os_path_absolute(arena, S8("."), true);
    // The child `ide cc` is named before the working directory changes.
    String8 ide = os_path_absolute(arena, program_state->input.arguments.pointer[0], true);
    bool entered = root_absolute.length && original.length && ide.length && compiler_driver_metrics_test_change_directory(root_absolute);
    if (BUSTER_REQUIRE(arguments, entered))
    {
        UnitTestResult batches = compiler_driver_metrics_test_batches(arguments, arena, root_absolute, ide);
        result.test_count += batches.test_count;
        result.succeeded_test_count += batches.succeeded_test_count;
        BUSTER_TEST(arguments, compiler_driver_metrics_test_change_directory(original));
    }
    (void)os_directory_delete(root_absolute);
#endif
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
    String8 bad_path = buster_test_temporary_path(arena, S8("buster-metrics-lane-bad"), S8(".c"));
    String8 output = buster_test_temporary_path(arena, S8("buster-metrics-lane"), S8(".out"));
    bool written = file_write(main_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int lane_helper(int value);\nint lane_data(void);\n"
                                                                         "int main(void) { return lane_helper(0) + lane_data() - 5; }\n"))) &&
                   file_write(helper_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int lane_helper(int value) { return value * 3; }\n"))) &&
                   file_write(data_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("static int lane_zero[8];\nint lane_value = 5;\n"
                                                                         "int lane_data(void) { return lane_value + lane_zero[1]; }\n"))) &&
                   file_write(bad_path, BUSTER_SLICE_TO_BYTE_SLICE(S8("int lane_bad(void) { return lane_missing; }\n")));
    u32 workers = (u32)buster_test_worker_count(BUSTER_MIN((u64)2, (u64)BUSTER_MAX(os_get_logical_thread_count(), (u32)1)));
    workers = BUSTER_MAX(workers, (u32)1);
    if (BUSTER_REQUIRE(arguments, written))
    {
        String8 command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fmetrics-out=unused.txt"), S8("-fmetrics-functions"),
                             S8("-o"), output, main_path, helper_path, data_path};
        CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        invocation.compile_jobs = 1;
        CompilerDriverResult serial = compiler_driver_execute_invocation(arena, invocation);
        BUSTER_TEST_RAW(arguments, serial.error == COMPILER_DRIVER_ERROR_NONE, serial.diagnostic);
        BUSTER_TEST(arguments, compiler_driver_metrics_test_intervals(&serial, UINT64_MAX));
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
                BUSTER_TEST(arguments, input->status == COMPILER_DRIVER_INPUT_STATUS_OK && input->measured && input->end_nanoseconds > input->start_nanoseconds);
                BUSTER_TEST(arguments, input->object_file_bytes == 0 && input->section_bytes[COMPILER_DRIVER_SECTION_CLASS_TEXT] != 0);
                BUSTER_TEST(arguments, input->function_count == 1 && input->arena_peak_bytes != 0);
            }
            BUSTER_TEST(arguments, string_equal(parallel.inputs[0].functions[0].name, S8("main")) &&
                                       string_equal(parallel.inputs[2].functions[0].name, S8("lane_data")));
            BUSTER_TEST(arguments, parallel.inputs[2].section_bytes[COMPILER_DRIVER_SECTION_CLASS_ZERO] == 8 * sizeof(int));
            String8 serial_records = compiler_driver_metrics_format(arena, &invocation, &serial, (CompilerDriverProcessMetrics){0});
            String8 parallel_records = compiler_driver_metrics_format(arena, &invocation, &parallel, (CompilerDriverProcessMetrics){0});
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(parallel_records, workers > 1 ? S8(" intervals=concurrent ") : S8(" intervals=serial ")));
            BUSTER_TEST(arguments, string_equal(compiler_driver_metrics_test_mask(arena, serial_records),
                                                compiler_driver_metrics_test_mask(arena, parallel_records)));
        }

        // -fkeep-going never links: the old output survives on both paths
        // while every other input still compiles and is recorded.
        String8 keep_going[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fkeep-going"), S8("-o"), output,
                                main_path, bad_path, helper_path};
        CompilerDriverInvocation failing = compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(keep_going));
        String8 sentinel = S8("no image may be linked after a failed unit");
        u32 jobs[] = {1, workers};
        for (u32 attempt = 0; attempt < BUSTER_ARRAY_LENGTH(jobs); attempt += 1)
        {
            BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
            failing.compile_jobs = jobs[attempt];
            CompilerDriverResult suppressed = compiler_driver_execute_invocation(arena, failing);
            BUSTER_TEST(arguments, suppressed.error == COMPILER_DRIVER_ERROR_ANALYSIS && suppressed.failed_input_count == 1);
            BUSTER_TEST(arguments, suppressed.input_result_count == 3 && suppressed.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK &&
                                       suppressed.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED &&
                                       suppressed.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
            BUSTER_TEST(arguments, bytes.length == sentinel.length && memory_compare(bytes.pointer, sentinel.pointer, bytes.length));
        }

        // An unreadable prebuilt input after a failed unit is its own failed
        // record and keeps the first failure as the invocation's error.
        String8 missing_object = buster_test_temporary_path(arena, S8("buster-metrics-lane-missing"), S8(".o"));
        String8 prebuilt[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fkeep-going"), S8("-o"), output,
                              bad_path, missing_object, helper_path};
        CompilerDriverResult unreadable = compiler_driver_execute_invocation(arena,
            compiler_driver_parse_arguments(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(prebuilt)));
        BUSTER_TEST(arguments, unreadable.error == COMPILER_DRIVER_ERROR_ANALYSIS && unreadable.failed_input_count == 2);
        BUSTER_TEST(arguments, unreadable.input_result_count == 3 && unreadable.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_FAILED &&
                                   unreadable.inputs[1].error == COMPILER_DRIVER_ERROR_FILE_READ &&
                                   compiler_driver_metrics_test_contains(unreadable.inputs[1].message, S8("could not read")));

        // A valid index defers selected member decoding until after the
        // source inputs. A corrupt selected member is a failed input, and
        // its failure cannot replace an earlier -fkeep-going rejection.
        Target target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX};
        ObjectSymbol symbol = {.name = S8("lane_helper"), .section = OBJECT_SECTION_DATA, .global = true};
        ObjectFile member = compiler_driver_archive_test_object(arena, target, &symbol, 1, 1);
        ByteSlice archive_bytes = compiler_driver_archive_test_bytes(arena, &member, 1, 1);
        ObjectArchive archive = object_archive_read_link(arena, archive_bytes, target);
        String8 archive_path = buster_test_temporary_path(arena, S8("buster-metrics-lane-corrupt"), S8(".a"));
        if (BUSTER_REQUIRE(arguments, archive.error == OBJECT_ERROR_NONE && archive.object_count == 1 && archive.member_bytes[0].length >= 4))
        {
            memcpy(archive.member_bytes[0].pointer, "NOPE", 4);
            BUSTER_TEST(arguments, file_write(archive_path, archive_bytes));
            for (u32 prior_error = 0; prior_error < 2; prior_error += 1)
            {
                String8 archive_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-fkeep-going"),
                    S8("-fmetrics-out=unused.txt"), S8("-o"), output, main_path, prior_error ? bad_path : archive_path,
                    prior_error ? archive_path : data_path, data_path};
                CompilerDriverInvocation archive_invocation = compiler_driver_parse_arguments(arena,
                    (SliceString8){.pointer = archive_command, .length = BUSTER_ARRAY_LENGTH(archive_command) - (prior_error == 0)});
                BUSTER_TEST(arguments, file_write(output, BUSTER_SLICE_TO_BYTE_SLICE(sentinel)));
                CompilerDriverResult rejected = compiler_driver_execute_invocation(arena, archive_invocation);
                BUSTER_TEST(arguments, rejected.error == (prior_error ? COMPILER_DRIVER_ERROR_ANALYSIS : COMPILER_DRIVER_ERROR_OBJECT) &&
                                           rejected.failed_input_count == 1 + prior_error);
                if (BUSTER_REQUIRE(arguments, rejected.input_result_count == 3 + prior_error))
                {
                    CompilerDriverInputResult* failed = &rejected.inputs[1 + prior_error];
                    BUSTER_TEST(arguments, failed->status == COMPILER_DRIVER_INPUT_STATUS_FAILED && failed->error == COMPILER_DRIVER_ERROR_OBJECT);
                    BUSTER_TEST(arguments, string_equal(failed->diagnostic_code, S8("driver.object")) &&
                                               compiler_driver_metrics_test_contains(failed->message, S8("could not read archive")));
                    BUSTER_TEST(arguments, rejected.inputs[2 + prior_error].status == COMPILER_DRIVER_INPUT_STATUS_NOT_RUN);
                    if (prior_error)
                    {
                        BUSTER_TEST(arguments, rejected.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED &&
                                                   string_equal(rejected.diagnostic, rejected.inputs[1].message));
                    }
                    String8 records = compiler_driver_metrics_format(arena, &archive_invocation, &rejected, (CompilerDriverProcessMetrics){0});
                    BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, prior_error ? S8(" rejected=1 failed=1 not_run=1 prebuilt=0 error=driver.analysis ") :
                                                                                                                     S8(" rejected=0 failed=1 not_run=1 prebuilt=0 error=driver.object ")));
                }
                ByteSlice bytes = file_read(arena, output, (FileReadOptions){0});
                BUSTER_TEST(arguments, bytes.length == sentinel.length && memory_compare(bytes.pointer, sentinel.pointer, bytes.length));
            }
        }
        (void)os_file_delete(archive_path);
    }
    (void)os_file_delete(main_path);
    (void)os_file_delete(helper_path);
    (void)os_file_delete(data_path);
    (void)os_file_delete(bad_path);
    (void)os_file_delete(output);
    scratch_end(temporary);
    return result;
}
