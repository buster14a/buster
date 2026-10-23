// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's naturally aligned baseline-x86 atomic load/store/fence helpers. Native fixtures compare actual Clang
// __atomic operations and exact adjacent bytes, through both encoders and real Tier 0 callers with patched CALL
// references. A bounded acknowledged message exchange excludes concurrent payload access; it is a consumer test,
// not a claim that a finite stress run proves the memory model. Child alarm bounds every native execution.
// Invalid IDs, widths and arena exhaustion must publish nothing. Source order/type constraints belong elsewhere.

#include <pthread.h>
#include <sched.h>

#define BUSTER_CBUILTIN_ATOMIC_TEST_WIDTHS 4u
#define BUSTER_CBUILTIN_ATOMIC_TEST_HELPERS 11u
#define BUSTER_CBUILTIN_ATOMIC_TEST_PAGE 4096u
#define BUSTER_CBUILTIN_ATOMIC_TEST_ROUNDS 2048u
#define BUSTER_CBUILTIN_ATOMIC_TEST_SPINS UINT32_C(10000000)

typedef struct CBuiltinAtomicHelperTest CBuiltinAtomicHelperTest;
struct CBuiltinAtomicHelperTest
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinAtomicTestProgram CBuiltinAtomicTestProgram;
struct CBuiltinAtomicTestProgram
{
    uint8_t* mapping;
    uint8_t* entry;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

typedef struct CBuiltinAtomicTestNative CBuiltinAtomicTestNative;
struct CBuiltinAtomicTestNative
{
    CBuiltinAtomicTestProgram direct[2][BUSTER_CBUILTIN_ATOMIC_TEST_HELPERS];
    CBuiltinAtomicTestProgram callers[2][2 * BUSTER_CBUILTIN_ATOMIC_TEST_WIDTHS];
};

static bool cbuiltin_atomic_test_expect(CBuiltinAtomicHelperTest* test, bool condition, const char* description, uint32_t item)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 24)
        {
            fprintf(stderr, "test-builtin-atomic-helpers: %s item %u\n", description, item);
        }
        test->failures += 1;
    }
    return condition;
}

static uint64_t cbuiltin_atomic_test_load(const volatile void* pointer, uint32_t bytes, int order)
{
    uint64_t result = 0;
    switch (bytes)
    {
    case 1: result = __atomic_load_n((const volatile uint8_t*)pointer, order); break;
    case 2: result = __atomic_load_n((const volatile uint16_t*)pointer, order); break;
    case 4: result = __atomic_load_n((const volatile uint32_t*)pointer, order); break;
    case 8: result = __atomic_load_n((const volatile uint64_t*)pointer, order); break;
    }
    return result;
}

static void cbuiltin_atomic_test_store(volatile void* pointer, uint32_t bytes, uint64_t value, int order)
{
    switch (bytes)
    {
    case 1: __atomic_store_n((volatile uint8_t*)pointer, (uint8_t)value, order); break;
    case 2: __atomic_store_n((volatile uint16_t*)pointer, (uint16_t)value, order); break;
    case 4: __atomic_store_n((volatile uint32_t*)pointer, (uint32_t)value, order); break;
    case 8: __atomic_store_n((volatile uint64_t*)pointer, value, order); break;
    }
}

