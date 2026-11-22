int target = 1;
#pragma weak alias = target
int alias(void);
int later(void) { return undeclared_later; }
