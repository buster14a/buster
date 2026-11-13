struct S { char c; int i; };
void f(int n, struct S* p, volatile struct S* v, _Atomic(struct S)* a, int (*q)[n], int (*r)[3])
{
__builtin_clear_padding(p);
__builtin_clear_padding(v);
__builtin_clear_padding(a);
__builtin_clear_padding(q);
__builtin_clear_padding(r);
}
