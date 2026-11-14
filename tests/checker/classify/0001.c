_Static_assert(__builtin_isnormal(1.0) && !__builtin_isnormal(0x1p-1030) && __builtin_issubnormal(0x1p-1030), "");
_Static_assert(__builtin_iszero(-0.0f) && !__builtin_issignaling(__builtin_nanf("")) && __builtin_isfpclass(-1.0, 8), "");
_Static_assert(__builtin_isnormal(0x1p-16382L) && __builtin_issubnormal(0x1p-16383L) && __builtin_isfpclass(-0.0L, 32), "");
_Static_assert(__builtin_signbitf(-0.0) && !__builtin_signbitl(1) && __builtin_signbitl(-__builtin_infl()), "");
int a[__builtin_isfpclass(-2.0, 8) + 1];
int f(double x)
{
switch (1)
{
case __builtin_isnormal(1.0):
return 1;
}
return __builtin_issignaling(x);
}
