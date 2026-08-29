#ifndef EXPECTED_STDC_VERSION
#error EXPECTED_STDC_VERSION must be defined
#endif

#ifndef EXPECTED_GNU
#error EXPECTED_GNU must be defined
#endif

#if __STDC_VERSION__ != EXPECTED_STDC_VERSION
#error incorrect __STDC_VERSION__
#endif

/* __GNUC__ is not part of the dialect switch: it says which extensions the
   compiler implements, not which ones the dialect permits, and both reference
   compilers predefine it in every standard mode -- `clang -std=c99 -dM -E`
   reports `__GNUC__ 4` beside `__STRICT_ANSI__ 1`.  musl's <tgmath.h> is what
   made the difference visible; see tests/basic_c_type_generic_math.c.
   __STRICT_ANSI__ is the dialect switch, and only it flips here. */
#ifndef __GNUC__
#error every dialect must define __GNUC__
#endif

#if EXPECTED_GNU
#ifdef __STRICT_ANSI__
#error GNU dialect must not define __STRICT_ANSI__
#endif
#else
#ifndef __STRICT_ANSI__
#error strict C dialect must define __STRICT_ANSI__
#endif
#endif

typedef __CHAR8_TYPE__ test_char8;
#if __STDC_VERSION__ >= 202311L
#if !true || false
#error C23 boolean constants failed in preprocessing
#endif
static test_char8* utf8_text = u8"ok";
alignas(16) static int c23_aligned = 3;
thread_local int c23_thread_value = 5;
static bool c23_truth = true;
static_assert(alignof(int) == 4, "C23 keyword aliases");
#else
static char* utf8_text = u8"ok";
#endif

int main(void)
{
#if __STDC_VERSION__ >= 202311L
    bool local = false;
    int generic_truth = _Generic(true, bool: 1, default: 0);
    return utf8_text[0] == 'o' && c23_aligned == 3 && c23_thread_value == 5 && c23_truth && generic_truth == 1 && !local ? 0 : 1;
#else
    return utf8_text[0] == 'o' ? 0 : 1;
#endif
}
