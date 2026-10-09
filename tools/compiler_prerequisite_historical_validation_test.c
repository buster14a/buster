// Native historical prerequisite controls. Include after the prerequisite
// validator and existing native admission fixture helpers. All data below is
// synthetic diagnostics; no source execution, network, host work or admission.
#ifndef BUSTER_COMPILER_PREREQUISITE_HISTORICAL_VALIDATION_TEST_INCLUDED
#define BUSTER_COMPILER_PREREQUISITE_HISTORICAL_VALIDATION_TEST_INCLUDED

typedef struct CompilerPrerequisiteHistoricalFixture CompilerPrerequisiteHistoricalFixture;
struct CompilerPrerequisiteHistoricalFixture
{
    String8 config_values[PREPARATION_CONFIG_COUNT];
    String8 fact_values[SAMPLING_FACT_COUNT];
    String8 api[HISTORICAL_API_COUNT];
    String8 config;
    String8 marker;
    String8 facts;
    String8 history;
    String8 plan;
    String8 proof;
    bool utility;
};

BUSTER_GLOBAL_LOCAL void compiler_prerequisite_historical_fixture_refresh(Arena* arena,
    CompilerPrerequisiteHistoricalFixture* fixture)
{
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"),
        S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    fixture->config = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names),
        (SliceString8){fixture->config_values, PREPARATION_CONFIG_COUNT});
    fixture->facts = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_prerequisite_historical_fact_names),
        (SliceString8){fixture->fact_values, SAMPLING_FACT_COUNT});
    String8 bytes[] = {fixture->config, fixture->facts, fixture->history, fixture->plan};
    u64 fields[] = {HISTORICAL_ALLOWLIST_SHA256, HISTORICAL_FACTS_SHA256,
        HISTORICAL_HISTORY_SHA256, HISTORICAL_FREEZE_SHA256};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bytes); i += 1)
    {
        fixture->api[fields[i]] = stage_object_sha256_bytes(arena, (u8*)bytes[i].pointer, bytes[i].length);
    }
    fixture->proof = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names),
        (SliceString8){fixture->api, HISTORICAL_API_COUNT});
    return;
}

