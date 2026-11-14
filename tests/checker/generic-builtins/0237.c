enum E { X };
typedef int V __attribute__((vector_size(16)));
void f(enum E* a, enum E* b, int (*c)[3], int (*d)[3], V* v, V* w)
{
__builtin_trivially_relocate(a, b, 1.5);
__builtin_trivially_relocate(c, d, 1);
__builtin_trivially_relocate(v, w, 1);
}
