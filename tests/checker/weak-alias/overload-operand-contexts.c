int target(void) { return 1; }
int alias(int);
#pragma weak alias = target
void *h(void) { return alias; }
void k(void) { void *q; q = alias; }
int g(void) { return alias != 0; }
int m(void) { return !alias; }
void n(void) { (void)alias; }
