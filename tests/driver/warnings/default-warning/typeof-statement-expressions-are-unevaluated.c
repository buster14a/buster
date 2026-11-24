int *q;
void f(int x) {
    __typeof__(({ *q; x == 1; x = x; x / 0; 1; })) t = 0;
    typeof(({ (0, x); (void *)q; 1; })) v = 0;
    (void)t;
    (void)v;
}
