void f(const void* p, unsigned long n)
{
__builtin_assume_dereferenceable(p, n);
}
