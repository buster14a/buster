struct S { int n; int fam[] __attribute__((counted_by(n))); };
int *f(struct S *s)
{
return __builtin_counted_by_ref(s->fam);
}
