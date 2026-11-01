// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Standalone protocol tests, not a claim of machine-code/CFG verification or driver migration. See
// docs/emission-metadata.md for the baseline, AVX-512 and sanitizer commands. No compiler tables or fake backend
// types are required. Guarded slices have exactly their stated logical lengths, including every SIMD tail.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../../src/buster/compiler/emission_metadata.c"

static uint64_t test_checks;

static void test_expect(bool condition, const char* expression, uint32_t line)
{
    test_checks += 1;
    if (!condition)
    {
        fprintf(stderr, "emission metadata: line %u: %s\n", line, expression);
        abort();
    }
}

#define BUSTER_EMISSION_TEST(expression) test_expect((expression), #expression, __LINE__)

static bool test_same_check(EmissionCheck a, EmissionCheck b)
{
    return a.error == b.error && a.part == b.part && a.index == b.index;
}

static EmissionSourceEvent test_origin(uint32_t boundary, uint32_t side, uint32_t origin)
{
    const EmissionSourceEvent event = {
        .at = {
            .boundary = boundary,
            .side = side,
        },
        .origin = origin,
        .kind = origin == BUSTER_EMISSION_NO_ORIGIN ? EMISSION_SOURCE_GENERATED : EMISSION_SOURCE_ORIGIN,
    };
    return event;
}

static EmissionFrameEvent test_frame(uint32_t boundary, uint32_t side, uint8_t kind, uint16_t reg, int32_t value)
{
    const EmissionFrameEvent event = {
        .at = {
            .boundary = boundary,
            .side = side,
        },
        .value = value,
        .reg = reg,
        .kind = kind,
    };
    return event;
}

