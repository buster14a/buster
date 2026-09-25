#define NEGATE(x) (-(x))
struct Cell { long double value; };
#define CASE(name, expression, expected, control) \
    long double scalar_##name = expression; \
    long double array_##name[1] = {expression}; \
    struct Cell record_##name = {expression}; \
    long double *local_static_##name(void) { static long double value = expression; return &value; } \
    long double automatic_##name(void) { long double value = expression; return value; } \
    double double_##name = expression;
#include "cases.def"
#undef CASE
