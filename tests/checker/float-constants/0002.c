_Static_assert(_Generic(__builtin_nans(""), double: 1, default: 0) && _Generic(__builtin_nansf(""), float: 1, default: 0) &&
_Generic(__builtin_nansl(""), long double: 1, default: 0), "");
_Static_assert(_Generic(__builtin_nanf16(""), _Float16: 1, default: 0) && _Generic(__builtin_nansf16(""), _Float16: 1, default: 0) &&
_Generic(__builtin_inff16(), _Float16: 1, default: 0) && _Generic(__builtin_huge_valf16(), _Float16: 1, default: 0), "");
_Static_assert(_Generic(__builtin_nanf128(""), __float128: 1, default: 0) && _Generic(__builtin_nansf128(""), __float128: 1, default: 0) &&
_Generic(__builtin_inff128(), __float128: 1, default: 0) && _Generic(__builtin_huge_valf128(), __float128: 1, default: 0), "");
