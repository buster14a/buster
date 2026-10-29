// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original structural and fault-injection fixtures. Every accepted reduction is independently reparsed and
// re-observed; source/arena sentinels cover rejected plans, budget exits and returned predicate faults.

static const char ir_reduce_test_phi[] =
    "function (i64) -> i64 {\n"
    "  slot $0 size=64 align=16 type=void flags=0x00\n"
    "  slot $1 size=32 align=8 type=void flags=0x00\n"
    "b0:\n"
    "  %0 = param.i64 0\n"
    "  %1 = const.i64 0x00000000000000000000000000000000\n"
    "  %2 = const.i64 0x00000000000000000000000012345678\n"
    "  %3 = add.i64 %2, %2\n"
    "  %4 = xor.i64 %3, %2\n"
    "  %5 = icmp.i1 %0, %1 eq\n"
    "  %6 = cbr.void %5, b1, b2\n"
    "b1:\n"
    "  %7 = const.i64 0x00000000000000000000000000000007\n"
    "  %8 = add.i64 %7, %7\n"
    "  %9 = mul.i64 %8, %8\n"
    "  %10 = br.void b3\n"
    "b2:\n"
    "  %11 = const.i64 0x00000000000000000000000000000009\n"
    "  %12 = xor.i64 %11, %11\n"
    "  %13 = br.void b3\n"
    "b3:\n"
    "  %14 = phi.i64 [%7, b1], [%11, b2]\n"
    "  %15 = ret.void %14\n"
    "b4:\n"
    "  %16 = const.i64 0x00000000000000000000000000000456\n"
    "  %17 = add.i64 %16, %16\n"
    "  %18 = br.void b5\n"
    "b5:\n"
    "  %19 = phi.i64 [%17, b4]\n"
    "  %20 = ret.void %19\n"
    "}\n";

static const char ir_reduce_test_memory[] =
    "function (i64) -> i64 {\n"
    "  slot $0 size=128 align=16 type=void flags=0x00\n"
    "b0:\n"
    "  %0 = param.i64 0\n"
    "  %1 = symbol_addr.ptr @2\n"
    "  %2 = const.i64 0x00000000000000000000000000000009\n"
    "  %3 = const.i64 0x000000000000000000000000000fffff\n"
    "  %4 = mul.i64 %3, %3\n"
    "  %5 = add.i64 %0, %2\n"
    "  %6 = store.void %1, %5 align=8 volatile\n"
    "  %7 = xor.i64 %4, %3\n"
    "  %8 = load.i64 %1 align=8 volatile\n"
    "  %9 = ret.void %8\n"
    "b1:\n"
    "  %10 = symbol_addr.ptr @2\n"
    "  %11 = const.i64 0x000000000000000000000000000000ab\n"
    "  %12 = store.void %10, %11 align=8 volatile\n"
    "  %13 = ret.void %11\n"
    "}\n";

static const char ir_reduce_test_call[] =
    "function (i64) -> i64 {\n"
    "  slot $0 size=8 align=8 type=i64 flags=0x00\n"
    "b0:\n"
    "  %0 = param.i64 0\n"
    "  %1 = call.i64 @3(%0) fixed=(i64) variadic=0\n"
    "  %2 = ret.void %1\n"
    "}\n";

