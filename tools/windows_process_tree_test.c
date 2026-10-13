// Native deadline controls for bootstrap_process_owner_test (#3690).
// Include the production owner with test-only delay seams; no production
// environment variable or alternate launch protocol enables these delays.
// main owns portable validation and Windows deadline cases; witness is the
// bounded native payload used to prove whether launch occurred.
#define WINDOWS_PROCESS_TREE_TEST 1
#define main tree_production_main
#include "windows_process_tree.c"
#undef main
#include <stdlib.h>

static unsigned tree_test_checks;
static unsigned tree_test_failures;
static char const* tree_test_case = "portable";
#define TREE_TEST(value) do { tree_test_checks += 1; if (!(value)) { tree_test_failures += 1; \
    fprintf(stderr, "WINDOWS_PROCESS_TREE_TEST_FAIL case=%s line=%u expression=%s\n", \
            tree_test_case, (unsigned)__LINE__, #value); } } while (0)

#if BUSTER_WINDOWS
// Independent reference clock: deadlines are absolute across both processes.
static bool tree_test_now(bool tick_clock, u64* output)
{
    bool valid;
    u64 value = 0;
    if (tick_clock)
    {
        value = GetTickCount64() * UINT64_C(1000000);
        valid = true;
    }
    else
    {
        LARGE_INTEGER counter, frequency;
        valid = QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency) &&
                counter.QuadPart >= 0 && frequency.QuadPart > 0;
        if (valid)
        {
            u64 rate = (u64)frequency.QuadPart;
            u64 seconds = (u64)counter.QuadPart / rate;
            u64 remainder = (u64)counter.QuadPart % rate;
            valid = seconds <= UINT64_MAX / UINT64_C(1000000000) &&
                    remainder <= UINT64_MAX / UINT64_C(1000000000);
            if (valid) value = seconds * UINT64_C(1000000000) + remainder * UINT64_C(1000000000) / rate;
        }
    }
    if (valid) *output = value;
    return valid;
}

static bool tree_test_number(char const* record, char const* name, u64* output)
{
    char key[64];
    int count = snprintf(key, sizeof(key), "\"%s\":", name);
    char const* field = count > 0 && (size_t)count < sizeof(key) ? strstr(record, key) : 0;
    char* end = 0;
    unsigned long long number = field ? strtoull(field + count, &end, 10) : 0;
    bool valid = field && end != field + count && (*end == ',' || *end == '}');
    if (valid) *output = (u64)number;
    return valid;
}

