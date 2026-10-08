#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(unsigned short) == 2, "LZCNT 16-bit type width");
_Static_assert(sizeof(unsigned int) == 4, "LZCNT 32-bit type width");
_Static_assert(sizeof(unsigned long long) == 8, "LZCNT 64-bit type width");

unsigned short zen5_lzcnt_probe_u16_macro(unsigned short value)
{
    return __lzcnt16(value);
}

unsigned int zen5_lzcnt_probe_u32_function(unsigned int value)
{
    return __lzcnt32(value);
}

unsigned int zen5_lzcnt_probe_u32_alias(unsigned int value)
{
    return _lzcnt_u32(value);
}

unsigned long long zen5_lzcnt_probe_u64_macro(unsigned long long value)
{
    return __lzcnt64(value);
}

unsigned long long zen5_lzcnt_probe_u64_alias(unsigned long long value)
{
    return _lzcnt_u64(value);
}

static volatile unsigned int zen5_lzcnt_evaluation_count;

static unsigned short zen5_lzcnt_side_effect16(unsigned short value)
{
    zen5_lzcnt_evaluation_count += 1;
    return value;
}

static unsigned long long zen5_lzcnt_side_effect64(unsigned long long value)
{
    zen5_lzcnt_evaluation_count += 1;
    return value;
}

static unsigned short zen5_lzcnt_reference16(unsigned short value)
{
    unsigned short bit = 0x8000u;
    unsigned short count = 0;
    while (bit && !(value & bit))
    {
        count += 1;
        bit >>= 1;
    }
    return count;
}

static unsigned int zen5_lzcnt_reference32(unsigned int value)
{
    unsigned int bit = 0x80000000u;
    unsigned int count = 0;
    while (bit && !(value & bit))
    {
        count += 1;
        bit >>= 1;
    }
    return count;
}

static unsigned int zen5_lzcnt_reference64(unsigned long long value)
{
    unsigned long long bit = 0x8000000000000000ull;
    unsigned int count = 0;
    while (bit && !(value & bit))
    {
        count += 1;
        bit >>= 1;
    }
    return count;
}

static unsigned int zen5_lzcnt_check16(unsigned short value)
{
    unsigned int result = zen5_lzcnt_probe_u16_macro(value) != zen5_lzcnt_reference16(value);
    return result;
}

static unsigned int zen5_lzcnt_check32(unsigned int value)
{
    unsigned int expected = zen5_lzcnt_reference32(value);
    unsigned int result = zen5_lzcnt_probe_u32_function(value) != expected ||
                          zen5_lzcnt_probe_u32_alias(value) != expected;
    return result;
}

static unsigned int zen5_lzcnt_check64(unsigned long long value)
{
    unsigned int expected = zen5_lzcnt_reference64(value);
    unsigned int result = zen5_lzcnt_probe_u64_macro(value) != expected ||
                          zen5_lzcnt_probe_u64_alias(value) != expected;
    return result;
}

int main(void)
{
    unsigned int failures = 0;
    unsigned int sample = 0;
    unsigned int powers = 0;
    unsigned int edges = 0;
    unsigned int before16 = 0;
    unsigned short result16 = 0;
    unsigned int before64 = 0;
    unsigned long long result64 = 0;
    unsigned int bit = 0;
    unsigned int i = 0;
    unsigned int cases32[] = {
        0u, 1u, 2u, 3u, 0x7fffffffu, 0x80000000u, 0x80000001u,
        0xffffffffu, 0x55555555u, 0xaaaaaaaau
    };
    unsigned long long cases64[] = {
        0ull, 1ull, 2ull, 3ull, 0x7fffffffffffffffull,
        0x8000000000000000ull, 0x8000000000000001ull,
        0xffffffffffffffffull, 0x5555555555555555ull,
        0xaaaaaaaaaaaaaaaaull
    };

    for (sample = 0; sample <= 0xffffu; sample += 1)
    {
        failures += zen5_lzcnt_check16((unsigned short)sample);
    }
    for (i = 0; i < sizeof(cases32) / sizeof(cases32[0]); i += 1)
    {
        failures += zen5_lzcnt_check32(cases32[i]);
        edges += 1;
    }
    for (bit = 0; bit < 32; bit += 1)
    {
        failures += zen5_lzcnt_check32(1u << bit);
        powers += 1;
    }
    for (i = 0; i < sizeof(cases64) / sizeof(cases64[0]); i += 1)
    {
        failures += zen5_lzcnt_check64(cases64[i]);
        edges += 1;
    }
    for (bit = 0; bit < 64; bit += 1)
    {
        failures += zen5_lzcnt_check64(1ull << bit);
        powers += 1;
    }

    before16 = zen5_lzcnt_evaluation_count;
    result16 = zen5_lzcnt_probe_u16_macro(zen5_lzcnt_side_effect16(0x4000u));
    if (result16 != 1u || zen5_lzcnt_evaluation_count != before16 + 1u)
    {
        failures += 1;
    }

    before64 = zen5_lzcnt_evaluation_count;
    result64 = zen5_lzcnt_probe_u64_macro(zen5_lzcnt_side_effect64(0x4000000000000000ull));
    if (result64 != 1ull || zen5_lzcnt_evaluation_count != before64 + 1u)
    {
        failures += 1;
    }

    if (failures)
    {
        printf("ZEN5_LZCNT_CONFORMANCE status=fail failures=%u\n", failures);
    }
    else
    {
        printf("ZEN5_LZCNT_CONFORMANCE status=pass cases16=65536 edge_cases=%u power_cases=%u\\n",
               edges, powers);
    }
    return failures ? 1 : 0;
}
