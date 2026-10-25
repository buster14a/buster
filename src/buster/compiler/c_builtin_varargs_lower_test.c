// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's scalar SysV va_list recipes, consumed by both Tier 0 paths and native code. A Clang variadic wrapper owns
// real va_start/va_copy/va_arg oracle lists; generated helpers consume a separate list and store the requested scalar
// with its actual machine class, preserving floating bits. Synthetic save/overflow areas test va_start independently
// of a generated variadic frame. These tests do not supply the absent source selector or variadic prologue wiring.
// Every helper starts after a prefix, and va_arg preserves an output pointer already on the value stack. Guard pages
// bound publication tails and refusal snapshots. Native execution is confined to an alarm-bounded child process.
// Structs and unions (#137): a helper per shape copies va_arg's bytes out, for shapes of every plan (eightbytes of
// both kinds in either order, the high one alone, on the stack, aligned to 16, one of 3 bytes, a 16-byte vector in one
// XMM register), read from Clang's lists between scalars that leave each kind of register too few, against Clang's own
// va_arg of a copy.

#include <stdarg.h>

#define BUSTER_CBUILTIN_VA_TEST_ENTRIES 2048u
#define BUSTER_CBUILTIN_VA_TEST_LOCALS 128u
#define BUSTER_CBUILTIN_VA_TEST_ARGUMENTS 24u
#define BUSTER_CBUILTIN_VA_TEST_CLASSES 10u
#define BUSTER_CBUILTIN_VA_TEST_STACK BUSTER_CBUILTIN_VA_TEST_CLASSES
#define BUSTER_CBUILTIN_VA_TEST_START (BUSTER_CBUILTIN_VA_TEST_STACK + 1)
#define BUSTER_CBUILTIN_VA_TEST_C23_START (BUSTER_CBUILTIN_VA_TEST_START + 1)
#define BUSTER_CBUILTIN_VA_TEST_COPY (BUSTER_CBUILTIN_VA_TEST_C23_START + 1)
#define BUSTER_CBUILTIN_VA_TEST_END (BUSTER_CBUILTIN_VA_TEST_COPY + 1)
#define BUSTER_CBUILTIN_VA_TEST_PROGRAMS (BUSTER_CBUILTIN_VA_TEST_END + 1)
#define BUSTER_CBUILTIN_VA_TEST_SHAPES 12u
// The aggregate helpers after the others, one per shape.
#define BUSTER_CBUILTIN_VA_TEST_HELPERS (BUSTER_CBUILTIN_VA_TEST_PROGRAMS + BUSTER_CBUILTIN_VA_TEST_SHAPES)
#define BUSTER_CBUILTIN_VA_TEST_PAGE 4096u

typedef struct CBuiltinVaLowerTestTally CBuiltinVaLowerTestTally;
struct CBuiltinVaLowerTestTally
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinVaLowerTestStream CBuiltinVaLowerTestStream;
struct CBuiltinVaLowerTestStream
{
    uint8_t kinds[BUSTER_CBUILTIN_VA_TEST_ENTRIES];
    uint8_t classes[BUSTER_CBUILTIN_VA_TEST_ENTRIES];
    uint64_t values[BUSTER_CBUILTIN_VA_TEST_ENTRIES];
    uint8_t local_classes[BUSTER_CBUILTIN_VA_TEST_LOCALS];
    uint8_t local_flags[BUSTER_CBUILTIN_VA_TEST_LOCALS];
    uint32_t local_sizes[BUSTER_CBUILTIN_VA_TEST_LOCALS];
    uint8_t local_alignments[BUSTER_CBUILTIN_VA_TEST_LOCALS];
    CBuiltinLowerBuffer buffer;
};

typedef struct CBuiltinVaLowerTestProgram CBuiltinVaLowerTestProgram;
struct CBuiltinVaLowerTestProgram
{
    uint8_t* bytes;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

typedef struct CBuiltinVaLowerTestNative CBuiltinVaLowerTestNative;
struct CBuiltinVaLowerTestNative
{
    CBuiltinVaLowerTestProgram programs[2][BUSTER_CBUILTIN_VA_TEST_HELPERS];
};

// The aggregate shapes, as C types and as the plans abi_va_arg gives them: their bytes, alignment, location kind, each
// eightbyte's source, flags, and the first byte that holds data (the high eightbyte alone's padding comes before it).
typedef struct CBuiltinVaTestGeneralPair CBuiltinVaTestGeneralPair;
struct CBuiltinVaTestGeneralPair
{
    long a;
    long b;
};
typedef struct CBuiltinVaTestVectorPair CBuiltinVaTestVectorPair;
struct CBuiltinVaTestVectorPair
{
    double a;
    double b;
};
typedef struct CBuiltinVaTestGeneralVector CBuiltinVaTestGeneralVector;
struct CBuiltinVaTestGeneralVector
{
    long a;
    double b;
};
typedef struct CBuiltinVaTestVectorGeneral CBuiltinVaTestVectorGeneral;
struct CBuiltinVaTestVectorGeneral
{
    double a;
    long b;
};
typedef struct CBuiltinVaTestInt CBuiltinVaTestInt;
struct CBuiltinVaTestInt
{
    int a;
};
typedef struct CBuiltinVaTestFloats CBuiltinVaTestFloats;
struct CBuiltinVaTestFloats
{
    float a;
    float b;
};
typedef struct CBuiltinVaTestInts CBuiltinVaTestInts;
struct CBuiltinVaTestInts
{
    int a;
    int b;
    int c;
};
typedef struct CBuiltinVaTestLongs CBuiltinVaTestLongs;
struct CBuiltinVaTestLongs
{
    long a;
    long b;
    long c;
};
typedef struct CBuiltinVaTestAligned CBuiltinVaTestAligned;
struct CBuiltinVaTestAligned
{
    _Alignas(16) long a;
    long b;
};
typedef struct CBuiltinVaTestHigh CBuiltinVaTestHigh;
struct CBuiltinVaTestHigh
{
    long : 64;
    long b;
};
typedef struct CBuiltinVaTestChars CBuiltinVaTestChars;
struct CBuiltinVaTestChars
{
    char c[3];
};
// SSE and SSEUP: both eightbytes in one XMM register's 16-byte slot.
typedef double CBuiltinVaTestDoubles __attribute__((vector_size(16)));
typedef struct CBuiltinVaTestVector CBuiltinVaTestVector;
struct CBuiltinVaTestVector
{
    CBuiltinVaTestDoubles v;
};

typedef struct CBuiltinVaLowerTestShape CBuiltinVaLowerTestShape;
struct CBuiltinVaLowerTestShape
{
    uint32_t size;
    uint8_t alignment_log2;
    uint8_t kind;
    uint8_t sources[2];
    uint8_t flags;
    uint8_t first;
};

#define BUSTER_CBUILTIN_VA_TEST_G BUSTER_ABI_VA_GENERAL
#define BUSTER_CBUILTIN_VA_TEST_V BUSTER_ABI_VA_VECTOR
#define BUSTER_CBUILTIN_VA_TEST_N BUSTER_ABI_VA_NONE

static const CBuiltinVaLowerTestShape cbuiltin_va_lower_test_shapes[BUSTER_CBUILTIN_VA_TEST_SHAPES] =
{
    { 16, 3, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_G }, 0, 0 },
    { 16, 3, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_V, BUSTER_CBUILTIN_VA_TEST_V }, 0, 0 },
    { 16, 3, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_V }, 0, 0 },
    { 16, 3, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_V, BUSTER_CBUILTIN_VA_TEST_G }, 0, 0 },
    { 4, 2, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_N }, 0, 0 },
    { 8, 2, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_V, BUSTER_CBUILTIN_VA_TEST_N }, 0, 0 },
    { 12, 2, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_G }, 0, 0 },
    { 24, 3, BUSTER_ABI_LOCATION_STACK, { BUSTER_CBUILTIN_VA_TEST_N, BUSTER_CBUILTIN_VA_TEST_N }, 0, 0 },
    { 16, 4, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_G }, 0, 0 },
    { 16, 3, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_N, BUSTER_CBUILTIN_VA_TEST_G }, BUSTER_ABI_FLAG_HIGH, 8 },
    { 3, 0, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_G, BUSTER_CBUILTIN_VA_TEST_N }, 0, 0 },
    { 16, 4, BUSTER_ABI_LOCATION_REGISTERS, { BUSTER_CBUILTIN_VA_TEST_V, BUSTER_CBUILTIN_VA_TEST_N }, 0, 0 },
};

