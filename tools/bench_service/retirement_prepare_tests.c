/* Dedicated #1018 fixture. Compile against tools/throughput/shared.c; this
 * intentionally does not change the shared service test registration owned by
 * the #923 integrator. It uses the real descriptor-backed materializer.
 */
#define BQ_RETIREMENT_CORRECTNESS_TEST_ONLY 1
#define main bq_service_cli_main
#include "main.c"
#undef main
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
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

/* The production entry's first census boundary uses exact profile-pinned
 * declaration bytes. The miniature B fixture uses the pinned test seam and
 * cannot stand in for this derived 192-row-per-subject matrix. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_support_population(void)
{
    char directory[] = "/tmp/bq-retirement-support-XXXXXX";
    char path[128], profile[128];
    char const* ledger = "path\trole\tcompile_obligation\tbytes\tsha256\n"
        "tests/a.c\tsubject\tsupported-object-zero-fallback\t4\t"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "tests/b.c\tsubject\tregistered-non-object-control\t5\t"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "tests/z.h\tsupport-file\tdependency-only\t3\t"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n";
    BQ_PREP_CHECK(mkdtemp(directory) != NULL);
    int length = snprintf(path, sizeof(path), "%s/support.tsv", directory);
    BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(path) && bq_prep_test_write(path, ledger));
    BqRetirementPrepared prepared = {.rows = 386, .object_rows = 384};
    bq_digest(ledger, (u32)strlen(ledger), (char8*)prepared.support_sha256);
    length = snprintf(profile, sizeof(profile), "support-declaration-sha256=%.64s\n",
                      prepared.support_sha256);
    BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(profile));
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(file >= 3 && bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    BqRetirementTrustedRow* rows = calloc(prepared.rows, sizeof(*rows));
    BQ_PREP_CHECK(rows != NULL);
    if (rows)
    {
        for (u32 i = 0; i < prepared.object_rows; i += 1)
        {
            rows[i].row = rows[i].census_row = i;
            rows[i].stage = BQ_RETIREMENT_STAGE_OBJECT;
            rows[i].target = bq_retirement_census_target_ids[(i % BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT) / 16u];
            memset(rows[i].source_sha256, i < 192 ? 'a' : 'b', 64);
        }
        rows[384].row = 384;
        rows[384].stage = BQ_RETIREMENT_STAGE_LINK;
        rows[385].row = 385;
        rows[385].stage = BQ_RETIREMENT_STAGE_SELF_HOST;
        BQ_PREP_CHECK(bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[192].source_sha256[0] = 'a';
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[192].source_sha256[0] = 'b';
        rows[16].target = rows[0].target;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[16].target = bq_retirement_census_target_ids[1];
        rows[383].census_row = 382;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[383].census_row = 383;
        rows[385].stage = BQ_RETIREMENT_STAGE_LINK;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[385].stage = BQ_RETIREMENT_STAGE_SELF_HOST;
        rows[384].row = 0;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[384].row = 384;
        rows[384].census_row = 384;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, rows));
        rows[384].census_row = 0;
    }
    free(rows);
    prepared.object_rows = 192;
    BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    prepared.object_rows = 384;
    prepared.rows = 385;
    BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    prepared.rows = 386;
    char first = prepared.support_sha256[0];
    prepared.support_sha256[0] = first == '0' ? '1' : '0';
    BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    prepared.support_sha256[0] = first;
    BQ_PREP_CHECK(fcntl(file, F_SETFD, 0) == 0 &&
                  !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL) &&
                  fcntl(file, F_SETFD, FD_CLOEXEC) == 0);
    BQ_PREP_CHECK(chmod(path, 0600) == 0 &&
                  !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL) &&
                  chmod(path, 0400) == 0);
    char link[128];
    length = snprintf(link, sizeof(link), "%s/alias", directory);
    BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(link) && linkat(AT_FDCWD, path, AT_FDCWD, link, 0) == 0 &&
                  !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL) &&
                  unlink(link) == 0);
    BQ_PREP_CHECK(close(file) == 0 && !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    BQ_PREP_CHECK(chmod(path, 0600) == 0);
    file = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(file >= 3 && !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    BQ_PREP_CHECK(file >= 3 && pwrite(file, "x", 1, 6) == 1 && close(file) == 0 &&
                  chmod(path, 0400) == 0);
    file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(file >= 3 && !bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
    if (file >= 3) BQ_PREP_CHECK(close(file) == 0);
    BQ_PREP_CHECK(unlink(path) == 0);

    /* Replay the repository's currently reviewed 559-input declaration too;
     * the service receives a private read-only copy, never a mutable git file. */
    char* actual = malloc(BQ_RETIREMENT_SUPPORT_BYTES_CAP + 1u);
    u32 actual_bytes = 0;
    int source = open("docs/native-retirement-support-v1.tsv", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    BQ_PREP_CHECK(actual && source >= 3 &&
                  bq_read_file(source, (u8*)actual, BQ_RETIREMENT_SUPPORT_BYTES_CAP, &actual_bytes));
    if (source >= 3) BQ_PREP_CHECK(close(source) == 0);
    if (actual && actual_bytes)
    {
        actual[actual_bytes] = 0;
        length = snprintf(path, sizeof(path), "%s/actual.tsv", directory);
        BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(path) &&
                      bq_prep_test_write(path, actual));
        prepared = (BqRetirementPrepared){.rows = 78914, .object_rows = 78912};
        bq_digest(actual, actual_bytes, (char8*)prepared.support_sha256);
        length = snprintf(profile, sizeof(profile), "support-declaration-sha256=%.64s\n",
                          prepared.support_sha256);
        BQ_PREP_CHECK(length > 0 && (size_t)length < sizeof(profile));
        file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(file >= 3 &&
                      bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
        BqRetirementTrustedRow* full = calloc(prepared.rows, sizeof(*full));
        BQ_PREP_CHECK(full != NULL);
        if (full)
        {
            String8 text = {(char8*)actual, actual_bytes}, line = {0};
            u64 offset = 0;
            u32 object = 0;
            BQ_PREP_CHECK(bq_next_line(text, &offset, &line));
            while (offset < text.length && bq_next_line(text, &offset, &line))
            {
                char8* tab = memchr(line.pointer, '\t', (size_t)line.length);
                char8* next = tab ? memchr(tab + 1, '\t', (size_t)(line.pointer + line.length - tab - 1)) : NULL;
                bool subject = next && (size_t)(next - tab - 1) == strlen("subject") &&
                               !memcmp(tab + 1, "subject", strlen("subject"));
                if (subject && line.length >= 64)
                    for (u32 cell = 0; cell < BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT &&
                                       object < prepared.object_rows; cell += 1)
                    {
                        full[object].row = full[object].census_row = object;
                        full[object].target = bq_retirement_census_target_ids[cell / 16u];
                        full[object].stage = BQ_RETIREMENT_STAGE_OBJECT;
                        memcpy(full[object].source_sha256, line.pointer + line.length - 64, 64);
                        object += 1;
                    }
            }
            BQ_PREP_CHECK(object == prepared.object_rows);
            if (object == prepared.object_rows && object)
            {
                full[object].row = object;
                full[object].stage = BQ_RETIREMENT_STAGE_LINK;
                full[object + 1].row = object + 1;
                full[object + 1].stage = BQ_RETIREMENT_STAGE_SELF_HOST;
                BQ_PREP_CHECK(bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, full));
                full[object - 1].target -= 1;
                BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, full));
            }
            free(full);
        }
        prepared.object_rows -= 192;
        BQ_PREP_CHECK(!bq_retirement_support_projection(file, string_from_pointer(profile), &prepared, NULL));
        if (file >= 3) BQ_PREP_CHECK(close(file) == 0);
        BQ_PREP_CHECK(unlink(path) == 0);
    }
    free(actual);
    BQ_PREP_CHECK(rmdir(directory) == 0);
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_census_append(char* text, u32 capacity, u32* used,
    BqRetirementTrustedRow* rows, u32 row, u32 target_index, u32 allocator,
    char const* fixture, char const* fixture_recipe, char const* compile_obligation)
{
    u32 subject_row = row % BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT;
    u32 within_target = subject_row % 16u;
    u32 frontend = within_target / 8u;
    u32 pic = (within_target % 8u) / 4u;
    String8 lowering = frontend ? S8("direct-ssa") : S8("local-backed-canonical");
    String8 pic_text = pic ? S8("1") : S8("0");
    String8 execution = target_index == 8 ? S8("unavailable-platform-control") : S8("semantic-gate-509");
    char line[512] = {0};
    u32 group = row / BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT;
    int length = snprintf(line, sizeof(line),
        "%u\t%u\t%s\t%.*s\t%.*s\tbaseline\tfixture-features\t%.*s\t%.*s\t%.*s\t1\t%s\t%s\tsemantic-gate-509\t%.*s\tnone\tgroups/%u/%.*s.argv\n",
        row, group, fixture,
        (int)bq_retirement_census_targets[target_index].length, bq_retirement_census_targets[target_index].pointer,
        (int)bq_retirement_census_target_abis[target_index].length, bq_retirement_census_target_abis[target_index].pointer,
        (int)bq_retirement_census_allocators[allocator].length, bq_retirement_census_allocators[allocator].pointer,
        (int)lowering.length, lowering.pointer, (int)pic_text.length, pic_text.pointer,
        fixture_recipe, compile_obligation, (int)execution.length, execution.pointer, group,
        (int)bq_retirement_census_allocators[allocator].length, bq_retirement_census_allocators[allocator].pointer);
    bool ok = length > 0 && (u32)length < sizeof(line) &&
              *used + (u32)length < capacity;
    if (ok)
    {
        memcpy(text + *used, line, (size_t)length);
        *used += (u32)length;
        rows[row].row = row;
        rows[row].census_row = row;
        rows[row].target = bq_retirement_census_target_ids[target_index];
        rows[row].stage = BQ_RETIREMENT_STAGE_OBJECT;
        rows[row].compiler_eligible = strcmp(compile_obligation, "registered-non-object-control") != 0;
        memset(rows[row].configuration_sha256, '3', 64);
        rows[row].configuration_sha256[64] = 0;
    }
    BQ_PREP_CHECK(ok);
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_census_identity_sha256(String8 fields[17], char digest[65])
{
    char json[2048] = {0};
    int length = snprintf(json, sizeof(json),
        "{\"PIC\":\"%.*s\",\"allocator\":\"%.*s\",\"argv_evidence\":\"%.*s\","
        "\"artifact_stage\":\"object\",\"compile_obligation\":\"%.*s\",\"cpu\":\"%.*s\","
        "\"cpu_features\":\"%.*s\",\"diagnostic_obligation\":\"%.*s\","
        "\"execution_obligation\":\"%.*s\",\"fixture\":\"%.*s\",\"fixture_recipe\":\"%.*s\","
        "\"frontend_lowering\":\"%.*s\",\"link_obligation\":\"%.*s\",\"target\":\"%.*s\","
        "\"target_abi\":\"%.*s\"}",
        (int)fields[9].length, fields[9].pointer, (int)fields[7].length, fields[7].pointer,
        (int)fields[16].length, fields[16].pointer, (int)fields[12].length, fields[12].pointer,
        (int)fields[5].length, fields[5].pointer, (int)fields[6].length, fields[6].pointer,
        (int)fields[15].length, fields[15].pointer, (int)fields[14].length, fields[14].pointer,
        (int)fields[2].length, fields[2].pointer, (int)fields[11].length, fields[11].pointer,
        (int)fields[8].length, fields[8].pointer, (int)fields[13].length, fields[13].pointer,
        (int)fields[3].length, fields[3].pointer, (int)fields[4].length, fields[4].pointer);
    bool ok = length > 0 && (u32)length < sizeof(json);
    if (ok) bq_digest(json, (u32)length, (char8*)digest);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_write_bytes(char const* path, char const* bytes, u32 count)
{
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
    bool ok = file >= 3 && bq_write_all(file, (u8 const*)bytes, count) && fsync(file) == 0;
    if (file >= 3 && close(file) != 0) ok = false;
    return ok;
}

/* #1020: the service derives each row's configuration_sha256 from rows.tsv as
 * row_configuration_digest does in retirement_validator_eligibility_test.py
 * (which shares the golden below) and joins
 * every B row, including LINK/SELF_HOST stages, to the census row it names. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_configuration_join(String8 text, String8 const known_fields[17],
    BqRetirementPrepared const* prepared, BqRetirementTrustedRow* trusted)
{
    static char const golden[] = "b8f9e4810f3286d61bc6dcee5fa0c72fa9d873d8e32f5b84f739e16bd426a585";
    BqRetirementValidatorRawRow known = {0};
    for (u32 field = 0; field < BUSTER_ARRAY_LENGTH(known.fields); field += 1) known.fields[field] = known_fields[field];
    char known_digest[SHA256_HEX_CAPACITY] = {0};
    BQ_PREP_CHECK(bq_retirement_validator_row_configuration_sha256(&known, known_digest) &&
                  !strcmp(known_digest, golden));
    BqRetirementValidatorRawRow control = known;
    control.fields[10] = S8("0");
    control.fields[0] = S8("7");
    BQ_PREP_CHECK(bq_retirement_validator_row_configuration_sha256(&control, known_digest) &&
                  !strcmp(known_digest, golden));
    control.fields[16] = S8("groups/0/fast.argv");
    BQ_PREP_CHECK(bq_retirement_validator_row_configuration_sha256(&control, known_digest) &&
                  strcmp(known_digest, golden));
    control.fields[16] = S8("groups/0/\x7f.argv");
    BQ_PREP_CHECK(!bq_retirement_validator_row_configuration_sha256(&control, known_digest) &&
                  !known_digest[0]);

    BqRetirementValidatorRawRow* raw = NULL;
    char rows_identity[SHA256_HEX_CAPACITY] = {0};
    BqRetirementValidatorEligibility eligibility = {0};
    bool ok = bq_retirement_validator_rows_digest(text, prepared->object_rows, rows_identity, &raw);
    eligibility.row_count = prepared->object_rows;
    eligibility.classification = ok ? calloc(prepared->object_rows, 1) : NULL;
    eligibility.configuration_sha256 = ok ?
        calloc(prepared->object_rows, sizeof(*eligibility.configuration_sha256)) : NULL;
    ok = ok && eligibility.classification && eligibility.configuration_sha256;
    for (u32 row = 0; ok && row < prepared->object_rows; row += 1)
    {
        eligibility.classification[row] = 1;
        ok = bq_retirement_validator_row_configuration_sha256(raw + row, eligibility.configuration_sha256[row]);
    }
    for (u32 row = 0; ok && row < prepared->rows; row += 1)
        memcpy(trusted[row].configuration_sha256, eligibility.configuration_sha256[trusted[row].census_row],
               SHA256_HEX_CAPACITY);
    BQ_PREP_CHECK(ok && !strcmp(eligibility.configuration_sha256[0], golden) &&
                  strcmp(eligibility.configuration_sha256[0], eligibility.configuration_sha256[1]) &&
                  bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
    if (ok)
    {
        u32 const link = prepared->object_rows;
        trusted[17].configuration_sha256[0] ^= 1;
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        trusted[17].configuration_sha256[0] ^= 1;
        trusted[link].configuration_sha256[63] ^= 1;
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        memcpy(trusted[link].configuration_sha256, eligibility.configuration_sha256[1], SHA256_HEX_CAPACITY);
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        memcpy(trusted[link].configuration_sha256, eligibility.configuration_sha256[0], SHA256_HEX_CAPACITY);
        BQ_PREP_CHECK(bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        eligibility.classification[5] = 0;
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        eligibility.classification[5] = 1;
        eligibility.row_count -= 1;
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        eligibility.row_count += 1;
        trusted[3].census_row = 2;
        memcpy(trusted[3].configuration_sha256, eligibility.configuration_sha256[2], SHA256_HEX_CAPACITY);
        BQ_PREP_CHECK(!bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
        trusted[3].census_row = 3;
        memcpy(trusted[3].configuration_sha256, eligibility.configuration_sha256[3], SHA256_HEX_CAPACITY);
        BQ_PREP_CHECK(bq_retirement_validator_rows_join(prepared, trusted, &eligibility));
    }
    free(eligibility.classification);
    free(eligibility.configuration_sha256);
    free(raw);
}

/* Both #508 inputs.tsv and rows.tsv need pins independent of B's declaration.
 * This fixture's input digest below is an independent known SHA-256 vector. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_raw_census_boundary(void)
{
    enum { subject_count = 3, object_rows = subject_count * BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT,
           all_rows = object_rows + 2,
           text_capacity = 1024 * 1024 };
    char directory[] = "/tmp/bq-retirement-raw-census-XXXXXX";
    char support_path[160] = {0}, inputs_path[160] = {0}, rows_path[160] = {0};
    bool ok = mkdtemp(directory) != NULL;
    int length = snprintf(support_path, sizeof(support_path), "%s/support.tsv", directory);
    ok = ok && length > 0 && (size_t)length < sizeof(support_path);
    length = ok ? snprintf(inputs_path, sizeof(inputs_path), "%s/inputs.tsv", directory) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(inputs_path);
    length = ok ? snprintf(rows_path, sizeof(rows_path), "%s/rows.tsv", directory) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(rows_path);
    char const* support = "path\trole\tcompile_obligation\tbytes\tsha256\n"
        "tests/a.c\tsubject\tsupported-object-zero-fallback\t4\t"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "tests/basic_c_constexpr.c\tsubject\tsupported-object-zero-fallback\t5\t"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "tests/z-control.c\tsubject\tregistered-non-object-control\t6\t"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n"
        "tests/zz.h\tsupport-file\tdependency-only\t3\t"
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\n";
    char const* inputs = "path\trole\tcompile_obligation\tbytes\tbuster_hash_64\tsha256\tfixture_recipe\tfixture_flags\n"
        "tests/a.c\tsubject\tsupported-object-zero-fallback\t4\t11\t"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tcompiler-default\t\n"
        "tests/basic_c_constexpr.c\tsubject\tsupported-object-zero-fallback\t5\t22\t"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\tc23\t-std=c23\n"
        "tests/z-control.c\tsubject\tregistered-non-object-control\t6\t33\t"
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\tcompiler-default\t\n"
        "tests/zz.h\tsupport-file\tdependency-only\t3\t44\t"
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\tcompiler-default\t\n";
    ok = ok && bq_prep_test_write(support_path, support) && bq_prep_test_write(inputs_path, inputs);
    char* text = calloc(text_capacity, 1);
    char* changed = calloc(text_capacity, 1);
    BqRetirementTrustedRow* trusted = calloc(all_rows, sizeof(*trusted));
    ok = ok && text && changed && trusted;
    u32 used = 0;
    static char const census_header[] = "row\tgroup\tfixture\ttarget\ttarget_abi\tcpu\tcpu_features\tallocator\tfrontend_lowering\tPIC\tselected\tfixture_recipe\tcompile_obligation\tlink_obligation\texecution_obligation\tdiagnostic_obligation\targv_evidence\n";
    if (ok)
    {
        memcpy(text, census_header, sizeof(census_header) - 1);
        used = sizeof(census_header) - 1;
        for (u32 subject = 0; subject < subject_count; subject += 1)
        {
            char const* fixture = subject == 0 ? "tests/a.c" :
                subject == 1 ? "tests/basic_c_constexpr.c" : "tests/z-control.c";
            char const* fixture_recipe = subject == 1 ? "c23" : "compiler-default";
            char const* obligation = subject == 2 ? "registered-non-object-control" :
                "supported-object-zero-fallback";
            for (u32 target = 0; target < 12; target += 1)
                for (u32 frontend = 0; frontend < 2; frontend += 1)
                    for (u32 pic = 0; pic < 2; pic += 1)
                        for (u32 allocator = 0; allocator < BQ_RETIREMENT_CENSUS_ALLOCATOR_COUNT; allocator += 1)
                        {
                            u32 row = subject * BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT + target * 16u +
                                      frontend * 8u + pic * 4u + allocator;
                            bq_prep_test_census_append(text, text_capacity, &used, trusted, row, target,
                                                       allocator, fixture, fixture_recipe, obligation);
                        }
        }
        u64 offset = sizeof(census_header) - 1;
        u32 census_row = 0;
        String8 line = {0};
        String8 text_view = {(char8*)text, used};
        bool identities_ok = true;
        while (identities_ok && offset < text_view.length)
        {
            identities_ok = bq_next_line(text_view, &offset, &line) && census_row < object_rows;
            String8 fields[17] = {0};
            char identity[SHA256_HEX_CAPACITY] = {0};
            identities_ok = identities_ok && bq_retirement_census_line_fields(line, fields) &&
                            bq_prep_test_census_identity_sha256(fields, identity);
            if (identities_ok)
            {
                memcpy(trusted[census_row].identity_sha256, identity, SHA256_HEX_CAPACITY);
                census_row += 1;
            }
        }
        ok = ok && identities_ok && census_row == object_rows;
        ok = used > sizeof(census_header) - 1 && used < text_capacity &&
             bq_prep_test_write_bytes(rows_path, text, used);
    }
    BqRetirementPrepared prepared = {.rows = all_rows, .object_rows = object_rows, .native_target = 1};
    if (ok)
    {
        bq_digest(support, (u32)strlen(support), (char8*)prepared.support_sha256);
        bq_digest(text, used, (char8*)prepared.census_sha256);
        memset(prepared.preparation_sha256, '1', 64);
        prepared.preparation_sha256[64] = 0;
        memset(prepared.aa_second_commands_sha256, '2', 64);
        prepared.aa_second_commands_sha256[64] = 0;
        for (u32 row = 0; row < object_rows; row += 1)
        {
            memcpy(trusted[row].source_sha256, row < BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT ?
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" :
                   row < 2 * BQ_RETIREMENT_OBJECT_ROWS_PER_SUBJECT ?
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" :
                   "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc", 65);
        }
        trusted[object_rows].row = object_rows;
        trusted[object_rows].census_row = 0;
        trusted[object_rows].target = bq_retirement_census_target_ids[0];
        trusted[object_rows].stage = BQ_RETIREMENT_STAGE_LINK;
        trusted[object_rows + 1].row = object_rows + 1;
        trusted[object_rows + 1].census_row = 0;
        trusted[object_rows + 1].target = bq_retirement_census_target_ids[0];
        trusted[object_rows + 1].stage = BQ_RETIREMENT_STAGE_SELF_HOST;
        memcpy(trusted[object_rows].source_sha256, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 65);
        memcpy(trusted[object_rows + 1].source_sha256, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 65);
        String8 known_fields[17] = {
            S8("0"), S8("0"), S8("tests/a.c"), S8("x86_64-unknown-linux-gnu"), S8("systemv-x86_64"),
            S8("baseline"), S8("fixture-features"), S8("none"), S8("local-backed-canonical"), S8("0"),
            S8("1"), S8("compiler-default"), S8("supported-object-zero-fallback"), S8("semantic-gate-509"),
            S8("semantic-gate-509"), S8("none"), S8("groups/0/none.argv")
        };
        char expected_identity[SHA256_HEX_CAPACITY] = {0}, service_identity[SHA256_HEX_CAPACITY] = {0};
        BQ_PREP_CHECK(bq_prep_test_census_identity_sha256(known_fields, expected_identity) &&
                      bq_retirement_census_identity_sha256(known_fields, service_identity) &&
                      !strcmp(expected_identity, "f87f6401cf1b60971613e1fd7f740e30828c1d93b29830b94e681b325433b6fb") &&
                      !strcmp(expected_identity, service_identity));
        bq_prep_test_configuration_join((String8){(char8*)text, used}, known_fields, &prepared, trusted);
    }
    static char const inputs_pin[] = "0e2ddd7a187c0aa3371021b5e4d3939440320ab41043ade4f3b087e4d8adb17c";
    char profile[384] = {0};
    int profile_length = ok ? snprintf(profile, sizeof(profile),
        "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
        prepared.support_sha256, inputs_pin, prepared.census_sha256) : -1;
    ok = ok && profile_length > 0 && (size_t)profile_length < sizeof(profile);
    BQ_PREP_CHECK(ok);
    int support_fd = ok ? open(support_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int inputs_fd = ok ? open(inputs_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int rows_fd = ok ? open(rows_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_PREP_CHECK(support_fd >= 3 && inputs_fd >= 3 && rows_fd >= 3);
    if (support_fd >= 3 && inputs_fd >= 3 && rows_fd >= 3)
    {
        char verified_inputs[SHA256_HEX_CAPACITY] = {0};
        char verified_rows[SHA256_HEX_CAPACITY] = {0};
        BQ_PREP_CHECK(bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                      string_from_pointer(profile), &prepared, trusted, verified_inputs, verified_rows) &&
                      !memcmp(verified_inputs, inputs_pin, SHA256_HEX_CAPACITY) &&
                      !memcmp(verified_rows, prepared.census_sha256, SHA256_HEX_CAPACITY));
        char no_pin[256] = {0};
        int no_pin_length = snprintf(no_pin, sizeof(no_pin),
            "support-declaration-sha256=%.64s\ncensus-rows-sha256=%.64s\n",
            prepared.support_sha256, prepared.census_sha256);
        BQ_PREP_CHECK(no_pin_length > 0 && (size_t)no_pin_length < sizeof(no_pin) &&
                      !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(no_pin), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        char altered_pin[65] = {0};
        memcpy(altered_pin, inputs_pin, 64);
        altered_pin[0] = altered_pin[0] == '0' ? '1' : '0';
        char altered_pin_profile[384] = {0};
        int altered_pin_length = snprintf(altered_pin_profile, sizeof(altered_pin_profile),
            "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
            prepared.support_sha256, altered_pin, prepared.census_sha256);
        BQ_PREP_CHECK(altered_pin_length > 0 && (size_t)altered_pin_length < sizeof(altered_pin_profile) &&
                      !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(altered_pin_profile), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        char saved = prepared.census_sha256[0];
        prepared.census_sha256[0] = saved == '0' ? '1' : '0';
        BQ_PREP_CHECK(!bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(profile), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        prepared.census_sha256[0] = saved;
        saved = trusted[17].identity_sha256[0];
        trusted[17].identity_sha256[0] = saved == '0' ? '1' : '0';
        BQ_PREP_CHECK(!bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(profile), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        trusted[17].identity_sha256[0] = saved;
        BQ_PREP_CHECK(close(rows_fd) == 0);
        rows_fd = open(rows_path, O_RDONLY | O_NOFOLLOW);
        BQ_PREP_CHECK(rows_fd >= 3 &&
                      !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(profile), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        if (rows_fd >= 3) BQ_PREP_CHECK(close(rows_fd) == 0);
        rows_fd = open(rows_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(rows_fd >= 3);

        BQ_PREP_CHECK(close(inputs_fd) == 0);
        inputs_fd = open(inputs_path, O_RDONLY | O_NOFOLLOW);
        BQ_PREP_CHECK(inputs_fd >= 3 &&
                      !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                       string_from_pointer(profile), &prepared, trusted,
                                                       verified_inputs, verified_rows));
        if (inputs_fd >= 3) BQ_PREP_CHECK(close(inputs_fd) == 0);
        inputs_fd = open(inputs_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(inputs_fd >= 3);

        char inputs_changed[2048] = {0};
        u32 inputs_size = (u32)strlen(inputs);
        memcpy(inputs_changed, inputs, inputs_size);
        char* mutated_flag = strstr(inputs_changed, "-std=c23");
        BQ_PREP_CHECK(mutated_flag != NULL);
        if (mutated_flag) memcpy(mutated_flag, "-std=c99", strlen("-std=c99"));
        char mutated_inputs_digest[SHA256_HEX_CAPACITY] = {0};
        bq_digest(inputs_changed, inputs_size, (char8*)mutated_inputs_digest);
        char input_mutation_path[160] = {0};
        int input_mutation_length = snprintf(input_mutation_path, sizeof(input_mutation_path),
                                             "%s/mutated-inputs.tsv", directory);
        BQ_PREP_CHECK(mutated_flag && input_mutation_length > 0 &&
                      (size_t)input_mutation_length < sizeof(input_mutation_path) &&
                      bq_prep_test_write_bytes(input_mutation_path, inputs_changed, inputs_size));
        if (mutated_flag && input_mutation_length > 0 &&
            (size_t)input_mutation_length < sizeof(input_mutation_path))
        {
            int mutated_inputs_fd = open(input_mutation_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(mutated_inputs_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, mutated_inputs_fd, rows_fd,
                                                           string_from_pointer(profile), &prepared, trusted,
                                                           verified_inputs, verified_rows));
            char mutated_inputs_profile[384] = {0};
            int mutated_profile_length = snprintf(mutated_inputs_profile, sizeof(mutated_inputs_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%.64s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, mutated_inputs_digest, prepared.census_sha256);
            BQ_PREP_CHECK(mutated_profile_length > 0 &&
                          (size_t)mutated_profile_length < sizeof(mutated_inputs_profile) &&
                          mutated_inputs_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, mutated_inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_inputs_profile), &prepared,
                                                           trusted, verified_inputs, verified_rows));
            if (mutated_inputs_fd >= 3) BQ_PREP_CHECK(close(mutated_inputs_fd) == 0);
            BQ_PREP_CHECK(unlink(input_mutation_path) == 0);
        }

        memcpy(inputs_changed, inputs, inputs_size);
        char* mutated_source_sha = strstr(inputs_changed,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        BQ_PREP_CHECK(mutated_source_sha != NULL);
        if (mutated_source_sha) memset(mutated_source_sha, 'e', 64);
        bq_digest(inputs_changed, inputs_size, (char8*)mutated_inputs_digest);
        input_mutation_length = snprintf(input_mutation_path, sizeof(input_mutation_path),
                                         "%s/mutated-input-source.tsv", directory);
        BQ_PREP_CHECK(mutated_source_sha && input_mutation_length > 0 &&
                      (size_t)input_mutation_length < sizeof(input_mutation_path) &&
                      bq_prep_test_write_bytes(input_mutation_path, inputs_changed, inputs_size));
        if (mutated_source_sha && input_mutation_length > 0 &&
            (size_t)input_mutation_length < sizeof(input_mutation_path))
        {
            int mutated_inputs_fd = open(input_mutation_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            char mutated_inputs_profile[384] = {0};
            int mutated_profile_length = snprintf(mutated_inputs_profile, sizeof(mutated_inputs_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%.64s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, mutated_inputs_digest, prepared.census_sha256);
            BQ_PREP_CHECK(mutated_profile_length > 0 &&
                          (size_t)mutated_profile_length < sizeof(mutated_inputs_profile) &&
                          mutated_inputs_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, mutated_inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_inputs_profile), &prepared,
                                                           trusted, verified_inputs, verified_rows));
            if (mutated_inputs_fd >= 3) BQ_PREP_CHECK(close(mutated_inputs_fd) == 0);
            BQ_PREP_CHECK(unlink(input_mutation_path) == 0);
        }

        memcpy(changed, text, used);
        char* allocator = strstr(changed + sizeof(census_header) - 1,
                                 "\tbaseline\tfixture-features\tnone\t");
        BQ_PREP_CHECK(allocator != NULL);
        if (allocator) memcpy(allocator + strlen("\tbaseline\tfixture-features\t"), "fast", 4);
        char allocator_digest[SHA256_HEX_CAPACITY] = {0};
        bq_digest(changed, used, (char8*)allocator_digest);
        char allocator_mutation_path[160];
        int allocator_path_length = snprintf(allocator_mutation_path, sizeof(allocator_mutation_path),
                                            "%s/mutated-allocator.tsv", directory);
        BQ_PREP_CHECK(allocator && allocator_path_length > 0 &&
                      (size_t)allocator_path_length < sizeof(allocator_mutation_path) &&
                      bq_prep_test_write_bytes(allocator_mutation_path, changed, used));
        if (allocator && allocator_path_length > 0 &&
            (size_t)allocator_path_length < sizeof(allocator_mutation_path))
        {
            char mutated_profile[384] = {0};
            int mutated_length = snprintf(mutated_profile, sizeof(mutated_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, inputs_pin, allocator_digest);
            memcpy(prepared.census_sha256, allocator_digest, SHA256_HEX_CAPACITY);
            rows_fd = open(allocator_mutation_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(mutated_length > 0 && (size_t)mutated_length < sizeof(mutated_profile) && rows_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_profile), &prepared, trusted,
                                                           verified_inputs, verified_rows));
            if (rows_fd >= 3) BQ_PREP_CHECK(close(rows_fd) == 0);
            bq_digest(text, used, (char8*)prepared.census_sha256);
            BQ_PREP_CHECK(unlink(allocator_mutation_path) == 0);
        }

        memcpy(changed, text, used);
        char* ordinal = strstr(changed + sizeof(census_header) - 1, "\n17\t4\t");
        BQ_PREP_CHECK(ordinal != NULL);
        if (ordinal) memcpy(ordinal + 1, "16", 2);
        char ordinal_digest[SHA256_HEX_CAPACITY] = {0};
        bq_digest(changed, used, (char8*)ordinal_digest);
        char ordinal_mutation_path[160];
        int ordinal_path_length = snprintf(ordinal_mutation_path, sizeof(ordinal_mutation_path),
                                           "%s/mutated-ordinal.tsv", directory);
        BQ_PREP_CHECK(ordinal && ordinal_path_length > 0 &&
                      (size_t)ordinal_path_length < sizeof(ordinal_mutation_path) &&
                      bq_prep_test_write_bytes(ordinal_mutation_path, changed, used));
        if (ordinal && ordinal_path_length > 0 && (size_t)ordinal_path_length < sizeof(ordinal_mutation_path))
        {
            char mutated_profile[384] = {0};
            int mutated_length = snprintf(mutated_profile, sizeof(mutated_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, inputs_pin, ordinal_digest);
            memcpy(prepared.census_sha256, ordinal_digest, SHA256_HEX_CAPACITY);
            rows_fd = open(ordinal_mutation_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(mutated_length > 0 && (size_t)mutated_length < sizeof(mutated_profile) && rows_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_profile), &prepared, trusted,
                                                           verified_inputs, verified_rows));
            if (rows_fd >= 3) BQ_PREP_CHECK(close(rows_fd) == 0);
            bq_digest(text, used, (char8*)prepared.census_sha256);
            BQ_PREP_CHECK(unlink(ordinal_mutation_path) == 0);
        }

        memcpy(changed, text, used);
        char* row_recipe = strstr(changed + sizeof(census_header) - 1, "\tcompiler-default\t");
        BQ_PREP_CHECK(row_recipe != NULL);
        if (row_recipe) row_recipe[strlen("\tcompiler-default") - 1] = 'x';
        char row_recipe_digest[SHA256_HEX_CAPACITY] = {0};
        bq_digest(changed, used, (char8*)row_recipe_digest);
        char row_recipe_path[160] = {0};
        int row_recipe_path_length = snprintf(row_recipe_path, sizeof(row_recipe_path),
                                               "%s/mutated-row-recipe.tsv", directory);
        BQ_PREP_CHECK(row_recipe && row_recipe_path_length > 0 &&
                      (size_t)row_recipe_path_length < sizeof(row_recipe_path) &&
                      bq_prep_test_write_bytes(row_recipe_path, changed, used));
        if (row_recipe && row_recipe_path_length > 0 &&
            (size_t)row_recipe_path_length < sizeof(row_recipe_path))
        {
            char mutated_profile[384] = {0};
            int mutated_length = snprintf(mutated_profile, sizeof(mutated_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, inputs_pin, row_recipe_digest);
            memcpy(prepared.census_sha256, row_recipe_digest, SHA256_HEX_CAPACITY);
            rows_fd = open(row_recipe_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(mutated_length > 0 && (size_t)mutated_length < sizeof(mutated_profile) && rows_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_profile), &prepared, trusted,
                                                           verified_inputs, verified_rows));
            if (rows_fd >= 3) BQ_PREP_CHECK(close(rows_fd) == 0);
            bq_digest(text, used, (char8*)prepared.census_sha256);
            BQ_PREP_CHECK(unlink(row_recipe_path) == 0);
        }

        memcpy(changed, text, used);
        char* fixture = strstr(changed + sizeof(census_header) - 1, "tests/a.c");
        BQ_PREP_CHECK(fixture != NULL);
        if (fixture) memcpy(fixture, "tests/b.c", strlen("tests/b.c"));
        char source_digest[SHA256_HEX_CAPACITY] = {0};
        bq_digest(changed, used, (char8*)source_digest);
        char source_mutation_path[160];
        int source_mutation_length = snprintf(source_mutation_path, sizeof(source_mutation_path),
                                               "%s/mutated-source.tsv", directory);
        BQ_PREP_CHECK(fixture && source_mutation_length > 0 &&
                      (size_t)source_mutation_length < sizeof(source_mutation_path) &&
                      bq_prep_test_write_bytes(source_mutation_path, changed, used));
        if (fixture && source_mutation_length > 0 &&
            (size_t)source_mutation_length < sizeof(source_mutation_path))
        {
            char mutated_profile[384] = {0};
            int mutated_length = snprintf(mutated_profile, sizeof(mutated_profile),
                "support-declaration-sha256=%.64s\ncensus-inputs-sha256=%s\ncensus-rows-sha256=%.64s\n",
                prepared.support_sha256, inputs_pin, source_digest);
            memcpy(prepared.census_sha256, source_digest, SHA256_HEX_CAPACITY);
            rows_fd = open(source_mutation_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            BQ_PREP_CHECK(mutated_length > 0 && (size_t)mutated_length < sizeof(mutated_profile) && rows_fd >= 3 &&
                          !bq_retirement_census_projection(support_fd, inputs_fd, rows_fd,
                                                           string_from_pointer(mutated_profile), &prepared, trusted,
                                                           verified_inputs, verified_rows));
            if (rows_fd >= 3) BQ_PREP_CHECK(close(rows_fd) == 0);
            bq_digest(text, used, (char8*)prepared.census_sha256);
            BQ_PREP_CHECK(unlink(source_mutation_path) == 0);
        }
        BQ_PREP_CHECK(close(inputs_fd) == 0);
        BQ_PREP_CHECK(close(support_fd) == 0);
    }
    else
    {
        if (support_fd >= 3) close(support_fd);
        if (inputs_fd >= 3) close(inputs_fd);
        if (rows_fd >= 3) close(rows_fd);
    }
    free(trusted);
    free(changed);
    free(text);
    if (support_path[0]) BQ_PREP_CHECK(unlink(support_path) == 0);
    if (inputs_path[0]) BQ_PREP_CHECK(unlink(inputs_path) == 0);
    if (rows_path[0]) BQ_PREP_CHECK(unlink(rows_path) == 0);
    BQ_PREP_CHECK(rmdir(directory) == 0);
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

/* A real service readback joins the B gate here. The miniature #508
 * declaration, including both compiler-latency stages, is synthetic and
 * cannot authorize a timing campaign. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_correctness_join(BqQueue* queue, BqJob const* job,
    int installed, int workspaces, String8 profile, char const* preparation_digest,
    char const* record_digest, BqRetirementBinaries const* observed,
    String8 workspace_root, char const* fixed_driver, char const* fixed_toolchain,
    char const* build_record_digest)
{
    BqRetirementPrepared prepared = {.rows = 3, .object_rows = 1, .native_target = 1};
    memcpy(prepared.preparation_sha256, preparation_digest, SHA256_HEX_CAPACITY);
    memset(prepared.support_sha256, '1', 64);
    memset(prepared.census_sha256, 'b', 64);
    memset(prepared.aa_second_commands_sha256, 'c', 64);
    for (u32 side = 0; side < 2; side += 1)
    {
        memcpy(prepared.source_sha256[side], observed->source_sha256[side], SHA256_HEX_CAPACITY);
        memcpy(prepared.binary_sha256[side], observed->binary_sha256[side], SHA256_HEX_CAPACITY);
    }
    BqRetirementTrustedRow rows[3] = {{.row = 0, .census_row = 0, .target = 1,
                                       .stage = BQ_RETIREMENT_STAGE_OBJECT, .classification = 1,
                                       .compiler_eligible = 1}};
    memset(rows[0].identity_sha256, '1', 64);
    memset(rows[0].source_sha256, '2', 64);
    memset(rows[0].configuration_sha256, '3', 64);
    memset(rows[0].compiler_command_sha256[0], '4', 64);
    memset(rows[0].compiler_command_sha256[1], '5', 64);
    for (u32 stage = 1; stage < BUSTER_ARRAY_LENGTH(rows); stage += 1)
    {
        rows[stage] = rows[0];
        rows[stage].row = stage;
        rows[stage].stage = stage == 1 ? BQ_RETIREMENT_STAGE_LINK : BQ_RETIREMENT_STAGE_SELF_HOST;
        memset(rows[stage].identity_sha256, (int)('1' + stage), 64);
        memset(rows[stage].configuration_sha256, (int)('3' + stage), 64);
        for (u32 side = 0; side < 2; side += 1)
            memset(rows[stage].compiler_command_sha256[side], (int)('4' + stage * 2 + side), 64);
    }
    BqRetirementRequiredCheck required[BQ_RETIREMENT_CHECK_COUNT - 1u] = {0};
    BqRetirementCheckResult checked[BQ_RETIREMENT_CHECK_COUNT - 1u] = {0};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(required); i += 1)
    {
        required[i].kind = i + 1;
        required[i].rows = i == BQ_RETIREMENT_CHECK_MATRIX - 1 ||
                           i == BQ_RETIREMENT_CHECK_NO_FALLBACK - 1 ? BUSTER_ARRAY_LENGTH(rows) : 1;
        memset(required[i].command_sha256, (int)('a' + i), 64);
        memset(required[i].configuration_sha256, (int)('1' + i), 64);
        memset(required[i].receipt_sha256, (int)('a' + i), 64);
    }
    BqRetirementRowFact facts[3] = {0};
    u32 identities[7] = {0};
    u8 census[1] = {0};
    BqRetirementHeldBinaries held = {0};
    BqRetirementCorrectness gate = {0};
    BqError begin = build_record_digest ? bq_retirement_correctness_begin_service_built_pinned(
        queue, job, installed, workspaces, workspace_root, profile, fixed_driver,
        fixed_toolchain,
        preparation_digest, record_digest, build_record_digest, &prepared, rows, required,
        BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
        census, BUSTER_ARRAY_LENGTH(census), &held, &gate) :
        bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
        profile, preparation_digest, record_digest, &prepared, rows, required,
        BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
        census, BUSTER_ARRAY_LENGTH(census), &held, &gate);
    BQ_PREP_CHECK(begin == BQ_OK &&
                  held.owned == 1 && held.descriptors[0] >= 3 && held.descriptors[1] >= 3 &&
                  !strcmp(gate.prepared.binary_sha256[0], observed->binary_sha256[0]) &&
                  !bq_retirement_correctness_ready(&gate));
    int live = held.descriptors[0];
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_RECIPE_MISMATCH &&
                  held.descriptors[0] == live && fcntl(live, F_GETFD) >= 0);
    bq_retirement_binaries_release(&held);

    char wrong_record[SHA256_HEX_CAPACITY];
    memcpy(wrong_record, record_digest, SHA256_HEX_CAPACITY);
    wrong_record[0] = wrong_record[0] == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, wrong_record, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_CORRUPT &&
                  gate.failed && !held.owned);

    char saved = prepared.binary_sha256[0][0];
    prepared.binary_sha256[0][0] = saved == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned && held.descriptors[0] == -1 && held.descriptors[1] == -1);
    prepared.binary_sha256[0][0] = saved;
    saved = prepared.source_sha256[1][0];
    prepared.source_sha256[1][0] = saved == '0' ? '1' : '0';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned);
    prepared.source_sha256[1][0] = saved;
    prepared.support_sha256[0] = '2';
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
                  census, BUSTER_ARRAY_LENGTH(census), &held, &gate) == BQ_SOURCE_MISMATCH &&
                  gate.failed && !held.owned);
    prepared.support_sha256[0] = '1';
    prepared.census_sha256[0] = 0;
    gate = (BqRetirementCorrectness){0};
    BQ_PREP_CHECK(bq_retirement_correctness_begin_service_pinned(queue, job, installed, workspaces,
                  profile, preparation_digest, record_digest, &prepared, rows, required,
                  BUSTER_ARRAY_LENGTH(required), checked, facts, identities, BUSTER_ARRAY_LENGTH(identities),
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
    sigset_t blocked = {0}, prior_mask = {0};
    struct sigaction ignore_pipe = {.sa_handler = SIG_IGN}, prior_pipe = {0};
    bool masked = ok && sigemptyset(&blocked) == 0 && sigaddset(&blocked, SIGTERM) == 0 &&
                  sigaddset(&blocked, SIGINT) == 0 &&
                  sigprocmask(SIG_BLOCK, &blocked, &prior_mask) == 0;
    bool ignored = masked && sigemptyset(&ignore_pipe.sa_mask) == 0 &&
                   sigaction(SIGPIPE, &ignore_pipe, &prior_pipe) == 0;
    mode_t prior_umask = umask(0000);
    ok = ok && masked && ignored;
    pid_t child = ok ? fork() : -1;
    if (child == 0) bq_retirement_build_exec_fd(executable, writer, attempt, source, &stage);
    umask(prior_umask);
    if (ignored && sigaction(SIGPIPE, &prior_pipe, NULL) != 0) ok = false;
    if (masked && sigprocmask(SIG_SETMASK, &prior_mask, NULL) != 0) ok = false;
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

/* Preserve the produced executable inode while replacing the configured build
 * root. A path-only freeze would otherwise accept the same bytes from a root
 * that the reaped child never used. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_swap_build_root(int attempt)
{
    bool ok = renameat(attempt, "matched-build", attempt, "held-build") == 0 &&
              mkdirat(attempt, "matched-build", 0700) == 0;
    int held = ok ? openat(attempt, "held-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int replacement = ok ? openat(attempt, "matched-build", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && held >= 3 && replacement >= 3 && mkdirat(replacement, "Release", 0700) == 0;
    int old_release = ok ? openat(held, "Release", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int new_release = ok ? openat(replacement, "Release", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && old_release >= 3 && new_release >= 3 &&
         linkat(old_release, "ide", new_release, "ide", 0) == 0 &&
         unlinkat(old_release, "ide", 0) == 0;
    if (new_release >= 0 && close(new_release) != 0) ok = false;
    if (old_release >= 0 && close(old_release) != 0) ok = false;
    if (replacement >= 0 && close(replacement) != 0) ok = false;
    if (held >= 0 && close(held) != 0) ok = false;
    return ok;
}

/* Check the freeze's held output against the name and metadata the child
 * produced. These changes leave the held descriptor valid while invalidating
 * the pathname or rewriting the file without changing its size. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_build_output_stability(int root)
{
    int release = root >= 3 ? openat(root, "Release",
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int input = release >= 3 ? openat(release, "ide", O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    struct stat first = {0};
    bool ok = release >= 3 && input >= 3 && fstat(input, &first) == 0 &&
              bq_retirement_build_output_stable(root, release, input, &first);
    BQ_PREP_CHECK(ok);
    if (ok)
    {
        bool moved = renameat(root, "Release", root, "held-release") == 0;
        bool refused = moved && !bq_retirement_build_output_stable(root, release, input, &first);
        bool restored = moved && renameat(root, "held-release", root, "Release") == 0;
        BQ_PREP_CHECK(moved && refused && restored &&
            bq_retirement_build_output_stable(root, release, input, &first));
        ok = restored;
    }
    if (ok)
    {
        int copy = openat(release, "ide-replacement",
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        bool copied = copy >= 3;
        u8 bytes[16384];
        u64 offset = 0;
        while (copied && offset < (u64)first.st_size)
        {
            size_t wanted = (u64)first.st_size - offset < sizeof(bytes) ?
                            (size_t)((u64)first.st_size - offset) : sizeof(bytes);
            ssize_t count = pread(input, bytes, wanted, (off_t)offset);
            if (count < 0 && errno == EINTR) continue;
            copied = count > 0 && bq_write_all(copy, bytes, (u32)count);
            if (copied) offset += (u64)count;
        }
        copied = copied && fchmod(copy, first.st_mode & 07777) == 0;
        if (copy >= 0 && close(copy) != 0) copied = false;
        bool moved = copied && renameat(release, "ide", release, "held-ide") == 0;
        bool replaced = moved && renameat(release, "ide-replacement", release, "ide") == 0;
        bool refused = replaced && !bq_retirement_build_output_stable(root, release, input, &first);
        bool removed = replaced && renameat(release, "ide", release, "ide-replacement") == 0;
        bool restored = moved && renameat(release, "held-ide", release, "ide") == 0;
        bool refreshed = restored && fstat(input, &first) == 0;
        BQ_PREP_CHECK(copied && moved && replaced && refused && removed && refreshed &&
            bq_retirement_build_output_stable(root, release, input, &first));
        ok = refreshed && unlinkat(release, "ide-replacement", 0) == 0;
        BQ_PREP_CHECK(ok);
    }
    if (ok)
    {
        u8 original = 0, changed = 0;
        int writer = openat(release, "ide", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        bool writable = writer >= 3 && pread(input, &original, 1, 0) == 1;
        changed = original ^ 0xffu;
        struct timespec timestamp[2] = {
            {.tv_nsec = UTIME_OMIT}, {.tv_sec = first.st_mtime == 1 ? 2 : 1, .tv_nsec = 0}};
        bool rewritten = writable && pwrite(writer, &changed, 1, 0) == 1 &&
            futimens(writer, timestamp) == 0;
        struct stat after = {0};
        bool refused = rewritten && fstat(input, &after) == 0 &&
            after.st_size == first.st_size && after.st_ino == first.st_ino &&
            !bq_retirement_build_output_stable(root, release, input, &first);
        bool restored = rewritten && pwrite(writer, &original, 1, 0) == 1;
        if (writer >= 0 && close(writer) != 0) restored = false;
        struct stat fresh = {0};
        BQ_PREP_CHECK(writable && rewritten && refused && restored &&
            fstat(input, &fresh) == 0 &&
            bq_retirement_build_output_stable(root, release, input, &fresh));
    }
    if (input >= 0) close(input);
    if (release >= 0) close(release);
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
    for (u32 trial = 0; trial < 18; trial += 1)
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
        BqRetirementStore store = bq_retirement_queue_store(queue);
        BQ_PREP_CHECK(ok && bq_retirement_preparation_record(store, &job, &prepared, BQ_OK, 2));
        char preparation_digest[SHA256_HEX_CAPACITY] = {0};
        String8 pinned = string_from_pointer(profile);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
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
            !strcmp(command.env[0], checked.path) && command.file_umask == 0077);
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
        if (trial == 9)
        {
            BqRetirementBuildProcess flooded = {0};
            int exit_code = -1;
            BQ_PREP_CHECK(bq_prep_test_run_stage(&build, &flooded, &exit_code) &&
                exit_code == 0 && flooded.log_eof && flooded.log_overflow &&
                !flooded.capture_failed && flooded.log_bytes == BQ_RETIREMENT_BUILD_LOG_CAP);
            struct stat bounded = {0};
            BQ_PREP_CHECK(flooded.writer >= 3 && fstat(flooded.writer, &bounded) == 0 &&
                bounded.st_size == BQ_RETIREMENT_BUILD_LOG_CAP &&
                bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &flooded, geteuid(), &build) ==
                    BQ_WORKER_FAILED && build.failed && !build.next && !flooded.state);
        }
        for (u32 stage = 0; ok && stage < (trial ? 4u : 1u); stage += 1)
        {
            if (trial >= 6 && trial < 10) break;
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
            if (trial && trial != 16 && stage == 2)
                ok = renameat(attempt, "matched-build", attempt, "old-build") == 0;
            if ((trial == 10 && stage == 1) || (trial == 11 && stage == 3))
            {
                int root = bq_open_absolute_directory(string_from_pointer(build.build));
                BQ_PREP_CHECK(root >= 3 && mkdirat(root, "Release", 0700) == 0);
                int release = root >= 3 ? openat(root, "Release",
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
                int stale = release >= 3 ? openat(release, "ide",
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0500) : -1;
                BQ_PREP_CHECK(stale >= 3 &&
                    bq_write_all(stale, (u8 const*)"stale executable\n", 17));
                if (stale >= 0) close(stale);
                if (release >= 0) close(release);
                if (root >= 0) close(root);
                BqRetirementBuildProcess refused = {0};
                BQ_PREP_CHECK(!bq_retirement_matched_build_launch(&build, &refused) &&
                    build.failed && !refused.process && !refused.state);
                char log_name[32];
                snprintf(log_name, sizeof(log_name), "build-log-%u", stage);
                struct stat log_stat = {0};
                BQ_PREP_CHECK(fstatat(attempt, log_name, &log_stat, AT_SYMLINK_NOFOLLOW) != 0 &&
                    errno == ENOENT);
                break;
            }
            if ((trial == 14 && stage == 1) || (trial == 15 && stage == 3))
            {
                int held = build.generated_root;
                struct stat previous = {0}, replacement = {0};
                BQ_PREP_CHECK(held >= 3 && fstat(held, &previous) == 0 &&
                    renameat(attempt, "matched-build", attempt, "held-generated") == 0 &&
                    mkdirat(attempt, "matched-build", 0700) == 0);
                int substituted = openat(attempt, "matched-build",
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                BQ_PREP_CHECK(substituted >= 3 && fstat(substituted, &replacement) == 0 &&
                    (previous.st_dev != replacement.st_dev || previous.st_ino != replacement.st_ino));
                if (substituted >= 0) close(substituted);
                BqRetirementBuildProcess refused = {0};
                BQ_PREP_CHECK(!bq_retirement_matched_build_launch(&build, &refused) &&
                    build.failed && build.generated_root == -1 &&
                    !refused.state && !refused.process &&
                    fcntl(held, F_GETFD) < 0 && errno == EBADF);
                char log_name[32];
                snprintf(log_name, sizeof(log_name), "build-log-%u", stage);
                struct stat absent = {0};
                BQ_PREP_CHECK(fstatat(attempt, log_name, &absent, AT_SYMLINK_NOFOLLOW) != 0 &&
                    errno == ENOENT);
                break;
            }
            BqRetirementBuildStage current = {0};
            ok = ok && bq_retirement_matched_build_stage(&build, &current) &&
                 !strcmp(current.argv[3], command.argv[3]) &&
                 current.file_umask == (stage < 2 ? 0077 : 0007);
            BqRetirementBuildProcess process = {0};
            int exit_code = -1;
            if (ok) ok = bq_prep_test_run_stage(&build, &process, &exit_code);
            BQ_PREP_CHECK(ok && (trial ? exit_code == 0 : exit_code == 5));
            if (ok)
            {
                int held_root = process.build_root;
                int held_generated = build.generated_root;
                if (trial == 1 && stage == 1)
                    bq_prep_test_build_output_stability(held_root);
                if ((trial == 12 && stage == 1) || (trial == 13 && stage == 3))
                {
                    struct stat original = {0}, replacement_stat = {0};
                    BQ_PREP_CHECK(held_root >= 3 && fstat(held_root, &original) == 0 &&
                        bq_prep_test_swap_build_root(attempt));
                    int replacement = openat(attempt, "matched-build",
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                    BQ_PREP_CHECK(replacement >= 3 && fstat(replacement, &replacement_stat) == 0 &&
                        (original.st_dev != replacement_stat.st_dev ||
                         original.st_ino != replacement_stat.st_ino) &&
                        fstat(held_root, &original) == 0);
                    if (replacement >= 0) close(replacement);
                }
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
                uid_t candidate = ((trial == 2 && stage == 3) ||
                                   (trial == 17 && stage == 2)) ? geteuid() + 1 : geteuid();
                BqError expected = !trial ? BQ_WORKER_FAILED :
                                   trial == 4 || trial == 5 ? BQ_WORKER_FAILED :
                                   ((trial == 2 || trial == 13) && stage == 3) ||
                                   (trial == 12 && stage == 1) ||
                                   ((trial == 16 || trial == 17) && stage == 2) ?
                                   BQ_SOURCE_MISMATCH : BQ_OK;
                BQ_PREP_CHECK(bq_retirement_matched_build_complete_pinned(queue, &job,
                    installed, workspaces, pinned, &process, candidate, &build) == expected &&
                    !process.state && !process.process);
                struct stat generated_after = {0};
                if (expected == BQ_OK && !(stage & 1u))
                    BQ_PREP_CHECK(build.generated_root >= 3 &&
                        fstat(build.generated_root, &generated_after) == 0 &&
                        (u64)generated_after.st_dev == build.generated_device &&
                        (u64)generated_after.st_ino == build.generated_inode);
                if (stage & 1u)
                    BQ_PREP_CHECK(build.generated_root == -1 &&
                        fcntl(held_generated, F_GETFD) < 0 && errno == EBADF);
                if ((trial == 12 && stage == 1) || (trial == 13 && stage == 3))
                    BQ_PREP_CHECK(fcntl(held_root, F_GETFD) < 0 && errno == EBADF);
            }
            bq_retirement_matched_build_abort(&process);
            if (trial == 4 || trial == 5 || (trial == 12 && stage == 1) ||
                ((trial == 16 || trial == 17) && stage == 2)) break;
        }
        BQ_PREP_CHECK(ok);
        if (trial != 1)
        {
            char record_name[48], bytes[16];
            u32 size = 0;
            BQ_PREP_CHECK(build.failed && build.next == (trial == 3 || trial == 16 || trial == 17 ? 2u :
                                                    trial == 2 || trial == 11 || trial == 13 || trial == 15 ? 3u :
                                                    trial == 10 || trial == 12 || trial == 14 ? 1u : 0u) &&
                !build.binary_record_sha256[0] &&
                !bq_retirement_matched_build_stage(&build, &command) &&
                bq_record_name(record_name, "binaries", job.id) &&
                bq_record_read(queue, record_name, (u8*)bytes, sizeof(bytes), &size) == BQ_NOT_FOUND &&
                (build.stage_receipt_sha256[0][0] != 0) == (trial < 4 || trial >= 10));
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
        BQ_PREP_CHECK(bq_retirement_matched_build_release(&build) && build.generated_root == -1);
        if (attempt >= 0) close(attempt);
    }
}

/* Installed #1020 reference template and inventory for the real A fixture's
 * subjects and toolchain bundle, plus a synthetic profile carrying their pins. */
typedef struct BqPrepReferenceFixture
{
    char recipes[512], template_path[576], inventory_path[576];
    char toolchain_root[BQ_RETIREMENT_TOOLCHAIN_PATH_CAP];
    char template_sha256[SHA256_HEX_CAPACITY], inventory_sha256[SHA256_HEX_CAPACITY];
    char clang_sha256[SHA256_HEX_CAPACITY], build_command_sha256[SHA256_HEX_CAPACITY];
    char pinned[1024];
    BqRetirementToolchain checked;
} BqPrepReferenceFixture;

BUSTER_GLOBAL_LOCAL bool bq_prep_test_reference_install(int installed, char const* installed_path,
    BqRetirementPreparation const* preparation, char const* profile, BqPrepReferenceFixture* fixture)
{
    char* recipes = fixture->recipes;
    char* template_path = fixture->template_path;
    char* inventory_path = fixture->inventory_path;
    char* toolchain_root = fixture->toolchain_root;
    int length = snprintf(recipes, sizeof(fixture->recipes), "%s/recipes", installed_path);
    bool ok = length > 0 && (size_t)length < sizeof(fixture->recipes);
    length = snprintf(template_path, sizeof(fixture->template_path),
                      "%s/native-retirement-performance-v1.reference-template", recipes);
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->template_path);
    length = snprintf(inventory_path, sizeof(fixture->inventory_path),
                      "%s/native-retirement-performance-v1.reference-inventory", recipes);
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->inventory_path);
    length = snprintf(toolchain_root, sizeof(fixture->toolchain_root),
                      "%s/toolchain/native-retirement-performance-v1", installed_path);
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->toolchain_root);
    ok = ok && bq_retirement_toolchain_verify(installed, string_from_pointer(profile), toolchain_root,
                                              &fixture->checked) == BQ_OK;
    bq_digest("fixture only\n", 13, (char8*)fixture->clang_sha256);

    BqRetirementOracleTemplateRow approved = {.row = 1, .census_row = 0, .target = 1};
    bq_digest("int a;\n", 7, (char8*)approved.source_sha256);
    memset(approved.configuration_sha256, '5', 64);
    strcpy(approved.output_name, "oracle-output");
    BqRetirementOracleTemplate template = {.population_rows = 2, .object_rows = 1, .native_target = 1,
                                           .reference_count = 1, .references = &approved};
    BqRetirementReferenceSourceIdentity source[2] = {0};
    for (u32 side = 0; side < 2; side += 1)
    {
        memcpy(source[side].commit, preparation->subjects[side].commit, 40);
        memcpy(source[side].tree, preparation->subjects[side].tree, 40);
        memcpy(source[side].manifest_sha256, preparation->subjects[side].manifest_sha256, 64);
        memcpy(template.source_commit[side], source[side].commit, 41);
        memcpy(template.source_tree[side], source[side].tree, 41);
        memcpy(template.source_sha256[side], source[side].manifest_sha256, 65);
    }
    memset(template.census_sha256, '3', 64);
    memset(template.population_sha256, '4', 64);
    ok = ok && bq_retirement_profile_sha(string_from_pointer(profile), S8("support-declaration-sha256="),
                                         template.support_sha256) &&
         bq_retirement_profile_sha(string_from_pointer(profile), S8("toolchain-manifest-sha256="),
                                   template.toolchain_identity_sha256);
    BqRetirementReferencePlanRow row = {.row = 1, .source_side = 0, .flag_count = 1,
                                        .build_environment_count = 1, .runtime_argument_count = 1,
                                        .runtime_environment_count = 1};
    strcpy(row.source_path, "src/main.c");
    memcpy(row.source_sha256, approved.source_sha256, 65);
    row.flags[0] = "-std=c11";
    row.build_environment[0] = "LC_ALL=C";
    row.runtime_environment[0] = "LC_ALL=C";
    BqRetirementReferencePlan plan = {.template = &template, .rows = &row, .count = 1};
    memcpy(plan.clang_sha256, fixture->clang_sha256, 65);
    u8 template_bytes[2048];
    u64 template_length = 0;
    ok = ok && bq_ref_plan_row(&row, &approved, approved.build_command_sha256) &&
         bq_ref_runtime_command(&row, approved.logical_command_sha256) &&
         bq_retirement_oracle_template_hash(&template, fixture->template_sha256) &&
         bq_retirement_reference_template_write(&template, template_bytes, sizeof(template_bytes),
                                                &template_length) &&
         chmod(recipes, 0700) == 0 &&
         bq_prep_test_write_bytes(template_path, (char const*)template_bytes, (u32)template_length);
    int writer = ok ? open(inventory_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600) : -1;
    ok = ok && writer >= 3 && bq_retirement_reference_inventory_encode(&plan, source,
             template.toolchain_identity_sha256, writer, fixture->inventory_sha256) && fchmod(writer, 0400) == 0;
    if (writer >= 0 && close(writer) != 0) ok = false;
    ok = ok && chmod(recipes, 0500) == 0;
    memcpy(fixture->build_command_sha256, approved.build_command_sha256, SHA256_HEX_CAPACITY);
    length = ok ? snprintf(fixture->pinned, sizeof(fixture->pinned),
                           "%sreference-template-sha256=%s\nreference-inventory-sha256=%s\n"
                           "census-rows-sha256=%.64s\n", profile, fixture->template_sha256,
                           fixture->inventory_sha256, template.census_sha256) : -1;
    ok = ok && length > 0 && (size_t)length < sizeof(fixture->pinned);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_reference_remove(BqPrepReferenceFixture const* fixture)
{
    bool ok = chmod(fixture->recipes, 0700) == 0 && unlink(fixture->template_path) == 0 &&
              unlink(fixture->inventory_path) == 0 && chmod(fixture->recipes, 0500) == 0;
    return ok;
}

