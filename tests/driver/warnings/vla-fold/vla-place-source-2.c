#define F (int)(1.0 + 2)
void f(void) { static int b[F]; extern int c[F]; static int d[sizeof(int[F])]; }
