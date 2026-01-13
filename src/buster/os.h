#pragma once
#include <buster/base.h>

ENUM(ThreadSpawnPolicy,
    THREAD_SPAWN_POLICY_SINGLE_THREADED,
    THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD,
    THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES,
    THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES,
);


STRUCT(ProtectionFlags)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(MapFlags)
{
    u64 private:1;
    u64 anonymous:1;
    u64 no_reserve:1;
    u64 populate:1;
    u64 reserved:60;
};

STRUCT(OpenFlags)
{
    u64 truncate:1;
    u64 execute:1;
    u64 write:1;
    u64 read:1;
    u64 create:1;
    u64 directory:1;
    u64 reserved:58;
};

STRUCT(OpenPermissions)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(FileStats)
{
    u64 modified_time_s;
    u64 modified_time_ns;
    u64 size;
};

STRUCT(FileStatsOptions)
{
    union
    {
        u64 raw;
        struct
        {
            u64 size:1;
            u64 modified_time:1;
            u64 reserved:62;
        };
    };
};

STRUCT(ThreadCreateOptions)
{
    u8 reserved[4];
};

#if BUSTER_KERNEL == 0
typedef
#if defined(__linux__) || defined(__APPLE__)
void*
#elif defined(_WIN32)
unsigned long
#endif
ThreadReturnType;

typedef ThreadReturnType ThreadCallback(void*);

typedef 
#ifdef _WIN32
u64
#else
u128
#endif
TimeDataType;
#endif

STRUCT(ThreadInitialization)
{
};

ENUM_T(StandardStream, u8,
    STANDARD_STREAM_IN,
    STANDARD_STREAM_OUT,
    STANDARD_STREAM_ERROR,
    STANDARD_STREAM_COUNT,
);

STRUCT(ProcessSpawnResult)
{
    ProcessHandle* handle;
    FileDescriptor* pipes[STANDARD_STREAM_COUNT][2];
};

STRUCT(ProcessSpawnOptions)
{
    u64 capture:STANDARD_STREAM_COUNT;
    u64 reserved:sizeof(u64)*8-STANDARD_STREAM_COUNT;
};

STRUCT(ProcessWaitResult)
{
    ByteSlice streams[STANDARD_STREAM_COUNT];
    ProcessResult result;
    u8 reserved[4];
};

STRUCT(OsError)
{
    u32 v;
};

#define BUSTER_OS_ERROR_BUFFER_MAX_LENGTH (BUSTER_KB(64))

BUSTER_DECL OsError os_get_last_error();
BUSTER_DECL StringOs os_error_write_message(StringOs string, OsError error);
BUSTER_DECL ProcessSpawnResult os_process_spawn(StringOs first_argument, StringOsList argv, StringOsList envp, ProcessSpawnOptions options);
BUSTER_DECL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn);
BUSTER_DECL StringOs os_get_environment_variable(StringOs variable);

BUSTER_DECL void os_make_directory(StringOs path);
BUSTER_DECL FileDescriptor* os_file_open(StringOs path, OpenFlags flags, OpenPermissions permissions);
BUSTER_DECL u64 os_file_get_size(FileDescriptor* file_descriptor);
BUSTER_DECL FileStats os_file_get_stats(FileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_DECL void os_file_write(FileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_DECL u64 os_file_read(FileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count);
BUSTER_DECL bool os_file_close(FileDescriptor* file_descriptor);

BUSTER_DECL StringOs os_path_absolute(StringOs buffer, StringOs relative_file_path);
BUSTER_DECL FileDescriptor* os_get_stdout();
BUSTER_DECL ThreadHandle* os_thread_create(ThreadCallback* callback, ThreadCreateOptions options);
BUSTER_DECL u32 os_thread_join(ThreadHandle* handle);

BUSTER_DECL BUSTER_THREAD_LOCAL_DECL Thread* thread;
#include <buster/string_os.h>
STRUCT(ProgramInput)
{
    StringOsList argv;
    StringOsList envp;
    ThreadSpawnPolicy thread_spawn_policy;
    u32 verbose:1;
    u32 reserved:31;
};

STRUCT(ProgramState)
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called:1;
    u64 _is_debugger_present:1;
    u64 reserved:62;
};

BUSTER_DECL ProgramState* program_state;
BUSTER_DECL Arena* thread_arena();

[[noreturn]] [[gnu::cold]] BUSTER_DECL void os_fail();
[[gnu::noreturn]] BUSTER_DECL void os_exit(u32 code);
BUSTER_DECL bool os_initialize_time();

BUSTER_DECL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map);
BUSTER_DECL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock);

#if BUSTER_USE_IO_RING
BUSTER_DECL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data);
BUSTER_DECL u32 io_ring_submit_and_wait_all();
#endif
