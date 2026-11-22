int target(void);
int alias(void);
#pragma weak alias = target
#pragma weak implicit_alias = target
int target(void) { return 3; }
