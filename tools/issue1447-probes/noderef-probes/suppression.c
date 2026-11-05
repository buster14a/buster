#define ND __attribute__((noderef))
struct S { int value; int values[3]; int *pointer; struct S *next; };
void sink(int);
void suppression(int ND *p, int ND **inner, int *ND *outer, struct S ND *s, int i)
{
    (void)*p;
    *p = i;
    ++*p;
    sink(p[i]);
    (void)&*p;
    (void)&p[i];
    (void)*&p;
    (void)sizeof *p;
    (void)__alignof__(*p);
    __typeof__(*p) local = i;
    (void)_Generic(*p, int: 1);
    (void)__builtin_choose_expr(0, *p, 1);
    (void)(0 ? *p : 1);
    (void)*(&*p);
    (void)s->value;
    (void)(*s).value;
    (void)&s->value;
    (void)&(*s).value;
    (void)&s->values[i];
    (void)&s->pointer[i];
    (void)&s->next->value;
    (void)&**inner;
    (void)&**outer;
    (void)*(&**inner);
    (void)sizeof s->pointer[i];
    (void)local;
}
