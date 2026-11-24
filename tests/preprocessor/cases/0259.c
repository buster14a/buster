#define S(x) #x
#define T(x) S(x) [x]
T(_Pragma("ppf one") a)
#define P(a, b) a ## b
#define Q(x, y) P(x, y) [x]
Q(_Pragma("ppf two") l, r)
#define PR _Pragma("ppf three")
T(PR b)
#define D(x) x x
D(_Pragma("ppf four"))
