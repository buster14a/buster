// Host-compiler-only x86-64 SysV probe. The following float must occupy XMM0
// when the aggregate uses a GPR, or XMM1 when the aggregate uses XMM0.
// This avoids relying on an accidentally live unused register or a compiler
// version string; LLVM has changed this policy under an ABI compatibility flag.
struct unnamed_record { float lead; int : 20; };
extern int puts(char const* text);

__attribute__((naked, noinline)) unsigned int observe_first_sse(struct unnamed_record record, float following)
{
    __asm__("movd %xmm0, %eax\n\tret");
}

int main(void)
{
    struct unnamed_record record = {1.5f};
    unsigned int observed = observe_first_sse(record, 2.5f);
    int result = 0;
    if (observed == 0x3fc00000u)
    {
        puts("padding");
    }
    else if (observed == 0x40200000u)
    {
        puts("integer");
    }
    else
    {
        result = 1;
    }
    return result;
}
