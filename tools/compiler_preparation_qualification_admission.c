// Pure native admission for the distinct #3211 preparation qualification.
// Ownership: trusted #3217 driver; include after sampling admission helpers.
// Entry: compiler_preparation_admission_validate. Map: *_plan_parse binds
// reviewed pre-outcome source/tool/control pins; *_paths_outside checks lexical
// cleanup boundaries; *_self_test has no process, network or benchmark effects.
// Parsed fields borrow their input buffers. This policy never authenticates
// supplied transport bytes: the existing trusted API adapter/controller must
// observe owner/history facts and canonical environment paths before invoking it.
#ifndef BUSTER_COMPILER_PREPARATION_ADMISSION_INCLUDED
#define BUSTER_COMPILER_PREPARATION_ADMISSION_INCLUDED

typedef struct CompilerPreparationPlan CompilerPreparationPlan;
struct CompilerPreparationPlan
{
    String8 schema;
    String8 phase;
    String8 baseline_revision;
    String8 baseline_tree;
    String8 candidate_revision;
    String8 candidate_tree;
    String8 trusted_revision;
    String8 protocol_sha256;
    String8 lab_sha256;
    String8 python_sha256;
    String8 native_driver_sha256;
    String8 source_root;
    String8 output_root;
    String8 baseline_treatment;
    String8 candidate_treatment;
    String8 closure_policy;
    String8 toolchain_policy;
    String8 command;
    String8 lab_repetitions;
    String8 compiler_repetitions;
    String8 aa_families;
    String8 aa_primary;
    String8 aa_confidence_percent;
    String8 aa_ratio_lower;
    String8 aa_ratio_upper;
    String8 net_preparation;
    String8 physical_budget_seconds;
    String8 worker_budget_seconds;
    String8 tail_budget_seconds;
    bool valid;
};

typedef struct CompilerPreparationAdmission CompilerPreparationAdmission;
struct CompilerPreparationAdmission
{
    CompilerPreparationPlan plan;
    String8 phase;
    String8 family;
    String8 freeze_revision;
    String8 freeze_sha256;
    String8 protocol_sha256;
    String8 trusted_revision;
    String8 policy_trusted_revision;
    String8 history_since;
    String8 reason;
    u64 packet;
    u64 reservation_seconds;
    u64 worker_seconds;
    u64 tail_seconds;
    u64 timeout_minutes;
    bool valid;
};

typedef enum CompilerPreparationConfigField
{
    PREPARATION_CONFIG_SCHEMA,
    PREPARATION_CONFIG_STATE,
    PREPARATION_CONFIG_FREEZE_REVISION,
    PREPARATION_CONFIG_FREEZE_SHA,
    PREPARATION_CONFIG_PROTOCOL_SHA,
    PREPARATION_CONFIG_HISTORY_SINCE,
    PREPARATION_CONFIG_REPOSITORY,
    PREPARATION_CONFIG_OWNER_LOGIN,
    PREPARATION_CONFIG_OWNER_ID,
    PREPARATION_CONFIG_COUNT,
} CompilerPreparationConfigField;

#define BUSTER_PREPARATION_PHYSICAL_SECONDS 5400u
#define BUSTER_PREPARATION_WORKER_SECONDS 5280u
#define BUSTER_PREPARATION_TAIL_SECONDS 120u
#define BUSTER_PREPARATION_TIMEOUT_MINUTES 90u