static void tree_test_deadline(char const* mode, bool tick_clock)
{
    unsigned failures_before = tree_test_failures;
    tree_test_case = mode;
    WCHAR directory[MAX_PATH] = {0}, marker[MAX_PATH] = {0}, status[MAX_PATH] = {0}, staging[MAX_PATH] = {0};
    WCHAR cancel_path[MAX_PATH] = {0};
    WCHAR executable[MAX_PATH] = {0}, command[TREE_COMMAND_CAPACITY] = {0}, job[128] = {0};
    DWORD directory_count = GetTempPathW(MAX_PATH, directory);
    bool paths = directory_count && directory_count < MAX_PATH && GetTempFileNameW(directory, L"bpt", 0, marker);
    TREE_TEST(paths);
    if (paths)
    {
        TREE_TEST(DeleteFileW(marker));
        int count = swprintf(status, MAX_PATH, L"%ls.json", marker);
        paths = count > 0 && count < MAX_PATH;
        count = swprintf(staging, MAX_PATH, L"%ls.tmp", status);
        paths = count > 0 && count < MAX_PATH && paths;
        count = swprintf(cancel_path, MAX_PATH, L"%ls.cancel", status);
        paths = count > 0 && count < MAX_PATH && paths;
        DWORD length = GetModuleFileNameW(0, executable, MAX_PATH);
        paths = length && length < MAX_PATH && paths;
        count = swprintf(job, BUSTER_ARRAY_LENGTH(job), L"Local\\buster-native-deadline-%lu-%llu",
                         GetCurrentProcessId(), (unsigned long long)GetTickCount64());
        paths = count > 0 && (size_t)count < BUSTER_ARRAY_LENGTH(job) && paths;
        TREE_TEST(paths);
        bool expired = strcmp(mode, "expired-start") == 0;
        bool startup = strcmp(mode, "startup-cannot-restart") == 0;
        bool cleanup = strcmp(mode, "separate-cleanup-allowance") == 0;
        bool late_cleanup = strcmp(mode, "cleanup-cannot-restart") == 0;
        bool late_status = strcmp(mode, "late-publication-fails") == 0;
        bool payload_status = strcmp(mode, "payload-124") == 0 || strcmp(mode, "payload-125") == 0;
        bool cancel_before = strcmp(mode, "cancel-before-launch") == 0;
        bool cancel_startup = strcmp(mode, "cancel-during-startup") == 0;
        bool cancel_runtime = strcmp(mode, "cancel-during-runtime") == 0;
        bool cancel = cancel_before || cancel_startup || cancel_runtime;
        bool normal = cleanup || late_cleanup || late_status || payload_status;
        unsigned root_code = strcmp(mode, "payload-124") == 0 ? 124u : (strcmp(mode, "payload-125") == 0 ? 125u : 17u);
        count = swprintf(command, TREE_COMMAND_CAPACITY, L"\"%ls\" witness \"%ls\" %u %u", executable, marker,
                         normal || expired || startup ? 0u : 30000u, root_code);
        TREE_TEST(count > 0 && count < TREE_COMMAND_CAPACITY);
        u64 now = 0;
        TREE_TEST(tree_test_now(tick_clock, &now));
        u64 deadline = expired ? now - 1 : now + UINT64_C(1500000000);
        PROCESS_INFORMATION writer = {0};
        if (cancel_before)
        {
            FILE* request = _wfopen(cancel_path, L"wb");
            TREE_TEST(request);
            if (request) TREE_TEST(fclose(request) == 0);
        }
        else if (cancel_runtime)
        {
            WCHAR writer_command[TREE_COMMAND_CAPACITY];
            count = swprintf(writer_command, TREE_COMMAND_CAPACITY, L"\"%ls\" cancel \"%ls\" %u", executable,
                             cancel_path, 750u);
            TREE_TEST(count > 0 && count < TREE_COMMAND_CAPACITY);
            STARTUPINFOW startup_info = {.cb = sizeof(startup_info)};
            TREE_TEST(CreateProcessW(0, writer_command, 0, 0, FALSE, 0, 0, 0, &startup_info, &writer));
        }
        tree_test_startup_cancel_path = cancel_startup ? cancel_path : 0;
        tree_test_startup_delay_ms = startup ? 2000 : 0;
        tree_test_cleanup_delay_ms = cleanup ? 2000 : (late_cleanup ? 3500 : 0);
        tree_test_status_delay_ms = late_status ? 3500 : 0;
        // Publication shares the cleanup budget. The separate-allowance case
        // spends 2 s of simulated cleanup, and loaded hosted Windows runners
        // have published for over 1 s (#3816), so it gets headroom past 3 s.
        // The late cases keep 3 s so their 3.5 s delays still exceed it.
        unsigned cleanup_budget_ms = cleanup ? 5000u : 3000u;
        if (!paths) tree_test_launch_context = (TreeTestLaunchContext){0};
        int code = paths ? tree_launch(job, cleanup_budget_ms, command, deadline, tick_clock, status) : TREE_EXIT_DRAIN_FAILED;
        int expected = late_cleanup || late_status ? TREE_EXIT_DRAIN_FAILED : (normal || cancel ? 0 : TREE_EXIT_RUNTIME_TIMEOUT);
        TREE_TEST(tree_test_launch_context.startup_cancel_created == cancel_startup);
        TREE_TEST(code == (late_cleanup || late_status ? TREE_EXIT_DRAIN_FAILED :
                          (normal || cancel ? 0 : TREE_EXIT_RUNTIME_TIMEOUT)));
        if (writer.hProcess)
        {
            TREE_TEST(WaitForSingleObject(writer.hProcess, 5000) == WAIT_OBJECT_0);
            DWORD writer_code = 2;
            TREE_TEST(GetExitCodeProcess(writer.hProcess, &writer_code) && writer_code == 0);
            TREE_TEST(CloseHandle(writer.hThread));
            TREE_TEST(CloseHandle(writer.hProcess));
        }
        char record[2048] = {0};
        FILE* file = paths ? _wfopen(status, L"rb") : 0;
        TREE_TEST(file);
        if (file)
        {
            size_t size = fread(record, 1, sizeof(record) - 1, file);
            TREE_TEST(!ferror(file) && feof(file) && size < sizeof(record) - 1);
            TREE_TEST(fclose(file) == 0);
            TREE_TEST(strstr(record, late_cleanup ? "\"complete\":false" : "\"complete\":true"));
            TREE_TEST(strstr(record, normal || cancel ? "\"runtime_timed_out\":false" :
                                                              "\"runtime_timed_out\":true"));
            TREE_TEST(strstr(record, cancel ? "\"cancelled\":true" : "\"cancelled\":false"));
            u64 recorded = 0, finished = 0, cleanup_started = 0, cleanup_finished = 0, root_pid = 0;
            TREE_TEST(tree_test_number(record, "runtime_deadline_ns", &recorded) && recorded == deadline);
            TREE_TEST(tree_test_number(record, "root_finished_ns", &finished));
            TREE_TEST(tree_test_number(record, "cleanup_started_ns", &cleanup_started));
            TREE_TEST(tree_test_number(record, "cleanup_finished_ns", &cleanup_finished));
            TREE_TEST(tree_test_number(record, "root_pid", &root_pid));
            TREE_TEST(cleanup_finished >= cleanup_started);
            if (normal)
            {
                u64 recorded_code = 0;
                TREE_TEST(tree_test_number(record, "root_returncode", &recorded_code) && recorded_code == root_code);
            }
            if (cleanup || late_cleanup)
            {
                TREE_TEST(finished > 0 && finished <= deadline && cleanup_started >= finished);
                TREE_TEST(cleanup_finished > deadline);
                TREE_TEST(late_cleanup ? cleanup_finished - cleanup_started > UINT64_C(3000000000) :
                                        cleanup_finished - cleanup_started < UINT64_C(3000000000));
            }
            if (cancel)
            {
                TREE_TEST(finished == 0);
                TREE_TEST(tree_test_launch_context.cancel_observed_ns > 0 &&
                          tree_test_launch_context.cancel_observed_ns <= cleanup_started);
            }
            if (expired || startup || cancel_before || cancel_startup) TREE_TEST(root_pid == 0);
            else TREE_TEST(root_pid != 0);
        }
        bool executed = GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES;
        TREE_TEST(executed == !(expired || startup || cancel_before || cancel_startup));
        if (executed) TREE_TEST(DeleteFileW(marker));
        if (GetFileAttributesW(status) != INVALID_FILE_ATTRIBUTES) TREE_TEST(DeleteFileW(status));
        TREE_TEST(GetFileAttributesW(staging) == INVALID_FILE_ATTRIBUTES);
        if (cancel) TREE_TEST(DeleteFileW(cancel_path));
        if (code != expected || tree_test_failures != failures_before)
        {
            TreeTestLaunchContext const* context = &tree_test_launch_context;
            TreeLaunchStatus const* observed = &context->status;
            fprintf(stderr,
                "WINDOWS_PROCESS_TREE_TEST_CONTEXT case=%s clock=%s code=%d expected=%d paths=%u "
                "startup_delay_ms=%u cleanup_delay_ms=%u status_delay_ms=%u cleanup_budget_ms=%u "
                "complete=%u clock_valid=%u cancelled=%u runtime_timed_out=%u root_status_known=%u root_returncode=%lu "
                "root_pid=%lu startup_cancel_created=%u cancel_observed_ns=%llu "
                "runtime_deadline_ns=%llu root_finished_ns=%llu cleanup_started_ns=%llu cleanup_finished_ns=%llu "
                "cleanup_limit_ns=%llu publication_started_ns=%llu publication_finished_ns=%llu "
                "publication_clock_valid=%u publication_ok=%u publication_stage=%s win32_error_valid=%u win32_error=%lu "
                "publication_marks_clock_valid=%u publication_attributes_ns=%llu publication_create_ns=%llu "
                "publication_write_ns=%llu publication_flush_ns=%llu publication_close_ns=%llu publication_rename_ns=%llu\n",
                mode, tick_clock ? "tick" : "qpc", code, expected, (unsigned)paths,
                tree_test_startup_delay_ms, tree_test_cleanup_delay_ms, tree_test_status_delay_ms, cleanup_budget_ms,
                (unsigned)observed->complete, (unsigned)observed->clock_valid, (unsigned)observed->cancelled,
                (unsigned)observed->runtime_timed_out, (unsigned)observed->root_status_known,
                (unsigned long)observed->root_returncode, (unsigned long)observed->root_pid,
                (unsigned)context->startup_cancel_created, (unsigned long long)context->cancel_observed_ns,
                (unsigned long long)observed->runtime_deadline_ns,
                (unsigned long long)observed->root_finished_ns, (unsigned long long)observed->cleanup_started_ns,
                (unsigned long long)observed->cleanup_finished_ns, (unsigned long long)context->cleanup_limit_ns,
                (unsigned long long)context->publication_started_ns, (unsigned long long)context->publication_finished_ns,
                (unsigned)context->publication_clock_valid, (unsigned)context->publication_ok,
                context->publication_stage ? context->publication_stage : (context->publication_ok ? "published" : "not-attempted"),
                (unsigned)context->publication_error_valid, (unsigned long)context->publication_error,
                (unsigned)context->publication_marks_clock_valid,
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_ATTRIBUTES],
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_CREATE],
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_WRITE],
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_FLUSH],
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_CLOSE],
                (unsigned long long)context->publication_mark_ns[TREE_TEST_PUBLICATION_MARK_RENAME]);
            fprintf(stderr, "WINDOWS_PROCESS_TREE_TEST_STATUS case=%s record=%s\n", mode, record[0] ? record : "<missing>");
        }
        tree_test_startup_cancel_path = 0;
        tree_test_startup_delay_ms = 0;
        tree_test_cleanup_delay_ms = 0;
        tree_test_status_delay_ms = 0;
    }
}
#endif