static void test_empty(void)
{
    const EmissionMetadata metadata = { 0 };
    const EmissionLayout layout = { 0 };
    const EmissionStorage storage = { 0 };
    EmissionMetadataPlan plan = {
        .source_count = 7,
        .frame_count = 9,
    };
    EmissionResolved resolved = {
        .source_count = 7,
        .frame_count = 9,
        .size = 11,
    };
    BUSTER_EMISSION_TEST(emission_metadata_plan(&metadata, &layout, false, &plan).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(plan.source_count == 0 && plan.frame_count == 0);
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, false, &resolved).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(resolved.sources == NULL && resolved.frames == NULL && resolved.source_count == 0 && resolved.frame_count == 0 && resolved.size == 0);
    BUSTER_EMISSION_TEST(emission_metadata_check(NULL, &layout, false).error == EMISSION_INVALID);
    BUSTER_EMISSION_TEST(emission_metadata_check(&metadata, NULL, false).error == EMISSION_INVALID);
    BUSTER_EMISSION_TEST(emission_metadata_plan(&metadata, &layout, false, NULL).error == EMISSION_INVALID);
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, NULL, false, &resolved).error == EMISSION_INVALID);
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, false, NULL).error == EMISSION_INVALID);
    const EmissionLayout padding = {
        .size = 32,
    };
    const EmissionSourceEvent sources[] = {
        {
            .at = {
                .boundary = 0,
                .side = EMISSION_LEFT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
        {
            .at = {
                .boundary = 0,
                .side = EMISSION_RIGHT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
    };
    const EmissionMetadata generated = {
        .sources = sources,
        .source_count = 2,
    };
    EmissionSourceRow rows[2];
    const EmissionStorage destination = {
        .sources = rows,
        .source_capacity = 2,
    };
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&generated, &padding, &destination, false, &resolved).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(rows[0].offset == 0 && rows[1].offset == 32);
}

static void test_layouts(void)
{
    uint32_t starts[] = { 2, 3, 8, 8, 12 };
    uint32_t ends[] = { 3, 8, 8, 10, 13 };
    const EmissionLayout layout = {
        .starts = starts,
        .ends = ends,
        .count = 5,
        .size = 16,
    };
    const EmissionSourceEvent sources[] = {
        {
            .at = {
                .boundary = 0,
                .side = EMISSION_LEFT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
        {
            .at = {
                .boundary = 0,
                .side = EMISSION_RIGHT,
            },
            .origin = 0,
            .kind = EMISSION_SOURCE_ORIGIN,
        },
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_LEFT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_RIGHT,
            },
            .origin = 1,
            .kind = EMISSION_SOURCE_ORIGIN,
        },
        {
            .at = {
                .boundary = 2,
                .side = EMISSION_RIGHT,
            },
            .origin = 2,
            .kind = EMISSION_SOURCE_ORIGIN,
        },
        {
            .at = {
                .boundary = 3,
                .side = EMISSION_RIGHT,
            },
            .origin = 3,
            .kind = EMISSION_SOURCE_ORIGIN,
        },
        {
            .at = {
                .boundary = 4,
                .side = EMISSION_LEFT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
        {
            .at = {
                .boundary = 4,
                .side = EMISSION_RIGHT,
            },
            .origin = 4,
            .kind = EMISSION_SOURCE_ORIGIN,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_LEFT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_RIGHT,
            },
            .origin = BUSTER_EMISSION_NO_ORIGIN,
            .kind = EMISSION_SOURCE_GENERATED,
        },
    };
    const EmissionFrameEvent frames[] = {
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_LEFT,
            },
            .value = 16,
            .reg = 7,
            .kind = EMISSION_FRAME_CFA,
        },
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_LEFT,
            },
            .value = -16,
            .reg = 6,
            .kind = EMISSION_FRAME_OFFSET,
        },
        {
            .at = {
                .boundary = 2,
                .side = EMISSION_LEFT,
            },
            .kind = EMISSION_FRAME_REMEMBER_STATE,
        },
        {
            .at = {
                .boundary = 2,
                .side = EMISSION_LEFT,
            },
            .value = 8,
            .kind = EMISSION_FRAME_CFA_OFFSET,
        },
        {
            .at = {
                .boundary = 3,
                .side = EMISSION_LEFT,
            },
            .reg = 6,
            .kind = EMISSION_FRAME_RESTORE,
        },
        {
            .at = {
                .boundary = 3,
                .side = EMISSION_LEFT,
            },
            .kind = EMISSION_FRAME_RESTORE_STATE,
        },
        {
            .at = {
                .boundary = 4,
                .side = EMISSION_LEFT,
            },
            .value = 16,
            .reg = 6,
            .kind = EMISSION_FRAME_CFA,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_LEFT,
            },
            .value = 7,
            .reg = 12,
            .kind = EMISSION_FRAME_REGISTER,
        },
    };
    const uint32_t expected_sources[] = { 0, 2, 3, 3, 8, 8, 10, 12, 13, 16 };
    const uint32_t expected_frames[] = { 3, 3, 8, 8, 8, 8, 10, 13 };
    const EmissionMetadata metadata = {
        .sources = sources,
        .frames = frames,
        .source_count = 10,
        .frame_count = 8,
        .origin_count = 5,
        .register_count = 17,
    };
    EmissionSourceRow source_rows[10];
    EmissionFrameRow frame_rows[8];
    const EmissionStorage storage = {
        .sources = source_rows,
        .frames = frame_rows,
        .source_capacity = 10,
        .frame_capacity = 8,
    };
    EmissionResolved result;
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, scalar != 0, &result).error == EMISSION_OK);
        BUSTER_EMISSION_TEST(result.source_count == 10 && result.frame_count == 8 && result.size == 16);
        for (uint32_t i = 0; i < 10; i += 1)
        {
            BUSTER_EMISSION_TEST(source_rows[i].offset == expected_sources[i]);
            BUSTER_EMISSION_TEST(source_rows[i].origin == sources[i].origin && source_rows[i].kind == sources[i].kind);
        }
        for (uint32_t i = 0; i < 8; i += 1)
        {
            BUSTER_EMISSION_TEST(frame_rows[i].offset == expected_frames[i]);
            BUSTER_EMISSION_TEST(frame_rows[i].kind == frames[i].kind && frame_rows[i].reg == frames[i].reg && frame_rows[i].value == frames[i].value && frame_rows[i].reserved == 0);
        }
    }
    // Relax record 1 from five bytes to two. The same producer anchors resolve through the new map; zero-width
    // records and the gap before record 4 remain explicit. No producer event is rewritten to a byte offset.
    ends[1] = 5;
    starts[2] = 5;
    ends[2] = 5;
    starts[3] = 5;
    ends[3] = 7;
    starts[4] = 9;
    ends[4] = 10;
    const EmissionLayout relaxed = {
        .starts = starts,
        .ends = ends,
        .count = 5,
        .size = 13,
    };
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &relaxed, &storage, false, &result).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(source_rows[4].offset == 5 && source_rows[5].offset == 5);
    BUSTER_EMISSION_TEST(frame_rows[6].offset == 7 && source_rows[7].offset == 9);
    BUSTER_EMISSION_TEST(source_rows[8].offset == 10 && source_rows[9].offset == 13);
}

