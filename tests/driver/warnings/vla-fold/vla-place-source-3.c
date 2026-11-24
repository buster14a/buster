#define F (int)(1.0 + 2)
struct S { int m[sizeof(int[F])]; };
