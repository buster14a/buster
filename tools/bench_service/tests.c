/* Native regression executable, following tools/throughput/tests.c.
 * No network, benchmark samples or alternative model. Deadline coverage uses
 * only bounded local helper processes and a monotonic timer.
 * Fixture byte edits model durable crash prefixes; they do not simulate a disk
 * cache losing power. Every recovery verdict comes from bq_open/bq_replay.
 */
#define BUSTER_BENCH_SERVICE_TEST 1
#define main bench_service_cli_main
#include "main.c"
#undef main
#include <stdlib.h>
#include <stddef.h>
#ifdef __linux__
#include <sys/time.h>
#include <sys/wait.h>
#include "sgid_sandbox_test.h"
#endif

BUSTER_GLOBAL_LOCAL u32 bq_test_assertions;
BUSTER_GLOBAL_LOCAL u32 bq_test_failures;
#ifdef __linux__
BUSTER_GLOBAL_LOCAL char bq_test_executable[BQ_PATH_CAP + 1];
#endif
#define BQ_CHECK(expression) do { bq_test_assertions += 1; if (!(expression)) { bq_test_failures += 1; fprintf(stderr, "QUEUE_TEST failure line=%d: %s\n", __LINE__, #expression); } } while (0)

BUSTER_GLOBAL_LOCAL bool bq_test_file_sha256(char const* path, char output[SHA256_HEX_CAPACITY])
{
    FILE* file = path ? fopen(path, "rb") : NULL;
    Sha256 digest;
    bool ok = file != NULL;
    if (ok) sha256_init(&digest);
    u8 bytes[4096];
    while (ok)
    {
        size_t count = fread(bytes, 1, sizeof(bytes), file);
        if (count) sha256_add(&digest, bytes, count);
        if (count != sizeof(bytes))
        {
            ok = !ferror(file);
            break;
        }
    }
    if (ok) sha256_finish_hex(&digest, (char8*)output);
    if (file && fclose(file) != 0) ok = false;
    if (!ok) output[0] = 0;
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_test_profile_sha256(char const* profile, char const* key,
                                                 char output[SHA256_HEX_CAPACITY])
{
    char const* value = profile && key ? strstr(profile, key) : NULL;
    u32 key_length = key ? (u32)strlen(key) : 0;
    bool ok = value && (value == profile || value[-1] == '\n');
    if (ok) value += key_length;
    for (u32 index = 0; ok && index < SHA256_HEX_CAPACITY - 1; index += 1)
        ok = (value[index] >= '0' && value[index] <= '9') || (value[index] >= 'a' && value[index] <= 'f');
    if (ok) ok = value[SHA256_HEX_CAPACITY - 1] == '\n';
    if (ok)
    {
        memcpy(output, value, SHA256_HEX_CAPACITY - 1);
        output[SHA256_HEX_CAPACITY - 1] = 0;
    }
    else
    {
        output[0] = 0;
    }
    return ok;
}

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

BUSTER_GLOBAL_LOCAL void bq_test_typed_client(void)
{
    char* gateway[] = {"submit", "run-123-1", "1111111111111111111111111111111111111111",
                       "2222222222222222222222222222222222222222"};
    char* client[] = {"submit", "github-actions", gateway[1], "validate-buster-v1", gateway[2], gateway[3]};
    BqPacket fixed, typed;
    u32 operation = 0;
    BQ_CHECK(bq_client_arguments(4, gateway, true, &fixed, &operation) && operation == BQ_OP_SUBMIT);
    BQ_CHECK(bq_client_arguments(6, client, false, &typed, &operation) &&
             fixed.size == typed.size && !memcmp(fixed.bytes, typed.bytes, fixed.size));
    client[3] = "native-retirement-performance-v1";
    BQ_CHECK(!bq_client_arguments(6, client, false, &typed, &operation) && !typed.size);
    client[3] = "fake-success-v1";
    BQ_CHECK(!bq_client_arguments(6, client, false, &typed, &operation));
    char const* invalid_ids[] = {"main", "HEAD", "1111111", "../installed", "--help", "$(id)",
                                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
    for (u32 i = 0; i < sizeof(invalid_ids) / sizeof(invalid_ids[0]); i += 1)
    {
        gateway[2] = (char*)invalid_ids[i];
        BQ_CHECK(!bq_client_arguments(4, gateway, true, &typed, &operation) && !typed.size);
    }
    gateway[2] = client[4];
    gateway[1] = "not a key";
    BQ_CHECK(!bq_client_arguments(4, gateway, true, &typed, &operation));
    gateway[1] = "run-123-1";
    BQ_CHECK(!bq_client_arguments(6, client, true, &typed, &operation));
    char* private_operation[] = {"worker-run"};
    BQ_CHECK(!bq_client_arguments(1, private_operation, true, &typed, &operation));
    char* status[] = {"result", "7"};
    BQ_CHECK(bq_client_arguments(2, status, true, &typed, &operation) && operation == BQ_OP_RESULT &&
             bq_u64(typed.bytes + BQ_CONTROL_HEADER) == 7);
    status[1] = "0";
    BQ_CHECK(!bq_client_arguments(2, status, true, &fixed, &operation));
    status[1] = "18446744073709551616";
    BQ_CHECK(!bq_client_arguments(2, status, true, &fixed, &operation));

    u8 body[BQ_CONTROL_BODY] = {0};
    bq_put64(body + 4, 7);
    bq_put64(body + 12, 2);
    bq_put64(body + 20, 12);
    bq_put32(body + 28, BQ_FINISHED);
    bq_put32(body + 32, BQ_INTERRUPTED);
    bq_put32(body + 52, 1);
    memset(body + 56, 'a', 64);
    char const* root = "/var/lib/buster-bench/workspaces/result-7-2";
    bq_put32(body + 124, (u32)strlen(root));
    memcpy(body + 128, root, strlen(root));
    memset(body + 320, 'b', 64);
    memset(body + 384, 'c', 64);
    memset(body + 448, 'd', 64);
    bq_packet(&fixed, BQ_OP_RESULT | 0x80000000u, 1, body, sizeof(body));
    BQ_CHECK(bq_public_response_valid(&typed, &fixed));
    for (u32 length = 0; length < BQ_CONTROL_BODY; length += 1)
    {
        BqPacket truncated;
        bq_packet(&truncated, BQ_OP_RESULT | 0x80000000u, 1, body, length);
        BQ_CHECK(bq_public_response_valid(&typed, &truncated) == (length == 124));
    }
    u32 invalid_offsets[] = {4, 28, 32, 36, 40, 44, 48, 52, 56, 120, 124, 128, 319, 320, 384, 448};
    for (u32 i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); i += 1)
    {
        BqPacket invalid = fixed;
        invalid.bytes[BQ_CONTROL_HEADER + invalid_offsets[i]] = 0xff;
        BQ_CHECK(!bq_public_response_valid(&typed, &invalid));
    }
    BqPacket traversal = fixed;
    memcpy(traversal.bytes + BQ_CONTROL_HEADER + 128, "/../", 4);
    BQ_CHECK(!bq_public_response_valid(&typed, &traversal));
    FILE* output = tmpfile();
    BQ_CHECK(output != NULL);
    if (output)
    {
        BQ_CHECK(bq_response_write(BQ_OP_RESULT, &fixed, output) && fflush(output) == 0);
        rewind(output);
        char receipt[1024] = {0};
        size_t count = fread(receipt, 1, sizeof(receipt) - 1, output);
        BQ_CHECK(count > 0 && !ferror(output) && strstr(receipt, "job=7 token=2 sequence=12") &&
                 strstr(receipt, "outcome=interrupted validity=not-evaluated") &&
                 strstr(receipt, "result-bound=1 statistical-decision=not-evaluated\n") &&
                 strstr(receipt, root));
        char digest_line[96];
        char const* names[] = {"manifest", "bundle", "full-result"};
        for (u32 i = 0; i < 3; i += 1)
        {
            snprintf(digest_line, sizeof(digest_line), "%s-sha256=%.64s\n", names[i], (char const*)body + 320 + i * 64);
            BQ_CHECK(strstr(receipt, digest_line) != NULL);
        }
        fclose(output);
    }
    char* logs[] = {"logs", "7", "10"};
    BQ_CHECK(bq_client_arguments(3, logs, true, &typed, &operation) && operation == BQ_OP_LOGS);
    memset(body, 0, sizeof(body));
    bq_put32(body + 4, 1);
    bq_put64(body + 8, 11);
    bq_put64(body + 20, 11);
    bq_put64(body + 28, 7);
    bq_put32(body + 36, BQ_ADVANCE);
    bq_packet(&fixed, BQ_OP_LOGS | 0x80000000u, 1, body, 52);
    BQ_CHECK(bq_public_response_valid(&typed, &fixed));
    u32 log_offsets[] = {4, 8, 16, 28, 36, 40, 44, 48};
    for (u32 i = 0; i < sizeof(log_offsets) / sizeof(log_offsets[0]); i += 1)
    {
        BqPacket invalid = fixed;
        invalid.bytes[BQ_CONTROL_HEADER + log_offsets[i]] = 0xff;
        BQ_CHECK(!bq_public_response_valid(&typed, &invalid));
    }
    bq_put64(fixed.bytes + BQ_CONTROL_HEADER + 20, 10);
    BQ_CHECK(!bq_public_response_valid(&typed, &fixed));
    memset(body, 0, sizeof(body));
    bq_put64(body + 8, 10);
    bq_packet(&fixed, BQ_OP_LOGS | 0x80000000u, 1, body, 20);
    BQ_CHECK(bq_public_response_valid(&typed, &fixed));
    char* capabilities[] = {"capabilities"};
    BQ_CHECK(bq_client_arguments(1, capabilities, true, &typed, &operation));
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BQ_CHECK(bq_dispatch(&queue, typed.bytes, typed.size, &fixed) == BQ_OK &&
             bq_public_response_valid(&typed, &fixed));
    fixed.bytes[BQ_CONTROL_HEADER + 4] = 0x1b;
    BQ_CHECK(!bq_public_response_valid(&typed, &fixed));
    bq_packet(&fixed, BQ_OP_CAPABILITIES | 0x80000000u, 1, body, 4);
    BQ_CHECK(!bq_public_response_valid(&typed, &fixed));
}

BUSTER_GLOBAL_LOCAL void bq_test_codec(void)
{
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    BqPacket request, response;
    BQ_CHECK(sizeof(bq_capabilities_v2) - 1 <= BQ_CONTROL_BODY - 4);
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
    fields[2] = S8("native-retirement-performance-v1");
    BQ_CHECK(bq_recipe_from_name(fields[2]) == BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED &&
             bq_recipe_blocked(bq_recipe_from_name(fields[2])) &&
             !bq_recipe_admitted(bq_recipe_from_name(fields[2])) &&
             bq_request_make(fields, &malformed) == BQ_BAD_REQUEST);
    fields[2] = S8("fake-success-v1");
    fields[0] = (String8){0};
    BQ_CHECK(bq_request_make(fields, &malformed) == BQ_BAD_REQUEST);
    u64 value = 0;
    BQ_CHECK(bq_decimal("18446744073709551615", true, &value) && value == UINT64_MAX);
    BQ_CHECK(!bq_decimal("18446744073709551616", true, &value));
    BQ_CHECK(!bq_decimal("1x", true, &value));
    BQ_CHECK(!bq_decimal("-1", true, &value));
    BQ_CHECK(!bq_decimal("0", true, &value));
    BQ_CHECK(bq_decimal("0", false, &value) && value == 0);
    FILE* attributes_file = fopen(".gitattributes", "rb");
    char attributes[4096];
    u32 attributes_size = attributes_file ? (u32)fread(attributes, 1, sizeof(attributes), attributes_file) : 0;
    int attributes_extra = attributes_file ? fgetc(attributes_file) : 0;
    bool attributes_complete = attributes_file && attributes_size < sizeof(attributes) &&
                               attributes_extra == EOF && !ferror(attributes_file);
    if (attributes_file)
    {
        fclose(attributes_file);
    }
    char const* rules[] = {
        "tools/bench_service/profiles/validate-buster-v1.recipe text eol=lf",
        "tools/bench_service/profiles/native-retirement-performance-v1.blocked text eol=lf",
    };
    bool rules_found[BUSTER_ARRAY_LENGTH(rules)] = {0};
    for (u32 start = 0; attributes_complete && start < attributes_size;)
    {
        u32 end = start;
        while (end < attributes_size && attributes[end] != '\n')
        {
            end += 1;
        }
        u32 content_end = end > start && attributes[end - 1] == '\r' ? end - 1 : end;
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rules); index += 1)
        {
            u32 rule_length = (u32)strlen(rules[index]);
            if (content_end - start == rule_length && !memcmp(attributes + start, rules[index], rule_length))
                rules_found[index] = true;
        }
        start = end < attributes_size ? end + 1 : end;
    }
    BQ_CHECK(attributes_complete && rules_found[0] && rules_found[1]);
    BqRecipe recipes[] = {BQ_RECIPE_VALIDATE_BUSTER, BQ_RECIPE_NATIVE_RETIREMENT_BLOCKED};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(recipes); index += 1)
    {
        BqRecipeFiles files;
        String8 expected = bq_recipe_profile(recipes[index]);
        bool described = bq_recipe_files(recipes[index], &files);
        char path[BQ_PATH_CAP + 1];
        int path_length = described ? snprintf(path, sizeof(path), "tools/bench_service/profiles/%s", files.profile) : -1;
        FILE* profile = path_length > 0 && (u32)path_length < sizeof(path) ? fopen(path, "rb") : NULL;
        u8* bytes = expected.length ? malloc((size_t)expected.length + 1) : NULL;
        size_t count = profile && bytes ? fread(bytes, 1, (size_t)expected.length + 1, profile) : 0;
        BQ_CHECK(described && expected.length > 0 && profile && bytes && count == expected.length &&
                 !memcmp(bytes, expected.pointer, expected.length) &&
                 (recipes[index] == BQ_RECIPE_VALIDATE_BUSTER ? !strcmp(files.command, "bench_service_recipe") :
                                                               !files.command[0]));
        free(bytes);
        if (profile) fclose(profile);
    }
    struct BqPinnedFile
    {
        char const* key;
        char const* path;
    } pins[] = {
        {"contract-sha256=", "docs/native-retirement-performance-contract.md"},
        {"support-declaration-sha256=", "docs/native-retirement-support-v1.tsv"},
        {"binding-validator-sha256=", "tools/native_retirement_performance_binding.py"},
        {"binding-schema-sha256=", "tools/native_retirement_performance_schema.py"},
        {"statistics-sha256=", "tools/throughput/retirement_stats.h"},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(pins); index += 1)
    {
        char expected[SHA256_HEX_CAPACITY], actual[SHA256_HEX_CAPACITY];
        BQ_CHECK(bq_test_profile_sha256(bq_native_retirement_blocked_profile, pins[index].key, expected) &&
                 bq_test_file_sha256(pins[index].path, actual) &&
                 !memcmp(expected, actual, SHA256_HEX_CAPACITY));
    }
#ifdef _WIN32
    BQ_CHECK(bq_open(&queue, ".") == BQ_UNSUPPORTED);
#endif
#ifndef __linux__
    BqWorkerConfig worker = {0};
    u64 worker_id = UINT64_MAX;
    BQ_CHECK(bq_worker_run(&queue, &worker, &worker_id) == BQ_UNSUPPORTED && worker_id == 0);
    BQ_CHECK(bq_worker_unit(S8("/unsupported"), S8("1"), S8("2"), S8("validate-buster-v1"), S8("/workspace"),
                             S8("1111111111111111111111111111111111111111"),
                             S8("2222222222222222222222222222222222222222"), S8("/workspace/result")) == BQ_UNSUPPORTED);
#else
    char boot_id[BQ_WORKER_BOOT_CAP];
    BQ_CHECK(bq_worker_read_regular("/proc/sys/kernel/random/boot_id", boot_id, sizeof(boot_id)) &&
             bq_worker_boot_valid(boot_id));
    BQ_CHECK(bq_worker_unit(S8("/unsupported"), S8("1"), S8("2"),
                             S8("native-retirement-performance-v1"), S8("/workspace"),
                             S8("1111111111111111111111111111111111111111"),
                             S8("2222222222222222222222222222222222222222"),
                             S8("/workspace/result")) == BQ_BAD_REQUEST);
    char field[4] = {0};
    BQ_CHECK(bq_worker_copy_field(field, sizeof(field), "abc") == 3 && !strcmp(field, "abc"));
    BQ_CHECK(bq_worker_copy_field(field, sizeof(field), "abcd") == -1 && !field[0]);
#endif
}

#ifndef _WIN32
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

