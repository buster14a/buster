// Sealed raw compiler-benchmark evidence (#2918), included by the trusted build.c.
// Entry: compiler_evidence_main.
//   seal BUNDLE IDENTITY    write evidence-manifest.tsv, then evidence-seal.txt last
//   verify BUNDLE IDENTITY  refuse unless the retained bytes reproduce that sealed manifest
//   self-test               native normal and negative controls
// IDENTITY is the trusted attempt identity as key<TAB>value lines in
// COMPILER_EVIDENCE_IDENTITY_KEYS order, optionally followed by the
// compiler_evidence_route_keys lines of a snapshot-v1 attempt (#3211), which
// select the snapshot inventory and its exact owned-phase count. Required
// members are derived here from the admitted profiles, that route and the
// bundle's own lab plan/availability records, never from a producer's role
// column: compiler_evidence_manifest regenerates the canonical manifest from
// disk, and verify compares it byte for byte.
// receipt.json binds the seal; machine-specifications/ is appended by the
// workflow after the producer exits and is validated by the publisher's own
// machine record, so neither is a manifest row.
//
// Map: compiler_evidence_identity (trusted header), compiler_evidence_path_valid
// (path identity rules), compiler_evidence_collect (bounded nofollow walk; hidden
// entries are not retained),
// compiler_evidence_facts / compiler_evidence_required / compiler_evidence_phase_member
// (inventory v1, ordinary and snapshot),
// compiler_evidence_omissions_valid, compiler_evidence_manifest,
// compiler_evidence_seal_text, compiler_evidence_seal, compiler_evidence_verify,
// compiler_evidence_self_test, compiler_evidence_main.
//
// Linux is the approved measurement and publication platform, like
// compiler_closure; other driver builds report an explicit unsupported result.
#define COMPILER_EVIDENCE_MANIFEST_SCHEMA "buster-compiler-evidence-manifest-v1"
#define COMPILER_EVIDENCE_INVENTORY "buster-compiler-ordinary-raw-v1"
// The same inventory plus the snapshot-v1 closure records and owned-phase population.
#define COMPILER_EVIDENCE_SNAPSHOT_INVENTORY "buster-compiler-snapshot-raw-v1"
#define COMPILER_EVIDENCE_SEAL_SCHEMA "buster-compiler-evidence-seal-v1"
#define COMPILER_EVIDENCE_OMISSIONS_SCHEMA "buster-compiler-evidence-omissions-v1"
#define COMPILER_EVIDENCE_MANIFEST "evidence-manifest.tsv"
#define COMPILER_EVIDENCE_SEAL "evidence-seal.txt"
#define COMPILER_EVIDENCE_OMISSIONS "evidence-omissions.json"
#define COMPILER_EVIDENCE_RECEIPT "receipt.json"
#define COMPILER_EVIDENCE_MACHINE "machine-specifications"
// The artifact's inflated retained budget and entry count, including control files.
#define COMPILER_EVIDENCE_TOTAL_LIMIT (48ull << 20)
#define COMPILER_EVIDENCE_ENTRY_LIMIT 4096ull
#define COMPILER_EVIDENCE_JSON_LIMIT (8ull << 20)
#define COMPILER_EVIDENCE_MEMBER_LIMIT (32ull << 20)
#define COMPILER_EVIDENCE_MANIFEST_LIMIT (4ull << 20)
#define COMPILER_EVIDENCE_RECEIPT_LIMIT (1ull << 20)
#define COMPILER_EVIDENCE_SEAL_LIMIT 4096ull
#define COMPILER_EVIDENCE_OMISSIONS_LIMIT (32ull << 10)
#define COMPILER_EVIDENCE_OMISSION_EXAMPLES 50ull
#define COMPILER_EVIDENCE_PATH_LIMIT 512ull
#define COMPILER_EVIDENCE_PAIRS_MIN 10ull
#define COMPILER_EVIDENCE_PAIRS_MAX 1000ull
#define COMPILER_EVIDENCE_HARNESS_REVISION "afe67cbebcb257e7fe130a8eccdd5ff08a79b555"
// compiler_owned_phase.PHASE_LIMIT: the native owner admits at most this many phases.
#define COMPILER_EVIDENCE_PHASE_LIMIT 256ull
#define COMPILER_EVIDENCE_PHASE_DIRECTORY "owned-phases/"

#if BUSTER_LINUX
// The trusted identity header, in manifest order after schema and inventory.
typedef struct CompilerEvidenceKey CompilerEvidenceKey;
struct CompilerEvidenceKey
{
    String8 name;
    u8 kind;
};
// Kinds: r repository, d positive decimal, h 40 lowercase hex, l lab profile, c corpus profile,
// p snapshot preparation policy, n owned-phase count.
BUSTER_GLOBAL_LOCAL CompilerEvidenceKey const compiler_evidence_identity_keys[] = {
    {S8_INITIALIZER("repository"), 'r'}, {S8_INITIALIZER("request_run"), 'd'}, {S8_INITIALIZER("request_attempt"), 'd'},
    {S8_INITIALIZER("executor_run"), 'd'}, {S8_INITIALIZER("executor_attempt"), 'd'},
    {S8_INITIALIZER("trusted_revision"), 'h'}, {S8_INITIALIZER("baseline_revision"), 'h'},
    {S8_INITIALIZER("candidate_revision"), 'h'}, {S8_INITIALIZER("baseline_tree"), 'h'},
    {S8_INITIALIZER("candidate_tree"), 'h'}, {S8_INITIALIZER("lab_profile"), 'l'},
    {S8_INITIALIZER("corpus_profile"), 'c'}, {S8_INITIALIZER("corpus_harness_revision"), 'h'},
};

