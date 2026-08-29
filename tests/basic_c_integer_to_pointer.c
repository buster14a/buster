// Converting an integer to a pointer (C11 6.3.2.3p5) when the integer is
// narrower than a pointer.  The conversion is on the integer's *value*, so a
// signed operand sign-extends: `(void *)-1` is the all-ones pointer, not the
// low half of one.  Every backend here lowers the IR's INTEGER_TO_POINTER as a
// plain register copy, and LLVM's own inttoptr zero-extends a narrower
// operand, so the widening has to happen before the conversion or the top half
// of the pointer is whatever the 32-bit result left behind -- zero.
//
// This was not a hypothetical.  musl's `MAP_FAILED` is `((void *) -1)`, and
// with the high half cleared no caller of `mmap` could ever see a failure:
// libc-test's `t_vmfill` mapped memory until the kernel refused and then
// looped forever, which is what hung `regression/malloc-oom`,
// `regression/malloc-brk-fail`, `regression/setenv-oom` and
// `regression/pthread_create-oom` against a Buster-built musl.
//
// The comparisons are written against `unsigned long` rather than against the
// pointers, because a pointer comparison is exactly the thing under test: the
// fixture has to read the bits.
//
// Clang warns `-Wint-to-void-pointer-cast` on most of the lines below.  That
// warning *is* the construct under test, so it stays; nothing in the build
// compiles this file with the host compiler's warning set.

typedef unsigned long address_type;

// Out of line and not const, so nothing here folds the answer at the call
// site: the conversion has to be the one code generation emitted.
static int negative_one(void)
{
    return -1;
}

static int negative_two(void)
{
    return -2;
}

static unsigned int unsigned_all_ones(void)
{
    return 0xffffffffu;
}

static short negative_short(void)
{
    return -3;
}

static long long wide_negative_one(void)
{
    return -1;
}

int main(void)
{
    // The constant form, which is what a `MAP_FAILED` comparison is.
    if ((address_type)(void*)-1 != ~(address_type)0)
    {
        return 1;
    }
    // The same conversion with the integer arriving in a register.
    if ((address_type)(void*)negative_one() != ~(address_type)0)
    {
        return 2;
    }
    if ((address_type)(void*)negative_two() != ~(address_type)0 - 1)
    {
        return 3;
    }
    if ((address_type)(void*)negative_short() != ~(address_type)0 - 2)
    {
        return 4;
    }
    // An unsigned operand of the same width zero-extends, so its all-ones
    // value stays in the low half.  This is the half of the rule that was
    // already right, and it has to stay right.
    if ((address_type)(void*)unsigned_all_ones() != 0xffffffffu)
    {
        return 5;
    }
    // Already pointer width: no widening at all.
    if ((address_type)(void*)wide_negative_one() != ~(address_type)0)
    {
        return 6;
    }
    // The comparison a libc actually writes.
    void* failed = (void*)-1;
    if ((void*)negative_one() != failed)
    {
        return 7;
    }
    if ((void*)unsigned_all_ones() == failed)
    {
        return 8;
    }
    // A null pointer constant is the other integer-to-pointer conversion in
    // the language, and zero widens the same way from every integer type.
    void* null_pointer = 0;
    if (null_pointer != (void*)0L || (address_type)null_pointer != 0)
    {
        return 9;
    }
    // Through a conditional, whose null-pointer-constant arm converts on its
    // own path.
    int predicate = negative_one() < 0;
    void* selected = predicate ? failed : 0;
    if ((address_type)selected != ~(address_type)0)
    {
        return 10;
    }
    return 0;
}