// A shape's plan, as abi_va_arg gives it: a register per eightbyte of its kind, its bytes in whole slots, and its
// overflow alignment its own, at least 8.
static AbiVaArg cbuiltin_va_lower_test_shape_plan(const CBuiltinVaLowerTestShape* shape)
{
    uint8_t general = 0;
    uint8_t vector = 0;
    for (uint32_t k = 0; k < 2; k += 1)
    {
        general += shape->sources[k] == BUSTER_ABI_VA_GENERAL ? 1 : 0;
        vector += shape->sources[k] == BUSTER_ABI_VA_VECTOR ? 1 : 0;
    }
    const AbiVaArg result =
    {
        .kind = shape->kind,
        .integer_registers = general,
        .vector_registers = vector,
        .sources = { shape->sources[0], shape->sources[1] },
        .flags = shape->flags,
        .overflow_alignment_log2 = (uint8_t)(shape->alignment_log2 > 3 ? shape->alignment_log2 : 3),
        .stack_bytes = (shape->size + 7) / 8 * 8,
    };
    return result;
}

static const uint8_t cbuiltin_va_lower_test_classes[BUSTER_CBUILTIN_VA_TEST_CLASSES] =
{
    BUSTER_TYPED_CLASS_I8,
    BUSTER_TYPED_CLASS_U8,
    BUSTER_TYPED_CLASS_I16,
    BUSTER_TYPED_CLASS_U16,
    BUSTER_TYPED_CLASS_I32,
    BUSTER_TYPED_CLASS_U32,
    BUSTER_TYPED_CLASS_I64,
    BUSTER_TYPED_CLASS_U64,
    BUSTER_TYPED_CLASS_F32,
    BUSTER_TYPED_CLASS_F64,
};

