_Atomic double a;
_Complex double f(void)
{
return __builtin_complex(a, 1.0);
}
