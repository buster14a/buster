static int implementation(void) { return 7; }
static int (*resolve(void))(void) { return implementation; }
int target(void) __attribute__((ifunc("resolve")));
#pragma weak alias = target
