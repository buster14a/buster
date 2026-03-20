#pragma once

#include <buster/entry_point.h>
#include <buster/system_headers.h>
#include <buster/target.h>
#include <buster/assertion.h>
#include <buster/arena.h>
#include <buster/arguments.h>

BUSTER_V_IMPL BUSTER_THREAD_LOCAL_DECL ThreadContext* thread_context_thread_local;
BUSTER_V_IMPL OsState os_state;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZING
BUSTER_EXPORT s32 LLVMFuzzerTestOneInput(const u8* pointer, size_t size)
{
    return buster_fuzz(pointer, size);
}
#else


BUSTER_GLOBAL_LOCAL OsThreadHandle** async_threads;
BUSTER_GLOBAL_LOCAL u32 async_thread_count = 0;
BUSTER_GLOBAL_LOCAL u32 global_async_exit = 0;
BUSTER_GLOBAL_LOCAL OsConditionVariableHandle* async_tick_start_condition_variable;
BUSTER_GLOBAL_LOCAL OsMutexHandle* async_tick_start_mutex;
BUSTER_GLOBAL_LOCAL OsMutexHandle* async_tick_stop_mutex;
BUSTER_GLOBAL_LOCAL u32 async_loop_again = 0;
BUSTER_GLOBAL_LOCAL u32 async_loop_again_high_priority = 0;

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool is_async_thread = false;

#define ins_atomic_u128_eval_cond_assign(x,k,c) (u32)__atomic_compare_exchange_n((__int128 *)(x),(__int128 *)(c),*(__int128 *)(k),0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)
#define ins_atomic_u64_eval(x)                  __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define ins_atomic_u64_inc_eval(x)              (__atomic_fetch_add((u64*)(x), 1, __ATOMIC_SEQ_CST) + 1)
#define ins_atomic_u64_dec_eval(x)              (__atomic_fetch_sub((u64*)(x), 1, __ATOMIC_SEQ_CST) - 1)
#define ins_atomic_u64_eval_assign(x,c)         __atomic_exchange_n(x, c, __ATOMIC_SEQ_CST)
#define ins_atomic_u64_add_eval(x,c)            (__atomic_fetch_add((u64*)(x), c, __ATOMIC_SEQ_CST) + (c))
#define ins_atomic_u64_eval_cond_assign(x,k,c)  ({ u64 _new = (c); __atomic_compare_exchange_n((u64*)(x),&_new,(k),0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); _new; })
#define ins_atomic_u32_eval(x)                  __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define ins_atomic_u32_inc_eval(x)              (__atomic_fetch_add((u32*)(x), 1, __ATOMIC_SEQ_CST) + 1)
#define ins_atomic_u32_dec_eval(x)              (__atomic_fetch_sub((u32*)(x), 1, __ATOMIC_SEQ_CST) - 1)
#define ins_atomic_u32_add_eval(x,c)            (__atomic_fetch_add((u32*)(x), c, __ATOMIC_SEQ_CST) + (c))
#define ins_atomic_u32_eval_assign(x,c)         __atomic_exchange_n((x), (c), __ATOMIC_SEQ_CST)
#define ins_atomic_u32_eval_cond_assign(x,k,c)  ({ u32 _new = (c); __atomic_compare_exchange_n((u32*)(x),&_new,(k),0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); _new; })
#define ins_atomic_u8_eval_assign(x,c)          __atomic_exchange_n((x), (c), __ATOMIC_SEQ_CST)
#define ins_atomic_u8_or(x,c)                   __atomic_fetch_or((U8 *)(x), (U8)(c), __ATOMIC_SEQ_CST)

BUSTER_F_IMPL ProcessResult buster_argument_process(StringOsList argument_pointer, StringOsList environment_pointer, u64 argument_index, StringOs argument)
{
    BUSTER_UNUSED(argument_pointer);
    BUSTER_UNUSED(environment_pointer);
    BUSTER_UNUSED(argument_index);

    StringOs flag_string_starts[] = {
        [(u64)ProgramFlag::Verbose] = SOs("--verbose="),
        [(u64)ProgramFlag::Ci] = SOs("--ci="),
        [(u64)ProgramFlag::Test_Persist] = SOs("--test-persist="),
    };

    static_assert(BUSTER_ARRAY_LENGTH(flag_string_starts) == (u64)ProgramFlag::Count);

    ProcessResult result = ProcessResult::Failed;
    let process_result = boolean_argument_process(flag_string_starts, BUSTER_ARRAY_LENGTH(flag_string_starts), program_state->input.flags, (u64)ProgramFlag::Count, argument);
    if (process_result.valid && process_result.index < (u64)ProgramFlag::Count)
    {
        result = ProcessResult::Success;
    }

    return result;
}

