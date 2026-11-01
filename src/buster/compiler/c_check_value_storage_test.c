// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original representation fixtures for #879. Include after the checker test helpers. Numeric consumers
// remain covered by the existing real/complex action and source tests; these check the non-arithmetic
// handoff, including payloads an x87 load would evaluate. Never read a floating representation's padding.
#include <stddef.h>
#include <fenv.h>

_Static_assert(sizeof(CheckValue) == 96 && _Alignof(CheckValue) == 16, "unchanged checker value layout");
_Static_assert(offsetof(CheckValue, floating) == 32 && offsetof(CheckValue, imaginary) == 48, "unchanged floating offsets");
_Static_assert(sizeof(CheckLiteral) == 48 && _Alignof(CheckLiteral) == 16, "unchanged literal layout");
_Static_assert(offsetof(CheckLiteral, floating) == 16 && sizeof(long double) == 16, "matching host long double representation");
_Static_assert(__LDBL_MANT_DIG__ == 64 && __LDBL_MAX_EXP__ == 16384, "x87 extended format");

static void check_test_value_storage(CheckTest* test)
{
    const CheckValue identifier = {
        .type = BUSTER_TYPE_INT,
        .category = CHECK_LVALUE,
        .object = 17,
    };
    check_test_expect(test, identifier.floating_storage[0] == 0 && identifier.floating_storage[1] == 0 &&
        identifier.imaginary_storage[0] == 0 && identifier.imaginary_storage[1] == 0 && identifier.object == 17,
        "omitted real and imaginary payloads are positive-zero integer storage");
    const CheckLiteral integer = {
        .type = BUSTER_TYPE_INT,
        .integer = UINT64_C(0xfedcba9876543210),
        .valid = true,
        .high = UINT64_C(0x123456789abcdef0),
    };
    CheckValue value = check_literal_value(integer);
    check_test_expect(test, value.integer == (((unsigned __int128)integer.high << 64) | integer.integer) &&
        value.constant_kind == CHECK_CONSTANT_INTEGER && value.flags == BUSTER_CHECK_ICE &&
        value.floating_storage[0] == 0 && value.floating_storage[1] == 0 &&
        value.imaginary_storage[0] == 0 && value.imaginary_storage[1] == 0,
        "integer literal construction preserves all 128 bits without floating work");
    CheckLiteral imaginary_integer = integer;
    imaginary_integer.imaginary = true;
    imaginary_integer.type = BUSTER_TYPE_COMPLEX(type_index(BUSTER_TYPE_INT));
    value = check_literal_value(imaginary_integer);
    check_test_expect(test, value.integer == (unsigned __int128)integer.integer << 64 && value.flags == 0 &&
        value.constant_kind == CHECK_CONSTANT_COMPLEX && value.floating_storage[0] == 0 &&
        value.floating_storage[1] == 0 && value.imaginary_storage[0] == 0 && value.imaginary_storage[1] == 0,
        "imaginary integer construction keeps the established packed component representation");
    const CheckLiteral null_pointer = { .type = BUSTER_TYPE_NULLPTR, .valid = true };
    value = check_literal_value(null_pointer);
    check_test_expect(test, value.constant_kind == CHECK_CONSTANT_ADDRESS && value.flags == 0 && value.integer == 0 &&
        value.object == 0 && value.offset == 0 && value.fault == CHECK_FAULT_NONE,
        "nullptr construction preserves address metadata and omitted fields");

    // Independent x87 encodings: +/- zero, one, least subnormal, greatest finite, infinity, quiet NaN and
    // signaling NaN. The padding is deliberately different from the zero padding the handoff publishes.
    const uint64_t significands[] = {
        0,
        0,
        UINT64_C(0x8000000000000000),
        1,
        UINT64_MAX,
        UINT64_C(0x8000000000000000),
        UINT64_C(0xc000000000000123),
        UINT64_C(0x8000000000000123),
    };
    const uint16_t exponents[] = { 0, 0x8000, 0x3fff, 0, 0x7ffe, 0x7fff, 0x7fff, 0x7fff };
    fenv_t saved;
    const bool saved_environment = fegetenv(&saved) == 0;
    check_test_expect(test, saved_environment, "capture floating environment before representation-only handoff");
    if (saved_environment)
    {
        check_test_expect(test, feclearexcept(FE_ALL_EXCEPT) == 0, "clear exceptions before representation-only handoff");
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(significands); i += 1)
        {
            for (uint32_t imaginary = 0; imaginary < 2; imaginary += 1)
            {
                CheckLiteral literal = {
                    .type = imaginary ? BUSTER_TYPE_COMPLEX_LONG_DOUBLE : BUSTER_TYPE_LONG_DOUBLE,
                    .real = true,
                    .imaginary = imaginary != 0,
                    .valid = true,
                };
                memcpy(&literal.floating, significands + i, sizeof(significands[i]));
                memcpy((uint8_t*)&literal.floating + 8, exponents + i, sizeof(exponents[i]));
                memset((uint8_t*)&literal.floating + 10, 0xa5, sizeof(literal.floating) - 10);
                value = check_literal_value(literal);
                const uint64_t* active = imaginary ? value.imaginary_storage : value.floating_storage;
                const uint64_t* inactive = imaginary ? value.floating_storage : value.imaginary_storage;
                check_test_expect(test, active[0] == significands[i] && active[1] == exponents[i] &&
                    inactive[0] == 0 && inactive[1] == 0 && value.type == literal.type &&
                    value.constant_kind == (imaginary ? CHECK_CONSTANT_COMPLEX : CHECK_CONSTANT_FLOATING) &&
                    value.flags == (imaginary ? 0 : BUSTER_CHECK_FLOAT_LITERAL),
                    "literal handoff preserves every meaningful bit and zeroes inactive components and padding");
            }
        }
        check_test_expect(test, fetestexcept(FE_ALL_EXCEPT) == 0, "literal handoff does not evaluate signaling NaNs or raise floating exceptions");
        check_test_expect(test, fesetenv(&saved) == 0, "restore floating environment after representation fixtures");
    }

    // A constexpr identifier's copy (#879) takes the same ten meaningful bytes as the literal handoff, never an x87
    // load and store, and leaves the destination's padding zero although the source's is poison.
    CheckValue source = { 0 };
    source.floating_storage[0] = UINT64_C(0x8000000000000123);
    source.floating_storage[1] = UINT64_C(0xa5a5a5a5a5a57fff);
    source.imaginary_storage[0] = UINT64_C(0xc000000000000456);
    source.imaginary_storage[1] = UINT64_C(0x5a5a5a5a5a5affff);
    CheckValue destination = { 0 };
    if (saved_environment)
    {
        check_test_expect(test, feclearexcept(FE_ALL_EXCEPT) == 0, "clear exceptions before the constexpr payload copy");
        check_value_copy_floating(&destination, &source);
        check_test_expect(test, destination.floating_storage[0] == source.floating_storage[0] &&
            destination.floating_storage[1] == (source.floating_storage[1] & UINT64_C(0xffff)) &&
            destination.imaginary_storage[0] == source.imaginary_storage[0] &&
            destination.imaginary_storage[1] == (source.imaginary_storage[1] & UINT64_C(0xffff)),
            "constexpr payload copy preserves the ten meaningful bytes and zeroes the poison padding");
        check_test_expect(test, fetestexcept(FE_ALL_EXCEPT) == 0, "constexpr payload copy raises no floating exception");
        check_test_expect(test, fesetenv(&saved) == 0, "restore the floating environment after the constexpr payload copy");
    }
}
