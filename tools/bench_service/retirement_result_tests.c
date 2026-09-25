/* Focused Linux failure and receipt-authority fixtures for #1023.
 * Compile with BUSTER_RETIREMENT_STORE_TEST and link retirement_result.c and
 * src/buster/lib/hash.c; no benchmark result is produced by these fixtures.
 */
#define _GNU_SOURCE 1
#include "../throughput/retirement_store.h"
#ifdef __linux__
#include <buster/lib/hash.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern unsigned tp_retirement_store_test_sync_calls, tp_retirement_store_test_fail_sync;
static unsigned assertions, failures;
#define CHECK(value) do { ++assertions; if (!(value)) { ++failures; fprintf(stderr, "STORE_TEST line=%d: %s\n", __LINE__, #value); } } while (0)
static char const digest_a[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static char const digest_b[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

typedef struct Fixture
{
    char path[96], private_path[96];
    int root, private_root;
    TpRetirementStore store;
    TpRetirementStoredFile files[4];
} Fixture;

static int fixture_start(Fixture* fixture, unsigned capacity)
{
    *fixture = (Fixture){.root = -1, .private_root = -1};
    strcpy(fixture->path, "/tmp/buster-retirement-store-XXXXXX");
    strcpy(fixture->private_path, "/tmp/buster-retirement-authority-XXXXXX");
    int valid = mkdtemp(fixture->path) != NULL;
    if (valid) fixture->root = open(fixture->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (valid) valid = mkdtemp(fixture->private_path) != NULL;
    if (valid) fixture->private_root = open(fixture->private_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (valid) valid = fixture->root >= 0 &&
                       fixture->private_root >= 0 &&
                       tp_retirement_store_open(&fixture->store, fixture->root, fixture->files, capacity);
    tp_retirement_store_test_fail_sync = 0;
    tp_retirement_store_test_sync_calls = 0;
    return valid;
}

static void fixture_clean_directory(int directory)
{
    int scan = directory >= 0 ? fcntl(directory, F_DUPFD_CLOEXEC, 3) : -1;
    DIR* stream = scan >= 0 ? fdopendir(scan) : NULL;
    if (!stream && scan >= 0) close(scan);
    struct dirent* entry;
    while (stream && (entry = readdir(stream)) != NULL)
    {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
        {
            struct stat info;
            if (fstatat(directory, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(info.st_mode))
            {
                int child = openat(directory, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
                if (child >= 0)
                {
                    int child_scan = fcntl(child, F_DUPFD_CLOEXEC, 3);
                    DIR* child_stream = child_scan >= 0 ? fdopendir(child_scan) : NULL;
                    struct dirent* leaf;
                    while (child_stream && (leaf = readdir(child_stream)) != NULL)
                        if (strcmp(leaf->d_name, ".") && strcmp(leaf->d_name, ".."))
                            unlinkat(child, leaf->d_name, 0);
                    if (child_stream) closedir(child_stream);
                    else if (child_scan >= 0) close(child_scan);
                    close(child);
                    unlinkat(directory, entry->d_name, AT_REMOVEDIR);
                }
            }
            else unlinkat(directory, entry->d_name, 0);
        }
    }
    if (stream) closedir(stream);
}

static void fixture_stop(Fixture* fixture)
{
    tp_retirement_store_close(&fixture->store);
    if (fixture->root >= 0)
    {
        fixture_clean_directory(fixture->root);
        CHECK(close(fixture->root) == 0);
    }
    if (fixture->private_root >= 0)
    {
        fixture_clean_directory(fixture->private_root);
        CHECK(close(fixture->private_root) == 0);
    }
    CHECK(rmdir(fixture->path) == 0);
    CHECK(rmdir(fixture->private_path) == 0);
    tp_retirement_store_test_fail_sync = 0;
}

static int fixture_publish(Fixture* fixture, char const* path, char const* bytes,
                           size_t length, uint64_t claimed_length)
{
    TpRetirementPending pending = {0};
    int valid = tp_retirement_store_begin(&fixture->store, path, 1024, &pending);
    if (valid) valid = fwrite(bytes, 1, length, pending.stream) == length;
    if (valid)
    {
        Sha256 hash;
        char digest[65];
        sha256_init(&hash);
        sha256_add(&hash, bytes, length);
        sha256_finish_hex(&hash, (char8*)digest);
        valid = tp_retirement_store_publish(&fixture->store, &pending, claimed_length, digest);
    }
    else if (pending.stream) tp_retirement_store_abort(&fixture->store, &pending);
    return valid;
}

static int fixture_receipt_count(Fixture* fixture, char const* path, char const* shard,
                                 size_t shard_bytes, unsigned claimed_records)
{
    Sha256 hash;
    char digest[65], body[1024];
    sha256_init(&hash);
    sha256_add(&hash, shard, shard_bytes);
    sha256_finish_hex(&hash, (char8*)digest);
    int size = snprintf(body, sizeof(body),
        "{\"attempt\":2,\"boot_id\":\"boot-1\",\"bound_at_ns\":1000,\"completed_at_ns\":2000,"
        "\"context_sha256\":\"%s\",\"execution_plan_sha256\":\"%s\",\"invocations\":1,"
        "\"job_id\":\"job-1\",\"schema\":\"buster-native-retirement-execution-receipt-v1\","
        "\"shards\":[{\"bytes\":%zu,\"path\":\"%s\",\"records\":%u,\"sha256\":\"%s\"}],\"version\":1}\n",
        digest_b, digest_a, shard_bytes, path, claimed_records, digest);
    int valid = size > 0 && (size_t)size < sizeof(body) &&
                fixture_publish(fixture, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, body, (size_t)size, (uint64_t)size);
    return valid;
}

static int fixture_receipt(Fixture* fixture, char const* path, char const* shard, size_t shard_bytes)
{
    int valid = fixture_receipt_count(fixture, path, shard, shard_bytes, 1);
    return valid;
}

typedef struct QueueAuthorityFixture
{
    char path[96];
    int root;
} QueueAuthorityFixture;

static int fixture_queue_start(QueueAuthorityFixture* queue)
{
    *queue = (QueueAuthorityFixture){.root = -1};
    strcpy(queue->path, "/tmp/buster-retirement-queue-XXXXXX");
    int valid = mkdtemp(queue->path) != NULL;
    if (valid) queue->root = open(queue->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    valid = valid && queue->root >= 0;
    return valid;
}

static void fixture_queue_stop(QueueAuthorityFixture* queue)
{
    if (queue->root >= 0)
    {
        fixture_clean_directory(queue->root);
        CHECK(close(queue->root) == 0);
    }
    CHECK(rmdir(queue->path) == 0);
}

static int fixture_authority_ready(Fixture* fixture, TpRetirementReceiptAuthority* authority)
{
    int valid = fixture_start(fixture, 4);
    if (valid) valid = tp_retirement_store_plan(&fixture->store, 2, 3, 1024);
    if (valid) valid = fixture_publish(fixture, "shard.jsonl", "{}\n", 3, 3);
    if (valid) valid = fixture_receipt(fixture, "shard.jsonl", "{}\n", 3);
    if (valid) valid = tp_retirement_store_receipt_authority(&fixture->store, fixture->private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, authority);
    return valid;
}

static void test_queue_authority_copy(void)
{
    Fixture fixture;
    QueueAuthorityFixture queue;
    TpRetirementReceiptAuthority authority;
    CHECK(fixture_authority_ready(&fixture, &authority));
    CHECK(fixture_queue_start(&queue));
    CHECK(tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    struct stat sealed, after;
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &sealed, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISREG(sealed.st_mode) && (sealed.st_mode & 0777) == 0400);
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &after, AT_SYMLINK_NOFOLLOW) == 0 &&
          after.st_dev == sealed.st_dev && after.st_ino == sealed.st_ino);
    tp_retirement_store_close(&fixture.store);
    CHECK(tp_retirement_store_authority_reopen(fixture.root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, queue.root,
        "job-1", 3, digest_a, digest_b, &authority));
    fixture_queue_stop(&queue);
    fixture_stop(&fixture);
}

static void test_queue_authority_invalid_source(void)
{
    Fixture fixture;
    QueueAuthorityFixture queue;
    TpRetirementReceiptAuthority authority;
    struct stat info;
    CHECK(fixture_authority_ready(&fixture, &authority));
    CHECK(fixture_queue_start(&queue));
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-2", 2, digest_a, digest_b, &authority));
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_b, digest_b, &authority));
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_a, &authority));
    TpRetirementReceiptAuthority altered = authority;
    altered.receipt_sha256[0] = altered.receipt_sha256[0] == '0' ? '1' : '0';
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &altered));
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, fixture.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info, AT_SYMLINK_NOFOLLOW) != 0);
    CHECK(unlinkat(fixture.private_root, "authority-job-1-2.txt", 0) == 0);
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info, AT_SYMLINK_NOFOLLOW) != 0);
    fixture_queue_stop(&queue);
    fixture_stop(&fixture);

    CHECK(fixture_authority_ready(&fixture, &authority));
    CHECK(fixture_queue_start(&queue));
    CHECK(unlinkat(fixture.root, "shard.jsonl", 0) == 0);
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info, AT_SYMLINK_NOFOLLOW) != 0);
    fixture_queue_stop(&queue);
    fixture_stop(&fixture);

    CHECK(fixture_authority_ready(&fixture, &authority));
    CHECK(fixture_queue_start(&queue));
    CHECK(unlinkat(fixture.root, "shard.jsonl", 0) == 0);
    int replacement = openat(fixture.root, "shard.jsonl",
        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    CHECK(replacement >= 0 && write(replacement, "{}\n", 3) == 3 &&
          fchmod(replacement, 0400) == 0 && close(replacement) == 0);
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info, AT_SYMLINK_NOFOLLOW) != 0);
    fixture_queue_stop(&queue);
    fixture_stop(&fixture);
}

