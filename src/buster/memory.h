#pragma once
#include <buster/base.h>

template <typename T>
BUSTER_F_DECL bool slice_compare(Slice<T> a, Slice<T> b);
BUSTER_F_DECL bool memory_compare(const void* a, const void* b, u64 count);
