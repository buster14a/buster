#pragma once
#include <buster/base.h>

ENUM(ThreadSpawnPolicy,
    THREAD_SPAWN_POLICY_SINGLE_THREADED,
    THREAD_SPAWN_POLICY_SPAWN_SINGLE_THREAD,
    THREAD_SPAWN_POLICY_SATURATE_LOGICAL_CORES,
    THREAD_SPAWN_POLICY_SATURATE_PHYSICAL_CORES);

STRUCT(ProtectionFlags)
{
    u64 read:1;
    u64 write:1;
    u64 execute:1;
    u64 reserved:61;
};

STRUCT(MapFlags)
{
    u64 priv:1;
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

#if BUSTER_KERNEL == 0

typedef void ThreadReturnType;
typedef ThreadReturnType ThreadCallback(void*);
STRUCT(ThreadCreateOptions)
{
    ThreadCallback* callback;
    void* argument;
};

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
    Input,
    Output,
    Error);

STRUCT(ProcessSpawnResult)
{
    OsProcessHandle* handle;
    OsFileDescriptor* pipes[(size_t)StandardStream::Count][2];
};

STRUCT(ProcessSpawnOptions)
{
    u64 capture:(size_t)StandardStream::Count;
    u64 reserved:sizeof(u64)*8-(size_t)StandardStream::Count;
};

STRUCT(ProcessWaitResult)
{
    ByteSlice streams[(size_t)StandardStream::Count];
    ProcessResult result;
    u8 reserved[4];
};

STRUCT(OsError)
{
    u32 v;
};

#define BUSTER_OS_ERROR_BUFFER_MAX_LENGTH (BUSTER_KB(64))

BUSTER_F_DECL OsError os_get_last_error();
BUSTER_F_DECL StringOs os_error_write_message(StringOs string, OsError error);
BUSTER_F_DECL ProcessSpawnResult os_process_spawn(StringOs first_argument, StringOsList argv, StringOsList envp, ProcessSpawnOptions options);
BUSTER_F_DECL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn);
BUSTER_F_DECL StringOs os_get_environment_variable(StringOs variable);

BUSTER_F_DECL void os_make_directory(StringOs path);
BUSTER_F_DECL OsFileDescriptor* os_file_open(StringOs path, OpenFlags flags, OpenPermissions permissions);
BUSTER_F_DECL u64 os_file_get_size(OsFileDescriptor* file_descriptor);
BUSTER_F_DECL FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_F_DECL void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_F_DECL u64 os_file_read(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count);
BUSTER_F_DECL bool os_file_close(OsFileDescriptor* file_descriptor);

BUSTER_F_DECL StringOs os_path_absolute(StringOs buffer, StringOs relative_file_path);
BUSTER_F_DECL OsFileDescriptor* os_get_stdout();
BUSTER_F_DECL OsThreadHandle* os_thread_create(ThreadCreateOptions options);
BUSTER_F_DECL bool os_thread_join(OsThreadHandle* handle);

ENUM(ProgramFlag,
    Verbose,
    Ci,
    Test_Persist);
STRUCT(ProgramInput)
{
    StringOsList argv;
    StringOsList envp;
    FLAG_ARRAY_U64(flags, ProgramFlag);
    ThreadSpawnPolicy thread_spawn_policy;
    u8 reserved[4];
};

STRUCT(ProgramState)
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called:1;
    u64 _is_debugger_present:1;
    u64 reserved:62;
};

STRUCT(LaneContext)
{
    u64 lane_index;
    u64 lane_count;
    OsBarrierHandle* barrier;
    u64* broadcast_memory;
};

STRUCT(ThreadContext)
{
    Arena* arenas[(u64)ScratchArenaId::Count];
    LaneContext lane_context;
};

BUSTER_V_DECL ProgramState* program_state;

[[noreturn]] [[gnu::cold]] BUSTER_F_DECL void os_fail();
[[gnu::noreturn]] BUSTER_F_DECL void os_exit(u32 code);

BUSTER_F_DECL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map);
BUSTER_F_DECL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock);
BUSTER_F_DECL bool os_unreserve(void* address, u64 size);

BUSTER_F_DECL bool os_is_tty(OsFileDescriptor* file);
BUSTER_F_DECL OsModuleHandle* os_dynamic_library_load(StringOs library);
BUSTER_F_DECL void os_dynamic_library_unload(OsModuleHandle* module);
BUSTER_F_DECL OsSymbol* os_dynamic_library_function_load(OsModuleHandle* module, String8 symbol);
BUSTER_F_DECL u32 os_get_logical_thread_count();
BUSTER_F_DECL u64 os_get_page_size();
BUSTER_F_DECL OsProcessHandle* os_get_current_process_handle();
BUSTER_F_DECL OsThreadHandle* os_get_current_thread_handle();
BUSTER_F_DECL void os_thread_set_name(StringOs thread_name);

[[gnu::cold]] BUSTER_F_DECL bool is_debugger_present();

BUSTER_F_DECL u64 os_now_microseconds();

BUSTER_F_DECL ThreadContext* thread_context_allocate();
BUSTER_F_DECL void thread_context_select(ThreadContext* context);
BUSTER_F_DECL void thread_context_release(ThreadContext* context);
BUSTER_F_DECL LaneContext thread_context_set_lane(LaneContext lane_context);
BUSTER_F_DECL void thread_context_lane_barrier_wait(void* broadcast_pointer, u64 broadcast_size, u64 broadcast_source_lane_index);
BUSTER_F_DECL Arena* thread_context_get_scratch(Arena** conflicts, u64 count);

BUSTER_F_DECL OsMutexHandle* os_mutex_allocate();
BUSTER_F_DECL void os_mutex_take(OsMutexHandle* mutex);
BUSTER_F_DECL void os_mutex_drop(OsMutexHandle* mutex);

BUSTER_F_DECL OsConditionVariableHandle* os_condition_variable_allocate();
BUSTER_F_DECL void os_condition_variable_release(OsConditionVariableHandle* condition_variable);
BUSTER_F_DECL bool os_condition_variable_wait(OsConditionVariableHandle* condition_variable, OsMutexHandle* mutex, u64 endt_us);
BUSTER_F_DECL void os_condition_variable_broadcast(OsConditionVariableHandle* condition_variable);

BUSTER_F_DECL OsBarrierHandle* os_barrier_allocate(u32 count);

BUSTER_F_DECL void lane_sync();
BUSTER_F_DECL u64 lane_index();
#define lane_sync_u64(pointer, source_lane_index) thread_context_lane_barrier_wait((pointer), sizeof(*(pointer)), (source_lane_index))

#if BUSTER_USE_IO_RING
BUSTER_DECL IoRingSubmission io_ring_prepare_open(char* path, u64 user_data);
BUSTER_DECL u32 io_ring_submit_and_wait_all();
#endif
