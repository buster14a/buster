int f(void) { static int n = 5; return n++; }
int fn __asm__("f.n") = 9;
int main(void) { return f() + fn; }
