typedef int __attribute__((warn_unused_result)) WI;
typedef WI WJ;
typedef WI (*FP)(void);
typedef WI FN(void);
struct __attribute__((warn_unused_result)) S { int a; };
__attribute__((warn_unused_result)) int w(int);
__attribute__((const)) int c(int);
WI (*fp)(void);
WJ (*fj)(void);
WI *(*fpp)(void);
WI *pw(void);
FP fq;
FN *fn;
struct S (*sp)(void);
int (*plain)(void);
void f(WI (*cb)(void), FP cb2) {
  fp(); (*fp)(); (fp)(); fj(); fpp(); pw(); fq(); fn(); sp(); plain(); cb(); cb2(); (*w)(1); (**c)(1);
}
