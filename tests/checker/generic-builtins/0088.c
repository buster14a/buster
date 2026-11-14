void *b[5];
void f(void)
{
if (0) __builtin_longjmp(b, 0);
}
