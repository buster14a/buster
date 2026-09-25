/* Reduced C17 cases: no calls, no arithmetic edge cases, only named fields. */
struct One { int a[1]; };
struct Two { int a[2]; };
struct Tail { int a[2]; int tail; };
struct Pair { int x, y; };
struct Nested { struct Pair a; int tail; };
struct Pointers { int *a[1]; };
static int anchor;
#if CASE == 0
#define TYPE struct One
#define INIT { .a = {1}, .a[0] = 2 }
#define CHECK (obj.a[0] != 2)
#elif CASE == 1
#define TYPE struct One
#define INIT { .a[0] = 2, .a = {1} }
#define CHECK (obj.a[0] != 1)
#elif CASE == 2
#define TYPE struct One
#define INIT { .a = {1}, .a = {2} }
#define CHECK (obj.a[0] != 2)
#elif CASE == 3
#define TYPE struct One
#define INIT { .a[0] = 1, .a[0] = 2 }
#define CHECK (obj.a[0] != 2)
#elif CASE == 4
#define TYPE struct Two
#define INIT { .a[0] = 1, .a = {[1] = 2} }
#define CHECK ((obj.a[0] != 0) | ((obj.a[1] != 2) << 1))
#elif CASE == 5
#define TYPE struct Two
#define INIT { .a = {1, 2}, .a[0] = 3 }
#define CHECK ((obj.a[0] != 3) | ((obj.a[1] != 2) << 1))
#elif CASE == 6
#define TYPE struct Tail
#define INIT { .a[0] = 1, 2 }
#define CHECK ((obj.a[0] != 1) | ((obj.a[1] != 2) << 1) | ((obj.tail != 0) << 2))
#elif CASE == 7
#define TYPE struct Tail
#define INIT { .a[1] = 2, 3 }
#define CHECK ((obj.a[0] != 0) | ((obj.a[1] != 2) << 1) | ((obj.tail != 3) << 2))
#elif CASE == 8
#define TYPE struct Two
#define INIT { .a[0] = 1, 2 }
#define CHECK ((obj.a[0] != 1) | ((obj.a[1] != 2) << 1))
#elif CASE == 9
#define TYPE struct Tail
#define INIT { .a[0] = 1, .a[1] = 2, .tail = 3 }
#define CHECK ((obj.a[0] != 1) | ((obj.a[1] != 2) << 1) | ((obj.tail != 3) << 2))
#elif CASE == 10
#define TYPE struct Tail
#define INIT { 1, 2, 3 }
#define CHECK ((obj.a[0] != 1) | ((obj.a[1] != 2) << 1) | ((obj.tail != 3) << 2))
#elif CASE == 11
#define TYPE struct Pointers
#define INIT { .a = {&anchor}, .a[0] = 0 }
#define CHECK (obj.a[0] != 0)
#elif CASE == 12
#define TYPE struct Two
#define INIT { .a = {1, 2}, .a = {3} }
#define CHECK ((obj.a[0] != 3) | ((obj.a[1] != 0) << 1))
#elif CASE == 13
#define TYPE struct Tail
#define INIT { .a = {1, 2}, .tail = 3 }
#define CHECK ((obj.a[0] != 1) | ((obj.a[1] != 2) << 1) | ((obj.tail != 3) << 2))
#elif CASE == 14
#define TYPE struct Nested
#define INIT { .a.x = 1, 2 }
#define CHECK ((obj.a.x != 1) | ((obj.a.y != 2) << 1) | ((obj.tail != 0) << 2))
#elif CASE == 15
#define TYPE struct Nested
#define INIT { .a = {1, 2}, .a.x = 3 }
#define CHECK ((obj.a.x != 3) | ((obj.a.y != 2) << 1))
#else
#error unknown CASE
#endif
#if STORAGE == 1
static TYPE obj = INIT;
#endif
int main(void)
{
#if STORAGE == 0
    TYPE obj = INIT;
#elif STORAGE == 2
    TYPE obj = (TYPE)INIT;
#endif
    return CHECK;
}
