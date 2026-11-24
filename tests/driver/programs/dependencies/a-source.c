int b_func(void);
int a_func(void) { return b_func() + 1; }