BUSTER_GLOBAL_LOCAL bool bq_test_mkdtemp_physical(char* path, u32 capacity)
{
    bool created = mkdtemp(path) != NULL;
    char* physical = created ? realpath(path, NULL) : NULL;
    bool ok = physical != NULL;
    if (ok)
    {
        u64 size = (u64)strlen(physical);
        ok = size < capacity;
        if (ok)
        {
            memcpy(path, physical, (size_t)size + 1);
        }
    }
    if (created && !ok)
    {
        BQ_CHECK(rmdir(path) == 0);
        path[0] = 0;
    }
    free(physical);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_physical_temp_paths(void)
{
    char root[BQ_PATH_CAP + 1] = "/tmp/buster-physical-root-XXXXXX";
    bool root_ok = bq_test_mkdtemp_physical(root, sizeof(root));
    BQ_CHECK(root_ok);
    if (root_ok)
    {
        char alias[BQ_PATH_CAP + 1] = "/tmp/buster-physical-alias-XXXXXX";
        bool alias_ok = mkdtemp(alias) != NULL && rmdir(alias) == 0 && symlink(root, alias) == 0;
        BQ_CHECK(alias_ok);
        if (alias_ok)
        {
            char child[BQ_PATH_CAP + 1];
            int length = snprintf(child, sizeof(child), "%s/child-XXXXXX", alias);
            bool child_ok = length > 0 && (u32)length < sizeof(child) &&
                            bq_test_mkdtemp_physical(child, sizeof(child));
            BQ_CHECK(child_ok);
            if (child_ok)
            {
                char through_alias[BQ_PATH_CAP + 1];
                char const* leaf = strrchr(child, '/');
                length = snprintf(through_alias, sizeof(through_alias), "%s/%s", alias, leaf ? leaf + 1 : "");
                BQ_CHECK(leaf && length > 0 && (u32)length < sizeof(through_alias));
                int rejected = leaf && length > 0 && (u32)length < sizeof(through_alias) ?
                               bq_open_absolute_directory(string_from_pointer(through_alias)) : -1;
                BQ_CHECK(rejected < 0);
                if (rejected >= 0)
                {
                    close(rejected);
                }
                int physical = bq_open_absolute_directory(string_from_pointer(child));
                BQ_CHECK(physical >= 0);
                if (physical >= 0)
                {
                    close(physical);
                }
                BQ_CHECK(rmdir(child) == 0);
            }
            BQ_CHECK(unlink(alias) == 0);
        }
        char bounded[BQ_PATH_CAP + 1];
        int length = snprintf(bounded, sizeof(bounded), "%s/bounded-XXXXXX", root);
        BQ_CHECK(length > 0 && (u32)length < sizeof(bounded));
        if (length > 0 && (u32)length < sizeof(bounded))
        {
            BQ_CHECK(!bq_test_mkdtemp_physical(bounded, (u32)strlen(root) + 1) && !bounded[0]);
        }
        BQ_CHECK(rmdir(root) == 0);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_workspace_root_group_policy(void)
{
    char root[BQ_PATH_CAP + 1] = "/tmp/buster-workspace-group-XXXXXX";
    bool root_ok = bq_test_mkdtemp_physical(root, sizeof(root));
#ifdef __APPLE__
    /* /tmp is owned by wheel on Darwin and new directories inherit that
     * group. Model the provisioned service root by assigning the service's
     * effective group before validating the production policy. */
    root_ok = root_ok && chown(root, (uid_t)-1, getegid()) == 0;
#endif
    int directory = root_ok && chmod(root, 0710) == 0 ?
                    open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    BQ_CHECK(directory >= 0);
    if (directory >= 0)
    {
#ifdef __APPLE__
        BQ_CHECK(bq_workspace_root_directory(directory));
#else
        BQ_CHECK(!bq_workspace_root_directory(directory));
#endif
        close(directory);
    }
    if (root_ok) BQ_CHECK(rmdir(root) == 0);
}

typedef struct BqFixture
{
    char path[80];
    BqQueue queue;
} BqFixture;

BUSTER_GLOBAL_LOCAL bool bq_test_begin(BqFixture* fixture)
{
    *fixture = (BqFixture){.queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1}};
    snprintf(fixture->path, sizeof(fixture->path), "/tmp/buster-queue-XXXXXX");
    bool ok = bq_test_mkdtemp_physical(fixture->path, sizeof(fixture->path));
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
                !strncmp(entry->d_name, "cleanup-", 8) || !strncmp(entry->d_name, "worker-", 7))
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
    if (ok && defect == 9)
    {
        char manifest_text[8192];
        int header = snprintf(manifest_text, sizeof(manifest_text),
                              "BQ-SOURCE-V1\nrepository=buster14a/buster\nrevision=%s\n", revision);
        ok = header > 0 && (u32)header < sizeof(manifest_text);
        u64 used = ok ? (u64)header : 0;
        for (u32 index = 0; ok && index < 80; index += 1)
        {
            char name[64], body[128], file_path[4096];
            int name_length = snprintf(name, sizeof(name), "file-%03u.c", index);
            int body_length = snprintf(body, sizeof(body), "int bq_fixture_%03u(void){return %u;}\n", index, index);
            int path_length = snprintf(file_path, sizeof(file_path), "%s/%s", source, name);
            char8 file_digest[SHA256_HEX_CAPACITY];
            ok = name_length > 0 && body_length > 0 && path_length > 0 && (u32)path_length < sizeof(file_path) &&
                 bq_test_write_path(file_path, body, 0400);
            if (ok)
            {
                bq_digest(body, (u32)body_length, file_digest);
                int line = snprintf(manifest_text + used, sizeof(manifest_text) - used,
                                    "%.64s src/%s\n", file_digest, name);
                ok = line > 0 && (u64)line < sizeof(manifest_text) - used;
                if (ok) used += (u64)line;
            }
        }
        ok = ok && used > 4096 && bq_test_write_path(manifest, manifest_text, 0400) &&
             chmod(source, 0500) == 0 && chmod(root, 0500) == 0;
    }
    if (ok && defect != 9)
    {
        file_ok = defect == 8 ? mkfifo(file, 0400) == 0 : bq_test_write_path(file, contents, 0400);
        ok = file_ok;
    }
    char8 digest[SHA256_HEX_CAPACITY];
    if (ok && defect != 9)
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
    bool ok = bq_test_begin(&fixture->queue) &&
              bq_test_mkdtemp_physical(fixture->installed, sizeof(fixture->installed)) &&
              bq_test_mkdtemp_physical(fixture->workspaces, sizeof(fixture->workspaces));
#ifdef __APPLE__
    /* Production provisioning pins this root to the service group. Darwin's
     * /tmp group inheritance does not, so make the fixture faithful first. */
    ok = ok && chown(fixture->workspaces, (uid_t)-1, getegid()) == 0;
#endif
    ok = ok && chmod(fixture->workspaces, 02710) == 0;
    if (ok)
    {
        char recipes[512], recipe[1024];
        snprintf(recipes, sizeof(recipes), "%s/recipes", fixture->installed);
        snprintf(recipe, sizeof(recipe), "%s/validate-buster-v1.recipe", recipes);
        ok = mkdir(recipes, 0700) == 0 &&
             (defect == 6 ? mkfifo(recipe, 0400) == 0 :
              bq_test_write_path(recipe, defect == 4 ? "bad-recipe\n" : bq_validate_buster_profile, 0400)) &&
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

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
BUSTER_GLOBAL_LOCAL void bq_test_sgid_sandbox_child(void)
{
    char root[] = "/tmp/buster-sgid-filter-XXXXXX";
    bool ok = mkdtemp(root) != NULL;
    gid_t inherited_group = getegid();
    bool distinct = false;
    if (ok) ok = bq_test_sgid_fixture_group(root, &inherited_group, &distinct);
    BQ_CHECK(ok);
    BqMaterialFixture* fixtures = calloc(3, sizeof(*fixtures));
    bool started[3] = {0};
    BQ_CHECK(fixtures != NULL);
    for (u32 index = 0; ok && fixtures && index < 3; index += 1)
    {
        started[index] = bq_material_test_begin(fixtures + index, 0);
        ok = started[index] && (!distinct || chown(fixtures[index].workspaces, (uid_t)-1, inherited_group) == 0) &&
             chmod(fixtures[index].workspaces, 02710) == 0;
        BQ_CHECK(ok);
    }
    char collision[512] = {0};
    if (ok)
    {
        int length = snprintf(collision, sizeof(collision), "%s/job-1-attempt-2", fixtures[1].workspaces);
        ok = length > 0 && (size_t)length < sizeof(collision) && mkdir(collision, 0700) == 0;
        BQ_CHECK(ok);
    }
    int parent = ok ? open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    BQ_CHECK(parent >= 0);
    if (ok) ok = bq_test_install_sgid_restriction();
    BQ_CHECK(ok);
    if (ok)
    {
        mode_t modes[] = {02710, 02750, 02700, 02770};
        mode_t masks[] = {0077, 0022, 0000, 0777};
        bool created = false;
        int controlled = bq_create_inherited_group_directory(parent, "controlled", 02770, &created);
        BQ_CHECK(controlled >= 0 && created && bq_test_sgid_controls(parent, "controlled"));
        if (controlled >= 0) close(controlled);
        for (u32 m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
        {
            for (u32 u = 0; u < BUSTER_ARRAY_LENGTH(masks); u += 1)
            {
                char name[32];
                snprintf(name, sizeof(name), "mode-%u-umask-%u", m, u);
                mode_t old = umask(masks[u]);
                created = false;
                int child = bq_create_inherited_group_directory(parent, name, modes[m], &created);
                mode_t observed = umask(masks[u]);
                umask(old);
                struct stat info = {0};
                BQ_CHECK(child >= 0 && created && observed == masks[u] && fstat(child, &info) == 0 &&
                         info.st_uid == geteuid() && info.st_gid == inherited_group &&
                         (info.st_mode & 07777) == modes[m]);
                if (child >= 0) close(child);
                created = true;
                old = umask(masks[u]);
                child = bq_create_inherited_group_directory(parent, name, modes[m], &created);
                observed = umask(masks[u]);
                umask(old);
                BQ_CHECK(child < 0 && !created && observed == masks[u] && errno == EEXIST);
                if (child >= 0) close(child);
                BQ_CHECK(unlinkat(parent, name, AT_REMOVEDIR) == 0);
            }
        }
        created = true;
        mode_t old = umask(0022);
        int invalid = bq_create_inherited_group_directory(-1, "invalid", 02770, &created);
        mode_t observed = umask(old);
        BQ_CHECK(invalid < 0 && !created && observed == 0022);
        if (invalid >= 0) close(invalid);
        int missing = openat(parent, "missing", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        BQ_CHECK(missing < 0 && errno == ENOENT);
        BQ_CHECK(mkdirat(parent, "no-sgid", 0700) == 0);
        int no_sgid = openat(parent, "no-sgid", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        BQ_CHECK(no_sgid >= 0 && fchmod(no_sgid, 0700) == 0);
        created = true;
        old = umask(0077);
        int refused = bq_create_inherited_group_directory(no_sgid, "child", 02770, &created);
        observed = umask(old);
        BQ_CHECK(refused < 0 && !created && observed == 0077);
        if (refused >= 0) close(refused);
        if (no_sgid >= 0) close(no_sgid);
        BQ_CHECK(unlinkat(parent, "no-sgid", AT_REMOVEDIR) == 0);
        bq_test_unverify_next_create = true;
        created = false;
        old = umask(0777);
        refused = bq_create_inherited_group_directory(parent, "unverified", 02770, &created);
        observed = umask(old);
        struct stat unverified = {0};
        BQ_CHECK(refused < 0 && created && !bq_test_unverify_next_create && observed == 0777 &&
                 fstatat(parent, "unverified", &unverified, AT_SYMLINK_NOFOLLOW) == 0 &&
                 (unverified.st_mode & 07777) == 0700);
        if (refused >= 0) close(refused);
        BQ_CHECK(unlinkat(parent, "unverified", AT_REMOVEDIR) == 0);
        BQ_CHECK(unlinkat(parent, "controlled", AT_REMOVEDIR) == 0);

        for (u32 index = 0; index < 3; index += 1)
        {
            BqRequest request = bq_test_real_request(1);
            u64 id = 0, token = 0;
            BQ_CHECK(bq_submit(&fixtures[index].queue.queue, &request, &id) == BQ_OK);
            if (index == 2) bq_test_unverify_next_create = true;
            BqError result = bq_materialize(&fixtures[index].queue.queue,
                                            string_from_pointer(fixtures[index].installed),
                                            string_from_pointer(fixtures[index].workspaces), &id, &token);
            BqJob* job = bq_job(&fixtures[index].queue.queue.state, id);
            if (index == 0)
            {
                BQ_CHECK(result == BQ_OK && job && job->phase == BQ_PREPARING &&
                         !fixtures[index].queue.queue.needs_reconciliation);
            }
            else
            {
                BQ_CHECK(result == BQ_WORKSPACE_MISMATCH && job && job->phase == BQ_RESERVED &&
                         fixtures[index].queue.queue.needs_reconciliation &&
                         bq_failure_evidence(&fixtures[index].queue.queue, job) == BQ_WORKSPACE_MISMATCH);
                char name[64], path[512];
                BQ_CHECK(bq_workspace_name(name, id, token));
                int length = snprintf(path, sizeof(path), "%s/%s", fixtures[index].workspaces, name);
                BQ_CHECK(length > 0 && (size_t)length < sizeof(path) && access(path, F_OK) == 0);
                if (index == 2)
                {
                    BQ_CHECK(!bq_test_unverify_next_create &&
                             bq_workspace_reconcile(&fixtures[index].queue.queue,
                                                    string_from_pointer(fixtures[index].workspaces), id, token) != BQ_OK &&
                             fixtures[index].queue.queue.needs_reconciliation && access(path, F_OK) == 0);
                }
            }
        }
    }
    if (parent >= 0) close(parent);
    for (u32 index = 0; fixtures && index < 3; index += 1)
        if (started[index]) bq_material_test_end(fixtures + index);
    free(fixtures);
    if (root[0]) BQ_CHECK(rmdir(root) == 0);
    char const* required = getenv("BQ_REQUIRE_DISTINCT_GROUP");
    BQ_CHECK(!required || strcmp(required, "1") || distinct);
    printf("SGID_SANDBOX_TEST service assertions=%u failures=%u group=%s arch=%s\n",
           bq_test_assertions, bq_test_failures, distinct ? "different-primary" : "unsupported-different-primary",
#if defined(__x86_64__)
           "x86_64");
#else
           "aarch64");
#endif
    fflush(stdout);
    _exit(bq_test_failures ? 1 : 0);
}

BUSTER_GLOBAL_LOCAL void bq_test_sgid_sandbox(void)
{
    pid_t child = fork();
    BQ_CHECK(child >= 0);
    if (child == 0) bq_test_sgid_sandbox_child();
    if (child > 0)
    {
        int status = 0;
        pid_t waited = waitpid(child, &status, 0);
        BQ_CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}
#endif

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
        mode_t prior_umask = umask(0077);
        BqError materialized = bq_dispatch(&fixture.queue.queue, packet.bytes, packet.size, &response);
        mode_t remaining_umask = umask(prior_umask);
        BQ_CHECK(materialized == BQ_OK && remaining_umask == 0077);
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
        struct stat base, candidate, workspace_root_info, workspace_info;
        BQ_CHECK(stat(fixture.workspaces, &workspace_root_info) == 0 && stat(workspace, &workspace_info) == 0 &&
                 workspace_info.st_gid == workspace_root_info.st_gid);
        BQ_CHECK(stat(base_source, &base) == 0 && stat(candidate_source, &candidate) == 0 && base.st_ino != candidate.st_ino);
        BQ_CHECK((base.st_mode & 07777) == 0440 && (candidate.st_mode & 07777) == 0440);
        struct stat attempt_info = {0}, base_subject = {0}, candidate_subject = {0};
        char base_subject_path[1024], candidate_subject_path[1024], base_source_dir[1024], candidate_source_dir[1024];
        snprintf(base_subject_path, sizeof(base_subject_path), "%s/base", workspace);
        snprintf(candidate_subject_path, sizeof(candidate_subject_path), "%s/candidate", workspace);
        snprintf(base_source_dir, sizeof(base_source_dir), "%s/base/source", workspace);
        snprintf(candidate_source_dir, sizeof(candidate_source_dir), "%s/candidate/source", workspace);
        BQ_CHECK(stat(workspace, &attempt_info) == 0 && stat(base_subject_path, &base_subject) == 0 &&
                 stat(candidate_subject_path, &candidate_subject) == 0 &&
                 (attempt_info.st_mode & 07777) == 02710 && attempt_info.st_gid == workspace_root_info.st_gid &&
                 (base_subject.st_mode & 07777) == 02710 && base_subject.st_gid == workspace_root_info.st_gid &&
                 (candidate_subject.st_mode & 07777) == 02710 &&
                 candidate_subject.st_gid == workspace_root_info.st_gid);
        struct stat base_source_info = {0}, candidate_source_info = {0};
        BQ_CHECK(stat(base_source_dir, &base_source_info) == 0 && stat(candidate_source_dir, &candidate_source_info) == 0 &&
                 (base_source_info.st_mode & 0222) == 0 && (candidate_source_info.st_mode & 0222) == 0 &&
                 (base_source_info.st_mode & 0050) == 0050 && (candidate_source_info.st_mode & 0050) == 0050);
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
        BQ_CHECK(stat(copied_manifest, &base) == 0 && (base.st_mode & 07777) == 0440);
        char identity[1024];
        snprintf(identity, sizeof(identity), "%s/.identity", workspace);
        BQ_CHECK(stat(identity, &base) == 0 && (base.st_mode & 0222) == 0);
        char base_build[1024], candidate_build[1024], sentinel[2048];
        snprintf(base_build, sizeof(base_build), "%s/base/build", workspace);
        snprintf(candidate_build, sizeof(candidate_build), "%s/candidate/build", workspace);
        BQ_CHECK(stat(base_build, &base) == 0 && stat(candidate_build, &candidate) == 0 && base.st_ino != candidate.st_ino);
        BQ_CHECK((base.st_mode & 07777) == 02700 && (candidate.st_mode & 07777) == 02700 &&
                 base.st_gid == workspace_root_info.st_gid && candidate.st_gid == workspace_root_info.st_gid);
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

BUSTER_GLOBAL_LOCAL bool bq_test_old_replay(u8 const* image, u32 size, u32 maximum_schema)
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
            u32 schema = bq_u32(frame + 8);
            ok = !memcmp(frame, "BQJNL001", 8) && schema >= BQ_SCHEMA_LEGACY && schema <= maximum_schema &&
                 length <= BQ_REQUEST_CAP && size - offset - BQ_HEADER_SIZE >= length &&
                 !memcmp(frame + 32, digest, 64);
        }
        if (ok)
        {
            bq_digest(frame + BQ_HEADER_SIZE, length, digest);
            ok = !memcmp(frame + 96, digest, 64) &&
                 bq_apply(&state, bq_u32(frame + 8), (BqRecordKind)bq_u32(frame + 12), bq_u64(frame + 24),
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
        BQ_CHECK(bq_test_old_replay(image, legacy_size, BQ_SCHEMA_LEGACY));
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
        BQ_CHECK(!bq_test_old_replay(image, mixed_size, BQ_SCHEMA_LEGACY));
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
        BQ_CHECK(!bq_test_old_replay(image, real_size, BQ_SCHEMA_LEGACY));
        bq_test_image(&fixture, image, real_size);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_CORRUPT);
        bq_test_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_supervisor_schema_migration(void)
{
    BqFixture fixture;
    if (bq_test_begin(&fixture))
    {
        BqRequest real = bq_test_real_request(9);
        u8 image[BQ_RECORD_CAP * 3];
        bq_frame_schema(image, BQ_SCHEMA_MATERIALIZATION, BQ_SUBMIT, 1, real.bytes, real.size);
        u32 old_size = BQ_HEADER_SIZE + real.size;
        BQ_CHECK(bq_test_old_replay(image, old_size, BQ_SCHEMA_MATERIALIZATION));
        bq_test_image(&fixture, image, old_size);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK &&
                 fixture.queue.state.journal_schema == BQ_SCHEMA_MATERIALIZATION && fixture.queue.bytes == old_size);
        BQ_CHECK(bq_u32(image + 8) == BQ_SCHEMA_MATERIALIZATION);
        BQ_CHECK(bq_cancel(&fixture.queue, 1) == BQ_OK && fixture.queue.state.journal_schema == BQ_SCHEMA);
        u32 upgraded_size = (u32)fixture.queue.bytes;
        BQ_CHECK(bq_read(fixture.queue.journal_fd, image, upgraded_size, 0));
        BQ_CHECK(bq_u32(image + 8) == BQ_SCHEMA_MATERIALIZATION && bq_u32(image + old_size + 8) == BQ_SCHEMA);
        BQ_CHECK(!bq_test_old_replay(image, upgraded_size, BQ_SCHEMA_MATERIALIZATION));
        bq_close(&fixture.queue);
        BQ_CHECK(bq_open(&fixture.queue, fixture.path) == BQ_OK && fixture.queue.state.journal_schema == BQ_SCHEMA &&
                 bq_job(&fixture.queue.state, 1)->outcome == BQ_CANCELLED);
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
        memset(body, 0, sizeof(body));
        bq_put32(body, BQ_PATH_CAP + 1);
        bq_packet(&request, BQ_OP_WORKER_RUN, 10, body, sizeof(body));
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST &&
                 queue->state.sequence == sequence);
        bq_put64(body, id);
        bq_packet(&request, BQ_OP_STATUS, 10, body, 9);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        bq_put64(body + 8, UINT64_MAX);
        bq_packet(&request, BQ_OP_LOGS, 10, body, 16);
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_BAD_REQUEST);
        BQ_CHECK(queue->state.sequence == sequence);
        BqRequest real = bq_test_real_request(88);
        bq_packet(&request, BQ_OP_SUBMIT, 11, real.bytes, real.size);
        queue->fault.fail_write_at = 1;
        BQ_CHECK(bq_dispatch(queue, request.bytes, request.size, &response) == BQ_IO && queue->poisoned);
        bq_test_end(&fixture);
    }
}

#ifdef __linux__
BUSTER_GLOBAL_LOCAL void bq_test_transport_boundaries(void)
{
    u32 public_operations[] = {BQ_OP_CAPABILITIES, BQ_OP_SUBMIT, BQ_OP_STATUS, BQ_OP_RESULT, BQ_OP_CANCEL, BQ_OP_LOGS};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(public_operations); i += 1)
    {
        BQ_CHECK(bq_transport_public_operation(public_operations[i]) == BQ_OK);
    }
    u32 private_operations[] = {BQ_OP_FAKE_RUN, BQ_OP_FAKE_RECONCILE, BQ_OP_MATERIALIZE,
                                BQ_OP_WORKSPACE_RECONCILE, BQ_OP_WORKER_RUN};
    for (u32 i = 0; i < BUSTER_ARRAY_LENGTH(private_operations); i += 1)
    {
        BQ_CHECK(bq_transport_public_operation(private_operations[i]) == BQ_BAD_REQUEST);
    }
    bq_worker_cancel_signal = 0;
    bq_worker_shutdown_signal = 0;
    bq_worker_cancel_handler(SIGTERM);
    BQ_CHECK(bq_worker_cancel_signal && bq_worker_shutdown_signal);
    bq_worker_cancel_signal = 0;
    bq_worker_shutdown_signal = 0;
    bq_worker_cancel_handler(SIGINT);
    BQ_CHECK(bq_worker_cancel_signal && bq_worker_shutdown_signal);
    bq_worker_cancel_signal = bq_worker_shutdown_signal = 0;
    bq_transport_stop_signal = 0;
    bq_worker_transport_stop_signal = 0;
    bq_transport_stop_handler(SIGTERM);
    BQ_CHECK(bq_transport_stop_signal && bq_worker_transport_stop_signal);
    bq_transport_stop_signal = 0;
    bq_worker_transport_stop_signal = 0;
    bq_transport_stop_handler(SIGINT);
    BQ_CHECK(bq_transport_stop_signal && bq_worker_transport_stop_signal);
    bq_transport_stop_signal = 0;
    bq_worker_transport_stop_signal = 0;
    BQ_CHECK(bq_transport_queue_admissible(&(BqQueue){0}));
    BqRequest fake = bq_test_request(7, false), real = {0};
    String8 fields[BQ_FIELD_COUNT] = {S8(BQ_EXPORT_PRINCIPAL), S8("request-7"), S8("validate-buster-v1"),
                                      S8("1111111111111111111111111111111111111111"),
                                      S8("2222222222222222222222222222222222222222")};
    BQ_CHECK(bq_request_make(fields, &real) == BQ_OK);
    BqPacket packet;
    bq_packet(&packet, BQ_OP_SUBMIT, 7, fake.bytes, fake.size);
    BQ_CHECK(bq_transport_public_request(packet.bytes, packet.size) == BQ_UNSUPPORTED);
    bq_packet(&packet, BQ_OP_SUBMIT, 8, real.bytes, real.size);
    BQ_CHECK(bq_transport_public_request(packet.bytes, packet.size) == BQ_OK);
    BqPacket request, response;
    u8 response_body[4] = {0};
    bq_packet(&request, BQ_OP_CAPABILITIES, 123, NULL, 0);
    bq_packet(&response, BQ_OP_CAPABILITIES | 0x80000000u, 123, response_body, sizeof(response_body));
    BQ_CHECK(bq_transport_response_matches(&request, &response));
    bq_put32(response.bytes + 4, BQ_CONTROL_SCHEMA - 1);
    BQ_CHECK(!bq_transport_response_matches(&request, &response));
    bq_put32(response.bytes + 4, BQ_CONTROL_SCHEMA);
    bq_put32(response.bytes + 8, BQ_OP_CAPABILITIES);
    BQ_CHECK(!bq_transport_response_matches(&request, &response));
    bq_put32(response.bytes + 8, BQ_OP_CAPABILITIES | 0x80000000u);
    bq_put64(response.bytes + 16, 124);
    BQ_CHECK(!bq_transport_response_matches(&request, &response));
    bq_put64(response.bytes + 16, 123);
    response.size = BQ_CONTROL_HEADER;
    BQ_CHECK(!bq_transport_response_matches(&request, &response));
    char missing_socket[BQ_PATH_CAP + 1];
    snprintf(missing_socket, sizeof(missing_socket), "/tmp/buster-transport-missing-%ld.sock", (long)getpid());
    unlink(missing_socket);
    response = (BqPacket){0};
    BQ_CHECK(bq_transport_request(missing_socket, &request, &response) == BQ_IO && !response.size);
    BQ_CHECK(bq_transport_request(missing_socket, &request, NULL) == BQ_BAD_REQUEST);
    BqQueue incompatible = {0};
    incompatible.state.job_count = 1;
    incompatible.state.jobs[0].phase = BQ_QUEUED;
    incompatible.state.jobs[0].request = fake;
    BQ_CHECK(!bq_transport_queue_admissible(&incompatible));
    incompatible.state.jobs[0].phase = BQ_FINISHED;
    BQ_CHECK(bq_transport_queue_admissible(&incompatible));
#ifdef __linux__
    BQ_CHECK(strstr(bq_capabilities_v2, "local-recipes=fake-success-v1,fake-failure-v1") != NULL);
    BQ_CHECK(strstr(bq_capabilities_v2, "service-recipes=validate-buster-v1 blocked-recipes=native-retirement-performance-v1") != NULL);
    BQ_CHECK(strstr(bq_capabilities_v2, "retirement=blocked") != NULL);
    char close_root[BQ_PATH_CAP + 1] = "/tmp/buster-transport-close-XXXXXX";
    bool close_root_ok = bq_test_mkdtemp_physical(close_root, sizeof(close_root));
    BQ_CHECK(close_root_ok);
    if (close_root_ok)
    {
        int parent = open(close_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        int replacement = parent >= 0 ? openat(parent, "socket", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600) : -1;
        struct stat info = {0};
        bool inspected = replacement >= 0 && fstat(replacement, &info) == 0;
        BqTransportEndpoint endpoint = {.listener = -1, .parent = parent, .device = inspected ? info.st_dev : 0,
                                        .inode = inspected ? info.st_ino : 0};
        snprintf(endpoint.leaf, sizeof(endpoint.leaf), "%s", "socket");
        BQ_CHECK(inspected && bq_transport_endpoint_close(&endpoint) == BQ_CONFIGURATION_MISMATCH);
        if (replacement >= 0) close(replacement);
        int verify_parent = open(close_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        int verify = verify_parent >= 0 ? openat(verify_parent, "socket", O_RDONLY | O_CLOEXEC) : -1;
        BQ_CHECK(verify >= 0);
        if (verify >= 0) close(verify);
        if (verify_parent >= 0)
        {
            BQ_CHECK(unlinkat(verify_parent, "socket", 0) == 0);
            close(verify_parent);
        }
        rmdir(close_root);
    }
    int packets[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, packets) == 0)
    {
        u8 short_frame = 0;
        u8 received[BQ_CONTROL_CAP];
        u32 received_size = 0;
        BQ_CHECK(send(packets[0], &short_frame, 1, MSG_NOSIGNAL) == 1);
        BQ_CHECK(bq_transport_receive(packets[1], received, &received_size) == BQ_BAD_REQUEST && received_size == 0);
        u64 before = bq_worker_monotonic_milliseconds();
        BQ_CHECK(bq_transport_receive(packets[1], received, &received_size) == BQ_IO &&
                 bq_worker_monotonic_milliseconds() - before >= BQ_TRANSPORT_IO_MILLISECONDS);
        close(packets[0]);
        close(packets[1]);
    }
    else
    {
        BQ_CHECK(errno == EPERM || errno == EAFNOSUPPORT || errno == ENOSYS);
    }
    int stream[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, stream) == 0)
    {
        int flags = fcntl(stream[0], F_GETFL);
        bool full = flags >= 0 && fcntl(stream[0], F_SETFL, flags | O_NONBLOCK) == 0;
        char fill[4096] = {0};
        for (u32 i = 0; full && i < 4096; i += 1)
        {
            ssize_t count = send(stream[0], fill, sizeof(fill), MSG_NOSIGNAL);
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                break;
            }
            if (count < 0)
            {
                full = false;
            }
        }
        BqPacket response;
        bq_packet(&response, BQ_OP_CAPABILITIES, 0, NULL, 0);
        u64 before = bq_worker_monotonic_milliseconds();
        BQ_CHECK(full && bq_transport_send(stream[0], response.bytes, response.size) == BQ_IO &&
                 bq_worker_monotonic_milliseconds() - before >= BQ_TRANSPORT_IO_MILLISECONDS);
        close(stream[0]);
        close(stream[1]);
    }
    else
    {
        BQ_CHECK(errno == EPERM || errno == EAFNOSUPPORT || errno == ENOSYS);
    }

    BqFixture service_fixture;
    if (bq_test_begin(&service_fixture))
    {
        char socket_path[BQ_PATH_CAP + 1];
        snprintf(socket_path, sizeof(socket_path), "%s/control.sock", service_fixture.path);
        bq_close(&service_fixture.queue);
        pid_t child = fork();
        BQ_CHECK(child >= 0);
        if (child == 0)
        {
            BqWorkerConfig config = {0};
            _exit(bq_transport_serve(service_fixture.path, socket_path, &config) == BQ_OK ? 0 : 1);
        }
        if (child > 0)
        {
            struct stat socket_info = {0};
            bool ready = false;
            for (u32 attempt = 0; attempt < 200 && !ready; attempt += 1)
            {
                ready = lstat(socket_path, &socket_info) == 0 && S_ISSOCK(socket_info.st_mode);
                if (!ready) usleep(10000);
            }
            BQ_CHECK(ready);
            FILE* input = tmpfile();
            FILE* output = tmpfile();
            FILE* diagnostics = tmpfile();
            BQ_CHECK(input && output && diagnostics);
            if (input && output && diagnostics)
            {
                char* capabilities[] = {"bench_service", "client", socket_path, "capabilities"};
                BQ_CHECK(bq_cli(4, capabilities, input, output, diagnostics) == 0);
                rewind(output);
                char text[2048] = {0};
                size_t count = fread(text, 1, sizeof(text) - 1, output);
                BQ_CHECK(count && strstr(text, "service-recipes=validate-buster-v1") != NULL);
                fclose(output);
                fclose(diagnostics);
                output = tmpfile();
                diagnostics = tmpfile();
                BQ_CHECK(output && diagnostics);
                if (output && diagnostics)
                {
                    char* status[] = {"bench_service", "client", socket_path, "status", "1"};
                    BQ_CHECK(bq_cli(5, status, input, output, diagnostics) != 0);
                    rewind(diagnostics);
                    memset(text, 0, sizeof(text));
                    count = fread(text, 1, sizeof(text) - 1, diagnostics);
                    BQ_CHECK(count && strstr(text, "not-found") != NULL);
                    char* fake_submit[] = {"bench_service", "client", socket_path, "submit", "test-principal",
                                           "request-typed", "fake-success-v1",
                                           "1111111111111111111111111111111111111111",
                                           "2222222222222222222222222222222222222222"};
                    BQ_CHECK(bq_cli(9, fake_submit, input, output, diagnostics) != 0);
                }
            }
            if (input) fclose(input);
            if (output) fclose(output);
            if (diagnostics) fclose(diagnostics);
            BQ_CHECK(kill(child, SIGTERM) == 0);
            int status = 0;
            BQ_CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
        bq_test_end(&service_fixture);
    }
#endif
}

#include "worker_deadline_tests.c"

BUSTER_GLOBAL_LOCAL bool bq_test_worker_probe_locked(char const* path);

BUSTER_GLOBAL_LOCAL void bq_test_worker_lease_handoff(void)
{
    char root[] = "/tmp/buster-lease-handoff-XXXXXX";
    char result_root[BQ_PATH_CAP + 1], lease_path[BQ_PATH_CAP + 1];
    int result_directory = -1;
    int ready_pipe[2] = {-1, -1}, release_pipe[2] = {-1, -1};
    pid_t child = -1;
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    bool ok = bq_test_mkdtemp_physical(root, sizeof(root));
    int result_length = snprintf(result_root, sizeof(result_root), "%s/result", root);
    int lease_length = snprintf(lease_path, sizeof(lease_path), "%s/host.lock", root);
    ok = ok && result_length > 0 && (u32)result_length < sizeof(result_root) &&
         lease_length > 0 && (u32)lease_length < sizeof(lease_path) && mkdir(result_root, 0700) == 0 &&
         (result_directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) >= 0 &&
         bq_worker_lease_acquire(lease_path, &lease) == 0 &&
         bq_worker_lease_handoff_open(result_root, result_directory, &handoff) &&
         pipe(ready_pipe) == 0 && pipe(release_pipe) == 0;
    if (ok) child = fork();
    if (child == 0)
    {
        close(ready_pipe[0]);
        close(release_pipe[1]);
        close(handoff.listener);
        close(handoff.parent);
        close(result_directory);
        /* The fork inherited the coordinator's locked open-file description.
         * Close it before receiving so the child can hold the lease only via
         * the authenticated SCM_RIGHTS transfer.  Otherwise this test would
         * pass even when the handoff omitted the descriptor. */
        if (lease.descriptor >= 0) close(lease.descriptor);
        lease.descriptor = -1;
        BqWorkerLease transferred = {.descriptor = -1};
        BqError received = bq_worker_lease_handoff_receive(string_from_pointer(lease_path), string_from_pointer(result_root),
                                                            S8("1"), S8("2"),
                                                            &transferred);
        u8 state = received == BQ_OK ? 1 : 0;
        ssize_t written = write(ready_pipe[1], &state, sizeof(state));
        char release = 0;
        ssize_t released = written == sizeof(state) ? read(release_pipe[0], &release, sizeof(release)) : -1;
        if (released == sizeof(release)) bq_worker_lease_release(&transferred);
        close(ready_pipe[1]);
        close(release_pipe[0]);
        _exit(received == BQ_OK && released == sizeof(release) ? 0 : 1);
    }
    if (child > 0)
    {
        close(ready_pipe[1]);
        close(release_pipe[0]);
        BqError sent = bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, 1, 2);
        if (sent == BQ_OK) bq_worker_lease_release(&lease);
        u8 state = 0;
        ssize_t read_state = read(ready_pipe[0], &state, sizeof(state));
        BQ_CHECK(sent == BQ_OK && read_state == sizeof(state) && state == 1 && bq_test_worker_probe_locked(lease_path));
        BQ_CHECK(handoff.listener < 0 && handoff.parent < 0);
        struct stat missing = {0};
        BQ_CHECK(fstatat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, &missing, AT_SYMLINK_NOFOLLOW) != 0 &&
                 errno == ENOENT);
        char empty_bundle[] = "BQ-BUNDLE-V1\nentries=0\nbytes=0\n";
        char empty_bundle_digest[SHA256_HEX_CAPACITY];
        bq_digest(empty_bundle, (u32)strlen(empty_bundle), empty_bundle_digest);
        char bundle_path[BQ_PATH_CAP + 64];
        int bundle_length = snprintf(bundle_path, sizeof(bundle_path), "%s/validate-buster-v1.bundle", result_root);
        bool bundle_ok = bundle_length > 0 && (u32)bundle_length < sizeof(bundle_path) &&
                         bq_test_write_path(bundle_path, empty_bundle, 0400);
        BQ_CHECK(bundle_ok && bq_worker_bundle_validate_full(result_directory, empty_bundle_digest, NULL) == BQ_OK &&
                 bq_test_worker_probe_locked(lease_path));
        if (bundle_length > 0 && (u32)bundle_length < sizeof(bundle_path)) unlink(bundle_path);
        char release = 1;
        BQ_CHECK(write(release_pipe[1], &release, sizeof(release)) == sizeof(release));
        bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 20));
    }
    if (child > 0)
    {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && !bq_test_worker_probe_locked(lease_path));
    }
    if (child < 0 && ok) BQ_CHECK(false);
    if (ready_pipe[0] >= 0) close(ready_pipe[0]);
    if (ready_pipe[1] >= 0) close(ready_pipe[1]);
    if (release_pipe[0] >= 0) close(release_pipe[0]);
    if (release_pipe[1] >= 0) close(release_pipe[1]);
    bq_worker_lease_handoff_close(&handoff);
    bq_worker_lease_release(&lease);
    if (result_directory >= 0) close(result_directory);
    unlink(lease_path);
    rmdir(result_root);
    rmdir(root);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_lease_handoff_negative(u32 mode)
{
    char root[] = "/tmp/buster-lease-handoff-negative-XXXXXX";
    char result_root[BQ_PATH_CAP + 1], lease_path[BQ_PATH_CAP + 1], socket_path[BQ_PATH_CAP + 1];
    int result_directory = -1, ready_pipe[2] = {-1, -1}, hold_pipe[2] = {-1, -1};
    pid_t child = -1;
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    bool ok = bq_test_mkdtemp_physical(root, sizeof(root));
    int result_length = snprintf(result_root, sizeof(result_root), "%s/result", root);
    int lease_length = snprintf(lease_path, sizeof(lease_path), "%s/host.lock", root);
    ok = ok && result_length > 0 && (u32)result_length < sizeof(result_root) &&
         lease_length > 0 && (u32)lease_length < sizeof(lease_path) && mkdir(result_root, 0700) == 0 &&
         (result_directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) >= 0 &&
         bq_worker_lease_acquire(lease_path, &lease) == 0 &&
         bq_worker_lease_handoff_open(result_root, result_directory, &handoff) &&
         bq_worker_lease_socket_path(result_root, socket_path) && pipe(ready_pipe) == 0 &&
         pipe(hold_pipe) == 0;
    if (ok) child = fork();
    if (child == 0)
    {
        close(ready_pipe[0]);
        close(hold_pipe[1]);
        close(handoff.listener);
        close(handoff.parent);
        close(result_directory);
        if (lease.descriptor >= 0) close(lease.descriptor);
        lease.descriptor = -1;
        int client = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        struct sockaddr_un address = {0};
        bool received_ok = client >= 0;
        if (received_ok)
        {
            size_t length = strlen(socket_path);
            address.sun_family = AF_UNIX;
            memcpy(address.sun_path, socket_path, length + 1);
            socklen_t address_size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length + 1);
            received_ok = connect(client, (struct sockaddr*)&address, address_size) == 0;
        }
        BqWorkerLeaseMessage request = {0};
        BqWorkerLeaseMessage response = {0};
        struct stat lease_info = {0};
        int lease_probe = open(lease_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        received_ok = received_ok && lease_probe >= 0 && fstat(lease_probe, &lease_info) == 0;
        if (lease_probe >= 0) close(lease_probe);
        received_ok = received_ok && bq_worker_lease_message_make(&request, BQ_WORKER_LEASE_REQUEST, lease_path, 1, 2, 0, 0) &&
                      send(client, &request, sizeof(request), MSG_NOSIGNAL) == (ssize_t)sizeof(request);
        int received_fd = -1;
        if (received_ok)
        {
            char control[CMSG_SPACE(sizeof(received_fd))] = {0};
            struct iovec vector = {&response, sizeof(response)};
            struct msghdr message = {0};
            message.msg_iov = &vector;
            message.msg_iovlen = 1;
            message.msg_control = control;
            message.msg_controllen = sizeof(control);
            ssize_t count = recvmsg(client, &message, MSG_CMSG_CLOEXEC);
            received_ok = count == (ssize_t)sizeof(response) && !(message.msg_flags & MSG_CTRUNC) &&
                          bq_worker_lease_message_matches(&response, BQ_WORKER_LEASE_RESPONSE, lease_path, 1, 2,
                                                          (u64)lease_info.st_dev, (u64)lease_info.st_ino);
            for (struct cmsghdr* header = received_ok ? CMSG_FIRSTHDR(&message) : NULL; header;
                 header = CMSG_NXTHDR(&message, header))
            {
                if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                    header->cmsg_len == CMSG_LEN(sizeof(received_fd)) && received_fd < 0)
                    memcpy(&received_fd, CMSG_DATA(header), sizeof(received_fd));
            }
            received_ok = received_ok && received_fd >= 3;
        }
        u8 state = received_ok ? 1 : 0;
        if (received_ok && mode == 1)
        {
            BqWorkerLeaseMessage invalid = response;
            invalid.phase = BQ_WORKER_LEASE_ACK;
            invalid.inode += 1;
            received_ok = send(client, &invalid, sizeof(invalid), MSG_NOSIGNAL) == (ssize_t)sizeof(invalid);
        }
        if (received_fd >= 0) close(received_fd);
        if (write(ready_pipe[1], &state, sizeof(state)) != sizeof(state)) received_ok = false;
        if (received_ok && mode == 2)
        {
            char hold = 0;
            received_ok = read(hold_pipe[0], &hold, sizeof(hold)) == sizeof(hold);
        }
        if (client >= 0) close(client);
        close(ready_pipe[1]);
        close(hold_pipe[0]);
        _exit(received_ok ? 0 : 1);
    }
    if (child > 0)
    {
        close(ready_pipe[1]);
        close(hold_pipe[0]);
        hold_pipe[0] = -1;
        BqError sent = bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, 1, 2);
        u8 state = 0;
        ssize_t read_state = read(ready_pipe[0], &state, sizeof(state));
        bool coordinator_valid = lease.descriptor >= 0 && fcntl(lease.descriptor, F_GETFD) >= 0;
        BQ_CHECK(sent != BQ_OK && read_state == sizeof(state) && state == 1 && bq_test_worker_probe_locked(lease_path));
        BQ_CHECK(mode != 2 || (sent != BQ_OK && coordinator_valid && bq_test_worker_probe_locked(lease_path)));
        if (mode == 2 && read_state == sizeof(state) && state == 1)
        {
            char release = 1;
            BQ_CHECK(write(hold_pipe[1], &release, sizeof(release)) == sizeof(release));
        }
        bq_worker_lease_release(&lease);
    }
    if (child > 0)
    {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && !bq_test_worker_probe_locked(lease_path));
    }
    if (child < 0 && ok) BQ_CHECK(false);
    if (ready_pipe[0] >= 0) close(ready_pipe[0]);
    if (ready_pipe[1] >= 0) close(ready_pipe[1]);
    if (hold_pipe[0] >= 0) close(hold_pipe[0]);
    if (hold_pipe[1] >= 0) close(hold_pipe[1]);
    bq_worker_lease_handoff_close(&handoff);
    bq_worker_lease_release(&lease);
    if (result_directory >= 0) close(result_directory);
    unlink(lease_path);
    rmdir(result_root);
    rmdir(root);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_lease_handoff_cleanup_failure(u32 mode)
{
    char root[] = "/tmp/buster-lease-cleanup-XXXXXX";
    char result_root[BQ_PATH_CAP + 1], lease_path[BQ_PATH_CAP + 1];
    int result_directory = -1;
    int ready_pipe[2] = {-1, -1}, hold_pipe[2] = {-1, -1};
    pid_t child = -1;
    BqWorkerLease lease = {.descriptor = -1};
    BqWorkerLeaseHandoff handoff = {.listener = -1, .parent = -1};
    bool ok = bq_test_mkdtemp_physical(root, sizeof(root));
    int result_length = snprintf(result_root, sizeof(result_root), "%s/result", root);
    int lease_length = snprintf(lease_path, sizeof(lease_path), "%s/host.lock", root);
    ok = ok && result_length > 0 && (u32)result_length < sizeof(result_root) &&
         lease_length > 0 && (u32)lease_length < sizeof(lease_path) && mkdir(result_root, 0700) == 0 &&
         (result_directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) >= 0 &&
         bq_worker_lease_acquire(lease_path, &lease) == 0 &&
         bq_worker_lease_handoff_open(result_root, result_directory, &handoff) &&
         pipe(ready_pipe) == 0 && pipe(hold_pipe) == 0;
    if (ok) child = fork();
    if (child == 0)
    {
        close(ready_pipe[0]);
        close(hold_pipe[1]);
        close(handoff.listener);
        close(handoff.parent);
        close(result_directory);
        if (lease.descriptor >= 0) close(lease.descriptor);
        lease.descriptor = -1;
        BqWorkerLease transferred = {.descriptor = -1};
        BqError received = bq_worker_lease_handoff_receive(string_from_pointer(lease_path), string_from_pointer(result_root),
                                                            S8("1"), S8("2"), &transferred);
        u8 state = received == BQ_OK ? 1 : 0;
        ssize_t written = write(ready_pipe[1], &state, sizeof(state));
        char hold = 0;
        ssize_t released = written == sizeof(state) ? read(hold_pipe[0], &hold, sizeof(hold)) : -1;
        if (released == sizeof(hold)) bq_worker_lease_release(&transferred);
        close(ready_pipe[1]);
        close(hold_pipe[0]);
        _exit(received == BQ_OK && released == sizeof(hold) ? 0 : 1);
    }
    if (child > 0)
    {
        close(ready_pipe[1]);
        close(hold_pipe[0]);
        hold_pipe[0] = -1;
        ready_pipe[1] = -1;
        bq_worker_test_handoff_unlink_failure = mode == 0;
        bq_worker_test_handoff_fsync_failure = mode == 1;
        bq_worker_test_handoff_listener_close_failure = mode == 2;
        bq_worker_test_handoff_parent_close_failure = mode == 3;
        BqError sent = bq_worker_lease_handoff_send(&handoff, lease.descriptor, lease_path, 1, 2);
        bq_worker_test_handoff_unlink_failure = false;
        bq_worker_test_handoff_fsync_failure = false;
        bq_worker_test_handoff_listener_close_failure = false;
        bq_worker_test_handoff_parent_close_failure = false;
        u8 state = 0;
        ssize_t read_state = read(ready_pipe[0], &state, sizeof(state));
        BQ_CHECK(sent == BQ_IO && read_state == sizeof(state) && state == 1 &&
                 bq_test_worker_probe_locked(lease_path));
        BQ_CHECK(handoff.listener < 0 && handoff.parent < 0);
        BQ_CHECK(bq_worker_lease_handoff_close(&handoff));
        struct stat missing = {0};
        errno = 0;
        bool still_present = fstatat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, &missing,
                                     AT_SYMLINK_NOFOLLOW) == 0;
        if (still_present) unlinkat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, 0);
        errno = 0;
        BQ_CHECK(fstatat(result_directory, BQ_WORKER_LEASE_HANDOFF_NAME, &missing, AT_SYMLINK_NOFOLLOW) != 0 &&
                 errno == ENOENT);
        if (read_state == sizeof(state) && state == 1)
        {
            char release = 1;
            BQ_CHECK(write(hold_pipe[1], &release, sizeof(release)) == sizeof(release));
        }
        bq_worker_lease_release(&lease);
    }
    if (child > 0)
    {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        BQ_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0 && !bq_test_worker_probe_locked(lease_path));
    }
    if (child < 0 && ok) BQ_CHECK(false);
    if (ready_pipe[0] >= 0) close(ready_pipe[0]);
    if (ready_pipe[1] >= 0) close(ready_pipe[1]);
    if (hold_pipe[0] >= 0) close(hold_pipe[0]);
    if (hold_pipe[1] >= 0) close(hold_pipe[1]);
    bq_worker_lease_handoff_close(&handoff);
    bq_worker_lease_release(&lease);
    if (result_directory >= 0) close(result_directory);
    unlink(lease_path);
    rmdir(result_root);
    rmdir(root);
}