static bool cbuiltin_atomic_test_map(CBuiltinAtomicHelperTest* test, const CBuiltinAtomicHelper* helper, Tier0Code* caller, bool scalar, CBuiltinAtomicTestProgram* program, uint32_t item)
{
    const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(helper->code.count);
    const uint64_t caller_offset = (helper_capacity + 15) & ~UINT64_C(15);
    const uint64_t capacity = caller == NULL ? helper_capacity : caller_offset + BUSTER_X86_ENCODER_CAPACITY(caller->count);
    const uint64_t pages = (capacity + BUSTER_CBUILTIN_ATOMIC_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_ATOMIC_TEST_PAGE - 1);
    program->mapping_bytes = pages + BUSTER_CBUILTIN_ATOMIC_TEST_PAGE;
    program->mapping = mmap(0, program->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool result = cbuiltin_atomic_test_expect(test, program->mapping != MAP_FAILED, "native mapping", item);
    if (result)
    {
        const int64_t encoded = scalar ? x86_encode_scalar(helper->code.records, helper->code.count, program->mapping, helper_capacity, NULL, NULL)
                                       : x86_encode(helper->code.records, helper->code.count, program->mapping, helper_capacity, NULL, NULL);
        result = cbuiltin_atomic_test_expect(test, encoded > 0, "helper encoding", item);
        program->entry = program->mapping;
        program->byte_count = encoded > 0 ? (uint32_t)encoded : 0;
        if (result && caller != NULL)
        {
            uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
            const int64_t caller_bytes = scalar ? x86_encode_scalar(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends)
                                               : x86_encode(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends);
            result = cbuiltin_atomic_test_expect(test, caller_bytes > 0 && caller->reference_count == 1 && caller->references[0].target == 1 &&
                caller->references[0].field == X86_FIXUP_DISPLACEMENT && caller->references[0].kind == BUSTER_LINK_FIXUP_CALL &&
                caller->references[0].instruction < caller->count && caller->references[0].addend == 0, "Tier 0 helper reference", item);
            if (result)
            {
                const LinkReference reference = caller->references[0];
                const uint32_t distance = x86_fixup_distance(caller->records[reference.instruction].form, reference.field);
                result = cbuiltin_atomic_test_expect(test, distance == 4 && ends[0] >= distance && ends[0] <= (uint64_t)caller_bytes, "bounded rel32 helper fixup", item);
                if (result)
                {
                    const int32_t displacement = -(int32_t)(caller_offset + ends[0]);
                    memcpy(program->mapping + caller_offset + ends[0] - distance, &displacement, sizeof(displacement));
                    program->entry = program->mapping + caller_offset;
                    program->byte_count = (uint32_t)caller_bytes;
                }
            }
        }
        result = cbuiltin_atomic_test_expect(test, result && mprotect(program->mapping, pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(program->mapping + pages, BUSTER_CBUILTIN_ATOMIC_TEST_PAGE, PROT_NONE) == 0, "native permissions", item);
    }
    return result;
}

typedef struct CBuiltinAtomicTestExchange CBuiltinAtomicTestExchange;
struct CBuiltinAtomicTestExchange
{
    uint64_t payload;
    uint64_t ready;
    uint64_t acknowledged;
    uint32_t failed;
    void (*store)(volatile void*, uint64_t, int);
    uint64_t (*load)(const volatile void*, int);
    void (*fence)(int);
};

static bool cbuiltin_atomic_test_wait(CBuiltinAtomicTestExchange* exchange, const uint64_t* flag, uint64_t expected)
{
    bool result = false;
    for (uint32_t spin = 0; !result && spin < BUSTER_CBUILTIN_ATOMIC_TEST_SPINS && __atomic_load_n(&exchange->failed, __ATOMIC_RELAXED) == 0; spin += 1)
    {
        result = __atomic_load_n(flag, __ATOMIC_ACQUIRE) == expected;
        __builtin_ia32_pause();
        if ((spin & 255) == 255)
        {
            // The approved runner pins the process to one CPU, so the peer must get a turn promptly.
            sched_yield();
        }
    }
    if (!result)
    {
        __atomic_store_n(&exchange->failed, 1, __ATOMIC_RELAXED);
    }
    return result;
}

static void* cbuiltin_atomic_test_sender(void* opaque)
{
    CBuiltinAtomicTestExchange* exchange = opaque;
    bool running = true;
    for (uint64_t i = 1; running && i <= BUSTER_CBUILTIN_ATOMIC_TEST_ROUNDS; i += 1)
    {
        running = cbuiltin_atomic_test_wait(exchange, &exchange->acknowledged, i - 1);
        if (running)
        {
            exchange->store(&exchange->payload, i * UINT64_C(0x102030405), __ATOMIC_RELEASE);
            exchange->fence(__ATOMIC_RELEASE);
            __atomic_store_n(&exchange->ready, i, __ATOMIC_RELEASE);
        }
    }
    return NULL;
}

static void cbuiltin_atomic_test_exchange(CBuiltinAtomicHelperTest* test, const CBuiltinAtomicTestNative* native)
{
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CBuiltinAtomicTestExchange exchange =
        {
            .store = (void (*)(volatile void*, uint64_t, int))(void*)native->callers[path][7].entry,
            .load = (uint64_t (*)(const volatile void*, int))(void*)native->callers[path][3].entry,
            .fence = (void (*)(int))(void*)native->direct[path][8].entry,
        };
        pthread_t sender;
        const bool created = pthread_create(&sender, NULL, cbuiltin_atomic_test_sender, &exchange) == 0;
        bool running = created;
        for (uint64_t i = 1; running && i <= BUSTER_CBUILTIN_ATOMIC_TEST_ROUNDS; i += 1)
        {
            running = cbuiltin_atomic_test_wait(&exchange, &exchange.ready, i);
            if (running)
            {
                exchange.fence(__ATOMIC_ACQUIRE);
                const uint64_t value = exchange.load(&exchange.payload, __ATOMIC_ACQUIRE);
                running = value == i * UINT64_C(0x102030405);
                if (!running)
                {
                    __atomic_store_n(&exchange.failed, 1, __ATOMIC_RELAXED);
                }
                __atomic_store_n(&exchange.acknowledged, i, __ATOMIC_RELEASE);
            }
        }
        const bool joined = created && pthread_join(sender, NULL) == 0;
        cbuiltin_atomic_test_expect(test, created && joined && running && __atomic_load_n(&exchange.failed, __ATOMIC_RELAXED) == 0,
            "acknowledged two-thread Tier 0 caller payload exchange", path);
    }
}

static void cbuiltin_atomic_test_refusals(CBuiltinAtomicHelperTest* test)
{
    uint8_t storage[256] __attribute__((aligned(64)));
    uint8_t snapshot[sizeof(storage)];
    const CBuiltinId ids[] = { CBUILTIN_NONE, CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_ATOMIC_THREAD_FENCE, CBUILTIN_ATOMIC_SIGNAL_FENCE, CBUILTIN_SYNC_SYNCHRONIZE };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(ids); i += 1)
    {
        memset(storage, 0xa5, sizeof(storage));
        memcpy(snapshot, storage, sizeof(storage));
        Arena arena =
        {
            .base = storage,
            .length = 16,
            .capacity = sizeof(storage),
        };
        const CBuiltinAtomicHelper rejected = cbuiltin_atomic_helper_emit(ids[i], i == 1 || i == 2 ? 3 : 1, &arena);
        cbuiltin_atomic_test_expect(test, rejected.code.error == TIER0_ERROR_UNSUPPORTED && rejected.code.records == NULL && rejected.code.count == 0 &&
            rejected.parameter_count == 0 && rejected.required_alignment == 0 && !rejected.compiler_barrier && arena.length == 16 && memcmp(storage, snapshot, sizeof(storage)) == 0,
            "unsupported id or width publishes nothing", i);
    }
    const uint32_t invalid_widths[] = { 0, 3, 16, UINT32_MAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid_widths); i += 1)
    {
        Arena arena = { .base = storage, .capacity = sizeof(storage) };
        const CBuiltinAtomicHelper rejected = cbuiltin_atomic_helper_emit(CBUILTIN_ATOMIC_LOAD_N, invalid_widths[i], &arena);
        cbuiltin_atomic_test_expect(test, rejected.code.error == TIER0_ERROR_UNSUPPORTED && arena.length == 0, "unsupported scalar width", i);
    }
    memset(storage, 0xa5, sizeof(storage));
    memcpy(snapshot, storage, sizeof(storage));
    Arena small =
    {
        .base = storage,
        .length = 16,
        .capacity = 16 + sizeof(X86Instruction) - 1,
    };
    const CBuiltinAtomicHelper exhausted = cbuiltin_atomic_helper_emit(CBUILTIN_ATOMIC_STORE_N, 8, &small);
    cbuiltin_atomic_test_expect(test, exhausted.code.error == TIER0_ERROR_MEMORY && exhausted.code.records == NULL && exhausted.code.count == 0 &&
        exhausted.parameter_count == 0 && exhausted.required_alignment == 0 && !exhausted.compiler_barrier && small.length == 16 && memcmp(storage, snapshot, sizeof(storage)) == 0,
        "arena exhaustion is transactional", 0);
    const CBuiltinAtomicHelper missing = cbuiltin_atomic_helper_emit(CBUILTIN_ATOMIC_LOAD_N, 8, NULL);
    cbuiltin_atomic_test_expect(test, missing.code.error == TIER0_ERROR_MEMORY && missing.code.records == NULL && missing.code.count == 0, "missing output arena is a bounded memory refusal", 0);
}

