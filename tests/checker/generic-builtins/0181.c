int printf(const char*, ...);
int pf(void* ctx, const char* f, ...);
int kr();
struct S { int a; unsigned b : 2; struct { char c; } in; int arr[2]; };
union U { int a; };
void f(struct S* s, const union U* u, int (*p)(const char*, ...))
{
__builtin_dump_struct(s, printf);
__builtin_dump_struct(u, p);
__builtin_dump_struct((s), (printf));
__builtin_dump_struct(s, pf, (void*)0);
__builtin_dump_struct(s, pf, s);
__builtin_dump_struct(s, kr, 1, 2.0);
}
