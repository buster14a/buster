#pragma once

#include <buster/entry_point.h>
#include <buster/system_headers.h>
#include <buster/target.h>
#include <buster/arena.h>
#include <buster/string.h>

OsState os_state;

#if BUSTER_LINK_LIBC
#if BUSTER_FUZZ
BUSTER_EXPORT s32 LLVMFuzzerTestOneInput(const u8* pointer, size_t size)
{
    return buster_fuzz(pointer, size);
}
#else
ProcessResult buster_argument_process(SliceString8 argument_pointer, SliceString8 environment_pointer, u64 argument_index, String8 argument)
{
    BUSTER_UNUSED(argument_pointer);
    BUSTER_UNUSED(environment_pointer);
    BUSTER_UNUSED(argument_index);

    String8 flag_string_starts[] = {
        [PROGRAM_FLAG_VERBOSE] = S8("--verbose="),
        [PROGRAM_FLAG_CI] = S8("--ci="),
        [PROGRAM_FLAG_TEST_PERSIST] = S8("--test-persist="),
    };

    BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(flag_string_starts) == PROGRAM_FLAG_COUNT);

    ProcessResult result = PROCESS_RESULT_FAILED;
    BooleanArgumentProcessResult process_result = boolean_argument_process(flag_string_starts, BUSTER_ARRAY_LENGTH(flag_string_starts), program_state->input.flags, PROGRAM_FLAG_COUNT, argument);
    if (process_result.valid && process_result.index < (u64)PROGRAM_FLAG_COUNT)
    {
        result = PROCESS_RESULT_SUCCESS;
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

BUSTER_GLOBAL_LOCAL void install_signal_handlers(void)
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

BUSTER_GLOBAL_LOCAL ProcessResult buster_entry_point(StringOsList argv, StringOsList envp)
{
    os_state.arena = arena_create((ArenaCreation){0});
    SliceString8 arguments = os_string_list_to_slice_string(os_state.arena, argv);
    SliceString8 environment = os_string_list_to_slice_string(os_state.arena, envp);
    
#ifdef _WIN32
    {
        LARGE_INTEGER i;
        if (QueryPerformanceFrequency(&i))
        {
            os_state.frequency = (u64)i.QuadPart;
        }
    }
    WSADATA WinSockData;
    WSAStartup(MAKEWORD(2, 2), &WinSockData);
#if defined(_MSC_VER)
    GUID guid = WSAID_MULTIPLE_RIO;
    DWORD rio_byte = 0;
    SOCKET Sock = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
    WSAIoctl(Sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), (void**)&w32_rio_functions, sizeof(w32_rio_functions), &rio_byte, 0, 0);
    closesocket(Sock);
#endif
#endif
    os_state.entity_arena = arena_create((ArenaCreation){0});
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_init(&os_state.entity_mutex, 0);
#elif defined(_WIN32)
    InitializeCriticalSection(&os_state.entity_mutex);
#endif
    os_state.logical_thread_count = os_get_logical_thread_count();
    os_state.page_size = os_get_page_size();
    os_state.large_page_size = BUSTER_MB(2);
    os_state.allocation_granularity = os_state.page_size;
    os_state.process.handle = os_get_current_process_handle();

    cpu_detect_model();

    ThreadContext* thread_context = thread_context_allocate();
    thread_context_select(thread_context);
    os_thread_set_name(S8("main_thread"));

    program_state->arena = arena_create((ArenaCreation){0});
    program_state->input.arguments = arguments;
    program_state->input.environment = environment;

    ProcessResult result = process_arguments();

    if (result == PROCESS_RESULT_SUCCESS)
    {
        result = entry_point();
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
    result = (int)buster_entry_point(GetCommandLineW(), GetEnvironmentStringsW());
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
