// CPython's configure probe for HAVE_GCC_ASM_FOR_X87, kept as a running
// fixture: `fnstcw %0` stores the x87 control word through a memory operand
// and `fldcw %0` loads it back, and neither touches a stack position or a
// general register -- the control word and the one `m` operand are the whole
// effect.  The answers are checked rather than only assembled, because a
// store that hit the wrong slot still exits cleanly: the precision-control
// field is moved from its power-up value and read back, so a control word
// that never reached the FPU -- or a read that never reached memory --
// returns the wrong bits.
//
// Rounding and precision control are restored before the checks return, so
// the fixture leaves the FPU the way it found it whatever the verdict.
int main(void)
{
    unsigned short original = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(original));
    // Power-up control word: round-to-nearest, 64-bit precision, all
    // exceptions masked.  A hosted process on x86-64 Linux starts there.
    if (original != 0x037f)
    {
        return 1;
    }
    unsigned short modified = (unsigned short)(original & ~0x0300u);
    __asm__ __volatile__("fldcw %0" : : "m"(modified));
    unsigned short read_back = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(read_back));
    __asm__ __volatile__("fldcw %0" : : "m"(original));
    if (read_back != modified)
    {
        return 2;
    }
    unsigned short restored = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(restored));
    return restored == original ? 0 : 3;
}
