int target(void);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#pragma weak alias = target
#pragma clang diagnostic pop
int implementation(void) { return 7; }
int target(void) __attribute__((weak, alias("implementation")));
