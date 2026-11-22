int target = 1;
#pragma weak alias = target
static int alias;
int use(void) { return alias; }
