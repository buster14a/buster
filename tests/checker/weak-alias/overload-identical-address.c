int alias(void);
int target(void) { return 1; }
#pragma weak alias = target
int (*pointer)(void) = alias;
int (*assigned)(void);
void set(void) { assigned = alias; }
int take(int (*)(void));
int pass(void) { return take(alias) + alias(); }
