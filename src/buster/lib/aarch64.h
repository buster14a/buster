#pragma once

#include <buster/lib/target.h>
BUSTER_F_DECL CpuModel cpu_detect_model_aarch64(void);
BUSTER_F_DECL String8 aarch64_cpu_brand_string(char8* buffer, u64 capacity);
