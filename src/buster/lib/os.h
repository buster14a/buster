#pragma once
#include <buster/lib/base.h>

typedef struct ProtectionFlags ProtectionFlags;
struct ProtectionFlags
{
    u64 read : 1;
    u64 write : 1;
    u64 execute : 1;
    u64 reserved : 61;
};

typedef struct MapFlags MapFlags;
struct MapFlags
{
    u64 priv : 1;
    u64 anonymous : 1;
    u64 no_reserve : 1;
    u64 populate : 1;
    u64 reserved : 60;
};

typedef struct OpenFlags OpenFlags;
struct OpenFlags
{
    u64 truncate : 1;
    u64 execute : 1;
    u64 write : 1;
    u64 read : 1;
    u64 create : 1;
    u64 directory : 1;
    u64 reserved : 58;
};

typedef struct OpenPermissions OpenPermissions;
struct OpenPermissions
{
    u64 read : 1;
    u64 write : 1;
    u64 execute : 1;
    u64 reserved : 61;
};

typedef struct FileStats FileStats;
struct FileStats
{
    u64 modified_time_s;
    u64 modified_time_ns;
    u64 size;
};

typedef struct FileStatsOptions FileStatsOptions;
struct FileStatsOptions
{
    union
    {
        u64 raw;
        struct
        {
            u64 size : 1;
            u64 modified_time : 1;
            u64 reserved : 62;
        };
    };
};

#if BUSTER_KERNEL == 0

typedef struct ThreadCreateOptions ThreadCreateOptions;
struct ThreadCreateOptions
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

typedef struct ThreadInitialization ThreadInitialization;
struct ThreadInitialization
{
    u8 foo;
};

typedef enum StandardStream
{
    STANDARD_STREAM_INPUT,
    STANDARD_STREAM_OUTPUT,
    STANDARD_STREAM_ERROR,
    STANDARD_STREAM_COUNT,
} StandardStream;

typedef struct ProcessSpawnResult ProcessSpawnResult;
struct ProcessSpawnResult
{
    OsProcessHandle* handle;
    OsFileDescriptor* pipes[STANDARD_STREAM_COUNT][2];
};

typedef struct ProcessSpawnOptions ProcessSpawnOptions;
struct ProcessSpawnOptions
{
    u64 capture : (size_t)STANDARD_STREAM_COUNT;
    u64 use_process_environment : 1;
    u64 reserved : sizeof(u64) * 8 - (size_t)STANDARD_STREAM_COUNT - 1;
};

typedef struct ProcessWaitResult ProcessWaitResult;
struct ProcessWaitResult
{
    ByteSlice streams[(size_t)STANDARD_STREAM_COUNT];
    ProcessResult result;
    u8 reserved[4];
};

typedef struct OsError OsError;
struct OsError
{
    u32 v;
};

#define BUSTER_OS_ERROR_BUFFER_MAX_LENGTH (BUSTER_KB(64))

BUSTER_F_DECL OsError os_get_last_error(void);
BUSTER_F_DECL String8 string8_from_os_error(Arena* arena, OsError error, bool null_terminate);
BUSTER_F_DECL ProcessSpawnResult os_process_spawn(SliceString8 argv, SliceString8 environment_keys, SliceString8 environment_values,
                                                  ProcessSpawnOptions options);
BUSTER_F_DECL ProcessWaitResult os_process_wait_sync(Arena* arena, ProcessSpawnResult spawn);
BUSTER_F_DECL String8 os_get_environment_variable(String8 variable);

BUSTER_F_DECL void os_make_directory(String8 path);
BUSTER_F_DECL bool os_file_delete(String8 path);
// Deletes `path` and everything under it. Symbolic links are removed as links
// rather than followed, so the walk cannot escape the tree it was given.
// Returns whether the tree is gone; a missing `path` counts as success.
BUSTER_F_DECL bool os_directory_delete(String8 path);
BUSTER_F_DECL OsFileDescriptor* os_file_open(String8 path, OpenFlags flags, OpenPermissions permissions);
BUSTER_F_DECL u64 os_file_get_size(OsFileDescriptor* file_descriptor);
BUSTER_F_DECL FileStats os_file_get_stats(OsFileDescriptor* file_descriptor, FileStatsOptions options);
BUSTER_F_DECL void os_file_write(OsFileDescriptor* file_descriptor, ByteSlice buffer);
BUSTER_F_DECL u64 os_file_read(OsFileDescriptor* file_descriptor, ByteSlice buffer, u64 byte_count);
BUSTER_F_DECL bool os_file_close(OsFileDescriptor* file_descriptor);

BUSTER_F_DECL String8 os_path_absolute(Arena* arena, String8 relative_file_path, bool null_terminate);
BUSTER_F_DECL OsFileDescriptor* os_get_stdout(void);
BUSTER_F_DECL OsFileDescriptor* os_get_standard_stream(StandardStream stream);
BUSTER_F_DECL OsThreadHandle* os_thread_create(ThreadCreateOptions options);
BUSTER_F_DECL bool os_thread_join(OsThreadHandle* handle);

typedef enum ProgramFlag
{
    PROGRAM_FLAG_VERBOSE,
    PROGRAM_FLAG_CI,
    PROGRAM_FLAG_TEST_PERSIST,
    PROGRAM_FLAG_COUNT,
} ProgramFlag;

