int a_func(void);
int foo(void) { return 11; }
int main(void) { return a_func() == 11 ? 0 : 1; }
