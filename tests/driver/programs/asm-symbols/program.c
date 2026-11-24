static __attribute__((used)) const char message[] = "naked";
static __attribute__((naked, noinline)) long twice(long x)
{
    __asm__("leaq (%rdi,%rdi), %rax\n\tret\nthrice:\n\tleaq (%rdi,%rdi,2), %rax\n\tret\n");
}
static long thrice_address(void) { long a; __asm__("leaq thrice(%%rip), %0" : "=r"(a)); return a; }
int main(void)
{
    long (*f)(long) = (long (*)(long))thrice_address();
    __asm__ volatile("leaq message(%%rip), %%rdi\n\tcall puts" ::: "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
    return (int)(twice(5) + f(7)) - 31;
}
