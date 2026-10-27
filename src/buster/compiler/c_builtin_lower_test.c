// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Runtime builtin recipes against Clang's actual builtin operations, independently of c_builtin.c's fold kernels.
// Every recipe is appended by both paths, validated, compiled by both Tier 0 paths and executed in a child. Overflow
// tests cover every ordinary 8/16/32/64 signed/unsigned and _Bool input/destination combination, wrapped bytes and
// untouched surrounding bytes, and 128-bit operands and destinations (__int128's) mixed with 64-bit operands and
// narrower destinations. Guard-page tests exercise exact output tails and transactional capacity failures.
// This tests a typed-stream consumer; it does not claim source checking or C-to-Tier-1 integration.

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_CBUILTIN_LOWER_TEST_ENTRIES (BUSTER_CBUILTIN_LOWER_ENTRIES + 2)
#define BUSTER_CBUILTIN_LOWER_TEST_LOCALS 32u
#define BUSTER_CBUILTIN_LOWER_TEST_INPUTS 192u
#define BUSTER_CBUILTIN_LOWER_TEST_PAGE 4096u

typedef struct CBuiltinLowerTestTally CBuiltinLowerTestTally;
struct CBuiltinLowerTestTally
{
    uint64_t checks;
    uint64_t failures;
    uint32_t a_type;
    uint32_t b_type;
    uint32_t result_type;
};

typedef struct CBuiltinLowerTestStream CBuiltinLowerTestStream;
struct CBuiltinLowerTestStream
{
    uint8_t kinds[BUSTER_CBUILTIN_LOWER_TEST_ENTRIES];
    uint8_t classes[BUSTER_CBUILTIN_LOWER_TEST_ENTRIES];
    uint64_t values[BUSTER_CBUILTIN_LOWER_TEST_ENTRIES];
    uint8_t local_classes[BUSTER_CBUILTIN_LOWER_TEST_LOCALS];
    uint8_t local_flags[BUSTER_CBUILTIN_LOWER_TEST_LOCALS];
    CBuiltinLowerBuffer buffer;
};

typedef struct CBuiltinLowerTestNative CBuiltinLowerTestNative;
struct CBuiltinLowerTestNative
{
    uint64_t result;
    uint8_t bytes[24];
};

static bool cbuiltin_lower_test_expect(CBuiltinLowerTestTally* tally, bool condition, const char* check, uint32_t id, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-lowering: %s: builtin %u types %u/%u/%u item %u\n", check, id, tally->a_type, tally->b_type, tally->result_type, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static uint64_t cbuiltin_lower_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void cbuiltin_lower_test_stream(CBuiltinLowerTestStream* stream, const CBuiltinLowerCall* call)
{
    memset(stream, 0, sizeof(*stream));
    stream->buffer.kinds = stream->kinds;
    stream->buffer.classes = stream->classes;
    stream->buffer.values = stream->values;
    stream->buffer.capacity = BUSTER_CBUILTIN_LOWER_TEST_ENTRIES;
    stream->buffer.local_classes = stream->local_classes;
    stream->buffer.local_flags = stream->local_flags;
    stream->buffer.local_capacity = BUSTER_CBUILTIN_LOWER_TEST_LOCALS;
    const bool overflow = call->id >= CBUILTIN_ADD_OVERFLOW;
    stream->buffer.local_count = overflow ? 3 : 1;
    stream->local_classes[0] = (uint8_t)(overflow ? cbuiltin_lower_type_class(call->a_type) : cbuiltin_lower_parameter_class(call->id));
    if (overflow)
    {
        stream->local_classes[1] = (uint8_t)cbuiltin_lower_type_class(call->b_type);
        stream->local_classes[2] = BUSTER_TYPED_CLASS_U64;
    }
}

static __int128 cbuiltin_lower_test_integer(uint64_t bits, uint32_t type)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    const unsigned __int128 modulus = (unsigned __int128)1 << width;
    const __int128 truncated = bits & (uint64_t)(modulus - 1);
    const __int128 result = (type & BUSTER_CBUILTIN_SIGNED) != 0 && truncated >= (__int128)(modulus / 2) ? truncated - (__int128)modulus : truncated;
    return result;
}

static __attribute__((noinline)) uint64_t cbuiltin_lower_test_unary(CBuiltinId id, uint64_t value)
{
    uint64_t result = 0;
    switch (id)
    {
    case CBUILTIN_BSWAP16: result = __builtin_bswap16(value); break;
    case CBUILTIN_BSWAP32: result = __builtin_bswap32(value); break;
    case CBUILTIN_BSWAP64: result = __builtin_bswap64(value); break;
    case CBUILTIN_CLZ: result = __builtin_clz(value); break;
    case CBUILTIN_CLZL: result = __builtin_clzl(value); break;
    case CBUILTIN_CLZLL: result = __builtin_clzll(value); break;
    case CBUILTIN_CTZ: result = __builtin_ctz(value); break;
    case CBUILTIN_CTZL: result = __builtin_ctzl(value); break;
    case CBUILTIN_CTZLL: result = __builtin_ctzll(value); break;
    case CBUILTIN_POPCOUNT: result = __builtin_popcount(value); break;
    case CBUILTIN_POPCOUNTL: result = __builtin_popcountl(value); break;
    case CBUILTIN_POPCOUNTLL: result = __builtin_popcountll(value); break;
    case CBUILTIN_PARITY: result = __builtin_parity(value); break;
    case CBUILTIN_PARITYL: result = __builtin_parityl(value); break;
    case CBUILTIN_PARITYLL: result = __builtin_parityll(value); break;
    case CBUILTIN_FFS: result = __builtin_ffs(value); break;
    case CBUILTIN_FFSL: result = __builtin_ffsl(value); break;
    case CBUILTIN_FFSLL: result = __builtin_ffsll(value); break;
    default: break;
    }
    return result;
}

