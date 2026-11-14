void *b[5];
void f(int n)
{
__builtin_longjmp(b, n);
}
