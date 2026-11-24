int a_func(void);
int b_func(void) { return 9; }
int main(void) { return a_func() == 10 ? 0 : 1; }
