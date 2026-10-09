// Each experimental child is waited by the existing OS owner, then its escaped
// descendants are adopted and reaped before another phase is admitted.
// Cancellation handlers only publish flags; exact-PID signalling remains in
// the OS wait and single-thread native supervisor.
#if BUSTER_LINUX
BUSTER_GLOBAL_LOCAL ProcessControlAtomic compiler_closure_cancel_signal;
BUSTER_GLOBAL_LOCAL ProcessControlAtomic compiler_closure_cancel_escalated;
BUSTER_GLOBAL_LOCAL bool compiler_closure_cleanup_failed;
BUSTER_GLOBAL_LOCAL bool compiler_closure_signals_owned;
BUSTER_GLOBAL_LOCAL u64 compiler_closure_cleanup_us;
BUSTER_GLOBAL_LOCAL u64 compiler_closure_cleanup_waves;
BUSTER_GLOBAL_LOCAL u64 compiler_closure_cleanup_signalled;
BUSTER_GLOBAL_LOCAL u64 compiler_closure_cleanup_reaped;
BUSTER_GLOBAL_LOCAL struct sigaction compiler_closure_prior_term;
BUSTER_GLOBAL_LOCAL struct sigaction compiler_closure_prior_int;

typedef struct CompilerClosurePhaseResult CompilerClosurePhaseResult;
struct CompilerClosurePhaseResult
{
    ProcessWaitResult wait;
    u64 cleanup_us, waves, signalled, reaped;
    bool cleanup_proven, success;
};

