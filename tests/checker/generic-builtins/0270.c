void fetch(char* p)
{
    const int bits = 5;
    __builtin_prefetch(p, bits - 5);
}
