void f(int* a, long* b)
{
__builtin_trivially_relocate(a, b, 1);
}