BUSTER_GLOBAL_LOCAL void compiler_closure_cancel(int signal)
{
    if (!process_control_atomic_set_if_zero(&compiler_closure_cancel_signal, (u64)signal))
    {
        process_control_atomic_store(&compiler_closure_cancel_escalated, 1);
    }
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_admitting(void)
{
    return !compiler_closure_cleanup_failed && !process_control_atomic_load(&compiler_closure_cancel_signal);
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_signals_begin(void)
{
    bool result = !compiler_closure_signals_owned && compiler_closure_admitting();
    struct sigaction action = {.sa_handler = compiler_closure_cancel};
    result = result && sigemptyset(&action.sa_mask) == 0;
    if (result)
    {
        result = sigaction(SIGTERM, &action, &compiler_closure_prior_term) == 0;
        if (result)
        {
            result = sigaction(SIGINT, &action, &compiler_closure_prior_int) == 0;
            if (!result) { sigaction(SIGTERM, &compiler_closure_prior_term, 0); }
        }
    }
    compiler_closure_signals_owned = result;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_closure_signals_end(void)
{
    bool result = compiler_closure_signals_owned;
    if (result)
    {
        bool interrupt = sigaction(SIGINT, &compiler_closure_prior_int, 0) == 0;
        bool terminate = sigaction(SIGTERM, &compiler_closure_prior_term, 0) == 0;
        result = interrupt && terminate;
        compiler_closure_signals_owned = false;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerClosurePhaseResult compiler_closure_phase_run_bounded(Arena* arena, SliceString8 arguments,
    String8 root, u64 timeout_us, bool observe_resources, u64 stream_limit)
{
    CompilerClosurePhaseResult result = {.wait = {.result = PROCESS_RESULT_FAILED}};
    CompilerExperimentSupervisor supervisor = {0};
    bool began = compiler_closure_admitting() && compiler_experiment_supervisor_begin(arena, &supervisor);
    if (began)
    {
        ProcessGroupControlState control = {.cancellation_signal = &compiler_closure_cancel_signal,
            .cancellation_escalated = &compiler_closure_cancel_escalated};
        ProcessRun run = {.arguments = arguments, .working_directory = root,
            .spawn_options = {.capture = (1u << STANDARD_STREAM_OUTPUT) | (1u << STANDARD_STREAM_ERROR),
                .use_process_environment = 1, .search_path = 1, .new_process_group = 1,
                .observe_resources = observe_resources,
                .capture_overflow_policy = PROCESS_CAPTURE_OVERFLOW_FAIL,
                .capture_limits = {.per_stream = {[STANDARD_STREAM_OUTPUT] = stream_limit,
                    [STANDARD_STREAM_ERROR] = stream_limit}, .total = stream_limit * 2}}};
        run.spawn = compiler_closure_admitting() ? process_run_spawn(arena, &run) : (ProcessSpawnResult){0};
        if (run.spawn.handle)
        {
            run.spawn.process_group_control = &control;
            result.wait = os_process_wait_deadline(arena, run.spawn, timeout_us);
        }
        bool released = !result.wait.process_group_reservation_retained && !result.wait.process_group_ownership_lost;
        u64 cleanup_start = os_now_microseconds();
        result.cleanup_proven = released && compiler_experiment_supervisor_end(arena, &supervisor);
        result.cleanup_us = os_now_microseconds() - cleanup_start;
        result.waves = supervisor.waves;
        result.signalled = supervisor.signalled;
        result.reaped = supervisor.reaped;
        compiler_closure_cleanup_us += result.cleanup_us;
        compiler_closure_cleanup_waves += result.waves;
        compiler_closure_cleanup_signalled += result.signalled;
        compiler_closure_cleanup_reaped += result.reaped;
        result.success = run.spawn.handle && result.cleanup_proven && compiler_closure_admitting() &&
            !result.signalled && !result.reaped && result.wait.result == PROCESS_RESULT_SUCCESS &&
            !result.wait.platform_status && !result.wait.timed_out && !result.wait.capture_failed &&
            !result.wait.output_truncated && !result.wait.process_tree_cleanup_failed;
    }
    if (!began || !result.cleanup_proven) { compiler_closure_cleanup_failed = true; }
    return result;
}

BUSTER_GLOBAL_LOCAL CompilerClosurePhaseResult compiler_closure_phase_run(Arena* arena, SliceString8 arguments,
    String8 root, u64 timeout_us, bool observe_resources)
{
    return compiler_closure_phase_run_bounded(arena, arguments, root, timeout_us, observe_resources, 1ull << 20);
}
#endif

#if BUSTER_LINUX
// Exercises the real OS manager deadline/cancellation lane, including children
// that retain capture pipes while living in independent private sessions.
BUSTER_GLOBAL_LOCAL ProcessResult compiler_closure_phase_self_test(Arena* arena, String8 mode)
{
    bool timeout = string_equal(mode, S8("timeout"));
    bool interrupt = string_equal(mode, S8("SIGINT"));
    bool terminate = string_equal(mode, S8("SIGTERM"));
    bool result = timeout || interrupt || terminate;
    String8 program = string_format(arena, S8(
        "import os,signal,time\n"
        "owner=os.getppid()\n"
        "child=os.fork()\n"
        "if child==0:\n"
        " os.setsid()\n"
        " grand=os.fork()\n"
        " if grand==0:\n"
        "  os.setsid()\n"
        "  while True: time.sleep(1)\n"
        " while True: time.sleep(1)\n"
        "time.sleep(0.2)\n"
        "if {u64}: os.kill(owner,{u64})\n"
        "while True: time.sleep(1)\n"), timeout ? 0ull : 1ull, interrupt ? (u64)SIGINT : (u64)SIGTERM);
    String8 arguments[] = {S8("python3"), S8("-B"), S8("-c"), program};
    CompilerClosurePhaseResult phase = result ? compiler_closure_phase_run(arena,
        (SliceString8)BUSTER_ARRAY_TO_SLICE(arguments), (String8){0},
        timeout ? 700000ull : 5000000ull, false) : (CompilerClosurePhaseResult){0};
    result = result && !phase.success && phase.cleanup_proven && phase.signalled >= 2 && phase.reaped >= 2 &&
        !phase.wait.process_group_reservation_retained && !phase.wait.process_group_ownership_lost &&
        (timeout ? phase.wait.timed_out != 0 : process_control_atomic_load(&compiler_closure_cancel_signal) ==
            (interrupt ? (u64)SIGINT : (u64)SIGTERM));
    bool canceled_admission = timeout || !compiler_closure_admitting();
    result = result && canceled_admission;
    string_print(S8("COMPILER_CLOSURE_CONTAINMENT mode={S8} cleanup_proven={u64} signalled={u64} reaped={u64} success={u64}\n"),
        mode, phase.cleanup_proven ? 1ull : 0ull, phase.signalled, phase.reaped, result ? 1ull : 0ull);
    return result ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}
#endif
