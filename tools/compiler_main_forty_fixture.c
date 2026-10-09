// Actual hosted fixed-40 MAIN data proof. Never an approved-host benchmark.
// Native claims/owners surround the real compiler, original lab and bounded
// export; no copied count labels, new inference, API authority or activation.
#if BUSTER_LINUX && !BUSTER_ANDROID
#define BUSTER_MAIN_FORTY_FIXTURE_SECONDS 480ull

BUSTER_GLOBAL_LOCAL String8 compiler_main_forty_adapter(Arena* arena,u64 version)
{
    String8 pieces[]={string_format(arena,S8("#define MAIN40_VARIANT {u64}\n"),version),
        S8("/* Private native Clang adapter for actual hosted lab data; never performance qualification. */\n#include <stdio.h>\n#include <string.h>\n#include <unistd.h>\n#include \"../fixture-dependency.h\"\n#include \"generated/ignored.h\"\n#include \"../build/generated/value.h\"\n#if FIXTURE_GENERATED != FIXTURE_VERSION || FIXTURE_BUILD != 1\n#error incorrect generated diagnostic input\n#endif\nint main(int argc,char** argv)\n{\n    int result=1;\n    if (argc==2 && strcmp(argv[1],\"--version\")==0)\n        result=printf(\"DIAGNOSTIC-UNQUALIFIED actual Clang adapter %d\\n\",MAIN40_VARIANT)>0 ? 0:1;\n    else if (argc>=3 && argc<=128 && strcmp(argv[1],\"cc\")==0)\n    {\n        char* forwarded[128]={0};\n        forwarded[0]=FIXTURE_CLANG;\n        for (int i=2;i<argc;i+=1) forwarded[i-1]=argv[i];\n        execv(FIXTURE_CLANG,forwarded);\n    }\n    return result;\n}\n")};
    return string_join_arena(arena,(SliceString8)BUSTER_ARRAY_TO_SLICE(pieces),false);
}

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_data(Arena* arena,String8 input,String8 output,
    String8 member,CompilerClosureUtilityExportTotals* totals,String8List* manifest,String8* digest)
{
    struct stat before={0},after={0};
    String8 observed={0};
    bool valid=os_now_microseconds()<totals->deadline &&
        compiler_sampling_controller_hash(arena,input,&observed,&before) && S_ISREG(before.st_mode) &&
        before.st_size>=0 && (u64)before.st_size<=BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT &&
        totals->files<BUSTER_UTILITY_EXPORT_FILES &&
        (u64)before.st_size<=BUSTER_UTILITY_EXPORT_BYTES-totals->bytes;
    String8 data=valid ? compiler_closure_read(arena,input,BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT) : (String8){0};
    String8 terminated=string_duplicate_arena(arena,input,true);
    valid=valid && data.pointer && data.length==(u64)before.st_size &&
        string_equal(observed,stage_object_sha256_bytes(arena,(u8*)data.pointer,data.length)) &&
        lstat((char*)terminated.pointer,&after)==0 && before.st_dev==after.st_dev &&
        before.st_ino==after.st_ino && before.st_mode==after.st_mode && before.st_size==after.st_size &&
        before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec &&
        before.st_ctim.tv_sec==after.st_ctim.tv_sec && before.st_ctim.tv_nsec==after.st_ctim.tv_nsec &&
        file_write(output,BUSTER_SLICE_TO_BYTE_SLICE(data));
    if (valid)
    {
        String8 destination=string_duplicate_arena(arena,output,true);
        valid=chmod((char*)destination.pointer,0644)==0;
    }
    if (valid)
    {
        *digest=observed;
        totals->files+=1; totals->bytes+=data.length;
        string8_list_push(arena,manifest,string_format(arena,S8("F\t{S8}\t{S8}\t{u64}\t420\n"),
            member,observed,data.length));
    }
    return valid;
}