static void test_failure(const EmissionMetadata* metadata, const EmissionLayout* layout, EmissionError error, EmissionPart part, uint32_t index)
{
    EmissionSourceRow sources[4];
    EmissionFrameRow frames[4];
    uint8_t before_sources[sizeof(sources)];
    uint8_t before_frames[sizeof(frames)];
    memset(sources, 0xa5, sizeof(sources));
    memset(frames, 0x5a, sizeof(frames));
    memcpy(before_sources, sources, sizeof(sources));
    memcpy(before_frames, frames, sizeof(frames));
    const EmissionStorage storage = {
        .sources = sources,
        .frames = frames,
        .source_capacity = 4,
        .frame_capacity = 4,
    };
    const EmissionResolved unchanged = {
        .sources = sources,
        .frames = frames,
        .source_count = 31,
        .frame_count = 37,
        .size = 41,
    };
    const EmissionMetadataPlan unplanned = {
        .source_count = 43,
        .frame_count = 47,
    };
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        EmissionResolved result = unchanged;
        EmissionMetadataPlan plan = unplanned;
        const EmissionCheck check = emission_metadata_resolve(metadata, layout, &storage, scalar != 0, &result);
        BUSTER_EMISSION_TEST(check.error == error && check.part == part && check.index == index);
        BUSTER_EMISSION_TEST(memcmp(sources, before_sources, sizeof(sources)) == 0 && memcmp(frames, before_frames, sizeof(frames)) == 0);
        BUSTER_EMISSION_TEST(result.sources == unchanged.sources && result.frames == unchanged.frames && result.source_count == unchanged.source_count && result.frame_count == unchanged.frame_count && result.size == unchanged.size);
        BUSTER_EMISSION_TEST(test_same_check(check, emission_metadata_plan(metadata, layout, scalar != 0, &plan)));
        BUSTER_EMISSION_TEST(plan.source_count == unplanned.source_count && plan.frame_count == unplanned.frame_count);
    }
}

