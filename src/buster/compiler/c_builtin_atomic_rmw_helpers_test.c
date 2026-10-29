// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #148 scalar atomic RMW leaf fixtures. Include after c_builtin_atomic_rmw_helpers.c and the ordinary
// Tier 0/branch-sizing/encoder test dependencies. Independently spelled Clang __atomic/__sync calls provide
// object, expected-value and result oracles. BOOL_BITS checks the leaf's canonical 0/1 representation contract
// against byte operations; source-language eligibility of individual legacy bool spellings belongs to checking.
// Every helper executes through both encoders, and through scalar/vector Tier 0 CALLs with and without a live
// U64 stack prefix. All native work runs in an alarm-bounded child. Guard pages expose oversized narrow accesses;
// CAS success uses read-only expected storage and expected==object. A finite contention/handoff run is a real
// consumer fixture, not a proof of the memory model. No source capability or Tier 1 support is inferred here.

#include <pthread.h>
#include <sched.h>
#include <signal.h>

#define BUSTER_CBUILTIN_RMW_TEST_CAS 0x01u
#define BUSTER_CBUILTIN_RMW_TEST_GNU 0x02u
#define BUSTER_CBUILTIN_RMW_TEST_BOOL 0x04u
#define BUSTER_CBUILTIN_RMW_TEST_RELEASE 0x08u
// clang's min and max, also of signed integers.
#define BUSTER_CBUILTIN_RMW_TEST_ORDERED 0x10u
#define BUSTER_CBUILTIN_RMW_TEST_CASES 176u
#define BUSTER_CBUILTIN_RMW_TEST_PAGE 4096u
#define BUSTER_CBUILTIN_RMW_TEST_PREFIX UINT64_C(0x713596a2d84bc0ef)
#define BUSTER_CBUILTIN_RMW_TEST_THREADS 4u
#define BUSTER_CBUILTIN_RMW_TEST_ROUNDS 1024u
#define BUSTER_CBUILTIN_RMW_TEST_SPINS UINT32_C(10000000)

typedef struct CBuiltinAtomicRmwTest CBuiltinAtomicRmwTest;
struct CBuiltinAtomicRmwTest
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinAtomicRmwTestSpec CBuiltinAtomicRmwTestSpec;
struct CBuiltinAtomicRmwTestSpec
{
    CBuiltinId id;
    uint8_t parameters;
    uint8_t flags;
};

static const CBuiltinAtomicRmwTestSpec cbuiltin_atomic_rmw_test_specs[] =
{
    { CBUILTIN_ATOMIC_EXCHANGE_N, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N, 6, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_CAS },
    { CBUILTIN_ATOMIC_FETCH_ADD, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_ATOMIC_FETCH_SUB, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_ATOMIC_FETCH_AND, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_FETCH_OR, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_FETCH_XOR, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_FETCH_NAND, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_ATOMIC_ADD_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_ATOMIC_SUB_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_ATOMIC_AND_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_OR_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_XOR_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_ATOMIC_NAND_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU },
    { CBUILTIN_SYNC_FETCH_AND_ADD, 2, 0 },
    { CBUILTIN_SYNC_FETCH_AND_SUB, 2, 0 },
    { CBUILTIN_SYNC_FETCH_AND_AND, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_FETCH_AND_OR, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_FETCH_AND_XOR, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_FETCH_AND_NAND, 2, 0 },
    { CBUILTIN_SYNC_ADD_AND_FETCH, 2, 0 },
    { CBUILTIN_SYNC_SUB_AND_FETCH, 2, 0 },
    { CBUILTIN_SYNC_AND_AND_FETCH, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_OR_AND_FETCH, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_XOR_AND_FETCH, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_NAND_AND_FETCH, 2, 0 },
    { CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP, 3, BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_CAS },
    { CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP, 3, BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_CAS },
    { CBUILTIN_SYNC_LOCK_TEST_AND_SET, 2, BUSTER_CBUILTIN_RMW_TEST_BOOL },
    { CBUILTIN_SYNC_LOCK_RELEASE, 1, BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_RELEASE },
    { CBUILTIN_ATOMIC_FETCH_MIN, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_ORDERED },
    { CBUILTIN_ATOMIC_FETCH_MAX, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_ORDERED },
    { CBUILTIN_ATOMIC_MIN_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_ORDERED },
    { CBUILTIN_ATOMIC_MAX_FETCH, 3, BUSTER_CBUILTIN_RMW_TEST_GNU | BUSTER_CBUILTIN_RMW_TEST_BOOL | BUSTER_CBUILTIN_RMW_TEST_ORDERED },
};

