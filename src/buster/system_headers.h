#pragma once

#include <buster/base.h>

#if defined(__linux__)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <linux/limits.h>
#include <linux/fs.h>
#include <spawn.h>
#include <errno.h>
#if BUSTER_USE_PTHREAD
#include <pthread.h>
#endif
#if BUSTER_USE_IO_RING
#include <liburing.h>
#endif
#endif

#if defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/syslimits.h>
#include <spawn.h>
#include <errno.h>
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <limits.h>
#include <setjmp.h>

BUSTER_GLOBAL_LOCAL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {};
#endif

struct Thread
{
    Arena* arena;
    ThreadEntryPoint* entry_point;
    ThreadHandle* handle;
};

#ifdef _WIN32
#define BUSTER_MAX_PATH_LENGTH (u64)MAX_PATH
#else
#define BUSTER_MAX_PATH_LENGTH (u64)(PATH_MAX)
#endif
