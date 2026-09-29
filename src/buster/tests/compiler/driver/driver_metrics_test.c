// Included by driver_test.c. Per-input metrics and -fkeep-going for batched
// invocations (CompilerDriverInputResult, compiler_driver_metrics_format).
// compiler_driver_test_input_metrics runs multi-input -c batches inside a
// private temporary working directory (they write `<input>.o` there): record
// contents, setup outside input 0, ordered intervals, scratch-inclusive arena
// peaks, diagnostic digests, the unchanged default path, continue-on-failure,
// caps and truncation, and the `ide cc -fmetrics-out` process record.
// compiler_driver_test_input_metrics_lanes checks input order and link
// suppression on the serial and -fcompile-jobs link paths.

BUSTER_GLOBAL_LOCAL String8 compiler_driver_metrics_test_join(Arena* arena, String8 directory, String8 name)
{
    return string_format_z(arena, S8("{S8}/{S8}"), directory, name);
}

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

BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_contains(String8 text, String8 needle)
{
    return string_first_sequence(text, needle) != BUSTER_STRING_NO_MATCH;
}

BUSTER_GLOBAL_LOCAL bool compiler_driver_metrics_test_same_bytes(ByteSlice a, ByteSlice b)
{
    return a.pointer && b.pointer && a.length && a.length == b.length && memory_compare(a.pointer, b.pointer, a.length);
}

