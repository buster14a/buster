extern unsigned long long xcr_live(unsigned, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long);
int main(void)
{
    unsigned a,b,c,d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    int result = 77;
    if (c & (1u<<27))
    {
        result = 0;
        unsigned low,high;
        __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        unsigned long long expected = (unsigned long long)low | ((unsigned long long)high <<32);
        for (unsigned long long n=0; n<32; ++n)
        {
            unsigned long long v=0xfffffff000000000ull+n;
            result |= xcr_live(0,v,v+1,v+2,v+3,v+4,v+5,v+6) != expected + v + 3*(v+1) + 5*(v+2) + 7*(v+3) + 11*(v+4) + 13*(v+5) + 17*(v+6);
        }
    }
    return result;
}
