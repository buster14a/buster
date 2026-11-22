int target(void) { return 1; }
#pragma weak alias = target
static int alias(void);
int use(void) { return alias(); }