#if defined(__linux__)
BUSTER_GLOBAL_LOCAL void signal_handler(int sig, siginfo_t *info, void *arg)
{
    BUSTER_UNUSED(sig);
    BUSTER_UNUSED(info);
    BUSTER_UNUSED(arg);
}
#endif

BUSTER_GLOBAL_LOCAL void install_signal_handlers()
{
#if defined(__linux__)
      // install signal handler for the crash call stacks
  {
    struct sigaction handler = { .sa_sigaction = &signal_handler, .sa_flags = SA_SIGINFO, };
    sigfillset(&handler.sa_mask);
    sigaction(SIGILL, &handler, NULL);
    sigaction(SIGTRAP, &handler, NULL);
    sigaction(SIGABRT, &handler, NULL);
    sigaction(SIGFPE, &handler, NULL);
    sigaction(SIGBUS, &handler, NULL);
    sigaction(SIGSEGV, &handler, NULL);
    sigaction(SIGQUIT, &handler, NULL);
  }
#else
#endif
}

#if !BUSTER_SINGLE_THREADED
BUSTER_GLOBAL_LOCAL void async_thread_entry_point(void* arg)
{
    let lane = *(LaneContext*)arg;
    thread_context_set_lane(lane);
    is_async_thread = true;

    let scratch = scratch_begin(0, 0);
    os_thread_set_name(string8_format(scratch.arena, S8("async_thread_{u64}"), lane_index()));

    u32 need_exit = 0;

    while (!need_exit)
    {
        if (lane_index() == 0)
        {
            if (!ins_atomic_u32_eval(&async_loop_again))
            {
                os_mutex_take(async_tick_start_mutex);
                os_condition_variable_wait(async_tick_start_condition_variable, async_tick_start_mutex, os_now_microseconds() + (1000 * 1000));
                os_mutex_drop(async_tick_start_mutex);
            }

            ins_atomic_u32_eval_assign(&async_loop_again, 0);
            ins_atomic_u32_eval_assign(&async_loop_again_high_priority, 0);
        }

        lane_sync();

        // TODO: work
        async_user_tick();

        lane_sync();

        if (lane_index() == 0)
        {
            need_exit = ins_atomic_u32_eval(&global_async_exit);
        }

        lane_sync_u64(&need_exit, 0);
    }

    scratch_end(scratch);
}
#endif

BUSTER_GLOBAL_LOCAL ProcessResult buster_entry_point(StringOsList argv, StringOsList envp)
{
#ifdef _WIN32
    {
        LARGE_INTEGER i;
        if (QueryPerformanceFrequency(&i))
        {
            (u64)
        }
    }
    WSADATA WinSockData;
    WSAStartup(MAKEWORD(2, 2), &WinSockData);
    GUID guid = WSAID_MULTIPLE_RIO;
    DWORD rio_byte = 0;
    SOCKET Sock = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
    WSAIoctl(Sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), (void**)&w32_rio_functions, sizeof(w32_rio_functions), &rio_byte, 0, 0);
    closesocket(Sock);
