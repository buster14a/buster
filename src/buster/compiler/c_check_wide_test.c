// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// c_check_wide.c's kernels against their scalar twins (#1447): the bitwise operations, sums and differences with their
// carries, compares and shifts, at every count of limbs around the vector's 8 and at random places, with random limbs
// and limbs of all ones that carry through whole chunks. Then values: a wide constant's limbs survive a conversion
// round trip, read as an int64_t only where they fit, and print in decimal as clang does.
#ifndef BUSTER_C_CHECK_WIDE_TEST_INCLUDED
#define BUSTER_C_CHECK_WIDE_TEST_INCLUDED

#define BUSTER_CHECK_WIDE_TEST_LIMBS 80u

static uint64_t check_test_wide_limb(CheckTest* test, uint32_t shape)
{
    const uint64_t random = (uint64_t)check_test_random(test) << 32 | check_test_random(test);
    // Shapes: random limbs, all ones (a carry passes through), zeros, or one bit.
    return shape == 0 ? random : shape == 1 ? UINT64_MAX : shape == 2 ? 0 : UINT64_C(1) << (random & 63);
}

static void check_test_wide_kernels(CheckTest* test)
{
    uint64_t a[BUSTER_CHECK_WIDE_TEST_LIMBS];
    uint64_t b[BUSTER_CHECK_WIDE_TEST_LIMBS];
    uint64_t vector[BUSTER_CHECK_WIDE_TEST_LIMBS];
    uint64_t scalar[BUSTER_CHECK_WIDE_TEST_LIMBS];
    bool same = true;
    for (uint32_t count = 1; count <= BUSTER_CHECK_WIDE_TEST_LIMBS && same; count += 1)
    {
        for (uint32_t round = 0; round < 24 && same; round += 1)
        {
            const uint32_t shape = round % 4;
            for (uint32_t i = 0; i < count; i += 1)
            {
                a[i] = check_test_wide_limb(test, shape);
                b[i] = check_test_wide_limb(test, (shape + round / 4) % 4);
            }
            // An equal pair now and then, or one that differs in a single limb.
            if (round % 6 == 5)
            {
                memcpy(b, a, count * sizeof(uint64_t));
                b[check_test_random(test) % count] ^= round % 12 == 5 ? 0 : 1;
            }
            for (uint32_t op = CHECK_WIDE_AND; op <= CHECK_WIDE_NOT; op += 1)
            {
                check_wide_bitwise_vector((CheckWideBitwise)op, vector, a, b, count);
                check_wide_bitwise_scalar((CheckWideBitwise)op, scalar, a, b, count);
                same &= memcmp(vector, scalar, count * sizeof(uint64_t)) == 0;
            }
            for (uint32_t form = 0; form < 4; form += 1)
            {
                const bool invert = (form & 1) != 0;
                const uint32_t carry = form >> 1;
                const uint32_t vector_carry = check_wide_add_vector(vector, a, b, count, invert, carry);
                const uint32_t scalar_carry = check_wide_add_scalar(scalar, a, b, count, invert, carry);
                same &= vector_carry == scalar_carry && memcmp(vector, scalar, count * sizeof(uint64_t)) == 0;
            }
            same &= check_wide_compare_vector(a, b, count) == check_wide_compare_scalar(a, b, count);
            same &= check_wide_compare_vector(a, a, count) == 0;
            const uint32_t bits = count * BUSTER_CHECK_WIDE_LIMB_BITS;
            const uint32_t amounts[] = { 0, 1, 63, 64, 65, bits - 1, check_test_random(test) % bits, check_test_random(test) % bits };
            for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(amounts); i += 1)
            {
                const uint32_t amount = amounts[i] < bits ? amounts[i] : 0;
                for (uint32_t left = 0; left < 2; left += 1)
                {
                    if (left != 0)
                    {
                        check_wide_shift_left_vector(vector, a, count, amount);
                        check_wide_shift_left_scalar(scalar, a, count, amount);
                    }
                    else
                    {
                        check_wide_shift_right_vector(vector, a, count, amount);
                        check_wide_shift_right_scalar(scalar, a, count, amount);
                    }
                    same &= memcmp(vector, scalar, count * sizeof(uint64_t)) == 0;
                }
            }
        }
    }
    check_test_expect(test, same, "wide constant kernels match their scalar twins");
}

