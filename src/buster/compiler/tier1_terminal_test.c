// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Independent terminal-shape fixtures, included after tier1_call_test.c. Original semantic IR reaches the real
// selector, allocator, frame emitter and both encoders. Original helper records either return or execute UD2;
// they are backend ABI fixtures, not production builtin implementations or C-source acceptance. Empty sinks must
// remain real self-branches. Native children publish readiness and are observed, killed and reaped with bounded
// nonblocking waits; an unexpected return, signal or repeated helper call cannot count as an infinite-loop pass.

#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>

#define BUSTER_TIER1_TERMINAL_CODE_BYTES (16u << 10)
#define BUSTER_TIER1_TERMINAL_HELPER_OFFSET (8u << 10)
#define BUSTER_TIER1_TERMINAL_TIMEOUT_MS 2000u
#define BUSTER_TIER1_TERMINAL_OBSERVE_MS 20u
#define BUSTER_TIER1_TERMINAL_MARKER UINT64_C(0x37a11e7a1c001eed)
#define BUSTER_TIER1_TERMINAL_RETURN UINT64_C(0x9371a64bd28ef005)

typedef enum Tier1TerminalShape
{
    TIER1_TERMINAL_RETURN,
    TIER1_TERMINAL_UNREACHABLE,
    TIER1_TERMINAL_FORWARD_UNREACHABLE,
    TIER1_TERMINAL_SINK,
    TIER1_TERMINAL_RETURNING_HELPER,
    TIER1_TERMINAL_TRAP_HELPER,
    TIER1_TERMINAL_COUNT,
} Tier1TerminalShape;

typedef struct Tier1TerminalObserved Tier1TerminalObserved;
struct Tier1TerminalObserved
{
    // The sole child writes aligned machine words; the parent observes this shared mapping on Linux/x86-64.
    volatile uint64_t entered;
    volatile uint64_t calls;
    volatile uint64_t alignment;
    volatile uint64_t ready;
    volatile uint64_t result;
    volatile uint64_t finished;
};