typedef struct BqWorkerFake
{
    BqQueue* queue;
    BqWorkerObserved observed;
    BqWorkerObserved child;
    BqWorkerResult completion;
    pid_t detached;
    pid_t launcher;
    int inherited_lease;
    u32 starts;
    u32 continues;
    u32 terms;
    u32 kills;
    u32 joins;
    u32 launcher_cleanups;
    u32 observes;
    u32 child_observes;
    u32 child_terms;
    u32 child_kills;
    u32 child_collect_polls;
    u32 child_collect_after;
    u32 cgroup_release_polls;
    u32 cgroup_release_after;
    u32 delays;
    u32 term_polls;
    u32 observe_failures;
    u32 term_clear_after;
    u32 reuse_after_observe;
    u32 cancel_after_observe;
    u64 elapsed;
    bool cancel_on_join;
    bool mismatch_unit;
    bool mismatch_resources;
    bool hide_unit;
    bool spawn_detached;
    bool spawn_launcher;
    bool spawn_fixed_recipe;
    bool block_join;
    bool term_clears;
    bool kill_clears;
    bool child_term_clears;
    bool child_kill_clears;
    bool child_collect_without_kill;
    bool argv_valid;
    bool start_error;
    bool skip_inherited_lease;
    bool drain_on_join;
    bool collect_on_join;
    bool replace_slice_on_cleanup_join;
    bool launcher_cleanup_failure;
} BqWorkerFake;

