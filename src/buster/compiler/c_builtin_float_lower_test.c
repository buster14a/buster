// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// F32/F64 runtime recipes against the host Clang's own classification and constant builtins. Each append path is
// compiled by its corresponding Tier 0 path, compared record-for-record and executed in an alarm-bounded child.
// Raw patterns include both signs, signaling/quiet NaNs, subnormals, boundaries and randoms. A live stack prefix
// survives each recipe. Constants are read back through their actual floating storage, never numerically converted.
// Guard pages bound exact publication tails and snapshots require refusals to leave every output byte untouched.

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUSTER_CBUILTIN_FLOAT_TEST_ENTRIES (BUSTER_CBUILTIN_LOWER_ENTRIES + 16)
#define BUSTER_CBUILTIN_FLOAT_TEST_LOCALS 32u
#define BUSTER_CBUILTIN_FLOAT_TEST_INPUTS 384u
#define BUSTER_CBUILTIN_FLOAT_TEST_PAGE 4096u
#define BUSTER_CBUILTIN_FLOAT_TEST_PREFIX UINT64_C(0xbadc0ffee1234567)

typedef struct CBuiltinFloatLowerTestTally CBuiltinFloatLowerTestTally;
struct CBuiltinFloatLowerTestTally
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinFloatLowerTestStream CBuiltinFloatLowerTestStream;
struct CBuiltinFloatLowerTestStream
{
    uint8_t kinds[BUSTER_CBUILTIN_FLOAT_TEST_ENTRIES];
    uint8_t classes[BUSTER_CBUILTIN_FLOAT_TEST_ENTRIES];
    uint64_t values[BUSTER_CBUILTIN_FLOAT_TEST_ENTRIES];
    uint8_t local_classes[BUSTER_CBUILTIN_FLOAT_TEST_LOCALS];
    uint8_t local_flags[BUSTER_CBUILTIN_FLOAT_TEST_LOCALS];
    CBuiltinLowerBuffer buffer;
};

typedef struct CBuiltinFloatLowerTestProgram CBuiltinFloatLowerTestProgram;
struct CBuiltinFloatLowerTestProgram
{
    uint8_t* bytes;
    uint64_t mapping_bytes;
};

