#include <buster/tests/integer_test.h>
#include <buster/lib/integer.h>

#if BUSTER_INCLUDE_TESTS
// Test-only research kernel for link_objects' checked align/add recurrence.
// integer_alignment_summary_then composes constant-size transfer functions;
// integer_alignment_blocked reconstructs offsets and the first failing prefix.
// Nothing here replaces a production path or establishes a performance win.
typedef struct IntegerAlignmentItem IntegerAlignmentItem;
struct IntegerAlignmentItem
{
    u64 alignment;
    u64 size;
};

typedef struct IntegerAlignmentSummary IntegerAlignmentSummary;
struct IntegerAlignmentSummary
{
    u64 bias;
    u64 alignment;
    u64 tail;
    bool valid;
};

typedef struct IntegerAlignmentScan IntegerAlignmentScan;
struct IntegerAlignmentScan
{
    u64 end;
    u32 completed;
    bool valid;
};

enum
{
    INTEGER_ALIGNMENT_TEST_CAPACITY = 65,
};

// On its defined domain, T(x) = round_up(x + bias, alignment) + tail.
// A false valid flag denotes the everywhere-undefined transfer, not x == 0.
BUSTER_GLOBAL_LOCAL bool integer_alignment_summary_apply(IntegerAlignmentSummary summary, u64 input, u64* output)
{
    u64 shifted;
    u64 aligned;
    bool valid = summary.valid && output && u64_add_checked(input, summary.bias, &shifted) &&
                 align_forward_checked(shifted, summary.alignment, &aligned) && u64_add_checked(aligned, summary.tail, output);
    return valid;
}

BUSTER_GLOBAL_LOCAL IntegerAlignmentSummary integer_alignment_summary_item(IntegerAlignmentItem item)
{
    IntegerAlignmentSummary result = {
        .alignment = item.alignment,
        .tail = item.size,
        .valid = item.alignment && !(item.alignment & (item.alignment - 1u)),
    };
    return result;
}

// "Then" preserves input order: right(left(x)). Power-of-two alignments are
// comparable by divisibility. Checked arithmetic is essential; wrapping would
// make a failed prefix appear to recover. Nonnegative transfers cannot recover.
BUSTER_GLOBAL_LOCAL IntegerAlignmentSummary integer_alignment_summary_then(IntegerAlignmentSummary left, IntegerAlignmentSummary right)
{
    IntegerAlignmentSummary result = {.alignment = 1};
    if (left.valid && right.valid)
    {
        u64 shift;
        bool valid = u64_add_checked(left.tail, right.bias, &shift);
        if (valid)
        {
            u64 rounded;
            if (left.alignment >= right.alignment)
            {
                result.bias = left.bias;
                result.alignment = left.alignment;
                valid = align_forward_checked(shift, right.alignment, &rounded) && u64_add_checked(rounded, right.tail, &result.tail);
            }
            else
            {
                result.alignment = right.alignment;
                result.tail = right.tail;
                valid = align_forward_checked(shift, left.alignment, &rounded) && u64_add_checked(left.bias, rounded, &result.bias);
            }
            result.valid = valid;
            u64 at_zero;
            result.valid = integer_alignment_summary_apply(result, 0, &at_zero);
        }
    }
    return result;
}

// The incumbent's arithmetic, separated from the linker's metadata validation.
// An offset is published only after both rounding and size addition succeed.
BUSTER_GLOBAL_LOCAL IntegerAlignmentScan integer_alignment_scalar(IntegerAlignmentItem const* items, u32 count, u64 input, u64* offsets)
{
    IntegerAlignmentScan result = {.end = input, .valid = !count || (items && offsets)};
    while (result.valid && result.completed < count)
    {
        IntegerAlignmentItem item = items[result.completed];
        u64 aligned;
        result.valid = align_forward_checked(result.end, item.alignment, &aligned);
        if (result.valid)
        {
            u64 next;
            result.valid = u64_add_checked(aligned, item.size, &next);
            if (result.valid)
            {
                offsets[result.completed++] = aligned;
                result.end = next;
            }
        }
    }
    return result;
}