static void test_invalid(void)
{
    uint32_t starts[] = { 0, 2, 4 };
    uint32_t ends[] = { 2, 3, 5 };
    EmissionLayout layout = {
        .starts = starts,
        .ends = ends,
        .count = 3,
        .size = 6,
    };
    EmissionSourceEvent sources[] = { test_origin(0, EMISSION_LEFT, 0), test_origin(1, EMISSION_RIGHT, 1) };
    EmissionFrameEvent frames[] = { test_frame(1, EMISSION_LEFT, EMISSION_FRAME_CFA, 7, 16), test_frame(3, EMISSION_LEFT, EMISSION_FRAME_RESTORE, 6, 0) };
    EmissionMetadata metadata = {
        .sources = sources,
        .frames = frames,
        .source_count = 2,
        .frame_count = 2,
        .origin_count = 2,
        .register_count = 17,
    };
    BUSTER_EMISSION_TEST(emission_metadata_check(&metadata, &layout, false).error == EMISSION_OK);
    starts[1] = 1;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_LAYOUT, 1);
    starts[1] = 2;
    ends[2] = 3;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_LAYOUT, 2);
    ends[2] = 7;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_LAYOUT, 2);
    ends[2] = 5;
    layout.starts = NULL;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_LAYOUT, 0);
    layout.starts = starts;
    layout.ends = NULL;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_LAYOUT, 0);
    layout.ends = ends;
    metadata.sources = NULL;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_DESCRIPTOR, 0);
    metadata.sources = sources;
    metadata.frames = NULL;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_DESCRIPTOR, 0);
    metadata.frames = frames;
    metadata.register_count = 0;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_DESCRIPTOR, 0);
    metadata.register_count = BUSTER_EMISSION_MAX_REGISTERS + 1;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_DESCRIPTOR, 0);
    metadata.register_count = 17;
    sources[0].at.side = EMISSION_RIGHT;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 0);
    sources[0].at.side = EMISSION_LEFT;
    sources[1].at.boundary = 4;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1].at.boundary = 1;
    sources[1].at.side = 2;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1].at = sources[0].at;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1] = test_origin(1, EMISSION_RIGHT, 2);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1].origin = BUSTER_EMISSION_NO_ORIGIN;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1] = test_origin(1, EMISSION_RIGHT, 1);
    sources[1].kind = EMISSION_SOURCE_GENERATED;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1].kind = 2;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_SOURCE, 1);
    sources[1] = test_origin(1, EMISSION_RIGHT, 1);
    frames[1].at.boundary = 4;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1].at.boundary = 0;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1].at.boundary = 3;
    frames[1].reserved = 1;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1].reserved = 0;
    frames[1].kind = 255;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_RESTORE, 17, 0);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_SAME_VALUE, 6, 1);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_UNDEFINED, 6, 1);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_CFA_OFFSET, 7, 16);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_REGISTER, 6, -1);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1].value = 17;
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_RESTORE_STATE, 0, 0);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_REMEMBER_STATE, 0, 0);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 2);
    frames[0] = test_frame(1, EMISSION_LEFT, EMISSION_FRAME_REMEMBER_STATE, 1, 0);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 0);
    frames[0] = test_frame(1, EMISSION_LEFT, EMISSION_FRAME_REMEMBER_STATE, 0, 0);
    frames[1] = test_frame(3, EMISSION_LEFT, EMISSION_FRAME_RESTORE_STATE, 0, 1);
    test_failure(&metadata, &layout, EMISSION_INVALID, EMISSION_FRAME, 1);
}

static void test_capacity(void)
{
    const uint32_t starts[] = { 0 };
    const uint32_t ends[] = { 1 };
    const EmissionLayout layout = {
        .starts = starts,
        .ends = ends,
        .count = 1,
        .size = 1,
    };
    const EmissionSourceEvent source = test_origin(0, EMISSION_LEFT, 0);
    const EmissionFrameEvent frame = test_frame(1, EMISSION_LEFT, EMISSION_FRAME_CFA_OFFSET, 0, 8);
    const EmissionMetadata metadata = {
        .sources = &source,
        .frames = &frame,
        .source_count = 1,
        .frame_count = 1,
        .origin_count = 1,
        .register_count = 17,
    };
    for (uint32_t which = 0; which < 4; which += 1)
    {
        EmissionSourceRow row = {
            .offset = 77,
            .origin = 88,
            .kind = 99,
        };
        EmissionFrameRow rule = {
            .offset = 11,
            .value = 22,
            .reg = 33,
            .kind = 44,
            .reserved = 55,
        };
        const EmissionStorage storage = {
            .sources = which == 2 ? NULL : &row,
            .frames = which == 3 ? NULL : &rule,
            .source_capacity = which == 0 ? 0 : 1,
            .frame_capacity = which == 1 ? 0 : 1,
        };
        EmissionResolved result = {
            .size = 123,
        };
        const EmissionCheck check = emission_metadata_resolve(&metadata, &layout, &storage, false, &result);
        BUSTER_EMISSION_TEST(check.error == (which < 2 ? EMISSION_CAPACITY : EMISSION_INVALID) && check.part == EMISSION_DESTINATION);
        BUSTER_EMISSION_TEST(row.offset == 77 && row.origin == 88 && row.kind == 99);
        BUSTER_EMISSION_TEST(rule.offset == 11 && rule.value == 22 && rule.reg == 33 && rule.kind == 44 && rule.reserved == 55);
        BUSTER_EMISSION_TEST(result.size == 123 && result.source_count == 0 && result.frame_count == 0);
    }
}

