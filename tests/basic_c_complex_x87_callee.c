// The callee half of the COMPLEX_X87 result pair.  Compiled by one compiler
// and linked against a caller compiled by the other, so a disagreement about
// which x87 register holds which half is a wrong answer rather than a
// wrong-looking disassembly.  See basic_c_complex_x87_caller.c for what the
// classification is.
#if defined(__x86_64__) && !defined(_WIN32)

typedef long double _Complex ldc;

ldc ldc_compose(long double real, long double imaginary)
{
    ldc z;
    __real__ z = real;
    __imag__ z = imaginary;
    return z;
}

ldc ldc_conjugate(ldc z)
{
    __imag__ z = -__imag__ z;
    return z;
}

// The integers on either side move the memory argument off the first slot, so
// a classification that consumed the wrong number of stack bytes answers
// wrong on the way in as well as on the way out.
ldc ldc_bump(int before, ldc z, int after)
{
    if (before != 11 || after != 22)
    {
        return ldc_compose(0.0L, 0.0L);
    }
    __real__ z += (long double)before;
    __imag__ z += (long double)after;
    return z;
}

// Addition only: a complex multiply or divide is what the host compiler
// lowers to a compiler-runtime helper -- __mulxc3, __divxc3 -- and this link
// has no compiler runtime on it. The inline arithmetic is
// basic_c_complex_arithmetic.c's subject; this file's is the register pair.
ldc ldc_sum(ldc a, ldc b) { return a + b; }

// A result that came back from a call and goes straight out again: the pair
// is popped off the x87 stack and pushed back onto it inside one frame.
ldc ldc_relay(ldc z) { return ldc_conjugate(z); }

// The halves on their own, which is the argument direction the eighteen
// src/complex units share with musl's `creall` and `cimagl`.
long double ldc_real(ldc z) { return __real__ z; }
long double ldc_imaginary(ldc z) { return __imag__ z; }

// A discarded result still has to leave the x87 stack empty: an unbalanced
// pop shows up as a stack fault in whatever runs next, not here.
long double ldc_discard(ldc z)
{
    ldc_conjugate(z);
    ldc_conjugate(z);
    return __real__ z;
}

#endif
