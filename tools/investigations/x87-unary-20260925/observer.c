#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
_Static_assert(CHAR_BIT == 8 && UINT_MAX == 4294967295u, "32-bit unsigned int required");
_Static_assert(ULONG_MAX == 18446744073709551615ul, "LP64 required");
_Static_assert(LDBL_MANT_DIG == 64 && sizeof(long double) == 16, "SysV binary80 required");
struct Cell { long double value; };
#define CASE(name, expression, expected, control) \
    extern long double scalar_##name; \
    extern long double array_##name[1]; \
    extern struct Cell record_##name; \
    extern long double *local_static_##name(void); \
    extern long double automatic_##name(void); \
    extern double double_##name;
#include "cases.def"
#undef CASE
static unsigned checks, failures, control_failures;
static void observe(const char *name, const char *context, long double got, long double expected, int control)
{
    unsigned char actual_bytes[sizeof got], expected_bytes[sizeof expected];
    memcpy(actual_bytes, &got, sizeof got);
    memcpy(expected_bytes, &expected, sizeof expected);
    /* Compare only the ten value bytes; padding is explicitly ignored. */
    int mismatch = memcmp(actual_bytes, expected_bytes, 10) != 0;
    printf("%s\t%s\t%d\t%d\t%La\t%La\t", name, context, control, mismatch, got, expected);
    for (unsigned i = 0; i < 10; ++i) printf("%02x", actual_bytes[i]);
    putchar('\t');
    for (unsigned i = 0; i < 10; ++i) printf("%02x", expected_bytes[i]);
    putchar('\n');
    checks += 1;
    failures += (unsigned)mismatch;
    control_failures += (unsigned)(mismatch && control);
}
int main(void)
{
    puts("case\tcontext\tcontrol\tmismatch\tactual\texpected\tactual_binary80\texpected_binary80");
#define CASE(name, expression, expected, control) \
    observe(#name, "scalar", scalar_##name, expected, control); \
    observe(#name, "array", array_##name[0], expected, control); \
    observe(#name, "record", record_##name.value, expected, control); \
    observe(#name, "local_static", *local_static_##name(), expected, control); \
    observe(#name, "automatic", automatic_##name(), expected, control); \
    observe(#name, "double_static", (long double)double_##name, (long double)(double)(expected), control);
#include "cases.def"
#undef CASE
    fprintf(stderr, "checks=%u failures=%u control_failures=%u\n", checks, failures, control_failures);
    return failures != 0;
}
