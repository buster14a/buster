/* Investigation inputs, not registered production fixtures. C17, no UB or
 * assumptions about evaluation order. Observe only final named values. */
int printf(char const *, ...);
struct Pair { int x, y; };
struct Outer { struct Pair a; int z, w; };
struct Rows { int a[2][2]; };
struct Pointers { int *p, *q; };
struct PointerOuter { struct Pointers a; int tail; };
static int anchor;
#if CASE == 0
#define TYPE struct Outer
#define INIT { .a.x = 1, 2 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 0, 0
#elif CASE == 1
#define TYPE struct Outer
#define INIT { .a.y = 2, 3 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 0, 2, 3, 0
#elif CASE == 2
#define TYPE struct Outer
#define INIT { .a.x = 1, 2, 3, 4 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 3, 4
#elif CASE == 3
#define TYPE struct Outer
#define INIT { .a = {1, 2}, .a = {.x = 3} }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 3, 0, 0, 0
#elif CASE == 4
#define TYPE struct Outer
#define INIT { .a = {1, 2}, .a.x = 3 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 3, 2, 0, 0
#elif CASE == 5
#define TYPE struct Outer
#define INIT { .a.x = 3, .a = {1, 2} }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 0, 0
#elif CASE == 6
#define TYPE struct Outer
#define INIT { .a = {1, 2}, .z = 3, .w = 4 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 3, 4
#elif CASE == 7
#define TYPE struct Outer
#define INIT { .a.x = 1, .a.y = 2, .z = 3, .w = 4 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 3, 4
#elif CASE == 8
#define TYPE struct Rows
#define INIT { .a[0][0] = 1, 2, 3, 4 }
#define OBS obj.a[0][0], obj.a[0][1], obj.a[1][0], obj.a[1][1]
#define EXPECT 1, 2, 3, 4
#elif CASE == 9
#define TYPE struct Rows
#define INIT { .a[0] = {1, 2}, .a[0][0] = 3 }
#define OBS obj.a[0][0], obj.a[0][1], obj.a[1][0], obj.a[1][1]
#define EXPECT 3, 2, 0, 0
#elif CASE == 10
#define TYPE struct Rows
#define INIT { .a[0] = {1, 2}, .a[0] = {3} }
#define OBS obj.a[0][0], obj.a[0][1], obj.a[1][0], obj.a[1][1]
#define EXPECT 3, 0, 0, 0
#elif CASE == 11
#define TYPE struct Rows
#define INIT { .a = {{1, 2}, {3, 4}} }
#define OBS obj.a[0][0], obj.a[0][1], obj.a[1][0], obj.a[1][1]
#define EXPECT 1, 2, 3, 4
#elif CASE == 12
#define TYPE struct PointerOuter
#define INIT { .a = {&anchor, &anchor}, .a = {.p = 0} }
#define OBS obj.a.p != 0, obj.a.q != 0, obj.tail, 0
#define EXPECT 0, 0, 0, 0
#elif CASE == 13
#define TYPE struct PointerOuter
#define INIT { .a = {&anchor, &anchor}, .a.p = 0 }
#define OBS obj.a.p != 0, obj.a.q == &anchor, obj.tail, 0
#define EXPECT 0, 1, 0, 0
#elif CASE == 14
#define TYPE struct PointerOuter
#define INIT { .a.p = &anchor, .a.q = &anchor, .a.p = 0 }
#define OBS obj.a.p != 0, obj.a.q == &anchor, obj.tail, 0
#define EXPECT 0, 1, 0, 0
#elif CASE == 15
#define TYPE struct Outer
#define INIT { 1, 2, 3, 4 }
#define OBS obj.a.x, obj.a.y, obj.z, obj.w
#define EXPECT 1, 2, 3, 4
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
#endif
    int actual[4] = { OBS };
    int expected[4] = { EXPECT };
    int failed = 0;
    for (int i = 0; i < 4; i += 1)
    {
        failed |= (actual[i] != expected[i]) << i;
    }
    printf("%d %d %d %d\n", actual[0], actual[1], actual[2], actual[3]);
    return failed;
}
