void f(int* a)
{
int c[2];
__builtin_trivially_relocate(a, c, 1);
}
