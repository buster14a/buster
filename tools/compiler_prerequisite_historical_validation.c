// Read-only historical compiler research data. Include after approved sampling
// history, preparation/Utility admission and MAIN field/read helpers.
// Entry map: compiler_prerequisite_historical_* validates complete prerequisite
// identities; compiler_historical_original_facts_* binds actual ZIP/API facts;
// compiler_historical_terminal_* validates original failed/hostless data only.
// *_packet keeps committed native plan policy; *_paths checks declared lexical
// identities; strict original API proofs bind immutable attempts and bytes.
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

// Pure output formation makes the data-only boundary independently testable.
BUSTER_GLOBAL_LOCAL String8 compiler_prerequisite_historical_output(Arena* arena,
    CompilerPrerequisiteHistoricalValidation observed)
{
    String8 output = {0};
    bool utility = observed.is_utility;
    if (observed.valid)
    {
        CompilerPreparationAdmission preparation = observed.preparation;
        CompilerClosureUtilityAdmission closure = observed.utility;
        String8 prefix = utility ? S8("utility") : S8("preparation");
        String8 extra = utility ? string_format(arena, S8("utility_pull_head={S8}\n"), closure.plan.pull_head) : S8("");
        output = string_format(arena,
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
    }
    return output;
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
        String8 output = compiler_prerequisite_historical_output(arena, observed);
        if (file_write(arguments.pointer[7], BUSTER_SLICE_TO_BYTE_SLICE(output)))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    string_print(S8("COMPILER_PREREQUISITE_HISTORICAL validation={S8} qualification=unqualified execution_authority=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("refused"));
    return result;
}

// Bind actual original artifact transport to separately validated current API
// facts. Only observed PR state may change; no observation is re-encoded.
// This result is a byte binding, never historical admission by itself. The
// caller binds current_facts_sha256 to its independently validated 51-row proof.
typedef struct CompilerHistoricalOriginalFacts CompilerHistoricalOriginalFacts;
struct CompilerHistoricalOriginalFacts
{
    String8 current_sha256;
    String8 original_sha256;
    bool valid;
};

BUSTER_GLOBAL_LOCAL CompilerHistoricalOriginalFacts compiler_historical_original_facts_validate(Arena* arena,
    String8 current_text, String8 original_text)
{
    CompilerHistoricalOriginalFacts result = {0};
    String8 current[SAMPLING_FACT_COUNT] = {0}, original[SAMPLING_FACT_COUNT] = {0};
    bool valid = current_text.pointer && current_text.length && current_text.length <= 16384 &&
        original_text.pointer && original_text.length && original_text.length <= 16384 &&
        compiler_main_fields(current_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names), current) &&
        compiler_main_fields(original_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names), original) &&
        string_equal(current[SAMPLING_FACT_SCHEMA], S8("buster-main-sampling-github-facts-v1")) &&
        (string_equal(current[SAMPLING_FACT_PULL_STATE], S8("open")) ||
            string_equal(current[SAMPLING_FACT_PULL_STATE], S8("closed"))) &&
        string_equal(original[SAMPLING_FACT_PULL_STATE], S8("open")) &&
        compiler_sampling_hex(current[SAMPLING_FACT_REQUEST_HEAD], 40) &&
        compiler_sampling_hex(current[SAMPLING_FACT_TRUSTED_REVISION], 40) &&
        string_equal(current[SAMPLING_FACT_REQUEST_ATTEMPT], S8("1")) &&
        string_equal(current[SAMPLING_FACT_EXECUTOR_ATTEMPT], S8("1"));
    for (u64 i = 0; valid && i < SAMPLING_FACT_COUNT; i += 1)
    {
        if (i != SAMPLING_FACT_PULL_STATE)
        {
            valid = string_equal(current[i], original[i]);
        }
    }
    u64 request = 0, executor = 0, parents = 0;
    valid = valid && compiler_sampling_admission_decimal(current[SAMPLING_FACT_REQUEST_RUN], &request) && request &&
        compiler_sampling_admission_decimal(current[SAMPLING_FACT_EXECUTOR_RUN], &executor) && executor &&
        request != executor && compiler_sampling_admission_decimal(current[SAMPLING_FACT_PARENT_COUNT], &parents) &&
        (parents == 1 || parents == 2) && current[SAMPLING_FACT_FRESH_PARENT_0].length &&
        !string_equal(current[SAMPLING_FACT_FRESH_PARENT_0], S8("-")) &&
        (parents == 2 ? string_equal(current[SAMPLING_FACT_FRESH_PARENT_1], current[SAMPLING_FACT_FRESH_PARENT_0]) :
            string_equal(current[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
    u64 repositories[] = {SAMPLING_FACT_REPOSITORY, SAMPLING_FACT_REQUEST_REPOSITORY,
        SAMPLING_FACT_HEAD_REPOSITORY, SAMPLING_FACT_PULL_REPOSITORY};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(repositories); i += 1)
    {
        valid = string_equal(current[repositories[i]], S8("buster14a/buster"));
    }
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
    {
        valid = string_equal(current[role], S8("davidgmbb")) && string_equal(current[role + 1], S8("39247043"));
    }
    if (valid)
    {
        result.current_sha256 = stage_object_sha256_bytes(arena, (u8*)current_text.pointer, current_text.length);
        result.original_sha256 = stage_object_sha256_bytes(arena, (u8*)original_text.pointer, original_text.length);
    }
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_historical_original_facts_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool valid = arguments.length == 4 &&
        string_equal(arguments.pointer[0], S8("--validate-historical-original-facts"));
    String8 current = {0}, original = {0};
    valid = valid && compiler_main_route_read(arena, arguments.pointer[1], 16384, &current) &&
        compiler_main_route_read(arena, arguments.pointer[2], 16384, &original);
    CompilerHistoricalOriginalFacts observed = valid ? compiler_historical_original_facts_validate(arena,
        current, original) : (CompilerHistoricalOriginalFacts){0};
    if (observed.valid)
    {
        String8 output = string_format(arena,
            S8("historical_original_facts_valid=true\ncurrent_facts_sha256={S8}\noriginal_facts_sha256={S8}\n"
               "historical_execution_authority=false\nqualification=unqualified\n"),
            observed.current_sha256, observed.original_sha256);
        if (file_write(arguments.pointer[3], BUSTER_SLICE_TO_BYTE_SLICE(output)))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    string_print(S8("COMPILER_HISTORICAL_ORIGINAL_FACTS binding={S8} execution_authority=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("refused"));
    return result;
}

// Separate terminal data path. A verified original failure is chargeable data,
// never a complete measurement or execution authority. A missing queued check
// cannot erase an executor: the adapter independently inventories canonical
// original request/head executor runs, including failures before publication.
BUSTER_GLOBAL_LOCAL String8 compiler_historical_terminal_names[] = {
    S8("schema"), S8("kind"), S8("phase"), S8("packet"), S8("family"),
    S8("executor_inventory_count"), S8("selected_executor_inventory_id"),
    S8("physical_job_id"), S8("physical_job_state"), S8("physical_job_conclusion"),
    S8("physical_job_started_at"), S8("physical_job_completed_at"), S8("terminal_state"),
    S8("terminal_api_sha256"), S8("terminal_api_bytes"), S8("context_revision"), S8("context_main_relation")
};
typedef enum CompilerHistoricalTerminalField
{
    TERMINAL_SCHEMA, TERMINAL_KIND, TERMINAL_PHASE, TERMINAL_PACKET, TERMINAL_FAMILY,
    TERMINAL_EXECUTOR_COUNT, TERMINAL_SELECTED_EXECUTOR, TERMINAL_JOB_ID, TERMINAL_JOB_STATE,
    TERMINAL_JOB_CONCLUSION, TERMINAL_JOB_START, TERMINAL_JOB_END, TERMINAL_STATE,
    TERMINAL_API_SHA256, TERMINAL_API_BYTES, TERMINAL_CONTEXT, TERMINAL_CONTEXT_RELATION, TERMINAL_COUNT,
} CompilerHistoricalTerminalField;

typedef struct CompilerHistoricalTerminalValidation CompilerHistoricalTerminalValidation;
struct CompilerHistoricalTerminalValidation
{
    String8 kind;
    String8 phase;
    String8 family;
    String8 freeze_revision;
    String8 freeze_sha256;
    String8 trusted_revision;
    String8 protocol_sha256;
    String8 policy_revision;
    String8 context_revision;
    String8 request_run_id;
    String8 request_head;
    String8 request_conclusion;
    String8 executor_run_id;
    String8 executor_attempt;
    String8 executor_conclusion;
    String8 state;
    String8 api_sha256;
    String8 envelope_sha256;
    String8 job_id;
    String8 job_state;
    String8 job_conclusion;
    String8 job_start;
    String8 job_end;
    u64 packet;
    u64 envelope_bytes;
    bool valid;
};

BUSTER_GLOBAL_LOCAL bool compiler_historical_terminal_conclusion(String8 conclusion)
{
    bool valid = string_equal(conclusion, S8("success")) || string_equal(conclusion, S8("failure")) ||
        string_equal(conclusion, S8("cancelled")) || string_equal(conclusion, S8("timed_out")) ||
        string_equal(conclusion, S8("skipped")) || string_equal(conclusion, S8("neutral")) ||
        string_equal(conclusion, S8("action_required")) || string_equal(conclusion, S8("startup_failure"));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_historical_terminal_failed(String8 conclusion)
{
    bool failed = string_equal(conclusion, S8("failure")) || string_equal(conclusion, S8("timed_out")) ||
        string_equal(conclusion, S8("action_required")) || string_equal(conclusion, S8("startup_failure"));
    return failed;
}

BUSTER_GLOBAL_LOCAL bool compiler_historical_terminal_time_order(String8 start, String8 end)
{
    bool start_known = !string_equal(start, S8("-")), end_known = !string_equal(end, S8("-"));
    bool valid = (!start_known || compiler_sampling_admission_timestamp(start)) &&
        (!end_known || compiler_sampling_admission_timestamp(end));
    u64 cursor = 0;
    while (valid && start_known && end_known && cursor < start.length && start.pointer[cursor] == end.pointer[cursor])
    {
        cursor += 1;
    }
    valid = valid && (!start_known || !end_known || cursor == start.length || start.pointer[cursor] < end.pointer[cursor]);
    return valid;
}

// Parse immutable plan controls even when no original executor/policy exists.
// The separate protected context is not substituted for missing original Pi.
// These lineage fields are derived from actual committed plans solely to check
// the fixed native chronological schedule; they do not recreate an allowlist.
BUSTER_GLOBAL_LOCAL bool compiler_historical_terminal_plan(Arena* arena, String8 kind,
    String8 phase, u64 packet, String8 freeze_revision, String8 freeze_text, String8 parent_text,
    String8 acquisition_text, String8 history, String8* facts, String8* api,
    CompilerHistoricalTerminalValidation* result)
{
    bool sampling = string_equal(kind, S8("sampling"));
    bool utility = string_equal(kind, S8("utility"));
    bool valid = compiler_sampling_historical_digest(arena, freeze_text, api[HISTORICAL_FREEZE_SHA256]);
    String8 lineage[SAMPLING_CONFIG_COUNT] = {0};
    lineage[SAMPLING_CONFIG_OWNER_LOGIN] = S8("davidgmbb");
    lineage[SAMPLING_CONFIG_OWNER_ID] = S8("39247043");
    lineage[SAMPLING_CONFIG_FREEZE_REVISION] = freeze_revision;
    lineage[SAMPLING_CONFIG_FREEZE_SHA] = api[HISTORICAL_FREEZE_SHA256];
    String8 acquisition_revision = freeze_revision, acquisition_sha = api[HISTORICAL_FREEZE_SHA256];
    if (valid && sampling)
    {
        bool acquiring = string_equal(phase, S8("acquire")), confirming = string_equal(phase, S8("confirm"));
        CompilerSamplingAcquisitionPlan acquisition = compiler_sampling_acquisition_plan_parse(acquisition_text);
        CompilerSamplingFreeze freeze = acquiring ? (CompilerSamplingFreeze){0} : compiler_sampling_freeze_parse(freeze_text);
        CompilerSamplingAcquisitionPlan joined_acquisition = {0};
        CompilerSamplingFreeze pilot = {0};
        valid = acquisition.valid &&
            compiler_sampling_historical_digest(arena, acquisition_text, api[HISTORICAL_ACQUISITION_SHA256]);
        if (valid && acquiring)
        {
            valid = !parent_text.length && string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], S8("-")) &&
                string_equal(freeze_text, acquisition_text);
        }
        else if (valid)
        {
            valid = freeze.valid && string_equal(freeze.phase, phase) &&
                compiler_sampling_historical_digest(arena, parent_text, api[HISTORICAL_PARENT_FREEZE_SHA256]) &&
                string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], freeze.campaign_parent) &&
                compiler_sampling_admission_parent(freeze, parent_text, &joined_acquisition, &pilot) &&
                string_equal(acquisition.base, freeze.base) && string_equal(acquisition.base_tree, freeze.base_tree) &&
                string_equal(acquisition.trusted_revision, freeze.trusted_revision) &&
                string_equal(acquisition.baseline_revision, freeze.baseline_revision) &&
                string_equal(acquisition.ab1_revision, freeze.ab1_revision) &&
                string_equal(acquisition.ab2_revision, freeze.ab2_revision) &&
                string_equal(acquisition.protocol_sha256, freeze.protocol_sha256);
            lineage[SAMPLING_CONFIG_PARENT_SHA] = freeze.campaign_parent;
            lineage[SAMPLING_CONFIG_PARENT_REVISION] = freeze.campaign_parent_revision;
            acquisition_revision = confirming ? pilot.campaign_parent_revision : freeze.campaign_parent_revision;
            acquisition_sha = confirming ? pilot.campaign_parent : freeze.campaign_parent;
            valid = valid && string_equal(api[HISTORICAL_ACQUISITION_SHA256], acquisition_sha) &&
                (confirming || string_equal(acquisition_text, parent_text));
        }
        if (valid)
        {
            result->trusted_revision = acquisition.trusted_revision;
            result->protocol_sha256 = acquisition.protocol_sha256;
        }
    }
    else if (valid)
    {
        CompilerPreparationPlan preparation = utility ? (CompilerPreparationPlan){0} : compiler_preparation_plan_parse(freeze_text);
        CompilerClosureUtilityPlan closure = utility ? compiler_closure_utility_plan_parse(freeze_text) : (CompilerClosureUtilityPlan){0};
        valid = !parent_text.length && !acquisition_text.length &&
            string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], S8("-")) &&
            string_equal(api[HISTORICAL_ACQUISITION_SHA256], S8("-")) &&
            (utility ? closure.valid : preparation.valid) &&
            compiler_prerequisite_historical_paths(utility ? closure.source_root : preparation.source_root,
                utility ? closure.output_root : preparation.output_root,
                utility ? closure.trusted_root : preparation.trusted_root, utility);
        if (valid)
        {
            result->trusted_revision = utility ? closure.trusted_revision : preparation.trusted_revision;
            result->protocol_sha256 = utility ? closure.protocol_sha256 : preparation.protocol_sha256;
        }
    }
    valid = valid && compiler_sampling_admission_history(history, lineage, facts,
        sampling ? phase : S8("acquire"), sampling ? packet : 0, acquisition_sha, acquisition_revision);
    return valid;
}

BUSTER_GLOBAL_LOCAL CompilerHistoricalTerminalValidation compiler_historical_terminal_validate(Arena* arena,
    String8 kind, String8 allowlist, String8 marker, String8 facts_text, String8 history, String8 freeze_text,
    String8 parent_text, String8 acquisition_text, String8 api_text, String8 terminal_text, String8 envelope)
{
    CompilerHistoricalTerminalValidation result = {.kind = kind};
    bool sampling = string_equal(kind, S8("sampling")), utility = string_equal(kind, S8("utility"));
    String8 terminal[TERMINAL_COUNT] = {0}, api[HISTORICAL_API_COUNT] = {0}, facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = (sampling || utility || string_equal(kind, S8("preparation"))) &&
        allowlist.length <= 16384 && marker.pointer && marker.length && marker.length <= 512 &&
        facts_text.pointer && facts_text.length && facts_text.length <= 16384 &&
        history.pointer && history.length && history.length <= BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES &&
        freeze_text.pointer && freeze_text.length && freeze_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES &&
        parent_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES && acquisition_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES &&
        api_text.pointer && api_text.length && api_text.length <= 32768 &&
        terminal_text.pointer && terminal_text.length && terminal_text.length <= 16384 &&
        envelope.pointer && envelope.length && envelope.length <= 8 * 1024 * 1024 &&
        compiler_main_fields(terminal_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_historical_terminal_names), terminal) &&
        compiler_main_fields(api_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names), api) &&
        compiler_main_fields(facts_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names), facts) &&
        string_equal(terminal[TERMINAL_SCHEMA], S8("buster-compiler-historical-terminal-v1")) &&
        string_equal(terminal[TERMINAL_KIND], kind) &&
        string_equal(api[HISTORICAL_SCHEMA], sampling ? S8("buster-main-sampling-historical-api-v1") :
            S8("buster-compiler-prerequisite-historical-api-v1")) &&
        string_equal(api[HISTORICAL_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(facts[SAMPLING_FACT_SCHEMA], S8("buster-main-sampling-github-facts-v1")) &&
        (string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("open")) ||
            string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("closed"))) &&
        string_equal(api[HISTORICAL_PULL_STATE], facts[SAMPLING_FACT_PULL_STATE]) &&
        string_equal(api[HISTORICAL_REQUEST_RUN_ATTEMPT], S8("1")) &&
        string_equal(api[HISTORICAL_REQUEST_LATEST_ATTEMPT], S8("1")) &&
        string_equal(facts[SAMPLING_FACT_REQUEST_ATTEMPT], S8("1")) &&
        string_equal(api[HISTORICAL_REQUEST_WORKFLOW], S8(".github/workflows/9700x-direct-request.yml")) &&
        string_equal(api[HISTORICAL_REQUEST_EVENT], S8("pull_request")) &&
        string_equal(api[HISTORICAL_REQUEST_STATUS], S8("completed")) &&
        compiler_historical_terminal_conclusion(api[HISTORICAL_REQUEST_CONCLUSION]);
    u64 request = 0, pull = 0, parents = 0, packet = 0, envelope_bytes = 0;
    valid = valid && compiler_sampling_admission_decimal(api[HISTORICAL_REQUEST_RUN_ID], &request) && request &&
        string_equal(api[HISTORICAL_REQUEST_RUN_ID], facts[SAMPLING_FACT_REQUEST_RUN]) &&
        compiler_sampling_hex(api[HISTORICAL_REQUEST_HEAD], 40) &&
        string_equal(api[HISTORICAL_REQUEST_HEAD], facts[SAMPLING_FACT_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_SOURCE_COMMIT], api[HISTORICAL_REQUEST_HEAD]) &&
        compiler_sampling_admission_decimal(api[HISTORICAL_PULL_NUMBER], &pull) && pull &&
        string_equal(api[HISTORICAL_ASSOCIATED_PULL_NUMBER], api[HISTORICAL_PULL_NUMBER]) &&
        string_equal(api[HISTORICAL_ASSOCIATED_COMMIT], api[HISTORICAL_REQUEST_HEAD]) &&
        compiler_sampling_hex(api[HISTORICAL_PULL_CURRENT_HEAD], 40) &&
        compiler_sampling_admission_decimal(facts[SAMPLING_FACT_PARENT_COUNT], &parents) && (parents == 1 || parents == 2) &&
        compiler_sampling_hex(api[HISTORICAL_FIRST_PARENT], 40) &&
        !string_equal(api[HISTORICAL_FIRST_PARENT], api[HISTORICAL_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_COMPARE_PARENT_0], api[HISTORICAL_FIRST_PARENT]) &&
        string_equal(api[HISTORICAL_COMPARE_HEAD_0], api[HISTORICAL_REQUEST_HEAD]) &&
        compiler_sampling_admission_decimal(terminal[TERMINAL_PACKET], &packet) &&
        string_equal(terminal[TERMINAL_PACKET], string_format(arena, S8("{u64}"), packet)) &&
        compiler_sampling_admission_decimal(terminal[TERMINAL_API_BYTES], &envelope_bytes) &&
        envelope_bytes == envelope.length &&
        compiler_sampling_historical_digest(arena, envelope, terminal[TERMINAL_API_SHA256]);
    if (valid && parents == 2)
    {
        valid = compiler_sampling_hex(api[HISTORICAL_SECOND_PARENT], 40) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_FIRST_PARENT]) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_REQUEST_HEAD]) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], api[HISTORICAL_SECOND_PARENT]) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], api[HISTORICAL_REQUEST_HEAD]);
    }
    else if (valid)
    {
        valid = string_equal(api[HISTORICAL_SECOND_PARENT], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], S8("-"));
    }
    u64 repositories[] = {SAMPLING_FACT_REPOSITORY, SAMPLING_FACT_REQUEST_REPOSITORY,
        SAMPLING_FACT_HEAD_REPOSITORY, SAMPLING_FACT_PULL_REPOSITORY};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(repositories); i += 1)
    {
        valid = string_equal(facts[repositories[i]], S8("buster14a/buster"));
    }
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
    {
        valid = string_equal(facts[role], S8("davidgmbb")) && string_equal(facts[role + 1], S8("39247043"));
    }
    if (marker.pointer && marker.length && marker.pointer[marker.length - 1] == '\n')
    {
        marker = string_slice(marker, 0, marker.length - 1);
    }
    String8 revision = marker.length >= 40 ? string_slice(marker, marker.length - 40, marker.length) : (String8){0};
    CompilerSamplingPacket schedule = sampling ? compiler_sampling_schedule(terminal[TERMINAL_PHASE], packet) :
        (CompilerSamplingPacket){0};
    String8 profile = sampling ? string_equal(terminal[TERMINAL_PHASE], S8("acquire")) ?
        S8("compiler-main-sampling-acquire-v1") : string_equal(terminal[TERMINAL_PHASE], S8("confirm")) ?
        S8("compiler-main-sampling-confirm-v1") : S8("compiler-main-sampling-pilot-v1") :
        utility ? S8("compiler-baseline-closure-utility-v1") : S8("compiler-baseline-closure-qualification-v1");
    String8 expected_marker = string_format(arena, S8("profile: {S8} packet: {u64} freeze: {S8}"), profile, packet, revision);
    valid = valid && compiler_sampling_hex(revision, 40) && string_equal(marker, expected_marker) &&
        string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
        (parents == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
            string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-"))) &&
        (sampling ? schedule.valid && string_equal(terminal[TERMINAL_FAMILY], schedule.family) :
            packet == 0 && string_equal(terminal[TERMINAL_PHASE], utility ? S8("utility") : S8("qualify")) &&
                string_equal(terminal[TERMINAL_FAMILY], kind)) &&
        compiler_sampling_historical_digest(arena, allowlist, api[HISTORICAL_ALLOWLIST_SHA256]) &&
        compiler_sampling_historical_digest(arena, facts_text, api[HISTORICAL_FACTS_SHA256]) &&
        compiler_sampling_historical_digest(arena, history, api[HISTORICAL_HISTORY_SHA256]) &&
        compiler_sampling_historical_prefix(history, request) &&
        compiler_historical_terminal_plan(arena, kind, terminal[TERMINAL_PHASE], packet, revision,
            freeze_text, parent_text, acquisition_text, history, facts, api, &result);
    bool no_executor = string_equal(terminal[TERMINAL_EXECUTOR_COUNT], S8("0"));
    bool known_executor = string_equal(terminal[TERMINAL_EXECUTOR_COUNT], S8("1"));
    valid = valid && (no_executor || known_executor);
    if (valid && no_executor)
    {
        valid = !allowlist.length && string_equal(terminal[TERMINAL_SELECTED_EXECUTOR], S8("-")) &&
            string_equal(terminal[TERMINAL_STATE], S8("hostless")) &&
            compiler_sampling_hex(terminal[TERMINAL_CONTEXT], 40) &&
            (string_equal(terminal[TERMINAL_CONTEXT_RELATION], S8("ahead")) ||
                string_equal(terminal[TERMINAL_CONTEXT_RELATION], S8("identical"))) &&
            string_equal(api[HISTORICAL_POLICY_REVISION], S8("-")) &&
            string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("-")) &&
            string_equal(facts[SAMPLING_FACT_TRUSTED_REVISION], S8("-")) &&
            string_equal(facts[SAMPLING_FACT_EXECUTOR_RUN], S8("-")) &&
            string_equal(facts[SAMPLING_FACT_EXECUTOR_ATTEMPT], S8("-"));
        for (u64 i = HISTORICAL_EXECUTOR_RUN_ID; valid && i <= HISTORICAL_EXECUTOR_TRIGGERING_ID; i += 1)
        {
            valid = string_equal(api[i], S8("-"));
        }
    }
    else if (valid)
    {
        u64 executor = 0;
        String8 expected_title = string_format(arena, S8("9700X request {S8}.1 head {S8}"),
            api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_REQUEST_HEAD]);
        valid = allowlist.pointer && allowlist.length &&
            string_equal(terminal[TERMINAL_CONTEXT], S8("-")) &&
            string_equal(terminal[TERMINAL_CONTEXT_RELATION], S8("-")) &&
            compiler_sampling_hex(api[HISTORICAL_POLICY_REVISION], 40) &&
            (string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("ahead")) ||
                string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("identical"))) &&
            string_equal(api[HISTORICAL_POLICY_REVISION], facts[SAMPLING_FACT_TRUSTED_REVISION]) &&
            string_equal(api[HISTORICAL_EXECUTOR_HEAD], api[HISTORICAL_POLICY_REVISION]) &&
            compiler_sampling_admission_decimal(api[HISTORICAL_EXECUTOR_RUN_ID], &executor) && executor && executor != request &&
            string_equal(api[HISTORICAL_EXECUTOR_RUN_ID], facts[SAMPLING_FACT_EXECUTOR_RUN]) &&
            string_equal(terminal[TERMINAL_SELECTED_EXECUTOR], api[HISTORICAL_EXECUTOR_RUN_ID]) &&
            string_equal(api[HISTORICAL_EXECUTOR_RUN_ATTEMPT], S8("1")) &&
            string_equal(api[HISTORICAL_EXECUTOR_LATEST_ATTEMPT], S8("1")) &&
            string_equal(facts[SAMPLING_FACT_EXECUTOR_ATTEMPT], S8("1")) &&
            string_equal(api[HISTORICAL_EXECUTOR_WORKFLOW], S8(".github/workflows/9700x-direct-bench.yml")) &&
            string_equal(api[HISTORICAL_EXECUTOR_EVENT], S8("workflow_run")) &&
            string_equal(api[HISTORICAL_EXECUTOR_BRANCH], S8("main")) &&
            string_equal(api[HISTORICAL_EXECUTOR_TITLE], expected_title) &&
            string_equal(api[HISTORICAL_EXECUTOR_STATUS], S8("completed")) &&
            compiler_historical_terminal_conclusion(api[HISTORICAL_EXECUTOR_CONCLUSION]) &&
            string_equal(api[HISTORICAL_EXECUTOR_ACTOR_LOGIN], S8("davidgmbb")) &&
            string_equal(api[HISTORICAL_EXECUTOR_ACTOR_ID], S8("39247043")) &&
            string_equal(api[HISTORICAL_EXECUTOR_TRIGGERING_LOGIN], S8("davidgmbb")) &&
            string_equal(api[HISTORICAL_EXECUTOR_TRIGGERING_ID], S8("39247043"));
        if (valid && sampling)
        {
            CompilerSamplingAdmission packet_context = compiler_sampling_historical_packet(arena,
                allowlist, marker, facts_text, history, freeze_text, parent_text);
            valid = packet_context.valid && packet_context.packet == packet &&
                string_equal(packet_context.phase, terminal[TERMINAL_PHASE]) &&
                string_equal(packet_context.family, terminal[TERMINAL_FAMILY]) &&
                string_equal(packet_context.freeze_revision, revision) &&
                string_equal(packet_context.freeze_sha256, api[HISTORICAL_FREEZE_SHA256]) &&
                string_equal(packet_context.trusted_revision, result.trusted_revision);
        }
        else if (valid)
        {
            CompilerPrerequisiteHistoricalValidation packet_context = compiler_prerequisite_historical_packet(arena,
                utility, allowlist, marker, facts_text, history, freeze_text);
            String8 original_revision = utility ? packet_context.utility.freeze_revision : packet_context.preparation.freeze_revision;
            valid = packet_context.valid && string_equal(original_revision, revision);
        }
    }
    bool job_absent = string_equal(terminal[TERMINAL_JOB_ID], S8("-"));
    if (valid && job_absent)
    {
        for (u64 i = TERMINAL_JOB_STATE; valid && i <= TERMINAL_JOB_END; i += 1)
        {
            valid = string_equal(terminal[i], S8("-"));
        }
    }
    else if (valid)
    {
        u64 job = 0;
        valid = known_executor && compiler_sampling_admission_decimal(terminal[TERMINAL_JOB_ID], &job) && job &&
            string_equal(terminal[TERMINAL_JOB_STATE], S8("completed")) &&
            compiler_historical_terminal_conclusion(terminal[TERMINAL_JOB_CONCLUSION]) &&
            compiler_historical_terminal_time_order(terminal[TERMINAL_JOB_START], terminal[TERMINAL_JOB_END]);
    }
    valid = valid && (!no_executor || job_absent);
    bool check_absent = string_equal(api[HISTORICAL_CHECK_NAME], S8("-"));
    if (valid && check_absent)
    {
        for (u64 i = HISTORICAL_CHECK_APP_ID; valid && i <= HISTORICAL_CHECK_TITLE; i += 1)
        {
            valid = string_equal(api[i], S8("-"));
        }
    }
    else if (valid)
    {
        String8 check_marker = sampling ? string_format(arena, S8("buster-main-sampling-v1:{S8}:{S8}:{u64}:{S8}:{S8}:1"),
            api[HISTORICAL_FREEZE_SHA256], terminal[TERMINAL_PHASE], packet,
            api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_EXECUTOR_RUN_ID]) :
            string_format(arena, utility ? S8("buster-compiler-closure-utility-v1:{S8}:utility:0:{S8}:{S8}:1") :
                S8("buster-compiler-preparation-v1:{S8}:qualify:0:{S8}:{S8}:1"),
                api[HISTORICAL_FREEZE_SHA256], api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_EXECUTOR_RUN_ID]);
        bool completed = string_equal(api[HISTORICAL_CHECK_STATUS], S8("completed"));
        valid = known_executor && string_equal(api[HISTORICAL_CHECK_NAME], sampling ? S8("9700X compiler sampling research") :
            utility ? S8("9700X compiler closure utility research") : S8("9700X compiler preparation research")) &&
            string_equal(api[HISTORICAL_CHECK_APP_ID], S8("15368")) &&
            string_equal(api[HISTORICAL_CHECK_HEAD], api[HISTORICAL_REQUEST_HEAD]) &&
            string_equal(api[HISTORICAL_CHECK_EXTERNAL_ID], check_marker) &&
            (completed ? compiler_historical_terminal_conclusion(api[HISTORICAL_CHECK_CONCLUSION]) :
                (string_equal(api[HISTORICAL_CHECK_STATUS], S8("queued")) ||
                    string_equal(api[HISTORICAL_CHECK_STATUS], S8("in_progress"))) &&
                    string_equal(api[HISTORICAL_CHECK_CONCLUSION], S8("-"))) &&
            api[HISTORICAL_CHECK_TITLE].length;
    }
    bool cancelled = string_equal(api[HISTORICAL_REQUEST_CONCLUSION], S8("cancelled")) ||
        string_equal(api[HISTORICAL_EXECUTOR_CONCLUSION], S8("cancelled")) ||
        string_equal(terminal[TERMINAL_JOB_CONCLUSION], S8("cancelled"));
    bool failed = compiler_historical_terminal_failed(api[HISTORICAL_REQUEST_CONCLUSION]) ||
        compiler_historical_terminal_failed(api[HISTORICAL_EXECUTOR_CONCLUSION]) ||
        compiler_historical_terminal_failed(terminal[TERMINAL_JOB_CONCLUSION]);
    valid = valid && (no_executor || (string_equal(terminal[TERMINAL_STATE], S8("cancelled")) ? cancelled :
        string_equal(terminal[TERMINAL_STATE], S8("failed")) ? failed :
        string_equal(terminal[TERMINAL_STATE], S8("incomplete")) || string_equal(terminal[TERMINAL_STATE], S8("invalid"))));
    if (valid)
    {
        result.phase = terminal[TERMINAL_PHASE];
        result.family = terminal[TERMINAL_FAMILY];
        result.packet = packet;
        result.freeze_revision = revision;
        result.freeze_sha256 = api[HISTORICAL_FREEZE_SHA256];
        result.policy_revision = api[HISTORICAL_POLICY_REVISION];
        result.context_revision = terminal[TERMINAL_CONTEXT];
        result.request_run_id = api[HISTORICAL_REQUEST_RUN_ID];
        result.request_head = api[HISTORICAL_REQUEST_HEAD];
        result.request_conclusion = api[HISTORICAL_REQUEST_CONCLUSION];
        result.executor_run_id = api[HISTORICAL_EXECUTOR_RUN_ID];
        result.executor_attempt = api[HISTORICAL_EXECUTOR_RUN_ATTEMPT];
        result.executor_conclusion = api[HISTORICAL_EXECUTOR_CONCLUSION];
        result.state = terminal[TERMINAL_STATE];
        result.api_sha256 = stage_object_sha256_bytes(arena, (u8*)api_text.pointer, api_text.length);
        result.envelope_sha256 = terminal[TERMINAL_API_SHA256];
        result.envelope_bytes = envelope_bytes;
        result.job_id = terminal[TERMINAL_JOB_ID];
        result.job_state = terminal[TERMINAL_JOB_STATE];
        result.job_conclusion = terminal[TERMINAL_JOB_CONCLUSION];
        result.job_start = terminal[TERMINAL_JOB_START];
        result.job_end = terminal[TERMINAL_JOB_END];
    }
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_historical_terminal_output(Arena* arena,
    CompilerHistoricalTerminalValidation observed)
{
    String8 output = {0};
    if (observed.valid)
    {
        String8 prefix = observed.kind;
        String8 flags = string_format(arena,
            S8("{S8}_historical_terminal_valid=true\n{S8}_historical_valid=false\n"
               "{S8}_historical_measurement_valid=false\n{S8}_historical_execution_authority=false\n"
               "{S8}_historical_qualification=unqualified\n{S8}_phase={S8}\n{S8}_packet={u64}\n{S8}_family={S8}\n"),
            prefix, prefix, prefix, prefix, prefix, prefix, observed.phase, prefix, observed.packet, prefix, observed.family);
        String8 identity = string_format(arena,
            S8("{S8}_policy_revision={S8}\n{S8}_plan_revision={S8}\n{S8}_plan_sha256={S8}\n"
               "{S8}_trusted_revision={S8}\n{S8}_protocol_sha256={S8}\n"
               "{S8}_historical_context_revision={S8}\n{S8}_historical_request_run_id={S8}\n"
               "{S8}_historical_request_run_attempt=1\n{S8}_historical_request_head={S8}\n"
               "{S8}_historical_request_conclusion={S8}\n{S8}_historical_executor_run_id={S8}\n"
               "{S8}_historical_executor_run_attempt={S8}\n{S8}_historical_executor_conclusion={S8}\n"
               "{S8}_historical_terminal_state={S8}\n{S8}_historical_api_sha256={S8}\n"
               "{S8}_historical_terminal_api_sha256={S8}\n{S8}_historical_terminal_api_bytes={u64}\n"),
            prefix, observed.policy_revision, prefix, observed.freeze_revision, prefix, observed.freeze_sha256,
            prefix, observed.trusted_revision, prefix, observed.protocol_sha256,
            prefix, observed.context_revision, prefix, observed.request_run_id, prefix,
            prefix, observed.request_head, prefix, observed.request_conclusion, prefix, observed.executor_run_id,
            prefix, observed.executor_attempt, prefix, observed.executor_conclusion,
            prefix, observed.state, prefix, observed.api_sha256, prefix, observed.envelope_sha256, prefix, observed.envelope_bytes);
        String8 physical = string_format(arena,
            S8("{S8}_historical_physical_job_id={S8}\n{S8}_historical_physical_job_state={S8}\n"
               "{S8}_historical_physical_job_conclusion={S8}\n{S8}_historical_physical_job_started_at={S8}\n"
               "{S8}_historical_physical_job_completed_at={S8}\n"),
            prefix, observed.job_id, prefix, observed.job_state, prefix, observed.job_conclusion,
            prefix, observed.job_start, prefix, observed.job_end);
        String8 sampling = string_equal(prefix, S8("sampling")) ? string_format(arena,
            S8("sampling_freeze_revision={S8}\nsampling_freeze_sha256={S8}\n"),
            observed.freeze_revision, observed.freeze_sha256) : S8("");
        output = string_format(arena, S8("{S8}{S8}{S8}{S8}"), flags, identity, physical, sampling);
    }
    return output;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_historical_terminal_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool sampling = arguments.length && string_equal(arguments.pointer[0], S8("--validate-terminal-sampling"));
    bool utility = arguments.length && string_equal(arguments.pointer[0], S8("--validate-terminal-utility"));
    bool preparation = arguments.length && string_equal(arguments.pointer[0], S8("--validate-terminal-preparation"));
    bool valid = (sampling && arguments.length == 12) || ((utility || preparation) && arguments.length == 10);
    String8 data[10] = {0};
    u64 limits[] = {16384, 512, 16384, BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES,
        BUSTER_SAMPLING_FREEZE_MAX_BYTES, BUSTER_SAMPLING_FREEZE_MAX_BYTES, BUSTER_SAMPLING_FREEZE_MAX_BYTES,
        32768, 16384, 8 * 1024 * 1024};
    u64 prerequisite_indices[] = {0, 1, 2, 3, 4, 7, 8, 9};
    u64 count = sampling ? 10 : 8;
    for (u64 i = 0; valid && i < count; i += 1)
    {
        u64 field = sampling ? i : prerequisite_indices[i];
        valid = compiler_main_route_read(arena, arguments.pointer[i + 1], limits[field], &data[field]);
    }
    String8 kind = sampling ? S8("sampling") : utility ? S8("utility") : S8("preparation");
    CompilerHistoricalTerminalValidation observed = valid ? compiler_historical_terminal_validate(arena, kind,
        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]) :
        (CompilerHistoricalTerminalValidation){0};
    if (observed.valid)
    {
        String8 output = compiler_historical_terminal_output(arena, observed);
        if (file_write(arguments.pointer[sampling ? 11 : 9], BUSTER_SLICE_TO_BYTE_SLICE(output)))
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    string_print(S8("COMPILER_HISTORICAL_TERMINAL validation={S8} measurement_valid=false execution_authority=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("refused"));
    return result;
}
#endif