// 2^exponent's decimal digits for a few widths, against the value folded from a source-like path: a literal's limbs
// converted to the type, shifted and negated.
static void check_test_wide_values(CheckTest* test, TypeTable* table)
{
    const TypeId signed_200 = type_bit_int(table, 200, false);
    const TypeId unsigned_300 = type_bit_int(table, 300, true);
    bool valid = signed_200 != 0 && unsigned_300 != 0;
    CheckValue one = {
        .type = BUSTER_TYPE_INT,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE,
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = 1,
    };
    // check_wide_convert leaves the type to its caller, check_action_convert_plain.
    CheckValue value = one;
    valid = valid && check_wide_convert(table, &value, signed_200, 1) && value.constant_kind == CHECK_CONSTANT_INTEGER;
    value.type = signed_200;
    CheckValue count = one;
    count.integer = 199;
    CheckValue shifted = value;
    uint32_t warnings = 0;
    // 1 << 199 shifts into the sign: clang's shift-value fault, which GNU folding takes as the minimum.
    valid = valid && check_wide_fold(table, CHECK_OP_SHIFT_LEFT, &value, &count, &shifted, 1, BUSTER_CHECK_EXTENSION_GNU_CONSTANT, false, &warnings);
    char digits[128];
    const char* minimum = "-803469022129495137770981046170581301261101496891396417650688";
    uint32_t length = valid ? check_wide_decimal(table, &shifted, digits, sizeof(digits)) : 0;
    valid = valid && length == strlen(minimum) && memcmp(digits, minimum, length) == 0;
    // The unsigned 2^299 - 1 and its int64_t refusal; then 12345 converted and read back.
    CheckValue ones = one;
    ones.integer = (unsigned __int128)0 - 1;
    ones.type = BUSTER_TYPE_LONG;
    valid = valid && check_wide_convert(table, &ones, unsigned_300, 2);
    ones.type = unsigned_300;
    CheckValue all = ones;
    count.integer = 1;
    valid = valid && check_wide_fold(table, CHECK_OP_SHIFT_RIGHT, &ones, &count, &all, 2, 0, true, &warnings);
    const char* maximum = "1018517988167243043134222844204689080525734196832968125318070224677190649881668353091698687";
    length = valid ? check_wide_decimal(table, &all, digits, sizeof(digits)) : 0;
    valid = valid && length == strlen(maximum) && memcmp(digits, maximum, length) == 0;
    int64_t number = 0;
    valid = valid && !check_wide_i64(table, &all, &number);
    CheckValue small = one;
    small.integer = (unsigned __int128)(__int128)-12345;
    small.type = BUSTER_TYPE_INT;
    valid = valid && check_wide_convert(table, &small, signed_200, 3);
    small.type = signed_200;
    valid = valid && check_wide_i64(table, &small, &number) && number == -12345;
    length = valid ? check_wide_decimal(table, &small, digits, sizeof(digits)) : 0;
    valid = valid && length == 6 && memcmp(digits, "-12345", 6) == 0;
    // A value of a wide type whose pointer isn't the region's is no constant.
    CheckValue forged = small;
    forged.floating_storage[0] = (uint64_t)(uintptr_t)digits;
    bool truth = false;
    valid = valid && !check_wide_truth(table, &forged, &truth) && check_wide_truth(table, &small, &truth) && truth;
    check_test_expect(test, valid, "wide constants convert, fold, read and print as clang's");
}

static __attribute__((noinline)) void check_test_wide(CheckTest* test, TypeTable* table)
{
    check_test_wide_kernels(test);
    check_test_wide_values(test, table);
}

#endif
