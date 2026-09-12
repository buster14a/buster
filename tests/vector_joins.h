#pragma once
#include "frame_vectors.h"

#define VECTOR_JOIN_DECLARE(name, type) \
    type vector_join_##name(type first, type second, type third, unsigned count, unsigned choose); \
    type vector_join_host_##name(type value);
FRAME_VECTOR_TYPES(VECTOR_JOIN_DECLARE)
#undef VECTOR_JOIN_DECLARE