BUSTER_GLOBAL_LOCAL CompilerPreparationPlan compiler_preparation_plan_parse(String8 text)
{
    CompilerPreparationPlan result = {0};
    String8 names[] = {S8("schema"), S8("phase"), S8("baseline_revision"), S8("baseline_tree"),
        S8("candidate_revision"), S8("candidate_tree"), S8("trusted_revision"), S8("protocol_sha256"),
        S8("lab_sha256"), S8("python_sha256"), S8("native_driver_sha256"), S8("source_root"), S8("output_root"),
        S8("baseline_treatment"), S8("candidate_treatment"), S8("closure_policy"), S8("toolchain_policy"),
        S8("command"), S8("lab_repetitions"), S8("compiler_repetitions"), S8("aa_families"), S8("aa_primary"),
        S8("aa_confidence_percent"), S8("aa_ratio_lower"), S8("aa_ratio_upper"), S8("net_preparation"),
        S8("physical_budget_seconds"), S8("worker_budget_seconds"), S8("tail_budget_seconds")};
    String8 values[BUSTER_ARRAY_LENGTH(names)] = {0};
    bool valid = compiler_sampling_admission_fields(text, (SliceString8)BUSTER_ARRAY_TO_SLICE(names), values);
    String8* outputs[] = {&result.schema, &result.phase, &result.baseline_revision, &result.baseline_tree,
        &result.candidate_revision, &result.candidate_tree, &result.trusted_revision, &result.protocol_sha256,
        &result.lab_sha256, &result.python_sha256, &result.native_driver_sha256, &result.source_root, &result.output_root,
        &result.baseline_treatment, &result.candidate_treatment, &result.closure_policy, &result.toolchain_policy,
        &result.command, &result.lab_repetitions, &result.compiler_repetitions, &result.aa_families, &result.aa_primary,
        &result.aa_confidence_percent, &result.aa_ratio_lower, &result.aa_ratio_upper, &result.net_preparation,
        &result.physical_budget_seconds, &result.worker_budget_seconds, &result.tail_budget_seconds};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(outputs); i += 1) *outputs[i] = values[i];
    valid = valid && string_equal(result.schema, S8("buster-compiler-preparation-plan-v1")) &&
        string_equal(result.phase, S8("qualify"));
    String8 revisions[] = {result.baseline_revision, result.baseline_tree, result.candidate_revision,
        result.candidate_tree, result.trusted_revision};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(revisions); i += 1)
        valid = compiler_sampling_hex(revisions[i], 40);
    String8 digests[] = {result.protocol_sha256, result.lab_sha256, result.python_sha256, result.native_driver_sha256};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(digests); i += 1)
        valid = compiler_sampling_hex(digests[i], 64);
    valid = valid && !string_equal(result.baseline_revision, result.candidate_revision) &&
        !string_equal(result.baseline_tree, result.candidate_tree) &&
        compiler_sampling_acquisition_path(result.source_root) &&
        compiler_sampling_acquisition_path(result.output_root) &&
        string_equal(result.source_root, S8("/tmp/buster-3211-closure-source")) &&
        string_equal(result.output_root, S8("/tmp/buster-3211-closure-output")) &&
        string_equal(result.baseline_treatment, S8("legacy-rebuild")) &&
        string_equal(result.candidate_treatment, S8("snapshot-v1")) &&
        string_equal(result.closure_policy, S8("snapshot-v1")) &&
        string_equal(result.toolchain_policy, S8("clang-release-tests-off-native-v1")) &&
        string_equal(result.command, S8("compiler_closure-qualify-v1")) &&
        string_equal(result.lab_repetitions, S8("5")) && string_equal(result.compiler_repetitions, S8("5")) &&
        string_equal(result.aa_families, S8("3")) && string_equal(result.aa_primary, S8("wall")) &&
        string_equal(result.aa_confidence_percent, S8("95")) &&
        string_equal(result.aa_ratio_lower, S8("0.995")) && string_equal(result.aa_ratio_upper, S8("1.005")) &&
        string_equal(result.net_preparation, S8("snapshot-less-than-legacy")) &&
        string_equal(result.physical_budget_seconds, S8("5400")) &&
        string_equal(result.worker_budget_seconds, S8("5280")) &&
        string_equal(result.tail_budget_seconds, S8("120"));
    result.valid = valid;
    return result;
}

