// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original sources for clang's -Wimplicit-enum-enum-cast (#1447, the end of CheckImplicitConversion): an implicit
// conversion from one named enumeration to another, an enumerator taken as its enumeration, on by default under
// -Wenum-conversion. The shared warning harness compares the pinned Clang's complete diagnostics through both kernels and
// requested lanes.

// The underlying-type matrix (#1447, review on #1827): each ordered pair of C23 fixed underlying types converts a source
// enumeration's variable and its enumerators (1, its type's maximum and, for a signed type, its minimum) to a target
// enumeration. The warnings it expects follow clang's CheckImplicitConversion apart from the checker: a value whose likely
// range (a constant's significant or active bits, a variable's type's width) is no wider than the target's integer type
// warns of the enumerations, but a constant wider than it, or one whose non-negative value fills a signed target exactly
// from a signed type, warns of its constant conversion instead, and a wider variable of neither.
typedef struct CheckDriverEnumCastType CheckDriverEnumCastType;
struct CheckDriverEnumCastType
{
    const char* name;
    const char* maximum;
    const char* minimum;
    uint32_t width;
    bool is_signed;
};

static const CheckDriverEnumCastType check_driver_enum_cast_types[] =
{
    { "char", "127", "-128", 8, true },
    { "signed char", "127", "-128", 8, true },
    { "unsigned char", "255", NULL, 8, false },
    { "short", "32767", "-32768", 16, true },
    { "unsigned short", "65535", NULL, 16, false },
    { "int", "2147483647", "-2147483647 - 1", 32, true },
    { "unsigned int", "4294967295U", NULL, 32, false },
    { "long", "9223372036854775807L", "-9223372036854775807L - 1", 64, true },
    { "unsigned long", "18446744073709551615UL", NULL, 64, false },
};

// The warnings clang gives the matrix, by kind.
typedef struct CheckDriverEnumCastCounts CheckDriverEnumCastCounts;
struct CheckDriverEnumCastCounts
{
    uint32_t enums;
    uint32_t constants;
};

// One conversion to `target` of a likely range `width` bits wide, `negative` or not, a constant's or else a value's of type
// `source`, counted by the rule above.
static void check_driver_enum_cast_count(CheckDriverEnumCastCounts* counts, const CheckDriverEnumCastType* source, const CheckDriverEnumCastType* target,
                                         uint32_t width, bool negative, bool constant)
{
    const bool wider = width > target->width;
    const bool same_width = constant && width == target->width && target->is_signed && !negative && source->is_signed;
    counts->constants += constant && (wider || same_width) ? 1u : 0u;
    counts->enums += !wider && !same_width ? 1u : 0u;
}

// Writes the matrix's source to `buffer` and returns whether it fits; `counts` gets the warnings clang gives it.
static bool check_driver_enum_cast_matrix(char* buffer, uint64_t capacity, CheckDriverEnumCastCounts* counts)
{
    static const char* const forms[] = { "d = s; ", "d = S%u_one; ", "d = S%u_max; ", "d = S%u_min; " };
    uint64_t used = 0;
    bool fits = true;
    *counts = (CheckDriverEnumCastCounts){ 0 };
    for (uint32_t k = 0; fits && k < BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types); k += 1)
    {
        const CheckDriverEnumCastType* type = check_driver_enum_cast_types + k;
        const int written = type->minimum != NULL ?
            snprintf(buffer + used, capacity - used, "enum S%u : %s { S%u_one = 1, S%u_max = %s, S%u_min = %s };\nenum D%u : %s { D%u_zero };\n", k,
                     type->name, k, k, type->maximum, k, type->minimum, k, type->name, k) :
            snprintf(buffer + used, capacity - used, "enum S%u : %s { S%u_one = 1, S%u_max = %s };\nenum D%u : %s { D%u_zero };\n", k, type->name, k, k,
                     type->maximum, k, type->name, k);
        fits = written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
    }
    for (uint32_t pair = 0; fits && pair < BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types) * BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types); pair += 1)
    {
        const uint32_t i = pair / BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types);
        const uint32_t j = pair % BUSTER_ARRAY_LENGTH(check_driver_enum_cast_types);
        const CheckDriverEnumCastType* source = check_driver_enum_cast_types + i;
        const CheckDriverEnumCastType* target = check_driver_enum_cast_types + j;
        const uint32_t form_count = source->minimum != NULL ? 4u : 3u;
        int written = snprintf(buffer + used, capacity - used, "void f%u_%u(enum S%u s) { enum D%u d; ", i, j, i, j);
        fits = written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
        for (uint32_t form = 0; fits && form < form_count; form += 1)
        {
            written = snprintf(buffer + used, capacity - used, forms[form], i);
            fits = written > 0 && (uint64_t)written < capacity - used;
            used += fits ? (uint64_t)written : 0;
        }
        written = fits ? snprintf(buffer + used, capacity - used, "(void)d; }\n") : 0;
        fits = fits && written > 0 && (uint64_t)written < capacity - used;
        used += fits ? (uint64_t)written : 0;
        // The variable's range is its type's; 1 has one active bit; the maximum all of an unsigned type's bits and all but
        // a signed one's sign; the minimum all of them, as significant bits.
        check_driver_enum_cast_count(counts, source, target, source->width, false, false);
        check_driver_enum_cast_count(counts, source, target, 1, false, true);
        check_driver_enum_cast_count(counts, source, target, source->is_signed ? source->width - 1 : source->width, false, true);
        if (source->minimum != NULL)
        {
            check_driver_enum_cast_count(counts, source, target, source->width, true, true);
        }
    }
    return fits;
}

static void check_test_driver_enum_casts(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_suite(test, clang, executable, directory, "driver/warnings/enum-cast", false);
    static char matrix[16384];
    CheckDriverEnumCastCounts counts = { 0 };
    const bool fits = check_driver_enum_cast_matrix(matrix, sizeof(matrix), &counts);
    check_test_expect(test, fits, "the enumerations' underlying-type matrix fits its buffer");
    if (fits)
    {
        const CheckDriverDefaultWarningFixture fixtures[] =
        {
            {
                .name = "the underlying-type matrix: enumeration and constant conversions",
                .source = matrix,
                .warnings = counts.enums + counts.constants,
                .c23 = true,
            },
            {
                .name = "the underlying-type matrix: -Wno-constant-conversion leaves the enumerations'",
                .source = matrix,
                .options = { "-Wno-constant-conversion" },
                .warnings = counts.enums,
                .c23 = true,
            },
            {
                .name = "the underlying-type matrix: -Wno-implicit-enum-enum-cast leaves the constants'",
                .source = matrix,
                .options = { "-Wno-implicit-enum-enum-cast" },
                .warnings = counts.constants,
                .c23 = true,
            },
        };
        check_test_driver_warning_fixtures(test, clang, executable, directory, fixtures, BUSTER_ARRAY_LENGTH(fixtures));
    }
}
