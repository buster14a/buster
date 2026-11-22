long alias(int);
int target(int value) { return value; }
#pragma weak alias = target
int use(void) { return alias(1); }
