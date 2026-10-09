// Canonical MAIN comparison entry. Native C verifies authenticated runtime
// identity and owns the exact pinned ordinary comparator before its first
// child. Feature selection belongs to the immutable MAIN route, not argv.
// Runtime/source evidence is kept outside ordinary scratch until quiet.
#ifndef BUSTER_COMPILER_MAIN_COMPARISON_CONTROLLER_INCLUDED
#define BUSTER_COMPILER_MAIN_COMPARISON_CONTROLLER_INCLUDED

#define BUSTER_MAIN_WHOLE_BUDGET_US 5400000000ull
#define BUSTER_MAIN_WORKER_BUDGET_US 5280000000ull

BUSTER_GLOBAL_LOCAL String8 compiler_main_controller_route_names[]={
    S8("main_owned"),S8("main_profile"),S8("main_preparation_policy"),S8("main_phase_schema"),
    S8("main_measurement_revision"),S8("main_certificate_revision"),S8("main_certificate_sha256"),
    S8("lab_sha256"),S8("python_path"),S8("python_sha256"),S8("driver_sha256"),S8("compare_sha256"),
    S8("receipt_sha256"),S8("owned_phase_sha256"),S8("owned_plan_sha256"),S8("trusted_root"),
    S8("candidate_root"),S8("work_root"),S8("evidence_root"),S8("main_policy_revision")};
BUSTER_GLOBAL_LOCAL String8 compiler_main_controller_identity_names[]={
    S8("schema"),S8("base"),S8("base_tree"),S8("head"),S8("head_tree"),S8("pull"),
    S8("pull_head"),S8("first_parent"),S8("range")};

typedef struct CompilerMainComparisonController CompilerMainComparisonController;
struct CompilerMainComparisonController
{
    String8 route[20],facts[16],identity[9];
    String8 route_text,facts_text,identity_text,route_path,facts_path,identity_path,driver,proof_root;
    CompilerExperimentJobClock clock;
    bool valid;
};

BUSTER_GLOBAL_LOCAL bool compiler_main_controller_environment(CompilerMainComparisonController state)
{
    String8 keys[]={S8("GITHUB_REPOSITORY"),S8("GITHUB_RUN_ID"),S8("GITHUB_RUN_ATTEMPT"),
        S8("GITHUB_SHA"),S8("GITHUB_JOB"),S8("GITHUB_EVENT_NAME"),S8("GITHUB_REF"),
        S8("BQ_REPOSITORY"),S8("BQ_RUN_ID"),S8("BQ_RUN_ATTEMPT"),S8("BQ_TRUSTED_REVISION"),S8("BQ_HEAD_COMMIT"),
        S8("BQ_REQUEST_RUN_ID"),S8("BQ_REQUEST_ATTEMPT"),S8("BQ_BASE_COMMIT"),S8("BQ_BASE_TREE"),
        S8("BQ_HEAD_TREE"),S8("BQ_PULL"),S8("BQ_PULL_HEAD"),S8("BQ_FIRST_PARENT"),S8("BQ_RANGE")};
    String8 values[]={S8("buster14a/buster"),state.facts[2],state.facts[3],state.facts[4],S8("compare"),
        S8("workflow_run"),S8("refs/heads/main"),S8("buster14a/buster"),state.facts[2],state.facts[3],state.facts[4],state.identity[3],
        state.facts[8],state.facts[9],state.identity[1],state.identity[2],state.identity[4],
        state.identity[5],state.identity[6],state.identity[7],state.identity[8]};
    bool valid=BUSTER_ARRAY_LENGTH(keys)==BUSTER_ARRAY_LENGTH(values);
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(keys);i+=1)
        valid=compiler_experiment_job_clock_matches(keys[i],values[i]);
    String8 route_keys[]={S8("BQ_MAIN_OWNED"),S8("BQ_MAIN_PROFILE"),S8("BQ_MAIN_PREPARATION_POLICY"),
        S8("BQ_MAIN_PHASE_SCHEMA"),S8("BQ_MAIN_MEASUREMENT_REVISION"),S8("BQ_MAIN_CERTIFICATE_REVISION"),
        S8("BQ_MAIN_CERTIFICATE_SHA256"),S8("BQ_MAIN_LAB_SHA256"),S8("BQ_MAIN_PYTHON_PATH"),
        S8("BQ_MAIN_PYTHON_SHA256"),S8("BQ_MAIN_DRIVER_SHA256"),S8("BQ_MAIN_COMPARE_SHA256"),
        S8("BQ_MAIN_RECEIPT_SHA256"),S8("BQ_MAIN_OWNED_PHASE_SHA256"),S8("BQ_MAIN_OWNED_PLAN_SHA256"),
        S8("BQ_MAIN_TRUSTED_ROOT"),S8("BQ_MAIN_CANDIDATE_ROOT"),S8("BQ_MAIN_WORK_ROOT"),
        S8("BQ_MAIN_EVIDENCE_ROOT"),S8("BQ_MAIN_POLICY_REVISION")};
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(route_keys);i+=1)
        valid=compiler_experiment_job_clock_matches(route_keys[i],state.route[i]);
    return valid;
}

