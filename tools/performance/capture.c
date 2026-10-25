/* Original Linux sampling collector. The source is the kernel's native perf
 * event record stream and the exact perf_event_attr bytes; no collector or
 * unwinder library is used. Native trace conversion is a separate stage.
 * Runtime layout: capture_collect, ring drain, exact execution/resource receipt.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define main buster_performance_library_cli
#include "main.c"
#undef main
#include <linux/perf_event.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>

#define BUSTER_CAPTURE_PAGES 1024u
#define BUSTER_CAPTURE_TIMEOUT_NS UINT64_C(60000000000)

static uint64_t capture_clock(clockid_t clock)
{
    struct timespec time = {0};
    bool valid = clock_gettime(clock, &time) == 0 && time.tv_sec >= 0 && (uint64_t)time.tv_sec <= UINT64_MAX / UINT64_C(1000000000);
    uint64_t result = valid ? (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec : 0;
    return result;
}

static uint64_t capture_now(void)
{
    uint64_t result = capture_clock(CLOCK_MONOTONIC);
    return result;
}

static bool capture_write(const char *directory, const char *name, PerfBytes bytes)
{
    char path[4096];
    int size = snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = size > 0 && (size_t)size < sizeof(path) ? fopen(path, "wb") : NULL;
    bool result = file != NULL;
    if (file != NULL)
    {
        result = fwrite(bytes.data, 1, bytes.size, file) == bytes.size;
        result = fclose(file) == 0 && result;
    }
    return result;
}

static bool capture_calibrate(const char *directory)
{
    uint64_t before = capture_now();
    uint64_t boot = capture_clock(CLOCK_BOOTTIME);
    uint64_t real = capture_clock(CLOCK_REALTIME);
    uint64_t after = capture_now();
    bool result = before != 0 && boot != 0 && real != 0 && after >= before;
    char output[512];
    int size = snprintf(output, sizeof(output), "{\"monotonic_ns\":\"%llu\",\"boottime_ns\":\"%llu\",\"realtime_ns\":\"%llu\",\"read_bracket_ns\":\"%llu\",\"method\":\"sequential clock reads bracketed by CLOCK_MONOTONIC; midpoint alignment; error bounded by read bracket absent concurrent wall-clock adjustment\"}\n", (unsigned long long)(before + (after - before) / 2), (unsigned long long)boot, (unsigned long long)real, (unsigned long long)(after - before));
    result = result && size > 0 && (size_t)size < sizeof(output) && capture_write(directory, "clock-snapshot.json", (PerfBytes){.data = (const unsigned char *)output, .size = size > 0 ? (size_t)size : 0});
    return result;
}

static bool capture_drain(struct perf_event_mmap_page *metadata, size_t mapping_size, PerfBuffer *records)
{
    uint64_t offset = metadata->data_offset;
    uint64_t size = metadata->data_size;
    bool result = offset <= mapping_size && size <= mapping_size - offset && size != 0 && (size & (size - 1)) == 0;
    uint64_t head = __atomic_load_n(&metadata->data_head, __ATOMIC_ACQUIRE);
    uint64_t tail = metadata->data_tail;
    result = result && head >= tail && head - tail <= size;
    const unsigned char *ring = result ? (const unsigned char *)metadata + offset : NULL;
    unsigned char record[65536];
    while (tail < head && result)
    {
        for (size_t i = 0; i < sizeof(struct perf_event_header); ++i)
        {
            record[i] = ring[(tail + i) & (size - 1)];
        }
        struct perf_event_header header;
        memcpy(&header, record, sizeof(header));
        result = header.size >= sizeof(header) && header.size <= head - tail && header.size <= size;
        if (result)
        {
            for (size_t i = sizeof(header); i < header.size; ++i)
            {
                record[i] = ring[(tail + i) & (size - 1)];
            }
            result = perf_buffer_append(records, (PerfBytes){.data = record, .size = header.size});
            tail += header.size;
        }
    }
    if (result)
    {
        __atomic_store_n(&metadata->data_tail, tail, __ATOMIC_RELEASE);
    }
    return result;
}

static bool capture_receipt(const char *directory, int argc, char **argv, int command, pid_t child, unsigned int cpu, uint64_t period, bool sampling, bool collected, bool timed_out, int status, uint64_t start, uint64_t end, const struct rusage *usage, size_t record_bytes, const char *binary_path, const char *binary_sha256, bool binary_unchanged)
{
    struct utsname system = {0};
    bool result = uname(&system) == 0;
    PerfBuffer output = {0};
    result = result && perf_buffer_append(&output, perf_literal("{\"producer\":\"buster-linux-perf-first-party-1\",\"source_format\":\"Linux perf_event_attr plus native perf event records\",\"profiled\":")) && perf_buffer_append(&output, perf_literal(sampling ? "true" : "false"));
    char numbers[1024];
    uint64_t user_ns = (uint64_t)usage->ru_utime.tv_sec * UINT64_C(1000000000) + (uint64_t)usage->ru_utime.tv_usec * 1000;
    uint64_t system_ns = (uint64_t)usage->ru_stime.tv_sec * UINT64_C(1000000000) + (uint64_t)usage->ru_stime.tv_usec * 1000;
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    char timing[256] = "\"start_ns\":null,\"end_ns\":null,\"wall_ns\":null,\"timing_reason\":\"launcher was not released or clock failed\"";
    if (start != 0 && end >= start)
    {
        snprintf(timing, sizeof(timing), "\"start_ns\":\"%llu\",\"end_ns\":\"%llu\",\"wall_ns\":\"%llu\",\"timing_reason\":null", (unsigned long long)start, (unsigned long long)end, (unsigned long long)(end - start));
    }
    int count = snprintf(numbers, sizeof(numbers), ",\"collector_complete\":%s,\"timeout\":%s,\"pid\":%ld,\"affinity_cpu\":%u,\"period_ns\":\"%llu\",\"timestamp_clock\":\"CLOCK_MONOTONIC\",\"perfetto_clock_id\":3,%s,\"user_ns\":\"%llu\",\"system_ns\":\"%llu\",\"peak_rss_kib\":\"%llu\",\"exit_code\":%d,\"signal\":%d,\"raw_record_bytes\":\"%zu\",\"kernel\":", collected ? "true" : "false", timed_out ? "true" : "false", (long)child, cpu, (unsigned long long)period, timing, (unsigned long long)user_ns, (unsigned long long)system_ns, (unsigned long long)usage->ru_maxrss, exit_code, signal, record_bytes);
    result = result && count > 0 && (size_t)count < sizeof(numbers) && perf_buffer_append(&output, (PerfBytes){.data = (const unsigned char *)numbers, .size = count > 0 ? (size_t)count : 0}) && perf_json_quote(&output, perf_literal(system.release));
    result = result && perf_buffer_append(&output, perf_literal(",\"host\":")) && perf_json_quote(&output, perf_literal(system.nodename));
    result = result && perf_buffer_append(&output, perf_literal(",\"binary\":{\"path\":")) && perf_json_quote(&output, perf_literal(binary_path)) && perf_buffer_append(&output, perf_literal(",\"sha256\":")) && perf_json_quote(&output, perf_literal(binary_sha256)) && perf_buffer_append(&output, perf_literal(binary_unchanged ? ",\"unchanged_after_execution\":true}" : ",\"unchanged_after_execution\":false}"));
    result = result && perf_buffer_append(&output, perf_literal(",\"argv\":["));
    for (int i = command; i < argc && result; ++i)
    {
        result = (i == command || perf_buffer_byte(&output, ',')) && perf_json_quote(&output, perf_literal(argv[i]));
    }
    result = result && perf_buffer_append(&output, perf_literal("],\"limitations\":[\"Execution wall time brackets launcher release through observed leader exit, including exec/loader work; user/system rusage has microsecond source resolution.\",\"Inherited sampling is restricted to the selected CPU and observed leader lifetime. A task that changes affinity or outlives the leader has unknown additional coverage.\",\"CPU-clock sample periods are source weights, not exact invocation counts or instrumented function durations. User-mode sampling does not prove the preceding period contained only user-mode work.\",\"Frame-pointer callchain completeness, symbol coverage, kernel losses, task lifetimes and initial scheduling state require separate source/import checks.\"]}\n")) && capture_write(directory, "execution.json", perf_buffer_bytes(&output));
    free(output.data);
    return result;
}

static int capture_collect(int argc, char **argv)
{
    bool sampling = argc > 1 && strcmp(argv[1], "--no-sampling") != 0;
    int first = sampling ? 1 : 2;
    uint64_t cpu_number = 0;
    uint64_t period = 0;
    bool valid = argc >= first + 5 && perf_uint64(perf_literal(argv[first + 1]), &cpu_number) && cpu_number < CPU_SETSIZE && perf_uint64(perf_literal(argv[first + 2]), &period) && period >= 1000000 && period <= UINT64_C(1000000000) && strcmp(argv[first + 3], "--") == 0;
    if (!valid)
    {
        fputs("usage: capture [--no-sampling] NEW_OUTPUT_DIRECTORY CPU PERIOD_NS -- COMMAND [ARG...]\n", stderr);
    }
    /* A failed child must not terminate the collector while releasing the
     * launch pipe; retain its failure receipt and original output instead. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    const char *directory = valid ? argv[first] : "";
    int command = first + 4;
    char binary_path[4096] = {0};
    char binary_sha256[65] = {0};
    PerfBuffer executable = {0};
    valid = valid && strchr(argv[command], '/') != NULL && realpath(argv[command], binary_path) != NULL && perf_read_file(binary_path, &executable) && perf_sha256(perf_buffer_bytes(&executable), binary_sha256);
    free(executable.data);
    executable = (PerfBuffer){0};
    valid = valid && mkdir(directory, 0755) == 0;
    char log_path[4096];
    int path_size = valid ? snprintf(log_path, sizeof(log_path), "%s/workload.log", directory) : 0;
    valid = valid && path_size > 0 && (size_t)path_size < sizeof(log_path);
    int log = valid ? open(log_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644) : -1;
    int barrier[2] = {-1, -1};
    valid = valid && log >= 0 && pipe2(barrier, O_CLOEXEC) == 0;
    pid_t child = valid ? fork() : -1;
    valid = valid && child >= 0;
    if (child == 0)
    {
        signal(SIGPIPE, SIG_DFL);
        close(barrier[1]);
        cpu_set_t affinity;
        CPU_ZERO(&affinity);
        CPU_SET((unsigned int)cpu_number, &affinity);
        bool ready = setpgid(0, 0) == 0 && sched_setaffinity(0, sizeof(affinity), &affinity) == 0 && dup2(log, STDOUT_FILENO) >= 0 && dup2(log, STDERR_FILENO) >= 0;
        close(log);
        unsigned char token = 0;
        ready = ready && read(barrier[0], &token, 1) == 1 && token == 1;
        close(barrier[0]);
        if (ready)
        {
            execv(binary_path, argv + command);
            dprintf(STDERR_FILENO, "capture child: exec failed: %s\n", strerror(errno));
        }
        _exit(126);
    }
    if (barrier[0] >= 0)
    {
        close(barrier[0]);
    }
    if (log >= 0)
    {
        close(log);
    }
    int pid_descriptor = valid ? (int)syscall(SYS_pidfd_open, child, 0) : -1;
    valid = valid && pid_descriptor >= 0;
    struct perf_event_attr attributes = {0};
    attributes.type = PERF_TYPE_SOFTWARE;
    attributes.size = sizeof(attributes);
    attributes.config = PERF_COUNT_SW_CPU_CLOCK;
    attributes.sample_period = period;
    attributes.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD | PERF_SAMPLE_CALLCHAIN;
    attributes.disabled = 1;
    attributes.inherit = 1;
    attributes.exclude_kernel = 1;
    attributes.exclude_hv = 1;
    attributes.mmap = 1;
    attributes.mmap2 = 1;
    attributes.comm = 1;
    attributes.comm_exec = 1;
    attributes.task = 1;
    attributes.context_switch = 1;
    attributes.sample_id_all = 1;
    attributes.exclude_callchain_kernel = 1;
    attributes.use_clockid = 1;
    attributes.clockid = CLOCK_MONOTONIC;
    attributes.sample_max_stack = 127;
    attributes.wakeup_events = 1;
    /* Linux refuses a sampled inherited event's mmap ring with cpu == -1.
     * The workload is explicitly pinned to this CPU; keep that capture scope
     * in the receipt rather than silently dropping inherited task samples. */
    int event = valid && sampling ? (int)syscall(SYS_perf_event_open, &attributes, child, (int)cpu_number, -1, PERF_FLAG_FD_CLOEXEC) : -1;
    if (valid && sampling && event < 0)
    {
        fprintf(stderr, "capture: perf_event_open failed: %s\n", strerror(errno));
        valid = false;
    }
    long page = sysconf(_SC_PAGESIZE);
    size_t mapping_size = page > 0 && page <= 65536 ? (size_t)page * (BUSTER_CAPTURE_PAGES + 1) : 0;
    void *mapping = MAP_FAILED;
    if (valid && sampling)
    {
        mapping = mapping_size != 0 ? mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, event, 0) : MAP_FAILED;
        if (mapping == MAP_FAILED)
        {
            fprintf(stderr, "capture: perf ring mmap failed: %s\n", strerror(errno));
        }
        valid = mapping != MAP_FAILED && ioctl(event, PERF_EVENT_IOC_RESET, 0) == 0 && ioctl(event, PERF_EVENT_IOC_ENABLE, 0) == 0;
    }
    valid = valid && capture_calibrate(directory);
    uint64_t start = valid ? capture_now() : 0;
    valid = valid && start != 0;
    if (valid)
    {
        unsigned char token = 1;
        valid = write(barrier[1], &token, 1) == 1;
    }
    if (barrier[1] >= 0)
    {
        close(barrier[1]);
    }
    PerfBuffer records = {0};
    bool reaped = false;
    bool timed_out = false;
    int status = 0;
    struct rusage usage = {0};
    while (valid && !reaped)
    {
        if (sampling)
        {
            valid = capture_drain(mapping, mapping_size, &records);
        }
        pid_t waited = wait4(child, &status, WNOHANG, &usage);
        reaped = waited == child;
        valid = valid && (waited >= 0 || errno == EINTR);
        uint64_t now = capture_now();
        timed_out = now == 0 || now < start || now - start > BUSTER_CAPTURE_TIMEOUT_NS;
        valid = valid && !timed_out;
        if (valid && !reaped)
        {
            struct pollfd descriptors[2] = {
                {.fd = pid_descriptor, .events = POLLIN},
                {.fd = event, .events = sampling ? POLLIN : 0}
            };
            int polled = poll(descriptors, sampling ? 2 : 1, 100);
            valid = polled >= 0 || errno == EINTR;
        }
    }
    uint64_t end = capture_now();
    if (event >= 0)
    {
        bool disabled = ioctl(event, PERF_EVENT_IOC_DISABLE, 0) == 0;
        valid = valid && disabled;
        if (mapping != MAP_FAILED)
        {
            bool drained = capture_drain(mapping, mapping_size, &records);
            valid = valid && drained;
        }
    }
    if (child > 0 && !reaped)
    {
        if (getpgid(child) == child)
        {
            kill(-child, SIGKILL);
        }
        else
        {
            kill(child, SIGKILL);
        }
        pid_t waited;
        do
        {
            waited = wait4(child, &status, 0, &usage);
        } while (waited < 0 && errno == EINTR);
        reaped = waited == child;
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, mapping_size);
    }
    if (event >= 0)
    {
        close(event);
    }
    if (pid_descriptor >= 0)
    {
        close(pid_descriptor);
    }
    char final_sha256[65];
    bool binary_unchanged = child > 0 && perf_read_file(binary_path, &executable) && perf_sha256(perf_buffer_bytes(&executable), final_sha256) && strcmp(binary_sha256, final_sha256) == 0;
    free(executable.data);
    valid = valid && binary_unchanged;
    bool files = child > 0 && capture_write(directory, "event-attributes.bin", (PerfBytes){.data = (const unsigned char *)&attributes, .size = sizeof(attributes)}) && capture_write(directory, "perf-event-records.bin", perf_buffer_bytes(&records)) && capture_receipt(directory, argc, argv, command, child, (unsigned int)cpu_number, period, sampling, valid && reaped, timed_out, status, start, end, &usage, records.size, binary_path, binary_sha256, binary_unchanged);
    free(records.data);
    valid = valid && files && reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!valid)
    {
        fputs("capture: collection or workload failed; retained output is not a qualified native profile\n", stderr);
    }
    return valid ? 0 : 1;
}

int main(int argc, char **argv)
{
    int result = capture_collect(argc, argv);
    return result;
}
