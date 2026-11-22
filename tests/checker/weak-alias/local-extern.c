extern int alias;
int target = 1;
#pragma weak alias = target
int use(void) { extern int alias; return alias; }
