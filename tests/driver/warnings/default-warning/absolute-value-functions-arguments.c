int abs(int); long labs(long); double fabs(double); float fabsf(float);
typedef unsigned long size_type; typedef long L; enum E { A, B };
size_type s; const unsigned cu; L lv; enum E e; _Bool bo; unsigned char uc; char ch; __int128 i128; long double ld; float fl; double d; _Complex double cd; int *p; int arr[2]; void fn(void);
void g(void) {
(void)__builtin_abs(cu); (void)__builtin_abs(lv); (void)__builtin_abs(d); (void)__builtin_fabs(ch); (void)__builtin_fabsf(d); (void)__builtin_labs(ld);
(void)abs(s); (void)abs(lv); (void)abs(d); (void)fabs(ch); (void)fabsf(ld); (void)abs(e); (void)abs(bo); (void)abs(uc); (void)abs(i128); (void)fabs(fl);
(void)__builtin_abs(cd); (void)__builtin_cabs(d); (void)__builtin_llabs(ch); (void)(abs)(lv); (void)abs((L)1); (void)abs(1.0f);
}
static long llabs(long long x) { return x; }
void h(void) { (void)abs(1LL); }
