typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
v4i f(v4f v)
{
return __builtin_isfpclass(v, 0x108);
}
