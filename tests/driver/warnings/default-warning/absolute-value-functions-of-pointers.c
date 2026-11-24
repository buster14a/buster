int abs(int); long labs(long);
int *p; int arr[2]; void fn(void);
void g(void) { (void)__builtin_abs(p); (void)__builtin_labs(arr); (void)labs(arr); (void)abs(fn); (void)__builtin_llabs(p); }
