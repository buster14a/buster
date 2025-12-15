#pragma once

#include <buster/lib.h>

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

BUSTER_LOCAL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {};
#endif

STRUCT(IoRing)
{
#ifdef __linux__
#if BUSTER_USE_IO_RING
    struct io_uring linux_impl;
#endif
#endif
    u32 submitted_entry_count;
    u32 completed_entry_count;
};

struct Thread
{
    Arena* arena;
    ThreadEntryPoint* entry_point;
    IoRing ring;
    ThreadHandle* handle;
};

BUSTER_LOCAL constexpr u64 max_path_length =
#ifdef _WIN32
MAX_PATH
#else
PATH_MAX
#endif
;