/* Open descriptors, counted through /proc; the listing's own is excluded by
 * counting the same way before and after. */
BUSTER_GLOBAL_LOCAL u32 bq_prep_test_open_descriptors(void)
{
    DIR* listing = opendir("/proc/self/fd");
    u32 count = 0;
    for (struct dirent* entry = listing ? readdir(listing) : NULL; entry; entry = readdir(listing))
    {
        if (entry->d_name[0] != '.') count += 1;
    }
    if (listing) closedir(listing);
    return count;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_export_file(int attempt, char const* name, mode_t mode)
{
    int directory = openat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = directory >= 0 && fchmod(directory, 0700) == 0 && fchmodat(directory, name, mode, 0) == 0 &&
              fchmod(directory, BQ_RETIREMENT_EXPORT_MODE) == 0;
    if (directory >= 0 && close(directory) != 0) ok = false;
    return ok;
}

/* Flip, or restore, one byte of an exported file in place. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_export_byte(int attempt, char const* name, off_t offset, char* byte)
{
    int directory = openat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = directory >= 0 && fchmod(directory, 0700) == 0 && fchmodat(directory, name, 0600, 0) == 0;
    int file = ok ? openat(directory, name, O_RDWR | O_CLOEXEC | O_NOFOLLOW) : -1;
    char previous = 0;
    ok = ok && file >= 0 && pread(file, &previous, 1, offset) == 1 && pwrite(file, byte, 1, offset) == 1;
    if (ok) *byte = previous;
    if (file >= 0 && close(file) != 0) ok = false;
    if (directory >= 0)
    {
        if (fchmodat(directory, name, 0400, 0) != 0 || fchmod(directory, BQ_RETIREMENT_EXPORT_MODE) != 0) ok = false;
        if (close(directory) != 0) ok = false;
    }
    return ok;
}

/* #1020 worker-unit handoff on the real A fixture: the coordinator export
 * and the unit-side prepare through its profile seam. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_prepare(BqQueue* queue, BqJob const* job, int installed,
    int workspaces, int attempt, char const* installed_path, BqRetirementPreparation const* prepared,
    char const* profile, char const* digest)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    BqPrepReferenceFixture fixture = {0};
    struct stat attempt_info = {0};
    bool ok = bq_prep_test_reference_install(installed, installed_path, prepared, profile, &fixture) &&
              bq_workspace_seal(attempt, job, true) && fstat(attempt, &attempt_info) == 0 &&
              (attempt_info.st_mode & S_ISGID);
    BQ_PREP_CHECK(ok);
    String8 pinned = string_from_pointer(fixture.pinned);
    char record_name[48], request_name[48];
    ok = ok && bq_record_name(record_name, "preparation", job->id) && bq_record_name(request_name, "request", job->id);

    /* Export: only the verified digest, only once, into a sealed directory. */
    char altered[SHA256_HEX_CAPACITY];
    memcpy(altered, digest, sizeof(altered));
    altered[0] = altered[0] == 'a' ? 'b' : 'a';
    struct stat info = {0};
    BQ_PREP_CHECK(ok && bq_retirement_preparation_export(bq_retirement_queue_store(queue), job, workspaces,
                  altered) == BQ_CORRUPT &&
                  fstatat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
    BQ_PREP_CHECK(bq_retirement_preparation_export(bq_retirement_queue_store(queue), job, workspaces,
                  digest) == BQ_OK);
    BQ_PREP_CHECK(bq_retirement_preparation_export(bq_retirement_queue_store(queue), job, workspaces,
                  digest) == BQ_WORKSPACE_MISMATCH);
    BQ_PREP_CHECK(fstatat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                  S_ISDIR(info.st_mode) && (info.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE &&
                  info.st_gid == attempt_info.st_gid);
    BqRetirementStore store = {-1};
    BQ_PREP_CHECK(bq_retirement_unit_store_open(workspaces, job->id, job->token, &store) == BQ_OK &&
                  store.directory >= 0);
    u8 exported[2048], original[2048];
    u32 exported_size = 0, original_size = 0;
    char exported_sha256[SHA256_HEX_CAPACITY] = {0};
    BQ_PREP_CHECK(bq_record_read_at(store.directory, record_name, exported, sizeof(exported), &exported_size) ==
                  BQ_OK && bq_record_read(queue, record_name, original, sizeof(original), &original_size) == BQ_OK &&
                  exported_size == original_size && !memcmp(exported, original, exported_size));
    bq_digest(exported, exported_size, (char8*)exported_sha256);
    BQ_PREP_CHECK(!strcmp(exported_sha256, digest));
    BQ_PREP_CHECK(bq_record_read_at(store.directory, request_name, exported, sizeof(exported), &exported_size) ==
                  BQ_OK && exported_size == job->request.size && !memcmp(exported, job->request.bytes, exported_size));
    BQ_PREP_CHECK(fstatat(store.directory, record_name, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                  S_ISREG(info.st_mode) && info.st_nlink == 1 && (info.st_mode & 07777) == 0400);

    /* Unit side: A re-import, toolchain and pinned reference policy. */
    BqRetirementUnitPrepared unit = {0};
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                  fixture.toolchain_root, digest, &unit) == BQ_OK && unit.owned &&
                  !strcmp(unit.preparation_sha256, digest) &&
                  !memcmp(unit.job.digest, job->digest, SHA256_HEX_CAPACITY) &&
                  unit.job.id == job->id && unit.job.token == job->token &&
                  !strcmp(unit.preparation.inventory_sha256, prepared->inventory_sha256) &&
                  !strcmp(unit.preparation.subjects[0].materialized_identity_sha256,
                          prepared->subjects[0].materialized_identity_sha256) &&
                  !strcmp(unit.preparation.subjects[1].materialized_identity_sha256,
                          prepared->subjects[1].materialized_identity_sha256) &&
                  !strcmp(unit.toolchain.manifest_sha256, fixture.checked.manifest_sha256) &&
                  !strcmp(unit.policy.inventory_sha256, fixture.inventory_sha256) &&
                  !strcmp(unit.policy.template_sha256, fixture.template_sha256) &&
                  unit.policy.plan.template == &unit.policy.template && unit.policy.clang >= 3);
    /* The held inventory is the exact installed file a producer_begin rehashes. */
    char held_sha256[SHA256_HEX_CAPACITY] = {0};
    int flags = unit.policy.inventory >= 3 ? fcntl(unit.policy.inventory, F_GETFL) : -1;
    BQ_PREP_CHECK(unit.policy.inventory >= 3 && (fcntl(unit.policy.inventory, F_GETFD) & FD_CLOEXEC) &&
                  flags >= 0 && (flags & O_ACCMODE) == O_RDONLY &&
                  bq_retirement_oracle_file_hash(unit.policy.inventory, BQ_RETIREMENT_REFERENCE_INVENTORY_CAP,
                                                 false, held_sha256) &&
                  !strcmp(held_sha256, fixture.inventory_sha256));
    BqRetirementUnitPrepared live = unit;
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                  fixture.toolchain_root, digest, &unit) == BQ_BAD_REQUEST && unit.owned &&
                  unit.policy.inventory == live.policy.inventory && unit.policy.clang == live.policy.clang);
    BQ_PREP_CHECK(bq_retirement_unit_release(&unit) && !unit.owned && unit.policy.clang == -1 &&
                  unit.policy.inventory == -1);

    /* Fail closed: blocked profile, missing reference pins, stale handoff
     * digest and another attempt's identity. */
    BQ_PREP_CHECK(bq_retirement_unit_prepare(store, workspaces, installed, job->id, job->token, digest, &unit) ==
                  BQ_RECIPE_MISMATCH && !unit.owned && unit.policy.inventory == -1);
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token,
                  string_from_pointer(profile), fixture.toolchain_root, digest, &unit) == BQ_RECIPE_MISMATCH &&
                  !unit.owned && unit.policy.inventory == -1);
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                  fixture.toolchain_root, altered, &unit) == BQ_RECIPE_MISMATCH && !unit.owned &&
                  !unit.preparation.inventory_sha256[0]);
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token + 1,
                  pinned, fixture.toolchain_root, digest, &unit) != BQ_OK && !unit.owned);

    /* Tampered exported bytes: the record and the request it names. */
    char byte = 'X';
    BQ_PREP_CHECK(bq_prep_test_export_byte(attempt, record_name, 0, &byte) &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_CORRUPT && !unit.owned &&
                  bq_prep_test_export_byte(attempt, record_name, 0, &byte) && byte == 'X');
    byte = 'g';
    BQ_PREP_CHECK(bq_prep_test_export_byte(attempt, request_name, 4, &byte) &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) != BQ_OK && !unit.owned &&
                  bq_prep_test_export_byte(attempt, request_name, 4, &byte) && byte == 'g');
    BQ_PREP_CHECK(bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                  fixture.toolchain_root, digest, &unit) == BQ_OK && bq_retirement_unit_release(&unit));

    /* Directory closure: writable file or directory, extra file, extra link,
     * symlink in place of the record, and a replaced directory. */
    BQ_PREP_CHECK(bq_prep_test_export_file(attempt, request_name, 0600) &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_CORRUPT && !unit.owned &&
                  bq_prep_test_export_file(attempt, request_name, 0400));
    BQ_PREP_CHECK(fchmod(store.directory, 0700) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_WORKSPACE_MISMATCH && !unit.owned);
    int extra = openat(store.directory, "extra", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
    BQ_PREP_CHECK(extra >= 0 && close(extra) == 0 && fchmod(store.directory, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_WORKSPACE_MISMATCH && !unit.owned);
    BQ_PREP_CHECK(fchmod(store.directory, 0700) == 0 && unlinkat(store.directory, "extra", 0) == 0 &&
                  linkat(store.directory, record_name, store.directory, "linked", 0) == 0 &&
                  fchmod(store.directory, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_WORKSPACE_MISMATCH && !unit.owned);
    BQ_PREP_CHECK(fchmod(store.directory, 0700) == 0 && unlinkat(store.directory, "linked", 0) == 0 &&
                  renameat(store.directory, record_name, store.directory, "moved") == 0 &&
                  symlinkat("moved", store.directory, record_name) == 0 &&
                  fchmod(store.directory, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) != BQ_OK && !unit.owned);
    BQ_PREP_CHECK(fchmod(store.directory, 0700) == 0 && unlinkat(store.directory, record_name, 0) == 0 &&
                  renameat(store.directory, "moved", store.directory, record_name) == 0 &&
                  fchmod(store.directory, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_OK && bq_retirement_unit_release(&unit));
    BQ_PREP_CHECK(renameat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, attempt, "retirement-moved") == 0 &&
                  mkdirat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, BQ_RETIREMENT_EXPORT_MODE) == 0 &&
                  bq_retirement_unit_prepare_pinned(store, workspaces, installed, job->id, job->token, pinned,
                      fixture.toolchain_root, digest, &unit) == BQ_WORKSPACE_MISMATCH && !unit.owned &&
                  unlinkat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, AT_REMOVEDIR) == 0 &&
                  renameat(attempt, "retirement-moved", attempt, BQ_RETIREMENT_EXPORT_DIRECTORY) == 0);
    BQ_PREP_CHECK(bq_retirement_unit_release(&unit));

    /* Remove the export and seal so the later A fixtures see the attempt as before. */
    BQ_PREP_CHECK(store.directory >= 0 && fchmod(store.directory, 0700) == 0 &&
                  unlinkat(store.directory, record_name, 0) == 0 && unlinkat(store.directory, request_name, 0) == 0 &&
                  close(store.directory) == 0 && unlinkat(attempt, BQ_RETIREMENT_EXPORT_DIRECTORY, AT_REMOVEDIR) == 0 &&
                  unlinkat(attempt, ".identity", 0) == 0);
    BQ_PREP_CHECK(bq_prep_test_reference_remove(&fixture));
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors);
}