static void test_queue_authority_interrupted(void)
{
    Fixture fixture;
    QueueAuthorityFixture queue;
    TpRetirementReceiptAuthority authority;
    struct stat info;
    CHECK(fixture_authority_ready(&fixture, &authority));
    CHECK(fixture_queue_start(&queue));
    int pending = openat(queue.root, "authority-job-1-2.txt.pending",
        O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    CHECK(pending >= 0 && close(pending) == 0);
    CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
        "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fstatat(queue.root, "authority-job-1-2.txt.pending", &info, AT_SYMLINK_NOFOLLOW) == 0);
    CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info, AT_SYMLINK_NOFOLLOW) != 0);
    fixture_queue_stop(&queue);
    fixture_stop(&fixture);

    for (unsigned fail_at = 1; fail_at <= 4; ++fail_at)
    {
        CHECK(fixture_authority_ready(&fixture, &authority));
        CHECK(fixture_queue_start(&queue));
        tp_retirement_store_test_sync_calls = 0;
        tp_retirement_store_test_fail_sync = fail_at;
        CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
            "job-1", 2, digest_a, digest_b, &authority));
        tp_retirement_store_test_fail_sync = 0;
        CHECK(fstatat(queue.root, "authority-job-1-2.txt", &info,
                      AT_SYMLINK_NOFOLLOW) == (fail_at >= 3 ? 0 : -1));
        CHECK(fstatat(queue.root, "authority-job-1-2.txt.pending", &info,
                      AT_SYMLINK_NOFOLLOW) == (fail_at <= 3 ? 0 : -1));
        CHECK(!tp_retirement_store_authority_copy(fixture.root, fixture.private_root, queue.root,
            "job-1", 2, digest_a, digest_b, &authority));
        fixture_queue_stop(&queue);
        fixture_stop(&fixture);
    }
}

