int object;
extern int other;
int function(int);
int a[4], b[2];

int nonnull = &object != 0;
int null_function = 0 == function;
const char *selected = "abc" == 0 ? "wrong" : "right";
int bound[&object != (void *)0 ? 1 : -1];
struct Bits { int width : (&object != 0) + 1; };
enum { different = &other != &object };
_Static_assert(different && &other != 0, "object identity");
_Static_assert(((void *)&object == (void *)function) == 0, "function identity");
_Static_assert((0 != &"abc"[1]) + 1 == 2, "displaced literal");
_Static_assert((int[]){ 1, 2 } != 0 && &(int){ 1 } != 0, "compound literals");
_Static_assert(&a[3] != &b[1] && &a[3] == a + 3, "interior addresses");
_Static_assert(&a[4] != &b[1] && &a[4] != &b[2], "one past and a displaced pointer");
_Static_assert((void *)&object != (void *)"abc", "declared object and literal");
_Static_assert("abc" != &"abc"[1] && "a\0b" != "a\0c", "overlapping bytes differ");