// Function metadata the rebuild doesn't remap, each of which a function may carry with only rows it takes: a label
// (in a function without br_indirect), a variadic definition's incoming frame, and by-value aggregates' ABI records.
static const char* const ir_reduce_test_metadata_inputs[] = {
    "function () -> i32 {\n"
    "  label #0 b1\n"
    "b0:\n"
    "  %0 = br.void b1\n"
    "b1:\n"
    "  %1 = const.i32 0x0000000000000000000000000000000a\n"
    "  %2 = ret.void %1\n"
    "}\n",
    "function (ptr, i64) -> ptr {\n"
    "  variadic call(8,4,2,0,ret(1,0,8,0,2,255,255))\n"
    "  slot $0 size=176 align=16 type=void flags=0x09\n"
    "  slot $1 size=0 align=16 type=void flags=0x11\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = param.i64 1\n"
    "  %2 = stack_addr.ptr $0\n"
    "  %3 = stack_addr.ptr $1\n"
    "  %4 = ret.void %0\n"
    "}\n",
    "function (ptr, i64) -> ptr {\n"
    "  result size=12 ret(1,0,8,0,2,255,255) passed=#0\n"
    "  passed #0 size=12 loc(1,7,6,0,0)\n"
    "  passed #1 size=0 loc(1,2,255,0,0)\n"
    "  passed #2 size=16 loc(2,255,255,0,0)\n"
    "  abi #0 call(16,4,0,0,ret(1,0,8,0,2,255,255)) passed=#2 result=12\n"
    "  slot $0 size=12 align=4 type=void flags=0x01\n"
    "b0:\n"
    "  %0 = param.ptr 0\n"
    "  %1 = param.i64 1\n"
    "  %2 = stack_addr.ptr $0\n"
    "  %3 = call.void @1(%0) fixed=(ptr) variadic=0 abi=#0 into=%2\n"
    "  %4 = ret.void %2\n"
    "}\n",
};

typedef struct IrReduceTest IrReduceTest;
struct IrReduceTest
{
    uint64_t checks;
    uint64_t failures;
    uint64_t random;
};

static void ir_reduce_test_expect(IrReduceTest* test, bool valid, const char* description)
{
    test->checks += 1;
    if (!valid)
    {
        if (test->failures < 16)
        {
            fprintf(stderr, "ir-reduce: %s\n", description);
        }
        test->failures += 1;
    }
}

static bool ir_reduce_test_parse(const char* text, Arena* output, Arena* scratch, IrFunction* function)
{
    const uint64_t length = strlen(text);
    const IrParseResult parsed = ir_parse((String8){ .pointer = (char*)text, .length = length }, output, scratch, function);
    const bool valid = parsed.error == IR_PARSE_OK && parsed.consumed == length;
    return valid;
}

static bool ir_reduce_test_roundtrip(const IrFunction* function, Arena* output, Arena* scratch, IrFunction* result)
{
    FILE* file = tmpfile();
    bool valid = file != NULL && ir_print(function, scratch, file) == IR_OK && fseek(file, 0, SEEK_END) == 0;
    const long size = valid ? ftell(file) : -1;
    Arena source = arena_reserve(size >= 0 ? (uint64_t)size + 4096 : 0);
    char* text = source.base != NULL && size >= 0 ? arena_push(&source, (uint64_t)size, 1) : NULL;
    valid = valid && text != NULL && fseek(file, 0, SEEK_SET) == 0 && fread(text, 1, (uint64_t)size, file) == (uint64_t)size;
    if (valid)
    {
        const IrParseResult parsed = ir_parse_scalar((String8){ .pointer = text, .length = (uint64_t)size }, output, scratch, result);
        valid = parsed.error == IR_PARSE_OK && parsed.consumed == (uint64_t)size;
    }
    if (file != NULL)
    {
        valid = fclose(file) == 0 && valid;
    }
    arena_release(&source);
    return valid;
}

