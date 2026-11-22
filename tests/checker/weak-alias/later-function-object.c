int target(void) { return 1; }
#pragma weak alias = target
extern int alias;
int later(void) { return undeclared_later; }
