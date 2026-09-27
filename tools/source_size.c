// Source-size report and change ratchet (#1581), included by build.c.
// source_size_main owns the command line. source_size_inventory resolves one
// Git revision and lists its tracked blobs; source_size_inventory_parse checks
// that `git ls-tree` listing, and source_size_classify gives each path the
// category of its first matching source_size_rules row, or
// SOURCE_SIZE_FALLBACK_CATEGORY. source_size_baseline_format and
// source_size_baseline_parse own the committed SOURCE_SIZE_BASELINE_PATH
// snapshot, which source_size_baseline_read takes from the measured revision
// rather than the working tree. source_size_verdict is the ratchet decision;
// source_size_report prints the human tables and the SOURCE_SIZE_*_V1
// records; source_size_self_test covers the pure pieces without running Git.
// docs/source-size.md holds the policy and how growth is acknowledged.
#define SOURCE_SIZE_BASELINE_PATH "docs/source-size-baseline.txt"
#define SOURCE_SIZE_BASELINE_SCHEMA "buster-source-size-baseline-v1"
// Growth one change may add to a ratcheted category without refreshing the
// baseline. docs/source-size.md records the calibration.
#define SOURCE_SIZE_CHANGE_LIMIT_BYTES (32ull * 1024)
#define SOURCE_SIZE_LARGEST_FILES 10
#define SOURCE_SIZE_CHANGED_FILES 40
#define SOURCE_SIZE_GIT_TIMEOUT_US (120ull * 1000000)
// Hexadecimal object-name lengths of SHA-1 and SHA-256 repositories.
#define SOURCE_SIZE_SHA1_HEX 40
#define SOURCE_SIZE_SHA256_HEX 64

typedef enum SourceSizeCategory
{
    SOURCE_SIZE_CATEGORY_PRODUCTION,
    SOURCE_SIZE_CATEGORY_BUILD,
    SOURCE_SIZE_CATEGORY_TESTS,
    SOURCE_SIZE_CATEGORY_GENERATED,
    SOURCE_SIZE_CATEGORY_DOCS,
    SOURCE_SIZE_CATEGORY_DORMANT,
    SOURCE_SIZE_CATEGORY_COUNT,
} SourceSizeCategory;

BUSTER_GLOBAL_LOCAL String8 source_size_category_names[] = {
    [SOURCE_SIZE_CATEGORY_PRODUCTION] = S8_INITIALIZER("production"),
    [SOURCE_SIZE_CATEGORY_BUILD] = S8_INITIALIZER("build"),
    [SOURCE_SIZE_CATEGORY_TESTS] = S8_INITIALIZER("tests"),
    [SOURCE_SIZE_CATEGORY_GENERATED] = S8_INITIALIZER("generated"),
    [SOURCE_SIZE_CATEGORY_DOCS] = S8_INITIALIZER("docs"),
    [SOURCE_SIZE_CATEGORY_DORMANT] = S8_INITIALIZER("dormant"),
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(source_size_category_names) == SOURCE_SIZE_CATEGORY_COUNT);

typedef enum SourceSizeMatch
{
    SOURCE_SIZE_MATCH_PATH,
    SOURCE_SIZE_MATCH_PREFIX,
    SOURCE_SIZE_MATCH_SUFFIX,
    // Any directory component equals the pattern.
    SOURCE_SIZE_MATCH_DIRECTORY,
    SOURCE_SIZE_MATCH_NAME_PREFIX,
    SOURCE_SIZE_MATCH_NAME_INFIX,
    // The file name before its first '.' is the pattern or ends in "_pattern".
    SOURCE_SIZE_MATCH_STEM_WORD,
} SourceSizeMatch;

typedef struct SourceSizeRule SourceSizeRule;
struct SourceSizeRule
{
    SourceSizeCategory category;
    SourceSizeMatch match;
    String8 pattern;
};

// First match wins. Only production and build ratchet, so the rules above
// them decide what is exempt: a path they do not claim is measured.
BUSTER_GLOBAL_LOCAL SourceSizeRule source_size_rules[] = {
    // Dormant custom-language preservation material (AGENTS.md).
    {SOURCE_SIZE_CATEGORY_DORMANT, SOURCE_SIZE_MATCH_SUFFIX, S8_INITIALIZER(".bbb")},
    {SOURCE_SIZE_CATEGORY_DORMANT, SOURCE_SIZE_MATCH_PATH, S8_INITIALIZER("DORMANT_CUSTOM_COMPILER.md")},
    // Documentation, audits and their evidence, ledgers and licenses.
    {SOURCE_SIZE_CATEGORY_DOCS, SOURCE_SIZE_MATCH_PREFIX, S8_INITIALIZER("docs/")},
    {SOURCE_SIZE_CATEGORY_DOCS, SOURCE_SIZE_MATCH_PREFIX, S8_INITIALIZER("LICENSES/")},
    {SOURCE_SIZE_CATEGORY_DOCS, SOURCE_SIZE_MATCH_SUFFIX, S8_INITIALIZER(".md")},
    // Generated tables and data.
    {SOURCE_SIZE_CATEGORY_GENERATED, SOURCE_SIZE_MATCH_DIRECTORY, S8_INITIALIZER("generated")},
    {SOURCE_SIZE_CATEGORY_GENERATED, SOURCE_SIZE_MATCH_NAME_INFIX, S8_INITIALIZER(".generated.")},
    // Tests, fixtures and test harnesses wherever they live.
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_DIRECTORY, S8_INITIALIZER("tests")},
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_DIRECTORY, S8_INITIALIZER("fixtures")},
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_NAME_PREFIX, S8_INITIALIZER("test_")},
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_STEM_WORD, S8_INITIALIZER("test")},
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_STEM_WORD, S8_INITIALIZER("tests")},
    {SOURCE_SIZE_CATEGORY_TESTS, SOURCE_SIZE_MATCH_STEM_WORD, S8_INITIALIZER("fixture")},
    // Generators and scripts nested in the source tree are tooling.
    {SOURCE_SIZE_CATEGORY_BUILD, SOURCE_SIZE_MATCH_DIRECTORY, S8_INITIALIZER("tools")},
    {SOURCE_SIZE_CATEGORY_PRODUCTION, SOURCE_SIZE_MATCH_PREFIX, S8_INITIALIZER("src/")},
};