static uint64_t tier1_terminal_milliseconds(void)
{
    struct timespec time;
    const bool valid = clock_gettime(CLOCK_MONOTONIC, &time) == 0;
    return valid ? (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000 : 0;
}

// The iteration limit also bounds polling when the clock fails or signals interrupt every short sleep.
static bool tier1_terminal_reap(pid_t child, int* status)
{
    const uint64_t start = tier1_terminal_milliseconds();
    const struct timespec pause = {
        .tv_nsec = 1000000,
    };
    bool finished = false;
    bool waiting = child > 0;
    for (uint32_t attempt = 0; waiting && attempt <= BUSTER_TIER1_TERMINAL_TIMEOUT_MS; attempt += 1)
    {
        const pid_t reaped = waitpid(child, status, WNOHANG);
        finished = reaped == child;
        const bool poll_ok = reaped == 0 || (reaped < 0 && errno == EINTR);
        const uint64_t now = tier1_terminal_milliseconds();
        waiting = !finished && poll_ok && start != 0 && now >= start && now - start < BUSTER_TIER1_TERMINAL_TIMEOUT_MS;
        if (waiting)
        {
            nanosleep(&pause, 0);
        }
    }
    return finished;
}

static bool tier1_terminal_fixture(Tier1EmitTest* test, Tier1TerminalShape shape, IrFunction* function)
{
    const bool calls = shape >= TIER1_TERMINAL_RETURNING_HELPER;
    const bool forward = shape == TIER1_TERMINAL_FORWARD_UNREACHABLE;
    const bool returns = shape == TIER1_TERMINAL_RETURN;
    const IrSizes sizes = {
        .instruction_count = calls || forward || returns ? 3 : 1,
        .block_count = forward ? 3 : calls || returns ? 2 : 1,
        .constant_count = returns,
        .call_count = calls,
    };
    const bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->result_type = returns ? IR_I64 : IR_VOID;
        function->symbol_count = 2;
        function->block_offsets[function->block_count] = function->instruction_count;
        if (returns)
        {
            function->constants[0].low = BUSTER_TIER1_TERMINAL_RETURN;
            function->block_offsets[1] = 2;
            tier1_emit_test_node(function, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 2, IR_RET, IR_VOID, 0, BUSTER_IR_NONE);
        }
        else if (calls)
        {
            // The unknown-effect call is followed by an isolated sink even when its helper unexpectedly returns.
            function->block_offsets[1] = 2;
            tier1_call_descriptor(function, 0, 0, 0, IR_VOID);
            tier1_emit_test_node(function, 0, IR_CALL, IR_VOID, 1, 0);
            tier1_emit_test_node(function, 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 2, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
        }
        else if (forward)
        {
            function->block_offsets[1] = 1;
            function->block_offsets[2] = 2;
            tier1_emit_test_node(function, 0, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 1, IR_BR, IR_VOID, 2, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 2, IR_UNREACHABLE, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            const bool sink = shape == TIER1_TERMINAL_SINK;
            tier1_emit_test_node(function, 0, sink ? IR_BR : IR_UNREACHABLE, IR_VOID, sink ? 0 : BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
    }
    return ready;
}

static bool tier1_terminal_dominance(Tier1EmitTest* test, const IrFunction* function, bool vector, IrDominance* dominance)
{
    IrCfg cfg;
    bool ready = ir_build_cfg(function, &test->output, &test->scratch, &cfg) == IR_OK;
    if (ready)
    {
        const DominatorCfg graph = {
            .block_count = cfg.block_count,
            .edge_count = cfg.edge_count,
            .entry = 0,
            .successor_offsets = cfg.successor_offsets,
            .successors = cfg.successors,
            .predecessor_offsets = cfg.predecessor_offsets,
            .predecessors = cfg.predecessors,
        };
        DominatorOptions options = dominators_options();
        options.path = vector ? DOMINATOR_VECTOR : DOMINATOR_SCALAR;
        options.outputs = 0;
        Dominators result;
        ready = dominators_analyze(&test->output, &test->scratch, &graph, options, &result) == DOMINATOR_OK;
        if (ready)
        {
            *dominance = (IrDominance){
                .enter = result.preorder,
                .leave = result.subtree_end,
                .block_count = result.block_count,
            };
        }
    }
    return ready;
}

static uint8_t* tier1_terminal_load(Tier1EmitTest* test, Tier1TerminalShape shape, const IrFunction* function, Tier1EmitResult* emitted, Tier1TerminalObserved* observed, uint8_t* mapping)
{
    Tier1SelectCode* code = &emitted->code;
    const bool calls = shape >= TIER1_TERMINAL_RETURNING_HELPER;
    const bool sink = shape >= TIER1_TERMINAL_SINK;
    const uint32_t last = function->instruction_count - 1;
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    uint32_t* offsets = calloc((uint64_t)code->count + 1, sizeof(uint32_t));
    uint32_t* ends = calloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count), sizeof(uint32_t));
    uint32_t* scalar_ends = calloc(BUSTER_X86_ENCODER_FIXUP_CAPACITY(emitted->reference_count), sizeof(uint32_t));
    uint8_t* scalar = malloc(BUSTER_TIER1_TERMINAL_CODE_BYTES);
    bool ready = offsets && ends && scalar_ends && scalar && code->count != 0 && capacity <= BUSTER_TIER1_TERMINAL_HELPER_OFFSET;
    tier1_emit_test_expect(test, ready, "terminal encoder storage is bounded");
    if (ready)
    {
        ready = emitted->reference_count == (uint32_t)calls && code->branch_count == (uint32_t)sink;
        for (uint32_t node = 0; ready && node < function->instruction_count; node += 1)
        {
            ready = code->node_first[node] <= code->node_first[node + 1] && code->node_first[node + 1] <= code->count;
            if (ready && node != last && function->op[node] == IR_BR)
            {
                ready = code->node_first[node] == code->node_first[node + 1];
            }
        }
        if (ready && sink)
        {
            ready = code->branches[0] == code->node_first[last] && code->targets[0] == code->branches[0] && code->branches[0] + 1 == code->count && code->records[code->branches[0]].form == X86_FORM_JMP_REL8;
        }
        if (ready && !sink)
        {
            ready = code->records[code->count - 1].form == (shape == TIER1_TERMINAL_RETURN ? X86_FORM_RET : X86_FORM_UD2);
        }
        uint32_t marked = 0;
        for (uint32_t record = 0; ready && record < code->count; record += 1)
        {
            if (sink)
            {
                ready = code->records[record].form != X86_FORM_UD2 && code->records[record].form != X86_FORM_RET;
            }
            if ((code->records[record].form & BUSTER_X86_FORM_FIXUP) != 0)
            {
                ready = ready && calls && marked == 0 && emitted->references[0].instruction == record && code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP);
                marked += 1;
            }
            else if ((code->records[record].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_CALL_REL32)
            {
                ready = false;
            }
        }
        ready = ready && marked == (uint32_t)calls;
        tier1_emit_test_expect(test, ready, "terminal node ranges preserve identity edges and exact self-target metadata without synthetic traps or returns");
    }
    if (ready)
    {
        ready = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count).error == BRANCH_SIZING_OK &&
            size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets).error == BRANCH_SIZING_OK && x86_target_validate(test->allowed, code->records, code->count) == code->count;
        tier1_emit_test_expect(test, ready, "terminal branch metadata and baseline forms validate");
    }
    int64_t length = -1;
    if (ready)
    {
        length = x86_encode(code->records, code->count, mapping, capacity, 0, ends);
        const int64_t scalar_length = x86_encode_scalar(code->records, code->count, scalar, capacity, offsets, scalar_ends);
        ready = length > 0 && (uint64_t)length <= capacity && length == scalar_length && memcmp(mapping, scalar, (size_t)length) == 0 && memcmp(ends, scalar_ends, emitted->reference_count * sizeof(uint32_t)) == 0;
        tier1_emit_test_expect(test, ready, "terminal scalar/vector encoders agree on all bytes and symbolic field ends");
    }
    if (ready)
    {
        if (sink)
        {
            const uint32_t position = offsets[code->branches[0]];
            ready = position <= (uint32_t)length && (uint32_t)length - position == 2 && mapping[position] == 0xeb && mapping[position + 1] == 0xfe;
        }
        else if (shape == TIER1_TERMINAL_RETURN)
        {
            ready = mapping[length - 1] == 0xc3;
        }
        else
        {
            const uint32_t position = offsets[code->count - 1];
            ready = position <= (uint32_t)length && (uint32_t)length - position == 2 && mapping[position] == 0x0f && mapping[position + 1] == 0x0b;
        }
        tier1_emit_test_expect(test, ready, "independent terminal byte oracle identifies RET, UD2 or the actual EB FE self-loop");
    }
    if (ready && calls)
    {
        X86Instruction helper[] = {
            tier1_call_constant(X86_REGISTER_R11, (uintptr_t)observed),
            tier1_frame_emit_memory(X86_FORM_ADD_RM64_IMM8, 0, X86_REGISTER_R11, offsetof(Tier1TerminalObserved, calls)),
            tier1_frame_emit_register(X86_FORM_MOV_R64_RM64, X86_REGISTER_R10, X86_REGISTER_RSP),
            tier1_frame_emit_immediate(X86_FORM_AND_RM64_IMM32, X86_REGISTER_R10, 15),
            tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R11, offsetof(Tier1TerminalObserved, alignment)),
            tier1_call_constant(X86_REGISTER_R10, BUSTER_TIER1_TERMINAL_MARKER),
            tier1_frame_emit_memory(X86_FORM_MOV_RM64_R64, X86_REGISTER_R10, X86_REGISTER_R11, offsetof(Tier1TerminalObserved, ready)),
            {
                .form = shape == TIER1_TERMINAL_TRAP_HELPER ? X86_FORM_UD2 : X86_FORM_RET,
            },
        };
        helper[1].immediate = 1;
        const uint32_t count = BUSTER_ARRAY_LENGTH(helper);
        const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(count);
        ready = helper_capacity <= BUSTER_TIER1_TERMINAL_CODE_BYTES - BUSTER_TIER1_TERMINAL_HELPER_OFFSET && x86_target_validate(test->allowed, helper, count) == count;
        const int64_t helper_length = ready ? x86_encode(helper, count, mapping + BUSTER_TIER1_TERMINAL_HELPER_OFFSET, helper_capacity, 0, 0) : -1;
        const int64_t scalar_length = ready ? x86_encode_scalar(helper, count, scalar + BUSTER_TIER1_TERMINAL_HELPER_OFFSET, helper_capacity, 0, 0) : -1;
        ready = helper_length > 0 && (uint64_t)helper_length <= helper_capacity && helper_length == scalar_length && memcmp(mapping + BUSTER_TIER1_TERMINAL_HELPER_OFFSET, scalar + BUSTER_TIER1_TERMINAL_HELPER_OFFSET, (size_t)helper_length) == 0;
        tier1_emit_test_expect(test, ready, "independent returning/trapping helper encodes identically within baseline local TEXT");
        if (ready)
        {
            const LinkReference* reference = &emitted->references[0];
            const uint32_t end = ends[0];
            ready = reference->instruction < code->count && reference->target == function->a[0] && reference->target == 1 && reference->field == X86_FIXUP_DISPLACEMENT && reference->kind == BUSTER_LINK_FIXUP_CALL && reference->addend == 0;
            if (ready)
            {
                const uint32_t record = reference->instruction;
                ready = code->records[record].form == (X86_FORM_CALL_REL32 | BUSTER_X86_FORM_FIXUP) && record >= code->node_first[0] && record < code->node_first[1] &&
                    x86_fixup_distance(code->records[record].form, reference->field) == 4 && end >= 5 && end <= (uint32_t)length && end == offsets[record] + 5 && mapping[end - 5] == 0xe8;
                if (ready)
                {
                    const int64_t displacement = (int64_t)BUSTER_TIER1_TERMINAL_HELPER_OFFSET - end;
                    ready = displacement >= INT32_MIN && displacement <= INT32_MAX;
                    if (ready)
                    {
                        const int32_t relative = (int32_t)displacement;
                        memcpy(mapping + end - 4, &relative, sizeof(relative));
                        memcpy(scalar + end - 4, &relative, sizeof(relative));
                        int32_t decoded;
                        memcpy(&decoded, mapping + end - 4, sizeof(decoded));
                        ready = (int64_t)end + decoded == BUSTER_TIER1_TERMINAL_HELPER_OFFSET && memcmp(mapping, scalar, (size_t)length) == 0;
                    }
                }
            }
        }
        tier1_emit_test_expect(test, ready, "terminal CALL uses the original symbol and an independently checked E8 rel32 target");
    }
    if (ready)
    {
        ready = mprotect(mapping, BUSTER_TIER1_TERMINAL_CODE_BYTES, PROT_READ | PROT_EXEC) == 0;
        tier1_emit_test_expect(test, ready, "terminal local TEXT becomes executable only after checks and relocation");
    }
    free(scalar);
    free(scalar_ends);
    free(ends);
    free(offsets);
    return ready ? mapping : 0;
}

