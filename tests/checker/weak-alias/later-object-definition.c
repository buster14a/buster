int target = 1;
#pragma weak alias = target
int alias = 2;
int use(void) { return alias + target; }
