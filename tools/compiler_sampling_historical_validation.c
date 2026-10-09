// Historical #3212 evidence validation. Include after the sampling admission,
// controller and MAIN policy helpers. This entry never admits or executes work.
// The API adapter observes immutable original attempts and commits. Native code
// validates their joins and exact committed bytes; raw ZIP replay remains data.
// Live compiler_sampling_admission_validate keeps its OPEN-only policy.
#ifndef BUSTER_COMPILER_SAMPLING_HISTORICAL_VALIDATION_INCLUDED
#define BUSTER_COMPILER_SAMPLING_HISTORICAL_VALIDATION_INCLUDED

typedef struct CompilerSamplingHistoricalValidation CompilerSamplingHistoricalValidation;
struct CompilerSamplingHistoricalValidation
{
    CompilerSamplingAdmission packet;
    String8 policy_revision;
    String8 acquisition_revision;
    String8 acquisition_sha256;
    String8 api_sha256;
    bool valid;
};

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_historical_api_names[] = {
    S8("schema"),
    S8("repository"),
    S8("policy_revision"),
    S8("policy_main_relation"),
    S8("request_run_id"),
    S8("request_run_attempt"),
    S8("request_latest_attempt"),
    S8("request_workflow"),
    S8("request_event"),
    S8("request_status"),
    S8("request_conclusion"),
    S8("request_head"),
    S8("source_commit"),
    S8("first_parent"),
    S8("second_parent"),
    S8("compare_parent_0"),
    S8("compare_head_0"),
    S8("compare_parent_1"),
    S8("compare_head_1"),
    S8("executor_run_id"),
    S8("executor_run_attempt"),
    S8("executor_latest_attempt"),
    S8("executor_workflow"),
    S8("executor_event"),
    S8("executor_branch"),
    S8("executor_head"),
    S8("executor_title"),
    S8("executor_status"),
    S8("executor_conclusion"),
    S8("executor_actor_login"),
    S8("executor_actor_id"),
    S8("executor_triggering_login"),
    S8("executor_triggering_id"),
    S8("pull_number"),
    S8("associated_pull_number"),
    S8("associated_commit"),
    S8("pull_state"),
    S8("pull_current_head"),
    S8("allowlist_sha256"),
    S8("facts_sha256"),
    S8("history_sha256"),
    S8("freeze_sha256"),
    S8("parent_freeze_sha256"),
    S8("acquisition_sha256"),
    S8("check_name"),
    S8("check_app_id"),
    S8("check_head"),
    S8("check_external_id"),
    S8("check_status"),
    S8("check_conclusion"),
    S8("check_title")
};
typedef enum CompilerSamplingHistoricalApiField
{
    HISTORICAL_SCHEMA,
    HISTORICAL_REPOSITORY,
    HISTORICAL_POLICY_REVISION,
    HISTORICAL_POLICY_MAIN_RELATION,
    HISTORICAL_REQUEST_RUN_ID,
    HISTORICAL_REQUEST_RUN_ATTEMPT,
    HISTORICAL_REQUEST_LATEST_ATTEMPT,
    HISTORICAL_REQUEST_WORKFLOW,
    HISTORICAL_REQUEST_EVENT,
    HISTORICAL_REQUEST_STATUS,
    HISTORICAL_REQUEST_CONCLUSION,
    HISTORICAL_REQUEST_HEAD,
    HISTORICAL_SOURCE_COMMIT,
    HISTORICAL_FIRST_PARENT,
    HISTORICAL_SECOND_PARENT,
    HISTORICAL_COMPARE_PARENT_0,
    HISTORICAL_COMPARE_HEAD_0,
    HISTORICAL_COMPARE_PARENT_1,
    HISTORICAL_COMPARE_HEAD_1,
    HISTORICAL_EXECUTOR_RUN_ID,
    HISTORICAL_EXECUTOR_RUN_ATTEMPT,
    HISTORICAL_EXECUTOR_LATEST_ATTEMPT,
    HISTORICAL_EXECUTOR_WORKFLOW,
    HISTORICAL_EXECUTOR_EVENT,
    HISTORICAL_EXECUTOR_BRANCH,
    HISTORICAL_EXECUTOR_HEAD,
    HISTORICAL_EXECUTOR_TITLE,
    HISTORICAL_EXECUTOR_STATUS,
    HISTORICAL_EXECUTOR_CONCLUSION,
    HISTORICAL_EXECUTOR_ACTOR_LOGIN,
    HISTORICAL_EXECUTOR_ACTOR_ID,
    HISTORICAL_EXECUTOR_TRIGGERING_LOGIN,
    HISTORICAL_EXECUTOR_TRIGGERING_ID,
    HISTORICAL_PULL_NUMBER,
    HISTORICAL_ASSOCIATED_PULL_NUMBER,
    HISTORICAL_ASSOCIATED_COMMIT,
    HISTORICAL_PULL_STATE,
    HISTORICAL_PULL_CURRENT_HEAD,
    HISTORICAL_ALLOWLIST_SHA256,
    HISTORICAL_FACTS_SHA256,
    HISTORICAL_HISTORY_SHA256,
    HISTORICAL_FREEZE_SHA256,
    HISTORICAL_PARENT_FREEZE_SHA256,
    HISTORICAL_ACQUISITION_SHA256,
    HISTORICAL_CHECK_NAME,
    HISTORICAL_CHECK_APP_ID,
    HISTORICAL_CHECK_HEAD,
    HISTORICAL_CHECK_EXTERNAL_ID,
    HISTORICAL_CHECK_STATUS,
    HISTORICAL_CHECK_CONCLUSION,
    HISTORICAL_CHECK_TITLE,
    HISTORICAL_API_COUNT,
} CompilerSamplingHistoricalApiField;

