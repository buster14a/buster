#if __has_attribute(overloadable) && __has_attribute(__overloadable__)
#define O __attribute__((overloadable))
static int pick(int x) O{return x+5;}static double pick(double x) O{return x+0.5;}static int(*selected)(int)=pick;int tested(int n){int x=n&1023;return pick(x)+(int)(pick((double)(n%9))*2)+selected(x);}
#else
int tested(int n){return n-991;}
#endif
