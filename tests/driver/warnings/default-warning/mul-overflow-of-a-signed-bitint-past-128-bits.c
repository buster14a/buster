typedef _BitInt(200) S; typedef unsigned _BitInt(200) U;
int f1(S a, S b, S *r) { return __builtin_mul_overflow(a, b, r); }
int f2(int a, int b, S *r) { return __builtin_mul_overflow(a, b, r); }
int f3(U a, U b, U *r) { return __builtin_mul_overflow(a, b, r); }
int f4(U a, S b, U *r) { return __builtin_mul_overflow(a, b, r); }
int f5(U a, int b, U *r) { return __builtin_mul_overflow(a, b, r); }
int f6(_BitInt(129) a, int b, int *r) { return __builtin_mul_overflow(a, (b), r); }
int f7(__int128 a, __int128 b, _BitInt(256) *r) { return __builtin_mul_overflow(a, b, r); }
int f8(S a, S b, S *r) { return __builtin_add_overflow(a, b, r) + __builtin_sub_overflow(a, b, r); }
int f9(unsigned _BitInt(129) a, U *r) { return __builtin_mul_overflow(a, 3, r); }
