int target(void) __attribute__((ifunc("missing")));
#pragma weak alias = target
