#pragma once

#include <buster/base.h>
#include <buster/target.h>

typedef struct CpuId CpuId;
struct CpuId
{
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

BUSTER_F_DECL CpuModel cpu_detect_model_x86_64(void);
