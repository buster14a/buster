long b[8];
int f(void)
{
return __builtin_setjmp(b);
}