typedef struct TestGuard TestGuard;
struct TestGuard
{
    uint8_t* mapping;
    uint8_t* slice;
    size_t mapping_size;
};

static TestGuard test_guard(size_t bytes)
{
    const long page = sysconf(_SC_PAGESIZE);
    BUSTER_EMISSION_TEST(page > 0 && bytes <= (size_t)page);
    const size_t size = 3 * (size_t)page;
    uint8_t* mapping = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    BUSTER_EMISSION_TEST(mapping != MAP_FAILED);
    BUSTER_EMISSION_TEST(mprotect(mapping + page, (size_t)page, PROT_READ | PROT_WRITE) == 0);
    const TestGuard guard = {
        .mapping = mapping,
        .slice = mapping + 2 * page - bytes,
        .mapping_size = size,
    };
    return guard;
}

static void test_guard_release(TestGuard guard)
{
    BUSTER_EMISSION_TEST(munmap(guard.mapping, guard.mapping_size) == 0);
}

static void test_guarded_tails(void)
{
    for (uint32_t count = 0; count <= 65; count += 1)
    {
        const TestGuard sg = test_guard((size_t)count * sizeof(uint32_t));
        const TestGuard eg = test_guard((size_t)count * sizeof(uint32_t));
        uint32_t* const starts = (uint32_t*)sg.slice;
        uint32_t* const ends = (uint32_t*)eg.slice;
        for (uint32_t i = 0; i < count; i += 1)
        {
            starts[i] = 4 * i;
            ends[i] = 4 * i + 2;
        }
        const EmissionLayout layout = {
            .starts = starts,
            .ends = ends,
            .count = count,
            .size = 4 * count,
        };
        BUSTER_EMISSION_TEST(emission_layout_check(&layout, false).error == EMISSION_OK);
        for (uint32_t i = 0; i < count; i += 1)
        {
            const uint32_t original = ends[i];
            ends[i] = UINT32_MAX;
            const EmissionCheck scalar = emission_layout_check(&layout, true);
            const EmissionCheck vector = emission_layout_check(&layout, false);
            BUSTER_EMISSION_TEST(test_same_check(scalar, vector) && scalar.error == EMISSION_INVALID && scalar.index == i);
            ends[i] = original;
        }
        const TestGuard source_input = test_guard(sizeof(EmissionSourceEvent));
        const TestGuard frame_input = test_guard(2 * sizeof(EmissionFrameEvent));
        const TestGuard source_output = test_guard(sizeof(EmissionSourceRow));
        const TestGuard frame_output = test_guard(2 * sizeof(EmissionFrameRow));
        EmissionSourceEvent* source = (EmissionSourceEvent*)source_input.slice;
        EmissionFrameEvent* frames = (EmissionFrameEvent*)frame_input.slice;
        *source = test_origin(0, EMISSION_LEFT, BUSTER_EMISSION_NO_ORIGIN);
        frames[0] = test_frame(count, EMISSION_LEFT, EMISSION_FRAME_CFA, 7, 8);
        frames[1] = test_frame(count, EMISSION_RIGHT, EMISSION_FRAME_SAME_VALUE, 6, 0);
        const EmissionMetadata metadata = {
            .sources = source,
            .frames = frames,
            .source_count = 1,
            .frame_count = 2,
            .register_count = 17,
        };
        const EmissionStorage storage = {
            .sources = (EmissionSourceRow*)source_output.slice,
            .frames = (EmissionFrameRow*)frame_output.slice,
            .source_capacity = 1,
            .frame_capacity = 2,
        };
        EmissionResolved result;
        BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, false, &result).error == EMISSION_OK);
        BUSTER_EMISSION_TEST(result.frames[0].offset == (count == 0 ? 0 : 4 * count - 2));
        BUSTER_EMISSION_TEST(result.frames[1].offset == layout.size);
        test_guard_release(source_input);
        test_guard_release(frame_input);
        test_guard_release(source_output);
        test_guard_release(frame_output);
        test_guard_release(sg);
        test_guard_release(eg);
    }
}

