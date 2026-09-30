/* Real child executions with deterministic fixture output, never performance
 * acceptance. The fixture checks cwd, explicit environment, stdin and descriptor
 * isolation. The ordinary native and sanitized harnesses both run this path.
 * The layout child (test_retirement_layout_child) reports what a child in
 * lane B's canonical layout and sandbox sees.
 * The batch child reads its inputs from the `@file` response file, requires
 * them to be exactly the frozen list, compiles every input serially, and
 * writes one object per compiled input and a per-input metrics file with its
 * own monotonic offsets. */
#ifndef BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_TEST_H
#define BUSTER_THROUGHPUT_RETIREMENT_MEASUREMENT_TEST_H
#include "retirement_untimed.h"

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#define TEST_BATCH_INPUTS 8u
#define TEST_BATCH_METRICS_BYTES UINT64_C(1048576)
static char const test_batch_empty_digest[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
static char const test_batch_control_digest[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

typedef struct TestBatchFixture
{
    TpRetirementBatchInput inputs[TEST_BATCH_INPUTS];
    TpRetirementBatchContract contract;
    char fixtures[TEST_BATCH_INPUTS][64], artifacts[TEST_BATCH_INPUTS][64];
} TestBatchFixture;

/* `+name` is a compiled member (tests/name.c -> name.o) and `-name` a
 * rejected control; both sides build the same contract from these words. */
static int test_batch_contract(TestBatchFixture* fixture, char const* metrics, char* const* words,
                               unsigned count, char const* object_sha256)
{
    int ok = fixture && count && count <= TEST_BATCH_INPUTS;
    unsigned failures = 0;
    if (fixture) memset(fixture, 0, sizeof(*fixture));
    for (unsigned i = 0; ok && i < count; ++i)
    {
        int member = words[i][0] == '+';
        ok = (member || words[i][0] == '-') && words[i][1] &&
            snprintf(fixture->fixtures[i], sizeof(fixture->fixtures[i]), "tests/%s.c", words[i] + 1) > 0 &&
            snprintf(fixture->artifacts[i], sizeof(fixture->artifacts[i]), "%s.o", words[i] + 1) > 0;
        fixture->inputs[i] = (TpRetirementBatchInput){fixture->fixtures[i], member ? "ok" : "rejected",
            member ? "driver.none" : "driver.analysis", member ? test_batch_empty_digest : test_batch_control_digest,
            member ? object_sha256 : NULL, member ? fixture->artifacts[i] : NULL, (unsigned)member,
            member ? i : TP_RETIREMENT_BATCH_NO_ROW};
        failures += !member;
    }
    if (ok) fixture->contract = (TpRetirementBatchContract){"x86_64-linux", "none", metrics, fixture->inputs, count,
                                                            failures ? 1u : 0u, TEST_BATCH_METRICS_BYTES};
    return ok;
}

static uint64_t test_batch_clock(uint64_t origin, uint64_t after)
{
    uint64_t now = tp_process_monotonic_ns() - origin;
    while (now <= after) now = tp_process_monotonic_ns() - origin;
    return now;
}

/* The driver's response-file grammar (docs/agents/driver.md) for the
 * canonical subset the producer writes: whitespace-separated arguments with
 * double quotes and backslash escapes. Each argument must be the next frozen
 * fixture. */
static int test_batch_response_file(char const* leaf, TestBatchFixture const* fixture)
{
    FILE* file = leaf && leaf[0] == '@' ? fopen(leaf + 1, "rb") : NULL;
    char argument[1024];
    unsigned count = 0, index = 0;
    int c = 0, ok = file != NULL, quoted = 0, open = 0;
    while (ok && (c = fgetc(file)) != EOF)
    {
        if (c == '\\')
        {
            c = fgetc(file);
            ok = c != EOF && count < sizeof(argument) - 1;
            if (ok) argument[count++] = (char)c, open = 1;
        }
        else if (c == '"') quoted = !quoted, open = 1;
        else if (!quoted && (c == ' ' || c == '\n' || c == '\t'))
        {
            if (open)
            {
                argument[count] = 0;
                ok = index < fixture->contract.input_count && !strcmp(argument, fixture->inputs[index++].fixture);
            }
            count = 0;
            open = 0;
        }
        else
        {
            ok = count < sizeof(argument) - 1;
            if (ok) argument[count++] = (char)c, open = 1;
        }
    }
    ok = ok && !quoted && !open && index == fixture->contract.input_count;
    if (file) fclose(file);
    return ok;
}

static int test_retirement_batch_child(int argc, char** argv)
{
    int result = 2;
    char byte;
    char const* marker = getenv("TP_RETIREMENT_TEST");
    int leak = atoi(argv[5]);
    int ok = argc >= 8 && marker && !strcmp(marker, "explicit") && !getenv("TP_RETIREMENT_AMBIENT") &&
        read(STDIN_FILENO, &byte, 1) == 0 && fcntl(leak, F_GETFD) < 0 && errno == EBADF &&
        access("cwd-marker", F_OK) == 0;
    uint64_t origin = tp_process_monotonic_ns(), previous = 0;
    char const* behavior = argv[4];
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    TestBatchFixture fixture;
    TestMetricsInput timings[TEST_BATCH_INPUTS];
    ok = ok && test_batch_contract(&fixture, argv[3], argv + 7, (unsigned)(argc - 7), test_batch_empty_digest) &&
        test_batch_response_file(argv[6], &fixture);
    /* (L2) Touch the response file and leave identical bytes behind: renamed
     * away and back, or rewritten in place. Only its identity changes. */
    if (ok && !strcmp(behavior, "relist"))
        ok = rename(argv[6] + 1, "relisted.rsp") == 0 && rename("relisted.rsp", argv[6] + 1) == 0;
    if (ok && !strcmp(behavior, "rewrite"))
    {
        FILE* list = fopen(argv[6] + 1, "rb");
        char bytes[4096];
        size_t count = list ? fread(bytes, 1, sizeof(bytes), list) : 0;
        ok = list && count && count < sizeof(bytes) && fclose(list) == 0 && chmod(argv[6] + 1, 0600) == 0;
        list = ok ? fopen(argv[6] + 1, "wb") : NULL;
        ok = list && fwrite(bytes, 1, count, list) == count;
        if (list && fclose(list) != 0) ok = 0;
        ok = chmod(argv[6] + 1, 0400) == 0 && ok;
    }
    for (unsigned i = 0; ok && i < fixture.contract.input_count; ++i)
    {
        uint64_t start = test_batch_clock(origin, previous);
        if (fixture.inputs[i].artifact)
        {
            FILE* output = fopen(fixture.inputs[i].artifact, "wb");
            int changed = !i && !strcmp(behavior, "nondeterministic");
            if (changed) artifact[artifact_bytes - 1] ^= 1;
            ok = output && fwrite(artifact, 1, artifact_bytes, output) == artifact_bytes;
            if (changed) artifact[artifact_bytes - 1] ^= 1;
            if (output && fclose(output) != 0) ok = 0;
        }
        uint64_t end = test_batch_clock(origin, start);
        timings[i] = (TestMetricsInput){start, end, 65536 * (i + 1)};
        previous = end;
    }
    uint64_t wall = test_batch_clock(origin, previous);
    unsigned mutation = !strcmp(behavior, "status") ? TEST_METRICS_STATUS :
        !strcmp(behavior, "badmetrics") ? TEST_METRICS_UNKNOWN_KEY :
        !strcmp(behavior, "overlap") ? TEST_METRICS_OVERLAP : TEST_METRICS_VALID;
    if (ok && strcmp(behavior, "nometrics"))
    {
        char text[16384];
        size_t size = test_metrics_render(text, sizeof(text), &fixture.contract, wall, timings, mutation);
        FILE* metrics = fopen(argv[3], "wb");
        ok = size && metrics && fwrite(text, 1, size, metrics) == size;
        if (metrics && fclose(metrics) != 0) ok = 0;
    }
    if (ok) result = !strcmp(behavior, "exit0") ? 0 : (int)fixture.contract.exit_status;
    else result = 3;
    return result;
}

#if BUSTER_SANITIZE
/* A fixture child inside the canonical layout's sandbox cannot read /proc.
 * LeakSanitizer's exit-time check stops the world through /proc/<pid>/task
 * and aborts without it, and ASan cannot read ASAN_OPTIONS there
 * (/proc/self/environ), so the runtime asks here. Leak detection is off only
 * for a process that holds the canonical layout's A roots and work slot
 * (descriptors 5, 6 and 7) and cannot open /proc/self/status: a sandboxed
 * layout child. Every other process, the test runner included, keeps the
 * defaults; every other sanitizer check stays on in the sandboxed child, and
 * the same child code is leak-checked when it runs outside the sandbox. Raw
 * system calls: this runs during the sanitizer's own initialization. */
char const* __asan_default_options(void);
char const* __asan_default_options(void)
{
    int slots = 1;
    for (long slot = BQ_RETIREMENT_ROW_SLOT_SOURCE; slots && slot <= BQ_RETIREMENT_ROW_SLOT_WORK; slot += 1)
        slots = syscall(SYS_fcntl, slot, F_GETFD) >= 0;
    long proc = slots ? syscall(SYS_openat, AT_FDCWD, "/proc/self/status", O_RDONLY | O_CLOEXEC) : 0;
    if (slots && proc >= 0) syscall(SYS_close, proc);
    char const* options = slots && proc < 0 ? "detect_leaks=0" : "";
    return options;
}
#endif

/* A child in the canonical layout reports what it sees: its open
 * descriptors (0..1023), whether its cwd is slot 7, whether it may open
 * `outside` (a file beyond its sandbox), whether it may make a socket and
 * connect to the unix socket `socket_path`, and its address-space limit. */
static int test_retirement_layout_child(char const* outside, char const* socket_path)
{
    char fds[256];
    size_t used = 0;
    fds[0] = 0;
    for (int fd = 0; fd < 1024 && used + 8 < sizeof(fds); ++fd)
        if (fcntl(fd, F_GETFD) >= 0)
        {
            int written = snprintf(fds + used, sizeof(fds) - used, "%s%d", used ? "," : "", fd);
            used += written > 0 ? (size_t)written : 0;
        }
    struct stat here, slot;
    int cwd = stat(".", &here) == 0 && fstat(7, &slot) == 0 && here.st_dev == slot.st_dev &&
        here.st_ino == slot.st_ino;
    int file = open(outside, O_RDONLY | O_CLOEXEC);
    int opened = file >= 0, open_errno = errno;
    if (file >= 0) close(file);
    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int socket_errno = errno, connected = 0;
    if (sock >= 0)
    {
        struct sockaddr_un address;
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
        connected = connect(sock, (struct sockaddr*)&address, sizeof(address)) == 0;
        close(sock);
    }
    struct rlimit memory;
    char bound[32];
    int limited = getrlimit(RLIMIT_AS, &memory) == 0;
    if (limited && memory.rlim_cur == RLIM_INFINITY) snprintf(bound, sizeof(bound), "unlimited");
    else if (limited) snprintf(bound, sizeof(bound), "%llu", (unsigned long long)memory.rlim_cur);
    else snprintf(bound, sizeof(bound), "error");
    int ok = printf("fds=%s\ncwd=%s\nopen=%s\nsocket=%s\nas=%s\n", fds, cwd ? "slot" : "other",
        opened ? "allowed" : open_errno == EACCES ? "denied" : "error",
        connected ? "connected" : sock >= 0 ? "made" : socket_errno == EPERM ? "denied" : "error", bound) > 0 &&
        fflush(stdout) == 0;
    return ok;
}

static int test_retirement_measurement_child(int argc, char** argv)
{
    int result = 2;
    if (argc >= 8 && !strcmp(argv[2], "batch")) result = test_retirement_batch_child(argc, argv);
    else if (argc == 6)
    {
        char byte;
        char const* marker = getenv("TP_RETIREMENT_TEST");
        int leak = atoi(argv[5]);
        int ok = marker && !strcmp(marker, "explicit") && !getenv("TP_RETIREMENT_AMBIENT") &&
            read(STDIN_FILENO, &byte, 1) == 0 && fcntl(leak, F_GETFD) < 0 && errno == EBADF &&
            access("cwd-marker", F_OK) == 0;
        if (ok && !strcmp(argv[4], "timeout")) test_delay(5000);
        if (ok && !strcmp(argv[4], "fail")) result = 7;
        else if (ok && !strcmp(argv[4], "noisy"))
        {
            /* 1.5 MiB of output, then a failure: a log past the unit's cap. */
            static char block[65536];
            memset(block, 'n', sizeof(block));
            for (unsigned i = 0; ok && i < 24; ++i) ok = fwrite(block, 1, sizeof(block), stdout) == sizeof(block);
            ok = fflush(stdout) == 0 && ok;
            result = 7;
        }
        else if (ok && !strcmp(argv[4], "missing")) result = 0;
        else if (ok)
        {
            char const* bytes = !strcmp(argv[4], "wrong") ? "wrong\n" : "fixture-code\n";
            if (!strcmp(argv[2], "compiler"))
            {
                if (!strcmp(argv[4], "symlink")) ok = symlink("oracle-artifact", argv[3]) == 0;
                else if (!strcmp(argv[4], "hardlink")) ok = link("oracle-artifact", argv[3]) == 0;
                else
                {
                    FILE* output = fopen(argv[3], "wb");
                    unsigned char artifact[1024];
                    unsigned count = test_artifact_fixture(artifact, 1, 1);
                    if (!strcmp(argv[4], "zero-code")) test_artifact_put(artifact, 136, 2, 8);
                    ok = output && (!strcmp(argv[4], "wrong") ? fputs(bytes, output) >= 0 :
                        fwrite(artifact, 1, count, output) == count);
                    if (output && fclose(output) != 0) ok = 0;
                }
            }
            else if (!strcmp(argv[2], "runtime")) ok = fputs(bytes, stdout) >= 0 && fflush(stdout) == 0;
            else if (!strcmp(argv[2], "layout")) ok = test_retirement_layout_child(argv[3], argv[4]);
            else ok = 0;
            result = ok ? 0 : 3;
        }
    }
    return result;
}

static void test_retirement_measurement_command_file(char const* root, char const* leaf,
    TpRetirementMeasuredCommand const* command)
{
    char path[TP_PATH_CAP];
    CHECK(tp_path(path, root, leaf));
    FILE* file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file)
    {
        fputs("{\"argv\":[", file);
        for (unsigned i = 0; i < command->argument_count; ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, command->arguments[i]);
        }
        fputs("],\"cwd\":", file); tp_json_string(file, command->directory);
        fputs(",\"environment\":[", file);
        for (unsigned i = 0; i < command->environment_count; ++i)
        {
            if (i) fputc(',', file);
            tp_json_string(file, command->environment[i]);
        }
        fputs("]}", file);
        CHECK(fclose(file) == 0);
    }
}

