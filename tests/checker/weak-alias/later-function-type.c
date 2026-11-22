long target(int value) { return value; }
#pragma weak alias = target
int alias(int value);
int later(void) { return undeclared_later; }
