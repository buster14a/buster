#define h(x) _Pragma(x)
a h("once") \
    b
#define g(x) _Pragma(x) y
c g("once") \
d