static void test_publication_and_authority(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 4, 1024));
    CHECK(mkdirat(fixture.root, "evidence", 0700) == 0);
    CHECK(fixture_publish(&fixture, "evidence/invocations.jsonl", "first\n", 6, 6));
    CHECK(fixture_receipt(&fixture, "evidence/invocations.jsonl", "first\n", 6));
    CHECK(fixture.store.count == 2 && fixture.store.total > 14);
    CHECK(tp_retirement_store_validate(&fixture.store));
    TpRetirementReceiptAuthority authority;
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
                                                TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2,
                                                digest_a, digest_b, &authority));
    CHECK(tp_retirement_store_authority_matches(&fixture.store, fixture.private_root,
                                                TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2,
                                                digest_a, digest_b, &authority));
    CHECK(tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
                                               "job-1", 2, digest_a, digest_b, &authority));
    TpRetirementReceiptAuthority substituted = authority;
    substituted.receipt_sha256[0] = substituted.receipt_sha256[0] == '0' ? '1' : '0';
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
                                                "job-1", 2, digest_a, digest_b, &substituted));
    CHECK(!tp_retirement_store_authority_matches(&fixture.store, fixture.private_root,
                                                 TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2,
                                                 digest_a, digest_b, &substituted));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);
}

static void test_missing_authority_and_invalid_input(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(fixture_publish(&fixture, TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "receipt\n", 8, 8));
    CHECK(!tp_retirement_store_authority_matches(&fixture.store, fixture.private_root,
                                                 TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2,
                                                 digest_a, digest_b, NULL));
    fixture_stop(&fixture);
    CHECK(fixture_start(&fixture, 4));
    TpRetirementPending pending;
    CHECK(!tp_retirement_store_begin(&fixture.store, "../escape", 1024, &pending));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);
}

