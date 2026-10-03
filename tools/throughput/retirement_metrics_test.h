/* Failure-first tests for the strict per-input metrics reader. Included only by
 * tests.c. test_metrics_render writes synthetic `-fmetrics-out` text for a
 * frozen batch contract; one mutation at a time must reject the whole batch.
 * The same renderer backs the batch child of the measurement fixtures. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_METRICS_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_METRICS_TEST_H
#include "retirement_metrics.h"

typedef struct TestMetricsInput
{
    uint64_t start_ns, end_ns, arena_peak_bytes;
} TestMetricsInput;

enum
{
    TEST_METRICS_VALID, TEST_METRICS_REORDER, TEST_METRICS_UNKNOWN_KEY, TEST_METRICS_VERSION,
    TEST_METRICS_LEADING_ZERO, TEST_METRICS_UPPER_HEX, TEST_METRICS_EXTRA_INPUT, TEST_METRICS_MISSING_INPUT,
    TEST_METRICS_STATUS, TEST_METRICS_DIAGNOSTIC, TEST_METRICS_OVERLAP, TEST_METRICS_PHASES,
    TEST_METRICS_TOTAL, TEST_METRICS_WORKERS, TEST_METRICS_CONCURRENT, TEST_METRICS_KEEP_GOING,
    TEST_METRICS_EXIT, TEST_METRICS_ERROR, TEST_METRICS_TARGET, TEST_METRICS_ALLOCATOR,
    TEST_METRICS_PATH, TEST_METRICS_OBJECT, TEST_METRICS_TRUNCATED, TEST_METRICS_MESSAGE_BYTES,
    TEST_METRICS_NAME_TRUNCATED, TEST_METRICS_FUNCTION_ORDER, TEST_METRICS_UNREQUESTED_FUNCTIONS,
    TEST_METRICS_TRAILING, TEST_METRICS_NO_NEWLINE, TEST_METRICS_WALL, TEST_METRICS_ARENA,
    TEST_METRICS_MEASURED, TEST_METRICS_NOT_RUN, TEST_METRICS_ACTION, TEST_METRICS_SCHEMA,
    TEST_METRICS_DOUBLE_SPACE, TEST_METRICS_TAG, TEST_METRICS_COUNT_MISMATCH, TEST_METRICS_DIGEST_WORD,
    TEST_METRICS_JOBS, TEST_METRICS_FUNCTION_SIZES, TEST_METRICS_PREFIXED_PATH, TEST_METRICS_PARENT_PATH,
    TEST_METRICS_MUTATIONS
};

typedef struct TestMetricsText
{
    char* bytes;
    size_t size, capacity;
    int failed;
} TestMetricsText;

static void test_metrics_put(TestMetricsText* text, char const* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = text->failed ? -1 : vsnprintf(text->bytes + text->size, text->capacity - text->size, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= text->capacity - text->size) text->failed = 1;
    else text->size += (size_t)length;
}

static void test_metrics_hex(TestMetricsText* text, char const* key, char const* value, int upper)
{
    test_metrics_put(text, " %s=", key);
    if (!value || !value[0]) test_metrics_put(text, "-");
    for (size_t i = 0; value && value[i]; ++i)
        test_metrics_put(text, upper ? "%02X" : "%02x", (unsigned char)value[i]);
}

/* One batch's metrics text with `inputs` timings (members, then controls). */
static size_t test_metrics_render(char* bytes, size_t capacity, TpRetirementBatchContract const* contract,
    uint64_t wall_ns, TestMetricsInput const* inputs, unsigned mutation)
{
    TestMetricsText text = {.bytes = bytes, .capacity = capacity, .failed = !bytes || !contract};
    unsigned count = contract ? contract->input_count : 0, statuses[3] = {0};
    char const* first_error = "driver.none";
    for (unsigned i = 0; !text.failed && i < count; ++i)
    {
        char const* status = contract->inputs[i].status;
        statuses[!strcmp(status, "ok") ? 0 : !strcmp(status, "rejected") ? 1 : 2] += 1;
        if (strcmp(status, "ok") && !strcmp(first_error, "driver.none")) first_error = contract->inputs[i].error;
    }
    test_metrics_put(&text, "CC_METRICS version=%u schema=%s inputs=%u records=%u ok=%u rejected=%u failed=%u "
        "not_run=%u prebuilt=0 error=%s exit_status=%u action=%s target=%s allocator=%s compile_jobs=%u "
        "compilation_workers=%u intervals=%s keep_going=%u function_sizes=%u wall_ns=%" PRIu64
        " peak_rss_bytes=1048576\n",
        mutation == TEST_METRICS_VERSION ? 2u : 1u,
        mutation == TEST_METRICS_SCHEMA ? "other-metrics" : "buster-cc-metrics",
        count, mutation == TEST_METRICS_COUNT_MISMATCH ? count + 1 : count, statuses[0], statuses[1], statuses[2],
        mutation == TEST_METRICS_NOT_RUN ? 1u : 0u,
        mutation == TEST_METRICS_ERROR ? "driver.parse" : first_error,
        mutation == TEST_METRICS_EXIT ? contract->exit_status ^ 1u : contract->exit_status,
        mutation == TEST_METRICS_ACTION ? "link" : "object",
        mutation == TEST_METRICS_TARGET ? "aarch64-linux" : contract->target,
        mutation == TEST_METRICS_ALLOCATOR ? "quality-other" : contract->allocator,
        mutation == TEST_METRICS_JOBS ? 2u : 1u, mutation == TEST_METRICS_WORKERS ? 2u : 1u,
        mutation == TEST_METRICS_CONCURRENT ? "concurrent" : "serial",
        mutation == TEST_METRICS_KEEP_GOING ? 0u : 1u,
        mutation == TEST_METRICS_UNREQUESTED_FUNCTIONS ? 0u : mutation == TEST_METRICS_FUNCTION_SIZES ? 2u : 1u,
        mutation == TEST_METRICS_WALL ? inputs[count - 1].end_ns - 1 : wall_ns);
    if (mutation == TEST_METRICS_DOUBLE_SPACE && !text.failed)
    {
        memmove(text.bytes + 11, text.bytes + 10, text.size - 10);
        text.bytes[10] = ' ';
        ++text.size;
    }
    unsigned rendered = mutation == TEST_METRICS_MISSING_INPUT ? count - 1 :
        mutation == TEST_METRICS_EXTRA_INPUT ? count + 1 : count;
    for (unsigned i = 0; !text.failed && i < rendered; ++i)
    {
        TpRetirementBatchInput const* input = &contract->inputs[i < count ? i : count - 1];
        TestMetricsInput timing = inputs[i < count ? i : count - 1];
        int ok = !strcmp(input->status, "ok");
        uint64_t start = timing.start_ns, end = timing.end_ns;
        if (mutation == TEST_METRICS_OVERLAP && i == 1) start = inputs[0].end_ns - 1;
        uint64_t total = end - start + (mutation == TEST_METRICS_TOTAL && !i ? 1 : 0);
        char path[1024];
        /* The exact fixture path; a prefix, a `..` detour or another file is rejected. */
        snprintf(path, sizeof(path), "%s%s",
                 !i && mutation == TEST_METRICS_PREFIXED_PATH ? "/checkout/" :
                 !i && mutation == TEST_METRICS_PARENT_PATH ? "src/../" : "",
                 !i && mutation == TEST_METRICS_PATH ? "elsewhere/other.c" : input->fixture);
        char const* status = mutation == TEST_METRICS_STATUS && i == count - 1 ?
            (strcmp(input->status, "failed") ? "failed" : "rejected") : input->status;
        test_metrics_put(&text, "CC_METRICS_INPUT version=1 index=%u status=%s error=%s", i, status, input->error);
        if (mutation == TEST_METRICS_REORDER && !i) test_metrics_put(&text, " warnings=0 errors=%u", ok ? 0u : 1u);
        else test_metrics_put(&text, " errors=%s warnings=0", mutation == TEST_METRICS_LEADING_ZERO && !i ?
            "00" : ok ? "0" : "1");
        test_metrics_put(&text, " measured=%u start_ns=%" PRIu64 " end_ns=%" PRIu64 " total_ns=%" PRIu64
            " read_ns=%" PRIu64 " preprocess_ns=0 parse_ns=0 analysis_ns=0 ir_ns=0 codegen_ns=%" PRIu64
            " object_ns=0 emit_ns=0 arena_peak_bytes=%" PRIu64 " arena_retained_bytes=0 source_bytes=100"
            " preprocessed_tokens=10 object_file_bytes=%u text_bytes=%u rodata_bytes=0 data_bytes=0 bss_bytes=0"
            " tdata_bytes=0 tbss_bytes=0 initializer_bytes=0 unwind_bytes=0 debug_bytes=0 codegen_functions=%u"
            " instructions=0 values=0 code_bytes=%u stack_frame_bytes=0 max_stack_frame_bytes=0"
            " fallback_functions=0 fallback_records=0 function_records=%u function_records_omitted=0"
            " diagnostic_records=%u diagnostic_digest=%s diagnostic_line=0 diagnostic_column=0",
            mutation == TEST_METRICS_MEASURED && !i ? 0u : 1u, start, end, total, (end - start) / 4,
            mutation == TEST_METRICS_PHASES && !i ? end - start : (end - start) / 2,
            mutation == TEST_METRICS_ARENA && !i ? 0 : timing.arena_peak_bytes,
            ok || (mutation == TEST_METRICS_OBJECT && i == count - 1) ? 1000u : 0u, ok ? 100u : 0u,
            ok ? 1u : 0u, ok ? 100u : 0u, ok ? 1u : 0u, ok ? 0u : 1u,
            mutation == TEST_METRICS_DIAGNOSTIC && !i ?
                "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee" :
                mutation == TEST_METRICS_DIGEST_WORD && !i ? "-" : input->diagnostic_sha256);
        test_metrics_hex(&text, "path_hex", path, mutation == TEST_METRICS_UPPER_HEX && !i);
        test_metrics_hex(&text, "diagnostic_code_hex", ok ? NULL : input->error, 0);
        test_metrics_hex(&text, "diagnostic_path_hex", ok ? NULL : input->fixture, 0);
        test_metrics_put(&text, " message_bytes=%u message_truncated=%u",
            ok ? 0u : mutation == TEST_METRICS_MESSAGE_BYTES ? 6u : 5u,
            mutation == TEST_METRICS_TRUNCATED && !ok ? 1u : 0u);
        test_metrics_hex(&text, "message_hex", ok ? NULL : "error", 0);
        if (mutation == TEST_METRICS_UNKNOWN_KEY && !i) test_metrics_put(&text, " extra=1");
        test_metrics_put(&text, "\n");
        if (mutation == TEST_METRICS_TAG && !i && !text.failed)
        {
            char* tag = strstr(text.bytes, "CC_METRICS_INPUT");
            if (tag) memcpy(tag + 11, "ROW__", 5);
        }
        if (ok)
            test_metrics_put(&text, "CC_METRICS_FUNCTION version=1 input=%u ordinal=%u code_bytes=100 name_bytes=%u"
                " name_truncated=0 name_hex=6d61696e\n", i, mutation == TEST_METRICS_FUNCTION_ORDER && !i ? 1u : 0u,
                mutation == TEST_METRICS_NAME_TRUNCATED && !i ? 1025u : 4u);
    }
    if (mutation == TEST_METRICS_TRAILING) test_metrics_put(&text, "\n");
    if (mutation == TEST_METRICS_NO_NEWLINE && !text.failed && text.size) --text.size;
    return text.failed ? 0 : text.size;
}

