#pragma once

#include <buster/float.h>

BUSTER_F_IMPL f32 floor_f32(f32 v)
{
    return __builtin_floorf(v);
}

BUSTER_F_IMPL f64 floor_f64(f64 v)
{
    return __builtin_floor(v);
}

BUSTER_F_IMPL f32 ceil_f32(f32 v)
{
    return __builtin_ceilf(v);
}

BUSTER_F_IMPL f64 ceil_f64(f64 v)
{
    return __builtin_ceil(v);
}

BUSTER_F_IMPL f32 sqrt_f32(f32 v)
{
    return __builtin_sqrtf(v);
}

BUSTER_F_IMPL f64 sqrt_f64(f64 v)
{
    return __builtin_sqrt(v);
}

BUSTER_F_IMPL f32 pow_f32(f32 x, f32 y)
{
    return __builtin_powf(x, y);
}

BUSTER_F_IMPL f64 pow_f64(f64 x, f64 y)
{
    return __builtin_pow(x, y);
}

BUSTER_F_IMPL f32 fmod_f32(f32 a, f32 b)
{
    return __builtin_fmodf(a, b);
}

BUSTER_F_IMPL f64 fmod_f64(f64 a, f64 b)
{
    return __builtin_fmod(a, b);
}

BUSTER_F_IMPL f32 cos_f32(f32 v)
{
    return __builtin_cosf(v);
}

BUSTER_F_IMPL f64 cos_f64(f64 v)
{
    return __builtin_cos(v);
}

BUSTER_F_IMPL f32 acos_f32(f32 v)
{
    return __builtin_acosf(v);
}

BUSTER_F_IMPL f64 acos_f64(f64 v)
{
    return __builtin_acos(v);
}

BUSTER_F_IMPL f32 fabs_f32(f32 v)
{
    return __builtin_fabsf(v);
}

BUSTER_F_IMPL f64 fabs_f64(f64 v)
{
    return __builtin_fabs(v);
}

BUSTER_F_IMPL f32 round_f32(f32 v)
{
    return __builtin_roundf(v);
}

BUSTER_F_IMPL f64 round_f64(f64 v)
{
    return __builtin_round(v);
}
