int target = 7;
#pragma weak alias = target
extern int alias;
int use(void) { return alias; }
