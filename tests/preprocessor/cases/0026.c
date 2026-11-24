#define D 1
#define F(x) D x
#define G F(g)
F(a) G
#undef D
#define D 2 3
F(b) G
#pragma push_macro("D")
#undef D
#define D 4 5 6
F(c) G
#pragma pop_macro("D")
F(d) G
#undef D
F(e) G
