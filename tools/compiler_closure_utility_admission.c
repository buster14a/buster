// Pure native admission for the distinct #3211 closure utility qualification.
// Ownership: trusted #3217 driver; include after sampling admission helpers.
// Entry: compiler_closure_utility_admission_validate. Map: *_plan_parse binds
// reviewed pre-outcome source/tool/control pins; *_paths_outside checks lexical
// cleanup boundaries; *_self_test has no process, network or benchmark effects.
// Parsed fields borrow their input buffers. This policy never authenticates
// supplied transport bytes: the existing trusted API adapter/controller must
// observe owner/history facts and canonical environment paths before invoking it.
#ifndef BUSTER_COMPILER_CLOSURE_UTILITY_ADMISSION_INCLUDED
#define BUSTER_COMPILER_CLOSURE_UTILITY_ADMISSION_INCLUDED

typedef struct CompilerClosureUtilityPlan CompilerClosureUtilityPlan;
struct CompilerClosureUtilityPlan
{
    String8 schema;
    String8 phase;
    String8 baseline_revision;
    String8 baseline_tree;
    String8 candidate_revision;
    String8 candidate_tree;
    String8 pull_head;
    String8 trusted_revision;
    String8 trusted_root;
    String8 protocol_sha256;
    String8 lab_sha256;
    String8 comparator_sha256;
    String8 receipt_sha256;
    String8 owned_phase_sha256;
    String8 python_sha256;
    String8 python_path;
    String8 native_driver_sha256;
    String8 source_root;
    String8 output_root;
    String8 legacy_treatment;
    String8 snapshot_treatment;
    String8 toolchain_policy;
    String8 command;
    String8 mode;
    String8 legs;
    String8 leg_order;
    String8 compare_profile;
    String8 lab_target_minutes;
    String8 lab_warmups;
    String8 lab_cpu;
    String8 lab_profile_steps;
    String8 throughput_profile;
    String8 throughput_arguments;
    String8 throughput_cells;
    String8 throughput_rounds;
    String8 leg_clock_scope;
    String8 cache_policy;
    String8 utility_charge_policy;
    String8 utility_criterion;
    String8 physical_budget_seconds;
    String8 worker_budget_seconds;
    String8 tail_budget_seconds;
    String8 study_budget_seconds;
    bool valid;
};

