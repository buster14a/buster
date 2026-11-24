#pragma clang fp eval_method(extended)
float x;
enum E { X = 0.1 != 0.1f, Y = 0.5 == 0.1f, Z = sizeof(x == 0.1L) };
int f(void) { return (x == 0.1) + (x != 0.1L); }
