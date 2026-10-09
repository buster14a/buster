// Read-only historical #3211 prerequisites. Include after approved sampling
// history, preparation/Utility admission and MAIN field/read helpers.
// Entry: compiler_prerequisite_historical_validate and *_main. Map: *_packet
// keeps committed native plan policy; *_paths checks only declared lexical
// identities; the shared ordered original API proof binds immutable attempts.
// This module never observes a current host, admits work or executes sources.
// The trusted adapter independently obtains original Pi/Fi bytes, ancestry,
// associated PR membership and every-parent freshness. ZIP replay separately
// validates physical completion and science; a check title is not that proof.
// Live preparation/Utility admission remains OPEN-only and unchanged.
#ifndef BUSTER_COMPILER_PREREQUISITE_HISTORICAL_VALIDATION_INCLUDED
#define BUSTER_COMPILER_PREREQUISITE_HISTORICAL_VALIDATION_INCLUDED

typedef struct CompilerPrerequisiteHistoricalValidation CompilerPrerequisiteHistoricalValidation;
struct CompilerPrerequisiteHistoricalValidation
{
    CompilerPreparationAdmission preparation;
    CompilerClosureUtilityAdmission utility;
    String8 policy_revision;
    String8 api_sha256;
    String8 executor_conclusion;
    String8 check_conclusion;
    String8 check_title;
    bool is_utility;
    bool valid;
};

BUSTER_GLOBAL_LOCAL String8 compiler_prerequisite_historical_fact_names[] = {
    S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
    S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
    S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"),
    S8("triggering_login"), S8("triggering_id"), S8("pull_author_login"), S8("pull_author_id"),
    S8("request_repository"), S8("request_head_repository"), S8("pull_repository"), S8("pull_state"),
    S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")
};

