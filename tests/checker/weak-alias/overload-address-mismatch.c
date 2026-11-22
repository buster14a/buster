long alias(int);
int target(double value) { return value; }
#pragma weak alias = target
int (*pointer)(const char*) = alias;
int later(void) { return undeclared_later; }
