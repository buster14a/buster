// Standalone deterministic C debuggee for the RAD Debugger compatibility campaign.
// Build unchanged with Buster and a trusted compiler, then compare debugger observations.

#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#if defined(_MSC_VER)
#define DEBUGGEE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define DEBUGGEE_NOINLINE __attribute__((noinline))
#else
#define DEBUGGEE_NOINLINE
#endif

typedef struct DebuggeeRecord DebuggeeRecord;
struct DebuggeeRecord
{
    int tag;
    unsigned int flags : 3;
    unsigned int code : 13;
    int samples[3];
};

static DEBUGGEE_NOINLINE int debuggee_inner(int seed, const DebuggeeRecord *input_record)
{
    // The Win64 by-value aggregate uses CodeView S_DEFRANGE_REGISTER_REL_INDIR,
    // which pinned RAD does not parse. Keep the aggregate/array/bitfield oracle
    // on this normal local copy; by-value parameter evaluation is out of scope.
    DebuggeeRecord record = *input_record;
    volatile int inner_value = seed + record.samples[1];
    volatile int inner_total = inner_value + record.samples[0] + record.samples[2] + // RAD_BPT_INNER
                               (int)record.flags + (int)record.code + record.tag;
    return inner_total;
}

static DEBUGGEE_NOINLINE int debuggee_outer(int seed)
{
    int values[3] = {2, 3, 5};
    DebuggeeRecord record = {17, 5, 257, {7, 11, 13}};
    volatile int outer_value = seed + values[1];
    volatile int outer_result = debuggee_inner(outer_value, &record); // RAD_BPT_OUTER: inspect array and aggregate.
    volatile int final_result = outer_result + values[0] + values[1] + values[2] + seed;
    return final_result;
}

#if defined(__linux__) || defined(_WIN32)
typedef struct WorkerContext WorkerContext;
struct WorkerContext
{
    int seed;
    int result;
};
#endif

#if defined(__linux__) || defined(_WIN32)
#if defined(__linux__)
static void* debuggee_worker(void* opaque)
#else
static DWORD WINAPI debuggee_worker(LPVOID opaque)
#endif
{
    WorkerContext* context = opaque;
    volatile int worker_seed = context->seed;
    volatile int worker_result = debuggee_outer(worker_seed); // RAD_BPT_WORKER: child-thread stack.
    context->result = worker_result;
    return 0;
}
#endif

int main(void)
{
    int status = 0;

#if defined(_WIN32)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
    {
        fputs("RADDEBUGGER_DEBUGGEE failed to set stdout binary mode\n", stderr);
        abort();
    }
#endif
    volatile int main_seed = 3;
    volatile int main_result = debuggee_outer(main_seed);

#if defined(__linux__)
    WorkerContext worker = {7, 0};
    pthread_t thread;
    int create_status = pthread_create(&thread, 0, debuggee_worker, &worker);
    if (create_status == 0)
    {
        if (pthread_join(thread, 0) != 0) // RAD_BPT_JOIN: main waits while the child runs.
        {
            fputs("RADDEBUGGER_DEBUGGEE pthread_join failed\n", stderr);
            abort();
        }
        printf("RADDEBUGGER_DEBUGGEE main=%d worker=%d values=2,3,5 "
               "record=17,5,257,7,11,13 thread=joined\n",
               (int)main_result, worker.result);
    }
    else
    {
        fprintf(stderr, "RADDEBUGGER_DEBUGGEE pthread_create failed: %d\n", create_status);
        status = 1;
    }
#elif defined(_WIN32)
    WorkerContext worker = {7, 0};
    DWORD main_thread_id = GetCurrentThreadId();
    DWORD worker_thread_id = 0;
    DWORD wait_status;
    DWORD worker_exit_code;
    BOOL exit_status;
    BOOL close_status;
    HANDLE thread = CreateThread(0, 0, debuggee_worker, &worker, 0, &worker_thread_id);
    if (thread != 0)
    {
        wait_status = WaitForSingleObject(thread, INFINITE); // RAD_BPT_JOIN: main waits while the child runs.
        worker_exit_code = STILL_ACTIVE;
        exit_status = GetExitCodeThread(thread, &worker_exit_code);
        close_status = CloseHandle(thread);
        if (wait_status == WAIT_OBJECT_0 && exit_status && worker_exit_code == 0 && close_status &&
            main_thread_id != worker_thread_id)
        {
            printf("RADDEBUGGER_DEBUGGEE main=%d worker=%d values=2,3,5 "
                   "record=17,5,257,7,11,13 thread=joined\n",
                   (int)main_result, worker.result);
        }
        else
        {
            fputs("RADDEBUGGER_DEBUGGEE Windows thread join failed\n", stderr);
            status = 1;
        }
    }
    else
    {
        fprintf(stderr, "RADDEBUGGER_DEBUGGEE CreateThread failed: %lu\n", (unsigned long)GetLastError());
        status = 1;
    }
#else
    printf("RADDEBUGGER_DEBUGGEE main=%d worker=unavailable values=2,3,5 "
           "record=17,5,257,7,11,13 thread=not-built\n",
           (int)main_result);
#endif

    return status;
}
