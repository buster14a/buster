#include <setjmp.h>
#include <stdio.h>
static jmp_buf jump_buffer;
static void* builtin_buffer[5];
static int doubled(int x) { return 2 * x; }
static int tripled(int x) { return 3 * x; }
int (*volatile table[2])(int) = { doubled, tripled };
static int pick(int x) { switch (x & 7) { case 0: return 10; case 1: return 11; case 2: return 12; case 3: return 13; case 4: return 14; case 5: return 15; case 6: return 16; } return 17; }
static int jump(int x) { static void* labels[] = { &&even, &&odd }; goto *labels[x & 1]; even: return x / 2; odd: return 3 * x + 1; }
#ifndef NO_BUILTIN_SETJMP
__attribute__((noinline)) static void leave(void) { __builtin_longjmp(builtin_buffer, 1); }
__attribute__((noinline)) static int again(void) { if (__builtin_setjmp(builtin_buffer)) return 7; leave(); return 0; }
#else
static int again(void) { return (int)sizeof(builtin_buffer) - 33; }
#endif
int main(void)
{
    volatile int n = 0;
    int total = 0;
    if (setjmp(jump_buffer) == 0)
    {
        n = table[0](21) + table[1](1);
        longjmp(jump_buffer, 1);
    }
    for (int i = 0; i < 8; i += 1)
    {
        total += pick(i) + jump(i);
    }
    printf("%d %d %d\n", n, total, again());
    return 0;
}
