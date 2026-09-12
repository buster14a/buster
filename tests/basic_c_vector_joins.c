#include "vector_joins.h"

// Three simultaneous edge copies form a cycle. Values remain live across an
// independent callee, then a second join chooses each complete vector image.
#define VECTOR_JOIN_DEFINE(name, type) \
    type vector_join_##name(type first, type second, type third, unsigned count, unsigned choose) \
    { \
        for (unsigned index = 0; index < count; index += 1) \
        { \
            type saved = first; \
            first = second; \
            second = third; \
            third = saved; \
            first = vector_join_host_##name(first); \
        } \
        type result = choose == 0 ? first : choose == 1 ? second : third; \
        return result; \
    }
FRAME_VECTOR_TYPES(VECTOR_JOIN_DEFINE)
#undef VECTOR_JOIN_DEFINE
