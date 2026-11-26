#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { long a, b; } Pair;
typedef int v8si __attribute__((vector_size(32)));
static volatile int extra;
__attribute__((noinline)) void fill(char* p, int n) { memset(p, 'A', (size_t)n); }
__attribute__((noinline)) int plain(void) { char b[16]; fill(b, 16 + extra); return b[0]; }
__attribute__((noinline)) int aligned64(void) { _Alignas(64) char b[100]; fill(b, 100 + extra); return b[99]; }
__attribute__((noinline)) int vla(int n) { char b[n]; fill(b, n + 8 * extra); return b[0]; }
__attribute__((noinline)) int with_alloca(int n) { char* b = alloca(n); fill(b, n + 8 * extra); return b[0]; }
__attribute__((noinline)) Pair pair(void) { char b[16]; fill(b, 16 + extra); return (Pair){ b[0], 2 }; }
__attribute__((noinline)) long double ld(void) { char b[16]; fill(b, 16 + extra); return b[0] * 1.5L; }
int callee(int x);
__attribute__((noinline)) int callee(int x) { return x + 7; }
__attribute__((noinline)) int tail(int x) { char b[16]; fill(b, 16 + extra); __attribute__((musttail)) return callee(x + b[1]); }
__attribute__((noinline, preserve_most)) int preserving(int x) { char b[16]; fill(b, 16 + extra); return x + b[4]; }
__attribute__((noinline)) v8si vec(v8si a) { char b[16]; fill(b, 16 + extra); return a + b[5]; }
__attribute__((noinline)) int addressed(void) { long x; fill((char*)&x, 8 + extra); return (int)x; }
int main(int argc, char** argv)
{
    extra = argc > 1 ? 64 : 0;
    const int c = argc > 1 ? atoi(argv[1]) : -1;
    v8si v = { 1, 2, 3, 4, 5, 6, 7, 8 };
    if (c < 0)
    {
        Pair p = pair();
        printf("%d %d %d %d %ld %ld %d %d %d %d %d\n", plain(), aligned64(), vla(40), with_alloca(40), p.a, p.b, (int)ld(), tail(1),
               preserving(1), vec(v)[7], addressed());
        return 0;
    }
    const int r = c == 0 ? plain() : c == 1 ? aligned64() : c == 2 ? vla(40) : c == 3 ? with_alloca(40) : c == 4 ? (int)pair().a : c == 5 ? (int)ld() :
                  c == 6 ? tail(1) : c == 7 ? preserving(1) : c == 8 ? vec(v)[0] : addressed();
    printf("not detected %d\n", r);
    return 0;
}
