// Fixed native preparation and closure qualification, included after compiler_closure.c.
// Entry points: compiler_closure_prepare_main (prepare) and
// compiler_closure_qualification_main (qualify). The trusted outer runner owns
// process-tree cancellation and the existing 90-minute host cap.
// Every arm starts from the same private checkout with source/build/cache reset.
// Route the single preparation command adapter through the shared native phase
// containment helper before enabling qualification; historical lab launch
// behavior remains unchanged. All failed phases and attempts are retained.
#if BUSTER_LINUX
#define COMPILER_CLOSURE_QUALIFICATION_PROFILE "compiler-baseline-closure-qualification-v1"
#define COMPILER_CLOSURE_QUALIFICATION_STAGE_LIMIT 96ull
#define COMPILER_CLOSURE_QUALIFICATION_LOG_LIMIT (1ull << 20)
#define COMPILER_CLOSURE_QUALIFICATION_START_LIMIT_US (85ull * 60 * 1000000)

typedef struct CompilerClosurePreparation CompilerClosurePreparation;
struct CompilerClosurePreparation
{
    Arena* arena;
    String8 root, output, policy, base, base_tree, head, head_tree;
    String8 baseline, candidate, baseline_sha256, candidate_sha256, harness_sha256;
    String8 frozen_manifest, snapshot_digest, frozen_workload_sha256;
    String8 bootstrap_configuration, bootstrap_artifact_sha256;
    u64 baseline_bytes, candidate_bytes, harness_bytes;
    String8List ledger;
    String8 phase;
    u64 stage, started_us, phase_started_us;
    bool owned;
    bool success;
};

