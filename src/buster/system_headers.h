#pragma once

#include <buster/base.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <limits.h>
#include <setjmp.h>

BUSTER_GLOBAL_LOCAL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {};
#elif defined(__APPLE__) || defined(__linux__)
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__linux__)
#include <sys/ptrace.h>
#include <sys/sysinfo.h>
#include <linux/limits.h>
#if BUSTER_USE_IO_RING
#include <liburing.h>
#endif
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
        struct
        {
            pthread_t handle;
            ThreadCallback* callback;
            void* argument;
        } thread;
        pthread_mutex_t mutex;
#if 0
        pthread_barrier_t barrier;
#endif
        struct
        {
            pthread_cond_t handle;
            pthread_mutex_t rw_lock;
        } condition_variable;
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
#endif
    u32 logical_thread_count;
    u32 padding;
    u64 page_size;
    u64 large_page_size;
    u64 allocation_granularity;
    ProcessInformation process;
};

#ifdef _WIN32
#define BUSTER_MAX_PATH_LENGTH (u64)MAX_PATH
#else
#define BUSTER_MAX_PATH_LENGTH (u64)(PATH_MAX)
#endif