// Everything else is repository infrastructure: build.c and its bootstrap
// wrappers, CMake, CI workflows, tools/, the mobile packaging scripts and the
// root configuration files. A new top-level location therefore ratchets
// until a rule above deliberately exempts it.
#define SOURCE_SIZE_FALLBACK_CATEGORY SOURCE_SIZE_CATEGORY_BUILD

typedef struct SourceSizeEntry SourceSizeEntry;
struct SourceSizeEntry
{
    String8 path;
    u64 bytes;
    SourceSizeCategory category;
};

typedef struct SourceSizeTotals SourceSizeTotals;
struct SourceSizeTotals
{
    u64 files[SOURCE_SIZE_CATEGORY_COUNT];
    u64 bytes[SOURCE_SIZE_CATEGORY_COUNT];
};

typedef struct SourceSizeInventory SourceSizeInventory;
struct SourceSizeInventory
{
    String8 commit;
    SourceSizeEntry* entries;
    u64 count;
    SourceSizeTotals totals;
};

typedef struct SourceSizeBaseline SourceSizeBaseline;
struct SourceSizeBaseline
{
    String8 commit;
    SourceSizeTotals totals;
    bool present;
};

typedef struct SourceSizeChange SourceSizeChange;
struct SourceSizeChange
{
    String8 path;
    SourceSizeCategory category;
    u64 before;
    u64 after;
    bool added;
    bool removed;
};

typedef enum SourceSizeVerdict
{
    SOURCE_SIZE_VERDICT_WITHIN_LIMIT,
    SOURCE_SIZE_VERDICT_ACKNOWLEDGED,
    SOURCE_SIZE_VERDICT_UNACKNOWLEDGED,
    SOURCE_SIZE_VERDICT_COUNT,
} SourceSizeVerdict;

BUSTER_GLOBAL_LOCAL String8 source_size_verdict_names[] = {
    [SOURCE_SIZE_VERDICT_WITHIN_LIMIT] = S8_INITIALIZER("within-limit"),
    [SOURCE_SIZE_VERDICT_ACKNOWLEDGED] = S8_INITIALIZER("acknowledged"),
    [SOURCE_SIZE_VERDICT_UNACKNOWLEDGED] = S8_INITIALIZER("unacknowledged"),
};
BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(source_size_verdict_names) == SOURCE_SIZE_VERDICT_COUNT);

typedef struct SourceSizeClassification SourceSizeClassification;
struct SourceSizeClassification
{
    String8 path;
    SourceSizeCategory category;
};

BUSTER_GLOBAL_LOCAL bool source_size_ratcheted(SourceSizeCategory category)
{
    bool result = category == SOURCE_SIZE_CATEGORY_PRODUCTION || category == SOURCE_SIZE_CATEGORY_BUILD;
    return result;
}

