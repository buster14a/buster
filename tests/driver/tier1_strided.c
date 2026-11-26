// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2808: compile the SUBJECT twice, once with Buster and once with Clang -O0 and REFERENCE, then link this file's
// harness with both objects using Clang. The same original C kernels are the external compiler's oracle. Packed
// one-field records permit deliberately misaligned accesses; -fno-strict-aliasing matches Buster's aliasing contract
// for the different field views of the same bytes.
// Example (repeat the Buster compile/link/run at x86-64-v3, x86-64-v4 and znver5):
//
// clang -O0 -fno-strict-aliasing -DBUSTER_STRIDED_SUBJECT -DBUSTER_STRIDED_REFERENCE -c tests/driver/tier1_strided.c -o build/strided-reference.o
// build/compiler --tier1 --tier1-strict --tier1-passes=+vectorize --verify -O2 -fno-strict-aliasing -march=znver5 -DBUSTER_STRIDED_SUBJECT -c tests/driver/tier1_strided.c -o build/strided-checked.o
// clang -O2 -Wall -Wextra -Werror -fno-strict-aliasing tests/driver/tier1_strided.c build/strided-reference.o build/strided-checked.o -o build/test-strided
// build/test-strided
//
// Every result and every writable byte must agree. Guard pages bound both buffers; separate cases place each
// accessed span exactly against either end. Other cases alias the source/destination at small signed byte offsets.
// The sparse byte kernels cross inaccessible pages between every selected byte, exercising masked moves without
// permitting wide unmasked loads, a read/modify/write of padding, or access to unselected pages. Separate kernels
// cross virtual gaps of more than INT32_MAX elements to exercise qword-index gathers and scatters.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(BUSTER_STRIDED_SUBJECT)

typedef struct Strided8 Strided8;
struct __attribute__((packed)) Strided8
{
    uint8_t value;
};

typedef struct Strided16 Strided16;
struct __attribute__((packed)) Strided16
{
    uint16_t value;
};

typedef struct Strided32 Strided32;
struct __attribute__((packed)) Strided32
{
    uint32_t value;
};

typedef struct Strided64 Strided64;
struct __attribute__((packed)) Strided64
{
    uint64_t value;
};

typedef struct StridedFloat StridedFloat;
struct __attribute__((packed)) StridedFloat
{
    float value;
};

typedef struct StridedDouble StridedDouble;
struct __attribute__((packed)) StridedDouble
{
    double value;
};

typedef struct StridedRecord StridedRecord;
struct __attribute__((packed)) StridedRecord
{
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
};