BUSTER_GLOBAL_LOCAL CompilerPrerequisiteHistoricalFixture compiler_prerequisite_historical_fixture(Arena* arena,
    bool utility)
{
    CompilerPrerequisiteHistoricalFixture result = {.utility = utility};
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 d = S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    String8 e = S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    String8 f = S8("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    String8 a40 = string_slice(a, 0, 40), b40 = string_slice(b, 0, 40), c40 = string_slice(c, 0, 40);
    String8 d40 = string_slice(d, 0, 40), e40 = string_slice(e, 0, 40), f40 = string_slice(f, 0, 40);
    CompilerPreparationPlan preparation = {.schema = S8("buster-compiler-preparation-plan-v1"), .phase = S8("qualify"),
        .baseline_revision = a40, .baseline_tree = b40, .candidate_revision = c40, .candidate_tree = d40,
        .trusted_revision = e40, .trusted_root = S8("/home/runner/work/buster/buster/trusted"), .protocol_sha256 = a, .lab_sha256 = b, .python_sha256 = c, .python_path = S8("/usr/bin/python3"), .native_driver_sha256 = d,
        .source_root = S8("/tmp/buster-3211-closure-source"), .output_root = S8("/tmp/buster-3211-closure-output"),
        .baseline_treatment = S8("legacy-rebuild"), .candidate_treatment = S8("snapshot-v1"),
        .closure_policy = S8("snapshot-v1"), .toolchain_policy = S8("clang-release-tests-off-native-v1"),
        .command = S8("compiler_closure-qualify-v1"), .lab_repetitions = S8("5"), .compiler_repetitions = S8("5"),
        .aa_families = S8("3"), .aa_primary = S8("wall"), .aa_confidence_percent = S8("95"),
        .aa_ratio_lower = S8("0.995"), .aa_ratio_upper = S8("1.005"), .net_preparation = S8("snapshot-less-than-legacy"),
        .physical_budget_seconds = S8("5400"), .worker_budget_seconds = S8("5280"), .tail_budget_seconds = S8("120")};
    CompilerClosureUtilityPlan closure = {
        .schema = S8("buster-compiler-closure-utility-plan-v1"), .phase = S8("utility"), .baseline_revision = a40,
        .baseline_tree = b40, .candidate_revision = c40, .candidate_tree = d40,
        .pull_head = f40, .trusted_revision = e40, .trusted_root = S8("/home/runner/work/buster/buster/trusted"),
        .protocol_sha256 = a, .lab_sha256 = b, .comparator_sha256 = c,
        .receipt_sha256 = d, .owned_phase_sha256 = e, .python_sha256 = c,
        .python_path = S8("/usr/bin/python3"), .native_driver_sha256 = d, .source_root = S8("/tmp/buster-3211-utility-source"),
        .output_root = S8("/tmp/buster-3211-utility-output"), .legacy_treatment = S8("legacy-rebuild"), .snapshot_treatment = S8("snapshot-v1"),
        .toolchain_policy = S8("clang-release-tests-off-native-v1"), .command = S8("compiler-closure-utility-v1"), .mode = S8("main"),
        .legs = S8("2"), .leg_order = S8("legacy-then-snapshot"), .compare_profile = S8("compiler-compare-v1"),
        .lab_target_minutes = S8("10"), .lab_warmups = S8("1"), .lab_cpu = S8("2"),
        .lab_profile_steps = S8("none"), .throughput_profile = S8("throughput-corpus-v2"), .throughput_arguments = S8("ci-all-p20-w2-t120-cpu2"),
        .throughput_cells = S8("12"), .throughput_rounds = S8("2"), .leg_clock_scope = S8("bootstrap-through-export-hashfinalization"),
        .cache_policy = S8("fresh-mutable-per-leg"), .utility_charge_policy = S8("all-physical-residual-to-snapshot"), .utility_criterion = S8("snapshot-plus-residual-less-than-legacy"),
        .physical_budget_seconds = S8("5400"), .worker_budget_seconds = S8("5280"), .tail_budget_seconds = S8("120"),
        .study_budget_seconds = S8("10800")};
    result.plan = utility ? compiler_closure_utility_plan_fixture(arena, closure) :
        compiler_preparation_plan_fixture(arena, preparation);
    String8 plan_sha = stage_object_sha256_bytes(arena, (u8*)result.plan.pointer, result.plan.length);
    result.marker = string_format(arena, utility ?
        S8("profile: compiler-baseline-closure-utility-v1 packet: 0 freeze: {S8}") :
        S8("profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: {S8}"), a40);
    String8 config[] = {utility ? S8("buster-compiler-closure-utility-admission-v1") :
        S8("buster-compiler-preparation-admission-v1"), utility ? S8("utility") : S8("qualify"),
        a40, plan_sha, a, S8("2026-10-09T00:00:00Z"), S8("buster14a/buster"), S8("davidgmbb"), S8("39247043")};
    String8 facts[] = {S8("buster-main-sampling-github-facts-v1"), S8("buster14a/buster"), S8("10000"), S8("1"),
        S8("20000"), S8("1"), b40, f40, S8("davidgmbb"), S8("39247043"),
        S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"),
        S8("davidgmbb"), S8("39247043"), S8("buster14a/buster"), S8("buster14a/buster"),
        S8("buster14a/buster"), S8("open"), S8("1"), result.marker, S8("-")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(config); i += 1)
    {
        result.config_values[i] = config[i];
    }
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(facts); i += 1)
    {
        result.fact_values[i] = facts[i];
    }
    result.history = S8("phase\tpacket\trequest_run_id\trequest_run_attempt\texecutor_run_id\texecutor_run_attempt\tstate\tphysical_wall_us\tcampaign\tfreeze_revision\tactor_login\tactor_id\ttriggering_login\ttriggering_id\tpull_author_login\tpull_author_id\n");
    String8* api = result.api;
    api[HISTORICAL_SCHEMA] = S8("buster-compiler-prerequisite-historical-api-v1");
    api[HISTORICAL_REPOSITORY] = S8("buster14a/buster");
    api[HISTORICAL_POLICY_REVISION] = f40;
    api[HISTORICAL_POLICY_MAIN_RELATION] = S8("ahead");
    api[HISTORICAL_REQUEST_RUN_ID] = S8("10000");
    api[HISTORICAL_REQUEST_RUN_ATTEMPT] = S8("1");
    api[HISTORICAL_REQUEST_LATEST_ATTEMPT] = S8("1");
    api[HISTORICAL_REQUEST_WORKFLOW] = S8(".github/workflows/9700x-direct-request.yml");
    api[HISTORICAL_REQUEST_EVENT] = S8("pull_request");
    api[HISTORICAL_REQUEST_STATUS] = S8("completed");
    api[HISTORICAL_REQUEST_CONCLUSION] = S8("success");
    api[HISTORICAL_REQUEST_HEAD] = b40;
    api[HISTORICAL_SOURCE_COMMIT] = b40;
    api[HISTORICAL_FIRST_PARENT] = a40;
    api[HISTORICAL_SECOND_PARENT] = S8("-");
    api[HISTORICAL_COMPARE_PARENT_0] = a40;
    api[HISTORICAL_COMPARE_HEAD_0] = b40;
    api[HISTORICAL_COMPARE_PARENT_1] = S8("-");
    api[HISTORICAL_COMPARE_HEAD_1] = S8("-");
    api[HISTORICAL_EXECUTOR_RUN_ID] = S8("20000");
    api[HISTORICAL_EXECUTOR_RUN_ATTEMPT] = S8("1");
    api[HISTORICAL_EXECUTOR_LATEST_ATTEMPT] = S8("1");
    api[HISTORICAL_EXECUTOR_WORKFLOW] = S8(".github/workflows/9700x-direct-bench.yml");
    api[HISTORICAL_EXECUTOR_EVENT] = S8("workflow_run");
    api[HISTORICAL_EXECUTOR_BRANCH] = S8("main");
    api[HISTORICAL_EXECUTOR_HEAD] = f40;
    api[HISTORICAL_EXECUTOR_TITLE] = string_format(arena, S8("9700X request 10000.1 head {S8}"), b40);
    api[HISTORICAL_EXECUTOR_STATUS] = S8("completed");
    api[HISTORICAL_EXECUTOR_CONCLUSION] = S8("success");
    api[HISTORICAL_EXECUTOR_ACTOR_LOGIN] = S8("davidgmbb");
    api[HISTORICAL_EXECUTOR_ACTOR_ID] = S8("39247043");
    api[HISTORICAL_EXECUTOR_TRIGGERING_LOGIN] = S8("davidgmbb");
    api[HISTORICAL_EXECUTOR_TRIGGERING_ID] = S8("39247043");
    api[HISTORICAL_PULL_NUMBER] = S8("42");
    api[HISTORICAL_ASSOCIATED_PULL_NUMBER] = S8("42");
    api[HISTORICAL_ASSOCIATED_COMMIT] = b40;
    api[HISTORICAL_PULL_STATE] = S8("open");
    api[HISTORICAL_PULL_CURRENT_HEAD] = b40;
    api[HISTORICAL_PARENT_FREEZE_SHA256] = S8("-");
    api[HISTORICAL_ACQUISITION_SHA256] = S8("-");
    api[HISTORICAL_CHECK_NAME] = utility ? S8("9700X compiler closure utility research") :
        S8("9700X compiler preparation research");
    api[HISTORICAL_CHECK_APP_ID] = S8("15368");
    api[HISTORICAL_CHECK_HEAD] = b40;
    api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena, utility ?
        S8("buster-compiler-closure-utility-v1:{S8}:utility:0:10000:20000:1") :
        S8("buster-compiler-preparation-v1:{S8}:qualify:0:10000:20000:1"), plan_sha);
    api[HISTORICAL_CHECK_STATUS] = S8("completed");
    api[HISTORICAL_CHECK_CONCLUSION] = S8("success");
    api[HISTORICAL_CHECK_TITLE] = utility ? S8("Valid unqualified utility packet") :
        S8("Valid unqualified preparation packet");
    compiler_prerequisite_historical_fixture_refresh(arena, &result);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerPrerequisiteHistoricalValidation compiler_prerequisite_historical_fixture_observe(
    Arena* arena, CompilerPrerequisiteHistoricalFixture* fixture)
{
    compiler_prerequisite_historical_fixture_refresh(arena, fixture);
    CompilerPrerequisiteHistoricalValidation result = compiler_prerequisite_historical_validate(arena,
        fixture->utility, fixture->config, fixture->marker, fixture->facts,
        fixture->history, fixture->plan, fixture->proof);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_prerequisite_historical_self_test(Arena* arena)
{
    u64 cases = 0, failures = 0;
    String8 changed_sha = S8("dddddddddddddddddddddddddddddddddddddddd");
    String8 wrong_digest = S8("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    for (u64 kind = 0; kind < 2; kind += 1)
    {
        CompilerPrerequisiteHistoricalFixture fixture = compiler_prerequisite_historical_fixture(arena, kind == 1);
        CompilerPrerequisiteHistoricalValidation observed = compiler_prerequisite_historical_fixture_observe(arena, &fixture);
        String8 harness = fixture.utility ? observed.utility.trusted_revision : observed.preparation.trusted_revision;
        u64 reservation = fixture.utility ? observed.utility.reservation_seconds : observed.preparation.reservation_seconds;
        compiler_preparation_test_check(observed.valid && observed.is_utility == fixture.utility &&
            reservation == 5400 && !string_equal(harness, observed.policy_revision) &&
            string_equal(observed.policy_revision, fixture.fact_values[SAMPLING_FACT_TRUSTED_REVISION]),
            &cases, &failures);
        String8 output = compiler_prerequisite_historical_output(arena, observed);
        // Inspect the controlled pure output for its exact data-only flags.
        bool output_valid = string_contains(output, fixture.utility ? S8("utility_historical_valid=true\n") :
                S8("preparation_historical_valid=true\n")) &&
            string_contains(output, fixture.utility ? S8("utility_historical_execution_authority=false\n") :
                S8("preparation_historical_execution_authority=false\n")) &&
            string_contains(output, fixture.utility ? S8("utility_historical_qualification=unqualified\n") :
                S8("preparation_historical_qualification=unqualified\n")) &&
            !string_contains(output, fixture.utility ? S8("utility_admitted=") : S8("preparation_admitted="));
        compiler_preparation_test_check(output_valid, &cases, &failures);
        compiler_preparation_test_check(!compiler_prerequisite_historical_output(arena,
            (CompilerPrerequisiteHistoricalValidation){0}).length, &cases, &failures);
        // Current PR state/head is observational. Its immutable original commit
        // remains a member; neither a closed PR nor an advanced head is rewritten.
        fixture.fact_values[SAMPLING_FACT_PULL_STATE] = S8("closed");
        fixture.api[HISTORICAL_PULL_STATE] = S8("closed");
        fixture.api[HISTORICAL_PULL_CURRENT_HEAD] = changed_sha;
        compiler_preparation_test_check(compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        String8 cleanup = S8("/home/runner/_temp"), workspace = S8("/home/runner/work/buster/buster");
        bool live_closed = fixture.utility ? compiler_closure_utility_admission_validate(arena,
            fixture.config, fixture.marker, fixture.facts, fixture.history, fixture.plan, cleanup, workspace).valid :
            compiler_preparation_admission_validate(arena, fixture.config, fixture.marker, fixture.facts,
                fixture.history, fixture.plan, cleanup, workspace).valid;
        compiler_preparation_test_check(!live_closed, &cases, &failures);
        fixture.fact_values[SAMPLING_FACT_PULL_STATE] = S8("open");
        fixture.api[HISTORICAL_PULL_STATE] = S8("open");
        compiler_preparation_test_check(compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        bool live_open = fixture.utility ? compiler_closure_utility_admission_validate(arena,
            fixture.config, fixture.marker, fixture.facts, fixture.history, fixture.plan, cleanup, workspace).valid :
            compiler_preparation_admission_validate(arena, fixture.config, fixture.marker, fixture.facts,
                fixture.history, fixture.plan, cleanup, workspace).valid;
        compiler_preparation_test_check(live_open, &cases, &failures);
        // Complete scientific negatives keep their actual check/workflow failure
        // provenance. Raw replay still establishes physical completeness/criteria.
        String8 positive_title = fixture.api[HISTORICAL_CHECK_TITLE];
        fixture.api[HISTORICAL_EXECUTOR_CONCLUSION] = S8("failure");
        fixture.api[HISTORICAL_CHECK_CONCLUSION] = S8("failure");
        fixture.api[HISTORICAL_CHECK_TITLE] = fixture.utility ?
            S8("Complete unqualified Utility; net criterion not met") : S8("Unqualified preparation controls failed");
        observed = compiler_prerequisite_historical_fixture_observe(arena, &fixture);
        compiler_preparation_test_check(observed.valid &&
            string_equal(observed.executor_conclusion, S8("failure")) &&
            string_equal(observed.check_conclusion, S8("failure")) &&
            string_equal(observed.check_title, fixture.api[HISTORICAL_CHECK_TITLE]), &cases, &failures);
        fixture.api[HISTORICAL_CHECK_TITLE] = fixture.utility ?
            S8("Incomplete unqualified utility research") : S8("Incomplete unqualified preparation research");
        compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.api[HISTORICAL_EXECUTOR_CONCLUSION] = S8("success");
        fixture.api[HISTORICAL_CHECK_CONCLUSION] = S8("success");
        fixture.api[HISTORICAL_CHECK_TITLE] = positive_title;
        fixture.fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("2");
        fixture.fact_values[SAMPLING_FACT_FRESH_PARENT_1] = fixture.marker;
        fixture.api[HISTORICAL_SECOND_PARENT] = changed_sha;
        fixture.api[HISTORICAL_COMPARE_PARENT_1] = changed_sha;
        fixture.api[HISTORICAL_COMPARE_HEAD_1] = fixture.api[HISTORICAL_REQUEST_HEAD];
        compiler_preparation_test_check(compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.fact_values[SAMPLING_FACT_FRESH_PARENT_1] = S8("stale parent");
        compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.fact_values[SAMPLING_FACT_FRESH_PARENT_1] = fixture.marker;
        fixture.api[HISTORICAL_SECOND_PARENT] = fixture.api[HISTORICAL_FIRST_PARENT];
        compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("1");
        fixture.fact_values[SAMPLING_FACT_FRESH_PARENT_1] = S8("-");
        fixture.api[HISTORICAL_SECOND_PARENT] = S8("-");
        fixture.api[HISTORICAL_COMPARE_PARENT_1] = S8("-");
        fixture.api[HISTORICAL_COMPARE_HEAD_1] = S8("-");
        u64 bad_fields[] = {HISTORICAL_SCHEMA, HISTORICAL_REPOSITORY, HISTORICAL_POLICY_REVISION,
            HISTORICAL_POLICY_MAIN_RELATION, HISTORICAL_REQUEST_LATEST_ATTEMPT, HISTORICAL_EXECUTOR_LATEST_ATTEMPT,
            HISTORICAL_REQUEST_RUN_ATTEMPT, HISTORICAL_EXECUTOR_RUN_ATTEMPT,
            HISTORICAL_REQUEST_WORKFLOW, HISTORICAL_REQUEST_EVENT, HISTORICAL_REQUEST_STATUS,
            HISTORICAL_REQUEST_CONCLUSION, HISTORICAL_SOURCE_COMMIT, HISTORICAL_FIRST_PARENT,
            HISTORICAL_COMPARE_HEAD_0, HISTORICAL_COMPARE_PARENT_1, HISTORICAL_EXECUTOR_WORKFLOW,
            HISTORICAL_EXECUTOR_EVENT, HISTORICAL_EXECUTOR_BRANCH, HISTORICAL_EXECUTOR_TITLE,
            HISTORICAL_EXECUTOR_STATUS, HISTORICAL_EXECUTOR_CONCLUSION, HISTORICAL_EXECUTOR_HEAD,
            HISTORICAL_EXECUTOR_ACTOR_LOGIN, HISTORICAL_EXECUTOR_ACTOR_ID, HISTORICAL_EXECUTOR_TRIGGERING_LOGIN,
            HISTORICAL_EXECUTOR_TRIGGERING_ID, HISTORICAL_PULL_NUMBER, HISTORICAL_ASSOCIATED_PULL_NUMBER,
            HISTORICAL_ASSOCIATED_COMMIT, HISTORICAL_PULL_STATE, HISTORICAL_PULL_CURRENT_HEAD,
            HISTORICAL_PARENT_FREEZE_SHA256, HISTORICAL_ACQUISITION_SHA256, HISTORICAL_CHECK_NAME,
            HISTORICAL_CHECK_APP_ID, HISTORICAL_CHECK_HEAD, HISTORICAL_CHECK_EXTERNAL_ID,
            HISTORICAL_CHECK_STATUS, HISTORICAL_CHECK_CONCLUSION, HISTORICAL_CHECK_TITLE};
        String8 bad_values[] = {S8("foreign-schema"), S8("other/repository"), changed_sha, S8("behind"),
            S8("2"), S8("2"), S8("2"), S8("2"), S8("foreign.yml"), S8("push"), S8("queued"), S8("failure"),
            changed_sha, changed_sha, changed_sha, changed_sha, S8("foreign.yml"), S8("push"), S8("feature"),
            S8("wrong title"), S8("queued"), S8("cancelled"), changed_sha, S8("attacker"), S8("1"),
            S8("attacker"), S8("1"), S8("0"), S8("43"), changed_sha, S8("unknown"), S8("-"),
            wrong_digest, wrong_digest, S8("other check"), S8("1"), changed_sha, S8("wrong marker"),
            S8("queued"), S8("failure"), S8("incomplete packet")};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_fields); i += 1)
        {
            String8 saved = fixture.api[bad_fields[i]];
            fixture.api[bad_fields[i]] = bad_values[i];
            compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
                &cases, &failures);
            fixture.api[bad_fields[i]] = saved;
        }
        // Recompute transport hashes for owner/freshness edits, so these checks
        // reach policy joins instead of only failing a stale byte digest.
        for (u64 i = 0; i < SAMPLING_FACT_COUNT; i += 1)
        {
            String8 saved = fixture.fact_values[i];
            fixture.fact_values[i] = i == SAMPLING_FACT_FRESH_PARENT_1 ? S8("unexpected parent") : S8("-");
            compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
                &cases, &failures);
            fixture.fact_values[i] = saved;
        }
        for (u64 i = 0; i < PREPARATION_CONFIG_COUNT; i += 1)
        {
            String8 saved = fixture.config_values[i];
            fixture.config_values[i] = S8("-");
            compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
                &cases, &failures);
            fixture.config_values[i] = saved;
        }
        String8 original_revision = fixture.config_values[PREPARATION_CONFIG_FREEZE_REVISION];
        fixture.config_values[PREPARATION_CONFIG_FREEZE_REVISION] = changed_sha;
        compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.config_values[PREPARATION_CONFIG_FREEZE_REVISION] = original_revision;
        String8 original_history = fixture.history;
        String8 states[] = {S8("complete"), S8("failed"), S8("cancelled"), S8("hostless"), S8("incomplete")};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(states); i += 1)
        {
            String8 row = compiler_sampling_admission_fixture_row(arena, S8("acquire"), 0, 80, 180,
                states[i], 1800000000, fixture.config_values[PREPARATION_CONFIG_FREEZE_SHA], original_revision);
            fixture.history = string_format(arena, S8("{S8}{S8}"), original_history, row);
            compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
                &cases, &failures);
        }
        fixture.history = original_history;
        String8 original_plan = fixture.plan;
        String8 original_plan_sha = fixture.config_values[PREPARATION_CONFIG_FREEZE_SHA];
        if (fixture.utility)
        {
            CompilerClosureUtilityPlan changed = compiler_closure_utility_plan_parse(original_plan);
            changed.trusted_root = changed.source_root;
            fixture.plan = compiler_closure_utility_plan_fixture(arena, changed);
        }
        else
        {
            CompilerPreparationPlan changed = compiler_preparation_plan_parse(original_plan);
            changed.trusted_root = changed.source_root;
            fixture.plan = compiler_preparation_plan_fixture(arena, changed);
        }
        fixture.config_values[PREPARATION_CONFIG_FREEZE_SHA] =
            stage_object_sha256_bytes(arena, (u8*)fixture.plan.pointer, fixture.plan.length);
        fixture.api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena, fixture.utility ?
            S8("buster-compiler-closure-utility-v1:{S8}:utility:0:10000:20000:1") :
            S8("buster-compiler-preparation-v1:{S8}:qualify:0:10000:20000:1"),
            fixture.config_values[PREPARATION_CONFIG_FREEZE_SHA]);
        compiler_preparation_test_check(!compiler_prerequisite_historical_fixture_observe(arena, &fixture).valid,
            &cases, &failures);
        fixture.plan = original_plan;
        fixture.config_values[PREPARATION_CONFIG_FREEZE_SHA] = original_plan_sha;
        fixture.api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena, fixture.utility ?
            S8("buster-compiler-closure-utility-v1:{S8}:utility:0:10000:20000:1") :
            S8("buster-compiler-preparation-v1:{S8}:qualify:0:10000:20000:1"), original_plan_sha);
        compiler_prerequisite_historical_fixture_refresh(arena, &fixture);
        u64 digest_fields[] = {HISTORICAL_ALLOWLIST_SHA256, HISTORICAL_FACTS_SHA256,
            HISTORICAL_HISTORY_SHA256, HISTORICAL_FREEZE_SHA256};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(digest_fields); i += 1)
        {
            String8 saved = fixture.api[digest_fields[i]];
            fixture.api[digest_fields[i]] = wrong_digest;
            String8 proof = compiler_sampling_admission_fixture_fields(arena,
                (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names),
                (SliceString8){fixture.api, HISTORICAL_API_COUNT});
            compiler_preparation_test_check(!compiler_prerequisite_historical_validate(arena, fixture.utility,
                fixture.config, fixture.marker, fixture.facts, fixture.history, fixture.plan, proof).valid,
                &cases, &failures);
            fixture.api[digest_fields[i]] = saved;
        }
        String8 malformed[] = {string_format(arena, S8("{S8}schema\textra\n"), fixture.proof),
            string_format(arena, S8("{S8}unknown\textra\n"), fixture.proof),
            string_slice(fixture.proof, 0, fixture.proof.length - 1),
            string_format(arena, S8("repository\tbuster14a/buster\n{S8}"), fixture.proof),
            string_format(arena, S8("{S8}\r\n"), fixture.proof), S8("")};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(malformed); i += 1)
        {
            compiler_preparation_test_check(!compiler_prerequisite_historical_validate(arena, fixture.utility,
                fixture.config, fixture.marker, fixture.facts, fixture.history, fixture.plan, malformed[i]).valid,
                &cases, &failures);
        }
    }
    bool valid = failures == 0 && cases == 204;
    string_print(S8("COMPILER_PREREQUISITE_HISTORICAL_TEST cases={u64} failures={u64} qualification=unqualified\n"),
        cases, failures);
    return valid;
}
#endif
