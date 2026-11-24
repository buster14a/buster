typedef short S; typedef int *IP; typedef void *VP; typedef const void *CVP;
enum E { A }; enum E e; S s; char c; _Bool bo; int i; long l; unsigned _BitInt(63) u63; _BitInt(7) b7; const short cs; volatile char vc; __int128 i128;
struct R { int f : 3; } r;
void f(void) {
(void)(void*)s; (void)(IP)c; (void)(VP)i; (void)(void*)e; (void)(void*)bo; (void)(void*)l; (void)(void*)u63; (void)(void*)b7; (void)(CVP)i;
(void)(void*)(char)1; (void)(void*)(s+1); (void)(void*)cs; (void)(void*)vc; (void)(const void*)c; (void)sizeof((void*)c);
(void)(_Atomic(int*))c; (void)(void(*)(void))c; (void)(int(*)[2])c; (void)(void*)'a'; (void)(void*)(short)i; (void)(char*)(c);
(void)(void*)(char)(1 ? 2 : i); (void)(void*)(enum E)c; (void)(void*)__extension__ c; (void)(void*)r.f; (void)(int * const)c; (void)(void*)i128;
(void)(void*)(c = 1); (void)(void*)c++; (void)(void*)(i, c); (void)(void*)(int)1.5; (void)(void*)(1 + 2); (void)(void*)(char)i; }
void *g = (void*)(char)3;
