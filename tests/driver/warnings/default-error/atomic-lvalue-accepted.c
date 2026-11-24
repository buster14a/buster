// #3565: an _Atomic pointer, read, is its plain pointer. Each read below converts silently under every option that
// relaxes a conversion's diagnostics, as it does without one.
_Atomic(int *) ap;
const _Atomic(int *) cap;
int *volatile vp;
struct S
{
    _Atomic(char *) name;
    _Atomic(int *) list[2];
};
void take(int *, void *, _Bool);
int *file_scope_reader(void);

int *reads(struct S *s, _Atomic(int *) *pp, _Atomic(int *) local)
{
    int *a = ap;
    int *b = { ap };
    void *v = ap;
    _Bool t = ap;
    char *n = s->name;
    int *e = s->list[1];
    int *d = *pp;
    int *l = local;
    int *c = cap;
    int *w = vp;
    a = ap;
    v = s->name;
    t = *pp;
    e = ((void)0, ap);
    take(ap, ap, ap);
    take(s->list[0], s->name, local);
    (void)a;
    (void)b;
    (void)v;
    (void)t;
    (void)n;
    (void)e;
    (void)d;
    (void)l;
    (void)c;
    (void)w;
    return ap;
}

_Bool truth(struct S *s)
{
    return s->name;
}
