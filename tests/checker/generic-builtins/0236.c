void f(int* a, int* b)
{
__builtin_trivially_relocate(a, b, (void*)0);
}
