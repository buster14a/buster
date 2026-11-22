int target = 1;
#pragma weak alias = target
extern const int alias;
int later(void) { return undeclared_later; }
