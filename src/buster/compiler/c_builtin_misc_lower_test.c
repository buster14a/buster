// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original native Tier 0 consumers for #148's memory/hint recipes. The host Clang/libc provides an independent
// memory oracle; generated helpers run both publication and backend twins. A live U64 stack value spans every
// recipe, including its back edges and joins. Exact guarded tails and snapshot refusals test publication separately.
// Native calls execute in an alarm-bounded child; these fixtures establish no source/Tier 1 integration claim.

#define BUSTER_CBUILTIN_MISC_TEST_ENTRIES 2048u
#define BUSTER_CBUILTIN_MISC_TEST_LOCALS 64u
#define BUSTER_CBUILTIN_MISC_TEST_PROGRAMS 10u
#define BUSTER_CBUILTIN_MISC_TEST_PAGE 4096u
#define BUSTER_CBUILTIN_MISC_TEST_SENTINEL UINT64_C(0x4563)

typedef struct CBuiltinMiscLowerTestTally CBuiltinMiscLowerTestTally;
struct CBuiltinMiscLowerTestTally
{
    uint64_t checks;
    uint64_t failures;
};

typedef struct CBuiltinMiscLowerTestStream CBuiltinMiscLowerTestStream;
struct CBuiltinMiscLowerTestStream
{
    uint8_t kinds[BUSTER_CBUILTIN_MISC_TEST_ENTRIES];
    uint8_t classes[BUSTER_CBUILTIN_MISC_TEST_ENTRIES];
    uint64_t values[BUSTER_CBUILTIN_MISC_TEST_ENTRIES];
    uint8_t local_classes[BUSTER_CBUILTIN_MISC_TEST_LOCALS];
    uint8_t local_flags[BUSTER_CBUILTIN_MISC_TEST_LOCALS];
    CBuiltinLowerBuffer buffer;
};

typedef struct CBuiltinMiscLowerTestProgram CBuiltinMiscLowerTestProgram;
struct CBuiltinMiscLowerTestProgram
{
    uint8_t* bytes;
    uint64_t mapping_bytes;
    uint32_t byte_count;
};

static const CBuiltinId cbuiltin_misc_lower_test_ids[BUSTER_CBUILTIN_MISC_TEST_PROGRAMS] =
{
    CBUILTIN_EXPECT,
    CBUILTIN_ASSUME_ALIGNED,
    CBUILTIN_PREFETCH,
    CBUILTIN_EXTRACT_RETURN_ADDR,
    CBUILTIN_FROB_RETURN_ADDR,
    CBUILTIN_MEMCPY,
    CBUILTIN_MEMMOVE,
    CBUILTIN_MEMSET,
    CBUILTIN_MEMCMP,
    CBUILTIN_STRLEN,
};

static bool cbuiltin_misc_lower_test_expect(CBuiltinMiscLowerTestTally* tally, bool condition, const char* check, uint32_t item)
{
    tally->checks += 1;
    if (!condition)
    {
        if (tally->failures < 24)
        {
            fprintf(stderr, "test-builtin-misc-lowering: %s item %u\n", check, item);
        }
        tally->failures += 1;
    }
    return condition;
}

static void cbuiltin_misc_lower_test_stream(CBuiltinMiscLowerTestStream* stream, CBuiltinId id)
{
    memset(stream, 0, sizeof(*stream));
    stream->buffer.kinds = stream->kinds;
    stream->buffer.classes = stream->classes;
    stream->buffer.values = stream->values;
    stream->buffer.capacity = BUSTER_CBUILTIN_MISC_TEST_ENTRIES;
    stream->buffer.local_classes = stream->local_classes;
    stream->buffer.local_flags = stream->local_flags;
    stream->buffer.local_count = 3;
    stream->buffer.local_capacity = BUSTER_CBUILTIN_MISC_TEST_LOCALS;
    memset(stream->local_classes, BUSTER_TYPED_CLASS_U64, 3);
    if (id == CBUILTIN_EXPECT)
    {
        stream->local_classes[0] = BUSTER_TYPED_CLASS_I64;
        stream->local_classes[1] = BUSTER_TYPED_CLASS_I64;
    }
    else if (id == CBUILTIN_MEMSET)
    {
        stream->local_classes[1] = BUSTER_TYPED_CLASS_I32;
    }
}

static void cbuiltin_misc_lower_test_emit(CBuiltinMiscLowerTestStream* stream, uint32_t kind, uint32_t class, uint64_t value)
{
    const uint32_t i = stream->buffer.count;
    assert(i < stream->buffer.capacity);
    stream->kinds[i] = (uint8_t)kind;
    stream->classes[i] = (uint8_t)class;
    stream->values[i] = value;
    stream->buffer.count += 1;
}

static CBuiltinMiscLowerCall cbuiltin_misc_lower_test_call(CBuiltinId id)
{
    const bool unary = id == CBUILTIN_EXTRACT_RETURN_ADDR || id == CBUILTIN_FROB_RETURN_ADDR || id == CBUILTIN_STRLEN || id == CBUILTIN_ALLOCA;
    const bool frame = id == CBUILTIN_FRAME_ADDRESS || id == CBUILTIN_RETURN_ADDRESS;
    const CBuiltinMiscLowerCall result =
    {
        .id = id,
        .arguments = { 0, 1, 2 },
        .argument_count = frame ? 0 : unary ? 1 : id == CBUILTIN_EXPECT ? 2 : 3,
    };
    return result;
}