BUSTER_GLOBAL_LOCAL CompilerDriverResult compiler_driver_metrics_test_run(Arena* arena, String8* command, u64 count, CompilerDriverInvocation* parsed)
{
    CompilerDriverInvocation invocation = compiler_driver_parse_arguments(arena, (SliceString8){.pointer = command, .length = count});
    if (parsed)
    {
        *parsed = invocation;
    }
    return compiler_driver_execute_invocation(arena, invocation);
}

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
    String8 names[] = {S8("twin_first"), S8("twin_second"), S8("globals"), S8("rejected"), S8("third")};
    String8 sources[] = {
        S8("int twin(int value) { return value * 7 + 1; }\n"),
        S8("int twin(int value) { return value * 7 + 1; }\n"),
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
        CompilerDriverResult measured = compiler_driver_execute_invocation(arena, good);
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
                BUSTER_TEST(arguments, input->function_count == input->codegen.function_count && input->functions_omitted == 0);
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
            BUSTER_TEST(arguments, globals->function_count == 2 && string_equal(globals->functions[0].name, S8("metrics_first")) &&
                                       string_equal(globals->functions[1].name, S8("metrics_second")));
            BUSTER_TEST(arguments, globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_DATA] != 0 &&
                                       globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_READ_ONLY_DATA] != 0 &&
                                       globals->section_bytes[COMPILER_DRIVER_SECTION_CLASS_ZERO] == 100 * sizeof(int));
            String8 records = compiler_driver_metrics_format(arena, &good, &measured, (CompilerDriverProcessMetrics){.wall_nanoseconds = wall});
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" compilation_workers=1 intervals=serial ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_FUNCTION version=1 input=")) == 4);
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
        String8 failing_default[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), paths[2], paths[3], paths[4]};
        String8 failing_measured[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), metrics_option, paths[2], paths[3], paths[4]};
        (void)os_file_delete(objects[4]);
        CompilerDriverResult stopped = compiler_driver_metrics_test_run(arena, failing_default, BUSTER_ARRAY_LENGTH(failing_default), 0);
        BUSTER_TEST(arguments, stopped.error == COMPILER_DRIVER_ERROR_ANALYSIS && !stopped.inputs);
        BUSTER_TEST(arguments, !file_read(arena, objects[4], (FileReadOptions){0}).pointer);
        CompilerDriverResult stopped_measured = compiler_driver_metrics_test_run(arena, failing_measured, BUSTER_ARRAY_LENGTH(failing_measured), 0);
        BUSTER_TEST(arguments, stopped_measured.error == stopped.error && string_equal(stopped_measured.diagnostic, stopped.diagnostic));
        BUSTER_TEST(arguments, stopped_measured.diagnostic_count == stopped.diagnostic_count && string_equal(stopped_measured.warning, stopped.warning));
        BUSTER_TEST(arguments, !file_read(arena, objects[4], (FileReadOptions){0}).pointer);
        if (BUSTER_REQUIRE(arguments, stopped_measured.inputs && stopped_measured.input_result_count == 3))
        {
            BUSTER_TEST(arguments, stopped_measured.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            BUSTER_TEST(arguments, stopped_measured.inputs[1].status == COMPILER_DRIVER_INPUT_STATUS_REJECTED);
            BUSTER_TEST(arguments, stopped_measured.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_NOT_RUN && !stopped_measured.inputs[2].measured);
        }

        // -fkeep-going: the later input still compiles; the digest is stable
        // across runs and follows the diagnostic's content.
        String8 keep_going[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), S8("-fkeep-going"), metrics_option,
                                S8("-fmetrics-functions"), paths[2], paths[3], paths[4]};
        CompilerDriverInvocation continued_invocation = {0};
        CompilerDriverResult continued = compiler_driver_metrics_test_run(arena, keep_going, BUSTER_ARRAY_LENGTH(keep_going), &continued_invocation);
        BUSTER_TEST(arguments, continued.error == COMPILER_DRIVER_ERROR_ANALYSIS && string_equal(continued.diagnostic, stopped.diagnostic));
        ByteSlice third_object = file_read(arena, objects[4], (FileReadOptions){0});
        BUSTER_TEST(arguments, third_object.length != 0);
        CompilerDriverResult repeated = compiler_driver_execute_invocation(arena, continued_invocation);
        if (BUSTER_REQUIRE(arguments, continued.input_result_count == 3 && repeated.input_result_count == 3))
        {
            CompilerDriverInputResult const* rejected = &continued.inputs[1];
            BUSTER_TEST(arguments, continued.failed_input_count == 1);
            BUSTER_TEST(arguments, continued.inputs[0].status == COMPILER_DRIVER_INPUT_STATUS_OK && continued.inputs[2].status == COMPILER_DRIVER_INPUT_STATUS_OK);
            BUSTER_TEST(arguments, rejected->status == COMPILER_DRIVER_INPUT_STATUS_REJECTED && rejected->error == COMPILER_DRIVER_ERROR_ANALYSIS);
            BUSTER_TEST(arguments, rejected->error_count == 1 && rejected->warning_count == 1 && rejected->diagnostic_line == 2);
            BUSTER_TEST(arguments, rejected->diagnostic_record_count == 2 && string_equal(rejected->diagnostic_path, paths[3]));
            BUSTER_TEST(arguments, string_equal(rejected->message, stopped.diagnostic) && rejected->object_file_bytes == 0);
            BUSTER_TEST(arguments, continued.inputs[2].object_file_bytes == third_object.length);
            BUSTER_TEST(arguments, string_equal(rejected->diagnostic_digest, repeated.inputs[1].diagnostic_digest));
            BUSTER_TEST(arguments, !string_equal(rejected->diagnostic_digest, continued.inputs[0].diagnostic_digest));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_intervals(&continued, UINT64_MAX));
            String8 changed = S8("#warning metrics-warning\nint metrics_rejected(void) { return other_missing_value; }\n");
            BUSTER_TEST(arguments, file_write(paths[3], BUSTER_SLICE_TO_BYTE_SLICE(changed)));
            CompilerDriverResult altered = compiler_driver_execute_invocation(arena, continued_invocation);
            BUSTER_TEST(arguments, altered.input_result_count == 3 && altered.inputs[1].diagnostic_record_count == 2 &&
                                       !string_equal(altered.inputs[1].diagnostic_digest, rejected->diagnostic_digest));
            BUSTER_TEST(arguments, file_write(paths[3], BUSTER_SLICE_TO_BYTE_SLICE(sources[3])));

            CompilerDriverProcessMetrics process = {.wall_nanoseconds = 1, .peak_resident_bytes = 2, .exit_status = 1};
            String8 records = compiler_driver_metrics_format(arena, &continued_invocation, &continued, process);
            String8 again = compiler_driver_metrics_format(arena, &continued_invocation, &repeated, process);
            BUSTER_TEST(arguments, string_starts_with_sequence(records, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=3 records=3 ok=2 rejected=1 failed=0 ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS ")) == 1);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(records, S8("CC_METRICS_INPUT version=1 index=")) == 3);
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" status=rejected error=driver.analysis errors=1 warnings=1 measured=1 start_ns=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" diagnostic_records=2 diagnostic_digest=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(records, S8(" message_truncated=0 message_hex=")));
            BUSTER_TEST(arguments, records.length && records.pointer[records.length - 1] == '\n');
            BUSTER_TEST(arguments, string_equal(compiler_driver_metrics_test_mask(arena, records), compiler_driver_metrics_test_mask(arena, again)));
        }

        // The per-input function cap, through the private test limit.
        compiler_driver_test_set_function_limit(1);
        String8 capped_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), metrics_option, S8("-fmetrics-functions"), paths[2]};
        CompilerDriverResult capped = compiler_driver_metrics_test_run(arena, capped_command, BUSTER_ARRAY_LENGTH(capped_command), 0);
        compiler_driver_test_set_function_limit(0);
        BUSTER_TEST(arguments, capped.input_result_count == 1 && capped.inputs[0].function_count == 1 && capped.inputs[0].functions_omitted == 1 &&
                                   string_equal(capped.inputs[0].functions[0].name, S8("metrics_first")));

        // Messages and function names past the text limit are cut and say so.
        char8* long_name = arena_allocate(arena, char8, COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100);
        for (u32 index = 0; index < COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100; index += 1)
        {
            long_name[index] = (char8)('a' + index % 26);
        }
        String8 identifier = {.pointer = long_name, .length = COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100};
        String8 long_paths[] = {compiler_driver_metrics_test_join(arena, root, S8("long_name.c")), compiler_driver_metrics_test_join(arena, root, S8("long_error.c"))};
        BUSTER_TEST(arguments, file_write(long_paths[0], BUSTER_SLICE_TO_BYTE_SLICE(string_format(arena, S8("{S8}{S8}{S8}"), S8("int "), identifier, S8("(void) { return 3; }\n")))));
        BUSTER_TEST(arguments, file_write(long_paths[1], BUSTER_SLICE_TO_BYTE_SLICE(string_format(arena, S8("{S8}{S8}{S8}"), S8("int broken(void) { return "), identifier, S8("; }\n")))));
        String8 long_command[] = {S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"), S8("-fkeep-going"), metrics_option,
                                  S8("-fmetrics-functions"), long_paths[0], long_paths[1]};
        CompilerDriverInvocation long_invocation = {0};
        CompilerDriverResult long_result = compiler_driver_metrics_test_run(arena, long_command, BUSTER_ARRAY_LENGTH(long_command), &long_invocation);
        String8 long_records = compiler_driver_metrics_format(arena, &long_invocation, &long_result, (CompilerDriverProcessMetrics){0});
        BUSTER_TEST(arguments, long_result.input_result_count == 2 && long_result.inputs[1].message.length > COMPILER_DRIVER_METRICS_TEXT_LIMIT);
        BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(long_records, S8(" message_truncated=1 message_hex=")));
        BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(long_records, string_format(arena, S8(" name_bytes={u32} name_truncated=1 name_hex="),
                                                                                                   COMPILER_DRIVER_METRICS_TEXT_LIMIT + 100)));
        (void)os_file_delete(string_format_z(arena, S8("{S8}/long_name.o"), root));

        // The process: nonzero exit, one error line per failed input, and a
        // header carrying the exit status, interval order, wall time and peak.
        ProcessSpawnOptions capture = {.capture = ((u64)1 << STANDARD_STREAM_OUTPUT) | ((u64)1 << STANDARD_STREAM_ERROR),
                                       .use_process_environment = 1, .search_path = 1};
        String8 command[] = {ide, S8("cc"), S8("-target"), S8("x86_64-unknown-linux"), S8("-nostdinc"), S8("-c"),
                             S8("-fkeep-going"), metrics_option, paths[2], paths[3], paths[4]};
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
            BUSTER_TEST(arguments, string_starts_with_sequence(file, S8("CC_METRICS version=1 schema=buster-cc-metrics inputs=3 records=3 ok=2 rejected=1 ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(file, S8(" exit_status=1 action=object target=x86_64-linux ")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_contains(file, S8(" compilation_workers=1 intervals=serial keep_going=1 function_sizes=0 wall_ns=")));
            BUSTER_TEST(arguments, compiler_driver_metrics_test_count_lines(file, S8("CC_METRICS_INPUT ")) == 3);
        }
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        (void)os_file_delete(paths[index]);
        (void)os_file_delete(objects[index]);
    }
    (void)os_file_delete(compiler_driver_metrics_test_join(arena, root, S8("long_name.c")));
    (void)os_file_delete(compiler_driver_metrics_test_join(arena, root, S8("long_error.c")));
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
    (void)os_file_delete(path);
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
    }
    (void)os_file_delete(main_path);
    (void)os_file_delete(helper_path);
    (void)os_file_delete(data_path);
    (void)os_file_delete(bad_path);
    (void)os_file_delete(output);
    scratch_end(temporary);
    return result;
}
