#pragma once

#include <lib.h>

#if BUSTER_KERNEL == 0
#include <stdio.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <linux/limits.h>
#include <linux/fs.h>
#if BUSTER_USE_PTHREAD
#include <pthread.h>
#endif
#if BUSTER_USE_IO_RING
#include <liburing.h>
#endif

#elif defined(__APPLE__)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#elif _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
LOCAL RIO_EXTENSION_FUNCTION_TABLE w32_rio_functions = {};
#endif

STRUCT(IoRing)
{
#ifdef __linux__
#if BUSTER_USE_IO_RING
    struct io_uring linux_impl;
#endif
#else
#pragma error
#endif
    u32 submitted_entry_count;
    u32 completed_entry_count;
};

struct Thread
{
    Arena* arena;
    ThreadEntryPoint* entry_point;
    IoRing ring;
    pthread_t handle;
};