static void test_reopened_shard_inventory(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(mkdirat(fixture.root, "evidence", 0700) == 0);
    CHECK(fixture_publish(&fixture, "evidence/shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "evidence/shard.jsonl", "{}\n", 3));
    TpRetirementReceiptAuthority authority;
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    tp_retirement_store_close(&fixture.store);
    CHECK(tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    int directory = openat(fixture.root, "evidence", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    CHECK(directory >= 0 && unlinkat(directory, "shard.jsonl", 0) == 0);
    if (directory >= 0) CHECK(close(directory) == 0);
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(mkdirat(fixture.root, "evidence", 0700) == 0);
    CHECK(fixture_publish(&fixture, "evidence/shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "evidence/shard.jsonl", "{}\n", 3));
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    tp_retirement_store_close(&fixture.store);
    directory = openat(fixture.root, "evidence", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    CHECK(directory >= 0 && unlinkat(directory, "shard.jsonl", 0) == 0);
    int replacement = directory >= 0 ? openat(directory, "shard.jsonl",
        O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600) : -1;
    CHECK(replacement >= 0 && write(replacement, "{}\n", 3) == 3 &&
        fchmod(replacement, 0400) == 0 && close(replacement) == 0);
    if (directory >= 0) CHECK(close(directory) == 0);
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "shard.jsonl", "{}\n", 3));
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    tp_retirement_store_close(&fixture.store);
    CHECK(fchmodat(fixture.root, "shard.jsonl", 0600, 0) == 0);
    replacement = openat(fixture.root, "shard.jsonl", O_WRONLY | O_NOFOLLOW);
    CHECK(replacement >= 0 && ftruncate(replacement, 2) == 0 &&
        fchmod(replacement, 0400) == 0 && close(replacement) == 0);
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    fixture_stop(&fixture);
}

static void test_malformed_receipt_and_shard_inventory(void)
{
    Fixture fixture;
    TpRetirementReceiptAuthority authority;
    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt_count(&fixture, "shard.jsonl", "{}\n", 3, 2));
    CHECK(!tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fixture.store.failed && authority.receipt_sha256[0] == 0);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt_count(&fixture, "missing.jsonl", "{}\n", 3, 1));
    CHECK(!tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "shard.jsonl", "{}\n", 3));
    CHECK(!tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_b, digest_a, &authority));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);
}

