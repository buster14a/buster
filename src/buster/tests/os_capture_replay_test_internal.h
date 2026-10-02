#pragma once
// Included by os_test.c: ownership/readiness checks and bounded native trace
// enumeration. READ_FAILED/CLOSE_FAILED are synthetic adapter controls:
// they do not establish naturally possible failures of owned blocking pipes.
// Traces contain numeric events/counts, never output or paths.
#include <buster/lib/os_internal.h>

enum
{
    OS_CAPTURE_TEST_TRACE_LIMIT = 8,
    OS_CAPTURE_TEST_ENUM_DEPTH = 6,
    OS_CAPTURE_TEST_WORK_LIMIT = 1 + (OS_PROCESS_CAPTURE_EVENT_COUNT + 1) * OS_CAPTURE_TEST_ENUM_DEPTH,
    CAPTURE_ADMISSION_ERROR = 1,
    CAPTURE_MUTATION_ERROR = 2,
    CAPTURE_STATE_ERROR = 4,
    CAPTURE_STATUS_ERROR = 8,
};

typedef struct CaptureStep CaptureStep;
struct CaptureStep
{
    OsProcessCaptureEvent event;
    u64 bytes;
};

typedef struct CaptureTrace CaptureTrace;
struct CaptureTrace
{
    CaptureStep steps[OS_CAPTURE_TEST_TRACE_LIMIT];
    u32 count;
};

typedef struct CaptureReplay CaptureReplay;
struct CaptureReplay
{
    OsProcessCaptureState state;
    u32 violations;
    bool admitted;
    bool invariants;
};

BUSTER_GLOBAL_LOCAL bool os_capture_state_equal(OsProcessCaptureState first, OsProcessCaptureState second)
{
    bool result = first.phase == second.phase && first.observed_bytes == second.observed_bytes &&
        first.close_attempts == second.close_attempts && first.failed == second.failed && first.eof == second.eof &&
        first.close_outcome_unknown == second.close_outcome_unknown;
    return result;
}

// Independent admission: readiness permits one read, EOF/failure stops reads,
// and a close attempt consumes descriptor authority.
BUSTER_GLOBAL_LOCAL CaptureReplay os_capture_replay(const CaptureTrace* trace)
{
    CaptureReplay result = {.admitted = trace->count <= OS_CAPTURE_TEST_TRACE_LIMIT, .invariants = true};
    bool authority = true;
    bool read_ready = false;
    bool closing = false;
    bool failed = false;
    bool eof = false;
    bool unknown = false;
    u64 bytes = 0;
    u32 closes = 0;
    for (u32 index = 0; index < trace->count && result.admitted; index += 1)
    {
        CaptureStep step = trace->steps[index];
        bool waiting_event = step.event == OS_PROCESS_CAPTURE_WAIT_READY || step.event == OS_PROCESS_CAPTURE_WAIT_IDLE ||
            step.event == OS_PROCESS_CAPTURE_WAIT_INTERRUPTED || step.event == OS_PROCESS_CAPTURE_WAIT_FAILED;
        bool read_event = step.event == OS_PROCESS_CAPTURE_READ_BYTES || step.event == OS_PROCESS_CAPTURE_READ_INTERRUPTED ||
            step.event == OS_PROCESS_CAPTURE_READ_EOF || step.event == OS_PROCESS_CAPTURE_READ_FAILED;
        bool close_event = step.event == OS_PROCESS_CAPTURE_CLOSE_OK || step.event == OS_PROCESS_CAPTURE_CLOSE_FAILED;
        bool progress_valid = step.event == OS_PROCESS_CAPTURE_READ_BYTES
            ? step.bytes && step.bytes <= 16384 && step.bytes <= UINT64_MAX - bytes : !step.bytes;
        bool legal = authority && progress_valid &&
            ((waiting_event && !read_ready && !closing) || (read_event && read_ready && !closing) ||
             (step.event == OS_PROCESS_CAPTURE_STOP && !closing) || (close_event && closing));
        OsProcessCaptureState before = result.state;
        bool accepted = os_process_capture_step(&result.state, step.event, step.bytes);
        if (accepted != legal) { result.violations |= CAPTURE_ADMISSION_ERROR; }
        if (!legal)
        {
            if (!os_capture_state_equal(before, result.state)) { result.violations |= CAPTURE_MUTATION_ERROR; }
            result.admitted = false;
        }
        else
        {
            if (step.event == OS_PROCESS_CAPTURE_WAIT_READY) { read_ready = true; }
            if (step.event == OS_PROCESS_CAPTURE_READ_BYTES || step.event == OS_PROCESS_CAPTURE_READ_INTERRUPTED)
            {
                bytes += step.bytes;
                read_ready = false;
            }
            if (step.event == OS_PROCESS_CAPTURE_READ_EOF) { eof = true; }
            if (step.event == OS_PROCESS_CAPTURE_WAIT_FAILED || step.event == OS_PROCESS_CAPTURE_READ_FAILED ||
                step.event == OS_PROCESS_CAPTURE_STOP || step.event == OS_PROCESS_CAPTURE_CLOSE_FAILED) { failed = true; }
            if (step.event == OS_PROCESS_CAPTURE_WAIT_FAILED || step.event == OS_PROCESS_CAPTURE_READ_EOF ||
                step.event == OS_PROCESS_CAPTURE_READ_FAILED || step.event == OS_PROCESS_CAPTURE_STOP) { closing = true; }
            if (close_event)
            {
                closes += 1;
                authority = false;
                unknown = step.event == OS_PROCESS_CAPTURE_CLOSE_FAILED;
            }
            OsProcessCapturePhase phase = !authority ? OS_PROCESS_CAPTURE_CLOSED
                : closing ? OS_PROCESS_CAPTURE_CLOSING : read_ready ? OS_PROCESS_CAPTURE_READY : OS_PROCESS_CAPTURE_WAITING;
            bool state_valid = result.state.phase == phase && result.state.observed_bytes == bytes &&
                result.state.close_attempts == closes && closes <= 1 && result.state.failed == failed &&
                result.state.eof == eof && result.state.close_outcome_unknown == unknown && (!before.failed || result.state.failed);
            if (!state_valid) { result.violations |= CAPTURE_STATE_ERROR; }
            ProcessResult expected = !authority && eof && !failed ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            if (os_process_capture_result(&result.state, PROCESS_RESULT_SUCCESS) != expected ||
                os_process_capture_result(&result.state, PROCESS_RESULT_FAILED) != PROCESS_RESULT_FAILED) { result.violations |= CAPTURE_STATUS_ERROR; }
        }
    }
    result.invariants = !result.violations;
    return result;
}