// Using signed __int128 inputs makes every source value exact, including unsigned64. Clang owns both the operation
// and the destination conversion; the recipe's split multiplication and carry handling are not reused by this oracle.
static __attribute__((noinline)) uint32_t cbuiltin_lower_test_overflow(CBuiltinId id, __int128 a, __int128 b, uint32_t type, uint64_t* stored)
{
    uint32_t result = 0;
#define BUSTER_CBUILTIN_LOWER_ORACLE(T) do { \
    T destination; \
    if (id == CBUILTIN_ADD_OVERFLOW) { result = __builtin_add_overflow(a, b, &destination); } \
    else if (id == CBUILTIN_SUB_OVERFLOW) { result = __builtin_sub_overflow(a, b, &destination); } \
    else { result = __builtin_mul_overflow(a, b, &destination); } \
    *stored = (uint64_t)destination; \
} while (0)
#define BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH(N) case N: \
    if ((type & BUSTER_CBUILTIN_SIGNED) != 0) { BUSTER_CBUILTIN_LOWER_ORACLE(int##N##_t); } \
    else { BUSTER_CBUILTIN_LOWER_ORACLE(uint##N##_t); } \
    break;
    switch (type & ~BUSTER_CBUILTIN_SIGNED)
    {
    case 1:
        BUSTER_CBUILTIN_LOWER_ORACLE(_Bool);
        break;
        BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH(8)
        BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH(16)
        BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH(32)
        BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH(64)
    }
#undef BUSTER_CBUILTIN_LOWER_ORACLE_WIDTH
#undef BUSTER_CBUILTIN_LOWER_ORACLE
    return result;
}

static uint64_t cbuiltin_lower_test_input(uint32_t index, uint32_t type, uint64_t* state)
{
    const uint32_t width = type & ~BUSTER_CBUILTIN_SIGNED;
    const uint64_t sign = UINT64_C(1) << (width - 1);
    const uint64_t mask = UINT64_MAX >> (64 - width);
    const uint64_t boundaries[12] =
    {
        0,
        1,
        2,
        3,
        mask,
        mask - 1,
        sign,
        sign - 1,
        sign + 1,
        UINT64_C(0x5555555555555555),
        UINT64_C(0xaaaaaaaaaaaaaaaa),
        UINT64_C(0x8000000000000001),
    };
    const uint64_t result = index < 12 ? boundaries[index] : cbuiltin_lower_test_random(state);
    return result;
}

static bool cbuiltin_lower_test_same_code(const Tier0Code* a, const Tier0Code* b)
{
    bool result = a->error == TIER0_OK && b->error == TIER0_OK && a->count == b->count && a->branch_count == b->branch_count &&
                  a->reference_count == 0 && b->reference_count == 0 && a->deferred == b->deferred;
    if (result)
    {
        result = memcmp(a->records, b->records, a->count * sizeof(X86Instruction)) == 0 &&
                 memcmp(a->branches, b->branches, a->branch_count * sizeof(uint32_t)) == 0 && memcmp(a->targets, b->targets, a->branch_count * sizeof(uint32_t)) == 0;
    }
    return result;
}

static void cbuiltin_lower_test_case(CBuiltinLowerTestTally* tally, Arena* arena, CBuiltinLowerCall call, uint64_t* state)
{
    tally->a_type = call.a_type;
    tally->b_type = call.b_type;
    tally->result_type = call.result_type;
    CBuiltinLowerTestStream scalar_stream;
    CBuiltinLowerTestStream vector_stream;
    cbuiltin_lower_test_stream(&scalar_stream, &call);
    cbuiltin_lower_test_stream(&vector_stream, &call);
    const CBuiltinLowerResult scalar_lowered = cbuiltin_lower_scalar(&scalar_stream.buffer, &call);
    const CBuiltinLowerResult vector_lowered = cbuiltin_lower(&vector_stream.buffer, &call);
    bool ok = cbuiltin_lower_test_expect(tally, scalar_lowered.error == CBUILTIN_LOWER_OK && memcmp(&scalar_lowered, &vector_lowered, sizeof(scalar_lowered)) == 0,
                                         "lower results", call.id, call.result_type);
    ok = cbuiltin_lower_test_expect(tally, ok && scalar_stream.buffer.count == vector_stream.buffer.count &&
                                    memcmp(scalar_stream.kinds, vector_stream.kinds, scalar_stream.buffer.count) == 0 &&
                                    memcmp(scalar_stream.classes, vector_stream.classes, scalar_stream.buffer.count) == 0 &&
                                    memcmp(scalar_stream.values, vector_stream.values, scalar_stream.buffer.count * sizeof(uint64_t)) == 0 &&
                                    scalar_stream.buffer.local_count == vector_stream.buffer.local_count &&
                                    memcmp(scalar_stream.local_classes, vector_stream.local_classes, scalar_stream.buffer.local_count) == 0 &&
                                    memcmp(scalar_stream.local_flags, vector_stream.local_flags, scalar_stream.buffer.local_count) == 0,
                                    "lower twins", call.id, call.result_type) && ok;
    CBuiltinLowerBuffer* buffer = &scalar_stream.buffer;
    if (ok)
    {
        scalar_stream.kinds[buffer->count] = BUSTER_TYPED_CONVERT;
        scalar_stream.classes[buffer->count] = BUSTER_TYPED_CLASS_U64;
        scalar_stream.values[buffer->count] = 0;
        buffer->count += 1;
        scalar_stream.kinds[buffer->count] = BUSTER_TYPED_RETURN;
        scalar_stream.classes[buffer->count] = BUSTER_TYPED_CLASS_U64;
        scalar_stream.values[buffer->count] = 0;
        buffer->count += 1;
    }
    AbiLocation parameters[3] =
    {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    Tier0Function function =
    {
        .kinds = buffer->kinds,
        .classes = buffer->classes,
        .values = buffer->values,
        .count = buffer->count,
        .local_classes = buffer->local_classes,
        .local_flags = buffer->local_flags,
        .local_count = buffer->local_count,
        .parameter_count = call.id >= CBUILTIN_ADD_OVERFLOW ? 3 : 1,
        .parameters = parameters,
        .result =
        {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
        },
    };
    const uint64_t mark = arena->length;
    const Tier0Check checked = tier0_validate(&function);
    ok = cbuiltin_lower_test_expect(tally, ok && checked.error == TIER0_OK, checked.rule != 0 ? checked.rule : "typed validation", call.id, checked.node) && ok;
    Tier0Code scalar = ok ? tier0_compile_scalar(&function, arena) : (Tier0Code){ 0 };
    Tier0Code vector = ok ? tier0_compile(&function, arena) : (Tier0Code){ 0 };
    ok = cbuiltin_lower_test_expect(tally, ok && cbuiltin_lower_test_same_code(&scalar, &vector), "Tier 0 twins", call.id, scalar.node) && ok;
    uint8_t* code = MAP_FAILED;
    uint64_t code_pages = 0;
    CBuiltinLowerTestNative* native = MAP_FAILED;
    uint64_t a[BUSTER_CBUILTIN_LOWER_TEST_INPUTS];
    uint64_t b[BUSTER_CBUILTIN_LOWER_TEST_INPUTS];
    const bool overflow = call.id >= CBUILTIN_ADD_OVERFLOW;
    for (uint32_t i = 0; i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
    {
        const uint32_t at = overflow ? call.a_type : tier0_class_bytes(cbuiltin_lower_parameter_class(call.id)) * 8;
        a[i] = cbuiltin_lower_test_input(overflow && i < 144 ? i / 12 : i, at, state);
        b[i] = cbuiltin_lower_test_input(overflow && i < 144 ? i % 12 : i, overflow ? call.b_type : 64, state);
        if (!overflow && call.id >= CBUILTIN_CLZ && call.id <= CBUILTIN_CTZLL && (a[i] & cbuiltin_mask(at)) == 0)
        {
            a[i] = 1;
        }
    }
    if (ok)
    {
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
        code_pages = (capacity + BUSTER_CBUILTIN_LOWER_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_LOWER_TEST_PAGE - 1);
        code = mmap(0, code_pages + BUSTER_CBUILTIN_LOWER_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        native = mmap(0, sizeof(*native) * BUSTER_CBUILTIN_LOWER_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1);
        ok = cbuiltin_lower_test_expect(tally, code != MAP_FAILED && native != MAP_FAILED && offsets != 0, "native memory", call.id, 0);
        if (ok)
        {
            const BranchSizing sized = size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets);
            const int64_t encoded = sized.error == BRANCH_SIZING_OK ? x86_encode(scalar.records, scalar.count, code, capacity, 0, 0) : -1;
            ok = cbuiltin_lower_test_expect(tally, encoded >= 0 && mprotect(code, code_pages, PROT_READ | PROT_EXEC) == 0 &&
                                            mprotect(code + code_pages, BUSTER_CBUILTIN_LOWER_TEST_PAGE, PROT_NONE) == 0, "native encoding", call.id, 0);
        }
    }
    if (ok)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            uint64_t (*entry)(uint64_t, uint64_t, uint64_t*) = (uint64_t (*)(uint64_t, uint64_t, uint64_t*))(void*)code;
            for (uint32_t i = 0; i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
            {
                memset(native[i].bytes, 0xa5, sizeof(native[i].bytes));
                native[i].result = entry(a[i], b[i], (uint64_t*)(void*)(native[i].bytes + 8));
            }
            _exit(0);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        ok = cbuiltin_lower_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child", call.id, (uint32_t)status);
    }
    for (uint32_t i = 0; ok && i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
    {
        uint8_t expected_bytes[24];
        memset(expected_bytes, 0xa5, sizeof(expected_bytes));
        uint64_t expected;
        if (overflow)
        {
            uint64_t stored = 0;
            expected = cbuiltin_lower_test_overflow(call.id, cbuiltin_lower_test_integer(a[i], call.a_type), cbuiltin_lower_test_integer(b[i], call.b_type), call.result_type, &stored);
            memcpy(expected_bytes + 8, &stored, ((call.result_type & ~BUSTER_CBUILTIN_SIGNED) + 7) / 8);
        }
        else
        {
            expected = cbuiltin_lower_test_unary(call.id, a[i]);
        }
        cbuiltin_lower_test_expect(tally, native[i].result == expected, "native result", call.id, i);
        cbuiltin_lower_test_expect(tally, memcmp(native[i].bytes, expected_bytes, sizeof(expected_bytes)) == 0, "destination bytes", call.id, i);
    }
    if (native != MAP_FAILED)
    {
        munmap(native, sizeof(*native) * BUSTER_CBUILTIN_LOWER_TEST_INPUTS);
    }
    if (code != MAP_FAILED)
    {
        munmap(code, code_pages + BUSTER_CBUILTIN_LOWER_TEST_PAGE);
    }
    arena_pop_to(arena, mark);
}

// 128-bit operands and destinations (BUSTER_CBUILTIN_LOWER_WIDE): clang's own builtin on the operands' and the
// destination's types, each operand passed as its 128-bit pattern, and the destination's value as one.
#define BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, R) do { \
    R destination; \
    if (id == CBUILTIN_ADD_OVERFLOW) { overflow = __builtin_add_overflow((A)a, (B)b, &destination); } \
    else if (id == CBUILTIN_SUB_OVERFLOW) { overflow = __builtin_sub_overflow((A)a, (B)b, &destination); } \
    else { overflow = __builtin_mul_overflow((A)a, (B)b, &destination); } \
    *stored = (unsigned __int128)destination; \
} while (0)
#define BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT(A, B) switch (result_type) { \
    case 1: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, _Bool); break; \
    case 8 | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, int8_t); break; \
    case 32: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, uint32_t); break; \
    case 64: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, uint64_t); break; \
    case 64 | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, int64_t); break; \
    case BUSTER_CBUILTIN_LOWER_WIDE: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, unsigned __int128); break; \
    case BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE(A, B, __int128); break; \
    default: break; }
