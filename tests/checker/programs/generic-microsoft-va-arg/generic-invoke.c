static int calls;static int next(int v){calls++;return v;}static int twice(int x){return 2*x;}static int sum(int n,...){return n;}
struct S{int (*fn)(int);};
int tested(int n){int (*p)(int)=twice;struct S s={twice};return __builtin_invoke(twice,next(n))+__builtin_invoke(p,3)*10+__builtin_invoke(*p,1)*100+
__builtin_invoke(s.fn,next(2))*1000+__builtin_invoke(sum,7,1.5,"x")*10000+calls;}