// The optional trailing route lines of a snapshot-v1 attempt; absent means legacy-rebuild.
BUSTER_GLOBAL_LOCAL CompilerEvidenceKey const compiler_evidence_route_keys[] = {
    {S8_INITIALIZER("preparation_policy"), 'p'}, {S8_INITIALIZER("owned_phases"), 'n'},
};

// The snapshot-v1 closure records and their manifests, written beside the receipt.
BUSTER_GLOBAL_LOCAL String8 const compiler_evidence_closure_members[] = {
    S8_INITIALIZER("closure-snapshot.json"), S8_INITIALIZER("closure-snapshot.json.manifest.tsv"),
    S8_INITIALIZER("closure-restore.json"), S8_INITIALIZER("closure-restore.json.manifest.tsv"),
    S8_INITIALIZER("closure-verify.json"), S8_INITIALIZER("closure-verify.json.manifest.tsv"),
};

// The five members the native owner writes for each phase NNNN.json.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_phase_suffixes[] = {
    S8_INITIALIZER(""), S8_INITIALIZER(".argv"), S8_INITIALIZER(".stdout"), S8_INITIALIZER(".stderr"),
    S8_INITIALIZER(".bootstrap.complete"),
};

// The trusted frozen corpus population (THROUGHPUT_PROFILE["workloads"]).
// Tables passed as string_format arguments stay non-const: TCC rejects a
// const-qualified struct lvalue as a variadic argument.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_workloads[] = {
    S8_INITIALIZER("tiny_startup"), S8_INITIALIZER("large_function"), S8_INITIALIZER("many_functions"),
    S8_INITIALIZER("symbol_table"), S8_INITIALIZER("control_flow"), S8_INITIALIZER("backend_pressure"),
};

BUSTER_GLOBAL_LOCAL String8 const compiler_evidence_lab_fixed[] = {
    S8_INITIALIZER("lab/compare.json"), S8_INITIALIZER("lab/summary.json"), S8_INITIALIZER("lab/pairs.json"),
    S8_INITIALIZER("lab/a/lab.json"), S8_INITIALIZER("lab/b/lab.json"), S8_INITIALIZER("lab/a/env/env.json"),
};

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_lab_logs[] = {
    S8_INITIALIZER("commands.log"), S8_INITIALIZER("wrapper-rss.log"), S8_INITIALIZER("source-run.log"),
    S8_INITIALIZER("metrics-probe.log"), S8_INITIALIZER("warmup-0.log"),
};

BUSTER_GLOBAL_LOCAL String8 const compiler_evidence_corpus_core[] = {
    S8_INITIALIZER("throughput/samples.csv"), S8_INITIALIZER("throughput/telemetry.csv"),
    S8_INITIALIZER("throughput/metadata.json"), S8_INITIALIZER("throughput/jobs.tsv"),
    S8_INITIALIZER("throughput/commands.jsonl"), S8_INITIALIZER("throughput/capabilities.jsonl"),
    S8_INITIALIZER("throughput/complete.txt"), S8_INITIALIZER("throughput/summary.json"),
    S8_INITIALIZER("throughput/cpuinfo.txt"),
};

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_variants[] = {S8_INITIALIZER("a"), S8_INITIALIZER("b")};

BUSTER_GLOBAL_LOCAL String8 const compiler_evidence_omission_reasons[] = {
    S8_INITIALIZER("excluded-type"), S8_INITIALIZER("member-limit"), S8_INITIALIZER("byte-budget"),
    S8_INITIALIZER("file-budget"), S8_INITIALIZER("changed-during-copy"), S8_INITIALIZER("read-error"),
};

typedef struct CompilerEvidenceFacts CompilerEvidenceFacts;
struct CompilerEvidenceFacts
{
    u64 pairs;
    // Trusted owned-phase count of a snapshot-v1 attempt; 0 for legacy-rebuild.
    u64 phases;
    bool counters[2];
    bool metrics[2];
};

typedef struct CompilerEvidenceBundle CompilerEvidenceBundle;
struct CompilerEvidenceBundle
{
    String8* paths;
    u64 count;
    u64 entries;
    u64 bytes;
    u64 receipt_bytes;
    bool receipt;
};

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_join(Arena* arena, String8List list)
{
    return string_join_arena(arena, string8_list_to_slice(arena, list), false);
}

