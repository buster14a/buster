static const __float128 q[] = { __builtin_inff128(), -__builtin_nanf128("5"), __builtin_nansf128("") };
static _Float16 h = __builtin_nansf16("1");
struct S { double d; __float128 q; _Float16 h; } s = { __builtin_nans(""), __builtin_huge_valf128(), __builtin_inff16() };
const __float128* p = q;
