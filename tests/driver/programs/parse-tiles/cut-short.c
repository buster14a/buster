int f(a) __attribute__((unused)) int a; { return 0; }
int main(void) { return f(1); }