static bool cbuiltin_va_lower_test_expect(CBuiltinVaLowerTestTally* tally, bool condition, const char* check, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-varargs: %s item %u\n", check, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static void cbuiltin_va_lower_test_stream(CBuiltinVaLowerTestStream* stream, uint32_t locals)
{
    memset(stream, 0, sizeof(*stream));
    stream->buffer.kinds = stream->kinds;
    stream->buffer.classes = stream->classes;
    stream->buffer.values = stream->values;
    stream->buffer.capacity = BUSTER_CBUILTIN_VA_TEST_ENTRIES;
    stream->buffer.local_classes = stream->local_classes;
    stream->buffer.local_flags = stream->local_flags;
    stream->buffer.local_count = locals;
    stream->buffer.local_capacity = BUSTER_CBUILTIN_VA_TEST_LOCALS;
    memset(stream->local_classes, BUSTER_TYPED_CLASS_U64, locals);
}

static void cbuiltin_va_lower_test_emit(CBuiltinVaLowerTestStream* stream, uint32_t kind, uint32_t class, uint64_t value)
{
    const uint32_t i = stream->buffer.count;
    assert(i < stream->buffer.capacity);
    stream->kinds[i] = (uint8_t)kind;
    stream->classes[i] = (uint8_t)class;
    stream->values[i] = value;
    stream->buffer.count += 1;
}

static AbiVaArg cbuiltin_va_lower_test_plan(uint32_t class, bool stack)
{
    const bool floating = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
    const AbiVaArg result =
    {
        .kind = stack ? BUSTER_ABI_LOCATION_STACK : BUSTER_ABI_LOCATION_REGISTERS,
        .integer_registers = !stack && !floating ? 1 : 0,
        .vector_registers = !stack && floating ? 1 : 0,
        .sources =
        {
            stack ? BUSTER_ABI_VA_NONE : floating ? BUSTER_ABI_VA_VECTOR : BUSTER_ABI_VA_GENERAL,
            BUSTER_ABI_VA_NONE,
        },
        .overflow_alignment_log2 = stack ? 4 : 3,
        .stack_bytes = 8,
    };
    return result;
}

static bool cbuiltin_va_lower_test_program(CBuiltinVaLowerTestTally* tally, Arena* arena, const CBuiltinVaLowerCall* call, uint32_t parameters, bool argument, CBuiltinVaLowerTestProgram programs[2], uint32_t item)
{
    const uint64_t mark = arena->length;
    CBuiltinVaLowerTestStream streams[2];
    CBuiltinLowerResult lowered[2] = { 0 };
    bool result = true;
    // An aggregate's helper has its frame object after the parameters (call->object), and copies its bytes out.
    const bool aggregate = argument && call->size != 0;
    const uint32_t locals = parameters + (aggregate ? 1 : 0);
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CBuiltinVaLowerTestStream* stream = &streams[path];
        cbuiltin_va_lower_test_stream(stream, locals);
        if (aggregate)
        {
            stream->local_classes[parameters] = BUSTER_TYPED_CLASS_VOID;
            stream->local_flags[parameters] = BUSTER_TYPED_LOCAL_ADDRESS_TAKEN;
            stream->local_sizes[parameters] = (call->size + 7) / 8 * 8;
            stream->local_alignments[parameters] = cbuiltin_va_lower_test_shapes[item - BUSTER_CBUILTIN_VA_TEST_PROGRAMS].alignment_log2;
        }
        cbuiltin_va_lower_test_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, UINT64_C(0x55));
        cbuiltin_va_lower_test_emit(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        if (argument)
        {
            cbuiltin_va_lower_test_emit(stream, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, 1);
        }
        lowered[path] = path == 0 ? cbuiltin_varargs_lower_scalar(&stream->buffer, call) : cbuiltin_varargs_lower(&stream->buffer, call);
        result &= cbuiltin_va_lower_test_expect(tally, lowered[path].error == CBUILTIN_LOWER_OK && lowered[path].first == (argument ? 3u : 2u), "append after prefix", item);
        if (call->id == CBUILTIN_VA_END)
        {
            result &= cbuiltin_va_lower_test_expect(tally, lowered[path].count == 0 && lowered[path].temporary_count == 0 && stream->buffer.count == 2 && stream->buffer.local_count == parameters, "va_end has no extra evaluation", item);
        }
        if (argument && lowered[path].error == CBUILTIN_LOWER_OK)
        {
            result &= cbuiltin_va_lower_test_expect(tally, lowered[path].result_class == call->result_class, "argument result class", item);
            cbuiltin_va_lower_test_emit(stream, aggregate ? BUSTER_TYPED_COPY : BUSTER_TYPED_SET, aggregate ? BUSTER_TYPED_CLASS_VOID : call->result_class, aggregate ? call->size : 0);
        }
        cbuiltin_va_lower_test_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_va_lower_test_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    const CBuiltinVaLowerTestStream* scalar_stream = &streams[0];
    const CBuiltinVaLowerTestStream* vector_stream = &streams[1];
    result &= cbuiltin_va_lower_test_expect(tally, memcmp(&lowered[0], &lowered[1], sizeof(lowered[0])) == 0 &&
        scalar_stream->buffer.count == vector_stream->buffer.count && scalar_stream->buffer.local_count == vector_stream->buffer.local_count &&
        memcmp(scalar_stream->kinds, vector_stream->kinds, scalar_stream->buffer.count) == 0 &&
        memcmp(scalar_stream->classes, vector_stream->classes, scalar_stream->buffer.count) == 0 &&
        memcmp(scalar_stream->values, vector_stream->values, scalar_stream->buffer.count * sizeof(uint64_t)) == 0 &&
        memcmp(scalar_stream->local_classes, vector_stream->local_classes, scalar_stream->buffer.local_count) == 0 &&
        memcmp(scalar_stream->local_flags, vector_stream->local_flags, scalar_stream->buffer.local_count) == 0 &&
        memcmp(scalar_stream->local_sizes, vector_stream->local_sizes, sizeof(scalar_stream->local_sizes)) == 0, "publication twins", item);
    const AbiLocation locations[3] =
    {
        {
            .registers = { BUSTER_ABI_RDI, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        {
            .registers = { BUSTER_ABI_RSI, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
        {
            .registers = { BUSTER_ABI_RDX, BUSTER_ABI_REGISTER_NONE },
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
        },
    };
    Tier0Code codes[2] = { 0 };
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
            .local_sizes = aggregate ? streams[path].local_sizes : 0,
            .local_alignments = aggregate ? streams[path].local_alignments : 0,
            .parameter_count = parameters,
            .parameters = locations,
            .result =
            {
                .kind = BUSTER_ABI_RESULT_REGISTERS,
                .part_bytes = 8,
                .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
            },
        };
        const Tier0Check checked = tier0_validate(&function);
        result = cbuiltin_va_lower_test_expect(tally, checked.error == TIER0_OK, checked.rule != 0 ? checked.rule : "typed validation", item);
        if (result)
        {
            codes[path] = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
            result = cbuiltin_va_lower_test_expect(tally, codes[path].error == TIER0_OK && codes[path].reference_count == 0 &&
                x86_validate(codes[path].records, codes[path].count) == codes[path].count, "native records", item);
        }
        if (result)
        {
            const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(codes[path].count);
            const uint64_t pages = (capacity + BUSTER_CBUILTIN_VA_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_VA_TEST_PAGE - 1);
            programs[path].mapping_bytes = pages + BUSTER_CBUILTIN_VA_TEST_PAGE;
            programs[path].bytes = mmap(0, programs[path].mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)codes[path].count + 1);
            result = cbuiltin_va_lower_test_expect(tally, programs[path].bytes != MAP_FAILED && offsets != 0, "native allocation", item);
            if (result)
            {
                const BranchSizing sized = path == 0 ? size_branches_scalar(codes[path].records, codes[path].count, codes[path].branches, codes[path].targets, codes[path].branch_count, offsets)
                                                    : size_branches(codes[path].records, codes[path].count, codes[path].branches, codes[path].targets, codes[path].branch_count, offsets);
                const int64_t encoded = sized.error != BRANCH_SIZING_OK ? -1 : path == 0 ? x86_encode_scalar(codes[path].records, codes[path].count, programs[path].bytes, capacity, 0, 0)
                                                                                      : x86_encode(codes[path].records, codes[path].count, programs[path].bytes, capacity, 0, 0);
                result = cbuiltin_va_lower_test_expect(tally, encoded > 0 && mprotect(programs[path].bytes, pages, PROT_READ | PROT_EXEC) == 0 &&
                    mprotect(programs[path].bytes + pages, BUSTER_CBUILTIN_VA_TEST_PAGE, PROT_NONE) == 0, "native encoding", item);
                programs[path].byte_count = encoded > 0 ? (uint32_t)encoded : 0;
            }
        }
    }
    if (result)
    {
        result = cbuiltin_va_lower_test_expect(tally, cbuiltin_lower_test_same_code(&codes[0], &codes[1]) && programs[0].byte_count == programs[1].byte_count &&
            memcmp(programs[0].bytes, programs[1].bytes, programs[0].byte_count) == 0, "backend and encoded twins", item);
    }
    arena_pop_to(arena, mark);
    return result;
}

static uint32_t cbuiltin_va_lower_test_class_index(uint32_t class)
{
    uint32_t result = 0;
    for (uint32_t i = 0; i < BUSTER_CBUILTIN_VA_TEST_CLASSES; i += 1)
    {
        if (cbuiltin_va_lower_test_classes[i] == class)
        {
            result = i;
        }
    }
    return result;
}

static void cbuiltin_va_lower_test_read(CBuiltinVaLowerTestTally* tally, const CBuiltinVaLowerTestNative* native, uint32_t path, uint32_t class, void* list, uint64_t expected, uint32_t item)
{
    uint8_t bytes[24];
    uint8_t expected_bytes[24];
    memset(bytes, 0xa5, sizeof(bytes));
    memset(expected_bytes, 0xa5, sizeof(expected_bytes));
    const uint32_t width = tier0_class_bytes(class);
    memcpy(expected_bytes + 8, &expected, width);
    const uint32_t index = cbuiltin_va_lower_test_class_index(class);
    uint64_t (*entry)(void*, void*) = (uint64_t (*)(void*, void*))(void*)native->programs[path][index].bytes;
    const uint64_t result = entry(list, bytes + 8);
    cbuiltin_va_lower_test_expect(tally, result == 0 && memcmp(bytes, expected_bytes, sizeof(bytes)) == 0, "native scalar value and store width", item);
}

static bool cbuiltin_va_lower_test_same_list(const void* a, const void* b)
{
    AbiVaList left;
    AbiVaList right;
    memcpy(&left, a, sizeof(left));
    memcpy(&right, b, sizeof(right));
    const bool result = left.gp_offset == right.gp_offset && left.fp_offset == right.fp_offset && left.overflow_arg_area == right.overflow_arg_area && left.reg_save_area == right.reg_save_area;
    return result;
}

static uint32_t cbuiltin_va_lower_test_oracle(va_list list, uint32_t class, uint64_t* bits)
{
    uint32_t result = tier0_class_bytes(class);
    if (class == BUSTER_TYPED_CLASS_I32)
    {
        const int value = va_arg(list, int);
        *bits = (uint32_t)value;
    }
    else if (class == BUSTER_TYPED_CLASS_I64)
    {
        const long value = va_arg(list, long);
        memcpy(bits, &value, sizeof(value));
    }
    else if (class == BUSTER_TYPED_CLASS_U64)
    {
        *bits = va_arg(list, unsigned long);
    }
    else
    {
        const double value = va_arg(list, double);
        memcpy(bits, &value, sizeof(value));
    }
    return result;
}

static __attribute__((noinline)) void cbuiltin_va_lower_test_clang(CBuiltinVaLowerTestTally* tally, const CBuiltinVaLowerTestNative* native, uint32_t path, uint32_t shape, double named, ...)
{
    va_list generated;
    va_list oracle;
    va_list copy_oracle;
    va_start(generated, named);
    va_copy(oracle, generated);
    va_copy(copy_oracle, generated);
    const uint32_t count = shape == 0 ? 24 : 20;
    uint8_t classes[BUSTER_CBUILTIN_VA_TEST_ARGUMENTS];
    for (uint32_t i = 0; i < count; i += 1)
    {
        classes[i] = shape == 0 ? (i % 2 == 1 ? BUSTER_TYPED_CLASS_F64 : i % 6 == 0 ? BUSTER_TYPED_CLASS_I64 : i % 6 == 2 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64)
                               : shape == 1 ? (i < 10 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_I64) : (i < 10 ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64);
    }
    AbiVaList copied;
    uint64_t (*copy)(void*, void*) = (uint64_t (*)(void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_COPY].bytes;
    copy(&copied, generated);
    cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(&copied, generated), "va_copy fields", shape);
    AbiVaList original;
    memcpy(&original, generated, sizeof(original));
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint64_t expected = 0;
        cbuiltin_va_lower_test_oracle(copy_oracle, classes[i], &expected);
        cbuiltin_va_lower_test_read(tally, native, path, classes[i], &copied, expected, shape * 100 + i);
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(&copied, copy_oracle) && cbuiltin_va_lower_test_same_list(&original, generated), "copy advances independently", shape * 100 + i);
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint64_t expected = 0;
        cbuiltin_va_lower_test_oracle(oracle, classes[i], &expected);
        cbuiltin_va_lower_test_read(tally, native, path, classes[i], generated, expected, shape * 100 + i);
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(generated, oracle), "Clang cursor updates", shape * 100 + i);
    }
    uint64_t (*end)(void*) = (uint64_t (*)(void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_END].bytes;
    AbiVaList before_end = copied;
    end(&copied);
    cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(&copied, &before_end), "documented va_end no-op", shape);
    va_end(copy_oracle);
    va_end(oracle);
    va_end(generated);
}

