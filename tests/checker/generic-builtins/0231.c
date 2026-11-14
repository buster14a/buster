void f(_Atomic int* a, _Atomic int* b)
{
__builtin_trivially_relocate(a, b, 1);
}