int main(int argc, char** argv)
{
    int result = 0;
#if BUSTER_WINDOWS
    if (argc == 4 && strcmp(argv[1], "cancel") == 0)
    {
        Sleep((DWORD)strtoul(argv[3], 0, 10));
        FILE* request = fopen(argv[2], "wb");
        result = request ? 0 : 2;
        if (request && fclose(request) != 0) result = 2;
    }
    else if (argc == 5 && strcmp(argv[1], "witness") == 0)
    {
        FILE* file = fopen(argv[2], "wb");
        result = 2;
        if (file)
        {
            bool written = fputs("payload executed\n", file) >= 0;
            written = fclose(file) == 0 && written;
            if (written)
            {
                Sleep((DWORD)strtoul(argv[3], 0, 10));
                result = (int)strtoul(argv[4], 0, 10);
            }
        }
    }
    else
#else
    (void)argv;
#endif
    if (argc == 1)
    {
        u64 value = 0;
        TREE_TEST(tree_decimal("18446744073709551615", &value) && value == UINT64_MAX);
        TREE_TEST(!tree_decimal("18446744073709551616", &value));
        TREE_TEST(!tree_decimal("-1", &value));
        TREE_TEST(!tree_decimal("1x", &value));
#if BUSTER_WINDOWS
        tree_test_deadline("expired-start", false);
        tree_test_deadline("expired-start", true);
        tree_test_deadline("startup-cannot-restart", false);
        tree_test_deadline("runtime-expiry", false);
        tree_test_deadline("separate-cleanup-allowance", false);
        tree_test_deadline("cleanup-cannot-restart", false);
        tree_test_deadline("late-publication-fails", false);
        tree_test_deadline("payload-124", false);
        tree_test_deadline("payload-125", false);
        tree_test_deadline("cancel-before-launch", false);
        tree_test_deadline("cancel-during-startup", false);
        tree_test_deadline("cancel-during-startup", true);
        tree_test_deadline("cancel-during-runtime", false);
#endif
        printf("WINDOWS_PROCESS_TREE_TESTS checks=%u failures=%u platform=%s\n", tree_test_checks,
               tree_test_failures, BUSTER_WINDOWS ? "windows" : "portable");
        result = tree_test_failures ? 1 : 0;
    }
    else if (!result) result = TREE_EXIT_USAGE;
    return result;
}
