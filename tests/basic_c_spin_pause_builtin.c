// Clang's emmintrin.h declares `void _mm_pause(void);` and never defines
// it -- the compiler owns the definition -- so mimalloc's SSE2 spin loop
// (mi_atomic_yield, reached through every CPython allocation) left an
// unresolved _mm_pause behind unless the name is a builtin.  The pause is
// a hint: the observable contract is only that evaluation around it stays
// ordered.
#include <emmintrin.h>

int main(void)
{
    volatile int spins = 0;
    for (int index = 0; index < 3; index += 1)
    {
        spins += 1;
        _mm_pause();
    }
    __builtin_ia32_pause();
    if (spins != 3)
    {
        return 1;
    }
    return 0;
}