BUSTER_GLOBAL_LOCAL bool compiler_closure_commit_valid(String8 text)
{
    bool result = text.length == 40;
    for (u64 index = 0; result && index < text.length; index += 1)
    {
        char8 digit = text.pointer[index];
        result = (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_flush(CompilerClosurePreparation* preparation)
{
    TemporalArena temporary = scratch_begin(&preparation->arena, 1);
    String8 text = string_join_arena(temporary.arena, string8_list_to_slice(temporary.arena, preparation->ledger), false);
    bool result = text.length <= BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT &&
        file_publish(path_join(temporary.arena, preparation->output, S8("phases.tsv")), BUSTER_SLICE_TO_BYTE_SLICE(text));
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_begin(CompilerClosurePreparation* preparation, String8 phase)
{
    preparation->success = preparation->success && preparation->stage < COMPILER_CLOSURE_QUALIFICATION_STAGE_LIMIT &&
        os_now_microseconds() - preparation->started_us < COMPILER_CLOSURE_QUALIFICATION_START_LIMIT_US;
    if (preparation->success)
    {
        preparation->stage += 1;
        preparation->phase = phase;
        preparation->phase_started_us = os_now_microseconds();
        string8_list_push(preparation->arena, &preparation->ledger,
            string_format(preparation->arena, S8("start\t{u64}\t{S8}\t{u64}\n"), preparation->stage, phase, preparation->phase_started_us));
        preparation->success = compiler_closure_preparation_flush(preparation);
    }
    return preparation->success;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_end(CompilerClosurePreparation* preparation, bool success, u64 native_status)
{
    u64 finished = os_now_microseconds();
    string8_list_push(preparation->arena, &preparation->ledger,
        string_format(preparation->arena, S8("finish\t{u64}\t{S8}\t{u64}\t{u64}\t{S8}\t{u64}\n"),
            preparation->stage, preparation->phase, finished, finished - preparation->phase_started_us,
            success ? S8("complete") : S8("failed"), native_status));
    bool written = compiler_closure_preparation_flush(preparation);
    preparation->success = preparation->success && success && written;
    return preparation->success;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_command(CompilerClosurePreparation* preparation, String8 phase,
    SliceString8 arguments, bool lab)
{
    bool result = compiler_closure_preparation_begin(preparation, phase);
    if (result)
    {
        TemporalArena temporary = scratch_begin(&preparation->arena, 1);
        String8 stem = string_format(temporary.arena, S8("{u64}-{S8}"), preparation->stage, phase);
        String8 command_path = path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("{S8}.argv"), stem));
        result = file_publish(command_path, BUSTER_SLICE_TO_BYTE_SLICE(production_profile_argv_text(temporary.arena, arguments)));
        BUSTER_UNUSED(lab);
        u64 elapsed = os_now_microseconds() - preparation->started_us;
        u64 deadline = 88ull * 60 * 1000000;
        CompilerClosurePhaseResult phase_result = {.wait = {.result = PROCESS_RESULT_FAILED}};
        if (result && elapsed < deadline)
        {
            phase_result = compiler_closure_phase_run(temporary.arena, arguments, preparation->root, deadline - elapsed, true);
        }
        ProcessWaitResult wait = phase_result.wait;
        String8 cleanup = string_format(temporary.arena, S8("{{\"schema\":\"buster-native-qualification-supervisor-v1\","
            "\"cleanup_proven\":{S8},\"duration_us\":{u64},\"waves\":{u64},\"signalled\":{u64},\"reaped\":{u64},"
            "\"timed_out\":{u64},\"cancelled\":{u64},\"reservation_retained\":{u64},\"ownership_lost\":{u64}\n}\n"),
            phase_result.cleanup_proven ? S8("true") : S8("false"), phase_result.cleanup_us,
            phase_result.waves, phase_result.signalled, phase_result.reaped, (u64)wait.timed_out,
            process_control_atomic_load(&compiler_closure_cancel_signal) ? 1ull : 0ull,
            (u64)wait.process_group_reservation_retained, (u64)wait.process_group_ownership_lost);
        bool cleanup_written = file_publish(path_join(temporary.arena, preparation->output,
            string_format(temporary.arena, S8("{S8}.cleanup.json"), stem)), BUSTER_SLICE_TO_BYTE_SLICE(cleanup));
        result = result && phase_result.success && cleanup_written;
        bool logs = file_publish(path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("{S8}.stdout"), stem)),
            wait.streams[STANDARD_STREAM_OUTPUT]);
        logs = file_publish(path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("{S8}.stderr"), stem)),
            wait.streams[STANDARD_STREAM_ERROR]) && logs;
        result = compiler_closure_preparation_end(preparation, result && logs, wait.platform_status);
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_fields(String8 line, String8 fields[8], u64* count)
{
    bool result = true;
    u64 start = 0;
    *count = 0;
    for (u64 index = 0; result && index <= line.length; index += 1)
    {
        if (index == line.length || line.pointer[index] == '\t')
        {
            result = *count < 8;
            if (result)
            {
                fields[*count] = (String8){.pointer = line.pointer + start, .length = index - start};
                *count += 1;
                start = index + 1;
            }
        }
    }
    return result;
}

// Keep all source/resource/tool identities and configured bindings. Relevant
// generated headers, include trees, CMake/compiler graph inputs and the exact
// corpus executable are included. File mtimes, build artifacts and historical
// bootstrap paths are excluded from this comparison only; raw manifests stay
// untouched. The selected baseline producer is independently bound by digest.
BUSTER_GLOBAL_LOCAL String8 compiler_closure_preparation_workload(Arena* arena, String8 manifest,
    String8 bootstrap_configuration, String8 bootstrap_artifact_sha256)
{
    String8List lines = {0};
    String8 remaining = manifest;
    String8 line = {0};
    bool success = manifest.length && stage_object_sha256_valid(bootstrap_configuration) &&
        stage_object_sha256_valid(bootstrap_artifact_sha256);
    while (success && text_next_line(&remaining, &line))
    {
        String8 fields[8] = {0};
        u64 count = 0;
        success = compiler_closure_preparation_fields(line, fields, &count);
        if (success && count == 8)
        {
            bool source = string_equal(fields[0], S8("source")) || string_equal(fields[0], S8("resource")) ||
                string_equal(fields[0], S8("tool"));
            bool build = string_equal(fields[0], S8("build"));
            bool generated = build && (string_equal(fields[7], S8("generated")) ||
                string_starts_with_sequence(fields[7], S8("generated/")) || string_equal(fields[7], S8("include")) ||
                string_starts_with_sequence(fields[7], S8("include/")) ||
                string_equal(fields[7], S8("CMakeCache.txt")) || string_equal(fields[7], S8("compile_commands.json")) ||
                string_equal(fields[7], S8("throughput-tools/throughput")) ||
                string_ends_with_sequence(fields[7], S8(".h")) || string_ends_with_sequence(fields[7], S8(".inc")) ||
                string_ends_with_sequence(fields[7], S8(".c")) || string_ends_with_sequence(fields[7], S8(".cmake")) ||
                string_ends_with_sequence(fields[7], S8(".ninja")) || string_ends_with_sequence(fields[7], S8(".rsp")));
            success = source || build || string_equal(fields[0], S8("bootstrap"));
            if (success && (source || generated))
            {
                string8_list_push(arena, &lines, string_format(arena, S8("{S8}\t{S8}\t{S8}\t{S8}\t{S8}\t{S8}\n"),
                    fields[0], fields[1], fields[2], fields[5], fields[6], fields[7]));
            }
        }
        else if (success && count == 3 && string_equal(fields[0], S8("binding")))
        {
            bool random_path = string_equal(fields[1], S8("bootstrap_marker")) || string_equal(fields[1], S8("bootstrap_artifact"));
            if (!random_path) { string8_list_push(arena, &lines, string_format(arena, S8("{S8}\n"), line)); }
        }
        else if (success && ((count == 1 && string_equal(fields[0], S8("BUSTER_COMPILER_CLOSURE_V1"))) ||
            (count == 2 && (string_equal(fields[0], S8("root")) || string_equal(fields[0], S8("base")) ||
                string_equal(fields[0], S8("tree"))))))
        {
            string8_list_push(arena, &lines, string_format(arena, S8("{S8}\n"), line));
        }
        else if (success) { success = count == 3 && string_equal(fields[0], S8("END")); }
    }
    String8 result = {0};
    if (success)
    {
        string8_list_push(arena, &lines, string_format(arena, S8("producer\tconfiguration\t{S8}\nproducer\tartifact_sha256\t{S8}\n"),
            bootstrap_configuration, bootstrap_artifact_sha256));
        String8 text = string_join_arena(arena, string8_list_to_slice(arena, lines), false);
        if (text.length <= BUSTER_COMPILER_CLOSURE_MANIFEST_LIMIT) { result = text; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_initialize(CompilerClosurePreparation* preparation, Arena* arena,
    String8 root_argument, String8 output_argument, String8 policy, String8 base, String8 base_tree, String8 head, String8 head_tree)
{
    String8 root = os_path_absolute(arena, root_argument, true);
    String8 output = os_path_absolute_lexical(arena, output_argument, true);
    String8 parent = path_parent(arena, output);
    String8 real_parent = os_path_absolute(arena, parent, true);
    bool result = root.length && output.length && real_parent.length && string_equal(parent, real_parent) &&
        compiler_closure_path_safe(root) && compiler_closure_path_safe(output) &&
        production_profile_path_components_safe(output) && !string_equal(root, output) &&
        !production_profile_path_is_child(root, output) && !production_profile_path_is_child(output, root) &&
        compiler_closure_admitting() && compiler_closure_commit_valid(base) && compiler_closure_commit_valid(base_tree) &&
        compiler_closure_commit_valid(head) && compiler_closure_commit_valid(head_tree) &&
        (string_equal(policy, S8("legacy-rebuild")) || string_equal(policy, S8("snapshot-v1")));
    if (result)
    {
        OsDirectoryCreateResult directory = os_make_directory(output);
        result = directory.created && !directory.error.v;
    }
    *preparation = (CompilerClosurePreparation){.arena = arena, .root = root, .output = output, .policy = policy,
        .base = base, .base_tree = base_tree, .head = head, .head_tree = head_tree,
        .owned = result, .success = result, .started_us = os_now_microseconds()};
    if (result)
    {
        string8_list_push(arena, &preparation->ledger, string_format(arena,
            S8("BUSTER_COMPILER_PREPARATION_LEDGER_V1\nroot\t{S8}\npolicy\t{S8}\nbase\t{S8}\nbase_tree\t{S8}\nhead\t{S8}\nhead_tree\t{S8}\n"),
            root, policy, base, base_tree, head, head_tree));
        result = compiler_closure_preparation_begin(preparation, S8("pins"));
    }
    if (result)
    {
        TemporalArena temporary = scratch_begin(&arena, 1);
        ProductionProfileCommandResult revision = compiler_closure_git(temporary.arena, root, S8("HEAD"));
        ProductionProfileCommandResult current_tree = compiler_closure_git(temporary.arena, root, S8("HEAD^{tree}"));
        ProductionProfileCommandResult baseline_tree = compiler_closure_git(temporary.arena, root,
            string_format(temporary.arena, S8("{S8}^{{tree}}"), base));
        String8 ancestor[] = {S8("git"), S8("-C"), root, S8("merge-base"), S8("--is-ancestor"), base, head};
        String8 clean[] = {S8("git"), S8("-C"), root, S8("diff"), S8("--quiet"), S8("--exit-code"), S8("HEAD"), S8("--")};
        ProductionProfileCommandResult ancestry = compiler_closure_capture(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(ancestor));
        ProductionProfileCommandResult clean_result = compiler_closure_capture(temporary.arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(clean));
        result = revision.success && current_tree.success && baseline_tree.success && ancestry.success && clean_result.success &&
            string_equal(production_profile_trim(revision.output), head) &&
            string_equal(production_profile_trim(current_tree.output), head_tree) &&
            string_equal(production_profile_trim(baseline_tree.output), base_tree);
        result = compiler_closure_preparation_end(preparation, result, 0);
        scratch_end(temporary);
    }
    if (result)
    {
        OsDirectoryCreateResult binaries = os_make_directory(path_join(arena, output, S8("bin")));
        result = binaries.created && !binaries.error.v;
        preparation->success = preparation->success && result;
    }
    return preparation->success;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_checkout(CompilerClosurePreparation* preparation, String8 phase, String8 commit)
{
    String8 arguments[] = {S8("git"), S8("-C"), preparation->root, S8("checkout"), S8("--quiet"), S8("--detach"), commit};
    bool result = compiler_closure_preparation_command(preparation, phase, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_reset(CompilerClosurePreparation* preparation)
{
    String8 reset[] = {S8("git"), S8("-C"), preparation->root, S8("reset"), S8("--hard"), S8("--quiet"), preparation->base};
    String8 clean[] = {S8("git"), S8("-C"), preparation->root, S8("clean"), S8("-fdx")};
    bool result = compiler_closure_preparation_checkout(preparation, S8("reset-checkout"), preparation->base) &&
        compiler_closure_preparation_command(preparation, S8("reset-tracked-source"), (SliceString8)BUSTER_ARRAY_TO_SLICE(reset), false) &&
        compiler_closure_preparation_command(preparation, S8("reset-build-cache"), (SliceString8)BUSTER_ARRAY_TO_SLICE(clean), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_build(CompilerClosurePreparation* preparation, String8 role, String8 commit)
{
    TemporalArena temporary = scratch_begin(&preparation->arena, 1);
    String8 wrapper = path_join(temporary.arena, preparation->root, S8("build.sh"));
    String8 generate[] = {wrapper, S8("generate"), S8("--cc"), S8("clang"), S8("--no-include-tests")};
    String8 build[] = {wrapper, S8("build"), S8("--config"), S8("Release"), S8("-t"), S8("ide")};
    bool result = compiler_closure_preparation_checkout(preparation,
            string_format(temporary.arena, S8("{S8}-checkout"), role), commit) &&
        compiler_closure_preparation_command(preparation, string_format(temporary.arena, S8("{S8}-generate"), role),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(generate), false) &&
        compiler_closure_preparation_command(preparation, string_format(temporary.arena, S8("{S8}-build"), role),
            (SliceString8)BUSTER_ARRAY_TO_SLICE(build), false) &&
        compiler_closure_preparation_begin(preparation, string_format(temporary.arena, S8("{S8}-build-verify"), role));
    if (result)
    {
        result = compiler_closure_preparation_end(preparation, compiler_closure_cache_valid(temporary.arena, preparation->root), 0);
    }
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_freeze(CompilerClosurePreparation* preparation, bool baseline)
{
    String8 role = baseline ? S8("baseline") : S8("candidate");
    bool result = compiler_closure_preparation_begin(preparation,
        string_format(preparation->arena, S8("{S8}-freeze"), role));
    if (result)
    {
        TemporalArena temporary = scratch_begin(&preparation->arena, 1);
        String8 source = path_join(temporary.arena, preparation->root, S8("build/Release/ide"));
        String8 destination = path_join(preparation->arena, preparation->output, baseline ? S8("bin/ide-base") : S8("bin/ide-cand"));
        String8 source_hash = {0};
        String8 destination_hash = {0};
        struct stat source_status = {0};
        struct stat destination_status = {0};
        result = compiler_closure_hash(temporary.arena, source, &source_hash, &source_status) &&
            source_status.st_size > 0 && (source_status.st_mode & 0111) &&
            file_copy((CopyFileArguments){.original_path = source, .new_path = destination}) &&
            chmod((char*)destination.pointer, source_status.st_mode & 07777) == 0 &&
            compiler_closure_hash(temporary.arena, destination, &destination_hash, &destination_status) &&
            string_equal(source_hash, destination_hash) && source_status.st_size == destination_status.st_size &&
            (source_status.st_mode & 07777) == (destination_status.st_mode & 07777);
        if (result)
        {
            String8 digest = string_duplicate_arena(preparation->arena, destination_hash, true);
            if (baseline)
            {
                preparation->baseline = destination;
                preparation->baseline_sha256 = digest;
                preparation->baseline_bytes = (u64)destination_status.st_size;
            }
            else
            {
                preparation->candidate = destination;
                preparation->candidate_sha256 = digest;
                preparation->candidate_bytes = (u64)destination_status.st_size;
            }
            result = file_copy((CopyFileArguments){
                .original_path = path_join(temporary.arena, preparation->root, S8("build/CMakeCache.txt")),
                .new_path = path_join(temporary.arena, preparation->output,
                    baseline ? S8("baseline.CMakeCache.txt") : S8("candidate.CMakeCache.txt"))});
        }
        result = compiler_closure_preparation_end(preparation, result, 0);
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_transfer(CompilerClosurePreparation* preparation, String8 operation)
{
    bool result = compiler_closure_preparation_begin(preparation,
        string_format(preparation->arena, S8("closure-{S8}"), operation));
    if (result)
    {
        TemporalArena temporary = scratch_begin(&preparation->arena, 1);
        String8 snapshot = path_join(temporary.arena, preparation->output, S8("frozen-baseline"));
        String8 receipt = path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("closure-{S8}.json"), operation));
        result = compiler_closure_transfer(temporary.arena, operation, preparation->root, snapshot, preparation->base,
            preparation->base_tree, receipt, preparation->snapshot_digest.length ? preparation->snapshot_digest : S8("-"));
        if (result && string_equal(operation, S8("snapshot")))
        {
            String8 digest = compiler_closure_read(temporary.arena, path_join(temporary.arena, snapshot, S8(".complete")), SHA256_HEX_CAPACITY - 1);
            result = stage_object_sha256_valid(digest);
            if (result) { preparation->snapshot_digest = string_duplicate_arena(preparation->arena, digest, true); }
        }
        result = compiler_closure_preparation_end(preparation, result, 0);
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_inventory(CompilerClosurePreparation* preparation, String8 phase, bool capture)
{
    bool result = compiler_closure_preparation_begin(preparation, phase);
    if (result)
    {
        TemporalArena temporary = scratch_begin(&preparation->arena, 1);
        String8 cache = path_join(temporary.arena, preparation->root, S8(".cache/bootstrap-driver"));
        String8 manifest = compiler_closure_inventory(temporary.arena, preparation->root, preparation->root,
            path_join(temporary.arena, preparation->root, S8("build")), cache, preparation->base, preparation->base_tree);
        CompilerClosureBootstrapIdentity producer = {0};
        result = manifest.length && compiler_closure_bootstrap_identity(temporary.arena,
            preparation->root, preparation->root, cache, &producer);
        String8 workload = result ? compiler_closure_preparation_workload(temporary.arena, manifest,
            producer.configuration, producer.artifact_sha256) : (String8){0};
        String8 workload_digest = workload.length ? production_profile_sha256_text(temporary.arena, workload) : (String8){0};
        result = result && stage_object_sha256_valid(workload_digest);
        if (result && capture)
        {
            preparation->frozen_manifest = string_duplicate_arena(preparation->arena, manifest, true);
            preparation->frozen_workload_sha256 = string_duplicate_arena(preparation->arena, workload_digest, true);
            preparation->bootstrap_configuration = string_duplicate_arena(preparation->arena, producer.configuration, true);
            preparation->bootstrap_artifact_sha256 = string_duplicate_arena(preparation->arena, producer.artifact_sha256, true);
        }
        else if (result)
        {
            result = string_equal(preparation->frozen_manifest, manifest) &&
                string_equal(preparation->frozen_workload_sha256, workload_digest) &&
                string_equal(preparation->bootstrap_configuration, producer.configuration) &&
                string_equal(preparation->bootstrap_artifact_sha256, producer.artifact_sha256);
        }
        bool raw_written = file_publish(path_join(temporary.arena, preparation->output,
            string_format(temporary.arena, S8("{S8}.manifest.tsv"), phase)), BUSTER_SLICE_TO_BYTE_SLICE(manifest));
        bool normalized_written = file_publish(path_join(temporary.arena, preparation->output,
            string_format(temporary.arena, S8("{S8}.workload.tsv"), phase)), BUSTER_SLICE_TO_BYTE_SLICE(workload));
        result = compiler_closure_preparation_end(preparation, result && raw_written && normalized_written, 0);
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_execute(CompilerClosurePreparation* preparation)
{
    bool snapshot = string_equal(preparation->policy, S8("snapshot-v1"));
    bool result = compiler_closure_preparation_reset(preparation) &&
        compiler_closure_preparation_build(preparation, S8("baseline"), preparation->base) &&
        compiler_closure_preparation_freeze(preparation, true);
    if (result && snapshot) { result = compiler_closure_preparation_transfer(preparation, S8("snapshot")); }
    result = result && compiler_closure_preparation_build(preparation, S8("candidate"), preparation->head) &&
        compiler_closure_preparation_freeze(preparation, false);
    if (result && snapshot)
    {
        result = compiler_closure_preparation_checkout(preparation, S8("baseline-restore-checkout"), preparation->base) &&
            compiler_closure_preparation_transfer(preparation, S8("restore")) &&
            compiler_closure_preparation_transfer(preparation, S8("verify"));
    }
    else if (result)
    {
        String8 corpus_prepare[] = {path_join(preparation->arena, preparation->root, S8("build.sh")), S8("bench_throughput"), S8("help")};
        result = compiler_closure_preparation_build(preparation, S8("baseline-closure"), preparation->base) &&
            compiler_closure_preparation_command(preparation, S8("baseline-corpus-prepare"),
                (SliceString8)BUSTER_ARRAY_TO_SLICE(corpus_prepare), false);
    }
    if (result) { result = compiler_closure_preparation_begin(preparation, S8("baseline-corpus-verify")); }
    if (result)
    {
        String8 digest = {0};
        struct stat status = {0};
        result = compiler_closure_hash(preparation->arena,
            path_join(preparation->arena, preparation->root, S8("build/throughput-tools/throughput")), &digest, &status) &&
            status.st_size > 0 && (status.st_mode & 0111);
        if (result)
        {
            preparation->harness_sha256 = digest;
            preparation->harness_bytes = (u64)status.st_size;
        }
        result = compiler_closure_preparation_end(preparation, result, 0);
    }
    result = result && compiler_closure_preparation_inventory(preparation, S8("prepared"), true);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_preparation_write(CompilerClosurePreparation* preparation)
{
    bool result = preparation->owned;
    if (result)
    {
        TemporalArena temporary = scratch_begin(&preparation->arena, 1);
        String8 ledger = string_join_arena(temporary.arena, string8_list_to_slice(temporary.arena, preparation->ledger), false);
        bool complete = preparation->success && stage_object_sha256_valid(preparation->baseline_sha256) &&
            stage_object_sha256_valid(preparation->candidate_sha256) && stage_object_sha256_valid(preparation->harness_sha256) &&
            stage_object_sha256_valid(preparation->frozen_workload_sha256) &&
            stage_object_sha256_valid(preparation->bootstrap_configuration) &&
            stage_object_sha256_valid(preparation->bootstrap_artifact_sha256) && preparation->frozen_manifest.length &&
            preparation->baseline_bytes && preparation->candidate_bytes && preparation->harness_bytes;
        String8 text = string_format(temporary.arena, S8("{{\"schema\":\"buster-compiler-preparation-v1\","
            "\"state\":\"{S8}\",\"policy\":\"{S8}\",\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"head\":\"{S8}\",\"head_tree\":\"{S8}\","
            "\"root_sha256\":\"{S8}\",\"prepared_manifest_sha256\":\"{S8}\",\"frozen_workload_sha256\":\"{S8}\","
            "\"ledger_sha256\":\"{S8}\",\"baseline_sha256\":\"{S8}\",\"candidate_sha256\":\"{S8}\",\"harness_sha256\":\"{S8}\","
            "\"baseline_bytes\":{u64},\"candidate_bytes\":{u64},\"harness_bytes\":{u64},\"snapshot_digest\":\"{S8}\","
            "\"bootstrap_configuration\":\"{S8}\",\"bootstrap_artifact_sha256\":\"{S8}\",\"stage_count\":{u64},"
            "\"ownership_schema\":\"buster-native-qualification-supervisor-v1\",\"cleanup_proven\":{S8},\"duration_us\":{u64}\n}\n"),
            complete ? S8("complete") : S8("failed"), preparation->policy, preparation->base, preparation->base_tree,
            preparation->head, preparation->head_tree, production_profile_sha256_text(temporary.arena, preparation->root),
            production_profile_sha256_text(temporary.arena, preparation->frozen_manifest), preparation->frozen_workload_sha256,
            production_profile_sha256_text(temporary.arena, ledger), preparation->baseline_sha256, preparation->candidate_sha256,
            preparation->harness_sha256, preparation->baseline_bytes, preparation->candidate_bytes, preparation->harness_bytes,
            preparation->snapshot_digest, preparation->bootstrap_configuration, preparation->bootstrap_artifact_sha256,
            preparation->stage, !compiler_closure_cleanup_failed ? S8("true") : S8("false"), os_now_microseconds() - preparation->started_us);
        result = compiler_closure_preparation_flush(preparation) &&
            file_publish(path_join(temporary.arena, preparation->output, S8("prepared.json")), BUSTER_SLICE_TO_BYTE_SLICE(text));
        preparation->success = preparation->success && complete && result;
        scratch_end(temporary);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_qualification_pair(CompilerClosurePreparation* preparation,
    String8 name, String8 lab, String8 python, String8 baseline, String8 candidate, bool same_source)
{
    TemporalArena temporary = scratch_begin(&preparation->arena, 1);
    String8 lab_output = path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("{S8}-lab"), name));
    String8 corpus_output = path_join(temporary.arena, preparation->output, string_format(temporary.arena, S8("{S8}-throughput"), name));
    OsDirectoryCreateResult directory = preparation->success ? os_make_directory(lab_output) : (OsDirectoryCreateResult){0};
    bool result = preparation->success && directory.created && !directory.error.v && !path_exists(temporary.arena, corpus_output);
    preparation->success = preparation->success && result;
    String8 lab_arguments[] = {python, S8("-B"), lab, S8("compare"), S8("--baseline"), baseline, S8("--candidate"), candidate,
        S8("--repo-root"), preparation->root, S8("--cpu"), S8("2"), S8("--output"), lab_output,
        S8("--target-minutes"), S8("10"), S8("--warmups"), S8("1"), S8("--require-identical-output")};
    SliceString8 lab_slice = BUSTER_ARRAY_TO_SLICE(lab_arguments);
    if (!same_source) { lab_slice.length -= 1; }
    result = result && compiler_closure_preparation_command(preparation,
        string_format(temporary.arena, S8("{S8}-lab"), name), lab_slice, true);
    String8 corpus_arguments[] = {path_join(temporary.arena, preparation->root, S8("build/throughput-tools/throughput")), S8("run"),
        S8("--baseline"), baseline, S8("--candidate"), candidate, S8("--output"), corpus_output,
        S8("--baseline-id"), preparation->base, S8("--candidate-id"), same_source ? preparation->base : preparation->head,
        S8("--profile"), S8("ci"), S8("--mode"), S8("all"), S8("--pairs"), S8("20"), S8("--warmups"), S8("2"),
        S8("--timeout"), S8("120"), S8("--cpu"), S8("2"), S8("--require-identical-output")};
    SliceString8 corpus_slice = BUSTER_ARRAY_TO_SLICE(corpus_arguments);
    if (!same_source) { corpus_slice.length -= 1; }
    result = result && compiler_closure_preparation_command(preparation,
        string_format(temporary.arena, S8("{S8}-throughput"), name), corpus_slice, false) &&
        compiler_closure_preparation_inventory(preparation,
            string_format(temporary.arena, S8("{S8}-post"), name), false);
    scratch_end(temporary);
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_qualification_host(Arena* arena, String8 output)
{
    int descriptor = open("/proc/cpuinfo", O_RDONLY | O_NOFOLLOW);
    u64 limit = 64 * 1024;
    char8* bytes = arena_allocate(arena, char8, limit + 1);
    u64 used = 0;
    bool result = descriptor >= 0;
    bool eof = false;
    while (result && !eof && used < limit)
    {
        ssize_t count = read(descriptor, bytes + used, (size_t)(limit - used));
        if (count > 0) { used += (u64)count; }
        else if (count == 0) { eof = true; }
        else if (errno != EINTR) { result = false; }
    }
    if (result && !eof)
    {
        char extra = 0;
        result = read(descriptor, &extra, 1) == 0;
    }
    if (descriptor >= 0 && close(descriptor) != 0) { result = false; }
    String8 cpuinfo = {.pointer = bytes, .length = used};
    result = result && used && production_profile_contains(cpuinfo, S8("AMD Ryzen 7 9700X"));
    bool retained = file_publish(path_join(arena, output, S8("host-cpuinfo.txt")), BUSTER_SLICE_TO_BYTE_SLICE(cpuinfo));
    return result && retained;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_prepare_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    CompilerClosurePreparation preparation = {0};
    if (arguments.length == 8 && string_equal(arguments.pointer[0], S8("prepare")))
    {
        bool complete = compiler_closure_preparation_initialize(&preparation, arena,
            arguments.pointer[1], arguments.pointer[2], arguments.pointer[3],
            arguments.pointer[4], arguments.pointer[5], arguments.pointer[6], arguments.pointer[7]);
        complete = complete && compiler_closure_preparation_execute(&preparation);
        bool written = compiler_closure_preparation_write(&preparation);
        if (complete && written && preparation.success) { result = PROCESS_RESULT_SUCCESS; }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_qualification_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool owned = false;
    bool complete = false;
    u64 started = os_now_microseconds();
    String8 root = {0};
    String8 output = {0};
    String8 lab = {0};
    String8 python = {0};
    String8 lab_sha256 = {0};
    String8 python_sha256 = {0};
    CompilerClosurePreparation legacy = {0};
    CompilerClosurePreparation snapshot = {0};
    if (arguments.length == 9 && string_equal(arguments.pointer[0], S8("qualify")))
    {
        root = os_path_absolute(arena, arguments.pointer[1], true);
        output = os_path_absolute_lexical(arena, arguments.pointer[2], true);
        lab = os_path_absolute(arena, arguments.pointer[7], true);
        python = os_path_absolute(arena, executable_resolve_in_path(arena, arguments.pointer[8]), true);
        String8 output_parent = path_parent(arena, output);
        String8 trusted = path_parent(arena, path_parent(arena, lab));
        struct stat lab_status = {0};
        struct stat python_status = {0};
        bool safe = root.length && output.length && lab.length && python.length && trusted.length &&
            string_equal(output_parent, os_path_absolute(arena, output_parent, true)) &&
            compiler_closure_path_safe(root) && compiler_closure_path_safe(output) &&
            production_profile_path_components_safe(output) &&
            !string_equal(root, output) && !production_profile_path_is_child(root, output) &&
            !production_profile_path_is_child(output, root) && !string_equal(root, trusted) &&
            !production_profile_path_is_child(root, trusted) && !production_profile_path_is_child(trusted, root) &&
            !string_equal(output, trusted) && !production_profile_path_is_child(output, trusted) &&
            !production_profile_path_is_child(trusted, output) &&
            compiler_closure_commit_valid(arguments.pointer[3]) && compiler_closure_commit_valid(arguments.pointer[4]) &&
            compiler_closure_commit_valid(arguments.pointer[5]) && compiler_closure_commit_valid(arguments.pointer[6]) &&
            compiler_closure_hash(arena, lab, &lab_sha256, &lab_status) &&
            compiler_closure_hash(arena, python, &python_sha256, &python_status) &&
            lab_status.st_size > 0 && python_status.st_size > 0 && (python_status.st_mode & 0111);
        if (safe)
        {
            OsDirectoryCreateResult directory = os_make_directory(output);
            owned = directory.created && !directory.error.v;
        }
        complete = owned && compiler_closure_qualification_host(arena, output);
        if (complete)
        {
            // Both contexts validate the original HEAD and all pins before the
            // first arm is allowed to mutate the common lexical work root.
            bool legacy_pins = compiler_closure_preparation_initialize(&legacy, arena, root, path_join(arena, output, S8("legacy")),
                S8("legacy-rebuild"), arguments.pointer[3], arguments.pointer[4], arguments.pointer[5], arguments.pointer[6]);
            bool snapshot_pins = compiler_closure_preparation_initialize(&snapshot, arena, root, path_join(arena, output, S8("snapshot")),
                S8("snapshot-v1"), arguments.pointer[3], arguments.pointer[4], arguments.pointer[5], arguments.pointer[6]);
            legacy.started_us = started;
            snapshot.started_us = started;
            complete = legacy_pins && snapshot_pins;
        }
        complete = complete && compiler_closure_preparation_execute(&legacy) &&
            compiler_closure_qualification_pair(&legacy, S8("ab"), lab, python, legacy.baseline, legacy.candidate, false) &&
            compiler_closure_qualification_pair(&legacy, S8("immutable-aa"), lab, python, legacy.baseline, legacy.baseline, true) &&
            compiler_closure_preparation_execute(&snapshot);
        if (complete)
        {
            complete = compiler_closure_preparation_begin(&snapshot, S8("matched-frozen-workload"));
            if (complete)
            {
                bool matched = string_equal(legacy.frozen_workload_sha256, snapshot.frozen_workload_sha256) &&
                    string_equal(legacy.harness_sha256, snapshot.harness_sha256) &&
                    string_equal(legacy.bootstrap_configuration, snapshot.bootstrap_configuration) &&
                    string_equal(legacy.bootstrap_artifact_sha256, snapshot.bootstrap_artifact_sha256) &&
                    string_equal(legacy.candidate_sha256, snapshot.candidate_sha256);
                complete = compiler_closure_preparation_end(&snapshot, matched, 0);
            }
        }
        complete = complete && compiler_closure_qualification_pair(&snapshot, S8("ab"), lab, python, snapshot.baseline, snapshot.candidate, false) &&
            compiler_closure_qualification_pair(&snapshot, S8("immutable-aa"), lab, python, snapshot.baseline, snapshot.baseline, true) &&
            compiler_closure_qualification_pair(&snapshot, S8("cross-build-aa"), lab, python, legacy.baseline, snapshot.baseline, true);
        bool legacy_written = legacy.owned ? compiler_closure_preparation_write(&legacy) : false;
        bool snapshot_written = snapshot.owned ? compiler_closure_preparation_write(&snapshot) : false;
        complete = complete && legacy_written && snapshot_written && legacy.success && snapshot.success;
        if (owned)
        {
            String8 receipt = string_format(arena, S8("{{\"schema\":\"buster-compiler-closure-qualification-v1\","
                "\"profile\":\"" COMPILER_CLOSURE_QUALIFICATION_PROFILE "\",\"state\":\"{S8}\","
                "\"base\":\"{S8}\",\"base_tree\":\"{S8}\",\"head\":\"{S8}\",\"head_tree\":\"{S8}\","
                "\"root_sha256\":\"{S8}\",\"trusted_lab_sha256\":\"{S8}\",\"python_sha256\":\"{S8}\","
                "\"cpu\":2,\"target_minutes\":10,\"warmups\":1,\"seed\":20261003,\"min_effect_percent\":0.5,"
                "\"planned_labs\":5,\"planned_corpora\":5,\"default_activated\":false,"
                "\"ownership_schema\":\"buster-native-qualification-supervisor-v1\",\"cleanup_proven\":{S8},"
                "\"duration_us\":{u64}\n}\n"),
                complete ? S8("complete") : S8("failed"), arguments.pointer[3], arguments.pointer[4],
                arguments.pointer[5], arguments.pointer[6], production_profile_sha256_text(arena, root),
                lab_sha256, python_sha256, !compiler_closure_cleanup_failed ? S8("true") : S8("false"), os_now_microseconds() - started);
            bool written = file_publish(path_join(arena, output, S8("qualification.json")), BUSTER_SLICE_TO_BYTE_SLICE(receipt));
            if (complete && written) { result = PROCESS_RESULT_SUCCESS; }
        }
    }
    return result;
}
#endif // BUSTER_LINUX