#define BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND(A) switch (b_type) { \
    case 64: BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT(A, uint64_t); break; \
    case 64 | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT(A, int64_t); break; \
    case BUSTER_CBUILTIN_LOWER_WIDE: BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT(A, unsigned __int128); break; \
    case BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT(A, __int128); break; \
    default: break; }

static const uint32_t cbuiltin_lower_test_wide_operands[4] =
{
    64,
    64 | BUSTER_CBUILTIN_SIGNED,
    BUSTER_CBUILTIN_LOWER_WIDE,
    BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED,
};

static const uint32_t cbuiltin_lower_test_wide_results[7] =
{
    1,
    8 | BUSTER_CBUILTIN_SIGNED,
    32,
    64,
    64 | BUSTER_CBUILTIN_SIGNED,
    BUSTER_CBUILTIN_LOWER_WIDE,
    BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED,
};

static __attribute__((noinline)) uint32_t cbuiltin_lower_test_wide_oracle(CBuiltinId id, unsigned __int128 a, uint32_t a_type, unsigned __int128 b, uint32_t b_type,
                                                                           uint32_t result_type, unsigned __int128* stored)
{
    uint32_t overflow = 0;
    switch (a_type)
    {
    case 64: BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND(uint64_t); break;
    case 64 | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND(int64_t); break;
    case BUSTER_CBUILTIN_LOWER_WIDE: BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND(unsigned __int128); break;
    case BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED: BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND(__int128); break;
    default: break;
    }
    return overflow;
}

