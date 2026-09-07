_Alignas(8192) int initialized = 7;
_Alignas(8192) int zero_filled;

#ifdef HOSTED_ALIGNMENT
int puts(const char*);
#endif

int main(void)
{
#ifdef HOSTED_ALIGNMENT
    puts("hosted alignment");
#endif
    zero_filled = initialized;
    return ((unsigned long)&initialized & 8191ul) != 0 ||
           ((unsigned long)&zero_filled & 8191ul) != 0 || zero_filled != 7;
}