static void tier1_terminal_execute(Tier1EmitTest* test, Tier1TerminalShape shape, const IrFunction* function, Tier1EmitResult* emitted)
{
    Tier1TerminalObserved* observed = mmap(0, sizeof(*observed), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t* mapping = mmap(0, BUSTER_TIER1_TERMINAL_CODE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool storage = observed != MAP_FAILED && mapping != MAP_FAILED;
    tier1_emit_test_expect(test, storage, "terminal native code and shared observations allocate");
    const uint8_t* entry = storage ? tier1_terminal_load(test, shape, function, emitted, observed, mapping) : 0;
    const bool persistent = shape == TIER1_TERMINAL_SINK || shape == TIER1_TERMINAL_RETURNING_HELPER;
    const bool calls = shape >= TIER1_TERMINAL_RETURNING_HELPER;
    const pid_t owner = getpid();
    fflush(0);
    const pid_t child = entry ? fork() : -1;
    if (child == 0)
    {
        const struct rlimit cores = {0};
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGALRM);
        sigaddset(&signals, SIGILL);
        sigaddset(&signals, SIGSEGV);
        sigaddset(&signals, SIGBUS);
        const bool ready = setrlimit(RLIMIT_CORE, &cores) == 0 && prctl(PR_SET_PDEATHSIG, SIGKILL) == 0 && getppid() == owner &&
            signal(SIGALRM, SIG_DFL) != SIG_ERR && signal(SIGILL, SIG_DFL) != SIG_ERR && signal(SIGSEGV, SIG_DFL) != SIG_ERR && signal(SIGBUS, SIG_DFL) != SIG_ERR && sigprocmask(SIG_UNBLOCK, &signals, 0) == 0;
        if (ready)
        {
            alarm(5);
            observed->entered = BUSTER_TIER1_TERMINAL_MARKER;
            if (shape == TIER1_TERMINAL_RETURN)
            {
                const uint64_t arguments[6] = {0};
                observed->result = tier1_call_invoke(entry, arguments);
            }
            else
            {
                tier1_call_invoke_void(entry);
            }
            observed->finished = BUSTER_TIER1_TERMINAL_MARKER;
        }
        _exit(ready ? 0 : 125);
    }
    int status = 0;
    bool finished = false;
    bool survived = false;
    uint64_t ready_at = 0;
    const uint64_t start = tier1_terminal_milliseconds();
    const struct timespec pause = {
        .tv_nsec = 1000000,
    };
    bool waiting = child > 0 && start != 0;
    for (uint32_t attempt = 0; waiting && attempt <= BUSTER_TIER1_TERMINAL_TIMEOUT_MS; attempt += 1)
    {
        const pid_t reaped = waitpid(child, &status, WNOHANG);
        finished = reaped == child;
        const bool poll_ok = reaped == 0 || (reaped < 0 && errno == EINTR);
        const uint64_t now = tier1_terminal_milliseconds();
        const bool clock_valid = now >= start;
        const bool entered = observed->entered == BUSTER_TIER1_TERMINAL_MARKER && (!calls || observed->ready == BUSTER_TIER1_TERMINAL_MARKER);
        if (persistent && entered && ready_at == 0 && clock_valid)
        {
            ready_at = now;
        }
        survived = persistent && !finished && reaped == 0 && ready_at != 0 && clock_valid && now - ready_at >= BUSTER_TIER1_TERMINAL_OBSERVE_MS;
        waiting = !finished && !survived && poll_ok && clock_valid && now - start < BUSTER_TIER1_TERMINAL_TIMEOUT_MS;
        if (waiting)
        {
            nanosleep(&pause, 0);
        }
    }
    if (entry)
    {
        const bool expected = persistent ? survived && observed->finished == 0 : finished && (shape == TIER1_TERMINAL_RETURN ? WIFEXITED(status) && WEXITSTATUS(status) == 0 && observed->finished == BUSTER_TIER1_TERMINAL_MARKER && observed->result == BUSTER_TIER1_TERMINAL_RETURN : WIFSIGNALED(status) && WTERMSIG(status) == SIGILL && observed->finished == 0);
        tier1_emit_test_expect(test, expected && observed->entered == BUSTER_TIER1_TERMINAL_MARKER, "terminal child returns exact bits, raises SIGILL or remains alive after readiness as specified");
        tier1_emit_test_expect(test, calls ? observed->calls == 1 && observed->ready == BUSTER_TIER1_TERMINAL_MARKER && observed->alignment == 8 : observed->calls == 0, "terminal helper runs exactly once with SysV alignment before trapping or entering its isolated sink");
    }
    if (child > 0 && !finished)
    {
        const bool killed = kill(child, SIGKILL) == 0;
        finished = tier1_terminal_reap(child, &status);
        tier1_emit_test_expect(test, killed && finished && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "persistent or failed terminal child is killed and reaped within a separate deadline");
    }
    if (entry)
    {
        tier1_emit_test_expect(test, child > 0 && finished, "every terminal native child is reaped without a blocking wait");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, BUSTER_TIER1_TERMINAL_CODE_BYTES);
    }
    if (observed != MAP_FAILED)
    {
        munmap(observed, sizeof(*observed));
    }
}

