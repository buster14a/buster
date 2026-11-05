typedef int I4 __attribute__((ext_vector_type(4)));
typedef unsigned U4 __attribute__((ext_vector_type(4)));
typedef float F4 __attribute__((ext_vector_type(4)));
typedef _Bool B4 __attribute__((ext_vector_type(4)));
I4 noti(I4 a) { return !a; }
I4 notf(F4 a) { return !a; }
I4 andi(I4 a, I4 b) { return a && b; }
I4 ori(I4 a, I4 b) { return a || b; }
I4 andf(F4 a, F4 b) { return a && b; }
I4 orf(F4 a, F4 b) { return a || b; }
I4 andscalar(I4 a, int b) { return a && b; }
I4 scalarand(int a, I4 b) { return a && b; }
I4 scalaror(int a, I4 b) { return a || b; }
I4 selecti(I4 a, I4 b, I4 c) { return a ? b : c; }
I4 selectu(U4 a, I4 b, I4 c) { return a ? b : c; }
F4 selectf(I4 a, F4 b, F4 c) { return a ? b : c; }
I4 selectb(B4 a, I4 b, I4 c) { return a ? b : c; }
B4 selectbb(B4 a, B4 b, B4 c) { return a ? b : c; }
I4 selects(I4 a, int b, int c) { return a ? b : c; }