#undef BUSTER_CBUILTIN_LOWER_TEST_WIDE_SECOND
#undef BUSTER_CBUILTIN_LOWER_TEST_WIDE_RESULT
#undef BUSTER_CBUILTIN_LOWER_TEST_WIDE

// An operand's pattern, as 128 bits: a 64-bit one's input extended by its signedness, a 128-bit one's boundaries
// (the halves' carries and signs among them) and random values.
static unsigned __int128 cbuiltin_lower_test_wide_input(uint32_t index, uint32_t type, uint64_t* state)
{
    unsigned __int128 result = 0;
    if (!cbuiltin_lower_wide(type))
    {
        const uint64_t bits = cbuiltin_lower_test_input(index, type, state);
        result = (type & BUSTER_CBUILTIN_SIGNED) != 0 ? (unsigned __int128)(__int128)(int64_t)bits : bits;
    }
    else
    {
        const unsigned __int128 one = 1;
        const unsigned __int128 boundaries[16] =
        {
            0,
            1,
            2,
            ~(unsigned __int128)0,
            ~(unsigned __int128)0 - 1,
            one << 127,
            (one << 127) - 1,
            (one << 127) + 1,
            one << 64,
            (one << 64) - 1,
            (one << 64) + 1,
            one << 63,
            ~(unsigned __int128)0 << 63,
            ((unsigned __int128)UINT64_C(0x5555555555555555) << 64) | UINT64_C(0x5555555555555555),
            ((unsigned __int128)UINT64_C(0xaaaaaaaaaaaaaaaa) << 64) | UINT64_C(0xaaaaaaaaaaaaaaaa),
            (one << 96) | 3,
        };
        const uint64_t high = cbuiltin_lower_test_random(state);
        const uint64_t low = cbuiltin_lower_test_random(state);
        // Random values of every magnitude: their high halves shifted down so that some products fit.
        result = index < 16 ? boundaries[index] : ((unsigned __int128)(high >> (index % 64)) << 64) | low;
    }
    return result;
}