static void cbuiltin_va_lower_test_real_lists(CBuiltinVaLowerTestTally* tally, const CBuiltinVaLowerTestNative* native, uint32_t path)
{
    cbuiltin_va_lower_test_clang(tally, native, path, 0, 0.25,
        (long)-7, 1.25, (int)-23, -9.5, (unsigned long)UINT64_C(0xfedcba9876543210), 0.0,
        (long)8193, -0.0, (int)INT32_MIN, 0x1.0p-1022, (unsigned long)UINT64_MAX, 0x1.fffffffffffffp1023,
        (long)INT64_MIN, 13.5, (int)INT32_MAX, -71.25, (unsigned long)0, 1024.5,
        (long)INT64_MAX, -0x1.0p-1022, (int)0, 0x1.0p-1074, (unsigned long)1, -0x1.0p-1074);
    cbuiltin_va_lower_test_clang(tally, native, path, 1, -0.5,
        0.0, -0.0, 1.0, -1.0, 0.125, -0.25, 99.75, -101.5, 0x1.0p-1022, 0x1.0p-1074,
        (long)1, (long)-2, (long)3, (long)-4, (long)5, (long)-6, (long)7, (long)-8, (long)INT64_MIN, (long)INT64_MAX);
    cbuiltin_va_lower_test_clang(tally, native, path, 2, 1.5,
        (long)11, (long)-12, (long)13, (long)-14, (long)15, (long)-16, (long)17, (long)-18, (long)INT64_MIN, (long)INT64_MAX,
        0.0, -0.0, 1.0, -1.0, 0.125, -0.25, 99.75, -101.5, 0x1.0p-1022, 0x1.0p-1074);
}

