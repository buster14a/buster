#pragma once

#include <buster/lib/base.h>

BUSTER_F_DECL u64 align_forward(u64 n, u64 a);
BUSTER_F_DECL bool is_aligned(u64 n, u64 alignment);
BUSTER_F_DECL u64 next_power_of_two(u64 n);
BUSTER_F_DECL u8 trailing_zeroes_u32(u32 n);
BUSTER_F_DECL u8 trailing_zeroes_u64(u64 n);
BUSTER_F_DECL u8 leading_zeroes_u32(u32 n);
BUSTER_F_DECL u8 leading_zeroes_u64(u64 n);
