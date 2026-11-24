#include <stdio.h>
struct S { long a, b, c; };
__attribute__((naked, noinline)) long read_chain(void) { __asm__("movq %r10, %rax\n\tret"); }
__attribute__((naked, noinline)) long chain_plus(long x) { __asm__("leaq (%r10,%rdi), %rax\n\tret"); }
__attribute__((naked, noinline)) long chain_sum7(long a, long b, long c, long d, long e, long f, long g) { __asm__("leaq (%rdi,%rsi), %rax\n\taddq %rdx, %rax\n\taddq %rcx, %rax\n\taddq %r8, %rax\n\taddq %r9, %rax\n\taddq 8(%rsp), %rax\n\taddq %r10, %rax\n\tret"); }
__attribute__((naked, noinline)) double chain_double(double x) { __asm__("cvtsi2sdq %r10, %xmm1\n\taddsd %xmm1, %xmm0\n\tret"); }
__attribute__((naked, noinline)) struct S chain_struct(void) { __asm__("movq %rdi, %rax\n\tmovq %r10, (%rdi)\n\tmovq $2, 8(%rdi)\n\tmovq $3, 16(%rdi)\n\tret"); }
__attribute__((naked, noinline)) long chain_variadic(int n, ...) { __asm__("movq %r10, %rax\n\taddq %rsi, %rax\n\tret"); }
static int evaluated[3];
static long (*pick(long (*f)(long)))(long) { evaluated[0] += 1; return f; }
static char* chain_of(char* p) { evaluated[1] += 1; return p; }
static long arg(long x) { evaluated[2] += 1; return x; }
static char buffer[64];
static void target(void) {}
__attribute__((noinline)) long t_direct(char* p) { return __builtin_call_with_static_chain(read_chain(), p); }
__attribute__((noinline)) long t_plus(long x, char* p) { return __builtin_call_with_static_chain(chain_plus(x), p); }
__attribute__((noinline)) long t_swap(char* x, long p) { return __builtin_call_with_static_chain(chain_plus(p), x); }
__attribute__((noinline)) long t_sum(char* p) { return __builtin_call_with_static_chain(chain_sum7(1, 2, 3, 4, 5, 6, 7), p); }
__attribute__((noinline)) long t_indirect(long (*f)(long), char* p, long x) { return __builtin_call_with_static_chain(f(x), p); }
__attribute__((noinline)) long t_indirect_r10(char* p, long (*f)(long), long x) { return __builtin_call_with_static_chain(f(x), p) + 1; }
__attribute__((noinline)) long t_variadic(char* p) { return __builtin_call_with_static_chain(chain_variadic(1, 30L), p); }
__attribute__((noinline)) long t_variadic_indirect(long (*g)(int, ...), char* p) { return __builtin_call_with_static_chain(g(1, 5L), p); }
__attribute__((noinline)) long t_loop(char* p, int n) { long s = 0; for (int i = 0; i < n; i++) s += __builtin_call_with_static_chain(chain_plus(i), p + i); return s; }
__attribute__((noinline)) long t_tail(long x, char* p) { __attribute__((musttail)) return __builtin_call_with_static_chain(chain_plus(x), p); }
int main(int argc, char** argv)
{
    (void)argv;
    char array[4];
    long k = argc;
    printf("%d %d %d\n", __builtin_call_with_static_chain(read_chain(), buffer) == (long)buffer, __builtin_call_with_static_chain(read_chain(), array) == (long)array, __builtin_call_with_static_chain(read_chain(), target) == (long)target);
    printf("%ld %ld %g\n", __builtin_call_with_static_chain(chain_plus(5 * k), (char*)100), __builtin_call_with_static_chain(chain_sum7(1, 2, 3, 4, 5, 6, 7), (char*)1000), __builtin_call_with_static_chain(chain_double(0.5), (char*)2));
    struct S s = __builtin_call_with_static_chain(chain_struct(), (char*)7);
    printf("%ld %ld %ld %ld\n", s.a, s.b, s.c, __builtin_call_with_static_chain(chain_variadic(1, 30L), (char*)12));
    long (*volatile f)(long) = chain_plus;
    long (*g)(int, ...) = chain_variadic;
    long q = __builtin_call_with_static_chain(pick(f)(arg(k)), chain_of((char*)40));
    printf("%ld %d%d%d %ld\n", q, evaluated[0], evaluated[1], evaluated[2], __builtin_call_with_static_chain(g(1, 5L), (char*)6));
    printf("%ld %ld %ld %ld %ld\n", t_direct((char*)9), t_plus(5, (char*)100), t_swap((char*)100, 5), t_sum((char*)1000), t_indirect(chain_plus, (char*)40, k));
    printf("%ld %ld %ld %ld %ld\n", t_indirect_r10((char*)40, chain_plus, k), t_variadic((char*)12), t_variadic_indirect(chain_variadic, (char*)6), t_loop((char*)10, 4), t_tail(5, (char*)300));
    return evaluated[0] != 1 || evaluated[1] != 1 || evaluated[2] != 1;
}