static bool cbuiltin_float_lower_test_expect(CBuiltinFloatLowerTestTally* tally, bool condition, const char* check, uint32_t id, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-float-lowering: %s: builtin %u item %u\n", check, id, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static uint64_t cbuiltin_float_lower_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void cbuiltin_float_lower_test_stream(CBuiltinFloatLowerTestStream* stream, uint32_t class)
{
    memset(stream, 0, sizeof(*stream));
    stream->buffer.kinds = stream->kinds;
    stream->buffer.classes = stream->classes;
    stream->buffer.values = stream->values;
    stream->buffer.capacity = BUSTER_CBUILTIN_FLOAT_TEST_ENTRIES;
    stream->buffer.local_classes = stream->local_classes;
    stream->buffer.local_flags = stream->local_flags;
    stream->buffer.local_count = 1;
    stream->buffer.local_capacity = BUSTER_CBUILTIN_FLOAT_TEST_LOCALS;
    stream->local_classes[0] = (uint8_t)class;
}

static void cbuiltin_float_lower_test_emit(CBuiltinFloatLowerTestStream* stream, uint32_t kind, uint32_t class, uint64_t value)
{
    const uint32_t i = stream->buffer.count;
    assert(i < stream->buffer.capacity);
    stream->kinds[i] = (uint8_t)kind;
    stream->classes[i] = (uint8_t)class;
    stream->values[i] = value;
    stream->buffer.count += 1;
}

static __attribute__((noinline)) uint32_t cbuiltin_float_lower_test_oracle32(CBuiltinId id, uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    uint32_t result = 0;
    switch (id)
    {
    case CBUILTIN_ISNAN: result = __builtin_isnan(value) != 0; break;
    case CBUILTIN_ISINF: result = __builtin_isinf(value) != 0; break;
    case CBUILTIN_ISFINITE: result = __builtin_isfinite(value) != 0; break;
    case CBUILTIN_SIGNBIT: result = __builtin_signbit(value) != 0; break;
    case CBUILTIN_FPCLASSIFY: result = __builtin_fpclassify(0, 1, 2, 3, 4, value); break;
    default: break;
    }
    return result;
}

static __attribute__((noinline)) uint32_t cbuiltin_float_lower_test_oracle64(CBuiltinId id, uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    uint32_t result = 0;
    switch (id)
    {
    case CBUILTIN_ISNAN: result = __builtin_isnan(value) != 0; break;
    case CBUILTIN_ISINF: result = __builtin_isinf(value) != 0; break;
    case CBUILTIN_ISFINITE: result = __builtin_isfinite(value) != 0; break;
    case CBUILTIN_SIGNBIT: result = __builtin_signbit(value) != 0; break;
    case CBUILTIN_FPCLASSIFY: result = __builtin_fpclassify(0, 1, 2, 3, 4, value); break;
    default: break;
    }
    return result;
}

static __attribute__((noinline)) uint64_t cbuiltin_float_lower_test_constant(CBuiltinId id, uint32_t payload)
{
    uint64_t result = 0;
    if (id == CBUILTIN_HUGE_VALF || id == CBUILTIN_INFF || id == CBUILTIN_NANF)
    {
        float value = 0;
        if (id == CBUILTIN_HUGE_VALF)
        {
            value = __builtin_huge_valf();
        }
        else if (id == CBUILTIN_INFF)
        {
            value = __builtin_inff();
        }
        else
        {
            switch (payload)
            {
            case 0: value = __builtin_nanf(""); break;
            case 1: value = __builtin_nanf("1"); break;
            case 2: value = __builtin_nanf("0x12345"); break;
            default: value = __builtin_nanf("0xffffffffffffffffffffffffffffffff"); break;
            }
        }
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        result = bits;
    }
    else
    {
        double value = 0;
        if (id == CBUILTIN_HUGE_VAL)
        {
            value = __builtin_huge_val();
        }
        else if (id == CBUILTIN_INF)
        {
            value = __builtin_inf();
        }
        else
        {
            switch (payload)
            {
            case 0: value = __builtin_nan(""); break;
            case 1: value = __builtin_nan("1"); break;
            case 2: value = __builtin_nan("0x12345"); break;
            default: value = __builtin_nan("0xffffffffffffffffffffffffffffffff"); break;
            }
        }
        memcpy(&result, &value, sizeof(result));
    }
    return result;
}

static uint64_t cbuiltin_float_lower_test_input(uint32_t index, uint32_t class, uint64_t* state)
{
    const bool narrow = class == BUSTER_TYPED_CLASS_F32;
    const uint64_t sign = narrow ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000);
    const uint64_t exponent = narrow ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    const uint64_t fraction = narrow ? UINT64_C(0x007fffff) : UINT64_C(0x000fffffffffffff);
    const uint64_t normal = fraction + 1;
    const uint64_t quiet = normal >> 1;
    const uint64_t special[12] =
    {
        0,
        1,
        2,
        fraction,
        normal,
        normal + 1,
        exponent - 1,
        exponent,
        exponent | 1,
        exponent | quiet,
        exponent | quiet | 1,
        exponent | fraction,
    };
    uint64_t result;
    if (index < 24)
    {
        result = special[index / 2] | ((index & 1) != 0 ? sign : 0);
    }
    else if (index < 152)
    {
        // Every bit position appears alone and with the sign, including exponent and fraction boundaries.
        const uint32_t bit = (index - 24) / 2;
        result = (UINT64_C(1) << bit) | ((index & 1) != 0 ? sign : 0);
    }
    else if (index < 280)
    {
        // Exercise every NaN payload bit, including the quiet bit, with both signs. Bits outside the fraction
        // deliberately reduce to infinity, so neither format reads or invents a larger representation.
        const uint32_t bit = (index - 152) / 2;
        result = exponent | ((UINT64_C(1) << bit) & fraction) | ((index & 1) != 0 ? sign : 0);
    }
    else
    {
        result = cbuiltin_float_lower_test_random(state);
    }
    result &= narrow ? UINT32_MAX : UINT64_MAX;
    return result;
}