static void ir_reduce_test_fixture(IrReduceTest* test, const char* text, IrReduceInjection injection, bool verifier)
{
    Arena input = arena_reserve(UINT64_C(1) << 22);
    Arena output = arena_reserve(UINT64_C(1) << 24);
    Arena twin = arena_reserve(UINT64_C(1) << 24);
    Arena scratch = arena_reserve(UINT64_C(1) << 27);
    IrFunction function = { 0 };
    bool valid = input.base != NULL && output.base != NULL && twin.base != NULL && scratch.base != NULL &&
                 ir_reduce_test_parse(text, &input, &scratch, &function);
    ir_reduce_test_expect(test, valid, "nontrivial original CFG/phi or memory fixture parses completely");
    if (valid)
    {
        uint8_t* prefix = arena_push(&scratch, 23, 1);
        memset(prefix, 0xa7, 23);
        const uint64_t mark = scratch.length;
        IrReduceOracle oracle = { .argument_count = 1, .passes = 7, .injection = injection, .verifier = verifier,
                                  .arguments = { { 5, 0 } }, .memory = { 11, 22, 33, 44 } };
        const IrReduceLimits limits = ir_reduce_limits();
        IrReduceResult reduced = { 0 };
        IrReduceResult repeated = { 0 };
        const IrReduceStatus first = ir_reduce(&function, ir_reduce_oracle, &oracle, &limits, &output, &scratch, &reduced);
        const IrReduceStatus second = ir_reduce(&function, ir_reduce_oracle, &oracle, &limits, &twin, &scratch, &repeated);
        uint64_t a_length = 0;
        uint64_t b_length = 0;
        uint64_t a_hash = 0;
        uint64_t b_hash = 0;
        uint8_t* a = first == IR_REDUCE_COMPLETE ? ir_reduce_key(&reduced.function, &scratch, &a_length, &a_hash) : NULL;
        uint8_t* b = second == IR_REDUCE_COMPLETE ? ir_reduce_key(&repeated.function, &scratch, &b_length, &b_hash) : NULL;
        ir_reduce_test_expect(test, first == IR_REDUCE_COMPLETE && second == first && reduced.failure.kind != 0 &&
                              ir_reduce_same_failure(reduced.failure, repeated.failure) && a != NULL && b != NULL &&
                              a_length == b_length && a_hash == b_hash && memcmp(a, b, a_length) == 0,
                              "equal inputs, predicate and limits reproduce byte-identical fields and classified status");
        ir_reduce_test_expect(test, first == IR_REDUCE_COMPLETE && reduced.function.instruction_count * 3 <= function.instruction_count * 2 &&
                              reduced.function.block_count < function.block_count && reduced.accepted >= 2,
                              "CFG/phi and memory reproducers lose substantial dead rows, blocks and metadata");
        arena_pop_to(&scratch, mark);
        IrFunction reparsed = { 0 };
        const bool roundtrip = first == IR_REDUCE_COMPLETE && ir_reduce_test_roundtrip(&reduced.function, &twin, &scratch, &reparsed);
        ir_reduce_test_expect(test, roundtrip && ir_verify_complete(&reparsed, &scratch, true).error == IR_OK &&
                              ir_verify_complete(&reparsed, &scratch, false).error == IR_OK,
                              "reduced canonical text reparses and passes both complete SSA verifiers independently");
        const IrReduceObservation replay = roundtrip ? ir_reduce_oracle(&reparsed, &limits, &oracle, &scratch) :
                                                     (IrReduceObservation){ .status = IR_REDUCE_PREDICATE_ERROR };
        ir_reduce_test_expect(test, replay.status == IR_REDUCE_OBSERVED && ir_reduce_same_failure(replay.failure, reduced.failure),
                              "reparsed independent execution reproduces the same failure class and pass");
        ir_reduce_test_expect(test, scratch.length == mark && prefix[0] == 0xa7 && prefix[22] == 0xa7 &&
                              function.instruction_count == (injection == IR_REDUCE_INJECT_MEMORY_STORE ? 14 : 21),
                              "reduction/replay preserve caller scratch prefix and immutable original function");
        if (injection == IR_REDUCE_INJECT_MEMORY_STORE && first == IR_REDUCE_COMPLETE)
        {
            uint32_t stores = 0;
            uint32_t volatile_transfers = 0;
            for (uint32_t i = 0; i < reduced.function.instruction_count; i += 1)
            {
                stores += reduced.function.op[i] == IR_STORE;
                volatile_transfers += (reduced.function.flags[i] & BUSTER_IR_VOLATILE) != 0;
            }
            ir_reduce_test_expect(test, stores == 1 && volatile_transfers == 2,
                                  "reachable store and ordered volatile load survive while unreachable effects may be removed");
        }
    }
    arena_release(&scratch);
    arena_release(&twin);
    arena_release(&output);
    arena_release(&input);
}