static void test_retirement_measurement_copy(FILE* source, char const* root, char const* leaf)
{
    char path[TP_PATH_CAP];
    CHECK(tp_path(path, root, leaf));
    FILE* output = fopen(path, "wb");
    CHECK(output && fseek(source, 0, SEEK_SET) == 0);
    if (output)
    {
        unsigned char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0)
            CHECK(fwrite(buffer, 1, count, output) == count);
        CHECK(!ferror(source) && fclose(output) == 0);
    }
}

/* The metrics bytes already sit in the stage's metrics shard at the
 * transcript's (shard, offset, length); retire the scratch outputs. */
static void test_retirement_measurement_publish(int cwd, TestBatchFixture const* fixture)
{
    CHECK(unlinkat(cwd, fixture->contract.metrics, 0) == 0);
    for (unsigned i = 0; i < fixture->contract.input_count; ++i)
        if (fixture->inputs[i].artifact) CHECK(unlinkat(cwd, fixture->inputs[i].artifact, 0) == 0);
}

/* A reviewed test budget: every bound nonzero, object classes by group size,
 * link and self-host singletons by stage (a self-host process builds the
 * compiler and costs far more than a one-input batch), and separate untimed
 * bounds (the slowest untimed target). */
static TpRetirementCampaignBudget test_retirement_budget(void)
{
    TpRetirementCampaignBudget budget = {.reviewed_ns = UINT64_C(36000000000000),
        .reservation_ns = 1000000000, .materialization_ns = 2000000000, .baseline_build_ns = 3000000000,
        .candidate_build_ns = 3000000000, .correctness_ns = 4000000000, .settling_per_stage_ns = 500000000,
        .aa_qualification_ns = 600000000, .aa_receipt_sealing_ns = 700000000,
        .sample_export_per_stage_ns = 800000000, .final_statistics_ns = 900000000,
        .final_sealing_ns = 1000000000, .cleanup_ns = 20000000000, .runtime_process_ns = 50000000,
        .metrics_header_bytes = 4096, .metrics_input_bytes = 16384,
        .timed = {3, {{1, 40000000}, {4, 60000000}, {TP_RETIREMENT_BATCH_INPUTS, 2000000000}},
                  {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 45000000, [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 900000000}},
        .untimed = {3, {{1, 50000000}, {4, 80000000}, {TP_RETIREMENT_BATCH_INPUTS, 2500000000}},
                    {[TP_RETIREMENT_BUDGET_STAGE_LINK] = 60000000,
                     [TP_RETIREMENT_BUDGET_STAGE_SELF_HOST] = 1200000000}}};
    return budget;
}

/* Untimed code-artifact batches of one object group: a production and a
 * reproduction batch per variant, outside the timed window, with records and
 * the `untimed` metrics shard saved for the Python replay. */
static void test_retirement_untimed_fixture(char const* root, int cwd, int other,
    TpRetirementExecutable const* executable, int binary, char** environment,
    TpRetirementMeasuredCommand const* timed_command, TestBatchFixture const* timed_batch)
{
    TpRetirementCampaignBudget budget = test_retirement_budget();
    TestBatchFixture batch = *timed_batch;
    batch.contract.inputs = batch.inputs;
    CHECK(tp_retirement_budget_metrics_bytes(&budget, 3, &batch.contract.metrics_bytes_max) &&
          batch.contract.metrics_bytes_max == 4096 + 3 * 16384);
    char output_digest[65];
    CHECK(tp_retirement_batch_contract_output(&batch.contract, output_digest));
    char path[TP_PATH_CAP];
    /* The canonical budget record, which the Python replay binds into its
     * execution plan (the validator re-derives every metrics bound from it). */
    {
        char record[TP_RETIREMENT_BUDGET_BYTES];
        size_t record_size = tp_retirement_budget_encode(&budget, record, sizeof(record));
        CHECK(record_size && tp_path(path, root, "retirement-campaign-budget.txt"));
        FILE* record_file = fopen(path, "wb");
        CHECK(record_file && fwrite(record, 1, record_size, record_file) == record_size);
        if (record_file) CHECK(fclose(record_file) == 0);
    }
    CHECK(tp_path(path, root, "retirement-untimed-batches.jsonl"));
    FILE* records = fopen(path, "wb+");
    CHECK(tp_path(path, root, "retirement-metrics-untimed-0000.txt"));
    FILE* metrics_stream = fopen(path, "wb+");
    TpRetirementMetricsShards metrics;
    unsigned char reproduced[2];
    TpRetirementUntimed untimed;
    CHECK(records && metrics_stream && tp_retirement_metrics_shards_init(&metrics, "untimed", metrics_stream) &&
          tp_retirement_untimed_init(&untimed, records, &metrics, &budget, 1, reproduced, "job-1", 2, "boot-123",
              tp_first_allowed_cpu(), 1, UINT64_MAX - 1, 0));
    TpRetirementUntimedBatch run = {*timed_command, 0, 0, TP_RETIREMENT_UNTIMED_PRODUCTION, TP_RETIREMENT_GROUP_OBJECT};
    run.command.batch = &batch.contract;
    run.command.unit = 0;
    TpRetirementMeasurementResult result;
    for (unsigned step = 0; step < 4; ++step)
    {
        run.variant = step / 2;
        run.purpose = step % 2;
        run.command.variant = run.variant;
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        TpProcessInputs inputs = {binary, cwd, log, environment, 0, 0, {0, 0}, 0, 0};
        uint64_t records_before = untimed.records;
        CHECK(log >= 3 && tp_retirement_untimed_run(&untimed, &run, executable, &inputs, cwd, &result) &&
              result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE && !strcmp(result.output_sha256, output_digest) &&
              !strcmp(result.metrics.path, "retirement-metrics-untimed-0000.txt") &&
              untimed.records == records_before + 1);
        if (log >= 0) CHECK(close(log) == 0);
        CHECK(unlinkat(cwd, "child.log", 0) == 0 && unlinkat(cwd, "batch.metrics", 0) == 0 &&
              unlinkat(cwd, "alpha.o", 0) == 0 && unlinkat(cwd, "beta.o", 0) == 0);
    }
    TpRetirementShard record_descriptor;
    TpRetirementShardFile metrics_descriptor;
    CHECK(tp_retirement_untimed_finish(&untimed, &record_descriptor) && record_descriptor.records == 4 &&
          tp_retirement_metrics_shards_finish(&metrics, &metrics_descriptor) &&
          metrics_descriptor.contents.records == 4);
    char digest[65];
    uint64_t size = 0, lines = 0;
    CHECK(tp_path(path, root, "retirement-untimed-batches.jsonl") && records && fflush(records) == 0 &&
          tp_hash_file(path, digest, &size, &lines) && size == record_descriptor.bytes && lines == 4 &&
          !strcmp(digest, record_descriptor.sha256));
    if (records) CHECK(fclose(records) == 0);
    if (metrics_stream) CHECK(fclose(metrics_stream) == 0);

    /* Failure controls: records out of order, a missing reproduction, a
     * contract without the reviewed bound, a batch inside the timed window,
     * and a batch before the reservation. None is retried. */
    for (unsigned failure = 0; failure < 7; ++failure)
    {
        FILE* stream = tmpfile();
        FILE* shard = tmpfile();
        TpRetirementMetricsShards scratch;
        uint64_t now = tp_process_monotonic_ns();
        /* 3: inside a running window; 4: after the pre-sample binding of an
         * unfinished collection; 6 (accepted): after a completed window. */
        uint64_t bound_at = failure == 3 || failure == 6 ? now - 2 : failure == 4 ? 2 : UINT64_MAX - 1;
        uint64_t completed_at = failure == 3 ? UINT64_MAX - 1 : failure == 6 ? now - 1 : 0;
        uint64_t reserved_at = failure == 5 ? UINT64_MAX - 2 : 1;
        CHECK(stream && shard && tp_retirement_metrics_shards_init(&scratch, "untimed", shard) &&
              tp_retirement_untimed_init(&untimed, stream, &scratch, &budget, 1, reproduced, "job-1", 2,
                  "boot-123", tp_first_allowed_cpu(), reserved_at, bound_at, completed_at));
        run.variant = run.command.variant = 0;
        run.purpose = failure == 0 ? TP_RETIREMENT_UNTIMED_REPRODUCTION : TP_RETIREMENT_UNTIMED_PRODUCTION;
        TestBatchFixture unbounded = batch;
        unbounded.contract.inputs = unbounded.inputs;
        unbounded.contract.metrics_bytes_max = TEST_BATCH_METRICS_BYTES;
        run.command.batch = failure == 2 ? &unbounded.contract : &batch.contract;
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        TpProcessInputs inputs = {binary, cwd, log, environment, 0, 0, {0, 0}, 0, 0};
        int ok = log >= 3 && tp_retirement_untimed_run(&untimed, &run, executable, &inputs, cwd, &result);
        if (failure == 0)
        {
            /* A reproduction may follow nothing; its production then cannot. */
            CHECK(ok);
            CHECK(unlinkat(cwd, "batch.metrics", 0) == 0 && unlinkat(cwd, "alpha.o", 0) == 0 &&
                  unlinkat(cwd, "beta.o", 0) == 0 && close(log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
            log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            inputs.log = log;
            run.purpose = TP_RETIREMENT_UNTIMED_PRODUCTION;
            CHECK(!tp_retirement_untimed_run(&untimed, &run, executable, &inputs, cwd, &result) &&
                  result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID);
        }
        else if (failure == 1)
        {
            /* One production batch alone leaves both reproductions missing. */
            TpRetirementShard descriptor;
            CHECK(ok && !tp_retirement_untimed_finish(&untimed, &descriptor) && !descriptor.records);
        }
        else if (failure == 2) CHECK(!ok && result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID);
        else if (failure == 6) CHECK(ok && !untimed.failed && untimed.records == 1);
        else CHECK(!ok && result.status == TP_RETIREMENT_MEASUREMENT_COLLECTION_FAILED && untimed.failed &&
                   scratch.failed);
        if (failure != 1 && failure != 6) CHECK(untimed.failed);
        struct stat retained;
        for (unsigned leaf = 0; leaf < 3; ++leaf)
        {
            char const* name = leaf == 0 ? "alpha.o" : leaf == 1 ? "beta.o" : "batch.metrics";
            if (fstatat(cwd, name, &retained, AT_SYMLINK_NOFOLLOW) == 0) CHECK(unlinkat(cwd, name, 0) == 0);
        }
        if (log >= 0) CHECK(close(log) == 0);
        CHECK(unlinkat(cwd, "child.log", 0) == 0);
        if (stream) CHECK(fclose(stream) == 0);
        if (shard) CHECK(fclose(shard) == 0);
    }
    BUSTER_UNUSED(other);

    /* The widest record: 20-digit timestamps, PID and start token, exit
     * status 255, group 99999, `reproduction`/`candidate` and a metrics
     * artifact with 8-digit offset and length at the longest shard leaf. */
    char line[TP_RETIREMENT_UNTIMED_LINE_CAP], hash[65];
    memset(hash, 'e', 64); hash[64] = 0;
    TpRetirementMetricsArtifact widest = {"retirement-metrics-abcdefgh-2047.txt", UINT64_C(33554432),
        UINT64_C(33554432), {0}};
    memcpy(widest.sha256, hash, sizeof(hash));
    TpRetirementUntimedBatch max_batch = {{.exit_status = 255}, TP_RETIREMENT_MAX_CELLS - 1, 1,
        TP_RETIREMENT_UNTIMED_REPRODUCTION, TP_RETIREMENT_GROUP_OBJECT};
    TpProcessObservation max_observed = {.pid = UINT64_MAX, .start_token = UINT64_MAX,
        .started_ns = UINT64_MAX - 1, .finished_ns = UINT64_MAX, .valid = 1};
    char max_job[129], max_boot[129];
    memset(max_job, 'a', 128); max_job[128] = 0;
    memset(max_boot, 'b', 128); max_boot[128] = 0;
    size_t count = tp_retirement_untimed_record(line, sizeof(line), &max_batch, &max_observed, hash, hash, hash,
        &widest, max_job, UINT64_MAX, max_boot);
    CHECK(count == TP_RETIREMENT_UNTIMED_RECORD_BYTES_MAX && line[count - 1] == '\n');
    CHECK(!tp_retirement_untimed_record(line, count, &max_batch, &max_observed, hash, hash, hash, &widest,
        max_job, UINT64_MAX, max_boot) && !line[0]);
    max_batch.group_kind = TP_RETIREMENT_GROUP_SINGLETON;
    CHECK(!tp_retirement_untimed_record(line, sizeof(line), &max_batch, &max_observed, hash, hash, hash, &widest,
        max_job, UINT64_MAX, max_boot)); /* A singleton has no metrics and exits zero. */
}

static void test_retirement_measurement(char const* executable_path, char const* root)
{
    char directory[TP_PATH_CAP], executable_copy[TP_PATH_CAP], path[TP_PATH_CAP];
    CHECK(tp_path(directory, root, "retirement-measured"));
    CHECK(tp_mkdirs(directory));
    CHECK(test_text(directory, "cwd-marker", "fixed cwd\n"));
    CHECK(test_text(directory, "oracle-artifact", "fixture-code\n"));
    CHECK(tp_path(executable_copy, directory, "frozen-child"));
    CHECK(tp_copy_file(executable_path, executable_copy));
    CHECK(chmod(executable_copy, 0500) == 0);
    int binary = open(executable_copy, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    int cwd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int leak = fcntl(binary, F_DUPFD, 512); /* Intentionally lacks CLOEXEC. */
    char leak_text[32];
    snprintf(leak_text, sizeof(leak_text), "%d", leak);
    CHECK(binary >= 3 && cwd >= 3 && leak >= 3);
    char digest[65], expected_output[65], expected_artifact[65], malformed_digest[65], batch_output[65];
    uint64_t bytes = 0;
    CHECK(tp_retirement_file_hash(binary, digest, &bytes));
    CHECK(bytes > 0);
    TpRetirementExecutable executable;
    CHECK(!tp_retirement_executable_init(&executable, binary, "not-a-digest"));
    CHECK(!executable.valid && executable.descriptor == -1);
    CHECK(!tp_retirement_executable_init(&executable, leak, digest));
    char wrong_digest[65];
    memcpy(wrong_digest, digest, sizeof(digest));
    wrong_digest[0] = wrong_digest[0] == 'a' ? 'b' : 'a';
    CHECK(!tp_retirement_executable_init(&executable, binary, wrong_digest));
    CHECK(chmod(executable_copy, 0700) == 0);
    CHECK(!tp_retirement_executable_init(&executable, binary, digest));
    CHECK(chmod(executable_copy, 0500) == 0);
    CHECK(tp_retirement_executable_init(&executable, binary, digest));
    Sha256 hash;
    sha256_init(&hash); sha256_add(&hash, "fixture-code\n", 13); sha256_finish_hex(&hash, expected_output);
    unsigned char artifact[1024];
    unsigned artifact_bytes = test_artifact_fixture(artifact, 1, 1);
    sha256_init(&hash); sha256_add(&hash, artifact, artifact_bytes); sha256_finish_hex(&hash, expected_artifact);
    sha256_init(&hash); sha256_add(&hash, "wrong\n", 6); sha256_finish_hex(&hash, malformed_digest);
    char const* singleton_objects[] = {expected_artifact};
    CHECK(tp_retirement_batch_output_digest(singleton_objects, 1, batch_output));
    char* environment[] = {"LC_ALL=C", "TP_RETIREMENT_TEST=explicit", NULL};
    char* arguments[] = {executable_copy, "retirement-child", "compiler", "artifact.bin", "ok", leak_text, NULL};
    char command_digest[65];
    TpRetirementMeasuredCommand command = {.arguments = arguments, .argument_count = 6,
        .environment = environment, .environment_count = 2, .directory = directory,
        .artifact = "artifact.bin", .timeout_seconds = 2, .command_sha256 = command_digest,
        .output_sha256 = batch_output};
    CHECK(tp_retirement_command_hash(&command, command_digest));
    test_retirement_measurement_command_file(root, "retirement-measured-command-compiler.json", &command);
    arguments[4] = "literal \\ and \"quotes\"";
    CHECK(tp_retirement_command_hash(&command, command_digest));
    test_retirement_measurement_command_file(root, "retirement-measured-command-escaped.json", &command);
    CHECK(test_text(root, "retirement-measured-command-escaped.sha256", command_digest));
    arguments[4] = "bad\nargument";
    CHECK(!tp_retirement_command_hash(&command, command_digest) && !command_digest[0]);
    char* huge = (char*)malloc(TP_RETIREMENT_COMMAND_BYTES + 1);
    CHECK(huge != NULL);
    if (huge)
    {
        memset(huge, 'x', TP_RETIREMENT_COMMAND_BYTES);
        huge[TP_RETIREMENT_COMMAND_BYTES] = 0;
        arguments[4] = huge;
        CHECK(!tp_retirement_command_hash(&command, command_digest));
        free(huge);
    }
    arguments[4] = "ok";
    environment[1] = "LC_ALL=D";
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    environment[1] = "1INVALID=value";
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    environment[1] = "TP_RETIREMENT_TEST=explicit";
    command.argument_count = TP_RETIREMENT_COMMAND_ARGUMENTS + 1;
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    command.argument_count = 6;
    command.environment_count = TP_RETIREMENT_COMMAND_ENVIRONMENT + 1;
    CHECK(!tp_retirement_command_hash(&command, command_digest));
    command.environment_count = 2;
    CHECK(setenv("TP_RETIREMENT_AMBIENT", "must-not-leak", 1) == 0);

    /* Code sections are parsed once, outside timing, from the frozen artifact;
     * a byte-identical reproduction is required. */
    CHECK(test_text(directory, "frozen-artifact.bin", "") && test_text(directory, "reproduction.bin", ""));
    int frozen = openat(cwd, "frozen-artifact.bin", O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
    CHECK(frozen >= 3 && write(frozen, artifact, artifact_bytes) == (ssize_t)artifact_bytes && close(frozen) == 0);
    int reproduction = openat(cwd, "reproduction.bin", O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
    CHECK(reproduction >= 3 && write(reproduction, artifact, artifact_bytes) == (ssize_t)artifact_bytes &&
          close(reproduction) == 0);
    frozen = openat(cwd, "frozen-artifact.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    reproduction = openat(cwd, "reproduction.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    TpRetirementCodeSide sides[2];
    CHECK(tp_retirement_code_observe(frozen, reproduction, &sides[0]) && sides[0].code_bytes == 13 &&
          !strcmp(sides[0].code_sha256, expected_output) && !strcmp(sides[0].artifact_sha256, expected_artifact) &&
          !strcmp(sides[0].reproduction_sha256, expected_artifact));
    sides[1] = sides[0];
    CHECK(tp_path(path, root, "retirement-measured-code.jsonl"));
    FILE* code_file = fopen(path, "wb+");
    TpRetirementCodeRecords code_records;
    TpRetirementShard code_descriptor;
    CHECK(code_file && tp_retirement_code_records_init(&code_records, code_file) &&
          tp_retirement_code_records_append(&code_records, 0, sides) &&
          tp_retirement_code_records_finish(&code_records, &code_descriptor) && code_descriptor.records == 1);
    if (code_file) CHECK(fclose(code_file) == 0);
    if (reproduction >= 0) CHECK(close(reproduction) == 0);
    reproduction = openat(cwd, "reproduction.bin", O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(reproduction >= 3 && pwrite(reproduction, "x", 1, 0) == 1 && close(reproduction) == 0);
    reproduction = openat(cwd, "reproduction.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(!tp_retirement_code_observe(frozen, reproduction, &sides[0]) && !sides[0].artifact_sha256[0]);
    if (reproduction >= 0) CHECK(close(reproduction) == 0);
    if (frozen >= 0) CHECK(close(frozen) == 0);
    unsigned char zero_artifact[1024];
    unsigned zero_bytes = test_artifact_fixture(zero_artifact, 1, 1);
    test_artifact_put(zero_artifact, 136, 2, 8);
    for (unsigned file = 0; file < 2; ++file)
    {
        int output = openat(cwd, file ? "reproduction.bin" : "frozen-artifact.bin",
                            O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
        CHECK(output >= 3 && write(output, zero_artifact, zero_bytes) == (ssize_t)zero_bytes && close(output) == 0);
    }
    frozen = openat(cwd, "frozen-artifact.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    reproduction = openat(cwd, "reproduction.bin", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(tp_retirement_code_observe(frozen, reproduction, &sides[1]) && !sides[1].code_bytes &&
          !strcmp(sides[1].code_sha256, test_batch_empty_digest));
    if (reproduction >= 0) CHECK(close(reproduction) == 0);
    if (frozen >= 0) CHECK(close(frozen) == 0);
    CHECK(unlinkat(cwd, "frozen-artifact.bin", 0) == 0 && unlinkat(cwd, "reproduction.bin", 0) == 0);

    /* A complete 60-pair/two-round collection passes through real fresh
     * processes for one singleton link group and its runtime, including every
     * warmup. */
    TpSampleTest test;
    CHECK(test_sample_open(&test, 1));
    test.transcript.cpu = tp_first_allowed_cpu();
    TpRetirementInvocation invocation;
    int ok = test.transcript.cpu >= 0;
    while (ok && tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY)
    {
        command.unit = invocation.kind ? invocation.row : invocation.group;
        command.kind = invocation.kind;
        command.variant = invocation.variant;
        arguments[2] = invocation.kind ? "runtime" : "compiler";
        command.artifact = invocation.kind ? NULL : "artifact.bin";
        command.output_sha256 = invocation.kind ? expected_output : batch_output;
        CHECK(tp_retirement_command_hash(&command, command_digest));
        if (invocation.kind && !invocation.phase && !invocation.warmup && !invocation.variant)
            test_retirement_measurement_command_file(root, "retirement-measured-command-runtime.json", &command);
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        CHECK(log >= 3);
        TpProcessInputs inputs = {binary, cwd, log, environment, 0, 0, {0, 0}, 0, 0};
        TpRetirementMeasurementResult result;
        ok = tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result);
        CHECK(ok);
        CHECK(result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
              result.output_bytes == (invocation.kind ? 13 : artifact_bytes) && !result.metrics.path[0] &&
              !strcmp(result.output_sha256, invocation.kind ? expected_output : batch_output));
        if (log >= 0) CHECK(close(log) == 0);
        if (ok)
        {
            CHECK(unlinkat(cwd, "child.log", 0) == 0);
            if (!invocation.kind) CHECK(unlinkat(cwd, "artifact.bin", 0) == 0);
        }
    }
    CHECK(ok && tp_retirement_execution_complete(&test.execution));
    TpRetirementShard transcript_shard, sample_shard;
    CHECK(tp_retirement_transcript_end_shard(&test.transcript, &transcript_shard));
    CHECK(tp_retirement_transcript_finish(&test.transcript, tp_process_monotonic_ns()));
    CHECK(tp_retirement_samples_begin_export(&test.samples));
    CHECK(tp_path(path, root, "retirement-measured-samples.jsonl"));
    FILE* output = fopen(path, "wb+");
    CHECK(output && tp_retirement_samples_write_shard(&test.samples, output, &sample_shard));
    if (output) CHECK(fclose(output) == 0);
    CHECK(tp_retirement_samples_finish(&test.samples));
    CHECK(transcript_shard.records == 488 && sample_shard.records == 120);
    test_retirement_measurement_copy(test.stream, root, "retirement-measured-execution.jsonl");
    test_sample_close(&test);

    /* The authenticated census ID may be sparse. Compiler commands bind the
     * dense group; the runtime command binds the original canonical row. */
    CHECK(test_sample_open_layout(&test, 1, 6));
    test.transcript.cpu = tp_first_allowed_cpu();
    while (tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY &&
           !invocation.kind)
        CHECK(test_sample_observe(&test, 0, TEST_SAMPLE_VALID));
    CHECK(tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY &&
          invocation.row == 6 && invocation.dense == 0 && invocation.kind == 1);
    command.unit = 0;
    command.kind = 1;
    command.variant = invocation.variant;
    command.artifact = NULL;
    command.output_sha256 = expected_output;
    arguments[2] = "runtime";
    CHECK(tp_retirement_command_hash(&command, command_digest));
    int sparse_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    CHECK(sparse_log >= 3);
    TpProcessInputs sparse_inputs = {binary, cwd, sparse_log, environment, 0, 0, {0, 0}, 0, 0};
    TpRetirementMeasurementResult sparse_result;
    CHECK(!tp_retirement_measurement_run(&test.samples, &command, &executable, &sparse_inputs, cwd,
                                         &sparse_result) && sparse_result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID);
    test_sample_close(&test);
    CHECK(close(sparse_log) == 0 && unlinkat(cwd, "child.log", 0) == 0);
    CHECK(test_sample_open_layout(&test, 1, 6));
    test.transcript.cpu = tp_first_allowed_cpu();
    while (tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY &&
           !invocation.kind)
        CHECK(test_sample_observe(&test, 0, TEST_SAMPLE_VALID));
    command.unit = 6;
    command.variant = invocation.variant;
    sparse_log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    sparse_inputs.log = sparse_log;
    uint64_t runtime_start = test.execution.sequence;
    CHECK(sparse_log >= 3 && tp_retirement_measurement_run(&test.samples, &command, &executable,
                                                           &sparse_inputs, cwd, &sparse_result));
    CHECK(sparse_result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
          sparse_result.output_bytes == 13 && test.execution.sequence == runtime_start + 1);
    if (sparse_log >= 0) CHECK(close(sparse_log) == 0);
    CHECK(unlinkat(cwd, "child.log", 0) == 0);
    test_sample_close(&test);
    arguments[2] = "compiler";

    /* A failed command cannot advance or restart the same attempt. All output
     * remains present for the service's failure retention/sealing path. */
    for (unsigned failure = 0; failure < 23; ++failure)
    {
        CHECK(test_sample_open(&test, 1));
        test.transcript.cpu = tp_first_allowed_cpu();
        unsigned behavior = failure >= 16 ? failure - 16 : failure;
        command.kind = failure >= 16 && failure < 20;
        command.variant = 0;
        command.artifact = command.kind ? NULL : "artifact.bin";
        command.batch = NULL;
        command.exit_status = 0;
        command.output_sha256 = command.kind ? expected_output : batch_output;
        arguments[2] = command.kind ? "runtime" : "compiler";
        arguments[4] = behavior == 0 ? "fail" : behavior == 1 ? "wrong" : behavior == 2 ? "missing" :
                       behavior == 3 ? "timeout" : behavior == 14 ? "symlink" : behavior == 15 ? "hardlink" : "ok";
        TestBatchFixture misplaced;
        char* misplaced_words[] = {"+alpha"};
        CHECK(test_batch_contract(&misplaced, "batch.metrics", misplaced_words, 1, expected_artifact));
        if (failure == 20) command.batch = &misplaced.contract;
        if (failure == 21) command.output_sha256 = expected_artifact; /* Not the batch output digest. */
        if (failure == 22) command.artifact = "other.bin";
        command.timeout_seconds = behavior == 3 ? 1 : 2;
        if (command.kind)
        {
            while (tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY &&
                   !invocation.kind)
                CHECK(test_sample_observe(&test, 0, TEST_SAMPLE_VALID));
        }
        uint64_t before = test.execution.sequence;
        command.unit = failure == 4 ? 1 : 0;
        CHECK(tp_retirement_command_hash(&command, command_digest));
        if (failure == 5) command_digest[0] = command_digest[0] == 'a' ? 'b' : 'a';
        if (failure == 6) command.exit_status = 1; /* A singleton group always exits zero. */
        if (failure == 7) CHECK(test_text(directory, "artifact.bin", "fixture-code\n"));
        if (failure == 8) CHECK(symlinkat("cwd-marker", cwd, "artifact.bin") == 0);
        if (failure == 9) CHECK(chmod(executable_copy, 0700) == 0);
        int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        CHECK(log >= 3);
        TpProcessInputs inputs = {binary, cwd, log, environment, 0, 0, {0, 0}, 0, 0};
        if (failure == 10) CHECK(write(log, "stale", 5) == 5);
        if (failure == 11) inputs.environment = NULL;
        if (failure == 12) CHECK(fcntl(log, F_SETFD, 0) == 0);
        if (failure == 13) command.directory = "/";
        TpRetirementMeasurementResult result;
        CHECK(!tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result));
        if (behavior == 0 || behavior == 3)
        {
            CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED && result.observed.valid);
            if (!behavior) CHECK(result.process.exit_code == 7 && !result.process.timed_out);
            else CHECK(result.process.timed_out && result.process.signal_number != 0);
        }
        else if (behavior == 1 || behavior == 2 || failure == 14 || failure == 15 || failure == 21 || failure == 22)
        {
            CHECK(result.status == TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID && result.observed.valid);
            if (behavior == 1) CHECK(result.output_bytes == 6 && strcmp(result.output_sha256, expected_output));
        }
        else if (failure != 12)
            CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && !result.observed.valid);
        CHECK(test.samples.failed && test.execution.sequence == before && test.samples.collected == before);
        CHECK(!tp_retirement_measurement_run(&test.samples, &command, &executable, &inputs, cwd, &result));
        CHECK(result.status == TP_RETIREMENT_MEASUREMENT_PLAN_INVALID && !result.observed.valid);
        struct stat retained;
        CHECK(fstatat(cwd, "child.log", &retained, AT_SYMLINK_NOFOLLOW) == 0);
        if (failure == 1 || failure == 7 || failure == 8 || failure == 14 || failure == 15 || failure == 21 ||
            failure == 22)
            CHECK(fstatat(cwd, "artifact.bin", &retained, AT_SYMLINK_NOFOLLOW) == 0);
        if (log >= 0) CHECK(close(log) == 0);
        CHECK(unlinkat(cwd, "child.log", 0) == 0);
        if (fstatat(cwd, "artifact.bin", &retained, AT_SYMLINK_NOFOLLOW) == 0)
            CHECK(unlinkat(cwd, "artifact.bin", 0) == 0);
        CHECK(chmod(executable_copy, 0500) == 0);
        CHECK(tp_retirement_executable_init(&executable, binary, digest));
        command.directory = directory;
        test_sample_close(&test);
    }
    arguments[4] = "ok";

    /* A complete collection of one object group: two members and a frozen
     * rejection control in one serial continue-on-failure batch per
     * invocation. The inputs reach the child only through the digest-bound
     * `@file` response file. Every batch reproduces both objects byte for
     * byte and a metrics file the strict reader accepts; its members'
     * intervals and arena bytes become their samples, and its metrics bytes
     * are appended to the stage's `mb` metrics shard for the Python replay. */
    char list_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP], list_argument[TP_RETIREMENT_INPUT_LIST_LEAF_CAP + 1];
    char* batch_arguments[] = {executable_copy, "retirement-child", "batch", "batch.metrics", "ok", leak_text,
        list_argument, "+alpha", "+beta", "-control", NULL, NULL};
    TestBatchFixture batch;
    CHECK(test_batch_contract(&batch, "batch.metrics", batch_arguments + 7, 3, expected_artifact) &&
          tp_retirement_batch_contract_valid(&batch.contract) && batch.contract.exit_status == 1);
    CHECK(tp_retirement_batch_input_list_write(cwd, &batch.contract, list_leaf));
    snprintf(list_argument, sizeof(list_argument), "@%s", list_leaf);
    char rejected_leaf[TP_RETIREMENT_INPUT_LIST_LEAF_CAP];
    CHECK(!tp_retirement_batch_input_list_write(cwd, &batch.contract, rejected_leaf) && !rejected_leaf[0]);
    char batch_digest[65], batch_command_digest[65];
    CHECK(tp_retirement_batch_contract_output(&batch.contract, batch_digest));
    TpRetirementMeasuredCommand batch_command = {.arguments = batch_arguments, .argument_count = 10,
        .environment = environment, .environment_count = 2, .directory = directory, .batch = &batch.contract,
        .timeout_seconds = 2, .command_sha256 = batch_command_digest, .output_sha256 = batch_digest,
        .exit_status = 1};
    CHECK(tp_retirement_command_hash(&batch_command, batch_command_digest));
    test_retirement_measurement_command_file(root, "retirement-measured-command-batch.json", &batch_command);
    unsigned const batch_ids[] = {0, 1}, batch_metrics[] = {0, 0}, batch_kinds[] = {TP_RETIREMENT_GROUP_OBJECT};
    unsigned const batch_offsets[] = {0, 2}, batch_members[] = {0, 1};
    {
        /* Rows 0 and 1 form the group; row 4 is timed elsewhere. */
        TpRetirementSampleRow rows[3] = {{.id = 0}, {.id = 1}, {.id = 4}};
        unsigned sample_members[3] = {0, 1, 2};
        TpRetirementSampleGroup group = {.first = 0, .count = 2, .kind = TP_RETIREMENT_GROUP_OBJECT};
        TpRetirementSamples layout = {.rows = rows, .members = sample_members, .row_count = 3};
        TestBatchFixture rows_fixture = batch;
        rows_fixture.contract.inputs = rows_fixture.inputs;
        CHECK(tp_retirement_batch_rows_match(&layout, &group, &rows_fixture.contract));
        rows_fixture.inputs[2].row = 3;
        CHECK(tp_retirement_batch_rows_match(&layout, &group, &rows_fixture.contract));
        rows_fixture.inputs[2].row = 4;
        CHECK(tp_retirement_batch_contract_valid(&rows_fixture.contract) &&
              !tp_retirement_batch_rows_match(&layout, &group, &rows_fixture.contract));
        rows_fixture.inputs[2].row = TP_RETIREMENT_BATCH_NO_ROW;
        rows_fixture.inputs[1].row = 4;
        CHECK(!tp_retirement_batch_rows_match(&layout, &group, &rows_fixture.contract));
        rows_fixture.inputs[1] = batch.inputs[1];
        rows_fixture.inputs[1].member = 0;
        rows_fixture.inputs[1].row = TP_RETIREMENT_BATCH_NO_ROW;
        CHECK(!tp_retirement_batch_rows_match(&layout, &group, &rows_fixture.contract));
    }
    TpRetirementLayout batch_layout = {2, 1, batch_ids, batch_metrics, batch_kinds, batch_offsets, batch_members};
    char other_directory[TP_PATH_CAP];
    CHECK(tp_path(other_directory, root, "retirement-measured-other") && tp_mkdirs(other_directory) &&
          chmod(other_directory, 0700) == 0);
    int other = open(other_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(other >= 3);
    for (unsigned scenario = 0; scenario < 20; ++scenario)
    {
        memset(&test, 0, sizeof(test));
        test.stream = tmpfile();
        test.spool = tmpfile();
        FILE* metrics_stream = NULL;
        if (!scenario)
        {
            CHECK(tp_path(path, root, "retirement-metrics-mb-0000.txt"));
            metrics_stream = fopen(path, "wb+");
        }
        else metrics_stream = tmpfile();
        TpRetirementMetricsShards metrics_shards;
        CHECK(test.stream && test.spool && metrics_stream &&
              tp_retirement_execution_init(&test.execution, 1, 1, NULL, 0, 2, 60, test.workspace, 3) &&
              tp_retirement_transcript_init(&test.transcript, &test.execution, "job-1", 2, "boot-123",
                  tp_first_allowed_cpu(), 1000) &&
              tp_retirement_transcript_begin_shard(&test.transcript, test.stream) &&
              tp_retirement_samples_init(&test.samples, &test.transcript, test.spool, &batch_layout, test.rows,
                  test.groups, test.members) &&
              tp_retirement_metrics_shards_init(&metrics_shards, "mb", metrics_stream));
        if (scenario != 13) CHECK(tp_retirement_samples_attach_metrics(&test.samples, &metrics_shards));
        /* No spare stream and too little room for the reviewed bound. */
        if (scenario == 16) metrics_shards.bytes = TP_RETIREMENT_METRICS_SHARD_BYTES - TEST_BATCH_METRICS_BYTES + 1;
        batch_arguments[4] = scenario == 1 ? "nondeterministic" : scenario == 2 ? "status" :
            scenario == 3 ? "badmetrics" : scenario == 4 ? "overlap" : scenario == 5 ? "exit0" :
            scenario == 6 ? "nometrics" : scenario == 18 ? "relist" : scenario == 19 ? "rewrite" : "ok";
        batch_arguments[10] = scenario == 10 ? "tests/alpha.c" : scenario == 12 ? "@other.rsp" : NULL;
        batch_command.argument_count = scenario == 10 || scenario == 12 ? 11 : 10;
        TestBatchFixture changed = batch;
        changed.contract.inputs = changed.inputs;
        if (scenario == 7) changed.inputs[1].member = 0, changed.inputs[1].status = "ok";
        if (scenario == 9) changed.inputs[1].row = 5; /* A valid contract naming another row. */
        if (scenario == 14) changed.contract.metrics_bytes_max = 128; /* Below the batch's actual metrics. */
        batch_command.batch = scenario == 7 || scenario == 9 || scenario == 14 ? &changed.contract : &batch.contract;
        CHECK(tp_retirement_command_hash(&batch_command, batch_command_digest));
        if (scenario == 8) CHECK(test_text(directory, "beta.o", "stale\n"));
        if (scenario == 9) CHECK(tp_retirement_batch_contract_valid(&changed.contract));
        char saved_leaf[TP_PATH_CAP + 16];
        snprintf(saved_leaf, sizeof(saved_leaf), "%s.saved", list_leaf);
        if (scenario == 11)
        {
            /* The same leaf with other bytes: the name no longer is the digest. */
            CHECK(renameat(cwd, list_leaf, cwd, saved_leaf) == 0);
            static char const reordered[] = "\"tests/beta.c\"\n\"tests/alpha.c\"\n\"tests/control.c\"\n";
            int forged = openat(cwd, list_leaf, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0400);
            CHECK(forged >= 3 && write(forged, reordered, sizeof(reordered) - 1) == (ssize_t)(sizeof(reordered) - 1) &&
                  close(forged) == 0);
        }
        if (scenario == 17) CHECK(fchmodat(cwd, list_leaf, 0600, 0) == 0);
        unsigned runs = scenario ? 1 : 244;
        int run_ok = 1;
        for (unsigned i = 0; run_ok && i < runs; ++i)
        {
            CHECK(tp_retirement_execution_peek(&test.execution, &invocation) == TP_RETIREMENT_NEXT_READY);
            batch_command.unit = invocation.group;
            batch_command.variant = invocation.variant;
            int log = openat(cwd, "child.log", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            TpProcessInputs inputs = {binary, cwd, log, environment, 0, 0, {0, 0}, 0, 0};
            TpRetirementMeasurementResult result;
            uint64_t offset = metrics_shards.bytes;
            run_ok = log >= 3 && tp_retirement_measurement_run(&test.samples, &batch_command, &executable, &inputs,
                                                               scenario == 15 ? other : cwd, &result);
            if (!scenario)
            {
                CHECK(run_ok && result.status == TP_RETIREMENT_MEASUREMENT_COMPLETE &&
                      result.process.exit_code == 1 && !strcmp(result.output_sha256, batch_digest) &&
                      result.output_bytes == 2 * artifact_bytes && result.metrics.bytes &&
                      result.metrics.offset == offset && tp_retirement_digest(result.metrics.sha256) &&
                      !strcmp(result.metrics.path, "retirement-metrics-mb-0000.txt") &&
                      metrics_shards.bytes == offset + result.metrics.bytes);
                if (run_ok) test_retirement_measurement_publish(cwd, &batch);
            }
            else
            {
                unsigned output_failure = scenario < 5 || scenario == 6 || scenario == 14 || scenario >= 18;
                unsigned expected_status = scenario == 5 ? TP_RETIREMENT_MEASUREMENT_PROCESS_FAILED :
                    output_failure ? TP_RETIREMENT_MEASUREMENT_OUTPUT_INVALID : TP_RETIREMENT_MEASUREMENT_PLAN_INVALID;
                CHECK(!run_ok && result.status == expected_status && test.samples.failed &&
                      !test.execution.sequence && (scenario == 13 || metrics_shards.failed));
                struct stat retained;
                if (output_failure || scenario == 5)
                    CHECK(fstatat(cwd, "alpha.o", &retained, AT_SYMLINK_NOFOLLOW) == 0 &&
                          fstatat(cwd, "beta.o", &retained, AT_SYMLINK_NOFOLLOW) == 0);
                else CHECK(scenario == 8 || fstatat(cwd, "alpha.o", &retained, AT_SYMLINK_NOFOLLOW) != 0);
                for (unsigned leaf = 0; leaf < 3; ++leaf)
                {
                    char const* name = leaf == 0 ? "alpha.o" : leaf == 1 ? "beta.o" : "batch.metrics";
                    if (fstatat(cwd, name, &retained, AT_SYMLINK_NOFOLLOW) == 0) CHECK(unlinkat(cwd, name, 0) == 0);
                }
            }
            if (log >= 0) CHECK(close(log) == 0);
            CHECK(unlinkat(cwd, "child.log", 0) == 0);
        }
        if (scenario == 11) CHECK(unlinkat(cwd, list_leaf, 0) == 0 && renameat(cwd, saved_leaf, cwd, list_leaf) == 0);
        if (scenario == 17) CHECK(fchmodat(cwd, list_leaf, 0400, 0) == 0);
        if (!scenario)
        {
            TpRetirementShardFile metrics_shard;
            CHECK(run_ok && tp_retirement_execution_complete(&test.execution));
            CHECK(tp_retirement_metrics_shards_finish(&metrics_shards, &metrics_shard) &&
                  metrics_shard.contents.records == 244 && metrics_shard.contents.bytes == metrics_shards.total_bytes);
            CHECK(tp_retirement_transcript_end_shard(&test.transcript, &transcript_shard) &&
                  tp_retirement_transcript_finish(&test.transcript, tp_process_monotonic_ns()) &&
                  tp_retirement_samples_begin_export(&test.samples));
            TpRetirementShard batch_shards[2];
            char const* const names[] = {"retirement-measured-batch-rows.jsonl",
                                         "retirement-measured-batch-batches.jsonl"};
            for (unsigned population = 0; population < 2; ++population)
            {
                CHECK(tp_path(path, root, names[population]));
                output = fopen(path, "wb+");
                CHECK(output && tp_retirement_samples_write_shard(&test.samples, output, &batch_shards[population]));
                if (output) CHECK(fclose(output) == 0);
            }
            CHECK(tp_retirement_samples_finish(&test.samples) && transcript_shard.records == 244 &&
                  batch_shards[0].records == 240 && batch_shards[1].records == 120);
            test_retirement_measurement_copy(test.stream, root, "retirement-measured-batch-execution.jsonl");
        }
        if (metrics_stream) CHECK(fclose(metrics_stream) == 0);
        test_sample_close(&test);
    }
    batch_arguments[4] = "ok";
    batch_arguments[10] = NULL;
    batch_command.argument_count = 10;
    batch_command.batch = &batch.contract;
    CHECK(tp_retirement_command_hash(&batch_command, batch_command_digest));
    test_retirement_untimed_fixture(root, cwd, other, &executable, binary, environment, &batch_command, &batch);
    if (other >= 0) CHECK(close(other) == 0);
    /* The response file stays beside the command for the Python replay. */
    CHECK(unsetenv("TP_RETIREMENT_AMBIENT") == 0);
    if (binary >= 0) CHECK(close(binary) == 0);
    if (cwd >= 0) CHECK(close(cwd) == 0);
    if (leak >= 0) CHECK(close(leak) == 0);
    CHECK(unlink(executable_copy) == 0);
}
#endif
#endif
