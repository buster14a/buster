// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original process-boundary controls, included after the driver's artifact helpers. Real child faults and hangs
// cannot publish an observation or mutate parent-owned storage. Replay executes the generated script, not a model.

static IrReduceObservation ir_reduce_test_process_observer(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    uint32_t* state = context;
    const uint32_t mode = state[0];
    state[1] = 99;
    function->a[0] = 99;
    scratch->base[0] = 99;
    scratch->length = 1;
    IrReduceObservation result = {
        .status = IR_REDUCE_OBSERVED,
        .failure = { .kind = 71, .pass = 3, .detail = 5 },
    };
    if (limits->max_milliseconds != limits->predicate_milliseconds)
    {
        result.status = IR_REDUCE_PREDICATE_ERROR;
    }
    if (mode == 1)
    {
        raise(SIGKILL);
    }
    else if (mode == 2)
    {
        for (;;)
        {
            pause();
        }
    }
    else if (mode == 3 || mode == 4)
    {
        _exit(mode == 3 ? 0 : 42);
    }
    else if (mode >= 5)
    {
        result.status = mode == 9 ? (IrReduceObservationStatus)-1 : (IrReduceObservationStatus)(mode - 4);
    }
    return result;
}

static IrReduceObservation ir_reduce_test_process_candidate(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    (void)limits;
    (void)scratch;
    const bool hang = *(const bool*)context;
    if (function->instruction_count < 3)
    {
        if (hang)
        {
            for (;;)
            {
                pause();
            }
        }
        else
        {
            raise(SIGKILL);
        }
    }
    const IrReduceObservation result = {
        .status = IR_REDUCE_OBSERVED,
        .failure = { .kind = 71, .pass = 3, .detail = 5 },
    };
    return result;
}

