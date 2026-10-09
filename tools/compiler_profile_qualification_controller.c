// Owned native controller for disabled sampling research. Include after the
// packet runner helpers. Entry: *_execute; *_phase owns one child lease and
// subreaper at a time; *_prepared binds the durable acquisition to the freeze.
// Acquisition claims before clone/build and never measures a compiler. Later
// packets reuse those exact artifacts. No process exit qualifies a campaign.
#ifndef BUSTER_COMPILER_SAMPLING_CONTROLLER_INCLUDED
#define BUSTER_COMPILER_SAMPLING_CONTROLLER_INCLUDED
#define BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT (8ull << 20)

typedef struct CompilerSamplingControllerTransport CompilerSamplingControllerTransport;
struct CompilerSamplingControllerTransport { String8 bytes[7]; bool present; bool valid; };

typedef struct CompilerSamplingControllerOptions CompilerSamplingControllerOptions;
struct CompilerSamplingControllerOptions
{
    CompilerSamplingOptions packet;
    String8 acquisition_plan;
    String8 cleanup_root;
    String8 trusted_root;
    CompilerSamplingAdmission admitted;
    CompilerSamplingAcquisitionPlan plan;
    CompilerSamplingFreeze frozen;
    CompilerSamplingFreeze parent;
    CompilerSamplingControllerTransport transport;
    String8 acquisition_sha256;
    String8 facts_text;
};

typedef struct CompilerSamplingController CompilerSamplingController;
struct CompilerSamplingController
{
    Arena* arena;
    CompilerSamplingOptions packet;
    CompilerSamplingAcquisitionPlan plan;
    String8 evidence;
    String8 prepared;
    String8 claim;
    String8 acquisition_sha256;
    String8 prepared_sha256;
    String8 last_output;
    String8List phases;
    u64 started;
    u64 deadline;
    u64 stage;
    bool claimed;
    bool success;
    bool cleanup_failed;
};

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_cancelled(void)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    result = process_control_atomic_load(&compiler_sampling_cancel_signal) != 0;
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_path_safe(String8 name)
{
    bool result = name.length > 0 && name.length <= 512 && !string_equal(name, S8(".")) && !string_equal(name, S8(".."));
    for (u64 i = 0; result && i < name.length; i += 1)
        result = name.pointer[i] >= 32 && name.pointer[i] <= 126 && name.pointer[i] != '/' && name.pointer[i] != '\\';
    return result;
}