BUSTER_GLOBAL_LOCAL void os_capture_trace_print(const CaptureTrace* trace)
{
    string_print(S8("capture-replay-v1 count={u32}"), trace->count);
    for (u32 index = 0; index < trace->count && index < OS_CAPTURE_TEST_TRACE_LIMIT; index += 1)
    {
        string_print(S8(" {u32}:{u64}"), (u32)trace->steps[index].event, trace->steps[index].bytes);
    }
    string_print(S8("\n"));
}

typedef enum CaptureFailureClass
{
    OS_CAPTURE_WAIT_FAILURE,
    OS_CAPTURE_READ_PREFIX_FAILURE,
    OS_CAPTURE_INVARIANT_FAILURE,
} CaptureFailureClass;

// Preserve adapter admission and failure class. READ_PREFIX_FAILURE is a
// synthetic ownership-violation observation, not native pipe evidence.
BUSTER_GLOBAL_LOCAL bool os_capture_failure_matches(const CaptureTrace* trace, CaptureFailureClass failure_class)
{
    CaptureReplay replay = os_capture_replay(trace);
    bool has_failure = false;
    OsProcessCaptureEvent expected = failure_class == OS_CAPTURE_WAIT_FAILURE
        ? OS_PROCESS_CAPTURE_WAIT_FAILED : OS_PROCESS_CAPTURE_READ_FAILED;
    for (u32 index = 0; index < trace->count && index < OS_CAPTURE_TEST_TRACE_LIMIT; index += 1)
    {
        has_failure |= trace->steps[index].event == expected;
    }
    bool result = replay.admitted && (failure_class == OS_CAPTURE_INVARIANT_FAILURE ? !replay.invariants :
        replay.invariants && has_failure && replay.state.phase == OS_PROCESS_CAPTURE_CLOSED &&
        replay.state.failed && os_process_capture_result(&replay.state, PROCESS_RESULT_SUCCESS) == PROCESS_RESULT_FAILED &&
        (failure_class != OS_CAPTURE_READ_PREFIX_FAILURE || replay.state.observed_bytes != 0));
    return result;
}

