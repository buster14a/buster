typedef int v4 __attribute__((vector_size(16)));
struct S { int a; };
void f(int n) {
  __builtin_FILE(); __builtin_LINE(); __builtin_FUNCTION(); __builtin_COLUMN(); __builtin_FILE_NAME();
  __builtin_vectorelements(v4); __is_destructible(int); __is_bitwise_cloneable(int); __is_nothrow_destructible(int);
  n ? __builtin_LINE() : __builtin_LINE(); n, __builtin_FILE(); __builtin_LINE(), n;
  n ? __builtin_offsetof(struct S, a) : __builtin_offsetof(struct S, a); _Generic(n, int: __builtin_LINE()); _Generic(n, int: (n, __builtin_LINE()));
}
