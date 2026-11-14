_Static_assert(__builtin_issignaling(__builtin_nans("")) && __builtin_issignaling(__builtin_nansf("1")) &&
__builtin_issignaling(__builtin_nansl("0x10")), "");
_Static_assert(__builtin_isnan(__builtin_nanf16("")) && __builtin_issignaling(__builtin_nansf16("")) &&
!__builtin_issignaling(__builtin_nanf16("3")) && __builtin_isinf(__builtin_inff16()) && __builtin_isinf(__builtin_huge_valf16()), "");
_Static_assert(__builtin_isnan(__builtin_nanf128("9")) && __builtin_issignaling(__builtin_nansf128("")) &&
__builtin_isinf(__builtin_inff128()) && __builtin_isinf(-__builtin_huge_valf128()) && __builtin_signbit(-__builtin_nansf128("")), "");
_Static_assert(!__builtin_issignaling((double)__builtin_nansf("")) && !__builtin_issignaling(-(float)__builtin_nans("")) &&
__builtin_issignaling(-__builtin_nans("2")) && __builtin_fpclassify(0, 1, 2, 3, 4, __builtin_nansf16("")) == 0, "");
int a[__builtin_isinf(__builtin_inff16()) + __builtin_isfpclass(__builtin_nansf128(""), 1) + 1];
