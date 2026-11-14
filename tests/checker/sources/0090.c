enum A : int __attribute__((unused)); enum B : int __attribute__((aligned(8))) { B0 };
typedef enum C : __attribute__((unused)) unsigned __attribute__((unused)) char __attribute__((mode(DI))) { C0 } T; T t = C0;
_Static_assert(sizeof(enum B) == 4 && _Alignof(enum B) == 4 && sizeof(T) == 1, "B T");
enum D : short; enum D d; enum D : short; void f(void) { enum D; enum D *p = 0; (void)p; }