/* #1020 unit-side matched builds. The fixture broker stands in for the
 * systemd broker CLI: it records each typed request and runs the stage as
 * the test user, so these cases prove the seam, ordering, evidence and
 * cleanup, not the broker's uid split or sandbox. */
BUSTER_GLOBAL_LOCAL volatile sig_atomic_t bq_prep_test_cancel_writer = -1;

BUSTER_GLOBAL_LOCAL void bq_prep_test_cancel_handler(int signal_number)
{
    if (signal_number == SIGALRM && bq_prep_test_cancel_writer >= 0)
    {
        char byte = 1;
        ssize_t ignored = write(bq_prep_test_cancel_writer, &byte, 1);
        (void)ignored;
    }
}

/* Supervisor stand-in: answers exactly one PREPARING request, with the
 * correct acknowledgement or a corrupted one. */
BUSTER_GLOBAL_LOCAL pid_t bq_prep_test_phase_peer(BqPhaseChannel* channel, u64 job, u64 token, bool acknowledge)
{
    int pair[2] = {-1, -1};
    bool paired = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0;
    pid_t child = paired ? fork() : -1;
    if (child == 0)
    {
        close(pair[0]);
        unsigned char message[BQ_PHASE_MESSAGE_BYTES] = {0};
        bool ok = recv(pair[1], message, sizeof(message), 0) == BQ_PHASE_MESSAGE_BYTES;
        bq_phase_put(message + 40, acknowledge ? 1u : 2u);
        ok = ok && send(pair[1], message, sizeof(message), MSG_NOSIGNAL) == BQ_PHASE_MESSAGE_BYTES;
        close(pair[1]);
        _exit(ok ? 0 : 1);
    }
    if (pair[1] >= 0) close(pair[1]);
    bool ready = child > 0 && bq_phase_init(channel, pair[0], job, token);
    if (!ready && pair[0] >= 0) close(pair[0]);
    if (!ready) *channel = (BqPhaseChannel){.descriptor = -1, .failed = 1};
    return child;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_phase_peer_join(BqPhaseChannel* channel, pid_t child)
{
    bool ok = channel->descriptor < 0 || close(channel->descriptor) == 0;
    channel->descriptor = -1;
    int status = 0;
    pid_t waited = -1;
    do { if (child > 0) waited = waitpid(child, &status, 0); }
    while (waited < 0 && errno == EINTR);
    return ok && waited == child;
}

typedef struct BqPrepUnitAttempt
{
    BqJob job;
    BqRetirementStore store;
    BqRetirementUnitPrepared unit;
    char digest[SHA256_HEX_CAPACITY];
    int attempt;
} BqPrepUnitAttempt;

/* A fresh attempt of the real A fixture: materialized sources, the queue
 * record, the attempt seal, the coordinator export and the unit prepare. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_unit_attempt(BqQueue* queue, BqJob const* original, u64 id, int installed,
    int workspaces, BqRetirementPreparation const* original_preparation, char const* pinned,
    char const* toolchain_root, BqPrepUnitAttempt* out)
{
    *out = (BqPrepUnitAttempt){.job = *original, .store = {-1}, .attempt = -1,
                               .unit = {.policy = {.clang = -1, .inventory = -1}}};
    out->job.id = id;
    out->job.token = id + 10u;
    BqRetirementPreparation prepared = *original_preparation;
    char name[64];
    bool ok = bq_workspace_name(name, id, out->job.token) && mkdirat(workspaces, name, 0700) == 0;
    out->attempt = ok ? openat(workspaces, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    ok = ok && out->attempt >= 0;
    for (u32 side = 0; ok && side < 2; side += 1)
    {
        char const* subject_name = side ? "candidate" : "base";
        ok = mkdirat(out->attempt, subject_name, 0700) == 0;
        int subject = ok ? openat(out->attempt, subject_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && subject >= 0 && mkdirat(subject, "source", 02750) == 0;
        int source = ok ? openat(subject, "source", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
        ok = ok && source >= 0 && bq_copy_manifest(installed, source, bq_field(&out->job.request, 3 + side),
                                                   BQ_RETIREMENT_SOURCE_MANIFEST_CAP) &&
             bq_make_sources_read_only(source) &&
             bq_retirement_verify_subject(installed, subject, source, bq_field(&out->job.request, 3 + side),
                                          &prepared.subjects[side]);
        if (source >= 0) close(source);
        if (subject >= 0) close(subject);
    }
    BqRetirementStore queue_store = bq_retirement_queue_store(queue);
    String8 profile = string_from_pointer(pinned);
    ok = ok && bq_retirement_preparation_record(queue_store, &out->job, &prepared, BQ_OK, 2) &&
         bq_retirement_preparation_ready_pinned(queue_store, &out->job, installed, workspaces, profile,
                                                out->digest, NULL) == BQ_OK &&
         bq_workspace_seal(out->attempt, &out->job, true) &&
         bq_retirement_preparation_export(queue_store, &out->job, workspaces, out->digest) == BQ_OK &&
         bq_retirement_unit_store_open(workspaces, id, out->job.token, &out->store) == BQ_OK &&
         bq_retirement_unit_prepare_pinned(out->store, workspaces, installed, id, out->job.token, profile,
                                           toolchain_root, out->digest, &out->unit) == BQ_OK;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_prep_test_unit_attempt_close(BqPrepUnitAttempt* attempt)
{
    bool ok = bq_retirement_unit_release(&attempt->unit);
    if (attempt->store.directory >= 0 && close(attempt->store.directory) != 0) ok = false;
    if (attempt->attempt >= 0 && close(attempt->attempt) != 0) ok = false;
    attempt->store.directory = -1;
    attempt->attempt = -1;
    return ok;
}

BUSTER_GLOBAL_LOCAL u32 bq_prep_test_read_text(char const* path, char* text, u32 capacity)
{
    int file = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t count = file >= 0 ? read(file, text, capacity - 1u) : 0;
    if (file >= 0) close(file);
    u32 used = count > 0 ? (u32)count : 0;
    text[used] = 0;
    return used;
}

/* Flip, or restore, one byte of a sealed file by temporarily opening its
 * directory and the file for writing; the inode stays the same. */
BUSTER_GLOBAL_LOCAL bool bq_prep_test_flip_sealed(int parent, char const* directory_name, char const* name,
    mode_t directory_mode, mode_t file_mode)
{
    int directory = openat(parent, directory_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool ok = directory >= 0 && fchmod(directory, 0700) == 0 && fchmodat(directory, name, 0600, 0) == 0;
    int file = ok ? openat(directory, name, O_RDWR | O_CLOEXEC | O_NOFOLLOW) : -1;
    unsigned char byte = 0;
    ok = ok && file >= 0 && pread(file, &byte, 1, 0) == 1;
    byte ^= 0x20u;
    ok = ok && pwrite(file, &byte, 1, 0) == 1;
    if (file >= 0 && close(file) != 0) ok = false;
    if (directory >= 0 && (fchmodat(directory, name, file_mode, 0) != 0 || fchmod(directory, directory_mode) != 0))
        ok = false;
    if (directory >= 0 && close(directory) != 0) ok = false;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_unit_build(BqQueue* queue, BqJob const* original, int installed,
    int workspaces, char const* installed_path, char const* workspace_path, char const* profile,
    BqRetirementPreparation const* original_preparation)
{
    u32 descriptors = bq_prep_test_open_descriptors();
    BqPrepReferenceFixture fixture = {0};
    char driver[256], broker[256], launches[256], driver_sha256[SHA256_HEX_CAPACITY] = {0}, pinned[1400];
    int driver_length = snprintf(driver, sizeof(driver), "%s/fixture-driver", workspace_path);
    int broker_length = snprintf(broker, sizeof(broker), "%s/fixture-broker", workspace_path);
    int launches_length = snprintf(launches, sizeof(launches), "%s/broker-launches", workspace_path);
    bool ok = driver_length > 0 && (size_t)driver_length < sizeof(driver) && broker_length > 0 &&
              (size_t)broker_length < sizeof(broker) && launches_length > 0 &&
              (size_t)launches_length < sizeof(launches) && bq_prep_test_compile_driver(broker) &&
              bq_retirement_build_driver_sha(driver, driver_sha256) &&
              bq_prep_test_reference_install(installed, installed_path, original_preparation, profile, &fixture);
    int pinned_length = ok ? snprintf(pinned, sizeof(pinned), "%sbuild-driver-sha256=%.64s\n", fixture.pinned,
                                      driver_sha256) : -1;
    ok = ok && pinned_length > 0 && (size_t)pinned_length < sizeof(pinned);
    BQ_PREP_CHECK(ok);
    String8 root = string_from_pointer(workspace_path);
    String8 profile_pins = string_from_pointer(pinned);
    char const* toolchain_root = fixture.toolchain_root;
    int cancel[2] = {-1, -1};
    BQ_PREP_CHECK(pipe2(cancel, O_CLOEXEC | O_NONBLOCK) == 0);
    u64 generous = bq_phase_clock() + 300ull * 1000000000ull;

    /* Success: PREPARING acknowledged, then four broker requests in order,
     * sealed evidence, a clean re-import and two held executables. */
    BqPrepUnitAttempt success = {0};
    BQ_PREP_CHECK(ok && bq_prep_test_unit_attempt(queue, original, 61, installed, workspaces, original_preparation,
                                                  pinned, toolchain_root, &success));
    BqPhaseChannel phases = {.descriptor = -1};
    pid_t peer = bq_prep_test_phase_peer(&phases, 61, 71, true);
    BqRetirementUnitBuilt built = {0};
    BQ_PREP_CHECK(bq_retirement_unit_build_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, geteuid(), &phases, cancel[0], generous, &built) ==
                  BQ_OK && built.owned && phases.sequence == BQ_PHASE_PREPARING);
    BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    BQ_PREP_CHECK(built.verified.launcher == BQ_RETIREMENT_LAUNCH_BROKER && built.verified.next == 4 &&
                  !strcmp(built.verified.broker, broker) &&
                  !strcmp(built.verified.build_record_sha256, built.build_record_sha256) &&
                  !strcmp(built.binaries.verified.preparation_sha256, success.digest) &&
                  strcmp(built.binaries.verified.binary_sha256[0], built.binaries.verified.binary_sha256[1]) &&
                  built.binaries.descriptors[0] >= 3 && built.binaries.descriptors[1] >= 3 &&
                  (fcntl(built.binaries.descriptors[1], F_GETFD) & FD_CLOEXEC));
    char text[4096], expected[512];
    bq_prep_test_read_text(launches, text, sizeof(text));
    char const* names[] = {"retirement-base-generate", "retirement-base-build", "retirement-candidate-generate",
                           "retirement-candidate-build"};
    char* cursor = text;
    for (u32 stage = 0; stage < 4; stage += 1)
    {
        int length = snprintf(expected, sizeof(expected), "start-stage 61 71 %s %s %s\n", names[stage],
                              original_preparation->subjects[0].commit, original_preparation->subjects[1].commit);
        char* found = length > 0 && (size_t)length < sizeof(expected) ? strstr(cursor, expected) : NULL;
        /* The candidate stages are the broker's typed requests, in order. */
        BQ_PREP_CHECK(found == cursor);
        cursor = found ? found + length : cursor;
    }
    BQ_PREP_CHECK(*cursor == 0);
    int evidence = openat(success.attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    struct stat info = {0};
    BQ_PREP_CHECK(evidence >= 0 && fstat(evidence, &info) == 0 &&
                  (info.st_mode & 07777) == BQ_RETIREMENT_EXPORT_MODE &&
                  bq_retirement_unit_evidence_closed(evidence, workspaces, &success.job, true) &&
                  bq_retirement_unit_store_closed(success.store, workspaces, &success.job));
    if (evidence >= 0) close(evidence);

    /* Re-import, then tampered record, wrong toolchain and wrong binary. */
    BqRetirementMatchedBuild observed = {0};
    BQ_PREP_CHECK(bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, built.binary_record_sha256, built.build_record_sha256,
                  &observed) == BQ_OK && !strcmp(observed.stage_receipt_sha256[3],
                  built.verified.stage_receipt_sha256[3]));
    BQ_PREP_CHECK(bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, NULL, built.binary_record_sha256, built.build_record_sha256,
                  &observed) == BQ_CORRUPT && !observed.preparation_sha256[0]);
    char record_name[48];
    BQ_PREP_CHECK(bq_record_name(record_name, "matched-builds", success.job.id) &&
                  bq_prep_test_flip_sealed(success.attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, record_name,
                                           BQ_RETIREMENT_EXPORT_MODE, 0400) &&
                  bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces, installed, root,
                      profile_pins, driver, toolchain_root, broker, built.binary_record_sha256,
                      built.build_record_sha256, &observed) == BQ_CORRUPT && !observed.preparation_sha256[0] &&
                  bq_prep_test_flip_sealed(success.attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, record_name,
                                           BQ_RETIREMENT_EXPORT_MODE, 0400));
    char tool_bin[528], clang_path[544], saved[512];
    int bin_length = snprintf(tool_bin, sizeof(tool_bin), "%s/bin", toolchain_root);
    int clang_length = snprintf(clang_path, sizeof(clang_path), "%s/clang", tool_bin);
    int saved_length = snprintf(saved, sizeof(saved), "%s/unit-held-clang", workspace_path);
    bool swapped = bin_length > 0 && (size_t)bin_length < sizeof(tool_bin) && clang_length > 0 &&
                   (size_t)clang_length < sizeof(clang_path) && saved_length > 0 &&
                   (size_t)saved_length < sizeof(saved) && chmod(tool_bin, 0700) == 0 &&
                   rename(clang_path, saved) == 0 && bq_prep_test_write(clang_path, "fixture only\n") &&
                   chmod(clang_path, 0555) == 0 && chmod(tool_bin, 0555) == 0;
    BQ_PREP_CHECK(swapped && bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces,
                  installed, root, profile_pins, driver, toolchain_root, broker, built.binary_record_sha256,
                  built.build_record_sha256, &observed) != BQ_OK && !observed.preparation_sha256[0]);
    BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(clang_path) == 0 && rename(saved, clang_path) == 0 &&
                  chmod(tool_bin, 0555) == 0);
    BQ_PREP_CHECK(bq_prep_test_flip_sealed(success.attempt, "trusted-build", "candidate-ide", 0500, 0500) &&
                  bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces, installed, root,
                      profile_pins, driver, toolchain_root, broker, built.binary_record_sha256,
                      built.build_record_sha256, &observed) != BQ_OK && !observed.preparation_sha256[0] &&
                  bq_prep_test_flip_sealed(success.attempt, "trusted-build", "candidate-ide", 0500, 0500));
    BQ_PREP_CHECK(bq_retirement_unit_build_import_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, built.binary_record_sha256, built.build_record_sha256,
                  &observed) == BQ_OK);
    /* A live result and a second build into the same attempt are refused
     * before any child; PREPARING is already held by this channel. */
    BqRetirementUnitBuilt again = {0};
    BQ_PREP_CHECK(bq_retirement_unit_build_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, geteuid(), &phases, cancel[0], generous, &built) ==
                  BQ_BAD_REQUEST && built.owned);
    BQ_PREP_CHECK(bq_retirement_unit_build_pinned(success.store, &success.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, geteuid(), &phases, cancel[0], generous, &again) ==
                  BQ_WORKSPACE_MISMATCH && !again.owned && again.binaries.descriptors[0] == -1);
    BQ_PREP_CHECK(bq_prep_test_read_text(launches, text, sizeof(text)) == (u32)(cursor - text));
    BQ_PREP_CHECK(bq_retirement_unit_built_release(&built) && !built.owned && built.binaries.descriptors[0] == -1);
    /* The checked-in blocked profile and a profile without the driver pin
     * fail before the channel is touched. */
    BqPhaseChannel untouched = {.descriptor = -1};
    BQ_PREP_CHECK(bq_retirement_unit_build(success.store, &success.unit, workspaces, installed, root, &untouched,
                  cancel[0], generous, &again) != BQ_OK && !untouched.sequence && !again.owned);
    BQ_PREP_CHECK(bq_retirement_unit_build_pinned(success.store, &success.unit, workspaces, installed, root,
                  string_from_pointer(fixture.pinned), driver, toolchain_root, broker, geteuid(), &untouched,
                  cancel[0], generous, &again) == BQ_RECIPE_MISMATCH && !untouched.sequence && !again.owned);
    BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(&success));

    /* A refused acknowledgement launches nothing and creates no evidence. */
    BqPrepUnitAttempt refused = {0};
    BQ_PREP_CHECK(bq_prep_test_unit_attempt(queue, original, 62, installed, workspaces, original_preparation,
                                            pinned, toolchain_root, &refused));
    peer = bq_prep_test_phase_peer(&phases, 62, 72, false);
    BQ_PREP_CHECK(bq_retirement_unit_build_pinned(refused.store, &refused.unit, workspaces, installed, root,
                  profile_pins, driver, toolchain_root, broker, geteuid(), &phases, cancel[0], generous, &built) ==
                  BQ_WORKER_MISMATCH && !built.owned && phases.failed);
    BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
    BQ_PREP_CHECK(bq_prep_test_read_text(launches, text, sizeof(text)) == (u32)(cursor - text) &&
                  fstatat(refused.attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY, &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT && fstatat(refused.attempt, "build-log-0", &info, AT_SYMLINK_NOFOLLOW) != 0 &&
                  errno == ENOENT);
    BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(&refused));

    /* Cancellation (SIGTERM self-pipe, driven here by SIGALRM) and deadline
     * expiry mid-generate: the hanging stage is killed, reaped and its group
     * proven absent, and the broker is asked to KILL the stage unit. */
    for (u32 trial = 0; trial < 2; trial += 1)
    {
        u64 id = trial ? 64u : 63u;
        BqPrepUnitAttempt hanging = {0};
        BQ_PREP_CHECK(bq_prep_test_unit_attempt(queue, original, id, installed, workspaces, original_preparation,
                                                pinned, toolchain_root, &hanging));
        struct sigaction handler = {.sa_handler = bq_prep_test_cancel_handler}, prior = {0};
        struct itimerval timer = {.it_value = {0, 800000}}, stopped = {{0, 0}, {0, 0}};
        bool armed = !trial && sigemptyset(&handler.sa_mask) == 0 && sigaction(SIGALRM, &handler, &prior) == 0;
        bq_prep_test_cancel_writer = cancel[1];
        if (armed) armed = setitimer(ITIMER_REAL, &timer, NULL) == 0;
        u64 deadline = trial ? bq_phase_clock() + 800000000ull : generous;
        peer = bq_prep_test_phase_peer(&phases, id, id + 10u, true);
        BqError result = bq_retirement_unit_build_pinned(hanging.store, &hanging.unit, workspaces, installed, root,
            profile_pins, driver, toolchain_root, broker, geteuid(), &phases, cancel[0], deadline, &built);
        if (!trial)
        {
            BQ_PREP_CHECK(armed && setitimer(ITIMER_REAL, &stopped, NULL) == 0 &&
                          sigaction(SIGALRM, &prior, NULL) == 0);
            bq_prep_test_cancel_writer = -1;
            char drained[8];
            BQ_PREP_CHECK(read(cancel[0], drained, sizeof(drained)) >= 1);
        }
        BQ_PREP_CHECK(bq_prep_test_phase_peer_join(&phases, peer));
        BQ_PREP_CHECK(result == (trial ? BQ_WORKER_TIMEOUT : BQ_WORKER_CANCEL_SIGNAL) && !built.owned &&
                      built.binaries.descriptors[0] == -1);
        char pid_path[512], pid_text[32];
        int pid_length = snprintf(pid_path, sizeof(pid_path), "%s/job-%" PRIu64 "-attempt-%" PRIu64 "/matched-build/pid",
                                  workspace_path, (uint64_t)id, (uint64_t)id + 10u);
        long pid = pid_length > 0 && (size_t)pid_length < sizeof(pid_path) &&
                   bq_prep_test_read_text(pid_path, pid_text, sizeof(pid_text)) ? strtol(pid_text, NULL, 10) : 0;
        errno = 0;
        BQ_PREP_CHECK(pid > 1 && kill((pid_t)pid, 0) != 0 && errno == ESRCH);
        u32 used = bq_prep_test_read_text(launches, text, sizeof(text));
        int length = snprintf(expected, sizeof(expected), "start-stage %" PRIu64 " %" PRIu64
                              " retirement-base-generate %s %s\nsignal buster-bench-%" PRIu64 "-%" PRIu64
                              "-retirement-base-generate.service KILL\n", (uint64_t)id, (uint64_t)id + 10u,
                              original_preparation->subjects[0].commit, original_preparation->subjects[1].commit,
                              (uint64_t)id, (uint64_t)id + 10u);
        BQ_PREP_CHECK(length > 0 && used >= (u32)length && !strcmp(text + used - (u32)length, expected));
        char receipt[48];
        BQ_PREP_CHECK(bq_record_name(receipt, "matched-stage-0", id) &&
                      fstatat(hanging.attempt, "trusted-build", &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
        evidence = openat(hanging.attempt, BQ_RETIREMENT_UNIT_EVIDENCE_DIRECTORY,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        BQ_PREP_CHECK(evidence >= 0 && fstatat(evidence, receipt, &info, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
        if (evidence >= 0) close(evidence);
        BQ_PREP_CHECK(bq_prep_test_unit_attempt_close(&hanging));
    }
    if (cancel[0] >= 0) close(cancel[0]);
    if (cancel[1] >= 0) close(cancel[1]);
    BQ_PREP_CHECK(bq_prep_test_reference_remove(&fixture));
    BQ_PREP_CHECK(bq_prep_test_open_descriptors() == descriptors);
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
    BqRetirementStore store = bq_retirement_queue_store(&queue);
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
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_NOT_FOUND && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_record(store, &job, prepared, BQ_OK, 2) &&
                      bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_OK && strlen(digest) == 64);
        BqRetirementPreparation imported = {0};
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
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
        bq_prep_test_unit_prepare(&queue, &job, installed, workspaces, root, installed_path, prepared,
                                  profile, digest);
        bq_prep_test_matched_build(&queue, &job, installed, workspaces, installed_path,
                                   workspaces_path,
                                   profile, prepared);
        bq_prep_test_unit_build(&queue, &job, installed, workspaces, installed_path, workspaces_path, profile,
                                prepared);
        char binary_digest[SHA256_HEX_CAPACITY];
        bq_prep_test_binary_handoff(&queue, &job, installed, workspaces, root, profile, digest,
                                    prepared, binary_digest);
        char altered_digest[SHA256_HEX_CAPACITY];
        memcpy(altered_digest, digest, sizeof(altered_digest));
        altered_digest[0] = altered_digest[0] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), altered_digest, &imported) == BQ_RECIPE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        char invalid_digest[SHA256_HEX_CAPACITY] = "invalid";
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), invalid_digest, &imported) == BQ_RECIPE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        BqJob other = job;
        other.token += 1;
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &other, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
        other = job;
        other.digest[0] = other.digest[0] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &other, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_RECIPE_MISMATCH && !digest[0]);
        char wrong_profile[512];
        memcpy(wrong_profile, profile, strlen(profile) + 1);
        char* pin = strstr(wrong_profile, "inventory-sha256=");
        if (pin) pin[17] = pin[17] == 'a' ? 'b' : 'a';
        BQ_PREP_CHECK(pin && bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                      string_from_pointer(wrong_profile), digest, NULL) == BQ_RECIPE_MISMATCH && !digest[0]);
        char copied_path[128];
        int length = snprintf(copied_path, sizeof(copied_path), "%s/base/source/src", attempt);
        int copied = length > 0 && (u32)length < sizeof(copied_path) ?
                     bq_open_directory_path(workspaces, string_from_pointer(copied_path)) : -1;
        /* Exercise directory closure at the durable consumer, not only at
         * preflight. Failure must not damage the immutable preparation record. */
        char original_digest[SHA256_HEX_CAPACITY];
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
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
            BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
            BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
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
        BQ_PREP_CHECK(restored && bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_OK &&
                      !strcmp(digest, original_digest));
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 && mkdirat(copied, "unlisted", 0500) == 0 &&
                      fchmod(copied, 0500) == 0);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_SOURCE_MISMATCH && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), original_digest, &imported) == BQ_SOURCE_MISMATCH &&
                      !imported.inventory_sha256[0]);
        BQ_PREP_CHECK(copied >= 0 && fchmod(copied, 0700) == 0 &&
                      unlinkat(copied, "unlisted", AT_REMOVEDIR) == 0 && fchmod(copied, 0500) == 0);
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
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
            BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                          string_from_pointer(profile), digest, NULL) == BQ_SOURCE_MISMATCH && !digest[0]);
            BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
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
        BQ_PREP_CHECK(bq_retirement_preparation_ready_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), digest, NULL) == BQ_CORRUPT && !digest[0]);
        BQ_PREP_CHECK(bq_retirement_preparation_import_pinned(store, &job, installed, workspaces,
                      string_from_pointer(profile), original_digest, &imported) == BQ_CORRUPT &&
                      !imported.inventory_sha256[0]);
    }
    if (root >= 0) close(root);
    bq_close(&queue);
    if (queue_path[0] && ok) bq_prep_test_cleanup(queue_path);
}

