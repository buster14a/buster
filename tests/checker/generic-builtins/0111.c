struct S { int a[4]; short b; };
void g(void);
int* f(struct S* s, int x)
{
__builtin_preserve_access_index(g());
_Static_assert(_Generic(__builtin_preserve_access_index(s->b), short: 1, default: 0), "");
return __builtin_preserve_access_index(s->a) + *__builtin_preserve_access_index(&x);
}