static bool cbuiltin_misc_lower_test_program(CBuiltinMiscLowerTestTally* tally, Arena* arena, CBuiltinMiscLowerCall call, CBuiltinMiscLowerTestProgram programs[2], uint32_t item)
{
    const uint64_t mark = arena->length;
    CBuiltinMiscLowerTestStream streams[2];
    CBuiltinLowerResult lowered[2] = { 0 };
    bool result = true;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        CBuiltinMiscLowerTestStream* stream = streams + path;
        cbuiltin_misc_lower_test_stream(stream, call.id);
        cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, 0x73);
        cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_DISCARD, BUSTER_TYPED_CLASS_U64, 0);
        cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, BUSTER_CBUILTIN_MISC_TEST_SENTINEL);
        lowered[path] = path == 0 ? cbuiltin_misc_lower_scalar(&stream->buffer, &call) : cbuiltin_misc_lower(&stream->buffer, &call);
        result &= cbuiltin_misc_lower_test_expect(tally, lowered[path].error == CBUILTIN_LOWER_OK && lowered[path].first == 3, "append after live-stack prefix", item);
        if (lowered[path].result_class != BUSTER_TYPED_CLASS_VOID)
        {
            cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
        }
        cbuiltin_misc_lower_test_emit(stream, BUSTER_TYPED_RETURN, BUSTER_TYPED_CLASS_U64, 0);
    }
    result &= cbuiltin_misc_lower_test_expect(tally, memcmp(&lowered[0], &lowered[1], sizeof(lowered[0])) == 0 &&
        streams[0].buffer.count == streams[1].buffer.count && streams[0].buffer.local_count == streams[1].buffer.local_count &&
        memcmp(streams[0].kinds, streams[1].kinds, streams[0].buffer.count) == 0 &&
        memcmp(streams[0].classes, streams[1].classes, streams[0].buffer.count) == 0 &&
        memcmp(streams[0].values, streams[1].values, streams[0].buffer.count * sizeof(uint64_t)) == 0 &&
        memcmp(streams[0].local_classes, streams[1].local_classes, streams[0].buffer.local_count) == 0 &&
        memcmp(streams[0].local_flags, streams[1].local_flags, streams[0].buffer.local_count) == 0, "publication twins", item);
    const AbiLocation parameters[3] =
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
            .parameter_count = 3,
            .parameters = parameters,
            .result =
            {
                .kind = BUSTER_ABI_RESULT_REGISTERS,
                .part_bytes = 8,
                .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
            },
        };
        const Tier0Check checked = tier0_validate(&function);
        result = cbuiltin_misc_lower_test_expect(tally, checked.error == TIER0_OK, checked.rule != 0 ? checked.rule : "typed validation", item);
        if (result)
        {
            codes[path] = path == 0 ? tier0_compile_scalar(&function, arena) : tier0_compile(&function, arena);
            result = cbuiltin_misc_lower_test_expect(tally, codes[path].error == TIER0_OK && codes[path].reference_count == 0 &&
                x86_validate(codes[path].records, codes[path].count) == codes[path].count, "native records", item);
        }
        if (result)
        {
            const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(codes[path].count);
            const uint64_t pages = (capacity + BUSTER_CBUILTIN_MISC_TEST_PAGE - 1) & ~(uint64_t)(BUSTER_CBUILTIN_MISC_TEST_PAGE - 1);
            programs[path].mapping_bytes = pages + BUSTER_CBUILTIN_MISC_TEST_PAGE;
            programs[path].bytes = mmap(0, programs[path].mapping_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)codes[path].count + 1);
            result = cbuiltin_misc_lower_test_expect(tally, programs[path].bytes != MAP_FAILED && offsets != 0, "native allocation", item);
            if (result)
            {
                const BranchSizing sized = path == 0 ? size_branches_scalar(codes[path].records, codes[path].count, codes[path].branches, codes[path].targets, codes[path].branch_count, offsets)
                                                    : size_branches(codes[path].records, codes[path].count, codes[path].branches, codes[path].targets, codes[path].branch_count, offsets);
                const int64_t encoded = sized.error != BRANCH_SIZING_OK ? -1 : path == 0 ? x86_encode_scalar(codes[path].records, codes[path].count, programs[path].bytes, capacity, 0, 0)
                                                                                      : x86_encode(codes[path].records, codes[path].count, programs[path].bytes, capacity, 0, 0);
                result = cbuiltin_misc_lower_test_expect(tally, encoded > 0 && mprotect(programs[path].bytes, pages, PROT_READ | PROT_EXEC) == 0 &&
                    mprotect(programs[path].bytes + pages, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0, "native encoding", item);
                programs[path].byte_count = encoded > 0 ? (uint32_t)encoded : 0;
            }
        }
    }
    if (result)
    {
        result = cbuiltin_misc_lower_test_expect(tally, cbuiltin_lower_test_same_code(&codes[0], &codes[1]) && programs[0].byte_count == programs[1].byte_count &&
            memcmp(programs[0].bytes, programs[1].bytes, programs[0].byte_count) == 0, "backend and encoded twins", item);
    }
    arena_pop_to(arena, mark);
    return result;
}

// Generated entry points have no Clang function-type prefix.
static __attribute__((no_sanitize("function"))) uint64_t cbuiltin_misc_lower_test_invoke(const CBuiltinMiscLowerTestProgram* program, uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t (*entry)(uint64_t, uint64_t, uint64_t) = (uint64_t (*)(uint64_t, uint64_t, uint64_t))(void*)program->bytes;
    const uint64_t result = entry(a, b, c) - BUSTER_CBUILTIN_MISC_TEST_SENTINEL;
    return result;
}

static int cbuiltin_misc_lower_test_sign(int value)
{
    const int result = (value > 0) - (value < 0);
    return result;
}