static void tier1_terminal_failures(Tier1EmitTest* test, const Tier1SelectPlan* plan, const Tier1EmitOptions* options, uint64_t required, uint32_t denied_form)
{
    uint8_t* denied = malloc(X86_FORM_COUNT);
    tier1_emit_test_expect(test, denied != 0, "terminal target-denial table allocates");
    if (denied)
    {
        memcpy(denied, test->allowed, X86_FORM_COUNT);
        denied[denied_form] = 0;
        for (uint32_t failure = 0; failure < 4; failure += 1)
        {
            Arena output = test->output;
            Arena scratch = test->scratch;
            const uint64_t output_mark = output.length;
            const uint64_t scratch_mark = scratch.length;
            if (failure == 1 || failure == 3)
            {
                output.capacity = failure == 1 ? output_mark + 1 : required - 1;
            }
            if (failure == 2)
            {
                scratch.capacity = scratch_mark + 1;
            }
            Tier1EmitResult result;
            memset(&result, 0xa5, sizeof(result));
            const Tier1EmitResult before = result;
            uint32_t node = BUSTER_IR_NONE;
            const Tier1EmitError error = tier1_emit_function(plan, options, failure == 0 ? denied : test->allowed, &output, &scratch, &result, &node);
            tier1_emit_test_expect(test, error == (failure == 0 ? TIER1_EMIT_TARGET : TIER1_EMIT_MEMORY), "terminal target denial and early/late allocation failures report their exact category");
            tier1_emit_test_expect(test, output.length == output_mark && scratch.length == scratch_mark && memcmp(&result, &before, sizeof(result)) == 0, "failed terminal emission restores arenas and publishes no partial branch, call or result");
        }
        free(denied);
    }
}