// Splits on runs of `separator`. The returned count may exceed `capacity`;
// only the first `capacity` fields are stored.
BUSTER_GLOBAL_LOCAL u64 source_size_split(String8 text, char8 separator, String8* fields, u64 capacity)
{
    u64 count = 0;
    u64 start = 0;
    for (u64 i = 0; i <= text.length; i += 1)
    {
        if (i == text.length || text.pointer[i] == separator)
        {
            if (i > start)
            {
                if (count < capacity)
                {
                    fields[count] = string_slice(text, start, i);
                }
                count += 1;
            }
            start = i + 1;
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL bool source_size_object_name(String8 name)
{
    bool result = name.length == SOURCE_SIZE_SHA1_HEX || name.length == SOURCE_SIZE_SHA256_HEX;
    for (u64 i = 0; result && i < name.length; i += 1)
    {
        char8 c = name.pointer[i];
        result = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool source_size_parse_u64(String8 text, u64* value)
{
    IntegerParsingU64 parsed = string8_parse_u64_decimal(text);
    bool result = text.length && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == text.length;
    *value = parsed.value;
    return result;
}

// Bytewise order: the order of Git's index and of a recursive ls-tree.
BUSTER_GLOBAL_LOCAL s32 source_size_path_compare(String8 left, String8 right)
{
    u64 length = left.length < right.length ? left.length : right.length;
    int order = length ? memcmp(left.pointer, right.pointer, (size_t)length) : 0;
    s32 result = order < 0 ? -1 : order > 0 ? 1 : left.length < right.length ? -1 : left.length > right.length ? 1 : 0;
    return result;
}

BUSTER_GLOBAL_LOCAL SourceSizeCategory source_size_classify(String8 path)
{
    u64 slash = BUSTER_STRING_NO_MATCH;
    for (u64 i = 0; i < path.length; i += 1)
    {
        if (path.pointer[i] == '/')
        {
            slash = i;
        }
    }
    String8 directories = slash == BUSTER_STRING_NO_MATCH ? (String8){0} : string_slice(path, 0, slash + 1);
    String8 name = slash == BUSTER_STRING_NO_MATCH ? path : string_slice(path, slash + 1, path.length);
    u64 dot = string_first_code_unit(name, '.');
    String8 stem = dot == BUSTER_STRING_NO_MATCH ? name : string_slice(name, 0, dot);
    SourceSizeCategory result = SOURCE_SIZE_FALLBACK_CATEGORY;
    bool matched = false;
    for (u64 rule_index = 0; !matched && rule_index < BUSTER_ARRAY_LENGTH(source_size_rules); rule_index += 1)
    {
        SourceSizeRule rule = source_size_rules[rule_index];
        switch (rule.match)
        {
        case SOURCE_SIZE_MATCH_PATH:
            matched = string_equal(path, rule.pattern);
            break;
        case SOURCE_SIZE_MATCH_PREFIX:
            matched = string_starts_with_sequence(path, rule.pattern);
            break;
        case SOURCE_SIZE_MATCH_SUFFIX:
            matched = string_ends_with_sequence(path, rule.pattern);
            break;
        case SOURCE_SIZE_MATCH_DIRECTORY:
        {
            u64 start = 0;
            for (u64 i = 0; !matched && i < directories.length; i += 1)
            {
                if (directories.pointer[i] == '/')
                {
                    matched = string_equal(string_slice(directories, start, i), rule.pattern);
                    start = i + 1;
                }
            }
        }
        break;
        case SOURCE_SIZE_MATCH_NAME_PREFIX:
            matched = string_starts_with_sequence(name, rule.pattern);
            break;
        case SOURCE_SIZE_MATCH_NAME_INFIX:
            matched = string_first_sequence(name, rule.pattern) != BUSTER_STRING_NO_MATCH;
            break;
        case SOURCE_SIZE_MATCH_STEM_WORD:
            matched = string_equal(stem, rule.pattern) ||
                (stem.length > rule.pattern.length && stem.pointer[stem.length - rule.pattern.length - 1] == '_' &&
                 string_ends_with_sequence(stem, rule.pattern));
            break;
        }
        if (matched)
        {
            result = rule.category;
        }
    }
    return result;
}

// `git ls-tree -r -l -z` records are "<mode> <type> <object> <padded size>\t
// <path>\0". Paths must arrive strictly increasing: source_size_changes merges
// two listings in one pass, and a duplicate would be counted twice.
BUSTER_GLOBAL_LOCAL bool source_size_inventory_parse(Arena* arena, String8 listing, SourceSizeInventory* inventory)
{
    u64 capacity = 1;
    for (u64 i = 0; i < listing.length; i += 1)
    {
        capacity += listing.pointer[i] == 0;
    }
    inventory->entries = arena_allocate(arena, SourceSizeEntry, capacity);
    inventory->count = 0;
    inventory->totals = (SourceSizeTotals){0};
    bool result = !listing.length || listing.pointer[listing.length - 1] == 0;
    u64 start = 0;
    for (u64 i = 0; result && i < listing.length; i += 1)
    {
        if (listing.pointer[i] == 0)
        {
            String8 record = string_slice(listing, start, i);
            start = i + 1;
            u64 tab = string_first_code_unit(record, '\t');
            String8 fields[4];
            result = tab != BUSTER_STRING_NO_MATCH && tab + 1 < record.length &&
                source_size_split(string_slice(record, 0, tab), ' ', fields, BUSTER_ARRAY_LENGTH(fields)) == BUSTER_ARRAY_LENGTH(fields);
            if (result && string_equal(fields[1], S8("blob")))
            {
                String8 path = string_slice(record, tab + 1, record.length);
                u64 bytes = 0;
                result = source_size_object_name(fields[2]) && source_size_parse_u64(fields[3], &bytes) &&
                    (!inventory->count || source_size_path_compare(inventory->entries[inventory->count - 1].path, path) < 0);
                if (result)
                {
                    SourceSizeCategory category = source_size_classify(path);
                    inventory->entries[inventory->count] = (SourceSizeEntry){.path = path, .bytes = bytes, .category = category};
                    inventory->count += 1;
                    inventory->totals.files[category] += 1;
                    inventory->totals.bytes[category] += bytes;
                }
            }
            else if (result)
            {
                // A gitlink names another repository's commit and has no bytes here.
                result = string_equal(fields[1], S8("commit"));
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool source_size_git(Arena* arena, SliceString8 arguments, String8* output)
{
    bool result = false;
    ProcessSpawnResult spawn = os_process_spawn(arguments, (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR), .use_process_environment = 1, .search_path = 1});
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, SOURCE_SIZE_GIT_TIMEOUT_US);
        result = wait.result == PROCESS_RESULT_SUCCESS && wait.platform_status == 0 && !wait.timed_out && wait.dropped_total == 0;
        *output = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]);
        String8 error = build_compiler_output_trim(BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_ERROR]));
        if (!result && error.length)
        {
            string_print(S8("error: source_size: git {S8}: {S8}\n"), arguments.pointer[1], error);
        }
    }
    else
    {
        string_print(S8("error: source_size: could not start git\n"));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool source_size_inventory(Arena* arena, String8 revision, SourceSizeInventory* inventory)
{
    String8 commit = {0};
    String8 resolve[] = {S8("git"), S8("rev-parse"), S8("--verify"), S8("--quiet"), string_format(arena, S8("{S8}{S8}"), revision, S8("^{commit}"))};
    bool result = source_size_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(resolve), &commit);
    commit = build_compiler_output_trim(commit);
    result = result && source_size_object_name(commit);
    if (result)
    {
        String8 listing = {0};
        String8 list[] = {S8("git"), S8("ls-tree"), S8("-r"), S8("-l"), S8("-z"), S8("--full-tree"), commit};
        inventory->commit = commit;
        result = source_size_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(list), &listing) && source_size_inventory_parse(arena, listing, inventory);
        if (!result)
        {
            string_print(S8("error: source_size: could not list the tracked files of {S8}\n"), commit);
        }
    }
    else
    {
        string_print(S8("error: source_size: {S8} does not name a commit\n"), revision);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 source_size_baseline_format(Arena* arena, String8 commit, SourceSizeTotals totals)
{
    String8List lines = {0};
    string8_list_push(arena, &lines, S8("# Source-size baseline: rows are category, tracked files, bytes.\n"
                                        "# Regenerate with ./build.sh source_size --write-baseline; see docs/source-size.md.\n"
                                        "schema " SOURCE_SIZE_BASELINE_SCHEMA "\n"));
    string8_list_push(arena, &lines, string_format(arena, S8("commit {S8}\n"), commit));
    for (u64 category = 0; category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        string8_list_push(arena, &lines, string_format(arena, S8("{S8} {u64} {u64}\n"), source_size_category_names[category], totals.files[category],
            totals.bytes[category]));
    }
    String8 result = string_join_arena(arena, string8_list_to_slice(arena, lines), false);
    return result;
}

BUSTER_GLOBAL_LOCAL bool source_size_baseline_parse(String8 text, SourceSizeBaseline* baseline)
{
    *baseline = (SourceSizeBaseline){0};
    bool seen[SOURCE_SIZE_CATEGORY_COUNT] = {0};
    bool schema = false;
    bool result = text.length && text.pointer[text.length - 1] == '\n';
    u64 start = 0;
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        if (text.pointer[i] == '\n')
        {
            String8 line = string_slice(text, start, i);
            start = i + 1;
            String8 fields[3];
            u64 count = line.length && line.pointer[0] != '#' ? source_size_split(line, ' ', fields, BUSTER_ARRAY_LENGTH(fields)) : 0;
            if (count == 2 && string_equal(fields[0], S8("schema")))
            {
                result = !schema && string_equal(fields[1], S8(SOURCE_SIZE_BASELINE_SCHEMA));
                schema = true;
            }
            else if (count == 2 && string_equal(fields[0], S8("commit")))
            {
                result = !baseline->commit.length && source_size_object_name(fields[1]);
                baseline->commit = fields[1];
            }
            else if (count == 3)
            {
                u64 category = 0;
                while (category < SOURCE_SIZE_CATEGORY_COUNT && !string_equal(fields[0], source_size_category_names[category]))
                {
                    category += 1;
                }
                result = category < SOURCE_SIZE_CATEGORY_COUNT && !seen[category] && source_size_parse_u64(fields[1], &baseline->totals.files[category]) &&
                    source_size_parse_u64(fields[2], &baseline->totals.bytes[category]);
                if (result)
                {
                    seen[category] = true;
                }
            }
            else
            {
                result = count == 0;
            }
        }
    }
    result = result && schema && baseline->commit.length;
    for (u64 category = 0; result && category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        result = seen[category];
    }
    baseline->present = result;
    return result;
}

// Reads the baseline recorded in the measured revision, never the working
// tree, so a report is a function of its revisions alone. An absent file
// succeeds with present unset; a present file must be read and parse.
BUSTER_GLOBAL_LOCAL bool source_size_baseline_read(Arena* arena, SourceSizeInventory* inventory, SourceSizeBaseline* baseline)
{
    bool found = false;
    for (u64 i = 0; !found && i < inventory->count; i += 1)
    {
        found = string_equal(inventory->entries[i].path, S8(SOURCE_SIZE_BASELINE_PATH));
    }
    *baseline = (SourceSizeBaseline){0};
    bool result = !found;
    if (found)
    {
        String8 text = {0};
        String8 arguments[] = {S8("git"), S8("cat-file"), S8("blob"), string_format(arena, S8("{S8}:" SOURCE_SIZE_BASELINE_PATH), inventory->commit)};
        result = source_size_git(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), &text) && source_size_baseline_parse(text, baseline);
    }
    return result;
}

// A change may grow a ratcheted category by SOURCE_SIZE_CHANGE_LIMIT_BYTES.
// Beyond that it must also change that category's committed baseline row,
// which is the reviewable acknowledgement. Comparing rows rather than exact
// totals keeps the acknowledgement valid after a rebase or a merge-queue
// merge. A base without a baseline predates the ratchet and acknowledges.
BUSTER_GLOBAL_LOCAL SourceSizeVerdict source_size_verdict(u64 base_bytes, u64 head_bytes, SourceSizeBaseline base_baseline, SourceSizeBaseline head_baseline,
                                                         SourceSizeCategory category)
{
    bool acknowledged = !base_baseline.present || base_baseline.totals.bytes[category] != head_baseline.totals.bytes[category] ||
        base_baseline.totals.files[category] != head_baseline.totals.files[category];
    SourceSizeVerdict result = head_bytes <= base_bytes + SOURCE_SIZE_CHANGE_LIMIT_BYTES ? SOURCE_SIZE_VERDICT_WITHIN_LIMIT
        : acknowledged ? SOURCE_SIZE_VERDICT_ACKNOWLEDGED : SOURCE_SIZE_VERDICT_UNACKNOWLEDGED;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 source_size_signed(Arena* arena, u64 after, u64 before)
{
    String8 result = after > before ? string_format(arena, S8("+{u64}"), after - before)
        : after < before ? string_format(arena, S8("-{u64}"), before - after) : S8("0");
    return result;
}

BUSTER_GLOBAL_LOCAL String8 source_size_column(Arena* arena, String8 text, u64 width, bool right_align)
{
    u64 padding = text.length < width ? width - text.length : 0;
    String8 result = {.pointer = arena_allocate(arena, char8, text.length + padding + 1), .length = text.length + padding};
    memset(result.pointer, ' ', (size_t)result.length);
    if (text.length)
    {
        memcpy(result.pointer + (right_align ? padding : 0), text.pointer, (size_t)text.length);
    }
    return result;
}

// Keeps `indices` ordered by descending key; equal keys keep arrival order.
BUSTER_GLOBAL_LOCAL void source_size_rank(u64* indices, u64* keys, u64* count, u64 capacity, u64 index, u64 key)
{
    u64 position = *count;
    while (position > 0 && keys[position - 1] < key)
    {
        position -= 1;
    }
    if (position < capacity)
    {
        u64 last = *count < capacity ? *count : capacity - 1;
        for (u64 i = last; i > position; i -= 1)
        {
            indices[i] = indices[i - 1];
            keys[i] = keys[i - 1];
        }
        indices[position] = index;
        keys[position] = key;
        *count += *count < capacity;
    }
}

// Merges two bytewise-ordered inventories into the ratcheted files whose
// presence or size differs. A rename is a removal plus an addition.
BUSTER_GLOBAL_LOCAL u64 source_size_changes(Arena* arena, SourceSizeInventory* base, SourceSizeInventory* head, SourceSizeChange** changes)
{
    *changes = arena_allocate(arena, SourceSizeChange, base->count + head->count + 1);
    u64 count = 0;
    u64 base_index = 0;
    u64 head_index = 0;
    while (base_index < base->count || head_index < head->count)
    {
        s32 order = base_index == base->count ? 1 : head_index == head->count ? -1
            : source_size_path_compare(base->entries[base_index].path, head->entries[head_index].path);
        SourceSizeChange change = {.added = order > 0, .removed = order < 0};
        if (order <= 0)
        {
            change.path = base->entries[base_index].path;
            change.category = base->entries[base_index].category;
            change.before = base->entries[base_index].bytes;
            base_index += 1;
        }
        if (order >= 0)
        {
            change.path = head->entries[head_index].path;
            change.category = head->entries[head_index].category;
            change.after = head->entries[head_index].bytes;
            head_index += 1;
        }
        if (source_size_ratcheted(change.category) && (change.added || change.removed || change.before != change.after))
        {
            (*changes)[count] = change;
            count += 1;
        }
    }
    return count;
}

BUSTER_GLOBAL_LOCAL void source_size_row(Arena* arena, String8 name, String8 ratchet, String8 files, String8 bytes, String8 base_change, String8 since_baseline)
{
    string_print(S8("{S8}{S8}{S8}{S8}{S8}{S8}\n"), source_size_column(arena, name, 12, false), source_size_column(arena, ratchet, 8, false),
        source_size_column(arena, files, 7, true), source_size_column(arena, bytes, 12, true), source_size_column(arena, base_change, 16, true),
        source_size_column(arena, since_baseline, 16, true));
}

// Prints the report and returns whether every ratcheted category passes. A
// null base prints totals only and evaluates no ratchet.
BUSTER_GLOBAL_LOCAL bool source_size_report(Arena* arena, SourceSizeInventory* head, SourceSizeInventory* base, SourceSizeBaseline baseline,
                                            SourceSizeBaseline base_baseline)
{
    SourceSizeTotals empty = {0};
    SourceSizeTotals* before = base ? &base->totals : &empty;
    String8 base_commit = base ? base->commit : S8("-");
    string_print(S8("Source size of {S8}\n  base {S8}\n  baseline {S8} (" SOURCE_SIZE_BASELINE_PATH ")\n\n"), head->commit, base_commit, baseline.commit);
    source_size_row(arena, S8("category"), S8("ratchet"), S8("files"), S8("bytes"), S8("change vs base"), S8("since baseline"));
    u64 total_files = 0;
    u64 total_bytes = 0;
    u64 total_before = 0;
    u64 total_baseline = 0;
    for (u64 category = 0; category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        u64 bytes = head->totals.bytes[category];
        total_files += head->totals.files[category];
        total_bytes += bytes;
        total_before += before->bytes[category];
        total_baseline += baseline.totals.bytes[category];
        source_size_row(arena, source_size_category_names[category], source_size_ratcheted((SourceSizeCategory)category) ? S8("yes") : S8("no"),
            string_format(arena, S8("{u64}"), head->totals.files[category]), string_format(arena, S8("{u64}"), bytes),
            base ? source_size_signed(arena, bytes, before->bytes[category]) : S8("-"), source_size_signed(arena, bytes, baseline.totals.bytes[category]));
    }
    source_size_row(arena, S8("total"), S8(""), string_format(arena, S8("{u64}"), total_files), string_format(arena, S8("{u64}"), total_bytes),
        base ? source_size_signed(arena, total_bytes, total_before) : S8("-"), source_size_signed(arena, total_bytes, total_baseline));

    u64 largest[SOURCE_SIZE_LARGEST_FILES];
    u64 largest_bytes[SOURCE_SIZE_LARGEST_FILES];
    u64 largest_count = 0;
    for (u64 i = 0; i < head->count; i += 1)
    {
        if (source_size_ratcheted(head->entries[i].category))
        {
            source_size_rank(largest, largest_bytes, &largest_count, SOURCE_SIZE_LARGEST_FILES, i, head->entries[i].bytes);
        }
    }
    string_print(S8("\nLargest ratcheted files:\n"));
    for (u64 i = 0; i < largest_count; i += 1)
    {
        SourceSizeEntry entry = head->entries[largest[i]];
        string_print(S8("  {S8}{S8}  {S8}\n"), source_size_column(arena, source_size_category_names[entry.category], 12, false),
            source_size_column(arena, string_format(arena, S8("{u64}"), entry.bytes), 10, true), entry.path);
    }

    bool result = true;
    SourceSizeVerdict verdicts[SOURCE_SIZE_CATEGORY_COUNT] = {0};
    if (base)
    {
        SourceSizeChange* changes = 0;
        u64 change_count = source_size_changes(arena, base, head, &changes);
        u64 shown[SOURCE_SIZE_CHANGED_FILES];
        u64 shown_distance[SOURCE_SIZE_CHANGED_FILES];
        u64 shown_count = 0;
        for (u64 i = 0; i < change_count; i += 1)
        {
            u64 distance = changes[i].after > changes[i].before ? changes[i].after - changes[i].before : changes[i].before - changes[i].after;
            source_size_rank(shown, shown_distance, &shown_count, SOURCE_SIZE_CHANGED_FILES, i, distance);
        }
        string_print(S8("\nRatcheted files changed from base: {u64}\n"), change_count);
        for (u64 i = 0; i < shown_count; i += 1)
        {
            SourceSizeChange change = changes[shown[i]];
            string_print(S8("  {S8}{S8}  {S8}{S8}\n"), source_size_column(arena, source_size_category_names[change.category], 12, false),
                source_size_column(arena, source_size_signed(arena, change.after, change.before), 10, true), change.path,
                change.added ? S8(" (added)") : change.removed ? S8(" (removed)") : S8(""));
        }
        if (change_count > shown_count)
        {
            string_print(S8("  ... and {u64} smaller changes\n"), change_count - shown_count);
        }

        string_print(S8("\n"));
        for (u64 category = 0; category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
        {
            if (source_size_ratcheted((SourceSizeCategory)category))
            {
                u64 bytes = head->totals.bytes[category];
                verdicts[category] = source_size_verdict(before->bytes[category], bytes, base_baseline, baseline, (SourceSizeCategory)category);
                String8 name = source_size_category_names[category];
                String8 growth = source_size_signed(arena, bytes, before->bytes[category]);
                switch (verdicts[category])
                {
                case SOURCE_SIZE_VERDICT_WITHIN_LIMIT:
                    string_print(S8("{S8}: {S8} bytes from base, within the {u64}-byte change limit\n"), name, growth, SOURCE_SIZE_CHANGE_LIMIT_BYTES);
                    break;
                case SOURCE_SIZE_VERDICT_ACKNOWLEDGED:
                    string_print(S8("{S8}: {S8} bytes from base exceeds the {u64}-byte change limit; acknowledged {S8}\n"), name, growth,
                        SOURCE_SIZE_CHANGE_LIMIT_BYTES, base_baseline.present ? S8("by this change's baseline row") : S8("because the base has no baseline"));
                    break;
                case SOURCE_SIZE_VERDICT_UNACKNOWLEDGED:
                    string_print(S8("error: {S8}: {S8} bytes from base exceeds the {u64}-byte change limit and its baseline row is unchanged.\n"
                                    "  Acknowledge it: run ./build.sh source_size --write-baseline, commit " SOURCE_SIZE_BASELINE_PATH ",\n"
                                    "  and explain the growth in the change description (docs/source-size.md).\n"),
                        name, growth, SOURCE_SIZE_CHANGE_LIMIT_BYTES);
                    result = false;
                    break;
                case SOURCE_SIZE_VERDICT_COUNT:
                    BUSTER_UNREACHABLE();
                    break;
                }
            }
        }
    }

    string_print(S8("\n"));
    for (u64 category = 0; category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        string_print(S8("SOURCE_SIZE_V1 revision={S8} category={S8} ratcheted={u32} files={u64} bytes={u64} base_files={S8} base_bytes={S8} "
                        "baseline_files={u64} baseline_bytes={u64}\n"),
            head->commit, source_size_category_names[category], (u32)source_size_ratcheted((SourceSizeCategory)category), head->totals.files[category],
            head->totals.bytes[category], base ? string_format(arena, S8("{u64}"), before->files[category]) : S8("-"),
            base ? string_format(arena, S8("{u64}"), before->bytes[category]) : S8("-"), baseline.totals.files[category], baseline.totals.bytes[category]);
    }
    for (u64 category = 0; base && category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        if (source_size_ratcheted((SourceSizeCategory)category))
        {
            string_print(S8("SOURCE_SIZE_RATCHET_V1 category={S8} growth={S8} limit={u64} verdict={S8}\n"), source_size_category_names[category],
                source_size_signed(arena, head->totals.bytes[category], before->bytes[category]), SOURCE_SIZE_CHANGE_LIMIT_BYTES,
                source_size_verdict_names[verdicts[category]]);
        }
    }
    string_print(S8("SOURCE_SIZE_RESULT_V1 revision={S8} base={S8} baseline_commit={S8} status={S8}\n"), head->commit, base_commit, baseline.commit,
        result ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL void source_size_expect(bool condition, String8 name, u64* checks, u64* failures)
{
    *checks += 1;
    if (!condition)
    {
        *failures += 1;
        string_print(S8("SOURCE_SIZE_SELF_TEST_FAILURE {S8}\n"), name);
    }
}

BUSTER_GLOBAL_LOCAL bool source_size_self_test(Arena* arena)
{
    u64 checks = 0;
    u64 failures = 0;
    SourceSizeClassification classifications[] = {
        {S8_INITIALIZER("src/buster/lib/compiler/frontend/c/c_gen.c"), SOURCE_SIZE_CATEGORY_PRODUCTION},
        {S8_INITIALIZER("src/buster/lib/shaders/rect.slang"), SOURCE_SIZE_CATEGORY_PRODUCTION},
        {S8_INITIALIZER("src/buster/apps/ide/ide.c"), SOURCE_SIZE_CATEGORY_PRODUCTION},
        {S8_INITIALIZER("src/buster/lib/latest.c"), SOURCE_SIZE_CATEGORY_PRODUCTION},
        {S8_INITIALIZER("src/buster/lib/generator.c"), SOURCE_SIZE_CATEGORY_PRODUCTION},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/generated/x86_64-xed.jsonl"), SOURCE_SIZE_CATEGORY_GENERATED},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/generated/x86_64-assembly.generated.h"), SOURCE_SIZE_CATEGORY_GENERATED},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/generated/README.md"), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/aarch64_system_semantics.md"), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/tools/arm_a64_semantic_generate.py"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("src/buster/tests/compiler/driver/driver_test.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tests/c_abi_main_generated.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tests/custom/basic.bbb"), SOURCE_SIZE_CATEGORY_DORMANT},
        {S8_INITIALIZER("DORMANT_CUSTOM_COMPILER.md"), SOURCE_SIZE_CATEGORY_DORMANT},
        {S8_INITIALIZER("AGENTS.md"), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER("LICENSES/MIT.txt"), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER("docs/native-retirement-support-v1.tsv"), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER(SOURCE_SIZE_BASELINE_PATH), SOURCE_SIZE_CATEGORY_DOCS},
        {S8_INITIALIZER("build.c"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("build.sh"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("CMakeLists.txt"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER(".github/workflows/ci.yml"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER(".gitattributes"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("cmake/superbuild/CMakeLists.txt"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("ios/launch_simulator.sh"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("tools/source_size.c"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("tools/contest.py"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("new_location/module.c"), SOURCE_SIZE_CATEGORY_BUILD},
        {S8_INITIALIZER("tools/native_retirement_dependency_binding.generated.h"), SOURCE_SIZE_CATEGORY_GENERATED},
        {S8_INITIALIZER("tools/merge_queue_admission_test.py"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/bench_service/tests.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/bench_service/export_tests.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/throughput/qualification_test.h"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/test_mach_o_unwind_lld.sh"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/fixtures/atomic_or_overlap.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/clang_analyze_fixture.c"), SOURCE_SIZE_CATEGORY_TESTS},
        {S8_INITIALIZER("tools/throughput/README.md"), SOURCE_SIZE_CATEGORY_DOCS},
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(classifications); i += 1)
    {
        source_size_expect(source_size_classify(classifications[i].path) == classifications[i].category,
            string_format(arena, S8("classify:{S8}"), classifications[i].path), &checks, &failures);
    }

    SourceSizeInventory inventory = {0};
    String8 object = S8("0123456789abcdef0123456789abcdef01234567");
    String8 listing = string_format(arena, S8("100644 blob {S8}      12\tb.c{S8}100644 blob {S8}       7\tsrc/x.c{S8}160000 commit {S8}       -\tvendor{S8}"),
        object, S8("\0"), object, S8("\0"), object, S8("\0"));
    bool parsed = source_size_inventory_parse(arena, listing, &inventory);
    source_size_expect(parsed && inventory.count == 2 && inventory.totals.bytes[SOURCE_SIZE_CATEGORY_BUILD] == 12 &&
        inventory.totals.files[SOURCE_SIZE_CATEGORY_PRODUCTION] == 1 && inventory.totals.bytes[SOURCE_SIZE_CATEGORY_PRODUCTION] == 7,
        S8("listing:valid"), &checks, &failures);
    source_size_expect(source_size_inventory_parse(arena, (String8){0}, &inventory) && inventory.count == 0, S8("listing:empty"), &checks, &failures);
    String8 invalid_listings[] = {
        string_format(arena, S8("100644 blob {S8} 7\tsrc/x.c{S8}100644 blob {S8} 12\tb.c{S8}"), object, S8("\0"), object, S8("\0")),
        string_format(arena, S8("100644 blob {S8} 7\tb.c{S8}100644 blob {S8} 7\tb.c{S8}"), object, S8("\0"), object, S8("\0")),
        string_format(arena, S8("100644 blob {S8} 7\tb.c"), object),
        string_format(arena, S8("100644 blob {S8} 7x\tb.c{S8}"), object, S8("\0")),
        string_format(arena, S8("100644 blob {S8}\tb.c{S8}"), object, S8("\0")),
        string_format(arena, S8("100644 blob {S8} 7\t{S8}"), object, S8("\0")),
        string_format(arena, S8("100644 tree {S8} -\tsrc{S8}"), object, S8("\0")),
        string_format(arena, S8("100644 blob 0123 7\tb.c{S8}"), S8("\0")),
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid_listings); i += 1)
    {
        source_size_expect(!source_size_inventory_parse(arena, invalid_listings[i], &inventory), string_format(arena, S8("listing:invalid-{u64}"), i),
            &checks, &failures);
    }

    SourceSizeTotals totals = {0};
    for (u64 category = 0; category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        totals.files[category] = category + 1;
        totals.bytes[category] = 1000 * (category + 1);
    }
    SourceSizeBaseline baseline = {0};
    String8 text = source_size_baseline_format(arena, object, totals);
    bool round_trip = source_size_baseline_parse(text, &baseline) && baseline.present && string_equal(baseline.commit, object);
    for (u64 category = 0; round_trip && category < SOURCE_SIZE_CATEGORY_COUNT; category += 1)
    {
        round_trip = baseline.totals.files[category] == totals.files[category] && baseline.totals.bytes[category] == totals.bytes[category];
    }
    source_size_expect(round_trip, S8("baseline:round-trip"), &checks, &failures);
    String8 schema = S8("schema " SOURCE_SIZE_BASELINE_SCHEMA "\n");
    String8 commit = string_format(arena, S8("commit {S8}\n"), object);
    String8 rows = S8("production 1 1\nbuild 1 1\ntests 1 1\ngenerated 1 1\ndocs 1 1\ndormant 1 1\n");
    source_size_expect(source_size_baseline_parse(string_format(arena, S8("# note\n\n{S8}{S8}{S8}"), schema, commit, rows), &baseline), S8("baseline:minimal"),
        &checks, &failures);
    String8 invalid_baselines[] = {
        string_format(arena, S8("{S8}{S8}"), commit, rows),
        string_format(arena, S8("schema buster-source-size-baseline-v0\n{S8}{S8}"), commit, rows),
        string_format(arena, S8("{S8}{S8}{S8}{S8}"), schema, schema, commit, rows),
        string_format(arena, S8("{S8}{S8}"), schema, rows),
        string_format(arena, S8("{S8}commit 0123\n{S8}"), schema, rows),
        string_format(arena, S8("{S8}{S8}{S8}build 1 1\n"), schema, commit, rows),
        string_format(arena, S8("{S8}{S8}production 1 1\nbuild 1 1\n"), schema, commit),
        string_format(arena, S8("{S8}{S8}{S8}vendored 1 1\n"), schema, commit, rows),
        string_format(arena, S8("{S8}{S8}{S8}total 7\n"), schema, commit, rows),
        string_format(arena, S8("{S8}{S8}production x 1\nbuild 1 1\ntests 1 1\ngenerated 1 1\ndocs 1 1\ndormant 1 1\n"), schema, commit),
        string_format(arena, S8("{S8}{S8}{S8}dormant 1 1"), schema, commit, S8("production 1 1\nbuild 1 1\ntests 1 1\ngenerated 1 1\ndocs 1 1\n")),
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(invalid_baselines); i += 1)
    {
        source_size_expect(!source_size_baseline_parse(invalid_baselines[i], &baseline) && !baseline.present,
            string_format(arena, S8("baseline:invalid-{u64}"), i), &checks, &failures);
    }

    SourceSizeBaseline recorded = {.present = true, .totals = totals};
    SourceSizeBaseline refreshed = recorded;
    refreshed.totals.bytes[SOURCE_SIZE_CATEGORY_BUILD] += 1;
    SourceSizeBaseline absent = {0};
    u64 base_bytes = 5000;
    u64 over = base_bytes + SOURCE_SIZE_CHANGE_LIMIT_BYTES + 1;
    source_size_expect(source_size_verdict(base_bytes, base_bytes + SOURCE_SIZE_CHANGE_LIMIT_BYTES, recorded, recorded, SOURCE_SIZE_CATEGORY_BUILD) ==
        SOURCE_SIZE_VERDICT_WITHIN_LIMIT, S8("verdict:at-limit"), &checks, &failures);
    source_size_expect(source_size_verdict(base_bytes, 1, recorded, recorded, SOURCE_SIZE_CATEGORY_BUILD) == SOURCE_SIZE_VERDICT_WITHIN_LIMIT,
        S8("verdict:shrink"), &checks, &failures);
    source_size_expect(source_size_verdict(base_bytes, over, recorded, recorded, SOURCE_SIZE_CATEGORY_BUILD) == SOURCE_SIZE_VERDICT_UNACKNOWLEDGED,
        S8("verdict:over-limit"), &checks, &failures);
    source_size_expect(source_size_verdict(base_bytes, over, recorded, refreshed, SOURCE_SIZE_CATEGORY_PRODUCTION) == SOURCE_SIZE_VERDICT_UNACKNOWLEDGED,
        S8("verdict:other-row-refreshed"), &checks, &failures);
    source_size_expect(source_size_verdict(base_bytes, over, recorded, refreshed, SOURCE_SIZE_CATEGORY_BUILD) == SOURCE_SIZE_VERDICT_ACKNOWLEDGED,
        S8("verdict:row-refreshed"), &checks, &failures);
    source_size_expect(source_size_verdict(base_bytes, over, absent, recorded, SOURCE_SIZE_CATEGORY_BUILD) == SOURCE_SIZE_VERDICT_ACKNOWLEDGED,
        S8("verdict:no-base-baseline"), &checks, &failures);

    SourceSizeInventory before = {0};
    SourceSizeInventory after = {0};
    String8 before_listing = string_format(arena, S8("100644 blob {S8} 10\tbuild.c{S8}100644 blob {S8} 5\tdocs/a.md{S8}100644 blob {S8} 9\tsrc/old.c{S8}"),
        object, S8("\0"), object, S8("\0"), object, S8("\0"));
    String8 after_listing = string_format(arena, S8("100644 blob {S8} 12\tbuild.c{S8}100644 blob {S8} 6\tdocs/a.md{S8}100644 blob {S8} 0\tsrc/new.c{S8}"),
        object, S8("\0"), object, S8("\0"), object, S8("\0"));
    SourceSizeChange* changes = 0;
    bool merged = source_size_inventory_parse(arena, before_listing, &before) && source_size_inventory_parse(arena, after_listing, &after);
    u64 change_count = merged ? source_size_changes(arena, &before, &after, &changes) : 0;
    source_size_expect(merged && change_count == 3 && string_equal(changes[0].path, S8("build.c")) && changes[0].before == 10 && changes[0].after == 12 &&
        string_equal(changes[1].path, S8("src/new.c")) && changes[1].added && string_equal(changes[2].path, S8("src/old.c")) && changes[2].removed,
        S8("changes:merge"), &checks, &failures);

    u64 ranked[3];
    u64 keys[3];
    u64 ranked_count = 0;
    u64 samples[] = {5, 9, 5, 1, 9, 7};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(samples); i += 1)
    {
        source_size_rank(ranked, keys, &ranked_count, BUSTER_ARRAY_LENGTH(ranked), i, samples[i]);
    }
    source_size_expect(ranked_count == 3 && ranked[0] == 1 && ranked[1] == 4 && ranked[2] == 5, S8("rank:stable-top"), &checks, &failures);

    bool result = failures == 0;
    string_print(S8("SOURCE_SIZE_SELF_TEST checks={u64} failures={u64} status={S8}\n"), checks, failures, result ? S8("pass") : S8("fail"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult source_size_main(Arena* arena, SliceString8 arguments)
{
    String8 revision = S8("HEAD");
    String8 base_revision = {0};
    bool write_baseline = false;
    bool self_test = false;
    bool valid = true;
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        bool has_value = i + 1 < arguments.length;
        if (string_equal(argument, S8("--rev")) && has_value)
        {
            i += 1;
            revision = arguments.pointer[i];
        }
        else if (string_equal(argument, S8("--base")) && has_value)
        {
            i += 1;
            base_revision = arguments.pointer[i];
        }
        else if (string_equal(argument, S8("--write-baseline")))
        {
            write_baseline = true;
        }
        else if (string_equal(argument, S8("--self-test")))
        {
            self_test = true;
        }
        else
        {
            valid = false;
        }
    }
    // A revision is passed to Git as an argument, so it may not read as an option.
    valid = valid && revision.length && revision.pointer[0] != '-' && (!base_revision.length || base_revision.pointer[0] != '-') &&
        !(write_baseline && base_revision.length) && !(self_test && arguments.length != 1);
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (!valid)
    {
        string_print(S8("usage: ./build.sh source_size [--rev <revision>] [--base <revision>]\n"
                        "       ./build.sh source_size [--rev <revision>] --write-baseline\n"
                        "       ./build.sh source_size --self-test\n"));
    }
    else if (self_test)
    {
        result = source_size_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else
    {
        SourceSizeInventory head = {0};
        SourceSizeInventory base = {0};
        SourceSizeBaseline baseline = {0};
        SourceSizeBaseline base_baseline = {0};
        bool passed = source_size_inventory(arena, revision, &head) && (!base_revision.length || source_size_inventory(arena, base_revision, &base));
        if (passed && write_baseline)
        {
            String8 text = source_size_baseline_format(arena, head.commit, head.totals);
            passed = file_publish(S8(SOURCE_SIZE_BASELINE_PATH), (ByteSlice){.pointer = (u8*)text.pointer, .length = text.length}) &&
                source_size_baseline_parse(text, &baseline);
            if (passed)
            {
                string_print(S8("Wrote " SOURCE_SIZE_BASELINE_PATH " for {S8}\n\n"), head.commit);
            }
            else
            {
                string_print(S8("error: source_size: could not write " SOURCE_SIZE_BASELINE_PATH "\n"));
            }
        }
        else if (passed)
        {
            bool readable = source_size_baseline_read(arena, &head, &baseline);
            passed = readable && baseline.present;
            if (!readable)
            {
                string_print(S8("error: source_size: " SOURCE_SIZE_BASELINE_PATH " at {S8} is malformed\n"), head.commit);
            }
            else if (!passed)
            {
                string_print(S8("error: source_size: {S8} has no " SOURCE_SIZE_BASELINE_PATH "; create it with --write-baseline\n"), head.commit);
            }
            // A base whose baseline is missing or malformed predates the ratchet
            // or is being repaired by this change; either way it acknowledges.
            if (passed && base_revision.length && !source_size_baseline_read(arena, &base, &base_baseline))
            {
                string_print(S8("note: source_size: the base's " SOURCE_SIZE_BASELINE_PATH " is malformed; treating it as absent\n"));
            }
        }
        passed = passed && source_size_report(arena, &head, base_revision.length ? &base : 0, baseline, base_baseline);
        result = passed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    return result;
}
