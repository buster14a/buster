int target(void) { return 1; }
int alias(int);
#pragma weak alias = target
void *p = alias;
int g(void) { return alias != 0; }
