#pragma once

#include <buster/float.h>

#if defined(__has_builtin) && __has_builtin(__builtin_floorf)
#define BUILTIN_FLOORF(x) __builtin_floorf(x)
#else
#include <math.h>
#define BUILTIN_FLOORF(x) floorf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_floor)
#define BUILTIN_FLOOR(x) __builtin_floor(x)
#else
#include <math.h>
#define BUILTIN_FLOOR(x) floor(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_ceilf)
#define BUILTIN_CEILF(x) __builtin_ceilf(x)
#else
#include <math.h>
#define BUILTIN_CEILF(x) ceilf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_ceil)
#define BUILTIN_CEIL(x) __builtin_ceil(x)
#else
#include <math.h>
#define BUILTIN_CEIL(x) ceil(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_sqrtf)
#define BUILTIN_SQRTF(x) __builtin_sqrtf(x)
#else
#include <math.h>
#define BUILTIN_SQRTF(x) sqrtf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_sqrt)
#define BUILTIN_SQRT(x) __builtin_sqrt(x)
#else
#include <math.h>
#define BUILTIN_SQRT(x) sqrt(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_powf)
#define BUILTIN_POWF(x, y) __builtin_powf(x, y)
#else
#include <math.h>
#define BUILTIN_POWF(x, y) powf(x, y)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_pow)
#define BUILTIN_POW(x, y) __builtin_pow(x, y)
#else
#include <math.h>
#define BUILTIN_POW(x, y) pow(x, y)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_fmodf)
#define BUILTIN_FMODF(x, y) __builtin_fmodf(x, y)
#else
#include <math.h>
#define BUILTIN_FMODF(x, y) fmodf(x, y)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_fmod)
#define BUILTIN_FMOD(x, y) __builtin_fmod(x, y)
#else
#include <math.h>
#define BUILTIN_FMOD(x, y) fmod(x, y)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_cosf)
#define BUILTIN_COSF(x) __builtin_cosf(x)
#else
#include <math.h>
#define BUILTIN_COSF(x) cosf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_cos)
#define BUILTIN_COS(x) __builtin_cos(x)
#else
#include <math.h>
#define BUILTIN_COS(x) cos(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_acosf)
#define BUILTIN_ACOSF(x) __builtin_acosf(x)
#else
#include <math.h>
#define BUILTIN_ACOSF(x) acosf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_acos)
#define BUILTIN_ACOS(x) __builtin_acos(x)
#else
#include <math.h>
#define BUILTIN_ACOS(x) acos(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_fabsf)
#define BUILTIN_FABSF(x) __builtin_fabsf(x)
#else
#include <math.h>
#define BUILTIN_FABSF(x) fabsf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_fabs)
#define BUILTIN_FABS(x) __builtin_fabs(x)
#else
#include <math.h>
#define BUILTIN_FABS(x) fabs(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_roundf)
#define BUILTIN_ROUNDF(x) __builtin_roundf(x)
#else
#include <math.h>
#define BUILTIN_ROUNDF(x) roundf(x)
#endif

#if defined(__has_builtin) && __has_builtin(__builtin_round)
#define BUILTIN_ROUND(x) __builtin_round(x)
#else
#include <math.h>
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