typedef struct BqWorkerFixture
{
    BqMaterialFixture material;
    BqWorkerFake fake;
    BqWorkerBackend backend;
    BqWorkerConfig config;
    char root[BQ_PATH_CAP + 1];
    char boot[512];
    char lease[512];
    char unit[BQ_WORKER_UNIT_CAP];
    BqWorkerQuarantine quarantine;
} BqWorkerFixture;

BUSTER_GLOBAL_LOCAL bool bq_test_worker_probe_locked(char const* path)
{
    BqWorkerLease probe = {.descriptor = -1};
    int error = bq_worker_lease_acquire(path, &probe);
    if (!error) bq_worker_lease_release(&probe);
    return error == EWOULDBLOCK || error == EAGAIN;
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_start(BqWorkerBackend* backend, char const* const* argv, u32 count)
{
    BqWorkerFake* fake = backend->context;
    BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
    BqError error = BQ_OK;
    fake->starts += 1;
    fake->argv_valid = count == 6 && !strcmp(argv[0], BQ_SYSTEMD_BROKER) &&
        !strcmp(argv[1], "start-outer") && !strcmp(argv[2], "1") && argv[3][0] &&
        strlen(argv[4]) == 64 && strlen(argv[5]) == 64 &&
        bq_test_worker_probe_locked(fixture->lease);
    fake->inherited_lease = -1;
    snprintf(fake->observed.unit, sizeof(fake->observed.unit), "buster-bench-%s-%s.service", argv[2], argv[3]);
    snprintf(fake->observed.cgroup, sizeof(fake->observed.cgroup), "/buster.slice/buster-bench.slice/%s", fake->observed.unit);
    fake->observed.unit_found = true;
    fake->observed.active = true;
    fake->observed.populated = true;
    fake->observed.result = BQ_WORKER_RUNNING;
    if (fake->spawn_detached)
    {
        fake->detached = fork();
        if (!fake->detached)
        {
            close(fake->queue->directory_fd);
            close(fake->queue->lock_fd);
            close(fake->queue->journal_fd);
            setsid();
            for (;;) pause();
        }
        if (fake->detached < 0) error = BQ_IO;
    }
    else if (error == BQ_OK && fake->spawn_fixed_recipe)
    {
        char marker[BQ_PATH_CAP + 64];
        int marker_length = snprintf(marker, sizeof(marker), "%s/fixed-recipe.running", fixture->root);
        fake->detached = marker_length > 0 && (u32)marker_length < sizeof(marker) ? fork() : -1;
        if (!fake->detached)
        {
            close(fake->queue->directory_fd);
            close(fake->queue->lock_fd);
            close(fake->queue->journal_fd);
            char result[BQ_PATH_CAP + 1];
            int result_length = snprintf(result, sizeof(result), "%s/results/job-%s-attempt-%s",
                                         fixture->material.workspaces, argv[2], argv[3]);
            if (result_length > 0 && (u32)result_length < sizeof(result))
                execl(bq_test_executable, bq_test_executable, "fixed-recipe-helper", marker,
                      argv[2], argv[3], "validate-buster-v1", fixture->material.workspaces,
                      argv[4], argv[5], result, NULL);
            _exit(127);
        }
        if (fake->detached < 0) error = BQ_IO;
    }
    if (error == BQ_OK && fake->spawn_launcher)
    {
        fake->launcher = fork();
        if (!fake->launcher)
        {
            close(fake->queue->directory_fd);
            close(fake->queue->lock_fd);
            close(fake->queue->journal_fd);
            for (;;) pause();
        }
        if (fake->launcher < 0) error = BQ_IO;
    }
    if (error == BQ_OK) error = fake->start_error ? BQ_IO : fake->argv_valid ? BQ_OK : BQ_BAD_REQUEST;
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_reap(BqWorkerFake* fake);
BUSTER_GLOBAL_LOCAL bool bq_test_worker_limits(char const* root, char const* unit);
BUSTER_GLOBAL_LOCAL bool bq_test_worker_remove_cgroup(char const* root, char const* unit);

BUSTER_GLOBAL_LOCAL bool bq_test_worker_replace_slice(BqWorkerFake* fake)
{
    BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
    char slice[768], old[768];
    snprintf(slice, sizeof(slice), "%s/buster.slice/buster-bench.slice", fixture->root);
    snprintf(old, sizeof(old), "%s/retired.slice", fixture->root);
    return rename(slice, old) == 0 && bq_test_worker_limits(fixture->root, fake->observed.unit);
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_observe(BqWorkerBackend* backend, char const* unit,
                                                    BqWorkerObserved* observed, u64 deadline)
{
    BqWorkerFake* fake = backend->context;
    BqError error = BQ_OK;
    BQ_CHECK(fake->elapsed <= deadline);
    bool outer = fake->observed.unit[0] && !strcmp(unit, fake->observed.unit);
    bool child = fake->child.unit[0] && !strcmp(unit, fake->child.unit);
    if (outer) fake->observes += 1;
    else if (child) fake->child_observes += 1;
    if (outer && fake->observe_failures)
    {
        if (fake->observe_failures != UINT32_MAX) fake->observe_failures -= 1;
        error = BQ_IO;
    }
    else
    {
        if (outer && fake->terms && !fake->kills && fake->observed.populated)
        {
            fake->term_polls += 1;
            if (fake->term_clear_after && fake->term_polls >= fake->term_clear_after) bq_test_worker_reap(fake);
        }
        if (child && fake->child_collect_after &&
            (fake->child_kills || fake->child_collect_without_kill) && fake->child.unit_found &&
            !fake->child.active && !fake->child.populated)
        {
            fake->child_collect_polls += 1;
            if (fake->child_collect_polls >= fake->child_collect_after) fake->child.unit_found = false;
        }
        *observed = outer ? fake->observed : child ? fake->child : (BqWorkerObserved){0};
        if (outer && fake->hide_unit) observed->unit_found = false;
        if (outer && fake->mismatch_unit) snprintf(observed->unit, sizeof(observed->unit), "%s", "foreign.scope");
        if (outer && fake->mismatch_resources) observed->memory_max -= 1;
        if (outer && fake->reuse_after_observe && fake->observes >= fake->reuse_after_observe)
            snprintf(observed->invocation_id, sizeof(observed->invocation_id), "%s",
                     "ffffffffffffffffffffffffffffffff");
        if (outer && fake->cancel_after_observe && fake->observes >= fake->cancel_after_observe)
            BQ_CHECK(raise(SIGTERM) == 0);
    }
    return error;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_reap_child(BqWorkerFake* fake)
{
    BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
    if (fake->child.unit[0]) BQ_CHECK(bq_test_worker_remove_cgroup(fixture->root, fake->child.unit));
    fake->child.active = false;
    fake->child.populated = false;
    if (!fake->child_collect_after) fake->child.unit_found = false;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_reap(BqWorkerFake* fake)
{
    if (fake->launcher > 0)
    {
        kill(fake->launcher, SIGKILL);
        while (waitpid(fake->launcher, NULL, 0) < 0 && errno == EINTR) {}
        fake->launcher = 0;
    }
    if (fake->detached > 0)
    {
        kill(fake->detached, SIGKILL);
        while (waitpid(fake->detached, NULL, 0) < 0 && errno == EINTR) {}
        fake->detached = 0;
    }
    if (fake->inherited_lease >= 0)
    {
        close(fake->inherited_lease);
        fake->inherited_lease = -1;
    }
    fake->observed.active = false;
    fake->observed.populated = false;
    BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
    char events[1024];
    snprintf(events, sizeof(events), "%s/buster.slice/buster-bench.slice/%s/cgroup.events",
             fixture->root, fake->observed.unit);
    if (fake->observed.unit[0] && chmod(events, 0600) == 0)
    {
        int fd = open(events, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        char const empty[] = "populated 0\nfrozen 0\n";
        BQ_CHECK(fd >= 0 && bq_write_all(fd, (u8 const*)empty, sizeof(empty) - 1) && fsync(fd) == 0);
        if (fd >= 0) close(fd);
    }
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_signal(BqWorkerBackend* backend, char const* unit,
                                                   char const* signal_name, u64 deadline)
{
    BqWorkerFake* fake = backend->context;
    BQ_CHECK(fake->elapsed <= deadline);
    bool child = fake->child.unit[0] && !strcmp(unit, fake->child.unit);
    if (child && !strcmp(signal_name, "TERM"))
    {
        fake->child_terms += 1;
        if (fake->child_term_clears) bq_test_worker_reap_child(fake);
    }
    else if (child && !strcmp(signal_name, "KILL"))
    {
        fake->child_kills += 1;
        if (fake->child_kill_clears) bq_test_worker_reap_child(fake);
    }
    else if (!strcmp(signal_name, "CONT"))
    {
        fake->continues += 1;
    }
    else if (!strcmp(signal_name, "TERM"))
    {
        fake->terms += 1;
        if (fake->term_clears) bq_test_worker_reap(fake);
    }
    else if (!strcmp(signal_name, "KILL"))
    {
        fake->kills += 1;
        if (fake->kill_clears) bq_test_worker_reap(fake);
    }
    return BQ_OK;
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_join(BqWorkerBackend* backend, int* status, u64 deadline)
{
    BqWorkerFake* fake = backend->context;
    BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
    BQ_CHECK(bq_test_worker_probe_locked(fixture->lease));
    BQ_CHECK(fake->elapsed <= deadline);
    fake->joins += 1;
    BqError error = BQ_OK;
    if (fake->block_join)
    {
        u64 reap_deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000);
        pid_t reaped = 0;
        int child_status = 0;
        while (!reaped && bq_worker_monotonic_milliseconds() < reap_deadline)
        {
            reaped = waitpid(fake->detached, &child_status, WNOHANG);
            if (!reaped) bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10));
        }
        char marker[BQ_PATH_CAP + 64];
        int length = snprintf(marker, sizeof(marker), "%s/fixed-recipe.reaped", fixture->root);
        bool child_killed = reaped == fake->detached && WIFSIGNALED(child_status) && WTERMSIG(child_status) == SIGKILL;
        if (child_killed) fake->detached = 0;
        if (!child_killed || length <= 0 || (u32)length >= sizeof(marker) ||
            !bq_test_write_path(marker, "reaped\n", 0400))
            error = BQ_IO;
        while (fake->block_join) pause();
    }
    if (fake->cancel_on_join)
    {
        fake->cancel_on_join = false;
        error = BQ_WORKER_CANCEL_SIGNAL;
    }
    fake->observed.active = false;
    fake->observed.populated = fake->detached > 0;
    fake->observed.result = fake->completion;
    if (fake->drain_on_join && !fake->observed.populated && fake->observed.cgroup[0])
    {
        BQ_CHECK(bq_test_worker_remove_cgroup(fixture->root, fake->observed.unit));
        fake->observed.cgroup[0] = 0;
    }
    if (fake->collect_on_join && !fake->observed.populated) fake->observed.unit_found = false;
    if (fake->detached <= 0 && fake->inherited_lease >= 0)
    {
        close(fake->inherited_lease);
        fake->inherited_lease = -1;
    }
    if (fake->replace_slice_on_cleanup_join && fake->joins == 2)
        BQ_CHECK(bq_test_worker_replace_slice(fake));
    *status = fake->completion == BQ_WORKER_SUCCEEDED ? 0 :
              fake->completion == BQ_WORKER_OOM ? SIGKILL : 1 << 8;
    return error;
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_delay(BqWorkerBackend* backend, u32 milliseconds)
{
    BqWorkerFake* fake = backend->context;
    BQ_CHECK(milliseconds == 100);
    fake->delays += 1;
    fake->elapsed += milliseconds;
    if (fake->cgroup_release_after && !fake->observed.unit_found)
    {
        fake->cgroup_release_polls += 1;
        if (fake->cgroup_release_polls == fake->cgroup_release_after)
        {
            BqWorkerFixture* fixture = (BqWorkerFixture*)((char*)fake - offsetof(BqWorkerFixture, fake));
            BQ_CHECK(bq_test_worker_remove_cgroup(fixture->root, fixture->unit));
            fake->observed.cgroup[0] = 0;
        }
    }
    return BQ_OK;
}

BUSTER_GLOBAL_LOCAL BqError bq_test_worker_cleanup_launcher(BqWorkerBackend* backend, u64 deadline)
{
    BqWorkerFake* fake = backend->context;
    BQ_CHECK(fake->elapsed <= deadline);
    fake->launcher_cleanups += 1;
    BqError error = fake->launcher_cleanup_failure ? BQ_CLEANUP_FAILED : BQ_OK;
    if (error == BQ_OK && fake->launcher > 0)
    {
        BQ_CHECK(kill(fake->launcher, SIGKILL) == 0);
        int status = 0;
        error = bq_worker_waitpid_until(fake->launcher, &status,
                                        bq_worker_deadline(bq_worker_monotonic_milliseconds(), 1000));
        if (error == BQ_OK) fake->launcher = 0;
    }
    return error;
}

BUSTER_GLOBAL_LOCAL u64 bq_test_worker_clock(BqWorkerBackend* backend)
{
    BqWorkerFake* fake = backend->context;
    u64 result = fake->elapsed;
    return result;
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_limits(char const* root, char const* unit)
{
    char parent[512], slice[768], leaf[1024], path[1152];
    snprintf(parent, sizeof(parent), "%s/buster.slice", root);
    snprintf(slice, sizeof(slice), "%s/buster.slice/buster-bench.slice", root);
    snprintf(leaf, sizeof(leaf), "%s/%s", slice, unit);
    bool ok = (mkdir(parent, 0700) == 0 || errno == EEXIST) &&
              (mkdir(slice, 0700) == 0 || errno == EEXIST) && mkdir(leaf, 0700) == 0;
    char const* directories[] = {parent, slice, leaf};
    char const* names[] = {"cpuset.cpus.effective", "memory.max", "memory.swap.max", "pids.max"};
    char const* ancestor_limits[] = {"0-7\n", "max\n", "max\n", "max\n"};
    char const* child[] = {"2\n", "8589934592\n", "0\n", "256\n"};
    for (u32 directory = 0; ok && directory < BUSTER_ARRAY_LENGTH(directories); directory += 1)
    {
        for (u32 file = 0; ok && file < BUSTER_ARRAY_LENGTH(names); file += 1)
        {
            snprintf(path, sizeof(path), "%s/%s", directories[directory], names[file]);
            ok = access(path, F_OK) == 0 ||
                 bq_test_write_path(path, directory == 2 ? child[file] : ancestor_limits[file], 0400);
        }
    }
    snprintf(path, sizeof(path), "%s/cgroup.events", leaf);
    return ok && bq_test_write_path(path, "populated 1\nfrozen 0\n", 0400);
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_remove_cgroup(char const* root, char const* unit)
{
    int root_fd = bq_open_absolute_directory(string_from_pointer(root));
    int parent = root_fd >= 0 ? openat(root_fd, "buster.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int slice = parent >= 0 ? openat(parent, "buster-bench.slice", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    int leaf = slice >= 0 ? openat(slice, unit, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW) : -1;
    bool ok = leaf >= 0 && bq_remove_workspace_payload(leaf) && unlinkat(slice, unit, AT_REMOVEDIR) == 0;
    if (leaf >= 0) close(leaf);
    if (slice >= 0) close(slice);
    if (parent >= 0) close(parent);
    if (root_fd >= 0) close(root_fd);
    return ok;
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_begin(BqWorkerFixture* fixture, BqWorkerResult result, bool detached)
{
    *fixture = (BqWorkerFixture){0};
    bq_worker_cancel_signal = 0;
    bq_worker_test_finish_checkpoints = 0;
    bq_worker_test_cancel_during_finish = 0;
    fixture->quarantine.descriptor = -1;
    snprintf(fixture->root, sizeof(fixture->root), "/tmp/buster-worker-cgroup-XXXXXX");
    bool ok = bq_material_test_begin(&fixture->material, 0) &&
              bq_test_mkdtemp_physical(fixture->root, sizeof(fixture->root));
    snprintf(fixture->boot, sizeof(fixture->boot), "%s/boot-id", fixture->root);
    snprintf(fixture->lease, sizeof(fixture->lease), "%s/host.lock", fixture->root);
    snprintf(fixture->unit, sizeof(fixture->unit), "buster-bench-1-2.service");
    ok = ok && bq_test_write_path(fixture->boot, "12345678-1234-1234-1234-123456789abc\n", 0600) &&
         bq_test_worker_limits(fixture->root, fixture->unit);
    fixture->fake = (BqWorkerFake){
        .queue = &fixture->material.queue.queue,
        .completion = result,
        .inherited_lease = -1,
        .spawn_detached = detached,
        .kill_clears = true
    };
    snprintf(fixture->fake.observed.boot_id, sizeof(fixture->fake.observed.boot_id), "%s",
             "12345678-1234-1234-1234-123456789abc");
    snprintf(fixture->fake.observed.invocation_id, sizeof(fixture->fake.observed.invocation_id), "%s",
             "0123456789abcdef0123456789abcdef");
    snprintf(fixture->fake.observed.user, sizeof(fixture->fake.observed.user), "%s", "buster-bench");
    snprintf(fixture->fake.observed.group, sizeof(fixture->fake.observed.group), "%s", "buster-bench");
    snprintf(fixture->fake.observed.allowed_cpus, sizeof(fixture->fake.observed.allowed_cpus), "%s", "2");
    fixture->fake.observed.memory_max = 8ull * 1024 * 1024 * 1024;
    fixture->fake.observed.memory_swap_max = 0;
    fixture->fake.observed.tasks_max = 256;
    fixture->fake.observed.runtime_max_usec = 60ull * 60 * 1000000;
    fixture->fake.observed.timeout_stop_usec = 10ull * 1000000;
    fixture->fake.observed.send_sigkill = true;
    fixture->fake.observed.no_new_privileges = true;
    fixture->fake.observed.private_tmp = true;
    fixture->fake.observed.private_devices = true;
    fixture->fake.observed.private_network = true;
    fixture->fake.observed.protect_home = true;
    fixture->fake.observed.protect_system = true;
    fixture->fake.observed.protect_proc = true;
    fixture->fake.observed.restrict_suidsgid = true;
    fixture->fake.observed.protect_control_groups = true;
    fixture->fake.observed.protect_kernel_tunables = true;
    fixture->fake.observed.protect_kernel_modules = true;
    fixture->fake.observed.protect_kernel_logs = true;
    fixture->fake.observed.protect_clock = true;
    fixture->fake.observed.protect_hostname = true;
    fixture->fake.observed.lock_personality = true;
    fixture->fake.observed.memory_deny_write_execute = true;
    fixture->fake.observed.remove_ipc = true;
    fixture->fake.observed.keyring_private = true;
    fixture->fake.observed.restrict_namespaces = true;
    fixture->fake.observed.restrict_realtime = true;
    fixture->fake.observed.address_families_unix = true;
    fixture->fake.observed.syscall_architectures_native = true;
    fixture->fake.observed.syscall_filter_system_service = true;
    fixture->fake.observed.syscall_error_number_eperm = true;
    fixture->fake.observed.security_properties_valid = true;
    fixture->fake.observed.paths_valid = true;
    u32 inaccessible_root_length = (u32)strlen(fixture->material.workspaces);
    u32 lease_length = (u32)strlen(fixture->lease);
    if (inaccessible_root_length + 1 + lease_length < sizeof(fixture->fake.observed.inaccessible_paths))
    {
        memcpy(fixture->fake.observed.inaccessible_paths, fixture->material.workspaces, inaccessible_root_length);
        fixture->fake.observed.inaccessible_paths[inaccessible_root_length] = ' ';
        memcpy(fixture->fake.observed.inaccessible_paths + inaccessible_root_length + 1, fixture->lease, lease_length);
        fixture->fake.observed.inaccessible_paths[inaccessible_root_length + 1 + lease_length] = 0;
    }
    else
    {
        fixture->fake.observed.paths_valid = false;
    }
    snprintf(fixture->fake.observed.read_only_paths, sizeof(fixture->fake.observed.read_only_paths), "%s",
             fixture->material.installed);
    snprintf(fixture->fake.observed.read_write_paths, sizeof(fixture->fake.observed.read_write_paths), "%s",
             fixture->material.workspaces);
    snprintf(fixture->fake.observed.kill_mode, sizeof(fixture->fake.observed.kill_mode), "%s", "control-group");
    snprintf(fixture->fake.observed.collect_mode, sizeof(fixture->fake.observed.collect_mode), "%s", "inactive");
    fixture->backend = (BqWorkerBackend){&fixture->fake, bq_test_worker_start, bq_test_worker_observe,
                                        bq_test_worker_signal, bq_test_worker_join,
                                        bq_test_worker_cleanup_launcher, bq_test_worker_delay,
                                        bq_test_worker_clock};
    fixture->config = (BqWorkerConfig){
        .installed_root = string_from_pointer(fixture->material.installed),
        .workspace_root = string_from_pointer(fixture->material.workspaces),
        .lease_file = string_from_pointer(fixture->lease),
        .boot_id_file = string_from_pointer(fixture->boot),
        .cgroup_root = string_from_pointer(fixture->root),
        .limits = {2, 8ull * 1024 * 1024 * 1024, 0, 256, 60ull * 60 * 1000000},
        .backend = &fixture->backend,
        .quarantine = &fixture->quarantine,
        .queue_root = string_from_pointer(fixture->root),
        .production_path = false};
    BQ_CHECK(ok);
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_end(BqWorkerFixture* fixture)
{
    if (fixture->quarantine.descriptor >= 0)
    {
        close(fixture->quarantine.descriptor);
        fixture->quarantine.descriptor = -1;
    }
    bq_test_worker_reap(&fixture->fake);
    chmod(fixture->root, 0700);
    int root = open(fixture->root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    BQ_CHECK(root >= 0 && bq_remove_workspace_payload(root));
    if (root >= 0) close(root);
    BQ_CHECK(rmdir(fixture->root) == 0);
    bq_material_test_end(&fixture->material);
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_bind(BqWorkerFixture* fixture, BqJob* job)
{
    snprintf(fixture->fake.observed.unit, sizeof(fixture->fake.observed.unit), "%s", fixture->unit);
    snprintf(fixture->fake.observed.cgroup, sizeof(fixture->fake.observed.cgroup),
             "/buster.slice/buster-bench.slice/%s", fixture->unit);
    fixture->fake.observed.unit_found = true;
    fixture->fake.observed.active = true;
    fixture->fake.observed.populated = true;
    fixture->fake.observed.result = BQ_WORKER_RUNNING;
    return bq_worker_record_write(&fixture->material.queue.queue, job,
                                  "12345678-1234-1234-1234-123456789abc", fixture->unit) == BQ_OK &&
           bq_worker_observed(&fixture->config, fixture->fake.observed.boot_id, fixture->unit,
                              &fixture->fake.observed, false) &&
           bq_worker_instance_write(&fixture->material.queue.queue, job, &fixture->fake.observed) == BQ_OK;
}

BUSTER_GLOBAL_LOCAL bool bq_test_worker_make_success_result(BqWorkerFixture* fixture, BqJob* job,
                                                             BqWorkerFinalization* finalization)
{
    char report[BQ_PATH_CAP + 64], throughput_root[BQ_PATH_CAP + 64], throughput[BQ_PATH_CAP + 96], bundle[BQ_PATH_CAP + 64], manifest[BQ_PATH_CAP + 64];
    char payload_digest[SHA256_HEX_CAPACITY], throughput_digest[SHA256_HEX_CAPACITY], bundle_digest[SHA256_HEX_CAPACITY];
    char workspace_name[64], bundle_body[768], manifest_body[4096];
    char const payload[] = "data\n";
    char const throughput_payload[] = "fixed\n";
    bool ok = fixture && job && finalization && bq_worker_result_open(&fixture->config, job, finalization, true) == BQ_OK;
    int bundle_length = -1, manifest_length = -1;
    if (ok)
    {
        snprintf(report, sizeof(report), "%s/report.txt", finalization->result_root);
        snprintf(throughput_root, sizeof(throughput_root), "%s/throughput", finalization->result_root);
        snprintf(throughput, sizeof(throughput), "%s/throughput/result.txt", finalization->result_root);
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization->result_root);
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization->result_root);
        ok = bq_workspace_name(workspace_name, job->id, job->token);
    }
    if (ok)
    {
        bq_digest(payload, sizeof(payload) - 1, payload_digest);
        bq_digest(throughput_payload, sizeof(throughput_payload) - 1, throughput_digest);
        bundle_length = snprintf(bundle_body, sizeof(bundle_body),
                                 "BQ-BUNDLE-V1\nentries=2\nbytes=11\n%.64s 5 report.txt\n%.64s 6 throughput/result.txt\n",
                                 payload_digest, throughput_digest);
        ok = bundle_length > 0 && (u32)bundle_length < sizeof(bundle_body);
        if (ok) bq_digest(bundle_body, (u32)bundle_length, bundle_digest);
    }
    if (ok)
    {
        String8 base = bq_field(&job->request, 3), candidate = bq_field(&job->request, 4);
        manifest_length = snprintf(
            manifest_body, sizeof(manifest_body),
            "schema=1\nrecipe=validate-buster-v1\nstatus=succeeded\nstage=throughput\nprocess-result=success\n"
            "job-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nworkspace-root=%.192s\nresult-root=%s\n"
            "base-revision=%.64s\ncandidate-revision=%.64s\n"
            "driver=/usr/local/libexec/buster-bench-build\nthroughput=/usr/local/libexec/buster-bench-throughput\n"
            "trusted-source-scope=operator-installed-read-only\nnamespace-policy=private-workspace-post-run-identity\n"
            "base-binary=%.192s/%s/base/build/Release/ide\n"
            "candidate-binary=%.192s/%s/candidate/build/Release/ide\n"
            "base-binary-sha256=%064d\ncandidate-binary-sha256=%064d\nbundle-sha256=%.64s\n",
            (uint64_t)job->id, (uint64_t)job->token, fixture->config.workspace_root.pointer,
            finalization->result_root, base.pointer, candidate.pointer, fixture->config.workspace_root.pointer,
            workspace_name, fixture->config.workspace_root.pointer, workspace_name, 0, 0, bundle_digest);
        ok = manifest_length > 0 && (u32)manifest_length < sizeof(manifest_body);
    }
    if (ok)
        ok = mkdir(throughput_root, 0700) == 0 && bq_test_write_path(report, payload, 0400) &&
             bq_test_write_path(throughput, throughput_payload, 0400) &&
             bq_test_write_path(bundle, bundle_body, 0400) &&
             bq_test_write_path(manifest, manifest_body, 0400) &&
             bq_worker_result_validate(&fixture->config, job, finalization) == BQ_OK;
    return ok;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_success_and_tree_cleanup(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqRequest request = bq_test_real_request(40);
        u64 id = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK && id == 1);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 !fixture.material.queue.queue.state.active_id && !fixture.material.queue.queue.needs_reconciliation);
        BQ_CHECK(fixture.fake.argv_valid && fixture.fake.starts == 1 && fixture.fake.continues == 1 &&
                 fixture.fake.terms == 1 && fixture.fake.kills == 1 && fixture.fake.delays == 101 &&
                 fixture.fake.detached == 0);
        BQ_CHECK(!bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_drained_unit(void)
{
    BqWorkerFixture fixture;
    struct { BqWorkerResult result; BqError reason; } cases[] = {
        {BQ_WORKER_SUCCEEDED, BQ_NOT_FOUND},
        {BQ_WORKER_EXECUTION_FAILED, BQ_WORKER_FAILED},
        {BQ_WORKER_OOM, BQ_WORKER_OOM_FAILURE},
        {BQ_WORKER_TIMED_OUT, BQ_WORKER_TIMEOUT}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        for (u32 collected = 0; collected < 2; collected += 1)
        {
            if (bq_test_worker_begin(&fixture, cases[index].result, false))
            {
                BqRequest request = bq_test_real_request(146 + index * 2 + collected);
                u64 id = 0;
                fixture.fake.drain_on_join = true;
                fixture.fake.collect_on_join = collected != 0;
                /* A failed outer unit must be retained. Losing its manager
                 * result is an identity/evidence failure, not an exit-code verdict. */
                bool lost_result = collected && cases[index].result != BQ_WORKER_SUCCEEDED;
                BqError expected_error = lost_result ? BQ_WORKER_MISMATCH : BQ_OK;
                BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                         bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == expected_error);
                BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
                BqError expected_reason = lost_result ? BQ_WORKER_MISMATCH : cases[index].reason;
                BQ_CHECK(job && bq_failure_evidence(&fixture.material.queue.queue, job) == expected_reason);
                if (lost_result)
                {
                    BQ_CHECK(job && job->phase != BQ_FINISHED &&
                             fixture.material.queue.queue.needs_reconciliation &&
                             fixture.material.queue.queue.state.active_id == id);
                }
                else
                {
                    BqOutcome expected = cases[index].result == BQ_WORKER_SUCCEEDED ? BQ_SUCCEEDED : BQ_FAILED;
                    BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == expected &&
                             fixture.fake.joins >= 2 && !fixture.fake.observed.cgroup[0] &&
                             fixture.fake.observed.unit_found == (collected == 0));
                }
                /* Check the durable reason after replay, not just the fake's result. */
                bq_close(&fixture.material.queue.queue);
                BQ_CHECK(bq_open(&fixture.material.queue.queue, fixture.material.queue.path) == BQ_OK);
                job = bq_job(&fixture.material.queue.queue.state, id);
                BQ_CHECK(job && bq_failure_evidence(&fixture.material.queue.queue, job) == expected_reason);
                bq_test_worker_end(&fixture);
            }
        }
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(149);
        u64 id = 0;
        fixture.fake.collect_on_join = true;
        fixture.fake.cgroup_release_after = 3;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                 bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 fixture.fake.cgroup_release_polls == 3 && fixture.fake.kills == 0);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqWorkerObserved identity = fixture.fake.observed;
        snprintf(identity.unit, sizeof(identity.unit), "%s", fixture.unit);
        snprintf(identity.cgroup, sizeof(identity.cgroup), "/buster.slice/buster-bench.slice/%s", fixture.unit);
        identity.unit_found = true;
        identity.active = true;
        identity.populated = true;
        BQ_CHECK(bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &identity, false));
        BqWorkerObserved wrong_policy = identity;
        snprintf(wrong_policy.collect_mode, sizeof(wrong_policy.collect_mode), "%s", "inactive-or-failed");
        BQ_CHECK(!bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &wrong_policy, false));
        wrong_policy.collect_mode[0] = 0;
        BQ_CHECK(!bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &wrong_policy, false));
        BqWorkerObserved drained = identity;
        drained.active = false;
        drained.populated = false;
        drained.result = BQ_WORKER_SUCCEEDED;
        drained.cgroup[0] = 0;
        BQ_CHECK(bq_test_worker_remove_cgroup(fixture.root, fixture.unit) &&
                 bq_worker_instance_matches(&fixture.config, &identity, &drained));
        wrong_policy = drained;
        snprintf(wrong_policy.collect_mode, sizeof(wrong_policy.collect_mode), "%s", "inactive-or-failed");
        BQ_CHECK(!bq_worker_instance_matches(&fixture.config, &identity, &wrong_policy));
        snprintf(drained.invocation_id, sizeof(drained.invocation_id), "%s",
                 "ffffffffffffffffffffffffffffffff");
        BQ_CHECK(!bq_worker_instance_matches(&fixture.config, &identity, &drained));
        snprintf(drained.invocation_id, sizeof(drained.invocation_id), "%s", identity.invocation_id);
        BQ_CHECK(bq_test_worker_limits(fixture.root, fixture.unit) &&
                 !bq_worker_instance_matches(&fixture.config, &identity, &drained));
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_term_grace(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqRequest request = bq_test_real_request(41);
        u64 id = 0;
        fixture.fake.term_clear_after = 3;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                 bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        BQ_CHECK(fixture.fake.terms == 1 && fixture.fake.term_polls == 3 &&
                 fixture.fake.delays == 3 && fixture.fake.kills == 0 && fixture.fake.detached == 0);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_child_survives_parent_kill(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqRequest request = bq_test_real_request(141);
        u64 id = 0;
        fixture.fake.child = fixture.fake.observed;
        snprintf(fixture.fake.child.unit, sizeof(fixture.fake.child.unit),
                 "buster-bench-1-2-throughput.service");
        snprintf(fixture.fake.child.cgroup, sizeof(fixture.fake.child.cgroup),
                 "/buster.slice/buster-bench.slice/%s", fixture.fake.child.unit);
        snprintf(fixture.fake.child.invocation_id, sizeof(fixture.fake.child.invocation_id),
                 "%s", "fedcba9876543210fedcba9876543210");
        snprintf(fixture.fake.child.part_of, sizeof(fixture.fake.child.part_of), "%s", fixture.unit);
        snprintf(fixture.fake.child.binds_to, sizeof(fixture.fake.child.binds_to), "%s", fixture.unit);
        snprintf(fixture.fake.child.after, sizeof(fixture.fake.child.after),
                 "network.target %s basic.target", fixture.unit);
        snprintf(fixture.fake.child.collect_mode, sizeof(fixture.fake.child.collect_mode),
                 "%s", "inactive-or-failed");
        fixture.fake.child.unit_found = true;
        fixture.fake.child.active = true;
        fixture.fake.child.populated = true;
        fixture.fake.child.result = BQ_WORKER_RUNNING;
        fixture.fake.child_kill_clears = true;
        fixture.fake.child_collect_after = 3;
        BQ_CHECK(bq_test_worker_limits(fixture.root, fixture.fake.child.unit));
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                 bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 fixture.fake.terms == 1 && fixture.fake.kills == 1 &&
                 fixture.fake.child_terms == 1 && fixture.fake.child_kills == 1 &&
                 fixture.fake.child_collect_polls == 3 && fixture.fake.child_observes > 1 &&
                 !fixture.fake.child.unit_found &&
                 !fixture.fake.child.populated && !bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(150);
        u64 id = 0;
        fixture.fake.child = fixture.fake.observed;
        snprintf(fixture.fake.child.unit, sizeof(fixture.fake.child.unit),
                 "buster-bench-1-2-throughput.service");
        snprintf(fixture.fake.child.cgroup, sizeof(fixture.fake.child.cgroup),
                 "/buster.slice/buster-bench.slice/%s", fixture.fake.child.unit);
        snprintf(fixture.fake.child.invocation_id, sizeof(fixture.fake.child.invocation_id),
                 "%s", "fedcba9876543210fedcba9876543210");
        snprintf(fixture.fake.child.part_of, sizeof(fixture.fake.child.part_of), "%s", fixture.unit);
        snprintf(fixture.fake.child.binds_to, sizeof(fixture.fake.child.binds_to), "%s", fixture.unit);
        snprintf(fixture.fake.child.after, sizeof(fixture.fake.child.after), "%s", fixture.unit);
        snprintf(fixture.fake.child.collect_mode, sizeof(fixture.fake.child.collect_mode),
                 "%s", "inactive-or-failed");
        fixture.fake.child.unit_found = true;
        fixture.fake.child.active = false;
        fixture.fake.child.populated = false;
        fixture.fake.child.result = BQ_WORKER_SUCCEEDED;
        fixture.fake.child_collect_after = 3;
        fixture.fake.child_collect_without_kill = true;
        BQ_CHECK(bq_test_worker_limits(fixture.root, fixture.fake.child.unit) &&
                 bq_test_worker_remove_cgroup(fixture.root, fixture.fake.child.unit));
        fixture.fake.child.cgroup[0] = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                 bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 fixture.fake.child_collect_polls == 3 && fixture.fake.child_kills == 0 &&
                 !fixture.fake.child.unit_found);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_late_cancel(void)
{
    struct sigaction action = {0}, prior = {0};
    action.sa_handler = bq_worker_cancel_handler;
    sigemptyset(&action.sa_mask);
    bool installed = sigaction(SIGTERM, &action, &prior) == 0;
    BQ_CHECK(installed);
    BqWorkerFixture fixture;
    if (installed && bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(46);
        u64 id = 0;
        fixture.fake.cancel_after_observe = 4;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                 bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED &&
                 bq_failure_evidence(&fixture.material.queue.queue, job) == BQ_NOT_FOUND &&
                 !fixture.material.queue.queue.state.active_id && fixture.fake.observes >= 4 &&
                 bq_worker_cancel_signal == 0 && !bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
    if (installed) BQ_CHECK(sigaction(SIGTERM, &prior, NULL) == 0);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_cancel_during_finalization(void)
{
    u32 checkpoints[] = {3, 5};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(checkpoints); index += 1)
    {
        struct sigaction action = {0}, prior = {0};
        action.sa_handler = bq_worker_cancel_handler;
        sigemptyset(&action.sa_mask);
        bool installed = sigaction(SIGTERM, &action, &prior) == 0;
        BQ_CHECK(installed);
        BqWorkerFixture fixture;
        if (installed && bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
        {
            BqRequest request = bq_test_real_request(90 + index);
            u64 id = 0;
            bq_worker_test_cancel_during_finish = checkpoints[index];
            BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK &&
                     bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
            BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED &&
                     job->cancel_requested && bq_failure_evidence(&fixture.material.queue.queue, job) == BQ_NOT_FOUND &&
                     !fixture.material.queue.queue.state.active_id && !fixture.material.queue.queue.needs_reconciliation &&
                     bq_worker_test_finish_checkpoints >= checkpoints[index] &&
                     bq_worker_cancel_signal == 0 && !bq_test_worker_probe_locked(fixture.lease));
            bq_close(&fixture.material.queue.queue);
            BQ_CHECK(bq_open(&fixture.material.queue.queue, fixture.material.queue.path) == BQ_OK);
            job = bq_job(&fixture.material.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED &&
                     job->cancel_requested && !fixture.material.queue.queue.state.active_id &&
                     !fixture.material.queue.queue.needs_reconciliation);
            bq_test_worker_end(&fixture);
        }
        if (installed) BQ_CHECK(sigaction(SIGTERM, &prior, NULL) == 0);
    }
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(95);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root,
                                fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_FINALIZING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome outcome = phase == BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            BQ_CHECK(job && bq_real_advance(queue, job, phase, outcome) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        BQ_CHECK(job && bq_cancel(queue, id) == BQ_OK && job->cancel_requested);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {0};
        BQ_CHECK(job && job->phase == BQ_FINALIZING && job->outcome == BQ_SUCCEEDED &&
                 job->cancel_requested && bq_worker_finish(queue, &fixture.config, job,
                                                           BQ_SUCCEEDED, BQ_NOT_FOUND,
                                                           &finalization) == BQ_OK);
        BQ_CHECK(bq_worker_finalization_restore(&finalization) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED &&
                 job->cancel_requested && !queue->state.active_id && !queue->needs_reconciliation);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_post_publication_cancel(void)
{
    struct sigaction action = {0}, prior = {0};
    action.sa_handler = bq_worker_cancel_handler;
    sigemptyset(&action.sa_mask);
    bool installed = sigaction(SIGTERM, &action, &prior) == 0;
    BQ_CHECK(installed);
    BqWorkerFixture fixture;
    if (installed && bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(96);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        fixture.config.production_path = true;
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        bool artifact = job && bq_test_worker_make_success_result(&fixture, job, &finalization);
        bq_worker_test_finish_checkpoints = 0;
        bq_worker_test_cancel_during_finish = 5;
        BqError finished = artifact ? bq_worker_finish(queue, &fixture.config, job, BQ_SUCCEEDED, BQ_NOT_FOUND,
                                                        &finalization) : BQ_IO;
        bq_worker_test_cancel_during_finish = 0;
        BQ_CHECK(bq_worker_finalization_restore(&finalization) == BQ_OK && finished == BQ_OK);
        job = bq_job(&queue->state, id);
        char outcome[BQ_PATH_CAP + 64];
        char retained_throughput[BQ_PATH_CAP + 96];
        snprintf(outcome, sizeof(outcome), "%s/validate-buster-v1.outcome", finalization.result_root);
        snprintf(retained_throughput, sizeof(retained_throughput), "%s/throughput/result.txt", finalization.result_root);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED && job->cancel_requested &&
                 job->result_bound && bq_worker_result_binding_validate(job) == BQ_OK &&
                 access(retained_throughput, F_OK) == 0);
        int outcome_descriptor = open(outcome, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char outcome_body[512] = {0};
        ssize_t outcome_length = outcome_descriptor >= 0 ? read(outcome_descriptor, outcome_body,
                                                                 sizeof(outcome_body) - 1) : -1;
        if (outcome_descriptor >= 0) close(outcome_descriptor);
        BQ_CHECK(outcome_length > 0 && strstr(outcome_body, "status=cancelled") != NULL);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_CANCELLED && job->result_bound &&
                 bq_worker_result_binding_validate(job) == BQ_OK && access(retained_throughput, F_OK) == 0);
        bq_test_worker_end(&fixture);
    }
    if (installed) BQ_CHECK(sigaction(SIGTERM, &prior, NULL) == 0);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_prelaunch_cancel(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(45);
        u64 submitted = 0, id = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &submitted) == BQ_OK);
        bq_worker_cancel_signal = 1;
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_CANCEL_SIGNAL);
        BQ_CHECK(id == 0 && fixture.fake.starts == 0 && fixture.material.queue.queue.state.active_id == 0 &&
                 bq_job(&fixture.material.queue.queue.state, submitted)->phase == BQ_QUEUED &&
                 fixture.quarantine.descriptor < 0 && bq_worker_cancel_signal == 0);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_observe_error_retains_authority(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(42);
        u64 id = 0;
        fixture.fake.observe_failures = UINT32_MAX;
        fixture.fake.skip_inherited_lease = true;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_CLEANUP_FAILED);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id &&
                 fixture.material.queue.queue.needs_reconciliation && fixture.fake.inherited_lease < 0 &&
                 fixture.fake.terms == 0 && fixture.fake.kills == 0 &&
                 fixture.quarantine.descriptor >= 0 && bq_test_worker_probe_locked(fixture.lease));
        int quarantine = fixture.quarantine.descriptor;
        char foreign_lease[512];
        snprintf(foreign_lease, sizeof(foreign_lease), "%s/foreign.lock", fixture.root);
        BqWorkerConfig foreign = fixture.config;
        foreign.lease_file = string_from_pointer(foreign_lease);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &foreign, &id) == BQ_BUSY &&
                 fixture.quarantine.descriptor == quarantine && bq_test_worker_probe_locked(fixture.lease));
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH &&
                 fixture.quarantine.descriptor >= 0 && bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_reap(&fixture.fake);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqRequest request = bq_test_real_request(44);
        u64 id = 0;
        fixture.fake.start_error = true;
        fixture.fake.skip_inherited_lease = true;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_IO);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id &&
                 fixture.material.queue.queue.needs_reconciliation && fixture.fake.detached > 0 &&
                 fixture.fake.inherited_lease < 0 && fixture.quarantine.descriptor >= 0 &&
                 fixture.fake.terms == 0 && fixture.fake.kills == 0 &&
                 bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_reap(&fixture.fake);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_unbound_launcher_cleanup(void)
{
    for (u32 failure = 0; failure < 2; failure += 1)
    {
        BqWorkerFixture fixture;
        if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
        {
            BqRequest request = bq_test_real_request(92 + failure);
            u64 id = 0;
            fixture.fake.observe_failures = UINT32_MAX;
            fixture.fake.skip_inherited_lease = true;
            fixture.fake.spawn_launcher = true;
            fixture.fake.launcher_cleanup_failure = failure != 0;
            BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_CLEANUP_FAILED);
            BQ_CHECK(fixture.fake.launcher_cleanups == 1 &&
                     (failure ? fixture.fake.launcher > 0 : fixture.fake.launcher == 0) &&
                     fixture.material.queue.queue.state.active_id == id &&
                     fixture.material.queue.queue.needs_reconciliation &&
                     fixture.quarantine.descriptor >= 0 && bq_test_worker_probe_locked(fixture.lease));
            BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
            BQ_CHECK(job && bq_failure_evidence(&fixture.material.queue.queue, job) == BQ_CLEANUP_FAILED);
            bq_test_worker_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_instance_reuse(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, true))
    {
        BqRequest request = bq_test_real_request(43);
        u64 id = 0;
        fixture.fake.reuse_after_observe = 2;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id &&
                 fixture.material.queue.queue.needs_reconciliation && fixture.fake.continues == 0 &&
                 fixture.fake.terms == 0 && fixture.fake.kills == 0 &&
                 bq_test_worker_probe_locked(fixture.lease));
        BqWorkerObserved identity = {0};
        BQ_CHECK(bq_worker_instance_read(&fixture.material.queue.queue,
                                         bq_job(&fixture.material.queue.queue.state, id), &identity) == BQ_OK &&
                 !strcmp(identity.invocation_id, "0123456789abcdef0123456789abcdef"));
        bq_test_worker_reap(&fixture.fake);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqWorkerObserved identity = fixture.fake.observed;
        snprintf(identity.unit, sizeof(identity.unit), "%s", fixture.unit);
        snprintf(identity.cgroup, sizeof(identity.cgroup), "/buster.slice/buster-bench.slice/%s", fixture.unit);
        identity.unit_found = true;
        identity.active = true;
        identity.populated = true;
        BQ_CHECK(bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &identity, false));
        char leaf[1024], old[1024];
        snprintf(leaf, sizeof(leaf), "%s/buster.slice/buster-bench.slice/%s", fixture.root, fixture.unit);
        snprintf(old, sizeof(old), "%s/buster.slice/buster-bench.slice/replaced.scope", fixture.root);
        BQ_CHECK(rename(leaf, old) == 0 && bq_test_worker_limits(fixture.root, fixture.unit));
        BqWorkerObserved replacement = identity;
        replacement.cgroup_device = 0;
        replacement.cgroup_inode = 0;
        BQ_CHECK(!bq_worker_instance_matches(&fixture.config, &identity, &replacement));
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqWorkerObserved identity = fixture.fake.observed;
        snprintf(identity.unit, sizeof(identity.unit), "%s", fixture.unit);
        snprintf(identity.cgroup, sizeof(identity.cgroup), "/buster.slice/buster-bench.slice/%s", fixture.unit);
        identity.unit_found = true;
        BQ_CHECK(bq_worker_observed(&fixture.config, identity.boot_id, identity.unit, &identity, false));
        char slice[768], old[768];
        snprintf(slice, sizeof(slice), "%s/buster.slice/buster-bench.slice", fixture.root);
        snprintf(old, sizeof(old), "%s/foreign.slice", fixture.root);
        BQ_CHECK(bq_test_worker_remove_cgroup(fixture.root, fixture.unit) &&
                 rename(slice, old) == 0 && mkdir(slice, 0700) == 0 &&
                 !bq_worker_cgroup_absent(&fixture.config, &identity));
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(47);
        u64 id = 0;
        fixture.fake.replace_slice_on_cleanup_join = true;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id &&
                 fixture.material.queue.queue.needs_reconciliation && fixture.fake.joins >= 2 &&
                 fixture.quarantine.descriptor >= 0 && bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_cgroup_paths(void)
{
    char const* invalid[] = {"/buster.slice/buster-bench.slice/../foreign.scope", "/buster.slice/buster-bench.slice/./x.scope",
                             "/buster.slice/buster-bench.slice//x.scope", "/buster.slice/buster-bench.slice/x.scope/",
                             "/buster.slice/buster-bench.slice/x.scope\nforeign"};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(invalid); index += 1)
        BQ_CHECK(!bq_worker_cgroup_path_valid(invalid[index]));
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        char slice[1024], real[1024];
        snprintf(slice, sizeof(slice), "%s/buster.slice/buster-bench.slice", fixture.root);
        snprintf(real, sizeof(real), "%s/real.slice", fixture.root);
        BQ_CHECK(rename(slice, real) == 0 && symlink("real.slice", slice) == 0);
        BqWorkerObserved observed = fixture.fake.observed;
        snprintf(observed.unit, sizeof(observed.unit), "%s", fixture.unit);
        snprintf(observed.cgroup, sizeof(observed.cgroup), "/buster.slice/buster-bench.slice/%s", fixture.unit);
        observed.unit_found = true;
        BQ_CHECK(!bq_worker_observed(&fixture.config, observed.boot_id, observed.unit, &observed, false));
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        char link[512], linked_lease[768], dotdot_lease[768], basename[256];
        char open_parent[512], secure[768], nested[1024];
        snprintf(link, sizeof(link), "%s-link", fixture.root);
        snprintf(linked_lease, sizeof(linked_lease), "%s/host.lock", link);
        char const* slash = strrchr(fixture.root, '/');
        snprintf(basename, sizeof(basename), "%s", slash ? slash + 1 : "missing");
        int prefix = slash ? (int)(slash - fixture.root) : 0;
        snprintf(dotdot_lease, sizeof(dotdot_lease), "%.*s/%s/../%s/other.lock",
                 prefix, fixture.root, basename, basename);
        snprintf(open_parent, sizeof(open_parent), "%s/open", fixture.root);
        snprintf(secure, sizeof(secure), "%s/secure", open_parent);
        snprintf(nested, sizeof(nested), "%s/host.lock", secure);
        BqWorkerLease lease = {.descriptor = -1};
        BQ_CHECK(symlink(fixture.root, link) == 0 && bq_worker_lease_acquire(linked_lease, &lease) != 0 &&
                 bq_worker_lease_acquire(dotdot_lease, &lease) != 0);
        BQ_CHECK(mkdir(open_parent, 0777) == 0 && chmod(open_parent, 0777) == 0 && mkdir(secure, 0700) == 0 &&
                 bq_worker_lease_acquire(nested, &lease) != 0);
        BqWorkerConfig linked = fixture.config;
        linked.cgroup_root = string_from_pointer(link);
        BqWorkerObserved observed = fixture.fake.observed;
        snprintf(observed.unit, sizeof(observed.unit), "%s", fixture.unit);
        snprintf(observed.cgroup, sizeof(observed.cgroup), "/buster.slice/buster-bench.slice/%s", fixture.unit);
        observed.unit_found = true;
        BQ_CHECK(!bq_worker_observed(&linked, observed.boot_id, observed.unit, &observed, false));
        char dotdot_root[768];
        snprintf(dotdot_root, sizeof(dotdot_root), "%.*s/%s/../%s",
                 prefix, fixture.root, basename, basename);
        BqWorkerConfig dotted = fixture.config;
        dotted.cgroup_root = string_from_pointer(dotdot_root);
        BQ_CHECK(!bq_worker_observed(&dotted, observed.boot_id, observed.unit, &observed, false));
        BQ_CHECK(chmod(fixture.root, 0777) == 0 &&
                 bq_worker_lease_acquire(fixture.lease, &lease) != 0 &&
                 !bq_worker_observed(&fixture.config, observed.boot_id, observed.unit, &observed, false));
        char slice[768];
        snprintf(slice, sizeof(slice), "%s/buster.slice/buster-bench.slice", fixture.root);
        BQ_CHECK(chmod(fixture.root, 01777) == 0 &&
                 !bq_worker_observed(&fixture.config, observed.boot_id, observed.unit, &observed, false));
        BQ_CHECK(chmod(fixture.root, 0700) == 0 && chmod(slice, 0777) == 0 &&
                 !bq_worker_observed(&fixture.config, observed.boot_id, observed.unit, &observed, false));
        BQ_CHECK(chmod(slice, 01777) == 0 &&
                 !bq_worker_observed(&fixture.config, observed.boot_id, observed.unit, &observed, false));
        BQ_CHECK(chmod(slice, 0700) == 0 && unlink(link) == 0);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_deploy_policy(void)
{
    char service[8192];
    bool read = bq_worker_read_regular("tools/bench_service/deploy/buster-bench.service",
                                       service, sizeof(service));
    char const* required[] = {"\nUser=buster-bench\n", "\nGroup=buster-bench\n",
                              "\nCapabilityBoundingSet=\n", "\nAmbientCapabilities=\n",
                              "PrivateDevices=yes", "ProtectControlGroups=yes",
                              "ProtectKernelTunables=yes", "ProtectKernelModules=yes", "ProtectKernelLogs=yes",
                              "ProtectClock=yes", "ProtectHostname=yes", "RestrictNamespaces=yes",
                              "PrivateNetwork=yes", "RestrictAddressFamilies=AF_UNIX",
                              "SystemCallFilter=@system-service"};
    BQ_CHECK(read);
    for (u32 index = 0; read && index < BUSTER_ARRAY_LENGTH(required); index += 1)
        BQ_CHECK(strstr(service, required[index]) != NULL);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_systemd_results(void)
{
    char collected[] = "LoadState=not-found\nActiveState=inactive\nControlGroup=\n";
    char loaded[] = "LoadState=loaded\nActiveState=inactive\nControlGroup=\n";
    BQ_CHECK(bq_worker_systemd_collected(collected) && !bq_worker_systemd_collected(loaded));
    BQ_CHECK(bq_worker_systemd_result("success", true) == BQ_WORKER_RUNNING);
    BQ_CHECK(bq_worker_systemd_result("success", false) == BQ_WORKER_SUCCEEDED);
    BQ_CHECK(bq_worker_systemd_result("oom-kill", true) == BQ_WORKER_OOM);
    BQ_CHECK(bq_worker_systemd_result("timeout", false) == BQ_WORKER_TIMED_OUT);
    BQ_CHECK(bq_worker_systemd_result("canceled", false) == BQ_WORKER_CANCELLED_RESULT);
    BQ_CHECK(bq_worker_systemd_result("exit-code", true) == BQ_WORKER_EXECUTION_FAILED);
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_outcomes(void)
{
    struct { BqWorkerResult result; BqOutcome outcome; BqError reason; bool cancel; } cases[] = {
        {BQ_WORKER_EXECUTION_FAILED, BQ_FAILED, BQ_WORKER_FAILED, false},
        {BQ_WORKER_OOM, BQ_FAILED, BQ_WORKER_OOM_FAILURE, false},
        {BQ_WORKER_TIMED_OUT, BQ_FAILED, BQ_WORKER_TIMEOUT, false},
        {BQ_WORKER_CANCELLED_RESULT, BQ_CANCELLED, BQ_NOT_FOUND, true}};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(cases); index += 1)
    {
        BqWorkerFixture fixture;
        if (bq_test_worker_begin(&fixture, cases[index].result, false))
        {
            BqRequest request = bq_test_real_request(50 + index);
            u64 id = 0;
            fixture.fake.cancel_on_join = cases[index].cancel;
            BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
            BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
            BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
            BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == cases[index].outcome);
            BQ_CHECK(bq_failure_evidence(&fixture.material.queue.queue, job) == cases[index].reason);
            u8 status_body[8];
            BqPacket status, response;
            bq_put64(status_body, id);
            bq_packet(&status, BQ_OP_RESULT, 500 + index, status_body, sizeof(status_body));
            BQ_CHECK(bq_dispatch(&fixture.material.queue.queue, status.bytes, status.size, &response) == BQ_OK &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 32) == cases[index].outcome &&
                     bq_u32(response.bytes + BQ_CONTROL_HEADER + 120) ==
                     (cases[index].reason == BQ_NOT_FOUND ? 0 : (u32)cases[index].reason));
            bq_test_worker_end(&fixture);
        }
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_quarantine_and_recovery(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest first = bq_test_real_request(60), second = bq_test_real_request(61);
        u64 id = 0, second_id = 0;
        fixture.fake.mismatch_resources = true;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &first, &id) == BQ_OK &&
                 bq_submit(&fixture.material.queue.queue, &second, &second_id) == BQ_OK);
        BQ_CHECK(bq_test_worker_limits(fixture.root, "buster-bench-1-3.service"));
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_RESOURCE_MISMATCH);
        BQ_CHECK(!fixture.material.queue.queue.needs_reconciliation && !fixture.material.queue.queue.state.active_id &&
                 bq_job(&fixture.material.queue.queue.state, id)->outcome == BQ_FAILED &&
                 bq_job(&fixture.material.queue.queue.state, second_id)->phase == BQ_QUEUED && fixture.fake.starts == 1);
        BQ_CHECK(bq_failure_evidence(&fixture.material.queue.queue,
                 bq_job(&fixture.material.queue.queue.state, id)) == BQ_RESOURCE_MISMATCH);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest first = bq_test_real_request(64), second = bq_test_real_request(65);
        u64 id = 0, second_id = 0;
        fixture.fake.mismatch_unit = true;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &first, &id) == BQ_OK &&
                 bq_submit(&fixture.material.queue.queue, &second, &second_id) == BQ_OK);
        BQ_CHECK(bq_test_worker_limits(fixture.root, "buster-bench-1-3.service"));
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH);
        BQ_CHECK(fixture.material.queue.queue.needs_reconciliation && fixture.material.queue.queue.state.active_id == id &&
                 bq_job(&fixture.material.queue.queue.state, second_id)->phase == BQ_QUEUED && fixture.fake.starts == 1);
        bq_close(&fixture.material.queue.queue);
        BQ_CHECK(bq_open(&fixture.material.queue.queue, fixture.material.queue.path) == BQ_OK &&
                 fixture.material.queue.queue.needs_reconciliation);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH && fixture.fake.starts == 1);
        fixture.fake.mismatch_unit = false;
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_WORKER_MISMATCH &&
                 bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_reap(&fixture.fake);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id &&
                 fixture.material.queue.queue.needs_reconciliation);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, true))
    {
        BqRequest first = bq_test_real_request(62), second = bq_test_real_request(63);
        u64 id = 0, second_id = 0;
        fixture.fake.kill_clears = false;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &first, &id) == BQ_OK &&
                 bq_submit(&fixture.material.queue.queue, &second, &second_id) == BQ_OK);
        BQ_CHECK(bq_test_worker_limits(fixture.root, "buster-bench-1-3.service"));
        BqError first_error = bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id);
        BQ_CHECK(first_error == BQ_CLEANUP_FAILED);
        BQ_CHECK(fixture.material.queue.queue.state.active_id == id && fixture.fake.detached > 0 &&
                 fixture.fake.starts == 1 && fixture.quarantine.descriptor >= 0 &&
                 bq_test_worker_probe_locked(fixture.lease));
        bq_close(&fixture.material.queue.queue);
        BqError reopen = bq_open(&fixture.material.queue.queue, fixture.material.queue.path);
        BQ_CHECK(reopen == BQ_OK && fixture.material.queue.queue.needs_reconciliation);
        if (reopen == BQ_OK)
            BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_CLEANUP_FAILED && fixture.fake.starts == 1);
        BQ_CHECK(bq_job(&fixture.material.queue.queue.state, second_id)->phase == BQ_QUEUED);
        fixture.fake.kill_clears = true;
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK &&
                 fixture.fake.detached == 0 && fixture.quarantine.descriptor < 0 &&
                 !bq_test_worker_probe_locked(fixture.lease));
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_ancestor_budget(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        char path[768];
        snprintf(path, sizeof(path), "%s/buster.slice/buster-bench.slice/memory.max", fixture.root);
        BQ_CHECK(chmod(path, 0600) == 0);
        int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        char const tighter[] = "4096\n";
        BQ_CHECK(fd >= 0 && bq_write_all(fd, (u8 const*)tighter, sizeof(tighter) - 1) && fsync(fd) == 0);
        if (fd >= 0) close(fd);
        BqRequest request = bq_test_real_request(66);
        u64 id = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &id) == BQ_OK);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_RESOURCE_MISMATCH);
        BqJob* job = bq_job(&fixture.material.queue.queue.state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_FAILED &&
                 bq_failure_evidence(&fixture.material.queue.queue, job) == BQ_RESOURCE_MISMATCH);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_boot_and_identity_recovery(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(70);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(bq_test_worker_bind(&fixture, job));
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        int boot = open(fixture.boot, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        char const changed[] = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n";
        BQ_CHECK(boot >= 0 && bq_write_all(boot, (u8 const*)changed, sizeof(changed) - 1) && fsync(boot) == 0);
        if (boot >= 0) close(boot);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->outcome == BQ_INTERRUPTED && bq_failure_evidence(queue, job) == BQ_BOOT_INTERRUPTED &&
                 fixture.fake.starts == 0);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(71);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_WORKER_MISMATCH && queue->state.active_id == id &&
                 fixture.fake.starts == 0);
        /* Test cleanup uses the established workspace recovery path after the
         * missing identity has correctly quarantined the worker. */
        BQ_CHECK(bq_failure_write(queue, bq_job(&queue->state, id), BQ_WORKER_MISMATCH) == BQ_OK);
        BQ_CHECK(bq_workspace_reconcile(queue, fixture.config.workspace_root, id, token) == BQ_OK);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(72);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(bq_test_worker_bind(&fixture, job));
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        fixture.fake.hide_unit = true;
        BQ_CHECK(bq_test_worker_remove_cgroup(fixture.root, fixture.unit));
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->outcome == BQ_INTERRUPTED && bq_failure_evidence(queue, job) == BQ_WORKER_INTERRUPTED &&
                 fixture.fake.starts == 0);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(73);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(bq_test_worker_bind(&fixture, job));
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome outcome = phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            BQ_CHECK(bq_real_advance(queue, job, phase, outcome) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        int boot = open(fixture.boot, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        char const changed[] = "ffffffff-eeee-dddd-cccc-bbbbbbbbbbbb\n";
        BQ_CHECK(boot >= 0 && bq_write_all(boot, (u8 const*)changed, sizeof(changed) - 1) && fsync(boot) == 0);
        if (boot >= 0) close(boot);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 bq_failure_evidence(queue, job) == BQ_NOT_FOUND);
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(74);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(bq_test_worker_bind(&fixture, job));
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome outcome = phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            BQ_CHECK(bq_real_advance(queue, job, phase, outcome) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_SUCCEEDED &&
                 bq_failure_evidence(queue, job) == BQ_NOT_FOUND);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL bool bq_test_fixed_recipe_wait(char const* marker, char const* instance, pid_t* recipe)
{
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000);
    bool ready = false;
    while (!ready && bq_worker_monotonic_milliseconds() < deadline)
    {
        int descriptor = open(marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char body[512] = {0};
        ssize_t length = descriptor >= 0 ? read(descriptor, body, sizeof(body) - 1) : -1;
        if (descriptor >= 0) close(descriptor);
        long parsed = 0;
        ready = length > 0 && sscanf(body, "pid=%ld", &parsed) == 1 && parsed > 1 && parsed <= INT_MAX &&
                strstr(body, "recipe=validate-buster-v1\n") != NULL && access(instance, F_OK) == 0;
        if (ready) *recipe = (pid_t)parsed;
        else bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10));
    }
    return ready;
}

