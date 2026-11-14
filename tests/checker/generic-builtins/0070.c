typedef int A __attribute__((ext_vector_type(2)));
unsigned long f(A a)
{
return __builtin_vectorelements(int, a);
}
