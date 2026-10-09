// Pure native policy fixtures; synthetic values are never benchmark evidence.
// Included after compiler_main_profile_policy.c. No child/host operation here.
typedef struct CompilerMainPolicyFixture CompilerMainPolicyFixture;
struct CompilerMainPolicyFixture
{
    String8 policy[22], facts[16], certificate[43], reviews[17], criteria[16];
    String8 campaign, archive;
    bool no_sampling;
};

BUSTER_GLOBAL_LOCAL String8 compiler_main_policy_test_fields(Arena* arena, String8* names, String8* values, u64 count)
{
    String8 result = compiler_sampling_admission_fixture_fields(arena,
        (SliceString8){names, count}, (SliceString8){values, count});
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_main_policy_test_tables(Arena* arena, CompilerMainPolicyFixture* fixture,
    u64 changed_row, u64 changed_column, String8 changed_value, bool change_archive)
{
    String8List rows = {0}, saved_rows = {0};
    string8_list_push(arena, &rows, S8("phase\tpacket\tfamily\trequest_run\trequest_attempt\texecutor_run\texecutor_attempt\tpolicy_revision\tmeasurement_revision\tfreeze_revision\tfreeze_sha256\tparent_freeze_sha256\tartifact_id\tartifact_sha256\tartifact_bytes\tjob_wall_us\tnative_wall_us\tstate\tslots\tslot_validations\tcorpus_cells\tcalibration_low_ppm\tcalibration_high_ppm\tshort_halfwidth_ppm\traw_replay_sha256\tterminal_api_sha256\tterminal_api_bytes\n"));
    string8_list_push(arena, &saved_rows, S8("phase\tpacket\trequest_run\texecutor_run\tartifact_id\tartifact_sha256\tartifact_bytes\tarchive_kind\tarchive_reference\tarchive_version\tarchive_sha256\tarchive_bytes\tarchive_receipt_sha256\n"));
    String8 hex = S8("0123456789abcdef");
    for (u64 index = 0; index < 46; index += 1)
    {
        String8 phase = index == 0 ? S8("acquire") : index < 4 ? S8("pilot") :
            index < 44 ? S8("confirm") : index == 44 ? S8("preparation") : S8("utility");
        u64 packet = index < 4 ? (index ? index - 1 : 0) : index < 44 ? index - 4 : 0;
        CompilerSamplingPacket schedule = index < 44 ? compiler_sampling_schedule(phase, packet) : (CompilerSamplingPacket){0};
        String8 outcomes = S8("-"), validations = S8("-");
        if (index > 0 && index < 44)
        {
            String8List results = {0}, valid = {0};
            for (u64 slot = 0; slot < schedule.count; slot += 1)
            {
                String8 verdict = string_equal(schedule.family, S8("aa")) ? S8("unchanged") : S8("slower");
                string8_list_push(arena, &results, string_format(arena, S8("{S8}{S8}"), slot ? S8(",") : S8(""), verdict));
                string8_list_push(arena, &valid, slot ? S8(",valid") : S8("valid"));
            }
            outcomes = string_join_arena(arena, string8_list_to_slice(arena, results), false);
            validations = string_join_arena(arena, string8_list_to_slice(arena, valid), false);
        }
        u64 ref = index == 0 ? 11 : index < 4 ? 13 : index < 44 ? 15 : index == 44 ? 17 : 19;
        String8 row[] = {phase, string_format(arena, S8("{u64}"), packet), index < 44 ? schedule.family : phase,
            string_format(arena, S8("{u64}"), 1000 + index), S8("1"), string_format(arena, S8("{u64}"), 2000 + index),
            S8("1"), S8("cccccccccccccccccccccccccccccccccccccccc"), fixture->certificate[3],
            fixture->certificate[ref], fixture->certificate[ref + 1],
            index == 0 || index >= 44 ? S8("-") : fixture->certificate[index < 4 ? 12 : 14],
            string_format(arena, S8("{u64}"), 3000 + index),
            S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), S8("100"),
            S8("1000000"), S8("500000"), S8("complete"), outcomes, validations,
            index == 0 ? S8("0") : index < 44 ? S8("12") : index == 44 ? S8("60") : S8("24"),
            index > 1 && index < 4 ? S8("21000") : S8("-"), index > 1 && index < 4 ? S8("24000") : S8("-"),
            index > 0 && index < 4 ? S8("5000") : S8("-"),
            S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"), S8("-"), S8("0")};
        bool not_run = fixture->no_sampling && index < 44;
        if (not_run)
        {
            for (u64 i = 3; i < BUSTER_ARRAY_LENGTH(row); i += 1) row[i] = S8("-");
            row[14] = S8("0"); row[17] = S8("not_run"); row[20] = S8("0"); row[26] = S8("0");
        }
        if (!change_archive && index == changed_row && changed_column < BUSTER_ARRAY_LENGTH(row))
            row[changed_column] = changed_value;
        String8List columns = {0};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(row); i += 1)
            string8_list_push(arena, &columns, string_format(arena, S8("{S8}{S8}"), row[i],
                i + 1 == BUSTER_ARRAY_LENGTH(row) ? S8("\n") : S8("\t")));
        string8_list_push(arena, &rows, string_join_arena(arena, string8_list_to_slice(arena, columns), false));
        String8 saved[] = {row[0],row[1],row[3],row[5],row[12],row[13],row[14],S8("library"),
            string_format(arena, S8("libfile_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa{S8}{S8}"),
                string_slice(hex,index/16,index/16+1),string_slice(hex,index%16,index%16+1)),S8("0"),row[13],row[14],
            S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee")};
        if (not_run)
        {
            for (u64 i = 7; i < BUSTER_ARRAY_LENGTH(saved); i += 1) saved[i] = S8("-");
            saved[11] = S8("0");
        }
        if (change_archive && index == changed_row && changed_column < BUSTER_ARRAY_LENGTH(saved))
            saved[changed_column] = changed_value;
        String8List archived = {0};
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(saved); i += 1)
            string8_list_push(arena, &archived, string_format(arena, S8("{S8}{S8}"), saved[i],
                i + 1 == BUSTER_ARRAY_LENGTH(saved) ? S8("\n") : S8("\t")));
        string8_list_push(arena, &saved_rows, string_join_arena(arena, string8_list_to_slice(arena, archived), false));
    }
    fixture->campaign = string_join_arena(arena, string8_list_to_slice(arena, rows), false);
    fixture->archive = string_join_arena(arena, string8_list_to_slice(arena, saved_rows), false);
}