// Clang's own va_arg of shape `shape` from `oracle`, which it advances, its bytes into `bytes`.
static void cbuiltin_va_lower_test_shape_oracle(va_list oracle, uint32_t shape, uint8_t* bytes)
{
    if (shape == 0)
    {
        const CBuiltinVaTestGeneralPair value = va_arg(oracle, CBuiltinVaTestGeneralPair);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 1)
    {
        const CBuiltinVaTestVectorPair value = va_arg(oracle, CBuiltinVaTestVectorPair);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 2)
    {
        const CBuiltinVaTestGeneralVector value = va_arg(oracle, CBuiltinVaTestGeneralVector);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 3)
    {
        const CBuiltinVaTestVectorGeneral value = va_arg(oracle, CBuiltinVaTestVectorGeneral);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 4)
    {
        const CBuiltinVaTestInt value = va_arg(oracle, CBuiltinVaTestInt);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 5)
    {
        const CBuiltinVaTestFloats value = va_arg(oracle, CBuiltinVaTestFloats);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 6)
    {
        const CBuiltinVaTestInts value = va_arg(oracle, CBuiltinVaTestInts);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 7)
    {
        const CBuiltinVaTestLongs value = va_arg(oracle, CBuiltinVaTestLongs);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 8)
    {
        const CBuiltinVaTestAligned value = va_arg(oracle, CBuiltinVaTestAligned);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 9)
    {
        const CBuiltinVaTestHigh value = va_arg(oracle, CBuiltinVaTestHigh);
        memcpy(bytes, &value, sizeof(value));
    }
    else if (shape == 10)
    {
        const CBuiltinVaTestChars value = va_arg(oracle, CBuiltinVaTestChars);
        memcpy(bytes, &value, sizeof(value));
    }
    else
    {
        const CBuiltinVaTestVector value = va_arg(oracle, CBuiltinVaTestVector);
        memcpy(bytes, &value, sizeof(value));
    }
}

// The shapes' sequences: each argument a shape's index, or a long or a double between them; then the sequence's end.
// The helpers' named arguments take one general register and one vector register, which leave five and seven to the
// sequences, so that each shape is read from registers in one sequence and from the overflow area in another: once the
// registers of its kinds run out, or when one of a kind is left for a shape that needs two, which the next long or
// double then takes (sequences 1 and 3).
#define BUSTER_CBUILTIN_VA_TEST_LONG 100u
#define BUSTER_CBUILTIN_VA_TEST_DOUBLE 101u
#define BUSTER_CBUILTIN_VA_TEST_STOP 0xffu
#define BUSTER_CBUILTIN_VA_TEST_SEQUENCES 5u

static const uint8_t cbuiltin_va_lower_test_sequences[BUSTER_CBUILTIN_VA_TEST_SEQUENCES][24] =
{
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, BUSTER_CBUILTIN_VA_TEST_STOP },
    { BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_LONG, 0, BUSTER_CBUILTIN_VA_TEST_LONG, 4, 1, 9, 10, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_STOP },
    { BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, 1, 11, 5, 2, 0, 6, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_STOP },
    { BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_DOUBLE, 1, 2, 3, 8, 7, 6, BUSTER_CBUILTIN_VA_TEST_LONG, BUSTER_CBUILTIN_VA_TEST_DOUBLE, BUSTER_CBUILTIN_VA_TEST_STOP },
    { 11, 8, 9, 10, 7, 3, 6, 5, 4, BUSTER_CBUILTIN_VA_TEST_STOP },
};

// What the sequences' reading needs, the helpers' one named general argument, and how each shape was read: bit 0 from
// registers (the overflow area untouched), bit 1 from the overflow area.
#define BUSTER_CBUILTIN_VA_TEST_REGISTERS 1u
#define BUSTER_CBUILTIN_VA_TEST_OVERFLOW 2u
typedef struct CBuiltinVaLowerTestRun CBuiltinVaLowerTestRun;
struct CBuiltinVaLowerTestRun
{
    CBuiltinVaLowerTestTally* tally;
    const CBuiltinVaLowerTestNative* native;
    uint32_t path;
    uint32_t sequence;
    uint8_t read[BUSTER_CBUILTIN_VA_TEST_SHAPES];
};

// The run's sequence's arguments, read by the helpers from a list Clang started, each against Clang's va_arg of a copy:
// an aggregate's data bytes copied out exactly, nothing around them written, and both cursors alike after it.
static __attribute__((noinline)) void cbuiltin_va_lower_test_clang_shapes(CBuiltinVaLowerTestRun* run, double named, ...)
{
    va_list generated;
    va_list oracle;
    va_start(generated, named);
    va_copy(oracle, generated);
    CBuiltinVaLowerTestTally* tally = run->tally;
    const CBuiltinVaLowerTestNative* native = run->native;
    const uint32_t path = run->path;
    const uint8_t* kinds = cbuiltin_va_lower_test_sequences[run->sequence];
    for (uint32_t i = 0; kinds[i] != BUSTER_CBUILTIN_VA_TEST_STOP; i += 1)
    {
        const uint32_t item = 1000 + run->sequence * 100 + i;
        if (kinds[i] == BUSTER_CBUILTIN_VA_TEST_LONG || kinds[i] == BUSTER_CBUILTIN_VA_TEST_DOUBLE)
        {
            const uint32_t class = kinds[i] == BUSTER_CBUILTIN_VA_TEST_LONG ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_F64;
            uint64_t expected = 0;
            cbuiltin_va_lower_test_oracle(oracle, class, &expected);
            cbuiltin_va_lower_test_read(tally, native, path, class, generated, expected, item);
        }
        else
        {
            const CBuiltinVaLowerTestShape* shape = &cbuiltin_va_lower_test_shapes[kinds[i]];
            uint8_t bytes[48];
            uint8_t expected[48];
            memset(bytes, 0xa5, sizeof(bytes));
            memset(expected, 0xa5, sizeof(expected));
            cbuiltin_va_lower_test_shape_oracle(oracle, kinds[i], expected + 8);
            uint64_t (*entry)(void*, void*) = (uint64_t (*)(void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_PROGRAMS + kinds[i]].bytes;
            AbiVaList before;
            AbiVaList after;
            memcpy(&before, generated, sizeof(before));
            const uint64_t result = entry(generated, bytes + 8);
            memcpy(&after, generated, sizeof(after));
            run->read[kinds[i]] |= after.overflow_arg_area == before.overflow_arg_area ? BUSTER_CBUILTIN_VA_TEST_REGISTERS : BUSTER_CBUILTIN_VA_TEST_OVERFLOW;
            const bool outside = memcmp(bytes, expected, 8) == 0 && memcmp(bytes + 8 + shape->size, expected + 8 + shape->size, sizeof(bytes) - 8 - shape->size) == 0;
            const bool data = memcmp(bytes + 8 + shape->first, expected + 8 + shape->first, shape->size - shape->first) == 0;
            cbuiltin_va_lower_test_expect(tally, result == 0 && outside && data, "native aggregate bytes", item);
        }
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(generated, oracle), "aggregate cursor updates", item);
    }
    va_end(oracle);
    va_end(generated);
}