static IrReduceObservation ir_reduce_test_predicate(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    (void)limits;
    (void)scratch;
    const uint32_t mode = *(uint32_t*)context;
    IrReduceObservation result = { .status = IR_REDUCE_OBSERVED, .failure = { .kind = 71, .pass = 3, .detail = 5 } };
    if (mode == 1)
    {
        result.status = IR_REDUCE_PREDICATE_FAULT;
    }
    else if (mode == 2)
    {
        result.status = IR_REDUCE_PREDICATE_LIMIT;
    }
    else if (mode == 3)
    {
        result.status = IR_REDUCE_UNSUPPORTED;
    }
    else if (mode >= 10 && function->instruction_count < 3)
    {
        result.failure.kind += mode == 10;
        result.failure.pass += mode == 11;
        result.failure.detail += mode == 12;
    }
    return result;
}

static void ir_reduce_test_contracts(IrReduceTest* test)
{
    const char text[] = "function () -> i64 {\nb0:\n  %0 = const.i64 0x00000000000000000000000000000007\n"
                        "  %1 = const.i64 0x00000000000000000000000000000008\n  %2 = ret.void %0\n}\n";
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 27);
    IrFunction function = { 0 };
    const bool parsed = ir_reduce_test_parse(text, &input, &scratch, &function);
    ir_reduce_test_expect(test, parsed, "contract fixture parses");
    for (uint32_t mode = 10; parsed && mode <= 12; mode += 1)
    {
        IrReduceLimits limits = ir_reduce_limits();
        IrReduceResult result = { 0 };
        const IrReduceStatus status = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &result);
        ir_reduce_test_expect(test, status == IR_REDUCE_COMPLETE && result.function.instruction_count == 3 && result.different != 0 &&
                              result.failure.kind == 71 && result.failure.pass == 3 && result.failure.detail == 5,
                              "different failure kind, pass or category cannot replace the requested failure");
        arena_pop_to(&output, 0);
    }
    for (uint32_t mode = 1; parsed && mode <= 3; mode += 1)
    {
        IrReduceLimits limits = ir_reduce_limits();
        limits.predicate_milliseconds = 20;
        limits.max_milliseconds = 2000;
        IrReduceResult untouched;
        memset(&untouched, 0x5a, sizeof(untouched));
        const IrReduceResult before = untouched;
        const uint64_t started = ir_reduce_milliseconds();
        const IrReduceStatus status = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &untouched);
        ir_reduce_test_expect(test, status == IR_REDUCE_ORACLE_UNAVAILABLE && memcmp(&untouched, &before, sizeof(before)) == 0 &&
                              output.length == 0 && scratch.length == 0 && ir_reduce_milliseconds() - started < 1500,
                              "returned predicate fault, limit or unsupported status publishes no replacement IR");
    }
    if (parsed)
    {
        uint32_t mode = 0;
        IrReduceLimits limits = ir_reduce_limits();
        limits.max_candidates = 1;
        IrReduceResult bounded = { 0 };
        const IrReduceStatus status = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &bounded);
        ir_reduce_test_expect(test, status == IR_REDUCE_CANDIDATE_LIMIT && bounded.candidates == 1 &&
                              ir_verify_complete(&bounded.function, &scratch, true).error == IR_OK,
                              "candidate budget returns a verified explicitly partial reproducer");
        arena_pop_to(&output, 0);
        Arena denied = output;
        denied.capacity = 1;
        IrReduceResult untouched;
        memset(&untouched, 0x34, sizeof(untouched));
        const IrReduceResult before = untouched;
        const IrReduceStatus failed = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &denied, &scratch, &untouched);
        ir_reduce_test_expect(test, failed == IR_REDUCE_RESOURCE && denied.length == 0 && scratch.length == 0 &&
                              memcmp(&untouched, &before, sizeof(before)) == 0,
                              "final output allocation failure rolls back the whole unpublished reduction");
        const uint8_t old = function.op[0];
        function.op[0] = IR_INVALID;
        const IrReduceStatus invalid = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &untouched);
        ir_reduce_test_expect(test, invalid == IR_REDUCE_INVALID && output.length == 0 && scratch.length == 0 &&
                              memcmp(&untouched, &before, sizeof(before)) == 0,
                              "malformed input never reaches the predicate or publishes partial IR");
        function.op[0] = old;
        Arena same_backing = output;
        const IrReduceStatus overlap = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &same_backing, &untouched);
        ir_reduce_test_expect(test, overlap == IR_REDUCE_INVALID && output.length == 0 && same_backing.length == 0 &&
                              memcmp(&untouched, &before, sizeof(before)) == 0,
                              "distinct arena descriptors with overlapping writable backing reject before allocation");
        for (uint32_t owner = 0; owner < 2; owner += 1)
        {
            Arena* borrowed = owner == 0 ? &output : &scratch;
            uint8_t* bytes = arena_push(borrowed, function.instruction_count, 1);
            memcpy(bytes, function.op, function.instruction_count);
            arena_pop_to(borrowed, 0);
            IrFunction aliased = function;
            aliased.op = bytes;
            const IrReduceStatus alias_status = ir_reduce(&aliased, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &untouched);
            ir_reduce_test_expect(test, alias_status == IR_REDUCE_INVALID && output.length == 0 && scratch.length == 0 &&
                                  memcmp(bytes, function.op, function.instruction_count) == 0 &&
                                  memcmp(&untouched, &before, sizeof(before)) == 0,
                                  "borrowed source columns in either writable tail reject without clobbering bytes");
        }
        IrReduceLimits* borrowed_limits = BUSTER_ARENA_PUSH_ARRAY(&scratch, IrReduceLimits, 1);
        *borrowed_limits = limits;
        arena_pop_to(&scratch, 0);
        const IrReduceStatus borrowed_status = ir_reduce(&function, ir_reduce_test_predicate, &mode, borrowed_limits, &output, &scratch, &untouched);
        ir_reduce_test_expect(test, borrowed_status == IR_REDUCE_INVALID && output.length == 0 && scratch.length == 0 &&
                              memcmp(borrowed_limits, &limits, sizeof(limits)) == 0 &&
                              memcmp(&untouched, &before, sizeof(before)) == 0,
                              "limits borrowed from a writable tail reject before verifier scratch can overwrite them");
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void ir_reduce_test_rebuild(IrReduceTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 22);
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    IrFunction source = { 0 };
    bool valid = ir_reduce_test_parse(ir_reduce_test_call, &input, &scratch, &source);
    IrFunction rebuilt = { 0 };
    const IrCheck cloned = valid ? ir_rebuild(&source, NULL, &output, &scratch, &rebuilt) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
    ir_reduce_test_expect(test, cloned.error == IR_OK && rebuilt.call_count == 1 && rebuilt.call_type_count == 1 &&
                          rebuilt.extra_count == 1 && rebuilt.extra[0] == 0 && rebuilt.calls[0].argument_first == 0 &&
                          rebuilt.calls[0].parameter_count == 1 && rebuilt.call_types[0] == IR_I64 && rebuilt.slot_count == 0,
                          "call argument/signature slices are rebuilt and only referenced slots survive");
    arena_pop_to(&output, 0);
    const uint32_t blocks[] = { 0 };
    const uint32_t duplicate[] = { 0, 0, 2 };
    const uint32_t offsets[] = { 0, 3 };
    const IrRebuildPlan bad = { .blocks = blocks, .block_count = 1, .rows = duplicate, .offsets = offsets, .instruction_count = 3 };
    IrFunction untouched;
    memset(&untouched, 0x6c, sizeof(untouched));
    const IrFunction before = untouched;
    const IrCheck rejected = valid ? ir_rebuild(&source, &bad, &output, &scratch, &untouched) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
    ir_reduce_test_expect(test, rejected.error != IR_OK && output.length == 0 && scratch.length == 0 &&
                          memcmp(&untouched, &before, sizeof(before)) == 0, "duplicate mapped row is rejected transactionally");
    arena_pop_to(&input, 0);
    valid = ir_reduce_test_parse(ir_reduce_test_phi, &input, &scratch, &source);
    const uint32_t order[] = { 0, 2, 1, 3, 4, 5 };
    const uint32_t rows[] = { 0, 1, 2, 3, 4, 5, 6, 11, 12, 13, 7, 8, 9, 10, 14, 15, 16, 17, 18, 19, 20 };
    const uint32_t edges[] = { 0, 7, 10, 14, 16, 19, 21 };
    const IrRebuildPlan permute = { .blocks = order, .block_count = 6, .rows = rows, .offsets = edges, .instruction_count = 21 };
    const IrCheck permuted = valid ? ir_rebuild(&source, &permute, &output, &scratch, &rebuilt) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
    ir_reduce_test_expect(test, permuted.error == IR_OK && rebuilt.b[6] == 2 && rebuilt.c[6] == 1 &&
                          rebuilt.extra[rebuilt.a[14]] == 2 && rebuilt.extra[rebuilt.a[14] + 2] == 1,
                          "nontrivial block/value permutation repairs branches and phi predecessor/value pairs");
    arena_pop_to(&output, 0);
    Arena denied = output;
    denied.capacity = 8;
    const IrCheck exhausted = valid ? ir_rebuild(&source, NULL, &denied, &scratch, &untouched) : (IrCheck){ .error = IR_ERROR_STRUCTURE };
    ir_reduce_test_expect(test, exhausted.error == IR_ERROR_MEMORY && denied.length == 0 && scratch.length == 0 &&
                          memcmp(&untouched, &before, sizeof(before)) == 0,
                          "rebuild constructor exhaustion restores both arenas and the unpublished result");
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static IrReduceObservation ir_reduce_test_counting(const IrFunction* function, const IrReduceLimits* limits, void* context, Arena* scratch)
{
    (void)function;
    (void)limits;
    (void)scratch;
    *(uint32_t*)context += 1;
    return (IrReduceObservation){ .status = IR_REDUCE_OBSERVED, .failure = { .kind = 71, .pass = 3, .detail = 5 } };
}