BUSTER_GLOBAL_LOCAL void compiler_evidence_refuse(String8* problem, String8 reason)
{
    if (!problem->length) { *problem = reason; }
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_value_valid(String8 value, u8 kind)
{
    bool result = value.length != 0;
    if (kind == 'h')
    {
        result = value.length == 40;
        for (u64 index = 0; result && index < value.length; index += 1)
        {
            char8 byte = value.pointer[index];
            result = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
        }
    }
    else if (kind == 'd')
    {
        result = value.length <= 19 && value.pointer[0] >= '1' && value.pointer[0] <= '9';
        for (u64 index = 0; result && index < value.length; index += 1)
        {
            result = value.pointer[index] >= '0' && value.pointer[index] <= '9';
        }
    }
    else if (kind == 'r')
    {
        u64 slashes = 0;
        for (u64 index = 0; result && index < value.length; index += 1)
        {
            char8 byte = value.pointer[index];
            slashes += byte == '/';
            result = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                byte == '_' || byte == '.' || byte == '-' || byte == '/';
        }
        result = result && slashes == 1 && value.pointer[0] != '/' && value.pointer[value.length - 1] != '/';
    }
    else if (kind == 'l')
    {
        result = string_equal(value, S8("compiler-compare-v1"));
    }
    else if (kind == 'c')
    {
        result = string_equal(value, S8("throughput-corpus-v2")) || string_equal(value, S8("throughput-corpus-historical-v1"));
    }
    else if (kind == 'p')
    {
        result = string_equal(value, S8("snapshot-v1"));
    }
    else if (kind == 'n')
    {
        u64 count = 0;
        result = value.length && value.length <= 3 && value.pointer[0] >= '1' && value.pointer[0] <= '9';
        for (u64 index = 0; result && index < value.length; index += 1)
        {
            result = value.pointer[index] >= '0' && value.pointer[index] <= '9';
            count = count * 10 + (u64)(value.pointer[index] - '0');
        }
        result = result && count <= COMPILER_EVIDENCE_PHASE_LIMIT;
    }
    return result;
}

// The value of the key<TAB>value line at *cursor, advancing past it, or empty when malformed.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_identity_line(String8 text, u64* cursor, CompilerEvidenceKey key)
{
    u64 end = *cursor;
    while (end < text.length && text.pointer[end] != '\n') { end += 1; }
    String8 line = {.pointer = text.pointer + *cursor, .length = end - *cursor};
    bool valid = end < text.length && line.length > key.name.length + 1 && string_starts_with_sequence(line, key.name) &&
        line.pointer[key.name.length] == '\t';
    String8 value = valid ? (String8){.pointer = line.pointer + key.name.length + 1, .length = line.length - key.name.length - 1} : (String8){0};
    valid = valid && compiler_evidence_value_valid(value, key.kind);
    if (valid) { *cursor = end + 1; }
    return valid ? value : (String8){0};
}

// The trusted identity file becomes the manifest's identity header lines. The
// historical recipe always names its pinned harness revision. Trailing route
// lines select the snapshot inventory and set *phases; the snapshot route runs
// only the frozen throughput-corpus-v2 recipe, as the producer requires.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_identity(Arena* arena, String8 text, u64* phases, String8* problem)
{
    String8List lines = {0};
    u64 cursor = 0;
    String8 corpus = {0};
    String8 harness = {0};
    u64 count = 0;
    bool valid = text.length != 0;
    for (u64 key = 0; valid && key < BUSTER_ARRAY_LENGTH(compiler_evidence_identity_keys); key += 1)
    {
        String8 value = compiler_evidence_identity_line(text, &cursor, compiler_evidence_identity_keys[key]);
        valid = value.length != 0;
        if (valid)
        {
            string8_list_push(arena, &lines, string_format(arena, S8("{S8}\t{S8}\n"), compiler_evidence_identity_keys[key].name, value));
            if (string_equal(compiler_evidence_identity_keys[key].name, S8("corpus_profile"))) { corpus = value; }
            if (string_equal(compiler_evidence_identity_keys[key].name, S8("corpus_harness_revision"))) { harness = value; }
        }
    }
    bool snapshot = valid && cursor < text.length;
    for (u64 key = 0; snapshot && valid && key < BUSTER_ARRAY_LENGTH(compiler_evidence_route_keys); key += 1)
    {
        String8 value = compiler_evidence_identity_line(text, &cursor, compiler_evidence_route_keys[key]);
        valid = value.length != 0;
        if (valid)
        {
            string8_list_push(arena, &lines, string_format(arena, S8("{S8}\t{S8}\n"), compiler_evidence_route_keys[key].name, value));
            for (u64 index = 0; compiler_evidence_route_keys[key].kind == 'n' && index < value.length; index += 1)
            {
                count = count * 10 + (u64)(value.pointer[index] - '0');
            }
        }
    }
    valid = valid && cursor == text.length &&
        (!string_equal(corpus, S8("throughput-corpus-historical-v1")) || string_equal(harness, S8(COMPILER_EVIDENCE_HARNESS_REVISION))) &&
        (!snapshot || string_equal(corpus, S8("throughput-corpus-v2")));
    if (!valid) { compiler_evidence_refuse(problem, S8("trusted identity is malformed")); }
    *phases = valid ? count : 0;
    String8 inventory = snapshot ? S8("inventory\t" COMPILER_EVIDENCE_SNAPSHOT_INVENTORY "\n") : S8("inventory\t" COMPILER_EVIDENCE_INVENTORY "\n");
    String8 result = valid ? string_format(arena, S8("schema\t" COMPILER_EVIDENCE_MANIFEST_SCHEMA "\n{S8}{S8}"), inventory,
        compiler_evidence_join(arena, lines)) : (String8){0};
    return result;
}

// Printable ASCII without backslash, at most 512 bytes, relative, no empty, `.` or `..` component.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_path_valid(String8 path)
{
    bool result = path.length != 0 && path.length <= COMPILER_EVIDENCE_PATH_LIMIT && path.pointer[0] != '/';
    u64 start = 0;
    for (u64 index = 0; result && index <= path.length; index += 1)
    {
        if (index == path.length || path.pointer[index] == '/')
        {
            String8 component = {.pointer = path.pointer + start, .length = index - start};
            result = component.length && !string_equal(component, S8(".")) && !string_equal(component, S8(".."));
            start = index + 1;
        }
        else
        {
            char8 byte = path.pointer[index];
            result = byte >= 0x20 && byte <= 0x7e && byte != '\\';
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_control(String8 path)
{
    bool result = string_equal(path, S8(COMPILER_EVIDENCE_RECEIPT)) || string_equal(path, S8(COMPILER_EVIDENCE_MANIFEST)) ||
        string_equal(path, S8(COMPILER_EVIDENCE_SEAL)) || string_starts_with_sequence(path, S8(COMPILER_EVIDENCE_MACHINE "/"));
    return result;
}

BUSTER_GLOBAL_LOCAL void compiler_evidence_sort(String8* paths, u64 count)
{
    // In-place heap sort in bytewise order; no comparator callback dispatch.
    for (u64 start = count / 2; start; start -= 1)
    {
        u64 root = start - 1;
        while (root < count / 2)
        {
            u64 child = root * 2 + 1;
            if (child + 1 < count && assembly_import_string_compare(&paths[child], &paths[child + 1]) < 0) { child += 1; }
            if (assembly_import_string_compare(&paths[root], &paths[child]) >= 0) { break; }
            String8 swap = paths[root];
            paths[root] = paths[child];
            paths[child] = swap;
            root = child;
        }
    }
    for (u64 end = count; end > 1; end -= 1)
    {
        String8 swap = paths[0];
        paths[0] = paths[end - 1];
        paths[end - 1] = swap;
        u64 root = 0;
        u64 limit = end - 1;
        while (root < limit / 2)
        {
            u64 child = root * 2 + 1;
            if (child + 1 < limit && assembly_import_string_compare(&paths[child], &paths[child + 1]) < 0) { child += 1; }
            if (assembly_import_string_compare(&paths[root], &paths[child]) >= 0) { break; }
            swap = paths[root];
            paths[root] = paths[child];
            paths[child] = swap;
            root = child;
        }
    }
}

// Bounded, nofollow walk of every retained regular file. A symlink, FIFO or
// other entry type refuses the whole bundle; directories carry no bytes.
// The whole retained namespace: every non-hidden directory and file, control
// files included, is counted against the entry bound, path-validated and
// alias-checked. Only machine-specifications/, appended after the producer and
// validated by the publisher's own machine record, is not walked; its name
// still takes part in alias detection.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_collect(Arena* arena, String8 bundle, CompilerEvidenceBundle* output, String8* problem)
{
    String8* paths = arena_allocate(arena, String8, COMPILER_EVIDENCE_ENTRY_LIMIT);
    String8* names = arena_allocate(arena, String8, COMPILER_EVIDENCE_ENTRY_LIMIT + 1);
    u64 count = 0;
    u64 named = 0;
    u64 entries = 0;
    u64 bytes = 0;
    u64 receipt_bytes = 0;
    bool receipt = false;
    String8List pending = {0};
    string8_list_push(arena, &pending, S8(""));
    bool success = true;
    for (String8Node* node = pending.first; success && node; node = node->next)
    {
        String8 directory = node->string.length ? path_join(arena, bundle, node->string) : bundle;
        MuslDirectoryEntry* listed = 0;
        u64 listed_count = 0;
        success = compiler_closure_list(arena, directory, COMPILER_EVIDENCE_ENTRY_LIMIT - entries + 1, &listed, &listed_count);
        // The listing cap is the remaining entry bound, so an oversized namespace also refuses here.
        if (!success) { compiler_evidence_refuse(problem, S8("bundle has a symlink, special file, unreadable directory or more retained entries than its bound")); }
        for (u64 index = 0; success && index < listed_count; index += 1)
        {
            String8 relative = node->string.length ? path_join(arena, node->string, listed[index].name) : listed[index].name;
            // Hidden entries are not retained: actions/upload-artifact drops them (the
            // producer's scratch marker is one), so no manifest row may name them.
            bool hidden = listed[index].name.length && listed[index].name.pointer[0] == '.';
            bool machine = listed[index].is_directory && string_equal(relative, S8(COMPILER_EVIDENCE_MACHINE));
            if (!hidden)
            {
                entries += !machine;
                success = entries <= COMPILER_EVIDENCE_ENTRY_LIMIT && compiler_evidence_path_valid(relative);
                if (success) { names[named++] = relative; }
            }
            if (!hidden && success && listed[index].is_directory && !machine)
            {
                string8_list_push(arena, &pending, relative);
            }
            else if (!hidden && success && !listed[index].is_directory)
            {
                String8 terminated = string_duplicate_arena(arena, path_join(arena, bundle, relative), true);
                struct stat status = {0};
                success = lstat((char*)terminated.pointer, &status) == 0 && S_ISREG(status.st_mode) && status.st_size >= 0 &&
                    (u64)status.st_size <= COMPILER_EVIDENCE_TOTAL_LIMIT - bytes;
                if (success)
                {
                    bytes += (u64)status.st_size;
                    if (string_equal(relative, S8(COMPILER_EVIDENCE_RECEIPT)))
                    {
                        receipt = true;
                        receipt_bytes = (u64)status.st_size;
                    }
                    if (!compiler_evidence_control(relative)) { paths[count++] = relative; }
                }
            }
            if (!success)
            {
                compiler_evidence_refuse(problem, string_format(arena, S8("retained entry count, byte budget or path identity refused: {S8}"), relative));
            }
        }
    }
    if (success)
    {
        compiler_evidence_sort(paths, count);
        // Case-folded aliases refuse across the whole namespace, even on a case-sensitive filesystem.
        for (u64 index = 0; index < named; index += 1)
        {
            names[index] = string_duplicate_arena(arena, names[index], false);
            for (u64 byte = 0; byte < names[index].length; byte += 1)
            {
                char8 c = names[index].pointer[byte];
                if (c >= 'A' && c <= 'Z') { names[index].pointer[byte] = (char8)(c - 'A' + 'a'); }
            }
        }
        compiler_evidence_sort(names, named);
        for (u64 index = 1; success && index < named; index += 1)
        {
            success = !string_equal(names[index - 1], names[index]);
        }
        if (!success) { compiler_evidence_refuse(problem, S8("retained paths alias after case folding")); }
    }
    *output = (CompilerEvidenceBundle){.paths = paths, .count = success ? count : 0, .entries = entries, .bytes = bytes,
        .receipt_bytes = receipt_bytes, .receipt = receipt};
    return success;
}

BUSTER_GLOBAL_LOCAL CmJson compiler_evidence_json(Arena* arena, String8 bundle, String8 relative, u64 limit)
{
    String8 text = compiler_closure_read(arena, path_join(arena, bundle, relative), limit);
    CmJson result = text.length ? cm_json_parse((char const*)text.pointer, (size_t)text.length) : (CmJson){0};
    return result;
}

BUSTER_GLOBAL_LOCAL int compiler_evidence_boolean(CmJson const* json, unsigned node)
{
    char const* text = cm_value(json, node);
    int result = json->valid && node && json->tokens[node].kind == 'v' ? (cm_equal(text, "true") ? 1 : cm_equal(text, "false") ? 0 : -1) : -1;
    return result;
}

// The lab's own declared plan and per-variant availability decide which pair
// streams must exist; an unreadable or out-of-range declaration refuses.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_facts(Arena* arena, String8 bundle, CompilerEvidenceFacts* facts, String8* problem)
{
    CmJson compare = compiler_evidence_json(arena, bundle, S8("lab/compare.json"), COMPILER_EVIDENCE_JSON_LIMIT);
    unsigned plan = cm_member(&compare, 1, "plan");
    unsigned pairs = cm_member(&compare, plan, "pairs");
    u64 count = 0;
    bool success = compare.valid && plan && compare.tokens[plan].kind == 'o' && pairs && compare.tokens[pairs].kind == 'v' &&
        cm_unsigned(cm_value(&compare, pairs), &count) && count >= COMPILER_EVIDENCE_PAIRS_MIN &&
        count <= COMPILER_EVIDENCE_PAIRS_MAX && count % 2 == 0;
    cm_json_free(&compare);
    String8 variants[] = {S8("lab/a/lab.json"), S8("lab/b/lab.json")};
    for (u64 variant = 0; success && variant < 2; variant += 1)
    {
        CmJson meta = compiler_evidence_json(arena, bundle, variants[variant], COMPILER_EVIDENCE_JSON_LIMIT);
        unsigned perf = cm_member(&meta, cm_member(&meta, 1, "capabilities"), "perf_stat");
        int usable = compiler_evidence_boolean(&meta, cm_member(&meta, perf, "usable"));
        int metrics = compiler_evidence_boolean(&meta, cm_member(&meta, cm_member(&meta, 1, "collection"), "metrics_out"));
        success = meta.valid && usable >= 0 && metrics >= 0;
        facts->counters[variant] = usable == 1;
        facts->metrics[variant] = metrics == 1;
        cm_json_free(&meta);
    }
    facts->pairs = success ? count : 0;
    if (!success) { compiler_evidence_refuse(problem, S8("lab plan or per-variant counter/metrics availability is missing or malformed")); }
    return success;
}

// owned-phases/NNNN.json or one of its four siblings, with 1 <= NNNN <= phases.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_phase_member(String8 path, u64 phases)
{
    String8 prefix = S8(COMPILER_EVIDENCE_PHASE_DIRECTORY);
    bool result = path.length >= prefix.length + 9 && string_starts_with_sequence(path, prefix);
    String8 leaf = result ? (String8){.pointer = path.pointer + prefix.length, .length = path.length - prefix.length} : (String8){0};
    u64 number = 0;
    for (u64 index = 0; result && index < 4; index += 1)
    {
        result = leaf.pointer[index] >= '0' && leaf.pointer[index] <= '9';
        number = number * 10 + (u64)(leaf.pointer[index] - '0');
    }
    result = result && number >= 1 && number <= phases && memcmp(leaf.pointer + 4, ".json", 5) == 0;
    String8 suffix = result ? (String8){.pointer = leaf.pointer + 9, .length = leaf.length - 9} : (String8){0};
    bool known = false;
    for (u64 index = 0; result && !known && index < BUSTER_ARRAY_LENGTH(compiler_evidence_phase_suffixes); index += 1)
    {
        known = string_equal(suffix, compiler_evidence_phase_suffixes[index]);
    }
    return result && known;
}

// Inventory v1 obligation of one retained path, from the admitted profile, route and facts only.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_required(Arena* arena, CompilerEvidenceFacts facts, String8 path)
{
    bool result = string_equal(path, S8(COMPILER_EVIDENCE_OMISSIONS)) || (facts.phases && compiler_evidence_phase_member(path, facts.phases));
    for (u64 index = 0; facts.phases && !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_closure_members); index += 1)
    {
        result = string_equal(path, compiler_evidence_closure_members[index]);
    }
    for (u64 index = 0; !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_lab_fixed); index += 1)
    {
        result = string_equal(path, compiler_evidence_lab_fixed[index]);
    }
    for (u64 index = 0; !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_corpus_core); index += 1)
    {
        result = string_equal(path, compiler_evidence_corpus_core[index]);
    }
    for (u64 variant = 0; !result && variant < 2; variant += 1)
    {
        for (u64 index = 0; !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_lab_logs); index += 1)
        {
            result = string_equal(path, string_format(arena, S8("lab/{S8}/{S8}"), compiler_evidence_variants[variant], compiler_evidence_lab_logs[index]));
        }
    }
    for (u64 index = 0; !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_workloads); index += 1)
    {
        result = string_equal(path, string_format(arena, S8("throughput/inputs/{S8}.c"), compiler_evidence_workloads[index]));
    }
    // lab/pairs/NNNN-v.csv or .ccmetrics with 1 <= NNNN <= pairs, when that variant declares the stream.
    if (!result && string_starts_with_sequence(path, S8("lab/pairs/")))
    {
        String8 leaf = {.pointer = path.pointer + 10, .length = path.length - 10};
        u64 number = 0;
        bool digits = leaf.length > 6 && leaf.pointer[4] == '-' && (leaf.pointer[5] == 'a' || leaf.pointer[5] == 'b');
        for (u64 index = 0; digits && index < 4; index += 1)
        {
            digits = leaf.pointer[index] >= '0' && leaf.pointer[index] <= '9';
            number = number * 10 + (u64)(leaf.pointer[index] - '0');
        }
        u64 variant = digits && leaf.pointer[5] == 'b' ? 1 : 0;
        String8 extension = digits ? (String8){.pointer = leaf.pointer + 6, .length = leaf.length - 6} : (String8){0};
        result = digits && number >= 1 && number <= facts.pairs &&
            ((string_equal(extension, S8(".csv")) && facts.counters[variant]) ||
             (string_equal(extension, S8(".ccmetrics")) && facts.metrics[variant]));
    }
    return result;
}