BUSTER_GLOBAL_LOCAL bool bq_test_fixed_recipe_wait_reaped(char const* marker)
{
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 5000);
    bool reaped = access(marker, F_OK) == 0;
    while (!reaped && bq_worker_monotonic_milliseconds() < deadline)
    {
        bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 10));
        reaped = access(marker, F_OK) == 0;
    }
    return reaped;
}

BUSTER_GLOBAL_LOCAL int bq_test_fixed_recipe_helper(int argc, char** argv)
{
    u64 job = 0, token = 0;
    bool valid = argc == 10 && !strcmp(argv[1], "fixed-recipe-helper") && argv[2][0] == '/' &&
                 bq_decimal(argv[3], true, &job) && bq_decimal(argv[4], true, &token) &&
                 !strcmp(argv[5], "validate-buster-v1") && argv[6][0] == '/' &&
                 strlen(argv[7]) == 64 && strlen(argv[8]) == 64 && argv[9][0] == '/';
    char body[512];
    int length = valid ? snprintf(body, sizeof(body),
                                  "pid=%ld\nrecipe=%s\njob=%" PRIu64 "\ntoken=%" PRIu64
                                  "\nbase=%.64s\ncandidate=%.64s\nresult=%s\n",
                                  (long)getpid(), argv[5], (uint64_t)job, (uint64_t)token,
                                  argv[7], argv[8], argv[9]) : -1;
    valid = valid && length > 0 && (u32)length < sizeof(body) && bq_test_write_path(argv[2], body, 0400);
    while (valid) pause();
    int result = valid ? 0 : 1;
    return result;
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_fixed_recipe_sigkill_recovery(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(97);
        u64 id = 0;
        fixture.fake.spawn_fixed_recipe = true;
        fixture.fake.block_join = true;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK);
        pid_t coordinator = fork();
        if (!coordinator)
        {
            u64 worker_id = 0;
            BqError error = bq_worker_run(queue, &fixture.config, &worker_id);
            _exit(error == BQ_OK ? 0 : 1);
        }
        BQ_CHECK(coordinator > 0);
        if (coordinator > 0)
        {
            bq_close(queue);
            char marker[BQ_PATH_CAP + 64], reaped_marker[BQ_PATH_CAP + 64], instance[BQ_PATH_CAP + 64];
            int marker_length = snprintf(marker, sizeof(marker), "%s/fixed-recipe.running", fixture.root);
            int reaped_length = snprintf(reaped_marker, sizeof(reaped_marker), "%s/fixed-recipe.reaped", fixture.root);
            int instance_length = snprintf(instance, sizeof(instance), "%s/worker-instance-1", fixture.material.queue.path);
            pid_t recipe = 0;
            bool running = marker_length > 0 && (u32)marker_length < sizeof(marker) &&
                           instance_length > 0 && (u32)instance_length < sizeof(instance) &&
                           bq_test_fixed_recipe_wait(marker, instance, &recipe);
            BQ_CHECK(running && recipe > 1 && kill(recipe, SIGKILL) == 0);
            BQ_CHECK(reaped_length > 0 && (u32)reaped_length < sizeof(reaped_marker) &&
                     bq_test_fixed_recipe_wait_reaped(reaped_marker));
            BQ_CHECK(kill(coordinator, SIGKILL) == 0);
            int status = 0;
            while (waitpid(coordinator, &status, 0) < 0 && errno == EINTR) {}
            BQ_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
            int boot = open(fixture.boot, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
            char const changed[] = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n";
            BQ_CHECK(boot >= 0 && bq_write_all(boot, (u8 const*)changed, sizeof(changed) - 1) && fsync(boot) == 0);
            if (boot >= 0) close(boot);
            BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation &&
                     queue->state.active_id == id);
            fixture.fake.spawn_fixed_recipe = false;
            fixture.fake.block_join = false;
            BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
            BqJob* recovered = bq_job(&queue->state, id);
            BQ_CHECK(recovered && recovered->phase == BQ_FINISHED && recovered->outcome == BQ_INTERRUPTED &&
                     bq_failure_evidence(queue, recovered) == BQ_BOOT_INTERRUPTED &&
                     !queue->state.active_id && !queue->needs_reconciliation &&
                     !bq_test_worker_probe_locked(fixture.lease));
            bq_close(queue);
            BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && !queue->needs_reconciliation);
            recovered = bq_job(&queue->state, id);
            BQ_CHECK(recovered && recovered->outcome == BQ_INTERRUPTED);
        }
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_lock_precedes_materialization(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(80);
        BqWorkerLease blocker = {.descriptor = -1};
        u64 id = 0, submitted = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &submitted) == BQ_OK);
        BQ_CHECK(bq_worker_lease_acquire(fixture.lease, &blocker) == 0);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_BUSY);
        BQ_CHECK(bq_job(&fixture.material.queue.queue.state, submitted)->phase == BQ_QUEUED && fixture.fake.starts == 0);
        bq_worker_lease_release(&blocker);
        BQ_CHECK(bq_worker_run(&fixture.material.queue.queue, &fixture.config, &id) == BQ_OK);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_transport_worker_retries_after_busy(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqWorkerLease blocker = {.descriptor = -1};
        BqRequest request = bq_test_real_request(81);
        u64 submitted = 0;
        bq_transport_stop_signal = 0;
        bq_worker_cancel_signal = 0;
        bq_worker_shutdown_signal = 0;
        bq_worker_transport_stop_signal = 0;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &submitted) == BQ_OK);
        BQ_CHECK(bq_worker_lease_acquire(fixture.lease, &blocker) == 0);
        BQ_CHECK(bq_transport_worker_once(&fixture.material.queue.queue, &fixture.config) == BQ_BUSY &&
                 bq_job(&fixture.material.queue.queue.state, submitted)->phase == BQ_QUEUED && fixture.fake.starts == 0);
        bq_worker_lease_release(&blocker);
        /* This second tick is the autonomous retry; no client request is
         * involved between the busy result and successful execution. */
        BqError retried = bq_transport_worker_once(&fixture.material.queue.queue, &fixture.config);
        BQ_CHECK(retried == BQ_OK && bq_job(&fixture.material.queue.queue.state, submitted)->phase == BQ_FINISHED && fixture.fake.starts == 1);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_transport_worker_signal_handoff(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqRequest request = bq_test_real_request(82);
        u64 submitted = 0;
        bq_transport_stop_signal = 0;
        bq_worker_cancel_signal = 0;
        bq_worker_shutdown_signal = 0;
        bq_worker_transport_stop_signal = 1;
        BQ_CHECK(bq_submit(&fixture.material.queue.queue, &request, &submitted) == BQ_OK);
        BQ_CHECK(bq_transport_worker_once(&fixture.material.queue.queue, &fixture.config) == BQ_WORKER_CANCEL_SIGNAL &&
                 bq_job(&fixture.material.queue.queue.state, submitted)->phase == BQ_QUEUED && fixture.fake.starts == 0 &&
                 bq_transport_stop_signal);
        bq_worker_transport_stop_signal = 0;
        bq_worker_cancel_signal = 0;
        bq_worker_shutdown_signal = 0;
        bq_transport_stop_signal = 0;
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_result_bundle_and_evidence(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(83);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        char report[BQ_PATH_CAP + 64], bundle[BQ_PATH_CAP + 64], manifest[BQ_PATH_CAP + 64];
        snprintf(report, sizeof(report), "%s/report.txt", finalization.result_root);
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization.result_root);
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization.result_root);
        char const payload[] = "data\n";
        char payload_digest[SHA256_HEX_CAPACITY];
        bq_digest(payload, sizeof(payload) - 1, payload_digest);
        char bundle_body[512];
        int bundle_length = snprintf(bundle_body, sizeof(bundle_body), "BQ-BUNDLE-V1\nentries=1\nbytes=5\n%.64s 5 report.txt\n",
                                     payload_digest);
        char bundle_digest[SHA256_HEX_CAPACITY];
        bq_digest(bundle_body, (u32)bundle_length, bundle_digest);
        char workspace_name[64];
        BQ_CHECK(bundle_length > 0 && bq_workspace_name(workspace_name, id, token) &&
                 bq_test_write_path(report, payload, 0400) && bq_test_write_path(bundle, bundle_body, 0400));
        char manifest_body[4096];
        String8 base = bq_field(&job->request, 3), candidate = bq_field(&job->request, 4);
        int manifest_length = snprintf(manifest_body, sizeof(manifest_body),
            "schema=1\nrecipe=validate-buster-v1\nstatus=succeeded\nstage=throughput\nprocess-result=success\n"
            "job-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nworkspace-root=%.192s\nresult-root=%s\n"
            "base-revision=%.64s\ncandidate-revision=%.64s\n"
            "driver=/usr/local/libexec/buster-bench-build\nthroughput=/usr/local/libexec/buster-bench-throughput\n"
            "trusted-source-scope=operator-installed-read-only\nnamespace-policy=private-workspace-post-run-identity\n"
            "base-binary=%.192s/%s/base/build/Release/ide\ncandidate-binary=%.192s/%s/candidate/build/Release/ide\n"
            "base-binary-sha256=%064d\ncandidate-binary-sha256=%064d\nbundle-sha256=%.64s\n",
            (uint64_t)id, (uint64_t)token, fixture.config.workspace_root.pointer,
            finalization.result_root, base.pointer, candidate.pointer, fixture.config.workspace_root.pointer,
            workspace_name, fixture.config.workspace_root.pointer, workspace_name, 0, 0, bundle_digest);
        bool manifest_written = manifest_length > 0 && (u32)manifest_length < sizeof(manifest_body) &&
                               bq_test_write_path(manifest, manifest_body, 0400);
        BqError result_error = manifest_written ? bq_worker_result_validate(&fixture.config, job, &finalization) : BQ_IO;
        BQ_CHECK(manifest_written && result_error == BQ_OK);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_evidence(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, &finalization) == BQ_OK &&
                 bq_worker_result_failure_artifacts(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, &finalization) == BQ_OK &&
                 finalization.result_bound);
        unlink(report);
        BQ_CHECK(symlink("validate-buster-v1.manifest", report) == 0 &&
                 bq_worker_result_validate(&fixture.config, job, &finalization) == BQ_CONFIGURATION_MISMATCH);
        unlink(report);
        BQ_CHECK(bq_test_write_path(report, payload, 0400));
        BqError restored_error = bq_worker_result_validate(&fixture.config, job, &finalization);
        BQ_CHECK(restored_error == BQ_OK);
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome phase_outcome = phase >= BQ_FINALIZING ? BQ_SUCCEEDED : BQ_NO_OUTCOME;
            BQ_CHECK(bq_real_advance(queue, job, phase, phase_outcome) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        BQ_CHECK(job && bq_result_bind(queue, job, string_from_pointer(finalization.result_root),
                                       finalization.result_digest, finalization.bundle_digest,
                                       finalization.full_digest) == BQ_OK);
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->result_bound && bq_worker_result_binding_validate(job) == BQ_OK);
        BQ_CHECK(unlink(report) == 0 && bq_worker_result_binding_validate(job) == BQ_CONFIGURATION_MISMATCH);
        BQ_CHECK(bq_test_write_path(report, payload, 0400) &&
                 bq_worker_result_binding_validate(job) == BQ_OK);
        u8 bound_status_body[8];
        BqPacket bound_status, bound_response;
        bq_put64(bound_status_body, id);
        bq_packet(&bound_status, BQ_OP_STATUS, 490, bound_status_body, sizeof(bound_status_body));
        BQ_CHECK(bq_dispatch(queue, bound_status.bytes, bound_status.size, &bound_response) == BQ_OK &&
                 bound_response.size == BQ_CONTROL_CAP &&
                 !memcmp(bound_response.bytes + BQ_CONTROL_HEADER + 128, finalization.result_root,
                         strlen(finalization.result_root)));
        BQ_CHECK(bq_public_response_valid(&bound_status, &bound_response));
        char malicious_bundle[512], malicious_digest[SHA256_HEX_CAPACITY];
        int malicious_length = snprintf(malicious_bundle, sizeof(malicious_bundle),
                                         "BQ-BUNDLE-V1\nentries=1\nbytes=5\n%.64s 5 ../escape.txt\n",
                                         payload_digest);
        bq_digest(malicious_bundle, (u32)malicious_length, malicious_digest);
        unlink(bundle);
        BQ_CHECK(malicious_length > 0 && bq_test_write_path(bundle, malicious_bundle, 0400) &&
                 bq_worker_bundle_validate(finalization.result_directory, malicious_digest) == BQ_CONFIGURATION_MISMATCH);
        unlink(bundle);
        BQ_CHECK(bq_test_write_path(bundle, bundle_body, 0400));
        char const extra[] = "extra\n";
        char extra_path[BQ_PATH_CAP + 64];
        snprintf(extra_path, sizeof(extra_path), "%s/extra.txt", finalization.result_root);
        BQ_CHECK(bq_test_write_path(extra_path, extra, 0400) &&
                 bq_worker_result_validate(&fixture.config, job, &finalization) == BQ_CONFIGURATION_MISMATCH);
        unlink(extra_path);
        BQ_CHECK(bq_worker_result_evidence(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, &finalization) == BQ_OK &&
                 bq_worker_result_evidence(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, &finalization) == BQ_OK);
        char evidence[BQ_PATH_CAP + 64], evidence_bytes[1024] = {0};
        snprintf(evidence, sizeof(evidence), "%s/validate-buster-v1.outcome", finalization.result_root);
        int evidence_fd = open(evidence, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        ssize_t evidence_size = evidence_fd >= 0 ? read(evidence_fd, evidence_bytes, sizeof(evidence_bytes) - 1) : -1;
        if (evidence_fd >= 0) close(evidence_fd);
        BQ_CHECK(evidence_size > 0 && strstr(evidence_bytes, "status=cancelled") != NULL);
        unlink(evidence);
        BQ_CHECK(bq_worker_result_evidence(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK);
        memset(evidence_bytes, 0, sizeof(evidence_bytes));
        evidence_fd = open(evidence, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        evidence_size = evidence_fd >= 0 ? read(evidence_fd, evidence_bytes, sizeof(evidence_bytes) - 1) : -1;
        if (evidence_fd >= 0) close(evidence_fd);
        BQ_CHECK(evidence_size > 0 && strstr(evidence_bytes, "status=failed") != NULL &&
                 strstr(evidence_bytes, "error=worker-timeout") != NULL);
        close(finalization.result_directory);
        finalization.result_directory = -1;
        BQ_CHECK(bq_worker_result_open(&fixture.config, job, &finalization, false) == BQ_OK &&
                 bq_worker_result_validate(&fixture.config, job, &finalization) == BQ_OK);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_failure_bundle_replay(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(84);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        for (BqPhase phase = BQ_SETTLING; phase <= BQ_CLEANING; phase = (BqPhase)(phase + 1))
        {
            BqOutcome phase_outcome = phase >= BQ_FINALIZING ? BQ_FAILED : BQ_NO_OUTCOME;
            BQ_CHECK(bq_real_advance(queue, job, phase, phase_outcome) == BQ_OK);
            job = bq_job(&queue->state, id);
        }
        char manifest[BQ_PATH_CAP + 64], bundle[BQ_PATH_CAP + 64], outcome[BQ_PATH_CAP + 64];
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization.result_root);
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization.result_root);
        snprintf(outcome, sizeof(outcome), "%s/validate-buster-v1.outcome", finalization.result_root);
        BqError failure_evidence = job ? bq_worker_result_evidence(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) : BQ_IO;
        BqError first_failure = failure_evidence == BQ_OK ? bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT,
                                                                                                  &finalization) : BQ_IO;
        BqError second_failure = first_failure == BQ_OK ? bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT,
                                                                                               &finalization) : BQ_IO;
        int manifest_unlink = unlink(manifest);
        int bundle_unlink = unlink(bundle);
        BQ_CHECK(failure_evidence == BQ_OK);
        BQ_CHECK(first_failure == BQ_OK);
        BQ_CHECK(second_failure == BQ_OK);
        BQ_CHECK(manifest_unlink == 0 && bundle_unlink == 0);
        char existing_bundle[] = "BQ-BUNDLE-V1\nentries=0\nbytes=0\n";
        char existing_bundle_digest[SHA256_HEX_CAPACITY];
        char existing_manifest[4096];
        bq_digest(existing_bundle, (u32)strlen(existing_bundle), existing_bundle_digest);
        int existing_manifest_length = snprintf(
            existing_manifest, sizeof(existing_manifest),
            "schema=1\nrecipe=validate-buster-v1\nstatus=failed\nstage=candidate-build\nprocess-result=failed\n"
            "job-id=%" PRIu64 "\nattempt-token=%" PRIu64 "\nresult-root=%s\nbundle-sha256=%s\n",
            (uint64_t)id, (uint64_t)token, finalization.result_root, existing_bundle_digest);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(existing_manifest_length > 0 && (u32)existing_manifest_length < sizeof(existing_manifest) &&
                 bq_test_write_path(bundle, existing_bundle, 0400) &&
                 bq_test_write_path(manifest, existing_manifest, 0400) &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 unlink(manifest) == 0 && unlink(bundle) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_CANCELLED, BQ_WORKER_CANCEL_SIGNAL, &finalization) == BQ_OK &&
                 bq_result_bind(queue, job, string_from_pointer(finalization.result_root), finalization.result_digest,
                                finalization.bundle_digest, finalization.full_digest) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->result_bound && bq_worker_result_binding_validate(job) == BQ_OK &&
                 access(manifest, F_OK) == 0 && access(bundle, F_OK) == 0 && access(outcome, F_OK) == 0);
        close(finalization.result_directory);
        finalization.result_directory = -1;
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->result_bound && bq_worker_result_binding_validate(job) == BQ_OK);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_failure_bundle_coverage(void)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(86);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        char prepare[BQ_PATH_CAP + 80], stage[BQ_PATH_CAP + 80], throughput_dir[BQ_PATH_CAP + 64];
        char nested_dir[BQ_PATH_CAP + 80], first_file[BQ_PATH_CAP + 96], second_file[BQ_PATH_CAP + 96];
        char bundle[BQ_PATH_CAP + 64], manifest[BQ_PATH_CAP + 64];
        snprintf(prepare, sizeof(prepare), "%s/validate-buster-v1.prepare.manifest", finalization.result_root);
        snprintf(stage, sizeof(stage), "%s/validate-buster-v1.base-generate.manifest", finalization.result_root);
        snprintf(throughput_dir, sizeof(throughput_dir), "%s/throughput", finalization.result_root);
        snprintf(nested_dir, sizeof(nested_dir), "%s/throughput/nested", finalization.result_root);
        snprintf(first_file, sizeof(first_file), "%s/throughput/result.txt", finalization.result_root);
        snprintf(second_file, sizeof(second_file), "%s/throughput/nested/result.txt", finalization.result_root);
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization.result_root);
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization.result_root);
        BQ_CHECK(bq_test_write_path(prepare, "schema=1\nstage=prepare\n", 0400) &&
                 bq_test_write_path(stage, "schema=1\nstage=base-generate\n", 0400) &&
                 mkdir(throughput_dir, 0700) == 0 && mkdir(nested_dir, 0700) == 0 &&
                 bq_test_write_path(first_file, "first\n", 0400) &&
                 bq_test_write_path(second_file, "second\n", 0400));
        BQ_CHECK(bq_worker_result_evidence(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK);
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound);
        char saved_result[SHA256_HEX_CAPACITY], saved_bundle[SHA256_HEX_CAPACITY], saved_full[SHA256_HEX_CAPACITY];
        memcpy(saved_result, finalization.result_digest, sizeof(saved_result));
        memcpy(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle));
        memcpy(saved_full, finalization.full_digest, sizeof(saved_full));
        int bundle_fd = open(bundle, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char bundle_body[8192] = {0};
        ssize_t bundle_size = bundle_fd >= 0 ? read(bundle_fd, bundle_body, sizeof(bundle_body) - 1) : -1;
        if (bundle_fd >= 0) close(bundle_fd);
        BQ_CHECK(bundle_size > 0 && strstr(bundle_body, "validate-buster-v1.prepare.manifest") &&
                 strstr(bundle_body, "validate-buster-v1.base-generate.manifest") &&
                 strstr(bundle_body, "throughput/result.txt") &&
                 strstr(bundle_body, "throughput/nested/result.txt") &&
                 !strstr(bundle_body, "validate-buster-v1.outcome") &&
                 !strstr(bundle_body, "validate-buster-v1.bundle") &&
                 !strstr(bundle_body, "validate-buster-v1.manifest"));
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound &&
                 !memcmp(saved_result, finalization.result_digest, sizeof(saved_result)) &&
                 !memcmp(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle)) &&
                 !memcmp(saved_full, finalization.full_digest, sizeof(saved_full)));
        BQ_CHECK(unlink(manifest) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound &&
                 !memcmp(saved_result, finalization.result_digest, sizeof(saved_result)) &&
                 !memcmp(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle)) &&
                 !memcmp(saved_full, finalization.full_digest, sizeof(saved_full)));
        BQ_CHECK(unlink(bundle) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound && access(bundle, F_OK) != 0);
        BQ_CHECK(unlink(manifest) == 0);
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound);
        BQ_CHECK(unlink(bundle) == 0 && unlink(manifest) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_test_write_path(bundle, "BQ-BUNDLE-V1\nentries=0\nbytes=1\n", 0400) &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound);
        BQ_CHECK(unlink(bundle) == 0);
        char link_path[BQ_PATH_CAP + 64];
        snprintf(link_path, sizeof(link_path), "%s/evil", finalization.result_root);
        BQ_CHECK(symlink("throughput/result.txt", link_path) == 0 &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound && access(bundle, F_OK) != 0);
        BQ_CHECK(unlink(link_path) == 0);
        char fifo_path[BQ_PATH_CAP + 64];
        snprintf(fifo_path, sizeof(fifo_path), "%s/pipe", finalization.result_root);
        BQ_CHECK(mkfifo(fifo_path, 0400) == 0 &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound && access(bundle, F_OK) != 0);
        BQ_CHECK(unlink(fifo_path) == 0);
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound);
        close(finalization.result_directory);
        finalization.result_directory = -1;
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(87);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        char payload_path[BQ_PATH_CAP + 64];
        for (u32 index = 0; index < BQ_WORKER_BUNDLE_ENTRY_CAP - 3; index += 1)
        {
            int length = snprintf(payload_path, sizeof(payload_path), "%s/payload-%04u.txt",
                                  finalization.result_root, index);
            BQ_CHECK(length > 0 && (u32)length < sizeof(payload_path) &&
                     bq_test_write_path(payload_path, "x\n", 0400));
        }
        char bundle[BQ_PATH_CAP + 64], manifest[BQ_PATH_CAP + 64];
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization.result_root);
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization.result_root);
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound);
        struct stat bundle_info = {0};
        BQ_CHECK(stat(bundle, &bundle_info) == 0 && bundle_info.st_size > 32768);
        char saved_result[SHA256_HEX_CAPACITY], saved_bundle[SHA256_HEX_CAPACITY], saved_full[SHA256_HEX_CAPACITY];
        memcpy(saved_result, finalization.result_digest, sizeof(saved_result));
        memcpy(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle));
        memcpy(saved_full, finalization.full_digest, sizeof(saved_full));
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 !memcmp(saved_result, finalization.result_digest, sizeof(saved_result)) &&
                 !memcmp(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle)) &&
                 !memcmp(saved_full, finalization.full_digest, sizeof(saved_full)));
        BQ_CHECK(unlink(manifest) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound &&
                 !memcmp(saved_result, finalization.result_digest, sizeof(saved_result)) &&
                 !memcmp(saved_bundle, finalization.bundle_digest, sizeof(saved_bundle)) &&
                 !memcmp(saved_full, finalization.full_digest, sizeof(saved_full)));
        BQ_CHECK(bq_worker_result_evidence(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) == BQ_OK &&
                 finalization.result_bound);
        int overflow_length = snprintf(payload_path, sizeof(payload_path), "%s/payload-%04u.txt",
                                       finalization.result_root, BQ_WORKER_BUNDLE_ENTRY_CAP - 3);
        BQ_CHECK(overflow_length > 0 && (u32)overflow_length < sizeof(payload_path) &&
                 bq_test_write_path(payload_path, "x\n", 0400));
        BQ_CHECK(unlink(bundle) == 0 && unlink(manifest) == 0);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound && access(bundle, F_OK) != 0 && access(manifest, F_OK) != 0);
        close(finalization.result_directory);
        finalization.result_directory = -1;
        bq_test_worker_end(&fixture);
    }
    if (bq_test_worker_begin(&fixture, BQ_WORKER_SUCCEEDED, false))
    {
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(88);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_test_worker_make_success_result(&fixture, job, &finalization));
        char report[BQ_PATH_CAP + 64], bundle[BQ_PATH_CAP + 64], manifest[BQ_PATH_CAP + 64];
        snprintf(report, sizeof(report), "%s/report.txt", finalization.result_root);
        snprintf(bundle, sizeof(bundle), "%s/validate-buster-v1.bundle", finalization.result_root);
        snprintf(manifest, sizeof(manifest), "%s/validate-buster-v1.manifest", finalization.result_root);
        memset(finalization.result_digest, 0, sizeof(finalization.result_digest));
        memset(finalization.bundle_digest, 0, sizeof(finalization.bundle_digest));
        memset(finalization.full_digest, 0, sizeof(finalization.full_digest));
        finalization.result_bound = false;
        BQ_CHECK(unlink(report) == 0 && bq_test_write_path(report, "corrupted\n", 0400) &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound);
        BQ_CHECK(unlink(report) == 0 && bq_test_write_path(report, "data\n", 0400));
        BqJob mismatched_id = *job;
        mismatched_id.id += 1;
        BQ_CHECK(bq_worker_result_failure_artifacts(&mismatched_id, BQ_FAILED, BQ_WORKER_TIMEOUT,
                                                  &finalization) != BQ_OK && !finalization.result_bound);
        BqJob mismatched_token = *job;
        mismatched_token.token += 1;
        BQ_CHECK(bq_worker_result_failure_artifacts(&mismatched_token, BQ_FAILED, BQ_WORKER_TIMEOUT,
                                                  &finalization) != BQ_OK && !finalization.result_bound);
        BQ_CHECK(unlink(bundle) == 0 &&
                 bq_worker_result_failure_artifacts(job, BQ_FAILED, BQ_WORKER_TIMEOUT, &finalization) != BQ_OK &&
                 !finalization.result_bound);
        BQ_CHECK(unlink(manifest) == 0 && unlink(report) == 0);
        close(finalization.result_directory);
        finalization.result_directory = -1;
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL void bq_test_worker_preparing_recovery(u32 new_boot)
{
    BqWorkerFixture fixture;
    if (bq_test_worker_begin(&fixture, BQ_WORKER_EXECUTION_FAILED, false))
    {
        fixture.config.production_path = true;
        u32 inaccessible_root_length = (u32)strlen(fixture.root);
        u32 inaccessible_lease_length = (u32)strlen(fixture.lease);
        BQ_CHECK(inaccessible_root_length + 1 + inaccessible_lease_length <
                 sizeof(fixture.fake.observed.inaccessible_paths));
        memcpy(fixture.fake.observed.inaccessible_paths, fixture.root, inaccessible_root_length);
        fixture.fake.observed.inaccessible_paths[inaccessible_root_length] = ' ';
        memcpy(fixture.fake.observed.inaccessible_paths + inaccessible_root_length + 1, fixture.lease,
               inaccessible_lease_length + 1);
        BqQueue* queue = &fixture.material.queue.queue;
        BqRequest request = bq_test_real_request(85 + new_boot);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root, &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_PREPARING);
        BQ_CHECK(job && bq_test_worker_bind(&fixture, job));
        BqWorkerFinalization finalization = {.config = &fixture.config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&fixture.config, job, &finalization, true) == BQ_OK);
        char prepare[BQ_PATH_CAP + 96], stage[BQ_PATH_CAP + 96], evidence_dir[BQ_PATH_CAP + 80];
        char nested_dir[BQ_PATH_CAP + 96], first_file[BQ_PATH_CAP + 112], second_file[BQ_PATH_CAP + 128];
        snprintf(prepare, sizeof(prepare), "%.200s/validate-buster-v1.prepare.manifest", finalization.result_root);
        snprintf(stage, sizeof(stage), "%.200s/validate-buster-v1.base-generate.manifest", finalization.result_root);
        snprintf(evidence_dir, sizeof(evidence_dir), "%.200s/throughput", finalization.result_root);
        snprintf(nested_dir, sizeof(nested_dir), "%.200s/throughput/nested", finalization.result_root);
        snprintf(first_file, sizeof(first_file), "%.200s/throughput/result.txt", finalization.result_root);
        snprintf(second_file, sizeof(second_file), "%.200s/throughput/nested/result.txt", finalization.result_root);
        BQ_CHECK(bq_test_write_path(prepare, "schema=1\nstage=prepare\nprocess-result=running\n", 0400) &&
                 mkdir(evidence_dir, 0700) == 0 && mkdir(nested_dir, 0700) == 0 &&
                 bq_test_write_path(first_file, "first\n", 0400) &&
                 bq_test_write_path(second_file, "second\n", 0400));
        if (new_boot)
            BQ_CHECK(bq_test_write_path(stage, "schema=1\nstage=base-generate\nprocess-result=success\n", 0400));
        if (!new_boot)
        {
            BqWorkerLeaseHandoff stale = {.listener = -1, .parent = -1};
            BQ_CHECK(bq_worker_lease_handoff_open(finalization.result_root, finalization.result_directory, &stale));
            if (stale.listener >= 0) close(stale.listener);
            if (stale.parent >= 0) close(stale.parent);
        }
        if (finalization.result_directory >= 0)
        {
            close(finalization.result_directory);
            finalization.result_directory = -1;
        }
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK && queue->needs_reconciliation);
        if (new_boot)
        {
            int boot = open(fixture.boot, O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
            char const changed[] = "bbbbbbbb-0000-0000-0000-000000000000\n";
            BQ_CHECK(boot >= 0 && bq_write_all(boot, (u8 const*)changed, sizeof(changed) - 1) && fsync(boot) == 0);
            if (boot >= 0) close(boot);
        }
        BQ_CHECK(bq_worker_run(queue, &fixture.config, &id) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_INTERRUPTED && job->result_bound &&
                 bq_worker_result_binding_validate(job) == BQ_OK &&
                 bq_failure_evidence(queue, job) == (new_boot ? BQ_BOOT_INTERRUPTED : BQ_WORKER_INTERRUPTED) &&
                 !queue->state.active_id && !queue->needs_reconciliation &&
                 !bq_test_worker_probe_locked(fixture.lease));
        char result_root[BQ_PATH_CAP + 64], bundle[BQ_PATH_CAP + 96], manifest[BQ_PATH_CAP + 96];
        char socket_path[BQ_PATH_CAP + 96];
        int root_length = snprintf(result_root, sizeof(result_root), "%.140s/results/job-%" PRIu64 "-attempt-%" PRIu64,
                                   fixture.material.workspaces, (uint64_t)id, (uint64_t)token);
        int bundle_length = snprintf(bundle, sizeof(bundle), "%.200s/validate-buster-v1.bundle", result_root);
        int manifest_length = snprintf(manifest, sizeof(manifest), "%.200s/validate-buster-v1.manifest", result_root);
        int socket_length = snprintf(socket_path, sizeof(socket_path), "%.200s/.lease-handoff", result_root);
        BQ_CHECK(root_length > 0 && bundle_length > 0 && manifest_length > 0 && socket_length > 0 &&
                 access(manifest, F_OK) == 0 && access(bundle, F_OK) == 0 &&
                 access(socket_path, F_OK) != 0 && errno == ENOENT);
        int bundle_fd = open(bundle, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        char bundle_body[8192] = {0};
        ssize_t bundle_size = bundle_fd >= 0 ? read(bundle_fd, bundle_body, sizeof(bundle_body) - 1) : -1;
        if (bundle_fd >= 0) close(bundle_fd);
        BQ_CHECK(bundle_size > 0 && strstr(bundle_body, "validate-buster-v1.prepare.manifest") &&
                 strstr(bundle_body, "throughput/result.txt") &&
                 strstr(bundle_body, "throughput/nested/result.txt") &&
                 (!new_boot || strstr(bundle_body, "validate-buster-v1.base-generate.manifest")));
        char result_digest[SHA256_HEX_CAPACITY], bundle_digest[SHA256_HEX_CAPACITY], full_digest[SHA256_HEX_CAPACITY];
        if (job)
        {
            memcpy(result_digest, job->result_manifest_digest, sizeof(result_digest));
            memcpy(bundle_digest, job->result_bundle_digest, sizeof(bundle_digest));
            memcpy(full_digest, job->result_full_digest, sizeof(full_digest));
        }
        bq_close(queue);
        BQ_CHECK(bq_open(queue, fixture.material.queue.path) == BQ_OK);
        job = bq_job(&queue->state, id);
        BQ_CHECK(job && job->phase == BQ_FINISHED && job->outcome == BQ_INTERRUPTED && job->result_bound &&
                 !memcmp(result_digest, job->result_manifest_digest, sizeof(result_digest)) &&
                 !memcmp(bundle_digest, job->result_bundle_digest, sizeof(bundle_digest)) &&
                 !memcmp(full_digest, job->result_full_digest, sizeof(full_digest)) &&
                 bq_worker_result_binding_validate(job) == BQ_OK && !queue->state.active_id);
        BqRequest second = bq_test_real_request(96 + new_boot);
        u64 second_id = 0, second_token = 0;
        BQ_CHECK(bq_submit(queue, &second, &second_id) == BQ_OK && second_id != 0 &&
                 bq_materialize(queue, fixture.config.installed_root, fixture.config.workspace_root,
                                &second_id, &second_token) == BQ_OK && second_id != id &&
                 queue->state.active_id == second_id);
        BqJob* second_job = bq_job(&queue->state, second_id);
        BQ_CHECK(second_job && bq_failure_write(queue, second_job, BQ_WORKER_INTERRUPTED) == BQ_OK);
        queue->needs_reconciliation = true;
        BQ_CHECK(bq_workspace_reconcile(queue, fixture.config.workspace_root, second_id, second_token) == BQ_OK);
        bq_test_worker_end(&fixture);
    }
}