BUSTER_GLOBAL_LOCAL void bq_prep_test_flip_pin(char* profile, char const* key)
{
    char* pin = strstr(profile, key);
    BQ_PREP_CHECK(pin != NULL);
    if (pin)
    {
        pin += strlen(key);
        *pin = *pin == 'a' ? 'b' : 'a';
    }
}

/* #1020 installed reference policy on the real A fixture. The synthetic
 * profile adds reference-template/-inventory and census-row pins beside the
 * fixture's support, inventory and toolchain pins; the checked-in blocked
 * profile has none of them and must fail closed. */
BUSTER_GLOBAL_LOCAL void bq_prep_test_reference_policy(int installed, char const* installed_path,
    char const* workspaces_path, BqRetirementPreparation const* preparation, char const* profile)
{
    BqPrepReferenceFixture fixture = {0};
    bool ok = bq_prep_test_reference_install(installed, installed_path, preparation, profile, &fixture);
    BqRetirementToolchain checked = fixture.checked;
    char const* toolchain_root = fixture.toolchain_root;
    char const* template_path = fixture.template_path;
    char const* template_sha256 = fixture.template_sha256;
    char const* inventory_sha256 = fixture.inventory_sha256;
    char const* clang_sha256 = fixture.clang_sha256;
    char const* pinned = fixture.pinned;
    char held_sha256[SHA256_HEX_CAPACITY] = {0};
    int held = -1;
    BQ_PREP_CHECK(ok && bq_retirement_toolchain_hold_clang(&checked, &held, held_sha256) && held >= 3 &&
                  !strcmp(held_sha256, clang_sha256) && (fcntl(held, F_GETFD) & FD_CLOEXEC));
    if (held >= 0) close(held);

    BqRetirementReferencePolicy policy = {0};
    BQ_PREP_CHECK(ok && bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                  preparation, &checked, &policy) == BQ_OK && policy.owned && policy.clang >= 3 &&
                  !strcmp(policy.template_sha256, template_sha256) &&
                  !strcmp(policy.inventory_sha256, inventory_sha256) &&
                  !strcmp(policy.toolchain_manifest_sha256, checked.manifest_sha256) &&
                  policy.template.references == policy.template_rows &&
                  policy.plan.template == &policy.template && policy.plan.rows == policy.plan_rows &&
                  policy.plan.count == 1 && !strcmp(policy.plan_rows[0].source_path, "src/main.c") &&
                  !strcmp(policy.plan_rows[0].flags[0], "-std=c11") &&
                  !strcmp(policy.template_rows[0].build_command_sha256, fixture.build_command_sha256) &&
                  !strcmp(policy.source[1].commit, preparation->subjects[1].commit) &&
                  !strcmp(policy.plan.clang_sha256, clang_sha256));
    BqRetirementReferencePolicy live = policy;
    BQ_PREP_CHECK(bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                  preparation, &checked, &policy) == BQ_RECIPE_MISMATCH && policy.clang == live.clang &&
                  policy.inventory == live.inventory);
    BQ_PREP_CHECK(bq_retirement_reference_policy_release(&policy) && !policy.owned && policy.clang == -1 &&
                  policy.inventory == -1);

    /* The checked-in blocked profile has no reference pins. */
    char absent[SHA256_HEX_CAPACITY] = {0};
    String8 blocked = bq_recipe_profile(BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED);
    BQ_PREP_CHECK(!bq_retirement_profile_sha(blocked, S8("reference-template-sha256="), absent) &&
                  !bq_retirement_profile_sha(blocked, S8("reference-inventory-sha256="), absent));
    BQ_PREP_CHECK(bq_retirement_reference_policy_import(installed, preparation, &checked, &policy) ==
                  BQ_RECIPE_MISMATCH && !policy.owned && policy.clang == -1 && policy.inventory == -1);

    char variant[1024];
    char const* missing[] = {"reference-template-sha256=", "reference-inventory-sha256=",
                             "census-rows-sha256="};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(missing); index += 1)
    {
        memcpy(variant, pinned, sizeof(variant));
        char* line = strstr(variant, missing[index]);
        char* end = line ? strchr(line, '\n') : NULL;
        if (end) memmove(line, end + 1, strlen(end + 1) + 1);
        BQ_PREP_CHECK(end && bq_retirement_reference_policy_import_pinned(installed,
                      string_from_pointer(variant), preparation, &checked, &policy) == BQ_RECIPE_MISMATCH &&
                      !policy.owned);
    }
    char const* flipped[] = {"reference-template-sha256=", "reference-inventory-sha256=",
                             "support-declaration-sha256=", "census-rows-sha256=",
                             "toolchain-manifest-sha256="};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(flipped); index += 1)
    {
        memcpy(variant, pinned, sizeof(variant));
        bq_prep_test_flip_pin(variant, flipped[index]);
        BQ_PREP_CHECK(bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(variant),
                      preparation, &checked, &policy) == BQ_RECIPE_MISMATCH && !policy.owned);
    }
    for (u32 field = 0; field < 3; field += 1)
    {
        BqRetirementPreparation changed = *preparation;
        char* value = field == 0 ? changed.subjects[0].commit : field == 1 ? changed.subjects[1].tree :
                      changed.subjects[0].manifest_sha256;
        value[0] = value[0] == 'e' ? 'f' : 'e';
        BQ_PREP_CHECK(bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                      &changed, &checked, &policy) == BQ_SOURCE_MISMATCH && !policy.owned);
    }
    BqRetirementToolchain other = checked;
    other.manifest_sha256[0] = other.manifest_sha256[0] == 'a' ? 'b' : 'a';
    BQ_PREP_CHECK(bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                  preparation, &other, &policy) == BQ_CONFIGURATION_MISMATCH && !policy.owned);
    BQ_PREP_CHECK(chmod(template_path, 0600) == 0 &&
                  bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                      preparation, &checked, &policy) == BQ_CONFIGURATION_MISMATCH &&
                  chmod(template_path, 0400) == 0);

    /* A byte-equal clang inode replacement keeps every digest but breaks the
     * held bundle identity; restoring the original inode passes again. */
    char tool_bin[528], clang_path[544], saved[512];
    int bin_length = snprintf(tool_bin, sizeof(tool_bin), "%s/bin", toolchain_root);
    int clang_length = snprintf(clang_path, sizeof(clang_path), "%s/clang", tool_bin);
    int saved_length = snprintf(saved, sizeof(saved), "%s/reference-held-clang", workspaces_path);
    bool replaced = bin_length > 0 && (size_t)bin_length < sizeof(tool_bin) &&
                    clang_length > 0 && (size_t)clang_length < sizeof(clang_path) &&
                    saved_length > 0 && (size_t)saved_length < sizeof(saved) &&
                    chmod(tool_bin, 0700) == 0 && rename(clang_path, saved) == 0 &&
                    bq_prep_test_write(clang_path, "fixture only\n") && chmod(clang_path, 0555) == 0 &&
                    chmod(tool_bin, 0555) == 0;
    BQ_PREP_CHECK(replaced && !bq_retirement_toolchain_hold_clang(&checked, &held, held_sha256) && held == -1 &&
                  bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                      preparation, &checked, &policy) == BQ_CONFIGURATION_MISMATCH && !policy.owned);
    BQ_PREP_CHECK(chmod(tool_bin, 0700) == 0 && unlink(clang_path) == 0 && rename(saved, clang_path) == 0 &&
                  chmod(tool_bin, 0555) == 0 &&
                  bq_retirement_reference_policy_import_pinned(installed, string_from_pointer(pinned),
                      preparation, &checked, &policy) == BQ_OK &&
                  bq_retirement_reference_policy_release(&policy));
    BQ_PREP_CHECK(bq_prep_test_reference_remove(&fixture));
}

