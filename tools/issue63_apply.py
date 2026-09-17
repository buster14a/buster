#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text()


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one occurrence, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def matching_brace(text: str, opening: int) -> int:
    depth = 1
    index = opening + 1
    state = "normal"
    quote = ""
    while index < len(text):
        character = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "line_comment":
            if character == "\n":
                state = "normal"
        elif state == "block_comment":
            if character == "*" and following == "/":
                state = "normal"
                index += 1
        elif state == "string":
            if character == "\\":
                index += 1
            elif character == quote:
                state = "normal"
        else:
            if character == "/" and following == "/":
                state = "line_comment"
                index += 1
            elif character == "/" and following == "*":
                state = "block_comment"
                index += 1
            elif character in ('"', "'"):
                state = "string"
                quote = character
            elif character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
                if depth == 0:
                    return index
        index += 1
    raise RuntimeError("unmatched C brace")


def replace_function(path: str, signature: str, replacement: str) -> None:
    text = read(path)
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"{path}: missing function signature {signature!r}")
    if text.find(signature, start + 1) >= 0:
        raise RuntimeError(f"{path}: duplicate function signature {signature!r}")
    opening = text.find("{", start + len(signature))
    if opening < 0:
        raise RuntimeError(f"{path}: missing opening brace for {signature!r}")
    closing = matching_brace(text, opening)
    replacement = replacement.rstrip() + "\n"
    write(path, text[:start] + replacement + text[closing + 1 :])