BUSTER_GLOBAL_LOCAL CompilerMainComparisonController compiler_main_controller_resolve(Arena* arena,
    SliceString8 arguments)
{
    CompilerMainComparisonController state={0};
    bool valid=arguments.length==9 && string_equal(arguments.pointer[0],S8("--execute-main")) &&
        string_equal(arguments.pointer[1],S8("--route")) && string_equal(arguments.pointer[3],S8("--facts")) &&
        string_equal(arguments.pointer[5],S8("--identity")) && string_equal(arguments.pointer[7],S8("--driver"));
#if BUSTER_LINUX && !BUSTER_ANDROID
    // Exact approved CPU and durable UNKNOWN refusal precede claims or writes.
    CompilerSamplingControllerHost host=valid ? compiler_sampling_controller_observed_host(arena) :
        (CompilerSamplingControllerHost){0};
    valid=valid && host.valid && compiler_experiment_cleanup_guard(arena) && compiler_closure_admitting() &&
        !compiler_sampling_controller_cancelled();
    if (valid)
    {
        state.route_path=arguments.pointer[2]; state.facts_path=arguments.pointer[4];
        state.identity_path=arguments.pointer[6]; state.driver=arguments.pointer[8];
        String8 paths[]={state.route_path,state.facts_path,state.identity_path,state.driver};
        for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(paths);i+=1)
            valid=compiler_main_absolute(paths[i]) && string_equal(os_path_absolute(arena,paths[i],true),paths[i]);
        valid=valid && compiler_main_route_read(arena,state.route_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&state.route_text) &&
            compiler_main_route_read(arena,state.facts_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&state.facts_text) &&
            compiler_main_route_read(arena,state.identity_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&state.identity_text) &&
            compiler_main_fields(state.route_text,(SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_controller_route_names),state.route) &&
            compiler_main_fields(state.facts_text,(SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_fact_names),state.facts) &&
            compiler_main_fields(state.identity_text,(SliceString8)BUSTER_ARRAY_TO_SLICE(compiler_main_controller_identity_names),state.identity);
    }
    String8* route=state.route; String8* facts=state.facts; String8* identity=state.identity;
    valid=valid && string_equal(route[0],S8("true")) &&
        (string_equal(route[1],S8("compiler-compare-v1")) || string_equal(route[1],S8("compiler-main-40pairs-v1"))) &&
        (string_equal(route[2],S8("legacy-rebuild")) || string_equal(route[2],S8("snapshot-v1"))) &&
        string_equal(route[3],S8("buster-compiler-main-owned-phases-v1")) &&
        compiler_sampling_hex(route[4],40) && compiler_sampling_hex(route[19],40) &&
        string_equal(route[19],facts[4]) &&
        string_equal(facts[0],S8("buster-compiler-main-route-github-facts-v1")) &&
        string_equal(facts[1],S8("buster14a/buster")) &&
        string_equal(facts[5],S8(".github/workflows/9700x-direct-bench.yml")) &&
        string_equal(facts[6],S8("workflow_run")) && string_equal(facts[7],S8("main")) &&
        string_equal(facts[11],S8(".github/workflows/9700x-compiler-request.yml")) &&
        string_equal(facts[12],S8("push")) && string_equal(facts[13],S8("main")) &&
        string_equal(facts[14],S8("completed")) && string_equal(facts[15],S8("success")) &&
        string_equal(identity[0],S8("buster-compiler-main-identity-v1")) &&
        string_equal(identity[3],facts[10]) && compiler_sampling_hex(facts[10],40);
    u64 hash_indices[]={7,9,10,11,12,13,14};
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(hash_indices);i+=1)
        valid=compiler_sampling_hex(route[hash_indices[i]],64);
    u64 identity_hashes[]={1,2,3,4,6,7};
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(identity_hashes);i+=1)
        valid=compiler_sampling_hex(identity[identity_hashes[i]],40);
    u64 fact_numbers[]={2,3,8,9},number=0;
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(fact_numbers);i+=1)
        valid=compiler_experiment_job_clock_decimal(facts[fact_numbers[i]],&number) && number>0;
    valid=valid && !string_equal(facts[2],facts[8]) &&
        compiler_experiment_job_clock_decimal(identity[5],&number) &&
        compiler_experiment_job_clock_decimal(identity[8],&number) && number>0 &&
        compiler_main_controller_environment(state);
    for (u64 i=15;valid && i<19;i+=1)
    {
        valid=compiler_main_absolute(route[i]) &&
            string_equal(os_path_absolute_lexical(arena,route[i],true),route[i]) &&
            string_equal(os_path_absolute(arena,path_parent(arena,route[i]),true),path_parent(arena,route[i]));
        for (u64 j=15;valid && j<i;j+=1) valid=!compiler_sampling_path_overlap(route[i],route[j]);
    }
    // The route carries effective roots independently derived from the
    // approved canonical namespace and original API executor/attempt.
    String8 suffix=string_format(arena,S8("run-{S8}-attempt{S8}"),facts[2],facts[3]);
    for (u64 i=17;valid && i<19;i+=1)
        valid=string_equal(route[i],path_join(arena,path_parent(arena,route[i]),suffix));
    valid=valid && string_equal(os_path_absolute(arena,route[15],true),route[15]) &&
        string_equal(os_path_absolute(arena,route[16],true),route[16]) &&
        generate_path_kind(arena,route[17])==GENERATE_PATH_MISSING &&
        generate_path_kind(arena,route[18])==GENERATE_PATH_MISSING;
    if (valid)
    {
        state.proof_root=string_format(arena,S8("{S8}.native"),route[18]);
        valid=compiler_main_absolute(state.proof_root) &&
            generate_path_kind(arena,state.proof_root)==GENERATE_PATH_MISSING;
        for (u64 i=15;valid && i<19;i+=1)
            valid=!compiler_sampling_path_overlap(state.proof_root,route[i]);
        valid=valid && compiler_experiment_job_clock_resolve(arena,S8("main"),facts[4],&state.clock) &&
            compiler_experiment_job_clock_remaining_us(state.clock,BUSTER_MAIN_WHOLE_BUDGET_US,
                BUSTER_MAIN_WORKER_BUDGET_US)>=1000000ull;
    }
