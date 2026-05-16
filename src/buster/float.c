#pragma once

#include <buster/float.h>

#if __has_builtin(__builtin_floorf)
#define BUILTIN_FLOORF(x) __builtin_floorf(x)
#define BUILTIN_FLOOR(x) __builtin_floor(x)
#define BUILTIN_CEILF(x) __builtin_ceilf(x)
#define BUILTIN_CEIL(x) __builtin_ceil(x)
#define BUILTIN_SQRTF(x) __builtin_sqrtf(x)
#define BUILTIN_SQRT(x) __builtin_sqrt(x)
#define BUILTIN_POWF(x, y) __builtin_powf(x, y)
#define BUILTIN_POW(x, y) __builtin_pow(x, y)
#define BUILTIN_FMODF(x, y) __builtin_fmodf(x, y)
#define BUILTIN_FMOD(x, y) __builtin_fmod(x, y)
#define BUILTIN_COSF(x) __builtin_cosf(x)
#define BUILTIN_COS(x) __builtin_cos(x)
#define BUILTIN_ACOSF(x) __builtin_acosf(x)
#define BUILTIN_ACOS(x) __builtin_acos(x)
#define BUILTIN_FABSF(x) __builtin_fabsf(x)
#define BUILTIN_FABS(x) __builtin_fabs(x)
#define BUILTIN_ROUNDF(x) __builtin_roundf(x)
#define BUILTIN_ROUND(x) __builtin_round(x)
#else
#include <math.h>
#define BUILTIN_FLOORF(x) floorf(x)
#define BUILTIN_FLOOR(x) floor(x)
#define BUILTIN_CEILF(x) ceilf(x)
#define BUILTIN_CEIL(x) ceil(x)
#define BUILTIN_SQRTF(x) sqrtf(x)
#define BUILTIN_SQRT(x) sqrt(x)
#define BUILTIN_POWF(x, y) powf(x, y)
#define BUILTIN_POW(x, y) pow(x, y)
#define BUILTIN_FMODF(x, y) fmodf(x, y)
#define BUILTIN_FMOD(x, y) fmod(x, y)
#define BUILTIN_COSF(x) cosf(x)
#define BUILTIN_COS(x) cos(x)
#define BUILTIN_ACOSF(x) acosf(x)
#define BUILTIN_ACOS(x) acos(x)
#define BUILTIN_FABSF(x) fabsf(x)
#define BUILTIN_FABS(x) fabs(x)
#define BUILTIN_ROUNDF(x) roundf(x)
#define BUILTIN_ROUND(x) round(x)
#endif

f32 floor_f32(f32 v)
{
    return BUILTIN_FLOORF(v);
}

f64 floor_f64(f64 v)
{
    return BUILTIN_FLOOR(v);
}

f32 ceil_f32(f32 v)
{
    return BUILTIN_CEILF(v);
}

f64 ceil_f64(f64 v)
{
    return BUILTIN_CEIL(v);
}

f32 sqrt_f32(f32 v)
{
    return BUILTIN_SQRTF(v);
}

f64 sqrt_f64(f64 v)
{
    return BUILTIN_SQRT(v);
}

f32 pow_f32(f32 x, f32 y)
{
    return BUILTIN_POWF(x, y);
}

f64 pow_f64(f64 x, f64 y)
{
    return BUILTIN_POW(x, y);
}

f32 fmod_f32(f32 a, f32 b)
{
    return BUILTIN_FMODF(a, b);
}

f64 fmod_f64(f64 a, f64 b)
{
    return BUILTIN_FMOD(a, b);
}

f32 cos_f32(f32 v)
{
    return BUILTIN_COSF(v);
}

f64 cos_f64(f64 v)
{
    return BUILTIN_COS(v);
}

f32 acos_f32(f32 v)
{
    return BUILTIN_ACOSF(v);
}

f64 acos_f64(f64 v)
{
    return BUILTIN_ACOS(v);
}

f32 fabs_f32(f32 v)
{
    return BUILTIN_FABSF(v);
}

f64 fabs_f64(f64 v)
{
    return BUILTIN_FABS(v);
}

f32 round_f32(f32 v)
{
    return BUILTIN_ROUNDF(v);
}

f64 round_f64(f64 v)
{
    return BUILTIN_ROUND(v);
}