#else
#endif
    os_state.arena = arena_create((ArenaCreation){});
    os_state.entity_arena = arena_create((ArenaCreation){});
    pthread_mutex_init(&os_state.entity_mutex, 0);
    os_state.logical_thread_count = os_get_logical_thread_count();
    os_state.page_size = os_get_page_size();
    os_state.large_page_size = BUSTER_MB(2);
    os_state.allocation_granularity = os_state.page_size;
    os_state.process.handle = os_get_current_process_handle();

    cpu_detect_model();

    let thread_context = thread_context_allocate();
    thread_context_select(thread_context);

    os_thread_set_name(SOs("main_thread"));

    async_tick_start_condition_variable = os_condition_variable_allocate();
    async_tick_start_mutex = os_mutex_allocate();
    async_tick_stop_mutex = os_mutex_allocate();

    program_state->arena = arena_create((ArenaCreation){});
    program_state->input.argv = argv;
    program_state->input.envp = envp;

    let result = process_arguments();

    if (result == ProcessResult::Success)
    {
#if !BUSTER_SINGLE_THREADED
        u64 lane_broadcast_value = 0;
        {
            u64 main_thread_count = 1;
            async_thread_count = os_state.logical_thread_count;
            let clamped_main_thread_count = BUSTER_MIN(async_thread_count, main_thread_count);
            async_thread_count -= clamped_main_thread_count;
            // TODO: support argument
            async_thread_count = BUSTER_MAX(1, async_thread_count);

            let arena = program_state->arena;
            let lane_contexts = arena_allocate(arena, LaneContext, async_thread_count);
            let barrier = os_barrier_allocate(async_thread_count);
            async_threads = arena_allocate(arena, OsThreadHandle*, async_thread_count);

            for (u32 i = 0; i < async_thread_count; i += 1)
            {
                LaneContext* lane_context = &lane_contexts[i];
                lane_context->lane_index = i;
                lane_context->lane_count = async_thread_count;
                lane_context->barrier = barrier;
                lane_context->broadcast_memory = &lane_broadcast_value;
                async_threads[i] = os_thread_create((ThreadCreateOptions){ .callback = &async_thread_entry_point, .argument = lane_context });
            }
        }
#endif

        result = entry_point();

#if !BUSTER_SINGLE_THREADED
        ins_atomic_u32_inc_eval(&global_async_exit);
        os_condition_variable_broadcast(async_tick_start_condition_variable);

        for (u64 i = 0; i < async_thread_count; i += 1)
        {
            OsThreadHandle* thread = async_threads[i];
            os_thread_join(thread);
        }
#endif
    }

    return result;
}

int main(int argc, char* argv[], char* envp[])
{
    BUSTER_UNUSED(argc);
    int result;
#if defined(_WIN32)
    BUSTER_UNUSED(argv);
    BUSTER_UNUSED(envp);
    buster_entry_point(GetCommandLineW(), GetEnvironmentStringsW());
#else
    result = (int)buster_entry_point((StringOsList)argv, (StringOsList)envp);
#endif
    return result;
}
#endif
#else
#if defined(_WIN32)
[[gnu::noreturn]] BUSTER_EXPORT void mainCRTStartup()
{
    let result = buster_entry_point(GetCommandLineW(), GetEnvironmentStringsW());
    ExitProcess((UINT)result);
}
#endif

BUSTER_EXPORT void *memset(void* pointer, int c, size_t n)
{
    u8* restrict p = (u8*)pointer;

    for (u64 i = 0; i < n; i += 1)
    {
        p[i] = c;
    }

    return pointer;
}

BUSTER_EXPORT int memcmp(const void* s1, const void* s2, size_t n)
{
    let a = (u8*)s1;
    let b = (u8*)s2;

    int result = 0;

    for (u64 i = 0; i < n; i += 1)
    {
        result = a - b;
        if (result)
        {
            break;
        }
    }

    return result;
}

BUSTER_EXPORT void *memcpy(void* restrict destination, const void* restrict source, size_t n)
{
    for (u64 i = 0; i < n; i += 1)
    {
        ((u8*)(destination))[i] = ((u8*)(source))[i];
    }

    return destination;
}

BUSTER_EXPORT size_t strlen(const char* s)
{
    let i = s;
    let it = (char*)s;

    while (*it)
    {
        it += 1;
    }

    return (size_t)(it - i);
}

[[gnu::noreturn]] BUSTER_EXPORT void abort()
{
    BUSTER_TRAP();
}

BUSTER_EXPORT int atoi(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long atol(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT long long atoll(const char* pointer)
{
    return parse_decimal_scalar(pointer);
}

BUSTER_EXPORT double frexp(double x, int* e)
{
    // TODO:
    BUSTER_UNUSED(x);
    BUSTER_UNUSED(e);
    return 0.0;
}

[[gnu::noreturn]] BUSTER_EXPORT void longjmp(jmp_buf env, int val)
{
    // TODO:
    BUSTER_TRAP();
}

BUSTER_EXPORT void *memchr(const void* s, int c, size_t n)
{
    u8* pointer = s;
    u8 ch = (u8)c;
    u64 i;
    for (i = 0; i < n; i += 1)
    {
        if (pointer[i] == ch)
        {
            break;
        }
    }

    return pointer + i == pointer + n ? 0 : pointer + i;
}

BUSTER_IMPL int _fltused = 1;

#if defined(_WIN32)
BUSTER_EXPORT void __chkstk(void)
{
}
#endif
#endif