#if BUSTER_LINUX && !BUSTER_ANDROID
BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_hash(Arena* arena, String8 path, String8* digest, struct stat* status)
{
    String8 terminated = string_duplicate_arena(arena, path, true);
    struct stat named = {0}, before = {0}, after = {0}, final = {0};
    bool valid = lstat((char*)terminated.pointer, &named) == 0 && S_ISREG(named.st_mode);
    int descriptor = valid ? open((char*)terminated.pointer, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    valid = valid && descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_size > 0 && (u64)before.st_size <= 536870912ull &&
        named.st_dev == before.st_dev && named.st_ino == before.st_ino && named.st_mode == before.st_mode &&
        named.st_size == before.st_size;
    Sha256 hash;
    sha256_init(&hash);
    u8 buffer[65536];
    u64 used = 0, started = os_now_microseconds();
    bool eof = false;
    while (valid && !eof)
    {
        valid = os_now_microseconds() - started < 30000000ull;
        ssize_t count = valid ? read(descriptor, buffer, sizeof(buffer)) : -1;
        if (count > 0)
        {
            used += (u64)count;
            valid = used <= (u64)before.st_size;
            if (valid) sha256_add(&hash, buffer, (u64)count);
        }
        else if (!count) eof = true;
        else valid = valid && errno == EINTR;
    }
    valid = valid && eof && fstat(descriptor, &after) == 0 &&
        lstat((char*)terminated.pointer, &final) == 0 &&
        before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
        before.st_size == after.st_size && used == (u64)before.st_size &&
        before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec &&
        after.st_dev == final.st_dev && after.st_ino == final.st_ino && after.st_mode == final.st_mode &&
        after.st_size == final.st_size;
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    if (valid)
    {
        char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digits);
        *digest = (String8){digits, SHA256_HEX_CAPACITY - 1};
        *status = after;
    }
    return valid;
}
#endif

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_controller_read(Arena* arena, String8 path, u64 limit)
{
    String8 result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 terminated = string_duplicate_arena(arena, path, true);
    int descriptor = open((char*)terminated.pointer, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat before = {0}, after = {0};
    bool valid = descriptor >= 0 && fstat(descriptor, &before) == 0 && S_ISREG(before.st_mode) &&
        before.st_size >= 0 && (u64)before.st_size <= limit && limit <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT;
    char8* bytes = valid ? arena_allocate(arena, char8, limit + 1) : 0;
    u64 used = 0;
    bool eof = false;
    while (valid && !eof)
    {
        ssize_t count = read(descriptor, bytes + used, limit + 1 - used);
        if (count > 0)
        {
            used += (u64)count;
            valid = used <= limit;
        }
        else if (!count) eof = true;
        else valid = errno == EINTR;
    }
    valid = valid && eof && fstat(descriptor, &after) == 0 && before.st_dev == after.st_dev &&
        before.st_ino == after.st_ino && before.st_size == after.st_size && used == (u64)after.st_size &&
        before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    if (valid) result = (String8){bytes, used};
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path); BUSTER_UNUSED(limit);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_controller_environment(String8 name, bool* present)
{
    String8 result = {0};
    u64 matches = 0;
    for (u64 i = 0; i < program_state->input.environment_keys.length; i += 1)
    {
        if (string_equal(program_state->input.environment_keys.pointer[i], name))
        {
            matches += 1;
            result = program_state->input.environment_values.pointer[i];
        }
    }
    *present = matches == 1;
    if (matches != 1) result = (String8){0};
    return result;
}

BUSTER_GLOBAL_LOCAL s32 compiler_sampling_controller_base64_digit(u8 byte)
{
    s32 result = -1;
    if (byte >= 'A' && byte <= 'Z') result = (s32)(byte - 'A');
    else if (byte >= 'a' && byte <= 'z') result = (s32)(byte - 'a') + 26;
    else if (byte >= '0' && byte <= '9') result = (s32)(byte - '0') + 52;
    else if (byte == '+') result = 62;
    else if (byte == '/') result = 63;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_base64(Arena* arena, String8 text, bool empty, String8* decoded)
{
    bool result = (!text.length && empty) || (text.pointer && text.length && text.length <= 65536 && text.length % 4 == 0);
    char8* bytes = result ? arena_allocate(arena, char8, text.length / 4 * 3 + 1) : 0;
    u64 used = 0;
    for (u64 i = 0; result && i < text.length; i += 4)
    {
        s32 a = compiler_sampling_controller_base64_digit(text.pointer[i]);
        s32 b = compiler_sampling_controller_base64_digit(text.pointer[i + 1]);
        s32 c = compiler_sampling_controller_base64_digit(text.pointer[i + 2]);
        s32 d = compiler_sampling_controller_base64_digit(text.pointer[i + 3]);
        bool pad_c = text.pointer[i + 2] == '=', pad_d = text.pointer[i + 3] == '=';
        result = a >= 0 && b >= 0 && (c >= 0 || pad_c) && (d >= 0 || pad_d) &&
            (!pad_c || pad_d) && (!(pad_c || pad_d) || i + 4 == text.length) &&
            (!pad_c || !(b & 15)) && (!pad_d || pad_c || !(c & 3));
        if (result)
        {
            bytes[used++] = (char8)((a << 2) | (b >> 4));
            if (!pad_c) bytes[used++] = (char8)(((b & 15) << 4) | (c >> 2));
            if (!pad_d) bytes[used++] = (char8)(((c & 3) << 6) | d);
        }
    }
    for (u64 i = 0; result && i < used; i += 1)
    {
        u8 byte = bytes[i];
        result = byte == '\t' || byte == '\n' || (byte >= 32 && byte <= 126);
    }
    result = result && ((!used && empty) || (used && bytes[used - 1] == '\n'));
    if (result) *decoded = (String8){bytes, used};
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingControllerTransport compiler_sampling_controller_transport(Arena* arena)
{
    CompilerSamplingControllerTransport result = {.valid = true};
    String8 names[] = {S8("BQ_SAMPLING_REQUEST_DATA"), S8("BQ_SAMPLING_FREEZE_DATA"),
        S8("BQ_SAMPLING_PARENT_FREEZE_DATA"), S8("BQ_SAMPLING_ACQUISITION_PLAN_DATA"),
        S8("BQ_SAMPLING_ALLOWLIST_DATA"), S8("BQ_SAMPLING_FACTS_DATA"), S8("BQ_SAMPLING_HISTORY_DATA")};
    u64 total = 0, present_count = 0, observed_count = 0;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        bool present = false;
        String8 value = compiler_sampling_controller_environment(names[i], &present);
        total += value.length;
        present_count += present ? 1 : 0;
        result.valid = result.valid && value.length <= 65536 && total <= 262144;
        if (present) result.valid = result.valid && compiler_sampling_controller_base64(arena, value, i == 2, &result.bytes[i]);
    }
    for (u64 key = 0; key < program_state->input.environment_keys.length; key += 1)
    {
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            observed_count += string_equal(program_state->input.environment_keys.pointer[key], names[i]) ? 1 : 0;
        }
    }
    result.present = observed_count != 0;
    result.valid = result.valid && observed_count == present_count && (!present_count || present_count == BUSTER_ARRAY_LENGTH(names));
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_controller_fact(String8 text, String8 name)
{
    String8 result = {0};
    bool found = false;
    for (u64 begin = 0; begin < text.length;)
    {
        u64 tab = begin, end = begin;
        while (end < text.length && text.pointer[end] != '\n') end += 1;
        while (tab < end && text.pointer[tab] != '\t') tab += 1;
        if (tab < end && string_equal(string_slice(text, begin, tab), name))
        {
            if (found) result = (String8){0};
            else result = string_slice(text, tab + 1, end);
            found = true;
        }
        begin = end + 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_environment_matches(String8 name, String8 expected, bool required)
{
    bool present = false;
    String8 value = compiler_sampling_controller_environment(name, &present);
    bool result = present ? string_equal(value, expected) : !required;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_attempt(Arena* arena, String8 facts, CompilerSamplingAdmission admitted,
    CompilerSamplingAcquisitionPlan plan)
{
    String8 names[] = {S8("GITHUB_RUN_ID"), S8("GITHUB_RUN_ATTEMPT"), S8("GITHUB_REPOSITORY"), S8("GITHUB_SHA"),
        S8("BQ_REQUEST_RUN_ID"), S8("BQ_REQUEST_ATTEMPT"), S8("BQ_HEAD_COMMIT"),
        S8("BQ_SAMPLING_PHASE"), S8("BQ_SAMPLING_PACKET"), S8("BQ_SAMPLING_FAMILY"),
        S8("BQ_SAMPLING_FREEZE_REVISION"), S8("BQ_SAMPLING_FREEZE_SHA256"), S8("BQ_SAMPLING_CAMPAIGN_PARENT"),
        S8("BQ_SAMPLING_PARENT_FREEZE_REVISION"), S8("BQ_SAMPLING_PROTOCOL_SHA256"),
        S8("BQ_SAMPLING_BASE"), S8("BQ_SAMPLING_BASE_TREE"), S8("BQ_SAMPLING_CANDIDATE_REVISION"), S8("BQ_SAMPLING_TRUSTED_REVISION")};
    String8 candidate = string_equal(admitted.family, S8("aa")) ? plan.base :
        string_equal(admitted.family, S8("ab2")) ? plan.ab2_revision : plan.ab1_revision;
    String8 values[] = {compiler_sampling_controller_fact(facts, S8("executor_run_id")), S8("1"), S8("buster14a/buster"),
        compiler_sampling_controller_fact(facts, S8("trusted_revision")), compiler_sampling_controller_fact(facts, S8("request_run_id")),
        S8("1"), compiler_sampling_controller_fact(facts, S8("request_head")), admitted.phase,
        string_format(arena, S8("{u64}"), admitted.packet), admitted.family, admitted.freeze_revision, admitted.freeze_sha256,
        admitted.campaign_parent, admitted.parent_freeze_revision, admitted.protocol_sha256,
        plan.base, plan.base_tree, candidate, admitted.trusted_revision};
    bool result = admitted.valid && plan.valid;
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        result = values[i].length && compiler_sampling_controller_environment_matches(names[i], values[i], true);
    }
    result = result && compiler_sampling_controller_environment_matches(S8("BQ_SAMPLING_RESERVATION_SECONDS"),
        string_format(arena, S8("{u64}"), admitted.reservation_seconds), false) &&
        compiler_sampling_controller_environment_matches(S8("BQ_SAMPLING_TIMEOUT_MINUTES"),
            string_format(arena, S8("{u64}"), admitted.reservation_seconds / 60), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_materialize(Arena* arena, String8 directory,
    CompilerSamplingControllerTransport transport, CompilerSamplingOptions* packet, String8* plan_path)
{
    OsDirectoryCreateResult created = os_make_directory_exclusive(directory);
    bool result = transport.valid && transport.present && created.created && !created.error.v;
    String8 names[] = {S8("request.txt"), S8("freeze.tsv"), S8("parent-freeze.tsv"), S8("acquisition-plan.tsv"),
        S8("allowlist.tsv"), S8("facts.tsv"), S8("history.tsv")};
    String8* paths[] = {&packet->request, &packet->freeze, &packet->parent_freeze, plan_path,
        &packet->allowlist, &packet->facts, &packet->history};
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(names); i += 1)
    {
        *paths[i] = path_join(arena, directory, names[i]);
        result = file_write(*paths[i], BUSTER_SLICE_TO_BYTE_SLICE(transport.bytes[i]));
    }
    return result;
}

typedef struct CompilerSamplingControllerHost CompilerSamplingControllerHost;
struct CompilerSamplingControllerHost { String8 model; u64 records; bool valid; };

BUSTER_GLOBAL_LOCAL CompilerSamplingControllerHost compiler_sampling_controller_cpu_parse(String8 text)
{
    CompilerSamplingControllerHost result = {0};
    bool valid = text.pointer && text.length && text.length <= 65536;
    u64 processors[4096] = {0}, processor = 0;
    bool have_processor = false, have_model = false, have_record = false;
    for (u64 begin = 0; valid && begin <= text.length;)
    {
        u64 end = begin;
        while (valid && end < text.length && text.pointer[end] != '\n')
        {
            u8 byte = text.pointer[end];
            valid = byte == '\t' || (byte >= 32 && byte <= 126);
            end += 1;
        }
        String8 line = string_slice(text, begin, end);
        if (!line.length || begin == text.length)
        {
            if (have_record)
            {
                valid = valid && have_processor && have_model && result.records < BUSTER_ARRAY_LENGTH(processors);
                for (u64 i = 0; valid && i < result.records; i += 1) valid = processors[i] != processor;
                if (valid) processors[result.records++] = processor;
            }
            have_processor = false; have_model = false; have_record = false;
        }
        else if (valid)
        {
            have_record = true;
            u64 colon = 0;
            while (colon < line.length && line.pointer[colon] != ':') colon += 1;
            valid = colon < line.length;
            if (valid)
            {
                String8 key = production_profile_trim(string_slice(line, 0, colon));
                String8 value = production_profile_trim(string_slice(line, colon + 1, line.length));
                if (string_equal(key, S8("processor")))
                {
                    valid = !have_processor && compiler_sampling_admission_decimal(value, &processor) && processor <= 65535;
                    have_processor = true;
                }
                else if (string_equal(key, S8("model name")))
                {
                    valid = !have_model && value.length && value.length <= 128;
                    for (u64 i = 0; valid && i < value.length; i += 1)
                    {
                        valid = value.pointer[i] >= 32 && value.pointer[i] <= 126 && value.pointer[i] != '"' && value.pointer[i] != '\\';
                    }
                    valid = valid && string_equal(value, S8("AMD Ryzen 7 9700X 8-Core Processor")) &&
                        (!result.model.length || string_equal(result.model, value));
                    if (valid) result.model = value;
                    have_model = true;
                }
            }
        }
        if (end == text.length)
        {
            if (have_record)
            {
                valid = valid && have_processor && have_model && result.records < BUSTER_ARRAY_LENGTH(processors);
                for (u64 i = 0; valid && i < result.records; i += 1) valid = processors[i] != processor;
                if (valid) processors[result.records++] = processor;
            }
            begin = text.length + 1;
        }
        else begin = end + 1;
    }
    result.valid = valid && result.records && result.model.length;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingControllerHost compiler_sampling_controller_observed_host(Arena* arena)
{
    CompilerSamplingControllerHost result = {0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    int descriptor = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char8* bytes = arena_allocate(arena, char8, 65537);
    u64 used = 0;
    bool valid = descriptor >= 0, eof = false;
    while (valid && !eof)
    {
        ssize_t count = read(descriptor, bytes + used, 65537 - used);
        if (count > 0)
        {
            used += (u64)count;
            valid = used <= 65536;
        }
        else if (!count) eof = true;
        else valid = errno == EINTR;
    }
    if (descriptor >= 0) valid = close(descriptor) == 0 && valid;
    if (valid && eof) result = compiler_sampling_controller_cpu_parse((String8){bytes, used});
#else
    BUSTER_UNUSED(arena);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_host_receipt(Arena* arena, String8 evidence,
    CompilerSamplingControllerHost host, CompilerSamplingControllerOptions resolved)
{
    String8 text = string_format(arena,
        S8("{{\"schema\":\"buster-main-sampling-host-v1\",\"state\":\"complete\",\"cpu_model\":\"{S8}\","
           "\"logical_processor_records\":{u64},\"observed_from\":\"/proc/cpuinfo\",\"request_head\":\"{S8}\","
           "\"run_id\":\"{S8}\",\"run_attempt\":\"1\",\"request_run_id\":\"{S8}\","
           "\"measurement_trusted_revision\":\"{S8}\",\"policy_trusted_revision\":\"{S8}\","
           "\"freeze_sha256\":\"{S8}\",\"acquisition_campaign\":\"{S8}\",\"protocol_sha256\":\"{S8}\"\n}\n"),
        host.model, host.records, resolved.packet.head, compiler_sampling_controller_fact(resolved.facts_text, S8("executor_run_id")),
        compiler_sampling_controller_fact(resolved.facts_text, S8("request_run_id")), resolved.admitted.trusted_revision,
        compiler_sampling_controller_fact(resolved.facts_text, S8("trusted_revision")),
        resolved.admitted.freeze_sha256, resolved.acquisition_sha256, resolved.admitted.protocol_sha256);
    bool result = host.valid && file_write(path_join(arena, evidence, S8("host.json")), BUSTER_SLICE_TO_BYTE_SLICE(text));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_flush(CompilerSamplingController* controller)
{
    String8 text = string_join_arena(controller->arena, string8_list_to_slice(controller->arena, controller->phases), false);
    bool result = text.length <= BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT &&
        file_write(path_join(controller->arena, controller->evidence, S8("controller.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(text));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_phase(CompilerSamplingController* controller, String8 name,
    SliceString8 arguments, u64 cap_us)
{
    bool attempted = controller->success && !controller->cleanup_failed && !compiler_sampling_controller_cancelled() &&
        controller->stage < 32 && os_now_microseconds() < controller->deadline;
    bool complete = false;
    u64 started = os_now_microseconds(), elapsed = 0;
    ProcessWaitResult wait = {.result = PROCESS_RESULT_UNKNOWN};
    CompilerExperimentSupervisor supervisor = {0};
    bool began = false, cleanup = false;
    controller->stage += 1;
    controller->last_output = (String8){0};
#if BUSTER_LINUX && !BUSTER_ANDROID
    if (attempted)
    {
        began = compiler_experiment_supervisor_begin(controller->arena, &supervisor);
        if (began)
        {
            ProcessGroupControlState control = {.cancellation_signal = &compiler_sampling_cancel_signal,
                .cancellation_escalated = &compiler_sampling_cancel_escalated};
            u64 remaining = controller->deadline > started ? controller->deadline - started : 1;
            if (remaining > cap_us) remaining = cap_us;
            ProcessSpawnResult spawn = os_process_spawn(arguments, (SliceString8){0}, (SliceString8){0},
                (ProcessSpawnOptions){.use_process_environment = 1, .search_path = 1, .new_process_group = 1,
                    .observe_resources = 1, .capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                    .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
                    .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = 1ull << 20,
                        [STANDARD_STREAM_ERROR] = 1ull << 20}, .total = 2ull << 20}});
            if (spawn.handle)
            {
                spawn.process_group_control = &control;
                wait = os_process_wait_deadline(controller->arena, spawn, remaining);
            }
            bool released = !wait.process_group_reservation_retained && !wait.process_group_ownership_lost;
            cleanup = released && compiler_experiment_supervisor_end(controller->arena, &supervisor) &&
                !wait.process_tree_cleanup_failed;
            complete = spawn.handle && wait.result == PROCESS_RESULT_SUCCESS && !wait.platform_status && !wait.timed_out &&
                cleanup && !wait.capture_failed && !wait.capture_limit_exceeded && !wait.output_truncated &&
                !supervisor.signalled && !supervisor.reaped && !compiler_sampling_controller_cancelled();
        }
        controller->cleanup_failed = controller->cleanup_failed || !began || !cleanup;
        elapsed = os_now_microseconds() - started;
        String8 stem = string_format(controller->arena, S8("controller-{u64}-{S8}"), controller->stage, name);
        bool logs = file_write(path_join(controller->arena, controller->evidence, string_format(controller->arena, S8("{S8}.stdout.log"), stem)),
            BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_OUTPUT])) &&
            file_write(path_join(controller->arena, controller->evidence, string_format(controller->arena, S8("{S8}.stderr.log"), stem)),
                BUSTER_SLICE_TO_BYTE_SLICE(wait.streams[STANDARD_STREAM_ERROR]));
        bool proof = compiler_sampling_supervision_receipt(controller->arena,
            path_join(controller->arena, controller->evidence, string_format(controller->arena, S8("{S8}-supervision.tsv"), stem)),
            supervisor, cleanup, elapsed);
        complete = complete && logs && proof && os_now_microseconds() <= controller->deadline;
        if (complete) controller->last_output = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]);
    }
#else
    BUSTER_UNUSED(arguments); BUSTER_UNUSED(cap_us); BUSTER_UNUSED(supervisor);
#endif
    string8_list_push(controller->arena, &controller->phases, string_format(controller->arena,
        S8("{u64}\t{S8}\t{u64}\t{u64}\t{u64}\t{u64}\t{u64}\t{S8}\n"),
        controller->stage, name, elapsed, (u64)wait.platform_status, (u64)wait.timed_out,
        (u64)(attempted && (!began || !cleanup)), compiler_sampling_controller_cancelled() ? 1ull : 0ull,
        complete ? S8("complete") : attempted ? S8("failed") : S8("not_run")));
    bool result = compiler_sampling_controller_flush(controller) && complete;
    controller->success = controller->success && result;
    return result;
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_sampling_controller_git(Arena* arena, SliceString8 tail)
{
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 prefix[] = {S8("git"), S8("-c"), S8("gc.auto=0"), S8("-c"), S8("maintenance.auto=false"),
        S8("-c"), S8("core.hooksPath=/dev/null")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(prefix); i += 1) os_argument_builder_append(&builder, prefix[i]);
    for (u64 i = 0; i < tail.length; i += 1) os_argument_builder_append(&builder, tail.pointer[i]);
    SliceString8 result = os_argument_builder_flush(&builder);
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_controller_tree(CompilerSamplingController* controller, String8 revision, String8 name)
{
    String8 arguments[] = {S8("-C"), controller->plan.source_root, S8("rev-parse"),
        string_format(controller->arena, S8("{S8}{S8}"), revision, S8("^{tree}"))};
    bool complete = compiler_sampling_controller_phase(controller, name,
        compiler_sampling_controller_git(controller->arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments)), 120000000ull);
    String8 result = complete ? production_profile_trim(controller->last_output) : (String8){0};
    controller->success = controller->success && compiler_sampling_hex(result, 40);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_binary(Arena* arena, String8 path, String8 expected_sha, String8 expected_bytes)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 actual = {0};
    struct stat status = {0};
    u64 bytes = 0;
    result = compiler_sampling_hex(expected_sha, 64) && compiler_sampling_freeze_bytes(expected_bytes) &&
        compiler_sampling_admission_decimal(expected_bytes, &bytes) &&
        compiler_sampling_controller_hash(arena, path, &actual, &status) && status.st_size > 0 &&
        (u64)status.st_size == bytes && (status.st_mode & 0111) && string_equal(actual, expected_sha);
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(path); BUSTER_UNUSED(expected_sha); BUSTER_UNUSED(expected_bytes);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_prepared(CompilerSamplingController* controller, CompilerSamplingFreeze freeze)
{
    String8 prepared = compiler_sampling_controller_read(controller->arena,
        path_join(controller->arena, controller->prepared, S8("prepared.json")), 65536);
    String8 actual = prepared.length ? stage_object_sha256_bytes(controller->arena, (u8*)prepared.pointer, prepared.length) : (String8){0};
    bool result = freeze.valid && string_equal(actual, freeze.prepared_sha256) &&
        compiler_sampling_controller_binary(controller->arena, path_join(controller->arena, controller->prepared, S8("bin/ide-base")),
            freeze.baseline_sha256, freeze.baseline_bytes) &&
        compiler_sampling_controller_binary(controller->arena, path_join(controller->arena, controller->prepared, S8("bin/ide-cand")),
            freeze.ab1_candidate_sha256, freeze.ab1_candidate_bytes) &&
        compiler_sampling_controller_binary(controller->arena, path_join(controller->arena, controller->prepared, S8("bin/ide-cand2")),
            freeze.ab2_candidate_sha256, freeze.ab2_candidate_bytes);
    if (result) controller->prepared_sha256 = actual;
    return result;
}

// Export bounded data only: no stored executable can enter the hosted reader.
BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_export(CompilerSamplingController* controller)
{
    String8 output = path_join(controller->arena, controller->evidence, S8("prepared"));
    OsDirectoryCreateResult created = os_make_directory_exclusive(output);
    bool result = !created.error.v && created.created;
    String8 required[] = {S8("prepared.json"), S8("prepared.manifest.tsv"), S8("prepared.workload.tsv"),
        S8("baseline.binary.json"), S8("candidate.binary.json"), S8("candidate2.binary.json"), S8("phases.tsv"),
        S8("baseline.CMakeCache.txt"), S8("candidate.CMakeCache.txt"), S8("candidate2.CMakeCache.txt"),
        S8("closure-snapshot.json"), S8("closure-restore.json"), S8("closure-verify.json"),
        S8("closure-snapshot.json.manifest.tsv"), S8("closure-restore.json.manifest.tsv"), S8("closure-verify.json.manifest.tsv")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(required); i += 1)
    {
        String8 bytes = compiler_sampling_controller_read(controller->arena,
            path_join(controller->arena, controller->prepared, required[i]), BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
        bool retained = bytes.length && file_write(path_join(controller->arena, output, required[i]), BUSTER_SLICE_TO_BYTE_SLICE(bytes));
        result = retained && result;
    }
    MuslDirectoryEntry* entries = 0;
    u64 count = 0;
    bool listed = musl_list_directory(controller->arena, controller->prepared, &entries, &count) && count <= 512;
    result = result && listed;
    for (u64 i = 0; listed && i < count; i += 1)
    {
        String8 name = {(char8*)entries[i].name.pointer, entries[i].name.length};
        bool proof = string_ends_with_sequence(name, S8(".cleanup.json")) ||
            string_ends_with_sequence(name, S8(".argv")) || string_ends_with_sequence(name, S8(".stdout")) ||
            string_ends_with_sequence(name, S8(".stderr"));
        if (proof && !entries[i].is_directory)
        {
            String8 bytes = compiler_sampling_controller_read(controller->arena,
                path_join(controller->arena, controller->prepared, name), BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
            bool retained = compiler_sampling_controller_path_safe(name) && bytes.length &&
                file_write(path_join(controller->arena, output, name), BUSTER_SLICE_TO_BYTE_SLICE(bytes));
            result = retained && result;
        }
    }
    String8 closure = compiler_sampling_controller_read(controller->arena,
        path_join(controller->arena, controller->prepared, S8("frozen-baseline/manifest.tsv")), BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
    result = result && closure.length && file_write(path_join(controller->arena, output, S8("closure.manifest.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(closure));
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_terminal(CompilerSamplingController* controller, bool complete, bool unchanged)
{
    u64 wall = os_now_microseconds() - controller->started;
    CompilerSamplingPacket packet = compiler_sampling_schedule(controller->packet.phase, controller->packet.packet);
    bool within = wall <= packet.reservation_seconds * 1000000ull;
    String8 terminal = string_format(controller->arena,
        S8("physical_packet_wall_us\t{u64}\nprep_us\t{u64}\ncaptured_input_files_unchanged\t{S8}\n"
           "within_reservation\t{S8}\nprocess_state\t{S8}\nqualification_state\tunvalidated\nqueue_delay\tunavailable\n"),
        wall, controller->packet.prep_us, unchanged ? S8("true") : S8("false"), within ? S8("true") : S8("false"),
        complete && within && unchanged && !controller->cleanup_failed ? S8("complete") : S8("failed"));
    bool result = file_write(path_join(controller->arena, controller->evidence, S8("packet.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal)) &&
        file_write(path_join(controller->arena, controller->claim, S8("terminal.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(terminal));
    if (!complete || !within || controller->cleanup_failed || !result)
    {
        file_write(path_join(controller->arena, path_parent(controller->arena, controller->claim), S8("exhausted.tsv")),
            BUSTER_SLICE_TO_BYTE_SLICE(S8("state\texhausted\nreason\tcontroller-failed-overrun-or-cleanup-unknown\n")));
    }
    return result && complete && within && unchanged && !controller->cleanup_failed;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_not_run(CompilerSamplingController* controller)
{
    String8List rows = {0};
    string8_list_push(controller->arena, &rows, S8("trial\tprofile\tfamily\tordinal\tpairs\twall_us\tuser_cpu_us\tsystem_cpu_us\tpeak_rss_bytes\tcpu_status\tmemory_status\texit_status\ttimed_out\tcleanup_failed\tcapture_failed\tclosure_before\tclosure_after\tstate\n"));
    CompilerSamplingPacket planned = compiler_sampling_schedule(controller->packet.phase, controller->packet.packet);
    for (u64 i = 0; i < planned.count; i += 1)
    {
        CompilerSamplingSlot slot = planned.slots[i];
        CompilerSamplingProfile profile = compiler_sampling_profile(slot.profile);
        string8_list_push(controller->arena, &rows, string_format(controller->arena,
            S8("{u64}\t{S8}\t{S8}\t{u64}\t{u64}\t0\t0\t0\t0\t0\t0\t0\t0\t{u64}\t0\t-\t-\tnot_run\n"),
            i, slot.profile, planned.family, slot.ordinal, profile.pairs, controller->cleanup_failed ? 1ull : 0ull));
    }
    String8 text = string_join_arena(controller->arena, string8_list_to_slice(controller->arena, rows), false);
    bool result = file_write(path_join(controller->arena, controller->evidence, S8("attempts.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(text));
    return result;
}

// Pure admission/path resolution before the outer worker starts. No file is
// created and no child is launched. The worker repeats this against its own
// platform environment before claiming or materializing any transported data.
BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_resolve(Arena* arena, CompilerSamplingOptions options,
    CompilerSamplingControllerOptions* output)
{
    CompilerSamplingControllerOptions result = {.packet = options, .acquisition_plan = options.acquisition_plan};
    result.transport = compiler_sampling_controller_transport(arena);
    CompilerSamplingControllerTransport transport = result.transport;
    String8 allowlist = transport.present ? transport.bytes[4] : compiler_sampling_controller_read(arena, options.allowlist, 16384);
    String8 request = transport.present ? transport.bytes[0] : compiler_sampling_controller_read(arena, options.request, 512);
    String8 facts = transport.present ? transport.bytes[5] : compiler_sampling_controller_read(arena, options.facts, 16384);
    String8 history = transport.present ? transport.bytes[6] : compiler_sampling_controller_read(arena, options.history, BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES);
    String8 freeze_text = transport.present ? transport.bytes[1] : compiler_sampling_controller_read(arena, options.freeze, BUSTER_SAMPLING_FREEZE_MAX_BYTES);
    String8 parent_text = transport.present ? transport.bytes[2] : compiler_sampling_controller_read(arena, options.parent_freeze, BUSTER_SAMPLING_FREEZE_MAX_BYTES);
    String8 plan_text = transport.present ? transport.bytes[3] : compiler_sampling_controller_read(arena, options.acquisition_plan, BUSTER_SAMPLING_FREEZE_MAX_BYTES);
    result.admitted = compiler_sampling_admission_validate(arena, allowlist, request, facts, history, freeze_text, parent_text);
    result.frozen = compiler_sampling_freeze_parse(freeze_text);
    result.parent = compiler_sampling_freeze_parse(parent_text);
    result.plan = compiler_sampling_acquisition_plan_parse(plan_text);
    result.facts_text = facts;
    bool acquire = string_equal(result.admitted.phase, S8("acquire"));
    result.acquisition_sha256 = plan_text.length ? stage_object_sha256_bytes(arena, (u8*)plan_text.pointer, plan_text.length) : (String8){0};
    String8 expected_plan = acquire ? result.admitted.freeze_sha256 :
        string_equal(result.admitted.phase, S8("pilot")) ? result.frozen.campaign_parent : result.parent.campaign_parent;
    bool temp_present = false, workspace_present = false;
    String8 temp_label = compiler_sampling_controller_environment(S8("RUNNER_TEMP"), &temp_present);
    String8 workspace_label = compiler_sampling_controller_environment(S8("GITHUB_WORKSPACE"), &workspace_present);
    String8 cleanup = temp_present ? os_path_absolute(arena, temp_label, true) : (String8){0};
    String8 workspace = workspace_present ? os_path_absolute(arena, workspace_label, true) : (String8){0};
    String8 trusted = os_path_absolute(arena, options.trusted_root, true);
    String8 evidence = os_path_absolute_lexical(arena, options.output, true);
    String8 store = os_path_absolute(arena, result.plan.store_root, true);
    String8 source_parent = os_path_absolute(arena, path_parent(arena, result.plan.source_root), true);
    String8 driver = os_path_absolute(arena, program_state->input.arguments.pointer[0], true);
    String8 ledger = path_join(arena, store, S8("ledger"));
    String8 admission_directory = string_format(arena, S8("{S8}.admission"), evidence);
    bool valid = transport.valid && result.admitted.valid && result.plan.valid &&
        string_equal(result.acquisition_sha256, expected_plan) &&
        string_equal(result.plan.trusted_revision, result.admitted.trusted_revision) &&
        string_equal(result.plan.protocol_sha256, result.admitted.protocol_sha256) &&
        compiler_sampling_controller_attempt(arena, facts, result.admitted, result.plan) &&
        cleanup.length && workspace.length && trusted.length && evidence.length && driver.length &&
        string_equal(cleanup, os_path_absolute(arena, options.cleanup_root, true)) &&
        string_equal(store, result.plan.store_root) &&
        string_equal(source_parent, path_parent(arena, result.plan.source_root)) &&
        compiler_sampling_acquisition_plan_store_outside(result.plan, cleanup) &&
        compiler_sampling_acquisition_plan_store_outside(result.plan, workspace) &&
        !compiler_sampling_acquisition_path_within(result.plan.source_root, cleanup) &&
        !compiler_sampling_acquisition_path_within(result.plan.source_root, workspace) &&
        !compiler_sampling_path_overlap(store, trusted) && !compiler_sampling_path_overlap(result.plan.source_root, trusted) &&
        compiler_sampling_acquisition_path_within(evidence, cleanup) &&
        generate_path_kind(arena, evidence) == GENERATE_PATH_MISSING &&
        string_equal(os_path_absolute(arena, path_parent(arena, evidence), true), path_parent(arena, evidence)) &&
        (generate_path_kind(arena, ledger) == GENERATE_PATH_MISSING || string_equal(os_path_absolute(arena, ledger, true), ledger)) &&
        (acquire || (string_equal(os_path_absolute(arena, result.plan.source_root, true), result.plan.source_root) &&
            string_equal(os_path_absolute(arena, path_join(arena, path_join(arena, store, result.acquisition_sha256), S8("prepared")), true),
                path_join(arena, path_join(arena, store, result.acquisition_sha256), S8("prepared"))))) &&
        (!transport.present || generate_path_kind(arena, admission_directory) == GENERATE_PATH_MISSING) &&
        (!options.phase.length || string_equal(options.phase, result.admitted.phase)) &&
        (!options.packet_text.length || string_equal(options.packet_text, string_format(arena, S8("{u64}"), result.admitted.packet)));
    result.cleanup_root = cleanup;
    result.trusted_root = trusted;
    result.packet.phase = result.admitted.phase;
    result.packet.packet = result.admitted.packet;
    result.packet.packet_text = string_format(arena, S8("{u64}"), result.admitted.packet);
    result.packet.freeze_sha256 = result.admitted.freeze_sha256;
    result.packet.ledger_root = ledger;
    result.packet.output = evidence;
    result.packet.source = result.plan.source_root;
    result.packet.base = result.plan.base;
    result.packet.base_tree = result.plan.base_tree;
    result.packet.head = compiler_sampling_controller_fact(facts, S8("request_head"));
    result.packet.trusted_revision = result.admitted.trusted_revision;
    result.packet.campaign_parent = result.admitted.campaign_parent;
    result.packet.campaign_parent_revision = result.admitted.parent_freeze_revision;
    result.packet.driver = driver;
    result.packet.lab = path_join(arena, trusted, S8("tools/uarch_lab.py"));
    result.packet.protocol = path_join(arena, trusted, S8("docs/compiler-main-sampling-qualification-v1.json"));
    result.packet.python = os_path_absolute(arena, executable_resolve_in_path(arena, S8("python3")), true);
    result.packet.candidate_revision = string_equal(result.admitted.family, S8("aa")) ? result.plan.base :
        string_equal(result.admitted.family, S8("ab2")) ? result.plan.ab2_revision : result.plan.ab1_revision;
    result.packet.prepared = path_join(arena, path_join(arena, path_join(arena, store, result.acquisition_sha256), S8("prepared")), S8("prepared.json"));
    result.packet.prep_us = 0;
    result.packet.prep_text = S8("0");
    result.packet.valid = valid;
    if (valid) *output = result;
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_controller_execute(Arena* arena, CompilerSamplingControllerOptions options)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    u64 started = os_now_microseconds();
    CompilerSamplingControllerOptions resolved = {0};
    CompilerSamplingOptions requested = options.packet;
    requested.acquisition_plan = options.acquisition_plan;
    requested.cleanup_root = options.cleanup_root;
    requested.trusted_root = options.trusted_root;
    bool valid = compiler_sampling_controller_resolve(arena, requested, &resolved);
    CompilerSamplingController controller = {.arena = arena, .packet = resolved.packet, .plan = resolved.plan, .started = started};
    CompilerSamplingAdmission admitted = resolved.admitted;
    CompilerSamplingFreeze freeze = resolved.frozen;
    CompilerSamplingControllerTransport transport = resolved.transport;
    bool acquire = string_equal(admitted.phase, S8("acquire"));
    String8 evidence = resolved.packet.output, ledger = resolved.packet.ledger_root;
    String8 trusted = resolved.trusted_root, store = resolved.plan.store_root, driver = resolved.packet.driver;
    String8 plan_sha = resolved.acquisition_sha256;
    String8 admission_directory = string_format(arena, S8("{S8}.admission"), evidence);
    SliceString8 child_keys = {0}, child_values = {0};
    CompilerSamplingControllerHost observed = compiler_sampling_controller_observed_host(arena);
    valid = valid && compiler_sampling_owned_environment(arena, &child_keys, &child_values) && observed.valid;
    controller.evidence = evidence;
    controller.acquisition_sha256 = plan_sha;
    controller.prepared = path_join(arena, path_join(arena, store, plan_sha), S8("prepared"));
    controller.deadline = controller.started + admitted.reservation_seconds * 1000000ull;
    if (admitted.reservation_seconds > 120) controller.deadline -= 120000000ull;
    if (valid)
    {
        valid = compiler_sampling_ledger_claim(arena, ledger, admitted.freeze_sha256, admitted.phase, admitted.packet, &controller.claim);
        controller.claimed = valid;
        if (valid && transport.present) valid = compiler_sampling_controller_materialize(arena, admission_directory,
            transport, &controller.packet, &options.acquisition_plan);
        OsDirectoryCreateResult created = valid ? os_make_directory_exclusive(evidence) : (OsDirectoryCreateResult){0};
        valid = valid && created.created && !created.error.v;
        String8 claim = compiler_sampling_claim_record(arena, controller.packet);
        valid = valid && file_write(path_join(arena, evidence, S8("claim.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(claim)) &&
            compiler_sampling_controller_host_receipt(arena, evidence, observed, resolved);
    }
    controller.success = valid;
    if (controller.claimed)
    {
        string8_list_push(arena, &controller.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        CompilerSamplingSignalScope signals = {0};
        bool deferred = compiler_sampling_signals_begin(&signals);
        controller.success = controller.success && deferred;
        String8 trusted_pin[] = {S8("-C"), trusted, S8("rev-parse"), S8("HEAD")};
        compiler_sampling_controller_phase(&controller, S8("trusted-harness-pin"),
            compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(trusted_pin)), 120000000ull);
        controller.success = controller.success && string_equal(production_profile_trim(controller.last_output), admitted.trusted_revision);
        String8 lab_sha = {0}, python_sha = {0}, driver_sha = {0}, protocol_sha = {0};
        controller.success = controller.success && stage_object_sha256_file(arena, controller.packet.lab, &lab_sha) &&
            stage_object_sha256_file(arena, controller.packet.python, &python_sha) && stage_object_sha256_file(arena, driver, &driver_sha) &&
            stage_object_sha256_file(arena, controller.packet.protocol, &protocol_sha) && string_equal(protocol_sha, admitted.protocol_sha256);
        if (acquire)
        {
            OsDirectoryCreateResult owned = controller.success ? os_make_directory_exclusive(path_parent(arena, controller.prepared)) : (OsDirectoryCreateResult){0};
            controller.success = controller.success && owned.created && !owned.error.v &&
                generate_path_kind(arena, controller.plan.source_root) == GENERATE_PATH_MISSING;
            String8 clone[] = {S8("clone"), S8("--no-checkout"), S8("--no-tags"),
                S8("https://github.com/buster14a/buster.git"), controller.plan.source_root};
            compiler_sampling_controller_phase(&controller, S8("clone-sources"),
                compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clone)), 120000000ull);
            String8 fetch[] = {S8("-C"), controller.plan.source_root, S8("fetch"), S8("--no-tags"),
                S8("origin"), controller.plan.base, controller.plan.ab1_revision, controller.plan.ab2_revision};
            compiler_sampling_controller_phase(&controller, S8("fetch-pinned-arms"),
                compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(fetch)), 120000000ull);
            String8 base_tree = compiler_sampling_controller_tree(&controller, controller.plan.base, S8("baseline-tree"));
            String8 ab1_tree = compiler_sampling_controller_tree(&controller, controller.plan.ab1_revision, S8("ab1-tree"));
            String8 ab2_tree = compiler_sampling_controller_tree(&controller, controller.plan.ab2_revision, S8("ab2-tree"));
            controller.success = controller.success && string_equal(base_tree, controller.plan.base_tree);
            String8 checkout[] = {S8("-C"), controller.plan.source_root, S8("checkout"), S8("--detach"), controller.plan.ab1_revision};
            compiler_sampling_controller_phase(&controller, S8("primary-arm-checkout"),
                compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(checkout)), 120000000ull);
            String8 prepare[] = {driver, S8("compiler_closure"), S8("prepare"), controller.plan.source_root, controller.prepared,
                S8("snapshot-v1"), controller.plan.base, base_tree, controller.plan.ab1_revision, ab1_tree,
                controller.plan.ab2_revision, ab2_tree};
            compiler_sampling_controller_phase(&controller, S8("acquire-prepared-closure"),
                (SliceString8)BUSTER_ARRAY_TO_SLICE(prepare), admitted.reservation_seconds * 1000000ull);
            OsDirectoryCreateResult execution = controller.success ? os_make_directory_exclusive(path_join(arena, controller.claim, S8("execution"))) : (OsDirectoryCreateResult){0};
            controller.success = controller.success && execution.created && !execution.error.v;
            controller.packet.prep_us = os_now_microseconds() - controller.started;
            bool exported = compiler_sampling_controller_export(&controller);
            controller.success = controller.success && exported;
            String8 prepared_bytes = compiler_sampling_controller_read(arena, path_join(arena, controller.prepared, S8("prepared.json")), 65536);
            controller.prepared_sha256 = prepared_bytes.length ? stage_object_sha256_bytes(arena, (u8*)prepared_bytes.pointer, prepared_bytes.length) : (String8){0};
            String8 acquired = string_format(arena,
                S8("schema\tbuster-main-sampling-acquisition-receipt-v1\nphase\tacquire\npacket\t0\nmeasurement\tfalse\n"
                   "campaign\t{S8}\nbase\t{S8}\nbase_tree\t{S8}\nrequest_head\t{S8}\ntrusted_revision\t{S8}\n"
                   "ab1_revision\t{S8}\nab2_revision\t{S8}\nprotocol_sha256\t{S8}\nlab_sha256\t{S8}\npython_sha256\t{S8}\n"
                   "driver_sha256\t{S8}\nprepared_sha256\t{S8}\nreservation_seconds\t1800\nprocess_state\t{S8}\nqualification_state\tunvalidated\n"),
                plan_sha, controller.plan.base, controller.plan.base_tree, controller.plan.request_head, admitted.trusted_revision,
                controller.plan.ab1_revision, controller.plan.ab2_revision, protocol_sha, lab_sha, python_sha, driver_sha,
                controller.prepared_sha256, controller.success ? S8("complete") : S8("failed"));
            controller.success = file_write(path_join(arena, evidence, S8("acquisition.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(acquired)) && controller.success;
            if (controller.success) controller.success = file_write(path_join(arena, path_parent(arena, controller.prepared), S8("acquisition.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(acquired));
        }
        else
        {
            controller.success = controller.success && compiler_sampling_controller_prepared(&controller, freeze) &&
                string_equal(freeze.lab_sha256, lab_sha) && string_equal(freeze.python_sha256, python_sha) &&
                string_equal(freeze.driver_sha256, driver_sha) && string_equal(freeze.protocol_sha256, protocol_sha);
            controller.packet.baseline = path_join(arena, controller.prepared, S8("bin/ide-base"));
            controller.packet.candidate = string_equal(admitted.family, S8("aa")) ? controller.packet.baseline :
                path_join(arena, controller.prepared, string_equal(admitted.family, S8("ab1")) ? S8("bin/ide-cand") : S8("bin/ide-cand2"));
            controller.packet.closure = path_join(arena, controller.prepared, S8("frozen-baseline"));
            controller.packet.closure_sha256 = freeze.closure_sha256;
            controller.success = controller.success && compiler_sampling_controller_export(&controller);
            CompilerSamplingVerification before = controller.success ? compiler_sampling_closure_verify(arena, controller.packet, evidence, 99, false, controller.deadline) : (CompilerSamplingVerification){0};
            controller.success = controller.success && before.valid;
            controller.cleanup_failed = controller.cleanup_failed || before.cleanup_failed;
            String8 corpus[] = {path_join(arena, controller.plan.source_root, S8("build/throughput-tools/throughput")), S8("run"),
                S8("--baseline"), controller.packet.baseline, S8("--candidate"), controller.packet.candidate,
                S8("--output"), path_join(arena, evidence, S8("throughput")), S8("--baseline-id"), controller.plan.base,
                S8("--candidate-id"), controller.packet.candidate_revision, S8("--profile"), S8("ci"), S8("--mode"), S8("all"),
                S8("--pairs"), S8("20"), S8("--warmups"), S8("2"), S8("--timeout"), S8("120"), S8("--cpu"), S8("2")};
            SliceString8 corpus_arguments = (SliceString8)BUSTER_ARRAY_TO_SLICE(corpus);
            compiler_sampling_controller_phase(&controller, S8("full-default-corpus"), corpus_arguments,
                admitted.reservation_seconds * 1000000ull);
            CompilerSamplingVerification after = controller.success ? compiler_sampling_closure_verify(arena, controller.packet, evidence, 99, true, controller.deadline) : (CompilerSamplingVerification){0};
            controller.success = controller.success && after.valid && compiler_sampling_controller_prepared(&controller, freeze);
            controller.cleanup_failed = controller.cleanup_failed || after.cleanup_failed;
            controller.packet.prep_us = os_now_microseconds() - controller.started;
            if (controller.success) controller.success = compiler_sampling_run(arena, controller.packet) == PROCESS_RESULT_SUCCESS;
            else compiler_sampling_controller_not_run(&controller);
        }
        bool restored = deferred && compiler_sampling_signals_end(&signals);
        controller.success = controller.success && restored && !compiler_sampling_controller_cancelled();
        bool unchanged = acquire ? controller.success : compiler_sampling_controller_prepared(&controller, freeze);
        if (compiler_sampling_controller_terminal(&controller, controller.success, unchanged)) result = PROCESS_RESULT_SUCCESS;
    }
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(options);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_controller_self_test(Arena* arena)
{
    String8 tail[] = {S8("-C"), S8("/fixture/source"), S8("rev-parse"), S8("HEAD")};
    SliceString8 arguments = compiler_sampling_controller_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(tail));
    bool result = arguments.length == 11 && string_equal(arguments.pointer[0], S8("git")) &&
        string_equal(arguments.pointer[2], S8("gc.auto=0")) && string_equal(arguments.pointer[4], S8("maintenance.auto=false")) &&
        string_equal(arguments.pointer[6], S8("core.hooksPath=/dev/null")) && string_equal(arguments.pointer[10], S8("HEAD"));
    String8 cpu = S8("processor\t: 0\nmodel name\t: AMD Ryzen 7 9700X 8-Core Processor\n\n"
        "processor\t: 1\nmodel name\t: AMD Ryzen 7 9700X 8-Core Processor\n\n");
    CompilerSamplingControllerHost host = compiler_sampling_controller_cpu_parse(cpu);
    result = result && host.valid && host.records == 2 &&
        string_equal(host.model, S8("AMD Ryzen 7 9700X 8-Core Processor"));
    String8 bad_cpu[] = {S8("processor: 0\n\n"),
        S8("model name: AMD Ryzen 7 9700X 8-Core Processor\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\n\nprocessor: 1\nmodel name: other CPU\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\"\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\\\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\r\n\n"),
        S8("processor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\n\nprocessor: 0\nmodel name: AMD Ryzen 7 9700X 8-Core Processor\n\n")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_cpu); i += 1)
    {
        result = result && !compiler_sampling_controller_cpu_parse(bad_cpu[i]).valid;
    }
    String8 decoded = {0};
    result = result && compiler_sampling_controller_base64(arena, S8("eAo="), false, &decoded) &&
        string_equal(decoded, S8("x\n")) && compiler_sampling_controller_base64(arena, S8(""), true, &decoded) &&
        !compiler_sampling_controller_base64(arena, S8(""), false, &decoded);
    String8 invalid[] = {S8("eAo"), S8("eAp="), S8("eAo=\n"), S8("eAo=AAAA"), S8("eA=="), S8("AAo="),
        S8("eA0K"), S8("===="), S8("eAo-")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        result = result && !compiler_sampling_controller_base64(arena, invalid[i], false, &decoded);
    }
#if BUSTER_LINUX && !BUSTER_ANDROID
    String8 directory = {0};
    bool owned = summary_self_test_claim_directory(arena, S8("sampling-controller"), &directory);
    result = result && owned;
    if (owned)
    {
        CompilerSamplingController fixture = {.arena = arena, .evidence = directory,
            .started = os_now_microseconds(), .deadline = os_now_microseconds() + 5000000ull, .success = true};
        string8_list_push(arena, &fixture.phases, S8("stage\tphase\twall_us\texit_status\ttimed_out\tcleanup_failed\tcancelled\tstate\n"));
        String8 fake_preparation[] = {S8("/bin/sh"), S8("-c"), S8("printf native-prepared-fixture")};
        bool prepared = compiler_sampling_controller_phase(&fixture, S8("fake-preparation"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(fake_preparation), 2000000ull);
        bool output = prepared && string_equal(fixture.last_output, S8("native-prepared-fixture"));
        String8 fake_corpus[] = {S8("/bin/sh"), S8("-c"), S8("exit 7")};
        bool corpus = compiler_sampling_controller_phase(&fixture, S8("fake-corpus"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(fake_corpus), 2000000ull);
        bool skipped = !compiler_sampling_controller_phase(&fixture, S8("not-run-after-failed-corpus"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(fake_preparation), 2000000ull);
        result = result && output && !corpus && skipped && !fixture.success && !fixture.cleanup_failed;
        fixture.success = true;
        fixture.deadline = os_now_microseconds() + 5000000ull;
        String8 timed[] = {S8("/bin/sh"), S8("-c"), S8("sleep 1")};
        bool timeout = !compiler_sampling_controller_phase(&fixture, S8("real-manager-timeout"),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(timed), 20000ull);
        result = result && timeout && !fixture.cleanup_failed && !fixture.success;
        String8 binary_directory = path_join(arena, directory, S8("bin"));
        OsDirectoryCreateResult bin = os_make_directory_exclusive(binary_directory);
        String8 names[] = {S8("ide-base"), S8("ide-cand"), S8("ide-cand2")};
        String8 contents[] = {S8("baseline-fixture\n"), S8("ab1-binary-fixture\n"), S8("ab2-different-fixture\n")};
        CompilerSamplingFreeze bound = {.valid = true};
        String8* hashes[] = {&bound.baseline_sha256, &bound.ab1_candidate_sha256, &bound.ab2_candidate_sha256};
        String8* sizes[] = {&bound.baseline_bytes, &bound.ab1_candidate_bytes, &bound.ab2_candidate_bytes};
        bool binaries = bin.created && !bin.error.v;
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(names); i += 1)
        {
            String8 path = path_join(arena, binary_directory, names[i]);
            bool created = file_write(path, BUSTER_SLICE_TO_BYTE_SLICE(contents[i])) && chmod((char*)path.pointer, 0755) == 0;
            *hashes[i] = stage_object_sha256_bytes(arena, (u8*)contents[i].pointer, contents[i].length);
            *sizes[i] = string_format(arena, S8("{u64}"), contents[i].length);
            binaries = binaries && created;
        }
        String8 metadata = S8("{\"native_private_fixture\":true}\n");
        binaries = binaries && file_write(path_join(arena, directory, S8("prepared.json")), BUSTER_SLICE_TO_BYTE_SLICE(metadata));
        bound.prepared_sha256 = stage_object_sha256_bytes(arena, (u8*)metadata.pointer, metadata.length);
        fixture.prepared = directory;
        result = result && binaries && compiler_sampling_controller_prepared(&fixture, bound);
        String8 saved = bound.ab2_candidate_bytes;
        bound.ab2_candidate_bytes = S8("1");
        result = result && !compiler_sampling_controller_prepared(&fixture, bound);
        bound.ab2_candidate_bytes = saved;
        bound.prepared_sha256 = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        result = result && !compiler_sampling_controller_prepared(&fixture, bound);
        bool removed = os_directory_delete(directory);
        result = result && removed;
    }
#endif
    return result;
}
#endif // BUSTER_COMPILER_SAMPLING_CONTROLLER_INCLUDED