#include "retirement_campaign_service_tests.h"

int main(void)
{
    bq_prep_test_support_population();
    bq_prep_test_raw_census_boundary();
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
        bq_prep_test_reference_policy(input, installed, workspaces, &preparation, profile);
        BqRetirementSource verified_base = preparation.subjects[0];
        BQ_PREP_CHECK(preparation.subjects[0].entries == 1 && preparation.subjects[1].entries == 1 &&
                      preparation.source_reservation_bytes > BQ_RETIREMENT_COPY_OVERHEAD);
        struct statvfs filesystem = {0};
        u64 reservation = 0, block = 0;
        BQ_PREP_CHECK(fstatvfs(output, &filesystem) == 0);
        block = filesystem.f_frsize > filesystem.f_bsize ? filesystem.f_frsize : filesystem.f_bsize;
        if (block < 8192u) block = 8192u;
        BQ_PREP_CHECK(bq_retirement_source_reservation(&preparation, block, &reservation) &&
                      reservation == preparation.source_reservation_bytes);
        u64 payload = preparation.subjects[0].bytes + preparation.subjects[1].bytes +
                      preparation.subjects[0].manifest_bytes + preparation.subjects[1].manifest_bytes;
        u64 nodes = BQ_RETIREMENT_EXTRA_METADATA_NODES +
                    preparation.subjects[0].entries + preparation.subjects[1].entries +
                    preparation.subjects[0].directories + preparation.subjects[1].directories + 2u;
        BQ_PREP_CHECK(reservation == 2u * payload + 2u * nodes * block + BQ_RETIREMENT_COPY_OVERHEAD);
        u64 blocks = reservation / filesystem.f_frsize + (reservation % filesystem.f_frsize != 0);
        BQ_PREP_CHECK(blocks > 0 && bq_retirement_source_capacity(reservation, filesystem.f_frsize, blocks) &&
                      !bq_retirement_source_capacity(reservation, filesystem.f_frsize, blocks - 1u) &&
                      !bq_retirement_source_capacity(reservation, 0, UINT64_MAX));
        BQ_PREP_CHECK(!bq_retirement_source_reservation(&preparation, UINT64_MAX, &reservation));
        BqRetirementPreparation oversized = preparation;
        oversized.subjects[0].bytes = UINT64_MAX;
        BQ_PREP_CHECK(!bq_retirement_source_reservation(&oversized, block, &reservation));
        oversized = preparation;
        oversized.subjects[1].manifest_bytes = 0;
        BQ_PREP_CHECK(!bq_retirement_source_reservation(&oversized, block, &reservation));
        char same_tree_inventory[1024];
        int duplicate_length = snprintf(same_tree_inventory, sizeof(same_tree_inventory),
                          "BQ-RETIREMENT-INPUTS-V1\nrepository=buster14a/buster\n"
                          "support-sha256=%.64s\ncontract-sha256=%.64s\n"
                          "base=%s %s %s %u %" PRIu64 " %u %u %u\n"
                          "candidate=%s %s %s %u %" PRIu64 " %u %u %u\n",
                          "1111111111111111111111111111111111111111111111111111111111111111",
                          "2222222222222222222222222222222222222222222222222222222222222222",
                          subjects[0].commit, subjects[0].tree, subjects[0].manifest_sha256,
                          subjects[0].entries, (uint64_t)subjects[0].bytes, subjects[0].directories,
                          subjects[0].max_path, subjects[0].max_depth,
                          subjects[1].commit, subjects[0].tree, subjects[1].manifest_sha256,
                          subjects[1].entries, (uint64_t)subjects[1].bytes, subjects[1].directories,
                          subjects[1].max_path, subjects[1].max_depth);
        BqRetirementPreparation refused = {0};
        BQ_PREP_CHECK(duplicate_length > 0 && (u32)duplicate_length < sizeof(same_tree_inventory) &&
                      !bq_retirement_inventory(string_from_pointer(same_tree_inventory), pinned,
                                               &request, &refused));
        BQ_PREP_CHECK(bq_retirement_campaign_service_test(input, output, &preparation, profile));
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
