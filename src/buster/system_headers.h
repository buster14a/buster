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
#include <time.h>
#include <unistd.h>

#if BUSTER_USE_PTHREAD
#include <pthread.h>
#endif

#if defined(__linux__)
#include <sys/ptrace.h>
#include <linux/limits.h>
#include <linux/fs.h>
#if BUSTER_USE_IO_RING
#include <liburing.h>
#endif
#endif

#if defined(__APPLE__)
#include <sys/syslimits.h>
#include <sys/sysctl.h>
#endif
#endif

#include <math.h>

struct Thread
{
    Arena* arena;
    ThreadEntryPoint* entry_point;
    OsThreadHandle* handle;
};

#ifdef _WIN32
#define BUSTER_MAX_PATH_LENGTH (u64)MAX_PATH
#else
#define BUSTER_MAX_PATH_LENGTH (u64)(PATH_MAX)
#endif
