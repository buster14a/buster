int target(void);
#pragma weak alias = target
int use(void) { return target(); }
