#pragma once
#include <buster/lib/base.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <limits.h>
#include <setjmp.h>

#if defined(__MINGW32__)
WINBASEAPI HRESULT WINAPI SetThreadDescription(HANDLE thread, PCWSTR description);
#endif

#if defined(_MSC_VER)
#include <mswsock.h>
BUSTER_V_DECL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions;
#endif

#elif defined(__APPLE__) || defined(__linux__)
#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__linux__)
#include <sys/sysinfo.h>
#include <linux/limits.h>
#include <ucontext.h>
#endif

#if defined(__APPLE__)
#include <sys/syslimits.h>
#include <sys/sysctl.h>
#endif
#endif

typedef enum OsEntityKind
{
    OS_ENTITY_KIND_THREAD,
    OS_ENTITY_KIND_MUTEX,
    OS_ENTITY_KIND_RW_MUTEX,
    OS_ENTITY_KIND_CONDITION_VARIABLE,
    OS_ENTITY_KIND_BARRIER,
} OsEntityKind;

typedef struct OsEntity OsEntity;
struct OsEntity
{
    OsEntity* next;
    OsEntityKind kind;
    u32 padding;
    union
    {
#if defined(_WIN32)
        u8 foo;
        CRITICAL_SECTION mutex;
        struct
        {
            HANDLE handle;
            ThreadCallback* callback;
            void* argument;
        } thread;
        // Generation-counting barrier over a mutex and a condition variable:
        // pthread_barrier_t does not exist on Apple platforms, so every
        // platform shares this one algorithm instead. Single-threaded builds
        // compile the barrier out entirely; tcc's own minimal Win32 headers
        // predate CONDITION_VARIABLE, and the tcc-built build.c bootstrap is
        // always single-threaded.
#if !BUSTER_SINGLE_THREADED
        struct
        {
            CRITICAL_SECTION mutex;
            CONDITION_VARIABLE condition;
            u64 generation;
            u32 threshold;
            u32 arrived;
        } barrier;
#endif
//         struct
//         {
//             pthread_cond_t handle;
//             pthread_mutex_t rw_lock;
//         } condition_variable;
#else
        struct
        {
            pthread_t handle;
            ThreadCallback* callback;
            void* argument;
        } thread;
        pthread_mutex_t mutex;
#if !BUSTER_SINGLE_THREADED
        struct
        {
            pthread_mutex_t mutex;
            pthread_cond_t condition;
            u64 generation;
            u32 threshold;
            u32 arrived;
        } barrier;
#endif
        struct
        {
            pthread_cond_t handle;
            pthread_mutex_t rw_lock;
        } condition_variable;
#endif
    };
};

typedef struct ProcessInformation ProcessInformation;
struct ProcessInformation
{
    OsProcessHandle* handle;
};

typedef struct OsState OsState;
struct OsState
{
    Arena* arena;
    Arena* entity_arena;
    OsEntity* entity_free_list;
#if defined(__linux__) || defined(__APPLE__)
    pthread_mutex_t entity_mutex;
#elif defined(_WIN32)
    CRITICAL_SECTION entity_mutex;
#else
#error unsupported platform
#endif
#if defined(_WIN32)
    u64 frequency;
#endif
    u32 logical_thread_count;
    u32 padding;
    u64 page_size;
    u64 large_page_size;
    u64 allocation_granularity;
    ProcessInformation process;
};

BUSTER_V_DECL OsState os_state;

#ifdef _WIN32
#define BUSTER_MAX_PATH_LENGTH (u64) MAX_PATH
#else
#define BUSTER_MAX_PATH_LENGTH (u64)(PATH_MAX)
#endif
