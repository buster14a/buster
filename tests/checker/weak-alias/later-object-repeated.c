long target = 1;
#pragma weak alias = target
extern int alias;
extern float alias;
int alias = 2;
int later(void) { return undeclared_later; }