typedef struct ProgramInput ProgramInput;
struct ProgramInput
{
    SliceString8 arguments;
    SliceString8 environment_keys;
    SliceString8 environment_values;
    StringOsList raw_arguments;
    StringOsList raw_environment;
    FLAG_ARRAY_U64(flags, ProgramFlag, PROGRAM_FLAG_COUNT);
    u8 reserved[4];
};

typedef struct ProgramState ProgramState;
struct ProgramState
{
    ProgramInput input;
    Arena* arena;
    u64 is_debugger_present_called : 1;
    u64 _is_debugger_present : 1;
    u64 reserved : 62;
};

typedef struct LaneContext LaneContext;
struct LaneContext
{
    u64 lane_index;
    u64 lane_count;
    OsBarrierHandle* barrier;
    u64* broadcast_memory;
};

typedef struct ThreadContext ThreadContext;
struct ThreadContext
{
    Arena* arenas[(u64)SCRATCH_ARENA_COUNT];
    LaneContext lane_context;
};

BUSTER_V_DECL ProgramState* program_state;

BUSTER_NORETURN BUSTER_COLD BUSTER_F_DECL void os_fail_va(u32 line, String8 function, String8 file, String8 context, ...);
BUSTER_NORETURN BUSTER_COLD BUSTER_F_DECL void os_fail_raw(u32 line, String8 function, String8 file, String8 context);

#define os_fail_message(message)                                                                                                                               \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_va((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message))
#define os_fail_message_format(message, ...)                                                                                                                   \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_va((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message, __VA_ARGS__))
// Reports without allocating. Use for invariants that can fail while the thread
// context is unselected, where the formatted reporter would need the very
// scratch arena whose absence is being reported.
#define os_fail_message_raw(message)                                                                                                                           \
    (((void)(is_debugger_present() ? (BUSTER_BREAKPOINT(), 0) : 0)), os_fail_raw((u32)__LINE__, BUSTER_FUNCTION, S8(__FILE__), message))
#define os_fail() os_fail_message(S8("internal error"))
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#define BUSTER_CHECK_RAW(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message_raw(S8("assertion failed")), 0) : 0))
#define BUSTER_TODO_MESSAGE(message, ...)                                                                                                                      \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        os_fail_message_format(message, __VA_ARGS__);                                                                                                          \
    } while (1)
#define BUSTER_TODO()                                                                                                                                          \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        os_fail_message(S8("TODO"));                                                                                                                           \
    } while (1)

BUSTER_NORETURN BUSTER_F_DECL void os_exit(u32 code);

BUSTER_F_DECL void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map);
BUSTER_F_DECL bool os_commit(void* address, u64 size, ProtectionFlags protection, bool lock);
BUSTER_F_DECL bool os_unreserve(void* address, u64 size);
BUSTER_F_DECL bool os_flush_instruction_cache(void* address, u64 size);

BUSTER_F_DECL bool os_is_tty(OsFileDescriptor* file);
BUSTER_F_DECL OsModuleHandle* os_dynamic_library_load(String8 library);
BUSTER_F_DECL void os_dynamic_library_unload(OsModuleHandle* module);
BUSTER_F_DECL OsSymbol* os_dynamic_library_function_load(OsModuleHandle* module, String8 symbol);
BUSTER_F_DECL u32 os_get_logical_thread_count(void);
BUSTER_F_DECL u64 os_get_page_size(void);
BUSTER_F_DECL u64 os_get_current_process_id(void);
BUSTER_F_DECL OsProcessHandle* os_get_current_process_handle(void);
BUSTER_F_DECL OsThreadHandle* os_get_current_thread_handle(void);
BUSTER_F_DECL void os_thread_set_name(String8 thread_name);

BUSTER_COLD BUSTER_F_DECL bool is_debugger_present(void);

BUSTER_F_DECL u64 os_now_microseconds(void);

BUSTER_F_DECL ThreadContext* thread_context_allocate(void);
BUSTER_F_DECL ThreadContext* thread_context_selected(void);
BUSTER_F_DECL void thread_context_select(ThreadContext* context);
BUSTER_F_DECL void thread_context_release(ThreadContext* context);
BUSTER_F_DECL Arena* thread_context_get_scratch(Arena** conflicts, u64 count);

BUSTER_F_DECL void flag_set_ex(u64* flag_pointer, u64 flag_count, u64 flag_index, bool flag_value);
BUSTER_F_DECL bool flag_get_ex(u64* flag_pointer, u64 flag_count, u64 flag_index);

#define flag_get(arr, Count, e) flag_get_ex((arr), (Count), (u64)(e))
#define flag_set(arr, Count, e, v) flag_set_ex((arr), (Count), (e), (v))

typedef struct BooleanArgumentProcessResult BooleanArgumentProcessResult;
struct BooleanArgumentProcessResult
{
    u64 index;
    bool valid;
    u8 reserved[7];
};
BUSTER_F_DECL BooleanArgumentProcessResult boolean_argument_process(String8* flag_string_start_pointer, u64 flag_string_start_count, u64* flag_pointer,
                                                                    u64 flag_count, String8 argument);

BUSTER_F_DECL bool program_flag_get(ProgramFlag flag);
BUSTER_F_DECL String8 executable_resolve_in_path(Arena* arena, String8 file);