// Structurally different oracle: remainder and padding, no composition and no
// align_forward/u64_add helper. It retains the exact successfully written prefix.
BUSTER_GLOBAL_LOCAL IntegerAlignmentScan integer_alignment_reference(IntegerAlignmentItem const* items, u32 count, u64 input, u64* offsets)
{
    IntegerAlignmentScan result = {.end = input, .valid = !count || (items && offsets)};
    while (result.valid && result.completed < count)
    {
        IntegerAlignmentItem item = items[result.completed];
        result.valid = item.alignment && !(item.alignment & (item.alignment - 1u));
        if (result.valid)
        {
            u64 remainder = result.end % item.alignment;
            u64 padding = remainder ? item.alignment - remainder : 0;
            result.valid = padding <= UINT64_MAX - result.end;
            if (result.valid)
            {
                u64 aligned = result.end + padding;
                result.valid = item.size <= UINT64_MAX - aligned;
                if (result.valid)
                {
                    offsets[result.completed++] = aligned;
                    result.end = aligned + item.size;
                }
            }
        }
    }
    return result;
}

// Bounded experimental storage, not a production threshold or a scheduler.
// Phase 1 summaries have no incoming-state dependency. Phase 2 scans summaries.
// Phase 3 reconstructs each ready block; the first failed block is replayed to
// recover partial outputs. Later output slots remain untouched.
BUSTER_GLOBAL_LOCAL IntegerAlignmentScan integer_alignment_blocked(IntegerAlignmentItem const* items, u32 count, u64 input, u32 chunk, u64* offsets)
{
    IntegerAlignmentScan result = {
        .end = input,
        .valid = chunk && count <= INTEGER_ALIGNMENT_TEST_CAPACITY && (!count || (items && offsets)),
    };
    if (result.valid)
    {
        IntegerAlignmentSummary summaries[INTEGER_ALIGNMENT_TEST_CAPACITY];
        u64 incoming[INTEGER_ALIGNMENT_TEST_CAPACITY];
        u32 block_count = count / chunk + (count % chunk != 0);
        for (u32 block = 0; block < block_count; block += 1)
        {
            // block * chunk < count, even when chunk is UINT32_MAX.
            u32 first = block * chunk;
            u32 length = BUSTER_MIN(chunk, count - first);
            IntegerAlignmentSummary summary = {.alignment = 1, .valid = true};
            for (u32 offset = 0; offset < length; offset += 1)
            {
                IntegerAlignmentSummary item = integer_alignment_summary_item(items[first + offset]);
                summary = integer_alignment_summary_then(summary, item);
            }
            summaries[block] = summary;
        }
        u64 prefix_end = input;
        u32 ready = 0;
        bool prefix_valid = true;
        while (ready < block_count && prefix_valid)
        {
            incoming[ready] = prefix_end;
            prefix_valid = integer_alignment_summary_apply(summaries[ready], prefix_end, &prefix_end);
            ready += prefix_valid;
        }
        for (u32 block = 0; block < block_count && block <= ready && result.valid; block += 1)
        {
            u32 first = block * chunk;
            u32 length = BUSTER_MIN(chunk, count - first);
            IntegerAlignmentScan local = integer_alignment_scalar(items + first, length, incoming[block], offsets + first);
            result.end = local.end;
            result.completed = first + local.completed;
            result.valid = local.valid;
        }
        result.valid = result.valid && prefix_valid && result.end == prefix_end;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void integer_alignment_compare(UnitTestArguments* arguments, UnitTestResult* totals, IntegerAlignmentItem const* items, u32 count, u64 input, u32 chunk)
{
    UnitTestResult result = {0};
    u64 expected[INTEGER_ALIGNMENT_TEST_CAPACITY + 2];
    u64 scalar[INTEGER_ALIGNMENT_TEST_CAPACITY + 2];
    u64 blocked[INTEGER_ALIGNMENT_TEST_CAPACITY + 2];
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
    {
        expected[index] = scalar[index] = blocked[index] = UINT64_C(0xfeedface01234567);
    }
    if (BUSTER_REQUIRE(arguments, count <= INTEGER_ALIGNMENT_TEST_CAPACITY && chunk))
    {
        IntegerAlignmentScan reference = integer_alignment_reference(items, count, input, expected + 1);
        IntegerAlignmentScan direct = integer_alignment_scalar(items, count, input, scalar + 1);
        IntegerAlignmentScan actual = integer_alignment_blocked(items, count, input, chunk, blocked + 1);
        IntegerAlignmentSummary whole = {.alignment = 1, .valid = true};
        for (u32 index = 0; index < count; index += 1)
        {
            IntegerAlignmentSummary item = integer_alignment_summary_item(items[index]);
            whole = integer_alignment_summary_then(whole, item);
        }
        u64 summary_end = 17;
        bool summary_valid = integer_alignment_summary_apply(whole, input, &summary_end);
        BUSTER_TEST(arguments, summary_valid == reference.valid && summary_end == (summary_valid ? reference.end : 17u));
        BUSTER_TEST(arguments, reference.valid == direct.valid && reference.completed == direct.completed && reference.end == direct.end);
        BUSTER_TEST(arguments, reference.valid == actual.valid && reference.completed == actual.completed && reference.end == actual.end);
        // Includes both guards, unused tail, and the failed/future output slots.
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(expected); index += 1)
        {
            BUSTER_TEST(arguments, expected[index] == scalar[index] && expected[index] == blocked[index]);
        }
    }
    totals->test_count += result.test_count;
    totals->succeeded_test_count += result.succeeded_test_count;
}

BUSTER_GLOBAL_LOCAL u64 integer_alignment_random(u64* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state;
}

BUSTER_GLOBAL_LOCAL UnitTestResult integer_alignment_recurrence_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    IntegerAlignmentSummary identity = {.alignment = 1, .valid = true};
    IntegerAlignmentSummary invalid = {0};
    u64 output = 17;
    bool valid = integer_alignment_summary_apply(identity, UINT64_MAX, &output);
    BUSTER_TEST(arguments, valid && output == UINT64_MAX);
    valid = integer_alignment_summary_apply(identity, 0, 0);
    BUSTER_TEST(arguments, !valid);
    valid = integer_alignment_summary_apply(invalid, 0, &output);
    BUSTER_TEST(arguments, !valid && output == UINT64_MAX);
    IntegerAlignmentSummary add_one = integer_alignment_summary_item((IntegerAlignmentItem){1, 1});
    IntegerAlignmentSummary even = integer_alignment_summary_item((IntegerAlignmentItem){2, 0});
    IntegerAlignmentSummary forward = integer_alignment_summary_then(add_one, even);
    IntegerAlignmentSummary backward = integer_alignment_summary_then(even, add_one);
    valid = integer_alignment_summary_apply(forward, 0, &output);
    BUSTER_TEST(arguments, valid && output == 2);
    valid = integer_alignment_summary_apply(backward, 0, &output);
    BUSTER_TEST(arguments, valid && output == 1);

    IntegerAlignmentItem boundary[] = {{1, 1}, {UINT64_C(1) << 63, 0}, {1, UINT64_MAX - (UINT64_C(1) << 63)}};
    for (u32 chunk = 1; chunk <= BUSTER_ARRAY_LENGTH(boundary) + 1u; chunk += 1)
    {
        integer_alignment_compare(arguments, &result, boundary, BUSTER_ARRAY_LENGTH(boundary), 0, chunk);
        integer_alignment_compare(arguments, &result, boundary, BUSTER_ARRAY_LENGTH(boundary), UINT64_C(1) << 63, chunk);
    }
    IntegerAlignmentSummary maximum = integer_alignment_summary_item((IntegerAlignmentItem){1, UINT64_MAX});
    IntegerAlignmentSummary impossible = integer_alignment_summary_then(maximum, add_one);
    output = 17;
    valid = integer_alignment_summary_apply(impossible, 0, &output);
    BUSTER_TEST(arguments, !valid && output == 17);
    IntegerAlignmentSummary invalid_alignment = integer_alignment_summary_item((IntegerAlignmentItem){UINT64_MAX, 0});
    invalid_alignment = integer_alignment_summary_then(invalid_alignment, identity);
    valid = integer_alignment_summary_apply(invalid_alignment, 0, &output);
    BUSTER_TEST(arguments, !valid && output == 17);

    // Exhaust every ordered triple from a reduced alphabet, not the u64 domain.
    // Compare denotations: equivalent summaries need not be byte-identical.
    IntegerAlignmentItem alphabet[12];
    u64 sizes[] = {0, 1, 3};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(alphabet); index += 1)
    {
        alphabet[index] = (IntegerAlignmentItem){UINT64_C(1) << (index / 3u), sizes[index % 3u]};
    }
    u64 extremes[] = {UINT64_MAX - 17u, UINT64_MAX - 1u, UINT64_MAX, UINT64_C(1) << 63};
    for (u32 a = 0; a < BUSTER_ARRAY_LENGTH(alphabet); a += 1)
    {
        for (u32 b = 0; b < BUSTER_ARRAY_LENGTH(alphabet); b += 1)
        {
            for (u32 c = 0; c < BUSTER_ARRAY_LENGTH(alphabet); c += 1)
            {
                IntegerAlignmentItem items[] = {alphabet[a], alphabet[b], alphabet[c]};
                IntegerAlignmentSummary sa = integer_alignment_summary_item(items[0]);
                IntegerAlignmentSummary sb = integer_alignment_summary_item(items[1]);
                IntegerAlignmentSummary sc = integer_alignment_summary_item(items[2]);
                IntegerAlignmentSummary ab = integer_alignment_summary_then(sa, sb);
                IntegerAlignmentSummary bc = integer_alignment_summary_then(sb, sc);
                IntegerAlignmentSummary left = integer_alignment_summary_then(ab, sc);
                IntegerAlignmentSummary right = integer_alignment_summary_then(sa, bc);
                IntegerAlignmentSummary left_identity = integer_alignment_summary_then(identity, left);
                IntegerAlignmentSummary right_identity = integer_alignment_summary_then(right, identity);
                IntegerAlignmentSummary forms[] = {left, right, left_identity, right_identity};
                for (u32 state = 0; state < 16u + BUSTER_ARRAY_LENGTH(extremes); state += 1)
                {
                    u64 input = state < 16u ? state : extremes[state - 16u];
                    u64 offsets[3];
                    IntegerAlignmentScan reference = integer_alignment_reference(items, BUSTER_ARRAY_LENGTH(items), input, offsets);
                    for (u32 form = 0; form < BUSTER_ARRAY_LENGTH(forms); form += 1)
                    {
                        output = 17;
                        valid = integer_alignment_summary_apply(forms[form], input, &output);
                        BUSTER_TEST(arguments, valid == reference.valid && output == (valid ? reference.end : 17u));
                    }
                }
            }
        }
    }

    IntegerAlignmentItem items[INTEGER_ALIGNMENT_TEST_CAPACITY];
    for (u32 family = 0; family < 4; family += 1)
    {
        for (u32 count = 0; count <= INTEGER_ALIGNMENT_TEST_CAPACITY; count += 1)
        {
            for (u32 index = 0; index < count; index += 1)
            {
                items[index] = (IntegerAlignmentItem){UINT64_C(1) << (index % 6u), index % 9u};
                if (family == 1)
                {
                    items[index] = (IntegerAlignmentItem){UINT64_C(1) << (index % 64u), 0};
                }
                else if (family == 2 && index == count / 2u)
                {
                    items[index].alignment = count & 1u ? 0 : 3;
                }
                else if (family == 3 && index == count / 2u)
                {
                    items[index].size = UINT64_MAX;
                }
            }
            u64 input = family == 1 ? UINT64_MAX - 63u : family == 3 ? 1u : 0;
            for (u32 chunk = 1; chunk <= count + 1u; chunk += 1)
            {
                integer_alignment_compare(arguments, &result, items, count, input, chunk);
            }
            integer_alignment_compare(arguments, &result, items, count, input, UINT32_MAX);
        }
    }
    // Every error position, including the first and last, at every chunk width.
    for (u32 bad = 0; bad < INTEGER_ALIGNMENT_TEST_CAPACITY; bad += 1)
    {
        for (u32 index = 0; index < INTEGER_ALIGNMENT_TEST_CAPACITY; index += 1)
        {
            items[index] = (IntegerAlignmentItem){8, 1};
        }
        items[bad].alignment = 3;
        for (u32 chunk = 1; chunk <= INTEGER_ALIGNMENT_TEST_CAPACITY + 1u; chunk += 1)
        {
            integer_alignment_compare(arguments, &result, items, INTEGER_ALIGNMENT_TEST_CAPACITY, 0, chunk);
        }
    }
    u64 random = UINT64_C(0x318fc65ac47e1029);
    for (u32 trial = 0; trial < 192; trial += 1)
    {
        u64 bits = integer_alignment_random(&random);
        u32 count = (u32)(bits % (INTEGER_ALIGNMENT_TEST_CAPACITY + 1u));
        u64 input = trial & 1u ? integer_alignment_random(&random) : 0;
        for (u32 index = 0; index < count; index += 1)
        {
            bits = integer_alignment_random(&random);
            u64 size = bits & 15u;
            if ((bits >> 8) % 7u == 0)
            {
                size = UINT64_MAX - size;
            }
            items[index] = (IntegerAlignmentItem){UINT64_C(1) << ((bits >> 16) % 64u), size};
        }
        for (u32 chunk = 1; chunk <= count + 1u; chunk += 1)
        {
            integer_alignment_compare(arguments, &result, items, count, input, chunk);
        }
    }
    IntegerAlignmentScan empty = integer_alignment_blocked(0, 0, UINT64_MAX, 1, 0);
    BUSTER_TEST(arguments, empty.valid && empty.completed == 0 && empty.end == UINT64_MAX);
    IntegerAlignmentScan bad_chunk = integer_alignment_blocked(0, 0, 11, 0, 0);
    IntegerAlignmentScan bad_count = integer_alignment_blocked(0, INTEGER_ALIGNMENT_TEST_CAPACITY + 1u, 11, 1, 0);
    IntegerAlignmentScan bad_pointer = integer_alignment_blocked(0, 1, 11, 1, &output);
    BUSTER_TEST(arguments, !bad_chunk.valid && bad_chunk.completed == 0 && bad_chunk.end == 11);
    BUSTER_TEST(arguments, !bad_count.valid && bad_count.completed == 0 && bad_count.end == 11);
    BUSTER_TEST(arguments, !bad_pointer.valid && bad_pointer.completed == 0 && bad_pointer.end == 11);
    return result;
}