static void test_private_authority_failure(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "shard.jsonl", "{}\n", 3));
    TpRetirementReceiptAuthority authority;
    CHECK(!tp_retirement_store_receipt_authority(&fixture.store, fixture.root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fixture.store.failed && authority.receipt_sha256[0] == 0);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "shard.jsonl", "{}\n", 3));
    tp_retirement_store_test_sync_calls = 0;
    tp_retirement_store_test_fail_sync = 2;
    CHECK(!tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fixture.store.failed && authority.authority_sha256[0] == 0);
    struct stat info;
    CHECK(fstatat(fixture.private_root, "authority-job-1-2.txt.pending", &info,
                  AT_SYMLINK_NOFOLLOW) == 0 && info.st_size > 0);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 1024));
    CHECK(fixture_publish(&fixture, "shard.jsonl", "{}\n", 3, 3));
    CHECK(fixture_receipt(&fixture, "shard.jsonl", "{}\n", 3));
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(fchmodat(fixture.private_root, "authority-job-1-2.txt", 0600, 0) == 0);
    int descriptor = openat(fixture.private_root, "authority-job-1-2.txt", O_WRONLY | O_NOFOLLOW);
    CHECK(descriptor >= 0 && write(descriptor, "x", 1) == 1 && close(descriptor) == 0);
    CHECK(fchmodat(fixture.private_root, "authority-job-1-2.txt", 0400, 0) == 0);
    CHECK(!tp_retirement_store_authority_matches(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    fixture_stop(&fixture);
}

static void test_duplicate_and_planted_names(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 1));
    CHECK(fixture_publish(&fixture, "one", "a", 1, 1));
    TpRetirementPending pending;
    CHECK(!tp_retirement_store_begin(&fixture.store, "one", 1024, &pending));
    CHECK(fixture.store.failed && fixture.store.count == 1);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    int planted = openat(fixture.root, "one", O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(planted >= 0 && close(planted) == 0);
    CHECK(!fixture_publish(&fixture, "one", "a", 1, 1));
    CHECK(fixture.store.failed && fixture.store.count == 0);
    struct stat info;
    CHECK(fstatat(fixture.root, "one", &info, AT_SYMLINK_NOFOLLOW) == 0 && info.st_size == 0);
    CHECK(fstatat(fixture.root, "one.pending", &info, AT_SYMLINK_NOFOLLOW) == 0 && info.st_size == 1);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    planted = openat(fixture.root, "one.pending", O_CREAT | O_EXCL | O_WRONLY, 0600);
    CHECK(planted >= 0 && close(planted) == 0);
    CHECK(!tp_retirement_store_begin(&fixture.store, "one", 1024, &pending));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);
}

static void test_failed_writer_and_sync(void)
{
    Fixture fixture;
    struct stat info;
    CHECK(fixture_start(&fixture, 4));
    CHECK(!fixture_publish(&fixture, "short", "a", 1, 2));
    CHECK(fixture.store.failed && fixture.store.count == 0 &&
          fstatat(fixture.root, "short.pending", &info, AT_SYMLINK_NOFOLLOW) == 0 && info.st_size == 1);
    CHECK(fstatat(fixture.root, "short", &info, AT_SYMLINK_NOFOLLOW) != 0);
    fixture_stop(&fixture);

    for (unsigned fail_at = 1; fail_at <= 4; ++fail_at)
    {
        CHECK(fixture_start(&fixture, 4));
        tp_retirement_store_test_fail_sync = fail_at;
        CHECK(!fixture_publish(&fixture, "fault", "a", 1, 1));
        CHECK(fixture.store.failed && fixture.store.count == 0);
        CHECK(fstatat(fixture.root, "fault", &info, AT_SYMLINK_NOFOLLOW) == (fail_at >= 3 ? 0 : -1));
        CHECK(fstatat(fixture.root, "fault.pending", &info, AT_SYMLINK_NOFOLLOW) == (fail_at <= 3 ? 0 : -1));
        fixture_stop(&fixture);
    }
}

