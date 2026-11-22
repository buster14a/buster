#pragma weak alias = target
extern int alias;
int before(void) { return alias; }
int target = 3;
int after(void) { return alias; }
