enum E { A, B };
void arg(enum E);
void argi(int);
enum E fret(void) { return 3.5; }
_Atomic int gy = 7.5;
_Atomic char gc = 300;
void f(void) {
  int x = 1.5; enum E e = 2.5; _Atomic int y = 3.5; _Atomic char z = 4.5;
  arg(4.5); argi(4.5); e = 5.5; y = 6.5; const int c = 8.5; volatile int v = 9.5;
  _Bool b = 0.5; _Atomic _Bool ab = 0.5; enum E e2 = 300; _Atomic char k = 300; char d = 300; z = 500;
  (void)x; (void)e; (void)y; (void)z; (void)c; (void)v; (void)b; (void)ab; (void)e2; (void)k; (void)d;
}
