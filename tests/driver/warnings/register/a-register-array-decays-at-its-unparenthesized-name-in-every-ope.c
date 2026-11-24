struct I { int k; };
struct S { int m; int a[2]; struct I in; };
int g(int *);
int h(int p[2]);
void f(register int prm, register int parr[2]) {
    register int arr[3]; register struct S s; register struct S sa[2];
    int n1 = sizeof(arr);
    int n2 = sizeof arr[0];
    int n3 = *arr;
    int n4 = *(arr + 1);
    int n5 = g(arr);
    int *p6 = &arr[0];
    int *p7 = (arr);
    __typeof__(arr) t9;
    int *p10 = &s.in.k;
    int n11 = s.a[0];
    int n12 = sa[0].m;
    int *p13 = &prm;
    int *p14 = parr;
    int *p15 = &parr[0];
    int n16 = sizeof(&arr);
    int *p17 = 1 ? arr : 0;
    int n18 = (arr)[1];
    int n19 = 1[arr];
    int *p20 = (int *)arr;
    struct S cp = s;
    (void)n1;(void)n2;(void)n3;(void)n4;(void)n5;(void)p6;(void)p7;(void)t9;(void)p10;(void)n11;(void)n12;(void)p13;(void)p14;(void)p15;(void)n16;(void)p17;(void)n18;(void)n19;(void)p20;(void)cp;
}