// #733's review: a function carrying labels, a variadic frame or ABI records verifies, but the rebuild and the reducer
// must refuse it, publishing nothing, rather than hand back (or show the predicate) a function without them.
static void ir_reduce_test_metadata(IrReduceTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 20);
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 24);
    for (uint32_t k = 0; k < BUSTER_ARRAY_LENGTH(ir_reduce_test_metadata_inputs); k += 1)
    {
        IrFunction source = { 0 };
        const bool parsed = ir_reduce_test_parse(ir_reduce_test_metadata_inputs[k], &input, &scratch, &source);
        arena_pop_to(&scratch, 0);
        const bool verified = parsed && ir_verify_complete(&source, &scratch, true).error == IR_OK &&
                              ir_verify_complete(&source, &scratch, false).error == IR_OK;
        arena_pop_to(&scratch, 0);
        const bool carried = k == 0 ? source.label_count == 1 : k == 1 ? source.variadic : source.parameter_passed != 0 &&
                             source.result_size == 12 && source.abi_call_count == 1 && source.calls[0].abi == 1;
        ir_reduce_test_expect(test, verified && carried, "metadata fixture parses and passes both verifiers with its metadata");
        IrFunction untouched;
        memset(&untouched, 0x3c, sizeof(untouched));
        const IrFunction before = untouched;
        const IrCheck rebuilt = verified ? ir_rebuild(&source, NULL, &output, &scratch, &untouched) : (IrCheck){ .error = IR_OK };
        ir_reduce_test_expect(test, rebuilt.error == IR_ERROR_METADATA && output.length == 0 && scratch.length == 0 &&
                              memcmp(&untouched, &before, sizeof(before)) == 0,
                              "identity rebuild refuses labels, a variadic frame or ABI records instead of dropping them");
        uint32_t calls = 0;
        IrReduceResult unpublished;
        memset(&unpublished, 0x3c, sizeof(unpublished));
        const IrReduceResult unchanged = unpublished;
        const IrReduceLimits limits = ir_reduce_limits();
        const IrReduceStatus status = verified ? ir_reduce(&source, ir_reduce_test_counting, &calls, &limits, &output, &scratch, &unpublished) :
                                                 IR_REDUCE_COMPLETE;
        ir_reduce_test_expect(test, status == IR_REDUCE_INVALID && calls == 0 && output.length == 0 && scratch.length == 0 &&
                              memcmp(&unpublished, &unchanged, sizeof(unchanged)) == 0,
                              "reducer refuses unremapped metadata before the predicate sees a function without it");
        arena_pop_to(&input, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

static void ir_reduce_test_cache(IrReduceTest* test)
{
    Arena input = arena_reserve(UINT64_C(1) << 20);
    IrReduceWork work = { .limits = ir_reduce_limits(), .predicate = ir_reduce_test_predicate };
    work.scratch = arena_reserve(UINT64_C(1) << 23);
    work.cache = arena_reserve(UINT64_C(1) << 23);
    uint32_t mode = 0;
    work.context = &mode;
    work.started = ir_reduce_milliseconds();
    IrFunction source = { 0 };
    bool valid = ir_reduce_test_parse(ir_reduce_test_call, &input, &work.scratch, &source);
    uint64_t length = 0;
    uint64_t hash = 0;
    uint8_t* key = valid ? ir_reduce_key(&source, &work.scratch, &length, &hash) : NULL;
    uint8_t* forged = key != NULL ? arena_push(&work.cache, length, 1) : NULL;
    valid = forged != NULL && length != 0;
    IrReduceCache collision = { 0 };
    if (valid)
    {
        memcpy(forged, key, length);
        forged[length - 1] ^= 1;
        collision = (IrReduceCache){ .bytes = forged, .length = length, .hash = hash,
            .observation = { .status = IR_REDUCE_OBSERVED, .failure = { .kind = 999 } } };
        work.first = &collision;
        arena_pop_to(&work.scratch, 0);
        IrReduceObservation observed;
        valid = ir_reduce_observe(&work, &source, &observed) && observed.status == IR_REDUCE_OBSERVED &&
                observed.failure.kind == 71 && work.result.predicates == 1 && work.result.cache_hits == 0;
        ir_reduce_test_expect(test, valid, "a forced equal-hash/equal-length collision requires exact field bytes, not a hash alone");
        valid = ir_reduce_observe(&work, &source, &observed) && observed.failure.kind == 71 &&
                work.result.predicates == 1 && work.result.cache_hits == 1 && work.scratch.length == 0;
        ir_reduce_test_expect(test, valid, "an exactly repeated candidate reuses its classified observation without another callback");
    }
    else
    {
        ir_reduce_test_expect(test, false, "exact cache fixture storage");
    }
    arena_release(&work.cache);
    arena_release(&work.scratch);
    arena_release(&input);
}

static uint64_t ir_reduce_test_random(IrReduceTest* test)
{
    uint64_t value = test->random;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    test->random = value;
    return value;
}

static void ir_reduce_test_generated(IrReduceTest* test, uint64_t count)
{
    Arena input = arena_reserve(UINT64_C(1) << 22);
    Arena output = arena_reserve(UINT64_C(1) << 22);
    Arena scratch = arena_reserve(UINT64_C(1) << 27);
    for (uint64_t sample = 0; sample < count; sample += 1)
    {
        const uint32_t dead = 1 + ir_reduce_test_random(test) % 12;
        const IrSizes sizes = { .instruction_count = dead + 2, .block_count = 1, .constant_count = dead, .parameter_count = 1 };
        IrFunction function = { 0 };
        bool valid = ir_create(&input, &sizes, &function) == IR_OK;
        if (valid)
        {
            function.result_type = IR_I64;
            function.parameter_types[0] = IR_I64;
            function.block_offsets[0] = 0;
            function.block_offsets[1] = dead + 2;
            memset(function.flags, 0, dead + 2);
            memset(function.alignment_log2, 0, dead + 2);
            function.op[0] = IR_PARAM;
            function.type[0] = IR_I64;
            function.a[0] = 0;
            for (uint32_t i = 0; i < dead; i += 1)
            {
                function.op[i + 1] = IR_CONST;
                function.type[i + 1] = IR_I64;
                function.a[i + 1] = i;
                function.constants[i] = (IrConstant){ .low = ir_reduce_test_random(test) };
            }
            function.op[dead + 1] = IR_RET;
            function.type[dead + 1] = IR_VOID;
            function.a[dead + 1] = 0;
            uint32_t mode = 0;
            const IrReduceLimits limits = ir_reduce_limits();
            IrReduceResult result = { 0 };
            const IrReduceStatus status = ir_reduce(&function, ir_reduce_test_predicate, &mode, &limits, &output, &scratch, &result);
            valid = status == IR_REDUCE_COMPLETE && result.function.instruction_count == 2 && result.function.constant_count == 0 &&
                    ir_verify_complete(&result.function, &scratch, true).error == IR_OK &&
                    ir_verify_complete(&result.function, &scratch, false).error == IR_OK;
        }
        ir_reduce_test_expect(test, valid && scratch.length == 0, "random valid IR is compacted without invalid operands or retained scratch");
        arena_pop_to(&input, 0);
        arena_pop_to(&output, 0);
    }
    arena_release(&scratch);
    arena_release(&output);
    arena_release(&input);
}

#include "ir_reduce_ownership_test.c"

static void ir_reduce_test_driver(IrReduceTest* test);

static int run_ir_reduce_tests(int argc, char** argv)
{
    uint64_t seed = 733;
    uint64_t count = 8;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const bool seeded = strcmp(argv[i], "--seed") == 0;
        const bool counted = strcmp(argv[i], "--count") == 0;
        if ((seeded || counted) && i + 1 < argc)
        {
            char* end = NULL;
            errno = 0;
            const uint64_t value = strtoull(argv[++i], &end, 0);
            valid = argv[i][0] != 0 && end != argv[i] && *end == 0 && errno == 0 && (seeded || value <= 1024);
            if (seeded)
            {
                seed = value;
            }
            else
            {
                count = value;
            }
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (valid)
    {
        IrReduceTest test = { .random = seed != 0 ? seed : UINT64_C(0xd153e733) };
        ir_reduce_test_rebuild(&test);
        ir_reduce_test_metadata(&test);
        ir_reduce_test_ownership(&test);
        ir_reduce_test_cache(&test);
        ir_reduce_test_contracts(&test);
        ir_reduce_test_fixture(&test, ir_reduce_test_phi, IR_REDUCE_INJECT_VERIFY_PHI, true);
        ir_reduce_test_fixture(&test, ir_reduce_test_phi, IR_REDUCE_INJECT_RESULT_PHI, false);
        ir_reduce_test_fixture(&test, ir_reduce_test_memory, IR_REDUCE_INJECT_MEMORY_STORE, false);
        ir_reduce_test_generated(&test, count);
        ir_reduce_test_driver(&test);
        printf("ir-reduce: %" PRIu64 " checks, %" PRIu64 " failures, seed=%" PRIu64 " generated=%" PRIu64 "\n", test.checks, test.failures, seed, count);
        result = test.failures != 0;
    }
    else
    {
        fprintf(stderr, "usage: test-ir-reduce [--seed N] [--count N<=1024]\n");
    }
    return result;
}