static uint32_t test_random(uint32_t* state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void test_properties(void)
{
    uint32_t seed = 1056;
    for (uint32_t run = 0; run < 4096; run += 1)
    {
        uint32_t starts[97];
        uint32_t ends[97];
        EmissionSourceEvent sources[196];
        EmissionFrameEvent frames[196];
        EmissionSourceRow source_rows[2][196];
        EmissionFrameRow frame_rows[2][196];
        const uint32_t count = test_random(&seed) % 98;
        uint32_t size = run % 2 != 0 ? UINT32_MAX - 10000 : 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            size += test_random(&seed) % 8;
            starts[i] = size;
            size += test_random(&seed) % 16;
            ends[i] = size;
        }
        size += test_random(&seed) % 8;
        uint32_t events = 0;
        for (uint32_t boundary = 0; boundary <= count; boundary += 1)
        {
            for (uint32_t side = 0; side < 2; side += 1)
            {
                const uint32_t origin = test_random(&seed) % 9;
                sources[events] = test_origin(boundary, side, origin < 8 ? origin : BUSTER_EMISSION_NO_ORIGIN);
                frames[events] = test_frame(boundary, side, EMISSION_FRAME_CFA_OFFSET, 0, (int32_t)test_random(&seed));
                events += 1;
            }
        }
        const EmissionLayout layout = {
            .starts = starts,
            .ends = ends,
            .count = count,
            .size = size,
        };
        const EmissionMetadata metadata = {
            .sources = sources,
            .frames = frames,
            .source_count = events,
            .frame_count = events,
            .origin_count = 8,
            .register_count = 17,
        };
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            const EmissionStorage storage = {
                .sources = source_rows[scalar],
                .frames = frame_rows[scalar],
                .source_capacity = events,
                .frame_capacity = events,
            };
            EmissionResolved result;
            EmissionMetadataPlan plan;
            BUSTER_EMISSION_TEST(emission_metadata_plan(&metadata, &layout, scalar != 0, &plan).error == EMISSION_OK);
            BUSTER_EMISSION_TEST(plan.source_count == events && plan.frame_count == events);
            BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, scalar != 0, &result).error == EMISSION_OK);
            for (uint32_t i = 0; i < events; i += 1)
            {
                const uint32_t boundary = i / 2;
                const uint32_t expected = i % 2 != 0 ? (boundary == count ? size : starts[boundary]) : (boundary == 0 ? 0 : ends[boundary - 1]);
                BUSTER_EMISSION_TEST(result.sources[i].offset == expected && result.frames[i].offset == expected);
                BUSTER_EMISSION_TEST(result.sources[i].origin == sources[i].origin && result.sources[i].kind == sources[i].kind);
                BUSTER_EMISSION_TEST(result.frames[i].value == frames[i].value && result.frames[i].reserved == 0);
            }
        }
        BUSTER_EMISSION_TEST(memcmp(source_rows[0], source_rows[1], (size_t)events * sizeof(EmissionSourceRow)) == 0);
        BUSTER_EMISSION_TEST(memcmp(frame_rows[0], frame_rows[1], (size_t)events * sizeof(EmissionFrameRow)) == 0);
    }
}

