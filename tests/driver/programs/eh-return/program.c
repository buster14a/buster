#include <stdarg.h>
#include <stdio.h>
#include <string.h>
long seen[8];
long before;
void* target;
__attribute__((naked, noinline)) long run(long offset)
{
    __asm__(
        "pushq %rbx\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
        "movq $0x0b0b, %rbx\nmovq $0x0c0c, %r12\nmovq $0x0d0d, %r13\nmovq $0x0e0e, %r14\nmovq $0x0f0f, %r15\n"
        "subq $48, %rsp\n"
        "movq %rsp, before(%rip)\n"
        "leaq handler(%rip), %rsi\n"
        "movq $77, %rdx\nxorl %eax, %eax\n"
        "movq $6, 0(%rsp)\nmovq $7, 8(%rsp)\nmovq $8, 16(%rsp)\nmovq $9, 24(%rsp)\n"
        "call *target(%rip)\n"
        "movq %rbx, seen+16(%rip)\nmovq %r12, seen+24(%rip)\nmovq %r13, seen+32(%rip)\nmovq %r14, seen+40(%rip)\n"
        "movq %r15, seen+48(%rip)\nmovq %rsp, seen+56(%rip)\n"
        "jmp 1f\n"
        "handler:\n"
        "movq %rax, seen+0(%rip)\nmovq %rdx, seen+8(%rip)\nmovq %rbx, seen+16(%rip)\nmovq %r12, seen+24(%rip)\n"
        "movq %r13, seen+32(%rip)\nmovq %r14, seen+40(%rip)\nmovq %r15, seen+48(%rip)\nmovq %rsp, seen+56(%rip)\n"
        "movq before(%rip), %rsp\nxorl %eax, %eax\n"
        "1:\naddq $48, %rsp\n"
        "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %rbx\nret\n");
}
static volatile long sink;
static volatile int pick;
__attribute__((noinline)) static long churn(long x) { sink = x; return x * 7; }
__attribute__((noinline)) void thrower(long offset, void* h)
{
    __builtin_unwind_init();
    long a = churn(offset), b = churn(a + 1), c = churn(b + 2), d = churn(c + 3), e = churn(d + 4), f = churn(e + 5);
    sink = a + b + c + d + e + f;
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x7a7a + offset;
    frame[-6] = 0x6d6d + offset;
    if (pick) { frame[-7] = 0x5151 + offset; __builtin_eh_return(offset, h); }
    __builtin_eh_return(offset, h);
}
__attribute__((noinline)) void vthrower(long offset, void* h, ...)
{
    __builtin_unwind_init();
    va_list ap;
    va_start(ap, h);
    long extra = va_arg(ap, long);
    va_end(ap);
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x2a2a + offset + extra;
    frame[-6] = 0x3b3b + churn(extra);
    if (pick) __builtin_eh_return(offset + extra - 77, h);
    __builtin_eh_return(offset, h);
}
__attribute__((noinline)) long ethrower(long offset, void* h)
{
    __builtin_unwind_init();
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x3c3c + offset;
    frame[-6] = 0x4d4d + offset;
    if (pick) return (long)sink * 2 + churn(offset) * (__builtin_eh_return(offset, h), 3);
    return churn(offset + 1) + (__builtin_eh_return(offset, (char*)h + 0), 1);
}
__attribute__((noinline)) long tailee(long offset, void* h) { sink = offset * 3; return offset + 1000 + (h != 0); }
__attribute__((noinline)) long mthrower(long offset, void* h)
{
    __builtin_unwind_init();
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x5e5e + offset;
    frame[-6] = 0x6f6f + churn(offset);
    if (pick) __builtin_eh_return(offset, h);
    __attribute__((musttail)) return tailee(offset, h);
}
static int variant;
struct S { double a; long b; };
struct Big { long a, b, c, d; };
typedef double V2 __attribute__((vector_size(16)));
__attribute__((noinline)) static double fchurn(long x) { sink = x; return x * 0.5; }
__attribute__((noinline)) static struct S schurn(long x) { sink = x + 100; return (struct S){ x * 0.25, x }; }
__attribute__((noinline)) static V2 vchurn(long x) { sink = x + 200; return (V2){ x * 0.5, 3.0 }; }
__attribute__((noinline)) long cthrower(long offset, void* h)
{
    __builtin_unwind_init();
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x1111 + offset + variant;
    frame[-6] = 0x2222 + offset;
    if (variant == 0) return (long)(fchurn(offset) + (sink + 1 ? (__builtin_eh_return(offset, h), 1.0) : 2.0));
    if (variant == 1) return (long)((long double)fchurn(offset + 1) * 3.0L + (sink + 1 && (__builtin_eh_return(offset, h), 1)));
    if (variant == 2) return schurn(offset).b + ({ struct S t = schurn(offset + 2); t.b >= 0 ? (__builtin_eh_return(offset, h), 0) : 9; });
    if (variant == 3) return (long)(vchurn(offset) * (__builtin_eh_return(offset, h), (V2){ 1.0, 2.0 }))[0];
    return (long)fchurn(offset) + ((void)sink, (__builtin_eh_return(offset, h), 5));
}
__attribute__((noinline)) long big_tailee(long offset, void* h, struct Big b) { sink = b.a + 10 * b.b + 100 * b.c + 1000 * b.d; return offset + 2000 + (h != 0); }
__attribute__((noinline)) long bthrower(long offset, void* h, struct Big b)
{
    __builtin_unwind_init();
    long* frame = __builtin_frame_address(0);
    frame[-7] = 0x7e7e + offset + b.a;
    frame[-6] = 0x8f8f + churn(b.d);
    if (pick) __builtin_eh_return(offset, h);
    __attribute__((musttail)) return big_tailee(offset, h, (struct Big){ b.d, b.c, b.b, b.a });
}
__attribute__((noinline)) V2 trapping(V2 a, int go) { return go ? vchurn(2) * (__builtin_trap(), (V2){ 1.0, 2.0 }) : a + 1.0; }
__attribute__((noinline)) void maybe(long offset, void* h, int go)
{
    __builtin_unwind_init();
    if (go) __builtin_eh_return(offset, h);
    sink = 5;
}
int main(void)
{
    void* const functions[6] = { (void*)thrower, (void*)vthrower, (void*)ethrower, (void*)mthrower, (void*)cthrower, (void*)bthrower };
    for (int k = 0; k < 6; k += 1)
    {
        target = functions[k];
        for (long offset = 0; offset <= 32; offset += 8)
        {
            memset(seen, 0, sizeof(seen));
            pick = (offset & 8) != 0;
            variant = (int)(offset / 8);
            long r = run(offset & ~8);
            printf("%d %ld rax=%lx rdx=%lx rbx=%lx r12=%lx r13=%lx r14=%lx r15=%lx sp=%ld sink=%ld\n", k, r, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6], seen[7] - before, sink);
        }
    }
    printf("trapping %g\n", trapping((V2){ 4.0, 5.0 }, 0)[1]);
    maybe(0, 0, 0);
    printf("normal return %ld\n", sink);
    return 0;
}
