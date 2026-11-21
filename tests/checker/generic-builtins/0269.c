unsigned rotate(unsigned x)
{
    const int bits = 5;
    return __builtin_rotateleft32(x, __builtin_popcount(bits));
}

void* frame(void)
{
    const int bits = 5;
    return __builtin_frame_address(__builtin_popcount(bits) - 2);
}

unsigned long size(char* p)
{
    static const int bits = 5;
    return __builtin_object_size(p, (__builtin_popcount(bits) - 2));
}

void* aligned(char* p, unsigned offset)
{
    const int bits = 5;
    __builtin_prefetch(p, __builtin_popcount(bits) - 2, __builtin_popcount(bits));
    return __builtin_assume_aligned(p, __builtin_popcount(bits), offset);
}
