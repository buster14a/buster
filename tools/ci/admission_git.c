// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Metadata-only merge preflight. Git reads objects in a private bare repository; it never checks out PR code,
// runs hooks/filters/PR programs or writes a GitHub ref. Live branch tips, not GitHub's deferred test merge, bind it.

typedef struct CiGitResult CiGitResult;
struct CiGitResult { Buffer output; int status; };
typedef struct CiGit CiGit;
struct CiGit { char directory[256]; };
static CiGit ci_git;

static CiGitResult ci_git_exec(const char* directory, const char* const* command, const void* input, size_t length)
{
    FILE* source = tmpfile();
    int pipes[2];
    if (source == NULL || pipe(pipes) != 0 || (length != 0 && fwrite(input, 1, length, source) != length)) ci_die("cannot prepare Git metadata request");
    rewind(source);
    pid_t child = fork();
    if (child < 0) ci_die("cannot start Git metadata reader");
    if (child == 0)
    {
        if (dup2(fileno(source), STDIN_FILENO) < 0 || dup2(pipes[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipes[0]); close(pipes[1]);
        const char* remove[] = {"GIT_CONFIG_PARAMETERS", "GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_TEMPLATE_DIR", "GIT_ASKPASS", "SSH_ASKPASS", "GIT_SSH", "GIT_SSH_COMMAND", "GIT_PROXY_COMMAND"};
        for (size_t i = 0; i < sizeof(remove) / sizeof(remove[0]); ++i) unsetenv(remove[i]);
        setenv("GIT_CONFIG_COUNT", "0", 1);
        setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
        setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
        setenv("GIT_ATTR_NOSYSTEM", "1", 1);
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        setenv("GIT_NO_REPLACE_OBJECTS", "1", 1);
        setenv("GIT_LFS_SKIP_SMUDGE", "1", 1);
        char* arguments[80] = {"/usr/bin/git", "-c", "core.hooksPath=/dev/null", "-c", "core.fsmonitor=false", "-c", "core.attributesFile=/dev/null", "-c", "credential.helper=", "-c", "core.askPass=/bin/false", "-c", "protocol.allow=never", "-c", "protocol.https.allow=always", "-c", "pack.threads=1", "-c", "index.threads=1", NULL};
        size_t at = 19;
        if (directory != NULL) { arguments[at++] = "--git-dir"; arguments[at++] = (char*)directory; }
        for (size_t i = 0; command[i] != NULL; ++i)
        {
            if (at + 1 >= sizeof(arguments) / sizeof(arguments[0])) _exit(127);
            arguments[at++] = (char*)command[i];
        }
        arguments[at] = NULL;
        execv(arguments[0], arguments);
        _exit(127);
    }
    close(pipes[1]);
    FILE* output = fdopen(pipes[0], "r");
    if (output == NULL) ci_die("cannot read Git metadata output");
    CiGitResult result = {0};
    result.output = ci_read(output);
    fclose(output); fclose(source);
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) ci_die("Git metadata reader was interrupted");
    result.status = WEXITSTATUS(status);
    return result;
}

static Buffer ci_git_ok(const char* directory, const char* const* command, const void* input, size_t length)
{
    CiGitResult result = ci_git_exec(directory, command, input, length);
    if (result.status != 0) ci_die("Git metadata operation %s failed (%d); no admission", command[0], result.status);
    return result.output;
}

static void ci_git_dispose(CiGit* git)
{
    if (*git->directory != 0)
    {
        // Only a path returned by mkdtemp here is removed. No source checkout or user directory is involved.
        if (strncmp(git->directory, "/tmp/buster-9700x-merge-", sizeof("/tmp/buster-9700x-merge-") - 1) != 0) ci_die("invalid private Git cleanup path");
        pid_t child = fork();
        if (child == 0) { execl("/bin/rm", "rm", "-rf", "--", git->directory, (char*)NULL); _exit(127); }
        int status;
        if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) ci_die("private Git cleanup failed");
        git->directory[0] = 0;
    }
}

static void ci_git_cleanup(void) { ci_git_dispose(&ci_git); }

