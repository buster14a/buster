// Isolated CI unit-module processes, included only by the native build driver.
// ci_unit_tests_main owns child admission, quotas, bounded capture and cleanup.
// ci_unit_parse validates canonical inventories, exact selected timing rows and
// terminal counts before ci_unit_pair merges stable driver/rest results. A
// serial bootstrap or a quota below four retains the ordinary full invocation.
// Each group needs at least two workers to retain OS multi-lane assertions.
#define CI_UNIT_MAX_MODULES 512
#define CI_UNIT_CAPTURE_LIMIT BUSTER_MB(64)
#define CI_UNIT_TIMEOUT_US (5400ull * 1000000ull)

typedef struct CiUnitModule CiUnitModule;
struct CiUnitModule
{
    String8 name;
    bool audit, enabled, selected, driver, timed;
    u64 assertions;
};
typedef struct CiUnitProof CiUnitProof;
struct CiUnitProof
{
    CiUnitModule modules[CI_UNIT_MAX_MODULES];
    u64 count, selected, assertions, external;
    bool valid;
};
typedef struct CiUnitChild CiUnitChild;
struct CiUnitChild
{
    Arena* arena;
    ProcessSpawnResult spawn;
    ProcessWaitResult wait;
    CiUnitProof proof;
    u64 start, finish;
    bool clean;
};
typedef struct CiUnitWork CiUnitWork;
struct CiUnitWork
{
    CiUnitChild children[2];
    u64 timeout;
    bool audits;
};

