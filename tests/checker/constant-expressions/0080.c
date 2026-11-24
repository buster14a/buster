typedef float vf __attribute__((vector_size(16)));
_Static_assert(__builtin_types_compatible_p(__typeof__((vf){0}), vf), "vector");
struct S { int x; };
_Static_assert(__builtin_types_compatible_p(typeof((struct S){0}), struct S), "record");
enum { N=sizeof(__typeof__((int[2]){0})) }; _Static_assert(N==2*sizeof(int), "array");