// Lexical disjointness is necessary but does not attest canonical filesystem
// identity or freshness. The native controller observes those before claiming
// or fetching; arbitrary workflow arguments cannot supply that authority.
BUSTER_GLOBAL_LOCAL bool compiler_preparation_plan_paths_outside(CompilerPreparationPlan plan,
    String8 cleanup_root, String8 workspace_root)
{
    bool result = plan.valid && compiler_sampling_acquisition_path(cleanup_root) &&
        compiler_sampling_acquisition_path(workspace_root);
    String8 retained[] = {plan.source_root, plan.output_root};
    String8 cleaned[] = {cleanup_root, workspace_root};
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(retained); i += 1)
    {
        for (u64 j = 0; result && j < BUSTER_ARRAY_LENGTH(cleaned); j += 1)
            result = !compiler_sampling_acquisition_path_within(retained[i], cleaned[j]) &&
                !compiler_sampling_acquisition_path_within(cleaned[j], retained[i]);
    }
    result = result && !compiler_sampling_acquisition_path_within(plan.source_root, plan.output_root) &&
        !compiler_sampling_acquisition_path_within(plan.output_root, plan.source_root);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerPreparationAdmission compiler_preparation_admission_validate(Arena* arena,
    String8 allowlist, String8 marker, String8 facts_text, String8 history, String8 plan_text,
    String8 cleanup_root, String8 workspace_root)
{
    CompilerPreparationAdmission result = {.reason = S8("disabled-or-invalid-preparation-admission")};
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"),
        S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config[PREPARATION_CONFIG_COUNT] = {0};
    bool valid = compiler_sampling_admission_fields(allowlist, (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names), config) &&
        string_equal(config[PREPARATION_CONFIG_SCHEMA], S8("buster-compiler-preparation-admission-v1")) &&
        string_equal(config[PREPARATION_CONFIG_STATE], S8("qualify")) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_FREEZE_REVISION], 40) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_FREEZE_SHA], 64) &&
        compiler_sampling_hex(config[PREPARATION_CONFIG_PROTOCOL_SHA], 64) &&
        compiler_sampling_admission_timestamp(config[PREPARATION_CONFIG_HISTORY_SINCE]) &&
        string_equal(config[PREPARATION_CONFIG_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(config[PREPARATION_CONFIG_OWNER_LOGIN], S8("davidgmbb")) &&
        string_equal(config[PREPARATION_CONFIG_OWNER_ID], S8("39247043"));
    CompilerPreparationPlan plan = valid ? compiler_preparation_plan_parse(plan_text) : (CompilerPreparationPlan){0};
    valid = valid && plan.valid && compiler_preparation_plan_paths_outside(plan, cleanup_root, workspace_root) &&
        string_equal(plan.protocol_sha256, config[PREPARATION_CONFIG_PROTOCOL_SHA]);
    if (valid)
    {
        String8 digest = stage_object_sha256_bytes(arena, (u8*)plan_text.pointer, plan_text.length);
        valid = string_equal(digest, config[PREPARATION_CONFIG_FREEZE_SHA]);
    }
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 facts[SAMPLING_FACT_COUNT] = {0};
    valid = valid && compiler_sampling_admission_fields(facts_text, (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names), facts) &&
        string_equal(facts[SAMPLING_FACT_SCHEMA], S8("buster-main-sampling-github-facts-v1")) &&
        string_equal(facts[SAMPLING_FACT_PULL_STATE], S8("open")) &&
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
        valid = string_equal(facts[repositories[i]], config[PREPARATION_CONFIG_REPOSITORY]);
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
        valid = string_equal(facts[role], config[PREPARATION_CONFIG_OWNER_LOGIN]) &&
            string_equal(facts[role + 1], config[PREPARATION_CONFIG_OWNER_ID]);
    if (marker.length && marker.pointer && marker.pointer[marker.length - 1] == '\n')
        marker = string_slice(marker, 0, marker.length - 1);
    String8 expected = valid ? string_format(arena,
        S8("profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: {S8}"),
        config[PREPARATION_CONFIG_FREEZE_REVISION]) : (String8){0};
    valid = valid && string_equal(marker, expected) &&
        string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
        (parent_count == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
            string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
    // Reuse only the existing first-claim boundary: acquire/packet0 requires
    // the exact 16-column history header and rejects EVERY prior row, even
    // failed, cancelled, hostless or incomplete attempts. Its 1800-second
    // sampling schedule is not the preparation budget returned below.
    String8 history_config[SAMPLING_CONFIG_COUNT] = {0};
    history_config[SAMPLING_CONFIG_OWNER_LOGIN] = config[PREPARATION_CONFIG_OWNER_LOGIN];
    history_config[SAMPLING_CONFIG_OWNER_ID] = config[PREPARATION_CONFIG_OWNER_ID];
    valid = valid && compiler_sampling_admission_history(history, history_config, facts, S8("acquire"), 0,
        config[PREPARATION_CONFIG_FREEZE_SHA], config[PREPARATION_CONFIG_FREEZE_REVISION]);
    if (valid)
    {
        result.plan = plan;
        result.phase = S8("qualify");
        result.family = S8("preparation");
        result.freeze_revision = config[PREPARATION_CONFIG_FREEZE_REVISION];
        result.freeze_sha256 = config[PREPARATION_CONFIG_FREEZE_SHA];
        result.protocol_sha256 = plan.protocol_sha256;
        result.trusted_revision = plan.trusted_revision;
        result.policy_trusted_revision = facts[SAMPLING_FACT_TRUSTED_REVISION];
        result.history_since = config[PREPARATION_CONFIG_HISTORY_SINCE];
        result.reservation_seconds = BUSTER_PREPARATION_PHYSICAL_SECONDS;
        result.worker_seconds = BUSTER_PREPARATION_WORKER_SECONDS;
        result.tail_seconds = BUSTER_PREPARATION_TAIL_SECONDS;
        result.timeout_minutes = BUSTER_PREPARATION_TIMEOUT_MINUTES;
        result.reason = S8("authenticated-bounded-preparation-only");
    }
    result.valid = valid;
    return result;
}

#include "compiler_preparation_qualification_admission_test.h"
#endif
