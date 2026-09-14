/* Native regression executable, following tools/throughput/tests.c.
 * No sleeps, external workers, network, benchmark samples or alternative model.
 * Fixture byte edits model durable crash prefixes; they do not simulate a disk
 * cache losing power. Every recovery verdict comes from bq_open/bq_replay.
 */
#define main bench_service_cli_main
#include "main.c"
#undef main
#include <stdlib.h>

BUSTER_GLOBAL_LOCAL u32 bq_test_assertions;
BUSTER_GLOBAL_LOCAL u32 bq_test_failures;
#define BQ_CHECK(expression) do { bq_test_assertions += 1; if (!(expression)) { bq_test_failures += 1; fprintf(stderr, "QUEUE_TEST failure line=%d: %s\n", __LINE__, #expression); } } while (0)

BUSTER_GLOBAL_LOCAL BqRequest bq_test_request(u32 number, bool failure)
{
    char key[32];
    snprintf(key, sizeof(key), "request-%u", number);
    String8 fields[BQ_FIELD_COUNT] = {
        S8("test-principal"), string_from_pointer(key), failure ? S8("fake-failure-v1") : S8("fake-success-v1"),
        S8("1111111111111111111111111111111111111111"), S8("2222222222222222222222222222222222222222")};
    BqRequest request;
    BQ_CHECK(bq_request_make(fields, &request) == BQ_OK);
    return request;
}

BUSTER_GLOBAL_LOCAL BqRequest bq_test_real_request(u32 number)
{
    char key[32];
    snprintf(key, sizeof(key), "real-request-%u", number);
    String8 fields[BQ_FIELD_COUNT] = {
        S8("test-principal"), string_from_pointer(key), S8("validate-buster-v1"),
        S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")};
    BqRequest request;
    BQ_CHECK(bq_request_make(fields, &request) == BQ_OK);
    return request;
}

BUSTER_GLOBAL_LOCAL void bq_test_codec(void)
{
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqPacket request, response;
    bq_packet(&request, BQ_OP_CAPABILITIES, UINT64_MAX, NULL, 0);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_OK);
    BQ_CHECK(response.size <= BQ_CONTROL_CAP && bq_u64(response.bytes + 16) == UINT64_MAX);
    for (u32 prefix = 0; prefix < request.size; prefix += 1)
    {
        BQ_CHECK(bq_dispatch(&queue, request.bytes, prefix, &response) == BQ_BAD_REQUEST);
    }
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size + 1, &response) == BQ_BAD_REQUEST);
    bq_put32(request.bytes + 4, BQ_CONTROL_SCHEMA + 1);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
    bq_put32(request.bytes + 4, BQ_CONTROL_SCHEMA);
    bq_put32(request.bytes + 12, BQ_CONTROL_BODY + 1);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
    bq_packet(&request, BQ_OP_CAPABILITIES, 0, NULL, BQ_CONTROL_BODY + 1);
    BQ_CHECK(request.size == 0);
    bq_packet_schema(&request, 1, BQ_OP_CAPABILITIES, 42, NULL, 0);
    BQ_CHECK(bq_dispatch(&queue, request.bytes, request.size, &response) == BQ_OK && bq_u32(response.bytes + 4) == 1 &&
             response.size == BQ_CONTROL_HEADER + 4 + sizeof(bq_capabilities_v1) - 1);
    BqRequest valid = bq_test_request(1, false);
    for (u32 size = 0; size < valid.size; size += 1)
    {
        BqRequest shortened = valid;
        shortened.size = size;
        BQ_CHECK(!bq_request_valid(&shortened));
    }
    BqRequest malformed = valid;
    malformed.size += 1;
    BQ_CHECK(!bq_request_valid(&malformed));
    malformed = valid;
    bq_put32(malformed.bytes, UINT32_MAX);
    BQ_CHECK(!bq_request_valid(&malformed));
    malformed = valid;
    bq_field(&malformed, 3).pointer[0] = 'G';
    BQ_CHECK(!bq_request_valid(&malformed));
    malformed = valid;
    bq_field(&malformed, 1).pointer[0] = ';';
    BQ_CHECK(!bq_request_valid(&malformed));
    String8 fields[BQ_FIELD_COUNT] = {S8("p"), S8("k"), S8("sh"), S8("main"), S8("HEAD")};
    BQ_CHECK(bq_request_make(fields, &malformed) == BQ_BAD_REQUEST);
    fields[2] = S8("fake-success-v1");
    fields[3] = fields[4] = S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    BQ_CHECK(bq_request_make(fields, &malformed) == BQ_OK);
    fields[0] = (String8){0};
    BQ_CHECK(bq_request_make(fields, &malformed) == BQ_BAD_REQUEST);
    u64 value = 0;
    BQ_CHECK(bq_decimal("18446744073709551615", true, &value) && value == UINT64_MAX);
    BQ_CHECK(!bq_decimal("18446744073709551616", true, &value));
    BQ_CHECK(!bq_decimal("1x", true, &value));
    BQ_CHECK(!bq_decimal("-1", true, &value));
    BQ_CHECK(!bq_decimal("0", true, &value));
    BQ_CHECK(bq_decimal("0", false, &value) && value == 0);
    FILE* profile = fopen("tools/bench_service/profiles/validate-buster-v1.recipe", "rb");
    char recipe[sizeof(bq_real_recipe)] = {0};
    BQ_CHECK(profile && fread(recipe, 1, sizeof(recipe), profile) == sizeof(bq_real_recipe) - 1 &&
             !memcmp(recipe, bq_real_recipe, sizeof(bq_real_recipe)));
    if (profile)
    {
        fclose(profile);
    }
#ifdef _WIN32
    BQ_CHECK(bq_open(&queue, ".") == BQ_UNSUPPORTED);
#endif
}

#ifndef _WIN32
typedef struct BqFixture
{
    char path[80];
    BqQueue queue;
} BqFixture;