static void tier1_terminal_supported(Tier1EmitTest* test)
{
    for (uint32_t shape = 0; shape < TIER1_TERMINAL_COUNT; shape += 1)
    {
        IrFunction function;
        const bool ready = tier1_terminal_fixture(test, (Tier1TerminalShape)shape, &function);
        tier1_emit_test_expect(test, ready && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK, "original terminal fixture is valid semantic IR on both paths");
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            for (uint32_t frame = 0; frame < 2; frame += 1)
            {
                IrDominance dominance;
                const bool derived = tier1_terminal_dominance(test, &function, vector != 0, &dominance);
                tier1_emit_test_expect(test, derived, "terminal fixture derives its real CFG and dominance before cross-block selection");
                Tier1SelectPlan plan;
                uint32_t node = BUSTER_IR_NONE;
                const Tier1SelectError selected = derived ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector != 0, &plan, &node) : TIER1_SELECT_ERROR_MEMORY;
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "terminal fixture selects on the scalar/vector path");
                if (selected != TIER1_SELECT_OK)
                {
                    fprintf(stderr, "  terminal shape=%u vector=%u frame=%u selection=%u node=%u\n", shape, vector, frame, selected, node);
                }
                if (selected == TIER1_SELECT_OK)
                {
                    const Tier1EmitOptions options = {
                        .register_mask = tier1_liveness_register_mask(),
                        .frame_flags = frame ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        .vector = vector != 0,
                        .rematerialize = true,
                    };
                    const uint64_t output_mark = test->output.length;
                    const uint64_t scratch_mark = test->scratch.length;
                    Tier1EmitResult emitted;
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &emitted, &node);
                    tier1_emit_test_expect(test, error == TIER1_EMIT_OK && test->scratch.length == scratch_mark, "terminal emission succeeds and restores scratch");
                    if (error == TIER1_EMIT_OK)
                    {
                        const uint64_t required = test->output.length;
                        tier1_emit_test_expect(test, tier1_allocation_verify(&emitted.prepared.allocation, &emitted.allocation), "terminal allocation satisfies independent interference checks");
                        tier1_emit_test_expect(test, emitted.frame.frame_pointer == (frame != 0) && (shape < TIER1_TERMINAL_RETURNING_HELPER || !emitted.frame.red_zone), "terminal variants realize the requested frame pointer and protect calls from the red zone");
                        tier1_terminal_execute(test, (Tier1TerminalShape)shape, &function, &emitted);
                        arena_pop_to(&test->output, output_mark);
                        const uint32_t denied = shape == TIER1_TERMINAL_RETURN ? X86_FORM_RET : shape < TIER1_TERMINAL_SINK ? X86_FORM_UD2 : shape == TIER1_TERMINAL_TRAP_HELPER ? X86_FORM_CALL_REL32 : X86_FORM_JMP_REL8;
                        tier1_terminal_failures(test, &plan, &options, required, denied);
                    }
                }
                arena_pop_to(&test->output, 0);
            }
        }
        arena_pop_to(&test->input, 0);
    }
}

