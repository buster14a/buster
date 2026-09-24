/* Dedicated #1018 fixture. Compile against tools/throughput/shared.c; this
 * intentionally does not change the shared service test registration owned by
 * the #923 integrator. It uses the real descriptor-backed materializer.
 */
#define main bq_service_cli_main
#include "main.c"
#undef main
#include "retirement_correctness.c"
#include "retirement_correctness_service.c"
#include <stdlib.h>
#include <time.h>

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

BUSTER_GLOBAL_LOCAL bool bq_prep_test_toolchain(char const* installed,
                                                char digest[SHA256_HEX_CAPACITY])
{
    char parent[512], root[512], bin[512], path[512], manifest[1024];
    int length = snprintf(parent, sizeof(parent), "%s/toolchain", installed);
    bool ok = length > 0 && (size_t)length < sizeof(parent);
    length = ok ? snprintf(root, sizeof(root), "%s/native-retirement-performance-v1", parent) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(root);
    length = ok ? snprintf(bin, sizeof(bin), "%s/bin", root) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(bin) &&
         mkdir(parent, 0700) == 0 && mkdir(root, 0700) == 0 && mkdir(bin, 0700) == 0;
    char const* names[] = {"clang", "cmake", "ld", "ninja"};
    char const* body = "fixture only\n";
    char tool_digest[SHA256_HEX_CAPACITY];
    bq_digest(body, (u32)strlen(body), (char8*)tool_digest);
    u32 used = (u32)snprintf(manifest, sizeof(manifest),
        "BQ-RETIREMENT-TOOLCHAIN-V1\nplatform=linux-x86_64\n");
    ok = ok && used < sizeof(manifest);
    for (u32 index = 0; ok && index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        length = snprintf(path, sizeof(path), "%s/%s", bin, names[index]);
        ok = length > 0 && (size_t)length < sizeof(path) &&
             bq_prep_test_write(path, body) && chmod(path, 0555) == 0;
        length = ok ? snprintf(manifest + used, sizeof(manifest) - used,
                               "%.64s bin/%s\n", tool_digest, names[index]) : -1;
        ok = ok && length > 0 && (u32)length < sizeof(manifest) - used;
        if (ok) used += (u32)length;
    }
    length = ok ? snprintf(path, sizeof(path), "%s/toolchain.manifest", root) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(path) &&
         bq_prep_test_write(path, manifest) && chmod(path, 0444) == 0 &&
         chmod(bin, 0555) == 0 && chmod(root, 0555) == 0 && chmod(parent, 0555) == 0;
    if (ok) bq_digest(manifest, used, (char8*)digest);
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
    char toolchain_digest[SHA256_HEX_CAPACITY] = {0};
    ok = ok && bq_prep_test_source(installed, base, "int a;\n", &subjects[0]) &&
         bq_prep_test_source(installed, candidate, "int b;\n", &subjects[1]) &&
         bq_prep_test_inventory(installed, subjects, profile) &&
         bq_prep_test_toolchain(installed, toolchain_digest);
    size_t used = strlen(profile);
    length = ok ? snprintf(profile + used, 512 - used,
                           "toolchain-manifest-sha256=%.64s\n", toolchain_digest) : -1;
    ok = ok && length > 0 && (size_t)length < 512 - used &&
         chmod(recipes, 0500) == 0 && chmod(sources, 0500) == 0 &&
         chmod(installed, 0555) == 0 && chmod(workspaces, 02710) == 0;
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
        /* Readback must not consume the caller's directory cursor: the same
         * held source is used by verification and later evidence consumers. */
        BqRetirementSource repeated = {0};
        BQ_PREP_CHECK(bq_retirement_scan(destination, ".source-manifest", string_from_pointer(revision),
                                         &repeated, true) &&
                      bq_retirement_same_source(&observed, &repeated) &&
                      !memcmp(observed.installed_identity_sha256, repeated.installed_identity_sha256, 64));
        if (installed >= 0) close(installed);
        if (destination >= 0) close(destination);
    }
    if (root[0] && ok) bq_prep_test_cleanup(root);
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_binary(int directory, char const* name, char const* bytes)
{
    int file = openat(directory, name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    bool ok = file >= 0 && bq_write_all(file, (u8 const*)bytes, (u32)strlen(bytes)) &&
              fchmod(file, 0500) == 0 && fsync(file) == 0;
    if (file >= 0 && close(file) != 0) ok = false;
    return ok;
}

/* A real service readback joins the B gate here. The remaining one-row #508
 * declaration is synthetic and cannot authorize a timing campaign. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_correctness_join(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const* preparation_digest,
    char const* record_digest, BqRetirementBinaries const* observed,
    String8 workspace_root, char const* fixed_driver, char const* fixed_toolchain,
    char const* build_record_digest)
{
    BqRetirementPrepared prepared = {.rows = 1, .object_rows = 1, .native_target = 1};
    memcpy(prepared.preparation_sha256, preparation_digest, SHA256_HEX_CAPACITY);
    memset(prepared.support_sha256, '1', 64);
    memset(prepared.census_sha256, 'b', 64);
    for (u32 side = 0; side < 2; side += 1)
    {
        memcpy(prepared.source_sha256[side], observed->source_sha256[side], SHA256_HEX_CAPACITY);
        memcpy(prepared.binary_sha256[side], observed->binary_sha256[side], SHA256_HEX_CAPACITY);
    }
    BqRetirementTrustedRow row = {.row = 0, .census_row = 0, .target = 1,
                                  .stage = BQ_RETIREMENT_STAGE_OBJECT, .classification = 1,
                                  .compiler_eligible = 1};
    memset(row.identity_sha256, '1', 64);
    memset(row.source_sha256, '2', 64);
    memset(row.configuration_sha256, '3', 64);
    memset(row.compiler_command_sha256[0], '4', 64);
    memset(row.compiler_command_sha256[1], '5', 64);
    BqRetirementRequiredCheck required[BQ_RETIREMENT_CHECK_COUNT - 1u] = {0};
    BqRetirementCheckResult checked[BQ_RETIREMENT_CHECK_COUNT - 1u] = {0};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(required); i += 1)
    {
        required[i].kind = i + 1;
        required[i].rows = 1;
        memset(required[i].command_sha256, (int)('a' + i), 64);
        memset(required[i].configuration_sha256, (int)('1' + i), 64);
        memset(required[i].receipt_sha256, (int)('a' + i), 64);
    }
    BqRetirementRowFact fact = {0};
    u32 identities[3] = {0};
    u8 census[1] = {0};
    BqRetirementHeldBinaries held = {0};
    BqRetirementCorrectness gate = {0};
    BqError begin = build_record_digest ? bq_retirement_correctness_begin_service_built_pinned(
        queue, job, installed, workspaces, workspace_root, profile, fixed_driver,
        fixed_toolchain,
        preparation_digest, record_digest, build_record_digest, &prepared, &row, required,
        BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
        census, BUSTER_ARRAY_LENGTH(census), &held, &gate) :
        bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
        profile, preparation_digest, record_digest, &prepared, &row, required,
        BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
        census, BUSTER_ARRAY_LENGTH(census), &held, &gate);
    BQ_PREP_CHECK(begin == BQ_OK &&
                  held.owned == 1 && held.descriptors[0] >= 3 && held.descriptors[1] >= 3 &&
                  !strcmp(gate.prepared.binary_sha256[0], observed->binary_sha256[0]) &&
                  !bq_retirement_correctness_ready(&gate));
    int live = held.descriptors[0];
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_RECIPE_MISMATCH &&
                  held.descriptors[0] == live && fcntl(live, F_GETFD) >= 0);
    bq_retirement_binaries_release(&held);

    char wrong_record[SHA256_HEX_CAPACITY];
    memcpy(wrong_record, record_digest, SHA256_HEX_CAPACITY);
    wrong_record[0] = wrong_record[0] == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, wrong_record, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_CORRUPT &&
                  gate.failed && !held.owned);

    char saved = prepared.binary_sha256[0][0];
    prepared.binary_sha256[0][0] = saved == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned && held.descriptors[0] == -1 && held.descriptors[1] == -1);
    prepared.binary_sha256[0][0] = saved;
    saved = prepared.source_sha256[1][0];
    prepared.source_sha256[1][0] = saved == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned);
    prepared.source_sha256[1][0] = saved;
    prepared.support_sha256[0] = '2';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned);
    prepared.support_sha256[0] = '1';
    prepared.census_sha256[0] = 0;
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, &row, required,
                  BUSTER_ARRAY_LENGTH(required), checked, &fact, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_RECIPE_MISMATCH &&
                  gate.failed && !held.owned);
}

/* The miniature frozen files exercise the service's output record/importer,
 * not the trusted Clang build or complete toolchain provenance. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_binary_handoff(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, int attempt, char const* profile, char const* preparation_digest,
    BqRetirementPreparation const* prepared, char record_digest[SHA256_HEX_CAPACITY])
{
    record_digest[0] = 0;
    BqRetirementBinaries imported = {0};
    String8 pinned = string_from_pointer(profile);
    BQ_PREP_CHECK(mkdirat(attempt, "trusted-build", 0700) == 0);
    int directory = openat(attempt, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(directory >= 0 && bq_prep_test_binary(directory, "base-ide", "compiler A\n") &&
                  fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_record_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest) == BQ_SOURCE_MISMATCH && !record_digest[0]);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  bq_prep_test_binary(directory, "candidate-ide", "compiler B\n") &&
                  fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                  &imported) == BQ_NOT_FOUND && !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(bq_retirement_binaries_record_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest) == BQ_OK && strlen(record_digest) == 64);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK &&
                  !strcmp(imported.preparation_sha256, preparation_digest) &&
                  !strcmp(imported.source_sha256[0], prepared->subjects[0].manifest_sha256) &&
                  !strcmp(imported.source_sha256[1], prepared->subjects[1].manifest_sha256) &&
                  strcmp(imported.binary_sha256[0], imported.binary_sha256[1]) &&
                  strlen(imported.directory_identity_sha256) == 64 &&
                  strlen(imported.binary_identity_sha256[0]) == 64);
    /* The same two binaries with an unlisted file cannot be frozen as a
     * complete trusted-build output closure. Repeated scans are independent. */
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  bq_prep_test_binary(directory, "unlisted", "other\n") && fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_SOURCE_MISMATCH &&
                  !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  unlinkat(directory, "unlisted", 0) == 0 && fchmod(directory, 0500) == 0 &&
                  bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  renameat(attempt, "trusted-build", attempt, "held-trusted-build") == 0 &&
                  mkdirat(attempt, "trusted-build", 0700) == 0);
    int replacement_directory = openat(attempt, "trusted-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(replacement_directory >= 0 &&
                  renameat(directory, "base-ide", replacement_directory, "base-ide") == 0 &&
                  renameat(directory, "candidate-ide", replacement_directory, "candidate-ide") == 0 &&
                  fchmod(replacement_directory, 0500) == 0 && fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_CORRUPT &&
                  !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(replacement_directory >= 0 && fchmod(replacement_directory, 0700) == 0 &&
                  fchmod(directory, 0700) == 0 &&
                  renameat(replacement_directory, "base-ide", directory, "base-ide") == 0 &&
                  renameat(replacement_directory, "candidate-ide", directory, "candidate-ide") == 0 &&
                  unlinkat(attempt, "trusted-build", AT_REMOVEDIR) == 0 &&
                  renameat(attempt, "held-trusted-build", attempt, "trusted-build") == 0 &&
                  fchmod(directory, 0500) == 0 &&
                  bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    if (replacement_directory >= 0) close(replacement_directory);
    bq_prep_test_correctness_join(queue, job, installed, workspaces, pinned,
                                   preparation_digest, record_digest, &imported,
                                   (String8){0}, NULL, NULL, NULL);
    char wrong[SHA256_HEX_CAPACITY];
    memcpy(wrong, record_digest, sizeof(wrong));
    wrong[0] = wrong[0] == 'a' ? 'b' : 'a';
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, wrong, &imported) == BQ_CORRUPT && !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  wrong, record_digest, &imported) == BQ_RECIPE_MISMATCH && !imported.preparation_sha256[0]);
    BqJob stale = *job;
    stale.token += 1;
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, &stale, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) != BQ_OK && !imported.preparation_sha256[0]);
    char second[SHA256_HEX_CAPACITY];
    BQ_PREP_CHECK(bq_retirement_binaries_record_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, second) != BQ_OK && !second[0]);

    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  renameat(directory, "base-ide", attempt, "held-base-ide") == 0 &&
                  bq_prep_test_binary(directory, "base-ide", "compiler A\n") &&
                  fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_CORRUPT &&
                  !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  unlinkat(directory, "base-ide", 0) == 0 &&
                  renameat(attempt, "held-base-ide", directory, "base-ide") == 0 &&
                  fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  renameat(directory, "candidate-ide", attempt, "held-candidate-ide") == 0 &&
                  symlinkat("base-ide", directory, "candidate-ide") == 0 && fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_SOURCE_MISMATCH &&
                  !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  unlinkat(directory, "candidate-ide", 0) == 0 &&
                  renameat(attempt, "held-candidate-ide", directory, "candidate-ide") == 0 &&
                  fchmod(directory, 0500) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    int binary = directory >= 0 ? openat(directory, "candidate-ide", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_PREP_CHECK(binary >= 0 && fchmod(binary, 0700) == 0);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_SOURCE_MISMATCH &&
                  !imported.preparation_sha256[0]);
    int modified = directory >= 0 ? openat(directory, "candidate-ide", O_WRONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_PREP_CHECK(modified >= 0 && pwrite(modified, "X", 1, 0) == 1 &&
                  fchmod(modified, 0500) == 0);
    if (modified >= 0) close(modified);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_CORRUPT &&
                  !imported.preparation_sha256[0]);
    BQ_PREP_CHECK(binary >= 0 && fchmod(binary, 0700) == 0);
    modified = directory >= 0 ? openat(directory, "candidate-ide", O_WRONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_PREP_CHECK(modified >= 0 && pwrite(modified, "compiler B\n", 11, 0) == 11 &&
                  fchmod(modified, 0500) == 0);
    if (modified >= 0) close(modified);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    int record = openat(queue->directory_fd, "binaries-1", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(record >= 0 && fchmod(record, 0600) == 0);
    if (record >= 0) close(record);
    record = openat(queue->directory_fd, "binaries-1", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(record >= 0 && pwrite(record, "X", 1, 0) == 1 && fchmod(record, 0400) == 0);
    if (record >= 0) close(record);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_CORRUPT &&
                  !imported.preparation_sha256[0]);
    record = openat(queue->directory_fd, "binaries-1", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(record >= 0 && fchmod(record, 0600) == 0);
    if (record >= 0) close(record);
    record = openat(queue->directory_fd, "binaries-1", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(record >= 0 && pwrite(record, "B", 1, 0) == 1 && fchmod(record, 0400) == 0);
    if (record >= 0) close(record);
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_OK);
    pid_t child = fork();
    if (child == 0)
    {
        (void)close(STDIN_FILENO);
        char bytes[SHA256_HEX_CAPACITY] = {0}, identity[SHA256_HEX_CAPACITY] = {0};
        int pinned_binary = -1;
        bool ready = bq_retirement_frozen_binary_open(directory, "base-ide", bytes, identity,
                                                       &pinned_binary) && pinned_binary >= 3 &&
                     !strcmp(bytes, imported.binary_sha256[0]) &&
                     (fcntl(pinned_binary, F_GETFD) & FD_CLOEXEC) != 0;
        if (pinned_binary >= 0) close(pinned_binary);
        _exit(ready ? 0 : 1);
    }
    int child_status = 0;
    BQ_PREP_CHECK(child > 0 && waitpid(child, &child_status, 0) == child &&
                  WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
    int witnesses[2] = {-1, -1};
    BQ_PREP_CHECK(pipe(witnesses) == 0);
    BqRetirementHeldBinaries empty = {0};
    empty.descriptors[0] = witnesses[0];
    empty.descriptors[1] = witnesses[1];
    bq_retirement_binaries_release(&empty);
    BQ_PREP_CHECK(empty.descriptors[0] == -1 && empty.descriptors[1] == -1 &&
                  fcntl(witnesses[0], F_GETFD) >= 0 && fcntl(witnesses[1], F_GETFD) >= 0);
    if (witnesses[0] >= 0) close(witnesses[0]);
    if (witnesses[1] >= 0) close(witnesses[1]);
    BqRetirementHeldBinaries held = {.descriptors = {-1, -1}};
    BQ_PREP_CHECK(bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &held) == BQ_OK &&
                  held.owned == 1 && held.descriptors[0] >= 3 && held.descriptors[1] >= 3 &&
                  !strcmp(held.verified.binary_sha256[0], imported.binary_sha256[0]) &&
                  (fcntl(held.descriptors[0], F_GETFD) & FD_CLOEXEC) != 0);
    int live_descriptor = held.descriptors[0];
    BQ_PREP_CHECK(bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &held) == BQ_RECIPE_MISMATCH &&
                  held.descriptors[0] == live_descriptor && fcntl(live_descriptor, F_GETFD) >= 0);
    struct stat pinned_info = {0}, after_swap = {0};
    BQ_PREP_CHECK(held.descriptors[0] >= 0 && fstat(held.descriptors[0], &pinned_info) == 0 &&
                  directory >= 0 && fchmod(directory, 0700) == 0 &&
                  renameat(directory, "base-ide", attempt, "held-base-ide") == 0 &&
                  bq_prep_test_binary(directory, "base-ide", "compiler A\n") &&
                  fchmod(directory, 0500) == 0);
    char original[12] = {0};
    BQ_PREP_CHECK(held.descriptors[0] >= 0 && fstat(held.descriptors[0], &after_swap) == 0 &&
                  pinned_info.st_dev == after_swap.st_dev && pinned_info.st_ino == after_swap.st_ino &&
                  pread(held.descriptors[0], original, 11, 0) == 11 && !strcmp(original, "compiler A\n"));
    BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &imported) == BQ_CORRUPT &&
                  !imported.preparation_sha256[0]);
    BqRetirementHeldBinaries refused = {.descriptors = {-1, -1}};
    BQ_PREP_CHECK(bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &refused) == BQ_CORRUPT &&
                  refused.owned == 0 && refused.descriptors[0] == -1 && refused.descriptors[1] == -1 &&
                  !refused.verified.preparation_sha256[0]);
    BQ_PREP_CHECK(directory >= 0 && fchmod(directory, 0700) == 0 &&
                  unlinkat(directory, "base-ide", 0) == 0 &&
                  renameat(attempt, "held-base-ide", directory, "base-ide") == 0 &&
                  fchmod(directory, 0500) == 0);
    int old_descriptor = held.descriptors[0];
    bq_retirement_binaries_release(&held);
    BQ_PREP_CHECK(held.owned == 0 && held.descriptors[0] == -1 && held.descriptors[1] == -1 &&
                  !held.verified.preparation_sha256[0] &&
                  fcntl(old_descriptor, F_GETFD) == -1 && errno == EBADF);
    BQ_PREP_CHECK(bq_retirement_binaries_acquire_pinned(queue, job, installed, workspaces, pinned,
                  preparation_digest, record_digest, &held) == BQ_OK);
    bq_retirement_binaries_release(&held);
    if (binary >= 0) close(binary);
    if (directory >= 0) close(directory);
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_compile_driver(char const* driver_path)
{
    pid_t child = fork();
    if (child == 0)
    {
        char* const argv[] = {"/usr/bin/cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "tools/bench_service/retirement_matched_build_fixture.c", "-o", (char*)driver_path, NULL};
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    bool ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
              chmod(driver_path, 0500) == 0;
    return ok;
}

/* Execute verified driver and source FDs after replacing both pathnames. A
 * deliberately inheritable parent FD must not reach the exec'd driver. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_held_exec_fd(BqRetirementMatchedBuild* build,
    char const* driver_path, char const* workspace_path,
    int attempt, BqRetirementBuildStage const* command, bool reject_source)
{
    char held[512], probe[512], digest[SHA256_HEX_CAPACITY] = {0};
    int held_length = snprintf(held, sizeof(held), "%s/held-driver", workspace_path);
    int probe_length = snprintf(probe, sizeof(probe), "%s/driver-exec-probe", workspace_path);
    bool ok = held_length > 0 && (size_t)held_length < sizeof(held) &&
        probe_length > 0 && (size_t)probe_length < sizeof(probe) &&
        fcntl(90, F_GETFD) < 0 && errno == EBADF;
    int executable = ok ? open(driver_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && bq_retirement_build_driver_fd_sha(executable, digest) &&
        !memcmp(build->driver_sha256, digest, SHA256_HEX_CAPACITY);
    int source = ok ? bq_retirement_build_source_fd(build) : -1;
    ok = ok && source >= 3;
    int parent = ok ? openat(attempt, "base", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat parent_info = {0};
    bool parent_valid = parent >= 3 && fstat(parent, &parent_info) == 0;
    ok = ok && parent_valid;
    int writer = ok ? openat(attempt, "driver-probe-log",
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 3;
    int input = ok ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
    ok = ok && input >= 3 && dup2(input, 90) == 90;
    if (input >= 0) close(input);
    bool moved = false, replaced = false, source_moved = false, source_replaced = false;
    if (ok) moved = rename(driver_path, held) == 0;
    ok = ok && moved;
    if (ok) replaced = symlink("/missing-retirement-driver", driver_path) == 0;
    ok = ok && replaced;
    if (ok) ok = fchmod(parent, (parent_info.st_mode & 07777) | S_IWUSR) == 0;
    if (ok) source_moved = renameat(parent, "source", parent, "held-source") == 0;
    ok = ok && source_moved;
    if (ok) source_replaced = symlinkat("/missing-retirement-source", parent, "source") == 0;
    ok = ok && source_replaced;
    if (parent_valid && fchmod(parent, parent_info.st_mode & 07777) != 0) ok = false;
    BqRetirementBuildStage stage = *command;
    stage.argv[3] = probe;
    pid_t child = ok ? fork() : -1;
    if (child == 0) bq_retirement_build_exec_fd(executable, writer, attempt, source, &stage);
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    ok = ok && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (fcntl(90, F_GETFD) >= 0 && close(90) != 0) ok = false;
    if (executable >= 0 && close(executable) != 0) ok = false;
    if (source >= 0 && close(source) != 0) ok = false;
    if (writer >= 0 && close(writer) != 0) ok = false;
    if (replaced && unlink(driver_path) != 0) ok = false;
    if (moved && rename(held, driver_path) != 0) ok = false;
    if (ok && reject_source)
    {
        BqRetirementBuildProcess unlaunched = {0};
        struct stat absent = {0};
        ok = !bq_retirement_matched_build_launch(build, &unlaunched) && build->failed &&
            !unlaunched.state && fstatat(attempt, "build-log-0", &absent, AT_SYMLINK_NOFOLLOW) < 0 &&
            errno == ENOENT;
    }
    if (parent_valid && fchmod(parent, (parent_info.st_mode & 07777) | S_IWUSR) != 0) ok = false;
    if (source_replaced && unlinkat(parent, "source", 0) != 0) ok = false;
    if (source_moved && renameat(parent, "held-source", parent, "source") != 0) ok = false;
    if (parent_valid && fchmod(parent, parent_info.st_mode & 07777) != 0) ok = false;
    if (parent >= 3 && close(parent) != 0) ok = false;
    int reader = writer >= 0 ? openat(attempt, "driver-probe-log", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    char output[64] = {0};
    ssize_t count = reader >= 3 ? pread(reader, output, sizeof(output) - 1, 0) : -1;
    ok = ok && count == (ssize_t)strlen("fixture generated\n") &&
        !memcmp(output, "fixture generated\n", (size_t)count);
    if (reader >= 0 && close(reader) != 0) ok = false;
    if (writer >= 0 && unlinkat(attempt, "driver-probe-log", 0) != 0) ok = false;
    if (waited == child && rmdir(probe) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_run_stage(BqRetirementMatchedBuild* build,
    BqRetirementBuildProcess* process, int* exit_code)
{
    bool ok = bq_retirement_matched_build_launch(build, process);
    if (ok)
    {
        BQ_PREP_CHECK(process->state == BQ_RETIREMENT_BUILD_RUNNING && process->process > 0);
        bq_retirement_matched_build_abort(process);
        BQ_PREP_CHECK(process->state == BQ_RETIREMENT_BUILD_RUNNING && process->process > 0);
    }
    int result = ok ? 0 : -1;
    while (result == 0)
    {
        result = bq_retirement_matched_build_poll(process);
        if (!result)
        {
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
    *exit_code = process->state == BQ_RETIREMENT_BUILD_REAPED ? process->exit_code : -1;
    ok = ok && process->state == BQ_RETIREMENT_BUILD_REAPED;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_matched_build(BqQueue* queue, BqJob const* original,
    int installed, int workspaces, char const* installed_path, char const* workspace_path,
    char const* original_profile,
    BqRetirementPreparation const* original_preparation)
{
    char driver_path[256];
    int driver_length = snprintf(driver_path, sizeof(driver_path), "%s/fixture-driver", workspace_path);
    BQ_PREP_CHECK(driver_length > 0 && (u32)driver_length < sizeof(driver_path) &&
                  bq_prep_test_compile_driver(driver_path));
    char driver_digest[SHA256_HEX_CAPACITY] = {0};
    BQ_PREP_CHECK(bq_retirement_build_driver_sha(driver_path, driver_digest));
    char toolchain_root[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP];
    int root_length = snprintf(toolchain_root, sizeof(toolchain_root),
        "%s/toolchain/native-retirement-performance-v1", installed_path);
    BqRetirementToolchain checked = {0};
    BQ_PREP_CHECK(root_length > 0 && (size_t)root_length < sizeof(toolchain_root) &&
        bq_retirement_toolchain_verify(installed, string_from_pointer(original_profile),
            toolchain_root, &checked) == BQ_OK && checked.entries == 4 &&
        bq_retirement_toolchain_recheck(&checked));
    char wrong_pin[512];
    memcpy(wrong_pin, original_profile, strlen(original_profile) + 1);
    char* pin = strstr(wrong_pin, "toolchain-manifest-sha256=");
    if (pin) pin[26] = pin[26] == 'a' ? 'b' : 'a';
    BqRetirementToolchain refused = {0};
    BQ_PREP_CHECK(pin && bq_retirement_toolchain_verify(installed,
        string_from_pointer(wrong_pin), toolchain_root, &refused) == BQ_CONFIGURATION_MISMATCH &&
        !refused.manifest_sha256[0]);
    BQ_PREP_CHECK(bq_retirement_toolchain_verify(installed,
        S8("schema=1\n"), toolchain_root, &refused) == BQ_RECIPE_MISMATCH &&
        bq_retirement_toolchain_verify(installed, string_from_pointer(original_profile),
            workspace_path, &refused) == BQ_CONFIGURATION_MISMATCH);
    char tool_bin[512], extra[512];
    int bin_length = snprintf(tool_bin, sizeof(tool_bin), "%s/bin", toolchain_root);
    int extra_length = snprintf(extra, sizeof(extra), "%s/unlisted", tool_bin);
    BQ_PREP_CHECK(bin_length > 0 && (size_t)bin_length < sizeof(tool_bin) &&
        extra_length > 0 && (size_t)extra_length < sizeof(extra) &&
        chmod(tool_bin, 0700) == 0 && bq_prep_test_write(extra, "extra\n") &&
        chmod(tool_bin, 0555) == 0 && !bq_retirement_toolchain_recheck(&checked));
    BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(extra) == 0 &&
        symlink("/etc/passwd", extra) == 0 && chmod(tool_bin, 0555) == 0 &&
        !bq_retirement_toolchain_recheck(&checked));
    BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(extra) == 0 &&
        chmod(tool_bin, 0555) == 0 && bq_retirement_toolchain_recheck(&checked));
    char clang_path[512];
    int clang_length = snprintf(clang_path, sizeof(clang_path), "%s/clang", tool_bin);
    char held_tool[512];
    int held_length = snprintf(held_tool, sizeof(held_tool), "%s/held-tool-clang", workspace_path);
    BQ_PREP_CHECK(clang_length > 0 && (size_t)clang_length < sizeof(clang_path) &&
        held_length > 0 && (size_t)held_length < sizeof(held_tool) &&
        chmod(tool_bin, 0700) == 0 &&
        rename(clang_path, held_tool) == 0 &&
        bq_prep_test_write(clang_path, "fixture only\n") &&
        chmod(clang_path, 0555) == 0 && chmod(tool_bin, 0555) == 0);
    BqRetirementToolchain replacement = {0};
    BQ_PREP_CHECK(bq_retirement_toolchain_verify(installed,
        string_from_pointer(original_profile), toolchain_root, &replacement) == BQ_OK &&
        strcmp(replacement.identity_sha256, checked.identity_sha256) &&
        !bq_retirement_toolchain_recheck(&checked));
    /* The old inode is outside the bundle. A byte-equal replacement verifies
     * under a fresh pin but must fail this job's previously held identity. */
    BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 &&
        unlink(clang_path) == 0 && rename(held_tool, clang_path) == 0 &&
        chmod(tool_bin, 0555) == 0 && bq_retirement_toolchain_recheck(&checked));
    char profile[640];
    int length = snprintf(profile, sizeof(profile), "%sbuild-driver-sha256=%.64s\n",
                          original_profile, driver_digest);
    BQ_PREP_CHECK(length > 0 && (u32)length < sizeof(profile));
    for (u32 trial = 0; trial < 9; trial += 1)
    {
        BqJob job = *original;
        job.id = 30 + trial;
        job.token = 40 + trial;
        BqRetirementPreparation prepared = *original_preparation;
        char name[64];
        bool ok = bq_workspace_name(name, job.id, job.token) &&
                  mkdirat(workspaces, name, 0700) == 0;
        int attempt = ok ? openat(workspaces, name,
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && attempt >= 0;
        for (u32 side = 0; ok && side < 2; side += 1)
        {
            char const* subject_name = side ? "candidate" : "base";
            ok = mkdirat(attempt, subject_name, 0700) == 0;
            int subject = ok ? openat(attempt, subject_name,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
            ok = ok && subject >= 0 && mkdirat(subject, "source", 02750) == 0;
            int source = ok ? openat(subject, "source",
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
            if (ok)
            {
                ok = source >= 0 && bq_copy_manifest(installed, source,
                    bq_field(&job.request, 3 + side), BQ_RETIREMENT_SOURCE_MANIFEST_CAP) &&
                    bq_make_sources_read_only(source) &&
                    bq_retirement_verify_subject(installed, subject, source,
                        bq_field(&job.request, 3 + side), &prepared.subjects[side]);
            }
            if (source >= 0) close(source);
            if (subject >= 0) close(subject);
        }
        BQ_PREP_CHECK(ok && bq_retirement_preparation_record(queue, &job, &prepared, BQ_OK, 2));
        char preparation_digest[SHA256_HEX_CAPACITY] = {0};
        String8 pinned = string_from_pointer(profile);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(queue, &job, installed, workspaces,
                      pinned, preparation_digest, NULL) == BQ_OK);
        BqRetirementMatchedBuild build = {0};
        BQ_PREP_CHECK(bq_retirement_matched_build_begin_pinned(queue, &job, installed, workspaces,
            string_from_pointer(workspace_path), pinned, driver_path, toolchain_root,
            preparation_digest, true,
            &build) == BQ_OK);
        if (trial == 1)
        {
            char wrong_profile[640];
            memcpy(wrong_profile, profile, strlen(profile) + 1);
            char* pin = strstr(wrong_profile, "build-driver-sha256=");
            if (pin) pin[20] = pin[20] == 'a' ? 'b' : 'a';
            BqRetirementMatchedBuild refused = {0};
            BQ_PREP_CHECK(pin && bq_retirement_matched_build_begin_pinned(queue, &job,
                installed, workspaces, string_from_pointer(workspace_path),
                string_from_pointer(wrong_profile), driver_path, toolchain_root, preparation_digest,
                true, &refused) == BQ_CONFIGURATION_MISMATCH && !refused.preparation_sha256[0]);
        }
        BqRetirementBuildStage command = {0};
        BQ_PREP_CHECK(bq_retirement_matched_build_stage(&build, &command) &&
            command.argc == 16 && !strcmp(command.argv[7], "clang") &&
            !strcmp(command.argv[3], build.build) && !strcmp(command.cwd, build.source[0]) &&
            !strcmp(command.env[0], checked.path));
        if (trial == 1 || trial == 6)
            BQ_PREP_CHECK(bq_prep_test_held_exec_fd(&build, driver_path, workspace_path,
                attempt, &command, trial == 6) &&
                (trial == 6 ? build.failed && !bq_retirement_matched_build_stage(&build, &command) :
                              bq_retirement_matched_build_stage(&build, &command)));
        if (trial == 6)
        {
            BqRetirementBuildProcess unlaunched = {0};
            BQ_PREP_CHECK(bq_retirement_matched_build_poll(&unlaunched) == -1 &&
                bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &unlaunched, geteuid(), &build) ==
                    BQ_WORKER_FAILED && build.failed && !build.next);
        }
        if (trial == 7)
        {
            BqRetirementBuildProcess live = {0};
            BQ_PREP_CHECK(bq_retirement_matched_build_launch(&build, &live) &&
                bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &live, geteuid(), &build) ==
                    BQ_WORKER_FAILED && build.failed && live.state == BQ_RETIREMENT_BUILD_RUNNING &&
                    live.process > 0 && live.writer >= 3);
            int observed = 0;
            while (observed == 0)
            {
                observed = bq_retirement_matched_build_poll(&live);
                if (!observed)
                {
                    struct timespec pause = {0, 1000000};
                    nanosleep(&pause, NULL);
                }
            }
            bq_retirement_matched_build_abort(&live);
            BQ_PREP_CHECK(!live.state && !live.process && !live.writer);
        }
        if (trial == 8)
        {
            BqRetirementBuildProcess stolen = {0};
            BQ_PREP_CHECK(bq_retirement_matched_build_launch(&build, &stolen));
            int status = 0;
            pid_t waited = -1;
            do { if (stolen.process > 0) waited = waitpid(stolen.process, &status, 0); }
            while (waited < 0 && errno == EINTR);
            BQ_PREP_CHECK(waited > 0 && bq_retirement_matched_build_poll(&stolen) == -1 &&
                stolen.state == BQ_RETIREMENT_BUILD_WAIT_FAILED && !stolen.process &&
                bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &stolen, geteuid(), &build) == BQ_WORKER_FAILED &&
                build.failed && !stolen.state);
        }
        for (u32 stage = 0; ok && stage < (trial ? 4u : 1u); stage += 1)
        {
            if (trial >= 6) break;
            if (trial == 3 && stage == 2)
            {
                BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 &&
                    bq_prep_test_write(extra, "unlisted after build\n") &&
                    chmod(tool_bin, 0555) == 0 &&
                    !bq_retirement_matched_build_stage(&build, &command) && build.failed);
                BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(extra) == 0 &&
                    chmod(tool_bin, 0555) == 0 && bq_retirement_toolchain_recheck(&checked));
                break;
            }
            if (trial && stage == 2) ok = renameat(attempt, "matched-build", attempt, "old-build") == 0;
            BqRetirementBuildStage current = {0};
            ok = ok && bq_retirement_matched_build_stage(&build, &current) &&
                 !strcmp(current.argv[3], command.argv[3]);
            BqRetirementBuildProcess process = {0};
            int exit_code = -1;
            if (ok) ok = bq_prep_test_run_stage(&build, &process, &exit_code);
            BQ_PREP_CHECK(ok && (trial ? exit_code == 0 : exit_code == 5));
            if (ok)
            {
                if (trial == 4) process.command_sha256[0] ^= 1;
                if (trial == 5)
                {
                    int replacement = -1;
                    BQ_PREP_CHECK(renameat(attempt, process.name, attempt, "held-build-log") == 0);
                    replacement = openat(attempt, process.name,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
                    BQ_PREP_CHECK(replacement >= 3 && fchmod(replacement, 0400) == 0);
                    if (replacement >= 0) close(replacement);
                }
                uid_t candidate = trial == 2 ? geteuid() + 1 : geteuid();
                BqError expected = !trial ? BQ_WORKER_FAILED :
                                   trial == 4 || trial == 5 ? BQ_WORKER_FAILED :
                                   trial == 2 && stage == 3 ? BQ_SOURCE_MISMATCH : BQ_OK;
                BQ_PREP_CHECK(bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &process, candidate, &build) == expected &&
                    !process.state && !process.process);
            }
            bq_retirement_matched_build_abort(&process);
            if (trial == 4 || trial == 5) break;
        }
        BQ_PREP_CHECK(ok);
        if (trial != 1)
        {
            char record_name[48], bytes[16];
            u32 size = 0;
            BQ_PREP_CHECK(build.failed && build.next == (trial == 3 ? 2u : trial == 2 ? 3u : 0u) &&
                !build.binary_record_sha256[0] &&
                !bq_retirement_matched_build_stage(&build, &command) &&
                bq_record_name(record_name, "binaries", job.id) &&
                bq_record_read(queue, record_name, (u8*)bytes, sizeof(bytes), &size) == BQ_NOT_FOUND &&
                (build.stage_receipt_sha256[0][0] != 0) == (trial < 4));
            BQ_PREP_CHECK(!build.build_record_sha256[0]);
            BqRetirementCorrectness gate = {0};
            BqRetirementHeldBinaries held = {0};
            char const missing[SHA256_HEX_CAPACITY] =
                "0000000000000000000000000000000000000000000000000000000000000000";
            BQ_PREP_CHECK(bq_retirement_correctness_begin_service_built_pinned(
                queue, &job, installed, workspaces, string_from_pointer(workspace_path),
                pinned, driver_path, toolchain_root, preparation_digest, missing, missing,
                NULL, NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, 0, &held, &gate) != BQ_OK &&
                gate.failed && !held.owned && !bq_retirement_correctness_ready(&gate));
        }
        else
        {
            BqRetirementBinaries imported = {0};
            BqRetirementMatchedBuild observed = {0};
            BQ_PREP_CHECK(build.next == 4 && !build.failed && build.build_record_sha256[0] &&
                !bq_retirement_matched_build_stage(&build, &command) &&
                build.binary_record_sha256[0] &&
                bq_retirement_binaries_import_pinned(queue, &job, installed, workspaces,
                    pinned, preparation_digest, build.binary_record_sha256, &imported) == BQ_OK &&
                strcmp(imported.binary_sha256[0], imported.binary_sha256[1]) &&
                strcmp(build.command_sha256[0], build.command_sha256[2]));
            BQ_PREP_CHECK(bq_retirement_matched_build_import_pinned(queue, &job,
                installed, workspaces, string_from_pointer(workspace_path), pinned,
                driver_path, toolchain_root, preparation_digest, build.binary_record_sha256,
                build.build_record_sha256, &observed) == BQ_OK && observed.next == 4 &&
                !strcmp(observed.stage_receipt_sha256[3], build.stage_receipt_sha256[3]));
            BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 &&
                rename(clang_path, held_tool) == 0 &&
                bq_prep_test_write(clang_path, "fixture only\n") &&
                chmod(clang_path, 0555) == 0 && chmod(tool_bin, 0555) == 0);
            BQ_PREP_CHECK(bq_retirement_matched_build_import_pinned(queue, &job,
                installed, workspaces, string_from_pointer(workspace_path), pinned,
                driver_path, toolchain_root, preparation_digest, build.binary_record_sha256,
                build.build_record_sha256, &observed) == BQ_CORRUPT &&
                !observed.preparation_sha256[0]);
            BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(clang_path) == 0 &&
                rename(held_tool, clang_path) == 0 && chmod(tool_bin, 0555) == 0 &&
                bq_retirement_matched_build_import_pinned(queue, &job,
                    installed, workspaces, string_from_pointer(workspace_path), pinned,
                    driver_path, toolchain_root, preparation_digest, build.binary_record_sha256,
                    build.build_record_sha256, &observed) == BQ_OK);
            bq_prep_test_correctness_join(queue, &job, installed, workspaces, pinned,
                preparation_digest, build.binary_record_sha256, &imported,
                string_from_pointer(workspace_path), driver_path, toolchain_root,
                build.build_record_sha256);
            char record_name[48];
            BQ_PREP_CHECK(bq_record_name(record_name, "matched-log-2", job.id));
            int tampered = openat(queue->directory_fd, record_name,
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(tampered >= 0 && fchmod(tampered, 0600) == 0);
            if (tampered >= 0) close(tampered);
            tampered = openat(queue->directory_fd, record_name, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(tampered >= 0 && pwrite(tampered, "X", 1, 0) == 1 &&
                fchmod(tampered, 0400) == 0);
            if (tampered >= 0) close(tampered);
            BQ_PREP_CHECK(bq_retirement_matched_build_import_pinned(queue, &job,
                installed, workspaces, string_from_pointer(workspace_path), pinned,
                driver_path, toolchain_root, preparation_digest, build.binary_record_sha256,
                build.build_record_sha256, &observed) == BQ_CORRUPT && !observed.preparation_sha256[0]);
        }
        if (attempt >= 0) close(attempt);
    }
}

