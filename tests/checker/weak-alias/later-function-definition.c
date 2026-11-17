int target(void) { return 1; }
#pragma weak alias = target
int alias(void) { return 2; }
int use(void) { return alias(); }
