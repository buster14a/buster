typedef int V __attribute__((vector_size(16)));
V f(V v)
{
return __arithmetic_fence(v);
}
