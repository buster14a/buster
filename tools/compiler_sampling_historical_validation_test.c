// Native historical API/data controls. Include after the historical validator.
// All identities and outcomes below are synthetic diagnostics; no host work.
#ifndef BUSTER_COMPILER_SAMPLING_HISTORICAL_VALIDATION_TEST_INCLUDED
#define BUSTER_COMPILER_SAMPLING_HISTORICAL_VALIDATION_TEST_INCLUDED

BUSTER_GLOBAL_LOCAL String8 compiler_sampling_historical_fixture_api(Arena* arena, String8* api,
    String8 allowlist, String8 facts, String8 history, String8 freeze, String8 parent, String8 acquisition)
{
    String8 bytes[] = {allowlist,facts,history,freeze,acquisition};
    u64 fields[] = {HISTORICAL_ALLOWLIST_SHA256,HISTORICAL_FACTS_SHA256,HISTORICAL_HISTORY_SHA256,
        HISTORICAL_FREEZE_SHA256,HISTORICAL_ACQUISITION_SHA256};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bytes); i += 1)
        api[fields[i]] = stage_object_sha256_bytes(arena,bytes[i].pointer,bytes[i].length);
    api[HISTORICAL_PARENT_FREEZE_SHA256] = parent.length ?
        stage_object_sha256_bytes(arena,parent.pointer,parent.length) : S8("-");
    String8 result = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_sampling_historical_api_names),
        (SliceString8){api,HISTORICAL_API_COUNT});
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_sampling_historical_self_test(Arena* arena)
{
    String8 a = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 b = S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    String8 c = S8("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    String8 a40 = string_slice(a, 0, 40), b40 = string_slice(b, 0, 40), c40 = string_slice(c, 0, 40);
    CompilerSamplingAcquisitionPlan plan = {.schema = S8("buster-main-sampling-acquisition-v1"),
        .phase = S8("acquire"), .base = a40, .base_tree = b40, .request_head = c40, .trusted_revision = b40,
        .baseline_revision = a40, .ab1_revision = b40, .ab2_revision = c40, .protocol_sha256 = a,
        .source_root = S8("/srv/buster/source"), .store_root = S8("/srv/buster/evidence"),
        .closure_policy = S8("snapshot-v1"), .toolchain_policy = S8("clang-release-tests-off-native-v1"),
        .measurement = S8("false"), .physical_budget_seconds = S8("1800")};
    String8 plan_text = compiler_sampling_acquisition_plan_fixture(arena, plan);
    String8 plan_sha = stage_object_sha256_bytes(arena, (u8*)plan_text.pointer, plan_text.length);
    String8 names[] = {S8("schema"), S8("state"), S8("freeze_revision"), S8("freeze_sha256"), S8("campaign_parent"),
        S8("parent_freeze_revision"), S8("protocol_sha256"), S8("history_since"), S8("repository"), S8("owner_login"), S8("owner_id")};
    String8 config_values[] = {S8("buster-main-sampling-admission-v1"), S8("acquire"), a40, plan_sha, S8("-"),
        S8("-"), a, S8("2026-10-09T00:00:00Z"), S8("buster14a/buster"), S8("davidgmbb"), S8("39247043")};
    String8 marker = string_format(arena, S8("profile: compiler-main-sampling-acquire-v1 packet: 0 freeze: {S8}"), a40);
    String8 fact_names[] = {S8("schema"), S8("repository"), S8("request_run_id"), S8("request_run_attempt"),
        S8("executor_run_id"), S8("executor_run_attempt"), S8("request_head"), S8("trusted_revision"),
        S8("owner_login"), S8("owner_id"), S8("actor_login"), S8("actor_id"), S8("triggering_login"), S8("triggering_id"),
        S8("pull_author_login"), S8("pull_author_id"), S8("request_repository"), S8("request_head_repository"),
        S8("pull_repository"), S8("pull_state"), S8("parent_count"), S8("fresh_parent_0"), S8("fresh_parent_1")};
    String8 fact_values[] = {S8("buster-main-sampling-github-facts-v1"), S8("buster14a/buster"), S8("10000"), S8("1"),
        S8("20000"), S8("1"), c40, a40, S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"),
        S8("davidgmbb"), S8("39247043"), S8("davidgmbb"), S8("39247043"), S8("buster14a/buster"),
        S8("buster14a/buster"), S8("buster14a/buster"), S8("open"), S8("1"), marker, S8("-")};
    String8 header = S8("phase\tpacket\trequest_run_id\trequest_run_attempt\texecutor_run_id\texecutor_run_attempt\tstate\tphysical_wall_us\tcampaign\tfreeze_revision\tactor_login\tactor_id\ttriggering_login\ttriggering_id\tpull_author_login\tpull_author_id\n");
    SliceString8 config_names = (SliceString8)BUSTER_ARRAY_TO_SLICE(names);
    SliceString8 config_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(config_values);
    SliceString8 github_names = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_names);
    SliceString8 github_fields = (SliceString8)BUSTER_ARRAY_TO_SLICE(fact_values);
    String8 config = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
    String8 facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    String8 d40 = S8("dddddddddddddddddddddddddddddddddddddddd");
    String8 api[HISTORICAL_API_COUNT] = {0};
    api[HISTORICAL_SCHEMA] = S8("buster-main-sampling-historical-api-v1");
    api[HISTORICAL_REPOSITORY] = S8("buster14a/buster");
    api[HISTORICAL_POLICY_REVISION] = a40;
    api[HISTORICAL_POLICY_MAIN_RELATION] = S8("ahead");
    api[HISTORICAL_REQUEST_RUN_ID] = S8("10000");
    api[HISTORICAL_REQUEST_RUN_ATTEMPT] = S8("1");
    api[HISTORICAL_REQUEST_LATEST_ATTEMPT] = S8("1");
    api[HISTORICAL_REQUEST_WORKFLOW] = S8(".github/workflows/9700x-direct-request.yml");
    api[HISTORICAL_REQUEST_EVENT] = S8("pull_request");
    api[HISTORICAL_REQUEST_STATUS] = S8("completed");
    api[HISTORICAL_REQUEST_CONCLUSION] = S8("success");
    api[HISTORICAL_REQUEST_HEAD] = c40;
    api[HISTORICAL_SOURCE_COMMIT] = c40;
    api[HISTORICAL_FIRST_PARENT] = b40;
    api[HISTORICAL_SECOND_PARENT] = S8("-");
    api[HISTORICAL_COMPARE_PARENT_0] = b40;
    api[HISTORICAL_COMPARE_HEAD_0] = c40;
    api[HISTORICAL_COMPARE_PARENT_1] = S8("-");
    api[HISTORICAL_COMPARE_HEAD_1] = S8("-");
    api[HISTORICAL_EXECUTOR_RUN_ID] = S8("20000");
    api[HISTORICAL_EXECUTOR_RUN_ATTEMPT] = S8("1");
    api[HISTORICAL_EXECUTOR_LATEST_ATTEMPT] = S8("1");
    api[HISTORICAL_EXECUTOR_WORKFLOW] = S8(".github/workflows/9700x-direct-bench.yml");
    api[HISTORICAL_EXECUTOR_EVENT] = S8("workflow_run");
    api[HISTORICAL_EXECUTOR_BRANCH] = S8("main");
    api[HISTORICAL_EXECUTOR_HEAD] = a40;
    api[HISTORICAL_EXECUTOR_TITLE] = string_format(arena,S8("9700X request 10000.1 head {S8}"),c40);
    api[HISTORICAL_EXECUTOR_STATUS] = S8("completed");
    api[HISTORICAL_EXECUTOR_CONCLUSION] = S8("success");
    api[HISTORICAL_EXECUTOR_ACTOR_LOGIN] = S8("davidgmbb");
    api[HISTORICAL_EXECUTOR_ACTOR_ID] = S8("39247043");
    api[HISTORICAL_EXECUTOR_TRIGGERING_LOGIN] = S8("davidgmbb");
    api[HISTORICAL_EXECUTOR_TRIGGERING_ID] = S8("39247043");
    api[HISTORICAL_PULL_NUMBER] = S8("42");
    api[HISTORICAL_ASSOCIATED_PULL_NUMBER] = S8("42");
    api[HISTORICAL_ASSOCIATED_COMMIT] = c40;
    api[HISTORICAL_PULL_STATE] = S8("open");
    api[HISTORICAL_PULL_CURRENT_HEAD] = c40;
    api[HISTORICAL_ALLOWLIST_SHA256] = a;
    api[HISTORICAL_FACTS_SHA256] = a;
    api[HISTORICAL_HISTORY_SHA256] = a;
    api[HISTORICAL_FREEZE_SHA256] = a;
    api[HISTORICAL_PARENT_FREEZE_SHA256] = S8("-");
    api[HISTORICAL_ACQUISITION_SHA256] = a;
    api[HISTORICAL_CHECK_NAME] = S8("9700X compiler sampling research");
    api[HISTORICAL_CHECK_APP_ID] = S8("15368");
    api[HISTORICAL_CHECK_HEAD] = c40;
    api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena,S8("buster-main-sampling-v1:{S8}:acquire:0:10000:20000:1"),plan_sha);
    api[HISTORICAL_CHECK_STATUS] = S8("completed");
    api[HISTORICAL_CHECK_CONCLUSION] = S8("success");
    api[HISTORICAL_CHECK_TITLE] = S8("Valid unqualified sampling packet");
    String8 proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,header,plan_text,(String8){0},plan_text);
    CompilerSamplingHistoricalValidation observed = compiler_sampling_historical_validate(arena,
        config,marker,facts,header,plan_text,(String8){0},plan_text,proof);
    bool valid = observed.valid && observed.packet.packet == 0 && observed.packet.reservation_seconds == 1800 &&
        string_equal(observed.policy_revision,a40) && string_equal(observed.packet.trusted_revision,b40);
    u64 controls = 1;
    // The actual current PR is closed and its head advanced. Immutable commit
    // membership, not its new head, is the historical association being read.
    fact_values[SAMPLING_FACT_PULL_STATE] = S8("closed");
    api[HISTORICAL_PULL_STATE] = S8("closed");
    api[HISTORICAL_PULL_CURRENT_HEAD] = d40;
    facts = compiler_sampling_admission_fixture_fields(arena,github_names,github_fields);
    proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,header,plan_text,(String8){0},plan_text);
    observed = compiler_sampling_historical_validate(arena,config,marker,facts,header,plan_text,(String8){0},plan_text,proof);
    valid = observed.valid && valid; controls += 1;
    valid = !compiler_sampling_admission_validate(arena,config,marker,facts,header,plan_text,(String8){0}).valid && valid;
    controls += 1;
    String8 acquisition_row = compiler_sampling_admission_fixture_row(arena,S8("acquire"),0,80,180,
        S8("complete"),1800000000,plan_sha,a40);
    String8 acquisition_history = string_format(arena,S8("{S8}{S8}"),header,acquisition_row);
    CompilerSamplingFreeze freeze = {.schema = S8("buster-main-sampling-freeze-v1"), .phase = S8("pilot"),
        .campaign_parent = plan_sha, .campaign_parent_revision = a40, .base = a40, .base_tree = b40,
        .request_head = c40, .trusted_revision = b40, .baseline_revision = a40, .aa_candidate_revision = a40,
        .ab1_revision = b40, .ab2_revision = c40, .protocol_sha256 = a, .lab_sha256 = a,
        .python_sha256 = a, .driver_sha256 = a, .closure_sha256 = a, .prepared_sha256 = a, .baseline_sha256 = a,
        .aa_candidate_sha256 = a, .ab1_candidate_sha256 = b, .ab2_candidate_sha256 = c,
        .baseline_bytes = S8("10000000"), .ab1_candidate_bytes = S8("10001000"), .ab2_candidate_bytes = S8("10002000"),
        .candidate_pairs = S8("0"), .selected_candidate = S8("exploratory"),
        .calibration_ab1_low_percent = S8("-"), .calibration_ab1_high_percent = S8("-"),
        .calibration_ab2_low_percent = S8("-"), .calibration_ab2_high_percent = S8("-")};
    String8 pilot_text = compiler_sampling_freeze_fixture(arena, freeze);
    String8 pilot_sha = stage_object_sha256_bytes(arena, (u8*)pilot_text.pointer, pilot_text.length);
    config_values[SAMPLING_CONFIG_STATE] = S8("pilot");
    config_values[SAMPLING_CONFIG_FREEZE_REVISION] = b40;
    config_values[SAMPLING_CONFIG_FREEZE_SHA] = pilot_sha;
    config_values[SAMPLING_CONFIG_PARENT_SHA] = plan_sha;
    config_values[SAMPLING_CONFIG_PARENT_REVISION] = a40;
    config = compiler_sampling_admission_fixture_fields(arena, config_names, config_fields);
    marker = string_format(arena, S8("profile: compiler-main-sampling-pilot-v1 packet: 0 freeze: {S8}"), b40);
    fact_values[SAMPLING_FACT_FRESH_PARENT_0] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena, github_names, github_fields);
    api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena,
        S8("buster-main-sampling-v1:{S8}:pilot:0:10000:20000:1"),pilot_sha);
    proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,acquisition_history,pilot_text,plan_text,plan_text);
    observed = compiler_sampling_historical_validate(arena,config,marker,facts,acquisition_history,pilot_text,plan_text,plan_text,proof);
    valid = observed.valid && string_equal(observed.packet.family,S8("aa")) && valid; controls += 1;
    // Negative API facts retain the exact committed bytes and valid prefix.
    u64 bad_fields[] = {HISTORICAL_POLICY_REVISION,HISTORICAL_POLICY_MAIN_RELATION,
        HISTORICAL_REQUEST_LATEST_ATTEMPT,HISTORICAL_EXECUTOR_LATEST_ATTEMPT,HISTORICAL_REQUEST_RUN_ATTEMPT,
        HISTORICAL_EXECUTOR_RUN_ATTEMPT,HISTORICAL_REQUEST_WORKFLOW,HISTORICAL_REQUEST_EVENT,
        HISTORICAL_SOURCE_COMMIT,HISTORICAL_FIRST_PARENT,HISTORICAL_COMPARE_HEAD_0,
        HISTORICAL_ASSOCIATED_PULL_NUMBER,HISTORICAL_ASSOCIATED_COMMIT,HISTORICAL_PULL_STATE,
        HISTORICAL_PULL_CURRENT_HEAD,HISTORICAL_EXECUTOR_TITLE,HISTORICAL_EXECUTOR_HEAD,
        HISTORICAL_EXECUTOR_ACTOR_LOGIN,HISTORICAL_EXECUTOR_ACTOR_ID,HISTORICAL_EXECUTOR_TRIGGERING_LOGIN,
        HISTORICAL_CHECK_NAME,HISTORICAL_CHECK_APP_ID,HISTORICAL_CHECK_HEAD,HISTORICAL_CHECK_EXTERNAL_ID,
        HISTORICAL_CHECK_STATUS,HISTORICAL_CHECK_CONCLUSION,HISTORICAL_CHECK_TITLE,
        HISTORICAL_REQUEST_STATUS,HISTORICAL_REQUEST_CONCLUSION,HISTORICAL_EXECUTOR_STATUS,
        HISTORICAL_EXECUTOR_CONCLUSION};
    String8 bad_values[] = {d40,S8("behind"),S8("2"),S8("2"),S8("2"),S8("2"),S8("foreign.yml"),S8("push"),
        d40,d40,d40,S8("43"),d40,S8("unknown"),S8("-"),S8("wrong title"),d40,
        S8("attacker"),S8("1"),S8("attacker"),S8("other check"),S8("1"),d40,S8("wrong marker"),
        S8("queued"),S8("failure"),S8("invalid packet"),S8("queued"),S8("failure"),S8("queued"),S8("failure")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_fields); i += 1)
    {
        String8 saved = api[bad_fields[i]];
        api[bad_fields[i]] = bad_values[i];
        proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,acquisition_history,pilot_text,plan_text,plan_text);
        bool rejected = !compiler_sampling_historical_validate(arena,config,marker,facts,
            acquisition_history,pilot_text,plan_text,plan_text,proof).valid;
        valid = rejected && valid; controls += 1;
        api[bad_fields[i]] = saved;
    }
    // Fact/config edits get newly computed data hashes. These controls reach
    // the native owner/freeze/freshness checks rather than digest refusal only.
    u64 bad_fact_fields[] = {SAMPLING_FACT_OWNER_LOGIN,SAMPLING_FACT_OWNER_ID,SAMPLING_FACT_ACTOR_LOGIN,
        SAMPLING_FACT_TRIGGERING_LOGIN,SAMPLING_FACT_PULL_AUTHOR_LOGIN,SAMPLING_FACT_REPOSITORY,
        SAMPLING_FACT_FRESH_PARENT_0,SAMPLING_FACT_PARENT_COUNT};
    String8 bad_fact_values[] = {S8("attacker"),S8("1"),S8("attacker"),S8("attacker"),S8("attacker"),
        S8("fork/buster"),S8("-"),S8("3")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(bad_fact_fields); i += 1)
    {
        String8 saved = fact_values[bad_fact_fields[i]];
        fact_values[bad_fact_fields[i]] = bad_fact_values[i];
        String8 changed = compiler_sampling_admission_fixture_fields(arena,github_names,github_fields);
        proof = compiler_sampling_historical_fixture_api(arena,api,config,changed,acquisition_history,pilot_text,plan_text,plan_text);
        bool rejected = !compiler_sampling_historical_validate(arena,config,marker,changed,
            acquisition_history,pilot_text,plan_text,plan_text,proof).valid;
        valid = rejected && valid; controls += 1;
        fact_values[bad_fact_fields[i]] = saved;
    }
    String8 saved_sha = config_values[SAMPLING_CONFIG_FREEZE_SHA];
    config_values[SAMPLING_CONFIG_FREEZE_SHA] = a;
    String8 changed_config = compiler_sampling_admission_fixture_fields(arena,config_names,config_fields);
    proof = compiler_sampling_historical_fixture_api(arena,api,changed_config,facts,acquisition_history,pilot_text,plan_text,plan_text);
    valid = !compiler_sampling_historical_validate(arena,changed_config,marker,facts,
        acquisition_history,pilot_text,plan_text,plan_text,proof).valid && valid; controls += 1;
    config_values[SAMPLING_CONFIG_FREEZE_SHA] = saved_sha;
    // A later acquisition ID is disallowed even when its phase/campaign are
    // otherwise a valid native prefix; failed and hostless prior rows stay bad.
    String8 prefix_states[] = {S8("complete"),S8("failed"),S8("not_run")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(prefix_states); i += 1)
    {
        String8 row = compiler_sampling_admission_fixture_row(arena,S8("acquire"),0,
            i == 0 ? 30000 : 80,180,prefix_states[i],1800000000,plan_sha,a40);
        String8 changed_history = string_format(arena,S8("{S8}{S8}"),header,row);
        proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,changed_history,pilot_text,plan_text,plan_text);
        valid = !compiler_sampling_historical_validate(arena,config,marker,facts,
            changed_history,pilot_text,plan_text,plan_text,proof).valid && valid; controls += 1;
    }
    proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,acquisition_history,pilot_text,plan_text,plan_text);
    valid = !compiler_sampling_historical_validate(arena,config,marker,facts,
        acquisition_history,pilot_text,plan_text,pilot_text,proof).valid && valid; controls += 1;
    // Two Git parents both require the fresh immutable selector.
    fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("2");
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = marker;
    api[HISTORICAL_SECOND_PARENT] = a40;
    api[HISTORICAL_COMPARE_PARENT_1] = a40;
    api[HISTORICAL_COMPARE_HEAD_1] = c40;
    facts = compiler_sampling_admission_fixture_fields(arena,github_names,github_fields);
    proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,acquisition_history,pilot_text,plan_text,plan_text);
    valid = compiler_sampling_historical_validate(arena,config,marker,facts,
        acquisition_history,pilot_text,plan_text,plan_text,proof).valid && valid; controls += 1;
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = S8("-");
    String8 stale_facts = compiler_sampling_admission_fixture_fields(arena,github_names,github_fields);
    proof = compiler_sampling_historical_fixture_api(arena,api,config,stale_facts,acquisition_history,pilot_text,plan_text,plan_text);
    valid = !compiler_sampling_historical_validate(arena,config,marker,stale_facts,
        acquisition_history,pilot_text,plan_text,plan_text,proof).valid && valid; controls += 1;
    fact_values[SAMPLING_FACT_PARENT_COUNT] = S8("1");
    fact_values[SAMPLING_FACT_FRESH_PARENT_1] = S8("-");
    api[HISTORICAL_SECOND_PARENT] = api[HISTORICAL_COMPARE_PARENT_1] = api[HISTORICAL_COMPARE_HEAD_1] = S8("-");
    freeze.phase = S8("confirm");
    freeze.campaign_parent = pilot_sha;
    freeze.campaign_parent_revision = b40;
    freeze.candidate_pairs = S8("40");
    freeze.selected_candidate = S8("compiler-main-40pairs-candidate-v1");
    freeze.calibration_ab1_low_percent = freeze.calibration_ab2_low_percent = S8("2.0");
    freeze.calibration_ab1_high_percent = freeze.calibration_ab2_high_percent = S8("2.5");
    String8 confirm_text = compiler_sampling_freeze_fixture(arena,freeze);
    String8 confirm_sha = stage_object_sha256_bytes(arena,confirm_text.pointer,confirm_text.length);
    config_values[SAMPLING_CONFIG_STATE] = S8("confirm");
    config_values[SAMPLING_CONFIG_FREEZE_REVISION] = c40;
    config_values[SAMPLING_CONFIG_FREEZE_SHA] = confirm_sha;
    config_values[SAMPLING_CONFIG_PARENT_SHA] = pilot_sha;
    config_values[SAMPLING_CONFIG_PARENT_REVISION] = b40;
    config = compiler_sampling_admission_fixture_fields(arena,config_names,config_fields);
    String8List rows = {0};
    string8_list_push(arena,&rows,acquisition_history);
    for (u64 i = 0; i < 3; i += 1)
        string8_list_push(arena,&rows,compiler_sampling_admission_fixture_row(arena,S8("pilot"),i,
            81 + i,181 + i,S8("complete"),3000000000,pilot_sha,b40));
    for (u64 i = 0; i < 39; i += 1)
        string8_list_push(arena,&rows,compiler_sampling_admission_fixture_row(arena,S8("confirm"),i,
            84 + i,184 + i,S8("complete"),1000000,confirm_sha,c40));
    String8 confirm_history = string_join_arena(arena,string8_list_to_slice(arena,rows),false);
    marker = string_format(arena,S8("profile: compiler-main-sampling-confirm-v1 packet: 39 freeze: {S8}"),c40);
    fact_values[SAMPLING_FACT_FRESH_PARENT_0] = marker;
    facts = compiler_sampling_admission_fixture_fields(arena,github_names,github_fields);
    api[HISTORICAL_CHECK_EXTERNAL_ID] = string_format(arena,
        S8("buster-main-sampling-v1:{S8}:confirm:39:10000:20000:1"),confirm_sha);
    proof = compiler_sampling_historical_fixture_api(arena,api,config,facts,confirm_history,confirm_text,pilot_text,plan_text);
    observed = compiler_sampling_historical_validate(arena,config,marker,facts,confirm_history,confirm_text,pilot_text,plan_text,proof);
    valid = observed.valid && observed.packet.packet == 39 &&
        observed.packet.reservation_seconds == 960 && string_equal(observed.packet.family,S8("ab2")) && valid;
    controls += 1;
    valid = !compiler_sampling_admission_validate(arena,config,marker,facts,confirm_history,confirm_text,pilot_text).valid && valid;
    controls += 1;
    // Strict record controls: the API proof is ordered, complete and bounded.
    u64 suffix = 0, lines = 0;
    while (suffix < proof.length && lines < 2)
    {
        if (proof.pointer[suffix] == '\n') lines += 1;
        suffix += 1;
    }
    String8 reordered = string_format(arena,
        S8("repository\tbuster14a/buster\nschema\tbuster-main-sampling-historical-api-v1\n{S8}"),
        string_slice(proof,suffix,proof.length));
    String8 malformed[] = {
        string_format(arena,S8("{S8}schema\tbuster-main-sampling-historical-api-v1\n"),proof),
        string_format(arena,S8("{S8}unknown\tvalue\n"),proof),
        string_slice(proof,0,proof.length - 1),reordered,
        string_format(arena,S8("{S8}unknown\t\x01\n"),proof)};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(malformed); i += 1)
    {
        valid = !compiler_sampling_historical_validate(arena,config,marker,facts,
            confirm_history,confirm_text,pilot_text,plan_text,malformed[i]).valid && valid;
        controls += 1;
    }
    valid = controls == 57 && valid;
    string_print(S8("COMPILER_SAMPLING_HISTORICAL_SELF_TEST controls={u64} state={S8} physical_execution=none qualification=unqualified\n"),
        controls,valid ? S8("complete") : S8("failed"));
    return valid;
}
#endif
