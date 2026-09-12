#include "vector_joins.h"
#include <string.h>

#define VECTOR_JOIN_HOST(name, type) \
    type vector_join_host_##name(type value) { return value; }
FRAME_VECTOR_TYPES(VECTOR_JOIN_HOST)
#undef VECTOR_JOIN_HOST

int main(void)
{
    int failed = 0;
#define VECTOR_JOIN_CHECK(name, type) \
    { \
        type images[3] = {0}; \
        for (unsigned image = 0; image < 3; image += 1) \
        { \
            for (unsigned lane = 0; lane < sizeof(type) / sizeof(images[0][0]); lane += 1) \
            { \
                images[image][lane] = 13 + 31 * image + lane; \
            } \
        } \
        type (*volatile call)(type, type, type, unsigned, unsigned) = vector_join_##name; \
        for (unsigned count = 0; count < 11; count += 1) \
        { \
            for (unsigned choose = 0; choose < 3; choose += 1) \
            { \
                type expected = images[(count + choose) % 3]; \
                struct { unsigned char before[16]; type value; unsigned char after[16]; } output; \
                memset(&output, 0xa5, sizeof(output)); \
                output.value = call(images[0], images[1], images[2], count, choose); \
                failed |= memcmp(&output.value, &expected, sizeof(type)) != 0; \
                for (unsigned byte = 0; byte < 16; byte += 1) \
                { \
                    failed |= output.before[byte] != 0xa5 || output.after[byte] != 0xa5; \
                } \
            } \
        } \
    }
    FRAME_VECTOR_TYPES(VECTOR_JOIN_CHECK)
#undef VECTOR_JOIN_CHECK
    return failed;
}