// One overflow builtin with a 128-bit operand or destination, as a native (const void* a, const void* b, void* out):
// each operand loaded from its pointer into its slot (a 128-bit one's an object of 16 bytes), then the recipe.
static void cbuiltin_lower_test_wide_case(CBuiltinLowerTestTally* tally, Arena* arena, CBuiltinLowerCall call, uint64_t* state)
{
    tally->a_type = call.a_type;
    tally->b_type = call.b_type;
    tally->result_type = call.result_type;
    uint32_t local_sizes[BUSTER_CBUILTIN_LOWER_TEST_LOCALS] = { 0 };
    uint8_t local_alignments[BUSTER_CBUILTIN_LOWER_TEST_LOCALS] = { 0 };
    CBuiltinLowerTestStream streams[2];
    CBuiltinLowerResult lowered[2];
    const uint32_t types[2] = { call.a_type, call.b_type };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CBuiltinLowerTestStream* stream = &streams[path];
        cbuiltin_lower_test_stream(stream, &call);
        // Locals 0 to 2: the pointers; 3 and 4: the operands' slots.
        stream->buffer.local_count = 5;
        for (uint32_t i = 0; i < 3; i += 1)
        {
            stream->local_classes[i] = BUSTER_TYPED_CLASS_U64;
        }
        for (uint32_t i = 0; i < 2; i += 1)
        {
            const bool wide = cbuiltin_lower_wide(types[i]);
            const uint32_t class = wide ? ((types[i] & BUSTER_CBUILTIN_SIGNED) != 0 ? BUSTER_TYPED_CLASS_I128 : BUSTER_TYPED_CLASS_U128) : cbuiltin_lower_type_class(types[i]);
            stream->local_classes[3 + i] = wide ? BUSTER_TYPED_CLASS_VOID : (uint8_t)class;
            stream->local_flags[3 + i] = wide ? BUSTER_TYPED_LOCAL_ADDRESS_TAKEN : 0;
            local_sizes[3 + i] = wide ? 16 : 0;
            local_alignments[3 + i] = wide ? 4 : 0;
            const uint32_t at = stream->buffer.count;
            stream->kinds[at] = BUSTER_TYPED_LOCAL;
            stream->classes[at] = BUSTER_TYPED_CLASS_U64;
            stream->values[at] = i;
            stream->kinds[at + 1] = BUSTER_TYPED_LOAD;
            stream->classes[at + 1] = (uint8_t)class;
            stream->values[at + 1] = 0;
            stream->kinds[at + 2] = BUSTER_TYPED_SET_LOCAL;
            stream->classes[at + 2] = (uint8_t)class;
            stream->values[at + 2] = 3 + i;
            stream->buffer.count += 3;
        }
        CBuiltinLowerCall placed = call;
        placed.a = 3;
        placed.b = 4;
        placed.destination = 2;
        lowered[path] = path == 0 ? cbuiltin_lower_scalar(&stream->buffer, &placed) : cbuiltin_lower(&stream->buffer, &placed);
    }
    bool ok = cbuiltin_lower_test_expect(tally, lowered[0].error == CBUILTIN_LOWER_OK && memcmp(&lowered[0], &lowered[1], sizeof(lowered[0])) == 0,
                                         "wide lower results", call.id, call.result_type);
    const CBuiltinLowerTestStream* scalar_stream = &streams[0];
    const CBuiltinLowerTestStream* vector_stream = &streams[1];
    ok = cbuiltin_lower_test_expect(tally, ok && scalar_stream->buffer.count == vector_stream->buffer.count &&
                                    memcmp(scalar_stream->kinds, vector_stream->kinds, scalar_stream->buffer.count) == 0 &&
                                    memcmp(scalar_stream->classes, vector_stream->classes, scalar_stream->buffer.count) == 0 &&
                                    memcmp(scalar_stream->values, vector_stream->values, scalar_stream->buffer.count * sizeof(uint64_t)) == 0 &&
                                    scalar_stream->buffer.local_count == vector_stream->buffer.local_count &&
                                    memcmp(scalar_stream->local_classes, vector_stream->local_classes, scalar_stream->buffer.local_count) == 0 &&
                                    memcmp(scalar_stream->local_flags, vector_stream->local_flags, scalar_stream->buffer.local_count) == 0,
                                    "wide lower twins", call.id, call.result_type) && ok;
    CBuiltinLowerBuffer* buffer = &streams[0].buffer;
    bool int128 = false;
    if (ok)
    {
        for (uint32_t i = 0; i < buffer->count; i += 1)
        {
            int128 |= buffer->classes[i] == BUSTER_TYPED_CLASS_I128 || buffer->classes[i] == BUSTER_TYPED_CLASS_U128;
        }
        streams[0].kinds[buffer->count] = BUSTER_TYPED_CONVERT;
        streams[0].classes[buffer->count] = BUSTER_TYPED_CLASS_U64;
        streams[0].values[buffer->count] = 0;
        buffer->count += 1;
        streams[0].kinds[buffer->count] = BUSTER_TYPED_RETURN;
        streams[0].classes[buffer->count] = BUSTER_TYPED_CLASS_U64;
        streams[0].values[buffer->count] = 0;
        buffer->count += 1;
    }
    AbiLocation parameters[3] =
    {
        { .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
        { .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE }, .kind = BUSTER_ABI_LOCATION_REGISTERS },
    };
    Tier0Function function =
    {
        .kinds = buffer->kinds,
        .classes = buffer->classes,
        .values = buffer->values,
        .count = buffer->count,
        .local_classes = buffer->local_classes,
        .local_flags = buffer->local_flags,
        .local_count = buffer->local_count,
        .local_sizes = local_sizes,
        .local_alignments = local_alignments,
        .parameter_count = 3,
        .parameters = parameters,
        .result =
        {
            .kind = BUSTER_ABI_RESULT_REGISTERS,
            .part_bytes = 8,
            .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
        },
        .int128 = int128,
    };
    const uint64_t mark = arena->length;
    const Tier0Check checked = tier0_validate(&function);
    ok = cbuiltin_lower_test_expect(tally, ok && checked.error == TIER0_OK, checked.rule != 0 ? checked.rule : "wide typed validation", call.id, checked.node) && ok;
    Tier0Code scalar = ok ? tier0_compile_scalar(&function, arena) : (Tier0Code){ 0 };
    Tier0Code vector = ok ? tier0_compile(&function, arena) : (Tier0Code){ 0 };
    ok = cbuiltin_lower_test_expect(tally, ok && cbuiltin_lower_test_same_code(&scalar, &vector), "wide Tier 0 twins", call.id, scalar.node) && ok;
    uint8_t* code = MAP_FAILED;
    uint64_t code_pages = 0;
    CBuiltinLowerTestNative* native = MAP_FAILED;
    unsigned __int128 a[BUSTER_CBUILTIN_LOWER_TEST_INPUTS];
    unsigned __int128 b[BUSTER_CBUILTIN_LOWER_TEST_INPUTS];
    for (uint32_t i = 0; i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
    {
        a[i] = cbuiltin_lower_test_wide_input(i < 144 ? i / 12 : i, call.a_type, state);
        b[i] = cbuiltin_lower_test_wide_input(i < 144 ? i % 12 : i, call.b_type, state);
    }
    if (ok)
    {
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(scalar.count);
        code_pages = (capacity + BUSTER_CBUILTIN_LOWER_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_LOWER_TEST_PAGE - 1);
        code = mmap(0, code_pages + BUSTER_CBUILTIN_LOWER_TEST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        native = mmap(0, sizeof(*native) * BUSTER_CBUILTIN_LOWER_TEST_INPUTS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)scalar.count + 1);
        ok = cbuiltin_lower_test_expect(tally, code != MAP_FAILED && native != MAP_FAILED && offsets != 0, "wide native memory", call.id, 0);
        if (ok)
        {
            const BranchSizing sized = size_branches(scalar.records, scalar.count, scalar.branches, scalar.targets, scalar.branch_count, offsets);
            const int64_t encoded = sized.error == BRANCH_SIZING_OK ? x86_encode(scalar.records, scalar.count, code, capacity, 0, 0) : -1;
            ok = cbuiltin_lower_test_expect(tally, encoded >= 0 && mprotect(code, code_pages, PROT_READ | PROT_EXEC) == 0 &&
                                            mprotect(code + code_pages, BUSTER_CBUILTIN_LOWER_TEST_PAGE, PROT_NONE) == 0, "wide native encoding", call.id, 0);
        }
    }
    if (ok)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            uint64_t (*entry)(const void*, const void*, void*) = (uint64_t (*)(const void*, const void*, void*))(void*)code;
            for (uint32_t i = 0; i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
            {
                // An operand's bytes as its type holds them: the low ones of its pattern.
                memset(native[i].bytes, 0xa5, sizeof(native[i].bytes));
                native[i].result = entry(&a[i], &b[i], native[i].bytes + 4);
            }
            _exit(0);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        ok = cbuiltin_lower_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "wide native child", call.id, (uint32_t)status);
    }
    const uint32_t result_bytes = cbuiltin_lower_wide(call.result_type) ? 16 : ((call.result_type & ~BUSTER_CBUILTIN_SIGNED) + 7) / 8;
    for (uint32_t i = 0; ok && i < BUSTER_CBUILTIN_LOWER_TEST_INPUTS; i += 1)
    {
        uint8_t expected_bytes[24];
        memset(expected_bytes, 0xa5, sizeof(expected_bytes));
        unsigned __int128 stored = 0;
        const uint64_t expected = cbuiltin_lower_test_wide_oracle(call.id, a[i], call.a_type, b[i], call.b_type, call.result_type, &stored);
        memcpy(expected_bytes + 4, &stored, result_bytes);
        cbuiltin_lower_test_expect(tally, native[i].result == expected, "wide native result", call.id, i);
        cbuiltin_lower_test_expect(tally, memcmp(native[i].bytes, expected_bytes, sizeof(expected_bytes)) == 0, "wide destination bytes", call.id, i);
    }
    if (native != MAP_FAILED)
    {
        munmap(native, sizeof(*native) * BUSTER_CBUILTIN_LOWER_TEST_INPUTS);
    }
    if (code != MAP_FAILED)
    {
        munmap(code, code_pages + BUSTER_CBUILTIN_LOWER_TEST_PAGE);
    }
    arena_pop_to(arena, mark);
}

