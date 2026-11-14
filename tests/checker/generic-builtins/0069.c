typedef int A __attribute__((ext_vector_type(2)));typedef int B __attribute__((ext_vector_type(4)));
_Static_assert(__builtin_vectorelements((A){0}, (B){0}) == 4 && __builtin_vectorelements((B){0}, (A){0}) == 2, "");
unsigned long f(A a, B b)
{
return __builtin_vectorelements(a, b) * 10 + __builtin_vectorelements(b, a, a);
}