static bool cbuiltin_float_lower_test_programs(CBuiltinFloatLowerTestTally* tally, Arena* arena, const CBuiltinFloatLowerCall* call, CBuiltinFloatLowerTestProgram programs[2])
{
    const bool classification = call->id >= CBUILTIN_ISNAN && call->id <= CBUILTIN_FPCLASSIFY;
    const uint32_t class = classification ? call->argument_class : call->constant_class;
    CBuiltinFloatLowerTestStream streams[2];
    CBuiltinLowerResult lowered[2] = { 0 };
    bool result = true;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CBuiltinFloatLowerTestStream* stream = &streams[path];
        cbuiltin_float_lower_test_stream(stream, class);
        if (!classification)
        {
            stream->local_flags[0] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
        }
        cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_CBUILTIN_FLOAT_TEST_PREFIX);
        lowered[path] = path == 0 ? cbuiltin_float_lower_scalar(&stream->buffer, call) : cbuiltin_float_lower(&stream->buffer, call);
        result &= cbuiltin_float_lower_test_expect(tally, lowered[path].error == CBUILTIN_LOWER_OK && lowered[path].first == 1 &&
            lowered[path].result_class == (classification ? BUSTER_TYPED_CLASS_I32 : class), "append after live prefix", call->id, path);
        if (classification)
        {
            result &= cbuiltin_float_lower_test_expect(tally, lowered[path].temporary_count >= 2 && stream->local_classes[1] == class &&
                stream->local_flags[1] == BUSTER_TYPED_LOCAL_ADDRESS_TAKEN, "actual floating temporary", call->id, path);
        }
        else
        {
            result &= cbuiltin_float_lower_test_expect(tally, lowered[path].temporary_count == 0 && lowered[path].count == 1, "exact floating constant", call->id, path);
            cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_SET_LOCAL, class, 0);
            cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_LOCAL_ADDRESS, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_LOAD, class == BUSTER_TYPED_CLASS_F32 ? BUSTER_TYPED_CLASS_U32 : BUSTER_TYPED_CLASS_U64, 0);
        }
        cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_float_lower_test_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    const CBuiltinFloatLowerTestStream* scalar_stream = &streams[0];
    const CBuiltinFloatLowerTestStream* vector_stream = &streams[1];
    result &= cbuiltin_float_lower_test_expect(tally, memcmp(&lowered[0], &lowered[1], sizeof(lowered[0])) == 0 &&
        scalar_stream->buffer.count == vector_stream->buffer.count && scalar_stream->buffer.local_count == vector_stream->buffer.local_count &&
        memcmp(scalar_stream->kinds, vector_stream->kinds, scalar_stream->buffer.count) == 0 &&
        memcmp(scalar_stream->classes, vector_stream->classes, scalar_stream->buffer.count) == 0 &&
        memcmp(scalar_stream->values, vector_stream->values, scalar_stream->buffer.count * sizeof(uint64_t)) == 0 &&
        memcmp(scalar_stream->local_classes, vector_stream->local_classes, scalar_stream->buffer.local_count) == 0 &&
        memcmp(scalar_stream->local_flags, vector_stream->local_flags, scalar_stream->buffer.local_count) == 0, "publication twins", call->id, 0);
    const AbiLocation parameter =
    {
        .registers = { BUSTER_ABI_REGISTER_VECTOR, BUSTER_ABI_REGISTER_NONE },
        .kind = BUSTER_ABI_LOCATION_REGISTERS,
    };
    Tier0Code codes[2] = { 0 };
    const uint64_t mark = arena->length;
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        const CBuiltinLowerBuffer* buffer = &streams[path].buffer;
        const Tier0Function function =
        {
            .kinds = buffer->kinds,
            .classes = buffer->classes,
            .values = buffer->values,
            .count = buffer->count,
            .local_classes = buffer->local_classes,
            .local_flags = buffer->local_flags,
            .local_count = buffer->local_count,
            .parameter_count = classification ? 1 : 0,
            .parameters = &parameter,
            .result =
            {
                .kind = BUSTER_ABI_RESULT_REGISTERS,
                .part_bytes = 8,
                .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
            },
        };
        const Tier0Check checked = tier0_validate(&function);
        result &= cbuiltin_float_lower_test_expect(tally, checked.error == TIER0_OK, checked.rule != 0 ? checked.rule : "typed validation", call->id, checked.node);
        if (result)
        {
            codes[path] = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
            result &= cbuiltin_float_lower_test_expect(tally, codes[path].error == TIER0_OK, "Tier 0 compile", call->id, codes[path].node);
        }
    }
    if (result)
    {
        result &= cbuiltin_float_lower_test_expect(tally, codes[0].count == codes[1].count && codes[0].branch_count == 0 && codes[1].branch_count == 0 &&
            codes[0].reference_count == 0 && codes[1].reference_count == 0 && codes[0].deferred == codes[1].deferred &&
            memcmp(codes[0].records, codes[1].records, codes[0].count * sizeof(X86Instruction)) == 0, "Tier 0 twins", call->id, 0);
    }
    for (uint32_t path = 0; result && path < 2; path += 1)
    {
        const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(codes[path].count);
        const uint64_t code_bytes = (capacity + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_FLOAT_TEST_PAGE - 1);
        programs[path].mapping_bytes = code_bytes + BUSTER_CBUILTIN_FLOAT_TEST_PAGE;
        programs[path].bytes = mmap(0, programs[path].mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        result &= cbuiltin_float_lower_test_expect(tally, programs[path].bytes != MAP_FAILED, "code mapping", call->id, path);
        if (result)
        {
            const int64_t encoded = x86_encode(codes[path].records, codes[path].count, programs[path].bytes, capacity, 0, 0);
            result &= cbuiltin_float_lower_test_expect(tally, encoded >= 0 && mprotect(programs[path].bytes, code_bytes, PROT_READ | PROT_EXEC) == 0 &&
                mprotect(programs[path].bytes + code_bytes, BUSTER_CBUILTIN_FLOAT_TEST_PAGE, PROT_NONE) == 0, "code encoding", call->id, path);
        }
    }
    arena_pop_to(arena, mark);
    return result;
}