static void cbuiltin_va_lower_test_real_shapes(CBuiltinVaLowerTestTally* tally, const CBuiltinVaLowerTestNative* native, uint32_t path)
{
    const CBuiltinVaTestGeneralPair general_pair = { -7, 0x123456789abcdef };
    const CBuiltinVaTestVectorPair vector_pair = { 1.25, -0x1.0p-1022 };
    const CBuiltinVaTestGeneralVector general_vector = { INT64_MIN, 99.5 };
    const CBuiltinVaTestVectorGeneral vector_general = { -0.0, INT64_MAX };
    const CBuiltinVaTestInt four = { -42 };
    const CBuiltinVaTestFloats eight = { 1.5f, -2.25f };
    const CBuiltinVaTestInts twelve = { 1, -2, 3 };
    const CBuiltinVaTestLongs longs = { 10, 20, 30 };
    const CBuiltinVaTestAligned aligned = { 0x1111, 0x2222 };
    CBuiltinVaTestHigh high;
    memset(&high, 0x5a, sizeof(high));
    high.b = 0x3333;
    const CBuiltinVaTestChars chars = { { 'x', 'y', 'z' } };
    // Distinct halves, the high one's last byte 0xc0 (-3.5's sign and exponent).
    const CBuiltinVaTestVector vector = { { 1.25, -3.5 } };
    CBuiltinVaLowerTestRun run = { tally, native, path, 0, { 0 } };
    cbuiltin_va_lower_test_clang_shapes(&run, 0.5, general_pair, vector_pair, general_vector, vector_general, four, eight, twelve, longs, aligned, high, chars, vector);
    run.sequence = 1;
    cbuiltin_va_lower_test_clang_shapes(&run, 0.5, 1L, 2L, 3L, 4L, general_pair, 5L, four, vector_pair, high, chars, 7.5);
    run.sequence = 2;
    cbuiltin_va_lower_test_clang_shapes(&run, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, vector_pair, vector, eight, general_vector, general_pair, twelve, 8L);
    run.sequence = 3;
    cbuiltin_va_lower_test_clang_shapes(&run, 0.5, 1L, 2L, 3L, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, vector_pair, general_vector, vector_general, aligned, longs, twelve, 6L, 7.0);
    run.sequence = 4;
    cbuiltin_va_lower_test_clang_shapes(&run, 0.5, vector, aligned, high, chars, longs, vector_general, twelve, eight, four);
    // Every shape read both ways, but the one always on the stack.
    for (uint32_t k = 0; k < BUSTER_CBUILTIN_VA_TEST_SHAPES; k += 1)
    {
        const bool stack = cbuiltin_va_lower_test_shapes[k].kind == BUSTER_ABI_LOCATION_STACK;
        const uint8_t expected = stack ? BUSTER_CBUILTIN_VA_TEST_OVERFLOW : BUSTER_CBUILTIN_VA_TEST_REGISTERS | BUSTER_CBUILTIN_VA_TEST_OVERFLOW;
        cbuiltin_va_lower_test_expect(tally, run.read[k] == expected, "shape read from registers and from the overflow area", k);
    }
}