typedef struct CompilerClosureUtilityAdmission CompilerClosureUtilityAdmission;
struct CompilerClosureUtilityAdmission
{
    CompilerClosureUtilityPlan plan;
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

typedef enum CompilerClosureUtilityConfigField
{
    CLOSURE_UTILITY_CONFIG_SCHEMA,
    CLOSURE_UTILITY_CONFIG_STATE,
    CLOSURE_UTILITY_CONFIG_FREEZE_REVISION,
    CLOSURE_UTILITY_CONFIG_FREEZE_SHA,
    CLOSURE_UTILITY_CONFIG_PROTOCOL_SHA,
    CLOSURE_UTILITY_CONFIG_HISTORY_SINCE,
    CLOSURE_UTILITY_CONFIG_REPOSITORY,
    CLOSURE_UTILITY_CONFIG_OWNER_LOGIN,
    CLOSURE_UTILITY_CONFIG_OWNER_ID,
    CLOSURE_UTILITY_CONFIG_COUNT,
} CompilerClosureUtilityConfigField;

#define BUSTER_CLOSURE_UTILITY_PHYSICAL_SECONDS 5400u
#define BUSTER_CLOSURE_UTILITY_WORKER_SECONDS 5280u
#define BUSTER_CLOSURE_UTILITY_TAIL_SECONDS 120u
#define BUSTER_CLOSURE_UTILITY_TIMEOUT_MINUTES 90u

BUSTER_GLOBAL_LOCAL CompilerClosureUtilityPlan compiler_closure_utility_plan_parse(String8 text)
{
    CompilerClosureUtilityPlan result = {0};
    String8 names[] = {
        S8("schema"), S8("phase"), S8("baseline_revision"), S8("baseline_tree"),
        S8("candidate_revision"), S8("candidate_tree"), S8("pull_head"), S8("trusted_revision"),
        S8("trusted_root"), S8("protocol_sha256"), S8("lab_sha256"), S8("comparator_sha256"),
        S8("receipt_sha256"), S8("owned_phase_sha256"), S8("python_sha256"), S8("python_path"),
        S8("native_driver_sha256"), S8("source_root"), S8("output_root"), S8("legacy_treatment"),
        S8("snapshot_treatment"), S8("toolchain_policy"), S8("command"), S8("mode"),
        S8("legs"), S8("leg_order"), S8("compare_profile"), S8("lab_target_minutes"),
        S8("lab_warmups"), S8("lab_cpu"), S8("lab_profile_steps"), S8("throughput_profile"),
        S8("throughput_arguments"), S8("throughput_cells"), S8("throughput_rounds"), S8("leg_clock_scope"),
        S8("cache_policy"), S8("utility_charge_policy"), S8("utility_criterion"), S8("physical_budget_seconds"),
        S8("worker_budget_seconds"), S8("tail_budget_seconds"), S8("study_budget_seconds")};
    String8 values[BUSTER_ARRAY_LENGTH(names)] = {0};
    bool valid = compiler_sampling_admission_fields(text, (SliceString8)BUSTER_ARRAY_TO_SLICE(names), values);
    String8* outputs[] = {
        &result.schema, &result.phase, &result.baseline_revision, &result.baseline_tree,
        &result.candidate_revision, &result.candidate_tree, &result.pull_head, &result.trusted_revision,
        &result.trusted_root, &result.protocol_sha256, &result.lab_sha256, &result.comparator_sha256,
        &result.receipt_sha256, &result.owned_phase_sha256, &result.python_sha256, &result.python_path,
        &result.native_driver_sha256, &result.source_root, &result.output_root, &result.legacy_treatment,
        &result.snapshot_treatment, &result.toolchain_policy, &result.command, &result.mode,
        &result.legs, &result.leg_order, &result.compare_profile, &result.lab_target_minutes,
        &result.lab_warmups, &result.lab_cpu, &result.lab_profile_steps, &result.throughput_profile,
        &result.throughput_arguments, &result.throughput_cells, &result.throughput_rounds, &result.leg_clock_scope,
        &result.cache_policy, &result.utility_charge_policy, &result.utility_criterion, &result.physical_budget_seconds,
        &result.worker_budget_seconds, &result.tail_budget_seconds, &result.study_budget_seconds};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(outputs); i += 1) *outputs[i] = values[i];
    String8 revisions[] = {result.baseline_revision, result.baseline_tree, result.candidate_revision, result.candidate_tree, result.pull_head, result.trusted_revision};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(revisions); i += 1)
        valid = compiler_sampling_hex(revisions[i], 40);
    String8 digests[] = {result.protocol_sha256, result.lab_sha256, result.comparator_sha256, result.receipt_sha256, result.owned_phase_sha256, result.python_sha256, result.native_driver_sha256};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(digests); i += 1)
        valid = compiler_sampling_hex(digests[i], 64);
    String8 paths[] = {result.source_root, result.output_root, result.trusted_root, result.python_path};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(paths); i += 1)
        valid = compiler_sampling_acquisition_path(paths[i]);
    valid = valid && !string_equal(result.baseline_revision, result.candidate_revision) &&
        !string_equal(result.baseline_tree, result.candidate_tree);
    String8 controls[] = {result.schema, result.phase, result.source_root, result.output_root, result.legacy_treatment, result.snapshot_treatment, result.toolchain_policy, result.command, result.mode, result.legs, result.leg_order, result.compare_profile, result.lab_target_minutes, result.lab_warmups, result.lab_cpu, result.lab_profile_steps, result.throughput_profile, result.throughput_arguments, result.throughput_cells, result.throughput_rounds, result.leg_clock_scope, result.cache_policy, result.utility_charge_policy, result.utility_criterion, result.physical_budget_seconds, result.worker_budget_seconds, result.tail_budget_seconds, result.study_budget_seconds};
    String8 expected[] = {S8("buster-compiler-closure-utility-plan-v1"), S8("utility"), S8("/tmp/buster-3211-utility-source"), S8("/tmp/buster-3211-utility-output"), S8("legacy-rebuild"), S8("snapshot-v1"), S8("clang-release-tests-off-native-v1"), S8("compiler-closure-utility-v1"), S8("main"), S8("2"), S8("legacy-then-snapshot"), S8("compiler-compare-v1"), S8("10"), S8("1"), S8("2"), S8("none"), S8("throughput-corpus-v2"), S8("ci-all-p20-w2-t120-cpu2"), S8("12"), S8("2"), S8("bootstrap-through-export-hashfinalization"), S8("fresh-mutable-per-leg"), S8("all-physical-residual-to-snapshot"), S8("snapshot-plus-residual-less-than-legacy"), S8("5400"), S8("5280"), S8("120"), S8("10800")};
    for (u64 i = 0; valid && i < BUSTER_ARRAY_LENGTH(controls); i += 1)
        valid = string_equal(controls[i], expected[i]);
    result.valid = valid;
    return result;
}

