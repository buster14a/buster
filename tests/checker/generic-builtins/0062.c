void f(void* p, const void* q, unsigned long n)
{
__builtin_memcpy_inline(p, q, n);
}