static void cbuiltin_va_lower_test_synthetic(CBuiltinVaLowerTestTally* tally, const CBuiltinVaLowerTestNative* native, uint32_t path)
{
    uint8_t save[BUSTER_ABI_SAVE_AREA_BYTES] __attribute__((aligned(16)));
    uint64_t overflow[8] __attribute__((aligned(16)));
    memset(save, 0x5a, sizeof(save));
    for (uint32_t i = 0; i < 8; i += 1)
    {
        overflow[i] = UINT64_C(0x1020304050607080) + i;
    }
    const uint64_t general = UINT64_C(0xf1e2d3c4b5a69788);
    const uint64_t floating = UINT64_C(0x8000000000000000);
    memcpy(save + 40, &general, sizeof(general));
    memcpy(save + 160, &floating, sizeof(floating));
    for (uint32_t spelling = 0; spelling < 2; spelling += 1)
    {
        AbiVaList list;
        uint64_t (*start)(void*, void*, void*) = (uint64_t (*)(void*, void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_START + spelling].bytes;
        start(&list, save, overflow);
        const AbiVaList expected =
        {
            .gp_offset = 40,
            .fp_offset = 160,
            .overflow_arg_area = (uint64_t)(uintptr_t)overflow,
            .reg_save_area = (uint64_t)(uintptr_t)save,
        };
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(&list, &expected), "va_start supplied frame addresses and offsets", spelling);
        cbuiltin_va_lower_test_read(tally, native, path, BUSTER_TYPED_CLASS_U64, &list, general, spelling);
        cbuiltin_va_lower_test_expect(tally, list.gp_offset == 48 && list.fp_offset == 160 && list.overflow_arg_area == expected.overflow_arg_area, "last general register", spelling);
        cbuiltin_va_lower_test_read(tally, native, path, BUSTER_TYPED_CLASS_F64, &list, floating, spelling);
        cbuiltin_va_lower_test_expect(tally, list.gp_offset == 48 && list.fp_offset == 176 && list.overflow_arg_area == expected.overflow_arg_area, "last vector register", spelling);
        cbuiltin_va_lower_test_read(tally, native, path, BUSTER_TYPED_CLASS_U64, &list, overflow[0], spelling);
        cbuiltin_va_lower_test_read(tally, native, path, BUSTER_TYPED_CLASS_F64, &list, overflow[1], spelling);
        cbuiltin_va_lower_test_expect(tally, list.gp_offset == 48 && list.fp_offset == 176 && list.overflow_arg_area == expected.overflow_arg_area + 16, "exhausted register offsets stay fixed", spelling);
    }
    for (uint32_t c = 0; c < BUSTER_CBUILTIN_VA_TEST_CLASSES; c += 1)
    {
        const uint32_t class = cbuiltin_va_lower_test_classes[c];
        const bool is_float = class == BUSTER_TYPED_CLASS_F32 || class == BUSTER_TYPED_CLASS_F64;
        const uint64_t bits = class == BUSTER_TYPED_CLASS_F32 ? UINT64_C(0xffc12345) : class == BUSTER_TYPED_CLASS_F64 ? UINT64_C(0xfff8123456789abc) : UINT64_C(0xfedcba9887654321);
        memcpy(save + (is_float ? 160 : 40), &bits, sizeof(bits));
        overflow[0] = bits;
        AbiVaList list =
        {
            .gp_offset = is_float ? 48 : 40,
            .fp_offset = is_float ? 160 : 176,
            .overflow_arg_area = (uint64_t)(uintptr_t)overflow,
            .reg_save_area = (uint64_t)(uintptr_t)save,
        };
        cbuiltin_va_lower_test_read(tally, native, path, class, &list, bits, c);
        cbuiltin_va_lower_test_expect(tally, list.gp_offset == 48 && list.fp_offset == 176 && list.overflow_arg_area == (uint64_t)(uintptr_t)overflow, "unrelated exhausted cursor ignored", c);
        cbuiltin_va_lower_test_read(tally, native, path, class, &list, bits, c);
        cbuiltin_va_lower_test_expect(tally, list.gp_offset == 48 && list.fp_offset == 176 && list.overflow_arg_area == (uint64_t)(uintptr_t)overflow + 8, "scalar overflow slot", c);
    }
    uint8_t stored[24];
    memset(stored, 0xa5, sizeof(stored));
    AbiVaList aligned =
    {
        .gp_offset = 0,
        .fp_offset = 48,
        .overflow_arg_area = (uint64_t)(uintptr_t)((uint8_t*)overflow + 1),
        .reg_save_area = (uint64_t)(uintptr_t)save,
    };
    uint64_t (*stack)(void*, void*) = (uint64_t (*)(void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_STACK].bytes;
    stack(&aligned, stored + 8);
    uint64_t value;
    memcpy(&value, stored + 8, sizeof(value));
    cbuiltin_va_lower_test_expect(tally, value == overflow[2] && aligned.overflow_arg_area == (uint64_t)(uintptr_t)overflow + 24 && aligned.gp_offset == 0 && aligned.fp_offset == 48, "explicit stack plan aligns independently", path);
    uint8_t* guarded = mmap(0, BUSTER_CBUILTIN_VA_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = guarded != MAP_FAILED && mprotect(guarded + BUSTER_CBUILTIN_VA_TEST_PAGE, BUSTER_CBUILTIN_VA_TEST_PAGE, PROT_NONE) == 0;
    if (cbuiltin_va_lower_test_expect(tally, mapped, "native list guard allocation", path))
    {
        memset(guarded, 0xa5, BUSTER_CBUILTIN_VA_TEST_PAGE);
        void* tail = guarded + BUSTER_CBUILTIN_VA_TEST_PAGE - sizeof(AbiVaList);
        uint64_t (*start)(void*, void*, void*) = (uint64_t (*)(void*, void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_START].bytes;
        start(tail, save, overflow);
        uint64_t (*copy)(void*, void*) = (uint64_t (*)(void*, void*))(void*)native->programs[path][BUSTER_CBUILTIN_VA_TEST_COPY].bytes;
        AbiVaList source = aligned;
        copy(tail, &source);
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(tail, &source) && guarded[BUSTER_CBUILTIN_VA_TEST_PAGE - sizeof(AbiVaList) - 1] == 0xa5, "exact 24-byte start and copy tail", path);
        copy(&source, tail);
        cbuiltin_va_lower_test_expect(tally, cbuiltin_va_lower_test_same_list(tail, &source), "exact 24-byte copy source tail", path);
    }
    if (guarded != MAP_FAILED)
    {
        munmap(guarded, BUSTER_CBUILTIN_VA_TEST_PAGE * 2);
    }
}

static void cbuiltin_va_lower_test_guards(CBuiltinVaLowerTestTally* tally)
{
    const CBuiltinVaLowerCall call =
    {
        .id = CBUILTIN_VA_ARG,
        .list = 0,
        .result_class = BUSTER_TYPED_CLASS_U64,
        .plan =
        {
            .kind = BUSTER_ABI_LOCATION_REGISTERS,
            .integer_registers = 1,
            .sources = { BUSTER_ABI_VA_GENERAL, BUSTER_ABI_VA_NONE },
            .overflow_alignment_log2 = 3,
            .stack_bytes = 8,
        },
    };
    CBuiltinVaLowerTestStream stream;
    cbuiltin_va_lower_test_stream(&stream, 1);
    const CBuiltinLowerResult expected = cbuiltin_varargs_lower_scalar(&stream.buffer, &call);
    cbuiltin_va_lower_test_expect(tally, expected.error == CBUILTIN_LOWER_OK && expected.count < BUSTER_CBUILTIN_VA_TEST_PAGE / sizeof(uint64_t) - 2, "guard recipe bound", 0);
    uint8_t* pages[5];
    bool mapped = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        pages[i] = mmap(0, BUSTER_CBUILTIN_VA_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped &= pages[i] != MAP_FAILED;
        if (pages[i] != MAP_FAILED)
        {
            mapped &= mprotect(pages[i] + BUSTER_CBUILTIN_VA_TEST_PAGE, BUSTER_CBUILTIN_VA_TEST_PAGE, PROT_NONE) == 0;
        }
    }
    if (cbuiltin_va_lower_test_expect(tally, mapped, "publication guard allocation", 0))
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t variant = 0; variant < 9; variant += 1)
            {
                const uint32_t entries = expected.count + 2;
                const uint32_t locals = expected.temporary_count + 1;
                CBuiltinLowerBuffer buffer =
                {
                    .kinds = pages[0] + BUSTER_CBUILTIN_VA_TEST_PAGE - entries,
                    .classes = pages[1] + BUSTER_CBUILTIN_VA_TEST_PAGE - entries,
                    .values = (uint64_t*)(void*)(pages[2] + BUSTER_CBUILTIN_VA_TEST_PAGE - entries * sizeof(uint64_t)),
                    .count = 2,
                    .capacity = entries - (variant == 1),
                    .local_classes = pages[3] + BUSTER_CBUILTIN_VA_TEST_PAGE - locals,
                    .local_flags = pages[4] + BUSTER_CBUILTIN_VA_TEST_PAGE - locals,
                    .local_count = 1,
                    .local_capacity = locals - (variant == 2),
                };
                for (uint32_t i = 0; i < 5; i += 1)
                {
                    memset(pages[i], 0xa5, BUSTER_CBUILTIN_VA_TEST_PAGE);
                }
                buffer.local_classes[0] = variant == 3 ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U64;
                CBuiltinVaLowerCall request = call;
                if (variant == 4)
                {
                    request.plan.sources[1] = BUSTER_ABI_VA_GENERAL;
                }
                else if (variant == 5)
                {
                    request.id = UINT32_MAX;
                }
                else if (variant == 6)
                {
                    request.id = CBUILTIN_VA_START;
                    request.gp_offset = 49;
                    request.fp_offset = 48;
                }
                else if (variant == 7)
                {
                    // An aggregate whose plan counts two general registers for one general eightbyte.
                    request.size = 16;
                    request.object = 0;
                    request.plan.integer_registers = 2;
                    request.plan.stack_bytes = 16;
                }
                else if (variant == 8)
                {
                    // An aggregate whose object is a value, not a frame object.
                    request.size = 8;
                    request.object = 0;
                }
                uint8_t snapshots[5][BUSTER_CBUILTIN_VA_TEST_PAGE];
                for (uint32_t i = 0; i < 5; i += 1)
                {
                    memcpy(snapshots[i], pages[i], BUSTER_CBUILTIN_VA_TEST_PAGE);
                }
                const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_varargs_lower_scalar(&buffer, &request) : cbuiltin_varargs_lower(&buffer, &request);
                if (variant == 0)
                {
                    bool valid = lowered.error == CBUILTIN_LOWER_OK && lowered.first == 2 && lowered.count == expected.count && buffer.count == entries && buffer.local_count == locals;
                    for (uint32_t i = 2; i < buffer.count; i += 1)
                    {
                        if (buffer.kinds[i] == BUSTER_TYPED_BRANCH_FALSE || buffer.kinds[i] == BUSTER_TYPED_JUMP)
                        {
                            valid &= buffer.values[i] >= 2 && buffer.values[i] < buffer.count;
                        }
                    }
                    valid &= buffer.kinds[0] == 0xa5 && buffer.kinds[1] == 0xa5 && buffer.classes[0] == 0xa5 && buffer.classes[1] == 0xa5;
                    cbuiltin_va_lower_test_expect(tally, valid, "guarded exact append and branch offsets", path);
                }
                else
                {
                    const CBuiltinLowerError error = variant <= 2 ? CBUILTIN_LOWER_CAPACITY : variant == 3 || variant == 6 ? CBUILTIN_LOWER_ARGUMENT : CBUILTIN_LOWER_UNSUPPORTED;
                    bool untouched = lowered.error == error && buffer.count == 2 && buffer.local_count == 1;
                    for (uint32_t i = 0; i < 5; i += 1)
                    {
                        untouched &= memcmp(snapshots[i], pages[i], BUSTER_CBUILTIN_VA_TEST_PAGE) == 0;
                    }
                    cbuiltin_va_lower_test_expect(tally, untouched, "transactional refusal", path * 10 + variant);
                }
            }
        }
    }
    for (uint32_t i = 0; i < 5; i += 1)
    {
        if (pages[i] != MAP_FAILED)
        {
            munmap(pages[i], BUSTER_CBUILTIN_VA_TEST_PAGE * 2);
        }
    }
}

