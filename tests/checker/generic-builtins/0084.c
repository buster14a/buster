void *b[5];
void f(void)
{
__builtin_longjmp(b, 1);
}
int g(void)
{
return __builtin_setjmp(b);
}
_Static_assert(_Generic(__builtin_setjmp(b), int: 1, default: 0), "");
