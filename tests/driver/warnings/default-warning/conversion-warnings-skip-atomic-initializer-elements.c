_Atomic char ga[1] = { 1.5 };
struct S { _Atomic char c; int i; };
struct S gs = { 1.5, 2.5 };
void f(void) {
  _Atomic char a[2] = { 1.5, 2 };
  struct S s = { 3.5, 4.5 };
  struct { _Atomic int x; double d; } t = { .x = 5.5, .d = 1 };
  (void)a; (void)s; (void)t;
}
