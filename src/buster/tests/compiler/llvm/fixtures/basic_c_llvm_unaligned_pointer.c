int packed_first = 13;
int packed_second = 29;
struct __attribute__((packed)) Unaligned
{
    unsigned char marker;
    int *pointer;
    int tag;
};
struct Unaligned unaligned[2] = {{1, &packed_first, 5}, {2, &packed_second, 7}};
