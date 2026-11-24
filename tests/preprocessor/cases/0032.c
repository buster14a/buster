#define V(...) __VA_ARGS__ ## _x
#define F() a ## b
#define W(x) V(x) F()
V(a) V() F() W(b) W()
