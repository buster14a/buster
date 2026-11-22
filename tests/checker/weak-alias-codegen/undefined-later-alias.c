int target(void);
#pragma weak alias = target
int target(void) __attribute__((alias("missing")));