typedef struct CBuiltinAtomicRmwTestProgram CBuiltinAtomicRmwTestProgram;
struct CBuiltinAtomicRmwTestProgram
{
    uint8_t* mapping;
    uint8_t* entry;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

typedef struct CBuiltinAtomicRmwTestCase CBuiltinAtomicRmwTestCase;
struct CBuiltinAtomicRmwTestCase
{
    CBuiltinAtomicRmwTestSpec spec;
    uint32_t bytes;
    CBuiltinAtomicRmwFormat format;
    CBuiltinAtomicRmwTestProgram programs[2][3];
};

typedef union CBuiltinAtomicRmwTestCell CBuiltinAtomicRmwTestCell;
union CBuiltinAtomicRmwTestCell
{
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
};

_Static_assert(sizeof(CBuiltinAtomicRmwTestCell) == 8, "native oracle cells retain eight exact object bytes");

static void* cbuiltin_atomic_rmw_test_cell(CBuiltinAtomicRmwTestCell* cell, const CBuiltinAtomicRmwTestCase* item, uint64_t bits)
{
    void* result;
    if (item->format == CBUILTIN_ATOMIC_RMW_F32_BITS)
    {
        result = &cell->f32;
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_F64_BITS)
    {
        result = &cell->f64;
    }
    else if (item->bytes == 1)
    {
        result = &cell->u8;
    }
    else if (item->bytes == 2)
    {
        result = &cell->u16;
    }
    else if (item->bytes == 4)
    {
        result = &cell->u32;
    }
    else
    {
        result = &cell->u64;
    }
    memcpy(result, &bits, item->bytes);
    return result;
}

static bool cbuiltin_atomic_rmw_test_expect(CBuiltinAtomicRmwTest* test, bool condition, const char* description, uint32_t item)
{
    test->checks += 1;
    if (!condition)
    {
        if (test->failures < 24)
        {
            fprintf(stderr, "test-builtin-atomic-rmw-helpers: %s item %u\n", description, item);
        }
        test->failures += 1;
    }
    return condition;
}

// Each branch invokes the host compiler's builtin directly; no arithmetic/result recipe from the leaf is reused.
// The legacy NAND warning describes the intended current semantics being compared here.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsync-fetch-and-nand-semantics-changed"

// A signed Type's results are its Unsigned bits, zero-extended as the leaves return them.
#define BUSTER_CBUILTIN_RMW_TEST_ORACLE(Type, Unsigned) \
    switch (id) \
    { \
    case CBUILTIN_ATOMIC_EXCHANGE_N: result = __atomic_exchange_n(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N: result = __atomic_compare_exchange_n(object, expected, (Type)value, weak, order, failure); break; \
    case CBUILTIN_ATOMIC_FETCH_ADD: result = __atomic_fetch_add(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_SUB: result = __atomic_fetch_sub(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_AND: result = __atomic_fetch_and(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_OR: result = __atomic_fetch_or(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_XOR: result = __atomic_fetch_xor(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_NAND: result = __atomic_fetch_nand(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_ADD_FETCH: result = __atomic_add_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_SUB_FETCH: result = __atomic_sub_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_AND_FETCH: result = __atomic_and_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_OR_FETCH: result = __atomic_or_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_XOR_FETCH: result = __atomic_xor_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_NAND_FETCH: result = __atomic_nand_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_MIN: result = (Unsigned)__atomic_fetch_min(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_FETCH_MAX: result = (Unsigned)__atomic_fetch_max(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_MIN_FETCH: result = (Unsigned)__atomic_min_fetch(object, (Type)value, order); break; \
    case CBUILTIN_ATOMIC_MAX_FETCH: result = (Unsigned)__atomic_max_fetch(object, (Type)value, order); break; \
    case CBUILTIN_SYNC_FETCH_AND_ADD: result = __sync_fetch_and_add(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_SUB: result = __sync_fetch_and_sub(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_AND: result = __sync_fetch_and_and(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_OR: result = __sync_fetch_and_or(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_XOR: result = __sync_fetch_and_xor(object, (Type)value); break; \
    case CBUILTIN_SYNC_FETCH_AND_NAND: result = __sync_fetch_and_nand(object, (Type)value); break; \
    case CBUILTIN_SYNC_ADD_AND_FETCH: result = __sync_add_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_SUB_AND_FETCH: result = __sync_sub_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_AND_AND_FETCH: result = __sync_and_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_OR_AND_FETCH: result = __sync_or_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_XOR_AND_FETCH: result = __sync_xor_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_NAND_AND_FETCH: result = __sync_nand_and_fetch(object, (Type)value); break; \
    case CBUILTIN_SYNC_BOOL_COMPARE_AND_SWAP: result = __sync_bool_compare_and_swap(object, *expected, (Type)value); break; \
    case CBUILTIN_SYNC_VAL_COMPARE_AND_SWAP: result = __sync_val_compare_and_swap(object, *expected, (Type)value); break; \
    case CBUILTIN_SYNC_LOCK_TEST_AND_SET: result = __sync_lock_test_and_set(object, (Type)value); break; \
    case CBUILTIN_SYNC_LOCK_RELEASE: __sync_lock_release(object); result = 0; break; \
    default: break; \
    }

static uint64_t cbuiltin_atomic_rmw_test_oracle(const CBuiltinAtomicRmwTestCase* item, volatile void* object_pointer, void* expected_pointer, uint64_t value, bool weak, int order, int failure)
{
    uint64_t result = 0;
    const CBuiltinId id = item->spec.id;
    if (item->format == CBUILTIN_ATOMIC_RMW_F32_BITS)
    {
        union
        {
            uint32_t bits;
            float value;
        } desired = { .bits = (uint32_t)value }, previous;
        previous.value = __atomic_exchange_n((volatile float*)object_pointer, desired.value, order);
        result = previous.bits;
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_F64_BITS)
    {
        union
        {
            uint64_t bits;
            double value;
        } desired = { .bits = value }, previous;
        previous.value = __atomic_exchange_n((volatile double*)object_pointer, desired.value, order);
        result = previous.bits;
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS && item->bytes == 1)
    {
        volatile int8_t* object = object_pointer;
        int8_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(int8_t, uint8_t);
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS && item->bytes == 2)
    {
        volatile int16_t* object = object_pointer;
        int16_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(int16_t, uint16_t);
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS && item->bytes == 4)
    {
        volatile int32_t* object = object_pointer;
        int32_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(int32_t, uint32_t);
    }
    else if (item->format == CBUILTIN_ATOMIC_RMW_SIGNED_BITS && item->bytes == 8)
    {
        volatile int64_t* object = object_pointer;
        int64_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(int64_t, uint64_t);
    }
    else if (item->bytes == 1)
    {
        volatile uint8_t* object = object_pointer;
        uint8_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(uint8_t, uint8_t);
    }
    else if (item->bytes == 2)
    {
        volatile uint16_t* object = object_pointer;
        uint16_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(uint16_t, uint16_t);
    }
    else if (item->bytes == 4)
    {
        volatile uint32_t* object = object_pointer;
        uint32_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(uint32_t, uint32_t);
    }
    else if (item->bytes == 8)
    {
        volatile uint64_t* object = object_pointer;
        uint64_t* expected = expected_pointer;
        BUSTER_CBUILTIN_RMW_TEST_ORACLE(uint64_t, uint64_t);
    }
    return result;
}

#undef BUSTER_CBUILTIN_RMW_TEST_ORACLE
#pragma clang diagnostic pop

static uint64_t cbuiltin_atomic_rmw_test_invoke(const CBuiltinAtomicRmwTestCase* item, uint32_t path, uint32_t variant, volatile void* object, void* expected, uint64_t expected_value, uint64_t value, bool weak, int order, int failure)
{
    uint64_t arguments[5] = { value, (uint64_t)order, 0, 0, 0 };
    if ((item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_CAS) != 0)
    {
        arguments[0] = item->spec.id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N ? (uintptr_t)expected : expected_value;
        arguments[1] = value;
        arguments[2] = weak;
        arguments[3] = (uint32_t)order;
        arguments[4] = (uint32_t)failure;
    }
    uint8_t* entry = item->programs[path][variant].entry;
    uint64_t result = 0;
    if (variant == 0 && (item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_RELEASE) != 0)
    {
        void (*function)(volatile void*, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t) = (void (*)(volatile void*, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t))(void*)entry;
        function(object, arguments[0], arguments[1], (uint32_t)arguments[2], (uint32_t)arguments[3], (uint32_t)arguments[4]);
    }
    else
    {
        uint64_t (*function)(volatile void*, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t) = (uint64_t (*)(volatile void*, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t))(void*)entry;
        result = function(object, arguments[0], arguments[1], (uint32_t)arguments[2], (uint32_t)arguments[3], (uint32_t)arguments[4]);
    }
    if (variant == 2)
    {
        result -= BUSTER_CBUILTIN_RMW_TEST_PREFIX;
    }
    return result;
}

static bool cbuiltin_atomic_rmw_test_map(CBuiltinAtomicRmwTest* test, const Tier0Code* helper, const Tier0Code* caller, bool scalar, CBuiltinAtomicRmwTestProgram* program, uint32_t item)
{
    X86Instruction records[BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS];
    uint32_t offsets[BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS + 1];
    bool result = cbuiltin_atomic_rmw_test_expect(test, helper->count <= BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS, "bounded helper records", item);
    const uint64_t helper_capacity = BUSTER_X86_ENCODER_CAPACITY(helper->count);
    const uint64_t caller_offset = (helper_capacity + 15) & ~UINT64_C(15);
    const uint64_t capacity = caller == NULL ? helper_capacity : caller_offset + BUSTER_X86_ENCODER_CAPACITY(caller->count);
    const uint64_t pages = (capacity + BUSTER_CBUILTIN_RMW_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_RMW_TEST_PAGE - 1);
    if (result)
    {
        program->mapping_bytes = pages + BUSTER_CBUILTIN_RMW_TEST_PAGE;
        program->mapping = mmap(NULL, program->mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        result = cbuiltin_atomic_rmw_test_expect(test, program->mapping != MAP_FAILED, "native code mapping", item);
    }
    if (result)
    {
        memcpy(records, helper->records, helper->count * sizeof(*records));
        const BranchSizing sized = scalar ? size_branches_scalar(records, helper->count, helper->branches, helper->targets, helper->branch_count, offsets) :
                                          size_branches(records, helper->count, helper->branches, helper->targets, helper->branch_count, offsets);
        const int64_t encoded = sized.error != BRANCH_SIZING_OK ? -1 : scalar ? x86_encode_scalar(records, helper->count, program->mapping, helper_capacity, NULL, NULL) :
                                                                            x86_encode(records, helper->count, program->mapping, helper_capacity, NULL, NULL);
        result = cbuiltin_atomic_rmw_test_expect(test, encoded > 0, "branch sizing and native helper encoding", item);
        program->entry = program->mapping;
        program->byte_count = encoded > 0 ? (uint32_t)encoded : 0;
        if (result && caller != NULL)
        {
            uint32_t ends[BUSTER_X86_ENCODER_FIXUP_CAPACITY(1)] = { 0 };
            const int64_t caller_bytes = scalar ? x86_encode_scalar(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends) :
                                                 x86_encode(caller->records, caller->count, program->mapping + caller_offset, capacity - caller_offset, NULL, ends);
            result = cbuiltin_atomic_rmw_test_expect(test, caller_bytes > 0 && caller->reference_count == 1 && caller->references[0].target == 1 &&
                caller->references[0].kind == BUSTER_LINK_FIXUP_CALL && caller->references[0].field == X86_FIXUP_DISPLACEMENT &&
                caller->references[0].instruction < caller->count && caller->references[0].addend == 0, "real Tier 0 unknown-effect helper CALL reference", item);
            if (result)
            {
                const uint32_t distance = x86_fixup_distance(caller->records[caller->references[0].instruction].form, X86_FIXUP_DISPLACEMENT);
                result = cbuiltin_atomic_rmw_test_expect(test, distance == 4 && ends[0] >= distance && ends[0] <= (uint64_t)caller_bytes, "bounded helper rel32 patch", item);
                if (result)
                {
                    const int32_t displacement = -(int32_t)(caller_offset + ends[0]);
                    memcpy(program->mapping + caller_offset + ends[0] - distance, &displacement, sizeof(displacement));
                    program->entry = program->mapping + caller_offset;
                    program->byte_count = (uint32_t)caller_bytes;
                }
            }
        }
        result = cbuiltin_atomic_rmw_test_expect(test, result && mprotect(program->mapping, pages, PROT_READ | PROT_EXEC) == 0 &&
            mprotect(program->mapping + pages, BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_NONE) == 0, "native W^X permissions and trailing guard", item);
    }
    return result;
}

static bool cbuiltin_atomic_rmw_test_caller(CBuiltinAtomicRmwTest* test, Arena* arena, const CBuiltinAtomicRmwHelper* helper, CBuiltinAtomicRmwTestCase* item, uint32_t variant, uint32_t ordinal)
{
    const uint64_t mark = arena->length;
    uint8_t kinds[10] = { 0 };
    uint8_t classes[10] = { 0 };
    uint64_t values[10] = { 0 };
    uint8_t flags[6] = { 0 };
    AbiLocation locations[6] = { 0 };
    uint32_t count = 0;
    if (variant == 2)
    {
        kinds[count] = BUSTER_TYPED_CONST;
        classes[count] = BUSTER_TYPED_CLASS_U64;
        values[count] = BUSTER_CBUILTIN_RMW_TEST_PREFIX;
        count += 1;
    }
    for (uint32_t i = 0; i < helper->parameter_count; i += 1)
    {
        kinds[count] = BUSTER_TYPED_LOCAL;
        classes[count] = helper->parameter_classes[i];
        values[count] = i;
        count += 1;
        locations[i].kind = BUSTER_ABI_LOCATION_REGISTERS;
        locations[i].registers[0] = abi_integer_arguments[i];
        locations[i].registers[1] = BUSTER_ABI_REGISTER_NONE;
    }
    kinds[count] = BUSTER_TYPED_CALL;
    classes[count] = (uint8_t)helper->result_class;
    count += 1;
    const bool release = helper->result_class == BUSTER_TYPED_CLASS_VOID;
    if (release && variant == 1)
    {
        kinds[count] = BUSTER_TYPED_CONST;
        classes[count] = BUSTER_TYPED_CLASS_U64;
        count += 1;
    }
    else if (!release && variant == 2)
    {
        kinds[count] = BUSTER_TYPED_ADD;
        classes[count] = BUSTER_TYPED_CLASS_U64;
        count += 1;
    }
    else if (release && variant == 2)
    {
        // The prefix comes back from under the call that returns nothing as postorder has it, `prefix + (call, 0)`:
        // a return's operand must have the class of the entry before it, which Tier 0 reads for it.
        kinds[count] = BUSTER_TYPED_CONST;
        classes[count] = BUSTER_TYPED_CLASS_U64;
        kinds[count + 1] = BUSTER_TYPED_ADD;
        classes[count + 1] = BUSTER_TYPED_CLASS_U64;
        count += 2;
    }
    kinds[count] = BUSTER_TYPED_RETURN;
    classes[count] = BUSTER_TYPED_CLASS_U64;
    count += 1;
    const AbiReturn returned =
    {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
    };
    const AbiCall call =
    {
        .stack_alignment_log2 = BUSTER_ABI_STACK_ALIGNMENT_LOG2,
        .integer_registers = (uint8_t)helper->parameter_count,
        .result = release ? (AbiReturn){ 0 } : returned,
    };
    const SymbolId target = 1;
    const uint32_t first = 0;
    const uint8_t variadic = 0;
    const Tier0Function function =
    {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = count,
        .local_classes = helper->parameter_classes,
        .local_flags = flags,
        .local_count = helper->parameter_count,
        .parameter_count = helper->parameter_count,
        .parameters = locations,
        .result = returned,
        .call_targets = &target,
        .call_first = &first,
        .call_argument_counts = &helper->parameter_count,
        .call_variadic = &variadic,
        .calls = &call,
        .argument_locations = locations,
        .argument_classes = helper->parameter_classes,
        .call_count = 1,
        .argument_count = helper->parameter_count,
    };
    const Tier0Check checked = tier0_validate(&function);
    bool result = cbuiltin_atomic_rmw_test_expect(test, checked.error == TIER0_OK, checked.rule != NULL ? checked.rule : "Tier 0 caller validation", ordinal);
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        const Tier0Code code = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
        result = cbuiltin_atomic_rmw_test_expect(test, code.error == TIER0_OK && code.branch_count == 0 && code.reference_count == 1, "Tier 0 CALL compiles with retained live prefix", ordinal);
        if (result)
        {
            result = cbuiltin_atomic_rmw_test_map(test, &helper->code, &code, path == 0, &item->programs[path][variant], ordinal);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static bool cbuiltin_atomic_rmw_test_empty(const CBuiltinAtomicRmwHelper* helper, Tier0Error error)
{
    bool result = helper->code.error == error && helper->code.records == NULL && helper->code.count == 0 && helper->code.branches == NULL &&
        helper->code.targets == NULL && helper->code.branch_count == 0 && helper->code.references == NULL && helper->code.reference_count == 0 &&
        helper->code.deferred == 0 && helper->code.node == 0 && helper->parameter_count == 0 && helper->result_class == 0 &&
        !helper->compiler_barrier && helper->required_alignment == 0;
    for (uint32_t i = 0; i < 6; i += 1)
    {
        result &= helper->parameter_classes[i] == 0;
    }
    return result;
}

static void cbuiltin_atomic_rmw_test_refusals(CBuiltinAtomicRmwTest* test)
{
    uint8_t storage[512] __attribute__((aligned(64)));
    uint8_t snapshot[sizeof(storage)];
    const uint32_t widths[] = { 0, 3, 16, UINT32_MAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cbuiltin_atomic_rmw_test_specs); i += 1)
    {
        const CBuiltinAtomicRmwTestSpec spec = cbuiltin_atomic_rmw_test_specs[i];
        for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(widths) + 3; w += 1)
        {
            memset(storage, 0xa5, sizeof(storage));
            memcpy(snapshot, storage, sizeof(storage));
            Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
            const uint32_t bytes = w < BUSTER_ARRAY_LENGTH(widths) ? widths[w] : w == 4 ? 8 : 4;
            const CBuiltinAtomicRmwFormat format = w < 4 ? CBUILTIN_ATOMIC_RMW_INTEGER_BITS : w == 4 ? CBUILTIN_ATOMIC_RMW_BOOL_BITS :
                w == 5 ? CBUILTIN_ATOMIC_RMW_F64_BITS : (CBuiltinAtomicRmwFormat)UINT32_MAX;
            const CBuiltinAtomicRmwHelper rejected = cbuiltin_atomic_rmw_helper_emit(spec.id, bytes, format, &arena);
            cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&rejected, TIER0_ERROR_UNSUPPORTED) && arena.length == 16 &&
                memcmp(storage, snapshot, sizeof(storage)) == 0, "invalid width/format leaves every output and arena byte unchanged", i);
        }
        if ((spec.flags & BUSTER_CBUILTIN_RMW_TEST_BOOL) == 0)
        {
            Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
            const CBuiltinAtomicRmwHelper rejected = cbuiltin_atomic_rmw_helper_emit(spec.id, 1, CBUILTIN_ATOMIC_RMW_BOOL_BITS, &arena);
            cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&rejected, TIER0_ERROR_UNSUPPORTED) && arena.length == 16,
                "bool arithmetic/NAND never acquires a raw-byte substitute", i);
        }
        if ((spec.flags & BUSTER_CBUILTIN_RMW_TEST_ORDERED) == 0)
        {
            Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
            const CBuiltinAtomicRmwHelper rejected = cbuiltin_atomic_rmw_helper_emit(spec.id, 8, CBUILTIN_ATOMIC_RMW_SIGNED_BITS, &arena);
            cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&rejected, TIER0_ERROR_UNSUPPORTED) && arena.length == 16,
                "only min and max take signed integer bits", i);
        }
        if (spec.id != CBUILTIN_ATOMIC_EXCHANGE_N)
        {
            for (uint32_t floating = 0; floating < 2; floating += 1)
            {
                Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
                const CBuiltinAtomicRmwFormat format = floating ? CBUILTIN_ATOMIC_RMW_F64_BITS : CBUILTIN_ATOMIC_RMW_F32_BITS;
                const CBuiltinAtomicRmwHelper rejected = cbuiltin_atomic_rmw_helper_emit(spec.id, floating ? 8 : 4, format, &arena);
                cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&rejected, TIER0_ERROR_UNSUPPORTED) && arena.length == 16,
                    "floating arithmetic/CAS and legacy exchange remain explicitly unsupported", i);
            }
        }
        memset(storage, 0xa5, sizeof(storage));
        memcpy(snapshot, storage, sizeof(storage));
        Arena arena = { .base = storage, .length = 16, .capacity = 16 + sizeof(X86Instruction) - 1 };
        const CBuiltinAtomicRmwHelper exhausted = cbuiltin_atomic_rmw_helper_emit(spec.id, 8, CBUILTIN_ATOMIC_RMW_INTEGER_BITS, &arena);
        cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&exhausted, TIER0_ERROR_MEMORY) && arena.length == 16 &&
            memcmp(storage, snapshot, sizeof(storage)) == 0, "allocation exhaustion is transactional for every operation", i);
    }
    const CBuiltinId unknown[] = { CBUILTIN_NONE, CBUILTIN_ATOMIC_LOAD_N, CBUILTIN_ATOMIC_STORE_N, CBUILTIN_SYNC_SYNCHRONIZE, (CBuiltinId)UINT32_MAX };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(unknown); i += 1)
    {
        Arena arena = { .base = storage, .length = 16, .capacity = sizeof(storage) };
        const CBuiltinAtomicRmwHelper rejected = cbuiltin_atomic_rmw_helper_emit(unknown[i], 8, CBUILTIN_ATOMIC_RMW_INTEGER_BITS, &arena);
        cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&rejected, TIER0_ERROR_UNSUPPORTED) && arena.length == 16, "foreign/unknown IDs fail closed", i);
    }
    const CBuiltinAtomicRmwHelper missing = cbuiltin_atomic_rmw_helper_emit(CBUILTIN_ATOMIC_FETCH_ADD, 8, CBUILTIN_ATOMIC_RMW_INTEGER_BITS, NULL);
    cbuiltin_atomic_rmw_test_expect(test, cbuiltin_atomic_rmw_test_empty(&missing, TIER0_ERROR_MEMORY), "missing output arena is a bounded memory error", 0);
}

static bool cbuiltin_atomic_rmw_test_prepare(CBuiltinAtomicRmwTest* test, Arena* arena, const uint8_t* allowed, CBuiltinAtomicRmwTestCase* item, uint32_t ordinal)
{
    const uint64_t mark = arena->length;
    const CBuiltinAtomicRmwHelper helper = cbuiltin_atomic_rmw_helper_emit(item->spec.id, item->bytes, item->format, arena);
    const bool release = (item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_RELEASE) != 0;
    const bool cas = (item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_CAS) != 0;
    const uint32_t branches = release || (cas && item->spec.id != CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N) ? 0 : 1;
    bool result = cbuiltin_atomic_rmw_test_expect(test, helper.code.error == TIER0_OK && helper.code.count > 0 &&
        helper.code.count <= BUSTER_CBUILTIN_ATOMIC_RMW_RECORDS && helper.code.branch_count == branches && helper.code.reference_count == 0 &&
        helper.parameter_count == item->spec.parameters && helper.result_class == (release ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64) &&
        helper.required_alignment == item->bytes && helper.compiler_barrier && x86_validate(helper.code.records, helper.code.count) == helper.code.count &&
        x86_target_validate(allowed, helper.code.records, helper.code.count) == helper.code.count, "ABI, branch, alignment, compiler-barrier and baseline contracts", ordinal);
    for (uint32_t i = 0; result && i < 6; i += 1)
    {
        const bool order = (item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_GNU) != 0 && i >= (cas ? 3u : 2u);
        const uint8_t expected = i >= item->spec.parameters ? 0 : order ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
        result = cbuiltin_atomic_rmw_test_expect(test, helper.parameter_classes[i] == expected, "independent six-register parameter classes", ordinal);
    }
    for (uint32_t b = 0; result && b < helper.code.branch_count; b += 1)
    {
        result = cbuiltin_atomic_rmw_test_expect(test, helper.code.branches[b] < helper.code.count && helper.code.targets[b] < helper.code.count &&
            helper.code.records[helper.code.branches[b]].displacement == 0, "unsized branch has a bounded target and zero displacement", ordinal);
    }
    if (result && release)
    {
        uint32_t memory = 0;
        for (uint32_t i = 0; i < helper.code.count; i += 1)
        {
            const X86Instruction record = helper.code.records[i];
            const bool store = record.form == X86_FORM_MOV_RM8_R8 || record.form == X86_FORM_MOV_RM16_R16 ||
                record.form == X86_FORM_MOV_RM32_R32 || record.form == X86_FORM_MOV_RM64_R64;
            memory += record.mode == BUSTER_X86_MEMORY_BASE;
            result = cbuiltin_atomic_rmw_test_expect(test, record.mode != BUSTER_X86_MEMORY_BASE || (store && record.rm == X86_REGISTER_RDI),
                "legacy lock release writes once without reading or performing an RMW on its object", ordinal) && result;
        }
        result = cbuiltin_atomic_rmw_test_expect(test, memory == 1, "legacy lock release has exactly one object store", ordinal) && result;
    }
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        result = cbuiltin_atomic_rmw_test_map(test, &helper.code, NULL, path == 0, &item->programs[path][0], ordinal);
    }
    for (uint32_t variant = 1; result && variant < 3; variant += 1)
    {
        result = cbuiltin_atomic_rmw_test_caller(test, arena, &helper, item, variant, ordinal);
    }
    for (uint32_t variant = 0; result && variant < 3; variant += 1)
    {
        const CBuiltinAtomicRmwTestProgram* a = &item->programs[0][variant];
        const CBuiltinAtomicRmwTestProgram* b = &item->programs[1][variant];
        result = cbuiltin_atomic_rmw_test_expect(test, a->byte_count == b->byte_count && memcmp(a->entry, b->entry, a->byte_count) == 0,
            "scalar/vector branch sizing, encoding and ordinary CALL bytes agree", ordinal);
    }
    arena_pop_to(arena, mark);
    return result;
}

static void cbuiltin_atomic_rmw_test_sample(CBuiltinAtomicRmwTest* test, const CBuiltinAtomicRmwTestCase* item, uint32_t path, uint32_t variant, uint64_t initial, uint64_t value, uint64_t expected_value, bool weak, int order, int failure, uint32_t ordinal)
{
    uint64_t object[3];
    CBuiltinAtomicRmwTestCell reference[3];
    uint64_t expected[3];
    CBuiltinAtomicRmwTestCell reference_expected[3];
    memset(object, 0xa5, sizeof(object));
    memset(reference, 0xa5, sizeof(reference));
    memset(expected, 0x5a, sizeof(expected));
    memset(reference_expected, 0x5a, sizeof(reference_expected));
    memcpy(object + 1, &initial, item->bytes);
    memcpy(expected + 1, &expected_value, item->bytes);
    void* reference_object = cbuiltin_atomic_rmw_test_cell(reference + 1, item, initial);
    void* reference_value = cbuiltin_atomic_rmw_test_cell(reference_expected + 1, item, expected_value);
    const uint64_t oracle = cbuiltin_atomic_rmw_test_oracle(item, reference_object, reference_value, value, weak, order, failure);
    const uint64_t actual = cbuiltin_atomic_rmw_test_invoke(item, path, variant, object + 1, expected + 1, expected_value, value, weak, order, failure);
    cbuiltin_atomic_rmw_test_expect(test, actual == oracle && memcmp(object, reference, sizeof(object)) == 0 &&
        memcmp(expected, reference_expected, sizeof(expected)) == 0, "Clang return/object/expected oracle with exact adjacent bytes and retained live prefix", ordinal);
}

static void cbuiltin_atomic_rmw_test_values(CBuiltinAtomicRmwTest* test, const CBuiltinAtomicRmwTestCase* items, uint32_t count)
{
    const uint64_t patterns[] = { 0, 1, UINT64_MAX, UINT64_C(0x8000800080008080), UINT64_C(0x0123456789abcdef), UINT64_C(0x7ff0000000000001) };
    const uint64_t fp32[] = { 0, UINT64_C(0x80000000), UINT64_C(0x7f800000), UINT64_C(0xff800000), UINT64_C(0x7f800001), UINT64_C(0x00000001) };
    const uint64_t fp64[] = { 0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff0000000000001), 1 };
    const int failures[] = { __ATOMIC_RELAXED, __ATOMIC_CONSUME, __ATOMIC_ACQUIRE, __ATOMIC_SEQ_CST };
    for (uint32_t i = 0; i < count; i += 1)
    {
        const CBuiltinAtomicRmwTestCase* item = items + i;
        const bool boolean = item->format == CBUILTIN_ATOMIC_RMW_BOOL_BITS;
        const bool cas = (item->spec.flags & BUSTER_CBUILTIN_RMW_TEST_CAS) != 0;
        const uint64_t* samples = item->format == CBUILTIN_ATOMIC_RMW_F32_BITS ? fp32 : item->format == CBUILTIN_ATOMIC_RMW_F64_BITS ? fp64 : patterns;
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                for (uint32_t p = 0; p < BUSTER_ARRAY_LENGTH(patterns); p += 1)
                {
                    const uint64_t initial = boolean ? p & 1 : samples[p];
                    const uint64_t value = boolean ? (p + 1) & 1 : samples[(p + 3) % BUSTER_ARRAY_LENGTH(patterns)];
                    if (item->spec.id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N)
                    {
                        // Exact Clang accepts every success order with these four failure orders, including stronger failure.
                        for (int order = __ATOMIC_RELAXED; order <= __ATOMIC_SEQ_CST; order += 1)
                        {
                            for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(failures); f += 1)
                            {
                                for (uint32_t weak = 0; weak < 2; weak += 1)
                                {
                                    for (uint32_t match = 0; match < 2; match += 1)
                                    {
                                        cbuiltin_atomic_rmw_test_sample(test, item, path, variant, initial, value, initial ^ (match ? 0 : 1), weak != 0, order, failures[f], i);
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        for (int order = __ATOMIC_RELAXED; order <= __ATOMIC_SEQ_CST; order += 1)
                        {
                            for (uint32_t match = 0; match < (cas ? 2u : 1u); match += 1)
                            {
                                cbuiltin_atomic_rmw_test_sample(test, item, path, variant, initial, value, initial ^ (match ? 0 : 1), false, order, __ATOMIC_RELAXED, i);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void cbuiltin_atomic_rmw_test_guards(CBuiltinAtomicRmwTest* test, const CBuiltinAtomicRmwTestCase* items, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        const CBuiltinAtomicRmwTestCase* item = items + i;
        uint8_t* mapping = mmap(NULL, 4 * BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool mapped = mapping != MAP_FAILED && mprotect(mapping + BUSTER_CBUILTIN_RMW_TEST_PAGE, BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_NONE) == 0 &&
            mprotect(mapping + 3 * BUSTER_CBUILTIN_RMW_TEST_PAGE, BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_NONE) == 0;
        if (cbuiltin_atomic_rmw_test_expect(test, mapped, "naturally aligned exact object and expected tails", i))
        {
            uint8_t* object = mapping + BUSTER_CBUILTIN_RMW_TEST_PAGE - item->bytes;
            uint8_t* expected = mapping + 3 * BUSTER_CBUILTIN_RMW_TEST_PAGE - item->bytes;
            const uint64_t initial = 1;
            const uint64_t desired = 0;
            const uint64_t mismatch = 0;
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t variant = 0; variant < 3; variant += 1)
                {
                    memset(mapping, 0xa5, BUSTER_CBUILTIN_RMW_TEST_PAGE);
                    memset(mapping + 2 * BUSTER_CBUILTIN_RMW_TEST_PAGE, 0x5a, BUSTER_CBUILTIN_RMW_TEST_PAGE);
                    memcpy(object, &initial, item->bytes);
                    memcpy(expected, &mismatch, item->bytes);
                    CBuiltinAtomicRmwTestCell reference;
                    CBuiltinAtomicRmwTestCell reference_expected;
                    void* reference_object = cbuiltin_atomic_rmw_test_cell(&reference, item, initial);
                    void* reference_value = cbuiltin_atomic_rmw_test_cell(&reference_expected, item, mismatch);
                    const uint64_t oracle = cbuiltin_atomic_rmw_test_oracle(item, reference_object, reference_value, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
                    const uint64_t actual = cbuiltin_atomic_rmw_test_invoke(item, path, variant, object, expected, mismatch, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
                    cbuiltin_atomic_rmw_test_expect(test, actual == oracle && memcmp(object, &reference, item->bytes) == 0 &&
                        memcmp(expected, &reference_expected, item->bytes) == 0 && object[-1] == 0xa5 && expected[-1] == 0x5a,
                        "narrow RMW/CAS touches only exact object/expected widths", i);
                    if (item->spec.id == CBUILTIN_ATOMIC_COMPARE_EXCHANGE_N)
                    {
                        memcpy(object, &initial, item->bytes);
                        memcpy(expected, &initial, item->bytes);
                        const bool read_only = mprotect(mapping + 2 * BUSTER_CBUILTIN_RMW_TEST_PAGE, BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_READ) == 0;
                        if (cbuiltin_atomic_rmw_test_expect(test, read_only, "CAS expected becomes read-only on successful comparison", i))
                        {
                            const uint64_t succeeded = cbuiltin_atomic_rmw_test_invoke(item, path, variant, object, expected, initial, desired, true, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
                            cbuiltin_atomic_rmw_test_expect(test, succeeded == 1 && memcmp(object, &desired, item->bytes) == 0 && memcmp(expected, &initial, item->bytes) == 0,
                                "successful GNU CAS never writes expected", i);
                        }
                        const bool writable = mprotect(mapping + 2 * BUSTER_CBUILTIN_RMW_TEST_PAGE, BUSTER_CBUILTIN_RMW_TEST_PAGE, PROT_READ | PROT_WRITE) == 0;
                        cbuiltin_atomic_rmw_test_expect(test, writable, "restore expected storage permissions", i);
                        memcpy(object, &initial, item->bytes);
                        const uint64_t aliased = cbuiltin_atomic_rmw_test_invoke(item, path, variant, object, object, initial, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
                        cbuiltin_atomic_rmw_test_expect(test, aliased == 1 && memcmp(object, &desired, item->bytes) == 0 && object[-1] == 0xa5,
                            "successful GNU CAS preserves expected==object alias instead of storing the old value", i);
                    }
                }
            }
        }
        if (mapping != MAP_FAILED)
        {
            munmap(mapping, 4 * BUSTER_CBUILTIN_RMW_TEST_PAGE);
        }
    }
}

typedef struct CBuiltinAtomicRmwTestWorker CBuiltinAtomicRmwTestWorker;
struct CBuiltinAtomicRmwTestWorker
{
    const CBuiltinAtomicRmwTestCase* item;
    uint64_t* counter;
    uint64_t tickets[BUSTER_CBUILTIN_RMW_TEST_ROUNDS];
    uint32_t path;
};

static void* cbuiltin_atomic_rmw_test_worker(void* opaque)
{
    CBuiltinAtomicRmwTestWorker* worker = opaque;
    for (uint32_t i = 0; i < BUSTER_CBUILTIN_RMW_TEST_ROUNDS; i += 1)
    {
        worker->tickets[i] = cbuiltin_atomic_rmw_test_invoke(worker->item, worker->path, 2, worker->counter, NULL, 0, 1, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
    }
    return NULL;
}

typedef struct CBuiltinAtomicRmwTestExchange CBuiltinAtomicRmwTestExchange;
struct CBuiltinAtomicRmwTestExchange
{
    const CBuiltinAtomicRmwTestCase* item;
    uint64_t payload;
    uint64_t ready;
    uint64_t acknowledged;
    uint32_t failed;
    uint32_t path;
};

static bool cbuiltin_atomic_rmw_test_wait(CBuiltinAtomicRmwTestExchange* exchange, uint64_t* flag, uint64_t wanted)
{
    bool result = false;
    for (uint32_t spin = 0; !result && spin < BUSTER_CBUILTIN_RMW_TEST_SPINS && __atomic_load_n(&exchange->failed, __ATOMIC_RELAXED) == 0; spin += 1)
    {
        result = __atomic_load_n(flag, __ATOMIC_ACQUIRE) == wanted;
        __builtin_ia32_pause();
        if ((spin & 255) == 255)
        {
            sched_yield();
        }
    }
    if (!result)
    {
        __atomic_store_n(&exchange->failed, 1, __ATOMIC_RELAXED);
    }
    return result;
}

static void* cbuiltin_atomic_rmw_test_sender(void* opaque)
{
    CBuiltinAtomicRmwTestExchange* exchange = opaque;
    bool running = true;
    for (uint64_t i = 1; running && i <= BUSTER_CBUILTIN_RMW_TEST_ROUNDS; i += 1)
    {
        running = cbuiltin_atomic_rmw_test_wait(exchange, &exchange->acknowledged, i - 1);
        if (running)
        {
            // Relaxed (a plain mov on x86): the generated release orders it, which a detector can't see.
            __atomic_store_n(&exchange->payload, i * UINT64_C(0x102030405), __ATOMIC_RELAXED);
            cbuiltin_atomic_rmw_test_invoke(exchange->item, exchange->path, 2, &exchange->ready, NULL, 0, i, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void cbuiltin_atomic_rmw_test_threads(CBuiltinAtomicRmwTest* test, const CBuiltinAtomicRmwTestCase* items, uint32_t count)
{
    const CBuiltinAtomicRmwTestCase* fetch = NULL;
    const CBuiltinAtomicRmwTestCase* swap = NULL;
    for (uint32_t i = 0; i < count; i += 1)
    {
        if (items[i].bytes == 8 && items[i].format == CBUILTIN_ATOMIC_RMW_INTEGER_BITS)
        {
            if (items[i].spec.id == CBUILTIN_ATOMIC_FETCH_ADD)
            {
                fetch = items + i;
            }
            if (items[i].spec.id == CBUILTIN_ATOMIC_EXCHANGE_N)
            {
                swap = items + i;
            }
        }
    }
    if (cbuiltin_atomic_rmw_test_expect(test, fetch != NULL && swap != NULL, "thread fixture has real generated operations", 0))
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            uint64_t counter = 0;
            CBuiltinAtomicRmwTestWorker workers[BUSTER_CBUILTIN_RMW_TEST_THREADS] = { 0 };
            pthread_t threads[BUSTER_CBUILTIN_RMW_TEST_THREADS];
            uint32_t created = 0;
            for (uint32_t i = 0; created == i && i < BUSTER_CBUILTIN_RMW_TEST_THREADS; i += 1)
            {
                workers[i] = (CBuiltinAtomicRmwTestWorker){ .item = fetch, .counter = &counter, .path = path };
                created += pthread_create(threads + i, NULL, cbuiltin_atomic_rmw_test_worker, workers + i) == 0;
            }
            const uint64_t total = BUSTER_CBUILTIN_RMW_TEST_THREADS * BUSTER_CBUILTIN_RMW_TEST_ROUNDS;
            uint64_t seen[(BUSTER_CBUILTIN_RMW_TEST_THREADS * BUSTER_CBUILTIN_RMW_TEST_ROUNDS + 63) / 64] = { 0 };
            bool unique = created == BUSTER_CBUILTIN_RMW_TEST_THREADS;
            for (uint32_t i = 0; i < created; i += 1)
            {
                const bool joined = pthread_join(threads[i], NULL) == 0;
                if (!cbuiltin_atomic_rmw_test_expect(test, joined, "contended worker joined before its stack storage is read or reused", i))
                {
                    _exit(2);
                }
                for (uint32_t r = 0; r < BUSTER_CBUILTIN_RMW_TEST_ROUNDS; r += 1)
                {
                    const uint64_t ticket = workers[i].tickets[r];
                    const uint64_t bit = UINT64_C(1) << (ticket & 63);
                    const bool fresh = ticket < total && (seen[ticket >> 6] & bit) == 0;
                    unique &= fresh;
                    if (fresh)
                    {
                        seen[ticket >> 6] |= bit;
                    }
                }
            }
            cbuiltin_atomic_rmw_test_expect(test, unique && counter == total,
                "contended generated fetch-add returns each successful old value", path);
            CBuiltinAtomicRmwTestExchange exchange = { .item = swap, .path = path };
            pthread_t sender;
            const bool started = pthread_create(&sender, NULL, cbuiltin_atomic_rmw_test_sender, &exchange) == 0;
            bool running = started;
            for (uint64_t i = 1; running && i <= BUSTER_CBUILTIN_RMW_TEST_ROUNDS; i += 1)
            {
                running = cbuiltin_atomic_rmw_test_wait(&exchange, &exchange.ready, i);
                if (running)
                {
                    running = __atomic_load_n(&exchange.payload, __ATOMIC_RELAXED) == i * UINT64_C(0x102030405);
                    if (!running)
                    {
                        __atomic_store_n(&exchange.failed, 1, __ATOMIC_RELAXED);
                    }
                    cbuiltin_atomic_rmw_test_invoke(swap, path, 2, &exchange.acknowledged, NULL, 0, i, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
                }
            }
            const bool finished = started && pthread_join(sender, NULL) == 0;
            if (started && !finished)
            {
                cbuiltin_atomic_rmw_test_expect(test, false, "sender joined before its stack exchange storage is reused", path);
                _exit(2);
            }
            cbuiltin_atomic_rmw_test_expect(test, started && finished && running && __atomic_load_n(&exchange.failed, __ATOMIC_RELAXED) == 0,
                "acknowledged generated exchange release/acquire payload consumer", path);
        }
    }
}

static __attribute__((unused)) int run_builtin_atomic_rmw_helper_tests(int argc, char** argv)
{
    (void)argv;
    CBuiltinAtomicRmwTest test = { 0 };
    bool valid = cbuiltin_atomic_rmw_test_expect(&test, argc == 0, "usage: test-builtin-atomic-rmw-helpers", 0);
    valid = cbuiltin_atomic_rmw_test_expect(&test, __clang_major__ > 23 || (__clang_major__ == 23 && (__clang_minor__ > 1 ||
        (__clang_minor__ == 1 && __clang_patchlevel__ >= 1))), "direct host builtin oracles require Clang 23.1.1 or newer", 0) && valid;
    Arena arena = arena_reserve(UINT64_C(1) << 25);
    CBuiltinAtomicRmwTestCase items[BUSTER_CBUILTIN_RMW_TEST_CASES] = { 0 };
    uint32_t count = 0;
    if (valid)
    {
        valid = cbuiltin_atomic_rmw_test_expect(&test, arena.base != NULL && x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize(), "arena and encoder/Tier 0 initialization", 0);
    }
    X86Target target;
    uint8_t allowed[X86_FORM_COUNT];
    if (valid)
    {
        valid = cbuiltin_atomic_rmw_test_expect(&test, x86_target_parse("x86-64", 6, &target), "baseline generated-code profile", 0);
        if (valid)
        {
            x86_target_forms(&target, allowed);
            cbuiltin_atomic_rmw_test_refusals(&test);
        }
    }
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(cbuiltin_atomic_rmw_test_specs); i += 1)
    {
        const CBuiltinAtomicRmwTestSpec spec = cbuiltin_atomic_rmw_test_specs[i];
        const bool ordered = (spec.flags & BUSTER_CBUILTIN_RMW_TEST_ORDERED) != 0;
        const uint32_t variants = 4 + ((spec.flags & BUSTER_CBUILTIN_RMW_TEST_BOOL) != 0) + (spec.id == CBUILTIN_ATOMIC_EXCHANGE_N ? 2u : 0u) + (ordered ? 4u : 0u);
        for (uint32_t v = 0; valid && v < variants; v += 1)
        {
            valid = cbuiltin_atomic_rmw_test_expect(&test, count < BUSTER_CBUILTIN_RMW_TEST_CASES, "bounded complete helper inventory", i);
            if (valid)
            {
                CBuiltinAtomicRmwTestCase* item = items + count;
                item->spec = spec;
                item->bytes = v < 4 ? 1u << v : v == 4 ? 1 : ordered ? 1u << (v - 5) : v == 5 ? 4 : 8;
                item->format = v < 4 ? CBUILTIN_ATOMIC_RMW_INTEGER_BITS : v == 4 ? CBUILTIN_ATOMIC_RMW_BOOL_BITS :
                    ordered ? CBUILTIN_ATOMIC_RMW_SIGNED_BITS : v == 5 ? CBUILTIN_ATOMIC_RMW_F32_BITS : CBUILTIN_ATOMIC_RMW_F64_BITS;
                valid = cbuiltin_atomic_rmw_test_prepare(&test, &arena, allowed, item, count);
                count += 1;
            }
        }
    }
    cbuiltin_atomic_rmw_test_expect(&test, !valid || count == BUSTER_CBUILTIN_RMW_TEST_CASES, "all 34 IDs, four integer widths, 22 bool formats, two FP exchanges and min/max's four signed widths are represented", count);
    CBuiltinAtomicRmwTest* shared = MAP_FAILED;
    if (valid)
    {
        shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        valid = cbuiltin_atomic_rmw_test_expect(&test, shared != MAP_FAILED, "native result accounting mapping", 0);
    }
    if (valid)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            struct sigaction action = { .sa_handler = SIG_DFL };
            sigset_t unblocked;
            const bool signals = sigemptyset(&action.sa_mask) == 0 && sigemptyset(&unblocked) == 0 && sigaddset(&unblocked, SIGALRM) == 0 &&
                sigaction(SIGALRM, &action, NULL) == 0 && sigprocmask(SIG_UNBLOCK, &unblocked, NULL) == 0;
            if (!signals)
            {
                _exit(2);
            }
            alarm(30);
            cbuiltin_atomic_rmw_test_values(shared, items, count);
            cbuiltin_atomic_rmw_test_guards(shared, items, count);
            cbuiltin_atomic_rmw_test_threads(shared, items, count);
            _exit(shared->failures == 0 ? 0 : 1);
        }
        int status = 0;
        pid_t waited = -1;
        if (child > 0)
        {
            do
            {
                waited = waitpid(child, &status, 0);
            } while (waited < 0 && errno == EINTR);
        }
        cbuiltin_atomic_rmw_test_expect(&test, child > 0 && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "alarm-bounded native child completed", (uint32_t)status);
    }
    if (shared != MAP_FAILED)
    {
        test.checks += shared->checks;
        test.failures += shared->failures;
        munmap(shared, sizeof(*shared));
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t variant = 0; variant < 3; variant += 1)
            {
                CBuiltinAtomicRmwTestProgram* program = &items[i].programs[path][variant];
                if (program->mapping != NULL && program->mapping != MAP_FAILED)
                {
                    munmap(program->mapping, program->mapping_bytes);
                }
            }
        }
    }
    arena_release(&arena);
    printf("test-builtin-atomic-rmw-helpers: %" PRIu64 " checks, %" PRIu64 " failures\n", test.checks, test.failures);
    const int result = test.failures != 0;
    return result;
}