static void cbuiltin_float_lower_test_case(CBuiltinFloatLowerTestTally* tally, Arena* arena, const CBuiltinFloatLowerCall* call, uint64_t* state)
{
    CBuiltinFloatLowerTestProgram programs[2] = { 0 };
    bool ok = cbuiltin_float_lower_test_programs(tally, arena, call, programs);
    const bool classification = call->id >= CBUILTIN_ISNAN && call->id <= CBUILTIN_FPCLASSIFY;
    const uint32_t count = classification ? BUSTER_CBUILTIN_FLOAT_TEST_INPUTS : 1;
    uint64_t inputs[BUSTER_CBUILTIN_FLOAT_TEST_INPUTS];
    uint64_t expected[BUSTER_CBUILTIN_FLOAT_TEST_INPUTS];
    for (uint32_t i = 0; i < count; i += 1)
    {
        inputs[i] = classification ? cbuiltin_float_lower_test_input(i, call->argument_class, state) : 0;
        const uint64_t value = classification ? (call->argument_class == BUSTER_TYPED_CLASS_F32 ? cbuiltin_float_lower_test_oracle32(call->id, (uint32_t)inputs[i]) :
            cbuiltin_float_lower_test_oracle64(call->id, inputs[i])) : call->constant_bits;
        expected[i] = value ^ BUSTER_CBUILTIN_FLOAT_TEST_PREFIX;
    }
    uint64_t* native = mmap(0, count * sizeof(uint64_t) * 2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ok &= cbuiltin_float_lower_test_expect(tally, native != MAP_FAILED, "result mapping", call->id, 0);
    if (ok)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            alarm(10);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                for (uint32_t i = 0; i < count; i += 1)
                {
                    uint64_t result;
                    if (!classification)
                    {
                        uint64_t (*entry)(void) = (uint64_t (*)(void))(void*)programs[path].bytes;
                        result = entry();
                    }
                    else if (call->argument_class == BUSTER_TYPED_CLASS_F32)
                    {
                        const uint32_t bits = (uint32_t)inputs[i];
                        float value;
                        memcpy(&value, &bits, sizeof(value));
                        uint64_t (*entry)(float) = (uint64_t (*)(float))(void*)programs[path].bytes;
                        result = entry(value);
                    }
                    else
                    {
                        double value;
                        memcpy(&value, &inputs[i], sizeof(value));
                        uint64_t (*entry)(double) = (uint64_t (*)(double))(void*)programs[path].bytes;
                        result = entry(value);
                    }
                    native[path * count + i] = result;
                }
            }
            _exit(0);
        }
        int status = 0;
        const bool waited = child > 0 && waitpid(child, &status, 0) == child;
        ok &= cbuiltin_float_lower_test_expect(tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child", call->id, (uint32_t)status);
    }
    for (uint32_t path = 0; ok && path < 2; path += 1)
    {
        for (uint32_t i = 0; i < count; i += 1)
        {
            cbuiltin_float_lower_test_expect(tally, native[path * count + i] == expected[i], "native Clang oracle", call->id, path * count + i);
        }
    }
    if (native != MAP_FAILED)
    {
        munmap(native, count * sizeof(uint64_t) * 2);
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        if (programs[path].bytes != 0 && programs[path].bytes != MAP_FAILED)
        {
            munmap(programs[path].bytes, programs[path].mapping_bytes);
        }
    }
}