UnitTestResult integer_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    u64 sum = UINT64_MAX;
    bool valid = u64_add_checked(0, 0, &sum);
    BUSTER_TEST(arguments, valid && sum == 0);

    sum = 0;
    valid = u64_add_checked(UINT64_MAX - 1, 1, &sum);
    BUSTER_TEST(arguments, valid && sum == UINT64_MAX);

    sum = 7;
    valid = u64_add_checked(UINT64_MAX, 1, &sum);
    BUSTER_TEST(arguments, !valid && sum == 7);

    valid = u64_add_checked(1, 1, 0);
    BUSTER_TEST(arguments, !valid);

    u64 aligned = UINT64_MAX;
    valid = align_forward_checked(0, 0, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(9, 3, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    u64 values[] = {0, 1, 7, 8, 9, 63, 64, 65};
    u64 alignments[] = {1, 2, 4, 8, 16, 32, 64, 128};
    u64 expected[] = {0, 2, 8, 8, 16, 64, 64, 128};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(values); index += 1)
    {
        aligned = UINT64_MAX;
        valid = align_forward_checked(values[index], alignments[index], &aligned);
        BUSTER_TEST(arguments, valid && aligned == expected[index]);
    }

    aligned = UINT64_MAX;
    valid = align_forward_checked(1, UINT64_C(1) << 63, &aligned);
    BUSTER_TEST(arguments, valid && aligned == (UINT64_C(1) << 63));

    aligned = UINT64_MAX;
    valid = align_forward_checked((UINT64_C(1) << 63) + 1, UINT64_C(1) << 63, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(UINT64_MAX, 2, &aligned);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    aligned = UINT64_MAX;
    valid = align_forward_checked(1, 8, 0);
    BUSTER_TEST(arguments, !valid && aligned == UINT64_MAX);

    BUSTER_TEST_FIXTURE(arguments, integer_alignment_recurrence_tests);

    BUSTER_TEST(arguments, align_forward(9, 8) == 16);
    BUSTER_TEST(arguments, is_aligned(0, 1) && is_aligned(64, 64) && !is_aligned(65, 64));
    return result;
}
#endif