BUSTER_GLOBAL_LOCAL CompilerMainPolicyFixture compiler_main_policy_test_fixture(Arena* arena)
{
    CompilerMainPolicyFixture result = {0};
    String8 hash = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    String8 ref = string_slice(hash,0,40);
    for (u64 i = 0; i < 22; i += 1) result.policy[i] = hash;
    result.policy[0]=S8("buster-compiler-main-profile-routing-v1"); result.policy[1]=S8("buster14a/buster");
    result.policy[2]=S8("qualified-40");result.policy[3]=ref;result.policy[4]=S8("compiler-main-40pairs-v1");
    result.policy[5]=S8("legacy-rebuild");result.policy[6]=S8("buster-compiler-main-owned-phases-v1");
    result.policy[8]=S8("/usr/bin/python3.12");result.policy[15]=ref;result.policy[17]=S8("-");
    result.policy[18]=S8("/home/runner/work/buster/buster/trusted");
    result.policy[19]=S8("/home/runner/work/buster/buster/candidate");
    result.policy[20]=S8("/home/runner/work/_temp/compiler-bench/work");
    result.policy[21]=S8("/home/runner/work/_temp/compiler-bench/evidence");
    String8 facts[]={S8("buster-compiler-main-route-github-facts-v1"),result.policy[1],S8("31"),S8("2"),ref,
        S8(".github/workflows/9700x-direct-bench.yml"),S8("workflow_run"),S8("main"),S8("29"),S8("1"),ref,
        S8(".github/workflows/9700x-compiler-request.yml"),S8("push"),S8("main"),S8("completed"),S8("success")};
    for (u64 i=0;i<16;i+=1) result.facts[i]=facts[i];
    for (u64 i=0;i<43;i+=1) result.certificate[i]=hash;
    result.certificate[0]=S8("buster-compiler-main-profile-certificate-v1");result.certificate[1]=S8("reviewed");
    result.certificate[2]=result.policy[1];result.certificate[3]=ref;result.certificate[4]=result.policy[4];
    u64 refs[]={5,7,9,11,13,15,17,19,29,30,31,41};
    for (u64 i=0;i<BUSTER_ARRAY_LENGTH(refs);i+=1) result.certificate[refs[i]]=ref;
    result.certificate[22]=result.policy[8];result.certificate[35]=S8("100");result.certificate[37]=S8("200");result.certificate[39]=S8("300");
    result.certificate[34]=S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    result.certificate[36]=S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    result.certificate[38]=S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    result.certificate[40]=S8("legacy-rebuild");
    result.reviews[0]=S8("buster-compiler-main-profile-reviews-v1");result.reviews[1]=ref;
    for (u64 i=2;i<=9;i+=1) result.reviews[i]=S8("accepted");
    for (u64 i=10;i<17;i+=1) result.reviews[i]=ref;
    String8 criteria[]={S8("buster-compiler-main-profile-criteria-v1"),ref,hash,
        S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        S8("-1000"),S8("1000"),S8("-1000"),S8("1000"),S8("-1000"),S8("1000"),S8("0"),hash,
        S8("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),S8("1000000"),S8("500000"),S8("1500000")};
    for (u64 i=0;i<16;i+=1) result.criteria[i]=criteria[i];
    compiler_main_policy_test_tables(arena,&result,46,27,(String8){0},false);
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerMainRoute compiler_main_policy_test_validate(Arena* arena, CompilerMainPolicyFixture fixture)
{
    String8 criterion_names[] = {S8("schema"), S8("measurement_revision"), S8("preparation_plan_sha256"), S8("preparation_raw_replay_sha256"), S8("legacy_immutable_aa_low_ppm"), S8("legacy_immutable_aa_high_ppm"), S8("snapshot_immutable_aa_low_ppm"), S8("snapshot_immutable_aa_high_ppm"), S8("snapshot_cross_build_aa_low_ppm"), S8("snapshot_cross_build_aa_high_ppm"), S8("aa_corpus_regressions"), S8("utility_plan_sha256"), S8("utility_raw_replay_sha256"), S8("utility_legacy_wall_us"), S8("utility_snapshot_wall_us"), S8("utility_job_wall_us")};
    String8 reviews=compiler_main_policy_test_fields(arena,compiler_main_review_names,fixture.reviews,17);
    String8 criteria=compiler_main_policy_test_fields(arena,criterion_names,fixture.criteria,16);
    fixture.certificate[6]=stage_object_sha256_bytes(arena,(u8*)fixture.campaign.pointer,fixture.campaign.length);
    fixture.certificate[8]=stage_object_sha256_bytes(arena,(u8*)fixture.archive.pointer,fixture.archive.length);
    fixture.certificate[10]=stage_object_sha256_bytes(arena,(u8*)reviews.pointer,reviews.length);
    fixture.certificate[42]=stage_object_sha256_bytes(arena,(u8*)criteria.pointer,criteria.length);
    String8 certificate=compiler_main_policy_test_fields(arena,compiler_main_certificate_names,fixture.certificate,43);
    fixture.policy[16]=stage_object_sha256_bytes(arena,(u8*)certificate.pointer,certificate.length);
    CompilerMainRoute result=compiler_main_route_validate(arena,
        compiler_main_policy_test_fields(arena,compiler_main_policy_names,fixture.policy,22),
        compiler_main_policy_test_fields(arena,compiler_main_fact_names,fixture.facts,16),
        certificate,fixture.campaign,fixture.archive,reviews,criteria,(String8){0});
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_route_self_test(Arena* arena)
{
    u64 cases=0,failures=0;
    CompilerMainPolicyFixture initial=compiler_main_policy_test_fixture(arena);
    CompilerMainRoute observed=compiler_main_policy_test_validate(arena,initial);
    cases+=1;failures+=!(observed.valid&&observed.sampling_eligible&&observed.snapshot_eligible);
    CompilerMainPolicyFixture snapshot=initial;snapshot.policy[5]=S8("snapshot-v1");snapshot.certificate[40]=S8("snapshot-v1");
    observed=compiler_main_policy_test_validate(arena,snapshot);
    cases+=1;failures+=!(observed.valid&&observed.snapshot_eligible);
    u64 rows[]={2,3,4,4,5,4,4,4,4,4,4,4,4,4,4,4,4};
    u64 cols[]={21,22,18,18,18,3,5,12,4,6,8,10,11,15,19,20,24};
    String8 changes[]={S8("19999"),S8("25001"),S8("unchanged,slower,unchanged,unchanged,unchanged"),
        S8("unchanged,inconclusive,unchanged,unchanged,unchanged"),S8("unchanged,slower,slower,slower,slower"),
        S8("1000"),S8("2000"),S8("3000"),S8("2"),S8("2"),S8("ffffffffffffffffffffffffffffffffffffffff"),
        S8("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        S8("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),S8("1440000001"),
        S8("valid,invalid,valid,valid,valid"),S8("0"),S8("bad")};
    for(u64 i=0;i<BUSTER_ARRAY_LENGTH(changes);i+=1)
    {
        CompilerMainPolicyFixture changed=initial;
        compiler_main_policy_test_tables(arena,&changed,rows[i],cols[i],changes[i],false);
        observed=compiler_main_policy_test_validate(arena,changed);
        cases+=1;failures+=observed.valid;
    }
    for(u64 field=2;field<=9;field+=1)
    {
        CompilerMainPolicyFixture changed=initial;changed.reviews[field]=S8("rejected");
        observed=compiler_main_policy_test_validate(arena,changed);
        bool sampling=field<=6||field==9;
        cases+=1;failures+=sampling?observed.valid:!(observed.valid&&!observed.snapshot_eligible);
    }
    u64 criterion_fields[]={4,5,6,7,8,9,10,13,14,15};
    String8 criterion_changes[]={S8("-5001"),S8("5001"),S8("-5001"),S8("5001"),S8("-5001"),S8("5001"),
        S8("1"),S8("0"),S8("0"),S8("2000000")};
    for(u64 i=0;i<BUSTER_ARRAY_LENGTH(criterion_fields);i+=1)
    {
        CompilerMainPolicyFixture changed=initial;changed.criteria[criterion_fields[i]]=criterion_changes[i];
        observed=compiler_main_policy_test_validate(arena,changed);
        cases+=1;failures+=!(observed.valid&&observed.sampling_eligible&&!observed.snapshot_eligible);
        changed.policy[5]=S8("snapshot-v1");changed.certificate[40]=S8("snapshot-v1");
        observed=compiler_main_policy_test_validate(arena,changed);
        cases+=1;failures+=observed.valid;
    }
    CompilerMainPolicyFixture no_sampling=snapshot;no_sampling.no_sampling=true;
    no_sampling.policy[2]=S8("owned-long");no_sampling.policy[4]=S8("compiler-compare-v1");
    u64 unused[]={11,12,13,14,15,16,31,32,33,34,35,36,37,38,39};
    for(u64 i=0;i<BUSTER_ARRAY_LENGTH(unused);i+=1)no_sampling.certificate[unused[i]]=S8("-");
    compiler_main_policy_test_tables(arena,&no_sampling,46,27,(String8){0},false);
    observed=compiler_main_policy_test_validate(arena,no_sampling);
    cases+=1;failures+=!(observed.valid&&!observed.sampling_eligible&&observed.snapshot_eligible);
    String8 disabled[22]={0};
    for(u64 i=0;i<22;i+=1)disabled[i]=S8("-");
    disabled[0]=initial.policy[0];disabled[1]=initial.policy[1];disabled[2]=S8("disabled");
    disabled[4]=S8("compiler-compare-v1");disabled[5]=S8("legacy-rebuild");
    String8 facts=compiler_main_policy_test_fields(arena,compiler_main_fact_names,initial.facts,16);
    observed=compiler_main_route_validate(arena,compiler_main_policy_test_fields(arena,compiler_main_policy_names,disabled,22),
        facts,(String8){0},(String8){0},(String8){0},(String8){0},(String8){0},(String8){0});
    cases+=1;failures+=!(observed.valid&&!observed.owned);
    String8 long_policy[22]={0};
    for(u64 i=0;i<22;i+=1)long_policy[i]=initial.policy[i];
    long_policy[2]=S8("owned-long");long_policy[4]=S8("compiler-compare-v1");long_policy[15]=S8("-");long_policy[16]=S8("-");
    String8 previous=compiler_main_policy_test_fields(arena,compiler_main_policy_names,long_policy,22);
    observed=compiler_main_route_validate(arena,previous,facts,(String8){0},(String8){0},(String8){0},(String8){0},(String8){0},(String8){0});
    cases+=1;failures+=!(observed.valid&&observed.owned);
    long_policy[2]=S8("rollback");long_policy[17]=S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    observed=compiler_main_route_validate(arena,compiler_main_policy_test_fields(arena,compiler_main_policy_names,long_policy,22),
        facts,(String8){0},(String8){0},(String8){0},(String8){0},(String8){0},previous);
    cases+=1;failures+=!(observed.valid&&observed.owned);
    long_policy[3]=S8("ffffffffffffffffffffffffffffffffffffffff");
    observed=compiler_main_route_validate(arena,compiler_main_policy_test_fields(arena,compiler_main_policy_names,long_policy,22),
        facts,(String8){0},(String8){0},(String8){0},(String8){0},(String8){0},previous);
    cases+=1;failures+=observed.valid;
    string_print(S8("COMPILER_MAIN_ROUTE_SELF_TEST cases={u64} failures={u64} physical_execution=none\n"),cases,failures);
    bool result=cases==52&&failures==0;
    return result;
}
