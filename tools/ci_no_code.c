// Conservative no-code planning (#3107), included by build.c.
// ci_no_code_main owns identities and output; ci_no_code_diff parses complete
// NUL-delimited raw Git changes. Git runs through the existing bounded OS
// capture API. The allowlist is executable policy: changing it selects full
// validation until the previously trusted reader has admitted that change.
#define CI_NO_CODE_SCHEMA "buster-ci-no-code-v1"
#define CI_NO_CODE_GIT_TIMEOUT_US (60ull * 1000000)

typedef struct CiNoCodeDiff CiNoCodeDiff;
struct CiNoCodeDiff
{
    bool valid;
    bool prose_only;
    u64 changes;
};

// Explicit ordinary prose only. Agent instructions, policy/coverage documents,
// manifests, audits/evidence and arbitrary new Markdown paths are not exempt.
BUSTER_GLOBAL_LOCAL String8 ci_no_code_prose_paths[] = {
    S8_INITIALIZER("README.md"),
    S8_INITIALIZER("docs/compiler-lifetime.md"),
    S8_INITIALIZER("docs/diagnostics.md"),
    S8_INITIALIZER("docs/incremental-compilation.md"),
};

BUSTER_GLOBAL_LOCAL bool ci_no_code_sha(String8 value)
{
    bool result = value.length == 40;
    for (u64 i = 0; result && i < value.length; i += 1)
    {
        char8 c = value.pointer[i];
        result = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_no_code_prose(String8 path)
{
    bool result = false;
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(ci_no_code_prose_paths); i += 1)
    {
        result = result || string_equal(path, ci_no_code_prose_paths[i]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_no_code_token(String8 input, u64* cursor, char8 delimiter, String8* token)
{
    u64 start = *cursor;
    while (*cursor < input.length && input.pointer[*cursor] != delimiter)
    {
        *cursor += 1;
    }
    bool result = *cursor < input.length && *cursor > start;
    if (result)
    {
        *token = string_slice(input, start, *cursor);
        *cursor += 1;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CiNoCodeDiff ci_no_code_diff(String8 raw)
{
    CiNoCodeDiff result = {.valid = true, .prose_only = true};
    u64 cursor = 0;
    while (result.valid && cursor < raw.length)
    {
        String8 fields[5] = {0};
        String8 path = {0};
        result.valid = raw.pointer[cursor] == ':';
        cursor += 1;
        for (u64 i = 0; result.valid && i < BUSTER_ARRAY_LENGTH(fields); i += 1)
        {
            result.valid = ci_no_code_token(raw, &cursor, i == 4 ? 0 : ' ', &fields[i]);
        }
        result.valid = result.valid && ci_no_code_token(raw, &cursor, 0, &path) &&
            ci_no_code_sha(fields[2]) && ci_no_code_sha(fields[3]) &&
            fields[0].length == 6 && fields[1].length == 6 &&
            fields[4].length == 1 &&
            (fields[4].pointer[0] == 'A' || fields[4].pointer[0] == 'D' || fields[4].pointer[0] == 'M' ||
             fields[4].pointer[0] == 'T');
        if (result.valid)
        {
            char8 status = fields[4].pointer[0];
            bool regular_old = string_equal(fields[0], S8("100644"));
            bool regular_new = string_equal(fields[1], S8("100644"));
            bool absent_old = string_equal(fields[0], S8("000000"));
            bool absent_new = string_equal(fields[1], S8("000000"));
            bool zero_old = string_equal(fields[2], S8("0000000000000000000000000000000000000000"));
            bool zero_new = string_equal(fields[3], S8("0000000000000000000000000000000000000000"));
            // No rename collapsing: both paths of a move become D/A rows.
            // Type/mode changes, symlinks and gitlinks always execute.
            bool regular = (status == 'M' && regular_old && regular_new && !zero_old && !zero_new) ||
                (status == 'A' && absent_old && zero_old && regular_new && !zero_new) ||
                (status == 'D' && regular_old && !zero_old && absent_new && zero_new);
            result.prose_only = result.prose_only && regular && ci_no_code_prose(path);
            result.changes += 1;
        }
    }
    result.prose_only = result.valid && result.prose_only;
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_no_code_git(Arena* arena, String8 repo, SliceString8 arguments, String8* output)
{
    String8List command = {0};
    string8_list_push(arena, &command, S8("git"));
    string8_list_push(arena, &command, S8("-C"));
    string8_list_push(arena, &command, repo);
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        string8_list_push(arena, &command, arguments.pointer[i]);
    }
    ProcessSpawnResult spawn = os_process_spawn(string8_list_to_slice(arena, command), (SliceString8){0}, (SliceString8){0},
        (ProcessSpawnOptions){.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                             .use_process_environment = 1, .search_path = 1, .new_process_group = 1});
    bool result = false;
    if (spawn.handle)
    {
        ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, CI_NO_CODE_GIT_TIMEOUT_US);
        result = wait.result == PROCESS_RESULT_SUCCESS && wait.platform_status == 0 && !wait.timed_out &&
            !wait.capture_failed && !wait.capture_limit_exceeded && !wait.output_truncated && !wait.process_tree_cleanup_failed &&
            !wait.process_group_ownership_lost && !wait.dropped_total;
        *output = BYTE_SLICE_TO_STRING(8, wait.streams[STANDARD_STREAM_OUTPUT]);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CiNoCodeDiff ci_no_code_compare(Arena* arena, String8 repo, String8 base, String8 head)
{
    String8 raw = {0};
    String8 arguments[] = {S8("diff"), S8("--raw"), S8("--no-abbrev"), S8("-z"), S8("--no-renames"),
        S8("--no-ext-diff"), S8("--no-textconv"), S8("--ignore-submodules=none"), base, head, S8("--")};
    CiNoCodeDiff result = {0};
    if (ci_no_code_git(arena, repo, (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), &raw))
    {
        result = ci_no_code_diff(raw);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool ci_no_code_self_test(Arena* arena)
{
    String8 old = S8("1111111111111111111111111111111111111111");
    String8 new = S8("2222222222222222222222222222222222222222");
    String8 zero = S8("0000000000000000000000000000000000000000");
    bool result = ci_no_code_diff((String8){0}).prose_only;
    String8 paths[] = {S8("README.md"), S8("docs/compiler-lifetime.md"), S8("src/a.c"), S8("src/a.h"),
        S8("tests/a.md"), S8("tests/a.data"), S8("docs/native-retirement-dependencies-v1.json"),
        S8("docs/native-retirement-rebinding.md"), S8("docs/unknown.md"), S8("build.c"),
        S8(".github/workflows/ci.yml"), S8("tools/ci_no_code.c"), S8("README.md\nother.c"), S8("readme.md")};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(paths); i += 1)
    {
        String8 modes[] = {S8("100644"), S8("100755"), S8("120000"), S8("160000")};
        for (u64 mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
        {
            String8 raw = string_format(arena, S8(":{S8} {S8} {S8} {S8} M{char8}{S8}{char8}"),
                modes[mode], modes[mode], old, new, 0, paths[i], 0);
            CiNoCodeDiff parsed = ci_no_code_diff(raw);
            result = result && parsed.valid && parsed.changes == 1 &&
                parsed.prose_only == (i < 2 && mode == 0);
        }
    }
    String8 added = string_format(arena, S8(":000000 100644 {S8} {S8} A{char8}README.md{char8}"), zero, new, 0, 0);
    String8 deleted = string_format(arena, S8(":100644 000000 {S8} {S8} D{char8}README.md{char8}"), old, zero, 0, 0);
    String8 executable = string_format(arena, S8(":100644 100755 {S8} {S8} M{char8}README.md{char8}"), old, old, 0, 0);
    String8 typed = string_format(arena, S8(":100644 120000 {S8} {S8} T{char8}README.md{char8}"), old, new, 0, 0);
    String8 moved_out = string_format(arena, S8("{S8}:000000 100644 {S8} {S8} A{char8}src/a.c{char8}"), deleted, zero, new, 0, 0);
    result = result && ci_no_code_diff(added).prose_only && ci_no_code_diff(deleted).prose_only &&
        ci_no_code_diff(string_format(arena, S8("{S8}{S8}"), added, deleted)).prose_only &&
        !ci_no_code_diff(executable).prose_only && !ci_no_code_diff(typed).prose_only &&
        !ci_no_code_diff(moved_out).prose_only;
    for (u64 length = 1; length < added.length; length += 1)
    {
        result = result && !ci_no_code_diff(string_slice(added, 0, length)).valid;
    }
    result = result && !ci_no_code_diff(S8("malformed")).valid &&
        !ci_no_code_sha(S8("--bad")) && !ci_no_code_sha(S8("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
    string_print(S8("CI_NO_CODE_SELF_TEST {S8}\n"), result ? S8("passed") : S8("failed"));
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult ci_no_code_main(Arena* arena, SliceString8 arguments)
{
    String8 repo = S8(".");
    String8 base = {0};
    String8 head = {0};
    String8 tested = {0};
    String8 policy = {0};
    String8 event = {0};
    bool self_test = arguments.length == 1 && string_equal(arguments.pointer[0], S8("--self-test"));
    bool valid = true;
    for (u64 i = 0; !self_test && valid && i < arguments.length; i += 2)
    {
        valid = i + 1 < arguments.length;
        if (valid)
        {
            String8 key = arguments.pointer[i];
            String8 value = arguments.pointer[i + 1];
            if (string_equal(key, S8("--repo"))) repo = value;
            else if (string_equal(key, S8("--base"))) { valid = !base.length; base = value; }
            else if (string_equal(key, S8("--head"))) { valid = !head.length; head = value; }
            else if (string_equal(key, S8("--tested"))) { valid = !tested.length; tested = value; }
            else if (string_equal(key, S8("--policy"))) { valid = !policy.length; policy = value; }
            else if (string_equal(key, S8("--event"))) { valid = !event.length; event = value; }
            else valid = false;
        }
    }
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (self_test)
    {
        result = ci_no_code_self_test(arena) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
    }
    else if (valid && repo.length && ci_no_code_sha(base) && ci_no_code_sha(head) &&
        ci_no_code_sha(tested) && ci_no_code_sha(policy) &&
        (string_equal(event, S8("pull_request")) || string_equal(event, S8("merge_group"))))
    {
        // Failures select full validation with a visible reason. There is no
        // successful empty plan after failed/truncated Git capture.
        String8 reason = S8("invalid-source-identity-or-incomplete-diff");
        bool no_code = false;
        String8 parents = {0};
        String8 parent_args[] = {S8("show"), S8("-s"), S8("--format=%P"), tested, S8("--")};
        bool identities = ci_no_code_git(arena, repo, (SliceString8)BUSTER_ARRAY_TO_SLICE(parent_args), &parents);
        String8 expected_parents = string_equal(event, S8("pull_request")) ?
            string_format(arena, S8("{S8} {S8}\n"), base, head) : (String8){0};
        identities = identities && (string_equal(event, S8("pull_request")) ? string_equal(parents, expected_parents) :
            (string_equal(tested, head) && parents.length == 82 &&
             string_equal(string_slice(parents, 0, 40), base) && parents.pointer[40] == ' ' &&
             ci_no_code_sha(string_slice(parents, 41, 81)) && parents.pointer[81] == '\n'));
        String8 merge_base = {0};
        String8 ancestor_args[] = {S8("merge-base"), base, head};
        identities = identities && ci_no_code_git(arena, repo, (SliceString8)BUSTER_ARRAY_TO_SLICE(ancestor_args), &merge_base);
        if (merge_base.length == 41 && merge_base.pointer[40] == '\n')
        {
            merge_base = string_slice(merge_base, 0, 40);
        }
        identities = identities && ci_no_code_sha(merge_base);
        CiNoCodeDiff integrated = {0};
        CiNoCodeDiff contribution = {0};
        CiNoCodeDiff policy_change = {0};
        if (identities)
        {
            integrated = ci_no_code_compare(arena, repo, base, tested);
            contribution = ci_no_code_compare(arena, repo, merge_base, head);
            policy_change = ci_no_code_compare(arena, repo, policy, base);
            bool complete = integrated.valid && contribution.valid && policy_change.valid;
            no_code = complete && integrated.prose_only && contribution.prose_only && policy_change.prose_only;
            reason = !complete ? S8("incomplete-diff") : no_code ? S8("reviewed-prose-only") : S8("execution-affecting-or-unknown-input");
        }
        string_print(S8("{{\"schema\":\"" CI_NO_CODE_SCHEMA "\",\"profile\":\"{S8}\",\"no_code\":{S8},"
            "\"base\":\"{S8}\",\"head\":\"{S8}\",\"tested\":\"{S8}\",\"policy\":\"{S8}\","
            "\"changes\":{u64},\"reason\":\"{S8}\"}\n"),
            no_code ? S8("no-code") : S8("full"), no_code ? S8("true") : S8("false"),
            base, head, tested, policy, integrated.changes, reason);
        String8 output_path = os_get_environment_variable(S8("GITHUB_OUTPUT"));
        if (output_path.length)
        {
            String8 output = string_format(arena, S8("no_code={S8}\n"), no_code ? S8("true") : S8("false"));
            FILE* file = fopen((const char*)output_path.pointer, "ab");
            bool written = file && fwrite(output.pointer, 1, (size_t)output.length, file) == output.length;
            if (file)
            {
                written = fclose(file) == 0 && written;
            }
            result = written ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        }
        else
        {
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    else
    {
        string_print(S8("error: ci_no_code requires --repo PATH --base SHA --head SHA --tested SHA --policy SHA --event pull_request|merge_group\n"));
    }
    return result;
}