static void cbuiltin_misc_lower_test_constants(CBuiltinMiscLowerTestTally* tally)
{
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    uint8_t* left = mmap(0, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* right = mmap(0, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = left != MAP_FAILED && right != MAP_FAILED &&
        mprotect(left + BUSTER_CBUILTIN_MISC_TEST_PAGE, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0 &&
        mprotect(right + BUSTER_CBUILTIN_MISC_TEST_PAGE, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0;
    if (cbuiltin_misc_lower_test_expect(tally, arena.base != 0 && guarded, "constant arena and guards", 0))
    {
        for (uint32_t p = 5; p <= 8; p += 1)
        {
            for (uint32_t size = 0; size <= BUSTER_CBUILTIN_MISC_SMALL_BYTES + 1; size += 1)
            {
                CBuiltinMiscLowerCall call = cbuiltin_misc_lower_test_call(cbuiltin_misc_lower_test_ids[p]);
                call.constant_size_known = true;
                call.constant_size = size;
                CBuiltinMiscLowerTestProgram programs[2] = { 0 };
                const bool valid = cbuiltin_misc_lower_test_program(tally, &arena, call, programs, p * 100 + size);
                for (uint32_t path = 0; valid && path < 2; path += 1)
                {
                    for (uint32_t alignment = 0; alignment < 16; alignment += 1)
                    {
                        for (int distance = -17; distance <= 17; distance += 1)
                        {
                            uint8_t actual[256];
                            uint8_t expected[256];
                            uint8_t source[256];
                            for (uint32_t i = 0; i < sizeof(source); i += 1)
                            {
                                source[i] = (uint8_t)(i * 149u + 73u);
                                actual[i] = expected[i] = source[i];
                            }
                            const uint32_t at = 80 + alignment;
                            uint8_t* target = actual + at + distance;
                            uint8_t* destination = expected + at + distance;
                            const uint8_t* from = p == 6 ? actual + at : source + at;
                            uint64_t wanted = (uintptr_t)target;
                            uint64_t second = (uintptr_t)from;
                            if (p == 5)
                            {
                                memcpy(destination, source + at, size);
                            }
                            else if (p == 6)
                            {
                                memmove(destination, expected + at, size);
                            }
                            else if (p == 7)
                            {
                                second = (uint64_t)(int64_t)(distance * 129);
                                memset(destination, distance * 129, size);
                            }
                            else
                            {
                                wanted = (uint64_t)(int64_t)memcmp(target, from, size);
                            }
                            const uint64_t value = cbuiltin_misc_lower_test_invoke(programs + path, (uintptr_t)target, second, size);
                            const bool equal = p == 8 ? cbuiltin_misc_lower_test_sign((int)(uint32_t)value) == cbuiltin_misc_lower_test_sign((int)(uint32_t)wanted) : value == wanted;
                            cbuiltin_misc_lower_test_expect(tally, equal && memcmp(actual, expected, sizeof(actual)) == 0, "constant sizes, close overlap and guards", size * 10000 + p * 100 + alignment);
                        }
                    }
                    memset(left, 0x80, BUSTER_CBUILTIN_MISC_TEST_PAGE);
                    memset(right, 0x80, BUSTER_CBUILTIN_MISC_TEST_PAGE);
                    uint8_t* source = left + BUSTER_CBUILTIN_MISC_TEST_PAGE - size;
                    uint8_t* target = right + BUSTER_CBUILTIN_MISC_TEST_PAGE - size;
                    const uint64_t value = cbuiltin_misc_lower_test_invoke(programs + path, (uintptr_t)target, p == 7 ? 0x180 : (uintptr_t)source, size);
                    cbuiltin_misc_lower_test_expect(tally, value == (p == 8 ? 0 : (uintptr_t)target) && memcmp(target, source, size) == 0, "constant exact guarded tail, including zero", size);
                    if (p == 6 && size != 0)
                    {
                        for (uint32_t direction = 0; direction < 2; direction += 1)
                        {
                            uint8_t expected[128];
                            for (uint32_t i = 0; i < sizeof(expected); i += 1)
                            {
                                expected[i] = left[BUSTER_CBUILTIN_MISC_TEST_PAGE - sizeof(expected) + i] = (uint8_t)(i * 149 + 73);
                            }
                            uint8_t* end = left + BUSTER_CBUILTIN_MISC_TEST_PAGE;
                            uint8_t* to = end - size - direction;
                            uint8_t* from = end - size - (1 - direction);
                            memmove(expected + sizeof(expected) - size - direction, expected + sizeof(expected) - size - (1 - direction), size);
                            const uint64_t moved = cbuiltin_misc_lower_test_invoke(programs + path, (uintptr_t)to, (uintptr_t)from, size);
                            cbuiltin_misc_lower_test_expect(tally, moved == (uintptr_t)to && memcmp(end - sizeof(expected), expected, sizeof(expected)) == 0, "constant overlap at guarded end", size);
                        }
                    }
                }
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    if (programs[path].bytes != 0 && programs[path].bytes != MAP_FAILED)
                    {
                        munmap(programs[path].bytes, programs[path].mapping_bytes);
                    }
                }
            }
        }
    }
    if (left != MAP_FAILED)
    {
        munmap(left, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2);
    }
    if (right != MAP_FAILED)
    {
        munmap(right, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2);
    }
    arena_release(&arena);
}

static void cbuiltin_misc_lower_test_large(CBuiltinMiscLowerTestTally* tally, CBuiltinMiscLowerTestProgram programs[BUSTER_CBUILTIN_MISC_TEST_PROGRAMS], uint32_t path)
{
    static const uint32_t lengths[] =
    {
        256,
        4095,
        4096,
        1048576,
    };
    const uint32_t capacity = 1048576 + 256;
    uint8_t* source = malloc(capacity);
    uint8_t* actual = malloc(capacity);
    uint8_t* expected = malloc(capacity);
    if (cbuiltin_misc_lower_test_expect(tally, source != 0 && actual != 0 && expected != 0, "large buffers", path))
    {
        for (uint32_t size = 0; size < sizeof(lengths) / sizeof(lengths[0]); size += 1)
        {
            const uint32_t n = lengths[size];
            for (uint32_t alignment = 0; alignment < 16; alignment += 1)
            {
                const uint32_t at = 64 + alignment;
                for (uint32_t i = 0; i < capacity; i += 1)
                {
                    source[i] = (uint8_t)(1 + i % 255);
                    actual[i] = expected[i] = 0xa5;
                }
                memcpy(expected + at, source + 64, n);
                uint64_t value = cbuiltin_misc_lower_test_invoke(programs + 5, (uintptr_t)(actual + at), (uintptr_t)(source + 64), n);
                cbuiltin_misc_lower_test_expect(tally, value == (uintptr_t)(actual + at) && memcmp(actual, expected, capacity) == 0, "large copy", n);
                memset(expected + at, 0x80, n);
                value = cbuiltin_misc_lower_test_invoke(programs + 7, (uintptr_t)(actual + at), 0x180, n);
                cbuiltin_misc_lower_test_expect(tally, value == (uintptr_t)(actual + at) && memcmp(actual, expected, capacity) == 0, "large fill", n);
                for (int distance = -17; distance <= 17; distance += 1)
                {
                    memcpy(actual, source, capacity);
                    memcpy(expected, source, capacity);
                    memmove(expected + at + distance, expected + at, n);
                    value = cbuiltin_misc_lower_test_invoke(programs + 6, (uintptr_t)(actual + at + distance), (uintptr_t)(actual + at), n);
                    cbuiltin_misc_lower_test_expect(tally, value == (uintptr_t)(actual + at + distance) && memcmp(actual, expected, capacity) == 0, "large close overlap both directions", n);
                }
                for (uint32_t difference = 0; difference <= 8; difference += 1)
                {
                    memcpy(actual + at, source + 64, n);
                    if (difference != 0)
                    {
                        // Opposite orders in adjacent bytes catch integer-order comparisons and an imprecise first-byte mask.
                        const uint32_t byte = n / 2 + difference - 1;
                        actual[at + byte] = source[64 + byte] ^ 0x80;
                        actual[at + byte + 1] = source[64 + byte + 1] ^ 0x80;
                    }
                    const int oracle = memcmp(actual + at, source + 64, n);
                    value = cbuiltin_misc_lower_test_invoke(programs + 8, (uintptr_t)(actual + at), (uintptr_t)(source + 64), n);
                    cbuiltin_misc_lower_test_expect(tally, cbuiltin_misc_lower_test_sign((int)(uint32_t)value) == cbuiltin_misc_lower_test_sign(oracle), "large compare and each mismatch byte", n);
                }
                source[at + n] = 0;
                value = cbuiltin_misc_lower_test_invoke(programs + 9, (uintptr_t)(source + at), 0, 0);
                cbuiltin_misc_lower_test_expect(tally, value == n, "large length", n);
            }
        }
    }
    free(source);
    free(actual);
    free(expected);
}

static void cbuiltin_misc_lower_test_memory(CBuiltinMiscLowerTestTally* tally, CBuiltinMiscLowerTestProgram programs[BUSTER_CBUILTIN_MISC_TEST_PROGRAMS], uint32_t path)
{
    static const uint32_t lengths[] = { 0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 65, 255 };
    static const int values[] = { -1, 0, 1, 127, 128, 255, 256, 511 };
    uint8_t source[1024];
    uint8_t actual[1024];
    uint8_t expected[1024];
    uint32_t item = path * 100000;
    for (uint32_t alignment = 0; alignment < 16; alignment += 1)
    {
        for (uint32_t size = 0; size < sizeof(lengths) / sizeof(lengths[0]); size += 1)
        {
            const uint32_t n = lengths[size];
            for (uint32_t i = 0; i < sizeof(source); i += 1)
            {
                source[i] = (uint8_t)(i * 149u + 73u);
                actual[i] = expected[i] = 0xa5;
            }
            const uint32_t destination = 64 + alignment;
            const uint32_t from = 64 + (15 - alignment);
            memcpy(expected + destination, source + from, n);
            uint64_t result = cbuiltin_misc_lower_test_invoke(programs + 5, (uintptr_t)(actual + destination), (uintptr_t)(source + from), n);
            cbuiltin_misc_lower_test_expect(tally, result == (uintptr_t)(actual + destination) && memcmp(actual, expected, sizeof(actual)) == 0, "memcpy value, alignment and guards", item++);
            for (uint32_t fill = 0; fill < sizeof(values) / sizeof(values[0]); fill += 1)
            {
                memset(actual, 0xa5, sizeof(actual));
                memset(expected, 0xa5, sizeof(expected));
                memset(expected + destination, values[fill], n);
                result = cbuiltin_misc_lower_test_invoke(programs + 7, (uintptr_t)(actual + destination), (uint64_t)(int64_t)values[fill], n);
                cbuiltin_misc_lower_test_expect(tally, result == (uintptr_t)(actual + destination) && memcmp(actual, expected, sizeof(actual)) == 0, "memset low byte and guards", item++);
            }
            static const int offsets[] = { -256, -17, -1, 0, 1, 17, 256 };
            for (uint32_t overlap = 0; overlap < sizeof(offsets) / sizeof(offsets[0]); overlap += 1)
            {
                for (uint32_t i = 0; i < sizeof(actual); i += 1)
                {
                    actual[i] = expected[i] = source[i];
                }
                const uint32_t start = 384 + alignment;
                const uint32_t target = (uint32_t)((int)start + offsets[overlap]);
                memmove(expected + target, expected + start, n);
                result = cbuiltin_misc_lower_test_invoke(programs + 6, (uintptr_t)(actual + target), (uintptr_t)(actual + start), n);
                cbuiltin_misc_lower_test_expect(tally, result == (uintptr_t)(actual + target) && memcmp(actual, expected, sizeof(actual)) == 0, "memmove forward/backward and guards", item++);
            }
            memcpy(actual, source, sizeof(actual));
            for (uint32_t difference = 0; difference < 4; difference += 1)
            {
                memcpy(expected, source, sizeof(expected));
                if (n != 0 && difference != 0)
                {
                    const uint32_t at = difference == 1 ? 0 : difference == 2 ? n / 2 : n - 1;
                    expected[from + at] ^= 0x80;
                }
                const int oracle = memcmp(actual + from, expected + from, n);
                result = cbuiltin_misc_lower_test_invoke(programs + 8, (uintptr_t)(actual + from), (uintptr_t)(expected + from), n);
                cbuiltin_misc_lower_test_expect(tally, cbuiltin_misc_lower_test_sign((int)(uint32_t)result) == cbuiltin_misc_lower_test_sign(oracle) && memcmp(actual, source, sizeof(actual)) == 0,
                    "memcmp unsigned order and no writes", item++);
                result = cbuiltin_misc_lower_test_invoke(programs + 8, (uintptr_t)(expected + from), (uintptr_t)(actual + from), n);
                cbuiltin_misc_lower_test_expect(tally, cbuiltin_misc_lower_test_sign((int)(uint32_t)result) == -cbuiltin_misc_lower_test_sign(oracle), "memcmp reversed order", item++);
            }
            memset(actual, 0x81, sizeof(actual));
            actual[destination + n] = 0;
            memset(actual + destination + n + 1, 1, 8);
            result = cbuiltin_misc_lower_test_invoke(programs + 9, (uintptr_t)(actual + destination), 0, 0);
            cbuiltin_misc_lower_test_expect(tally, result == strlen((const char*)actual + destination), "strlen alignments and borrow after zero", item++);
        }
    }
}

static void cbuiltin_misc_lower_test_native_guards(CBuiltinMiscLowerTestTally* tally, CBuiltinMiscLowerTestProgram programs[BUSTER_CBUILTIN_MISC_TEST_PROGRAMS], uint32_t path)
{
    uint8_t* left = mmap(0, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t* right = mmap(0, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = left != MAP_FAILED && right != MAP_FAILED;
    bool protected = false;
    if (mapped)
    {
        protected = mprotect(left + BUSTER_CBUILTIN_MISC_TEST_PAGE, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0 &&
                    mprotect(right + BUSTER_CBUILTIN_MISC_TEST_PAGE, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0;
    }
    if (cbuiltin_misc_lower_test_expect(tally, mapped && protected, "native memory guard allocation", path))
    {
        const uintptr_t unreadable_left = (uintptr_t)(left + BUSTER_CBUILTIN_MISC_TEST_PAGE);
        const uintptr_t unreadable_right = (uintptr_t)(right + BUSTER_CBUILTIN_MISC_TEST_PAGE);
        for (uint32_t p = 5; p <= 8; p += 1)
        {
            const uint64_t result = cbuiltin_misc_lower_test_invoke(programs + p, unreadable_left, p == 7 ? UINT64_MAX : unreadable_right, 0);
            cbuiltin_misc_lower_test_expect(tally, result == (p == 8 ? 0 : unreadable_left), "zero-length has no memory access", path * 10 + p);
        }
        for (uint32_t n = 0; n <= 255; n += 1)
        {
            memset(left, 0x80, BUSTER_CBUILTIN_MISC_TEST_PAGE);
            memset(right, 0xa5, BUSTER_CBUILTIN_MISC_TEST_PAGE);
            left[BUSTER_CBUILTIN_MISC_TEST_PAGE - 1] = 0;
            uint8_t* source = left + BUSTER_CBUILTIN_MISC_TEST_PAGE - 1 - n;
            uint8_t* target = right + BUSTER_CBUILTIN_MISC_TEST_PAGE - 1 - n;
            uint64_t result = cbuiltin_misc_lower_test_invoke(programs + 9, (uintptr_t)source, 0, 0);
            cbuiltin_misc_lower_test_expect(tally, result == n, "strlen exact terminated tail", path * 1000 + n);
            result = cbuiltin_misc_lower_test_invoke(programs + 5, (uintptr_t)target, (uintptr_t)source, n + 1);
            cbuiltin_misc_lower_test_expect(tally, result == (uintptr_t)target && memcmp(source, target, n + 1) == 0 && target[-1] == 0xa5, "memcpy exact guarded tail", path * 1000 + n);
            result = cbuiltin_misc_lower_test_invoke(programs + 8, (uintptr_t)target, (uintptr_t)source, n + 1);
            cbuiltin_misc_lower_test_expect(tally, result == 0, "memcmp exact guarded tail", path * 1000 + n);
            result = cbuiltin_misc_lower_test_invoke(programs + 7, (uintptr_t)target, 0xff, n + 1);
            bool filled = result == (uintptr_t)target && target[-1] == 0xa5;
            for (uint32_t i = 0; i <= n; i += 1)
            {
                filled &= target[i] == 0xff;
            }
            cbuiltin_misc_lower_test_expect(tally, filled, "memset exact guarded tail", path * 1000 + n);
        }
    }
    if (left != MAP_FAILED)
    {
        munmap(left, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2);
    }
    if (right != MAP_FAILED)
    {
        munmap(right, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2);
    }
}

static void cbuiltin_misc_lower_test_hints(CBuiltinMiscLowerTestTally* tally, CBuiltinMiscLowerTestProgram programs[BUSTER_CBUILTIN_MISC_TEST_PROGRAMS], uint32_t path)
{
    static const uint64_t inputs[] = { 0, 1, UINT64_MAX, UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff), UINT64_C(0x123456789abcdef0) };
    for (uint32_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i += 1)
    {
        const uint64_t result = cbuiltin_misc_lower_test_invoke(programs, inputs[i], inputs[(i + 1) % (sizeof(inputs) / sizeof(inputs[0]))], 0);
        cbuiltin_misc_lower_test_expect(tally, result == inputs[i], "expect returns first converted long", path * 10 + i);
        for (uint32_t p = 1; p <= 4; p += 1)
        {
            const uint64_t value = cbuiltin_misc_lower_test_invoke(programs + p, inputs[i], 16, 0);
            cbuiltin_misc_lower_test_expect(tally, value == (p == 2 ? 0 : inputs[i]), "hint identity or no-op preserves stack", path * 100 + p * 10 + i);
        }
    }
}

static void cbuiltin_misc_lower_test_guards(CBuiltinMiscLowerTestTally* tally)
{
    uint8_t* pages[5];
    bool mapped = true;
    for (uint32_t i = 0; i < 5; i += 1)
    {
        pages[i] = mmap(0, BUSTER_CBUILTIN_MISC_TEST_PAGE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mapped &= pages[i] != MAP_FAILED;
        if (pages[i] != MAP_FAILED)
        {
            mapped &= mprotect(pages[i] + BUSTER_CBUILTIN_MISC_TEST_PAGE, BUSTER_CBUILTIN_MISC_TEST_PAGE, PROT_NONE) == 0;
        }
    }
    if (cbuiltin_misc_lower_test_expect(tally, mapped, "publication guard allocation", 0))
    {
        for (uint32_t p = 0; p < BUSTER_CBUILTIN_MISC_TEST_PROGRAMS; p += 1)
        {
            const CBuiltinId id = cbuiltin_misc_lower_test_ids[p];
            const CBuiltinMiscLowerCall call = cbuiltin_misc_lower_test_call(id);
            CBuiltinMiscLowerTestStream stream;
            cbuiltin_misc_lower_test_stream(&stream, id);
            const CBuiltinLowerResult expected = cbuiltin_misc_lower_scalar(&stream.buffer, &call);
            const bool bounded = expected.error == CBUILTIN_LOWER_OK && expected.count < BUSTER_CBUILTIN_MISC_TEST_PAGE / sizeof(uint64_t) - 2;
            if (cbuiltin_misc_lower_test_expect(tally, bounded, "recipe bound", p))
            {
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    for (uint32_t variant = 0; variant < 7; variant += 1)
                    {
                        if ((variant == 1 && expected.count == 0) || (variant == 2 && expected.temporary_count == 0))
                        {
                            continue;
                        }
                        const uint32_t entries = expected.count + 2;
                        const uint32_t locals = expected.temporary_count + 3;
                        CBuiltinLowerBuffer buffer =
                        {
                            .kinds = pages[0] + BUSTER_CBUILTIN_MISC_TEST_PAGE - entries,
                            .classes = pages[1] + BUSTER_CBUILTIN_MISC_TEST_PAGE - entries,
                            .values = (uint64_t*)(void*)(pages[2] + BUSTER_CBUILTIN_MISC_TEST_PAGE - entries * sizeof(uint64_t)),
                            .count = 2,
                            .capacity = entries - (variant == 1),
                            .local_classes = pages[3] + BUSTER_CBUILTIN_MISC_TEST_PAGE - locals,
                            .local_flags = pages[4] + BUSTER_CBUILTIN_MISC_TEST_PAGE - locals,
                            .local_count = 3,
                            .local_capacity = locals - (variant == 2),
                        };
                        for (uint32_t i = 0; i < 5; i += 1)
                        {
                            memset(pages[i], 0xa5, BUSTER_CBUILTIN_MISC_TEST_PAGE);
                        }
                        memcpy(buffer.local_classes, stream.local_classes, 3);
                        if (variant == 3)
                        {
                            buffer.local_classes[0] = BUSTER_TYPED_CLASS_F64;
                        }
                        CBuiltinMiscLowerCall request = call;
                        if (variant == 4)
                        {
                            request.argument_count = 4;
                        }
                        else if (variant == 5)
                        {
                            request.id = (CBuiltinId)UINT32_MAX;
                        }
                        else if (variant == 6)
                        {
                            request.arguments[0] = 3;
                        }
                        uint8_t snapshots[5][BUSTER_CBUILTIN_MISC_TEST_PAGE];
                        for (uint32_t i = 0; i < 5; i += 1)
                        {
                            memcpy(snapshots[i], pages[i], BUSTER_CBUILTIN_MISC_TEST_PAGE);
                        }
                        const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_misc_lower_scalar(&buffer, &request) : cbuiltin_misc_lower(&buffer, &request);
                        if (variant == 0)
                        {
                            bool valid = lowered.error == CBUILTIN_LOWER_OK && lowered.first == 2 && lowered.count == expected.count && buffer.count == entries && buffer.local_count == locals;
                            for (uint32_t i = 2; i < buffer.count; i += 1)
                            {
                                if (buffer.kinds[i] == BUSTER_TYPED_BRANCH_FALSE || buffer.kinds[i] == BUSTER_TYPED_BRANCH_TRUE || buffer.kinds[i] == BUSTER_TYPED_JUMP)
                                {
                                    valid &= buffer.values[i] >= 2 && buffer.values[i] < buffer.count;
                                }
                            }
                            valid &= buffer.kinds[0] == 0xa5 && buffer.kinds[1] == 0xa5 && buffer.classes[0] == 0xa5 && buffer.classes[1] == 0xa5;
                            valid &= buffer.local_flags[0] == 0xa5 && buffer.local_flags[1] == 0xa5 && buffer.local_flags[2] == 0xa5;
                            cbuiltin_misc_lower_test_expect(tally, valid, "guarded exact append and branch offsets", p * 10 + path);
                        }
                        else
                        {
                            const CBuiltinLowerError error = variant <= 2 ? CBUILTIN_LOWER_CAPACITY : variant == 5 ? CBUILTIN_LOWER_UNSUPPORTED : CBUILTIN_LOWER_ARGUMENT;
                            bool untouched = lowered.error == error && buffer.count == 2 && buffer.local_count == 3;
                            for (uint32_t i = 0; i < 5; i += 1)
                            {
                                untouched &= memcmp(snapshots[i], pages[i], BUSTER_CBUILTIN_MISC_TEST_PAGE) == 0;
                            }
                            cbuiltin_misc_lower_test_expect(tally, untouched, "transactional refusal", p * 100 + path * 10 + variant);
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
            munmap(pages[i], BUSTER_CBUILTIN_MISC_TEST_PAGE * 2);
        }
    }
    CBuiltinMiscLowerTestStream stream;
    cbuiltin_misc_lower_test_stream(&stream, CBUILTIN_MEMCPY);
    const CBuiltinMiscLowerCall call = cbuiltin_misc_lower_test_call(CBUILTIN_MEMCPY);
    const CBuiltinMiscLowerTestStream snapshot = stream;
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const CBuiltinLowerResult null_buffer = path == 0 ? cbuiltin_misc_lower_scalar(0, &call) : cbuiltin_misc_lower(0, &call);
        const CBuiltinLowerResult null_call = path == 0 ? cbuiltin_misc_lower_scalar(&stream.buffer, 0) : cbuiltin_misc_lower(&stream.buffer, 0);
        cbuiltin_misc_lower_test_expect(tally, null_buffer.error == CBUILTIN_LOWER_ARGUMENT && null_call.error == CBUILTIN_LOWER_ARGUMENT &&
            memcmp(&stream, &snapshot, sizeof(stream)) == 0, "null requests leave columns unchanged", path);
        static const CBuiltinId unsupported[] = { CBUILTIN_TRAP, CBUILTIN_UNREACHABLE };
        for (uint32_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i += 1)
        {
            CBuiltinMiscLowerCall request = call;
            request.id = unsupported[i];
            const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_misc_lower_scalar(&stream.buffer, &request) : cbuiltin_misc_lower(&stream.buffer, &request);
            cbuiltin_misc_lower_test_expect(tally, lowered.error == CBUILTIN_LOWER_UNSUPPORTED && memcmp(&stream, &snapshot, sizeof(stream)) == 0, "terminator operations refuse explicitly", path * 10 + i);
        }
        // The stack's and the frame's builtins: their arguments' counts (a size; no level, which the producer checked
        // was 0), each other count refused with the columns unchanged, and their entries exactly.
        static const CBuiltinId stack[] = { CBUILTIN_ALLOCA, CBUILTIN_FRAME_ADDRESS, CBUILTIN_RETURN_ADDRESS };
        static const uint8_t expected_kinds[3][4] = {
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_ALLOCA },
            { BUSTER_TYPED_FRAME_ADDRESS },
            { BUSTER_TYPED_FRAME_ADDRESS, BUSTER_TYPED_CONST, BUSTER_TYPED_ADD, BUSTER_TYPED_LOAD },
        };
        static const uint64_t expected_values[3][4] = { { 0, 0 }, { 0 }, { 0, 8, 0, 0 } };
        static const uint32_t expected_counts[3] = { 2, 1, 4 };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(stack); i += 1)
        {
            for (uint32_t count = 0; count <= 3; count += 1)
            {
                CBuiltinMiscLowerCall request = cbuiltin_misc_lower_test_call(stack[i]);
                const bool valid = count == request.argument_count;
                request.argument_count = count;
                cbuiltin_misc_lower_test_stream(&stream, stack[i]);
                const CBuiltinMiscLowerTestStream before = stream;
                const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_misc_lower_scalar(&stream.buffer, &request) : cbuiltin_misc_lower(&stream.buffer, &request);
                bool exact = valid ? lowered.error == CBUILTIN_LOWER_OK && lowered.first == 0 && lowered.count == expected_counts[i] &&
                                     lowered.result_class == BUSTER_TYPED_CLASS_U64 && lowered.temporary_count == 0 && stream.buffer.count == expected_counts[i]
                                   : lowered.error == CBUILTIN_LOWER_ARGUMENT && memcmp(&stream, &before, sizeof(stream)) == 0;
                for (uint32_t e = 0; valid && exact && e < expected_counts[i]; e += 1)
                {
                    exact = stream.kinds[e] == expected_kinds[i][e] && stream.classes[e] == BUSTER_TYPED_CLASS_U64 && stream.values[e] == expected_values[i][e];
                }
                cbuiltin_misc_lower_test_expect(tally, exact, "stack and frame recipes take their arguments' counts, and their entries exactly", path * 100 + i * 10 + count);
            }
        }
        for (uint32_t id = 0; id < 2; id += 1)
        {
            for (uint32_t count = id == 0 ? 2 : 1; count <= 3; count += 1)
            {
                CBuiltinMiscLowerCall request = cbuiltin_misc_lower_test_call(id == 0 ? CBUILTIN_ASSUME_ALIGNED : CBUILTIN_PREFETCH);
                request.argument_count = count;
                cbuiltin_misc_lower_test_stream(&stream, request.id);
                stream.local_classes[1] = BUSTER_TYPED_CLASS_I32;
                const CBuiltinLowerResult lowered = path == 0 ? cbuiltin_misc_lower_scalar(&stream.buffer, &request) : cbuiltin_misc_lower(&stream.buffer, &request);
                cbuiltin_misc_lower_test_expect(tally, lowered.error == CBUILTIN_LOWER_OK && lowered.count == (id == 0 ? 1u : 0u) && lowered.temporary_count == 0, "optional checked hint arguments", path * 100 + id * 10 + count);
            }
        }
        cbuiltin_misc_lower_test_stream(&stream, CBUILTIN_MEMCPY);
    }
}

// ALLOCA's and FRAME_ADDRESS's programs run natively, by both paths, from this function, whose own frame address their
// frames are below: an allocation's address is a multiple of 16 below it, by no more than its size rounded up to 16 and
// a page for the frames between, and so is a frame's address, by no more than a page. (RETURN_ADDRESS's value, a word of
// the frame, is the C programs' to check against clang's: c_check_program_test.c's frame-builtins.)
static __attribute__((noinline)) void cbuiltin_misc_lower_test_frames(CBuiltinMiscLowerTestTally* tally)
{
    static const uint64_t sizes[] = { 0, 1, 15, 16, 17, 4095, 65539 };
    const uint64_t frame = (uintptr_t)__builtin_frame_address(0);
    Arena arena = arena_reserve(UINT64_C(1) << 26);
    const bool reserved = cbuiltin_misc_lower_test_expect(tally, arena.base != 0, "frame arena", 0);
    for (uint32_t k = 0; reserved && k < 2; k += 1)
    {
        CBuiltinMiscLowerTestProgram programs[2] = { 0 };
        const CBuiltinId id = k == 0 ? CBUILTIN_ALLOCA : CBUILTIN_FRAME_ADDRESS;
        const bool valid = cbuiltin_misc_lower_test_program(tally, &arena, cbuiltin_misc_lower_test_call(id), programs, 700 + k);
        for (uint32_t path = 0; valid && path < 2; path += 1)
        {
            for (uint32_t s = 0; s < (k == 0 ? BUSTER_ARRAY_LENGTH(sizes) : 1); s += 1)
            {
                const uint64_t address = cbuiltin_misc_lower_test_invoke(programs + path, sizes[s], 0, 0);
                const uint64_t below = k == 0 ? ((sizes[s] + 15) & ~UINT64_C(15)) + BUSTER_CBUILTIN_MISC_TEST_PAGE : BUSTER_CBUILTIN_MISC_TEST_PAGE;
                cbuiltin_misc_lower_test_expect(tally, address % 16 == 0 && address < frame && frame - address <= below,
                                                k == 0 ? "native alloca: aligned, on the stack below its caller" : "native frame address: aligned, below its caller's",
                                                path * 100 + s);
            }
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (programs[path].bytes != 0 && programs[path].bytes != MAP_FAILED)
            {
                munmap(programs[path].bytes, programs[path].mapping_bytes);
            }
        }
    }
    arena_release(&arena);
}

static int run_builtin_misc_lower_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: build/compiler test-builtin-misc-lowering\n");
    }
    else if (!(x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize()))
    {
        fprintf(stderr, "test-builtin-misc-lowering: table initialization failed\n");
    }
    else
    {
        CBuiltinMiscLowerTestTally tally = { 0 };
        CBuiltinMiscLowerTestProgram native[2][BUSTER_CBUILTIN_MISC_TEST_PROGRAMS] = { 0 };
        Arena arena = arena_reserve(UINT64_C(1) << 26);
        bool valid = cbuiltin_misc_lower_test_expect(&tally, arena.base != 0, "arena allocation", 0);
        cbuiltin_misc_lower_test_guards(&tally);
        for (uint32_t p = 0; valid && p < BUSTER_CBUILTIN_MISC_TEST_PROGRAMS; p += 1)
        {
            CBuiltinMiscLowerTestProgram programs[2] = { 0 };
            valid = cbuiltin_misc_lower_test_program(&tally, &arena, cbuiltin_misc_lower_test_call(cbuiltin_misc_lower_test_ids[p]), programs, p);
            for (uint32_t path = 0; path < 2; path += 1)
            {
                native[path][p] = programs[path];
            }
        }
        CBuiltinMiscLowerTestTally* shared = MAP_FAILED;
        if (valid)
        {
            shared = mmap(0, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            valid = cbuiltin_misc_lower_test_expect(&tally, shared != MAP_FAILED, "native tally allocation", 0);
        }
        if (valid)
        {
            const pid_t child = fork();
            if (child == 0)
            {
                alarm(30);
                cbuiltin_misc_lower_test_constants(shared);
                cbuiltin_misc_lower_test_frames(shared);
                for (uint32_t path = 0; path < 2; path += 1)
                {
                    cbuiltin_misc_lower_test_hints(shared, native[path], path);
                    cbuiltin_misc_lower_test_memory(shared, native[path], path);
                    cbuiltin_misc_lower_test_native_guards(shared, native[path], path);
                    cbuiltin_misc_lower_test_large(shared, native[path], path);
                }
                _exit(shared->failures == 0 ? 0 : 1);
            }
            int status = 0;
            const bool waited = child > 0 && waitpid(child, &status, 0) == child;
            cbuiltin_misc_lower_test_expect(&tally, waited && WIFEXITED(status) && WEXITSTATUS(status) == 0, "native child", (uint32_t)status);
        }
        if (shared != MAP_FAILED)
        {
            tally.checks += shared->checks;
            tally.failures += shared->failures;
            munmap(shared, sizeof(*shared));
        }
        for (uint32_t path = 0; path < 2; path += 1)
        {
            for (uint32_t p = 0; p < BUSTER_CBUILTIN_MISC_TEST_PROGRAMS; p += 1)
            {
                if (native[path][p].bytes != 0 && native[path][p].bytes != MAP_FAILED)
                {
                    munmap(native[path][p].bytes, native[path][p].mapping_bytes);
                }
            }
        }
        arena_release(&arena);
        printf("test-builtin-misc-lowering: %" PRIu64 " checks, %" PRIu64 " failed\n", tally.checks, tally.failures);
        result = tally.failures == 0 ? 0 : 1;
    }
    return result;
}
