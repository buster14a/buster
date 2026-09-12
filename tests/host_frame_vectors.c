#include "frame_vectors.h"
#include <string.h>

#define FRAME_VECTOR_HOST(name, type) \
    type frame_vector_host_##name(type value) { return value; }
FRAME_VECTOR_TYPES(FRAME_VECTOR_HOST)
#undef FRAME_VECTOR_HOST

int main(void)
{
    int failed = 0;
#define FRAME_VECTOR_CHECK(name, type) \
    { \
        type left = {0}, right = {0}; \
        for (unsigned index = 0; index < sizeof(type) / sizeof(left[0]); index += 1) \
        { \
            left[index] = index + 3; \
            right[index] = index + 71; \
        } \
        type (*volatile call)(type, type, int) = frame_vector_##name; \
        for (int choose = 0; choose < 5; choose += 1) \
        { \
            type expected = choose < 3 ? left : right; \
            type direct = frame_vector_##name(left, right, choose); \
            type indirect = call(left, right, choose); \
            failed |= memcmp(&direct, &expected, sizeof(type)) != 0; \
            failed |= memcmp(&indirect, &expected, sizeof(type)) != 0; \
        } \
        type reverse = frame_vector_call_##name(right); \
        failed |= memcmp(&reverse, &right, sizeof(type)) != 0; \
    }
    FRAME_VECTOR_TYPES(FRAME_VECTOR_CHECK)
#undef FRAME_VECTOR_CHECK
    return failed;
}
