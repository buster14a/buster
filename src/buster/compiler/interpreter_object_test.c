// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Frame-owned typed objects (#176). Included by interpreter_test.c: exact sizes and alignment, representation
// copies, scalar views, failed frame entry, and bounded repeated calls are independent of the native compiler.

static void interpreter_test_object_storage(InterpreterTestTally* tally, Arena* scratch)
{
    uint8_t kinds[] = {
        BUSTER_TYPED_CONST,
        BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_LOCAL_ADDRESS,
        BUSTER_TYPED_COPY,
        BUSTER_TYPED_LOCAL,
        BUSTER_TYPED_RETURN,
    };
    uint8_t classes[] = {
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U8,
        BUSTER_TYPED_CLASS_U8,
    };
    uint64_t values[] = { 0x5a, 0, 1, 0, 17, 1, 0 };
    uint8_t locals[] = { BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_VOID };
    uint8_t flags[] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    uint32_t sizes[] = { 17, 17 };
    uint8_t alignments[] = { 7, 6 };
    Tier0Function function = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = BUSTER_ARRAY_LENGTH(kinds),
        .local_classes = locals,
        .local_flags = flags,
        .local_count = BUSTER_ARRAY_LENGTH(locals),
        .local_sizes = sizes,
        .local_alignments = alignments,
    };
    InterpreterResult result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0x5a, "objects: COPY transfers a defined byte without consuming undefined padding");
    classes[5] = BUSTER_TYPED_CLASS_U64;
    classes[6] = BUSTER_TYPED_CLASS_U64;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_UNDEFINED && result.entry == 5, "objects: copied padding remains undefined to a scalar view");
    classes[0] = BUSTER_TYPED_CLASS_U64;
    classes[1] = BUSTER_TYPED_CLASS_U64;
    values[0] = UINT64_C(0x123456789abcdef0);
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == values[0], "objects: scalar local stores and loads use the representation, not its address cell");
    values[4] = 18;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_MEMORY && result.entry == 4, "objects: COPY cannot consume alignment padding past the exact extent");
    values[4] = 17;
    sizes[1] = 7;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_MEMORY, "objects: COPY checks a smaller destination before writing");
    sizes[1] = 17;
    const uint8_t alignment_kinds[] = { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CONST, BUSTER_TYPED_AND, BUSTER_TYPED_RETURN };
    const uint8_t alignment_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    uint64_t alignment_values[] = { 0, 127, 0, 0 };
    function.kinds = alignment_kinds;
    function.classes = alignment_classes;
    function.values = alignment_values;
    function.count = BUSTER_ARRAY_LENGTH(alignment_kinds);
    for (uint32_t power = 0; power <= 12; power += 1)
    {
        alignments[0] = (uint8_t)power;
        alignment_values[1] = (UINT64_C(1) << power) - 1;
        result = interpreter_test_run(&function, scratch, 0);
        interpreter_test_expect(tally, result.status == INTERPRETER_OK && result.value.low == 0, "objects: requested power-of-two alignment");
    }
    alignments[0] = BUSTER_TIER0_MAX_OBJECT_ALIGNMENT_LOG2 + 1;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: excessive alignment refused before shifting");
    alignments[0] = 0;
    sizes[0] = BUSTER_TIER0_MAX_OBJECT_BYTES + 1;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: excessive extent refused before allocating");
    sizes[0] = BUSTER_TIER0_MAX_OBJECT_BYTES;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: combined storage cannot exceed the frame bound");
    sizes[0] = 17;
    function.local_alignments = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: missing alignment column refused");
    function.local_alignments = alignments;
    function.local_flags = 0;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: missing address-taken flag refused");
    function.local_flags = flags;
    locals[0] = BUSTER_TYPED_CLASS_U64;
    result = interpreter_test_run(&function, scratch, 0);
    interpreter_test_expect(tally, result.status == INTERPRETER_INVALID, "objects: value class with object size refused");
    locals[0] = BUSTER_TYPED_CLASS_VOID;
    function.parameter_count = 1;
    interpreter_test_expect(tally, !interpreter_typed_function_valid(&function), "objects: by-value parameters remain a distinct unsupported contract");
    function.parameter_count = 0;
    InterpreterOptions limits = interpreter_default_options();
    limits.max_memories = 1;
    result = interpreter_test_run(&function, scratch, &limits);
    interpreter_test_expect(tally, result.status == INTERPRETER_RESOURCE, "objects: registration exhaustion restores scratch");
    limits.max_frames = 2;
    limits.max_values = 8;
    limits.max_locals = 2;
    limits.max_memories = 2;
    Arena small = arena_reserve(4096);
    sizes[1] = 8192;
    const uint64_t prefix = 37;
    uint8_t* saved = arena_push(&small, prefix, 1);
    if (saved != 0)
    {
        memset(saved, 0xa5, prefix);
    }
    result = interpreter_test_run(&function, &small, &limits);
    bool preserved = saved != 0 && small.length == prefix;
    for (uint32_t i = 0; preserved && i < prefix; i += 1)
    {
        preserved = saved[i] == 0xa5;
    }
    interpreter_test_expect(tally, result.status == INTERPRETER_RESOURCE && preserved, "objects: partial entry allocation failure preserves caller arena prefix");
    arena_release(&small);
}