#else
    valid=false;
    BUSTER_UNUSED(arena);
#endif
    state.valid=valid;
    return state;
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_main_controller_compare(Arena* arena,
    CompilerMainComparisonController state)
{
    String8* route=state.route; String8* facts=state.facts; String8* identity=state.identity;
    String8 command[]={route[8],S8("-B"),path_join(arena,route[15],S8("tools/bench_direct/compiler_compare.py")),
        S8("--candidate"),route[16],S8("--lab"),path_join(arena,route[15],S8("tools/uarch_lab.py")),
        S8("--work"),route[17],S8("--evidence"),route[18],S8("--summary"),
        path_join(arena,state.proof_root,S8("main-summary.md")),S8("--closure-policy"),route[2],
        S8("--main-owned-phases"),S8("--main-profile"),route[1],S8("--closure-driver"),state.driver,
        S8("--mode"),S8("main"),S8("--repository"),S8("buster14a/buster"),S8("--ref"),S8("refs/heads/main"),
        S8("--pull"),identity[5],S8("--pull-head"),identity[6],S8("--base"),identity[1],
        S8("--base-tree"),identity[2],S8("--head"),identity[3],S8("--head-tree"),identity[4],
        S8("--trusted-revision"),route[4],S8("--request-run-id"),facts[8],S8("--run-id"),facts[2],
        S8("--run-attempt"),facts[3]};
    OsArgumentBuilder builder=os_argument_builder_start(arena);
    for (u64 i=0;i<BUSTER_ARRAY_LENGTH(command);i+=1) os_argument_builder_append(&builder,command[i]);
    return os_argument_builder_flush(&builder);
}

