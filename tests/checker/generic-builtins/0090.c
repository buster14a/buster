char *b[5];
int f(void)
{
return __builtin_setjmp(b);
}
