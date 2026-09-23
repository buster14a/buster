/* Dedicated #1018 fixture. Compile against tools/throughput/shared.c; this
 * intentionally does not change the shared service test registration owned by
 * the #923 integrator. It uses the real descriptor-backed materializer.
 */
#define main bq_service_cli_main
#include "main.c"
#undef main
#include <stdlib.h>

BUSTER_GLOBAL_LOCAL u32 bq_retirement_tests;
BUSTER_GLOBAL_LOCAL u32 bq_retirement_failures;
#define BQ_PREP_CHECK(expr) do { bq_retirement_tests += 1; if (!(expr)) { \
    bq_retirement_failures += 1; fprintf(stderr, "RETIREMENT_PREP failure line=%d: %s\n", __LINE__, #expr); \
} } while (0)

BUSTER_GLOBAL_LOCAL bool bq_prep_test_write(char const* path, char const* text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
    bool ok = fd >= 0 && bq_write_all(fd, (u8 const*)text, (u32)strlen(text)) && fsync(fd) == 0;
    if (fd >= 0 && close(fd) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_source(char const* installed, char const* revision, char const* contents,
                                            BqRetirementSource* expected)
{
    char root[512], src[512], file[512], manifest[512], text[512];
    int length = snprintf(root, sizeof(root), "%s/sources/%s", installed, revision);
    bool ok = length > 0 && (u32)length < sizeof(root);
    length = ok ? snprintf(src, sizeof(src), "%s/src", root) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(src);
    length = ok ? snprintf(file, sizeof(file), "%s/main.c", src) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(file);
    length = ok ? snprintf(manifest, sizeof(manifest), "%s/source.manifest", root) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(manifest);
    ok = ok && mkdir(root, 0700) == 0 && mkdir(src, 0700) == 0 && bq_prep_test_write(file, contents);
    char8 digest[SHA256_HEX_CAPACITY];
    if (ok) bq_digest(contents, (u32)strlen(contents), digest);
    length = ok ? snprintf(text, sizeof(text),
                           "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n%.64s src/main.c\n",
                           revision, digest) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(text) && bq_prep_test_write(manifest, text) &&
         chmod(src, 0500) == 0 && chmod(root, 0500) == 0;
    if (ok)
    {
        memcpy(expected->commit, revision, strlen(revision) + 1);
        memset(expected->tree, revision[0] == 'a' ? 'c' : 'd', 40);
        expected->tree[40] = 0;
        bq_digest(text, (u32)length, (char8*)expected->manifest_sha256);
        expected->entries = 1;
        expected->bytes = strlen(contents);
        expected->directories = 2;
        expected->max_path = 10;
        expected->max_depth = 2;
        expected->manifest_bytes = (u32)length;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_inventory(char const* installed, BqRetirementSource const subjects[2],
                                                char profile[512])
{
    char inventory[1024], path[512];
    int length = snprintf(inventory, sizeof(inventory),
                          "BQ-RETIREMENT-INPUTS-V1\nrepository=buster14a/buster\n"
                          "support-sha256=%.64s\ncontract-sha256=%.64s\n"
                          "base=%s %s %s %u %" PRIu64 " %u %u %u\n"
                          "candidate=%s %s %s %u %" PRIu64 " %u %u %u\n",
                          "1111111111111111111111111111111111111111111111111111111111111111",
                          "2222222222222222222222222222222222222222222222222222222222222222",
                          subjects[0].commit, subjects[0].tree, subjects[0].manifest_sha256, subjects[0].entries,
                          (uint64_t)subjects[0].bytes, subjects[0].directories, subjects[0].max_path, subjects[0].max_depth,
                          subjects[1].commit, subjects[1].tree, subjects[1].manifest_sha256, subjects[1].entries,
                          (uint64_t)subjects[1].bytes, subjects[1].directories, subjects[1].max_path, subjects[1].max_depth);
    bool ok = length > 0 && (u32)length < sizeof(inventory);
    char8 digest[SHA256_HEX_CAPACITY];
    if (ok) bq_digest(inventory, (u32)length, digest);
    length = ok ? snprintf(profile, 512,
                           "schema=1\nrecipe=native-retirement-performance-v1\n"
                           "support-declaration-sha256=%.64s\ncontract-sha256=%.64s\ninventory-sha256=%.64s\n",
                           "1111111111111111111111111111111111111111111111111111111111111111",
                           "2222222222222222222222222222222222222222222222222222222222222222", digest) : -1;
    ok = ok && length > 0 && length < 512;
    length = ok ? snprintf(path, sizeof(path), "%s/recipes/native-retirement-performance-v1.inventory", installed) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(path) && bq_prep_test_write(path, inventory);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_setup(char installed[80], char workspaces[80],
                                            BqRetirementSource subjects[2], char profile[512], BqRequest* request)
{
    memcpy(installed, "/tmp/bq-retirement-installed-XXXXXX", sizeof("/tmp/bq-retirement-installed-XXXXXX"));
    memcpy(workspaces, "/tmp/bq-retirement-workspaces-XXXXXX", sizeof("/tmp/bq-retirement-workspaces-XXXXXX"));
    bool ok = mkdtemp(installed) != NULL && mkdtemp(workspaces) != NULL;
    char sources[512], recipes[512];
    int length = snprintf(sources, sizeof(sources), "%s/sources", installed);
    ok = ok && length > 0 && (u32)length < sizeof(sources);
    length = ok ? snprintf(recipes, sizeof(recipes), "%s/recipes", installed) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(recipes) && mkdir(sources, 0700) == 0 &&
         mkdir(recipes, 0700) == 0;
    char const* base = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char const* candidate = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ok = ok && bq_prep_test_source(installed, base, "int a;\n", &subjects[0]) &&
         bq_prep_test_source(installed, candidate, "int b;\n", &subjects[1]) &&
         bq_prep_test_inventory(installed, subjects, profile) &&
         chmod(recipes, 0500) == 0 && chmod(sources, 0500) == 0 &&
         chmod(installed, 0500) == 0 && chmod(workspaces, 02710) == 0;
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("id"), S8("validate-buster-v1"),
        string_from_pointer(base), string_from_pointer(candidate)};
    ok = ok && bq_request_make(fields, request) == BQ_OK;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_cleanup(char const* path)
{
    int root = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(root >= 0 && bq_remove_workspace_payload(root));
    if (root >= 0) close(root);
    BQ_PREP_CHECK(rmdir(path) == 0);
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_large_manifest(void)
{
    char root[80] = "/tmp/bq-retirement-large-XXXXXX";
    char const* revision = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    bool ok = mkdtemp(root) != NULL;
    char sources[256], snapshot[256], src[256], copied[256], manifest_path[256];
    int length = snprintf(sources, sizeof(sources), "%s/sources", root);
    ok = ok && length > 0 && (u32)length < sizeof(sources);
    length = ok ? snprintf(snapshot, sizeof(snapshot), "%s/%s", sources, revision) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(snapshot);
    length = ok ? snprintf(src, sizeof(src), "%s/src", snapshot) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(src);
    length = ok ? snprintf(copied, sizeof(copied), "%s/copied", root) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(copied);
    length = ok ? snprintf(manifest_path, sizeof(manifest_path), "%s/source.manifest", snapshot) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(manifest_path) &&
         mkdir(sources, 0700) == 0 && mkdir(snapshot, 0700) == 0 &&
         mkdir(src, 0700) == 0 && mkdir(copied, 0700) == 0;
    char text[128u * 1024u] = {0};
    length = ok ? snprintf(text, sizeof(text), "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n", revision) : -1;
    ok = ok && length > 0 && (u32)length < sizeof(text);
    u32 used = ok ? (u32)length : 0;
    char8 digest[SHA256_HEX_CAPACITY];
    bq_digest("x", 1, digest);
    for (u32 index = 0; ok && index < 1024; index += 1)
    {
        char file[256];
        int named = snprintf(file, sizeof(file), "%s/f%04u.c", src, index);
        ok = named > 0 && (u32)named < sizeof(file) && bq_prep_test_write(file, "x");
        int line = ok ? snprintf(text + used, sizeof(text) - used, "%.64s src/f%04u.c\n", digest, index) : -1;
        ok = ok && line > 0 && (u32)line < sizeof(text) - used;
        if (ok) used += (u32)line;
    }
    ok = ok && used > BQ_SOURCE_MANIFEST_CAP && used < BQ_RETIREMENT_SOURCE_MANIFEST_CAP &&
         bq_prep_test_write(manifest_path, text) && chmod(src, 0500) == 0 &&
         chmod(snapshot, 0500) == 0 && chmod(sources, 0500) == 0;
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        int installed = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int destination = open(copied, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(installed >= 0 && destination >= 0 &&
                      !bq_copy_manifest(installed, destination, string_from_pointer(revision), BQ_SOURCE_MANIFEST_CAP));
        BQ_PREP_CHECK(bq_copy_manifest(installed, destination, string_from_pointer(revision),
                                       BQ_RETIREMENT_SOURCE_MANIFEST_CAP));
        BqRetirementSource observed = {0};
        BQ_PREP_CHECK(bq_make_sources_read_only(destination) &&
                      bq_retirement_scan(destination, ".source-manifest", string_from_pointer(revision),
                                         &observed, true) &&
                      observed.entries == 1024 && observed.bytes == 1024 &&
                      observed.directories == 2 && observed.max_path == 11 && observed.max_depth == 2 &&
                      observed.manifest_bytes == used);
        if (installed >= 0) close(installed);
        if (destination >= 0) close(destination);
    }
    if (root[0] && ok) bq_prep_test_cleanup(root);
}

/* Exercise the service's durable producer/readback boundary through the real
 * source copier. This internal test request cannot be submitted: the public
 * registry still rejects the blocked retirement descriptor. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_ready_handoff(int installed, int workspaces,
    BqRetirementPreparation* prepared, char const* profile)
{
    char queue_path[80] = "/tmp/bq-retirement-queue-XXXXXX";
    bool ok = mkdtemp(queue_path) != NULL;
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    if (ok) ok = bq_open(&queue, queue_path) == BQ_OK;
    BQ_PREP_CHECK(ok);
    BqJob job = {.id = 1, .token = 2};
    String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("handoff"),
        S8("native-retirement-performance-v1"),
        string_from_pointer(prepared->subjects[0].commit),
        string_from_pointer(prepared->subjects[1].commit)};
    for (u32 i = 0; ok && i < BQ_FIELD_COUNT; ++i)
    {
        ok = fields[i].length <= BQ_REQUEST_CAP - job.request.size - 4;
        if (ok)
        {
            bq_put32(job.request.bytes + job.request.size, (u32)fields[i].length);
            job.request.size += 4;
            memcpy(job.request.bytes + job.request.size, fields[i].pointer, (size_t)fields[i].length);
            job.request.size += (u32)fields[i].length;
        }
    }
    if (ok) bq_request_digest(&job.request, job.digest);
    char attempt[64];
    if (ok) ok = bq_workspace_name(attempt, job.id, job.token) && mkdirat(workspaces, attempt, 0700) == 0;
    int root = ok ? openat(workspaces, attempt, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && root >= 0;
    for (u32 side = 0; ok && side < 2; ++side)
    {
        char const* name = side ? "candidate" : "base";
        ok = mkdirat(root, name, 0700) == 0;
        int parent = ok ? openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && parent >= 0 && mkdirat(parent, "source", 02750) == 0;
        int source = ok ? openat(parent, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        if (ok)
        {
            ok = source >= 0 && bq_copy_manifest(installed, source, fields[3 + side],
                                                   BQ_RETIREMENT_SOURCE_MANIFEST_CAP) &&
                 bq_make_sources_read_only(source) &&
                 bq_retirement_verify_subject(installed, parent, source, fields[3 + side],
                                              &prepared->subjects[side]);
        }
        if (source >= 0) close(source);
        if (parent >= 0) close(parent);
    }
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        char digest[SHA256_HEX_CAPACITY];
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest) == BQ_NOT_FOUND && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_record(&queue, &job, prepared, BQ_OK, 2) &&
                      bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), digest) == BQ_OK && strlen(digest) == 64);
        BqJob other = job;
        other.token += 1;
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &other, installed, workspaces,
                      string_from_pointer(profile), digest) == BQ_CORRUPT && !digest[0]);
        other = job;
        other.digest[0] = other.digest[0] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &other, installed, workspaces,
                      string_from_pointer(profile), digest) == BQ_RECIPE_MISMATCH && !digest[0]);
        char wrong_profile[512];
        memcpy(wrong_profile, profile, strlen(profile) + 1);
        char* pin = strstr(wrong_profile, "inventory-sha256=");
        if (pin) pin[17] = pin[17] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(pin && bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(wrong_profile), digest) == BQ_RECIPE_MISMATCH && !digest[0]);
        char copied_path[128];
        int length = snprintf(copied_path, sizeof(copied_path), "%s/base/source/src", attempt);
        int copied = length > 0 && (u32)length < sizeof(copied_path) ?
                     bq_open_directory_path(workspaces, string_from_pointer(copied_path)) : -1;
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 &&
                      renameat(copied, "main.c", copied, "old.c") == 0);
        if (copied >= 0)
        {
            int replacement = openat(copied, "main.c", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
            BQ_PREP_CHECK(replacement >= 0 && bq_write_all(replacement, (u8 const*)"int a;\n", 7) &&
                          fchmod(replacement, 0400) == 0 && fchmod(copied, 0500) == 0);
            if (replacement >= 0) close(replacement);
            BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), digest) == BQ_SOURCE_MISMATCH && !digest[0]);
            close(copied);
        }
        int record = openat(queue.directory_fd, "preparation-1", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(record >= 0 && fchmod(record, 0600) == 0);
        if (record >= 0) close(record);
        record = openat(queue.directory_fd, "preparation-1", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(record >= 0 && pwrite(record, "X", 1, 0) == 1 && fchmod(record, 0400) == 0);
        if (record >= 0) close(record);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest) == BQ_CORRUPT && !digest[0]);
    }
    if (root >= 0) close(root);
    bq_close(&queue);
    if (queue_path[0] && ok) bq_prep_test_cleanup(queue_path);
}

int main(void)
{
    char installed[80] = {0}, workspaces[80] = {0}, profile[512] = {0};
    BqRetirementSource subjects[2] = {0};
    BqRequest request = {0};
    bool setup = bq_prep_test_setup(installed, workspaces, subjects, profile, &request);
    BQ_PREP_CHECK(setup);
    if (setup)
    {
        int input = open(installed, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int output = open(workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BqRetirementPreparation preparation = {0};
        String8 pinned = string_from_pointer(profile);
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request, pinned, &preparation) == BQ_OK);
        BqRetirementSource verified_base = preparation.subjects[0];
        BQ_PREP_CHECK(preparation.subjects[0].entries == 1 && preparation.subjects[1].entries == 1 &&
                      preparation.source_reservation_bytes > BQ_RETIREMENT_COPY_OVERHEAD);
        bq_prep_test_ready_handoff(input, output, &preparation, profile);

        char wrong_profile[512];
        memcpy(wrong_profile, profile, sizeof(wrong_profile));
        char* pin = strstr(wrong_profile, "inventory-sha256=");
        BQ_PREP_CHECK(pin != NULL);
        if (pin) pin[17] = pin[17] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request,
                       string_from_pointer(wrong_profile), &preparation) == BQ_RECIPE_MISMATCH);
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, -1, &request, pinned,
                       &preparation) == BQ_RESOURCE_MISMATCH);
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request, pinned, &preparation) == BQ_OK);

        char changed_inventory[1024];
        int manifest_length = snprintf(changed_inventory, sizeof(changed_inventory),
                                      "base=%s %s %s 4097 7 2 10 2", subjects[0].commit,
                                      subjects[0].tree, subjects[0].manifest_sha256);
        BqRetirementSource ignored = {0};
        BQ_PREP_CHECK(manifest_length > 0 && !bq_retirement_subject_line(string_from_pointer(changed_inventory),
                                                                          S8("base="), &ignored));
        String8 fields[BQ_FIELD_COUNT] = {S8("fixture"), S8("other"), S8("validate-buster-v1"),
                                         S8("cccccccccccccccccccccccccccccccccccccccc"),
                                         S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")};
        BqRequest overridden = {0};
        BQ_PREP_CHECK(bq_request_make(fields, &overridden) == BQ_OK &&
                      bq_retirement_preflight_pinned(input, output, &overridden, pinned,
                                                     &preparation) == BQ_RECIPE_MISMATCH);

        BQ_PREP_CHECK(mkdirat(output, "base", 02750) == 0);
        int subject = openat(output, "base", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(subject >= 0 && mkdirat(subject, "source", 02750) == 0);
        int source = openat(subject, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(source >= 0 && bq_copy_manifest(input, source, string_from_pointer(subjects[0].commit),
                                                       BQ_RETIREMENT_SOURCE_MANIFEST_CAP));
        BQ_PREP_CHECK(bq_make_sources_read_only(source) &&
                      bq_retirement_verify_subject(input, subject, source, string_from_pointer(subjects[0].commit),
                                                   &verified_base));
        struct stat info = {0};
        BQ_PREP_CHECK(fstatat(subject, "verification-source", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);

        char source_path[512], directory_path[512], old_path[512], manifest_path[512];
        int length = snprintf(source_path, sizeof(source_path), "%s/sources/%s/src/main.c", installed, subjects[0].commit);
        BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(source_path));
        length = snprintf(directory_path, sizeof(directory_path), "%s/sources/%s/src", installed, subjects[0].commit);
        BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(directory_path));
        length = snprintf(old_path, sizeof(old_path), "%s/sources/%s/src/old.c", installed, subjects[0].commit);
        BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(old_path));
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && rename(source_path, old_path) == 0 &&
                      bq_prep_test_write(source_path, "int a;\n") && chmod(directory_path, 0500) == 0);
        BQ_PREP_CHECK(!bq_retirement_verify_subject(input, subject, source,
                                                    string_from_pointer(subjects[0].commit), &verified_base));
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request, pinned, &preparation) == BQ_OK);

        BQ_PREP_CHECK(chmod(source_path, 0600) == 0);
        int altered = open(source_path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(altered >= 0 && write(altered, "changed", 7) == 7);
        if (altered >= 0) close(altered);
        BQ_PREP_CHECK(chmod(source_path, 0400) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && unlink(source_path) == 0 &&
                      chmod(directory_path, 0500) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);

        length = snprintf(manifest_path, sizeof(manifest_path), "%s/sources/%s/source.manifest", installed,
                          subjects[0].commit);
        BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(manifest_path));
        char8 source_digest[SHA256_HEX_CAPACITY];
        bq_digest("int a;\n", 7, source_digest);
        char duplicate[512];
        length = snprintf(duplicate, sizeof(duplicate),
                          "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n"
                          "%.64s src/main.c\n%.64s src/main.c\n",
                          subjects[0].commit, source_digest, source_digest);
        BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(duplicate) && chmod(manifest_path, 0600) == 0);
        int manifest_fd = open(manifest_path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(manifest_fd >= 0 && bq_write_all(manifest_fd, (u8 const*)duplicate, (u32)length));
        if (manifest_fd >= 0) close(manifest_fd);
        int source_root = openat(input, "sources", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int snapshot = source_root >= 0 ? openat(source_root, subjects[0].commit,
                                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        BqRetirementSource observed = {0};
        BQ_PREP_CHECK(chmod(manifest_path, 0400) == 0 && snapshot >= 0 &&
                      !bq_retirement_scan(snapshot, "source.manifest", string_from_pointer(subjects[0].commit),
                                          &observed, true));
        if (snapshot >= 0) close(snapshot);
        if (source_root >= 0) close(source_root);

        if (source >= 0) close(source);
        if (subject >= 0) close(subject);
        if (input >= 0) close(input);
        if (output >= 0) close(output);
        bq_prep_test_cleanup(workspaces);
        bq_prep_test_cleanup(installed);
    }
    bq_prep_test_large_manifest();
    printf("RETIREMENT_PREP_TEST assertions=%u failures=%u\n", bq_retirement_tests, bq_retirement_failures);
    int result = bq_retirement_failures ? 1 : 0;
    return result;
}
