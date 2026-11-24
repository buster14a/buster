struct S{int a;struct S*n;}s;union U{int a;float f;}u;typedef struct S T;typedef const struct S CT;T t;CT ct;
struct S h(void);struct W{struct S in;}w,*wp;
void g(struct S*p){(void)(struct S)s;(void)(union U)u;(void)(const struct S)s;(void)(T)t;(void)(CT)s;(void)(const T)t;
(void)sizeof((T)t);(void)(int)t;(void)(int)ct;(void)(long)u;(void)(char*)s;(void)(int)h();(void)(int)w.in;
(void)(int)wp->in;(void)(float)p[0];(void)(_Bool)*p;(void)(int)(s);}
