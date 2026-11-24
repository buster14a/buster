typedef int I;
enum { E = (I)3 << 30, F = 1 << 30, G = (short)1 << 31 };
int b[((__int128)1 << 127) < 0 ? 1 : 2];
int c[((long)1 << 63) < 0 ? 1 : 2];
_Static_assert((1 << 31) < 0, "");
struct S { int x[(1 << 31) < 0]; };
void f(void) { enum { H = 2 << 30 }; int y[(1 << 31) < 0 ? 1 : 2]; (void)y; }