static bool cbuiltin_atomic_test_caller(CBuiltinAtomicHelperTest* test, Arena* arena, const CBuiltinAtomicHelper* helper, CBuiltinAtomicTestProgram programs[2], uint32_t item);
static void cbuiltin_atomic_test_values(CBuiltinAtomicHelperTest* test, const CBuiltinAtomicTestNative* native);

static int run_builtin_atomic_helper_tests(int argc, char** argv)
{
    (void)argv;
    CBuiltinAtomicHelperTest test = { 0 };
    bool valid = cbuiltin_atomic_test_expect(&test, argc == 0, "usage: test-builtin-atomic-helpers", 0);
    CBuiltinAtomicTestNative native = { 0 };
    Arena arena = arena_reserve(UINT64_C(1) << 24);
    if (valid)
    {
        valid = cbuiltin_atomic_test_expect(&test, arena.base != NULL && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize(), "memory and table initialization", 0);
    }
    X86Target target;
    uint8_t allowed[X86_FORM_COUNT];
    if (valid)
    {
        valid = cbuiltin_atomic_test_expect(&test, x86_target_parse("x86-64", 6, &target), "baseline target profile", 0);
        x86_target_forms(&target, allowed);
        cbuiltin_atomic_test_refusals(&test);
    }
    for (uint32_t i = 0; valid && i < BUSTER_CBUILTIN_ATOMIC_TEST_HELPERS; i += 1)
    {
        const bool load = i < 4;
        const bool store = i >= 4 && i < 8;
        const CBuiltinId id = load ? CBUILTIN_ATOMIC_LOAD_N : store ? CBUILTIN_ATOMIC_STORE_N : i == 8 ? CBUILTIN_ATOMIC_THREAD_FENCE : i == 9 ? CBUILTIN_ATOMIC_SIGNAL_FENCE : CBUILTIN_SYNC_SYNCHRONIZE;
        const uint32_t bytes = load || store ? 1u << (i % 4) : 0;
        const CBuiltinAtomicHelper helper = cbuiltin_atomic_helper_emit(id, bytes, &arena);
        const uint32_t parameters = load ? 2 : store ? 3 : i == 10 ? 0 : 1;
        const uint32_t records = load ? 2 : store ? 3 : i == 9 ? 1 : 2;
        valid = cbuiltin_atomic_test_expect(&test, helper.code.error == TIER0_OK && helper.code.count == records && helper.code.branch_count == 0 &&
            helper.code.reference_count == 0 && helper.parameter_count == parameters && helper.result_class == (load ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID) &&
            helper.compiler_barrier && helper.required_alignment == bytes && x86_validate(helper.code.records, helper.code.count) == helper.code.count &&
            x86_target_validate(allowed, helper.code.records, helper.code.count) == helper.code.count, "helper ABI, alignment, record and baseline contracts", i);
        if (valid)
        {
            valid = cbuiltin_atomic_test_expect(&test, helper.parameter_classes[0] == (load || store ? BUSTER_TYPED_CLASS_U64 : parameters ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_VOID) &&
                helper.parameter_classes[1] == (load ? BUSTER_TYPED_CLASS_I32 : store ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_VOID) &&
                helper.parameter_classes[2] == (store ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_VOID), "helper parameter machine classes", i);
        }
        for (uint32_t path = 0; valid && path < 2; path += 1)
        {
            valid = cbuiltin_atomic_test_map(&test, &helper, NULL, path == 0, &native.direct[path][i], i);
        }
        if (valid)
        {
            valid = cbuiltin_atomic_test_expect(&test, native.direct[0][i].byte_count == native.direct[1][i].byte_count &&
                memcmp(native.direct[0][i].entry, native.direct[1][i].entry, native.direct[0][i].byte_count) == 0, "scalar and vector helper bytes", i);
        }
        if (valid && (load || store))
        {
            CBuiltinAtomicTestProgram programs[2] = { 0 };
            valid = cbuiltin_atomic_test_caller(&test, &arena, &helper, programs, i);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                native.callers[path][i] = programs[path];
            }
            if (valid)
            {
                valid = cbuiltin_atomic_test_expect(&test, programs[0].byte_count == programs[1].byte_count && memcmp(programs[0].entry, programs[1].entry, programs[0].byte_count) == 0,
                    "scalar and vector Tier 0 caller bytes with helper fixup", i);
            }
        }
    }
    CBuiltinAtomicHelperTest* shared = MAP_FAILED;
    if (valid)
    {
        shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        valid = cbuiltin_atomic_test_expect(&test, shared != MAP_FAILED, "native result mapping", 0);
    }
    if (valid)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(15);
            cbuiltin_atomic_test_values(shared, &native);
            cbuiltin_atomic_test_exchange(shared, &native);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        cbuiltin_atomic_test_expect(&test, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child", (uint32_t)status);
    }
    if (shared != MAP_FAILED)
    {
        test.checks += shared->checks;
        test.failures += shared->failures;
        munmap(shared, sizeof(*shared));
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t i = 0; i < BUSTER_CBUILTIN_ATOMIC_TEST_HELPERS; i += 1)
        {
            if (native.direct[path][i].mapping != NULL && native.direct[path][i].mapping != MAP_FAILED)
            {
                munmap(native.direct[path][i].mapping, native.direct[path][i].mapping_bytes);
            }
            if (i < 8 && native.callers[path][i].mapping != NULL && native.callers[path][i].mapping != MAP_FAILED)
            {
                munmap(native.callers[path][i].mapping, native.callers[path][i].mapping_bytes);
            }
        }
    }
    arena_release(&arena);
    printf("test-builtin-atomic-helpers: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}

static bool cbuiltin_atomic_test_caller(CBuiltinAtomicHelperTest* test, Arena* arena, const CBuiltinAtomicHelper* helper, CBuiltinAtomicTestProgram programs[2], uint32_t item)
{
    const uint64_t mark = arena->length;
    uint8_t kinds[5];
    uint8_t classes[5];
    uint64_t values[5] = { 0 };
    const uint32_t parameters = helper->parameter_count;
    uint8_t flags[3] = { 0 };
    AbiLocation locations[3] = { 0 };
    for (uint32_t p = 0; p < parameters; p += 1)
    {
        kinds[p] = BUSTER_TYPED_LOCAL;
        classes[p] = helper->parameter_classes[p];
        values[p] = p;
        locations[p].kind = BUSTER_ABI_LOCATION_REGISTERS;
        locations[p].registers[0] = abi_integer_arguments[p];
        locations[p].registers[1] = BUSTER_ABI_REGISTER_NONE;
    }
    kinds[parameters] = BUSTER_TYPED_CALL;
    classes[parameters] = (uint8_t)helper->result_class;
    kinds[parameters + 1] = BUSTER_TYPED_RETURN;
    classes[parameters + 1] = (uint8_t)helper->result_class;
    const SymbolId target = 1;
    const uint32_t first = 0;
    const uint8_t variadic = 0;
    AbiReturn returned = { 0 };
    if (helper->result_class != BUSTER_TYPED_CLASS_VOID)
    {
        returned.kind = BUSTER_ABI_RESULT_REGISTERS;
        returned.part_bytes = 8;
        returned.registers[0] = BUSTER_ABI_RAX;
        for (uint32_t i = 1; i < BUSTER_ABI_RESULT_PARTS; i += 1)
        {
            returned.registers[i] = BUSTER_ABI_REGISTER_NONE;
        }
    }
    const AbiCall call =
    {
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .integer_registers = (uint8_t)parameters,
        .result = returned,
    };
    const Tier0Function function =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = parameters + 2,
        .local_classes = helper->parameter_classes,
        .local_flags = flags,
        .local_count = parameters,
        .parameter_count = parameters,
        .parameters = locations,
        .result = returned,
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &parameters,
        .call_variadic = &variadic,
        .calls = &call,
        .argument_locations = locations,
        .argument_classes = helper->parameter_classes,
        .call_count = 1,
        .argument_count = parameters,
    };
    const Tier0Check checked = tier0_validate(&function);
    bool result = cbuiltin_atomic_test_expect(test, checked.error == TIER0_OK, checked.rule != NULL ? checked.rule : "Tier 0 caller validation", item);
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
        result = cbuiltin_atomic_test_expect(test, code.error == TIER0_OK && code.branch_count == 0 && code.reference_count == 1, "Tier 0 helper CALL compiled", item);
        if (result)
        {
            result = cbuiltin_atomic_test_map(test, helper, &code, path == 0, programs + path, item);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static void cbuiltin_atomic_test_values(CBuiltinAtomicHelperTest* test, const CBuiltinAtomicTestNative* native)
{
    const uint64_t patterns[] = { 0, 1, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x8000800080008080), UINT64_C(0x0123456789abcdef) };
    const int load_orders[] = { __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE, __ATOMIC_SEQ_CST };
    const int store_orders[] = { __ATOMIC_RELAXED, __ATOMIC_RELEASE, __ATOMIC_SEQ_CST };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        for (uint32_t w = 0; w < BUSTER_CBUILTIN_ATOMIC_TEST_WIDTHS; w += 1)
        {
            const uint32_t width = 1u << w;
            for (uint32_t caller = 0; caller < 2; caller += 1)
            {
                uint64_t (*load)(const volatile void*, int) = (uint64_t (*)(const volatile void*, int))(void*)(caller ? native->callers[path][w].entry : native->direct[path][w].entry);
                void (*store)(volatile void*, uint64_t, int) = (void (*)(volatile void*, uint64_t, int))(void*)(caller ? native->callers[path][4 + w].entry : native->direct[path][4 + w].entry);
                for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(patterns); p += 1)
                {
                    uint64_t actual[3];
                    uint64_t expected[3];
                    for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(store_orders); o += 1)
                    {
                        memset(actual, 0xa5, sizeof(actual));
                        memset(expected, 0xa5, sizeof(expected));
                        store(actual + 1, patterns[p], store_orders[o]);
                        cbuiltin_atomic_test_store(expected + 1, width, patterns[p], store_orders[o]);
                        cbuiltin_atomic_test_expect(test, memcmp(actual, expected, sizeof(actual)) == 0, "Clang store and exact adjacent bytes", w);
                    }
                    for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(load_orders); o += 1)
                    {
                        cbuiltin_atomic_test_store(actual + 1, width, patterns[p], __ATOMIC_RELAXED);
                        const uint64_t oracle = cbuiltin_atomic_test_load(actual + 1, width, load_orders[o]);
                        cbuiltin_atomic_test_expect(test, load(actual + 1, load_orders[o]) == oracle, "Clang load raw bits and zero extension", w);
                    }
                }
            }
            uint8_t* guarded = mmap(0, BUSTER_CBUILTIN_ATOMIC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            const bool mapped = guarded != MAP_FAILED && mprotect(guarded + BUSTER_CBUILTIN_ATOMIC_TEST_PAGE, BUSTER_CBUILTIN_ATOMIC_TEST_PAGE, PROT_NONE) == 0;
            if (cbuiltin_atomic_test_expect(test, mapped, "naturally aligned guarded object allocation", w))
            {
                memset(guarded, 0xa5, BUSTER_CBUILTIN_ATOMIC_TEST_PAGE);
                volatile void* tail = guarded + BUSTER_CBUILTIN_ATOMIC_TEST_PAGE - width;
                void (*store)(volatile void*, uint64_t, int) = (void (*)(volatile void*, uint64_t, int))(void*)native->direct[path][4 + w].entry;
                uint64_t (*load)(const volatile void*, int) = (uint64_t (*)(const volatile void*, int))(void*)native->direct[path][w].entry;
                store(tail, UINT64_MAX, __ATOMIC_SEQ_CST);
                const uint64_t mask = UINT64_MAX >> (64 - width * 8);
                cbuiltin_atomic_test_expect(test, load(tail, __ATOMIC_SEQ_CST) == mask && guarded[BUSTER_CBUILTIN_ATOMIC_TEST_PAGE - width - 1] == 0xa5, "exact guarded object width", w);
            }
            if (guarded != MAP_FAILED)
            {
                munmap(guarded, BUSTER_CBUILTIN_ATOMIC_TEST_PAGE * 2);
            }
        }
        void (*thread_fence)(int) = (void (*)(int))(void*)native->direct[path][8].entry;
        void (*signal_fence)(int) = (void (*)(int))(void*)native->direct[path][9].entry;
        void (*synchronize)(void) = (void (*)(void))(void*)native->direct[path][10].entry;
        for (int order = __ATOMIC_RELAXED; order <= __ATOMIC_SEQ_CST; order += 1)
        {
            thread_fence(order);
            signal_fence(order);
            __atomic_thread_fence(order);
            __atomic_signal_fence(order);
        }
        synchronize();
        __sync_synchronize();
        cbuiltin_atomic_test_expect(test, true, "native fence helpers returned", path);
    }
}