BUSTER_GLOBAL_LOCAL bool bq_test_begin(BqFixture* fixture)
{
    *fixture = (BqFixture){.queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1}};
    snprintf(fixture->path, sizeof(fixture->path), "/tmp/buster-queue-XXXXXX");
    bool ok = mkdtemp(fixture->path) != NULL;
    BQ_CHECK(ok);
    if (ok)
    {
        ok = bq_open(&fixture->queue, fixture->path) == BQ_OK;
        BQ_CHECK(ok);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_end(BqFixture* fixture)
{
    bq_close(&fixture->queue);
    int directory = open(fixture->path, O_RDONLY | O_DIRECTORY);
    BQ_CHECK(directory >= 0);
    if (directory >= 0)
    {
        /* Test-owned fixtures only; production never unlinks the stable lock. */
        DIR* stream = fdopendir(dup(directory));
        struct dirent* entry = NULL;
        while (stream && (entry = readdir(stream)) != NULL)
        {
            if (!strncmp(entry->d_name, "failure-", 8) || !strncmp(entry->d_name, "attempt-", 8) ||
                !strncmp(entry->d_name, "cleanup-", 8))
            {
                BQ_CHECK(unlinkat(directory, entry->d_name, 0) == 0);
            }
        }
        if (stream)
        {
            closedir(stream);
        }
        BQ_CHECK(unlinkat(directory, "journal", 0) == 0);
        BQ_CHECK(unlinkat(directory, "writer.lock", 0) == 0);
        close(directory);
        BQ_CHECK(rmdir(fixture->path) == 0);
    }
}

typedef struct BqMaterialFixture
{
    BqFixture queue;
    char installed[BQ_PATH_CAP + 1];
    char workspaces[BQ_PATH_CAP + 1];
} BqMaterialFixture;

BUSTER_GLOBAL_LOCAL bool bq_test_write_path(char const* path, char const* text, u32 mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
    u32 size = (u32)strlen(text);
    bool ok = fd >= 0 && bq_write_all(fd, (u8 const*)text, size) && fsync(fd) == 0;
    if (fd >= 0)
    {
        close(fd);
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_test_source(char const* installed, char const* revision, char const* contents, u32 defect)
{
    char sources[512], root[1024], source[2048], file[4096], manifest[2048];
    snprintf(sources, sizeof(sources), "%s/sources", installed);
    snprintf(root, sizeof(root), "%s/%s", sources, revision);
    snprintf(source, sizeof(source), "%s/src", root);
    snprintf(file, sizeof(file), "%s/main.c", source);
    snprintf(manifest, sizeof(manifest), "%s/source.manifest", root);
    bool file_ok = false;
    bool ok = (mkdir(sources, 0700) == 0 || errno == EEXIST) && mkdir(root, 0700) == 0 && mkdir(source, 0700) == 0;
    if (ok)
    {
        file_ok = defect == 8 ? mkfifo(file, 0400) == 0 : bq_test_write_path(file, contents, 0400);
        ok = file_ok;
    }
    char8 digest[SHA256_HEX_CAPACITY];
    if (ok)
    {
        bq_digest(contents, (u32)strlen(contents), digest);
        if (defect == 3)
        {
            digest[0] = digest[0] == '0' ? '1' : '0';
        }
        char text[1024];
        char const* identity = defect == 1 ? "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc" : revision;
        char const* path = defect == 2 ? "../escape.c" : "src/main.c";
        int length = snprintf(text, sizeof(text), "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n%.64s %s\n",
                              identity, digest, path);
        bool manifest_ok = defect == 7 ? mkfifo(manifest, 0400) == 0 : bq_test_write_path(manifest, text, 0400);
        ok = length > 0 && (u32)length < sizeof(text) && manifest_ok &&
             chmod(source, defect == 5 ? 0700 : 0500) == 0 && chmod(root, 0500) == 0;
    }
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_material_test_begin(BqMaterialFixture* fixture, u32 defect)
{
    *fixture = (BqMaterialFixture){0};
    snprintf(fixture->installed, sizeof(fixture->installed), "/tmp/buster-installed-XXXXXX");
    snprintf(fixture->workspaces, sizeof(fixture->workspaces), "/tmp/buster-workspaces-XXXXXX");
    bool ok = bq_test_begin(&fixture->queue) && mkdtemp(fixture->installed) && mkdtemp(fixture->workspaces);
    if (ok)
    {
        char recipes[512], recipe[1024];
        snprintf(recipes, sizeof(recipes), "%s/recipes", fixture->installed);
        snprintf(recipe, sizeof(recipe), "%s/validate-buster-v1.recipe", recipes);
        ok = mkdir(recipes, 0700) == 0 &&
             (defect == 6 ? mkfifo(recipe, 0400) == 0 :
              bq_test_write_path(recipe, defect == 4 ? "bad-recipe\n" : bq_real_recipe, 0400)) &&
             bq_test_source(fixture->installed, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "base-source\n", defect) &&
             bq_test_source(fixture->installed, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "candidate-source\n", 0) &&
             chmod(recipes, 0500) == 0;
        char sources[512];
        snprintf(sources, sizeof(sources), "%s/sources", fixture->installed);
        ok = ok && chmod(sources, 0500) == 0 && chmod(fixture->installed, 0500) == 0;
    }
    BQ_CHECK(ok);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_material_test_end(BqMaterialFixture* fixture)
{
    int temporary = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (temporary >= 0)
    {
        char const* installed = strrchr(fixture->installed, '/');
        char const* workspaces = strrchr(fixture->workspaces, '/');
        int installed_fd = installed ? openat(temporary, installed + 1, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        int workspaces_fd = workspaces ? openat(temporary, workspaces + 1, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        BQ_CHECK(installed_fd >= 0 && bq_remove_workspace_payload(installed_fd) &&
                 unlinkat(temporary, installed + 1, AT_REMOVEDIR) == 0);
        BQ_CHECK(workspaces_fd >= 0 && bq_remove_workspace_payload(workspaces_fd) &&
                 unlinkat(temporary, workspaces + 1, AT_REMOVEDIR) == 0);
        if (installed_fd >= 0)
        {
            close(installed_fd);
        }
        if (workspaces_fd >= 0)
        {
            close(workspaces_fd);
        }
        close(temporary);
    }
    bq_test_end(&fixture->queue);
}

BUSTER_GLOBAL_LOCAL void bq_test_materialization_failures(void)
{
    BqError expected[] = {BQ_OK, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH,
                          BQ_RECIPE_MISMATCH, BQ_SOURCE_MISMATCH, BQ_RECIPE_MISMATCH,
                          BQ_SOURCE_MISMATCH, BQ_SOURCE_MISMATCH};
    for (u32 defect = 1; defect < BUSTER_ARRAY_LENGTH(expected); defect += 1)
    {
        BqMaterialFixture fixture;
        if (bq_material_test_begin(&fixture, defect))
        {
            BqRequest request = bq_test_real_request(defect);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed), string_from_pointer(fixture.workspaces),
                                    &id, &token) == expected[defect]);
            BqJob* job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED && !fixture.queue.queue.state.active_id);
            BQ_CHECK(bq_failure_evidence(&fixture.queue.queue, job) == expected[defect]);
            u8 status_body[8];
            BqPacket status, response;
            bq_put64(status_body, id);
            bq_packet(&status, BQ_OP_STATUS, 80 + defect, status_body, sizeof(status_body));
            BQ_CHECK(bq_dispatch(&fixture.queue.queue, status.bytes, status.size, &response) == BQ_OK &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) == expected[defect]);
            bq_packet(&status, BQ_OP_RESULT, 90 + defect, status_body, sizeof(status_body));
            BQ_CHECK(bq_dispatch(&fixture.queue.queue, status.bytes, status.size, &response) == BQ_OK &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) == expected[defect]);
            char name[64], path[512];
            BQ_CHECK(bq_workspace_name(name, id, token));
            snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
            BQ_CHECK(access(path, F_OK) != 0 && errno == ENOENT);
            bq_close(&fixture.queue.queue);
            BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK);
            job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(job && job->outcome == BQ_FAILED && bq_failure_evidence(&fixture.queue.queue, job) == expected[defect]);
            if (defect == 1)
            {
                char failure_name[48];
                BQ_CHECK(bq_failure_name(failure_name, id));
                int failure = openat(fixture.queue.queue.directory_fd, failure_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
                BQ_CHECK(failure >= 0 && fchmod(failure, 0600) == 0);
                BQ_CHECK(bq_failure_evidence(&fixture.queue.queue, job) == BQ_CORRUPT);
                bq_packet(&status, BQ_OP_STATUS, 100, status_body, sizeof(status_body));
                BQ_CHECK(bq_dispatch(&fixture.queue.queue, status.bytes, status.size, &response) == BQ_CORRUPT &&
                         bq_u32(response.bytes + BQ_CONTROL_HEADER) == BQ_CORRUPT &&
                         bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) == BQ_CORRUPT);
                BQ_CHECK(failure >= 0 && fchmod(failure, 0400) == 0);
                if (failure >= 0)
                {
                    close(failure);
                }
            }
            if (defect == 2)
            {
                char failure_name[48];
                BQ_CHECK(bq_failure_name(failure_name, id) &&
                         unlinkat(fixture.queue.queue.directory_fd, failure_name, 0) == 0 &&
                         fsync(fixture.queue.queue.directory_fd) == 0);
                bq_packet(&status, BQ_OP_RESULT, 101, status_body, sizeof(status_body));
                BQ_CHECK(bq_dispatch(&fixture.queue.queue, status.bytes, status.size, &response) == BQ_CORRUPT &&
                         bq_failure_evidence(&fixture.queue.queue, job) == BQ_CORRUPT);
            }
            bq_material_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_materialization_and_recovery(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(1);
        BqPacket legacy, legacy_response;
        bq_packet_schema(&legacy, 1, BQ_OP_SUBMIT, 90, request.bytes, request.size);
        BQ_CHECK(bq_dispatch(&fixture.queue.queue, legacy.bytes, legacy.size, &legacy_response) == BQ_BAD_REQUEST &&
                 fixture.queue.queue.state.job_count == 0 && legacy_response.size == BQ_CONTROL_HEADER + 120);
        u64 id = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        u8 legacy_body[16] = {0};
        bq_put64(legacy_body, id);
        bq_packet_schema(&legacy, 1, BQ_OP_STATUS, 91, legacy_body, 8);
        BQ_CHECK(bq_dispatch(&fixture.queue.queue, legacy.bytes, legacy.size, &legacy_response) == BQ_UNSUPPORTED &&
                 legacy_response.size == BQ_CONTROL_HEADER + 120);
        bq_packet_schema(&legacy, 1, BQ_OP_LOGS, 92, legacy_body, 16);
        BQ_CHECK(bq_dispatch(&fixture.queue.queue, legacy.bytes, legacy.size, &legacy_response) == BQ_UNSUPPORTED);
        BQ_CHECK(bq_fake_run(&fixture.queue.queue, &id) == BQ_UNSUPPORTED && !fixture.queue.queue.state.active_id);
        u8 body[BQ_CONTROL_BODY];
        String8 installed = string_from_pointer(fixture.installed), workspaces = string_from_pointer(fixture.workspaces);
        bq_put32(body, (u32)installed.length);
        bq_put32(body + 4, (u32)workspaces.length);
        memcpy(body + 8, installed.pointer, (size_t)installed.length);
        memcpy(body + 8 + installed.length, workspaces.pointer, (size_t)workspaces.length);
        BqPacket packet, response;
        bq_packet(&packet, BQ_OP_MATERIALIZE, 91, body, 8 + (u32)installed.length + (u32)workspaces.length);
        BQ_CHECK(bq_dispatch(&fixture.queue.queue, packet.bytes, packet.size, &response) == BQ_OK);
        id = bq_u64(response.bytes + BQ_CONTROL_HEADER + 4);
        u64 token = bq_u64(response.bytes + BQ_CONTROL_HEADER + 12);
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_PREPARING && fixture.queue.queue.state.active_id == id && token == job->token);
        BQ_CHECK(bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) == BQ_OK);
        char name[64], workspace[512], base_source[1024], candidate_source[1024];
        BQ_CHECK(bq_workspace_name(name, id, token));
        snprintf(workspace, sizeof(workspace), "%s/%s", fixture.workspaces, name);
        snprintf(base_source, sizeof(base_source), "%s/base/source/src/main.c", workspace);
        snprintf(candidate_source, sizeof(candidate_source), "%s/candidate/source/src/main.c", workspace);
        struct stat base, candidate;
        BQ_CHECK(stat(base_source, &base) == 0 && stat(candidate_source, &candidate) == 0 && base.st_ino != candidate.st_ino);
        BQ_CHECK((base.st_mode & 0222) == 0 && (candidate.st_mode & 0222) == 0);
        BQ_CHECK(chmod(base_source, 0600) == 0);
        int same_euid_mutation = open(base_source, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(same_euid_mutation >= 0 && bq_write_all(same_euid_mutation, (u8 const*)"mutated\n", 8) &&
                 fsync(same_euid_mutation) == 0);
        if (same_euid_mutation >= 0)
        {
            close(same_euid_mutation);
        }
        BQ_CHECK(chmod(base_source, 0400) == 0);
        char copied_manifest[1024];
        snprintf(copied_manifest, sizeof(copied_manifest), "%s/base/source/.source-manifest", workspace);
        BQ_CHECK(stat(copied_manifest, &base) == 0 && (base.st_mode & 0222) == 0);
        char identity[1024];
        snprintf(identity, sizeof(identity), "%s/.identity", workspace);
        BQ_CHECK(stat(identity, &base) == 0 && (base.st_mode & 0222) == 0);
        char base_build[1024], candidate_build[1024], sentinel[2048];
        snprintf(base_build, sizeof(base_build), "%s/base/build", workspace);
        snprintf(candidate_build, sizeof(candidate_build), "%s/candidate/build", workspace);
        BQ_CHECK(stat(base_build, &base) == 0 && stat(candidate_build, &candidate) == 0 && base.st_ino != candidate.st_ino);
        snprintf(sentinel, sizeof(sentinel), "%s/generated", base_build);
        BQ_CHECK(bq_test_write_path(sentinel, "base-only\n", 0600));
        snprintf(sentinel, sizeof(sentinel), "%s/generated", candidate_build);
        BQ_CHECK(access(sentinel, F_OK) != 0 && errno == ENOENT);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_fake_reconcile(&fixture.queue.queue, id, token) == BQ_UNSUPPORTED);
        BQ_CHECK(chmod(identity, 0600) == 0);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, workspaces, id, token) == BQ_WORKSPACE_MISMATCH);
        BQ_CHECK(fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation && access(workspace, F_OK) == 0);
        BQ_CHECK(chmod(identity, 0400) == 0);
        bq_put64(body, id);
        bq_put64(body + 8, token);
        bq_put32(body + 16, (u32)workspaces.length);
        memcpy(body + 20, workspaces.pointer, (size_t)workspaces.length);
        bq_packet(&packet, BQ_OP_WORKSPACE_RECONCILE, 92, body, 20 + (u32)workspaces.length);
        BqError reconcile_error = bq_dispatch(&fixture.queue.queue, packet.bytes, packet.size, &response);
        if (reconcile_error != BQ_OK)
        {
            fprintf(stderr, "QUEUE_TEST reconciliation error=%s\n", bq_error_name(reconcile_error));
        }
        BQ_CHECK(reconcile_error == BQ_OK);
        job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_INTERRUPTED && !fixture.queue.queue.state.active_id);
        BQ_CHECK(access(workspace, F_OK) != 0 && errno == ENOENT);
        BQ_CHECK(bq_failure_evidence(&fixture.queue.queue, job) == BQ_NOT_FOUND);
        BqRequest second = bq_test_real_request(2);
        u64 second_id = 0, second_token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &second, &second_id) == BQ_OK && second_id != id);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, installed, workspaces, &second_id, &second_token) == BQ_OK);
        char second_name[64], second_workspace[512];
        BQ_CHECK(bq_workspace_name(second_name, second_id, second_token) && strcmp(name, second_name));
        snprintf(second_workspace, sizeof(second_workspace), "%s/%s", fixture.workspaces, second_name);
        BQ_CHECK(access(second_workspace, F_OK) == 0 && access(workspace, F_OK) != 0);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, workspaces, second_id, second_token) == BQ_INVALID_TRANSITION);
        BQ_CHECK(fixture.queue.queue.state.active_id == second_id && !fixture.queue.queue.needs_reconciliation &&
                 access(second_workspace, F_OK) == 0);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, workspaces, second_id, second_token) == BQ_OK);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_workspace_collision(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(1);
        u64 id = 0, token = 2;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK && id == 1);
        char name[64], path[512];
        BQ_CHECK(bq_workspace_name(name, id, token));
        snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
        BQ_CHECK(mkdir(path, 0700) == 0);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed), string_from_pointer(fixture.workspaces),
                                &id, &token) == BQ_WORKSPACE_MISMATCH);
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_RESERVED && fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_failure_evidence(&fixture.queue.queue, job) == BQ_WORKSPACE_MISMATCH);
        BQ_CHECK(rmdir(path) == 0);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED && !fixture.queue.queue.state.active_id);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_materialization_configuration(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(1);
        u64 id = 0, token = 0, expected_id = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &expected_id) == BQ_OK);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, S8("relative-installed"), string_from_pointer(fixture.workspaces),
                                &id, &token) == BQ_BAD_REQUEST);
        BqJob* job = bq_job(&fixture.queue.queue.state, expected_id);
        BQ_CHECK(job && job->phase == BQ_QUEUED && !fixture.queue.queue.state.active_id);
        char alias[80] = "/tmp/buster-installed-alias-XXXXXX";
        bool alias_ok = mkdtemp(alias) && rmdir(alias) == 0 && symlink("/tmp", alias) == 0;
        char const* leaf = strrchr(fixture.installed, '/');
        char installed_through_alias[512];
        int length = snprintf(installed_through_alias, sizeof(installed_through_alias), "%s/%s", alias, leaf ? leaf + 1 : "");
        BQ_CHECK(alias_ok && leaf && length > 0 && (u32)length < sizeof(installed_through_alias));
        if (alias_ok && leaf && length > 0 && (u32)length < sizeof(installed_through_alias))
        {
            BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(installed_through_alias),
                                    string_from_pointer(fixture.workspaces), &id, &token) == BQ_CONFIGURATION_MISMATCH);
            job = bq_job(&fixture.queue.queue.state, expected_id);
            BQ_CHECK(id == expected_id && job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                     !fixture.queue.queue.state.active_id && !fixture.queue.queue.needs_reconciliation);
            BQ_CHECK(bq_failure_evidence(&fixture.queue.queue, job) == BQ_CONFIGURATION_MISMATCH);
        }
        if (alias_ok)
        {
            BQ_CHECK(unlink(alias) == 0);
        }
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_workspace_entry_mismatch(void)
{
    for (u32 scenario = 0; scenario < 2; scenario += 1)
    {
        BqMaterialFixture fixture;
        if (bq_material_test_begin(&fixture, 0))
        {
            BqRequest request = bq_test_real_request(20 + scenario);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                                    string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
            char name[64], path[512], saved[512];
            BQ_CHECK(bq_workspace_name(name, id, token));
            snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
            snprintf(saved, sizeof(saved), "%s/saved-%u", fixture.workspaces, scenario);
            bq_close(&fixture.queue.queue);
            BQ_CHECK(rename(path, saved) == 0);
            if (scenario == 0)
            {
                BQ_CHECK(symlink(saved, path) == 0);
            }
            else
            {
                BQ_CHECK(bq_test_write_path(path, "not-a-directory\n", 0600));
            }
            BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
            BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) ==
                     BQ_WORKSPACE_MISMATCH);
            BQ_CHECK(fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation &&
                     access(path, F_OK) == 0 && access(saved, F_OK) == 0);
            BQ_CHECK(unlink(path) == 0 && rename(saved, path) == 0);
            BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
            bq_material_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_workspace_root_substitution(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(30);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                                string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
        char original[BQ_PATH_CAP + 32];
        snprintf(original, sizeof(original), "%s-original", fixture.workspaces);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(rename(fixture.workspaces, original) == 0 && mkdir(fixture.workspaces, 0700) == 0);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) ==
                 BQ_WORKSPACE_MISMATCH);
        BQ_CHECK(fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(rmdir(fixture.workspaces) == 0 && rename(original, fixture.workspaces) == 0);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_partial_cleanup_recovery(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(40);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                                string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
        char name[64], path[512], saved[512];
        BQ_CHECK(bq_workspace_name(name, id, token));
        snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
        snprintf(saved, sizeof(saved), "%s/original-workspace", fixture.workspaces);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        int root = open(fixture.workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int workspace = root >= 0 ? openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        struct stat root_info, workspace_info;
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(root >= 0 && workspace >= 0 && fstat(root, &root_info) == 0 && fstat(workspace, &workspace_info) == 0 &&
                 bq_cleanup_record(&fixture.queue.queue, job, &root_info, &workspace_info, true) == BQ_OK);
        if (workspace >= 0)
        {
            close(workspace);
        }
        BQ_CHECK(rename(path, saved) == 0 && mkdir(path, 0700) == 0);
        int replacement = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(replacement >= 0 && bq_workspace_seal(replacement, job, true));
        if (replacement >= 0)
        {
            close(replacement);
        }
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) ==
                 BQ_WORKSPACE_MISMATCH);
        replacement = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(replacement >= 0 && bq_remove_workspace_payload(replacement) && unlinkat(replacement, ".identity", 0) == 0);
        if (replacement >= 0)
        {
            close(replacement);
        }
        BQ_CHECK(rmdir(path) == 0 && rename(saved, path) == 0);
        workspace = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(workspace >= 0 && bq_remove_workspace_payload(workspace) && unlinkat(workspace, ".identity", 0) == 0 &&
                 fsync(workspace) == 0);
        if (workspace >= 0)
        {
            close(workspace);
        }
        if (root >= 0)
        {
            close(root);
        }
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        BQ_CHECK(access(path, F_OK) != 0 && errno == ENOENT && !fixture.queue.queue.state.active_id);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_uncertain_failure_cleanup(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(50);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_reserve(&fixture.queue.queue, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        int installed = bq_open_absolute_directory(string_from_pointer(fixture.installed));
        int root = bq_open_absolute_directory(string_from_pointer(fixture.workspaces));
        struct stat installed_info, root_info, workspace_info;
        char name[64], path[512];
        BQ_CHECK(installed >= 0 && root >= 0 && fstat(installed, &installed_info) == 0 && fstat(root, &root_info) == 0 &&
                 bq_workspace_name(name, id, token));
        BQ_CHECK(bq_attempt_write(&fixture.queue.queue, job, string_from_pointer(fixture.installed),
                                  string_from_pointer(fixture.workspaces), &installed_info, &root_info) == BQ_OK);
        BQ_CHECK(mkdirat(root, name, 0700) == 0);
        int workspace = openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(workspace >= 0 && fstat(workspace, &workspace_info) == 0 && bq_workspace_seal(workspace, job, true));
        BQ_CHECK(mkdirat(workspace, "partial-copy", 0700) == 0);
        fixture.queue.queue.fault.fail_write_at = 2;
        BQ_CHECK(bq_materialization_finish_failure(&fixture.queue.queue, job, BQ_SOURCE_MISMATCH, root, name, workspace,
                                                    &root_info, &workspace_info, true) == BQ_IO);
        snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
        BQ_CHECK(fixture.queue.queue.poisoned && fixture.queue.queue.state.active_id == id && access(path, F_OK) == 0);
        if (workspace >= 0)
        {
            close(workspace);
        }
        if (root >= 0)
        {
            close(root);
        }
        if (installed >= 0)
        {
            close(installed);
        }
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation &&
                 fixture.queue.queue.recovered_tail_bytes == 1);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                 bq_failure_evidence(&fixture.queue.queue, job) == BQ_SOURCE_MISMATCH && access(path, F_OK) != 0);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_no_attempt_recovery(void)
{
    /* The RESERVE itself may be the last durable action before a crash. */
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        BqRequest request = bq_test_real_request(55);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_reserve(&fixture.queue.queue, &id, &token) == BQ_OK);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation &&
                 fixture.queue.queue.state.active_id == id && bq_job(&fixture.queue.queue.state, id)->phase == BQ_RESERVED);
        char name[64], path[512];
        BQ_CHECK(bq_workspace_name(name, id, token));
        snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
        BQ_CHECK(bq_test_write_path(path, "unexpected-entry\n", 0600));
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) ==
                 BQ_WORKSPACE_MISMATCH && fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(unlink(path) == 0);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_INTERRUPTED &&
                 !fixture.queue.queue.state.active_id && !fixture.queue.queue.needs_reconciliation);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_configuration_transition_recovery(void)
{
    /* Reproduce uncertainty at each journal transition used after root
     * configuration fails, before an attempt/workspace can exist. */
    for (u32 scenario = 0; scenario < 2; scenario += 1)
    {
        BqMaterialFixture fixture;
        if (bq_material_test_begin(&fixture, 0))
        {
            BqRequest request = bq_test_real_request(56 + scenario);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_reserve(&fixture.queue.queue, &id, &token) == BQ_OK);
            BqJob* job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(bq_failure_write(&fixture.queue.queue, job, BQ_CONFIGURATION_MISMATCH) == BQ_OK);
            if (scenario == 1)
            {
                BQ_CHECK(bq_real_advance(&fixture.queue.queue, job, BQ_CLEANING, BQ_FAILED) == BQ_OK);
                job = bq_job(&fixture.queue.queue.state, id);
            }
            fixture.queue.queue.fault.fail_write_at = 2;
            BQ_CHECK(bq_real_advance(&fixture.queue.queue, job, scenario ? BQ_FINISHED : BQ_CLEANING, BQ_FAILED) == BQ_IO &&
                     fixture.queue.queue.poisoned && fixture.queue.queue.state.active_id == id);
            bq_close(&fixture.queue.queue);
            BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation &&
                     fixture.queue.queue.recovered_tail_bytes == 1);
            job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(job && job->phase == (scenario ? BQ_CLEANING : BQ_RESERVED));
            BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, S8("/definitely-missing-buster-workspace-root"), id, token) == BQ_OK);
            job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                     bq_failure_evidence(&fixture.queue.queue, job) == BQ_CONFIGURATION_MISMATCH &&
                     !fixture.queue.queue.state.active_id);
            bq_material_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_cancelled_failure_recovery(void)
{
    for (u32 attempted = 0; attempted < 3; attempted += 1)
    {
        BqMaterialFixture fixture;
        if (bq_material_test_begin(&fixture, 0))
        {
            BqRequest request = bq_test_real_request(58 + attempted);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_reserve(&fixture.queue.queue, &id, &token) == BQ_OK);
            BqJob* job = bq_job(&fixture.queue.queue.state, id);
            int installed = -1, root = -1, workspace = -1;
            struct stat installed_info = {0}, root_info = {0}, workspace_info = {0};
            char name[64] = {0}, path[512] = {0};
            if (attempted)
            {
                installed = bq_open_absolute_directory(string_from_pointer(fixture.installed));
                root = bq_open_absolute_directory(string_from_pointer(fixture.workspaces));
                BQ_CHECK(installed >= 0 && root >= 0 && fstat(installed, &installed_info) == 0 &&
                         fstat(root, &root_info) == 0 && bq_workspace_name(name, id, token));
                BQ_CHECK(bq_attempt_write(&fixture.queue.queue, job, string_from_pointer(fixture.installed),
                                          string_from_pointer(fixture.workspaces), &installed_info, &root_info) == BQ_OK);
                BQ_CHECK(mkdirat(root, name, 0700) == 0);
                workspace = openat(root, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                BQ_CHECK(workspace >= 0 && fstat(workspace, &workspace_info) == 0 &&
                         bq_workspace_seal(workspace, job, true));
                snprintf(path, sizeof(path), "%s/%s", fixture.workspaces, name);
            }
            BqError reason = attempted ? BQ_SOURCE_MISMATCH : BQ_CONFIGURATION_MISMATCH;
            BQ_CHECK(bq_failure_write(&fixture.queue.queue, job, reason) == BQ_OK);
            if (attempted == 2)
            {
                BQ_CHECK(bq_cleanup_record(&fixture.queue.queue, job, &root_info, &workspace_info, true) == BQ_OK);
                BQ_CHECK(bq_real_advance(&fixture.queue.queue, job, BQ_CLEANING, BQ_FAILED) == BQ_OK);
            }
            if (workspace >= 0)
            {
                close(workspace);
            }
            if (root >= 0)
            {
                close(root);
            }
            if (installed >= 0)
            {
                close(installed);
            }
            bq_close(&fixture.queue.queue);
            BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
            BQ_CHECK(bq_cancel(&fixture.queue.queue, id) == BQ_OK && bq_job(&fixture.queue.queue.state, id)->cancel_requested);
            String8 workspace_root = attempted ? string_from_pointer(fixture.workspaces) :
                                     S8("/definitely-missing-buster-workspace-root");
            BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, workspace_root, id, token) == BQ_OK);
            job = bq_job(&fixture.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED &&
                     bq_failure_evidence(&fixture.queue.queue, job) == reason && !fixture.queue.queue.state.active_id);
            if (attempted)
            {
                BQ_CHECK(access(path, F_OK) != 0 && errno == ENOENT);
            }
            bq_material_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_cleanup_bounds_and_failure(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 0))
    {
        int root = open(fixture.workspaces, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(root >= 0 && mkdirat(root, "bounded-topology", 0700) == 0);
        int topology = root >= 0 ? openat(root, "bounded-topology", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        u32 directories = 1;
        for (u32 i = 0; i < BQ_SOURCE_DIRECTORY_CAP - 1; i += 1)
        {
            char path[32];
            snprintf(path, sizeof(path), "d%03u", i);
            int directory = bq_open_destination_directory(topology, string_from_pointer(path), &directories);
            BQ_CHECK(directory >= 0);
            if (directory >= 0)
            {
                close(directory);
            }
        }
        BQ_CHECK(directories == BQ_SOURCE_DIRECTORY_CAP);
        int overflow = bq_open_destination_directory(topology, S8("overflow"), &directories);
        BQ_CHECK(overflow < 0 && directories == BQ_SOURCE_DIRECTORY_CAP && bq_remove_workspace_payload(topology));
        if (overflow >= 0)
        {
            close(overflow);
        }
        if (topology >= 0)
        {
            close(topology);
        }
        BQ_CHECK(unlinkat(root, "bounded-topology", AT_REMOVEDIR) == 0);
        BQ_CHECK(mkdirat(root, "deep-topology", 0700) == 0);
        int deep = openat(root, "deep-topology", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool deep_ok = deep >= 0;
        for (u32 i = 0; deep_ok && i < 220; i += 1)
        {
            deep_ok = mkdirat(deep, "child", 0700) == 0;
            int next = deep_ok ? openat(deep, "child", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
            deep_ok = next >= 0;
            close(deep);
            deep = next;
        }
        BQ_CHECK(deep_ok);
        if (deep >= 0)
        {
            close(deep);
        }
        deep = openat(root, "deep-topology", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_CHECK(deep >= 0 && bq_remove_workspace_payload(deep));
        if (deep >= 0)
        {
            close(deep);
        }
        BQ_CHECK(unlinkat(root, "deep-topology", AT_REMOVEDIR) == 0);
        BqRequest request = bq_test_real_request(60);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                                string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
        char name[64], build[512];
        BQ_CHECK(bq_workspace_name(name, id, token));
        snprintf(build, sizeof(build), "%s/%s/base/build", fixture.workspaces, name);
        int build_fd = open(build, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool created = build_fd >= 0;
        for (u32 i = 0; created && i < BQ_DIRECTORY_CAP + 1; i += 1)
        {
            char child[32];
            snprintf(child, sizeof(child), "overflow-%04u", i);
            created = mkdirat(build_fd, child, 0700) == 0;
        }
        BQ_CHECK(created);
        if (build_fd >= 0)
        {
            close(build_fd);
        }
        if (root >= 0)
        {
            close(root);
        }
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) ==
                 BQ_CLEANUP_FAILED);
        BqJob* job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(job && fixture.queue.queue.state.active_id == id && fixture.queue.queue.needs_reconciliation &&
                 bq_failure_evidence(&fixture.queue.queue, job) == BQ_CLEANUP_FAILED);
        u8 body[8];
        bq_put64(body, id);
        BqPacket packet, response;
        bq_packet(&packet, BQ_OP_STATUS, 150, body, sizeof(body));
        BQ_CHECK(bq_dispatch(&fixture.queue.queue, packet.bytes, packet.size, &response) == BQ_OK &&
                 bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) == BQ_CLEANUP_FAILED);
        bq_close(&fixture.queue.queue);
        BQ_CHECK(bq_open(&fixture.queue.queue, fixture.queue.path) == BQ_OK && fixture.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_workspace_reconcile(&fixture.queue.queue, string_from_pointer(fixture.workspaces), id, token) == BQ_OK);
        job = bq_job(&fixture.queue.queue.state, id);
        BQ_CHECK(!fixture.queue.queue.state.active_id && bq_failure_evidence(&fixture.queue.queue, job) == BQ_CLEANUP_FAILED);
        bq_material_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_image(BqFixture* fixture, u8 const* image, u32 size)
{
    bq_close(&fixture->queue);
    int directory = open(fixture->path, O_RDONLY | O_DIRECTORY);
    int fd = directory >= 0 ? openat(directory, "journal", O_WRONLY | O_TRUNC) : -1;
    BQ_CHECK(fd >= 0);
    if (fd >= 0)
    {
        u32 done = 0;
        bool ok = true;
        while (ok && done < size)
        {
            ssize_t n = write(fd, image + done, size - done);
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            ok = n > 0;
            if (ok)
            {
                done += (u32)n;
            }
        }
        BQ_CHECK(ok && fsync(fd) == 0);
        close(fd);
    }
    if (directory >= 0)
    {
        close(directory);
    }
}

BUSTER_GLOBAL_LOCAL bool bq_test_legacy_replay(u8 const* image, u32 size)
{
    BqState state = {0};
    u32 offset = 0;
    bool ok = true;
    while (ok && offset < size)
    {
        ok = size - offset >= BQ_HEADER_SIZE;
        u8 const* frame = image + offset;
        u32 length = ok ? bq_u32(frame + 16) : 0;
        char8 digest[SHA256_HEX_CAPACITY];
        if (ok)
        {
            bq_header_digest(frame, digest);
            ok = !memcmp(frame, "BQJNL001", 8) && bq_u32(frame + 8) == BQ_SCHEMA_LEGACY &&
                 length <= BQ_REQUEST_CAP && size - offset - BQ_HEADER_SIZE >= length &&
                 !memcmp(frame + 32, digest, 64);
        }
        if (ok)
        {
            bq_digest(frame + BQ_HEADER_SIZE, length, digest);
            ok = !memcmp(frame + 96, digest, 64) &&
                 bq_apply(&state, BQ_SCHEMA_LEGACY, (BqRecordKind)bq_u32(frame + 12), bq_u64(frame + 24),
                          frame + BQ_HEADER_SIZE, length) == BQ_OK;
        }
        if (ok)
        {
            offset += BQ_HEADER_SIZE + length;
        }
    }
    return ok && offset == size;
}

BUSTER_GLOBAL_LOCAL void bq_test_journal_schema_migration(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqRequest legacy = bq_test_request(1, false);
        u8 image[BQ_RECORD_CAP * 3];
        bq_frame_schema(image, BQ_SCHEMA_LEGACY, BQ_SUBMIT, 1, legacy.bytes, legacy.size);
        u32 legacy_size = BQ_HEADER_SIZE + legacy.size;
        BQ_CHECK(bq_test_legacy_replay(image, legacy_size));
        bq_test_image(&fixture, image, legacy_size);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK && fixture.queue.state.job_count == 1 &&
                 fixture.queue.state.journal_schema == BQ_SCHEMA_LEGACY && fixture.queue.bytes == legacy_size);
        BqRequest second = bq_test_request(2, false);
        u64 id = 0;
        BQ_CHECK(bq_submit(&fixture.queue, &second, &id) == BQ_OK && id == 2 &&
                 fixture.queue.state.journal_schema == BQ_SCHEMA);
        u32 mixed_size = (u32)fixture.queue.bytes;
        BQ_CHECK(bq_read(fixture.queue.journal_fd, image, mixed_size, 0));
        BQ_CHECK(bq_u32(image + 8) == BQ_SCHEMA_LEGACY && bq_u32(image + legacy_size + 8) == BQ_SCHEMA);
        BQ_CHECK(!bq_test_legacy_replay(image, mixed_size));
        u8 status_body[8];
        bq_put64(status_body, 1);
        BqPacket status, response;
        bq_packet_schema(&status, 1, BQ_OP_STATUS, 70, status_body, sizeof(status_body));
        BQ_CHECK(bq_dispatch(&fixture.queue, status.bytes, status.size, &response) == BQ_OK &&
                 response.size == BQ_CONTROL_HEADER + 120);
        bq_close(&fixture.queue);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK && fixture.queue.state.job_count == 2 &&
                 fixture.queue.state.journal_schema == BQ_SCHEMA);
        bq_frame_schema(image, BQ_SCHEMA, BQ_SUBMIT, 1, legacy.bytes, legacy.size);
        u32 current_size = BQ_HEADER_SIZE + legacy.size;
        bq_frame_schema(image + current_size, BQ_SCHEMA_LEGACY, BQ_SUBMIT, 2, second.bytes, second.size);
        bq_test_image(&fixture, image, current_size + BQ_HEADER_SIZE);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_CORRUPT);
        bq_test_image(&fixture, image, current_size + BQ_HEADER_SIZE + second.size);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_CORRUPT);
        BqRequest real = bq_test_real_request(3);
        bq_frame_schema(image, BQ_SCHEMA_LEGACY, BQ_SUBMIT, 1, real.bytes, real.size);
        u32 real_size = BQ_HEADER_SIZE + real.size;
        BQ_CHECK(!bq_test_legacy_replay(image, real_size));
        bq_test_image(&fixture, image, real_size);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_CORRUPT);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_closed_handle(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqRequest request = bq_test_request(1, false);
        u64 id = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_cancel(queue, id) == BQ_OK);
        bq_close(queue);
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_IO && id == 0);
        BQ_CHECK(bq_cancel(queue, 1) == BQ_IO);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK && id == 1);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_admission(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqQueue contender;
        BQ_CHECK(bq_open(&contender, fixture.path) == BQ_BUSY);
        BqRequest first = bq_test_request(0, false);
        u64 first_id = 0;
        u64 id = 0;
        u64 token = 0;
        BQ_CHECK(bq_submit(queue, &first, &first_id) == BQ_OK && first_id == 1);
        for (u32 i = 1; i < BQ_PENDING_CAP; i += 1)
        {
            BqRequest request = bq_test_request(i, false);
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
        }
        BqRequest extra = bq_test_request(BQ_PENDING_CAP, false);
        BQ_CHECK(bq_submit(queue, &extra, &id) == BQ_FULL);
        u64 sequence = queue->state.sequence;
        BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK && id == first_id && queue->state.sequence == sequence);
        BqRequest conflict = first;
        bq_field(&conflict, 4).pointer[0] = '3';
        BQ_CHECK(bq_submit(queue, &conflict, &id) == BQ_CONFLICT && queue->state.sequence == sequence);
        BQ_CHECK(bq_cancel(queue, first_id) == BQ_OK);
        sequence = queue->state.sequence;
        BQ_CHECK(bq_cancel(queue, first_id) == BQ_OK && queue->state.sequence == sequence);
        BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK && id == first_id);
        BqRequest other_principal = first;
        bq_field(&other_principal, 0).pointer[0] = 'z';
        BQ_CHECK(bq_submit(queue, &other_principal, &id) == BQ_OK && id != first_id);
        BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_OK && id == 2);
        u64 ignored_id = 0, ignored_token = 0;
        BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_BUSY);
        BQ_CHECK(bq_cancel(queue, id) == BQ_OK && queue->state.active_id == id);
        BQ_CHECK(bq_fake_step(queue, id, token) == BQ_OK);
        BQ_CHECK(bq_job(&queue->state, id)->phase == BQ_CLEANING);
        BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_BUSY);
        BQ_CHECK(bq_fake_step(queue, id, token) == BQ_OK);
        BQ_CHECK(bq_job(&queue->state, id)->outcome == BQ_CANCELLED && !queue->state.active_id);
        BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_OK && id == 3);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->needs_reconciliation);
        BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_RECONCILIATION_REQUIRED);
        BQ_CHECK(bq_fake_reconcile(queue, id, token + 1) == BQ_INVALID_TRANSITION && queue->needs_reconciliation);
        BQ_CHECK(bq_fake_reconcile(queue, id, token) == BQ_OK);
        BQ_CHECK(bq_job(&queue->state, id)->outcome == BQ_INTERRUPTED);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_prefixes_and_corruption(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqRequest first = bq_test_request(1, false);
        BqRequest second = bq_test_request(2, false);
        u64 id = 0;
        BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK);
        u32 stable = (u32)queue->bytes;
        queue->fault.before_sync = true;
        BQ_CHECK(bq_submit(queue, &second, &id) == BQ_IO && !id && queue->poisoned);
        u32 final_size = BQ_HEADER_SIZE + second.size;
        u8 image[BQ_RECORD_CAP * 2];
        BQ_CHECK(bq_read(queue->journal_fd, image, stable + final_size, 0));
        for (u32 prefix = 0; prefix < final_size; prefix += 1)
        {
            bq_test_image(&fixture, image, stable + prefix);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            BQ_CHECK(queue->state.job_count == 1 && queue->bytes == stable && queue->recovered_tail_bytes == prefix);
            BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK && id == 1);
        }
        /* A fully present but unacknowledged record may survive pre-sync crash. */
        bq_test_image(&fixture, image, stable + final_size);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->state.job_count == 2);
        BQ_CHECK(bq_submit(queue, &second, &id) == BQ_OK && id == 2);
        u32 offsets[] = {0, 8, 12, 16, 20, 24, 32, 96, BQ_HEADER_SIZE, stable - 1};
        for (u32 i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i += 1)
        {
            u8 corrupt[BQ_RECORD_CAP];
            memcpy(corrupt, image, stable);
            corrupt[offsets[i]] ^= 1;
            bq_test_image(&fixture, corrupt, stable);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_CORRUPT);
            int directory = open(fixture.path, O_RDONLY | O_DIRECTORY);
            struct stat info;
            BQ_CHECK(directory >= 0 && fstatat(directory, "journal", &info, 0) == 0 && (u64)info.st_size == stable);
            if (directory >= 0)
            {
                close(directory);
            }
        }
        /* Valid checksum does not bypass bounds, sequence or transition checks. */
        u8 corrupt[BQ_RECORD_CAP * 2];
        char8 digest[SHA256_HEX_CAPACITY];
        memcpy(corrupt, image, stable);
        bq_put32(corrupt + 16, BQ_REQUEST_CAP + 1);
        bq_header_digest(corrupt, digest);
        memcpy(corrupt + 32, digest, 64);
        bq_test_image(&fixture, corrupt, stable);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_CORRUPT);
        memcpy(corrupt, image, stable);
        memcpy(corrupt + stable, image, stable);
        bq_test_image(&fixture, corrupt, stable * 2);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_CORRUPT);
        memcpy(corrupt, image, stable);
        u8 invalid_reservation[16] = {0};
        bq_put64(invalid_reservation, 999);
        bq_put64(invalid_reservation + 8, 2);
        bq_frame(corrupt + stable, BQ_RESERVE, 2, invalid_reservation, sizeof(invalid_reservation));
        bq_test_image(&fixture, corrupt, stable + BQ_HEADER_SIZE + sizeof(invalid_reservation));
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_CORRUPT);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_faults(void)
{
    u32 failures[] = {1, 2, BQ_HEADER_SIZE, BQ_HEADER_SIZE + 1, BQ_HEADER_SIZE + 9};
    for (u32 scenario = 0; scenario < 9; scenario += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            BqQueue* queue = &fixture.queue;
            BqRequest first = bq_test_request(1, false), second = bq_test_request(2, false);
            u64 id = 0;
            BQ_CHECK(bq_submit(queue, &first, &id) == BQ_OK);
            queue->fault.write_chunk = 3;
            if (scenario < 5)
            {
                queue->fault.fail_write_at = failures[scenario];
            }
            queue->fault.before_sync = scenario == 5;
            queue->fault.sync_error = scenario == 6;
            queue->fault.after_sync = scenario == 7;
            BQ_CHECK(bq_submit(queue, &second, &id) == (scenario == 8 ? BQ_OK : BQ_IO));
            if (scenario != 8)
            {
                BQ_CHECK(queue->poisoned && !id && queue->state.job_count == 1);
                BQ_CHECK(bq_submit(queue, &first, &id) == BQ_IO);
            }
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            BQ_CHECK(queue->state.job_count == (scenario < 5 ? 1u : 2u));
            BQ_CHECK(bq_submit(queue, &second, &id) == BQ_OK && id == 2);
            BQ_CHECK(queue->state.job_count == 2);
            bq_test_end(&fixture);
        }
    }
    /* A reservation must persist before a worker can receive its token. */
    for (u32 scenario = 0; scenario < 3; scenario += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            BqQueue* queue = &fixture.queue;
            BqRequest request = bq_test_request(1, false);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
            queue->fault.fail_write_at = scenario == 0 ? 2 : 0;
            queue->fault.before_sync = scenario == 1;
            queue->fault.after_sync = scenario == 2;
            BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_IO && !id && !token);
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            BQ_CHECK(queue->needs_reconciliation == (scenario != 0));
            BQ_CHECK(bq_reserve(queue, &id, &token) == (scenario == 0 ? BQ_OK : BQ_RECONCILIATION_REQUIRED));
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_phase_restarts(void)
{
    for (u32 phase = BQ_QUEUED; phase <= BQ_FINISHED; phase += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            BqQueue* queue = &fixture.queue;
            BqRequest request = bq_test_request(1, false);
            BqRequest following = bq_test_request(2, true);
            u64 id = 0, token = 0, following_id = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_submit(queue, &following, &following_id) == BQ_OK);
            if (phase > BQ_QUEUED)
            {
                BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_OK);
                BQ_CHECK(bq_fake_step(queue, id, token + 1) == BQ_INVALID_TRANSITION);
                for (u32 next = BQ_PREPARING; next <= phase; next += 1)
                {
                    BQ_CHECK(bq_fake_step(queue, id, token) == BQ_OK);
                }
            }
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            BqJob* job = bq_job(&queue->state, id);
            BQ_CHECK(job && (u32)job->phase == phase && job->validity == BQ_NOT_EVALUATED);
            if (phase > BQ_QUEUED && phase < BQ_FINISHED)
            {
                u64 ignored_id = 0, ignored_token = 0;
                BQ_CHECK(queue->needs_reconciliation);
                BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_RECONCILIATION_REQUIRED);
                BQ_CHECK(bq_fake_step(queue, id, token) == BQ_RECONCILIATION_REQUIRED);
                BQ_CHECK(bq_fake_reconcile(queue, id, token) == BQ_OK);
                job = bq_job(&queue->state, id);
                BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == (phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_INTERRUPTED));
                bq_close(queue);
                BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && !queue->needs_reconciliation);
            }
            if (phase == BQ_QUEUED)
            {
                BQ_CHECK(bq_fake_run(queue, &id) == BQ_OK && id == 1);
            }
            BQ_CHECK(bq_fake_run(queue, &id) == BQ_OK && id == following_id);
            job = bq_job(&queue->state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED && job->validity == BQ_NOT_EVALUATED);
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK && id == 1);
            BQ_CHECK(bq_fake_run(queue, &id) == BQ_NOT_FOUND);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_lifetime_and_logs(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        u64 id = 0;
        for (u32 i = 0; i < BQ_JOB_CAP; i += 1)
        {
            BqRequest request = bq_test_request(i, false);
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_fake_run(queue, &id) == BQ_OK);
        }
        BqRequest extra = bq_test_request(BQ_JOB_CAP, false);
        BQ_CHECK(!bq_pending(&queue->state) && queue->state.job_count == BQ_JOB_CAP);
        BQ_CHECK(bq_submit(queue, &extra, &id) == BQ_FULL);
        BqRequest original = bq_test_request(0, false);
        BQ_CHECK(bq_submit(queue, &original, &id) == BQ_OK && id == 1);
        u64 before = queue->state.sequence;
        u8 body[16];
        bq_put64(body, id);
        bq_put64(body + 8, 0);
        BqPacket request, response;
        bq_packet(&request, BQ_OP_LOGS, 77, body, sizeof(body));
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        u8 const* result = response.bytes + BQ_CONTROL_HEADER;
        BQ_CHECK(bq_u32(result + 4) == BQ_LOG_PAGE && bq_u32(result + 16) == 1);
        u64 cursor = bq_u64(result + 8);
        bq_put64(body + 8, cursor);
        bq_packet(&request, BQ_OP_LOGS, 78, body, sizeof(body));
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        result = response.bytes + BQ_CONTROL_HEADER;
        BQ_CHECK(bq_u32(result + 4) == BQ_LOG_PAGE && bq_u32(result + 16) == 0 && bq_u64(result + 8) > cursor);
        BQ_CHECK(queue->state.sequence == before);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK && queue->state.job_count == BQ_JOB_CAP);
        BQ_CHECK(bq_submit(queue, &original, &id) == BQ_OK && id == 1);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_cli_acknowledgment(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        bq_close(&fixture.queue);
        FILE* discarded = tmpfile();
        FILE* broken = fopen("/dev/null", "r");
        BQ_CHECK(discarded && broken);
        if (discarded && broken)
        {
            char* submit[] = {"bench_service", "submit", fixture.path, "cli-principal", "lost-ack", "fake-success-v1",
                              "1111111111111111111111111111111111111111", "2222222222222222222222222222222222222222"};
            BQ_CHECK(bq_cli(8, submit, discarded, broken, discarded) != 0);
            BQ_CHECK(bq_cli(8, submit, discarded, discarded, discarded) == 0);
            BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK && fixture.queue.state.job_count == 1);
            bq_close(&fixture.queue);
            char* run[] = {"bench_service", "fake-run", fixture.path};
            BQ_CHECK(bq_cli(3, run, discarded, discarded, discarded) == 0);
            char* result[] = {"bench_service", "result", fixture.path, "1"};
            BQ_CHECK(bq_cli(4, result, discarded, discarded, discarded) == 0);
            result[3] = "1junk";
            BQ_CHECK(bq_cli(4, result, discarded, discarded, discarded) != 0);
            BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK);
            BqJob* job = bq_job(&fixture.queue.state, 1);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED && job->validity == BQ_NOT_EVALUATED);
        }
        if (discarded)
        {
            fclose(discarded);
        }
        if (broken)
        {
            fclose(broken);
        }
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_completion_failures(void)
{
    for (u32 scenario = 0; scenario < 3; scenario += 1)
    {
        BqFixture fixture;
        if (bq_test_begin(&fixture))
        {
            BqQueue* queue = &fixture.queue;
            BqRequest request = bq_test_request(1, false), next_request = bq_test_request(2, false);
            u64 id = 0, token = 0, next_id = 0;
            BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_submit(queue, &next_request, &next_id) == BQ_OK);
            BQ_CHECK(bq_reserve(queue, &id, &token) == BQ_OK);
            for (u32 phase = BQ_PREPARING; phase <= BQ_CLEANING; phase += 1)
            {
                BQ_CHECK(bq_fake_step(queue, id, token) == BQ_OK);
            }
            u64 before = queue->state.sequence;
            BQ_CHECK(bq_cancel(queue, id) == BQ_OK && queue->state.sequence == before);
            queue->fault.fail_write_at = scenario == 0 ? 2 : 0;
            queue->fault.after_sync = scenario == 1;
            queue->fault.before_sync = scenario == 2;
            BQ_CHECK(bq_fake_step(queue, id, token) == BQ_IO && queue->state.active_id == id);
            u64 ignored_id = 0, ignored_token = 0;
            BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_IO);
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
            if (scenario == 0)
            {
                BQ_CHECK(queue->needs_reconciliation);
                BQ_CHECK(bq_reserve(queue, &ignored_id, &ignored_token) == BQ_RECONCILIATION_REQUIRED);
                BQ_CHECK(bq_fake_reconcile(queue, id, token) == BQ_OK);
            }
            BqJob* job = bq_job(&queue->state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED && job->validity == BQ_NOT_EVALUATED);
            BQ_CHECK(bq_fake_run(queue, &ignored_id) == BQ_OK && ignored_id == next_id);
            bq_test_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_protocol_mutations(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqQueue* queue = &fixture.queue;
        BqRequest submission = bq_test_request(1, false);
        BqPacket request, response;
        bq_packet(&request, BQ_OP_SUBMIT, 9, submission.bytes, submission.size);
        for (u32 prefix = 0; prefix < request.size; prefix += 1)
        {
            BQ_CHECK(bq_dispatch(queue, request.bytes, prefix, &response) == BQ_BAD_REQUEST);
            BQ_CHECK(queue->state.job_count == 0);
        }
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        u64 id = bq_u64(response.bytes + BQ_CONTROL_HEADER + 4);
        /* Drop that response, restart, and retry identical wire bytes. */
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.path) == BQ_OK);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_OK);
        BQ_CHECK(bq_u64(response.bytes + BQ_CONTROL_HEADER + 4) == id && queue->state.job_count == 1);
        u64 sequence = queue->state.sequence;
        u8 concatenated[BQ_CONTROL_CAP * 2];
        memcpy(concatenated, request.bytes, request.size);
        memcpy(concatenated + request.size, request.bytes, request.size);
        BQ_CHECK(bq_dispatch(queue, concatenated, request.size * 2, &response) == BQ_BAD_REQUEST);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size + 1, &response) == BQ_BAD_REQUEST);
        bq_put32(request.bytes + BQ_CONTROL_HEADER, UINT32_MAX);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_packet(&request, 999, 10, NULL, 0);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        u8 body[16] = {0};
        bq_put64(body, id);
        bq_packet(&request, BQ_OP_STATUS, 10, body, 9);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_put64(body + 8, UINT64_MAX);
        bq_packet(&request, BQ_OP_LOGS, 10, body, 16);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        BQ_CHECK(queue->state.sequence == sequence);
        bq_test_end(&fixture);
    }
}
#endif

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    bq_test_codec();
#ifndef _WIN32
    bq_test_closed_handle();
    bq_test_admission();
    bq_test_prefixes_and_corruption();
    bq_test_faults();
    bq_test_phase_restarts();
    bq_test_completion_failures();
    bq_test_protocol_mutations();
    bq_test_journal_schema_migration();
    bq_test_lifetime_and_logs();
    bq_test_cli_acknowledgment();
    bq_test_materialization_failures();
    bq_test_materialization_and_recovery();
    bq_test_workspace_collision();
    bq_test_materialization_configuration();
    bq_test_workspace_entry_mismatch();
    bq_test_workspace_root_substitution();
    bq_test_partial_cleanup_recovery();
    bq_test_uncertain_failure_cleanup();
    bq_test_no_attempt_recovery();
    bq_test_configuration_transition_recovery();
    bq_test_cancelled_failure_recovery();
    bq_test_cleanup_bounds_and_failure();
    char const* storage = "posix-real-journal";
#else
    char const* storage = "unsupported-codec-only";
#endif
    printf("BENCH_SERVICE_SELF_TEST assertions=%u failures=%u storage=%s executor=fake-only\n",
           bq_test_assertions, bq_test_failures, storage);
    int result = bq_test_failures ? 1 : 0;
    return result;
}
