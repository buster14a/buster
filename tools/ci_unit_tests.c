// Isolated CI unit-module processes, included only by the native build driver.
// ci_unit_tests_main owns child admission, quotas, bounded capture and cleanup.
// ci_unit_schedule_run queries an independent inventory and dynamically assigns
// modules to two persistent process slots; output replays in canonical order.
// ci_unit_parse_selection binds each process to one enabled inventory module.
// BUSTER_TEST_UNIT_SCHEDULE=pair retains the old controlled experiment; its
// ci_unit_pair merges stable primary/rest results. A
// serial bootstrap or a quota below four retains the ordinary full invocation.
// Two workers per child retain OS multi-lane assertions.
// ci_unit_source_revision resolves provenance; serial_run retains streaming.
#define CI_UNIT_MAX_MODULES 512
#define CI_UNIT_CAPTURE_LIMIT BUSTER_MB(64)
#define CI_UNIT_TIMEOUT_US (5400ull * 1000000ull)
#define CI_UNIT_IDENTITY_CAPTURE_LIMIT BUSTER_KB(4)
#define CI_UNIT_IDENTITY_TIMEOUT_US (5ull * 1000000ull)

typedef struct CiUnitModule CiUnitModule;
struct CiUnitModule
{
    String8 name;
    bool audit, enabled, selected, primary, timed;
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

// Legacy pair ownership, independently matched by the test selector.
BUSTER_GLOBAL_LOCAL String8 ci_unit_primary_module(void)
{
    String8 result = BUSTER_WINDOWS && BUSTER_CPU_ARCH_AARCH64 ? S8("compiler_driver_tests") : S8("c_frontend_tests");
    return result;
}

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

BUSTER_GLOBAL_LOCAL CiUnitProof ci_unit_parse_selection(String8 output, bool primary, bool audits, String8 module_name, bool inventory)
{
    CiUnitProof proof = {.valid = true};
    bool batch_seen = false;
    bool terminal_seen[3] = {0};
    u64 terminal_count[3] = {0};
    u64 batch_modules = 0, batch_assertions = 0, batch_external = 0;
    u64 primary_count = 0;
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
            bool owned = string_equal(name, ci_unit_primary_module());
            proof.valid = proof.valid && string_equal(owner, owned ? S8("primary") : S8("rest")) &&
                enabled == (u64)(!audit || audits) && selected == (u64)(enabled && !inventory &&
                    (module_name.length ? string_equal(name, module_name) : owned == primary)) && (!owned || !audit);
            for (u64 previous = 0; proof.valid && previous < proof.count; previous += 1)
            {
                proof.valid = !string_equal(proof.modules[previous].name, name);
            }
            if (proof.valid)
            {
                proof.modules[proof.count++] = (CiUnitModule){.name = name, .audit = audit != 0, .enabled = enabled != 0,
                    .selected = selected != 0, .primary = owned};
                proof.selected += selected;
                primary_count += owned;
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
                string_equal(group, inventory ? S8("inventory") : module_name.length ? S8("module") : primary ? S8("primary") : S8("rest")) &&
                string_equal(status, inventory ? S8("inventory") : S8("pass")) && !failed &&
                modules_passed == batch_modules && passed == batch_assertions && external_passed == batch_external;
            batch_seen = true;
        }
        else if (inventory && string_starts_with_sequence(line, S8("CI_UNIT_HOST_V1 "))) {}
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
    proof.valid = proof.valid && proof.count > 1 && primary_count == 1 && (inventory ? !proof.selected : module_name.length ? proof.selected == 1 : proof.selected != 0) && batch_seen &&
        terminal_seen[0] && terminal_seen[1] && terminal_seen[2] && batch_modules == proof.selected &&
        batch_assertions == proof.assertions && terminal_count[0] == proof.assertions && terminal_count[1] == proof.selected && terminal_count[2] == batch_external;
    proof.external = batch_external;
    return proof;
}

BUSTER_GLOBAL_LOCAL CiUnitProof ci_unit_parse(String8 output, bool primary, bool audits)
{
    CiUnitProof result = ci_unit_parse_selection(output, primary, audits, (String8){0}, false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_pair(CiUnitProof* primary, CiUnitProof* rest)
{
    bool result = primary->valid && rest->valid && primary->count == rest->count;
    for (u64 i = 0; result && i < primary->count; i += 1)
    {
        CiUnitModule* a = primary->modules + i;
        CiUnitModule* b = rest->modules + i;
        result = string_equal(a->name, b->name) && a->audit == b->audit && a->enabled == b->enabled && a->primary == b->primary &&
            (u64)a->selected + (u64)b->selected == (u64)a->enabled;
    }
    result = result && UINT64_MAX - primary->assertions >= rest->assertions;
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

BUSTER_GLOBAL_LOCAL bool ci_unit_revision_select(String8 supplied, String8 queried, String8* revision)
{
    bool result = true;
    if (supplied.length)
    {
        result = ci_unit_hex(supplied, 40);
        *revision = supplied;
    }
    else { *revision = ci_unit_hex(queried, 40) ? queried : S8("unknown"); }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_source_revision(Arena* arena, String8 supplied, String8* revision)
{
    String8 queried = {0};
    bool query_safe = true;
    if (!supplied.length)
    {
        String8 arguments[] = {S8("git"), S8("rev-parse"), S8("--verify"), S8("HEAD")};
        ProcessSpawnOptions options = {.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
            .use_process_environment = 1, .search_path = 1, .new_process_group = 1,
            .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_TRUNCATE};
        options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        options.capture_limits.total = 2 * CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), (SliceString8){0}, (SliceString8){0}, options);
        ProcessWaitResult wait = {0};
        if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, CI_UNIT_IDENTITY_TIMEOUT_US); }
        query_safe = !wait.process_tree_cleanup_failed && !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
        if (ci_unit_clean(spawn, wait))
        {
            queried = build_compiler_output_trim(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]));
        }
    }
    bool result = ci_unit_revision_select(supplied, queried, revision) && query_safe;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_serial_run(Arena* arena, SliceString8 command)
{
    SliceString8 keys = {0}, values = {0};
    ci_unit_environment(arena, (String8){0}, (String8){0}, &keys, &values);
    // Uncaptured streams inherit the ordinary parent handles. No process
    // group, output quota or parent deadline is introduced on this path.
    ProcessSpawnResult spawn = os_process_spawn(command, keys, values, (ProcessSpawnOptions){0});
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
        result = wait.result;
    }
    return result;
}

