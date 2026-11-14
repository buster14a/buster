int f(double x)
{
return __builtin_isfpclass(x, 1023u) + __builtin_isfpclass(x, (char)3) + __builtin_isfpclass(x, (int)3.7) +
__builtin_isfpclass(x, sizeof(int)) + __builtin_isfpclass(x, (_Bool)0) + __builtin_isfpclass(x, (unsigned char)127);
}