// Every required obligation, derived independently of what the bundle contains.
BUSTER_GLOBAL_LOCAL String8List compiler_evidence_obligations(Arena* arena, CompilerEvidenceFacts facts)
{
    String8List result = {0};
    string8_list_push(arena, &result, S8(COMPILER_EVIDENCE_OMISSIONS));
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_evidence_lab_fixed); index += 1)
    {
        string8_list_push(arena, &result, compiler_evidence_lab_fixed[index]);
    }
    for (u64 variant = 0; variant < 2; variant += 1)
    {
        String8 name = compiler_evidence_variants[variant];
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_evidence_lab_logs); index += 1)
        {
            string8_list_push(arena, &result, string_format(arena, S8("lab/{S8}/{S8}"), name, compiler_evidence_lab_logs[index]));
        }
        for (u64 pair = 1; pair <= facts.pairs; pair += 1)
        {
            String8 stem = string_format(arena, S8("lab/pairs/{u64:width=[0,4]}-{S8}"), pair, name);
            if (facts.counters[variant]) { string8_list_push(arena, &result, string_format(arena, S8("{S8}.csv"), stem)); }
            if (facts.metrics[variant]) { string8_list_push(arena, &result, string_format(arena, S8("{S8}.ccmetrics"), stem)); }
        }
    }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_evidence_corpus_core); index += 1)
    {
        string8_list_push(arena, &result, compiler_evidence_corpus_core[index]);
    }
    for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_evidence_workloads); index += 1)
    {
        string8_list_push(arena, &result, string_format(arena, S8("throughput/inputs/{S8}.c"), compiler_evidence_workloads[index]));
    }
    for (u64 index = 0; facts.phases && index < BUSTER_ARRAY_LENGTH(compiler_evidence_closure_members); index += 1)
    {
        string8_list_push(arena, &result, compiler_evidence_closure_members[index]);
    }
    for (u64 phase = 1; phase <= facts.phases; phase += 1)
    {
        for (u64 index = 0; index < BUSTER_ARRAY_LENGTH(compiler_evidence_phase_suffixes); index += 1)
        {
            string8_list_push(arena, &result, string_format(arena, S8(COMPILER_EVIDENCE_PHASE_DIRECTORY "{u64:width=[0,4]}.json{S8}"),
                phase, compiler_evidence_phase_suffixes[index]));
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_evidence_reason(char const* text)
{
    bool result = false;
    for (u64 index = 0; !result && index < BUSTER_ARRAY_LENGTH(compiler_evidence_omission_reasons); index += 1)
    {
        String8 reason = compiler_evidence_omission_reasons[index];
        result = text && strlen(text) == reason.length && memcmp(text, reason.pointer, reason.length) == 0;
    }
    return result;
}

// Fixed schema, complete counts by the fixed reason enum, at most 50 examples.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_omissions_valid(Arena* arena, String8 bundle)
{
    CmJson json = compiler_evidence_json(arena, bundle, S8(COMPILER_EVIDENCE_OMISSIONS), COMPILER_EVIDENCE_OMISSIONS_LIMIT);
    u64 total = 0;
    u64 counted = 0;
    u64 keys = 0;
    bool result = json.valid && json.tokens[1].kind == 'o' && cm_equal(cm_get(&json, 1, "schema"), COMPILER_EVIDENCE_OMISSIONS_SCHEMA) &&
        cm_unsigned(cm_get(&json, 1, "total"), &total);
    for (unsigned key = result ? json.tokens[1].child : 0; result && key; )
    {
        unsigned value = json.tokens[key].next;
        keys += 1;
        result = cm_equal(json.tokens[key].text, "schema") || cm_equal(json.tokens[key].text, "total") ||
            cm_equal(json.tokens[key].text, "by_reason") || cm_equal(json.tokens[key].text, "examples");
        key = value ? json.tokens[value].next : 0;
    }
    unsigned reasons = cm_member(&json, 1, "by_reason");
    result = result && keys == 4 && reasons && json.tokens[reasons].kind == 'o';
    for (unsigned key = result ? json.tokens[reasons].child : 0; result && key; )
    {
        unsigned value = json.tokens[key].next;
        u64 count = 0;
        result = compiler_evidence_reason(json.tokens[key].text) && value && json.tokens[value].kind == 'v' &&
            cm_unsigned(cm_value(&json, value), &count) && count <= total - counted;
        counted += result ? count : 0;
        key = value ? json.tokens[value].next : 0;
    }
    unsigned examples = cm_member(&json, 1, "examples");
    u64 shown = 0;
    result = result && counted == total && examples && json.tokens[examples].kind == 'a';
    for (unsigned item = result ? json.tokens[examples].child : 0; result && item; item = json.tokens[item].next)
    {
        shown += 1;
        char const* path = cm_get(&json, item, "path");
        result = shown <= COMPILER_EVIDENCE_OMISSION_EXAMPLES && shown <= total && json.tokens[item].kind == 'o' &&
            path[0] && compiler_evidence_reason(cm_get(&json, item, "reason"));
    }
    cm_json_free(&json);
    return result;
}

BUSTER_GLOBAL_LOCAL u64 compiler_evidence_member_limit(String8 path)
{
    u64 result = string_ends_with_sequence(path, S8(".json")) ? COMPILER_EVIDENCE_JSON_LIMIT : COMPILER_EVIDENCE_MEMBER_LIMIT;
    return result;
}

// The canonical manifest of the retained bytes: identity header, counts, then
// one role/bytes/sha256/path row per non-control file in bytewise path order.
// A snapshot attempt's owned-phases/ holds exactly the trusted phase population.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_manifest(Arena* arena, String8 bundle, String8 header, u64 phases,
    CompilerEvidenceBundle* totals, String8* problem)
{
    CompilerEvidenceBundle files = {0};
    CompilerEvidenceFacts facts = {0};
    bool success = header.length && compiler_evidence_collect(arena, bundle, &files, problem) &&
        compiler_evidence_facts(arena, bundle, &facts, problem);
    facts.phases = phases;
    if (success)
    {
        success = compiler_evidence_omissions_valid(arena, bundle);
        if (!success) { compiler_evidence_refuse(problem, S8("evidence-omissions.json is missing or not the fixed bounded schema")); }
    }
    String8List obligations = success ? compiler_evidence_obligations(arena, facts) : (String8List){0};
    for (String8Node* node = obligations.first; success && node; node = node->next)
    {
        bool found = false;
        for (u64 index = 0; !found && index < files.count; index += 1) { found = string_equal(files.paths[index], node->string); }
        success = found;
        if (!success) { compiler_evidence_refuse(problem, string_format(arena, S8("required raw evidence is missing: {S8}"), node->string)); }
    }
    String8List rows = {0};
    u64 payload = 0;
    for (u64 index = 0; success && index < files.count; index += 1)
    {
        String8 path = files.paths[index];
        String8 digest = {0};
        struct stat status = {0};
        success = compiler_closure_hash(arena, path_join(arena, bundle, path), &digest, &status) &&
            (u64)status.st_size <= compiler_evidence_member_limit(path);
        bool required = success && compiler_evidence_required(arena, facts, path);
        if (success && phases && !required && string_starts_with_sequence(path, S8(COMPILER_EVIDENCE_PHASE_DIRECTORY)))
        {
            success = false;
            compiler_evidence_refuse(problem, string_format(arena, S8("owned-phase population differs from the trusted phase count: {S8}"), path));
        }
        else if (success)
        {
            payload += (u64)status.st_size;
            String8 role = required ? S8("required") : S8("optional");
            string8_list_push(arena, &rows, string_format(arena, S8("{S8}\t{u64}\t{S8}\t{S8}\n"),
                role, (u64)status.st_size, digest, path));
        }
        else
        {
            compiler_evidence_refuse(problem, string_format(arena, S8("retained member is unreadable, changed or over its limit: {S8}"), path));
        }
    }
    String8 result = {0};
    if (success)
    {
        String8 counts = string_format(arena, S8("members\t{u64}\npayload_bytes\t{u64}\nrole\tbytes\tsha256\tpath\n"), files.count, payload);
        result = string_format(arena, S8("{S8}{S8}{S8}"), header, counts, compiler_evidence_join(arena, rows));
        success = result.length <= COMPILER_EVIDENCE_MANIFEST_LIMIT;
        if (!success)
        {
            compiler_evidence_refuse(problem, S8("manifest exceeds its 4 MiB bound"));
            result = (String8){0};
        }
    }
    *totals = files;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_count_field(Arena* arena, String8 manifest, String8 key)
{
    String8 prefix = string_format(arena, S8("\n{S8}\t"), key);
    String8 result = {0};
    for (u64 index = 0; !result.length && index + prefix.length <= manifest.length; index += 1)
    {
        if (memcmp(manifest.pointer + index, prefix.pointer, prefix.length) == 0)
        {
            u64 start = index + prefix.length;
            u64 end = start;
            while (end < manifest.length && manifest.pointer[end] != '\n') { end += 1; }
            result = (String8){.pointer = manifest.pointer + start, .length = end - start};
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_seal_text(Arena* arena, String8 manifest)
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, manifest.pointer, manifest.length);
    char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
    sha256_finish_hex(&hash, digits);
    String8 result = string_format(arena, S8("schema=" COMPILER_EVIDENCE_SEAL_SCHEMA "\nstate=complete\nmanifest_bytes={u64}\n"
        "manifest_sha256={S8}\nmembers={S8}\npayload_bytes={S8}\n"), manifest.length,
        (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1},
        compiler_evidence_count_field(arena, manifest, S8("members")), compiler_evidence_count_field(arena, manifest, S8("payload_bytes")));
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_evidence_identity_file(Arena* arena, String8 path, u64* phases, String8* problem)
{
    String8 text = compiler_closure_read(arena, path, COMPILER_EVIDENCE_SEAL_LIMIT);
    return compiler_evidence_identity(arena, text, phases, problem);
}

// Write the manifest, then the seal last. Any existing control file refuses:
// a bundle is sealed once, from its final retained bytes.
// Self-test seam: a bundle-relative member rewritten after the seal is published
// and before the final pass, standing in for a concurrent change.
BUSTER_GLOBAL_LOCAL String8 compiler_evidence_seal_fault;

// The seal is the published completion marker. Before anything is written, the
// manifest, the seal and a receipt at its bound must fit the retained bounds the
// verifier enforces; any later failure removes both control files again.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_seal(Arena* arena, String8 bundle, String8 identity_path, String8* problem)
{
    String8 manifest_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), true);
    String8 seal_path = string_duplicate_arena(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_SEAL)), true);
    struct stat status = {0};
    bool fresh = lstat((char*)manifest_path.pointer, &status) != 0 && lstat((char*)seal_path.pointer, &status) != 0;
    if (!fresh) { compiler_evidence_refuse(problem, S8("bundle already carries a manifest or seal")); }
    u64 phases = 0;
    String8 header = fresh ? compiler_evidence_identity_file(arena, identity_path, &phases, problem) : (String8){0};
    CompilerEvidenceBundle totals = {0};
    String8 manifest = header.length ? compiler_evidence_manifest(arena, bundle, header, phases, &totals, problem) : (String8){0};
    String8 seal = manifest.length ? compiler_evidence_seal_text(arena, manifest) : (String8){0};
    u64 controls = manifest.length + seal.length + COMPILER_EVIDENCE_RECEIPT_LIMIT;
    bool fits = seal.length && totals.receipt_bytes <= COMPILER_EVIDENCE_RECEIPT_LIMIT &&
        totals.bytes - totals.receipt_bytes <= COMPILER_EVIDENCE_TOTAL_LIMIT - controls &&
        totals.entries + 2 + !totals.receipt <= COMPILER_EVIDENCE_ENTRY_LIMIT;
    if (seal.length && !fits) { compiler_evidence_refuse(problem, S8("manifest, seal and receipt would exceed the retained byte or entry bound")); }
    bool success = fits && production_profile_write(manifest_path, manifest) && production_profile_write(seal_path, seal);
    if (success && compiler_evidence_seal_fault.length)
    {
        production_profile_write(path_join(arena, bundle, compiler_evidence_seal_fault), S8("changed during sealing\n"));
    }
    // Refuse if the retained set changed while sealing.
    CompilerEvidenceBundle again_totals = {0};
    String8 again = success ? compiler_evidence_manifest(arena, bundle, header, phases, &again_totals, problem) : (String8){0};
    success = success && string_equal(again, manifest);
    if (fits && !success)
    {
        unlink((char*)seal_path.pointer);
        unlink((char*)manifest_path.pointer);
        compiler_evidence_refuse(problem, S8("bundle changed or could not be written while sealing"));
    }
    return success;
}