static void cbuiltin_lower_test_guards(CBuiltinLowerTestTally* tally)
{
    const CBuiltinLowerCall call =
    {
        .id = CBUILTIN_MUL_OVERFLOW,
        .a = 0,
        .b = 1,
        .destination = 2,
        .a_type = 64,
        .b_type = 64 | BUSTER_CBUILTIN_SIGNED,
        .result_type = 8 | BUSTER_CBUILTIN_SIGNED,
    };
    CBuiltinLowerTestStream stream;
    cbuiltin_lower_test_stream(&stream, &call);
    const CBuiltinLowerResult expected = cbuiltin_lower_scalar(&stream.buffer, &call);
    cbuiltin_lower_test_expect(tally, expected.error == CBUILTIN_LOWER_OK, "guard recipe", call.id, 0);
    uint8_t* pages[5];
    bool mapped = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        pages[i] = mmap(0, BUSTER_CBUILTIN_LOWER_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped &= pages[i] != MAP_FAILED;
        if (pages[i] != MAP_FAILED)
        {
            mapped &= mprotect(pages[i] + BUSTER_CBUILTIN_LOWER_TEST_PAGE, BUSTER_CBUILTIN_LOWER_TEST_PAGE, PROT_NONE) == 0;
        }
    }
    if (cbuiltin_lower_test_expect(tally, mapped, "guard memory", call.id, 0))
    {
        for (uint32_t variant = 0; variant < 4; variant += 1)
        {
            const uint32_t entry_capacity = expected.count + 2;
            const uint32_t local_capacity = 3 + expected.temporary_count;
            CBuiltinLowerBuffer buffer =
            {
                .kinds = pages[0] + BUSTER_CBUILTIN_LOWER_TEST_PAGE - entry_capacity,
                .classes = pages[1] + BUSTER_CBUILTIN_LOWER_TEST_PAGE - entry_capacity,
                .values = (uint64_t*)(void*)(pages[2] + BUSTER_CBUILTIN_LOWER_TEST_PAGE - entry_capacity * sizeof(uint64_t)),
                .count = 2,
                .capacity = entry_capacity - (variant == 1),
                .local_classes = pages[3] + BUSTER_CBUILTIN_LOWER_TEST_PAGE - local_capacity,
                .local_flags = pages[4] + BUSTER_CBUILTIN_LOWER_TEST_PAGE - local_capacity,
                .local_count = 3,
                .local_capacity = local_capacity - (variant == 2),
            };
            for (uint32_t i = 0; i < 5; i += 1)
            {
                memset(pages[i], 0xa5, BUSTER_CBUILTIN_LOWER_TEST_PAGE);
            }
            buffer.local_classes[0] = BUSTER_TYPED_CLASS_U64;
            buffer.local_classes[1] = BUSTER_TYPED_CLASS_I64;
            buffer.local_classes[2] = BUSTER_TYPED_CLASS_U64;
            if (variant == 3)
            {
                buffer.local_classes[2] = BUSTER_TYPED_CLASS_I32;
            }
            uint8_t snapshots[5][BUSTER_CBUILTIN_LOWER_TEST_PAGE];
            for (uint32_t i = 0; i < 5; i += 1)
            {
                memcpy(snapshots[i], pages[i], BUSTER_CBUILTIN_LOWER_TEST_PAGE);
            }
            const CBuiltinLowerResult lowered = cbuiltin_lower(&buffer, &call);
            if (variant == 0)
            {
                cbuiltin_lower_test_expect(tally, lowered.error == CBUILTIN_LOWER_OK && lowered.first == 2 && lowered.count == expected.count &&
                                            buffer.count == entry_capacity && buffer.local_count == local_capacity && buffer.kinds[0] == 0xa5 && buffer.kinds[1] == 0xa5,
                                            "exact guarded append", call.id, variant);
            }
            else
            {
                const CBuiltinLowerError error = variant == 3 ? CBUILTIN_LOWER_ARGUMENT : CBUILTIN_LOWER_CAPACITY;
                bool untouched = lowered.error == error && buffer.count == 2 && buffer.local_count == 3;
                for (uint32_t i = 0; i < 5; i += 1)
                {
                    untouched &= memcmp(snapshots[i], pages[i], BUSTER_CBUILTIN_LOWER_TEST_PAGE) == 0;
                }
                cbuiltin_lower_test_expect(tally, untouched, "transactional refusal", call.id, variant);
            }
        }
    }
    for (uint32_t i = 0; i < 5; i += 1)
    {
        if (pages[i] != MAP_FAILED)
        {
            munmap(pages[i], BUSTER_CBUILTIN_LOWER_TEST_PAGE * 2);
        }
    }
}

