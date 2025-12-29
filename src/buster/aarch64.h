#pragma once

#include <buster/target.h>
BUSTER_DECL CpuModel cpu_detect_model_aarch64();

#if BUSTER_UNITY_BUILD
#include <buster/aarch64.c>
#endif
