// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Reducer consumers of the actual verifier, optimizer and interpreter APIs. No production consumer is modified.
// One function, caller-stated integer argument bits and one registered 32-byte global are enough for standalone
// reproducers. Test injections alter an observer after real execution; their distinct option is never implicit.

typedef enum IrReduceFailureKind
{
    IR_REDUCE_FAILURE_NONE,
    IR_REDUCE_FAILURE_VERIFY,
    IR_REDUCE_FAILURE_RESULT,
    IR_REDUCE_FAILURE_MEMORY,
    IR_REDUCE_FAILURE_EXECUTION,
} IrReduceFailureKind;

typedef enum IrReduceInjection
{
    IR_REDUCE_INJECT_NONE,
    IR_REDUCE_INJECT_VERIFY_PHI,
    IR_REDUCE_INJECT_RESULT_PHI,
    IR_REDUCE_INJECT_MEMORY_STORE,
} IrReduceInjection;

typedef struct IrReduceOracle IrReduceOracle;
struct IrReduceOracle
{
    InterpreterValue arguments[16];
    uint32_t argument_count;
    uint32_t passes;
    IrReduceInjection injection;
    bool verifier;
    uint64_t memory[4];
};

static bool ir_reduce_has(const IrFunction* function, uint32_t op)
{
    bool found = false;
    for (uint32_t i = 0; !found && i < function->instruction_count; i += 1)
    {
        found = function->op[i] == op;
    }
    return found;
}

static bool ir_reduce_interpreter_limit(InterpreterStatus status)
{
    const bool result = status == INTERPRETER_RESOURCE || status == INTERPRETER_STEP_LIMIT ||
                        status == INTERPRETER_FRAME_LIMIT || status == INTERPRETER_VALUE_LIMIT;
    return result;
}

// Only checked semantic execution outcomes can establish an optimizer failure. Missing interpreter support,
// unresolved symbols/native bindings, invalid execution and unknown future statuses remain unavailable.
static IrReduceObservationStatus ir_reduce_execution_observation(InterpreterStatus status)
{
    IrReduceObservationStatus result = IR_REDUCE_UNSUPPORTED;
    if (ir_reduce_interpreter_limit(status))
    {
        result = IR_REDUCE_PREDICATE_LIMIT;
    }
    else if (status == INTERPRETER_OK || status == INTERPRETER_UNDEFINED || status == INTERPRETER_MEMORY || status == INTERPRETER_ARITHMETIC)
    {
        result = IR_REDUCE_OBSERVED;
    }
    return result;
}

static InterpreterResult ir_reduce_execute(const IrFunction* function, const IrReduceOracle* oracle, const IrReduceLimits* limits,
                                           uint64_t* memory, Arena* scratch)
{
    const IrInterpreterFunction entry = { .symbol = 1, .function = function };
    const InterpreterBinding global = { .symbol = 2, .bytes = (uint8_t*)memory, .size = sizeof(oracle->memory), .writable = true };
    const IrInterpreterProgram program = { .functions = &entry, .function_count = 1, .bindings = &global, .binding_count = 1 };
    InterpreterOptions options = interpreter_default_options();
    options.max_steps = limits->execution_steps;
    options.max_frames = limits->call_frames;
    return ir_interpret(&program, 1, oracle->arguments, oracle->argument_count, &options, scratch);
}

