// Output includes a NUL byte and stderr; exit 37 is intentional, not a failure.
extern int printf(char const *, ...);
extern int puts(char const *);
extern int putchar(int);
#if defined(_WIN32)
extern int _write(int, void const *, unsigned);
#define OUTPUT_ERROR _write
#else
extern long write(int, void const *, unsigned long);
#define OUTPUT_ERROR write
#endif
volatile unsigned input = 12345;
unsigned checksum(unsigned x)
{
    unsigned a = 1, b = 3, c = x;
    for (unsigned i = 0; i < 23; i += 1)
    {
        unsigned t = a;
        a = b + (c ^ i);
        b = c ^ (t << (i & 7));
        c = t + (b >> (i & 3));
    }
    return a ^ b ^ c;
}
int main(void)
{
    unsigned value = checksum(input);
    printf("checksum=%u\n", value);
    putchar('a'); putchar(0); putchar('z'); putchar('\n');
    OUTPUT_ERROR(2, "stderr-observable\n", 18);
    return 37;
}
