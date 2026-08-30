// The companion assembly stores a function's address with `.long`, which
// is R_X86_64_32: the zero-extended absolute form -fno-pic small-model
// foreign objects use for every address literal (CPython's clang-built
// perf_jit_trampoline.o is where it surfaced).  Both executable linkers
// share the arm that applies it; calling through the stored address
// proves the slot resolved to the function rather than to the anonymous
// relocation refusal it used to be.
extern unsigned const basic_asm_abs32_slot;

int main(void)
{
    int (*function)(void) = (int (*)(void))(unsigned long)basic_asm_abs32_slot;
    return function() == 7 ? 0 : 1;
}