// The retained bytes must reproduce the retained manifest exactly under the
// trusted identity, and the seal must bind those manifest bytes.
BUSTER_GLOBAL_LOCAL bool compiler_evidence_verify(Arena* arena, String8 bundle, String8 identity_path, String8* problem, String8* summary)
{
    u64 phases = 0;
    String8 header = compiler_evidence_identity_file(arena, identity_path, &phases, problem);
    String8 retained = header.length ? compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_MANIFEST)), COMPILER_EVIDENCE_MANIFEST_LIMIT) : (String8){0};
    String8 seal = retained.length ? compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_SEAL)), COMPILER_EVIDENCE_SEAL_LIMIT) : (String8){0};
    if (header.length && !seal.length) { compiler_evidence_refuse(problem, S8("bundle has no bounded manifest and seal")); }
    String8 receipt = seal.length ? compiler_closure_read(arena, path_join(arena, bundle, S8(COMPILER_EVIDENCE_RECEIPT)), COMPILER_EVIDENCE_RECEIPT_LIMIT) : (String8){0};
    if (seal.length && !receipt.length) { compiler_evidence_refuse(problem, S8("final receipt is missing or exceeds its 1 MiB bound")); }
    CompilerEvidenceBundle totals = {0};
    String8 expected = receipt.length ? compiler_evidence_manifest(arena, bundle, header, phases, &totals, problem) : (String8){0};
    bool success = expected.length && string_equal(expected, retained);
    if (expected.length && !success) { compiler_evidence_refuse(problem, S8("retained bytes, roles or identity do not reproduce the sealed manifest")); }
    success = success && string_equal(seal, compiler_evidence_seal_text(arena, retained));
    if (expected.length && string_equal(expected, retained) && !success) { compiler_evidence_refuse(problem, S8("seal does not bind the retained manifest")); }
    if (success)
    {
        Sha256 hash;
        sha256_init(&hash);
        sha256_add(&hash, retained.pointer, retained.length);
        char8* digits = arena_allocate(arena, char8, SHA256_HEX_CAPACITY);
        sha256_finish_hex(&hash, digits);
        *summary = string_format(arena, S8("members={S8} payload_bytes={S8} manifest_sha256={S8}"),
            compiler_evidence_count_field(arena, retained, S8("members")), compiler_evidence_count_field(arena, retained, S8("payload_bytes")),
            (String8){.pointer = digits, .length = SHA256_HEX_CAPACITY - 1});
    }
    return success;
}

