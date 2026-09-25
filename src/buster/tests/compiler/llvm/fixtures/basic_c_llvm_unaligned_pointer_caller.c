extern int packed_first;
extern int packed_second;
struct __attribute__((packed)) Unaligned
{
    unsigned char marker;
    int *pointer;
    int tag;
};
extern struct Unaligned unaligned[2];

int main(void)
{
    return sizeof(struct Unaligned) != 13 ||
           (char*)&unaligned[0].pointer - (char*)&unaligned[0] != 1 ||
           (char*)&unaligned[1] - (char*)&unaligned[0] != 13 ||
           unaligned[0].marker != 1 || unaligned[1].marker != 2 ||
           unaligned[0].pointer != &packed_first || unaligned[1].pointer != &packed_second ||
           *unaligned[0].pointer + unaligned[0].tag != 18 ||
           *unaligned[1].pointer + unaligned[1].tag != 36;
}