// Two parent lanes claim stable module slots; replay preserves table order.
// Retained captures share the pair's 128 MiB budget.
typedef struct CiUnitSchedule CiUnitSchedule;
struct CiUnitSchedule
{
    CiUnitProof inventory;
    CiUnitChild* children;
    u64 order[CI_UNIT_MAX_MODULES];
    u64 count, workers, epoch;
    String8 executable;
    AtomicU64 next, captured, failed;
    bool audits, self_test;
};

BUSTER_GLOBAL_LOCAL bool ci_unit_inventory_equal(CiUnitProof* a, CiUnitProof* b)
{
    bool result = a->valid && b->valid && a->count == b->count;
    for (u64 index = 0; result && index < a->count; index += 1)
    {
        CiUnitModule* left = a->modules + index;
        CiUnitModule* right = b->modules + index;
        result = string_equal(left->name, right->name) && left->audit == right->audit &&
            left->enabled == right->enabled && left->primary == right->primary;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void ci_unit_schedule_order(CiUnitSchedule* work)
{
    String8 priority[] = {S8("c_frontend_tests"), S8("compiler_driver_tests"), S8("c_ast_tests")};
    bool included[CI_UNIT_MAX_MODULES] = {0};
    work->count = 0;
    for (u64 priority_index = 0; priority_index < BUSTER_ARRAY_LENGTH(priority); priority_index += 1)
    {
        for (u64 index = 0; index < work->inventory.count; index += 1)
        {
            if (work->inventory.modules[index].enabled && string_equal(work->inventory.modules[index].name, priority[priority_index]))
            {
                included[index] = true;
                work->order[work->count++] = index;
            }
        }
    }
    for (u64 index = 0; index < work->inventory.count; index += 1)
    {
        if (work->inventory.modules[index].enabled && !included[index]) { work->order[work->count++] = index; }
    }
}

BUSTER_GLOBAL_LOCAL void ci_unit_schedule_lane(void* argument)
{
    CiUnitSchedule* work = argument;
    bool running = true;
    while (running && !atomic_u64_add(&work->failed, 0))
    {
        if (os_now_microseconds() - work->epoch >= CI_UNIT_TIMEOUT_US) { atomic_u64_increment(&work->failed); }
        u64 ticket = atomic_u64_increment(&work->next);
        running = ticket < work->count && !atomic_u64_add(&work->failed, 0);
        if (running)
        {
            u64 index = work->order[ticket];
            String8 name = work->inventory.modules[index].name;
            CiUnitChild* child = work->children + index;
            child->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256), .flags = {.no_pool = 1}});
            child->start = os_now_microseconds();
            if (child->arena)
            {
                SliceString8 keys = {0}, values = {0};
                ci_unit_environment(child->arena, string_format(child->arena, S8("module:{S8}"), name),
                    string_format(child->arena, S8("{u64}"), work->workers), &keys, &values);
                String8 command[] = {work->executable, work->self_test ? S8("test_units_partitioned") : S8("test"),
                    work->self_test ? S8("--self-test-module-child") : S8("--verbose=1"), S8("--ci=1")};
                SliceString8 arguments = {command, work->self_test ? 3 : 4};
                child->spawn = os_process_spawn(arguments, keys, values, ci_unit_spawn_options());
                if (child->spawn.handle)
                {
                    u64 elapsed = os_now_microseconds() - work->epoch;
                    u64 remaining = elapsed < CI_UNIT_TIMEOUT_US ? CI_UNIT_TIMEOUT_US - elapsed : 1;
                    child->wait = os_process_wait_deadline(child->arena, child->spawn, remaining);
                }
                child->clean = ci_unit_clean(child->spawn, child->wait);
                if (child->clean)
                {
                    child->proof = ci_unit_parse_selection(BYTE_SLICE_TO_STRING(8, child->wait.streams[STANDARD_STREAM_OUTPUT]),
                        false, work->audits, name, false);
                    child->clean = ci_unit_inventory_equal(&work->inventory, &child->proof);
                }
                u64 captured = child->wait.streams[STANDARD_STREAM_OUTPUT].length + child->wait.streams[STANDARD_STREAM_ERROR].length;
                u64 retained = atomic_u64_add(&work->captured, captured);
                child->clean = child->clean && retained + captured <= 2 * CI_UNIT_CAPTURE_LIMIT;
            }
            child->finish = os_now_microseconds();
            if (!child->clean) { atomic_u64_increment(&work->failed); }
        }
    }
}

BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_schedule_run(Arena* arena, String8 path, u64 jobs, String8 revision, String8 digest, bool self_test)
{
    u64 epoch = os_now_microseconds();
    bool audits = !string_equal(os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS")), S8("0"));
    SliceString8 keys = {0}, values = {0};
    ci_unit_environment(arena, S8("inventory"), S8("1"), &keys, &values);
    String8 command[] = {path, self_test ? S8("test_units_partitioned") : S8("test"),
        self_test ? S8("--self-test-module-child") : S8("--verbose=1"), S8("--ci=1")};
    ProcessSpawnResult spawn = os_process_spawn((SliceString8){command, self_test ? 3 : 4}, keys, values, ci_unit_spawn_options());
    ProcessWaitResult wait = {0};
    if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, CI_UNIT_TIMEOUT_US); }
    CiUnitSchedule* work = arena_allocate(arena, CiUnitSchedule, 1);
    memset(work, 0, sizeof(*work));
    bool valid = ci_unit_clean(spawn, wait);
    if (valid)
    {
        work->inventory = ci_unit_parse_selection(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]), false, audits, (String8){0}, true);
        valid = work->inventory.valid;
    }
    if (valid)
    {
        work->workers = jobs / 2;
        work->epoch = epoch;
        work->executable = path;
        work->audits = audits;
        work->self_test = self_test;
        work->children = arena_allocate(arena, CiUnitChild, work->inventory.count);
        memset(work->children, 0, sizeof(*work->children) * work->inventory.count);
        ci_unit_schedule_order(work);
        string_print(S8("CI_UNIT_SCHEDULE_PLAN_V1 source_revision={S8} binary_sha256={S8} workers={u64} slots=2 slot_workers={u64} modules={u64}\n"),
            revision, digest, jobs, work->workers, work->count);
        lane_run(2, &ci_unit_schedule_lane, work);
        valid = !atomic_u64_add(&work->failed, 0);
    }
    else
    {
        ci_unit_replay(wait);
        string_print(S8("error: unit-module inventory query did not complete cleanly\n"));
    }
    u64 modules = 0, assertions = 0, external = 0;
    for (u64 index = 0; work->children && index < work->inventory.count; index += 1)
    {
        if (work->inventory.modules[index].enabled)
        {
            CiUnitChild* child = work->children + index;
            bool clean = child->clean;
            if (child->arena)
            {
                clean = ci_unit_replay(child->wait) && clean;
                clean = arena_destroy(child->arena, 1) && clean;
            }
            else { clean = false; }
            valid = valid && clean && UINT64_MAX - assertions >= child->proof.assertions &&
                UINT64_MAX - external >= child->proof.external;
            modules += clean;
            assertions += child->proof.assertions;
            external += child->proof.external;
            string_print(S8("CI_UNIT_PROCESS_V1 group=module module={S8} index={u64} workers={u64} start_us={u64} end_us={u64} elapsed_us={u64} exit={u32} native_status={u32} timed_out={u32} capture_failed={u32} cleanup_failed={u32} status={S8}\n"),
                work->inventory.modules[index].name, index, work->workers, child->start ? child->start - epoch : 0,
                child->finish ? child->finish - epoch : 0, child->finish - child->start, clean ? 0u : 1u, child->wait.platform_status,
                (u32)child->wait.timed_out, (u32)(child->wait.capture_failed || child->wait.output_truncated || child->wait.capture_limit_exceeded),
                (u32)(child->wait.process_tree_cleanup_failed || child->wait.process_group_reservation_retained || child->wait.process_group_ownership_lost),
                clean ? S8("pass") : S8("fail"));
        }
    }
    valid = valid && modules == work->count && modules != 0;
    string_print(S8("CI_UNIT_SCHEDULE_V1 slots=2 workers={u64} elapsed_us={u64} modules={u64} assertions={u64} passed={u64} failed={u64} external={u64} captured_bytes={u64} status={S8}\n"),
        jobs, os_now_microseconds() - epoch, modules, assertions, valid ? assertions : 0, valid ? 0ull : 1ull, external,
        atomic_u64_add(&work->captured, 0), valid ? S8("pass") : S8("fail"));
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

