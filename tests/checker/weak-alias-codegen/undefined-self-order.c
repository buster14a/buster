int target(void);
#pragma weak target = target
int ordinary(void) __attribute__((alias("ordinary")));