BUSTER_GLOBAL_LOCAL CaptureTrace os_capture_minimize(CaptureTrace trace, CaptureFailureClass failure_class)
{
    u32 violation_class = os_capture_replay(&trace).violations;
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (u32 deleted = 0; deleted < trace.count && !changed; deleted += 1)
        {
            CaptureTrace candidate = {.count = trace.count - 1};
            for (u32 source = 0, destination = 0; source < trace.count; source += 1)
            {
                if (source != deleted) { candidate.steps[destination++] = trace.steps[source]; }
            }
            if (os_capture_failure_matches(&candidate, failure_class) &&
                (failure_class != OS_CAPTURE_INVARIANT_FAILURE || os_capture_replay(&candidate).violations == violation_class))
            {
                trace = candidate;
                changed = true;
            }
        }
    }
    return trace;
}

BUSTER_GLOBAL_LOCAL void os_capture_counterexample_print(CaptureTrace trace)
{
    CaptureReplay replay = os_capture_replay(&trace);
    if (replay.admitted && !replay.invariants)
    {
        trace = os_capture_minimize(trace, OS_CAPTURE_INVARIANT_FAILURE);
    }
    os_capture_trace_print(&trace);
}

BUSTER_GLOBAL_LOCAL UnitTestResult os_test_capture_replay(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    const CaptureTrace seeds[] = {
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_READ_EOF, 0}, {OS_PROCESS_CAPTURE_CLOSE_OK, 0}}, .count = 3},
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_READ_EOF, 0},
                   {OS_PROCESS_CAPTURE_CLOSE_FAILED, 0}}, .count = 3},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(seeds); index += 1)
    {
        CaptureReplay replay = os_capture_replay(seeds + index);
        bool correct = replay.admitted && replay.invariants;
        if (!correct) { os_capture_trace_print(seeds + index); }
        BUSTER_TEST(arguments, correct);
    }
    CaptureReplay completed = os_capture_replay(seeds);
    BUSTER_TEST(arguments, os_process_capture_result(&completed.state, PROCESS_RESULT_SUCCESS) == PROCESS_RESULT_SUCCESS);
    BUSTER_TEST(arguments, os_process_capture_result(&completed.state, PROCESS_RESULT_CRASH) == PROCESS_RESULT_CRASH);

    const CaptureTrace malformed[] = {
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_READ_BYTES, 0}}, .count = 2},
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_READ_BYTES, 16385}}, .count = 2},
        {.steps = {{OS_PROCESS_CAPTURE_STOP, 0}, {OS_PROCESS_CAPTURE_CLOSE_FAILED, 0},
                   {OS_PROCESS_CAPTURE_CLOSE_OK, 0}}, .count = 3},
        {.steps = {{OS_PROCESS_CAPTURE_EVENT_COUNT, 0}}, .count = 1},
        {.count = OS_CAPTURE_TEST_TRACE_LIMIT + 1},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(malformed); index += 1)
    {
        CaptureReplay replay = os_capture_replay(malformed + index);
        bool correct = !replay.admitted && replay.invariants;
        if (!correct) { os_capture_trace_print(malformed + index); }
        BUSTER_TEST(arguments, correct);
    }

    const CaptureTrace padded[] = {
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_IDLE, 0}, {OS_PROCESS_CAPTURE_WAIT_INTERRUPTED, 0},
                   {OS_PROCESS_CAPTURE_WAIT_IDLE, 0}, {OS_PROCESS_CAPTURE_WAIT_FAILED, 0},
                   {OS_PROCESS_CAPTURE_CLOSE_OK, 0}}, .count = 5},
        {.steps = {{OS_PROCESS_CAPTURE_WAIT_IDLE, 0}, {OS_PROCESS_CAPTURE_WAIT_READY, 0},
                   {OS_PROCESS_CAPTURE_READ_BYTES, 7}, {OS_PROCESS_CAPTURE_WAIT_INTERRUPTED, 0},
                   {OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_READ_FAILED, 0},
                   {OS_PROCESS_CAPTURE_CLOSE_OK, 0}}, .count = 7},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(padded); index += 1)
    {
        CaptureFailureClass failure_class = index ? OS_CAPTURE_READ_PREFIX_FAILURE : OS_CAPTURE_WAIT_FAILURE;
        BUSTER_TEST(arguments, os_capture_failure_matches(padded + index, failure_class));
        CaptureTrace minimized = os_capture_minimize(padded[index], failure_class);
        bool correct = minimized.count == (index ? 5u : 2u) && os_capture_failure_matches(&minimized, failure_class);
        if (!correct) { os_capture_trace_print(&minimized); }
        BUSTER_TEST(arguments, correct);
        CaptureReplay replay = os_capture_replay(&minimized);
        BUSTER_TEST(arguments, replay.state.observed_bytes == (index ? 7u : 0u));
    }

    // Native generator excludes synthetic read/close failures. Owned blocking
    // anonymous pipes cannot naturally produce EBADF; XNU pipe_close returns
    // zero. Poll resource failure, idle/EINTR and abandonment remain modeled.
    u64 arena_position = arguments->arena->position;
    CaptureTrace* work = arena_allocate(arguments->arena, CaptureTrace, OS_CAPTURE_TEST_WORK_LIMIT);
    work[0] = (CaptureTrace){0};
    u32 work_count = 1;
    u32 visited = 0;
    bool correct = true;
    while (work_count && correct)
    {
        CaptureTrace trace = work[--work_count];
        CaptureReplay replay = os_capture_replay(&trace);
        correct = replay.admitted && replay.invariants;
        if (!correct) { os_capture_counterexample_print(trace); }
        visited += 1;
        for (u32 event = 0; correct && event < OS_PROCESS_CAPTURE_EVENT_COUNT && trace.count < OS_CAPTURE_TEST_ENUM_DEPTH; event += 1)
        {
            if (event == OS_PROCESS_CAPTURE_READ_FAILED || event == OS_PROCESS_CAPTURE_CLOSE_FAILED) { continue; }
            u32 variants = event == OS_PROCESS_CAPTURE_READ_BYTES ? 2 : 1;
            for (u32 variant = 0; variant < variants && correct; variant += 1)
            {
                CaptureTrace candidate = trace;
                candidate.steps[candidate.count++] = (CaptureStep){
                    .event = (OsProcessCaptureEvent)event,
                    .bytes = event == OS_PROCESS_CAPTURE_READ_BYTES ? (variant ? 16384 : 1) : 0,
                };
                CaptureReplay extension = os_capture_replay(&candidate);
                correct = extension.invariants;
                if (!correct) { os_capture_counterexample_print(candidate); }
                if (extension.admitted && correct)
                {
                    correct = work_count < OS_CAPTURE_TEST_WORK_LIMIT;
                    if (correct) { work[work_count++] = candidate; }
                }
            }
        }
    }
    BUSTER_TEST(arguments, correct && !work_count && visited == 2063);
    arena_set_position(arguments->arena, arena_position);

    // Shared poll failure stops both open streams after interleaved progress.
    OsProcessCaptureState streams[2] = {0};
    const CaptureStep interleaved[] = {
        {OS_PROCESS_CAPTURE_WAIT_READY, 0}, {OS_PROCESS_CAPTURE_WAIT_READY, 0},
        {OS_PROCESS_CAPTURE_READ_BYTES, 3}, {OS_PROCESS_CAPTURE_READ_BYTES, 5},
        {OS_PROCESS_CAPTURE_WAIT_FAILED, 0}, {OS_PROCESS_CAPTURE_WAIT_FAILED, 0},
        {OS_PROCESS_CAPTURE_CLOSE_OK, 0}, {OS_PROCESS_CAPTURE_CLOSE_OK, 0},
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(interleaved); index += 1)
    {
        BUSTER_TEST(arguments, os_process_capture_step(streams + (index & 1), interleaved[index].event, interleaved[index].bytes));
        BUSTER_TEST(arguments, streams[0].observed_bytes + streams[1].observed_bytes == (index < 2 ? 0u : index == 2 ? 3u : 8u));
    }
    BUSTER_TEST(arguments, streams[0].close_attempts == 1 && streams[1].close_attempts == 1);
    ProcessResult folded = os_process_capture_result(streams, os_process_capture_result(streams + 1, PROCESS_RESULT_SUCCESS));
    ProcessResult reversed = os_process_capture_result(streams + 1, os_process_capture_result(streams, PROCESS_RESULT_SUCCESS));
    BUSTER_TEST(arguments, folded == PROCESS_RESULT_FAILED && reversed == PROCESS_RESULT_FAILED);
    return result;
}