static bool tier1_terminal_refusal_fixture(Tier1EmitTest* test, uint32_t shape, IrFunction* function)
{
    const bool phi = shape == 3;
    const bool conditional = shape == 2;
    const IrSizes sizes = {
        .instruction_count = shape == 0 ? 2 : shape == 1 ? 3 : conditional ? 6 : 4,
        .block_count = shape == 0 ? 1 : conditional ? 3 : 2,
        .extra_count = phi ? 2 : 0,
        .constant_count = phi || conditional,
        .call_count = shape < 2,
        .parameter_count = conditional,
    };
    const bool ready = ir_create(&test->input, &sizes, function) == IR_OK;
    if (ready)
    {
        function->symbol_count = 2;
        function->result_type = phi ? IR_I64 : IR_VOID;
        function->block_offsets[function->block_count] = function->instruction_count;
        if (shape < 2)
        {
            const uint32_t first = shape;
            if (shape != 0)
            {
                function->block_offsets[1] = 1;
                tier1_emit_test_node(function, 0, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            }
            tier1_call_descriptor(function, 0, 0, 0, IR_VOID);
            tier1_emit_test_node(function, first, IR_CALL, IR_VOID, 1, 0);
            tier1_emit_test_node(function, first + 1, IR_BR, IR_VOID, shape, BUSTER_IR_NONE);
        }
        else if (conditional)
        {
            function->parameter_types[0] = IR_I32;
            function->block_offsets[1] = 4;
            function->block_offsets[2] = 5;
            tier1_emit_test_node(function, 0, IR_PARAM, IR_I32, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 1, IR_CONST, IR_I32, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 2, IR_ICMP, IR_I1, 0, 1);
            function->c[2] = IR_NE;
            tier1_emit_test_node(function, 3, IR_CBR, IR_VOID, 2, 1);
            function->c[3] = 2;
            tier1_emit_test_node(function, 4, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 5, IR_RET, IR_VOID, BUSTER_IR_NONE, BUSTER_IR_NONE);
        }
        else
        {
            function->block_offsets[1] = 2;
            function->constants[0].low = BUSTER_TIER1_TERMINAL_RETURN;
            function->extra[0] = 0;
            function->extra[1] = 0;
            tier1_emit_test_node(function, 0, IR_CONST, IR_I64, 0, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 1, IR_BR, IR_VOID, 1, BUSTER_IR_NONE);
            tier1_emit_test_node(function, 2, IR_PHI, IR_I64, 0, 1);
            tier1_emit_test_node(function, 3, IR_RET, IR_VOID, 2, BUSTER_IR_NONE);
        }
    }
    return ready;
}

static void tier1_terminal_refusals(Tier1EmitTest* test)
{
    for (uint32_t shape = 0; shape < 4; shape += 1)
    {
        IrFunction function;
        const bool ready = tier1_terminal_refusal_fixture(test, shape, &function);
        tier1_emit_test_expect(test, ready && ir_verify_scalar(&function, &test->scratch, 0).error == IR_OK && ir_verify(&function, &test->scratch, 0).error == IR_OK, "nonempty loop, conditional or phi refusal fixture is valid semantic IR");
        for (uint32_t vector = 0; ready && vector < 2; vector += 1)
        {
            IrDominance dominance;
            const bool derived = tier1_terminal_dominance(test, &function, vector != 0, &dominance);
            tier1_emit_test_expect(test, derived, "terminal refusal derives real dominance before testing the unsupported boundary");
            Tier1SelectPlan plan;
            memset(&plan, 0xa5, sizeof(plan));
            const Tier1SelectPlan before = plan;
            uint32_t node = BUSTER_IR_NONE;
            const uint64_t output_mark = test->output.length;
            const uint64_t scratch_mark = test->scratch.length;
            const Tier1SelectError selected = derived ? tier1_select_plan(&function, &dominance, &test->output, &test->scratch, vector != 0, &plan, &node) : TIER1_SELECT_ERROR_MEMORY;
            if (shape == 3)
            {
                tier1_emit_test_expect(test, selected == TIER1_SELECT_ERROR_UNSUPPORTED, "phi still refuses at selection");
                tier1_emit_test_expect(test, test->output.length == output_mark && test->scratch.length == scratch_mark && memcmp(&plan, &before, sizeof(plan)) == 0, "phi refusal restores arenas and preserves the prior result");
            }
            else
            {
                tier1_emit_test_expect(test, selected == TIER1_SELECT_OK, "valid nonempty loops and conditionals select before the bounded native boundary");
                if (selected != TIER1_SELECT_OK)
                {
                    fprintf(stderr, "  terminal refusal shape=%u vector=%u frame=n/a selection=%u node=%u\n", shape, vector, selected, node);
                }
                for (uint32_t frame = 0; selected == TIER1_SELECT_OK && frame < 2; frame += 1)
                {
                    const Tier1EmitOptions options = {
                        .register_mask = tier1_liveness_register_mask(),
                        .frame_flags = frame ? BUSTER_TIER1_FRAME_KEEP_POINTER : 0,
                        .vector = vector != 0,
                    };
                    Tier1EmitResult result;
                    memset(&result, 0xa5, sizeof(result));
                    const Tier1EmitResult saved = result;
                    const uint64_t mark = test->output.length;
                    const Tier1EmitError error = tier1_emit_function(&plan, &options, test->allowed, &test->output, &test->scratch, &result, &node);
                    tier1_emit_test_expect(test, error == TIER1_EMIT_UNSUPPORTED, "nonempty self-loops and general conditional flow remain unsupported, never rewritten to traps");
                    tier1_emit_test_expect(test, test->output.length == mark && test->scratch.length == scratch_mark && memcmp(&result, &saved, sizeof(result)) == 0, "unsupported terminal topology preserves arenas and the published result");
                }
            }
            arena_pop_to(&test->output, 0);
        }
        arena_pop_to(&test->input, 0);
    }
}

static void tier1_terminal_tests(Tier1EmitTest* test)
{
    arena_pop_to(&test->input, 0);
    arena_pop_to(&test->output, 0);
    arena_pop_to(&test->scratch, 0);
    test->terms = 0;
    test->width = 8;
    test->selects = false;
    tier1_terminal_supported(test);
    tier1_terminal_refusals(test);
    tier1_emit_test_expect(test, test->input.length == 0 && test->output.length == 0 && test->scratch.length == 0, "terminal suite returns all arenas to their entry lengths");
}
