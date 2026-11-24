typedef float v4 __attribute__((ext_vector_type(4)));
typedef v4 Alias;
struct S { __typeof__(((v4){0}).xy) wrong; Alias member; };
typedef __typeof__(((v4){0}).xy) T;
void f(v4 v, struct S s) { (void)&v.xx; s.member++; }
typedef float v2 __attribute__((ext_vector_type(2)));
void g(v4 v) { (void)&v.xx; }
