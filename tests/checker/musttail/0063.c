long g(long a, long b)
{
__attribute__((musttail)) return __builtin_expect(a, b);
}
