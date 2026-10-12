// Native controls for the sealed raw-evidence contract (#2918), included by
// compiler_evidence.c. Every case builds a fresh fixture bundle, seals it,
// applies one mutation and checks seal/verify's verdict.
// Entry: compiler_evidence_self_test.

typedef struct CompilerEvidenceTest CompilerEvidenceTest;
struct CompilerEvidenceTest
{
    Arena* arena;
    String8 root;
    u64 index;
    u64 checks;
    u64 failures;
};

BUSTER_GLOBAL_LOCAL void compiler_evidence_expect(CompilerEvidenceTest* test, bool condition, String8 name, String8 problem)
{
    test->checks += 1;
    if (!condition)
    {
        test->failures += 1;
        string_print(S8("COMPILER_EVIDENCE_SELF_TEST_FAILURE {S8} problem={S8}\n"), name, problem.length ? problem : S8("-"));
    }
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_put(Arena* arena, String8 bundle, String8 relative, String8 text)
{
    String8 path = path_join(arena, bundle, relative);
    u64 slash = path.length;
    while (slash && path.pointer[slash - 1] != '/') { slash -= 1; }
    if (slash > 1) { make_directory_recursive(arena, (String8){.pointer = path.pointer, .length = slash - 1}); }
    return production_profile_write(path, text);
}

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_test_identity(Arena* arena, String8 candidate, String8 corpus, String8 harness)
{
    String8 result = string_format(arena,
        S8("repository\tbuster14a/buster\nrequest_run\t91\nrequest_attempt\t1\nexecutor_run\t92\nexecutor_attempt\t1\n"
           "trusted_revision\t{S8}\nbaseline_revision\t{S8}\ncandidate_revision\t{S8}\nbaseline_tree\t{S8}\n"
           "candidate_tree\t{S8}\nlab_profile\tcompiler-compare-v1\ncorpus_profile\t{S8}\ncorpus_harness_revision\t{S8}\n"),
        S8("9999999999999999999999999999999999999999"), S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), candidate,
        S8("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"), S8("dddddddddddddddddddddddddddddddddddddddd"), corpus, harness);
    return result;
}

// Role column of the manifest row naming path, or empty when no row names it.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_row_role(Arena* arena, String8 manifest, String8 path)
{
    u64 at = string_first_sequence(manifest, string_format(arena, S8("\t{S8}\n"), path));
    String8 result = {0};
    if (at != BUSTER_STRING_NO_MATCH)
    {
        u64 start = at;
        while (start > 0 && manifest.pointer[start - 1] != '\n') { start -= 1; }
        u64 end = start;
        while (end < at && manifest.pointer[end] != '\t') { end += 1; }
        result = (String8){.pointer = manifest.pointer + start, .length = end - start};
    }
    return result;
}