// Frozen bounded copier paths are relative to one component; bind the
// retained inventory to its actual enclosing evidence/main40 location.
// The private lab keeps its original perf argument, but this diagnostic
// environment has no optional perf executable. Every admitted tool resolves
// to its real canonical executable; missing counters stay NA in stock lab data.
typedef struct CompilerMainFortyToolScope
{
    SliceString8 prior_values;
    PosixStringList prior_raw;
    bool active;
} CompilerMainFortyToolScope;

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_tool_path(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved,CompilerClosureUtilityExportTotals* totals,
    String8List* manifest,CompilerMainFortyToolScope* scope)
{
    String8 names[]={S8("sh"),S8("bash"),S8("env"),S8("git"),S8("clang"),S8("clang++"),
        S8("ld"),S8("ld.lld"),S8("ld.gold"),S8("lld"),S8("ninja"),S8("cmake"),S8("python3"),S8("tcc"),
        S8("taskset"),S8("uname"),S8("lscpu"),S8("true"),S8("cat"),S8("mkdir"),S8("chmod"),
        S8("cp"),S8("mv"),S8("rm"),S8("readlink"),S8("realpath"),S8("dirname"),S8("basename"),
        S8("sed"),S8("grep"),S8("cut"),S8("tr"),S8("cmp"),S8("ls"),S8("head"),S8("tail"),
        S8("stat"),S8("tee"),S8("sort"),S8("awk"),S8("date"),S8("wc"),S8("find"),S8("xargs"),
        S8("touch"),S8("sleep"),S8("make"),S8("gcc"),S8("cc"),S8("g++"),S8("c++"),S8("ar"),
        S8("ranlib"),S8("nm"),S8("objdump"),S8("readelf"),S8("as"),S8("ldd"),S8("sha256sum"),
        S8("du"),S8("pwd"),S8("ln"),S8("printf"),S8("install"),S8("getconf"),S8("nproc")};
    String8 required[]={S8("sh"),S8("bash"),S8("env"),S8("git"),S8("clang"),S8("ld"),S8("ninja"),
        S8("cmake"),S8("python3"),S8("taskset"),S8("uname"),S8("lscpu"),S8("true")};
    String8 path=path_join(arena,resolved.options.cleanup_root,S8("diagnostic-bin"));
    String8 canonical[BUSTER_ARRAY_LENGTH(names)]={0},hashes[BUSTER_ARRAY_LENGTH(names)]={0};
    OsDirectoryCreateResult created=os_make_directory_exclusive(path);
    bool valid=created.created && !created.error.v &&
        string_equal(path,os_path_absolute(arena,path,true)) && compiler_closure_admitting() &&
        !compiler_sampling_controller_cancelled();
    String8List rows={0};
    string8_list_push(arena,&rows,S8("BUSTER_MAIN_FORTY_DIAGNOSTIC_TOOLS_V1\nperf\tunavailable\t-\t-\n"));
    string8_list_push(arena,&rows,string_format(arena,S8("PATH\t{S8}\t-\t-\n"),path));
    for (u64 i=0;valid && i<BUSTER_ARRAY_LENGTH(names);i+=1)
    {
        String8 found=executable_resolve_in_path(arena,names[i]);
        bool mandatory=false;
        for (u64 j=0;j<BUSTER_ARRAY_LENGTH(required);j+=1)
            mandatory=mandatory || string_equal(names[i],required[j]);
        valid=!mandatory || found.length;
        if (valid && found.length)
        {
            canonical[i]=os_path_absolute(arena,found,true);
            valid=canonical[i].length && compiler_closure_utility_source_fixture_literal(canonical[i]) &&
                os_now_microseconds()<totals->deadline && compiler_closure_admitting() &&
                !compiler_sampling_controller_cancelled();
            for (u64 prior=0;valid && prior<i;prior+=1)
                if (string_equal(canonical[i],canonical[prior])) hashes[i]=hashes[prior];
            struct stat status={0};
            valid=valid && (hashes[i].length ||
                compiler_sampling_controller_hash(arena,canonical[i],&hashes[i],&status));
            String8 target=string_duplicate_arena(arena,canonical[i],true);
            String8 link=string_duplicate_arena(arena,path_join(arena,path,names[i]),true);
            valid=valid && symlink((char*)target.pointer,(char*)link.pointer)==0 &&
                string_equal(os_path_absolute(arena,link,true),canonical[i]);
        }
        if (valid) string8_list_push(arena,&rows,string_format(arena,S8("tool\t{S8}\t{S8}\t{S8}\n"),
            names[i],canonical[i].length ? canonical[i]:S8("-"),hashes[i].length ? hashes[i]:S8("-")));
    }
    String8 text=string_join_arena(arena,string8_list_to_slice(arena,rows),false);
    String8 proof=path_join(arena,resolved.options.cleanup_root,S8("diagnostic-tools.tsv")),digest={0};
    valid=valid && file_publish(proof,BUSTER_SLICE_TO_BYTE_SLICE(text)) &&
        compiler_main_forty_data(arena,proof,path_join(arena,resolved.options.evidence,S8("diagnostic-tools.tsv")),
            S8("diagnostic-tools.tsv"),totals,manifest,&digest);
    u64 path_index=0,path_count=0;
    for (u64 i=0;i<program_state->input.environment_keys.length;i+=1)
        if (string_equal(program_state->input.environment_keys.pointer[i],S8("PATH")))
        { path_index=i; path_count+=1; }
    valid=valid && path_count==1 &&
        program_state->input.environment_keys.length==program_state->input.environment_values.length;
    if (valid)
    {
        scope->prior_values=program_state->input.environment_values;
        scope->prior_raw=program_state->input.raw_environment;
        SliceString8 values={.pointer=arena_allocate(arena,String8,scope->prior_values.length),
            .length=scope->prior_values.length};
        for (u64 i=0;i<values.length;i+=1) values.pointer[i]=scope->prior_values.pointer[i];
        values.pointer[path_index]=path;
        program_state->input.environment_values=values;
        program_state->input.raw_environment=posix_environment_from_keys_and_values(arena,
            program_state->input.environment_keys,values);
        scope->active=true;
        valid=!executable_resolve_in_path(arena,S8("perf")).length &&
            string_equal(os_get_environment_variable(S8("PATH")),path);
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_counters_unavailable(Arena* arena,String8 root)
{
    String8 summary=compiler_sampling_controller_read(arena,path_join(arena,root,S8("lab/summary.json")),
        BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
    String8 pairs=compiler_sampling_controller_read(arena,path_join(arena,root,S8("lab/pairs.json")),
        BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
    String8 needle=S8("\"counters\": false");
    u64 found=0;
    for (u64 i=0;pairs.pointer && i+needle.length<=pairs.length;i+=1)
        if (string_equal(string_slice(pairs,i,i+needle.length),needle)) found+=1;
    bool valid=summary.pointer && pairs.pointer && found==80 &&
        string_contains(summary,S8("\"counters\": {\n  \"perf_stat\": false"));
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_manifest(Arena* arena,String8List component,
    String8List* manifest)
{
    SliceString8 rows=string8_list_to_slice(arena,component);
    bool valid=rows.length<=BUSTER_UTILITY_EXPORT_FILES;
    for (u64 i=0;valid && i<rows.length;i+=1)
    {
        String8 row=rows.pointer[i];
        valid=row.length>=3 && (row.pointer[0]=='F' || row.pointer[0]=='D') &&
            row.pointer[1]=='\t' && row.pointer[row.length-1]=='\n';
        if (valid)
        {
            String8 pieces[]={string_slice(row,0,2),S8("main40/"),string_slice(row,2,row.length)};
            string8_list_push(arena,manifest,string_join_arena(arena,
                (SliceString8)BUSTER_ARRAY_TO_SLICE(pieces),false));
        }
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_source(Arena* arena,
    CompilerClosureUtilityControllerResolved* resolved,CompilerClosureUtilityExportTotals* totals,
    String8List* manifest,String8* baseline_adapter,String8* candidate_adapter,String8* workload_digest)
{
    String8 root=resolved->admitted.plan.source_root,evidence=resolved->options.evidence;
    bool valid=compiler_closure_utility_source_fixture_initialize(arena,root,
        path_join(arena,resolved->options.cleanup_root,S8("initialize")))==PROCESS_RESULT_SUCCESS;
    String8 adapter=path_join(arena,root,S8("src/fixture-ide.c"));
    String8 workload=path_join(arena,root,S8("src/buster/apps/ide/ide.c"));
    if (valid)
    {
        make_directory_recursive(arena,path_parent(arena,workload));
        valid=production_profile_write(workload,S8("/* Tiny real compiler workload for hosted data-pipeline proof only. */\n#include <stdint.h>\n#include <stdio.h>\nstatic uint64_t mix(uint64_t value)\n{\n    value^=value>>30;\n    value*=UINT64_C(0xbf58476d1ce4e5b9);\n    value^=value>>27;\n    value*=UINT64_C(0x94d049bb133111eb);\n    return value^(value>>31);\n}\nint main(void)\n{\n    uint64_t value=UINT64_C(0x123456789abcdef0);\n    for (unsigned int i=0;i<128;i+=1) value=mix(value+i);\n    int result=printf(\"%llu\\n\",(unsigned long long)value)>0 ? 0:1;\n    return result;\n}\n")) &&
            compiler_main_forty_data(arena,workload,path_join(arena,evidence,S8("source-workload.c")),
                S8("source-workload.c"),totals,manifest,workload_digest);
    }
    for (u64 version=1; valid && version<=2; version+=1)
    {
        valid=compiler_closure_admitting() && !compiler_sampling_controller_cancelled() &&
            production_profile_write(adapter,compiler_main_forty_adapter(arena,version));
        String8 add[]={S8("git"),S8("-c"),S8("gc.auto=0"),S8("-c"),S8("maintenance.auto=false"),
            S8("-c"),S8("core.hooksPath=/dev/null"),S8("-C"),root,S8("add"),S8(".")};
        String8 commit[]={S8("git"),S8("-c"),S8("gc.auto=0"),S8("-c"),S8("maintenance.auto=false"),
            S8("-c"),S8("core.hooksPath=/dev/null"),S8("-C"),root,S8("-c"),
            S8("user.name=Private actual MAIN40 diagnostic"),S8("-c"),
            S8("user.email=main40-fixture@example.invalid"),S8("commit"),S8("--quiet"),S8("-m"),
            version==1 ? S8("DIAGNOSTIC-UNQUALIFIED actual compiler baseline") :
                S8("DIAGNOSTIC-UNQUALIFIED actual compiler candidate")};
        valid=valid && compiler_closure_capture(arena,(SliceString8)BUSTER_ARRAY_TO_SLICE(add)).success &&
            compiler_closure_capture(arena,(SliceString8)BUSTER_ARRAY_TO_SLICE(commit)).success;
        String8 name=version==1 ? S8("baseline-adapter.c") : S8("candidate-adapter.c");
        valid=valid && compiler_main_forty_data(arena,adapter,path_join(arena,evidence,name),name,
            totals,manifest,version==1 ? baseline_adapter:candidate_adapter);
    }
    ProductionProfileCommandResult head=valid ? compiler_closure_git(arena,root,S8("HEAD")) : (ProductionProfileCommandResult){0};
    ProductionProfileCommandResult base=valid ? compiler_closure_git(arena,root,S8("HEAD^1")) : (ProductionProfileCommandResult){0};
    ProductionProfileCommandResult head_tree=valid ? compiler_closure_git(arena,root,S8("HEAD^{tree}")) : (ProductionProfileCommandResult){0};
    ProductionProfileCommandResult base_tree=valid ? compiler_closure_git(arena,root,S8("HEAD^1^{tree}")) : (ProductionProfileCommandResult){0};
    CompilerClosureUtilityPlan* plan=&resolved->admitted.plan;
    plan->baseline_revision=production_profile_trim(base.output);
    plan->baseline_tree=production_profile_trim(base_tree.output);
    plan->candidate_revision=production_profile_trim(head.output);
    plan->candidate_tree=production_profile_trim(head_tree.output);
    plan->pull_head=plan->candidate_revision;
    valid=valid && head.success && base.success && head_tree.success && base_tree.success &&
        compiler_sampling_hex(plan->baseline_revision,40) && compiler_sampling_hex(plan->baseline_tree,40) &&
        compiler_sampling_hex(plan->candidate_revision,40) && compiler_sampling_hex(plan->candidate_tree,40);
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_forty_owned(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved,String8 path,SliceString8 command,u64 seconds)
{
    String8 limit=string_format(arena,S8("{u64}"),seconds);
    OsArgumentBuilder builder=os_argument_builder_start(arena);
    os_argument_builder_append(&builder,S8("owned-phase"));
    os_argument_builder_append(&builder,path);
    os_argument_builder_append(&builder,resolved.options.trusted_root);
    os_argument_builder_append(&builder,limit);
    os_argument_builder_append(&builder,resolved.admitted.plan.native_driver_sha256);
    os_argument_builder_append(&builder,resolved.bootstrap_marker_sha256);
    os_argument_builder_append(&builder,S8("--"));
    for (u64 i=0;i<command.length;i+=1) os_argument_builder_append(&builder,command.pointer[i]);
    return compiler_closure_owned_phase(arena,os_argument_builder_flush(&builder));
}

BUSTER_GLOBAL_LOCAL SliceString8 compiler_main_forty_compare(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    CompilerClosureUtilityPlan plan=resolved.admitted.plan;
    String8 common[]={resolved.python,S8("-B"),resolved.comparator,S8("--candidate"),plan.source_root,
        S8("--lab"),resolved.lab,S8("--work"),path_join(arena,plan.output_root,S8("main40-work")),
        S8("--evidence"),path_join(arena,plan.output_root,S8("main40-evidence")),
        S8("--summary"),path_join(arena,plan.output_root,S8("main40.md")),
        S8("--closure-policy"),S8("snapshot-v1"),S8("--main-owned-phases"),
        S8("--main-profile"),S8("compiler-main-40pairs-v1"),S8("--closure-driver"),resolved.driver,
        S8("--mode"),S8("main"),S8("--repository"),S8("buster14a/buster"),S8("--ref"),S8("refs/heads/main"),
        S8("--pull"),S8("1"),S8("--pull-head"),plan.pull_head,S8("--base"),plan.baseline_revision,
        S8("--base-tree"),plan.baseline_tree,S8("--head"),plan.candidate_revision,S8("--head-tree"),plan.candidate_tree,
        S8("--trusted-revision"),plan.trusted_revision,S8("--request-run-id"),S8("1"),
        S8("--run-id"),S8("1"),S8("--run-attempt"),S8("1")};
    OsArgumentBuilder builder=os_argument_builder_start(arena);
    for (u64 i=0;i<BUSTER_ARRAY_LENGTH(common);i+=1) os_argument_builder_append(&builder,common[i]);
    return os_argument_builder_flush(&builder);
}

BUSTER_GLOBAL_LOCAL bool compiler_main_forty_context(Arena* arena,SliceString8 arguments,
    CompilerClosureUtilityControllerResolved* result)
{
    bool worker=arguments.length==5 && string_equal(arguments.pointer[4],S8("--owned-main-forty-fixture-worker"));
    bool valid=(arguments.length==4 || worker) &&
        string_equal(arguments.pointer[0],S8("--self-test-main-forty-native-export")) &&
        compiler_closure_utility_fixture_allowed(arena);
    String8 selection={0};
    valid=valid && compiler_closure_utility_fixture_lab_case(&selection) && string_equal(selection,S8("none"));
    String8 master=valid ? os_path_absolute_lexical(arena,arguments.pointer[1],true) : (String8){0};
    CompilerClosureUtilityControllerResolved resolved={0};
    valid=valid && string_equal(master,arguments.pointer[1]) &&
        compiler_closure_utility_fixture_context(arena,master,&resolved);
    String8 python=valid ? os_path_absolute(arena,arguments.pointer[2],true) : (String8){0};
    String8 lab=valid ? os_path_absolute(arena,arguments.pointer[3],true) : (String8){0};
    valid=valid && string_equal(python,arguments.pointer[2]) && string_equal(python,resolved.python) &&
        string_equal(lab,arguments.pointer[3]) &&
        string_equal(lab,path_join(arena,resolved.options.trusted_root,S8("tools/uarch_lab.py")));
    struct stat status={0};
    if (valid)
    {
        resolved.lab=lab;
        valid=compiler_sampling_controller_hash(arena,lab,&resolved.admitted.plan.lab_sha256,&status) &&
            S_ISREG(status.st_mode) && status.st_size>0 && status.st_size<=BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT;
    }
    if (valid) *result=resolved;
    return valid;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_forty_worker(Arena* arena,
    CompilerClosureUtilityControllerResolved resolved)
{
    ProcessResult result=PROCESS_RESULT_FAILED;
    String8 master=resolved.options.cleanup_root,evidence=resolved.options.evidence;
    String8 claim=path_join(arena,master,S8("main40.claim"));
    String8 expected=S8("BUSTER_MAIN_FORTY_DIAGNOSTIC_ONCE_V1\n");
    bool valid=getpgrp()==getpid() && getpgrp()!=getsid(0) &&
        string_equal(compiler_sampling_controller_read(arena,claim,128),expected);
    OsDirectoryCreateResult worker=valid ? os_make_directory_exclusive(path_join(arena,master,S8("main40.worker.claim"))) :
        (OsDirectoryCreateResult){0};
    bool claimed=valid && worker.created && !worker.error.v;
    if (claimed)
    {
        u64 started=os_now_microseconds();
        CompilerClosureUtilityExportTotals totals={.deadline=started+BUSTER_MAIN_FORTY_FIXTURE_SECONDS*1000000ull,
            .diagnostic=true};
        String8List manifest={0};
        string8_list_push(arena,&manifest,S8("BUSTER_COMPILER_MAIN_FORTY_EXPORT_V1\n"));
        String8 baseline_adapter={0},candidate_adapter={0},workload_digest={0};
        bool initialized=compiler_main_forty_source(arena,&resolved,&totals,&manifest,
            &baseline_adapter,&candidate_adapter,&workload_digest);
        CompilerClosureUtilityPlan plan=resolved.admitted.plan;
        CompilerMainFortyToolScope tools={0};
        bool tool_path=initialized && compiler_main_forty_tool_path(arena,resolved,&totals,&manifest,&tools);
        String8 marker=tool_path ? string_format(arena,
            S8("{{\"schema\":\"buster-compiler-main-forty-fixture-v1\",\"diagnostic_fixture\":true,"
               "\"qualification_state\":\"unqualified\",\"physical_qualification\":false,"
               "\"main_profile\":\"compiler-main-40pairs-v1\",\"preparation_policy\":\"snapshot-v1\","
               "\"phase_schema\":\"buster-compiler-main-owned-phases-v1\","
               "\"actual_lab\":true,\"diagnostic_perf\":\"unavailable\",\"corpus_data\":\"fixed-diagnostic-full-original-profile\","
               "\"expected\":{{\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"head\":\"{S8}\",\"head_tree\":\"{S8}\","
               "\"pull_head\":\"{S8}\",\"trusted_revision\":\"{S8}\",\"root\":\"{S8}\",\"output\":\"{S8}\","
               "\"trusted_root\":\"{S8}\",\"trusted_lab\":\"{S8}\",\"trusted_lab_sha256\":\"{S8}\","
               "\"python\":\"{S8}\",\"python_sha256\":\"{S8}\",\"native_driver\":\"{S8}\","
               "\"native_driver_sha256\":\"{S8}\",\"bootstrap_marker_sha256\":\"{S8}\","
               "\"baseline_adapter_sha256\":\"{S8}\",\"candidate_adapter_sha256\":\"{S8}\","
               "\"workload_sha256\":\"{S8}\"}}}}\n"),
            plan.baseline_revision,plan.baseline_tree,plan.candidate_revision,plan.candidate_tree,plan.pull_head,
            plan.trusted_revision,plan.source_root,plan.output_root,plan.trusted_root,resolved.lab,plan.lab_sha256,
            resolved.python,plan.python_sha256,resolved.driver,plan.native_driver_sha256,resolved.bootstrap_marker_sha256,
            baseline_adapter,candidate_adapter,workload_digest) : (String8){0};
        bool marked=tool_path && file_publish(path_join(arena,evidence,S8("fixture-plan.json")),BUSTER_SLICE_TO_BYTE_SLICE(marker));
        bool output=marked && os_make_directory_exclusive(plan.output_root).created;
        SliceString8 command=compiler_main_forty_compare(arena,resolved);
        ProcessResult measured=output ? compiler_main_forty_owned(arena,resolved,
            path_join(arena,evidence,S8("manager.json")),command,420) : PROCESS_RESULT_FAILED;
        if (tools.active)
        {
            program_state->input.environment_values=tools.prior_values;
            program_state->input.raw_environment=tools.prior_raw;
            tools.active=false;
        }
        bool counters_unavailable=measured==PROCESS_RESULT_SUCCESS &&
            compiler_main_forty_counters_unavailable(arena,path_join(arena,plan.output_root,S8("main40-work")));
        String8 main=path_join(arena,evidence,S8("main40"));
        OsDirectoryCreateResult exported_root=marked ? os_make_directory_exclusive(main) : (OsDirectoryCreateResult){0};
        String8 sources[]={path_join(arena,plan.output_root,S8("main40-evidence")),
            path_join(arena,plan.output_root,S8("main40-work/lab")),
            path_join(arena,plan.output_root,S8("main40-work/throughput"))};
        String8 names[]={S8("ordinary"),S8("lab"),S8("throughput")};
        String8 exported_path=string_duplicate_arena(arena,main,true);
        struct stat exported_status={0};
        bool exported=exported_root.created && !exported_root.error.v &&
            lstat((char*)exported_path.pointer,&exported_status)==0 && S_ISDIR(exported_status.st_mode);
        if (exported) string8_list_push(arena,&manifest,string_format(arena,S8("D\tmain40\t-\t0\t{u64}\n"),
            (u64)(exported_status.st_mode & 07777)));
        for (u64 i=0;i<BUSTER_ARRAY_LENGTH(sources);i+=1)
        {
            String8List component={0};
            bool copied=exported_root.created && !exported_root.error.v &&
                compiler_closure_utility_controller_copy(arena,sources[i],path_join(arena,main,names[i]),
                    names[i],&totals,&component);
            bool inventoried=compiler_main_forty_manifest(arena,component,&manifest);
            exported=exported && copied && inventoried;
        }
        String8 references[]={path_join(arena,plan.output_root,S8("main40-work/lab/a/reference.exe")),
            path_join(arena,plan.output_root,S8("main40-work/lab/b/reference.exe"))};
        String8 reference_names[]={S8("baseline-reference.bin"),S8("candidate-reference.bin")};
        for (u64 i=0;i<BUSTER_ARRAY_LENGTH(references);i+=1)
        {
            String8 digest={0};
            bool copied=exported_root.created && !exported_root.error.v &&
                compiler_main_forty_data(arena,references[i],path_join(arena,main,reference_names[i]),
                    path_join(arena,S8("main40"),reference_names[i]),&totals,&manifest,&digest);
            exported=exported && copied;
        }
        String8 raw_manifest=string_join_arena(arena,string8_list_to_slice(arena,manifest),false);
        bool manifest_written=file_publish(path_join(arena,evidence,S8("export.manifest.tsv")),
            BUSTER_SLICE_TO_BYTE_SLICE(raw_manifest));
        result=measured==PROCESS_RESULT_SUCCESS && counters_unavailable && exported && manifest_written &&
            generate_path_kind(arena,path_join(arena,plan.output_root,S8("main40-evidence/cleanup-uncertain")))==GENERATE_PATH_MISSING &&
            !compiler_closure_utility_controller_unknown(arena,resolved) && compiler_closure_admitting() &&
            !compiler_sampling_controller_cancelled() && os_now_microseconds()<totals.deadline ?
                PROCESS_RESULT_SUCCESS:PROCESS_RESULT_FAILED;
        string_print(S8("COMPILER_MAIN_FORTY_DIAGNOSTIC actual_lab=1 profile=compiler-main-40pairs-v1 "
            "owned_schema=buster-compiler-main-owned-phases-v1 initialized={u64} measured={u64} "
            "exported={u64} manifest={u64} files={u64} bytes={u64} success={u64} "
            "diagnostic_perf=unavailable counters_na={u64} physical_qualification=false\n"),
            (u64)initialized,(u64)(measured==PROCESS_RESULT_SUCCESS),(u64)exported,(u64)manifest_written,
            totals.files,totals.bytes,(u64)(result==PROCESS_RESULT_SUCCESS),(u64)counters_unavailable);
    }
    return result;
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_main_forty_fixture_execute(Arena* arena,SliceString8 arguments)
{
    ProcessResult result=PROCESS_RESULT_FAILED;
#if BUSTER_LINUX && !BUSTER_ANDROID
    CompilerClosureUtilityControllerResolved resolved={0};
    bool valid=compiler_main_forty_context(arena,arguments,&resolved);
    bool worker=valid && arguments.length==5;
    if (worker) result=compiler_main_forty_worker(arena,resolved);
    else if (valid && generate_path_kind(arena,resolved.options.cleanup_root)==GENERATE_PATH_MISSING)
    {
        String8 master=resolved.options.cleanup_root,evidence=resolved.options.evidence;
        OsDirectoryCreateResult made=os_make_directory_exclusive(master);
        OsDirectoryCreateResult storage=made.created && !made.error.v ? os_make_directory_exclusive(evidence) :
            (OsDirectoryCreateResult){0};
        String8 claim=S8("BUSTER_MAIN_FORTY_DIAGNOSTIC_ONCE_V1\n");
        bool claimed=storage.created && !storage.error.v &&
            file_publish(path_join(arena,master,S8("main40.claim")),BUSTER_SLICE_TO_BYTE_SLICE(claim));
        CompilerSamplingSignalScope signals={0};
        bool deferred=claimed && compiler_closure_utility_controller_signals_begin(&signals);
        String8 command[]={resolved.driver,S8("compiler_profile_qualification"),arguments.pointer[0],
            arguments.pointer[1],arguments.pointer[2],arguments.pointer[3],S8("--owned-main-forty-fixture-worker")};
        ProcessResult executed=deferred ? compiler_main_forty_owned(arena,resolved,
            path_join(arena,evidence,S8("entry.json")),(SliceString8)BUSTER_ARRAY_TO_SLICE(command),
            BUSTER_MAIN_FORTY_FIXTURE_SECONDS) : PROCESS_RESULT_FAILED;
        bool restored=deferred && compiler_closure_utility_controller_signals_end(&signals);
        result=executed==PROCESS_RESULT_SUCCESS && restored && compiler_closure_admitting() &&
            !compiler_sampling_controller_cancelled() ? PROCESS_RESULT_SUCCESS:PROCESS_RESULT_FAILED;
        String8 stdout_path=string_format(arena,S8("{S8}.stdout"),path_join(arena,evidence,S8("entry.json")));
        String8 observed=compiler_sampling_controller_read(arena,stdout_path,BUSTER_SAMPLING_CONTROLLER_METADATA_LIMIT);
        if (observed.length) string_print(S8("{S8}"),observed);
        string_print(S8("COMPILER_MAIN_FORTY_ENTRY_DIAGNOSTIC claimed={u64} restored={u64} success={u64} "
            "physical_qualification=false\n"),(u64)claimed,(u64)restored,(u64)(result==PROCESS_RESULT_SUCCESS));
    }
#else
    BUSTER_UNUSED(arena); BUSTER_UNUSED(arguments);
#endif
    return result;
}