BUSTER_GLOBAL_LOCAL bool compiler_main_controller_inputs(Arena* arena,CompilerMainComparisonController state)
{
    String8 observed={0};
    bool valid=compiler_main_route_read(arena,state.route_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&observed) &&
        string_equal(observed,state.route_text);
    valid=valid && compiler_main_route_read(arena,state.facts_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&observed) &&
        string_equal(observed,state.facts_text);
    valid=valid && compiler_main_route_read(arena,state.identity_path,BUSTER_MAIN_ROUTE_RECORD_LIMIT,&observed) &&
        string_equal(observed,state.identity_text);
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_controller_copy(Arena* arena,CompilerMainComparisonController state)
{
    String8 names[]={S8("main-owner.json"),S8("main-owner.json.argv"),S8("main-owner.json.stdout"),
        S8("main-owner.json.stderr"),S8("main-owner.json.bootstrap.complete"),S8("main-runtime.tsv"),
        S8("main-clock.tsv"),S8("physical-job-clock.tsv"),S8("main-route.tsv"),S8("main-facts.tsv"),
        S8("main-identity.tsv"),S8("main-summary.md")};
    bool valid=string_equal(os_path_absolute(arena,state.route[18],true),state.route[18]) &&
        string_equal(os_path_absolute(arena,state.proof_root,true),state.proof_root);
    for (u64 i=0;i<BUSTER_ARRAY_LENGTH(names);i+=1)
    {
        String8 content={0},target=path_join(arena,state.route[18],names[i]);
        bool copied=valid && generate_path_kind(arena,target)==GENERATE_PATH_MISSING &&
            compiler_main_route_read(arena,path_join(arena,state.proof_root,names[i]),
                BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT,&content) &&
            file_publish(target,BUSTER_SLICE_TO_BYTE_SLICE(content));
        valid=valid && copied;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_comparison_controller_main(Arena* arena,SliceString8 arguments)
{
    ProcessResult result=PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    CompilerMainComparisonController state=compiler_main_controller_resolve(arena,arguments);
    OsDirectoryCreateResult claim=state.valid ? os_make_directory_exclusive(state.proof_root) :
        (OsDirectoryCreateResult){0};
    bool claimed=state.valid && claim.created && !claim.error.v;
    if (claimed)
    {
        // This durable, never auto-deleted claim precedes every write/child.
        OsDirectoryCreateResult work=os_make_directory_exclusive(state.route[17]);
        OsDirectoryCreateResult evidence=work.created && !work.error.v ?
            os_make_directory_exclusive(state.route[18]) : (OsDirectoryCreateResult){0};
        bool storage=work.created && !work.error.v && evidence.created && !evidence.error.v;
        bool retained=storage &&
            file_publish(path_join(arena,state.proof_root,S8("main-route.tsv")),BUSTER_SLICE_TO_BYTE_SLICE(state.route_text)) &&
            file_publish(path_join(arena,state.proof_root,S8("main-facts.tsv")),BUSTER_SLICE_TO_BYTE_SLICE(state.facts_text)) &&
            file_publish(path_join(arena,state.proof_root,S8("main-identity.tsv")),BUSTER_SLICE_TO_BYTE_SLICE(state.identity_text)) &&
            file_publish(path_join(arena,state.proof_root,S8("physical-job-clock.tsv")),BUSTER_SLICE_TO_BYTE_SLICE(state.clock.record));
        String8 runtime[]={S8("--verify-main-runtime"),state.route_path,state.facts_path,state.route[15],
            state.route[16],state.route[17],state.route[18],state.driver,
            path_join(arena,state.proof_root,S8("main-runtime.tsv"))};
        bool verified=retained && compiler_main_controller_inputs(arena,state) &&
            compiler_main_runtime_main(arena,(SliceString8)BUSTER_ARRAY_TO_SLICE(runtime))==PROCESS_RESULT_SUCCESS;
        CompilerClosureBootstrapIdentity bootstrap={0};
        String8 trusted={0},marker={0};
        verified=verified && compiler_closure_owned_bootstrap(arena,state.driver,&trusted,&marker,&bootstrap) &&
            string_equal(trusted,state.route[15]) && string_equal(bootstrap.artifact_sha256,state.route[10]) &&
            compiler_main_controller_inputs(arena,state) && compiler_main_controller_environment(state);
        CompilerSamplingSignalScope signals={0};
        bool deferred=verified && compiler_closure_utility_controller_signals_begin(&signals);
        u64 remaining=deferred ? compiler_experiment_job_clock_remaining_us(state.clock,
            BUSTER_MAIN_WHOLE_BUDGET_US,BUSTER_MAIN_WORKER_BUDGET_US) : 0;
        u64 seconds=remaining/1000000ull;
        u64 admitted_elapsed=remaining<=BUSTER_MAIN_WORKER_BUDGET_US ? BUSTER_MAIN_WORKER_BUDGET_US-remaining : 0;
        bool clock_order=remaining>0 && remaining<=BUSTER_MAIN_WORKER_BUDGET_US &&
            admitted_elapsed>=state.clock.entry_elapsed_us;
        u64 native_elapsed=clock_order ? admitted_elapsed-state.clock.entry_elapsed_us : 0;
        // These are the actual conservative shared-clock admission values.
        // Whole-job publication remains a separate authenticated API fact.
        String8 clock_record=string_format(arena,
            S8("schema\tbuster-compiler-main-clock-v1\n"
               "physical_job_clock_sha256\t{S8}\n"
               "job_elapsed_at_native_entry_us\t{u64}\n"
               "native_elapsed_at_owner_admission_us\t{u64}\n"
               "remaining_us\t{u64}\ntimeout_seconds\t{u64}\n"),
            stage_object_sha256_bytes(arena,(u8*)state.clock.record.pointer,state.clock.record.length),
            state.clock.entry_elapsed_us,native_elapsed,remaining,seconds);
        bool clock_written=deferred && clock_order && seconds>0 &&
            file_publish(path_join(arena,state.proof_root,S8("main-clock.tsv")),BUSTER_SLICE_TO_BYTE_SLICE(clock_record));
        SliceString8 command=compiler_main_controller_compare(arena,state);
        String8 timeout=string_format(arena,S8("{u64}"),seconds);
        String8 owner_path=path_join(arena,state.proof_root,S8("main-owner.json"));
        String8 prefix[]={S8("owned-phase"),owner_path,state.route[15],timeout,state.route[10],
            bootstrap.marker_sha256,S8("--")};
        OsArgumentBuilder builder=os_argument_builder_start(arena);
        for (u64 i=0;i<BUSTER_ARRAY_LENGTH(prefix);i+=1) os_argument_builder_append(&builder,prefix[i]);
        for (u64 i=0;i<command.length;i+=1) os_argument_builder_append(&builder,command.pointer[i]);
        ProcessResult owned=clock_written && compiler_closure_admitting() && compiler_experiment_cleanup_guard(arena) ?
            compiler_closure_owned_phase(arena,os_argument_builder_flush(&builder)) : PROCESS_RESULT_FAILED;
        bool restored=deferred && compiler_closure_utility_controller_signals_end(&signals);
        bool quiet=!compiler_closure_cleanup_failed && compiler_experiment_cleanup_guard(arena) &&
            generate_path_kind(arena,path_join(arena,state.route[18],S8("cleanup-uncertain")))==GENERATE_PATH_MISSING;
        // Unknown cleanup retains both roots and forbids later publication writes.
        bool copied=quiet && compiler_main_controller_copy(arena,state);
        bool within=compiler_experiment_job_clock_remaining_us(state.clock,BUSTER_MAIN_WHOLE_BUDGET_US,
            BUSTER_MAIN_WORKER_BUDGET_US)>0;
        result=owned==PROCESS_RESULT_SUCCESS && restored && copied && within && compiler_closure_admitting() &&
            !compiler_sampling_controller_cancelled() ? PROCESS_RESULT_SUCCESS:PROCESS_RESULT_FAILED;
        string_print(S8("COMPILER_MAIN_NATIVE_OWNER profile={S8} policy={S8} claimed=1 verified={u64} "
            "manager_complete={u64} restored={u64} cleanup_known={u64} retained_copy={u64} "
            "within_worker_budget={u64} success={u64} complete_job_cost=unavailable proof_root={S8}\n"),
            state.route[1],state.route[2],(u64)verified,(u64)(owned==PROCESS_RESULT_SUCCESS),(u64)restored,
            (u64)quiet,(u64)copied,(u64)within,(u64)(result==PROCESS_RESULT_SUCCESS),state.proof_root);
    }
    else string_print(S8("COMPILER_MAIN_NATIVE_REFUSED physical runtime, authenticated route, deadline or duplicate claim\n"));
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(arguments);
#endif
    return result;
}
#endif
