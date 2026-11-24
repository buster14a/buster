enum E1 { A1, B1 };
enum E2 { A2, B2 };
struct S { enum E1 a; enum E2 b; };
enum E1 f1(void);
enum E2 f2(void);
int g(struct S s, struct S* p, enum E1* q, enum E2 r[2], int i)
{
    int x = s.a + s.b;
    x += p->a == p->b;
    x += f1() | f2();
    x += (enum E1)i * (enum E2)i;
    x += *q - r[1];
    x += (i, *q) < (i, r[0]);
    return x + (i ? s.a : s.b);
}
