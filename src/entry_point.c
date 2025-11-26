#pragma once

#include <entry_point.h>

#if BUSTER_UNITY_BUILD
#include <lib.c>
#else
#include <system_headers.h>
#endif

BUSTER_LOCAL ProcessResult buster_entry_point(int argc, char* argv[], char* envp[])
{
    ProcessResult result = {};
#ifdef _WIN32
    BOOL result = QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
    CHECK(result);

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
    program_state->input.argc = argc;
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
                    let create_result = pthread_create(&thread->handle, 0, &thread_os_entry_point, thread);
                    failure_count += create_result == 0;
                }

                if (failure_count == 0)
                {
                    for (u64 i = 0; i < thread_count; i += 1)
                    {
                        let thread = &threads[i];
                        void* return_value;
                        let join_result = pthread_join(thread->handle, &return_value);
                        failure_count += join_result == 0;
                        let thread_result = (ProcessResult)(u64)return_value;
                        if (thread_result != PROCESS_RESULT_SUCCESS)
                        {
                            result = thread_result;
                        }
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
                result = thread->entry_point(thread);
            }
    }

    return result;
}

int main(int argc, char* argv[], char* envp[])
{
    let result = buster_entry_point(argc, argv, envp);
    return (int)result;
}