BUSTER_GLOBAL_LOCAL bool bq_test_recipe_driver_run(char const* driver, char* const arguments[])
{
    pid_t child = driver && driver[0] == '/' ? fork() : -1;
    if (child == 0)
    {
        if (setpgid(0, 0) != 0) _exit(126);
        execv(driver, arguments);
        _exit(127);
    }
    int status = 0;
    bool reaped = false, exited = false;
    u64 deadline = bq_worker_deadline(bq_worker_monotonic_milliseconds(), 120000);
    while (child > 0 && !reaped)
    {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child)
        {
            reaped = true;
            exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        else if (waited < 0 && errno != EINTR)
        {
            reaped = true;
        }
        else if (waited == 0 && bq_worker_monotonic_milliseconds() >= deadline)
        {
            reaped = true;
        }
        else
        {
            bq_worker_sleep_until(bq_worker_deadline(bq_worker_monotonic_milliseconds(), 20));
        }
        if (reaped && !exited)
        {
            kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        }
    }
    return child > 0 && reaped && exited;
}

BUSTER_GLOBAL_LOCAL void bq_test_recipe_materialized_bridge(char const* driver)
{
    bool ok = driver != NULL;
    if (!ok)
    {
        printf("BENCH_SERVICE_RECIPE_MATERIALIZED_BRIDGE result=unavailable\n");
    }
    BqMaterialFixture fixture;
    memset(&fixture, 0, sizeof(fixture));
    if (ok) ok = bq_material_test_begin(&fixture, 9);
    char id_text[32] = {0}, token_text[32] = {0};
    char workspace[BQ_PATH_CAP + 1] = {0}, result_root[BQ_PATH_CAP + 1] = {0};
    if (ok)
    {
        BqRequest request = bq_test_real_request(93);
        u64 id = 0, token = 0;
        ok = bq_submit(&fixture.queue.queue, &request, &id) == BQ_OK &&
             bq_materialize(&fixture.queue.queue, string_from_pointer(fixture.installed),
                            string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK;
        BqJob* job = ok ? bq_job(&fixture.queue.queue.state, id) : NULL;
        BqWorkerConfig config = {0};
        config.workspace_root = string_from_pointer(fixture.workspaces);
        BqWorkerFinalization finalization = {.config = &config, .result_directory = -1};
        ok = ok && job && bq_worker_result_open(&config, job, &finalization, true) == BQ_OK;
        int id_length = snprintf(id_text, sizeof(id_text), "%" PRIu64, (uint64_t)id);
        int token_length = snprintf(token_text, sizeof(token_text), "%" PRIu64, (uint64_t)token);
        int workspace_length = snprintf(workspace, sizeof(workspace), "%.120s/job-%.20s-attempt-%.20s",
                                        fixture.workspaces, id_text, token_text);
        int result_length = snprintf(result_root, sizeof(result_root), "%.120s/results/job-%.20s-attempt-%.20s",
                                     fixture.workspaces, id_text, token_text);
        ok = ok && id_length > 0 && (u32)id_length < sizeof(id_text) && token_length > 0 &&
             (u32)token_length < sizeof(token_text) && workspace_length > 0 &&
             (u32)workspace_length < sizeof(workspace) && result_length > 0 &&
             (u32)result_length < sizeof(result_root);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        BQ_CHECK(ok);
    }
    if (ok)
    {
        char copied_manifest[BQ_PATH_CAP + 64], base_subject[BQ_PATH_CAP + 16], candidate_subject[BQ_PATH_CAP + 16];
        int manifest_length = snprintf(copied_manifest, sizeof(copied_manifest), "%.160s/base/source/.source-manifest",
                                       workspace);
        int base_length = snprintf(base_subject, sizeof(base_subject), "%.180s/base", workspace);
        int candidate_length = snprintf(candidate_subject, sizeof(candidate_subject), "%.180s/candidate", workspace);
        struct stat manifest_info = {0}, attempt_info = {0}, base_info = {0}, candidate_info = {0};
        ok = manifest_length > 0 && base_length > 0 && candidate_length > 0 &&
             stat(copied_manifest, &manifest_info) == 0 && manifest_info.st_size > 4096 &&
             stat(workspace, &attempt_info) == 0 && (attempt_info.st_mode & 07777) == 02710 &&
             stat(base_subject, &base_info) == 0 && (base_info.st_mode & 07777) == 02710 &&
             stat(candidate_subject, &candidate_info) == 0 && (candidate_info.st_mode & 07777) == 02710;
        BQ_CHECK(ok);
    }
    if (ok)
    {
        char* arguments[] = {(char*)driver, (char*)"bench_service_recipe_self_test", id_text, token_text,
                             fixture.workspaces,
                             (char*)"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                             (char*)"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                             result_root, NULL};
        ok = bq_test_recipe_driver_run(driver, arguments);
        BQ_CHECK(ok);
    }
    if (ok)
    {
        int result_directory = open(result_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int bundle = result_directory >= 0 ? openat(result_directory, "validate-buster-v1.bundle",
                                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
        struct stat bundle_info = {0};
        ok = bundle >= 0 && fstat(bundle, &bundle_info) == 0 && bundle_info.st_size > 0 &&
             (u64)bundle_info.st_size <= BQ_WORKER_BUNDLE_CAP;
        u8* body = ok ? mmap(NULL, (size_t)bundle_info.st_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : NULL;
        ok = body != NULL && body != MAP_FAILED;
        u64 used = 0;
        while (ok && used < (u64)bundle_info.st_size)
        {
            ssize_t count = read(bundle, body + used, (size_t)((u64)bundle_info.st_size - used));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) ok = false;
            else used += (u64)count;
        }
        char bundle_digest[SHA256_HEX_CAPACITY];
        if (ok) bq_digest(body, (u32)used, bundle_digest);
        ok = ok && bq_worker_bundle_validate_full(result_directory, bundle_digest, NULL) == BQ_OK;
        if (body && body != MAP_FAILED) munmap(body, (size_t)bundle_info.st_size);
        if (bundle >= 0) close(bundle);
        if (result_directory >= 0) close(result_directory);
        BQ_CHECK(ok);
    }
    if (ok)
    {
        char* fixed[] = {(char*)driver, (char*)"bench_service_recipe_self_test", NULL};
        ok = bq_test_recipe_driver_run(driver, fixed);
        BQ_CHECK(ok);
    }
    if (fixture.installed[0]) bq_material_test_end(&fixture);
}

BUSTER_GLOBAL_LOCAL void bq_test_large_source_manifest(void)
{
    BqMaterialFixture fixture;
    if (bq_material_test_begin(&fixture, 9))
    {
        BqQueue* queue = &fixture.queue.queue;
        BqRequest request = bq_test_real_request(92);
        u64 id = 0, token = 0;
        BQ_CHECK(bq_submit(queue, &request, &id) == BQ_OK &&
                 bq_materialize(queue, string_from_pointer(fixture.installed),
                                string_from_pointer(fixture.workspaces), &id, &token) == BQ_OK);
        BqJob* job = bq_job(&queue->state, id);
        char name[64], workspace[BQ_PATH_CAP + 1], copied_manifest[BQ_PATH_CAP + 64];
        BQ_CHECK(job && bq_workspace_name(name, id, token));
        int workspace_length = snprintf(workspace, sizeof(workspace), "%.140s/%.48s", fixture.workspaces, name);
        int manifest_length = snprintf(copied_manifest, sizeof(copied_manifest), "%.160s/base/source/.source-manifest",
                                       workspace);
        struct stat manifest_info = {0};
        BQ_CHECK(workspace_length > 0 && (u32)workspace_length < sizeof(workspace) && manifest_length > 0 &&
                 stat(copied_manifest, &manifest_info) == 0 && manifest_info.st_size > 4096);
        BqWorkerConfig config = {0};
        config.workspace_root = string_from_pointer(fixture.workspaces);
        BqWorkerFinalization finalization = {.config = &config, .result_directory = -1};
        BQ_CHECK(job && bq_worker_result_open(&config, job, &finalization, true) == BQ_OK);
        if (finalization.result_directory >= 0) close(finalization.result_directory);
        bq_material_test_end(&fixture);
    }
}
#endif
#endif

#include "export_tests.c"

BUSTER_GLOBAL_LOCAL int bq_test_run_all(int argc, char** argv)
{
#ifdef __linux__
    char* executable = realpath(argv[0], NULL);
    u64 executable_length = executable ? (u64)strlen(executable) : UINT64_MAX;
    BQ_CHECK(executable_length < sizeof(bq_test_executable));
    if (executable_length < sizeof(bq_test_executable))
        memcpy(bq_test_executable, executable, (size_t)executable_length + 1);
    free(executable);
#else
    (void)argc;
    (void)argv;
#endif
    bq_test_codec();
    bq_test_typed_client();
#ifndef _WIN32
    bq_test_physical_temp_paths();
    bq_test_workspace_root_group_policy();
    bq_test_closed_handle();
    bq_test_admission();
    bq_test_prefixes_and_corruption();
    bq_test_faults();
    bq_test_phase_restarts();
    bq_test_completion_failures();
    bq_test_protocol_mutations();
    bq_test_journal_schema_migration();
    bq_test_supervisor_schema_migration();
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
#ifdef __linux__
    /* The filter is confined to a child; supported Linux CI must execute it. */
#if defined(__x86_64__) || defined(__aarch64__)
    bq_test_sgid_sandbox();
#else
    printf("SGID_SANDBOX_TEST service status=unsupported-architecture\n");
#endif
    bq_test_transport_boundaries();
    bq_test_worker_deadlines();
    bq_test_worker_lease_handoff();
    bq_test_worker_lease_handoff_negative(0);
    bq_test_worker_lease_handoff_negative(1);
    bq_test_worker_lease_handoff_negative(2);
    bq_test_worker_lease_handoff_cleanup_failure(0);
    bq_test_worker_lease_handoff_cleanup_failure(1);
    bq_test_worker_lease_handoff_cleanup_failure(2);
    bq_test_worker_lease_handoff_cleanup_failure(3);
    bq_test_worker_success_and_tree_cleanup();
    bq_test_worker_drained_unit();
    bq_test_worker_term_grace();
    bq_test_worker_child_survives_parent_kill();
    bq_test_worker_late_cancel();
    bq_test_worker_cancel_during_finalization();
    bq_test_worker_post_publication_cancel();
    bq_test_worker_prelaunch_cancel();
    bq_test_worker_observe_error_retains_authority();
    bq_test_worker_unbound_launcher_cleanup();
    bq_test_worker_instance_reuse();
    bq_test_worker_cgroup_paths();
    bq_test_worker_deploy_policy();
    bq_test_worker_systemd_results();
    bq_test_worker_outcomes();
    bq_test_worker_quarantine_and_recovery();
    bq_test_worker_ancestor_budget();
    bq_test_worker_boot_and_identity_recovery();
    bq_test_worker_fixed_recipe_sigkill_recovery();
    bq_test_worker_lock_precedes_materialization();
    bq_test_transport_worker_retries_after_busy();
    bq_test_transport_worker_signal_handoff();
    bq_test_export_inventory();
    bq_test_export(true);
    bq_test_export(false);
    bq_test_worker_result_bundle_and_evidence();
    bq_test_worker_failure_bundle_replay();
    bq_test_worker_failure_bundle_coverage();
    bq_test_worker_preparing_recovery(0);
    bq_test_worker_preparing_recovery(1);
    bq_test_large_source_manifest();
    bq_test_recipe_materialized_bridge(argc > 2 ? argv[2] : NULL);
#endif
    char const* storage = "posix-real-journal";
#else
    char const* storage = "unsupported-codec-only";
#endif
    printf("BENCH_SERVICE_SELF_TEST assertions=%u failures=%u storage=%s executor=fake-plus-supervisor-fixed-recipe-handoff\n",
           bq_test_assertions, bq_test_failures, storage);
    int result = bq_test_failures ? 1 : 0;
    return result;
}

int main(int argc, char** argv)
{
    int result;
#ifdef __linux__
    bool helper = argc == 10 && !strcmp(argv[1], "fixed-recipe-helper");
    if (argc == 2 && !strcmp(argv[1], "--export-only"))
    {
        bq_test_export_inventory();
        bq_test_export(true);
        bq_test_export(false);
        printf("EXPORT_SELF_TEST assertions=%u failures=%u\n", bq_test_assertions, bq_test_failures);
        result = bq_test_failures ? 1 : 0;
    }
    else if (argc == 2 && !strcmp(argv[1], "--sgid-only"))
    {
#if defined(__x86_64__) || defined(__aarch64__)
        bq_test_sgid_sandbox();
        result = bq_test_failures ? 1 : 0;
#else
        printf("SGID_SANDBOX_TEST service status=unsupported-architecture\n");
        result = 0;
#endif
    }
    else result = helper ? bq_test_fixed_recipe_helper(argc, argv) : bq_test_run_all(argc, argv);
#else
    result = bq_test_run_all(argc, argv);
#endif
    return result;
}