#if defined(BUSTER_STRIDED_REFERENCE)
#define BUSTER_STRIDED_FUNCTION(name) reference_##name
#else
#define BUSTER_STRIDED_FUNCTION(name) checked_##name
#endif

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(count_u8)(void* output, const void* input, size_t count)
{
    (void)output;
    const uint8_t* source = input;
    uint64_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result += source[i] == 173;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(aos_sum)(void* output, const void* input, size_t count)
{
    (void)output;
    const StridedRecord* source = input;
    uint64_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result += (uint64_t)((int64_t)source[i].a * (int64_t)source[i].c);
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(aos_order)(void* output, const void* input, size_t count)
{
    (void)output;
    const StridedRecord* source = input;
    uint32_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        const uint32_t last = (uint32_t)source[i].d;
        const uint32_t first = (uint32_t)source[i].a;
        const uint32_t second = (uint32_t)source[i].b;
        result += (last + first) ^ second;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(aos_partial)(void* output, const void* input, size_t count)
{
    (void)output;
    const StridedRecord* source = input;
    uint32_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        const uint32_t third = (uint32_t)source[i].c;
        const uint32_t first = (uint32_t)source[i].a;
        result ^= third + first;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(rgb_gray)(void* output, const void* input, size_t count)
{
    uint8_t* destination = output;
    const uint8_t* source = input;
    const int n = (int)count;
    for (int i = 0; i < n; i += 1)
    {
        destination[i] = (uint8_t)((77u * source[3 * i] + 150u * source[3 * i + 1] + 29u * source[3 * i + 2]) >> 8);
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(stride9_i32)(void* output, const void* input, size_t count)
{
    (void)output;
    const Strided32* source = input;
    uint32_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result ^= source[(long)i * 9].value;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(stride17_i64)(void* output, const void* input, size_t count)
{
    (void)output;
    const Strided64* source = input;
    uint64_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result ^= source[(long)i * 17].value;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(reverse_i32)(void* output, const void* input, size_t count)
{
    (void)output;
    const Strided32* source = input;
    uint32_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result ^= source[(long)i * -1].value;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(reverse17_i64)(void* output, const void* input, size_t count)
{
    (void)output;
    const Strided64* source = input;
    uint64_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        result ^= source[(long)i * -17].value;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(reverse3_i16)(void* output, const void* input, size_t count)
{
    Strided16* destination = output;
    const Strided16* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 1].value = source[(long)i * -3].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(reverse9_i32)(void* output, const void* input, size_t count)
{
    Strided32* destination = output;
    const Strided32* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 1].value = source[(long)i * -9].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store3_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 3].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store_reverse3_i16)(void* output, const void* input, size_t count)
{
    Strided16* destination = output;
    const Strided16* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -3].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store9_i32)(void* output, const void* input, size_t count)
{
    Strided32* destination = output;
    const Strided32* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 9].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store_reverse17_i64)(void* output, const void* input, size_t count)
{
    Strided64* destination = output;
    const Strided64* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -17].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(opposite_i32)(void* output, const void* input, size_t count)
{
    Strided32* destination = output;
    const Strided32* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -9].value = source[(long)i * 9].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(copy17_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 1].value = source[(long)i * 17].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store9_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 9].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store17_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 17].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(opposite_i16)(void* output, const void* input, size_t count)
{
    Strided16* destination = output;
    const Strided16* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -9].value = source[(long)i * 17].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(copy9_f32)(void* output, const void* input, size_t count)
{
    StridedFloat* destination = output;
    const StridedFloat* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 1].value = source[(long)i * 9].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store_reverse17_f64)(void* output, const void* input, size_t count)
{
    StridedDouble* destination = output;
    const StridedDouble* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -17].value = source[(long)i * 1].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(store_reload_i32)(void* output, const void* input, size_t count)
{
    (void)input;
    Strided32* destination = output;
    uint32_t result = 0;
    for (size_t i = 0; i < count; i += 1)
    {
        const uint32_t value = destination[i * 2].value;
        destination[i * 2].value = value + 1;
        result ^= destination[i * 2].value * 3;
    }
    return result;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(fields_after_store_i32)(void* output, const void* input, size_t count)
{
    Strided32* destination = output;
    const Strided32* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[i].value = source[i * 4 + 3].value;
        destination[i].value += source[i * 4].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(page_stride_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 8192].value = source[(long)i * 8192].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(page_reverse_u8)(void* output, const void* input, size_t count)
{
    Strided8* destination = output;
    const Strided8* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * -8192].value = source[(long)i * -8192].value;
    }
    return 0;
}

// More than INT32_MAX elements per step forces qword indices, even when the hardware's scale is the element's size.
__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(huge_i32)(void* output, const void* input, size_t count)
{
    Strided32* destination = output;
    const Strided32* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[-(long)i * 2147483649L].value = source[(long)i * 2147483649L].value;
    }
    return 0;
}

__attribute__((noinline)) uint64_t BUSTER_STRIDED_FUNCTION(huge_i64)(void* output, const void* input, size_t count)
{
    Strided64* destination = output;
    const Strided64* source = input;
    for (size_t i = 0; i < count; i += 1)
    {
        destination[(long)i * 2147483649L].value = source[-(long)i * 2147483649L].value;
    }
    return 0;
}

#else

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUSTER_STRIDED_BYTES 131072u
#define BUSTER_STRIDED_PAGE_STEP 8192u
#define BUSTER_STRIDED_PAGE_COUNT 65u

typedef uint64_t StridedKernel(void* output, const void* input, size_t count);

extern StridedKernel checked_count_u8;
extern StridedKernel reference_count_u8;
extern StridedKernel checked_aos_sum;
extern StridedKernel reference_aos_sum;
extern StridedKernel checked_aos_order;
extern StridedKernel reference_aos_order;
extern StridedKernel checked_aos_partial;
extern StridedKernel reference_aos_partial;
extern StridedKernel checked_rgb_gray;
extern StridedKernel reference_rgb_gray;
extern StridedKernel checked_stride9_i32;
extern StridedKernel reference_stride9_i32;
extern StridedKernel checked_stride17_i64;
extern StridedKernel reference_stride17_i64;
extern StridedKernel checked_reverse_i32;
extern StridedKernel reference_reverse_i32;
extern StridedKernel checked_reverse17_i64;
extern StridedKernel reference_reverse17_i64;
extern StridedKernel checked_reverse3_i16;
extern StridedKernel reference_reverse3_i16;
extern StridedKernel checked_reverse9_i32;
extern StridedKernel reference_reverse9_i32;
extern StridedKernel checked_store3_u8;
extern StridedKernel reference_store3_u8;
extern StridedKernel checked_store_reverse3_i16;
extern StridedKernel reference_store_reverse3_i16;
extern StridedKernel checked_store9_i32;
extern StridedKernel reference_store9_i32;
extern StridedKernel checked_store_reverse17_i64;
extern StridedKernel reference_store_reverse17_i64;
extern StridedKernel checked_opposite_i32;
extern StridedKernel reference_opposite_i32;
extern StridedKernel checked_copy17_u8;
extern StridedKernel reference_copy17_u8;
extern StridedKernel checked_store9_u8;
extern StridedKernel reference_store9_u8;
extern StridedKernel checked_store17_u8;
extern StridedKernel reference_store17_u8;
extern StridedKernel checked_opposite_i16;
extern StridedKernel reference_opposite_i16;
extern StridedKernel checked_copy9_f32;
extern StridedKernel reference_copy9_f32;
extern StridedKernel checked_store_reverse17_f64;
extern StridedKernel reference_store_reverse17_f64;
extern StridedKernel checked_store_reload_i32;
extern StridedKernel reference_store_reload_i32;
extern StridedKernel checked_fields_after_store_i32;
extern StridedKernel reference_fields_after_store_i32;
extern StridedKernel checked_page_stride_u8;
extern StridedKernel reference_page_stride_u8;
extern StridedKernel checked_page_reverse_u8;
extern StridedKernel reference_page_reverse_u8;
extern StridedKernel checked_huge_i32;
extern StridedKernel reference_huge_i32;
extern StridedKernel checked_huge_i64;
extern StridedKernel reference_huge_i64;

typedef struct StridedCase StridedCase;
struct StridedCase
{
    const char* name;
    StridedKernel* checked;
    StridedKernel* reference;
    int32_t source_step;
    int32_t destination_step;
    uint32_t source_size;
    uint32_t destination_size;
};

static const StridedCase strided_cases[] = {
    {
        .name = "count_u8",
        .checked = checked_count_u8,
        .reference = reference_count_u8,
        .source_step = 1,
        .destination_step = 0,
        .source_size = 1,
        .destination_size = 0,
    },
    {
        .name = "aos_sum",
        .checked = checked_aos_sum,
        .reference = reference_aos_sum,
        .source_step = 16,
        .destination_step = 0,
        .source_size = 12,
        .destination_size = 0,
    },
    {
        .name = "aos_order",
        .checked = checked_aos_order,
        .reference = reference_aos_order,
        .source_step = 16,
        .destination_step = 0,
        .source_size = 16,
        .destination_size = 0,
    },
    {
        .name = "aos_partial",
        .checked = checked_aos_partial,
        .reference = reference_aos_partial,
        .source_step = 16,
        .destination_step = 0,
        .source_size = 12,
        .destination_size = 0,
    },
    {
        .name = "rgb_gray",
        .checked = checked_rgb_gray,
        .reference = reference_rgb_gray,
        .source_step = 3,
        .destination_step = 1,
        .source_size = 3,
        .destination_size = 1,
    },
    {
        .name = "stride9_i32",
        .checked = checked_stride9_i32,
        .reference = reference_stride9_i32,
        .source_step = 36,
        .destination_step = 0,
        .source_size = 4,
        .destination_size = 0,
    },
    {
        .name = "stride17_i64",
        .checked = checked_stride17_i64,
        .reference = reference_stride17_i64,
        .source_step = 136,
        .destination_step = 0,
        .source_size = 8,
        .destination_size = 0,
    },
    {
        .name = "reverse_i32",
        .checked = checked_reverse_i32,
        .reference = reference_reverse_i32,
        .source_step = -4,
        .destination_step = 0,
        .source_size = 4,
        .destination_size = 0,
    },
    {
        .name = "reverse17_i64",
        .checked = checked_reverse17_i64,
        .reference = reference_reverse17_i64,
        .source_step = -136,
        .destination_step = 0,
        .source_size = 8,
        .destination_size = 0,
    },
    {
        .name = "reverse3_i16",
        .checked = checked_reverse3_i16,
        .reference = reference_reverse3_i16,
        .source_step = -6,
        .destination_step = 2,
        .source_size = 2,
        .destination_size = 2,
    },
    {
        .name = "reverse9_i32",
        .checked = checked_reverse9_i32,
        .reference = reference_reverse9_i32,
        .source_step = -36,
        .destination_step = 4,
        .source_size = 4,
        .destination_size = 4,
    },
    {
        .name = "store3_u8",
        .checked = checked_store3_u8,
        .reference = reference_store3_u8,
        .source_step = 1,
        .destination_step = 3,
        .source_size = 1,
        .destination_size = 1,
    },
    {
        .name = "store_reverse3_i16",
        .checked = checked_store_reverse3_i16,
        .reference = reference_store_reverse3_i16,
        .source_step = 2,
        .destination_step = -6,
        .source_size = 2,
        .destination_size = 2,
    },
    {
        .name = "store9_i32",
        .checked = checked_store9_i32,
        .reference = reference_store9_i32,
        .source_step = 4,
        .destination_step = 36,
        .source_size = 4,
        .destination_size = 4,
    },
    {
        .name = "store_reverse17_i64",
        .checked = checked_store_reverse17_i64,
        .reference = reference_store_reverse17_i64,
        .source_step = 8,
        .destination_step = -136,
        .source_size = 8,
        .destination_size = 8,
    },
    {
        .name = "opposite_i32",
        .checked = checked_opposite_i32,
        .reference = reference_opposite_i32,
        .source_step = 36,
        .destination_step = -36,
        .source_size = 4,
        .destination_size = 4,
    },
    {
        .name = "copy17_u8",
        .checked = checked_copy17_u8,
        .reference = reference_copy17_u8,
        .source_step = 17,
        .destination_step = 1,
        .source_size = 1,
        .destination_size = 1,
    },
    {
        .name = "store9_u8",
        .checked = checked_store9_u8,
        .reference = reference_store9_u8,
        .source_step = 1,
        .destination_step = 9,
        .source_size = 1,
        .destination_size = 1,
    },
    {
        .name = "store17_u8",
        .checked = checked_store17_u8,
        .reference = reference_store17_u8,
        .source_step = 1,
        .destination_step = 17,
        .source_size = 1,
        .destination_size = 1,
    },
    {
        .name = "opposite_i16",
        .checked = checked_opposite_i16,
        .reference = reference_opposite_i16,
        .source_step = 34,
        .destination_step = -18,
        .source_size = 2,
        .destination_size = 2,
    },
    {
        .name = "copy9_f32",
        .checked = checked_copy9_f32,
        .reference = reference_copy9_f32,
        .source_step = 36,
        .destination_step = 4,
        .source_size = 4,
        .destination_size = 4,
    },
    {
        .name = "store_reverse17_f64",
        .checked = checked_store_reverse17_f64,
        .reference = reference_store_reverse17_f64,
        .source_step = 8,
        .destination_step = -136,
        .source_size = 8,
        .destination_size = 8,
    },
    {
        .name = "store_reload_i32",
        .checked = checked_store_reload_i32,
        .reference = reference_store_reload_i32,
        .source_step = 0,
        .destination_step = 8,
        .source_size = 0,
        .destination_size = 4,
    },
    {
        .name = "fields_after_store_i32",
        .checked = checked_fields_after_store_i32,
        .reference = reference_fields_after_store_i32,
        .source_step = 16,
        .destination_step = 4,
        .source_size = 16,
        .destination_size = 4,
    },
};

typedef struct StridedBuffer StridedBuffer;
struct StridedBuffer
{
    uint8_t* mapping;
    uint8_t* data;
    size_t bytes;
    size_t mapping_bytes;
};

static bool strided_buffer(StridedBuffer* buffer, size_t bytes, size_t page)
{
    buffer->bytes = bytes;
    buffer->mapping_bytes = bytes + 2 * page;
    buffer->mapping = mmap(NULL, buffer->mapping_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = buffer->mapping != MAP_FAILED;
    if (valid)
    {
        buffer->data = buffer->mapping + page;
        valid = mprotect(buffer->data, bytes, PROT_READ | PROT_WRITE) == 0;
    }
    return valid;
}

static void strided_release(StridedBuffer* buffer)
{
    if (buffer->mapping != NULL && buffer->mapping != MAP_FAILED)
    {
        munmap(buffer->mapping, buffer->mapping_bytes);
    }
}

static uint64_t strided_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static size_t strided_edge(int32_t step, uint32_t size, size_t count, size_t bytes, bool high)
{
    const size_t distance = count == 0 ? 0 : (count - 1) * (size_t)(step < 0 ? -step : step);
    const size_t place = high ? bytes - size - (step > 0 ? distance : 0) : step < 0 ? distance : 0;
    return place;
}

static bool strided_trial(const StridedCase* test, StridedBuffer* checked, StridedBuffer* reference, const uint8_t* initial, size_t count, size_t source, size_t destination)
{
    memcpy(checked->data, initial, checked->bytes);
    memcpy(reference->data, initial, reference->bytes);
    // Deliberately copied IEEE payloads: both zeros, infinities, quiet/signaling NaNs and the smallest subnormal.
    static const uint32_t float_bits[] = { 0, UINT32_C(0x80000000), UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fc12345), UINT32_C(0x7f812345), 1, UINT32_C(0x3f800000) };
    static const uint64_t double_bits[] = { 0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8123456789abc), UINT64_C(0x7ff0123456789abc), 1, UINT64_C(0x3ff0000000000000) };
    const bool floats = strcmp(test->name, "copy9_f32") == 0;
    const bool doubles = strcmp(test->name, "store_reverse17_f64") == 0;
    for (size_t i = 0; (floats || doubles) && i < count; i += 1)
    {
        const size_t at = (size_t)((int64_t)source + (int64_t)i * test->source_step);
        const void* bits = floats ? (const void*)&float_bits[(i + count) % 8] : (const void*)&double_bits[(i + count) % 8];
        const size_t size = floats ? sizeof(float_bits[0]) : sizeof(double_bits[0]);
        memcpy(checked->data + at, bits, size);
        memcpy(reference->data + at, bits, size);
    }
    const uint64_t expected = test->reference(reference->data + destination, reference->data + source, count);
    const uint64_t actual = test->checked(checked->data + destination, checked->data + source, count);
    const bool agree = expected == actual && memcmp(reference->data, checked->data, checked->bytes) == 0;
    if (!agree)
    {
        fprintf(stderr, "strided-native: %s n=%zu source=%zu destination=%zu: result %llu expected %llu or memory differs\n",
                test->name, count, source, destination, (unsigned long long)actual, (unsigned long long)expected);
    }
    return agree;
}

// A source/destination pair of pages per lane, every other page inaccessible. Each selected byte is the readable
// page's last, so a masked operation has only one active byte before PROT_NONE. The two halves are disjoint.
static uint32_t strided_sparse_pages(const char* filter, size_t page, uint64_t* checks)
{
    uint32_t failures = 0;
    const size_t half = BUSTER_STRIDED_PAGE_STEP * BUSTER_STRIDED_PAGE_COUNT;
    const size_t bytes = 2 * half;
    StridedBuffer checked = { 0 };
    StridedBuffer reference = { 0 };
    bool ready = page == BUSTER_STRIDED_PAGE_STEP / 2 && strided_buffer(&checked, bytes, page) && strided_buffer(&reference, bytes, page);
    if (ready)
    {
        for (size_t at = page; ready && at < bytes; at += BUSTER_STRIDED_PAGE_STEP)
        {
            ready = mprotect(checked.data + at, page, PROT_NONE) == 0 && mprotect(reference.data + at, page, PROT_NONE) == 0;
        }
    }
    if (!ready)
    {
        fprintf(stderr, "strided-native: cannot prepare sparse guard pages\n");
        failures += 1;
    }
    static const size_t lengths[] = { 0, 1, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65 };
    for (uint32_t reverse = 0; ready && reverse < 2; reverse += 1)
    {
        const char* name = reverse == 0 ? "page_stride_u8" : "page_reverse_u8";
        const bool selected = filter == NULL || strcmp(filter, name) == 0;
        for (uint32_t n = 0; selected && n < sizeof(lengths) / sizeof(lengths[0]); n += 1)
        {
            const size_t count = lengths[n];
            for (size_t at = 0; at < bytes; at += BUSTER_STRIDED_PAGE_STEP)
            {
                memset(checked.data + at, (int)(17 + at / BUSTER_STRIDED_PAGE_STEP), page);
                memset(reference.data + at, (int)(17 + at / BUSTER_STRIDED_PAGE_STEP), page);
            }
            const size_t last = reverse != 0 && count != 0 ? (count - 1) * BUSTER_STRIDED_PAGE_STEP : 0;
            const size_t source = last + page - 1;
            const size_t destination = half + last + page - 1;
            uint64_t expected;
            uint64_t actual;
            if (reverse == 0)
            {
                expected = reference_page_stride_u8(reference.data + destination, reference.data + source, count);
                actual = checked_page_stride_u8(checked.data + destination, checked.data + source, count);
            }
            else
            {
                expected = reference_page_reverse_u8(reference.data + destination, reference.data + source, count);
                actual = checked_page_reverse_u8(checked.data + destination, checked.data + source, count);
            }
            bool agree = expected == actual;
            for (size_t at = 0; agree && at < bytes; at += BUSTER_STRIDED_PAGE_STEP)
            {
                agree = memcmp(reference.data + at, checked.data + at, page) == 0;
            }
            *checks += 1;
            if (!agree)
            {
                fprintf(stderr, "strided-native: %s n=%zu: result or selected pages differ\n", name, count);
                failures += 1;
            }
        }
    }
    strided_release(&checked);
    strided_release(&reference);
    return failures;
}

// Large virtual holes force native qword-index gather/scatter forms without allocating the intervening memory.
// Only 17 input and 17 output pages are writable in each copy. Both traversal directions and both lane widths run.
static uint32_t strided_huge_indices(const char* filter, size_t page, uint64_t* checks)
{
    static const size_t lengths[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17 };
    const size_t count_max = 17;
    uint32_t failures = 0;
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        const char* name = kind == 0 ? "huge_i32" : "huge_i64";
        const bool selected = filter == NULL || strcmp(filter, name) == 0;
        if (selected)
        {
            const size_t element = kind == 0 ? 4 : 8;
            const size_t step = UINT64_C(2147483649) * element;
            const size_t half = ((count_max - 1) * step + 2 * page - 1) & ~(page - 1);
            StridedBuffer buffers[2] = { 0 };
            bool ready = true;
            for (uint32_t copy = 0; ready && copy < 2; copy += 1)
            {
                StridedBuffer* buffer = &buffers[copy];
                buffer->mapping_bytes = 2 * half + 2 * page;
                buffer->mapping = mmap(NULL, buffer->mapping_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                ready = buffer->mapping != MAP_FAILED;
                if (ready)
                {
                    buffer->data = buffer->mapping + page;
                    for (size_t i = 0; ready && i < count_max; i += 1)
                    {
                        const size_t at = (page - element + i * step) & ~(page - 1);
                        ready = mprotect(buffer->data + at, page, PROT_READ | PROT_WRITE) == 0 && mprotect(buffer->data + half + at, page, PROT_READ | PROT_WRITE) == 0;
                    }
                }
            }
            if (!ready)
            {
                fprintf(stderr, "strided-native: cannot reserve qword-index pages for %s\n", name);
                failures += 1;
            }
            for (uint32_t n = 0; ready && n < sizeof(lengths) / sizeof(lengths[0]); n += 1)
            {
                const size_t count = lengths[n];
                for (uint32_t copy = 0; copy < 2; copy += 1)
                {
                    for (size_t i = 0; i < count_max; i += 1)
                    {
                        const size_t at = (page - element + i * step) & ~(page - 1);
                        memset(buffers[copy].data + at, (int)(17 + i), page);
                        memset(buffers[copy].data + half + at, (int)(113 + i), page);
                    }
                }
                const size_t last = count == 0 ? 0 : (count - 1) * step;
                const size_t source = page - element + (kind == 0 ? 0 : last);
                const size_t destination = half + page - element + (kind == 0 ? last : 0);
                uint64_t expected;
                uint64_t actual;
                if (kind == 0)
                {
                    expected = reference_huge_i32(buffers[0].data + destination, buffers[0].data + source, count);
                    actual = checked_huge_i32(buffers[1].data + destination, buffers[1].data + source, count);
                }
                else
                {
                    expected = reference_huge_i64(buffers[0].data + destination, buffers[0].data + source, count);
                    actual = checked_huge_i64(buffers[1].data + destination, buffers[1].data + source, count);
                }
                bool agree = expected == actual;
                for (size_t i = 0; agree && i < count_max; i += 1)
                {
                    const size_t at = (page - element + i * step) & ~(page - 1);
                    agree = memcmp(buffers[0].data + at, buffers[1].data + at, page) == 0 && memcmp(buffers[0].data + half + at, buffers[1].data + half + at, page) == 0;
                }
                *checks += 1;
                if (!agree)
                {
                    fprintf(stderr, "strided-native: %s n=%zu: result or selected pages differ\n", name, count);
                    failures += 1;
                }
            }
            strided_release(&buffers[0]);
            strided_release(&buffers[1]);
        }
    }
    return failures;
}

int main(int argc, char** argv)
{
    const char* filter = argc == 2 ? argv[1] : NULL;
    const long queried_page = sysconf(_SC_PAGESIZE);
    const size_t page = queried_page > 0 ? (size_t)queried_page : 4096;
    StridedBuffer checked = { 0 };
    StridedBuffer reference = { 0 };
    uint8_t* initial = malloc(BUSTER_STRIDED_BYTES);
    const bool ready = argc <= 2 && initial != NULL && strided_buffer(&checked, BUSTER_STRIDED_BYTES, page) && strided_buffer(&reference, BUSTER_STRIDED_BYTES, page);
    uint32_t failures = ready ? 0 : 1;
    uint64_t checks = 0;
    uint64_t random = UINT64_C(0x2808a19b5d373521);
    static const size_t lengths[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 129, 255, 256, 257, 300 };
    static const int32_t deltas[] = { -256, -136, -128, -72, -64, -36, -17, -16, -9, -8, -4, -2, -1, 0, 1, 2, 4, 8, 9, 16, 17, 36, 64, 72, 128, 136, 256 };
    static const uint32_t offsets[] = { 0, 1, 3, 7 };
    for (uint32_t k = 0; ready && failures < 8 && k < sizeof(strided_cases) / sizeof(strided_cases[0]); k += 1)
    {
        const StridedCase* test = &strided_cases[k];
        const bool selected = filter == NULL || strcmp(filter, test->name) == 0;
        for (uint32_t n = 0; selected && failures < 8 && n < sizeof(lengths) / sizeof(lengths[0]); n += 1)
        {
            for (uint32_t at = 0; at < BUSTER_STRIDED_BYTES; at += 8)
            {
                const uint64_t word = strided_random(&random);
                memcpy(initial + at, &word, sizeof(word));
            }
            const size_t count = lengths[n];
            // The selected source and destination ranges touch either guard, both disjoint arrangements included.
            for (uint32_t edge = 0; failures < 8 && edge < 4; edge += 1)
            {
                const size_t source = strided_edge(test->source_step, test->source_size, count, checked.bytes, (edge & 1) != 0);
                const size_t destination = strided_edge(test->destination_step, test->destination_size, count, checked.bytes, (edge & 2) != 0);
                failures += !strided_trial(test, &checked, &reference, initial, count, source, destination);
                checks += 1;
            }
            for (uint32_t alignment = 0; failures < 8 && alignment < sizeof(offsets) / sizeof(offsets[0]); alignment += 1)
            {
                const size_t source = BUSTER_STRIDED_BYTES / 2 + offsets[alignment];
                for (uint32_t delta = 0; failures < 8 && delta < sizeof(deltas) / sizeof(deltas[0]); delta += 1)
                {
                    const size_t destination = (size_t)((int64_t)source + deltas[delta]);
                    failures += !strided_trial(test, &checked, &reference, initial, count, source, destination);
                    checks += 1;
                }
            }
        }
    }
    if (ready && failures == 0)
    {
        failures += strided_sparse_pages(filter, page, &checks);
        failures += strided_huge_indices(filter, page, &checks);
    }
    if (checks == 0)
    {
        fprintf(stderr, "strided-native: setup failed or no kernel matched\n");
        failures += 1;
    }
    printf("strided-native: %llu checks, %u failures\n", (unsigned long long)checks, failures);
    strided_release(&checked);
    strided_release(&reference);
    free(initial);
    return failures == 0 ? 0 : 1;
}

#endif