static IrReduceObservation ir_reduce_oracle(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    const IrReduceOracle* oracle = context;
    IrReduceObservation result = { .status = IR_REDUCE_UNSUPPORTED };
    const uint64_t mark = scratch->length;
    if (oracle != NULL && oracle->argument_count <= BUSTER_ARRAY_LENGTH(oracle->arguments))
    {
        const IrCheck vector = ir_verify_complete(function, scratch, true);
        IrCheck scalar = ir_verify_complete(function, scratch, false);
        if (ir_reduce_resource(vector.error) || ir_reduce_resource(scalar.error))
        {
            result.status = IR_REDUCE_PREDICATE_LIMIT;
        }
        else if (oracle->verifier)
        {
            // Controlled bad-consumer observation, after both authoritative verifiers checked real valid IR.
            if (oracle->injection == IR_REDUCE_INJECT_VERIFY_PHI && scalar.error == IR_OK && ir_reduce_has(function, IR_PHI))
            {
                scalar.error = IR_ERROR_PHI;
            }
            result.status = IR_REDUCE_OBSERVED;
            if (vector.error != scalar.error)
            {
                result.failure = (IrReduceFailure){ .kind = IR_REDUCE_FAILURE_VERIFY, .detail = (uint32_t)vector.error << 16 | scalar.error };
            }
        }
        else if (vector.error == IR_OK && scalar.error == IR_OK && oracle->argument_count == function->parameter_count)
        {
            const uint64_t available = scratch->capacity - scratch->length;
            const uint64_t half = (available / 2) & ~UINT64_C(63);
            uint8_t* bytes = arena_push(scratch, half * 2, 64);
            if (bytes != NULL && half >= 4096)
            {
                Arena optimized_storage = { .base = bytes, .capacity = half };
                Arena execution = { .base = bytes + half, .capacity = half };
                uint64_t expected_memory[4];
                memcpy(expected_memory, oracle->memory, sizeof(expected_memory));
                const InterpreterResult expected = ir_reduce_execute(function, oracle, limits, expected_memory, &execution);
                result.status = ir_reduce_interpreter_limit(expected.status) ? IR_REDUCE_PREDICATE_LIMIT : IR_REDUCE_UNSUPPORTED;
                if (expected.status == INTERPRETER_OK)
                {
                    result.status = IR_REDUCE_OBSERVED;
                    for (uint32_t path = 0; result.status == IR_REDUCE_OBSERVED && result.failure.kind == 0 && path < 2; path += 1)
                    {
                        IrOptimizeOptions options = ir_optimize_options();
                        options.passes = oracle->passes;
                        options.scalar = path != 0;
                        IrOptimizeResult optimized = { 0 };
                        const IrCheck check = ir_optimize(function, &optimized_storage, &execution, options, &optimized);
                        if (check.error != IR_OK)
                        {
                            result.status = check.error == IR_ERROR_MEMORY || check.error == IR_ERROR_LIMIT ?
                                            IR_REDUCE_PREDICATE_LIMIT : IR_REDUCE_UNSUPPORTED;
                        }
                        else
                        {
                            const IrCheck optimized_vector = ir_verify_complete(&optimized.function, &execution, true);
                            const IrCheck optimized_scalar = ir_verify_complete(&optimized.function, &execution, false);
                            if (ir_reduce_resource(optimized_vector.error) || ir_reduce_resource(optimized_scalar.error))
                            {
                                result.status = IR_REDUCE_PREDICATE_LIMIT;
                            }
                            else if (optimized_vector.error != IR_OK || optimized_scalar.error != IR_OK)
                            {
                                result.failure = (IrReduceFailure){ .kind = IR_REDUCE_FAILURE_VERIFY, .pass = oracle->passes,
                                    .detail = (uint32_t)optimized_vector.error << 16 | optimized_scalar.error };
                            }
                            else
                            {
                                uint64_t actual_memory[4];
                                memcpy(actual_memory, oracle->memory, sizeof(actual_memory));
                                InterpreterResult actual = ir_reduce_execute(&optimized.function, oracle, limits, actual_memory, &execution);
                                const IrReduceObservationStatus observation = ir_reduce_execution_observation(actual.status);
                                if (observation != IR_REDUCE_OBSERVED)
                                {
                                    result.status = observation;
                                }
                                else if (actual.status != INTERPRETER_OK)
                                {
                                    result.failure = (IrReduceFailure){ .kind = IR_REDUCE_FAILURE_EXECUTION, .pass = oracle->passes,
                                        .detail = (uint32_t)actual.status << 1 | path };
                                }
                                else
                                {
                                    if (oracle->injection == IR_REDUCE_INJECT_RESULT_PHI && ir_reduce_has(function, IR_PHI))
                                    {
                                        actual.value.low ^= 1;
                                    }
                                    if (oracle->injection == IR_REDUCE_INJECT_MEMORY_STORE && ir_reduce_has(function, IR_STORE))
                                    {
                                        actual_memory[0] ^= 1;
                                    }
                                    const bool value = actual.value.low == expected.value.low && actual.value.high == expected.value.high;
                                    const bool memory = memcmp(actual_memory, expected_memory, sizeof(actual_memory)) == 0;
                                    if (!value || !memory)
                                    {
                                        result.failure = (IrReduceFailure){ .kind = !value ? IR_REDUCE_FAILURE_RESULT : IR_REDUCE_FAILURE_MEMORY,
                                            .pass = oracle->passes, .detail = path };
                                    }
                                }
                            }
                        }
                        arena_pop_to(&optimized_storage, 0);
                    }
                }
            }
            else
            {
                result.status = IR_REDUCE_PREDICATE_LIMIT;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}
