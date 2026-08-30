// C11 6.10.1p4: conditional-inclusion arithmetic runs in intmax_t, or
// uintmax_t where an operand is unsigned.  An all-unsigned evaluator read
// `#if -1 < 0` as false, which is how CPython's
// `static_assert(_Py_IS_TYPE_SIGNED(pid_t))` failed.  The same evaluator
// answers _Static_assert through the parse-side retokenizer, so both
// spellings are pinned, with the unsigned half of every operator that
// consults the flag.
#if !(-1 < 0)
#error "signed comparison"
#endif
#if !(0xFFFFFFFFFFFFFFFFu > 0)
#error "unsigned comparison"
#endif
#if !(1u - 2u > 0)
#error "unsigned subtraction wraps"
#endif
#if !((-1) >> 1 == -1)
#error "arithmetic right shift"
#endif
#if !(7 / -2 == -3 && -7 % 2 == -1)
#error "signed division"
#endif
#if !((1 ? -1 : 0u) == -1)
#error "conditional carries the arms"
#endif

_Static_assert((int)-1 < 0, "cast negative compares signed");
_Static_assert(-1 < 0, "literal negative compares signed");

int main(void)
{
    return 0;
}