static void cbuiltin_float_lower_test_guards(CBuiltinFloatLowerTestTally* tally)
{
    uint8_t* pages[5];
    bool mapped = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        pages[i] = mmap(0, BUSTER_CBUILTIN_FLOAT_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped &= pages[i] != MAP_FAILED;
        if (pages[i] != MAP_FAILED)
        {
            mapped &= mprotect(pages[i] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE, BUSTER_CBUILTIN_FLOAT_TEST_PAGE, PROT_NONE) == 0;
        }
    }
    if (cbuiltin_float_lower_test_expect(tally, mapped, "guard mappings", 0, 0))
    {
        for (uint32_t shape = 0; shape < 3; shape += 1)
        {
            const CBuiltinFloatLowerCall original =
            {
                .id = shape == 2 ? CBUILTIN_NANF : CBUILTIN_FPCLASSIFY,
                .argument_local = 0,
                .argument_class = shape == 1 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32,
                .folded = true,
                .constant_class = BUSTER_TYPED_CLASS_F32,
                .constant_bits = UINT64_C(0x7fc12345),
            };
            CBuiltinFloatLowerTestStream stream;
            cbuiltin_float_lower_test_stream(&stream, original.argument_class);
            const CBuiltinLowerResult expected = cbuiltin_float_lower_scalar(&stream.buffer, &original);
            cbuiltin_float_lower_test_expect(tally, expected.error == CBUILTIN_LOWER_OK, "guard recipe", original.id, shape);
            for (uint32_t prefix = 0; prefix < 18; prefix += 1)
            {
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    for (uint32_t variant = 0; variant < 4; variant += 1)
                    {
                        CBuiltinFloatLowerCall call = original;
                        const uint32_t entries = prefix + expected.count;
                        const uint32_t locals = 1 + expected.temporary_count;
                        CBuiltinLowerBuffer buffer =
                        {
                            .kinds = pages[0] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries,
                            .classes = pages[1] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries,
                            .values = (uint64_t*)(void*)(pages[2] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries * sizeof(uint64_t)),
                            .count = prefix,
                            .capacity = entries - (variant == 1),
                            .local_classes = pages[3] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - locals,
                            .local_flags = pages[4] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - locals,
                            .local_count = 1,
                            .local_capacity = locals - (variant == 2 && shape != 2),
                        };
                        for (uint32_t i = 0; i < 5; i += 1)
                        {
                            memset(pages[i], 0xa5, BUSTER_CBUILTIN_FLOAT_TEST_PAGE);
                        }
                        buffer.local_classes[0] = (uint8_t)call.argument_class;
                        if (variant == 3)
                        {
                            if (shape == 2)
                            {
                                call.folded = false;
                            }
                            else
                            {
                                buffer.local_classes[0] = BUSTER_TYPED_CLASS_U64;
                            }
                        }
                        uint8_t snapshots[5][BUSTER_CBUILTIN_FLOAT_TEST_PAGE];
                        for (uint32_t i = 0; i < 5; i += 1)
                        {
                            memcpy(snapshots[i], pages[i], BUSTER_CBUILTIN_FLOAT_TEST_PAGE);
                        }
                        const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_float_lower_scalar(&buffer, &call) : cbuiltin_float_lower(&buffer, &call);
                        if (variant == 0 || (variant == 2 && shape == 2))
                        {
                            bool same = lowered.error == CBUILTIN_LOWER_OK && lowered.first == prefix && lowered.count == expected.count &&
                                buffer.count == entries && buffer.local_count == locals;
                            same &= memcmp(buffer.kinds, snapshots[0] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries, prefix) == 0;
                            same &= memcmp(buffer.classes, snapshots[1] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries, prefix) == 0;
                            same &= memcmp(buffer.values, snapshots[2] + BUSTER_CBUILTIN_FLOAT_TEST_PAGE - entries * sizeof(uint64_t), prefix * sizeof(uint64_t)) == 0;
                            same &= memcmp(buffer.kinds + prefix, stream.kinds, expected.count) == 0 &&
                                memcmp(buffer.classes + prefix, stream.classes, expected.count) == 0 &&
                                memcmp(buffer.values + prefix, stream.values, expected.count * sizeof(uint64_t)) == 0;
                            same &= buffer.local_classes[0] == call.argument_class && buffer.local_flags[0] == 0xa5;
                            same &= memcmp(buffer.local_classes + 1, stream.local_classes + 1, expected.temporary_count) == 0 &&
                                memcmp(buffer.local_flags + 1, stream.local_flags + 1, expected.temporary_count) == 0;
                            cbuiltin_float_lower_test_expect(tally, same, "exact guarded tail", call.id, prefix * 8 + path * 4 + variant);
                        }
                        else
                        {
                            const CBuiltinLowerError error = variant == 3 ? (shape == 2 ? CBUILTIN_LOWER_UNSUPPORTED : CBUILTIN_LOWER_ARGUMENT) : CBUILTIN_LOWER_CAPACITY;
                            bool untouched = lowered.error == error && buffer.count == prefix && buffer.local_count == 1;
                            for (uint32_t i = 0; i < 5; i += 1)
                            {
                                untouched &= memcmp(snapshots[i], pages[i], BUSTER_CBUILTIN_FLOAT_TEST_PAGE) == 0;
                            }
                            cbuiltin_float_lower_test_expect(tally, untouched, "transactional refusal", call.id, prefix * 8 + path * 4 + variant);
                        }
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; i < 5; i += 1)
    {
        if (pages[i] != MAP_FAILED)
        {
            munmap(pages[i], BUSTER_CBUILTIN_FLOAT_TEST_PAGE * 2);
        }
    }
}

static void cbuiltin_float_lower_test_refusals(CBuiltinFloatLowerTestTally* tally)
{
    for (uint32_t variant = 0; variant < 12; variant += 1)
    {
        CBuiltinFloatLowerTestStream stream;
        cbuiltin_float_lower_test_stream(&stream, BUSTER_TYPED_CLASS_F32);
        CBuiltinFloatLowerCall call =
        {
            .id = CBUILTIN_NANF,
            .folded = true,
            .constant_class = BUSTER_TYPED_CLASS_F32,
            .constant_bits = UINT64_C(0x7fc00001),
        };
        CBuiltinLowerError error = CBUILTIN_LOWER_ARGUMENT;
        switch (variant)
        {
        case 0: call.id = CBUILTIN_NANL; error = CBUILTIN_LOWER_UNSUPPORTED; break;
        case 1: call.id = CBUILTIN_HUGE_VALL; error = CBUILTIN_LOWER_UNSUPPORTED; break;
        case 2: call.id = CBUILTIN_INFL; error = CBUILTIN_LOWER_UNSUPPORTED; break;
        case 3: call.folded = false; error = CBUILTIN_LOWER_UNSUPPORTED; break;
        case 4: call.constant_class = BUSTER_TYPED_CLASS_F64; break;
        case 5: call.constant_bits = UINT64_C(0x17fc00001); break;
        case 6: call.constant_bits = UINT64_C(0x7f800001); break;
        case 7: call.constant_bits = UINT64_C(0xffc00001); break;
        case 8: call.id = CBUILTIN_INFF; break;
        case 9: call.id = CBUILTIN_ISNAN; call.argument_class = BUSTER_TYPED_CLASS_U64; error = CBUILTIN_LOWER_UNSUPPORTED; break;
        case 10: call.id = CBUILTIN_ISNAN; call.argument_class = BUSTER_TYPED_CLASS_F32; call.argument_local = 1; break;
        case 11: call.id = CBUILTIN_ISNAN; call.argument_class = BUSTER_TYPED_CLASS_F64; break;
        }
        CBuiltinFloatLowerTestStream before = stream;
        const CBuiltinLowerResult lowered = cbuiltin_float_lower(&stream.buffer, &call);
        cbuiltin_float_lower_test_expect(tally, lowered.error == error && memcmp(&before, &stream, sizeof(stream)) == 0, "explicit refused boundary", call.id, variant);
    }
    cbuiltin_float_lower_test_expect(tally, cbuiltin_float_lower(0, 0).error == CBUILTIN_LOWER_ARGUMENT, "null bounded input", 0, 0);
}

static int run_builtin_float_lower_tests(int argc, char** argv)
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
        fprintf(stderr, "usage: build/compiler test-builtin-float-lowering [--seed N]\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-builtin-float-lowering: table initialization failed\n");
    }
    else
    {
        CBuiltinFloatLowerTestTally tally = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 27);
        uint64_t state = seed != 0 ? seed : 1;
        cbuiltin_float_lower_test_guards(&tally);
        cbuiltin_float_lower_test_refusals(&tally);
        for (uint32_t class = BUSTER_TYPED_CLASS_F32; class <= BUSTER_TYPED_CLASS_F64; class += 1)
        {
            for (uint32_t id = CBUILTIN_ISNAN; id <= CBUILTIN_FPCLASSIFY; id += 1)
            {
                const CBuiltinFloatLowerCall call =
                {
                    .id = (CBuiltinId)id,
                    .argument_local = 0,
                    .argument_class = class,
                };
                cbuiltin_float_lower_test_case(&tally, &arena, &call, &state);
            }
        }
        const CBuiltinId constants[6] =
        {
            CBUILTIN_HUGE_VAL,
            CBUILTIN_HUGE_VALF,
            CBUILTIN_INF,
            CBUILTIN_INFF,
            CBUILTIN_NAN,
            CBUILTIN_NANF,
        };
        for (uint32_t i = 0; i < 6; i += 1)
        {
            const bool nan = constants[i] == CBUILTIN_NAN || constants[i] == CBUILTIN_NANF;
            const uint32_t class = cbuiltin_float_lower_constant_class(constants[i]);
            for (uint32_t payload = 0; payload < (nan ? 4u : 1u); payload += 1)
            {
                const uint64_t infinity = class == BUSTER_TYPED_CLASS_F32 ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
                const uint64_t quiet = class == BUSTER_TYPED_CLASS_F32 ? UINT64_C(0x00400000) : UINT64_C(0x0008000000000000);
                const uint64_t mask = quiet - 1;
                const uint64_t value = payload == 0 ? 0 : payload == 1 ? 1 : payload == 2 ? UINT64_C(0x12345) : UINT64_MAX;
                const CBuiltinFloatLowerCall call =
                {
                    .id = constants[i],
                    .folded = true,
                    .constant_class = class,
                    .constant_bits = infinity | (nan ? quiet | (value & mask) : 0),
                };
                cbuiltin_float_lower_test_expect(&tally, cbuiltin_float_lower_test_constant(call.id, payload) == call.constant_bits, "Clang constant representation", call.id, payload);
                cbuiltin_float_lower_test_case(&tally, &arena, &call, &state);
            }
        }
        arena_release(&arena);
        printf("test-builtin-float-lowering: %" PRIu64 " checks, %" PRIu64 " failed (seed %" PRIu64 ")\n", tally.checks, tally.failures, seed);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
