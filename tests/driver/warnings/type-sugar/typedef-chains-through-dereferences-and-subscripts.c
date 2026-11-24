typedef int v4si __attribute__((vector_size(16)));
typedef v4si *pv;
typedef pv *pvp;
typedef void *VP;
typedef VP *VPP;
typedef int I;
typedef I *IP;
typedef IP IPA[2];
void f(pv p, pvp pp, VPP b, IPA a, IP *q, v4si *direct)
{
    (*p)++;
    p[1]--;
    ++**pp;
    (*pp)();
    (void)**b;
    a[0]();
    (*a[1])();
    (*q)();
    (**q)();
    (*direct)++;
}
