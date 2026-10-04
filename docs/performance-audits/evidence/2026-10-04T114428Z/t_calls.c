struct VT { int (*fn)(void*, int); };
struct Obj { struct VT* vt; int n; };
int g(int);
int h(int);
int f(struct Obj* o, int x, int y)
{
    int r = g(h(x) + 1) * g(y) + o->vt->fn(o, x);
    r += o->n ? g(x) : h(y);
    return r;
}