// A complete ordinary bundle: 10 ABBA pairs, counters only on A, metrics on both.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_fixture(CompilerEvidenceTest* test, String8 pairs_json, String8 omissions)
{
    Arena* arena = test->arena;
    String8 bundle = path_join(arena, test->root, string_format(arena, S8("case-{u64}"), test->index++));
    bool written = compiler_evidence_put(arena, bundle, S8("lab/compare.json"), string_format(arena, S8("{{\"plan\": {{\"pairs\": {S8}, \"order\": \"ABBA\"}}}}"), pairs_json)) &&
        compiler_evidence_put(arena, bundle, S8("lab/summary.json"), S8("{\"verdict\": {}}")) &&
        compiler_evidence_put(arena, bundle, S8("lab/pairs.json"), S8("[]")) &&
        compiler_evidence_put(arena, bundle, S8("lab/a/lab.json"), S8("{\"capabilities\": {\"perf_stat\": {\"usable\": true}}, \"collection\": {\"metrics_out\": true}}")) &&
        compiler_evidence_put(arena, bundle, S8("lab/b/lab.json"), S8("{\"capabilities\": {\"perf_stat\": {\"usable\": false}}, \"collection\": {\"metrics_out\": true}}")) &&
        compiler_evidence_put(arena, bundle, S8("lab/a/env/env.json"), S8("{}")) &&
        compiler_evidence_put(arena, bundle, S8("lab/report.md"), S8("optional report\n")) &&
        compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_OMISSIONS), omissions) &&
        compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_RECEIPT), S8("{\"state\": \"measured\"}")) &&
        compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_MACHINE "/report.json"), S8("{}")) &&
        compiler_evidence_put(arena, bundle, S8("throughput/artifacts/tiny_startup-fast-0.log"), S8("optional log\n"));
    for (u64 variant = 0; written && variant < 2; variant += 1)
    {
        for (u64 index = 0; written && index < BUSTER_ARRAY_LENGTH(compiler_evidence_lab_logs); index += 1)
        {
            written = compiler_evidence_put(arena, bundle, string_format(arena, S8("lab/{S8}/{S8}"), compiler_evidence_variants[variant],
                compiler_evidence_lab_logs[index]), S8("log\n"));
        }
    }
    for (u64 pair = 1; written && pair <= 10; pair += 1)
    {
        written = compiler_evidence_put(arena, bundle, string_format(arena, S8("lab/pairs/{u64:width=[0,4]}-a.csv"), pair), S8("1,,cycles\n")) &&
            compiler_evidence_put(arena, bundle, string_format(arena, S8("lab/pairs/{u64:width=[0,4]}-a.ccmetrics"), pair), S8("wall_ns 1\n")) &&
            compiler_evidence_put(arena, bundle, string_format(arena, S8("lab/pairs/{u64:width=[0,4]}-b.ccmetrics"), pair), S8("wall_ns 2\n"));
    }
    for (u64 index = 0; written && index < BUSTER_ARRAY_LENGTH(compiler_evidence_corpus_core); index += 1)
    {
        written = compiler_evidence_put(arena, bundle, compiler_evidence_corpus_core[index], S8("corpus\n"));
    }
    for (u64 index = 0; written && index < BUSTER_ARRAY_LENGTH(compiler_evidence_workloads); index += 1)
    {
        written = compiler_evidence_put(arena, bundle, string_format(arena, S8("throughput/inputs/{S8}.c"), compiler_evidence_workloads[index]),
            S8("int main(void) { return 0; }\n"));
    }
    compiler_evidence_expect(test, written, S8("fixture:written"), (String8){0});
    return bundle;
}

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_valid_omissions(void)
{
    return S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 2, \"by_reason\": {\"excluded-type\": 1, \"byte-budget\": 1}, "
        "\"examples\": [{\"path\": \"lab/a/out.exe\", \"reason\": \"excluded-type\"}]}");
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_try_seal(CompilerEvidenceTest* test, String8 bundle, String8 identity, String8* problem)
{
    String8 identity_path = path_join(test->arena, bundle, S8("../identity.tsv"));
    bool written = production_profile_write(identity_path, identity);
    return written && compiler_evidence_seal(test->arena, bundle, identity_path, problem);
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_try_verify(CompilerEvidenceTest* test, String8 bundle, String8 identity, String8* problem)
{
    String8 identity_path = path_join(test->arena, bundle, S8("../identity.tsv"));
    String8 summary = {0};
    bool written = production_profile_write(identity_path, identity);
    return written && compiler_evidence_verify(test->arena, bundle, identity_path, problem, &summary);
}

// Seal a fresh fixture, apply one change, and expect verify to refuse with a reason containing `expected`.
BUSTER_GLOBAL_LOCAL void compiler_evidence_tamper(CompilerEvidenceTest* test, String8 name, String8 relative, String8 text, String8 expected)
{
    String8 identity = compiler_evidence_test_identity(test->arena, S8("cccccccccccccccccccccccccccccccccccccccc"), S8("throughput-corpus-v2"),
        S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    String8 bundle = compiler_evidence_fixture(test, S8("10"), compiler_evidence_valid_omissions());
    String8 problem = {0};
    bool sealed = compiler_evidence_try_seal(test, bundle, identity, &problem);
    bool changed = text.length ? compiler_evidence_put(test->arena, bundle, relative, text) : remove_path_recursive(test->arena, path_join(test->arena, bundle, relative));
    String8 refusal = {0};
    bool verified = compiler_evidence_try_verify(test, bundle, identity, &refusal);
    compiler_evidence_expect(test, sealed && changed && !verified && string_first_sequence(refusal, expected) != BUSTER_STRING_NO_MATCH, name,
        refusal.length ? refusal : problem);
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_self_test(Arena* arena)
{
    char directory[] = "/tmp/buster-compiler-evidence-XXXXXX";
    CompilerEvidenceTest test = {.arena = arena};
    bool created = mkdtemp(directory) != 0;
    test.root = string_from_pointer((char8*)directory);
    compiler_evidence_expect(&test, created, S8("temporary-root"), (String8){0});
    String8 ordinary = compiler_evidence_test_identity(arena, S8("cccccccccccccccccccccccccccccccccccccccc"), S8("throughput-corpus-v2"),
        S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    if (created)
    {
        // Path identity rules.
        String8 invalid_paths[] = {S8(""), S8("/abs"), S8("a//b"), S8("a/./b"), S8("a/../b"), S8("a\\b"), S8("a/\tb"), S8("..")};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(invalid_paths); index += 1)
        {
            compiler_evidence_expect(&test, !compiler_evidence_path_valid(invalid_paths[index]), string_format(arena, S8("path:invalid-{u64}"), index), (String8){0});
        }
        compiler_evidence_expect(&test, compiler_evidence_path_valid(S8("lab/pairs/0001-a.csv")), S8("path:valid"), (String8){0});
        char8 long_path[COMPILER_EVIDENCE_PATH_LIMIT + 1];
        memset(long_path, 'x', sizeof(long_path));
        compiler_evidence_expect(&test, !compiler_evidence_path_valid((String8){.pointer = long_path, .length = sizeof(long_path)}), S8("path:too-long"), (String8){0});

        // A complete bundle seals and verifies; the workflow's later machine record and a
        // checkpointed receipt are outside the manifest.
        String8 problem = {0};
        String8 bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        bool sealed = compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
        compiler_evidence_expect(&test, sealed, S8("normal:seal"), problem);
        problem = (String8){0};
        compiler_evidence_expect(&test, compiler_evidence_try_verify(&test, bundle, ordinary, &problem), S8("normal:verify"), problem);
        String8 manifest = compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), COMPILER_EVIDENCE_MANIFEST_LIMIT);
        compiler_evidence_expect(&test, string_first_sequence(manifest, S8("optional\t")) != BUSTER_STRING_NO_MATCH &&
            string_first_sequence(manifest, S8("\tlab/pairs/0001-b.csv\n")) == BUSTER_STRING_NO_MATCH &&
            string_first_sequence(manifest, S8("receipt.json")) == BUSTER_STRING_NO_MATCH &&
            string_first_sequence(manifest, S8(COMPILER_EVIDENCE_MACHINE)) == BUSTER_STRING_NO_MATCH, S8("normal:manifest-rows"), (String8){0});
        String8 roles[][2] = {
            {S8(COMPILER_EVIDENCE_OMISSIONS), S8("required")}, {S8("lab/compare.json"), S8("required")},
            {S8("lab/a/env/env.json"), S8("required")}, {S8("lab/b/warmup-0.log"), S8("required")},
            {S8("lab/pairs/0010-a.csv"), S8("required")}, {S8("lab/pairs/0010-b.ccmetrics"), S8("required")},
            {S8("throughput/inputs/tiny_startup.c"), S8("required")}, {S8("throughput/complete.txt"), S8("required")},
            {S8("lab/report.md"), S8("optional")}, {S8("throughput/artifacts/tiny_startup-fast-0.log"), S8("optional")},
        };
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(roles); index += 1)
        {
            compiler_evidence_expect(&test, string_equal(compiler_evidence_row_role(arena, manifest, roles[index][0]), roles[index][1]),
                string_format(arena, S8("normal:role:{S8}"), roles[index][0]), (String8){0});
        }
        compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_MACHINE "/sources.jsonl"), S8("{}\n"));
        problem = (String8){0};
        compiler_evidence_expect(&test, compiler_evidence_try_verify(&test, bundle, ordinary, &problem), S8("normal:machine-record-appended"), problem);
        problem = (String8){0};
        compiler_evidence_expect(&test, !compiler_evidence_try_seal(&test, bundle, ordinary, &problem), S8("reseal-refused"), problem);

        // Identity bindings.
        problem = (String8){0};
        String8 other = compiler_evidence_test_identity(arena, S8("ffffffffffffffffffffffffffffffffffffffff"), S8("throughput-corpus-v2"),
            S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
        compiler_evidence_expect(&test, !compiler_evidence_try_verify(&test, bundle, other, &problem), S8("identity:candidate-differs"), problem);
        problem = (String8){0};
        String8 historical = compiler_evidence_test_identity(arena, S8("cccccccccccccccccccccccccccccccccccccccc"), S8("throughput-corpus-historical-v1"),
            S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
        compiler_evidence_expect(&test, !compiler_evidence_try_verify(&test, bundle, historical, &problem), S8("identity:historical-unpinned"), problem);
        problem = (String8){0};
        compiler_evidence_expect(&test, !compiler_evidence_identity(arena, historical, &problem).length, S8("identity:historical-unpinned-parse"), (String8){0});
        String8 pinned = compiler_evidence_test_identity(arena, S8("cccccccccccccccccccccccccccccccccccccccc"), S8("throughput-corpus-historical-v1"),
            S8(COMPILER_EVIDENCE_HARNESS_REVISION));
        problem = (String8){0};
        compiler_evidence_expect(&test, compiler_evidence_identity(arena, pinned, &problem).length != 0, S8("identity:historical-pinned-parse"), problem);
        String8 malformed[] = {S8(""), S8("repository\tbuster14a/buster\n"), string_format(arena, S8("{S8}extra\tvalue\n"), ordinary),
            string_format(arena, S8("x{S8}"), ordinary)};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(malformed); index += 1)
        {
            problem = (String8){0};
            compiler_evidence_expect(&test, !compiler_evidence_identity(arena, malformed[index], &problem).length,
                string_format(arena, S8("identity:malformed-{u64}"), index), (String8){0});
        }

        // Retained bytes, roles and seal.
        compiler_evidence_tamper(&test, S8("tamper:raw-byte"), S8("lab/pairs/0003-a.csv"), S8("2,,cycles\n"), S8("reproduce"));
        compiler_evidence_tamper(&test, S8("tamper:summary"), S8("throughput/summary.json"), S8("{\"valid\": false}"), S8("reproduce"));
        compiler_evidence_tamper(&test, S8("tamper:required-removed"), S8("throughput/complete.txt"), (String8){0}, S8("required raw evidence is missing"));
        compiler_evidence_tamper(&test, S8("tamper:pair-removed"), S8("lab/pairs/0010-b.ccmetrics"), (String8){0}, S8("required raw evidence is missing"));
        compiler_evidence_tamper(&test, S8("tamper:optional-removed"), S8("lab/report.md"), (String8){0}, S8("reproduce"));
        compiler_evidence_tamper(&test, S8("tamper:extra-member"), S8("lab/unlisted.txt"), S8("x\n"), S8("reproduce"));
        compiler_evidence_tamper(&test, S8("tamper:availability"), S8("lab/b/lab.json"),
            S8("{\"capabilities\": {\"perf_stat\": {\"usable\": true}}, \"collection\": {\"metrics_out\": true}}"), S8("required raw evidence is missing"));
        compiler_evidence_tamper(&test, S8("tamper:seal"), S8(COMPILER_EVIDENCE_SEAL), S8("schema=" COMPILER_EVIDENCE_SEAL_SCHEMA "\nstate=complete\n"), S8("seal"));
        compiler_evidence_tamper(&test, S8("tamper:receipt-removed"), S8(COMPILER_EVIDENCE_RECEIPT), (String8){0}, S8("receipt"));

        // A required row relabelled optional with a matching recomputed seal still refuses.
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        problem = (String8){0};
        sealed = compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
        manifest = compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), COMPILER_EVIDENCE_MANIFEST_LIMIT);
        u64 row = string_first_sequence(manifest, S8("required\t"));
        bool relabelled = sealed && row != BUSTER_STRING_NO_MATCH;
        if (relabelled)
        {
            String8 forged = string_format(arena, S8("{S8}optional{S8}"), (String8){.pointer = manifest.pointer, .length = row},
                (String8){.pointer = manifest.pointer + row + 8, .length = manifest.length - row - 8});
            relabelled = compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST), forged) &&
                compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_SEAL), compiler_evidence_seal_text(arena, forged));
        }
        problem = (String8){0};
        compiler_evidence_expect(&test, relabelled && !compiler_evidence_try_verify(&test, bundle, ordinary, &problem), S8("tamper:relabel-with-reseal"), problem);

        // Incomplete bundles never seal.
        String8 summary_only = path_join(arena, test.root, S8("summary-only"));
        bool four = compiler_evidence_put(arena, summary_only, S8(COMPILER_EVIDENCE_RECEIPT), S8("{}")) &&
            compiler_evidence_put(arena, summary_only, S8("lab/summary.json"), S8("{}")) &&
            compiler_evidence_put(arena, summary_only, S8("throughput/summary.json"), S8("{}")) &&
            compiler_evidence_put(arena, summary_only, S8("throughput/metadata.json"), S8("{}"));
        problem = (String8){0};
        compiler_evidence_expect(&test, four && !compiler_evidence_try_seal(&test, summary_only, ordinary, &problem), S8("incomplete:summary-only-seal"), problem);
        problem = (String8){0};
        compiler_evidence_expect(&test, !compiler_evidence_try_verify(&test, summary_only, ordinary, &problem) &&
            string_first_sequence(problem, S8("no bounded manifest")) != BUSTER_STRING_NO_MATCH, S8("incomplete:summary-only-verify"), problem);
        String8 bad_plans[] = {S8("9"), S8("11"), S8("1002"), S8("\"10\""), S8("-10")};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(bad_plans); index += 1)
        {
            problem = (String8){0};
            bundle = compiler_evidence_fixture(&test, bad_plans[index], compiler_evidence_valid_omissions());
            compiler_evidence_expect(&test, !compiler_evidence_try_seal(&test, bundle, ordinary, &problem) &&
                string_first_sequence(problem, S8("lab plan")) != BUSTER_STRING_NO_MATCH, string_format(arena, S8("incomplete:plan-{u64}"), index), problem);
        }
        String8 bad_omissions[] = {
            S8("{\"schema\": \"other\", \"total\": 0, \"by_reason\": {}, \"examples\": []}"),
            S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 1, \"by_reason\": {}, \"examples\": []}"),
            S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 1, \"by_reason\": {\"lost\": 1}, \"examples\": []}"),
            S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 0, \"by_reason\": {}, \"examples\": [], \"extra\": 1}"),
            S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 0, \"by_reason\": {}, \"examples\": []}"),
            S8("{\"schema\": \"" COMPILER_EVIDENCE_OMISSIONS_SCHEMA "\", \"total\": 0, \"by_reason\": {}, \"examples\": [{\"path\": \"x\", \"reason\": \"read-error\"}]}"),
        };
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(bad_omissions); index += 1)
        {
            problem = (String8){0};
            bundle = compiler_evidence_fixture(&test, S8("10"), bad_omissions[index]);
            compiler_evidence_expect(&test, !compiler_evidence_try_seal(&test, bundle, ordinary, &problem),
                string_format(arena, S8("incomplete:omissions-{u64}"), index), problem);
        }

        // Hidden entries, which actions/upload-artifact drops, are not retained rows.
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        bool hidden = compiler_evidence_put(arena, bundle, S8(".buster-compiler-compare-scratch"), S8("marker\n")) &&
            compiler_evidence_put(arena, bundle, S8("lab/.cache/state"), S8("state\n"));
        problem = (String8){0};
        sealed = hidden && compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
        manifest = compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), COMPILER_EVIDENCE_MANIFEST_LIMIT);
        compiler_evidence_expect(&test, sealed && string_first_sequence(manifest, S8("/.")) == BUSTER_STRING_NO_MATCH &&
            string_first_sequence(manifest, S8("\t.")) == BUSTER_STRING_NO_MATCH, S8("hidden:not-retained"), problem);
        sealed = sealed && unlink((char*)string_duplicate_arena(arena, path_join(arena, bundle, S8(".buster-compiler-compare-scratch")), true).pointer) == 0;
        problem = (String8){0};
        compiler_evidence_expect(&test, sealed && compiler_evidence_try_verify(&test, bundle, ordinary, &problem), S8("hidden:dropped-by-upload"), problem);

        // A failed final pass or an exhausted aggregate bound leaves no completion marker behind.
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        compiler_evidence_seal_fault = S8("lab/report.md");
        problem = (String8){0};
        sealed = compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
        compiler_evidence_seal_fault = (String8){0};
        String8 seal_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_SEAL)), true);
        String8 manifest_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), true);
        struct stat unused = {0};
        compiler_evidence_expect(&test, !sealed && lstat((char*)seal_path.pointer, &unused) != 0 &&
            lstat((char*)manifest_path.pointer, &unused) != 0, S8("seal:final-pass-failure-unpublished"), problem);
        problem = (String8){0};
        compiler_evidence_expect(&test, compiler_evidence_try_seal(&test, bundle, ordinary, &problem), S8("seal:retry-after-failure"), problem);
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        // Members within 48 MiB fit alone but not with the manifest, seal and 1 MiB receipt bound.
        u64 used = 0;
        for (u64 index = 0; index < 2; index += 1)
        {
            u64 size = (COMPILER_EVIDENCE_TOTAL_LIMIT - (512ull << 10)) / 2;
            String8 filler = {.pointer = arena_allocate(arena, char8, size), .length = size};
            memset(filler.pointer, 'x', size);
            used += compiler_evidence_put(arena, bundle, string_format(arena, S8("lab/filler-{u64}.bin"), index), filler) ? size : 0;
        }
        problem = (String8){0};
        seal_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_SEAL)), true);
        manifest_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), true);
        sealed = compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
        compiler_evidence_expect(&test, used && !sealed && string_first_sequence(problem, S8("would exceed")) != BUSTER_STRING_NO_MATCH &&
            lstat((char*)seal_path.pointer, &unused) != 0 && lstat((char*)manifest_path.pointer, &unused) != 0,
            S8("seal:aggregate-bound-before-publication"), problem);
        remove_path_recursive(arena, bundle);

        // Directories count against the entry bound and take part in path and alias checks, control files too.
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        // 65 branches of 64 empty directories: no listing is large, but the namespace exceeds 4096 entries.
        bool nested = true;
        for (u64 index = 0; nested && index < 65 * 64; index += 1)
        {
            String8 empty = path_join(arena, bundle, string_format(arena, S8("lab/empty/{u64}/{u64}"), index / 64, index % 64));
            make_directory_recursive(arena, empty);
            struct stat made = {0};
            nested = lstat((char*)string_duplicate_arena(arena, empty, true).pointer, &made) == 0 && S_ISDIR(made.st_mode);
        }
        problem = (String8){0};
        compiler_evidence_expect(&test, nested && !compiler_evidence_try_seal(&test, bundle, ordinary, &problem) &&
            string_first_sequence(problem, S8("entr")) != BUSTER_STRING_NO_MATCH, S8("refuse:empty-directory-entries"), problem);
        remove_path_recursive(arena, bundle);
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        String8 tabbed_directory = string_duplicate_arena(arena, path_join(arena, bundle, S8("lab/bad\tdirectory")), true);
        problem = (String8){0};
        compiler_evidence_expect(&test, mkdir((char*)tabbed_directory.pointer, 0700) == 0 && !compiler_evidence_try_seal(&test, bundle, ordinary, &problem),
            S8("refuse:empty-directory-path"), problem);
        String8 control_aliases[] = {S8("Receipt.json"), S8("EVIDENCE-SEAL.txt"), S8("Machine-Specifications/report.json")};
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(control_aliases); index += 1)
        {
            bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
            bool written = compiler_evidence_put(arena, bundle, control_aliases[index], S8("{}"));
            if (index == 1) { written = written && compiler_evidence_put(arena, bundle, S8(COMPILER_EVIDENCE_SEAL), S8("x\n")); }
            problem = (String8){0};
            bool refused = index == 1 ? !compiler_evidence_try_verify(&test, bundle, ordinary, &problem) :
                !compiler_evidence_try_seal(&test, bundle, ordinary, &problem);
            compiler_evidence_expect(&test, written && refused, string_format(arena, S8("refuse:control-alias-{u64}"), index), problem);
        }

        // Non-regular entries and case-folded aliases refuse the bundle.
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        String8 link = string_duplicate_arena(arena, path_join(arena, bundle, S8("lab/link.txt")), true);
        bool linked = symlink("report.md", (char*)link.pointer) == 0;
        problem = (String8){0};
        compiler_evidence_expect(&test, linked && !compiler_evidence_try_seal(&test, bundle, ordinary, &problem), S8("refuse:symlink"), problem);
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        bool aliased = compiler_evidence_put(arena, bundle, S8("lab/Report.md"), S8("alias\n"));
        problem = (String8){0};
        compiler_evidence_expect(&test, aliased && !compiler_evidence_try_seal(&test, bundle, ordinary, &problem), S8("refuse:case-alias"), problem);
        bundle = compiler_evidence_fixture(&test, S8("10"), compiler_evidence_valid_omissions());
        bool tabbed = compiler_evidence_put(arena, bundle, S8("lab/bad\tname.txt"), S8("x\n"));
        problem = (String8){0};
        compiler_evidence_expect(&test, tabbed && !compiler_evidence_try_seal(&test, bundle, ordinary, &problem), S8("refuse:path-control-byte"), problem);
    }
    bool removed = created && remove_path_recursive(arena, test.root);
    compiler_evidence_expect(&test, removed, S8("temporary-root-removed"), (String8){0});
    string_print(S8("COMPILER_EVIDENCE_SELF_TEST checks={u64} failures={u64}\n"), test.checks, test.failures);
    return test.failures == 0;
}
