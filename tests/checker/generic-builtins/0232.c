int x[4], y[4];
void f(void)
{
__builtin_trivially_relocate(x, y, 1);
}
