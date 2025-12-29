#pragma once

#include <buster/lib.h>
#include <buster/target.h>

STRUCT(CpuId)
{
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

BUSTER_DECL CpuModel cpu_detect_model_x86_64();

#if BUSTER_UNITY_BUILD
#include <buster/x86_64.c>
#endif
