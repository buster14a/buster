struct S { unsigned char c : 4; signed char d : 2; signed char e : 8; int i : 3; unsigned char f : 8; };
struct S g = { 300, 200, 200, 9, 256 };
void f(void) {
    struct S t = { 300, 200, 200, 9, 256 };
    (void)t;
}