// The event vocabulary has no single fixed-allocation or single-epilogue assumption. These are protocol examples,
// not generated machine-code tests: instruction/CFG proof and independent DWARF checks remain integration work.
static void test_frame_vocabulary(void)
{
    const uint32_t starts[] = { 0, 1, 4, 8, 12, 16, 20, 24, 28 };
    const uint32_t ends[] = { 1, 4, 8, 12, 16, 20, 24, 28, 29 };
    const EmissionLayout layout = {
        .starts = starts,
        .ends = ends,
        .count = 9,
        .size = 32,
    };
    const EmissionFrameEvent frames[] = {
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_LEFT,
            },
            .value = 16,
            .reg = 7,
            .kind = EMISSION_FRAME_CFA,
        },
        {
            .at = {
                .boundary = 1,
                .side = EMISSION_LEFT,
            },
            .value = -16,
            .reg = 6,
            .kind = EMISSION_FRAME_OFFSET,
        },
        {
            .at = {
                .boundary = 2,
                .side = EMISSION_LEFT,
            },
            .value = 16,
            .reg = 6,
            .kind = EMISSION_FRAME_CFA,
        },
        {
            .at = {
                .boundary = 3,
                .side = EMISSION_LEFT,
            },
            .value = -4096,
            .reg = 12,
            .kind = EMISSION_FRAME_OFFSET,
        },
        {
            .at = {
                .boundary = 4,
                .side = EMISSION_LEFT,
            },
            .kind = EMISSION_FRAME_REMEMBER_STATE,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_LEFT,
            },
            .reg = 12,
            .kind = EMISSION_FRAME_RESTORE,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_LEFT,
            },
            .reg = 6,
            .kind = EMISSION_FRAME_RESTORE,
        },
        {
            .at = {
                .boundary = 5,
                .side = EMISSION_LEFT,
            },
            .value = 8,
            .reg = 7,
            .kind = EMISSION_FRAME_CFA,
        },
        {
            .at = {
                .boundary = 6,
                .side = EMISSION_LEFT,
            },
            .kind = EMISSION_FRAME_RESTORE_STATE,
        },
        {
            .at = {
                .boundary = 7,
                .side = EMISSION_LEFT,
            },
            .value = 13,
            .reg = 12,
            .kind = EMISSION_FRAME_REGISTER,
        },
        {
            .at = {
                .boundary = 8,
                .side = EMISSION_LEFT,
            },
            .reg = 12,
            .kind = EMISSION_FRAME_SAME_VALUE,
        },
        {
            .at = {
                .boundary = 9,
                .side = EMISSION_RIGHT,
            },
            .reg = 16,
            .kind = EMISSION_FRAME_UNDEFINED,
        },
    };
    const EmissionMetadata metadata = {
        .frames = frames,
        .frame_count = 12,
        .register_count = 17,
    };
    EmissionFrameRow rows[12];
    const EmissionStorage storage = {
        .frames = rows,
        .frame_capacity = 12,
    };
    EmissionResolved result;
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, false, &result).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(result.source_count == 0 && result.sources == NULL && result.frame_count == 12);
    BUSTER_EMISSION_TEST(rows[3].value == -4096 && rows[8].offset == 20 && rows[11].offset == 32);
    EmissionFrameEvent probes[] = {
        test_frame(1, EMISSION_LEFT, EMISSION_FRAME_CFA_OFFSET, 0, 4104),
        test_frame(2, EMISSION_LEFT, EMISSION_FRAME_CFA_OFFSET, 0, 8200),
        test_frame(3, EMISSION_LEFT, EMISSION_FRAME_CFA_OFFSET, 0, 12296),
    };
    const EmissionMetadata probed = {
        .frames = probes,
        .frame_count = 3,
        .register_count = 17,
    };
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&probed, &layout, &storage, false, &result).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(rows[0].value == 4104 && rows[1].value == 8200 && rows[2].value == 12296);
}