static void test_short_write_preserves_pending(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0)
    {
        Fixture fixture;
        int valid = fixture_start(&fixture, 4);
        TpRetirementPending pending = {0};
        struct rlimit limit = {.rlim_cur = 1, .rlim_max = 1};
        if (valid) valid = tp_retirement_store_begin(&fixture.store, "limited", 8192, &pending);
        if (valid) valid = signal(SIGXFSZ, SIG_IGN) != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0;
        char data[4096];
        memset(data, 'x', sizeof(data));
        if (valid)
        {
            Sha256 hash;
            char digest[65];
            sha256_init(&hash);
            sha256_add(&hash, data, sizeof(data));
            sha256_finish_hex(&hash, (char8*)digest);
            size_t written = fwrite(data, 1, sizeof(data), pending.stream);
            valid = written != sizeof(data) ||
                    !tp_retirement_store_publish(&fixture.store, &pending, sizeof(data), digest);
        }
        if (pending.stream) tp_retirement_store_abort(&fixture.store, &pending);
        struct stat info;
        valid = valid && fixture.store.failed && fixture.store.count == 0 &&
                fstatat(fixture.root, "limited.pending", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                fstatat(fixture.root, "limited", &info, AT_SYMLINK_NOFOLLOW) != 0;
        fixture_stop(&fixture);
        _exit(valid ? 0 : 1);
    }
    if (child > 0)
    {
        int status = 0;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

static void test_replaced_bytes_and_directory(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(fixture_publish(&fixture, "shard", "correct", 7, 7));
    CHECK(fchmodat(fixture.root, "shard", 0600, 0) == 0);
    int descriptor = openat(fixture.root, "shard", O_WRONLY | O_NOFOLLOW);
    CHECK(descriptor >= 0 && write(descriptor, "x", 1) == 1 && close(descriptor) == 0);
    CHECK(fchmodat(fixture.root, "shard", 0400, 0) == 0);
    CHECK(!tp_retirement_store_validate(&fixture.store) && fixture.store.failed);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(mkdirat(fixture.root, "nested", 0700) == 0);
    CHECK(fixture_publish(&fixture, "nested/shard", "correct", 7, 7));
    CHECK(renameat(fixture.root, "nested", fixture.root, "old") == 0);
    CHECK(mkdirat(fixture.root, "nested", 0700) == 0);
    CHECK(!tp_retirement_store_validate(&fixture.store) && fixture.store.failed);
    fixture_stop(&fixture);
}

static void test_inventory_bounds(void)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    TpRetirementPending pending;
    CHECK(!tp_retirement_store_begin(&fixture.store, "oversized", TP_RETIREMENT_STORE_FILE_BYTES + 1, &pending));
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(fixture_publish(&fixture, "shard", "a", 1, 1));
    fixture.files[1] = fixture.files[0];
    fixture.store.count = 2;
    fixture.store.total = 2;
    CHECK(!tp_retirement_store_validate(&fixture.store) && fixture.store.failed);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(!tp_retirement_store_plan(&fixture.store, 4, 4093, 0));
    CHECK(fixture.store.failed);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 2, 3, 0));
    CHECK(fixture_publish(&fixture, "shard", "a", 1, 1));
    CHECK(!tp_retirement_store_validate(&fixture.store) && fixture.store.failed);
    fixture_stop(&fixture);

    CHECK(fixture_start(&fixture, 4));
    fixture.store.total = TP_RETIREMENT_STORE_TOTAL_BYTES;
    CHECK(!fixture_publish(&fixture, "total", "a", 1, 1));
    CHECK(fixture.store.failed && fixture.store.count == 0);
    fixture_stop(&fixture);
}

