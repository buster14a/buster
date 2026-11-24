struct A { int x; int y; };
union U { int x; double d; };
_Atomic(struct A) a;
_Atomic(struct A) *p;
_Atomic(union U) u;
_Atomic(struct A *) ordinary_pointer;
_Atomic(struct A) array[1];
extern _Atomic(struct A) incomplete[];
const _Atomic(struct A) fixed;
int f(void) { return a.x + p->y + u.x; }
void g(void) { a.y = 3; fixed.x = 2; }
int pointer(void) { return ordinary_pointer->x; }
int decayed(void) { return array->x; }
int incomplete_decay(void) { return incomplete->x; }
int size = sizeof(a.x);
int bound[sizeof(p->x)];
typedef __typeof__(p->x) Member;
_Static_assert(__builtin_types_compatible_p(Member, int), "member type");