BUSTER_GLOBAL_LOCAL CompilerSamplingAdmission compiler_sampling_historical_packet(Arena* arena, String8 allowlist,
    String8 marker, String8 facts_text, String8 history, String8 freeze_text, String8 parent_freeze_text)
{
    CompilerSamplingAdmission result = {.reason = S8("invalid-historical-sampling-context")};
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"), S8("campaign_parent"),
        S8("parent_freeze_revision"), S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config[SAMPLING_CONFIG_COUNT] = {0};
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = compiler_sampling_admission_fields(allowlist, (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names), config) &&
        string_equal(config[SAMPLING_CONFIG_SCHEMA], S8("buster-main-sampling-admission-v1")) &&
        (string_equal(config[SAMPLING_CONFIG_STATE], S8("acquire")) ||
            string_equal(config[SAMPLING_CONFIG_STATE], S8("pilot")) || string_equal(config[SAMPLING_CONFIG_STATE], S8("confirm"))) &&
        string_equal(config[SAMPLING_CONFIG_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(config[SAMPLING_CONFIG_OWNER_LOGIN], S8("davidgmbb")) &&
        string_equal(config[SAMPLING_CONFIG_OWNER_ID], S8("39247043")) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_FREEZE_REVISION], 40) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_FREEZE_SHA], 64) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_PROTOCOL_SHA], 64) &&
        compiler_sampling_admission_timestamp(config[SAMPLING_CONFIG_HISTORY_SINCE]);
    bool acquiring = string_equal(config[SAMPLING_CONFIG_STATE], S8("acquire"));
    bool confirming = string_equal(config[SAMPLING_CONFIG_STATE], S8("confirm"));
    valid = valid && (acquiring ? string_equal(config[SAMPLING_CONFIG_PARENT_SHA], S8("-")) &&
        string_equal(config[SAMPLING_CONFIG_PARENT_REVISION], S8("-")) && !parent_freeze_text.length :
        compiler_sampling_hex(config[SAMPLING_CONFIG_PARENT_SHA], 64) &&
        compiler_sampling_hex(config[SAMPLING_CONFIG_PARENT_REVISION], 40));
    CompilerSamplingFreeze freeze = {0}, parent_pilot = {0};
    CompilerSamplingAcquisitionPlan acquisition = {0};
    String8 acquisition_sha = {0}, acquisition_revision = {0}, pinned_revision = {0};
    valid = valid && freeze_text.pointer && freeze_text.length && freeze_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES;
    if (valid)
    {
        String8 actual_sha = stage_object_sha256_bytes(arena, (u8*)freeze_text.pointer, freeze_text.length);
        valid = string_equal(actual_sha, config[SAMPLING_CONFIG_FREEZE_SHA]);
        if (valid && acquiring)
        {
            acquisition = compiler_sampling_acquisition_plan_parse(freeze_text);
            valid = acquisition.valid && string_equal(acquisition.protocol_sha256, config[SAMPLING_CONFIG_PROTOCOL_SHA]);
            pinned_revision = acquisition.trusted_revision;
        }
        else if (valid)
        {
            freeze = compiler_sampling_freeze_parse(freeze_text);
            valid = freeze.valid && string_equal(freeze.phase, config[SAMPLING_CONFIG_STATE]) &&
                string_equal(freeze.campaign_parent, config[SAMPLING_CONFIG_PARENT_SHA]) &&
                string_equal(freeze.campaign_parent_revision, config[SAMPLING_CONFIG_PARENT_REVISION]) &&
                string_equal(freeze.protocol_sha256, config[SAMPLING_CONFIG_PROTOCOL_SHA]) &&
                parent_freeze_text.pointer && parent_freeze_text.length &&
                parent_freeze_text.length <= BUSTER_SAMPLING_FREEZE_MAX_BYTES;
            if (valid)
            {
                String8 parent_sha = stage_object_sha256_bytes(arena, (u8*)parent_freeze_text.pointer, parent_freeze_text.length);
                valid = string_equal(parent_sha, freeze.campaign_parent) &&
                    compiler_sampling_admission_parent(freeze, parent_freeze_text, &acquisition, &parent_pilot);
            }
            pinned_revision = freeze.trusted_revision;
            acquisition_sha = confirming ? parent_pilot.campaign_parent : freeze.campaign_parent;
            acquisition_revision = confirming ? parent_pilot.campaign_parent_revision : freeze.campaign_parent_revision;
        }
    }
    valid = valid && compiler_sampling_admission_fields(facts_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names), facts) &&
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
        valid = string_equal(facts[repositories[i]], config[SAMPLING_CONFIG_REPOSITORY]);
    }
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
    {
        valid = string_equal(facts[role], config[SAMPLING_CONFIG_OWNER_LOGIN]) &&
            string_equal(facts[role + 1], config[SAMPLING_CONFIG_OWNER_ID]);
    }
    if (valid)
    {
        if (marker.length && marker.pointer[marker.length - 1] == '\n') marker = string_slice(marker, 0, marker.length - 1);
        String8 prefix = acquiring ? S8("profile: compiler-main-sampling-acquire-v1 packet: ") :
            confirming ? S8("profile: compiler-main-sampling-confirm-v1 packet: ") :
                S8("profile: compiler-main-sampling-pilot-v1 packet: ");
        valid = marker.pointer && marker.length > prefix.length && marker.length <= 256 &&
            string_equal(string_slice(marker, 0, prefix.length), prefix);
        u64 separator = prefix.length;
        while (valid && separator < marker.length && marker.pointer[separator] != ' ') separator += 1;
        String8 suffix = S8(" freeze: ");
        valid = valid && separator > prefix.length && separator + suffix.length + 40 == marker.length &&
            string_equal(string_slice(marker, separator, separator + suffix.length), suffix);
        if (valid)
        {
            valid = compiler_sampling_admission_decimal(string_slice(marker, prefix.length, separator), &result.packet) &&
                string_equal(string_slice(marker, separator + suffix.length, marker.length), config[SAMPLING_CONFIG_FREEZE_REVISION]) &&
                string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
                (parent_count == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
                    string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
        }
    }
    CompilerSamplingPacket planned = compiler_sampling_schedule(config[SAMPLING_CONFIG_STATE], result.packet);
    valid = valid && planned.valid && compiler_sampling_admission_history(history, config, facts,
        config[SAMPLING_CONFIG_STATE], result.packet, acquisition_sha, acquisition_revision);
    if (valid)
    {
        result.phase = config[SAMPLING_CONFIG_STATE];
        result.family = planned.family;
        result.reservation_seconds = planned.reservation_seconds;
        result.freeze_revision = config[SAMPLING_CONFIG_FREEZE_REVISION];
        result.freeze_sha256 = config[SAMPLING_CONFIG_FREEZE_SHA];
        result.campaign_parent = config[SAMPLING_CONFIG_PARENT_SHA];
        result.parent_freeze_revision = config[SAMPLING_CONFIG_PARENT_REVISION];
        result.protocol_sha256 = config[SAMPLING_CONFIG_PROTOCOL_SHA];
        result.trusted_revision = pinned_revision;
        result.history_since = config[SAMPLING_CONFIG_HISTORY_SINCE];
        result.reason = S8("historical-data-validation-only");
    }
    result.valid = valid;
    return result;

// The existing history validator checks the phase prefix, identities, budget
// and successful predecessors. This additional check excludes later requests
// from a retrospective replay, including a later row with an earlier slot.
BUSTER_GLOBAL_LOCAL bool compiler_sampling_historical_prefix(String8 history, u64 current)
{
    bool valid = history.length <= BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES && current > 0;
    u64 cursor = 0;
    while (valid && cursor < history.length && history.pointer[cursor] != '\n') cursor += 1;
    valid = valid && cursor < history.length;
    cursor += 1;
    u64 last = 0;
    while (valid && cursor < history.length)
    {
        u64 column = 0, begin = cursor;
        String8 request = {0};
        while (valid && cursor < history.length && history.pointer[cursor] != '\n')
        {
            if (history.pointer[cursor] == '\t')
            {
                if (column == SAMPLING_HISTORY_REQUEST_RUN)
                    request = string_slice(history, begin, cursor);
                column += 1;
                begin = cursor + 1;
            }
            cursor += 1;
        }
        u64 id = 0;
        valid = valid && cursor < history.length &&
            compiler_sampling_admission_decimal(request, &id) && id > last && id < current;
        last = id;
        cursor += 1;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_historical_digest(Arena* arena, String8 data, String8 expected)
{
    bool valid = compiler_sampling_hex(expected, 64) &&
        string_equal(stage_object_sha256_bytes(arena, data.pointer, data.length), expected);
    return valid;
}

BUSTER_GLOBAL_LOCAL CompilerSamplingHistoricalValidation compiler_sampling_historical_validate(Arena* arena,
    String8 allowlist, String8 marker, String8 facts_text, String8 history, String8 freeze_text,
    String8 parent_text, String8 acquisition_text, String8 api_text)
{
    CompilerSamplingHistoricalValidation result = {0};
    String8 api[HISTORICAL_API_COUNT] = {0};
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    bool valid = api_text.length <= 32768 &&
        compiler_main_fields(api_text,
            (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names), api) &&
        compiler_sampling_admission_fields(facts_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names), facts) &&
        string_equal(api[HISTORICAL_SCHEMA], S8("buster-main-sampling-historical-api-v1")) &&
        string_equal(api[HISTORICAL_REPOSITORY], S8("buster14a/buster")) &&
        compiler_sampling_hex(api[HISTORICAL_POLICY_REVISION], 40) &&
        (string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("identical")) ||
            string_equal(api[HISTORICAL_POLICY_MAIN_RELATION], S8("ahead"))) &&
        string_equal(api[HISTORICAL_POLICY_REVISION], facts[SAMPLING_FACT_TRUSTED_REVISION]) &&
        string_equal(api[HISTORICAL_EXECUTOR_HEAD], api[HISTORICAL_POLICY_REVISION]);
    u64 joins[][2] = {
        {HISTORICAL_REQUEST_RUN_ID, SAMPLING_FACT_REQUEST_RUN},
        {HISTORICAL_REQUEST_RUN_ATTEMPT, SAMPLING_FACT_REQUEST_ATTEMPT},
        {HISTORICAL_REQUEST_HEAD, SAMPLING_FACT_REQUEST_HEAD},
        {HISTORICAL_EXECUTOR_RUN_ID, SAMPLING_FACT_EXECUTOR_RUN},
        {HISTORICAL_EXECUTOR_RUN_ATTEMPT, SAMPLING_FACT_EXECUTOR_ATTEMPT},
        {HISTORICAL_PULL_STATE, SAMPLING_FACT_PULL_STATE}};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(joins); i += 1)
        valid = string_equal(api[joins[i][0]], facts[joins[i][1]]);
    u64 attempts[] = {HISTORICAL_REQUEST_RUN_ATTEMPT,HISTORICAL_REQUEST_LATEST_ATTEMPT,
        HISTORICAL_EXECUTOR_RUN_ATTEMPT,HISTORICAL_EXECUTOR_LATEST_ATTEMPT};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(attempts); i += 1)
        valid = string_equal(api[attempts[i]], S8("1"));
    valid = valid && string_equal(api[HISTORICAL_REQUEST_WORKFLOW], S8(".github/workflows/9700x-direct-request.yml")) &&
        string_equal(api[HISTORICAL_REQUEST_EVENT], S8("pull_request")) &&
        string_equal(api[HISTORICAL_REQUEST_STATUS], S8("completed")) &&
        string_equal(api[HISTORICAL_REQUEST_CONCLUSION], S8("success")) &&
        string_equal(api[HISTORICAL_EXECUTOR_WORKFLOW], S8(".github/workflows/9700x-direct-bench.yml")) &&
        string_equal(api[HISTORICAL_EXECUTOR_EVENT], S8("workflow_run")) &&
        string_equal(api[HISTORICAL_EXECUTOR_BRANCH], S8("main")) &&
        string_equal(api[HISTORICAL_EXECUTOR_STATUS], S8("completed")) &&
        string_equal(api[HISTORICAL_EXECUTOR_CONCLUSION], S8("success"));
    u64 actors[] = {HISTORICAL_EXECUTOR_ACTOR_LOGIN,HISTORICAL_EXECUTOR_TRIGGERING_LOGIN};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(actors); i += 1)
        valid = string_equal(api[actors[i]], S8("davidgmbb")) &&
            string_equal(api[actors[i] + 1], S8("39247043"));
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
        valid = compiler_sampling_hex(api[HISTORICAL_SECOND_PARENT], 40) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_REQUEST_HEAD]) &&
            !string_equal(api[HISTORICAL_SECOND_PARENT], api[HISTORICAL_FIRST_PARENT]) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], api[HISTORICAL_SECOND_PARENT]) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], api[HISTORICAL_REQUEST_HEAD]);
    else if (valid)
        valid = string_equal(api[HISTORICAL_SECOND_PARENT], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_PARENT_1], S8("-")) &&
            string_equal(api[HISTORICAL_COMPARE_HEAD_1], S8("-"));
    String8 title = string_format(arena, S8("9700X request {S8}.1 head {S8}"),
        api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_REQUEST_HEAD]);
    valid = valid && string_equal(api[HISTORICAL_EXECUTOR_TITLE], title) &&
        compiler_sampling_historical_digest(arena, allowlist, api[HISTORICAL_ALLOWLIST_SHA256]) &&
        compiler_sampling_historical_digest(arena, facts_text, api[HISTORICAL_FACTS_SHA256]) &&
        compiler_sampling_historical_digest(arena, history, api[HISTORICAL_HISTORY_SHA256]) &&
        compiler_sampling_historical_digest(arena, freeze_text, api[HISTORICAL_FREEZE_SHA256]) &&
        compiler_sampling_historical_digest(arena, acquisition_text, api[HISTORICAL_ACQUISITION_SHA256]) &&
        compiler_sampling_historical_prefix(history, request_id);
    CompilerSamplingAdmission packet = valid ? compiler_sampling_historical_packet(arena,
        allowlist, marker, facts_text, history, freeze_text, parent_text) : (CompilerSamplingAdmission){0};
    valid = valid && packet.valid && string_equal(api[HISTORICAL_FREEZE_SHA256], packet.freeze_sha256);
    bool acquiring = string_equal(packet.phase, S8("acquire"));
    CompilerSamplingFreeze frozen = acquiring ? (CompilerSamplingFreeze){0} : compiler_sampling_freeze_parse(freeze_text);
    CompilerSamplingFreeze parent = string_equal(packet.phase, S8("confirm")) ?
        compiler_sampling_freeze_parse(parent_text) : (CompilerSamplingFreeze){0};
    String8 acquisition_revision = acquiring ? packet.freeze_revision :
        string_equal(packet.phase, S8("pilot")) ? packet.parent_freeze_revision : parent.campaign_parent_revision;
    String8 acquisition_sha = acquiring ? packet.freeze_sha256 :
        string_equal(packet.phase, S8("pilot")) ? packet.campaign_parent : parent.campaign_parent;
    valid = valid && (acquiring ? !parent_text.length &&
        string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], S8("-")) :
        compiler_sampling_historical_digest(arena, parent_text, api[HISTORICAL_PARENT_FREEZE_SHA256]) &&
            string_equal(api[HISTORICAL_PARENT_FREEZE_SHA256], packet.campaign_parent)) &&
        compiler_sampling_hex(acquisition_revision, 40) &&
        string_equal(api[HISTORICAL_ACQUISITION_SHA256], acquisition_sha);
    CompilerSamplingAcquisitionPlan plan = valid ? compiler_sampling_acquisition_plan_parse(acquisition_text) :
        (CompilerSamplingAcquisitionPlan){0};
    valid = valid && plan.valid && string_equal(plan.trusted_revision, packet.trusted_revision) &&
        string_equal(plan.protocol_sha256, packet.protocol_sha256);
    if (valid && !acquiring)
        valid = string_equal(plan.base, frozen.base) && string_equal(plan.base_tree, frozen.base_tree) &&
            string_equal(plan.baseline_revision, frozen.baseline_revision) &&
            string_equal(plan.ab1_revision, frozen.ab1_revision) && string_equal(plan.ab2_revision, frozen.ab2_revision);
    String8 check_marker = string_format(arena, S8("buster-main-sampling-v1:{S8}:{S8}:{u64}:{S8}:{S8}:1"),
        packet.freeze_sha256, packet.phase, packet.packet, api[HISTORICAL_REQUEST_RUN_ID], api[HISTORICAL_EXECUTOR_RUN_ID]);
    valid = valid && string_equal(api[HISTORICAL_CHECK_NAME], S8("9700X compiler sampling research")) &&
        string_equal(api[HISTORICAL_CHECK_APP_ID], S8("15368")) &&
        string_equal(api[HISTORICAL_CHECK_HEAD], api[HISTORICAL_REQUEST_HEAD]) &&
        string_equal(api[HISTORICAL_CHECK_EXTERNAL_ID], check_marker) &&
        string_equal(api[HISTORICAL_CHECK_STATUS], S8("completed")) &&
        string_equal(api[HISTORICAL_CHECK_CONCLUSION], S8("success")) &&
        string_equal(api[HISTORICAL_CHECK_TITLE], S8("Valid unqualified sampling packet"));
    if (valid)
    {
        result.packet = packet;
        result.policy_revision = api[HISTORICAL_POLICY_REVISION];
        result.acquisition_revision = acquisition_revision;
        result.acquisition_sha256 = acquisition_sha;
        result.api_sha256 = stage_object_sha256_bytes(arena, api_text.pointer, api_text.length);
    }
    result.valid = valid;
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_sampling_historical_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool valid = arguments.length == 10 && string_equal(arguments.pointer[0], S8("--validate-historical-sampling"));
    String8 data[8] = {0};
    u64 limits[] = {16384,512,16384,BUSTER_SAMPLING_ADMISSION_HISTORY_MAX_BYTES,
        BUSTER_SAMPLING_FREEZE_MAX_BYTES,BUSTER_SAMPLING_FREEZE_MAX_BYTES,BUSTER_SAMPLING_FREEZE_MAX_BYTES,32768};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(data); i += 1)
        valid = compiler_main_route_read(arena, arguments.pointer[i + 1], limits[i], &data[i]);
    CompilerSamplingHistoricalValidation observed = valid ? compiler_sampling_historical_validate(arena,
        data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7]) : (CompilerSamplingHistoricalValidation){0};
    if (observed.valid)
    {
        CompilerSamplingAdmission packet = observed.packet;
        CompilerSamplingAcquisitionPlan plan = compiler_sampling_acquisition_plan_parse(data[6]);
        String8 candidate = string_equal(packet.family, S8("aa")) ? plan.base :
            string_equal(packet.family, S8("ab2")) ? plan.ab2_revision : plan.ab1_revision;
        String8 output = string_format(arena,
            S8("sampling_historical_valid=true\nsampling_policy_revision={S8}\nsampling_phase={S8}\n"
               "sampling_packet={u64}\nsampling_family={S8}\nsampling_reservation_seconds={u64}\n"
               "sampling_timeout_minutes={u64}\nsampling_freeze_revision={S8}\nsampling_freeze_sha256={S8}\n"
               "sampling_campaign_parent={S8}\nsampling_parent_freeze_revision={S8}\nsampling_protocol_sha256={S8}\n"
               "sampling_base={S8}\nsampling_base_tree={S8}\nsampling_candidate_revision={S8}\n"
               "sampling_trusted_revision={S8}\nsampling_acquisition_revision={S8}\nsampling_acquisition_sha256={S8}\n"
               "sampling_historical_api_sha256={S8}\nsampling_historical_qualification=unqualified\n"
               "sampling_historical_execution_authority=false\n"),
            observed.policy_revision,packet.phase,packet.packet,packet.family,packet.reservation_seconds,
            packet.reservation_seconds / 60,packet.freeze_revision,packet.freeze_sha256,
            packet.campaign_parent,packet.parent_freeze_revision,packet.protocol_sha256,
            plan.base,plan.base_tree,candidate,packet.trusted_revision,
            observed.acquisition_revision,observed.acquisition_sha256,observed.api_sha256);
        if (file_write(arguments.pointer[9], BUSTER_SLICE_TO_BYTE_SLICE(output))) result = PROCESS_RESULT_SUCCESS;
    }
    string_print(S8("COMPILER_SAMPLING_HISTORICAL validation={S8} qualification=unqualified execution_authority=false\n"),
        result == PROCESS_RESULT_SUCCESS ? S8("complete") : S8("refused"));
    return result;
}
#endif
