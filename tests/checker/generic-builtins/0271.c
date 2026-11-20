void fetch(char* p, unsigned bits)
{
    __builtin_prefetch(p, __builtin_popcount(bits));
}