static int run_builtin_varargs_lower_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: build/compiler test-builtin-varargs\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-builtin-varargs: table initialization failed\n");
    }
    else
    {
        CBuiltinVaLowerTestTally tally = { 0 };
        CBuiltinVaLowerTestNative native = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 26);
        bool valid = cbuiltin_va_lower_test_expect(&tally, arena.base != 0 && sizeof(va_list) == sizeof(AbiVaList), "arena and host va_list layout", 0);
        cbuiltin_va_lower_test_guards(&tally);
        for (uint32_t p = 0; valid && p < BUSTER_CBUILTIN_VA_TEST_PROGRAMS; p += 1)
        {
            const bool argument = p <= BUSTER_CBUILTIN_VA_TEST_STACK;
            const uint32_t class = p < BUSTER_CBUILTIN_VA_TEST_CLASSES ? cbuiltin_va_lower_test_classes[p] : BUSTER_TYPED_CLASS_U64;
            CBuiltinVaLowerCall call =
            {
                .id = argument ? CBUILTIN_VA_ARG : p == BUSTER_CBUILTIN_VA_TEST_START ? CBUILTIN_VA_START : p == BUSTER_CBUILTIN_VA_TEST_C23_START ? CBUILTIN_C23_VA_START : p == BUSTER_CBUILTIN_VA_TEST_COPY ? CBUILTIN_VA_COPY : CBUILTIN_VA_END,
                .list = 0,
                .source = 1,
                .register_save_area = 1,
                .overflow_arg_area = 2,
                .gp_offset = 40,
                .fp_offset = 160,
                .result_class = class,
                .plan = cbuiltin_va_lower_test_plan(class, p == BUSTER_CBUILTIN_VA_TEST_STACK),
            };
            const uint32_t parameters = p == BUSTER_CBUILTIN_VA_TEST_START || p == BUSTER_CBUILTIN_VA_TEST_C23_START ? 3 : p == BUSTER_CBUILTIN_VA_TEST_END ? 1 : 2;
            CBuiltinVaLowerTestProgram programs[2] = { 0 };
            valid = cbuiltin_va_lower_test_program(&tally, &arena, &call, parameters, argument, programs, p);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                native.programs[path][p] = programs[path];
            }
        }
        // An aggregate helper per shape: va_arg into its frame object (local 2) and the bytes copied out.
        for (uint32_t k = 0; valid && k < BUSTER_CBUILTIN_VA_TEST_SHAPES; k += 1)
        {
            const CBuiltinVaLowerCall call =
            {
                .id = CBUILTIN_VA_ARG,
                .list = 0,
                .result_class = BUSTER_TYPED_CLASS_U64,
                .plan = cbuiltin_va_lower_test_shape_plan(&cbuiltin_va_lower_test_shapes[k]),
                .size = cbuiltin_va_lower_test_shapes[k].size,
                .object = 2,
            };
            CBuiltinVaLowerTestProgram programs[2] = { 0 };
            valid = cbuiltin_va_lower_test_program(&tally, &arena, &call, 2, true, programs, BUSTER_CBUILTIN_VA_TEST_PROGRAMS + k);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                native.programs[path][BUSTER_CBUILTIN_VA_TEST_PROGRAMS + k] = programs[path];
            }
        }
        CBuiltinVaLowerTestTally* shared = MAP_FAILED;
        if (valid)
        {
            shared = mmap(0, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            valid = cbuiltin_va_lower_test_expect(&tally, shared != MAP_FAILED, "native tally allocation", 0);
        }
        if (valid)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                alarm(10);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    cbuiltin_va_lower_test_synthetic(shared, &native, path);
                    cbuiltin_va_lower_test_real_lists(shared, &native, path);
                    cbuiltin_va_lower_test_real_shapes(shared, &native, path);
                }
                _exit(shared->failures == 0 ? 0 : 1);
            }
            int status = 0;
            const bool waited = child > 0 && waitpid(child, &status, 0) == child;
            cbuiltin_va_lower_test_expect(&tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child", (uint32_t)status);
        }
        if (shared != MAP_FAILED)
        {
            tally.checks += shared->checks;
            tally.failures += shared->failures;
            munmap(shared, sizeof(*shared));
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t p = 0; p < BUSTER_CBUILTIN_VA_TEST_HELPERS; p += 1)
            {
                if (native.programs[path][p].bytes != 0 && native.programs[path][p].bytes != MAP_FAILED)
                {
                    munmap(native.programs[path][p].bytes, native.programs[path][p].mapping_bytes);
                }
            }
        }
        arena_release(&arena);
        printf("test-builtin-varargs: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
