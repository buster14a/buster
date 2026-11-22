#pragma weak alias = target
int alias(void);
int before(void) { return alias(); }
int target(void) { return 3; }
int after(void) { return alias(); }
