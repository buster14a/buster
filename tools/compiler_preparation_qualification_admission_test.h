// Native adversarial fixtures; synthetic identity bytes are test data only.
BUSTER_GLOBAL_LOCAL String8 compiler_preparation_plan_fixture(Arena* arena, CompilerPreparationPlan fixture)
{
    String8 names[] = {S8("schema"), S8("phase"), S8("baseline_revision"), S8("baseline_tree"),
        S8("candidate_revision"), S8("candidate_tree"), S8("trusted_revision"), S8("protocol_sha256"),
        S8("lab_sha256"), S8("python_sha256"), S8("native_driver_sha256"), S8("source_root"), S8("output_root"),
        S8("baseline_treatment"), S8("candidate_treatment"), S8("closure_policy"), S8("toolchain_policy"),
        S8("command"), S8("lab_repetitions"), S8("compiler_repetitions"), S8("aa_families"), S8("aa_primary"),
        S8("aa_confidence_percent"), S8("aa_ratio_lower"), S8("aa_ratio_upper"), S8("net_preparation"),
        S8("physical_budget_seconds"), S8("worker_budget_seconds"), S8("tail_budget_seconds")};
    String8 values[] = {fixture.schema, fixture.phase, fixture.baseline_revision, fixture.baseline_tree,
        fixture.candidate_revision, fixture.candidate_tree, fixture.trusted_revision, fixture.protocol_sha256,
        fixture.lab_sha256, fixture.python_sha256, fixture.native_driver_sha256, fixture.source_root, fixture.output_root,
        fixture.baseline_treatment, fixture.candidate_treatment, fixture.closure_policy, fixture.toolchain_policy,
        fixture.command, fixture.lab_repetitions, fixture.compiler_repetitions, fixture.aa_families, fixture.aa_primary,
        fixture.aa_confidence_percent, fixture.aa_ratio_lower, fixture.aa_ratio_upper, fixture.net_preparation,
        fixture.physical_budget_seconds, fixture.worker_budget_seconds, fixture.tail_budget_seconds};
    String8 result = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(names), (SliceString8)BUSTER_ARRAY_TO_SLICE(values));
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_preparation_test_check(bool condition, u64* cases, u64* failures)
{
    *cases += 1;
    *failures += condition ? 0 : 1;
}

