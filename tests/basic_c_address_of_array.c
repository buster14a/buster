#include <string.h>

static unsigned read_little_endian_32(const void *bytes)
{
    const unsigned char *cursor = (const unsigned char *)bytes;
    return (unsigned)cursor[0] | ((unsigned)cursor[1] << 8) | ((unsigned)cursor[2] << 16) | ((unsigned)cursor[3] << 24);
}

static unsigned char global_bytes[4] = {0x78, 0x56, 0x34, 0x12};

// `&array` is the one lvalue whose address-of operand is already a place: an
// array is never loaded into a value, so taking its address has to use that
// place directly and produce a pointer to the array type.  LZ4IO reads its
// block header through `LZ4IO_readLE32(&blockInfo)`.
int main(void)
{
    unsigned char local_bytes[4] = {0x34, 0x12, 0, 0};
    unsigned char (*whole)[4] = &local_bytes;
    if (read_little_endian_32(&local_bytes) != 0x1234u) return 1;
    if (read_little_endian_32(&global_bytes) != 0x12345678u) return 2;
    if ((void *)whole != (void *)local_bytes) return 3;
    if (sizeof(*whole) != 4) return 4;
    if ((*whole)[1] != 0x12) return 5;
    // Pointer arithmetic on a pointer to array strides by the whole array.
    if ((void *)(&local_bytes + 1) != (void *)(local_bytes + 4)) return 6;
    unsigned char copy[4];
    memcpy(&copy, &local_bytes, sizeof(copy));
    if (read_little_endian_32(&copy) != 0x1234u) return 7;
    return 0;
}