// These are committed canonical strings, not current filesystem observations.
// No invented cleanup/workspace root can become historical execution authority.
BUSTER_GLOBAL_LOCAL bool compiler_prerequisite_historical_paths(String8 source, String8 output,
    String8 trusted, bool utility)
{
    String8 paths[] = {source, output, trusted};
    bool valid = true;
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        valid = compiler_sampling_acquisition_path(paths[i]);
        for (u64 j = i + 1; valid && j < BUSTER_ARRAY_LENGTH(paths); j += 1)
        {
            valid = !compiler_sampling_acquisition_path_within(paths[i], paths[j]) &&
                !compiler_sampling_acquisition_path_within(paths[j], paths[i]);
        }
    }
    String8 qualified[] = {S8("/tmp/buster-3211-closure-source"), S8("/tmp/buster-3211-closure-output")};
    for (u64 i = 0; valid && utility && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        for (u64 j = 0; valid && j < BUSTER_ARRAY_LENGTH(qualified); j += 1)
        {
            valid = !compiler_sampling_acquisition_path_within(paths[i], qualified[j]) &&
                !compiler_sampling_acquisition_path_within(qualified[j], paths[i]);
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL CompilerPrerequisiteHistoricalValidation compiler_prerequisite_historical_packet(Arena* arena,
    bool utility, String8 allowlist, String8 marker, String8 facts_text, String8 history, String8 plan_text)
{
    CompilerPrerequisiteHistoricalValidation result = {.is_utility = utility};
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"),
        S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config[PREPARATION_CONFIG_COUNT] = {0};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = allowlist.pointer && allowlist.length && allowlist.length <= 16384 &&
        facts_text.pointer && facts_text.length && facts_text.length <= 16384 &&
        marker.pointer && marker.length && marker.length <= 512 &&
        history.pointer && history.length && history.length <= BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES &&
        plan_text.pointer && plan_text.length && plan_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES &&
        compiler_sampling_admission_fields(allowlist, (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names), config) &&
        string_equal(config[PREPARATION_CONFIG_SCHEMA], utility ?
            S8("buster-compiler-closure-utility-admission-v1") : S8("buster-compiler-preparation-admission-v1")) &&
        string_equal(config[PREPARATION_CONFIG_STATE], utility ? S8("utility") : S8("qualify")) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_FREEZE_REVISION], 40) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_FREEZE_SHA], 64) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_PROTOCOL_SHA], 64) &&
        compiler_sampling_admission_timestamp(config[PREPARATION_CONFIG_HISTORY_SINCE]) &&
        string_equal(config[PREPARATION_CONFIG_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(config[PREPARATION_CONFIG_OWNER_LOGIN], S8("davidgmbb")) &&
        string_equal(config[PREPARATION_CONFIG_OWNER_ID], S8("39247043"));
    CompilerPreparationPlan preparation = valid && !utility ?
        compiler_preparation_plan_parse(plan_text) : (CompilerPreparationPlan){0};
    CompilerClosureUtilityPlan closure = valid && utility ?
        compiler_closure_utility_plan_parse(plan_text) : (CompilerClosureUtilityPlan){0};
    String8 protocol = utility ? closure.protocol_sha256 : preparation.protocol_sha256;
    String8 trusted = utility ? closure.trusted_revision : preparation.trusted_revision;
    valid = valid && (utility ? closure.valid : preparation.valid) &&
        compiler_prerequisite_historical_paths(utility ? closure.source_root : preparation.source_root,
            utility ? closure.output_root : preparation.output_root,
            utility ? closure.trusted_root : preparation.trusted_root, utility) &&
        string_equal(protocol, config[PREPARATION_CONFIG_PROTOCOL_SHA]) &&
        compiler_sampling_historical_digest(arena, plan_text, config[PREPARATION_CONFIG_FREEZE_SHA]) &&
        compiler_sampling_admission_fields(facts_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names), facts) &&
        string_equal(facts[SAMPLING_FACT_SCHEMA], S8("buster-main-sampling-github-facts-v1")) &&
        (string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("open")) ||
            string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("closed"))) &&
        compiler_sampling_hex(facts[SAMPLING_FACT_REQUEST_HEAD], 40) &&
        compiler_sampling_hex(facts[SAMPLING_FACT_TRUSTED_REVISION], 40) &&
        string_equal(facts[SAMPLING_FACT_REQUEST_ATTEMPT], S8("1")) &&
        string_equal(facts[SAMPLING_FACT_EXECUTOR_ATTEMPT], S8("1"));
    u64 request_run = 0, executor_run = 0, parent_count = 0;
    valid = valid && compiler_sampling_admission_decimal(facts[SAMPLING_FACT_REQUEST_RUN], &request_run) && request_run &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_EXECUTOR_RUN], &executor_run) && executor_run &&
        request_run != executor_run &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_PARENT_COUNT], &parent_count) &&
        (parent_count == 1 || parent_count == 2);
    u64 repositories[] = {SAMPLING_FACT_REPOSITORY, SAMPLING_FACT_REQUEST_REPOSITORY,
        SAMPLING_FACT_HEAD_REPOSITORY, SAMPLING_FACT_PULL_REPOSITORY};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(repositories); i += 1)
    {
        valid = string_equal(facts[repositories[i]], config[PREPARATION_CONFIG_REPOSITORY]);
    }
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
    {
        valid = string_equal(facts[role], config[PREPARATION_CONFIG_OWNER_LOGIN]) &&
            string_equal(facts[role + 1], config[PREPARATION_CONFIG_OWNER_ID]);
    }
    if (marker.length && marker.pointer && marker.pointer[marker.length - 1] == '\n')
    {
        marker = string_slice(marker, 0, marker.length - 1);
    }
    String8 expected = valid ? string_format(arena,
        utility ? S8("profile: compiler-baseline-closure-utility-v1 packet: 0 freeze: {S8}") :
            S8("profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: {S8}"),
        config[PREPARATION_CONFIG_FREEZE_REVISION]) : (String8){0};
    valid = valid && string_equal(marker, expected) &&
        string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
        (parent_count == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
            string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
    // Preparation and Utility are each first claims. Keep the exact empty
    // 16-column prefix: any prior success/failure/hostless row blocks reuse.
    String8 history_config[SAMPLING_CONFIG_COUNT] = {0};
    history_config[SAMPLING_CONFIG_OWNER_LOGIN] = config[PREPARATION_CONFIG_OWNER_LOGIN];
    history_config[SAMPLING_CONFIG_OWNER_ID] = config[PREPARATION_CONFIG_OWNER_ID];
    valid = valid && compiler_sampling_admission_history(history, history_config, facts, S8("acquire"), 0,
        config[PREPARATION_CONFIG_FREEZE_SHA], config[PREPARATION_CONFIG_FREEZE_REVISION]) &&
        compiler_sampling_historical_prefix(history, request_run);
    if (valid && utility)
    {
        result.utility = (CompilerClosureUtilityAdmission){
            .plan = closure, .phase = S8("utility"), .family = S8("utility"),
            .freeze_revision = config[PREPARATION_CONFIG_FREEZE_REVISION],
            .freeze_sha256 = config[PREPARATION_CONFIG_FREEZE_SHA], .protocol_sha256 = protocol,
            .trusted_revision = trusted, .policy_trusted_revision = facts[SAMPLING_FACT_TRUSTED_REVISION],
            .history_since = config[PREPARATION_CONFIG_HISTORY_SINCE], .reason = S8("historical-data-validation-only"),
            .reservation_seconds = BUSTER_CLOSURE_UTILITY_PHYSICAL_SECONDS,
            .worker_seconds = BUSTER_CLOSURE_UTILITY_WORKER_SECONDS, .tail_seconds = BUSTER_CLOSURE_UTILITY_TAIL_SECONDS,
            .timeout_minutes = BUSTER_CLOSURE_UTILITY_TIMEOUT_MINUTES, .valid = true};
    }
    else if (valid)
    {
        result.preparation = (CompilerPreparationAdmission){
            .plan = preparation, .phase = S8("qualify"), .family = S8("preparation"),
            .freeze_revision = config[PREPARATION_CONFIG_FREEZE_REVISION],
            .freeze_sha256 = config[PREPARATION_CONFIG_FREEZE_SHA], .protocol_sha256 = protocol,
            .trusted_revision = trusted, .policy_trusted_revision = facts[SAMPLING_FACT_TRUSTED_REVISION],
            .history_since = config[PREPARATION_CONFIG_HISTORY_SINCE], .reason = S8("historical-data-validation-only"),
            .reservation_seconds = BUSTER_PREPARATION_PHYSICAL_SECONDS,
            .worker_seconds = BUSTER_PREPARATION_WORKER_SECONDS, .tail_seconds = BUSTER_PREPARATION_TAIL_SECONDS,
            .timeout_minutes = BUSTER_PREPARATION_TIMEOUT_MINUTES, .valid = true};
    }
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerPrerequisiteHistoricalValidation compiler_prerequisite_historical_validate(Arena* arena,
    bool utility, String8 allowlist, String8 marker, String8 facts_text, String8 history,
    String8 plan_text, String8 api_text)
{
    CompilerPrerequisiteHistoricalValidation result = {.is_utility = utility};
    String8 api[HISTORICAL_API_COUNT] = {0};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = allowlist.pointer && allowlist.length && allowlist.length <= 16384 &&
        marker.pointer && marker.length && marker.length <= 512 &&
        facts_text.pointer && facts_text.length && facts_text.length <= 16384 &&
        history.pointer && history.length && history.length <= BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES &&
        plan_text.pointer && plan_text.length && plan_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES &&
        api_text.pointer && api_text.length && api_text.length <= 32768 &&
        compiler_main_fields(api_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names), api) &&
        compiler_sampling_admission_fields(facts_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names), facts) &&
        string_equal(api[HISTORICAL_SCHEMA], S8("buster-compiler-prerequisite-historical-api-v1")) &&
        string_equal(api[HISTORICAL_REPOSITORY], S8("buster14a/buster")) &&
        compiler_sampling_hex(api[HISTORICAL_POLICY_REVISION], 40) &&
        (string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("identical")) ||
            string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("ahead"))) &&
        string_equal(api[HISTORICAL_POLICY_REVISION], facts[SAMPLING_FACT_TRUSTED_REVISION]) &&
        string_equal(api[HISTORICAL_EXECUTOR_HEAD], api[HISTORICAL_POLICY_REVISION]) &&
        string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], S8("-")) &&
        string_equal(api[HISTORICAL_ACQUISITION_SHA256], S8("-"));
    u64 joins[][2] = {{HISTORICAL_REQUEST_RUN_ID, SAMPLING_FACT_REQUEST_RUN},
        {HISTORICAL_REQUEST_RUN_ATTEMPT, SAMPLING_FACT_REQUEST_ATTEMPT},
        {HISTORICAL_REQUEST_HEAD, SAMPLING_FACT_REQUEST_HEAD},
        {HISTORICAL_EXECUTOR_RUN_ID, SAMPLING_FACT_EXECUTOR_RUN},
        {HISTORICAL_EXECUTOR_RUN_ATTEMPT, SAMPLING_FACT_EXECUTOR_ATTEMPT},
        {HISTORICAL_PULL_STATE, SAMPLING_FACT_PULL_STATE}};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(joins); i += 1)
    {
        valid = string_equal(api[joins[i][0]], facts[joins[i][1]]);
    }
    u64 attempts[] = {HISTORICAL_REQUEST_RUN_ATTEMPT, HISTORICAL_REQUEST_LATEST_ATTEMPT,
        HISTORICAL_EXECUTOR_RUN_ATTEMPT, HISTORICAL_EXECUTOR_LATEST_ATTEMPT};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(attempts); i += 1)
    {
        valid = string_equal(api[attempts[i]], S8("1"));
    }
    valid = valid && string_equal(api[HISTORICAL_REQUEST_WORKFLOW], S8(".github/workflows/9700x-direct-request.yml")) &&
        string_equal(api[HISTORICAL_REQUEST_EVENT], S8("pull_request")) &&
        string_equal(api[HISTORICAL_REQUEST_STATUS], S8("completed")) &&
        string_equal(api[HISTORICAL_REQUEST_CONCLUSION], S8("success")) &&
        string_equal(api[HISTORICAL_EXECUTOR_WORKFLOW], S8(".github/workflows/9700x-direct-bench.yml")) &&
        string_equal(api[HISTORICAL_EXECUTOR_EVENT], S8("workflow_run")) &&
        string_equal(api[HISTORICAL_EXECUTOR_BRANCH], S8("main")) &&
        string_equal(api[HISTORICAL_EXECUTOR_STATUS], S8("completed")) &&
        (string_equal(api[HISTORICAL_EXECUTOR_CONCLUSION], S8("success")) ||
            string_equal(api[HISTORICAL_EXECUTOR_CONCLUSION], S8("failure")));
    u64 actors[] = {HISTORICAL_EXECUTOR_ACTOR_LOGIN, HISTORICAL_EXECUTOR_TRIGGERING_LOGIN};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(actors); i += 1)
    {
        valid = string_equal(api[actors[i]], S8("davidgmbb")) &&
            string_equal(api[actors[i] + 1], S8("39247043"));
    }
    u64 request_id = 0, executor_id = 0, pull_id = 0, parent_count = 0;
    valid = valid && compiler_sampling_admission_decimal(api[HISTORICAL_REQUEST_RUN_ID], &request_id) && request_id &&
        compiler_sampling_admission_decimal(api[HISTORICAL_EXECUTOR_RUN_ID], &executor_id) && executor_id &&
        request_id != executor_id && compiler_sampling_admission_decimal(api[HISTORICAL_PULL_NUMBER], &pull_id) && pull_id &&
        string_equal(api[HISTORICAL_PULL_NUMBER], api[HISTORICAL_ASSOCIATED_PULL_NUMBER]) &&
        string_equal(api[HISTORICAL_SOURCE_COMMIT], api[HISTORICAL_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_ASSOCIATED_COMMIT], api[HISTORICAL_REQUEST_HEAD]) &&
        compiler_sampling_hex(api[HISTORICAL_PULL_CURRENT_HEAD], 40) &&
        compiler_sampling_hex(api[HISTORICAL_FIRST_PARENT], 40) &&
        !string_equal(api[HISTORICAL_FIRST_PARENT], api[HISTORICAL_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_COMPARE_PARENT_0], api[HISTORICAL_FIRST_PARENT]) &&
        string_equal(api[HISTORICAL_COMPARE_HEAD_0], api[HISTORICAL_REQUEST_HEAD]) &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_PARENT_COUNT], &parent_count) &&
        (parent_count == 1 || parent_count == 2);
    if (valid && parent_count == 2)
    {
        valid = compiler_sampling_hex(api[HISTORICAL_SECOND_PARENT], 40) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_REQUEST_HEAD]) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_FIRST_PARENT]) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], api[HISTORICAL_SECOND_PARENT]) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], api[HISTORICAL_REQUEST_HEAD]);
    }
    else if (valid)
    {
        valid = string_equal(api[HISTORICAL_SECOND_PARENT], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], S8("-"));
    }
    String8 title = string_format(arena, S8("9700X request {S8}.1 head {S8}"),
        api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_REQUEST_HEAD]);
    valid = valid && string_equal(api[HISTORICAL_EXECUTOR_TITLE], title) &&
        compiler_sampling_historical_digest(arena, allowlist, api[HISTORICAL_ALLOWLIST_SHA256]) &&
        compiler_sampling_historical_digest(arena, facts_text, api[HISTORICAL_FACTS_SHA256]) &&
        compiler_sampling_historical_digest(arena, history, api[HISTORICAL_HISTORY_SHA256]) &&
        compiler_sampling_historical_digest(arena, plan_text, api[HISTORICAL_FREEZE_SHA256]);
    CompilerPrerequisiteHistoricalValidation packet = valid ? compiler_prerequisite_historical_packet(arena,
        utility, allowlist, marker, facts_text, history, plan_text) : (CompilerPrerequisiteHistoricalValidation){0};
    String8 plan_sha = utility ? packet.utility.freeze_sha256 : packet.preparation.freeze_sha256;
    String8 check_marker = string_format(arena,
        utility ? S8("buster-compiler-closure-utility-v1:{S8}:utility:0:{S8}:{S8}:1") :
            S8("buster-compiler-preparation-v1:{S8}:qualify:0:{S8}:{S8}:1"),
        plan_sha, api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_EXECUTOR_RUN_ID]);
    // Scientific negatives are complete data. Their publisher may fail the
    // executor workflow; the independent raw reader still proves host success,
    // cleanup, complete controls and actual numeric science/cost evidence.
    bool positive = string_equal(api[HISTORICAL_CHECK_CONCLUSION], S8("success")) &&
        string_equal(api[HISTORICAL_CHECK_TITLE], utility ?
            S8("Valid unqualified utility packet") : S8("Valid unqualified preparation packet"));
    bool negative = string_equal(api[HISTORICAL_CHECK_CONCLUSION], S8("failure")) &&
        string_equal(api[HISTORICAL_CHECK_TITLE], utility ?
            S8("Complete unqualified Utility; net criterion not met") : S8("Unqualified preparation controls failed"));
    valid = valid && packet.valid && string_equal(api[HISTORICAL_FREEZE_SHA256], plan_sha) &&
        string_equal(api[HISTORICAL_CHECK_NAME], utility ?
            S8("9700X compiler closure utility research") : S8("9700X compiler preparation research")) &&
        string_equal(api[HISTORICAL_CHECK_APP_ID], S8("15368")) &&
        string_equal(api[HISTORICAL_CHECK_HEAD], api[HISTORICAL_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_CHECK_EXTERNAL_ID], check_marker) &&
        string_equal(api[HISTORICAL_CHECK_STATUS], S8("completed")) && (positive || negative);
    if (valid)
    {
        result = packet;
        result.policy_revision = api[HISTORICAL_POLICY_REVISION];
        result.api_sha256 = stage_object_sha256_bytes(arena, (u8*)api_text.pointer, api_text.length);
        result.executor_conclusion = api[HISTORICAL_EXECUTOR_CONCLUSION];
        result.check_conclusion = api[HISTORICAL_CHECK_CONCLUSION];
        result.check_title = api[HISTORICAL_CHECK_TITLE];
    }
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_prerequisite_historical_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool utility = arguments.length && string_equal(arguments.pointer[0], S8("--validate-historical-utility"));
    bool valid = arguments.length == 8 && (utility ||
        string_equal(arguments.pointer[0], S8("--validate-historical-preparation")));
    String8 data[6] = {0};
    u64 limits[] = {16384, 512, 16384, BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES,
        BUSTER_SAMPLING_FREEZE_MAX_BYTES, 32768};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(data); i += 1)
    {
        valid = compiler_main_route_read(arena, arguments.pointer[i + 1], limits[i], &data[i]);
    }
    CompilerPrerequisiteHistoricalValidation observed = valid ? compiler_prerequisite_historical_validate(arena,
        utility, data[0], data[1], data[2], data[3], data[4], data[5]) : (CompilerPrerequisiteHistoricalValidation){0};
    if (observed.valid)
    {
        CompilerPreparationAdmission preparation = observed.preparation;
        CompilerClosureUtilityAdmission closure = observed.utility;
        String8 prefix = utility ? S8("utility") : S8("preparation");
        String8 extra = utility ? string_format(arena, S8("utility_pull_head={S8}\n"), closure.plan.pull_head) : S8("");
        String8 output = string_format(arena,
            S8("{S8}_historical_valid=true\n{S8}_policy_revision={S8}\n"
               "{S8}_phase={S8}\n{S8}_packet=0\n{S8}_family={S8}\n"
               "{S8}_reservation_seconds={u64}\n{S8}_worker_seconds={u64}\n{S8}_timeout_minutes={u64}\n"
               "{S8}_plan_revision={S8}\n{S8}_plan_sha256={S8}\n{S8}_protocol_sha256={S8}\n"
               "{S8}_base={S8}\n{S8}_base_tree={S8}\n{S8}_candidate_revision={S8}\n{S8}_candidate_tree={S8}\n"
               "{S8}{S8}_trusted_revision={S8}\n{S8}_historical_api_sha256={S8}\n"
               "{S8}_historical_executor_conclusion={S8}\n{S8}_historical_check_conclusion={S8}\n"
               "{S8}_historical_check_title={S8}\n{S8}_historical_qualification=unqualified\n"
               "{S8}_historical_execution_authority=false\n"),
            prefix, prefix, observed.policy_revision,
            prefix, utility ? closure.phase : preparation.phase, prefix, prefix, utility ? closure.family : preparation.family,
            prefix, utility ? closure.reservation_seconds : preparation.reservation_seconds,
            prefix, utility ? closure.worker_seconds : preparation.worker_seconds,
            prefix, utility ? closure.timeout_minutes : preparation.timeout_minutes,
            prefix, utility ? closure.freeze_revision : preparation.freeze_revision,
            prefix, utility ? closure.freeze_sha256 : preparation.freeze_sha256,
            prefix, utility ? closure.protocol_sha256 : preparation.protocol_sha256,
            prefix, utility ? closure.plan.baseline_revision : preparation.plan.baseline_revision,
            prefix, utility ? closure.plan.baseline_tree : preparation.plan.baseline_tree,
            prefix, utility ? closure.plan.candidate_revision : preparation.plan.candidate_revision,
            prefix, utility ? closure.plan.candidate_tree : preparation.plan.candidate_tree,
            extra, prefix, utility ? closure.trusted_revision : preparation.trusted_revision,
            prefix, observed.api_sha256, prefix, observed.executor_conclusion,
            prefix, observed.check_conclusion, prefix, observed.check_title, prefix, prefix);
        if (file_write(arguments.pointer[7], BUSTER_SLICE_TO_BYTE_SLICE(output)))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    string_print(S8("COMPILER_PREREQUISITE_HISTORICAL validation={S8} qualification=unqualified execution_authority=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("refused"));
    return result;
}
#endif
