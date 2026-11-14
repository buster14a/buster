void f(void* a, void* b)
{
__builtin_trivially_relocate(a, b, 1);
}