// Private process fixtures exercise the real native wait/capture path. They
// are never part of a compiler test inventory or an accepted measurement.
BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_self_test_child(String8 mode)
{
    bool primary = !string_equal(mode, S8("rest"));
    bool valid = string_equal(os_get_environment_variable(S8("BUSTER_TEST_MODULE_GROUP")), primary ? S8("primary") : S8("rest")) &&
        string_equal(os_get_environment_variable(S8("BUSTER_TEST_JOBS")), S8("1"));
    bool fallback = string_equal(mode, S8("fallback-pass")) || string_equal(mode, S8("fallback-fail"));
    if (fallback)
    {
        valid = !os_get_environment_variable(S8("BUSTER_TEST_MODULE_GROUP")).length &&
            string_equal(os_get_environment_variable(S8("BUSTER_TEST_JOBS")), S8("2"));
        string_print(S8("CI_UNIT_FALLBACK_STDOUT\n"));
        ByteSlice message = BUSTER_SLICE_TO_BYTE_SLICE(S8("CI_UNIT_FALLBACK_STDERR\n"));
        OsFileTransferResult written = os_file_write_checked(os_get_standard_stream(STANDARD_STREAM_ERROR), message);
        valid = valid && !written.error.v && written.transferred == message.length;
    }
    else if (string_equal(mode, S8("hang")))
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
        string_print(S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected={u32} group=primary\n"
            "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected={u32} group=rest\n"
            "TEST_MODULE_TIMING index={u32} module={S8} duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
            "CI_UNIT_BATCH_V1 group={S8} modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"
            "[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests\n"),
            ci_unit_primary_module(), (u32)primary, (u32)!primary, primary ? 0u : 1u, primary ? ci_unit_primary_module() : S8("other_tests"), primary ? S8("primary") : S8("rest"));
    }
    ProcessResult result = valid && !string_equal(mode, S8("fail")) && !string_equal(mode, S8("fallback-fail")) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_process_self_test(Arena* arena, String8 executable)
{
    bool result = true;
    SliceString8 keys = {0}, values = {0};
    ci_unit_environment(arena, S8("primary"), S8("1"), &keys, &values);
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
        String8 group = i ? S8("rest") : S8("primary");
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
    String8 modes[] = {S8("fail"), S8("hang"), S8("primary")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(modes); i += 1)
    {
        ci_unit_environment(arena, S8("primary"), S8("1"), &keys, &values);
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

BUSTER_GLOBAL_LOCAL bool ci_unit_review_self_test(Arena* arena, String8 executable)
{
    String8 exact = S8("0123456789abcdef0123456789abcdef01234567");
    String8 revision = {0};
    bool result = ci_unit_revision_select((String8){0}, exact, &revision) && string_equal(revision, exact);
    result = ci_unit_revision_select((String8){0}, (String8){0}, &revision) && string_equal(revision, S8("unknown")) && result;
    result = ci_unit_revision_select((String8){0}, S8("invalid git output"), &revision) && string_equal(revision, S8("unknown")) && result;
    result = !ci_unit_source_revision(arena, S8("malformed"), &revision) && result;
    result = ci_unit_source_revision(arena, exact, &revision) && string_equal(revision, exact) && result;
    result = ci_unit_source_revision(arena, (String8){0}, &revision) &&
        (ci_unit_hex(revision, 40) || string_equal(revision, S8("unknown"))) && result;
    String8 modes[] = {S8("fallback-pass"), S8("fallback-fail")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(modes); i += 1)
    {
        SliceString8 keys = {0}, values = {0};
        ci_unit_environment(arena, S8("primary"), S8("2"), &keys, &values);
        String8 command[] = {executable, S8("test_units_partitioned"), S8("--self-test-fallback"), modes[i]};
        ProcessSpawnOptions options = ci_unit_spawn_options();
        options.capture_limits.per_stream[STANDARD_STREAM_OUTPUT] = CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        options.capture_limits.per_stream[STANDARD_STREAM_ERROR] = CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        options.capture_limits.total = 2 * CI_UNIT_IDENTITY_CAPTURE_LIMIT;
        ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(command), keys, values, options);
        ProcessWaitResult wait = {0};
        if (spawn.handle) { wait = os_process_wait_deadline(arena, spawn, 5000000); }
        bool status = spawn.handle && wait.result == (i ? PROCESS_RESULT_FAILED : PROCESS_RESULT_SUCCESS) && !wait.timed_out &&
            !wait.capture_failed && !wait.output_truncated && !wait.process_tree_cleanup_failed &&
            string_equal(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]), S8("CI_UNIT_FALLBACK_STDOUT\n")) &&
            string_equal(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]), S8("CI_UNIT_FALLBACK_STDERR\n"));
        result = status && result;
    }
    return result;
}

typedef struct CiUnitObservation CiUnitObservation;
struct CiUnitObservation
{
    CiUnitProof proof;
    String8 revision, digest;
    u64 elapsed, workers, external;
    bool valid;
};

