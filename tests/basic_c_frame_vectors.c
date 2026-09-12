#include "frame_vectors.h"

// Each conditional joins complete vector values through canonical CFG edges.
// The host caller uses independent input and result images; the reverse call
// checks the Buster caller against an independently compiled callee too.
#define FRAME_VECTOR_DEFINE(name, type) \
    type frame_vector_##name(type left, type right, int choose) \
    { \
        type result = choose ? right : left; \
        for (int index = 0; index < 3; index += 1) \
        { \
            result = (index == choose) ? left : result; \
        } \
        return result; \
    } \
    type frame_vector_call_##name(type value) \
    { \
        return frame_vector_host_##name(value); \
    }
FRAME_VECTOR_TYPES(FRAME_VECTOR_DEFINE)
#undef FRAME_VECTOR_DEFINE