#include "compiler_evidence_test.c"
#endif

BUSTER_GLOBAL_LOCAL ProcessResult compiler_evidence_main(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
#if BUSTER_LINUX
    String8 problem = {0};
    String8 summary = {0};
    if (arguments.length == 1 && string_equal(arguments.pointer[0], S8("self-test")))
    {
        result = compiler_evidence_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (arguments.length == 3 && string_equal(arguments.pointer[0], S8("seal")))
    {
        bool sealed = compiler_evidence_seal(arena, arguments.pointer[1], arguments.pointer[2], &problem);
        if (sealed) { string_print(S8("COMPILER_EVIDENCE_SEALED\n")); }
        else { string_print(S8("COMPILER_EVIDENCE_INCOMPLETE reason={S8}\n"), problem.length ? problem : S8("unknown")); }
        result = sealed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (arguments.length == 3 && string_equal(arguments.pointer[0], S8("verify")))
    {
        bool verified = compiler_evidence_verify(arena, arguments.pointer[1], arguments.pointer[2], &problem, &summary);
        if (verified) { string_print(S8("COMPILER_EVIDENCE_VERIFIED {S8}\n"), summary); }
        else { string_print(S8("COMPILER_EVIDENCE_REFUSED reason={S8}\n"), problem.length ? problem : S8("unknown")); }
        result = verified ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else
    {
        string_print(S8("usage: compiler_evidence seal BUNDLE IDENTITY\n"
            "       compiler_evidence verify BUNDLE IDENTITY\n"
            "       compiler_evidence self-test\n"));
    }
#else
    BUSTER_UNUSED(arena);
    BUSTER_UNUSED(arguments);
    string_print(S8("error: compiler_evidence requires the approved Linux publication platform\n"));
#endif
    return result;
}