static int fixture_publish_file(Fixture* fixture, char const* source_root,
                                char const* source_name, char const* published_name)
{
    char source_path[512];
    int length = snprintf(source_path, sizeof(source_path), "%s/%s", source_root, source_name);
    FILE* input = length > 0 && (size_t)length < sizeof(source_path) ? fopen(source_path, "rb") : NULL;
    TpRetirementPending pending = {0};
    int valid = input && tp_retirement_store_begin(&fixture->store, published_name,
                                                   TP_RETIREMENT_STORE_FILE_BYTES, &pending);
    Sha256 hash;
    sha256_init(&hash);
    uint64_t bytes = 0;
    unsigned char buffer[65536];
    while (valid)
    {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count && (bytes > TP_RETIREMENT_STORE_FILE_BYTES - count ||
                      fwrite(buffer, 1, count, pending.stream) != count)) valid = 0;
        if (valid && count)
        {
            sha256_add(&hash, buffer, count);
            bytes += count;
        }
        if (count < sizeof(buffer))
        {
            valid = valid && !ferror(input);
            break;
        }
    }
    if (input && fclose(input) != 0) valid = 0;
    if (valid)
    {
        char digest[65];
        sha256_finish_hex(&hash, (char8*)digest);
        valid = tp_retirement_store_publish(&fixture->store, &pending, bytes, digest);
    }
    else if (pending.stream) tp_retirement_store_abort(&fixture->store, &pending);
    return valid;
}

static void test_actual_encoder_fixture(char const* source_root)
{
    Fixture fixture;
    CHECK(fixture_start(&fixture, 4));
    CHECK(tp_retirement_store_plan(&fixture.store, 3, 3, 1024));
    CHECK(fixture_publish_file(&fixture, source_root, "retirement-shard-0.jsonl",
                               "retirement-shard-0.jsonl"));
    CHECK(fixture_publish_file(&fixture, source_root, "retirement-shard-1.jsonl",
                               "retirement-shard-1.jsonl"));
    CHECK(fixture_publish_file(&fixture, source_root, "retirement-invocation-receipt.json",
                               TP_RETIREMENT_EXECUTION_RECEIPT_PATH));
    TpRetirementReceiptAuthority authority;
    CHECK(tp_retirement_store_receipt_authority(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    CHECK(tp_retirement_store_authority_matches(&fixture.store, fixture.private_root,
        TP_RETIREMENT_EXECUTION_RECEIPT_PATH, "job-1", 2, digest_a, digest_b, &authority));
    tp_retirement_store_close(&fixture.store);
    CHECK(tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    /* Fresh replay must validate every shard, not merely the two trusted
     * receipt files. Preserve the authority while removing one sealed inode. */
    CHECK(unlinkat(fixture.root, "retirement-shard-1.jsonl", 0) == 0);
    CHECK(!tp_retirement_store_authority_reopen(fixture.root, fixture.private_root,
        "job-1", 2, digest_a, digest_b, &authority));
    fixture_stop(&fixture);
}

int main(int argc, char** argv)
{
    test_publication_and_authority();
    test_queue_authority_copy();
    test_queue_authority_invalid_source();
    test_queue_authority_interrupted();
    test_missing_authority_and_invalid_input();
    test_reopened_shard_inventory();
    test_malformed_receipt_and_shard_inventory();
    test_private_authority_failure();
    test_duplicate_and_planted_names();
    test_failed_writer_and_sync();
    test_short_write_preserves_pending();
    test_replaced_bytes_and_directory();
    test_inventory_bounds();
    if (argc == 2) test_actual_encoder_fixture(argv[1]);
    fprintf(stderr, "STORE_TEST assertions=%u failures=%u\n", assertions, failures);
    return failures ? 1 : 0;
}
#endif
