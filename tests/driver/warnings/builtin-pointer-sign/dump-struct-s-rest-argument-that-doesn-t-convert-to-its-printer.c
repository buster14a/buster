struct S { int a; };
union U { int a; };
typedef struct { int a; } T;
int p(int* x, const char* f, ...);
int q(int y, int* x, const char* f, ...);
int r(long* x, const char* f, ...);
void g(struct S* s, double z) { __builtin_dump_struct(s, p, z); }
void h(union U* s, struct S v) { __builtin_dump_struct(s, &q, 1 + 2, v); }
void k(T* s, int *ip) { __builtin_dump_struct(s, p, 5); }
void m(struct S* s, int (*fp)(int*, const char*, ...)) { __builtin_dump_struct(s, fp, 1.5); }
void n(struct S* s, int *ip) { __builtin_dump_struct(s, r, ip); }