def replace_between(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    text = read(path)
    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError(f"{path}: missing section start {start_marker!r}")
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{path}: missing section end {end_marker!r}")
    write(path, text[:start] + replacement.rstrip() + "\n\n" + text[end:])


# Name the three contracts independently while preserving BUSTER_CHECK's
# existing debug-assert/release-assume behavior for proven invariants.
replace_between(
    "src/buster/lib/os.h",
    "#define BUSTER_CHECK_RAW(ok)",
    "// Stated by every global table",
    r'''#define BUSTER_CHECK_RAW(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message_raw(S8("assertion failed")), 0) : 0))
#define BUSTER_ENSURE_RAW(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message_raw(S8("runtime failure")), 0) : 0))
#define BUSTER_ENSURE(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("runtime failure")), 0) : 0))
// Validation covers values derived from source text, object bytes, file sizes,
// system calls, or other fallible inputs. It must retain both its branch and
// failure in every build.
#define BUSTER_VALIDATE(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("validation failed")), 0) : 0))
// A debug check is diagnostic only. An assumption is reserved for facts the
// implementation has already proved; BUSTER_CHECK remains its compatibility
// spelling so existing invariant sites keep their optimized code shape.
#if BUSTER_OPTIMIZE
#define BUSTER_DEBUG_CHECK(ok) ((void)0)
#define BUSTER_ASSUME(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (BUSTER_UNREACHABLE(), 0) : 0))
#else
#define BUSTER_DEBUG_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#define BUSTER_ASSUME(ok) BUSTER_DEBUG_CHECK(ok)
#endif
#define BUSTER_CHECK(ok) BUSTER_ASSUME(ok)''',
)

# Deterministic, calling-thread-only failure injection for OS acquisitions.
replace_once(
    "src/buster/lib/os_internal.h",
    "#if BUSTER_INCLUDE_TESTS\n\n",
    r'''#if BUSTER_INCLUDE_TESTS

typedef enum OsResourceTestOperation
{
    OS_RESOURCE_TEST_RESERVE,
    OS_RESOURCE_TEST_COMMIT,
    OS_RESOURCE_TEST_BARRIER_CREATE,
    OS_RESOURCE_TEST_THREAD_CREATE,
    OS_RESOURCE_TEST_THREAD_JOIN,
    OS_RESOURCE_TEST_OPERATION_COUNT,
} OsResourceTestOperation;

// Fail exactly one selected operation after `successful_operations_before_failure`
// matching calls on the calling thread. The injected join failure is reported
// after a successful native join, so no live thread or handle is leaked by the
// test seam itself.
BUSTER_F_DECL void os_resource_test_fail_after(OsResourceTestOperation operation, u64 successful_operations_before_failure);
BUSTER_F_DECL void os_resource_test_clear(void);

''',
)

replace_once(
    "src/buster/lib/arena.h",
    "#if BUSTER_INCLUDE_TESTS\nBUSTER_F_DECL void arena_test_fail_next_commit(void);\n#endif",
    "#if BUSTER_INCLUDE_TESTS\nBUSTER_F_DECL bool arena_test_allocate_commit_attempt(Arena* arena, u64 aligned_size_after);\n#endif",
)

# The commit attempt owns no logical cursor publication. The public allocation
# path remains fatal, but tests can now prove that a failed native commit leaves
# both high-water marks unchanged and that the next allocation is usable.
replace_between(
    "src/buster/lib/arena.c",
    "#if BUSTER_INCLUDE_TESTS\nBUSTER_GLOBAL_LOCAL bool arena_fail_next_commit;",
    "BUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit",
    "BUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit",
)
# The previous replacement retains the declaration marker once; remove the
# duplicate introduced by using it as the section replacement text.
replace_once(
    "src/buster/lib/arena.c",
    "BUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit\n\nBUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit",
    "BUSTER_GLOBAL_LOCAL u64 arena_os_position_after_commit",
)

replace_function(
    "src/buster/lib/arena.c",
    "void arena_allocate_commit(Arena* arena, u64 aligned_size_after)",
    r'''BUSTER_GLOBAL_LOCAL bool arena_allocate_commit_attempt(Arena* arena, u64 aligned_size_after)
{
    BUSTER_VALIDATE(aligned_size_after > arena->os_position && aligned_size_after <= arena->reserved_size);
    u64 os_position = arena->os_position;
    u64 target_committed_size = aligned_size_after;
    u64 remainder = target_committed_size & (arena->granularity - 1);
    if (remainder)
    {
        u64 increment = arena->granularity - remainder;
        u64 remaining = arena->reserved_size - target_committed_size;
        target_committed_size = increment <= remaining ? target_committed_size + increment : arena->reserved_size;
    }
    u64 size_to_commit = target_committed_size - os_position;
    u8* commit_pointer = (u8*)arena + os_position;
    bool result = os_commit(commit_pointer, size_to_commit,
                            (ProtectionFlags){.read = 1, .write = 1, .execute = arena->flags.execute}, arena->flags.prefault_pages);
    if (result)
    {
        arena->os_position = arena_os_position_after_commit(target_committed_size, arena->reserved_size);
    }
    return result;
}

void arena_allocate_commit(Arena* arena, u64 aligned_size_after)
{
    if (!arena_allocate_commit_attempt(arena, aligned_size_after))
    {
        os_fail_message(S8("arena commit failed"));
    }
}

#if BUSTER_INCLUDE_TESTS
bool arena_test_allocate_commit_attempt(Arena* arena, u64 aligned_size_after)
{
    bool result = arena_allocate_commit_attempt(arena, aligned_size_after);
    return result;
}
#endif''',
)
replace_once(
    "src/buster/lib/arena.c",
    "BUSTER_CHECK_RAW(arena_destroy_extended(pooled, 1, reserved_size));",
    "BUSTER_ENSURE_RAW(arena_destroy_extended(pooled, 1, reserved_size));",
)

# Install the OS failure script beside the existing prefault script.
replace_once(
    "src/buster/lib/os.c",
    "#if BUSTER_INCLUDE_TESTS\nBUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool os_prefault_test_forced;",
    r'''#if BUSTER_INCLUDE_TESTS
typedef struct OsResourceTestState OsResourceTestState;
struct OsResourceTestState
{
    OsResourceTestOperation operation;
    u64 remaining;
    bool armed;
    u8 reserved[7];
};

BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL OsResourceTestState os_resource_test_state;
BUSTER_GLOBAL_LOCAL BUSTER_THREAD_LOCAL_DECL bool os_prefault_test_forced;''',
)
replace_once(
    "src/buster/lib/os.c",
    "void os_prefault_test_force_next(OsPrefaultResult result)\n{",
    r'''void os_resource_test_fail_after(OsResourceTestOperation operation, u64 successful_operations_before_failure)
{
    BUSTER_ENSURE((u64)operation < (u64)OS_RESOURCE_TEST_OPERATION_COUNT);
    os_resource_test_state = (OsResourceTestState){
        .operation = operation,
        .remaining = successful_operations_before_failure,
        .armed = true,
    };
}

void os_resource_test_clear(void)
{
    os_resource_test_state = (OsResourceTestState){0};
}

BUSTER_GLOBAL_LOCAL bool os_resource_test_should_fail(OsResourceTestOperation operation)
{
    bool result = false;
    if (os_resource_test_state.armed && os_resource_test_state.operation == operation)
    {
        if (os_resource_test_state.remaining)
        {
            os_resource_test_state.remaining -= 1;
        }
        else
        {
            os_resource_test_state.armed = false;
            result = true;
        }
    }
    return result;
}

void os_prefault_test_force_next(OsPrefaultResult result)
{''',
)

replace_function(
    "src/buster/lib/os.c",
    "bool os_commit(void* address, u64 size, ProtectionFlags protection, bool prefault)",
    r'''bool os_commit(void* address, u64 size, ProtectionFlags protection, bool prefault)
{
    bool result = true;
#if BUSTER_INCLUDE_TESTS
    result = !os_resource_test_should_fail(OS_RESOURCE_TEST_COMMIT);
#endif
    if (result)
    {
#if defined(__linux__) || defined(__APPLE__)
        int protection_flags = os_posix_protection_flags(protection);
        int os_result = mprotect(address, size, protection_flags);
        result = os_result == 0;
#elif defined(_WIN32)
        DWORD protection_flags = os_windows_protection_flags(protection);
        void* os_result = VirtualAlloc(address, size, MEM_COMMIT, protection_flags);
        result = os_result != 0;
#endif
    }

    // Strictly subordinate and strictly advisory: the request is issued only
    // once the commit itself succeeded, and its outcome is deliberately kept
    // out of `result`. A caller that needs the outcome asks os_prefault.
    if (result & prefault)
    {
        (void)os_prefault(address, size);
    }

#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_COMMIT, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, result);
#endif
    return result;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)",
    r'''void* os_reserve(void* base, u64 size, ProtectionFlags protection, MapFlags map)
{
    void* address = 0;
    bool attempt = true;
#if BUSTER_INCLUDE_TESTS
    attempt = !os_resource_test_should_fail(OS_RESOURCE_TEST_RESERVE);
#endif
    if (attempt)
    {
#if defined(__linux__) || defined(__APPLE__)
        // An SDK without MAP_JIT cannot honour a JIT reservation, so the mapping is
        // skipped rather than made without the flag the caller asked for. Only the
        // `if` is conditional; the block below is not, so the braces balance in
        // every configuration.
#if defined(__APPLE__) && !defined(MAP_JIT)
        if (!map.jit)
#endif
        {
            int protection_flags = os_posix_protection_flags(protection);
            int map_flags = os_posix_map_flags(map);

            address = mmap(base, size, protection_flags, map_flags, -1, 0);
            if (address == MAP_FAILED)
            {
                address = 0;
            }
        }
#elif defined(_WIN32)
        DWORD allocation_flags = os_windows_allocation_flags(map);
        DWORD protection_flags = os_windows_protection_flags(protection);
        address = VirtualAlloc(base, size, allocation_flags, protection_flags);
#endif
    }
#if BUSTER_BENCH_ALLOCATIONS
    arena_benchmark_event(ARENA_BENCHMARK_OS_RESERVE, S8(__FILE__), S8(__func__), __LINE__, size, 0, 0, 0, address != 0);
#endif
    return address;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "OsThreadHandle* os_thread_create(ThreadCreateOptions options)",
    r'''OsThreadHandle* os_thread_create(ThreadCreateOptions options)
{
    BUSTER_UNUSED(options);
    OsEntity* result = 0;
    bool attempt = true;
#if BUSTER_INCLUDE_TESTS
    attempt = !os_resource_test_should_fail(OS_RESOURCE_TEST_THREAD_CREATE);
#endif
    if (attempt)
    {
        result = os_entity_allocate(OS_ENTITY_KIND_THREAD);
        result->thread.callback = options.callback;
        result->thread.argument = options.argument;
        // Counted before the thread exists rather than from inside it, so no
        // window has the new thread running while the process still looks serial.
        atomic_u64_increment(&os_live_thread_count);
#if defined(__linux__) || defined(__APPLE__)
        int create_result = pthread_create(&result->thread.handle, 0, &pthread_entry_point, result);
        bool os_result = create_result == 0;
        if (!os_result)
        {
            atomic_u64_decrement(&os_live_thread_count);
            os_entity_release(result);
            result = 0;
        }
#elif defined(_WIN32)
        HANDLE handle = CreateThread(0, 0, &windows_thread_entry_point, result, 0, 0);
        if (handle)
        {
            result->thread.handle = handle;
        }
        else
        {
            atomic_u64_decrement(&os_live_thread_count);
            os_entity_release(result);
            result = 0;
        }
#endif
    }
    return (OsThreadHandle*)result;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "bool os_thread_join(OsThreadHandle* handle)",
    r'''bool os_thread_join(OsThreadHandle* handle)
{
    OsEntity* entity = (OsEntity*)handle;
    BUSTER_CHECK(entity != 0);

    bool result;
#if defined(__linux__) || defined(__APPLE__)
    void* void_return_value = 0;
    int join_result = pthread_join(entity->thread.handle, &void_return_value);
    result = join_result == 0;
#elif defined(_WIN32)
    DWORD wait_result = WaitForSingleObject(entity->thread.handle, INFINITE);
    result = wait_result == WAIT_OBJECT_0;
    if (result)
    {
        result = CloseHandle(entity->thread.handle) != 0;
    }
#endif
    if (result)
    {
        os_entity_release(entity);
#if BUSTER_INCLUDE_TESTS
        if (os_resource_test_should_fail(OS_RESOURCE_TEST_THREAD_JOIN))
        {
            // Native ownership is already consumed. Only the reported result
            // is faulted, so a test cannot manufacture a leaked live thread.
            result = false;
        }
#endif
    }
    return result;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "OsBarrierHandle* os_barrier_create(u32 thread_count)",
    r'''OsBarrierHandle* os_barrier_create(u32 thread_count)
{
    BUSTER_CHECK(thread_count >= 1);
    OsEntity* result = 0;
    bool attempt = true;
#if BUSTER_INCLUDE_TESTS
    attempt = !os_resource_test_should_fail(OS_RESOURCE_TEST_BARRIER_CREATE);
#endif
    if (attempt)
    {
        result = os_entity_allocate(OS_ENTITY_KIND_BARRIER);
        result->barrier.threshold = thread_count;
        result->barrier.arrived = 0;
        result->barrier.generation = 0;
#if defined(__linux__) || defined(__APPLE__)
        // The condition variable is only attempted once the mutex exists, and it
        // unwinds the mutex when it fails; both failures leave `result` null.
        if (pthread_mutex_init(&result->barrier.mutex, 0) != 0)
        {
            os_entity_release(result);
            result = 0;
        }
        else if (pthread_cond_init(&result->barrier.condition, 0) != 0)
        {
            pthread_mutex_destroy(&result->barrier.mutex);
            os_entity_release(result);
            result = 0;
        }
#elif defined(_WIN32)
        InitializeCriticalSection(&result->barrier.mutex);
        InitializeConditionVariable(&result->barrier.condition);
#endif
    }
    return (OsBarrierHandle*)result;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "ThreadContext* thread_context_allocate(void)",
    r'''BUSTER_GLOBAL_LOCAL ThreadContext* thread_context_allocate_attempt(void)
{
    Arena* arenas[(u64)SCRATCH_ARENA_COUNT] = {0};
    u64 arena_count = 0;
    for (u64 i = 0; i < SCRATCH_ARENA_COUNT; i += 1)
    {
        Arena* arena = arena_create((ArenaCreation){0});
        if (!arena)
        {
            break;
        }
        arenas[i] = arena;
        arena_count += 1;
    }

    ThreadContext* result = 0;
    if (arena_count == SCRATCH_ARENA_COUNT)
    {
        result = arena_allocate(arenas[0], ThreadContext, 1);
        memset(result, 0, sizeof(*result));
        memcpy(result->arenas, arenas, sizeof(arenas));
        result->lane_context.lane_count = 1;
    }
    else
    {
        bool destroyed = true;
        for (u64 i = arena_count; i > 0; i -= 1)
        {
            bool current = arena_destroy(arenas[i - 1], 1);
            destroyed = current && destroyed;
        }
        BUSTER_ENSURE_RAW(destroyed);
    }
    return result;
}

ThreadContext* thread_context_allocate(void)
{
    ThreadContext* result = thread_context_allocate_attempt();
    if (!result)
    {
        os_fail_message_raw(S8("thread context allocation failed"));
    }
    return result;
}''',
)

replace_function(
    "src/buster/lib/os.c",
    "void thread_context_release(ThreadContext* thread_context)",
    r'''void thread_context_release(ThreadContext* thread_context)
{
    if (thread_context)
    {
#if !BUSTER_SINGLE_THREADED
        // Resident lane workers own OS entities and their own thread contexts.
        // Join them while the entity mutex and arenas are still alive.
        lane_gang_release(thread_context);
#endif
        ThreadContext* selected = thread_context_selected();
        if (selected == thread_context)
        {
            thread_context_select(0);
            // The context owns the scratch arenas that contain it. TLS must stop
            // referring to that storage before any arena is unmapped. Reported
            // without scratch: there is deliberately no selected context here.
            BUSTER_CHECK_RAW(thread_context_selected() == 0);
        }
        Arena* arenas[BUSTER_ARRAY_LENGTH(thread_context->arenas)];
        memcpy(arenas, thread_context->arenas, sizeof(arenas));
        bool destroyed = true;
        for (u64 i = BUSTER_ARRAY_LENGTH(arenas); i > 0; i -= 1)
        {
            bool current = arena_destroy(arenas[i - 1], 1);
            destroyed = current && destroyed;
        }
        BUSTER_ENSURE_RAW(destroyed);
    }
}''',
)

# Replace the complete threaded lane implementation. A startup mutex gates
# workers until every acquisition succeeds, so a partial gang can release the
# gate, join the created prefix, and destroy its resources without ever touching
# a barrier whose threshold includes missing workers.
replace_between(
    "src/buster/lib/os.c",
    "#if !BUSTER_SINGLE_THREADED\ntypedef struct LanePersistentWorkerStart",
    "#endif\n\nvoid lane_run(u64 lane_count_requested",
    r'''#if !BUSTER_SINGLE_THREADED
typedef struct LanePersistentWorkerStart LanePersistentWorkerStart;
struct LanePersistentWorkerStart
{
    LaneGang* gang;
    u64 lane_index;
};

struct LaneGang
{
    OsBarrierHandle* dispatch_barrier;
    OsBarrierHandle* active_barrier;
    OsMutexHandle* startup_mutex;
    LanePersistentWorkerStart* starts;
    OsThreadHandle** handles;
    ThreadCallback* callback;
    void* argument;
    u64 broadcast_memory;
    u64 capacity;
    u64 worker_count;
    u64 active_count;
    bool startup_ready;
    bool shutdown;
    bool running;
    u8 reserved[5];
};

BUSTER_GLOBAL_LOCAL ThreadReturnType lane_persistent_worker_entry_point(void* argument)
{
    LanePersistentWorkerStart* start = (LanePersistentWorkerStart*)argument;
    LaneGang* gang = start->gang;
    ThreadContext* thread_context = thread_context_selected();
    os_mutex_lock(gang->startup_mutex);
    bool startup_ready = gang->startup_ready;
    os_mutex_unlock(gang->startup_mutex);
    if (startup_ready)
    {
        os_thread_set_name(S8("lane_worker"));
        for (;;)
        {
            os_barrier_wait(gang->dispatch_barrier);
            if (gang->shutdown)
            {
                break;
            }
            if (start->lane_index < gang->active_count)
            {
                LaneContext saved_lane = thread_context->lane_context;
                u64 saved_arena_positions[SCRATCH_ARENA_COUNT];
                for (u64 arena_index = 0; arena_index < SCRATCH_ARENA_COUNT; arena_index += 1)
                {
                    saved_arena_positions[arena_index] = thread_context->arenas[arena_index]->position;
                }
                thread_context->lane_context = (LaneContext){
                    .lane_index = start->lane_index,
                    .lane_count = gang->active_count,
                    .barrier = gang->active_barrier,
                    .broadcast_memory = &gang->broadcast_memory,
                };
                gang->callback(gang->argument);
                thread_context->lane_context = saved_lane;
                // A fresh worker context used to disappear after every lane_run.
                // Reset resident worker scratch so callbacks retain that lifetime
                // contract and cannot accumulate allocation across dispatches. A
                // large high-water mark is unmapped as well; otherwise a long-lived
                // IDE would retain one peak scratch commitment per resident lane.
                for (u64 arena_index = 0; arena_index < SCRATCH_ARENA_COUNT; arena_index += 1)
                {
                    Arena* scratch = thread_context->arenas[arena_index];
                    if (scratch->os_position - saved_arena_positions[arena_index] > BUSTER_MB(16))
                    {
                        // arenas[0] owns ThreadContext itself, so preserve its
                        // stable prefix and decommit only the unused tail.
                        if (!arena_set_position_and_decommit(scratch, saved_arena_positions[arena_index]))
                        {
                            os_fail_message(S8("lane scratch decommit failed"));
                        }
                    }
                    else
                    {
                        arena_set_position(scratch, saved_arena_positions[arena_index]);
                    }
                }
            }
            os_barrier_wait(gang->dispatch_barrier);
        }
    }
}

BUSTER_GLOBAL_LOCAL bool lane_gang_join_workers(LaneGang* gang)
{
    bool result = true;
    for (u64 lane = 1; lane <= gang->worker_count; lane += 1)
    {
        bool joined = os_thread_join(gang->handles[lane]);
        result = joined && result;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL LaneGang* lane_gang_create(Arena* arena, u64 count)
{
    BUSTER_CHECK(arena != 0);
    BUSTER_CHECK(count > 1 && count <= UINT32_MAX);
    LaneGang* result = arena_allocate(arena, LaneGang, 1);
    memset(result, 0, sizeof(*result));
    result->capacity = count;
    result->starts = arena_allocate(arena, LanePersistentWorkerStart, count);
    result->handles = arena_allocate(arena, OsThreadHandle*, count);
    memset(result->handles, 0, sizeof(*result->handles) * count);
    result->startup_mutex = os_mutex_create();
    bool constructed = false;
    if (result->startup_mutex)
    {
        os_mutex_lock(result->startup_mutex);
        result->dispatch_barrier = os_barrier_create((u32)count);
        if (result->dispatch_barrier)
        {
            for (u64 lane = 1; lane < count; lane += 1)
            {
                result->starts[lane] = (LanePersistentWorkerStart){
                    .gang = result,
                    .lane_index = lane,
                };
                result->handles[lane] = os_thread_create((ThreadCreateOptions){
                    .callback = &lane_persistent_worker_entry_point,
                    .argument = &result->starts[lane],
                });
                if (!result->handles[lane])
                {
                    break;
                }
                result->worker_count += 1;
            }
            constructed = result->worker_count == count - 1;
        }
        result->startup_ready = constructed;
        os_mutex_unlock(result->startup_mutex);
        if (!constructed)
        {
            bool joined = lane_gang_join_workers(result);
            if (!joined)
            {
                os_fail_message(S8("lane worker join failed"));
            }
            if (result->dispatch_barrier)
            {
                os_barrier_destroy(result->dispatch_barrier);
            }
            os_mutex_destroy(result->startup_mutex);
            result = 0;
        }
    }
    else
    {
        result = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool lane_gang_destroy(LaneGang* gang)
{
    bool result = true;
    if (gang)
    {
        BUSTER_CHECK(!gang->running);
        BUSTER_CHECK(gang->worker_count == gang->capacity - 1);
        gang->shutdown = true;
        os_barrier_wait(gang->dispatch_barrier);
        result = lane_gang_join_workers(gang);
        if (result)
        {
            if (gang->active_barrier)
            {
                os_barrier_destroy(gang->active_barrier);
            }
            os_barrier_destroy(gang->dispatch_barrier);
            os_mutex_destroy(gang->startup_mutex);
        }
    }
    return result;
}

void lane_gang_release(ThreadContext* thread_context)
{
    if (thread_context)
    {
        BUSTER_CHECK((thread_context->lane_gang == 0) == (thread_context->lane_arena == 0));
        if (thread_context->lane_gang)
        {
            if (!lane_gang_destroy(thread_context->lane_gang))
            {
                os_fail_message(S8("lane worker join failed"));
            }
            thread_context->lane_gang = 0;
            Arena* lane_arena = thread_context->lane_arena;
            thread_context->lane_arena = 0;
            BUSTER_ENSURE(arena_destroy(lane_arena, 1));
        }
    }
}

BUSTER_GLOBAL_LOCAL bool lane_gang_install(ThreadContext* thread_context, u64 count)
{
    BUSTER_CHECK(thread_context != 0);
    BUSTER_CHECK(thread_context->lane_gang == 0 && thread_context->lane_arena == 0);
    Arena* arena = arena_create((ArenaCreation){0});
    LaneGang* gang = arena ? lane_gang_create(arena, count) : 0;
    bool result = gang != 0;
    if (result)
    {
        thread_context->lane_arena = arena;
        thread_context->lane_gang = gang;
    }
    else if (arena)
    {
        BUSTER_ENSURE(arena_destroy(arena, 1));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool lane_gang_dispatch(ThreadContext* thread_context, LaneGang* gang, u64 count, ThreadCallback* callback, void* argument)
{
    BUSTER_CHECK(thread_context != 0 && gang != 0 && !gang->running);
    BUSTER_CHECK(count > 1 && count <= gang->capacity);
    bool result = true;
    if (!gang->active_barrier || gang->active_count != count)
    {
        OsBarrierHandle* replacement = os_barrier_create((u32)count);
        if (replacement)
        {
            OsBarrierHandle* previous = gang->active_barrier;
            gang->active_barrier = replacement;
            gang->active_count = count;
            if (previous)
            {
                os_barrier_destroy(previous);
            }
        }
        else
        {
            result = false;
        }
    }
    if (result)
    {
        gang->active_count = count;
        gang->broadcast_memory = 0;
        gang->callback = callback;
        gang->argument = argument;
        gang->running = true;

        LaneContext saved = thread_context->lane_context;
        thread_context->lane_context = (LaneContext){
            .lane_index = 0,
            .lane_count = count,
            .barrier = gang->active_barrier,
            .broadcast_memory = &gang->broadcast_memory,
        };
        // The first generation publishes this dispatch; the second replaces the
        // old per-call joins as its completion and visibility edge.
        os_barrier_wait(gang->dispatch_barrier);
        callback(argument);
        os_barrier_wait(gang->dispatch_barrier);
        thread_context->lane_context = saved;

        gang->running = false;
        gang->callback = 0;
        gang->argument = 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void lane_run_fresh(ThreadContext* thread_context, u64 count, ThreadCallback* callback, void* argument)
{
    Arena* arena = arena_create((ArenaCreation){0});
    LaneGang* gang = arena ? lane_gang_create(arena, count) : 0;
    if (!gang)
    {
        if (arena)
        {
            BUSTER_ENSURE(arena_destroy(arena, 1));
        }
        os_fail_message(S8("lane gang creation failed"));
    }
    if (!lane_gang_dispatch(thread_context, gang, count, callback, argument))
    {
        if (!lane_gang_destroy(gang))
        {
            os_fail_message(S8("lane worker join failed"));
        }
        BUSTER_ENSURE(arena_destroy(arena, 1));
        os_fail_message(S8("lane barrier creation failed"));
    }
    if (!lane_gang_destroy(gang))
    {
        os_fail_message(S8("lane worker join failed"));
    }
    BUSTER_ENSURE(arena_destroy(arena, 1));
}
#endif''',
)

replace_function(
    "src/buster/lib/os.c",
    "void lane_run(u64 lane_count_requested, ThreadCallback* callback, void* argument)",
    r'''void lane_run(u64 lane_count_requested, ThreadCallback* callback, void* argument)
{
    ThreadContext* thread_context = thread_context_selected();
    BUSTER_CHECK(thread_context != 0);
    BUSTER_CHECK(callback != 0);

#if BUSTER_SINGLE_THREADED
    BUSTER_UNUSED(lane_count_requested);
    u64 count = 1;
#else
    u64 count = lane_count_requested ? lane_count_requested : os_get_logical_thread_count();
    BUSTER_CHECK(count <= UINT32_MAX);
#endif

    LaneContext saved = thread_context->lane_context;
    // One chain over the three ways to run: inline, a fresh short-lived gang, or
    // the resident one. Only the second and third exist when threading is on,
    // so the `else` arms live inside the same #if and the braces balance in
    // either configuration.
    if (count == 1)
    {
        thread_context->lane_context = (LaneContext){
            .lane_count = 1,
        };
        callback(argument);
        thread_context->lane_context = saved;
    }
#if !BUSTER_SINGLE_THREADED
    // A lane inside an active region gets an independent short-lived gang.
    // Re-entering the resident dispatch barriers would deadlock the outer
    // region and would break the documented nested-gang behavior.
    else if (saved.lane_count > 1 || (thread_context->lane_gang && thread_context->lane_gang->running))
    {
        lane_run_fresh(thread_context, count, callback, argument);
    }
    else
    {
        if (!thread_context->lane_gang || thread_context->lane_gang->capacity < count)
        {
            lane_gang_release(thread_context);
            if (!lane_gang_install(thread_context, count))
            {
                os_fail_message(S8("lane gang creation failed"));
            }
        }
        if (!lane_gang_dispatch(thread_context, thread_context->lane_gang, count, callback, argument))
        {
            lane_gang_release(thread_context);
            os_fail_message(S8("lane barrier creation failed"));
        }
    }
#endif
}''',
)

# Arena fault tests: reservation and initial commit are explicit failures; a
# growth commit can be attempted non-fatally to assert both cursors remain
# unchanged before a successful retry.
replace_once(
    "src/buster/tests/arena_test.c",
    "            arena_test_fail_next_commit();",
    "            os_resource_test_fail_after(OS_RESOURCE_TEST_COMMIT, 0);",
)
replace_once(
    "src/buster/tests/arena_test.c",
    "#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS\n    String8 failure_mode",
    r'''#if BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS
    {
        ArenaCreation creation = {
            .reserved_size = BUSTER_MB(1),
            .initial_size = BUSTER_KB(64),
            .flags = {.no_pool = 1},
        };
        os_resource_test_fail_after(OS_RESOURCE_TEST_RESERVE, 0);
        Arena* failed_reservation = arena_create(creation);
        os_resource_test_clear();
        BUSTER_TEST(arguments, failed_reservation == 0);

        os_resource_test_fail_after(OS_RESOURCE_TEST_COMMIT, 0);
        Arena* failed_initial_commit = arena_create(creation);
        os_resource_test_clear();
        BUSTER_TEST(arguments, failed_initial_commit == 0);

        Arena* arena = arena_create(creation);
        BUSTER_TEST(arguments, arena != 0);
        if (arena)
        {
            u64 position_before = arena->position;
            u64 os_position_before = arena->os_position;
            u64 requested_end = os_position_before + 1;
            os_resource_test_fail_after(OS_RESOURCE_TEST_COMMIT, 0);
            bool committed = arena_test_allocate_commit_attempt(arena, requested_end);
            os_resource_test_clear();
            BUSTER_TEST(arguments, !committed);
            BUSTER_TEST(arguments, arena->position == position_before);
            BUSTER_TEST(arguments, arena->os_position == os_position_before);

            u64 allocation_size = requested_end - position_before;
            void* allocation = arena_allocate_bytes(arena, allocation_size, 1);
            BUSTER_TEST(arguments, allocation != 0);
            BUSTER_TEST(arguments, arena->position == requested_end);
            BUSTER_TEST(arguments, arena->os_position >= requested_end);
            BUSTER_TEST(arguments, arena_destroy(arena, 1));
        }
    }

    String8 failure_mode''',
)

# A no-op lane body lets resource failures exercise only ownership and cleanup.
replace_once(
    "src/buster/tests/os_test.c",
    "#endif\n\ntypedef struct OsTestLaneState",
    r'''BUSTER_GLOBAL_LOCAL ThreadReturnType os_test_resource_lane(void* argument)
{
    BUSTER_UNUSED(argument);
}
#endif

typedef struct OsTestLaneState''',
)

# Child-side deterministic failure modes. Each fatal diagnostic is reached only
# after the relevant constructor or destructor has unwound what it owns.
replace_once(
    "src/buster/tests/os_test.c",
    "#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS\n    String8 fatal_mode",
    r'''#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS
#if !BUSTER_SINGLE_THREADED
    String8 resource_failure_mode = os_get_environment_variable(S8("BUSTER_OS_RESOURCE_FAILURE_MODE"));
    if (resource_failure_mode.length)
    {
        ThreadContext* resource_context = thread_context_allocate();
        thread_context_select(resource_context);
        if (string_equal(resource_failure_mode, S8("barrier_create")))
        {
            os_resource_test_fail_after(OS_RESOURCE_TEST_BARRIER_CREATE, 0);
            lane_run(2, &os_test_resource_lane, 0);
        }
        else if (string_equal(resource_failure_mode, S8("thread_create_partial")))
        {
            os_resource_test_fail_after(OS_RESOURCE_TEST_THREAD_CREATE, 1);
            lane_run(3, &os_test_resource_lane, 0);
        }
        else if (string_equal(resource_failure_mode, S8("active_barrier")))
        {
            os_resource_test_fail_after(OS_RESOURCE_TEST_BARRIER_CREATE, 1);
            lane_run(2, &os_test_resource_lane, 0);
        }
        else if (string_equal(resource_failure_mode, S8("join")))
        {
            lane_run(2, &os_test_resource_lane, 0);
            os_resource_test_fail_after(OS_RESOURCE_TEST_THREAD_JOIN, 0);
            thread_context_release(resource_context);
        }
        os_exit(97);
    }
#endif
    String8 fatal_mode''',
)

# Parent-side deadline and diagnostic checks. Existing fatal-output children
# exit before this block; arena-failure children are skipped explicitly so the
# subprocess fixtures never recurse into one another.
replace_once(
    "src/buster/tests/os_test.c",
    "#endif\n\n#if !BUSTER_ANDROID && !BUSTER_IOS\n    // Recoverable file IO",
    r'''#endif

#if (BUSTER_LINUX || BUSTER_MACOS || BUSTER_WINDOWS) && !BUSTER_ANDROID && !BUSTER_IOS && !BUSTER_SINGLE_THREADED
    if (!os_get_environment_variable(S8("BUSTER_ARENA_FAILURE_MODE")).length &&
        !os_get_environment_variable(S8("BUSTER_OS_FATAL_OUTPUT_MODE")).length)
    {
        String8 modes[] = {
            S8("barrier_create"),
            S8("thread_create_partial"),
            S8("active_barrier"),
            S8("join"),
        };
        String8 diagnostics[] = {
            S8("lane gang creation failed"),
            S8("lane gang creation failed"),
            S8("lane barrier creation failed"),
            S8("lane worker join failed"),
        };
        BUSTER_CT_CHECK(BUSTER_ARRAY_LENGTH(modes) == BUSTER_ARRAY_LENGTH(diagnostics));
        String8 child_arguments[] = {program_state->input.arguments.pointer[0], S8("test")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            SliceString8 inherited_keys = program_state->input.environment_keys;
            SliceString8 inherited_values = program_state->input.environment_values;
            String8* keys = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            String8* values = arena_allocate(arguments->arena, String8, inherited_keys.length + 2);
            keys[0] = S8("BUSTER_OS_RESOURCE_FAILURE_MODE");
            values[0] = modes[index];
            keys[1] = S8("BUSTER_TEST_JOBS");
            values[1] = S8("1");
            u64 count = 2;
            for (u64 inherited = 0; inherited < inherited_keys.length; inherited += 1)
            {
                if (!string_equal(inherited_keys.pointer[inherited], keys[0]) &&
                    !string_equal(inherited_keys.pointer[inherited], keys[1]))
                {
                    keys[count] = inherited_keys.pointer[inherited];
                    values[count] = inherited_values.pointer[inherited];
                    count += 1;
                }
            }
            ProcessSpawnResult spawn = os_process_spawn((SliceString8)BUSTER_ARRAY_TO_SLICE(child_arguments),
                (SliceString8){keys, count}, (SliceString8){values, count},
                (ProcessSpawnOptions){.capture = (u64)1 << STANDARD_STREAM_ERROR});
            BUSTER_TEST(arguments, spawn.handle != 0);
            if (spawn.handle)
            {
                ProcessWaitResult wait = os_process_wait_deadline(arguments->arena, spawn, 30000000);
                BUSTER_TEST(arguments, !wait.timed_out);
                BUSTER_TEST(arguments, wait.result == PROCESS_RESULT_FAILED);
                String8 error = {(char8*)wait.streams[STANDARD_STREAM_ERROR].pointer,
                                 wait.streams[STANDARD_STREAM_ERROR].length};
                BUSTER_TEST(arguments, string_first_sequence(error, diagnostics[index]) != BUSTER_STRING_NO_MATCH);
            }
        }
    }
#endif

#if !BUSTER_ANDROID && !BUSTER_IOS
    // Recoverable file IO''',
)

# Classify the remaining directly-audited fallible cleanup sites as always-on
# runtime requirements instead of optimizer assumptions.
for path in (
    "src/buster/tests/os_test.c",
    "src/buster/tests/test.c",
    "src/buster/tests/compiler/driver/object_path_test.c",
    "tools/differential.c",
):
    text = read(path)
    text = text.replace("BUSTER_CHECK(arena_destroy(", "BUSTER_ENSURE(arena_destroy(")
    text = text.replace("BUSTER_CHECK(os_file_close(", "BUSTER_ENSURE(os_file_close(")
    write(path, text)

replace_once(
    "src/buster/tests/os_test.c",
    "BUSTER_CHECK(pooled != 0);",
    "BUSTER_ENSURE(pooled != 0);",
)

print("issue #63 source patch applied")