// Lexical disjointness is necessary but does not attest canonical filesystem
// identity or freshness. The native controller observes those before claiming
// or fetching; arbitrary workflow arguments cannot supply that authority.
BUSTER_GLOBAL_LOCAL bool compiler_closure_utility_plan_paths_outside(CompilerClosureUtilityPlan plan,
    String8 cleanup_root, String8 workspace_root)
{
    bool result = plan.valid && compiler_sampling_acquisition_path(cleanup_root) &&
        compiler_sampling_acquisition_path(workspace_root);
    String8 retained[] = {plan.source_root, plan.output_root};
    String8 qualified[] = {S8("/tmp/buster-3211-closure-source"), S8("/tmp/buster-3211-closure-output")};
    String8 cleaned[] = {cleanup_root, workspace_root};
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(retained); i += 1)
    {
        for (u64 j = 0; result && j < BUSTER_ARRAY_LENGTH(cleaned); j += 1)
            result = !compiler_sampling_acquisition_path_within(retained[i], cleaned[j]) &&
                !compiler_sampling_acquisition_path_within(cleaned[j], retained[i]);
    }
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(retained); i += 1)
        result = !compiler_sampling_acquisition_path_within(retained[i], plan.trusted_root) &&
            !compiler_sampling_acquisition_path_within(plan.trusted_root, retained[i]);
    for (u64 i = 0; result && i < BUSTER_ARRAY_LENGTH(qualified); i += 1)
    {
        result = !compiler_sampling_acquisition_path_within(plan.trusted_root, qualified[i]) &&
            !compiler_sampling_acquisition_path_within(qualified[i], plan.trusted_root);
        for (u64 j = 0; result && j < BUSTER_ARRAY_LENGTH(retained); j += 1)
            result = !compiler_sampling_acquisition_path_within(retained[j], qualified[i]) &&
                !compiler_sampling_acquisition_path_within(qualified[i], retained[j]);
    }
    result = result && !compiler_sampling_acquisition_path_within(plan.source_root, plan.output_root) &&
        !compiler_sampling_acquisition_path_within(plan.output_root, plan.source_root);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerClosureUtilityAdmission compiler_closure_utility_admission_validate(Arena* arena,
    String8 allowlist, String8 marker, String8 facts_text, String8 history, String8 plan_text,
    String8 cleanup_root, String8 workspace_root)
{
    CompilerClosureUtilityAdmission result = {.reason = S8("disabled-or-invalid-utility-admission")};
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"),
        S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config[CLOSURE_UTILITY_CONFIG_COUNT] = {0};
    bool valid = compiler_sampling_admission_fields(allowlist, (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names), config) &&
        string_equal(config[CLOSURE_UTILITY_CONFIG_SCHEMA], S8("buster-compiler-closure-utility-admission-v1")) &&
        string_equal(config[CLOSURE_UTILITY_CONFIG_STATE], S8("utility")) &&
        compiler_sampling_hex(config[CLOSURE_UTILITY_CONFIG_FREEZE_REVISION], 40) &&
        compiler_sampling_hex(config[CLOSURE_UTILITY_CONFIG_FREEZE_SHA], 64) &&
        compiler_sampling_hex(config[CLOSURE_UTILITY_CONFIG_PROTOCOL_SHA], 64) &&
        compiler_sampling_admission_timestamp(config[CLOSURE_UTILITY_CONFIG_HISTORY_SINCE]) &&
        string_equal(config[CLOSURE_UTILITY_CONFIG_REPOSITORY], S8("buster14a/buster")) &&
        string_equal(config[CLOSURE_UTILITY_CONFIG_OWNER_LOGIN], S8("davidgmbb")) &&
        string_equal(config[CLOSURE_UTILITY_CONFIG_OWNER_ID], S8("39247043"));
    CompilerClosureUtilityPlan plan = valid ? compiler_closure_utility_plan_parse(plan_text) : (CompilerClosureUtilityPlan){0};
    valid = valid && plan.valid && compiler_closure_utility_plan_paths_outside(plan, cleanup_root, workspace_root) &&
        string_equal(plan.protocol_sha256, config[CLOSURE_UTILITY_CONFIG_PROTOCOL_SHA]);
    if (valid)
    {
        String8 digest = stage_object_sha256_bytes(arena, (u8*)plan_text.pointer, plan_text.length);
        valid = string_equal(digest, config[CLOSURE_UTILITY_CONFIG_FREEZE_SHA]);
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
        valid = string_equal(facts[repositories[i]], config[CLOSURE_UTILITY_CONFIG_REPOSITORY]);
    for (u64 role = SAMPLING_FACT_OWNER_LOGIN; valid && role <= SAMPLING_FACT_PULL_AUTHOR_LOGIN; role += 2)
        valid = string_equal(facts[role], config[CLOSURE_UTILITY_CONFIG_OWNER_LOGIN]) &&
            string_equal(facts[role + 1], config[CLOSURE_UTILITY_CONFIG_OWNER_ID]);
    if (marker.length && marker.pointer && marker.pointer[marker.length - 1] == '\n')
        marker = string_slice(marker, 0, marker.length - 1);
    String8 expected = valid ? string_format(arena,
        S8("profile: compiler-baseline-closure-utility-v1 packet: 0 freeze: {S8}"),
        config[CLOSURE_UTILITY_CONFIG_FREEZE_REVISION]) : (String8){0};
    valid = valid && string_equal(marker, expected) &&
        string_equal(facts[SAMPLING_FACT_FRESH_PARENT_0], marker) &&
        (parent_count == 2 ? string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], marker) :
            string_equal(facts[SAMPLING_FACT_FRESH_PARENT_1], S8("-")));
    // Reuse only the existing first-claim boundary: acquire/packet0 requires
    // the exact 16-column history header and rejects EVERY prior row, even
    // failed, cancelled, hostless or incomplete attempts. Its 1800-second
    // sampling schedule is not the utility budget returned below.
    String8 history_config[SAMPLING_CONFIG_COUNT] = {0};
    history_config[SAMPLING_CONFIG_OWNER_LOGIN] = config[CLOSURE_UTILITY_CONFIG_OWNER_LOGIN];
    history_config[SAMPLING_CONFIG_OWNER_ID] = config[CLOSURE_UTILITY_CONFIG_OWNER_ID];
    valid = valid && compiler_sampling_admission_history(history, history_config, facts, S8("acquire"), 0,
        config[CLOSURE_UTILITY_CONFIG_FREEZE_SHA], config[CLOSURE_UTILITY_CONFIG_FREEZE_REVISION]);
    if (valid)
    {
        result.plan = plan;
        result.phase = S8("utility");
        result.family = S8("utility");
        result.freeze_revision = config[CLOSURE_UTILITY_CONFIG_FREEZE_REVISION];
        result.freeze_sha256 = config[CLOSURE_UTILITY_CONFIG_FREEZE_SHA];
        result.protocol_sha256 = plan.protocol_sha256;
        result.trusted_revision = plan.trusted_revision;
        result.policy_trusted_revision = facts[SAMPLING_FACT_TRUSTED_REVISION];
        result.history_since = config[CLOSURE_UTILITY_CONFIG_HISTORY_SINCE];
        result.reservation_seconds = BUSTER_CLOSURE_UTILITY_PHYSICAL_SECONDS;
        result.worker_seconds = BUSTER_CLOSURE_UTILITY_WORKER_SECONDS;
        result.tail_seconds = BUSTER_CLOSURE_UTILITY_TAIL_SECONDS;
        result.timeout_minutes = BUSTER_CLOSURE_UTILITY_TIMEOUT_MINUTES;
        result.reason = S8("authenticated-bounded-utility-only");
    }
    result.valid = valid;
    return result;
}

#include "compiler_closure_utility_admission_test.h"
#endif
