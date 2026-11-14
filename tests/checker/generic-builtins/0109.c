typedef float V __attribute__((vector_size(16)));
V f(V v, _Complex float z)
{
return __arithmetic_fence(v) + __real__ __arithmetic_fence(z);
}
