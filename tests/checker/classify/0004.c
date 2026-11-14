typedef float v4 __attribute__((vector_size(16)));
typedef int i4 __attribute__((vector_size(16)));
typedef double v2 __attribute__((ext_vector_type(2)));
i4 f(v4 x)
{
return __builtin_isfpclass(x, 3);
}
_Static_assert(_Generic(__builtin_isfpclass((v4){ 0 }, 3), i4: 1, default: 0), "");
