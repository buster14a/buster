void f(const int* a, const int* b)
{
__builtin_trivially_relocate(a, b, 1);
}
