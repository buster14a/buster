enum Small : signed char { s0 }; enum Big : short { b0 = 30000, b1 = 5, b2 = -200 }; enum Bigger : int { i0 };
enum USmall : unsigned char { u0 };
void f1(enum Big x){ enum Small r = x; (void)r; }
void f2(void){ enum Small r = b0; (void)r; }
void f3(void){ enum Small r = b1; (void)r; }
void f4(void){ enum Small r = b2; (void)r; }
void f5(void){ enum USmall r = b2; (void)r; }
void f6(void){ enum USmall r = b1; (void)r; }
void f7(enum Small s){ enum USmall r = s; (void)r; }
void f8(enum USmall s){ enum Small r = s; (void)r; }
void f9(enum Small s){ enum Big r = s; (void)r; }
enum E { A }; enum F { B = -1 }; enum G { C = 0x7fffffff, D = -1 };
void g1(void){ enum E e = B; (void)e; }  void g2(enum F f){ enum E e = f; (void)e; }
void g3(enum E x){ enum F e = x; (void)e; }  void g4(void){ enum E e = C; (void)e; }
enum Big2 : short { b3 = 128, b4 = 255 }; enum UBig : unsigned short { ub = 128 };
void h1(void){ enum Small r = b3; (void)r; }
void h2(void){ enum Small r = ub; (void)r; }
void h3(void){ enum USmall r = b4; (void)r; }
struct BF { enum Big x : 4; enum Big y : 9; };
void h4(struct BF *p){ enum Small r = p->x; enum Small q = p->y; (void)r; (void)q; }
void h5(enum Big x, int c){ enum Small r = c ? b1 : x; (void)r; }