static void test_limits_and_state_stack(void)
{
    const EmissionLayout layout = {
        .size = UINT32_MAX,
    };
    EmissionSourceEvent source = test_origin(0, EMISSION_LEFT, UINT32_MAX - 1);
    EmissionFrameEvent frames[] = {
        test_frame(0, EMISSION_LEFT, EMISSION_FRAME_REMEMBER_STATE, 0, 0),
        test_frame(0, EMISSION_LEFT, EMISSION_FRAME_REMEMBER_STATE, 0, 0),
        test_frame(0, EMISSION_LEFT, EMISSION_FRAME_OFFSET, UINT16_MAX, INT32_MIN),
        test_frame(0, EMISSION_RIGHT, EMISSION_FRAME_RESTORE_STATE, 0, 0),
        test_frame(0, EMISSION_RIGHT, EMISSION_FRAME_RESTORE_STATE, 0, 0),
    };
    EmissionMetadata metadata = {
        .sources = &source,
        .frames = frames,
        .source_count = 1,
        .frame_count = 5,
        .origin_count = UINT32_MAX,
        .register_count = BUSTER_EMISSION_MAX_REGISTERS,
    };
    EmissionSourceRow source_row;
    EmissionFrameRow frame_rows[5];
    const EmissionStorage storage = {
        .sources = &source_row,
        .frames = frame_rows,
        .source_capacity = 1,
        .frame_capacity = 5,
    };
    EmissionResolved result;
    BUSTER_EMISSION_TEST(emission_metadata_resolve(&metadata, &layout, &storage, false, &result).error == EMISSION_OK);
    BUSTER_EMISSION_TEST(frame_rows[2].reg == UINT16_MAX && frame_rows[2].value == INT32_MIN && frame_rows[4].offset == UINT32_MAX);
    BUSTER_EMISSION_TEST(source_row.origin == UINT32_MAX - 1 && result.size == UINT32_MAX);
    metadata.register_count -= 1;
    const EmissionCheck invalid_register = emission_metadata_check(&metadata, &layout, false);
    BUSTER_EMISSION_TEST(invalid_register.error == EMISSION_INVALID && invalid_register.part == EMISSION_FRAME && invalid_register.index == 2);
    metadata.register_count += 1;
    frames[4].kind = EMISSION_FRAME_UNDEFINED;
    const EmissionCheck leaked_state = emission_metadata_check(&metadata, &layout, false);
    BUSTER_EMISSION_TEST(leaked_state.error == EMISSION_INVALID && leaked_state.part == EMISSION_FRAME && leaked_state.index == 5);
    frames[1].kind = EMISSION_FRAME_RESTORE_STATE;
    const EmissionCheck underflow = emission_metadata_check(&metadata, &layout, false);
    BUSTER_EMISSION_TEST(underflow.error == EMISSION_INVALID && underflow.part == EMISSION_FRAME && underflow.index == 3);
}

int main(void)
{
    test_empty();
    test_layouts();
    test_invalid();
    test_capacity();
    test_guarded_tails();
    test_properties();
    test_frame_vocabulary();
    test_limits_and_state_stack();
#if defined(BUSTER_HOST_VECTOR) && BUSTER_HOST_VECTOR
    const uint32_t vector = 1;
#else
    const uint32_t vector = 0;
#endif
    printf("emission metadata: %llu checks passed (vector=%u)\n", (unsigned long long)test_checks, vector);
    return 0;
}
