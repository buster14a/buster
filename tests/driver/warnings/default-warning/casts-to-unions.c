typedef short ms;
union U{int*p;long*q;char c;int i;};struct S{int a;};
int f(int n,const int k,int arr[2]){union U u=(union U)&n;union U v=(union U)arr;(void)(union U)(char)n;
(void)(union U)k;(void)v;return *u.p;}
int g(ms s,double d){(void)(union U)s;(void)(union U)d;(void)(struct S)s;return 0;}
