/* Focused regression coverage for the fixed gateway's atomic idle-only submit.
 * This includes the production queue implementation directly, following the
 * native service test executable, but keeps the required workflow check small.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "queue.c"
#include "exclusive_admission.c"
#include <stdlib.h>

static u32 assertions;
static u32 failures;

#define CHECK(expression) do { \
    assertions += 1; \
    if (!(expression)) { \
        failures += 1; \
        fprintf(stderr, "EXCLUSIVE_ADMISSION_TEST failure line=%d: %s\n", __LINE__, #expression); \
    } \
} while (0)

static BqRequest make_request(char const* key, char const* recipe)
{
    String8 fields[BQ_FIELD_COUNT] = {
        S8("github-actions"), string_from_pointer(key), string_from_pointer(recipe),
        S8("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        S8("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
    };
    BqRequest request = {0};
    CHECK(bq_request_make(fields, &request) == BQ_OK);
    return request;
}

static void remove_fixture(BqQueue* queue, char const* path)
{
    bq_close(queue);
    int directory = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(directory >= 0);
    if (directory >= 0)
    {
        CHECK(unlinkat(directory, "journal", 0) == 0);
        CHECK(unlinkat(directory, "writer.lock", 0) == 0);
        close(directory);
    }
    CHECK(rmdir(path) == 0);
}

int main(void)
{
    char path[] = "/tmp/buster-exclusive-admission-XXXXXX";
    BqQueue queue = {.directory_fd = -1, .lock_fd = -1, .journal_fd = -1};
    bool created = mkdtemp(path) != NULL;
    CHECK(created);
    if (!created)
    {
        return 1;
    }

    CHECK(bq_open(&queue, path) == BQ_OK);
    BqRequest unsupported = make_request("unsupported", "fake-success-v1");
    BqRequest first = make_request("first", "validate-buster-v1");
    BqRequest second = make_request("second", "validate-buster-v1");
    BqRequest third = make_request("third", "validate-buster-v1");
    u64 id = 0, first_id = 0, second_id = 0, token = 0;

    CHECK(bq_submit_exclusive(&queue, &unsupported, &id) == BQ_UNSUPPORTED && id == 0);
    CHECK(bq_submit_exclusive(&queue, &first, &first_id) == BQ_OK && first_id != 0);
    u64 sequence = queue.state.sequence;
    CHECK(bq_submit_exclusive(&queue, &first, &id) == BQ_OK && id == first_id &&
          queue.state.sequence == sequence);

    BqRequest conflict = first;
    bq_field(&conflict, 4).pointer[0] = 'c';
    CHECK(bq_submit_exclusive(&queue, &conflict, &id) == BQ_CONFLICT && id == 0 &&
          queue.state.sequence == sequence);
    CHECK(bq_submit_exclusive(&queue, &second, &id) == BQ_BUSY && id == 0 &&
          queue.state.sequence == sequence);

    CHECK(bq_cancel(&queue, first_id) == BQ_OK && bq_pending(&queue.state) == 0);
    CHECK(bq_submit_exclusive(&queue, &second, &second_id) == BQ_OK &&
          second_id != 0 && second_id != first_id);
    CHECK(bq_reserve(&queue, &id, &token) == BQ_OK && id == second_id && token != 0);
    CHECK(bq_submit_exclusive(&queue, &second, &id) == BQ_OK && id == second_id);
    CHECK(bq_submit_exclusive(&queue, &third, &id) == BQ_BUSY && id == 0);

    bq_close(&queue);
    CHECK(bq_open(&queue, path) == BQ_OK && queue.needs_reconciliation);
    CHECK(bq_submit_exclusive(&queue, &second, &id) == BQ_OK && id == second_id);
    CHECK(bq_submit_exclusive(&queue, &third, &id) == BQ_RECONCILIATION_REQUIRED && id == 0);

    remove_fixture(&queue, path);
    printf("EXCLUSIVE_ADMISSION_TEST assertions=%u failures=%u\n", assertions, failures);
    return failures ? 1 : 0;
}
