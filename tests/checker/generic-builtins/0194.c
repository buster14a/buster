struct S { long n; int fam[] __attribute__((counted_by(n))); };
struct P { int k; char *p __attribute__((sized_by(k))); int * __attribute__((counted_by(k))) q; void *v __attribute__((counted_by_or_null(k))); };
struct N { int k; struct { int x; char fam[] __attribute__((counted_by(k))); }; };
struct Q { int n; int fam[]; };
_Static_assert(_Generic(__builtin_counted_by_ref(((struct S *)0)->fam), long *: 1, default: 0), "");
int f(struct S *s, struct P *p, struct N *m, struct Q *q)
{
*__builtin_counted_by_ref(s->fam) = 3;
*__builtin_counted_by_ref(p->q) = 4;
*__builtin_counted_by_ref(m->fam) = 5;
typeof(__builtin_counted_by_ref(p->p)) v = (void *)__builtin_counted_by_ref(q->fam);
(void)v;
return (int)sizeof(*__builtin_counted_by_ref((s)->fam)) + (int)__builtin_dynamic_object_size(s->fam, 0);
}