/* Serial per-input intervals inside a window of `wall` nanoseconds. */
static void test_metrics_timings(TestMetricsInput* inputs, unsigned count, uint64_t wall)
{
    uint64_t step = wall / (count + 1);
    for (unsigned i = 0; i < count; ++i)
        inputs[i] = (TestMetricsInput){step * i + 1, step * (i + 1), 65536 * (i + 1)};
}

static void test_retirement_metrics(char const* root)
{
    char const* const empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    char const* const object = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    char const* const rejected = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    TpRetirementBatchInput inputs[3] = {
        {"tests/alpha.c", "ok", "driver.none", empty, object, "alpha.o", 1, 3},
        {"tests/beta.c", "ok", "driver.none", empty, object, "beta.o", 1, 7},
        {"tests/control.c", "rejected", "driver.analysis", rejected, NULL, NULL, 0, TP_RETIREMENT_BATCH_NO_ROW}};
    TpRetirementBatchContract contract = {"x86_64-linux", "none", "batch.metrics", inputs, 3, 1,
        TP_RETIREMENT_METRICS_ARTIFACT_BYTES};
    CHECK(tp_retirement_batch_contract_valid(&contract));
    TestMetricsInput timings[3];
    test_metrics_timings(timings, 3, 4000);
    size_t capacity = 1u << 16;
    char* text = (char*)malloc(capacity);
    TpRetirementMemberSample members[3];
    CHECK(text != NULL);
    size_t size = text ? test_metrics_render(text, capacity, &contract, 4000, timings, TEST_METRICS_VALID) : 0;
    CHECK(size && tp_retirement_metrics_check((unsigned char const*)text, size, &contract, 5000, members, 2));
    CHECK(members[0].interval_ns == timings[0].end_ns - timings[0].start_ns &&
          members[0].peak_memory_bytes == 65536 && members[1].peak_memory_bytes == 131072 &&
          members[0].row == 3 && members[1].row == 7);
    /* The supervisor's process interval must contain the compiler's window;
     * zero means an untimed batch. */
    CHECK(tp_retirement_metrics_check((unsigned char const*)text, size, &contract, 0, members, 2));
    CHECK(!tp_retirement_metrics_check((unsigned char const*)text, size, &contract, 3999, members, 2) &&
          !members[0].interval_ns);
    CHECK(!tp_retirement_metrics_check((unsigned char const*)text, size, &contract, 5000, members, 1));
    char path[TP_PATH_CAP];
    CHECK(tp_path(path, root, "retirement-metrics-fixture.txt"));
    FILE* file = fopen(path, "wb");
    CHECK(file && fwrite(text, 1, size, file) == size);
    if (file) CHECK(fclose(file) == 0);
    for (unsigned mutation = 1; text && mutation < TEST_METRICS_MUTATIONS; ++mutation)
    {
        size = test_metrics_render(text, capacity, &contract, 4000, timings, mutation);
        int accepted = size && tp_retirement_metrics_check((unsigned char const*)text, size, &contract, 5000,
                                                           members, 2);
        if (accepted) fprintf(stderr, "metrics mutation %u was accepted\n", mutation);
        CHECK(size && !accepted);
    }
    /* Frozen contracts are consistent before any launch. */
    TpRetirementBatchContract bad = contract;
    bad.exit_status = 0;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    TpRetirementBatchInput changed[3];
    memcpy(changed, inputs, sizeof(changed));
    bad = contract;
    bad.inputs = changed;
    changed[1].artifact = "alpha.o";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[1] = inputs[1];
    changed[2].object_sha256 = object;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[2] = inputs[2];
    changed[2].member = 1;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[2] = inputs[2];
    changed[0] = inputs[2];
    changed[2] = inputs[0];
    CHECK(!tp_retirement_batch_contract_valid(&bad)); /* Members precede controls. */
    memcpy(changed, inputs, sizeof(changed));
    changed[0].error = "driver.parse";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0] = inputs[0];
    changed[0].fixture = "tests/../alpha.c";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0].fixture = "tests/al\tpha.c";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0] = inputs[0];
    changed[1].artifact = "batch.metrics";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[1] = inputs[1];
    changed[2].status = "not_run";
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[2] = inputs[2];
    /* Members name distinct census rows in ascending order; a control never
     * repeats a row already in the batch. */
    changed[0].row = TP_RETIREMENT_BATCH_NO_ROW;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0].row = 7;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0].row = 9;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[0] = inputs[0];
    changed[2].row = 3;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    changed[2].row = 5;
    CHECK(tp_retirement_batch_contract_valid(&bad));
    changed[2] = inputs[2];
    CHECK(tp_retirement_batch_contract_valid(&bad));
    /* The output digest is the canonical JSON list of per-input objects. */
    char digest[65], contract_digest[65];
    char const* objects[] = {object, NULL};
    Sha256 hash;
    sha256_init(&hash);
    char expected_json[140];
    snprintf(expected_json, sizeof(expected_json), "[\"%s\",null]", object);
    sha256_add(&hash, expected_json, strlen(expected_json));
    char expected[65];
    sha256_finish_hex(&hash, expected);
    CHECK(tp_retirement_batch_output_digest(objects, 2, digest) && !strcmp(digest, expected));
    objects[1] = "not-a-digest";
    CHECK(!tp_retirement_batch_output_digest(objects, 2, digest) && !digest[0]);
    CHECK(tp_retirement_batch_contract_digest(&contract, contract_digest));
    changed[2].diagnostic_sha256 = empty;
    CHECK(tp_retirement_batch_contract_digest(&bad, digest) && strcmp(digest, contract_digest));
    changed[2] = inputs[2];
    changed[1].row = 8;
    CHECK(tp_retirement_batch_contract_digest(&bad, digest) && strcmp(digest, contract_digest));
    changed[1] = inputs[1];
    /* The reviewed metrics bound is part of the contract: an artifact above
     * it rejects, and a missing or oversized bound is inconsistent. */
    size = test_metrics_render(text, capacity, &contract, 4000, timings, TEST_METRICS_VALID);
    bad = contract;
    bad.metrics_bytes_max = size - 1;
    CHECK(tp_retirement_batch_contract_valid(&bad) &&
          tp_retirement_batch_contract_digest(&bad, digest) && strcmp(digest, contract_digest));
    CHECK(size && !tp_retirement_metrics_check((unsigned char const*)text, size, &bad, 5000, members, 2));
    bad.metrics_bytes_max = size;
    CHECK(tp_retirement_metrics_check((unsigned char const*)text, size, &bad, 5000, members, 2));
    bad.metrics_bytes_max = 0;
    CHECK(!tp_retirement_batch_contract_valid(&bad));
    bad.metrics_bytes_max = TP_RETIREMENT_METRICS_ARTIFACT_BYTES + 1;
    CHECK(!tp_retirement_batch_contract_valid(&bad));

    /* The response file: one quoted line per input in frozen order, with `"`
     * and `\` escaped; its leaf is named by its own SHA-256. */
    char list[256], leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP];
    char const* const canonical = "\"tests/alpha.c\"\n\"tests/beta.c\"\n\"tests/control.c\"\n";
    uint64_t list_size = tp_retirement_batch_input_list(&contract, list, sizeof(list));
    CHECK(list_size == strlen(canonical) && !memcmp(list, canonical, (size_t)list_size) &&
          tp_retirement_batch_input_list(&contract, NULL, 0) == list_size);
    sha256_init(&hash);
    sha256_add(&hash, canonical, (u64)list_size);
    sha256_finish_hex(&hash, expected);
    CHECK(tp_retirement_batch_input_list_digest(&contract, digest) && !strcmp(digest, expected));
    CHECK(tp_retirement_batch_input_list_leaf(&contract, leaf) && !strncmp(leaf, "retirement-inputs-", 18) &&
          !strncmp(leaf + 18, expected, 64) && !strcmp(leaf + 82, ".rsp"));
    CHECK(!tp_retirement_batch_input_list(&contract, list, list_size - 1));
    memcpy(changed, inputs, sizeof(changed));
    bad = contract;
    bad.inputs = changed;
    changed[0].fixture = "tests/a \"quoted\" \\name.c";
    list_size = tp_retirement_batch_input_list(&bad, list, sizeof(list));
    CHECK(list_size && !memcmp(list, "\"tests/a \\\"quoted\\\" \\\\name.c\"\n", 30) &&
          tp_retirement_batch_contract_valid(&bad) && tp_retirement_batch_contract_digest(&bad, digest) &&
          strcmp(digest, contract_digest));
    /* A leading `@` is response-file nesting and a leading `-` an option. */
    changed[0].fixture = "@tests/alpha.c";
    CHECK(!tp_retirement_batch_input_list(&bad, NULL, 0) && !tp_retirement_batch_contract_valid(&bad));
    changed[0].fixture = "-tests/alpha.c";
    CHECK(!tp_retirement_batch_input_list(&bad, NULL, 0) && !tp_retirement_batch_contract_valid(&bad) &&
          !tp_retirement_batch_input_list_leaf(&bad, leaf) && !leaf[0]);
    free(text);
}
#endif