// Descriptors with BUSTER_CBUILTIN_LOWER_WIDE beside other bits, as an operand's (its slot a 128-bit operand's object,
// which the recipe would take) or the destination's, by both publication paths: each refused as unsupported, with
// nothing appended and no local claimed. The flag alone, signed or not, says 128 bits (#533's review).
static void cbuiltin_lower_test_wide_refusals(CBuiltinLowerTestTally* tally)
{
    const uint32_t malformed[4] =
    {
        BUSTER_CBUILTIN_LOWER_WIDE | 1,
        BUSTER_CBUILTIN_LOWER_WIDE | 64,
        BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED | 64,
        BUSTER_CBUILTIN_LOWER_WIDE | BUSTER_CBUILTIN_SIGNED | 8,
    };
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(malformed); m += 1)
    {
        for (uint32_t position = 0; position < 3; position += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                const CBuiltinLowerCall call =
                {
                    .id = CBUILTIN_MUL_OVERFLOW,
                    .a = 0,
                    .b = 1,
                    .destination = 2,
                    .a_type = position == 0 ? malformed[m] : 64,
                    .b_type = position == 1 ? malformed[m] : 64 | BUSTER_CBUILTIN_SIGNED,
                    .result_type = position == 2 ? malformed[m] : 64,
                };
                CBuiltinLowerTestStream stream;
                cbuiltin_lower_test_stream(&stream, &call);
                if (position < 2)
                {
                    stream.local_classes[position] = BUSTER_TYPED_CLASS_VOID;
                    stream.local_flags[position] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
                }
                stream.buffer.count = 2;
                const CBuiltinLowerTestStream before = stream;
                const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_lower_scalar(&stream.buffer, &call) : cbuiltin_lower(&stream.buffer, &call);
                const bool untouched = memcmp(stream.kinds, before.kinds, sizeof(stream.kinds)) == 0 && memcmp(stream.classes, before.classes, sizeof(stream.classes)) == 0 &&
                                       memcmp(stream.values, before.values, sizeof(stream.values)) == 0 &&
                                       memcmp(stream.local_classes, before.local_classes, sizeof(stream.local_classes)) == 0 &&
                                       memcmp(stream.local_flags, before.local_flags, sizeof(stream.local_flags)) == 0;
                cbuiltin_lower_test_expect(tally, lowered.error == CBUILTIN_LOWER_UNSUPPORTED && stream.buffer.count == 2 && stream.buffer.local_count == 3 && untouched,
                                           "malformed wide descriptor refused", call.id, m * 6 + position * 2 + path);
            }
        }
    }
}