BUSTER_GLOBAL_LOCAL bool ci_unit_take(String8* text, String8 prefix)
{
    bool result = string_starts_with_sequence(*text, prefix);
    if (result) { text->pointer += prefix.length; text->length -= prefix.length; }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_number(String8* text, String8 prefix, u64* value)
{
    bool result = ci_unit_take(text, prefix);
    if (result)
    {
        IntegerParsingU64 parsed = string8_parse_u64_decimal(*text);
        result = parsed.status == INTEGER_PARSING_SUCCESS && parsed.length != 0;
        if (result)
        {
            *value = parsed.value;
            text->pointer += parsed.length;
            text->length -= parsed.length;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_word(String8* text, String8 prefix, String8* value)
{
    bool result = ci_unit_take(text, prefix);
    if (result)
    {
        u64 length = 0;
        while (length < text->length && text->pointer[length] != ' ') { length += 1; }
        *value = (String8){text->pointer, length};
        text->pointer += length; text->length -= length;
        result = length != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_token(String8 name)
{
    bool result = name.length != 0;
    for (u64 i = 0; result && i < name.length; i += 1)
    {
        u8 c = name.pointer[i];
        result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_clean(ProcessSpawnResult spawn, ProcessWaitResult wait)
{
    bool result = spawn.handle && spawn.failure == PROCESS_SPAWN_FAILURE_NONE && wait.result == PROCESS_RESULT_SUCCESS &&
        !wait.platform_status && !wait.timed_out && !wait.termination_requested && !wait.forcibly_terminated &&
        !wait.capture_limit_exceeded && !wait.output_truncated && !wait.capture_failed && !wait.process_tree_cleanup_failed &&
        !wait.process_group_reservation_retained && !wait.process_group_ownership_lost && !wait.dropped_total;
    return result;
}

BUSTER_GLOBAL_LOCAL CiUnitProof ci_unit_parse(String8 output, bool driver, bool audits)
{
    CiUnitProof proof = {.valid = true};
    bool batch_seen = false;
    bool terminal_seen[3] = {0};
    u64 terminal_count[3] = {0};
    u64 batch_modules = 0, batch_assertions = 0, batch_external = 0;
    u64 driver_count = 0;
    u64 cursor = 0;
    while (proof.valid && cursor < output.length)
    {
        u64 begin = cursor;
        while (cursor < output.length && output.pointer[cursor] != '\n') { cursor += 1; }
        String8 line = {output.pointer + begin, cursor - begin};
        cursor += cursor < output.length;
        if (line.length && line.pointer[line.length - 1] == '\r') { line.length -= 1; }
        String8 text = line;
        if (string_starts_with_sequence(line, S8("CI_UNIT_MODULE_V1")))
        {
            u64 index = 0, audit = 0, enabled = 0, selected = 0;
            String8 name = {0}, owner = {0};
            proof.valid = !batch_seen && !terminal_seen[0] && proof.count < CI_UNIT_MAX_MODULES &&
                ci_unit_number(&text, S8("CI_UNIT_MODULE_V1 index="), &index) &&
                ci_unit_word(&text, S8(" module="), &name) && ci_unit_number(&text, S8(" table_audit="), &audit) &&
                ci_unit_number(&text, S8(" enabled="), &enabled) && ci_unit_number(&text, S8(" selected="), &selected) &&
                ci_unit_word(&text, S8(" group="), &owner) && !text.length && index == proof.count && audit <= 1 && enabled <= 1 && selected <= 1 && ci_unit_token(name);
            bool owned = string_equal(name, S8("compiler_driver_tests"));
            proof.valid = proof.valid && string_equal(owner, owned ? S8("driver") : S8("rest")) &&
                enabled == (u64)(!audit || audits) && selected == (u64)(enabled && owned == driver);
            for (u64 previous = 0; proof.valid && previous < proof.count; previous += 1)
            {
                proof.valid = !string_equal(proof.modules[previous].name, name);
            }
            if (proof.valid)
            {
                proof.modules[proof.count++] = (CiUnitModule){.name = name, .audit = audit != 0, .enabled = enabled != 0,
                    .selected = selected != 0, .driver = owned};
                proof.selected += selected;
                driver_count += owned;
            }
        }
        else if (string_starts_with_sequence(line, S8("TEST_MODULE_TIMING")))
        {
            u64 index = 0, duration = 0, passed = 0, failed = 0, assertions = 0;
            String8 name = {0}, status = {0};
            proof.valid = !batch_seen && !terminal_seen[0] &&
                ci_unit_number(&text, S8("TEST_MODULE_TIMING index="), &index) && ci_unit_word(&text, S8(" module="), &name) &&
                ci_unit_number(&text, S8(" duration_ns="), &duration) && ci_unit_number(&text, S8(" passed="), &passed) &&
                ci_unit_number(&text, S8(" failed="), &failed) && ci_unit_number(&text, S8(" assertions="), &assertions) &&
                ci_unit_word(&text, S8(" status="), &status) && !text.length && index < proof.count && !failed && passed == assertions && string_equal(status, S8("pass"));
            if (proof.valid)
            {
                CiUnitModule* module = proof.modules + index;
                proof.valid = module->selected && !module->timed && string_equal(module->name, name) && UINT64_MAX - proof.assertions >= assertions;
                if (proof.valid) { module->timed = true; module->assertions = assertions; proof.assertions += assertions; }
            }
        }
        else if (string_starts_with_sequence(line, S8("CI_UNIT_BATCH_V1")))
        {
            String8 group = {0}, status = {0};
            u64 modules_passed = 0, passed = 0, failed = 0, external_passed = 0;
            proof.valid = !batch_seen && ci_unit_word(&text, S8("CI_UNIT_BATCH_V1 group="), &group) &&
                ci_unit_number(&text, S8(" modules="), &batch_modules) && ci_unit_number(&text, S8(" modules_passed="), &modules_passed) &&
                ci_unit_number(&text, S8(" assertions="), &batch_assertions) && ci_unit_number(&text, S8(" passed="), &passed) &&
                ci_unit_number(&text, S8(" failed="), &failed) && ci_unit_number(&text, S8(" external="), &batch_external) &&
                ci_unit_number(&text, S8(" external_passed="), &external_passed) && ci_unit_word(&text, S8(" status="), &status) && !text.length &&
                string_equal(group, driver ? S8("driver") : S8("rest")) && string_equal(status, S8("pass")) && !failed &&
                modules_passed == batch_modules && passed == batch_assertions && external_passed == batch_external;
            batch_seen = true;
        }
        else if (string_starts_with_sequence(line, S8("CI_UNIT_"))) { proof.valid = false; }
        else if (line.length && line.pointer[0] == '[')
        {
            // Fixture messages also use brackets. Only terminal test summaries
            // enter this grammar; malformed variants of those summaries fail.
            bool summary = string_first_sequence(line, S8("Unit tests")) != BUSTER_STRING_NO_MATCH ||
                string_first_sequence(line, S8("Module tests")) != BUSTER_STRING_NO_MATCH ||
                string_first_sequence(line, S8("External tests")) != BUSTER_STRING_NO_MATCH;
            if (summary)
            {
                u64 passed = 0, total = 0, selected = 0, registered = 0;
                proof.valid = ci_unit_number(&text, S8("["), &passed) && ci_unit_number(&text, S8("/"), &total) && ci_unit_take(&text, S8("] ")) && passed == total;
                u32 kind = 3;
                if (proof.valid && ci_unit_take(&text, S8("Unit tests"))) kind = 0;
                else if (proof.valid && ci_unit_take(&text, S8("Module tests"))) kind = 1;
                else if (proof.valid && ci_unit_take(&text, S8("External tests"))) kind = 2;
                proof.valid = proof.valid && kind < 3;
                if (proof.valid && kind == 0)
                {
                    proof.valid = ci_unit_number(&text, S8(" ("), &selected) && ci_unit_number(&text, S8(" of "), &registered) &&
                        ci_unit_take(&text, S8(" modules selected)")) && selected == proof.selected && registered == proof.count;
                }
                proof.valid = proof.valid && !text.length && !terminal_seen[kind];
                if (proof.valid) { terminal_seen[kind] = true; terminal_count[kind] = total; }
            }
        }
    }
    for (u64 index = 0; proof.valid && index < proof.count; index += 1)
    {
        proof.valid = proof.modules[index].selected == proof.modules[index].timed;
    }
    proof.valid = proof.valid && proof.count > 1 && driver_count == 1 && proof.selected && batch_seen &&
        terminal_seen[0] && terminal_seen[1] && terminal_seen[2] && batch_modules == proof.selected &&
        batch_assertions == proof.assertions && terminal_count[0] == proof.assertions && terminal_count[1] == proof.selected && terminal_count[2] == batch_external;
    proof.external = batch_external;
    return proof;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_pair(CiUnitProof* driver, CiUnitProof* rest)
{
    bool result = driver->valid && rest->valid && driver->count == rest->count;
    for (u64 i = 0; result && i < driver->count; i += 1)
    {
        CiUnitModule* a = driver->modules + i;
        CiUnitModule* b = rest->modules + i;
        result = string_equal(a->name, b->name) && a->audit == b->audit && a->enabled == b->enabled && a->driver == b->driver &&
            (u64)a->selected + (u64)b->selected == (u64)a->enabled;
    }
    result = result && UINT64_MAX - driver->assertions >= rest->assertions;
    return result;
}

BUSTER_GLOBAL_LOCAL void ci_unit_environment(Arena* arena, String8 group, String8 workers, SliceString8* keys, SliceString8* values)
{
    SliceString8 inherited_keys = program_state->input.environment_keys;
    SliceString8 inherited_values = program_state->input.environment_values;
    keys->pointer = arena_allocate(arena, String8, inherited_keys.length + 2);
    values->pointer = arena_allocate(arena, String8, inherited_keys.length + 2);
    keys->length = 1; values->length = 1;
    keys->pointer[0] = S8("BUSTER_TEST_MODULE_GROUP"); values->pointer[0] = group;
    if (workers.length)
    {
        keys->pointer[1] = S8("BUSTER_TEST_JOBS"); values->pointer[1] = workers;
        keys->length = 2; values->length = 2;
    }
    for (u64 i = 0; i < inherited_keys.length; i += 1)
    {
        bool replaced = string_equal(inherited_keys.pointer[i], S8("BUSTER_TEST_MODULE_GROUP")) ||
            (workers.length && string_equal(inherited_keys.pointer[i], S8("BUSTER_TEST_JOBS")));
        if (!replaced)
        {
            keys->pointer[keys->length++] = inherited_keys.pointer[i];
            values->pointer[values->length++] = inherited_values.pointer[i];
        }
    }
}

BUSTER_GLOBAL_LOCAL ProcessSpawnOptions ci_unit_spawn_options(void)
{
    ProcessSpawnOptions options = {.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR), .new_process_group = 1,
        .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_TRUNCATE};
    options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = CI_UNIT_CAPTURE_LIMIT;
    options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = CI_UNIT_CAPTURE_LIMIT;
    options.capture_limits.total = CI_UNIT_CAPTURE_LIMIT;
    return options;
}

BUSTER_GLOBAL_LOCAL void ci_unit_wait_lane(void* argument)
{
    CiUnitWork* work = argument;
    LaneRange range = lane_range(2);
    for (u64 index = range.start; index < range.end; index += 1)
    {
        CiUnitChild* child = work->children + index;
        if (child->spawn.handle) { child->wait = os_process_wait_deadline(child->arena, child->spawn, work->timeout); }
        child->finish = os_now_microseconds();
        child->clean = ci_unit_clean(child->spawn, child->wait);
        if (child->clean)
        {
            child->proof = ci_unit_parse(BYTE_SLICE_TO_STRING(8, child->wait.streams[STANDARD_STREAM_OUTPUT]), index == 0, work->audits);
            child->clean = child->proof.valid;
        }
    }
}

BUSTER_GLOBAL_LOCAL bool ci_unit_replay(ProcessWaitResult wait)
{
    bool result = true;
    for (u32 stream = STANDARD_STREAM_OUTPUT; stream <= STANDARD_STREAM_ERROR; stream += 1)
    {
        OsFileTransferResult written = os_file_write_checked(os_get_standard_stream((StandardStream)stream), wait.streams[stream]);
        result = result && !written.error.v && written.transferred == wait.streams[stream].length;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_hex(String8 text, u64 count)
{
    bool result = text.length == count;
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        char8 c = text.pointer[i];
        result = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return result;
}

// Private process fixtures exercise the real native wait/capture path. They
// are never part of a compiler test inventory or an accepted measurement.
BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_self_test_child(String8 mode)
{
    bool driver = !string_equal(mode, S8("rest"));
    bool valid = string_equal(os_get_environment_variable(S8("BUSTER_TEST_MODULE_GROUP")), driver ? S8("driver") : S8("rest")) &&
        string_equal(os_get_environment_variable(S8("BUSTER_TEST_JOBS")), S8("1"));
    if (string_equal(mode, S8("hang")))
    {
        u64 start = os_now_microseconds();
        while (os_now_microseconds() - start < 10000000) {}
    }
    else
    {
        u8 filler[8192];
        memset(filler, 'x', sizeof(filler)); filler[sizeof(filler) - 1] = '\n';
        for (u32 i = 0; valid && i < 32; i += 1)
        {
            for (u32 stream = STANDARD_STREAM_OUTPUT; valid && stream <= STANDARD_STREAM_ERROR; stream += 1)
            {
                OsFileTransferResult written = os_file_write_checked(os_get_standard_stream((StandardStream)stream), (ByteSlice){filler, sizeof(filler)});
                valid = !written.error.v && written.transferred == sizeof(filler);
            }
        }
        string_print(S8("CI_UNIT_MODULE_V1 index=0 module=compiler_driver_tests table_audit=0 enabled=1 selected={u32} group=driver\n"
            "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected={u32} group=rest\n"
            "TEST_MODULE_TIMING index={u32} module={S8} duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
            "CI_UNIT_BATCH_V1 group={S8} modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"
            "[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests\n"),
            (u32)driver, (u32)!driver, driver ? 0u : 1u, driver ? S8("compiler_driver_tests") : S8("other_tests"), driver ? S8("driver") : S8("rest"));
    }
    ProcessResult result = valid && !string_equal(mode, S8("fail")) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_process_self_test(Arena* arena, String8 executable)
{
    bool result = true;
    SliceString8 keys = {0}, values = {0};
    ci_unit_environment(arena, S8("driver"), S8("1"), &keys, &values);
    // Every inherited key other than the two owned overrides is retained.
    for (u64 i = 0; result && i < program_state->input.environment_keys.length; i += 1)
    {
        String8 key = program_state->input.environment_keys.pointer[i];
        if (!string_equal(key, S8("BUSTER_TEST_MODULE_GROUP")) && !string_equal(key, S8("BUSTER_TEST_JOBS")))
        {
            bool found = false;
            for (u64 j = 0; j < keys.length; j += 1)
            {
                if (string_equal(key, keys.pointer[j]))
                {
                    found = string_equal(program_state->input.environment_values.pointer[i], values.pointer[j]);
                }
            }
            result = result && found;
        }
    }
    CiUnitWork work = {.timeout = 5000000};
    for (u64 i = 0; i < 2; i += 1)
    {
        CiUnitChild* child = work.children + i;
        child->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(16), .flags = {.no_pool = 1}});
        String8 group = i ? S8("rest") : S8("driver");
        ci_unit_environment(arena, group, S8("1"), &keys, &values);
        String8 command[] = {executable, S8("test_units_partitioned"), S8("--self-test-child"), group};
        child->start = os_now_microseconds();
        if (child->arena) { child->spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values, ci_unit_spawn_options()); }
    }
    lane_run(2, &ci_unit_wait_lane, &work);
    result = result && work.children[0].clean && work.children[1].clean && ci_unit_pair(&work.children[0].proof, &work.children[1].proof);
    for (u64 i = 0; i < 2; i += 1)
    {
        bool destroyed = work.children[i].arena && arena_destroy(work.children[i].arena, 1);
        result = result && destroyed;
    }
    String8 modes[] = {S8("fail"), S8("hang"), S8("driver")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(modes); i += 1)
    {
        ci_unit_environment(arena, S8("driver"), S8("1"), &keys, &values);
        String8 command[] = {executable, S8("test_units_partitioned"), S8("--self-test-child"), modes[i]};
        ProcessSpawnOptions options = ci_unit_spawn_options();
        if (i == 2)
        {
            options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = 1024;
            options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = 1024;
            options.capture_limits.total = 1024;
        }
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values, options);
        ProcessWaitResult wait = {0};
        if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, i == 1 ? 10000 : 5000000); }
        bool rejected = spawn.handle && !ci_unit_clean(spawn, wait) && !wait.process_tree_cleanup_failed &&
            !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
        rejected = rejected && (i == 0 ? wait.result != PROCESS_RESULT_SUCCESS && !wait.timed_out :
            i == 1 ? wait.timed_out != 0 : wait.capture_limit_exceeded && wait.output_truncated);
        result = result && rejected;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_self_test(void)
{
    String8 inventory = S8("CI_UNIT_MODULE_V1 index=0 module=compiler_driver_tests table_audit=0 enabled=1 selected=1 group=driver\n"
        "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=0 group=rest\n");
    String8 execution = S8("TEST_MODULE_TIMING index=0 module=compiler_driver_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
        "CI_UNIT_BATCH_V1 group=driver modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"
        "[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests\n");
    TemporalArena scratch = scratch_begin(0, 0);
    String8 full = string_format(scratch.arena, S8("{S8}{S8}"), inventory, execution);
    CiUnitProof valid = ci_unit_parse(full, true, false);
    bool result = valid.valid && !ci_unit_parse(full, false, false).valid && !ci_unit_parse(inventory, true, false).valid;
    String8 rejected[] = {
        S8("CI_UNIT_BATCH_V1 group=driver modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"),
        S8("TEST_MODULE_TIMING index=0 module=compiler_driver_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"),
        S8("[2/2] Unit tests (1 of 2 modules selected)\n"),
        S8("CI_UNIT_MODULE_V1 index=2 module=unexpected table_audit=0 enabled=1 selected=1 group=driver\n"),
        S8("CI_UNIT_UNKNOWN_V1 status=pass\n"),
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        result = result && !ci_unit_parse(string_format(scratch.arena, S8("{S8}{S8}"), full, rejected[i]), true, false).valid;
    }
    CiUnitProof other = valid;
    other.modules[0].selected = false; other.modules[1].selected = true;
    result = result && ci_unit_pair(&valid, &other);
    other.modules[1].name = S8("unknown_tests"); result = result && !ci_unit_pair(&valid, &other);
    other = valid; result = result && !ci_unit_pair(&valid, &other);
    ProcessSpawnResult spawn = {.handle = (OsProcessHandle*)(uintptr_t)1};
    ProcessWaitResult wait = {.result = PROCESS_RESULT_SUCCESS};
    result = result && ci_unit_clean(spawn, wait);
    wait.timed_out = 1; result = result && !ci_unit_clean(spawn, wait); wait.timed_out = 0;
    wait.capture_failed = 1; result = result && !ci_unit_clean(spawn, wait); wait.capture_failed = 0;
    wait.capture_limit_exceeded = 1; result = result && !ci_unit_clean(spawn, wait); wait.capture_limit_exceeded = 0;
    wait.process_tree_cleanup_failed = 1; result = result && !ci_unit_clean(spawn, wait); wait.process_tree_cleanup_failed = 0;
    wait.process_group_reservation_retained = 1; result = result && !ci_unit_clean(spawn, wait); wait.process_group_reservation_retained = 0;
    wait.process_group_ownership_lost = 1; result = result && !ci_unit_clean(spawn, wait);
    scratch_end(scratch);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_tests_main(Arena* arena, SliceString8 arguments, String8 executable)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("--self-test-child")))
    {
        result = ci_unit_self_test_child(arguments.pointer[1]);
    }
    else if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        bool valid = ci_unit_self_test();
        valid = ci_unit_process_self_test(arena, executable) && valid;
        string_print(S8("CI_UNIT_SELF_TEST_V1 status={S8}\n"), valid ? S8("pass") : S8("fail"));
        result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (arguments.length == 1 && arguments.pointer[0].length)
    {
        String8 path = os_path_absolute_lexical(arena, arguments.pointer[0], true);
        String8 command[] = {path, S8("test"), S8("--verbose=1"), S8("--ci=1")};
        String8 jobs_text = os_get_environment_variable(S8("BUSTER_TEST_JOBS"));
        IntegerParsingU64 jobs = string8_parse_u64_decimal(jobs_text);
        bool valid_jobs = !jobs_text.length || (jobs.status == INTEGER_PARSING_SUCCESS && jobs.length == jobs_text.length && jobs.value > 0);
        bool grouped = valid_jobs && jobs_text.length && jobs.value >= 4 && !BUSTER_SINGLE_THREADED;
        if (path.length && valid_jobs && !grouped)
        {
            SliceString8 keys = {0}, values = {0};
            ci_unit_environment(arena, (String8){0}, (String8){0}, &keys, &values);
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values, ci_unit_spawn_options());
            ProcessWaitResult wait = {0};
            if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, CI_UNIT_TIMEOUT_US); }
            bool replayed = ci_unit_replay(wait);
            result = ci_unit_clean(spawn, wait) && replayed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        else if (path.length && grouped)
        {
            String8 revision = os_get_environment_variable(S8("BUSTER_TEST_SOURCE_REVISION"));
            String8 digest = {0};
            bool valid = ci_unit_hex(revision, 40) && stage_object_sha256_file(arena, path, &digest);
            if (valid)
            {
                u64 epoch = os_now_microseconds();
                u64 group_workers = jobs.value / 2;
                string_print(S8("CI_UNIT_PLAN_V1 source_revision={S8} binary_sha256={S8} workers={u64} groups=2 group_workers={u64}\n"), revision, digest, jobs.value, group_workers);
                CiUnitWork work = {.timeout = CI_UNIT_TIMEOUT_US,
                    .audits = !string_equal(os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS")), S8("0"))};
                bool admitted = true;
                for (u64 i = 0; i < 2; i += 1)
                {
                    CiUnitChild* child = work.children + i;
                    child->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256), .flags = {.no_pool = 1}});
                    SliceString8 keys = {0}, values = {0};
                    ci_unit_environment(arena, i ? S8("rest") : S8("driver"), string_format(arena, S8("{u64}"), group_workers), &keys, &values);
                    child->start = os_now_microseconds();
                    if (child->arena && admitted) { child->spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values, ci_unit_spawn_options()); }
                    admitted = admitted && child->spawn.handle != 0;
                }
                if (!admitted) { work.timeout = 1; }
                // Both pipes must be drained concurrently: waiting for one
                // child first can block the other on its bounded OS pipe.
                lane_run(2, &ci_unit_wait_lane, &work);
                valid = ci_unit_pair(&work.children[0].proof, &work.children[1].proof);
                u64 assertions = 0, modules = 0;
                for (u64 i = 0; i < 2; i += 1)
                {
                    CiUnitChild* child = work.children + i;
                    bool replayed = ci_unit_replay(child->wait);
                    bool destroyed = child->arena && arena_destroy(child->arena, 1);
                    bool clean = child->clean && replayed && destroyed;
                    string_print(S8("CI_UNIT_PROCESS_V1 group={S8} workers={u64} start_us={u64} end_us={u64} elapsed_us={u64} exit={u32} native_status={u32} timed_out={u32} capture_failed={u32} cleanup_failed={u32} status={S8}\n"),
                        i ? S8("rest") : S8("driver"), group_workers, child->start - epoch, child->finish - epoch, child->finish - child->start,
                        clean ? 0u : 1u, child->wait.platform_status, (u32)child->wait.timed_out,
                        (u32)(child->wait.capture_failed || child->wait.output_truncated || child->wait.capture_limit_exceeded),
                        (u32)(child->wait.process_tree_cleanup_failed || child->wait.process_group_reservation_retained || child->wait.process_group_ownership_lost || !destroyed), clean ? S8("pass") : S8("fail"));
                    valid = valid && clean;
                    assertions += child->proof.assertions; modules += child->proof.selected;
                }
                string_print(S8("CI_UNIT_PARTITION_V1 groups=2 workers={u64} elapsed_us={u64} modules={u64} assertions={u64} passed={u64} failed={u64} status={S8}\n"),
                    jobs.value, os_now_microseconds() - epoch, modules, assertions, valid ? assertions : 0, valid ? 0ull : 1ull, valid ? S8("pass") : S8("fail"));
                result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            }
            else { string_print(S8("error: partitioned tests require exact BUSTER_TEST_SOURCE_REVISION and readable binary\n")); }
        }
        else { string_print(S8("error: invalid unit-test path or BUSTER_TEST_JOBS quota\n")); }
    }
    else { string_print(S8("usage: test_units_partitioned <ide-path> | --self-test\n")); }
    return result;
}