// Compare matched module work after the native parent's coverage gate.
BUSTER_GLOBAL_LOCAL CiUnitObservation ci_unit_observe(String8 output, bool scheduled)
{
    CiUnitObservation observation = {.valid = true};
    bool plan_seen = false, terminal_seen = false;
    u64 cursor = 0, terminal_modules = 0, terminal_assertions = 0;
    while (observation.valid && cursor < output.length)
    {
        u64 begin = cursor;
        while (cursor < output.length && output.pointer[cursor] != '\n') { cursor += 1; }
        String8 line = {output.pointer + begin, cursor - begin};
        cursor += cursor < output.length;
        if (line.length && line.pointer[line.length - 1] == '\r') { line.length -= 1; }
        String8 text = line;
        if (string_starts_with_sequence(line, S8("TEST_MODULE_TIMING ")))
        {
            u64 index = 0, duration = 0, passed = 0, failed = 0, assertions = 0;
            String8 name = {0}, status = {0};
            observation.valid = !terminal_seen && ci_unit_number(&text, S8("TEST_MODULE_TIMING index="), &index) &&
                ci_unit_word(&text, S8(" module="), &name) && ci_unit_number(&text, S8(" duration_ns="), &duration) &&
                ci_unit_number(&text, S8(" passed="), &passed) && ci_unit_number(&text, S8(" failed="), &failed) &&
                ci_unit_number(&text, S8(" assertions="), &assertions) && ci_unit_word(&text, S8(" status="), &status) &&
                !text.length && index < CI_UNIT_MAX_MODULES && ci_unit_token(name) && !failed && passed == assertions &&
                string_equal(status, S8("pass")) && !observation.proof.modules[index].timed &&
                UINT64_MAX - observation.proof.assertions >= assertions;
            if (observation.valid)
            {
                for (u64 previous = 0; observation.valid && previous < CI_UNIT_MAX_MODULES; previous += 1)
                {
                    observation.valid = !observation.proof.modules[previous].timed || !string_equal(observation.proof.modules[previous].name, name);
                }
                observation.proof.modules[index] = (CiUnitModule){.name = name, .timed = true, .assertions = assertions};
                observation.proof.count += 1;
                observation.proof.assertions += assertions;
            }
        }
        else if (string_starts_with_sequence(line, S8("CI_UNIT_BATCH_V1 ")))
        {
            u64 modules = 0, modules_passed = 0, assertions = 0, passed = 0, failed = 0, external = 0, external_passed = 0;
            String8 group = {0}, status = {0};
            observation.valid = !terminal_seen && ci_unit_word(&text, S8("CI_UNIT_BATCH_V1 group="), &group) &&
                ci_unit_number(&text, S8(" modules="), &modules) && ci_unit_number(&text, S8(" modules_passed="), &modules_passed) &&
                ci_unit_number(&text, S8(" assertions="), &assertions) && ci_unit_number(&text, S8(" passed="), &passed) &&
                ci_unit_number(&text, S8(" failed="), &failed) && ci_unit_number(&text, S8(" external="), &external) &&
                ci_unit_number(&text, S8(" external_passed="), &external_passed) && ci_unit_word(&text, S8(" status="), &status) &&
                !text.length && modules == modules_passed && assertions == passed && !failed && external == external_passed &&
                string_equal(status, S8("pass")) && UINT64_MAX - observation.external >= external;
            observation.external += external;
        }
        else if (string_starts_with_sequence(line, scheduled ? S8("CI_UNIT_SCHEDULE_PLAN_V1 ") : S8("CI_UNIT_PLAN_V1 ")))
        {
            u64 slots = 0, slot_workers = 0, modules = 0;
            String8 primary = {0};
            observation.valid = !plan_seen && !terminal_seen &&
                ci_unit_word(&text, scheduled ? S8("CI_UNIT_SCHEDULE_PLAN_V1 source_revision=") : S8("CI_UNIT_PLAN_V1 source_revision="), &observation.revision) &&
                ci_unit_word(&text, S8(" binary_sha256="), &observation.digest) && ci_unit_number(&text, S8(" workers="), &observation.workers) &&
                ci_unit_number(&text, scheduled ? S8(" slots=") : S8(" groups="), &slots) &&
                ci_unit_number(&text, scheduled ? S8(" slot_workers=") : S8(" group_workers="), &slot_workers);
            if (scheduled) { observation.valid = observation.valid && ci_unit_number(&text, S8(" modules="), &modules); }
            else { observation.valid = observation.valid && ci_unit_word(&text, S8(" primary_module="), &primary); }
            observation.valid = observation.valid && !text.length && slots == 2 && slot_workers >= 2 &&
                slot_workers == observation.workers / 2 && ci_unit_hex(observation.revision, 40) && ci_unit_hex(observation.digest, 64);
            plan_seen = true;
        }
        else if (string_starts_with_sequence(line, scheduled ? S8("CI_UNIT_SCHEDULE_V1 ") : S8("CI_UNIT_PARTITION_V1 ")))
        {
            u64 slots = 0, workers = 0, passed = 0, failed = 0, external = 0, captured = 0;
            String8 status = {0};
            observation.valid = plan_seen && !terminal_seen &&
                ci_unit_number(&text, scheduled ? S8("CI_UNIT_SCHEDULE_V1 slots=") : S8("CI_UNIT_PARTITION_V1 groups="), &slots) &&
                ci_unit_number(&text, S8(" workers="), &workers) && ci_unit_number(&text, S8(" elapsed_us="), &observation.elapsed) &&
                ci_unit_number(&text, S8(" modules="), &terminal_modules) && ci_unit_number(&text, S8(" assertions="), &terminal_assertions) &&
                ci_unit_number(&text, S8(" passed="), &passed) && ci_unit_number(&text, S8(" failed="), &failed);
            if (scheduled)
            {
                observation.valid = observation.valid && ci_unit_number(&text, S8(" external="), &external) &&
                    ci_unit_number(&text, S8(" captured_bytes="), &captured) && captured <= 2 * CI_UNIT_CAPTURE_LIMIT;
            }
            observation.valid = observation.valid && ci_unit_word(&text, S8(" status="), &status) && !text.length &&
                slots == 2 && workers == observation.workers && observation.elapsed && !failed &&
                passed == terminal_assertions && string_equal(status, S8("pass"));
            terminal_seen = true;
        }
    }
    observation.valid = observation.valid && plan_seen && terminal_seen && observation.proof.count &&
        terminal_modules == observation.proof.count && terminal_assertions == observation.proof.assertions;
    return observation;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_observations_equal(CiUnitObservation* pair, CiUnitObservation* scheduled)
{
    bool result = pair->valid && scheduled->valid && string_equal(pair->revision, scheduled->revision) &&
        string_equal(pair->digest, scheduled->digest) && pair->workers == scheduled->workers &&
        pair->proof.count == scheduled->proof.count && pair->proof.assertions == scheduled->proof.assertions && pair->external == scheduled->external;
    for (u64 index = 0; result && index < CI_UNIT_MAX_MODULES; index += 1)
    {
        CiUnitModule* left = pair->proof.modules + index;
        CiUnitModule* right = scheduled->proof.modules + index;
        result = left->timed == right->timed && (!left->timed ||
            (string_equal(left->name, right->name) && left->assertions == right->assertions));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ci_unit_fixture_next(String8 output, u64* cursor, String8 module_name)
{
    String8 result = {0};
    while (!result.length && *cursor < output.length)
    {
        u64 begin = *cursor;
        while (*cursor < output.length && output.pointer[*cursor] != '\n') { *cursor += 1; }
        String8 line = {output.pointer + begin, *cursor - begin};
        *cursor += *cursor < output.length;
        if (line.length && line.pointer[line.length - 1] == '\r') { line.length -= 1; }
        String8 text = line, kind = {0}, module = {0};
        if (ci_unit_word(&text, S8("TEST_FIXTURE_START_V1 kind="), &kind) &&
            ci_unit_word(&text, S8(" module="), &module) && string_equal(module, module_name))
        {
            result = line;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_fixtures_equal(String8 pair, String8 scheduled, CiUnitProof* proof)
{
    bool result = true;
    for (u64 index = 0; result && index < CI_UNIT_MAX_MODULES; index += 1)
    {
        if (proof->modules[index].timed)
        {
            u64 pair_cursor = 0, scheduled_cursor = 0;
            bool running = true;
            while (result && running)
            {
                String8 left = ci_unit_fixture_next(pair, &pair_cursor, proof->modules[index].name);
                String8 right = ci_unit_fixture_next(scheduled, &scheduled_cursor, proof->modules[index].name);
                result = string_equal(left, right);
                running = left.length || right.length;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 ci_unit_log_text(Arena* arena, ByteSlice bytes)
{
    String8 result = BYTE_SLICE_TO_STRING(8, bytes);
    if (bytes.length >= 2 && ((bytes.pointer[0] == 0xff && bytes.pointer[1] == 0xfe) ||
                             (bytes.pointer[0] == 0xfe && bytes.pointer[1] == 0xff)))
    {
        result = (String8){0};
        if (!(bytes.length & 1))
        {
            bool little_endian = bytes.pointer[0] == 0xff;
            String16 text = {.pointer = arena_allocate(arena, char16, bytes.length / 2 - 1), .length = bytes.length / 2 - 1};
            for (u64 index = 0; index < text.length; index += 1)
            {
                u8* source = bytes.pointer + 2 + 2 * index;
                text.pointer[index] = (char16)(little_endian ? source[0] | ((u16)source[1] << 8) : source[1] | ((u16)source[0] << 8));
            }
            result = string8_from_string16(arena, text, false);
        }
    }
    else if (bytes.length >= 3 && bytes.pointer[0] == 0xef && bytes.pointer[1] == 0xbb && bytes.pointer[2] == 0xbf)
    {
        result = (String8){(char8*)bytes.pointer + 3, bytes.length - 3};
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_compare(Arena* arena, String8 pair_path, String8 schedule_path)
{
    FileMapRead pair_file = file_map_read(arena, pair_path, (FileReadOptions){0});
    FileMapRead schedule_file = file_map_read(arena, schedule_path, (FileReadOptions){0});
    bool valid = pair_file.bytes.pointer && schedule_file.bytes.pointer &&
        pair_file.bytes.length <= 4 * CI_UNIT_CAPTURE_LIMIT && schedule_file.bytes.length <= 4 * CI_UNIT_CAPTURE_LIMIT;
    CiUnitObservation pair = {0}, scheduled = {0};
    if (valid)
    {
        String8 pair_text = ci_unit_log_text(arena, pair_file.bytes);
        String8 schedule_text = ci_unit_log_text(arena, schedule_file.bytes);
        pair = ci_unit_observe(pair_text, false);
        scheduled = ci_unit_observe(schedule_text, true);
        valid = ci_unit_observations_equal(&pair, &scheduled) && ci_unit_fixtures_equal(pair_text, schedule_text, &pair.proof);
    }
    string_print(S8("CI_UNIT_COMPARE_V1 source_revision={S8} binary_sha256={S8} workers={u64} modules={u64} assertions={u64} pair_elapsed_us={u64} schedule_elapsed_us={u64} status={S8}\n"),
        pair.revision, pair.digest, pair.workers, pair.proof.count, pair.proof.assertions, pair.elapsed, scheduled.elapsed,
        valid ? S8("pass") : S8("fail"));
    file_map_unmap(pair_file);
    file_map_unmap(schedule_file);
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

// Private children exercise the native schedule without registered assertions.
BUSTER_GLOBAL_LOCAL ProcessResult ci_unit_schedule_test_child(void)
{
    String8 group = os_get_environment_variable(S8("BUSTER_TEST_MODULE_GROUP"));
    bool inventory = string_equal(group, S8("inventory"));
    bool audits = !string_equal(os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS")), S8("0"));
    String8 name = string_starts_with_sequence(group, S8("module:")) ? (String8){group.pointer + 7, group.length - 7} : (String8){0};
    String8 names[] = {ci_unit_primary_module(), S8("other_tests"), S8("audit_tests")};
    u64 selected = 0;
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        bool enabled = index != 2 || audits;
        bool runs = !inventory && enabled && string_equal(name, names[index]);
        selected += runs;
        string_print(S8("CI_UNIT_MODULE_V1 index={u64} module={S8} table_audit={u32} enabled={u32} selected={u32} group={S8}\n"),
            index, names[index], (u32)(index == 2), (u32)enabled, (u32)runs, index ? S8("rest") : S8("primary"));
    }
    if (selected == 1)
    {
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
        {
            if (string_equal(name, names[index]))
            {
                string_print(S8("TEST_MODULE_TIMING index={u64} module={S8} duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"), index, name);
            }
        }
    }
    string_print(S8("CI_UNIT_BATCH_V1 group={S8} modules={u64} modules_passed={u64} assertions={u64} passed={u64} failed=0 external=0 external_passed=0 status={S8}\n"
        "[{u64}/{u64}] Unit tests ({u64} of 3 modules selected)\n[{u64}/{u64}] Module tests\n[0/0] External tests\n"),
        inventory ? S8("inventory") : S8("module"), selected, selected, 2 * selected, 2 * selected,
        inventory ? S8("inventory") : S8("pass"), 2 * selected, 2 * selected, selected, selected, selected);
    bool valid = (inventory || selected == 1) && string_equal(os_get_environment_variable(S8("BUSTER_TEST_JOBS")), inventory ? S8("1") : S8("2"));
    ProcessResult result = valid ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_schedule_self_test(Arena* arena, String8 executable)
{
    String8 rows = string_format(arena, S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected=0 group=primary\n"
        "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=1 group=rest\n"), ci_unit_primary_module());
    String8 execution = S8("TEST_MODULE_TIMING index=1 module=other_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
        "CI_UNIT_BATCH_V1 group=module modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"
        "[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests\n");
    String8 full = string_format(arena, S8("{S8}{S8}"), rows, execution);
    CiUnitProof proof = ci_unit_parse_selection(full, false, false, S8("other_tests"), false);
    bool result = proof.valid && proof.selected == 1 && proof.assertions == 2 &&
        !ci_unit_parse_selection(full, false, false, ci_unit_primary_module(), false).valid &&
        !ci_unit_parse_selection(rows, false, false, S8("other_tests"), false).valid &&
        !ci_unit_parse_selection(string_format(arena, S8("{S8}{S8}"), full, execution), false, false, S8("other_tests"), false).valid;
    String8 inventory = string_format(arena, S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected=0 group=primary\n"
        "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=0 group=rest\n"
        "CI_UNIT_BATCH_V1 group=inventory modules=0 modules_passed=0 assertions=0 passed=0 failed=0 external=0 external_passed=0 status=inventory\n"
        "[0/0] Unit tests (0 of 2 modules selected)\n[0/0] Module tests\n[0/0] External tests\n"), ci_unit_primary_module());
    CiUnitProof queried = ci_unit_parse_selection(inventory, false, false, (String8){0}, true);
    result = result && ci_unit_inventory_equal(&queried, &proof);
    proof.modules[1].name = S8("changed_tests");
    result = result && !ci_unit_inventory_equal(&queried, &proof);
    proof.modules[1].name = S8("other_tests");
    proof.modules[1].enabled = false;
    result = result && !ci_unit_inventory_equal(&queried, &proof);
    String8 source = S8("0123456789abcdef0123456789abcdef01234567");
    String8 digest = S8("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    String8 timing = S8("TEST_MODULE_TIMING index=1 module=other_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n");
    String8 pair_log = string_format(arena, S8("CI_UNIT_PLAN_V1 source_revision={S8} binary_sha256={S8} workers=4 groups=2 group_workers=2 primary_module={S8}\n{S8}"
        "CI_UNIT_PARTITION_V1 groups=2 workers=4 elapsed_us=100 modules=1 assertions=2 passed=2 failed=0 status=pass\n"), source, digest, ci_unit_primary_module(), timing);
    String8 scheduled_log = string_format(arena, S8("CI_UNIT_SCHEDULE_PLAN_V1 source_revision={S8} binary_sha256={S8} workers=4 slots=2 slot_workers=2 modules=1\n{S8}"
        "CI_UNIT_SCHEDULE_V1 slots=2 workers=4 elapsed_us=90 modules=1 assertions=2 passed=2 failed=0 external=0 captured_bytes=0 status=pass\n"), source, digest, timing);
    CiUnitObservation pair_observed = ci_unit_observe(pair_log, false);
    CiUnitObservation scheduled_observed = ci_unit_observe(scheduled_log, true);
    result = result && ci_unit_observations_equal(&pair_observed, &scheduled_observed);
    scheduled_observed.proof.modules[1].assertions += 1;
    result = result && !ci_unit_observations_equal(&pair_observed, &scheduled_observed);
    scheduled_observed.proof.modules[1].assertions -= 1;
    scheduled_observed.digest = S8("wrong-binary");
    result = result && !ci_unit_observations_equal(&pair_observed, &scheduled_observed) &&
        !ci_unit_observe(string_format(arena, S8("{S8}{S8}"), pair_log, timing), false).valid;
    String8 fixture = S8("TEST_FIXTURE_START_V1 kind=fixture module=other_tests fixture=example index=0\n");
    result = result && ci_unit_fixtures_equal(fixture, fixture, &pair_observed.proof) &&
        !ci_unit_fixtures_equal(fixture, (String8){0}, &pair_observed.proof);
    u8 utf16[] = {0xff, 0xfe, 'a', 0, 'b', 0};
    result = result && string_equal(ci_unit_log_text(arena, (ByteSlice){utf16, sizeof(utf16)}), S8("ab")) &&
        !ci_unit_log_text(arena, (ByteSlice){utf16, sizeof(utf16) - 1}).length;
    result = ci_unit_schedule_run(arena, executable, 4, S8("self-test"), S8("self-test"), true) == PROCESS_RESULT_SUCCESS && result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_unit_self_test(void)
{
    TemporalArena scratch = scratch_begin(0, 0);
    String8 inventory = string_format(scratch.arena, S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected=1 group=primary\n"
        "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=0 group=rest\n"), ci_unit_primary_module());
    String8 execution = string_format(scratch.arena, S8("TEST_MODULE_TIMING index=0 module={S8} duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"
        "CI_UNIT_BATCH_V1 group=primary modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"
        "[2/2] Unit tests (1 of 2 modules selected)\n[1/1] Module tests\n[0/0] External tests\n"), ci_unit_primary_module());
    String8 full = string_format(scratch.arena, S8("{S8}{S8}"), inventory, execution);
    CiUnitProof valid = ci_unit_parse(full, true, false);
    bool result = valid.valid && !ci_unit_parse(full, false, false).valid && !ci_unit_parse(inventory, true, false).valid;
    String8 legacy_owners[] = {S8("driver"), S8("frontend")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(legacy_owners); i += 1)
    {
        String8 legacy = string_format(scratch.arena, S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected=1 group={S8}\n"
            "CI_UNIT_MODULE_V1 index=1 module=other_tests table_audit=0 enabled=1 selected=0 group=rest\n{S8}"),
            ci_unit_primary_module(), legacy_owners[i], execution);
        result = result && !ci_unit_parse(legacy, true, false).valid;
    }
    String8 missing_primary = string_format(scratch.arena, S8("CI_UNIT_MODULE_V1 index=0 module=other_tests table_audit=0 enabled=1 selected=1 group=primary\n{S8}"), execution);
    String8 duplicate_primary = string_format(scratch.arena, S8("CI_UNIT_MODULE_V1 index=0 module={S8} table_audit=0 enabled=1 selected=1 group=primary\n"
        "CI_UNIT_MODULE_V1 index=1 module={S8} table_audit=0 enabled=1 selected=1 group=primary\n{S8}"),
        ci_unit_primary_module(), ci_unit_primary_module(), execution);
    result = result && !ci_unit_parse(missing_primary, true, false).valid && !ci_unit_parse(duplicate_primary, true, false).valid;
    String8 rejected[] = {
        S8("CI_UNIT_BATCH_V1 group=primary modules=1 modules_passed=1 assertions=2 passed=2 failed=0 external=0 external_passed=0 status=pass\n"),
        S8("TEST_MODULE_TIMING index=0 module=compiler_driver_tests duration_ns=1 passed=2 failed=0 assertions=2 status=pass\n"),
        S8("[2/2] Unit tests (1 of 2 modules selected)\n"),
        S8("CI_UNIT_MODULE_V1 index=2 module=unexpected table_audit=0 enabled=1 selected=1 group=primary\n"),
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
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test-module-child")))
    {
        result = ci_unit_schedule_test_child();
    }
    else if (arguments.length == 3 && string_equal(arguments.pointer[0], S8("--compare")))
    {
        result = ci_unit_compare(arena, arguments.pointer[1], arguments.pointer[2]);
    }
    else if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("--self-test-child")))
    {
        result = ci_unit_self_test_child(arguments.pointer[1]);
    }
    else if (arguments.length == 2 && string_equal(arguments.pointer[0], S8("--self-test-fallback")))
    {
        String8 command[] = {executable, S8("test_units_partitioned"), S8("--self-test-child"), arguments.pointer[1]};
        result = ci_unit_serial_run(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
    }
    else if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test")))
    {
        bool valid = ci_unit_self_test();
        valid = ci_unit_process_self_test(arena, executable) && valid;
        valid = ci_unit_review_self_test(arena, executable) && valid;
        valid = ci_unit_schedule_self_test(arena, executable) && valid;
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
        String8 schedule = os_get_environment_variable(S8("BUSTER_TEST_UNIT_SCHEDULE"));
        // Preserve the existing qualification receipt protocol.
        String8 checks_evidence = os_get_environment_variable(S8("BUSTER_CI_CHECKS_EVIDENCE"));
        if (!schedule.length && checks_evidence.length && !string_equal(checks_evidence, S8("0"))) { schedule = S8("pair"); }
        bool valid_schedule = !schedule.length || string_equal(schedule, S8("modules")) || string_equal(schedule, S8("pair"));
        valid_jobs = valid_jobs && valid_schedule;
        bool grouped = valid_jobs && jobs_text.length && jobs.value >= 4 && !BUSTER_SINGLE_THREADED;
        if (path.length && valid_jobs && !grouped)
        {
            result = ci_unit_serial_run(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(command));
        }
        else if (path.length && grouped)
        {
            String8 supplied_revision = os_get_environment_variable(S8("BUSTER_TEST_SOURCE_REVISION"));
            String8 revision = {0}, digest = {0};
            bool valid = ci_unit_source_revision(arena, supplied_revision, &revision) && stage_object_sha256_file(arena, path, &digest);
            if (valid)
            {
                if (!string_equal(schedule, S8("pair")))
                {
                    result = ci_unit_schedule_run(arena, path, jobs.value, revision, digest, false);
                }
                else
                {
                    u64 epoch = os_now_microseconds();
                    u64 group_workers = jobs.value / 2;
                    string_print(S8("CI_UNIT_PLAN_V1 source_revision={S8} binary_sha256={S8} workers={u64} groups=2 group_workers={u64} primary_module={S8}\n"), revision, digest, jobs.value, group_workers, ci_unit_primary_module());
                    CiUnitWork work = {.timeout = CI_UNIT_TIMEOUT_US,
                        .audits = !string_equal(os_get_environment_variable(S8("BUSTER_TEST_TABLE_AUDITS")), S8("0"))};
                    bool admitted = true;
                    for (u64 i = 0; i < 2; i += 1)
                    {
                        CiUnitChild* child = work.children + i;
                        child->arena = arena_create((ArenaCreation){.reserved_size = BUSTER_MB(256), .flags = {.no_pool = 1}});
                        SliceString8 keys = {0}, values = {0};
                        ci_unit_environment(arena, i ? S8("rest") : S8("primary"), string_format(arena, S8("{u64}"), group_workers), &keys, &values);
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
                            i ? S8("rest") : S8("primary"), group_workers, child->start - epoch, child->finish - epoch, child->finish - child->start,
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
            }
            else { string_print(S8("error: invalid BUSTER_TEST_SOURCE_REVISION or unreadable test binary\n")); }
        }
        else { string_print(S8("error: invalid unit-test path or BUSTER_TEST_JOBS quota\n")); }
    }
    else { string_print(S8("usage: test_units_partitioned <ide-path> | --self-test\n")); }
    return result;
}