static int run_builtin_lower_tests(int argc, char** argv)
{
    uint64_t seed = 1;
    bool valid = true;
    for (int i = 0; valid && i < argc; i += 1)
    {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            seed = strtoull(argv[i + 1], 0, 0);
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "usage: build/compiler test-builtin-lowering [--seed N]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-builtin-lowering: table initialization failed\n");
    }
    else
    {
        CBuiltinLowerTestTally tally = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 28);
        uint64_t state = seed != 0 ? seed : 1;
        cbuiltin_lower_test_guards(&tally);
        cbuiltin_lower_test_wide_refusals(&tally);
        for (uint32_t id = CBUILTIN_BSWAP16; id <= CBUILTIN_FFSLL; id += 1)
        {
            const CBuiltinLowerCall call = { .id = (CBuiltinId)id, .a = 0 };
            cbuiltin_lower_test_case(&tally, &arena, call, &state);
        }
        const uint32_t types[9] =
        {
            1,
            8,
            8 | BUSTER_CBUILTIN_SIGNED,
            16,
            16 | BUSTER_CBUILTIN_SIGNED,
            32,
            32 | BUSTER_CBUILTIN_SIGNED,
            64,
            64 | BUSTER_CBUILTIN_SIGNED,
        };
        for (uint32_t id = CBUILTIN_ADD_OVERFLOW; id <= CBUILTIN_MUL_OVERFLOW; id += 1)
        {
            for (uint32_t at = 0; at < 9; at += 1)
            {
                for (uint32_t bt = 0; bt < 9; bt += 1)
                {
                    for (uint32_t rt = 0; rt < 9; rt += 1)
                    {
                        const CBuiltinLowerCall call =
                        {
                            .id = (CBuiltinId)id,
                            .a = 0,
                            .b = 1,
                            .destination = 2,
                            .a_type = types[at],
                            .b_type = types[bt],
                            .result_type = types[rt],
                        };
                        cbuiltin_lower_test_case(&tally, &arena, call, &state);
                    }
                }
            }
        }
        // 128-bit operands or destinations, mixed with 64-bit ones and narrower destinations, signed and unsigned.
        for (uint32_t id = CBUILTIN_ADD_OVERFLOW; id <= CBUILTIN_MUL_OVERFLOW; id += 1)
        {
            for (uint32_t at = 0; at < BUSTER_ARRAY_LENGTH(cbuiltin_lower_test_wide_operands); at += 1)
            {
                for (uint32_t bt = 0; bt < BUSTER_ARRAY_LENGTH(cbuiltin_lower_test_wide_operands); bt += 1)
                {
                    for (uint32_t rt = 0; rt < BUSTER_ARRAY_LENGTH(cbuiltin_lower_test_wide_results); rt += 1)
                    {
                        const CBuiltinLowerCall call =
                        {
                            .id = (CBuiltinId)id,
                            .a_type = cbuiltin_lower_test_wide_operands[at],
                            .b_type = cbuiltin_lower_test_wide_operands[bt],
                            .result_type = cbuiltin_lower_test_wide_results[rt],
                        };
                        if (cbuiltin_lower_wide(call.a_type) || cbuiltin_lower_wide(call.b_type) || cbuiltin_lower_wide(call.result_type))
                        {
                            cbuiltin_lower_test_wide_case(&tally, &arena, call, &state);
                        }
                    }
                }
            }
        }
        arena_release(&arena);
        printf("test-builtin-lowering: %" PRIu64 " checks, %" PRIu64 " failed (seed %" PRIu64 ")\n", tally.checks, tally.failures, seed);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