static void interpreter_test_object_frames(InterpreterTestTally* tally)
{
    // Two 32KiB objects fit, but retaining a callee's arena storage across 64 sequential calls does not. The caller
    // passes its object's address; each callee copies it into a private object and writes there, never into its caller.
    const uint8_t caller_kinds[] = {
        BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CALL, BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_CONST, BUSTER_TYPED_SUB, BUSTER_TYPED_STORE_LOCAL, BUSTER_TYPED_BRANCH_TRUE,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN,
    };
    const uint8_t caller_classes[] = {
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    const uint64_t caller_values[] = { 0x1234, 0, 64, 1, 0, 0, 0, 1, 1, 0, 1, 4, 0, 0 };
    const uint8_t caller_locals[] = { BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_U64 };
    const uint8_t caller_flags[] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, 0 };
    const uint32_t caller_sizes[] = { 32768, 0 };
    const uint8_t caller_alignments[] = { 7, 0 };
    const SymbolId calls[] = { 2 };
    const uint32_t first[] = { 0 };
    const uint32_t counts[] = { 1 };
    const uint8_t variadic[] = { 0 };
    const uint8_t arguments[] = { BUSTER_TYPED_CLASS_U64 };
    const uint8_t callee_kinds[] = {
        BUSTER_TYPED_STACK_SAVE, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_CONST, BUSTER_TYPED_ALLOCA, BUSTER_TYPED_DISCARD,
        BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_LOCAL, BUSTER_TYPED_COPY,
        BUSTER_TYPED_CONST, BUSTER_TYPED_SET_LOCAL,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_STACK_RESTORE,
        BUSTER_TYPED_LOCAL, BUSTER_TYPED_RETURN,
    };
    const uint8_t callee_classes[] = {
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
        BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64,
    };
    const uint64_t callee_values[] = { 0, 2, 64, 0, 0, 1, 0, 32768, 0x5678, 1, 2, 0, 1, 0 };
    const uint8_t callee_locals[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_VOID, BUSTER_TYPED_CLASS_U64 };
    const uint8_t callee_flags[] = { 0, BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, 0 };
    const uint32_t callee_sizes[] = { 0, 32768, 0 };
    const uint8_t callee_alignments[] = { 0, 7, 0 };
    Tier0Function functions[] = {
        {
            .kinds = caller_kinds,
            .classes = caller_classes,
            .values = caller_values,
            .count = BUSTER_ARRAY_LENGTH(caller_kinds),
            .local_classes = caller_locals,
            .local_flags = caller_flags,
            .local_count = BUSTER_ARRAY_LENGTH(caller_locals),
            .local_sizes = caller_sizes,
            .local_alignments = caller_alignments,
            .call_targets = calls,
            .call_first = first,
            .call_argument_counts = counts,
            .call_variadic = variadic,
            .call_count = 1,
            .argument_classes = arguments,
            .argument_count = 1,
        },
        {
            .kinds = callee_kinds,
            .classes = callee_classes,
            .values = callee_values,
            .count = BUSTER_ARRAY_LENGTH(callee_kinds),
            .local_classes = callee_locals,
            .local_flags = callee_flags,
            .local_count = BUSTER_ARRAY_LENGTH(callee_locals),
            .parameter_count = 1,
            .local_sizes = callee_sizes,
            .local_alignments = callee_alignments,
        },
    };
    const SymbolId symbols[] = { 1, 2 };
    const InterpreterProgram program = {
        .functions = functions,
        .function_symbols = symbols,
        .function_count = BUSTER_ARRAY_LENGTH(functions),
    };
    InterpreterOptions limits = interpreter_default_options();
    limits.max_frames = 4;
    limits.max_values = 16;
    limits.max_locals = 8;
    limits.max_memories = 8;
    Arena scratch = arena_reserve(256 * 1024);
    InterpreterResult result;
    bool ran = interpreter_typed_run(&program, 1, 0, 0, &scratch, &limits, &result);
    interpreter_test_expect(tally, ran && result.status == INTERPRETER_OK && result.value.low == 0x1234 && scratch.length == 0,
                            "objects: calls reclaim storage, keep caller bytes and survive dynamic-stack restore");
    // Returning an object's address cannot extend its frame lifetime. No intervening allocation may reuse it here.
    const uint8_t escaped_kinds[] = { BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_RETURN };
    const uint8_t escaped_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    const uint64_t escaped_values[] = { 1, 0 };
    functions[1].kinds = escaped_kinds;
    functions[1].classes = escaped_classes;
    functions[1].values = escaped_values;
    functions[1].count = BUSTER_ARRAY_LENGTH(escaped_kinds);
    uint8_t use_kinds[BUSTER_ARRAY_LENGTH(caller_kinds)];
    memcpy(use_kinds, caller_kinds, sizeof(use_kinds));
    use_kinds[6] = BUSTER_TYPED_LOAD;
    functions[0].kinds = use_kinds;
    ran = interpreter_typed_run(&program, 1, 0, 0, &scratch, &limits, &result);
    interpreter_test_expect(tally, !ran && result.status == INTERPRETER_MEMORY && result.entry == 6 && scratch.length == 0,
                            "objects: callee return unregisters its object before the caller can read it");
    arena_release(&scratch);
}

static void interpreter_test_objects(InterpreterTestTally* tally, Arena* scratch)
{
    interpreter_test_object_storage(tally, scratch);
    interpreter_test_object_frames(tally);
}