static void ci_git_init(CiGit* git, bool network)
{
    if (*git->directory == 0)
    {
        ci_copy(git->directory, sizeof(git->directory), "/tmp/buster-9700x-merge-XXXXXX");
        if (mkdtemp(git->directory) == NULL) ci_die("cannot create private bare Git metadata store");
        if (git == &ci_git && atexit(ci_git_cleanup) != 0) ci_die("cannot register private Git cleanup");
        const char* init[] = {"init", "--bare", "--quiet", "--template=", git->directory, NULL};
        Buffer output = ci_git_ok(NULL, init, NULL, 0); free(output.data);
        if (network)
        {
            const char* url[] = {"config", "remote.origin.url", "https://github.com/" BUSTER_CI_REPO ".git", NULL};
            const char* promisor[] = {"config", "remote.origin.promisor", "true", NULL};
            const char* filter[] = {"config", "remote.origin.partialclonefilter", "blob:none", NULL};
            output = ci_git_ok(git->directory, url, NULL, 0); free(output.data);
            output = ci_git_ok(git->directory, promisor, NULL, 0); free(output.data);
            output = ci_git_ok(git->directory, filter, NULL, 0); free(output.data);
        }
    }
}

static void ci_git_sha(Buffer* output, char* sha)
{
    if (output->length != 41 || output->data[40] != '\n') ci_die("invalid Git object identity");
    output->data[40] = 0;
    if (!ci_sha(output->data)) ci_die("invalid Git SHA");
    ci_copy(sha, BUSTER_CI_SHA, output->data);
    free(output->data);
}

static void ci_git_fetch(const char* base, const char* head)
{
    if (!ci_sha(base) || !ci_sha(head)) ci_die("invalid immutable merge inputs");
    ci_git_init(&ci_git, true);
    char base_commit[60], head_commit[60];
    snprintf(base_commit, sizeof(base_commit), "%s^{commit}", base);
    snprintf(head_commit, sizeof(head_commit), "%s^{commit}", head);
    const char* base_exists[] = {"cat-file", "-e", base_commit, NULL};
    const char* head_exists[] = {"cat-file", "-e", head_commit, NULL};
    CiGitResult a = ci_git_exec(ci_git.directory, base_exists, NULL, 0);
    CiGitResult b = ci_git_exec(ci_git.directory, head_exists, NULL, 0);
    free(a.output.data); free(b.output.data);
    if (a.status != 0 || b.status != 0)
    {
        const char* fetch[] = {"fetch", "--quiet", "--filter=blob:none", "--no-tags", "origin", base, head, NULL};
        Buffer output = ci_git_ok(ci_git.directory, fetch, NULL, 0); free(output.data);
    }
}

static void ci_git_commit(CiGit* git, const char* tree, const char* base, const char* head, char* merge)
{
    Buffer commit = {0};
    buffer_printf(&commit, "tree %s\n", tree);
    if (base != NULL) buffer_printf(&commit, "parent %s\n", base);
    if (head != NULL && (base == NULL || strcmp(base, head) != 0)) buffer_printf(&commit, "parent %s\n", head);
    // An unpublished automation object, not a human contribution or a replacement for anyone's branch.
    buffer_printf(&commit, "author Buster CI <ci@buster.invalid> 0 +0000\ncommitter Buster CI <ci@buster.invalid> 0 +0000\n\nBuster 9700X frozen qualification merge v1.\n");
    const char* hash[] = {"hash-object", "-w", "-t", "commit", "--stdin", NULL};
    Buffer output = ci_git_ok(git->directory, hash, commit.data, commit.length);
    free(commit.data);
    ci_git_sha(&output, merge);
}

static int ci_git_merge_in(CiGit* git, const char* base, const char* head, char* merge)
{
    const char* tree[] = {"merge-tree", "--write-tree", "--no-messages", base, head, NULL};
    CiGitResult result = ci_git_exec(git->directory, tree, NULL, 0);
    int clean = result.status == 0;
    merge[0] = 0;
    if (result.status != 0 && result.status != 1) ci_die("cannot establish current mergeability (%d)", result.status);
    if (clean)
    {
        char tree_sha[BUSTER_CI_SHA];
        ci_git_sha(&result.output, tree_sha);
        ci_git_commit(git, tree_sha, base, head, merge);
    }
    else free(result.output.data);
    return clean;
}

static int ci_git_merge(const char* base, const char* head, char* merge)
{
    ci_git_fetch(base, head);
    return ci_git_merge_in(&ci_git, base, head, merge);
}

static bool ci_document(const char* path);

