int target(void);
#pragma weak alias = target
int implementation(void) { return 7; }
int target(void) __attribute__((alias("implementation")));