static void ir_reduce_test_process(IrReduceTest* test)
{
    const IrReduceObservationStatus expected[] = {
        IR_REDUCE_OBSERVED,
        IR_REDUCE_PREDICATE_FAULT,
        IR_REDUCE_PREDICATE_LIMIT,
        IR_REDUCE_PREDICATE_ERROR,
        IR_REDUCE_PREDICATE_FAULT,
        IR_REDUCE_UNSUPPORTED,
        IR_REDUCE_PREDICATE_LIMIT,
        IR_REDUCE_PREDICATE_FAULT,
        IR_REDUCE_PREDICATE_ERROR,
        IR_REDUCE_PREDICATE_ERROR,
    };
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(expected); mode += 1)
    {
        uint32_t state[2] = { mode, 7 };
        uint32_t operand = 8;
        uint8_t byte = 9;
        IrFunction function = { .a = &operand };
        Arena scratch = { .base = &byte, .capacity = 1 };
        IrReduceLimits limits = ir_reduce_limits();
        limits.max_milliseconds = 60000;
        limits.predicate_milliseconds = mode == 2 ? 50 : 5000;
        IrReduceProcess process = {
            .predicate = ir_reduce_test_process_observer,
            .context = state,
            .started = ir_reduce_milliseconds(),
        };
        const IrReduceObservation observed = ir_reduce_process(&function, &limits, &process, &scratch);
        const IrReduceFailure failure = { .kind = 71, .pass = 3, .detail = 5 };
        ir_reduce_test_expect(test, observed.status == expected[mode],
                              "process boundary distinguishes successful observations, signals, missing replies, hangs and invalid statuses");
        ir_reduce_test_expect(test, mode == 0 ? ir_reduce_same_failure(observed.failure, failure) :
                              ir_reduce_same_failure(observed.failure, (IrReduceFailure){ 0 }),
                              "only a successful observation transports a classified failure");
        ir_reduce_test_expect(test, state[1] == 7 && operand == 8 && byte == 9 && scratch.length == 0,
                              "successful, crashing and timed-out predicates cannot mutate the parent's function, context or arena");
        if (mode == 0)
        {
            process.started = UINT64_MAX;
            ir_reduce_test_expect(test, ir_reduce_process(&function, &limits, &process, &scratch).status == IR_REDUCE_PREDICATE_ERROR,
                                  "an unavailable monotonic clock refuses before fork");
            process.started = ir_reduce_milliseconds();
            limits.max_milliseconds = 0;
            ir_reduce_test_expect(test, ir_reduce_process(&function, &limits, &process, &scratch).status == IR_REDUCE_PREDICATE_LIMIT,
                                  "an exhausted overall budget refuses before fork");
        }
    }
    const uint64_t started = ir_reduce_milliseconds();
    const pid_t child = fork();
    if (child == 0)
    {
        (void)setpgid(0, 0);
        for (;;)
        {
            pause();
        }
    }
    bool reaped = false;
    if (child > 0)
    {
        (void)setpgid(child, child);
        int status = 0;
        const IrReduceObservationStatus waited = ir_reduce_process_wait(child, started, 50, &status);
        errno = 0;
        const pid_t again = waitpid(child, &status, WNOHANG);
        reaped = waited == IR_REDUCE_PREDICATE_LIMIT && again == -1 && errno == ECHILD;
    }
    ir_reduce_test_expect(test, reaped, "a timed-out child is killed and reaped, leaving no zombie");

    const char source[] = "function () -> i64 {\nb0:\n"
                          "  %0 = const.i64 0x00000000000000000000000000000007\n"
                          "  %1 = const.i64 0x00000000000000000000000000000008\n"
                          "  %2 = ret.void %0\n}\n";
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 27);
    IrFunction function = { 0 };
    const bool ready = input.base != NULL && output.base != NULL && scratch.base != NULL &&
                       ir_reduce_test_parse(source, &input, &scratch, &function);
    ir_reduce_test_expect(test, ready, "candidate crash/timeout fixture parses");
    for (uint32_t mode = 0; ready && mode < 2; mode += 1)
    {
        bool hang = mode != 0;
        IrReduceLimits limits = ir_reduce_limits();
        limits.max_milliseconds = 60000;
        limits.predicate_milliseconds = hang ? 50 : 5000;
        IrReduceProcess process = {
            .predicate = ir_reduce_test_process_candidate,
            .context = &hang,
            .started = ir_reduce_milliseconds(),
        };
        IrReduceResult reduced = { 0 };
        const IrReduceStatus status = ir_reduce(&function, ir_reduce_process, &process, &limits, &output, &scratch, &reduced);
        const IrReduceFailure failure = { .kind = 71, .pass = 3, .detail = 5 };
        ir_reduce_test_expect(test, status <= IR_REDUCE_CACHE_LIMIT && reduced.function.instruction_count == 3 &&
                              ir_reduce_same_failure(reduced.failure, failure) && (hang ? reduced.limits != 0 : reduced.faults != 0),
                              "a crashing or hanging smaller candidate cannot replace the original failure");
        ir_reduce_test_expect(test, status <= IR_REDUCE_CACHE_LIMIT &&
                              ir_verify_complete(&reduced.function, &scratch, true).error == IR_OK &&
                              ir_verify_complete(&reduced.function, &scratch, false).error == IR_OK &&
                              scratch.length == 0 && function.instruction_count == 3 && function.constants[0].low == 7,
                              "candidate process faults preserve both authoritative validity and the original case");
        arena_pop_to(&output, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static int ir_reduce_test_process_script(const char* script, const char* executable)
{
    const uint64_t started = ir_reduce_milliseconds();
    const pid_t child = fork();
    int result = -1;
    if (child == 0)
    {
        if (setpgid(0, 0) == 0 && setenv("BUSTER_COMPILER", executable, 1) == 0)
        {
            execl("/bin/sh", "sh", script, (char*)NULL);
        }
        _exit(125);
    }
    if (child > 0)
    {
        (void)setpgid(child, child);
        int status = 0;
        const IrReduceObservationStatus waited = ir_reduce_process_wait(child, started, 120000, &status);
        if (waited == IR_REDUCE_OBSERVED && WIFEXITED(status))
        {
            result = WEXITSTATUS(status);
        }
    }
    return result;
}

static bool ir_reduce_test_process_binary(const char* path)
{
    // An appended byte preserves this ELF's loadable image but changes its recorded byte identity.
    FILE* input = fopen("/proc/self/exe", "rb");
    FILE* output = input != NULL ? fopen(path, "wx") : NULL;
    bool valid = input != NULL && output != NULL;
    uint8_t buffer[16384];
    for (size_t got = valid ? fread(buffer, 1, sizeof(buffer), input) : 0; valid && got != 0; got = fread(buffer, 1, sizeof(buffer), input))
    {
        valid = fwrite(buffer, 1, got, output) == got;
    }
    if (input != NULL)
    {
        const bool read = !ferror(input);
        const bool closed = fclose(input) == 0;
        valid = read && closed && valid;
    }
    if (output != NULL)
    {
        const bool appended = valid && fputc(0, output) != EOF;
        const bool closed = fclose(output) == 0;
        valid = appended && closed;
    }
    valid = valid && chmod(path, 0700) == 0;
    return valid;
}

static void ir_reduce_test_process_replay(IrReduceTest* test, const char* directory, const char* bundle, Arena* storage)
{
    char executable[PATH_MAX];
    uint64_t hash = 0;
    uint64_t bytes = 0;
    const bool binary = ir_reduce_driver_binary(&hash, &bytes, executable, sizeof(executable));
    ir_reduce_test_expect(test, binary, "the running compiler has a complete path and reproducible byte identity");
    char refused[PATH_MAX];
    const bool paths = ir_reduce_test_driver_path(refused, sizeof(refused), directory, "identity-refused");
    for (uint32_t mode = 0; binary && paths && mode < 5; mode += 1)
    {
        char encoded_hash[24];
        char encoded_bytes[24];
        snprintf(encoded_hash, sizeof(encoded_hash), "0x%016" PRIx64, hash ^ (mode == 2));
        snprintf(encoded_bytes, sizeof(encoded_bytes), "%" PRIu64, mode == 4 ? UINT64_C(0) : bytes + (mode == 3));
        char* arguments[20] = {
            "--revision", "0123456789abcdef0123456789abcdef01234567",
            "--output", refused,
            "--demo", "phi",
            "--inject", "result-phi",
        };
        uint32_t count = 8;
        if (mode != 1)
        {
            arguments[count++] = "--expect-binary-hash";
            arguments[count++] = encoded_hash;
        }
        if (mode != 0)
        {
            arguments[count++] = "--expect-binary-bytes";
            arguments[count++] = encoded_bytes;
        }
        const int status = run_ir_reduce((int)count, arguments);
        ir_reduce_test_expect(test, status == 1 && ir_reduce_test_driver_absent(refused),
                              "partial identity, wrong hash, wrong size and zero size refuse without publication");
        ir_reduce_test_driver_remove(refused);
    }
    char short_path[2] = { 7, 8 };
    ir_reduce_test_expect(test, !ir_reduce_driver_binary(&hash, &bytes, short_path, sizeof(short_path)) &&
                          short_path[1] == 8 && !ir_reduce_driver_binary(&hash, &bytes, short_path, 0),
                          "truncated executable paths and zero path capacity refuse without overrun");
    char changed[PATH_MAX];
    char moved[PATH_MAX];
    char script[PATH_MAX];
    char replayed[PATH_MAX];
    char first[PATH_MAX];
    char second[PATH_MAX];
    const bool named = binary && ir_reduce_test_driver_path(changed, sizeof(changed), directory, "changed-compiler") &&
                       ir_reduce_test_driver_path(moved, sizeof(moved), directory, "moved ' bundle") &&
                       ir_reduce_test_driver_path(script, sizeof(script), moved, "replay.sh") &&
                       ir_reduce_test_driver_path(replayed, sizeof(replayed), moved, "replayed") &&
                       ir_reduce_test_driver_path(first, sizeof(first), moved, "reduced.ir") &&
                       ir_reduce_test_driver_path(second, sizeof(second), replayed, "reduced.ir");
    const bool renamed = named && rename(bundle, moved) == 0;
    ir_reduce_test_expect(test, renamed, "the replay fixture moves to a workspace path containing whitespace and a quote");
    if (renamed)
    {
        const bool copied = ir_reduce_test_process_binary(changed);
        const int refused_status = copied ? ir_reduce_test_process_script(script, changed) : -1;
        ir_reduce_test_expect(test, copied && refused_status == 1 && ir_reduce_test_driver_absent(replayed),
                              "the actual generated replay refuses an otherwise runnable compiler with a different byte identity");
        unlink(changed);
        const int status = ir_reduce_test_process_script(script, executable);
        const uint64_t mark = storage->length;
        String8 original = { 0 };
        String8 reduced = { 0 };
        const bool same = status == 0 && ir_reduce_test_driver_read(first, storage, &original) &&
                          ir_reduce_test_driver_read(second, storage, &reduced) && original.length == reduced.length &&
                          memcmp(original.pointer, reduced.pointer, original.length) == 0;
        ir_reduce_test_expect(test, same, "the moved generated script executes with the recorded binary and reproduces identical reduced IR");
        arena_pop_to(storage, mark);
        ir_reduce_test_driver_remove(replayed);
        ir_reduce_test_expect(test, rename(moved, bundle) == 0, "moved replay cleanup restores the untouched original bundle");
    }
}