BUSTER_GLOBAL_LOCAL bool compiler_preparation_admission_self_test(Arena* arena)
{
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 d = S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    String8 e = S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    String8 f = S8("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    String8 a40 = string_slice(a, 0, 40), b40 = string_slice(b, 0, 40), c40 = string_slice(c, 0, 40);
    String8 d40 = string_slice(d, 0, 40), e40 = string_slice(e, 0, 40), f40 = string_slice(f, 0, 40);
    CompilerPreparationPlan plan = {.schema = S8("buster-compiler-preparation-plan-v1"), .phase = S8("qualify"),
        .baseline_revision = a40, .baseline_tree = b40, .candidate_revision = c40, .candidate_tree = d40,
        .trusted_revision = e40, .protocol_sha256 = a, .lab_sha256 = b, .python_sha256 = c, .native_driver_sha256 = d,
        .source_root = S8("/tmp/buster-3211-closure-source"), .output_root = S8("/tmp/buster-3211-closure-output"),
        .baseline_treatment = S8("legacy-rebuild"), .candidate_treatment = S8("snapshot-v1"),
        .closure_policy = S8("snapshot-v1"), .toolchain_policy = S8("clang-release-tests-off-native-v1"),
        .command = S8("compiler_closure-qualify-v1"), .lab_repetitions = S8("5"), .compiler_repetitions = S8("5"),
        .aa_families = S8("3"), .aa_primary = S8("wall"), .aa_confidence_percent = S8("95"),
        .aa_ratio_lower = S8("0.995"), .aa_ratio_upper = S8("1.005"), .net_preparation = S8("snapshot-less-than-legacy"),
        .physical_budget_seconds = S8("5400"), .worker_budget_seconds = S8("5280"), .tail_budget_seconds = S8("120")};
    String8 plan_text = compiler_preparation_plan_fixture(arena, plan);
    String8 plan_sha = stage_object_sha256_bytes(arena, (u8*)plan_text.pointer, plan_text.length);
    String8 config_names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"),
        S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config_values[] = {S8("buster-compiler-preparation-admission-v1"), S8("qualify"), a40, plan_sha, a,
        S8("2026-10-09T00:00:00Z"), S8("buster14a/buster"), S8("davidgmbb"), S8("39247043")};
    String8 marker = string_format(arena,
        S8("profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: {S8}"), a40);
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 fact_values[] = {S8("buster-main-sampling-github-facts-v1"), S8("buster14a/buster"), S8("10000"), S8("1"),
        S8("20000"), S8("1"), b40, f40, S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"),
        S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"), S8("buster14a/buster"),
        S8("buster14a/buster"), S8("buster14a/buster"), S8("open"), S8("1"), marker, S8("-")};
    SliceString8 config_keys = (SliceString8)BUSTER_ARRAY_TO_SLICE(config_names);
    SliceString8 config_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(config_values);
    SliceString8 fact_keys = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names);
    SliceString8 fact_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_values);
    String8 config = compiler_sampling_admission_fixture_fields(arena, config_keys, config_fields);
    String8 facts = compiler_sampling_admission_fixture_fields(arena, fact_keys, fact_fields);
    String8 history = S8("phase\tpacket\trequest_run_id\trequest_run_attempt\texecutor_run_id\texecutor_run_attempt\tstate\tphysical_wall_us\tcampaign\tfreeze_revision\tactor_login\tactor_id\ttriggering_login\ttriggering_id\tpull_author_login\tpull_author_id\n");
    String8 cleanup = S8("/home/runner/_temp"), workspace = S8("/home/runner/work/buster/buster");
    u64 cases = 0, failures = 0;
    CompilerPreparationPlan parsed = compiler_preparation_plan_parse(plan_text);
    compiler_preparation_test_check(parsed.valid && compiler_preparation_plan_paths_outside(parsed, cleanup, workspace),
        &cases, &failures);
    CompilerPreparationAdmission admitted = compiler_preparation_admission_validate(arena,
        config, marker, facts, history, plan_text, cleanup, workspace);
    compiler_preparation_test_check(admitted.valid && admitted.packet == 0 && admitted.reservation_seconds == 5400 &&
        admitted.worker_seconds == 5280 && admitted.tail_seconds == 120 && admitted.timeout_minutes == 90 &&
        string_equal(admitted.phase, S8("qualify")) && string_equal(admitted.family, S8("preparation")) &&
        string_equal(admitted.trusted_revision, e40) && string_equal(admitted.policy_trusted_revision, f40) &&
        !string_equal(admitted.trusted_revision, admitted.policy_trusted_revision), &cases, &failures);
    compiler_preparation_test_check(compiler_preparation_admission_validate(arena, config,
        string_format(arena, S8("{S8}\n"), marker), facts, history, plan_text, cleanup, workspace).valid, &cases, &failures);

    String8* plan_fields[] = {&plan.schema, &plan.phase, &plan.baseline_revision, &plan.baseline_tree,
        &plan.candidate_revision, &plan.candidate_tree, &plan.trusted_revision, &plan.protocol_sha256,
        &plan.lab_sha256, &plan.python_sha256, &plan.native_driver_sha256, &plan.source_root, &plan.output_root,
        &plan.baseline_treatment, &plan.candidate_treatment, &plan.closure_policy, &plan.toolchain_policy,
        &plan.command, &plan.lab_repetitions, &plan.compiler_repetitions, &plan.aa_families, &plan.aa_primary,
        &plan.aa_confidence_percent, &plan.aa_ratio_lower, &plan.aa_ratio_upper, &plan.net_preparation,
        &plan.physical_budget_seconds, &plan.worker_budget_seconds, &plan.tail_budget_seconds};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(plan_fields); i += 1)
    {
        String8 saved = *plan_fields[i];
        *plan_fields[i] = S8("-");
        compiler_preparation_test_check(!compiler_preparation_plan_parse(compiler_preparation_plan_fixture(arena, plan)).valid,
            &cases, &failures);
        *plan_fields[i] = saved;
    }
    plan.schema = S8("buster-main-sampling-freeze-v1");
    compiler_preparation_test_check(!compiler_preparation_plan_parse(compiler_preparation_plan_fixture(arena, plan)).valid,
        &cases, &failures);
    plan.schema = parsed.schema;
    plan.baseline_revision = d40;
    String8 changed_plan = compiler_preparation_plan_fixture(arena, plan);
    compiler_preparation_test_check(compiler_preparation_plan_parse(changed_plan).valid &&
        !compiler_preparation_admission_validate(arena, config, marker, facts, history, changed_plan, cleanup, workspace).valid,
        &cases, &failures);
    plan.baseline_revision = parsed.baseline_revision;
    plan.candidate_revision = plan.baseline_revision;
    compiler_preparation_test_check(!compiler_preparation_plan_parse(compiler_preparation_plan_fixture(arena, plan)).valid,
        &cases, &failures);
    plan.candidate_revision = parsed.candidate_revision;
    plan.candidate_tree = plan.baseline_tree;
    compiler_preparation_test_check(!compiler_preparation_plan_parse(compiler_preparation_plan_fixture(arena, plan)).valid,
        &cases, &failures);
    plan.candidate_tree = parsed.candidate_tree;

    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(config_values); i += 1)
    {
        String8 saved = config_values[i];
        config_values[i] = i == PREPARATION_CONFIG_STATE ? S8("disabled") : S8("-");
        String8 changed = compiler_sampling_admission_fixture_fields(arena, config_keys, config_fields);
        compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
            changed, marker, facts, history, plan_text, cleanup, workspace).valid, &cases, &failures);
        config_values[i] = saved;
    }
    config_values[PREPARATION_CONFIG_FREEZE_SHA] = b;
    String8 wrong_hash = compiler_sampling_admission_fixture_fields(arena, config_keys, config_fields);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        wrong_hash, marker, facts, history, plan_text, cleanup, workspace).valid, &cases, &failures);
    config_values[PREPARATION_CONFIG_FREEZE_SHA] = plan_sha;

    fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("2");
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena, fact_keys, fact_fields);
    compiler_preparation_test_check(compiler_preparation_admission_validate(arena,
        config, marker, facts, history, plan_text, cleanup, workspace).valid, &cases, &failures);
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(fact_values); i += 1)
    {
        String8 saved = fact_values[i];
        fact_values[i] = S8("-");
        String8 changed = compiler_sampling_admission_fixture_fields(arena, fact_keys, fact_fields);
        compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
            config, marker, changed, history, plan_text, cleanup, workspace).valid, &cases, &failures);
        fact_values[i] = saved;
    }
    fact_values[SAMPLING_FACT_EXECUTOR_RUN] = fact_values[SAMPLING_FACT_REQUEST_RUN];
    String8 same_run = compiler_sampling_admission_fixture_fields(arena, fact_keys, fact_fields);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        config, marker, same_run, history, plan_text, cleanup, workspace).valid, &cases, &failures);
    fact_values[SAMPLING_FACT_EXECUTOR_RUN] = S8("20000");
    fact_values[SAMPLING_FACT_REQUEST_ATTEMPT] = S8("2");
    String8 replay = compiler_sampling_admission_fixture_fields(arena, fact_keys, fact_fields);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        config, marker, replay, history, plan_text, cleanup, workspace).valid, &cases, &failures);
    fact_values[SAMPLING_FACT_REQUEST_ATTEMPT] = S8("1");

    String8 bad_markers[] = {string_format(arena, S8(" {S8}"), marker),
        string_format(arena, S8("{S8} "), marker), string_format(arena, S8("{S8}\r\n"), marker),
        string_format(arena, S8("{S8}\n\n"), marker),
        string_format(arena, S8("profile: compiler-baseline-closure-qualification-v1 packet: 1 freeze: {S8}"), a40),
        string_format(arena, S8("profile: compiler-baseline-closure-qualification-v1 packet: 00 freeze: {S8}"), a40),
        string_format(arena, S8("profile: compiler-main-sampling-acquire-v1 packet: 0 freeze: {S8}"), a40),
        string_format(arena, S8("profile: compiler-baseline-closure-qualification-v1 packet: 0 freeze: {S8}"), b40)};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_markers); i += 1)
        compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
            config, bad_markers[i], facts, history, plan_text, cleanup, workspace).valid, &cases, &failures);
    String8 bad_paths[] = {S8("/tmp"), plan.source_root, plan.output_root,
        S8("/tmp/buster-3211-closure-source/nested"), S8("/tmp/buster-3211-closure-output/nested"),
        S8("relative"), S8("/home/runner/../tmp"), S8("/home//runner"), S8("/")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_paths); i += 1)
    {
        compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
            config, marker, facts, history, plan_text, bad_paths[i], workspace).valid, &cases, &failures);
        compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
            config, marker, facts, history, plan_text, cleanup, bad_paths[i]).valid, &cases, &failures);
    }
    String8 phases[] = {S8("preparation"), S8("qualify")};
    String8 states[] = {S8("complete"), S8("failed"), S8("cancelled"), S8("hostless"), S8("running")};
    for (u64 phase = 0; phase < BUSTER_ARRAY_LENGTH(phases); phase += 1)
    {
        for (u64 state = 0; state < BUSTER_ARRAY_LENGTH(states); state += 1)
        {
            String8 row = compiler_sampling_admission_fixture_row(arena, phases[phase], 0, 80, 180,
                states[state], 0, plan_sha, a40);
            String8 prior = string_format(arena, S8("{S8}{S8}"), history, row);
            compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
                config, marker, facts, prior, plan_text, cleanup, workspace).valid, &cases, &failures);
        }
    }
    String8 malformed_plans[] = {string_format(arena, S8("{S8}unknown\tvalue\n"), plan_text),
        string_format(arena, S8("{S8}schema\tbuster-compiler-preparation-plan-v1\n"), plan_text),
        string_slice(plan_text, 0, plan_text.length - 1),
        string_format(arena, S8("{S8}unknown\tbad\r\n"), plan_text), S8("schema\tbad\0hidden\n")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(malformed_plans); i += 1)
        compiler_preparation_test_check(!compiler_preparation_plan_parse(malformed_plans[i]).valid, &cases, &failures);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        string_format(arena, S8("{S8}state\tqualify\n"), config), marker, facts, history, plan_text, cleanup, workspace).valid,
        &cases, &failures);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        config, marker, string_format(arena, S8("{S8}owner_id\t39247043\n"), facts), history, plan_text, cleanup, workspace).valid,
        &cases, &failures);
    compiler_preparation_test_check(!compiler_preparation_admission_validate(arena,
        config, marker, facts, string_slice(history, 0, history.length - 1), plan_text, cleanup, workspace).valid,
        &cases, &failures);
    char8* large = arena_allocate(arena, char8, 16385);
    memset(large, 'x', 16385);
    compiler_preparation_test_check(!compiler_preparation_plan_parse((String8){large, 16385}).valid, &cases, &failures);
    fprintf(stdout, "COMPILER_PREPARATION_ADMISSION_TEST cases=%llu failures=%llu status=%s\n",
        (unsigned long long)cases, (unsigned long long)failures, failures ? "fail" : "pass");
    bool result = failures == 0;
    return result;
}
