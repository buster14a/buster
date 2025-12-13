#pragma once

#include <buster/entry_point.h>

#if BUSTER_UNITY_BUILD
#include <buster/lib.c>
#else
#include <buster/system_headers.h>
#endif

BUSTER_LOCAL ProcessResult buster_entry_point(OsStringList argv, OsStringList envp)
{
    ProcessResult result = {};
    os_initialize_time();
#ifdef _WIN32
    WSADATA WinSockData;
    WSAStartup(MAKEWORD(2, 2), &WinSockData);
    GUID guid = WSAID_MULTIPLE_RIO;
    DWORD rio_byte = 0;
    SOCKET Sock = socket(AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP);
    WSAIoctl(Sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), (void**)&w32_rio_functions, sizeof(w32_rio_functions), &rio_byte, 0, 0);
    closesocket(Sock);
#else
#endif

    program_state->arena = arena_create((ArenaInitialization){});
    program_state->input.argv = argv;
    program_state->input.envp = envp;

    result = process_arguments();

    if (result == PROCESS_RESULT_SUCCESS)
    {
#if BUSTER_USE_PTHREAD
            u64 thread_count = 0;

            switch (program_state->input.thread_spawn_policy)
            {
                break; case THREAD_SPAWN_POLICY_SINGLE_THREADED:
                {
                    thread_count = 0;
                }
                break; case THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD:
                {
                    thread_count = 1;
                }
                break; case THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES:
                {
                }
                break; case THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES:
                {
                }
            }

            if (thread_count != 0)
            {
                Thread* threads = arena_allocate(program_state->arena, Thread, thread_count);

                u64 failure_count = 0;
                for (u64 i = 0; i < thread_count; i += 1)
                {
                    let thread = &threads[i];
                    thread->entry_point = thread_entry_point;
                    let handle = os_thread_create(thread_os_entry_point, (ThreadCreateOptions){});
                    thread->handle = handle;
                    failure_count += handle == 0;
                }

                if (failure_count == 0)
                {
                    for (u64 i = 0; i < thread_count; i += 1)
                    {
                        let thread = &threads[i];
                        let exit_code = os_thread_join(thread->handle);
                        let thread_result = (ProcessResult)exit_code;
                        if (thread_result != PROCESS_RESULT_SUCCESS)
                        {
                            result = thread_result;
                        }
                        failure_count += thread_result != PROCESS_RESULT_SUCCESS;
                    }
                }

                if (failure_count == 0)
                {
                    result = PROCESS_RESULT_FAILED;
                }
            }
            else
#endif
            {
                Thread thread_buffer = {};
                thread = &thread_buffer;
                thread->entry_point = thread_entry_point;
                thread->arena = arena_create((ArenaInitialization){});
                BUSTER_CHECK(program_state->input.thread_spawn_policy == THREAD_SPAWN_POLICY_SINGLE_THREADED);
                result = thread->entry_point();
            }
    }

    return result;
}

#if BUSTER_LINK_LIBC
BUSTER_EXPORT int main(int argc, char* argv[], char* envp[])
{
    BUSTER_UNUSED(argc);
    let result = buster_entry_point((OsStringList)argv, (OsStringList)envp);
    return (int)result;
}
#else
#if _WIN32
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

int _fltused = 1;

#if _WIN32
BUSTER_EXPORT void __chkstk(void)
{
}
#endif
#endif
