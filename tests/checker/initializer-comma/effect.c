struct S { int x; } s;
void effect(void);
int *r = (effect(), s);
