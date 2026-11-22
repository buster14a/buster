static long alias;
int target(void) { return 1; }
#pragma weak alias = target
int use(void) { int alias = 7; return alias; }
