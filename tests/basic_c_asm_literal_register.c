// A deliberately refused inline-assembly template. The refusal is by design --
// a literal general register in a template is one the emitter could also hand
// to an operand, so a generic input allocated in RAX would be silently
// overwritten -- but it used to be reported as "C code generation failed with
// error 2 ... opcode 37, operation 73", which names nothing a programmer can
// act on (issue #831). The driver test compiles this and asserts the
// diagnostic names the rule and the register, the way the frontend's refusals
// name themselves. CPython's configure reaches the same refusal through its
// HAVE_GCC_ASM_FOR_X64 probe.
//
// This file is never expected to compile. Do not add it to a fixture list that
// requires success.

int main(void)
{
    unsigned int slot;
    __asm__ __volatile__("movl %%eax, %0" : "=m"(slot));
    return 0;
}