/* Exercise the service's durable producer/readback boundary through the real
 * source copier. This internal test request cannot be submitted: the public
 * registry still rejects the blocked retirement descriptor. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_ready_handoff(int installed, int workspaces,
    char const* installed_path, char const* workspaces_path,
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
                      string_from_pointer(profile), digest, NULL) == BQ_NOT_FOUND && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_record(&queue, &job, prepared, BQ_OK, 2) &&
                      bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_OK && strlen(digest) == 64);
        BqRetirementPreparation imported = {0};
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest, &imported) == BQ_OK &&
                      !strcmp(imported.inventory_sha256, prepared->inventory_sha256) &&
                      !strcmp(imported.subjects[0].installed_identity_sha256,
                              prepared->subjects[0].installed_identity_sha256) &&
                      !strcmp(imported.subjects[1].installed_identity_sha256,
                              prepared->subjects[1].installed_identity_sha256) &&
                      !strcmp(imported.subjects[0].materialized_identity_sha256,
                              prepared->subjects[0].materialized_identity_sha256) &&
                      !strcmp(imported.subjects[1].materialized_identity_sha256,
                              prepared->subjects[1].materialized_identity_sha256) &&
                      !strcmp(imported.subjects[0].manifest_sha256, prepared->subjects[0].manifest_sha256) &&
                      imported.source_reservation_bytes == prepared->source_reservation_bytes);
        bq_prep_test_matched_build(&queue, &job, installed, workspaces, installed_path,
                                   workspaces_path,
                                   profile, prepared);
        char binary_digest[SHA256_HEX_CAPACITY];
        bq_prep_test_binary_handoff(&queue, &job, installed, workspaces, root, profile, digest,
                                    prepared, binary_digest);
        char altered_digest[SHA256_HEX_CAPACITY];
        memcpy(altered_digest, digest, sizeof(altered_digest));
        altered_digest[0] = altered_digest[0] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), altered_digest, &imported) == BQ_RECIPE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        char invalid_digest[SHA256_HEX_CAPACITY] = "invalid";
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), invalid_digest, &imported) == BQ_RECIPE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        BqJob other = job;
        other.token += 1;
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &other, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
        other = job;
        other.digest[0] = other.digest[0] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &other, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_RECIPE_MISMATCH && !digest[0]);
        char wrong_profile[512];
        memcpy(wrong_profile, profile, strlen(profile) + 1);
        char* pin = strstr(wrong_profile, "inventory-sha256=");
        if (pin) pin[17] = pin[17] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(pin && bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(wrong_profile), digest, NULL) == BQ_RECIPE_MISMATCH && !digest[0]);
        char copied_path[128];
        int length = snprintf(copied_path, sizeof(copied_path), "%s/base/source/src", attempt);
        int copied = length > 0 && (u32)length < sizeof(copied_path) ?
                     bq_open_directory_path(workspaces, string_from_pointer(copied_path)) : -1;
        /* Exercise directory closure at the durable consumer, not only at
         * preflight. Failure must not damage the immutable preparation record. */
        char original_digest[SHA256_HEX_CAPACITY];
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), original_digest, NULL) == BQ_OK);
        char installed_path[128];
        int installed_length = snprintf(installed_path, sizeof(installed_path), "sources/%s/src",
                                        prepared->subjects[0].commit);
        int installed_source = installed_length > 0 && (u32)installed_length < sizeof(installed_path) ?
                               bq_open_directory_path(installed, string_from_pointer(installed_path)) : -1;
        /* Keep the old inode outside the installed tree while substituting a
         * byte-identical file. The current inventory and manifest still pass. */
        bool moved = installed_source >= 0 && fchmod(installed_source, 0700) == 0 &&
                     renameat(installed_source, "main.c", workspaces, "held-installed-main.c") == 0;
        int replacement = moved ? openat(installed_source, "main.c", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC |
                                         O_NOFOLLOW, 0600) : -1;
        bool swapped = replacement >= 0 && bq_write_all(replacement, (u8 const*)"int a;\n", 7) &&
                       fchmod(replacement, 0400) == 0 && fchmod(installed_source, 0500) == 0;
        if (replacement >= 0) close(replacement);
        BQ_PREP_CHECK(swapped);
        if (swapped)
        {
            BqRetirementPreparation rescanned = {0};
            BQ_PREP_CHECK(bq_retirement_preflight_pinned_impl(installed, workspaces, &job.request,
                          string_from_pointer(profile), &rescanned, false) == BQ_OK &&
                          !strcmp(rescanned.subjects[0].manifest_sha256,
                                  prepared->subjects[0].manifest_sha256) &&
                          strcmp(rescanned.subjects[0].installed_identity_sha256,
                                 prepared->subjects[0].installed_identity_sha256));
            BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
            BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), original_digest, &imported) == BQ_CORRUPT &&
                          !imported.inventory_sha256[0]);
            BqRetirementBinaries binaries = {0};
            BQ_PREP_CHECK(bq_retirement_binaries_import_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), original_digest, binary_digest,
                          &binaries) == BQ_CORRUPT && !binaries.preparation_sha256[0]);
        }
        bool restored = installed_source >= 0 && fchmod(installed_source, 0700) == 0;
        if (restored && moved && replacement >= 0)
            restored = unlinkat(installed_source, "main.c", 0) == 0;
        if (restored && moved)
            restored = renameat(workspaces, "held-installed-main.c", installed_source, "main.c") == 0;
        if (installed_source >= 0 && fchmod(installed_source, 0500) != 0) restored = false;
        BQ_PREP_CHECK(restored);
        if (installed_source >= 0) close(installed_source);
        BQ_PREP_CHECK(restored && bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_OK &&
                      !strcmp(digest, original_digest));
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 && mkdirat(copied, "unlisted", 0500) == 0 &&
                      fchmod(copied, 0500) == 0);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_SOURCE_MISMATCH && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), original_digest, &imported) == BQ_SOURCE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 &&
                      unlinkat(copied, "unlisted", AT_REMOVEDIR) == 0 && fchmod(copied, 0500) == 0);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_OK && !strcmp(digest, original_digest));
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 &&
                      renameat(copied, "main.c", copied, "old.c") == 0);
        if (copied >= 0)
        {
            int replacement = openat(copied, "main.c", O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
            BQ_PREP_CHECK(replacement >= 0 && bq_write_all(replacement, (u8 const*)"int a;\n", 7) &&
                          fchmod(replacement, 0400) == 0 && unlinkat(copied, "old.c", 0) == 0 &&
                          fchmod(copied, 0500) == 0);
            if (replacement >= 0) close(replacement);
            BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_SOURCE_MISMATCH && !digest[0]);
            BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                          string_from_pointer(profile), original_digest, &imported) == BQ_SOURCE_MISMATCH &&
                          !imported.inventory_sha256[0]);
            close(copied);
        }
        int record = openat(queue.directory_fd, "preparation-1", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(record >= 0 && fchmod(record, 0600) == 0);
        if (record >= 0) close(record);
        record = openat(queue.directory_fd, "preparation-1", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(record >= 0 && pwrite(record, "X", 1, 0) == 1 && fchmod(record, 0400) == 0);
        if (record >= 0) close(record);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(&queue, &job, installed, workspaces,
                      string_from_pointer(profile), original_digest, &imported) == BQ_CORRUPT &&
                      !imported.inventory_sha256[0]);
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
        bq_prep_test_ready_handoff(input, output, installed, workspaces, &preparation, profile);

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

        int copied_src = source >= 0 ? openat(source, "src", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        BQ_PREP_CHECK(copied_src >= 0 && fchmod(copied_src, 0700) == 0);
        int planted = copied_src >= 0 ? openat(copied_src, "extra.c", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400) : -1;
        BQ_PREP_CHECK(planted >= 0 && close(planted) == 0 && fchmod(copied_src, 0500) == 0 &&
                      !bq_retirement_verify_subject(input, subject, source,
                                                    string_from_pointer(subjects[0].commit), &verified_base));
        BQ_PREP_CHECK(copied_src >= 0 && fchmod(copied_src, 0700) == 0 &&
                      unlinkat(copied_src, "extra.c", 0) == 0 && fchmod(copied_src, 0500) == 0);
        if (copied_src >= 0) close(copied_src);

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
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && unlink(old_path) == 0 &&
                      chmod(directory_path, 0500) == 0);
        BQ_PREP_CHECK(bq_retirement_preflight_pinned(input, output, &request, pinned, &preparation) == BQ_OK);

        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && bq_prep_test_write(old_path, "extra") &&
                      chmod(directory_path, 0500) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && unlink(old_path) == 0 &&
                      chmod(directory_path, 0500) == 0);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && mkdir(old_path, 0500) == 0 &&
                      chmod(directory_path, 0500) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && rmdir(old_path) == 0 &&
                      symlink("main.c", old_path) == 0 && chmod(directory_path, 0500) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned,
                                                     &preparation) == BQ_SOURCE_MISMATCH);
        BQ_PREP_CHECK(chmod(directory_path, 0700) == 0 && unlink(old_path) == 0 &&
                      chmod(directory_path, 0500) == 0 &&
                      bq_retirement_preflight_pinned(input, output, &request, pinned, &preparation) == BQ_OK);

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