static bool ci_git_code_in(CiGit* git, const char* base, const char* merge)
{
    const char* diff[] = {"diff-tree", "--no-commit-id", "--name-only", "--no-renames", "-r", "-z", base, merge, NULL};
    Buffer output = ci_git_ok(git->directory, diff, NULL, 0);
    bool code = false;
    for (size_t at = 0; at < output.length; )
    {
        size_t length = strlen(output.data + at);
        if (length == 0 || length >= output.length - at) ci_die("invalid immutable changed-path stream");
        code = code || !ci_document(output.data + at);
        at += length + 1;
    }
    free(output.data);
    return code;
}

static bool ci_git_code(const CiIdentity* current)
{
    char expected[BUSTER_CI_SHA];
    if (!ci_git_merge(current->base, current->head, expected) || strcmp(expected, current->merge) != 0) ci_die("immutable merge changed during diff classification");
    return ci_git_code_in(&ci_git, current->base, current->merge);
}

static Buffer ci_git_pack_payload(CiGit* git, const CiLease* lease)
{
    Buffer revisions = {0};
    buffer_printf(&revisions, "%s\n^%s\n^%s\n", lease->merge, lease->base, lease->head);
    const char* pack[] = {"pack-objects", "--stdout", "--revs", NULL};
    Buffer output = ci_git_ok(git->directory, pack, revisions.data, revisions.length);
    free(revisions.data);
    return output;
}

static void ci_git_pack(const CiLease* lease, const char* path)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL) ci_die("cannot write frozen merge object pack");
    if (lease->pr != 0)
    {
        char merge[BUSTER_CI_SHA];
        if (!ci_git_merge(lease->base, lease->head, merge) || strcmp(merge, lease->merge) != 0) ci_die("frozen merge does not match the lease");
        Buffer output = ci_git_pack_payload(&ci_git, lease);
        if (fwrite(output.data, 1, output.length, file) != output.length) ci_die("cannot save frozen merge objects");
        free(output.data);
    }
    if (fclose(file) != 0) ci_die("cannot finish frozen merge object pack");
}

static void ci_git_materialize(const CiLease* lease, const char* path)
{
    if (lease->pr == 0 || !ci_sha(lease->base) || !ci_sha(lease->head) || !ci_sha(lease->merge)) ci_die("invalid frozen checkout request");
    const char* current[] = {"rev-parse", "HEAD", NULL};
    Buffer output = ci_git_ok(NULL, current, NULL, 0);
    char sha[BUSTER_CI_SHA];
    ci_git_sha(&output, sha);
    if (strcmp(sha, lease->base) != 0) ci_die("frozen checkout must start at the exact base");
    const char* fetch[] = {"fetch", "--quiet", "--no-tags", "--depth=1", "https://github.com/" BUSTER_CI_REPO ".git", lease->head, NULL};
    output = ci_git_ok(NULL, fetch, NULL, 0); free(output.data);
    FILE* file = fopen(path, "rb");
    if (file == NULL) ci_die("missing frozen merge object pack");
    Buffer pack = ci_read(file); fclose(file);
    const char* unpack[] = {"index-pack", "--stdin", "--strict", NULL};
    output = ci_git_ok(NULL, unpack, pack.data, pack.length); free(output.data); free(pack.data);
    char parent[60];
    snprintf(parent, sizeof(parent), "%s^1", lease->merge);
    const char* first[] = {"rev-parse", parent, NULL};
    output = ci_git_ok(NULL, first, NULL, 0); ci_git_sha(&output, sha);
    if (strcmp(sha, lease->base) != 0) ci_die("frozen checkout has the wrong base parent");
    if (strcmp(lease->head, lease->base) != 0)
    {
        snprintf(parent, sizeof(parent), "%s^2", lease->merge);
        output = ci_git_ok(NULL, first, NULL, 0); ci_git_sha(&output, sha);
        if (strcmp(sha, lease->head) != 0) ci_die("frozen checkout has the wrong head parent");
    }
    const char* checkout[] = {"checkout", "--quiet", "--detach", "--force", lease->merge, NULL};
    output = ci_git_ok(NULL, checkout, NULL, 0); free(output.data);
    output = ci_git_ok(NULL, current, NULL, 0); ci_git_sha(&output, sha);
    if (strcmp(sha, lease->merge) != 0) ci_die("frozen checkout identity mismatch");
}
